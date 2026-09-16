"""Read a WOTC cooked package without the editor: decompress it, list its exports, follow Kismet links.

Why this exists: a mission's rules live in its Kismet, and the Kismet lives only in the cooked
`Obj_*.upk` — there is no SrcOrig for it. The Supply Extraction crate bug (docs/surfaces/tactical.md,
object scanner) was settled by reading which Kismet ops link into `SeqAct_RemoveInteractiveObjectFromLevel`.

    python tools/upk_inspect.py decompress <pkg.upk> <out.bin>
    python tools/upk_inspect.py names      <pkg.upk> <out.bin> [regex]
    python tools/upk_inspect.py exports    <pkg.upk> <out.bin> [class-or-name substring]
    python tools/upk_inspect.py refs       <pkg.upk> <out.bin> <export#> [...]   # who points AT it
    python tools/upk_inspect.py refs       <pkg.upk> <out.bin> -<export#>        # what IT points at

Format facts (measured on Obj_SupplyExtraction.upk, 2026-09-16):
- COMPRESSION_FLAGS 0x08 is Oodle; the decoder is the game's own oo2core_4_win64.dll
  (OodleLZ_Decompress, 14 args, fuzzSafe=1, threadPhase=3), per UE3 block.
- Chunks are found by scanning for the package tag 0x9E2A83C1 past the summary: each chunk header is
  tag, BlockSize (0x200000 here), CompressedSize, UncompressedSize, then (comp, raw) per block.
- The summary's offsets were computed BEFORE the chunk table was appended to it, so the uncompressed
  image is raw[:NameOffset] + decompressed chunks, not raw[:first chunk] + chunks.
- A cooked map flattens every Kismet op into Main_Sequence, so an export's outer says nothing
  about which sub-sequence owns it; `refs` (int32 package-index matches inside payloads) is how
  the graph is recovered. Matches are heuristic — a StaticMesh payload can hold the same int by
  chance — so read the class names, not the count.
"""
import ctypes
import os
import re
import struct
import sys

TAG = 0x9E2A83C1
DEFAULT_OODLE = r"D:\SteamLibrary\steamapps\common\XCOM 2\XCom2-WarOfTheChosen\Binaries\Win64\oo2core_4_win64.dll"


# ---- decompress -------------------------------------------------------------------------------

def _oodle():
    path = os.environ.get("X2_OODLE_DLL", DEFAULT_OODLE)
    dll = ctypes.WinDLL(path)
    fn = dll.OodleLZ_Decompress
    fn.restype = ctypes.c_ssize_t
    fn.argtypes = [ctypes.c_void_p, ctypes.c_ssize_t, ctypes.c_void_p, ctypes.c_ssize_t,
                   ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_void_p, ctypes.c_ssize_t,
                   ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_ssize_t, ctypes.c_int]
    return fn


def decompress(src, dst):
    dec = _oodle()
    data = open(src, "rb").read()
    tag = struct.pack("<I", TAG)
    pos = 4
    out_all = bytearray()
    chunks = 0
    while True:
        i = data.find(tag, pos)
        if i < 0:
            break
        block_size, comp_size, raw_size = struct.unpack_from("<iii", data, i + 4)
        if block_size <= 0 or comp_size <= 0 or raw_size <= 0 or block_size > 0x400000:
            pos = i + 4
            continue
        nblocks = (raw_size + block_size - 1) // block_size
        p = i + 16
        blocks = []
        ok = True
        for _ in range(nblocks):
            if p + 8 > len(data):
                ok = False
                break
            c, r = struct.unpack_from("<ii", data, p)
            if c <= 0 or r <= 0 or r > block_size:
                ok = False
                break
            blocks.append((c, r))
            p += 8
        if not ok:
            pos = i + 4
            continue
        for c, r in blocks:
            buf = ctypes.create_string_buffer(r)
            n = dec(data[p:p + c], c, buf, r, 1, 0, 0, None, 0, None, None, None, 0, 3)
            if n != r:
                raise RuntimeError(f"oodle returned {n}, wanted {r} (chunk at {i})")
            out_all += buf.raw
            p += c
        chunks += 1
        pos = p
    open(dst, "wb").write(out_all)
    print(f"{chunks} chunks, {len(out_all)} bytes -> {dst}")


# ---- tables -----------------------------------------------------------------------------------

def fstring(buf, pos):
    (n,) = struct.unpack_from("<i", buf, pos)
    pos += 4
    if n < 0:
        s = buf[pos:pos + (-n) * 2].decode("utf-16-le", "replace").rstrip("\0")
        pos += (-n) * 2
    else:
        s = buf[pos:pos + n].decode("latin1").rstrip("\0")
        pos += n
    return s, pos


class Package:
    def __init__(self, upk, binpath):
        raw = open(upk, "rb").read()
        body = open(binpath, "rb").read()
        pos = 12
        _, pos = fstring(raw, pos)  # folder name
        f = struct.unpack_from("<12i", raw, pos)
        name_count, name_off, exp_count, exp_off, imp_count, imp_off = f[1:7]
        img = raw[:name_off] + body
        self.img = img

        names = []
        p = name_off
        for _ in range(name_count):
            s, p = fstring(img, p)
            p += 8
            names.append(s)
        self.names = names

        def nm(p):
            idx, num = struct.unpack_from("<ii", img, p)
            s = names[idx] if 0 <= idx < len(names) else f"<bad {idx}>"
            return (s + (f"_{num - 1}" if num else "")), p + 8

        imports = []
        p = imp_off
        for _ in range(imp_count):
            _, p = nm(p)
            cls, p = nm(p)
            (outer,) = struct.unpack_from("<i", img, p); p += 4
            obj, p = nm(p)
            imports.append((cls, outer, obj))
        self.imports = imports

        exports = []
        p = exp_off
        for _ in range(exp_count):
            cls_i, sup_i, outer = struct.unpack_from("<iii", img, p); p += 12
            obj, p = nm(p)
            p += 4 + 8  # archetype, object flags
            ssize, soff, eflags, netcount = struct.unpack_from("<iiii", img, p); p += 16
            p += netcount * 4 + 16 + 4  # net objects, guid, package flags
            exports.append((cls_i, outer, obj, ssize, soff))
        self.exports = exports

    def objname(self, i):
        if i > 0:
            return self.exports[i - 1][2]
        if i < 0:
            return self.imports[-i - 1][2]
        return "Class"

    def path(self, i):
        parts = []
        guard = 0
        while i and guard < 64:
            guard += 1
            if i > 0:
                e = self.exports[i - 1]
                parts.append(e[2]); i = e[1]
            else:
                im = self.imports[-i - 1]
                parts.append(im[2]); i = im[1]
        return ".".join(reversed(parts))

    def payload(self, k):
        _, _, _, sz, off = self.exports[k - 1]
        return self.img[off:off + sz]


def strings(payload):
    return [m.group().decode() for m in re.finditer(rb"[\x20-\x7e]{5,}", payload)]


def cmd_names(pkg, pattern=None):
    rx = re.compile(pattern) if pattern else None
    for s in pkg.names:
        if rx is None or rx.search(s):
            print(s)


def cmd_exports(pkg, flt=None):
    for k, (cls_i, outer, obj, ssize, soff) in enumerate(pkg.exports, 1):
        cls = pkg.objname(cls_i)
        if flt and flt.lower() not in cls.lower() and flt.lower() not in obj.lower():
            continue
        print(f"[{k}] {cls}  {pkg.path(k)}  size={ssize} off={soff}")


def cmd_refs(pkg, *targets):
    for t in (int(x) for x in targets):
        if t < 0:
            t = -t
            cls_i, _, obj, _, _ = pkg.exports[t - 1]
            payload = pkg.payload(t)
            print(f"### from [{t}] {pkg.objname(cls_i)} {obj}: {strings(payload)}")
            seen = set()
            for i in range(0, len(payload) - 3):
                (v,) = struct.unpack_from("<i", payload, i)
                if 0 < v <= len(pkg.exports) and v != t and v not in seen:
                    c, _, name, _, _ = pkg.exports[v - 1]
                    cn = pkg.objname(c)
                    if cn.startswith("Seq") or cn.startswith("Interp"):
                        seen.add(v)
                        print(f"  -> [{v}] {cn} {name}: {strings(pkg.payload(v))[:6]}")
            continue
        cls_i, _, obj, _, _ = pkg.exports[t - 1]
        print(f"### target [{t}] {pkg.objname(cls_i)} {obj}")
        print("    own strings:", strings(pkg.payload(t)))
        needle = struct.pack("<i", t)
        for k, (c, _, name, _, _) in enumerate(pkg.exports, 1):
            if k == t:
                continue
            payload = pkg.payload(k)
            if needle in payload:
                cn = pkg.objname(c)
                if cn.startswith("Seq") or cn.startswith("Interp"):
                    print(f"  <- [{k}] {cn} {name}: {strings(payload)[:8]}")


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    verb, upk, binpath = argv[0], argv[1], argv[2]
    if verb == "decompress":
        decompress(upk, binpath)
        return 0
    pkg = Package(upk, binpath)
    if verb == "names":
        cmd_names(pkg, *argv[3:4])
    elif verb == "exports":
        cmd_exports(pkg, *argv[3:4])
    elif verb == "refs":
        cmd_refs(pkg, *argv[3:])
    else:
        print(__doc__)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
