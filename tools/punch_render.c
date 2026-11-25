/* SPDX-License-Identifier: MPL-2.0 */
/* Host check of the punches: a scene for 3 s, each punch held 8 s, then 3 s after; one WAV each, and the levels
 * before, during and after.   ./punch_render OUTDIR */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "drone_dsp.h"
int plat_cpu2_run(void (*f)(uint32_t), uint32_t a) { (void)f; (void)a; return 0; }
int plat_cpu2_wait(void) { return 0; }
static float *buf; static unsigned frames;
static void run(float secs) { unsigned n = (unsigned)(secs * 44100.0f) / 256u * 256u; for (unsigned i = 0; i < n; i += 256) { dr_render(buf + 2 * frames, 256); frames += 256; } }
static double rms(unsigned a, unsigned b) { double s = 0; for (unsigned i = 2 * a; i < 2 * b; i++) s += buf[i] * buf[i]; return 10 * log10(s / (2.0 * (b - a)) + 1e-20); }
static void wav(const char *path, const float *x, unsigned n)
{
    FILE *f = fopen(path, "wb"); unsigned data = n * 4, v; short s16[2];
    fwrite("RIFF", 1, 4, f); v = 36 + data; fwrite(&v, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f); v = 16; fwrite(&v, 4, 1, f);
    s16[0] = 1; s16[1] = 2; fwrite(s16, 2, 2, f); v = 44100; fwrite(&v, 4, 1, f); v = 176400; fwrite(&v, 4, 1, f);
    s16[0] = 4; s16[1] = 16; fwrite(s16, 2, 2, f); fwrite("data", 1, 4, f); fwrite(&data, 4, 1, f);
    for (unsigned i = 0; i < n * 2; i++) { float y = x[i] * 32767.0f; short q = y > 32767 ? 32767 : y < -32768 ? -32768 : (short)y; fwrite(&q, 2, 1, f); }
    fclose(f);
}
int main(int argc, char **argv)
{
    static const char *PN[PU_N] = {"burn", "void", "shatter", "warble", "infinite", "shimmer", "throw", "ring", "decimate", "gate", "scream", "rotate"};
    char path[256];
    buf = malloc(sizeof(float) * 2 * 44100 * 16);
    for (int p = 0; p < PU_N; p++) {
        dr_defaults((dr_ctl_t *)&dr_ctl); dr_init(); frames = 0;
        for (int i = 0; i < 4; i++) { dr_ctl.L[i].on = 1; dr_ctl.L[i].swell = 20; }
        dr_ctl.L[0].note[1] = 40; dr_ctl.L[0].nv = 2;
        run(3.0f); unsigned a = frames;
        dr_ctl.punch = (uint16_t)(1u << p); run(8.0f); unsigned b = frames;
        dr_ctl.punch = 0; run(3.0f);
        int nan = 0; for (unsigned i = 0; i < frames * 2; i++) if (!isfinite(buf[i])) nan++;
        printf("%-9s before %6.1f  held %6.1f  after %6.1f dB  nan %d\n", PN[p], rms(a / 2, a), rms(a + 22050, b), rms(b + 44100, frames), nan);
        snprintf(path, sizeof path, "%s/punch_%02d_%s.wav", argc > 1 ? argv[1] : ".", p + 1, PN[p]);
        wav(path, buf, frames);
    }
    return 0;
}
