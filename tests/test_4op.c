/* hexter: the DX21 / DX27 / DX100 / TX81Z conversion, in detail.
 *
 * Copyright (C) 2026 Keith Adler. GPL-2.0-or-later.
 *
 * The FB-01 conversion has Sean Bolton's ear behind it. This one has XDX's
 * reading of the format and nothing else, so it is worth pinning down exactly
 * what it does, value by value, rather than trusting that it looks right.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dx7_voice.h"
#include "dx7_voice_4op.h"

static int checks = 0, failures = 0;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; \
    printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static const uint8_t op_max[21] = {
    99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 3, 3, 7, 3, 7, 99, 1, 31, 99, 14
};
static const uint8_t global_max[29] = {
    99, 99, 99, 99, 99, 99, 99, 99, 31, 7, 1, 99, 99, 99, 99, 1, 5, 7, 48,
    127, 127, 127, 127, 127, 127, 127, 127, 127, 127
};

static int op_base(int op) { return (6 - op) * 21; }

/* the voice memory keeps its operators as OP4, OP2, OP3, OP1 */
static const int op_offset[4] = { 30, 10, 20, 0 };   /* OP1, OP2, OP3, OP4 */

static uint8_t voice[DX_4OP_VOICE_SIZE_PACKED];

static void
blank_voice(void)
{
    int i;
    memset(voice, 0, sizeof(voice));
    for (i = 0; i < 10; i++) voice[57 + i] = ' ';
    voice[46] = 24;                     /* transpose, centred */
    for (i = 0; i < 4; i++)
        voice[op_offset[i] + 8] = 4;    /* a ratio of 1.00 */
}

static uint8_t *op(int n) { return voice + op_offset[n - 1]; }   /* n is 1 to 4 */

static void
convert(uint8_t *out155, uint8_t *waves6)
{
    dx_4op_voice_to_dx7(voice, out155, waves6);
}

static void
test_algorithms(void)
{
    /* XDX's reading: which DX7 algorithm, and where the four operators land */
    static const uint8_t dx7_alg[8]      = { 1, 14, 8, 7, 5, 22, 31, 32 };
    static const uint8_t routing[8][4]   = {
        { 3, 4, 5, 6 }, { 4, 5, 3, 6 }, { 3, 5, 6, 4 }, { 3, 4, 5, 6 },
        { 3, 4, 5, 6 }, { 3, 4, 5, 6 }, { 3, 4, 5, 6 }, { 3, 4, 5, 6 },
    };
    uint8_t out[155];
    int alg, i;

    for (alg = 0; alg < 8; alg++) {
        int placed = 1, silent = 1, used[7];

        blank_voice();
        voice[40] = (uint8_t)alg;
        for (i = 1; i <= 4; i++) op(i)[7] = (uint8_t)(90 - (i - 1) * 20);
        convert(out, NULL);

        CHECK(out[134] == dx7_alg[alg] - 1,
              "four-operator algorithm %d becomes DX7 %d (got %d)",
              alg + 1, dx7_alg[alg], out[134] + 1);

        memset(used, 0, sizeof(used));
        for (i = 1; i <= 4; i++) {
            int target = routing[alg][i - 1];
            used[target] = 1;
            if (out[op_base(target) + 16] != 90 - (i - 1) * 20) placed = 0;
        }
        CHECK(placed, "algorithm %d puts its four operators where they belong", alg + 1);

        for (i = 1; i <= 6; i++)
            if (!used[i] && out[op_base(i) + 16] != 0) silent = 0;
        CHECK(silent, "algorithm %d leaves its spare operators silent", alg + 1);
    }
}

static void
test_operator_order(void)
{
    uint8_t out[155];
    int i, right = 1;

    /* the four are stored backwards; a distinct level each proves which is which */
    blank_voice();
    voice[40] = 0;                       /* algorithm 1: operators land on 3,4,5,6 */
    for (i = 1; i <= 4; i++) op(i)[7] = (uint8_t)(10 + i * 10);
    convert(out, NULL);
    for (i = 1; i <= 4; i++)
        if (out[op_base(2 + i) + 16] != 10 + i * 10) right = 0;
    CHECK(right, "the operators come out of their reversed storage in order");
}

static void
test_envelope_and_levels(void)
{
    uint8_t out[155];
    int base, v, wrong;

    blank_voice();
    voice[40] = 0;
    base = op_base(3);                   /* OP1 lands on DX7 OP3 */

    /* output level carries straight across, and it is not attenuation here */
    for (v = 0, wrong = -1; v <= 99; v++) {
        blank_voice(); voice[40] = 0;
        op(1)[7] = (uint8_t)v;
        convert(out, NULL);
        if (out[base + 16] != v && wrong < 0) wrong = v;
    }
    CHECK(wrong < 0, "every output level carries across (first wrong at %d)", wrong);

    /* the envelope rates scale to the DX7's 0-99 */
    blank_voice(); voice[40] = 0; op(1)[0] = 31; convert(out, NULL);
    CHECK(out[base + 0] == 99, "the fastest attack becomes 99 (%d)", out[base + 0]);
    blank_voice(); voice[40] = 0; op(1)[0] = 0;  convert(out, NULL);
    CHECK(out[base + 0] == 0, "and the slowest becomes 0 (%d)", out[base + 0]);

    /* sustain: the four-operator machines store it as a level, not attenuation */
    blank_voice(); voice[40] = 0; op(1)[4] = 15; convert(out, NULL);
    CHECK(out[base + 5] == 99, "sustain at its maximum is 99 (%d)", out[base + 5]);
    blank_voice(); voice[40] = 0; op(1)[4] = 0;  convert(out, NULL);
    CHECK(out[base + 5] == 0, "and at its minimum is 0 (%d)", out[base + 5]);

    /* detune is 0-6 around a centre of 3, landing on the DX7's 7 */
    for (v = 0, wrong = -1; v < 7; v++) {
        blank_voice(); voice[40] = 0;
        op(1)[9] = (uint8_t)v;
        convert(out, NULL);
        if (out[base + 20] != v + 4 && wrong < 0) wrong = v;
    }
    CHECK(wrong < 0, "detune is a linear offset here, unlike the FB-01's "
                     "(first wrong at %d)", wrong);
}

static void
test_frequency_table(void)
{
    uint8_t out[155];
    int base = op_base(3), v, wrong, distinct = 0, seen[64];
    uint8_t coarse[64], fine[64];

    /* all 64 ratios convert, and the table is not accidentally flat */
    for (v = 0, wrong = -1; v < 64; v++) {
        blank_voice(); voice[40] = 0;
        op(1)[8] = (uint8_t)v;
        convert(out, NULL);
        coarse[v] = out[base + 18];
        fine[v]   = out[base + 19];
        if (coarse[v] > 31 || fine[v] > 99) wrong = v;
    }
    CHECK(wrong < 0, "all 64 frequency ratios stay in range (first wrong at %d)", wrong);

    memset(seen, 0, sizeof(seen));
    for (v = 0; v < 64; v++) {
        int k, dup = 0;
        for (k = 0; k < v; k++)
            if (coarse[k] == coarse[v] && fine[k] == fine[v]) dup = 1;
        if (!dup) distinct++;
    }
    CHECK(distinct >= 60, "and they are nearly all different ratios (%d of 64)", distinct);

    /* ratio 1.00 is the fourth entry and has to be exactly coarse 1, fine 0 */
    blank_voice(); voice[40] = 0; op(1)[8] = 4; convert(out, NULL);
    CHECK(out[base + 18] == 1 && out[base + 19] == 0,
          "ratio 1.00 is coarse %d fine %d", out[base + 18], out[base + 19]);
}

static void
test_waveforms(void)
{
    static const int wave_byte[4] = { 80, 76, 78, 74 };   /* OP1, OP2, OP3, OP4 */
    uint8_t out[155], waves[6];
    int i, v, wrong;

    /* a TX81Z voice carries a waveform per operator; a DX21 voice has zeroes */
    for (v = 0, wrong = -1; v < 8; v++) {
        blank_voice();
        voice[40] = 0;                   /* algorithm 1: OP1 to OP4 land on 3,4,5,6 */
        for (i = 0; i < 4; i++) voice[wave_byte[i]] = (uint8_t)(v << 4);
        convert(out, waves);
        for (i = 0; i < 4; i++)
            if (waves[2 + i] != v && wrong < 0) wrong = v;
    }
    CHECK(wrong < 0, "every operator waveform comes across (first wrong at %d)", wrong);

    /* each operator's waveform follows it to wherever the algorithm sends it */
    blank_voice();
    voice[40] = 2;                       /* algorithm 3: OP1..OP4 to DX7 3, 5, 6, 4 */
    voice[wave_byte[0]] = 1 << 4;
    voice[wave_byte[1]] = 3 << 4;
    voice[wave_byte[2]] = 5 << 4;
    voice[wave_byte[3]] = 7 << 4;
    convert(out, waves);
    CHECK(waves[2] == 1 && waves[4] == 3 && waves[5] == 5 && waves[3] == 7,
          "a waveform follows its operator (%d %d %d %d)",
          waves[2], waves[4], waves[5], waves[3]);
    CHECK(waves[0] == 0 && waves[1] == 0, "and the spare operators stay sines");

    /* only three bits of that byte are the waveform */
    blank_voice();
    voice[40] = 0;
    voice[wave_byte[0]] = 0xff;
    convert(out, waves);
    CHECK(waves[2] == 7, "only three bits of the byte are the waveform (%d)", waves[2]);

    /* passing no array at all is allowed */
    blank_voice();
    convert(out, NULL);
    CHECK(1, "converting without asking for waveforms does not crash");
}

static void
test_voice_level(void)
{
    uint8_t out[155];
    int v, wrong;

    /* feedback */
    for (v = 0, wrong = -1; v < 8; v++) {
        blank_voice();
        voice[40] = (uint8_t)(v << 3);
        convert(out, NULL);
        if (out[135] != v && wrong < 0) wrong = v;
    }
    CHECK(wrong < 0, "every feedback value (first wrong at %d)", wrong);

    /* the four LFO waveforms */
    {
        static const uint8_t want[4] = { 2, 3, 0, 5 };
        for (v = 0, wrong = -1; v < 4; v++) {
            blank_voice();
            voice[45] = (uint8_t)v;
            convert(out, NULL);
            if (out[142] != want[v] && wrong < 0) wrong = v;
        }
        CHECK(wrong < 0, "each LFO waveform maps across (first wrong at %d)", wrong);
    }

    /* transpose, and its clamp */
    blank_voice(); voice[46] = 24; convert(out, NULL);
    CHECK(out[144] == 24, "centred transpose stays centred (%d)", out[144]);
    blank_voice(); voice[46] = 48; convert(out, NULL);
    CHECK(out[144] == 48, "the top of the range survives (%d)", out[144]);
    blank_voice(); voice[46] = 200; convert(out, NULL);
    CHECK(out[144] <= 48, "and past it clamps (%d)", out[144]);

    /* the name, ten characters of it */
    blank_voice();
    memcpy(voice + 57, "FOUR OP XY", 10);
    convert(out, NULL);
    CHECK(!memcmp(out + 145, "FOUR OP XY", 10), "the name comes across ('%.10s')", out + 145);

    blank_voice();
    voice[57] = 0x02; voice[58] = 0x7f; voice[59] = 'Q';
    convert(out, NULL);
    CHECK(out[145] == ' ' && out[147] == 'Q',
          "control characters in a name become spaces ('%.3s')", out + 145);
}

/* every conversion has to land inside what a DX7 voice allows */
static void
test_every_output_is_legal(void)
{
    uint8_t out[155], waves[6];
    unsigned long seed = 4444;
    int round, i, bad = -1, badval = 0;

    for (round = 0; round < 4000 && bad < 0; round++) {
        for (i = 0; i < DX_4OP_VOICE_SIZE_PACKED; i++) {
            seed = seed * 1103515245 + 12345;
            voice[i] = (uint8_t)((seed >> 16) & 0xff);
        }
        dx_4op_voice_to_dx7(voice, out, waves);

        for (i = 0; i < 126 && bad < 0; i++)
            if (out[i] > op_max[i % 21]) { bad = i; badval = out[i]; }
        for (i = 126; i < 155 && bad < 0; i++)
            if (out[i] > global_max[i - 126]) { bad = i; badval = out[i]; }
        for (i = 0; i < 6 && bad < 0; i++)
            if (waves[i] > 7) { bad = 1000 + i; badval = waves[i]; }
    }
    CHECK(bad < 0, "4000 random voices all convert to legal DX7 values "
                   "(byte %d was %d)", bad, badval);

    for (round = 0; round < 2; round++) {
        memset(voice, round ? 0xff : 0x00, sizeof(voice));
        dx_4op_voice_to_dx7(voice, out, waves);
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
    test_algorithms();
    test_operator_order();
    test_envelope_and_levels();
    test_frequency_table();
    test_waveforms();
    test_voice_level();
    test_every_output_is_legal();

    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
