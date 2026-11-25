/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Michael (MONODRONE) */
/* The scene strip: a 1-bit place, drawn in code when a scene is chosen, then animated each frame over the sound:
 * REACTOR (cooling towers, the block and its stack, pylons), DUNES (the Zone's sand room: pillars, a misty door, mounds
 * of sand, a lone figure), BUNKER (an old
 * train tunnel: a ribbed vault, lamps, rails, racks of boxes, a door), PEAKS (mountains, cloud) and FACTORY (a derelict hall, light
 * through the roof, hanging chains) and CONTROL (the dead control room: walls of empty dial sockets, the core's round
 * mimic board losing its tiles, curved consoles, rods hanging from the ceiling) and CHANNEL (a city's concrete drainage
 * channel under an expressway, rusted pipes, graffiti). Over it: drifting cloud, smoke, ash, Geiger sparks; the punches and FREEZE
 * show in it. Included by app.c (the UI side; gfx.c's canvas). */
#pragma once
#include <stdint.h>
#include "fastmath.h"

#define LW 240
#define LH 88
#define LROW (LW / 8)
enum { LAND_REACTOR, LAND_DUNES, LAND_BUNKER, LAND_PEAKS, LAND_FACTORY, LAND_CONTROL, LAND_CHANNEL, LAND_CITY, NLAND };
static const char *const LAND_NAME[NLAND] = {"REACTOR", "DUNES", "BUNKER", "PEAKS", "FACTORY", "CONTROL", "CHANNEL", "CITY"};

static uint8_t lv[LH][LROW];              /* the place: 1 = ink */
static uint8_t lm[LH][LROW];              /* 1 = something stands there (not sky): cloud and smoke go behind it */
static uint8_t lw[LH][LROW];              /* this frame's picture */
static uint32_t lrng = 1u;
static uint8_t land_kind = 0xFFu;

static const uint8_t BAYER[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
static inline uint32_t lr(void) { lrng ^= lrng << 13; lrng ^= lrng >> 17; lrng ^= lrng << 5; return lrng; }
static inline int32_t lri(int32_t lo, int32_t hi) { return lo + (int32_t)(lr() % (uint32_t)(hi - lo + 1)); }

/* a pixel of the place at a tone 0 (dark) .. 16 (ink), dithered; fg: it stands in front of the sky */
static inline void lset(uint8_t (*b)[LROW], int32_t x, int32_t y, int on)
{
    if ((uint32_t)x >= LW || (uint32_t)y >= LH) return;
    if (on) b[y][x >> 3] |= (uint8_t)(0x80u >> (x & 7));
    else b[y][x >> 3] &= (uint8_t)~(0x80u >> (x & 7));
}
static inline int lget(uint8_t (*b)[LROW], int32_t x, int32_t y)
{
    if ((uint32_t)x >= LW || (uint32_t)y >= LH) return 0;
    return (b[y][x >> 3] >> (7 - (x & 7))) & 1;
}
static inline void ltone(int32_t x, int32_t y, int32_t tone, int fg)
{
    if ((uint32_t)x >= LW || (uint32_t)y >= LH) return;
    lset(lv, x, y, tone > (int32_t)BAYER[y & 3][x & 3]);
    if (fg) lset(lm, x, y, 1);
}
static void lspan(int32_t x0, int32_t x1, int32_t y, int32_t tone, int fg)
{
    if (x0 > x1) { int32_t t = x0; x0 = x1; x1 = t; }
    for (int32_t x = x0; x <= x1; x++) ltone(x, y, tone, fg);
}
static void lrect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t tone, int fg)
{
    for (int32_t j = y; j < y + h; j++) lspan(x, x + w - 1, j, tone, fg);
}
static void lline(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t tone, int fg)
{
    int32_t dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? y0 - y1 : y1 - y0;
    int32_t sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, e = dx + dy;
    for (;;) {
        ltone(x0, y0, tone, fg);
        if (x0 == x1 && y0 == y1) break;
        int32_t e2 = 2 * e;
        if (e2 >= dy) { e += dy; x0 += sx; }
        if (e2 <= dx) { e += dx; y0 += sy; }
    }
}
/* a filled polygon (even-odd), up to 8 points */
static void lpoly(const int16_t *xy, int32_t n, int32_t tone, int fg)
{
    int32_t ymin = LH, ymax = -1;
    for (int32_t i = 0; i < n; i++) { if (xy[2 * i + 1] < ymin) ymin = xy[2 * i + 1]; if (xy[2 * i + 1] > ymax) ymax = xy[2 * i + 1]; }
    for (int32_t y = ymin < 0 ? 0 : ymin; y <= ymax && y < LH; y++) {
        int32_t xs[8], m = 0;
        for (int32_t i = 0; i < n && m < 8; i++) {
            int32_t x0 = xy[2 * i], y0 = xy[2 * i + 1], x1 = xy[2 * ((i + 1) % n)], y1 = xy[2 * ((i + 1) % n) + 1];
            if ((y0 <= y && y1 > y) || (y1 <= y && y0 > y)) xs[m++] = x0 + (y - y0) * (x1 - x0) / (y1 - y0);
        }
        for (int32_t a = 0; a < m; a++) for (int32_t b = a + 1; b < m; b++) if (xs[b] < xs[a]) { int32_t t = xs[a]; xs[a] = xs[b]; xs[b] = t; }
        for (int32_t a = 0; a + 1 < m; a += 2) lspan(xs[a], xs[a + 1], y, tone, fg);
    }
}

static inline int lchk(int32_t x, int32_t y) { return ((x + y) & 1) == 0; }
static void lstar(uint8_t (*b)[LROW], int32_t x, int32_t y, int big)
{
    lset(b, x, y, 1);
    if (big) { lset(b, x - 1, y, 1); lset(b, x + 1, y, 1); lset(b, x, y - 1, 1); lset(b, x, y + 1, 1); }
}
/* a night sky (the style's): black, crossed stars, a thin checker haze down to the horizon at row hz */
static void lnight(int32_t hz)
{
    lrect(0, 0, LW, LH, 0, 0);
    for (int32_t i = 0; i < 30; i++) lstar(lv, lri(2, LW - 3), lri(2, hz - 24), (lr() % 6u) == 0);
    for (int32_t y = hz - 8; y < hz; y++) for (int32_t x = 0; x < LW; x++) if (lchk(x, y) && (y & 1) && (y > hz - 4 || (x & 2))) lset(lv, x, y, 1);
}

/* ---------------------------------------------------------------------------------------------- the places --- */
/* a cooling tower: the hyperbola's waist at 70 % up; lit from the left */
static void ltower(int32_t cx, int32_t base, int32_t h, int32_t wb)
{
    /* a cooling tower (the hyperbola's waist 70 % up), lit from the left: white, checker, black across it; rings of
     * concrete as black lines on its lit side; streaks of stain down from the rim; the rim lit */
    int32_t top = base - h, wt = wb * 62 / 100, wn = wb * 52 / 100;
    int8_t stain[64];
    for (int32_t k = 0; k < 64; k++) stain[k] = (int8_t)((lr() % 5u) == 0 ? lri(4, h / 2) : 0);
    for (int32_t y = top; y < base; y++) {
        int32_t u = (y - top) * 256 / h, w;
        if (u < 77) w = wn + (wt - wn) * (77 - u) * (77 - u) / (77 * 77);
        else w = wn + (wb - wn) * (u - 77) * (u - 77) / (179 * 179);
        for (int32_t x = cx - w; x <= cx + w; x++) {
            int32_t f = (x - (cx - w)) * 100 / (2 * w + 1), on;   /* 0 left .. 100 right */
            on = f < 34 ? 1 : f < 52 ? lchk(x, y) : f < 66 ? lchk(x, y) && (y & 1) : 0;
            if (f < 52 && (y - top) % 9 == 8) on = 0;            /* rings */
            int32_t sk = (x - cx + wb + 64) & 63;
            if (f < 40 && stain[sk] && y - top < stain[sk] && ((y - top) & 3) != 3) on = 0;   /* stains from the rim */
            lset(lv, x, y, on); lset(lm, x, y, 1);
        }
        lset(lv, cx - w, y, 1);
    }
    for (int32_t x = cx - wt; x <= cx + wt; x++) { lset(lv, x, top, 1); lset(lv, x, top + 1, x < cx); }   /* the rim */
}
static void lpylon(int32_t x, int32_t base, int32_t h)
{
    lline(x - 6, base, x, base - h, 16, 1);
    lline(x + 6, base, x, base - h, 16, 1);
    for (int32_t k = 1; k < 5; k++) {
        int32_t y = base - h * k / 5, hw = 6 - 6 * k / 5;
        lline(x - hw, y, x + hw, y, 16, 1);
        lline(x - hw, y, x + hw - 1, y - h / 5, 10, 1);
    }
    lline(x - 9, base - h * 4 / 5, x + 9, base - h * 4 / 5, 16, 1);
}
static void lwire(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t sag)
{
    for (int32_t x = x0; x <= x1; x++) {
        int32_t u = (x - x0) * 64 / (x1 - x0 + 1);
        lset(lv, x, y0 + (y1 - y0) * u / 64 + sag * u * (64 - u) / 1024, 1);
    }
}
static void lreactor(void)
{
    int32_t x, y;
    lnight(66);
    for (y = 70; y < LH; y++) for (x = 0; x < LW; x++) { lset(lv, x, y, 0); lset(lm, x, y, 1); }
    for (x = 0; x < LW; x++) if (x & 1) lset(lv, x, 70, 1);       /* the ground's edge */
    ltower(52, 72, 46, 22);
    ltower(98, 72, 38, 18);
    /* the block: its front checkered, black windows in rows, the roof's edge lit; the sarcophagus's slope lit white,
     * hatched black along it; the striped vent stack in its lattice */
    for (x = 124; x < 198; x++)
        for (y = 46; y < 72; y++) { lset(lv, x, y, x < 132 ? 1 : lchk(x, y) && ((y & 1) || x < 150)); lset(lm, x, y, 1); }
    for (x = 134; x < 196; x += 7) for (y = 50; y < 66; y += 8) lrect(x, y, 3, 4, 0, 1);
    lspan(124, 197, 46, 16, 1); lspan(124, 197, 47, 16, 1);
    { const int16_t sl[] = {150, 46, 196, 46, 196, 34, 168, 34}; lpoly(sl, 4, 16, 1); }
    for (int32_t k = 0; k < 7; k++) lline(154 + k * 6, 45, 168 + k * 4, 35, 0, 1);
    lspan(168, 196, 34, 16, 1);
    for (y = 8; y < 34; y++) for (x = 176; x < 183; x++) { lset(lv, x, y, ((y / 4) & 1) ? (x < 180) : 0); lset(lm, x, y, 1); }
    for (y = 10; y < 34; y += 6) { lline(174, y, 184, y + 6, 16, 1); lline(184, y, 174, y + 6, 16, 1); }
    lline(174, 8, 174, 34, 16, 1); lline(184, 8, 184, 34, 16, 1);
    lpylon(214, 72, 34);
    lwire(0, 40, 205, 42, 10);
    lwire(222, 42, 239, 44, 4);
    for (x = 0; x < LW; x++) { y = 78 - x / 80; lset(lv, x, y, 1); if ((x / 6) & 1) lset(lv, x, y + 4, 1); }   /* the road, its broken line */
    for (int32_t k = 0; k < 20; k++) lset(lv, lri(0, LW - 1), lri(72, LH - 1), 1);   /* rubble */
}
static void lscrawl(int32_t x, int32_t y, int32_t w, int32_t tone)
{
    int32_t px = x, py = y + lri(0, 6);
    for (int32_t i = 0; i < w; i++) {
        int32_t nx = px + lri(1, 3), ny = y + lri(0, 6);
        lline(px, py, nx, ny, tone, 1);
        px = nx; py = ny;
    }
}
/* BUNKER (now an old train tunnel, deep underground): a ribbed concrete vault running away to a bright far end, a
 * stain of water down it, lamps along its crown; rails running off; rusted racks of boxes down both sides; a wooden
 * double door in the left wall. Everything seen through one vanishing point (world X across, Y down, Z away) */
#define BK_VX 150.0f
#define BK_VY 40.0f
#define BK_F 80.0f
#define BK_W 1.6f                        /* the tunnel's half width, the arch's height over its springing */
#define BK_H 1.35f
static int16_t bk_lamp[6][2];
static inline int32_t bkx(float X, float Z) { return (int32_t)(BK_VX + X * BK_F / Z + 0.5f); }
static inline int32_t bky(float Y, float Z) { return (int32_t)(BK_VY + Y * BK_F / Z + 0.5f); }
static void bkline(float x0, float y0, float z0, float x1, float y1, float z1, int on)
{
    lline(bkx(x0, z0), bky(y0, z0), bkx(x1, z1), bky(y1, z1), on ? 16 : 0, 1);
}
static void bkbox(float x0, float x1, float y0, float y1, float z0, float z1)
{
    /* a box on a shelf: its front face lit (white), its top checkered, its far side dark */
    int32_t ax = bkx(x0, z0), bx = bkx(x1, z0), ay = bky(y0, z0), by = bky(y1, z0);
    if (ax > bx) { int32_t t = ax; ax = bx; bx = t; }
    for (int32_t y = ay; y <= by; y++) for (int32_t x = ax; x <= bx; x++) lset(lv, x, y, y == ay || (x + y) % 5 != 0);
    int32_t ty = bky(y0, z1), tx0 = bkx(x0, z1), tx1 = bkx(x1, z1);
    if (tx0 > tx1) { int32_t t = tx0; tx0 = tx1; tx1 = t; }
    for (int32_t y = ty; y < ay; y++) for (int32_t x = tx0 < ax ? tx0 : ax; x <= (tx1 > bx ? tx1 : bx); x++) if (lchk(x, y)) lset(lv, x, y, 1);
    lline(ax, by, bx, by, 0, 1);
}
static void lbunker(void)
{
    int32_t x, y, i;
    lrect(0, 0, LW, LH, 0, 1);
    /* the vault, the walls and the floor: for each pixel, where its ray meets the tunnel, and how far off */
    for (y = 0; y < LH; y++)
        for (x = 0; x < LW; x++) {
            float dx = ((float)x - BK_VX) / BK_F, dy = ((float)y - BK_VY) / BK_F, z, on = 0;
            int surf;                                            /* 0 vault, 1 wall, 2 floor */
            if (dy < 0.0f) { z = 1.0f / fm_sqrtf(dx * dx / (BK_W * BK_W) + dy * dy / (BK_H * BK_H) + 1e-6f); surf = 0; }
            else {
                float zw = BK_W / (fm_fabsf(dx) + 1e-6f), zf = 1.0f / (dy + 1e-6f);
                if (zw < zf) { z = zw; surf = 1; } else { z = zf; surf = 2; }
            }
            if (z > 40.0f) { lset(lv, x, y, 1); continue; }      /* the far end: light */
            float fog = (z - 2.0f) / 14.0f;                      /* 0 near .. 1 far: brighter toward the light */
            if (fog < 0.0f) fog = 0.0f;
            if (fog > 1.0f) fog = 1.0f;
            float ring = z * 2.2f; ring -= (float)(int32_t)ring;  /* the ribs, every so far */
            if (surf == 0) {
                float a = dx * z / BK_W;                         /* across the vault: -1 left .. 1 right */
                int rib = ring < 0.035f + 0.03f / z;
                on = rib ? 1 : fog > 0.82f ? 1 : fog > 0.6f ? lchk(x, y) : fog > 0.35f ? lchk(x, y) && (y & 1) : fog > 0.12f ? lchk(x, y) && (y % 3 == 0) && (x & 1) : 0;
                {   /* the shuttering boards' joints, running along the vault into the distance */
                    float b = (a + 1.0f) * 4.0f; b -= (float)(int32_t)b;
                    if ((b < z * 0.008f || b > 1.0f - z * 0.008f) && fog > 0.15f && fog < 0.6f && ring > 0.12f && ((x + y) & 1)) on = 1;
                }
                if (ring > 0.04f && ring < 0.04f + 0.05f / z && fog < 0.5f) on = 0;   /* a dark line behind each rib (its shadow) */
                if (fm_fabsf(a) < z * 0.006f + 0.004f && fog < 0.8f) on = 1;   /* the cable along the crown */
            } else if (surf == 1) {
                float Y = dy * z;                                /* 0 the springing .. 1 the floor */
                int rib = ring < 0.035f + 0.03f / z;
                on = rib && Y < 0.5f ? 1 : fog > 0.6f ? lchk(x, y) : Y < 0.45f ? lchk(x, y) && (y & 1) : Y < 0.55f ? 0 : lchk(x, y) && (y % 4 == 0) && (x & 1);   /* paler above a dark band */
            } else {
                float X = dx * z;
                on = fog > 0.7f ? lchk(x, y) : 0;
                if (fm_fabsf(X) < 0.7f && ring < 0.18f && fog < 0.7f) on = lchk(x, y) || ring < 0.08f;   /* sleepers */
                if (fm_fabsf(fm_fabsf(X) - 0.52f) < z * 0.007f) on = 1;   /* the rails, a pixel wide */
                if (z < 3.0f && ((x * 7 + y * 13) % 23) == 0) on = 1;  /* grit */
            }
            lset(lv, x, y, (int)on);
        }
    lset(lm, 0, 0, 1);
    for (y = 0; y < LH; y++) for (x = 0; x < LW; x++) lset(lm, x, y, 1);   /* (indoors: all of it stands) */
    /* a stain of water down the vault on the left, from a crack */
    {
        int32_t sx = 66, sy = 0;
        for (i = 0; i < 34; i++) {                               /* a wandering streak, a thinner run beside it */
            lset(lv, sx, sy, 1);
            if (i < 22 && (i & 1)) lset(lv, sx + 2, sy, 1);
            sy++;
            if ((lr() % 4u) == 0) sx += (lr() & 1u) ? 1 : -1;
        }
    }
    /* lamps along the crown, each a bright point, the near ones bigger */
    for (i = 0; i < 6; i++) {
        float z = 2.0f + (float)i * 2.4f;
        int32_t lx = bkx(0.0f, z), ly = bky(-BK_H * 0.97f, z);
        bk_lamp[i][0] = (int16_t)lx; bk_lamp[i][1] = (int16_t)ly;
        lline(lx, ly - 3, lx, ly, 16, 1);
        lrect(lx - (i < 2 ? 2 : 1), ly, i < 2 ? 5 : 3, 2, 16, 1);
    }
    /* the racks: rusted angle-iron, four shelves, boxes on them; one down each side */
    for (int32_t side = -1; side <= 1; side += 2) {
        float xi = 0.82f * (float)side, xo = 1.38f * (float)side;
        static const float SH[4] = {0.22f, 0.48f, 0.74f, 0.98f};
        for (int32_t p = 0; p < 5; p++) {                      /* posts */
            float z = 1.7f + (float)p * 1.5f;
            bkline(xi, 0.08f, z, xi, 1.0f, z, 1);
            bkline(xo, 0.08f, z, xo, 1.0f, z, 1);
        }
        for (int32_t k = 0; k < 4; k++) {                      /* shelves, along their length */
            bkline(xi, SH[k], 1.7f, xi, SH[k], 7.7f, 1);
            if (k == 0) bkline(xi, SH[k], 1.7f, xo, SH[k], 1.7f, 1);
        }
        for (int32_t b = 0; b < 7; b++) {                      /* boxes */
            float z0 = 1.8f + (float)lri(0, 50) / 10.0f, dz = 0.3f + (float)lri(0, 4) / 10.0f;
            int32_t k = lri(0, 2);
            float top = SH[k] - 0.12f - (float)lri(0, 8) / 100.0f;
            bkbox(xi + 0.04f * (float)side, xi + 0.3f * (float)side, top, SH[k] - 0.01f, z0, z0 + dz);
        }
    }
    /* the door in the left wall, near: two wooden leaves, X-braced, a dark gap between them, its frame lit */
    {
        float z0 = 1.05f, z1 = 1.3f, zm = (z0 + z1) * 0.5f;
        int32_t xa = bkx(-BK_W, z0), xb = bkx(-BK_W, z1);
        for (x = xa; x <= xb; x++) {
            float t = (float)(x - xa) / (float)(xb - xa + 1), z = z0 + (z1 - z0) * t;
            int32_t ya = bky(0.05f, z), yb = bky(1.0f, z);
            for (y = ya; y < yb && y < LH; y++) lset(lv, x, y, (x - xa) < 2 || ((y - ya) % 4 != 3));
            lset(lv, x, ya - 1, 1);
        }
        bkline(-BK_W, 0.05f, z0, -BK_W, 1.0f, zm, 0); bkline(-BK_W, 1.0f, z0, -BK_W, 0.05f, zm, 0);
        bkline(-BK_W, 0.05f, zm, -BK_W, 1.0f, z1, 0); bkline(-BK_W, 1.0f, zm, -BK_W, 0.05f, z1, 0);
        int32_t xg = bkx(-BK_W, zm);
        for (y = bky(0.05f, zm); y < LH; y++) { lset(lv, xg, y, 0); lset(lv, xg + 1, y, 0); }
    }
    /* a puddle or two catching the far light */
    for (i = 0; i < 3; i++) {
        float z = 1.6f + (float)i * 1.3f, X = -0.15f + 0.2f * (float)i;
        int32_t px = bkx(X, z), py = bky(1.0f, z), pw = (int32_t)(10.0f / z) + 3;
        for (x = px - pw; x <= px + pw; x++) if ((x + i) % 3) lset(lv, x, py, 1);
    }
    /* junk in the near left, big and dark against the tunnel: stacked boxes, a plank crate, sacks, a bucket, a pipe
     * lying across, rubble. Lit from the far end of the tunnel (the right): black masses, their tops and right edges
     * lit, a little hatching on the faces toward the light */
    {
        static const int8_t ST[6][4] = {{2, 46, 26, 22}, {26, 52, 22, 16}, {6, 30, 18, 16}, {28, 38, 16, 14}, {48, 58, 20, 12}, {8, 18, 12, 12}};
        for (i = 0; i < 6; i++) {                               /* boxes stacked: x, top, width, height */
            int32_t bx = ST[i][0], by = ST[i][1], bw = ST[i][2], bh = ST[i][3];
            for (y = by; y < by + bh; y++) for (x = bx; x < bx + bw; x++) {
                int32_t e = x - bx, on = y == by || e == bw - 1 || (e > bw * 2 / 3 && ((x + y) % 3 == 0));
                lset(lv, x, y, on);
            }
            lline(bx + bw / 2, by, bx + bw / 2 + 4, by - 3, 16, 1);   /* a flap up */
            for (x = bx + 2; x < bx + bw - 3; x += 2) lset(lv, x, by + bh / 2, 1);   /* tape round it */
        }
        for (x = 0; x < 72; x++) lset(lv, x, 68, 0);
    }
    {
        /* the crate, nearer: black, its lid's far edge and its side toward the light lit, planks and a brace on that side */
        const int16_t c[] = {66, 62, 104, 58, 120, 64, 120, 88, 66, 88};
        lpoly(c, 5, 0, 1);
        lline(66, 62, 104, 58, 16, 1); lline(104, 58, 120, 64, 16, 1); lline(66, 62, 82, 68, 16, 1); lline(82, 68, 120, 64, 16, 1);
        for (y = 68; y < 88; y++) {                              /* its lit side: planks, a brace */
            int32_t x0 = 82, x1 = 120;
            for (x = x0; x < x1; x++) if (((y - 68) % 5 == 0) || (x == x1 - 1) || ((x + y) % 4 == 0 && x > x0 + 20)) lset(lv, x, y, 1);
        }
        lline(84, 70, 118, 86, 16, 1); lline(84, 86, 118, 70, 16, 1);
        lline(82, 68, 82, 88, 16, 1);
        for (int32_t k = 0; k < 3; k++) lline(70 + k * 10, 62 - k, 86 + k * 10, 68 - k, 16, 1);   /* lid planks */
    }
    {
        /* sacks slumped in front: dark lumps, lit along their tops and right */
        static const int8_t SK[3][3] = {{20, 80, 12}, {44, 84, 10}, {4, 86, 10}};
        for (i = 0; i < 3; i++) {
            int32_t cx = SK[i][0], cy = SK[i][1], r = SK[i][2];
            for (y = -r; y <= r / 2; y++) for (x = -r - 4; x <= r + 4; x++) {
                int32_t q = x * x * 2 / 3 + y * y;
                if (q > r * r) continue;
                int edge = q > (r - 2) * (r - 2) && (y < -r / 3 || x > r / 2);
                lset(lv, cx + x, cy + y, edge || (x > r / 3 && y < 0 && ((cx + x + cy + y) % 3 == 0)));
            }
            lline(cx - r / 2, cy - r / 2, cx + 1, cy - 2, 16, 1);
        }
    }
    {
        /* a bucket by the crate: dark, its rim an ellipse, lit on the right */
        int32_t cx = 134, top = 72, r = 7;
        for (y = top; y < top + 13; y++) for (x = cx - r; x <= cx + r; x++) lset(lv, x, y, x >= cx + r - 1 || (x > cx + 2 && lchk(x, y)));
        for (x = cx - r; x <= cx + r; x++) { int32_t d = x - cx, e = 2 - d * d * 2 / (r * r); lset(lv, x, top - e, 1); lset(lv, x, top + e, 1); }
        lline(cx - r, top, cx - 4, top - 7, 16, 1); lline(cx - 4, top - 7, cx + r, top - 1, 16, 1);
    }
    for (x = 0; x < 64; x++) { y = 72 - x / 8; lset(lv, x, y - 1, 1); lset(lv, x, y, 0); lset(lv, x, y + 1, 0); lset(lv, x, y + 2, x & 1); }   /* a pipe lying across */
    for (i = 0; i < 22; i++) {                                    /* rubble: chips lit on top */
        int32_t rx = lri(0, 160), ry = lri(80, LH - 2), rw = lri(1, 4);
        for (x = rx; x < rx + rw; x++) { lset(lv, x, ry, 1); lset(lv, x, ry + 1, 0); }
    }
}
/* ---- the style (after the 1-bit landscapes Michael pointed to): big white masses on black, one light from the left;
 * lit faces white with black cracks, shadow faces black with white hatching; 50 % checker for the middle tones;
 * cumulus as stacked round puffs with dashed arcs and a checkered underside; crossed stars ---- */
/* PEAKS: a dark sky with crossed stars and wind streaks, a cumulus bank rising behind, a grey far range with snow
 * caps, the near range's sharp peaks lit from the left (white faces cracked black, shadow faces black hatched white),
 * a mist band and black pines along the bottom. The clouds and streaks move (land_frame) */
static int16_t pk_top[LW];
/* one range of peaks: back (0) the massif tapering right, front (1) a lower ridge with a few peaks */
static void lrange(int front)
{
    int32_t pkx[16], pky[16], pl[16], pr[16], np = 0, x, y, i;
    int32_t px = front ? lri(-10, 30) : lri(10, 50);
    while (px < LW + 20 && np < 16) {
        int32_t env = front ? 46 + px / 20 : 14 + px * px / 1500 + px / 12;   /* the range's height: highest left, tapering right */
        int32_t big = np == 0 || (lr() % 3u) == 0;
        pkx[np] = px; pky[np] = env + (big ? lri(-4, 2) : lri(3, 10));
        pl[np] = lri(70, 105); pr[np] = lri(80, 125);                         /* (slopes, x100) */
        np++;
        px += front ? lri(30, 60) : lri(10, 26) + px / 12;                    /* (further apart, smaller, to the right) */
    }
    int16_t top[LW]; int8_t own[LW];
    for (x = 0; x < LW; x++) {
        int32_t best = LH * 2, o = 0;
        for (i = 0; i < np; i++) {
            int32_t dx = x - pkx[i], t = pky[i] + (dx < 0 ? -dx * pl[i] / 100 : dx * pr[i] / 100);
            if (t < best) { best = t; o = i; }
        }
        top[x] = (int16_t)(best + (int32_t)(lr() % 3u) - 1);
        own[x] = (int8_t)o;
        if (!front) pk_top[x] = top[x];
    }
    /* which way each bit of the skyline faces: climbing to the right faces the light (left) */
    int8_t lit_c[LW];
    for (x = 0; x < LW; x++) {
        int32_t a = top[x > 2 ? x - 3 : 0], b = top[x < LW - 3 ? x + 3 : LW - 1];
        lit_c[x] = (int8_t)(b < a || (b == a && x > 0 && lit_c[x - 1]));
    }
    for (x = 0; x < LW; x++) {
        for (y = top[x]; y < LH; y++) {
            int32_t d = y - top[x], on;
            int32_t xs = x - d * 2 / 5;                                       /* (the faces' edges bend right as they fall) */
            int lit = lit_c[xs < 0 ? 0 : xs];
            int32_t fade = front ? 10 : 20;
            if (lit) on = d < fade ? 1 : d < fade + 8 ? lchk(x, y) : d < fade + 12 ? lchk(x, y) && (y & 1) : 0;
            else on = d < 1;
            lset(lv, x, y, on); lset(lm, x, y, 1);
        }
    }
    /* cracks down the lit faces (black, along the slope); hatching down the shadow faces (white) */
    for (i = 0; i < np; i++) {
        int32_t n = front ? 4 : 7;
        for (int32_t k = 0; k < n; k++) {
            int32_t sx = pkx[i] - lri(2, 20);
            if (sx < 0 || sx >= LW || own[sx] != i) continue;
            int32_t sy = top[sx] + lri(1, 5), len = lri(4, 12);
            for (int32_t s2 = 0; s2 < len; s2++) lset(lv, sx - s2 * pl[i] / 130, sy + s2, 0);
        }
        for (int32_t k = 0; k < n + 2; k++) {
            int32_t sx = pkx[i] + lri(3, 24);
            if (sx < 0 || sx >= LW || own[sx] != i) continue;
            int32_t sy = top[sx] + lri(2, 7), len = lri(4, 11);
            for (int32_t s2 = 0; s2 < len; s2++) {
                int32_t xx = sx + s2 * 100 / pr[i], yy = sy + s2;
                if (xx < LW && yy > top[xx] && (s2 & 3) != 3) lset(lv, xx, yy, 1);
            }
        }
    }
}

static void lpeaks(void)
{
    int32_t x, y, i;
    lrect(0, 0, LW, LH, 0, 0);
    for (i = 0; i < 34; i++) lstar(lv, lri(2, LW - 3), lri(2, 34), (lr() % 6u) == 0);
    /* the far range: checker grey, snow on its tops */
    {
        int16_t f[LW];
        int32_t n = 7;
        for (x = 0; x < LW; x++) f[x] = LH;
        for (i = 0; i < n; i++) {
            int32_t px = i * LW / (n - 1) + lri(-12, 12), py = lri(36, 46), sl = lri(55, 85);
            for (x = 0; x < LW; x++) { int32_t t = py + (x < px ? px - x : x - px) * sl / 100; if (t < f[x]) f[x] = (int16_t)t; }
        }
        for (x = 0; x < LW; x++)
            for (y = f[x]; y < LH; y++) {
                int32_t d = y - f[x];
                lset(lv, x, y, d < 2 ? 1 : d < 4 ? lchk(x, y) || (lr() & 1u) : lchk(x, y) && (y & 1)); lset(lm, x, y, 1);
            }
    }
    /* the near range: one massif, its high peaks toward the left, falling away to the right in smaller and smaller
     * peaks; a lower ridge in front of it. Each peak is lit on its left; the ridge between faces bends as it falls */
    lrange(0);
    lrange(1);
    /* the mist along the foot: a checkered band, and black pines standing in it */
    for (y = 62; y < LH; y++) for (x = 0; x < LW; x++) {      /* mist at the foot, then dark: the pines black against it */
        int on = y < 62 ? 0 : y < 65 ? lchk(x, y) && (y & 1) : y < 74 ? lchk(x, y) : y < 77 ? lchk(x, y) && (y & 1) : 0;
        if (y >= 62) { lset(lv, x, y, on); lset(lm, x, y, 1); }
    }
    for (x = 1; x < LW; x += lri(3, 7)) {                       /* black pines standing in the mist */
        int32_t h = lri(20, 36), base = LH + 2;
        for (y = 0; y < h; y++) {
            int32_t yy = base - h + y + 1, w = (y % 4) / 2 + y / 7;   /* tiers, spiky */
            for (int32_t dx = -w; dx <= w; dx++) { lset(lv, x + dx, yy, 0); lset(lm, x + dx, yy, 1); }

        }
    }
}
/* PEAKS' sky in motion: wind streaks (dashed lines) sliding by, and the cumulus bank drifting, behind the peaks */
static void lpeaks_sky(uint32_t t, int clouds)
{
    int32_t x, i;
    for (i = 0; i < 6; i++) {
        int32_t y = 6 + i * 5 + (i & 1), len = 18 + (i * 17) % 30, sp = 3 + i;
        int32_t x0 = (int32_t)((t / (uint32_t)(40 * sp) + (uint32_t)i * 71u) % (uint32_t)(LW + 60)) - 30;
        for (x = x0; x < x0 + len; x++) if (((x + i) / 2) & 1) if (!lget(lm, x, y)) lset(lw, x, y, 1);
    }
    /* two cumulus masses: puffs heaped up from a flat base, the big ones behind, small ones in front at the edges */
    static const int8_t P[2][10][3] = {
        {{0, 34, 9}, {9, 28, 11}, {20, 22, 13}, {32, 18, 12}, {43, 23, 11}, {53, 29, 9}, {62, 33, 7}, {-8, 36, 6}, {14, 34, 8}, {40, 32, 9}},
        {{0, 36, 7}, {8, 31, 9}, {18, 26, 11}, {29, 24, 10}, {39, 28, 9}, {48, 33, 7}, {24, 34, 8}, {-6, 38, 5}, {54, 37, 5}, {12, 36, 7}}};
    for (int32_t c = 0; c < 2 * clouds; c++) {
        int32_t base = c ? 46 : 44, ox = ((int32_t)(t / (c ? 300u : 200u)) + c * 150) % (LW + 140) - 70;
        for (i = 0; i < 10; i++) {
            int32_t cx = ox + P[c][i][0], cy = P[c][i][1] + 4, r = P[c][i][2];
            for (int32_t y = -r; y <= r; y++)
                for (x = -r; x <= r; x++) {
                    int32_t d = x * x + y * y, px = cx + x, py = cy + y;
                    if (d > r * r || py > base || lget(lm, px, py)) continue;
                    int on = py > base - 4 ? lchk(px, py) : 1;    /* the flat base in shade */
                    int32_t ri = r - 2;                           /* each puff's dashed arc, lower left */
                    if (x <= 0 && y >= -r / 4 && d >= (ri - 1) * (ri - 1) && d <= ri * ri && ((px + py) & 2)) on = 0;
                    lset(lw, px, py, on);
                }
        }
    }
}
/* DUNES (after the sand room in Stalker): a dark hall, its stone pillars lit down their left edges and cracked; at the
 * back a tall doorway glowing through the haze, two small figures in it; the floor a sea of sand mounds, row behind
 * row, each lit along its crest and falling into shadow; a lone dark figure standing among them. Dust turns in the
 * light (land_frame) */
static void lhump(int32_t cx, int32_t base, int32_t wl, int32_t wr, int32_t h, int dim)
{
    /* a rounded mound, lopsided (wl to the left of its top, wr to the right): a lit crest line; the upper left lit,
     * checker below it; the rest in shadow. dim: far off, in the haze */
    for (int32_t x = cx - wl; x <= cx + wr; x++) {
        int32_t dx = x - cx, w = dx < 0 ? wl : wr, q = w * w - dx * dx;
        if (q < 0) continue;
        int32_t r = 0;
        while ((r + 1) * (r + 1) * w * w <= q * h * h) r++;      /* r = h * sqrt(1 - (dx/w)^2) */
        int32_t t = base - r;
        for (int32_t y = t; y <= base + 1 && y < LH; y++) {
            int32_t d = y - t, on;
            int left = dx < wr / 4;
            if (d < 1) on = dim ? lchk(x, y) || (x & 1) : 1;
            else if (left && d < r / 4 + 1) on = dim ? lchk(x, y) : 1;
            else if (left && d < r / 2 + 1) on = lchk(x, y) && (dim ? (y & 1) : 1);
            else if (d < r / 4 + 1) on = lchk(x, y) && (y & 1);
            else on = 0;
            lset(lv, x, y, on); lset(lm, x, y, 1);
        }
    }
}
/* the one standing among the mounds (after the film's still): side-on, his body turned to our right, his head
 * turned to face us; bare-headed, collar up, a long dark coat to the knee, a hand at his front. A silhouette, with a
 * thin lit outline so he holds against the dark. '#' black, '.' nothing */
static const char *const STALKER[34] = {
    "....###....",
    "...#####...",
    "...#####...",
    "...#####...",
    "....###....",
    "....###....",
    "...#####...",
    "..#######..",
    "..########.",
    ".#########.",
    ".#########.",
    ".#########.",
    ".########..",
    ".########..",
    ".#########.",
    ".#########.",
    ".#########.",
    ".########.#",
    ".########.#",
    ".#########.",
    "..########.",
    "..########.",
    "..########.",
    "..#########",
    ".##########",
    "....###....",
    "....###....",
    "....####...",
    "....##.##..",
    "....##.##..",
    "....##..##.",
    "....##..##.",
    "....###.###",
    "..........."};
/* the two at the door, walking: small, one a step behind */
static const char *const WALKER[14] = {
    "..##..", ".####.", "..##..", ".####.", "######", "######", "######", ".####.", ".####.",
    ".#..#.", ".#..#.", "#...#.", "#....#", "......"};
static void lsprite(const char *const *rows, int32_t n, int32_t x0, int32_t y0, int halo)
{
    int32_t w = 0;
    while (rows[0][w]) w++;
    if (halo)                                                    /* a lit outline first, where the dark is behind */
        for (int32_t r = 0; r < n; r++) for (int32_t c = 0; c < w; c++) if (rows[r][c] != '.')
            for (int32_t dy = -1; dy <= 1; dy++) for (int32_t dx = -1; dx <= 1; dx++) {
                int32_t rr = r + dy, cc = c + dx;
                if (rr >= 0 && rr < n && cc >= 0 && cc < w && rows[rr][cc] != '.') continue;
                lset(lv, x0 + c + dx, y0 + r + dy, 1);
            }
    for (int32_t r = 0; r < n; r++) for (int32_t c = 0; c < w; c++)
        if (rows[r][c] != '.') lset(lv, x0 + c, y0 + r, rows[r][c] == 'o');
}
static void ldunes(void)
{
    int32_t x, y, i;
    lrect(0, 0, LW, LH, 0, 1);
    /* the back wall: the haze, brightest round the door; the doorway's two tall leaves, their seams dark */
    for (y = 4; y < 46; y++)
        for (x = 60; x < 180; x++) {
            int32_t dx = x - 120, d = dx * dx / 16 + (y - 30) * (y - 30);
            int on = d < 90 ? 1 : d < 260 ? lchk(x, y) || ((x + y) % 4 == 1) : d < 560 ? lchk(x, y) : d < 1100 ? lchk(x, y) && (y & 1) : d < 1700 ? lchk(x, y) && (y % 4 == 0) && (x & 2) : 0;
            lset(lv, x, y, on);
        }
    for (y = 12; y < 44; y++) { lset(lv, 102, y, 0); lset(lv, 138, y, 0); if (y < 30) lset(lv, 120, y, 0); }   /* the door's frame and seam */
    for (x = 102; x <= 138; x++) lset(lv, x, 12, 0);
    lsprite(WALKER, 14, 112, 30, 0); lsprite(WALKER, 14, 120, 31, 0);   /* the two at the door, black against the light */
    /* the pillars: near ones big at the sides, smaller toward the door; black, the left edge lit, a checker band down
     * the lit side, cracks */
    static const int16_t PIL[8][3] = {{0, 22, 0}, {30, 16, 6}, {58, 10, 10}, {78, 6, 12},
                                      {160, 6, 12}, {176, 10, 10}, {198, 16, 6}, {222, 18, 0}};
    for (i = 0; i < 8; i++) {
        int32_t px = PIL[i][0], pw = PIL[i][1], top = PIL[i][2], foot = 46 + (pw - 6) * 2;
        for (x = px; x < px + pw && x < LW; x++)
            for (y = top; y < foot && y < LH; y++) {
                int32_t e = x - px;
                lset(lv, x, y, e == 0 || (e < pw / 4 && lchk(x, y) && (y & 1)));
                lset(lm, x, y, 1);
            }
        for (int32_t k = 0; k < pw / 4 + 1; k++) {
            int32_t cx = px + lri(2, pw - 2), cy = lri(top + 4, foot - 10);
            for (int32_t s2 = 0; s2 < lri(4, 10); s2++) { lset(lv, cx, cy + s2, 1); cx += lri(-1, 1); }
        }
    }
    /* the floor far off, bright under the door */
    for (y = 44; y < 48; y++) for (x = 50; x < 190; x++) lset(lv, x, y, (x - 120) * (x - 120) < (y - 36) * 70 ? 1 : lchk(x, y) && (y & 1));
    /* the mounds: scattered, not in rows; small and crowded far off, big near; lopsided, overlapping; drawn from the
     * back forward. The one standing among them goes in when the mounds reach his feet */
    {
        int16_t mb[80], mx[80], n = 0;
        while (n < 80) {
            int32_t u = lri(0, 1000);
            mb[n] = (int16_t)(46 + u * u / 22000 + lri(0, 2));       /* (more of them far off) */
            mx[n] = (int16_t)lri(-20, LW + 20);
            n++;
        }
        for (int32_t a = 0; a < n; a++) for (int32_t b = a + 1; b < n; b++) if (mb[b] < mb[a]) {
            int16_t t = mb[a]; mb[a] = mb[b]; mb[b] = t; t = mx[a]; mx[a] = mx[b]; mx[b] = t;
        }
        int placed = 0;
        for (int32_t k = 0; k < n; k++) {
            int32_t b = mb[k], near = b - 44;
            if (!placed && b > 66) { lsprite(STALKER, 34, 84, 66 - 32, 1); placed = 1; }
            int32_t w = 5 + near * 4 / 5 + lri(-2, 3);
            lhump(mx[k], b, w * lri(60, 130) / 100, w * lri(60, 130) / 100, 2 + near * 2 / 5 + lri(-1, 2), b < 52);
        }
        if (!placed) lsprite(STALKER, 34, 84, 34, 1);
    }
}
static void lfactory(void)
{
    int32_t x, y;
    /* inside a derelict hall: black; the back wall's high windows full of light (white panes, black bars, some gone);
     * the roof's trusses lit; columns black, lit down their left; the floor going away in lit lines; debris */
    lrect(0, 0, LW, LH, 0, 1);
    for (x = 14; x < 226; x += 16)
        for (y = 18; y < 48; y += 10) {
            int32_t gone = (lr() % 5u) == 0;
            if (gone) { lline(x, y, x + 11, y + 7, 16, 1); lline(x + 11, y, x + 4, y + 7, 16, 1); continue; }
            lrect(x, y, 12, 8, 16, 1);
            for (int32_t k = 0; k < 8; k++) lset(lv, x + 6, y + k, 0);
            for (int32_t k = 0; k < 12; k++) lset(lv, x + k, y + 4, 0);
            if (lr() & 1u) lrect(x + lri(0, 6), y + lri(0, 4), 6, 4, 0, 1);   /* (a pane gone) */
        }
    for (y = 49; y < 56; y++) for (x = 0; x < LW; x++) if (lchk(x, y) && (y < 52 || (x & 2))) lset(lv, x, y, 1);   /* light on the floor's far end */
    for (int32_t k = -7; k <= 7; k++) for (y = 56; y < LH; y++) {   /* the floor going away: lit lines, broken */
        x = 120 + k * 6 + (k * 34) * (y - 56) / 32;
        if (((y + k) & 3) != 0) lset(lv, x, y, 1);
    }
    for (int32_t i = 0; i < 4; i++) {                            /* columns, near ones bigger */
        int32_t w = 4 + i * 3, x0 = 30 + i * 4 - i * i * 2 - i * 9, x1 = 210 - i * 4 + i * i * 2 + i * 9 - w;
        for (int32_t c = 0; c < 2; c++) {
            int32_t cx = c ? x1 : x0;
            for (x = cx; x < cx + w; x++) for (y = 6; y < 56 + i * 10 && y < LH; y++) lset(lv, x, y, x == cx || (x < cx + w / 3 && lchk(x, y) && (y & 1)));
        }
    }
    for (x = 0; x < LW; x += 20) { lline(x, 2, x + 10, 12, 16, 1); lline(x + 10, 12, x + 20, 2, 16, 1); lline(x + 10, 2, x + 10, 12, 10, 1); }   /* trusses */
    lspan(0, LW - 1, 2, 16, 1); lspan(0, LW - 1, 12, 16, 1); lspan(0, LW - 1, 13, 10, 1);
    for (int32_t i = 0; i < 7; i++) {                            /* debris: lumps lit on top */
        int32_t dx = lri(20, 210), dy = lri(64, 84), dw = lri(6, 16);
        for (x = dx; x < dx + dw; x++) { int32_t hh = 2 + (x - dx) * (dx + dw - x) * 6 / (dw * dw); for (y = dy - hh; y <= dy; y++) lset(lv, x, y, y == dy - hh || (x < dx + dw / 2 && y < dy - hh + 2)); }
    }
}

/* CONTROL: the reactor's dead control room. The left wall a grid of empty dial sockets (a few gauges left), the core's
 * round mimic board of small tiles (many gone; some still blink: land_frame), the wall curving away right with its
 * windows and indicator strips, consoles in front, the ceiling's beams with rods hanging. All of it stands (indoors) */
static int16_t ct_cx, ct_cy, ct_r;                 /* the mimic board's disc */
static void lcontrol(void)
{
    int32_t x, y, i;
    lrect(0, 0, LW, LH, 0, 1);
    /* the ceiling: black; three beams running toward the far right, their undersides lit, their sides checkered */
    for (i = 0; i < 3; i++) {                                    /* (each beam: a lit underside line, a checkered side) */
        int32_t xa = 96 + i * 46;
        for (x = xa; x < LW; x++) {
            int32_t yy = (x - xa) / 7;
            if (yy > 12) break;
            lset(lv, x, yy + 1, 1);
            if (yy > 0 && lchk(x, yy)) lset(lv, x, yy, 1);
        }
    }
    for (x = 0; x < LW; x++) lset(lv, x, 14 + (x > 150 ? (x - 150) / 18 : 0), 1);   /* where the ceiling meets the walls */
    for (i = 0; i < 7; i++) {                                    /* rods hanging on wires */
        x = 10 + i * 33 + lri(-5, 5);
        int32_t len = lri(5, 12), lean = lri(-2, 2);
        lline(x, 0, x + lean, len, 16, 1);
        lset(lv, x + lean + 1, len, 1); lset(lv, x + lean - 1, len - 1, 1);
    }
    /* the left wall: pale panels (lit from the left, shading to checker toward the board), full of black round
     * sockets, each with a lit lower rim; a few gauges left (white faces, a needle) */
    for (x = 0; x < 94; x++)
        for (y = 15; y < 70; y++) {
            int on = x < 50 ? 1 : x < 76 ? lchk(x, y) || (y & 1) : lchk(x, y);
            if (x % 31 == 30) on = 0;                            /* the panels' seams */
            lset(lv, x, y, on);
        }
    for (int32_t col = 0; col < 9; col++) {
        int32_t cx = 5 + col * 10 + col / 3;
        for (int32_t row = 0; row < 5; row++) {
            int32_t cy = 21 + row * 10, r = 3;
            if ((lr() % 9u) == 0) continue;
            int gauge = (lr() % 8u) == 0;
            for (y = -r; y <= r; y++) for (x = -r; x <= r; x++)
                if (x * x + y * y <= r * r) lset(lv, cx + x, cy + y, gauge);
            if (gauge) { lline(cx, cy, cx + lri(-2, 2), cy - 2, 0, 1); for (x = -r; x <= r; x++) lset(lv, cx + x, cy + r + 1, 0); }
            else for (x = -1; x <= 1; x++) lset(lv, cx + x, cy + r, 1);   /* (the socket's lit lip) */
        }
    }
    for (x = 0; x < 94; x++) for (y = 70; y < 72; y++) lset(lv, x, y, 0);
    /* the mimic board: a black frame; the core: a big white disc of tiles, gridded black, a third of them gone */
    ct_cx = (int16_t)118; ct_cy = (int16_t)42; ct_r = (int16_t)23;
    for (y = ct_cy - ct_r; y <= ct_cy + ct_r; y++)
        for (x = ct_cx - ct_r; x <= ct_cx + ct_r; x++) {
            int32_t dx = x - ct_cx, dy = y - ct_cy;
            if (dx * dx + dy * dy > ct_r * ct_r) continue;
            lset(lv, x, y, (dx + 100) % 4 != 3 && (dy + 100) % 4 != 3);   /* 3 x 3 tiles, black between */
            if (dx * dx + dy * dy > (ct_r - 1) * (ct_r - 1)) lset(lv, x, y, 1);   /* its rim */
        }
    for (y = ct_cy - ct_r + 1; y < ct_cy + ct_r; y++)            /* tiles gone (a quarter) */
        for (x = ct_cx - ct_r + 1; x < ct_cx + ct_r; x++) {
            int32_t dx = x - ct_cx, dy = y - ct_cy;
            if ((dx + 100) % 4 || (dy + 100) % 4 || dx * dx + dy * dy > (ct_r - 3) * (ct_r - 3)) continue;
            if ((lr() % 4u) == 0) lrect(x, y, 3, 3, 0, 1);
        }
    for (y = 15; y < 70; y++) { lset(lv, 94, y, 1); lset(lv, 143, y, 1); }
    /* the wall curving away right: checker, darker as it goes; window boxes white with black bars, smaller and
     * closer together; lamps below; its top and foot lines converging */
    for (x = 144; x < LW; x++) {
        int32_t top = 16 + (x - 144) / 11, bot = 68 - (x - 144) / 9;
        for (y = top; y < bot; y++) lset(lv, x, y, x < 190 ? lchk(x, y) && (y % 3 == 0) : lchk(x, y) && (y % 3 == 0) && (x & 2));
        lset(lv, x, top, 1); lset(lv, x, bot, 1);
    }
    for (i = 0; i < 8; i++) {
        int32_t bx = 147 + i * 12 - i * i / 3, w = 10 - i, by = 20 + i, h = 10 - i;
        if (w < 4 || bx + w >= LW) break;
        lrect(bx, by, w, h, 16, 1);
        for (y = by; y < by + h; y++) lset(lv, bx + w / 2, y, 0);
        for (x = bx; x < bx + w; x++) lset(lv, x, by + h / 2, 0);
        if ((lr() & 3u) == 0) lrect(bx, by, w / 2, h / 2, 0, 1);   /* (a pane gone) */
        for (int32_t k = 0; k < 3; k++) if (lr() & 1u) lrect(bx + k * 3, by + h + 4, 2, 2, 16, 1);
    }
    /* the console in front: black, its top edge thick and lit, its slope hatched, its face full of holes */
    {
        int16_t yt[LW];
        for (x = 0; x < LW; x++) yt[x] = (int16_t)(x < 150 ? 71 - x / 40 : x < 168 ? 67 + (x - 150) / 2 : 70 - (x - 168) / 12);
        for (x = 0; x < LW; x++) {
            for (y = yt[x]; y < LH; y++) lset(lv, x, y, 0);
            lset(lv, x, yt[x], 1); lset(lv, x, yt[x] + 1, 1);
            for (y = yt[x] + 2; y < yt[x] + 6; y++) if (((x + y) % 4) == 0) lset(lv, x, y, 1);   /* the slope, hatched */
        }
        for (i = 0; i < 10; i++) {                               /* holes in the face, each with a lit top lip */
            int32_t hx = 6 + i * 15 + lri(-3, 3), hy = 79 + lri(0, 3), hw = lri(6, 10);
            for (x = hx; x < hx + hw; x++) lset(lv, x, hy, 1);
            lrect(hx + 1, hy + 1, hw - 2, lri(3, 5), 0, 1);
        }
        for (i = 0; i < 18; i++) lset(lv, lri(172, 236), lri(72, 80), 1);   /* switches on the far desk */
    }
}

/* CHANNEL: a city's drainage channel. An expressway slants across the top left (its lit beam, railings, pipes and
 * shadow under it, a pillar); dark blocks and towers behind with a few lit windows and a sign on a lattice; across
 * the front the channel's big concrete wall, lit from the left, stained, sprayed, two rusted pipes curving out of
 * their openings and running down it out of sight */
static void lchannel(void)
{
    int32_t x, y, i;
    lrect(0, 0, LW, LH, 0, 0);
    for (y = 30; y < 44; y++) for (x = 0; x < LW; x++) if (lchk(x, y) && (y & 1) && (y > 37 || (x & 2))) lset(lv, x, y, 1);   /* city haze */
    /* far towers: checker grey slabs, lit window dots */
    for (i = 0; i < 9; i++) {
        int32_t tx = 70 + i * 20 + lri(-6, 6), tw = lri(8, 14), tt = lri(2, 22);
        for (x = tx; x < tx + tw; x++) for (y = tt; y < 48; y++) {
            int on = x == tx ? 1 : lchk(x, y) && (y % 3 == 0);
            if (!on && ((x - tx) % 3 == 1) && ((y - tt) % 4 == 2) && (lr() % 5u) == 0) on = 1;
            lset(lv, x, y, on); lset(lm, x, y, 1);
        }
    }
    /* near blocks on the right: lit left faces, black fronts with window rows; the sign on its lattice */
    static const int16_t BL[4][3] = {{150, 26, 20}, {176, 30, 12}, {206, 20, 6}, {226, 18, 16}};
    for (i = 0; i < 4; i++) {
        int32_t bx = BL[i][0], bw = BL[i][1], bt = BL[i][2];
        for (x = bx; x < bx + bw && x < LW; x++) for (y = bt; y < 60; y++) {
            int32_t e = x - bx, on;
            on = e < 4 ? (e < 2 ? 1 : lchk(x, y)) : (((e - 5) % 5 < 2) && ((y - bt) % 6 >= 2) && ((y - bt) % 6 < 4) ? (lr() % 3u) == 0 : 0);
            lset(lv, x, y, on); lset(lm, x, y, 1);
        }
        for (x = bx; x < bx + bw && x < LW; x++) lset(lv, x, bt, 1);
    }
    for (y = 2; y < 20; y++) { lset(lv, 182, y, 1); lset(lv, 196, y, 1); if (y % 4 == 0) for (x = 182; x <= 196; x++) lset(lv, x, y, (x + y) & 1); }
    lrect(178, 0, 23, 9, 16, 0);                                  /* the sign: white, its marks black */
    for (x = 180; x < 199; x += 4) { lline(x, 2, x + 2, 6, 0, 0); lline(x + 2, 2, x, 6, 0, 0); }
    /* the expressway: its deck coming at us from the right, slanting down; railing posts on top, the lit beam along
     * its side, black under it with pipes, a pillar */
    for (x = 0; x < 140; x++) {
        int32_t top = 10 + x * 30 / 140, th = 22 - x * 14 / 140;
        for (y = top - 3; y < top; y++) if ((x % 4) == 0 || y == top - 3) lset(lv, x, y, 1);   /* railing */
        for (y = top; y < top + th; y++) {
            int32_t d = y - top, on = d < 1 ? 1 : d < 3 ? 0 : d < 6 ? 1 : d < 8 ? lchk(x, y) : (d - 9) % 6 == 0 && lchk(x, y);
            lset(lv, x, y, on); lset(lm, x, y, 1);
        }
        lset(lv, x, top + th + 2, 1);                             /* a pipe under it */
    }
    for (x = 18; x < 34; x++) for (y = 30; y < 62; y++) { lset(lv, x, y, x < 20 || (x < 24 && lchk(x, y))); lset(lm, x, y, 1); }   /* the pillar */
    for (y = 34; y < 60; y++) lset(lv, 38, y, (y & 1));           /* a downpipe beside it */
    /* the channel wall: its coping lit along the top, its face white in the light fading to checker and dark
     * toward the right and the bottom; stains run down it */
    int16_t wt[LW];
    for (x = 0; x < LW; x++) wt[x] = (int16_t)(60 - x * 24 / 240);
    for (x = 0; x < LW; x++) {
        for (y = wt[x]; y < LH; y++) {
            int32_t d = y - wt[x], on;
            int32_t f = x / 5 + d;                               /* (further from the light) */
            on = d < 2 ? 1 : d < 3 ? 0 : f < 22 ? 1 : f < 40 ? lchk(x, y) : f < 56 ? lchk(x, y) && (y & 1) : 0;
            lset(lv, x, y, on); lset(lm, x, y, 1);
        }
    }
    for (i = 0; i < 26; i++) {                                    /* stains: dark streaks down from the coping */
        x = lri(0, LW - 1);
        int32_t len = lri(4, 18);
        for (y = wt[x] + 3; y < wt[x] + 3 + len && y < LH; y++) if ((y & 3) != 3) lset(lv, x, y, 0);
    }
    /* graffiti: scrawls and a splat, black on the lit concrete, white in the dark */
    for (i = 0; i < 8; i++) { int32_t gx = lri(90, 180), gy = wt[gx] + lri(6, 22); lscrawl(gx, gy, lri(4, 9), gx < 140 ? 0 : 16); }
    {
        int32_t cx = 160, cy = wt[160] + 10;
        for (int32_t a = 0; a < 12; a++) {
            static const int8_t C[12] = {100, 87, 50, 0, -50, -87, -100, -87, -50, 0, 50, 87};
            int32_t r = (a & 1) ? 7 : 4;
            lline(cx, cy, cx + C[a] * r / 100, cy + C[(a + 3) % 12] * r / 200, 16, 1);
        }
    }
    /* two rusted pipes out of their openings, out a little and bending down: round, lit top left, checker, dark under */
    for (i = 0; i < 2; i++) {
        int32_t ox = i ? 200 : 70, oy = wt[ox] + (i ? 8 : 10), r = i ? 3 : 5;
        lrect(ox - r - 2, oy - r - 2, 2 * r + 5, 2 * r + 5, 0, 1);            /* the opening, black, its frame lit */
        for (y = oy - r - 3; y < oy + r + 3; y++) lset(lv, ox - r - 3, y, 1);
        for (x = ox - r - 3; x < ox + r + 3; x++) lset(lv, x, oy - r - 3, 1);
        int32_t px = ox, py = oy;
        for (int32_t pass = 0; pass < 2; pass++) {                 /* (its dark edge first, then its body over it) */
            px = ox; py = oy;
            for (int32_t k = 0; py < LH + r; k++) {                /* along its path: out to the left, bending, down the wall to the ground */
                if (k < 6) px--; else if (k < 12) { px--; py++; } else py++;
                int32_t rr = pass ? r : r + 1;
                for (int32_t dy = -rr; dy <= rr; dy++) for (int32_t dx = -rr; dx <= rr; dx++) {
                    if (dx * dx + dy * dy > rr * rr) continue;
                    int32_t sd = dx + dy;
                    lset(lv, px + dx, py + dy, pass ? (sd < r / 2 ? 1 : sd < r + 1 ? lchk(px + dx, py + dy) : 0) : 0);
                }
            }
        }
    }
}

/* CITY: an occupied city in fog. A colossal tower stands out of the haze, its top lost in it, the sun a white disc
 * behind its shoulder; cables sag across the sky from the near left and from hanging pylons into the tower; old
 * mansard roofs, dormers and chimneys run off into the fog; up close, a balcony's rail and a brick wall with a
 * satellite dish. Lights blink on the tower, the fog drifts, something flies across (land_frame) */
static int16_t cy_tx;
static void csag(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t sag, int on)
{
    int32_t n = x1 > x0 ? x1 - x0 : x0 - x1, d = x1 > x0 ? 1 : -1, x;
    for (x = 0; x <= n; x++) { int32_t t = x * 256 / n; lset(lv, x0 + x * d, y0 + (y1 - y0) * t / 256 + sag * 4 * t * (256 - t) / 65536, on); }
}
static void cwire(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t sag)
{   /* a wire that reads on anything: black over the fog, lit over the dark */
    int32_t n = x1 > x0 ? x1 - x0 : x0 - x1, d = x1 > x0 ? 1 : -1, x;
    for (x = 0; x <= n; x++) {
        int32_t t = x * 256 / n, px = x0 + x * d, py = y0 + (y1 - y0) * t / 256 + sag * 4 * t * (256 - t) / 65536;
        int dark = !lget(lv, px - 1, py - 1) && !lget(lv, px + 1, py - 1) && !lget(lv, px, py - 2);
        lset(lv, px, py, dark);
    }
}
static void lcity(void)
{
    int32_t x, y, i;
    /* the sky: black at the edges, a glow of fog behind the tower where the sun is, the sun a small white disc */
    int32_t gx = 104, gy = 30, sx = 140, sy = 20;
    for (y = 0; y < LH; y++) for (x = 0; x < LW; x++) {
        int32_t dx = (x - gx) * 2 / 3, dy = (y - gy) * 2, d = dx * dx + dy * dy, s2 = (x - sx) * (x - sx) + (y - sy) * (y - sy), on;
        if (s2 < 20) on = 1;
        else if (s2 < 64) on = lchk(x, y) || ((x ^ y) & 3) == 0;
        else if (d < 1600) on = lchk(x, y);
        else if (d < 3600) on = lchk(x, y) && (y & 1);
        else if (d < 6400) on = lchk(x, y) && (y & 1) && (x & 2);
        else on = 0;
        lset(lv, x, y, on);
    }
    /* the tower: a black wedge out of the fog, its top lost in it; the sun side edge lit, ribs catching light */
    cy_tx = (int16_t)100;
    for (y = 0; y < 66; y++) {
        int32_t w = 12 + y / 5, x0 = cy_tx - w / 2, x1 = x0 + w;
        for (x = x0; x <= x1; x++) {
            int on, fade = y < 12 && ((lr() & 15u) < (uint32_t)(12 - y));
            if (fade) continue;
            on = x == x1 || (y % 8 == 3 && x > x1 - 4)
                 || (y > 14 && (x - x0) % 4 == 2 && x > x1 - 7 && (y & 1))          /* panel seams on the lit side */
                 || (y > 20 && y % 8 == 6 && (x - x0) % 5 == 1 && x < x1 - 4 && (lr() % 3u) == 0);   /* a few lit ports */
            lset(lv, x, y, on); lset(lm, x, y, 1);
        }
        if (y > 14 && y % 16 == 15) for (x = x0 - 2; x <= x1 + 2; x++) {              /* flanges ringing it, lit on top */
            lset(lv, x, y, x > cy_tx - 2); lset(lv, x, y + 1, 0); lset(lm, x, y, 1); lset(lm, x, y + 1, 1);
        }
    }
    for (y = 26; y < 52; y++) {                                       /* a scaffold climbing its shadow side */
        int32_t xr = cy_tx - (12 + y / 5) / 2 - 1;
        for (x = xr - 7; x <= xr; x++) {
            int32_t e = x - (xr - 7), q = (y - 26) % 8;
            int on = e == 0 ? (y & 1) : (q == 0 ? (x & 1) : (e == q || e == 7 - q) && (y & 1));
            if (on || e == 0) { lset(lv, x, y, on); lset(lm, x, y, 1); }
        }
    }
    for (y = 8; y < 14; y++) lset(lv, cy_tx + 6, y, 0);               /* a mast out of the haze, a light on it */
    lset(lv, cy_tx + 6, 7, 1);
    /* cables: a heavy bundle sagging from the upper left into the tower, a few slung out to the right */
    for (i = 0; i < 3; i++) { csag(-2, 6 + i * 5, cy_tx - 8, 30 + i * 4, 10, 0); csag(-2, 7 + i * 5, cy_tx - 8, 31 + i * 4, 10, 0); }
    for (i = 0; i < 3; i++) csag(cy_tx + 9, 24 + i * 8, LW - 26, 8 + i * 12, 9 + i * 3, 0);
    /* a wooden pole near us on the left: crossarms, insulators, wires sagging off both ways */
    {
        int32_t px = 34;
        for (y = 12; y < LH; y++) { lset(lv, px, y, 0); lset(lv, px + 1, y, 0); lset(lv, px + 2, y, 1); lset(lm, px, y, 1); lset(lm, px + 1, y, 1); lset(lm, px + 2, y, 1); }
        for (i = 0; i < 2; i++) {
            int32_t ay = 16 + i * 7;
            for (x = px - 9; x <= px + 11; x++) { lset(lv, x, ay, 1); lset(lv, x, ay + 1, 0); lset(lm, x, ay, 1); lset(lm, x, ay + 1, 1); }
            for (int32_t k = 0; k < 3; k++) {
                int32_t ix = px - 8 + k * 9;
                lset(lv, ix, ay - 1, 1); lset(lv, ix, ay - 2, 1); lset(lm, ix, ay - 1, 1); lset(lm, ix, ay - 2, 1);
                if (i == 0) cwire(ix, ay - 2, -2, ay + 10 + k * 3, 5);
                else cwire(ix, ay - 2, cy_tx - 9, 42 + k * 3, 10 + k * 2);
                if (i == 0) cwire(ix, ay - 2, cy_tx - 9, 36 + k * 2, 14);
            }
        }
        lline(px - 7, 17, px, 26, 0, 1); lline(px + 9, 17, px + 2, 26, 0, 1);   /* braces */
        for (y = 30; y < 36; y++) for (x = px + 3; x < px + 8; x++) { lset(lv, x, y, x == px + 7 || y == 30); lset(lm, x, y, 1); }   /* transformer can */
    }
    /* a pylon box hanging on the right-hand cables */
    for (y = 22; y < 40; y++) for (x = 176; x < 184; x++) { lset(lv, x, y, x == 176 || (y == 22) || (y % 6 == 4 && x < 180)); lset(lm, x, y, 1); }
    /* the roofs: a far row hazed, a near row black with lit ridges, dormers and a few lit windows, chimneys */
    for (int32_t r = 0; r < 2; r++) {
        int32_t base = 58 + r * 10;
        for (x = -6; x < LW; ) {
            int32_t w = lri(18, 30) + r * 10, top = base - lri(2, 6) - r * 3;
            for (int32_t xx = x; xx < x + w && xx < LW; xx++) {
                if (xx < 0) continue;
                int32_t e = xx - x, t = top + (e < 4 ? 4 - e : e > w - 5 ? e - (w - 5) : 0);
                for (y = t; y < LH; y++) {
                    int32_t dy = y - t;
                    int on = r == 0 ? (y == t || (lchk(xx, y) && !(y & 1) && (xx & 2)))
                           : (y == t && e >= 4)
                             || (dy > 0 && dy < 6 && !(dy & 1) && e > 3 && e < w - 4 && ((xx + dy) % 3) == 0)   /* slate courses */
                             || (dy == 6 && e > 1 && e < w - 1 && (xx & 1) == 0)                                   /* cornice */
                             || (dy >= 9 && dy <= 11 && e > 2 && e < w - 3 && (e - 3) % 6 == 3 && (dy & 1))    /* window jambs */
                             || (dy == 8 && e > 2 && e < w - 3 && (e - 3) % 6 < 4);                               /* lintels */
                    if (r == 1 && dy >= 9 && dy <= 11 && e > 3 && e < w - 3 && (e - 3) % 6 > 0 && (e - 3) % 6 < 3
                        && ((uint32_t)(x * 7 + (e - 3) / 6 * 13) % 5u) == 0) on = 1;                               /* one lit pane here and there */
                    lset(lv, xx, y, on); lset(lm, xx, y, 1);
                }
            }
            if (r == 1) for (int32_t k = 0; k < w / 14; k++) {
                int32_t dx = x + 6 + k * 14, dy = top + 5;
                if (dx >= 1 && dx + 4 < LW) {
                    lset(lv, dx + 1, dy - 3, 1); lset(lv, dx, dy - 2, 1); lset(lv, dx + 2, dy - 2, 1);
                    if (lr() % 3u) lrect(dx, dy, 3, 2, 16, 1);
                }
            }
            if ((lr() % 3u) == 0 && x >= 0 && x + w / 3 + 2 < LW) {
                int32_t cx = x + w / 3;
                for (y = top - 4; y < top; y++) { lset(lv, cx + 1, y, 1); lset(lv, cx, y, 0); lset(lm, cx, y, 1); lset(lm, cx + 1, y, 1); }
                for (x = cx - 1; x <= cx + 2; x++) { lset(lv, x, top - 5, 1); lset(lm, x, top - 5, 1); }
                lset(lv, cx, top - 6, 1); lset(lv, cx + 1, top - 7, 1);
            }
            if (r == 1 && (lr() % 3u) == 0 && x + w / 2 + 3 < LW && x > 0) {          /* a TV aerial */
                int32_t ax = x + w * 2 / 3;
                for (y = top - 9; y < top; y++) lset(lv, ax, y, 1);
                for (int32_t k = 0; k < 3; k++) for (int32_t q = -2 + k / 2; q <= 2 - k / 2; q++) lset(lv, ax + q, top - 8 + k * 2, 1);
            }
            x += w + lri(0, 3);
        }
    }
    /* up close: a balcony rail along the bottom (one lit line, posts), black beneath; a brick wall on the right,
     * its corner lit, a few bricks catching light near the edge; a dish on it */
    for (x = 0; x < 214; x++) {
        y = 79 + x / 30;
        for (int32_t k = y; k < LH; k++) { lset(lv, x, k, (x % 18 == 0 && k < y + 6) || k == y); lset(lm, x, k, 1); }
    }
    for (x = 214; x < LW; x++) for (y = 0; y < LH; y++) {
        int32_t off = ((y / 4) & 1) ? 4 : 0, e = x - 214;
        int on = e == 0 || (e < 9 && y % 4 == 0 && ((x + off) % 8) != 0 && ((x + y) & 1) && e < 9 - (y & 3));
        lset(lv, x, y, on); lset(lm, x, y, 1);
    }
    {
        int32_t cx = 205, cyy = 50, r = 7;
        for (y = -r; y <= r; y++) for (x = -r / 2; x <= r / 2; x++) {
            int32_t q = x * x * 4 + y * y;
            if (q > r * r) continue;
            lset(lv, cx + x, cyy + y, q > (r - 2) * (r - 2) && x <= 0);
            lset(lm, cx + x, cyy + y, 1);
        }
        lline(cx + 1, cyy, cx + 9, cyy + 1, 0, 1); lline(cx - 4, cyy, cx - 8, cyy - 1, 16, 1);
        for (x = cx + 1; x < 214; x++) lset(lm, x, cyy + 1, 1);
    }
}

static void land_make(uint32_t kind, uint32_t seed)
{
    for (int32_t y = 0; y < LH; y++) for (int32_t i = 0; i < LROW; i++) lv[y][i] = lm[y][i] = 0;
    lrng = seed * 2654435761u + 0x9E3779B9u;
    if (!lrng) lrng = 1u;
    switch (kind) {
    case LAND_REACTOR: lreactor(); break;
    case LAND_DUNES: ldunes(); break;
    case LAND_BUNKER: lbunker(); break;
    case LAND_PEAKS: lpeaks(); break;
    case LAND_CONTROL: lcontrol(); break;
    case LAND_CHANNEL: lchannel(); break;
    case LAND_CITY: lcity(); break;
    default: lfactory(); break;
    }
    land_kind = (uint8_t)kind;
}

/* ----------------------------------------------------------------------------------------------- the motion --- */
#define NASH 48
static struct { int16_t x, y; uint8_t s; } ash[NASH];      /* x, y in 1/16 px */
#define NSMOKE 18
static struct { int16_t x, y, r, age; } smoke[NSMOKE];
static int32_t cloud_x, sparks[6][3], spark_n;
static uint32_t land_t, land_events;

/* a soft cloud of dither at (x, y): radius r, tone t, behind what stands (sky only) */
static void wcloud(int32_t cx, int32_t cy, int32_t r, int32_t tone)
{
    for (int32_t y = cy - r / 2; y <= cy + r / 2; y++)
        for (int32_t x = cx - r; x <= cx + r; x++) {
            if ((uint32_t)x >= LW || (uint32_t)y >= LH || lget(lm, x, y)) continue;
            int32_t dx = x - cx, dy = (y - cy) * 2, d = dx * dx + dy * dy, rr = r * r;
            if (d > rr) continue;
            int32_t t = tone * (rr - d) / rr + 1;
            if (t > (int32_t)BAYER[y & 3][x & 3]) lset(lw, x, y, 1);
        }
}

/* this frame: the place, then what moves. lvl: the output 0..255; wind: the weather -16..16; ev: clicks so far */
static void land_frame(uint32_t dt, uint32_t lvl, int32_t wind, uint32_t ev, uint32_t punch, int freeze)
{
    int32_t y, x, i;
    if (freeze) dt = 0;
    land_t += dt * 16u;
    for (y = 0; y < LH; y++) for (i = 0; i < LROW; i++) lw[y][i] = lv[y][i];
    /* cloud banks drifting across the sky */
    cloud_x += (int32_t)dt * (4 + (int32_t)lvl / 24 + wind) / 16;
    for (i = 0; i < 5; i++) {
        int32_t span = LW + 120, cx = ((cloud_x / (8 + i * 3) + i * 97) % span + span) % span - 60;
        if (land_kind == LAND_PEAKS || land_kind == LAND_REACTOR) { if (i == 0) lpeaks_sky(land_t, land_kind == LAND_PEAKS); continue; }
        wcloud(cx, 8 + i * 6, 22 + (i * 13) % 20, 4 + (i & 1) * 2);
    }
    /* smoke from the towers and the stack */
    if (land_kind == LAND_REACTOR || land_kind == LAND_FACTORY) {
        static const int16_t SRC[2][3][2] = {{{52, 24}, {98, 33}, {179, 8}}, {{60, 2}, {130, 2}, {190, 2}}};
        for (i = 0; i < NSMOKE; i++) {
            if (smoke[i].age <= 0 && (lr() & 7u) == 0 && dt) {
                const int16_t *s = SRC[land_kind == LAND_FACTORY][lr() % 3u];
                smoke[i].x = (int16_t)(s[0] * 16); smoke[i].y = (int16_t)(s[1] * 16); smoke[i].r = 2; smoke[i].age = 90;
            }
            if (smoke[i].age > 0) {
                for (uint32_t s = 0; s < dt && smoke[i].age > 0; s++) {
                    smoke[i].age--;
                    smoke[i].x = (int16_t)(smoke[i].x + 6 + wind * 2);
                    smoke[i].y = (int16_t)(smoke[i].y + (land_kind == LAND_FACTORY ? 3 : -4));
                    if ((smoke[i].age & 7) == 0 && smoke[i].r < 9) smoke[i].r++;
                }
                wcloud(smoke[i].x / 16, smoke[i].y / 16, smoke[i].r + 2, 3 + smoke[i].age / 18);
            }
        }
    }
    /* the place's own motion */
    if (land_kind == LAND_REACTOR && (land_t / 700u) & 1u) lset(lw, 179, 7, 0);   /* the stack's light blinks */
    if (land_kind == LAND_DUNES)                                /* dust turning slowly in the light from the door */
        for (i = 0; i < 18; i++) {
            uint32_t h = (uint32_t)i * 2654435761u;
            int32_t mx = 70 + (int32_t)((h >> 8) % 100u) + (int32_t)((land_t / (60u + (h >> 20) % 50u)) % 24u) - 12;
            int32_t my = 6 + (int32_t)((h >> 16) % 38u) + (int32_t)((land_t / (90u + (h >> 4) % 70u)) % 6u) - 3;
            if (((land_t / 260u) + (uint32_t)i) % 5u) lset(lw, mx, my, !lget(lw, mx, my));
        }
    if (land_kind == LAND_CITY) {
        /* lights blinking on the tower; fog drifting across the roofs; something flying across, rotors flicking */
        for (i = 0; i < 4; i++) if (((land_t / (420u + (uint32_t)i * 170u)) + (uint32_t)i) & 1u) {
            int32_t ly = 18 + i * 12, lx = cy_tx - (12 + ly / 5) / 2 + 3 + (i & 1) * 5;
            lset(lw, lx, ly, 1); lset(lw, lx, ly + 1, 1);
        }
        for (y = 46; y < 58; y += 4) {
            int32_t off = (int32_t)(land_t / (60u + (uint32_t)y)) % 24;
            for (x = 0; x < LW; x++) if (((x + off) % 24) < 10 && lchk(x, y) && !lget(lw, x, y)) lset(lw, x, y, 1);
        }
        {
            int32_t fx = (int32_t)((land_t / 40u) % (LW + 40u)) - 20, fy = 38 + (int32_t)((land_t / 900u) % 3u);
            for (x = fx - 3; x <= fx + 3; x++) lset(lw, x, fy, 0);
            lset(lw, fx, fy + 1, 0); lset(lw, fx, fy + 2, 0); lset(lw, fx + 1, fy + 3, 0);
            int32_t rp = (int32_t)((land_t / 50u) & 1u);
            lline(fx - 4 + rp * 2, fy - 2, fx + 4 - rp * 2, fy - 1, 0, 0);
            for (x = fx - 4; x <= fx + 4; x++) if (lget(lv, x, fy - 2)) lset(lw, x, fy - 2, (x + rp) & 1);
        }
    }
    if (land_kind == LAND_CHANNEL) {
        /* a window or two going on and off; the sign stuttering */
        if ((land_t / 1700u) & 1u) { lset(lw, 160, 34, 1); lset(lw, 161, 34, 1); lset(lw, 160, 35, 1); lset(lw, 161, 35, 1); }
        for (int32_t q = 0; q < 3; q++) if (((land_t / (700u + (uint32_t)q * 310u)) + (uint32_t)q) & 1u) { lset(lw, 190 + q * 13, 28 + q * 6, 1); lset(lw, 191 + q * 13, 28 + q * 6, 1); }
        if ((lr() % 61u) == 0) for (y = 0; y < 9; y++) for (x = 178; x < 201; x++) lset(lw, x, y, !lget(lw, x, y));
    }
    if (land_kind == LAND_CONTROL) {
        /* the mimic board: tiles still blinking (a few at a time, each its own rhythm); one indicator strip flashing;
         * now and then the lights stutter and the room drops to dark */
        for (y = ct_cy - 20; y <= ct_cy + 20; y += 4)
            for (x = ct_cx - 20; x <= ct_cx + 20; x += 4) {
                int32_t dx = x - ct_cx, dy = y - ct_cy;
                if (dx * dx + dy * dy > 19 * 19) continue;
                uint32_t h = (uint32_t)(x * 73856093) ^ (uint32_t)(y * 19349663);
                if ((h % 9u) != 0) continue;
                int on = ((land_t / (300u + (h >> 4) % 900u) + (h >> 12)) & 1u) != 0;
                for (int32_t j = 0; j < 3; j++) for (int32_t q = 0; q < 3; q++) lset(lw, x + q, y + j, on);
            }
        if ((land_t / 520u) & 1u) for (y = 35; y < 38; y++) for (x = 146; x < 148; x++) lset(lw, x, y, 0);
        if ((lr() % 97u) == 0) for (y = 0; y < LH; y++) for (i = 0; i < LROW; i++) lw[y][i] &= (y & 1) ? 0x11u : 0x44u;
    }
    if (land_kind == LAND_BUNKER)                               /* the lamps: one or two flickering, now and then one dies a moment */
        for (i = 0; i < 6; i++) {
            uint32_t h = (uint32_t)i * 2654435761u;
            int dark = (i == 1 && (lr() % 9u) < 3u) || (i == 4 && ((land_t / (500u + (h >> 22))) % 7u) == 0) || (lr() % 61u) == 0;
            if (dark) for (y = bk_lamp[i][1] - 1; y < bk_lamp[i][1] + 3; y++) for (x = bk_lamp[i][0] - 3; x <= bk_lamp[i][0] + 3; x++) lset(lw, x, y, 0);
        }
    if (land_kind == LAND_FACTORY) {
        /* shafts of light through the roof, dust turning in them; chains swaying */
        for (i = 0; i < 3; i++) {
            int32_t x0 = 50 + i * 70;
            for (y = 12; y < 70; y++)
                for (x = x0 + (y - 12) / 2; x < x0 + 14 + (y - 12) / 2; x++)
                    if ((int32_t)BAYER[y & 3][(x + (int32_t)(land_t / 400u)) & 3] < 8 - (y - 12) / 10) lset(lw, x, y, 1);
        }
        for (i = 0; i < 4; i++) {
            int32_t sway = (int32_t)((land_t / 30u + (uint32_t)i * 40u) % 64u);
            sway = sway < 32 ? sway - 16 : 48 - sway;
            int32_t xa = 40 + i * 50, xb = xa + sway / 3;
            for (y = 12; y < 40 + i * 4; y++) lset(lw, xa + (xb - xa) * (y - 12) / (28 + i * 4), y, (y & 1) == 0);
        }
    }
    /* ash: more of it, and faster, the louder it is */
    {
        int32_t want = 8 + (int32_t)lvl * (NASH - 8) / 255;
        for (i = 0; i < NASH; i++) {
            if (i >= want && ash[i].s) continue;
            if (!ash[i].s) { ash[i].x = (int16_t)(lri(0, LW - 1) * 16); ash[i].y = 0; ash[i].s = (uint8_t)lri(3, 9); }
            if (dt) {
                ash[i].y = (int16_t)(ash[i].y + ash[i].s * (int32_t)dt);
                ash[i].x = (int16_t)(ash[i].x + (wind * 2 + (int32_t)(lr() % 5u) - 2) * (int32_t)dt);
                if (ash[i].y >= LH * 16 || ash[i].x < 0 || ash[i].x >= LW * 16) ash[i].s = 0;
            }
            if (ash[i].s && i < want) {
                x = ash[i].x / 16; y = ash[i].y / 16;
                lset(lw, x, y, !lget(lw, x, y));
            }
        }
    }
    /* a Geiger spark for each new click (a few a frame at most) */
    if (ev != land_events) {
        uint32_t n = ev - land_events;
        land_events = ev;
        for (uint32_t k = 0; k < n && k < 3u; k++) {
            int32_t *s = sparks[spark_n++ % 6];
            s[0] = lri(4, LW - 5); s[1] = lri(LH / 2, LH - 4); s[2] = 3;
        }
    }
    for (i = 0; i < 6; i++)
        if (sparks[i][2] > 0) {
            int32_t sx = sparks[i][0], sy = sparks[i][1], r = 4 - sparks[i][2];
            lset(lw, sx, sy, 1); lset(lw, sx - r, sy, 1); lset(lw, sx + r, sy, 1); lset(lw, sx, sy - r, 1); lset(lw, sx, sy + r, 1);
            if (dt) sparks[i][2]--;
        }
    /* the punches: BURN flashes the sky and splits it with lightning; VOID fades it to a ghost; SHATTER tears rows */
    if ((punch & DR_PUNCH_BURN) && (lr() & 3u) == 0) {
        for (y = 0; y < LH; y++) for (x = 0; x < LW; x++) if (!lget(lm, x, y)) lset(lw, x, y, !lget(lw, x, y));
        x = lri(20, 220);
        for (y = 0; y < 60 && !lget(lm, x, y); y++) { lset(lw, x, y, 1); x += lri(-2, 2); }
    }
    if (punch & DR_PUNCH_VOID)
        for (y = 0; y < LH; y++) for (i = 0; i < LROW; i++) lw[y][i] &= (y & 1) ? 0x22u : 0x88u;
    if (punch & (1u << PU_WARBLE))                             /* WARBLE: the picture wavers, row by row */
        for (y = 0; y < LH; y++) {
            int32_t off = (int32_t)((land_t / 40u + (uint32_t)y * 3u) % 12u);
            off = off < 6 ? off - 3 : 9 - off;
            if (!off) continue;
            uint8_t row[LROW];
            for (i = 0; i < LROW; i++) row[i] = lw[y][i];
            for (x = 0; x < LW; x++) lset(lw, x, y, (row[((x + off + LW) % LW) >> 3] >> (7 - (((x + off + LW) % LW) & 7))) & 1u);
        }
    if (punch & (1u << PU_DECIMATE))                           /* DECIMATE: coarse blocks */
        for (y = 0; y < LH; y++) for (x = 0; x < LW; x++) lset(lw, x, y, lget(lw, x & ~3, y & ~1));
    if (punch & (1u << PU_SCREAM))                             /* SCREAM: lines torn bright */
        for (i = 0; i < 4; i++) { y = lri(0, LH - 1); for (x = 0; x < LW; x++) lset(lw, x, y, !lget(lw, x, y)); }
    if ((punch & (1u << PU_GATE)) && (lr() & 1u))              /* GATE: the power fails */
        for (y = 0; y < LH; y++) for (i = 0; i < LROW; i++) lw[y][i] = 0;
    if (punch & DR_PUNCH_SHATTER)
        for (y = 0; y < LH; y++) {
            if ((lr() & 7u) > 2u) continue;
            int32_t off = lri(1, LROW - 1);
            uint8_t row[LROW];
            for (i = 0; i < LROW; i++) row[i] = lw[y][(i + off) % LROW];
            for (i = 0; i < LROW; i++) lw[y][i] = row[i];
        }
}
