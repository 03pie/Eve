"""只读解析 PE 图标资源，逐帧核对正式 Eve ICO；不加载或执行 EXE。"""
import argparse
import json
from pathlib import Path
import struct


def unpack(data, offset, layout):
    """所有目录和数据读取都检查文件边界。"""
    size = struct.calcsize(layout)
    if offset < 0 or offset + size > len(data):
        raise ValueError("resource exceeds file bounds")
    return struct.unpack_from(layout, data, offset)


def expected_icons(path):
    """ICO 的 PNG 数据原样比较，不能用近似缩略图代替。"""
    data = path.read_bytes()
    reserved, kind, count = unpack(data, 0, "<HHH")
    if (reserved, kind, count) != (0, 1, 7):
        raise ValueError("expected seven-frame Eve ICO")
    result = {}
    for index in range(count):
        width, height, _, _, planes, bits, size, offset = unpack(data, 6 + index * 16, "<BBBBHHII")
        dimension = width or 256
        if dimension != (height or 256) or (planes, bits) != (1, 32) or offset + size > len(data):
            raise ValueError("invalid Eve ICO entry")
        result[dimension] = data[offset:offset + size]
    if sorted(result) != [16, 24, 32, 48, 64, 128, 256]:
        raise ValueError("unexpected Eve icon dimensions")
    return result


class Resources:
    """将 Windows x64 PE 的 RVA 与受限资源目录映射到文件字节。"""
    def __init__(self, data):
        self.data = data
        if data[:2] != b"MZ":
            raise ValueError("not a PE executable")
        pe, = unpack(data, 60, "<I")
        if data[pe:pe + 4] != b"PE\0\0":
            raise ValueError("invalid PE signature")
        sections, = unpack(data, pe + 6, "<H")
        optional_size, = unpack(data, pe + 20, "<H")
        magic, = unpack(data, pe + 24, "<H")
        if magic != 0x20b or optional_size < 136:
            raise ValueError("expected PE32+ resource directories")
        self.sections = [unpack(data, pe + 24 + optional_size + i * 40 + 8, "<IIII") for i in range(sections)]
        rva, self.size = unpack(data, pe + 24 + 112 + 16, "<II")
        if not rva or not self.size:
            raise ValueError("no resource directory")
        self.base = self.file_offset(rva)

    def file_offset(self, rva):
        for virtual_size, virtual, raw_size, raw in self.sections:
            if virtual <= rva < virtual + max(virtual_size, raw_size):
                offset = rva - virtual
                if offset >= raw_size or raw + offset >= len(self.data):
                    break
                return raw + offset
        raise ValueError("unmapped resource RVA")

    def entries(self, relative):
        if relative < 0 or relative + 16 > self.size:
            raise ValueError("invalid resource directory offset")
        named, numbered = unpack(self.data, self.base + relative + 12, "<HH")
        if relative + 16 + (named + numbered) * 8 > self.size:
            raise ValueError("invalid resource directory size")
        for index in range(named + numbered):
            identity, child = unpack(self.data, self.base + relative + 16 + index * 8, "<II")
            if not identity & 0x80000000:
                yield identity, bool(child & 0x80000000), child & 0x7fffffff

    def typed(self, kind):
        result = {}
        for identity, directory, types in self.entries(0):
            if identity != kind or not directory:
                continue
            for name, named_directory, languages in self.entries(types):
                if not named_directory:
                    raise ValueError("expected resource name directory")
                for language, is_directory, leaf in self.entries(languages):
                    if is_directory or leaf + 16 > self.size:
                        raise ValueError("expected resource data entry")
                    rva, length, _, _ = unpack(self.data, self.base + leaf, "<IIII")
                    offset = self.file_offset(rva)
                    if offset + length > len(self.data):
                        raise ValueError("resource data exceeds file")
                    result[name, language] = self.data[offset:offset + length]
        return result


def check(path, expected):
    resources = Resources(path.read_bytes())
    frames, groups = resources.typed(3), resources.typed(14)
    for (group, language), data in groups.items():
        reserved, kind, count = unpack(data, 0, "<HHH")
        if (reserved, kind, count) != (0, 1, len(expected)):
            continue
        found = {}
        for index in range(count):
            width, height, _, _, planes, bits, size, identity = unpack(data, 6 + index * 14, "<BBBBHHIH")
            image = frames.get((identity, language))
            dimension = width or 256
            if (height or 256) != dimension or (planes, bits) != (1, 32) or image is None or len(image) != size:
                break
            found[dimension] = image
        else:
            if found == expected:
                return {"executable": str(path.resolve()), "group": group, "language": language,
                        "dimensions": sorted(found), "matchesOriginalBytes": True}
    raise ValueError("no complete icon group matches the seven original Eve frames")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executables", type=Path, nargs="+")
    parser.add_argument("--icon", type=Path, default=Path(__file__).resolve().parents[1] / "app/shell/resources/eve.ico")
    args = parser.parse_args()
    expected = expected_icons(args.icon)
    results, errors = [], []
    for executable in args.executables:
        try:
            results.append(check(executable, expected))
        except (ValueError, OSError, struct.error) as failure:
            errors.append({"executable": str(executable), "error": str(failure)})
    print(json.dumps({"passed": results, "failed": errors}, indent=2))
    return 1 if errors else 0


if __name__ == "__main__":
    raise SystemExit(main())
