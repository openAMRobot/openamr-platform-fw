# OpenAMRobot 2.0 base controller: portable core, adapters and NUCLEO-H723ZG port

Status: **proposal** for the firmware owner to adopt, adapt or reject. Build-verified and host-tested only; nothing here has run on hardware. The safety core is a proposal for platform-fw #17 and needs two human approvals including the platform lead.

## Layers (I8 architecture baseline, P-03 rev18.5 item 14)

| Layer | Path | Rule |
|---|---|---|
| Core | `firmware/core/` | Portable C11. Depends only on interfaces it defines. Never calls a HAL, a register or a communication stack. |
| Device adapters | `firmware/motor_controller_bridge/` (ZLAC8015D CANopen), `firmware/sensor_bridge/` (ICM-42688-P), `firmware/battery_monitor/` (Daly BMS) | Portable C11 behind core interfaces (CAN TX, SPI, motor backend). |
| Safety core | `firmware/safety_io/` | E-01 rev 3 state machine; timestamped inputs in, state, outputs and reasons out. |
| Board port | `boards/stm32/OpenAMRobot/Port/` | Clock, timers, FDCAN, SPI, UART, IWDG, reset cause. Called from the CubeMX `USER CODE` blocks of `main.c` only. |

`tools/check_core_portability.py` fails the host tests if anything under `firmware/` includes a board, HAL or stack header.

## Control tick (TIM6, 50 Hz, highest application priority)

1. Drain CAN1 (ZLAC8015D) and CAN2 (Daly) receive FIFOs into the adapters.
2. Bus-off supervision per channel (`can_health`): hold-off, restart, counter, latch after repeated events.
3. Read the safety inputs, step `safety_core`, drive the RESET lamp and CHARGE_REQUEST.
4. `motion_tick(now, motion_allowed, ...)`: the single place where wheel targets are written. `motion_allowed` comes from the safety core and nothing else.
5. ZLAC8015D adapter tick (SDO cycle), Daly request.
6. Feed IWDG1. Nowhere else feeds it.

The background loop polls the IMU (until INT1 is assigned) and will run the micro-ROS executor over the serial transport hook (USART3 on the ST-LINK virtual COM port).

## Command path (`firmware/core/src/motion.c`)

| Rule | Value | Source |
|---|---|---|
| Command timeout, evaluated every tick, non-latching | 200 ms | I8 WP, Gate A parity |
| Host heartbeat | 500 ms | I8 WP |
| Linear and per-wheel clamp (curvature kept, pure rotation included) | 0.5 m/s | P-03 item 29, item 30 A5 |
| Deceleration, travel / hold-and-move | 1.5 / 1.0 m/s² | P-03 item 30 A6 |
| Stale commands on gate entry | discarded; only a command received after the entry time moves the robot | E-01 rev 3 |

On timeout, inhibit, host loss or driver not ready the core writes zero at once and the drive's own deceleration (6084h) brings the wheels to rest. Nothing is replayed.

## Commissioning record: no motion on placeholders

`firmware/core/include/oamr/commissioning.h` defines the values without which the base must not move; the board fills them in `boards/stm32/OpenAMRobot/Port/Inc/port_commissioning.h`, where every field is unset today.

| Missing | Effect |
|---|---|
| Wheel radius, track width or acceleration limit | Motion core state `NOT_COMMISSIONED`, zero written every tick. The port holds Software Stop asserted, so the safety core stays LOCKED and the RESET lamp stays on. |
| ZLAC8015D ramps (6083h/6084h) or link-loss block (2000h, 1017h, consumer misses) | As above, and the ZLAC adapter is not started: nothing is sent on CAN1, nothing is written or saved (2010h) on the drive. |

The motion defaults are NaN, not numbers: there is no 0.100 m, 0.400 m or 1.5 m/s² anywhere in the firmware. Host tests use explicit test-only values (`tests/host/core/test_params.h`).

The relationship between 2000h and the STM32 heartbeat consumer is stated once, in the header comment of `oamr/commissioning.h`. In short: 2000h is the drive stopping itself when the STM32 goes silent (accepted range 200 to 300 ms, roadmap risk 2); the consumer (misses × 1017h, at least 3 misses) is the STM32 noticing the drive went silent. They guard different failures and are both required. TODO(Evans): rule all three values under D-24.

## ZLAC8015D boot configuration (proposal for decision D-24, P-03 item 30 A4)

NMT pre-operational, then for each entry: read; write only if different; read back; latch `CONFIG_FAILED` on mismatch or abort. Save with 2010h only if something was written. Targets zeroed, NMT start, controlword 06h, 07h, 0Fh, statusword checked. Entries: 6060h = 3, 6083h:1/2 and 6084h:1/2, 2000h, 1017h, all from the commissioning record. Heartbeat consumer: misses × 1017h (see above).

Command latency: the port drains CAN1 once per 20 ms tick and the adapter completes one SDO transfer per tick, so a new wheel target reaches the drive up to six ticks (120 ms) later, the right wheel one tick after the left (`Zlac.PortTimingOneTransferPerTick`). TODO(Evans): decide whether to move 60FFh to an RPDO or the synchronous 60FFh:3 write before motion tests. Drive faults (603Fh, decoded per side) latch until an explicit fault reset.

## Open parameters (TODO(Evans))

| Parameter | Where | Current placeholder |
|---|---|---|
| Six safety and charging pins, polarity | `Port/Inc/port_board.h` | unassigned: base stays LOCKED |
| IMU INT1 EXTI pin | `Port/Inc/port_board.h` | unassigned: IMU polled |
| Wheel radius, track width | `port_commissioning.h` | unset: base stays LOCKED |
| Acceleration limit | `port_commissioning.h` | unset: base stays LOCKED |
| 6083h / 6084h times | `port_commissioning.h` | unset: ZLAC not started |
| 2000h, 1017h period, consumer misses (D-24) | `port_commissioning.h` | unset: ZLAC not started (first-pass proposal 250 ms, 100 ms, 3) |
| SDO vs RPDO for 60FFh (command latency) | `zlac8015d.c` | SDO, up to 120 ms |
| Size of 2000h and 2010h, save value | `zlac8015d.c` | 2 bytes, 1 |
| Quick stop 605Ah / 6085h | `zlac8015d.c` | not configured |
| Wheel direction (invert left/right) | `zlac8015d.c` | not inverted |
| SDO timeout | `zlac8015d.c` | 50 ms |
| Bus-off hold-off, events per window | `can_health.c` | 100 ms, 3 per 10 s |
| BMS stale time, Daly layout on the delivered unit | `daly_bms.c`, `safety_core.c` | 3 s; V1.2 layout |
| Column E-stop and encoder stale times | `safety_core.c` | 500 ms, 100 ms |
| IWDG timeout, BOR level | `port_board.h`, option bytes | 100 ms, not set |

## Host tests

```bash
tools/host_tests.sh   # needs cmake, a C/C++ compiler, libgtest-dev, python3
```

Same conventions as the `tests/host` layout of platform-fw #27 (CMake, CTest, GoogleTest, sanitizers, `-Werror`), in its own project under `tests/host/core` so it does not need the linorobot2 checkout.
