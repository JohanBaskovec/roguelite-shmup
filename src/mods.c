/*
 * Room modifiers: a combat room with one more rule, rolled with its door (gen_doors) and shown under the door's
 * reward ("+ HORDE"). The room keeps it in Room.mod (G.entry_mod carries it through the transition).
 *   COMPACTOR  the walls close in slowly from the first wave on (crush.c, crush_slow), fast once the last wave's
 *              timer runs out
 *   BLACKOUT   dark: a circle of light round the ship; the shots and the enemies' glow show through (drawing only)
 *   POWDER     packed with barrels and oil drums (blocks.c: theme_kind, the block budget)
 *   HORDE      a single wave, HORDE_BUDGET times bigger, pouring in; its timer is the last wave's (then the walls)
 *   ELITE      every wave of a single enemy type (Room.elite, spawn_wave)
 *   STORM      lightning strikes the floor on spots shown STORM_WARN before: hurts the ship, the enemies, sets
 *              blocks alight
 *   WAGER      the door costs a whole heart (WAGER_COST hp, never the last one); the reward comes twice (two
 *              one-of-N groups for an upgrade / active room, twice the loot otherwise)
 * Never in a boss room, a stats room, a shop, a workshop or a secret room.
 */
#include "audio.h"
#include "draw.h"
#include "game.h"

#include <stdio.h>

int g_mod_override = -1;

static const struct {
    const char *name, *desc;
    uint32_t color;
    int min_depth; /* first room it can be met in */
} MODS[MOD_COUNT] = {
    [MOD_NONE] = {"NONE", "", 0xffffff, 999},
    [MOD_COMPACTOR] = {"COMPACTOR", "the walls are moving", 0xff3b5c, 3},
    [MOD_BLACKOUT] = {"BLACKOUT", "the lights are out", 0x8090ff, 2},
    [MOD_POWDER] = {"POWDER KEG", "mind the barrels", 0xff8a2a, 2},
    [MOD_HORDE] = {"HORDE", "all of them at once", 0xff5f7e, 4},
    [MOD_ELITE] = {"ELITE", "only one kind", 0xffd23f, 3},
    [MOD_STORM] = {"STORM", "lightning strikes", 0x8fd8ff, 4},
    [MOD_WAGER] = {"WAGER", "one heart in, double reward", 0xff4d8a, 2},
};

#define MOD_CHANCE 0.3f /* a combat door with a modifier */

const char *mod_name(int mod) { return mod > MOD_NONE && mod < MOD_COUNT ? MODS[mod].name : ""; }
const char *mod_desc(int mod) { return mod > MOD_NONE && mod < MOD_COUNT ? MODS[mod].desc : ""; }
uint32_t mod_color(int mod) { return mod > MOD_NONE && mod < MOD_COUNT ? MODS[mod].color : 0xffffff; }

int mod_by_name(const char *name) {
    for (int m = MOD_NONE + 1; m < MOD_COUNT; m++) {
        const char *a = MODS[m].name, *b = name;
        bool same = true;
        for (; *a && *b && same; a++, b++) {
            char ca = *a == ' ' ? '-' : (char)(*a | 0x20), cb = *b == ' ' ? '-' : (char)(*b | 0x20);
            same = ca == cb;
        }
        if (same && !*a && !*b) return m;
    }
    return -1;
}

RoomMod mod_roll(int depth) {
    if (!chance(MOD_CHANCE)) return MOD_NONE;
    int cand[MOD_COUNT], n = 0;
    for (int m = MOD_NONE + 1; m < MOD_COUNT; m++)
        if (MODS[m].min_depth <= depth) cand[n++] = m;
    return n ? (RoomMod)cand[irand(0, n - 1)] : MOD_NONE;
}

/* ELITE: a type that can fill a wave on its own at this depth (or one of the --enemy ones). */
static int elite_type(int depth) {
    int cand[EN_COUNT], n = 0;
    for (int t = 0; t < EN_COUNT; t++) {
        bool ok = g_only_count ? g_only_enemies[t] : ENEMIES[t].min_depth <= depth && ENEMIES[t].cost <= 4;
        if (ok && !type_is_boss(t)) cand[n++] = t;
    }
    return n ? cand[irand(0, n - 1)] : EN_CHASER;
}

void mods_on_room_enter(void) {
    Room *r = &G.room;
    if (r->mod == MOD_NONE || r->type != ROOM_COMBAT) {
        r->mod = MOD_NONE;
        return;
    }
    char sub[64];
    snprintf(sub, sizeof sub, "%s", MODS[r->mod].desc);
    switch (r->mod) {
    case MOD_HORDE: r->nwaves = 1; break;
    case MOD_ELITE: {
        int t = elite_type(r->depth);
        r->elite = t + 1;
        char name[32];
        snprintf(name, sizeof name, "%s", ENEMIES[t].name);
        for (char *c = name; *c; c++) *c = (char)(*c >= 'a' && *c <= 'z' ? *c - 32 : *c);
        snprintf(sub, sizeof sub, "only %ss", name);
    } break;
    case MOD_STORM: r->storm_cd = 2.0f; break;
    default: break;
    }
    fx_banner(MODS[r->mod].name, sub, col_hex(MODS[r->mod].color, 1), 1.6f);
}

/* ---- Wager --------------------------------------------------------------- */

bool mod_door_pay(const Door *d) {
    Player *pl = &G.pl;
    if (d->mod != MOD_WAGER) return true;
    V2 at = v2_add(d->pos, v2_scale(v2_sub(v2(ARENA_CX, ARENA_CY), d->pos), 90.0f / v2_dist(v2(ARENA_CX, ARENA_CY), d->pos)));
    if (pl->hp <= WAGER_COST) { /* never the last heart */
        if (G.room.msg_cd <= 0.0f) {
            fx_popup(at, col_hex(0xff5f7e, 1), "NEED MORE THAN 1 HEART");
            sfx_play_at(SFX_DENIED, d->pos);
            G.room.msg_cd = 1.0f;
        }
        return false;
    }
    pl->hp -= WAGER_COST;
    fx_popup(v2_add(pl->pos, v2(0, -40)), col_hex(MODS[MOD_WAGER].color, 1), "-1 HEART: DOUBLE REWARD");
    fx_burst(pl->pos, 24, 80.0f, 320.0f, 0.5f, 3.0f, col_hex(MODS[MOD_WAGER].color, 1));
    sfx_play(SFX_PLAYER_HURT);
    return true;
}

/* ---- Storm --------------------------------------------------------------- */

static void strike_lands(V2 p) {
    Room *r = &G.room;
    for (int i = 0; i < G.nen; i++) {
        Enemy *e = &G.en[i];
        if (e->hp > 0.0f && !enemy_is_boss(e) && v2_dist(e->pos, p) < STORM_R + e->radius) enemy_damage(e, STORM_ENEMY_DMG, v2(0, 0));
    }
    for (int i = 0; i < r->nobs; i++) { /* lightning sets the blocks it hits alight */
        const SDL_FRect *o = &r->obs[i].r;
        float cx = clampf(p.x, o->x, o->x + o->w), cy = clampf(p.y, o->y, o->y + o->h);
        if (v2_dist(p, v2(cx, cy)) < STORM_R * 0.6f) room_block_ignite(i);
    }
    fx_burst(p, 26, 150.0f, 560.0f, 0.35f, 3.0f, col_hex(0xdff4ff, 1));
    fx_particle(p, v2(0, 0), 0.3f, STORM_R * 2.4f, col_hex(0x8fd8ff, 0.9f), 0.0f, PT_RING);
    fx_particle(p, v2(0, 0), 0.18f, STORM_R * 2.0f, col_hex(0xffffff, 0.8f), 0.0f, PT_GLOW);
    sfx_play_at(SFX_SHOT_RAIL, p);
    fx_shake(0.12f);
}

void mods_update(void) {
    Room *r = &G.room;
    if (r->mod != MOD_STORM || r->cleared || r->wave < 1 || active_time_stop_on()) return; /* TIME STOP holds the sky */
    Player *pl = &G.pl;
    int w = 0;
    for (int i = 0; i < r->nstrikes; i++) {
        float prev = r->strike_t[i];
        r->strike_t[i] -= DT;
        if (prev > 0.0f && r->strike_t[i] <= 0.0f) strike_lands(r->strike_pos[i]);
        if (r->strike_t[i] <= 0.0f && v2_dist(pl->pos, r->strike_pos[i]) < STORM_R + pl->hit_radius) player_hurt(1);
        if (r->strike_t[i] > -STORM_LIVE) {
            r->strike_pos[w] = r->strike_pos[i];
            r->strike_t[w++] = r->strike_t[i];
        }
    }
    r->nstrikes = w;
    if ((r->storm_cd -= DT) > 0.0f || r->nstrikes >= (int)ARRAY_LEN(r->strike_pos)) return;
    r->storm_cd = frand(0.55f, 1.15f);
    V2 p;
    if (chance(0.5f)) p = v2_add(pl->pos, v2_scale(v2_angle(frand(0.0f, TAU_F)), frand(0.0f, 150.0f))); /* round the ship */
    else p = v2(frand(room_x0() + 40.0f, room_x1() - 40.0f), frand(room_y0() + 40.0f, room_y1() - 40.0f));
    p.x = clampf(p.x, room_x0() + 30.0f, room_x1() - 30.0f);
    p.y = clampf(p.y, room_y0() + 30.0f, room_y1() - 30.0f);
    r->strike_pos[r->nstrikes] = p;
    r->strike_t[r->nstrikes++] = STORM_WARN;
}

int mods_hazards(BossHazard *out, int max) {
    const Room *r = &G.room;
    int n = 0;
    if (r->mod != MOD_STORM || r->cleared) return 0;
    for (int i = 0; i < r->nstrikes && n < max; i++)
        out[n++] = (BossHazard){r->strike_pos[i], r->strike_pos[i], STORM_R, r->strike_t[i] <= 0.0f};
    return n;
}

void mods_draw_under(void) {
    const Room *r = &G.room;
    if (r->mod != MOD_STORM) return;
    draw_blend_add();
    for (int i = 0; i < r->nstrikes; i++) {
        V2 p = r->strike_pos[i];
        float t = r->strike_t[i];
        if (t > 0.0f) { /* the warning: a ring closing on the spot, brighter as it comes */
            float k = 1.0f - t / STORM_WARN;
            draw_circle(p, STORM_R, col_hex(0x8fd8ff, 0.06f + 0.16f * k));
            draw_ring(p, STORM_R, 2.0f, col_hex(0x8fd8ff, 0.4f + 0.5f * k));
            draw_ring(p, STORM_R * (1.0f - 0.85f * k) + 8.0f, 3.0f, col_hex(0xdff4ff, 0.3f + 0.6f * k));
            draw_line(v2(p.x - 14.0f, p.y), v2(p.x + 14.0f, p.y), 2.0f, col_hex(0xdff4ff, 0.6f * k));
            draw_line(v2(p.x, p.y - 14.0f), v2(p.x, p.y + 14.0f), 2.0f, col_hex(0xdff4ff, 0.6f * k));
        } else { /* the bolt: a zigzag down from the top of the room */
            V2 a = v2(p.x + 30.0f * sinf(p.x), ARENA_Y0);
            unsigned seed = (unsigned)(p.x * 7.0f + p.y * 13.0f);
            for (int k = 1; k <= 8; k++) {
                float f = (float)k / 8.0f;
                seed = seed * 1103515245u + 12345u;
                float j = k == 8 ? 0.0f : ((float)((seed >> 16) % 61) - 30.0f);
                V2 b = v2(a.x + (p.x - a.x) * (1.0f / (float)(9 - k)) + j, ARENA_Y0 + (p.y - ARENA_Y0) * f);
                draw_line(a, b, 7.0f, col_hex(0x8fd8ff, 0.5f));
                draw_line(a, b, 3.0f, col_hex(0xffffff, 0.95f));
                a = b;
            }
            draw_glow(p, STORM_R * 1.4f, col_hex(0xdff4ff, 0.6f));
        }
    }
    draw_blend_alpha();
}

/* ---- Blackout ------------------------------------------------------------ */

#define LIGHT_R 210.0f  /* clear round the ship... */
#define LIGHT_FADE 140.0f /* ...fading to dark over that */
#define DARK 0.94f

void mods_draw_over(void) {
    const Room *r = &G.room;
    if (r->mod != MOD_BLACKOUT) return;
    float a = r->cleared ? clampf(1.0f - (r->clear_t + ROOM_REVEAL_T) / 0.8f, 0.0f, 1.0f) : 1.0f; /* light back on */
    if (a <= 0.0f) return;
    V2 c = G.pl.pos;
    const int N = 48;
    const float far = 4000.0f, r0 = LIGHT_R, r1 = LIGHT_R + LIGHT_FADE;
    SDL_FColor clear = rgba(0.0f, 0.0f, 0.02f, 0.0f), dark = rgba(0.0f, 0.0f, 0.02f, DARK * a);
    for (int i = 0; i < N; i++) {
        V2 u0 = v2_angle(TAU_F * (float)i / (float)N), u1 = v2_angle(TAU_F * (float)(i + 1) / (float)N);
        V2 a0 = v2_add(c, v2_scale(u0, r0)), a1 = v2_add(c, v2_scale(u1, r0));
        V2 b0 = v2_add(c, v2_scale(u0, r1)), b1 = v2_add(c, v2_scale(u1, r1));
        V2 f0 = v2_add(c, v2_scale(u0, far)), f1 = v2_add(c, v2_scale(u1, far));
        draw_quad_grad(a0, a1, b1, b0, clear, clear, dark, dark);
        draw_quad(b0, b1, f1, f0, dark);
    }
    /* What gives itself away in the dark: the enemies' glow, every shot, the closing walls' edge. */
    draw_blend_add();
    for (int i = 0; i < G.nen; i++) {
        const Enemy *e = &G.en[i];
        if (e->hp <= 0.0f || v2_dist(e->pos, c) < r0) continue;
        SDL_FColor col = col_hex(ENEMIES[e->type].color, 0.35f * a);
        draw_glow(e->pos, e->radius * 1.8f, col);
        draw_circle(e->pos, 3.0f, col_hex(ENEMIES[e->type].color, 0.8f * a));
    }
    draw_blend_alpha();
    bullets_draw_player();
    bullets_draw_enemy();
    if (r->crushing) crush_draw();
}
