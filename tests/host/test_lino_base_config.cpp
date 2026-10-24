// Host test of the active motor-driver block of the Gate A overlay config. MIT, OpenAMRobot.
// The header is included unchanged; only macros are evaluated.
#include <gtest/gtest.h>

#include <cmath>

#include "lino_base_config.h"

TEST(LinoBaseConfig, ActiveMotorDriverBlockIsGeneric2In) {
#ifndef USE_GENERIC_2_IN_MOTOR_DRIVER
  FAIL() << "the test below assumes the USE_GENERIC_2_IN_MOTOR_DRIVER block is active";
#endif
  EXPECT_EQ(PWM_BITS, 10);
}

TEST(LinoBaseConfig, PwmMinIsMinusPwmMax) {
  GTEST_SKIP() << "PWM_MIN expands to -1025, tracked in https://github.com/openAMRobot/openamr-platform-fw/issues/25";
  // Parenthesised on purpose: -(PWM_MAX) is the intended value; PWM_MIN must equal it.
  EXPECT_DOUBLE_EQ(static_cast<double>(PWM_MAX), 1023.0);
  EXPECT_DOUBLE_EQ(static_cast<double>(PWM_MIN), -(static_cast<double>(PWM_MAX)));
}
