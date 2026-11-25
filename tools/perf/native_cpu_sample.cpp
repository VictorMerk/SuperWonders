// Opt-in, bounded Windows x64 instruction-pointer sampler. Run only against
// your own leased test process. No injection, stack scans, or target writes.
// clang++ -std=c++17 -O2 -static -municode native_cpu_sample.cpp -o native_cpu_sample.exe
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

struct Handle {
    HANDLE value;
    explicit Handle(HANDLE h) : value(h) {}
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
};
static uint64_t ticks(FILETIME t) { return (uint64_t(t.dwHighDateTime) << 32) | t.dwLowDateTime; }
static bool identity(HANDLE p, uint64_t started, const wchar_t* expected) {
    FILETIME c{}, e{}, k{}, u{};
    wchar_t path[32768]; DWORD size = 32768;
    return GetProcessTimes(p, &c, &e, &k, &u) && ticks(c) == started &&
           QueryFullProcessImageNameW(p, 0, path, &size) && !_wcsicmp(path, expected) &&
           WaitForSingleObject(p, 0) == WAIT_TIMEOUT;
}
struct Row { uint32_t round, tid; uint64_t ip, cpu; };
struct Module { uint64_t base, size; std::wstring name; };
int wmain(int argc, wchar_t** argv) {
    if (argc != 7 && argc != 8) {
        std::fprintf(stderr, "usage: native_cpu_sample PID creation_FILETIME executable output.csv seconds interval_ms [TID]\n");
        return 2;
    }
    DWORD pid = wcstoul(argv[1], nullptr, 10);
    uint64_t started = _wcstoui64(argv[2], nullptr, 10);
    int seconds = _wtoi(argv[5]), interval = _wtoi(argv[6]);
    DWORD selected = argc == 8 ? wcstoul(argv[7], nullptr, 10) : 0;
    if (argc == 8 && !selected) return 2;
    if (!pid || !started || seconds < 1 || seconds > 60 || interval < 2 || interval > 100 || seconds * 1000 / interval > 15000) return 2;
    Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, pid));
    if (!process.value || !identity(process.value, started, argv[3])) return 3;
    // Exclusive output creation preserves previous evidence and validates output
    // before suspending anything. All actual CSV writing occurs after sampling.
    FILE* output = _wfopen(argv[4], L"wx");
    if (!output) return 4;
    std::vector<Row> rows; rows.reserve(64 * (seconds * 1000 / interval + 1));
    std::vector<Module> modules;
    uint32_t failures = 0, rounds = 0; bool changed = false;
    ULONGLONG end = GetTickCount64() + seconds * 1000;
    while (GetTickCount64() < end) {
        if (!identity(process.value, started, argv[3])) { changed = true; break; }
        Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0));
        THREADENTRY32 entry{}; entry.dwSize = sizeof entry;
        unsigned count = 0;
        if (snapshot.value != INVALID_HANDLE_VALUE && Thread32First(snapshot.value, &entry)) do {
            if (entry.th32OwnerProcessID != pid || (selected && entry.th32ThreadID != selected) || count++ >= 64) continue;
            Handle thread(OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, entry.th32ThreadID));
            if (!thread.value || GetProcessIdOfThread(thread.value) != pid) { ++failures; continue; }
            CONTEXT context{}; context.ContextFlags = CONTEXT_CONTROL;
            FILETIME c{}, e{}, k{}, u{};
            bool timed = GetThreadTimes(thread.value, &c, &e, &k, &u);
            if (SuspendThread(thread.value) == DWORD(-1)) { ++failures; continue; }
            // No allocation, I/O, locks, or early returns between suspend/resume.
            bool captured = GetThreadContext(thread.value, &context);
            DWORD resumed = ResumeThread(thread.value);
            if (resumed == DWORD(-1)) { std::fclose(output); return 5; }
            if (captured && timed) rows.push_back({rounds, entry.th32ThreadID, context.Rip, ticks(k) + ticks(u)});
            else ++failures;
        } while (Thread32Next(snapshot.value, &entry));
        ++rounds;
        Sleep(interval);
    }
    Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid));
    MODULEENTRY32W module{}; module.dwSize = sizeof module;
    if (snapshot.value != INVALID_HANDLE_VALUE && Module32FirstW(snapshot.value, &module)) do {
        if (modules.size() >= 1024) break;
        modules.push_back({uint64_t(module.modBaseAddr), module.modBaseSize, module.szModule});
    } while (Module32NextW(snapshot.value, &module));
    std::fprintf(output, "round,tid,ip,cpu_100ns,module,rva\n");
    for (const auto& row : rows) {
        const Module* found = nullptr;
        for (const auto& m : modules) if (row.ip >= m.base && row.ip - m.base < m.size) { found = &m; break; }
        char name[1024] = "unknown";
        if (found) WideCharToMultiByte(CP_UTF8, 0, found->name.c_str(), -1, name, sizeof name, nullptr, nullptr);
        for (char* p = name; *p; ++p) if (*p == ',' || *p == '\n' || *p == '\r') *p = '_';
        std::fprintf(output, "%u,%u,%llx,%llu,%s,%llx\n", row.round, row.tid,
                     (unsigned long long)row.ip, (unsigned long long)row.cpu, name,
                     (unsigned long long)(found ? row.ip - found->base : 0));
    }
    bool bad = std::ferror(output); std::fclose(output);
    std::printf("rounds=%u samples=%zu failures=%u identity_changed=%d\n", rounds, rows.size(), failures, changed);
    return bad ? 6 : changed ? 7 : 0;
}
