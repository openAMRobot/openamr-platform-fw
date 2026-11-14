/* SPDX-License-Identifier: MIT */
/*
 * The status record: a plain struct rendered as one JSON line per period.
 * Field names follow BaseStatus of interfaces #38 through the mapping table
 * below; the table is printed by the "mapping" command so the Python side
 * and the document use the same list. Not a ROS message, not a micro-ROS
 * publisher.
 */
#include <inttypes.h>
#include <string.h>

#include "chain.h"

typedef struct {
    const char *record_field;
    const char *base_status_field; /* "" when #38 has no field */
    const char *note;
} oamr_sil_map_t;

static const oamr_sil_map_t MAP[] = {
    {"contract_version", "contract_version", "uint16, frozen"},
    {"boot_id", "boot_id", "uint32, frozen, 0 = UNKNOWN"},
    {"sequence", "sequence", "uint32, frozen, +1 per record"},
    {"monotonic_us", "monotonic_us", "uint64"},
    {"state", "state", "STATE_* name"},
    {"operating_state", "operating_state", "scenario input, not decided here"},
    {"recovery", "recovery", "RECOVERY_REQUIRED when a latched fault is held"},
    {"faults[].code", "faults[].code", "group 18001"},
    {"faults[].lifecycle", "faults[].lifecycle", "LIFECYCLE_*"},
    {"faults[].condition_active", "faults[].condition_active", "OBS_*"},
    {"faults[].inhibits_motion", "faults[].inhibits_motion", ""},
    {"subsystems[].id", "subsystems[].subsystem", "SUBSYSTEM_*"},
    {"subsystems[].state", "subsystems[].state", "STATE_* of BaseSubsystemStatus"},
    {"subsystems[].data_age_us", "subsystems[].data_age", "us here, Duration in #38"},
    {"subsystems[].fault_code", "subsystems[].fault_code", ""},
    {"subsystems[].detail", "", "no field in #38: free-text reason for the test only"},
    {"odom.*", "", "no odometry in BaseStatus; belongs on /odom or a wheel topic"},
    {"wheel[].*", "", "no per-wheel raw speed or validity in #38"},
    {"bms", "", "BMS values not in BaseStatus; only SUBSYSTEM_BMS state"},
};

void oamr_sil_print_mapping(FILE *out)
{
    fprintf(out, "{\"type\":\"mapping\",\"rows\":[");
    for (size_t i = 0; i < sizeof(MAP) / sizeof(MAP[0]); i++) {
        fprintf(out, "%s{\"record\":\"%s\",\"base_status\":\"%s\",\"note\":\"%s\"}", i ? "," : "",
                MAP[i].record_field, MAP[i].base_status_field, MAP[i].note);
    }
    fprintf(out, "]}\n");
}

static const char *state_name(uint8_t s)
{
    static const char *const n[] = {"UNKNOWN", "INITIALIZING", "READY", "DEGRADED", "FAULTED"};
    return s < 5u ? n[s] : "UNKNOWN";
}

static const char *op_name(uint32_t s)
{
    static const char *const n[] = {"UNKNOWN", "LOCKED", "RUN", "INHIBITED", "CHARGING", "CHARGER_FAULT"};
    return s < 6u ? n[s] : "UNKNOWN";
}

static const char *ss_name(uint8_t s)
{
    static const char *const n[] = {"UNKNOWN", "ABSENT", "STALE", "DEGRADED", "OK"};
    return s < 5u ? n[s] : "UNKNOWN";
}

static const char *lifecycle_name(uint8_t s)
{
    static const char *const n[] = {"UNKNOWN", "ACTIVE", "LATCHED", "ACKNOWLEDGED", "CLEARED"};
    return s < 5u ? n[s] : "UNKNOWN";
}

static void wheel_json(FILE *o, const char *name, const oamr_tlm_velocity_t *v)
{
    fprintf(o, "\"%s\":{\"validity\":\"%s\",\"seq\":%" PRIu32 ",\"raw\":%" PRId32 "}", name,
            oamr_tlm_validity_name(v->meta.validity), v->meta.seq, v->raw);
}

void oamr_sil_emit_record(oamr_sil_chain_t *c)
{
    oamr_sil_subsystem_t subs[SUB_COUNT];
    oamr_sil_model_subsystems(c, subs);
    oamr_sil_model_update_faults(c, subs);
    const uint8_t st = oamr_sil_model_controller_state(c, subs);
    bool latched = false;
    for (unsigned i = 0; i < c->n_faults; i++) {
        latched = latched || c->faults[i].lifecycle == 2u;
    }
    c->sequence += c->seq_skip;
    c->seq_skip = 0u;
    FILE *o = c->out;
    fprintf(o,
            "{\"type\":\"status\",\"contract_version\":%" PRIu32 ",\"boot_id\":%" PRIu32
            ",\"sequence\":%" PRIu32 ",\"monotonic_us\":%" PRIu64
            ",\"state\":\"%s\",\"operating_state\":\"%s\",\"recovery\":\"%s\",\"faults\":[",
            (uint32_t)c->val[K_CONTRACT_VERSION], c->boot_id, c->sequence, c->now_us, state_name(st),
            op_name((uint32_t)c->val[K_OPERATING_STATE]), latched ? "REQUIRED" : "NOT_REQUIRED");
    for (unsigned i = 0; i < c->n_faults; i++) {
        const oamr_sil_fault_t *f = &c->faults[i];
        fprintf(o,
                "%s{\"code\":%u,\"lifecycle\":\"%s\",\"condition_active\":\"%s\",\"inhibits_motion\":true}",
                i ? "," : "", (unsigned)f->code, lifecycle_name(f->lifecycle),
                f->condition_active == 2u ? "YES" : "NO");
    }
    fprintf(o, "],\"subsystems\":[");
    for (unsigned i = 0; i < SUB_COUNT; i++) {
        const oamr_sil_subsystem_t *s = &subs[i];
        fprintf(o, "%s{\"id\":%u,\"state\":\"%s\",\"data_age_us\":", i ? "," : "", (unsigned)s->id,
                ss_name(s->state));
        if (s->age_known) {
            fprintf(o, "%" PRIu32, s->data_age_us);
        } else {
            fprintf(o, "null");
        }
        fprintf(o, ",\"fault_code\":%u,\"detail\":\"%s\"}", (unsigned)s->fault_code, s->detail);
    }
    fprintf(o, "],");
    wheel_json(o, "wheel1", &c->vel.ch1);
    fprintf(o, ",");
    wheel_json(o, "wheel2", &c->vel.ch2);
    fprintf(o,
            ",\"odom\":{\"x_m\":%.6f,\"y_m\":%.6f,\"yaw_rad\":%.6f,\"v_m_s\":%.6f,\"w_rad_s\":%.6f,"
            "\"steps\":%" PRIu32 ",\"rejects\":%" PRIu32 ",\"skipped_invalid\":%" PRIu32
            ",\"reject_stale\":%" PRIu32 ",\"reject_order\":%" PRIu32 "}",
            (double)c->odom.pose.x_m, (double)c->odom.pose.y_m, (double)c->odom.pose.yaw_rad,
            (double)c->twist.v_m_s, (double)c->twist.w_rad_s, c->odom_steps, c->odom_rejects,
            c->odom_skipped_invalid, c->odom_reject_reason[OAMR_ODOM_STALE],
            c->odom_reject_reason[OAMR_ODOM_OUT_OF_ORDER]);
    fprintf(o,
            ",\"meas\":{\"tx_frames\":%" PRIu32 ",\"wheel_period_max_us\":[%.0f,%.0f],"
            "\"wheel_period_n\":[%" PRIu32 ",%" PRIu32 "],\"wheel_period_sum_us\":[%.0f,%.0f],"
            "\"pair_skew_max_us\":%.0f,\"sdo_unsolicited\":%" PRIu32 ",\"sdo_wrong_object\":%" PRIu32
            ",\"hb_lost_events\":%" PRIu32 ",\"drive_bootups\":%" PRIu32 ",\"imu_init\":%d,"
            "\"bms_usable\":%s,\"bms_decoded\":%" PRIu32 "}}\n",
            c->tx_frames, c->max_wheel_period_us[0], c->max_wheel_period_us[1], c->n_wheel_period[0],
            c->n_wheel_period[1], c->sum_wheel_period_us[0], c->sum_wheel_period_us[1],
            c->max_pair_skew_us, c->upl.unsolicited, c->upl.wrong_object, c->hb.lost_events,
            c->drive_bootups_seen, (int)c->icm_init, oamr_daly_rx_usable(&c->daly) ? "true" : "false",
            c->daly.counters.decoded);
    c->sequence++;
}

void oamr_sil_emit_aux(oamr_sil_chain_t *c)
{
    /* Identity and freshness only: no lift, light or permit field is
     * produced here (CTL-017 is out of scope of this proof). */
    fprintf(c->out,
            "{\"type\":\"aux\",\"contract_version\":%" PRIu32 ",\"boot_id\":%" PRIu32
            ",\"sequence\":%" PRIu32 ",\"monotonic_us\":%" PRIu64 "}\n",
            (uint32_t)c->val[K_CONTRACT_VERSION], c->boot_id, c->aux_sequence, c->now_us);
    c->aux_sequence++;
}
