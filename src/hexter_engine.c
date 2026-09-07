/* hexter DX7 software synthesizer - host-independent engine
 *
 * Copyright (C) 2004-2018 Sean Bolton and others.
 * Copyright (C) 2026 Keith Adler.
 *
 * Portions of this file come from hexter.c, and so may contain code from
 * Chris Cannam and Steve Harris's public domain DSSI example code and
 * from Peter Hanappe's Fluidsynth.
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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>

#include "hexter_types.h"
#include "hexter.h"
#include "hexter_synth.h"
#include "dx7_voice.h"
#include "dx7_voice_data.h"
#include "dx7_bank.h"
#include "hexter_engine.h"

/* ---- mutual exclusion (the engine's copies of the hexter.c versions) ---- */

int
dssp_voicelist_mutex_lock(hexter_instance_t *instance)
{
    return pthread_mutex_lock(&instance->voicelist_mutex);
}

int
dssp_voicelist_mutex_unlock(hexter_instance_t *instance)
{
    return pthread_mutex_unlock(&instance->voicelist_mutex);
}

static inline int
voicelist_trylock(hexter_instance_t *instance)
{
    int rc = pthread_mutex_trylock(&instance->voicelist_mutex);
    if (rc) {
        instance->voicelist_mutex_grab_failed = 1;
        return rc;
    }
    if (instance->voicelist_mutex_grab_failed) {
        hexter_instance_all_voices_off(instance);
        instance->voicelist_mutex_grab_failed = 0;
    }
    return 0;
}

static inline int
clampi(int x, int lo, int hi)
{
    return x < lo ? lo : (x > hi ? hi : x);
}

static inline float
clampf(float x, float lo, float hi)
{
    return x < lo ? lo : (x > hi ? hi : x);
}

/* ---- lifetime ---- */

hexter_engine_t *
hexter_engine_new(float sample_rate)
{
    hexter_instance_t *instance;
    int i;

    if (sample_rate < 1000.0f) return NULL;

    dx7_voice_init_tables();

    instance = (hexter_instance_t *)calloc(1, sizeof(hexter_instance_t));
    if (!instance) return NULL;

    for (i = 0; i < HEXTER_MAX_POLYPHONY; i++) {
        instance->voice[i] = dx7_voice_new();
        if (!instance->voice[i]) {
            hexter_engine_free(instance);
            return NULL;
        }
    }
    if (!(instance->patches = (dx7_patch_t *)malloc(128 * DX7_VOICE_SIZE_PACKED))) {
        hexter_engine_free(instance);
        return NULL;
    }

    instance->tuning_value = HEXTER_ENGINE_TUNING_DEFAULT;
    instance->volume_value = HEXTER_ENGINE_VOLUME_DEFAULT;
    instance->tuning = &instance->tuning_value;
    instance->volume = &instance->volume_value;
    instance->output = NULL;

    instance->sample_rate = sample_rate;
    instance->nugget_remains = 0;
    dx7_eg_init_constants(instance);

    instance->note_id = 0;
    instance->polyphony = HEXTER_DEFAULT_POLYPHONY;
    instance->monophonic = DSSP_MONO_MODE_OFF;
    instance->max_voices = instance->polyphony;
    instance->current_voices = 0;
    instance->last_key = 0;
    pthread_mutex_init(&instance->voicelist_mutex, NULL);
    instance->voicelist_mutex_grab_failed = 0;
    pthread_mutex_init(&instance->patches_mutex, NULL);
    instance->pending_program_change = -1;
    instance->current_program = 0;
    instance->overlay_program = -1;
    hexter_data_performance_init(instance->performance_buffer);
    hexter_data_patches_init(instance->patches);
    hexter_instance_select_program(instance, 0, 0);
    hexter_instance_set_performance_data(instance);
    hexter_instance_init_controls(instance);
    for (i = 0; i < 8; i++) instance->held_keys[i] = -1;
    dx7_lfo_reset(instance);

    return instance;
}

void
hexter_engine_free(hexter_engine_t *instance)
{
    int i;

    if (!instance) return;
    if (instance->patches) {
        hexter_instance_all_voices_off(instance);
        pthread_mutex_destroy(&instance->voicelist_mutex);
        pthread_mutex_destroy(&instance->patches_mutex);
        free(instance->patches);
    }
    for (i = 0; i < HEXTER_MAX_POLYPHONY; i++) {
        if (instance->voice[i]) free(instance->voice[i]);
    }
    free(instance);
}

void
hexter_engine_reset(hexter_engine_t *instance)
{
    hexter_instance_all_voices_off(instance);
    instance->current_voices = 0;
    dx7_lfo_reset(instance);
}

float
hexter_engine_get_sample_rate(const hexter_engine_t *instance)
{
    return instance->sample_rate;
}

/* ---- continuous parameters ---- */

void
hexter_engine_set_tuning(hexter_engine_t *instance, float a4_hz)
{
    instance->tuning_value = clampf(a4_hz, HEXTER_ENGINE_TUNING_MIN, HEXTER_ENGINE_TUNING_MAX);
}

float
hexter_engine_get_tuning(const hexter_engine_t *instance)
{
    return instance->tuning_value;
}

void
hexter_engine_set_volume(hexter_engine_t *instance, float db)
{
    instance->volume_value = clampf(db, HEXTER_ENGINE_VOLUME_MIN, HEXTER_ENGINE_VOLUME_MAX);
}

float
hexter_engine_get_volume(const hexter_engine_t *instance)
{
    return instance->volume_value;
}

/* ---- voice assignment ---- */

static void
clear_held_keys(hexter_instance_t *instance)
{
    int i;
    for (i = 0; i < 8; i++) instance->held_keys[i] = -1;
}

/* caller holds the voice list lock */
static void
apply_polyphony(hexter_instance_t *instance, int polyphony)
{
    int i;

    polyphony = clampi(polyphony, 1, HEXTER_MAX_POLYPHONY);
    instance->polyphony = polyphony;

    if (!instance->monophonic) {
        instance->max_voices = polyphony;
        for (i = polyphony; i < HEXTER_MAX_POLYPHONY; i++) {
            dx7_voice_t *voice = instance->voice[i];
            if (_PLAYING(voice)) {
                if (instance->held_keys[0] != -1)
                    clear_held_keys(instance);
                dx7_voice_off(voice);
            }
        }
    }
}

/* caller holds the voice list lock */
static void
apply_mono_mode(hexter_instance_t *instance, int mode)
{
    mode = clampi(mode, HEXTER_MONO_OFF, HEXTER_MONO_BOTH);

    if (mode == HEXTER_MONO_OFF) {
        instance->monophonic = 0;
        instance->max_voices = instance->polyphony;
    } else {
        if (!instance->monophonic) {
            hexter_instance_all_voices_off(instance);
            instance->max_voices = 1;
            instance->mono_voice = NULL;
            clear_held_keys(instance);
        }
        instance->monophonic = mode;
    }
}

int
hexter_engine_set_polyphony(hexter_engine_t *instance, int voices)
{
    pthread_mutex_lock(&instance->voicelist_mutex);
    apply_polyphony(instance, voices);
    pthread_mutex_unlock(&instance->voicelist_mutex);
    return instance->polyphony;
}

int
hexter_engine_get_polyphony(const hexter_engine_t *instance)
{
    return instance->polyphony;
}

int
hexter_engine_set_mono_mode(hexter_engine_t *instance, int mode)
{
    pthread_mutex_lock(&instance->voicelist_mutex);
    apply_mono_mode(instance, mode);
    pthread_mutex_unlock(&instance->voicelist_mutex);
    return instance->monophonic;
}

int
hexter_engine_get_mono_mode(const hexter_engine_t *instance)
{
    return instance->monophonic;
}

int
hexter_engine_get_active_voices(const hexter_engine_t *instance)
{
    return instance->current_voices;
}

/* ---- programs and the bank ---- */

int
hexter_engine_get_program(const hexter_engine_t *instance)
{
    return instance->current_program;
}

void
hexter_engine_select_program(hexter_engine_t *instance, int program)
{
    if (program < 0 || program >= 128) return;

    if (pthread_mutex_trylock(&instance->patches_mutex)) {
        instance->pending_program_change = program;
        return;
    }
    hexter_instance_select_program(instance, 0, program);
    pthread_mutex_unlock(&instance->patches_mutex);
}

static inline void
handle_pending_program_change(hexter_instance_t *instance)
{
    if (pthread_mutex_trylock(&instance->patches_mutex))
        return;
    hexter_instance_select_program(instance, 0, instance->pending_program_change);
    instance->pending_program_change = -1;
    pthread_mutex_unlock(&instance->patches_mutex);
}

void
hexter_engine_get_program_name(const hexter_engine_t *instance, int program, char *name)
{
    if (program < 0 || program >= 128) {
        name[0] = 0;
        return;
    }
    dx7_voice_copy_name(name, &instance->patches[program]);
}

void
hexter_engine_get_patch(const hexter_engine_t *instance, int program, uint8_t *packed128)
{
    if (program < 0 || program >= 128) {
        memcpy(packed128, &dx7_voice_init_voice, DX7_VOICE_SIZE_PACKED);
        return;
    }
    memcpy(packed128, &instance->patches[program], DX7_VOICE_SIZE_PACKED);
}

/* caller holds the patches lock: re-derive the sounding patch after the
 * bank changed underneath it */
static void
refresh_current_patch(hexter_instance_t *instance, int first, int count)
{
    if (instance->current_program >= first &&
        instance->current_program < first + count &&
        instance->current_program != instance->overlay_program)
        dx7_patch_unpack(instance->patches, instance->current_program,
                         instance->current_patch_buffer);
}

void
hexter_engine_set_patch(hexter_engine_t *instance, int program, const uint8_t *packed128)
{
    hexter_engine_set_bank(instance, program, packed128, 1);
}

int
hexter_engine_set_bank(hexter_engine_t *instance, int first_program,
                       const uint8_t *packed, int count)
{
    if (first_program < 0 || first_program >= 128 || count <= 0) return 0;
    if (first_program + count > 128) count = 128 - first_program;

    pthread_mutex_lock(&instance->patches_mutex);
    memcpy(&instance->patches[first_program], packed, count * DX7_VOICE_SIZE_PACKED);
    refresh_current_patch(instance, first_program, count);
    pthread_mutex_unlock(&instance->patches_mutex);
    return count;
}

void
hexter_engine_get_bank(const hexter_engine_t *instance, uint8_t *packed_out)
{
    memcpy(packed_out, instance->patches, 128 * DX7_VOICE_SIZE_PACKED);
}

int
hexter_engine_load_bank_file(hexter_engine_t *instance, const char *path,
                             int first_program, char **errmsg)
{
    dx7_patch_t tmp[128];
    int count;

    if (errmsg) *errmsg = NULL;
    if (first_program < 0 || first_program >= 128) {
        if (errmsg) *errmsg = strdup("bank position out of range");
        return 0;
    }
    count = dx7_patchbank_load(path, tmp, 128 - first_program, errmsg);
    if (count <= 0) return 0;
    return hexter_engine_set_bank(instance, first_program, (const uint8_t *)tmp, count);
}

int
hexter_engine_load_bank_memory(hexter_engine_t *instance, const uint8_t *data,
                               size_t size, const char *name_hint,
                               int first_program, char **errmsg)
{
    dx7_patch_t tmp[128];
    uint8_t *scratch;
    int count;

    if (errmsg) *errmsg = NULL;
    if (first_program < 0 || first_program >= 128) {
        if (errmsg) *errmsg = strdup("bank position out of range");
        return 0;
    }
    if (size > 2097152) {
        if (errmsg) *errmsg = strdup("patch data is too large");
        return 0;
    }
    /* the parser rearranges in place, and the DX2 transform needs headroom */
    scratch = (uint8_t *)calloc(1, size + 16384 + 128 * 381);
    if (!scratch) {
        if (errmsg) *errmsg = strdup("out of memory");
        return 0;
    }
    memcpy(scratch, data, size);
    count = dx7_patchbank_parse(scratch, (long)size, name_hint, tmp,
                                128 - first_program, errmsg);
    free(scratch);
    if (count <= 0) return 0;
    return hexter_engine_set_bank(instance, first_program, (const uint8_t *)tmp, count);
}

int
hexter_engine_load_bank_from_env(hexter_engine_t *instance)
{
    const char *path = getenv("HEXTER_DEFAULT_BANK");
    char *err = NULL;
    int count;

    if (!path || !*path) path = getenv("HEXTER_DEFAULT_PATCH");
    if (!path || !*path) return 0;
    count = hexter_engine_load_bank_file(instance, path, 0, &err);
    if (!count) {
        fprintf(stderr, "hexter: could not load default bank '%s': %s\n",
                path, err ? err : "unknown error");
    }
    free(err);
    return count;
}

void
hexter_engine_init_bank(hexter_engine_t *instance)
{
    int i;

    pthread_mutex_lock(&instance->patches_mutex);
    for (i = 0; i < 128; i++)
        memcpy(&instance->patches[i], &dx7_voice_init_voice, DX7_VOICE_SIZE_PACKED);
    instance->overlay_program = -1;
    refresh_current_patch(instance, 0, 128);
    pthread_mutex_unlock(&instance->patches_mutex);
}

/* ---- performance data ---- */

void
hexter_engine_get_performance(const hexter_engine_t *instance, uint8_t *out64)
{
    memcpy(out64, instance->performance_buffer, DX7_PERFORMANCE_SIZE);
}

void
hexter_engine_set_performance(hexter_engine_t *instance, const uint8_t *in64)
{
    pthread_mutex_lock(&instance->patches_mutex);
    memcpy(instance->performance_buffer, in64, DX7_PERFORMANCE_SIZE);
    hexter_instance_set_performance_data(instance);
    pthread_mutex_unlock(&instance->patches_mutex);
}

/* ---- edit buffer ---- */

void
hexter_engine_get_current_patch(const hexter_engine_t *instance, uint8_t *unpacked155)
{
    memcpy(unpacked155, instance->current_patch_buffer, DX7_VOICE_SIZE_UNPACKED);
}

/* caller holds the patches lock */
static void
set_current_patch_locked(hexter_instance_t *instance, const uint8_t *unpacked155)
{
    memcpy(instance->current_patch_buffer, unpacked155, DX7_VOICE_SIZE_UNPACKED);
    /* keep it as the edit buffer for this program so reselecting the
     * program, or restoring a session, keeps the edit */
    instance->overlay_program = instance->current_program;
    memcpy(instance->overlay_patch_buffer, unpacked155, DX7_VOICE_SIZE_UNPACKED);
}

void
hexter_engine_set_current_patch(hexter_engine_t *instance, const uint8_t *unpacked155)
{
    pthread_mutex_lock(&instance->patches_mutex);
    set_current_patch_locked(instance, unpacked155);
    pthread_mutex_unlock(&instance->patches_mutex);
}

/* caller holds the patches lock (and the voice list lock if voices may be
 * playing) */
static void
set_voice_parameter_locked(hexter_instance_t *instance, int index, int value)
{
    static const uint8_t op_param_max[21] = {
        99, 99, 99, 99, 99, 99, 99, 99,   /* eg rates and levels */
        99, 99, 99,                       /* breakpoint, depths */
        3, 3,                             /* curves */
        7, 3, 7,                          /* rate scaling, amp mod sens, velocity sens */
        99, 1, 31, 99, 14                 /* output level, mode, coarse, fine, detune */
    };
    static const uint8_t global_param_max[30] = {
        99, 99, 99, 99, 99, 99, 99, 99,   /* 126-133 pitch eg */
        31, 7, 1,                         /* 134 algorithm, 135 feedback, 136 osc sync */
        99, 99, 99, 99, 1, 5, 7,          /* 137-143 lfo speed, delay, pmd, amd, sync, wave, pms */
        48,                               /* 144 transpose */
        127, 127, 127, 127, 127,          /* 145-154 name */
        127, 127, 127, 127, 127
    };

    if (index < 0 || index >= DX7_VOICE_SIZE_UNPACKED) return;
    if (value < 0) value = 0;

    if (index < 126) {
        int opnum = 5 - index / 21;
        int param = index % 21;
        if (value > op_param_max[param]) value = op_param_max[param];
        instance->current_patch_buffer[index] = (uint8_t)value;
        hexter_instance_apply_op_param(instance, opnum, param, value);
    } else {
        if (value > global_param_max[index - 126]) value = global_param_max[index - 126];
        instance->current_patch_buffer[index] = (uint8_t)value;
    }
    if (instance->overlay_program == instance->current_program)
        instance->overlay_patch_buffer[index] = (uint8_t)value;
    else {
        instance->overlay_program = instance->current_program;
        memcpy(instance->overlay_patch_buffer, instance->current_patch_buffer,
               DX7_VOICE_SIZE_UNPACKED);
    }
}

void
hexter_engine_set_voice_parameter(hexter_engine_t *instance, int index, int value)
{
    pthread_mutex_lock(&instance->voicelist_mutex);
    pthread_mutex_lock(&instance->patches_mutex);
    set_voice_parameter_locked(instance, index, value);
    pthread_mutex_unlock(&instance->patches_mutex);
    pthread_mutex_unlock(&instance->voicelist_mutex);
}

void
hexter_engine_store_current_patch(hexter_engine_t *instance, int program)
{
    if (program < 0 || program >= 128) return;

    pthread_mutex_lock(&instance->patches_mutex);
    dx7_patch_pack(instance->current_patch_buffer, instance->patches, (uint8_t)program);
    if (instance->overlay_program == program) instance->overlay_program = -1;
    if (instance->current_program == program &&
        instance->overlay_program == -1) {
        /* the bank now matches the edit buffer; nothing else to do */
    }
    pthread_mutex_unlock(&instance->patches_mutex);
}

/* ---- sysex, on the audio thread ---- */

/* Yamaha DX7 messages:
 *   F0 43 0n 00 01 1B <155 bytes> <cksum> F7       single voice (to the edit buffer)
 *   F0 43 0n 09 20 00 <4096 bytes> <cksum> F7      32 voices (to programs 0-31)
 *   F0 43 1n gp pp vv F7                           parameter change:
 *        g = (gp >> 2): 0 voice parameter (pp 0-155), 2 function parameter (pp 64-77)
 */
static void
handle_sysex(hexter_instance_t *instance, const uint8_t *d, uint32_t n)
{
    if (n < 7 || d[0] != 0xF0 || d[1] != 0x43 || d[n - 1] != 0xF7) return;

    if ((d[2] & 0xF0) == 0x00) {

        if (n == 163 && d[3] == 0x00 && d[4] == 0x01 && d[5] == 0x1B) {
            /* single voice: 155 bytes of unpacked voice data */
            if (dx7_bulk_dump_checksum(d + 6, 155) != d[161]) return;
            if (pthread_mutex_trylock(&instance->patches_mutex)) return;
            set_current_patch_locked(instance, d + 6);
            pthread_mutex_unlock(&instance->patches_mutex);

        } else if (n == 4104 && d[3] == 0x09 && d[4] == 0x20 && d[5] == 0x00) {
            /* 32 voice bulk dump */
            if (dx7_bulk_dump_checksum(d + 6, 4096) != d[4102]) return;
            if (pthread_mutex_trylock(&instance->patches_mutex)) return;
            memcpy(instance->patches, d + 6, 4096);
            if (instance->overlay_program >= 0 && instance->overlay_program < 32)
                instance->overlay_program = -1;
            refresh_current_patch(instance, 0, 32);
            pthread_mutex_unlock(&instance->patches_mutex);
        }

    } else if ((d[2] & 0xF0) == 0x10 && n == 7) {

        int group = d[3] >> 2;
        int param = ((d[3] & 0x03) << 7) | (d[4] & 0x7F);
        int value = d[5] & 0x7F;

        if (group == 0) {
            if (pthread_mutex_trylock(&instance->patches_mutex)) return;
            set_voice_parameter_locked(instance, param, value);
            pthread_mutex_unlock(&instance->patches_mutex);
        } else if (group == 2) {
            uint8_t *perf = instance->performance_buffer;
            int slot = -1;
            switch (param) {
              case 64: apply_mono_mode(instance, value ? HEXTER_MONO_ON : HEXTER_MONO_OFF); return;
              case 65: slot = 3;  value = clampi(value, 0, 12); break;  /* pitch bend range */
              case 69: slot = 5;  value = clampi(value, 0, 99); break;  /* portamento time */
              case 70: slot = 9;  value = clampi(value, 0, 15); break;  /* mod wheel sensitivity */
              case 71: slot = 10; value = clampi(value, 0, 7);  break;  /* mod wheel assign */
              case 72: slot = 11; value = clampi(value, 0, 15); break;  /* foot sensitivity */
              case 73: slot = 12; value = clampi(value, 0, 7);  break;  /* foot assign */
              case 74: slot = 15; value = clampi(value, 0, 15); break;  /* breath sensitivity */
              case 75: slot = 16; value = clampi(value, 0, 7);  break;  /* breath assign */
              case 76: slot = 13; value = clampi(value, 0, 15); break;  /* aftertouch sensitivity */
              case 77: slot = 14; value = clampi(value, 0, 7);  break;  /* aftertouch assign */
              default: return;
            }
            if (pthread_mutex_trylock(&instance->patches_mutex)) return;
            perf[0] &= ~0x01;  /* leave 0.5.9 compatibility mode */
            perf[slot] = (uint8_t)value;
            hexter_instance_set_performance_data(instance);
            pthread_mutex_unlock(&instance->patches_mutex);
        }
    }
}

/* ---- events and rendering ---- */

int
hexter_event_from_midi(const uint8_t *msg, size_t len, uint32_t frame,
                       hexter_event_t *ev)
{
    memset(ev, 0, sizeof(*ev));
    ev->frame = frame;
    if (len == 0) return 0;

    if (msg[0] == 0xF0) {
        if (len < 2) return 0;
        ev->type = HEXTER_EV_SYSEX;
        ev->data = msg;
        ev->size = (uint32_t)len;
        return 1;
    }

    switch (msg[0] & 0xF0) {
      case 0x80:
        if (len < 3) return 0;
        ev->type = HEXTER_EV_NOTE_OFF; ev->a = msg[1] & 0x7F; ev->b = msg[2] & 0x7F;
        return 1;
      case 0x90:
        if (len < 3) return 0;
        ev->type = HEXTER_EV_NOTE_ON; ev->a = msg[1] & 0x7F; ev->b = msg[2] & 0x7F;
        return 1;
      case 0xA0:
        if (len < 3) return 0;
        ev->type = HEXTER_EV_KEY_PRESSURE; ev->a = msg[1] & 0x7F; ev->b = msg[2] & 0x7F;
        return 1;
      case 0xB0:
        if (len < 3) return 0;
        ev->type = HEXTER_EV_CONTROL_CHANGE; ev->a = msg[1] & 0x7F; ev->b = msg[2] & 0x7F;
        return 1;
      case 0xC0:
        if (len < 2) return 0;
        ev->type = HEXTER_EV_PROGRAM_CHANGE; ev->a = msg[1] & 0x7F;
        return 1;
      case 0xD0:
        if (len < 2) return 0;
        ev->type = HEXTER_EV_CHANNEL_PRESSURE; ev->a = msg[1] & 0x7F;
        return 1;
      case 0xE0:
        if (len < 3) return 0;
        ev->type = HEXTER_EV_PITCH_BEND;
        ev->value = (int32_t)(((msg[2] & 0x7F) << 7) | (msg[1] & 0x7F)) - 8192;
        return 1;
      default:
        return 0;
    }
}

/* caller holds the voice list lock */
static inline void
handle_event(hexter_instance_t *instance, const hexter_event_t *ev)
{
    switch (ev->type) {
      case HEXTER_EV_NOTE_ON:
        if (ev->b > 0)
            hexter_instance_note_on(instance, ev->a, ev->b);
        else
            hexter_instance_note_off(instance, ev->a, 64);
        break;
      case HEXTER_EV_NOTE_OFF:
        hexter_instance_note_off(instance, ev->a, ev->b);
        break;
      case HEXTER_EV_KEY_PRESSURE:
        if (ev->a < 128) hexter_instance_key_pressure(instance, ev->a, ev->b);
        break;
      case HEXTER_EV_CONTROL_CHANGE:
        if (ev->a < 128) hexter_instance_control_change(instance, ev->a, ev->b & 0x7F);
        break;
      case HEXTER_EV_CHANNEL_PRESSURE:
        hexter_instance_channel_pressure(instance, ev->a & 0x7F);
        break;
      case HEXTER_EV_PITCH_BEND:
        hexter_instance_pitch_bend(instance, clampi(ev->value, -8192, 8191));
        break;
      case HEXTER_EV_PROGRAM_CHANGE:
        hexter_engine_select_program(instance, ev->a);
        break;
      case HEXTER_EV_SYSEX:
        if (ev->data) handle_sysex(instance, ev->data, ev->size);
        break;
      case HEXTER_EV_POLYPHONY:
        apply_polyphony(instance, ev->value);
        break;
      case HEXTER_EV_MONO_MODE:
        apply_mono_mode(instance, ev->value);
        break;
      default:
        break;
    }
}

void
hexter_engine_render(hexter_engine_t *instance, float *out, uint32_t nframes,
                     const hexter_event_t *events, uint32_t nevents)
{
    unsigned long samples_done = 0;
    unsigned long event_index = 0;
    unsigned long burst_size;

    memset(out, 0, sizeof(float) * nframes);
    if (nframes == 0) return;

    if (voicelist_trylock(instance))
        return;

    instance->output = out;

    if (instance->pending_program_change > -1)
        handle_pending_program_change(instance);

    while (samples_done < nframes) {

        if (!instance->nugget_remains)
            instance->nugget_remains = HEXTER_NUGGET_SIZE;

        while (event_index < nevents && events[event_index].frame <= samples_done) {
            handle_event(instance, &events[event_index]);
            event_index++;
        }

        burst_size = HEXTER_NUGGET_SIZE;
        if (instance->nugget_remains < burst_size)
            burst_size = instance->nugget_remains;
        if (event_index < nevents && events[event_index].frame - samples_done < burst_size)
            burst_size = events[event_index].frame - samples_done;
        if (nframes - samples_done < burst_size)
            burst_size = nframes - samples_done;

        hexter_instance_render_voices(instance, samples_done, burst_size,
                                      (burst_size == instance->nugget_remains));
        samples_done += burst_size;
        instance->nugget_remains -= burst_size;
    }

    /* events at frame == nframes (or trailing after a zero-length burst) */
    while (event_index < nevents) {
        handle_event(instance, &events[event_index]);
        event_index++;
    }

    instance->output = NULL;
    dssp_voicelist_mutex_unlock(instance);
}

/* ---- state ---- */

#define STATE_MAGIC   "HXT2"
#define STATE_VERSION 1

static void put_u32(uint8_t *p, uint32_t v) { p[0] = v & 0xFF; p[1] = (v >> 8) & 0xFF; p[2] = (v >> 16) & 0xFF; p[3] = (v >> 24) & 0xFF; }
static uint32_t get_u32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static void put_f32(uint8_t *p, float f) { uint32_t u; memcpy(&u, &f, 4); put_u32(p, u); }
static float get_f32(const uint8_t *p) { uint32_t u = get_u32(p); float f; memcpy(&f, &u, 4); return f; }

/* layout (little-endian):
 *   0     "HXT2"
 *   4     u32 version
 *   8     u32 payload length (bytes after this field)
 *   12    bank, 128 x 128
 *   16396 performance, 64
 *   16460 i32 program
 *   16464 i32 overlay program (-1 = none)
 *   16468 overlay patch, 155
 *   16623 current patch, 155
 *   16778 pad, 2
 *   16780 f32 tuning
 *   16784 f32 volume
 *   16788 i32 polyphony
 *   16792 i32 mono mode
 *   16796 u32 reserved
 *   16800 end */

size_t
hexter_engine_state_save(const hexter_engine_t *instance, uint8_t *buf, size_t size)
{
    if (size < HEXTER_STATE_SIZE) return 0;
    memset(buf, 0, HEXTER_STATE_SIZE);
    memcpy(buf, STATE_MAGIC, 4);
    put_u32(buf + 4, STATE_VERSION);
    put_u32(buf + 8, HEXTER_STATE_SIZE - 12);
    memcpy(buf + 12, instance->patches, 128 * DX7_VOICE_SIZE_PACKED);
    memcpy(buf + 16396, instance->performance_buffer, DX7_PERFORMANCE_SIZE);
    put_u32(buf + 16460, (uint32_t)instance->current_program);
    put_u32(buf + 16464, (uint32_t)instance->overlay_program);
    memcpy(buf + 16468, instance->overlay_patch_buffer, DX7_VOICE_SIZE_UNPACKED);
    memcpy(buf + 16623, instance->current_patch_buffer, DX7_VOICE_SIZE_UNPACKED);
    put_f32(buf + 16780, instance->tuning_value);
    put_f32(buf + 16784, instance->volume_value);
    put_u32(buf + 16788, (uint32_t)instance->polyphony);
    put_u32(buf + 16792, (uint32_t)instance->monophonic);
    return HEXTER_STATE_SIZE;
}

int
hexter_engine_state_load(hexter_engine_t *instance, const uint8_t *buf, size_t size)
{
    int program, overlay;

    if (size < HEXTER_STATE_SIZE) return 0;
    if (memcmp(buf, STATE_MAGIC, 4) != 0) return 0;
    if (get_u32(buf + 4) != STATE_VERSION) return 0;
    if (get_u32(buf + 8) < HEXTER_STATE_SIZE - 12) return 0;

    program = (int)get_u32(buf + 16460);
    overlay = (int)get_u32(buf + 16464);
    if (program < 0 || program >= 128) program = 0;
    if (overlay < -1 || overlay >= 128) overlay = -1;

    pthread_mutex_lock(&instance->voicelist_mutex);
    pthread_mutex_lock(&instance->patches_mutex);

    hexter_instance_all_voices_off(instance);
    memcpy(instance->patches, buf + 12, 128 * DX7_VOICE_SIZE_PACKED);
    memcpy(instance->performance_buffer, buf + 16396, DX7_PERFORMANCE_SIZE);
    hexter_instance_set_performance_data(instance);
    instance->overlay_program = overlay;
    memcpy(instance->overlay_patch_buffer, buf + 16468, DX7_VOICE_SIZE_UNPACKED);
    instance->pending_program_change = -1;
    hexter_instance_select_program(instance, 0, program);
    memcpy(instance->current_patch_buffer, buf + 16623, DX7_VOICE_SIZE_UNPACKED);

    instance->tuning_value = clampf(get_f32(buf + 16780), HEXTER_ENGINE_TUNING_MIN, HEXTER_ENGINE_TUNING_MAX);
    instance->volume_value = clampf(get_f32(buf + 16784), HEXTER_ENGINE_VOLUME_MIN, HEXTER_ENGINE_VOLUME_MAX);
    apply_mono_mode(instance, HEXTER_MONO_OFF);
    apply_polyphony(instance, (int)get_u32(buf + 16788));
    apply_mono_mode(instance, (int)get_u32(buf + 16792));

    pthread_mutex_unlock(&instance->patches_mutex);
    pthread_mutex_unlock(&instance->voicelist_mutex);
    return 1;
}
