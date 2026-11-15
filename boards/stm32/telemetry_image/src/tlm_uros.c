/* SPDX-License-Identifier: MIT */
/*
 * micro-ROS client of the TelemetryUrosSerialSimBench preset (TLM_UROS).
 *
 * Read-only: five best-effort publishers, no subscription, no service, no
 * parameter server, no executor. Every value comes from the same snapshot
 * the JSON console prints (tlm_report.c); nothing here reads a bus or a
 * sensor itself, and nothing here commands anything.
 *
 * It runs in the reporter task, the lowest application priority
 * (TLM_PRIO_REPORTER, below the CAN and sensor tasks), once per report
 * period, and follows the connection state machine of tlm_uros_sm.h: wait for
 * the agent, create the entities, publish, destroy them when the link is lost,
 * retry. Memory: the static arena of tlm_uros_alloc.h, reset after every
 * teardown; the micro-ROS library is built with static pools
 * (uros/colcon.meta). The link is the one transport of tlm_uros_transport.h.
 */
#include <math.h>
#include <string.h>
#include <time.h>

#include <rcl/rcl.h>
#include <rclc/rclc.h>
#include <rcutils/allocator.h>
#include <rmw_microros/rmw_microros.h>
#include <sensor_msgs/msg/range.h>
#include <std_msgs/msg/float32_multi_array.h>
#include <std_msgs/msg/string.h>
#include <uxr/client/profile/transport/custom/custom_transport.h>

#include "FreeRTOS.h"
#include "task.h"

#include "tlm.h"
#include "tlm_build_info.h"
#include "tlm_uros.h"
#include "tlm_uros_alloc.h"
#include "tlm_uros_msg.h"
#include "tlm_uros_sm.h"
#include "tlm_uros_transport.h"

/* ---- static memory ---- */
static uint8_t s_arena_mem[TLM_UROS_ARENA_BYTES] __attribute__((aligned(8)));
static tlm_arena_t s_arena;

static void *a_alloc(size_t size, void *state)
{
    (void)state;
    return tlm_arena_alloc(&s_arena, size);
}
static void a_free(void *p, void *state)
{
    (void)state;
    tlm_arena_free(&s_arena, p);
}
static void *a_realloc(void *p, size_t size, void *state)
{
    (void)state;
    return tlm_arena_realloc(&s_arena, p, size);
}
static void *a_zalloc(size_t n, size_t size, void *state)
{
    (void)state;
    return tlm_arena_zalloc(&s_arena, n, size);
}

/* The heap is not linked: every C-library allocation entry is wrapped to a
 * fatal stop (CMakeLists.txt, -Wl,--wrap). micro-ROS uses the arena above. */
void *__wrap_malloc(size_t n);
void *__wrap_calloc(size_t n, size_t s);
void *__wrap_realloc(void *p, size_t n);
void __wrap_free(void *p);
void *__wrap__malloc_r(void *r, size_t n);
void *__wrap__calloc_r(void *r, size_t n, size_t s);
void *__wrap__realloc_r(void *r, void *p, size_t n);
void __wrap__free_r(void *r, void *p);
void *__wrap_malloc(size_t n)
{
    (void)n;
    tlm_fatal("heap_malloc");
    return 0;
}
void *__wrap_calloc(size_t n, size_t s)
{
    (void)n;
    (void)s;
    tlm_fatal("heap_calloc");
    return 0;
}
void *__wrap_realloc(void *p, size_t n)
{
    (void)p;
    (void)n;
    tlm_fatal("heap_realloc");
    return 0;
}
void __wrap_free(void *p)
{
    if (p != 0)
    {
        tlm_fatal("heap_free");
    }
}
void *__wrap__malloc_r(void *r, size_t n)
{
    (void)r;
    return __wrap_malloc(n);
}
void *__wrap__calloc_r(void *r, size_t n, size_t s)
{
    (void)r;
    return __wrap_calloc(n, s);
}
void *__wrap__realloc_r(void *r, void *p, size_t n)
{
    (void)r;
    return __wrap_realloc(p, n);
}
void __wrap__free_r(void *r, void *p)
{
    (void)r;
    __wrap_free(p);
}

/* rand() and srand() of newlib-nano allocate their state from the heap on
 * first use; rmw_microxrcedds draws the XRCE client key from rand(). These
 * replace them (the C library's members are then not linked): xorshift32,
 * seeded in tlm_uros_init() from the boot id and the clock, so a reconnect
 * after a reset does not reuse the previous session's key. Not for anything
 * that needs unpredictability. */
static uint32_t s_rand = 0x9E3779B9u;
int rand(void);
void srand(unsigned seed);
int rand(void)
{
    s_rand ^= s_rand << 13;
    s_rand ^= s_rand >> 17;
    s_rand ^= s_rand << 5;
    return (int)(s_rand & 0x7FFFFFFFu);
}
void srand(unsigned seed)
{
    s_rand = seed != 0u ? (uint32_t)seed : 0x9E3779B9u;
}

/* Micro XRCE-DDS client time base: the image clock (tlm_time.c). */
int clock_gettime(clockid_t clock_id, struct timespec *tp);
int clock_gettime(clockid_t clock_id, struct timespec *tp)
{
    (void)clock_id;
    const uint64_t us = tlm_time_us();
    tp->tv_sec = (time_t)(us / 1000000u);
    tp->tv_nsec = (long)((us % 1000000u) * 1000u);
    return 0;
}

/* ---- transport adapter: uxrCustomTransport -> tlm_uros_transport_t ---- */
static bool t_open(struct uxrCustomTransport *t)
{
    (void)t;
    return tlm_uros_transport()->open();
}
static bool t_close(struct uxrCustomTransport *t)
{
    (void)t;
    return tlm_uros_transport()->close();
}
static size_t t_write(struct uxrCustomTransport *t, const uint8_t *b, size_t n, uint8_t *err)
{
    (void)t;
    return tlm_uros_transport()->write(b, n, err);
}
static size_t t_read(struct uxrCustomTransport *t, uint8_t *b, size_t n, int timeout, uint8_t *err)
{
    (void)t;
    return tlm_uros_transport()->read(b, n, timeout, err);
}

/* ---- entities and messages ---- */
enum
{
    PUB_WHEEL = 0,
    PUB_HB,
    PUB_TOF_FRONT,
    PUB_TOF_REAR,
    PUB_STATUS,
    PUB_N
};
static rclc_support_t s_support;
static rcl_node_t s_node;
static rcl_publisher_t s_pub[PUB_N];
static bool s_pub_ok[PUB_N];
static bool s_node_ok, s_support_ok;
static tlm_uros_sm_t s_sm;
static uint32_t s_fini_errors; /* teardown calls that reported an error (link already gone) */
static rcl_allocator_t s_alloc;

static std_msgs__msg__Float32MultiArray s_wheel_msg;
static float s_wheel_data[2];
static std_msgs__msg__MultiArrayDimension s_wheel_dim;
static char s_wheel_label[] = "ch1_ch2_drive_units";
static std_msgs__msg__String s_hb_msg;
static char s_hb_text[16];
static sensor_msgs__msg__Range s_tof_msg[TLM_N_TFLUNA];
static char s_tof_frame[TLM_N_TFLUNA][12] = {"tof_front", "tof_rear"};
static std_msgs__msg__String s_status_msg;
static tlm_uros_values_t s_values;

static void messages_init(void)
{
    s_wheel_dim.label.data = s_wheel_label;
    s_wheel_dim.label.size = strlen(s_wheel_label);
    s_wheel_dim.label.capacity = sizeof(s_wheel_label);
    s_wheel_dim.size = 2u;
    s_wheel_dim.stride = 2u;
    s_wheel_msg.layout.dim.data = &s_wheel_dim;
    s_wheel_msg.layout.dim.size = 1u;
    s_wheel_msg.layout.dim.capacity = 1u;
    s_wheel_msg.layout.data_offset = 0u;
    s_wheel_msg.data.data = s_wheel_data;
    s_wheel_msg.data.size = 2u;
    s_wheel_msg.data.capacity = 2u;
    s_hb_msg.data.data = s_hb_text;
    s_hb_msg.data.capacity = sizeof(s_hb_text);
    for (unsigned i = 0u; i < TLM_N_TFLUNA; i++)
    {
        sensor_msgs__msg__Range *r = &s_tof_msg[i];
        r->header.frame_id.data = s_tof_frame[i];
        r->header.frame_id.size = strlen(s_tof_frame[i]);
        r->header.frame_id.capacity = sizeof(s_tof_frame[i]);
        r->radiation_type = sensor_msgs__msg__Range__INFRARED;
        r->field_of_view = TLM_UROS_TFLUNA_FOV_RAD;
        r->min_range = TLM_UROS_TFLUNA_MIN_M;
        r->max_range = TLM_UROS_TFLUNA_MAX_M;
    }
    s_status_msg.data.data = s_values.status;
    s_status_msg.data.capacity = sizeof(s_values.status);
}

static void destroy_entities(void)
{
    if (s_support_ok)
    {
        rmw_context_t *ctx = rcl_context_get_rmw_context(&s_support.context);
        (void)rmw_uros_set_context_entity_destroy_session_timeout(ctx, 0);
    }
    for (unsigned i = 0u; i < PUB_N; i++)
    {
        if (s_pub_ok[i])
        {
            if (rcl_publisher_fini(&s_pub[i], &s_node) != RCL_RET_OK)
            {
                s_fini_errors++;
            }
            s_pub_ok[i] = false;
        }
    }
    if (s_node_ok)
    {
        if (rcl_node_fini(&s_node) != RCL_RET_OK)
        {
            s_fini_errors++;
        }
        s_node_ok = false;
    }
    if (s_support_ok)
    {
        if (rclc_support_fini(&s_support) != RCL_RET_OK)
        {
            s_fini_errors++;
        }
        s_support_ok = false;
    }
    tlm_arena_reset(&s_arena);
}

static bool create_entities(void)
{
    if (rclc_support_init(&s_support, 0, NULL, &s_alloc) != RCL_RET_OK)
    {
        return false;
    }
    s_support_ok = true;
    s_node = rcl_get_zero_initialized_node();
    if (rclc_node_init_default(&s_node, TLM_UROS_NODE_NAME, TLM_UROS_NAMESPACE, &s_support) != RCL_RET_OK)
    {
        return false;
    }
    s_node_ok = true;
    const struct
    {
        const rosidl_message_type_support_t *ts;
        const char *topic;
    } p[PUB_N] = {
        [PUB_WHEEL] = {ROSIDL_GET_MSG_TYPE_SUPPORT(std_msgs, msg, Float32MultiArray), "wheel_speeds"},
        [PUB_HB] = {ROSIDL_GET_MSG_TYPE_SUPPORT(std_msgs, msg, String), "drive_heartbeat"},
        [PUB_TOF_FRONT] = {ROSIDL_GET_MSG_TYPE_SUPPORT(sensor_msgs, msg, Range), "tof/front"},
        [PUB_TOF_REAR] = {ROSIDL_GET_MSG_TYPE_SUPPORT(sensor_msgs, msg, Range), "tof/rear"},
        [PUB_STATUS] = {ROSIDL_GET_MSG_TYPE_SUPPORT(std_msgs, msg, String), "status"},
    };
    for (unsigned i = 0u; i < PUB_N; i++)
    {
        s_pub[i] = rcl_get_zero_initialized_publisher();
        if (rclc_publisher_init_best_effort(&s_pub[i], &s_node, p[i].ts, p[i].topic) != RCL_RET_OK)
        {
            return false;
        }
        s_pub_ok[i] = true;
    }
    return true;
}

static void publish(const tlm_status_t *status, uint32_t sens_missed)
{
    const tlm_uros_meta_t meta = {
        .commit = TLM_GIT_COMMIT,
        .client_state = tlm_uros_state_name(s_sm.state),
        .sessions = s_sm.sessions,
        .losses = s_sm.losses,
        .sens_missed = sens_missed,
        .arena_high_water = (uint32_t)s_arena.high_water,
        .stack_free_words = (uint32_t)uxTaskGetStackHighWaterMark(NULL),
    };
    tlm_uros_values(status, &meta, &s_values);
    uint32_t ok = 0u, failed = 0u;
#define PUB(i, msg) ((rcl_publish(&s_pub[i], (msg), NULL) == RCL_RET_OK) ? ok++ : failed++)
    s_wheel_data[0] = s_values.wheel[0];
    s_wheel_data[1] = s_values.wheel[1];
    (void)PUB(PUB_WHEEL, &s_wheel_msg);
    const size_t hb_len = strlen(s_values.drive_hb);
    if (hb_len < sizeof(s_hb_text))
    {
        memcpy(s_hb_text, s_values.drive_hb, hb_len + 1u);
        s_hb_msg.data.size = hb_len;
        (void)PUB(PUB_HB, &s_hb_msg);
    }
    for (unsigned i = 0u; i < TLM_N_TFLUNA; i++)
    {
        sensor_msgs__msg__Range *r = &s_tof_msg[i];
        r->header.stamp.sec = (int32_t)s_values.stamp_sec;
        r->header.stamp.nanosec = s_values.stamp_nanosec;
        r->range = s_values.range_m[i];
        (void)PUB(PUB_TOF_FRONT + i, r);
    }
    if (s_values.status_len > 0u)
    {
        s_status_msg.data.size = s_values.status_len;
        (void)PUB(PUB_STATUS, &s_status_msg);
    }
#undef PUB
    tlm_uros_sm_published(&s_sm, ok, failed);
}

void tlm_uros_init(uint32_t boot_id)
{
    srand((unsigned)(boot_id * 2654435761u) ^ (unsigned)tlm_time_us());
    tlm_arena_init(&s_arena, s_arena_mem, sizeof(s_arena_mem));
    s_alloc.allocate = a_alloc;
    s_alloc.deallocate = a_free;
    s_alloc.reallocate = a_realloc;
    s_alloc.zero_allocate = a_zalloc;
    s_alloc.state = NULL;
    if (!rcutils_set_default_allocator(&s_alloc))
    {
        tlm_fatal("uros_allocator");
    }
    messages_init();
    (void)rmw_uros_set_custom_transport(tlm_uros_transport()->framing, NULL, t_open, t_close, t_write,
                                        t_read);
    tlm_uros_sm_init(&s_sm);
}

void tlm_uros_period(const tlm_status_t *status, uint32_t sens_missed)
{
    switch (tlm_uros_sm_step(&s_sm))
    {
    case TLM_UROS_DO_PING:
        tlm_uros_sm_ping(&s_sm, rmw_uros_ping_agent(TLM_UROS_PING_TIMEOUT_MS, 1u) == RMW_RET_OK);
        break;
    case TLM_UROS_DO_CREATE:
        tlm_uros_sm_created(&s_sm, create_entities());
        break;
    case TLM_UROS_DO_PUBLISH:
        publish(status, sens_missed);
        break;
    case TLM_UROS_DO_PUBLISH_PING:
        publish(status, sens_missed);
        tlm_uros_sm_ping(&s_sm, rmw_uros_ping_agent(TLM_UROS_PING_TIMEOUT_MS, 1u) == RMW_RET_OK);
        break;
    case TLM_UROS_DO_DESTROY:
        destroy_entities();
        tlm_uros_sm_destroyed(&s_sm);
        break;
    case TLM_UROS_DO_NOTHING:
    default:
        break;
    }
}

tlm_uros_report_t tlm_uros_report(void)
{
    const tlm_uros_report_t r = {
        .state = tlm_uros_state_name(s_sm.state),
        .sessions = s_sm.sessions,
        .losses = s_sm.losses,
        .published = s_sm.published,
        .publish_failures = s_sm.publish_failures,
        .arena_high_water = (uint32_t)s_arena.high_water,
        .arena_failures = s_arena.failures,
        .fini_errors = s_fini_errors,
    };
    return r;
}
