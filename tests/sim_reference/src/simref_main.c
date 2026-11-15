/* SIMULATION ONLY. Not production firmware. Not reviewed for safety. Must not be linked into any board image. */
/*
 * Linux process of the simulation reference. Opens two SocketCAN interfaces (drive bus and
 * lift bus) whose names must start with "vcan" (any other name is refused at start-up,
 * before a socket is opened) and one local Unix datagram socket on which the simulation
 * sends the simulated pins and host messages and receives the status.
 *
 * Local socket protocol (one text datagram per message):
 *   pins <y32> <estop_contact> <reset> <charger_fault> <contactor_fb>
 *   pin <y32|estop_contact|reset|charger_fault|contactor_fb> <0|1>
 *   session <epoch>          hb <epoch>
 *   cmd <epoch> <seq> <v m/s> <w rad/s>
 *   sup <ui_stop|ui_resume|jetson_stop_release|charge_cmd|charge_cmd_removed|charger_ack> <epoch> <seq>
 *   lift <op> <stop>
 *   arms <lpose> <lmoving> <lfault> <rpose> <rmoving> <rfault> <bimanual>   (pose: STOWED CARRY OTHER UNKNOWN)
 *   subscribe                (status JSON is sent to this sender every SIMREF_STATUS_PERIOD_MS)
 */

#include <errno.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#include "simref/simref_ctrl.h"
#include "simref/simref_timing.h"

static volatile sig_atomic_t g_stop;
static void on_signal(int s) { (void)s; g_stop = 1; }

typedef struct {
    int can_fd[SIMREF_BUS_COUNT];
    unsigned long tx_errors;
} io_t;

static int64_t now_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

static void can_send(void *ctx, simref_bus_t bus, const simref_frame_t *f)
{
    io_t *io = ctx;
    struct can_frame cf;
    memset(&cf, 0, sizeof cf);
    cf.can_id = f->id & CAN_SFF_MASK;
    cf.can_dlc = f->len;
    memcpy(cf.data, f->data, f->len);
    if (write(io->can_fd[bus], &cf, sizeof cf) != (ssize_t)sizeof cf)
        io->tx_errors++;
}

static int open_can(const char *name)
{
    int fd = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (fd < 0)
        return -1;
    struct ifreq ifr;
    memset(&ifr, 0, sizeof ifr);
    snprintf(ifr.ifr_name, sizeof ifr.ifr_name, "%s", name);
    if (ioctl(fd, SIOCGIFINDEX, &ifr) < 0) {
        close(fd);
        return -1;
    }
    struct sockaddr_can addr = {.can_family = AF_CAN, .can_ifindex = ifr.ifr_ifindex};
    if (bind(fd, (struct sockaddr *)&addr, sizeof addr) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static simref_pose_t pose_of(const char *s)
{
    if (strcmp(s, "STOWED") == 0) return SIMREF_POSE_STOWED;
    if (strcmp(s, "CARRY") == 0) return SIMREF_POSE_CARRY;
    if (strcmp(s, "OTHER") == 0) return SIMREF_POSE_OTHER;
    return SIMREF_POSE_UNKNOWN;
}

static const char *const PIN_NAMES[SIMREF_PIN_COUNT] = {"y32", "estop_contact", "reset", "charger_fault", "contactor_fb"};
static const char *const SUP_NAMES[SIMREF_SUP_COUNT] = {"ui_stop", "ui_resume", "jetson_stop_release",
                                                        "charge_cmd", "charge_cmd_removed", "charger_ack"};

static int handle_line(simref_ctrl_t *c, char *line, int64_t t)
{
    char w[8][32];
    int n = sscanf(line, "%31s %31s %31s %31s %31s %31s %31s %31s", w[0], w[1], w[2], w[3], w[4], w[5], w[6], w[7]);
    if (n <= 0)
        return -1;
    if (strcmp(w[0], "pins") == 0 && n == 6) {
        for (int i = 0; i < SIMREF_PIN_COUNT; i++)
            simref_ctrl_set_pin(c, (simref_pin_t)i, atoi(w[1 + i]) != 0, t);
    } else if (strcmp(w[0], "pin") == 0 && n == 3) {
        for (int i = 0; i < SIMREF_PIN_COUNT; i++)
            if (strcmp(w[1], PIN_NAMES[i]) == 0)
                simref_ctrl_set_pin(c, (simref_pin_t)i, atoi(w[2]) != 0, t);
    } else if (strcmp(w[0], "session") == 0 && n == 2) {
        simref_ctrl_host_session(c, (uint32_t)strtoul(w[1], NULL, 10), t);
    } else if (strcmp(w[0], "hb") == 0 && n == 2) {
        simref_ctrl_host_heartbeat(c, (uint32_t)strtoul(w[1], NULL, 10), t);
    } else if (strcmp(w[0], "cmd") == 0 && n == 5) {
        simref_ctrl_host_cmd(c, (uint32_t)strtoul(w[1], NULL, 10), (uint32_t)strtoul(w[2], NULL, 10),
                             strtod(w[3], NULL), strtod(w[4], NULL), t);
    } else if (strcmp(w[0], "sup") == 0 && n == 4) {
        for (int i = 0; i < SIMREF_SUP_COUNT; i++)
            if (strcmp(w[1], SUP_NAMES[i]) == 0)
                simref_ctrl_host_supervisory(c, (simref_sup_t)i, (uint32_t)strtoul(w[2], NULL, 10),
                                             (uint32_t)strtoul(w[3], NULL, 10), t);
    } else if (strcmp(w[0], "lift") == 0 && n == 3) {
        simref_ctrl_host_lift(c, (uint8_t)atoi(w[1]), (uint8_t)atoi(w[2]), t);
    } else if (strcmp(w[0], "arms") == 0 && n == 8) {
        simref_arm_t l = {pose_of(w[1]), atoi(w[2]) != 0, atoi(w[3]) != 0};
        simref_arm_t r = {pose_of(w[4]), atoi(w[5]) != 0, atoi(w[6]) != 0};
        simref_ctrl_host_arms(c, &l, &r, pose_of(w[7]), t);
    } else if (strcmp(w[0], "subscribe") == 0) {
        return 1;
    } else {
        return -1;
    }
    return 0;
}

#define B(x) ((x) ? "true" : "false")

static int status_json(const simref_ctrl_t *c, const io_t *io, int64_t t, char *buf, size_t cap)
{
    const simref_sm_t *sm = &c->sm;
    const simref_drive_t *d = &c->drive;
    simref_sm_inputs_t in = simref_ctrl_inputs(c, t);
    int n = 0;
#define P(...) do { int k = snprintf(buf + n, cap - (size_t)n, __VA_ARGS__); if (k < 0 || (size_t)k >= cap - (size_t)n) return -1; n += k; } while (0)
    P("{\"simulation_only\":true,\"t_us\":%lld,\"uptime_ms\":%lld,\"boot_cause\":\"%s\",\"reboots\":%u,",
      (long long)t, (long long)((t - c->boot_us) / 1000),
      c->cfg.boot_cause == SIMREF_CAUSE_POWER_ON ? "POWER_ON" : c->cfg.boot_cause == SIMREF_CAUSE_WATCHDOG ? "WATCHDOG" : "SOFTWARE",
      c->reboots);
    P("\"state\":\"%s\",\"motor\":\"%s\",\"lamp\":\"%s\",\"charge_request\":\"%s\",\"motion_admissible\":%s,",
      simref_state_name(sm->state), sm->motor_follow ? "FOLLOW_CMD" : "ZERO", simref_sm_lamp_on(sm->state) ? "ON" : "OFF",
      simref_sm_charge_request_on(sm->state) ? "ON" : "OFF", B(simref_sm_motion_admissible(sm)));
    P("\"latches\":{\"reset_seen\":%s,\"ui_stop\":%s,\"jetson_stop\":%s,\"wcm_latched\":%s,\"interlock_latched\":%s},",
      B(sm->latches.reset_seen), B(sm->latches.ui_stop), B(sm->latches.jetson_stop), B(sm->latches.wcm_latched),
      B(sm->latches.interlock_latched));
    P("\"reason_mask\":[");
    for (unsigned i = 0; i < sm->n_mask; i++)
        P("%s\"%s\"", i ? "," : "", simref_reason_name((simref_reason_t)sm->reason_mask[i]));
    P("],\"first_reason\":%s%s%s,", sm->first_reason ? "\"" : "", sm->first_reason ? simref_reason_name(sm->first_reason) : "null",
      sm->first_reason ? "\"" : "");
    P("\"reason_log_n\":%u,\"reason_log\":[", c->log_n);
    uint32_t first = c->log_n > SIMREF_CTRL_LOG ? c->log_n - SIMREF_CTRL_LOG : 0;
    for (uint32_t i = first; i < c->log_n; i++) {
        const simref_reason_log_t *e = &c->log[i % SIMREF_CTRL_LOG];
        P("%s{\"n\":%u,\"t_us\":%lld,\"reason\":\"%s\"}", i > first ? "," : "", i, (long long)e->t_us,
          simref_reason_name(e->reason));
    }
    P("],\"spec_errors\":%u,\"input_detail\":\"%s\",", c->spec_errors,
      c->input_detail == SIMREF_INPUT_DETAIL_CONTACTOR_FEEDBACK ? "CONTACTOR_FEEDBACK" : "NONE");
    P("\"inputs\":{\"run_ok\":%s,\"base_stopped\":%s,\"arming_ok\":%s,\"qualified\":%s,\"charge_pre_ok\":%s,\"session_up\":%s},",
      B(in.run_ok), B(in.base_stopped), B(in.arming_ok), B(in.qualified), B(in.charge_pre_ok), B(c->session_up));
    P("\"pins\":{\"known\":%s", B(c->pins_known));
    for (int i = 0; i < SIMREF_PIN_COUNT; i++)
        P(",\"%s\":%d", PIN_NAMES[i], c->deb[i] ? 1 : 0);
    P("},\"drive\":{\"phase\":\"%s\",\"detail\":\"%s\",\"statusword\":%u,\"fault_code\":%u,\"last_abort\":%u,"
      "\"last_abort_index\":%u,\"enable_sequences\":%u,\"target_writes\":%u,\"epoch\":%u,\"hb_state\":%u,"
      "\"target_rpm\":[%d,%d],\"sent_rpm\":[%d,%d],\"actual_rpm\":[%.1f,%.1f]},",
      simref_dl_phase_name(d->phase), simref_dl_detail_name(d->detail), d->statusword, d->fault_code, d->last_abort,
      d->last_abort_index, d->enable_sequences, d->target_writes, d->epoch, d->hb_state, c->target_rpm[0],
      c->target_rpm[1], d->sent_rpm[0], d->sent_rpm[1], d->act_rpm10[0] / 10.0, d->act_rpm10[1] / 10.0);
    P("\"odom\":{\"valid\":%s,\"x\":%.6f,\"y\":%.6f,\"yaw\":%.6f,\"v\":%.6f,\"w\":%.6f,\"wheel_pos\":[%.6f,%.6f],"
      "\"wheel_rad_s\":[%.6f,%.6f]},",
      B(c->odom_valid), c->x, c->y, c->yaw, c->v, c->w, c->wheel_pos[0], c->wheel_pos[1], c->wheel_rad_s[0],
      c->wheel_rad_s[1]);
    P("\"permit\":{\"permit\":%s,\"reason\":\"%s\",\"seq\":%u,\"boot_id\":%u},", B(c->permit.permit),
      simref_permit_reason_name(c->permit.reason), c->permit_tx.seq, c->permit_tx.boot_id);
    P("\"lift\":{\"nmt\":%u,\"hb_seen\":%s,\"cmd_valid\":%s},", c->lift_nmt, B(c->lift_hb_seen), B(c->lift_cmd_valid));
    P("\"cmd\":{\"accepted\":%u,\"rejected\":%u,\"age_ms\":%lld},\"can_tx_errors\":%lu}", c->cmd_accepted,
      c->cmd_rejected, c->cmd_valid_seen ? (long long)((t - c->cmd_us) / 1000) : -1LL, io->tx_errors);
#undef P
    return n;
}

static void usage(void)
{
    fprintf(stderr, "usage: simref_process --can-drive <vcan..> --can-aux <vcan..> --sock <path>\n"
                    "       [--boot-cause POWER_ON|WATCHDOG|SOFTWARE] [--wheel-radius m] [--wheel-separation m]\n"
                    "       [--left-sign +-1] [--right-sign +-1]\n");
}

int main(int argc, char **argv)
{
    const char *can_drive = NULL, *can_aux = NULL, *sock_path = NULL;
    simref_ctrl_cfg_t cfg;
    simref_ctrl_default_cfg(&cfg);
    for (int i = 1; i < argc; i += 2) {
        const char *a = argv[i];
        const char *v = i + 1 < argc ? argv[i + 1] : NULL;
        if (v == NULL) { usage(); return 2; }
        if (strcmp(a, "--can-drive") == 0) can_drive = v;
        else if (strcmp(a, "--can-aux") == 0) can_aux = v;
        else if (strcmp(a, "--sock") == 0) sock_path = v;
        else if (strcmp(a, "--boot-cause") == 0)
            cfg.boot_cause = strcmp(v, "WATCHDOG") == 0 ? SIMREF_CAUSE_WATCHDOG
                             : strcmp(v, "SOFTWARE") == 0 ? SIMREF_CAUSE_SOFTWARE : SIMREF_CAUSE_POWER_ON;
        else if (strcmp(a, "--wheel-radius") == 0) cfg.wheel_radius_m = strtod(v, NULL);
        else if (strcmp(a, "--wheel-separation") == 0) cfg.wheel_separation_m = cfg.permit.track_m = strtod(v, NULL);
        else if (strcmp(a, "--left-sign") == 0) cfg.left_sign = atoi(v) < 0 ? -1 : 1;
        else if (strcmp(a, "--right-sign") == 0) cfg.right_sign = atoi(v) < 0 ? -1 : 1;
        else { usage(); return 2; }
    }
    if (!can_drive || !can_aux || !sock_path) {
        usage();
        return 2;
    }
    fprintf(stderr, "SIMULATION ONLY. Not production firmware. Not reviewed for safety.\n");
    /* No physical device is commanded: refuse every interface that is not vcan, before any
     * socket is opened. */
    const char *names[2] = {can_drive, can_aux};
    for (int i = 0; i < 2; i++)
        if (!simref_iface_allowed(names[i])) {
            fprintf(stderr, "simref: refused interface '%s': only interfaces whose name starts with 'vcan'\n", names[i]);
            return 3;
        }
    io_t io = {0};
    for (int i = 0; i < 2; i++) {
        io.can_fd[i] = open_can(names[i]);
        if (io.can_fd[i] < 0) {
            fprintf(stderr, "simref: cannot open %s: %s\n", names[i], strerror(errno));
            return 4;
        }
    }
    int us = socket(AF_UNIX, SOCK_DGRAM, 0);
    struct sockaddr_un sa = {.sun_family = AF_UNIX};
    snprintf(sa.sun_path, sizeof sa.sun_path, "%s", sock_path);
    unlink(sock_path);
    if (us < 0 || bind(us, (struct sockaddr *)&sa, sizeof sa) < 0) {
        fprintf(stderr, "simref: cannot bind %s: %s\n", sock_path, strerror(errno));
        return 4;
    }
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    static simref_ctrl_t ctrl;
    simref_ctrl_init(&ctrl, &cfg, now_us(), can_send, &io);
    struct sockaddr_un sub;
    socklen_t sub_len = 0;
    int64_t next_status = now_us();
    fprintf(stderr, "simref: running on %s and %s, socket %s\n", can_drive, can_aux, sock_path);

    while (!g_stop) {
        int64_t t = now_us();
        int64_t dl = simref_ctrl_next_deadline(&ctrl);
        if (next_status < dl)
            dl = next_status;
        int timeout_ms = dl <= t ? 0 : (int)((dl - t + 999) / 1000);
        if (timeout_ms > 10)
            timeout_ms = 10;
        struct pollfd pfd[3] = {{io.can_fd[0], POLLIN, 0}, {io.can_fd[1], POLLIN, 0}, {us, POLLIN, 0}};
        int r = poll(pfd, 3, timeout_ms);
        if (r < 0 && errno != EINTR)
            break;
        t = now_us();
        for (int b = 0; b < 2; b++) {
            if (!(pfd[b].revents & POLLIN))
                continue;
            struct can_frame cf;
            while (recv(io.can_fd[b], &cf, sizeof cf, MSG_DONTWAIT) == (ssize_t)sizeof cf) {
                if (cf.can_id & (CAN_EFF_FLAG | CAN_RTR_FLAG | CAN_ERR_FLAG))
                    continue;
                simref_frame_t f = {.id = cf.can_id & CAN_SFF_MASK, .len = cf.can_dlc > 8 ? 8 : cf.can_dlc};
                memcpy(f.data, cf.data, f.len);
                simref_ctrl_on_frame(&ctrl, (simref_bus_t)b, &f, t);
                simref_ctrl_step(&ctrl, t);
            }
        }
        if (pfd[2].revents & POLLIN) {
            char buf[256];
            struct sockaddr_un from;
            socklen_t fl = sizeof from;
            ssize_t k;
            while ((k = recvfrom(us, buf, sizeof buf - 1, MSG_DONTWAIT, (struct sockaddr *)&from, &fl)) > 0) {
                buf[k] = 0;
                if (handle_line(&ctrl, buf, t) == 1 && fl > sizeof(sa_family_t)) {
                    sub = from;
                    sub_len = fl;
                }
                fl = sizeof from;
            }
        }
        simref_ctrl_step(&ctrl, t);
        if (t >= next_status) {
            next_status = t + (int64_t)SIMREF_STATUS_PERIOD_MS * 1000;
            if (sub_len) {
                static char js[8192];
                int n = status_json(&ctrl, &io, t, js, sizeof js);
                if (n > 0)
                    sendto(us, js, (size_t)n, MSG_DONTWAIT, (struct sockaddr *)&sub, sub_len);
            }
        }
    }
    /* on exit: disable the drive (shutdown word), so a stopped reference leaves it disabled */
    simref_drive_disable(&ctrl.drive, now_us());
    unlink(sock_path);
    return 0;
}
