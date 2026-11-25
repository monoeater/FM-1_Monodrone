/* SPDX-License-Identifier: MPL-2.0 */
/* Host check of the drone DSP: renders each engine to WAV (three settings of its knobs on one note, then the same
 * as a three-note chord), and a scene with the punches; prints level, peak, NaNs and the cost on this host.
 *   cc -O2 -ffp-contract=off -DPLAT_HOST -Ibase/include -Iapp -o dsp_render tools/dsp_render.c app/drone_dsp.c -lm
 *   ./dsp_render OUTDIR */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include "drone_dsp.h"
int plat_cpu2_run(void (*f)(uint32_t), uint32_t a) { (void)f; (void)a; return 0; }
int plat_cpu2_wait(void) { return 0; }

static void wav(const char *path, const float *x, unsigned frames)
{
    FILE *f = fopen(path, "wb");
    unsigned data = frames * 4, v;
    fwrite("RIFF", 1, 4, f); v = 36 + data; fwrite(&v, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
    v = 16; fwrite(&v, 4, 1, f);
    short s16[2] = {1, 2}; fwrite(s16, 2, 2, f);
    v = 44100; fwrite(&v, 4, 1, f); v = 44100 * 4; fwrite(&v, 4, 1, f);
    s16[0] = 4; s16[1] = 16; fwrite(s16, 2, 2, f);
    fwrite("data", 1, 4, f); fwrite(&data, 4, 1, f);
    for (unsigned i = 0; i < frames * 2; i++) { float y = x[i] * 32767.0f; short q = y > 32767 ? 32767 : y < -32768 ? -32768 : (short)y; fwrite(&q, 2, 1, f); }
    fclose(f);
}
static float *buf; static unsigned frames;
static double cost;
static void run(float secs)
{
    unsigned n = (unsigned)(secs * 44100.0f) / 256u * 256u;
    for (unsigned i = 0; i < n; i += 256) {
        clock_t t0 = clock();
        dr_render(buf + 2 * frames, 256);
        cost += (double)(clock() - t0) / CLOCKS_PER_SEC;
        frames += 256;
    }
}
static void report(const char *name, unsigned from)
{
    double s = 0; float pk = 0; unsigned nan = 0;
    for (unsigned i = 2 * from; i < 2 * frames; i++) { float v = buf[i]; if (!isfinite(v)) nan++; else { s += v * v; if (fabsf(v) > pk) pk = fabsf(v); } }
    printf("%-22s rms %6.1f dB  peak %6.1f dB  nan %u\n", name, 10 * log10(s / (2.0 * (frames - from)) + 1e-20), 20 * log10(pk + 1e-20), nan);
}
static void chord(volatile dr_layer_t *l, int a, int b, int c)
{
    l->note[0] = (uint8_t)a; l->note[1] = (uint8_t)b; l->note[2] = (uint8_t)c; l->nv = c ? 3 : 1;
}
int main(int argc, char **argv)
{
    const char *out = argc > 1 ? argv[1] : ".";
    static const char *EN[] = {"hive", "swarm", "rot", "wind", "howl", "geiger", "smear", "hum", "scrape", "choir",
                               "static", "chaos"};
    char path[512];
    buf = malloc(sizeof(float) * 2 * 44100 * 60);
    for (int e = 0; e < DR_ENGINES; e++) {
        dr_defaults((dr_ctl_t *)&dr_ctl);
        dr_ctl.glob[GL_HISS] = 0;
        dr_ctl.L[0].engine = e; chord(&dr_ctl.L[0], 41, 0, 0); dr_ctl.L[0].swell = 10;
        dr_init();
        frames = 0;
        if (e == DR_SMEAR) {                 /* something to smear: a HIVE chord on layer 2 for 3 s first */
            dr_ctl.L[1].engine = DR_HIVE; chord(&dr_ctl.L[1], 52, 57, 64); dr_ctl.L[1].on = 1; dr_ctl.L[1].swell = 10; run(3.0f);
            dr_ctl.L[1].on = 0; dr_ctl.capture = 0;
        }
        unsigned from = frames;
        dr_ctl.L[0].on = 1;
        static const unsigned char set[3][3] = {{20, 30, 20}, {64, 64, 64}, {120, 110, 120}};
        for (int s = 0; s < 3; s++) {
            for (int k = 0; k < 3; k++) dr_ctl.L[0].p[k] = set[s][k];
            run(4.0f);
        }
        dr_ctl.L[0].p[0] = dr_ctl.L[0].p[1] = dr_ctl.L[0].p[2] = 64;
        chord(&dr_ctl.L[0], 41, 48, 51);     /* the chord: F2, C3, D#3 */
        run(5.0f);
        snprintf(path, sizeof path, "%s/eng_%s.wav", out, EN[e]);
        wav(path, buf + 2 * from, frames - from);
        report(EN[e], from);
    }
    /* the default scene, all four on (chords on two), then each punch, freeze */
    dr_defaults((dr_ctl_t *)&dr_ctl); dr_init(); frames = 0; cost = 0;
    for (int i = 0; i < 4; i++) dr_ctl.L[i].on = 1;
    chord(&dr_ctl.L[0], 33, 40, 45); chord(&dr_ctl.L[1], 52, 55, 59);
    run(8.0f); report("scene", 0);
    unsigned m = frames; dr_ctl.punch = DR_PUNCH_BURN; run(2.0f); dr_ctl.punch = 0; run(1.0f); report("burn", m);
    m = frames; dr_ctl.punch = DR_PUNCH_VOID; run(4.0f); dr_ctl.punch = 0; run(2.0f); report("void", m);
    m = frames; dr_ctl.punch = DR_PUNCH_SHATTER; run(3.0f); dr_ctl.punch = 0; run(1.0f); report("shatter", m);
    m = frames; dr_ctl.freeze = 1; run(1.0f); for (int i = 0; i < 4; i++) dr_ctl.L[i].on = 0; run(5.0f); report("freeze", m);
    dr_ctl.freeze = 0; run(3.0f);
    double audio = frames / 44100.0;
    printf("scene render: %.3f s for %.1f s of audio on this host (%.2f %%)\n", cost, audio, 100 * cost / audio);
    snprintf(path, sizeof path, "%s/scene.wav", out);
    wav(path, buf, frames);
    return 0;
}
