/* hexter DSSI software synthesizer plugin
 *
 * Yamaha FB-01 voices converted to six-operator ones. See dx7_voice_fb01.h for
 * the format and for what this can and cannot carry across.
 *
 * The conversion tables below are Sean Bolton's, from FB_TX_Conv.c, which he
 * wrote in 1986 and placed in the public domain. They are not arithmetic: he
 * sat between an FB-01 and a TX7 and tuned them by ear, which is why the
 * envelope rates, the output level and the sustain level are lookup tables
 * with no formula behind them. The banks that ship with hexter as
 * fb01_roms_converted_*.dx7 were made with this code.
 *
 * This file used to derive those values from the format instead, and it was
 * wrong in ways you could hear: the LFO ran three times too fast, the pitch
 * modulation was halved for no reason, the key sync bit was inverted, and the
 * envelope rates were linear where the hardware's are not.
 *
 * Copyright (C) 1986 Sean Bolton, for the conversion tables and mapping.
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

/*
 * The eight parameters of one operator, named as the manual's Operator Block
 * table names them. TL and SL are attenuation, the way this chip counts: zero
 * is loudest.
 */
enum {
    OP_LEVEL = 0,           /* TL, attenuation: 0 is loudest, 127 silent */
    OP_CURB_VELOCITY,       /* bit 7 scaling type bit 0, bits 4-6 velocity for TL */
    OP_DEPTH_ADJUST,        /* bits 4-7 level scaling depth, bits 0-3 adjust for TL */
    OP_CURB_FINE_MULTIPLE,  /* bit 7 scaling type bit 1, bits 4-6 DT1, bits 0-3 multiple */
    OP_RATE_ATTACK,         /* bits 6-7 rate scaling depth, bits 0-4 AR */
    OP_MOD_DECAY1,          /* bit 7 carrier, bits 5-6 velocity for AR, bits 0-4 D1R */
    OP_COARSE_DECAY2,       /* bits 6-7 DT2, bits 0-4 D2R */
    OP_SUSTAIN_RELEASE,     /* bits 4-7 SL, attenuating, bits 0-3 RR */
    OP_PARAMS = 8
};

/*
 * Sean Bolton's conversion tables, tuned by ear between an FB-01 and a TX7.
 * Public domain, from FB_TX_Conv.c.
 *
 * The first four are envelope rates and the sustain level. The FB-01's rates
 * and the DX7's do not run at the same speeds for the same numbers, and the
 * relationship is not a straight line, which is what these encode. The sustain
 * table stops at 35 rather than falling to zero, so a patch with its sustain
 * wound down is quiet rather than silent.
 */
static const uint8_t t_attack[32] = {
    0x0C, 0x0F, 0x12, 0x16, 0x19, 0x1C, 0x1F, 0x22,
    0x25, 0x29, 0x2C, 0x3F, 0x32, 0x35, 0x38, 0x3C,
    0x3F, 0x42, 0x45, 0x48, 0x4C, 0x4F, 0x52, 0x55,
    0x58, 0x5B, 0x5F, 0x60, 0x61, 0x62, 0x62, 0x62,
};
static const uint8_t t_decay1[32] = {
    0x0A, 0x0D, 0x10, 0x13, 0x16, 0x19, 0x1C, 0x1F,
    0x22, 0x25, 0x28, 0x2B, 0x2E, 0x31, 0x34, 0x36,
    0x39, 0x3C, 0x3F, 0x42, 0x45, 0x48, 0x4B, 0x4E,
    0x51, 0x54, 0x57, 0x5A, 0x5D, 0x60, 0x61, 0x62,
};
static const uint8_t t_decay2[32] = {
    0x0C, 0x0F, 0x11, 0x14, 0x17, 0x1A, 0x1D, 0x20,
    0x22, 0x25, 0x28, 0x2B, 0x2E, 0x31, 0x34, 0x36,
    0x39, 0x3C, 0x3F, 0x42, 0x45, 0x47, 0x4A, 0x4D,
    0x50, 0x53, 0x56, 0x58, 0x5B, 0x5E, 0x61, 0x63,
};
static const uint8_t t_release[16] = {
    0x09, 0x0F, 0x15, 0x1C, 0x22, 0x28, 0x2E, 0x35,
    0x3B, 0x41, 0x48, 0x4E, 0x54, 0x5B, 0x61, 0x63,
};
static const uint8_t t_sustain[16] = {
    0x63, 0x5E, 0x5A, 0x56, 0x51, 0x4D, 0x49, 0x45,
    0x40, 0x3C, 0x38, 0x34, 0x2F, 0x2B, 0x27, 0x23,
};

/* DT1, as sign and magnitude, landing on the DX7's detune centred at 7 */
static const uint8_t t_detune[8] = { 7, 8, 9, 10, 7, 6, 5, 4 };

/* velocity sensitivity, and DT2 as a DX7 frequency fine value */
static const uint8_t t_velocity[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };
static const uint8_t coarse_detune_fine[4] = { 0, 41, 57, 73 };

/* the FB-01's four LFO waveforms in the DX7's numbering */
static const uint8_t t_lfo_wave[4] = { 2, 3, 0, 5 };

/*
 * Which DX7 algorithm stands in for each of the FB-01's eight, and which DX7
 * operator each of its four becomes. Also Sean's, and it disagrees with the
 * table the DX21 family converter uses, which came from XDX: on the FB-01's
 * algorithm 2 the operators land in a different order, and its algorithm 8
 * becomes DX7 29 rather than 31 with two operators moved.
 *
 * Both are kept rather than reconciled. This one was arrived at by listening
 * to an FB-01 and a TX7 side by side, so it is the one to trust for an FB-01;
 * the other is a DX21-family tool's reading of DX21-family machines, and no
 * hardware has been put in front of it to say whether those eight algorithms
 * are wired the same way. Guessing that they are would be changing four other
 * synths on evidence from a fifth.
 */
static const struct {
    uint8_t dx7_algorithm;      /* 1 to 32, as printed on the instrument */
    uint8_t dx7_op[4];          /* for FB-01 OP1, OP2, OP3, OP4 */
} fb01_algorithm_map[8] = {
    {  1, { 3, 4, 5, 6 } },
    { 14, { 3, 4, 5, 6 } },
    {  8, { 3, 5, 6, 4 } },
    {  7, { 3, 4, 5, 6 } },
    {  5, { 3, 4, 5, 6 } },
    { 22, { 3, 4, 5, 6 } },
    { 29, { 2, 3, 5, 6 } },
    { 32, { 3, 4, 5, 6 } },
};

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
fb01_bank_at(const uint8_t *data, long length, long at, int midshift,
             fb01_bank_layout_t *layout)
{
    /* byte k of the message, which is shifted along inside a MIDI file */
    #define B(k) data[at + (k) + midshift]
    long size, voices, params;
    int i;

    if (at < 0 || at + 9 + midshift > length) return 0;
    if (data[at] != 0xf0 || B(1) != 0x43) return 0;   /* the F0 is not shifted */

    if (B(2) == 0x75) {
        /* voice bank x: F0 43 75 0n 00 00 0x */
        if (B(3) > 0x0f) return 0;                    /* system channel */
        if (B(4) != 0x00 || B(5) != 0x00) return 0;
        size   = FB01_BANK_SIZE;
        voices = FB01_VOICE_OFFSET;
        params = 7;
    } else if (B(2) <= 0x0f && B(3) == 0x0c) {
        /* voice bank 0: F0 43 0n 0C */
        size   = FB01_BANK0_SIZE;
        voices = FB01_BANK0_VOICE_OFF;
        params = 4;
    } else {
        return 0;
    }

    if (at + size + midshift > length) return 0;
    if (B(params) != 0x00 || B(params + 1) != 0x40) return 0;  /* 64 bank bytes */
    if (B(size - 1) != 0xf7) return 0;

    /* every voice announces its own 128 bytes, which is a strong check that
     * this is the message it claims to be and not something the same length */
    for (i = 0; i < FB01_BANK_VOICES; i++) {
        long v = voices + (long)i * FB01_VOICE_STRIDE;
        if (B(v) != 0x01 || B(v + 1) != 0x00) return 0;
    }

    if (layout) {
        layout->size = size;
        layout->voice_offset = voices;
    }
    return 1;
    #undef B
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
        uint8_t *o = unpacked155 + dx7_op_base(fb01_algorithm_map[alg].dx7_op[i]);
        int level    = param(s, OP_LEVEL);
        int curb     = ((param(s, OP_CURB_VELOCITY) >> 7) & 0x01) |
                       ((param(s, OP_CURB_FINE_MULTIPLE) >> 6) & 0x02);
        int depth    = (param(s, OP_DEPTH_ADJUST) >> 4) & 0x0f;
        int fine     = (param(s, OP_CURB_FINE_MULTIPLE) >> 4) & 0x07;
        int multiple = param(s, OP_CURB_FINE_MULTIPLE) & 0x0f;
        int coarse   = (param(s, OP_COARSE_DECAY2) >> 6) & 0x03;
        int sustain  = (param(s, OP_SUSTAIN_RELEASE) >> 4) & 0x0f;
        int detune;

        /* envelope, through the tuned tables rather than by arithmetic */
        o[0] = t_attack [param(s, OP_RATE_ATTACK)    & 0x1f];
        o[1] = t_decay1 [param(s, OP_MOD_DECAY1)     & 0x1f];
        o[2] = t_decay2 [param(s, OP_COARSE_DECAY2)  & 0x1f];
        o[3] = t_release[param(s, OP_SUSTAIN_RELEASE) & 0x0f];
        o[4] = 99;
        o[5] = t_sustain[sustain];      /* attenuating, and it floors at 35 */
        o[6] = 0;
        o[7] = 0;

        /*
         * Keyboard level scaling. Sean's converter leaves this alone, which on
         * a DX7 with both depths at zero scales nothing whatever the break
         * point says. The FB-01's own depth is carried across here because
         * throwing it away would lose something the patch asked for, and a
         * depth of zero still means no scaling.
         */
        o[8]  = 39;
        o[9]  = clamp(depth * 99 / 15, 0, 99);
        o[10] = o[9];
        o[11] = clamp(curb, 0, 3);
        o[12] = o[11];

        o[13] = clamp(((param(s, OP_RATE_ATTACK) >> 6) & 0x03) * 2, 0, 7);
        o[14] = clamp(ams, 0, 3);
        o[15] = t_velocity[(param(s, OP_CURB_VELOCITY) >> 4) & 0x07];

        /*
         * Level is attenuation: 0 is loudest, the opposite of the DX7's output
         * level, so it is subtracted from 99 rather than rescaled from a range
         * of 127. Anything above 99 is silent. This is Sean's mapping and it
         * is quieter in the middle than arithmetic would make it.
         */
        o[16] = (level > 99) ? 0 : (uint8_t)(99 - level);
        if (!((enabled >> (6 - i)) & 0x01)) o[16] = 0;   /* the FB-01 switched it off */

        o[17] = 0;                                       /* ratio, not fixed */
        o[18] = clamp(multiple, 0, 31);
        o[19] = coarse_detune_fine[coarse];

        /*
         * DT1, stored as sign and magnitude: 0 and 4 are no detune, 1-3 go one
         * way and 5-7 the other. Sean's table says the same, which is worth
         * noting because editors that show it as a plain 0-7 dial read it as a
         * linear offset instead and get it wrong.
         */
        (void)detune;
        o[20] = t_detune[fine];
    }

    /* the FB-01 has no pitch envelope, so leave a flat one */
    unpacked155[126] = unpacked155[127] = unpacked155[128] = unpacked155[129] = 99;
    unpacked155[130] = unpacked155[131] = unpacked155[132] = unpacked155[133] = 50;

    unpacked155[134] = clamp(fb01_algorithm_map[alg].dx7_algorithm - 1, 0, 31);
    unpacked155[135] = clamp((param(params128, V_ALGORITHM_FEEDBACK) >> 3) & 0x07, 0, 7);
    unpacked155[136] = 1;                                /* key sync on */

    /*
     * The LFO, which this used to get badly wrong. Its speed is a whole byte,
     * 0 to 255, not 0 to 127, and the FB-01's fastest is nowhere near the
     * DX7's, so Sean's scaling tops out around 38 rather than 99. Reading it
     * as a seven-bit value and stretching it to 99 made every converted patch
     * wobble about three times too fast.
     */
    unpacked155[137] = clamp((int)(param(params128, V_LFO_SPEED) * 38.0 / 229.0 + 0.25), 0, 99);
    unpacked155[138] = 0;                                /* no LFO delay to carry */
    unpacked155[139] = clamp((param(params128, V_LFO_SYNC_PMD) & 0x7f) >> 3, 0, 99);
    unpacked155[140] = clamp((param(params128, V_LFO_LOAD_AMD) >> 1) & 0x3f, 0, 99);
    /* the sync bit means what it says: set is synchronized */
    unpacked155[141] = (param(params128, V_LFO_SYNC_PMD) >> 7) & 0x01;
    unpacked155[142] = t_lfo_wave[(param(params128, V_LFO_WAVE) >> 5) & 0x03];
    /* carried as it stands; halving it was a guess, and a wrong one */
    unpacked155[143] = clamp(pms, 0, 7);

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
