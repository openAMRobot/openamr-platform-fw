/* SPDX-License-Identifier: MIT */
/* One step of the read-only chain at the current simulated time. */
#include "chain.h"

static void wheel_samples(oamr_sil_chain_t *c)
{
    const uint64_t now = c->now_us;
    for (unsigned w = 0; w < 2u; w++) {
        const oamr_co_sample_t *s = oamr_co_upl_sample(&c->upl, (uint8_t)(OBJ_VEL1 + w));
        if (s == NULL || s->status != OAMR_CO_SAMPLE_VALID || s->seq == c->wheel_mark_seq[w]) {
            continue;
        }
        c->wheel_mark_seq[w] = s->seq;
        oamr_fresh_mark(&c->wheel_fresh[w], (uint32_t)s->sample_us);
        if (c->last_wheel_us[w] != 0u && s->sample_us > c->last_wheel_us[w]) {
            const double p = (double)(s->sample_us - c->last_wheel_us[w]);
            c->sum_wheel_period_us[w] += p;
            c->n_wheel_period[w]++;
            if (p > c->max_wheel_period_us[w]) {
                c->max_wheel_period_us[w] = p;
            }
        }
        c->last_wheel_us[w] = s->sample_us;
    }
    (void)now;
}

/* Decode both channels and, only when both are VALID and both new, offer the
 * pair to the odometry. Anything else is counted as skipped: the odometry
 * never sees an invalid or repeated sample. */
static void odometry(oamr_sil_chain_t *c)
{
    const uint64_t now = c->now_us;
    oamr_tlm_decode_resolution(&c->tlm_cfg, oamr_co_upl_sample(&c->upl, OBJ_RESOLUTION),
                               (uint32_t)c->val[K_RESOLUTION_REQUIRED], true, 0u, now, &c->resolution);
    oamr_tlm_decode_statusword(&c->tlm_cfg, oamr_co_upl_sample(&c->upl, OBJ_STATUSWORD), 0u, now, &c->sw);
    oamr_tlm_decode_fault(&c->tlm_cfg, oamr_co_upl_sample(&c->upl, OBJ_FAULT), 0u, now, &c->fault);
    oamr_tlm_decode_velocity_pair(&c->tlm_cfg, oamr_co_upl_sample(&c->upl, OBJ_VEL1),
                                  oamr_co_upl_sample(&c->upl, OBJ_VEL2), &c->resolution, c->used_seq1,
                                  c->used_seq2, now, &c->vel);
    if (!c->vel.both_valid) {
        /* Count only when at least one channel has something new to offer. */
        if (c->vel.ch1.meta.fresh_seq || c->vel.ch2.meta.fresh_seq) {
            c->odom_skipped_invalid++;
        }
        return;
    }
    const oamr_co_sample_t *s1 = oamr_co_upl_sample(&c->upl, OBJ_VEL1);
    const oamr_co_sample_t *s2 = oamr_co_upl_sample(&c->upl, OBJ_VEL2);
    const uint64_t older = s1->sample_us < s2->sample_us ? s1->sample_us : s2->sample_us;
    const uint64_t newer = s1->sample_us < s2->sample_us ? s2->sample_us : s1->sample_us;
    const double skew = (double)(newer - older);
    if (skew > c->max_pair_skew_us) {
        c->max_pair_skew_us = skew;
    }
    c->used_seq1 = c->vel.ch1.meta.seq;
    c->used_seq2 = c->vel.ch2.meta.seq;
    const bool left_is_1 = (uint32_t)c->val[K_LEFT_CHANNEL] == 1u;
    const float left = (float)(left_is_1 ? c->vel.ch1.raw : c->vel.ch2.raw);
    const float right = (float)(left_is_1 ? c->vel.ch2.raw : c->vel.ch1.raw);
    if (!c->have_prev_pair) {
        /* First pair after start or a drive restart: no dt yet, reference only. */
        c->have_prev_pair = true;
        c->prev_pair_us = older;
        return;
    }
    if (older <= c->prev_pair_us) {
        c->odom_rejects++;
        c->odom_reject_reason[OAMR_ODOM_OUT_OF_ORDER]++;
        return;
    }
    const float dt = (float)((double)(older - c->prev_pair_us) * 1e-6);
    oamr_twist2d_t tw;
    /* The pair is stamped with its older sample: its age bounds both. */
    const oamr_odom_result_t r =
        oamr_odom_update_speeds(&c->odom, left, right, (uint32_t)older, (uint32_t)now, dt, &tw);
    /* Event line for the invariant check of the harness: every offered pair
     * with both validities and both sample ages at the moment of the call. */
    fprintf(c->out,
            "{\"type\":\"odom\",\"result\":%d,\"v1\":\"%s\",\"v2\":\"%s\",\"age1_us\":%.0f,"
            "\"age2_us\":%.0f,\"fresh1\":%s,\"fresh2\":%s}\n",
            (int)r, oamr_tlm_validity_name(c->vel.ch1.meta.validity),
            oamr_tlm_validity_name(c->vel.ch2.meta.validity), (double)(now - s1->sample_us),
            (double)(now - s2->sample_us), c->vel.ch1.meta.fresh_seq ? "true" : "false",
            c->vel.ch2.meta.fresh_seq ? "true" : "false");
    if (r == OAMR_ODOM_OK) {
        c->odom_steps++;
        c->twist = tw;
        c->prev_pair_us = older;
    } else {
        c->odom_rejects++;
        if ((unsigned)r < 8u) {
            c->odom_reject_reason[r]++;
        }
    }
}

static void sensors(oamr_sil_chain_t *c)
{
    const uint32_t now32 = (uint32_t)c->now_us;
    const oamr_mbseq_decision_t d = oamr_mbseq_step(&c->mbseq, now32);
    if (d.action == OAMR_MBSEQ_TRIGGER) {
        /* Pin pulse is the port's job; here the trigger is reported so the
         * co-simulator can answer with the sensor's text. */
        oamr_mb_arm(&c->mb[d.sensor]);
        oamr_mbseq_triggered(&c->mbseq, d.sensor, now32);
        c->mb_triggers[d.sensor]++;
        fprintf(c->out, "{\"type\":\"mb_trigger\",\"unit\":%u}\n", (unsigned)d.sensor);
    } else if (d.action == OAMR_MBSEQ_TIMED_OUT) {
        oamr_mb_disarm(&c->mb[d.sensor]);
    }
    if (c->icm_init == OAMR_ICM_OK) {
        if (oamr_icm_poll(&c->icm, now32)) {
            oamr_icm_sample_t s;
            if (oamr_icm_take_sample(&c->icm, &s)) {
                oamr_fresh_mark(&c->imu_fresh, s.stamp_us);
            }
        }
    }
}

void oamr_sil_chain_step(oamr_sil_chain_t *c)
{
    if (!c->configured) {
        return;
    }
    if (c->now_us >= c->next_tick_us || c->now_us + (uint64_t)c->val[K_SCHED_TICK_US] < c->next_tick_us) {
        oamr_co_upl_tick(&c->upl, c->now_us);
        c->next_tick_us = c->now_us + (uint64_t)c->val[K_SCHED_TICK_US];
    }
    if (oamr_co_hb_classify(&c->hb, c->now_us) == OAMR_CO_HB_ALIVE) {
        c->drive_seen_alive = true;
    }
    wheel_samples(c);
    odometry(c);
    sensors(c);
    /* A clock that went backwards re-arms the timers instead of freezing them. */
    const uint64_t sp = (uint64_t)c->val[K_STATUS_PERIOD_US];
    if (c->next_status_us > c->now_us + sp) {
        c->next_status_us = c->now_us + sp;
    }
    if (c->now_us >= c->next_status_us) {
        oamr_sil_emit_record(c);
        c->next_status_us += sp;
        if (c->next_status_us <= c->now_us) {
            c->next_status_us = c->now_us + sp;
        }
    }
    const uint64_t ap = (uint64_t)c->val[K_AUX_PERIOD_US];
    if (c->next_aux_us > c->now_us + ap) {
        c->next_aux_us = c->now_us + ap;
    }
    if (c->now_us >= c->next_aux_us) {
        oamr_sil_emit_aux(c);
        c->next_aux_us += ap;
        if (c->next_aux_us <= c->now_us) {
            c->next_aux_us = c->now_us + ap;
        }
    }
}
