/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Michael (MONODRONE) */
/* MONODRONE's sound: four drone layers, each on one of twelve engines and up to four voices (a latched chord),
 * into one effects chain (dirt, filter, tape delay, reverb), with twelve punch effects and a global page (detune,
 * sub, hiss, unrest). Single-precision float only (the AC79's FPU): no double, no 64-bit integer multiply in the
 * render (felucca-bsp docs/CPU.md).
 *
 * The UI writes dr_ctl (bytes, 0..127 unless noted) whenever it likes; the render reads it once a block and glides
 * every value toward it, so a knob turned, a scene recalled or a byte half-written never clicks. */
#pragma once
#include <stdint.h>

#define DR_LAYERS 4
#define DR_VOICES 4
enum { DR_HIVE, DR_SWARM, DR_ROT, DR_WIND, DR_HOWL, DR_GEIGER, DR_SMEAR, DR_HUM, DR_SCRAPE, DR_CHOIR, DR_STATIC,
       DR_CHAOS, DR_ENGINES };
/* the punches (held with FX): bit i of dr_ctl.punch */
enum { PU_BURN, PU_VOID, PU_SHATTER, PU_WARBLE, PU_INFINITE, PU_SHIMMER, PU_THROW, PU_RING, PU_DECIMATE, PU_GATE,
       PU_SCREAM, PU_ROTATE, PU_N };
enum { DR_PUNCH_BURN = 1, DR_PUNCH_VOID = 2, DR_PUNCH_SHATTER = 4 };
/* fx[]: four pages of four */
enum { FX_DRIVE, FX_CRUSH, FX_RATE, FX_DIRT, FX_CUT, FX_RESO, FX_MODE, FX_SWEEP,
       FX_TIME, FX_FDBK, FX_TONE, FX_ECHO, FX_SIZE, FX_DAMP, FX_MOD, FX_VERB, FX_N };
enum { MOD_SPEED, MOD_TONE, MOD_FILTER, MOD_SPACE, MOD_N };
enum { GL_DETUNE, GL_SUB, GL_HISS, GL_UNREST, GL_N };
/* MACRO: four knobs that each move many settings at once, on top of them (0: none) */
enum { MC_ROT, MC_SPACE, MC_UNREST, MC_STARVE,              /* MACRO */
       MC_TANGLE, MC_RUNAWAY, MC_RUNOFF, MC_DELUGE, MC_N };    /* MACRO 2 */

typedef struct {
    uint8_t on;          /* 0/1: it fades in or out over SWELL */
    uint8_t engine;      /* DR_* */
    uint8_t note[DR_VOICES];   /* the chord: MIDI notes, the first nv of them sound */
    uint8_t nv;          /* voices sounding, 1..4 */
    uint8_t p[4];        /* K1..K3 the engine's three, K4 its level */
    uint8_t tune;        /* 64 = none; +-12 semitones */
    uint8_t glide;       /* 0..8 s */
    uint8_t swell;       /* the fade in and out: 20 ms..40 s */
    uint8_t drift;       /* its own slow wander: pitch and K1 */
    uint8_t pan;         /* 64 = centre */
    uint8_t push;        /* the key's pressure: louder and brighter (not saved) */
} dr_layer_t;

typedef struct {
    dr_layer_t L[DR_LAYERS];
    uint8_t fx[FX_N];
    uint8_t mod[MOD_N];  /* WEATHER: speed, and how far it moves the layers' K1, the filter, the delay and reverb */
    uint8_t glob[GL_N];  /* GLOBAL: DETUNE (every voice apart), SUB (an octave under the lowest), HISS (the floor),
                          * UNREST (pitch and level unsteady, tape dropouts) */
    uint8_t macro[MC_N]; /* MACRO: ROT (dirt, crush, darker), VAST (bigger, wetter, longer echoes), NAUSEA (detune,
                          * drift, wander), STARVE (a dying supply); MACRO 2: TANGLE (the layers on
                          * modulate each other: each one's loudness moves the next one's K1, and, further up, its
                          * sound ring-modulates the next), RUNAWAY (feedback screaming high, a fifth over the
                          * highest note), RUNOFF (low, a fifth over the lowest), DELUGE (a bubbling sub under a wall) (not saved) */
    uint8_t pk[PU_N][4]; /* each punch's own knobs (held with its key: K1..K3, K4 FADE; DR_PK_DEF the sound it had before) */
    uint16_t punch;      /* the punches held: bit PU_* (not saved) */
    uint8_t freeze;      /* the reverb (and the delay) hold what they have, forever (not saved) */
    uint8_t capture;     /* SMEAR's buffer records the layers (not saved) */
    uint8_t morph;       /* nonzero while a scene morphs in: everything glides over 0.1 + (morph/127)^2 x 20 s */
} dr_ctl_t;

extern volatile dr_ctl_t dr_ctl;
extern const uint8_t DR_PK_DEF[PU_N][4];
/* a punch's fade, in and out, s, with its K4 (FADE) at b: 10 (b/127)^2, 0..10 s, the same for all */
float dr_punch_fade(uint8_t b);
/* the render to the UI: each layer's level and the output's, 0..255; the output's shape, 64 points, -127..127 */
extern volatile uint8_t dr_meter[DR_LAYERS + 1];
extern volatile int8_t dr_scope[64];
extern volatile uint32_t dr_events;      /* GEIGER's clicks and STATIC's crackles so far (the screen's sparks) */

void dr_defaults(dr_ctl_t *c);           /* the power-on scene */
void dr_init(void);                      /* before the audio starts */
void dr_panic(void);                     /* silence at once (an update) */
/* the render: n stereo frames into lr (float, about -1..1, interleaved) */
void dr_render(float *lr, uint32_t n);
