"""Minimal PSARC (v1.4) reader and patch-archive writer for The Last Guardian.

The Last Guardian keeps its scripts and data in PSARC archives. Base archives
(``trico_boot.psarc``) list their file names separated by newlines; patch
archives (``trico_bootp.psarc``) separate them with NUL bytes, keep the table of
contents sorted by MD5, and store blocks uncompressed. Every entry is looked up
by the MD5 of its upper-cased path.

This module never needs game data to be imported: it only reads archives the
user already owns and writes a new archive next to them.
"""
from __future__ import annotations

import hashlib
import io
import os
import struct
import zlib
from dataclasses import dataclass, field

HEADER = struct.Struct(">4sHH4sIIIII")
ENTRY_SIZE = 30
MAGIC = b"PSAR"


def path_md5(name: str) -> bytes:
    """Hash used by the game for lookups: MD5 of the upper-cased relative path."""
    return hashlib.md5(name.upper().encode("utf-8")).digest()


@dataclass
class Entry:
    md5: bytes
    zindex: int
    length: int
    offset: int


@dataclass
class Archive:
    path: str
    version: tuple[int, int]
    compression: bytes
    toc_length: int
    block_size: int
    flags: int
    entries: list[Entry]
    zsizes: list[int]
    zsize_bytes: int
    names: list[str] = field(default_factory=list)
    name_separator: bytes = b"\n"
    _fh: io.BufferedReader | None = None

    # -- reading -----------------------------------------------------------
    @classmethod
    def open(cls, path: str) -> "Archive":
        fh = open(path, "rb")
        try:
            return cls._parse(path, fh)
        except Exception:
            fh.close()
            raise

    @classmethod
    def _parse(cls, path: str, fh) -> "Archive":
        head = fh.read(HEADER.size)
        if len(head) < HEADER.size:
            raise ValueError(f"{path}: not a PSARC archive")
        magic, vmaj, vmin, comp, toc_len, ent_size, n_ent, bsize, flags = HEADER.unpack(head)
        if magic != MAGIC:
            raise ValueError(f"{path}: not a PSARC archive")
        if ent_size != ENTRY_SIZE:
            raise ValueError(f"{path}: unexpected TOC entry size {ent_size}")
        entries = []
        for _ in range(n_ent):
            raw = fh.read(ENTRY_SIZE)
            entries.append(
                Entry(
                    md5=raw[:16],
                    zindex=struct.unpack(">I", raw[16:20])[0],
                    length=int.from_bytes(raw[20:25], "big"),
                    offset=int.from_bytes(raw[25:30], "big"),
                )
            )
        zb = 1
        while (1 << (8 * zb)) < bsize:
            zb += 1
        n_blocks = (toc_len - HEADER.size - n_ent * ENTRY_SIZE) // zb
        table = fh.read(n_blocks * zb)
        zsizes = [int.from_bytes(table[i * zb:(i + 1) * zb], "big") for i in range(n_blocks)]
        arc = cls(path, (vmaj, vmin), comp, toc_len, bsize, flags, entries, zsizes, zb, _fh=fh)
        manifest = arc.read_entry(0)
        sep = b"\0" if manifest.count(b"\0") > manifest.count(b"\n") else b"\n"
        arc.name_separator = sep
        arc.names = [n.decode("utf-8") for n in manifest.split(sep) if n]
        return arc

    def close(self) -> None:
        if self._fh:
            self._fh.close()
            self._fh = None

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()

    def read_entry(self, index: int) -> bytes:
        e = self.entries[index]
        fh = self._fh
        assert fh is not None
        fh.seek(e.offset)
        out = bytearray()
        remaining = e.length
        z = e.zindex
        while remaining > 0:
            want = min(remaining, self.block_size)
            size = self.zsizes[z]
            z += 1
            if size == 0:
                chunk = fh.read(self.block_size)
            else:
                chunk = fh.read(size)
                if size != want:
                    chunk = zlib.decompress(chunk)
            out += chunk[:want]
            remaining -= want
        return bytes(out)

    def index_by_md5(self) -> dict[bytes, int]:
        return {e.md5: i for i, e in enumerate(self.entries) if i != 0}

    def find(self, name: str) -> int | None:
        return self.index_by_md5().get(path_md5(name))

    def read(self, name: str) -> bytes | None:
        i = self.find(name)
        return None if i is None else self.read_entry(i)

    def data_start(self) -> int:
        return min(e.offset for e in self.entries)


def _blocks_for(data: bytes, block_size: int) -> tuple[list[int], bytes]:
    """Store data uncompressed, the way the shipped patch archives do."""
    sizes = []
    for pos in range(0, max(len(data), 1), block_size):
        chunk = data[pos:pos + block_size]
        if not chunk:
            break
        sizes.append(0 if len(chunk) == block_size else len(chunk))
    return sizes, data


def _align(value: int, alignment: int) -> int:
    return (value + alignment - 1) // alignment * alignment


def write_patched(src: Archive, replacements: dict[str, bytes], out_path: str,
                  progress=None) -> dict:
    """Write a copy of ``src`` where each path in ``replacements`` has new contents.

    Paths already in the archive are replaced, new paths are added. Original data
    is copied byte for byte and new data is appended, so the result keeps every
    untouched entry exactly as shipped.
    """
    by_md5 = src.index_by_md5()
    entries = [Entry(e.md5, e.zindex, e.length, e.offset) for e in src.entries]
    zsizes = list(src.zsizes)
    names = list(src.names)
    known = {n.upper() for n in names}

    appended = bytearray()
    orig_end = os.path.getsize(src.path)

    pending: list[tuple[Entry, bytes]] = []
    for name, data in sorted(replacements.items()):
        md5 = path_md5(name)
        sizes, payload = _blocks_for(data, src.block_size)
        entry = Entry(md5, len(zsizes), len(data), 0)
        zsizes.extend(sizes)
        pending.append((entry, payload))
        if md5 in by_md5:
            entries[by_md5[md5]] = entry
        else:
            entries.append(entry)
        if name.upper() not in known:
            names.append(name)
            known.add(name.upper())

    if src.name_separator == b"\0":
        names.sort()
    manifest = src.name_separator.join(n.encode("utf-8") for n in names)
    m_sizes, _ = _blocks_for(manifest, src.block_size)
    manifest_entry = Entry(b"\0" * 16, len(zsizes), len(manifest), 0)
    zsizes.extend(m_sizes)
    entries[0] = manifest_entry
    pending.append((manifest_entry, manifest))

    if max(zsizes) >= (1 << (8 * src.zsize_bytes)):
        raise ValueError("block size table overflow")

    head, rest = entries[0], sorted(entries[1:], key=lambda e: e.md5)
    entries = [head] + rest

    toc_len = HEADER.size + len(entries) * ENTRY_SIZE + len(zsizes) * src.zsize_bytes
    old_start = src.data_start()
    shift = 0 if toc_len <= old_start else _align(toc_len, 0x10000) - old_start
    new_data_base = orig_end + shift
    for entry, payload in pending:
        entry.offset = new_data_base + len(appended)
        appended += payload
    pending_ids = {id(e) for e, _ in pending}
    for e in entries:
        if id(e) not in pending_ids:
            e.offset += shift

    tmp = out_path + ".partial"
    with open(tmp, "wb") as out:
        out.write(HEADER.pack(MAGIC, src.version[0], src.version[1], src.compression,
                              toc_len, ENTRY_SIZE, len(entries), src.block_size, src.flags))
        for e in entries:
            out.write(e.md5 + struct.pack(">I", e.zindex) + e.length.to_bytes(5, "big")
                      + e.offset.to_bytes(5, "big"))
        for s in zsizes:
            out.write(s.to_bytes(src.zsize_bytes, "big"))
        out.write(b"\0" * (old_start + shift - out.tell()))
        fh = src._fh
        assert fh is not None
        fh.seek(old_start)
        copied = 0
        total = orig_end - old_start
        while True:
            buf = fh.read(8 << 20)
            if not buf:
                break
            out.write(buf)
            copied += len(buf)
            if progress:
                progress(copied, total)
        out.write(appended)
    os.replace(tmp, out_path)
    return {"entries": len(entries), "appended_bytes": len(appended), "shift": shift}


def _compressed_blocks_for(data: bytes, block_size: int) -> tuple[list[int], bytes]:
    """zlib blocks where that helps, the way the base game archives are stored."""
    sizes, out = [], bytearray()
    for pos in range(0, len(data), block_size):
        chunk = data[pos:pos + block_size]
        packed = zlib.compress(chunk, 9)
        if len(packed) < len(chunk):
            sizes.append(len(packed))
            out += packed
        else:
            sizes.append(0 if len(chunk) == block_size else len(chunk))
            out += chunk
    return sizes, bytes(out)


def create(out_path: str, files: dict[str, bytes], patch_style: bool = True,
           block_size: int = 0x10000, compress: bool = False) -> None:
    """Create a small archive from scratch. Used by the tests to build fixtures."""
    sep = b"\0" if patch_style else b"\n"
    names = sorted(files) if patch_style else list(files)
    manifest = sep.join(n.encode() for n in names)
    zb = 1
    while (1 << (8 * zb)) < block_size:
        zb += 1
    zsizes: list[int] = []
    blobs: list[tuple[Entry, bytes]] = []
    for name, data in [("", manifest)] + [(n, files[n]) for n in names]:
        pack = _compressed_blocks_for if compress else _blocks_for
        sizes, payload = pack(data, block_size)
        e = Entry(b"\0" * 16 if name == "" else path_md5(name), len(zsizes), len(data), 0)
        zsizes.extend(sizes)
        blobs.append((e, payload))
    entries = [blobs[0][0]] + sorted((b[0] for b in blobs[1:]), key=lambda e: e.md5)
    toc_len = HEADER.size + len(entries) * ENTRY_SIZE + len(zsizes) * zb
    pos = _align(toc_len, 16)
    for e, payload in blobs:
        e.offset = pos
        pos += len(payload)
    with open(out_path, "wb") as out:
        out.write(HEADER.pack(MAGIC, 1, 4, b"zlib", toc_len, ENTRY_SIZE, len(entries),
                              block_size, 13 if patch_style else 1))
        for e in entries:
            out.write(e.md5 + struct.pack(">I", e.zindex) + e.length.to_bytes(5, "big")
                      + e.offset.to_bytes(5, "big"))
        for s in zsizes:
            out.write(s.to_bytes(zb, "big"))
        out.write(b"\0" * (_align(toc_len, 16) - toc_len))
        for _, payload in blobs:
            out.write(payload)


def copy_progress_printer(label: str):
    last = [-1]

    def cb(done: int, total: int) -> None:
        pct = int(done * 100 / total) if total else 100
        if pct // 10 != last[0]:
            last[0] = pct // 10
            print(f"{label}: {pct}%", flush=True)

    return cb


__all__ = ["Archive", "Entry", "create", "write_patched", "path_md5", "copy_progress_printer"]
