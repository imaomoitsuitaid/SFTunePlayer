"""Generates small test assets: a sine-wave SF2, an SFZ + WAV, and tuning files."""
import struct, math, wave, sys, os
out = sys.argv[1] if len(sys.argv) > 1 else "assets"
os.makedirs(out, exist_ok=True)

def chunk(tag, data):
    pad = b"\0" if len(data) % 2 else b""
    return tag + struct.pack("<I", len(data)) + data + pad
def lst(tag, data): return chunk(b"LIST", tag + data)
def name20(s): return s.encode()[:20].ljust(20, b"\0")

# ---- SF2: 440 Hz sine at 44000 Hz (period = exactly 100 samples) ----
sr, n = 44000, 44000
smp = b"".join(struct.pack("<h", int(20000 * math.sin(2 * math.pi * i / 100))) for i in range(n)) + b"\0\0" * 46
info = lst(b"INFO", chunk(b"ifil", struct.pack("<HH", 2, 1)) + chunk(b"INAM", b"Test\0\0"))
sdta = lst(b"sdta", chunk(b"smpl", smp))
# presets: 0 = plain, 1 = preset-level coarseTune +12 (additive)
phdr = (name20("Sine") + struct.pack("<HHHIII", 0, 0, 0, 0, 0, 0) +
        name20("Sine+Oct") + struct.pack("<HHHIII", 1, 0, 1, 0, 0, 0) +
        name20("EOP") + struct.pack("<HHHIII", 0, 0, 2, 0, 0, 0))
pbag = struct.pack("<HH", 0, 0) + struct.pack("<HH", 1, 0) + struct.pack("<HH", 3, 0)
pgen = (struct.pack("<Hh", 41, 0) +                       # preset 0: instrument 0
        struct.pack("<Hh", 51, 12) + struct.pack("<Hh", 41, 0) +   # preset 1: coarse +12, instrument 0
        struct.pack("<Hh", 0, 0))
pmod = b"\0" * 10 * 1
inst = name20("SineInst") + struct.pack("<H", 0) + name20("EOI") + struct.pack("<H", 1)
ibag = struct.pack("<HH", 0, 0) + struct.pack("<HH", 2, 0)
igen = (struct.pack("<Hh", 54, 1) +      # sampleModes = loop
        struct.pack("<Hh", 53, 0) +      # sampleID 0
        struct.pack("<Hh", 0, 0))
imod = b"\0" * 10
shdr = (name20("sine440") + struct.pack("<IIIIIBbHH", 0, n, 1000, 41000, sr, 69, 0, 0, 1) +
        name20("EOS") + struct.pack("<IIIIIBbHH", 0, 0, 0, 0, 0, 0, 0, 0, 0))
pdta = lst(b"pdta", b"".join(chunk(t, d) for t, d in [(b"phdr", phdr), (b"pbag", pbag), (b"pmod", pmod), (b"pgen", pgen),
                                                      (b"inst", inst), (b"ibag", ibag), (b"imod", imod), (b"igen", igen), (b"shdr", shdr)]))
body = b"sfbk" + info + sdta + pdta
open(f"{out}/sine.sf2", "wb").write(b"RIFF" + struct.pack("<I", len(body)) + body)

# ---- SFZ: 220.5 Hz sine at 44100 (period 200) ----
os.makedirs(f"{out}/samples", exist_ok=True)
with wave.open(f"{out}/samples/sine 220.wav", "wb") as w:
    w.setnchannels(1); w.setsampwidth(2); w.setframerate(44100)
    w.writeframes(b"".join(struct.pack("<h", int(20000 * math.sin(2 * math.pi * i / 200))) for i in range(44100)))
open(f"{out}/test.sfz", "w").write("""// test instrument
<control> default_path=samples/
#define $KC 57
<global> ampeg_release=0.2
<group> loop_mode=loop_continuous loop_start=200 loop_end=40199
<region> sample=sine 220.wav lokey=0 hikey=63 pitch_keycenter=$KC
<region> sample=sine 220.wav lokey=e4 hikey=127 pitch_keycenter=a3 tune=50 /* +50 cents */
""")

# ---- tunings ----
open(f"{out}/19edo.scl", "w").write("! 19edo.scl\n!\n19-tone equal temperament\n 19\n!\n" +
                                   "".join(f" {1200*i/19:.5f}\n" for i in range(1, 20)))
open(f"{out}/just.scl", "w").write("! just.scl\nJust major\n7\n9/8\n5/4\n4/3\n3/2\n5/3\n15/8\n2/1\n")
# white-key-only mapping of the just scale; reference A4 = 440
open(f"{out}/whitekeys.kbm", "w").write("! map\n12\n0\n127\n60\n69\n440.0\n7\n0\nx\n1\nx\n2\n3\nx\n4\nx\n5\nx\n6\n")
# AnaMark tun: 12-TET but every key +25 cents, note 60 exactly 6013.6863 (exact section overrides)
lines = ["; test tun", "[Tuning]"] + [f"note {k}={100*k+25}" for k in range(128)]
lines += ["[Exact Tuning]", "note 60=6013.6863"]
open(f"{out}/plus25.tun", "w").write("\n".join(lines) + "\n")
print("assets written to", out)
