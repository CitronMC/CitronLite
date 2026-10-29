// The log: off unless Citron Lite was started with --debug or a debug.txt sits in its data folder.
// Each line starts with the milliseconds since the first one.
#pragma once
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <string>

namespace citron {

inline bool& LogEnabled() { static bool e = false; return e; }
inline std::string& LogPath() { static std::string p = "citron-lite.log"; return p; }

inline void Log(const char* fmt, ...) {
    if (!LogEnabled()) return;
    static std::mutex m;
    std::lock_guard<std::mutex> g(m);
    FILE* f = std::fopen(LogPath().c_str(), "a");
    if (!f) return;
    static const auto t0 = std::chrono::steady_clock::now();
    const long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - t0).count();
    std::fprintf(f, "[%8lld] ", ms);
    va_list ap;
    va_start(ap, fmt);
    std::vfprintf(f, fmt, ap);
    va_end(ap);
    std::fputc('\n', f);
    std::fclose(f);
}

} // namespace citron
