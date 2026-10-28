// High frame rate glue (see APP.md, "High fps").
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <vector>

#include "render/rt64_hifps_api.h"
#include "mtc_config.h"
#include "mtc_hifps.h"

namespace {
    std::mutex mtx;
    void* app = nullptr;

    void push_locked() {
        if (app == nullptr) return;
        const mtc::ExtraGraphicsConfig& extra = mtc::get_extra_graphics_config();
        bool interpolate = extra.frame_interpolation;
        if (const char* env = std::getenv("MTC_HIFPS_INTERP")) interpolate = std::string(env) != "0";
        // A fixed limiter (90..238, Uncapped, Custom) is a real target: let it exceed the detected display rate,
        // with vsync off. "V-Sync" follows the display and "60" is the original pacing.
        const bool manual = extra.framerate != mtc::FramerateMode::Fps60 && extra.framerate != mtc::FramerateMode::VSync;
        rt64_hifps_set_interpolation(app, interpolate ? 1 : 0);
        rt64_hifps_set_allow_above_display(app, manual ? 1 : 0);
    }
}

// ---- Tag hooks ---------------------------------------------------------------------------------------------
// The game appends display list commands at the pointer stored in D_800D2FE4 (the "DL head"). A hooked draw
// function's commands are the bytes the head advanced by between its entry and its return, so that range gets
// the function's object id. Ranges are kept per display list buffer: the game double-buffers its display list
// (two 0x8000-byte buffers), builds the next list while RT64 draws the previous one, and send_dl hands RT64 the
// ranges of the list it is about to process.
namespace {
    constexpr uint32_t kDlHeadVram = 0x800D2FE4;
    constexpr uint32_t kDlBufferSize = 0x8000;
    constexpr size_t kMaxDepth = 64;

    struct Frame { uint32_t fn, id, head; };
    struct PendingRange { rt64_hifps_tag_range range; uint32_t age; };

    std::vector<Frame> call_stack;          // game thread only
    std::mutex tag_mtx;                     // pending (game thread <-> renderer thread)
    std::vector<PendingRange> pending;
    uint64_t hooked_calls = 0, tagged_ranges = 0, sent_ranges = 0, send_count = 0;

    int tags_mode() { // 1 = on (default when the hooks are compiled in), 0 = off via MTC_HIFPS_TAGS=0
        static const int mode = [] {
            const char* env = std::getenv("MTC_HIFPS_TAGS");
            return (env != nullptr && std::string(env) == "0") ? 0 : 1;
        }();
        return mode;
    }

    bool tags_log() {
        static const bool on = std::getenv("MTC_HIFPS_TAGS_LOG") != nullptr;
        return on;
    }

    uint32_t read_dl_head(const uint8_t* rdram) {
        // RDRAM is stored as native-endian 32-bit words.
        return *reinterpret_cast<const uint32_t*>(rdram + (kDlHeadVram - 0x80000000u));
    }

    uint32_t mix_id(uint32_t fn, uint32_t a0, uint32_t ra) {
        uint64_t h = 0x9E3779B97F4A7C15ull ^ fn;
        h = (h ^ a0) * 0xBF58476D1CE4E5B9ull;
        h = (h ^ ra) * 0x94D049BB133111EBull;
        h ^= h >> 31;
        const uint32_t id = uint32_t(h) ^ uint32_t(h >> 32);
        return (id == 0) ? 1 : id;
    }
}

extern "C" void mtc_hifps_fn_enter(uint8_t* rdram, uint32_t fn, uint32_t a0, uint32_t ra) {
    if (tags_mode() == 0) return;
    if (call_stack.size() >= kMaxDepth) call_stack.clear(); // unbalanced (tail calls): start over
    call_stack.push_back({ fn, mix_id(fn, a0, ra), read_dl_head(rdram) });
    hooked_calls++;
}

extern "C" void mtc_hifps_fn_exit(uint8_t* rdram, uint32_t fn) {
    if (tags_mode() == 0) return;
    // Unwind to this function's frame; frames above it belong to callees that left without a hooked return.
    // A return with no matching entry (a jr $ra inside a merged splat function) is ignored.
    size_t depth = call_stack.size();
    while (depth > 0 && call_stack[depth - 1].fn != fn) depth--;
    if (depth == 0) return;
    call_stack.resize(depth);
    const Frame frame = call_stack.back();
    call_stack.pop_back();
    const uint32_t head = read_dl_head(rdram);
    if (head <= frame.head || head - frame.head > kDlBufferSize) return; // nothing written, or the head was reset
    std::lock_guard lock{ tag_mtx };
    pending.push_back({ { frame.head & 0x00FFFFFFu, head & 0x00FFFFFFu, frame.id, RT64_HIFPS_MODE_AUTO }, 0 });
    tagged_ranges++;
}

void mtc::hifps::before_display_list(uint32_t dl_address) {
    if (tags_mode() == 0) return;
    void* target;
    {
        std::lock_guard lock{ mtx };
        target = app;
    }
    std::vector<rt64_hifps_tag_range> ranges;
    const uint32_t start = dl_address & 0x00FFFFFFu;
    const uint32_t end = start + kDlBufferSize;
    {
        std::lock_guard lock{ tag_mtx };
        if (pending.empty() && sent_ranges == 0) return; // hooks not compiled in
        size_t keep = 0;
        for (PendingRange& p : pending) {
            if (p.range.start >= start && p.range.start < end) {
                ranges.push_back(p.range);
            }
            else if (++p.age < 4) { // the other buffer (being built now); drop strays after a few lists
                pending[keep++] = p;
            }
        }
        pending.resize(keep);
        sent_ranges += ranges.size();
        send_count++;
        if (tags_log() && (send_count % 300) == 0) {
            std::fprintf(stderr, "[hifps-tags] lists=%llu hooked calls=%llu ranges=%llu sent=%llu (this list %zu)\n",
                (unsigned long long)send_count, (unsigned long long)hooked_calls, (unsigned long long)tagged_ranges,
                (unsigned long long)sent_ranges, ranges.size());
        }
    }
    if (target != nullptr) {
        rt64_hifps_set_tag_ranges(target, start, end, ranges.data(), unsigned(ranges.size()));
    }
}

void mtc::hifps::attach(void* rt64_app) {
    if (!rt64_hifps_supported()) return;
    std::lock_guard lock{ mtx };
    app = rt64_app;
    push_locked();
}

void mtc::hifps::detach(void* rt64_app) {
    std::lock_guard lock{ mtx };
    if (app == rt64_app) app = nullptr;
}

void mtc::hifps::refresh() {
    std::lock_guard lock{ mtx };
    push_locked();
}
