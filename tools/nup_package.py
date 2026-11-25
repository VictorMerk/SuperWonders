"""Read and inspect a local NUP without assuming its filename identifies its edition.

Layout reference: adriadam10/mkw-recomp tools/prepare/nup.py and runtime/sdk/fs/nup.c
at dd9297545beb9d2c90170eea93a01030961bf7cf (MIT, copyright its contributors).
This reader adds bounds, tree, output-path and content-hash checks. No game data ships here.
"""
from dataclasses import dataclass
import hashlib
from pathlib import Path
import struct


class PackageError(ValueError):
    pass


@dataclass(frozen=True)
class Entry:
    path: str
    size: int
    offset: int
    stored_size: int
    codec: int
    digest: bytes


class Nup:
    MAX_TOC = 256 << 20
    MAX_FILE = 512 << 20

    def __init__(self, path):
        self.path = Path(path)
        self.size = self.path.stat().st_size
        with self.path.open('rb') as f:
            h = f.read(128)
            if len(h) != 128 or h[:4] != b'NUP\x01':
                raise PackageError('Not a NUP v1 package')
            self.toc_offset = struct.unpack_from('<I', h, 8)[0]
            f.seek(self.toc_offset)
            h = f.read(48)
            if len(h) != 48 or h[:4] != b'TOC\x01':
                raise PackageError('Missing NUP table of contents')
            nt = struct.unpack_from('<I', h, 4)[0]
            self.nd, self.nf, self.pool, self.pool_size = struct.unpack_from('<QQQQ', h, 8)
            self.dirs = 48 + nt * 64
            self.files = self.dirs + self.nd * 24
            self.children = self.files + self.nf * 80
            total = self.pool + self.pool_size
            if not nt or not self.nd or self.children > self.pool or total > self.MAX_TOC:
                raise PackageError('Invalid or oversized NUP table')
            if self.toc_offset < 128 or self.toc_offset + total > self.size:
                raise PackageError('NUP table extends outside the package')
            f.seek(self.toc_offset)
            self.table = f.read(total)
        self.titles = []
        for i in range(nt):
            tid, root, exefs, romfs, version = struct.unpack_from('<Q4I', self.table, 48 + i * 64)
            if max(root, exefs, romfs) >= self.nd:
                raise PackageError('NUP title has an invalid directory')
            self.titles.append(dict(title_id=f'{tid:016X}', root=root, exefs=exefs, romfs=romfs,
                                    content_version=version))

    def _name(self, at):
        off, n = struct.unpack_from('<II', self.table, at)
        if not n or n > 255 or off + n > self.pool_size:
            raise PackageError('Invalid NUP filename')
        try:
            name = self.table[self.pool + off:self.pool + off + n].decode('utf-8')
        except UnicodeDecodeError as e:
            raise PackageError('NUP filename is not UTF-8') from e
        if name in ('.', '..') or any(c in name for c in '/\\\0:') or name.endswith((' ', '.')):
            raise PackageError('Unsafe NUP filename')
        stem = name.split('.')[0].upper()
        if stem in {'CON', 'PRN', 'AUX', 'NUL', *('COM' + str(i) for i in range(1, 10)),
                    *('LPT' + str(i) for i in range(1, 10))}:
            raise PackageError('Reserved NUP filename')
        return name

    def entries(self, part, title_index=0):
        if part not in ('exefs', 'romfs'):
            raise PackageError('Part must be exefs or romfs')
        if title_index < 0 or title_index >= len(self.titles):
            raise PackageError('Title index outside the package')
        result, seen_dirs, seen_names = {}, set(), set()
        stack = [(self.titles[title_index][part], '', 0)]
        while stack:
            d, prefix, depth = stack.pop()
            if d in seen_dirs or depth > 64:
                raise PackageError('Cyclic or excessively deep NUP tree')
            seen_dirs.add(d)
            _, _, first, count = struct.unpack_from('<4I', self.table, self.dirs + d * 24)
            if self.children + (first + count) * 4 > self.pool:
                raise PackageError('NUP child list outside its table')
            for k in range(first, first + count):
                child = struct.unpack_from('<I', self.table, self.children + k * 4)[0]
                is_dir = child < self.nd
                if child >= self.nd + self.nf:
                    raise PackageError('NUP child index outside its table')
                rec = self.dirs + child * 24 if is_dir else self.files + (child - self.nd) * 80
                name = prefix + self._name(rec)
                if name.casefold() in seen_names:
                    raise PackageError('Duplicate or case-colliding NUP path')
                seen_names.add(name.casefold())
                if is_dir:
                    stack.append((child, name + '/', depth + 1))
                    continue
                size, off, stored = struct.unpack_from('<QQQ', self.table, rec + 8)
                codec = self.table[rec + 64]
                if codec not in (0, 1) or (codec == 0 and size != stored):
                    raise PackageError('Invalid NUP file encoding')
                if off + stored > self.size or off < 128:
                    raise PackageError('NUP file extends outside the package')
                if stored and off < self.toc_offset + len(self.table) and off + stored > self.toc_offset:
                    raise PackageError('NUP file overlaps its table')
                result[name] = Entry(name, size, off, stored, codec, self.table[rec + 32:rec + 64])
        return dict(sorted(result.items()))

    def read(self, entry, max_size=None):
        limit = self.MAX_FILE if max_size is None else max_size
        if max(entry.size, entry.stored_size) > limit:
            raise PackageError(f'{entry.path}: exceeds the read limit')
        with self.path.open('rb') as f:
            f.seek(entry.offset)
            data = f.read(entry.stored_size)
        if len(data) != entry.stored_size:
            raise PackageError(f'{entry.path}: truncated file')
        if entry.codec:
            try:
                import zstandard
            except ImportError as e:
                raise PackageError('NUP decompression needs Python zstandard') from e
            try:
                data = zstandard.ZstdDecompressor(max_window_size=1 << 31).decompress(
                    data, max_output_size=max(1, entry.size), allow_extra_data=False)
            except zstandard.ZstdError as e:
                raise PackageError(f'{entry.path}: invalid zstd data') from e
        if len(data) != entry.size:
            raise PackageError(f'{entry.path}: decoded length differs from the table')
        if any(entry.digest) and hashlib.sha256(data).digest() != entry.digest:
            raise PackageError(f'{entry.path}: SHA-256 differs from the table')
        return data

    def extract(self, part, destination, title_index=0):
        """Extract only into a new directory; never merge two editions or replace an installation."""
        destination = Path(destination)
        if destination.exists() or destination.is_symlink():
            raise PackageError('Extraction requires a new output directory')
        entries = self.entries(part, title_index)
        destination.mkdir(parents=True)
        root = destination.resolve()
        for name, entry in entries.items():
            out = destination / name
            if not out.resolve().is_relative_to(root):
                raise PackageError('Extraction path escapes its destination')
            out.parent.mkdir(parents=True, exist_ok=True)
            with out.open('xb') as f:
                f.write(self.read(entry))
        return len(entries)
