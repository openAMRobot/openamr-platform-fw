/* SPDX-License-Identifier: MIT */
/* LOCAL STAND-IN status serialisation. See uros_status.h. */
#include "uros_status.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    diagnostic_msgs__msg__KeyValue *kv;
    char (*val)[OAMR_UROS_VALUE_LEN];
    size_t cap;
    size_t n;
} kv_writer_t;

static void str_set(rosidl_runtime_c__String *s, char *buf, size_t cap)
{
    s->data = buf;
    s->size = strlen(buf);
    s->capacity = cap;
}

static void put(kv_writer_t *w, const char *key, const char *fmt, ...)
{
    if (w->n >= w->cap) {
        return;
    }
    diagnostic_msgs__msg__KeyValue *kv = &w->kv[w->n];
    kv->key.data = (char *)key;  /* read-only for the serializer */
    kv->key.size = strlen(key);
    kv->key.capacity = kv->key.size + 1u;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(w->val[w->n], OAMR_UROS_VALUE_LEN, fmt, ap);
    va_end(ap);
    str_set(&kv->value, w->val[w->n], OAMR_UROS_VALUE_LEN);
    w->n++;
}

static const char *b(bool v)
{
    return v ? "true" : "false";
}

static const char *can_state_name(oamr_can_health_state_t st)
{
    switch (st) {
    case OAMR_CAN_OK: return "OK";
    case OAMR_CAN_BUS_OFF_WAIT: return "BUS_OFF";
    case OAMR_CAN_RESTARTING: return "RESTARTING";
    case OAMR_CAN_FAILED: return "FAILED";
    default: return "UNKNOWN";
    }
}

static const char *drive_state_name(oamr_zlac_state_t st)
{
    switch (st) {
    case OAMR_ZLAC_INIT: return "INIT";
    case OAMR_ZLAC_CONFIGURING: return "CONFIGURING";
    case OAMR_ZLAC_SAVING: return "SAVING";
    case OAMR_ZLAC_ZEROING: return "ZEROING";
    case OAMR_ZLAC_STARTING: return "STARTING";
    case OAMR_ZLAC_ENABLING: return "ENABLING";
    case OAMR_ZLAC_OPERATIONAL: return "OPERATIONAL";
    case OAMR_ZLAC_FAULT: return "FAULT";
    case OAMR_ZLAC_COMM_LOST: return "COMM_LOST";
    case OAMR_ZLAC_CONFIG_FAILED: return "CONFIG_FAILED";
    default: return "UNKNOWN";
    }
}

static const char *column_name(oamr_sc_column_estop_t c, bool fresh)
{
    if (!fresh) {
        return "UNKNOWN";
    }
    switch (c) {
    case OAMR_SC_COLUMN_RELEASED: return "RELEASED";
    case OAMR_SC_COLUMN_PRESSED: return "PRESSED";
    default: return "UNKNOWN";
    }
}

void oamr_uros_status_msgs_init(oamr_uros_status_msgs_t *m, const char *hardware_id)
{
    memset(m, 0, sizeof(*m));
    snprintf(m->name, sizeof(m->name), "base/status");
    snprintf(m->aux_name, sizeof(m->aux_name), "base/aux_status");
    snprintf(m->hardware_id, sizeof(m->hardware_id), "%s", hardware_id);
    str_set(&m->status.name, m->name, sizeof(m->name));
    str_set(&m->aux.name, m->aux_name, sizeof(m->aux_name));
    str_set(&m->status.hardware_id, m->hardware_id, sizeof(m->hardware_id));
    str_set(&m->aux.hardware_id, m->hardware_id, sizeof(m->hardware_id));
    str_set(&m->status.message, m->message, sizeof(m->message));
    str_set(&m->aux.message, m->message, sizeof(m->message));
    m->status.values.data = m->status_kv;
    m->status.values.capacity = OAMR_UROS_STATUS_KEYS;
    m->aux.values.data = m->aux_kv;
    m->aux.values.capacity = OAMR_UROS_AUX_KEYS;
}

oamr_uros_change_key_t oamr_uros_change_key(const oamr_app_status_t *s)
{
    oamr_uros_change_key_t k;
    memset(&k, 0, sizeof(k));
    k.controller_state = (uint8_t)s->controller_state;
    k.safety_state = (uint8_t)s->safety_state;
    k.applied_state = (uint8_t)s->applied.state;
    k.motion_allowed = s->motion_allowed ? 1u : 0u;
    k.n_faults = s->n_faults;
    k.safety_reasons = s->safety_reasons;
    for (uint8_t i = 0; i < s->n_faults; i++) {
        k.codes[i] = s->faults[i].code;
    }
    return k;
}

static void faults_text(char *out, size_t cap, const oamr_app_status_t *s)
{
    /* "code:NAME:latched:inhibits;..." in ascending code order (bounded list). */
    size_t n = 0;
    out[0] = '\0';
    for (uint8_t i = 0; i < s->n_faults && n < cap; i++) {
        const oamr_app_fault_t *f = &s->faults[i];
        const int w = snprintf(out + n, cap - n, "%s%u:%s:%d:%d", i ? ";" : "", f->code,
                               oamr_app_fault_name(f->code), f->latched ? 1 : 0,
                               f->inhibits_motion ? 1 : 0);
        if (w < 0) {
            break;
        }
        n += (size_t)w;
    }
}

void oamr_uros_fill_status(oamr_uros_status_msgs_t *m, const oamr_app_status_t *s, uint32_t seq)
{
    kv_writer_t w = {m->status_kv, m->status_val, OAMR_UROS_STATUS_KEYS, 0};
    /* Contract and identity */
    put(&w, "contract_version", "%u", (unsigned)s->contract_version);
    put(&w, "stand_in", "%s", "BaseStatus");
    put(&w, "boot_id", "%08" PRIx32, s->boot_id);
    put(&w, "sequence", "%" PRIu32, seq);
    put(&w, "monotonic_us", "%" PRIu64, s->now_us);
    put(&w, "powered_debug", "%s", b(s->powered_debug));
    put(&w, "reset_cause", "0x%03" PRIx32, (uint32_t)s->reset_cause);
    /* Controller state */
    put(&w, "controller_state", "%s", oamr_app_controller_state_name(s->controller_state));
    put(&w, "ready", "%s", b(s->controller_state == OAMR_CTRL_READY));
    put(&w, "command_accepted", "%s", b(s->command_accepted));
    put(&w, "recovery_required", "%s", b(s->recovery_required));
    /* Command and communication */
    put(&w, "host_link_ok", "%s", b(s->host_link_ok));
    put(&w, "applied_state", "%s", oamr_app_applied_state_name(s->applied.state));
    put(&w, "applied_left_m_s", "%.4f", (double)s->applied.left_m_s);
    put(&w, "applied_right_m_s", "%.4f", (double)s->applied.right_m_s);
    put(&w, "applied_clamped", "%s", b(s->applied.clamped));
    put(&w, "cmd_age_ms", "%" PRId64,
        s->applied.cmd_age_us == UINT64_MAX ? (int64_t)-1 : (int64_t)(s->applied.cmd_age_us / 1000u));
    /* Safety observation (E-01 rev 3) */
    put(&w, "safety_state", "%s", oamr_app_safety_state_name(s->safety_state));
    put(&w, "safety_reasons", "0x%03" PRIx32, s->safety_reasons);
    put(&w, "safety_first_reason", "0x%03" PRIx32, s->safety_first_reason);
    put(&w, "motion_allowed", "%s", b(s->motion_allowed));
    put(&w, "reset_required", "%s", b(s->reset_lamp));
    put(&w, "charge_request", "%s", b(s->charge_request));
    /* Faults */
    faults_text(m->faults_val, sizeof(m->faults_val), s);
    put(&w, "fault_count", "%u", (unsigned)s->n_faults);
    put(&w, "faults_overflow", "%s", b(s->faults_overflow));
    if (w.n < w.cap) {
        diagnostic_msgs__msg__KeyValue *kv = &w.kv[w.n++];
        kv->key.data = (char *)"faults";
        kv->key.size = 6u;
        kv->key.capacity = 7u;
        str_set(&kv->value, m->faults_val, sizeof(m->faults_val));
    }
    /* Sensors and actuators */
    put(&w, "drive_state", "%s", drive_state_name(s->drive_state));
    put(&w, "drive_ready", "%s", b(s->drive_ready));
    put(&w, "drive_fault_raw", "0x%08" PRIx32, s->drive_fault_raw);
    put(&w, "wheel_feedback_valid", "%s", b(s->wheels.valid));
    put(&w, "wheel_left_rad_s", "%.4f", (double)s->wheels.left_rad_s);
    put(&w, "wheel_right_rad_s", "%.4f", (double)s->wheels.right_rad_s);
    put(&w, "can1_state", "%s", can_state_name(s->can1));
    put(&w, "can2_state", "%s", can_state_name(s->can2));
    put(&w, "can1_bus_off_total", "%" PRIu32, s->can1_bus_off_total);
    put(&w, "can2_bus_off_total", "%" PRIu32, s->can2_bus_off_total);
    put(&w, "bms_fresh", "%s", b(s->bms_fresh));
    put(&w, "bms_fault", "%s", b(s->bms.have_data && s->bms.fault));
    put(&w, "bms_soc_percent", "%.1f", (double)s->bms.soc_percent);
    put(&w, "bms_voltage_v", "%.2f", (double)s->bms.pack_voltage_v);
    put(&w, "imu_present", "%s", b(s->imu_present));
    put(&w, "imu_healthy", "%s", b(s->imu_healthy));
    put(&w, "imu_invalid_samples", "%" PRIu32, s->imu_counters.invalid_samples);
    put(&w, "imu_read_failures", "%" PRIu32, s->imu_counters.read_failures);
    put(&w, "commissioning_missing", "0x%02" PRIx32, s->commissioning_missing);
    m->status.values.size = w.n;

    /* level: OK / WARN / ERROR mirrors READY / DEGRADED / FAULTED */
    switch (s->controller_state) {
    case OAMR_CTRL_READY: m->status.level = diagnostic_msgs__msg__DiagnosticStatus__OK; break;
    case OAMR_CTRL_FAULTED: m->status.level = diagnostic_msgs__msg__DiagnosticStatus__ERROR; break;
    case OAMR_CTRL_UNKNOWN: m->status.level = diagnostic_msgs__msg__DiagnosticStatus__STALE; break;
    default: m->status.level = diagnostic_msgs__msg__DiagnosticStatus__WARN; break;
    }
    snprintf(m->message, sizeof(m->message), "%s", oamr_app_controller_state_name(s->controller_state));
    str_set(&m->status.message, m->message, sizeof(m->message));
}

void oamr_uros_fill_aux(oamr_uros_status_msgs_t *m, const oamr_app_status_t *s, uint32_t seq)
{
    kv_writer_t w = {m->aux_kv, m->aux_val, OAMR_UROS_AUX_KEYS, 0};
    put(&w, "contract_version", "%u", (unsigned)s->contract_version);
    put(&w, "boot_id", "%08" PRIx32, s->boot_id);
    put(&w, "sequence", "%" PRIu32, seq);
    put(&w, "estop2_state", "%s", column_name(s->estop_column, s->estop_column_fresh));
    /* Lift, head light, Getac power-good: relayed from CTL-017 over CAN3
     * (platform-fw #12). Not wired in this build: reported UNKNOWN, never
     * inferred. TODO(Evans): fill from the CAN3 lift adapter. */
    put(&w, "lift_state", "%s", "UNKNOWN");
    put(&w, "head_light", "%s", "UNKNOWN");
    put(&w, "getac_power_good", "%s", "UNKNOWN");
    m->aux.values.size = w.n;
    m->aux.level = diagnostic_msgs__msg__DiagnosticStatus__WARN;
    str_set(&m->aux.message, m->message, sizeof(m->message));
}
