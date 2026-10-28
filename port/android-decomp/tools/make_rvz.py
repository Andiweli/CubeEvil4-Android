# makes an RVZ (zstd frames of raw blocks, no packing) of a disc image: a check of the reader on a whole disc
import struct, sys
src, dst = sys.argv[1], sys.argv[2]
chunk = 0x20000
data = open(src, 'rb').read()
size = len(data)
def zraw(b):
    f = bytearray(b'\x28\xb5\x2f\xfd\xa0') + struct.pack('<I', len(b))
    at = 0
    while True:
        n = min(len(b) - at, 128 * 1024)
        last = at + n == len(b)
        h = (1 if last else 0) | (n << 3)
        f += bytes([h & 0xff, (h >> 8) & 0xff, (h >> 16) & 0xff]) + b[at:at + n]
        at += n
        if at >= len(b): break
    return bytes(f)
out = bytearray(0x48 + 0xDC)
groups = []
for g in range(0, size, chunk):
    while len(out) % 4: out.append(0)
    piece = data[g:g + chunk]
    if piece.count(0) == len(piece):
        groups.append((0, 0)); continue
    z = zraw(piece)
    groups.append((len(out), len(z) | 0x80000000))
    out += z
raw = zraw(struct.pack('>QQII', 0x80, size - 0x80, 0, len(groups)))
table = zraw(b''.join(struct.pack('>III', at >> 2, s, 0) for at, s in groups))
rawAt = len(out); out += raw
tabAt = len(out); out += table
struct.pack_into('>4sIII', out, 0, b'RVZ\x01', 0x01000000, 0x00030000, 0xDC)
struct.pack_into('>QQ', out, 0x24, size, len(out))
h2 = 0x48
struct.pack_into('>IIiI', out, h2, 1, 5, 0, chunk)
out[h2 + 0x10:h2 + 0x90] = data[:0x80]
struct.pack_into('>IQI', out, h2 + 0xB4, 1, rawAt, len(raw))
struct.pack_into('>IQI', out, h2 + 0xC4, len(groups), tabAt, len(table))
open(dst, 'wb').write(out)
print(dst, len(out), 'groups', len(groups))
