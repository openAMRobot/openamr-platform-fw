// Declares upstream linorobot2 PID as UpstreamPID, next to the overlay PID, so one test binary can
// compare both. The upstream sources are compiled from a checkout of linorobot2_hardware at the
// pinned base (LINOROBOT2_HARDWARE_DIR) with -DPID=UpstreamPID; nothing upstream is copied here.
#ifndef OPENAMR_HOST_UPSTREAM_PID_H
#define OPENAMR_HOST_UPSTREAM_PID_H

#pragma push_macro("PID_H")
#undef PID_H
#define PID UpstreamPID
#include UPSTREAM_PID_HEADER
#undef PID
#pragma pop_macro("PID_H")

#endif
