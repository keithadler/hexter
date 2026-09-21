/* hexter DSSI software synthesizer plugin
 *
 * Yamaha FB-01 voices converted to the six-operator voice this engine plays.
 *
 * Copyright (C) 2026 Keith Adler
 *
 * The layout here is Yamaha's own, from the "Voice Data Format $00 - $3F" and
 * "Operator Block" tables in the FB-01 owner's manual. It was first worked out
 * from the FB01 Sound Editor by Frederic Meslin
 * (https://sourceforge.net/projects/fb01editor/) and cross-checked against
 * Edisyn (https://github.com/eclab/edisyn); both agree with the manual and
 * with this, field for field, and neither is copied here. See AUTHORS.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#ifndef _DX7_VOICE_FB01_H
#define _DX7_VOICE_FB01_H

#include <stdint.h>
#include <stddef.h>

/*
 * The FB-01 sends a bank in one message of its own, nothing like the DX7's,
 * and the manual gives it in two forms. Both carry the same 49 packets: the
 * bank's own 64 parameters, then 48 voices. A packet is a two-byte count, the
 * data, and a checksum.
 *
 *   voice bank 0, the user bank:
 *     F0 43 0n 0C  00 40 <64 bank bytes> <sum>
 *     then 48 voices of 131 bytes: 01 00 <128 voice bytes> <sum>
 *     then F7                                     = 6360 bytes
 *
 *   voice bank x, x being 0 to 6:
 *     F0 43 75 0n 00 00 0x  00 40 <64 bank bytes> <sum>
 *     then the same 48 voices, then F7            = 6363 bytes
 *
 * The only difference is the header, three bytes longer in the second, so the
 * voices start at 71 or at 74. Somebody dumping their own sounds is likely to
 * send the first, since that is the bank they can write to.
 *
 * Every value is a pair of bytes holding one 8-bit number, low nibble first,
 * so the 128 bytes of a voice are 64 parameters.
 */
#define FB01_BANK_VOICES        48
#define FB01_VOICE_STRIDE      131   /* one voice inside the bank */
#define FB01_VOICE_PARAM_OFF     2   /* past a voice's own two-byte header */
#define FB01_VOICE_PARAM_LEN   128   /* 64 parameters, two bytes each */

/* voice bank 0 */
#define FB01_BANK0_SIZE       6360
#define FB01_BANK0_VOICE_OFF    71
/* voice bank x */
#define FB01_BANK_SIZE        6363
#define FB01_VOICE_OFFSET       74

/* which of the two forms a bank turned out to be */
typedef struct {
    long size;          /* the whole message, counting the F0 and the F7 */
    long voice_offset;  /* where the first voice starts, from the F0 */
} fb01_bank_layout_t;

/*
 * True if an FB-01 bank dump of either form starts at data[at], and then
 * `layout` says which one it was.
 *
 * Inside a standard MIDI file the F0 is followed by the event's own length
 * bytes, so everything after it sits `midshift` further along; pass 0 for a
 * plain sysex file. This is the same shift the rest of the bank reader uses.
 */
int  fb01_bank_at(const uint8_t *data, long length, long at, int midshift,
                  fb01_bank_layout_t *layout);

/*
 * Convert one FB-01 voice, given the 128 parameter bytes of it, into a 155-byte
 * unpacked DX7 voice.
 *
 * The FB-01 is a four-operator machine, so the same compromise applies as for
 * the DX21 family: four operators become four of the DX7's six and the other
 * two are silenced. What it cannot carry is anything the DX7 has no place for.
 */
void fb01_voice_to_dx7(const uint8_t *params128, uint8_t *unpacked155);

#endif /* _DX7_VOICE_FB01_H */
