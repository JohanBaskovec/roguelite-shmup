#include "draw.h"
#include "game.h"
#include "audio.h"

void pickup_spawn(PickupType type, int sub, V2 pos, V2 vel) {
    if (G.npk >= MAX_PICKUPS) return;
    Pickup *p = &G.pk[G.npk++];
    p->type = type;
    p->sub = sub;
    p->pos = pos;
    p->vel = vel;
    p->t = 0.0f;
}

static bool is_pedestal(PickupType t) { return t == PK_WEAPON || t == PK_UPGRADE; }

PickupType reward_icon(RewardType r) {
    switch (r) {
    case RW_KEY: return PK_KEY;
    case RW_BOMBS: return PK_BOMB;
    case RW_HEART: return PK_HEART;
    case RW_WEAPON: return PK_WEAPON;
    case RW_UPGRADE: return PK_UPGRADE;
    default: return PK_COIN;
    }
}

const char *reward_name(RewardType r) {
    switch (r) {
    case RW_COINS: return "COINS";
    case RW_KEY: return "KEY";
    case RW_BOMBS: return "BOMBS";
    case RW_HEART: return "HEARTS";
    case RW_WEAPON: return "WEAPON";
    case RW_UPGRADE: return "UPGRADE";
    default: return "";
    }
}

static V2 random_vel(float lo, float hi) { return v2_scale(v2_angle(frand(0.0f, TAU_F)), frand(lo, hi)); }

void reward_drop(RewardType r, V2 pos) {
    switch (r) {
    case RW_COINS: {
        int n = irand(6, 10);
        for (int i = 0; i < n; i++) pickup_spawn(PK_COIN, 1, pos, random_vel(100.0f, 450.0f));
    } break;
    case RW_KEY: pickup_spawn(PK_KEY, 0, pos, random_vel(50.0f, 120.0f)); break;
    case RW_BOMBS:
        for (int i = 0; i < 2; i++) pickup_spawn(PK_BOMB, 0, pos, random_vel(100.0f, 250.0f));
        break;
    case RW_HEART:
        for (int i = 0; i < 2; i++) pickup_spawn(PK_HEART, 0, pos, random_vel(100.0f, 250.0f));
        break;
    case RW_WEAPON:
    case RW_UPGRADE: {
        int w = r == RW_WEAPON ? weapon_roll() : -1;
        if (w >= 0) pickup_spawn(PK_WEAPON, w, pos, v2(0, 0));
        else pickup_spawn(PK_UPGRADE, upgrade_roll(), pos, v2(0, 0));
    } break;
    default:
        return;
    }
    fx_particle(pos, v2(0, 0), 0.5f, 220.0f, col_hex(0xffffff, 0.8f), 0.0f, PT_RING);
    fx_burst(pos, 30, 100.0f, 500.0f, 0.5f, 3.0f, col_hex(0xffe066, 1));
}

static bool collect(const Pickup *k) {
    Player *p = &G.pl;
    V2 pop = v2_add(p->pos, v2(0, -36));
    switch (k->type) {
    case PK_COIN: {
        /* Coins collected in quick succession climb in pitch. */
        static float last_coin_t = -10.0f;
        static int combo = 0;
        combo = G.time - last_coin_t < 0.3f ? (combo < 12 ? combo + 1 : 12) : 0;
        last_coin_t = G.time;
        sfx_play_ex(SFX_COIN, 1.0f, 1.0f + 0.06f * (float)combo, 0.0f);
        p->coins += k->sub;
        fx_burst(k->pos, 5, 60.0f, 200.0f, 0.2f, 2.0f, col_hex(0xffd23f, 1));
    } break;
    case PK_KEY:
        p->keys++;
        sfx_play(SFX_KEY);
        fx_popup(pop, col_hex(0xd9e3f0, 1), "+1 KEY");
        break;
    case PK_BOMB:
        p->bombs++;
        sfx_play(SFX_BOMB_PICKUP);
        fx_popup(pop, col_hex(0xff8a3d, 1), "+1 BOMB");
        break;
    case PK_HEART:
        if (p->hp >= p->max_hp) return false;
        p->hp = p->hp + 2 > p->max_hp ? p->max_hp : p->hp + 2;
        sfx_play(SFX_HEART);
        fx_popup(pop, col_hex(0xff3355, 1), "+HP");
        break;
    case PK_WEAPON:
        weapon_give(k->sub);
        fx_shake(0.3f);
        break;
    case PK_UPGRADE:
        upgrade_apply(k->sub);
        fx_shake(0.3f);
        break;
    }
    if (is_pedestal(k->type)) {
        sfx_play(SFX_ITEM);
        fx_particle(k->pos, v2(0, 0), 0.5f, 300.0f, COL_WHITE, 0.0f, PT_RING);
        fx_burst(k->pos, 40, 200.0f, 700.0f, 0.5f, 3.0f, COL_WHITE);
    }
    return true;
}

void pickups_update(void) {
    const Player *pl = &G.pl;
    /* Once the room is cleared, resources fly to the player: no backtracking chores. */
    bool vacuum = G.room.cleared && G.room.clear_t > 0.6f;
    for (int i = 0; i < G.npk; i++) {
        Pickup *k = &G.pk[i];
        k->t += DT;
        V2 d = v2_sub(pl->pos, k->pos);
        float dist = v2_len(d);
        bool taken = false;

        if (is_pedestal(k->type)) {
            if (k->t > 0.5f && dist < 40.0f) taken = collect(k);
        } else {
            bool wants = !(k->type == PK_HEART && pl->hp >= pl->max_hp);
            float radius = vacuum ? 1e9f : pl->magnet;
            if (wants && k->t > 0.35f && dist < radius) {
                V2 dir = v2_scale(d, 1.0f / fmaxf(dist, 1.0f));
                k->vel = v2_lerp(k->vel, v2_scale(dir, vacuum ? 1400.0f : 1000.0f), smooth_k(8.0f));
            } else {
                k->vel = v2_scale(k->vel, fmaxf(0.0f, 1.0f - 3.5f * DT));
            }
            k->pos = v2_add(k->pos, v2_scale(k->vel, DT));
            V2 n;
            /* Vacuumed pickups fly over obstacles so nothing stays stuck behind a pillar. */
            if (!vacuum && room_collide_circle(&k->pos, 10.0f, &n)) {
                float into = v2_dot(k->vel, n);
                if (into < 0.0f) k->vel = v2_scale(v2_sub(k->vel, v2_scale(n, 2.0f * into)), 0.5f);
            }
            if (wants && k->t > 0.2f && dist < PLAYER_BODY_R + 14.0f) taken = collect(k);
        }

        if (taken) {
            G.pk[i] = G.pk[--G.npk];
            i--;
        }
    }
}

/* ---- Icons --------------------------------------------------------------- */

void draw_heart(V2 p, float s, SDL_FColor col, bool left_half_only) {
    float r = s * 0.55f;
    draw_circle(v2(p.x - s * 0.5f, p.y - s * 0.25f), r, col);
    if (!left_half_only) draw_circle(v2(p.x + s * 0.5f, p.y - s * 0.25f), r, col);
    V2 l = v2(p.x - s * 1.03f, p.y - s * 0.1f);
    V2 rr = v2(p.x + s * 1.03f, p.y - s * 0.1f);
    V2 m = v2(p.x, p.y - s * 0.1f);
    V2 b = v2(p.x, p.y + s * 1.05f);
    if (left_half_only) draw_tri(l, m, b, col);
    else draw_tri(l, rr, b, col);
}

void draw_pickup_icon(PickupType type, int sub, V2 p, float s, float t) {
    switch (type) {
    case PK_COIN:
        draw_circle(p, s, col_hex(0xffd23f, 1));
        draw_circle(p, s * 0.68f, col_hex(0xe0a000, 1));
        draw_rect(p.x - s * 0.14f, p.y - s * 0.42f, s * 0.28f, s * 0.84f, col_hex(0xfff2a8, 1));
        break;
    case PK_KEY: {
        SDL_FColor c = col_hex(0xd9e3f0, 1);
        draw_ring(v2(p.x - s * 0.5f, p.y), s * 0.42f, s * 0.26f, c);
        draw_line(v2(p.x - s * 0.15f, p.y), v2(p.x + s * 1.0f, p.y), s * 0.26f, c);
        draw_rect(p.x + s * 0.55f, p.y, s * 0.18f, s * 0.45f, c);
        draw_rect(p.x + s * 0.85f, p.y, s * 0.18f, s * 0.35f, c);
    } break;
    case PK_BOMB: {
        V2 c = v2(p.x, p.y + s * 0.15f);
        draw_line(v2(c.x + s * 0.3f, c.y - s * 0.7f), v2(c.x + s * 0.6f, c.y - s * 1.1f), s * 0.18f, col_hex(0xb0a080, 1));
        draw_circle(c, s * 0.85f, col_hex(0x3a3f4b, 1));
        draw_circle(v2(c.x - s * 0.3f, c.y - s * 0.3f), s * 0.22f, col_hex(0x8890a0, 1));
        float f = 0.6f + 0.4f * sinf(t * 25.0f);
        draw_circle(v2(c.x + s * 0.6f, c.y - s * 1.1f), s * 0.22f * f + 1.0f, col_hex(0xffb030, 1));
    } break;
    case PK_HEART:
        draw_heart(p, s * 0.9f, col_hex(0xff3355, 1), false);
        break;
    case PK_WEAPON: {
        SDL_FColor c = col_hex(sub >= 0 ? WEAPONS[sub].color : 0x66f0ff, 1);
        draw_ngon_outline(p, s * 1.05f, 6, t * 1.5f, s * 0.2f, c);
        draw_tri(v2(p.x, p.y - s * 0.7f), v2(p.x + s * 0.55f, p.y + s * 0.5f), v2(p.x - s * 0.55f, p.y + s * 0.5f), c);
    } break;
    case PK_UPGRADE: {
        SDL_FColor c = col_hex(sub >= 0 ? UPGRADES[sub].color : 0xb57cff, 1);
        draw_ngon(p, s, 4, t * 2.0f, c);
        draw_ngon(p, s, 4, t * 2.0f + PI_F * 0.25f, c);
        draw_circle(p, s * 0.4f, COL_WHITE);
    } break;
    }
}

static SDL_FColor pickup_glow_color(const Pickup *k) {
    switch (k->type) {
    case PK_COIN: return col_hex(0xffd23f, 1);
    case PK_KEY: return col_hex(0xd9e3f0, 1);
    case PK_BOMB: return col_hex(0xff8a3d, 1);
    case PK_HEART: return col_hex(0xff3355, 1);
    case PK_WEAPON: return col_hex(WEAPONS[k->sub].color, 1);
    case PK_UPGRADE: return col_hex(UPGRADES[k->sub].color, 1);
    }
    return COL_WHITE;
}

void pickups_draw(void) {
    draw_blend_add();
    for (int i = 0; i < G.npk; i++) {
        const Pickup *k = &G.pk[i];
        SDL_FColor c = pickup_glow_color(k);
        if (is_pedestal(k->type)) {
            float pulse = 0.5f + 0.5f * sinf(G.time * 4.0f);
            draw_glow(k->pos, 90.0f, col_a(c, 0.25f + 0.1f * pulse));
            draw_quad_grad(v2(k->pos.x - 20, k->pos.y + 10), v2(k->pos.x + 20, k->pos.y + 10),
                           v2(k->pos.x + 8, k->pos.y - 160), v2(k->pos.x - 8, k->pos.y - 160),
                           col_a(c, 0.35f), col_a(c, 0.35f), col_a(c, 0.0f), col_a(c, 0.0f));
        } else {
            draw_glow(k->pos, 26.0f, col_a(c, 0.35f));
        }
    }
    draw_blend_alpha();

    for (int i = 0; i < G.npk; i++) {
        const Pickup *k = &G.pk[i];
        if (is_pedestal(k->type)) {
            V2 base = v2(k->pos.x, k->pos.y + 20.0f);
            draw_ngon(base, 28.0f, 6, 0.0f, col_hex(0x14182a, 1));
            draw_ngon_outline(base, 28.0f, 6, 0.0f, 3.0f, pickup_glow_color(k));
            draw_pickup_icon(k->type, k->sub, v2(k->pos.x, k->pos.y - 10.0f + sinf(G.time * 3.0f) * 5.0f), 16.0f, G.time);
        } else {
            draw_pickup_icon(k->type, k->sub, k->pos, 9.0f, G.time + (float)i);
        }
    }

    for (int i = 0; i < G.npk; i++) {
        const Pickup *k = &G.pk[i];
        if (!is_pedestal(k->type)) continue;
        const char *name = k->type == PK_WEAPON ? WEAPONS[k->sub].name : UPGRADES[k->sub].name;
        draw_text_centered(k->pos.x, k->pos.y + 56.0f, 2.0f, pickup_glow_color(k), "%s", name);
        if (k->type == PK_WEAPON && G.pl.wlevel[k->sub] > 0)
            draw_text_centered(k->pos.x, k->pos.y + 80.0f, 1.5f, col_a(COL_WHITE, 0.7f), "LEVEL UP");
    }
}
