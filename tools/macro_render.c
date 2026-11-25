/* SPDX-License-Identifier: MPL-2.0 */
/* Host check of the MACRO knobs: each factory-like scene, each macro swept 0 -> 127 over 6 s, held 4 s, back over 2 s;
 * prints the level before, at full and after, and writes macro_NAME.wav.
 *   cc -O2 -ffp-contract=off -DPLAT_HOST -Ibase/include -Iapp -o macro_render tools/macro_render.c app/drone_dsp.c -lm */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "drone_dsp.h"
int plat_cpu2_run(void (*f)(uint32_t), uint32_t a) { (void)f; (void)a; return 0; }
int plat_cpu2_wait(void) { return 0; }
static float buf[2 * 44100 * 20]; static unsigned fr;
static void wav(const char *path, const float *x, unsigned frames)
{
    FILE *f = fopen(path, "wb"); unsigned data = frames * 4, v; short s16[2] = {1, 2};
    fwrite("RIFF", 1, 4, f); v = 36 + data; fwrite(&v, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f); v = 16; fwrite(&v, 4, 1, f);
    fwrite(s16, 2, 2, f); v = 44100; fwrite(&v, 4, 1, f); v = 44100 * 4; fwrite(&v, 4, 1, f);
    s16[0] = 4; s16[1] = 16; fwrite(s16, 2, 2, f); fwrite("data", 1, 4, f); fwrite(&data, 4, 1, f);
    for (unsigned i = 0; i < frames * 2; i++) { float y = x[i] * 32767.0f; short q = y > 32767 ? 32767 : y < -32768 ? -32768 : (short)y; fwrite(&q, 2, 1, f); }
    fclose(f);
}
static double rms(unsigned a, unsigned b) { double s = 0; for (unsigned i = 2 * a; i < 2 * b; i++) s += buf[i] * buf[i]; return 10 * log10(s / (2.0 * (b - a)) + 1e-20); }
static void run(float secs, int m, float from, float to)
{
    unsigned n = (unsigned)(secs * 44100.0f) / 256u * 256u;
    for (unsigned i = 0; i < n; i += 256) { float t = (float)i / n; dr_ctl.macro[m] = (uint8_t)(from + (to - from) * t); dr_render(buf + 2 * fr, 256); fr += 256; }
    dr_ctl.macro[m] = (uint8_t)to;
}
int main(int argc, char **argv)
{
    static const char *N[MC_N] = {"rot", "space", "unrest", "starve", "tangle", "runaway", "runoff", "deluge"}; char p[512];
    for (int m = 0; m < MC_N; m++) {
        dr_defaults((dr_ctl_t *)&dr_ctl); dr_init(); fr = 0;
        for (int i = 0; i < 4; i++) dr_ctl.L[i].on = 1;
        dr_ctl.L[0].note[0] = 33; dr_ctl.L[0].note[1] = 40; dr_ctl.L[0].nv = 2;
        run(4, m, 0, 0); unsigned a = fr; run(6, m, 0, 127); unsigned b = fr; run(4, m, 127, 127); unsigned c = fr;
        run(2, m, 127, 0); run(3, m, 0, 0);
        printf("%-7s before %6.1f  full %6.1f  after %6.1f dB\n", N[m], rms(a - 44100 * 2, a), rms(b, c), rms(fr - 44100, fr));
        snprintf(p, sizeof p, "%s/macro_%s.wav", argc > 1 ? argv[1] : ".", N[m]); wav(p, buf, fr);
    }
    return 0;
}
