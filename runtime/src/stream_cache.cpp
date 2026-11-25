#include "stream_cache.h"
#include "file_io.h"
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <map>
#include <mutex>
#include <thread>
#include <string_view>
#include <vector>
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace nx::streaming {
namespace fs = std::filesystem;
namespace { std::atomic<bool> g_enabled{false}; }
void set_enabled(bool value) { g_enabled.store(value, std::memory_order_relaxed); }
bool enabled() {
    static const bool allowed = [] { const char* e = std::getenv("NX_STREAM_CACHE"); return !e || e[0] != '0'; }();
    return allowed && g_enabled.load(std::memory_order_relaxed);
}
bool eligible_music(const std::string& path) {
    constexpr std::string_view directory = "/Sound/Resource/Stream/";
    const auto at = path.find(directory);
    if (!path.starts_with("content:") || at == std::string::npos || !path.ends_with(".bwav")) return false;
    const std::string_view name(path.data() + at + directory.size(), path.size() - at - directory.size());
    return name.starts_with("BGM_") || name.starts_with("Env_");
}

struct MusicCache::Entry {
    fs::path path;
    fs::file_time_type modified;
    uint64_t size = 0, stamp = 0;
    std::vector<uint8_t> data;
    std::atomic<bool> ready{false}, failed{false};
};
struct MusicCache::State {
    mutable std::mutex lock;
    std::condition_variable available;
    std::map<fs::path,Token> entries;
    std::deque<Token> pending;
    std::atomic<bool> stopping{false};
    std::atomic<uint64_t> hits{0}, completed{0}, failed{0}, dropped{0};
    uint64_t reserved = 0, clock = 0, total_limit, file_limit;
    size_t entry_limit, pending_limit;
    std::thread worker;
    State(uint64_t total, uint64_t file, size_t count, size_t jobs)
        : total_limit(total), file_limit(file), entry_limit(count), pending_limit(jobs), worker([this] { run(); }) {}
    ~State() { stop(); }
    void stop() {
        stopping.store(true);
        { std::lock_guard guard(lock); pending.clear(); }
        available.notify_all();
        if (worker.joinable()) worker.join();
    }
    void run() {
#if defined(_WIN32)
        // No guest read waits for this worker. Lower its I/O/memory/CPU scheduling
        // priority too, rather than competing with foreground asset loads.
        SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_BEGIN);
#endif
        for (;;) {
            Token entry;
            {
                std::unique_lock guard(lock);
                available.wait(guard, [&] { return stopping.load() || !pending.empty(); });
                if (stopping.load()) return;
                entry = std::move(pending.front()); pending.pop_front();
            }
            bool ok = false;
            try {
                std::ifstream source(entry->path, std::ios::binary);
                std::vector<uint8_t> data(entry->size);
                ok = bool(source);
                for (size_t at = 0; ok && at < data.size();) {
                    if (stopping.load(std::memory_order_relaxed)) { ok = false; break; }
                    const size_t count = std::min<size_t>(65536, data.size() - at);
                    ok = bool(source.read(reinterpret_cast<char*>(data.data() + at), count));
                    at += count;
                }
                std::error_code error;
                ok = ok && fs::file_size(entry->path, error) == entry->size && !error &&
                    fs::last_write_time(entry->path, error) == entry->modified && !error;
                if (ok) {
                    entry->data = std::move(data);
                    entry->ready.store(true, std::memory_order_release);
                    entry.reset();
                    completed.fetch_add(1, std::memory_order_relaxed);
                }
            } catch (...) { ok = false; }
            if (!ok) entry->failed.store(true, std::memory_order_release), failed.fetch_add(1, std::memory_order_relaxed);
        }
    }
};

MusicCache::MusicCache(uint64_t total, uint64_t file, size_t count, size_t jobs)
    : state(std::make_unique<State>(total, file, count, jobs)) {}
MusicCache::~MusicCache() = default;
void MusicCache::stop() { state->stop(); }

MusicCache::Token MusicCache::open(const fs::path& path) {
    try {
        if (state->stopping.load() || !state->entry_limit || !state->pending_limit) return {};
        std::error_code error;
        const auto size = fs::file_size(path, error);
        if (error || !size || size > state->file_limit || size > state->total_limit) return {};
        const auto modified = fs::last_write_time(path, error);
        if (error) return {};
        std::lock_guard guard(state->lock);
        if (state->stopping.load()) return {};
        auto found = state->entries.find(path);
        if (found != state->entries.end()) {
            auto& entry = found->second;
            if (entry->size == size && entry->modified == modified && !entry->failed.load(std::memory_order_acquire)) {
                entry->stamp = ++state->clock;
                return entry;
            }
            // Existing open streams retain their snapshot. A new open of changed
            // content falls back to the actual file until those handles close.
            if (entry.use_count() != 1) return {};
            state->reserved -= entry->size;
            state->entries.erase(found);
        }
        if (state->pending.size() >= state->pending_limit) { state->dropped++; return {}; }
        while (state->reserved > state->total_limit - size || state->entries.size() >= state->entry_limit) {
            auto oldest = state->entries.end();
            for (auto it = state->entries.begin(); it != state->entries.end(); ++it)
                if (it->second.use_count() == 1 && (oldest == state->entries.end() || it->second->stamp < oldest->second->stamp)) oldest = it;
            if (oldest == state->entries.end()) { state->dropped++; return {}; }
            state->reserved -= oldest->second->size;
            state->entries.erase(oldest);
        }
        auto entry = std::make_shared<Entry>();
        entry->path = path; entry->size = size; entry->modified = modified; entry->stamp = ++state->clock;
        state->entries.emplace(path, entry);
        try { state->pending.push_back(entry); }
        catch (...) { state->entries.erase(path); return {}; }
        state->reserved += size;
        state->available.notify_one();
        return entry;
    } catch (...) { return {}; }
}

bool MusicCache::read(const Token& entry, int64_t offset, void* output, uint64_t bytes, uint64_t& read_bytes) const {
    if (!entry || !entry->ready.load(std::memory_order_acquire) || !file_io::valid_range(offset, bytes) || (!output && bytes)) return false;
    read_bytes = uint64_t(offset) < entry->data.size() ? std::min<uint64_t>(bytes, entry->data.size() - uint64_t(offset)) : 0;
    if (read_bytes) std::memcpy(output, entry->data.data() + size_t(offset), size_t(read_bytes));
    state->hits.fetch_add(1, std::memory_order_relaxed);
    return true;
}
CacheStats MusicCache::stats() const {
    std::lock_guard guard(state->lock);
    return {state->reserved, state->entries.size(), state->hits.load(), state->completed.load(), state->failed.load(), state->dropped.load()};
}
}
