# Hardware-in-the-loop bench

Layer 3 of the CI strategy ([ci.md](ci.md)): hardware-in-the-loop checks prove that the real
electronics behave as designed, on a controlled self-hosted bench runner. This page is the bench
procedure. The bench is a scaffold today: the workflow and scripts exist, but no bench runner is
registered and the firmware does not yet print the boot banner the scripts read. Every result
of a run is recorded as it is; NOT RUN is never PASS.

## Rules

- The workflow "Firmware HIL bench" (`.github/workflows/firmware-hil-bench.yml`) runs only on
  manual dispatch, never on `pull_request`, `push` or `schedule`, so untrusted code cannot reach
  the bench. It runs on a runner with the labels `self-hosted` and `openamr-bench`, in the GitHub
  environment `hil-bench`.
- It flashes only an image that "Firmware nightly" built and recorded (SHA-256 in `SHA256SUMS`
  of the `ci-evidence` artifact) for a commit on `main`. A commit that is not on `main`, or has no
  nightly record, is refused.
- Every script in `tools/hil/` refuses to run unless `HIL_BENCH=1` is set on the runner.
  `can_loopback.py` also needs `HIL_CAN_HARNESS_ONLY=1`, the operator's statement that only the
  bench harness is on CAN1 to CAN3.
- No script drives the E-stop chain, contactors, brakes, lift power or the charger, and no script
  sends a motion command. E-stop, brakes and contactor power removal are hardwired; the firmware
  only reads and reports their status. Those paths are checked by a person with the manual
  checklist below on every run.
- Automated bench results are not safety evidence. Safety evidence is the signed manual
  checklist together with the hardwired safety-chain test record.

## Bench wiring

Controller: NUCLEO-H723ZG (MB1364, revision as recorded in the run). Pins and settings are the
controller I/O contract `interfaces/electrical/base-controller-io.yaml` in openamr-platform-hw.

| Link | Bench side | Controller side |
|---|---|---|
| ST-LINK V3 (on board) | bench PC USB | SWD, and USART3 virtual COM port on PD8/PD9 (boot banner) |
| Ethernet | bench PC NIC, static address, e.g. 192.168.1.1/24 | on-board LAN8742A (RMII), e.g. 192.168.1.10 |
| CAN1 (traction) | USB-CAN adapter `can0`, 500 kbit/s, 120 Ω terminated | FDCAN1 PD0/PD1 via transceiver; **no ZLAC8015D drivers connected** |
| CAN2 (BMS) | USB-CAN adapter `can1`, 250 kbit/s, terminated | FDCAN2 PB12/PB6 via transceiver; **no BMS connected** |
| CAN3 (upper-body auxiliary) | USB-CAN adapter `can2`, 500 kbit/s, terminated | FDCAN3 PD12/PD13 via transceiver; **no actuators connected** |
| MB7060 x4 | USB-UART taps on each sensor TX (inverted RS232 level converted by the tap) | UART4/5/7/8 RX |
| TF-Luna x2 | USB-UART taps on each sensor TX | USART2 and USART6 RX |
| ICM-42688-P (MIKROE-4237) | none | SPI4, CS PE11 |
| Safety and charging I/O (item 22) | hardwired chain as built; not connected to the bench PC | inputs as allocated; read and reported only |

The bench never connects motors, brakes, the lift, contactors or the charger to anything the
bench PC can switch.

## Runner registration (owner action)

1. On a dedicated bench PC (Ubuntu 24.04), install the GitHub Actions runner and register it for
   openAMRobot/openamr-platform-fw with the labels `self-hosted` and `openamr-bench`. Do not
   register it for any other repository and do not use it for other jobs.
2. In the runner's `.env`, set `HIL_BENCH=1`, `HIL_BOM_ISSUE`, `HIL_P03_REVISION`,
   `HIL_FIXTURE_REVISION`, `HIL_CONTROLLER_IP`, `HIL_VCP_PORT`, `HIL_MB7060_PORTS`,
   `HIL_TFLUNA_PORTS`, and `HIL_CAN_HARNESS_ONLY=1` only while the harness alone is on the buses.
3. Install `stlink-tools` (or STM32CubeProgrammer with `HIL_STM32_FLASHER=cubeprog`),
   `teensy_loader_cli`, `gh`, Python 3 with `pyserial`, `python-can` and the packages of
   `tools/ci/requirements.txt`, and bring up `can0` to `can2` with the bitrates above.
4. In repository settings, create the environment `hil-bench` with **required reviewers** (the
   electrical owner and the platform lead) and restrict it to the `main` branch. This PR does not
   change settings; the owner does this.

## Firmware contract the scripts expect (proposed)

The bench-test firmware is not written yet. The scripts expect, on the virtual COM port at
115200 baud after reset:

```
OPENAMR BOOT fw=<git sha> board=NUCLEO-H723ZG
OPENAMR RESET reason=<flags from RCC_RSR: POR, PIN, SFT, IWDG, WWDG, BOR, LPWR>
OPENAMR IMU whoami=0x47
```

and a UDP echo on port 7, and an echo of CAN test frames 0x7F1 to 0x7F3 on 0x7F9 to 0x7FB. Until
the firmware provides them, the matching bench tests FAIL, and that is what the record shows.

## Automated tests (tools/hil)

| Script | Test names | Passes when |
|---|---|---|
| `flash.py` | `flash-stm32`, `flash-teensy` | the image SHA-256 equals the nightly record and the flasher exits 0 |
| `boot_banner.py` | `boot-banner`, `reset-reason` | the banner names the expected firmware SHA and board; the reset reason after an ST-LINK reset includes PIN |
| `imu_whoami.py` | `imu-whoami` | WHO_AM_I is 0x47 (register BASE-CONTROLLER-GATES) |
| `eth_check.py` | `eth-ping`, `eth-udp-echo` | ping answers and the UDP payload comes back unchanged |
| `can_loopback.py` | `can1-loopback`, `can2-loopback`, `can3-loopback` | each test frame is echoed with the same payload |
| `sensor_frames.py` | `mb7060-N-frames`, `tfluna-N-frames` | at least 3 complete frames per sensor; TF-Luna checksums all good |

Results are appended to `build/hil/results.jsonl`; raw logs go to `build/hil/logs/`.

## Manual checklist (every run, by a person)

The workflow writes `bench-record.json` (schema `tools/hil/bench-record.schema.json`) with each
item below as PENDING and no signature. The operator performs each check on the bench, records
what was observed and the result (PASS, FAIL or NOT RUN), initials each item, and signs the
record with name, role and date. A record with a PENDING item or no signature is not bench
evidence.

1. E-stop: each E-stop button removes actuator power (traction, arms, lift) and engages the
   brakes; the Jetson, 5 V and regulated 24 V stay powered.
2. RESET short press: the PNOZ monitored reset works only after the E-stop is released; motion
   still needs a command.
3. RESET 5 s hold: the controller requests the Jetson reboot, then resets itself.
4. Brake path: brakes engage with actuator power removed and release only with actuator power.
5. Lift power path: the E-stop removes lift power and the firmware cannot restore it.
6. Watchdog: stopping `/cmd_vel` stops the wheels within the configured timeout (observed; the
   timeout is not changed for the test).
7. Communication loss: unplugging Ethernet stops motion and the controller reports the loss.
8. Charger interlock: with the charge relay energised, actuators cannot be enabled; a RESET is
   needed after charging.
9. Fault injection: a BMS fault frame on CAN2 stops motion and locks; a disconnected sensor is
   reported.
10. Y32, E-stop spare NO, RESET NC and charger fault inputs are reported with the right state.

## Bench record

| Field | Source |
|---|---|
| `firmware_sha`, `firmware_image_sha256` | workflow input; SHA-256 from the nightly `SHA256SUMS` |
| `hardware_revision` | workflow input |
| `bom_issue`, `p03_revision`, `fixture_revision` | runner environment (`HIL_BOM_ISSUE`, `HIL_P03_REVISION`, `HIL_FIXTURE_REVISION`) |
| `operator` | the GitHub login that dispatched the run |
| `date`, `run_url` | the run |
| `tests[]` | automated results (`test`, `status`, `detail`, `log`) |
| `raw_logs[]` | every file under `build/hil/logs` |
| `manual_checklist[]` | the items above, completed by the operator |
| `signature` | `null` until the operator signs (`name`, `role`, `date`) |

The record is uploaded as the artifact `bench-record` (90 days). The signed copy is attached to
the work-package issue and named in the release evidence by its SHA-256.
