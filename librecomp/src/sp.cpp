#include <cstdio>
#include <fstream>
#include <ultramodern/ultramodern.hpp>
#include <ultramodern/rsp.hpp>
#include "recomp.h"

// Forward declaration - implemented in src/f3ddkr.cpp
extern void f3ddkr_process_dl(uint8_t* rdram, uint32_t dl_addr, uint32_t dl_size);

extern "C" void osSpTaskLoad_recomp(uint8_t* rdram, recomp_context* ctx) {
    // Nothing to do here
}

bool dump_frame = false;

extern "C" void osSpTaskStartGo_recomp(uint8_t* rdram, recomp_context* ctx) {
    OSTask* task = TO_PTR(OSTask, ctx->r4);
    // Log task submissions
    {
        static int sp_go_count = 0;
        sp_go_count++;
        if (sp_go_count <= 10 || (sp_go_count % 500 == 0)) {
            fprintf(stderr, "[SP-GO] #%d type=%u data_ptr=0x%08X data_sz=0x%X\n",
                sp_go_count, task->t.type, task->t.data_ptr, task->t.data_size);
            fflush(stderr);
        }
    }
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
        // Run audio tasks synchronously on the game thread.
        ultramodern::rsp::run_task(rdram, task);
        ultramodern::send_sp_complete_message(PASS_RDRAM1);
    } else {
        // Run GFX tasks synchronously on the game thread to avoid external
        // message queue deadlock. The gfx thread's sp_complete/dp_complete
        // go through the external queue which starves when all game threads
        // are blocked on osRecvMesg.
        ultramodern::send_sp_complete_message(PASS_RDRAM1);
        f3ddkr_process_dl(rdram, task->t.data_ptr, task->t.data_size);
        ultramodern::send_dp_complete_message(PASS_RDRAM1);
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
