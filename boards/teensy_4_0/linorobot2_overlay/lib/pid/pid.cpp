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

#include "Arduino.h"
#include "pid.h"

PID::PID(float min_val, float max_val, float kp, float ki, float kd):
    min_val_(min_val),
    max_val_(max_val),
    kp_(kp),
    ki_(ki),
    kd_(kd),
    integral_(0.0),      // explicit init (was indeterminate) — Raj PR5
    derivative_(0.0),
    prev_error_(0.0)
{
}

double PID::compute(float setpoint, float measured_value)
{
    double error = setpoint - measured_value;
    derivative_ = error - prev_error_;

    integral_ += error;                                          // tentative integrate

    double pid = (kp_ * error) + (ki_ * integral_) + (kd_ * derivative_);

    // Back-calculation anti-windup: if the output saturates, pull the excess straight back OUT of the
    // integral so it only ever supplies what is actually achievable (ki*integral = limit - kp*e - kd*d).
    // Unlike conditional integration (which merely FREEZES a too-large integral), this BLEEDS the windup
    // out, so a long/saturated rise (e.g. a hard step to near-max speed) no longer overshoots.
    if (pid > max_val_)
    {
        if (ki_ > 1e-6) integral_ -= (pid - max_val_) / ki_;
        pid = max_val_;
    }
    else if (pid < min_val_)
    {
        if (ki_ > 1e-6) integral_ -= (pid - min_val_) / ki_;
        pid = min_val_;
    }

    if (setpoint == 0 && error == 0)
    {
        integral_ = 0;
        derivative_ = 0;
    }

    prev_error_ = error;
    return pid;
}

void PID::updateConstants(float kp, float ki, float kd)
{
    kp_ = kp;
    ki_ = ki;
    kd_ = kd;
}

void PID::reset()
{
    integral_ = 0.0;
    derivative_ = 0.0;
    prev_error_ = 0.0;
}
