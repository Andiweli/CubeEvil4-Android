#!/usr/bin/env python3
"""The app's list of shader programs (asset gx-programs.keys; decision of the project owner,
23.10.2026): the keys of the programs devices made, so a first start makes them ahead too
(gx_gl.cpp WarmProgramCache on a thread of its own, GxRenderer::Prewarm).

Sources of keys:
  - error reports (.zip): logs/program-keys.bin and logs/previous/program-keys.bin
    (CoreBridge.writeProgramKeys: every key of the device's program cache)
  - key list files (program-keys.bin, gx-programs.keys): "RE4L", the size of a key, the count, the keys
  - single key files of a program cache (<hash>.key): "RE4K", the size of a key, the key
  - folders: every such file in them (also .zip)

Each key is kept once; its last field (centroid, the MSAA setting) is set to 0: the app makes each
program for the MSAA setting in use. Keys of another size (another renderer version) are left out.

  gx_program_list.py merge OUT.keys SOURCE...   (OUT.keys may be among the sources: it grows)
  gx_program_list.py info FILE...
"""
import os
import struct
import sys
import zipfile

LIST_MAGIC = b"RE4L"
KEY_MAGIC = b"RE4K"


def keys_of_bytes(data, name, size_wanted, out):
    """Keys of a list or key file's bytes into out (a dict key bytes -> True); the key size."""
    if data[:4] == LIST_MAGIC and len(data) >= 12:
        size, count = struct.unpack_from("<II", data, 4)
        if size_wanted and size != size_wanted:
            print(f"  {name}: keys of {size} bytes, not {size_wanted}: left out")
            return size_wanted
        if len(data) < 12 + size * count:
            print(f"  {name}: cut short, left out")
            return size_wanted
        for i in range(count):
            out[normal(data[12 + i * size:12 + (i + 1) * size])] = True
        return size
    if data[:4] == KEY_MAGIC and len(data) >= 8:
        (size,) = struct.unpack_from("<I", data, 4)
        if size_wanted and size != size_wanted:
            return size_wanted
        if len(data) >= 8 + size:
            out[normal(data[8:8 + size])] = True
            return size
    return size_wanted


def normal(key):
    """The key with its MSAA setting (the last 4 bytes, ShaderKey::centroid) at 0."""
    return key[:-4] + b"\0\0\0\0"


def collect(path, size, out):
    if os.path.isdir(path):
        for root, _, files in os.walk(path):
            for f in sorted(files):
                if f.endswith((".key", ".keys", ".bin", ".zip")):
                    size = collect(os.path.join(root, f), size, out)
        return size
    if path.endswith(".zip"):
        with zipfile.ZipFile(path) as z:
            for n in z.namelist():
                if os.path.basename(n) == "program-keys.bin":
                    size = keys_of_bytes(z.read(n), f"{path}:{n}", size, out)
        return size
    with open(path, "rb") as f:
        data = f.read()
    if data[:4] in (LIST_MAGIC, KEY_MAGIC):
        size = keys_of_bytes(data, path, size, out)
    return size


def write(path, keys, size):
    data = bytearray(LIST_MAGIC + struct.pack("<II", size, len(keys)))
    for k in sorted(keys):
        data += k
    tmp = path + ".part"
    with open(tmp, "wb") as f:
        f.write(data)
    os.replace(tmp, path)


def main(argv):
    if len(argv) >= 3 and argv[1] == "merge":
        out_path, sources = argv[2], argv[3:]
        keys, size = {}, 0
        before = 0
        if os.path.exists(out_path):
            size = collect(out_path, size, keys)
            before = len(keys)
        for s in sources:
            if os.path.abspath(s) != os.path.abspath(out_path):
                size = collect(s, size, keys)
        if not size:
            print("no keys found")
            return 1
        write(out_path, keys, size)
        print(f"{out_path}: {len(keys)} programs ({len(keys) - before} new), keys of {size} bytes")
        return 0
    if len(argv) >= 3 and argv[1] == "info":
        for p in argv[2:]:
            keys = {}
            size = collect(p, 0, keys)
            print(f"{p}: {len(keys)} programs, keys of {size} bytes")
        return 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
