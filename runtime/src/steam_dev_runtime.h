#pragma once
namespace nx::steam_dev {
void start_from_environment();
void pump_current();
bool active() noexcept;
void shutdown_current();
}
