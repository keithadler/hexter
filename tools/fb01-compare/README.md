# Checking the FB-01 converter against the one that came first

Sean Bolton wrote an FB-01 to TX7 converter in December 1986, sitting between
the two machines and tuning the tables by ear. He put it in the public domain
and sent it here in September 2026 along with 56 banks off his own hardware,
2688 voices. hexter's converter now uses his tables.

This directory answers the question that follows from that: given the same
voice, do the two converters now produce the same patch?

The answer is yes, on every parameter that sounds. The differences that remain
are all deliberate, and every one of them is accounted for per voice rather
than waved past in a total.

## Running it

You need his converter, `FB_TX_Conv.c`, which is public domain but is not
vendored here, and a directory of FB-01 bank dumps.

```sh
cc -O2 -o fbtx FB_TX_Conv.c
cc -O2 -I ../../src -o hexconv hexconv.c \
   ../../src/dx7_voice_fb01.c ../../src/dx7_voice_data.c ../../src/dx7_voice_patches.c

python3 fbraw.py raw.bin banks/*.syx     # the 64 raw parameters of every voice
./fbtx < raw.bin > sean.bin              # his conversion
./hexconv banks/*.syx > hex.bin          # hexter's

python3 account.py sean.bin hex.bin raw.bin
```

## What it reports

```
voices compared:                  2688
accounted for completely:         2688  (100.0%)
with an unexplained difference:   0

accounted differences, by class:
  operator silent on both sides             26945 bytes
  name                                      25562 bytes
  breakpoint, both depths zero              16128 bytes
  amp mod sensitivity carried                3275 bytes
  osc key sync                               2688 bytes
  operator switched off on the FB-01         1284 bytes
```

The six classes, and why each is allowed:

| class | why |
|---|---|
| **name** | his converter never carried one; every patch it writes is called `FB:new`. hexter carries the FB-01's seven characters. |
| **breakpoint** | hexter writes 39, the value that scales nothing; he leaves 0. Allowed only where both level scaling depths are zero on both sides, which makes the breakpoint unreachable. |
| **operator silent on both sides** | the DX7 has six operators and the FB-01 has four. The two left over have output level 0 either way, so their envelope bytes cannot sound. |
| **osc key sync** | he writes the whole feedback byte, which zeroes the key sync bit as a side effect. hexter sets it, because the FB-01's oscillators are key synced. |
| **amp mod sensitivity** | hexter carries it. Allowed only where the FB-01 voice actually has a nonzero one, which 835 of the 2688 do. |
| **operator switched off** | hexter silences an operator the FB-01 switched off. Allowed only where *that* operator, found by walking the same algorithm map hexter walks, has its enable bit clear. 920 voices, 34.2%, have at least one. |

Everything else has to match byte for byte: every envelope rate and level,
every output level, the algorithm, feedback, detune, coarse and fine frequency,
rate scaling, key velocity sensitivity, LFO speed, delay, waveform and depths,
pitch envelope, pitch mod sensitivity and transpose. All 2688 do.

The corpus covers all eight FB-01 algorithms, including the two whose mapping
was corrected last: algorithm 1 in 574 voices and algorithm 6 in 24.

## The check can fail

An accounting that passes proves nothing until it has been made to fail. Change
one entry of the attack table in `dx7_voice_fb01.c`, `0x12` to `0x13`, rebuild
`hexconv`, and re-run:

```
accounted for completely:         2650  (98.6%)
with an unexplained difference:   38

UNEXPLAINED:
  op6 EG R1        16   voice 50 byte 0: 18 -> 19
```

One nibble in one table, caught in 38 voices.

## What this does not check

Level scaling depth is zero in all 10752 operators of the corpus, so hexter
carrying it is exercised by nothing here. It is in the code and it is not
tested by these banks. A bank that uses it would be welcome.

Nor is any of this a hardware comparison. It says hexter agrees with a
converter a person tuned by ear in 1986 against real machines, which is a
different and weaker claim than having listened to an FB-01.
