/* hexter: the FB-01 conversion, in detail.
 *
 * Copyright (C) 2026 Keith Adler. GPL-2.0-or-later.
 *
 * The conversion tables are Sean Bolton's, tuned by ear between an FB-01 and a
 * TX7. These tests pin what they do, parameter by parameter and value by
 * value, so that nobody can quietly turn them back into arithmetic.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "dx7_voice.h"
#include "dx7_voice_fb01.h"

static int checks = 0, failures = 0;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; \
    printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

/* the DX7 operator that FB-01 operator `op` (1-4) becomes, per algorithm */
static const uint8_t routing[8][4] = {
    { 3, 4, 5, 6 }, { 3, 4, 5, 6 }, { 3, 5, 6, 4 }, { 3, 4, 5, 6 },
    { 3, 4, 5, 6 }, { 3, 4, 5, 6 }, { 2, 3, 5, 6 }, { 3, 4, 5, 6 },
};
static const uint8_t dx7_alg[8] = { 1, 14, 8, 7, 5, 22, 29, 32 };

/* what each of an unpacked voice's bytes is allowed to be */
static const uint8_t op_max[21] = {
    99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 3, 3, 7, 3, 7, 99, 1, 31, 99, 14
};
static const uint8_t global_max[29] = {
    99, 99, 99, 99, 99, 99, 99, 99, 31, 7, 1, 99, 99, 99, 99, 1, 5, 7, 48,
    127, 127, 127, 127, 127, 127, 127, 127, 127, 127
};

static int op_base(int op) { return (6 - op) * 21; }   /* op is 1 to 6 */

/* ---- building a voice to convert ---- */

static uint8_t voice[FB01_VOICE_PARAM_LEN];

static void
put(int index, int value)
{
    voice[index * 2]     = (uint8_t)(value & 0x0f);
    voice[index * 2 + 1] = (uint8_t)((value >> 4) & 0x0f);
}

/* the operators are stored backwards, OP4 first */
static int op_param(int op) { return 16 + (4 - op) * 8; }   /* op is 1 to 4 */

static void
put_op(int op, int index, int value)
{
    put(op_param(op) + index, value);
}

static void
blank_voice(void)
{
    int op, i;

    memset(voice, 0, sizeof(voice));
    for (i = 0; i < 7; i++) put(i, ' ');
    put(11, 0x78);          /* all four operators enabled */
    for (op = 1; op <= 4; op++) {
        put_op(op, 0, 0);   /* loudest */
        put_op(op, 3, 1);   /* ratio 1 */
    }
}

static void
convert(uint8_t *out155)
{
    fb01_voice_to_dx7(voice, out155);
}

/* ---- the tables, value by value ---- */

static void
test_envelope_tables(void)
{
    static const uint8_t attack[32] = {
        0x0C,0x0F,0x12,0x16,0x19,0x1C,0x1F,0x22,0x25,0x29,0x2C,0x3F,0x32,0x35,0x38,0x3C,
        0x3F,0x42,0x45,0x48,0x4C,0x4F,0x52,0x55,0x58,0x5B,0x5F,0x60,0x61,0x62,0x62,0x62 };
    static const uint8_t decay1[32] = {
        0x0A,0x0D,0x10,0x13,0x16,0x19,0x1C,0x1F,0x22,0x25,0x28,0x2B,0x2E,0x31,0x34,0x36,
        0x39,0x3C,0x3F,0x42,0x45,0x48,0x4B,0x4E,0x51,0x54,0x57,0x5A,0x5D,0x60,0x61,0x62 };
    static const uint8_t decay2[32] = {
        0x0C,0x0F,0x11,0x14,0x17,0x1A,0x1D,0x20,0x22,0x25,0x28,0x2B,0x2E,0x31,0x34,0x36,
        0x39,0x3C,0x3F,0x42,0x45,0x47,0x4A,0x4D,0x50,0x53,0x56,0x58,0x5B,0x5E,0x61,0x63 };
    static const uint8_t release[16] = {
        0x09,0x0F,0x15,0x1C,0x22,0x28,0x2E,0x35,0x3B,0x41,0x48,0x4E,0x54,0x5B,0x61,0x63 };
    static const uint8_t sustain[16] = {
        0x63,0x5E,0x5A,0x56,0x51,0x4D,0x49,0x45,0x40,0x3C,0x38,0x34,0x2F,0x2B,0x27,0x23 };
    uint8_t out[155];
    int v, wrong;
    int base;

    /* algorithm 1 puts FB-01 OP1 on DX7 OP3 */
    base = op_base(3);

    for (v = 0, wrong = -1; v < 32; v++) {
        blank_voice();
        put_op(1, 4, v);                 /* attack rate */
        convert(out);
        if (out[base + 0] != attack[v] && wrong < 0) wrong = v;
    }
    CHECK(wrong < 0, "every attack rate goes through the tuned table (first wrong at %d)", wrong);

    for (v = 0, wrong = -1; v < 32; v++) {
        blank_voice();
        put_op(1, 5, v);                 /* first decay */
        convert(out);
        if (out[base + 1] != decay1[v] && wrong < 0) wrong = v;
    }
    CHECK(wrong < 0, "every first decay rate does too (first wrong at %d)", wrong);

    for (v = 0, wrong = -1; v < 32; v++) {
        blank_voice();
        put_op(1, 6, v);                 /* second decay */
        convert(out);
        if (out[base + 2] != decay2[v] && wrong < 0) wrong = v;
    }
    CHECK(wrong < 0, "and every second decay rate (first wrong at %d)", wrong);

    for (v = 0, wrong = -1; v < 16; v++) {
        blank_voice();
        put_op(1, 7, v);                 /* release, low nibble */
        convert(out);
        if (out[base + 3] != release[v] && wrong < 0) wrong = v;
    }
    CHECK(wrong < 0, "every release rate (first wrong at %d)", wrong);

    for (v = 0, wrong = -1; v < 16; v++) {
        blank_voice();
        put_op(1, 7, v << 4);            /* sustain, high nibble */
        convert(out);
        if (out[base + 5] != sustain[v] && wrong < 0) wrong = v;
    }
    CHECK(wrong < 0, "every sustain level (first wrong at %d)", wrong);

    /* the two ends of the sustain table are the interesting ones */
    blank_voice(); put_op(1, 7, 0 << 4); convert(out);
    CHECK(out[base + 5] == 99, "sustain wide open is 99 (%d)", out[base + 5]);
    blank_voice(); put_op(1, 7, 15 << 4); convert(out);
    CHECK(out[base + 5] == 35, "sustain wound down stops at 35, not silence (%d)", out[base + 5]);

    /* the fastest attack the FB-01 has is not the fastest the DX7 has */
    blank_voice(); put_op(1, 4, 31); convert(out);
    CHECK(out[base + 0] == 98, "the fastest attack is 98, not 99 (%d)", out[base + 0]);
}

static void
test_level_and_detune(void)
{
    uint8_t out[155];
    int base = op_base(3), v, wrong;

    /* level is attenuation subtracted from 99, not rescaled from 127 */
    blank_voice(); put_op(1, 0, 0);   convert(out);
    CHECK(out[base + 16] == 99, "attenuation 0 is full level (%d)", out[base + 16]);
    blank_voice(); put_op(1, 0, 50);  convert(out);
    CHECK(out[base + 16] == 49, "attenuation 50 is level 49 (%d)", out[base + 16]);
    blank_voice(); put_op(1, 0, 99);  convert(out);
    CHECK(out[base + 16] == 0, "attenuation 99 is silence (%d)", out[base + 16]);
    blank_voice(); put_op(1, 0, 100); convert(out);
    CHECK(out[base + 16] == 0, "and past 99 stays silent rather than wrapping (%d)", out[base + 16]);
    blank_voice(); put_op(1, 0, 127); convert(out);
    CHECK(out[base + 16] == 0, "including the very top (%d)", out[base + 16]);

    /* DT1: sign and magnitude around the DX7's 7 */
    {
        static const uint8_t want[8] = { 7, 8, 9, 10, 7, 6, 5, 4 };
        for (v = 0, wrong = -1; v < 8; v++) {
            blank_voice();
            put_op(1, 3, (v << 4) | 1);
            convert(out);
            if (out[base + 20] != want[v] && wrong < 0) wrong = v;
        }
        CHECK(wrong < 0, "DT1 is read as sign and magnitude (first wrong at %d)", wrong);
        CHECK(want[0] == want[4], "and 0 and 4 both mean no detune");
    }

    /* DT2 becomes a frequency fine value */
    {
        static const uint8_t want[4] = { 0, 41, 57, 73 };
        for (v = 0, wrong = -1; v < 4; v++) {
            blank_voice();
            put_op(1, 6, v << 6);
            convert(out);
            if (out[base + 19] != want[v] && wrong < 0) wrong = v;
        }
        CHECK(wrong < 0, "DT2 becomes the DX7's frequency fine (first wrong at %d)", wrong);
    }

    /* the frequency ratio carries straight across */
    for (v = 0, wrong = -1; v < 16; v++) {
        blank_voice();
        put_op(1, 3, v);
        convert(out);
        if (out[base + 18] != v && wrong < 0) wrong = v;
    }
    CHECK(wrong < 0, "every frequency ratio carries across (first wrong at %d)", wrong);

    /* velocity sensitivity, all eight */
    for (v = 0, wrong = -1; v < 8; v++) {
        blank_voice();
        put_op(1, 1, v << 4);
        convert(out);
        if (out[base + 15] != v && wrong < 0) wrong = v;
    }
    CHECK(wrong < 0, "every velocity sensitivity (first wrong at %d)", wrong);
}

static void
test_algorithms(void)
{
    uint8_t out[155];
    int alg, i;

    for (alg = 0; alg < 8; alg++) {
        int right = 1, silent = 1;
        int used[7];

        blank_voice();
        put(12, alg);
        /* a distinct level per operator, so the routing is visible */
        for (i = 1; i <= 4; i++) put_op(i, 0, (i - 1) * 20);
        convert(out);

        if (out[134] != dx7_alg[alg] - 1) right = 0;
        CHECK(out[134] == dx7_alg[alg] - 1,
              "four-operator algorithm %d becomes DX7 %d (got %d)",
              alg + 1, dx7_alg[alg], out[134] + 1);

        memset(used, 0, sizeof(used));
        for (i = 1; i <= 4; i++) {
            int target = routing[alg][i - 1];
            int want = 99 - (i - 1) * 20;
            used[target] = 1;
            if (out[op_base(target) + 16] != want) right = 0;
        }
        CHECK(right, "algorithm %d puts its four operators where they belong", alg + 1);

        /* whichever two are left over have to be silent */
        for (i = 1; i <= 6; i++)
            if (!used[i] && out[op_base(i) + 16] != 0) silent = 0;
        CHECK(silent, "algorithm %d leaves its spare operators silent", alg + 1);
    }
}

static void
test_enable_bits(void)
{
    uint8_t out[155];
    int op, i;

    /* each operator in turn, switched off on its own */
    for (op = 1; op <= 4; op++) {
        int others = 1;
        blank_voice();
        put(11, 0x78 & ~(1 << (7 - op)));
        convert(out);
        CHECK(out[op_base(routing[0][op - 1]) + 16] == 0,
              "OP%d switched off is silent", op);
        for (i = 1; i <= 4; i++)
            if (i != op && out[op_base(routing[0][i - 1]) + 16] == 0) others = 0;
        CHECK(others, "and switching OP%d off leaves the other three alone", op);
    }

    /* all four off is a silent voice */
    blank_voice();
    put(11, 0x00);
    convert(out);
    {
        int any = 0;
        for (i = 1; i <= 6; i++) if (out[op_base(i) + 16]) any = 1;
        CHECK(!any, "a voice with every operator switched off is silent");
    }
}

static void
test_lfo_and_voice(void)
{
    uint8_t out[155];
    int v, wrong;

    /* speed: a whole byte, and the FB-01's fastest is far slower than the DX7's */
    blank_voice(); put(8, 0);   convert(out);
    CHECK(out[137] == 0, "LFO speed 0 is 0 (%d)", out[137]);
    blank_voice(); put(8, 127); convert(out);
    CHECK(out[137] == 21, "LFO speed 127 is 21 (%d)", out[137]);
    blank_voice(); put(8, 229); convert(out);
    CHECK(out[137] == 38, "LFO speed 229 is 38 (%d)", out[137]);
    blank_voice(); put(8, 255); convert(out);
    CHECK(out[137] <= 99 && out[137] >= 38,
          "and the very top stays in range (%d)", out[137]);

    /* the four waveforms */
    {
        static const uint8_t want[4] = { 2, 3, 0, 5 };   /* saw, square, triangle, S&H */
        for (v = 0, wrong = -1; v < 4; v++) {
            blank_voice();
            put(14, v << 5);
            convert(out);
            if (out[142] != want[v] && wrong < 0) wrong = v;
        }
        CHECK(wrong < 0, "each LFO waveform maps across (first wrong at %d)", wrong);
    }

    /* key sync means what it says */
    blank_voice(); put(10, 0x00); convert(out);
    CHECK(out[141] == 0, "the sync bit clear is not synchronized (%d)", out[141]);
    blank_voice(); put(10, 0x80); convert(out);
    CHECK(out[141] == 1, "and set is synchronized (%d)", out[141]);

    /* pitch and amplitude modulation depth */
    blank_voice(); put(10, 127); convert(out);
    CHECK(out[139] == 15, "pitch mod depth 127 becomes 15 (%d)", out[139]);
    blank_voice(); put(9, 127);  convert(out);
    CHECK(out[140] == 63, "amplitude mod depth 127 becomes 63 (%d)", out[140]);

    /* sensitivities, carried rather than halved */
    for (v = 0, wrong = -1; v < 8; v++) {
        blank_voice();
        put(13, v << 4);
        convert(out);
        if (out[143] != v && wrong < 0) wrong = v;
    }
    CHECK(wrong < 0, "pitch mod sensitivity carries across unhalved (first wrong at %d)", wrong);

    /* feedback */
    for (v = 0, wrong = -1; v < 8; v++) {
        blank_voice();
        put(12, v << 3);
        convert(out);
        if (out[135] != v && wrong < 0) wrong = v;
    }
    CHECK(wrong < 0, "every feedback value (first wrong at %d)", wrong);

    /* transpose is two's complement around the DX7's 24 */
    blank_voice(); put(15, 0);    convert(out);
    CHECK(out[144] == 24, "no transpose is the DX7's 24 (%d)", out[144]);
    blank_voice(); put(15, 12);   convert(out);
    CHECK(out[144] == 36, "an octave up is 36 (%d)", out[144]);
    blank_voice(); put(15, 0xf4); convert(out);
    CHECK(out[144] == 12, "an octave down is 12 (%d)", out[144]);
    blank_voice(); put(15, 0x80); convert(out);
    CHECK(out[144] <= 48, "and the far end clamps rather than wrapping (%d)", out[144]);
    blank_voice(); put(15, 100);  convert(out);
    CHECK(out[144] <= 48, "as does the other far end (%d)", out[144]);
}

static void
test_name(void)
{
    uint8_t out[155];
    int i;

    blank_voice();
    for (i = 0; i < 7; i++) put(i, "FB-01!?"[i]);
    convert(out);
    CHECK(!memcmp(out + 145, "FB-01!?   ", 10),
          "a seven-character name is padded to ten ('%.10s')", out + 145);

    /* anything unprintable becomes a space rather than going into the name */
    blank_voice();
    put(0, 0x01); put(1, 0x1f); put(2, 'A');
    convert(out);
    CHECK(out[145] == ' ' && out[146] == ' ' && out[147] == 'A',
          "control characters become spaces ('%.3s')", out + 145);

    /* the high bit is dropped rather than making a name unprintable */
    blank_voice();
    put(0, 'Z' | 0x80);
    convert(out);
    CHECK(out[145] == 'Z', "the high bit is stripped (%d)", out[145]);
}

/*
 * Nothing the conversion produces may be outside what a DX7 voice allows. A
 * table index that ran off the end, or a value that wrapped, would show up
 * here even if no single test above happened to look at it.
 */
static void
test_every_output_is_legal(void)
{
    uint8_t out[155];
    unsigned long seed = 20260921;
    int round, i, bad = -1, badval = 0;

    for (round = 0; round < 4000 && bad < 0; round++) {
        for (i = 0; i < FB01_VOICE_PARAM_LEN; i++) {
            seed = seed * 1103515245 + 12345;
            voice[i] = (uint8_t)((seed >> 16) & 0x0f);
        }
        fb01_voice_to_dx7(voice, out);

        for (i = 0; i < 126 && bad < 0; i++)
            if (out[i] > op_max[i % 21]) { bad = i; badval = out[i]; }
        for (i = 126; i < 155 && bad < 0; i++)
            if (out[i] > global_max[i - 126]) { bad = i; badval = out[i]; }
    }
    CHECK(bad < 0, "4000 random voices all convert to legal DX7 values "
                   "(byte %d was %d)", bad, badval);

    /* and the same for every extreme: all nibbles low, then all high */
    for (round = 0; round < 2; round++) {
        memset(voice, round ? 0x0f : 0x00, sizeof(voice));
        fb01_voice_to_dx7(voice, out);
        bad = -1;
        for (i = 0; i < 126 && bad < 0; i++)
            if (out[i] > op_max[i % 21]) bad = i;
        for (i = 126; i < 155 && bad < 0; i++)
            if (out[i] > global_max[i - 126]) bad = i;
        CHECK(bad < 0, "an all-%s voice converts legally (byte %d)",
              round ? "ones" : "zeroes", bad);
    }
}

int
main(void)
{
    test_envelope_tables();
    test_level_and_detune();
    test_algorithms();
    test_enable_bits();
    test_lfo_and_voice();
    test_name();
    test_every_output_is_legal();

    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
