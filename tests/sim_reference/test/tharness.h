/* SIMULATION ONLY. Not production firmware. Not reviewed for safety. Must not be linked into any board image. */
#ifndef SIMREF_THARNESS_H
#define SIMREF_THARNESS_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "simref/simref_ctrl.h"

extern int th_failures, th_checks;
#define CHECK(c) do { th_checks++; if (!(c)) { th_failures++; fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c); } } while (0)
#define CHECK_EQ(a, b) do { long long _a = (long long)(a), _b = (long long)(b); th_checks++; if (_a != _b) { th_failures++; fprintf(stderr, "%s:%d: CHECK_EQ failed: %s (%lld) != %s (%lld)\n", __FILE__, __LINE__, #a, _a, #b, _b); } } while (0)
#define RUN(fn) do { int _f = th_failures; fn(); fprintf(stderr, "%s %s\n", th_failures == _f ? "PASS" : "FAIL", #fn); th_cases++; } while (0)
extern int th_cases;
int th_report(const char *suite);

/* A fake ZLAC8015D node 1 and a fake CTL-017 heartbeat, answering at once, on a simulated clock. */
typedef struct {
    bool silent;           /* no heartbeat, no SDO answer */
    bool booted;
    int64_t next_hb_us;
    uint8_t nmt;
    uint16_t sw_state;     /* 0x40 SOD, 0x21 RTSO, 0x23 SO, 0x27 OE (with voltage bit 0x10) */
    uint16_t pending_sw;
    int64_t pending_due_us;
    int64_t transition_delay_us;
    int abort_word;        /* controlword value that is aborted, -1 none */
    bool fault;
    int32_t target[2];
    uint16_t v1017, v2000;
    uint32_t writes_60ff, writes_6040, nmt_starts;
    int32_t last_60ff_nonzero_until_us;
} th_drive_t;

typedef struct {
    simref_ctrl_t c;
    th_drive_t d;
    int64_t t;
    bool lift_alive;
    int64_t lift_next_hb;
    /* every frame the controller sent on the drive bus */
    uint32_t tx_count[2];
    int64_t last_nonzero_target_us;
    int64_t first_zero_after_nonzero_us;
    uint32_t controlword_0f_writes;
} th_world_t;

void th_world_init(th_world_t *w, simref_cause_t cause);
void th_world_run(th_world_t *w, int64_t dt_us);      /* advance with 1 ms steps */
void th_world_pins(th_world_t *w, int y32, int estop, int reset, int charger_fault, int contactor);
void th_world_pin(th_world_t *w, simref_pin_t p, int level);
/* host session up, jetson stop released, RESET pressed for press_ms and released */
void th_world_arm_to_run(th_world_t *w);
void th_world_press_reset(th_world_t *w, int64_t hold_ms);
void th_world_host_hb(th_world_t *w);
void th_world_cmd(th_world_t *w, double v, double om);
void th_drive_power_cycle(th_world_t *w);
bool th_reason_logged(const simref_ctrl_t *c, simref_reason_t r);

#endif
