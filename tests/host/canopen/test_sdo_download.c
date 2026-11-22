/* SPDX-License-Identifier: MIT */
/* Codec test of the SDO expedited download encoder, which lives in its own
 * library (oamr_canopen_sdo_download) outside the read-only library.
 * Golden frames: CiA 301 (initiate SDO download, ccs = 1, n/e/s bits);
 * edition and clause not verified against a copy here: "source not verified". */
#include "oamr/co_sdo_download.h"
#include "tharness.h"

/* Codec-only test of the download encoder. A neutral object (1000h-range test
 * index 2FFFh) is used so no drive object appears in a write frame here. */
static void sdo_download_codec_only(void)
{
    oamr_co_frame_t f;
    CHECK(oamr_co_sdo_encode_download_expedited(5, 0x2FFF, 1, 0x11223344u, 4, &f));
    const uint8_t w4[8] = {0x23, 0xFF, 0x2F, 0x01, 0x44, 0x33, 0x22, 0x11};
    CHECK_EQ(f.id, 0x605u);
    CHECK_EQ(f.dlc, 8);
    CHECK(memcmp(f.data, w4, 8) == 0);
    CHECK(oamr_co_sdo_encode_download_expedited(5, 0x2FFF, 1, 0x11223344u, 2, &f));
    const uint8_t w2[8] = {0x2B, 0xFF, 0x2F, 0x01, 0x44, 0x33, 0x00, 0x00};
    CHECK(memcmp(f.data, w2, 8) == 0);
    CHECK(oamr_co_sdo_encode_download_expedited(5, 0x2FFF, 1, 0x11223344u, 1, &f));
    const uint8_t w1[8] = {0x2F, 0xFF, 0x2F, 0x01, 0x44, 0x00, 0x00, 0x00};
    CHECK(memcmp(f.data, w1, 8) == 0);
    CHECK(!oamr_co_sdo_encode_download_expedited(5, 0x2FFF, 1, 0, 3, &f));
    CHECK(!oamr_co_sdo_encode_download_expedited(5, 0x2FFF, 1, 0, 0, &f));
    CHECK(!oamr_co_sdo_encode_download_expedited(5, 0x2FFF, 1, 0, 5, &f));
    CHECK(!oamr_co_sdo_encode_download_expedited(0, 0x2FFF, 1, 0, 4, &f));
    CHECK(!oamr_co_sdo_encode_download_expedited(5, 0x2FFF, 1, 0, 4, NULL));
}

int main(void)
{
    static const th_case cases[] = {
        {"sdo_download_codec_only", sdo_download_codec_only},
    };
    return th_run("sdo_download", cases, sizeof(cases) / sizeof(cases[0]));
}
