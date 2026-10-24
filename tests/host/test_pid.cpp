// Host tests for the Gate A overlay PID (boards/teensy_4_0/linorobot2_overlay/lib/pid).
// MIT, OpenAMRobot. The code under test is compiled unchanged; private state (integral,
// derivative, previous error) is observed through compute() only.
#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "pid.h"
#include "upstream_pid.h"

namespace {

constexpr float kMin = -1023.0f;  // the overlay's PWM range for a 10-bit PWM
constexpr float kMax = 1023.0f;

// Steps the PID with a fixed measurement until the output is strictly inside (min, max);
// returns the number of steps taken, or limit if it never leaves saturation.
template <typename Pid>
int steps_to_leave_saturation(Pid& pid, float setpoint, float measured, int limit) {
  for (int step = 1; step <= limit; ++step) {
    const double out = pid.compute(setpoint, measured);
    if (out < kMax && out > kMin) return step;
  }
  return limit;
}

// Drives a PID into positive saturation: large positive error for n steps.
template <typename Pid>
void wind_up(Pid& pid, int n) {
  for (int i = 0; i < n; ++i) pid.compute(5000.0f, 0.0f);
}

}  // namespace

TEST(OverlayPid, OutputStaysWithinMinMax) {
  PID pid(kMin, kMax, 2.0f, 0.8f, 0.5f);
  // Deterministic sweep of setpoints and measurements, including large steps both ways.
  const std::vector<float> setpoints = {0.0f, 5000.0f, -5000.0f, 10.0f, -10.0f, 300.0f, 0.0f};
  for (float sp : setpoints) {
    for (int i = 0; i < 200; ++i) {
      const float measured = static_cast<float>((i * 37) % 400) - 200.0f;
      const double out = pid.compute(sp, measured);
      ASSERT_GE(out, kMin) << "setpoint " << sp << " step " << i;
      ASSERT_LE(out, kMax) << "setpoint " << sp << " step " << i;
    }
  }
}

TEST(OverlayPid, AntiWindupLeavesSaturationWithinBoundedStepsUnlikeUpstream) {
  // Gains with the P term alone below the limit (kp * 5000 = 500 < 1023), so only the integral
  // saturates the output during the wind-up.
  const float kp = 0.1f, ki = 0.05f, kd = 0.0f;
  PID overlay(kMin, kMax, kp, ki, kd);
  UpstreamPID upstream(kMin, kMax, kp, ki, kd);
  wind_up(overlay, 200);
  wind_up(upstream, 200);
  ASSERT_DOUBLE_EQ(overlay.compute(5000.0f, 0.0f), kMax);
  ASSERT_DOUBLE_EQ(upstream.compute(5000.0f, 0.0f), kMax);

  // The setpoint drops below the measurement: the output must come off the limit.
  const int limit = 100000;
  const int overlay_steps = steps_to_leave_saturation(overlay, 50.0f, 100.0f, limit);
  const int upstream_steps = steps_to_leave_saturation(upstream, 50.0f, 100.0f, limit);
  EXPECT_LE(overlay_steps, 3) << "back-calculation must bleed the integral out of saturation";
  EXPECT_GT(upstream_steps, 1000) << "reference: the upstream integral winds up without bound";
  EXPECT_LT(overlay_steps, upstream_steps);
}

TEST(OverlayPid, AntiWindupBleedsIntegralSoOutputTracksTheLimit) {
  // While saturated, back-calculation keeps ki * integral = max - kp * e - kd * d, so the first
  // unsaturated output after the error halves is predictable: kp * e2 + (max - kp * e1) + ki * e2.
  const float kp = 0.1f, ki = 0.05f;
  PID pid(kMin, kMax, kp, ki, 0.0f);
  wind_up(pid, 50);
  const double expected = kp * 2500.0 + (kMax - kp * 5000.0) + ki * 2500.0;
  EXPECT_NEAR(pid.compute(2500.0f, 0.0f), std::fmin(expected, kMax), 1e-3);
}

TEST(OverlayPid, ResetClearsIntegralDerivativeAndPreviousError) {
  const float kp = 0.3f, ki = 0.2f, kd = 0.7f;
  PID used(kMin, kMax, kp, ki, kd);
  for (int i = 0; i < 20; ++i) used.compute(400.0f, static_cast<float>(i));
  used.reset();
  PID fresh(kMin, kMax, kp, ki, kd);
  // With all state cleared, the reset PID must reproduce a fresh one step for step; the first
  // step exercises prev_error (derivative = e - 0) and the integral (= e).
  for (int i = 0; i < 10; ++i) {
    const float sp = 120.0f, measured = static_cast<float>(i * 3);
    ASSERT_DOUBLE_EQ(used.compute(sp, measured), fresh.compute(sp, measured)) << "step " << i;
  }
}

TEST(OverlayPid, ZeroSetpointAndZeroErrorResetsIntegral) {
  const float kp = 0.0f, ki = 0.5f, kd = 0.0f;  // integral only, so the output is ki * integral
  PID pid(kMin, kMax, kp, ki, kd);
  for (int i = 0; i < 10; ++i) pid.compute(100.0f, 0.0f);  // integral 1000, output 500
  pid.compute(0.0f, 0.0f);                                   // setpoint 0 and error 0: reset
  EXPECT_DOUBLE_EQ(pid.compute(10.0f, 0.0f), 0.5 * 10.0) << "integral must restart from zero";
}

TEST(OverlayPid, NonZeroErrorAtZeroSetpointKeepsIntegral) {
  PID pid(kMin, kMax, 0.0f, 0.5f, 0.0f);
  for (int i = 0; i < 10; ++i) pid.compute(100.0f, 0.0f);  // integral 1000
  // Setpoint 0 but the wheel still moves (error -5): the integral is not reset.
  EXPECT_DOUBLE_EQ(pid.compute(0.0f, 5.0f), 0.5 * (1000.0 - 5.0));
}

TEST(OverlayPid, UpdateConstantsTakesEffect) {
  PID pid(kMin, kMax, 0.0f, 0.0f, 0.0f);
  EXPECT_DOUBLE_EQ(pid.compute(10.0f, 0.0f), 0.0);
  pid.reset();
  pid.updateConstants(2.0f, 0.0f, 0.0f);
  EXPECT_DOUBLE_EQ(pid.compute(10.0f, 0.0f), 20.0);
  pid.updateConstants(0.0f, 0.0f, 1.5f);
  // derivative = 4 - 10 = -6 with the new kd only
  EXPECT_DOUBLE_EQ(pid.compute(10.0f, 6.0f), 1.5 * (4.0 - 10.0));
}

TEST(UpstreamPid, ReferenceIsTheUnmodifiedUpstreamClamp) {
  // Guards the comparison: the upstream reference clamps but keeps integrating.
  UpstreamPID pid(kMin, kMax, 0.0f, 1.0f, 0.0f);
  EXPECT_DOUBLE_EQ(pid.compute(2000.0f, 0.0f), kMax);
  EXPECT_DOUBLE_EQ(pid.compute(-1500.0f, 0.0f), 500.0);  // integral 2000 - 1500, not bled
}
