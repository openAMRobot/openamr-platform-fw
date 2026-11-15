/* SPDX-License-Identifier: MIT */
/* micro-ROS client of the TelemetryUrosSerialSimBench preset (src/tlm_uros.c). */
#ifndef TLM_UROS_H
#define TLM_UROS_H

#include <stdint.h>

#include "tlm_status.h"

/* Node /base_fw/openamr_base_fw; topics /base_fw/<name> (Proposed, README). */
#define TLM_UROS_NODE_NAME "openamr_base_fw"
#define TLM_UROS_NAMESPACE "base_fw"
/* Arena for rcl, rclc and rmw allocations. Source: measured high-water mark
 * 2064 bytes in Renode over two sessions ("arena" in the status topic, 15 Nov
 * 2026), about twice that; README "micro-ROS". */
#define TLM_UROS_ARENA_BYTES 4096u
/* One ping attempt per check. Source: this image; half a report period. */
#define TLM_UROS_PING_TIMEOUT_MS 50

typedef struct
{
    const char *state;
    uint32_t sessions, losses, published, publish_failures;
    uint32_t arena_high_water, arena_failures, fini_errors;
} tlm_uros_report_t;

/* Before the scheduler starts: allocator, transport, messages, state machine. */
void tlm_uros_init(uint32_t boot_id);
/* Once per report period, from the reporter task. */
void tlm_uros_period(const tlm_status_t *status, uint32_t sens_missed);
tlm_uros_report_t tlm_uros_report(void);

#endif /* TLM_UROS_H */
