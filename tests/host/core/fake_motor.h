// SPDX-License-Identifier: MIT
// Fake motor backend: records every write the motion core makes.
#pragma once

#include <vector>

extern "C" {
#include "oamr/motor_backend.h"
}

struct FakeMotor {
    struct Write {
        float left;
        float right;
    };
    bool ready = true;
    std::vector<Write> writes;

    static void set(void *ctx, float l, float r)
    {
        static_cast<FakeMotor *>(ctx)->writes.push_back({l, r});
    }
    static bool is_ready(void *ctx) { return static_cast<FakeMotor *>(ctx)->ready; }
    static oamr_wheel_feedback_t feedback(void *)
    {
        oamr_wheel_feedback_t f{};
        return f;
    }
    oamr_motor_backend_t backend()
    {
        return oamr_motor_backend_t{&FakeMotor::set, &FakeMotor::is_ready, &FakeMotor::feedback, this};
    }
    const Write &last() const { return writes.back(); }
};
