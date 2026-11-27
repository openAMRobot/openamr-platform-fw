#include "fault_capture.h"

#include "oamr/fault_retained.h"

#include <string.h>

/* The linker reserves this NOLOAD object outside startup .data/.bss handling. */
__attribute__((section(".fault_retained"), aligned(8), used))
#if defined(OAMR_FAULT_CAPTURE_TEST_TERMINAL)
oamr_fault_retained_storage_t retained_storage;
#else
static oamr_fault_retained_storage_t retained_storage;
#endif

static oamr_fault_retained_t retained;
static oamr_fault_retained_snapshot_t boot_snapshot;
static bool boot_has_record;
static bool storage_ready;

/* Same reviewed raw-RSR definitions used by the USART3 reset reporter. */
static const oamr_reset_flag_definitions_t reset_definitions = {
  .power_on = RCC_RSR_PORRSTF,
  .pin = RCC_RSR_PINRSTF,
  .software = RCC_RSR_SFTRSTF,
  .iwdg = RCC_RSR_IWDG1RSTF,
  .wwdg = RCC_RSR_WWDG1RSTF,
  .low_power = RCC_RSR_LPWRRSTF,
  .bor = RCC_RSR_BORRSTF,
  .other = RCC_RSR_CPURSTF | RCC_RSR_D1RSTF | RCC_RSR_D2RSTF
};

static bool fault_capture_prepare_storage_boot(void)
{
  /* Normal boot establishes readiness once. Do not call this from a fault. */
  __HAL_RCC_BKPRAM_CLK_ENABLE();
  HAL_PWR_EnableBkUpAccess();
  if ((HAL_PWREx_EnableBkUpReg() != HAL_OK) ||
      (__HAL_PWR_GET_FLAG(PWR_FLAG_BRR) == 0U))
  {
    storage_ready = false;
    return false;
  }
  if (oamr_fault_retained_init(&retained, &retained_storage) != OAMR_STATUS_OK)
  {
    storage_ready = false;
    return false;
  }
  storage_ready = true;
  return true;
}

void fault_capture_from_exception(const uint32_t *stack_pointer,
                                  uint32_t exc_return,
                                  uint32_t exception_kind)
{
  oamr_exception_scb_snapshot_t scb;
  oamr_fault_record_data_t data;
  oamr_exception_capture_metadata_t metadata;
  oamr_fault_record_t record;
  size_t frame_words;

  scb.cfsr = SCB->CFSR;
  scb.hfsr = SCB->HFSR;
  scb.bfar = SCB->BFAR;
  scb.mmar = SCB->MMFAR;
  frame_words = oamr_exception_frame_required_words(exc_return);
  if ((frame_words != 0u) &&
      (oamr_exception_frame_decode(stack_pointer, frame_words, exc_return,
                                   exception_kind, &scb, RCC->RSR,
                                   &data, &metadata) != OAMR_STATUS_OK))
  {
    frame_words = 0u;
  }
  if (frame_words == 0u)
  {
    (void)oamr_exception_capture_scb_only(exception_kind, &scb, RCC->RSR,
                                          &data, &metadata);
    if (oamr_exception_frame_required_words(exc_return) != 0u)
    {
      metadata.exc_return = exc_return;
      if ((exc_return & UINT32_C(4)) != 0u)
      {
        metadata.flags |= (uint16_t)OAMR_EXCEPTION_CAPTURE_USES_PSP;
      }
      if ((exc_return & UINT32_C(0x10)) == 0u)
      {
        metadata.flags |= (uint16_t)OAMR_EXCEPTION_CAPTURE_EXTENDED_FRAME;
      }
    }
  }

  /* No waits or regulator changes in exception context. If normal startup did
   * not establish readiness, preserve registers only for the debugger and skip
   * retention rather than fabricating a valid record. */
  if (storage_ready && (__HAL_PWR_GET_FLAG(PWR_FLAG_BRR) != 0U) &&
      (oamr_fault_retained_init(&retained, &retained_storage) == OAMR_STATUS_OK) &&
      (oamr_fault_record_init(&record, &data, &reset_definitions) == OAMR_STATUS_OK))
  {
    (void)oamr_fault_retained_commit(&retained, &record, &metadata, NULL);
  }

  /* Preserve the previous terminal behavior. No UART, delay, watchdog,
   * recovery, safe-output, or other control action belongs in this path. */
#if defined(OAMR_FAULT_CAPTURE_TEST_TERMINAL)
  oamr_fault_capture_test_terminal();
#endif
  for (;;)
  {
  }
}

void fault_capture_boot_init(void)
{
  oamr_fault_retained_snapshot_t snapshot;
  oamr_status_t status;

  (void)memset(&boot_snapshot, 0, sizeof(boot_snapshot));
  boot_has_record = false;
  storage_ready = false;
  if (!fault_capture_prepare_storage_boot())
  {
    return;
  }
  status = oamr_fault_retained_load(&retained, &snapshot);
  if (status == OAMR_STATUS_OK)
  {
    snapshot.record.reset_definitions = reset_definitions;
    boot_snapshot = snapshot;
    boot_has_record = true;
  }
}

void fault_capture_boot_complete(void)
{
  /* Do not leave all backup-domain control registers writable in runtime.
   * The regulator/clock remain enabled; actual retention still needs bench
   * evidence and the fault path performs no setup or waits. */
  HAL_PWR_DisableBkUpAccess();
}

bool fault_capture_storage_ready(void)
{
  return storage_ready;
}

bool fault_capture_boot_has_record(void)
{
  return boot_has_record;
}

bool fault_capture_boot_get_snapshot(oamr_fault_record_t *out_record,
                                     oamr_exception_capture_metadata_t *out_metadata)
{
  if (!boot_has_record || (out_record == NULL) || (out_metadata == NULL))
  {
    return false;
  }
  *out_record = boot_snapshot.record;
  *out_metadata = boot_snapshot.metadata;
  return true;
}

bool fault_capture_acknowledge(
    const oamr_fault_record_t *record,
    const oamr_exception_capture_metadata_t *metadata)
{
  oamr_fault_retained_snapshot_t requested;
  oamr_status_t status;

  if (!boot_has_record || (record == NULL) || (metadata == NULL) ||
      (record->crc32 != boot_snapshot.record.crc32) ||
      (metadata->sequence != boot_snapshot.metadata.sequence) ||
      (metadata->exc_return != boot_snapshot.metadata.exc_return) ||
      (metadata->exception_kind != boot_snapshot.metadata.exception_kind) ||
      (metadata->flags != boot_snapshot.metadata.flags) ||
      (memcmp(&record->data, &boot_snapshot.record.data,
              sizeof(record->data)) != 0))
  {
    return false;
  }
  requested = boot_snapshot;
  status = oamr_fault_retained_retire(
      &retained, requested.slot_index, &requested);
  if (status != OAMR_STATUS_OK)
  {
    return false;
  }
  (void)memset(&boot_snapshot, 0, sizeof(boot_snapshot));
  boot_has_record = false;
  return true;
}
