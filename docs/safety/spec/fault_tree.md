# Fault tree: unintended base motion and unintended lift motion

Status: review material for the firmware owner (Evans), the platform lead and Mohamed.
Test oracle material for specification review. Not firmware. Not a safety function. It
claims no measured performance: every stop time and distance in this file is an open bench
item (B2, B3, B4, B9).

Layers, from most to least independent:

| Layer | What it is | Rated |
| --- | --- | --- |
| HW | Hardwired chain: two dual-channel E-stops, PNOZ s3, K1/K2 removing SAFE actuator power and the SAFE 24 V brake-release supply, spring-applied hub brakes (rev 5 1, R3, P-03 item 30 A5) | The only safety function |
| DRV | ZLAC8015D: controlled deceleration on a zero command (0x6084), link-loss stop 0x2000 (decision D-24, proposed), own faults | Not a safety function |
| BC | STM32H723 base controller: state machine rev 5, command freshness, heartbeat consumer, lift permit producer | Not safety-rated (rev 5 1) |
| CTL | CTL-017: local lift gate, permit supervision, stop sensors, travel timeout, Hall window (P-03 item 30 A7) | Not safety-rated |
| HOST | Jetson: collision monitor stop zone, velocity smoother, Resume rules (P-03 item 30 A1, A2), host stale limits on /base/status | Not safety-rated |

"Cover" below means the layer that is designed to stop the branch. "Open" means there is no
designed cover yet, or the cover depends on evidence that does not exist.

## 1. Top event: unintended base motion

```
Unintended base motion
├── A. Motion commanded when it should not be
│   ├── A1 Host sends motion while an operator expects a stop
│   ├── A2 Stale or replayed command executed after a fault
│   ├── A3 Old-session command executed after a host reboot
│   └── A4 Base controller enters RUN without a valid RESET
├── B. Commanded stop not executed
│   ├── B1 Base controller keeps sending a non-zero target (hang, bug)
│   ├── B2 CAN1 lost while moving
│   ├── B3 Drive ignores or misinterprets the zero command
│   └── B4 Drive ramps too slowly (deceleration setting)
├── C. Motion without command
│   ├── C1 Drive fault, runaway or encoder fault
│   └── C2 Robot rolls on a slope or after power loss
└── D. Operator E-stop
    └── D1 E-stop pressed while moving
```

| Branch | HW | DRV | BC | HOST | Cover and evidence | Status |
| --- | --- | --- | --- | --- | --- | --- |
| A1 | E-stop by operator | | UI Stop latch, INHIBITED, no auto Resume (rev 5 2.3, R1) | collision monitor stop zone (A2), Resume rules (A1) | BC and HOST; vectors h02, h16, h33 | Covered in spec; host part in platform-sw |
| A2 | | | first motion only after RUN entry; queued commands discarded (rev 5 3, T1 to T11) | | BC; vectors h23, h35 | Covered in spec |
| A3 | | | session epoch, SESSION_OLD (rev 5 3.1) | | BC; vectors h06, h27 | Covered in spec; stale-epoch heartbeat not in the event model |
| A4 | PNOZ monitored start | | reset_seen only from qualified short press (5.1, 5.2) | | HW and BC; vectors h01, h10, h15, h25 | Covered in spec; stuck-low RESET is undetectable by firmware (rev 5 5.1) |
| B1 | E-stop by operator | 0x2000 link-loss stop only if the controller stops talking | IWDG reset leads to LOCKED (T1) | /base/status stale 0.5 s on host | DRV fallback is **not** triggered while a hung controller keeps sending PDOs with a non-zero target | **Open**: a hang that keeps CAN traffic alive has no automatic cover except the operator E-stop. B4 case (i) must measure it. Whether 0x2000 is refreshed by PDO, SDO, NMT or heartbeat traffic is unknown (rev 5 5.4) |
| B2 | E-stop by operator | 0x2000 = 250 ms proposed (D-24) | heartbeat consumer 300 ms proposed, T10b | | DRV | **Open until B3**: stop profile on link loss (ramp, freewheel or fault) is not documented by the supplier |
| B3 | E-stop by operator | | read back 2026h:05, 0x2000, 0x1017 (T10b) | | BC detects config mismatch; no cover for a drive that acknowledges and still moves | **Open**: a CAN acknowledgement is not standstill (rev 5 5.4); needs independent wheel speed check, not specified |
| B4 | | 0x6084 500 ms default, read back | | | DRV | **Open**: P-03 item 30 A6 asks for 1.5 m/s2 travel and 1.0 m/s2 hold-and-move, identical in Nav2, smoother, ZLAC and Gazebo; rev 5 keeps 0x6084 at the factory default. Owner decision |
| C1 | E-stop by operator | drive fault stop | DRIVE_FAULT, any non-zero 603Fh blocks (T10a) | | DRV and BC | Partly covered; per-wheel meaning of 603Fh not confirmed, so treated as blocking |
| C2 | spring-applied brakes when SAFE 24 V is off | | | | HW | **Open until D-25 and B8**: brake torque 5 Nm vs 6 Nm and holding on the slope not confirmed by the supplier |
| D1 | PNOZ drop-out, K1/K2, brake grip | | T3 to LOCKED, outputs zero | | HW | **Open until B2**: stop distance at most 0.15 m at 0.2, 0.3 and 0.5 m/s not yet measured; the ~37 ms figure is a screening estimate |

## 2. Top event: unintended lift motion

```
Unintended lift motion
├── L1 Lift moves while the base moves
│   ├── L1a Base not stopped but permit granted (bad stopped evidence)
│   ├── L1b Base starts moving while the lift moves
│   └── L1c Permit stale or replayed at CTL-017
├── L2 Lift moves while an arm is outside the approved pose or moving
│   ├── L2a Arm status wrong, stale or from an old session
│   └── L2b Arm starts moving during the lift move
├── L3 Lift moves without a command or past its target
│   ├── L3a CTL-017 resumes an old target after recovery
│   ├── L3b Stop sensor or Hall count failure, overtravel
│   └── L3c H-bridge fault, motor driven with logic stopped
├── L4 Lift keeps moving after a fault
│   ├── L4a CAN3 lost during a move
│   ├── L4b Base controller reboots during a move
│   └── L4c CTL-017 hangs during a move
└── L5 Operator E-stop during a lift move
```

| Branch | HW | BC | CTL | HOST | Cover and evidence | Status |
| --- | --- | --- | --- | --- | --- | --- |
| L1a | | permit only from both wheels, linear and angular limits, hold time (#20) | | | BC; vectors permit_base_01 to 04 | Covered in spec; thresholds provisional (CONFLICT C2), wheel geometry TODO(Evans) |
| L1b | E-stop by operator | base movement inhibited while the lift moves (#20, 13 Nov) | | | BC | **Open**: no rev 5 row and no vector yet. The base-side inhibit using CTL-017 feedback is named in #20 but not specified (states, freshness, reason). Owner item |
| L1c | | permit carries boot or session and sequence (#20, platform lead 13 Nov) | rejects stale, duplicate, out-of-order, wrong-session permits | | CTL; vectors permit_ctl_01, 02 | Covered in spec; wire contract not decided (C7) |
| L2a | | arm status fresh, current session, approved pose (#20) | | manipulation publishes measured status | BC; vector permit_base_05 | **Open contract**: ArmStatus message, pose definitions and feedback age not agreed (#20, manipulation review). Build 1 pose set CONFLICT C8 |
| L2b | | permit withdrawn at once with STOP (#20) | stops on withdrawal | base and arm arbitration (manipulation #24) | BC and CTL; vectors permit_base_05, permit_ctl_02 | Covered in spec; withdrawal-to-stop time not measured (B9) |
| L3a | | no replay (#20) | new command required after recovery | | CTL; vector permit_ctl_03 | Covered in spec |
| L3b | | | stop sensors, Hall window, travel timeout (P-03 item 30 A7) | | CTL; abstracted as `local_ok` | **Partly open**: Hall pulses per mm, current limit, standstill timeout are owner items; the vectors do not exercise the local checks |
| L3c | E-stop removes motor stage supply | | | | HW only | **Open**: an H-bridge that drives with logic stopped is covered only by the operator E-stop; the lift E-stop stop distance (5 mm design target) is not measured |
| L4a | | heartbeat consumer, CTL017_LINK_LOST | permit expiry stops the lift | | CTL; vectors permit_ctl_02 expiry, permit_base_06 | Covered in spec; permit timeout TODO(Evans) |
| L4b | | permit false at boot | producer session change drops the permit | | CTL; vectors permit_base_07, permit_ctl_03 | Covered in spec |
| L4c | E-stop by operator | CTL-017 status stale, permit withdrawn | IWDG on CTL-017 (owner) | | BC cannot stop a hung CTL-017 that keeps driving | **Open**: the only automatic cover is CTL-017's own watchdog; the stopped state of the H-bridge after an IWDG reset must be shown on the bench |
| L5 | E-stop cuts the motor stage | | | | HW | **Open until bench**: lift stop distance not measured |

## 3. Branches with no designed cover yet

1. B1: a hung base controller that keeps CAN1 traffic alive with a non-zero target.
2. B3: a drive that acknowledges a zero target but keeps moving; no independent wheel speed
   check is specified.
3. B4: deceleration settings of P-03 item 30 A6 against the 0x6084 factory default.
4. L1b: base movement inhibited while the lift moves; named, not specified, no vector.
5. L3c and L4c: lift motor driven while CTL-017 logic is stopped or hung; only the operator
   E-stop covers it.
6. All physical stops (D1, C2, L5) wait for B2, B8 and B9 and the D-25 supplier evidence.

These are review findings, not designs. The owner decides the covers.
