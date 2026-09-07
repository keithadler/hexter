/* hexter DX7 software synthesizer - LV2 plugin
 *
 * Copyright (C) 2026 Keith Adler.
 * hexter is copyright (C) 2004-2018 Sean Bolton and others.
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
#include <math.h>

#include <lv2/core/lv2.h>
#include <lv2/atom/atom.h>
#include <lv2/atom/forge.h>
#include <lv2/atom/util.h>
#include <lv2/midi/midi.h>
#include <lv2/patch/patch.h>
#include <lv2/urid/urid.h>
#include <lv2/state/state.h>
#include <lv2/worker/worker.h>
#include <lv2/log/log.h>
#include <lv2/log/logger.h>

#include "hexter_engine.h"
#include "dx7_bank.h"

#define HEXTER_URI        "https://github.com/keithadler/hexter"
#define HEXTER_URI_BANK   HEXTER_URI "#bank"
#define HEXTER_URI_STATE  HEXTER_URI "#state"

#define MAX_EVENTS  4096
#define PATH_MAX_LEN 1024

enum {
    PORT_CONTROL = 0,
    PORT_NOTIFY,
    PORT_OUT,
    PORT_TUNING,
    PORT_VOLUME,
    PORT_POLYPHONY,
    PORT_MONO_MODE,
    PORT_PROGRAM,
    PORT_COUNT
};

typedef struct {
    LV2_URID atom_Chunk;
    LV2_URID atom_Path;
    LV2_URID atom_URID;
    LV2_URID atom_Object;
    LV2_URID midi_MidiEvent;
    LV2_URID patch_Set;
    LV2_URID patch_Get;
    LV2_URID patch_property;
    LV2_URID patch_value;
    LV2_URID hexter_bank;
    LV2_URID hexter_state;
} hexter_uris_t;

/* what the worker hands back to run() */
typedef struct {
    int         count;
    char        path[PATH_MAX_LEN];
    uint8_t     patches[HEXTER_BANK_PATCHES * HEXTER_PATCH_PACKED_SIZE];
} bank_response_t;

typedef struct {
    hexter_engine_t *engine;
    double           sample_rate;

    const LV2_Atom_Sequence *control;
    LV2_Atom_Sequence       *notify;
    float                   *out;
    const float             *tuning;
    const float             *volume;
    const float             *polyphony;
    const float             *mono_mode;
    const float             *program;

    LV2_URID_Map            *map;
    LV2_Worker_Schedule     *schedule;
    LV2_Log_Logger           logger;
    hexter_uris_t            uris;
    LV2_Atom_Forge           forge;
    LV2_Atom_Forge_Frame     notify_frame;

    float last_tuning, last_volume;
    int   last_polyphony, last_mono, last_program;
    char  bank_path[PATH_MAX_LEN];
    int   bank_notify_pending;   /* work_response happened; announce in the next run() */

    hexter_event_t events[MAX_EVENTS];
} hexter_lv2_t;

static void
map_uris(LV2_URID_Map *map, hexter_uris_t *u)
{
    u->atom_Chunk     = map->map(map->handle, LV2_ATOM__Chunk);
    u->atom_Path      = map->map(map->handle, LV2_ATOM__Path);
    u->atom_URID      = map->map(map->handle, LV2_ATOM__URID);
    u->atom_Object    = map->map(map->handle, LV2_ATOM__Object);
    u->midi_MidiEvent = map->map(map->handle, LV2_MIDI__MidiEvent);
    u->patch_Set      = map->map(map->handle, LV2_PATCH__Set);
    u->patch_Get      = map->map(map->handle, LV2_PATCH__Get);
    u->patch_property = map->map(map->handle, LV2_PATCH__property);
    u->patch_value    = map->map(map->handle, LV2_PATCH__value);
    u->hexter_bank    = map->map(map->handle, HEXTER_URI_BANK);
    u->hexter_state   = map->map(map->handle, HEXTER_URI_STATE);
}

static LV2_Handle
instantiate(const LV2_Descriptor *descriptor, double rate,
            const char *bundle_path, const LV2_Feature *const *features)
{
    hexter_lv2_t *h = (hexter_lv2_t *)calloc(1, sizeof(hexter_lv2_t));
    const LV2_Feature *const *f;

    if (!h) return NULL;

    for (f = features; *f; f++) {
        if (!strcmp((*f)->URI, LV2_URID__map)) h->map = (LV2_URID_Map *)(*f)->data;
        else if (!strcmp((*f)->URI, LV2_WORKER__schedule)) h->schedule = (LV2_Worker_Schedule *)(*f)->data;
        else if (!strcmp((*f)->URI, LV2_LOG__log)) h->logger.log = (LV2_Log_Log *)(*f)->data;
    }
    if (!h->map) {
        fprintf(stderr, "hexter.lv2: host does not provide urid:map\n");
        free(h);
        return NULL;
    }
    lv2_log_logger_set_map(&h->logger, h->map);
    map_uris(h->map, &h->uris);
    lv2_atom_forge_init(&h->forge, h->map);

    h->engine = hexter_engine_new((float)rate);
    if (!h->engine) {
        free(h);
        return NULL;
    }
    h->sample_rate = rate;
    hexter_engine_load_bank_from_env(h->engine);

    h->last_tuning = hexter_engine_get_tuning(h->engine);
    h->last_volume = hexter_engine_get_volume(h->engine);
    h->last_polyphony = hexter_engine_get_polyphony(h->engine);
    h->last_mono = hexter_engine_get_mono_mode(h->engine);
    h->last_program = hexter_engine_get_program(h->engine);
    return (LV2_Handle)h;
}

static void
connect_port(LV2_Handle instance, uint32_t port, void *data)
{
    hexter_lv2_t *h = (hexter_lv2_t *)instance;

    switch (port) {
      case PORT_CONTROL:   h->control = (const LV2_Atom_Sequence *)data; break;
      case PORT_NOTIFY:    h->notify = (LV2_Atom_Sequence *)data; break;
      case PORT_OUT:       h->out = (float *)data; break;
      case PORT_TUNING:    h->tuning = (const float *)data; break;
      case PORT_VOLUME:    h->volume = (const float *)data; break;
      case PORT_POLYPHONY: h->polyphony = (const float *)data; break;
      case PORT_MONO_MODE: h->mono_mode = (const float *)data; break;
      case PORT_PROGRAM:   h->program = (const float *)data; break;
      default: break;
    }
}

static void
activate(LV2_Handle instance)
{
    hexter_lv2_t *h = (hexter_lv2_t *)instance;
    hexter_engine_reset(h->engine);
}

/* write a patch:Set naming the current bank file into the notify port */
static void
notify_bank_path(hexter_lv2_t *h, uint32_t frame)
{
    if (!h->notify || !h->bank_path[0]) return;
    lv2_atom_forge_frame_time(&h->forge, frame);
    {
        LV2_Atom_Forge_Frame obj;
        lv2_atom_forge_object(&h->forge, &obj, 0, h->uris.patch_Set);
        lv2_atom_forge_key(&h->forge, h->uris.patch_property);
        lv2_atom_forge_urid(&h->forge, h->uris.hexter_bank);
        lv2_atom_forge_key(&h->forge, h->uris.patch_value);
        lv2_atom_forge_path(&h->forge, h->bank_path, (uint32_t)strlen(h->bank_path));
        lv2_atom_forge_pop(&h->forge, &obj);
    }
}

static void
run(LV2_Handle instance, uint32_t nframes)
{
    hexter_lv2_t *h = (hexter_lv2_t *)instance;
    uint32_t ne = 0;
    hexter_event_t ev;

    if (h->notify) {
        const uint32_t cap = h->notify->atom.size;
        lv2_atom_forge_set_buffer(&h->forge, (uint8_t *)h->notify, cap);
        lv2_atom_forge_sequence_head(&h->forge, &h->notify_frame, 0);
    }

    if (h->bank_notify_pending) {
        h->bank_notify_pending = 0;
        notify_bank_path(h, 0);
    }

    /* control ports */
    if (h->tuning && *h->tuning != h->last_tuning) {
        h->last_tuning = *h->tuning;
        hexter_engine_set_tuning(h->engine, h->last_tuning);
    }
    if (h->volume && *h->volume != h->last_volume) {
        h->last_volume = *h->volume;
        hexter_engine_set_volume(h->engine, h->last_volume);
    }
    if (h->polyphony) {
        int p = (int)lrintf(*h->polyphony);
        if (p != h->last_polyphony && ne < MAX_EVENTS) {
            h->last_polyphony = p;
            memset(&ev, 0, sizeof(ev));
            ev.type = HEXTER_EV_POLYPHONY; ev.value = p;
            h->events[ne++] = ev;
        }
    }
    if (h->mono_mode) {
        int m = (int)lrintf(*h->mono_mode);
        if (m != h->last_mono && ne < MAX_EVENTS) {
            h->last_mono = m;
            memset(&ev, 0, sizeof(ev));
            ev.type = HEXTER_EV_MONO_MODE; ev.value = m;
            h->events[ne++] = ev;
        }
    }
    if (h->program) {
        int p = (int)lrintf(*h->program);
        if (p != h->last_program && ne < MAX_EVENTS) {
            h->last_program = p;
            memset(&ev, 0, sizeof(ev));
            ev.type = HEXTER_EV_PROGRAM_CHANGE; ev.a = (uint8_t)(p < 0 ? 0 : (p > 127 ? 127 : p));
            h->events[ne++] = ev;
        }
    }

    /* atom events: MIDI and patch messages */
    if (h->control) {
        LV2_ATOM_SEQUENCE_FOREACH(h->control, a) {
            uint32_t frame = (uint32_t)a->time.frames;
            if (frame > nframes) frame = nframes;

            if (a->body.type == h->uris.midi_MidiEvent) {
                const uint8_t *msg = (const uint8_t *)LV2_ATOM_BODY_CONST(&a->body);
                if (ne < MAX_EVENTS && hexter_event_from_midi(msg, a->body.size, frame, &ev))
                    h->events[ne++] = ev;

            } else if (lv2_atom_forge_is_object_type(&h->forge, a->body.type)) {
                const LV2_Atom_Object *obj = (const LV2_Atom_Object *)&a->body;

                if (obj->body.otype == h->uris.patch_Set) {
                    const LV2_Atom *property = NULL, *value = NULL;
                    lv2_atom_object_get(obj, h->uris.patch_property, &property,
                                        h->uris.patch_value, &value, 0);
                    if (property && property->type == h->uris.atom_URID &&
                        ((const LV2_Atom_URID *)property)->body == h->uris.hexter_bank &&
                        value && value->type == h->uris.atom_Path && h->schedule) {
                        /* load the file off the audio thread */
                        h->schedule->schedule_work(h->schedule->handle,
                                                   lv2_atom_total_size(value), value);
                    }
                } else if (obj->body.otype == h->uris.patch_Get) {
                    notify_bank_path(h, frame);
                }
            }
        }
    }

    if (h->out) {
        /* control-port program changes are applied before MIDI at frame 0,
         * everything else keeps its frame order */
        hexter_engine_render(h->engine, h->out, nframes, h->events, ne);
    }

    if (h->notify) lv2_atom_forge_pop(&h->forge, &h->notify_frame);
}

static void
deactivate(LV2_Handle instance)
{
    hexter_lv2_t *h = (hexter_lv2_t *)instance;
    hexter_engine_reset(h->engine);
}

static void
cleanup(LV2_Handle instance)
{
    hexter_lv2_t *h = (hexter_lv2_t *)instance;
    if (h->engine) hexter_engine_free(h->engine);
    free(h);
}

/* ---- worker: bank file loading ---- */

static LV2_Worker_Status
work(LV2_Handle instance, LV2_Worker_Respond_Function respond,
     LV2_Worker_Respond_Handle handle, uint32_t size, const void *data)
{
    hexter_lv2_t *h = (hexter_lv2_t *)instance;
    const LV2_Atom *atom = (const LV2_Atom *)data;
    const char *path;
    bank_response_t *resp;
    char *err = NULL;

    if (atom->type != h->uris.atom_Path) return LV2_WORKER_ERR_UNKNOWN;
    path = (const char *)LV2_ATOM_BODY_CONST(atom);

    resp = (bank_response_t *)calloc(1, sizeof(bank_response_t));
    if (!resp) return LV2_WORKER_ERR_NO_SPACE;
    snprintf(resp->path, sizeof(resp->path), "%s", path);
    resp->count = dx7_patchbank_load(path, (dx7_patch_t *)resp->patches,
                                     HEXTER_BANK_PATCHES, &err);
    if (!resp->count) {
        lv2_log_error(&h->logger, "hexter: could not load bank '%s': %s\n",
                      path, err ? err : "unknown error");
        free(err);
        free(resp);
        return LV2_WORKER_ERR_UNKNOWN;
    }
    respond(handle, sizeof(bank_response_t), resp);
    free(resp);
    return LV2_WORKER_SUCCESS;
}

static LV2_Worker_Status
work_response(LV2_Handle instance, uint32_t size, const void *data)
{
    hexter_lv2_t *h = (hexter_lv2_t *)instance;
    const bank_response_t *resp = (const bank_response_t *)data;

    if (size != sizeof(bank_response_t)) return LV2_WORKER_ERR_UNKNOWN;
    hexter_engine_set_bank(h->engine, 0, resp->patches, resp->count);
    memcpy(h->bank_path, resp->path, sizeof(h->bank_path));
    h->bank_notify_pending = 1;
    return LV2_WORKER_SUCCESS;
}

static const LV2_Worker_Interface worker_iface = { work, work_response, NULL };

/* ---- state ---- */

static LV2_State_Status
save(LV2_Handle instance, LV2_State_Store_Function store,
     LV2_State_Handle handle, uint32_t flags, const LV2_Feature *const *features)
{
    hexter_lv2_t *h = (hexter_lv2_t *)instance;
    uint8_t buf[HEXTER_STATE_SIZE];
    size_t n = hexter_engine_state_save(h->engine, buf, sizeof(buf));
    LV2_State_Map_Path *map_path = NULL;
    const LV2_Feature *const *f;

    if (!n) return LV2_STATE_ERR_UNKNOWN;
    store(handle, h->uris.hexter_state, buf, n, h->uris.atom_Chunk,
          LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE);

    if (h->bank_path[0]) {
        for (f = features; f && *f; f++)
            if (!strcmp((*f)->URI, LV2_STATE__mapPath)) map_path = (LV2_State_Map_Path *)(*f)->data;
        if (map_path) {
            char *apath = map_path->abstract_path(map_path->handle, h->bank_path);
            if (apath) {
                store(handle, h->uris.hexter_bank, apath, strlen(apath) + 1,
                      h->uris.atom_Path, LV2_STATE_IS_POD);
                free(apath);
            }
        } else {
            store(handle, h->uris.hexter_bank, h->bank_path, strlen(h->bank_path) + 1,
                  h->uris.atom_Path, LV2_STATE_IS_POD);
        }
    }
    return LV2_STATE_SUCCESS;
}

static LV2_State_Status
restore(LV2_Handle instance, LV2_State_Retrieve_Function retrieve,
        LV2_State_Handle handle, uint32_t flags, const LV2_Feature *const *features)
{
    hexter_lv2_t *h = (hexter_lv2_t *)instance;
    size_t size;
    uint32_t type, vflags;
    const void *value;
    LV2_State_Map_Path *map_path = NULL;
    const LV2_Feature *const *f;

    value = retrieve(handle, h->uris.hexter_state, &size, &type, &vflags);
    if (value && type == h->uris.atom_Chunk) {
        if (!hexter_engine_state_load(h->engine, (const uint8_t *)value, size))
            return LV2_STATE_ERR_BAD_TYPE;
        h->last_tuning = hexter_engine_get_tuning(h->engine);
        h->last_volume = hexter_engine_get_volume(h->engine);
        h->last_polyphony = hexter_engine_get_polyphony(h->engine);
        h->last_mono = hexter_engine_get_mono_mode(h->engine);
        h->last_program = hexter_engine_get_program(h->engine);
    }

    value = retrieve(handle, h->uris.hexter_bank, &size, &type, &vflags);
    if (value && type == h->uris.atom_Path) {
        for (f = features; f && *f; f++)
            if (!strcmp((*f)->URI, LV2_STATE__mapPath)) map_path = (LV2_State_Map_Path *)(*f)->data;
        if (map_path) {
            char *path = map_path->absolute_path(map_path->handle, (const char *)value);
            if (path) {
                snprintf(h->bank_path, sizeof(h->bank_path), "%s", path);
                free(path);
            }
        } else {
            snprintf(h->bank_path, sizeof(h->bank_path), "%s", (const char *)value);
        }
    }
    return LV2_STATE_SUCCESS;
}

static const LV2_State_Interface state_iface = { save, restore };

static const void *
extension_data(const char *uri)
{
    if (!strcmp(uri, LV2_STATE__interface)) return &state_iface;
    if (!strcmp(uri, LV2_WORKER__interface)) return &worker_iface;
    return NULL;
}

static const LV2_Descriptor descriptor = {
    HEXTER_URI,
    instantiate,
    connect_port,
    activate,
    run,
    deactivate,
    cleanup,
    extension_data
};

LV2_SYMBOL_EXPORT const LV2_Descriptor *
lv2_descriptor(uint32_t index)
{
    return index == 0 ? &descriptor : NULL;
}
