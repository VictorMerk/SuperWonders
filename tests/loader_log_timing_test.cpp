#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
// Include actual synchronous loader logger. A real mutex contender waits while
// the fixture owns its lock; a controlled monotonic clock models wait/sink costs.
// No timing numbers from this CPU fixture are hardware performance evidence.
#include <nx/runtime.h>
#include <string>
#include <thread>
namespace fixture {
std::atomic<uint64_t> now{100000000};
std::mutex lock;
std::condition_variable changed;
unsigned reads = 0;
bool waiting = false;
std::string log_path, platform_line;
void check(bool ok, const char *m) {
  if (!ok) {
    std::fprintf(stderr, "%s\n", m);
    std::abort();
  }
}
} // namespace fixture
namespace nx::plat {
uint64_t monotonic_ns() {
  std::lock_guard guard(fixture::lock);
  if (++fixture::reads == 2) {
    fixture::waiting = true;
    fixture::changed.notify_all();
  }
  return fixture::now.fetch_add(1000000);
}
std::string data_root() { return "fixture"; }
FILE *fopen_utf8(const std::string &path, const char *mode) {
  fixture::check(path == fixture::log_path && std::string(mode) == "w",
                 "only test-owned log sink opened");
  fixture::now += 2000000;
  return std::tmpfile();
}
void log_line(const char *line) {
  fixture::platform_line = line;
  fixture::now += 5000000;
}
} // namespace nx::plat
#include "loader.cpp"
namespace nx {
void backtrace(CPU &) { std::abort(); }
bool commit(uint64_t, uint64_t) { std::abort(); }
void vm_set(uint64_t, uint64_t, uint32_t, uint32_t) { std::abort(); }
namespace plat {
[[noreturn]] void exit_process(int) { std::abort(); }
} // namespace plat
namespace game {
const Info kInfo = {"test", "test", 0, "test", "test", 0};
}
} // namespace nx
static void env(const char *key, const char *value) {
#if defined(_WIN32)
  fixture::check(_putenv_s(key, value) == 0, "fixture environment");
#else
  fixture::check(setenv(key, value, 1) == 0, "fixture environment");
#endif
}
int main(int argc, char **argv) {
  using fixture::check;
  check(argc == 1 || (argc == 2 && std::string(argv[1]) == "--off"), "fixture arguments");
  const bool on = argc == 1;
  env("NX_FRAMETRACE", on ? "test-enabled" : "");
  const auto root =
      std::filesystem::temp_directory_path() /
      ("wonder-log-phases-" + std::to_string(
          std::chrono::steady_clock::now().time_since_epoch().count()));
  check(std::filesystem::create_directory(root), "unique test-owned folder");
  fixture::log_path = (root / "nx.log").string();
  env("NX_LOG", fixture::log_path.c_str());
  if (on) {
    std::unique_lock owned(nx::g_log_lock);
    std::thread writer([] { nx::log("phase %d", 7); });
    {
      std::unique_lock guard(fixture::lock);
      check(fixture::changed.wait_for(guard, std::chrono::seconds(5),
                                     [] { return fixture::waiting; }),
            "logger contender reaches timed lock within deadline");
    }
    fixture::now += 17000000;
    owned.unlock();
    writer.join();
  } else
    nx::log("phase %d", 7);
  check(fixture::platform_line == "phase 7" && nx::g_log_file,
        "synchronous platform/file output preserved");
  std::rewind(nx::g_log_file);
  char line[64]{};
  check(std::fgets(line, sizeof line, nx::g_log_file) &&
            std::string(line) == "phase 7\n",
        "file line and flush preserved");
  using namespace nx::ft;
  if (on) {
    check(g_counter[kLogMutexNs] >= 17000000 &&
              g_counter[kLogPlatformNs] >= 5000000 &&
              g_counter[kLogSetupNs] >= 2000000 && g_counter[kLogFileNs] > 0,
          "actual logger attributes real lock contention and mock sink phases");
    check(g_counter[kLogNs] >=
              g_counter[kLogMutexNs] + g_counter[kLogPlatformNs] +
                  g_counter[kLogSetupNs] + g_counter[kLogFileNs],
          "total includes phase work and formatting/gaps");
  } else {
    check(fixture::reads == 0, "disabled actual logger reads no timing clock");
    check(g_counter[kLogNs] == 0 && g_counter[kLogMutexNs] == 0 &&
              g_counter[kLogPlatformNs] == 0 && g_counter[kLogSetupNs] == 0 &&
              g_counter[kLogFileNs] == 0,
          "disabled phase counters unchanged");
  }
  const uint64_t setup_before = g_counter[kLogSetupNs];
  // The previous rewind is read-only verification; restore append position.
  std::fseek(nx::g_log_file, 0, SEEK_END);
  nx::log("phase %d", 8);
  check(g_counter[kLogSetupNs] == setup_before, "existing sink does not repeat open/rotation phase");
  check(fixture::platform_line == "phase 8" && g_counter[kLogLines] == 2, "second output call preserved");
  std::rewind(nx::g_log_file);
  check(std::fgets(line, sizeof line, nx::g_log_file) && std::string(line) == "phase 7\n" &&
        std::fgets(line, sizeof line, nx::g_log_file) && std::string(line) == "phase 8\n",
        "all file output retained synchronously in order");
  if (!on) check(fixture::reads == 0, "disabled repeated actual logging still reads no clock");
  std::fclose(nx::g_log_file);
  nx::g_log_file = nullptr;
  check(std::filesystem::remove(root), "remove only owned empty folder");
  std::puts("actual logger phase/output test passed");
}
