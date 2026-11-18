# Brackets in extracted-game paths

The user's r8 log showed a useful contradiction: the native file check accepted `Shader/*.zs`, but Python rejected
the same RomFS. The extraction's parent contained `[BASE]`. Python's `glob` interpreted that literal directory
name as a character class because the script concatenated the root and wildcard into one pattern.
The failure occurred before copying/building, and the log does not establish low disk space or an incomplete dump.

The fix escapes the literal filesystem root while preserving the intended wildcard suffix. It applies to:

- initial `Shader/*.zs` validation and Maxwell tool dependency enumeration;
- shader archive extraction and program catalog discovery, across all five archive groups;
- the menu modification's resource-size table;
- recompiler dependency enumeration and generated C++ discovery when the kit/output path itself has brackets.

Five synthetic regressions check actual setup validation, every shader archive group/catalog, generated compilation
units (with a stub compiler), kit dependencies and unique menu size-table selection. The first four failed before
the correction and pass afterward; the additional menu regression passes too. No game assets or real compilation
are needed. Missing shaders and multiple size tables remain errors. The Windows runtime builds and the focused
setup/discovery/reporter CTest targets pass.

## Small r8 setup-only patch

A private 17 KB ZIP carries only three patched official-kit scripts plus instructions/hash metadata. It changes
`setup_game.py`, `tools/maxwell/extract_romfs_shaders.py` and `tools/wonder_menu_mod.py`. The r8 EXEs, recompiler,
`gamebuild.py` and `kit-id.txt` stay unchanged, preserving the original r8 game-code identity used by LAN matching.
Close/cancel setup before replacing scripts. Apply over the existing complete r8 kit; this is not a standalone
first-install package. Renaming the source parent to a simple literal name, then reselecting it, is an immediate
workaround for the original scripts. It does not repair genuinely missing files or unsupported game versions.

The source-tree `gamebuild.py` correction belongs to the next complete kit, whose kit ID must be regenerated.
Dropping that file alone into the old r8 kit would change its computed ID and can force rebuilding/mismatches;
it is deliberately excluded from the small r8 patch.

## Launcher error in the same log

The log also reported `Missing EndDisabled()` in the native launcher while navigating during setup. A sidebar row
conditionally began a disabled scope based on the selected page, changed that page during navigation, then used a
fresh page comparison to decide whether to end it. The two decisions could differ. The row now always pairs
`BeginDisabled(row_disabled)` with `EndDisabled()` and uses the entry-state snapshot to suppress navigation into
disabled rows. This correction is in source/the next runtime, not the three-script r8 hotfix. Native testing of
the changed setup-navigation behavior is deferred while the user is using Wonder; the earlier r8 setup-page
captures do not establish this later correction.
