"""Unpack both streams to 155-byte DX7 voices and name every difference."""
import sys, collections

def unpack(p):
    """128 packed bytes -> 155 unpacked. Transcribed from hexter's
    dx7_patch_unpack() so the field boundaries are the engine's, not mine."""
    v = [0]*155
    up = 0; pp = 0
    for _ in range(6):
        for _ in range(11):
            v[up] = p[pp]; up += 1; pp += 1          # EG rates/levels, brkpt, depths
        v[up] = p[pp] & 0x03; up += 1                # left curve
        v[up] = p[pp] >> 2;   up += 1; pp += 1       # right curve
        v[up]     = p[pp] & 0x07                     # rate scaling
        v[up + 6] = p[pp] >> 3;  pp += 1             # detune, six along
        up += 1
        v[up] = p[pp] & 0x03; up += 1                # amp mod sensitivity
        v[up] = p[pp] >> 2;   up += 1; pp += 1       # key velocity sensitivity
        v[up] = p[pp]; up += 1; pp += 1              # output level
        v[up] = p[pp] & 0x01; up += 1                # osc mode
        v[up] = p[pp] >> 1;   up += 1; pp += 1       # freq coarse
        v[up] = p[pp]; pp += 1                       # freq fine
        up += 2                                      # step over detune
    for _ in range(9):
        v[up] = p[pp]; up += 1; pp += 1              # pitch EG, algorithm
    v[up] = p[pp] & 0x07; up += 1                    # feedback
    v[up] = p[pp] >> 3;   up += 1; pp += 1           # osc key sync
    for _ in range(4):
        v[up] = p[pp]; up += 1; pp += 1              # LFO speed..amp mod depth
    v[up] = p[pp] & 0x01;        up += 1             # LFO key sync
    v[up] = (p[pp] >> 1) & 0x07; up += 1             # LFO waveform
    v[up] = p[pp] >> 4;          up += 1; pp += 1    # pitch mod sensitivity
    for _ in range(11):
        v[up] = p[pp]; up += 1; pp += 1              # transpose, name
    return v

OP = ["EG R1","EG R2","EG R3","EG R4","EG L1","EG L2","EG L3","EG L4",
      "kbd lvl scl brkpt","kbd lvl scl left depth","kbd lvl scl right depth",
      "kbd lvl scl left curve","kbd lvl scl right curve","kbd rate scaling",
      "amp mod sensitivity","key velocity sensitivity","output level",
      "osc mode","osc freq coarse","osc freq fine","osc detune"]
GLOBAL = {134:"algorithm",135:"feedback",136:"osc key sync",
          137:"LFO speed",138:"LFO delay",139:"LFO pitch mod depth",
          140:"LFO amp mod depth",141:"LFO key sync",142:"LFO waveform",
          143:"pitch mod sensitivity",144:"transpose"}
for i in range(126,134): GLOBAL[i] = "pitch EG %d" % (i-125)
for i in range(145,155): GLOBAL[i] = "name"

def label(i):
    if i < 126:
        return "op%d %s" % (6 - i//21, OP[i % 21])
    return GLOBAL.get(i, "byte %d" % i)

def main():
    a = open(sys.argv[1],'rb').read()   # sean
    b = open(sys.argv[2],'rb').read()   # hexter
    n = len(a)//128
    assert len(b) == len(a)
    
    hits = collections.Counter()
    voices_with = collections.Counter()
    examples = {}
    identical = 0
    for k in range(n):
        va, vb = unpack(a[k*128:(k+1)*128]), unpack(b[k*128:(k+1)*128])
        diffs = [i for i in range(155) if va[i] != vb[i]]
        if not diffs:
            identical += 1
            continue
        seen = set()
        for i in diffs:
            L = label(i)
            hits[L] += 1
            seen.add(L)
            examples.setdefault(L, (k, i, va[i], vb[i]))
        for L in seen: voices_with[L] += 1
    
    print("voices compared:      %d" % n)
    print("identical, all 155:   %d  (%.1f%%)" % (identical, 100.0*identical/n))
    print("differing:            %d" % (n - identical))
    print()
    print("%-34s %7s %8s   %s" % ("parameter", "bytes", "voices", "first example (sean -> hexter)"))
    for L, c in hits.most_common():
        k, i, x, y = examples[L]
        print("%-34s %7d %8d   voice %d byte %d: %d -> %d" % (L, c, voices_with[L], k, i, x, y))

if __name__ == '__main__':
    main()
