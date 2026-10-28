// Wraps N64Recomp's generated recomp_overlays.inl (function/section tables) for the game-code module.
#include "recomp_overlays.inl"
#include "librecomp/rsp.hpp"

SectionTableEntry* mtc_section_table = section_table;
size_t mtc_num_code_sections = ARRLEN(section_table);
size_t mtc_total_num_sections = num_sections;
int* mtc_overlay_sections_by_index = overlay_sections_by_index;
size_t mtc_num_overlays = ARRLEN(overlay_sections_by_index);

RspExitReason n_aspMain(uint8_t* rdram, uint32_t ucode_addr);
int mtc_n_aspMain_entry(uint8_t* rdram, uint32_t ucode_addr) { return (int)n_aspMain(rdram, ucode_addr); }
