/* SPDX-License-Identifier: MIT */
/*
 * Telemetry image: every timing constant and every tunable of the image, in
 * one place, each with its source.
 *
 * Rule: a value that is still open (marked TODO(Evans) in the module that
 * consumes it, or stated there as "no default") is NOT active. Its *_SET
 * macro is 0, the feature that needs it stays disabled, and the boot banner
 * and every status line name the disabled feature. Nothing in the image
 * falls back to a guessed number.
 *
 * TLM_TEST_PARAMS (CMake option, preset "RenodeTest") activates the block of
 * test values at the end of this file. Those values are NOT approved; they
 * exist so the Renode tests can exercise the code paths. An image built with
 * them prints params "test" in the banner and in every status line, and must
 * not be flashed to a board that is connected to a drive.
 */
#ifndef TLM_PARAMS_H
#define TLM_PARAMS_H

/* ------------------------------------------------------------------------ */
/* Values with a source: always active                                        */
/* ------------------------------------------------------------------------ */

/* Console: USART3 on PD8 (TX) and PD9 (RX), 115200 8N1.
 * Source: task statement of this image (diagnostic console). */
#define TLM_CONSOLE_BAUD 115200u

/* Status line period. Source: task statement of this image (one line per 100 ms). */
#define TLM_REPORT_PERIOD_MS 100u

/* Time allowed for one console write before it is abandoned and counted.
 * Source: one report period; a line that cannot leave in one period is dropped. */
#define TLM_CONSOLE_TX_TIMEOUT_MS TLM_REPORT_PERIOD_MS

/* Console budget. 8N1 is 10 bit times per character, so one report period
 * carries TLM_CONSOLE_BAUD / 10 * TLM_REPORT_PERIOD_MS / 1000 characters
 * (1152 at 115200 baud and 100 ms). Source: task statement of this change:
 * one status line takes at most 60 % of the period. The slow line carries
 * the rarely changing fields once every TLM_SLOW_EVERY periods, directly
 * after the status line of that period, and is capped at 35 %, so a period
 * that carries both stays at or below 95 % of the wire time. */
#define TLM_CONSOLE_CHARS_PER_PERIOD (TLM_CONSOLE_BAUD / 10u * TLM_REPORT_PERIOD_MS / 1000u)
#define TLM_STATUS_LINE_MAX (TLM_CONSOLE_CHARS_PER_PERIOD * 60u / 100u) /* 691, newline included */
#define TLM_SLOW_LINE_MAX (TLM_CONSOLE_CHARS_PER_PERIOD * 35u / 100u)   /* 403, newline included */
/* Slow line every 5 periods. Its content is split into two parts that
 * alternate ("cfg", then "diag"), so each part is refreshed once per second
 * and each fits TLM_SLOW_LINE_MAX. Source: this image. */
#define TLM_SLOW_EVERY 5u

/* Bound of the console wait in tlm_fatal(): spins of the TX-ready poll for
 * the whole fatal record, not per character. Source: this image; about
 * 0.1 s at 550 MHz, far more than the 60-odd characters need at 115200. */
#define TLM_FATAL_TX_SPIN_MAX 10000000u

/* CAN1 nominal bit rate, for the banner only. The bit timing itself is the
 * generated MX_FDCAN1_Init() (prescaler 6, seg1 13, seg2 2 at 48 MHz).
 * Source: boards/stm32/OpenAMRobot/OpenAMRobot.ioc, README table (500 kbit/s). */
#define TLM_CAN1_BITRATE 500000u

/* CANopen node id of the drive on CAN1.
 * Source: task statement (node 1); docs/architecture/can1-drive-link.md scope. */
#define TLM_DRIVE_NODE_ID 1u

/* Period of the can1_reader tick: at most one SDO upload request is started
 * per tick, so five objects are refreshed every 5 ticks.
 * Source: docs/architecture/can1-drive-link.md section 3, the 20 ms cycle
 * assumed there for one transfer per tick (2.7 % bus load at 500 kbit/s). */
#define TLM_CAN1_TICK_MS 20u

/* Period at which the sensors task drains the UART receive rings.
 * Source: derived. The rings hold TLM_UART_RING_SIZE bytes; the fastest
 * stream is a TF-Luna at 115200 baud, default 100 frames/s of 9 bytes
 * (SJ-PM-TF-Luna A05) = 900 bytes/s, so 5 ms is 4.5 bytes per poll. */
#define TLM_SENSORS_POLL_MS 5u
#define TLM_UART_RING_SIZE 64u

/* FreeRTOS tick. Source: FreeRTOSConfig.h of this image (1 kHz, the same
 * rate as the HAL tick the generated code sets up). */
#define TLM_TICK_HZ 1000u

/* Task priorities (fixed; larger is more urgent) and stack depths in words.
 * Source: this image. can1_reader must see a response before the next tick;
 * the reporter may be preempted while it prints. */
#define TLM_PRIO_CAN1_TICK 4u
#define TLM_PRIO_CAN1_READER 3u
#define TLM_PRIO_SENSORS 2u
#define TLM_PRIO_REPORTER 1u
#define TLM_STACK_CAN1_TICK 320u
#define TLM_STACK_CAN1_READER 384u
#define TLM_STACK_SENSORS 384u
#define TLM_STACK_REPORTER 640u

/* CAN1 receive queue between the FDCAN1 interrupt and can1_reader, in frames.
 * Source: this image; twice the generated RX FIFO 0 depth (16 elements). */
#define TLM_CAN1_RX_QUEUE_LEN 32u

/* Frames taken from FDCAN1 RX FIFO 0 per interrupt entry; frames left in the
 * FIFO are taken on the next entry, which the handler requests by pending
 * its own interrupt again. Source: this image; a quarter of the 16-element
 * FIFO keeps one entry short while two entries in a row still empty a FIFO
 * that fills at the bus rate. */
#define TLM_CAN1_RX_MAX_PER_IRQ 4u

/* Restart attempts of a sensor UART whose reception stopped (a
 * HAL_UART_Receive_IT call did not return HAL_OK). After this many failed
 * attempts in a row the port is reported failed and left alone.
 * Source: this image. */
#define TLM_UART_RX_RESTART_MAX 3u

/* CAN2 and CAN3 nominal bit rates, for the banner only. The bit timing is the
 * generated MX_FDCAN2_Init() and MX_FDCAN3_Init() (prescaler 12 and 6, seg1 13,
 * seg2 2 at 48 MHz). Source: boards/stm32/OpenAMRobot/OpenAMRobot.ioc. */
#define TLM_CAN2_BITRATE 250000u
#define TLM_CAN3_BITRATE 500000u

/* CANopen node id of the CTL-017 lift controller on CAN3.
 * Source: openamr-upperbody-fw PR #17 (8dc7236), docs/ctl017_object_dictionary.md
 * section 1 (Alex 24 Oct, P-03 item 29), proposal v1. */
#define TLM_LIFT_NODE_ID 0x10u

/* The Daly decoder stays locked: protocol_confirmed is set only by the owner
 * after a CAN capture of the delivered BMS matches daly_rx.h (PR #48). No
 * build of this image, the test build included, unlocks it; battery values
 * are reported as "locked" and never printed. */
#define TLM_DALY_PROTOCOL_CONFIRMED 0

/* Receive queue of CAN2 and CAN3 together, in frames; frames taken from each
 * RX FIFO 0 per interrupt entry (both FIFOs have 16 elements, generated init).
 * Source: this image. The lift sends about 30 frames/s (TPDO1, TPDO2, heartbeat
 * at 100 ms, upperbody-fw #17 section 3 and 4 proposals), the BMS answers at
 * 5 Hz (BOM CAN buses sheet, proposed): an entry takes the whole FIFO. */
#define TLM_CAN_AUX_RX_QUEUE_LEN 32u
#define TLM_CAN_AUX_RX_MAX_PER_IRQ 16u
#define TLM_PRIO_CAN_AUX 3u
#define TLM_STACK_CAN_AUX 384u

/* ------------------------------------------------------------------------ */
/* Open values: inactive unless TLM_TEST_PARAMS is defined                    */
/* ------------------------------------------------------------------------ */
#ifndef TLM_TEST_PARAMS

#define TLM_PARAMS_NAME "default"

/* Heartbeat consumer timeout for node 1.
 * Source: none approved. co_hb_consumer.h: "TODO(Evans): D-24 proposes 300 ms". */
#define TLM_HB_TIMEOUT_SET 0
#define TLM_HB_TIMEOUT_US 0u

/* SDO upload timeout per request and retry limit.
 * Source: none approved. can1-drive-link.md section 6: both TODO(Evans). */
#define TLM_SDO_TIMEOUT_SET 0
#define TLM_SDO_TIMEOUT_US 0u
#define TLM_SDO_RETRIES_SET 0
#define TLM_SDO_RETRIES 0u
/* Guard time after a retry inside which a response is counted late
 * (co_upload_sched.h, late_guard_us: no default). Used only with retries. */
#define TLM_SDO_LATE_GUARD_SET 0
#define TLM_SDO_LATE_GUARD_US 0u

/* Overall SDO health (co_upload_sched.h): N consecutive protocol errors and
 * the time without one complete valid cycle. No default; unset disables the
 * criterion and the slow line and the banner say so. */
#define TLM_SDO_HEALTH_N_SET 0
#define TLM_SDO_HEALTH_N 0u
#define TLM_SDO_HEALTH_TIME_SET 0
#define TLM_SDO_HEALTH_TIME_US 0u

/* Maximum age of a drive sample before it is reported STALE.
 * Source: none approved. zlac_telemetry.h: "TODO(Evans): tie to the scheduler cycle time". */
#define TLM_DRIVE_MAX_AGE_SET 0
#define TLM_DRIVE_MAX_AGE_US 0u

/* Value 2026h:05 must read before a velocity is reported as valid.
 * Source: none approved. zlac_telemetry.h: "no default: D-24 asks for 1". */
#define TLM_SPEED_RESOLUTION_SET 0
#define TLM_SPEED_RESOLUTION_REQUIRED 0u

/* TF-Luna output format (1 = 9-byte/cm, 2 = 9-byte/mm).
 * Source: none confirmed. tfluna.h: "must match the format set in the sensor;
 * no default". The factory default is cm, but the installed sensors' setting
 * is not recorded. */
#define TLM_TFLUNA_UNIT_SET 0
#define TLM_TFLUNA_UNIT 0

/* Age at which a TF-Luna or MB7060 sample is reported stale.
 * Source: none. freshness.h: "The stale limit has no default". */
#define TLM_TFLUNA_STALE_SET 0
#define TLM_TFLUNA_STALE_US 0u
#define TLM_MB7060_STALE_SET 0
#define TLM_MB7060_STALE_US 0u

/* CTL-017 heartbeat consumer timeout. Source: none approved.
 * upperbody-fw #17 section 2: "node lost after 300 ms" (D15, TODO(Evans)). */
#define TLM_LIFT_HB_TIMEOUT_SET 0
#define TLM_LIFT_HB_TIMEOUT_US 0u

/* Daly BMS and host addresses on CAN2, cell and sensor counts.
 * Source: none confirmed. daly_rx.h: "Addresses have no default" (TODO(Evans)). */
#define TLM_DALY_ADDR_SET 0
#define TLM_DALY_BMS_ADDR 0u
#define TLM_DALY_HOST_ADDR 0u
#define TLM_DALY_N_CELLS 0u
#define TLM_DALY_N_TEMPS 0u

#else /* TLM_TEST_PARAMS ---------------------------------------------------- */

/* TEST VALUES. Not approved, not measured, not for a board on a drive. Each
 * is the proposal named in its source so the Renode tests exercise the path
 * an owner-approved value would take. */
#define TLM_PARAMS_NAME "test"

/* D-24 proposal quoted in co_hb_consumer.h (300 ms). */
#define TLM_HB_TIMEOUT_SET 1
#define TLM_HB_TIMEOUT_US 300000u

/* Test only: two ticks of TLM_CAN1_TICK_MS, no retry, so a missing answer is
 * visible within one pass of the object list. */
#define TLM_SDO_TIMEOUT_SET 1
#define TLM_SDO_TIMEOUT_US 40000u
#define TLM_SDO_RETRIES_SET 1
#define TLM_SDO_RETRIES 0u
/* No retry in the test values, so the guard is not used (co_upload_sched.h). */
#define TLM_SDO_LATE_GUARD_SET 0
#define TLM_SDO_LATE_GUARD_US 0u

/* Test only: 8 protocol errors in a row; one second, two passes of the
 * five-object list at one request per 20 ms tick. */
#define TLM_SDO_HEALTH_N_SET 1
#define TLM_SDO_HEALTH_N 8u
#define TLM_SDO_HEALTH_TIME_SET 1
#define TLM_SDO_HEALTH_TIME_US 1000000u

/* Test only: three passes over the five objects at one per 20 ms tick. */
#define TLM_DRIVE_MAX_AGE_SET 1
#define TLM_DRIVE_MAX_AGE_US 300000u

/* D-24 proposal quoted in zlac_telemetry.h (1). */
#define TLM_SPEED_RESOLUTION_SET 1
#define TLM_SPEED_RESOLUTION_REQUIRED 1u

/* Factory default format of the TF-Luna manual (9-byte/cm). */
#define TLM_TFLUNA_UNIT_SET 1
#define TLM_TFLUNA_UNIT 1

/* Test only: 200 ms, twenty frame periods at the 100 Hz default rate. */
#define TLM_TFLUNA_STALE_SET 1
#define TLM_TFLUNA_STALE_US 200000u
/* Test only: 300 ms, about three 99 ms MB7060 cycles (PD11838z). */
#define TLM_MB7060_STALE_SET 1
#define TLM_MB7060_STALE_US 300000u

/* D15 proposal quoted in upperbody-fw #17 section 2 (300 ms). */
#define TLM_LIFT_HB_TIMEOUT_SET 1
#define TLM_LIFT_HB_TIMEOUT_US 300000u

/* V1.0 addresses as recorded in daly_rx.h (BMS 0x01, host 0x40), unconfirmed;
 * 8S pack (E-01 rev1.8), one temperature sensor (SIL test value of
 * openamr-platform-sw config/sil.yaml). The decoder stays locked. */
#define TLM_DALY_ADDR_SET 1
#define TLM_DALY_BMS_ADDR 0x01u
#define TLM_DALY_HOST_ADDR 0x40u
#define TLM_DALY_N_CELLS 8u
#define TLM_DALY_N_TEMPS 1u

#endif /* TLM_TEST_PARAMS */

/* The MB7060 trigger sequencer (reading timeout and gap, both TODO(Evans) in
 * mb7060.h) is not part of this image at all: the image sends no trigger
 * pulse, so the parser is never armed and every MB7060 range is reported
 * unknown. There is no parameter to set for it here. */
#define TLM_MB7060_TRIGGER_SET 0

#endif /* TLM_PARAMS_H */
