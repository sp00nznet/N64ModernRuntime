#include <cassert>
#include <cstring>
#include <cinttypes>

#include "rsp.hpp"

static recomp::rsp::callbacks_t rsp_callbacks {};

void recomp::rsp::set_callbacks(const callbacks_t& callbacks) {
    rsp_callbacks = callbacks;
}

uint8_t dmem[0x1000];
uint16_t rspReciprocals[512];
uint16_t rspInverseSquareRoots[512];

// From Ares emulator. For license details, see rsp_vu.h
void recomp::rsp::constants_init() {
    rspReciprocals[0] = u16(~0);
    for (u16 index = 1; index < 512; index++) {
        u64 a = index + 512;
        u64 b = (u64(1) << 34) / a;
        rspReciprocals[index] = u16((b + 1) >> 8);
    }

    for (u16 index = 0; index < 512; index++) {
        u64 a = (index + 512) >> ((index % 2 == 1) ? 1 : 0);
        u64 b = 1 << 17;
        //find the largest b where b < 1.0 / sqrt(a)
        while (a * (b + 1) * (b + 1) < (u64(1) << 44)) b++;
        rspInverseSquareRoots[index] = u16(b >> 1);
    }
}

// Read a native-endian 32-bit word from an RDRAM host pointer at a byte offset.
// MEM_W stores words in host-native (little-endian on x86) byte order, so we
// can simply dereference. The pointer arithmetic uses byte offsets.
static inline uint32_t read_task_u32(const uint8_t* task_base, uint32_t byte_offset) {
    return *reinterpret_cast<const uint32_t*>(task_base + byte_offset);
}

// N64 OSTask_s field byte offsets (all 32-bit on N64):
// 0x00: type, 0x04: flags, 0x08: ucode_boot, 0x0C: ucode_boot_size,
// 0x10: ucode, 0x14: ucode_size, 0x18: ucode_data, 0x1C: ucode_data_size,
// 0x20: dram_stack, 0x24: dram_stack_size, 0x28: output_buff, 0x2C: output_buff_size,
// 0x30: data_ptr, 0x34: data_size, 0x38: yield_data_ptr, 0x3C: yield_data_size
// Total: 0x40 bytes
//
// IMPORTANT: The host OSTask_s struct has 64-bit pointer fields (PTR(u64) = u64*),
// making its layout incompatible with the N64's 32-bit layout. We must read fields
// at their N64 byte offsets instead of using struct member access.

// Runs a recompiled RSP microcode
bool recomp::rsp::run_task(uint8_t* rdram, const OSTask* task) {
    assert(rsp_callbacks.get_rsp_microcode != nullptr);
    RspUcodeFunc* ucode_func = rsp_callbacks.get_rsp_microcode(task);

    if (ucode_func == nullptr) {
        fprintf(stderr, "No registered RSP ucode for %" PRIu32 " (returned `nullptr`)\n", task->t.type);
        return false;
    }

    // task points into RDRAM (via TO_PTR). Copy the raw N64 task bytes (0x40)
    // into DMEM at 0xFC0. memcpy copies the raw RDRAM bytes, and RSP_MEM_W_LOAD
    // in the ucode uses XOR-3 to read them back correctly.
    memcpy(&dmem[0xFC0], task, 0x40);

    // Read task fields at their correct N64 byte offsets from the RDRAM pointer
    const uint8_t* task_bytes = reinterpret_cast<const uint8_t*>(task);
    uint32_t n64_ucode           = read_task_u32(task_bytes, 0x10);
    uint32_t n64_ucode_data      = read_task_u32(task_bytes, 0x18);
    uint32_t n64_ucode_data_size = read_task_u32(task_bytes, 0x1C);

    // Load the ucode data into DMEM.
    // On real N64, the boot code only loads ucode_data_size bytes, so
    // DMEM beyond that offset preserves state from the previous task.
    // This is critical for aspMain: ENVMIXER params at DMEM[0x360] are
    // set by SAVEBUFF and must persist across tasks.
    {
        uint32_t data_sz = n64_ucode_data_size;
        if (data_sz == 0 || data_sz > 0xF80) data_sz = 0xF80;
        // ucode_data is an N64 virtual address (0x80XXXXXX), convert to physical
        uint32_t ucode_data_phys = n64_ucode_data & 0x7FFFFFFF;
        dma_rdram_to_dmem(rdram, 0x0000, ucode_data_phys, data_sz - 1);
    }

    // Run the ucode — pass N64 ucode address
    RspExitReason exit_reason = ucode_func(rdram, n64_ucode);

    // Ensure that the ucode exited correctly
    if (exit_reason != RspExitReason::Broke) {
        fprintf(stderr, "RSP ucode %" PRIu32 " exited unexpectedly. exit_reason: %i\n", task->t.type, static_cast<int>(exit_reason));
        assert(exit_reason == RspExitReason::Broke);
        return false;
    }

    return true;
}
