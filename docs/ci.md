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

## Layers 2 to 4

The nightly integration workflow, the hardware-in-the-loop bench workflow and the release
evidence record are described in the sections added with them.
