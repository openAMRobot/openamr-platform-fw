#ifndef OAMR_CAN_FRAME_H
#define OAMR_CAN_FRAME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OAMR_CAN_MAX_DLC ((uint8_t)8u)
#define OAMR_CAN_STANDARD_ID_MAX UINT32_C(0x7ff)
#define OAMR_CAN_EXTENDED_ID_MAX UINT32_C(0x1fffffff)

typedef enum {
    OAMR_CAN_ID_STANDARD = 0,
    OAMR_CAN_ID_EXTENDED = 1
} oamr_can_id_type_t;

/* CAN/adapter errors may be combined as a bit mask. */
typedef enum {
    OAMR_CAN_ERROR_NONE = 0,
    OAMR_CAN_ERROR_BIT = 1 << 0,
    OAMR_CAN_ERROR_STUFF = 1 << 1,
    OAMR_CAN_ERROR_CRC = 1 << 2,
    OAMR_CAN_ERROR_FORM = 1 << 3,
    OAMR_CAN_ERROR_ACK = 1 << 4,
    OAMR_CAN_ERROR_OVERFLOW = 1 << 5,
    OAMR_CAN_ERROR_BUS_OFF = 1 << 6,
    OAMR_CAN_ERROR_OTHER = 1 << 7
} oamr_can_error_t;

/*
 * Classic CAN frame, passed by value with inline payload storage. Consequently
 * no borrowed payload lifetime or payload ownership is implied. Only data[0]
 * through data[dlc - 1] are meaningful; bytes beyond dlc are ignored.
 *
 * timestamp_us is acquisition metadata supplied by the producer. Zero is not
 * a missing-timestamp sentinel; consumers must carry timestamp validity
 * separately when checking freshness. Error flags describe reception/adapter
 * conditions and do not make structural validation fail by themselves.
 */
typedef struct {
    uint64_t timestamp_us;
    uint32_t id;
    oamr_can_id_type_t id_type;
    oamr_can_error_t error_flags;
    uint8_t dlc;
    uint8_t data[OAMR_CAN_MAX_DLC];
} oamr_can_frame_t;

/* Unknown ID types are invalid. Both standard and extended ID zero are valid. */
static inline bool oamr_can_id_is_valid(oamr_can_id_type_t id_type, uint32_t id)
{
    if (id_type == OAMR_CAN_ID_STANDARD) {
        return id <= OAMR_CAN_STANDARD_ID_MAX;
    }
    if (id_type == OAMR_CAN_ID_EXTENDED) {
        return id <= OAMR_CAN_EXTENDED_ID_MAX;
    }
    return false;
}

/*
 * Structural validation only: NULL is invalid, DLC 0..8 is accepted, and the
 * identifier must match its 11-bit or 29-bit type. Timestamp and error flags
 * are deliberately not interpreted.
 */
static inline bool oamr_can_frame_is_valid(const oamr_can_frame_t *frame)
{
    return frame != NULL && frame->dlc <= OAMR_CAN_MAX_DLC &&
           oamr_can_id_is_valid(frame->id_type, frame->id);
}

#ifdef __cplusplus
}
#endif

#endif /* OAMR_CAN_FRAME_H */
