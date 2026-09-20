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
 * An FB-01 bank dump, which is its own message and nothing like the DX7's:
 *
 *   F0 43 75 0n 00 00 bb 00 40 <64 bank bytes> <sum>
 *   then 48 voices of 131 bytes: 01 00 <128 voice bytes> <sum>
 *   then F7
 *
 * which comes to 74 + 48 * 131 + 1 = 6363 bytes.
 *
 * Every value in it is a pair of bytes holding one 8-bit number, low nibble
 * first, so the 128 bytes of a voice are 64 parameters.
 */
#define FB01_BANK_SIZE        6363
#define FB01_BANK_VOICES        48
#define FB01_VOICE_STRIDE      131   /* one voice inside the bank */
#define FB01_VOICE_OFFSET       74   /* where the first one starts */
#define FB01_VOICE_PARAM_OFF     2   /* past a voice's own two-byte header */
#define FB01_VOICE_PARAM_LEN   128   /* 64 parameters, two bytes each */

/*
 * True if an FB-01 bank dump starts at data[at].
 *
 * Inside a standard MIDI file the F0 is followed by the event's own length
 * bytes, so everything after it sits `midshift` further along; pass 0 for a
 * plain sysex file. This is the same shift the rest of the bank reader uses.
 */
int  fb01_bank_at(const uint8_t *data, long length, long at, int midshift);

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
