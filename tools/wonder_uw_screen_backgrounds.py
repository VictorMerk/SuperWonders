"""Generate experimental widescreen background layouts from the player's data."""
import argparse
import os
import struct
from wonder_menu_mod import unzs, zs, sarc_get, sarc_nodes, flyt_sections, pane_name

PANES = {
    'Boot': ('P_BG',),
    'CourseIn': ('P_BG', 'P_Ground', 'P_PtnJagStatic', 'P_PtnJag'),
    'CourseOut': ('P_BG_00',),
    'Title': ('P_Ptn', 'P_Gradation', 'P_PtnComplete', 'P_BG'),
    'FadeBlack': ('P_Fade',),
    'FadeWhite': ('P_Fade',),
    'FadeBlackDemo': ('P_Fade',),
    'FadeWhiteDemo': ('P_Fade',),
    'AccountSelect': ('P_Header', 'P_Footer'),
    'PlayerNumSelect': ('P_Header', 'P_Footer'),
    'Option': ('P_BG', 'P_Header', 'P_Footer'),
    'TitleBlurBG': ('P_Mask',),
    'CharacterSelect': ('P_BG', 'P_CapEasyBG', 'P_Header', 'P_ColorLineH01', 'P_ColorLineH02', 'P_ColorLineH03', 'P_Footer'),
    'MainMenu': ('P_Header', 'P_Header_00', 'P_ColorLineH01', 'P_ColorLineH02', 'P_ColorLineH03', 'P_Footer', 'P_Footer_00'),
    'BadgeShop': ('P_HeaderEdge', 'P_Header', 'P_Footer'),
    'WipeToDown': ('P_WipeToDown',),
    'WipeToUp': ('P_WipeToUp',),
    'WipeToLeft': ('P_WipeToLeft',),
    'WipeToRight': ('P_WipeToLeft',),
    'WipeWarp': ('P_WipeToUp',),
}

def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument('--romfs', required=True)
    ap.add_argument('--menu-mod', required=True, help='active recomp-menu romfs')
    ap.add_argument('--out', required=True, help='new mod directory')
    ap.add_argument('--aspect', type=float, default=32/9)
    args = ap.parse_args(argv)
    factor = args.aspect / (16/9)
    if not 1.0 <= factor <= 3.0:
        raise ValueError('unsupported aspect')
    for name, panes in PANES.items():
        filename = name + '.Nin_NX_NVN.blarc.zs'
        root = args.menu_mod if name == 'Title' else args.romfs
        archive = unzs(os.path.join(root, 'Layout', filename))
        layout_name = 'blyt/' + name + '.bflyt'
        layout = bytearray(sarc_get(archive, layout_name))
        found = set()
        for off, tag, _ in flyt_sections(layout):
            if tag != b'pic1':
                continue
            pane = pane_name(layout, off)
            if pane in panes:
                width = struct.unpack_from('<f', layout, off + 0x4c)[0]
                if abs(width - 1920.0) > 0.01 and abs(width - 2000.0) > 0.01:
                    raise ValueError(f'{name}/{pane}: unexpected width {width}')
                struct.pack_into('<f', layout, off + 0x4c, width * factor)
                found.add(pane)
        if found != set(panes):
            raise ValueError(f'{name}: missing panes {set(panes) - found}')
        if name == 'Boot':
            for off, tag, _ in flyt_sections(layout):
                if tag == b'prt1' and pane_name(layout, off) == 'L_WonderFlowerMorphing':
                    x = struct.unpack_from('<f', layout, off + 0x2c)[0]
                    struct.pack_into('<f', layout, off + 0x2c, x + 960.0 * (factor - 1.0))
        if name == 'Title':
            for off, tag, _ in flyt_sections(layout):
                pane = pane_name(layout, off) if tag in (b'pic1', b'txt1') else ''
                if pane == 'P_Logo' or pane in ('T_QuitGame', 'T_QuitGameUS'):
                    x = struct.unpack_from('<f', layout, off + 0x2c)[0]
                    shift = 960.0 * (factor - 1.0)
                    struct.pack_into('<f', layout, off + 0x2c, x + (-shift if pane == 'P_Logo' else shift))
        result = bytearray(archive)
        nodes = [(start, end) for entry, _, start, end in sarc_nodes(archive) if entry == layout_name]
        if len(nodes) != 1 or nodes[0][1] - nodes[0][0] != len(layout):
            raise ValueError(f'{name}: layout size changed')
        start, end = nodes[0]
        result[start:end] = layout
        target = os.path.join(args.out, 'romfs', 'Layout', filename)
        os.makedirs(os.path.dirname(target), exist_ok=True)
        with open(target, 'wb') as f:
            f.write(zs(bytes(result)))
        print(name, ', '.join(panes))

if __name__ == '__main__':
    main()
