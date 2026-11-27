#include "oamr/diagnostic_tx.h"

typedef struct {
    uint8_t *data;
    size_t capacity;
    size_t size;
} oamr_diagnostic_builder_t;

static void oamr_diagnostic_sat_add(uint64_t *counter, uint64_t amount)
{
    if (UINT64_MAX - *counter < amount) {
        *counter = UINT64_MAX;
    } else {
        *counter += amount;
    }
}

static void oamr_diagnostic_sat_inc(uint64_t *counter)
{
    if (*counter < UINT64_MAX) {
        ++*counter;
    }
}

static bool oamr_diagnostic_tx_is_valid(const oamr_diagnostic_tx_t *tx)
{
    return tx != NULL && tx->initialized && tx->storage != NULL &&
           tx->storage_size != 0u && tx->capacity != 0u &&
           tx->capacity <= tx->storage_size && tx->head < tx->capacity &&
           tx->count <= tx->capacity && tx->transport.write != NULL;
}

static oamr_status_t oamr_diagnostic_append_byte(
    oamr_diagnostic_builder_t *builder,
    uint8_t byte)
{
    if (builder->size == builder->capacity) {
        return OAMR_STATUS_OVERFLOW;
    }
    builder->data[builder->size] = byte;
    ++builder->size;
    return OAMR_STATUS_OK;
}

static oamr_status_t oamr_diagnostic_append_bytes(
    oamr_diagnostic_builder_t *builder,
    const void *data,
    size_t data_size)
{
    const uint8_t *bytes = (const uint8_t *)data;
    size_t i;

    if (data_size > builder->capacity - builder->size) {
        return OAMR_STATUS_OVERFLOW;
    }
    for (i = 0u; i < data_size; ++i) {
        builder->data[builder->size + i] = bytes[i];
    }
    builder->size += data_size;
    return OAMR_STATUS_OK;
}

static oamr_status_t oamr_diagnostic_append_string(
    oamr_diagnostic_builder_t *builder,
    const char *text)
{
    size_t i = 0u;

    if (text == NULL) {
        return OAMR_STATUS_INVALID;
    }
    while (text[i] != '\0') {
        oamr_status_t status = oamr_diagnostic_append_byte(
            builder, (uint8_t)text[i]);
        if (status != OAMR_STATUS_OK) {
            return status;
        }
        ++i;
    }
    return OAMR_STATUS_OK;
}

#define OAMR_DIAGNOSTIC_APPEND_LITERAL(builder, literal)                       \
    oamr_diagnostic_append_bytes((builder), (literal), sizeof(literal) - 1u)

static oamr_status_t oamr_diagnostic_append_u32(
    oamr_diagnostic_builder_t *builder,
    uint32_t value)
{
    char digits[10];
    size_t count = 0u;
    size_t i;

    do {
        digits[count] = (char)('0' + (value % 10u));
        ++count;
        value /= 10u;
    } while (value != 0u);

    for (i = count; i > 0u; --i) {
        oamr_status_t status = oamr_diagnostic_append_byte(
            builder, (uint8_t)digits[i - 1u]);
        if (status != OAMR_STATUS_OK) {
            return status;
        }
    }
    return OAMR_STATUS_OK;
}

static oamr_status_t oamr_diagnostic_append_hex32(
    oamr_diagnostic_builder_t *builder,
    uint32_t value)
{
    static const char digits[] = "0123456789abcdef";
    unsigned shift;

    oamr_status_t status = OAMR_DIAGNOSTIC_APPEND_LITERAL(builder, "0x");
    if (status != OAMR_STATUS_OK) {
        return status;
    }
    for (shift = 32u; shift > 0u; shift -= 4u) {
        status = oamr_diagnostic_append_byte(
            builder, (uint8_t)digits[(value >> (shift - 4u)) & 0x0fu]);
        if (status != OAMR_STATUS_OK) {
            return status;
        }
    }
    return OAMR_STATUS_OK;
}

static oamr_status_t oamr_diagnostic_append_field_hex32(
    oamr_diagnostic_builder_t *builder,
    const char *name,
    uint32_t value,
    bool first)
{
    oamr_status_t status;

    if (!first) {
        status = oamr_diagnostic_append_byte(builder, (uint8_t)',');
        if (status != OAMR_STATUS_OK) {
            return status;
        }
    }
    status = oamr_diagnostic_append_string(builder, name);
    if (status != OAMR_STATUS_OK) {
        return status;
    }
    status = oamr_diagnostic_append_byte(builder, (uint8_t)'=');
    if (status != OAMR_STATUS_OK) {
        return status;
    }
    return oamr_diagnostic_append_hex32(builder, value);
}

static oamr_status_t oamr_diagnostic_finish(
    oamr_diagnostic_builder_t *builder)
{
    oamr_status_t status = OAMR_DIAGNOSTIC_APPEND_LITERAL(builder, "\r\n");
    if (status == OAMR_STATUS_OK && builder->size < builder->capacity) {
        builder->data[builder->size] = 0u;
    }
    return status;
}

static oamr_status_t oamr_diagnostic_submit_formatted(
    oamr_diagnostic_tx_t *tx,
    oamr_diagnostic_builder_t *builder)
{
    oamr_status_t status = oamr_diagnostic_finish(builder);
    if (status != OAMR_STATUS_OK) {
        oamr_diagnostic_sat_inc(&tx->formatting_errors);
        return status;
    }
    return oamr_diagnostic_tx_submit(tx, builder->data, builder->size);
}

oamr_status_t oamr_diagnostic_tx_init(oamr_diagnostic_tx_t *tx,
                                      void *storage,
                                      size_t storage_size,
                                      const oamr_transport_t *transport)
{
    if (tx == NULL) {
        return OAMR_STATUS_INVALID;
    }

    tx->storage = NULL;
    tx->storage_size = 0u;
    tx->capacity = 0u;
    tx->head = 0u;
    tx->count = 0u;
    tx->transport.context = NULL;
    tx->transport.read = NULL;
    tx->transport.write = NULL;
    tx->enqueued_records = 0u;
    tx->enqueued_bytes = 0u;
    tx->transmitted_bytes = 0u;
    tx->would_block_calls = 0u;
    tx->would_block_bytes = 0u;
    tx->dropped_records = 0u;
    tx->dropped_bytes = 0u;
    tx->formatting_errors = 0u;
    tx->transport_errors = 0u;
    tx->error_bytes = 0u;
    tx->initialized = false;

    if (storage == NULL || storage_size == 0u || transport == NULL ||
        transport->write == NULL) {
        return OAMR_STATUS_INVALID;
    }

    tx->storage = (uint8_t *)storage;
    tx->storage_size = storage_size;
    tx->capacity = storage_size;
    tx->transport = *transport;
    tx->transport.read = NULL;
    tx->initialized = true;
    return OAMR_STATUS_OK;
}

oamr_status_t oamr_diagnostic_tx_submit(oamr_diagnostic_tx_t *tx,
                                        const void *data,
                                        size_t data_size)
{
    const uint8_t *bytes = (const uint8_t *)data;
    size_t tail;
    size_t first_size;
    size_t i;

    if (!oamr_diagnostic_tx_is_valid(tx) ||
        (data_size != 0u && data == NULL)) {
        return OAMR_STATUS_INVALID;
    }
    if (data_size == 0u || data_size > tx->capacity - tx->count) {
        oamr_diagnostic_sat_inc(&tx->dropped_records);
        oamr_diagnostic_sat_add(&tx->dropped_bytes, (uint64_t)data_size);
        return OAMR_STATUS_OVERFLOW;
    }

    tail = tx->head + tx->count;
    if (tail >= tx->capacity) {
        tail -= tx->capacity;
    }
    first_size = tx->capacity - tail;
    if (first_size > data_size) {
        first_size = data_size;
    }
    for (i = 0u; i < first_size; ++i) {
        tx->storage[tail + i] = bytes[i];
    }
    for (i = first_size; i < data_size; ++i) {
        tx->storage[i - first_size] = bytes[i];
    }

    tx->count += data_size;
    oamr_diagnostic_sat_inc(&tx->enqueued_records);
    oamr_diagnostic_sat_add(&tx->enqueued_bytes, (uint64_t)data_size);
    return OAMR_STATUS_OK;
}

oamr_transport_result_t oamr_diagnostic_tx_flush_once(oamr_diagnostic_tx_t *tx)
{
    oamr_transport_result_t result;
    size_t contiguous;
    size_t written = 0u;

    if (!oamr_diagnostic_tx_is_valid(tx)) {
        return OAMR_TRANSPORT_INVALID;
    }
    if (tx->count == 0u) {
        return OAMR_TRANSPORT_OK;
    }

    contiguous = tx->capacity - tx->head;
    if (contiguous > tx->count) {
        contiguous = tx->count;
    }
    result = tx->transport.write(tx->transport.context, tx->storage + tx->head,
                                 contiguous, &written);

    if (result == OAMR_TRANSPORT_OK && written != 0u &&
        written <= contiguous) {
        tx->head += written;
        if (tx->head == tx->capacity) {
            tx->head = 0u;
        }
        tx->count -= written;
        oamr_diagnostic_sat_add(&tx->transmitted_bytes, (uint64_t)written);
        return OAMR_TRANSPORT_OK;
    }

    if (result == OAMR_TRANSPORT_WOULD_BLOCK ||
        (result == OAMR_TRANSPORT_OK && written == 0u)) {
        oamr_diagnostic_sat_inc(&tx->would_block_calls);
        oamr_diagnostic_sat_add(&tx->would_block_bytes, (uint64_t)tx->count);
        return OAMR_TRANSPORT_WOULD_BLOCK;
    }

    /* Retain bytes: neither a malformed transfer nor an error proves loss. */
    oamr_diagnostic_sat_inc(&tx->transport_errors);
    oamr_diagnostic_sat_add(&tx->error_bytes, (uint64_t)tx->count);
    return result == OAMR_TRANSPORT_OK ? OAMR_TRANSPORT_ERROR : result;
}

static oamr_status_t oamr_diagnostic_format_boot(
    uint8_t output[OAMR_DIAGNOSTIC_FORMAT_CAPACITY],
    size_t *out_size,
    const oamr_diagnostic_boot_t *boot)
{
    oamr_diagnostic_builder_t builder = {output,
                                         OAMR_DIAGNOSTIC_FORMAT_CAPACITY, 0u};
    oamr_status_t status;

    if (boot == NULL || boot->image == NULL || boot->board == NULL) {
        return OAMR_STATUS_INVALID;
    }
    status = OAMR_DIAGNOSTIC_APPEND_LITERAL(&builder, "OAMR BOOT image=");
    if (status == OAMR_STATUS_OK) {
        status = oamr_diagnostic_append_string(&builder, boot->image);
    }
    if (status == OAMR_STATUS_OK) {
        status = OAMR_DIAGNOSTIC_APPEND_LITERAL(&builder, ",board=");
    }
    if (status == OAMR_STATUS_OK) {
        status = oamr_diagnostic_append_string(&builder, boot->board);
    }
    if (status == OAMR_STATUS_OK) {
        status = OAMR_DIAGNOSTIC_APPEND_LITERAL(&builder, ",boot_sequence=");
    }
    if (status == OAMR_STATUS_OK) {
        status = oamr_diagnostic_append_u32(&builder, boot->boot_sequence);
    }
    if (status != OAMR_STATUS_OK) {
        return status;
    }
    *out_size = builder.size;
    return OAMR_STATUS_OK;
}

static oamr_status_t oamr_diagnostic_format_version(
    uint8_t output[OAMR_DIAGNOSTIC_FORMAT_CAPACITY],
    size_t *out_size,
    const oamr_diagnostic_version_t *version)
{
    oamr_diagnostic_builder_t builder = {output,
                                         OAMR_DIAGNOSTIC_FORMAT_CAPACITY, 0u};
    oamr_status_t status;

    if (version == NULL || version->firmware == NULL ||
        version->git_revision == NULL || version->configuration == NULL) {
        return OAMR_STATUS_INVALID;
    }
    status = OAMR_DIAGNOSTIC_APPEND_LITERAL(&builder, "OAMR VERSION firmware=");
    if (status == OAMR_STATUS_OK) {
        status = oamr_diagnostic_append_string(&builder, version->firmware);
    }
    if (status == OAMR_STATUS_OK) {
        status = OAMR_DIAGNOSTIC_APPEND_LITERAL(&builder, ",git=");
    }
    if (status == OAMR_STATUS_OK) {
        status = oamr_diagnostic_append_string(&builder, version->git_revision);
    }
    if (status == OAMR_STATUS_OK) {
        status = OAMR_DIAGNOSTIC_APPEND_LITERAL(&builder, ",configuration=");
    }
    if (status == OAMR_STATUS_OK) {
        status = oamr_diagnostic_append_string(&builder, version->configuration);
    }
    if (status != OAMR_STATUS_OK) {
        return status;
    }
    *out_size = builder.size;
    return OAMR_STATUS_OK;
}

static oamr_status_t oamr_diagnostic_format_clock(
    uint8_t output[OAMR_DIAGNOSTIC_FORMAT_CAPACITY],
    size_t *out_size,
    const oamr_diagnostic_clock_t *clock)
{
    static const char prefix[] = "OAMR CLOCK sysclk_hz=";
    oamr_diagnostic_builder_t builder = {output,
                                         OAMR_DIAGNOSTIC_FORMAT_CAPACITY, 0u};
    uint32_t values[4];
    static const char *const separators[] = {",hclk_hz=", ",pclk1_hz=",
                                              ",pclk2_hz="};
    size_t i;
    oamr_status_t status;

    if (clock == NULL) {
        return OAMR_STATUS_INVALID;
    }
    values[0] = clock->sysclk_hz;
    values[1] = clock->hclk_hz;
    values[2] = clock->pclk1_hz;
    values[3] = clock->pclk2_hz;
    status = oamr_diagnostic_append_bytes(&builder, prefix, sizeof(prefix) - 1u);
    for (i = 0u; i < 4u && status == OAMR_STATUS_OK; ++i) {
        if (i != 0u) {
            status = oamr_diagnostic_append_string(&builder, separators[i - 1u]);
        }
        if (status == OAMR_STATUS_OK) {
            status = oamr_diagnostic_append_u32(&builder, values[i]);
        }
    }
    if (status != OAMR_STATUS_OK) {
        return status;
    }
    *out_size = builder.size;
    return OAMR_STATUS_OK;
}

static oamr_status_t oamr_diagnostic_append_status(
    oamr_diagnostic_builder_t *builder,
    oamr_status_t value)
{
    switch (value) {
    case OAMR_STATUS_INVALID:
        return OAMR_DIAGNOSTIC_APPEND_LITERAL(builder, "invalid");
    case OAMR_STATUS_OK:
        return OAMR_DIAGNOSTIC_APPEND_LITERAL(builder, "ok");
    case OAMR_STATUS_EMPTY:
        return OAMR_DIAGNOSTIC_APPEND_LITERAL(builder, "empty");
    case OAMR_STATUS_STALE:
        return OAMR_DIAGNOSTIC_APPEND_LITERAL(builder, "stale");
    case OAMR_STATUS_TIMEOUT:
        return OAMR_DIAGNOSTIC_APPEND_LITERAL(builder, "timeout");
    case OAMR_STATUS_OVERFLOW:
        return OAMR_DIAGNOSTIC_APPEND_LITERAL(builder, "overflow");
    case OAMR_STATUS_ERROR:
        return OAMR_DIAGNOSTIC_APPEND_LITERAL(builder, "error");
    default:
        return OAMR_DIAGNOSTIC_APPEND_LITERAL(builder, "unknown");
    }
}

static oamr_status_t oamr_diagnostic_format_peripheral(
    uint8_t output[OAMR_DIAGNOSTIC_FORMAT_CAPACITY],
    size_t *out_size,
    const oamr_diagnostic_peripheral_t *peripheral)
{
    oamr_diagnostic_builder_t builder = {output,
                                         OAMR_DIAGNOSTIC_FORMAT_CAPACITY, 0u};
    oamr_status_t status;

    if (peripheral == NULL || peripheral->name == NULL) {
        return OAMR_STATUS_INVALID;
    }
    status = OAMR_DIAGNOSTIC_APPEND_LITERAL(&builder, "OAMR PERIPH name=");
    if (status == OAMR_STATUS_OK) {
        status = oamr_diagnostic_append_string(&builder, peripheral->name);
    }
    if (status == OAMR_STATUS_OK) {
        status = OAMR_DIAGNOSTIC_APPEND_LITERAL(&builder, ",status=");
    }
    if (status == OAMR_STATUS_OK) {
        status = oamr_diagnostic_append_status(&builder, peripheral->status);
    }
    if (status == OAMR_STATUS_OK) {
        status = OAMR_DIAGNOSTIC_APPEND_LITERAL(&builder, ",detail=");
    }
    if (status == OAMR_STATUS_OK) {
        status = oamr_diagnostic_append_hex32(&builder, peripheral->detail);
    }
    if (status != OAMR_STATUS_OK) {
        return status;
    }
    *out_size = builder.size;
    return OAMR_STATUS_OK;
}

static oamr_status_t oamr_diagnostic_format_reset(
    uint8_t output[OAMR_DIAGNOSTIC_FORMAT_CAPACITY],
    size_t *out_size,
    const oamr_reset_cause_t *reset)
{
    static const struct {
        uint32_t mask;
        const char *name;
    } causes[] = {
        {OAMR_RESET_CAUSE_POWER_ON, "power_on"},
        {OAMR_RESET_CAUSE_PIN, "pin"},
        {OAMR_RESET_CAUSE_SOFTWARE, "software"},
        {OAMR_RESET_CAUSE_IWDG, "iwdg"},
        {OAMR_RESET_CAUSE_WWDG, "wwdg"},
        {OAMR_RESET_CAUSE_LOW_POWER, "low_power"},
        {OAMR_RESET_CAUSE_BOR, "bor"},
        {OAMR_RESET_CAUSE_OTHER, "other"}
    };
    oamr_diagnostic_builder_t builder = {output,
                                         OAMR_DIAGNOSTIC_FORMAT_CAPACITY, 0u};
    oamr_status_t status;
    size_t i;
    bool any = false;

    if (reset == NULL || (reset->causes & ~UINT32_C(0xff)) != 0u) {
        return OAMR_STATUS_INVALID;
    }
    status = OAMR_DIAGNOSTIC_APPEND_LITERAL(&builder, "OAMR RESET ");
    if (status == OAMR_STATUS_OK) {
        status = oamr_diagnostic_append_field_hex32(&builder, "raw_flags",
                                                    reset->raw_flags, true);
    }
    if (status == OAMR_STATUS_OK) {
        status = oamr_diagnostic_append_field_hex32(&builder, "causes",
                                                    reset->causes, false);
    }
    if (status == OAMR_STATUS_OK) {
        status = oamr_diagnostic_append_field_hex32(&builder, "unknown_flags",
                                                    reset->unknown_flags, false);
    }
    if (status == OAMR_STATUS_OK) {
        status = OAMR_DIAGNOSTIC_APPEND_LITERAL(&builder, ",names=");
    }
    for (i = 0u; i < sizeof(causes) / sizeof(causes[0]) &&
                    status == OAMR_STATUS_OK;
         ++i) {
        if ((reset->causes & causes[i].mask) != 0u) {
            if (any) {
                status = oamr_diagnostic_append_byte(&builder, (uint8_t)'|');
            }
            if (status == OAMR_STATUS_OK) {
                status = oamr_diagnostic_append_string(&builder, causes[i].name);
            }
            any = true;
        }
    }
    if (!any && status == OAMR_STATUS_OK) {
        status = OAMR_DIAGNOSTIC_APPEND_LITERAL(&builder, "none");
    }
    if (status != OAMR_STATUS_OK) {
        return status;
    }
    *out_size = builder.size;
    return OAMR_STATUS_OK;
}

static oamr_status_t oamr_diagnostic_format_fault(
    uint8_t output[OAMR_DIAGNOSTIC_FORMAT_CAPACITY],
    size_t *out_size,
    const oamr_fault_record_t *fault)
{
    oamr_diagnostic_builder_t builder = {output,
                                         OAMR_DIAGNOSTIC_FORMAT_CAPACITY, 0u};
    oamr_status_t status;

    if (fault == NULL) {
        return OAMR_STATUS_INVALID;
    }

#define OAMR_FAULT_TEXT_U32(field, value, first)                               \
    do {                                                                       \
        if (status == OAMR_STATUS_OK) {                                        \
            status = oamr_diagnostic_append_field_hex32(                       \
                &builder, (field), (value), (first));                          \
        }                                                                      \
    } while (false)

    status = OAMR_DIAGNOSTIC_APPEND_LITERAL(&builder, "OAMR FAULT ");
    OAMR_FAULT_TEXT_U32("magic", fault->magic, true);
    if (status == OAMR_STATUS_OK) {
        status = OAMR_DIAGNOSTIC_APPEND_LITERAL(&builder, ",version=");
    }
    if (status == OAMR_STATUS_OK) {
        status = oamr_diagnostic_append_u32(&builder, fault->version);
    }
    if (status == OAMR_STATUS_OK) {
        status = OAMR_DIAGNOSTIC_APPEND_LITERAL(&builder, ",length=");
    }
    if (status == OAMR_STATUS_OK) {
        status = oamr_diagnostic_append_u32(&builder, fault->length);
    }
    OAMR_FAULT_TEXT_U32("raw_reset_flags", fault->data.raw_reset_flags, false);
    OAMR_FAULT_TEXT_U32("stacked_pc", fault->data.stacked_pc, false);
    OAMR_FAULT_TEXT_U32("stacked_lr", fault->data.stacked_lr, false);
    OAMR_FAULT_TEXT_U32("stacked_xpsr", fault->data.stacked_xpsr, false);
    OAMR_FAULT_TEXT_U32("cfsr", fault->data.cfsr, false);
    OAMR_FAULT_TEXT_U32("hfsr", fault->data.hfsr, false);
    OAMR_FAULT_TEXT_U32("bfar", fault->data.bfar, false);
    OAMR_FAULT_TEXT_U32("mmar", fault->data.mmar, false);
    OAMR_FAULT_TEXT_U32("reset_power_on", fault->reset_definitions.power_on, false);
    OAMR_FAULT_TEXT_U32("reset_pin", fault->reset_definitions.pin, false);
    OAMR_FAULT_TEXT_U32("reset_software", fault->reset_definitions.software, false);
    OAMR_FAULT_TEXT_U32("reset_iwdg", fault->reset_definitions.iwdg, false);
    OAMR_FAULT_TEXT_U32("reset_wwdg", fault->reset_definitions.wwdg, false);
    OAMR_FAULT_TEXT_U32("reset_low_power", fault->reset_definitions.low_power, false);
    OAMR_FAULT_TEXT_U32("reset_bor", fault->reset_definitions.bor, false);
    OAMR_FAULT_TEXT_U32("reset_other", fault->reset_definitions.other, false);
    OAMR_FAULT_TEXT_U32("crc32", fault->crc32, false);
#undef OAMR_FAULT_TEXT_U32

    if (status != OAMR_STATUS_OK) {
        return status;
    }
    *out_size = builder.size;
    return OAMR_STATUS_OK;
}

#define OAMR_DIAGNOSTIC_REPORT_FUNCTION(function_name, data_type, formatter)   \
    oamr_status_t function_name(oamr_diagnostic_tx_t *tx,                     \
                                const data_type *data)                         \
    {                                                                          \
        uint8_t formatted[OAMR_DIAGNOSTIC_FORMAT_CAPACITY];                    \
        size_t formatted_size = 0u;                                            \
        oamr_status_t status;                                                  \
        oamr_diagnostic_builder_t builder = {                                  \
            formatted, OAMR_DIAGNOSTIC_FORMAT_CAPACITY, 0u};                   \
                                                                               \
        if (!oamr_diagnostic_tx_is_valid(tx)) {                                \
            return OAMR_STATUS_INVALID;                                        \
        }                                                                      \
        status = formatter(formatted, &formatted_size, data);                  \
        if (status != OAMR_STATUS_OK) {                                        \
            oamr_diagnostic_sat_inc(&tx->formatting_errors);                   \
            return status;                                                     \
        }                                                                      \
        builder.size = formatted_size;                                         \
        return oamr_diagnostic_submit_formatted(tx, &builder);                 \
    }

OAMR_DIAGNOSTIC_REPORT_FUNCTION(oamr_diagnostic_tx_report_boot,
                                oamr_diagnostic_boot_t,
                                oamr_diagnostic_format_boot)
OAMR_DIAGNOSTIC_REPORT_FUNCTION(oamr_diagnostic_tx_report_version,
                                oamr_diagnostic_version_t,
                                oamr_diagnostic_format_version)
OAMR_DIAGNOSTIC_REPORT_FUNCTION(oamr_diagnostic_tx_report_clock,
                                oamr_diagnostic_clock_t,
                                oamr_diagnostic_format_clock)
OAMR_DIAGNOSTIC_REPORT_FUNCTION(oamr_diagnostic_tx_report_peripheral,
                                oamr_diagnostic_peripheral_t,
                                oamr_diagnostic_format_peripheral)
OAMR_DIAGNOSTIC_REPORT_FUNCTION(oamr_diagnostic_tx_report_reset,
                                oamr_reset_cause_t,
                                oamr_diagnostic_format_reset)
OAMR_DIAGNOSTIC_REPORT_FUNCTION(oamr_diagnostic_tx_report_fault,
                                oamr_fault_record_t,
                                oamr_diagnostic_format_fault)

#undef OAMR_DIAGNOSTIC_REPORT_FUNCTION
#undef OAMR_DIAGNOSTIC_APPEND_LITERAL

oamr_status_t oamr_diagnostic_tx_get_snapshot(
    const oamr_diagnostic_tx_t *tx,
    oamr_diagnostic_tx_snapshot_t *out_snapshot)
{
    if (out_snapshot == NULL) {
        return OAMR_STATUS_INVALID;
    }
    out_snapshot->enqueued_records = 0u;
    out_snapshot->enqueued_bytes = 0u;
    out_snapshot->transmitted_bytes = 0u;
    out_snapshot->would_block_calls = 0u;
    out_snapshot->would_block_bytes = 0u;
    out_snapshot->dropped_records = 0u;
    out_snapshot->dropped_bytes = 0u;
    out_snapshot->formatting_errors = 0u;
    out_snapshot->transport_errors = 0u;
    out_snapshot->error_bytes = 0u;
    out_snapshot->queued_bytes = 0u;
    out_snapshot->free_bytes = 0u;
    out_snapshot->capacity = 0u;
    out_snapshot->initialized = false;

    if (!oamr_diagnostic_tx_is_valid(tx)) {
        return OAMR_STATUS_INVALID;
    }

    out_snapshot->enqueued_records = tx->enqueued_records;
    out_snapshot->enqueued_bytes = tx->enqueued_bytes;
    out_snapshot->transmitted_bytes = tx->transmitted_bytes;
    out_snapshot->would_block_calls = tx->would_block_calls;
    out_snapshot->would_block_bytes = tx->would_block_bytes;
    out_snapshot->dropped_records = tx->dropped_records;
    out_snapshot->dropped_bytes = tx->dropped_bytes;
    out_snapshot->formatting_errors = tx->formatting_errors;
    out_snapshot->transport_errors = tx->transport_errors;
    out_snapshot->error_bytes = tx->error_bytes;
    out_snapshot->queued_bytes = tx->count;
    out_snapshot->free_bytes = tx->capacity - tx->count;
    out_snapshot->capacity = tx->capacity;
    out_snapshot->initialized = true;
    return OAMR_STATUS_OK;
}
