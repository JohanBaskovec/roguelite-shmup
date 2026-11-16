#include "draw.h"
#include "game.h"
#include "audio.h"

#include <stdio.h>

void pickup_spawn(PickupType type, int sub, V2 pos, V2 vel) {
    if (G.npk >= MAX_PICKUPS) return;
    Pickup *p = &G.pk[G.npk++];
    p->type = type;
    p->sub = sub;
    p->price = 0;
    p->choice = false;
    p->pos = pos;
    p->vel = vel;
    p->t = 0.0f;
}

static SDL_FColor pickup_glow_color(const Pickup *k);

static bool is_pedestal(PickupType t) { return t == PK_UPGRADE || t == PK_STAT || t == PK_AUGMENT || t == PK_ACTIVE; }
/* Items for sale stand on a pedestal too (the shop's hearts, bombs and keys included). */
static bool on_pedestal(const Pickup *k) { return is_pedestal(k->type) || k->price > 0; }

PickupType reward_icon(RewardType r) {
    switch (r) {
    case RW_KEY: return PK_KEY;
    case RW_BOMBS: return PK_BOMB;
    case RW_HEART: return PK_HEART;
    case RW_UPGRADE: return PK_UPGRADE;
    case RW_ACTIVE: return PK_ACTIVE;
    default: return PK_COIN;
    }
}

const char *reward_name(RewardType r) {
    switch (r) {
    case RW_COINS: return "COINS";
    case RW_KEY: return "KEY";
    case RW_BOMBS: return "BOMBS";
    case RW_HEART: return "HEARTS";
    case RW_UPGRADE: return "UPGRADE";
    case RW_ACTIVE: return "ACTIVE";
    default: return "";
    }
}

static V2 random_vel(float lo, float hi) { return v2_scale(v2_angle(frand(0.0f, TAU_F)), frand(lo, hi)); }

const char *door_label(const Door *d) {
    if (d->type == ROOM_SECRET) return G.room.type == ROOM_SECRET ? "EXIT" : "SECRET";
    if (d->type == ROOM_WORKSHOP) return "WORKSHOP";
    if (d->type == ROOM_BOSS) return "BOSS";
    return d->type == ROOM_STATS ? "STATS" : d->type == ROOM_SHOP ? "SHOP" : reward_name(d->reward);
}

int shop_price(PickupType type, int sub) {
    (void)sub;
    int price;
    switch (type) {
    case PK_HEART: price = 6; break; /* supplies: flat prices */
    case PK_BOMB: price = 5; break;
    case PK_KEY: price = 6; break;
    default: price = (type == PK_UPGRADE ? 14 : 8) + G.room.depth / 4; break;
    }
    if (upg_has(UPG_COUPON)) price = (int)roundf((float)price * 0.7f);
    price = (int)roundf((float)price * (1.0f + malus_pct(MAL_PRICES)));
    return price > 1 ? price : 1;
}

/* Luck nudges the amount of resources a room reward gives. */
static int luck_adjust(int n) {
    int luck = G.pl.luck;
    if (luck > 0 && chance(0.15f * (float)luck)) n++;
    if (luck < 0 && n > 1 && chance(-0.15f * (float)luck)) n--;
    return n;
}

void pickup_spawn_choice(PickupType type, int sub, V2 pos) {
    int before = G.npk;
    pickup_spawn(type, sub, pos, v2(0, 0));
    if (G.npk > before) G.pk[G.npk - 1].choice = true;
}

static bool item_present(PickupType type, int sub) {
    for (int i = 0; i < G.npk; i++)
        if (G.pk[i].type == type && G.pk[i].sub == sub) return true;
    return false;
}

static bool on_offer(int u) {
    if (item_present(PK_UPGRADE, u)) return true;
    for (int i = 0; i < G.nmc; i++) /* the slot machine's prize */
        if (G.mc[i].type == MC_GAMBLE && !G.mc[i].broken && G.mc[i].prize == PK_UPGRADE && G.mc[i].prize_sub == u) return true;
    return false;
}

int upgrade_fresh(void) {
    int cand[UPG_COUNT], n = 0;
    for (int u = 0; u < UPG_COUNT; u++)
        if (UPGRADES[u].tier == 0 && upgrade_unlocked(u) && !upgrade_capped(u) && !on_offer(u)) cand[n++] = u;
    return n ? cand[irand(0, n - 1)] : -1;
}

/* Every upgrade owned: a stat trade instead (not one already on the floor), -1 if none. */
static int fresh_stat_offer(void) {
    int offers[ST_COUNT];
    int n = stats_roll_offers(offers, ST_COUNT);
    for (int i = 0; i < n; i++)
        if (!item_present(PK_STAT, offers[i])) return offers[i];
    return n > 0 ? offers[0] : -1;
}

/* Upgrade rooms: REWARD_CHOICES distinct upgrades in a row, take one (stat trades once every
 * upgrade is owned). */
static void spawn_item_choices(V2 center) {
    for (int i = 0; i < REWARD_CHOICES; i++) {
        PickupType type = PK_UPGRADE;
        int sub = upgrade_fresh();
        if (sub < 0) {
            type = PK_STAT;
            sub = fresh_stat_offer();
            if (sub < 0) continue;
        }
        V2 p = v2(center.x + ((float)i - (float)(REWARD_CHOICES - 1) * 0.5f) * 420.0f, center.y - 20.0f);
        pickup_spawn_choice(type, sub, p);
        uint32_t c = type == PK_STAT ? COLOR_STATS : 0xb57cff;
        fx_particle(p, v2(0, 0), 0.5f, 160.0f, col_hex(c, 1), 0.0f, PT_RING);
    }
}

void pickups_refresh_owned(void) {
    for (int i = 0; i < G.npk; i++) /* prices follow COUPON / the PRICES malus being turned on or off */
        if (G.pk[i].price > 0) G.pk[i].price = shop_price(G.pk[i].type, G.pk[i].sub);
    for (int i = 0; i < G.npk; i++) {
        Pickup *k = &G.pk[i];
        if (k->type != PK_UPGRADE || G.pl.upg_count[k->sub] == 0) continue;
        int sub = upgrade_fresh();
        if (sub < 0) {
            int offer = fresh_stat_offer();
            if (offer < 0) continue;
            k->type = PK_STAT;
            sub = offer;
        }
        k->sub = sub;
        if (k->price > 0) k->price = shop_price(k->type, k->sub);
        fx_particle(k->pos, v2(0, 0), 0.4f, 140.0f, COL_WHITE, 0.0f, PT_RING);
    }
    workshop_refresh_prize();
}

void secret_reward_drop(V2 center, bool items) {
    /* Items: free pedestals, all of them can be taken (not a one-of-N choice). */
    int n = !items ? 0 : chance(0.12f) ? 2 : 1;
    for (int i = 0; i < n; i++) {
        PickupType type = PK_UPGRADE;
        int sub = upgrade_fresh();
        if (sub < 0) { /* every upgrade owned */
            type = PK_STAT;
            sub = fresh_stat_offer();
            if (sub < 0) continue;
        }
        V2 p = v2(center.x + ((float)i - (float)(n - 1) * 0.5f) * 360.0f, center.y - 40.0f);
        pickup_spawn(type, sub, p, v2(0, 0));
        fx_particle(p, v2(0, 0), 0.5f, 160.0f, col_hex(COLOR_SECRET, 1), 0.0f, PT_RING);
    }
    /* Resources: always coins, often bombs, sometimes keys. */
    V2 pile = v2(center.x, center.y + 140.0f);
    int coins = (int)roundf((float)irand(5, 10) * luck_factor());
    for (int i = 0; i < coins; i++) pickup_spawn(PK_COIN, 1, pile, random_vel(100.0f, 400.0f));
    int bombs = chance(0.6f) ? luck_adjust(irand(1, 2)) : 0;
    for (int i = 0; i < bombs; i++) pickup_spawn(PK_BOMB, 0, pile, random_vel(100.0f, 250.0f));
    int keys = chance(0.4f) ? luck_adjust(1) : 0;
    for (int i = 0; i < keys; i++) pickup_spawn(PK_KEY, 0, pile, random_vel(50.0f, 200.0f));
    fx_burst(center, 40, 100.0f, 500.0f, 0.5f, 3.0f, col_hex(COLOR_SECRET, 1));
}

void reward_drop(RewardType r, V2 pos) {
    switch (r) {
    case RW_COINS: {
        /* Coins only come from rewards, so a coin room pays roughly one shop item. */
        float base = (float)irand(13, 18) + (float)G.room.depth * 0.55f;
        if (upg_has(UPG_MAGNET)) base *= 1.5f; /* COIN MAGNET */
        int n = (int)roundf(base * luck_factor());
        for (int i = 0; i < n; i++) pickup_spawn(PK_COIN, 1, pos, random_vel(100.0f, 450.0f));
    } break;
    case RW_KEY: {
        /* Usually one key, rarely two, even more rarely three. */
        float roll = frand(0.0f, 1.0f);
        int n = luck_adjust(roll < KEY_REWARD_3_CHANCE ? 3 : roll < KEY_REWARD_3_CHANCE + KEY_REWARD_2_CHANCE ? 2 : 1);
        for (int i = 0; i < n; i++) pickup_spawn(PK_KEY, 0, pos, random_vel(50.0f, 160.0f));
    } break;
    case RW_BOMBS: {
        int n = luck_adjust(2);
        for (int i = 0; i < n; i++) pickup_spawn(PK_BOMB, 0, pos, random_vel(100.0f, 250.0f));
    } break;
    case RW_HEART: {
        int n = luck_adjust(2) - malus(MAL_HEAL); /* HEAL malus: fewer hearts, at least one */
        if (n < 1) n = 1;
        for (int i = 0; i < n; i++) pickup_spawn(PK_HEART, 0, pos, random_vel(100.0f, 250.0f));
    } break;
    case RW_UPGRADE: spawn_item_choices(pos); break;
    case RW_ACTIVE: active_spawn_choices(pos); break;
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
    case PK_UPGRADE:
        if (p->upg_count[k->sub] > 0) return false; /* never twice (pickups_refresh_owned swaps it) */
        upgrade_apply(k->sub);
        fx_shake(0.3f);
        break;
    case PK_STAT:
        stat_offer_take(k->sub);
        fx_shake(0.3f);
        break;
    case PK_ACTIVE:
        active_give(k->sub);
        fx_shake(0.3f);
        break;
    case PK_AUGMENT: {
        /* Paid in heart containers; the machine never takes the last one. */
        int cost = UPGRADES[k->sub].tier * 2;
        if (p->max_hp - cost < 2) return false;
        p->base.max_hp -= cost;
        player_recompute();
        upgrade_apply(k->sub);
        G.augments++;
        sfx_play(SFX_PLAYER_HURT);
        G.hurt_flash = 0.6f;
        fx_shake(0.5f);
    } break;
    }
    if (on_pedestal(k)) {
        sfx_play(SFX_ITEM);
        fx_particle(k->pos, v2(0, 0), 0.5f, 300.0f, COL_WHITE, 0.0f, PT_RING);
        fx_burst(k->pos, 40, 200.0f, 700.0f, 0.5f, 3.0f, COL_WHITE);
    }
    return true;
}

static void buy(const Pickup *k) {
    G.pl.coins -= k->price;
    G.items_bought++;
    sfx_play_ex(SFX_COIN, 1.0f, 0.7f, 0.0f);
    fx_popup(v2(k->pos.x, k->pos.y - 90.0f), col_hex(0xffd23f, 1), "-%d COINS", k->price);
    fx_burst(k->pos, 24, 100.0f, 400.0f, 0.4f, 2.5f, col_hex(0xffd23f, 1));
}

static void deny_purchase(const Pickup *k) {
    static float last_t = -10.0f;
    if (G.time - last_t < 1.0f && G.time >= last_t) return;
    last_t = G.time;
    sfx_play_at(SFX_DENIED, k->pos);
    fx_popup(v2(k->pos.x, k->pos.y - 90.0f), col_hex(0xff4d6a, 1), "NEED %d COINS", k->price);
}

static void deny_take(const Pickup *k, const char *msg) {
    sfx_play_at(SFX_DENIED, k->pos);
    fx_popup(v2(k->pos.x, k->pos.y - 90.0f), col_hex(0xff4d6a, 1), "%s", msg);
}

/* What E acts on: the nearest item on a pedestal / shop counter, or workshop machine, within
 * reach (nothing is ever taken by walking over it). */
void interact_target(int *pk, int *mc) {
    *pk = *mc = -1;
    if (G.state != GS_PLAYING) return;
    float best = 1e30f;
    for (int i = 0; i < G.npk; i++) {
        const Pickup *k = &G.pk[i];
        float d2 = v2_dist2(G.pl.pos, k->pos);
        if (on_pedestal(k) && k->t > 0.3f && d2 < INTERACT_R * INTERACT_R && d2 < best) {
            best = d2;
            *pk = i;
        }
    }
    for (int i = 0; i < G.nmc; i++) {
        float d2 = v2_dist2(G.pl.pos, G.mc[i].pos);
        if (d2 < INTERACT_R_MACHINE * INTERACT_R_MACHINE && d2 < best) {
            best = d2;
            *pk = -1;
            *mc = i;
        }
    }
}

void pickups_update(void) {
    const Player *pl = &G.pl;
    /* Once the room is cleared, resources fly to the player: no backtracking chores. */
    bool vacuum = G.room.cleared && G.room.clear_t > 0.6f;
    bool choice_taken = false;
    pickups_refresh_owned();
    for (int i = 0; i < G.npk; i++) G.pk[i].t += DT;

    /* E: take / buy the item in reach. */
    int target, machine;
    interact_target(&target, &machine);
    if (G.in.interact_pressed && target >= 0) {
        G.in.interact_pressed = false; /* used up (the machines read it next) */
        Pickup *k = &G.pk[target];
        if (k->price > G.pl.coins) {
            deny_purchase(k);
        } else if (collect(k)) {
            if (k->price > 0) buy(k);
            choice_taken = k->choice;
            G.pk[target] = G.pk[--G.npk];
        } else {
            deny_take(k, k->type == PK_HEART ? "FULL HEALTH" : k->type == PK_AUGMENT ? "NOT ENOUGH HEARTS" : "CANNOT TAKE IT");
        }
    }

    for (int i = 0; i < G.npk; i++) {
        Pickup *k = &G.pk[i];
        V2 d = v2_sub(pl->pos, k->pos);
        float dist = v2_len(d);
        bool taken = false;

        if (!on_pedestal(k)) {
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
            /* ...but never through the arena walls (the player may be standing in a doorway). */
            k->pos.x = clampf(k->pos.x, ARENA_X0 + 10.0f, ARENA_X1 - 10.0f);
            k->pos.y = clampf(k->pos.y, ARENA_Y0 + 10.0f, ARENA_Y1 - 10.0f);
            if (wants && k->t > 0.2f && dist < PLAYER_BODY_R + 14.0f) taken = collect(k);
        }

        if (taken) {
            G.pk[i] = G.pk[--G.npk];
            i--;
        }
    }

    /* One-of-N choices (stat trades, weapon / upgrade rooms): the other offers vanish. */
    if (choice_taken) {
        for (int i = 0; i < G.npk; i++) {
            if (!G.pk[i].choice) continue;
            SDL_FColor c = pickup_glow_color(&G.pk[i]);
            fx_particle(G.pk[i].pos, v2(0, 0), 0.4f, 120.0f, c, 0.0f, PT_RING);
            fx_burst(G.pk[i].pos, 20, 80.0f, 300.0f, 0.4f, 2.5f, c);
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
    case PK_AUGMENT:
        draw_ring(p, s * 1.35f, s * 0.15f, col_hex(0xff3355, 0.8f + 0.2f * sinf(t * 6.0f)));
        /* the upgrade's own icon inside a blood-red ring */
        __attribute__((fallthrough));
    case PK_UPGRADE: {
        SDL_FColor c = col_hex(sub >= 0 ? UPGRADES[sub].color : 0xb57cff, 1);
        int sides = sub >= 0 ? 3 + sub % 4 : 4; /* a little shape variety across 50+ upgrades */
        draw_ngon(p, s, sides, t * 2.0f, c);
        draw_ngon(p, s, sides, t * 2.0f + PI_F / (float)sides, c);
        draw_circle(p, s * 0.4f, COL_WHITE);
    } break;
    case PK_ACTIVE:
        draw_active_icon(sub, p, s, t);
        break;
    case PK_STAT: {
        /* Green arrow up, red arrow down: a trade. */
        SDL_FColor g = col_hex(0x5dff8a, 1), r = col_hex(0xff4d6a, 1);
        float b = sinf(t * 4.0f) * s * 0.08f;
        draw_tri(v2(p.x - s * 0.5f, p.y - s * 0.95f - b), v2(p.x - s * 0.05f, p.y - s * 0.1f - b), v2(p.x - s * 0.95f, p.y - s * 0.1f - b), g);
        draw_rect(p.x - s * 0.68f, p.y - s * 0.12f - b, s * 0.36f, s * 0.8f, g);
        draw_tri(v2(p.x + s * 0.5f, p.y + s * 0.95f + b), v2(p.x + s * 0.95f, p.y + s * 0.1f + b), v2(p.x + s * 0.05f, p.y + s * 0.1f + b), r);
        draw_rect(p.x + s * 0.32f, p.y - s * 0.68f + b, s * 0.36f, s * 0.8f, r);
    } break;
    }
}

static SDL_FColor pickup_glow_color(const Pickup *k) {
    switch (k->type) {
    case PK_COIN: return col_hex(0xffd23f, 1);
    case PK_KEY: return col_hex(0xd9e3f0, 1);
    case PK_BOMB: return col_hex(0xff8a3d, 1);
    case PK_HEART: return col_hex(0xff3355, 1);
    case PK_UPGRADE:
    case PK_AUGMENT: return col_hex(UPGRADES[k->sub].color, 1);
    case PK_STAT: return col_hex(COLOR_STATS, 1);
    case PK_ACTIVE: return col_hex(ACTIVES[k->sub].color, 1);
    }
    return COL_WHITE;
}

/* The item / machine E would act on: corner brackets around it and a key cap above. */
void interact_draw(void) {
    int pk, mc;
    interact_target(&pk, &mc);
    if (pk < 0 && mc < 0) return;
    V2 c;
    float hw, top, bottom, cap_y;
    SDL_FColor col;
    if (pk >= 0) {
        const Pickup *k = &G.pk[pk];
        c = k->pos;
        hw = 46.0f, top = 48.0f, bottom = 54.0f;
        cap_y = c.y - (k->price > 0 || k->type == PK_ACTIVE || k->type == PK_AUGMENT ? 112.0f : 82.0f);
        col = pickup_glow_color(k);
    } else {
        const Machine *m = &G.mc[mc];
        c = m->pos;
        hw = 76.0f, top = 84.0f, bottom = 84.0f;
        cap_y = c.y - 112.0f;
        col = m->broken ? col_a(COL_WHITE, 0.4f) : COL_WHITE;
    }
    float pulse = 0.5f + 0.5f * sinf(G.time * 7.0f), grow = 4.0f * pulse, arm = 16.0f, th = 3.0f;
    float x0 = c.x - hw - grow, x1 = c.x + hw + grow, y0 = c.y - top - grow, y1 = c.y + bottom + grow;
    SDL_FColor bc = col_a(col, 0.65f + 0.35f * pulse);
    for (int s = 0; s < 4; s++) {
        float x = s & 1 ? x1 : x0, y = s & 2 ? y1 : y0, dx = s & 1 ? -arm : arm, dy = s & 2 ? -arm : arm;
        draw_rect(fminf(x, x + dx) - th * 0.5f, y - th * 0.5f, arm + th, th, bc);
        draw_rect(x - th * 0.5f, fminf(y, y + dy) - th * 0.5f, th, arm + th, bc);
    }
    /* Key cap: "E". */
    float s = 32.0f, bob = sinf(G.time * 5.0f) * 2.0f;
    draw_rect(c.x - s * 0.5f, cap_y - s * 0.5f + bob + 3.0f, s, s, col_hex(0x05060c, 0.8f));
    draw_rect(c.x - s * 0.5f, cap_y - s * 0.5f + bob, s, s, col_hex(0x1a1f33, 1));
    draw_rect_outline(c.x - s * 0.5f, cap_y - s * 0.5f + bob, s, s, 2.0f, bc);
    draw_text_centered(c.x, cap_y - 10.0f + bob, 2.5f, COL_WHITE, "E");
}

void pickups_draw(void) {
    draw_blend_add();
    for (int i = 0; i < G.npk; i++) {
        const Pickup *k = &G.pk[i];
        SDL_FColor c = pickup_glow_color(k);
        if (on_pedestal(k)) {
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
        if (on_pedestal(k)) {
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
        if (!on_pedestal(k)) continue;
        if (k->price > 0) {
            char buf[16];
            int len = snprintf(buf, sizeof buf, "%d", k->price);
            float w = (float)len * 8.0f * 2.5f;
            bool afford = G.pl.coins >= k->price;
            float x0 = k->pos.x - (w + 26.0f) * 0.5f;
            draw_pickup_icon(PK_COIN, 1, v2(x0 + 9.0f, k->pos.y - 62.0f), 9.0f, G.time);
            draw_text(x0 + 26.0f, k->pos.y - 72.0f, 2.5f, afford ? col_hex(0xffd23f, 1) : col_hex(0xff4d6a, 1), "%s", buf);
        }
        float ls = k->price > 0 ? 1.75f : 2.0f; /* shops pack 5 items in a row */
        if (k->type == PK_HEART || k->type == PK_BOMB || k->type == PK_KEY) { /* the shop's supply counter */
            static const char *const what[] = {[PK_HEART] = "+1 HEART", [PK_BOMB] = "+1 BOMB", [PK_KEY] = "+1 KEY"};
            static const char *const why[] = {[PK_HEART] = "heals one heart", [PK_BOMB] = "clears the bullets,|opens secret rooms",
                                              [PK_KEY] = "opens a locked door"};
            draw_text_centered(k->pos.x, k->pos.y + 56.0f, ls, pickup_glow_color(k), "%s", what[k->type]);
            bool full = k->type == PK_HEART && G.pl.hp >= G.pl.max_hp;
            draw_desc(k->pos.x, k->pos.y + 60.0f + 10.0f * ls, 1.2f, full ? col_hex(0xff6a7e, 0.9f) : col_a(COL_WHITE, 0.75f),
                      full ? "full health" : why[k->type]);
            continue;
        }
        if (k->type == PK_STAT) {
            draw_text_centered(k->pos.x, k->pos.y + 56.0f, ls, col_hex(0x5dff8a, 1), "%s", STATS[stat_offer_up(k->sub)].up_text);
            draw_text_centered(k->pos.x, k->pos.y + 56.0f + 13.0f * ls, ls, col_hex(0xff4d6a, 1), "%s", STATS[stat_offer_down(k->sub)].down_text);
            continue;
        }
        if (k->type == PK_ACTIVE) {
            /* One owned already: a level up. Another one: added to the ones owned. */
            bool level_up = G.pl.act[k->sub].level > 0;
            int level = level_up ? G.pl.act[k->sub].level + 1 : 1;
            if (level_up)
                draw_text_centered(k->pos.x, k->pos.y - 74.0f, 2.0f, col_hex(0x7dff9a, 1), "LEVEL UP -> %d", level);
            else
                draw_text_centered(k->pos.x, k->pos.y - 74.0f, 1.5f, col_hex(0x7dff9a, 0.8f), "NEW");
            draw_text_centered(k->pos.x, k->pos.y + 56.0f, ls, pickup_glow_color(k), "%s", ACTIVES[k->sub].name);
            const char *desc = active_desc(k->sub, level);
            float dy = k->pos.y + 60.0f + 10.0f * ls;
            draw_desc(k->pos.x, dy, 1.5f, col_a(COL_WHITE, 0.7f), desc);
            draw_text_centered(k->pos.x, dy + (SDL_strchr(desc, '|') ? 33.0f : 16.5f), 1.5f, col_a(COL_WHITE, 0.5f),
                               "recharge %.1f s", (double)active_cooldown(k->sub, level));
            continue;
        }
        const char *name = UPGRADES[k->sub].name;
        if (k->type == PK_AUGMENT) {
            int hearts = UPGRADES[k->sub].tier;
            draw_text_centered(k->pos.x, k->pos.y - 74.0f, 2.0f, col_hex(0xff4d6a, 1), "-%d HEART%s", hearts, hearts > 1 ? "S" : "");
        }
        draw_text_centered(k->pos.x, k->pos.y + 56.0f, ls, pickup_glow_color(k), "%s", name);
        /* Bonus, then malus in red; shops pack 5 items in a row: smaller text there. */
        draw_upgrade_texts(k->pos.x, k->pos.y + 60.0f + 10.0f * ls, k->price > 0 ? 1.2f : 1.5f, 0.9f, k->sub);
    }
}

void draw_desc(float cx, float y, float scale, SDL_FColor col, const char *desc) {
    const char *bar = SDL_strchr(desc, '|');
    if (!bar) {
        draw_text_centered(cx, y, scale, col, "%s", desc);
        return;
    }
    draw_text_centered(cx, y, scale, col, "%.*s", (int)(bar - desc), desc);
    draw_text_centered(cx, y + 11.0f * scale, scale, col, "%s", bar + 1);
}
