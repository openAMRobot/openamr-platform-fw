#include "oamr/tfluna.h"

#include <stddef.h>
#include <string.h>

static void increment(uint64_t *counter)
{
    if (*counter < UINT64_MAX) {
        ++*counter;
    }
}

static void discard(oamr_tfl_t *parser, uint64_t count)
{
    if (UINT64_MAX - parser->counters.bytes_discarded < count) {
        parser->counters.bytes_discarded = UINT64_MAX;
    } else {
        parser->counters.bytes_discarded += count;
    }
}

static bool valid_unit(oamr_tfl_unit_t unit)
{
    return unit == OAMR_TFL_UNIT_CM || unit == OAMR_TFL_UNIT_MM;
}

static uint8_t checksum(const uint8_t *frame)
{
    uint16_t sum = 0u;
    size_t i;

    for (i = 0u; i < OAMR_TFL_FRAME_LEN - 1u; ++i) {
        sum = (uint16_t)(sum + frame[i]);
    }
    return (uint8_t)(sum & 0xffu);
}

oamr_status_t oamr_tfl_init(oamr_tfl_t *parser,
                            const oamr_tfl_config_t *config)
{
    if (parser == NULL) {
        return OAMR_STATUS_INVALID;
    }

    (void)memset(parser, 0, sizeof(*parser));
    if (config == NULL || !valid_unit(config->unit)) {
        return OAMR_STATUS_INVALID;
    }

    parser->config = *config;
    parser->configured = true;
    return OAMR_STATUS_OK;
}

void oamr_tfl_flush(oamr_tfl_t *parser)
{
    if (parser != NULL && parser->length <= OAMR_TFL_FRAME_LEN) {
        discard(parser, parser->length);
        parser->length = 0u;
    }
}

oamr_status_t oamr_tfl_decode(const uint8_t frame[OAMR_TFL_FRAME_LEN],
                              oamr_tfl_unit_t unit,
                              oamr_tfl_sample_t *out)
{
    oamr_tfl_sample_t sample;
    uint16_t distance;
    uint16_t strength;
    uint16_t temperature;

    if (frame == NULL || out == NULL || !valid_unit(unit) ||
        frame[0] != OAMR_TFL_HEADER || frame[1] != OAMR_TFL_HEADER ||
        checksum(frame) != frame[OAMR_TFL_FRAME_LEN - 1u]) {
        return OAMR_STATUS_INVALID;
    }

    distance = (uint16_t)(frame[2] | ((uint16_t)frame[3] << 8));
    strength = (uint16_t)(frame[4] | ((uint16_t)frame[5] << 8));
    temperature = (uint16_t)(frame[6] | ((uint16_t)frame[7] << 8));

    (void)memset(&sample, 0, sizeof(sample));
    sample.raw_distance = distance;
    sample.distance_mm = unit == OAMR_TFL_UNIT_CM ? (uint32_t)distance * 10u
                                                  : (uint32_t)distance;
    sample.strength = strength;
    sample.raw_temperature = temperature;
    sample.temperature_c_x8 = (int32_t)temperature - 2048;
    sample.sequence = 0u;
    *out = sample;
    return OAMR_STATUS_OK;
}

/* Retain only the earliest useful suffix after a rejected nine-byte window. */
static void resynchronize(oamr_tfl_t *parser)
{
    size_t start = 1u;
    size_t retained;

    while (start < OAMR_TFL_FRAME_LEN) {
        if (parser->frame[start] == OAMR_TFL_HEADER &&
            (start + 1u == OAMR_TFL_FRAME_LEN ||
             parser->frame[start + 1u] == OAMR_TFL_HEADER)) {
            break;
        }
        ++start;
    }

    retained = OAMR_TFL_FRAME_LEN - start;
    discard(parser, start);
    if (retained > 0u) {
        (void)memmove(parser->frame, &parser->frame[start], retained);
    }
    parser->length = (uint8_t)retained;

}

oamr_tfl_push_t oamr_tfl_push(oamr_tfl_t *parser,
                              uint8_t byte,
                              oamr_tfl_sample_t *out)
{
    oamr_tfl_sample_t sample;

    if (parser == NULL || out == NULL || parser->length >= OAMR_TFL_FRAME_LEN) {
        return OAMR_TFL_PUSH_INVALID;
    }
    if (!parser->configured || !valid_unit(parser->config.unit)) {
        return OAMR_TFL_PUSH_NOT_CONFIGURED;
    }

    if (parser->length == 0u && byte != OAMR_TFL_HEADER) {
        discard(parser, 1u);
        return OAMR_TFL_PUSH_NONE;
    }
    if (parser->length == 1u && byte != OAMR_TFL_HEADER) {
        discard(parser, 2u);
        parser->length = 0u;
        return OAMR_TFL_PUSH_NONE;
    }

    parser->frame[parser->length] = byte;
    ++parser->length;
    if (parser->length < OAMR_TFL_FRAME_LEN) {
        return OAMR_TFL_PUSH_NONE;
    }

    if (oamr_tfl_decode(parser->frame, parser->config.unit, &sample) !=
        OAMR_STATUS_OK) {
        increment(&parser->counters.checksum_errors);
        resynchronize(parser);
        return OAMR_TFL_PUSH_CHECKSUM_ERROR;
    }

    parser->length = 0u;
    ++parser->next_sequence;
    sample.sequence = parser->next_sequence;
    increment(&parser->counters.frames_ok);
    *out = sample;
    return OAMR_TFL_PUSH_SAMPLE;
}

oamr_status_t oamr_tfl_consume(oamr_tfl_t *parser,
                               const uint8_t *data,
                               size_t length,
                               size_t *byte_consumed,
                               oamr_tfl_sample_t *out)
{
    size_t i;

    if (byte_consumed == NULL) {
        return OAMR_STATUS_INVALID;
    }
    *byte_consumed = 0u;
    if (parser == NULL || out == NULL || (data == NULL && length != 0u)) {
        return OAMR_STATUS_INVALID;
    }
    if (!parser->configured) {
        return OAMR_STATUS_INVALID;
    }

    for (i = 0u; i < length; ++i) {
        oamr_tfl_push_t result = oamr_tfl_push(parser, data[i], out);
        *byte_consumed = i + 1u;
        if (result == OAMR_TFL_PUSH_SAMPLE) {
            return OAMR_STATUS_OK;
        }
        if (result == OAMR_TFL_PUSH_INVALID ||
            result == OAMR_TFL_PUSH_NOT_CONFIGURED) {
            return OAMR_STATUS_INVALID;
        }
    }
    return OAMR_STATUS_EMPTY;
}
