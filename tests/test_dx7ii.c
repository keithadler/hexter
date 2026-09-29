/* hexter DSSI software synthesizer plugin
 *
 * The DX7II's extra voice data, checked.
 *
 * Copyright (C) 2026 Keith Adler
 *
 * There is no DX7II here and no bank off one, so nothing below is a comparison
 * against real hardware. What it can do is hold the packing to the published
 * layout rather than to itself: a round trip cannot catch a field that was
 * packed and unpacked at the same wrong offset, and that is exactly the class
 * of bug that got through the first run of the Casio librarian's tests. So the
 * bit positions are asserted as absolute bytes, taken from the specification,
 * and the round trip is only the second check.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>

#include "dx7_voice_dx7ii.h"
#include "dx7_bank.h"
#include "dx7_voice.h"

static int passed, failed;

static void ok(int cond, const char *fmt, ...)
{
    if (cond) { passed++; return; }
    failed++;
    va_list ap;
    va_start(ap, fmt);
    printf("  FAIL: ");
    vprintf(fmt, ap);
    printf("\n");
    va_end(ap);
}

/* ACED offsets, repeated here on purpose. The implementation has its own copy,
 * and two copies that were written from the specification separately will
 * disagree if either was typed wrong; one shared copy would agree with itself
 * however wrong it was. */
enum {
    A_SCALING = 0, A_AMS = 6, A_PEG_RANGE = 12, A_LFO_TRIG = 13, A_PEG_VEL = 14,
    A_RANDOM = 15, A_MODE = 16, A_PB_RANGE = 17, A_PB_STEP = 18, A_PB_MODE = 19,
    A_PORTA_MODE = 20, A_PORTA_STEP = 21, A_PORTA_TIME = 22,
    A_MW = 23, A_FC1 = 26, A_BC = 30, A_AT = 34, A_PEG_RATE = 38,
    A_FC2 = 64, A_MIDI = 68, A_UNISON = 72
};

static void test_bit_positions(void)
{
    printf("-- each field comes out of the byte the specification names --\n");

    uint8_t p[DX7II_AMEM_VOICE];
    dx7ii_aced_t a;

    /* byte 0: scaling mode, one bit per operator, OP6 in bit 0 */
    for (int op = 0; op < 6; op++) {
        memset(p, 0, sizeof p);
        p[0] = (uint8_t)(1u << op);
        dx7ii_amem_unpack(p, &a);
        ok(a.raw[A_SCALING + op] == 1,
           "byte 0 bit %d is OP%d's scaling mode", op, 6 - op);
        int others = 0;
        for (int k = 0; k < 6; k++) if (k != op && a.raw[A_SCALING + k]) others++;
        ok(others == 0, "and setting it sets no other operator's");
    }

    /* bytes 1 to 3: two operators each, three bits, low operator low */
    struct { int byte, shift, aced_op; } ams[] = {
        { 1, 0, 0 }, { 1, 3, 1 },    /* OP6, OP5 */
        { 2, 0, 2 }, { 2, 3, 3 },    /* OP4, OP3 */
        { 3, 0, 4 }, { 3, 3, 5 },    /* OP2, OP1 */
    };
    for (size_t i = 0; i < sizeof ams / sizeof ams[0]; i++) {
        memset(p, 0, sizeof p);
        p[ams[i].byte] = (uint8_t)(5u << ams[i].shift);
        dx7ii_amem_unpack(p, &a);
        ok(a.raw[A_AMS + ams[i].aced_op] == 5,
           "byte %d bits %d..%d is operator slot %d's AM sensitivity",
           ams[i].byte, ams[i].shift, ams[i].shift + 2, ams[i].aced_op);
    }

    /* byte 4 carries four things */
    memset(p, 0, sizeof p); p[4] = 0x03;
    dx7ii_amem_unpack(p, &a);
    ok(a.raw[A_PEG_RANGE] == 3, "byte 4 bits 1:0 are the pitch envelope range");

    memset(p, 0, sizeof p); p[4] = 0x04;
    dx7ii_amem_unpack(p, &a);
    ok(a.raw[A_LFO_TRIG] == 1, "byte 4 bit 2 is the LFO key trigger");

    memset(p, 0, sizeof p); p[4] = 0x70;
    dx7ii_amem_unpack(p, &a);
    ok(a.raw[A_RANDOM] == 7, "byte 4 bits 6:4 are the random pitch amount");

    /* byte 5 */
    memset(p, 0, sizeof p); p[5] = 0x03;
    dx7ii_amem_unpack(p, &a);
    ok(a.raw[A_MODE] == 3, "byte 5 bits 1:0 are the voice mode");

    memset(p, 0, sizeof p); p[5] = (uint8_t)(12u << 2);
    dx7ii_amem_unpack(p, &a);
    ok(a.raw[A_PB_RANGE] == 12, "byte 5 bits 5:2 are the pitch bend range");

    /* byte 6 */
    memset(p, 0, sizeof p); p[6] = 0x0c;
    dx7ii_amem_unpack(p, &a);
    ok(a.raw[A_PB_STEP] == 12, "byte 6 bits 3:0 are the pitch bend step");

    memset(p, 0, sizeof p); p[6] = (uint8_t)(2u << 4);
    dx7ii_amem_unpack(p, &a);
    ok(a.raw[A_PB_MODE] == 2, "byte 6 bits 5:4 are the pitch bend mode");

    /* byte 7 */
    memset(p, 0, sizeof p); p[7] = 0x01;
    dx7ii_amem_unpack(p, &a);
    ok(a.raw[A_PORTA_MODE] == 1, "byte 7 bit 0 is the portamento mode");

    memset(p, 0, sizeof p); p[7] = (uint8_t)(12u << 1);
    dx7ii_amem_unpack(p, &a);
    ok(a.raw[A_PORTA_STEP] == 12, "byte 7 bits 4:1 are the portamento step");

    /* byte 8 and the plain byte runs */
    memset(p, 0, sizeof p); p[8] = 99;
    dx7ii_amem_unpack(p, &a);
    ok(a.raw[A_PORTA_TIME] == 99, "byte 8 is the portamento time");

    struct { int from, count, to; const char *what; } runs[] = {
        {  9, 3, A_MW,   "mod wheel" },
        { 12, 4, A_FC1,  "foot controller 1" },
        { 16, 4, A_BC,   "breath controller" },
        { 20, 4, A_AT,   "aftertouch" },
        { 26, 4, A_FC2,  "foot controller 2" },
        { 30, 4, A_MIDI, "the MIDI controller" },
    };
    for (size_t i = 0; i < sizeof runs / sizeof runs[0]; i++) {
        memset(p, 0, sizeof p);
        for (int k = 0; k < runs[i].count; k++)
            p[runs[i].from + k] = (uint8_t)(11 + k);
        dx7ii_amem_unpack(p, &a);
        int good = 1;
        for (int k = 0; k < runs[i].count; k++)
            if (a.raw[runs[i].to + k] != (uint8_t)(11 + k)) good = 0;
        ok(good, "bytes %d.. are %s's routing, in order",
           runs[i].from, runs[i].what);
    }

    memset(p, 0, sizeof p); p[24] = 0x07;
    dx7ii_amem_unpack(p, &a);
    ok(a.raw[A_PEG_RATE] == 7, "byte 24 bits 2:0 are the pitch envelope rate scaling");

    memset(p, 0, sizeof p); p[34] = 0x07;
    dx7ii_amem_unpack(p, &a);
    ok(a.raw[A_UNISON] == 7, "byte 34 bits 2:0 are the unison detune");

    /* the reserved run really is untouched */
    memset(p, 0xff, sizeof p);
    dx7ii_amem_unpack(p, &a);
    int reserved_clear = 1;
    for (int i = 39; i <= 63; i++) if (a.raw[i]) reserved_clear = 0;
    ok(reserved_clear, "ACED 39 to 63 are reserved and stay zero");
}

static void test_the_bit_that_cannot_survive(void)
{
    printf("-- the one field that loses information, losing it on purpose --\n");

    uint8_t p[DX7II_AMEM_VOICE];
    dx7ii_aced_t a;

    memset(p, 0, sizeof p);
    dx7ii_amem_unpack(p, &a);
    ok(a.raw[A_PEG_VEL] == 0, "no pitch envelope velocity stays none");

    memset(p, 0, sizeof p);
    p[4] = 0x08;
    dx7ii_amem_unpack(p, &a);
    /*
     * AMEM has one bit where ACED has three. A set bit cannot mean 1, because
     * 1 of 7 is nearly nothing and the bank plainly meant something. It comes
     * back as the top of the range.
     */
    ok(a.raw[A_PEG_VEL] == 7,
       "a set bit becomes full depth, not 1 (got %u)", a.raw[A_PEG_VEL]);

    /* and packing it back sets the bit again, whatever value it holds */
    for (int v = 1; v <= 7; v++) {
        dx7ii_aced_t b;
        memset(&b, 0, sizeof b);
        b.raw[A_PEG_VEL] = (uint8_t)v;
        uint8_t q[DX7II_AMEM_VOICE];
        dx7ii_aced_pack(&b, q);
        ok((q[4] & 0x08) != 0, "packing pitch envelope velocity %d sets the bit", v);
    }
}

static void test_round_trip(void)
{
    printf("-- pack and unpack agree, over every value each field can hold --\n");

    /* Deterministic, so a failure can be reproduced from the seed alone. */
    unsigned seed = 20260928u;
    int mismatches = 0;

    for (int iter = 0; iter < 2000; iter++) {
        uint8_t p[DX7II_AMEM_VOICE], q[DX7II_AMEM_VOICE];
        for (int i = 0; i < DX7II_AMEM_VOICE; i++) {
            seed = seed * 1103515245u + 12345u;
            p[i] = (uint8_t)((seed >> 16) & 0x7f);
        }

        dx7ii_aced_t a, b;
        dx7ii_amem_unpack(p, &a);
        dx7ii_aced_pack(&a, q);
        dx7ii_amem_unpack(q, &b);

        /* Packing is lossy by construction in one field, so the fixed point is
         * the second unpack, not the first pack. */
        if (memcmp(a.raw, b.raw, DX7II_ACED_SIZE) != 0) mismatches++;
    }
    ok(mismatches == 0,
       "unpack, pack, unpack lands in the same place every time (%d of 2000 did not)",
       mismatches);
}

static void test_finding_a_dump(void)
{
    printf("-- an AMEM dump is recognised, and things that are not one are not --\n");

    uint8_t dump[DX7II_AMEM_DUMP];
    memset(dump, 0, sizeof dump);
    dump[0] = 0xf0; dump[1] = 0x43; dump[2] = 0x00; dump[3] = DX7II_FORMAT_AMEM;
    dump[4] = 0x08; dump[5] = 0x60;
    for (int i = 0; i < DX7II_AMEM_DATA; i++) dump[6 + i] = (uint8_t)(i & 0x7f);
    dump[6 + DX7II_AMEM_DATA] = dx7ii_checksum(dump + 6, DX7II_AMEM_DATA);
    dump[7 + DX7II_AMEM_DATA] = 0xf7;

    const uint8_t *data = NULL;
    ok(dx7ii_amem_at(dump, sizeof dump, 0, 0, &data) == 1, "finds a clean AMEM dump");
    ok(data == dump + 6, "and points at the data rather than the header");

    ok(dx7ii_aced_at(dump, sizeof dump, 0, 0, NULL) == 0,
       "and does not mistake it for a single voice's ACED");

    /* the checks that matter are the ones that reject */
    struct { int at; uint8_t to; const char *why; } spoil[] = {
        { 1, 0x42, "a manufacturer that is not Yamaha" },
        { 3, 0x09, "a DX7 voice dump rather than an AMEM" },
        { 4, 0x09, "a byte count that is not 1120" },
        { 5, 0x61, "a byte count that is not 1120" },
        { 7 + DX7II_AMEM_DATA, 0x00, "no end of exclusive where one belongs" },
    };
    for (size_t i = 0; i < sizeof spoil / sizeof spoil[0]; i++) {
        uint8_t bad[DX7II_AMEM_DUMP];
        memcpy(bad, dump, sizeof bad);
        bad[spoil[i].at] = spoil[i].to;
        ok(dx7ii_amem_at(bad, sizeof bad, 0, 0, NULL) == 0,
           "rejects %s", spoil[i].why);
    }

    /* truncation must not read past the end */
    for (size_t len = 0; len < sizeof dump; len += 97)
        ok(dx7ii_amem_at(dump, len, 0, 0, NULL) == 0,
           "rejects a dump cut off at %zu bytes", len);

    /* found at an offset, and inside a MIDI file's two byte shift */
    uint8_t padded[DX7II_AMEM_DUMP + 40];
    memset(padded, 0x11, sizeof padded);
    memcpy(padded + 17, dump, sizeof dump);
    ok(dx7ii_amem_at(padded, sizeof padded, 17, 0, NULL) == 1,
       "finds one part way through a file");
    ok(dx7ii_amem_at(padded, sizeof padded, 16, 0, NULL) == 0,
       "and not one byte early");
}

static void test_describe(void)
{
    printf("-- what a voice's extras say about themselves --\n");

    char buf[256];
    dx7ii_aced_t a;

    /* A voice that uses nothing beyond the DX7 has nothing to report, and the
     * pitch envelope range of 1 is the DX7's own, so it is not news. */
    memset(&a, 0, sizeof a);
    a.raw[A_PEG_RANGE] = 1;
    ok(dx7ii_aced_describe(&a, buf, sizeof buf) == 0,
       "a voice using nothing extra reports nothing (said: \"%s\")", buf);

    memset(&a, 0, sizeof a);
    a.raw[A_PEG_RANGE] = 1;
    a.raw[A_MODE] = 2;            /* unison poly */
    a.raw[A_UNISON] = 4;
    const int n = dx7ii_aced_describe(&a, buf, sizeof buf);
    ok(n == 2, "a unison voice reports two things (got %d)", n);
    ok(strstr(buf, "unison poly") != NULL, "and names the mode (said: \"%s\")", buf);
    ok(strstr(buf, "unison detune") != NULL, "and the detune");

    /* it must not run off the end of a short buffer */
    memset(&a, 0xff, sizeof a);
    for (size_t cap = 1; cap < 40; cap++) {
        char small[64];
        memset(small, 0x7e, sizeof small);
        dx7ii_aced_describe(&a, small, cap);
        int terminated = 0;
        for (size_t i = 0; i < cap; i++) if (small[i] == '\0') { terminated = 1; break; }
        ok(terminated && small[cap] == 0x7e,
           "a %zu byte buffer is filled and not overrun", cap);
    }
}

/*
 * The whole point: a real DX7II bank file is a voice dump followed by an AMEM
 * dump, and hexter has always read the first half and walked over the second
 * without a word. This builds one and checks both halves arrive.
 */
static void test_a_whole_dx7ii_bank(void)
{
    printf("-- a DX7II bank gives up both of its halves --\n");

    const long vmem_dump = 4104;                 /* F0 43 0n 09 20 00, 4096, cs, F7 */
    const long total = vmem_dump + DX7II_AMEM_DUMP;
    uint8_t *file = calloc(1, (size_t)total + 64);

    /* the voices */
    file[0] = 0xf0; file[1] = 0x43; file[2] = 0x00; file[3] = 0x09;
    file[4] = 0x20; file[5] = 0x00;
    for (int i = 0; i < 4096; i++) file[6 + i] = (uint8_t)((i * 7 + 3) & 0x7f);
    file[6 + 4096] = dx7ii_checksum(file + 6, 4096);
    file[7 + 4096] = 0xf7;

    /* and the extras, with voice 5 in unison and voice 9 using random pitch */
    uint8_t *amem = file + vmem_dump;
    amem[0] = 0xf0; amem[1] = 0x43; amem[2] = 0x00; amem[3] = DX7II_FORMAT_AMEM;
    amem[4] = 0x08; amem[5] = 0x60;
    uint8_t *adata = amem + 6;
    for (int v = 0; v < DX7II_AMEM_VOICES; v++) {
        uint8_t *voice = adata + v * DX7II_AMEM_VOICE;
        voice[4] = 0x01;                 /* pitch envelope range 1, the DX7's */
        if (v == 5)  { voice[5] = 0x02;  /* unison poly */  voice[34] = 0x04; }
        if (v == 9)  { voice[4] |= 0x30; }                  /* random pitch 3 */
    }
    amem[6 + DX7II_AMEM_DATA] = dx7ii_checksum(adata, DX7II_AMEM_DATA);
    amem[7 + DX7II_AMEM_DATA] = 0xf7;

    dx7_patch_t patches[64];
    dx7ii_aced_t extras[64];
    int n_extras = -1;
    char *err = NULL;
    memset(extras, 0xee, sizeof extras);

    /*
     * A fresh copy for every parse. dx7_patchbank_parse says in its own header
     * that the buffer is scratch and is modified, and it is: it moves the
     * voices it finds to the front. Handing the same buffer in twice made the
     * second call find no SysEx header, fall through to "assume raw data" and
     * return 5232/128, which is 40. That was this test being wrong, not the
     * loader, and it is worth saying because 40 looks like a parsing bug.
     */
    uint8_t *scratch = malloc((size_t)total + 64);
    memcpy(scratch, file, (size_t)total + 64);
    const int n = dx7_patchbank_parse_dx7ii(scratch, total, "bank.syx", patches, 64,
                                            NULL, extras, &n_extras, &err);
    free(scratch);
    ok(n == 32, "the 32 voices still load (got %d%s%s)", n,
       err ? ", error: " : "", err ? err : "");
    ok(n_extras == 32, "and their extras come with them (got %d)", n_extras);

    char buf[256];
    ok(dx7ii_aced_describe(&extras[0], buf, sizeof buf) == 0,
       "a voice using nothing extra says nothing");
    ok(dx7ii_aced_describe(&extras[5], buf, sizeof buf) == 2,
       "voice 5 reports its unison (said: \"%s\")", buf);
    ok(strstr(buf, "unison") != NULL, "and says so in words");
    ok(dx7ii_aced_describe(&extras[9], buf, sizeof buf) == 1,
       "voice 9 reports its random pitch (said: \"%s\")", buf);

    /* and the same file through the plain entry point still works, since every
     * existing caller goes that way */
    scratch = malloc((size_t)total + 64);
    memcpy(scratch, file, (size_t)total + 64);
    int m = dx7_patchbank_parse(scratch, total, "bank.syx", patches, 64, &err);
    free(scratch);
    ok(m == 32, "the old entry point is unchanged (got %d)", m);

    /* a plain DX7 bank must report no extras at all */
    n_extras = -1;
    scratch = malloc((size_t)total + 64);
    memcpy(scratch, file, (size_t)total + 64);
    int k = dx7_patchbank_parse_dx7ii(scratch, vmem_dump, "bank.syx", patches, 64,
                                      NULL, extras, &n_extras, &err);
    free(scratch);
    ok(k == 32, "a plain DX7 bank still loads (got %d)", k);
    ok(n_extras == 0, "and reports no DX7II extras (got %d)", n_extras);

    free(file);
}

int main(void)
{
    printf("DX7II extra voice data\n\n");
    test_a_whole_dx7ii_bank();
    test_bit_positions();
    test_the_bit_that_cannot_survive();
    test_round_trip();
    test_finding_a_dump();
    test_describe();
    printf("\n%d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
