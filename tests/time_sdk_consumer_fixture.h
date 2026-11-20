#pragma once
#include <cstdint>
#include <cstring>
#include <span>
#include <stdexcept>

// Independently derived from the ORIGINAL NSO consumers, not producer constants.
// No guest instructions/assets are included. Both versions read a u32 sequence
// at 0xd0, select a 56-byte record by its low bit, compare the record's source
// UUID with the system context, and apply signed nanosecond adjustment BEFORE
// truncating to seconds. The neutral multiplier/offset/limit case is sufficient
// here; interpreting non-neutral slew schedules is intentionally out of scope.
namespace time_sdk_fixture {
struct Profile { const char* name; uint64_t steady, calculate; const char* calculate_sha256; };
inline constexpr Profile profiles[] = {
    {"Original100",0x3d0300,0x3d0440,"ab1b230efcae02fb7e69c8fb3c7527e63ac3af9c40020490a2a965f3864eeb49"},
    {"Product120",0x3d0ae0,0x3d0c20,"2c03335e646bf40e236b1cfd25a73d575af4af912f63d7dd2e5b083159ff7264"},
};
template<class T> T read(std::span<const uint8_t> page,size_t offset) {
    if(offset>page.size() || sizeof(T)>page.size()-offset) throw std::out_of_range("clock fixture span");
    T value; std::memcpy(&value,page.data()+offset,sizeof(value)); return value;
}
struct Result { uint32_t error; int64_t seconds; uint64_t source_lo,source_hi; };
inline Result current(const Profile&,std::span<const uint8_t> page,size_t context_counter,int64_t now_ns) {
    const size_t context=context_counter+8+32*(read<uint32_t>(page,context_counter)&1);
    const size_t adjustment=0xd8+56*(read<uint32_t>(page,0xd0)&1);
    const auto lo=read<uint64_t>(page,context+16),hi=read<uint64_t>(page,context+24);
    if(!(lo|hi) || lo!=read<uint64_t>(page,adjustment+40) || hi!=read<uint64_t>(page,adjustment+48))
        return {0xcc74,0,0,0};
    // Signed 64-bit baseline/multiplier/shift/offset/limit, as loaded by the
    // five x-register loads. Zero multiplier/offset/limit gives zero slew.
    (void)read<int64_t>(page,adjustment);
    (void)read<int64_t>(page,adjustment+16);
    if(read<int64_t>(page,adjustment+8) || read<int64_t>(page,adjustment+24) || read<int64_t>(page,adjustment+32))
        throw std::runtime_error("fixture only models neutral adjustment");
    return {0,now_ns/1000000000+read<int64_t>(page,context),lo,hi};
}
}
