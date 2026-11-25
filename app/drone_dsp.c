/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Michael (MONODRONE) */
/* MONODRONE's sound (drone_dsp.h). Everything per sample is float; the per-block numbers (cutoffs, decay gains,
 * pitches) use fm1-base's fastmath. The big buffers are int16 in the pool. */
#include <stdint.h>
#include "plat.h"
#include "fastmath.h"
#include "drone_dsp.h"

#define SR 44100.0f
#define ISR (1.0f / 44100.0f)
#define MAXN 256u

volatile dr_ctl_t dr_ctl;
/* each punch's knobs as they start: where the punch sounds as it did before it had them (K4: FADE, in and out, 0..10 s) */
const uint8_t DR_PK_DEF[PU_N][4] = {
    {69, 45, 0, 7},     /* BURN: FUZZ, BITS */
    {61, 40, 0, 18},     /* VOID: SOAK (the time it pours in), DRY (what is left of the sound), DARK */
    {64, 57, 32, 7},    /* SHATTER: SIZE, REPEAT, REVERSE */
    {64, 64, 64, 64},    /* WARBLE: RATE (the pulse), DEPTH, WOW */
    {71, 64, 64, 28},    /* INFINITE: FALL (how fast it deepens), DARK, DRAG (the echoes pulled down) */
    {64, 64, 0, 40},     /* SHIMMER DOWN: MIX, FEED (back into the reverb) */
    {106, 85, 0, 13},    /* THROW: FDBK, ECHO */
    {64, 64, 64, 34},    /* RING: FREQ, WOBBLE, MIX */
    {54, 51, 0, 7},     /* DECIMATE: RATE (down), BITS (fewer) */
    {64, 127, 0, 6},    /* GATE: SPEED, DEPTH */
    {64, 74, 73, 22},    /* SCREAM: PITCH, FDBK, DRIVE */
    {64, 64, 0, 22},     /* ROTATE: SPEED, WOBBLE */
};
/* FADE, every punch the same: in and out over 10 (b/127)^2 s, 0..10 s (at least 5 ms) */
float dr_punch_fade(uint8_t b) { float u = (float)(b > 127u ? 127u : b) / 127.0f, s = 10.0f * u * u; return s < 0.005f ? 0.005f : s; }
static dr_ctl_t E;                       /* what the render hears: dr_ctl with the MACRO knobs applied, once a block */
volatile uint8_t dr_meter[DR_LAYERS + 1];
volatile int8_t dr_scope[64];
volatile uint32_t dr_events;

/* ------------------------------------------------------------------------------------------------- helpers --- */
static uint32_t rng_state = 0x9E3779B9u;
static inline uint32_t rnd32(uint32_t *s)
{
    uint32_t x = *s;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return *s = x;
}
static inline float rndf(uint32_t *s) { return (float)(int32_t)rnd32(s) * (1.0f / 2147483648.0f); }   /* -1..1 */
static inline float rndu(uint32_t *s) { return (float)(rnd32(s) >> 8) * (1.0f / 16777216.0f); }      /* 0..1 */
static inline float u7(uint8_t v) { return (float)(v > 127u ? 127u : v) * (1.0f / 127.0f); }

/* sin(2 pi ph) for ph in [0, 1): a parabola, refined (error ~0.1 %) */
static inline float psin(float ph)
{
    float t = ph - 0.5f, a = t < 0.0f ? -t : t, y = 8.0f * t - 16.0f * t * a;
    float b = y < 0.0f ? -y : y;
    return -(y + 0.225f * (y * b - y));
}
static inline float wrap1(float p) { return p - fm_floorf(p); }
/* a soft clip to +-1: a cubic, flat from +-1.5 (no divide) */
static inline float sat(float x)
{
    x = x > 1.5f ? 1.5f : x < -1.5f ? -1.5f : x;
    return x - 0.148148148f * x * x * x;
}
static inline float flush(float x) { return (x > -1e-15f && x < 1e-15f) ? 0.0f : x; }
#define FL(v) ((v) = flush(v))                  /* once a block on a decaying state: no denormals, cheaply */
static inline float clampf(float x, float lo, float hi) { return x < lo ? lo : x > hi ? hi : x; }

/* the TPT state-variable filter (Zavalishin): g = tan(pi fc / SR), k = 1 / Q */
typedef struct { float ic1, ic2; } svf_t;
typedef struct { float g, k, a1; } svfc_t;
static inline void svf_coef(svfc_t *c, float fc, float k)
{
    fc = clampf(fc, 10.0f, 18000.0f);
    c->g = fm_tanf(FM_PI * fc * ISR);
    c->k = k;
    c->a1 = 1.0f / (1.0f + c->g * (c->g + k));
}
/* one sample: low, band and high */
static inline void svf_run(svf_t *s, const svfc_t *c, float x, float *lp, float *bp, float *hp)
{
    float h = (x - (c->g + c->k) * s->ic1 - s->ic2) * c->a1;
    float v1 = c->g * h, b = v1 + s->ic1;
    float v2 = c->g * b, l = v2 + s->ic2;
    s->ic1 = b + v1;
    s->ic2 = l + v2;
    *lp = l; *bp = b; *hp = h;
}

/* a slow smooth random: a new target every 1/rate s, glided to */
typedef struct { float v, t, ph; } wander_t;
static inline float wander(wander_t *w, float rate, float dt, uint32_t *r)
{
    w->ph += rate * dt;
    if (w->ph >= 1.0f) { w->ph -= fm_floorf(w->ph); w->t = rndf(r); }
    w->v += (w->t - w->v) * clampf(rate * dt * 3.0f, 0.0f, 1.0f);
    return w->v;
}

/* ------------------------------------------------------------------------------------------ the big buffers --- */
#define HOWL_N 2048u                    /* a voice's feedback loop: 46 ms, down to 21.5 Hz */
#define GRAIN_N 32768u                  /* SMEAR's memory: 1.49 s at 22.05 kHz, mono (a power of two) */
#define DLY_N 32768u                    /* the tape delay: 1.49 s at 22.05 kHz, mono */
#define SHAT_N 4096u                    /* SHATTER: 93 ms stereo */
static const uint16_t VLEN[4] = {2111, 2633, 3217, 3863};   /* the reverb's four lines, at 22.05 kHz */
static const uint16_t APLEN[4] = {113, 241, 379, 677};       /* its four diffusers */
static const uint16_t APAT[4] = {0, 113, 354, 733};
#define VERB_N (2111 + 2633 + 3217 + 3863 + 4 * 16)
static int16_t howl_buf[DR_LAYERS][DR_VOICES][HOWL_N] PLAT_POOL;
static int16_t grain_buf[GRAIN_N] PLAT_POOL;
static int16_t dly_buf[DLY_N] PLAT_POOL;
static int16_t shat_buf[SHAT_N * 2u] PLAT_POOL;
static int16_t verb_buf[VERB_N] PLAT_POOL;
static int16_t ap_buf[113 + 241 + 379 + 677] PLAT_POOL;
static uint32_t grain_w;                /* SMEAR's write head */
static float grain_acc;
static uint32_t grain_have;             /* samples recorded so far (up to GRAIN_N) */

/* ---------------------------------------------------------------------------------------------- the layers --- */
#define NGRAIN 8
typedef struct { float pos, inc, age, len, amp; } grain_t;
typedef struct {                        /* a voice: one note of the layer's chord */
    float ph[6], z[10];
    svf_t f[4];
    uint32_t r, burst, cnt;
    uint32_t hw;                        /* HOWL's write index */
    float lp2;                          /* log2 of its pitch / 440 Hz, glided */
    float vf;                           /* in / out of the chord, 0..1 */
    float det;                          /* its own place in DETUNE, -1..1 */
    wander_t wd, wu;                    /* its drift; its unrest */
} voice_t;
typedef struct {
    uint8_t eng, pend;                  /* the engine playing; the one asked for (a switch fades through silence) */
    float sw;                           /* the switch's fade, 0..1 */
    float fade;                         /* on / off, 0..1 (SWELL) */
    float level;                        /* K4 and push, glided */
    float p[3];                         /* K1..K3, glided, 0..1 */
    float gl, gr;                       /* its pan */
    float meter, dc;
    wander_t wk, we;                    /* drift of K1; the engine's own slow wander (sweeps, sag, fading) */
    uint32_t r;
    voice_t v[DR_VOICES];
    grain_t g[NGRAIN];
    float spawn;
} layer_t;
static layer_t L[DR_LAYERS];

static const float ROT_RATIO[16] = {0.25f, 0.5f, 0.707f, 1.0f, 1.414f, 1.5f, 2.0f, 2.76f,
                                    3.0f, 3.5f, 4.2f, 5.4f, 6.0f, 7.13f, 8.5f, 11.3f};

static void voice_reset(voice_t *v, int16_t *hb, uint32_t seed)
{
    uint32_t k, r = v->r ? v->r : seed;
    for (k = 0; k < 6; k++) v->ph[k] = rndu(&r);
    for (k = 0; k < 10; k++) v->z[k] = 0.0f;
    for (k = 0; k < 4; k++) v->f[k].ic1 = v->f[k].ic2 = 0.0f;
    for (k = 0; k < HOWL_N; k++) hb[k] = 0;
    v->burst = v->cnt = v->hw = 0;
    v->z[1] = 0.3f + 0.4f * rndu(&r);   /* CHAOS's map starts off its fixed points */
    v->r = r;
}
static void layer_reset(layer_t *l, uint32_t i)
{
    for (uint32_t k = 0; k < DR_VOICES; k++) voice_reset(&l->v[k], howl_buf[i][k], 0x1234567u + (i * 4u + k) * 0x9E3779B9u);
    for (uint32_t k = 0; k < NGRAIN; k++) l->g[k].age = l->g[k].len = 0.0f;
    l->spawn = 0.0f;
}

/* polyBLEP: a saw's step smoothed (t: the phase, dt: the step) */
static inline float blep(float t, float dt)
{
    if (t < dt) { t /= dt; return t + t - t * t - 1.0f; }
    if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
    return 0.0f;
}
static inline float saw(float *ph, float dt)
{
    float t = *ph + dt;
    if (t >= 1.0f) t -= 1.0f;
    *ph = t;
    return 2.0f * t - 1.0f - blep(t, dt);
}

/* Each engine fills out[n] (mono) for one voice, its frequency glided f0..f1 across the block, with K1..K3 a, b, c
 * (0..1) and ew, the layer's slow wander (-1..1). */
typedef struct { float a, b, c, ew, nvf; } ek_t;   /* the knobs, the wander, 1 / voices */

/* HIVE: a Lyra-like pair, two sines detuned, each modulating the other, through a sharpening clip. a DETUNE (up to
 * 3 %), b CROSS (cross-FM), c SHARP (drive into the clip). A third sine an octave down holds the floor. */
static void eng_hive(voice_t *v, float *out, uint32_t n, float f0, float f1, const ek_t *k)
{
    float d = 1.0f + k->a * k->a * 0.03f, x = k->b * k->b * 0.9f, drive = 1.0f + k->c * k->c * 14.0f;
    float inc = f0 * ISR, dinc = (f1 - f0) * ISR / (float)n, norm = 0.55f / sat(drive * 0.8f);
    float ya = v->z[0], yb = v->z[1];
    for (uint32_t i = 0; i < n; i++, inc += dinc) {
        v->ph[0] = wrap1(v->ph[0] + inc);
        v->ph[1] = wrap1(v->ph[1] + inc * d);
        v->ph[2] = wrap1(v->ph[2] + inc * 0.5f);
        float na = psin(wrap1(v->ph[0] + x * yb));
        float nb = psin(wrap1(v->ph[1] + x * ya));
        ya = na; yb = nb;
        float y = sat(drive * (0.4f * (ya + yb) + 0.25f * psin(v->ph[2]))) * norm;
        v->z[2] += (y - v->z[2]) * 0.55f;                        /* (the edges a little rounder: ~6 kHz) */
        out[i] = v->z[2];
    }
    v->z[0] = ya; v->z[1] = yb;
}

/* SWARM: five saws spread apart into a resonant low-pass. a SPREAD (up to 70 cents), b BRIGHT (the cutoff, 1..90x
 * the pitch), c GRIT (resonance and drive) */
static const float SPREAD[5] = {-1.0f, -0.45f, 0.0f, 0.5f, 1.0f};
static void eng_swarm(voice_t *v, float *out, uint32_t n, float f0, float f1, const ek_t *k)
{
    float sp = k->a * k->a * 0.04f, ratio[5], inc = f0 * ISR, dinc = (f1 - f0) * ISR / (float)n;
    svfc_t fc;
    svf_coef(&fc, f1 * fm_exp2f(k->b * 6.5f) + 30.0f, 2.0f - 1.85f * k->c);
    float drive = 1.0f + k->c * k->c * 10.0f, dmk = 0.6f / sat(0.3f * drive);
    for (uint32_t j = 0; j < 5; j++) ratio[j] = 1.0f + SPREAD[j] * sp;
    for (uint32_t i = 0; i < n; i++, inc += dinc) {
        float s = 0.0f, lo, bp, hp;
        for (uint32_t j = 0; j < 5; j++) s += saw(&v->ph[j], inc * ratio[j]);
        svf_run(&v->f[0], &fc, s * 0.22f, &lo, &bp, &hp);
        out[i] = sat(lo * drive) * dmk * 0.65f;
    }
}

/* ROT: two-operator FM, the modulator feeding back on itself. a RATIO (16 steps, most of them inharmonic),
 * b INDEX, c FEEDBACK (toward noise) */
static void eng_rot(voice_t *v, float *out, uint32_t n, float f0, float f1, const ek_t *k)
{
    float target = ROT_RATIO[(uint32_t)(k->a * 15.99f)];
    if (v->z[2] <= 0.0f) v->z[2] = target;
    v->z[2] += (target - v->z[2]) * 0.05f;                       /* the ratio glides (a few blocks) */
    float ratio = v->z[2], idx = k->b * k->b * 2.2f * (1.0f + 0.25f * k->ew), fb = k->c * k->c * 1.3f;
    float inc = f0 * ISR, dinc = (f1 - f0) * ISR / (float)n, ym = v->z[0], yp = v->z[1];
    for (uint32_t i = 0; i < n; i++, inc += dinc) {
        v->ph[0] = wrap1(v->ph[0] + inc);
        v->ph[1] = wrap1(v->ph[1] + inc * ratio);
        float m = psin(wrap1(v->ph[1] + fb * 0.5f * (ym + yp)));
        yp = ym; ym = m;
        out[i] = psin(wrap1(v->ph[0] + idx * m)) * 0.6f;
    }
    v->z[0] = ym; v->z[1] = yp;
}

/* WIND: noise through two resonant band-passes, one at the pitch, one a fifth up that the weather sweeps.
 * a FOCUS (hiss .. whistle), b SWEEP (how far the second wanders), c RUMBLE (brown noise underneath) */
static void eng_wind(voice_t *v, float *out, uint32_t n, float f0, float f1, const ek_t *k)
{
    (void)f0;
    float q = 0.6f * fm_exp2f(k->a * 6.0f), kq = 1.0f / q;
    svfc_t c1, c2;
    svf_coef(&c1, f1, kq);
    svf_coef(&c2, f1 * 1.5f * fm_exp2f(k->ew * k->b * 2.5f), kq);
    float g = 2.6f * fm_sqrtf(q) * kq, rum = k->c * k->c * 1.6f;
    for (uint32_t i = 0; i < n; i++) {
        float x = rndf(&v->r), lo, bp, hp, bp2;
        svf_run(&v->f[0], &c1, x, &lo, &bp, &hp);
        svf_run(&v->f[1], &c2, x, &lo, &bp2, &hp);
        v->z[0] += (x - v->z[0]) * 0.012f;                       /* brown-ish (noise: never denormal) */
        v->z[1] += (v->z[0] - v->z[1]) * 0.02f;
        out[i] = (bp + 0.7f * bp2) * g + v->z[1] * rum * 3.0f;
    }
}

/* HOWL: a feedback loop tuned to the pitch (a delay line, damped, saturated), excited by noise. a FEEDBACK (past 1:
 * it screams on its own), b DAMP (dark .. bright), c EXCITE (noise in, and the loop's flutter) */
static void eng_howl(voice_t *v, int16_t *buf, float *out, uint32_t n, float f0, float f1, const ek_t *k)
{
    (void)f0;
    float d = clampf(SR / f1, 4.0f, (float)(HOWL_N - 4u));
    d *= 1.0f + 0.004f * k->c * k->ew;                           /* the loop flutters */
    float fb = 0.75f + k->a * 0.42f, damp = 0.08f + k->b * k->b * 0.9f, exc = 0.004f + k->c * k->c * 0.35f;
    float z = v->z[0], dc = v->z[1];
    uint32_t w = v->hw;
    for (uint32_t i = 0; i < n; i++) {
        float rp = (float)w - d + (float)HOWL_N;
        uint32_t i0 = (uint32_t)rp;
        float fr = rp - (float)i0;
        float y0 = (float)buf[i0 & (HOWL_N - 1u)], y1 = (float)buf[(i0 + 1u) & (HOWL_N - 1u)];
        float y = (y0 + (y1 - y0) * fr) * (1.0f / 32767.0f);
        z += (y - z) * damp;
        dc += (z - dc) * 0.002f;                                 /* the loop's DC out */
        float s = sat(fb * (z - dc) * 1.2f + rndf(&v->r) * exc);
        buf[w & (HOWL_N - 1u)] = (int16_t)(s * 32767.0f);
        w++;
        out[i] = (z - dc) * 0.7f;
    }
    v->z[0] = z; v->z[1] = dc; v->hw = w & (HOWL_N - 1u);
}

/* GEIGER: random clicks, from a few a minute to a storm, raw or ringing at the pitch, with fire's crackle (clusters
 * of them) and pops. a RATE, b RING (raw .. a ping at the pitch), c FIRE (bursts and pops). A chord shares the rate */
static void eng_geiger(voice_t *v, float *out, uint32_t n, float f0, float f1, const ek_t *k)
{
    (void)f0;
    float rate = 0.3f * fm_exp2f(k->a * 12.5f) * k->nvf, p = rate * ISR;
    svfc_t fc;
    svf_coef(&fc, f1 * 2.0f, 1.0f / (2.0f + k->b * k->b * 60.0f));
    float ring = k->b * k->b, raw = 1.0f - ring, c = k->c;
    for (uint32_t i = 0; i < n; i++) {
        float imp = 0.0f, lo, bp, hp;
        float pb = v->burst ? 0.04f : 0.0f;
        if (rndu(&v->r) < p + pb) {
            float amp = 0.25f + 0.75f * rndu(&v->r);
            if (rndu(&v->r) < 0.05f) amp *= 2.0f;                /* now and then a loud one */
            imp = rnd32(&v->r) & 1u ? amp : -amp;
            if (v->burst) v->burst--;
            else if (rndu(&v->r) < c) v->burst = 2u + (rnd32(&v->r) & 31u) * (uint32_t)(1.0f + c * 3.0f);
            dr_events++;
        }
        svf_run(&v->f[0], &fc, imp, &lo, &bp, &hp);
        float click = imp - v->z[0];                             /* a sharp edge: the impulse less the last */
        v->z[0] = imp;
        v->z[1] += (imp * 6.0f - v->z[1]) * 0.03f;               /* fire's pops: the impulses low-passed */
        v->z[2] += (v->z[1] - v->z[2]) * 0.05f;
        out[i] = click * raw * 0.7f + bp * ring * 4.0f + v->z[2] * c * 0.9f;
    }
}

/* HUM: a transformer's hum: the mains tone, rectified into buzz, a loose lamination rattling, sagging under load.
 * a BUZZ (sine .. rectified, driven), b RATTLE (the buzzing bursts), c SAG (the load: droop, 100 Hz ripple) */
static void eng_hum(voice_t *v, float *out, uint32_t n, float f0, float f1, const ek_t *k)
{
    float sag = 1.0f + k->c * 0.012f * k->ew;
    float inc = f0 * ISR * sag, dinc = (f1 - f0) * ISR * sag / (float)n;
    float a = k->a, drive = 1.0f + a * a * 16.0f, dn = 1.0f / sat(drive * 0.7f), rp = k->b * k->b * 18.0f * ISR;
    for (uint32_t i = 0; i < n; i++, inc += dinc) {
        v->ph[0] = wrap1(v->ph[0] + inc);
        v->ph[1] = wrap1(v->ph[1] + 100.0f * ISR);               /* the ripple: 100 Hz, whatever the note */
        float s = psin(v->ph[0]), r = (fm_fabsf(s) - 0.6366f) * 2.2f;
        float y = s * (1.0f - a) + r * a + psin(wrap1(v->ph[0] * 3.0f)) * a * 0.25f;
        y = sat(y * drive * 0.7f) * dn;
        y *= 1.0f - k->c * 0.4f * (0.5f + 0.5f * psin(v->ph[1]));
        if (rndu(&v->r) < rp) v->z[0] = 0.4f + 0.6f * rndu(&v->r);   /* a rattle starts */
        v->z[0] *= 0.9993f;
        float nz = rndf(&v->r);
        v->z[1] += (nz - v->z[1]) * 0.5f;
        float rat = (nz - v->z[1]) * v->z[0] * (s > 0.0f ? 1.0f : 0.3f);   /* bright noise, gated by the wave */
        out[i] = (y * 0.5f + rat * 1.4f * k->b) * 0.8f;
    }
}

/* SCRAPE: metal scraped and bowed: four inharmonic resonances (a plate's) driven by friction noise and stick-slip
 * at the pitch. a PRESSURE (bow and noise in), b METAL (how far apart, how long they ring), c GRIND (bursts) */
static const float PLATE[4] = {1.0f, 2.32f, 4.25f, 6.63f}, PLATE_G[4] = {1.0f, 0.75f, 0.55f, 0.4f};
static void eng_scrape(voice_t *v, float *out, uint32_t n, float f0, float f1, const ek_t *k)
{
    (void)f0;
    svfc_t c[4];
    float q = 15.0f + k->b * k->b * 160.0f, kq = 1.0f / q, spread = 0.55f + k->b * 0.9f;
    for (uint32_t j = 0; j < 4; j++) svf_coef(&c[j], f1 * (1.0f + (PLATE[j] - 1.0f) * spread), kq);
    float inc = f1 * ISR, gp = (1.5f + k->c * k->c * 70.0f) * ISR, gain = 2.4f / fm_sqrtf(q);
    for (uint32_t i = 0; i < n; i++) {
        float imp = 0.0f;
        v->ph[0] += inc * (1.0f + 0.03f * k->c * rndf(&v->r));    /* stick-slip: a catch each period, unsteady */
        if (v->ph[0] >= 1.0f) { v->ph[0] -= 1.0f; imp = 0.4f + 0.6f * k->a; }
        if (rndu(&v->r) < gp) v->z[0] = 0.3f + 0.7f * rndu(&v->r);   /* a grinding burst */
        v->z[0] *= 0.9985f;
        float e = rndf(&v->r) * (k->a * 0.5f + v->z[0] * k->c * 1.2f) + imp;
        float y = 0.0f, lo, bp, hp;
        for (uint32_t j = 0; j < 4; j++) { svf_run(&v->f[j], &c[j], e, &lo, &bp, &hp); y += bp * PLATE_G[j]; }
        out[i] = sat(y * gain * 3.0f) * 0.6f;
    }
}

/* CHOIR: a dead choir: three voices a little apart, buzzing through three dark vowel formants, with breath.
 * a VOWEL (oo .. oh .. ah .. eh), b BREATH, c CROWD (how far apart the three, how unsteady) */
static const float VOWEL[4][3] = {{325.0f, 700.0f, 2530.0f}, {450.0f, 800.0f, 2830.0f}, {700.0f, 1150.0f, 2800.0f},
                                  {550.0f, 1650.0f, 2450.0f}};
static void eng_choir(voice_t *v, float *out, uint32_t n, float f0, float f1, const ek_t *k)
{
    float pos = k->a * 2.999f, fr = pos - (float)(uint32_t)pos, fq[3];
    uint32_t vi = (uint32_t)pos;
    svfc_t c[3];
    static const float FQ[3] = {9.0f, 11.0f, 13.0f}, FG[3] = {1.0f, 0.55f, 0.3f};
    for (uint32_t j = 0; j < 3; j++) {
        fq[j] = VOWEL[vi][j] + (VOWEL[vi + 1][j] - VOWEL[vi][j]) * fr;
        svf_coef(&c[j], fq[j] * (1.0f + 0.03f * k->ew), 1.0f / FQ[j]);
    }
    float d = k->c * 0.014f + 0.002f, inc = f0 * ISR, dinc = (f1 - f0) * ISR / (float)n, br = k->b;
    for (uint32_t i = 0; i < n; i++, inc += dinc) {
        v->ph[5] = wrap1(v->ph[5] + 5.1f * ISR);                 /* vibrato, a little unsteady */
        float vib = 1.0f + 0.004f * psin(v->ph[5]) * (0.5f + k->c);
        float s = saw(&v->ph[0], inc * vib) + saw(&v->ph[1], inc * vib * (1.0f + d)) + saw(&v->ph[2], inc * (1.0f - d));
        float x = s * 0.33f * (1.0f - br * 0.6f) + rndf(&v->r) * br * 0.9f;
        float y = 0.0f, lo, bp, hp;
        for (uint32_t j = 0; j < 3; j++) { svf_run(&v->f[j], &c[j], x, &lo, &bp, &hp); y += bp * c[j].k * FG[j]; }
        out[i] = sat(y * 9.0f) * 0.55f;
    }
}

/* STATIC: a shortwave set between stations: static and crackle, a heterodyne whistle drifting, and a signal (a
 * keyed tone) coming and going. a TUNE (the whistle, and how far it drifts), b SIGNAL, c FADE (how deep the fading) */
static void eng_static(voice_t *v, float *out, uint32_t n, float f0, float f1, const ek_t *k)
{
    (void)f0;
    svfc_t cs;
    svf_coef(&cs, 1600.0f + 900.0f * k->ew, 0.9f);
    float wf = f1 * 2.0f * fm_exp2f(k->a * k->ew * 1.5f), winc = wf * ISR, sinc = f1 * 4.0f * ISR;
    float fade = 1.0f - k->c * 0.95f * (0.5f + 0.5f * k->ew);    /* the station's level, fading */
    for (uint32_t i = 0; i < n; i++) {
        float nz = rndf(&v->r), lo, bp, hp, crk = 0.0f;
        svf_run(&v->f[0], &cs, nz, &lo, &bp, &hp);
        if (rndu(&v->r) < 25.0f * ISR) { crk = rndf(&v->r) * 0.9f; dr_events++; }
        v->ph[0] = wrap1(v->ph[0] + winc);
        v->ph[1] = wrap1(v->ph[1] + sinc);
        if (!v->cnt) {                                           /* the key: dits and dahs, gaps between letters */
            uint32_t r = rnd32(&v->r);
            v->z[5] = v->z[5] > 0.5f ? 0.0f : 1.0f;
            v->cnt = (uint32_t)(SR * 0.07f) * (v->z[5] > 0.5f ? ((r & 3u) ? 1u : 3u) : ((r & 7u) ? 1u : 3u + (r & 4u)));
        }
        v->cnt--;
        v->z[6] += (v->z[5] - v->z[6]) * 0.01f;
        float sig = psin(v->ph[1]) * v->z[6] * k->b;
        float st = bp * 0.9f * (1.0f - k->b * 0.4f) + crk;
        float wh = psin(v->ph[0]) * k->a * 0.25f;
        out[i] = sat(((st + wh) * (0.4f + 0.6f * (1.0f - fade)) + sig * fade * 0.8f) * 2.0f) * 0.6f;
    }
}

/* CHAOS: the logistic map clocked at the pitch (order breaking into chaos), bending a sine; the clock now and then
 * fractures to another rate. a CHAOS (r 3.57 .. 4), b BREAK (how often it fractures), c GRIT (stepped and crushed) */
static void eng_chaos(voice_t *v, float *out, uint32_t n, float f0, float f1, const ek_t *k)
{
    static const float JUMP[5] = {0.5f, 0.75f, 1.5f, 2.0f, 0.25f};
    float r = 3.57f + 0.43f * k->a, inc = f0 * ISR, dinc = (f1 - f0) * ISR / (float)n;
    float pj = k->b * k->b * 3.0f * ISR, c = k->c, q = 48.0f - c * 40.0f;
    if (v->z[2] <= 0.0f) v->z[2] = 1.0f;
    for (uint32_t i = 0; i < n; i++, inc += dinc) {
        if (v->cnt) { if (--v->cnt == 0) v->z[2] = 1.0f; }
        else if (rndu(&v->r) < pj) { v->z[2] = JUMP[rnd32(&v->r) % 5u]; v->cnt = 2000u + (rnd32(&v->r) % 14000u); }
        v->ph[0] += inc * v->z[2];
        if (v->ph[0] >= 1.0f) {                                  /* the map steps */
            v->ph[0] -= 1.0f;
            float x = v->z[1];
            v->z[0] = x;
            x = r * x * (1.0f - x);
            if (x < 0.0001f || x > 0.9999f) x = 0.3f + 0.4f * rndu(&v->r);
            v->z[1] = x;
        }
        float m = v->z[0] + (v->z[1] - v->z[0]) * v->ph[0];      /* between its steps, or held (GRIT) */
        m += (v->z[1] - m) * c;
        v->ph[1] = wrap1(v->ph[1] + inc * v->z[2]);
        float s = psin(wrap1(v->ph[1] + k->a * 0.6f * (m - 0.5f)));
        float y = (m * 2.0f - 1.0f) * (0.25f + 0.45f * k->a) + s * (1.0f - k->a * 0.5f);
        if (c > 0.05f) y = fm_floorf(y * q + 0.5f) / q;
        out[i] = sat(y) * 0.5f;
    }
}

/* SMEAR: grains of the last 1.5 s of the layers (REC records them; held, the memory stays). Each grain plays a
 * piece at one of the chord's pitches (C4 = as recorded). a SIZE (30 ms..1 s), b SCATTER (where, how far off pitch),
 * c DENSITY. One engine for the whole layer, not one a voice */
static inline float grain_read(float pos)
{
    uint32_t i0 = (uint32_t)pos;
    float fr = pos - (float)i0;
    float y0 = (float)grain_buf[i0 & (GRAIN_N - 1u)], y1 = (float)grain_buf[(i0 + 1u) & (GRAIN_N - 1u)];
    return (y0 + (y1 - y0) * fr) * (1.0f / 32767.0f);
}
static void eng_smear(layer_t *l, float *out, uint32_t n, const float *fv, uint32_t nfv, const ek_t *k)
{
    float len_s = 0.03f * fm_exp2f(k->a * 5.0f), dens = 1.0f * fm_exp2f(k->c * 5.3f);
    float overlap = dens * len_s, norm = 3.5f / fm_sqrtf(overlap < 1.0f ? 1.0f : overlap);
    float have = (float)grain_have;
    uint32_t j;
    for (uint32_t i = 0; i < n; i++) out[i] = 0.0f;
    if (grain_have < 2205u || !nfv) return;                      /* nothing recorded yet */
    l->spawn += dens * (float)n * ISR;
    while (l->spawn >= 1.0f) {
        l->spawn -= 1.0f;
        for (j = 0; j < NGRAIN && l->g[j].age < l->g[j].len; j++) {}
        if (j == NGRAIN) break;
        grain_t *g = &l->g[j];
        float jit = k->b * k->b;
        float st = rndf(&l->r) * jit * 0.4f;                     /* up to +-0.4 semitone... */
        if (jit > 0.4f && rndu(&l->r) < jit * 0.3f) st += rnd32(&l->r) & 1u ? 12.0f : -12.0f;   /* ...or an octave */
        g->inc = fv[rnd32(&l->r) % nfv] * (1.0f / 261.6256f) * 0.5f * fm_exp2f(st * (1.0f / 12.0f));
        g->len = len_s * SR;
        float span = g->len * g->inc;                            /* buffer samples it will cross */
        float back = span + 600.0f + rndu(&l->r) * jit * (have - span - 1200.0f > 0.0f ? have - span - 1200.0f : 0.0f);
        g->pos = (float)grain_w - back + (float)GRAIN_N * 4.0f;
        g->age = 0.0f;
        g->amp = 0.7f + 0.3f * rndu(&l->r);
    }
    for (j = 0; j < NGRAIN; j++) {
        grain_t *g = &l->g[j];
        if (g->age >= g->len) continue;
        float il = 1.0f / g->len;
        for (uint32_t i = 0; i < n && g->age < g->len; i++) {
            float u = g->age * il, w = 4.0f * u * (1.0f - u);
            out[i] += grain_read(g->pos) * w * w * g->amp * norm;
            g->pos += g->inc;
            if (g->pos >= (float)GRAIN_N * 8.0f) g->pos -= (float)GRAIN_N * 4.0f;   /* (kept positive, below 2^19) */
            g->age += 1.0f;
        }
    }
}
/* ----------------------------------------------------------------------------------------------- the effects --- */
typedef struct {
    float drive, crush, rate, dirt, cut, reso, mode, sweep, time, fdbk, tone, echo, size, damp, mod, verb;
} fxs_t;
static fxs_t F;                                                  /* glided toward dr_ctl.fx */
static svf_t fl[2];
static float hold[2], hold_ph;
static float dtime_s;                                            /* the delay's time in samples, glided like tape */
static uint32_t dly_w;
static float dly_lp, dly_hp, dly_out[2], dly_in_acc, dly_prev[2];
static uint32_t verb_at[4], verb_i[4], ap_i[4];
static float verb_lp[4], verb_out[2], verb_prev[2], vmod_ph;
static uint32_t half;                                            /* frame parity: the 22.05 kHz parts run on odd ones */
static float punch_g[PU_N];
/* the punches' own state */
#define WB_N 2048u                                               /* WARBLE: 46 ms of the output, stereo */
#define SD_N 4096u                                               /* SHIMMER DOWN: the pitch shifter's line, 22.05 kHz */
static int16_t wb_buf[WB_N * 2u] PLAT_POOL;
static int16_t sd_buf[SD_N] PLAT_POOL;
static uint32_t wb_w, sd_w, gate_n, gate_on = 1u;
static float wb_ph[2], sd_d, sd_out, ring_ph, ring_f = 80.0f, dec_ph, dec_h[2], gate_g = 1.0f, scr_ph, rot_ph;
static wander_t pw[4];                                           /* WARBLE's drift, RING's carrier, ROTATE's speed */
static float dc_x[2], dc_y[2], lim_g = 1.0f;
/* RUNAWAY's loop: the output back into the dirt through a short wandering line and a band (MONOEATER.HARSH's LOOP) */
#define LOOP_N 4096u                     /* 93 ms stereo */
static int16_t loop_buf[LOOP_N * 2u] PLAT_POOL;
static uint32_t loop_w;
static float loop_d[2] = {1800.0f, 1800.0f}, loop_hz[2] = {1320.0f, 110.0f}, loop_was[2];   /* [0] RUNAWAY, [1] RUNOFF */
static svf_t loop_f[2][2];
/* STARVE: a dying supply: each layer sputters out (more often, the further it's turned), every pitch sags and
 * wavers, the bits and the rate fall (DIRT's CRUSH), and past a quarter the whole output drops out now and then */
static float j_starve, j_sag, sv_g[DR_LAYERS] = {1, 1, 1, 1}, sv_t[DR_LAYERS], sv_s[DR_LAYERS] = {1, 1, 1, 1};
static wander_t sag_w;
static float st_g = 1.0f, st_t, st_target = 1.0f;
/* DELUGE (MONOEATER.HARSH's, after a Uboa excerpt): its own sub, a rough tone at the scene's lowest pitch folded into
 * ~40..75 Hz, bubbling and skipping (bursts, rolls, skips; each hit its own pitch, popping down from above); over it
 * everything above ~150 Hz pinned into a steady wall (low mids scooped, the two sides apart). Held to the level in */
static float dl_lo1[2], dl_lo2[2], dl_lm[2], dl_top[2], dl_env[2], dl_ein, dl_eout, dl_gn = 1.0f;
static float dl_haas[512], dl_ph, dl_henv, dl_hpk, dl_br, dl_f = 48.0f;
static int32_t dl_hitn, dl_att;
static float dl_floor = 0.15f, dl_pm = 1.0f, dl_bend = 1.0f, dl_hard, dl_click, dl_cl_n;
static int32_t dl_left, dl_mode;
static uint32_t dl_hw;
static float mtrim = 1.0f, mt_want = 1.0f;                /* the MACROs' level held: ROT, SPACE and WEIGHT turned up are louder */
static wander_t wx[6];                                           /* the weather: four layers' K1, filter, space */
static uint32_t shat_w, shat_len, shat_pos, shat_left, shat_reps, shat_live;
static float shat_mix, shat_rev;

/* dr_ctl into E, the MACRO knobs on top: each pushes its bytes up toward 127 (up) or down toward 0 (dn) by its share */
static inline uint8_t mup(uint8_t v, float m) { return (uint8_t)((float)v + (127.0f - (float)v) * m + 0.5f); }
static inline uint8_t mdn(uint8_t v, float m) { return (uint8_t)((float)v * (1.0f - m) + 0.5f); }
static void macro_apply(void)
{
    uint32_t i;
    const volatile uint8_t *s = (const volatile uint8_t *)&dr_ctl;
    uint8_t *d = (uint8_t *)&E;
    for (i = 0; i < sizeof E; i++) d[i] = s[i];
    float rot = u7(E.macro[MC_ROT]), spc = u7(E.macro[MC_SPACE]), unr = u7(E.macro[MC_UNREST]);
    /* the last 40 % of each knob goes further (tp: 0 below 60 %, curving up to 1 at full) */
#define TP(m) ((m) > 0.6f ? ((m) - 0.6f) * ((m) - 0.6f) * 6.25f : 0.0f)
    if (rot > 0.0f) {                    /* ROT: driven, crushed, downsampled, all of it in; at full, ruined */
        float t = TP(rot);
        E.fx[FX_DRIVE] = mup(E.fx[FX_DRIVE], rot * 0.75f + t * 0.25f);
        E.fx[FX_CRUSH] = mup(E.fx[FX_CRUSH], rot * 0.8f + t * 0.2f);
        E.fx[FX_RATE] = mup(E.fx[FX_RATE], rot * rot * 0.45f + t * 0.35f);
        E.fx[FX_DIRT] = mup(E.fx[FX_DIRT], rot);
        E.fx[FX_RESO] = mup(E.fx[FX_RESO], t * 0.35f);
    }
    if (spc > 0.0f) {                    /* SPACE: a bigger room, wetter, longer and wetter echoes; at full, endless */
        float t = TP(spc);
        E.fx[FX_SIZE] = mup(E.fx[FX_SIZE], spc * 0.9f + t * 0.1f);
        E.fx[FX_VERB] = mup(E.fx[FX_VERB], spc * 0.8f + t * 0.2f);
        E.fx[FX_DAMP] = mup(E.fx[FX_DAMP], spc * 0.3f);
        E.fx[FX_MOD] = mup(E.fx[FX_MOD], spc * 0.4f + t * 0.35f);
        E.fx[FX_ECHO] = mup(E.fx[FX_ECHO], spc * 0.6f + t * 0.3f);
        E.fx[FX_FDBK] = mup(E.fx[FX_FDBK], spc * 0.55f + t * 0.37f);
    }
    if (unr > 0.0f) {                    /* UNREST: apart, wandering, unsteady, the weather quicker and deeper; at full, seasick */
        float t = TP(unr);
        E.glob[GL_DETUNE] = mup(E.glob[GL_DETUNE], unr * 0.8f + t * 0.2f);
        E.glob[GL_UNREST] = mup(E.glob[GL_UNREST], unr * 0.75f + t * 0.25f);
        E.mod[MOD_SPEED] = mup(E.mod[MOD_SPEED], unr * 0.4f + t * 0.35f);
        E.mod[MOD_TONE] = mup(E.mod[MOD_TONE], unr * 0.6f + t * 0.4f);
        E.mod[MOD_FILTER] = mup(E.mod[MOD_FILTER], unr * 0.5f + t * 0.45f);
        E.mod[MOD_SPACE] = mup(E.mod[MOD_SPACE], t * 0.6f);
        for (i = 0; i < DR_LAYERS; i++) E.L[i].drift = mup(E.L[i].drift, unr * 0.8f + t * 0.2f);
    }
    {   /* MACRO 2: RUNAWAY, STARVE, DELUGE (MONOEATER.HARSH's) */
        float ra = u7(E.macro[MC_RUNAWAY]), sv = u7(E.macro[MC_STARVE]), rf = u7(E.macro[MC_RUNOFF]);
        if (rf > ra) ra = rf;                /* RUNOFF pushes the feedback the same way; only its loop's pitch differs */
        E.macro[MC_DELUGE] = (uint8_t)(((uint32_t)E.macro[MC_DELUGE] * 89u + 63u) / 127u);   /* (HARSH's full: its 70 %) */
        float dg = u7(E.macro[MC_DELUGE]);
        if (ra > 0.0f) {                 /* RUNAWAY / RUNOFF: every feedback pushed toward screaming by itself (the loops in, below) */
            for (i = 0; i < DR_LAYERS; i++)
                if (E.L[i].engine == DR_HOWL) { E.L[i].p[0] = mup(E.L[i].p[0], ra * 0.8f); E.L[i].p[2] = mup(E.L[i].p[2], ra * 0.4f); }
                else if (E.L[i].engine == DR_SCRAPE) E.L[i].p[1] = mup(E.L[i].p[1], ra * 0.6f);
                else if (E.L[i].engine == DR_ROT) E.L[i].p[2] = mup(E.L[i].p[2], ra * 0.5f);
            E.fx[FX_RESO] = mup(E.fx[FX_RESO], ra * 0.6f);
            E.fx[FX_FDBK] = mup(E.fx[FX_FDBK], ra * 0.5f);
        }
        if (sv > 0.0f) E.fx[FX_CRUSH] = mup(E.fx[FX_CRUSH], sv * 0.75f);   /* STARVE: the bits and the rate falling */
        if (dg > 0.0f) {                 /* DELUGE: nothing wanders and nothing stops: the sweep and UNREST calmed */
            E.fx[FX_SWEEP] = mdn(E.fx[FX_SWEEP], dg * 0.9f);
            E.glob[GL_UNREST] = mdn(E.glob[GL_UNREST], dg * 0.7f);
        }
    }
#undef TP
}

static inline uint32_t shat_size(uint32_t len, int sh)
{
    len = sh >= 0 ? len << sh : len >> -sh;
    return len < 32u ? 32u : len > 2048u ? 2048u : len;
}

/* the fx bytes into numbers, glided (once a block) */
static void fx_glide(float k)
{
    const uint8_t *x = E.fx;
    float *f = &F.drive;
    for (uint32_t i = 0; i < FX_N; i++) f[i] += (u7(x[i]) - f[i]) * k;
}

/* the reverb's line read with a fractional position (int16) */
static inline float vread(uint32_t line, float back)
{
    uint32_t n = VLEN[line];
    float rp = (float)verb_i[line] - back;
    while (rp < 0.0f) rp += (float)n;
    uint32_t i0 = (uint32_t)rp, i1;
    if (i0 >= 2u * n) i0 = 0;
    float fr = rp - (float)i0;
    if (i0 >= n) i0 -= n;
    i1 = i0 + 1u >= n ? 0u : i0 + 1u;
    float y0 = (float)verb_buf[verb_at[line] + i0], y1 = (float)verb_buf[verb_at[line] + i1];
    return (y0 + (y1 - y0) * fr) * (1.0f / 32767.0f);
}

/* TANGLE's: each layer's own sound (two blocks: this and the last), its level and its usual level */
static float xb[2][DR_LAYERS][MAXN] PLAT_POOL;
static uint32_t xb_i;
static float x_rms[DR_LAYERS], x_avg[DR_LAYERS];
static float j_tangle;
static float sub_lp2 = -3.0f, sub_ph, sub_g, hiss_lp, hiss_ph[3], hiss_crk;
static float burn_ein, burn_eout, burn_g = 1.0f, void_lp[2], scr_fb[2], scr_base = 110.0f;
static float void_t, void_ref, void_wg = 1.0f, bus_env, wet_env, inf_depth, wb_lp[2], wb_pp;
static float drop_g = 1.0f, flut_ph;
static uint32_t drop_n;

void dr_defaults(dr_ctl_t *c)
{
    static const uint8_t eng[4] = {DR_SWARM, DR_HIVE, DR_WIND, DR_GEIGER}, note[4] = {33, 40, 45, 69};
    static const uint8_t p[4][4] = {{50, 40, 50, 100}, {40, 50, 50, 90}, {70, 60, 50, 80}, {30, 60, 40, 70}};
    static const uint8_t pan[4] = {54, 74, 40, 88};
    static const uint8_t fx[FX_N] = {40, 0, 0, 55, 92, 30, 0, 25, 78, 60, 40, 35, 100, 70, 40, 62};
    static const uint8_t mod[MOD_N] = {40, 40, 30, 30}, glob[GL_N] = {20, 0, 25, 25};
    uint32_t i, k;
    for (i = 0; i < DR_LAYERS; i++) {
        dr_layer_t *l = &c->L[i];
        l->on = 0; l->engine = eng[i];
        for (k = 0; k < DR_VOICES; k++) l->note[k] = note[i];
        l->nv = 1;
        for (k = 0; k < 4; k++) l->p[k] = p[i][k];
        l->tune = 64; l->glide = 0; l->swell = 70; l->drift = 30; l->pan = pan[i]; l->push = 0;
    }
    for (k = 0; k < FX_N; k++) c->fx[k] = fx[k];
    for (k = 0; k < MOD_N; k++) c->mod[k] = mod[k];
    for (k = 0; k < GL_N; k++) c->glob[k] = glob[k];
    for (i = 0; i < PU_N; i++) for (k = 0; k < 4; k++) c->pk[i][k] = DR_PK_DEF[i][k];
    for (k = 0; k < MC_N; k++) c->macro[k] = 0;
    c->punch = 0; c->freeze = 0; c->capture = 1; c->morph = 0;
}

void dr_init(void)
{
    uint32_t i, at = 0;
    for (i = 0; i < 2u * DR_LAYERS * MAXN; i++) (&xb[0][0][0])[i] = 0.0f;
    for (i = 0; i < DR_LAYERS; i++) x_rms[i] = x_avg[i] = 0.0f;
    for (i = 0; i < LOOP_N * 2u; i++) loop_buf[i] = 0;
    for (i = 0; i < DR_LAYERS; i++) {
        L[i].r = 0x51ED27u * (i + 1u);
        layer_reset(&L[i], i);
        L[i].eng = L[i].pend = dr_ctl.L[i].engine;
        L[i].sw = 1.0f;
        for (uint32_t k = 0; k < 3; k++) L[i].p[k] = u7(dr_ctl.L[i].p[k]);
        for (uint32_t v = 0; v < DR_VOICES; v++) {
            L[i].v[v].lp2 = ((float)dr_ctl.L[i].note[v] - 69.0f) * (1.0f / 12.0f);
            L[i].v[v].det = rndf(&L[i].r);
        }
    }
    for (i = 0; i < GRAIN_N; i++) grain_buf[i] = 0;
    for (i = 0; i < DLY_N; i++) dly_buf[i] = 0;
    for (i = 0; i < SHAT_N * 2u; i++) shat_buf[i] = 0;
    for (i = 0; i < VERB_N; i++) verb_buf[i] = 0;
    for (i = 0; i < 113u + 241u + 379u + 677u; i++) ap_buf[i] = 0;
    for (i = 0; i < WB_N * 2u; i++) wb_buf[i] = 0;
    for (i = 0; i < SD_N; i++) sd_buf[i] = 0;
    for (i = 0; i < 4; i++) { verb_at[i] = at; at += VLEN[i] + 16u; verb_i[i] = 0; }
    grain_w = grain_have = 0;
    dtime_s = 0.0f;
    lim_g = 1.0f;
}

void dr_panic(void)
{
    for (uint32_t i = 0; i < DR_LAYERS; i++) L[i].fade = 0.0f;
    lim_g = 0.0f;
}

/* the layers, two at a time: 0 and 1 here, 2 and 3 on the second core when it runs (each pair its own buses) */
static float monoA[MAXN], monoB[MAXN], vA[MAXN], vB[MAXN], busL[MAXN], busR[MAXN], busL2[MAXN], busR2[MAXN];
static float capA[MAXN], capB[MAXN];                            /* what SMEAR may record: every layer but SMEAR's */
static float j_bt, j_wrate, j_wtone, j_wv[6], j_kslow;           /* j_kslow: a morph's glide, 0 = none */
static float j_detune, j_unrest;
/* TANGLE: each layer on is moved by the next one on (in turn, round): its loudness against its own usual pushes K1,
 * and its sound (the block before: the layers run on two cores) ring-modulates this one's, normalised to its level */
static int tangle_src(uint32_t li)
{
    for (uint32_t k = 1; k < DR_LAYERS; k++) {
        uint32_t j = (li + k) % DR_LAYERS;
        if (E.L[j].on && L[j].fade > 0.01f) return (int)j;
    }
    return -1;
}
static uint32_t j_n;

static inline float note_lp2(const volatile dr_layer_t *c, uint32_t k)
{
    return ((float)c->note[k] - 69.0f + ((float)c->tune - 64.0f) * (12.0f / 63.0f)) * (1.0f / 12.0f);
}

static void layers_run(uint32_t first)
{
    uint32_t i, li, n = j_n;
    float bt = j_bt, wrate = j_wrate, wtone = j_wtone, *wv = j_wv;
    float *mono = first ? monoB : monoA, *vo = first ? vB : vA;
    float *bL = first ? busL2 : busL, *bR = first ? busR2 : busR, *cap = first ? capB : capA;
    for (i = 0; i < n; i++) bL[i] = bR[i] = cap[i] = 0.0f;
    for (li = first; li < first + 2u; li++) {
        layer_t *l = &L[li];
        const dr_layer_t *c = &E.L[li];
        uint32_t nv = c->nv < 1u ? 1u : c->nv > DR_VOICES ? DR_VOICES : c->nv;
        float want = c->on ? 1.0f : 0.0f;
        float sw_t = 0.02f * fm_exp2f(u7(c->swell) * 11.0f);     /* 20 ms .. 41 s */
        float fk = bt / sw_t;
        if (c->engine != l->pend && c->engine < DR_ENGINES) l->pend = c->engine;
        if (l->fade < 0.0005f && want == 0.0f) {                 /* silent: nothing to do (but a switch can land) */
            l->fade = 0.0f;
            if (l->pend != l->eng) { l->eng = l->pend; layer_reset(l, li); l->sw = 1.0f; }
            l->meter *= 0.8f;
            dr_meter[li] = (uint8_t)(l->meter * 255.0f);
            for (uint32_t k = 0; k < DR_VOICES; k++) { l->v[k].lp2 = note_lp2(c, k); l->v[k].vf = k < nv ? 1.0f : 0.0f; }
            continue;
        }
        /* on and off: linear in amplitude over SWELL, so a long swell is long all the way */
        if (l->fade < want) { l->fade += fk; if (l->fade > want) l->fade = want; }
        else if (l->fade > want) { l->fade -= fk; if (l->fade < want) l->fade = want; }
        /* the engine switch: 25 ms out, the new one in */
        if (l->pend != l->eng) {
            l->sw -= bt * 40.0f;
            if (l->sw <= 0.0f) { l->sw = 0.0f; l->eng = l->pend; layer_reset(l, li); }
        } else if (l->sw < 1.0f) {
            l->sw += bt * 40.0f;
            if (l->sw > 1.0f) l->sw = 1.0f;
        }
        float dr = u7(c->drift);
        float dk = wander(&l->wk, 0.1f + dr * 0.3f, bt, &l->r);
        float ew = wander(&l->we, 0.07f + wrate * 0.5f, bt, &l->r);
        /* K1..K3 glided; K1 moved by the drift and the weather; pressure brightens */
        float push = u7(c->push);
        int xs = j_tangle > 0.001f ? tangle_src(li) : -1;
        float xk = 0.0f, xm = 0.0f, xn = 0.0f, xm1 = 1.0f;
        const float *xsrc = 0;
        if (xs >= 0) {
            float r = x_rms[xs] / (x_avg[xs] + 1e-5f) - 1.0f;    /* the other's loudness against its usual */
            xk = j_tangle * 0.6f * clampf(r, -1.0f, 1.0f);
            xm = j_tangle * j_tangle * 0.75f;                    /* the ring: in over the top half */
            xn = 1.0f / (x_rms[xs] + 1e-4f);
            float cg = fm_rsqrtf((1.0f - xm) * (1.0f - xm) + xm * xm);   /* the blend's level held */
            xn *= cg * 1.15f; xm1 = (1.0f - xm) * cg;
            xsrc = xb[xb_i ^ 1u][xs];
        }
        for (uint32_t k = 0; k < 3; k++) {
            float t = u7(c->p[k]);
            if (k == 0) t = clampf(t + dk * dr * 0.15f + wv[li] * wtone * 0.25f + push * 0.2f + xk, 0.0f, 1.0f);
            l->p[k] += (t - l->p[k]) * (j_kslow > 0.0f ? j_kslow : clampf(bt * 12.0f, 0.0f, 1.0f));
        }
        float lev = u7(c->p[3]);
        lev = lev * lev * (1.0f + push * 0.8f);
        l->level += (lev - l->level) * (j_kslow > 0.0f ? j_kslow : clampf(bt * 20.0f, 0.0f, 1.0f));
        float pan = ((float)c->pan - 64.0f) * (1.0f / 64.0f);
        l->gl = fm_cosf((pan + 1.0f) * 0.25f * FM_PI);
        l->gr = fm_sinf((pan + 1.0f) * 0.25f * FM_PI);
        ek_t ek = {l->p[0], l->p[1], l->p[2], ew, 1.0f / (float)nv};
        /* the voices: each its own pitch (the chord's note, its drift, DETUNE's place, UNREST), in and out of the
         * chord over 40 ms; summed as their count's square root */
        float gt = 0.003f + u7(c->glide) * u7(c->glide) * 8.0f, gk = clampf(bt / gt * 2.0f, 0.0f, 1.0f);
        float fv[DR_VOICES];
        uint32_t nfv = 0, active = 0;
        for (i = 0; i < n; i++) mono[i] = 0.0f;
        for (uint32_t k = 0; k < DR_VOICES; k++) {
            voice_t *v = &l->v[k];
            int von = k < nv;
            float d0 = wander(&v->wd, 0.15f + dr * 0.4f, bt, &v->r);
            float du = wander(&v->wu, 0.8f + j_unrest * 3.0f, bt, &v->r);
            if (!von && v->vf <= 0.0f) { v->lp2 = note_lp2(c, k); continue; }
            if (von && v->vf <= 0.0f) v->lp2 = note_lp2(c, k);   /* a voice joining: at its note, not gliding in */
            v->vf = clampf(v->vf + (von ? bt : -bt) * 25.0f, 0.0f, 1.0f);
            float target = note_lp2(c, k) + (d0 * dr * dr * 0.6f + v->det * j_detune * 0.45f +
                           ((float)li - 1.5f) * j_detune * 0.06f + du * j_unrest * 0.5f) * (1.0f / 12.0f)
                           - j_starve * (0.15f + 0.6f * j_sag);   /* STARVE: sagging, wavering (octaves) */
            float f0 = clampf(440.0f * fm_exp2f(v->lp2), 8.0f, 12000.0f);
            v->lp2 += (target - v->lp2) * gk;
            float f1 = clampf(440.0f * fm_exp2f(v->lp2), 8.0f, 12000.0f);
            if (von) fv[nfv++] = f1;
            if (l->eng == DR_SMEAR) continue;                    /* (SMEAR: one engine for the layer, below) */
            switch (l->eng) {
            case DR_HIVE: eng_hive(v, vo, n, f0, f1, &ek); break;
            case DR_SWARM: eng_swarm(v, vo, n, f0, f1, &ek); break;
            case DR_ROT: eng_rot(v, vo, n, f0, f1, &ek); break;
            case DR_WIND: eng_wind(v, vo, n, f0, f1, &ek); break;
            case DR_HOWL: eng_howl(v, howl_buf[li][k], vo, n, f0, f1, &ek); break;
            case DR_GEIGER: eng_geiger(v, vo, n, f0, f1, &ek); break;
            case DR_HUM: eng_hum(v, vo, n, f0, f1, &ek); break;
            case DR_SCRAPE: eng_scrape(v, vo, n, f0, f1, &ek); break;
            case DR_CHOIR: eng_choir(v, vo, n, f0, f1, &ek); break;
            case DR_STATIC: eng_static(v, vo, n, f0, f1, &ek); break;
            default: eng_chaos(v, vo, n, f0, f1, &ek); break;
            }
            float g = v->vf * (1.0f - j_unrest * 0.45f * fm_fabsf(du));   /* UNREST: the level unsteady too */
            for (i = 0; i < n; i++) mono[i] += vo[i] * g;
            active++;
        }
        if (l->eng == DR_SMEAR) { eng_smear(l, mono, n, fv, nfv, &ek); active = 1; }
        float g = l->fade * l->fade * l->sw * l->level * (active > 1u ? fm_rsqrtf((float)active) : 1.0f), pk = 0.0f;
        float gL = g * l->gl, gR = g * l->gr, gC = l->eng == DR_SMEAR ? 0.0f : g * 0.7f;
        float *xo = xb[xb_i][li], ss = 0.0f;
        if (j_starve > 0.001f) {         /* STARVE: out for 10..150 ms, more often the further it's turned */
            sv_t[li] -= bt;
            if (sv_t[li] <= 0.0f) {
                if (sv_g[li] > 0.5f && rndu(&l->v[0].r) < j_starve * 0.7f) { sv_g[li] = 0.0f; sv_t[li] = 0.01f + 0.14f * rndu(&l->v[0].r); }
                else { sv_g[li] = 1.0f; sv_t[li] = (0.04f + 0.4f * rndu(&l->v[0].r)) / (0.2f + j_starve * 3.0f); }
            }
        } else sv_g[li] = 1.0f;
        float sv0 = sv_s[li], svd = (sv_g[li] - sv0) * 0.6f / (float)n;
        sv_s[li] = sv0 + svd * (float)n;
        for (i = 0; i < n; i++) {
            float y = mono[i] - l->dc;                           /* a 7 Hz DC blocker */
            l->dc += y * 0.001f;
            xo[i] = y * l->fade; ss += y * y;                    /* (for TANGLE: this layer's own, before the ring) */
            if (xsrc) y *= xm1 + xm * clampf(xsrc[i] * xn, -3.5f, 3.5f);
            y *= sv0 + svd * (float)i;
            float ay = y < 0.0f ? -y : y;
            if (ay > pk) pk = ay;
            bL[i] += y * gL;
            bR[i] += y * gR;
            cap[i] += y * gC;
        }
        pk *= g;
        x_rms[li] = fm_sqrtf(ss / (float)n) * l->fade;
        x_avg[li] += (x_rms[li] - x_avg[li]) * clampf(bt * 0.5f, 0.0f, 1.0f);   /* its usual: over ~2 s */
        l->meter = pk > l->meter ? pk : l->meter * 0.92f;
        dr_meter[li] = (uint8_t)(clampf(l->meter * 1.4f, 0.0f, 1.0f) * 255.0f);
    }
}

void dr_render(float *lr, uint32_t n)
{
    uint32_t i, li;
    float bt = (float)n * ISR;                                   /* the block's length, s */
    float wrate = 0.01f * fm_exp2f(u7(E.mod[MOD_SPEED]) * 8.0f);   /* the weather: 0.01..2.5 Hz */
    float wtone = u7(E.mod[MOD_TONE]), wfilt = u7(E.mod[MOD_FILTER]), wspace = u7(E.mod[MOD_SPACE]);
    float wv[6];
    if (n > MAXN) n = MAXN;
    macro_apply();
    xb_i ^= 1u;
    j_starve = u7(E.macro[MC_STARVE]);
    j_sag = 0.5f + 0.5f * wander(&sag_w, 0.7f + j_starve * 6.0f, (float)n * ISR, &rng_state);
    {
        float r = u7(E.macro[MC_ROT]), sp = u7(E.macro[MC_SPACE]);
        float ra = u7(E.macro[MC_RUNAWAY]), rf = u7(E.macro[MC_RUNOFF]), sv = j_starve;
        mt_want = fm_exp2f(-(3.8f * r * r + 2.8f * sp * sp + 7.5f * ra * ra + 4.0f * rf * rf - 1.5f * sv) * (1.0f / 6.02f));
    }
    j_tangle = u7(E.macro[MC_TANGLE]);
    for (i = 0; i < 6; i++) wv[i] = j_wv[i] = wander(&wx[i], wrate * (0.7f + 0.15f * (float)i), bt, &rng_state);
    j_n = n; j_bt = bt; j_wrate = wrate; j_wtone = wtone;
    {
        float d = u7(E.glob[GL_DETUNE]), u = u7(E.glob[GL_UNREST]), mt = u7(E.morph);
        j_detune = d * d; j_unrest = u * u;
        j_kslow = E.morph ? clampf(bt * 3.0f / (0.1f + mt * mt * 20.0f), 0.0f, 1.0f) : 0.0f;
    }
    {   /* layers 2 and 3 to the second core (if it runs), 0 and 1 here; then theirs added to ours */
        int two = plat_cpu2_run(layers_run, 2u);
        layers_run(0u);
        if (two && plat_cpu2_wait() != 0) two = 0;                /* (given up: the job dropped) */
        if (!two) layers_run(2u);
        for (i = 0; i < n; i++) { busL[i] += busL2[i]; busR[i] += busR2[i]; }
    }

    /* ---- SMEAR's memory: the layers, mono, at 22.05 kHz ---- */
    if (E.capture) {
        for (i = 0; i < n; i++) {
            float m = capA[i] + capB[i];
            if (i & 1u) {
                float v = clampf((grain_acc + m) * 0.5f, -1.0f, 1.0f);
                grain_buf[grain_w] = (int16_t)(v * 32767.0f);
                grain_w = grain_w + 1u >= GRAIN_N ? 0u : grain_w + 1u;
                if (grain_have < GRAIN_N) grain_have++;
            } else grain_acc = m;
        }
    }

    /* ---- GLOBAL: the SUB under the lowest note sounding, the HISS under everything ---- */
    {
        float sub = u7(E.glob[GL_SUB]), hiss = u7(E.glob[GL_HISS]), lf = 0.0f;
        int low = 200;
        for (li = 0; li < DR_LAYERS; li++) {
            const dr_layer_t *c = &E.L[li];
            if (L[li].fade > lf) lf = L[li].fade;
            if (!c->on) continue;
            for (uint32_t k = 0; k < c->nv && k < DR_VOICES; k++) if (c->note[k] < low) low = c->note[k];
        }
        if (low < 200) scr_base = 440.0f * fm_exp2f(((float)low - 69.0f) * (1.0f / 12.0f));
        if (low < 200) {                                         /* an octave down; two if that is still above 90 Hz */
            float t = ((float)low - 69.0f - 12.0f) * (1.0f / 12.0f);
            if (440.0f * fm_exp2f(t) > 90.0f) t -= 1.0f;
            if (sub_lp2 < -2.9f) sub_lp2 = t;
            sub_lp2 += (t - sub_lp2) * clampf(bt * 4.0f, 0.0f, 1.0f);
        }
        float sg = sub * sub * 0.55f * lf * lf;
        if (sg > 0.0001f || sub_g > 0.0001f) {
            float inc = 440.0f * fm_exp2f(sub_lp2) * ISR, dg = (sg - sub_g) / (float)n;
            for (i = 0; i < n; i++) {
                sub_ph = wrap1(sub_ph + inc);
                sub_g += dg;
                float y = sat(psin(sub_ph) * 1.3f) * sub_g;
                busL[i] += y; busR[i] += y;
            }
        } else sub_g = 0.0f;
        float hg = hiss * hiss * 0.16f;
        if (hg > 0.0001f) {                                      /* tape hiss, mains hum, the odd crackle */
            for (i = 0; i < n; i++) {
                float w = rndf(&rng_state);
                hiss_lp += (w - hiss_lp) * 0.25f;
                hiss_ph[0] = wrap1(hiss_ph[0] + 50.0f * ISR);
                hiss_ph[1] = wrap1(hiss_ph[1] + 150.0f * ISR);
                float hum = psin(hiss_ph[0]) * 0.5f + psin(hiss_ph[1]) * 0.25f;
                if (rndu(&rng_state) < 3.0f * ISR) hiss_crk = rndf(&rng_state) * 3.0f;
                float y = (hiss_lp * 0.8f + w * 0.2f + hum * 0.35f + hiss_crk) * hg;
                hiss_crk *= 0.6f;
                float y2 = (hiss_lp * 0.6f - w * 0.25f + hum * 0.35f) * hg;
                busL[i] += y; busR[i] += y2;
            }
        }
    }

    /* ---- the effects ---- */
    {   /* the layers' level going in (VOID's reference) */
        float a = 0.0f;
        for (i = 0; i < n; i++) a += fm_fabsf(busL[i]) + fm_fabsf(busR[i]);
        bus_env += (a / (float)(2u * n) - bus_env) * clampf(bt * 4.0f, 0.0f, 1.0f);
    }
    fx_glide(j_kslow > 0.0f ? j_kslow : clampf(bt * 10.0f, 0.0f, 1.0f));
    uint32_t freeze = E.freeze, punch = E.punch;
    {   /* the punches' ramps (in, out), s: most at once; VOID in over 1.4 s, out over 0.8 s; the spaces slower */
        for (i = 0; i < PU_N; i++) {
            float t = (punch >> i) & 1u ? 1.0f : 0.0f, sp = bt / dr_punch_fade(E.pk[i][3]);   /* FADE */
            punch_g[i] += clampf(t - punch_g[i], -sp, sp);
        }
    }
    float g_wb = punch_g[PU_WARBLE], g_inf = punch_g[PU_INFINITE], g_sd = punch_g[PU_SHIMMER], g_th = punch_g[PU_THROW];
    g_wb = g_wb * g_wb * (3.0f - 2.0f * g_wb);                  /* (WARBLE eases in) */
    float g_ring = punch_g[PU_RING], g_dec = punch_g[PU_DECIMATE], g_gate = punch_g[PU_GATE];
    float g_scr = punch_g[PU_SCREAM], g_rot = punch_g[PU_ROTATE];
    /* the punches' own knobs (block rate) */
#define PK(i, k) u7(E.pk[i][k])
    float bu_fz = 4.0f * fm_exp2f(PK(PU_BURN, 0) * 4.0f), bu_q = 64.0f * fm_exp2f(-PK(PU_BURN, 1) * 4.0f), bu_iq = 1.0f / bu_q;
    float vo_soak = 0.1f * fm_exp2f(PK(PU_VOID, 0) * 4.5f), vo_duck = 1.0f - PK(PU_VOID, 1) * 0.7f, vo_dark = PK(PU_VOID, 2);
    int sh_shift = (int)(PK(PU_SHATTER, 0) * 3.99f) - 2;
    uint32_t sh_reps = 1u + (uint32_t)(PK(PU_SHATTER, 1) * 12.0f);
    float sh_rev = PK(PU_SHATTER, 2);
    float wb_rate = 0.03f * fm_exp2f(PK(PU_WARBLE, 0) * 5.0f), wb_dep = PK(PU_WARBLE, 1), wb_wow = PK(PU_WARBLE, 2) * 2.0f;
    float in_fall = 1.5f * fm_exp2f(PK(PU_INFINITE, 0) * 4.0f), in_dark = clampf(PK(PU_INFINITE, 1) * 1.7f, 0.0f, 0.97f);
    float in_drag = PK(PU_INFINITE, 2) * 2.0f;
    float sd_mix = PK(PU_SHIMMER, 0) * 1.6f, sd_feed = PK(PU_SHIMMER, 1) * 1.6f;
    float th_fb = 0.75f + 0.3f * PK(PU_THROW, 0), th_echo = PK(PU_THROW, 1) * 1.2f;
    float ri_base = 7.5f * fm_exp2f(PK(PU_RING, 0) * 4.0f), ri_wob = PK(PU_RING, 1) * 2.0f, ri_mix = PK(PU_RING, 2) * 1.7f;
    float de_step = fm_exp2f(-1.0f - PK(PU_DECIMATE, 0) * 7.0f), de_q = fm_exp2f(6.0f - PK(PU_DECIMATE, 1) * 5.0f);
    float de_iq = 1.0f / de_q;
    float ga_len = fm_exp2f((0.5f - PK(PU_GATE, 0)) * 4.0f), ga_off = 1.0f - PK(PU_GATE, 1);
    float sc_pitch = fm_exp2f((PK(PU_SCREAM, 0) - 0.5f) * 2.0f), sc_fb = PK(PU_SCREAM, 1) * 0.6f;
    float sc_drive = 2.0f * fm_exp2f(PK(PU_SCREAM, 2) * 4.5f);
    float ro_rate = fm_exp2f((PK(PU_ROTATE, 0) - 0.5f) * 5.0f), ro_wob = PK(PU_ROTATE, 1) * 5.0f;
#undef PK
    /* VOID, a held wash: the first 0.45 s soak into the reverb, then it freezes (nothing in, nothing lost) and holds,
     * the dry ducked under it, the wet held as loud as the sound was when it came on; let go: the tail rings out */
    float vd = punch_g[PU_VOID];
    int void_cap = 0, void_hold = 0;
    if ((punch >> PU_VOID) & 1u) {
        if (void_t == 0.0f) { void_ref = bus_env; void_wg = 1.0f; }
        void_t += bt;
        void_cap = void_t < vo_soak; void_hold = !void_cap;
    } else void_t = 0.0f;
    /* INFINITE, a black hole: everything poured into a reverb that loses nothing; held, it deepens (7 s): the dry
     * drains into it, the tail sinks octave on octave, darkens, and the echoes are dragged down */
    if ((punch >> PU_INFINITE) & 1u) inf_depth += bt / in_fall; else inf_depth -= bt / 1.5f;
    inf_depth = clampf(inf_depth, 0.0f, 1.0f);
    float idp = inf_depth * g_inf;
    if (g_sd < g_inf * (0.35f + 0.65f * inf_depth)) g_sd = g_inf * (0.35f + 0.65f * inf_depth);
    /* DIRT */
    float pre = 1.0f + F.drive * F.drive * 30.0f, dmake = 0.35f / sat(pre * 0.35f);
    float qbits = fm_exp2f(15.0f - F.crush * 12.0f), iqbits = 1.0f / qbits;   /* 15 .. 3 bits */
    int crush_on = F.crush > 0.004f;
    float hstep = 1.0f / (1.0f + F.rate * F.rate * 47.0f);       /* the sample rate / 1 .. 48 */
    float dirt = F.dirt;
    /* FILTER: the cutoff moved by its own sweep (the weather's filter wander) */
    float sweep = wv[4] * (F.sweep * 3.0f + wfilt * 3.0f);
    svfc_t fcoef;
    /* SCREAM: amp feedback: the sound driven hard into a resonance that locks onto a harmonic of the lowest note
     * (1.4..2.8 kHz) and feeds back on itself, distorted again after; it barely moves (a slow 3 % drift) */
    {
        float n = fm_floorf(1400.0f / scr_base) + 1.0f, t = scr_base * n;
        if (t > 2800.0f) t = 2000.0f;
        t *= sc_pitch * (1.0f + 0.03f * wander(&pw[3], 0.15f, bt, &rng_state));
        float lt = fm_log2f(t * (1.0f / 20.0f));
        if (scr_ph < 1.0f) scr_ph = lt;
        scr_ph += (lt - scr_ph) * clampf(bt * 1.5f, 0.0f, 1.0f);
    }
    float lc = F.cut * 10.0f + sweep, lsc = scr_ph;
    svf_coef(&fcoef, 20.0f * fm_exp2f(lc + (lsc - lc) * g_scr), (2.0f - 1.96f * F.reso) * (1.0f - g_scr) + 0.07f * g_scr);
    float m = F.mode * 2.0f, wlp = m < 1.0f ? 1.0f - m : 0.0f, wbp = m < 1.0f ? m : 2.0f - m, whp = m > 1.0f ? m - 1.0f : 0.0f;
    wlp += (0.35f - wlp) * g_scr; wbp += (1.0f - wbp) * g_scr; whp -= whp * g_scr;
    float scr_drive = 1.0f + sc_drive * g_scr, scr_fbk = sc_fb * g_scr;
    int fbypass = F.cut > 0.995f && F.mode < 0.01f && g_scr < 0.001f;
    /* DELAY (22.05 kHz): tape wow, UNREST's flutter and dropouts; VOID slows the tape (the echoes fall in pitch) */
    flut_ph = wrap1(flut_ph + 6.3f * bt);
    float dt_want = 0.02f * fm_exp2f(F.time * 6.2f) * 22050.0f;  /* 20 ms .. 1.47 s */
    dt_want *= 1.0f + 0.006f * wspace * wv[5] + 0.003f * j_unrest * psin(flut_ph) + in_drag * idp;   /* (INFINITE drags) */
    if (dt_want > (float)DLY_N - 8.0f) dt_want = (float)DLY_N - 8.0f;
    if (dtime_s <= 0.0f) dtime_s = dt_want;
    if (drop_n) { drop_n--; drop_g += (0.15f - drop_g) * 0.3f; }
    else {
        drop_g += (1.0f - drop_g) * 0.2f;
        if (rndu(&rng_state) < j_unrest * 0.5f * bt) drop_n = 8u + (rnd32(&rng_state) % 40u);   /* a dropout */
    }
    float fbk = freeze ? 1.0f : F.fdbk * 1.12f, dtone = 0.05f + F.tone * F.tone * 0.9f, echo = F.echo * drop_g;
    /* THROW: the delay takes everything in, louder, and runs on near unity; INFINITE: it never lets go either */
    fbk += (1.0f - fbk) * g_inf;
    fbk += (th_fb - fbk) * g_th;
    echo += (th_echo - echo) * g_th;
    float d_in = 1.0f + 0.3f * g_th;
    /* REVERB (22.05 kHz); VOID: longer, darker, everything into it */
    float t60 = 0.4f * fm_exp2f(F.size * 6.3f);                 /* 0.4 .. 32 s */
    float vg[4], vdamp = freeze ? 1.0f : 0.15f + (1.0f - F.damp) * 0.85f, vmod = (F.mod * 5.0f + wspace * 2.0f);
    for (i = 0; i < 4; i++) vg[i] = freeze ? 0.99995f : clampf(fm_exp2f(-9.966f * (float)VLEN[i] / (22050.0f * t60)), 0.0f, 0.9995f);
    {   /* nothing lost: INFINITE, VOID (soaking, then holding) */
        float hold = g_inf > (float)void_hold * vd ? g_inf : (float)void_hold * vd;
        for (i = 0; i < 4; i++) vg[i] += (0.99995f - vg[i]) * hold;
        if (void_cap) for (i = 0; i < 4; i++) vg[i] += (0.999f - vg[i]) * vd;
        vdamp += (1.0f - vdamp) * hold;
    }
    float verb = F.verb, vwet = 1.3f;
    verb += (1.0f - verb) * vd;                                   /* VOID: all wash */
    verb += (0.9f - verb) * (g_inf > g_sd ? g_inf : g_sd) * (verb < 0.9f ? 1.0f : 0.0f);
    vmod += 5.0f * g_inf + 3.0f * idp;                            /* INFINITE: the held mass moves, more as it deepens */
    if (vmod > 8.0f) vmod = 8.0f;                                 /* (the reads stay inside their lines) */
    vwet *= 1.0f - 0.5f * g_sd - 0.3f * g_th - 0.25f * g_inf;   /* (SHIMMER, THROW, INFINITE: as loud as before) */
    vwet *= 1.0f + (void_wg - 1.0f) * vd;                         /* VOID: the wash as loud as the sound it took */
    float dry = (1.0f - vo_duck * vd) * (1.0f - 0.92f * idp), vlpk = (1.0f - in_dark * idp) * (1.0f - 0.9f * vo_dark * vd);
    float vin = freeze ? 0.0f : 1.0f + 1.5f * g_inf;              /* (INFINITE: everything in, at once) */
    if (vd > 0.001f) vin = void_cap ? vin + 2.5f * vd : vin * (1.0f - vd);   /* VOID: soak, then nothing more */
    float burn = punch_g[0], shat = punch_g[2];
    if (shat > 0.001f && !shat_len) {                            /* SHATTER begins: a slice of the last moment */
        shat_len = shat_size(256u << (rnd32(&rng_state) % 4u), sh_shift);
        shat_reps = 1u + rnd32(&rng_state) % sh_reps; shat_pos = 0; shat_left = shat_len; shat_live = 0;
        shat_rev = rndu(&rng_state) < sh_rev ? 1.0f : 0.0f;
    }
    if (shat <= 0.001f) shat_len = 0;
    /* BURN's level: its output held to the level going in (followers below, the gain once a block) */
    if (burn > 0.001f && burn_eout > 1e-5f) burn_g += (clampf(burn_ein / burn_eout, 0.15f, 2.0f) * 1.1f - burn_g) * 0.25f;
    float opk = 0.0f;
    /* RING's carrier: low and unsteady, 30..250 Hz */
    float ring_t = ri_base * fm_exp2f(1.5f + 1.5f * ri_wob * wander(&pw[1], 0.7f, bt, &rng_state));
    ring_f += (ring_t - ring_f) * clampf(bt * 6.0f, 0.0f, 1.0f);
    float ring_inc = ring_f * ISR, rot_rate = ro_rate * (0.8f + ro_wob * (0.5f + 0.5f * wander(&pw[2], 0.3f, bt, &rng_state)));
    float wbw = wander(&pw[0], 0.15f, bt, &rng_state);
    /* RUNAWAY's and RUNOFF's loops (block rate): each tuned a fifth above the lowest note (its 3rd harmonic), raised or
     * lowered by octaves into its range (RUNAWAY 1.4..2.8 kHz, RUNOFF 70..140 Hz); they follow the scene, gliding
     * when the lowest note changes, and nothing else moves them; each line a whole number of its pitch's periods, so
     * the comb and the band agree on it. In as the knob's square */
    float g_lp[2];
    svfc_t lpc[2];
    for (uint32_t q = 0; q < 2u; q++) {
        float g = u7(E.macro[q ? MC_RUNOFF : MC_RUNAWAY]), lo = q ? 70.0f : 1400.0f, f5 = scr_base * 3.0f;
        g_lp[q] = g * g * 0.85f;
        while (f5 < lo) f5 *= 2.0f;
        while (f5 > lo * 2.0f) f5 *= 0.5f;
        if (loop_was[q] <= 0.02f) loop_hz[q] = f5;
        loop_was[q] = g;
        loop_hz[q] += (f5 - loop_hz[q]) * clampf(bt * 3.0f, 0.0f, 1.0f);
        float per = SR / loop_hz[q], dt = per * fm_floorf((q ? 2400.0f : 1800.0f) / per + 0.5f);
        loop_d[q] += (dt - loop_d[q]) * clampf(bt * 2.0f, 0.0f, 1.0f);
        svf_coef(&lpc[q], loop_hz[q], q ? 0.15f : 0.2f);
    }
    /* STARVE's dropouts of the whole output, past a quarter */
    if (j_starve > 0.25f) {
        st_t -= bt;
        if (st_t <= 0.0f) {
            int shut = st_target > 0.5f && rndu(&rng_state) < (j_starve - 0.25f) * 0.8f;
            st_target = shut ? 0.0f : 1.0f;
            st_t = shut ? 0.005f + 0.05f * rndu(&rng_state) : (0.03f + 0.3f * rndu(&rng_state)) / (0.3f + j_starve * 2.0f);
        }
    } else st_target = 1.0f;
    float dlv = u7(E.macro[MC_DELUGE]);
    for (i = 0; i < n; i++) {
        float x[2] = {busL[i], busR[i]};
        /* RUNAWAY's and RUNOFF's loops: the output back in before the dirt */
        for (uint32_t q = 0; q < 2u; q++) if (g_lp[q] > 0.001f) {
            float rp = (float)loop_w - clampf(loop_d[q], 8.0f, (float)(LOOP_N - 8u)) + (float)LOOP_N;
            uint32_t i0 = (uint32_t)rp;
            for (uint32_t c = 0; c < 2; c++) {
                float y = (float)loop_buf[2u * (i0 & (LOOP_N - 1u)) + c] * (1.0f / 32767.0f), lo, bp, hp;
                svf_run(&loop_f[q][c], &lpc[q], y, &lo, &bp, &hp);
                x[c] += sat(bp * 3.0f + y * (q ? 0.3f : 0.5f)) * 0.9f * g_lp[q];
            }
        }
        /* RING: the whole bus against a low, unsteady carrier */
        if (g_ring > 0.001f) {
            ring_ph = wrap1(ring_ph + ring_inc * (1.0f + 0.02f * rndf(&rng_state)));
            float cr = psin(ring_ph) * 1.5f;
            float gr = clampf(g_ring * g_ring * ri_mix, 0.0f, 1.0f);                  /* (it eases in) */
            x[0] += (x[0] * cr - x[0]) * gr; x[1] += (x[1] * cr - x[1]) * gr;
        }
        /* DIRT */
        if (dirt > 0.002f) {
            hold_ph += hstep;
            int take = hold_ph >= 1.0f;
            if (take) hold_ph -= 1.0f;
            for (uint32_t c = 0; c < 2; c++) {
                float w = sat(x[c] * pre) * dmake;
                if (take) hold[c] = w;
                w = hold[c];
                if (crush_on) w = fm_floorf(w * qbits + 0.5f) * iqbits;
                x[c] += (w - x[c]) * dirt;
            }
        }
        /* FILTER */
        if (!fbypass)
            for (uint32_t c = 0; c < 2; c++) {
                float lo, bp, hp;
                svf_run(&fl[c], &fcoef, g_scr > 0.001f ? sat(x[c] * scr_drive + scr_fb[c] * scr_fbk) : x[c], &lo, &bp, &hp);
                scr_fb[c] = bp;
                x[c] = lo * wlp + bp * wbp + hp * whp;
                if (g_scr > 0.001f) x[c] = x[c] + (sat(x[c] * 2.5f) * 0.32f - x[c]) * g_scr;   /* (torn again, held in) */
            }
        /* DELAY and REVERB, at half rate: in on even frames (averaged), out interpolated */
        float mi = (x[0] + x[1]) * 0.5f;
        if (half) {
            float in = (dly_in_acc + mi) * 0.5f;
            dtime_s += (dt_want - dtime_s) * 0.0004f;            /* the tape catches up (pitch bends as it does) */
            float rp = (float)dly_w - dtime_s + (float)DLY_N;
            uint32_t i0 = (uint32_t)rp;
            float fr = rp - (float)i0;
            float y0 = (float)dly_buf[i0 & (DLY_N - 1u)], y1 = (float)dly_buf[(i0 + 1u) & (DLY_N - 1u)];
            float y = (y0 + (y1 - y0) * fr) * (1.0f / 32767.0f);
            float rp2 = (float)dly_w - dtime_s * 0.71f + (float)DLY_N;   /* the right's tap: a little sooner */
            uint32_t j0 = (uint32_t)rp2;
            float fr2 = rp2 - (float)j0;
            float z0 = (float)dly_buf[j0 & (DLY_N - 1u)], z1 = (float)dly_buf[(j0 + 1u) & (DLY_N - 1u)];
            float y2 = (z0 + (z1 - z0) * fr2) * (1.0f / 32767.0f);
            dly_lp += (y - dly_lp) * (freeze ? 1.0f : dtone);
            dly_hp += (dly_lp - dly_hp) * 0.006f;                /* (below ~40 Hz out of the loop) */
            float wv_ = sat((dly_lp - dly_hp) * fbk + (freeze ? 0.0f : in * d_in));
            dly_buf[dly_w] = (int16_t)(wv_ * 32767.0f);
            dly_w = dly_w + 1u >= DLY_N ? 0u : dly_w + 1u;
            dly_prev[0] = dly_out[0]; dly_prev[1] = dly_out[1];
            dly_out[0] = y; dly_out[1] = y2;
            /* the reverb: the dry and the echoes in, diffused, into four lines mixed (Hadamard) */
            float vi = (in + (y + y2) * 0.5f * echo) * 0.5f * vin;
            if (g_sd > 0.001f) vi += sd_out * g_sd * sd_feed;              /* SHIMMER DOWN: the tail, an octave down, back in */
            for (uint32_t k = 0; k < 4; k++) {                   /* four all-pass diffusers */
                uint32_t base = APAT[k], len = APLEN[k];
                float d = (float)ap_buf[base + ap_i[k]] * (1.0f / 32767.0f);
                float v = vi + d * 0.6f;
                ap_buf[base + ap_i[k]] = (int16_t)(clampf(v, -1.0f, 1.0f) * 32767.0f);
                vi = d - v * 0.6f;
                ap_i[k] = ap_i[k] + 1u >= len ? 0u : ap_i[k] + 1u;
            }
            vmod_ph = wrap1(vmod_ph + 0.37f / 22050.0f);
            float mo = psin(vmod_ph) * vmod, mo2 = psin(wrap1(vmod_ph + 0.25f)) * vmod;
            float o0 = vread(0, (float)VLEN[0] - 2.0f - 8.0f - mo), o1 = vread(1, (float)VLEN[1] - 2.0f - 8.0f - mo2);
            float o2 = vread(2, (float)VLEN[2] - 2.0f - 8.0f + mo), o3 = vread(3, (float)VLEN[3] - 2.0f - 8.0f + mo2);
            for (uint32_t k = 0; k < 4; k++) {
                float o = k == 0 ? o0 : k == 1 ? o1 : k == 2 ? o2 : o3;
                verb_lp[k] += (o - verb_lp[k]) * vdamp;
            }
            float h0 = verb_lp[0] + verb_lp[1], h1 = verb_lp[0] - verb_lp[1];
            float h2 = verb_lp[2] + verb_lp[3], h3 = verb_lp[2] - verb_lp[3];
            float m0 = (h0 + h2) * 0.5f, m1 = (h1 + h3) * 0.5f, m2 = (h0 - h2) * 0.5f, m3 = (h1 - h3) * 0.5f;
            float in4[4] = {m0 * vg[0] + vi, m1 * vg[1], m2 * vg[2] + vi, m3 * vg[3]};
            for (uint32_t k = 0; k < 4; k++) {
                verb_buf[verb_at[k] + verb_i[k]] = (int16_t)(clampf(in4[k], -1.0f, 1.0f) * 32767.0f);
                verb_i[k] = verb_i[k] + 1u >= VLEN[k] ? 0u : verb_i[k] + 1u;
            }
            if (g_sd > 0.001f) {   /* SHIMMER DOWN: two read heads at half speed, crossfaded (an octave down) */
                float sin_ = (o0 + o1 + o2 + o3) * 0.45f + vi * 0.5f;
                sd_buf[sd_w] = (int16_t)(clampf(sin_, -1.0f, 1.0f) * 32767.0f);
                sd_d += 0.5f;                                     /* the read falls behind by half a sample a sample */
                if (sd_d >= (float)(SD_N - 4u)) sd_d -= (float)(SD_N - 8u);
                float acc = 0.0f;
                for (uint32_t h = 0; h < 2u; h++) {
                    float d = sd_d + (float)h * (float)(SD_N / 2u - 2u);
                    if (d >= (float)(SD_N - 4u)) d -= (float)(SD_N - 8u);
                    float rp = (float)sd_w - d + (float)SD_N;
                    uint32_t i0 = (uint32_t)rp;
                    float fr = rp - (float)i0, u = d / (float)(SD_N - 4u);
                    float y0 = (float)sd_buf[i0 & (SD_N - 1u)], y1 = (float)sd_buf[(i0 + 1u) & (SD_N - 1u)];
                    float w = u < 0.5f ? u * 2.0f : 2.0f - u * 2.0f;   /* a triangle: each head silent at its jump */
                    acc += (y0 + (y1 - y0) * fr) * (1.0f / 32767.0f) * w;
                }
                sd_out = acc;
                sd_w = (sd_w + 1u) & (SD_N - 1u);
            } else sd_out = 0.0f;
            verb_prev[0] = verb_out[0]; verb_prev[1] = verb_out[1];
            void_lp[0] += ((o0 + o2) * 0.6f - void_lp[0]) * vlpk;   /* VOID darkens the wet */
            void_lp[1] += ((o1 + o3) * 0.6f - void_lp[1]) * vlpk;
            verb_out[0] = void_lp[0] + sd_out * g_sd * sd_mix; verb_out[1] = void_lp[1] + sd_out * g_sd * sd_mix;
        } else dly_in_acc = mi;
        float hk = half ? 0.5f : 1.0f;                           /* between the half-rate outputs (a frame late) */
        float dl = dly_prev[0] + (dly_out[0] - dly_prev[0]) * hk, dR = dly_prev[1] + (dly_out[1] - dly_prev[1]) * hk;
        float vl = verb_prev[0] + (verb_out[0] - verb_prev[0]) * hk, vr = verb_prev[1] + (verb_out[1] - verb_prev[1]) * hk;
        half ^= 1u;
        wet_env += ((fm_fabsf(vl) + fm_fabsf(vr)) * 0.5f * verb * 1.3f - wet_env) * 0.0005f;
        float oL = (x[0] + dl * echo) * dry * (1.0f - 0.45f * verb) + vl * verb * vwet;
        float oR = (x[1] + dR * echo) * dry * (1.0f - 0.45f * verb) + vr * verb * vwet;
        /* BURN: crushed fuzz, held to the level going in */
        burn_ein += (fm_fabsf(oL) + fm_fabsf(oR) - burn_ein) * 0.0008f;
        if (burn > 0.001f) {
            float bl = fm_floorf(sat(oL * bu_fz) * bu_q) * bu_iq;
            float br = fm_floorf(sat(oR * bu_fz) * bu_q) * bu_iq;
            burn_eout += (fm_fabsf(bl) + fm_fabsf(br) - burn_eout) * 0.0008f;
            oL += (bl * burn_g - oL) * burn; oR += (br * burn_g - oR) * burn;
        }
        /* SHATTER: the last moment's slices, repeated, now and then backwards */
        shat_buf[2u * shat_w] = (int16_t)(clampf(oL, -1.0f, 1.0f) * 32767.0f);
        shat_buf[2u * shat_w + 1u] = (int16_t)(clampf(oR, -1.0f, 1.0f) * 32767.0f);
        if (!shat_len || shat_live) shat_w = (shat_w + 1u) & (SHAT_N - 1u);
        if (shat_len) {
            if (shat_live) {                                     /* a slice's worth of live, then a new one */
                if (--shat_live == 0) {
                    shat_len = shat_size(128u << (rnd32(&rng_state) % 5u), sh_shift);
                    shat_reps = 1u + rnd32(&rng_state) % (sh_reps + 1u); shat_pos = 0;
                    shat_rev = rndu(&rng_state) < sh_rev ? 1.0f : 0.0f;
                }
            } else {
                uint32_t p = shat_rev > 0.5f ? shat_len - 1u - shat_pos : shat_pos;
                uint32_t at = (shat_w - shat_len + p) & (SHAT_N - 1u);
                float e = shat_pos < 32u ? (float)shat_pos / 32.0f : shat_len - shat_pos < 32u ? (float)(shat_len - shat_pos) / 32.0f : 1.0f;
                float sl = (float)shat_buf[2u * at] * (1.0f / 32767.0f) * e, sr = (float)shat_buf[2u * at + 1u] * (1.0f / 32767.0f) * e;
                shat_mix = shat;
                oL += (sl - oL) * shat_mix; oR += (sr - oR) * shat_mix;
                if (++shat_pos >= shat_len) {
                    shat_pos = 0;
                    if (--shat_reps == 0) shat_live = shat_len;
                }
            }
        }
        /* DECIMATE: the rate down to ~2.7 kHz and five bits */
        if (g_dec > 0.001f) {
            dec_ph += de_step;
            if (dec_ph >= 1.0f) { dec_ph -= 1.0f; dec_h[0] = fm_floorf(oL * de_q + 0.5f) * de_iq; dec_h[1] = fm_floorf(oR * de_q + 0.5f) * de_iq; }
            oL += (dec_h[0] - oL) * g_dec; oR += (dec_h[1] - oR) * g_dec;
        }
        /* GATE: a failing supply: on and off at uneven lengths (15..130 ms on, 8..90 ms off) */
        if (g_gate > 0.001f) {
            if (!gate_n) {
                gate_on ^= 1u;
                gate_n = (uint32_t)((float)(gate_on ? 660u + rnd32(&rng_state) % 5100u : 350u + rnd32(&rng_state) % 3600u) * ga_len) + 1u;
            }
            gate_n--;
            gate_g += ((gate_on ? 1.0f : ga_off) - gate_g) * 0.06f;
            float gg = 1.0f + (gate_g - 1.0f) * g_gate;
            oL *= gg; oR *= gg;
        } else { gate_n = 0; gate_on = 1u; gate_g = 1.0f; }
        /* WARBLE: a dying tape machine: the output through a short line, its length swung by wow, flutter, drift */
        wb_buf[2u * wb_w] = (int16_t)(clampf(oL, -1.0f, 1.0f) * 32767.0f);
        wb_buf[2u * wb_w + 1u] = (int16_t)(clampf(oR, -1.0f, 1.0f) * 32767.0f);
        if (g_wb > 0.001f) {
            wb_ph[0] = wrap1(wb_ph[0] + 0.11f * ISR);               /* a slow sway (~10 cents) */
            float d = 900.0f + 250.0f * wb_wow * psin(wb_ph[0]) + 60.0f * wbw;
            float rp = (float)wb_w - d + (float)WB_N;
            uint32_t i0 = (uint32_t)rp;
            float fr = rp - (float)i0;
            uint32_t a0 = i0 & (WB_N - 1u), a1 = (i0 + 1u) & (WB_N - 1u);
            float wl = ((float)wb_buf[2u * a0] + ((float)wb_buf[2u * a1] - (float)wb_buf[2u * a0]) * fr) * (1.0f / 32767.0f);
            float wr = ((float)wb_buf[2u * a0 + 1u] + ((float)wb_buf[2u * a1 + 1u] - (float)wb_buf[2u * a0 + 1u]) * fr) * (1.0f / 32767.0f);
            /* ...and a pulse moving through: every ~6 s it swells and dims, darker in the troughs */
            wb_pp = wrap1(wb_pp + wb_rate * ISR);
            float tr = 0.5f - 0.5f * psin(wrap1(wb_pp + 0.25f));      /* 0 at the swell, 1 in the trough */
            float k = 1.0f - clampf(1.7f * wb_dep, 0.0f, 0.95f) * tr * tr;
            wb_lp[0] += (wl - wb_lp[0]) * k; wb_lp[1] += (wr - wb_lp[1]) * k;
            float gg = 1.0f - wb_dep * tr;
            oL += (wb_lp[0] * gg - oL) * g_wb; oR += (wb_lp[1] * gg - oR) * g_wb;
        }
        wb_w = (wb_w + 1u) & (WB_N - 1u);
        /* ROTATE: the stereo field turning, unsteadily */
        if (g_rot > 0.001f) {
            rot_ph = wrap1(rot_ph + rot_rate * ISR);
            float sn = psin(rot_ph) * g_rot, cs = psin(wrap1(rot_ph + 0.25f)) * g_rot + (1.0f - g_rot);
            float nl = oL * cs - oR * sn, nr = oL * sn + oR * cs;
            oL = nl; oR = nr;
        }
        /* RUNAWAY's send: after the punches, so whatever else is held goes round too */
        loop_buf[2u * loop_w] = (int16_t)(clampf(oL, -1.0f, 1.0f) * 32767.0f);
        loop_buf[2u * loop_w + 1u] = (int16_t)(clampf(oR, -1.0f, 1.0f) * 32767.0f);
        loop_w = (loop_w + 1u) & (LOOP_N - 1u);
        /* DELUGE */
        if (dlv > 0.001f) {
            float h[2], l[2];
            if (--dl_hitn <= 0) {
                if (dl_left <= 0) {          /* the next stretch: a burst, a roll or a skip */
                    float u = rndu(&rng_state);
                    dl_mode = u < 0.4f ? 0 : u < 0.8f ? 1 : 2;
                    dl_left = dl_mode == 0 ? 3 + (int32_t)(rnd32(&rng_state) % 7u) : dl_mode == 1 ? 2 + (int32_t)(rnd32(&rng_state) % 5u) : 1;
                }
                dl_left--;
                float gap = dl_mode == 0 ? 0.018f + 0.022f * rndu(&rng_state) : dl_mode == 1 ? 0.045f + 0.045f * rndu(&rng_state)
                                                                                    : 0.09f + 0.26f * rndu(&rng_state);
                dl_hitn = (int32_t)(gap * SR);
                if (dl_mode == 2) { dl_hpk = 0.0f; dl_floor = 0.02f + 0.05f * rndu(&rng_state); dl_att = 0; }
                else {
                    dl_hpk = 0.6f + 0.65f * rndu(&rng_state);
                    dl_floor = 0.08f + 0.15f * rndu(&rng_state);
                    dl_pm = 0.8f + 0.55f * rndu(&rng_state);
                    dl_bend = 1.5f + 0.6f * rndu(&rng_state) + dl_hard * 1.5f;
                    dl_click = 1.0f;
                    dl_att = 1;
                }
            }
            if (dl_att) { dl_henv += (dl_hpk - dl_henv) * 0.06f; if (dl_henv >= 0.95f * dl_hpk) dl_att = 0; }
            else dl_henv += (dl_floor * (1.0f - 0.8f * dl_hard) - dl_henv) * (0.0018f + 0.002f * dl_hard);
            dl_bend += (1.0f - dl_bend) * 0.0035f;
            dl_br += (rndf(&rng_state) - dl_br) * 0.01f;
            dl_ph = wrap1(dl_ph + dl_f * dl_pm * dl_bend * ISR * (1.0f + 0.15f * dl_br));
            float sv = psin(dl_ph) + 0.35f * psin(wrap1(dl_ph * 2.0f)) + dl_br * 0.6f;
            float hard = dl_hard;
            float sub = sat(sv * dl_henv * (1.3f + 1.2f * hard)) * (0.6f + 0.7f * hard) * 1.8f;   /* (louder than HARSH: drones fill the lows) */
            dl_cl_n += (rndf(&rng_state) - dl_cl_n) * 0.3f;
            sub += dl_cl_n * dl_click * hard * 1.2f;
            dl_click *= 0.985f;
            float duck = 1.0f - 0.75f * hard * clampf((dl_henv - dl_floor) * 1.4f, 0.0f, 1.0f);
            for (uint32_t c = 0; c < 2; c++) {
                float v = c ? oR : oL;
                dl_lo1[c] += (v - dl_lo1[c]) * 0.021f; dl_lo2[c] += (dl_lo1[c] - dl_lo2[c]) * 0.021f;
                float hi = v - dl_lo2[c];
                dl_lm[c] += (hi - dl_lm[c]) * 0.082f; hi -= dl_lm[c] * 0.45f;
                float e = fm_fabsf(hi);
                dl_env[c] += (e - dl_env[c]) * (e > dl_env[c] ? 0.03f : 0.004f);
                float gh = clampf(0.4f / (dl_env[c] + 1e-3f), 0.5f, 6.0f);
                dl_top[c] += (sat(hi * gh * 1.2f) - dl_top[c]) * 0.88f;
                h[c] = dl_top[c] * duck;
                l[c] = dl_lo2[c] * 0.25f * duck + sub;   /* (the scene's own lows further under than HARSH's) */
            }
            dl_haas[dl_hw] = h[1];
            h[1] = h[1] * 0.55f + dl_haas[(dl_hw - 331u) & 511u] * 0.45f;
            dl_hw = (dl_hw + 1u) & 511u;
            float mid = (h[0] + h[1]) * 0.5f, side = (h[0] - h[1]) * 0.5f * (1.0f + 0.4f * dlv);
            float yl = mid + side + l[0], yr = mid - side + l[1];
            dl_ein += (fm_fabsf(oL) + fm_fabsf(oR) - dl_ein) * 0.0008f;
            dl_eout += (fm_fabsf(yl) + fm_fabsf(yr) - dl_eout) * 0.0008f;
            oL += (yl * dl_gn - oL) * dlv; oR += (yr * dl_gn - oR) * dlv;
        }
        mtrim += (mt_want - mtrim) * 0.0005f;
        oL *= mtrim; oR *= mtrim;
        /* out: DC blocked, limited (a peak over 0.9 pulls the gain at once; back over ~150 ms) */
        for (uint32_t c = 0; c < 2; c++) {
            float v = c ? oR : oL, y = v - dc_x[c] + 0.9995f * dc_y[c];
            dc_x[c] = v; dc_y[c] = y;
            if (c) oR = y; else oL = y;
        }
        float p = fm_fabsf(oL) > fm_fabsf(oR) ? fm_fabsf(oL) : fm_fabsf(oR);
        lim_g += (1.0f - lim_g) * 0.00015f;
        if (p * lim_g > 0.9f) lim_g = 0.9f / p;
        oL = sat(oL * lim_g * 1.1f) * 0.9f;
        oR = sat(oR * lim_g * 1.1f) * 0.9f;
        if (p > opk) opk = p;
        st_g += (st_target - st_g) * 0.04f;                       /* STARVE's dropouts */
        oL *= st_g; oR *= st_g;
        lr[2 * i] = oL; lr[2 * i + 1] = oR;
    }
    if (dlv > 0.001f) {                  /* DELUGE: the sub's pitch (the lowest note, folded into ~40..75 Hz), the level held */
        float f = scr_base;
        while (f > 76.0f) f *= 0.5f;
        while (f < 38.0f) f *= 2.0f;
        dl_f += (f - dl_f) * clampf(bt * 4.0f, 0.0f, 1.0f);
        dl_hard = dlv > 0.6f ? (dlv - 0.6f) / 0.4f : 0.0f; dl_hard *= dl_hard;
        if (dl_eout > 1e-5f) dl_gn += (clampf(dl_ein / dl_eout, 0.3f, 3.0f) - dl_gn) * 0.2f;
    }
    if (vd > 0.001f && wet_env > 1e-5f) {                        /* VOID's wash held at the level it took */
        float t = clampf(void_ref * 0.9f / (wet_env / (void_wg > 0.01f ? void_wg : 1.0f) + 1e-5f), 0.5f, 4.0f);
        void_wg += (t - void_wg) * (t < void_wg ? 0.3f : 0.05f);
    } else void_wg = 1.0f;
    /* the decaying states, once a block: no denormals */
    for (li = 0; li < DR_LAYERS; li++) {
        layer_t *l = &L[li];
        FL(l->dc);
        for (uint32_t k = 0; k < DR_VOICES; k++) {
            voice_t *v = &l->v[k];
            for (i = 0; i < 10; i++) FL(v->z[i]);
            for (i = 0; i < 4; i++) { FL(v->f[i].ic1); FL(v->f[i].ic2); }
        }
    }
    for (i = 0; i < 4; i++) FL(verb_lp[i]);
    for (i = 0; i < 2; i++) { FL(fl[i].ic1); FL(fl[i].ic2); FL(dc_x[i]); FL(dc_y[i]); FL(verb_out[i]); FL(dly_out[i]); FL(hold[i]); FL(void_lp[i]); }
    FL(dly_lp); FL(dly_hp); FL(burn_eout); FL(hiss_crk); FL(sd_out); FL(scr_fb[0]); FL(scr_fb[1]); FL(wb_lp[0]); FL(wb_lp[1]);
    /* the meters and the scope */
    {
        static float om;
        om = opk > om ? opk : om * 0.9f;
        dr_meter[DR_LAYERS] = (uint8_t)(clampf(om, 0.0f, 1.0f) * 255.0f);
        static uint32_t sp;                                      /* 64 points over 1024 frames (23 ms) */
        for (i = 0; i < n; i += 16u) {
            float v = (lr[2 * i] + lr[2 * i + 1]) * 0.5f;
            dr_scope[sp] = (int8_t)(clampf(v * 1.4f, -1.0f, 1.0f) * 127.0f);
            sp = (sp + 1u) & 63u;
        }
    }
}
