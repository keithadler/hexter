#!/bin/sh
#
# Every DEBUG_MESSAGE, GUIDB_MESSAGE and TUIDB_MESSAGE call ends in a
# semicolon.
#
# Those macros expand to `do { ... } while (0)`, which is a statement and needs
# its terminator. They used to expand to nothing in a release build, so a call
# site that forgot the semicolon compiled anyway and nobody found out.
#
# One had. It was in the GTK GUI, which is built on Linux and not on macOS, so
# it went through every local build and every other platform's CI and failed
# only on the one job that compiles that file. This check takes no time and
# does not need GTK, so it catches that on the machine the edit was made on.
set -eu
cd "$(dirname "$0")/.."

python3 - <<'PY'
import pathlib, re, sys

bad = []
for f in sorted(pathlib.Path('src').glob('*.c')):
    s = f.read_text(errors='replace')
    for m in re.finditer(r'\b(DEBUG_MESSAGE|GUIDB_MESSAGE|TUIDB_MESSAGE)\s*\(', s):
        i = s.index('(', m.start())
        depth = 0
        while i < len(s):
            if s[i] == '(':
                depth += 1
            elif s[i] == ')':
                depth -= 1
                if depth == 0:
                    break
            i += 1
        j = i + 1
        while j < len(s) and s[j] in ' \t':
            j += 1
        if j >= len(s) or s[j] != ';':
            bad.append("%s:%d: %s( ... ) is not terminated with a semicolon"
                       % (f, s[:m.start()].count('\n') + 1, m.group(1)))

if bad:
    print("\n".join(bad))
    sys.exit(1)
print("debug macro calls: all terminated")
PY
