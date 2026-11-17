#include "shader_bundle.h"
#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstring>
#include <fstream>
#include <limits>
#include <new>
#include <mutex>
#include <cstdio>
#if defined(_WIN32)
#include <io.h>
#include <fcntl.h>
#include <share.h>
#include <sys/stat.h>
#else
#include <sys/file.h>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace nx::nvn {
namespace {
namespace fs = std::filesystem;
constexpr char magic[] = "WSPV0001";
constexpr uint64_t record_size = 40, max_file = 512ull<<20, max_shader = 64ull<<20;
uint64_t checksum(std::span<const uint8_t> data) {
    uint64_t h = 0xcbf29ce484222325ull;
    for (auto byte : data) h = (h^byte)*0x100000001b3ull;
    return h;
}
uint64_t get(const uint8_t* p, size_t n = 8) {
    uint64_t v = 0;
    for (size_t i = 0; i < n; ++i) v |= uint64_t(p[i])<<(i*8);
    return v;
}
void put(uint8_t* p, uint64_t v, size_t n = 8) {
    for (size_t i = 0; i < n; ++i) { p[i] = uint8_t(v); v >>= 8; }
}
bool hash_value(const std::string& name, uint64_t& value) {
    if (name.size() != 16) return false;
    const auto r = std::from_chars(name.data(),name.data()+name.size(),value,16);
    return r.ec == std::errc{} && r.ptr == name.data()+name.size();
}
uint64_t timestamp(const fs::path& path, std::error_code& error) {
    // Native clock representation is intentionally local to this cache. A cache
    // copied between platforms/libraries will miss safely and use the source files.
    return uint64_t(fs::last_write_time(path,error).time_since_epoch().count());
}
bool spirv(std::span<const uint8_t> data) {
    return data.size() >= 20 && data.size() <= max_shader && data.size()%4 == 0 && get(data.data(),4) == 0x07230203;
}
}

// Append only already-read translations: first play never waits for a full pack.
// Readers keep immutable bytes. One process holds the writer lock; other game
// instances continue using their source files without contending for that lock.
struct ShaderBundle::Journal {
    fs::path path;
    std::vector<uint8_t> bytes, buffer;
    std::unordered_map<uint64_t,Entry> entries;
    std::unordered_map<uint64_t,std::pair<uint64_t,uint64_t>> written;
    std::mutex lock;
    FILE* file = nullptr;
    bool attempted = false;
    uint64_t end = 0, pending = 0;
    std::chrono::steady_clock::time_point last_flush{};
    explicit Journal(const fs::path& root) : path(root/"spirv.journal") {
        try {
            std::ifstream in(path,std::ios::binary|std::ios::ate);
            if (!in) return;
            const auto length = in.tellg();
            if (length < 0 || uint64_t(length) > max_file) return;
            bytes.resize(size_t(length)); in.seekg(0);
            in.read(reinterpret_cast<char*>(bytes.data()),bytes.size());
            if (!in) { bytes.clear(); return; }
            uint64_t at = 0;
            while (bytes.size()-at >= 48) {
                const auto* h = bytes.data()+at;
                const auto size = get(h+24);
                if (!header(h) || size > bytes.size()-at-48) break;
                const std::span<const uint8_t> code(h+48,size_t(size));
                if (entries.size() >= 100000 && !entries.contains(get(h+8))) break;
                if (spirv(code) && checksum(code) == get(h+32))
                    entries[get(h+8)] = {at+48,size,get(h+16),get(h+32)};
                at += 48+size;
            }
        } catch (const std::bad_alloc&) {
            std::vector<uint8_t>().swap(bytes);
            std::unordered_map<uint64_t,Entry>().swap(entries);
        }
    }
    static bool header(const uint8_t* h) {
        const auto size = get(h+24);
        return !std::memcmp(h,"SWPJ",4) && get(h+4,4) == 1 &&
            size >= 20 && size <= max_shader && !(size%4) && checksum({h,40}) == get(h+40);
    }
    ~Journal() { if (file) std::fclose(file); }
    bool open_writer() {
        if (attempted) return file != nullptr;
        attempted = true;
        int fd = -1;
#if defined(_WIN32)
        if (_wsopen_s(&fd,path.c_str(),_O_RDWR|_O_CREAT|_O_BINARY,_SH_DENYWR,_S_IREAD|_S_IWRITE)) return false;
        file = _fdopen(fd,"r+b");
        if (!file) { _close(fd); return false; }
#else
        fd = ::open(path.c_str(),O_RDWR|O_CREAT,0600);
        if (fd < 0) return false;
        if (flock(fd,LOCK_EX|LOCK_NB)) { ::close(fd); return false; }
        file = fdopen(fd,"r+b");
        if (!file) { ::close(fd); return false; }
#endif
        buffer.resize(1u<<20);
        std::setvbuf(file,reinterpret_cast<char*>(buffer.data()),_IOFBF,buffer.size());
        // Re-scan under the exclusive lock, including records another process
        // may have written since construction. Repair only an incomplete tail.
        std::array<uint8_t,48> h{};
        end = 0;
        while (std::fread(h.data(),1,h.size(),file) == h.size() && header(h.data())) {
            const auto size = get(h.data()+24);
            if (end > max_file-48-size) break;
            std::vector<uint8_t> payload(size_t(size),0);
            if (std::fread(payload.data(),1,payload.size(),file) != payload.size()) break;
            end += 48+size;
        }
        std::clearerr(file);
#if defined(_WIN32)
        if (_chsize_s(fd,end) || _fseeki64(file,end,SEEK_SET)) { close(); return false; }
#else
        if (ftruncate(fd,off_t(end)) || fseeko(file,off_t(end),SEEK_SET)) { close(); return false; }
#endif
        last_flush = std::chrono::steady_clock::now();
        return true;
    }
    void close() { if (file) std::fclose(file); file = nullptr; }
    void append(uint64_t hash, uint64_t modified, std::span<const uint8_t> code) noexcept {
        std::lock_guard guard(lock);
        try {
            const auto sum = checksum(code);
            const auto found = written.find(hash);
            if (found != written.end() && found->second == std::pair{modified,sum}) return;
            if (written.size() >= 100000 || !open_writer() || end > max_file-48-code.size()) return;
            std::array<uint8_t,48> h{};
            std::memcpy(h.data(),"SWPJ",4); put(h.data()+4,1,4); put(h.data()+8,hash);
            put(h.data()+16,modified); put(h.data()+24,code.size()); put(h.data()+32,sum);
            put(h.data()+40,checksum({h.data(),40}));
            if (std::fwrite(h.data(),1,h.size(),file) != h.size() ||
                std::fwrite(code.data(),1,code.size(),file) != code.size()) { close(); return; }
            end += h.size()+code.size(); pending += h.size()+code.size();
            written[hash] = {modified,sum};
            const auto now = std::chrono::steady_clock::now();
            if (pending >= (1u<<20) || now-last_flush >= std::chrono::seconds(2)) {
                if (std::fflush(file)) { close(); return; }
                pending = 0; last_flush = now;
            }
        } catch (...) { close(); }  // Cache failure must never prevent original shader loading.
    }
    void flush() { std::lock_guard guard(lock); if (file && std::fflush(file)) close(); }
};

bool build_shader_bundle(const fs::path& data, std::string& error,
                         const std::function<void(uint64_t,uint64_t)>& progress) {
    error.clear(); std::error_code ec;
    struct File { fs::path path; uint64_t hash; };
    std::vector<File> files;
    fs::directory_iterator it(data/"spirv",ec), end;
    if (ec) { error = "Could not open the translated shader folder: "+ec.message(); return false; }
    while (it != end) {
        const auto entry = *it;
        uint64_t hash = 0;
        if (entry.path().extension() == ".spv" && hash_value(entry.path().stem().string(),hash) && entry.is_regular_file(ec))
            files.push_back({entry.path(),hash});
        if (ec) break;
        it.increment(ec); if (ec) break;
    }
    if (ec || files.empty() || files.size() > 100000) { error = "No usable translated shader set was found."; return false; }
    std::sort(files.begin(),files.end(),[](const File& a,const File& b) { return a.hash < b.hash; });
    fs::path temporary;
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    for (uint32_t i = 0; i < 100; ++i) {
        const auto candidate = data/("shader-bundle-"+std::to_string(stamp)+"-"+std::to_string(i)+".partial");
        if (fs::create_directory(candidate,ec)) { temporary = candidate; break; }
        if (ec && ec != std::errc::file_exists) break;
    }
    if (temporary.empty()) { error = "Could not create the shader cache: "+ec.message(); return false; }
    const auto pending = temporary/"archive";
    std::ofstream out(pending,std::ios::binary);
    auto fail = [&](const std::string& message) {
        error = message; out.close(); std::error_code ignored;
        fs::remove(pending,ignored); fs::remove(temporary,ignored); return false;
    };
    const uint64_t header_size = 16+files.size()*record_size;
    std::vector<uint8_t> header(size_t(header_size),0);
    std::memcpy(header.data(),magic,8); put(header.data()+8,files.size(),4); put(header.data()+12,record_size,4);
    out.write(reinterpret_cast<const char*>(header.data()),header.size());
    uint64_t offset = header_size;
    if (progress) progress(0,files.size());
    for (size_t i = 0; i < files.size(); ++i) {
        const auto& file = files[i];
        const auto size = fs::file_size(file.path,ec);
        if (ec || size < 20 || size > max_shader || offset > max_file-size) return fail("A shader is missing or too large.");
        const auto modified = timestamp(file.path,ec);
        if (ec) return fail("A shader's modification time could not be read.");
        std::ifstream input(file.path,std::ios::binary);
        std::vector<uint8_t> code(size_t(size),0);
        input.read(reinterpret_cast<char*>(code.data()),code.size());
        if (!input || !spirv(code)) return fail("A translated shader is invalid; prepare the game files again.");
        if (fs::file_size(file.path,ec) != size || ec || timestamp(file.path,ec) != modified || ec)
            return fail("Shaders changed during cache preparation; retry after shader translation finishes.");
        auto* record = header.data()+16+i*record_size;
        put(record,file.hash); put(record+8,offset); put(record+16,size);
        put(record+24,modified); put(record+32,checksum(code));
        out.write(reinterpret_cast<const char*>(code.data()),code.size());
        if (!out) return fail("Writing the shader cache failed; check free disk space.");
        offset += size;
        if (progress && ((i+1)%32 == 0 || i+1 == files.size())) progress(i+1,files.size());
    }
    out.seekp(0); out.write(reinterpret_cast<const char*>(header.data()),header.size()); out.flush();
    if (!out) return fail("Writing the shader cache index failed.");
    out.close();
    fs::rename(pending,data/"spirv.pack",ec);
    if (ec) return fail("Replacing the shader cache failed: "+ec.message());
    fs::remove(temporary,ec);
    // The complete pack supersedes the learned journal. An active Windows writer
    // prevents removal; keeping it in that case remains safe.
    fs::remove(data/"spirv.journal",ec);
    return true;
}

ShaderBundle::ShaderBundle(const fs::path& data, bool enabled) : directory(data/"spirv") {
    if (!enabled) return;
    try { journal = std::make_unique<Journal>(data); }
    catch (const std::bad_alloc&) { return; }
    try {
    std::ifstream input(data/"spirv.pack",std::ios::binary|std::ios::ate);
    if (!input) return;
    const auto length = input.tellg();
    if (length < 16 || uint64_t(length) > max_file-journal->bytes.size()) return;
    std::array<uint8_t,16> prefix{};
    input.seekg(0); input.read(reinterpret_cast<char*>(prefix.data()),prefix.size());
    if (!input || std::memcmp(prefix.data(),magic,8) || get(prefix.data()+12,4) != record_size) return;
    const uint64_t count = get(prefix.data()+8,4), header = 16+count*record_size;
    if (!count || count > 100000 || header > uint64_t(length)) return;
    std::vector<uint8_t> table(size_t(header),0);
    std::memcpy(table.data(),prefix.data(),prefix.size());
    input.read(reinterpret_cast<char*>(table.data()+16),table.size()-16);
    if (!input) return;
    std::unordered_map<uint64_t,Entry> index;
    for (uint64_t i = 0; i < count; ++i) {
        const auto* r = table.data()+16+i*record_size;
        Entry e{get(r+8),get(r+16),get(r+24),get(r+32)};
        if (e.size < 20 || e.size > max_shader || e.size%4 || e.offset < header ||
            e.offset > uint64_t(length) || e.size > uint64_t(length)-e.offset || !index.emplace(get(r),e).second) return;
    }
    // Validate the complete index before committing memory for the large payload.
    std::vector<uint8_t> content(size_t(length),0);
    std::memcpy(content.data(),table.data(),table.size());
    input.read(reinterpret_cast<char*>(content.data()+header),content.size()-size_t(header));
    if (!input) return;
    entries = std::move(index); bytes = std::move(content);
    } catch (const std::bad_alloc&) { entries.clear(); bytes.clear(); }
}

ShaderBundle::~ShaderBundle() = default;
size_t ShaderBundle::size() const { return entries.size()+(journal ? journal->entries.size() : 0); }
size_t ShaderBundle::memory_bytes() const { return bytes.size()+(journal ? journal->bytes.size() : 0); }
void ShaderBundle::flush() { if (journal) journal->flush(); }
std::vector<uint8_t> ShaderBundle::load(const std::string& hash) {
    uint64_t id = 0;
    if (!hash_value(hash,id)) return {};
    const auto cached = find(hash);
    if (!cached.empty()) return {cached.begin(),cached.end()};
    const auto source = directory/(hash+".spv");
    std::error_code ec;
    const auto size = fs::file_size(source,ec);
    if (ec || size < 20 || size > max_shader) return {};
    const auto modified = journal ? timestamp(source,ec) : 0;
    if (ec) return {};
    std::ifstream input(source,std::ios::binary);
    std::vector<uint8_t> code(size_t(size),0);
    input.read(reinterpret_cast<char*>(code.data()),code.size());
    if (!input) return {};
    if (journal && spirv(code) && fs::file_size(source,ec) == size && !ec && timestamp(source,ec) == modified && !ec)
        journal->append(id,modified,code);
    return code;
}

std::span<const uint8_t> ShaderBundle::find(const std::string& hash) const {
    uint64_t id = 0;
    if (!hash_value(hash,id)) return {};
    if (!entries.contains(id) && (!journal || !journal->entries.contains(id))) return {};
    const auto source = directory/(hash+".spv");
    std::error_code ec;
    const auto size = fs::file_size(source,ec);
    if (ec) return {};
    const auto modified = timestamp(source,ec);
    if (ec) return {};
    const auto lookup = [&](const auto& index, const auto& content) -> std::span<const uint8_t> {
        const auto found = index.find(id);
        if (found == index.end()) return {};
        const auto& e = found->second;
        if (size != e.size || modified != e.timestamp) return {};
        const std::span<const uint8_t> code(content.data()+e.offset,size_t(e.size));
        return spirv(code) && checksum(code) == e.checksum ? code : std::span<const uint8_t>{};
    };
    if (journal) { const auto cached = lookup(journal->entries,journal->bytes); if (!cached.empty()) return cached; }
    return lookup(entries,bytes);
}
}
