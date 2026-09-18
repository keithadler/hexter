/* hexter for iOS - the engine behind an AVAudioEngine source node, driven from Swift
 *
 * Copyright (C) 2026 Keith Adler. GPL-2.0-or-later.
 *
 * Everything the audio thread touches lives here in C: the engine, a ring of MIDI messages
 * that any thread may push into, and a peak meter. Swift never runs on the audio thread.
 */
#ifndef HEXTER_BRIDGE_H
#define HEXTER_BRIDGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Longest MIDI message the ring accepts: a full 32-voice bank dump fits. */
#define HEXTER_BRIDGE_MAX_MESSAGE 4608

typedef struct hexter_bridge hexter_bridge_t;

hexter_bridge_t *hexter_bridge_new(float sample_rate);
void hexter_bridge_free(hexter_bridge_t *b);

/* audio thread only: drains queued MIDI and renders `frames` mono samples */
void hexter_bridge_render(hexter_bridge_t *b, float *out, uint32_t frames);

/* any thread: one complete MIDI message (channel voice, or F0..F7 sysex); false if dropped */
bool hexter_bridge_midi(hexter_bridge_t *b, const uint8_t *msg, size_t len);

/* any thread: highest |sample| since the last call, then resets to 0 */
float hexter_bridge_peak(hexter_bridge_t *b);

/* returns patches loaded; on failure a negative number and `err` holds the message */
int hexter_bridge_load_bank(hexter_bridge_t *b, const uint8_t *data, size_t size,
                            const char *name_hint, char *err, size_t err_size);

void hexter_bridge_program_name(hexter_bridge_t *b, int program, char *name12);
void hexter_bridge_select_program(hexter_bridge_t *b, int program);
int  hexter_bridge_program(hexter_bridge_t *b);
void hexter_bridge_set_volume(hexter_bridge_t *b, float db);
int  hexter_bridge_set_polyphony(hexter_bridge_t *b, int voices);
int  hexter_bridge_active_voices(hexter_bridge_t *b);

#ifdef __cplusplus
}
#endif
#endif
