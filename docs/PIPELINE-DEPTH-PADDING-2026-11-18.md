# Remove undefined padding from graphics-pipeline identities

`BindDepthStencilState` recorded the raw bytes of a decoded depth/stencil struct. Three byte-sized flags were followed by a 32-bit comparison enum, leaving an unnamed alignment byte between them. Member initializers set the real fields but did not define that padding byte. Command replay copied it into the graphics-pipeline key, whose identity is compared and hashed byte for byte.

The same depth, shaders and colour state could consequently create another Vulkan pipeline solely because a stack byte changed. This especially affects the monolithic pipeline path used by the tested AMD device. The GPL dynamic path clears the complete depth state before lookup, which can hide the defect on other configurations. This is an explanation from the source paths, not an NVIDIA hardware test.

The decoded struct now names that byte `reserved` and initializes it to zero. The key canonicalizer also clears it for legacy recorded command blobs and persisted recipes. Static assertions preserve the existing 40-byte struct size and comparison-enum offset and require an object representation with no remaining implicit padding. Recipe format/version and live depth/stencil fields are unchanged.

## Current evidence

The CPU regression checks default initialization, every possible legacy byte value, idempotence, and preservation of depth enable/write/compare and every front/back stencil operation. Both padding and existing blend-state regression tests pass.

A read-only audit of an isolated 3,449-entry recipe fixture found 2,218 identities after the earlier blend fix and 381 after also clearing the padding. A separate 11,970-entry session snapshot reduces to 6,066 with the blend fix and 1,779 with both fixes. These are recipe counts, not FPS, time saved or confirmation that every recorded shader is supported.

Native pixel/identity checks, compatible-cache replay timings and real-course verification for this additional fix are pending. No unverified runtime is installed from this branch.
