#include "draw.h"
#include "game.h"

#include <stdarg.h>
#include <stdio.h>

/* Particles are only for show: they never touch the game's generator (their own, G.fx_rng), so a
 * run plays the same with or without them. Headless runs without (g_fx_off; --fx keeps them). */
bool g_fx_off;
static float fx_rand(float lo, float hi) { return rng_range(&G.fx_rng, lo, hi); }

void fx_particle(V2 pos, V2 vel, float life, float size, SDL_FColor col, float drag, int shape) {
    if (g_fx_off || G.npt >= MAX_PARTICLES) return;
    Particle *p = &G.pt[G.npt++];
    p->pos = pos;
    p->vel = vel;
    p->life = p->max_life = life;
    p->size = size;
    p->col = col;
    p->drag = drag;
    p->shape = (uint8_t)shape;
    p->rot = p->spin = 0.0f;
}

void fx_shard(V2 pos, V2 vel, float life, float size, SDL_FColor col, float spin) {
    if (g_fx_off || G.npt >= MAX_PARTICLES) return;
    fx_particle(pos, vel, life, size, col, 2.5f, PT_SHARD);
    if (G.npt > 0 && G.pt[G.npt - 1].shape == PT_SHARD) {
        G.pt[G.npt - 1].rot = fx_rand(0.0f, TAU_F);
        G.pt[G.npt - 1].spin = spin;
    }
}

void fx_burst(V2 pos, int n, float smin, float smax, float life, float size, SDL_FColor col) {
    if (g_fx_off) return;
    for (int i = 0; i < n; i++) {
        V2 v = v2_scale(v2_angle(fx_rand(0.0f, TAU_F)), fx_rand(smin, smax));
        fx_particle(pos, v, life * fx_rand(0.6f, 1.2f), size * fx_rand(0.7f, 1.3f), col, 4.0f, PT_SPARK);
    }
}

void fx_explosion(V2 pos, float radius, SDL_FColor col) {
    if (g_fx_off) return;
    int n = (int)(10.0f + radius * 1.2f);
    fx_burst(pos, n, 120.0f, 450.0f + radius * 9.0f, 0.45f, 2.5f + radius * 0.07f, col);
    fx_burst(pos, n / 3, 80.0f, 300.0f, 0.3f, 2.0f, COL_WHITE);
    for (int i = 0; i < n / 3; i++) {
        V2 v = v2_scale(v2_angle(fx_rand(0.0f, TAU_F)), fx_rand(20.0f, 160.0f));
        fx_particle(pos, v, fx_rand(0.3f, 0.7f), radius * fx_rand(0.5f, 1.0f), col_a(col, 0.5f), 3.0f, PT_GLOW);
    }
    fx_particle(pos, v2(0, 0), 0.16f, radius * 3.0f, col_a(COL_WHITE, 0.9f), 0.0f, PT_GLOW);
    fx_particle(pos, v2(0, 0), 0.32f, radius * 3.2f, col, 0.0f, PT_RING);
}

void fx_shake(float amount) { G.shake = fminf(1.0f, G.shake + amount); }

void fx_popup(V2 pos, SDL_FColor col, const char *fmt, ...) {
    if (G.npop >= MAX_POPUPS) return;
    Popup *p = &G.pop[G.npop++];
    p->pos = pos;
    p->t = 1.0f;
    p->col = col;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(p->text, sizeof p->text, fmt, ap);
    va_end(ap);
}

void fx_banner(const char *title, const char *sub, SDL_FColor col, float duration) {
    snprintf(G.banner, sizeof G.banner, "%s", title);
    snprintf(G.banner_sub, sizeof G.banner_sub, "%s", sub ? sub : "");
    G.banner_col = col;
    G.banner_t = G.banner_max = duration;
}

/* Timers that keep running even during hitstop / pause. */
void fx_timers(void) {
    G.shake = fmaxf(0.0f, G.shake - 1.8f * DT);
    G.hurt_flash = fmaxf(0.0f, G.hurt_flash - 2.2f * DT);
    G.bomb_flash = fmaxf(0.0f, G.bomb_flash - 2.5f * DT);
    if (G.banner_t > 0.0f) G.banner_t -= DT;
}

void fx_update(void) {
    for (int i = 0; i < G.npt; i++) {
        Particle *p = &G.pt[i];
        p->life -= DT;
        if (p->life <= 0.0f) {
            G.pt[i] = G.pt[--G.npt];
            i--;
            continue;
        }
        p->pos = v2_add(p->pos, v2_scale(p->vel, DT));
        p->vel = v2_scale(p->vel, maxf(0.0f, 1.0f - p->drag * DT));
        p->rot += p->spin * DT;
    }
    for (int i = 0; i < G.npop; i++) {
        Popup *p = &G.pop[i];
        p->t -= DT;
        p->pos.y -= 45.0f * DT;
        if (p->t <= 0.0f) {
            G.pop[i] = G.pop[--G.npop];
            i--;
        }
    }
}

void fx_draw(void) {
    draw_blend_add();
    for (int i = 0; i < G.npt; i++) {
        Particle *p = &G.pt[i];
        float k = p->life / p->max_life;
        switch (p->shape) {
        case PT_GLOW:
            draw_glow(p->pos, p->size * (0.4f + 0.6f * k), col_a(p->col, p->col.a * k));
            break;
        case PT_SPARK: {
            float spd = v2_len(p->vel);
            V2 dir = spd > 1e-3f ? v2_scale(p->vel, 1.0f / spd) : v2(1, 0);
            float len = fminf(spd * 0.035f, 40.0f) + p->size;
            draw_line(v2_sub(p->pos, v2_scale(dir, len)), p->pos, fmaxf(1.0f, p->size * k), col_a(p->col, p->col.a * k));
        } break;
        case PT_RING: {
            float r = p->size * (1.0f - k * k) + 4.0f;
            float thick = (2.0f + p->size * 0.03f) * k + 1.5f;
            draw_ring(p->pos, r, thick, col_a(p->col, p->col.a * k * 0.9f));
        } break;
        case PT_SHARD: { /* a thin glass triangle: glowing face, bright edge, fading to nothing */
            float a = p->col.a * k * k * (3.0f - 2.0f * k); /* smooth fade */
            V2 c[3];
            for (int v = 0; v < 3; v++) {
                float ang = p->rot + (float)v * 2.2f + (v == 2 ? 0.5f : 0.0f);
                float r = p->size * (v == 0 ? 1.0f : 0.55f);
                c[v] = v2_add(p->pos, v2_scale(v2_angle(ang), r));
            }
            draw_tri(c[0], c[1], c[2], col_a(p->col, 0.45f * a));
            draw_line(c[0], c[1], 1.5f, col_a(COL_WHITE, 0.8f * a));
            draw_line(c[1], c[2], 1.0f, col_a(p->col, 0.9f * a));
        } break;
        default:
            break;
        }
    }
    draw_blend_alpha();
}

void fx_draw_popups(void) {
    for (int i = 0; i < G.npop; i++) {
        Popup *p = &G.pop[i];
        draw_text_centered(p->pos.x, p->pos.y, 2.0f, col_a(p->col, clampf(p->t / 0.35f, 0.0f, 1.0f)), "%s", p->text);
    }
}

void fx_clear(void) {
    G.npt = 0;
    G.npop = 0;
}
