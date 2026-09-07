/* hexter DX7 software synthesizer - host-independent engine API
 *
 * Copyright (C) 2004-2018 Sean Bolton and others.
 * Copyright (C) 2026 Keith Adler.
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

/* This is the whole of hexter as a synthesizer, with no plugin API, GUI
 * toolkit or MIDI system attached. The CLAP and LV2 plugins, the command
 * line renderer and the tests are all written against it.
 *
 * Threading: hexter_engine_render() is the audio thread. Everything that
 * changes the patch bank, performance data or voice settings takes a
 * mutex; render() only ever tries the lock and produces silence for the
 * block if it loses, so a main-thread call can never stall audio. Events
 * that belong at a sample position (notes, controllers, program changes,
 * sysex) are passed into render() with a frame offset and applied there,
 * under the lock, exactly as the DSSI plugin did. */

#ifndef _HEXTER_ENGINE_H
#define _HEXTER_ENGINE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HEXTER_ENGINE_VERSION "2.0.0"

typedef struct _hexter_instance_t hexter_engine_t;

/* voice assignment modes (hexter_engine_set_mono_mode) */
enum {
    HEXTER_MONO_OFF  = 0,   /* polyphonic */
    HEXTER_MONO_ON   = 1,   /* monophonic, retrigger envelopes on every new key */
    HEXTER_MONO_ONCE = 2,   /* monophonic, legato: envelopes retrigger only after all keys released */
    HEXTER_MONO_BOTH = 3    /* monophonic, retrigger on new key, and on release back to a held key */
};

/* limits */
#define HEXTER_ENGINE_MAX_POLYPHONY      64
#define HEXTER_ENGINE_DEFAULT_POLYPHONY  10
#define HEXTER_ENGINE_TUNING_MIN     415.3f
#define HEXTER_ENGINE_TUNING_MAX     466.2f
#define HEXTER_ENGINE_TUNING_DEFAULT 440.0f
#define HEXTER_ENGINE_VOLUME_MIN     -70.0f   /* dB */
#define HEXTER_ENGINE_VOLUME_MAX      20.0f
#define HEXTER_ENGINE_VOLUME_DEFAULT   0.0f

#define HEXTER_PATCH_PACKED_SIZE    128   /* one voice as stored in a bank */
#define HEXTER_PATCH_UNPACKED_SIZE  155   /* one voice as edited / sent by sysex */
#define HEXTER_BANK_PATCHES         128
#define HEXTER_PERFORMANCE_SIZE      64

/* events, passed to hexter_engine_render() sorted by frame */
enum {
    HEXTER_EV_NONE = 0,
    HEXTER_EV_NOTE_ON,          /* a = key, b = velocity (0 acts as note off) */
    HEXTER_EV_NOTE_OFF,         /* a = key, b = release velocity */
    HEXTER_EV_KEY_PRESSURE,     /* a = key, b = pressure */
    HEXTER_EV_CONTROL_CHANGE,   /* a = controller, b = value */
    HEXTER_EV_CHANNEL_PRESSURE, /* a = pressure */
    HEXTER_EV_PITCH_BEND,       /* value = -8192 .. 8191 */
    HEXTER_EV_PROGRAM_CHANGE,   /* a = program 0..127 */
    HEXTER_EV_SYSEX,            /* data/size = complete message, F0 .. F7 */
    HEXTER_EV_POLYPHONY,        /* value = 1..64 */
    HEXTER_EV_MONO_MODE         /* value = HEXTER_MONO_* */
};

typedef struct {
    uint32_t       frame;   /* offset into the render block */
    uint16_t       type;    /* HEXTER_EV_* */
    uint8_t        a, b;
    int32_t        value;
    const uint8_t *data;    /* sysex only */
    uint32_t       size;    /* sysex only */
} hexter_event_t;

/* ---- lifetime ---- */
hexter_engine_t *hexter_engine_new(float sample_rate);
void  hexter_engine_free(hexter_engine_t *e);
/* silence all voices and reset the LFO; call when the host (re)activates */
void  hexter_engine_reset(hexter_engine_t *e);
float hexter_engine_get_sample_rate(const hexter_engine_t *e);

/* ---- continuous parameters (any thread; plain stores) ---- */
void  hexter_engine_set_tuning(hexter_engine_t *e, float a4_hz);
float hexter_engine_get_tuning(const hexter_engine_t *e);
void  hexter_engine_set_volume(hexter_engine_t *e, float db);
float hexter_engine_get_volume(const hexter_engine_t *e);

/* ---- voice assignment (main thread; blocks on the voice lock) ---- */
int   hexter_engine_set_polyphony(hexter_engine_t *e, int voices);  /* returns the value applied */
int   hexter_engine_get_polyphony(const hexter_engine_t *e);
int   hexter_engine_set_mono_mode(hexter_engine_t *e, int mode);    /* returns the value applied */
int   hexter_engine_get_mono_mode(const hexter_engine_t *e);
int   hexter_engine_get_active_voices(const hexter_engine_t *e);

/* ---- programs and the bank (main thread unless noted) ---- */
int   hexter_engine_get_program(const hexter_engine_t *e);
/* any thread: applied immediately if the bank is free, else at the next render */
void  hexter_engine_select_program(hexter_engine_t *e, int program);
/* name is 10 characters plus NUL; pass at least 11 bytes */
void  hexter_engine_get_program_name(const hexter_engine_t *e, int program, char *name);
void  hexter_engine_get_patch(const hexter_engine_t *e, int program, uint8_t *packed128);
void  hexter_engine_set_patch(hexter_engine_t *e, int program, const uint8_t *packed128);
/* copy 'count' packed patches into the bank starting at first_program; returns count stored */
int   hexter_engine_set_bank(hexter_engine_t *e, int first_program, const uint8_t *packed, int count);
void  hexter_engine_get_bank(const hexter_engine_t *e, uint8_t *packed_out);  /* 128 * 128 bytes */
/* load a bank file (.syx, .dx7, .mid, .tx7, .snd, .bnk, .dx2, raw) into the bank
 * at first_program; returns patches loaded, 0 on error with *errmsg set
 * (malloc'd; caller frees) */
int   hexter_engine_load_bank_file(hexter_engine_t *e, const char *path,
                                   int first_program, char **errmsg);
int   hexter_engine_load_bank_memory(hexter_engine_t *e, const uint8_t *data,
                                     size_t size, const char *name_hint,
                                     int first_program, char **errmsg);
/* honor HEXTER_DEFAULT_BANK (or the older HEXTER_DEFAULT_PATCH) if set;
 * returns patches loaded, 0 if unset or on error */
int   hexter_engine_load_bank_from_env(hexter_engine_t *e);
/* clear the whole bank to the init voice */
void  hexter_engine_init_bank(hexter_engine_t *e);

/* ---- performance (global controller assignment) data ---- */
void  hexter_engine_get_performance(const hexter_engine_t *e, uint8_t *out64);
void  hexter_engine_set_performance(hexter_engine_t *e, const uint8_t *in64);

/* ---- the edit buffer: what is actually sounding for the current program ---- */
void  hexter_engine_get_current_patch(const hexter_engine_t *e, uint8_t *unpacked155);
/* replace the sounding patch (unsaved edit; new notes use it) */
void  hexter_engine_set_current_patch(hexter_engine_t *e, const uint8_t *unpacked155);
/* single voice parameter edit, index 0..155 in DX7 voice-data order;
 * operator parameters also update playing voices */
void  hexter_engine_set_voice_parameter(hexter_engine_t *e, int index, int value);
/* write the edit buffer back into the bank as 'program' */
void  hexter_engine_store_current_patch(hexter_engine_t *e, int program);

/* ---- events and rendering (audio thread) ---- */
/* translate a MIDI message (channel voice, or a complete sysex) into an
 * event; returns 1 if the message is one hexter handles */
int   hexter_event_from_midi(const uint8_t *msg, size_t len, uint32_t frame,
                             hexter_event_t *ev);
/* render 'nframes' mono samples into out, applying events at their frame
 * offsets; events must be sorted by frame and frame <= nframes */
void  hexter_engine_render(hexter_engine_t *e, float *out, uint32_t nframes,
                           const hexter_event_t *events, uint32_t nevents);

/* ---- state, for host save/restore ---- */
#define HEXTER_STATE_SIZE  16800
/* writes exactly HEXTER_STATE_SIZE bytes; returns bytes written or 0 */
size_t hexter_engine_state_save(const hexter_engine_t *e, uint8_t *buf, size_t size);
/* returns 1 on success */
int    hexter_engine_state_load(hexter_engine_t *e, const uint8_t *buf, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* _HEXTER_ENGINE_H */
