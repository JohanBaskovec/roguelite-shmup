#include "audio.h"

#include "rng.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SR 48000
#define MAX_VOICES 64
#define MAX_PENDING 128
#define MIX_CHUNK 1024

/* ---- Synthesis ----------------------------------------------------------- */

enum { W_SQUARE, W_SAW, W_SINE, W_TRI, W_NOISE };

/* One oscillator layer. Zero fields get sensible defaults (see tone_add). */
typedef struct {
    int wave;
    float f0, f1;          /* start / end frequency, exponential slide (noise: sample-and-hold rate) */
    float dur, delay;      /* seconds */
    float attack;          /* seconds, linear */
    float decay;           /* envelope exponent after attack: (1 - t)^decay */
    float duty;            /* square duty cycle */
    float vib_hz, vib_depth;
    float lp0, lp1;        /* one-pole low-pass cutoff sweep, 0 = off */
    float vol;
} Tone;

typedef struct {
    float *data;
    int len;
} Sample;

typedef struct {
    float volume;       /* per-sound gain */
    float min_interval; /* seconds between two triggers (rate limiting) */
    int max_voices;     /* simultaneous instances */
    float pitch_var;    /* random pitch variation, avoids machine-gun repetition */
} SfxInfo;

static const SfxInfo INFO[SFX_COUNT] = {
    [SFX_SHOT_BLASTER] = {0.45f, 0.05f, 4, 0.06f},
    [SFX_SHOT_SCATTER] = {0.55f, 0.08f, 3, 0.05f},
    [SFX_SHOT_NEEDLER] = {0.60f, 0.05f, 3, 0.08f},
    [SFX_SHOT_SEEKER] = {0.70f, 0.10f, 3, 0.08f},
    [SFX_SHOT_BOUNCER] = {0.40f, 0.08f, 3, 0.08f},
    [SFX_DRONE_SHOT] = {0.42f, 0.06f, 2, 0.10f},
    [SFX_ENEMY_SHOT] = {0.45f, 0.05f, 4, 0.08f},
    [SFX_ENEMY_RING] = {0.45f, 0.10f, 3, 0.05f},
    [SFX_ENEMY_SPIRAL] = {0.36f, 0.08f, 2, 0.05f},
    [SFX_SNIPER_CHARGE] = {0.60f, 0.10f, 3, 0.00f},
    [SFX_SNIPER_FIRE] = {0.45f, 0.05f, 3, 0.05f},
    [SFX_TANK_FIRE] = {0.60f, 0.10f, 2, 0.05f},
    [SFX_ENEMY_APPEAR] = {0.32f, 0.05f, 3, 0.10f},
    [SFX_WAVE] = {0.80f, 0.30f, 1, 0.00f},
    [SFX_HIT] = {0.33f, 0.03f, 4, 0.15f},
    [SFX_BOUNCE] = {0.28f, 0.04f, 2, 0.10f},
    [SFX_EXPLODE_SMALL] = {0.50f, 0.03f, 6, 0.15f},
    [SFX_EXPLODE_BIG] = {0.85f, 0.10f, 2, 0.05f},
    [SFX_PLAYER_HURT] = {0.80f, 0.10f, 1, 0.00f},
    [SFX_PLAYER_DEATH] = {1.00f, 0.50f, 1, 0.00f},
    [SFX_DASH] = {0.90f, 0.05f, 2, 0.05f},
    [SFX_BOMB] = {1.00f, 0.20f, 1, 0.00f},
    [SFX_WEAPON_SWITCH] = {0.50f, 0.05f, 1, 0.00f},
    [SFX_DENIED] = {0.65f, 0.25f, 1, 0.00f},
    [SFX_COIN] = {0.80f, 0.03f, 4, 0.00f},
    [SFX_KEY] = {0.50f, 0.05f, 2, 0.00f},
    [SFX_BOMB_PICKUP] = {0.75f, 0.05f, 2, 0.00f},
    [SFX_HEART] = {0.50f, 0.05f, 2, 0.00f},
    [SFX_ITEM] = {0.60f, 0.20f, 1, 0.00f},
    [SFX_ROOM_CLEAR] = {0.60f, 0.50f, 1, 0.00f},
    [SFX_DOOR_OPEN] = {0.90f, 0.20f, 1, 0.00f},
    [SFX_UNLOCK] = {0.60f, 0.20f, 1, 0.00f},
    [SFX_SECRET] = {0.80f, 0.50f, 1, 0.00f},
    [SFX_DOOR_ENTER] = {1.00f, 0.20f, 1, 0.00f},
    [SFX_UI_START] = {0.50f, 0.20f, 1, 0.00f},
    [SFX_PAUSE] = {0.40f, 0.10f, 1, 0.00f},
    [SFX_SWORD] = {0.55f, 0.05f, 3, 0.10f},
    [SFX_DEFLECT] = {0.45f, 0.04f, 3, 0.15f},
    [SFX_SHIELD] = {0.60f, 0.20f, 1, 0.00f},
    [SFX_POWER_UP] = {0.60f, 0.20f, 1, 0.00f},
    [SFX_CHAOS] = {0.60f, 0.20f, 1, 0.00f},
};

static Sample samples[SFX_COUNT];

static void tone_add(Sample *s, Tone t) {
    if (t.f1 <= 0.0f) t.f1 = t.f0;
    if (t.decay <= 0.0f) t.decay = 1.0f;
    if (t.duty <= 0.0f) t.duty = 0.5f;
    if (t.lp0 > 0.0f && t.lp1 <= 0.0f) t.lp1 = t.lp0;
    if (t.vol <= 0.0f) t.vol = 0.5f;

    int start = (int)(t.delay * SR);
    int n = (int)(t.dur * SR);
    float phase = 0.0f, lp = 0.0f, noise = 0.0f;
    uint32_t seed = 0x9E3779B9u ^ (uint32_t)(t.f0 * 7.0f + t.delay * 1000.0f);

    for (int i = 0; i < n && start + i < s->len; i++) {
        float k = (float)i / (float)n;
        float sec = (float)i / (float)SR;
        float f = t.f0 * powf(t.f1 / t.f0, k);
        if (t.vib_depth > 0.0f) f *= 1.0f + t.vib_depth * sinf(TAU_F * t.vib_hz * sec);

        phase += f / (float)SR;
        bool wrapped = phase >= 1.0f;
        if (wrapped) phase -= floorf(phase);

        float v;
        switch (t.wave) {
        case W_SQUARE: v = phase < t.duty ? 1.0f : -1.0f; break;
        case W_SAW: v = 2.0f * phase - 1.0f; break;
        case W_SINE: v = sinf(TAU_F * phase); break;
        case W_TRI: v = 4.0f * fabsf(phase - 0.5f) - 1.0f; break;
        default:
            if (wrapped || i == 0) {
                seed = seed * 1664525u + 1013904223u;
                noise = (float)(seed >> 8) / 8388608.0f - 1.0f;
            }
            v = noise;
            break;
        }

        if (t.lp0 > 0.0f) {
            float fc = t.lp0 * powf(t.lp1 / t.lp0, k);
            lp += (1.0f - expf(-TAU_F * fc / (float)SR)) * (v - lp);
            v = lp;
        }

        float env;
        if (sec < t.attack) env = sec / t.attack;
        else env = powf(fmaxf(0.0f, 1.0f - (sec - t.attack) / fmaxf(t.dur - t.attack, 1e-4f)), t.decay);

        s->data[start + i] += v * env * t.vol;
    }
}

static void build(SfxId id, const Tone *tones, int n) {
    float len_s = 0.0f;
    for (int i = 0; i < n; i++) len_s = fmaxf(len_s, tones[i].delay + tones[i].dur);
    Sample *s = &samples[id];
    s->len = (int)(len_s * SR) + 2;
    s->data = calloc((size_t)s->len, sizeof(float));
    if (!s->data) {
        s->len = 0;
        return;
    }
    for (int i = 0; i < n; i++) tone_add(s, tones[i]);
}

#define SFX(id, ...)                                         \
    do {                                                     \
        const Tone tones_[] = {__VA_ARGS__};                 \
        build(id, tones_, (int)(sizeof tones_ / sizeof tones_[0])); \
    } while (0)

/* Note frequencies */
#define C5 523.25f
#define E5 659.25f
#define G5 783.99f
#define B5 987.77f
#define C6 1046.5f
#define D6 1174.7f
#define E6 1318.5f
#define G6 1568.0f
#define C7 2093.0f
#define D7 2349.3f
#define WHITE 48000.0f /* noise rate = sample rate -> white noise */

static void generate_all(void) {
    /* -- player weapons -- */
    SFX(SFX_SHOT_BLASTER,
        {.wave = W_SQUARE, .f0 = 1500, .f1 = 450, .dur = 0.065f, .decay = 2, .duty = 0.25f, .lp0 = 7000, .lp1 = 2500, .vol = 0.5f},
        {.wave = W_NOISE, .f0 = WHITE, .dur = 0.012f, .vol = 0.2f});
    SFX(SFX_SHOT_SCATTER,
        {.wave = W_NOISE, .f0 = WHITE, .dur = 0.17f, .decay = 2, .lp0 = 7000, .lp1 = 700, .vol = 0.7f},
        {.wave = W_SINE, .f0 = 200, .f1 = 55, .dur = 0.13f, .decay = 1.5f, .vol = 0.7f},
        {.wave = W_SQUARE, .f0 = 400, .f1 = 100, .dur = 0.05f, .vol = 0.15f});
    SFX(SFX_SHOT_NEEDLER,
        {.wave = W_SQUARE, .f0 = 2800, .f1 = 1900, .dur = 0.035f, .decay = 2, .duty = 0.125f, .vol = 0.4f});
    SFX(SFX_SHOT_SEEKER,
        {.wave = W_SAW, .f0 = 250, .f1 = 1000, .dur = 0.14f, .decay = 1.5f, .lp0 = 2500, .lp1 = 4000, .vol = 0.3f},
        {.wave = W_NOISE, .f0 = WHITE, .dur = 0.18f, .attack = 0.02f, .decay = 2, .lp0 = 1500, .lp1 = 4000, .vol = 0.35f});
    SFX(SFX_SHOT_BOUNCER,
        {.wave = W_SINE, .f0 = 700, .f1 = 240, .dur = 0.11f, .decay = 1.5f, .vol = 0.6f},
        {.wave = W_TRI, .f0 = 1400, .f1 = 480, .dur = 0.06f, .vol = 0.2f});
    SFX(SFX_DRONE_SHOT,
        {.wave = W_SQUARE, .f0 = 2000, .f1 = 1300, .dur = 0.03f, .decay = 2, .vol = 0.35f});

    /* -- enemies -- */
    SFX(SFX_ENEMY_SHOT,
        {.wave = W_TRI, .f0 = 900, .f1 = 380, .dur = 0.09f, .decay = 1.5f, .vol = 0.55f},
        {.wave = W_SQUARE, .f0 = 450, .f1 = 190, .dur = 0.07f, .lp0 = 3000, .vol = 0.15f});
    SFX(SFX_ENEMY_RING,
        {.wave = W_SINE, .f0 = 1000, .f1 = 180, .dur = 0.3f, .decay = 1.4f, .vib_hz = 18, .vib_depth = 0.05f, .vol = 0.55f},
        {.wave = W_NOISE, .f0 = WHITE, .dur = 0.08f, .lp0 = 3000, .lp1 = 800, .vol = 0.25f},
        {.wave = W_TRI, .f0 = 500, .f1 = 90, .dur = 0.25f, .vol = 0.25f});
    SFX(SFX_ENEMY_SPIRAL,
        {.wave = W_TRI, .f0 = 1300, .f1 = 1050, .dur = 0.035f, .decay = 2, .vol = 0.5f});
    SFX(SFX_SNIPER_CHARGE,
        {.wave = W_SINE, .f0 = 250, .f1 = 1600, .dur = 0.7f, .attack = 0.62f, .decay = 4, .vib_hz = 14, .vib_depth = 0.03f, .vol = 0.4f},
        {.wave = W_SQUARE, .f0 = 125, .f1 = 800, .dur = 0.7f, .attack = 0.62f, .decay = 4, .lp0 = 1500, .lp1 = 3000, .vol = 0.08f});
    SFX(SFX_SNIPER_FIRE,
        {.wave = W_SQUARE, .f0 = 2400, .f1 = 180, .dur = 0.2f, .decay = 1.6f, .lp0 = 8000, .lp1 = 1500, .vol = 0.4f},
        {.wave = W_NOISE, .f0 = WHITE, .dur = 0.07f, .lp0 = 9000, .lp1 = 2000, .vol = 0.4f});
    SFX(SFX_TANK_FIRE,
        {.wave = W_NOISE, .f0 = 6000, .f1 = 500, .dur = 0.32f, .decay = 1.8f, .lp0 = 3000, .lp1 = 300, .vol = 0.8f},
        {.wave = W_SINE, .f0 = 130, .f1 = 40, .dur = 0.28f, .decay = 1.5f, .vol = 0.8f},
        {.wave = W_SQUARE, .f0 = 220, .f1 = 60, .dur = 0.12f, .lp0 = 1500, .lp1 = 400, .vol = 0.15f});
    SFX(SFX_ENEMY_APPEAR,
        {.wave = W_SINE, .f0 = 350, .f1 = 900, .dur = 0.09f, .decay = 1.5f, .vol = 0.5f},
        {.wave = W_NOISE, .f0 = WHITE, .dur = 0.06f, .attack = 0.03f, .lp0 = 2000, .lp1 = 5000, .vol = 0.15f});
    SFX(SFX_WAVE,
        {.wave = W_SQUARE, .f0 = 440, .dur = 0.12f, .decay = 0.6f, .lp0 = 3000, .vol = 0.3f},
        {.wave = W_SQUARE, .f0 = 660, .dur = 0.14f, .delay = 0.14f, .decay = 0.8f, .lp0 = 3000, .vol = 0.3f},
        {.wave = W_SINE, .f0 = 200, .f1 = 900, .dur = 0.35f, .attack = 0.3f, .decay = 3, .vol = 0.25f});

    /* -- impacts -- */
    SFX(SFX_HIT,
        {.wave = W_NOISE, .f0 = WHITE, .dur = 0.03f, .decay = 3, .lp0 = 9000, .lp1 = 3000, .vol = 0.6f},
        {.wave = W_SQUARE, .f0 = 1400, .f1 = 900, .dur = 0.025f, .vol = 0.25f});
    SFX(SFX_BOUNCE,
        {.wave = W_SINE, .f0 = 1100, .f1 = 750, .dur = 0.04f, .decay = 2, .vol = 0.5f});
    SFX(SFX_EXPLODE_SMALL,
        {.wave = W_NOISE, .f0 = 5000, .f1 = 400, .dur = 0.28f, .decay = 2, .lp0 = 6000, .lp1 = 500, .vol = 0.7f},
        {.wave = W_SINE, .f0 = 180, .f1 = 50, .dur = 0.18f, .decay = 2, .vol = 0.6f});
    SFX(SFX_EXPLODE_BIG,
        {.wave = W_NOISE, .f0 = 3000, .f1 = 150, .dur = 0.8f, .decay = 1.6f, .lp0 = 4000, .lp1 = 150, .vol = 0.8f},
        {.wave = W_SINE, .f0 = 110, .f1 = 30, .dur = 0.55f, .decay = 1.3f, .vol = 0.9f},
        {.wave = W_NOISE, .f0 = 8000, .f1 = 2000, .dur = 0.3f, .delay = 0.05f, .decay = 2, .vol = 0.2f});

    /* -- player -- */
    SFX(SFX_PLAYER_HURT,
        {.wave = W_SQUARE, .f0 = 600, .f1 = 120, .dur = 0.28f, .decay = 1.2f, .vib_hz = 35, .vib_depth = 0.25f, .lp0 = 4000, .lp1 = 1500, .vol = 0.5f},
        {.wave = W_NOISE, .f0 = 6000, .f1 = 1000, .dur = 0.15f, .lp0 = 6000, .lp1 = 1500, .vol = 0.5f});
    SFX(SFX_PLAYER_DEATH,
        {.wave = W_NOISE, .f0 = 3000, .f1 = 60, .dur = 1.4f, .decay = 1.5f, .lp0 = 5000, .lp1 = 100, .vol = 0.7f},
        {.wave = W_SAW, .f0 = 600, .f1 = 40, .dur = 1.2f, .lp0 = 3000, .lp1 = 300, .vol = 0.3f},
        {.wave = W_SINE, .f0 = 100, .f1 = 30, .dur = 0.8f, .vol = 0.6f});
    /* Sword: an airy whoosh with a metallic edge; cutting a bullet rings like a small bell. */
    SFX(SFX_SWORD,
        {.wave = W_NOISE, .f0 = WHITE, .dur = 0.14f, .attack = 0.02f, .decay = 2, .lp0 = 1200, .lp1 = 7000, .vol = 0.55f},
        {.wave = W_SAW, .f0 = 900, .f1 = 1500, .dur = 0.08f, .decay = 2, .lp0 = 3000, .vol = 0.12f});
    SFX(SFX_DEFLECT,
        {.wave = W_SINE, .f0 = 2400, .f1 = 2200, .dur = 0.12f, .decay = 2.5f, .vol = 0.35f},
        {.wave = W_SQUARE, .f0 = 3600, .f1 = 3000, .dur = 0.03f, .duty = 0.25f, .vol = 0.12f});
    /* Active items: a glassy bubble forming, a rising power chord, a wobbly random arpeggio. */
    SFX(SFX_SHIELD,
        {.wave = W_SINE, .f0 = 300, .f1 = 1200, .dur = 0.25f, .attack = 0.05f, .decay = 2, .vib_hz = 18, .vib_depth = 0.03f, .vol = 0.4f},
        {.wave = W_TRI, .f0 = E6, .dur = 0.4f, .delay = 0.12f, .decay = 3, .vol = 0.25f},
        {.wave = W_NOISE, .f0 = WHITE, .dur = 0.2f, .attack = 0.1f, .decay = 3, .lp0 = 3000, .lp1 = 9000, .vol = 0.15f});
    SFX(SFX_POWER_UP,
        {.wave = W_SAW, .f0 = 110, .f1 = 220, .dur = 0.35f, .decay = 2, .lp0 = 600, .lp1 = 3000, .vol = 0.4f},
        {.wave = W_SQUARE, .f0 = C5, .dur = 0.08f, .duty = 0.3f, .vol = 0.25f},
        {.wave = W_SQUARE, .f0 = G5, .dur = 0.08f, .delay = 0.06f, .duty = 0.3f, .vol = 0.25f},
        {.wave = W_SQUARE, .f0 = C6, .dur = 0.25f, .delay = 0.12f, .decay = 2, .duty = 0.3f, .vol = 0.3f});
    SFX(SFX_CHAOS,
        {.wave = W_TRI, .f0 = E5, .dur = 0.07f, .vol = 0.3f},
        {.wave = W_TRI, .f0 = B5, .dur = 0.07f, .delay = 0.05f, .vol = 0.3f},
        {.wave = W_TRI, .f0 = G5, .dur = 0.07f, .delay = 0.10f, .vol = 0.3f},
        {.wave = W_TRI, .f0 = D6, .dur = 0.07f, .delay = 0.15f, .vol = 0.3f},
        {.wave = W_TRI, .f0 = C6, .dur = 0.07f, .delay = 0.20f, .vol = 0.3f},
        {.wave = W_SINE, .f0 = G6, .dur = 0.5f, .delay = 0.25f, .decay = 2, .vib_hz = 9, .vib_depth = 0.04f, .vol = 0.3f});
    SFX(SFX_DASH,
        {.wave = W_NOISE, .f0 = WHITE, .dur = 0.16f, .attack = 0.03f, .decay = 2, .lp0 = 800, .lp1 = 6000, .vol = 0.6f},
        {.wave = W_SINE, .f0 = 300, .f1 = 700, .dur = 0.12f, .vol = 0.15f});
    SFX(SFX_BOMB,
        {.wave = W_NOISE, .f0 = 6000, .f1 = 100, .dur = 1.3f, .decay = 1.6f, .lp0 = 8000, .lp1 = 120, .vol = 0.8f},
        {.wave = W_SINE, .f0 = 80, .f1 = 25, .dur = 1.0f, .decay = 1.3f, .vol = 0.9f},
        {.wave = W_SAW, .f0 = 200, .f1 = 40, .dur = 0.5f, .lp0 = 1500, .lp1 = 200, .vol = 0.2f});
    SFX(SFX_WEAPON_SWITCH,
        {.wave = W_SQUARE, .f0 = 700, .dur = 0.035f, .vol = 0.3f},
        {.wave = W_SQUARE, .f0 = 1050, .dur = 0.05f, .delay = 0.04f, .vol = 0.3f},
        {.wave = W_NOISE, .f0 = WHITE, .dur = 0.01f, .vol = 0.2f});
    SFX(SFX_DENIED,
        {.wave = W_SQUARE, .f0 = 160, .dur = 0.1f, .decay = 0.5f, .lp0 = 2000, .vol = 0.45f},
        {.wave = W_SQUARE, .f0 = 120, .dur = 0.16f, .delay = 0.12f, .lp0 = 2000, .vol = 0.45f});

    /* -- pickups -- */
    SFX(SFX_COIN,
        {.wave = W_SQUARE, .f0 = B5, .dur = 0.06f, .decay = 0.5f, .vol = 0.35f},
        {.wave = W_SQUARE, .f0 = E6, .dur = 0.22f, .delay = 0.06f, .decay = 1.5f, .vol = 0.35f});
    SFX(SFX_KEY,
        {.wave = W_TRI, .f0 = D6, .dur = 0.08f, .vol = 0.5f},
        {.wave = W_TRI, .f0 = G6, .dur = 0.08f, .delay = 0.07f, .vol = 0.5f},
        {.wave = W_TRI, .f0 = D7, .dur = 0.3f, .delay = 0.14f, .decay = 2, .vol = 0.5f});
    SFX(SFX_BOMB_PICKUP,
        {.wave = W_SQUARE, .f0 = 196, .f1 = 392, .dur = 0.14f, .duty = 0.25f, .vol = 0.35f},
        {.wave = W_TRI, .f0 = 392, .dur = 0.2f, .delay = 0.1f, .vol = 0.3f});
    SFX(SFX_HEART,
        {.wave = W_SINE, .f0 = C5, .dur = 0.1f, .vol = 0.5f},
        {.wave = W_SINE, .f0 = E5, .dur = 0.1f, .delay = 0.08f, .vol = 0.5f},
        {.wave = W_SINE, .f0 = G5, .dur = 0.1f, .delay = 0.16f, .vol = 0.5f},
        {.wave = W_SINE, .f0 = C6, .dur = 0.35f, .delay = 0.24f, .decay = 2, .vol = 0.5f},
        {.wave = W_TRI, .f0 = C6 * 2.0f, .dur = 0.3f, .delay = 0.24f, .decay = 2, .vol = 0.1f});
    SFX(SFX_ITEM,
        {.wave = W_SQUARE, .f0 = G5, .dur = 0.1f, .vol = 0.3f},
        {.wave = W_SQUARE, .f0 = C6, .dur = 0.1f, .delay = 0.06f, .vol = 0.3f},
        {.wave = W_SQUARE, .f0 = E6, .dur = 0.1f, .delay = 0.12f, .vol = 0.3f},
        {.wave = W_SQUARE, .f0 = G6, .dur = 0.1f, .delay = 0.18f, .vol = 0.3f},
        {.wave = W_TRI, .f0 = C7, .dur = 0.55f, .delay = 0.24f, .decay = 1.5f, .vib_hz = 7, .vib_depth = 0.015f, .vol = 0.45f},
        {.wave = W_SINE, .f0 = C6, .dur = 0.55f, .delay = 0.24f, .decay = 1.5f, .vol = 0.3f});

    /* -- rooms & UI -- */
    SFX(SFX_ROOM_CLEAR,
        {.wave = W_SQUARE, .f0 = C5, .dur = 0.12f, .decay = 1.5f, .duty = 0.25f, .vol = 0.3f},
        {.wave = W_SQUARE, .f0 = E5, .dur = 0.12f, .delay = 0.07f, .decay = 1.5f, .duty = 0.25f, .vol = 0.3f},
        {.wave = W_SQUARE, .f0 = G5, .dur = 0.12f, .delay = 0.14f, .decay = 1.5f, .duty = 0.25f, .vol = 0.3f},
        {.wave = W_SQUARE, .f0 = C6, .dur = 0.12f, .delay = 0.21f, .decay = 1.5f, .duty = 0.25f, .vol = 0.3f},
        {.wave = W_TRI, .f0 = C6, .dur = 0.6f, .delay = 0.28f, .decay = 2, .vib_hz = 6, .vib_depth = 0.01f, .vol = 0.25f},
        {.wave = W_TRI, .f0 = E6, .dur = 0.6f, .delay = 0.28f, .decay = 2, .vib_hz = 6, .vib_depth = 0.01f, .vol = 0.25f},
        {.wave = W_TRI, .f0 = G6, .dur = 0.6f, .delay = 0.28f, .decay = 2, .vib_hz = 6, .vib_depth = 0.01f, .vol = 0.25f});
    SFX(SFX_DOOR_OPEN,
        {.wave = W_NOISE, .f0 = WHITE, .dur = 0.45f, .attack = 0.1f, .decay = 2, .lp0 = 300, .lp1 = 3000, .vol = 0.45f},
        {.wave = W_SINE, .f0 = 110, .f1 = 220, .dur = 0.4f, .attack = 0.05f, .decay = 1.5f, .vol = 0.45f},
        {.wave = W_TRI, .f0 = 440, .f1 = 880, .dur = 0.25f, .delay = 0.15f, .vol = 0.15f});
    SFX(SFX_UNLOCK,
        {.wave = W_NOISE, .f0 = WHITE, .dur = 0.02f, .lp0 = 10000, .vol = 0.5f},
        {.wave = W_SQUARE, .f0 = 1200, .dur = 0.03f, .vol = 0.3f},
        {.wave = W_NOISE, .f0 = WHITE, .dur = 0.025f, .delay = 0.07f, .lp0 = 8000, .vol = 0.5f},
        {.wave = W_SQUARE, .f0 = 1700, .dur = 0.04f, .delay = 0.07f, .vol = 0.3f},
        {.wave = W_TRI, .f0 = G6, .dur = 0.2f, .delay = 0.12f, .vol = 0.35f},
        {.wave = W_TRI, .f0 = C7, .dur = 0.35f, .delay = 0.18f, .decay = 2, .vol = 0.35f});
    /* Rubble burst, then a rising shimmering arpeggio: a hidden passage opened. */
    SFX(SFX_SECRET,
        {.wave = W_NOISE, .f0 = WHITE, .dur = 0.35f, .decay = 3, .lp0 = 1800, .lp1 = 300, .vol = 0.7f},
        {.wave = W_SINE, .f0 = 90, .f1 = 40, .dur = 0.3f, .decay = 2, .vol = 0.6f},
        {.wave = W_TRI, .f0 = G5, .dur = 0.12f, .delay = 0.18f, .vol = 0.35f},
        {.wave = W_TRI, .f0 = B5, .dur = 0.12f, .delay = 0.26f, .vol = 0.35f},
        {.wave = W_TRI, .f0 = D6, .dur = 0.12f, .delay = 0.34f, .vol = 0.35f},
        {.wave = W_TRI, .f0 = G6, .dur = 0.6f, .delay = 0.42f, .decay = 2, .vib_hz = 7, .vib_depth = 0.01f, .vol = 0.4f},
        {.wave = W_SINE, .f0 = D7, .dur = 0.5f, .delay = 0.46f, .decay = 3, .vol = 0.15f});
    SFX(SFX_DOOR_ENTER,
        {.wave = W_NOISE, .f0 = WHITE, .dur = 0.4f, .attack = 0.25f, .decay = 3, .lp0 = 400, .lp1 = 5000, .vol = 0.5f},
        {.wave = W_SINE, .f0 = 200, .f1 = 600, .dur = 0.35f, .attack = 0.3f, .decay = 3, .vol = 0.2f});
    SFX(SFX_UI_START,
        {.wave = W_SQUARE, .f0 = C5, .dur = 0.1f, .duty = 0.25f, .vol = 0.35f},
        {.wave = W_SQUARE, .f0 = G5, .dur = 0.1f, .delay = 0.06f, .duty = 0.25f, .vol = 0.35f},
        {.wave = W_SQUARE, .f0 = C6, .dur = 0.1f, .delay = 0.12f, .duty = 0.25f, .vol = 0.35f},
        {.wave = W_SQUARE, .f0 = G6, .dur = 0.3f, .delay = 0.18f, .decay = 2, .duty = 0.25f, .vol = 0.35f});
    SFX(SFX_PAUSE,
        {.wave = W_SINE, .f0 = 880, .dur = 0.06f, .vol = 0.5f},
        {.wave = W_SINE, .f0 = 660, .dur = 0.1f, .delay = 0.06f, .vol = 0.5f});
}

/* ---- Mixer --------------------------------------------------------------- */

typedef struct {
    bool active;
    int id;
    float pos, rate, gl, gr;
    uint64_t order;
} Voice;

typedef struct {
    int id;
    float vol, rate, pan;
} Request;

static SDL_AudioStream *stream;
static Voice voices[MAX_VOICES];       /* owned by the audio thread (guarded by the stream lock) */
static Request pending[MAX_PENDING];   /* owned by the game thread */
static int npending;
static uint64_t last_play_ns[SFX_COUNT];
static uint64_t voice_order;
static Rng arng;
static bool muted;
static const float master = 1.0f;

static void SDLCALL audio_callback(void *userdata, SDL_AudioStream *st, int additional, int total) {
    (void)userdata;
    (void)total;
    float buf[MIX_CHUNK * 2];
    int frames = additional / (int)(sizeof(float) * 2);
    while (frames > 0) {
        int n = frames > MIX_CHUNK ? MIX_CHUNK : frames;
        memset(buf, 0, sizeof(float) * 2 * (size_t)n);
        for (int v = 0; v < MAX_VOICES; v++) {
            Voice *vo = &voices[v];
            if (!vo->active) continue;
            const Sample *s = &samples[vo->id];
            for (int i = 0; i < n; i++) {
                int ip = (int)vo->pos;
                if (ip >= s->len - 1) {
                    vo->active = false;
                    break;
                }
                float fr = vo->pos - (float)ip;
                float x = s->data[ip] + (s->data[ip + 1] - s->data[ip]) * fr;
                buf[i * 2] += x * vo->gl;
                buf[i * 2 + 1] += x * vo->gr;
                vo->pos += vo->rate;
            }
        }
        float m = muted ? 0.0f : master;
        for (int i = 0; i < n * 2; i++) buf[i] = tanhf(buf[i] * m); /* soft limiter */
        SDL_PutAudioStreamData(st, buf, (int)(sizeof(float) * 2 * (size_t)n));
        frames -= n;
    }
}

bool audio_init(void) {
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        SDL_Log("Audio disabled: %s", SDL_GetError());
        return false;
    }
    rng_seed(&arng, SDL_GetPerformanceCounter());
    generate_all();
    SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, "512"); /* low latency */
    SDL_AudioSpec spec = {SDL_AUDIO_F32, 2, SR};
    stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, audio_callback, NULL);
    if (!stream) {
        SDL_Log("Audio disabled: %s", SDL_GetError());
        return false;
    }
    SDL_ResumeAudioStreamDevice(stream);
    return true;
}

void audio_shutdown(void) {
    if (stream) SDL_DestroyAudioStream(stream);
    stream = NULL;
    for (int i = 0; i < SFX_COUNT; i++) {
        free(samples[i].data);
        samples[i].data = NULL;
        samples[i].len = 0;
    }
}

void audio_toggle_mute(void) { muted = !muted; }

static const char *const SFX_NAMES[] = {
    "shot_blaster", "shot_scatter", "shot_needler", "shot_seeker", "shot_bouncer", "drone_shot",
    "enemy_shot", "enemy_ring", "enemy_spiral", "sniper_charge", "sniper_fire", "tank_fire",
    "enemy_appear", "wave",
    "hit", "bounce", "explode_small", "explode_big",
    "player_hurt", "player_death", "dash", "bomb", "weapon_switch", "denied",
    "coin", "key", "bomb_pickup", "heart", "item",
    "room_clear", "door_open", "unlock", "secret", "door_enter", "ui_start", "pause",
    "sword", "deflect", "shield", "power_up", "chaos",
};
_Static_assert(sizeof SFX_NAMES / sizeof SFX_NAMES[0] == SFX_COUNT, "one WAV name per SfxId, in enum order");

static void put_u32(FILE *f, uint32_t v) { uint8_t b[4] = {(uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24)}; fwrite(b, 1, 4, f); }
static void put_u16(FILE *f, uint16_t v) { uint8_t b[2] = {(uint8_t)v, (uint8_t)(v >> 8)}; fwrite(b, 1, 2, f); }

bool audio_dump_wavs(const char *dir) {
    if (samples[0].len == 0) generate_all();
    for (int i = 0; i < SFX_COUNT; i++) {
        char path[512];
        snprintf(path, sizeof path, "%s/%02d_%s.wav", dir, i, SFX_NAMES[i]);
        FILE *f = fopen(path, "wb");
        if (!f) return false;
        const Sample *s = &samples[i];
        uint32_t bytes = (uint32_t)s->len * 2;
        fwrite("RIFF", 1, 4, f); put_u32(f, 36 + bytes); fwrite("WAVEfmt ", 1, 8, f);
        put_u32(f, 16); put_u16(f, 1); put_u16(f, 1); put_u32(f, SR); put_u32(f, SR * 2); put_u16(f, 2); put_u16(f, 16);
        fwrite("data", 1, 4, f); put_u32(f, bytes);
        for (int j = 0; j < s->len; j++) {
            /* Same gain staging as the in-game mixer (centered voice). */
            float x = tanhf(s->data[j] * INFO[i].volume * sqrtf(0.5f) * master);
            put_u16(f, (uint16_t)(int16_t)lrintf(clampf(x, -1.0f, 1.0f) * 32767.0f));
        }
        fclose(f);
    }
    return true;
}
bool audio_muted(void) { return muted; }

void sfx_play_ex(SfxId id, float vol, float pitch, float pan) {
    if (!stream || id < 0 || id >= SFX_COUNT || samples[id].len == 0) return;
    uint64_t now = SDL_GetTicksNS();
    if (last_play_ns[id] != 0 && now - last_play_ns[id] < (uint64_t)(INFO[id].min_interval * 1e9f)) return;
    last_play_ns[id] = now;
    if (npending >= MAX_PENDING) return;
    float pv = INFO[id].pitch_var;
    Request *r = &pending[npending++];
    r->id = id;
    r->vol = vol * INFO[id].volume;
    r->rate = pitch * (1.0f + rng_range(&arng, -pv, pv));
    r->pan = clampf(pan, -1.0f, 1.0f);
}

void sfx_play(SfxId id) { sfx_play_ex(id, 1.0f, 1.0f, 0.0f); }

void sfx_play_at(SfxId id, V2 pos) {
    sfx_play_ex(id, 1.0f, 1.0f, (pos.x - ARENA_CX) / (ARENA_X1 - ARENA_CX) * 0.7f);
}

static void start_voice(const Request *r) {
    int same = 0, oldest_same = -1, free_slot = -1, oldest = -1;
    for (int i = 0; i < MAX_VOICES; i++) {
        Voice *v = &voices[i];
        if (!v->active) {
            if (free_slot < 0) free_slot = i;
            continue;
        }
        if (oldest < 0 || v->order < voices[oldest].order) oldest = i;
        if (v->id == r->id) {
            same++;
            if (oldest_same < 0 || v->order < voices[oldest_same].order) oldest_same = i;
        }
    }
    int slot = same >= INFO[r->id].max_voices ? oldest_same : (free_slot >= 0 ? free_slot : oldest);
    if (slot < 0) return;
    Voice *v = &voices[slot];
    v->active = true;
    v->id = r->id;
    v->pos = 0.0f;
    v->rate = r->rate;
    v->gl = r->vol * sqrtf(0.5f * (1.0f - r->pan));
    v->gr = r->vol * sqrtf(0.5f * (1.0f + r->pan));
    v->order = ++voice_order;
}

void audio_flush(void) {
    if (!stream || npending == 0) return;
    SDL_LockAudioStream(stream); /* the callback runs with this lock held */
    for (int i = 0; i < npending; i++) start_voice(&pending[i]);
    SDL_UnlockAudioStream(stream);
    npending = 0;
}
