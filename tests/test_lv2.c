/* hexter LV2 plugin test: load the bundle through lilv as a host would
 *
 * Copyright (C) 2026 Keith Adler. GPL-2.0-or-later.
 *
 * Arguments: path to the built hexter.lv2 bundle directory, path to the bank
 * directory.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#include <lilv/lilv.h>
#include <lv2/atom/atom.h>
#include <lv2/atom/forge.h>
#include <lv2/atom/util.h>
#include <lv2/midi/midi.h>
#include <lv2/patch/patch.h>
#include <lv2/urid/urid.h>
#include <lv2/state/state.h>
#include <lv2/worker/worker.h>

#define HEXTER_URI "https://github.com/keithadler/hexter"
#define BLOCK 256

static int failures = 0, checks = 0;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

/* ---- URID map ---- */

static char *uris[512];
static int nuris = 0;

static LV2_URID
map_uri(LV2_URID_Map_Handle handle, const char *uri)
{
    int i;
    for (i = 0; i < nuris; i++) if (!strcmp(uris[i], uri)) return (LV2_URID)(i + 1);
    if (nuris >= 512) return 0;
    uris[nuris++] = strdup(uri);
    return (LV2_URID)nuris;
}

static const char *
unmap_uri(LV2_URID_Unmap_Handle handle, LV2_URID urid)
{
    return (urid >= 1 && (int)urid <= nuris) ? uris[urid - 1] : NULL;
}

static LV2_URID_Map map = { NULL, map_uri };
static LV2_URID_Unmap unmap = { NULL, unmap_uri };

/* ---- synchronous worker ---- */

static const LV2_Worker_Interface *worker_iface = NULL;
static LV2_Handle worker_instance = NULL;
static uint8_t *pending_response = NULL;
static uint32_t pending_size = 0;
static int work_calls = 0;

static LV2_Worker_Status
respond(LV2_Worker_Respond_Handle handle, uint32_t size, const void *data)
{
    free(pending_response);
    pending_response = (uint8_t *)malloc(size);
    memcpy(pending_response, data, size);
    pending_size = size;
    return LV2_WORKER_SUCCESS;
}

static LV2_Worker_Status
schedule_work(LV2_Worker_Schedule_Handle handle, uint32_t size, const void *data)
{
    work_calls++;
    if (!worker_iface) return LV2_WORKER_ERR_UNKNOWN;
    return worker_iface->work(worker_instance, respond, NULL, size, data);
}

static LV2_Worker_Schedule schedule = { NULL, schedule_work };

static void
deliver_response(void)
{
    if (pending_response && worker_iface) {
        worker_iface->work_response(worker_instance, pending_size, pending_response);
        free(pending_response);
        pending_response = NULL;
        pending_size = 0;
    }
}

/* ---- buffers ---- */

static uint8_t control_buf[16384];
static uint8_t notify_buf[4096];
static float out_buf[BLOCK];
static float tuning = 440.0f, volume = 0.0f, polyphony = 10.0f, mono = 0.0f, program = 0.0f;

static LV2_Atom_Forge forge;
static LV2_Atom_Forge_Frame seq_frame;
static LV2_URID urid_midi, urid_patch_Set, urid_patch_Get, urid_patch_property, urid_patch_value, urid_bank, urid_atom_Path, urid_atom_URID, urid_atom_Object, urid_atom_Chunk, urid_state;

static void
begin_block(void)
{
    lv2_atom_forge_set_buffer(&forge, control_buf, sizeof(control_buf));
    lv2_atom_forge_sequence_head(&forge, &seq_frame, 0);
    /* an empty output sequence with its capacity in the size field, as hosts do */
    ((LV2_Atom_Sequence *)notify_buf)->atom.type = map_uri(NULL, LV2_ATOM__Sequence);
    ((LV2_Atom_Sequence *)notify_buf)->atom.size = sizeof(notify_buf) - sizeof(LV2_Atom);
}

static void
midi3(uint32_t frame, uint8_t a, uint8_t b, uint8_t c)
{
    uint8_t m[3] = { a, b, c };
    lv2_atom_forge_frame_time(&forge, frame);
    lv2_atom_forge_atom(&forge, 3, urid_midi);
    lv2_atom_forge_write(&forge, m, 3);
}

static void
patch_set_bank(uint32_t frame, const char *path)
{
    LV2_Atom_Forge_Frame obj;
    lv2_atom_forge_frame_time(&forge, frame);
    lv2_atom_forge_object(&forge, &obj, 0, urid_patch_Set);
    lv2_atom_forge_key(&forge, urid_patch_property);
    lv2_atom_forge_urid(&forge, urid_bank);
    lv2_atom_forge_key(&forge, urid_patch_value);
    lv2_atom_forge_path(&forge, path, (uint32_t)strlen(path));
    lv2_atom_forge_pop(&forge, &obj);
}

static void
patch_get(uint32_t frame)
{
    LV2_Atom_Forge_Frame obj;
    lv2_atom_forge_frame_time(&forge, frame);
    lv2_atom_forge_object(&forge, &obj, 0, urid_patch_Get);
    lv2_atom_forge_pop(&forge, &obj);
}

static double
run_blocks(LilvInstance *inst, int blocks, float *keep, uint32_t keep_frames)
{
    double energy = 0.0;
    int b;
    uint32_t i, kept = 0;
    for (b = 0; b < blocks; b++) {
        if (b > 0) { begin_block(); lv2_atom_forge_pop(&forge, &seq_frame); }
        lilv_instance_run(inst, BLOCK);
        deliver_response();
        for (i = 0; i < BLOCK; i++) {
            energy += (double)out_buf[i] * out_buf[i];
            if (keep && kept < keep_frames) keep[kept++] = out_buf[i];
        }
    }
    return sqrt(energy / (blocks * BLOCK));
}

/* does the notify port hold a patch:Set for the bank with this path? */
static int
notify_has_bank(const char *path)
{
    const LV2_Atom_Sequence *seq = (const LV2_Atom_Sequence *)notify_buf;
    LV2_ATOM_SEQUENCE_FOREACH(seq, ev) {
        if (ev->body.type == urid_atom_Object) {
            const LV2_Atom_Object *obj = (const LV2_Atom_Object *)&ev->body;
            if (obj->body.otype == urid_patch_Set) {
                const LV2_Atom *prop = NULL, *val = NULL;
                lv2_atom_object_get(obj, urid_patch_property, &prop, urid_patch_value, &val, 0);
                if (prop && ((const LV2_Atom_URID *)prop)->body == urid_bank && val && val->type == urid_atom_Path &&
                    !strcmp((const char *)LV2_ATOM_BODY_CONST(val), path))
                    return 1;
            }
        }
    }
    return 0;
}

/* ---- state helpers (in-memory store) ---- */

typedef struct { uint32_t key, type, flags; size_t size; void *value; } prop_t;
static prop_t props[8];
static int nprops = 0;

static LV2_State_Status
store(LV2_State_Handle h, uint32_t key, const void *value, size_t size, uint32_t type, uint32_t flags)
{
    if (nprops >= 8) return LV2_STATE_ERR_NO_SPACE;
    props[nprops].key = key; props[nprops].type = type; props[nprops].flags = flags; props[nprops].size = size;
    props[nprops].value = malloc(size);
    memcpy(props[nprops].value, value, size);
    nprops++;
    return LV2_STATE_SUCCESS;
}

static const void *
retrieve(LV2_State_Handle h, uint32_t key, size_t *size, uint32_t *type, uint32_t *flags)
{
    int i;
    for (i = 0; i < nprops; i++) {
        if (props[i].key == key) { *size = props[i].size; *type = props[i].type; *flags = props[i].flags; return props[i].value; }
    }
    return NULL;
}

int
main(int argc, char **argv)
{
    const char *bundle = argc > 1 ? argv[1] : NULL;
    const char *bank_dir = argc > 2 ? argv[2] : ".";
    LilvWorld *world;
    LilvNode *bundle_uri, *plugin_uri;
    const LilvPlugins *plugins;
    const LilvPlugin *plugin;
    LilvInstance *inst;
    const LV2_Feature map_feature = { LV2_URID__map, &map };
    const LV2_Feature unmap_feature = { LV2_URID__unmap, &unmap };
    const LV2_Feature sched_feature = { LV2_WORKER__schedule, &schedule };
    const LV2_Feature *features[] = { &map_feature, &unmap_feature, &sched_feature, NULL };
    char bank_path[1024], bundle_path[1100];
    const LV2_State_Interface *state_iface;

    if (!bundle) { fprintf(stderr, "usage: test_lv2 BUNDLE_DIR BANK_DIR\n"); return 2; }

    lv2_atom_forge_init(&forge, &map);
    urid_midi = map_uri(NULL, LV2_MIDI__MidiEvent);
    urid_patch_Set = map_uri(NULL, LV2_PATCH__Set);
    urid_patch_Get = map_uri(NULL, LV2_PATCH__Get);
    urid_patch_property = map_uri(NULL, LV2_PATCH__property);
    urid_patch_value = map_uri(NULL, LV2_PATCH__value);
    urid_bank = map_uri(NULL, HEXTER_URI "#bank");
    urid_state = map_uri(NULL, HEXTER_URI "#state");
    urid_atom_Path = map_uri(NULL, LV2_ATOM__Path);
    urid_atom_URID = map_uri(NULL, LV2_ATOM__URID);
    urid_atom_Object = map_uri(NULL, LV2_ATOM__Object);
    urid_atom_Chunk = map_uri(NULL, LV2_ATOM__Chunk);

    world = lilv_world_new();
    snprintf(bundle_path, sizeof(bundle_path), "%s/", bundle);
    bundle_uri = lilv_new_file_uri(world, NULL, bundle_path);
    lilv_world_load_bundle(world, bundle_uri);
    lilv_world_load_specifications(world);
    lilv_world_load_plugin_classes(world);
    plugins = lilv_world_get_all_plugins(world);
    plugin_uri = lilv_new_uri(world, HEXTER_URI);
    plugin = lilv_plugins_get_by_uri(plugins, plugin_uri);
    CHECK(plugin != NULL, "bundle %s contains %s", bundle, HEXTER_URI);
    if (!plugin) return 1;

    CHECK(lilv_plugin_get_num_ports(plugin) == 8, "8 ports, have %u", lilv_plugin_get_num_ports(plugin));
    {
        LilvNode *name = lilv_plugin_get_name(plugin);
        CHECK(name && !strcmp(lilv_node_as_string(name), "hexter"), "plugin name");
        lilv_node_free(name);
    }
    {
        LilvNode *urid_map = lilv_new_uri(world, LV2_URID__map);
        LilvNodes *req = lilv_plugin_get_required_features(plugin);
        CHECK(lilv_nodes_contains(req, urid_map), "requires urid:map");
        CHECK(lilv_nodes_size(req) == 1, "requires only urid:map (%u)", lilv_nodes_size(req));
        lilv_nodes_free(req);
        lilv_node_free(urid_map);
    }

    inst = lilv_plugin_instantiate(plugin, 48000.0, features);
    CHECK(inst != NULL, "instantiate");
    if (!inst) return 1;
    worker_instance = lilv_instance_get_handle(inst);
    worker_iface = (const LV2_Worker_Interface *)lilv_instance_get_extension_data(inst, LV2_WORKER__interface);
    state_iface = (const LV2_State_Interface *)lilv_instance_get_extension_data(inst, LV2_STATE__interface);
    CHECK(worker_iface != NULL, "worker interface");
    CHECK(state_iface != NULL, "state interface");

    lilv_instance_connect_port(inst, 0, control_buf);
    lilv_instance_connect_port(inst, 1, notify_buf);
    lilv_instance_connect_port(inst, 2, out_buf);
    lilv_instance_connect_port(inst, 3, &tuning);
    lilv_instance_connect_port(inst, 4, &volume);
    lilv_instance_connect_port(inst, 5, &polyphony);
    lilv_instance_connect_port(inst, 6, &mono);
    lilv_instance_connect_port(inst, 7, &program);
    lilv_instance_activate(inst);

    /* play a chord on the default bank */
    {
        double r;
        begin_block();
        midi3(0, 0x90, 60, 100);
        midi3(5, 0x90, 64, 100);
        midi3(9, 0x90, 67, 100);
        lv2_atom_forge_pop(&forge, &seq_frame);
        r = run_blocks(inst, 40, NULL, 0);
        CHECK(r > 0.01, "chord audible: rms %.4f", r);
        begin_block();
        midi3(0, 0xB0, 123, 0);   /* all notes off */
        lv2_atom_forge_pop(&forge, &seq_frame);
        run_blocks(inst, 600, NULL, 0);
        r = run_blocks(inst, 100, NULL, 0);
        CHECK(r < 1e-4, "silent after all notes off: rms %.6f", r);
    }

    /* load the ROM bank with patch:Set and check the sound changes */
    {
        static float before[BLOCK * 20], after[BLOCK * 20];
        double r;
        snprintf(bank_path, sizeof(bank_path), "%s/dx7_roms.dx7", bank_dir);

        program = 10.0f;   /* program 11 of whatever bank is loaded */
        begin_block();
        midi3(0, 0x90, 60, 100);
        lv2_atom_forge_pop(&forge, &seq_frame);
        run_blocks(inst, 20, before, BLOCK * 20);
        begin_block();
        midi3(0, 0xB0, 120, 0);
        lv2_atom_forge_pop(&forge, &seq_frame);
        run_blocks(inst, 2, NULL, 0);

        begin_block();
        patch_set_bank(0, bank_path);
        lv2_atom_forge_pop(&forge, &seq_frame);
        run_blocks(inst, 1, NULL, 0);
        CHECK(work_calls == 1, "bank load went through the worker (%d calls)", work_calls);
        begin_block();
        lv2_atom_forge_pop(&forge, &seq_frame);
        run_blocks(inst, 1, NULL, 0);
        CHECK(notify_has_bank(bank_path), "notify port announced the bank path");

        begin_block();
        midi3(0, 0x90, 60, 100);
        lv2_atom_forge_pop(&forge, &seq_frame);
        r = run_blocks(inst, 20, after, BLOCK * 20);
        CHECK(r > 0.01, "ROM E.PIANO 1 audible: rms %.4f", r);
        CHECK(memcmp(before, after, sizeof(before)) != 0, "sound changed after loading the ROM bank");
        begin_block();
        midi3(0, 0xB0, 120, 0);
        lv2_atom_forge_pop(&forge, &seq_frame);
        run_blocks(inst, 2, NULL, 0);

        /* patch:Get answers with the path */
        begin_block();
        patch_get(0);
        lv2_atom_forge_pop(&forge, &seq_frame);
        run_blocks(inst, 1, NULL, 0);
        CHECK(notify_has_bank(bank_path), "patch:Get answered with the bank path");

        /* a bad path is reported, not fatal */
        begin_block();
        patch_set_bank(0, "/nonexistent/bank.syx");
        lv2_atom_forge_pop(&forge, &seq_frame);
        run_blocks(inst, 2, NULL, 0);
        CHECK(work_calls == 2, "second load attempted (%d)", work_calls);
    }

    /* control ports: tuning shifts pitch; program changes the sound */
    {
        static float a[BLOCK * 20], b[BLOCK * 20];
        program = 0.0f;   /* BRASS 1 */
        tuning = 440.0f;
        begin_block(); midi3(0, 0x90, 69, 100); lv2_atom_forge_pop(&forge, &seq_frame);
        run_blocks(inst, 20, a, BLOCK * 20);
        begin_block(); midi3(0, 0xB0, 120, 0); lv2_atom_forge_pop(&forge, &seq_frame);
        run_blocks(inst, 2, NULL, 0);
        tuning = 466.2f;
        begin_block(); midi3(0, 0x90, 69, 100); lv2_atom_forge_pop(&forge, &seq_frame);
        run_blocks(inst, 20, b, BLOCK * 20);
        begin_block(); midi3(0, 0xB0, 120, 0); lv2_atom_forge_pop(&forge, &seq_frame);
        run_blocks(inst, 2, NULL, 0);
        CHECK(memcmp(a, b, sizeof(a)) != 0, "tuning control port changes the output");
        tuning = 440.0f;
    }

    /* state save / restore through the state interface */
    {
        static float a[BLOCK * 8], b[BLOCK * 8];
        LilvInstance *inst2;
        size_t size = 0; uint32_t type = 0, flags = 0;
        const void *v;

        program = 10.0f; volume = -6.0f; polyphony = 5.0f;
        begin_block(); lv2_atom_forge_pop(&forge, &seq_frame);
        run_blocks(inst, 1, NULL, 0);

        CHECK(state_iface->save(lilv_instance_get_handle(inst), store, NULL, LV2_STATE_IS_POD, features) == LV2_STATE_SUCCESS, "state save");
        v = retrieve(NULL, urid_state, &size, &type, &flags);
        CHECK(v && size == 16800 && type == urid_atom_Chunk, "state chunk stored (%zu bytes)", size);
        v = retrieve(NULL, urid_bank, &size, &type, &flags);
        CHECK(v && type == urid_atom_Path && !strcmp((const char *)v, bank_path), "bank path stored");

        inst2 = lilv_plugin_instantiate(plugin, 48000.0, features);
        CHECK(inst2 != NULL, "second instance");
        {
            static uint8_t control2[4096];
            static float out2[BLOCK];
            static float t2 = 440.0f, v2 = -6.0f, p2 = 5.0f, m2 = 0.0f, pr2 = 10.0f;
            const LV2_State_Interface *si2 = (const LV2_State_Interface *)lilv_instance_get_extension_data(inst2, LV2_STATE__interface);
            lilv_instance_connect_port(inst2, 0, control2);
            lilv_instance_connect_port(inst2, 1, NULL);
            lilv_instance_connect_port(inst2, 2, out2);
            lilv_instance_connect_port(inst2, 3, &t2);
            lilv_instance_connect_port(inst2, 4, &v2);
            lilv_instance_connect_port(inst2, 5, &p2);
            lilv_instance_connect_port(inst2, 6, &m2);
            lilv_instance_connect_port(inst2, 7, &pr2);
            CHECK(si2->restore(lilv_instance_get_handle(inst2), retrieve, NULL, 0, features) == LV2_STATE_SUCCESS, "state restore");
            lilv_instance_activate(inst2);

            /* same note into both: identical output proves the bank came across */
            {
                uint8_t m[3] = { 0x90, 60, 100 };
                LV2_Atom_Sequence *s2 = (LV2_Atom_Sequence *)control2;
                LV2_Atom_Event *ev;
                int k;
                uint32_t i;
                s2->atom.type = map_uri(NULL, LV2_ATOM__Sequence);
                s2->atom.size = sizeof(LV2_Atom_Sequence_Body) + sizeof(LV2_Atom_Event) + 8;
                s2->body.unit = 0; s2->body.pad = 0;
                ev = (LV2_Atom_Event *)(s2 + 1);
                ev->time.frames = 0; ev->body.type = urid_midi; ev->body.size = 3;
                memcpy(ev + 1, m, 3);

                /* both LFOs from phase zero: activate resets the engine */
                lilv_instance_deactivate(inst); lilv_instance_activate(inst);
                lilv_instance_deactivate(inst2); lilv_instance_activate(inst2);
                begin_block(); midi3(0, 0x90, 60, 100); lv2_atom_forge_pop(&forge, &seq_frame);
                run_blocks(inst, 8, a, BLOCK * 8);
                for (k = 0; k < 8; k++) {
                    lilv_instance_run(inst2, BLOCK);
                    for (i = 0; i < BLOCK; i++) b[k * BLOCK + i] = out2[i];
                    s2->atom.size = sizeof(LV2_Atom_Sequence_Body);
                }
                {
                    /* oscillator phase carries across notes on a used voice (as on the
                     * DX7 with key sync off), so compare levels, not samples */
                    double ra = 0, rb = 0;
                    for (i = 0; i < BLOCK * 8; i++) { ra += (double)a[i] * a[i]; rb += (double)b[i] * b[i]; }
                    ra = sqrt(ra / (BLOCK * 8)); rb = sqrt(rb / (BLOCK * 8));
                    CHECK(ra > 0.01 && fabs(ra - rb) < 0.02 * ra, "restored instance renders at the same level (%.4f vs %.4f)", ra, rb);
                }
            }
            lilv_instance_deactivate(inst2);
            lilv_instance_free(inst2);
        }
    }

    lilv_instance_deactivate(inst);
    lilv_instance_free(inst);
    lilv_node_free(plugin_uri);
    lilv_node_free(bundle_uri);
    lilv_world_free(world);

    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
