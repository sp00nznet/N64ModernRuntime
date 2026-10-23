// Internals of deterministic mode shared between the scheduler (deterministic.cpp), the timers
// (timer.cpp) and the event code (events.cpp). See ultramodern::det in ultramodern.hpp.
#pragma once

#include <cstdint>

#include "ultramodern/ultra64.h"
#include "ultramodern/ultramodern.hpp"

namespace ultramodern::det {
    // Virtual CPU counter ticks; each read also moves it on slightly so busy-waits on the clock end.
    uint64_t now_for_read();
    void set_rdram(RDRAM_ARG1);
    // Called by the idle thread instead of waiting for host messages: advances virtual time to the
    // next retrace or timer and delivers it.
    void idle(RDRAM_ARG1);

    // timer.cpp
    uint64_t next_timer_timestamp(RDRAM_ARG1); // UINT64_MAX when no timer is active
    void fire_timers(RDRAM_ARG uint64_t now);

    // events.cpp: VI state swap plus the VI and AI messages for one retrace.
    void vi_retrace(RDRAM_ARG1);
}
