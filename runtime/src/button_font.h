#pragma once
#include "controllers.h"
#include "overlay/settings.h"
#include <array>
#include <string>
#include <vector>

namespace nx {
// A new copy per launch: the guest caches rasterized glyphs, so changes need a restart.
std::vector<uint8_t> pc_button_font(const overlay::Settings& settings, plat::ControllerKind kind,
                                  const std::array<std::string, 16>& key_names = {});
const char* physical_button_name(plat::ControllerKind kind, int physical);
}
