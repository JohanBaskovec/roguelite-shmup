#include "audio.h"
#include "draw.h"
#include "game.h"

/*
 * The newer upgrades (50 regular ones, UPG_TWIN_BARREL .. UPG_SPLASH) and Baphomet exclusives
 * (UPG_X_ARSENAL .. UPG_X_DOOMSDAY). Their table entries live with the others (UPGRADES in
 * upgrades.c, MALUSES in maluses.c); their effects are here, called from the hooks of upgrades.c
 * (upg2_*), and read through upg_has() so that toggles and CHAOS loans follow. Their state lives
 * in the Player (reset with it at every run) and in the Enemy (acid, static_n, stun_t).
 */

static int on(int u) { return upg_has(u); }
static float proc2(float p) { return minf(1.0f, p * (on(UPG_LOADED_DICE) ? 2.0f : 1.0f)); }
static float aura(void) { return G.pl.dmg_mult; }
static bool fighting(void) { return G.nen > 0 && !G.room.cleared; }
static int pct2(float p) { return (int)roundf(p * 100.0f); }

#define SCATTER_CHANCE 0.25f
#define CONCUSSION_CHANCE 0.12f
#define VAMPIRE_DAMAGE 900.0f
#define REPULSOR_R 110.0f
#define SHOCK_R 100.0f
#define ORBITAL_ARRAY_R 110.0f
#define TEMPEST_R 210.0f
#define NUKE_R 260.0f

/* ---- Small helpers --------------------------------------------------------- */

/* A shot of ours carrying every upgrade. scaled: dmg already comes from the volley (only the
 * shot speed and range multipliers are applied here, like the TAIL GUN's). */
static PBullet *shot2(V2 pos, float ang, float speed, float radius, float dmg, float life, uint32_t color, bool scaled) {
    const Player *p = &G.pl;
    PBullet *b = pbullet_spawn(pos, v2_scale(v2_angle(ang), speed), radius, dmg, col_hex(color, 1));
    if (!b) return NULL;
    b->life = life;
    b->knockback = 30.0f;
    if (scaled) {
        b->vel = v2_scale(b->vel, p->shot_speed_mult);
        b->life *= p->range_mult;
    }
    pbullet_arm(b, scaled);
    return b;
}

static void ring2(V2 at, int count, float dmg, float speed, float life, uint32_t color) {
    float off = frand(0.0f, TAU_F);
    for (int i = 0; i < count; i++) shot2(at, off + TAU_F * (float)i / (float)count, speed, 5.0f, dmg, life, color, false);
}

/* CONCUSSION, COLD SNAP, CRYO BOMB: frozen in place for a moment (never a boss, nor something
 * in the air or glued to a body). */
static void stun(Enemy *e, float t) {
    if (e->hp <= 0.0f || enemy_is_boss(e) || e->alt > 0.0f || e->type == EN_CENTIPEDE || e->type == EN_CRAWLER) return;
    e->stun_t = maxf(e->stun_t, t);
}

static Enemy *by_uid2(int uid) {
    if (uid <= 0) return NULL;
    for (int i = 0; i < G.nen; i++)
        if (G.en[i].uid == uid && G.en[i].hp > 0.0f) return &G.en[i];
    return NULL;
}

/* The enemy with the most health (HUNTER'S MARK, SMITE), the boss included. */
static Enemy *toughest(void) {
    Enemy *best = NULL;
    for (int i = 0; i < G.nen; i++) {
        Enemy *e = &G.en[i];
        if (e->hp <= 0.0f || e->fake || enemy_untouchable(e)) continue;
        if (!best || e->hp > best->hp) best = e;
    }
    return best;
}

static Enemy *nearest_to(V2 at, float max_d) {
    Enemy *best = NULL;
    float bd = max_d * max_d;
    for (int i = 0; i < G.nen; i++) {
        Enemy *e = &G.en[i];
        float d2 = v2_dist2(e->pos, at);
        if (e->hp > 0.0f && !e->fake && !enemy_untouchable(e) && d2 < bd) {
            bd = d2;
            best = e;
        }
    }
    return best;
}

static void heal_half(uint32_t color) {
    Player *p = &G.pl;
    if (p->hp >= p->max_hp) return;
    p->hp++;
    sfx_play(SFX_HEART);
    fx_popup(v2_add(p->pos, v2(0, -36)), col_hex(color, 1), "+HP");
}

static void beam_fx(V2 at, uint32_t color) {
    for (int k = 0; k < 10; k++)
        fx_particle(v2(at.x + frand(-6, 6), at.y - (float)k * 60.0f), v2(0, 0), 0.25f, 18.0f, col_hex(color, 0.7f), 0.0f, PT_GLOW);
}

static V2 array_orb(int i) {
    return v2_add(G.pl.pos, v2_scale(v2_angle(-G.time * 1.9f + TAU_F * (float)i / 4.0f), ORBITAL_ARRAY_R));
}

/* ---- Table helpers (stats, caps, descriptions, one-time effects) ------------ */

void upg2_stat_effect(Stats *p, int u) {
    switch (u) {
    case UPG_SPARE_PARTS: p->max_hp += 2; break;
    case UPG_BUCKSHOT: p->extra_proj += 2; break;
    case UPG_SLUGGER: p->dmg_mult *= 1.6f; break;
    case UPG_NIMBLE: p->speed = minf(900.0f, p->speed * 1.2f); p->shot_speed_mult *= 1.15f; break;
    case UPG_X_ARSENAL: p->extra_proj++; p->rate_mult *= 1.2f; p->dmg_mult *= 1.2f; break;
    case UPG_X_GHOST_HULL: p->speed = minf(900.0f, p->speed * 1.25f); break;
    case UPG_X_SUPERCHARGER: p->dash_cooldown = maxf(0.15f, p->dash_cooldown * 0.5f); break;
    case UPG_X_VOID_ROUNDS: p->pierce += 2; p->dmg_mult *= 1.25f; break;
    case UPG_X_HIVE_QUEEN: p->drones += 2; p->dmg_mult *= 1.1f; break;
    default: break;
    }
}

bool upg2_capped(int u, const Stats *all) {
    switch (u) {
    case UPG_SPARE_PARTS: return all->max_hp >= 24;
    case UPG_BUCKSHOT: return all->extra_proj >= 4;
    case UPG_X_HIVE_QUEEN: return all->drones >= MAX_DRONES;
    case UPG_QUICK_HANDS:
    case UPG_X_SUPERCHARGER: { /* nothing to recharge without an active item */
        int owned[ACT_COUNT];
        return actives_owned(owned) == 0;
    }
    default: return false;
    }
}

const char *upg2_desc_at(int u, char *b, int size) {
    switch (u) {
    case UPG_SCATTERBRAIN: SDL_snprintf(b, (size_t)size, "Volleys: %d%% chance to throw|3 shots in random directions", pct2(proc2(SCATTER_CHANCE))); return b;
    case UPG_CONCUSSION: SDL_snprintf(b, (size_t)size, "Hits: %d%% chance to stun|enemies for 0.7 s", pct2(proc2(CONCUSSION_CHANCE))); return b;
    default: return NULL;
    }
}

void upg2_grant(int u) {
    Player *p = &G.pl;
    switch (u) {
    case UPG_SPARE_PARTS: p->hp = p->hp + 2 > p->max_hp ? p->max_hp : p->hp + 2; p->bombs++; break;
    case UPG_LOCKSMITH: p->keys++; break;
    default: break;
    }
}

/* ---- Firing ------------------------------------------------------------------ */

float upg2_volley_damage_mult(void) {
    const Player *p = &G.pl;
    float m = 1.0f;
    if (on(UPG_ESCALATION)) m *= 1.0f + minf(0.12f * p->hold_t, 0.6f);
    if (on(UPG_KILLSTREAK)) m *= 1.0f + 0.01f * (float)p->streak;
    if (on(UPG_FULL_POWER) && p->hp >= p->max_hp) m *= 1.4f;
    return m;
}

float upg2_fire_rate_mult(void) {
    const Player *p = &G.pl;
    float m = 1.0f;
    if (on(UPG_BURST_FIRE)) m *= SDL_fmodf(G.time, 1.0f) < 0.6f ? 1.7f : 0.45f;
    if (on(UPG_STRAFE) && v2_len2(p->vel) > 80.0f * 80.0f) {
        float c = fabsf(v2_dot(v2_norm(p->vel), v2_angle(p->aim_angle)));
        if (c < 0.7f) m *= 1.35f;      /* sideways */
        else if (c > 0.85f) m *= 0.85f; /* straight at / away from the aim */
    }
    if (on(UPG_FRENZY) && p->frenzy_t > 0.0f) m *= 1.5f;
    if (on(UPG_DESPERADO) && p->hp * 2 <= p->max_hp) m *= 1.2f;
    return m;
}

void upg2_on_bullet_spawn(PBullet *b) {
    const Player *p = &G.pl;
    if (on(UPG_FULL_POWER) && p->hp >= p->max_hp) b->pierce += 1;
    if (on(UPG_DESPERADO) && p->hp * 2 <= p->max_hp) b->pierce += 3;
    if (b->pierce > 999) b->pierce = 999;
    if (on(UPG_SLUGGER)) b->knockback *= 1.5f;
}

void upg2_on_volley(V2 muzzle, float aim, float dmg, SDL_FColor col) {
    Player *p = &G.pl;
    uint32_t c = ((uint32_t)(col.r * 255.0f) << 16) | ((uint32_t)(col.g * 255.0f) << 8) | (uint32_t)(col.b * 255.0f);
    V2 side = v2_perp(v2_angle(aim));
    if (on(UPG_TWIN_BARREL))
        for (int k = -1; k <= 1; k += 2) shot2(v2_add(muzzle, v2_scale(side, 16.0f * (float)k)), aim, 1150.0f, 4.0f, dmg * 0.6f, 0.7f, 0x9ad7ff, true);
    if (on(UPG_HELIX))
        for (int k = 0; k < 2; k++) {
            PBullet *b = pbullet_spawn(muzzle, v2_scale(v2_angle(aim), 1000.0f * p->shot_speed_mult), 4.5f, dmg * 0.5f, col_hex(0x5dffd8, 1));
            if (!b) break;
            b->life = 0.75f * p->range_mult;
            b->knockback = 20.0f;
            b->flags |= PB_WAVE | (k ? PB_FLIP : 0);
            pbullet_arm(b, true);
            if (k) b->flags |= PB_FLIP;
            else b->flags &= ~PB_FLIP;
        }
    if (on(UPG_SCATTERBRAIN) && chance(proc2(SCATTER_CHANCE)))
        for (int k = 0; k < 3; k++) shot2(muzzle, aim + frand(-PI_F, PI_F), 1000.0f, 4.0f, dmg * 0.7f, 0.6f, c, true);
    if (on(UPG_OPENING_SALVO) && p->salvo_ready && fighting()) {
        p->salvo_ready = false;
        V2 at = G.in.mouse;
        explode_at(at, 170.0f, dmg * 4.0f, NULL, 0xffd36b);
        fx_particle(at, v2(0, 0), 0.4f, 340.0f, col_hex(0xffd36b, 0.9f), 0.0f, PT_RING);
        sfx_play_at(SFX_EXPLODE_BIG, at);
    }
    if (on(UPG_TIME_BOMB) && p->volleys % 6 == 0) {
        PBullet *b = shot2(muzzle, aim, 450.0f, 9.0f, dmg * 3.0f, 1.4f, 0xff6a2a, true);
        if (b) b->flags |= PB_EXPLOSIVE;
    }
    if (on(UPG_X_ANNIHILATOR) && ++p->nuke_volleys >= 10 && p->nuke_t <= 0.0f) {
        Enemy *t = nearest_to(G.in.mouse, 2400.0f);
        if (t) {
            p->nuke_volleys = 0;
            p->nuke_t = 0.6f;
            p->nuke_pos = t->pos;
            p->nuke_dmg = dmg * 10.0f;
            sfx_play_ex(SFX_SNIPER_CHARGE, 0.7f, 0.6f, 0.0f);
        }
    }
}

/* ---- Hits ---------------------------------------------------------------------- */

float upg2_hit_damage(const PBullet *b, const Enemy *e, float dmg) {
    const Player *p = &G.pl;
    (void)b;
    if (on(UPG_ACID)) dmg *= 1.0f + e->acid;
    if (on(UPG_HUNTERS_MARK) && e->uid == p->mark_uid) dmg *= 2.0f;
    if (on(UPG_COMBO)) dmg *= 1.0f + 0.02f * (float)p->combo;
    if (on(UPG_FIELD_NOTES)) dmg *= 1.0f + 0.02f * (float)p->notes[e->type];
    if (on(UPG_FIREPROOF) && e->burn_t > 0.0f) dmg *= 1.2f;
    /* HEADSMAN: a hit that leaves it under 30% finishes it (never a boss). */
    if (on(UPG_X_EXECUTIONER) && !enemy_is_boss(e) && e->hp - dmg < e->max_hp * 0.3f) {
        fx_particle(e->pos, v2(0, 0), 0.3f, e->radius * 3.0f, col_hex(0xff4466, 1), 0.0f, PT_RING);
        return e->hp + 1.0f;
    }
    return dmg;
}

void upg2_after_hit(PBullet *b, Enemy *e, float dmg) {
    Player *p = &G.pl;
    b->hit_any = true;
    bool byproduct = (b->flags & PB_BYPRODUCT) != 0;
    if (on(UPG_ACID)) e->acid = minf(e->acid + 0.05f, 0.4f);
    if (on(UPG_CHROMA)) {
        switch (irand(0, 2)) {
        case 0: e->slow_t = maxf(e->slow_t, 1.5f); break;
        case 1:
            e->poison_dps = maxf(e->poison_t > 0.0f ? e->poison_dps : 0.0f, dmg * 0.4f);
            e->poison_t = maxf(e->poison_t, 3.0f);
            break;
        default:
            e->burn_dps = maxf(e->burn_t > 0.0f ? e->burn_dps : 0.0f, dmg * 0.8f);
            e->burn_t = maxf(e->burn_t, 2.0f);
            break;
        }
    }
    if (on(UPG_VAMPIRE) && p->vamp_room < 2) {
        p->vamp = minf(p->vamp + dmg, VAMPIRE_DAMAGE);
        if (p->vamp >= VAMPIRE_DAMAGE && p->hp < p->max_hp) {
            p->vamp = 0.0f;
            p->vamp_room++;
            heal_half(0xd01040);
        }
    }
    if (byproduct) return; /* no chain reactions from fragments, mines, vortices */
    if (on(UPG_COMBO) && p->combo < 30) p->combo++;
    if (on(UPG_STATIC) && ++e->static_n >= 5) {
        e->static_n = 0;
        upg_bolt(v2_add(e->pos, v2(0, -40)), e, dmg * 2.0f, 3);
        sfx_play_ex(SFX_DRONE_SHOT, 0.5f, 0.6f, 0.0f);
    }
    if (on(UPG_CONCUSSION) && chance(proc2(CONCUSSION_CHANCE))) {
        stun(e, 0.7f);
        fx_particle(e->pos, v2(0, 0), 0.2f, e->radius * 2.6f, col_hex(0xfff0b0, 0.9f), 0.0f, PT_RING);
    }
    if (on(UPG_SPLASH)) {
        for (int i = 0; i < G.nen; i++) {
            Enemy *o = &G.en[i];
            float r = 70.0f + o->radius;
            if (o == e || o->hp <= 0.0f || v2_dist2(o->pos, e->pos) > r * r) continue;
            enemy_damage(o, dmg * 0.25f, v2(0, 0));
        }
        if ((G.ticks & 3) == 0) fx_particle(e->pos, v2(0, 0), 0.15f, 140.0f, col_hex(0x66e0ff, 0.35f), 0.0f, PT_RING);
    }
    /* PRISM HEART: a shot's first hit splits it into two more (only real shots of ours). */
    if (on(UPG_X_PRISM_HEART) && b->armed && !(b->flags & PB_PRISMED) && b >= G.pb && b < G.pb + G.npb && G.npb < MAX_PBULLETS - 400) {
        b->flags |= PB_PRISMED;
        for (int k = -1; k <= 1; k += 2) {
            PBullet *c = &G.pb[G.npb++];
            *c = *b;
            c->vel = v2_rotate(b->vel, 0.4f * (float)k);
            c->damage *= 0.6f;
            c->last_hit = e->uid;
            c->life = maxf(c->life, 0.3f);
        }
        fx_particle(b->pos, v2(0, 0), 0.15f, 40.0f, col_hex(0xd8a0ff, 0.8f), 0.0f, PT_RING);
    }
}

void upg2_bullet_death(const PBullet *b) {
    /* COMBO METER: a real shot that dies without having hit anything is a miss. */
    if (on(UPG_COMBO) && b->armed && !b->hit_any && !(b->flags & PB_BYPRODUCT) && G.pl.combo > 0) {
        if (G.pl.combo >= 10) fx_popup(v2_add(G.pl.pos, v2(0, -48)), col_hex(0xffe14d, 0.8f), "COMBO LOST");
        G.pl.combo = 0;
    }
}

/* ---- Kills ----------------------------------------------------------------------- */

void upg2_on_kill(const Enemy *e) {
    Player *p = &G.pl;
    if (on(UPG_FIELD_NOTES) && p->notes[e->type] < 20) p->notes[e->type]++;
    if (on(UPG_KILLSTREAK) && p->streak < 50) p->streak++;
    if (on(UPG_FRENZY)) p->frenzy_t = 1.5f;
    if (on(UPG_SLOW_MOTION)) p->slowmo_t = minf(p->slowmo_t + 1.0f, 3.0f);
    if (on(UPG_QUICK_HANDS))
        for (int a = 1; a < ACT_COUNT; a++)
            if (p->act[a].level > 0) p->act[a].cd = maxf(0.0f, p->act[a].cd - 0.4f);
    if (on(UPG_WISPS)) {
        Enemy *t = NULL;
        float bd = 900.0f * 900.0f;
        for (int i = 0; i < G.nen; i++) {
            Enemy *o = &G.en[i];
            float d2 = v2_dist2(o->pos, e->pos);
            if (o != e && o->hp > 0.0f && d2 < bd) bd = d2, t = o;
        }
        float a = t ? v2_heading(v2_sub(t->pos, e->pos)) : frand(0.0f, TAU_F);
        PBullet *b = shot2(e->pos, a, 600.0f, 5.0f, 8.0f, 1.3f, 0xb0ffea, false);
        if (b) {
            b->homing += 8.0f;
            b->last_hit = e->uid;
        }
    }
    if (on(UPG_SUPERNOVA) && ++p->nova_kills >= 20) {
        p->nova_kills = 0;
        ring2(p->pos, 24, 10.0f, 950.0f, 0.6f, 0xfff5c0);
        fx_particle(p->pos, v2(0, 0), 0.4f, 300.0f, col_hex(0xfff5c0, 0.9f), 0.0f, PT_RING);
        sfx_play(SFX_POWER_UP);
    }
    /* BOWLING BALL: the wreck flies on, away from the ship, and smashes into the others. */
    if (on(UPG_BOWLING) && !enemy_is_boss(e) && !e->fake) {
        V2 d = v2_sub(e->pos, p->pos);
        float a = v2_len2(d) > 1.0f ? v2_heading(d) : frand(0.0f, TAU_F);
        PBullet *b = pbullet_spawn(e->pos, v2_scale(v2_angle(a), 900.0f), maxf(6.0f, e->radius * 0.7f),
                                   minf(0.4f * e->max_hp, 150.0f), col_hex(ENEMIES[e->type].color, 1));
        if (b) {
            b->life = 0.5f;
            b->pierce = 3;
            b->knockback = 250.0f;
            b->last_hit = e->uid;
            b->flags = PB_FRAGMENT;
        }
    }
    if (on(UPG_BOUNTY) && e->uid == p->bounty_uid && !p->bounty_done) {
        p->bounty_done = true;
        fx_popup(v2_add(e->pos, v2(0, -30)), col_hex(0xffd23f, 1), "BOUNTY: +5 ON CLEAR");
        sfx_play(SFX_COIN);
    }
    if (on(UPG_X_EXECUTIONER) && ++p->exec_kills >= 20) {
        p->exec_kills = 0;
        heal_half(0xff4466);
    }
}

/* ---- Player events ------------------------------------------------------------- */

void upg2_on_hurt(void) {
    Player *p = &G.pl;
    p->streak = 0;
    if (on(UPG_PHASE_CLOAK)) p->invuln += 1.0f;
    if (on(UPG_LAST_RESORT) && !p->resort_used && p->hp > 0 && p->hp <= 2) {
        p->resort_used = true;
        p->resort_pending = true; /* set off next tick, outside the bullet loops */
    }
    if (on(UPG_COLD_SNAP)) {
        for (int i = 0; i < G.nen; i++) {
            Enemy *e = &G.en[i];
            if (e->hp <= 0.0f || v2_dist2(e->pos, p->pos) > 260.0f * 260.0f) continue;
            stun(e, 1.2f);
            e->slow_t = maxf(e->slow_t, 3.0f);
        }
        fx_particle(p->pos, v2(0, 0), 0.4f, 520.0f, col_hex(0xb8f0ff, 0.8f), 0.0f, PT_RING);
    }
}

/* PHOENIX FEATHER: once per run, a lethal hit becomes a full heal in a burst of fire. */
bool upg2_lethal_hit(void) {
    Player *p = &G.pl;
    if (!on(UPG_X_PHOENIX) || p->phoenix_used) return false;
    p->phoenix_used = true;
    p->hp = p->max_hp;
    p->invuln = 2.5f;
    /* Called from inside the enemy bullet loop: mark bullets dead, the loop removes them. */
    for (int i = 0; i < G.neb; i++) G.eb[i].life = -1.0f;
    for (int i = 0; i < G.nen; i++) {
        Enemy *e = &G.en[i];
        if (e->hp <= 0.0f) continue;
        enemy_damage(e, 150.0f * aura(), v2_scale(v2_norm(v2_sub(e->pos, p->pos)), 500.0f));
        e->burn_dps = maxf(e->burn_t > 0.0f ? e->burn_dps : 0.0f, 30.0f * aura());
        e->burn_t = maxf(e->burn_t, 3.0f);
    }
    sfx_play(SFX_BOMB);
    fx_particle(p->pos, v2(0, 0), 0.9f, 1400.0f, col_hex(0xff8a3d, 0.9f), 0.0f, PT_RING);
    fx_banner("PHOENIX FEATHER", "reborn from the ashes - once", col_hex(0xff8a3d, 1), 2.0f);
    return true;
}

void upg2_on_active(void) {
    if (on(UPG_X_SUPERCHARGER)) ring2(G.pl.pos, 16, 9.0f, 1000.0f, 0.55f, 0x7dfcff);
}

void upg2_on_bomb(void) {
    Player *p = &G.pl;
    if (on(UPG_CLUSTER_BOMBS))
        for (int k = 0; k < 8; k++) {
            V2 at = v2_add(p->pos, v2_scale(v2_angle(TAU_F * (float)k / 8.0f + 0.4f), 230.0f));
            explode_at(at, 110.0f, 40.0f * aura(), NULL, 0xff8a3d);
        }
    if (on(UPG_CRYO_BOMB)) {
        for (int i = 0; i < G.nen; i++) {
            stun(&G.en[i], 2.5f);
            G.en[i].slow_t = maxf(G.en[i].slow_t, 4.0f);
        }
        fx_particle(p->pos, v2(0, 0), 0.6f, 1300.0f, col_hex(0x8fe9ff, 0.9f), 0.0f, PT_RING);
    }
}

/* DEMOLISHER: a block broken by the ship goes off like a small bomb. */
void upg2_on_block_break(V2 at) {
    if (!on(UPG_DEMOLISHER)) return;
    explode_at(at, 110.0f, 30.0f * aura(), NULL, 0xffa64d);
}

bool upg_armor_piercing(void) { return on(UPG_ARMOR_PIERCER) > 0; }
bool upg_fireproof(void) { return on(UPG_FIREPROOF) > 0; }
bool upg_ghost_hull(void) { return on(UPG_X_GHOST_HULL) > 0; }
float upg_enemy_tempo(void) { return on(UPG_X_CHRONOSPHERE) ? 0.8f : 1.0f; }

/* ---- Every tick ------------------------------------------------------------------ */

void upg2_update(void) {
    Player *p = &G.pl;
    bool fight = fighting();
    if (p->frenzy_t > 0.0f) p->frenzy_t -= DT;
    if (p->slowmo_t > 0.0f) p->slowmo_t -= DT;
    if (p->nuke_t <= 0.0f && p->nuke_dmg > 0.0f && !on(UPG_X_ANNIHILATOR)) p->nuke_dmg = 0.0f;
    p->hold_t = weapon_trigger() && on(UPG_ESCALATION) ? p->hold_t + DT : 0.0f;

    if (p->resort_pending) { /* LAST RESORT: the free bomb */
        p->resort_pending = false;
        if (on(UPG_LAST_RESORT) && p->hp > 0) {
            player_bomb_blast();
            fx_popup(v2_add(p->pos, v2(0, -60)), col_hex(0xff8a3d, 1), "LAST RESORT!");
        }
    }
    /* NANITES: slow repairs while hurt, during a fight. */
    if (on(UPG_NANITES) && fight && p->hp < p->max_hp) {
        p->regen_t += DT;
        if (p->regen_t >= 40.0f) {
            p->regen_t = 0.0f;
            heal_half(0x66ffb0);
        }
    }
    /* HUNTER'S MARK: a new mark once the last one is gone. */
    if (on(UPG_HUNTERS_MARK) && fight && !by_uid2(p->mark_uid)) {
        Enemy *t = toughest();
        p->mark_uid = t ? t->uid : 0;
    }
    /* BOUNTY BOARD: one wanted enemy per room (another one if it vanished without being killed). */
    if (on(UPG_BOUNTY) && fight && !p->bounty_done && !by_uid2(p->bounty_uid)) {
        int cand[MAX_ENEMIES], n = 0;
        for (int i = 0; i < G.nen; i++)
            if (G.en[i].hp > 0.0f && !G.en[i].fake && !enemy_is_boss(&G.en[i])) cand[n++] = i;
        p->bounty_uid = n ? G.en[cand[irand(0, n - 1)]].uid : 0;
    }
    /* REPULSOR FIELD: incoming enemy bullets are bent away. */
    if (on(UPG_REPULSOR) && !active_time_stop_on()) {
        for (int i = 0; i < G.neb; i++) {
            EBullet *b = &G.eb[i];
            V2 d = v2_sub(b->pos, p->pos);
            float l2 = v2_len2(d), s2 = v2_len2(b->vel);
            if (b->life <= 0.0f || ebullet_airborne(b) || l2 > REPULSOR_R * REPULSOR_R || l2 < 1.0f || s2 < 100.0f) continue;
            if (v2_dot(d, b->vel) >= 0.0f) continue; /* already going away */
            float s = sqrtf(s2);
            V2 v = v2_add(b->vel, v2_scale(d, 1500.0f * DT / sqrtf(l2)));
            b->vel = v2_scale(v2_norm(v), s);
        }
    }
    if (!fight) return;
    /* SHOCKWAVE: an enemy too close sets off a pulse. */
    if (on(UPG_SHOCKWAVE)) {
        p->shock_cd -= DT;
        if (p->shock_cd <= 0.0f && nearest_to(p->pos, SHOCK_R + 20.0f)) {
            p->shock_cd = 2.5f;
            for (int i = 0; i < G.nen; i++) {
                Enemy *e = &G.en[i];
                float r = SHOCK_R + 60.0f + e->radius;
                if (e->hp > 0.0f && v2_dist2(e->pos, p->pos) < r * r)
                    enemy_damage(e, 25.0f * aura(), v2_scale(v2_norm(v2_sub(e->pos, p->pos)), 650.0f));
            }
            fx_particle(p->pos, v2(0, 0), 0.3f, 2.0f * (SHOCK_R + 60.0f), col_hex(0xd0e8ff, 0.9f), 0.0f, PT_RING);
            sfx_play_ex(SFX_THUD, 0.8f, 1.3f, 0.0f);
        }
    }
    /* REAR GUARD: a shot at whatever is behind the ship. */
    if (on(UPG_REAR_GUARD) && (p->rear_cd -= DT) <= 0.0f) {
        Enemy *best = NULL;
        float bd = 900.0f * 900.0f;
        V2 fwd = v2_angle(p->aim_angle);
        for (int i = 0; i < G.nen; i++) {
            Enemy *e = &G.en[i];
            V2 d = v2_sub(e->pos, p->pos);
            float d2 = v2_len2(d);
            if (e->hp <= 0.0f || e->fake || v2_dot(d, fwd) > 0.0f || d2 > bd) continue;
            bd = d2;
            best = e;
        }
        if (best) {
            p->rear_cd = 0.4f;
            shot2(p->pos, v2_heading(v2_sub(best->pos, p->pos)), 1100.0f, 4.0f, 9.0f, 0.8f, 0x7dd3ff, false);
        }
    }
    /* AFTERIMAGE: the ship's wake fires a homing shot. */
    if (on(UPG_AFTERIMAGE) && v2_len2(p->vel) > 150.0f * 150.0f && (p->afterimage_cd -= DT) <= 0.0f) {
        p->afterimage_cd = 1.2f;
        Enemy *t = nearest_to(p->pos, 1400.0f);
        V2 from = v2_sub(p->pos, v2_scale(v2_norm(p->vel), 22.0f));
        PBullet *b = shot2(from, t ? v2_heading(v2_sub(t->pos, from)) : p->aim_angle, 700.0f, 5.0f, 10.0f, 1.0f, 0x9aa8ff, false);
        if (b) b->homing += 7.0f;
        fx_particle(from, v2(0, 0), 0.3f, 30.0f, col_hex(0x9aa8ff, 0.6f), 0.0f, PT_GLOW);
    }
    /* SENTRY GUN: the turret left at the room's entry. */
    if (on(UPG_SENTRY) && p->sentry_on && (p->sentry_cd -= DT) <= 0.0f) {
        Enemy *t = NULL;
        float bd = 750.0f * 750.0f;
        for (int i = 0; i < G.nen; i++) {
            Enemy *e = &G.en[i];
            float d2 = v2_dist2(e->pos, p->sentry_pos);
            if (e->hp > 0.0f && !e->fake && !enemy_untouchable(e) && d2 < bd && room_line_clear(p->sentry_pos, e->pos, 4.0f)) bd = d2, t = e;
        }
        if (t) {
            p->sentry_cd = 0.35f;
            shot2(p->sentry_pos, v2_heading(v2_sub(t->pos, p->sentry_pos)), 1000.0f, 4.0f, 8.0f, 0.8f, 0x8ab8ff, false);
        }
    }
    /* SMITE: a judgement from above on the toughest enemy. */
    if (on(UPG_SMITE) && (p->smite_cd -= DT) <= 0.0f) {
        Enemy *t = toughest();
        if (t) {
            p->smite_cd = 5.0f;
            enemy_damage(t, t->max_hp * (enemy_is_boss(t) ? 0.015f : 0.1f), v2(0, 0));
            beam_fx(t->pos, 0xfff07a);
            fx_particle(t->pos, v2(0, 0), 0.3f, t->radius * 4.0f, col_hex(0xfff07a, 0.9f), 0.0f, PT_RING);
            sfx_play_at(SFX_EXPLODE_SMALL, t->pos);
        }
    }
    /* THUNDERHEAD: while the trigger is held, lightning near the cursor. */
    if (on(UPG_THUNDERHEAD) && weapon_trigger() && (p->thunder_cd -= DT) <= 0.0f) {
        Enemy *t = nearest_to(G.in.mouse, 500.0f);
        if (t) {
            p->thunder_cd = 3.0f;
            upg_bolt(v2_add(t->pos, v2(0, -500.0f)), t, 40.0f * aura(), 2);
            sfx_play_at(SFX_EXPLODE_SMALL, t->pos);
        }
    }
    /* ORBITAL ARRAY: four wide orbs. */
    if (on(UPG_X_ORBITAL_ARRAY)) {
        for (int k = 0; k < 4; k++) {
            V2 o = array_orb(k);
            for (int i = 0; i < G.neb; i++) {
                EBullet *b = &G.eb[i];
                float r = 15.0f + b->radius * 0.6f;
                if (b->life > 0.0f && !ebullet_airborne(b) && v2_dist2(b->pos, o) < r * r) {
                    b->life = -1.0f;
                    fx_particle(b->pos, v2(0, 0), 0.15f, 14.0f, col_hex(0xffe3a0, 1), 0.0f, PT_GLOW);
                }
            }
            for (int i = 0; i < G.nen; i++) {
                Enemy *e = &G.en[i];
                float r = e->radius + 15.0f;
                if (e->hp > 0.0f && v2_dist2(e->pos, o) < r * r) enemy_damage(e, 120.0f * aura() * DT, v2(0, 0));
            }
        }
    }
    /* TEMPEST: everything inside the storm takes damage (like the TESLA FIELD, armour ignored). */
    if (on(UPG_X_TEMPEST)) {
        float dps = 45.0f * aura();
        for (int i = 0; i < G.nen; i++) {
            Enemy *e = &G.en[i];
            float r = TEMPEST_R + e->radius;
            if (e->hp <= 0.0f || enemy_untouchable(e) || v2_dist2(e->pos, p->pos) > r * r) continue;
            e->hp -= enemy_damage_cap(e, dps * DT);
            G.last_enemy_damage_t = G.time;
            if ((G.ticks + e->uid * 5) % 20 == 0)
                fx_particle(e->pos, v2(frand(-60, 60), frand(-60, 60)), 0.2f, 12.0f, col_hex(0x9db8ff, 0.8f), 0.0f, PT_GLOW);
        }
    }
    /* RAILSTORM: a rail through everything at the nearest enemy. */
    if (on(UPG_X_RAILSTORM) && (p->rail_cd -= DT) <= 0.0f) {
        Enemy *t = nearest_to(p->pos, 2000.0f);
        if (t) {
            p->rail_cd = 1.6f;
            PBullet *b = shot2(p->pos, v2_heading(v2_sub(t->pos, p->pos)), 2400.0f, 6.0f, 60.0f, 0.6f, 0xe0f4ff, false);
            if (b) {
                b->pierce = 999;
                b->style = BS_RAIL;
                b->knockback = 120.0f;
            }
            sfx_play_ex(SFX_SHOT_RAIL, 0.7f, 1.2f, 0.0f);
        }
    }
    /* ANNIHILATOR: the strike called down lands. */
    if (p->nuke_t > 0.0f && (p->nuke_t -= DT) <= 0.0f && on(UPG_X_ANNIHILATOR)) {
        explode_at(p->nuke_pos, NUKE_R, p->nuke_dmg, NULL, 0xff2050);
        beam_fx(p->nuke_pos, 0xff2050);
        fx_explosion(p->nuke_pos, NUKE_R, col_hex(0xff2050, 1));
        fx_shake(0.5f);
        sfx_play_at(SFX_EXPLODE_BIG, p->nuke_pos);
    }
    /* EVENT ENGINE: a black hole where the enemies are thickest. */
    if (on(UPG_X_SINGULARITY) && (p->sing_cd -= DT) <= 0.0f) {
        int best = -1, best_n = 0;
        for (int i = 0; i < G.nen; i++) {
            if (G.en[i].hp <= 0.0f) continue;
            int n = 0;
            for (int j = 0; j < G.nen; j++) n += G.en[j].hp > 0.0f && v2_dist2(G.en[i].pos, G.en[j].pos) < 200.0f * 200.0f;
            if (n > best_n) best_n = n, best = i;
        }
        if (best >= 0) {
            p->sing_cd = 7.0f;
            upg_vortex(G.en[best].pos, 2.5f, 80.0f * aura());
        }
    }
    /* DOOMSDAY CLOCK: every enemy loses a quarter of its health (a boss far less). */
    if (on(UPG_X_DOOMSDAY)) {
        p->doom_cd += DT;
        if (p->doom_cd >= 12.0f) {
            p->doom_cd = 0.0f;
            for (int i = 0; i < G.nen; i++) {
                Enemy *e = &G.en[i];
                if (e->hp <= 0.0f || enemy_untouchable(e)) continue;
                e->hp -= enemy_damage_cap(e, e->max_hp * (enemy_is_boss(e) ? 0.04f : 0.25f));
                e->flash = 0.1f;
                fx_particle(e->pos, v2(0, 0), 0.35f, e->radius * 3.5f, col_hex(0xb0103a, 0.9f), 0.0f, PT_RING);
            }
            G.last_enemy_damage_t = G.time;
            fx_shake(0.3f);
            sfx_play(SFX_BOMB);
        }
    }
}

void upg2_on_room_enter(void) {
    Player *p = &G.pl;
    bool fight = !G.room.cleared;
    p->salvo_ready = fight;
    p->resort_used = p->resort_pending = false;
    p->sentry_on = fight && on(UPG_SENTRY);
    p->sentry_pos = p->pos;
    p->sentry_cd = 0.5f;
    p->mark_uid = p->bounty_uid = 0;
    p->bounty_done = false;
    p->vamp_room = 0;
    p->nuke_t = 0.0f;
    p->nuke_volleys = 0;
    p->hold_t = p->frenzy_t = p->slowmo_t = 0.0f;
    if (on(UPG_X_OVERSHIELD) && fight && p->shield < 4) player_add_shield(4 - p->shield);
}

void upg2_on_room_clear(void) {
    Player *p = &G.pl;
    p->sentry_on = false;
    if (on(UPG_SHIELD_BATTERY) && !p->room_hit && p->shield < 4) {
        player_add_shield(1);
        fx_popup(v2_add(p->pos, v2(0, -96)), col_hex(0x66d9ff, 1), "+1/2 SHIELD");
    }
    if (on(UPG_INTEREST) && p->coins >= 10) {
        int gain = p->coins / 10 > 8 ? 8 : p->coins / 10;
        p->coins += gain;
        fx_popup(v2_add(p->pos, v2(0, -108)), col_hex(0xffd23f, 1), "INTEREST +%d", gain);
    }
    if (on(UPG_LOCKSMITH) && ++p->lock_rooms >= 5) {
        p->lock_rooms = 0;
        p->keys++;
        fx_popup(v2_add(p->pos, v2(0, -120)), col_hex(0xd9e3f0, 1), "+1 KEY");
    }
    if (on(UPG_BOUNTY) && p->bounty_done) {
        p->coins += 5;
        p->bounty_done = false;
        fx_popup(v2_add(p->pos, v2(0, -132)), col_hex(0xffd23f, 1), "BOUNTY +5 COINS");
    }
}

/* ---- Drawing ------------------------------------------------------------------------ */

void upg2_draw(void) {
    const Player *p = &G.pl;
    draw_blend_add();
    if (on(UPG_SENTRY) && p->sentry_on) {
        V2 s = p->sentry_pos;
        Enemy *t = nearest_to(s, 750.0f);
        float a = t ? v2_heading(v2_sub(t->pos, s)) : G.time;
        draw_glow(s, 30.0f, col_hex(0x8ab8ff, 0.25f));
        draw_ngon_outline(s, 13.0f, 6, 0.0f, 2.0f, col_hex(0x8ab8ff, 0.9f));
        draw_line(s, v2_add(s, v2_scale(v2_angle(a), 18.0f)), 4.0f, col_hex(0xffffff, 0.8f));
    }
    if (on(UPG_X_ORBITAL_ARRAY))
        for (int k = 0; k < 4; k++) {
            V2 o = array_orb(k);
            draw_glow(o, 30.0f, col_hex(0xffe3a0, 0.35f));
            draw_circle(o, 11.0f, col_hex(0xffe3a0, 1));
            draw_circle(o, 5.0f, COL_WHITE);
        }
    if (on(UPG_X_TEMPEST)) {
        draw_ring(p->pos, TEMPEST_R, 2.0f, col_hex(0x9db8ff, 0.12f + 0.05f * sinf(G.time * 11.0f)));
        for (int k = 0; k < 3; k++) {
            float a = G.time * (2.0f + (float)k) + (float)k * 2.1f;
            draw_arc(p->pos, TEMPEST_R - 10.0f - 18.0f * (float)k, 2.0f, a, a + 0.9f, col_hex(0x9db8ff, 0.3f));
        }
    }
    if (on(UPG_REPULSOR)) draw_ring(p->pos, REPULSOR_R, 1.5f, col_hex(0x80c8ff, 0.08f + 0.04f * sinf(G.time * 6.0f)));
    if (p->nuke_t > 0.0f) { /* ANNIHILATOR: where it is about to land */
        float k = 1.0f - p->nuke_t / 0.6f;
        draw_ring(p->nuke_pos, NUKE_R * (1.4f - 0.4f * k), 3.0f, col_hex(0xff2050, 0.4f + 0.5f * k));
        draw_glow(p->nuke_pos, NUKE_R * 0.5f, col_hex(0xff2050, 0.15f + 0.2f * k));
    }
    if (on(UPG_ESCALATION) && p->hold_t > 0.5f)
        draw_arc(p->pos, 30.0f, 2.5f, -PI_F * 0.5f, -PI_F * 0.5f + TAU_F * minf(p->hold_t / 5.0f, 1.0f), col_hex(0xffb070, 0.6f));
    /* Marks on enemies: the hunted one, the wanted one, the stunned ones. */
    for (int i = 0; i < G.nen; i++) {
        const Enemy *e = &G.en[i];
        if (e->hp <= 0.0f) continue;
        if (on(UPG_HUNTERS_MARK) && e->uid == p->mark_uid) {
            float r = e->radius + 10.0f + 2.0f * sinf(G.time * 8.0f);
            draw_ring(e->pos, r, 2.0f, col_hex(0xff5050, 0.8f));
            for (int k = 0; k < 4; k++) {
                V2 d = v2_angle(PI_F * 0.5f * (float)k + G.time);
                draw_line(v2_add(e->pos, v2_scale(d, r - 4.0f)), v2_add(e->pos, v2_scale(d, r + 8.0f)), 2.0f, col_hex(0xff5050, 0.8f));
            }
        }
        if (on(UPG_BOUNTY) && e->uid == p->bounty_uid && !p->bounty_done) {
            V2 c = v2_add(e->pos, v2(0, -e->radius - 16.0f));
            draw_circle(c, 6.0f, col_hex(0xffd23f, 0.9f));
            draw_ring(c, 8.0f, 1.5f, col_hex(0xfff0a0, 0.9f));
        }
        if (e->stun_t > 0.0f)
            for (int k = 0; k < 3; k++) {
                V2 d = v2_angle(G.time * 5.0f + TAU_F * (float)k / 3.0f);
                draw_circle(v2_add(v2_add(e->pos, v2(0, -e->radius - 6.0f)), v2(d.x * (e->radius * 0.7f), d.y * 4.0f)), 3.0f,
                            col_hex(0xfff0b0, 0.9f));
            }
    }
    draw_blend_alpha();
}
