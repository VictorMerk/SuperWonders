# Optical-flow frame generation reference

The four GLSL compute shaders and the three-level motion-search/synthesis design
are adapted from [adriadam10/mkw-recomp](https://github.com/adriadam10/mkw-recomp/tree/dd9297545beb9d2c90170eea93a01030961bf7cf),
`runtime/gfx/vk/device/vk_framegen.c` and `runtime/shaders/fg_*.comp` (MIT).
Their original notices identify adaptations from SkyboundTotk's
`rx_framegen.cpp`, commit `a4495f95b5e5ab6562d603058dcaa3457d0c8027`,
Copyright (c) 2026 NotRexed, MIT. Notices are retained in the shaders and LICENSE.

Wonder's integration uses its own allocation, descriptor sets, image transitions,
frame submission and presentation scheduling. It generates one intermediate frame
from two pictures; it does not use motion vectors, DLSS or FSR 3. It is optional
and off by default. GPU cost and visual artifacts must be assessed on the target
hardware. The synthetic GPU probe exercises actual interpolation and resizing
without game assets (`NX_FRAMEGEN_PROBE=1 NX_HEADLESS=1 NX_FRAMEGEN=1`).
