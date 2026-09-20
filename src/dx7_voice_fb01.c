/* hexter DSSI software synthesizer plugin
 *
 * Yamaha FB-01 voices converted to six-operator ones. See dx7_voice_fb01.h for
 * the format, for what this can and cannot carry across, and for where the
 * format was read from and why none of that program's code is here.
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
#include "dx7_voice_fb01.h"

/*
 * The voice parameters, by their index in the 64. Anything not named here the
 * DX7 has no use for.
 */
enum {
    V_NAME = 0,             /* seven characters */
    V_USERCODE = 7,
    V_LFO_SPEED = 8,
    V_LFO_LOAD_AMD,         /* bit 7 LFO load, bits 0-6 amplitude mod depth */
    V_LFO_SYNC_PMD,         /* bit 7 LFO sync, bits 0-6 pitch mod depth */
    V_OP_ENABLE,            /* bits 6,5,4,3 enable OP1, OP2, OP3, OP4 */
    V_ALGORITHM_FEEDBACK,   /* bits 0-2 algorithm, bits 3-5 feedback */
    V_SENSITIVITY,          /* bits 0-1 amplitude mod, bits 4-6 pitch mod */
    V_LFO_WAVE,             /* bits 5-6 */
    V_TRANSPOSE,            /* signed, around zero */
    V_OPERATORS = 16        /* four operators of eight parameters each */
};

/* and the eight parameters of one operator */
enum {
    OP_LEVEL = 0,           /* attenuation: 0 is loudest, 127 silent */
    OP_CURB_VELOCITY,       /* bit 7 curve low bit, bits 4-6 level velocity */
    OP_DEPTH_ADJUST,        /* bits 4-7 level scaling depth, bits 0-3 adjust */
    OP_CURB_FINE_MULTIPLE,  /* bit 7 curve high bit, bits 4-6 detune, bits 0-3 ratio */
    OP_RATE_ATTACK,         /* bits 6-7 rate scaling, bits 0-4 attack */
    OP_MOD_DECAY1,          /* bit 7 modulator, bits 5-6 attack velocity, bits 0-4 decay */
    OP_COARSE_DECAY2,       /* bits 6-7 coarse detune, bits 0-4 second decay */
    OP_SUSTAIN_RELEASE,     /* bits 4-7 sustain, attenuating, bits 0-3 release */
    OP_PARAMS = 8
};

/*
 * The FB-01's coarse detune multiplies the ratio by roughly 1, 1.41, 1.57 and
 * 1.73. The DX7 reaches those with its frequency fine value, which scales the
 * coarse ratio by one hundredth per step.
 */
static const uint8_t coarse_detune_fine[4] = { 0, 41, 57, 73 };

static int
clamp(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

/* one parameter: two bytes, low nibble first */
static int
param(const uint8_t *params128, int index)
{
    return (params128[index * 2] & 0x0f) | ((params128[index * 2 + 1] & 0x0f) << 4);
}

/* the unpacked DX7 voice stores OP6 first, so operator n starts here */
static int
dx7_op_base(int op)          /* op is 1 to 6 */
{
    return (6 - op) * 21;
}

int
fb01_bank_identify(const uint8_t *data, long length)
{
    int i;

    if (length != FB01_BANK_SIZE) return 0;
    if (data[0] != 0xf0 || data[1] != 0x43 || data[2] != 0x75) return 0;
    if (data[4] != 0x00 || data[5] != 0x00) return 0;
    if (data[7] != 0x00 || data[8] != 0x40) return 0;   /* 64 bank bytes follow */
    if (data[FB01_BANK_SIZE - 1] != 0xf7) return 0;

    /* every voice announces its own 128 bytes, which is a strong check that
     * this is the message it claims to be and not something the same length */
    for (i = 0; i < FB01_BANK_VOICES; i++) {
        const uint8_t *v = data + FB01_VOICE_OFFSET + i * FB01_VOICE_STRIDE;
        if (v[0] != 0x01 || v[1] != 0x00) return 0;
    }
    return 1;
}

void
fb01_voice_to_dx7(const uint8_t *params128, uint8_t *unpacked155)
{
    int i, alg, op, enabled, pms, ams, transpose;

    memset(unpacked155, 0, DX7_VOICE_SIZE_UNPACKED);

    alg = param(params128, V_ALGORITHM_FEEDBACK) & 0x07;
    enabled = param(params128, V_OP_ENABLE);
    ams = param(params128, V_SENSITIVITY) & 0x03;
    pms = (param(params128, V_SENSITIVITY) >> 4) & 0x07;

    /* Silence every operator first; the four that carry the sound are filled
     * in below and the other two stay quiet. */
    for (op = 1; op <= 6; op++) {
        uint8_t *o = unpacked155 + dx7_op_base(op);
        o[8]  = 39;             /* break point, the value that scales nothing */
        o[18] = 1;              /* a sane ratio for a silent operator */
    }

    for (i = 0; i < 4; i++) {          /* FB-01 OP1 through OP4 */
        /* the four operators are stored backwards, OP4 first */
        const uint8_t *s = params128 + (V_OPERATORS + (3 - i) * OP_PARAMS) * 2;
        uint8_t *o = unpacked155 + dx7_op_base(dx_4op_algorithm_map[alg].dx7_op[i]);
        int level    = param(s, OP_LEVEL);
        int curb     = ((param(s, OP_CURB_VELOCITY) >> 7) & 0x01) |
                       ((param(s, OP_CURB_FINE_MULTIPLE) >> 6) & 0x02);
        int depth    = (param(s, OP_DEPTH_ADJUST) >> 4) & 0x0f;
        int fine     = (param(s, OP_CURB_FINE_MULTIPLE) >> 4) & 0x07;
        int multiple = param(s, OP_CURB_FINE_MULTIPLE) & 0x0f;
        int coarse   = (param(s, OP_COARSE_DECAY2) >> 6) & 0x03;
        int sustain  = (param(s, OP_SUSTAIN_RELEASE) >> 4) & 0x0f;
        int detune;

        /* envelope: the FB-01 has one decay stage fewer than the DX7, so its
         * sustain is held through the third stage and the release falls away */
        o[0] = clamp((param(s, OP_RATE_ATTACK) & 0x1f) * 99 / 31, 0, 99);
        o[1] = clamp((param(s, OP_MOD_DECAY1) & 0x1f) * 99 / 31, 0, 99);
        o[2] = clamp((param(s, OP_COARSE_DECAY2) & 0x1f) * 99 / 31, 0, 99);
        o[3] = clamp((param(s, OP_SUSTAIN_RELEASE) & 0x0f) * 99 / 15, 0, 99);
        o[4] = 99;
        /* sustain is stored as attenuation, so 0 is the loudest */
        o[5] = clamp((15 - sustain) * 99 / 15, 0, 99);
        o[6] = 0;
        o[7] = 0;

        /* keyboard level scaling: one depth either side, with the curve the
         * FB-01 chose. A depth of zero leaves the break point doing nothing. */
        o[8]  = 39;
        o[9]  = clamp(depth * 99 / 15, 0, 99);
        o[10] = o[9];
        o[11] = clamp(curb, 0, 3);
        o[12] = o[11];

        o[13] = clamp(((param(s, OP_RATE_ATTACK) >> 6) & 0x03) * 7 / 3, 0, 7);
        o[14] = clamp(ams, 0, 3);
        o[15] = clamp((param(s, OP_CURB_VELOCITY) >> 4) & 0x07, 0, 7);

        /* Level is attenuation too: 0 is loudest and 127 is silent, the
         * opposite of the DX7's output level. Getting this backwards would
         * turn every patch inside out. */
        o[16] = clamp((127 - clamp(level, 0, 127)) * 99 / 127, 0, 99);
        if (!((enabled >> (6 - i)) & 0x01)) o[16] = 0;   /* the FB-01 switched it off */

        o[17] = 0;                                       /* ratio, not fixed */
        o[18] = clamp(multiple, 0, 31);
        o[19] = coarse_detune_fine[coarse];

        /* detune is 0 and 4 for none, 1-3 one way and 5-7 the other; the DX7
         * centers the same idea on 7 */
        detune = (fine < 4) ? fine : -(fine - 4);
        o[20] = clamp(7 + detune, 0, 14);
    }

    /* the FB-01 has no pitch envelope, so leave a flat one */
    unpacked155[126] = unpacked155[127] = unpacked155[128] = unpacked155[129] = 99;
    unpacked155[130] = unpacked155[131] = unpacked155[132] = unpacked155[133] = 50;

    unpacked155[134] = clamp(dx_4op_algorithm_map[alg].dx7_algorithm - 1, 0, 31);
    unpacked155[135] = clamp((param(params128, V_ALGORITHM_FEEDBACK) >> 3) & 0x07, 0, 7);
    unpacked155[136] = 1;                                /* key sync on */

    unpacked155[137] = clamp(param(params128, V_LFO_SPEED) * 99 / 127, 0, 99);
    unpacked155[138] = 0;                                /* no LFO delay to carry */
    unpacked155[139] = clamp((param(params128, V_LFO_SYNC_PMD) & 0x7f) * 99 / 127, 0, 99);
    unpacked155[140] = clamp((param(params128, V_LFO_LOAD_AMD) & 0x7f) * 99 / 127, 0, 99);
    /* the sync bit reads the other way round: zero means synchronized */
    unpacked155[141] = ((param(params128, V_LFO_SYNC_PMD) >> 7) & 0x01) ? 0 : 1;
    unpacked155[142] = dx_4op_lfo_wave_map[(param(params128, V_LFO_WAVE) >> 5) & 0x03];
    /* pitch modulation reaches further on the DX7 for the same number, so
     * halve it rather than let converted voices wobble twice as hard */
    unpacked155[143] = clamp(pms / 2, 0, 7);

    /* transpose is signed around zero; the DX7 centers it at 24 */
    transpose = param(params128, V_TRANSPOSE);
    if (transpose > 127) transpose -= 256;
    unpacked155[144] = (uint8_t)clamp(24 + transpose, 0, 48);

    /* the name is seven characters where the DX7 has ten */
    for (i = 0; i < 10; i++) {
        int c = (i < 7) ? (param(params128, V_NAME + i) & 0x7f) : ' ';
        unpacked155[145 + i] = (uint8_t)((c >= 32) ? c : ' ');
    }
}
