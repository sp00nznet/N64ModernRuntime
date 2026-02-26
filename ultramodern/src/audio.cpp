#include "ultramodern/ultra64.h"
#include "ultramodern/ultramodern.hpp"
#include <cassert>

static uint32_t sample_rate = 48000;

static ultramodern::audio_callbacks_t audio_callbacks;

void ultramodern::set_audio_callbacks(const ultramodern::audio_callbacks_t& callbacks) {
    audio_callbacks = callbacks;
}

void ultramodern::init_audio() {
    // Pick an initial dummy sample rate; this will be set by the game later to the true sample rate.
    set_audio_frequency(48000);
}

void ultramodern::set_audio_frequency(uint32_t freq) {
    if (audio_callbacks.set_frequency) {
        audio_callbacks.set_frequency(freq);
    }
    sample_rate = freq;
}

static int audio_buf_count = 0;

void ultramodern::queue_audio_buffer(RDRAM_ARG PTR(int16_t) audio_data_, uint32_t byte_count) {
    // Bounds-check the N64 address before any pointer conversion.
    uint32_t phys = (uint32_t)((uint64_t)audio_data_ - 0xFFFFFFFF80000000ULL);
    if (byte_count == 0 || byte_count > 0x10000 || phys + byte_count > 0x800000) {
        return; // Invalid buffer - skip silently
    }

    // Ensure that the byte count is an integer multiple of samples.
    byte_count &= ~1u;
    uint32_t sample_count = byte_count / sizeof(int16_t);

    audio_buf_count++;
    {
        int nonzero_bytes = 0;
        for (uint32_t i = 0; i < byte_count && i < 256; i++) {
            if (rdram[phys + i] != 0) nonzero_bytes++;
        }
        if (audio_buf_count <= 5 || (audio_buf_count % 100) == 0) {
            fprintf(stderr, "[AUDIO-BUF] #%d: n64addr=0x%08X phys=0x%06X bytes=%u samples=%u nz=%d\n",
                    audio_buf_count, (uint32_t)audio_data_, phys, byte_count, sample_count, nonzero_bytes);
            fflush(stderr);
        }
    }

    // Queue the audio data.
    if (sample_count > 0 && audio_callbacks.queue_samples) {
        audio_callbacks.queue_samples(TO_PTR(int16_t, audio_data_), sample_count);
    }
}

// For SDL2
//uint32_t buffer_offset_frames = 1;
// For Godot
float buffer_offset_frames = 0.5f;

// If there's ever any audio popping, check here first. Some games are very sensitive to
// the remaining sample count and reporting a number that's too high here can lead to issues.
// Reporting a number that's too low can lead to audio lag in some games.
uint32_t ultramodern::get_remaining_audio_bytes() {
    // Get the number of remaining buffered audio bytes.
    uint32_t buffered_byte_count;
    if (audio_callbacks.get_frames_remaining != nullptr) {
        buffered_byte_count = audio_callbacks.get_frames_remaining() * 2 * sizeof(int16_t);
    }
    else {
        buffered_byte_count = 100;
    }
    // Adjust the reported count to be some number of refreshes in the future, which helps ensure that
    // there are enough samples even if the audio thread experiences a small amount of lag. This prevents
    // audio popping on games that use the buffered audio byte count to determine how many samples
    // to generate.
    uint32_t samples_per_vi = (sample_rate / 60);
    if (buffered_byte_count > static_cast<uint32_t>(buffer_offset_frames * sizeof(int16_t) * samples_per_vi)) {
        buffered_byte_count -= static_cast<uint32_t>(buffer_offset_frames * sizeof(int16_t) * samples_per_vi);
    }
    else {
        buffered_byte_count = 0;
    }
    return buffered_byte_count;
}
