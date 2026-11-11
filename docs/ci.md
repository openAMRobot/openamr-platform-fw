# Continuous integration

OpenAMRobot CI has four layers:

- Pull request checks prove that each change is internally valid. Fast, deterministic, required before merge, no physical hardware.
- Nightly integration checks prove that the repositories still agree, across pinned commits.
- Hardware-in-the-loop checks prove that the real electronics behave as designed, on a controlled self-hosted bench runner.
- Release evidence proves which exact system was tested.

A passing build or host test proves that the firmware compiles and that the tested functions
behave as the tests state on a PC. It is not simulation, bench acceptance or safety evidence.
SKIP or NOT RUN is never reported as PASS. No CI job authors or changes E-stop, brake, contactor,
watchdog, motor-enable or charge-inhibit logic, and no automated job drives a safety output.

## Layer 1: pull request checks

GitHub-hosted runners only; no secrets, no hardware.

| Workflow | Runs | Proves |
|---|---|---|
| Firmware STM32H723 build | Debug and Release with the Arm GNU toolchain, warnings in project code fail, Ethernet DMA descriptors in RAM_D2 (`tools/ci/check_dma_placement.py`), only where `boards/stm32/` holds a project | the CubeMX project builds and links with the descriptors where Ethernet DMA can reach them |
| Firmware Teensy Gate A build | PlatformIO build of the linorobot2 overlay on linorobot2_hardware `aaf9d59`, platform `teensy@5.0.0` pinned in the CI copy of `platformio.ini` | the Gate A firmware builds |
| Firmware host tests | `python -m unittest discover -s tests -v` | the encoder calibration tools behave as tested |
| Firmware host C/C++ tests | `tests/host`: CMake, CTest and GoogleTest (`libgtest-dev` 1.14.0), `-Wall -Wextra -Werror`, AddressSanitizer and UndefinedBehaviorSanitizer; `tests/host/mutation_check.sh`; `tools/ci/check_skips.py`; cppcheck 2.13.0 (`tools/ci/run_cppcheck.py`); clang-format 18 on `firmware/` (`tools/ci/check_format.sh`) | the overlay PID clamps, bleeds the integral after saturation (compared with upstream), resets; each mutation of the PID fails a test; every skip names an issue; no unsuppressed cppcheck finding; the portable core is formatted |

Rules the checks enforce:

- An empty suite fails (`ctest --no-tests=error`, `grep '^Ran [1-9]'`).
- `GTEST_SKIP`, `DISABLED_`, `skip` and `expectedFailure` name an open issue on the same line.
- Every entry of `tools/ci/cppcheck-suppressions.txt` has a `# reason:` line.
- cppcheck analyses the Gate A overlay, `boards/stm32/*/Core/Src` and `firmware/`, never vendored
  STMicroelectronics `Drivers/` or upstream linorobot2 files. A part with no sources is reported
  NOT RUN in the job summary.
- clang-format checks only `firmware/` (style `firmware/.clang-format`); the overlay, CubeMX-generated
  and vendored code are not reformatted.

### Run locally (Ubuntu 24.04)

```
sudo apt-get install libgtest-dev cmake ninja-build cppcheck clang-format-18
git clone https://github.com/linorobot/linorobot2_hardware ../linorobot2_hardware
git -C ../linorobot2_hardware checkout aaf9d59cd18c0cd1905be6fdae9ea5c99961a766
cmake -S tests/host -B build/host -G Ninja -DLINOROBOT2_HARDWARE_DIR=../linorobot2_hardware
cmake --build build/host && ctest --test-dir build/host --output-on-failure --no-tests=error
bash tests/host/mutation_check.sh ../linorobot2_hardware
python3 -m unittest discover -s tests/static -v
python3 tools/ci/run_cppcheck.py && CLANG_FORMAT=clang-format-18 bash tools/ci/check_format.sh firmware
```

The STM32 workflow also checks the CubeMX `.ioc` against the controller I/O contract of
openamr-platform-hw and the vendored ST files against their upstream release
(`tools/ci/check_ioc_contract.py`, `tools/ci/check_vendor_provenance.py`, added with #28).

## Layer 2: nightly integration checks

Workflow `.github/workflows/firmware-nightly.yml` ("Firmware nightly"), daily at 02:47 UTC and on
manual dispatch; never on pull requests. GitHub-hosted runners only, no secrets.

| Job | Runs | Check names in the evidence |
|---|---|---|
| prepare | checks out openamr-platform-fw `main`, openamr-platform-hw `main`, openAMRobot/.github `main`; records the three SHAs | |
| host | `tools/ci/nightly_fw.sh`: Python host tests, C/C++ host tests, mutation check, skip tracking, cppcheck, clang-format | `python-host-tests`, `cpp-host-tests`, `mutation-check`, `skip-tracking`, `cppcheck`, `clang-format` |
| teensy | Gate A PlatformIO build (`teensy@5.0.0`, linorobot2_hardware `aaf9d59`); uploads `firmware.hex` and `firmware.elf` | `teensy-build` |
| stm32 | when `boards/stm32/*/*.ioc` is on `main`: `tools/ci/nightly_stm32.sh` for Debug and Release; RAM_D2 check; in Debug the `.ioc` against openamr-platform-hw `main` and the vendor provenance check; uploads ELF, map and HEX | `stm32-build-debug`, `stm32-build-release`, `ram-d2-debug`, `ram-d2-release`, `ioc-contract`, `vendor-provenance` |
| renode | experimental, not a gate (see below) | `renode-boot` (marked `experimental`) |
| evidence | merges every job's results, checks that `write_evidence.py` and its schema are identical to openamr-platform-hw, writes `ci-evidence.json` and `SHA256SUMS`, uploads both with the firmware as `ci-evidence` (90 days) | `evidence-format-sync` |

A tool that is not on `main` yet is reported NOT RUN with that reason. With no STM32 project on
`main`, the STM32 checks are `NOT RUN: no STM32 project on openamr-platform-fw main`. The run
fails after the evidence is uploaded when any gating check failed.

### Renode boot (experimental, not a gate)

`tools/ci/renode_boot.py` boots the STM32 Debug ELF in Renode 1.16.1 (pinned release, SHA-256
checked) for a fixed virtual time and passes when the CPU is in `main()` and never ended in a
fault or error handler. Renode has no STM32H723 platform, so it uses Renode's
`platforms/cpus/stm32h743.repl` plus `tests/renode/stm32h723-additions.repl` (FDCAN3 at 0x4000D400) <!-- decision-allow: BASE-CONTROLLER-IO Renode platform file name used for emulation, not the base controller -->
and a PWR CSR1 ACTVOSRDY read stub. The job summary lists every register access the platform
does not model (RCC registers, UART RX inversion and others). It runs with `continue-on-error`
and is recorded with `experimental: true`, so it never decides the evidence result. It is not
simulation evidence.

## Layer 3: hardware-in-the-loop bench

Workflow `.github/workflows/firmware-hil-bench.yml` ("Firmware HIL bench"): manual dispatch only,
`runs-on: [self-hosted, openamr-bench]`, environment `hil-bench`, inputs `firmware_sha` and
`hardware_revision`. It flashes the image the nightly recorded for that commit on `main`, runs the
scripts in `tools/hil/`, and writes a bench record whose manual checklist a person completes and
signs. See [hil-bench.md](hil-bench.md). No job on a GitHub-hosted runner or on `pull_request`
reaches the bench, hardware or secrets.

## Release evidence: ci-evidence.json

The nightly writes `ci-evidence.json` in the format defined in openamr-platform-hw
(`docs/ci.md`, "Release evidence"; schema `tools/ci/ci-evidence.schema.json`, kept identical here
and checked nightly by `evidence-format-sync`). For firmware, `artifacts[]` lists every built
image (Teensy HEX and ELF, STM32 ELF, map and HEX per preset) with its SHA-256, and
`contract` names the openamr-platform-hw contract the `.ioc` was checked against. The HIL bench
record is separate and becomes release evidence only when signed.
