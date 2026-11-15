/* SIMULATION ONLY. Not production firmware. Not reviewed for safety. Must not be linked into any board image. */
/*
 * One compile-time option per open conflict (C1 to C8 of docs/safety/spec/state_machine.yaml
 * version 2) and per tick-semantics reading. The default of each option is the reading the
 * Python model (tools/spec/model.py) uses. Every option is an interpretation, not an owner
 * ruling. Agreement with the Python model is not owner approval.
 */
#ifndef SIMREF_OPTIONS_H
#define SIMREF_OPTIONS_H

/* C1 drive link timing. 1: side b (D-24: 2000h 250 ms, 1017h 100 ms, consumer 300 ms).
 * 0: side a (2000h 500 ms, 1017h 200). Interpretation, not an owner ruling. */
#ifndef SIMREF_OPT_C1_DRIVE_TIMING_SIDE_B
#define SIMREF_OPT_C1_DRIVE_TIMING_SIDE_B 1
#endif

/* C2 base stopped definition. 1: |v| and |w| below thresholds from both wheels (#20, the
 * model's permit producer). 0: both wheels below 1 rpm (rev 5 5.5). Interpretation, not an owner ruling. */
#ifndef SIMREF_OPT_C2_STOPPED_BY_TWIST
#define SIMREF_OPT_C2_STOPPED_BY_TWIST 1
#endif

/* C3 WCM_FAULT re-asserted in LOCKED. 0: does not block RUN (rev 5 5.3, model default
 * wcm_blocks_run false). 1: blocks RUN entry (row Q1). Interpretation, not an owner ruling. */
#ifndef SIMREF_OPT_C3_WCM_BLOCKS_RUN
#define SIMREF_OPT_C3_WCM_BLOCKS_RUN 0
#endif

/* C4 UI Resume rejected in INHIBITED. 1: stay INHIBITED, latch kept (row V3, model).
 * 0: go to LOCKED (rev 5 2.3). Interpretation, not an owner ruling. */
#ifndef SIMREF_OPT_C4_RESUME_REJECT_STAYS_INHIBITED
#define SIMREF_OPT_C4_RESUME_REJECT_STAYS_INHIBITED 1
#endif

/* C5 charge interlock outside a charge attempt. 1: not applicable in LOCKED, RUN, INHIBITED
 * (spec error), handled in CHARGER_FAULT by K2 (version 2, F1). 0: latched in any state
 * (H21 text). Interpretation, not an owner ruling. */
#ifndef SIMREF_OPT_C5_INTERLOCK_ONLY_IN_ATTEMPT
#define SIMREF_OPT_C5_INTERLOCK_ONLY_IN_ATTEMPT 1
#endif

/* C6 drive fault in CHARGING. 1: stay, record (rows D2, D4, H30). 0: go to LOCKED.
 * Interpretation, not an owner ruling. */
#ifndef SIMREF_OPT_C6_DRIVE_FAULT_IN_CHARGING_STAYS
#define SIMREF_OPT_C6_DRIVE_FAULT_IN_CHARGING_STAYS 1
#endif

/* C7 lift permit carrier. 1: dedicated cyclic permit PDO (#20, SIL layout of platform-sw
 * config/sil.yaml). 0: carried in the CTL-017 RPDO1 (not implemented; the build fails).
 * Interpretation, not an owner ruling. */
#ifndef SIMREF_OPT_C7_DEDICATED_PERMIT_PDO
#define SIMREF_OPT_C7_DEDICATED_PERMIT_PDO 1
#endif
#if !SIMREF_OPT_C7_DEDICATED_PERMIT_PDO
#error "C7 side a (permit in RPDO1) is not implemented in the simulation reference"
#endif

/* C8 approved arm poses of the process default. 1: STOWED only (platform lead, Build 1).
 * 0: STOWED and CARRY (owner proposal). The adapter passes each vector's own
 * approved_poses. Interpretation, not an owner ruling. */
#ifndef SIMREF_OPT_C8_STOWED_ONLY
#define SIMREF_OPT_C8_STOWED_ONLY 1
#endif

/* Tick semantics (tick_semantics, status INTERPRETATION).
 * APPLICABILITY_CURRENT_STATE 1: applicability judged on the current state (version 2).
 * 0: on the state at the start of the tick (version 1). Interpretation, not an owner ruling. */
#ifndef SIMREF_OPT_TICK_APPLICABILITY_CURRENT_STATE
#define SIMREF_OPT_TICK_APPLICABILITY_CURRENT_STATE 1
#endif
/* ORDER_STABLE_OTHERS 1: events of rank "others" keep arrival order (stable sort, model).
 * 0: "others" sorted by event id. Interpretation, not an owner ruling. */
#ifndef SIMREF_OPT_TICK_ORDER_STABLE_OTHERS
#define SIMREF_OPT_TICK_ORDER_STABLE_OTHERS 1
#endif

/* Not a spec conflict, a gap: the specification has no contactor event (vector h14 makes
 * no claim about contactor state). 1: feedback that disagrees with the commanded state for
 * SIMREF_CONTACTOR_SETTLE_MS raises input_invalid{IMPLAUSIBLE} with detail
 * CONTACTOR_FEEDBACK. 2: raises estop_diag. Interpretation, not an owner ruling. */
#ifndef SIMREF_OPT_CONTACTOR_MISMATCH_EVENT
#define SIMREF_OPT_CONTACTOR_MISMATCH_EVENT 1
#endif

#endif
