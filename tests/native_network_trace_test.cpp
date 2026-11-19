#include "native_network_trace.h"
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>

using namespace nx;
using namespace nx::native_net;
namespace {
void check(bool value, const char* message) {
    if (!value) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}
CPU* expected_cpu;
unsigned calls = 0, events = 0;
Event last;
bool throw_guest = false, throw_sink = false;
void original(CPU& cpu) {
    check(&cpu == expected_cpu, "original receives the exact CPU object");
    check(cpu.x[1] == 0x123456789abcdef0ull, "opaque pointer argument is forwarded unchanged");
    ++calls;
    cpu.x[0] = uint64_t(-1ll);
    cpu.x[10] = 42; cpu.zf = 1;
    if (throw_guest) throw std::runtime_error("guest failure identity");
}
void sink(const Event& event) {
    ++events; last = event;
    if (throw_sink) throw std::logic_error("sink failure");
}
}
int main() {
    Symbol symbols[] = {
        {"_ZN2nn6socket6SendToEiPKvmiPK8sockaddrj", 4, 4, 2, 1, 0},
        // Same entry under another allowlisted alias must not double wrap.
        {"_ZN2nn6socket8RecvFromEiPvmiP8sockaddrPj", 4, 4, 2, 1, 0},
        {"_ZN2nn6socket6SocketEiii", 8, 4, 2, 1, 1}, // imported symbol excluded
        {"_ZN2nn4nifm19SetLocalNetworkModeEb", 99, 4, 2, 1, 0}, // malformed offset
    };
    ModuleInfo module{}; module.text_size = 16; module.symbols = symbols; module.symbol_count = 4;
    Fn table[] = {nullptr, original, original, nullptr};
    CPU cpu{}; expected_cpu = &cpu;
    auto prepare = [&] {
        cpu.x[0] = 7; cpu.x[1] = 0x123456789abcdef0ull; cpu.x[2] = 1024;
        cpu.x[3] = 0; cpu.x[4] = 0xfedcba9876543210ull; cpu.x[5] = 16; cpu.x[30] = 0x1020;
    };
    {
        Census off;
        check(off.install(false, module, table, {}, {}, sink) == 0 && table[1] == original,
              "disabled census leaves every function table entry unchanged");
    }
    {
        Census census;
        check(census.install(true, module, table, {0x1000, 256}, {0x2000, 256}, sink) == 1,
              "only defined valid unique SDK entry is hooked");
        check(table[2] == original && table[0] == nullptr, "unselected entries untouched");
        Census second;
        check(second.install(true, module, table, {}, {}, sink) == 0, "second owner cannot corrupt wrappers");
        prepare(); table[1](cpu);
        check(calls == 1 && cpu.x[10] == 42 && cpu.zf == 1 && int64_t(cpu.x[0]) == -1,
              "guest return values, registers and flags survive forwarding");
        check(last.fields[0] == 1024 && last.fields[3] == 16 && last.fields[2] == -1 &&
              last.caller_offset == 32 && last.has_result && last.error,
              "safe counts and source-relative caller captured without pointer fields");
        throw_guest = throw_sink = true;
        prepare();
        try { table[1](cpu); check(false, "guest exception expected"); }
        catch (const std::runtime_error& e) {
            check(std::string(e.what()) == "guest failure identity", "original exception preserved despite failing sink");
        }
        check(cpu.x[10] == 42 && last.exception && !last.has_result, "partial guest effects retained on exception");
        throw_guest = throw_sink = false;
        prepare(); cpu.x[2] = cpu.x[1]; cpu.x[30] = cpu.x[1]; table[1](cpu);
        check(last.fields[0] == -1 && last.caller_offset == 0 && std::string(last.caller_module) == "other",
              "unexpected pointer-sized count and caller are redacted");
        for (int i = 0; i < 1000; ++i) { prepare(); table[1](cpu); }
        const auto count = census.snapshot()[0];
        check(count.calls == 1003 && count.errors == 1002 && count.exceptions == 1,
              "every call counted including suppressed events and guest exceptions");
        check(count.total_ns >= count.peak_ns && events < 20, "duration aggregates and bounded sampling");
    }
    check(table[1] == original, "original entry restored at quiescent shutdown");
    {
        module.text = 4; // Table remains indexed by module offset, as in loader.cpp.
        Census nonzero;
        check(nonzero.install(true, module, table, {0x1000, 64, 16}, {}, sink) == 1,
              "nonzero text start accepts an interior module-relative symbol");
        prepare(); table[1](cpu);
        check(last.caller_offset == 32, "caller is module-relative, not text-relative");
    }
    {
        module.text = 8;
        Census outside;
        check(outside.install(true, module, table, {}, {}, sink) == 0 && table[1] == original,
              "symbol before nonzero text start rejected without table mutation");
        module.text = 4; symbols[0].value = 16; symbols[1].value = 16;
        check(outside.install(true, module, table, {}, {}, sink) == 0,
              "symbol beyond allocated table rejected even if inside text span");
    }
    std::puts("native network census forwarding and redaction passed");
}
