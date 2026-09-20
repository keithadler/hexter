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
void dx_4op_voice_to_dx7(const uint8_t *packed128, uint8_t *unpacked155,
                         uint8_t *op_wave6);

/*
 * The eight four-operator algorithms as the nearest six-operator one, with the
 * DX7 operator each four-operator operator becomes. Operators are numbered the
 * way people number them, 1 to 6.
 *
 * Every Yamaha four-operator machine of this era wires its operators the same
 * eight ways in the same order, so the FB-01 converter shares this table.
 */
typedef struct {
    uint8_t dx7_algorithm;          /* 1 to 32, as printed on the instrument */
    uint8_t dx7_op[4];              /* for four-operator OP1, OP2, OP3, OP4 */
} dx_4op_algorithm_t;

extern const dx_4op_algorithm_t dx_4op_algorithm_map[8];

/* saw, square, triangle, sample and hold, in the DX7's own numbering */
extern const uint8_t dx_4op_lfo_wave_map[4];

#endif /* _DX7_VOICE_4OP_H */
