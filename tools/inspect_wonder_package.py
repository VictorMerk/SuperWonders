"""Inspect a Wonder NUP and optionally extract its executables into a new private folder.

Usage: python tools/inspect_wonder_package.py GAME.nup [--json REPORT.json]
       [--extract-exefs NEW_DIRECTORY] [--extract-romfs NEW_DIRECTORY] [--verify-all]
The report describes measured contents, never infers a supported edition from the filename.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct

from nup_package import Nup, PackageError


def nso_image(data):
    if len(data) < 256 or data[:4] != b'NSO0':
        raise PackageError('Executable is not a decrypted NSO')
    flags = struct.unpack_from('<I', data, 12)[0]
    segments = []
    for i in range(3):
        off, address, size = struct.unpack_from('<III', data, 16 + i * 16)
        stored = struct.unpack_from('<I', data, 96 + i * 4)[0] if flags & (1 << i) else size
        if max(size, address + size) > 512 << 20 or off + stored > len(data):
            raise PackageError('Invalid NSO segment')
        segment = data[off:off + stored]
        if flags & (1 << i):
            try:
                import lz4.block
            except ImportError as e:
                raise PackageError('Executable inspection needs Python lz4') from e
            segment = lz4.block.decompress(segment, uncompressed_size=size)
        if len(segment) != size:
            raise PackageError('Invalid NSO segment length')
        segments.append((address, segment))
    image = bytearray(max(a + len(s) for a, s in segments))
    for a, segment in segments:
        image[a:a + len(segment)] = segment
    return bytes(image)


def inspect(package):
    report = {'package_bytes': package.size, 'titles': [], 'gameplay_verified': False}
    for index, title in enumerate(package.titles):
        exefs, romfs = package.entries('exefs', index), package.entries('romfs', index)
        item = dict(title, index=index, exefs_files=len(exefs), romfs_files=len(romfs), modules=[])
        item['resource_products'] = sorted({int(m.group(1)) for n in romfs
            if (m := re.search(r'^System/Resource/ResourceSizeTable\.Product\.(\d+)\.', n))})
        for name, entry in exefs.items():
            if name not in ('main', 'sdk', 'rtld') and not re.fullmatch(r'subsdk\d+', name):
                continue
            data = package.read(entry)
            module = {'name': name, 'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest()}
            if data[:4] == b'NSO0':
                module['build_id'] = data[64:96].hex()
                try:
                    image = nso_image(data)
                except PackageError as e:
                    module['inspection_error'] = str(e)
                else:
                    module['sdk_markers'] = sorted({x.decode('ascii') for x in re.findall(
                        rb'SDK MW\+[\x21-\x7e]+', image) if b'NVN' in x or b'nnSdk' in x})
                    module['maxwell_embedded_headers'] = image.count(b'\x78\x56\x34\x12' + bytes(44))
                    module['switch2_detection_import_present'] = b'IsRunningOnOunce' in image
                    module['upgrade_shop_import_present'] = b'ShowShopApplicationInformationForUpgrade' in image
            item['modules'].append(module)
        sdk = next((m for m in item['modules'] if m['name'] == 'sdk'), {})
        markers = sdk.get('sdk_markers', [])
        if any('NintendoSDK_NVN-' in m for m in markers):
            item['graphics_evidence'] = 'NVN SDK; existing Maxwell backend is the initial candidate'
        elif any('NVN2' in m for m in markers):
            item['graphics_evidence'] = 'NVN2 SDK; evaluate the MKW NVN2/SM86 backend'
        else:
            item['graphics_evidence'] = 'Not identified; inspect the executable before choosing a backend'
        item['edition'] = 'Unverified: title ID, SDK and filename alone do not establish upgrade content'
        item['runtime_support'] = 'Not certified; version-specific patches and gameplay need verification'
        report['titles'].append(item)
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('package', type=Path)
    parser.add_argument('--json', type=Path)
    parser.add_argument('--extract-exefs', type=Path)
    parser.add_argument('--extract-romfs', type=Path)
    parser.add_argument('--title-index', type=int)
    parser.add_argument('--verify-all', action='store_true')
    args = parser.parse_args()
    try:
        package = Nup(args.package)
        report = inspect(package)
        if args.verify_all:
            count = 0
            for i in range(len(package.titles)):
                for part in ('exefs', 'romfs'):
                    for entry in package.entries(part, i).values():
                        package.read(entry)
                        count += 1
            report['verified_files'] = count
        if args.extract_exefs or args.extract_romfs:
            if len(package.titles) != 1 and args.title_index is None:
                raise PackageError('A multi-title package requires --title-index for extraction')
            for part, destination in (('exefs', args.extract_exefs), ('romfs', args.extract_romfs)):
                if destination:
                    package.extract(part, destination, args.title_index or 0)
        text = json.dumps(report, indent=2)
        if args.json:
            args.json.parent.mkdir(parents=True, exist_ok=True)
            args.json.write_text(text + '\n', encoding='utf-8')
        print(text)
    except (OSError, ValueError) as e:
        parser.exit(2, f'Package check failed: {e}\n')


if __name__ == '__main__':
    main()
