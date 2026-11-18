#!/usr/bin/env python3
"""Build redistributable PC prompt fonts from the bundled OFL Nunito font.

No game or system-font data is used. Requires fontTools. The format-12 cmap
deliberately has one group per character so the runtime can remap bindings
without moving tables or changing the font's size.
"""
from pathlib import Path
import struct

from fontTools import subset
from fontTools.ttLib import TTFont
from fontTools.ttLib.tables.DefaultTable import DefaultTable
from fontTools.pens.ttGlyphPen import TTGlyphPen
from fontTools.pens.cu2quPen import Cu2QuPen
from make_button_font import Builder, ICONS

ROOT = Path(__file__).resolve().parents[2]
EXTRA_KEYS = ['Space', 'Shift', 'Ctrl', 'Alt', 'Enter', 'Esc', 'Tab', 'Back',
              'Left', 'Up', 'Right', 'Down', 'Home', 'End', 'PgUp', 'PgDn', 'Ins', 'Del']
EXTRA_KEYS += [f'F{i}' for i in range(1, 13)]


class PCBuilder(Builder):
    def build(self, kind, arg):
        if kind != 'ps':
            return super().build(kind, arg)
        tt = TTGlyphPen(None)
        pen = Cu2QuPen(tt, max_err=1.0, reverse_direction=False)
        S, cy = self.size, self.cy
        cx = self.side + S / 2
        self.circle(pen, cx, cy, S / 2)
        a, t = .25 * S, .055 * S
        if arg == 'cross':
            pts = [(-t,a),(t,a),(t,t),(a,t),(a,-t),(t,-t),(t,-a),(-t,-a),(-t,-t),(-a,-t),(-a,t),(-t,t)]
            self.poly(pen, self._rot(pts, 45, cx, cy), hole=True)
        elif arg == 'circle':
            self.circle(pen, cx, cy, a, hole=True)
            self.circle(pen, cx, cy, a - t)
        elif arg == 'square':
            self.rrect(pen, cx-a, cy-a, cx+a, cy+a, 0, hole=True)
            self.rrect(pen, cx-a+t, cy-a+t, cx+a-t, cy+a-t, 0)
        else:
            pts = [(cx,cy+a*1.2),(cx+a*1.1,cy-a),(cx-a*1.1,cy-a)]
            self.poly(pen, pts, hole=True)
            self.poly(pen, [(cx+(x-cx)*.67,cy+(y-cy)*.67) for x,y in pts])
        return tt.glyph(), round(S + 2*self.side)


def main():
    out = ROOT / 'third_party/fonts/PCButtons'
    out.mkdir(exist_ok=True)
    for device in ('nintendo', 'xbox', 'playstation'):
        font = TTFont(ROOT / 'third_party/fonts/Nunito/Nunito-SemiBold.ttf')
        sub = subset.Subsetter()
        sub.populate(unicodes=range(32, 127))
        sub.subset(font)
        b = PCBuilder(font)
        icons = dict(ICONS)
        face = {'nintendo': ['B','A','Y','X'], 'xbox': ['A','B','X','Y'],
                'playstation': ['cross','circle','square','triangle']}[device]
        labels = {'nintendo': ['L','R','ZL','ZR','+','-','L','R'],
                  'xbox': ['LB','RB','LT','RT','Menu','View','LS','RS'],
                  'playstation': ['L1','R1','L2','R2','Opt','Share','L3','R3']}[device]
        physical = [('ps' if device == 'playstation' else 'circle_letter', x) for x in face]
        physical += [('shoulder', x) for x in labels]
        physical += [('dpad', x) for x in ('left','up','right','down')]
        for i, (kind, arg) in enumerate(physical):
            icons[0xE200+i] = (f'physical{i}', kind, arg)
        keys = [(0xE300+c, chr(c)) for c in range(33, 127)]
        keys += [(0xE400+i, x) for i,x in enumerate(EXTRA_KEYS)]
        for cp, label in keys:
            icons[cp] = (f'key{cp:x}', 'shoulder', label)
        order = font.getGlyphOrder()
        mapping = dict(font.getBestCmap())
        for cp, (name, kind, arg) in icons.items():
            glyph, adv = b.build(kind, arg)
            order.append(name)
            font['glyf'][name] = glyph
            glyph.recalcBounds(font['glyf'])
            font['hmtx'][name] = (adv, getattr(glyph, 'xMin', 0))
            mapping[cp] = name
        font.setGlyphOrder(order)
        font['glyf'].glyphOrder = order
        font['maxp'].numGlyphs = len(order)
        for name_id in (1, 4, 6):
            for record in font['name'].names:
                if record.nameID == name_id:
                    record.string = f'SuperWonders PC Buttons {device}'.encode(record.getEncoding())
        # A Unicode cmap supported by the guest font engine; separate groups
        # make each button independently replaceable even with custom bindings.
        groups = b''.join(struct.pack('>III', cp, cp, order.index(name)) for cp,name in sorted(mapping.items()))
        subtable = struct.pack('>HHIII', 12, 0, 16+len(groups), 0, len(mapping)) + groups
        cmap = DefaultTable('cmap')
        cmap.data = struct.pack('>HHHHI', 0, 1, 3, 10, 12) + subtable
        font['cmap'] = cmap
        for tag in ('hdmx','LTSH','DSIG','VDMX','GPOS','GSUB','GDEF'):
            if tag in font:
                del font[tag]
        font.save(out / f'{device}.ttf')
        print(device, (out / f'{device}.ttf').stat().st_size)


if __name__ == '__main__':
    main()
