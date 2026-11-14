/* SPDX-License-Identifier: MIT */
/*
 * Read-only telemetry decode for the ZLAC8015D on CAN1.
 *
 * Input: samples produced by the read-only upload scheduler
 * (oamr/co_upload_sched.h). Output: typed values, each with its own
 * validity. Nothing here sends a frame or changes the drive.
 *
 * Objects decoded (vendor CANopen routine V1.07 labelled V4.0; the V4.2 EDS
 * and manual are not in hand, see docs/architecture/can1-drive-link.md):
 *   606Ch:01, 606Ch:02  actual velocity, I32, two channels. Decoded as
 *                       channel 1 and channel 2 by subindex, each with its own
 *                       validity. Which wheel is which, and the sign, come
 *                       from the wheels-up bench (TODO(Evans)).
 *   603Fh:00            fault code, 32 bit. Split into two raw 16-bit halves
 *                       named a (bits 31..16) and b (bits 15..0). The vendor
 *                       documents contradict each other on which half is
 *                       which wheel, so no left or right meaning is given.
 *   6041h:00            statusword, U16, CiA 402 bit decode.
 *   2026h:05            speed resolution, raw. Decision D-24: speed maths
 *                       only when it reads the value the owner requires.
 *
 * Validity rules: a value is valid only if the sample is VALID, carries the
 * expected size, and is not older than the caller's max age at now_us. The
 * max age has no default (0 = not configured: everything reports
 * NOT_CONFIGURED). A sample whose seq has not advanced since the previous
 * decode is reported with fresh_seq = false so a caller can tell a repeat
 * from a new reading.
 */
#ifndef OAMR_ZLAC_TELEMETRY_H
#define OAMR_ZLAC_TELEMETRY_H

#include <stdbool.h>
#include <stdint.h>

#include "oamr/co_upload_sched.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OAMR_ZLAC_OD_ACTUAL_VELOCITY 0x606Cu
#define OAMR_ZLAC_OD_FAULT_CODE 0x603Fu
#define OAMR_ZLAC_OD_STATUSWORD 0x6041u
#define OAMR_ZLAC_OD_SPEED_RESOLUTION 0x2026u
#define OAMR_ZLAC_OD_SPEED_RESOLUTION_SUB 0x05u

typedef enum {
    OAMR_TLM_NOT_CONFIGURED = 0,
    OAMR_TLM_VALID,
    OAMR_TLM_NOT_READ,     /* no sample yet */
    OAMR_TLM_READ_FAILED,  /* last transfer timed out, aborted or was malformed */
    OAMR_TLM_BAD_SIZE,     /* server stated a size other than the object's, or no size for a short type */
    OAMR_TLM_STALE,        /* older than max_age_us, or stamped in the future */
    OAMR_TLM_UNCONFIRMED   /* velocity: speed resolution not confirmed, no unit conversion */
} oamr_tlm_validity_t;

typedef struct {
    uint64_t max_age_us;   /* no default; TODO(Evans): tie to the scheduler cycle time */
} oamr_tlm_config_t;

typedef struct {
    oamr_tlm_validity_t validity;
    uint32_t seq;          /* scheduler seq of the sample used */
    bool fresh_seq;        /* seq differs from the one passed in as last_seq */
} oamr_tlm_meta_t;

/* ---- 606Ch: actual velocity, one channel per subindex ---- */
typedef struct {
    oamr_tlm_meta_t meta;
    int32_t raw;           /* drive units; 0 unless meta.validity is VALID or UNCONFIRMED */
} oamr_tlm_velocity_t;

/* ---- 603Fh: fault code halves ---- */
typedef struct {
    oamr_tlm_meta_t meta;
    uint32_t raw;
    uint16_t half_a;       /* bits 31..16, no wheel meaning */
    uint16_t half_b;       /* bits 15..0, no wheel meaning */
    bool any_fault;        /* raw != 0 */
} oamr_tlm_fault_t;

/* ---- 6041h: statusword (CiA 402 table) ---- */
#define OAMR_SW_READY_TO_SWITCH_ON 0x0001u
#define OAMR_SW_SWITCHED_ON 0x0002u
#define OAMR_SW_OPERATION_ENABLED 0x0004u
#define OAMR_SW_FAULT 0x0008u
#define OAMR_SW_VOLTAGE_ENABLED 0x0010u
#define OAMR_SW_QUICK_STOP 0x0020u        /* 0 = quick stop active */
#define OAMR_SW_SWITCH_ON_DISABLED 0x0040u
#define OAMR_SW_WARNING 0x0080u
#define OAMR_SW_REMOTE 0x0200u
#define OAMR_SW_TARGET_REACHED 0x0400u
#define OAMR_SW_INTERNAL_LIMIT 0x0800u

typedef enum {
    OAMR_402_UNKNOWN = 0,              /* bit pattern not in the CiA 402 table */
    OAMR_402_NOT_READY_TO_SWITCH_ON,
    OAMR_402_SWITCH_ON_DISABLED,
    OAMR_402_READY_TO_SWITCH_ON,
    OAMR_402_SWITCHED_ON,
    OAMR_402_OPERATION_ENABLED,
    OAMR_402_QUICK_STOP_ACTIVE,
    OAMR_402_FAULT_REACTION_ACTIVE,
    OAMR_402_FAULT
} oamr_402_state_t;

typedef struct {
    oamr_tlm_meta_t meta;
    uint16_t raw;
    oamr_402_state_t state;
    bool fault;
    bool warning;
    bool voltage_enabled;
    bool quick_stop_active;   /* bit 5 clear */
    bool remote;
    bool target_reached;
    bool internal_limit;
} oamr_tlm_statusword_t;

/* Pure CiA 402 state decode from a statusword (mask 006Fh / 004Fh rules). */
oamr_402_state_t oamr_402_state_from_statusword(uint16_t sw);
const char *oamr_402_state_name(oamr_402_state_t s);

/* ---- 2026h:05: speed resolution ---- */
typedef struct {
    oamr_tlm_meta_t meta;
    uint32_t raw;
    bool confirmed;        /* VALID and raw == required value passed by the caller */
} oamr_tlm_resolution_t;

/* Decoders. sample may be NULL (treated as NOT_READ). last_seq is the seq the
 * caller saw on its previous decode of the same entry (0 for none). */
void oamr_tlm_decode_velocity(const oamr_tlm_config_t *cfg, const oamr_co_sample_t *sample,
                              const oamr_tlm_resolution_t *resolution, uint32_t last_seq,
                              uint64_t now_us, oamr_tlm_velocity_t *out);
void oamr_tlm_decode_fault(const oamr_tlm_config_t *cfg, const oamr_co_sample_t *sample,
                           uint32_t last_seq, uint64_t now_us, oamr_tlm_fault_t *out);
void oamr_tlm_decode_statusword(const oamr_tlm_config_t *cfg, const oamr_co_sample_t *sample,
                                uint32_t last_seq, uint64_t now_us, oamr_tlm_statusword_t *out);
/* required_value has no default: D-24 asks for 1; the caller passes it.
 * required_set false = NOT_CONFIGURED. */
void oamr_tlm_decode_resolution(const oamr_tlm_config_t *cfg, const oamr_co_sample_t *sample,
                                uint32_t required_value, bool required_set, uint32_t last_seq,
                                uint64_t now_us, oamr_tlm_resolution_t *out);

/* Both velocity channels decoded together for convenience; each keeps its
 * own validity, so one channel failing never hides or invalidates the other. */
typedef struct {
    oamr_tlm_velocity_t ch1;   /* 606Ch:01 */
    oamr_tlm_velocity_t ch2;   /* 606Ch:02 */
    bool both_valid;           /* both VALID and both from a fresh seq */
} oamr_tlm_velocity_pair_t;

void oamr_tlm_decode_velocity_pair(const oamr_tlm_config_t *cfg, const oamr_co_sample_t *s1,
                                   const oamr_co_sample_t *s2, const oamr_tlm_resolution_t *resolution,
                                   uint32_t last_seq1, uint32_t last_seq2, uint64_t now_us,
                                   oamr_tlm_velocity_pair_t *out);

const char *oamr_tlm_validity_name(oamr_tlm_validity_t v);

#ifdef __cplusplus
}
#endif

#endif /* OAMR_ZLAC_TELEMETRY_H */
