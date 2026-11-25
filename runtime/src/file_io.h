#pragma once
#include <cerrno>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <sys/stat.h>
#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

namespace nx::file_io {
inline bool valid_range(int64_t offset,uint64_t bytes) {
    return offset >= 0 && bytes <= uint64_t(std::numeric_limits<int64_t>::max()-offset);
}
// Metadata lookup must not seek a FILE that a streaming read is using.
inline int64_t size(FILE* file) {
    if (!file) return -1;
#if defined(_WIN32)
    struct _stat64 info{};
    return _fstat64(_fileno(file),&info)==0 ? int64_t(info.st_size) : -1;
#else
    struct stat info{};
    return fstat(fileno(file),&info)==0 ? int64_t(info.st_size) : -1;
#endif
}
inline uint64_t read_at(FILE* file,int64_t offset,void* output,uint64_t bytes) {
    if (!file || !valid_range(offset,bytes) || !bytes || !output) return 0;
#if defined(_WIN32)
    _lock_file(file);
    const uint64_t read = _fseeki64_nolock(file,offset,SEEK_SET)==0 ? _fread_nolock(output,1,size_t(bytes),file) : 0;
    _unlock_file(file);
    return read;
#else
    if (bytes > uint64_t(SSIZE_MAX)) return 0;
    uint64_t done = 0;
    while (done < bytes) {
        const ssize_t n = pread(fileno(file),static_cast<uint8_t*>(output)+done,size_t(bytes-done),offset+int64_t(done));
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        done += uint64_t(n);
    }
    return done;
#endif
}
// Seek, write and flush form one operation with respect to reads of this FILE.
inline bool write_at(FILE* file,int64_t offset,const void* input,uint64_t bytes) {
    if (!file || !valid_range(offset,bytes)) return false;
    if (!bytes) return true;
    if (!input) return false;
#if defined(_WIN32)
    _lock_file(file);
    const bool seek = _fseeki64_nolock(file,offset,SEEK_SET)==0;
    const bool wrote = seek && _fwrite_nolock(input,1,size_t(bytes),file)==bytes;
    const bool flushed = _fflush_nolock(file)==0;
    _unlock_file(file);
#else
    flockfile(file);
    const bool seek = fseeko(file,off_t(offset),SEEK_SET)==0;
    const bool wrote = seek && std::fwrite(input,1,size_t(bytes),file)==bytes;
    const bool flushed = std::fflush(file)==0;
    funlockfile(file);
#endif
    return wrote && flushed;
}
}
