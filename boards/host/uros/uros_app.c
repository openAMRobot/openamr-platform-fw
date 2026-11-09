/* SPDX-License-Identifier: MIT */
/*
 * micro-ROS application layer. See uros_app.h.
 * Pattern: ping the agent, create the entities, spin; on repeated ping
 * failures destroy and go back to waiting (micro-ROS reconnection example).
 * The control tick never depends on this file: when the agent is gone the
 * host heartbeat simply stops and the core stops the base.
 */
#include "uros_app.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include <rcl/rcl.h>
#include <rclc/executor.h>
#include <rclc/rclc.h>
#include <rmw_microros/rmw_microros.h>

#include <diagnostic_msgs/msg/diagnostic_status.h>
#include <diagnostic_msgs/msg/key_value.h>
#include <geometry_msgs/msg/twist.h>
#include <nav_msgs/msg/odometry.h>
#include <sensor_msgs/msg/imu.h>

#include "uros_status.h"

static oamr_uros_config_t s_cfg;
static oamr_transport_t s_tr;
static oamr_clock_t s_clk;
static oamr_uros_hooks_t s_hooks;
static oamr_uros_counters_t s_cnt;
static oamr_uros_state_t s_state = OAMR_UROS_WAITING_AGENT;

static rcl_allocator_t s_alloc;
static rclc_support_t s_support;
static rcl_node_t s_node;
static rcl_subscription_t s_sub_cmd;
static rcl_publisher_t s_pub_status;
static rcl_publisher_t s_pub_aux;
static rcl_publisher_t s_pub_odom;
static rcl_publisher_t s_pub_imu;
static rclc_executor_t s_exec;
static geometry_msgs__msg__Twist s_cmd_msg;
static nav_msgs__msg__Odometry s_odom_msg;
static sensor_msgs__msg__Imu s_imu_msg;
static oamr_uros_status_msgs_t s_status_msgs;

static uint64_t s_last_ping_us;
static uint64_t s_last_status_us;
static uint64_t s_last_aux_us;
static uint64_t s_last_odom_us;
static uint8_t s_ping_misses;
static oamr_app_status_t s_snap;
static oamr_uros_change_key_t s_last_key;
static bool s_have_key;
static uint32_t s_seq;
static uint32_t s_aux_seq;

/* Return codes the layer deliberately does not act on (teardown of a dead
 * session, best-effort publishes): the next ping decides. */
static void ignore_ret(rcl_ret_t r)
{
    (void)r;
}

/* --- custom transport over oamr_transport_t --------------------------- */

static bool tr_open(struct uxrCustomTransport *t)
{
    (void)t;
    return s_tr.open == NULL || s_tr.open(s_tr.ctx);
}

static bool tr_close(struct uxrCustomTransport *t)
{
    (void)t;
    return s_tr.close == NULL || s_tr.close(s_tr.ctx);
}

static size_t tr_write(struct uxrCustomTransport *t, const uint8_t *buf, size_t len, uint8_t *err)
{
    (void)t;
    const size_t n = s_tr.write(s_tr.ctx, buf, len);
    if (n != len) {
        *err = 1;
    }
    return n;
}

static size_t tr_read(struct uxrCustomTransport *t, uint8_t *buf, size_t len, int timeout,
                      uint8_t *err)
{
    (void)t;
    (void)err;
    return s_tr.read(s_tr.ctx, buf, len, timeout > 0 ? (uint32_t)timeout : 0u);
}

void oamr_uros_default_config(oamr_uros_config_t *cfg)
{
    cfg->status_period_ms = 100u;
    cfg->aux_period_ms = 200u;
    cfg->odom_period_ms = 20u;  /* TODO(Evans) */
    cfg->ping_period_ms = 100u;
    cfg->ping_timeout_ms = 50u;
    cfg->ping_misses_to_drop = 3u;
    cfg->hardware_id = "openamr_base_controller";
}

void oamr_uros_init(const oamr_uros_config_t *cfg, const oamr_transport_t *transport,
                    const oamr_clock_t *clock, const oamr_uros_hooks_t *hooks)
{
    s_cfg = *cfg;
    s_tr = *transport;
    s_clk = *clock;
    s_hooks = *hooks;
    memset(&s_cnt, 0, sizeof(s_cnt));
    s_state = OAMR_UROS_WAITING_AGENT;
    rmw_uros_set_custom_transport(true, NULL, tr_open, tr_close, tr_write, tr_read);
    oamr_uros_status_msgs_init(&s_status_msgs, cfg->hardware_id);
}

const oamr_uros_counters_t *oamr_uros_counters(void)
{
    return &s_cnt;
}

/* --- entities ----------------------------------------------------------- */

static void on_cmd_vel(const void *msg)
{
    const geometry_msgs__msg__Twist *t = (const geometry_msgs__msg__Twist *)msg;
    s_cnt.cmd_received++;
    s_hooks.on_cmd_vel(s_hooks.ctx, (float)t->linear.x, (float)t->angular.z);
}

static rmw_qos_profile_t status_qos(void)
{
    rmw_qos_profile_t q = rmw_qos_profile_default;
    q.reliability = RMW_QOS_POLICY_RELIABILITY_RELIABLE;
    q.durability = RMW_QOS_POLICY_DURABILITY_TRANSIENT_LOCAL;
    q.history = RMW_QOS_POLICY_HISTORY_KEEP_LAST;
    q.depth = 1;
    return q;
}

static bool create_entities(void)
{
    s_alloc = rcl_get_default_allocator();
    if (rclc_support_init(&s_support, 0, NULL, &s_alloc) != RCL_RET_OK) {
        return false;
    }
    if (rclc_node_init_default(&s_node, "base_controller", "", &s_support) != RCL_RET_OK) {
        return false;
    }
    const rmw_qos_profile_t sq = status_qos();
    bool ok = true;
    ok = ok && rclc_subscription_init_best_effort(
                   &s_sub_cmd, &s_node, ROSIDL_GET_MSG_TYPE_SUPPORT(geometry_msgs, msg, Twist),
                   "cmd_vel") == RCL_RET_OK;
    ok = ok && rclc_publisher_init(
                   &s_pub_status, &s_node,
                   ROSIDL_GET_MSG_TYPE_SUPPORT(diagnostic_msgs, msg, DiagnosticStatus),
                   "base/status", &sq) == RCL_RET_OK;
    ok = ok && rclc_publisher_init(
                   &s_pub_aux, &s_node,
                   ROSIDL_GET_MSG_TYPE_SUPPORT(diagnostic_msgs, msg, DiagnosticStatus),
                   "base/aux_status", &sq) == RCL_RET_OK;
    /* Odometry (~720 B serialized) is larger than the 512 B XRCE-DDS MTU and
     * only reliable streams fragment, so it uses a reliable writer.
     * TODO(Evans): on the STM32, either keep it reliable or raise the MTU. */
    ok = ok && rclc_publisher_init_default(
                   &s_pub_odom, &s_node, ROSIDL_GET_MSG_TYPE_SUPPORT(nav_msgs, msg, Odometry),
                   "odom/unfiltered") == RCL_RET_OK;
    ok = ok && rclc_publisher_init_best_effort(
                   &s_pub_imu, &s_node, ROSIDL_GET_MSG_TYPE_SUPPORT(sensor_msgs, msg, Imu),
                   "imu/data_raw") == RCL_RET_OK;
    if (!ok) {
        return false;
    }
    s_exec = rclc_executor_get_zero_initialized_executor();
    if (rclc_executor_init(&s_exec, &s_support.context, 1, &s_alloc) != RCL_RET_OK) {
        return false;
    }
    if (rclc_executor_add_subscription(&s_exec, &s_sub_cmd, &s_cmd_msg, on_cmd_vel, ON_NEW_DATA) !=
        RCL_RET_OK) {
        return false;
    }
    /* Clock sync: header stamps in ROS time when available (diagnostic only,
     * I8 3.1); the monotonic stamp is carried in the status fields. */
    (void)rmw_uros_sync_session(100);
    return true;
}

static void destroy_entities(void)
{
    rmw_context_t *rmw = rcl_context_get_rmw_context(&s_support.context);
    (void)rmw_uros_set_context_entity_destroy_session_timeout(rmw, 0);
    ignore_ret(rcl_subscription_fini(&s_sub_cmd, &s_node));
    ignore_ret(rcl_publisher_fini(&s_pub_status, &s_node));
    ignore_ret(rcl_publisher_fini(&s_pub_aux, &s_node));
    ignore_ret(rcl_publisher_fini(&s_pub_odom, &s_node));
    ignore_ret(rcl_publisher_fini(&s_pub_imu, &s_node));
    (void)rclc_executor_fini(&s_exec);
    ignore_ret(rcl_node_fini(&s_node));
    (void)rclc_support_fini(&s_support);
}

/* --- publishing --------------------------------------------------------- */

static void stamp(builtin_interfaces__msg__Time *t, uint64_t mono_us)
{
    int64_t ns;
    if (rmw_uros_epoch_synchronized()) {
        /* Shift the monotonic sample time onto the agent's clock. */
        const uint64_t now_us = oamr_clock_now_us(&s_clk);
        const uint64_t age_us = now_us >= mono_us ? now_us - mono_us : 0u;
        ns = rmw_uros_epoch_nanos() - (int64_t)age_us * 1000;
    } else {
        ns = (int64_t)mono_us * 1000;
    }
    t->sec = (int32_t)(ns / 1000000000LL);
    t->nanosec = (uint32_t)(ns % 1000000000LL);
}

static void set_str(rosidl_runtime_c__String *s, char *buf, size_t cap, const char *v)
{
    snprintf(buf, cap, "%s", v);
    s->data = buf;
    s->size = strlen(buf);
    s->capacity = cap;
}

static void publish_status(uint64_t now_us, bool force)
{
    s_hooks.snapshot(s_hooks.ctx, &s_snap);
    const oamr_uros_change_key_t key = oamr_uros_change_key(&s_snap);
    const bool changed = !s_have_key || memcmp(&key, &s_last_key, sizeof(key)) != 0;
    const bool due = now_us - s_last_status_us >= (uint64_t)s_cfg.status_period_ms * 1000u;
    if (!force && !due && !changed) {
        return;
    }
    s_last_key = key;
    s_have_key = true;
    s_last_status_us = now_us;
    oamr_uros_fill_status(&s_status_msgs, &s_snap, ++s_seq);
    if (rcl_publish(&s_pub_status, &s_status_msgs.status, NULL) == RCL_RET_OK) {
        s_cnt.status_published++;
    }
}

static void publish_aux(uint64_t now_us)
{
    if (now_us - s_last_aux_us < (uint64_t)s_cfg.aux_period_ms * 1000u) {
        return;
    }
    s_last_aux_us = now_us;
    oamr_uros_fill_aux(&s_status_msgs, &s_snap, ++s_aux_seq);
    ignore_ret(rcl_publish(&s_pub_aux, &s_status_msgs.aux, NULL));
}

static void publish_odom(uint64_t now_us)
{
    if (now_us - s_last_odom_us < (uint64_t)s_cfg.odom_period_ms * 1000u || !s_snap.odom.valid) {
        return;
    }
    s_last_odom_us = now_us;
    static char frame[16];
    static char child[24];
    nav_msgs__msg__Odometry *o = &s_odom_msg;
    stamp(&o->header.stamp, s_snap.odom.stamp_us);
    set_str(&o->header.frame_id, frame, sizeof(frame), "odom");
    set_str(&o->child_frame_id, child, sizeof(child), "base_footprint");
    o->pose.pose.position.x = s_snap.odom.x_m;
    o->pose.pose.position.y = s_snap.odom.y_m;
    o->pose.pose.position.z = 0.0;
    o->pose.pose.orientation.x = 0.0;
    o->pose.pose.orientation.y = 0.0;
    o->pose.pose.orientation.z = sin(s_snap.odom.yaw_rad * 0.5);
    o->pose.pose.orientation.w = cos(s_snap.odom.yaw_rad * 0.5);
    o->twist.twist.linear.x = s_snap.odom.linear_m_s;
    o->twist.twist.angular.z = s_snap.odom.angular_rad_s;
    /* TODO(Evans): wheel odometry covariances from the 2.0 bench (EKF input). */
    memset(o->pose.covariance, 0, sizeof(o->pose.covariance));
    memset(o->twist.covariance, 0, sizeof(o->twist.covariance));
    ignore_ret(rcl_publish(&s_pub_odom, o, NULL));
}

static void publish_imu(void)
{
    oamr_imu_sample_t smp;
    while (s_hooks.take_imu(s_hooks.ctx, &smp)) {
        if (!smp.valid) {
            continue;  /* never published as data (I8 5.6) */
        }
        static char frame[16];
        sensor_msgs__msg__Imu *m = &s_imu_msg;
        stamp(&m->header.stamp, smp.stamp_us);
        set_str(&m->header.frame_id, frame, sizeof(frame), "imu_link");
        m->orientation_covariance[0] = -1.0;  /* no orientation estimate */
        m->angular_velocity.x = smp.gyro_rad_s[0];
        m->angular_velocity.y = smp.gyro_rad_s[1];
        m->angular_velocity.z = smp.gyro_rad_s[2];
        m->linear_acceleration.x = smp.accel_m_s2[0];
        m->linear_acceleration.y = smp.accel_m_s2[1];
        m->linear_acceleration.z = smp.accel_m_s2[2];
        /* TODO(Evans): real gyro/accel covariance from the bench (I8 5.6). */
        ignore_ret(rcl_publish(&s_pub_imu, m, NULL));
    }
}

/* --- spin ----------------------------------------------------------------- */

oamr_uros_state_t oamr_uros_spin_once(uint32_t timeout_ms)
{
    const uint64_t now_us = oamr_clock_now_us(&s_clk);
    const bool ping_due = now_us - s_last_ping_us >= (uint64_t)s_cfg.ping_period_ms * 1000u;

    if (s_state == OAMR_UROS_WAITING_AGENT) {
        if (!ping_due) {
            return s_state;
        }
        s_last_ping_us = now_us;
        if (rmw_uros_ping_agent((int)s_cfg.ping_timeout_ms, 1) != RMW_RET_OK) {
            return s_state;
        }
        if (!create_entities()) {
            destroy_entities();
            return s_state;
        }
        s_cnt.sessions++;
        s_ping_misses = 0;
        s_have_key = false;
        s_state = OAMR_UROS_CONNECTED;
        s_hooks.on_host_alive(s_hooks.ctx);
        publish_status(now_us, true);
        return s_state;
    }

    if (ping_due) {
        s_last_ping_us = now_us;
        if (rmw_uros_ping_agent((int)s_cfg.ping_timeout_ms, 1) == RMW_RET_OK) {
            s_ping_misses = 0;
            s_hooks.on_host_alive(s_hooks.ctx);
        } else if (++s_ping_misses >= s_cfg.ping_misses_to_drop) {
            destroy_entities();
            s_cnt.drops++;
            s_state = OAMR_UROS_WAITING_AGENT;
            return s_state;
        }
    }
    (void)rclc_executor_spin_some(&s_exec, (uint64_t)timeout_ms * 1000000u);
    const uint64_t t = oamr_clock_now_us(&s_clk);
    publish_status(t, false);
    publish_aux(t);
    publish_odom(t);
    publish_imu();
    return s_state;
}
