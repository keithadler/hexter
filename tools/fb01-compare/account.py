"""
Every difference between Sean Bolton's 1986 converter and hexter's, accounted
for or reported. The rule: a difference is allowed only if it falls into one of
the classes below AND the data justifies it for that particular voice. Anything
else is a finding.

  1. name        his converter never carried it; hexter does
  2. breakpoint  only reachable through a level scaling depth; skipped only
                 when both sides have both depths at zero for that operator
  3. silent op   an operator whose output level is 0 on both sides cannot
                 sound, so its envelope bytes are inaudible
  4. key sync    he writes the whole feedback byte, which zeroes it; hexter
                 sets it, because the FB-01's oscillators are key synced
  5. amp mod     hexter carries it; allowed only where the FB-01 voice has a
                 nonzero amplitude mod sensitivity
  6. op enable   hexter silences an operator the FB-01 switched off; allowed
                 only where that operator's enable bit is actually clear
"""
import sys, collections
sys.path.insert(0, sys.path[0] or '.')
from dx7voice import unpack, label

# hexter's fb01_algorithm_map: for each FB-01 algorithm, which DX7 operator
# each of FB-01 OP1..OP4 becomes.
MAP = [[3,4,5,6],[3,4,5,6],[3,5,6,4],[3,4,5,6],[3,4,5,6],[3,4,5,6],[2,3,5,6],[3,4,5,6]]

sean = open(sys.argv[1], 'rb').read()
hexr = open(sys.argv[2], 'rb').read()
raw  = open(sys.argv[3], 'rb').read()
n = len(sean) // 128

allowed = collections.Counter()
findings = collections.Counter()
examples = {}
clean = 0

for k in range(n):
    a, b = unpack(sean[k*128:(k+1)*128]), unpack(hexr[k*128:(k+1)*128])
    fb = raw[k*64:(k+1)*64]
    enable = fb[11]
    alg = fb[12] & 0x07
    fb_ams = fb[13] & 0x03

    # which of the four FB-01 operators each DX7 operator came from is not
    # needed: what matters per DX7 operator is whether it sounds at all.
    bad = []
    for i in range(155):
        if a[i] == b[i]:
            continue
        if 145 <= i <= 154:
            allowed['name'] += 1; continue
        if i == 136:
            allowed['osc key sync'] += 1; continue
        if i < 126:
            op = i // 21            # 0 = op6 ... 5 = op1
            o = op * 21
            silent_both = (a[o+16] == 0 and b[o+16] == 0)
            field = i % 21
            if field == 8 and a[o+9] == b[o+9] == 0 and a[o+10] == b[o+10] == 0:
                allowed['breakpoint, both depths zero'] += 1; continue
            if silent_both:
                allowed['operator silent on both sides'] += 1; continue
            if field == 14 and fb_ams:
                allowed['amp mod sensitivity carried'] += 1; continue
            if field == 16 and b[i] == 0:
                # hexter silenced it. Allowed only if THIS DX7 operator is the
                # one the FB-01 switched off, which means walking the same
                # algorithm map hexter walks: FB-01 OP(j+1) lands on DX7
                # operator MAP[alg][j], and its enable bit is 6-j.
                op_number = 6 - op          # op index 0 is DX7 operator 6
                j = [x for x in range(4) if MAP[alg][x] == op_number]
                if j and not ((enable >> (6 - j[0])) & 1):
                    allowed['operator switched off on the FB-01'] += 1; continue
        bad.append(i)
    if not bad:
        clean += 1
    for i in bad:
        L = label(i)
        findings[L] += 1
        examples.setdefault(L, (k, i, a[i], b[i]))

print("voices compared:                  %d" % n)
print("accounted for completely:         %d  (%.1f%%)" % (clean, 100.0*clean/n))
print("with an unexplained difference:   %d" % (n - clean))
print()
print("accounted differences, by class:")
for L, c in allowed.most_common():
    print("  %-38s %8d bytes" % (L, c))
if findings:
    print()
    print("UNEXPLAINED:")
    for L, c in findings.most_common():
        k, i, x, y = examples[L]
        print("  %-38s %6d   voice %d byte %d: %d -> %d" % (L, c, k, i, x, y))
else:
    print()
    print("No unexplained differences. On every parameter that sounds, and is")
    print("not one of the documented departures, the two converters agree byte")
    print("for byte across all %d voices." % n)
