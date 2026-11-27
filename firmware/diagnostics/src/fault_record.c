#include "oamr/fault_record.h"

static void oamr_fault_put_u16(uint8_t *out, uint16_t value)
{
    out[0] = (uint8_t)(value & UINT16_C(0xff));
    out[1] = (uint8_t)(value >> 8);
}

static void oamr_fault_put_u32(uint8_t *out, uint32_t value)
{
    out[0] = (uint8_t)(value & UINT32_C(0xff));
    out[1] = (uint8_t)((value >> 8) & UINT32_C(0xff));
    out[2] = (uint8_t)((value >> 16) & UINT32_C(0xff));
    out[3] = (uint8_t)(value >> 24);
}

static uint16_t oamr_fault_get_u16(const uint8_t *data)
{
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static uint32_t oamr_fault_get_u32(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static void oamr_fault_encode_fields(const oamr_fault_record_t *record,
                                     uint8_t out_data[OAMR_FAULT_RECORD_SERIALIZED_SIZE])
{
    oamr_fault_put_u32(out_data, record->magic);
    oamr_fault_put_u16(out_data + 4u, record->version);
    oamr_fault_put_u16(out_data + 6u, record->length);
    oamr_fault_put_u32(out_data + 8u, record->data.raw_reset_flags);
    oamr_fault_put_u32(out_data + 12u, record->data.stacked_pc);
    oamr_fault_put_u32(out_data + 16u, record->data.stacked_lr);
    oamr_fault_put_u32(out_data + 20u, record->data.stacked_xpsr);
    oamr_fault_put_u32(out_data + 24u, record->data.cfsr);
    oamr_fault_put_u32(out_data + 28u, record->data.hfsr);
    oamr_fault_put_u32(out_data + 32u, record->data.bfar);
    oamr_fault_put_u32(out_data + 36u, record->data.mmar);
    oamr_fault_put_u32(out_data + 40u, record->crc32);
}

uint32_t oamr_fault_crc32(const void *data, size_t data_size)
{
    const uint8_t *bytes = (const uint8_t *)data;
    uint32_t crc = UINT32_C(0xffffffff);
    size_t i;

    if (bytes == NULL) {
        return 0u;
    }

    for (i = 0u; i < data_size; ++i) {
        unsigned bit;
        crc ^= bytes[i];
        for (bit = 0u; bit < 8u; ++bit) {
            uint32_t mask = (uint32_t)(0u - (crc & UINT32_C(1)));
            crc = (crc >> 1) ^ (UINT32_C(0xedb88320) & mask);
        }
    }
    return crc ^ UINT32_C(0xffffffff);
}

oamr_status_t oamr_fault_record_init(
    oamr_fault_record_t *record,
    const oamr_fault_record_data_t *data,
    const oamr_reset_flag_definitions_t *reset_definitions)
{
    uint8_t encoded[OAMR_FAULT_RECORD_SERIALIZED_SIZE];

    if (record == NULL) {
        return OAMR_STATUS_INVALID;
    }
    if (data == NULL || reset_definitions == NULL) {
        record->magic = 0u;
        record->version = 0u;
        record->length = 0u;
        record->crc32 = 0u;
        return OAMR_STATUS_INVALID;
    }

    record->magic = OAMR_FAULT_RECORD_MAGIC;
    record->version = OAMR_FAULT_RECORD_VERSION;
    record->length = (uint16_t)OAMR_FAULT_RECORD_SERIALIZED_SIZE;
    record->data = *data;
    record->reset_definitions = *reset_definitions;
    record->crc32 = 0u;
    oamr_fault_encode_fields(record, encoded);
    record->crc32 = oamr_fault_crc32(
        encoded, OAMR_FAULT_RECORD_CRC_OFFSET);
    return OAMR_STATUS_OK;
}

oamr_status_t oamr_fault_record_encode(
    const oamr_fault_record_t *record,
    uint8_t *out_data,
    size_t out_capacity,
    size_t *out_size)
{
    if (out_size != NULL) {
        *out_size = 0u;
    }
    if (record == NULL || out_data == NULL || out_size == NULL ||
        out_capacity < OAMR_FAULT_RECORD_SERIALIZED_SIZE) {
        return OAMR_STATUS_INVALID;
    }

    oamr_fault_encode_fields(record, out_data);
    *out_size = OAMR_FAULT_RECORD_SERIALIZED_SIZE;
    return OAMR_STATUS_OK;
}

oamr_fault_record_validity_t oamr_fault_record_validate(
    const uint8_t *data,
    size_t data_size,
    oamr_fault_validation_t *out_validation)
{
    oamr_fault_validation_t result;
    uint32_t actual_crc;

    result.validity = OAMR_FAULT_RECORD_INVALID;
    result.error = OAMR_FAULT_VALIDATE_MALFORMED;
    result.inspected_size = 0u;

    if (data_size < OAMR_FAULT_RECORD_SERIALIZED_SIZE) {
        result.error = OAMR_FAULT_VALIDATE_TRUNCATED;
        result.inspected_size = data_size;
        if (data == NULL && data_size != 0u) {
            result.error = OAMR_FAULT_VALIDATE_MALFORMED;
        }
        if (out_validation != NULL) {
            *out_validation = result;
        }
        return OAMR_FAULT_RECORD_INVALID;
    }

    if (data == NULL) {
        if (out_validation != NULL) {
            *out_validation = result;
        }
        return OAMR_FAULT_RECORD_INVALID;
    }

    result.inspected_size = OAMR_FAULT_RECORD_SERIALIZED_SIZE;
    if (oamr_fault_get_u32(data) != OAMR_FAULT_RECORD_MAGIC) {
        result.error = OAMR_FAULT_VALIDATE_BAD_MAGIC;
    } else if (oamr_fault_get_u16(data + 4u) != OAMR_FAULT_RECORD_VERSION) {
        result.error = OAMR_FAULT_VALIDATE_UNSUPPORTED_VERSION;
    } else if (oamr_fault_get_u16(data + 6u) !=
               (uint16_t)OAMR_FAULT_RECORD_SERIALIZED_SIZE) {
        result.error = OAMR_FAULT_VALIDATE_BAD_LENGTH;
    } else {
        actual_crc = oamr_fault_crc32(
            data, OAMR_FAULT_RECORD_CRC_OFFSET);
        if (actual_crc != oamr_fault_get_u32(data + 40u)) {
            result.error = OAMR_FAULT_VALIDATE_CRC_MISMATCH;
        } else {
            result.validity = OAMR_FAULT_RECORD_VALID;
            result.error = (oamr_fault_validate_error_t)0;
        }
    }

    if (out_validation != NULL) {
        *out_validation = result;
    }
    return result.validity;
}

oamr_status_t oamr_fault_record_decode(
    const uint8_t *data,
    size_t data_size,
    oamr_fault_record_t *out_record)
{
    if (out_record == NULL) {
        return OAMR_STATUS_INVALID;
    }

    out_record->magic = 0u;
    out_record->version = 0u;
    out_record->length = 0u;
    out_record->data.raw_reset_flags = 0u;
    out_record->data.stacked_pc = 0u;
    out_record->data.stacked_lr = 0u;
    out_record->data.stacked_xpsr = 0u;
    out_record->data.cfsr = 0u;
    out_record->data.hfsr = 0u;
    out_record->data.bfar = 0u;
    out_record->data.mmar = 0u;
    out_record->reset_definitions.power_on = 0u;
    out_record->reset_definitions.pin = 0u;
    out_record->reset_definitions.software = 0u;
    out_record->reset_definitions.iwdg = 0u;
    out_record->reset_definitions.wwdg = 0u;
    out_record->reset_definitions.low_power = 0u;
    out_record->reset_definitions.bor = 0u;
    out_record->reset_definitions.other = 0u;
    out_record->crc32 = 0u;

    if (oamr_fault_record_validate(data, data_size, NULL) !=
        OAMR_FAULT_RECORD_VALID) {
        return OAMR_STATUS_INVALID;
    }

    out_record->magic = oamr_fault_get_u32(data);
    out_record->version = oamr_fault_get_u16(data + 4u);
    out_record->length = oamr_fault_get_u16(data + 6u);
    out_record->data.raw_reset_flags = oamr_fault_get_u32(data + 8u);
    out_record->data.stacked_pc = oamr_fault_get_u32(data + 12u);
    out_record->data.stacked_lr = oamr_fault_get_u32(data + 16u);
    out_record->data.stacked_xpsr = oamr_fault_get_u32(data + 20u);
    out_record->data.cfsr = oamr_fault_get_u32(data + 24u);
    out_record->data.hfsr = oamr_fault_get_u32(data + 28u);
    out_record->data.bfar = oamr_fault_get_u32(data + 32u);
    out_record->data.mmar = oamr_fault_get_u32(data + 36u);
    out_record->crc32 = oamr_fault_get_u32(data + 40u);
    return OAMR_STATUS_OK;
}
