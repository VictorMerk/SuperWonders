#pragma once
#include <cstring>
#include <nx/gamelib.h>

namespace nx::wonder {
enum class Version { Unknown, Original100, Product120 };
inline Version source_version() {
    const auto* module = game_lib().main;
    if (!module || !module->build_id) return Version::Unknown;
    if (std::strcmp(module->build_id,
        "cd6e42aee7934f4d8393cd43893ae25edc83d338000000000000000000000000") == 0)
        return Version::Original100;
    if (std::strcmp(module->build_id,
        "ff773e90972d544eb79406eaa65396d53c43efb9000000000000000000000000") == 0)
        return Version::Product120;
    return Version::Unknown;
}
}
