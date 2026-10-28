// Function-entry counters for N64Recomp trace_mode builds (diagnostics only; see tools/trace/README).
#ifndef MTC_TRACE_H
#define MTC_TRACE_H
#ifdef __cplusplus
extern "C" {
#endif
void mtc_trace_register(const char* name, unsigned long long* counter);
#ifdef __cplusplus
}
#endif
#define TRACE_ENTRY() { static unsigned long long mtc_trace_count_; static int mtc_trace_reg_; \
    __atomic_fetch_add(&mtc_trace_count_, 1, __ATOMIC_RELAXED); \
    if (!__atomic_exchange_n(&mtc_trace_reg_, 1, __ATOMIC_ACQ_REL)) mtc_trace_register(__func__, &mtc_trace_count_); }
#define TRACE_RETURN()
#endif
