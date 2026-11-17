import os, sys, struct, argparse
sys.path.insert(0, os.path.dirname(__file__))
from wonder_menu_mod import unzs, zs, sarc_get, sarc_replace, flyt_sections, pane_name, rstb_grow

def wrap(data, targets):
    out = bytearray(data)
    for name, shift in targets:
        sections = list(flyt_sections(out))
        found = [(i,o,s) for i,(o,t,s) in enumerate(sections) if t in (b'pan1', b'pic1', b'prt1') and pane_name(out,o) == name]
        if len(found) != 1: raise ValueError('pane not unique: '+name)
        i,o,size = found[0]
        # Include user data and the complete original child tree under the new parent.
        j=i+1
        if j<len(sections) and sections[j][1]==b'usd1': j+=1
        if j<len(sections) and sections[j][1]==b'pas1':
            depth=0
            while j<len(sections):
                typ=sections[j][1]
                if typ==b'pas1': depth+=1
                elif typ==b'pae1':
                    depth-=1
                    if depth==0:
                        j+=1
                        break
                j+=1
            if depth: raise ValueError('unbalanced pane tree')
        end=sections[j][0] if j<len(sections) else len(out)
        root_o,root_t,root_size = next((a,b,c) for a,b,c in sections if b == b'pan1' and pane_name(out,a) == 'RootPane')
        parent=bytearray(out[root_o:root_o+root_size])
        # Clone the original pane to preserve its format; remove its transform and use the requested translation.
        parent[12:36]=('UW_'+name).encode().ljust(24,b'\0')
        struct.pack_into('<fff',parent,0x2c,shift,0.,0.)
        struct.pack_into('<fff',parent,0x38,0.,0.,0.)
        struct.pack_into('<ff',parent,0x44,1.,1.)
        original=out[o:end]
        out[o:end]=parent+b'pas1'+struct.pack('<I',8)+original+b'pae1'+struct.pack('<I',8)
        struct.pack_into('<I',out,0xc,len(out))
        struct.pack_into('<H',out,0x10,struct.unpack_from('<H',out,0x10)[0]+3)
    return bytes(out)

def main(argv=None):
    ap=argparse.ArgumentParser();ap.add_argument('--romfs',required=True);ap.add_argument('--out',required=True);ap.add_argument('--aspect',type=float,default=32/9);ap.add_argument('--rstb',required=True);args=ap.parse_args(argv)
    if not 16/9 <= args.aspect <= 64/9: raise ValueError('unsupported aspect')
    shift=960*(args.aspect/(16/9)-1)
    source=os.path.join(args.romfs,'Layout','WorldMapDisplay.Nin_NX_NVN.blarc.zs')
    archive=unzs(source); name='blyt/WorldMapDisplay.bflyt'
    modified=wrap(sarc_get(archive,name), [('N_Header01',-shift),('N_Footer01',shift)])
    dest=os.path.join(args.out,'romfs','Layout');os.makedirs(dest,exist_ok=True)
    new_archive=sarc_replace(archive,{name:modified})
    with open(os.path.join(dest,os.path.basename(source)),'wb') as f:f.write(zs(new_archive))
    # Start from the active menu mod's table so its own enlarged resources remain allocated.
    table=unzs(args.rstb)
    table=rstb_grow(table, {'Layout/WorldMapDisplay.Nin_NX_NVN.blarc': len(new_archive)-len(archive)+0x2000})
    table_dir=os.path.join(args.out,'romfs','System','Resource');os.makedirs(table_dir,exist_ok=True)
    with open(os.path.join(table_dir,os.path.basename(args.rstb)),'wb') as f:f.write(zs(table))
    print('Created experimental world-map HUD mod; offset',shift)
if __name__=='__main__':main()
