/* SIMULATION ONLY. Not production firmware. Not reviewed for safety. Must not be linked into any board image. */
/*
 * Flat C entry points for the ctypes adapter (docs/safety/spec/adapter.md section 2):
 * create a context, feed one tick, read a JSON snapshot. Built only into the host shared
 * library libsimref_adapter.so.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "simref/simref_permit.h"
#include "simref/simref_sm.h"

#define EXPORT __attribute__((visibility("default")))

EXPORT void *simref_ad_sm_new(void);
EXPORT void simref_ad_free(void *h);
EXPORT int simref_ad_sm_tick(void *h, unsigned inputs, int n, const int *ids, const int *p, const int *old,
                             const int *replay, const int *queued);
EXPORT int simref_ad_sm_snapshot(void *h, char *buf, int cap);
EXPORT int simref_ad_event_count(void);
EXPORT const char *simref_ad_event_name(int i);
EXPORT void *simref_ad_permit_new(unsigned set, double v_max, double w_max, double track, long long hold_us,
                                  long long wheel_stale_us, long long arm_stale_us, unsigned poses);
EXPORT void simref_ad_permit_boot(void *h);
EXPORT void simref_ad_permit_begin(void *h);
EXPORT void simref_ad_permit_session(void *h, int src, unsigned session);
EXPORT void simref_ad_permit_wheel(void *h, long long t, int side, unsigned session, long long seq, int valid, double v);
EXPORT void simref_ad_permit_arms(void *h, long long t, unsigned session, long long seq, int valid, int lp, int lm,
                                  int lf, int rp, int rm, int rf, int bim);
EXPORT void simref_ad_permit_flags(void *h, int base_fault, int set_fault, int link_ok, int set_link);
EXPORT void simref_ad_permit_evaluate(void *h, long long t);
EXPORT int simref_ad_permit_snapshot(void *h, char *buf, int cap);

void *simref_ad_sm_new(void)
{
    simref_sm_t *sm = malloc(sizeof *sm);
    if (sm)
        simref_sm_init(sm);
    return sm;
}

void simref_ad_free(void *h) { free(h); }
int simref_ad_event_count(void) { return SIMREF_EV_COUNT; }
const char *simref_ad_event_name(int i) { return simref_event_name((simref_event_id_t)i); }

int simref_ad_sm_tick(void *h, unsigned inputs, int n, const int *ids, const int *p, const int *old,
                      const int *replay, const int *queued)
{
    simref_sm_inputs_t in = {.run_ok = inputs & 1u, .base_stopped = (inputs >> 1) & 1u, .arming_ok = (inputs >> 2) & 1u,
                             .qualified = (inputs >> 3) & 1u, .charge_pre_ok = (inputs >> 4) & 1u};
    simref_event_t ev[SIMREF_SM_MAX_TICK_EVENTS];
    if (n < 0 || n > SIMREF_SM_MAX_TICK_EVENTS)
        return -1;
    for (int i = 0; i < n; i++) {
        ev[i] = (simref_event_t){.id = (simref_event_id_t)ids[i], .cause = (uint8_t)p[i], .subcause = (uint8_t)p[i],
                                 .kind = (uint8_t)p[i], .session_old = old[i] != 0, .seq_replay = replay[i] != 0,
                                 .queued = queued[i] != 0};
    }
    return (int)simref_sm_tick(h, &in, ev, (size_t)n);
}

#define B(x) ((x) ? "true" : "false")
#define P(...) do { int k = snprintf(buf + o, (size_t)(cap - o), __VA_ARGS__); if (k < 0 || k >= cap - o) return -1; o += k; } while (0)

int simref_ad_sm_snapshot(void *h, char *buf, int cap)
{
    const simref_sm_t *sm = h;
    int o = 0;
    P("{\"state\":\"%s\",\"latches\":{\"reset_seen\":%s,\"ui_stop\":%s,\"jetson_stop\":%s,\"wcm_latched\":%s,"
      "\"interlock_latched\":%s},", simref_state_name(sm->state), B(sm->latches.reset_seen), B(sm->latches.ui_stop),
      B(sm->latches.jetson_stop), B(sm->latches.wcm_latched), B(sm->latches.interlock_latched));
    P("\"outputs\":{\"motor\":\"%s\",\"lamp\":\"%s\",\"charge_request\":\"%s\"},", sm->motor_follow ? "FOLLOW_CMD" : "ZERO",
      simref_sm_lamp_on(sm->state) ? "ON" : "OFF", simref_sm_charge_request_on(sm->state) ? "ON" : "OFF");
    P("\"reasons\":[");
    for (unsigned i = 0; i < sm->n_reasons; i++)
        P("%s\"%s\"", i ? "," : "", simref_reason_name(sm->reasons[i]));
    P("],\"reason_mask\":[");
    for (unsigned i = 0; i < sm->n_mask; i++)
        P("%s\"%s\"", i ? "," : "", simref_reason_name((simref_reason_t)sm->reason_mask[i]));
    if (sm->first_reason)
        P("],\"first_reason\":\"%s\",", simref_reason_name(sm->first_reason));
    else
        P("],\"first_reason\":null,");
    P("\"results\":[");
    for (unsigned i = 0; i < sm->n_results; i++)
        P("%s\"%s\"", i ? "," : "", sm->results[i] == SIMREF_RES_ACCEPTED ? "accepted" : "rejected");
    P("],\"rows\":[");
    for (unsigned i = 0; i < sm->n_rows; i++)
        P("%s\"%s\"", i ? "," : "", simref_sm_row_id(sm->rows[i]));
    P("],\"motion_admissible\":%s}", B(simref_sm_motion_admissible(sm)));
    return o;
}

void *simref_ad_permit_new(unsigned set, double v_max, double w_max, double track, long long hold_us,
                           long long wheel_stale_us, long long arm_stale_us, unsigned poses)
{
    simref_permit_t *p = malloc(sizeof *p);
    simref_permit_cfg_t c = {.set = set, .stop_v_max = v_max, .stop_w_max = w_max, .track_m = track,
                             .stop_hold_us = hold_us, .wheel_stale_us = wheel_stale_us, .arm_stale_us = arm_stale_us,
                             .approved_poses = poses};
    if (p)
        simref_permit_init(p, &c);
    return p;
}

void simref_ad_permit_boot(void *h) { simref_permit_boot(h); }
void simref_ad_permit_begin(void *h) { simref_permit_begin_step(h); }
void simref_ad_permit_session(void *h, int src, unsigned session) { simref_permit_source_session(h, (simref_src_t)src, session); }
void simref_ad_permit_wheel(void *h, long long t, int side, unsigned session, long long seq, int valid, double v)
{
    simref_permit_wheel(h, t, (simref_src_t)side, session, seq, valid != 0, v);
}
void simref_ad_permit_arms(void *h, long long t, unsigned session, long long seq, int valid, int lp, int lm, int lf,
                           int rp, int rm, int rf, int bim)
{
    simref_arm_t l = {(simref_pose_t)lp, lm != 0, lf != 0}, r = {(simref_pose_t)rp, rm != 0, rf != 0};
    simref_permit_arms(h, t, session, seq, valid != 0, &l, &r, (simref_pose_t)bim);
}
void simref_ad_permit_flags(void *h, int base_fault, int set_fault, int link_ok, int set_link)
{
    if (set_fault)
        simref_permit_set_base_fault(h, base_fault != 0);
    if (set_link)
        simref_permit_set_ctl017_link(h, link_ok != 0);
}
void simref_ad_permit_evaluate(void *h, long long t) { simref_permit_evaluate(h, t); }

int simref_ad_permit_snapshot(void *h, char *buf, int cap)
{
    const simref_permit_t *p = h;
    static const char *const SRC[] = {"wheel:left", "wheel:right", "arms:arms"};
    int o = 0;
    P("{\"permit\":%s,\"reason\":\"%s\",\"stop_request\":%s,\"rejections\":[", B(p->permit),
      simref_permit_reason_name(p->reason), B(p->stop_request));
    for (size_t i = 0; i < p->n_rej; i++)
        P("%s\"%s:%s\"", i ? "," : "", SRC[p->rej[i].src], simref_reject_name(p->rej[i].why));
    P("]}");
    return o;
}

/* ---- whole controller, for host co-simulation with the Python emulators ---------------- */
#include "simref/simref_ctrl.h"

typedef void (*simref_ad_send_cb)(int bus, unsigned id, int len, const unsigned char *data);
typedef struct {
    simref_ctrl_t c;
    simref_ad_send_cb cb;
} ad_ctrl_t;

EXPORT void *simref_ad_ctrl_new(int cause, long long now_us, simref_ad_send_cb cb);
EXPORT void simref_ad_ctrl_frame(void *h, int bus, unsigned id, int len, const unsigned char *data, long long now_us);
EXPORT void simref_ad_ctrl_step(void *h, long long now_us);
EXPORT void simref_ad_ctrl_pin(void *h, int pin, int level, long long now_us);
EXPORT void simref_ad_ctrl_session(void *h, unsigned epoch, long long now_us);
EXPORT void simref_ad_ctrl_hb(void *h, unsigned epoch, long long now_us);
EXPORT void simref_ad_ctrl_cmd(void *h, unsigned epoch, unsigned seq, double v, double w, long long now_us);
EXPORT void simref_ad_ctrl_sup(void *h, int kind, unsigned epoch, unsigned seq, long long now_us);
EXPORT void simref_ad_ctrl_lift(void *h, int op, int stop, long long now_us);
EXPORT void simref_ad_ctrl_arms(void *h, int pose, long long now_us);
EXPORT int simref_ad_ctrl_snapshot(void *h, char *buf, int cap);

static void ad_send(void *ctx, simref_bus_t bus, const simref_frame_t *f)
{
    ad_ctrl_t *a = ctx;
    if (a->cb)
        a->cb((int)bus, f->id, f->len, f->data);
}

void *simref_ad_ctrl_new(int cause, long long now_us, simref_ad_send_cb cb)
{
    ad_ctrl_t *a = calloc(1, sizeof *a);
    if (!a)
        return NULL;
    a->cb = cb;
    simref_ctrl_cfg_t cfg;
    simref_ctrl_default_cfg(&cfg);
    cfg.boot_cause = (simref_cause_t)cause;
    simref_ctrl_init(&a->c, &cfg, now_us, ad_send, a);
    return a;
}

void simref_ad_ctrl_frame(void *h, int bus, unsigned id, int len, const unsigned char *data, long long now_us)
{
    simref_frame_t f = {.id = id, .len = (uint8_t)(len > 8 ? 8 : len)};
    memcpy(f.data, data, f.len);
    simref_ctrl_on_frame(&((ad_ctrl_t *)h)->c, (simref_bus_t)bus, &f, now_us);
}
void simref_ad_ctrl_step(void *h, long long now_us) { simref_ctrl_step(&((ad_ctrl_t *)h)->c, now_us); }
void simref_ad_ctrl_pin(void *h, int pin, int level, long long now_us)
{
    simref_ctrl_set_pin(&((ad_ctrl_t *)h)->c, (simref_pin_t)pin, level != 0, now_us);
}
void simref_ad_ctrl_session(void *h, unsigned epoch, long long now_us) { simref_ctrl_host_session(&((ad_ctrl_t *)h)->c, epoch, now_us); }
void simref_ad_ctrl_hb(void *h, unsigned epoch, long long now_us) { simref_ctrl_host_heartbeat(&((ad_ctrl_t *)h)->c, epoch, now_us); }
void simref_ad_ctrl_cmd(void *h, unsigned epoch, unsigned seq, double v, double w, long long now_us)
{
    simref_ctrl_host_cmd(&((ad_ctrl_t *)h)->c, epoch, seq, v, w, now_us);
}
void simref_ad_ctrl_sup(void *h, int kind, unsigned epoch, unsigned seq, long long now_us)
{
    simref_ctrl_host_supervisory(&((ad_ctrl_t *)h)->c, (simref_sup_t)kind, epoch, seq, now_us);
}
void simref_ad_ctrl_lift(void *h, int op, int stop, long long now_us)
{
    simref_ctrl_host_lift(&((ad_ctrl_t *)h)->c, (uint8_t)op, (uint8_t)stop, now_us);
}
void simref_ad_ctrl_arms(void *h, int pose, long long now_us)
{
    simref_arm_t a = {(simref_pose_t)pose, false, false};
    simref_ctrl_host_arms(&((ad_ctrl_t *)h)->c, &a, &a, (simref_pose_t)pose, now_us);
}

int simref_ad_ctrl_snapshot(void *h, char *buf, int cap)
{
    const simref_ctrl_t *c = &((ad_ctrl_t *)h)->c;
    int o = 0;
    P("{\"state\":\"%s\",\"motor\":\"%s\",\"phase\":\"%s\",\"detail\":\"%s\",\"sent\":[%d,%d],\"act\":[%d,%d],"
      "\"permit\":%s,\"permit_reason\":\"%s\",\"first_reason\":\"%s\",\"log_n\":%u,\"last\":\"%s\",\"enable_sequences\":%u,"
      "\"lift_nmt\":%u,\"x\":%.4f,\"spec_errors\":%u}",
      simref_state_name(c->sm.state), c->sm.motor_follow ? "FOLLOW_CMD" : "ZERO", simref_dl_phase_name(c->drive.phase),
      simref_dl_detail_name(c->drive.detail), c->drive.sent_rpm[0], c->drive.sent_rpm[1], c->drive.act_rpm10[0],
      c->drive.act_rpm10[1], B(c->permit.permit), simref_permit_reason_name(c->permit.reason),
      simref_reason_name(c->sm.first_reason), c->log_n,
      c->log_n ? simref_reason_name(c->log[(c->log_n - 1) % SIMREF_CTRL_LOG].reason) : "NONE",
      c->drive.enable_sequences, c->lift_nmt, c->x, c->spec_errors);
    return o;
}
