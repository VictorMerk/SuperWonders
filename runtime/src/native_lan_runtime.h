#pragma once
namespace nx::native_lan {
// Called by the loader after mapping and before resolving any guest imports.
// NX_NATIVE_LAN=1 only; otherwise leaves every HLE/default entry untouched.
void install_before_relocations();
bool active() noexcept;
// Explicit Windows close path also invokes this; ExitProcess bypasses atexit.
void shutdown() noexcept;
}
