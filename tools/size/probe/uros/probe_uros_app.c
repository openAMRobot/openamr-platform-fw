/* SPDX-License-Identifier: MIT */
/*
 * Size probe, NOT FIRMWARE. Links the micro-ROS client with an entity count
 * close to the I8 host link so that the linker keeps what a real build keeps.
 * Message types are stand-ins from the standard set: the I8 messages
 * (interfaces #38) are not in the library yet. Nothing here commands motion;
 * subscription callbacks only count messages.
 *
 * Entities (inside the default colcon.meta caps of 1 node, 10 publishers,
 * 5 subscriptions): 10 publishers (status, aux status, IMU, IMU
 * temperature, 6 ranges for 4 MB7060 and 2 TF-Luna), 2 subscriptions
 * (stand-ins for the two inbound I8 topics), 1 timer, 1 executor.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <rcl/rcl.h>
#include <rclc/executor.h>
#include <rclc/rclc.h>
#include <rmw_microros/rmw_microros.h>

#include <geometry_msgs/msg/twist.h>
#include <sensor_msgs/msg/imu.h>
#include <sensor_msgs/msg/range.h>
#include <sensor_msgs/msg/temperature.h>
#include <std_msgs/msg/u_int32.h>
#include <std_msgs/msg/u_int8_multi_array.h>

#include "probe_transport.h"

#define PROBE_RANGE_PUBS 6

static rcl_publisher_t s_pub_status;
static rcl_publisher_t s_pub_aux;
static rcl_publisher_t s_pub_imu;
static rcl_publisher_t s_pub_temp;
static rcl_publisher_t s_pub_range[PROBE_RANGE_PUBS];
static rcl_subscription_t s_sub_in0;
static rcl_subscription_t s_sub_in1;
static geometry_msgs__msg__Twist s_in0_msg;
static std_msgs__msg__UInt32 s_in1_msg;
static sensor_msgs__msg__Imu s_imu_msg;
static volatile uint32_t s_rx_count;

static void on_rx0(const void *msg) { (void)msg; s_rx_count++; }
static void on_rx1(const void *msg) { (void)msg; s_rx_count++; }

static void on_timer(rcl_timer_t *t, int64_t last)
{
    (void)t; (void)last;
    (void)rcl_publish(&s_pub_imu, &s_imu_msg, NULL);
}

int probe_uros_session(void)
{
    rmw_uros_set_custom_transport(PROBE_TRANSPORT_FRAMING, NULL, probe_transport_open,
                                  probe_transport_close, probe_transport_write, probe_transport_read);
    probe_set_allocator();

    if (rmw_uros_ping_agent(100, 1) != RMW_RET_OK) {
        return -1;
    }
    rcl_allocator_t allocator = rcl_get_default_allocator();
    rclc_support_t support;
    if (rclc_support_init(&support, 0, NULL, &allocator) != RCL_RET_OK) {
        return -1;
    }
    (void)rmw_uros_sync_session(100);
    rcl_node_t node;
    (void)rclc_node_init_default(&node, "base_controller", "", &support);
    (void)rclc_publisher_init_best_effort(&s_pub_status, &node,
        ROSIDL_GET_MSG_TYPE_SUPPORT(std_msgs, msg, UInt8MultiArray), "base/status");
    (void)rclc_publisher_init_best_effort(&s_pub_aux, &node,
        ROSIDL_GET_MSG_TYPE_SUPPORT(std_msgs, msg, UInt8MultiArray), "base/aux_status");
    (void)rclc_publisher_init_best_effort(&s_pub_imu, &node,
        ROSIDL_GET_MSG_TYPE_SUPPORT(sensor_msgs, msg, Imu), "imu/data_raw");
    (void)rclc_publisher_init_best_effort(&s_pub_temp, &node,
        ROSIDL_GET_MSG_TYPE_SUPPORT(sensor_msgs, msg, Temperature), "imu/temperature");
    for (int i = 0; i < PROBE_RANGE_PUBS; i++) {
        (void)rclc_publisher_init_best_effort(&s_pub_range[i], &node,
            ROSIDL_GET_MSG_TYPE_SUPPORT(sensor_msgs, msg, Range), "range");
    }
    (void)rclc_subscription_init_best_effort(&s_sub_in0, &node,
        ROSIDL_GET_MSG_TYPE_SUPPORT(geometry_msgs, msg, Twist), "probe/in0");
    (void)rclc_subscription_init_default(&s_sub_in1, &node,
        ROSIDL_GET_MSG_TYPE_SUPPORT(std_msgs, msg, UInt32), "probe/in1");
    rcl_timer_t timer;
    (void)rclc_timer_init_default2(&timer, &support, RCL_MS_TO_NS(5), on_timer, true);
    rclc_executor_t exec = rclc_executor_get_zero_initialized_executor();
    (void)rclc_executor_init(&exec, &support.context, 3, &allocator);
    (void)rclc_executor_add_subscription(&exec, &s_sub_in0, &s_in0_msg, on_rx0, ON_NEW_DATA);
    (void)rclc_executor_add_subscription(&exec, &s_sub_in1, &s_in1_msg, on_rx1, ON_NEW_DATA);
    (void)rclc_executor_add_timer(&exec, &timer);

    while (rmw_uros_ping_agent(50, 1) == RMW_RET_OK && rmw_uros_epoch_synchronized()) {
        (void)rclc_executor_spin_some(&exec, RCL_MS_TO_NS(1));
        probe_idle();
    }
    (void)rclc_executor_fini(&exec);
    (void)rcl_timer_fini(&timer);
    (void)rcl_subscription_fini(&s_sub_in0, &node);
    (void)rcl_subscription_fini(&s_sub_in1, &node);
    (void)rcl_publisher_fini(&s_pub_status, &node);
    (void)rcl_publisher_fini(&s_pub_aux, &node);
    (void)rcl_publisher_fini(&s_pub_imu, &node);
    (void)rcl_publisher_fini(&s_pub_temp, &node);
    for (int i = 0; i < PROBE_RANGE_PUBS; i++) {
        (void)rcl_publisher_fini(&s_pub_range[i], &node);
    }
    (void)rcl_node_fini(&node);
    (void)rclc_support_fini(&support);
    return 0;
}
