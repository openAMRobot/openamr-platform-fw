// SPDX-License-Identifier: MIT
// Geometry and acceleration used by the host tests only. These are NOT
// hardware values: the firmware has no defaults for them and refuses to move
// until the commissioning record supplies measured ones (TODO(Evans)).
#pragma once

extern "C" {
#include "oamr/motion.h"
}

constexpr float kTestWheelRadiusM = 0.1f;
constexpr float kTestTrackWidthM = 0.4f;
constexpr float kTestAccelLimitMS2 = 1.5f;

inline void commission_for_test(oamr_motion_config_t *cfg)
{
    cfg->wheel_radius_m = kTestWheelRadiusM;
    cfg->track_width_m = kTestTrackWidthM;
    cfg->accel_limit_m_s2 = kTestAccelLimitMS2;
    cfg->commissioned = true;
}
