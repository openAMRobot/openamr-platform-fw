# Software-in-the-loop chain (host build of the base firmware)

Night-shift proposal: agent-written draft for the owner to adopt, adapt or reject. Not for merge without the owner and the platform lead.

For Evans (firmware) and Arka (ROS side). The portable firmware core (the same C sources the NUCLEO-H723ZG builds) runs as a Linux process, so the whole chain can be exercised before the hardware is on the bench.

```
ros2 topic pub /cmd_vel ──► micro-ROS agent ──pty (serial)──► oamr_base_host
                                                                 │  firmware/core, motor_controller_bridge,
                                                                 │  battery_monitor, sensor_bridge, safety_io,
                                                                 │  firmware/app (same sources as the STM32)
                                                    vcan0 (CAN1) ├──► ZLAC8015D emulator (tools/can_sil)
                                                    vcan1 (CAN2) └──► Daly BMS emulator  (tools/can_sil)
/base/status, /base/aux_status, /odom/unfiltered, /imu/data_raw ◄──┘
```

## Quick start (Ubuntu 24.04, ROS 2 Jazzy, root or passwordless sudo)

```bash
tools/sil_setup.sh          # once: micro-ROS host libs + agent (~15 min), build/host, vcan0/vcan1
tools/sil_run.sh            # whole chain, Ctrl-C stops it; logs in /tmp/oamr_sil
# second shell, ROS 2 sourced:
tools/sil_ctl.py arm        # PNOZ cycle + clear the software stop -> safety RUN
ros2 topic pub -r 20 /cmd_vel geometry_msgs/msg/Twist "{linear: {x: 0.2}}"
ros2 topic echo /base/status --field values
tools/sil_test.sh           # the end-to-end suite (16 scenarios, about 90 s)
```

`tools/sil_run.sh --no-override` starts the base the way the STM32 is today: safety pins unassigned, gpio_valid false, the safety core stays LOCKED and nothing moves. `--no-commissioning` starts it with an empty commissioning record (NOT_COMMISSIONED; the ZLAC adapter is not started).

## Layout

| Path | What |
|---|---|
| `firmware/app/` | Board-independent composition: the control tick (bus-off supervision → safety core → motion core with motion_allowed → ZLAC → Daly → odometry), status snapshot, fault list. Same sequence as `boards/stm32/.../port_app.c`; the STM32 port is not switched to it yet (TODO(Evans)), so the STM32 image is unchanged. |
| `firmware/core/src/odometry.c` | Differential-drive odometry (midpoint integration, gap rejection, needs commissioned geometry). |
| `boards/host/src/` | Host board port: CLOCK_MONOTONIC, SocketCAN (CAN_ERR_BUSOFF / CAN_ERR_RESTARTED drive the bus-off supervision like FDCAN PSR.BO), pty transport, fake ICM-42688-P register file, test control socket. |
| `boards/host/uros/` | micro-ROS application layer: plain rclc C on `oamr_transport_t`, no Linux dependency. Intended for reuse on the STM32 (see below). |
| `boards/host/config/sil_commissioning.conf` | SIL-only commissioning values (0.1 m wheel, 0.4 m track, 1.5 m/s²). Not robot values. |
| `tools/can_sil/daly_bms_emulator.py` | Daly BMS on CAN2 with the layouts the adapter assumes (unconfirmed for the delivered unit). |
| `tests/sil/` | pytest end-to-end suite. |

## Topics (I8 rev 3, section 6)

| Topic | Type | QoS | Rate |
|---|---|---|---|
| `/cmd_vel` (sub) | geometry_msgs/Twist | best effort | host rate |
| `/base/status` | **stand-in** diagnostic_msgs/DiagnosticStatus | reliable, transient-local, depth 1 | 10 Hz + on change (stale 0.5 s) |
| `/base/aux_status` | **stand-in** diagnostic_msgs/DiagnosticStatus | reliable, transient-local, depth 1 | 5 Hz (stale 0.6 s) |
| `/odom/unfiltered` | nav_msgs/Odometry, odom → base_footprint | reliable (see MTU note) | 50 Hz (rate TODO(Evans)) |
| `/imu/data_raw` | sensor_msgs/Imu, imu_link | best effort | every valid sample (200 Hz ODR) |

**Stand-in.** `openamr_base_msgs` does not exist yet (openamrobot-interfaces #30; PR #31 is documentation only). Both status topics carry one `KeyValue` per I8 field so the ROS side can be written against the field names now. TODO(Evans): switch to `openamr_base_msgs/BaseStatus` / `BaseAuxStatus` once merged; `oamr_app_status_t` already carries every field.

`/base/status` keys: `contract_version, stand_in, boot_id, sequence, monotonic_us, powered_debug, reset_cause, controller_state, ready, command_accepted, recovery_required, host_link_ok, applied_state, applied_left_m_s, applied_right_m_s, applied_clamped, cmd_age_ms, safety_state, safety_reasons, safety_first_reason, motion_allowed, reset_required, charge_request, fault_count, faults_overflow, faults, drive_state, drive_ready, drive_fault_raw, wheel_feedback_valid, wheel_left_rad_s, wheel_right_rad_s, can1_state, can2_state, can1_bus_off_total, can2_bus_off_total, bms_fresh, bms_fault, bms_soc_percent, bms_voltage_v, imu_present, imu_healthy, imu_invalid_samples, imu_read_failures, commissioning_missing`.
`faults` is `code:NAME:latched:inhibits_motion` entries joined by `;`, ascending code, at most 16. `level` is OK/WARN/ERROR for READY/DEGRADED/FAULTED.

`/base/aux_status` keys: `contract_version, boot_id, sequence, estop2_state, lift_state, head_light, getac_power_good`. Lift, head light and Getac power-good come from CTL-017 over CAN3, which is not part of this chain: they report `UNKNOWN`, never an inferred value.

## Fault codes, group 18001 (proposal, TODO(Evans): register in openamrobot-interfaces)

| Code | Name | Latched | Inhibits motion |
|---|---|---|---|
| 18001 | CMD_TIMEOUT (/cmd_vel older than 200 ms) | no | yes |
| 18002 | HOST_LINK_LOST (host heartbeat older than 500 ms) | no | yes |
| 18003 | DRIVE_COMM_LOST (ZLAC heartbeat lost / SDO failures) | no | yes |
| 18004 | DRIVE_FAULT (603Fh non-zero) | yes | yes |
| 18005 | DRIVE_CONFIG_FAILED (boot read-back mismatch) | yes | yes |
| 18006 / 18008 | CAN1 / CAN2 BUS_OFF (restarting) | no | yes |
| 18007 / 18009 | CAN1 / CAN2 FAILED (too many bus-offs) | yes | yes |
| 18010 | BMS_STALE (no Daly data for 3 s) | no | yes (safety core) |
| 18011 | BMS_FAULT (Daly level-2 alarm / hardware bit) | no | yes (safety core) |
| 18012 | IMU_INVALID | no | **no**: E-01 rev 3 does not list the IMU as a motion inhibit |
| 18013 | IMU_NOT_PRESENT | no | no |
| 18014 | SAFETY_INHIBIT (safety core not RUN) | no | yes |
| 18015 | NOT_COMMISSIONED | no | yes |

## Host heartbeat

A successful agent ping (every 100 ms, 3 misses before the session is dropped) counts as the host heartbeat for the motion core and the safety core. When the agent or the pty goes away the heartbeat stops, the motion core writes zero at once and the safety core latches LOCKED after 500 ms (E-01 rev 3). The status topic stops with it; on the host side it is stale after 0.5 s. PROPOSAL, TODO(Evans): I8 does not yet say what the host heartbeat is; a dedicated topic from the host may replace the ping.

## Test-only safety override

The SIL needs a way to close the safety chain that the bench provides with real wiring. `boards/host/src/host_ctl.c` drives the safety GPIO inputs from a Unix socket, but only when `oamr_base_host` was started with `--sil-safety-override`; otherwise the commands are refused and the inputs stay invalid. The file has an `#error` for any ARM target and lives only in `boards/host`, so it cannot end up in the STM32 image. Safety pin values stay TODO(Evans).

## Scenarios covered (`tests/sil/test_sil_chain.py`)

| Scenario | Result checked |
|---|---|
| No override | LOCKED, SAFETY_INHIBIT, zero 60FFh; GPIO commands refused |
| No commissioning | sw_stop held, LOCKED, NOT_COMMISSIONED, ZLAC adapter not started |
| RESET cycle | LOCKED → RUN only after the PNOZ cycle and sw_stop cleared |
| /cmd_vel 0.2 m/s | 60FFh within 2 % (19.1 rpm; see the rounding note), applied OK, READY, odometry follows 606Ch |
| Stop /cmd_vel | zero 60FFh within 350 ms of the last command (200 ms timeout + 150 ms for one adapter SDO cycle and transport), wheels at rest after the drive ramp, CMD_TIMEOUT |
| Explicit zero | zero 60FFh within 300 ms, no timeout |
| 1.2 m/s, 5 rad/s | clamped to 0.5 m/s per wheel, curvature kept |
| 0.5 → 0.05 m/s | applied deceleration ≤ 1.5 m/s² (+15 % sampling margin), ramp ≈ 0.3 s |
| Agent killed | zero wheels, LOCKED, no status for > 0.5 s; agent restart reconnects, stays LOCKED |
| Status rates | /base/status ≥ 9 Hz, no gap ≥ 0.5 s; aux 5 Hz, no gap ≥ 0.6 s; IMU flowing |
| Emulator stopped | DRIVE_COMM_LOST, command not applied |
| vcan0 deleted | DRIVE_COMM_LOST, drive not ready |
| CAN1 bus-off (CAN_ERR_BUSOFF frame) | CAN1_BUS_OFF, zero applied; cleared after CAN_ERR_RESTARTED |
| BMS silent | BMS_STALE after 3 s, LOCKED, zero |
| BMS level-2 alarm | BMS_FAULT, LOCKED, zero |
| IMU invalid samples | IMU_INVALID, invalid samples never published, command still applied |

**Measured stop latency** (this run, CI container): 0.152 to 0.159 s from the end of the command loop to zero on 60FFh over 5 stops; the last /cmd_vel was sent up to 50 ms before that, so about 0.20 s after the last command, as the 200 ms timeout predicts.

**60FFh rounding.** The ZLAC adapter writes whole rpm to 60FFh. At 0.2 m/s on the SIL 0.1 m wheel the exact target is 19.10 rpm; the drive gets 19 rpm (0.5 % low). The test bound is max(2 %, half a count).

## Notes for the STM32 build of the micro-ROS layer (TODO(Evans))

- Route: `micro_ros_stm32cubemx_utils` static library (`libmicroros.a`) built with the same colcon.meta settings as `tools/sil_setup.sh` (custom transport, history 32/32), linked into the CubeMX project; `boards/host/uros/uros_app.c` and `uros_status.c` compile unchanged. Transport: UART or USB CDC first (issue #21), wrapped as `oamr_transport_t`.
- Call `oamr_uros_spin_once()` from the background loop or a low-priority task, never from the control timer. The control tick does not depend on it.
- `/odom/unfiltered` (~720 B serialized) does not fit the 512 B XRCE-DDS MTU, and only reliable streams fragment. It is published reliable here; keep that or raise the MTU.
- Memory: the status serialisation uses static buffers only; the rclc entities are allocated once per session with the default allocator, which needs a heap (or a static allocator) on the MCU.

## Known limits

- The STM32 port still has its own copy of the control tick (`port_app.c`); `firmware/app` is the same sequence, with odometry added. TODO(Evans): switch the STM32 to `firmware/app` so both run one copy.
- CAN3 (CTL-017, lift, 2nd E-stop) is not in this chain; the 2nd E-stop observation comes from the control socket. Gazebo coupling is the next step (out of scope).
- vcan has no arbitration, bit timing or error counters; bus-off is injected as the kernel's error frame, not produced by the bus.
- The wheel inversion (`invert_left/right`), the odometry rate and covariances, and the IMU health thresholds are TODO(Evans).
