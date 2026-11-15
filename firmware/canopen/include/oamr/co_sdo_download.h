/* SPDX-License-Identifier: MIT */
/*
 * CiA 301 SDO expedited download (write) request encoder, kept apart from the
 * read-only SDO client codec (oamr/co_sdo.h).
 *
 * This is a codec function and nothing else. It is compiled into its own
 * library (oamr_canopen_sdo_download in tests/host/canopen/CMakeLists.txt);
 * the read-only library oamr_canopen does not contain or link it, and
 * tests/host/canopen/read_only_guard.cmake fails if the read-only archive
 * defines or references the symbol, or if any other source under
 * firmware/canopen or firmware/drive_telemetry names it.
 *
 * Nothing in this repository calls it outside its codec test. A firmware
 * image that must stay read-only does not compile src/co_sdo_download.c.
 *
 * Moved unchanged from co_sdo.c (platform-fw #49, 25a5c72).
 */
#ifndef OAMR_CO_SDO_DOWNLOAD_H
#define OAMR_CO_SDO_DOWNLOAD_H

#include <stdbool.h>
#include <stdint.h>

#include "oamr/co_frame.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Codec only. Initiate expedited download (write) request, size 1, 2 or 4.
 * Returns false for an invalid node id, size or a null frame. */
bool oamr_co_sdo_encode_download_expedited(uint8_t node_id, uint16_t index, uint8_t subindex,
                                           uint32_t value, uint8_t size, oamr_co_frame_t *out);

#ifdef __cplusplus
}
#endif

#endif /* OAMR_CO_SDO_DOWNLOAD_H */
