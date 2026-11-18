"""Wonder frame-step patches, selected by NSO identity and checked before any writes.

Wonder normally turns the NVN present interval (1 or 2) into its simulation step, so
the game runs at the wrong speed if it is rendered at a rate other than 60/30 FPS.
Replace those calculations with loads from a runtime-owned page. The runtime updates
that page from the measured elapsed time between presents.
"""
import struct


BRANCHES = (0xC9528, 0xC9588)
SITE_WORDS = 8
ORIGINAL = (
    0xBD4262C0, 0x52A84E08, 0xAA1F03E9, 0x1E270101,
    0x91022288, 0x52AFF80A, 0x5E21D800, 0x1E211801,
)

ADRP8, LOAD_RATIO, LOAD_SECONDS, NOP = -1, -2, -3, -4
REPLACEMENT = (ADRP8, LOAD_RATIO, LOAD_SECONDS, 2, 4, 5, NOP, NOP)

# The perspective config builder (main+0x160de44) copies the aspect pointer's float
# at main+0x160deac into the projection config. Read a runtime-owned aspect instead.
# This is the same kind of camera-input patch used by TOTK Recomp, not a shader edit.
ASPECT_SITE = 0x160dea4
ASPECT_ORIGINAL = (0xF9402009, 0xAA0803E0, 0xBD400120, 0xBD005900)
ASPECT_LOAD = 0xBD400920  # ldr s0, [x9, #8]

# The supplied Product.120 executable moves both conversions and interleaves an
# ADRP x11 needed immediately after them. Preserve it at its original PC, as well
# as the x8/x9/w10 setup. This is not the 1.0.0 eight-word replacement shifted.
BUILD_100 = "cd6e42aee7934f4d8393cd43893ae25edc83d338000000000000000000000000"
BUILD_120 = "ff773e90972d544eb79406eaa65396d53c43efb9000000000000000000000000"
BRANCHES_120 = (0xC5A28, 0xC5A8C)
ORIGINAL_120 = (0xBD426300, 0x52A84E08, 0xB001514B, 0x1E270101,
                0xAA1F03E9, 0x91022288, 0x52AFF80A, 0x5E21D800, 0x1E211801)
REPLACEMENT_120 = (ADRP8, LOAD_RATIO, 2, LOAD_SECONDS, 4, 5, 6, NOP, NOP)
ASPECT_SITE_120 = 0x173339C


def _adrp(rd, pc, target):
    pages = (target >> 12) - (pc >> 12)
    if not -(1 << 20) <= pages < (1 << 20):
        raise ValueError("Wonder real-time frame-step page is outside ADRP range")
    imm = pages & 0x1FFFFF
    return 0x90000000 | ((imm & 3) << 29) | ((imm >> 2) << 5) | rd


def patch_wonder_realtime(module):
    """Patch main's two present-interval conversions and reserve one guest data page."""
    if module.name != "main":
        return

    build_id = getattr(module, "build_id", "")
    if build_id == BUILD_120:
        branches, original, replacement, aspect_site = (BRANCHES_120, ORIGINAL_120,
                                                       REPLACEMENT_120, ASPECT_SITE_120)
        version = "Product.120"
    elif not build_id or build_id == BUILD_100:
        branches, original, replacement, aspect_site = BRANCHES, ORIGINAL, REPLACEMENT, ASPECT_SITE
        version = "1.0.0"
    else:
        raise ValueError(f"Wonder frame-step patch has no verified layout for NSO build {build_id}")

    bss_start, bss_size = module.bss
    page = (bss_start + bss_size + 0xFFF) & ~0xFFF
    if page < module.text[0] + module.text[1] and page + 0x1000 > module.text[0]:
        raise ValueError("Wonder real-time frame-step page overlaps main text")

    # Validate every site before changing the image: a rejected aspect signature
    # must not leave a half-patched module behind.
    for branch in branches:
        if branch < module.text[0] or branch + len(original) * 4 > module.text[0] + module.text[1]:
            raise ValueError(f"Wonder real-time frame-step site main+{branch:#x} is outside main text")
        actual = struct.unpack_from("<" + "I" * len(original), module.image, branch)
        if actual != original:
            raise ValueError(
                f"Wonder real-time frame-step patch expects game version {version}: "
                f"main+{branch:#x} has {actual!r}"
            )

    if aspect_site < module.text[0] or aspect_site + len(ASPECT_ORIGINAL) * 4 > module.text[0] + module.text[1]:
        raise ValueError(f"Wonder perspective-aspect site main+{aspect_site:#x} is outside main text")
    actual = struct.unpack_from("<" + "I" * len(ASPECT_ORIGINAL), module.image, aspect_site)
    if actual != ASPECT_ORIGINAL:
        raise ValueError(f"Wonder perspective-aspect patch expects game version {version}: "
                         f"main+{aspect_site:#x} has {actual!r}")

    changes = []
    for branch in branches:
        words = []
        for i, item in enumerate(replacement):
            if item == ADRP8:
                word = _adrp(8, branch + i * 4, page)
            elif item == LOAD_RATIO:
                word = 0xBD400100  # ldr s0, [x8]
            elif item == LOAD_SECONDS:
                word = 0xBD400501  # ldr s1, [x8, #4]
            elif item == NOP:
                word = 0xD503201F
            else:
                word = original[item]
            words.append(word)
        changes.append((branch, words))
    aspect_words = (
        _adrp(9, aspect_site, page),
        ASPECT_ORIGINAL[1],  # preserve x0 = projection config
        ASPECT_LOAD,
        ASPECT_ORIGINAL[3],
    )
    changes.append((aspect_site, aspect_words))
    for address, words in changes:
        struct.pack_into("<" + "I" * len(words), module.image, address, *words)

    # load_modules() commits through the end of BSS. Put the page at the end so the
    # runtime can find it through the optional ModuleInfo offset.
    module.bss = (bss_start, page + 0x1000 - bss_start)
    module.frame_step_offset = page
    module.wonder_version = version
