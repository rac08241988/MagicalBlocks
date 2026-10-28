// Game-code module glue (compiled on the player's PC together with the code generated from their ROM).
// Shipped source; contains no ROM data. Provides the symbols the generated code links against by
// forwarding to the exe through MtcGameImports, and exports mtc_game_init.
#include "recomp.h"
#include "librecomp/sections.h"
#include "funcs.h"
#include "mtc_game_abi.h"
// Written by the app next to the generated sources: the identity (app version, ROM SHA1, ABI) of this build.
#include "mtc_game_stamp.h"

#if defined(_WIN32)
#define MTC_EXPORT __declspec(dllexport)
#else
#define MTC_EXPORT __attribute__((visibility("default")))
#endif

static const MtcGameImports* imp = nullptr;

// Tables from recomp_overlays.inl (mtc_game_tables.cpp) and the RSP microcode (n_aspMain.cpp).
extern SectionTableEntry* mtc_section_table;
extern size_t mtc_num_code_sections;
extern size_t mtc_total_num_sections;
extern int* mtc_overlay_sections_by_index;
extern size_t mtc_num_overlays;
int mtc_n_aspMain_entry(uint8_t* rdram, uint32_t ucode_addr);

extern "C" {
    // RSP DMEM used by the module's n_aspMain (the exe copies its DMEM in/out around each task).
    uint8_t dmem[0x1000];

#define MTC_X(name) void name(uint8_t* rdram, recomp_context* ctx) { imp->name(rdram, ctx); }
    MTC_IMPORT_RECOMP_FUNCS(MTC_X)
#undef MTC_X

    void cop0_status_write(recomp_context* ctx, gpr value) { imp->cop0_status_write(ctx, value); }
    gpr cop0_status_read(recomp_context* ctx) { return imp->cop0_status_read(ctx); }
    void switch_error(const char* func, uint32_t vram, uint32_t jtbl) { imp->switch_error(func, vram, jtbl); }
    void do_break(uint32_t vram) { imp->do_break(vram); }
    recomp_func_t* get_function(int32_t vram) { return imp->get_function(vram); }
    void recomp_syscall_handler(uint8_t* rdram, recomp_context* ctx, int32_t instruction_vram) { imp->recomp_syscall_handler(rdram, ctx, instruction_vram); }
    void pause_self(uint8_t* rdram) { imp->pause_self(rdram); }
    void mtc_ws_on_bg_load(uint8_t* rdram, uint32_t rom_offset) { imp->mtc_ws_on_bg_load(rdram, rom_offset); }

    void* memcpy(void* dst, const void* src, size_t n) { return imp->memcpy(dst, src, n); }
    void* memmove(void* dst, const void* src, size_t n) { return imp->memmove(dst, src, n); }
    void* memset(void* dst, int c, size_t n) { return imp->memset(dst, c, n); }
    int memcmp(const void* a, const void* b, size_t n) { return imp->memcmp(a, b, n); }
    int printf(const char* fmt, ...) {
        va_list ap;
        va_start(ap, fmt);
        int r = imp->vprintf(fmt, ap);
        va_end(ap);
        return r;
    }
    int fegetround(void) { return imp->fegetround(); }
    int fesetround(int mode) { return imp->fesetround(mode); }
    float sqrtf(float v) { return imp->sqrtf(v); }
    double sqrt(double v) { return imp->sqrt(v); }
    void abort(void) { imp->abort(); for (;;) {} }

#if defined(_WIN32)
    // -nostdlib: minimal DLL entry point, nothing to initialise (no C++ globals with constructors).
    int __stdcall DllMainCRTStartup(void*, unsigned long, void*) { return 1; }
#endif

    MTC_EXPORT int mtc_game_init(const MtcGameImports* imports, MtcGameExports* exports) {
        if (imports == nullptr || exports == nullptr) return 1;
        if (imports->abi_version != MTC_GAME_ABI_VERSION || imports->struct_size != sizeof(MtcGameImports)) return 2;
        if (exports->struct_size != sizeof(MtcGameExports)) return 3;
        imp = imports;
        exports->abi_version = MTC_GAME_ABI_VERSION;
        exports->generator_stamp = MTC_GAME_STAMP;
        exports->section_table = mtc_section_table;
        exports->num_code_sections = mtc_num_code_sections;
        exports->total_num_sections = mtc_total_num_sections;
        exports->overlay_sections_by_index = mtc_overlay_sections_by_index;
        exports->num_overlays = mtc_num_overlays;
        exports->entrypoint = recomp_entrypoint;
        exports->n_aspMain = mtc_n_aspMain_entry;
        exports->dmem = dmem;
        return 0;
    }
}
