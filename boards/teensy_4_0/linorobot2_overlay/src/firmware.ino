// Copyright (c) 2021 Juan Miguel Jimeno
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#include <Arduino.h>
#include <micro_ros_platformio.h>
#include <stdio.h>

#include <rcl/rcl.h>
#include <rcl/error_handling.h>
#include <rclc/rclc.h>
#include <rclc/executor.h>

#include <nav_msgs/msg/odometry.h>
#include <sensor_msgs/msg/imu.h>
#include <sensor_msgs/msg/magnetic_field.h>
#include <geometry_msgs/msg/twist.h>
#include <geometry_msgs/msg/vector3.h>
#include <std_msgs/msg/float32_multi_array.h>

#include "config.h"
#include "syslog.h"
#include "motor.h"
#include "kinematics.h"
#include "pid.h"
#include "odometry.h"
#include "imu.h"
#include "mag.h"
#define ENCODER_USE_INTERRUPTS
#define ENCODER_OPTIMIZE_INTERRUPTS
#include "encoder.h"
#include "lidar.h"
#include "wifis.h"
#include "ota.h"

#ifdef MICRO_ROS_TRANSPORT_ARDUINO_WIFI
// remove wifi initialization code from wifi transport
static inline void set_microros_net_transports(IPAddress agent_ip, uint16_t agent_port)
{
    static struct micro_ros_agent_locator locator;
    locator.address = agent_ip;
    locator.port = agent_port;

    rmw_uros_set_custom_transport(
        false,
        (void *) &locator,
        platformio_transport_open,
        platformio_transport_close,
        platformio_transport_write,
        platformio_transport_read
    );
}
#endif

#ifndef NODE_NAME
#define NODE_NAME "linorobot_base_node"
#endif

#ifndef RCCHECK
#define RCCHECK(fn) { rcl_ret_t temp_rc = fn; if((temp_rc != RCL_RET_OK)){rclErrorLoop();}}
#endif
#define RCSOFTCHECK(fn) { rcl_ret_t temp_rc = fn; if((temp_rc != RCL_RET_OK)){}}
#define EXECUTE_EVERY_N_MS(MS, X)  do { \
  static volatile int64_t init = -1; \
  if (init == -1) { init = uxr_millis();} \
  if (uxr_millis() - init > MS) { X; init = uxr_millis();} \
} while (0)

rcl_publisher_t odom_publisher;
rcl_publisher_t imu_publisher;
rcl_publisher_t mag_publisher;
rcl_subscription_t twist_subscriber;
rcl_subscription_t openloop_subscriber;   // DEBUG: PWM boucle ouverte (test moteurs)

// --- DEBUG: telemetrie par roue (Vector3, statique, best-effort) ---
rcl_publisher_t debug_left_publisher;   // gauche/MOTOR1
rcl_publisher_t debug_right_publisher;  // droite/MOTOR2
rcl_publisher_t debug_pwm_publisher;    // sortie PID (PWM)

nav_msgs__msg__Odometry odom_msg;
sensor_msgs__msg__Imu imu_msg;
sensor_msgs__msg__MagneticField mag_msg;
geometry_msgs__msg__Twist twist_msg;

// --- DEBUG: messages + valeurs transmises de moveBase() a publishData() ---
geometry_msgs__msg__Vector3 debug_left_msg;   // x=rpm cible, y=rpm mesure, z=counts bruts
geometry_msgs__msg__Vector3 debug_right_msg;  // idem droite
geometry_msgs__msg__Vector3 debug_pwm_msg;    // x=pwm gauche, y=pwm droite
float debug_req_rpm1 = 0, debug_req_rpm2 = 0;
float debug_cur_rpm1 = 0, debug_cur_rpm2 = 0;
int32_t debug_counts1 = 0, debug_counts2 = 0;
double debug_pwm1 = 0, debug_pwm2 = 0;

// Production / diagnostic build profile (Raj review PR5): defined -> the raw-PWM debug path can
// drive the motors (current commissioning build). Comment this out for a PRODUCTION image that must
// NOT drive motors from /debug/openloop. The subscriber stays (executor count unchanged); only the
// motor application is gated, so production firmware never exposes powered raw debug.
#define ENABLE_POWERED_DEBUG

// --- DEBUG: mode boucle ouverte (PWM fixe applique aux 2 moteurs, PID ignore) ---
geometry_msgs__msg__Vector3 openloop_msg;  // x = PWM applique aux 2 moteurs
double openloop_pwm = 0;
unsigned long openloop_time = 0;

// --- DEBUG: live tuning via /debug/tune (Twist: linear=Kp,Ki,Kd ; angular.x=Rgain ; angular.y=Kff ; angular.z=ff_offset) ---
rcl_subscription_t tune_subscriber;
geometry_msgs__msg__Twist tune_msg;   // linear=Kp,Ki,Kd ; angular.x=right gain ; angular.y=Kff ; angular.z=ff_offset
float motor2_gain = MOTOR2_GAIN;   // right-wheel PWM scale, live-tunable (default = MOTOR2_GAIN)
// Velocity feedforward: PWM = kff*target_rpm + ff_offset(stiction) + PID(residual). The bulk of the
// holding PWM is supplied directly (we measured ~9.3 PWM/rpm open-loop), so the integral barely works
// -> the response shape is the SAME at every speed (no speed-dependent windup/overshoot). Live-tunable:
// /debug/tune angular.y = kff, angular.z = ff_offset (only when >0, so the PID tuner leaves them alone).
#define KFF_DEFAULT 7.87f
#define FF_OFFSET_DEFAULT 21.0f
float kff = KFF_DEFAULT;
float ff_offset = FF_OFFSET_DEFAULT;
// Anti-stiction dither: at LOW target speed (below the drivetrain's stick-slip floor), add a small
// PWM that flips sign every control tick (±amp @ 25 Hz). Net average = 0 (no speed bias), but it keeps
// the wheels micro-moving so static friction never grabs -> smooth slow motion (docking) instead of
// the 0->9 rpm stick-slip limit cycle. Amplitude live-tunable via /debug/tune angular.z.
#define DITHER_BELOW_RPM 13.0     // only dither below this target rpm (above = clean, no need)
#define DITHER_DEFAULT 92.0f      // tuned 2026-06-29: breaks stick-slip down to ~0.06 m/s (docking)
float dither_amp = DITHER_DEFAULT;  // PWM, live-tunable (/debug/tune angular.z); active only <13 rpm

// --- Encoder ripple correction table, LOADED AT RUNTIME (no reflash) via /debug/enc_cal.
// Indexed by wheel angle (counts mod CPR). true_rpm = measured_rpm / CAL[bin]. Instant (no lag),
// applied to current_rpm1/2 before PID+odometry. Loaded at runtime so the table's phase matches the
// CURRENT boot's encoder zero (a compiled-in table is wrong: flashing reboots & shifts the zero).
// Until a table is received, both default to 1.0 = passthrough (raw rpm, like no correction).
#define ENC_CAL_NBINS 36
#define ENC_CAL_CPR   1024
float LEFT_CAL[ENC_CAL_NBINS];
float RIGHT_CAL[ENC_CAL_NBINS];
volatile bool enc_cal_loaded = false;
rcl_subscription_t enc_cal_subscriber;
std_msgs__msg__Float32MultiArray enc_cal_msg;   // 72 floats = 36 left then 36 right
float enc_cal_recv[2 * ENC_CAL_NBINS];

rclc_executor_t executor;
rclc_support_t support;
rcl_allocator_t allocator;
rcl_node_t node;
rcl_timer_t control_timer;

unsigned long long time_offset = 0;
unsigned long prev_cmd_time = 0;
unsigned long prev_odom_update = 0;

enum states 
{
  WAITING_AGENT,
  AGENT_AVAILABLE,
  AGENT_CONNECTED,
  AGENT_DISCONNECTED
} state;

Encoder motor1_encoder(MOTOR1_ENCODER_A, MOTOR1_ENCODER_B, COUNTS_PER_REV1, MOTOR1_ENCODER_INV);
Encoder motor2_encoder(MOTOR2_ENCODER_A, MOTOR2_ENCODER_B, COUNTS_PER_REV2, MOTOR2_ENCODER_INV);
Encoder motor3_encoder(MOTOR3_ENCODER_A, MOTOR3_ENCODER_B, COUNTS_PER_REV3, MOTOR3_ENCODER_INV);
Encoder motor4_encoder(MOTOR4_ENCODER_A, MOTOR4_ENCODER_B, COUNTS_PER_REV4, MOTOR4_ENCODER_INV);

Motor motor1_controller(PWM_FREQUENCY, PWM_BITS, MOTOR1_INV, MOTOR1_PWM, MOTOR1_IN_A, MOTOR1_IN_B);
Motor motor2_controller(PWM_FREQUENCY, PWM_BITS, MOTOR2_INV, MOTOR2_PWM, MOTOR2_IN_A, MOTOR2_IN_B);
Motor motor3_controller(PWM_FREQUENCY, PWM_BITS, MOTOR3_INV, MOTOR3_PWM, MOTOR3_IN_A, MOTOR3_IN_B);
Motor motor4_controller(PWM_FREQUENCY, PWM_BITS, MOTOR4_INV, MOTOR4_PWM, MOTOR4_IN_A, MOTOR4_IN_B);

PID motor1_pid(PWM_MIN, PWM_MAX, K_P, K_I, K_D);
PID motor2_pid(PWM_MIN, PWM_MAX, K_P, K_I, K_D);
PID motor3_pid(PWM_MIN, PWM_MAX, K_P, K_I, K_D);
PID motor4_pid(PWM_MIN, PWM_MAX, K_P, K_I, K_D);

Kinematics kinematics(
    Kinematics::LINO_BASE, 
    MOTOR_MAX_RPM, 
    MAX_RPM_RATIO, 
    MOTOR_OPERATING_VOLTAGE, 
    MOTOR_POWER_MAX_VOLTAGE, 
    WHEEL_DIAMETER, 
    LR_WHEELS_DISTANCE
);

Odometry odometry;
IMU imu;
MAG mag;

#ifndef BAUDRATE
#define BAUDRATE 921600
#endif

void setup()
{
    for (int i = 0; i < ENC_CAL_NBINS; i++) { LEFT_CAL[i] = 1.0f; RIGHT_CAL[i] = 1.0f; }  // 1.0 = passthrough until a table is loaded
    pinMode(LED_PIN, OUTPUT);
    Serial.begin(BAUDRATE);
#ifdef ESP32
    Serial.setRxBufferSize(1024);
#endif

#ifdef BOARD_INIT // board specific setup, must include Wire.begin
    BOARD_INIT
#else
    Wire.begin();
#endif

    initWifis();
    initOta();
    bool imu_ok = imu.init();
    if (!imu_ok) // take IMU failure as fatal
    {
        Serial.println("IMU init failed");
        syslog(LOG_INFO, "%s IMU init failed %lu", __FUNCTION__, millis());
        while (1)
        {
            flashLED(3); // flash 3 times
            runWifis();
            runOta();
        }
    }
    bool mag_ok = mag.init();
    if (!mag_ok) // take mag failure as fatal
    {
        Serial.println("MAG init failed");
        syslog(LOG_INFO, "%s MAG init failed %lu", __FUNCTION__, millis());
        while (1)
        {
            flashLED(4); // flash 4 times
            runWifis();
            runOta();
        }
    }
    initLidar(); // after wifi connected

#ifdef MICRO_ROS_TRANSPORT_ARDUINO_WIFI
    set_microros_net_transports(AGENT_IP, AGENT_PORT);
#else
    set_microros_serial_transports(Serial);
#endif

#ifdef BOARD_INIT_LATE // board specific setup
    BOARD_INIT_LATE
#endif
    syslog(LOG_INFO, "%s Ready %lu", __FUNCTION__, millis());
}

void loop() {
    switch (state) 
    {
        case WAITING_AGENT:
            EXECUTE_EVERY_N_MS(500, state = (RMW_RET_OK == rmw_uros_ping_agent(100, 1)) ? AGENT_AVAILABLE : WAITING_AGENT;);
            break;
        case AGENT_AVAILABLE:
            syslog(LOG_INFO, "%s agent available %lu", __FUNCTION__, millis());
            state = (true == createEntities()) ? AGENT_CONNECTED : WAITING_AGENT;
            if (state == WAITING_AGENT) 
            {
                destroyEntities();
            }
            break;
        case AGENT_CONNECTED:
#ifndef USE_STAY_CONNECTED // Stay connected. Do not ping.
            EXECUTE_EVERY_N_MS(200, state = (RMW_RET_OK == rmw_uros_ping_agent(100, 1)) ? AGENT_CONNECTED : AGENT_DISCONNECTED;);
#endif
            if (state == AGENT_CONNECTED) 
            {
                rclc_executor_spin_some(&executor, RCL_MS_TO_NS(100));
            }
            break;
        case AGENT_DISCONNECTED:
            syslog(LOG_INFO, "%s agent disconnected %lu", __FUNCTION__, millis());
            fullStop();
            destroyEntities();
            state = WAITING_AGENT;
            break;
        default:
            break;
    }
    runWifis();
    runOta();
#ifdef WDT_TIMEOUT
    esp_task_wdt_reset();
#endif
#ifdef BOARD_LOOP // board specific loop
    BOARD_LOOP
#endif
}

void controlCallback(rcl_timer_t * timer, int64_t last_call_time) 
{
    RCLC_UNUSED(last_call_time);
    if (timer != NULL) 
    {
       moveBase();
       publishData();
    }
}

void twistCallback(const void * msgin)
{
    digitalWrite(LED_PIN, !digitalRead(LED_PIN));

    prev_cmd_time = millis();
}

// Raw-PWM diagnostic endpoint. Validated + bounded (Raj PR5): reject non-finite values and clamp
// to a conservative limit so a stray/huge command cannot slam the motors. (Full arming/exclusive
// diagnostic mode is a follow-up; this is the bounds + validation layer.)
#define OPENLOOP_PWM_LIMIT (0.7 * PWM_MAX)   // conservative ceiling for raw diagnostic PWM
void openloopCallback(const void * msgin)
{
    const geometry_msgs__msg__Vector3 * m = (const geometry_msgs__msg__Vector3 *)msgin;
    double v = m->x;
    if (!isfinite(v)) { openloop_pwm = 0.0; return; }            // reject NaN/Inf
    openloop_pwm = constrain(v, -OPENLOOP_PWM_LIMIT, OPENLOOP_PWM_LIMIT);
    openloop_time = millis();
}

// DEBUG: live tuning -- update both PIDs + the right-wheel gain at runtime.
void tuneCallback(const void * msgin)
{
    const geometry_msgs__msg__Twist * m = (const geometry_msgs__msg__Twist *)msgin;
    motor1_pid.updateConstants((float)m->linear.x, (float)m->linear.y, (float)m->linear.z);
    motor2_pid.updateConstants((float)m->linear.x, (float)m->linear.y, (float)m->linear.z);
    if (m->angular.x > 0.0) motor2_gain = (float)m->angular.x;  // right-wheel PWM scale
    if (m->angular.y > 0.0) kff = (float)m->angular.y;          // velocity feedforward gain (PWM/rpm)
    if (m->angular.z >= 0.0) dither_amp = (float)m->angular.z;  // anti-stiction dither amplitude (PWM)
    // (ff_offset is fixed at FF_OFFSET_DEFAULT now that it's tuned; angular.z freed for the dither)
}

// Receive the encoder ripple table at runtime: 72 floats = 36 LEFT_CAL then 36 RIGHT_CAL.
void encCalCallback(const void * msgin)
{
    const std_msgs__msg__Float32MultiArray * m = (const std_msgs__msg__Float32MultiArray *)msgin;
    if (m->data.size < (size_t)(2 * ENC_CAL_NBINS)) return;   // ignore malformed
    for (int i = 0; i < ENC_CAL_NBINS; i++) {
        LEFT_CAL[i]  = m->data.data[i];
        RIGHT_CAL[i] = m->data.data[ENC_CAL_NBINS + i];
    }
    enc_cal_loaded = true;
}

bool createEntities()
{
    syslog(LOG_INFO, "%s %lu", __FUNCTION__, millis());
    allocator = rcl_get_default_allocator();
    //create init_options
    RCCHECK(rclc_support_init(&support, 0, NULL, &allocator));
    // create node
    RCCHECK(rclc_node_init_default(&node, NODE_NAME, "", &support));
    // create odometry publisher
    RCCHECK(rclc_publisher_init_default(
        &odom_publisher,
        &node,
        ROSIDL_GET_MSG_TYPE_SUPPORT(nav_msgs, msg, Odometry),
        "odom/unfiltered"
    ));
    // --- DEBUG publishers (best-effort : non bloquant a 50 Hz) ---
    RCCHECK(rclc_publisher_init_best_effort(
        &debug_left_publisher,
        &node,
        ROSIDL_GET_MSG_TYPE_SUPPORT(geometry_msgs, msg, Vector3),
        "debug/left"
    ));
    RCCHECK(rclc_publisher_init_best_effort(
        &debug_right_publisher,
        &node,
        ROSIDL_GET_MSG_TYPE_SUPPORT(geometry_msgs, msg, Vector3),
        "debug/right"
    ));
    RCCHECK(rclc_publisher_init_best_effort(
        &debug_pwm_publisher,
        &node,
        ROSIDL_GET_MSG_TYPE_SUPPORT(geometry_msgs, msg, Vector3),
        "debug/pwm"
    ));
    // create IMU publisher
    RCCHECK(rclc_publisher_init_default( 
        &imu_publisher, 
        &node,
        ROSIDL_GET_MSG_TYPE_SUPPORT(sensor_msgs, msg, Imu),
    // if we have magnetomter, use imu/data_raw for madgwick filter
#ifndef USE_FAKE_MAG
        "imu/data_raw"
#else
        "imu/data"
#endif
    ));
#ifndef USE_FAKE_MAG
    RCCHECK(rclc_publisher_init_default(
        &mag_publisher,
        &node,
        ROSIDL_GET_MSG_TYPE_SUPPORT(sensor_msgs, msg, MagneticField),
        "imu/mag"
    ));
#endif
    // create twist command subscriber
    RCCHECK(rclc_subscription_init_default(
        &twist_subscriber,
        &node,
        ROSIDL_GET_MSG_TYPE_SUPPORT(geometry_msgs, msg, Twist),
        "cmd_vel"
    ));
    // DEBUG: souscription commande boucle ouverte
    RCCHECK(rclc_subscription_init_default(
        &openloop_subscriber,
        &node,
        ROSIDL_GET_MSG_TYPE_SUPPORT(geometry_msgs, msg, Vector3),
        "debug/openloop"
    ));
    // DEBUG: live PID/gain tuning subscriber
    RCCHECK(rclc_subscription_init_default(
        &tune_subscriber,
        &node,
        ROSIDL_GET_MSG_TYPE_SUPPORT(geometry_msgs, msg, Twist),
        "debug/tune"
    ));
    // DEBUG: runtime encoder ripple table subscriber (72 floats: 36 left + 36 right)
    RCCHECK(rclc_subscription_init_default(
        &enc_cal_subscriber,
        &node,
        ROSIDL_GET_MSG_TYPE_SUPPORT(std_msgs, msg, Float32MultiArray),
        "debug/enc_cal"
    ));
    // pre-allocate the incoming message buffer (publisher sends an empty layout, so no dim allocation)
    enc_cal_msg.data.data = enc_cal_recv;
    enc_cal_msg.data.capacity = 2 * ENC_CAL_NBINS;
    enc_cal_msg.data.size = 0;
    enc_cal_msg.layout.dim.data = NULL;
    enc_cal_msg.layout.dim.capacity = 0;
    enc_cal_msg.layout.dim.size = 0;
    // create timer for actuating the motors at 50 Hz (1000/20)
    const unsigned int control_timeout = 20;
    RCCHECK(rclc_timer_init_default2( 
        &control_timer, 
        &support,
        RCL_MS_TO_NS(control_timeout),
        controlCallback,
        true
    ));
    executor = rclc_executor_get_zero_initialized_executor();
    RCCHECK(rclc_executor_init(&executor, &support.context, 5, & allocator));
    RCCHECK(rclc_executor_add_subscription(
        &executor,
        &twist_subscriber,
        &twist_msg,
        &twistCallback,
        ON_NEW_DATA
    ));
    RCCHECK(rclc_executor_add_subscription(
        &executor,
        &openloop_subscriber,
        &openloop_msg,
        &openloopCallback,
        ON_NEW_DATA
    ));
    RCCHECK(rclc_executor_add_subscription(
        &executor,
        &tune_subscriber,
        &tune_msg,
        &tuneCallback,
        ON_NEW_DATA
    ));
    RCCHECK(rclc_executor_add_subscription(
        &executor,
        &enc_cal_subscriber,
        &enc_cal_msg,
        &encCalCallback,
        ON_NEW_DATA
    ));
    RCCHECK(rclc_executor_add_timer(&executor, &control_timer));

    // synchronize time with the agent
    syncTime();
    digitalWrite(LED_PIN, HIGH);

    return true;
}

bool destroyEntities()
{
    rmw_context_t * rmw_context = rcl_context_get_rmw_context(&support.context);
    (void) rmw_uros_set_context_entity_destroy_session_timeout(rmw_context, 0);

    RCSOFTCHECK(rcl_publisher_fini(&odom_publisher, &node));
    RCSOFTCHECK(rcl_publisher_fini(&debug_left_publisher, &node));
    RCSOFTCHECK(rcl_publisher_fini(&debug_right_publisher, &node));
    RCSOFTCHECK(rcl_publisher_fini(&debug_pwm_publisher, &node));
    RCSOFTCHECK(rcl_publisher_fini(&imu_publisher, &node));
#ifndef USE_FAKE_MAG
    RCSOFTCHECK(rcl_publisher_fini(&mag_publisher, &node));
#endif
    RCSOFTCHECK(rcl_subscription_fini(&twist_subscriber, &node));
    RCSOFTCHECK(rcl_subscription_fini(&openloop_subscriber, &node));
    RCSOFTCHECK(rcl_subscription_fini(&tune_subscriber, &node));
    RCSOFTCHECK(rcl_subscription_fini(&enc_cal_subscriber, &node));
    RCSOFTCHECK(rcl_timer_fini(&control_timer));
    RCSOFTCHECK(rclc_executor_fini(&executor));
    RCSOFTCHECK(rcl_node_fini(&node))
    RCSOFTCHECK(rclc_support_fini(&support));

    digitalWrite(LED_PIN, HIGH);
    
    return true;
}

// --- Apply the runtime-loaded ripple table: true_rpm = measured_rpm / CAL[counts mod CPR].
// Instant (no averaging, no lag) — the measured velocity is corrected at the current wheel angle.
static inline float calib_rpm(float rpm, long counts, const float *cal)
{
    if (!enc_cal_loaded) return rpm;                                    // no table yet: passthrough
    long pos = ((counts % ENC_CAL_CPR) + ENC_CAL_CPR) % ENC_CAL_CPR;    // 0..CPR-1
    int b = (int)(pos * ENC_CAL_NBINS / ENC_CAL_CPR);
    float r = cal[b];
    return (r > 0.05f) ? (rpm / r) : rpm;
}

// Velocity feedforward: the PWM we already know is needed to hold target_rpm (signed; stiction offset
// added in the direction of motion). The PID then only trims the residual -> speed-independent response.
static inline double feedforward(double target_rpm)
{
    if (target_rpm > 0.01)  return (double)kff * target_rpm + (double)ff_offset;
    if (target_rpm < -0.01) return (double)kff * target_rpm - (double)ff_offset;
    return 0.0;
}

// --- Small-window velocity estimator (low-speed noise). The instant getRPM (counts per fixed 20 ms)
// has only ~1 count/sample below ~5 rpm -> ±70% quantization noise -> the PID chases it (bad for slow
// docking moves). Instead, measure the time to accumulate a small FIXED displacement (12 counts): the
// velocity is Δcounts/Δt with Δcounts exact and Δt at microsecond precision -> clean at any speed.
// Window is tiny (≠ the half-rev ripple window), so lag is ~20-30 ms at nav speed (negligible). The
// ripple table still corrects per-angle afterwards (12 counts ≈ 4° -> ripple essentially intact).
#define VEL_WIN_COUNTS 12
#define VEL_MAX_AGE_US 200000UL   // cap look-back at 200 ms (bounds lag; no averaging into a standstill)
#define VEL_BUF 32
struct VelEstimator {
    long          cnt[VEL_BUF];
    unsigned long tus[VEL_BUF];
    int   head = -1;
    int   n = 0;
    float last = 0.0f;
    float update(long c, unsigned long now, int cpr) {
        float rpm = last;
        int found = -1;
        for (int i = 0; i < n; i++) {
            int j = head - i; if (j < 0) j += VEL_BUF;
            if ((now - tus[j]) > VEL_MAX_AGE_US && found >= 0) break;
            found = j;
            if (labs(c - cnt[j]) >= VEL_WIN_COUNTS) break;
        }
        if (found >= 0) {
            long dc = c - cnt[found];
            unsigned long dt = now - tus[found];
            if (dt > 0) rpm = ((float)dc / (float)cpr) / ((float)dt / 60000000.0f);
        }
        head = (head + 1) % VEL_BUF;
        cnt[head] = c; tus[head] = now;
        if (n < VEL_BUF) n++;
        last = rpm;
        return rpm;
    }
};
VelEstimator vel1, vel2;

void fullStop()
{
    twist_msg.linear.x = 0.0;
    twist_msg.linear.y = 0.0;
    twist_msg.angular.z = 0.0;

    motor1_controller.brake();
    motor2_controller.brake();
    motor3_controller.brake();
    motor4_controller.brake();
}

void moveBase()
{
    // Current wheel speeds — always (odometry + telemetry). Instant rpm, then the runtime ripple table
    // corrects the per-rev encoder error at the current wheel angle (no lag).
    unsigned long now_us = micros();
    long c1 = motor1_encoder.read(), c2 = motor2_encoder.read();
    float current_rpm1 = calib_rpm(vel1.update(c1, now_us, COUNTS_PER_REV1), c1, LEFT_CAL);
    float current_rpm2 = calib_rpm(vel2.update(c2, now_us, COUNTS_PER_REV2), c2, RIGHT_CAL);
    float current_rpm3 = motor3_encoder.getRPM();
    float current_rpm4 = motor4_encoder.getRPM();

#ifdef ENABLE_POWERED_DEBUG
    bool ol_active = (fabs(openloop_pwm) >= 1.0) && ((millis() - openloop_time) < 300);
#else
    bool ol_active = false;   // production build: raw /debug/openloop cannot drive the motors
#endif
    bool cmd_stale = ((millis() - prev_cmd_time) >= 200);

    Kinematics::rpm req_rpm = {};      // zero-init; only set in the closed-loop path
    double pwm1 = 0.0, pwm2 = 0.0;

    if (ol_active)
    {
        // Diagnostic open-loop (bounded in openloopCallback): same fixed PWM on both wheels, PID
        // bypassed. Checked FIRST so the diagnostic path is exclusive. PIDs kept reset so resuming
        // closed-loop doesn't jump.
        pwm1 = openloop_pwm;
        pwm2 = openloop_pwm;
        motor1_controller.spin(constrain((int)pwm1, PWM_MIN, PWM_MAX));
        motor2_controller.spin(constrain((int)(pwm2 * motor2_gain), PWM_MIN, PWM_MAX));
        motor3_controller.spin(0);
        motor4_controller.spin(0);
        motor1_pid.reset();
        motor2_pid.reset();
    }
    else if (cmd_stale)
    {
        // Watchdog (Raj PR5): no fresh /cmd_vel within 200 ms -> DETERMINISTIC full stop + PID
        // reset (not just velocity=0 with the PID still running). Odometry below still updates.
        fullStop();
        motor1_pid.reset();
        motor2_pid.reset();
        motor3_pid.reset();
        motor4_pid.reset();
        digitalWrite(LED_PIN, HIGH);
    }
    else
    {
        digitalWrite(LED_PIN, LOW);
        req_rpm = kinematics.getRPM(twist_msg.linear.x, twist_msg.linear.y, twist_msg.angular.z);
        // Feedforward supplies the holding PWM; the PID only trims the residual error.
        pwm1 = feedforward(req_rpm.motor1) + motor1_pid.compute(req_rpm.motor1, current_rpm1);
        pwm2 = feedforward(req_rpm.motor2) + motor2_pid.compute(req_rpm.motor2, current_rpm2);
        // Anti-stiction dither: ±dither_amp flipped every tick (25 Hz), only below the stick-slip floor.
        static int dither_sign = 1;
        dither_sign = -dither_sign;
        if (dither_amp > 0.0f) {
            if (fabs(req_rpm.motor1) > 0.1 && fabs(req_rpm.motor1) < DITHER_BELOW_RPM) pwm1 += dither_sign * dither_amp;
            if (fabs(req_rpm.motor2) > 0.1 && fabs(req_rpm.motor2) < DITHER_BELOW_RPM) pwm2 += dither_sign * dither_amp;
        }
        motor1_controller.spin(constrain((int)pwm1, PWM_MIN, PWM_MAX));
        motor2_controller.spin(constrain((int)(pwm2 * motor2_gain), PWM_MIN, PWM_MAX));
        motor3_controller.spin(motor3_pid.compute(req_rpm.motor3, current_rpm3));
        motor4_controller.spin(motor4_pid.compute(req_rpm.motor4, current_rpm4));
    }

    // --- DEBUG telemetry per wheel ---
    debug_req_rpm1 = req_rpm.motor1;
    debug_req_rpm2 = req_rpm.motor2;
    debug_cur_rpm1 = current_rpm1;
    debug_cur_rpm2 = current_rpm2;
    debug_counts1 = motor1_encoder.read();
    debug_counts2 = motor2_encoder.read();
    debug_pwm1 = pwm1;
    debug_pwm2 = pwm2;

    // --- Odometry (always) ---
    Kinematics::velocities current_vel = kinematics.getVelocities(
        current_rpm1, current_rpm2, current_rpm3, current_rpm4);

    unsigned long now = millis();
    float vel_dt = (now - prev_odom_update) / 1000.0;
    prev_odom_update = now;
    // Reject an abnormal first/large dt so the first odometry sample doesn't jump (Raj PR5).
    if (vel_dt <= 0.0 || vel_dt > 0.5) vel_dt = 0.0;
    odometry.update(vel_dt, current_vel.linear_x, current_vel.linear_y, current_vel.angular_z);
}

void publishData()
{
    odom_msg = odometry.getData();
    imu_msg = imu.getData();
#ifdef USE_FAKE_IMU
    imu_msg.angular_velocity.z = odom_msg.twist.twist.angular.z;
#endif
    mag_msg = mag.getData();
#ifdef MAG_BIAS
    const float mag_bias[3] = MAG_BIAS;
    mag_msg.magnetic_field.x -= mag_bias[0];
    mag_msg.magnetic_field.y -= mag_bias[1];
    mag_msg.magnetic_field.z -= mag_bias[2];
#endif

    struct timespec time_stamp = getTime();

    odom_msg.header.stamp.sec = time_stamp.tv_sec;
    odom_msg.header.stamp.nanosec = time_stamp.tv_nsec;

    imu_msg.header.stamp.sec = time_stamp.tv_sec;
    imu_msg.header.stamp.nanosec = time_stamp.tv_nsec;

    mag_msg.header.stamp.sec = time_stamp.tv_sec;
    mag_msg.header.stamp.nanosec = time_stamp.tv_nsec;

    RCSOFTCHECK(rcl_publish(&imu_publisher, &imu_msg, NULL));
#ifndef USE_FAKE_MAG
    RCSOFTCHECK(rcl_publish(&mag_publisher, &mag_msg, NULL));
#endif
    RCSOFTCHECK(rcl_publish(&odom_publisher, &odom_msg, NULL));

    // --- DEBUG: telemetrie par roue ---
    debug_left_msg.x = debug_req_rpm1;   // rpm cible gauche
    debug_left_msg.y = debug_cur_rpm1;   // rpm mesure gauche
    debug_left_msg.z = debug_counts1;    // counts bruts gauche
    debug_right_msg.x = debug_req_rpm2;  // rpm cible droite
    debug_right_msg.y = debug_cur_rpm2;  // rpm mesure droite
    debug_right_msg.z = debug_counts2;   // counts bruts droite
    debug_pwm_msg.x = debug_pwm1;        // pwm gauche
    debug_pwm_msg.y = debug_pwm2;        // pwm droite
    debug_pwm_msg.z = 0;
    RCSOFTCHECK(rcl_publish(&debug_left_publisher, &debug_left_msg, NULL));
    RCSOFTCHECK(rcl_publish(&debug_right_publisher, &debug_right_msg, NULL));
    RCSOFTCHECK(rcl_publish(&debug_pwm_publisher, &debug_pwm_msg, NULL));
}

bool syncTime()
{
    const int timeout_ms = 1000;
    if (rmw_uros_epoch_synchronized()) return true; // synchronized previously
    // get the current time from the agent
    RCCHECK(rmw_uros_sync_session(timeout_ms));
    if (rmw_uros_epoch_synchronized()) {
#if (_POSIX_TIMERS > 0)
        // Get time in milliseconds or nanoseconds
        int64_t time_ns = rmw_uros_epoch_nanos();
    timespec tp;
    tp.tv_sec = time_ns / 1000000000;
    tp.tv_nsec = time_ns % 1000000000;
    clock_settime(CLOCK_REALTIME, &tp);
#else
    unsigned long long ros_time_ms = rmw_uros_epoch_millis();
    // now we can find the difference between ROS time and uC time
    time_offset = ros_time_ms - millis();
#endif
    return true;
    }
    return false;
}

struct timespec getTime()
{
    struct timespec tp = {0};
#if (_POSIX_TIMERS > 0)
    clock_gettime(CLOCK_REALTIME, &tp);
#else
    // add time difference between uC time and ROS time to
    // synchronize time with ROS
    unsigned long long now = millis() + time_offset;
    tp.tv_sec = now / 1000;
    tp.tv_nsec = (now % 1000) * 1000000;
#endif
    return tp;
}

void rclErrorLoop() 
{
    while(true)
    {
        flashLED(2);
    }
}

void flashLED(int n_times)
{
    for(int i=0; i<n_times; i++)
    {
        digitalWrite(LED_PIN, HIGH);
        delay(150);
        digitalWrite(LED_PIN, LOW);
        delay(150);
    }
    delay(1000);
}
