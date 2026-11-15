/* SIMULATION ONLY. Not production firmware. Not reviewed for safety. Must not be linked into any board image. */
/*
 * Every timing value of the simulation reference. One header, nothing elsewhere.
 * Each value is a proposed test value, not approved. None of them is an owner ruling and
 * none of them may be copied into a board image.
 *
 * Boundary rule used by every timer in this library: a timer "has expired" when the age
 * is greater than or equal to the limit (age 199 ms with a 200 ms limit is fresh, 200 ms
 * is expired). Interpretation, not an owner ruling.
 */
#ifndef SIMREF_TIMING_H
#define SIMREF_TIMING_H

/* Command watchdog: age of the last valid motion command (I8 decision 3, open). */
#define SIMREF_CMD_WATCHDOG_MS 200u              /* proposed test value, not approved */
/* Heartbeat consumer for drive node 1 (D-24 proposal, conflict C1 side b). */
#define SIMREF_DRIVE_HB_CONSUMER_MS 300u         /* proposed test value, not approved */
/* Lift permit producer period on the virtual lift bus. */
#define SIMREF_PERMIT_PERIOD_MS 50u              /* proposed test value, not approved */
/* Lift permit expiry on the consumer (CTL-017 emulator config, not used by the producer). */
#define SIMREF_PERMIT_EXPIRY_MS 150u             /* proposed test value, not approved */

/* Further values the reference needs. Same status. */
#define SIMREF_TICK_MS 10u                       /* proposed test value, not approved: control tick */
#define SIMREF_DRIVE_PERIOD_MS 20u               /* proposed test value, not approved: CAN1 cycle (50 Hz, can1-drive-link.md) */
#define SIMREF_SDO_TIMEOUT_MS 50u                /* proposed test value, not approved */
#define SIMREF_CIA402_TRANSITION_LIMIT_MS 100u   /* proposed test value, not approved: E4 to E6 statusword */
#define SIMREF_DRIVE_STATUS_EVERY_N_CYCLES 5u    /* proposed test value, not approved: 6041h and 603Fh read */
#define SIMREF_SESSION_TIMEOUT_MS 500u           /* proposed test value, not approved: host session (I8, open) */
#define SIMREF_INPUT_DEBOUNCE_MS 20u             /* proposed test value, not approved: rev 5 debounce */
#define SIMREF_RESET_INVALID_MS 2000u            /* proposed test value, not approved: RESET high 2 s (T7) */
#define SIMREF_RESET_REBOOT_MS 5000u             /* proposed test value, not approved: RESET high 5 s (T8) */
#define SIMREF_CONTACTOR_SETTLE_MS 100u          /* proposed test value, not approved: feedback may lag the command */
#define SIMREF_CHARGE_INTERLOCK_WINDOW_MS 500u   /* proposed test value, not approved: rev 5 5.6 */
#define SIMREF_CHARGE_INTERLOCK_HIGH_MS 20u      /* proposed test value, not approved: rev 5 5.6 */
#define SIMREF_ENCODER_STALE_MS 100u             /* proposed test value, not approved: wheel evidence */
#define SIMREF_ARM_STALE_MS 200u                 /* proposed test value, not approved: arm evidence */
#define SIMREF_STOP_HOLD_MS 500u                 /* proposed test value, not approved: #20 proposal (C2) */
#define SIMREF_CTL017_HB_CONSUMER_MS 300u        /* proposed test value, not approved: lift node 0x10 */
#define SIMREF_CORE_HB_PERIOD_MS 100u            /* proposed test value, not approved: core heartbeat on CAN3 */
#define SIMREF_LIFT_KEEPALIVE_MS 100u            /* proposed test value, not approved: CTL-017 RPDO1 keep-alive */
#define SIMREF_STATUS_PERIOD_MS 50u              /* proposed test value, not approved: status to the simulation */

#endif
