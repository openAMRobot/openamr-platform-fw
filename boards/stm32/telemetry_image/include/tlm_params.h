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
#define TLM_PRIO_CAN1_READER 3u
#define TLM_PRIO_SENSORS 2u
#define TLM_PRIO_REPORTER 1u
#define TLM_STACK_CAN1_READER 384u
#define TLM_STACK_SENSORS 384u
#define TLM_STACK_REPORTER 640u

/* CAN1 receive queue between the FDCAN1 interrupt and can1_reader, in frames.
 * Source: this image; twice the generated RX FIFO 0 depth (16 elements). */
#define TLM_CAN1_RX_QUEUE_LEN 32u

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

#endif /* TLM_TEST_PARAMS */

/* The MB7060 trigger sequencer (reading timeout and gap, both TODO(Evans) in
 * mb7060.h) is not part of this image at all: the image sends no trigger
 * pulse, so the parser is never armed and every MB7060 range is reported
 * unknown. There is no parameter to set for it here. */
#define TLM_MB7060_TRIGGER_SET 0

#endif /* TLM_PARAMS_H */
