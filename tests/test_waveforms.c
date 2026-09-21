/* hexter: the eight operator waveforms, in detail.
 *
 * Copyright (C) 2026 Keith Adler. GPL-2.0-or-later.
 *
 * Shape 0 is the sine the DX7 has always had and must stay exactly that.
 * The other seven are derived the way ymfm derives them, and each has a shape
 * that can be described and therefore checked rather than admired.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "dx7_voice.h"
#include "hexter_engine.h"

static int checks = 0, failures = 0;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; \
    printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static double
sample(int wave, int i)
{
#ifdef HEXTER_USE_FLOATING_POINT
    return (double)dx7_voice_wave_table[wave][i];
#else
    return (double)dx7_voice_wave_table[wave][i] / (double)(1 << FP_SHIFT);
#endif
}

/* ---- the tables themselves ---- */

static void
test_tables(void)
{
    int w, i, half = SINE_SIZE / 2;

    dx7_voice_init_tables();

    /* shape 0 is the sine table, entry for entry, because a DX7 patch has to
     * render exactly as it did before any of this existed */
    {
        int same = 1;
        for (i = 0; i <= SINE_SIZE; i++)
            if (dx7_voice_wave_table[0][i] != dx7_voice_sin_table[i]) { same = 0; break; }
        CHECK(same, "shape 0 is the original sine table (first difference at %d)", i);
    }

    /* every table has its interpolation guard entry */
    for (w = 0; w < DX7_WAVEFORMS; w++)
        CHECK(dx7_voice_wave_table[w][SINE_SIZE] == dx7_voice_wave_table[w][0],
              "shape %d wraps at the end for interpolation", w);

    /* shapes 2 to 7 silence the second half of the cycle */
    for (w = 2; w < DX7_WAVEFORMS; w++) {
        int quiet = 1;
        for (i = half; i < SINE_SIZE; i++)
            if (fabs(sample(w, i)) > 1e-6) { quiet = 0; break; }
        CHECK(quiet, "shape %d is silent through its second half (sample %d)", w, i);
    }

    /* shapes 0 and 1 are not */
    for (w = 0; w < 2; w++) {
        double energy = 0.0;
        for (i = half; i < SINE_SIZE; i++) energy += fabs(sample(w, i));
        CHECK(energy > 1.0, "shape %d uses its whole cycle (energy %.2f)", w, energy);
    }

    /*
     * None of them may be louder than the sine. That is the bound that matters:
     * a shape that peaked higher would make every patch using it louder than
     * the same patch on a DX7, and it is measured against shape 0 rather than
     * against a number, because shape 0 is by definition right.
     */
    {
        double sine_peak = 0.0;
        for (i = 0; i <= SINE_SIZE; i++)
            if (fabs(sample(0, i)) > sine_peak) sine_peak = fabs(sample(0, i));
        CHECK(sine_peak > 0.5, "the sine table has a sensible peak (%.4f)", sine_peak);

        for (w = 0; w < DX7_WAVEFORMS; w++) {
            double peak = 0.0;
            for (i = 0; i <= SINE_SIZE; i++)
                if (fabs(sample(w, i)) > peak) peak = fabs(sample(w, i));
            CHECK(peak <= sine_peak * 1.0001,
                  "shape %d is no louder than the sine (%.4f against %.4f)",
                  w, peak, sine_peak);
            CHECK(peak > sine_peak * 0.2, "and shape %d is not nearly empty (%.4f)", w, peak);
        }
    }

    /* shape 1 is the sine squared, keeping its sign: everywhere the sine is
     * positive so is shape 1, and it is never larger */
    {
        int sign_ok = 1, smaller = 1;
        for (i = 0; i < SINE_SIZE; i++) {
            double s = sample(0, i), q = sample(1, i);
            if (s > 1e-6 && q < -1e-6) sign_ok = 0;
            if (s < -1e-6 && q > 1e-6) sign_ok = 0;
            if (fabs(q) > fabs(s) + 1e-6) smaller = 0;
        }
        CHECK(sign_ok, "shape 1 keeps the sine's sign");
        CHECK(smaller, "and is never further from zero than the sine");
    }

    /* the eight are all different from one another */
    {
        int pairs = 0, x;
        for (w = 0; w < DX7_WAVEFORMS; w++)
            for (x = w + 1; x < DX7_WAVEFORMS; x++)
                if (memcmp(dx7_voice_wave_table[w], dx7_voice_wave_table[x],
                           sizeof(dx7_voice_wave_table[0]))) pairs++;
        CHECK(pairs == DX7_WAVEFORMS * (DX7_WAVEFORMS - 1) / 2,
              "all eight tables differ from one another (%d of %d pairs)",
              pairs, DX7_WAVEFORMS * (DX7_WAVEFORMS - 1) / 2);
    }

    /* every shape is built from a cosine, so every one starts where the sine
     * starts rather than somewhere of its own */
    for (w = 1; w < DX7_WAVEFORMS; w++)
        CHECK(fabs(sample(w, 0) - sample(0, 0)) < 0.001,
              "shape %d starts where the sine does (%.4f against %.4f)",
              w, sample(w, 0), sample(0, 0));
}

/* ---- the engine's side of it ---- */

static void
test_engine_side(void)
{
    hexter_engine_t *e = hexter_engine_new(44100.0f);
    uint8_t set[HEXTER_OPERATORS], got[HEXTER_OPERATORS];
    int i, w;

    /* a fresh engine is all sines */
    hexter_engine_get_op_waves(e, got);
    for (i = 0; i < HEXTER_OPERATORS; i++)
        CHECK(got[i] == 0, "operator %d starts as a sine (%d)", i + 1, got[i]);

    /* every shape on every operator reads back */
    for (w = 0; w < HEXTER_OP_WAVEFORMS; w++) {
        int wrong = -1;
        for (i = 0; i < HEXTER_OPERATORS; i++) set[i] = (uint8_t)w;
        hexter_engine_set_op_waves(e, set);
        hexter_engine_get_op_waves(e, got);
        for (i = 0; i < HEXTER_OPERATORS; i++)
            if (got[i] != w && wrong < 0) wrong = i;
        CHECK(wrong < 0, "shape %d reads back on every operator (operator %d)", w, wrong + 1);
    }

    /* each operator holds its own shape */
    for (i = 0; i < HEXTER_OPERATORS; i++) set[i] = (uint8_t)i;
    hexter_engine_set_op_waves(e, set);
    hexter_engine_get_op_waves(e, got);
    CHECK(!memcmp(set, got, HEXTER_OPERATORS), "six operators keep six different shapes");

    /* out of range wraps rather than reading off the end of the table */
    for (w = 0; w < 40; w++) {
        for (i = 0; i < HEXTER_OPERATORS; i++) set[i] = (uint8_t)w;
        hexter_engine_set_op_waves(e, set);
        hexter_engine_get_op_waves(e, got);
        if (got[0] != w % HEXTER_OP_WAVEFORMS) break;
    }
    CHECK(w == 40, "shapes past the eighth wrap (stopped at %d)", w);

    for (i = 0; i < HEXTER_OPERATORS; i++) set[i] = 255;
    hexter_engine_set_op_waves(e, set);
    hexter_engine_get_op_waves(e, got);
    CHECK(got[0] < HEXTER_OP_WAVEFORMS, "even 255 lands in range (%d)", got[0]);

    hexter_engine_free(e);
}

static void
test_programs_and_state(void)
{
    hexter_engine_t *a = hexter_engine_new(44100.0f);
    hexter_engine_t *b = hexter_engine_new(44100.0f);
    static uint8_t state[HEXTER_STATE_SIZE];
    uint8_t set[HEXTER_OPERATORS], got[HEXTER_OPERATORS];
    int i;

    /* a shape belongs to the program it was set on */
    for (i = 0; i < HEXTER_OPERATORS; i++) set[i] = (uint8_t)(i % HEXTER_OP_WAVEFORMS);
    hexter_engine_select_program(a, 9);
    hexter_engine_set_op_waves(a, set);

    hexter_engine_select_program(a, 10);
    hexter_engine_get_op_waves(a, got);
    {
        int all_sine = 1;
        for (i = 0; i < HEXTER_OPERATORS; i++) if (got[i]) all_sine = 0;
        CHECK(all_sine, "another program is unaffected");
    }
    hexter_engine_select_program(a, 9);
    hexter_engine_get_op_waves(a, got);
    CHECK(!memcmp(set, got, HEXTER_OPERATORS), "and coming back finds them again");

    /* through a saved state */
    CHECK(hexter_engine_state_save(a, state, sizeof(state)) == HEXTER_STATE_SIZE,
          "state saves");
    CHECK(hexter_engine_state_load(b, state, sizeof(state)), "and loads");
    hexter_engine_get_op_waves(b, got);
    CHECK(!memcmp(set, got, HEXTER_OPERATORS), "with the waveforms intact");
    CHECK(hexter_engine_get_program(b) == 9, "and the program (%d)",
          hexter_engine_get_program(b));

    /* a state from before waveforms existed still loads, all sines */
    {
        hexter_engine_t *c = hexter_engine_new(44100.0f);
        static uint8_t old_state[HEXTER_STATE_SIZE_V1];
        int all_sine = 1;

        memcpy(old_state, state, HEXTER_STATE_SIZE_V1);
        old_state[8]  = (HEXTER_STATE_SIZE_V1 - 12) & 0xff;
        old_state[9]  = ((HEXTER_STATE_SIZE_V1 - 12) >> 8) & 0xff;
        old_state[10] = old_state[11] = 0;
        CHECK(hexter_engine_state_load(c, old_state, sizeof(old_state)),
              "a state from before the waveforms still loads");
        hexter_engine_get_op_waves(c, got);
        for (i = 0; i < HEXTER_OPERATORS; i++) if (got[i]) all_sine = 0;
        CHECK(all_sine, "and every operator comes back a sine");
        hexter_engine_free(c);
    }

    hexter_engine_free(a);
    hexter_engine_free(b);
}

/* ---- and what it sounds like ---- */

static void
render(hexter_engine_t *e, float *out, unsigned long n)
{
    hexter_event_t note;
    memset(&note, 0, sizeof(note));
    note.type = HEXTER_EV_NOTE_ON; note.a = 60; note.b = 100;
    memset(out, 0, n * sizeof(float));
    hexter_engine_render(e, out, (uint32_t)n, &note, 1);
}

static void
test_sound(void)
{
    static float buf[HEXTER_OP_WAVEFORMS][2048];
    uint8_t set[HEXTER_OPERATORS];
    int w, x, i, pairs = 0;

    for (w = 0; w < HEXTER_OP_WAVEFORMS; w++) {
        hexter_engine_t *e = hexter_engine_new(44100.0f);
        double peak = 0.0;

        for (i = 0; i < HEXTER_OPERATORS; i++) set[i] = (uint8_t)w;
        hexter_engine_set_op_waves(e, set);
        render(e, buf[w], 2048);
        for (i = 0; i < 2048; i++) if (fabs(buf[w][i]) > peak) peak = fabs(buf[w][i]);
        CHECK(peak > 0.001, "shape %d makes a sound (peak %.5f)", w, peak);
        CHECK(peak <= 1.5, "and does not run away (peak %.5f)", peak);
        hexter_engine_free(e);
    }

    for (w = 0; w < HEXTER_OP_WAVEFORMS; w++)
        for (x = w + 1; x < HEXTER_OP_WAVEFORMS; x++)
            if (memcmp(buf[w], buf[x], sizeof(buf[0]))) pairs++;
    CHECK(pairs == HEXTER_OP_WAVEFORMS * (HEXTER_OP_WAVEFORMS - 1) / 2,
          "all eight sound different (%d of %d pairs)",
          pairs, HEXTER_OP_WAVEFORMS * (HEXTER_OP_WAVEFORMS - 1) / 2);

    /* and asking for the sine again gives back what a fresh engine gives */
    {
        hexter_engine_t *e = hexter_engine_new(44100.0f);
        static float plain[2048], again[2048];

        render(e, plain, 2048);
        hexter_engine_free(e);

        e = hexter_engine_new(44100.0f);
        for (i = 0; i < HEXTER_OPERATORS; i++) set[i] = 0;
        hexter_engine_set_op_waves(e, set);
        render(e, again, 2048);
        CHECK(!memcmp(plain, again, sizeof(plain)),
              "asking for shape 0 renders exactly what never asking does");
        hexter_engine_free(e);
    }
}

int
main(void)
{
    test_tables();
    test_engine_side();
    test_programs_and_state();
    test_sound();

    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
