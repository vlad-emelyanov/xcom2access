"""Build an uncooked UE3 sound package (one SoundCue + one SoundNodeWave per WAV) WITHOUT the editor.

Why this exists: the only documented way to get a sound into XCOM 2 is the SDK editor's Content
Browser — drag a WAV in, tick "Auto Create Cue", save the package (XCOM2Mods_Audio.pdf, section 1).
That is a sighted, mouse-driven workflow with no commandlet behind it (the editor's 181 commandlets
include nothing that imports audio), so for this project it is not a workflow at all. The package
format, on the other hand, is small and fully observable, and an editor-authored reference package
(MusicModdingSystem's SoundDropshipIntroCues.upk: exactly one cue and one wave) was decoded byte by
byte on 2026-09-17 to write this. Every constant below was read off that file; the PackageSource
checksum was verified against two independent packages.

    python tools/make_sound_upk.py <out.upk> <wav>... [--sound-class NAME] [--force] [--check]

The package name is the output file's stem. Each WAV `Foo.wav` becomes SoundNodeWave `Foo_Wave`
and SoundCue `Foo`, so the cue's path is `<Package>.Foo` and — because the Highlander's
XComSoundManager keys its SoundCues map by the cue's object NAME — `PlaySoundEvent("Foo")` is
how script plays it once the path is registered under `+SoundCuePaths` (docs/build.md, sounds).

A WAV's file name may carry build TAGS between the stem and the extension, `Foo.loop.right.wav`,
so the file itself says how it is built and no manifest can drift from it:
- `loop`: the cue loops for ever — a SoundNodeLooping (`Foo_Loop`, bLoopIndefinitely at its
  default TRUE) sits between the cue and the wave. Meant for a bed that script holds in an
  AudioComponent and steers with AdjustVolume, not for PlaySoundEvent, which never stops it.
- `left` / `right` / `center`: the pan is BAKED into the samples (the source is mono-summed first,
  then written to one channel of a silent stereo pair, or, for `center`, kept mono). A cue played
  without spatialisation keeps its channels as they are, so this is how a tone gets a fixed,
  world-absolute placement — spatialisation would pan it relative to the camera, which rotates.

What the game needs from a wave (measured, not assumed): the PC audio device plays the OGG VORBIS
copy in `CompressedPCData`, and the editor writes that copy with ffmpeg's libvorbis (the vendor
string in the reference package reads "Lavf55.25.101"), so ffmpeg here produces byte-compatible
data. `RawData` holds the imported WAV verbatim and is kept for parity with the editor. The WAV is
first normalised to 16-bit PCM, the one bit depth the importer accepts (the PDF: "exactly 16 bits").

Format facts (uncooked package, PackageVersion 845, LicenseeVersion 108, as the editor saves it):
- Summary: tag, version, TotalHeaderSize, folder "None", then 12 ints — PackageFlags 0x20000001,
  NameCount/Offset, ExportCount/Offset, ImportCount/Offset, DependsOffset, ImportExportGuidsOffset,
  ImportGuidsCount, ExportGuidsCount, ThumbnailTableOffset — then GUID, one generation
  (exports, names, net objects = exports), EngineVersion 8917, CookerVersion 0, CompressionFlags 0,
  no compressed chunks, PackageSource = appStrCrcCaps(package name), no additional packages to cook,
  no texture allocations.
- Every name carries flags 0x0007001000000000. Imports: Engine.SoundCue, Engine.SoundNodeWave
  (both `Core.Class`), the Engine package itself, and Engine.SoundNodeLooping when a cue loops.
  Export ObjectFlags: cue 0x000F000400000000 (LoadForClient|Server|Edit, Standalone, Public), wave
  0x000F010400000000 (the same plus PerObjectLocalized), looping node 0x0007000100000000 (LoadFor*
  plus Transactional — a subobject the editor makes with the cue as its Outer, neither public nor
  standalone); 68-byte entries (no net objects, zero guid, zero package flags). The node's Outer
  column is the cue's export index; every other export is top-level.
- Depends: the cue depends on its node chain, the node on its wave. Thumbnails: a 12-byte empty
  stub per TOP-LEVEL export (which is also where the empty import/export-guid section sits), then
  the table naming class, object and offset — subobjects get no thumbnail, as in the editor.
- Cue payload: NetIndex; tagged properties FirstNode (Object → the wave, or the looping node),
  VolumeMultiplier (Float), Duration (Float — the wave's, or 10000.0 for a loop, the engine's own
  INDEFINITELY_LOOPING_DURATION), optional SoundClass (Name); None; then the EditorData map — one
  entry per node: node index, NodePosX, NodePosY.
- Looping node payload: NetIndex; ChildNodes (ArrayProperty: count 1, the wave's index); None. Its
  loop flags stay at their class defaults, which is what "loop for ever" means.
- Wave payload: NetIndex; Duration, NumChannels, SampleRate, RawPCMDataSize; None; then five bulk
  data blocks in this order — RawData (the WAV), CompressedPCData (the OGG), and three empty ones
  (Xbox 360, PS3, and one more) — each `flags 0, element count, size on disk, ABSOLUTE file offset
  of the payload`, payload inline. An empty block still records the offset where its payload would
  start, which is the position right after its own header.
- ⛔ An object name ending in `_<digits>` is split by FName parsing into (name, number) when the
  game resolves a config PATH, but is stored literally here with number 0 — the two never match, so
  such stems are refused rather than silently unloadable.

`--check` re-parses the written file and prints its tables and where each wave's RIFF/OggS land.
Without `--force` the package is rebuilt only when it is older than any input WAV or this script,
which keeps X2ModBuildCommon's shader-precompile step (keyed on Content timestamps) from re-running
on every companion build.
"""
import os
import re
import struct
import subprocess
import sys
import tempfile
import uuid
import wave

TAG = 0x9E2A83C1
PACKAGE_VERSION = 845
LICENSEE_VERSION = 108
ENGINE_VERSION = 8917
PACKAGE_FLAGS = 0x20000001
NAME_FLAGS = 0x0007001000000000
CUE_OBJECT_FLAGS = 0x000F000400000000
WAVE_OBJECT_FLAGS = 0x000F010400000000
NODE_OBJECT_FLAGS = 0x0007000100000000
INDEFINITELY_LOOPING_DURATION = 10000.0
OGG_QUALITY = "4"  # libvorbis -q:a; the editor's own compressor is ffmpeg-based, so this matches in kind

PAN_TAGS = ("left", "right", "center")
KNOWN_TAGS = ("loop",) + PAN_TAGS

# UE3 appStrCrcCaps: a byte-wise CRC over the UPPERCASED UTF-16 name, polynomial 0x04C11DB7,
# non-reflected, two table steps per character. Reproduces 0x96AB9864 for "SoundDropshipIntroCues"
# and 0xBAD4ABAB for "MissVoiceRU_English" — both read off editor-saved packages.
_CRC_TABLE = []
for _i in range(256):
    _c = _i << 24
    for _ in range(8):
        _c = ((_c << 1) ^ 0x04C11DB7) & 0xFFFFFFFF if _c & 0x80000000 else (_c << 1) & 0xFFFFFFFF
    _CRC_TABLE.append(_c)


def str_crc_caps(s):
    crc = 0xFFFFFFFF
    for ch in s.upper():
        c = ord(ch)
        crc = ((crc << 8) & 0xFFFFFFFF) ^ _CRC_TABLE[(crc >> 24) ^ (c & 0xFF)]
        crc = ((crc << 8) & 0xFFFFFFFF) ^ _CRC_TABLE[(crc >> 24) ^ ((c >> 8) & 0xFF)]
    return crc ^ 0xFFFFFFFF


def fstring(s):
    b = s.encode("ascii") + b"\0"
    return struct.pack("<i", len(b)) + b


def i32(v):
    return struct.pack("<i", v)


def f32(v):
    return struct.pack("<f", v)


# ---- inputs -----------------------------------------------------------------------------------

def parse_spec(path):
    """`Foo.loop.right.wav` -> ("Foo", {"loop", "right"}); a bare `Foo.wav` has no tags."""
    parts = os.path.basename(path).split(".")
    stem, tags = parts[0], set(parts[1:-1])
    if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", stem) or re.search(r"_[0-9]+$", stem):
        sys.exit(f"make_sound_upk: refusing name {stem!r}: ASCII identifier, not ending in _<digits> (FName splitting)")
    unknown = tags - set(KNOWN_TAGS)
    if unknown:
        sys.exit(f"make_sound_upk: {path}: unknown tag(s) {sorted(unknown)}; known: {', '.join(KNOWN_TAGS)}")
    if len(tags & set(PAN_TAGS)) > 1:
        sys.exit(f"make_sound_upk: {path}: at most one of {', '.join(PAN_TAGS)}")
    return stem, tags


# ---- audio ------------------------------------------------------------------------------------

def run_ffmpeg(args):
    cmd = ["ffmpeg", "-y", "-hide_banner", "-loglevel", "error"] + args
    try:
        subprocess.run(cmd, check=True)
    except FileNotFoundError:
        sys.exit("make_sound_upk: ffmpeg not found on PATH (needed to normalise the WAV and encode the Vorbis copy)")
    except subprocess.CalledProcessError as e:
        sys.exit(f"make_sound_upk: ffmpeg failed ({e.returncode}) on: {' '.join(cmd)}")


def wav_info(path):
    with wave.open(path, "rb") as w:
        return w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getnframes()


def pan_filter(pan, channels):
    """The ffmpeg -af chain that bakes a fixed pan: mono-sum the source, then place it. None = leave as is."""
    mono = "c0" if channels == 1 else "0.5*c0+0.5*c1"
    if pan == "left":
        return f"pan=stereo|c0={mono}"       # the unnamed right channel is silent
    if pan == "right":
        return f"pan=stereo|c1={mono}"
    if pan == "center":
        return None if channels == 1 else f"pan=mono|c0={mono}"
    return None


def prepare_sound(src, workdir, pan=None):
    """Return (wav_bytes, ogg_bytes, channels, rate, pcm_size, duration) for one input file."""
    stem = parse_spec(src)[0]
    wav_path = os.path.join(workdir, stem + ".wav")
    ogg_path = os.path.join(workdir, stem + ".ogg")
    # No metadata and bit-exact muxing: a bare RIFF with 'fmt ' and 'data', the shape the
    # importer and FWaveModInfo parse; LIST/INFO chunks would only be dead weight in RawData.
    common = ["-map_metadata", "-1", "-fflags", "+bitexact", "-flags", "+bitexact"]
    run_ffmpeg(["-i", src] + common + ["-c:a", "pcm_s16le", "-f", "wav", wav_path])
    filt = pan_filter(pan, wav_info(wav_path)[0])
    if filt:
        panned = os.path.join(workdir, stem + ".pan.wav")
        run_ffmpeg(["-i", wav_path, "-af", filt] + common + ["-c:a", "pcm_s16le", "-f", "wav", panned])
        os.replace(panned, wav_path)
    run_ffmpeg(["-i", wav_path] + common + ["-c:a", "libvorbis", "-q:a", OGG_QUALITY, "-f", "ogg", ogg_path])
    channels, width, rate, frames = wav_info(wav_path)
    if width != 2:
        sys.exit(f"make_sound_upk: {src}: normalised WAV is not 16-bit (width {width})")
    if channels not in (1, 2):
        sys.exit(f"make_sound_upk: {src}: {channels} channels; the importer takes 1 or 2")
    wav_bytes = open(wav_path, "rb").read()
    ogg_bytes = open(ogg_path, "rb").read()
    if not wav_bytes.startswith(b"RIFF") or not ogg_bytes.startswith(b"OggS"):
        sys.exit(f"make_sound_upk: {src}: ffmpeg output is not RIFF/OggS as expected")
    return wav_bytes, ogg_bytes, channels, rate, frames * channels * 2, frames / rate


# ---- package ----------------------------------------------------------------------------------

class NameTable:
    def __init__(self):
        self.names = []
        self.index = {}

    def add(self, s):
        if s not in self.index:
            self.index[s] = len(self.names)
            self.names.append(s)

    def ref(self, s):
        # An FName on disk: name index + number. Number is always 0 here — see the docstring.
        return i32(self.index[s]) + i32(0)


def prop(names, name, type_name, value):
    return names.ref(name) + names.ref(type_name) + i32(len(value)) + i32(0) + value


def bulk(count_and_bytes, abs_offset):
    """A bulk data header: flags, element count, size on disk, absolute file offset; payload follows."""
    n = len(count_and_bytes)
    return i32(0) + i32(n) + i32(n) + i32(abs_offset) + count_and_bytes


IMPORT_CUE, IMPORT_WAVE, IMPORT_ENGINE, IMPORT_LOOP = -1, -2, -3, -4
IMPORT_CLASS_NAMES = {IMPORT_CUE: "SoundCue", IMPORT_WAVE: "SoundNodeWave", IMPORT_LOOP: "SoundNodeLooping"}


def build_package(package_name, sounds, sound_class=None):
    """sounds: list of dicts — stem, loop, wav, ogg, channels, rate, pcm_size, duration."""
    any_loop = any(s["loop"] for s in sounds)
    names = NameTable()
    wanted = {"Class", "Core", "Duration", "Engine", "FirstNode", "FloatProperty", "IntProperty", "None",
              "NumChannels", "ObjectProperty", "Package", "RawPCMDataSize", "SampleRate", "SoundCue",
              "SoundNodeWave", "VolumeMultiplier", package_name}
    wanted |= {s["stem"] for s in sounds} | {s["stem"] + "_Wave" for s in sounds}
    if any_loop:
        wanted |= {"SoundNodeLooping", "ChildNodes", "ArrayProperty"} | {s["stem"] + "_Loop" for s in sounds if s["loop"]}
    if sound_class:
        wanted |= {"SoundClass", "NameProperty", sound_class}
    for s in sorted(wanted):
        names.add(s)

    imports = (
        names.ref("Core") + names.ref("Class") + i32(IMPORT_ENGINE) + names.ref("SoundCue")
        + names.ref("Core") + names.ref("Class") + i32(IMPORT_ENGINE) + names.ref("SoundNodeWave")
        + names.ref("Core") + names.ref("Package") + i32(0) + names.ref("Engine")
    )
    import_count = 3
    if any_loop:
        imports += names.ref("Core") + names.ref("Class") + i32(IMPORT_ENGINE) + names.ref("SoundNodeLooping")
        import_count = 4

    # Exports: per sound, cue, [looping node], wave. NetIndex runs in export order. Each entry is
    # (class_import, object_name, outer_export, object_flags, payload_fn(abs_offset) -> bytes, size, depends).
    exports = []
    for s in sounds:
        cue_idx = len(exports) + 1
        loop_idx = cue_idx + 1 if s["loop"] else 0
        wave_idx = cue_idx + (2 if s["loop"] else 1)
        first = loop_idx or wave_idx

        def cue_payload(_abs, net=cue_idx - 1, first=first, loop_idx=loop_idx, wave_idx=wave_idx,
                        duration=INDEFINITELY_LOOPING_DURATION if s["loop"] else s["duration"]):
            p = i32(net)
            p += prop(names, "FirstNode", "ObjectProperty", i32(first))
            p += prop(names, "VolumeMultiplier", "FloatProperty", f32(1.0))
            p += prop(names, "Duration", "FloatProperty", f32(duration))
            if sound_class:
                p += prop(names, "SoundClass", "NameProperty", names.ref(sound_class))
            p += names.ref("None")
            # EditorData: one entry per node, laid out left to right as the cue editor would.
            nodes = [loop_idx, wave_idx] if loop_idx else [wave_idx]
            p += i32(len(nodes))
            for k, node in enumerate(nodes):
                p += i32(node) + i32(100 - 250 * k) + i32(-35)
            return p

        def loop_payload(_abs, net=loop_idx - 1, wave_idx=wave_idx):
            p = i32(net)
            p += prop(names, "ChildNodes", "ArrayProperty", i32(1) + i32(wave_idx))
            p += names.ref("None")
            return p

        def wave_payload(abs_off, net=wave_idx - 1, s=s):
            p = i32(net)
            p += prop(names, "Duration", "FloatProperty", f32(s["duration"]))
            p += prop(names, "NumChannels", "IntProperty", i32(s["channels"]))
            p += prop(names, "SampleRate", "IntProperty", i32(s["rate"]))
            p += prop(names, "RawPCMDataSize", "IntProperty", i32(s["pcm_size"]))
            p += names.ref("None")
            p += bulk(s["wav"], abs_off + len(p) + 16)
            p += bulk(s["ogg"], abs_off + len(p) + 16)
            for _ in range(3):
                p += bulk(b"", abs_off + len(p) + 16)
            return p

        cue_deps = [loop_idx, wave_idx] if loop_idx else [wave_idx]
        exports.append((IMPORT_CUE, s["stem"], 0, CUE_OBJECT_FLAGS, cue_payload, len(cue_payload(0)), cue_deps))
        if loop_idx:
            exports.append((IMPORT_LOOP, s["stem"] + "_Loop", cue_idx, NODE_OBJECT_FLAGS, loop_payload,
                            len(loop_payload(0)), [wave_idx]))
        exports.append((IMPORT_WAVE, s["stem"] + "_Wave", 0, WAVE_OBJECT_FLAGS, wave_payload, len(wave_payload(0)), []))

    # Header layout, computed once: nothing below depends on the payload offsets except the
    # export table's SerialOffset column and the bulk headers, and both are filled in last.
    name_table = b"".join(fstring(n) + struct.pack("<Q", NAME_FLAGS) for n in names.names)
    export_entry_size = 68
    depends = b"".join(i32(len(d)) + b"".join(i32(x) for x in d) for *_, d in exports)
    thumbnailed = [(cls, obj) for cls, obj, outer, *_ in exports if outer == 0]
    thumbs_data = b"\0" * (12 * len(thumbnailed))
    fixed_before_names = 4 + 4 + 4 + len(fstring("None")) + 48 + 16 + 4 + 12 + 4 * 7
    name_off = fixed_before_names
    import_off = name_off + len(name_table)
    export_off = import_off + len(imports)
    depends_off = export_off + export_entry_size * len(exports)
    guids_off = depends_off + len(depends)
    thumb_table_off = guids_off + len(thumbs_data)
    thumb_table = i32(len(thumbnailed))
    for k, (cls, obj) in enumerate(thumbnailed):
        thumb_table += fstring(IMPORT_CLASS_NAMES[cls]) + fstring(obj) + i32(guids_off + 12 * k)
    header_size = thumb_table_off + len(thumb_table)

    # Export data, with absolute offsets now known.
    offsets, payloads, pos = [], [], header_size
    for cls, obj, outer, flags, fn, size, _d in exports:
        payload = fn(pos)
        assert len(payload) == size
        offsets.append(pos)
        payloads.append(payload)
        pos += size

    export_table = b""
    for (cls, obj, outer, flags, _fn, size, _d), off in zip(exports, offsets):
        export_table += (i32(cls) + i32(0) + i32(outer) + names.ref(obj) + i32(0) + struct.pack("<Q", flags)
                         + i32(size) + i32(off) + i32(0) + i32(0) + b"\0" * 16 + i32(0))
    assert len(export_table) == export_entry_size * len(exports)

    summary = (
        struct.pack("<I", TAG) + i32((LICENSEE_VERSION << 16) | PACKAGE_VERSION) + i32(header_size) + fstring("None")
        + struct.pack("<12i", PACKAGE_FLAGS, len(names.names), name_off, len(exports), export_off,
                      import_count, import_off, depends_off, guids_off, 0, 0, thumb_table_off)
        + uuid.uuid4().bytes
        + i32(1) + i32(len(exports)) + i32(len(names.names)) + i32(len(exports))
        + i32(ENGINE_VERSION) + i32(0) + i32(0) + i32(0)
        + struct.pack("<I", str_crc_caps(package_name)) + i32(0) + i32(0)
    )
    assert len(summary) == name_off
    header = summary + name_table + imports + export_table + depends + thumbs_data + thumb_table
    assert len(header) == header_size
    return header + b"".join(payloads)


# ---- check -------------------------------------------------------------------------------------

def read_fstring(buf, pos):
    (n,) = struct.unpack_from("<i", buf, pos)
    return buf[pos + 4:pos + 4 + n - 1].decode("latin1"), pos + 4 + n


def check(path):
    raw = open(path, "rb").read()
    tag, ver, header_size = struct.unpack_from("<Iii", raw, 0)
    folder, pos = read_fstring(raw, 12)
    f = struct.unpack_from("<12i", raw, pos)
    print(f"{path}: tag={tag:#x} version={ver & 0xFFFF}/{ver >> 16} header={header_size} flags={f[0]:#x}")
    names, p = [], f[2]
    for _ in range(f[1]):
        s, p = read_fstring(raw, p)
        p += 8
        names.append(s)
    print("  names:", names)
    p = f[6]
    for k in range(f[5]):
        cls_ni, = struct.unpack_from("<i", raw, p + 8)
        obj_ni, = struct.unpack_from("<i", raw, p + 20)
        print(f"  import [{-(k + 1)}] {names[cls_ni]} {names[obj_ni]}")
        p += 28
    p = f[4]
    for k in range(f[3]):
        cls, sup, outer = struct.unpack_from("<iii", raw, p)
        ni, = struct.unpack_from("<i", raw, p + 12)
        flags, = struct.unpack_from("<Q", raw, p + 24)
        size, off = struct.unpack_from("<ii", raw, p + 32)
        payload = raw[off:off + size]
        marks = {sig: [m.start() for m in re.finditer(re.escape(sig), payload)][:3] for sig in (b"RIFF", b"OggS")}
        print(f"  [{k + 1}] class={cls} outer={outer} {names[ni]} flags={flags:#x} size={size} off={off}"
              f" RIFF={marks[b'RIFF']} OggS={marks[b'OggS']}")
        p += 68
    assert header_size == struct.unpack_from("<i", raw, f[4] + 36)[0], "first export must start at the header's end"
    print(f"  {len(raw)} bytes")


# ---- main --------------------------------------------------------------------------------------

def main(argv):
    force = "--force" in argv
    do_check = "--check" in argv
    sound_class = None
    if "--sound-class" in argv:
        i = argv.index("--sound-class")
        sound_class = argv[i + 1]
        del argv[i:i + 2]
    argv = [a for a in argv if a not in ("--force", "--check")]
    if len(argv) < 2:
        print(__doc__)
        return 2
    out, wavs = argv[0], argv[1:]
    package_name = os.path.splitext(os.path.basename(out))[0]
    if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", package_name) or re.search(r"_[0-9]+$", package_name):
        sys.exit(f"make_sound_upk: refusing package name {package_name!r}: ASCII identifier, not ending in _<digits>")
    specs = [parse_spec(w) for w in wavs]
    stems = [stem for stem, _ in specs]
    if len(set(stems)) != len(stems):
        sys.exit(f"make_sound_upk: duplicate cue name among inputs: {sorted(set(s for s in stems if stems.count(s) > 1))}")

    newest_input = max(os.path.getmtime(p) for p in wavs + [os.path.abspath(__file__)])
    if not force and os.path.exists(out) and os.path.getmtime(out) >= newest_input:
        print(f"make_sound_upk: {out} is up to date")
        if do_check:
            check(out)
        return 0

    sounds = []
    with tempfile.TemporaryDirectory() as workdir:
        for w, (stem, tags) in zip(wavs, specs):
            pan = next((t for t in PAN_TAGS if t in tags), None)
            wav_b, ogg_b, channels, rate, pcm_size, duration = prepare_sound(w, workdir, pan)
            sounds.append(dict(stem=stem, loop="loop" in tags, wav=wav_b, ogg=ogg_b, channels=channels,
                               rate=rate, pcm_size=pcm_size, duration=duration))
    data = build_package(package_name, sounds, sound_class)
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    open(out, "wb").write(data)
    described = ", ".join(s["stem"] + (" (loop)" if s["loop"] else "") for s in sounds)
    print(f"make_sound_upk: wrote {out} ({len(data)} bytes, {len(sounds)} cue(s): {described})")
    if do_check:
        check(out)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
