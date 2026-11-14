"""Validate a compiled Guardian NRO, identity, and the exact prepared assets."""
from pathlib import Path
import argparse
import hashlib
import json
import struct

ROOT = Path(__file__).resolve().parent.parent
TITLE = "NinMagic Guardian"
VERSION = "0.1.3"

def require(value, message):
    if not value:
        raise ValueError(message)

def region(data, offset, size):
    require(0 <= offset <= len(data) and 0 <= size <= len(data) - offset, "Out of bounds resource")
    return data[offset:offset + size]

def text(data):
    require(b"\0" in data, "Unterminated identity")
    return data.split(b"\0", 1)[0].decode("utf-8")

def verify(path, expected_version=VERSION):
    data = Path(path).read_bytes()
    require(0x80 < len(data) <= 16 * 1024 * 1024, "Invalid NRO size")
    require(data[0x10:0x14] == b"NRO0", "Invalid NRO header")
    code_size = struct.unpack_from("<I", data, 0x18)[0]
    require(code_size >= 0x80, "Invalid code size")
    end = 0
    for i in range(3):
        offset, size = struct.unpack_from("<II", data, 0x20 + i * 8)
        require(size and offset >= end and offset + size <= code_size and (i or offset == 0), "Invalid code segments")
        end = offset + size
    header = struct.unpack("<II6Q", region(data, code_size, 56))
    require(header[:2] == (0x54455341, 0), "Invalid ASET header")
    end, assets = 56, []
    for i in range(3):
        offset, size = header[2 + i * 2:4 + i * 2]
        require(size and offset >= end, "Empty or overlapping resources")
        assets.append(region(data, code_size + offset, size))
        end = offset + size
    icon, nacp, romfs = assets
    require(icon == (ROOT / "assets/icon.jpg").read_bytes(), "Wrong app icon")
    require(len(nacp) == 0x4000, "Invalid NACP size")
    for i in range(16):
        title, author = text(nacp[i*0x300:i*0x300+0x200]), text(nacp[i*0x300+0x200:(i+1)*0x300])
        require((title, author) in ((TITLE, "NinMagic"), ("", "")) and (i or title), "Wrong NRO identity")
    require(text(nacp[0x3060:0x3070]) == expected_version, "Wrong NRO version")
    h = struct.unpack("<10Q", region(romfs, 0, 80))
    require(h[0] == 80, "Invalid RomFS header")
    dirs, meta = region(romfs, h[3], h[4]), region(romfs, h[7], h[8])
    visited_dirs, visited_files, files = set(), set(), {}
    required = {"font.bin", "logo.rgba", "FONT-LICENSE.txt", "rescue/ninmagic-guardian-rescue.bin", "rescue/libsys_minerva.bso", "rescue/ninmagic-guardian.ini"}
    def name_at(table, offset, size):
        value = region(table, offset, size).decode("utf-8")
        require(value and value not in {".", ".."} and not any(c in value for c in "/\\:\0"), "Unsafe RomFS resource name")
        return value
    def directory(offset, parent, prefix, depth):
        require(depth <= 4 and offset not in visited_dirs, "Cyclic or deep RomFS directory metadata")
        visited_dirs.add(offset)
        found_parent, sibling, child, file, _, name_size = struct.unpack("<6I", region(dirs, offset, 24))
        require(offset == 0 or found_parent == parent, "Wrong RomFS directory parent")
        if offset == 0:
            require(name_size == 0 and sibling == 0xffffffff, "Invalid RomFS root")
            path = ""
        else:
            path = prefix + name_at(dirs, offset+24, name_size) + "/"
            require(path == "rescue/", "Unexpected runtime asset directory")
        while file != 0xffffffff:
            require(file not in visited_files, "Cyclic RomFS file metadata")
            visited_files.add(file)
            file_parent, next_file, content_offset, size, _, length = struct.unpack("<IIQQII", region(meta, file, 32))
            name = path + name_at(meta, file+32, length)
            require(file_parent == offset and name not in files and name in required, "Unexpected runtime asset")
            content = region(romfs, h[9]+content_offset, size)
            expected = ROOT / "romfs" / name
            require(expected.is_file() and not expected.is_symlink() and content == expected.read_bytes(), "Runtime asset mismatch: " + name)
            files[name] = {"bytes": size, "sha256": hashlib.sha256(content).hexdigest()}
            file = next_file
        while child != 0xffffffff:
            child = directory(child, offset, path, depth+1)
        return sibling
    directory(0, 0, "", 0)
    require(set(files) == required, "Missing runtime assets; build and embed the independent rescue first")
    require(files["logo.rgba"]["bytes"] == 256*256*4, "Wrong logo dimensions")
    return {"title": TITLE, "version": expected_version, "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest(), "romfs": files, "hardware_tested": False, "workflow": "selective SD system backup and restore; same backup readable by Hekate rescue", "nand_access": False}

if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("nro", type=Path)
    p.add_argument("--expected-version", default=VERSION)
    p.add_argument("--output", type=Path)
    args = p.parse_args()
    report = json.dumps(verify(args.nro, args.expected_version), ensure_ascii=False, indent=2) + "\n"
    if args.output:
        args.output.write_text(report, encoding="utf-8")
    print(report)
