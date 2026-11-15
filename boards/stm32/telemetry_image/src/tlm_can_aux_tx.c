/* SPDX-License-Identifier: MIT */
#include "tlm_can_aux_tx.h"

static uint32_t s_refused[TLM_CAN_AUX_N];

bool tlm_can_aux_tx(tlm_can_aux_bus_t bus, const oamr_co_frame_t *f)
{
    (void)f;
    if ((unsigned)bus < (unsigned)TLM_CAN_AUX_N)
    {
        s_refused[bus]++;
    }
    else
    {
        s_refused[TLM_CAN_AUX_BMS]++;
        s_refused[TLM_CAN_AUX_LIFT]++;
    }
    return false; /* receive-only: no frame is ever handed to FDCAN2 or FDCAN3 */
}

uint32_t tlm_can_aux_tx_refused(tlm_can_aux_bus_t bus)
{
    return ((unsigned)bus < (unsigned)TLM_CAN_AUX_N) ? s_refused[bus] : 0u;
}
