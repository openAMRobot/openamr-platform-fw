/* SPDX-License-Identifier: MIT */
/* Wiring of the read-only chain (tests only). See chain.h. */
#include "chain.h"

#include <inttypes.h>
#include <math.h>
#include <string.h>

/* The only transmit path of the whole program. It is handed to
 * co_upload_sched, which only ever builds initiate-upload requests. The
 * check below makes that a hard property of the harness: anything else is
 * refused and reported. */
static bool sil_send(void *ctx, const oamr_co_frame_t *f)
{
    oamr_sil_chain_t *c = (oamr_sil_chain_t *)ctx;
    if (f->dlc != 8u || f->data[0] != OAMR_CO_SDO_CCS_UPLOAD_INIT) {
        fprintf(c->out, "{\"type\":\"error\",\"what\":\"non-upload frame refused\"}\n");
        return false;
    }
    c->tx_frames++;
    fprintf(c->out, "{\"type\":\"tx\",\"id\":%" PRIu32 ",\"data\":\"", f->id);
    for (unsigned i = 0; i < f->dlc; i++) {
        fprintf(c->out, "%02X", f->data[i]);
    }
    fprintf(c->out, "\"}\n");
    return true;
}

static uint32_t u32(const oamr_sil_chain_t *c, oamr_sil_key_t k)
{
    return (uint32_t)c->val[k];
}

void oamr_sil_chain_reset(oamr_sil_chain_t *c, FILE *out)
{
    memset(c, 0, sizeof(*c));
    c->out = out;
    c->imu_who = OAMR_ICM_WHO_AM_I_VALUE;
}

bool oamr_sil_chain_set(oamr_sil_chain_t *c, const char *key, double value)
{
    oamr_sil_key_t k;
    if (!oamr_sil_key_from_name(key, &k) || !isfinite(value)) {
        return false;
    }
    c->val[k] = value;
    c->set[k] = true;
    return true;
}

static bool init_modules(oamr_sil_chain_t *c, uint64_t now_us)
{
    bool ok = true;
    const uint32_t now32 = (uint32_t)now_us;

    c->objects[OBJ_VEL1] = (oamr_co_object_t){OAMR_ZLAC_OD_ACTUAL_VELOCITY, 1u};
    c->objects[OBJ_VEL2] = (oamr_co_object_t){OAMR_ZLAC_OD_ACTUAL_VELOCITY, 2u};
    c->objects[OBJ_STATUSWORD] = (oamr_co_object_t){OAMR_ZLAC_OD_STATUSWORD, 0u};
    c->objects[OBJ_FAULT] = (oamr_co_object_t){OAMR_ZLAC_OD_FAULT_CODE, 0u};
    c->objects[OBJ_RESOLUTION] = (oamr_co_object_t){OAMR_ZLAC_OD_SPEED_RESOLUTION,
                                                    OAMR_ZLAC_OD_SPEED_RESOLUTION_SUB};
    const oamr_co_upl_config_t ucfg = {
        .node_id = (uint8_t)u32(c, K_NODE_ID),
        .objects = c->objects,
        .n_objects = OAMR_SIL_N_OBJ,
        .timeout_us = (uint64_t)c->val[K_SDO_TIMEOUT_US],
        .max_retries = (uint8_t)u32(c, K_SDO_RETRIES),
        .max_retries_set = true,
        .send = sil_send,
        .send_ctx = c,
    };
    ok = (oamr_co_upl_init(&c->upl, &ucfg) != OAMR_CO_UPL_NOT_CONFIGURED) && ok;
    const oamr_co_hb_config_t hcfg = {(uint8_t)u32(c, K_NODE_ID), (uint64_t)c->val[K_HB_TIMEOUT_US]};
    ok = (oamr_co_hb_init(&c->hb, &hcfg, now_us) != OAMR_CO_HB_NOT_CONFIGURED) && ok;
    c->tlm_cfg.max_age_us = (uint64_t)c->val[K_TLM_MAX_AGE_US];

    const oamr_odom_config_t ocfg = {
        .wheel_radius_m = (float)c->val[K_WHEEL_RADIUS_M],
        .track_width_m = (float)c->val[K_TRACK_WIDTH_M],
        .counts_per_rev = (float)c->val[K_COUNTS_PER_REV],
        .speed_rad_s_per_unit = (float)c->val[K_SPEED_RAD_S_PER_UNIT],
        .left_sign = (int8_t)c->val[K_LEFT_SIGN],
        .right_sign = (int8_t)c->val[K_RIGHT_SIGN],
        .max_input_age_us = u32(c, K_ODOM_MAX_AGE_US),
        .max_count_delta = u32(c, K_MAX_COUNT_DELTA),
    };
    ok = oamr_odom_init(&c->odom, &ocfg) && ok;

    if (u32(c, K_LEFT_CHANNEL) != 1u && u32(c, K_LEFT_CHANNEL) != 2u) {
        ok = false;
    }
    const oamr_fresh_config_t wf = {u32(c, K_TLM_MAX_AGE_US)};
    for (unsigned i = 0; i < 2u; i++) {
        ok = oamr_fresh_init(&c->wheel_fresh[i], &wf) && ok;
    }
    const oamr_tfl_config_t tcfg = {(oamr_tfl_unit_t)u32(c, K_TFL_UNIT)};
    const oamr_fresh_config_t tf = {u32(c, K_TFL_STALE_US)};
    for (unsigned i = 0; i < OAMR_SIL_N_TFL; i++) {
        ok = oamr_tfl_init(&c->tfl[i], &tcfg) && ok;
        ok = oamr_fresh_init(&c->tfl_fresh[i], &tf) && ok;
    }
    const oamr_fresh_config_t mf = {u32(c, K_MB_STALE_US)};
    for (unsigned i = 0; i < OAMR_SIL_N_MB; i++) {
        oamr_mb_init(&c->mb[i]);
        ok = oamr_fresh_init(&c->mb_fresh[i], &mf) && ok;
    }
    const oamr_mbseq_config_t scfg = {OAMR_SIL_N_MB, u32(c, K_MB_TIMEOUT_US), u32(c, K_MB_GAP_US), true};
    ok = oamr_mbseq_init(&c->mbseq, &scfg) && ok;

    /* The IMU identity failure is a scenario outcome, not a configuration
     * error: the chain runs and reports it. */
    oamr_sil_icm_fake_init(&c->icm_fake, c->imu_who);
    const oamr_icm_spi_t spi = {oamr_sil_icm_fake_transfer, oamr_sil_icm_fake_delay, &c->icm_fake};
    const oamr_icm_config_t icfg = {(oamr_icm_gyro_fs_t)u32(c, K_IMU_GYRO_FS),
                                    (oamr_icm_accel_fs_t)u32(c, K_IMU_ACCEL_FS), (uint8_t)u32(c, K_IMU_ODR)};
    if (!oamr_icm_config_valid(&icfg)) {
        ok = false;
    }
    c->icm_init = oamr_icm_init(&c->icm, &spi, &icfg);
    const oamr_fresh_config_t imf = {u32(c, K_IMU_STALE_US)};
    ok = oamr_fresh_init(&c->imu_fresh, &imf) && ok;

    /* Daly: decoder configured, protocol_confirmed stays false (platform-fw
     * #48: protocol of the delivered unit not confirmed). */
    const oamr_daly_rx_config_t dcfg = {false, true, (uint8_t)u32(c, K_DALY_BMS_ADDR),
                                        (uint8_t)u32(c, K_DALY_HOST_ADDR), (uint8_t)u32(c, K_DALY_CELLS),
                                        (uint8_t)u32(c, K_DALY_TEMPS)};
    c->daly_ok = oamr_daly_rx_init(&c->daly, &dcfg);
    ok = c->daly_ok && ok;

    c->now_us = now_us;
    c->start_us = now_us;
    c->next_tick_us = now_us;
    c->next_status_us = now_us + (uint64_t)c->val[K_STATUS_PERIOD_US];
    c->next_aux_us = now_us + (uint64_t)c->val[K_AUX_PERIOD_US];
    c->sequence = 0u;
    c->aux_sequence = 0u;
    c->have_prev_pair = false;
    c->used_seq1 = c->used_seq2 = 0u;
    (void)now32;
    return ok;
}

bool oamr_sil_chain_start(oamr_sil_chain_t *c, uint64_t now_us)
{
    bool all = true;
    for (unsigned i = 0; i < (unsigned)K_COUNT; i++) {
        if (!c->set[i]) {
            fprintf(c->out, "{\"type\":\"error\",\"what\":\"unset key\",\"key\":\"%s\"}\n",
                    oamr_sil_key_name((oamr_sil_key_t)i));
            all = false;
        }
    }
    c->started = true;
    if (!all) {
        c->configured = false;
        return false;
    }
    c->boot_id = u32(c, K_BOOT_ID);
    c->configured = init_modules(c, now_us);
    if (!c->configured) {
        fprintf(c->out, "{\"type\":\"error\",\"what\":\"module rejected its configuration\"}\n");
    }
    return c->configured;
}

bool oamr_sil_chain_reboot(oamr_sil_chain_t *c, uint32_t new_boot_id, uint64_t now_us)
{
    /* Everything volatile goes; configuration and output stream stay. */
    oamr_sil_chain_t keep = *c;
    oamr_sil_chain_reset(c, keep.out);
    memcpy(c->val, keep.val, sizeof(c->val));
    memcpy(c->set, keep.set, sizeof(c->set));
    c->imu_who = keep.imu_who;
    c->started = true;
    c->boot_id = new_boot_id;
    c->configured = init_modules(c, now_us);
    return c->configured;
}

void oamr_sil_chain_set_time(oamr_sil_chain_t *c, uint64_t now_us)
{
    c->now_us = now_us;
}

void oamr_sil_chain_can1(oamr_sil_chain_t *c, const oamr_co_frame_t *f)
{
    if (!c->configured) {
        return;
    }
    const uint32_t boot_before = c->hb.bootups;
    (void)oamr_co_hb_on_frame(&c->hb, f, c->now_us);
    if (c->hb.bootups != boot_before) {
        c->drive_bootups_seen++;
        if (c->drive_seen_alive) {
            /* The drive restarted under us: its volatile state is gone and
             * the odometry reference with it. */
            c->drive_restarted = true;
            oamr_odom_reset_reference(&c->odom);
            c->have_prev_pair = false;
        }
    }
    (void)oamr_co_upl_on_frame(&c->upl, f, c->now_us);
}

void oamr_sil_chain_can2(oamr_sil_chain_t *c, const oamr_daly_rx_frame_t *f)
{
    if (!c->configured) {
        return;
    }
    (void)oamr_daly_rx_on_frame(&c->daly, f, (uint32_t)c->now_us);
}

void oamr_sil_chain_tfl_bytes(oamr_sil_chain_t *c, unsigned unit, const uint8_t *b, size_t n)
{
    if (!c->configured || unit >= OAMR_SIL_N_TFL) {
        return;
    }
    for (size_t i = 0; i < n; i++) {
        oamr_tfl_sample_t s;
        if (oamr_tfl_push(&c->tfl[unit], b[i], &s) == OAMR_TFL_PUSH_SAMPLE) {
            /* A checksum-correct frame proves the sensor is alive; whether
             * the distance is usable is a separate field. */
            oamr_fresh_mark(&c->tfl_fresh[unit], (uint32_t)c->now_us);
            c->tfl_last[unit] = s.status;
        }
    }
}

void oamr_sil_chain_mb_bytes(oamr_sil_chain_t *c, unsigned unit, const uint8_t *b, size_t n)
{
    if (!c->configured || unit >= OAMR_SIL_N_MB) {
        return;
    }
    for (size_t i = 0; i < n; i++) {
        oamr_mb_sample_t s;
        if (oamr_mb_push(&c->mb[unit], b[i], &s) == OAMR_MB_PUSH_SAMPLE) {
            if (oamr_mbseq_reading(&c->mbseq, (uint8_t)unit, (uint32_t)c->now_us)) {
                oamr_fresh_mark(&c->mb_fresh[unit], (uint32_t)c->now_us);
                c->mb_last[unit] = s.status;
            }
        }
    }
}

void oamr_sil_chain_imu_data(oamr_sil_chain_t *c, const uint8_t raw[14])
{
    oamr_sil_icm_fake_data(&c->icm_fake, raw);
}

void oamr_sil_chain_imu_bus(oamr_sil_chain_t *c, bool down)
{
    c->icm_fake.bus_down = down;
}
