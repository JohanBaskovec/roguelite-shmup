#ifndef COMMON_H
#define COMMON_H

#include <SDL3/SDL.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>

/* Logical resolution: the renderer letterboxes this onto the real window. */
#define SCREEN_W 1920
#define SCREEN_H 1080

/* Fixed simulation step. 120 Hz keeps fast bullets and collisions crisp. */
#define TICK_RATE 120
#define DT (1.0f / (float)TICK_RATE)

#define PI_F 3.14159265358979f
#define TAU_F 6.28318530717959f

/* Playfield in logical pixels. A whole room always fits on screen. */
/* The arena is a whole number of GRID cells (30 x 15): the floor's grid, room layouts sit on it. */
#define GRID 60.0f
#define GW 30 /* cells across: (ARENA_X1 - ARENA_X0) / GRID */
#define GH 15 /* cells down: (ARENA_Y1 - ARENA_Y0) / GRID */
#define ARENA_X0 60.0f
#define ARENA_Y0 130.0f
#define ARENA_X1 1860.0f
#define ARENA_Y1 1030.0f
#define ARENA_CX ((ARENA_X0 + ARENA_X1) * 0.5f)
#define ARENA_CY ((ARENA_Y0 + ARENA_Y1) * 0.5f)
#define WALL_T 28.0f

#define ARRAY_LEN(a) ((int)(sizeof(a) / sizeof((a)[0])))

/* ---- 2D vectors ---------------------------------------------------------- */

typedef struct { float x, y; } V2;

static inline V2 v2(float x, float y) { V2 r = {x, y}; return r; }
static inline V2 v2_add(V2 a, V2 b) { return v2(a.x + b.x, a.y + b.y); }
static inline V2 v2_sub(V2 a, V2 b) { return v2(a.x - b.x, a.y - b.y); }
static inline V2 v2_scale(V2 a, float s) { return v2(a.x * s, a.y * s); }
static inline float v2_dot(V2 a, V2 b) { return a.x * b.x + a.y * b.y; }
static inline float v2_len2(V2 a) { return a.x * a.x + a.y * a.y; }
static inline float v2_len(V2 a) { return sqrtf(v2_len2(a)); }
static inline float v2_dist2(V2 a, V2 b) { return v2_len2(v2_sub(a, b)); }
static inline float v2_dist(V2 a, V2 b) { return v2_len(v2_sub(a, b)); }
static inline V2 v2_perp(V2 a) { return v2(-a.y, a.x); }
static inline V2 v2_angle(float a) { return v2(cosf(a), sinf(a)); }
static inline float v2_heading(V2 a) { return atan2f(a.y, a.x); }
static inline V2 v2_lerp(V2 a, V2 b, float t) { return v2(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t); }
static inline V2 v2_norm(V2 a) {
    float l = v2_len(a);
    return l > 1e-6f ? v2_scale(a, 1.0f / l) : v2(0.0f, 0.0f);
}
static inline V2 v2_rotate(V2 a, float ang) {
    float c = cosf(ang), s = sinf(ang);
    return v2(a.x * c - a.y * s, a.x * s + a.y * c);
}

static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
/* For hot loops: fminf / fmaxf are libm calls (the compiler may not inline them: NaN and signed-zero
 * rules). Same result for ordinary numbers. */
static inline float minf(float a, float b) { return a < b ? a : b; }
static inline float maxf(float a, float b) { return a > b ? a : b; }
static inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
static inline float angle_wrap(float a) {
    while (a > PI_F) a -= TAU_F;
    while (a < -PI_F) a += TAU_F;
    return a;
}
/* Frame-rate independent smoothing factor for a given responsiveness. */
static inline float smooth_k(float rate) { return 1.0f - expf(-rate * DT); }

/* ---- Colors -------------------------------------------------------------- */

static inline SDL_FColor rgba(float r, float g, float b, float a) {
    SDL_FColor c = {r, g, b, a};
    return c;
}
static inline SDL_FColor col_hex(uint32_t rgb, float a) {
    return rgba((float)((rgb >> 16) & 0xff) / 255.0f, (float)((rgb >> 8) & 0xff) / 255.0f,
                (float)(rgb & 0xff) / 255.0f, a);
}
static inline SDL_FColor col_a(SDL_FColor c, float a) { c.a = a; return c; }
static inline SDL_FColor col_mul(SDL_FColor c, float k) { c.r *= k; c.g *= k; c.b *= k; return c; }
static inline SDL_FColor col_lerp(SDL_FColor a, SDL_FColor b, float t) {
    return rgba(lerpf(a.r, b.r, t), lerpf(a.g, b.g, t), lerpf(a.b, b.b, t), lerpf(a.a, b.a, t));
}
static inline SDL_FColor col_hsv(float h, float s, float v, float a) {
    h = fmodf(h, 360.0f);
    if (h < 0.0f) h += 360.0f;
    float c = v * s, x = c * (1.0f - fabsf(fmodf(h / 60.0f, 2.0f) - 1.0f)), m = v - c;
    float r, g, b;
    if (h < 60.0f) { r = c; g = x; b = 0; }
    else if (h < 120.0f) { r = x; g = c; b = 0; }
    else if (h < 180.0f) { r = 0; g = c; b = x; }
    else if (h < 240.0f) { r = 0; g = x; b = c; }
    else if (h < 300.0f) { r = x; g = 0; b = c; }
    else { r = c; g = 0; b = x; }
    return rgba(r + m, g + m, b + m, a);
}

#define COL_WHITE rgba(1.0f, 1.0f, 1.0f, 1.0f)
#define COL_BLACK rgba(0.0f, 0.0f, 0.0f, 1.0f)

#endif
