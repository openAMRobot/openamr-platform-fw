/* USER CODE BEGIN oamr_board_smoke */
/* Reference the portable API in the embedded image. This does not configure a
 * peripheral or create an application data path. */
#include "oamr_board_smoke.h"

#include <stddef.h>

static uint8_t oamr_smoke_storage[2];
static oamr_bounded_queue_t oamr_smoke_queue;
static oamr_time_validator_t oamr_smoke_time;
static oamr_can_frame_t oamr_smoke_frame;
static oamr_sample_t oamr_smoke_sample;
static oamr_transport_t oamr_smoke_transport;

static oamr_transport_result_t oamr_smoke_read(void *context, uint8_t *data,
                                                size_t capacity, size_t *transferred)
{
  (void)context;
  (void)data;
  (void)capacity;
  if (transferred != NULL)
  {
    *transferred = 0u;
  }
  return OAMR_TRANSPORT_WOULD_BLOCK;
}

oamr_status_t oamr_board_smoke(void)
{
  oamr_status_t status;

  oamr_smoke_transport.read = oamr_smoke_read;
  (void)oamr_smoke_transport;
  oamr_smoke_sample.unit = OAMR_SAMPLE_UNIT_RAW;
  oamr_smoke_frame.id_type = OAMR_CAN_ID_STANDARD;
  if (!oamr_can_frame_is_valid(&oamr_smoke_frame))
  {
    return OAMR_STATUS_INVALID;
  }

  status = oamr_bounded_queue_init(&oamr_smoke_queue, oamr_smoke_storage,
                                   sizeof(oamr_smoke_storage), 1u,
                                   sizeof(oamr_smoke_storage));
  if (status != OAMR_STATUS_OK)
  {
    return status;
  }
  status = oamr_bounded_queue_push(&oamr_smoke_queue, &oamr_smoke_storage[0]);
  if (status != OAMR_STATUS_OK)
  {
    return status;
  }
  status = oamr_bounded_queue_pop(&oamr_smoke_queue, &oamr_smoke_storage[1]);
  if (status != OAMR_STATUS_OK)
  {
    return status;
  }
  status = oamr_time_validate_monotonic(&oamr_smoke_time, 0u);
  if (status != OAMR_STATUS_OK)
  {
    return status;
  }
  return oamr_freshness_check(0u, true, 0u, 0u);
}
/* USER CODE END oamr_board_smoke */
