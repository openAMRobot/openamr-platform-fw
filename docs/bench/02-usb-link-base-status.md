# 02 USB link to the Jetson and `/base/status` freshness

**Proves:** the NUCLEO talks micro-ROS over the first bench transport (serial on the ST-LINK
virtual COM port, USB cable to the Jetson), `/base/status` arrives at 10 Hz, goes stale when the
link is cut, and the base is in a stopped state without the agent. Ethernet/UDP is a separate task
(platform-fw #21) and is not part of this procedure.
**Moves anything?** No. Same single-board setup as procedure 01. Nothing on CAN, no motor supply.

> **Firmware status: [firmware gap].** In the draft stack (#40) the transport is only a hook: the
> board port opens USART3 (PD8/PD9, 115200 8N1) and the four transport functions map onto the
> micro-ROS custom transport callbacks, but **the micro-ROS client, the executor spin and the
> `/base/status` publisher do not exist yet** (`TODO(Evans)` in `port_app_background()`). The
> message definitions (`BaseStatus`, `BaseAuxStatus`) are a draft in interfaces #30 / #31
> **[pending interfaces #30]**. This procedure is written for the day the firmware publishes. Until then,
> steps 1 and 2 pass and step 3 onward are recorded NOT RUN with the reason.

## Preconditions

- Procedure 01 passed or at least step 3 (flash) and step 4 (boot) are done and recorded.
- Jetson (or bench PC with ROS 2 Jazzy) and the NUCLEO connected by the ST-LINK USB cable. The
  actual Jetson module, image and JetPack are `TODO(Arka)`; write them in the record.
- `micro_ros_agent` installed. The Jetson install and exact version are `TODO(Mohamed)`
  (platform software). A known-good source pin used by the manifest repository is recorded there; use
  the version the manifest names and write it in the record.
- Persistent device path of the virtual COM port (`ls -l /dev/serial/by-id/`), not `/dev/ttyACM0`.
- Serial baud accepted by the firmware: 115200 in the draft, `TODO(Evans)` confirm.
- ROS settings equal on both ends: `echo $ROS_DOMAIN_ID $RMW_IMPLEMENTATION`, recorded.
- Chrony or equivalent running; `chronyc tracking` offset below 5 ms (the same bar as the H-day runbook).

## Step 1. Environment and port

```
ros2 doctor --report | head -40
echo $ROS_DOMAIN_ID $RMW_IMPLEMENTATION
chronyc tracking
ls -l /dev/serial/by-id/
```

Expected: ROS 2 Jazzy, clock offset under 5 ms, one ST-LINK VCP path. Close any serial terminal
that still has the port open (only one program may own it).

## Step 2. Start the agent

```
export STM32_BENCH_DEVICE=/dev/serial/by-id/<persistent path>
export STM32_BENCH_BAUD=115200        # TODO(Evans): confirm the firmware baud
ros2 run micro_ros_agent micro_ros_agent serial --dev "$STM32_BENCH_DEVICE" -b "$STM32_BENCH_BAUD" -v6
```

Expected: the agent starts and keeps the port open (no `Permission denied`; if so, add the user to
`dialout`). With the **[firmware gap]** the agent shows no session: record "no client session" and stop here.
With client firmware: a session is created within a few seconds after pressing RESET on the NUCLEO.

## Step 3. Graph and identity

In a second terminal, same ROS environment:

```
ros2 node list
ros2 topic list -t
ros2 topic info /base/status -v
ros2 topic info /base/aux_status -v
ros2 topic info /cmd_vel -v
```

Expected (frozen contract, I8 rev 3): `/base/status` and `/base/aux_status` exist, published by the
NUCLEO node, QoS reliable and transient-local with depth 1. `/cmd_vel` has the NUCLEO as
subscriber. **Exactly zero publishers on `/cmd_vel` in this procedure** (no UI, no Nav2, no mux
running). The type names depend on interfaces #30 (`TODO(Evans)` final names) and are recorded as found.

Read one message:
```
ros2 topic echo /base/status --once --qos-durability transient_local --qos-reliability reliable
```
Record: contract version (starts at 1), firmware identity, boot identity, sequence number, state
(expected LOCKED at this stage), reset cause, build configuration (**powered debug must be
reported as not compiled in** for any image later used with motors), and fault group 18001 codes.
A retained snapshot is not proof that the link is alive (the first message may be retained).

## Step 4. Rate and freshness

```
ros2 topic hz /base/status --window 100
ros2 topic hz /base/aux_status --window 50
```

Expected: `/base/status` average rate 10 Hz (published on change plus a 10 Hz heartbeat); aux 5 Hz.
Pass: 10 Hz within 10 percent over 100 messages, no gap above 0.5 s (the stale threshold) in
`min`/`max` period of the output. `/base/aux_status`: 5 Hz, no gap above 0.6 s. The sequence number
increments by 1 per message; check by sampling twice:
```
ros2 topic echo /base/status --field seq --once
```
(field name is `TODO(Evans)` until interfaces #30 merges). Run for 10 minutes; record the
maximum gap and the count of sequence jumps. Serial jitter is the reason the stale threshold is five
periods and not one.

`/base/aux_status` carries the CTL-017 block relayed over CAN3; with no CTL-017 attached it must
report the lift as not present or stale, never as healthy (`TODO(Evans)` field). Record what it says.

## Step 5. Cable pull

1. With `ros2 topic hz /base/status` running, unplug the USB cable. Note the time.
2. Expected: the rate output stops. Consumers must treat the status as stale after **0.5 s** without
   a message. The agent shows the client as lost.
3. Plug the cable back. Expected: the agent re-creates the session (the ping and reconnect pattern)
   within `TODO(Evans)` seconds, status resumes, **boot identity unchanged** if the MCU did not
   reset (the USB cable only carries data here; the board stays powered from the same cable: if the
   board loses power with the cable, boot identity changes and the reset cause is POWER_ON; record
   which one happened).
4. Record reconnect time and the sequence number before and after.

## Step 6. Agent kill

Roadmap risk 11 acceptance: "Kill the agent: base enters stop within 500 ms; restart ..."

1. Note the state in `/base/status`. Kill the agent: `pkill -f "[m]icro_ros_agent"` (from another
   terminal; the pattern with brackets avoids killing the shell).
2. Expected: with no host heartbeat for 500 ms the base core reports a stop condition (host
   heartbeat 500 ms, `/cmd_vel` stale 200 ms). The status itself cannot be read without the agent:
   read the state over the debugger instead (procedure 01 step 5: `print/x s_safety.out.reasons`;
   `OAMR_SC_R_COMMS_LOST` is bit 8, value 0x100) and note the time from the kill to the reason bit
   with a timestamped gdb session or a scope on the lamp output once assigned (`TODO(Evans)`).
3. Restart the agent. Expected: the session returns and **no motion command is replayed**; the
   base stays inhibited until a fresh command and an explicit resume (P-03 item 30 A1; the Resume
   rule is pinned only after platform-sw #90, #125, interfaces #35 and ui #50 merge, so here only note what the
   firmware reports).

Wheels are not connected in this procedure, so "stop" means the reason bits and the zero
velocity in the debugger (`print s_motion` fields, `TODO(Evans)` which). Repeat the kill and
restart 5 times; record each reconnect time.

## Pass criteria

- Steps 2, 3, 4 pass with the client firmware present; 0 `/cmd_vel` publishers throughout.
- Rate 10 Hz within 10 percent, no gap above 0.5 s over 10 minutes; aux 5 Hz, no gap above 0.6 s.
- Cable pull: stale within 0.5 s, session returns in the time Evans sets.
- Agent kill x5: the comms-lost reason within 500 ms each time, no replayed command.

## Stop condition

Any motion command published on `/cmd_vel` by anything other than this procedure's absent
publishers (it means another bench setup is on the same ROS domain): stop the agent, find the
publisher (`ros2 topic info /cmd_vel -v`), set a unique `ROS_DOMAIN_ID` for the bench, repeat.

## Record

Jetson and image, agent version, domain and RMW, device path and baud, message type names as
found, one full `/base/status` echo, hz outputs, cable-pull and kill timings (5 values).

## Open values (TODO)

- `TODO(Evans)` baud, message and field names, reconnect time limit, field for the lift state, debugger variable names.
- `TODO(Mohamed)` micro-ROS agent version on the Jetson.
- `TODO(Arka)` Jetson module, image and JetPack, bench `ROS_DOMAIN_ID`.
