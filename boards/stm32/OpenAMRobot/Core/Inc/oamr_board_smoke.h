/* USER CODE BEGIN Header */
/* Build-time interface consumer; not a wire protocol or safety interface. */
/* USER CODE END Header */
#ifndef OAMR_BOARD_SMOKE_H
#define OAMR_BOARD_SMOKE_H

#include "oamr/bounded_queue.h"
#include "oamr/can_frame.h"
#include "oamr/freshness.h"
#include "oamr/sample.h"
#include "oamr/status.h"
#include "oamr/time.h"
#include "oamr/transport.h"

oamr_status_t oamr_board_smoke(void);

#endif
