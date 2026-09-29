/* hexter DSSI software synthesizer plugin
 *
 * The DX7II's extra voice data: ACED unpacked, AMEM packed.
 *
 * Copyright (C) 2026 Keith Adler
 *
 * Written from the DX7II specification documented at
 * https://github.com/probonopd/dx-specs. See dx7_voice_dx7ii.h.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#include <string.h>
#include <stdio.h>

#include "dx7_voice_dx7ii.h"

/*
 * ACED offsets. Named rather than written as numbers at the point of use,
 * because the packed and the unpacked layouts disagree about where things live
 * and a bare 17 in two different functions is how they end up disagreeing
 * silently.
 */
enum {
    A_SCALING_MODE = 0,    /* 6 bytes, OP6 first */
    A_AMS          = 6,    /* 6 bytes, OP6 first */
    A_PEG_RANGE    = 12,
    A_LFO_KEY_TRIG = 13,
    A_PEG_VEL      = 14,
    A_RANDOM_PITCH = 15,
    A_VOICE_MODE   = 16,
    A_PB_RANGE     = 17,
    A_PB_STEP      = 18,
    A_PB_MODE      = 19,
    A_PORTA_MODE   = 20,
    A_PORTA_STEP   = 21,
    A_PORTA_TIME   = 22,
    A_MW           = 23,   /* pitch, amplitude, EG bias */
    A_FC1          = 26,   /* pitch, amplitude, EG bias, volume */
    A_BC           = 30,   /* pitch, amplitude, EG bias, pitch bias */
    A_AT           = 34,   /* pitch, amplitude, EG bias, pitch bias */
    A_PEG_RATE_SCL = 38,
    /* 39 to 63 are reserved and are written as zero */
    A_FC2          = 64,   /* pitch, amplitude, EG bias, volume */
    A_MIDI_CTL     = 68,   /* pitch, amplitude, EG bias, volume */
    A_UNISON_DET   = 72
};

/* AMEM byte offsets. */
enum {
    M_SCALING      = 0,
    M_AMS_65       = 1,
    M_AMS_43       = 2,
    M_AMS_21       = 3,
    M_PEG_LFO_RND  = 4,
    M_MODE_PBRANGE = 5,
    M_PB_STEP_MODE = 6,
    M_PORTA        = 7,
    M_PORTA_TIME   = 8,
    M_MW           = 9,
    M_FC1          = 12,
    M_BC           = 16,
    M_AT           = 20,
    M_PEG_RATE_SCL = 24,
    M_RESERVED     = 25,
    M_FC2          = 26,
    M_MIDI_CTL     = 30,
    M_UNISON       = 34
};

uint8_t dx7ii_checksum(const uint8_t *data, size_t len)
{
    unsigned sum = 0;
    for (size_t i = 0; i < len; i++) sum += data[i];
    return (uint8_t)((~sum + 1) & 0x7f);
}

void dx7ii_amem_unpack(const uint8_t *p, dx7ii_aced_t *out)
{
    uint8_t *a = out->raw;
    memset(a, 0, DX7II_ACED_SIZE);

    /* Byte 0: one bit of scaling mode per operator, OP6 in bit 0. */
    for (int op = 0; op < 6; op++)
        a[A_SCALING_MODE + op] = (uint8_t)((p[M_SCALING] >> op) & 0x01);

    /*
     * Bytes 1 to 3: two operators to a byte, three bits each, the lower
     * numbered operator of the pair in the low bits. OP6 and OP5 share byte 1,
     * so OP6 is bits 2:0 and OP5 is bits 5:3, and so on down to OP1.
     */
    a[A_AMS + 0] = (uint8_t)( p[M_AMS_65]       & 0x07);   /* OP6 */
    a[A_AMS + 1] = (uint8_t)((p[M_AMS_65] >> 3) & 0x07);   /* OP5 */
    a[A_AMS + 2] = (uint8_t)( p[M_AMS_43]       & 0x07);   /* OP4 */
    a[A_AMS + 3] = (uint8_t)((p[M_AMS_43] >> 3) & 0x07);   /* OP3 */
    a[A_AMS + 4] = (uint8_t)( p[M_AMS_21]       & 0x07);   /* OP2 */
    a[A_AMS + 5] = (uint8_t)((p[M_AMS_21] >> 3) & 0x07);   /* OP1 */

    a[A_PEG_RANGE]    = (uint8_t)( p[M_PEG_LFO_RND]       & 0x03);
    a[A_LFO_KEY_TRIG] = (uint8_t)((p[M_PEG_LFO_RND] >> 2) & 0x01);
    /*
     * Pitch envelope velocity sensitivity is three bits in ACED and one bit in
     * AMEM, so a packed bank cannot say "4". Anything that was not zero was
     * stored as one and comes back as the top of the range rather than as a 1,
     * which would read as "almost none" and is the opposite of what the bank
     * meant. The information is gone either way; this loses it in the
     * direction that is audible rather than the direction that is silent.
     */
    a[A_PEG_VEL]      = (uint8_t)(((p[M_PEG_LFO_RND] >> 3) & 0x01) ? 7 : 0);
    a[A_RANDOM_PITCH] = (uint8_t)((p[M_PEG_LFO_RND] >> 4) & 0x07);

    a[A_VOICE_MODE] = (uint8_t)( p[M_MODE_PBRANGE]       & 0x03);
    a[A_PB_RANGE]   = (uint8_t)((p[M_MODE_PBRANGE] >> 2) & 0x0f);
    a[A_PB_STEP]    = (uint8_t)( p[M_PB_STEP_MODE]       & 0x0f);
    a[A_PB_MODE]    = (uint8_t)((p[M_PB_STEP_MODE] >> 4) & 0x03);

    a[A_PORTA_MODE] = (uint8_t)( p[M_PORTA]       & 0x01);
    a[A_PORTA_STEP] = (uint8_t)((p[M_PORTA] >> 1) & 0x0f);
    a[A_PORTA_TIME] = (uint8_t)( p[M_PORTA_TIME]  & 0x7f);

    memcpy(a + A_MW,       p + M_MW,       3);
    memcpy(a + A_FC1,      p + M_FC1,      4);
    memcpy(a + A_BC,       p + M_BC,       4);
    memcpy(a + A_AT,       p + M_AT,       4);
    memcpy(a + A_FC2,      p + M_FC2,      4);
    memcpy(a + A_MIDI_CTL, p + M_MIDI_CTL, 4);

    a[A_PEG_RATE_SCL] = (uint8_t)(p[M_PEG_RATE_SCL] & 0x07);
    a[A_UNISON_DET]   = (uint8_t)(p[M_UNISON]       & 0x07);
}

void dx7ii_aced_pack(const dx7ii_aced_t *in, uint8_t *p)
{
    const uint8_t *a = in->raw;
    memset(p, 0, DX7II_AMEM_VOICE);

    for (int op = 0; op < 6; op++)
        if (a[A_SCALING_MODE + op] & 0x01) p[M_SCALING] |= (uint8_t)(1u << op);

    p[M_AMS_65] = (uint8_t)((a[A_AMS + 0] & 0x07) | ((a[A_AMS + 1] & 0x07) << 3));
    p[M_AMS_43] = (uint8_t)((a[A_AMS + 2] & 0x07) | ((a[A_AMS + 3] & 0x07) << 3));
    p[M_AMS_21] = (uint8_t)((a[A_AMS + 4] & 0x07) | ((a[A_AMS + 5] & 0x07) << 3));

    p[M_PEG_LFO_RND] = (uint8_t)((a[A_PEG_RANGE]    & 0x03)
                               | ((a[A_LFO_KEY_TRIG] & 0x01) << 2)
                               | ((a[A_PEG_VEL] ? 1u : 0u)   << 3)
                               | ((a[A_RANDOM_PITCH] & 0x07) << 4));

    p[M_MODE_PBRANGE] = (uint8_t)((a[A_VOICE_MODE] & 0x03)
                                | ((a[A_PB_RANGE]  & 0x0f) << 2));
    p[M_PB_STEP_MODE] = (uint8_t)((a[A_PB_STEP]    & 0x0f)
                                | ((a[A_PB_MODE]   & 0x03) << 4));
    p[M_PORTA]        = (uint8_t)((a[A_PORTA_MODE] & 0x01)
                                | ((a[A_PORTA_STEP] & 0x0f) << 1));
    p[M_PORTA_TIME]   = (uint8_t)(a[A_PORTA_TIME] & 0x7f);

    memcpy(p + M_MW,       a + A_MW,       3);
    memcpy(p + M_FC1,      a + A_FC1,      4);
    memcpy(p + M_BC,       a + A_BC,       4);
    memcpy(p + M_AT,       a + A_AT,       4);
    memcpy(p + M_FC2,      a + A_FC2,      4);
    memcpy(p + M_MIDI_CTL, a + A_MIDI_CTL, 4);

    p[M_PEG_RATE_SCL] = (uint8_t)(a[A_PEG_RATE_SCL] & 0x07);
    p[M_UNISON]       = (uint8_t)(a[A_UNISON_DET]   & 0x07);
}

/*
 * Finding a dump.
 *
 * The framing is checked in full rather than by the format byte alone, because
 * this is looking through whatever a player handed over, which may be a MIDI
 * file with other things in it, and a stray 0x06 is not a bank.
 */
static int dump_at(const uint8_t *buf, size_t len, size_t pos, int midshift,
                   uint8_t format, size_t payload, const uint8_t **data)
{
    const size_t total = payload + 8;   /* header 6, checksum, F7 */
    if (pos + (size_t)midshift + total > len) return 0;

    const uint8_t *m = buf + pos;
    const size_t s = (size_t)midshift;

    if (m[0] != 0xf0)              return 0;
    if (m[1 + s] != 0x43)          return 0;
    if ((m[2 + s] & 0xf0) != 0x00) return 0;   /* bulk dump, any device number */
    if (m[3 + s] != format)        return 0;
    if (m[4 + s] != (uint8_t)((payload >> 7) & 0x7f)) return 0;
    if (m[5 + s] != (uint8_t)(payload & 0x7f))        return 0;
    if (m[6 + s + payload + 1] != 0xf7)               return 0;

    if (data) *data = m + 6 + s;
    return 1;
}

int dx7ii_amem_at(const uint8_t *buf, size_t len, size_t pos, int midshift,
                  const uint8_t **data)
{
    return dump_at(buf, len, pos, midshift, DX7II_FORMAT_AMEM,
                   DX7II_AMEM_DATA, data);
}

int dx7ii_aced_at(const uint8_t *buf, size_t len, size_t pos, int midshift,
                  const uint8_t **data)
{
    return dump_at(buf, len, pos, midshift, DX7II_FORMAT_ACED,
                   DX7II_ACED_SIZE, data);
}

/* ------------------------------------------------------------------------ */

/*
 * One controller's three depths become one sensitivity and a set of bits.
 *
 * `assign` says which destinations are live, which is exact: a depth of zero
 * is off and anything else is on. `sensitivity` is the strongest of them
 * rescaled from 0..99 to 0..15. Taking the strongest rather than an average
 * keeps the routing the voice leant on and exaggerates the one it did not,
 * which is the failure that sounds like the patch rather than like a bug.
 */
static int one_controller(const uint8_t *depths, int n,
                          uint8_t *sensitivity, uint8_t *assign)
{
    uint8_t bits = 0, strongest = 0;
    int live = 0;

    /* Only the first three destinations exist on a DX7. The fourth that some
     * of these carry is volume, which a DX7 has no routing for at all. */
    for (int i = 0; i < n && i < 3; i++) {
        if (depths[i]) {
            bits |= (uint8_t)(1u << i);
            live++;
            if (depths[i] > strongest) strongest = depths[i];
        }
    }

    if (strongest > 99) strongest = 99;
    *assign = bits;
    /*
     * 99 maps to 15, rounded to nearest. And any depth the voice actually set
     * keeps at least a sensitivity of 1, because rounding a live routing down
     * to zero turns a subtle control into a dead one, which is a worse lie
     * than making it slightly too strong. A depth of 1 rounds to 0 without
     * this, which is what the test caught.
     */
    if (strongest == 0) {
        *sensitivity = 0;
    } else {
        unsigned v = (strongest * 15u + 49u) / 99u;
        *sensitivity = (uint8_t)(v ? v : 1u);
    }

    /* More than one destination live with differing depths is the case a DX7
     * cannot hold. Equal depths lose nothing. */
    if (live > 1) {
        for (int i = 0; i < n && i < 3; i++)
            if (depths[i] && depths[i] != strongest) return 1;
    }
    return 0;
}

void dx7ii_aced_controllers(const dx7ii_aced_t *a, dx7ii_controllers_t *out)
{
    const uint8_t *r = a->raw;
    memset(out, 0, sizeof(*out));

    /* These two are the same number on both machines. */
    out->pitch_bend_range = (uint8_t)(r[A_PB_RANGE] > 12 ? 12 : r[A_PB_RANGE]);
    out->portamento_time  = (uint8_t)(r[A_PORTA_TIME] > 99 ? 99 : r[A_PORTA_TIME]);

    int lost = 0;
    lost += one_controller(r + A_MW,  3, &out->mod_wheel_sensitivity, &out->mod_wheel_assign);
    lost += one_controller(r + A_FC1, 4, &out->foot_sensitivity,      &out->foot_assign);
    lost += one_controller(r + A_BC,  4, &out->breath_sensitivity,    &out->breath_assign);
    lost += one_controller(r + A_AT,  4, &out->pressure_sensitivity,  &out->pressure_assign);
    out->flattened = lost;
}

static const char *voice_mode_name(unsigned v)
{
    switch (v & 3) {
    case 1:  return "mono";
    case 2:  return "unison poly";
    case 3:  return "unison mono";
    default: return "poly";
    }
}

static int any(const uint8_t *p, size_t n)
{
    for (size_t i = 0; i < n; i++) if (p[i]) return 1;
    return 0;
}

int dx7ii_aced_describe(const dx7ii_aced_t *a, char *out, size_t cap)
{
    const uint8_t *r = a->raw;
    const char *found[10];
    int n = 0;

    if ((r[A_VOICE_MODE] & 3) != 0)      found[n++] = voice_mode_name(r[A_VOICE_MODE]);
    if (r[A_RANDOM_PITCH])               found[n++] = "random pitch";
    if (r[A_LFO_KEY_TRIG])               found[n++] = "per voice LFO";
    if (r[A_PEG_RANGE] != 1)             found[n++] = "a pitch envelope range other than the DX7's";
    if (r[A_PEG_VEL])                    found[n++] = "pitch envelope velocity";
    if (any(r + A_SCALING_MODE, 6))      found[n++] = "fractional scaling";
    if (r[A_PORTA_TIME])                 found[n++] = "portamento";
    if (any(r + A_FC2, 4))               found[n++] = "a second foot controller";
    if (any(r + A_MIDI_CTL, 4))          found[n++] = "MIDI controller routing";
    if (r[A_UNISON_DET])                 found[n++] = "unison detune";

    if (out && cap) {
        out[0] = '\0';
        size_t used = 0;
        for (int i = 0; i < n; i++) {
            const char *sep = (i == 0) ? "" : (i == n - 1 ? " and " : ", ");
            const size_t need = strlen(sep) + strlen(found[i]);
            if (used + need + 1 >= cap) break;
            memcpy(out + used, sep, strlen(sep));   used += strlen(sep);
            memcpy(out + used, found[i], strlen(found[i]));
            used += strlen(found[i]);
            out[used] = '\0';
        }
    }
    return n;
}
