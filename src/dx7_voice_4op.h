/* hexter DSSI software synthesizer plugin
 *
 * Four-operator Yamaha voices (DX21, DX27, DX100, and the TX81Z's base voice)
 * converted to the six-operator voice this engine plays.
 *
 * Copyright (C) 2026 Keith Adler
 *
 * The layout of the four-operator voice memory, and the choice of which
 * six-operator algorithm stands in for each four-operator one, follow XDX by
 * Wurly (https://github.com/wurly200a/xdx), MIT licensed, Copyright (c) 2020
 * Wurly. See AUTHORS.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#ifndef _DX7_VOICE_4OP_H
#define _DX7_VOICE_4OP_H

#include <stdint.h>
#include <stddef.h>

/* One voice of a four-operator bulk dump. */
#define DX_4OP_VOICE_SIZE_PACKED  128
/* 32 of them, which is the same 4096 bytes a DX7 bulk dump carries. */
#define DX_4OP_DUMP_VOICES         32

/*
 * Convert one 128-byte four-operator voice into a 155-byte unpacked DX7 voice.
 *
 * The four operators become four of the DX7's six; the other two are silenced
 * by zeroing their output level. What survives is the shape of the sound: the
 * operator wiring, the envelopes, the frequency ratios, the LFO and the name.
 * What cannot survive is anything the DX7 has no place for, and anything the
 * four-operator machines expressed differently, so this is a sound-alike
 * rather than a faithful copy.
 */
void dx_4op_voice_to_dx7(const uint8_t *packed128, uint8_t *unpacked155);

#endif /* _DX7_VOICE_4OP_H */
