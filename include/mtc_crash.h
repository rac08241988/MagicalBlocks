#pragma once
// Persistent log file + crash reporting (see docs/CRASH-REPORTS.md).
#include <filesystem>
#include <string>

namespace mtc::crash {
    // Catch native crashes (access violations, stack overflow), abort(), std::terminate, CRT invalid-parameter /
    // pure-call and unexpected exit()/quick_exit(): each writes logs/crash-<time>.txt (+ .dmp on Windows) and
    // shows a message box with the report path. Call as early as possible.
    void install_handlers();

    // Rotate <app folder>/logs/latest.log (keeps 5) and tee stdout/stderr (incl. RT64 output) into it.
    void init_logging(const std::filesystem::path& app_folder, const std::string& version);

    // Mark the coming exit as intentional (normal quit), so the exit hooks stay quiet.
    void mark_clean_exit();

    // Reserve stack for the crash handler on the calling thread (Windows SetThreadStackGuarantee).
    void prepare_thread();

    // Free-form line into the log ("[note] ...").
    void note(const std::string& line);

    std::filesystem::path logs_dir();
}
