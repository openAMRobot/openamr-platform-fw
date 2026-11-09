/* SPDX-License-Identifier: MIT */
/*
 * LOCAL STAND-IN for openamr_base_msgs/BaseStatus and BaseAuxStatus.
 *
 * The I8 package does not exist yet (interfaces #30/#31), so /base/status and
 * /base/aux_status carry diagnostic_msgs/DiagnosticStatus: one KeyValue per
 * I8 rev 3 field, values as text. Key names are the I8 group fields in
 * snake_case and are the contract the SIL tests check (docs/sil.md lists
 * them). TODO(Evans): switch to the real messages once merged; the snapshot
 * (oamr_app_status_t) already carries every field.
 *
 * Statically allocated; no heap use after init.
 */
#ifndef OAMR_UROS_STATUS_H
#define OAMR_UROS_STATUS_H

#include <stdint.h>

#include <diagnostic_msgs/msg/diagnostic_status.h>
#include <diagnostic_msgs/msg/key_value.h>

#include "oamr/base_app.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OAMR_UROS_STATUS_KEYS 48u
#define OAMR_UROS_AUX_KEYS 12u
#define OAMR_UROS_VALUE_LEN 64u
#define OAMR_UROS_FAULTS_LEN 512u

typedef struct {
    diagnostic_msgs__msg__DiagnosticStatus status;
    diagnostic_msgs__msg__KeyValue status_kv[OAMR_UROS_STATUS_KEYS];
    char status_val[OAMR_UROS_STATUS_KEYS][OAMR_UROS_VALUE_LEN];
    char faults_val[OAMR_UROS_FAULTS_LEN];
    diagnostic_msgs__msg__DiagnosticStatus aux;
    diagnostic_msgs__msg__KeyValue aux_kv[OAMR_UROS_AUX_KEYS];
    char aux_val[OAMR_UROS_AUX_KEYS][OAMR_UROS_VALUE_LEN];
    char name[24];
    char aux_name[24];
    char message[32];
    char hardware_id[48];
} oamr_uros_status_msgs_t;

/* Fields whose change triggers an immediate status publish (I8: on change
 * plus the 10 Hz heartbeat). */
typedef struct {
    uint8_t controller_state;
    uint8_t safety_state;
    uint8_t applied_state;
    uint8_t motion_allowed;
    uint8_t n_faults;
    uint8_t pad[3];
    uint32_t safety_reasons;
    uint16_t codes[OAMR_APP_MAX_FAULTS];
} oamr_uros_change_key_t;

void oamr_uros_status_msgs_init(oamr_uros_status_msgs_t *m, const char *hardware_id);
oamr_uros_change_key_t oamr_uros_change_key(const oamr_app_status_t *s);
void oamr_uros_fill_status(oamr_uros_status_msgs_t *m, const oamr_app_status_t *s, uint32_t seq);
void oamr_uros_fill_aux(oamr_uros_status_msgs_t *m, const oamr_app_status_t *s, uint32_t seq);

#ifdef __cplusplus
}
#endif

#endif /* OAMR_UROS_STATUS_H */
