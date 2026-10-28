// ABI between MagicalBlocks (exe) and the game-code module (mtc_game.dll / libmtc_game.so) that is
// generated and compiled on the player's PC from their own ROM (see OPTB.md). Shipped source, no ROM data.
// The module links against nothing (-nostdlib): every runtime function the generated code calls is reached
// through MtcGameImports, filled in by the exe.
#ifndef MTC_GAME_ABI_H
#define MTC_GAME_ABI_H

#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>

#define MTC_GAME_ABI_VERSION 1

#ifdef __cplusplus
extern "C" {
#endif

// Include recomp.h (N64Recomp) and librecomp/sections.h before this header: recomp_context, gpr and recomp_func_t come from there.
typedef recomp_func_t mtc_recomp_func_t;

// libultra functions reimplemented by ultramodern/librecomp that the generated code calls (N64Recomp emits
// calls to <name>_recomp). Keep in sync with the generator output: the module link fails on a missing one.
#define MTC_IMPORT_RECOMP_FUNCS(X) \
    X(__osDisableInt_recomp) \
    X(__osMotorAccess_recomp) \
    X(__osRestoreInt_recomp) \
    X(__osSetFpcCsr_recomp) \
    X(__osSpSetPc_recomp) \
    X(osAiGetLength_recomp) \
    X(osAiGetStatus_recomp) \
    X(osAiSetFrequency_recomp) \
    X(osAiSetNextBuffer_recomp) \
    X(osContGetReadData_recomp) \
    X(osContInit_recomp) \
    X(osContStartReadData_recomp) \
    X(osCreateMesgQueue_recomp) \
    X(osCreatePiManager_recomp) \
    X(osCreateThread_recomp) \
    X(osCreateViManager_recomp) \
    X(osDestroyThread_recomp) \
    X(osEPiRawStartDma_recomp) \
    X(osGetCount_recomp) \
    X(osGetThreadPri_recomp) \
    X(osGetTime_recomp) \
    X(osInitialize_recomp) \
    X(osInvalDCache_recomp) \
    X(osInvalICache_recomp) \
    X(osJamMesg_recomp) \
    X(osMotorInit_recomp) \
    X(osPiStartDma_recomp) \
    X(osRecvMesg_recomp) \
    X(osSendMesg_recomp) \
    X(osSetEventMesg_recomp) \
    X(osSetIntMask_recomp) \
    X(osSetThreadPri_recomp) \
    X(osSetTimer_recomp) \
    X(osSpTaskLoad_recomp) \
    X(osSpTaskStartGo_recomp) \
    X(osSpTaskYield_recomp) \
    X(osSpTaskYielded_recomp) \
    X(osStartThread_recomp) \
    X(osStopThread_recomp) \
    X(osUnmapTLBAll_recomp) \
    X(osViBlack_recomp) \
    X(osViGetCurrentFramebuffer_recomp) \
    X(osViGetNextFramebuffer_recomp) \
    X(osViSetEvent_recomp) \
    X(osViSetMode_recomp) \
    X(osViSetSpecialFeatures_recomp) \
    X(osViSetYScale_recomp) \
    X(osViSwapBuffer_recomp) \
    X(osVirtualToPhysical_recomp) \
    X(osWritebackDCacheAll_recomp) \
    X(osWritebackDCache_recomp) \

typedef struct MtcGameImports {
    uint32_t abi_version;
    uint32_t struct_size;
#define MTC_X(name) mtc_recomp_func_t* name;
    MTC_IMPORT_RECOMP_FUNCS(MTC_X)
#undef MTC_X
    // librecomp/ultramodern helpers declared in recomp.h
    void (*cop0_status_write)(recomp_context* ctx, gpr value);
    gpr (*cop0_status_read)(recomp_context* ctx);
    void (*switch_error)(const char* func, uint32_t vram, uint32_t jtbl);
    void (*do_break)(uint32_t vram);
    mtc_recomp_func_t* (*get_function)(int32_t vram);
    void (*recomp_syscall_handler)(uint8_t* rdram, recomp_context* ctx, int32_t instruction_vram);
    void (*pause_self)(uint8_t* rdram);
    // app hooks (recomp config [[patches.hook]])
    void (*mtc_ws_on_bg_load)(uint8_t* rdram, uint32_t rom_offset);
    // C runtime pieces the compiler may emit calls to
    void* (*memcpy)(void* dst, const void* src, size_t n);
    void* (*memmove)(void* dst, const void* src, size_t n);
    void* (*memset)(void* dst, int c, size_t n);
    int (*memcmp)(const void* a, const void* b, size_t n);
    int (*vprintf)(const char* fmt, va_list ap);
    int (*fegetround)(void);
    int (*fesetround)(int mode);
    float (*sqrtf)(float);
    double (*sqrt)(double);
    void (*abort)(void);
} MtcGameImports;

// Include librecomp/sections.h before this header (SectionTableEntry).
typedef struct MtcGameExports {
    uint32_t abi_version;
    uint32_t struct_size;
    // The game's identity, so a stale module from another ROM/version is never used.
    const char* generator_stamp;
    // N64Recomp's recomp_overlays.inl tables (see src/main/register_overlays.cpp)
    SectionTableEntry* section_table;
    size_t num_code_sections;
    size_t total_num_sections;
    int* overlay_sections_by_index;
    size_t num_overlays;
    mtc_recomp_func_t* entrypoint;
    // RSPRecomp n_aspMain (audio microcode). Returns RspExitReason as int. Uses the module's own DMEM.
    int (*n_aspMain)(uint8_t* rdram, uint32_t ucode_addr);
    uint8_t* dmem;
} MtcGameExports;

// Exported by the module. Returns 0 on success.
typedef int (mtc_game_init_t)(const MtcGameImports* imports, MtcGameExports* exports);

#ifdef __cplusplus
}
#endif

#endif
