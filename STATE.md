# STATE

Read this file before any work in this repository and update it in the same PR as the work.
The PR evidence check fails when STATE.md exists and a PR neither updates it nor says
"no change" with a reason. Keep it under 60 lines; facts only, each with a date or SHA.

## Repository

- Type and lifecycle: firmware, active
- Readiness: R0 (evidence: Repository quality run 38303061893 on main f96014d, 2026-10-12;
  no firmware build or test runs on main yet)
- Owner role: platform-lead (maintainers.yaml); electrical-owner owns the base controller firmware
- Verification: `python -m unittest discover -s tests -v` and the build workflows of PR #23
  (not on main); Repository quality last PASS on main at f96014d (2026-10-12)
- Harness rollout: step (a) in this PR; steps (b) to (e) not started (rollout/README.md)

## Firmware baselines

- Gate A: Teensy 4.0 linorobot2 overlay, deployed firmware of 2026-06-29, base
  linorobot2_hardware jazzy `aaf9d59` (NOTICE.md); built in CI only on PR #23 (teensy@5.0.0).
- STM32 baseline: STM32H723ZG CubeMX project (CubeMX 6.18.1, STM32Cube FW_H7 V1.13.0, HAL
  1.11.6) in PR #22 at e49f0bb; `boards/stm32/` on main holds only .gitkeep (2026-10-24).

## Current work

| Work package | Branch or PR | Status | Next step |
|---|---|---|---|
| E-01 hardware chain is the primary stop | #18 e01-safety-power | draft | platform-lead review |
| 2.0 controller I/O, P-03 rev19.6 item 22 | #19 docs/base-controller-2-0-io-p03-19-6 | in review | owner review |
| STM32H723 CubeMX project | #22 build/add-stm32cubemx-project | in review | owner review; vendored HAL edits (e49f0bb) |
| STM32 and Teensy builds, host tests | #23 ci/firmware-build-and-tests | draft | review, then merge |
| Shared rules v2, STATE.md | governance/agent-rules-state (this PR) | draft | owner adopt/adapt/reject |

## Decisions this repository implements

BASE-CONTROLLER-IO, BASE-CONTROLLER-GATES, IMU-TOPIC-OWNERSHIP, BMS-FAULTS, LIFT (CAN3 lift
interface), COMPUTE (register openAMRobot/.github decisions.yaml at 1c3f516, 2026-10-24).

## Known gaps

- E-01 safety state machine not implemented, #17; lift motion interlock, #20.
- Near-field issue #11 still states 4 TF-Luna and 2 MB7040 (superseded by BASE-CONTROLLER-IO).
- Local .github/PULL_REQUEST_TEMPLATE.md overrides the organization template; alignment
  (rollout step (a)) left to the owner (2026-10-24).

## Not verified

- No firmware has been flashed or run on hardware by CI (2026-10-24).
- STM32 firmware has not run on a NUCLEO-H723ZG under any recorded test (2026-10-24).
