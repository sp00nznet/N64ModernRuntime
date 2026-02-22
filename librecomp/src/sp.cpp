#include <cstdio>
#include <fstream>
#include <ultramodern/ultramodern.hpp>
#include <ultramodern/rsp.hpp>
#include "recomp.h"

extern "C" void osSpTaskLoad_recomp(uint8_t* rdram, recomp_context* ctx) {
    // Nothing to do here
}

bool dump_frame = false;

extern "C" void osSpTaskStartGo_recomp(uint8_t* rdram, recomp_context* ctx) {
    //printf("[sp] osSpTaskStartGo(0x%08X)\n", (uint32_t)ctx->r4);
    OSTask* task = TO_PTR(OSTask, ctx->r4);
    // For debugging
    if (dump_frame) {
        char addr_str[32];
        constexpr size_t ram_size = 0x800000;
        std::unique_ptr<char[]> ram_unswapped = std::make_unique<char[]>(ram_size);
        snprintf(addr_str, sizeof(addr_str) - 1, "%08X", task->t.data_ptr);
        addr_str[sizeof(addr_str) - 1] = '\0';
        std::ofstream dump_file{ "ramdump" + std::string{ addr_str } + ".bin", std::ios::binary};

        for (size_t i = 0; i < ram_size; i++) {
            ram_unswapped[i] = rdram[i ^ 3];
        }

        dump_file.write(ram_unswapped.get(), ram_size);
        dump_frame = false;
    }
    if (task->t.type == M_AUDTASK) {
        // Run audio tasks synchronously on the game thread to prevent a race
        // condition where the game overwrites the command list buffer before
        // the async RSP thread processes it. DKR double-buffers command lists
        // but the game thread outruns the RSP thread, corrupting the data.
        ultramodern::rsp::run_task(rdram, task);
        ultramodern::send_sp_complete_message(rdram);
    } else {
        ultramodern::submit_rsp_task(rdram, ctx->r4);
    }
}

extern "C" void osSpTaskYield_recomp(uint8_t* rdram, recomp_context* ctx) {
    // Ignore yield requests (acts as if the task completed before it received the yield request)
}

extern "C" void osSpTaskYielded_recomp(uint8_t* rdram, recomp_context* ctx) {
    // Task yield requests are ignored, so always return 0 as tasks will never be yielded
    ctx->r2 = 0;
}

extern "C" void __osSpSetPc_recomp(uint8_t* rdram, recomp_context* ctx) {
    assert(false);
}
