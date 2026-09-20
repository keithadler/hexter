/* hexter DSSI software synthesizer plugin
 *
 * Four-operator Yamaha voices converted to six-operator ones. See
 * dx7_voice_4op.h for what this can and cannot carry across, and for the
 * attribution: the voice memory layout and the algorithm mapping come from
 * XDX by Wurly, MIT licensed.
 *
 * Copyright (C) 2026 Keith Adler
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#include <string.h>

#include "dx7_voice.h"
#include "dx7_voice_4op.h"

/*
 * The four-operator voice memory, 128 bytes. The first 40 hold the operators,
 * ten bytes each, in the order the hardware stores them: OP4, OP2, OP3, OP1.
 */
#define OP_STRIDE   10
static const int op_offset[4] = { 30, 10, 20, 0 };   /* OP1, OP2, OP3, OP4 */

enum {                  /* within an operator's ten bytes */
    OP_AR = 0, OP_D1R, OP_D2R, OP_RR, OP_D1L, OP_KSL,
    OP_SENS,            /* bit 6 amp mod enable, bits 5-3 EG bias, bits 2-0 velocity */
    OP_OUT, OP_FREQ,
    OP_KSR_DETUNE       /* bits 4-3 rate scaling, bits 2-0 detune */
};

enum {                  /* the voice's own bytes */
    V_ALG_FB_SYNC = 40, /* bits 2-0 algorithm, bits 5-3 feedback, bits 7-6 LFO sync */
    V_LFO_SPEED, V_LFO_DELAY, V_PMD, V_AMD,
    V_LFO_WAVE_SENS,    /* bits 1-0 wave, bits 3-2 amp mod sens, bits 6-4 pitch mod sens */
    V_TRANSPOSE,
    V_NAME = 57,        /* ten characters */
    V_PEG_RATE = 67,    /* three rates, then three levels */
    V_PEG_LEVEL = 70
};

/* Described in the header, and shared with the FB-01 converter. */
const dx_4op_algorithm_t dx_4op_algorithm_map[8] = {
    {  1, { 3, 4, 5, 6 } },
    { 14, { 4, 5, 3, 6 } },
    {  8, { 3, 5, 6, 4 } },
    {  7, { 3, 4, 5, 6 } },
    {  5, { 3, 4, 5, 6 } },
    { 22, { 3, 4, 5, 6 } },
    { 31, { 3, 4, 5, 6 } },
    { 32, { 3, 4, 5, 6 } },
};

/*
 * The four-operator machines choose a frequency ratio from a fixed list of 64.
 * The DX7 builds its ratio from a coarse and a fine value, so each entry is the
 * closest the DX7 can come.
 */
static const struct { uint8_t coarse, fine; } freq_map[64] = {
    { 0,  0}, { 0, 42}, { 0, 56}, { 0, 74}, { 1,  0}, { 1, 41}, { 1, 57}, { 1, 73},
    { 2,  0}, { 2, 41}, { 3,  0}, { 2, 57}, { 2, 73}, { 4,  0}, { 4,  6}, { 3, 57},
    { 5,  0}, { 3, 73}, { 5, 13}, { 6,  0}, { 4, 57}, { 4, 73}, { 7,  0}, { 7,  1},
    { 5, 57}, { 8,  0}, { 8,  6}, { 5, 73}, { 9,  0}, { 6, 57}, { 7, 41}, {10,  0},
    { 6, 73}, { 7, 57}, {11,  0}, {10, 13}, {12,  0}, { 7, 73}, { 8, 57}, { 8, 59},
    {13,  0}, { 8, 73}, {14,  0}, {10, 41}, { 9, 57}, {15,  0}, {14, 11}, { 9, 73},
    {10, 57}, {16,  6}, {11, 57}, {10, 73}, {11, 67}, {12, 57}, {11, 73}, {18, 10},
    {13, 57}, {12, 73}, {20,  6}, {14, 57}, {13, 73}, {15, 57}, {14, 73}, {15, 73},
};

/* saw, square, triangle, sample and hold, in the DX7's own numbering */
const uint8_t dx_4op_lfo_wave_map[4] = { 2, 3, 0, 5 };

static int
clamp(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

/* the unpacked DX7 voice stores OP6 first, so operator n starts here */
static int
dx7_op_base(int op)          /* op is 1 to 6 */
{
    return (6 - op) * 21;
}

/*
 * A TX81Z packs its extras into the bytes a DX21 leaves empty, and the operator
 * waveform is three bits of one of them. A DX21, DX27 or DX100 voice has zeros
 * there, which reads as waveform 0, the sine, which is what those machines have.
 */
static const int wave_byte[4] = { 80, 76, 78, 74 };   /* OP1, OP2, OP3, OP4 */

void
dx_4op_voice_to_dx7(const uint8_t *packed128, uint8_t *unpacked155,
                    uint8_t *op_wave6)
{
    int i, alg, op;

    memset(unpacked155, 0, DX7_VOICE_SIZE_UNPACKED);
    if (op_wave6) memset(op_wave6, 0, 6);

    alg = packed128[V_ALG_FB_SYNC] & 0x07;

    /* Silence every operator first; the four that carry the sound are filled
     * in below and the other two stay quiet. */
    for (op = 1; op <= 6; op++) {
        uint8_t *o = unpacked155 + dx7_op_base(op);
        o[16] = 0;              /* output level */
        o[8]  = 39;             /* break point, the value that scales nothing */
        o[18] = 1;              /* frequency coarse 1, a sane ratio for a silent operator */
    }

    for (i = 0; i < 4; i++) {          /* four-operator OP1 through OP4 */
        const uint8_t *s = packed128 + op_offset[i];
        uint8_t *o = unpacked155 + dx7_op_base(dx_4op_algorithm_map[alg].dx7_op[i]);
        int freq = s[OP_FREQ] & 0x3f;

        /* envelope: four rates and four levels. The four-operator envelope has
         * one fewer stage, so its sustain level is held through the DX7's
         * third stage and the release falls to nothing. */
        o[0] = clamp(s[OP_AR]  * 99 / 20, 0, 99);
        o[1] = clamp(s[OP_D1R] * 99 / 31, 0, 99);
        o[2] = clamp(s[OP_D2R] * 99 / 31, 0, 99);
        o[3] = clamp(s[OP_RR]  * 99 / 10, 0, 99);
        o[4] = 99;
        o[5] = clamp(s[OP_D1L] * 99 / 15, 0, 99);
        o[6] = 0;
        o[7] = 0;

        o[13] = clamp((s[OP_KSR_DETUNE] >> 3) & 0x03, 0, 7);   /* rate scaling */
        o[14] = clamp((s[OP_SENS] >> 6) & 0x01
                          ? (packed128[V_LFO_WAVE_SENS] >> 2) & 0x03 : 0, 0, 3);
        o[15] = clamp(s[OP_SENS] & 0x07, 0, 7);                /* velocity sensitivity */
        o[16] = clamp(s[OP_OUT], 0, 99);                       /* output level */
        o[17] = 0;                                             /* ratio, not fixed */
        o[18] = freq_map[freq].coarse;
        o[19] = freq_map[freq].fine;
        /* detune is -3..+3 either way, but the DX7 centers it at 7 */
        o[20] = clamp((s[OP_KSR_DETUNE] & 0x07) + 4, 0, 14);

        if (op_wave6) {
            int dx7_op = dx_4op_algorithm_map[alg].dx7_op[i];        /* 1 to 6 */
            op_wave6[dx7_op - 1] = (packed128[wave_byte[i]] >> 4) & 0x07;
        }
    }

    /* pitch envelope: three rates and three levels become four of each */
    unpacked155[126] = clamp(packed128[V_PEG_RATE + 0], 0, 99);
    unpacked155[127] = clamp(packed128[V_PEG_RATE + 1], 0, 99);
    unpacked155[128] = 99;
    unpacked155[129] = clamp(packed128[V_PEG_RATE + 2], 0, 99);
    unpacked155[130] = clamp(packed128[V_PEG_LEVEL + 0], 0, 99);
    unpacked155[131] = clamp(packed128[V_PEG_LEVEL + 1], 0, 99);
    unpacked155[132] = clamp(packed128[V_PEG_LEVEL + 1], 0, 99);
    unpacked155[133] = clamp(packed128[V_PEG_LEVEL + 2], 0, 99);

    unpacked155[134] = clamp(dx_4op_algorithm_map[alg].dx7_algorithm - 1, 0, 31);
    unpacked155[135] = clamp((packed128[V_ALG_FB_SYNC] >> 3) & 0x07, 0, 7);  /* feedback */
    unpacked155[136] = 1;                                                    /* key sync on */

    unpacked155[137] = clamp(packed128[V_LFO_SPEED], 0, 99);
    unpacked155[138] = clamp(packed128[V_LFO_DELAY], 0, 99);
    unpacked155[139] = clamp(packed128[V_PMD], 0, 99);
    unpacked155[140] = clamp(packed128[V_AMD], 0, 99);
    unpacked155[141] = (packed128[V_ALG_FB_SYNC] >> 6) & 0x01;               /* LFO sync */
    unpacked155[142] = dx_4op_lfo_wave_map[packed128[V_LFO_WAVE_SENS] & 0x03];
    /* pitch modulation reaches further on the DX7 for the same number, so
     * halve it rather than let converted voices wobble twice as hard */
    unpacked155[143] = clamp(((packed128[V_LFO_WAVE_SENS] >> 4) & 0x07) / 2, 0, 7);

    unpacked155[144] = clamp(packed128[V_TRANSPOSE], 0, 48);

    for (i = 0; i < 10; i++) {
        uint8_t c = packed128[V_NAME + i] & 0x7f;
        unpacked155[145 + i] = (c >= 32) ? c : ' ';
    }
}
