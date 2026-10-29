"""将正式设计中已有的透明 PNG 原样封装为 ICO，不重采样或重新绘制。"""
from pathlib import Path
import struct


def main():
    """固定读取正式资源目录，验证尺寸/透明通道后写入七尺寸 Windows 图标。"""
    root = Path(__file__).resolve().parents[1] / "app" / "shell" / "resources"
    sizes = (16, 24, 32, 48, 64, 128, 256)
    images = []
    for size in sizes:
        data = (root / f"eve-{size}.png").read_bytes()
        if len(data) < 33 or data[:8] != b"\x89PNG\r\n\x1a\n" or data[12:16] != b"IHDR":
            raise ValueError(f"invalid PNG: {size}")
        width, height = struct.unpack_from(">II", data, 16)
        if (width, height, data[24], data[25]) != (size, size, 8, 6):
            raise ValueError(f"expected {size}px RGBA PNG")
        images.append(data)
    offset = 6 + 16 * len(images)
    entries = []
    for size, data in zip(sizes, images):
        dimension = size if size < 256 else 0
        entries.append(struct.pack("<BBBBHHII", dimension, dimension, 0, 0, 1, 32, len(data), offset))
        offset += len(data)
    output = root / "eve.ico"
    output.write_bytes(struct.pack("<HHH", 0, 1, len(images)) + b"".join(entries) + b"".join(images))
    print(f"Created {output}: {len(images)} original PNG frames")


if __name__ == "__main__":
    main()
