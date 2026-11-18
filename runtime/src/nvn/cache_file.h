#pragma once
#include <chrono>
#include <filesystem>
#include <string>

namespace nx::nvn {
// Every writer owns a distinct directory beside the destination. A failed or
// interrupted write leaves the previous cache intact; concurrent processes
// cannot truncate each other's temporary file.
class CacheFile {
    std::filesystem::path target, directory, pending;
public:
    explicit CacheFile(const std::string& destination)
        : target(reinterpret_cast<const char8_t*>(destination.c_str())) {
        const auto stamp=std::chrono::steady_clock::now().time_since_epoch().count();
        for(unsigned i=0;i<100;++i) {
            auto name=target.filename(); name+=".pending-"+std::to_string(stamp)+"-"+std::to_string(i);
            const auto candidate=target.parent_path()/name;
            std::error_code error;
            if(std::filesystem::create_directory(candidate,error)) {
                directory=candidate; pending=directory/"data"; break;
            }
            if(error && error!=std::errc::file_exists) break;
        }
    }
    CacheFile(const CacheFile&)=delete;
    CacheFile& operator=(const CacheFile&)=delete;
    ~CacheFile() {
        if(directory.empty()) return;
        std::error_code ignored;
        std::filesystem::remove(pending,ignored);
        std::filesystem::remove(directory,ignored);  // only the empty directory we created
    }
    std::string path() const {
        const auto value=pending.u8string(); return {reinterpret_cast<const char*>(value.data()),value.size()};
    }
    bool commit() {
        if(pending.empty()) return false;
        std::error_code error;
        std::filesystem::rename(pending,target,error);
        return !error;
    }
};
}
