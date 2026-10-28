// Collects per-function call counters and, every MTC_TRACE_INTERVAL ms (default 1000), appends the
// functions called during the interval (with call counts) to $MTC_TRACE_FILE (default mtc_trace.txt).
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <algorithm>
#include "trace.h"

namespace {
struct Entry { const char* name; unsigned long long* counter; unsigned long long last; };
std::mutex mtx;
std::vector<Entry> entries;
std::once_flag started;

void dumper() {
    const char* f = std::getenv("MTC_TRACE_FILE");
    const char* iv = std::getenv("MTC_TRACE_INTERVAL");
    int interval = iv ? std::atoi(iv) : 1000;
    FILE* out = std::fopen(f ? f : "mtc_trace.txt", "w");
    if (!out) return;
    auto t0 = std::chrono::steady_clock::now();
    while (true) {
        std::this_thread::sleep_for(std::chrono::milliseconds(interval));
        std::vector<std::pair<unsigned long long, const char*>> hits;
        {
            std::lock_guard lock{ mtx };
            for (auto& e : entries) {
                unsigned long long c = __atomic_load_n(e.counter, __ATOMIC_RELAXED);
                if (c != e.last) { hits.emplace_back(c - e.last, e.name); e.last = c; }
            }
        }
        std::sort(hits.begin(), hits.end(), [](auto& a, auto& b) { return a.second < b.second; });
        double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        std::fprintf(out, "== t=%.1f n=%zu\n", t, hits.size());
        for (auto& [n, name] : hits) std::fprintf(out, "%s %llu\n", name, n);
        std::fflush(out);
    }
}
}

extern "C" void mtc_trace_register(const char* name, unsigned long long* counter) {
    std::call_once(started, [] { std::thread(dumper).detach(); });
    std::lock_guard lock{ mtx };
    entries.push_back(Entry{ name, counter, 0 });
}
