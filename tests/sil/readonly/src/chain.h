/* SPDX-License-Identifier: MIT */
/*
 * Read-only chain, host side of the SIL integration proof (tests only).
 *
 *   CAN1 frames -> co_upload_sched + co_hb_consumer (#49)
 *               -> zlac_telemetry decoders (#49)
 *               -> both-wheel speed pair with validity -> diff_odometry (#48)
 *   UART bytes  -> tfluna x2, mb7060 x4 + sequencer (#48)
 *   SPI regs    -> icm42688 (#48)
 *   CAN2 frames -> daly_rx (#48), protocol unconfirmed, always UNKNOWN
 *   everything  -> freshness (#48) -> status record every status period
 *
 * What this is not: no command path. The only frame this program can send is
 * an SDO initiate-upload request built by co_upload_sched. There is no
 * /cmd_vel input, no target, no enable, no NMT, no write.
 *
 * TEST ORACLE: the controller state, fault list and lifecycle in the record
 * are derived by a small reporting model in record_model.c, labelled as a
 * test oracle. The operating state (LOCKED/RUN/...) is an input of the
 * scenario standing in for the owner's state machine; nothing here decides
 * it. See docs/architecture/readonly-chain.md.
 */
#ifndef OAMR_SIL_CHAIN_H
#define OAMR_SIL_CHAIN_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "fake_icm_spi.h"
#include "oamr/co_hb_consumer.h"
#include "oamr/co_upload_sched.h"
#include "oamr/daly_rx.h"
#include "oamr/diff_odometry.h"
#include "oamr/freshness.h"
#include "oamr/icm42688.h"
#include "oamr/mb7060.h"
#include "oamr/tfluna.h"
#include "oamr/zlac_telemetry.h"

/* ---- configuration: every key must be set, there are no defaults ---- */
typedef enum {
    K_NODE_ID = 0,
    K_SDO_TIMEOUT_US,
    K_SDO_RETRIES,
    K_SCHED_TICK_US,
    K_HB_TIMEOUT_US,
    K_TLM_MAX_AGE_US,
    K_RESOLUTION_REQUIRED,
    K_WHEEL_RADIUS_M,
    K_TRACK_WIDTH_M,
    K_COUNTS_PER_REV,
    K_MAX_COUNT_DELTA,
    K_SPEED_RAD_S_PER_UNIT,
    K_LEFT_CHANNEL,
    K_LEFT_SIGN,
    K_RIGHT_SIGN,
    K_ODOM_MAX_AGE_US,
    K_STATUS_PERIOD_US,
    K_AUX_PERIOD_US,
    K_CONTRACT_VERSION,
    K_BOOT_ID,
    K_OPERATING_STATE,
    K_BMS_REQUIRED,
    K_TFL_UNIT,
    K_TFL_STALE_US,
    K_MB_STALE_US,
    K_MB_TIMEOUT_US,
    K_MB_GAP_US,
    K_IMU_GYRO_FS,
    K_IMU_ACCEL_FS,
    K_IMU_ODR,
    K_IMU_STALE_US,
    K_DALY_BMS_ADDR,
    K_DALY_HOST_ADDR,
    K_DALY_CELLS,
    K_DALY_TEMPS,
    K_COUNT
} oamr_sil_key_t;

const char *oamr_sil_key_name(oamr_sil_key_t k);
bool oamr_sil_key_from_name(const char *name, oamr_sil_key_t *out);

#define OAMR_SIL_N_TFL 2u
#define OAMR_SIL_N_MB 4u
#define OAMR_SIL_N_OBJ 5u
#define OAMR_SIL_MAX_FAULTS 8u

/* Index of each object in the upload list. */
enum { OBJ_VEL1 = 0, OBJ_VEL2, OBJ_STATUSWORD, OBJ_FAULT, OBJ_RESOLUTION };

/* I8 SUBSYSTEM_* ids (interfaces #38 BaseSubsystemStatus). */
enum {
    SUB_MOTOR_DRIVER = 1,
    SUB_ENCODERS = 2,
    SUB_IMU = 3,
    SUB_BMS = 4,
    SUB_RANGE_FL = 5,
    SUB_RANGE_FR = 6,
    SUB_RANGE_RL = 7,
    SUB_RANGE_RR = 8,
    SUB_CLIFF_FRONT = 9,
    SUB_CLIFF_REAR = 10,
    SUB_COUNT = 10
};

/* I8 BaseSubsystemStatus STATE_* values. */
enum { SS_UNKNOWN = 0, SS_ABSENT = 1, SS_STALE = 2, SS_DEGRADED = 3, SS_OK = 4 };

typedef struct {
    uint8_t id;
    uint8_t state;
    uint32_t data_age_us; /* 0 when ABSENT or UNKNOWN */
    bool age_known;
    uint16_t fault_code;
    const char *detail;   /* short machine word for the test, not operator text */
} oamr_sil_subsystem_t;

typedef struct {
    uint16_t code;
    uint8_t lifecycle;        /* I8 LIFECYCLE_* */
    uint8_t condition_active; /* I8 OBS_* */
    bool latched_now;
} oamr_sil_fault_t;

typedef struct {
    /* config */
    double val[K_COUNT];
    bool set[K_COUNT];
    bool started;
    bool configured;

    /* time: the chain keeps a 64-bit monotonic us; #48 modules get the low 32 bits */
    uint64_t now_us;
    uint64_t start_us;

    /* CAN1 read-only client */
    oamr_co_object_t objects[OAMR_SIL_N_OBJ];
    oamr_co_upl_t upl;
    oamr_co_hb_consumer_t hb;
    oamr_tlm_config_t tlm_cfg;
    oamr_tlm_resolution_t resolution;
    oamr_tlm_velocity_pair_t vel;
    oamr_tlm_statusword_t sw;
    oamr_tlm_fault_t fault;
    uint32_t used_seq1, used_seq2;
    uint32_t tx_frames;
    uint64_t next_tick_us;
    uint32_t drive_bootups_seen;

    /* odometry */
    oamr_odom_t odom;
    bool have_prev_pair;
    uint64_t prev_pair_us;
    uint32_t odom_steps;
    uint32_t odom_rejects;
    uint32_t odom_reject_reason[8]; /* by oamr_odom_result_t */
    uint32_t odom_skipped_invalid;  /* pairs not offered: a channel not VALID or not new */
    double max_pair_skew_us;        /* time between the two wheel samples of one pair */
    uint64_t last_wheel_us[2];
    uint32_t wheel_mark_seq[2];
    double max_wheel_period_us[2];
    double sum_wheel_period_us[2];
    uint32_t n_wheel_period[2];
    oamr_twist2d_t twist;

    /* sensors */
    oamr_tfl_t tfl[OAMR_SIL_N_TFL];
    oamr_fresh_t tfl_fresh[OAMR_SIL_N_TFL];
    oamr_tfl_status_t tfl_last[OAMR_SIL_N_TFL];
    oamr_mb_t mb[OAMR_SIL_N_MB];
    oamr_mbseq_t mbseq;
    oamr_fresh_t mb_fresh[OAMR_SIL_N_MB];
    oamr_mb_status_t mb_last[OAMR_SIL_N_MB];
    uint32_t mb_triggers[OAMR_SIL_N_MB];
    oamr_sil_icm_fake_t icm_fake;
    oamr_icm_t icm;
    oamr_icm_result_t icm_init;
    uint8_t imu_who; /* fake part identity, set by the scenario before start */
    oamr_fresh_t wheel_fresh[2];
    oamr_fresh_t imu_fresh;
    oamr_daly_rx_t daly;
    bool daly_ok;

    /* status record */
    uint32_t boot_id;
    uint32_t sequence;
    uint64_t next_status_us;
    uint64_t next_aux_us;
    uint32_t aux_sequence;
    bool drive_seen_alive;
    bool drive_restarted;
    uint32_t seq_skip; /* failure injection: next record skips this many numbers */
    oamr_sil_fault_t faults[OAMR_SIL_MAX_FAULTS];
    uint8_t n_faults;

    FILE *out;
} oamr_sil_chain_t;

void oamr_sil_chain_reset(oamr_sil_chain_t *c, FILE *out);
bool oamr_sil_chain_set(oamr_sil_chain_t *c, const char *key, double value);
/* Builds every module config from the keys. Returns false and prints the
 * missing keys when anything is unset: the chain is then NOT_CONFIGURED and
 * does nothing. */
bool oamr_sil_chain_start(oamr_sil_chain_t *c, uint64_t now_us);
/* Controller restart: every module re-initialised, sequence from zero, new
 * boot id. Configuration is kept. */
bool oamr_sil_chain_reboot(oamr_sil_chain_t *c, uint32_t new_boot_id, uint64_t now_us);

void oamr_sil_chain_set_time(oamr_sil_chain_t *c, uint64_t now_us);
void oamr_sil_chain_can1(oamr_sil_chain_t *c, const oamr_co_frame_t *f);
void oamr_sil_chain_can2(oamr_sil_chain_t *c, const oamr_daly_rx_frame_t *f);
void oamr_sil_chain_tfl_bytes(oamr_sil_chain_t *c, unsigned unit, const uint8_t *b, size_t n);
void oamr_sil_chain_mb_bytes(oamr_sil_chain_t *c, unsigned unit, const uint8_t *b, size_t n);
void oamr_sil_chain_imu_data(oamr_sil_chain_t *c, const uint8_t raw[14]);
void oamr_sil_chain_imu_bus(oamr_sil_chain_t *c, bool down);
/* One step at the current time: scheduler tick, sequencer, IMU poll,
 * odometry, and a status record when one is due. */
void oamr_sil_chain_step(oamr_sil_chain_t *c);

/* record_model.c (TEST ORACLE): subsystem states, faults, controller state. */
void oamr_sil_model_subsystems(oamr_sil_chain_t *c, oamr_sil_subsystem_t subs[SUB_COUNT]);
void oamr_sil_model_update_faults(oamr_sil_chain_t *c, const oamr_sil_subsystem_t subs[SUB_COUNT]);
uint8_t oamr_sil_model_controller_state(const oamr_sil_chain_t *c,
                                        const oamr_sil_subsystem_t subs[SUB_COUNT]);
/* record_json.c: the record and its mapping table to BaseStatus. */
void oamr_sil_emit_record(oamr_sil_chain_t *c);
void oamr_sil_emit_aux(oamr_sil_chain_t *c);

#endif
