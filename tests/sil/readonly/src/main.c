/* SPDX-License-Identifier: MIT */
/*
 * oamr_sil_readonly: line protocol on stdin, JSON lines on stdout. Every
 * command is answered by zero or more JSON lines and then {"type":"done"},
 * so the co-simulator can run in lock step with simulated time.
 *
 *   set KEY VALUE        configuration (all keys required, no defaults)
 *   imu_who HEX          identity of the simulated IMU part (before start)
 *   start T_US           build every module from the keys
 *   t T_US               set the clock (may step backwards) and run one step
 *   can1 ID HEX          classic 11-bit frame received on CAN1 at the current time
 *   can2 ID HEX          29-bit frame received on CAN2 (Daly), receive only
 *   tfl UNIT HEX         bytes from TF-Luna UNIT (0..1)
 *   mb UNIT HEX          bytes from MB7060 UNIT (0..3)
 *   imu HEX              14 data bytes for the IMU register file, data-ready set
 *   imu_bus 0|1          SPI bus down (1) or up (0)
 *   reboot BOOT_ID T_US  controller restart: new boot id, sequence from zero
 *   seqskip N            next status record skips N sequence numbers
 *   mapping              print the record to BaseStatus mapping table
 *
 * There is no command to send anything except what the upload scheduler
 * produces on its own.
 */
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include "chain.h"

void oamr_sil_print_mapping(FILE *out);

static oamr_sil_chain_t g_chain; /* the one context; main owns it */

static size_t hex_bytes(const char *h, uint8_t *out, size_t max)
{
    size_t n = 0;
    while (h[0] != '\0' && h[1] != '\0' && n < max) {
        char b[3] = {h[0], h[1], 0};
        char *end = NULL;
        const unsigned long v = strtoul(b, &end, 16);
        if (end != b + 2) {
            return (size_t)-1;
        }
        out[n++] = (uint8_t)v;
        h += 2;
    }
    if (h[0] != '\0') {
        return (size_t)-1;
    }
    return n;
}

static void bad(const char *what)
{
    printf("{\"type\":\"error\",\"what\":\"%s\"}\n", what);
}

/* Split off the next whitespace-separated word (C11 has no strtok_r). */
static const char *word(char **p)
{
    while (**p == ' ' || **p == '\t' || **p == '\r' || **p == '\n') {
        (*p)++;
    }
    if (**p == '\0') {
        return NULL;
    }
    const char *w = *p;
    while (**p != '\0' && **p != ' ' && **p != '\t' && **p != '\r' && **p != '\n') {
        (*p)++;
    }
    if (**p != '\0') {
        **p = '\0';
        (*p)++;
    }
    return w;
}

static void command(oamr_sil_chain_t *c, char *line)
{
    char *p = line;
    const char *cmd = word(&p);
    const char *a1 = word(&p);
    const char *a2 = word(&p);
    uint8_t buf[64];
    if (cmd == NULL) {
        return;
    }
    if (strcmp(cmd, "set") == 0 && a1 && a2) {
        if (!oamr_sil_chain_set(c, a1, strtod(a2, NULL))) {
            bad("unknown key");
        }
    } else if (strcmp(cmd, "imu_who") == 0 && a1) {
        c->imu_who = (uint8_t)strtoul(a1, NULL, 16);
    } else if (strcmp(cmd, "start") == 0 && a1) {
        (void)oamr_sil_chain_start(c, strtoull(a1, NULL, 10));
    } else if (strcmp(cmd, "t") == 0 && a1) {
        oamr_sil_chain_set_time(c, strtoull(a1, NULL, 10));
        oamr_sil_chain_step(c);
    } else if ((strcmp(cmd, "can1") == 0 || strcmp(cmd, "can2") == 0) && a1) {
        const size_t n = hex_bytes(a2 ? a2 : "", buf, 9);
        if (n > 8u) {
            bad("frame payload");
        } else if (cmd[3] == '1') {
            oamr_co_frame_t f = {(uint32_t)strtoul(a1, NULL, 16), false, (uint8_t)n, {0}};
            memcpy(f.data, buf, n);
            oamr_sil_chain_can1(c, &f);
        } else {
            oamr_daly_rx_frame_t f = {(uint32_t)strtoul(a1, NULL, 16), true, (uint8_t)n, {0}};
            memcpy(f.data, buf, n);
            oamr_sil_chain_can2(c, &f);
        }
    } else if ((strcmp(cmd, "tfl") == 0 || strcmp(cmd, "mb") == 0) && a1 && a2) {
        const size_t n = hex_bytes(a2, buf, sizeof(buf));
        if (n > sizeof(buf)) {
            bad("byte payload");
        } else if (cmd[0] == 't') {
            oamr_sil_chain_tfl_bytes(c, (unsigned)atoi(a1), buf, n);
        } else {
            oamr_sil_chain_mb_bytes(c, (unsigned)atoi(a1), buf, n);
        }
    } else if (strcmp(cmd, "imu") == 0 && a1) {
        if (hex_bytes(a1, buf, 14) != 14u) {
            bad("imu payload");
        } else {
            oamr_sil_chain_imu_data(c, buf);
        }
    } else if (strcmp(cmd, "imu_bus") == 0 && a1) {
        oamr_sil_chain_imu_bus(c, atoi(a1) != 0);
    } else if (strcmp(cmd, "reboot") == 0 && a1 && a2) {
        (void)oamr_sil_chain_reboot(c, (uint32_t)strtoul(a1, NULL, 10), strtoull(a2, NULL, 10));
    } else if (strcmp(cmd, "seqskip") == 0 && a1) {
        c->seq_skip = (uint32_t)strtoul(a1, NULL, 10);
    } else if (strcmp(cmd, "mapping") == 0) {
        oamr_sil_print_mapping(stdout);
    } else {
        bad("unknown command");
    }
}

int main(void)
{
    char line[512];
    oamr_sil_chain_reset(&g_chain, stdout);
    while (fgets(line, sizeof(line), stdin) != NULL) {
        command(&g_chain, line);
        printf("{\"type\":\"done\"}\n");
        fflush(stdout);
    }
    return 0;
}
