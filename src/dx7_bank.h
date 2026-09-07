/* hexter DX7 software synthesizer
 *
 * Copyright (C) 2004, 2009, 2011 Sean Bolton and others.
 * DX7 patchbank loading code by Martin Tarenskeen.
 * Copyright (C) 2026 Keith Adler (engine extraction).
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be
 * useful, but WITHOUT ANY WARRANTY; without even the implied
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 * PURPOSE.  See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the Free
 * Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 */

#ifndef _DX7_BANK_H
#define _DX7_BANK_H

#include <stdint.h>

#include "hexter_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Yamaha bulk-dump checksum: the two's complement of the 7-bit sum. */
int dx7_bulk_dump_checksum(const uint8_t *data, int length);

/* Parse a patch bank held in memory. Recognizes DX7 32-voice bulk dumps
 * (bare or inside a Standard MIDI File), single-voice dumps, raw packed
 * 128-byte voices, and several legacy editor formats chosen by the file
 * extension of 'filename' (which may be NULL). 'data' is scratch: it is
 * modified during parsing. Copies up to 'maxpatches' packed patches to
 * 'firstpatch' and returns the number copied; on failure returns 0 and, if
 * errmsg is non-NULL, sets *errmsg to a malloc'd message the caller frees. */
int dx7_patchbank_parse(uint8_t *data, long length, const char *filename,
                        dx7_patch_t *firstpatch, int maxpatches,
                        char **errmsg);

/* Read 'filename' and parse it with dx7_patchbank_parse(). */
int dx7_patchbank_load(const char *filename, dx7_patch_t *firstpatch,
                       int maxpatches, char **errmsg);

#ifdef __cplusplus
}
#endif

#endif /* _DX7_BANK_H */
