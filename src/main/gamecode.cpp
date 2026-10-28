// Option (b): generate + compile + load the game code on the player's PC (see include/mtc_gamecode.h, OPTB.md).
#include "mtc_gamecode.h"
#include "mtc_game.h"
#include "mtc_support.h"
#include "mtc_widescreen_glue.h"

#include "recomp.h"
#include "librecomp/sections.h"
#include "librecomp/rsp.hpp"
#include "librecomp/overlays.hpp"
#include "librecomp/game.hpp"
#include "ultramodern/ultramodern.hpp"

#include "../../gamecode/glue/mtc_game_abi.h"

#include <atomic>
#include <cfenv>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <Windows.h>
#else
#include <dlfcn.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

extern const std::string version_string;
namespace mtc { std::filesystem::path get_app_folder_path(); }

// ---- exe-side definitions the module imports ---------------------------------------------------------------
extern "C" {
#define MTC_X(name) void name(uint8_t* rdram, recomp_context* ctx);
    MTC_IMPORT_RECOMP_FUNCS(MTC_X)
#undef MTC_X
}

namespace {
    constexpr const char* generator_id = "n64recomp-ffb39cd";
#ifdef _WIN32
    constexpr const char* module_name = "mtc_game.dll";
#else
    constexpr const char* module_name = "libmtc_game.so";
#endif

    std::mutex state_mutex;
    std::atomic<int> cur_state{ (int)mtc::gamecode::State::NoRom };
    std::atomic<uint32_t> counter{ 0 };
    float cur_progress = 0.0f;
    std::string cur_message;
    std::thread build_thread;

    bool loaded = false;
    MtcGameImports imports{};
    MtcGameExports exports{};

    void set_state(mtc::gamecode::State s, float p, const std::string& msg) {
        {
            std::lock_guard lock{ state_mutex };
            cur_progress = p;
            cur_message = msg;
        }
        cur_state.store((int)s);
        counter.fetch_add(1);
    }

    std::filesystem::path cache_dir() { return mtc::get_app_folder_path() / "gamecode-cache"; }  // generated from the user ROM; separate from the shipped kit (portable mode shares the folder)
    std::filesystem::path stored_rom_path() { return recomp::get_config_path() / (std::u8string(mtc::game_id) + u8".z64"); }
    // Folder of the running exe (the app's own get_program_path() is relative to the working directory, but the
    // tools run with their own working directory).
    std::filesystem::path exe_dir() {
#ifdef _WIN32
        std::vector<wchar_t> buf(32768);
        DWORD n = GetModuleFileNameW(nullptr, buf.data(), (DWORD)buf.size());
        if (n > 0 && n < buf.size()) return std::filesystem::path(std::wstring(buf.data(), n)).parent_path();
#else
        std::error_code ec;
        std::filesystem::path p = std::filesystem::read_symlink("/proc/self/exe", ec);
        if (!ec) return p.parent_path();
#endif
        return std::filesystem::absolute(mtc::get_program_path());
    }
    std::filesystem::path shipped_dir() { return exe_dir() / "gamecode"; }

    std::string path_u8(const std::filesystem::path& p) {
        std::u8string s = p.u8string();
        return std::string(s.begin(), s.end());
    }

    bool read_file(const std::filesystem::path& p, std::string& out) {
        std::ifstream f{ p, std::ios::binary };
        if (!f.good()) return false;
        std::stringstream ss;
        ss << f.rdbuf();
        out = ss.str();
        return true;
    }

    bool write_file(const std::filesystem::path& p, const std::string& data) {
        std::ofstream f{ p, std::ios::binary | std::ios::trunc };
        if (!f.good()) return false;
        f.write(data.data(), (std::streamsize)data.size());
        return f.good();
    }

    // Stamp = what the module was built from. A different app version, ROM or ABI forces a rebuild.
    std::string expected_stamp(std::string* rom_sha1_out = nullptr) {
        std::string rom;
        if (!read_file(stored_rom_path(), rom)) return {};
        std::string sha1 = mtc::sha1_hex(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(rom.data()), rom.size()));
        if (sha1 != mtc::rom_sha1_usa) return {};
        if (rom_sha1_out) *rom_sha1_out = sha1;
        return "mtc-gamecode abi=" + std::to_string(MTC_GAME_ABI_VERSION) + " app=" + version_string + " rom=" + sha1 + " gen=" + generator_id;
    }

    // ---- child processes -------------------------------------------------------------------------------------
    void log_command(const std::filesystem::path& exe, const std::vector<std::string>& args, const std::filesystem::path& log) {
        std::ofstream f{ log, std::ios::binary | std::ios::app };
        f << "> " << path_u8(exe);
        for (const auto& a : args) f << ' ' << a;
        f << "\n";
    }
    // Runs `exe args...` in `cwd`, output appended to `log`. Returns the exit code (-1 if it couldn't start).
#ifdef _WIN32
    std::wstring widen(const std::string& s) { return std::filesystem::path(std::u8string(s.begin(), s.end())).wstring(); }

    void append_quoted(std::wstring& cmd, const std::wstring& arg) {
        if (!arg.empty() && arg.find_first_of(L" \t\"") == std::wstring::npos) { cmd += arg; return; }
        cmd += L'"';
        for (size_t i = 0; i < arg.size(); i++) {
            size_t bs = 0;
            while (i < arg.size() && arg[i] == L'\\') { bs++; i++; }
            if (i == arg.size()) { cmd.append(bs * 2, L'\\'); break; }
            if (arg[i] == L'"') { cmd.append(bs * 2 + 1, L'\\'); cmd += L'"'; }
            else { cmd.append(bs, L'\\'); cmd += arg[i]; }
        }
        cmd += L'"';
    }

    int run_process(const std::filesystem::path& exe, const std::vector<std::string>& args, const std::filesystem::path& cwd, const std::filesystem::path& log) {
        log_command(exe, args, log);
        std::wstring cmd;
        append_quoted(cmd, exe.wstring());
        for (const auto& a : args) { cmd += L' '; append_quoted(cmd, widen(a)); }

        SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, TRUE };
        HANDLE log_h = CreateFileW(log.wstring().c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        STARTUPINFOW si{};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdInput = INVALID_HANDLE_VALUE;
        si.hStdOutput = log_h;
        si.hStdError = log_h;
        PROCESS_INFORMATION pi{};
        std::vector<wchar_t> buf(cmd.begin(), cmd.end());
        buf.push_back(0);
        BOOL ok = CreateProcessW(exe.wstring().c_str(), buf.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, cwd.wstring().c_str(), &si, &pi);
        if (log_h != INVALID_HANDLE_VALUE) CloseHandle(log_h);
        if (!ok) return -1;
        WaitForSingleObject(pi.hProcess, INFINITE);
        DWORD code = 1;
        GetExitCodeProcess(pi.hProcess, &code);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return (int)code;
    }
#else
    int run_process(const std::filesystem::path& exe, const std::vector<std::string>& args, const std::filesystem::path& cwd, const std::filesystem::path& log) {
        log_command(exe, args, log);
        std::vector<std::string> argv_s;
        argv_s.push_back(exe.string());
        for (const auto& a : args) argv_s.push_back(a);
        std::vector<char*> argv;
        for (auto& a : argv_s) argv.push_back(a.data());
        argv.push_back(nullptr);
        pid_t pid = fork();
        if (pid < 0) return -1;
        if (pid == 0) {
            int fd = open(log.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
            if (fd >= 0) { dup2(fd, 1); dup2(fd, 2); close(fd); }
            if (chdir(cwd.c_str()) != 0) _exit(126);
            // A bare name (e.g. "clang") is looked up in PATH.
            execvp(argv[0], argv.data());
            _exit(127);
        }
        int status = 0;
        while (waitpid(pid, &status, 0) < 0) {}
        return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    }
#endif

    struct Tools {
        std::filesystem::path n64recomp, rsprecomp, cc;
        std::vector<std::string> c_flags, cxx_flags, link_flags;
    };

    Tools find_tools() {
        Tools t;
        std::filesystem::path prog = exe_dir();
        std::filesystem::path tools = prog / "tools";
        if (const char* e = getenv("MTC_TOOLS_DIR")) tools = e;
        std::filesystem::path inc = shipped_dir() / "include";
        std::filesystem::path glue = shipped_dir() / "glue";
        std::vector<std::string> common = {
            "-I", path_u8(glue), "-I", path_u8(inc), "-I", "RecompiledFuncs", "-I", ".",
            "-O3", "-DNDEBUG", "-fno-strict-aliasing", "-fvisibility=hidden", "-w",
            "-Wno-implicit-function-declaration",
        };
#ifdef _WIN32
        t.n64recomp = tools / "N64Recomp.exe";
        t.rsprecomp = tools / "RSPRecomp.exe";
        t.cc = prog / "toolchain" / "bin" / "clang-20.exe";
        common.insert(common.begin(), { "--target=x86_64-w64-windows-gnu", "-mno-stack-arg-probe", "-fms-extensions" });
        t.link_flags = { "--target=x86_64-w64-windows-gnu", "-shared", "-nostdlib", "-fuse-ld=lld", "-Wl,-e,DllMainCRTStartup" };
        t.cxx_flags = { "--driver-mode=g++", "-stdlib=libc++" };
#else
        t.n64recomp = tools / "N64Recomp";
        t.rsprecomp = tools / "RSPRecomp";
        t.cc = "clang";
        common.insert(common.begin(), { "-fPIC" });
        t.link_flags = { "-shared", "-nostdlib", "-fPIC", "-Wl,--no-undefined", "-Wl,-Bsymbolic" };
        t.cxx_flags = { "--driver-mode=g++" };
#endif
        if (const char* e = getenv("MTC_CC")) t.cc = e;
        t.c_flags = common;
        t.c_flags.insert(t.c_flags.end(), { "-std=gnu17" });
        t.cxx_flags.insert(t.cxx_flags.end(), common.begin(), common.end());
        t.cxx_flags.insert(t.cxx_flags.end(), { "-std=c++20", "-march=nehalem", "-fno-exceptions", "-fno-rtti", "-fno-threadsafe-statics" });
        return t;
    }

    std::string replace_line(std::string text, const std::string& key, const std::string& value) {
        // Replaces `key = ...` (first occurrence at line start) with `key = "value"`.
        size_t pos = 0;
        while ((pos = text.find(key, pos)) != std::string::npos) {
            if (pos == 0 || text[pos - 1] == '\n') {
                size_t end = text.find('\n', pos);
                text.replace(pos, (end == std::string::npos ? text.size() : end) - pos, key + " = \"" + value + "\"");
                return text;
            }
            pos += key.size();
        }
        return text;
    }

    // ---- module loading ----------------------------------------------------------------------------------------
    void exe_switch_error(const char* func, uint32_t vram, uint32_t jtbl) { switch_error(func, vram, jtbl); }
    void exe_do_break(uint32_t vram) { do_break(vram); }
    recomp_func_t* exe_get_function(int32_t vram) { return get_function(vram); }
    void exe_pause_self(uint8_t* rdram) { pause_self(rdram); }
    // The runtime has no syscall handler (the game never executes `syscall`); same outcome as a missing symbol.
    void exe_syscall(uint8_t*, recomp_context*, int32_t vram) { fprintf(stderr, "syscall at 0x%08X\n", (uint32_t)vram); std::abort(); }
    void exe_cop0_w(recomp_context* ctx, gpr v) { cop0_status_write(ctx, v); }
    gpr exe_cop0_r(recomp_context* ctx) { return cop0_status_read(ctx); }
    void* exe_memcpy(void* d, const void* s, size_t n) { return std::memcpy(d, s, n); }
    void* exe_memmove(void* d, const void* s, size_t n) { return std::memmove(d, s, n); }
    void* exe_memset(void* d, int c, size_t n) { return std::memset(d, c, n); }
    int exe_memcmp(const void* a, const void* b, size_t n) { return std::memcmp(a, b, n); }
    int exe_vprintf(const char* fmt, va_list ap) { return std::vprintf(fmt, ap); }
    int exe_fegetround() { return std::fegetround(); }
    int exe_fesetround(int m) { return std::fesetround(m); }
    float exe_sqrtf(float v) { return std::sqrt(v); }
    double exe_sqrt(double v) { return std::sqrt(v); }
    void exe_abort() { std::abort(); }

    void fill_imports() {
        imports.abi_version = MTC_GAME_ABI_VERSION;
        imports.struct_size = sizeof(MtcGameImports);
#define MTC_X(name) imports.name = name;
        MTC_IMPORT_RECOMP_FUNCS(MTC_X)
#undef MTC_X
        imports.cop0_status_write = exe_cop0_w;
        imports.cop0_status_read = exe_cop0_r;
        imports.switch_error = exe_switch_error;
        imports.do_break = exe_do_break;
        imports.get_function = exe_get_function;
        imports.recomp_syscall_handler = exe_syscall;
        imports.pause_self = exe_pause_self;
        imports.mtc_ws_on_bg_load = mtc_ws_on_bg_load;
        imports.memcpy = exe_memcpy;
        imports.memmove = exe_memmove;
        imports.memset = exe_memset;
        imports.memcmp = exe_memcmp;
        imports.vprintf = exe_vprintf;
        imports.fegetround = exe_fegetround;
        imports.fesetround = exe_fesetround;
        imports.sqrtf = exe_sqrtf;
        imports.sqrt = exe_sqrt;
        imports.abort = exe_abort;
    }
}

// Entry/microcode thunks handed to librecomp at startup (before the module is loaded).
extern "C" void mtc_gamecode_entrypoint(uint8_t* rdram, recomp_context* ctx) {
    if (exports.entrypoint == nullptr) {
        ultramodern::error_handling::message_box("The game has not been prepared yet. Start it from the launcher.");
        ultramodern::error_handling::quick_exit(__FILE__, __LINE__, __FUNCTION__);
    }
    exports.entrypoint(rdram, ctx);
}

RspExitReason mtc_gamecode_n_aspMain(uint8_t* rdram, uint32_t ucode_addr) {
    // The module has its own DMEM array; librecomp loaded the task into the exe's.
    std::memcpy(exports.dmem, dmem, 0x1000);
    RspExitReason r = (RspExitReason)exports.n_aspMain(rdram, ucode_addr);
    std::memcpy(dmem, exports.dmem, 0x1000);
    return r;
}

#ifndef MTC_STATIC_GAMECODE
bool mtc::gamecode::is_static() { return false; }

bool mtc::gamecode::module_is_current() {
    std::string stamp;
    if (!read_file(cache_dir() / "stamp.txt", stamp)) return false;
    if (!std::filesystem::exists(cache_dir() / module_name)) return false;
    std::string want = expected_stamp();
    return !want.empty() && stamp == want;
}

static bool generate_impl(const std::function<void(float, const std::string&)>& progress, std::string& error);

bool mtc::gamecode::generate(const std::function<void(float, const std::string&)>& progress, std::string& error) {
    bool ok = generate_impl(progress, error);
    if (!ok && getenv("MTC_KEEP_GAMECODE_BUILD") == nullptr) {
        // Don't leave the half-built intermediate files behind (generate.log next to it keeps the details).
        std::error_code ec;
        std::filesystem::remove_all(cache_dir() / "build", ec);
    }
    if (!ok) fprintf(stderr, "[gamecode] prepare failed: %s\n", error.c_str());
    return ok;
}

static bool generate_impl(const std::function<void(float, const std::string&)>& progress, std::string& error) {
    namespace fs = std::filesystem;
    std::error_code ec;
    std::string rom_sha1;
    std::string stamp = expected_stamp(&rom_sha1);
    if (stamp.empty()) { error = "No valid ROM is installed. Select your ROM first."; return false; }

    fs::path base = cache_dir();
    fs::path build = base / "build";
    fs::path log = base / "generate.log";
    fs::remove_all(build, ec);
    fs::create_directories(build / "RecompiledFuncs", ec);
    fs::create_directories(build / "rsp", ec);
    fs::create_directories(build / "obj", ec);
    if (!fs::is_directory(build / "obj")) { error = "Could not create the folder " + path_u8(build) + "."; return false; }
    fs::remove(base / "stamp.txt", ec);
    write_file(log, "Preparing game code: " + stamp + "\n");

    Tools tools = find_tools();
    fs::path ship = shipped_dir();
    auto missing = [&](const fs::path& p) { return !p.is_absolute() ? false : !fs::exists(p); };
    for (const fs::path& p : { ship / "mtc.us.toml", ship / "mtc.syms.toml", ship / "n_aspMain.us.toml", tools.n64recomp, tools.rsprecomp, tools.cc }) {
        if (missing(p)) { error = "A program file is missing: " + path_u8(p) + ". Please reinstall the game."; return false; }
    }

    // 1) configs: the stored ROM lives two levels up (<app folder>/mtc.n64.us.z64); relative paths keep
    //    non-ASCII user folders out of the tools' command lines.
    progress(0.02f, "Reading your ROM...");
    std::string cfg, rsp_cfg;
    read_file(ship / "mtc.us.toml", cfg);
    read_file(ship / "n_aspMain.us.toml", rsp_cfg);
    std::string rom_rel = "../../" + path_u8(stored_rom_path().filename());
    cfg = replace_line(cfg, "symbols_file_path", "mtc.syms.toml");
    cfg = replace_line(cfg, "rom_file_path", rom_rel);
    cfg = replace_line(cfg, "output_func_path", "RecompiledFuncs");
    rsp_cfg = replace_line(rsp_cfg, "rom_file_path", rom_rel);
    rsp_cfg = replace_line(rsp_cfg, "output_file_path", "rsp/n_aspMain.cpp");
    fs::copy_file(ship / "mtc.syms.toml", build / "mtc.syms.toml", fs::copy_options::overwrite_existing, ec);
    if (!write_file(build / "mtc.us.toml", cfg) || !write_file(build / "n_aspMain.us.toml", rsp_cfg) ||
        !write_file(build / "mtc_game_stamp.h", "#define MTC_GAME_STAMP \"" + stamp + "\"\n")) {
        error = "Could not write to " + path_u8(build) + ". Is the disk full?";
        return false;
    }

    // 2) recompile (MIPS -> C)
    progress(0.05f, "Translating the game code...");
    if (int rc = run_process(tools.n64recomp, { "mtc.us.toml" }, build, log); rc != 0) {
        error = "Translating the game code failed (N64Recomp exit code " + std::to_string(rc) + ").";
        return false;
    }
    progress(0.10f, "Translating the sound code...");
    if (int rc = run_process(tools.rsprecomp, { "n_aspMain.us.toml" }, build, log); rc != 0 || !fs::exists(build / "rsp" / "n_aspMain.cpp")) {
        error = "Translating the sound code failed (RSPRecomp exit code " + std::to_string(rc) + ").";
        return false;
    }

    // 3) compile
    struct Job { std::string src; std::string obj; bool cxx; };
    std::vector<Job> jobs;
    for (const auto& e : fs::directory_iterator(build / "RecompiledFuncs", ec)) {
        std::string name = path_u8(e.path().filename());
        if (e.path().extension() == ".c") jobs.push_back({ "RecompiledFuncs/" + name, "obj/" + path_u8(e.path().stem()) + ".o", false });
    }
    if (jobs.empty()) { error = "Translating the game code produced no output."; return false; }
    jobs.push_back({ "rsp/n_aspMain.cpp", "obj/n_aspMain.o", true });
    jobs.push_back({ path_u8(ship / "glue" / "mtc_game_tables.cpp"), "obj/mtc_game_tables.o", true });
    jobs.push_back({ path_u8(ship / "glue" / "mtc_game_glue.cpp"), "obj/mtc_game_glue.o", true });

    std::atomic<size_t> next{ 0 }, done{ 0 };
    std::atomic<bool> failed{ false };
    std::string failed_src;
    std::mutex fail_mutex;
    unsigned n_threads = std::max(1u, std::min(std::thread::hardware_concurrency(), 16u));
    if (const char* e = getenv("MTC_GAMECODE_JOBS")) n_threads = std::max(1, atoi(e));
    progress(0.12f, "Building the game (this happens only once)...");
    std::vector<std::thread> workers;
    for (unsigned t = 0; t < n_threads; t++) {
        workers.emplace_back([&, t]() {
            fs::path job_log = build / ("obj/job" + std::to_string(t) + ".log");
            while (!failed) {
                size_t i = next.fetch_add(1);
                if (i >= jobs.size()) break;
                const Job& j = jobs[i];
                std::vector<std::string> args = j.cxx ? tools.cxx_flags : tools.c_flags;
                args.insert(args.end(), { "-c", j.src, "-o", j.obj });
                if (run_process(tools.cc, args, build, job_log) != 0) {
                    std::lock_guard lock{ fail_mutex };
                    failed = true;
                    failed_src = j.src;
                }
                size_t d = done.fetch_add(1) + 1;
                progress(0.12f + 0.80f * float(d) / float(jobs.size()), "Building the game (this happens only once)...");
            }
        });
    }
    for (auto& w : workers) w.join();
    // Collect compiler output into the main log.
    for (unsigned t = 0; t < n_threads; t++) {
        std::string s;
        if (read_file(build / ("obj/job" + std::to_string(t) + ".log"), s) && !s.empty()) {
            std::ofstream f{ log, std::ios::binary | std::ios::app };
            f << s;
        }
    }
    if (failed) {
        error = "Building the game failed while compiling " + failed_src + ". Details are in " + path_u8(log) + ".";
        return false;
    }

    // 4) link
    progress(0.94f, "Finishing...");
    std::vector<std::string> link_args = tools.link_flags;
    link_args.insert(link_args.end(), { "-o", module_name });
    for (const Job& j : jobs) link_args.push_back(j.obj);
    if (int rc = run_process(tools.cc, link_args, build, log); rc != 0 || !fs::exists(build / module_name)) {
        error = "Building the game failed at the last step (link exit code " + std::to_string(rc) + "). Details are in " + path_u8(log) + ".";
        return false;
    }
    fs::rename(build / module_name, base / module_name, ec);
    if (ec) {
        fs::copy_file(build / module_name, base / module_name, fs::copy_options::overwrite_existing, ec);
        if (ec) { error = "Could not save the built game to " + path_u8(base) + " (" + ec.message() + ")."; return false; }
    }
    if (!write_file(base / "stamp.txt", stamp)) { error = "Could not write to " + path_u8(base) + "."; return false; }

    // The generated sources and objects are only intermediate files: remove them.
    if (getenv("MTC_KEEP_GAMECODE_BUILD") == nullptr) fs::remove_all(build, ec);
    progress(1.0f, "Ready");
    return true;
}

bool mtc::gamecode::load(std::string& error) {
    if (loaded) return true;
    std::string stamp;
    if (!module_is_current() || !read_file(cache_dir() / "stamp.txt", stamp)) {
        error = "The game has not been prepared yet.";
        return false;
    }
    std::filesystem::path p = cache_dir() / module_name;
    mtc_game_init_t* init = nullptr;
#ifdef _WIN32
    HMODULE h = LoadLibraryExW(p.wstring().c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (h == nullptr) { error = "Could not load " + path_u8(p) + " (error " + std::to_string(GetLastError()) + ")."; return false; }
    init = reinterpret_cast<mtc_game_init_t*>(GetProcAddress(h, "mtc_game_init"));
#else
    void* h = dlopen(p.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (h == nullptr) { error = std::string("Could not load the game module: ") + dlerror(); return false; }
    init = reinterpret_cast<mtc_game_init_t*>(dlsym(h, "mtc_game_init"));
#endif
    if (init == nullptr) { error = "The game module is invalid (no mtc_game_init)."; return false; }
    fill_imports();
    exports = {};
    exports.struct_size = sizeof(MtcGameExports);
    if (int rc = init(&imports, &exports); rc != 0) { error = "The game module does not match this version (code " + std::to_string(rc) + ")."; return false; }
    if (exports.generator_stamp == nullptr || stamp != exports.generator_stamp) { error = "The game module is from another version."; return false; }

    recomp::overlays::overlay_section_table_data_t sections{
        .code_sections = exports.section_table,
        .num_code_sections = exports.num_code_sections,
        .total_num_sections = exports.total_num_sections,
    };
    recomp::overlays::overlays_by_index_t overlays{
        .table = exports.overlay_sections_by_index,
        .len = exports.num_overlays,
    };
    recomp::overlays::register_overlays(sections, overlays);
    loaded = true;
    return true;
}

bool mtc::gamecode::is_loaded() { return loaded; }
#else
bool mtc::gamecode::is_static() { return true; }
bool mtc::gamecode::module_is_current() { return true; }
bool mtc::gamecode::generate(const std::function<void(float, const std::string&)>&, std::string&) { return true; }
bool mtc::gamecode::load(std::string&) { return true; }
bool mtc::gamecode::is_loaded() { return true; }
#endif

void mtc::gamecode::refresh_state() {
    State s = (State)cur_state.load();
    if (s == State::Building) return;
    if (!recomp::is_rom_valid(mtc::game_id)) { set_state(State::NoRom, 0.0f, ""); return; }
    if (module_is_current()) { set_state(State::Ready, 1.0f, "Ready"); return; }
    set_state(State::NeedsBuild, 0.0f, "");
}

void mtc::gamecode::start_async_build() {
    State s = (State)cur_state.load();
    if (s != State::NeedsBuild && s != State::Failed) return;
    set_state(State::Building, 0.0f, "Preparing the game...");
    build_thread = std::thread([]() {
        std::string err;
        bool ok = generate([](float p, const std::string& m) { set_state(State::Building, p, m); }, err);
        if (ok) set_state(State::Ready, 1.0f, "Ready");
        else set_state(State::Failed, 0.0f, err);
    });
    build_thread.detach();
}

mtc::gamecode::State mtc::gamecode::state() { return (State)cur_state.load(); }
float mtc::gamecode::progress() { std::lock_guard lock{ state_mutex }; return cur_progress; }
std::string mtc::gamecode::message() { std::lock_guard lock{ state_mutex }; return cur_message; }
uint32_t mtc::gamecode::change_counter() { return counter.load(); }
