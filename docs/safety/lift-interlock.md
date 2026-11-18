# Lift Motion Interlock — Engineering Proposal

**Issue:** [openamr-platform-fw #20 — Lift motion interlock in the authoritative controller (UC1-19)](https://github.com/openAMRobot/openamr-platform-fw/issues/20)  
**Status:** Engineering proposal — pending cross-team review and approval  
**Scope:** Build 1 lift-motion supervisory interlock  
**Primary controller:** STM32H723 base controller (Gate B / I8)  
**Actuator controller:** CTL-017  
**Safety boundary:** Functional supervisory interlock; not a safety-rated function

> This document proposes the control architecture, evidence requirements, protocol behaviour and validation plan for the lift-motion interlock. Numerical thresholds, interface details and cross-team dependencies marked open are not approved implementation requirements.

---

## 1. Purpose and authority

The lift must not move unless the base is confirmed stopped and both arms are in an approved safe configuration.

The interlock is enforced by the controller architecture, not by the host application or UI.

| Component | Responsibility |
|---|---|
| Host / UI | Requests lift movement; cannot grant permission or bypass the interlock |
| STM32H723 base controller (Gate B / I8) | Sole authoritative producer of `lift_permit`; evaluates base and arm prerequisites |
| Manipulation controller | Publishes measured arm status and owns arm mechanisms and their control |
| CTL-017 | Independently enforces the received permit before accepting lift movement commands and supervises it during movement |
| E-01 hardwired safety chain | Remains independent of this functional interlock |

The STM32H723 is Gate B. The existing Teensy 4.0 Gate A implementation and its encoder configuration are reference material only. Gate A values must not be silently treated as validated Gate B configuration.

### 1.1 Permission rule

I8 may assert `lift_permit` only when all required evidence is valid, fresh, current-session and within the agreed operating limits:

- Base standstill has been continuously qualified.
- Both arms have valid measured status.
- Both arms are in the approved Build 1 safe pose: `STOWED`.
- Neither arm is moving.
- No relevant base, arm or communication fault/inhibit is active.
- Required controller-session and communication supervision is valid.
- Any additional prerequisite agreed by the responsible subsystem owners is satisfied.

If any prerequisite is unknown, invalid, stale, out of sequence or associated with an obsolete controller session, permission is denied.

`lift_permit` defaults to false at startup and after any relevant restart or communication loss.

### 1.2 Safety boundary

This is a functional supervisory interlock. It does not establish a safety-rated standstill condition and must not be represented as a replacement for E-01.

The PNOZ s3 and K1/K2 hardwired safety chain remain independent. The lift interlock must not weaken, bypass or take ownership of that chain.

Simulation and software tests can verify logic and protocol behaviour. They cannot establish physical safety performance or replace robot-level acceptance testing.

---

## 2. Base standstill qualification

### 2.1 Proposed chassis-level criteria

The current candidate for standstill qualification is:

- Absolute chassis linear velocity below `0.01 m/s`.
- Absolute chassis angular velocity below `0.02 rad/s`.
- Both conditions continuously satisfied for at least `500 ms`.

These are proposed engineering thresholds, not validated acceptance limits. They must be checked against measured encoder noise, drivetrain behaviour, geometry and stopping performance during B5 testing.

The chassis velocities should be derived from both wheel measurements and the validated drivetrain geometry.

For a differential-drive base:

\[
v = \frac{r}{2}(\omega_R+\omega_L)
\]

\[
\omega = \frac{r}{t}(\omega_R-\omega_L)
\]

Where:

- \(v\) is chassis linear velocity.
- \(\omega\) is chassis angular velocity.
- \(r\) is effective wheel radius.
- \(t\) is effective wheel separation.
- \(\omega_R\) and \(\omega_L\) are measured wheel angular velocities.

An average wheel-speed check alone is insufficient. Opposing wheel motion can produce a near-zero average linear velocity while the chassis is still rotating.

Both wheel feedback channels must be valid and fresh. If either measurement is invalid, stale, out of sequence or associated with a previous controller session, the standstill result is `UNKNOWN`, not stopped.

### 2.2 Gate A reference values

The existing Gate A documentation provides provisional drivetrain geometry values:

- Wheel radius: `0.10 m`
- Wheel separation: `0.46 m`

These values are reference inputs only until confirmed against the actual robot configuration.

The Gate A encoder overlay contains a `1024` counts-per-revolution configuration and uses a quadrature decoder implementation. This does not, by itself, establish whether the configured value represents encoder cycles, decoded counts or another scaling convention. Do not assume a factor-of-four conversion without confirming the encoder specification and measured behaviour.

The Gate B encoder resolution, scaling and measurement path must be established independently.

A previous proposal to require each wheel to remain below `1 rpm` is not included as an additional acceptance criterion here. Its relationship to the chassis-level thresholds must first be established using actual encoder scaling and B5 measurements.

### 2.3 Hysteresis and measurement validity

The implementation should avoid permit oscillation around the standstill boundary.

The exact hysteresis band and qualification behaviour remain open pending B5 encoder-noise measurements.

The implementation must distinguish:

- Measurement timestamp or equivalent sample-age evidence.
- Message publication sequence.
- Local message receipt time.
- Controller boot/session identity.

A newly published status message does not prove that the underlying encoder sample is new. Republishing an old measurement with a new sequence number must not refresh its measurement age.

---

## 3. Arm status and safe-pose qualification

### 3.1 Status ownership

The manipulation subsystem owns the arm mechanisms and is responsible for publishing measured arm status.

I8 evaluates that status and decides whether the lift permit can be asserted. Manipulation does not publish or control the authoritative lift permit.

The current I1 v1.0 and I2 v0.6 interface documents do not establish a complete runtime measured-arm-status contract. A dedicated interface, such as:

`openamrobot_manipulation_interfaces/msg/ArmStatus.msg`

is a proposed solution requiring agreement with the manipulation owner.

### 3.2 Required status information

The agreed runtime interface should provide enough information for I8 to determine whether both arms are demonstrably safe.

At minimum, the interface needs to establish:

- Arm identity.
- Measured joint state, and any additional measured pose information required to classify the safe configuration.
- Arm motion state.
- Controller and feedback health.
- Validity of the reported measurements.
- Producer boot/session identity.
- Publication sequence.
- Measurement timestamp or equivalent underlying sample-age information.
- A defined bimanual classification or sufficient information for I8 to derive one consistently.

The interface must distinguish the age of the underlying measurements from the age of the latest published message. A producer must not make stale measurements appear fresh by repeatedly publishing them with incrementing sequence numbers.

The clock relationship and method for determining measurement age must be specified by the interface owners. If timestamps cannot be compared reliably across controllers, an equivalent validated freshness mechanism is required.

### 3.3 Build 1 safe pose

For Build 1, the proposed permitted bimanual classification is:

`STOWED`

Other states, including `CARRY`, `OTHER` and `UNKNOWN`, must not grant permission in Build 1.

A future `CARRY` state can be considered only after the manipulation owner defines and approves its measured joint-space region, tolerances, settling requirements and collision envelope.

A commanded target or requested pose is not evidence that an arm has reached its safe pose. Qualification must use measured state and valid feedback.

Payload, compliant, force-controlled or otherwise undefined configurations remain unqualified until their meaning and safety implications are explicitly defined.

### 3.4 Arm motion and inhibit acknowledgement — open decision

The arms must not begin moving into an unsafe configuration while the lift is permitted to move.

A proposed additional mechanism is an explicit manipulation-side arm-motion-inhibit acknowledgement. Under this proposal, I8 would require a valid, fresh and session-bound acknowledgement before asserting `lift_permit`, and would continuously supervise it while permission remains active.

However, this acknowledgement is **not yet an agreed #20 requirement**. It introduces an additional interface and responsibility that must be reviewed with the manipulation owner.

The following must be agreed before implementation:

- Whether an explicit acknowledgement is required.
- Which controller produces it.
- What exactly the acknowledgement guarantees.
- How it is associated with both arms and their measured status.
- Its freshness, validity and session rules.
- How it is withdrawn when arm motion is requested, begins or becomes possible.
- How the system behaves when the acknowledgement is lost.

Until that decision is made, the document does not treat the acknowledgement as an approved acceptance criterion.

Arm-motion arbitration is separately tracked under the manipulation workstream. It must not be conflated with the lift-permit decision.

---

## 4. Permit lifecycle

### 4.1 Default-deny behaviour

`lift_permit` must be false:

- At I8 startup.
- At CTL-017 startup.
- Before a valid controller-session handshake.
- When required base feedback is invalid or stale.
- When either arm status is invalid, stale or from an obsolete session.
- When either arm is not in the approved Build 1 `STOWED` state.
- When either arm is moving.
- When a relevant fault or inhibit is active.
- When the permit communication watchdog expires.
- When the producer or receiver restarts and the previous session is no longer valid.

Unknown must never be interpreted as safe.

### 4.2 Granting permission

I8 may assert the permit only after all required prerequisites have been continuously satisfied for their agreed qualification periods.

The permit must be refreshed cyclically. A one-time grant or sticky boolean is insufficient.

A heartbeat proves only that a communication endpoint is alive. It does not prove that the lift is permitted to move.

### 4.3 Withdrawing permission

I8 must withdraw permission when any required prerequisite becomes false, invalid, stale or unknown.

The withdrawal must be communicated promptly. CTL-017 must stop lift motion on permit withdrawal or expiry of its permit-reception watchdog.

The stop command must remain available regardless of permit state. A missing or invalid permit must never prevent a stop request from being accepted.

CTL-017 must discard or invalidate the active lift target when permission is lost. Restoring permission must not automatically resume a previous command.

A new valid lift command is required after permission has been re-established.

### 4.4 CTL-017 local enforcement

CTL-017 must reject `MOVE_TO_STOP` and `START_HOMING` unless it has a valid current-session permit.

It must continuously supervise the permit while executing lift movement.

CTL-017 must independently retain its local protections, including:

- Travel limits.
- Stop sensors.
- Travel timeout.
- Hall-window checks.
- Its own command/core heartbeat supervision.
- Any additional local actuator protections already required by its design.

The interlock does not replace these local protections.

A CTL-017 stopped-state report, if used as evidence by I8, must represent fresh measured actuator state. Publishing a new status message containing an old stopped measurement must not satisfy freshness requirements.

---

## 5. Controller sessions and CAN communication

### 5.1 Communication architecture

The preferred architecture is a dedicated, cyclic permit message between I8 and CTL-017.

The existing command interface must not be treated as an implicit permit channel merely because it has spare bits. In particular, a three-byte RPDO1 command payload is not sufficient justification for assuming that the full permit protocol fits in that mapping.

The exact CAN identifier, PDO mapping, transmission type, refresh period, timeout and object-dictionary details must be agreed with the CTL-017 owner.

The I8 `BaseAuxStatus` proposal in [openamrobot-interfaces PR #38](https://github.com/openAMRobot/openamrobot-interfaces/pull/38) includes:

- `lift_permit`
- `lift_permit_reason_code`
- `lift_permit_max_age_ms`

The existing `boot_id` and `sequence` fields identify the producer boot and publication order in that interface.

Those fields describe the I8 interface proposal. They must not be assumed to define the CTL-017 CANopen PDO wire mapping. The transport mapping and semantics must be agreed explicitly across the relevant repositories.

### 5.2 Full session handshake and compact cyclic permit frame

The full producer boot nonce must not be transmitted in every cyclic permit frame.

Classic CAN provides a maximum payload of eight bytes per frame. A 64-bit nonce alone consumes all eight bytes, leaving no room for a sequence number, permit state or reason code.

The proposed design is therefore:

1. Establish the controller session through a handshake.
2. Exchange and validate the full producer and receiver boot/session identities during that handshake.
3. Bind the producer session to the current receiver session.
4. Agree a compact session tag for that specific controller-session pairing.
5. Carry the compact tag, sequence number and permit information in each cyclic permit frame.
6. Reject frames that do not belong to the currently established session.

The full boot nonce is used for session establishment, not repeated in every permit frame.

#### Proposed eight-byte cyclic frame

One candidate layout is:

| Field | Size | Purpose |
|---|---:|---|
| Session tag | 2 bytes | Identifies the negotiated producer/receiver session pairing |
| Sequence number | 3 bytes | Monotonic permit-frame sequence |
| Permit state | 1 byte | Permit asserted or withdrawn |
| Reason code | 1 byte | Diagnostic reason for the current state |
| Reserved | 1 byte | Reserved for agreed protocol use; transmit as zero |
| **Total** | **8 bytes** | Classic CAN payload limit |

This is a protocol proposal, not an approved PDO mapping.

The 16-bit session tag is an identifier negotiated during the handshake. It is not a substitute for the full boot nonces. The receiver must allocate or validate tags so that an obsolete session cannot be mistaken for the active session. A tag must not be reused within the receiver's current boot session.

The sequence number is 24-bit and must increase monotonically within a session. It must not wrap within that session. At a 50 Hz refresh rate, a 24-bit sequence space lasts approximately 3.9 days. The controller must establish a new session before rollover rather than allowing the sequence to wrap.

The final tag allocation, byte order, sequence semantics and reserved-byte behaviour require agreement with the CTL-017 owner.

### 5.3 Restart and stale-frame handling

A 64-bit producer boot nonce is proposed to identify each I8 boot session. It should be generated using a validated hardware random source. If the source cannot provide a valid nonce, I8 must fail closed and must not grant lift permission.

A static MCU unique identifier alone is not a boot-session identifier.

The handshake must bind both sides of the connection:

- The current I8 producer boot session.
- The current CTL-017 receiver boot session.

A CTL-017 restart invalidates its previous session. It must reject cached or queued permit frames until a new handshake has completed.

An I8 restart similarly invalidates the old producer session. CTL-017 must reject permit frames from the previous producer session.

The negotiated session tag must identify the active pairing, not merely the producer in isolation. A receiver restart must therefore require a new pairing and a new valid permit session before lift commands can be accepted.

No automatic restoration or replay of a previous lift command is allowed after either controller restarts.

### 5.4 Sequence and freshness rules

The receiver must reject:

- Missing permit frames.
- Stale permit frames.
- Duplicate sequence numbers.
- Out-of-order sequence numbers.
- Frames with an incorrect session tag.
- Frames from a previous producer or receiver session.
- Frames received before a valid session handshake.
- Frames received after the permit watchdog expires.

Sequence number freshness and measurement freshness are separate concepts.

A fresh permit frame does not prove that its underlying encoder or arm measurements are fresh. I8 must validate the age and validity of the underlying measurements before granting permission.

Likewise, the receiver's local frame-reception time must not be treated as proof that the producer's underlying measurements are current.

### 5.5 Timing proposals

The following are initial engineering proposals for validation, not frozen acceptance values:

| Parameter | Candidate | Status |
|---|---:|---|
| Permit refresh rate | 50 Hz | Proposed |
| CTL-017 permit receive watchdog | 100 ms | Proposed |
| Base standstill qualification | 500 ms | Proposed |
| Measurement-age limits | To be agreed | Open |
| Arm-status freshness | To be agreed | Open |
| Stop confirmation timeout | To be validated on B5 | Open |
| Physical stopping-time acceptance | To be validated on B5 | Open |

The watchdog measures local receipt freshness. Measurement-age limits measure the age of the underlying sensor evidence. These must remain distinct.

A producer-provided maximum age field may be useful for diagnostics or interface-level reporting, but it must not replace CTL-017's own local receive watchdog.

### 5.6 Diagnostic reason codes

The reason code should identify why permission is denied or withdrawn. The following categories are proposed:

- Base moving.
- Invalid or stale base feedback.
- Missing or stale arm status.
- Unsafe arm pose.
- Arm motion detected.
- Arm-controller fault.
- Base fault or inhibit.
- CTL-017 communication/session fault.
- Session not established.
- Permit watchdog expired.

These are interlock diagnostic reasons, not central `BaseFault` identifiers. Exact numeric values and ownership must be agreed with the interface owners.

---

## 6. Verification and test matrix

Testing must establish the behaviour of both the I8 producer and CTL-017 consumer. A software-only test is not physical acceptance evidence.

| Test | Setup / fault injection | Expected result |
|---|---|---|
| Startup default deny | Start I8 and CTL-017 without a valid session | Permit remains false; lift commands rejected |
| Valid stopped state | Valid fresh wheel feedback; both arms measured `STOWED`; all other prerequisites satisfied | I8 may assert permit after qualification |
| Base moving | Wheel feedback indicates translation | Permit remains false or is withdrawn |
| Base rotating | Opposing wheel motion produces chassis rotation | Permit remains false or is withdrawn |
| Invalid wheel feedback | One encoder channel invalid | Standstill becomes unknown; permit denied |
| Stale wheel measurement | Repeated publication of an old sample with a new message sequence | Measurement remains stale; permit denied |
| Arm unsafe | Either arm reports a state other than `STOWED` | Permit denied |
| Arm moving | Either arm reports active movement | Permit denied or withdrawn |
| Missing arm status | One arm status is absent | Permit denied |
| Stale arm measurement | Old arm measurement is republished with a new message sequence | Permit denied |
| Arm controller fault | Arm status reports a fault | Permit denied |
| Arm inhibit proposal | If approved, remove or invalidate the acknowledgement during permission | Permit withdrawn; CTL-017 stops |
| Producer restart | Restart I8 while CTL-017 remains powered | Previous producer session rejected; new handshake required |
| Receiver restart | Restart CTL-017 while I8 remains powered | Old session and queued frames rejected; new handshake required |
| Wrong session tag | Send a permit frame with an obsolete or incorrect tag | Frame rejected |
| Duplicate sequence | Repeat a previously accepted sequence number | Frame rejected |
| Out-of-order sequence | Send an older sequence number | Frame rejected |
| Sequence rollover boundary | Approach the maximum sequence value | New session established before rollover |
| CAN loss | Interrupt permit communication during lift movement | CTL-017 watchdog expires and stops movement |
| Permit withdrawal | Remove a required prerequisite during movement | I8 withdraws permission; CTL-017 stops |
| No automatic resume | Restore communication and prerequisites after a stop | Previous command is not replayed |
| Local protections | Exercise travel limits, stop sensors, timeout and Hall-window checks | CTL-017 local protections continue to operate independently |
| Stop command availability | Request STOP while permit is false or communication is degraded | STOP remains accepted through the defined control path |

The arm-inhibit acknowledgement test is conditional on the cross-team decision in §3.4. It must not be treated as a committed acceptance test until that interface is approved.

### 6.1 B5 physical validation evidence

B5 testing should record enough information to evaluate the standstill qualification and stopping behaviour.

At minimum, record:

- Initial chassis speed and direction.
- Initial wheel speeds and directions.
- Relevant load and operating condition.
- Encoder sample timestamps or equivalent measurement-age evidence.
- Encoder noise and standstill variation.
- Time at which the stop condition was detected.
- Time at which I8 withdrew permission.
- Time at which CTL-017 received and processed the withdrawal.
- Stop acknowledgement, where available.
- Time until physical lift standstill.
- Additional lift travel after withdrawal.
- Any overshoot, oscillation or unexpected movement.
- Communication and watchdog behaviour.
- Any faults or invalid measurements observed.

The B5 results must be used to validate or revise the proposed thresholds and timeouts. Passing a simulation test does not establish physical stopping performance.

---

## 7. Open decisions and ownership

The following items remain open and must be resolved before the relevant implementation details are treated as approved.

| Decision | Required agreement / evidence | Owner |
|---|---|---|
| Gate B encoder scaling and validity | Confirm sensor configuration, scaling and measured behaviour | I8 / platform firmware |
| Standstill thresholds | Validate candidate linear/angular limits, qualification period and hysteresis against B5 data | I8 / platform firmware |
| Gate B drivetrain geometry | Confirm effective wheel radius and separation | Platform mechanical / firmware |
| ArmStatus schema | Agree message fields, sample-age semantics, validity and session handling | Manipulation + interface owners |
| STOWED joint-space definition | Approve measured joint-space values, tolerances, settling and collision envelope | Manipulation |
| Arm-motion-inhibit acknowledgement | Decide whether required; define producer, consumer, semantics and failure behaviour | Manipulation + I8 |
| Arm-status freshness limit | Agree maximum measurement age and clock/age mechanism | Manipulation + I8 |
| Permit refresh and timeout | Validate refresh period, watchdog and stop behaviour | I8 + CTL-017 |
| CAN PDO / object-dictionary mapping | Agree COB-ID, byte layout, transmission type and configuration | I8 + CTL-017 |
| Session handshake | Agree nonce exchange, receiver binding, tag allocation and restart behaviour | I8 + CTL-017 |
| Sequence semantics | Agree byte order, duplicate/out-of-order handling and re-handshake before rollover | I8 + CTL-017 |
| Stop-time acceptance | Validate physical stopping time and additional travel on B5 | CTL-017 + platform integration |
| Diagnostic reason codes | Agree values and mapping to diagnostic interfaces | I8 + interface owners |

### 7.1 Cross-repository dependencies

The work intersects with:

- I8 interface definition: [openamrobot-interfaces PR #38](https://github.com/openAMRobot/openamrobot-interfaces/pull/38)
- Base-controller and platform firmware work.
- CTL-017 actuator firmware and CANopen object-dictionary configuration.
- Manipulation runtime arm-status interface and safe-pose definition.

Related tracked work includes the I8, manipulation and platform integration issues identified in the #20 discussion. Their interface and acceptance details must be checked against the current owning issues before implementation is considered complete.

### 7.2 Responsibility boundaries

- **I8 / platform firmware:** Base standstill qualification, evaluation of arm status, authoritative permit generation, session producer behaviour and permit withdrawal.
- **CTL-017:** Permit reception and validation, session consumer behaviour, command gating, watchdog handling, stop response and local actuator protections.
- **Manipulation:** Measured arm status, arm-state semantics, safe-pose definition, arm-controller health and any agreed arm-motion-inhibit interface.
- **Interface owners:** Message schemas, CAN/PDO mapping, timing contracts and compatibility across repositories.
- **Integration team:** B5/HIL evidence and physical acceptance.

---

## 8. Acceptance boundary and release considerations

This proposal is intended to support implementation and cross-team review of issue #20.

The software and HIL acceptance criteria must demonstrate that:

1. I8 is the sole authoritative producer of `lift_permit`.
2. Lift permission is denied unless the base and both arms satisfy the agreed prerequisites.
3. CTL-017 rejects lift movement commands without a valid current-session permit.
4. CTL-017 stops movement when permission is withdrawn or its permit watchdog expires.
5. Restart, stale-data, sequence and communication faults fail closed.
6. No previous lift command is automatically replayed after permission returns.
7. CTL-017's independent local protections remain effective.
8. E-01 remains independent.

The robot integration and physical acceptance window identified in the #20 discussion is **23 November–18 December 2026**. Physical acceptance must be based on the agreed robot-level test evidence, not simulation results or this document alone.

**This document does not claim that the interlock has been implemented, tested on the physical robot, safety-validated or approved by all subsystem owners.**