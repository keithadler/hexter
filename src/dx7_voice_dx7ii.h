/* hexter DSSI software synthesizer plugin
 *
 * The DX7II's extra voice data: ACED unpacked, AMEM packed.
 *
 * Copyright (C) 2026 Keith Adler
 *
 * The byte layout follows the DX7II specification documented at
 * https://github.com/probonopd/dx-specs, which credits DerekCook's
 * YamahaSynthFileFormats and the DX7II service manual, and states that it was
 * verified against Dexed. That repository carries no licence, which does not
 * matter here: a file format is a fact rather than a work, and nothing of
 * theirs is copied. This is written from the description, the same way the
 * Casio librarian in pdsynth was written from a published specification.
 * Found by Reaper10, who apologised for it.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

/*
 * What this is for.
 *
 * A DX7II voice is a DX7 voice plus a second block. The first block, VCED
 * packed into VMEM, is byte for byte what a DX7 has, which is why hexter has
 * always been able to load the voices out of a DX7II bank without anybody
 * noticing there was more to them. The second block, ACED packed into AMEM,
 * holds everything Yamaha added in 1986: the controller routings that used to
 * be global function data and are now per voice, unison, a selectable pitch
 * envelope range, random pitch, and per operator scaling and amplitude
 * sensitivity beyond what the original allowed.
 *
 * That block has been arriving in hexter and falling on the floor without a
 * word, which is the thing worth fixing first. Reading it is separate from
 * acting on it: this file reads it and says what is there. What the engine
 * does with any of it is a later question and a harder one.
 */
#ifndef _DX7_VOICE_DX7II_H
#define _DX7_VOICE_DX7II_H

#include <stdint.h>
#include <stddef.h>

/* Yamaha's format ids, in the fourth byte of the dump. */
#define DX7II_FORMAT_ACED   0x05   /* one voice's extras, unpacked */
#define DX7II_FORMAT_AMEM   0x06   /* thirty two of them, packed */

#define DX7II_ACED_SIZE     73     /* one unpacked ACED */
#define DX7II_AMEM_VOICE    35     /* one packed voice inside an AMEM dump */
#define DX7II_AMEM_VOICES   32
#define DX7II_AMEM_DATA     (DX7II_AMEM_VOICE * DX7II_AMEM_VOICES)   /* 1120 */
/* F0 43 0n 06 08 60, 1120 bytes, checksum, F7 */
#define DX7II_AMEM_DUMP     (DX7II_AMEM_DATA + 8)                    /* 1128 */
/* F0 43 0n 05 00 49, 73 bytes, checksum, F7 */
#define DX7II_ACED_DUMP     (DX7II_ACED_SIZE + 8)                    /* 81 */

/*
 * One voice's extras, unpacked. The offsets are ACED's own, so this is the
 * shape a DX7II sends when it dumps its edit buffer, and `raw` can be handed
 * straight back to one.
 */
typedef struct {
    uint8_t raw[DX7II_ACED_SIZE];
} dx7ii_aced_t;

/*
 * Expand one packed AMEM voice into an ACED.
 *
 * Packing is not simply narrower fields: AMEM keeps pitch envelope velocity
 * sensitivity in a single bit where ACED gives it three, so a value of 1 there
 * became a 7 on the way in and cannot become a 1 again. Unpacking spreads it
 * back over the full range rather than pretending the information is still
 * present.
 */
void dx7ii_amem_unpack(const uint8_t *packed35, dx7ii_aced_t *out);

/* And back again, for writing a bank a DX7II will read. */
void dx7ii_aced_pack(const dx7ii_aced_t *in, uint8_t *packed35);

/*
 * Is there an AMEM dump at `pos`? `midshift` is the same two byte offset
 * dx7_bank.c uses when the dump is inside a standard MIDI file.
 *
 * Returns 1 and fills `data` with a pointer to the 1120 bytes, or 0.
 */
int dx7ii_amem_at(const uint8_t *buf, size_t len, size_t pos, int midshift,
                  const uint8_t **data);

/* The same question for a single unpacked ACED dump. */
int dx7ii_aced_at(const uint8_t *buf, size_t len, size_t pos, int midshift,
                  const uint8_t **data);

/*
 * What is actually set in a voice's extras, in a sentence, for telling a
 * player what came in with their bank and what is not being used. Writes at
 * most `cap` bytes including the terminator and returns the number of
 * distinct things it found, which is 0 when the voice's extras are all
 * defaults and there is nothing worth saying.
 */
int dx7ii_aced_describe(const dx7ii_aced_t *a, char *out, size_t cap);

/*
 * What a DX7II voice would set on a DX7's front panel.
 *
 * On a DX7 the controller routings, the bend range and the portamento time are
 * function data: global, set once, the same for every patch. hexter holds them
 * on the instance and sets them from its performance buffer, next to Sean
 * Bolton's own note reading "-FIX- later these will optionally come from
 * patch". The DX7II is the machine that does exactly that, keeping them per
 * voice in ACED, so this converts one into the other.
 *
 * Two of them are exact. The routings are not, and it is worth knowing which
 * way: a DX7 has one sensitivity per controller shared by its three
 * destinations, and a DX7II has an independent depth for each. A voice that
 * sends the mod wheel hard to pitch and gently to amplitude cannot be
 * expressed, so the strongest depth wins and `flattened` counts how many
 * controllers lost a distinction that way. Nothing is silently rounded.
 */
typedef struct {
    uint8_t pitch_bend_range;        /* semitones, 0 to 12 */
    uint8_t portamento_time;         /* 0 to 99 */
    uint8_t mod_wheel_sensitivity;   /* 0 to 15 */
    uint8_t mod_wheel_assign;        /* bit 0 pitch, 1 amplitude, 2 EG bias */
    uint8_t foot_sensitivity,     foot_assign;
    uint8_t breath_sensitivity,   breath_assign;
    uint8_t pressure_sensitivity, pressure_assign;
    int     flattened;               /* controllers whose depths had to become one */
} dx7ii_controllers_t;

void dx7ii_aced_controllers(const dx7ii_aced_t *a, dx7ii_controllers_t *out);

/* The Yamaha checksum: the low seven bits of the negated sum of the data. */
uint8_t dx7ii_checksum(const uint8_t *data, size_t len);

#endif /* _DX7_VOICE_DX7II_H */
