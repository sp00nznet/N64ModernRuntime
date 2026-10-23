// Deterministic mode: the scheduler side. See ultramodern::det in ultramodern.hpp.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>

#include "deterministic.hpp"

void dequeue_external_messages(RDRAM_ARG1); // mesgqueue.cpp

namespace {
std::atomic_bool enabled_flag = false;
ultramodern::det::frame_callback_t frame_callback = nullptr;

// Only game threads touch these, and only one game thread runs at a time.
uint64_t virtual_now = 0;     // CPU counter ticks
uint32_t reads_since_advance = 0;
uint64_t retraces = 0;
bool pacing_started = false;
std::chrono::high_resolution_clock::time_point pacing_start;

constexpr uint64_t counter_per_second = 46'875'000;
constexpr uint64_t ticks_per_retrace = counter_per_second / 60;
// Each clock read moves time on by this much (~1.4 us), so code that spins on the clock finishes.
constexpr uint64_t ticks_per_read = 64;
} // namespace

void ultramodern::det::enable() { enabled_flag = true; }
bool ultramodern::det::enabled() { return enabled_flag; }
void ultramodern::det::set_frame_callback(frame_callback_t callback) { frame_callback = callback; }
uint64_t ultramodern::det::retrace_count() { return retraces; }

static uint8_t* det_rdram = nullptr; // the clock read path has no rdram argument

static uint64_t next_event(RDRAM_ARG1) {
    return std::min((retraces + 1) * ticks_per_retrace, ultramodern::det::next_timer_timestamp(PASS_RDRAM1));
}

// Moves virtual time to `t` (the next event) and posts what fires there. Runs on a game thread at a
// deterministic point; the messages are picked up at the next send/receive.
static void advance_to(RDRAM_ARG uint64_t t) {
    // Keep virtual time from running ahead of real time (it may fall behind on a slow machine).
    if (!pacing_started) {
        pacing_started = true;
        pacing_start = std::chrono::high_resolution_clock::now() - std::chrono::microseconds(virtual_now * 1'000'000 / counter_per_second);
    }
    ultramodern::sleep_until(pacing_start + std::chrono::microseconds(t * 1'000'000 / counter_per_second));

    virtual_now = std::max(virtual_now, t);
    reads_since_advance = 0;

    ultramodern::det::fire_timers(PASS_RDRAM t);
    if ((retraces + 1) * ticks_per_retrace <= t) {
        if (frame_callback != nullptr) {
            frame_callback(retraces);
        }
        ultramodern::det::vi_retrace(PASS_RDRAM1);
        retraces++;
    }
}

void ultramodern::det::set_rdram(RDRAM_ARG1) { det_rdram = rdram; }

uint64_t ultramodern::det::now_for_read() {
    uint64_t t = virtual_now + (uint64_t)(++reads_since_advance) * ticks_per_read;
    // A game spinning on the clock reaches the next event this way, as hardware interrupts would.
    if (det_rdram != nullptr) {
        uint8_t* rdram = det_rdram;
        const uint64_t e = next_event(PASS_RDRAM1);
        if (t >= e) {
            advance_to(PASS_RDRAM e);
            t = virtual_now;
        }
    }
    return t;
}

void ultramodern::det::idle(RDRAM_ARG1) {
    det_rdram = rdram;
    // Deliver what the game threads themselves queued (task completions, SI); that may wake one.
    dequeue_external_messages(PASS_RDRAM1);
    if (!ultramodern::thread_queue_empty(PASS_RDRAM ultramodern::running_queue)) {
        return;
    }
    // Nothing can run until the next event: move virtual time to it.
    advance_to(PASS_RDRAM next_event(PASS_RDRAM1));
    dequeue_external_messages(PASS_RDRAM1);
}
