"""Port this mod's pinned RVAs from the build they were measured on to another build of the game.

There is more than one retail XCom2.exe -- the Steam one everything here was measured against, and
the GOG one, which is the same source RELINKED, so every address we hard-code moved. This derives
the new ones from the old, statically, from the two files.

    python tools/derive_offsets.py --reference "<...>/XCom2.exe" --target "<...>/XCom2.exe"

Method, and why each answer can be trusted:

  * A GLOBAL is reached by RIP-relative operands, so the code AROUND a reference is its signature:
    wildcard the 4-byte displacement, find that shape in the target image, read ITS displacement.
    Hundreds of sites are tried and they vote; a right answer is unanimous.
  * A FUNCTION is matched on its opening instructions, with capstone masking every byte that
    encodes an address (RIP displacements, large immediates, relative call and jump targets).
  * A VTABLE that no code reference reaches is ported through its CONTENTS: match the functions it
    points at, find where the target image stores pointers to them, and vote on where slot 0 is.

Every anchor is then confirmed by something that shares none of those assumptions: the number of
code sites referencing a global must be the SAME in both images; two adjacent globals must keep
their distance; ProcessEvent must be stored in the same number of vtables, at the same slot index;
and the two builds' copies of a function must agree instruction for instruction, including every
structure displacement -- which is what says the object layouts the mod transcribes still hold.

⛔ Run every new check against the REFERENCE build as a control. A check that scores badly there is
a broken check, not a broken build: the RTTI-locator test that was written first scored 0% on the
build the mod demonstrably works on.

Needs capstone (`pip install capstone`). Takes a few minutes: the reference scans are linear over
~20 MB of .text, once per anchor.
"""
import argparse
import struct
import sys
from collections import Counter

try:
    import capstone
except ImportError:
    sys.exit("capstone is required: pip install capstone")

# What the mod pins, as measured on the Steam build. Keep in step with:
#   GNames/GObjects   deps/CodeRed-Generator/Engine/XCOM2/Configuration.cpp (-> GameDefines.hpp)
#   ProcessEvent      src/process_event_hook.cpp
#   the movie player  src/movie_subtitles.cpp (global, vtable, and the subtitle draw it hooks)
REFERENCE_ANCHORS = {
    "GNames": 0x1DAF450,
    "GObjects": 0x1DAF4A8,
    "MoviePlayerGlobal": 0x1DB8900,
}
REFERENCE_VTABLES = {"MoviePlayerVtable": 0x13E9B28}
REFERENCE_FUNCTIONS = {"ProcessEvent": 0x143B50, "MovieSubtitleDraw": 0x2432D0}
PROCESS_EVENT_SLOT = 72


class PE:
    """Just enough PE to map RVAs to file offsets and name the section an address lands in."""

    def __init__(self, path, tag):
        self.tag = tag
        self.path = path
        self.data = open(path, "rb").read()
        pe = struct.unpack_from("<I", self.data, 0x3C)[0]
        nsec = struct.unpack_from("<H", self.data, pe + 6)[0]
        optsize = struct.unpack_from("<H", self.data, pe + 20)[0]
        self.timestamp = struct.unpack_from("<I", self.data, pe + 8)[0]
        self.imagebase = struct.unpack_from("<Q", self.data, pe + 0x18 + 0x18)[0]
        self.size_of_image = struct.unpack_from("<I", self.data, pe + 0x18 + 0x38)[0]
        self.sections = []
        for i in range(nsec):
            o = pe + 24 + optsize + i * 40
            name = self.data[o : o + 8].rstrip(b"\0").decode("latin1")
            vsize, va, rsize, roff = struct.unpack_from("<IIII", self.data, o + 8)
            self.sections.append((name, va, vsize, roff, rsize))

    def fingerprint(self):
        return (f"{self.tag}: {len(self.data)} bytes, PE timestamp 0x{self.timestamp:08X}, "
                f"SizeOfImage 0x{self.size_of_image:X}, imagebase 0x{self.imagebase:X}")

    def section_of(self, rva):
        for s in self.sections:
            if s[1] <= rva < s[1] + max(s[2], s[4]):
                return s
        return None

    def named(self, name):
        for s in self.sections:
            if s[0] == name:
                return s
        raise RuntimeError(f"{self.tag}: no {name} section")

    def rva2off(self, rva):
        s = self.section_of(rva)
        if not s or rva - s[1] >= s[4]:
            return None
        return s[3] + (rva - s[1])

    def off2rva(self, off):
        for _name, va, _vsize, roff, rsize in self.sections:
            if roff <= off < roff + rsize:
                return va + (off - roff)
        return None

    def get(self, rva, n):
        o = self.rva2off(rva)
        return None if o is None else self.data[o : o + n]

    def body(self, name):
        _n, va, vsize, roff, rsize = self.named(name)
        return self.data[roff : roff + min(vsize, rsize)], va

    def count_pointers_to(self, rva):
        needle = struct.pack("<Q", self.imagebase + rva)
        count, start = 0, 0
        while True:
            i = self.data.find(needle, start)
            if i < 0:
                return count
            count, start = count + 1, i + 1


def rip_refs(pe, target_rva, limit=400):
    """RVAs of displacement fields in .text whose RIP-relative target is target_rva.

    Assumes the displacement is the instruction's last field, which holds for the load and lea
    forms that reach a global. A false positive costs nothing: every hit is verified by matching.
    """
    data, base = pe.body(".text")
    out = []
    for i in range(len(data) - 4):
        need = target_rva - (base + i + 4)
        if -0x80000000 <= need <= 0x7FFFFFFF and data[i : i + 4] == struct.pack("<i", need):
            out.append(base + i)
            if len(out) >= limit:
                break
    return out


def find_masked(hay, pat, mask, limit=8):
    """Offsets where pat matches hay, comparing only the bytes mask marks True."""
    best_len = best_at = run = at = 0
    for i, m in enumerate(mask + [False]):
        if m:
            if run == 0:
                at = i
            run += 1
        else:
            if run > best_len:
                best_len, best_at = run, at
            run = 0
    anchor = pat[best_at : best_at + best_len]
    hits, start = [], 0
    while True:
        i = hay.find(anchor, start)
        if i < 0:
            return hits
        s = i - best_at
        if 0 <= s and s + len(pat) <= len(hay):
            if all(not m or hay[s + k] == pat[k] for k, m in enumerate(mask)):
                hits.append(s)
                if len(hits) >= limit:
                    return hits
        start = i + 1


def signature(pe, rva, insn_count=16, max_bytes=64):
    """A function's opening bytes, with everything that encodes an address wildcarded."""
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    md.detail = True
    pat, mask = bytearray(), []
    for n, insn in enumerate(md.disasm(pe.get(rva, 256), pe.imagebase + rva)):
        if n >= insn_count or len(pat) >= max_bytes:
            break
        b = insn.bytes
        m = [True] * len(b)
        for op in insn.operands:
            if (op.type == capstone.x86.X86_OP_MEM
                    and op.mem.base == capstone.x86.X86_REG_RIP and insn.disp_offset):
                for k in range(insn.disp_offset, insn.disp_offset + insn.disp_size):
                    m[k] = False
            if (op.type == capstone.x86.X86_OP_IMM and insn.imm_size == 4
                    and abs(op.imm) > 0x10000 and insn.imm_offset):
                for k in range(insn.imm_offset, insn.imm_offset + insn.imm_size):
                    m[k] = False
        if (insn.group(capstone.CS_GRP_CALL) or insn.group(capstone.CS_GRP_JUMP)) and len(b) >= 5:
            for k in range(len(b) - 4, len(b)):
                m[k] = False
        pat += b
        mask += m
    return bytes(pat), mask


def port_global(ref, tgt, rva, sample=400):
    sites = rip_refs(ref, rva, limit=sample)
    rdata, rbase = ref.body(".text")
    tdata, tbase = tgt.body(".text")
    votes, matched = Counter(), 0
    for site in sites:
        off = site - rbase
        for ctx in (28, 22, 16, 12, 8):
            if off - ctx < 0:
                continue
            hits = find_masked(tdata, rdata[off - ctx : off + 4], [True] * ctx + [False] * 4)
            if len(hits) == 1:
                disp = struct.unpack_from("<i", tdata, hits[0] + ctx)[0]
                votes[tbase + hits[0] + ctx + 4 + disp] += 1
                matched += 1
                break
    return len(sites), matched, votes


def port_function(ref, tgt, rva):
    pat, mask = signature(ref, rva)
    if sum(mask) < 16:
        return None, 0
    tdata, tbase = tgt.body(".text")
    hits = find_masked(tdata, pat, mask, limit=4)
    return (tbase + hits[0] if len(hits) == 1 else None), len(hits)


def port_vtable(ref, tgt, vt_rva, entries=20):
    votes, ported = Counter(), 0
    for i in range(entries):
        ptr = ref.get(vt_rva + i * 8, 8)
        if not ptr:
            break
        fn = struct.unpack("<Q", ptr)[0] - ref.imagebase
        sec = ref.section_of(fn)
        if not sec or sec[0] != ".text":
            continue
        gfn, _ = port_function(ref, tgt, fn)
        if gfn is None:
            continue
        ported += 1
        needle = struct.pack("<Q", tgt.imagebase + gfn)
        start = 0
        while True:
            at = tgt.data.find(needle, start)
            if at < 0:
                break
            start = at + 1
            rva = tgt.off2rva(at)
            if rva is not None:
                votes[rva - i * 8] += 1
    return ported, votes


def slot_histogram(pe, fn_rva):
    """Which vtable index holds this function, counted over every vtable that holds it."""
    _n, va, vsize, roff, rsize = pe.named(".rdata")
    rd = pe.data[roff : roff + min(vsize, rsize)]
    lo = va
    text = pe.named(".text")
    t_lo, t_hi = text[1], text[1] + max(text[2], text[4])

    def is_code_at(rva):
        i = rva - lo
        if i < 0 or i + 8 > len(rd):
            return False
        v = struct.unpack_from("<Q", rd, i)[0] - pe.imagebase
        return t_lo <= v < t_hi

    idx, at = Counter(), 0
    needle = struct.pack("<Q", pe.imagebase + fn_rva)
    while True:
        at = rd.find(needle, at)
        if at < 0:
            break
        rva = lo + at
        at += 1
        if rva % 8:
            continue
        start = rva
        while start - 8 >= lo and is_code_at(start - 8):
            start -= 8
        idx[(rva - start) // 8] += 1
    return idx


def diff_function(ref, tgt, r_rva, t_rva, limit=600):
    """Instruction-for-instruction comparison, with structure displacements compared explicitly."""
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    md.detail = True
    a = list(md.disasm(ref.get(r_rva, 4096), ref.imagebase + r_rva))
    b = list(md.disasm(tgt.get(t_rva, 4096), tgt.imagebase + t_rva))
    lockstep = mismatch = 0
    first = None

    def disps(insn):
        return [op.mem.disp for op in insn.operands
                if op.type == capstone.x86.X86_OP_MEM
                and op.mem.base not in (0, capstone.x86.X86_REG_RIP)]

    for i in range(min(len(a), len(b), limit)):
        if a[i].mnemonic != b[i].mnemonic:
            first = first or f"instruction {i}: {a[i].mnemonic} vs {b[i].mnemonic}"
            break
        lockstep += 1
        if disps(a[i]) != disps(b[i]):
            mismatch += 1
            first = first or (f"instruction {i}: {a[i].mnemonic} {a[i].op_str}"
                              f" vs {b[i].mnemonic} {b[i].op_str}")
        if a[i].mnemonic == "ret" and i > 4:
            break
    return lockstep, mismatch, first


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--reference", required=True, help="the build whose RVAs are already known")
    ap.add_argument("--target", required=True, help="the build to derive RVAs for")
    ap.add_argument("--sample", type=int, default=400,
                    help="how many reference sites to try per global (default 400)")
    args = ap.parse_args()

    ref, tgt = PE(args.reference, "reference"), PE(args.target, "target")
    print(ref.fingerprint())
    print(tgt.fingerprint())
    if (ref.timestamp, ref.size_of_image) == (tgt.timestamp, tgt.size_of_image):
        print("\nThese are the same build. Nothing to derive.")
        return 0
    print()

    derived = {}
    for name, rva in REFERENCE_ANCHORS.items():
        seen, matched, votes = port_global(ref, tgt, rva, args.sample)
        print(f"== {name} (reference 0x{rva:X}) ==")
        print(f"   reference sites sampled: {seen}, matched in target: {matched}")
        for target_rva, n in votes.most_common(3):
            sec = tgt.section_of(target_rva)
            print(f"   -> 0x{target_rva:X}  ({n} votes, {sec[0] if sec else '??'})")
        if len(votes) > 1:
            print("   ⚠ NOT unanimous -- do not use this without settling the disagreement")
        if votes:
            derived[name] = votes.most_common(1)[0][0]
        print()

    for name, rva in REFERENCE_FUNCTIONS.items():
        got, hits = port_function(ref, tgt, rva)
        print(f"== {name} (reference 0x{rva:X}) ==")
        print(f"   signature matches in target: {hits}")
        if got:
            derived[name] = got
            print(f"   -> 0x{got:X}")
        print()

    for name, rva in REFERENCE_VTABLES.items():
        ported, votes = port_vtable(ref, tgt, rva)
        print(f"== {name} (reference 0x{rva:X}), ported by contents ==")
        print(f"   entries whose function matched: {ported}")
        for target_rva, n in votes.most_common(3):
            sec = tgt.section_of(target_rva)
            print(f"   -> 0x{target_rva:X}  ({n} votes, {sec[0] if sec else '??'})")
        if votes:
            derived[name] = votes.most_common(1)[0][0]
        print()

    print("== confirmations (each independent of how the address was found) ==")
    for name, rva in REFERENCE_ANCHORS.items():
        if name not in derived:
            continue
        cr = len(rip_refs(ref, rva, limit=5000))
        ct = len(rip_refs(tgt, derived[name], limit=5000))
        # rip_refs deliberately over-approximates -- it counts any 4-byte window that WOULD reach
        # the address, so a handful of its hits are not instructions at all. A count that differs
        # by a site or two is that noise; a wrong address lands in unrelated data and the counts
        # are not in the same league.
        close = abs(cr - ct) <= max(2, int(0.01 * max(cr, ct)))
        print(f"   {name:18} referenced from {cr} sites in the reference, {ct} in the target"
              f"   {'match' if close else 'DIVERGES'}")
    if "GNames" in derived and "GObjects" in derived:
        dr = REFERENCE_ANCHORS["GObjects"] - REFERENCE_ANCHORS["GNames"]
        dt = derived["GObjects"] - derived["GNames"]
        print(f"   GObjects - GNames  reference 0x{dr:X}, target 0x{dt:X}"
              f"   {'match' if dr == dt else 'MISMATCH -- distrust both'}")
    if "ProcessEvent" in derived:
        pr, pt = REFERENCE_FUNCTIONS["ProcessEvent"], derived["ProcessEvent"]
        print(f"   ProcessEvent stored in {ref.count_pointers_to(pr)} vtables in the reference,"
              f" {tgt.count_pointers_to(pt)} in the target")
        for tag, pe, rva in (("reference", ref, pr), ("target", tgt, pt)):
            hist = slot_histogram(pe, rva)
            top = hist.most_common(1)[0] if hist else (None, 0)
            flag = "" if top[0] == PROCESS_EVENT_SLOT else f"  ⚠ EXPECTED SLOT {PROCESS_EVENT_SLOT}"
            print(f"   vtable slot, {tag:9}: most common is {top[0]} ({top[1]} vtables){flag}")
        lockstep, mismatch, first = diff_function(ref, tgt, pr, pt)
        print(f"   ProcessEvent diff: {lockstep} instructions in lockstep,"
              f" {mismatch} differing structure displacements")
        print(f"   first difference : {first or 'none'}")
    if "MovieSubtitleDraw" in derived:
        # The hook's target. Its prologue is a common shape, so the signature match alone is weak;
        # what settles it is the whole body agreeing, structure displacements included.
        lockstep, mismatch, first = diff_function(
            ref, tgt, REFERENCE_FUNCTIONS["MovieSubtitleDraw"], derived["MovieSubtitleDraw"])
        print(f"   MovieSubtitleDraw diff: {lockstep} instructions in lockstep,"
              f" {mismatch} differing structure displacements")
        print(f"   first difference : {first or 'none'}")
    print()

    print("== summary ==")
    known = dict(REFERENCE_ANCHORS, **REFERENCE_FUNCTIONS, **REFERENCE_VTABLES)
    for name, rva in known.items():
        got = derived.get(name)
        print(f"   {name:18} reference 0x{rva:<9X} target "
              f"{('0x%X' % got) if got else 'NOT FOUND':<11} delta "
              f"{(f'{got - rva:+#x}') if got else '--'}")
    return 0 if len(derived) == len(known) else 1


if __name__ == "__main__":
    sys.exit(main())
