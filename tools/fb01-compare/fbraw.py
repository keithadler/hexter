# Write the raw 64 parameters of every voice of every bank, in bank order,
# which is what FB_TX_Conv reads on stdin.
import sys
out = open(sys.argv[1], 'wb')
n = 0
for path in sys.argv[2:]:
    d = open(path, 'rb').read()
    off = 74 if len(d) == 6363 else 71 if len(d) == 6360 else None
    if off is None:
        sys.exit("%s: unexpected size %d" % (path, len(d)))
    for v in range(48):
        p = d[off + v * 131 + 2:][:128]
        out.write(bytes(((p[2*i] & 0x0f) | ((p[2*i+1] & 0x0f) << 4)) for i in range(64)))
        n += 1
print("fb raw:", n, "voices", file=sys.stderr)
