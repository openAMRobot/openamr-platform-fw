/* SPDX-License-Identifier: MIT */
/* CiA 301 codec: COB-ID helpers, SDO upload/abort/download codec, heartbeat,
 * NMT command and EMCY decode. Golden frames are written out byte by byte. */
#include "oamr/co_cobid.h"
#include "oamr/co_nmt_emcy.h"
#include "oamr/co_sdo.h"
#include "tharness.h"

static void cobid_helpers(void)
{
    CHECK_EQ(oamr_co_cobid(OAMR_CO_COB_SDO_RX_BASE, 1), 0x601u);
    CHECK_EQ(oamr_co_cobid(OAMR_CO_COB_SDO_TX_BASE, 1), 0x581u);
    CHECK_EQ(oamr_co_cobid(OAMR_CO_COB_HEARTBEAT_BASE, 0x10), 0x710u);
    CHECK_EQ(oamr_co_cobid(OAMR_CO_COB_EMCY_BASE, 0x10), 0x090u);
    CHECK_EQ(oamr_co_cobid(OAMR_CO_COB_TPDO1_BASE, 0x10), 0x190u);
    CHECK_EQ(oamr_co_cobid(OAMR_CO_COB_RPDO1_BASE, 0x10), 0x210u);
    CHECK_EQ(oamr_co_cobid(OAMR_CO_COB_SDO_RX_BASE, 0), 0u);
    CHECK_EQ(oamr_co_cobid(OAMR_CO_COB_SDO_RX_BASE, 128), 0u);
    CHECK(oamr_co_node_id_valid(1));
    CHECK(oamr_co_node_id_valid(127));
    CHECK(!oamr_co_node_id_valid(0));
    CHECK(!oamr_co_node_id_valid(200));
}

static void cobid_classify(void)
{
    uint8_t n = 99;
    CHECK_EQ(oamr_co_classify(0x000, false, &n), OAMR_CO_FN_NMT);
    CHECK_EQ(n, 0);
    CHECK_EQ(oamr_co_classify(0x080, false, &n), OAMR_CO_FN_SYNC);
    CHECK_EQ(oamr_co_classify(0x081, false, &n), OAMR_CO_FN_EMCY);
    CHECK_EQ(n, 1);
    CHECK_EQ(oamr_co_classify(0x701, false, &n), OAMR_CO_FN_HEARTBEAT);
    CHECK_EQ(n, 1);
    CHECK_EQ(oamr_co_classify(0x581, false, &n), OAMR_CO_FN_SDO_TX);
    CHECK_EQ(oamr_co_classify(0x601, false, &n), OAMR_CO_FN_SDO_RX);
    CHECK_EQ(oamr_co_classify(0x190, false, &n), OAMR_CO_FN_TPDO1);
    CHECK_EQ(n, 0x10);
    CHECK_EQ(oamr_co_classify(0x290, false, &n), OAMR_CO_FN_TPDO2);
    CHECK_EQ(oamr_co_classify(0x310, false, &n), OAMR_CO_FN_RPDO2);
    CHECK_EQ(oamr_co_classify(0x390, false, &n), OAMR_CO_FN_TPDO3);
    CHECK_EQ(oamr_co_classify(0x410, false, &n), OAMR_CO_FN_RPDO3);
    CHECK_EQ(oamr_co_classify(0x490, false, &n), OAMR_CO_FN_TPDO4);
    CHECK_EQ(oamr_co_classify(0x510, false, &n), OAMR_CO_FN_RPDO4);
    CHECK_EQ(oamr_co_classify(0x765, false, &n), OAMR_CO_FN_HEARTBEAT); /* 0x700 + 0x65 */
    CHECK_EQ(n, 0x65);
    CHECK_EQ(oamr_co_classify(0x7E5, false, &n), OAMR_CO_FN_OTHER);     /* LSS response */
    CHECK_EQ(oamr_co_classify(0x700, false, &n), OAMR_CO_FN_OTHER);     /* node 0 */
    CHECK_EQ(oamr_co_classify(0x100, false, &n), OAMR_CO_FN_OTHER);     /* TIME */
    CHECK_EQ(oamr_co_classify(0x701, true, &n), OAMR_CO_FN_INVALID);    /* extended */
    CHECK_EQ(oamr_co_classify(0x800, false, &n), OAMR_CO_FN_INVALID);   /* > 11 bit */
    CHECK_EQ(oamr_co_classify(0x701, false, NULL), OAMR_CO_FN_HEARTBEAT);
}

static void sdo_upload_request_golden(void)
{
    oamr_co_frame_t f;
    CHECK(oamr_co_sdo_encode_upload_request(1, 0x6041, 0, &f));
    const uint8_t want[8] = {0x40, 0x41, 0x60, 0x00, 0, 0, 0, 0};
    CHECK_EQ(f.id, 0x601u);
    CHECK(!f.extended);
    CHECK_EQ(f.dlc, 8);
    CHECK(memcmp(f.data, want, 8) == 0);

    CHECK(oamr_co_sdo_encode_upload_request(1, 0x606C, 2, &f));
    const uint8_t want2[8] = {0x40, 0x6C, 0x60, 0x02, 0, 0, 0, 0};
    CHECK(memcmp(f.data, want2, 8) == 0);

    CHECK(!oamr_co_sdo_encode_upload_request(0, 0x6041, 0, &f));
    CHECK(!oamr_co_sdo_encode_upload_request(128, 0x6041, 0, &f));
    CHECK(!oamr_co_sdo_encode_upload_request(1, 0x6041, 0, NULL));
}

static void sdo_upload_response_sizes(void)
{
    oamr_co_sdo_response_t r;
    /* 4 bytes, s=1 n=0: 43h. 606Ch:01 = -1234 (0xFFFFFB2E) */
    const uint8_t b4[8] = {0x43, 0x6C, 0x60, 0x01, 0x2E, 0xFB, 0xFF, 0xFF};
    oamr_co_frame_t f = th_frame(0x581, 8, b4);
    CHECK_EQ(oamr_co_sdo_decode_upload_response(&f, 1, 0x606C, 1, &r), OAMR_CO_SDO_RX_OK);
    CHECK_EQ(r.size, 4);
    CHECK(r.size_indicated);
    CHECK_EQ((int32_t)r.value, -1234);

    /* 2 bytes: 4Bh. statusword 0637h, garbage in the unused bytes is masked */
    const uint8_t b2[8] = {0x4B, 0x41, 0x60, 0x00, 0x37, 0x06, 0xAA, 0x55};
    f = th_frame(0x581, 8, b2);
    CHECK_EQ(oamr_co_sdo_decode_upload_response(&f, 1, 0x6041, 0, &r), OAMR_CO_SDO_RX_OK);
    CHECK_EQ(r.size, 2);
    CHECK_EQ(r.value, 0x0637u);

    /* 1 byte: 4Fh */
    const uint8_t b1[8] = {0x4F, 0x26, 0x20, 0x05, 0x01, 0xEE, 0xEE, 0xEE};
    f = th_frame(0x581, 8, b1);
    CHECK_EQ(oamr_co_sdo_decode_upload_response(&f, 1, 0x2026, 5, &r), OAMR_CO_SDO_RX_OK);
    CHECK_EQ(r.size, 1);
    CHECK_EQ(r.value, 1u);

    /* 3 bytes: 47h */
    const uint8_t b3[8] = {0x47, 0x00, 0x10, 0x00, 0x11, 0x22, 0x33, 0x44};
    f = th_frame(0x581, 8, b3);
    CHECK_EQ(oamr_co_sdo_decode_upload_response(&f, 1, 0x1000, 0, &r), OAMR_CO_SDO_RX_OK);
    CHECK_EQ(r.size, 3);
    CHECK_EQ(r.value, 0x332211u);

    /* expedited, size not indicated: 42h -> 4 bytes, size_indicated false */
    const uint8_t bn[8] = {0x42, 0x3F, 0x60, 0x00, 0x04, 0x00, 0x00, 0x02};
    f = th_frame(0x581, 8, bn);
    CHECK_EQ(oamr_co_sdo_decode_upload_response(&f, 1, 0x603F, 0, &r), OAMR_CO_SDO_RX_OK);
    CHECK_EQ(r.size, 4);
    CHECK(!r.size_indicated);
    CHECK_EQ(r.value, 0x02000004u);

    /* s=0 but n != 0 is malformed */
    const uint8_t bbad[8] = {0x46, 0x3F, 0x60, 0x00, 0, 0, 0, 0};
    f = th_frame(0x581, 8, bbad);
    CHECK_EQ(oamr_co_sdo_decode_upload_response(&f, 1, 0x603F, 0, &r), OAMR_CO_SDO_RX_MALFORMED);
}

static void sdo_upload_response_rejects(void)
{
    oamr_co_sdo_response_t r;
    const uint8_t ok[8] = {0x4B, 0x41, 0x60, 0x00, 0x37, 0x06, 0, 0};
    oamr_co_frame_t f;

    /* truncated and oversized */
    for (uint8_t dlc = 0; dlc < 8; dlc++) {
        f = th_frame(0x581, dlc, ok);
        CHECK_EQ(oamr_co_sdo_decode_upload_response(&f, 1, 0x6041, 0, &r), OAMR_CO_SDO_RX_MALFORMED);
        CHECK_EQ(r.value, 0u);
    }
    f = th_frame(0x581, 8, ok);
    f.dlc = 9; /* malformed classic CAN length */
    CHECK_EQ(oamr_co_sdo_decode_upload_response(&f, 1, 0x6041, 0, &r), OAMR_CO_SDO_RX_MALFORMED);
    f.dlc = 15;
    CHECK_EQ(oamr_co_sdo_decode_upload_response(&f, 1, 0x6041, 0, &r), OAMR_CO_SDO_RX_MALFORMED);

    /* wrong node, request direction, extended */
    f = th_frame(0x582, 8, ok);
    CHECK_EQ(oamr_co_sdo_decode_upload_response(&f, 1, 0x6041, 0, &r), OAMR_CO_SDO_RX_NOT_FOR_US);
    f = th_frame(0x601, 8, ok);
    CHECK_EQ(oamr_co_sdo_decode_upload_response(&f, 1, 0x6041, 0, &r), OAMR_CO_SDO_RX_NOT_FOR_US);
    f = th_frame(0x581, 8, ok);
    f.extended = true;
    CHECK_EQ(oamr_co_sdo_decode_upload_response(&f, 1, 0x6041, 0, &r), OAMR_CO_SDO_RX_NOT_FOR_US);
    f.extended = false;
    CHECK_EQ(oamr_co_sdo_decode_upload_response(&f, 0, 0x6041, 0, &r), OAMR_CO_SDO_RX_NOT_FOR_US);
    CHECK_EQ(oamr_co_sdo_decode_upload_response(NULL, 1, 0x6041, 0, &r), OAMR_CO_SDO_RX_NOT_FOR_US);

    /* wrong index and wrong subindex echo */
    CHECK_EQ(oamr_co_sdo_decode_upload_response(&f, 1, 0x6040, 0, &r), OAMR_CO_SDO_RX_WRONG_OBJECT);
    CHECK_EQ(oamr_co_sdo_decode_upload_response(&f, 1, 0x6041, 1, &r), OAMR_CO_SDO_RX_WRONG_OBJECT);
    CHECK_EQ(r.value, 0u);

    /* segmented initiate (41h, e=0 s=1, size in data) */
    const uint8_t seg[8] = {0x41, 0x41, 0x60, 0x00, 0x10, 0, 0, 0};
    f = th_frame(0x581, 8, seg);
    CHECK_EQ(oamr_co_sdo_decode_upload_response(&f, 1, 0x6041, 0, &r), OAMR_CO_SDO_RX_SEGMENTED);

    /* download ack (60h) to an upload */
    const uint8_t dack[8] = {0x60, 0x41, 0x60, 0x00, 0, 0, 0, 0};
    f = th_frame(0x581, 8, dack);
    CHECK_EQ(oamr_co_sdo_decode_upload_response(&f, 1, 0x6041, 0, &r), OAMR_CO_SDO_RX_UNEXPECTED_CS);

    /* upload segment responses with toggle 0 and toggle 1 (00h, 10h) */
    const uint8_t t0[8] = {0x00, 1, 2, 3, 4, 5, 6, 7};
    const uint8_t t1[8] = {0x10, 1, 2, 3, 4, 5, 6, 7};
    f = th_frame(0x581, 8, t0);
    CHECK_EQ(oamr_co_sdo_decode_upload_response(&f, 1, 0x6041, 0, &r), OAMR_CO_SDO_RX_UNEXPECTED_CS);
    f = th_frame(0x581, 8, t1);
    CHECK_EQ(oamr_co_sdo_decode_upload_response(&f, 1, 0x6041, 0, &r), OAMR_CO_SDO_RX_UNEXPECTED_CS);

    /* block upload (C0h) and reserved bit 4 set in an initiate response */
    const uint8_t blk[8] = {0xC2, 0x41, 0x60, 0x00, 0, 0, 0, 0};
    f = th_frame(0x581, 8, blk);
    CHECK_EQ(oamr_co_sdo_decode_upload_response(&f, 1, 0x6041, 0, &r), OAMR_CO_SDO_RX_MALFORMED);
    const uint8_t rsv[8] = {0x5B, 0x41, 0x60, 0x00, 0, 0, 0, 0};
    f = th_frame(0x581, 8, rsv);
    CHECK_EQ(oamr_co_sdo_decode_upload_response(&f, 1, 0x6041, 0, &r), OAMR_CO_SDO_RX_MALFORMED);

    /* NULL out is allowed */
    f = th_frame(0x581, 8, ok);
    CHECK_EQ(oamr_co_sdo_decode_upload_response(&f, 1, 0x6041, 0, NULL), OAMR_CO_SDO_RX_OK);
}

static void sdo_abort_decode(void)
{
    oamr_co_sdo_response_t r;
    /* 606Ch:03 does not exist: 06090011h */
    const uint8_t ab[8] = {0x80, 0x6C, 0x60, 0x03, 0x11, 0x00, 0x09, 0x06};
    oamr_co_frame_t f = th_frame(0x581, 8, ab);
    CHECK_EQ(oamr_co_sdo_decode_upload_response(&f, 1, 0x606C, 3, &r), OAMR_CO_SDO_RX_ABORT);
    CHECK_EQ(r.abort_code, OAMR_CO_SDO_ABORT_NO_SUBINDEX);
    CHECK(strcmp(oamr_co_sdo_abort_name(r.abort_code), "subindex does not exist") == 0);
    /* abort for another object is not ours */
    CHECK_EQ(oamr_co_sdo_decode_upload_response(&f, 1, 0x606C, 1, &r), OAMR_CO_SDO_RX_WRONG_OBJECT);

    uint16_t idx = 0;
    uint8_t sub = 0;
    uint32_t code = 0;
    CHECK(oamr_co_sdo_decode_abort(&f, 0x581, &idx, &sub, &code));
    CHECK_EQ(idx, 0x606C);
    CHECK_EQ(sub, 3);
    CHECK_EQ(code, 0x06090011u);
    CHECK(oamr_co_sdo_decode_abort(&f, 0x581, NULL, NULL, NULL));
    CHECK(!oamr_co_sdo_decode_abort(&f, 0x601, &idx, &sub, &code));
    f.dlc = 7;
    CHECK(!oamr_co_sdo_decode_abort(&f, 0x581, &idx, &sub, &code));
    const uint8_t notab[8] = {0x43, 0x6C, 0x60, 0x03, 0, 0, 0, 0};
    f = th_frame(0x581, 8, notab);
    CHECK(!oamr_co_sdo_decode_abort(&f, 0x581, &idx, &sub, &code));

    CHECK(strcmp(oamr_co_sdo_abort_name(0x12345678u), "unknown") == 0);
    CHECK(strcmp(oamr_co_sdo_abort_name(OAMR_CO_SDO_ABORT_TIMEOUT), "SDO protocol timed out") == 0);
    CHECK(strcmp(oamr_co_sdo_abort_name(OAMR_CO_SDO_ABORT_NO_OBJECT), "object does not exist") == 0);
}

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

static void heartbeat_decode(void)
{
    oamr_co_nmt_state_t st = OAMR_CO_NMT_STOPPED;
    const uint8_t s05[1] = {0x05}, s7f[1] = {0x7F}, s00[1] = {0x00}, s04[1] = {0x04};
    const uint8_t s85[1] = {0x85}, s01[1] = {0x01};
    oamr_co_frame_t f = th_frame(0x701, 1, s05);
    CHECK_EQ(oamr_co_decode_heartbeat(&f, 1, &st), OAMR_CO_DEC_OK);
    CHECK_EQ(st, OAMR_CO_NMT_OPERATIONAL);
    f = th_frame(0x701, 1, s7f);
    CHECK_EQ(oamr_co_decode_heartbeat(&f, 1, &st), OAMR_CO_DEC_OK);
    CHECK_EQ(st, OAMR_CO_NMT_PRE_OPERATIONAL);
    f = th_frame(0x701, 1, s00);
    CHECK_EQ(oamr_co_decode_heartbeat(&f, 1, &st), OAMR_CO_DEC_OK);
    CHECK_EQ(st, OAMR_CO_NMT_BOOTUP);
    f = th_frame(0x701, 1, s04);
    CHECK_EQ(oamr_co_decode_heartbeat(&f, 1, &st), OAMR_CO_DEC_OK);
    CHECK_EQ(st, OAMR_CO_NMT_STOPPED);
    /* toggle bit from node guarding and unknown state */
    f = th_frame(0x701, 1, s85);
    CHECK_EQ(oamr_co_decode_heartbeat(&f, 1, &st), OAMR_CO_DEC_MALFORMED);
    f = th_frame(0x701, 1, s01);
    CHECK_EQ(oamr_co_decode_heartbeat(&f, 1, &st), OAMR_CO_DEC_MALFORMED);
    /* truncated, oversized */
    f = th_frame(0x701, 0, NULL);
    CHECK_EQ(oamr_co_decode_heartbeat(&f, 1, &st), OAMR_CO_DEC_MALFORMED);
    const uint8_t two[2] = {0x05, 0x00};
    f = th_frame(0x701, 2, two);
    CHECK_EQ(oamr_co_decode_heartbeat(&f, 1, &st), OAMR_CO_DEC_MALFORMED);
    /* wrong node, extended */
    f = th_frame(0x710, 1, s05);
    CHECK_EQ(oamr_co_decode_heartbeat(&f, 1, &st), OAMR_CO_DEC_NOT_FOR_US);
    CHECK_EQ(oamr_co_decode_heartbeat(&f, 0x10, NULL), OAMR_CO_DEC_OK);
    f.extended = true;
    CHECK_EQ(oamr_co_decode_heartbeat(&f, 0x10, &st), OAMR_CO_DEC_NOT_FOR_US);
}

static void nmt_command_decode(void)
{
    oamr_co_nmt_cmd_t c = OAMR_CO_NMT_CMD_STOP;
    uint8_t node = 0xEE;
    const uint8_t start_all[2] = {0x01, 0x00};
    oamr_co_frame_t f = th_frame(0x000, 2, start_all);
    CHECK_EQ(oamr_co_decode_nmt_command(&f, &c, &node), OAMR_CO_DEC_OK);
    CHECK_EQ(c, OAMR_CO_NMT_CMD_START);
    CHECK_EQ(node, 0);
    const uint8_t reset1[2] = {0x81, 0x01};
    f = th_frame(0x000, 2, reset1);
    CHECK_EQ(oamr_co_decode_nmt_command(&f, &c, &node), OAMR_CO_DEC_OK);
    CHECK_EQ(c, OAMR_CO_NMT_CMD_RESET_NODE);
    CHECK_EQ(node, 1);
    const uint8_t bad[2] = {0x03, 0x01};
    f = th_frame(0x000, 2, bad);
    CHECK_EQ(oamr_co_decode_nmt_command(&f, &c, &node), OAMR_CO_DEC_MALFORMED);
    const uint8_t badnode[2] = {0x01, 0x80};
    f = th_frame(0x000, 2, badnode);
    CHECK_EQ(oamr_co_decode_nmt_command(&f, &c, &node), OAMR_CO_DEC_MALFORMED);
    f = th_frame(0x000, 1, start_all);
    CHECK_EQ(oamr_co_decode_nmt_command(&f, &c, &node), OAMR_CO_DEC_MALFORMED);
    const uint8_t three[3] = {0x01, 0x00, 0x00};
    f = th_frame(0x000, 3, three);
    CHECK_EQ(oamr_co_decode_nmt_command(&f, &c, &node), OAMR_CO_DEC_MALFORMED);
    f = th_frame(0x001, 2, start_all);
    CHECK_EQ(oamr_co_decode_nmt_command(&f, &c, &node), OAMR_CO_DEC_NOT_FOR_US);
}

static void emcy_decode(void)
{
    oamr_co_emcy_t e;
    /* 2310h current at output, error register 03h, manufacturer bytes */
    const uint8_t b[8] = {0x10, 0x23, 0x03, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5};
    oamr_co_frame_t f = th_frame(0x081, 8, b);
    CHECK_EQ(oamr_co_decode_emcy(&f, 1, &e), OAMR_CO_DEC_OK);
    CHECK_EQ(e.error_code, 0x2310);
    CHECK_EQ(e.error_register, OAMR_CO_ERRREG_GENERIC | OAMR_CO_ERRREG_CURRENT);
    CHECK_EQ(e.manufacturer[0], 0xA1);
    CHECK_EQ(e.manufacturer[4], 0xA5);
    /* error reset */
    const uint8_t rst[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    f = th_frame(0x081, 8, rst);
    CHECK_EQ(oamr_co_decode_emcy(&f, 1, &e), OAMR_CO_DEC_OK);
    CHECK_EQ(e.error_code, 0);
    /* truncated: cleared output */
    f = th_frame(0x081, 7, b);
    CHECK_EQ(oamr_co_decode_emcy(&f, 1, &e), OAMR_CO_DEC_MALFORMED);
    CHECK_EQ(e.error_code, 0);
    f = th_frame(0x081, 8, b);
    f.dlc = 9;
    CHECK_EQ(oamr_co_decode_emcy(&f, 1, &e), OAMR_CO_DEC_MALFORMED);
    /* SYNC (080h) is not an EMCY of node 1, and wrong node */
    f = th_frame(0x080, 0, NULL);
    CHECK_EQ(oamr_co_decode_emcy(&f, 1, &e), OAMR_CO_DEC_NOT_FOR_US);
    f = th_frame(0x090, 8, b);
    CHECK_EQ(oamr_co_decode_emcy(&f, 1, &e), OAMR_CO_DEC_NOT_FOR_US);
    CHECK_EQ(oamr_co_decode_emcy(&f, 0x10, NULL), OAMR_CO_DEC_OK);
}

int main(void)
{
    static const th_case cases[] = {
        {"cobid_helpers", cobid_helpers},
        {"cobid_classify", cobid_classify},
        {"sdo_upload_request_golden", sdo_upload_request_golden},
        {"sdo_upload_response_sizes", sdo_upload_response_sizes},
        {"sdo_upload_response_rejects", sdo_upload_response_rejects},
        {"sdo_abort_decode", sdo_abort_decode},
        {"sdo_download_codec_only", sdo_download_codec_only},
        {"heartbeat_decode", heartbeat_decode},
        {"nmt_command_decode", nmt_command_decode},
        {"emcy_decode", emcy_decode},
    };
    return th_run("codec", cases, sizeof(cases) / sizeof(cases[0]));
}
