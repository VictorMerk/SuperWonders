#pragma once
#include <cstdint>
#include <string_view>
namespace nx {
// Verified GameFramework::procFrame_ -> WindowSetPresentInterval return sites.
// Both bodies keep x19 as the framework and use the same 0x118/0x24c/0x264/0x268 fields.
inline uint64_t frame_governor_return_site(std::string_view build){
    if(build=="cd6e42aee7934f4d8393cd43893ae25edc83d338000000000000000000000000")return 0xe16ac;
    if(build=="ff773e90972d544eb79406eaa65396d53c43efb9000000000000000000000000")return 0xcfde4;
    return 0;
}
}
