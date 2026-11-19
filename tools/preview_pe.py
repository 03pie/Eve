"""Read the actual x64 PE import closure for the portable preview; never load a library."""
from pathlib import Path
import ctypes
from ctypes import wintypes
import os
import re
import struct


def file_version(path):
    """Read the Windows VERSIONINFO resource through the system API, without loading the image."""
    if os.name != "nt":
        raise ValueError("Windows is required to record deployed runtime file versions")
    version = ctypes.WinDLL("version", use_last_error=True)
    version.GetFileVersionInfoSizeW.argtypes = [wintypes.LPCWSTR, ctypes.POINTER(wintypes.DWORD)]
    version.GetFileVersionInfoSizeW.restype = wintypes.DWORD
    version.GetFileVersionInfoW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, wintypes.LPVOID]
    version.GetFileVersionInfoW.restype = wintypes.BOOL
    version.VerQueryValueW.argtypes = [wintypes.LPCVOID, wintypes.LPCWSTR,
                                     ctypes.POINTER(wintypes.LPVOID), ctypes.POINTER(wintypes.UINT)]
    version.VerQueryValueW.restype = wintypes.BOOL
    ignored = wintypes.DWORD()
    size = version.GetFileVersionInfoSizeW(str(path), ctypes.byref(ignored))
    if not size:
        return None
    data = ctypes.create_string_buffer(size)
    read_ok = version.GetFileVersionInfoW(str(path), 0, size, data)
    pointer, length = wintypes.LPVOID(), wintypes.UINT()
    if not read_ok or not version.VerQueryValueW(data, "\\", ctypes.byref(pointer), ctypes.byref(length)) or length.value < 52:
        raise ValueError(f"invalid VERSIONINFO: {path}")
    signature, _, ms, ls = struct.unpack_from("<IIII", ctypes.string_at(pointer.value, 52))
    if signature != 0xfeef04bd:
        raise ValueError(f"invalid VERSIONINFO signature: {path}")
    return ".".join(str(value) for value in (ms >> 16, ms & 0xffff, ls >> 16, ls & 0xffff))


def inspect_pe(path):
    path = Path(path)
    if not 64 <= path.stat().st_size <= 256 * 1024 * 1024:
        raise ValueError(f"PE size outside preview limits: {path}")
    data = path.read_bytes()

    def unpack(fmt, offset):
        if offset < 0 or offset + struct.calcsize(fmt) > len(data):
            raise ValueError(f"truncated PE: {path}")
        return struct.unpack_from(fmt, data, offset)

    if data[:2] != b"MZ":
        raise ValueError(f"not a PE file: {path}")
    pe, = unpack("<I", 0x3c)
    if data[pe:pe + 4] != b"PE\0\0":
        raise ValueError(f"invalid PE signature: {path}")
    machine, sections = unpack("<HH", pe + 4)
    optional_size, = unpack("<H", pe + 20)
    optional = pe + 24
    magic, = unpack("<H", optional)
    if machine != 0x8664 or magic != 0x20b or not 1 <= sections <= 96 or optional_size < 112:
        raise ValueError(f"preview requires an x64 PE32+ image: {path}")
    unpack(f"<{optional_size}s", optional)
    count, = unpack("<I", optional + 108)
    if count > (optional_size - 112) // 8:
        raise ValueError(f"invalid PE directory count: {path}")
    headers_size, = unpack("<I", optional + 60)
    ranges = []
    for index in range(sections):
        section = optional + optional_size + index * 40
        _, rva, size, offset = unpack("<IIII", section + 8)
        if offset + size > len(data):
            raise ValueError(f"truncated PE section: {path}")
        ranges.append((rva, size, offset))

    def address(rva, size):
        matches = [offset + rva - start for start, length, offset in ranges
                   if start <= rva and rva + size <= start + length]
        if rva < headers_size and rva + size <= min(headers_size, len(data)):
            matches.append(rva)
        if len(matches) != 1:
            raise ValueError(f"invalid/ambiguous PE RVA: {path}")
        return matches[0]

    def library_name(rva):
        offset = address(rva, 1)
        end = data.find(b"\0", offset, min(offset + 261, len(data)))
        if end < 0:
            raise ValueError(f"unterminated PE library name: {path}")
        address(rva, end - offset + 1)
        name = data[offset:end].decode("ascii")
        if not re.fullmatch(r"[a-zA-Z0-9_.-]+\.[dD][lL][lL]", name):
            raise ValueError(f"invalid PE library basename: {path}")
        return name.lower()

    def directory(index, delay):
        if index >= count:
            return []
        rva, size = unpack("<II", optional + 112 + index * 8)
        if not rva and not size:
            return []
        width = 32 if delay else 20
        if not rva or size < width or size > width * 4096:
            raise ValueError(f"invalid PE import directory: {path}")
        result = []
        for offset in range(0, size - width + 1, width):
            words = unpack("<" + "I" * (width // 4), address(rva + offset, width))
            if not any(words):
                return sorted(set(result))
            if delay and words[0] != 1:
                raise ValueError(f"unsupported VA-based delay import: {path}")
            result.append(library_name(words[1] if delay else words[3]))
        raise ValueError(f"unterminated PE import directory: {path}")

    return {"machine": "x64", "imports": directory(1, False), "delayImports": directory(13, True)}
