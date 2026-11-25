/* SPDX-License-Identifier: MPL-2.0 */
/* Exports the scene strip's pictures for editing: for each scene, its place as drawn (base), the mask (where something
 * stands; sky = 0) and FRAMES frames of the motion over it (dt 3 = about 48 ms each, mid level, no wind), as PBM.
 * White (1) is ink, as on the screen.
 *   cc -O2 -DPLAT_HOST -Ibase/include -Iapp -o land_frames tools/land_frames.c && ./land_frames OUTDIR */
#include <stdio.h>
#include <string.h>
#include "drone_dsp.h"
#include "land.h"
#define FRAMES 48
static void pbm(const char *path, uint8_t (*b)[LROW])
{
    FILE *f = fopen(path, "wb");
    fprintf(f, "P4\n%d %d\n", LW, LH);
    for (int y = 0; y < LH; y++) for (int i = 0; i < LROW; i++) fputc(b[y][i] ^ 0xFFu, f);   /* (PBM: 1 = black) */
    fclose(f);
}
int main(int argc, char **argv)
{
    /* each scene's place (app.c LANDMAP) and its seed (the scene's number) */
    static const uint8_t PLACE[16] = {LAND_REACTOR, LAND_PEAKS, LAND_DUNES, LAND_BUNKER, LAND_PEAKS, LAND_FACTORY,
                                      LAND_REACTOR, LAND_DUNES, LAND_REACTOR, LAND_FACTORY, LAND_CHANNEL, LAND_BUNKER,
                                      LAND_CONTROL, LAND_DUNES, LAND_PEAKS, LAND_FACTORY};
    char p[512];
    for (int s = 0; s < 16; s++) {
        memset(ash, 0, sizeof ash); memset(smoke, 0, sizeof smoke); memset(sparks, 0, sizeof sparks);
        cloud_x = 0; land_t = 0; land_events = 0; spark_n = 0;
        land_make(PLACE[s], (uint32_t)s + 1u);
        snprintf(p, sizeof p, "%s/%02d_base.pbm", argv[1], s + 1); pbm(p, lv);
        snprintf(p, sizeof p, "%s/%02d_mask.pbm", argv[1], s + 1); pbm(p, lm);
        for (int k = 0; k < 40; k++) land_frame(3, 128, 0, 0, 0, 0);        /* (the smoke and ash under way first) */
        uint32_t ev = 0;
        for (int k = 0; k < FRAMES; k++) {
            if (k % 7 == 3) ev++;                                           /* a Geiger spark now and then */
            land_frame(3, 128, 0, ev, 0, 0);
            snprintf(p, sizeof p, "%s/%02d_f%02d.pbm", argv[1], s + 1, k); pbm(p, lw);
        }
    }
    return 0;
}
