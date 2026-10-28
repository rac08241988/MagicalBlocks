// Persistent log file + crash reporting. See include/mtc_crash.h and docs/CRASH-REPORTS.md.
#include "mtc_crash.h"

#include <algorithm>
#include <atomic>
#include <vector>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <exception>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dbghelp.h>
#if defined(__clang__) && defined(__MINGW32__)
#include <cstddef>
#include <cxxabi.h>
#include <typeinfo>
#include <unwind.h>
#endif
#include <fcntl.h>
#include <io.h>
#else
#include <execinfo.h>
#include <fcntl.h>
#include <sys/utsname.h>
#include <unistd.h>
#endif

namespace {
    std::filesystem::path g_logs;
    std::string g_version = "?";
    std::mutex g_log_mutex;
    FILE* g_log_file = nullptr;            // latest.log (written by the tee thread and note())
    int g_console_fd = -1;                 // original stdout (console / terminal), -1 if none
    std::atomic<bool> g_clean_exit{ false };
    std::atomic<bool> g_crashing{ false };

    std::string timestamp(bool for_file) {
        std::time_t t = std::time(nullptr);
        std::tm tmv{};
#ifdef _WIN32
        localtime_s(&tmv, &t);
#else
        localtime_r(&t, &tmv);
#endif
        char buf[64];
        std::strftime(buf, sizeof(buf), for_file ? "%Y%m%d-%H%M%S" : "%Y-%m-%d %H:%M:%S", &tmv);
        return buf;
    }

    void write_log_raw(const char* data, size_t size) {
        std::lock_guard lock{ g_log_mutex };
        if (g_log_file != nullptr) {
            fwrite(data, 1, size, g_log_file);
            fflush(g_log_file);
        }
    }

    void rotate_logs() {
        std::error_code ec;
        std::filesystem::create_directories(g_logs, ec);
        constexpr int keep = 5;
        std::filesystem::remove(g_logs / ("previous-" + std::to_string(keep - 1) + ".log"), ec);
        for (int i = keep - 2; i >= 1; i--) {
            std::filesystem::rename(g_logs / ("previous-" + std::to_string(i) + ".log"), g_logs / ("previous-" + std::to_string(i + 1) + ".log"), ec);
        }
        std::filesystem::rename(g_logs / "latest.log", g_logs / "previous-1.log", ec);
        // Old crash reports: keep the newest 10 of each kind.
        std::vector<std::filesystem::path> old;
        for (const auto& e : std::filesystem::directory_iterator(g_logs, ec)) {
            const std::string name = e.path().filename().string();
            if (name.rfind("crash-", 0) == 0) old.push_back(e.path());
        }
        std::sort(old.begin(), old.end());
        while (old.size() > 20) { std::filesystem::remove(old.front(), ec); old.erase(old.begin()); }
    }

    FILE* open_file_for_append(const std::filesystem::path& path) {
#ifdef _WIN32
        return _wfopen(path.c_str(), L"ab");
#else
        return fopen(path.c_str(), "ab");
#endif
    }

    std::string os_description() {
#ifdef _WIN32
        using RtlGetVersionFn = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
        RTL_OSVERSIONINFOW vi{};
        vi.dwOSVersionInfoSize = sizeof(vi);
        if (HMODULE ntdll = GetModuleHandleW(L"ntdll.dll")) {
            if (auto fn = reinterpret_cast<RtlGetVersionFn>(GetProcAddress(ntdll, "RtlGetVersion"))) fn(&vi);
        }
        std::ostringstream ss;
        ss << "Windows " << vi.dwMajorVersion << "." << vi.dwMinorVersion << " build " << vi.dwBuildNumber;
        if (HMODULE ntdll = GetModuleHandleW(L"ntdll.dll")) {
            if (GetProcAddress(ntdll, "wine_get_version") != nullptr) ss << " (wine)";
        }
        SYSTEM_INFO si{};
        GetNativeSystemInfo(&si);
        ss << ", " << si.dwNumberOfProcessors << " logical CPUs";
        return ss.str();
#else
        utsname u{};
        uname(&u);
        return std::string(u.sysname) + " " + u.release + " " + u.machine;
#endif
    }

    std::string path_utf8(const std::filesystem::path& p) {
        const std::u8string s = p.u8string();
        return std::string(reinterpret_cast<const char*>(s.data()), s.size());
    }

#ifdef _WIN32
    // ---------------------------------------------------------------------------------------------------------
    // Windows: tee via a CRT pipe, SEH/CRT handlers, DbgHelp stack walk + minidump.
    void tee_thread(int read_fd) {
        char buf[4096];
        for (;;) {
            const int n = _read(read_fd, buf, sizeof(buf));
            if (n <= 0) break;
            write_log_raw(buf, size_t(n));
            if (g_console_fd >= 0) _write(g_console_fd, buf, unsigned(n));
        }
    }

    std::string module_of(DWORD64 addr, DWORD64& offset) {
        HMODULE mod = nullptr;
        offset = addr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCWSTR>(addr), &mod) || mod == nullptr) {
            return "?";
        }
        wchar_t name[MAX_PATH] = {};
        GetModuleFileNameW(mod, name, MAX_PATH);
        offset = addr - reinterpret_cast<DWORD64>(mod);
        return path_utf8(std::filesystem::path(name).filename());
    }

    const char* exception_name(DWORD code) {
        switch (code) {
            case EXCEPTION_ACCESS_VIOLATION: return "EXCEPTION_ACCESS_VIOLATION";
            case EXCEPTION_STACK_OVERFLOW: return "EXCEPTION_STACK_OVERFLOW";
            case EXCEPTION_ILLEGAL_INSTRUCTION: return "EXCEPTION_ILLEGAL_INSTRUCTION";
            case EXCEPTION_INT_DIVIDE_BY_ZERO: return "EXCEPTION_INT_DIVIDE_BY_ZERO";
            case EXCEPTION_PRIV_INSTRUCTION: return "EXCEPTION_PRIV_INSTRUCTION";
            case EXCEPTION_IN_PAGE_ERROR: return "EXCEPTION_IN_PAGE_ERROR";
            case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return "EXCEPTION_ARRAY_BOUNDS_EXCEEDED";
            case EXCEPTION_DATATYPE_MISALIGNMENT: return "EXCEPTION_DATATYPE_MISALIGNMENT";
            case 0xC0000409: return "STATUS_STACK_BUFFER_OVERRUN (fast fail)";
            case 0xC0000374: return "STATUS_HEAP_CORRUPTION";
            case 0xE06D7363: return "C++ exception";
            case 0x20474343: return "C++ exception (libc++/libunwind)";
            case 0xC000000D: return "STATUS_INVALID_PARAMETER";
            case 0xC0000025: return "pure virtual call";
            case 0xE0000001: return "std::terminate";
            case 0xE0000002: return "abort()";
            case 0xE0000003: return "unexpected exit";
            default: return "unknown";
        }
    }

    struct CrashJob {
        std::string kind;            // "exception", "abort", "terminate", ...
        std::string detail;
        DWORD code = 0;
        EXCEPTION_POINTERS* pointers = nullptr;
        CONTEXT context{};
        DWORD thread_id = 0;
        std::filesystem::path report, dump;
    };

    void write_stack(std::ostream& out, CrashJob& job) {
        HANDLE process = GetCurrentProcess();
        HANDLE thread = OpenThread(THREAD_ALL_ACCESS, FALSE, job.thread_id);
        SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES | SYMOPT_FAIL_CRITICAL_ERRORS);
        static bool sym_ready = SymInitializeW(process, nullptr, TRUE) != FALSE;
        CONTEXT ctx = job.context;
        STACKFRAME64 frame{};
        frame.AddrPC.Offset = ctx.Rip;    frame.AddrPC.Mode = AddrModeFlat;
        frame.AddrFrame.Offset = ctx.Rbp; frame.AddrFrame.Mode = AddrModeFlat;
        frame.AddrStack.Offset = ctx.Rsp; frame.AddrStack.Mode = AddrModeFlat;
        out << "Stack (module+offset; symbolize with tools/symbolize_crash.py and the build's .map):\n";
        for (int i = 0; i < 64; i++) {
            if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, thread != nullptr ? thread : GetCurrentThread(), &frame, &ctx,
                    nullptr, SymFunctionTableAccess64, SymGetModuleBase64, nullptr)) break;
            const DWORD64 pc = frame.AddrPC.Offset;
            if (pc == 0) break;
            DWORD64 off = 0;
            const std::string mod = module_of(pc, off);
            char line[512];
            snprintf(line, sizeof(line), "  #%02d %016llX %s+0x%llX", i, (unsigned long long)pc, mod.c_str(), (unsigned long long)off);
            out << line;
            if (sym_ready) {
                alignas(SYMBOL_INFO) char symbuf[sizeof(SYMBOL_INFO) + 256] = {};
                auto* sym = reinterpret_cast<SYMBOL_INFO*>(symbuf);
                sym->SizeOfStruct = sizeof(SYMBOL_INFO);
                sym->MaxNameLen = 255;
                DWORD64 disp = 0;
                if (SymFromAddr(process, pc, &disp, sym)) out << "  " << sym->Name << "+0x" << std::hex << disp << std::dec;
            }
            out << "\n";
        }
        if (thread != nullptr) CloseHandle(thread);
    }

    void write_dump(CrashJob& job) {
        HANDLE file = CreateFileW(job.dump.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) return;
        MINIDUMP_EXCEPTION_INFORMATION mei{};
        EXCEPTION_RECORD record{};
        EXCEPTION_POINTERS fake{};
        mei.ThreadId = job.thread_id;
        mei.ClientPointers = FALSE;
        if (job.pointers != nullptr) {
            mei.ExceptionPointers = job.pointers;
        }
        else {
            record.ExceptionCode = job.code;
            record.ExceptionAddress = reinterpret_cast<PVOID>(job.context.Rip);
            fake.ExceptionRecord = &record;
            fake.ContextRecord = &job.context;
            mei.ExceptionPointers = &fake;
        }
        const auto type = MINIDUMP_TYPE(MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules | MiniDumpWithIndirectlyReferencedMemory |
            MiniDumpWithDataSegs | MiniDumpWithHandleData);
        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file, type, &mei, nullptr, nullptr);
        CloseHandle(file);
    }

    void write_report(CrashJob& job) {
        const std::string stamp = timestamp(true);
        job.report = g_logs / ("crash-" + stamp + ".txt");
        job.dump = g_logs / ("crash-" + stamp + ".dmp");
        std::ostringstream out;
        out << "Magical Blocks " << g_version << " crash report\n";
        out << "Time: " << timestamp(false) << "\nOS: " << os_description() << "\n";
        out << "Kind: " << job.kind << "\n";
        if (!job.detail.empty()) out << "Detail: " << job.detail << "\n";
        char line[256];
        DWORD64 off = 0;
        const DWORD64 addr = (job.pointers != nullptr) ? reinterpret_cast<DWORD64>(job.pointers->ExceptionRecord->ExceptionAddress) : job.context.Rip;
        const std::string mod = module_of(addr, off);
        snprintf(line, sizeof(line), "Code: 0x%08lX (%s)\nAddress: %016llX = %s+0x%llX\nThread: %lu\n",
            (unsigned long)job.code, exception_name(job.code), (unsigned long long)addr, mod.c_str(), (unsigned long long)off, (unsigned long)job.thread_id);
        out << line;
        if (job.pointers != nullptr && job.code == EXCEPTION_ACCESS_VIOLATION && job.pointers->ExceptionRecord->NumberParameters >= 2) {
            const ULONG_PTR* p = job.pointers->ExceptionRecord->ExceptionInformation;
            snprintf(line, sizeof(line), "Access violation: %s at %016llX\n", p[0] == 0 ? "read" : (p[0] == 1 ? "write" : "execute"), (unsigned long long)p[1]);
            out << line;
        }
        const CONTEXT& c = job.context;
        snprintf(line, sizeof(line), "RAX=%016llX RBX=%016llX RCX=%016llX RDX=%016llX\n", c.Rax, c.Rbx, c.Rcx, c.Rdx); out << line;
        snprintf(line, sizeof(line), "RSI=%016llX RDI=%016llX RBP=%016llX RSP=%016llX\n", c.Rsi, c.Rdi, c.Rbp, c.Rsp); out << line;
        snprintf(line, sizeof(line), "R8 =%016llX R9 =%016llX R10=%016llX R11=%016llX\n", c.R8, c.R9, c.R10, c.R11); out << line;
        snprintf(line, sizeof(line), "R12=%016llX R13=%016llX R14=%016llX R15=%016llX RIP=%016llX\n", c.R12, c.R13, c.R14, c.R15, c.Rip); out << line;
        write_stack(out, job);
        // Loaded modules (base addresses) so offsets can be resolved without the dump.
        out << "Modules:\n";
        HMODULE mods[512];
        DWORD needed = 0;
        using EnumFn = BOOL(WINAPI*)(HANDLE, HMODULE*, DWORD, LPDWORD);
        static EnumFn enum_modules = reinterpret_cast<EnumFn>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "K32EnumProcessModules"));
        if (enum_modules != nullptr && enum_modules(GetCurrentProcess(), mods, sizeof(mods), &needed)) {
            for (DWORD i = 0; i < needed / sizeof(HMODULE) && i < 512; i++) {
                wchar_t name[MAX_PATH] = {};
                GetModuleFileNameW(mods[i], name, MAX_PATH);
                snprintf(line, sizeof(line), "  %016llX ", (unsigned long long)reinterpret_cast<DWORD64>(mods[i]));
                out << line << path_utf8(std::filesystem::path(name).filename()) << "\n";
            }
        }
        out << "Log: " << path_utf8(g_logs / "latest.log") << "\n";
        const std::string text = out.str();
        {
            std::ofstream f{ job.report, std::ios::binary };
            f << text;
        }
        write_dump(job);
        const std::string marker = "\n[crash] " + job.kind + " - report: " + path_utf8(job.report) + "\n";
        write_log_raw(marker.data(), marker.size());
        write_log_raw(text.data(), text.size());
    }

    void show_box(const CrashJob& job) {
        std::wstring msg = L"Magical Blocks has stopped because of an error (" +
            std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(job.kind.c_str()))).wstring() +
            L").\n\nA crash report was saved to:\n" + job.report.wstring() +
            L"\n\nThe log of this session:\n" + (g_logs / "latest.log").wstring() +
            L"\n\nPlease attach both files (and the .dmp file if you can) when you report the problem.";
        MessageBoxW(nullptr, msg.c_str(), L"Magical Blocks", MB_OK | MB_ICONERROR | MB_SETFOREGROUND | MB_TOPMOST);
    }

    // Runs the report on a fresh thread: the crashing thread may have no stack left (stack overflow).
    void handle_crash(CrashJob& job, bool terminate_after) {
        if (g_crashing.exchange(true)) {
            Sleep(INFINITE); // another thread is already reporting
        }
        std::thread worker([&job] {
            write_report(job);
            show_box(job);
        });
        worker.join();
        if (terminate_after) {
            TerminateProcess(GetCurrentProcess(), job.code != 0 ? job.code : 3);
        }
    }

    bool readable(const void* p, size_t n) {
        MEMORY_BASIC_INFORMATION mbi{};
        if (p == nullptr || VirtualQuery(p, &mbi, sizeof(mbi)) == 0) return false;
        if (mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) != 0) return false;
        return reinterpret_cast<const char*>(p) + n <= reinterpret_cast<const char*>(mbi.BaseAddress) + mbi.RegionSize;
    }

#if defined(__clang__) && defined(__MINGW32__)
    // libc++abi's __cxa_exception (64-bit layout) ends with the _Unwind_Exception that libunwind's SEH
    // implementation passes as ExceptionInformation[0] of STATUS_GCC_THROW (0x20474343); the thrown
    // object follows it. Used only to print the type and what() of an uncaught C++ exception (e.g. an
    // RT64 shader compiler error thrown on one of its worker threads), which a release build would
    // otherwise never show anywhere.
    struct CxaExceptionMirror {
        size_t referenceCount;
        std::type_info* exceptionType;
        void (*exceptionDestructor)(void*);
        void (*unexpectedHandler)();
        void (*terminateHandler)();
        void* nextException;
        int handlerCount;
        int handlerSwitchValue;
        const unsigned char* actionRecord;
        const unsigned char* languageSpecificData;
        void* catchTemp;
        void* adjustedPtr;
        _Unwind_Exception unwindHeader;
    };

    std::string describe_cxx_exception(const EXCEPTION_RECORD* rec) {
        if (rec->ExceptionCode != 0x20474343 || rec->NumberParameters < 1) return "";
        auto* ue = reinterpret_cast<_Unwind_Exception*>(rec->ExceptionInformation[0]);
        if (!readable(ue, sizeof(_Unwind_Exception))) return "";
        auto* header = reinterpret_cast<CxaExceptionMirror*>(reinterpret_cast<char*>(ue) - offsetof(CxaExceptionMirror, unwindHeader));
        if (!readable(header, sizeof(CxaExceptionMirror)) || !readable(header->exceptionType, sizeof(void*) * 2)) return "";
        std::string type = header->exceptionType->name();
        int status = 0;
        if (char* demangled = abi::__cxa_demangle(type.c_str(), nullptr, nullptr, &status)) {
            if (status == 0) type = demangled;
            std::free(demangled);
        }
        std::string out = "C++ exception of type " + type;
        // All std:: exception classes derive from std::exception with it as the primary base.
        if (type.rfind("std::", 0) == 0 && type.find("exception_ptr") == std::string::npos) {
            auto* object = reinterpret_cast<const std::exception*>(ue + 1);
            if (readable(object, sizeof(void*)) && readable(*reinterpret_cast<void* const*>(object), sizeof(void*))) {
                const char* what = object->what();
                if (what != nullptr && readable(what, 1)) {
                    out += ": ";
                    out.append(what, strnlen(what, 8192));
                }
            }
        }
        return out;
    }
#else
    std::string describe_cxx_exception(const EXCEPTION_RECORD*) { return ""; }
#endif

    LONG WINAPI unhandled_filter(EXCEPTION_POINTERS* ep) {
        CrashJob job;
        job.kind = "unhandled exception";
        job.detail = describe_cxx_exception(ep->ExceptionRecord);
        job.code = ep->ExceptionRecord->ExceptionCode;
        job.pointers = ep;
        job.context = *ep->ContextRecord;
        job.thread_id = GetCurrentThreadId();
        handle_crash(job, true);
        return EXCEPTION_EXECUTE_HANDLER;
    }

    // Stack overflows can leave too little stack for the unhandled filter itself; catch them first.
    LONG WINAPI vectored_stack_overflow(EXCEPTION_POINTERS* ep) {
        if (ep->ExceptionRecord->ExceptionCode == EXCEPTION_STACK_OVERFLOW) {
            return unhandled_filter(ep);
        }
        return EXCEPTION_CONTINUE_SEARCH;
    }

    void report_here(const char* kind, const std::string& detail, DWORD code) {
        CrashJob job;
        job.kind = kind;
        job.detail = detail;
        job.code = code;
        RtlCaptureContext(&job.context);
        job.thread_id = GetCurrentThreadId();
        handle_crash(job, true);
    }

    void on_invalid_parameter(const wchar_t*, const wchar_t*, const wchar_t*, unsigned, uintptr_t) {
        report_here("CRT invalid parameter", "", 0xC000000D);
    }

    void on_purecall() {
        report_here("pure virtual function call", "", 0xC0000025);
    }
#else
    // ---------------------------------------------------------------------------------------------------------
    // Linux/macOS: tee via a pipe, signal handlers with backtrace().
    void tee_thread(int read_fd) {
        char buf[4096];
        for (;;) {
            const ssize_t n = read(read_fd, buf, sizeof(buf));
            if (n <= 0) break;
            write_log_raw(buf, size_t(n));
            if (g_console_fd >= 0) { ssize_t w = write(g_console_fd, buf, size_t(n)); (void)w; }
        }
    }

    void report_here(const char* kind, const std::string& detail, int code) {
        if (g_crashing.exchange(true)) { for (;;) pause(); }
        const std::filesystem::path report = g_logs / ("crash-" + timestamp(true) + ".txt");
        std::ostringstream out;
        out << "Magical Blocks " << g_version << " crash report\nTime: " << timestamp(false)
            << "\nOS: " << os_description() << "\nKind: " << kind << "\n";
        if (!detail.empty()) out << "Detail: " << detail << "\n";
        out << "Code: " << code << "\nStack:\n";
        void* frames[64];
        const int n = backtrace(frames, 64);
        char** syms = backtrace_symbols(frames, n);
        for (int i = 0; i < n; i++) out << "  #" << i << " " << (syms ? syms[i] : "?") << "\n";
        const std::string text = out.str();
        { std::ofstream f{ report }; f << text; }
        const std::string marker = "\n[crash] " + std::string(kind) + " - report: " + path_utf8(report) + "\n";
        write_log_raw(marker.data(), marker.size());
        write_log_raw(text.data(), text.size());
        fprintf(stderr, "%s", marker.c_str());
    }

    void on_signal(int sig) {
        report_here("fatal signal", strsignal(sig), sig);
        signal(sig, SIG_DFL);
        raise(sig);
    }
#endif

    std::string current_exception_text() {
        if (std::exception_ptr e = std::current_exception()) {
            try { std::rethrow_exception(e); }
            catch (const std::exception& ex) { return std::string("uncaught exception: ") + ex.what(); }
            catch (...) { return "uncaught non-standard exception"; }
        }
        return "std::terminate without an active exception";
    }

    void on_terminate() {
        report_here("std::terminate", current_exception_text(), 0xE0000001);
        std::_Exit(3);
    }

    void on_abort(int) {
        report_here("abort()", "", 0xE0000002);
        std::_Exit(3);
    }

    void on_exit_hook() {
        if (g_clean_exit.load() || g_crashing.load()) return;
        // exit()/quick_exit() without a normal quit: librecomp uses these for fatal emulation errors
        // (unknown function, jump table out of range, break) after printing the reason to stderr.
        report_here("unexpected exit", "exit()/quick_exit() called without a normal quit; see the log for the reason", 0xE0000003);
    }
}

void mtc::crash::install_handlers() {
#ifdef _WIN32
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    SetUnhandledExceptionFilter(unhandled_filter);
    AddVectoredExceptionHandler(1, vectored_stack_overflow);
    _set_invalid_parameter_handler(on_invalid_parameter);
    _set_purecall_handler(on_purecall);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    prepare_thread();
#else
    for (int sig : { SIGSEGV, SIGBUS, SIGILL, SIGFPE }) signal(sig, on_signal);
#endif
    signal(SIGABRT, on_abort);
    std::set_terminate(on_terminate);
    std::atexit(on_exit_hook);
    std::at_quick_exit(on_exit_hook);
}

void mtc::crash::prepare_thread() {
#ifdef _WIN32
    ULONG guarantee = 64 * 1024;
    SetThreadStackGuarantee(&guarantee);
#endif
}

void mtc::crash::mark_clean_exit() {
    g_clean_exit.store(true);
}

std::filesystem::path mtc::crash::logs_dir() {
    return g_logs;
}

void mtc::crash::note(const std::string& line) {
    const std::string text = "[note] " + line + "\n";
    write_log_raw(text.data(), text.size());
}

void mtc::crash::init_logging(const std::filesystem::path& app_folder, const std::string& version) {
    g_version = version;
    g_logs = app_folder / "logs";
    rotate_logs();
    {
        std::lock_guard lock{ g_log_mutex };
        g_log_file = open_file_for_append(g_logs / "latest.log");
    }
    if (g_log_file == nullptr) return;

    fflush(stdout);
    fflush(stderr);
#ifdef _WIN32
    // GUI-subsystem process: stdout/stderr may have no descriptor yet.
    if (_fileno(stdout) < 0) freopen("NUL", "w", stdout);
    if (_fileno(stderr) < 0) freopen("NUL", "w", stderr);
    if (GetConsoleWindow() != nullptr) g_console_fd = _dup(_fileno(stdout));
    int fds[2];
    if (_pipe(fds, 1 << 16, _O_BINARY) == 0) {
        _dup2(fds[1], _fileno(stdout));
        _dup2(fds[1], _fileno(stderr));
        _close(fds[1]);
        SetStdHandle(STD_OUTPUT_HANDLE, reinterpret_cast<HANDLE>(_get_osfhandle(_fileno(stdout))));
        SetStdHandle(STD_ERROR_HANDLE, reinterpret_cast<HANDLE>(_get_osfhandle(_fileno(stderr))));
        std::thread(tee_thread, fds[0]).detach();
    }
#else
    g_console_fd = dup(STDOUT_FILENO);
    int fds[2];
    if (pipe(fds) == 0) {
        dup2(fds[1], STDOUT_FILENO);
        dup2(fds[1], STDERR_FILENO);
        close(fds[1]);
        std::thread(tee_thread, fds[0]).detach();
    }
#endif
    setvbuf(stdout, nullptr, _IONBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);

    std::ostringstream hdr;
    hdr << "Magical Blocks " << version << " - log started " << timestamp(false) << "\n";
    hdr << "OS: " << os_description() << "\n";
    std::error_code ec;
    hdr << "Working directory: " << path_utf8(std::filesystem::current_path(ec)) << "\n";
    hdr << "Data folder: " << path_utf8(app_folder) << "\n";
#ifdef _WIN32
    wchar_t exe[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    hdr << "Executable: " << path_utf8(std::filesystem::path(exe)) << "\n";
    hdr << "Command line: " << path_utf8(std::filesystem::path(GetCommandLineW())) << "\n";
#endif
    const std::string h = hdr.str();
    write_log_raw(h.data(), h.size());
}
