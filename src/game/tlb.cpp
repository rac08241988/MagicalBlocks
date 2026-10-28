// Native replacements for Capcom's hand-written TLB helpers in the supported ROM (USA).
//
//   capcom_TlbReadEntry  0x80057E00  (index, out[4])       tlbr, returns out
//   capcom_TlbWriteEntry 0x80057E74  (index, in[4])        tlbwi
//   capcom_TlbUnmapAll   0x800581D0  ()                    writes 32 never-matching kseg0 entries
//   capcom_TlbMap        0x80058258  (index, vaddr, paddr, page_size_index, cached) -> 1 ok / 0 bad page size
//
// N64Recomp cannot translate cop0/TLB instructions, and the recompiled code accesses RDRAM directly
// (no address translation), so a real TLB mapping can't be honoured. Static analysis shows these four
// functions are dead code in this ROM (no jal callers outside each other, no function pointers to them),
// so they are listed as `ignored` in mtc.us.toml and implemented here as a logged shadow TLB. If the game
// ever creates a mapping for a kuseg/kseg2/kseg3 address we print a loud warning so it can be investigated.
//
// Struct layout used by Read/Write (from the asm):
//   word0 <-> EntryHi ($10), word1 <-> EntryLo0 ($2), word2 <-> EntryLo1 ($3), word3 <-> PageMask ($5)
#include <array>
#include <atomic>
#include <cstdio>
#include "recomp.h"
#include "librecomp/helpers.hpp"

namespace {
    struct TlbEntry {
        uint32_t entry_hi = 0;
        uint32_t entry_lo0 = 0;
        uint32_t entry_lo1 = 0;
        uint32_t page_mask = 0;
    };
    std::array<TlbEntry, 32> shadow_tlb{};
    std::atomic<int> warn_count{ 0 };

    // Page-size table at 0x800A5FA0 (12-byte entries: {index, odd_page_offset, page_mask}) used by TlbMap.
    constexpr uint32_t page_size_table_vram = 0x800A5FA0;

    bool is_real_mapping(uint32_t entry_hi, uint32_t lo0, uint32_t lo1) {
        uint32_t vaddr = entry_hi & 0xFFFFE000u;
        bool unmapped_segment = (vaddr >= 0x80000000u && vaddr < 0xC0000000u); // kseg0/kseg1 never go through the TLB
        bool valid = ((lo0 | lo1) & 0x2) != 0;                                   // V bit
        return valid && !unmapped_segment;
    }

    void write_entry(uint32_t index, const TlbEntry& e, const char* who) {
        index &= 31;
        shadow_tlb[index] = e;
        if (is_real_mapping(e.entry_hi, e.entry_lo0, e.entry_lo1)) {
            if (warn_count.fetch_add(1) < 64) {
                fprintf(stderr,
                    "[mtc][TLB] WARNING: %s created a real TLB mapping (index %u: EntryHi=%08X EntryLo0=%08X EntryLo1=%08X PageMask=%08X).\n"
                    "           Recompiled code does not translate addresses; accesses through this mapping will hit the wrong memory.\n",
                    who, index, e.entry_hi, e.entry_lo0, e.entry_lo1, e.page_mask);
            }
        }
    }
}

extern "C" void capcom_TlbReadEntry(uint8_t* rdram, recomp_context* ctx) {
    uint32_t index = _arg<0, uint32_t>(rdram, ctx) & 31;
    gpr out = ctx->r5;
    const TlbEntry& e = shadow_tlb[index];
    MEM_W(0x0, out) = e.entry_hi;
    MEM_W(0x4, out) = e.entry_lo0;
    MEM_W(0x8, out) = e.entry_lo1;
    MEM_W(0xC, out) = e.page_mask;
    ctx->r2 = out;
}

extern "C" void capcom_TlbWriteEntry(uint8_t* rdram, recomp_context* ctx) {
    uint32_t index = _arg<0, uint32_t>(rdram, ctx);
    gpr in = ctx->r5;
    TlbEntry e{ (uint32_t)MEM_W(0x0, in), (uint32_t)MEM_W(0x4, in), (uint32_t)MEM_W(0x8, in), (uint32_t)MEM_W(0xC, in) };
    write_entry(index, e, "capcom_TlbWriteEntry");
    ctx->r2 = in;
}

extern "C" void capcom_TlbUnmapAll(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    // Original: for i in 0..31: EntryHi = kseg0 address (0x80000000 + i*0x2000), EntryLo0/1 = 0, PageMask = 0x2000 (sic).
    for (uint32_t i = 0; i < 32; i++) {
        shadow_tlb[i] = TlbEntry{ 0x80000000u + i * 0x2000u, 0, 0, 0x2000 };
    }
    static bool logged = false;
    if (!logged) {
        logged = true;
        fprintf(stderr, "[mtc][TLB] capcom_TlbUnmapAll called (harmless: all entries invalidated in the shadow TLB)\n");
    }
    ctx->r2 = 0;
}

extern "C" void capcom_TlbMap(uint8_t* rdram, recomp_context* ctx) {
    uint32_t index = _arg<0, uint32_t>(rdram, ctx);
    uint32_t vaddr = _arg<1, uint32_t>(rdram, ctx);
    uint32_t paddr = _arg<2, uint32_t>(rdram, ctx);
    uint32_t size_index = _arg<3, uint32_t>(rdram, ctx);
    uint32_t cached = _arg<4, uint32_t>(rdram, ctx);

    gpr table_entry = (int32_t)(page_size_table_vram + size_index * 12);
    if ((uint32_t)MEM_W(0x0, table_entry) != size_index) {
        ctx->r2 = 0;
        return;
    }
    uint32_t odd_offset = MEM_W(0x4, table_entry);
    uint32_t page_mask = MEM_W(0x8, table_entry);
    uint32_t attr = ((cached != 0 ? 3u : 0u) | 2u) << 3;
    TlbEntry e{ vaddr,
                ((paddr >> 6) | attr | 7u),
                (((paddr + odd_offset) >> 6) | attr | 7u),
                page_mask };
    fprintf(stderr, "[mtc][TLB] capcom_TlbMap(index=%u, vaddr=%08X, paddr=%08X, size_index=%u, cached=%u)\n",
        index, vaddr, paddr, size_index, cached);
    write_entry(index, e, "capcom_TlbMap");
    ctx->r2 = 1;
}
