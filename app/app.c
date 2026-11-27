/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Michael (MONODRONE) */
/* MONODRONE's controls, screen and scenes. Part of fm1-base's unity build (after the platform: gfx.c, announce.c and
 * fastmath are in scope); the sound is drone_dsp.c, its own unit.
 *
 * The panel (NOTES.md has the whole of it):
 *   white keys   the selected layer's pitch, and on; the same key again: off. More keys held: the next layers
 *   black keys   F#3..A#4 the selected layer's engine (8); C#5 BURN, D#5 VOID, F#5 SHATTER (held; tapped twice:
 *                latched until tapped again)
 *   SELECT       the layer (1..4)      ALGO   its engine      PRESET   the scene (1..16)
 *   K1..K4       the page's four
 *   HOME LAYER   EDIT/ENV SHAPE   GLO MIX (again: PAN)   FX the effects (again: the next page)   LFO WEATHER
 *   PLAY         the scene's layers in / out      SEQ   the selected layer in / out
 *   ARP          FREEZE      REC   SMEAR's memory records (lit) or holds
 *   SAVE         the scene into its slot; held + a white key: into that slot      SEL held + a white key: recall
 *   OCT- OCT+    the keys' octave; with SEL held, the selected layer's chord an octave down/up; with GLO, every layer's */
#include <stddef.h>
#include "drone_dsp.h"
#include "land.h"
#include "logo.h"
static uint32_t land_of(uint32_t b) { b /= 16u; return b < NLAND ? b : NLAND - 1u; }   /* a scene's place byte to its place */

/* ------------------------------------------------------------------------------------------------- colours --- */
#define K_BG RGB(10, 11, 9)
#define K_PANEL RGB(26, 28, 22)
#define K_LINE RGB(48, 52, 40)
#define K_ASH RGB(110, 112, 96)
#define K_BONE RGB(214, 210, 186)
#define K_TOX RGB(170, 230, 40)
#define K_RUST RGB(226, 112, 34)
#define K_BLOOD RGB(222, 44, 32)
#define K_ICE RGB(150, 200, 196)
static const uint16_t LAYER_INK[DR_LAYERS] = {K_TOX, K_RUST, K_ICE, RGB(200, 70, 120)};

/* --------------------------------------------------------------------------------------------- the scenes --- */
#define NSCENES 16
#define SC_MAGIC 0x4F4E444Du             /* "MDNO" */
#define SC_VERSION 11u                   /* 11: GLIDE starts at 3 ms (an untouched 30 is set to 0) */
#define SC_VERSION_10 10u                /* 10: places stored place*16+8 (CHANNEL, the seventh); 9: place*21+10 */
#define SC_VERSION_9 9u
#define SC_VERSION_8 8u                  /* 8: scenes 9..16 their own (a 7 or before: scenes_upgrade) */
#define SC_VERSION_7 7u                  /* 7: FADE in seconds (a 5's or 6's K4s are set to where it starts) */
#define SC_VERSION_6 6u
#define SC_VERSION_5 5u                   /* 5: the punches' knobs after the scenes (a 4 is read, they start as they were) */
typedef struct {
    dr_layer_t L[DR_LAYERS];             /* .on: the layers that come in with PLAY */
    uint8_t fx[FX_N], mod[MOD_N], glob[GL_N];
    char name[10];                       /* the factory scene's name (kept when you save over it) */
    uint8_t land;                        /* its place (SCENE page K2): stored place*16+8 (room for eight) */
} scene_t;
typedef struct {
    uint32_t magic, version;
    uint8_t cur, octave_p3, morph, rsv;  /* the scene last used; the octave + 3; MORPH (SCENE page) */
    scene_t sc[NSCENES];
    uint8_t pk[PU_N][4];                 /* the punches' own knobs (one set, not per scene) */
} store_t;
static store_t st;
static dr_ctl_t *const C = (dr_ctl_t *)&dr_ctl;   /* the UI writes it; the render reads it (volatile there) */
static int8_t octave = -2;               /* white key k plays 53 + k + 12 * octave: F1..G3 to start */
static uint8_t dirty;
/* SEL held: every knob turned is for now; let go, it all springs back (a scene recalled or a mutation keeps) */
static dr_ctl_t spring;
static uint8_t spring_on, spring_turned;
static void spring_back(void)
{
    uint32_t i, k;
    for (i = 0; i < DR_LAYERS; i++) {
        dr_layer_t *l = &C->L[i];
        const dr_layer_t *o = &spring.L[i];
        for (k = 0; k < 4u; k++) l->p[k] = o->p[k];
        l->tune = o->tune; l->glide = o->glide; l->swell = o->swell; l->drift = o->drift; l->pan = o->pan;
    }
    for (k = 0; k < FX_N; k++) C->fx[k] = spring.fx[k];
    for (k = 0; k < MOD_N; k++) C->mod[k] = spring.mod[k];
    for (k = 0; k < GL_N; k++) C->glob[k] = spring.glob[k];
    for (k = 0; k < MC_N; k++) C->macro[k] = spring.macro[k];
    for (i = 0; i < PU_N; i++) for (k = 0; k < 4u; k++) C->pk[i][k] = spring.pk[i][k];
}
static uint8_t armed;                    /* PLAY's set: the layers that come in */
static uint32_t morph_until;             /* a scene morphing in: until then */

static void scene_to_ctl(const scene_t *s, int keep_on)
{
    uint32_t i, k;
    for (i = 0; i < DR_LAYERS; i++) {
        uint8_t on = C->L[i].on, push = C->L[i].push;
        C->L[i] = s->L[i];
        C->L[i].push = push;
        C->L[i].on = keep_on ? on : 0;
    }
    for (k = 0; k < FX_N; k++) C->fx[k] = s->fx[k];
    for (k = 0; k < MOD_N; k++) C->mod[k] = s->mod[k];
    for (k = 0; k < GL_N; k++) C->glob[k] = s->glob[k];
}
static void ctl_to_scene(scene_t *s)
{
    uint32_t i, k;
    for (i = 0; i < DR_LAYERS; i++) {
        s->L[i] = C->L[i];
        s->L[i].push = 0;
        s->L[i].on = C->L[i].on || ((armed >> i) & 1u && !(C->L[0].on | C->L[1].on | C->L[2].on | C->L[3].on));
    }
    for (k = 0; k < FX_N; k++) s->fx[k] = C->fx[k];
    for (k = 0; k < MOD_N; k++) s->mod[k] = C->mod[k];
    for (k = 0; k < GL_N; k++) s->glob[k] = C->glob[k];
}
/* the factory scenes, sixteen (yours to overwrite). Per layer: engine, the chord (up to four
 * notes, 0 = none), K1 K2 K3 LEVEL, swell, drift, pan; then the effects (DIRT, FILTER, DELAY, REVERB: four each),
 * the weather and GLOBAL (DETUNE, SUB, HISS, UNREST) */
typedef struct { uint8_t e, n[4], k[4], swell, drift, pan; } fl_t;
typedef struct { char name[10]; fl_t l[4]; uint8_t fx[FX_N], mod[MOD_N], glob[GL_N]; } fs_t;
static const fs_t FACTORY[NSCENES] = {
    {"ZONE", {{DR_HOWL, {29, 36, 0, 0}, {72, 35, 30, 95}, 80, 30, 50}, {DR_HUM, {31, 0, 0, 0}, {60, 40, 70, 80}, 90, 30, 74},
              {DR_GEIGER, {69, 0, 0, 0}, {45, 50, 40, 75}, 40, 20, 90}, {DR_SCRAPE, {41, 48, 0, 0}, {50, 70, 40, 60}, 95, 50, 34}},
     {50, 10, 0, 60, 80, 45, 0, 35, 90, 72, 35, 35, 110, 75, 50, 72}, {30, 40, 45, 45}, {30, 50, 45, 40}},
    {"DRONEVIL", {{DR_SWARM, {28, 35, 0, 0}, {60, 40, 115, 112}, 50, 20, 44}, {DR_SWARM, {40, 47, 52, 0}, {85, 50, 105, 92}, 60, 30, 84},
                  {DR_HOWL, {40, 0, 0, 0}, {85, 45, 45, 60}, 70, 30, 64}, {DR_CHAOS, {28, 0, 0, 0}, {40, 20, 30, 55}, 80, 40, 30}},
     {95, 20, 0, 95, 88, 50, 0, 25, 70, 85, 30, 45, 100, 80, 40, 55}, {25, 30, 35, 50}, {45, 75, 30, 25}},
    {"LYRA", {{DR_HIVE, {33, 40, 45, 0}, {35, 45, 60, 95}, 60, 40, 40}, {DR_HIVE, {52, 57, 0, 0}, {55, 65, 80, 85}, 70, 40, 88},
              {DR_HIVE, {64, 0, 0, 0}, {25, 85, 50, 70}, 80, 50, 64}, {DR_HOWL, {21, 0, 0, 0}, {70, 30, 20, 50}, 90, 60, 24}},
     {45, 0, 0, 45, 100, 25, 0, 25, 85, 80, 45, 55, 98, 60, 70, 58}, {45, 40, 25, 60}, {35, 25, 20, 35}},
    {"DUST", {{DR_WIND, {36, 0, 0, 0}, {35, 90, 95, 92}, 40, 40, 64}, {DR_GEIGER, {60, 0, 0, 0}, {95, 25, 115, 85}, 20, 30, 44},
              {DR_STATIC, {57, 0, 0, 0}, {70, 40, 90, 70}, 30, 20, 90}, {DR_ROT, {31, 0, 0, 0}, {100, 95, 120, 60}, 50, 50, 30}},
     {70, 85, 45, 75, 85, 60, 50, 60, 40, 65, 80, 30, 80, 40, 40, 40}, {60, 50, 60, 40}, {25, 30, 90, 55}},
    {"CORTINI", {{DR_SWARM, {45, 52, 57, 0}, {40, 60, 30, 80}, 90, 50, 40}, {DR_CHOIR, {57, 64, 0, 0}, {20, 35, 50, 75}, 100, 60, 88},
                 {DR_SMEAR, {60, 0, 0, 0}, {80, 60, 70, 70}, 90, 40, 64}, {DR_HIVE, {33, 0, 0, 0}, {25, 30, 40, 60}, 100, 30, 20}},
     {25, 0, 0, 30, 96, 25, 0, 30, 92, 72, 60, 55, 112, 55, 75, 78}, {35, 35, 25, 50}, {25, 35, 25, 20}},
    {"RADIO", {{DR_STATIC, {64, 0, 0, 0}, {90, 70, 100, 85}, 20, 10, 30}, {DR_STATIC, {76, 0, 0, 0}, {50, 100, 60, 60}, 20, 10, 98},
               {DR_GEIGER, {72, 0, 0, 0}, {85, 0, 0, 70}, 10, 0, 64}, {DR_CHAOS, {36, 0, 0, 0}, {110, 60, 80, 70}, 30, 20, 64}},
     {30, 40, 20, 50, 110, 30, 85, 50, 40, 55, 90, 30, 50, 50, 20, 30}, {50, 20, 40, 20}, {20, 10, 70, 60}},
    {"REACTOR", {{DR_HUM, {31, 43, 0, 0}, {90, 60, 80, 100}, 60, 30, 64}, {DR_HOWL, {50, 0, 0, 0}, {112, 70, 60, 60}, 70, 40, 40},
                 {DR_SCRAPE, {38, 45, 0, 0}, {80, 100, 90, 70}, 80, 30, 88}, {DR_GEIGER, {66, 0, 0, 0}, {110, 40, 95, 70}, 50, 30, 64}},
     {85, 35, 10, 70, 80, 75, 0, 50, 50, 80, 50, 35, 95, 65, 50, 55}, {50, 50, 50, 50}, {35, 65, 55, 50}},
    {"ASH", {{DR_SMEAR, {48, 0, 0, 0}, {100, 80, 80, 90}, 100, 50, 40}, {DR_CHOIR, {36, 43, 48, 0}, {5, 40, 70, 80}, 100, 50, 88},
             {DR_WIND, {55, 0, 0, 0}, {90, 60, 30, 60}, 110, 40, 64}, {DR_SMEAR, {60, 67, 0, 0}, {60, 110, 100, 70}, 100, 60, 24}},
     {30, 0, 0, 40, 90, 30, 0, 25, 110, 75, 45, 50, 124, 72, 80, 85}, {20, 40, 30, 70}, {30, 40, 40, 30}},
    /* 9..16 */
    {"INFECTUM", {{DR_CHOIR, {31, 38, 43, 0}, {30, 55, 60, 100}, 110, 40, 40}, {DR_HUM, {19, 0, 0, 0}, {40, 30, 90, 85}, 100, 30, 64},
                  {DR_SMEAR, {55, 62, 0, 0}, {110, 70, 90, 70}, 110, 50, 88}, {DR_HOWL, {74, 0, 0, 0}, {60, 80, 20, 45}, 120, 60, 24}},
     {35, 0, 0, 40, 85, 40, 0, 20, 100, 78, 35, 50, 126, 80, 70, 90}, {15, 30, 30, 75}, {40, 85, 25, 25}},
    {"MEATGRND", {{DR_SWARM, {26, 33, 0, 0}, {95, 70, 125, 110}, 20, 40, 40}, {DR_CHAOS, {38, 0, 0, 0}, {110, 95, 110, 85}, 20, 50, 88},
                  {DR_SCRAPE, {29, 36, 0, 0}, {110, 90, 120, 80}, 30, 40, 64}, {DR_ROT, {45, 0, 0, 0}, {110, 110, 100, 60}, 30, 60, 24}},
     {115, 50, 30, 105, 75, 70, 0, 60, 30, 80, 40, 40, 70, 70, 40, 40}, {75, 60, 60, 30}, {60, 90, 40, 70}},
    {"REMOTE", {{DR_STATIC, {69, 0, 0, 0}, {60, 70, 110, 90}, 40, 30, 30}, {DR_WIND, {40, 0, 0, 0}, {50, 75, 60, 100}, 80, 50, 64},
                {DR_HIVE, {45, 57, 0, 0}, {20, 40, 25, 90}, 100, 40, 98}, {DR_GEIGER, {78, 0, 0, 0}, {25, 30, 15, 70}, 30, 20, 50}},
     {20, 20, 10, 30, 70, 40, 20, 40, 105, 75, 30, 55, 115, 85, 45, 70}, {35, 45, 30, 55}, {20, 30, 75, 30}},
    {"SOYLENT", {{DR_HOWL, {45, 46, 0, 0}, {100, 40, 85, 100}, 60, 70, 30}, {DR_HOWL, {57, 58, 0, 0}, {95, 55, 80, 90}, 60, 70, 98},
                  {DR_CHOIR, {33, 40, 0, 0}, {100, 70, 100, 90}, 80, 60, 64}, {DR_CHAOS, {69, 0, 0, 0}, {80, 40, 50, 65}, 60, 50, 64}},
     {60, 0, 0, 50, 92, 60, 0, 75, 60, 85, 60, 50, 105, 60, 110, 70}, {85, 70, 70, 65}, {80, 40, 20, 90}},
    {"^SIEVERT", {{DR_GEIGER, {72, 0, 0, 0}, {115, 60, 90, 85}, 10, 20, 40}, {DR_HUM, {31, 0, 0, 0}, {100, 80, 40, 80}, 40, 20, 64},
                  {DR_STATIC, {81, 0, 0, 0}, {40, 30, 60, 50}, 30, 30, 90}, {DR_HIVE, {28, 35, 0, 0}, {50, 40, 70, 75}, 90, 40, 20}},
     {55, 35, 25, 55, 95, 35, 0, 30, 40, 50, 70, 30, 85, 55, 40, 45}, {40, 50, 40, 40}, {25, 60, 60, 45}},
    {"ABYSS", {{DR_WIND, {33, 0, 0, 0}, {30, 100, 110, 95}, 90, 50, 30}, {DR_SMEAR, {52, 0, 0, 0}, {90, 100, 60, 75}, 110, 50, 98},
                {DR_CHOIR, {45, 52, 0, 0}, {60, 100, 40, 60}, 110, 60, 64}, {DR_HUM, {21, 0, 0, 0}, {20, 20, 110, 70}, 100, 40, 64}},
     {25, 0, 0, 30, 80, 25, 0, 35, 110, 70, 40, 45, 120, 90, 60, 80}, {20, 40, 40, 70}, {35, 70, 50, 35}},
    {"FROST", {{DR_SWARM, {28, 0, 0, 0}, {70, 30, 90, 95}, 60, 30, 8}, {DR_HOWL, {64, 71, 0, 0}, {105, 30, 70, 70}, 60, 40, 120},
                {DR_ROT, {40, 0, 0, 0}, {60, 80, 70, 70}, 70, 50, 20}, {DR_SCRAPE, {52, 0, 0, 0}, {80, 110, 60, 65}, 70, 50, 108}},
     {70, 15, 0, 60, 85, 55, 0, 45, 75, 70, 50, 45, 100, 65, 50, 55}, {40, 55, 45, 45}, {20, 70, 30, 45}},
    {"OMEGA", {{DR_SCRAPE, {33, 40, 0, 0}, {95, 100, 80, 85}, 50, 30, 30}, {DR_HUM, {28, 0, 0, 0}, {80, 110, 70, 80}, 60, 20, 64},
               {DR_GEIGER, {60, 0, 0, 0}, {60, 100, 100, 60}, 30, 30, 98}, {DR_ROT, {52, 0, 0, 0}, {90, 70, 50, 55}, 50, 40, 50}},
     {75, 25, 15, 65, 75, 60, 0, 40, 35, 75, 45, 50, 75, 70, 40, 50}, {45, 45, 50, 40}, {30, 75, 50, 50}},
};
static void scene_build(scene_t *s, const fs_t *f, uint8_t land)
{
    dr_ctl_t d;
    uint32_t k, j;
    dr_defaults(&d);
    for (k = 0; k < DR_LAYERS; k++) {
        dr_layer_t *l = &d.L[k];
        l->on = 1;                                       /* PLAY brings all four in */
        l->engine = f->l[k].e;
        l->nv = 0;
        for (j = 0; j < DR_VOICES; j++) if (f->l[k].n[j]) l->note[l->nv++] = f->l[k].n[j];
        if (!l->nv) l->note[l->nv++] = 48;
        for (j = l->nv; j < DR_VOICES; j++) l->note[j] = l->note[0];
        for (j = 0; j < 4u; j++) l->p[j] = f->l[k].k[j];
        l->swell = f->l[k].swell; l->drift = f->l[k].drift; l->pan = f->l[k].pan;
        s->L[k] = *l;
    }
    for (k = 0; k < FX_N; k++) s->fx[k] = f->fx[k];
    for (k = 0; k < MOD_N; k++) s->mod[k] = f->mod[k];
    for (k = 0; k < GL_N; k++) s->glob[k] = f->glob[k];
    for (k = 0; k < 10u; k++) s->name[k] = f->name[k];
    s->land = (uint8_t)(land * 16u + 8u);
}
static const uint8_t LANDMAP[NSCENES] = {LAND_REACTOR, LAND_PEAKS, LAND_DUNES, LAND_BUNKER, LAND_PEAKS, LAND_FACTORY,
                                         LAND_REACTOR, LAND_DUNES,
                                         LAND_REACTOR, LAND_FACTORY, LAND_CHANNEL, LAND_BUNKER, LAND_CONTROL, LAND_DUNES,
                                         LAND_PEAKS, LAND_FACTORY};
/* scenes renamed since (L23): a saved scene still carrying an old name takes the new one */
static void scenes_rename(void)
{
    static const char *const OLD[5][2] = {{"WISHGRNT", "INFECTUM"}, {"PSYKOSYS", "SOYLENT"}, {"STRAND", "ABYSS"},
                                          {"DIVIDE", "FROST"}, {"ANNEX", "OMEGA"}};
    for (uint32_t i = 0; i < NSCENES; i++)
        for (uint32_t k = 0; k < 5u; k++) {
            uint32_t c = 0;
            while (c < 10u && OLD[k][0][c] && st.sc[i].name[c] == OLD[k][0][c]) c++;
            if (OLD[k][0][c] || (c < 10u && st.sc[i].name[c])) continue;     /* (not this name) */
            for (c = 0; c < 10u; c++) st.sc[i].name[c] = 0;
            for (c = 0; OLD[k][1][c]; c++) st.sc[i].name[c] = OLD[k][1][c];
        }
}
static void scene_default(scene_t *s, uint32_t i) { scene_build(s, &FACTORY[i % NSCENES], LANDMAP[i % NSCENES]); }
/* L12 and before: 9..16 were 1..8 again. Each still as it was then becomes its new scene; one saved over keeps its
 * sound and takes the new name */
static void scenes_upgrade(void)
{
    for (uint32_t i = 8; i < NSCENES; i++) {
        scene_t old;
        scene_build(&old, &FACTORY[i - 8u], LANDMAP[i - 8u]);
        for (uint32_t k = 0; k < 10u; k++) old.name[k] = FACTORY[i - 8u].name[k];
        if (!memcmp(&old, &st.sc[i], sizeof old)) scene_default(&st.sc[i], i);
        else for (uint32_t k = 0; k < 10u; k++) st.sc[i].name[k] = FACTORY[i].name[k];
    }
}
static int store_valid(void)
{
    uint32_t i, k;
    if (st.magic != SC_MAGIC || (st.version != SC_VERSION && st.version != 4u && st.version != SC_VERSION_5 && st.version != SC_VERSION_6 && st.version != SC_VERSION_7 && st.version != SC_VERSION_8 && st.version != SC_VERSION_9 && st.version != SC_VERSION_10) || st.cur >= NSCENES || st.octave_p3 > 5u || st.morph > 127u) return 0;
    for (i = 0; i < NSCENES; i++)
        for (k = 0; k < DR_LAYERS; k++)
            if (st.sc[i].L[k].engine >= DR_ENGINES || !st.sc[i].L[k].nv || st.sc[i].L[k].nv > DR_VOICES) return 0;
    return 1;
}

/* --------------------------------------------------------------------------------------------- the sound --- */
static float out_f[2 * APP_BLOCK];
static float gain_now;
/* silent from power-on until a layer first sounds (PLAY, a note, MIDI): nothing at all while it starts, the global
 * hiss and hum included, and never before the platform has read MASTER (its first half second runs at half volume,
 * whatever the knob says). Then in over the usual few milliseconds */
static volatile uint8_t out_open;
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    uint32_t i;
    float gain = out_open ? (float)master_q12 * (1.0f / 4096.0f) * 8300000.0f : 0.0f;
    if (n > APP_BLOCK) n = APP_BLOCK;
    dr_render(out_f, n);
    for (i = 0; i < n; i++) {
        gain_now += (gain - gain_now) * 0.004f;
        lr[2 * i] = (int32_t)(out_f[2 * i] * gain_now);
        lr[2 * i + 1] = (int32_t)(out_f[2 * i + 1] * gain_now);
    }
}
void app_panic(void) { dr_panic(); gain_now = 0.0f; }
static volatile uint32_t overloads;
void app_overload(void) { overloads++; }   /* the render took over 90 % of its time: shown red in the header */

void app_init(void)
{
    uint32_t i;
    int got = plat_store_load(OBJ_SCENES, &st, sizeof st);
    uint32_t was = st.version;
    if (got == (int)offsetof(store_t, pk) && st.version == 4u) {   /* saved by L8 or before: the punches as they were */
        st.version = SC_VERSION;
        for (i = 0; i < PU_N; i++) for (uint32_t k = 0; k < 4u; k++) st.pk[i][k] = DR_PK_DEF[i][k];
    } else if (got == (int)sizeof st && (st.version == SC_VERSION_5 || st.version == SC_VERSION_6)) {   /* L9..L11: FADE anew */
        st.version = SC_VERSION;
        for (i = 0; i < PU_N; i++) st.pk[i][3] = DR_PK_DEF[i][3];
    } else if (got == (int)sizeof st && (st.version == SC_VERSION_7 || st.version == SC_VERSION_8 || st.version == SC_VERSION_9 || st.version == SC_VERSION_10)) st.version = SC_VERSION;   /* L12..L28 */
    else if (got != (int)sizeof st || st.version != SC_VERSION) got = -1;
    if (got >= 0 && was < SC_VERSION && store_valid()) {
        for (i = 0; i < NSCENES; i++) for (uint32_t k = 0; k < DR_LAYERS; k++) if (st.sc[i].L[k].glide == 30u) st.sc[i].L[k].glide = 0;   /* GLIDE: 3 ms */
    }
    if (got >= 0 && was < SC_VERSION_10 && store_valid()) {
        for (i = 0; i < NSCENES; i++) {  /* the places: five over 0..127 (8 and before), six (9), now place*16+8 */
            uint32_t pl = st.sc[i].land * (was == SC_VERSION_9 ? 6u : 5u) / 128u;
            if (was == SC_VERSION_8 && i == 12u && pl == LAND_REACTOR) pl = LAND_CONTROL;   /* 13 µSIEVERT: the control room */
            if (was >= SC_VERSION_8 && i == 10u && pl == LAND_PEAKS) pl = LAND_CHANNEL;     /* 11 REMOTE: the channel */
            st.sc[i].land = (uint8_t)(pl * 16u + 8u);
        }
        if (was < SC_VERSION_8) scenes_upgrade();
    }
    if (got < 0 || !store_valid()) {
        st.magic = SC_MAGIC; st.version = SC_VERSION; st.cur = 0; st.octave_p3 = 1; st.morph = 50;
        for (i = 0; i < NSCENES; i++) scene_default(&st.sc[i], i);
        for (i = 0; i < PU_N; i++) for (uint32_t k = 0; k < 4u; k++) st.pk[i][k] = DR_PK_DEF[i][k];
    }
    scenes_rename();
    octave = (int8_t)((int)st.octave_p3 - 3);
    dr_defaults(C);
    scene_to_ctl(&st.sc[st.cur], 0);
    for (i = 0; i < PU_N; i++) for (uint32_t k = 0; k < 4u; k++) C->pk[i][k] = st.pk[i][k] < 128u ? st.pk[i][k] : DR_PK_DEF[i][k];
    armed = 0;
    for (i = 0; i < DR_LAYERS; i++) if (st.sc[st.cur].L[i].on) armed |= (uint8_t)(1u << i);
    C->capture = 1;
    dr_init();
    (void)plat_cpu2_set(1);              /* the second core: layers 3 and 4 (one core if it cannot) */
}

/* ------------------------------------------------------------------------------------------------ the UI --- */
enum { PG_LAYER, PG_SHAPE, PG_MIX, PG_PAN, PG_DIRT, PG_FILTER, PG_DELAY, PG_REVERB, PG_WEATHER, PG_SCENE, PG_GLOBAL, PG_OUT, PG_MACRO, PG_PUNCH, PG_MACRO2, NPG };
static const char *const PAGE_NAME[NPG] = {"LAYER", "SHAPE", "MIX", "PAN", "DIRT", "FILTER", "DELAY", "REVERB", "WEATHER", "SCENE", "GLOBAL", "OUT", "MACRO", "PUNCH", "MACRO 2"};
static const char *const ENG_NAME[DR_ENGINES] = {"HIVE", "SWARM", "ROT", "WIND", "HOWL", "GEIGER", "SMEAR", "HUM",
                                                 "SCRAPE", "CHOIR", "STATIC", "CHAOS"};
static const char *const ENG_K[DR_ENGINES][3] = {
    {"BEAT", "CROSS", "SHARP"}, {"SPREAD", "BRIGHT", "GRIT"}, {"RATIO", "INDEX", "FEEDBK"},
    {"FOCUS", "SWEEP", "RUMBLE"}, {"FEEDBK", "DAMP", "EXCITE"}, {"RATE", "RING", "FIRE"},
    {"SIZE", "SCATTER", "DENSITY"}, {"BUZZ", "RATTLE", "SAG"}, {"PRESS", "METAL", "GRIND"},
    {"VOWEL", "BREATH", "CROWD"}, {"TUNE", "SIGNAL", "FADE"}, {"CHAOS", "BREAK", "GRIT"}};
static const char *const PAGE_K[NPG][4] = {
    {0, 0, 0, "LEVEL"}, {"TUNE", "GLIDE", "SWELL", "DRIFT"}, {"LVL 1", "LVL 2", "LVL 3", "LVL 4"},
    {"PAN 1", "PAN 2", "PAN 3", "PAN 4"}, {"DRIVE", "CRUSH", "RATE", "MIX"}, {"CUTOFF", "RESO", "MODE", "SWEEP"},
    {"TIME", "FDBK", "TONE", "MIX"}, {"SIZE", "DAMP", "MOD", "MIX"}, {"SPEED", "TIMBRE", "FILTER", "ROOM"},
    {"MORPH", "PLACE", 0, 0}, {"DETUNE", "SUB", "HISS", "UNREST"}, {"LEVEL", "PAN", 0, 0}, {"ROT", "VAST", "NAUSEA", "STARVE"}, {0, 0, 0, 0}, {"TANGLE", "RUNAWAY", "RUNOFF", "DELUGE"}};
static const char *const PUNCH_NAME[PU_N] = {"BURN", "VOID", "SHATTER", "WARBLE", "INFINITE", "SHIMMER DOWN", "THROW",
                                              "RING", "DECIMATE", "GATE", "SCREAM", "ROTATE"};
/* a punch's own knobs, while its key is held (drone_dsp.c DR_PK_DEF) */
static const char *const PUNCH_K[PU_N][4] = {
    {"FUZZ", "BITS", 0, "FADE"}, {"SOAK", "DRY", "DARK", "FADE"}, {"SIZE", "REPEAT", "REVERSE", "FADE"},
    {"RATE", "DEPTH", "WOW", "FADE"}, {"FALL", "DARK", "DRAG", "FADE"}, {"MIX", "FEED", 0, "FADE"},
    {"FDBK", "ECHO", 0, "FADE"}, {"FREQ", "WOBBLE", "MIX", "FADE"}, {"RATE", "BITS", 0, "FADE"},
    {"SPEED", "DEPTH", 0, "FADE"}, {"PITCH", "FDBK", "DRIVE", "FADE"}, {"SPEED", "WOBBLE", 0, "FADE"}};
/* the punches' keys (with FX held): the eleven black keys, then the lowest white key (F3) */
static const uint8_t PUNCH_KEY[PU_N] = {1, 3, 5, 8, 10, 13, 15, 17, 20, 22, 25, 0};
static const uint16_t ROT_SHOW[16] = {25, 50, 71, 100, 141, 150, 200, 276, 300, 350, 420, 540, 600, 713, 850, 1130};

static struct {
    uint8_t page, sel;
    int8_t focus;                        /* a held white key's layer: the knobs and the black keys act on it */
    char msg[2][24];
    uint32_t msg_until;
    uint32_t keys, btn;
    uint32_t notekeys;                   /* the keys held now that are playing notes */
    uint32_t pkeys;                      /* the keys held now that are not notes (punches, slots) */
    uint8_t fxmode;                      /* FX mode: the keys switch the punches */
    uint8_t fxpage;
    uint8_t home_used;
    uint8_t env_snap, env_came;          /* ENV held snapped a macro (or reset them): its release is not a page; it brought MACRO */
    uint8_t pedit, pback;                /* the punch whose knobs show (its key held, FX mode; 0: none), the page before */
    uint16_t ptwist;                     /* punches whose knobs were turned while held: no 2 s latch */                   /* HOME held turned SELECT: its release is not LAYER */                      /* the effects page FX mode shows */
    uint16_t punch_latch, punch_skip;     /* latched; held but let go of (a tap that unlatched) */
    uint32_t punch_tap[PU_N];
    uint32_t save_t, sel_used, save_used;
    uint8_t rn, rn_pos;                  /* renaming the scene (SAVE pressed twice): the cursor */
    char rn_buf[9];
    uint8_t redraw, scope_due;
    uint32_t drawn_at, turned_at[4];
    uint8_t k_turned;
} ui;

static void text_cat(char *d, const char *s)
{
    while (*d) d++;
    while (*s) *d++ = *s++;
    *d = 0;
}
static void int_text(int32_t v, char *b)
{
    char t[12];
    int n = 0;
    uint32_t u = (uint32_t)(v < 0 ? -v : v);
    do t[n++] = (char)('0' + u % 10u); while ((u /= 10u) && n < 11);
    if (v < 0) *b++ = '-';
    while (n) *b++ = t[--n];
    *b = 0;
}
/* a value in tenths: "1.5" */
static void tenths_text(int32_t v, char *b)
{
    char t[12];
    if (v < 0) { *b++ = '-'; v = -v; }
    int_text(v / 10, b);
    t[0] = '.'; t[1] = (char)('0' + v % 10); t[2] = 0;
    text_cat(b, t);
}
static void note_name(int32_t note, char *b)
{
    static const char *const N[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    char o[12];
    b[0] = 0;
    text_cat(b, N[note % 12]);
    int_text(note / 12 - 1, o);
    text_cat(b, o);
}

void app_say(const char *a, const char *b)
{
    uint32_t i;
    char t[48];
    for (i = 0; i < 23u && a && a[i]; i++) ui.msg[0][i] = a[i];
    ui.msg[0][i] = 0;
    for (i = 0; i < 23u && b && b[i]; i++) ui.msg[1][i] = b[i];
    ui.msg[1][i] = 0;
    ui.msg_until = plat_ms() + 1400u;
    ui.redraw = 1;
    if (ann_on()) {
        t[0] = 0;
        text_cat(t, ui.msg[0]);
        if (ui.msg[1][0]) { text_cat(t, " "); text_cat(t, ui.msg[1]); }
        ann_say(t, 0, ANN_NOW);
    }
}

/* the layer the knobs, the black keys and the screen act on: a held white key's, or the selected one */
static uint32_t cur(void) { return ui.focus >= 0 ? (uint32_t)ui.focus : ui.sel; }

/* the byte a page's knob k edits (0: none) */
static uint8_t *knob_ref(uint32_t page, uint32_t k)
{
    dr_layer_t *l = &C->L[cur()];
    switch (page) {
    case PG_LAYER: return &l->p[k];
    case PG_SHAPE: return k == 0 ? &l->tune : k == 1 ? &l->glide : k == 2 ? &l->swell : &l->drift;
    case PG_MIX: return &C->L[k].p[3];
    case PG_PAN: return &C->L[k].pan;
    case PG_DIRT: case PG_FILTER: case PG_DELAY: case PG_REVERB: return &C->fx[(page - PG_DIRT) * 4u + k];
    case PG_WEATHER: return &C->mod[k];
    case PG_SCENE: return k == 0 ? &st.morph : k == 1 ? &st.sc[st.cur].land : 0;
    case PG_GLOBAL: return &C->glob[k];
    case PG_MACRO: return &C->macro[k];
    case PG_MACRO2: return &C->macro[MC_TANGLE + k];
    case PG_PUNCH: return k < 4u && ui.pedit && PUNCH_K[ui.pedit - 1u][k] ? &C->pk[ui.pedit - 1u][k] : 0;
    case PG_OUT: return k == 0 ? &l->p[3] : k == 1 ? &l->pan : 0;
    }
    return 0;
}
static const char *knob_label(uint32_t page, uint32_t k)
{
    if (page == PG_LAYER && k < 3u) return ENG_K[C->L[cur()].engine][k];
    if (page == PG_PUNCH) return k < 4u && ui.pedit ? PUNCH_K[ui.pedit - 1u][k] : 0;
    return PAGE_K[page][k];
}
/* a knob's value as the screen shows it (no float: the UI never computes one) */
static void knob_value(uint32_t page, uint32_t k, char *t)
{
    const uint8_t *r = knob_ref(page, k);
    int32_t v = r ? *r : 0;
    t[0] = 0;
    if (!r) return;
    if (page == PG_LAYER && k == 0 && C->L[cur()].engine == DR_ROT) {
        int32_t i = (int32_t)(v * 1599 / 12700), x = ROT_SHOW[i];   /* the ratio, in hundredths */
        int_text(x / 100, t);
        char f[4] = {'.', (char)('0' + x / 10 % 10), (char)('0' + x % 10), 0};
        text_cat(t, f);
        return;
    }
    if (page == PG_SHAPE && k == 0) {    /* semitones, one decimal: +-12 over 63 steps */
        int32_t d = (v - 64) * 120 / 63;
        if (d > 0) { t[0] = '+'; t[1] = 0; tenths_text(d, t + 1); } else tenths_text(d, t);
        return;
    }
    if (page == PG_PAN || (page == PG_OUT && k == 1)) {
        int32_t d = (v - 64) * 100 / 63;
        if (d < -2) { text_cat(t, "L"); int_text(-d, t + 1); }
        else if (d > 2) { text_cat(t, "R"); int_text(d, t + 1); }
        else text_cat(t, "C");
        return;
    }
    if (page == PG_SCENE && k == 0) {   /* seconds: 0.1 + (v/127)^2 x 20 */
        tenths_text((int32_t)(1 + v * v * 200 / 16129), t);
        text_cat(t, "S");
        return;
    }
    {   /* times, as the render computes them (drone_dsp.c) */
        float u = (float)v / 127.0f, sec = -1.0f;
        if (page == PG_SHAPE && k == 1) sec = 0.003f + u * u * 8.0f;
        if (page == PG_SHAPE && k == 2) sec = 0.02f * fm_exp2f(u * 11.0f);
        if (page == PG_REVERB && k == 0) sec = 0.4f * fm_exp2f(u * 6.3f);
        if (page == PG_DELAY && k == 0) sec = 0.02f * fm_exp2f(u * 6.2f);
        if (page == PG_PUNCH && k == 3 && ui.pedit) sec = dr_punch_fade((uint8_t)v);
        if (sec >= 0.0f) {
            if (sec < 1.0f) { int_text((int32_t)(sec * 1000.0f + 0.5f), t); text_cat(t, "MS"); }
            else { tenths_text((int32_t)(sec * 10.0f + 0.5f), t); text_cat(t, "S"); }
            return;
        }
    }
    if (page == PG_SCENE && k == 1) { text_cat(t, LAND_NAME[land_of(v)]); return; }
    if (page == PG_FILTER && k == 2) {
        text_cat(t, v < 22 ? "LOW" : v < 42 ? "LO-BD" : v < 85 ? "BAND" : v < 106 ? "BD-HI" : "HIGH");
        return;
    }
    int_text((v * 100 + 63) / 127, t);
}

static void page_set(uint32_t p)
{
    ui.page = (uint8_t)p;
    app_say(PAGE_NAME[p], p == PG_LAYER || p == PG_SHAPE || p == PG_OUT ? ENG_NAME[C->L[cur()].engine] : "");
}
static void layer_select(int32_t s)
{
    char t[8] = "LAYER ";
    ui.sel = (uint8_t)((s % DR_LAYERS + DR_LAYERS) % DR_LAYERS);
    t[6] = (char)('1' + ui.sel); t[7] = 0;
    app_say(t, ENG_NAME[C->L[ui.sel].engine]);
}
static void engine_set(uint32_t li, uint32_t e)
{
    if (e >= DR_ENGINES || C->L[li].engine == e) return;
    C->L[li].engine = (uint8_t)e;
    dirty = 1;
    char t[8] = "LAYER ";
    t[6] = (char)('1' + li); t[7] = 0;
    app_say(t, ENG_NAME[e]);
}
static void scene_recall(uint32_t i)
{
    uint32_t k, was_on = 0;
    char t[12] = "SCENE ";
    for (k = 0; k < DR_LAYERS; k++) was_on |= C->L[k].on;
    st.cur = (uint8_t)i;
    scene_to_ctl(&st.sc[i], 0);
    armed = 0;
    for (k = 0; k < DR_LAYERS; k++) {
        if (st.sc[i].L[k].on) armed |= (uint8_t)(1u << k);
        C->L[k].on = was_on && st.sc[i].L[k].on;   /* playing: the new scene's layers come in, glided there */
    }
    if (was_on && st.morph) {            /* MORPH: everything glides there over its time */
        C->morph = st.morph;
        morph_until = plat_ms() + 100u + (uint32_t)st.morph * st.morph * 20000u / 16129u;
    }
    dirty = 0;
    int_text((int32_t)i + 1, t + 6);
    {
        static char nm[11];
        for (uint32_t q = 0; q < 10u; q++) nm[q] = st.sc[i].name[q];
        nm[10] = 0;
        app_say(t, was_on ? nm : "PLAY TO START");
    }
}
/* the letters a name can have ('^' is drawn as the micro sign) */
static const char RN_SET[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-./#^";
static int rn_ok(char c) { for (uint32_t i = 0; RN_SET[i]; i++) if (RN_SET[i] == c && c) return 1; return 0; }
static void scene_store(uint32_t i);
static void rename_keep(void)
{
    int32_t n = 8;
    while (n > 0 && ui.rn_buf[n - 1] == ' ') n--;                 /* (trailing spaces off) */
    for (uint32_t q = 0; q < 10u; q++) st.sc[st.cur].name[q] = q < (uint32_t)n ? ui.rn_buf[q] : 0;
    if (n == 0) { const char *d = "SCENE"; for (uint32_t q = 0; d[q]; q++) st.sc[st.cur].name[q] = d[q]; }
    ui.rn = 0;
    scene_store(st.cur);
}
static void scene_store(uint32_t i)
{
    char t[12] = "SAVED ";
    ctl_to_scene(&st.sc[i]);
    st.cur = (uint8_t)i;
    st.octave_p3 = (uint8_t)(octave + 3);
    int_text((int32_t)i + 1, t + 6);
    for (uint32_t q = 0; q < PU_N; q++) for (uint32_t k = 0; k < 4u; k++) st.pk[q][k] = C->pk[q][k];
    if (plat_store_save(OBJ_SCENES, &st, sizeof st) == 0) { dirty = 0; app_say(t, ""); }
    else app_say("SAVE", "FAILED");
}
static int any_on(void) { return C->L[0].on | C->L[1].on | C->L[2].on | C->L[3].on; }

static void play_toggle(void)
{
    uint32_t k;
    if (any_on()) {                      /* out: remember which */
        armed = 0;
        for (k = 0; k < DR_LAYERS; k++) { if (C->L[k].on) armed |= (uint8_t)(1u << k); C->L[k].on = 0; }
        app_say("FADING", "OUT");
    } else {
        if (!armed) armed = 1u << ui.sel;
        for (k = 0; k < DR_LAYERS; k++) C->L[k].on = (armed >> k) & 1u;
        app_say("RISING", "");
    }
}

/* SEL + PLAY: MUTATE, every layer's three engine controls and DRIFT nudged at random (up to +-24), the scene's
 * shape kept; SAVE keeps it, PRESET away and back drops it */
static uint32_t mut_rng = 0x2545F491u;
static void mutate(void)
{
    for (uint32_t i = 0; i < DR_LAYERS; i++)
        for (uint32_t k = 0; k < 3u; k++) {
            mut_rng ^= mut_rng << 13; mut_rng ^= mut_rng >> 17; mut_rng ^= mut_rng << 5;
            int32_t d = (int32_t)(mut_rng % 49u) - 24;
            C->L[i].p[k] = (uint8_t)clamp((int32_t)C->L[i].p[k] + d, 0, 127);
        }
    dirty = 1;
    app_say("MUTATED", "");
}

/* the keyboard: a chord latched on a layer. Keys played together build it (up to four notes; a fifth replaces the
 * last); let everything go and it stays sounding; the next key played afresh starts a new chord on the selected
 * layer, the voices gliding from the old notes to the new. SEQ mutes */
static int8_t chord_layer = -1;          /* the layer the keys held now are playing (-1: none held) */
static void chord_note(uint32_t note, int fresh)
{
    uint32_t li;
    dr_layer_t *l;
    char t[24], nn[8];
    if (note > 127u) note = 127u;
    if (fresh || chord_layer < 0) chord_layer = (int8_t)ui.sel;
    li = (uint32_t)chord_layer;
    l = &C->L[li];
    if (fresh) {
        for (uint32_t j = 0; j < DR_VOICES; j++) l->note[j] = (uint8_t)note;
        l->nv = 1;
    } else {
        for (uint32_t j = 0; j < l->nv; j++) if (l->note[j] == note) return;
        if (l->nv < DR_VOICES) l->note[l->nv++] = (uint8_t)note;
        else l->note[DR_VOICES - 1] = (uint8_t)note;
    }
    dirty = 1;                           /* (a layer muted stays muted: SEQ and PLAY bring it in) */
    ui.focus = (int8_t)li;
    t[0] = 0;
    for (uint32_t j = 0; j < l->nv; j++) { note_name(l->note[j], nn); if (j) text_cat(t, " "); text_cat(t, nn); }
    {
        char a[10] = "LAYER ";
        a[6] = (char)('1' + li); a[7] = 0;
        app_say(a, t);
    }
}

/* MIDI in (USB or the TRS jack): notes build and latch chords as the keys do; a note off lets its pressure go.
 * CC 1 the layer's K1, 7 its level, 74 the cutoff, 71 the resonance, 91 the reverb, 93 the delay; aftertouch is
 * the key's pressure */
static uint8_t midi_down[128];
static uint32_t midi_held;
static void midi_note(uint32_t note, uint32_t vel)
{
    if (!vel) {
        if (midi_down[note]) {
            midi_down[note] = 0;
            if (midi_held) midi_held--;
            if (!midi_held && chord_layer >= 0 && !ui.notekeys) { C->L[chord_layer].push = 0; chord_layer = -1; ui.focus = -1; }
        }
        return;
    }
    if (midi_down[note]) return;
    chord_note(note, !midi_held && !ui.notekeys);
    midi_down[note] = 1;
    midi_held++;
}
static void midi_poll(void)
{
    uint32_t pkt, guard = 64;
    int port;
    while (guard-- && plat_midi_in(&pkt, &port)) {
        uint32_t st8 = (pkt >> 8) & 0xFFu, kind = st8 & 0xF0u, d1 = (pkt >> 16) & 0x7Fu, d2 = (pkt >> 24) & 0x7Fu;
        if (kind == 0x90u) midi_note(d1, d2);
        else if (kind == 0x80u) midi_note(d1, 0);
        else if (kind == 0xA0u || kind == 0xD0u) { if (chord_layer >= 0) C->L[chord_layer].push = (uint8_t)(kind == 0xA0u ? d2 : d1); }
        else if (kind == 0xB0u) {
            uint8_t *r = d1 == 1u ? &C->L[cur()].p[0] : d1 == 7u ? &C->L[cur()].p[3] : d1 == 74u ? &C->fx[FX_CUT] :
                         d1 == 71u ? &C->fx[FX_RESO] : d1 == 91u ? &C->fx[FX_VERB] : d1 == 93u ? &C->fx[FX_ECHO] : 0;
            if (r) { *r = (uint8_t)d2; ui.redraw = 1; }
            if (d1 == 123u || d1 == 120u) { for (uint32_t n = 0; n < 128u; n++) midi_down[n] = 0; midi_held = 0; }
        }
    }
}

static int32_t accel(int32_t e)          /* a fast turn goes further */
{
    int32_t a = e < 0 ? -e : e;
    return a >= 4 ? e * 4 : a >= 2 ? e * 2 : e;
}

static void input(void)
{
    static const uint8_t WK[16] = PLAT_WHITE_KEYS, BK[11] = PLAT_BLACK_KEYS;
    uint32_t keys, btn = plat_buttons(), now = plat_ms(), k, i;
    uint32_t bdown = btn & ~ui.btn, kdown, kup;
    int32_t e;
    touch_poll();
    keys = touch_keys();
    midi_poll();
    kdown = keys & ~ui.keys; kup = ui.keys & ~keys;
    uint32_t white = 0, black = 0;
    for (i = 0; i < 16u; i++) white |= 1u << WK[i];
    for (i = 0; i < 11u; i++) black |= 1u << BK[i];

    /* SAVE and SEL held: a white key is a scene slot */
    int save_held = (btn >> B_SAVE) & 1u, sel_held = (btn >> B_SEL) & 1u;
    /* SAVE pressed twice (within 0.4 s): rename the scene. K1 the cursor, K2 the letter; SAVE keeps it (and saves),
     * any other button lets it go */
    if (ui.rn && (bdown & ~(1u << B_SAVE))) { ui.rn = 0; app_say("NAME", "AS IT WAS"); }
    if ((bdown >> B_SAVE) & 1u) {
        if (ui.rn) { rename_keep(); ui.save_used = 1; ui.save_t = 0; }
        else if (now - ui.save_t < 400u) {
            ui.rn = 1; ui.rn_pos = 0; ui.save_used = 1;
            for (uint32_t q = 0; q < 8u; q++) { char c = st.sc[st.cur].name[q]; ui.rn_buf[q] = rn_ok(c) ? c : ' '; }
            ui.rn_buf[8] = 0;
            app_say("RENAME", "K1 MOVE K2 LETTER");
            ui.save_t = 0;
        } else { ui.save_used = 0; ui.save_t = now; }
        if (!ui.rn && ui.save_used) ui.save_t = 0;               /* (a press that kept a name starts no double press) */
    }
    if ((bdown >> B_SEL) & 1u) { ui.sel_used = 0; spring = *C; spring_on = 1; spring_turned = 0; }
    if (spring_on && ui.sel_used) spring_on = 0;                 /* a scene or a mutation: it stays */
    if (spring_on && !sel_held) {
        spring_on = 0;
        if (spring_turned) { spring_back(); app_say("SPRUNG BACK", ""); }
    }

    /* FX: a mode. In it the keys switch the punches: on while held; held 2 s, or tapped twice, latched; pressed
     * again, off. ALGO turns the effects pages. FX again: the keys are notes again (latched punches stay) */
    if ((bdown >> B_FX) & 1u) {          /* FX: into FX mode; in it, the next effects page (DIRT, FILTER, DELAY, REVERB);
                                          * from another page, back to the effects. HOME leaves FX mode */
        if (!ui.fxmode) {
            ui.fxmode = 1;
            if (ui.fxpage < PG_DIRT || ui.fxpage > PG_REVERB) ui.fxpage = PG_DIRT;
            ui.page = ui.fxpage;
            app_say("FX", "KEYS ARE EFFECTS");
        } else {
            if (ui.page >= PG_DIRT && ui.page <= PG_REVERB) ui.fxpage = (uint8_t)(PG_DIRT + (ui.page - PG_DIRT + 1u) % 4u);
            else if (ui.fxpage < PG_DIRT || ui.fxpage > PG_REVERB) ui.fxpage = PG_DIRT;
            ui.pedit = 0;
            page_set(ui.fxpage);
        }
        ui.redraw = 1;
    }
    int fx_held = ui.fxmode;
    if (ui.fxmode) ui.pkeys |= kdown;
    for (i = 0; i < PU_N; i++) {
        uint32_t bit = 1u << PUNCH_KEY[i], pb = 1u << i;
        if (kdown & bit & ui.pkeys) {
            if (ui.punch_latch & pb) { ui.punch_latch &= (uint16_t)~pb; app_say(PUNCH_NAME[i], "OFF"); ui.punch_skip |= (uint16_t)pb; }
            else if (now - ui.punch_tap[i] < 350u) { ui.punch_latch |= (uint16_t)pb; app_say(PUNCH_NAME[i], "LATCHED"); }
            else app_say(PUNCH_NAME[i], "");
            ui.punch_tap[i] = now;
            ui.ptwist &= (uint16_t)~pb;
            if (ui.page != PG_PUNCH) ui.pback = ui.page;         /* its knobs on K1..K3 while it is held */
            ui.pedit = (uint8_t)(i + 1u); ui.page = PG_PUNCH; ui.redraw = 1;
        }
        if (kup & bit) ui.punch_skip &= (uint16_t)~pb;
        else if ((keys & ui.pkeys & bit) && !(ui.punch_latch & pb) && !(ui.punch_skip & pb) && !(ui.ptwist & pb) &&
                 now - ui.punch_tap[i] >= 2000u) {
            ui.punch_latch |= (uint16_t)pb;                      /* held 2 s: latched */
            app_say(PUNCH_NAME[i], "LATCHED");
        }
    }
    if (ui.page == PG_PUNCH) {           /* the punch keys all let go (or FX mode left): the page it was on */
        uint32_t held = 0;
        for (i = 0; i < PU_N; i++) if ((keys & ui.pkeys) >> PUNCH_KEY[i] & 1u) held |= 1u << i;
        if (!held || !ui.fxmode) { ui.page = ui.pback; ui.pedit = 0; ui.redraw = 1; }
        else if (!((held >> (ui.pedit - 1u)) & 1u))           /* the newest let go: another still held shows */
            for (i = 0; i < PU_N; i++) if ((held >> i) & 1u) ui.pedit = (uint8_t)(i + 1u);
    }
    {
        uint16_t p = ui.punch_latch;
        for (i = 0; i < PU_N; i++)
            if (((keys & ui.pkeys) >> PUNCH_KEY[i]) & 1u && !((ui.punch_skip >> i) & 1u)) p |= (uint16_t)(1u << i);
        C->punch = p;
    }
    /* SAVE, SEL or GLO held: the white keys are scene slots (SAVE store, SEL recall) or layers 1-4 in and out (GLO) */
    for (i = 0; i < 16u; i++) {
        uint32_t bit = 1u << WK[i];
        if (!(kdown & bit) || (ui.pkeys & bit)) continue;
        if (save_held) { scene_store(i); ui.save_used = 1; ui.pkeys |= bit; }
        else if (sel_held) { scene_recall(i); ui.sel_used = 1; ui.pkeys |= bit; }
        else if (((btn >> B_GLO) & 1u) && i < DR_LAYERS) {
            dr_layer_t *l = &C->L[i];
            char t[8] = "LAYER ";
            l->on = !l->on;
            if (l->on) armed |= (uint8_t)(1u << i);
            t[6] = (char)('1' + i); t[7] = 0;
            app_say(t, l->on ? "IN" : "OUT");
            ui.pkeys |= bit;
        }
    }
    if (save_held || sel_held || ((btn >> B_GLO) & 1u)) ui.pkeys |= kdown & black;
    /* every other key a note: the chord */
    {
        uint32_t play = kdown & ~ui.pkeys;
        int fresh = !ui.notekeys && !midi_held;
        for (k = 0; k < NKEYS; k++)
            if ((play >> k) & 1u) { chord_note((uint32_t)clamp(53 + (int32_t)k + 12 * octave, 0, 127), fresh); fresh = 0; }
        ui.notekeys = (ui.notekeys | play) & keys;
        ui.pkeys &= keys;
        if (chord_layer >= 0) {
            if (!ui.notekeys && !midi_held) { C->L[chord_layer].push = 0; chord_layer = -1; ui.focus = -1; ui.redraw = 1; }
            else {
                uint32_t pr = 0;
                for (k = 0; k < NKEYS; k++) if ((ui.notekeys >> k) & 1u && touch_pressure(k) > pr) pr = touch_pressure(k);
                if (ui.notekeys) C->L[chord_layer].push = (uint8_t)pr;
            }
        }
    }

    /* the buttons */
    if ((bdown >> B_HOME) & 1u) ui.home_used = 0;
    if (!((btn >> B_HOME) & 1u) && ((ui.btn >> B_HOME) & 1u) && !ui.home_used) {  /* HOME tapped: the layer's pages in turn; */
        if (ui.fxmode) { ui.fxmode = 0; app_say("KEYS", "NOTES"); page_set(PG_LAYER); }   /* in FX mode, out of it to LAYER */
        else page_set(ui.page == PG_LAYER ? PG_SHAPE : ui.page == PG_SHAPE ? PG_OUT : PG_LAYER);
    }
    if ((bdown >> B_EDIT) & 1u) page_set(PG_SHAPE);
    /* ENV: pressed elsewhere, MACRO; let go on MACRO or MACRO 2 with nothing done, the next (MACRO, MACRO 2, GLOBAL).
     * Held, a macro knob snaps: clockwise full, anticlockwise off. Held + GLO: every macro off (as MONOEATER.HARSH) */
    if ((bdown >> B_ENV) & 1u) {
        ui.env_snap = 0; ui.env_came = 0;
        if (ui.page != PG_MACRO && ui.page != PG_MACRO2) { page_set(PG_MACRO); ui.env_came = 1; }
    }
    if (!((btn >> B_ENV) & 1u) && ((ui.btn >> B_ENV) & 1u) && !ui.env_snap && !ui.env_came)
        page_set(ui.page == PG_MACRO ? PG_MACRO2 : PG_GLOBAL);
    if (((bdown >> B_GLO) & 1u) && ((btn >> B_ENV) & 1u)) {
        for (k = 0; k < MC_N; k++) C->macro[k] = 0;
        ui.env_snap = 1;
        app_say("MACROS", "ALL OFF");
    } else if ((bdown >> B_GLO) & 1u) page_set(ui.page == PG_MIX ? PG_PAN : PG_MIX);
    if ((bdown >> B_LFO) & 1u) page_set(ui.page == PG_WEATHER ? PG_SCENE : PG_WEATHER);
    if ((bdown >> B_PLAY) & 1u) {
        if (sel_held) { mutate(); ui.sel_used = 1; } else play_toggle();
    }
    if ((bdown >> B_SEQ) & 1u) {
        dr_layer_t *l = &C->L[ui.sel];
        l->on = !l->on;
        if (l->on) armed |= (uint8_t)(1u << ui.sel);
        char t[8] = "LAYER ";
        t[6] = (char)('1' + ui.sel); t[7] = 0;
        app_say(t, l->on ? "IN" : "OUT");
    }
    if ((bdown >> B_ARP) & 1u) { C->freeze = !C->freeze; app_say("FREEZE", C->freeze ? "ON" : "OFF"); }
    if ((bdown >> B_REC) & 1u) { C->capture = !C->capture; app_say("SMEAR MEMORY", C->capture ? "RECORDING" : "HELD"); }
    if (((btn >> B_GLO) & 1u) && ((bdown >> B_OCTDN) & 1u || (bdown >> B_OCTUP) & 1u)) {
        /* GLO + OCT: every layer's chord a whole octave down or up together (none moves if one would run off the end) */
        int32_t d = ((bdown >> B_OCTUP) & 1u) ? 12 : -12, ok = 1;
        for (uint32_t li = 0; li < DR_LAYERS; li++)
            for (uint32_t j = 0; j < DR_VOICES; j++) if ((int32_t)C->L[li].note[j] + d < 12 || (int32_t)C->L[li].note[j] + d > 108) ok = 0;
        if (ok) for (uint32_t li = 0; li < DR_LAYERS; li++) for (uint32_t j = 0; j < DR_VOICES; j++) {
            C->L[li].note[j] = (uint8_t)(C->L[li].note[j] + d);
            if (spring_on) spring.L[li].note[j] = C->L[li].note[j];
        }
        app_say("ALL LAYERS", !ok ? "AT THE END" : d > 0 ? "OCTAVE UP" : "OCTAVE DOWN");
    }
    else if (sel_held && ((bdown >> B_OCTDN) & 1u || (bdown >> B_OCTUP) & 1u)) {
        /* SEL + OCT: the selected layer's chord a whole octave down or up (the voices glide there); kept when SEL goes */
        int32_t d = ((bdown >> B_OCTUP) & 1u) ? 12 : -12, ok = 1;
        dr_layer_t *l = &C->L[ui.sel];
        char t[8] = "LAYER 1";
        for (uint32_t j = 0; j < DR_VOICES; j++) if ((int32_t)l->note[j] + d < 12 || (int32_t)l->note[j] + d > 108) ok = 0;
        if (ok) for (uint32_t j = 0; j < DR_VOICES; j++) {
            l->note[j] = (uint8_t)(l->note[j] + d);
            if (spring_on) spring.L[ui.sel].note[j] = l->note[j];
        }
        t[6] = (char)('1' + ui.sel);
        app_say(t, !ok ? "AT THE END" : d > 0 ? "OCTAVE UP" : "OCTAVE DOWN");
    }
    else if ((bdown >> B_OCTDN) & 1u || (bdown >> B_OCTUP) & 1u) {
        char t[4];
        octave = (int8_t)clamp(octave + (((bdown >> B_OCTUP) & 1u) ? 1 : -1), -3, 2);
        t[0] = octave < 0 ? '-' : '+'; t[1] = (char)('0' + (octave < 0 ? -octave : octave)); t[2] = 0;
        app_say("OCTAVE", t);
    }
    /* SAVE let go without a key: the current slot */
    if (!save_held && ((ui.btn >> B_SAVE) & 1u) && !ui.save_used) scene_store(st.cur);

    /* the encoders */
    if ((e = plat_enc(EN_SELECT)) != 0) {
        if ((btn >> B_HOME) & 1u) {      /* HOME held: SELECT turns the layer's own pages: LAYER, SHAPE, OUT */
            static const uint8_t VP[3] = {PG_LAYER, PG_SHAPE, PG_OUT};
            uint32_t at = ui.page == PG_SHAPE ? 1u : ui.page == PG_OUT ? 2u : 0u;
            page_set(VP[(at + (e > 0 ? 1u : 2u)) % 3u]);
            ui.home_used = 1;
        } else {
            layer_select((int32_t)ui.sel + (e > 0 ? 1 : -1));
            if (ui.page == PG_SHAPE || ui.page == PG_OUT) page_set(PG_LAYER);   /* a new layer: its first page */
        }
    }
    if ((e = plat_enc(EN_ALGO)) != 0 && ui.fxmode) {             /* FX mode: ALGO turns the effects pages */
        ui.fxpage = (uint8_t)(PG_DIRT + ((ui.page >= PG_DIRT && ui.page <= PG_REVERB ? (uint32_t)ui.page - PG_DIRT : 0u) + (e > 0 ? 1u : 3u)) % 4u);
        page_set(ui.fxpage);
    } else if (e != 0)
        engine_set(cur(), (uint32_t)((C->L[cur()].engine + (e > 0 ? 1 : DR_ENGINES - 1)) % DR_ENGINES));
    if ((e = plat_enc(EN_PRESET)) != 0) scene_recall((uint32_t)((st.cur + (e > 0 ? 1 : NSCENES - 1)) % NSCENES));
    for (k = 0; k < 4u; k++) {
        if ((e = plat_enc(EN_K1 + (int)k)) == 0) continue;
        if (ui.rn) {                     /* renaming: K1 the cursor, K2 the letter */
            if (k == 0) ui.rn_pos = (uint8_t)clamp((int32_t)ui.rn_pos + (e > 0 ? 1 : -1), 0, 7);
            else if (k == 1) {
                int32_t at = 0, n = (int32_t)(sizeof RN_SET - 1);
                while (at < n && RN_SET[at] != ui.rn_buf[ui.rn_pos]) at++;
                ui.rn_buf[ui.rn_pos] = RN_SET[((at + (e > 0 ? 1 : -1)) % n + n) % n];
            }
            ui.redraw |= 1u;
            continue;
        }
        uint8_t *r = knob_ref(ui.page, k);
        if (!r) continue;
        if (((btn >> B_ENV) & 1u) && (ui.page == PG_MACRO || ui.page == PG_MACRO2)) { *r = e > 0 ? 127u : 0u; ui.env_snap = 1; }
        else if (ui.page == PG_SCENE && k == 1u) *r = (uint8_t)(clamp((int32_t)land_of(*r) + (e > 0 ? 1 : -1), 0, NLAND - 1) * 16 + 8);   /* PLACE: one a click */
        else *r = (uint8_t)clamp((int32_t)*r + accel(e), 0, 127);
        if (spring_on && !spring_turned) { spring_turned = 1; app_say("FOR NOW", "LET GO OF SEL: BACK"); }
        if (ui.page == PG_PUNCH) { ui.ptwist |= (uint16_t)(1u << (ui.pedit - 1u)); }
        else if (ui.page != PG_MACRO && ui.page != PG_MACRO2) dirty = 1;
        ui.turned_at[k] = now;
        ui.k_turned |= (uint8_t)(1u << k);
        ui.redraw = 1;
    }

    /* the lights: the selected layer's note and engine bright, the others' notes dim; punches; the buttons */
    uint32_t lk = 0, dk = 0, lb = 0, db = 0;
    for (k = 0; k < NKEYS; k++) {
        int32_t note = 53 + (int32_t)k + 12 * octave;
        for (uint32_t li = 0; li < DR_LAYERS; li++) {
            const dr_layer_t *l = &C->L[li];
            if (!l->on) continue;
            for (uint32_t j = 0; j < l->nv && j < DR_VOICES; j++)
                if (l->note[j] == note) { if (li == cur()) lk |= 1u << k; else dk |= 1u << k; }
        }
    }
    if (fx_held) for (i = 0; i < PU_N; i++) dk |= 1u << PUNCH_KEY[i];   /* FX mode: the punch keys show */
    if (ui.fxmode) lb |= 1u << B_FX;
    for (i = 0; i < PU_N; i++) if ((C->punch >> i) & 1u) lk |= 1u << PUNCH_KEY[i];
    lk |= keys;
    if (C->freeze) lb |= 1u << B_ARP;
    if (C->capture) lb |= 1u << B_REC;
    if (any_on()) lb |= LED_PLAY_GREEN;
    if (C->L[ui.sel].on) lb |= 1u << B_SEQ;
    if (dirty) db |= 1u << B_SAVE;
    lb |= 1u << (ui.page == PG_LAYER || ui.page == PG_OUT ? B_HOME : ui.page == PG_SHAPE ? B_EDIT : ui.page <= PG_PAN ? B_GLO :
                 ui.page <= PG_REVERB ? B_FX : ui.page == PG_GLOBAL || ui.page == PG_MACRO || ui.page == PG_MACRO2 ? B_ENV : B_LFO);
    plat_lights(lb | btn, lk, db, dk, 1);

    if (keys != ui.keys || btn != ui.btn) ui.redraw = 1;
    ui.keys = keys;
    ui.btn = btn;
}
void app_input_only(void) { input(); }

/* ------------------------------------------------------------------------------------------- the screen --- */
static void draw_header(void)
{
    char t[24];
    uint32_t now = plat_ms();
    cv_begin(240, 24, K_BG);
    if (now < ui.msg_until) {
        int32_t x = cv_text(8, 4, &FONT_B, ui.msg[0], K_BONE);
        cv_text(x + 8, 4, &FONT_B, ui.msg[1], K_TOX);
    } else {
        int32_t x = cv_text(8, 4, &FONT_B, "MONODRONE", K_TOX);
        t[0] = 0; int_text(st.cur + 1, t); text_cat(t, " ");
        { char nm[11]; for (uint32_t q = 0; q < 10u; q++) nm[q] = st.sc[st.cur].name[q]; nm[10] = 0; text_cat(t, nm); }
        x = cv_text(x + 10, 6, &FONT_XS, t, K_ASH);
        cv_text(x + 8, 6, &FONT_XS, ui.page == PG_PUNCH && ui.pedit ? PUNCH_NAME[ui.pedit - 1u] : PAGE_NAME[ui.page], K_BONE);
        static uint32_t seen, red_until;
        if (overloads != seen) { seen = overloads; red_until = now + 2000u; }
        int_text((int32_t)plat_cpu_pct(), t); text_cat(t, "%");
        if (!plat_cpu2_on()) text_cat(t, " 1C");
        cv_text(232 - text_w(&FONT_XS, t), 6, &FONT_XS, t, now < red_until || plat_cpu_pct() > 85u ? K_BLOOD : K_LINE);
    }
    cv_blit(0, 0);
}

static void draw_layers(void)
{
    char t[12];
    cv_begin(240, 72, K_BG);
    for (uint32_t i = 0; i < DR_LAYERS; i++) {
        const dr_layer_t *l = &C->L[i];
        int32_t y = (int32_t)i * 18, on = l->on, m = dr_meter[i];
        uint16_t ink = LAYER_INK[i];
        if (i == cur()) cv_rect(0, y, 240, 18, K_PANEL);
        cv_rect(4, y + 2, 15, 15, on ? ink : K_LINE);
        t[0] = (char)('1' + i); t[1] = 0;
        cv_text(8, y + 1, &FONT_B, t, on ? K_BG : K_ASH);
        cv_text(26, y + 1, &FONT_B, ENG_NAME[l->engine], on ? K_BONE : K_ASH);
        note_name(l->note[0], t);
        {
            int32_t xe = cv_text(98, y + 1, &FONT_B, t, on ? ink : K_ASH);
            if (l->nv > 1u) { char pl[4] = {'+', (char)('0' + l->nv - 1u), 0, 0}; cv_text(xe + 2, y + 4, &FONT_XS, pl, on ? ink : K_ASH); }
        }
        cv_rect(140, y + 6, 94, 7, K_LINE);
        cv_rect(140, y + 6, m * 94 / 255, 7, on ? ink : K_ASH);
        cv_rect(140 + l->p[3] * 93 / 127, y + 3, 1, 13, K_BONE);
    }
    cv_blit(0, 114);
}

/* the place (land.h), 1-bit: ink on night */
#define K_NIGHT RGB(10, 12, 26)
#define K_INK RGB(226, 228, 212)
static void draw_land(void)
{
    static uint32_t last, built = 0xFFFFFFFFu;
    uint32_t now = plat_ms(), want = land_of(st.sc[st.cur].land);
    uint32_t key = want | (uint32_t)st.cur << 8;
    char nm[12];
    if (key != built) { land_make(want, st.cur + 1u); built = key; }
    uint32_t dt = last ? (now - last) / 16u : 1u;
    if (dt > 6u) dt = 6u;
    if (!dt) dt = 1u;
    last = now;
    land_frame(dt, dr_meter[DR_LAYERS], ((int32_t)C->mod[MOD_SPACE] - 50) / 14, dr_events, C->punch, C->freeze);
    cv_begin(240, LH, K_NIGHT);
    for (int32_t y = 0; y < LH; y++)
        for (int32_t x = 0; x < LW; x++)
            if ((lw[y][x >> 3] >> (7 - (x & 7))) & 1u) cv_pset(x, y, K_INK);
    /* a card's frame, rounded, and its tag: the scene's name */
    cv_rect(1, 0, 238, 1, K_INK); cv_rect(1, LH - 1, 238, 1, K_INK);
    cv_rect(0, 1, 1, LH - 2, K_INK); cv_rect(239, 1, 1, LH - 2, K_INK);
    for (uint32_t q = 0; q < 10u; q++) nm[q] = st.sc[st.cur].name[q];
    nm[10] = 0;
    {
        int32_t w = text_w(&FONT_T, nm) + 8;
        cv_rect(3, 3, w, 13, K_NIGHT);
        cv_rect(4, 3, w - 2, 1, K_INK); cv_rect(4, 15, w - 2, 1, K_INK);
        cv_rect(3, 4, 1, 11, K_INK); cv_rect(3 + w - 1, 4, 1, 11, K_INK);
        cv_text(7, 4, &FONT_T, nm, K_INK);
    }
    if (C->freeze) cv_text(236 - text_w(&FONT_T, "FROZEN"), LH - 14, &FONT_T, "FROZEN", K_INK);
    cv_blit(0, 24);
}

static void draw_knobs(void)
{
    char t[12];
    uint32_t now = plat_ms();
    cv_begin(240, 54, K_BG);
    cv_rect(0, 0, 240, 1, K_LINE);
    if (ui.rn) {                         /* renaming: the name big, a cell per letter, the cursor under one */
        cv_text(4, 4, &FONT_XS, "RENAME   K1 MOVE   K2 LETTER   SAVE KEEPS", K_ASH);
        for (uint32_t q = 0; q < 8u; q++) {
            int32_t x = 8 + (int32_t)q * 28;
            char c[2] = {ui.rn_buf[q], 0};
            cv_text(x + 2, 18, &FONT_M, c, q == ui.rn_pos ? K_TOX : K_BONE);
            cv_rect(x, 46, 24, q == ui.rn_pos ? 4 : 1, q == ui.rn_pos ? K_TOX : K_LINE);
        }
        cv_blit(0, 186);
        return;
    }
    for (uint32_t k = 0; k < 4u; k++) {
        int32_t x = 2 + (int32_t)k * 60;
        const uint8_t *r = knob_ref(ui.page, k);
        int lit = ui.turned_at[k] && now - ui.turned_at[k] < 600u;
        const char *lab = knob_label(ui.page, k);
        uint16_t ink = ui.page == PG_MIX || ui.page == PG_PAN ? LAYER_INK[k] : ui.page <= PG_SHAPE || ui.page == PG_OUT ? LAYER_INK[cur()] : K_RUST;
        cv_text(x + 2, 4, &FONT_XS, lab ? lab : "", lit ? K_BONE : K_ASH);
        knob_value(ui.page, k, t);
        if (text_w(&FONT_M, t) <= 57) cv_text(x + 2, 18, &FONT_M, t, lit ? K_BONE : ink);
        else cv_text(x + 2, 24, &FONT_B, t, lit ? K_BONE : ink);
        cv_rect(x + 2, 46, 54, 4, K_LINE);
        if (r) cv_rect(x + 2, 46, *r * 54 / 127, 4, ink);
    }
    cv_blit(0, 186);
}


/* ------------------------------------------------------------------------------------ the loading screen --- */
/* The MONOEATER head (logo.h) in ink on the dark, MONODRONE in its green under it; fog drifting behind the head,
 * ash falling over everything, Geiger sparks, the picture tearing now and then. It comes up out of the dark, holds,
 * and falls away into the panel; anything pressed or turned ends it at once (and does what it does). The splash
 * (APP_SPLASH) is its first frame, still */
#define INTRO_MS 3400u
#define LOGO_X ((240 - LOGO_N) / 2)
#define LOGO_Y 12
#define INTRO_NAME_Y 142
#define INTRO_TAG_Y 182
static uint8_t intro_on;
static uint32_t intro_t0, intro_last, intro_rng = 0x2545F491u;
static struct { int16_t x, y; uint8_t s; } iash[40];       /* x, y in 1/16 px */
static int16_t ispark[5][3];
static inline uint32_t irnd(void) { intro_rng ^= intro_rng << 13; intro_rng ^= intro_rng >> 17; intro_rng ^= intro_rng << 5; return intro_rng; }
static inline int logo_bit(const uint8_t *b, int32_t x, int32_t y)
{
    return (uint32_t)x < LOGO_N && (uint32_t)y < LOGO_N && ((b[(uint32_t)y * LOGO_ROW + ((uint32_t)x >> 3)] >> (7 - (x & 7))) & 1u);
}

/* one frame at t ms (0: the splash, still: the head and the name whole, no weather) */
static void intro_draw(uint32_t t)
{
    static const char *const TAG = "MUSIC FOR IRRADIATED LANDSCAPES";
    int still = t == 0;
    int32_t in = still ? 17 : (int32_t)(t * 17u / 900u);                       /* the head coming up: 0..16 */
    int32_t out = t > INTRO_MS - 450u ? (int32_t)((t - (INTRO_MS - 450u)) * 17u / 450u) : 0;   /* and going */
    int32_t tone = in - out;
    int32_t tear_y = -1, tear_h = 0, tear_dx = 0;
    int32_t fc[6][3];
    if (tone > 16) tone = 16;
    if (!still && (t / 90u) % 9u == 4u) {                       /* now and then the picture tears */
        uint32_t h = (t / 90u) * 2654435761u;
        tear_y = LOGO_Y + (int32_t)((h >> 8) % LOGO_N); tear_h = 3 + (int32_t)((h >> 16) % 9u); tear_dx = (int32_t)((h >> 4) % 15u) - 7;
    }
    for (int32_t i = 0; i < 6; i++) {                           /* the fog: six banks drifting at their own speeds */
        int32_t span = 360, sp = 14 + i * 9;
        fc[i][0] = (int32_t)(((t / (uint32_t)sp) + (uint32_t)i * 97u) % (uint32_t)span) - 60;
        fc[i][1] = i < 4 ? 20 + i * 30 : 214 + (i - 4) * 18;
        fc[i][2] = 38 + (i * 17) % 30;
    }
    if (!still && t - intro_last >= 16u) {                       /* the ash and the sparks move on */
        uint32_t dt = (t - intro_last) / 16u;
        if (dt > 4u) dt = 4u;
        intro_last = t;
        for (int32_t i = 0; i < 40; i++) {
            if (!iash[i].s) { iash[i].x = (int16_t)((irnd() % 240u) * 16u); iash[i].y = (int16_t)(-(int32_t)(irnd() % 240u) * 16); iash[i].s = (uint8_t)(4u + irnd() % 8u); }
            iash[i].y = (int16_t)(iash[i].y + (int32_t)iash[i].s * (int32_t)dt);
            iash[i].x = (int16_t)(iash[i].x + ((int32_t)(irnd() % 5u) - 2 + 3) * (int32_t)dt);
            if (iash[i].y >= 240 * 16 || iash[i].x >= 240 * 16) iash[i].s = 0;
        }
        for (int32_t i = 0; i < 5; i++) if (ispark[i][2] > 0) ispark[i][2]--;
        if ((irnd() % 7u) == 0) {
            int16_t *k = ispark[irnd() % 5u];
            k[0] = (int16_t)(8 + irnd() % 224u); k[1] = (int16_t)(8 + irnd() % 224u); k[2] = 4;
        }
    }
    for (int32_t b = 0; b < 3; b++) {                            /* three bands of 80 rows */
        int32_t y0 = b * 80;
        cv_begin(240, 80, K_BG);
        for (int32_t y = 0; y < 80; y++) {
            int32_t gy = y0 + y, ly = gy - LOGO_Y, sh = gy >= tear_y && gy < tear_y + tear_h ? tear_dx : 0;
            for (int32_t x = 0; x < 240; x++) {
                int32_t lx = x - LOGO_X + sh;
                int32_t th = BAYER[gy & 3][x & 3];
                if (logo_bit(LOGO_MASK, lx, ly)) {
                    if (logo_bit(LOGO_INK, lx, ly) && tone > th) cv_pset(x, y, K_INK);
                    continue;
                }
                if (still || (gy > INTRO_NAME_Y - 6 && gy < INTRO_TAG_Y + 22)) continue;
                int32_t f = 0;                                   /* the fog behind: the thickest bank here */
                for (int32_t i = 0; i < 6; i++) {
                    int32_t dx = x - fc[i][0], dy = (gy - fc[i][1]) * 2, r = fc[i][2], d = dx * dx + dy * dy;
                    if (d < r * r) { int32_t v = 5 * (r * r - d) / (r * r) + 1; if (v > f) f = v; }
                }
                if (f - out / 3 > th) cv_pset(x, y, K_INK);
            }
        }
        cv_oy = -y0; cv_y0 = 0; cv_y1 = 80;                     /* (the name and the sparks in screen rows) */
        if (still || t > 500u) {                                 /* the name, typed in letter by letter */
            char nm[10] = "MONODRONE";
            uint32_t n = still ? 9u : (t - 500u) / 55u;
            if (n < 9u) nm[n] = 0;
            if (out < 12) {
                cv_text((240 - text_w(&FONT_L, "MONODRONE")) / 2, INTRO_NAME_Y, &FONT_L, nm, K_TOX);
                if (still || t > 1100u) cv_text((240 - text_w(&FONT_XS, TAG)) / 2, INTRO_TAG_Y, &FONT_XS, TAG, K_RUST);
            }
        }
        if (!still) {
            for (int32_t i = 0; i < 40; i++)
                if (iash[i].s && iash[i].y >= 0) cv_pset(iash[i].x / 16, iash[i].y / 16, K_INK);
            for (int32_t i = 0; i < 5; i++)
                if (ispark[i][2] > 0) {
                    int32_t sx = ispark[i][0], sy = ispark[i][1], r = 5 - ispark[i][2];
                    cv_pset(sx, sy, K_BONE); cv_pset(sx - r, sy, K_BONE); cv_pset(sx + r, sy, K_BONE);
                    cv_pset(sx, sy - r, K_BONE); cv_pset(sx, sy + r, K_BONE);
                }
            if (out < 12) {                                       /* loading: a line along the bottom */
                int32_t w = (int32_t)(t * 200u / (INTRO_MS - 450u));
                cv_rect(20, 226, 200, 1, K_LINE);
                cv_rect(20, 226, w > 200 ? 200 : w, 1, K_TOX);
            }
        }
        cv_oy = cv_y0 = 0;
        cv_blit(0, (uint32_t)y0);
    }
}

void app_splash(void) { intro_draw(0); }

/* the update's screens (APP_SYS_SCREENS, base/device: an install's steps, the chip's update mode), as the panel is:
 * the header, the head, what is happening in green; the five steps along the bottom */
static void sys_screen(const char *tag, const char *big, uint16_t bigc, const char *l1, const char *l2, int32_t step, int failed)
{
    static const char *const STEP[5] = {"PACKAGE", "CHECK", "LOADER", "CONFIRM", "RESTART"};
    for (int32_t b = 0; b < 3; b++) {
        int32_t y0 = b * 80;
        cv_begin(240, 80, K_BG);
        cv_oy = -y0; cv_y0 = 0; cv_y1 = 80;
        for (int32_t y = 0; y < 80; y++) {
            int32_t ly = y0 + y - 34;
            if ((uint32_t)ly >= LOGO_N) continue;
            for (int32_t x = 0; x < LOGO_N; x++)
                if (logo_bit(LOGO_INK, x, ly)) cv_pset(LOGO_X + x, y0 + y, K_INK);
        }
        {
            int32_t x = cv_text(8, 4, &FONT_B, "MONODRONE", K_TOX);
            cv_text(x + 10, 6, &FONT_XS, tag, K_BONE);
            cv_rect(0, 24, 240, 1, K_LINE);
        }
        cv_text((240 - text_w(&FONT_M, big)) / 2, 160, &FONT_M, big, bigc);
        if (step > 0) {
            for (int32_t i = 0; i < 5; i++) {
                int32_t x = 6 + i * 46;
                uint16_t c = i + 1 < step ? K_BONE : i + 1 == step ? (failed ? K_BLOOD : K_TOX) : K_LINE;
                cv_rect(x, 194, 44, 4, c);
                cv_text(x + (44 - text_w(&FONT_T, STEP[i])) / 2, 202, &FONT_T, STEP[i], i + 1 <= step ? K_BONE : K_ASH);
            }
        } else if (l1) cv_text((240 - text_w(&FONT_XS, l1)) / 2, 194, &FONT_XS, l1, K_ASH);
        if (l2) cv_text((240 - text_w(&FONT_XS, l2)) / 2, 220, &FONT_XS, l2, K_BONE);
        cv_oy = cv_y0 = 0;
        cv_blit(0, (uint32_t)y0);
    }
    lcd_sync();
}

void app_ota_show(uint32_t step, int32_t code)
{
    static int32_t last = 1;
    if (step < 9u) {
        last = step >= 1u && step <= 5u ? (int32_t)step : 1;
        sys_screen("UPDATE", step == 5u ? "RESTARTING" : "UPDATING", K_TOX, 0, "KEEP IT PLUGGED IN", last, 0);
    } else if (code == 1) sys_screen("UPDATE", "DRY RUN OK", K_TOX, 0, "NOTHING WAS CHANGED", 5, 0);
    else sys_screen("UPDATE", "UPDATE FAILED", K_BLOOD, 0, "TRY AGAIN FROM THE INSTALLER", last, 1);
}

void app_uboot_show(const char *why)
{
    const char *w = why;                 /* why, in the panel's words: "UPDATE MODE (REC)", "UBOOT (USB)" */
    for (const char *p = why; *p; p++)
        if (p[0] == '(' && p[1] == 'R') { w = "REC HELD AT START"; break; }
        else if (p[0] == '(' && p[1] == 'U') { w = "ASKED FOR OVER USB"; break; }
    sys_screen("UPDATE MODE", "UPDATE MODE", K_TOX, w, "OPEN THE WEB INSTALLER", 0, 0);
}

void app_ui_init(void)
{
    touch_on(1);
    ui.focus = -1;
    intro_on = 1;
    intro_t0 = plat_ms();
    intro_last = 0;
}

static void intro_end(void)
{
    intro_on = 0;
    lcd_fill(0, 0, 240, 240, K_BG);
    ui.redraw = 1;
    if (!any_on()) app_say("MONODRONE", "PLAY TO START");
}

void app_frame(void)
{
    static uint32_t last_scope, last_cpu, msg_was;
    uint32_t now = plat_ms();
    {
        static uint32_t frames;
        if (frames < 3u) frames++;
        if (!out_open && frames >= 3u && any_on()) out_open = 1;    /* (MASTER read by the main loop by now) */
    }
    if (intro_on) {
        uint32_t was_turned = ui.turned_at[0] ^ ui.turned_at[1] ^ ui.turned_at[2] ^ ui.turned_at[3];
        input();
        ann_tick(now);
        if (ui.btn || ui.keys || ui.rn || was_turned != (ui.turned_at[0] ^ ui.turned_at[1] ^ ui.turned_at[2] ^ ui.turned_at[3])
            || now - intro_t0 >= INTRO_MS) intro_end();
        else {
            if (now - intro_t0 - intro_last >= 33u || !intro_last) intro_draw(now - intro_t0 ? now - intro_t0 : 1u);
            return;
        }
    }
    input();
    ann_tick(now);
    if (ui.msg_until && now >= ui.msg_until) { ui.msg_until = 0; ui.redraw = 1; }
    if (C->morph && (int32_t)(now - morph_until) >= 0) C->morph = 0;
    {   /* the guard: the render overloaded in three seconds running, the load still high: the last layer sounding
         * goes out (better a layer less than a broken sound); said on the screen */
        static uint32_t seen, at, run;
        if (now - at >= 1000u) {
            uint32_t o = overloads;
            run = o != seen && plat_cpu_pct() > 88u ? run + 1u : 0u;
            seen = o; at = now;
            if (run >= 3u) {
                run = 0;
                for (int32_t i = DR_LAYERS - 1; i >= 0; i--)
                    if (C->L[i].on) {
                        char t[10] = "LAYER ";
                        C->L[i].on = 0;
                        t[6] = (char)('1' + i); t[7] = 0;
                        app_say("CPU FULL", t);
                        break;
                    }
            }
        }
    }
    if (ui.k_turned && now - ui.drawn_at > 700u) { ui.k_turned = 0; ui.redraw = 1; }
    if (now - last_cpu > 500u) { last_cpu = now; ui.redraw |= 2u; }
    if (ui.redraw) {
        draw_header();
        if (ui.redraw & 1u) draw_knobs();
        ui.redraw = 0;
        ui.drawn_at = now;
    }
    (void)msg_was;
    if (now - last_scope >= 40u) {       /* the layers' meters and the scope: 25 a second */
        last_scope = now;
        draw_layers();
        draw_land();
    }
}

#ifdef PLAT_HOST
int app_expect(const char *what, int *got)
{
    if (!strcmp(what, "sel")) *got = ui.sel;
    else if (!strcmp(what, "page")) *got = ui.page;
    else if (!strcmp(what, "fxmode")) *got = ui.fxmode;
    else if (!strncmp(what, "sysota", 6)) { int q = atoi(what + 6); app_ota_show(q >= 90 ? 9u : (uint32_t)q, q == 91 ? 1 : -1); *got = 1; }   /* (test: the update's screens) */
    else if (!strcmp(what, "sysuboot")) { app_uboot_show("UPDATE MODE (REC)"); *got = 1; }
    else if (!strcmp(what, "scene")) *got = st.cur;
    else if (!strcmp(what, "octave")) *got = octave;
    else if (!strcmp(what, "punch")) *got = C->punch;
    else if (!strncmp(what, "pk", 2)) { int q = atoi(what + 2); *got = C->pk[(q / 10) % PU_N][q % 4]; }
    else if (!strncmp(what, "macro", 5)) { int q = atoi(what + 5); *got = q < MC_N ? C->macro[q] : 0; }
    else if (!strcmp(what, "mkold")) {   /* (test: the store as L12 left it: 9..16 the old copies, 14 saved over) */
        for (uint32_t i = 8; i < NSCENES; i++) { scene_build(&st.sc[i], &FACTORY[i - 8u], LANDMAP[i - 8u]); }
        st.sc[13].fx[0] ^= 1u;
        for (uint32_t i = 0; i < NSCENES; i++) st.sc[i].land = (uint8_t)(land_of(st.sc[i].land) * 26u + 13u);   /* (the old places) */
        st.version = SC_VERSION_7;
        *got = plat_store_save(OBJ_SCENES, &st, sizeof st) == 0;
    }
    else if (!strcmp(what, "mkv10")) {   /* (test: a store as L28 left it: GLIDE 30 everywhere, scene 2 layer 1 set to 90) */
        for (uint32_t i = 0; i < NSCENES; i++) for (uint32_t k = 0; k < DR_LAYERS; k++) st.sc[i].L[k].glide = 30;
        st.sc[1].L[0].glide = 90; st.version = SC_VERSION_10;
        *got = plat_store_save(OBJ_SCENES, &st, sizeof st) == 0;
    }
    else if (!strncmp(what, "glide", 5)) *got = C->L[atoi(what + 5) & 3].glide;
    else if (!strcmp(what, "mkoldnames")) {   /* (test: slots saved with the names from before L23) */
        static const char *const N[5] = {"WISHGRNT", "PSYKOSYS", "STRAND", "DIVIDE", "ANNEX"};
        static const uint8_t SL[5] = {8, 11, 13, 14, 15};
        for (uint32_t k = 0; k < 5u; k++) { for (uint32_t c = 0; c < 10u; c++) st.sc[SL[k]].name[c] = 0; for (uint32_t c = 0; N[k][c]; c++) st.sc[SL[k]].name[c] = N[k][c]; }
        st.sc[2].name[0] = 'Q';
        *got = plat_store_save(OBJ_SCENES, &st, sizeof st) == 0;
    }
    else if (!strncmp(what, "sceng", 5)) *got = st.sc[atoi(what + 5) % NSCENES].L[0].engine;
    else if (!strncmp(what, "scname", 6)) *got = st.sc[atoi(what + 6) % NSCENES].name[0];
    else if (!strcmp(what, "rn")) *got = ui.rn;
    else if (!strncmp(what, "curname", 7)) *got = st.sc[st.cur].name[atoi(what + 7) % 10];
    else if (!strcmp(what, "land")) *got = st.sc[st.cur].land;
    else if (!strncmp(what, "scland", 6)) *got = st.sc[atoi(what + 6) % NSCENES].land;
    else if (!strncmp(what, "scfx", 4)) *got = st.sc[atoi(what + 4) % NSCENES].fx[0];
    else if (!strcmp(what, "freeze")) *got = C->freeze;
    else if (!strncmp(what, "setmc", 5)) { int q = atoi(what + 5); C->macro[(q / 1000) % MC_N] = (uint8_t)(q % 1000 > 127 ? 127 : q % 1000); *got = 1; }   /* (test: setmcMVVV) */
    else if (!strncmp(what, "layers", 6)) { int q = atoi(what + 6); for (uint32_t k = 0; k < DR_LAYERS; k++) C->L[k].on = (uint8_t)((q >> k) & 1); *got = 1; }   /* (test: layersMASK) */
    else if (!strcmp(what, "capture")) *got = C->capture;
    else if (!strncmp(what, "on", 2)) *got = C->L[atoi(what + 2) & 3].on;
    else if (!strncmp(what, "note", 4)) *got = C->L[atoi(what + 4) & 3].note[0];
    else if (!strncmp(what, "nv", 2)) *got = C->L[atoi(what + 2) & 3].nv;
    else if (!strncmp(what, "eng", 3)) *got = C->L[atoi(what + 3) & 3].engine;
    else if (!strncmp(what, "level", 5)) *got = C->L[atoi(what + 5) & 3].p[3];
    else return 0;
    return 1;
}
#endif
