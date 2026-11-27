#include "oamr/mb7060.h"

#include <stddef.h>
#include <string.h>

#define OAMR_MB_CHAR_R 0x52u
#define OAMR_MB_CHAR_CR 0x0du
#define OAMR_MB_CHAR_ZERO 0x30u
#define OAMR_MB_CHAR_NINE 0x39u

static void increment(uint64_t *counter)
{
    if (*counter < UINT64_MAX) {
        ++*counter;
    }
}

static void discard(oamr_mb_t *parser, uint64_t count)
{
    if (UINT64_MAX - parser->counters.bytes_discarded < count) {
        parser->counters.bytes_discarded = UINT64_MAX;
    } else {
        parser->counters.bytes_discarded += count;
    }
}

static void reset_partial(oamr_mb_t *parser)
{
    discard(parser, parser->length);
    parser->length = 0u;
    parser->in_frame = false;
}

void oamr_mb_init(oamr_mb_t *parser)
{
    if (parser != NULL) {
        (void)memset(parser, 0, sizeof(*parser));
    }
}

void oamr_mb_arm(oamr_mb_t *parser)
{
    if (parser == NULL) {
        return;
    }

    reset_partial(parser);
    parser->armed = true;
}

void oamr_mb_disarm(oamr_mb_t *parser)
{
    if (parser != NULL) {
        parser->armed = false;
    }
}

void oamr_mb_flush(oamr_mb_t *parser)
{
    if (parser != NULL) {
        reset_partial(parser);
    }
}

static bool is_digit(uint8_t byte)
{
    return byte >= OAMR_MB_CHAR_ZERO && byte <= OAMR_MB_CHAR_NINE;
}

static void reject_partial(oamr_mb_t *parser, bool restart)
{
    discard(parser, parser->length);
    parser->length = 0u;
    parser->in_frame = restart;

    if (restart) {
        parser->frame[0] = OAMR_MB_CHAR_R;
        parser->length = 1u;
    }
    increment(&parser->counters.malformed);
}

static oamr_mb_push_t finish_frame(oamr_mb_t *parser,
                                   oamr_mb_sample_t *out)
{
    oamr_mb_sample_t sample;
    uint16_t range_cm = 0u;
    uint8_t digit_count = (uint8_t)(parser->length - 1u);
    uint8_t i;

    discard(parser, (uint64_t)parser->length + 1u);
    parser->length = 0u;
    parser->in_frame = false;

    if (digit_count == 0u || digit_count > OAMR_MB_MAX_DIGITS) {
        increment(&parser->counters.malformed);
        return OAMR_MB_PUSH_MALFORMED;
    }

    for (i = 0u; i < digit_count; ++i) {
        range_cm = (uint16_t)(range_cm * 10u);
        range_cm = (uint16_t)(range_cm +
                             (uint16_t)(parser->frame[i + 1u] -
                                        OAMR_MB_CHAR_ZERO));
    }

    if (!parser->armed) {
        increment(&parser->counters.unsolicited);
        return OAMR_MB_PUSH_UNSOLICITED;
    }

    sample.range_cm = range_cm;
    ++parser->next_sequence;
    sample.sequence = parser->next_sequence;
    parser->armed = false;
    increment(&parser->counters.frames_ok);
    *out = sample;
    return OAMR_MB_PUSH_SAMPLE;
}

oamr_mb_push_t oamr_mb_push(oamr_mb_t *parser,
                            uint8_t byte,
                            oamr_mb_sample_t *out)
{
    if (parser == NULL || out == NULL || parser->length > OAMR_MB_MAX_FRAME_LEN ||
        (parser->in_frame && parser->length == 0u)) {
        return OAMR_MB_PUSH_INVALID;
    }

    if (!parser->in_frame) {
        if (byte != OAMR_MB_CHAR_R) {
            discard(parser, 1u);
            return OAMR_MB_PUSH_NONE;
        }
        parser->frame[0] = byte;
        parser->length = 1u;
        parser->in_frame = true;
        return OAMR_MB_PUSH_NONE;
    }

    if (byte == OAMR_MB_CHAR_R) {
        /* The previous R may itself have been noise; retain only the new R. */
        reject_partial(parser, true);
        return OAMR_MB_PUSH_MALFORMED;
    }

    if (byte == OAMR_MB_CHAR_CR) {
        return finish_frame(parser, out);
    }

    if (!is_digit(byte) || parser->length >= 1u + OAMR_MB_MAX_DIGITS) {
        /* The fourth digit (or any non-digit) is outside the grammar. */
        discard(parser, (uint64_t)parser->length + 1u);
        parser->length = 0u;
        parser->in_frame = false;
        increment(&parser->counters.malformed);
        return OAMR_MB_PUSH_MALFORMED;
    }

    parser->frame[parser->length] = byte;
    ++parser->length;
    return OAMR_MB_PUSH_NONE;
}
