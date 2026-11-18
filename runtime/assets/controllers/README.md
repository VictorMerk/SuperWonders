# Controller artwork

Illustrations from Al. Lopez / AL2009man, Gamepad Asset Pack (MIT).
Upstream repository: https://github.com/AL2009man/Gamepad-Asset-Pack
See SOURCES.json for pinned commit, original path and dimensions for each image.
PNG files were resized proportionally to at most 640 x 440 for the menu atlas;
no controller parts were redrawn. Retain LICENSE and SOURCES.json in distributions.

Art is embedded at configure time and decoded into colored rectangles of ImGui's
existing font atlas after font setup, before the Vulkan backend uploads it.
There is no runtime download, external image path, extra descriptor or GPU queue.
Steam Deck's chroma-key screen is covered by a native menu panel when drawn.
The small paired Joy-Con illustration describes a pair, not every single-grip mode.

Physical hit regions are normalized to each source image. Shoulder chips supply
inputs not pictured by a model's front view. Selection uses SDL's stable physical
button order, independently of game mappings; artwork never changes bindings.
