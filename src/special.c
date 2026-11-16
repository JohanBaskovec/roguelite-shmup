#include "audio.h"
#include "draw.h"
#include "game.h"

#include <stdio.h>
#include <string.h>

/*
 * Six more rooms whose reward is machines. Like every room they start full of enemies; once it is cleared
 * (room_reveal) their machines appear (Machine, G.mc), used with E like the casino's:
 *  TRADER     up to TRADER_OFFERS upgrades for sale (MC_TRADE), each paid with TRADE_COST of the ship's own
 *             (random, never the same upgrade on two offers): the ship loses them, as a trade.
 *  PURIFIER   up to PURIFY_OFFERS owned upgrades (MC_PURIFY) whose malus is bought off with coins, for good
 *             (Player.malus_off: malus() and malus_stat_effect() skip it).
 *  VAULT      a safe (MC_SAFE) stands in the middle of the floor (nothing around it, blocks.c) from the moment the
 *             enemies come: the ship must break it (shots, the blade) while they attack. The sooner, the more coins (they
 *             wait, banked, until the fight is over: reward after the fight, Room.vault_coins), and if the last enemy
 *             falls first the safe vanishes. No coins can open it, E does nothing on it.
 *  FORGE      up to FORGE_OFFERS pairs of owned upgrades (MC_FORGE, one of them, for a few coins): B is fused into A (both bonuses stay
 *             active, B's malus is gone, the menu shows one entry that goes on and off as one: Player.fused_into).
 *  KEY ROOM   a locked gate (MC_GATE): one key opens it, an upgrade waits behind.
 *  INFIRMARY  a full heal (MC_HEAL), or a heart container for coins (MC_CONTAINER): one of the two.
 * Their doors are locked (door_needs_key) and as likely as any other room's (gen_doors); a room whose machines come out
 * empty (a trader for a ship with nothing to pay with...) drops a resource instead (coins, keys, bombs or hearts, its door said which).
 */

#define TRIGGER_R 62.0f
#define SAFE_R 92.0f
#define SAFE_HIT -2 /* PBullet.last_hit: this shot already hit the safe */
#define ROW_SPACING 520.0f
#define SAFE_W 200.0f /* the safe's body */
#define SAFE_H 180.0f

static V2 centre(void) { return v2(ARENA_CX, ARENA_CY - 60.0f); }

const char *room_type_name(RoomType t) {
    switch (t) {
    case ROOM_TRADER: return "TRADER";
    case ROOM_PURIFIER: return "PURIFIER";
    case ROOM_VAULT: return "VAULT";
    case ROOM_FORGE: return "FUSION FORGE";
    case ROOM_KEYS: return "KEY ROOM";
    case ROOM_INFIRMARY: return "INFIRMARY";
    default: return "";
    }
}

int room_type_by_name(const char *name) {
    static const struct { const char *n; RoomType t; } NAMES[] = {{"trader", ROOM_TRADER}, {"purifier", ROOM_PURIFIER}, {"vault", ROOM_VAULT},
                                                                {"forge", ROOM_FORGE}, {"keys", ROOM_KEYS}, {"key-room", ROOM_KEYS},
                                                                {"infirmary", ROOM_INFIRMARY}};
    for (int i = 0; i < (int)ARRAY_LEN(NAMES); i++) {
        const char *a = NAMES[i].n, *b = name;
        for (; *a && *b && *a == (char)(*b | 0x20); a++, b++) {}
        if (!*a && !*b) return NAMES[i].t;
    }
    return -1;
}

bool room_type_machines(RoomType t) { return t == ROOM_CASINO || (t >= ROOM_TRADER && t <= ROOM_INFIRMARY); }

/* ---- What the ship owns ------------------------------------------------------------------------------ */

bool upgrade_in_fusion(int u) {
    if (G.pl.fused_into[u]) return true;
    for (int v = 0; v < UPG_COUNT; v++)
        if (G.pl.fused_into[v] == u + 1) return true;
    return false;
}

/* Owned for real (not a CHAOS loan), not the ship's own starting weapon, not already part of a fusion. */
bool upgrade_tradable(int u) {
    const Player *p = &G.pl;
    return u >= 0 && u < UPG_COUNT && p->upg_count[u] > 0 && !p->chaos[u] && u != CHARACTERS[G.character].start_upgrade && !upgrade_in_fusion(u);
}

bool malus_removable(int u) { return MALUSES[u].kind > MAL_NONE && MALUSES[u].kind < MAL_COUNT && MALUSES[u].kind != MAL_CUSTOM; }

static bool purifiable(int u) {
    return u >= 0 && u < UPG_COUNT && G.pl.upg_count[u] > 0 && !G.pl.chaos[u] && malus_removable(u) && !G.pl.malus_off[u] && G.pl.fused_into[u] == 0;
}

static bool fusable(int u) { return u >= 0 && u < UPG_COUNT && G.pl.upg_count[u] > 0 && !G.pl.chaos[u] && !upgrade_in_fusion(u); }

static int collect(bool (*ok)(int), int *out) {
    int n = 0;
    for (int u = 0; u < UPG_COUNT; u++)
        if (ok(u)) out[n++] = u;
    return n;
}

static void shuffle(int *a, int n) {
    for (int i = n - 1; i > 0; i--) {
        int j = irand(0, i), t = a[i];
        a[i] = a[j];
        a[j] = t;
    }
}

static int special_price(float k) {
    int p = (int)roundf((float)shop_price(PK_UPGRADE, 0) * k);
    return p > 1 ? p : 1;
}

/* The machine room has nothing for this ship (a purifier when no upgrade has a malus to buy off...): it drops a random resource instead (coins, keys, bombs or hearts). */
bool special_room_empty(RoomType t) {
    int pool[UPG_COUNT];
    switch (t) {
    case ROOM_TRADER: return collect(upgrade_tradable, pool) < TRADE_COST;
    case ROOM_SHOP: return G.pl.coins < SHOP_MIN_COINS;
    case ROOM_BAPHOMET: return !baphomet_pact_possible(); /* no pact to sign: unlocked, not owned, payable in hearts */
    case ROOM_PURIFIER: return collect(purifiable, pool) == 0 || G.pl.coins < special_price(PURIFY_PRICE_K); /* nothing to purify, or not the coins to pay */
    case ROOM_FORGE: {
        int n = collect(fusable, pool), nr = 0;
        for (int i = 0; i < n; i++) nr += malus_removable(pool[i]);
        return n < 2 || nr == 0;
    }
    default: return false;
    }
}

/* The trader gives up the ship's upgrades: gone for good (only the one-time effects they already had stay). */
void upgrade_remove(int u) {
    Player *p = &G.pl;
    p->upg_count[u] = 0;
    p->upg_off[u] = false;
    p->malus_off[u] = false;
    p->chaos[u] = false;
    player_recompute();
}

/* ---- Spawning -------------------------------------------------------------------------------------------- */

static Machine *add(MachineType type, V2 pos, uint32_t color) {
    Machine *m = &G.mc[G.nmc++];
    memset(m, 0, sizeof *m);
    m->type = type;
    m->pos = pos;
    m->arg[0] = m->arg[1] = -1;
    fx_particle(pos, v2(0, 0), 0.5f, 200.0f, col_hex(color, 1), 0.0f, PT_RING);
    return m;
}

static V2 row_pos(int i, int n) { return v2(ARENA_CX + ((float)i - (float)(n - 1) * 0.5f) * ROW_SPACING, ARENA_CY - 30.0f); }

void trader_spawn(void) {
    G.nmc = 0;
    int pool[UPG_COUNT], n = collect(upgrade_tradable, pool);
    shuffle(pool, n);
    int offers = n / TRADE_COST < TRADER_OFFERS ? n / TRADE_COST : TRADER_OFFERS;
    for (int i = 0; i < offers; i++) {
        Machine *m = add(MC_TRADE, row_pos(i, offers), COLOR_TRADER);
        m->prize = PK_UPGRADE;
        m->prize_sub = upgrade_fresh(); /* (skips the prizes of the other offers) */
        m->arg[0] = pool[TRADE_COST * i];
        m->arg[1] = pool[TRADE_COST * i + 1];
        if (m->prize_sub < 0) G.nmc--; /* every upgrade owned: nothing left to sell */
    }
}

void purifier_spawn(void) {
    G.nmc = 0;
    int pool[UPG_COUNT], n = collect(purifiable, pool);
    if (special_room_empty(ROOM_PURIFIER)) return; /* no machine: the room drops a resource (room_reveal) */
    shuffle(pool, n);
    int offers = n < PURIFY_OFFERS ? n : PURIFY_OFFERS;
    for (int i = 0; i < offers; i++) {
        Machine *m = add(MC_PURIFY, row_pos(i, offers), COLOR_PURIFIER);
        m->arg[0] = pool[i];
        m->price = special_price(PURIFY_PRICE_K);
    }
}

/* With the enemies (room_enter): dead centre, on floor kept clear of blocks. */
void vault_spawn(void) {
    G.nmc = 0;
    Machine *m = add(MC_SAFE, v2(ARENA_CX, ARENA_CY), COLOR_VAULT);
    m->hp = SAFE_HP;
    m->timer = SAFE_TIME;
    G.room.vault_coins = 0;
}

/* The last enemy is dead: a safe still shut vanishes, a broken one is cleared away. */
void vault_on_clear(void) {
    if (G.room.type != ROOM_VAULT) return;
    for (int i = 0; i < G.nmc; i++) {
        const Machine *m = &G.mc[i];
        if (m->type != MC_SAFE) continue;
        fx_burst(m->pos, m->broken ? 20 : 50, 100.0f, 500.0f, 0.5f, 3.0f, col_hex(COLOR_VAULT, 1));
        if (!m->broken) {
            fx_popup(v2(m->pos.x, m->pos.y - 130.0f), col_hex(0xff4d6a, 1), "THE SAFE VANISHES");
            sfx_play_at(SFX_DENIED, m->pos);
        }
    }
    G.nmc = 0;
}

/* The room has shattered: the coins the safe paid are out. */
void vault_reveal(void) {
    V2 at = v2(ARENA_CX, ARENA_CY);
    for (int k = 0; k < G.room.vault_coins; k++) pickup_spawn(PK_COIN, 1, at, v2_scale(v2_angle(frand(0.0f, TAU_F)), frand(120.0f, 480.0f)));
    if (G.room.vault_coins > 0) fx_burst(at, 40, 120.0f, 520.0f, 0.5f, 3.0f, col_hex(COLOR_VAULT, 1));
}

void forge_spawn(void) {
    G.nmc = 0;
    int pool[UPG_COUNT], n = collect(fusable, pool), removable[UPG_COUNT], nr = 0;
    for (int i = 0; i < n; i++)
        if (malus_removable(pool[i])) removable[nr++] = pool[i];
    int pairs[FORGE_OFFERS][2], np = 0;
    for (int tries = 0; tries < 40 && np < FORGE_OFFERS && nr > 0 && n >= 2; tries++) {
        int b = removable[irand(0, nr - 1)], a = pool[irand(0, n - 1)];
        bool same = a == b;
        for (int k = 0; k < np && !same; k++)
            same = (pairs[k][0] == a && pairs[k][1] == b) || (pairs[k][0] == b && pairs[k][1] == a);
        if (same) continue;
        pairs[np][0] = a; /* keeps its malus */
        pairs[np++][1] = b; /* absorbed: its malus goes */
    }
    for (int i = 0; i < np; i++) {
        Machine *m = add(MC_FORGE, row_pos(i, np), COLOR_FORGE);
        m->arg[0] = pairs[i][0];
        m->arg[1] = pairs[i][1];
        m->group = 1;
        m->price = special_price(FORGE_PRICE_K);
    }
}

void keys_room_spawn(void) {
    G.nmc = 0;
    Machine *m = add(MC_GATE, centre(), COLOR_KEYS);
    machine_roll_prize(m);
}

void infirmary_spawn(void) {
    G.nmc = 0;
    Machine *a = add(MC_HEAL, v2(ARENA_CX - 300.0f, ARENA_CY - 30.0f), COLOR_INFIRMARY);
    Machine *b = add(MC_CONTAINER, v2(ARENA_CX + 300.0f, ARENA_CY - 30.0f), COLOR_INFIRMARY);
    a->group = b->group = 1;
    b->price = special_price(CONTAINER_PRICE_K);
}

/* A prize the ship now owns (taken elsewhere in the room, or paid back by a trade) is swapped for a fresh one. */
void special_refresh_prize(void) {
    for (int i = 0; i < G.nmc; i++) {
        Machine *m = &G.mc[i];
        if ((m->type == MC_TRADE || m->type == MC_GATE) && !m->broken && m->prize == PK_UPGRADE && G.pl.upg_count[m->prize_sub] > 0) {
            int sub = upgrade_fresh();
            if (sub >= 0) m->prize_sub = sub;
            else if (m->type == MC_GATE) machine_roll_prize(m);
            else m->broken = true;
        }
    }
}

/* ---- The safe ------------------------------------------------------------------------------------------ */

V2 machine_safe_pos(void) {
    for (int i = 0; i < G.nmc; i++)
        if (G.mc[i].type == MC_SAFE) return G.mc[i].pos;
    return centre();
}

bool machine_in_reach(int i, V2 p, float r) { return v2_dist2(G.mc[i].pos, p) < (SAFE_R + r) * (SAFE_R + r); }

static int safe_coins(const Machine *m) {
    float frac = clampf(m->timer / SAFE_TIME, 0.0f, 1.0f);
    int n = (int)roundf(((float)SAFE_MIN_COINS + (float)(SAFE_MAX_COINS - SAFE_MIN_COINS) * frac) * luck_factor());
    return n > 1 ? n : 1;
}

void machine_damage(int i, float dmg) {
    Machine *m = &G.mc[i];
    if (m->type != MC_SAFE || m->broken || m->hp <= 0.0f) return;
    m->hp -= dmg;
    m->anim = 0.12f;
    sfx_play_at(SFX_HIT, m->pos);
    fx_particle(v2_add(m->pos, v2(frand(-50.0f, 50.0f), frand(-50.0f, 50.0f))), v2(0, 0), 0.12f, 36.0f, col_hex(COLOR_VAULT, 0.8f), 0.0f, PT_GLOW);
    if (m->hp > 0.0f) return;
    int n = safe_coins(m);
    m->broken = true;
    m->hp = 0.0f;
    G.machine_uses++;
    G.room.vault_coins = n; /* banked: they come out once the fight is over, like any room's reward */
    m->shatter_t = CRACK_TIME; /* it cracks, then bursts into glass like the obstacles of a cleared room (special_tick) */
    sfx_play(SFX_CRACKLE);
}

bool machine_shot_hit(PBullet *b) {
    for (int i = 0; i < G.nmc; i++) {
        Machine *m = &G.mc[i];
        if (m->type != MC_SAFE || m->broken || b->last_hit == SAFE_HIT) continue;
        if (v2_dist2(b->pos, m->pos) >= (SAFE_R + b->radius) * (SAFE_R + b->radius)) continue;
        b->last_hit = SAFE_HIT;
        machine_damage(i, b->damage);
        return true;
    }
    return false;
}

/* ---- Using them ---------------------------------------------------------------------------------------- */

static void deny(const Machine *m, const char *msg) {
    sfx_play_at(SFX_DENIED, m->pos);
    fx_popup(v2(m->pos.x, m->pos.y - 130.0f), col_hex(0xff4d6a, 1), "%s", msg);
}

/* Using one machine of a one-of-N group puts the others out of use. */
static void close_group(const Machine *used) {
    if (!used->group) return;
    for (int i = 0; i < G.nmc; i++) {
        Machine *m = &G.mc[i];
        if (m != used && m->group == used->group && !m->broken) {
            m->broken = true;
            m->anim = 0.0f;
            fx_particle(m->pos, v2(0, 0), 0.3f, 120.0f, COL_WHITE, 0.0f, PT_RING);
        }
    }
}

static void done(Machine *m, uint32_t color) {
    m->broken = true;
    G.machine_uses++;
    sfx_play_at(SFX_ITEM, m->pos);
    fx_burst(m->pos, 40, 120.0f, 520.0f, 0.5f, 3.0f, col_hex(color, 1));
    close_group(m);
}

static void use_trade(Machine *m) {
    if (!upgrade_tradable(m->arg[0]) || !upgrade_tradable(m->arg[1]) || G.pl.upg_count[m->prize_sub] > 0) {
        deny(m, "OFFER GONE");
        m->broken = true;
        return;
    }
    fx_popup(v2(m->pos.x, m->pos.y - 130.0f), col_hex(0xff4d6a, 1), "-%s  -%s", UPGRADES[m->arg[0]].name, UPGRADES[m->arg[1]].name);
    upgrade_remove(m->arg[0]);
    upgrade_remove(m->arg[1]);
    upgrade_apply(m->prize_sub);
    done(m, COLOR_TRADER);
}

static void use_purify(Machine *m) {
    int u = m->arg[0];
    if (!purifiable(u)) {
        deny(m, "ALREADY CLEAN");
        m->broken = true;
        return;
    }
    if (G.pl.coins < m->price) {
        deny(m, "NOT ENOUGH COINS");
        return;
    }
    G.pl.coins -= m->price;
    G.pl.malus_off[u] = true;
    player_recompute();
    fx_banner("MALUS REMOVED", UPGRADES[u].name, col_hex(COLOR_PURIFIER, 1), 2.0f);
    done(m, COLOR_PURIFIER);
}

static void use_forge(Machine *m) {
    int a = m->arg[0], b = m->arg[1];
    if (!fusable(a) || !fusable(b)) {
        deny(m, "OFFER GONE");
        m->broken = true;
        return;
    }
    if (G.pl.coins < m->price) {
        deny(m, "NOT ENOUGH COINS");
        return;
    }
    G.pl.coins -= m->price;
    Player *p = &G.pl;
    p->fused_into[b] = (int16_t)(a + 1);
    p->malus_off[b] = true; /* one malus left: a's */
    p->upg_off[b] = p->upg_off[a];
    player_recompute();
    char sub[96];
    snprintf(sub, sizeof sub, "%s + %s", UPGRADES[a].name, UPGRADES[b].name);
    fx_banner("FUSION", sub, col_hex(COLOR_FORGE, 1), 2.4f);
    done(m, COLOR_FORGE);
}

static void use_gate(Machine *m) {
    if (G.pl.keys < 1) {
        deny(m, "NEED A KEY");
        return;
    }
    G.pl.keys--;
    G.machine_uses++;
    m->broken = true;
    m->anim = 0.6f;
    pickup_spawn(m->prize, m->prize_sub, v2(m->pos.x, m->pos.y + 6.0f), v2(0, 0)); /* the pedestal, taken with E */
    sfx_play_at(SFX_KEY, m->pos);
    fx_burst(m->pos, 40, 120.0f, 500.0f, 0.5f, 3.0f, col_hex(COLOR_KEYS, 1));
    fx_popup(v2(m->pos.x, m->pos.y - 130.0f), col_hex(0xd9e3f0, 1), "-1 KEY");
}

static void use_heal(Machine *m) {
    Player *p = &G.pl;
    if (p->hp >= p->max_hp) {
        deny(m, "FULL HEALTH");
        return;
    }
    p->hp = p->max_hp;
    sfx_play(SFX_HEART);
    fx_popup(v2(m->pos.x, m->pos.y - 130.0f), col_hex(0xff5f7e, 1), "FULL HEAL");
    done(m, COLOR_INFIRMARY);
}

static void use_container(Machine *m) {
    if (!stat_can_change(ST_MAX_HP, true)) {
        deny(m, "NO ROOM FOR MORE");
        return;
    }
    if (G.pl.coins < m->price) {
        deny(m, "NOT ENOUGH COINS");
        return;
    }
    G.pl.coins -= m->price;
    stat_apply(ST_MAX_HP, true); /* +1 heart container, filled */
    fx_popup(v2(m->pos.x, m->pos.y - 130.0f), col_hex(0xff5f7e, 1), "+1 HEART CONTAINER");
    done(m, COLOR_INFIRMARY);
}

const char *machine_used_text(const Machine *m) {
    switch (m->type) {
    case MC_SUPPLY: return "BROKEN";
    case MC_GAMBLE: return "PRIZE WON";
    case MC_TRADE: return "SOLD";
    case MC_PURIFY: return "PURIFIED";
    case MC_SAFE: return "";
    case MC_GATE: return "OPEN";
    case MC_HEAL:
    case MC_CONTAINER: return "ONE ONLY";
    case MC_FORGE: return "ONE FUSION ONLY";
    }
    return "";
}

/* Each tick, for every machine of the six rooms (the safe's countdown, the prices). */
void special_tick(int i) {
    Machine *m = &G.mc[i];
    if (m->type == MC_SAFE && m->shatter_t > 0.0f && (m->shatter_t -= DT) <= 0.0f) {
        m->shatter_t = 0.0f;
        m->gone = true;
        room_shatter_rect((SDL_FRect){m->pos.x - SAFE_W * 0.5f, m->pos.y - SAFE_H * 0.5f, SAFE_W, SAFE_H}, col_hex(COLOR_VAULT, 0.95f), v2_add(m->pos, v2(0, 60.0f)));
    }
    if (m->type == MC_PURIFY) m->price = special_price(PURIFY_PRICE_K);
    else if (m->type == MC_CONTAINER) m->price = special_price(CONTAINER_PRICE_K);
    else if (m->type == MC_FORGE) m->price = special_price(FORGE_PRICE_K);
    else if (m->type == MC_SAFE && !m->broken && !active_time_stop_on() && m->timer > 0.0f) {
        m->timer -= DT; /* the bonus melts away (SAFE_MIN_COINS stay until the fight is over) */
        if (m->timer < 0.0f) m->timer = 0.0f;
    }
}

void special_use(Machine *m) {
    switch (m->type) {
    case MC_TRADE: use_trade(m); break;
    case MC_PURIFY: use_purify(m); break;
    case MC_GATE: use_gate(m); break;
    case MC_HEAL: use_heal(m); break;
    case MC_CONTAINER: use_container(m); break;
    case MC_FORGE: use_forge(m); break;
    default: break; /* the safe is broken by shots, not by E */
    }
}

/* ---- Drawing ----------------------------------------------------------------------------------------------- */

static float desc_lines(const char *s) { return SDL_strchr(s, '|') ? 2.0f : 1.0f; }

void fused_draw_texts(float cx, float y, float scale, int u) {
    const Player *p = &G.pl;
    float line = 11.0f * scale, yy = y;
    draw_desc(cx, yy, scale, col_a(COL_WHITE, 0.8f), upgrade_desc(u));
    yy += desc_lines(upgrade_desc(u)) * line + 3.0f * scale;
    for (int v = 0; v < UPG_COUNT; v++) {
        if (p->fused_into[v] != u + 1) continue;
        draw_text_centered(cx, yy, scale * 0.85f, col_hex(UPGRADES[v].color, 0.9f), "+ %s", UPGRADES[v].name);
        yy += line;
        draw_desc(cx, yy, scale, col_a(COL_WHITE, 0.8f), upgrade_desc(v));
        yy += desc_lines(upgrade_desc(v)) * line + 3.0f * scale;
    }
    if (p->malus_off[u]) draw_desc(cx, yy, scale, col_hex(0x7dff9a, 0.95f), "Malus removed");
    else draw_desc(cx, yy, scale, col_hex(0xff6a7e, 0.95f), upgrade_malus_desc(u));
}

static void cabinet(V2 p, SDL_FColor c, float w, float h, bool inside) {
    draw_blend_add();
    draw_glow(p, 110.0f, col_a(c, 0.2f));
    draw_blend_alpha();
    draw_rect(p.x - w * 0.5f, p.y - h * 0.5f, w, h, col_hex(0x10131f, 1));
    draw_rect_outline(p.x - w * 0.5f, p.y - h * 0.5f, w, h, 3.0f, c);
    draw_rect(p.x - w * 0.5f - 6.0f, p.y + h * 0.5f - 12.0f, w + 12.0f, 14.0f, col_a(c, 0.6f));
    draw_ring(p, TRIGGER_R, 1.5f, col_a(c, inside ? 0.5f : 0.15f));
}

/* The name, then the bonus (white) and the malus (red) of an upgrade, under a machine; returns the next y. */
static float upgrade_block(V2 p, float y, int u, const char *prefix) {
    draw_text_centered(p.x, y, 1.75f, col_hex(UPGRADES[u].color, 1), "%s%s", prefix, UPGRADES[u].name);
    return y + 18.0f + draw_upgrade_texts(p.x, y + 18.0f, 1.25f, 0.85f, u) + 8.0f;
}

static void draw_big_heart(V2 p, float s, SDL_FColor c, bool plus) {
    draw_heart(p, s, c, false);
    if (plus) {
        draw_rect(p.x - s * 0.1f, p.y - s * 0.45f, s * 0.2f, s * 0.9f, COL_WHITE);
        draw_rect(p.x - s * 0.45f, p.y - s * 0.1f, s * 0.9f, s * 0.2f, COL_WHITE);
    }
}

void special_draw(const Machine *m) {
    const Player *pl = &G.pl;
    SDL_FColor c = col_hex(m->type == MC_TRADE ? COLOR_TRADER : m->type == MC_PURIFY ? COLOR_PURIFIER : m->type == MC_SAFE ? COLOR_VAULT
                           : m->type == MC_GATE ? COLOR_KEYS : m->type == MC_FORGE ? COLOR_FORGE : COLOR_INFIRMARY,
                           m->broken && m->type != MC_SAFE ? 0.4f : 1.0f);
    float bump = m->anim > 0.0f ? sinf(m->anim * 40.0f) * 3.0f : 0.0f;
    V2 p = v2(m->pos.x + bump, m->pos.y);
    float ty = p.y + 86.0f;
    SDL_FColor gold = col_hex(0xffd23f, 1), bad = col_hex(0xff4d6a, 1);

    switch (m->type) {
    case MC_TRADE: {
        cabinet(p, c, 120.0f, 140.0f, m->inside);
        draw_text_centered(p.x, p.y - 150.0f, 2.0f, c, "TRADE");
        if (m->broken) {
            draw_text_centered(p.x, ty, 2.0f, c, "%s", machine_used_text(m));
            break;
        }
        draw_pickup_icon(PK_UPGRADE, m->arg[0], v2(p.x - 26.0f, p.y - 36.0f), 11.0f, G.time);
        draw_pickup_icon(PK_UPGRADE, m->arg[1], v2(p.x + 26.0f, p.y - 36.0f), 11.0f, G.time);
        draw_tri(v2(p.x - 12.0f, p.y - 4.0f), v2(p.x + 12.0f, p.y - 4.0f), v2(p.x, p.y + 12.0f), col_a(COL_WHITE, 0.7f));
        draw_pickup_icon(PK_UPGRADE, m->prize_sub, v2(p.x, p.y + 38.0f + sinf(G.time * 3.0f) * 3.0f), 14.0f, G.time);
        float y = upgrade_block(p, ty - 4.0f, m->prize_sub, "BUY: ");
        draw_text_centered(p.x, y, 1.5f, bad, "PAY WITH:");
        for (int k = 0; k < 2; k++)
            draw_text_centered(p.x, y + 20.0f + 17.0f * (float)k, 1.5f, col_hex(UPGRADES[m->arg[k]].color, 1), "%s", UPGRADES[m->arg[k]].name);
    } break;
    case MC_PURIFY: {
        cabinet(p, c, 120.0f, 140.0f, m->inside);
        draw_text_centered(p.x, p.y - 150.0f, 2.0f, c, "PURIFIER");
        if (m->broken) {
            draw_text_centered(p.x, ty, 2.0f, c, "%s", machine_used_text(m));
            break;
        }
        draw_pickup_icon(PK_UPGRADE, m->arg[0], v2(p.x, p.y - 36.0f), 13.0f, G.time);
        draw_circle(v2(p.x, p.y + 30.0f), 22.0f, col_a(c, 0.85f)); /* a drop */
        draw_tri(v2(p.x - 17.0f, p.y + 22.0f), v2(p.x + 17.0f, p.y + 22.0f), v2(p.x, p.y - 8.0f), col_a(c, 0.85f));
        draw_text_centered(p.x, ty - 4.0f, 1.75f, col_hex(UPGRADES[m->arg[0]].color, 1), "%s", UPGRADES[m->arg[0]].name);
        float h = draw_upgrade_texts(p.x, ty + 14.0f, 1.25f, 0.85f, m->arg[0]);
        draw_text_centered(p.x, ty + 24.0f + h, 1.5f, col_a(COL_WHITE, 0.85f), "REMOVE THE MALUS");
        draw_text_centered(p.x, ty + 42.0f + h, 2.0f, pl->coins >= m->price ? gold : bad, "%d COINS", m->price);
    } break;
    case MC_SAFE: {
        if (m->gone) break;
        float k = clampf(m->hp / SAFE_HP, 0.0f, 1.0f), t = clampf(m->timer / SAFE_TIME, 0.0f, 1.0f);
        draw_blend_add();
        draw_glow(p, 190.0f, col_a(c, m->broken ? 0.06f : 0.22f));
        draw_blend_alpha();
        draw_rect(p.x - 100.0f, p.y - 90.0f, 200.0f, 180.0f, col_hex(0x14161f, 1));
        draw_rect_outline(p.x - 100.0f, p.y - 90.0f, 200.0f, 180.0f, 5.0f, c);
        draw_rect_outline(p.x - 84.0f, p.y - 74.0f, 168.0f, 148.0f, 2.0f, col_a(c, 0.5f));
        draw_ring(p, 46.0f, 4.0f, col_a(c, 0.9f)); /* the dial */
        for (int s = 0; s < 8; s++) {
            V2 d = v2_angle(TAU_F * (float)s / 8.0f + (m->broken ? 0.0f : G.time * 0.5f));
            draw_line(v2_add(p, v2_scale(d, 30.0f)), v2_add(p, v2_scale(d, 44.0f)), 3.0f, col_a(c, 0.8f));
        }
        draw_circle(p, 8.0f, c);
        draw_rect(p.x - 100.0f, p.y + 96.0f, 200.0f, 12.0f, col_hex(0x251b1b, 1)); /* what is left of its armour */
        draw_rect(p.x - 100.0f, p.y + 96.0f, 200.0f * k, 12.0f, col_hex(0xff6a4d, 1));
        if (m->broken) { /* cracking: the same white cracks as an obstacle about to burst */
            room_draw_cracks((SDL_FRect){p.x - SAFE_W * 0.5f, p.y - SAFE_H * 0.5f, SAFE_W, SAFE_H}, 1.0f - m->shatter_t / CRACK_TIME, 7);
            break;
        }
        draw_text_centered(p.x, p.y - 156.0f, 2.0f, c, "BREAK IT BEFORE THE FIGHT ENDS");
        draw_rect(p.x - 100.0f, p.y - 120.0f, 200.0f, 10.0f, col_hex(0x1b2230, 1)); /* the bonus melting away */
        draw_rect(p.x - 100.0f, p.y - 120.0f, 200.0f * t, 10.0f, t > 0.3f ? gold : bad);
        draw_text_centered(p.x, p.y + 122.0f, 2.5f, t > 0.3f ? gold : bad, "%d COINS", safe_coins(m));
        draw_text_centered(p.x, p.y + 148.0f, 1.5f, col_a(COL_WHITE, 0.7f), "THE SOONER, THE MORE");
    } break;
    case MC_GATE: {
        cabinet(p, c, 150.0f, 160.0f, m->inside);
        draw_text_centered(p.x, p.y - 150.0f, 2.0f, c, "KEY ROOM");
        ty = p.y + 100.0f;
        if (m->prize == PK_UPGRADE) draw_pickup_icon(PK_UPGRADE, m->prize_sub, p, 18.0f + sinf(G.time * 3.0f) * 2.0f, G.time);
        else draw_pickup_icon(m->prize, m->prize_sub, p, 18.0f, G.time);
        if (!m->broken) { /* the gate: bars across the front, a padlock on them */
            for (int b = -3; b <= 3; b++) draw_rect(p.x + (float)b * 20.0f - 2.0f, p.y - 72.0f, 4.0f, 144.0f, col_a(c, 0.85f));
            draw_rect(p.x - 70.0f, p.y - 8.0f, 140.0f, 5.0f, col_a(c, 0.85f));
            draw_rect(p.x - 14.0f, p.y - 14.0f, 28.0f, 24.0f, col_hex(0xffd23f, 1));
            draw_arc(v2(p.x, p.y - 14.0f), 10.0f, 4.0f, PI_F, TAU_F, col_hex(0xffd23f, 1));
            if (m->prize == PK_UPGRADE) {
                draw_text_centered(p.x, ty - 4.0f, 1.75f, col_hex(UPGRADES[m->prize_sub].color, 1), "%s", UPGRADES[m->prize_sub].name);
                float h = draw_upgrade_texts(p.x, ty + 14.0f, 1.25f, 0.85f, m->prize_sub);
                draw_text_centered(p.x, ty + 26.0f + h, 1.75f, pl->keys > 0 ? col_hex(0xd9e3f0, 1) : bad, "ONE KEY OPENS IT (%d)", pl->keys);
            } else {
                draw_text_centered(p.x, ty, 1.75f, gold, "BEHIND THE GATE: A TREASURE");
            }
        } else {
            draw_text_centered(p.x, ty, 2.0f, c, "OPEN");
        }
    } break;
    case MC_HEAL:
    case MC_CONTAINER: {
        bool heal = m->type == MC_HEAL;
        SDL_FColor hc = col_a(col_hex(0xff3355, 1), m->broken ? 0.4f : 1.0f);
        cabinet(p, c, 120.0f, 140.0f, m->inside);
        draw_text_centered(p.x, p.y - 150.0f, 2.0f, c, "%s", heal ? "FULL HEAL" : "HEART CONTAINER");
        draw_big_heart(v2(p.x, p.y), 34.0f + sinf(G.time * 4.0f) * (m->broken ? 0.0f : 2.0f), hc, !heal);
        if (m->broken) {
            draw_text_centered(p.x, ty, 2.0f, c, "NOT TAKEN");
            break;
        }
        if (heal) {
            draw_text_centered(p.x, ty, 2.0f, col_hex(0x7dff9a, 1), "FREE");
            draw_text_centered(p.x, ty + 24.0f, 1.5f, col_a(COL_WHITE, 0.8f), pl->hp >= pl->max_hp ? "ALREADY AT FULL HEALTH" : "ALL YOUR HEARTS BACK");
        } else {
            draw_text_centered(p.x, ty, 2.0f, pl->coins >= m->price ? gold : bad, "%d COINS", m->price);
            draw_text_centered(p.x, ty + 24.0f, 1.5f, col_a(COL_WHITE, 0.8f), "+1 MAX HEART, FILLED");
        }
        draw_text_centered(p.x, ty + 44.0f, 1.25f, col_a(COL_WHITE, 0.55f), "ONE OF THE TWO");
    } break;
    case MC_FORGE: {
        cabinet(p, c, 120.0f, 140.0f, m->inside);
        draw_text_centered(p.x, p.y - 150.0f, 2.0f, c, "FUSE");
        if (m->broken) {
            draw_text_centered(p.x, ty, 2.0f, c, "%s", machine_used_text(m));
            break;
        }
        int a = m->arg[0], b = m->arg[1];
        draw_pickup_icon(PK_UPGRADE, a, v2(p.x - 28.0f, p.y - 34.0f), 12.0f, G.time);
        draw_pickup_icon(PK_UPGRADE, b, v2(p.x + 28.0f, p.y - 34.0f), 12.0f, G.time);
        draw_tri(v2(p.x - 12.0f, p.y - 2.0f), v2(p.x + 12.0f, p.y - 2.0f), v2(p.x, p.y + 14.0f), col_a(COL_WHITE, 0.7f));
        draw_ngon(v2(p.x, p.y + 40.0f), 17.0f, 6, G.time, c); /* the one that comes out */
        draw_text_centered(p.x, ty - 4.0f, 1.5f, col_hex(UPGRADES[a].color, 1), "%s", UPGRADES[a].name);
        draw_text_centered(p.x, ty + 12.0f, 1.5f, col_hex(UPGRADES[b].color, 1), "+ %s", UPGRADES[b].name);
        float y = ty + 32.0f;
        draw_desc(p.x, y, 1.25f, col_a(COL_WHITE, 0.8f), upgrade_desc(a));
        y += desc_lines(upgrade_desc(a)) * 14.0f + 4.0f;
        draw_desc(p.x, y, 1.25f, col_a(COL_WHITE, 0.8f), upgrade_desc(b));
        y += desc_lines(upgrade_desc(b)) * 14.0f + 6.0f;
        draw_text_centered(p.x, y, 1.25f, col_a(COL_WHITE, 0.6f), "ONE MALUS LEFT:");
        draw_desc(p.x, y + 14.0f, 1.25f, col_hex(0xff6a7e, 0.95f), pl->malus_off[a] ? "(none)" : upgrade_malus_desc(a));
        draw_text_centered(p.x, y + 14.0f + desc_lines(upgrade_malus_desc(a)) * 14.0f + 4.0f, 1.25f, col_hex(0x7dff9a, 0.9f), "%s's malus is gone", UPGRADES[b].name);
        draw_text_centered(p.x, y + 14.0f + desc_lines(upgrade_malus_desc(a)) * 14.0f + 22.0f, 2.0f, pl->coins >= m->price ? gold : bad, "%d COINS", m->price);
    } break;
    default: break;
    }
}
