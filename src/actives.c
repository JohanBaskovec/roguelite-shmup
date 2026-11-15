#include "audio.h"
#include "draw.h"
#include "game.h"

/*
 * Active items: the ship starts without any; active rooms (RW_ACTIVE) offer a choice of 3.
 * Every item taken is kept (Player.act[], indexed by ActiveId): SPACE / SHIFT / right click
 * triggers all the ones turned on and ready at once, then each recharges on its own. Taking
 * one already owned levels it up: longer effects, bigger explosions, faster recharge. Each
 * can be turned off / on in the TAB menu (active_toggle). Every recharge time also scales
 * with the RECHARGE stat (Player.dash_cooldown, which is the dash's own recharge).
 */

const ActiveDef ACTIVES[ACT_COUNT] = {
    [ACT_NONE] = {"NONE", 0x8899aa, 0.0f},
    [ACT_DASH] = {"DASH", 0x7dfcff, DASH_BASE_CD},
    [ACT_SHIELD] = {"SHIELD", 0x9fe8ff, 10.0f},
    [ACT_BOOM] = {"BOOM", 0xff8a3d, 6.0f},
    [ACT_STRENGTH] = {"STRENGTH", 0xff4d4d, 18.0f},
    [ACT_CHAOS] = {"CHAOS", 0xd77dff, 40.0f},
    [ACT_REVERSAL] = {"REVERSAL", 0x7dffef, 20.0f},
    [ACT_TURRET] = {"TURRET", 0x9dff6a, 18.0f},
    [ACT_BLINK] = {"BLINK", 0xc08aff, 3.5f},
    [ACT_NOVA] = {"NOVA", 0xfff07a, 5.0f},
    [ACT_TIME_STOP] = {"TIME STOP", 0x8ab8ff, 22.0f},
    [ACT_BLADES] = {"BLADE STORM", 0xe0e8ff, 14.0f},
};

/* ---- Numbers per level ------------------------------------------------------- */

static int lv(int level) { return (level < 1 ? 1 : level > MAX_ACTIVE_LEVEL ? MAX_ACTIVE_LEVEL : level) - 1; }

static float base_duration(int a, int level) {
    switch (a) {
    case ACT_DASH: return 0.13f + 0.02f * (float)lv(level);
    case ACT_SHIELD: return 2.5f + 0.5f * (float)lv(level);
    case ACT_STRENGTH: return 4.0f + 1.0f * (float)lv(level);
    case ACT_CHAOS: return 8.0f + 1.5f * (float)lv(level);
    case ACT_TURRET: return 8.0f + 1.5f * (float)lv(level);
    case ACT_BLINK: return 0.4f; /* invulnerable after the jump */
    case ACT_TIME_STOP: return 2.5f + 0.5f * (float)lv(level);
    case ACT_BLADES: return 5.0f + 1.0f * (float)lv(level);
    default: return 0.0f;
    }
}

/* ACTIVE TIME malus: every effect shorter. */
float active_duration(int a, int level) { return base_duration(a, level) * fmaxf(0.3f, 1.0f - malus_pct(MAL_ACTIVE_TIME)); }

int active_chaos_count(int level) { return 3 + 2 * lv(level); }
static float shield_radius(int level) { return 34.0f + 5.0f * (float)lv(level); }
static float boom_radius(int level) { return 70.0f * (1.0f + 0.15f * (float)lv(level)); }
static float boom_power(int level) { return 1.0f + 0.25f * (float)lv(level); } /* damage multiplier */
static float power(int level) { return 1.0f + 0.25f * (float)lv(level); }      /* generic damage scaling */
static float turret_rate(int level) { return 0.12f * powf(0.9f, (float)lv(level)); }
static float blink_range(int level) { return 520.0f + 60.0f * (float)lv(level); }
static int nova_count(int level) { return 24 + 4 * lv(level); }
static int blade_count(int level) { return 3 + lv(level) / 2; }
#define BLADE_ORBIT 88.0f

static V2 blade_pos(int k, int n) { return v2_add(G.pl.pos, v2_scale(v2_angle(G.pl.blade_a + TAU_F * (float)k / (float)n), BLADE_ORBIT)); }

const char *active_desc(int a, int level) {
    static char bufs[4][96];
    static int next;
    char *b = bufs[next++ & 3];
    switch (a) {
    case ACT_DASH: SDL_snprintf(b, 96, "Quick dash, invulnerable|for %.2f s", (double)active_duration(a, level)); break;
    case ACT_SHIELD: SDL_snprintf(b, 96, "Invincible for %.1f s|the bubble eats bullets", (double)active_duration(a, level)); break;
    case ACT_BOOM:
        SDL_snprintf(b, 96, "All your shots explode at once|and keep flying (x%.2f damage)", (double)(1.5f * boom_power(level)));
        break;
    case ACT_STRENGTH: SDL_snprintf(b, 96, "Quadruple shots for %.0f s", (double)active_duration(a, level)); break;
    case ACT_CHAOS:
        SDL_snprintf(b, 96, "%d random new upgrades|for %.1f s", active_chaos_count(level), (double)active_duration(a, level));
        break;
    case ACT_REVERSAL: SDL_snprintf(b, 96, "Every enemy bullet becomes|one of yours (x%.2f damage)", (double)power(level)); break;
    case ACT_TURRET:
        SDL_snprintf(b, 96, "Drops a turret that shoots|for %.1f s (%.0f shots/s)", (double)active_duration(a, level), (double)(1.0f / turret_rate(level)));
        break;
    case ACT_BLINK: SDL_snprintf(b, 96, "Teleport to the cursor (%.0f px)|with a blast at both ends", (double)blink_range(level)); break;
    case ACT_NOVA: SDL_snprintf(b, 96, "A ring of %d piercing shots", nova_count(level)); break;
    case ACT_TIME_STOP: SDL_snprintf(b, 96, "Enemies and their bullets|freeze for %.1f s", (double)active_duration(a, level)); break;
    case ACT_BLADES:
        SDL_snprintf(b, 96, "%d blades circle you for %.0f s|cutting bullets and enemies", blade_count(level), (double)active_duration(a, level));
        break;
    default: b[0] = 0; break;
    }
    return b;
}

static const char *const OPTION_NAMES[ACT_COUNT] = {
    "none", "dash", "shield", "boom", "strength", "chaos", "reversal", "turret", "blink", "nova", "timestop", "blades",
};

const char *active_option_name(int a) { return a >= 0 && a < ACT_COUNT ? OPTION_NAMES[a] : "?"; }

/* CHAOS lends upgrades whose whole effect lives in the hooks (nothing applied once by
 * upgrade_apply), so taking them back is exact. Economy / stat upgrades are excluded. */
static const int CHAOS_POOL[] = {
    UPG_FROST, UPG_VENOM, UPG_THERMITE, UPG_ARC, UPG_PAYLOAD, UPG_SHRAPNEL, UPG_CRIT, UPG_GUILLOTINE,
    UPG_BOOMERANG, UPG_PHASE, UPG_TAIL_GUN, UPG_FLANK, UPG_CAPACITOR, UPG_REACTIVE, UPG_ORB, UPG_TESLA,
    UPG_LEECH, UPG_ADRENALINE, UPG_BLOODLUST, UPG_RAM, UPG_DASH_NOVA, UPG_CHRONO, UPG_VOLATILE, UPG_MINES,
    UPG_WOUNDED_FURY, UPG_SCAR_TISSUE, UPG_LAST_STAND, UPG_BLOOD_SCENT, UPG_LOADED_DICE,
    UPG_SCOPE, UPG_POINT_BLANK, UPG_BULLY, UPG_GIANT_SLAYER, UPG_MOMENTUM, UPG_CHAIN_REACTION, UPG_TURRET_MODE,
    UPG_RUN_GUN, UPG_FROSTBITE, UPG_PYROMANIAC, UPG_TOXIC_CLOUD, UPG_OVERKILL, UPG_MISSILE_POD, UPG_ORBITAL,
    UPG_SPIKED_HULL, UPG_ECHO, UPG_EMERGENCY, UPG_RAMPAGE, UPG_BLACK_HOLE,
};

/* -12% per level, and the RECHARGE stat. */
float active_cooldown(int a, int level) {
    const Player *p = &G.pl;
    if (a <= ACT_NONE || a >= ACT_COUNT) return 0.0f;
    float base = a == ACT_DASH ? p->dash_cooldown : ACTIVES[a].cooldown * p->dash_cooldown / DASH_BASE_CD;
    return base * powf(0.88f, (float)lv(level));
}

bool active_running(int a) {
    const ActiveSlot *k = &G.pl.act[a];
    return k->level > 0 && !k->off && k->t > 0.0f;
}
int active_level_on(int a) { return G.pl.act[a].level > 0 && !G.pl.act[a].off ? G.pl.act[a].level : 0; }
bool active_strength_on(void) { return active_running(ACT_STRENGTH); }
bool active_shield_on(void) { return active_running(ACT_SHIELD); }
bool active_time_stop_on(void) { return active_running(ACT_TIME_STOP); }

int actives_owned(int *out) {
    int n = 0;
    for (int a = ACT_NONE + 1; a < ACT_COUNT; a++)
        if (G.pl.act[a].level > 0) out[n++] = a;
    return n;
}

void active_end_chaos(void) {
    Player *p = &G.pl;
    for (int u = 0; u < UPG_COUNT; u++) p->chaos[u] = false;
}

static void end_effect(int a) {
    Player *p = &G.pl;
    if (a == ACT_CHAOS) {
        bool lent = false;
        for (int u = 0; u < UPG_COUNT; u++) lent |= p->chaos[u];
        if (lent) {
            active_end_chaos();
            fx_popup(v2_add(p->pos, v2(0, -40)), col_hex(ACTIVES[ACT_CHAOS].color, 1), "CHAOS FADES");
        }
    }
    if (a == ACT_DASH) p->dash_t = 0.0f;
    p->act[a].t = 0.0f;
}

void active_give(int a) {
    Player *p = &G.pl;
    if (a <= ACT_NONE || a >= ACT_COUNT) return;
    ActiveSlot *k = &p->act[a];
    char title[48], desc[96];
    if (k->level > 0) {
        /* Owned already: a level up (a running effect keeps going). */
        if (k->level < MAX_ACTIVE_LEVEL) k->level++;
        SDL_snprintf(title, sizeof title, "%s - LEVEL %d", ACTIVES[a].name, k->level);
    } else {
        k->level = 1;
        k->off = false;
        SDL_snprintf(title, sizeof title, "%s", ACTIVES[a].name);
    }
    k->cd = k->cd_max = 0.0f; /* ready at once */
    SDL_snprintf(desc, sizeof desc, "%s", active_desc(a, k->level));
    for (char *c = desc; *c; c++)
        if (*c == '|') *c = ' ';
    fx_banner(title, desc, col_hex(ACTIVES[a].color, 1), 2.6f);
}

/* Every new screen (room entered, or back from a secret room): all active items ready again.
 * Effects already running keep going. */
void actives_recharge_all(void) {
    for (int a = ACT_NONE + 1; a < ACT_COUNT; a++) G.pl.act[a].cd = G.pl.act[a].cd_max = 0.0f;
}

bool active_toggle(int a) {
    Player *p = &G.pl;
    if (a <= ACT_NONE || a >= ACT_COUNT || p->act[a].level == 0) return false;
    ActiveSlot *k = &p->act[a];
    k->off = !k->off;
    if (k->off && k->t > 0.0f) end_effect(a); /* off = no effect at all, a running one stops */
    G.toggles++;
    return true;
}

/* ---- Effects ---------------------------------------------------------------- */

static void use_dash(void) {
    Player *p = &G.pl;
    const Input *in = &G.in;
    V2 mv = v2_norm(v2((in->right ? 1.0f : 0.0f) - (in->left ? 1.0f : 0.0f), (in->down ? 1.0f : 0.0f) - (in->up ? 1.0f : 0.0f)));
    p->dash_dir = (mv.x != 0.0f || mv.y != 0.0f) ? mv : v2_angle(p->aim_angle);
    p->dash_t = active_duration(ACT_DASH, p->act[ACT_DASH].level);
    sfx_play_at(SFX_DASH, p->pos);
    fx_burst(p->pos, 12, 100.0f, 350.0f, 0.25f, 2.5f, col_hex(CHARACTERS[G.character].color, 1));
}

/* Every shot in flight blows up where it is, and keeps going. */
static void use_boom(void) {
    Player *p = &G.pl;
    float radius = fminf(boom_radius(p->act[ACT_BOOM].level) * sqrtf(p->bullet_size), 170.0f);
    float power = boom_power(p->act[ACT_BOOM].level);
    SDL_FColor c = col_hex(ACTIVES[ACT_BOOM].color, 1);
    int n = 0;
    for (int i = 0; i < G.npb; i++) {
        const PBullet *b = &G.pb[i];
        if (b->life <= 0.0f || (b->flags & PB_FRAGMENT)) continue;
        float dmg = fmaxf(b->damage * 1.5f, 6.0f * p->dmg_mult) * power;
        for (int j = 0; j < G.nen; j++) {
            Enemy *e = &G.en[j];
            float r = radius + e->radius;
            if (e->hp > 0.0f && v2_dist2(e->pos, b->pos) < r * r)
                enemy_damage(e, dmg, v2_scale(v2_norm(v2_sub(e->pos, b->pos)), 200.0f));
        }
        if (n++ < 200) { /* past that, the screen is white anyway */
            fx_particle(b->pos, v2(0, 0), 0.3f, radius * 2.0f, col_a(c, 0.7f), 0.0f, PT_RING);
            fx_particle(b->pos, v2(0, 0), 0.18f, radius * 1.2f, col_a(c, 0.5f), 0.0f, PT_GLOW);
        }
    }
    /* No shots out (sword, or not firing): the ship itself goes off. */
    if (n == 0 || sword_on()) {
        float big = 240.0f * (1.0f + 0.1f * (float)lv(p->act[ACT_BOOM].level));
        for (int j = 0; j < G.nen; j++) {
            Enemy *e = &G.en[j];
            float r = big + e->radius;
            if (e->hp > 0.0f && v2_dist2(e->pos, p->pos) < r * r)
                enemy_damage(e, 80.0f * p->dmg_mult * power, v2_scale(v2_norm(v2_sub(e->pos, p->pos)), 500.0f));
        }
        fx_particle(p->pos, v2(0, 0), 0.4f, big * 2.0f, col_a(c, 0.8f), 0.0f, PT_RING);
        fx_burst(p->pos, 50, 200.0f, 900.0f, 0.5f, 3.0f, c);
    }
    sfx_play(SFX_EXPLODE_BIG);
    fx_shake(0.5f);
}

static void use_chaos(void) {
    Player *p = &G.pl;
    int cand[ARRAY_LEN(CHAOS_POOL)], n = 0;
    for (int i = 0; i < ARRAY_LEN(CHAOS_POOL); i++)
        if (p->upg_count[CHAOS_POOL[i]] == 0 && !upgrade_capped(CHAOS_POOL[i])) cand[n++] = CHAOS_POOL[i];
    int want = active_chaos_count(p->act[ACT_CHAOS].level), give = n < want ? n : want;
    for (int i = 0; i < give; i++) { /* partial Fisher-Yates */
        int j = irand(i, n - 1), t = cand[i];
        cand[i] = cand[j];
        cand[j] = t;
        p->chaos[cand[i]] = true;
    }
    char sub[64];
    SDL_snprintf(sub, sizeof sub, "+%d random upgrades for %.1f s", give, (double)active_duration(ACT_CHAOS, p->act[ACT_CHAOS].level));
    fx_banner("CHAOS", sub, col_hex(ACTIVES[ACT_CHAOS].color, 1), 1.6f);
    fx_burst(p->pos, 60, 200.0f, 800.0f, 0.6f, 3.0f, col_hex(ACTIVES[ACT_CHAOS].color, 1));
}

/* Every enemy bullet turns around as one of ours (marked dead, removed by its own loop). */
static void use_reversal(void) {
    Player *p = &G.pl;
    float dmg = 10.0f * power(p->act[ACT_REVERSAL].level); /* the damage bonuses come with pbullet_arm */
    for (int i = 0; i < G.neb; i++) {
        EBullet *e = &G.eb[i];
        if (e->life <= 0.0f) continue;
        e->life = -1.0f;
        Enemy *t = enemy_nearest(e->pos, 2000.0f);
        V2 dir = t ? v2_norm(v2_sub(t->pos, e->pos)) : v2_norm(v2_sub(e->pos, p->pos));
        PBullet *b = pbullet_spawn(e->pos, v2_scale(dir, 900.0f), fmaxf(e->radius, 5.0f), dmg, col_hex(ACTIVES[ACT_REVERSAL].color, 1));
        if (!b) break;
        b->life = 1.5f;
        b->knockback = 40.0f;
        b->flags = PB_REFLECTED;
        pbullet_arm(b, false);
        fx_particle(e->pos, v2(0, 0), 0.2f, 14.0f, col_hex(ACTIVES[ACT_REVERSAL].color, 0.8f), 0.0f, PT_GLOW);
    }
    sfx_play(SFX_DEFLECT);
    fx_particle(p->pos, v2(0, 0), 0.5f, 900.0f, col_hex(ACTIVES[ACT_REVERSAL].color, 0.6f), 0.0f, PT_RING);
}

static void blast(V2 at, float radius, float dmg, uint32_t color) {
    explode_at(at, radius, dmg, NULL, color);
    sfx_play_at(SFX_EXPLODE_SMALL, at);
}

static void use_blink(void) {
    Player *p = &G.pl;
    V2 to = v2_sub(G.in.mouse, p->pos);
    float d = v2_len(to), range = blink_range(p->act[ACT_BLINK].level);
    if (d > range) to = v2_scale(to, range / d);
    V2 from = p->pos, dest = v2_add(p->pos, to);
    dest.x = clampf(dest.x, ARENA_X0 + PLAYER_BODY_R + 2.0f, ARENA_X1 - PLAYER_BODY_R - 2.0f);
    dest.y = clampf(dest.y, ARENA_Y0 + PLAYER_BODY_R + 2.0f, ARENA_Y1 - PLAYER_BODY_R - 2.0f);
    room_collide_circle(&dest, PLAYER_BODY_R, NULL); /* never inside an obstacle */
    dest.x = clampf(dest.x, ARENA_X0 + PLAYER_BODY_R, ARENA_X1 - PLAYER_BODY_R);
    dest.y = clampf(dest.y, ARENA_Y0 + PLAYER_BODY_R, ARENA_Y1 - PLAYER_BODY_R);
    p->pos = dest;
    p->vel = v2(0, 0);
    p->invuln = fmaxf(p->invuln, active_duration(ACT_BLINK, p->act[ACT_BLINK].level));
    float dmg = 40.0f * p->dmg_mult * power(p->act[ACT_BLINK].level);
    blast(from, 120.0f, dmg, ACTIVES[ACT_BLINK].color);
    blast(dest, 120.0f, dmg, ACTIVES[ACT_BLINK].color);
    for (int k = 0; k <= 12; k++)
        fx_particle(v2_lerp(from, dest, (float)k / 12.0f), v2(0, 0), 0.25f, 14.0f, col_hex(ACTIVES[ACT_BLINK].color, 0.6f), 0.0f, PT_GLOW);
    sfx_play(SFX_TELEPORT);
}

static void use_nova(void) {
    Player *p = &G.pl;
    int n = nova_count(p->act[ACT_NOVA].level);
    float off = frand(0.0f, TAU_F), dmg = 14.0f;
    for (int k = 0; k < n; k++) {
        PBullet *b = pbullet_spawn(p->pos, v2_scale(v2_angle(off + TAU_F * (float)k / (float)n), 1100.0f), 8.0f, dmg,
                                   col_hex(ACTIVES[ACT_NOVA].color, 1));
        if (!b) break;
        b->life = 0.8f;
        b->pierce = 3;
        b->knockback = 120.0f;
        pbullet_arm(b, false);
    }
    sfx_play_ex(SFX_SHOT_RAIL, 1.0f, 0.7f, 0.0f);
    fx_particle(p->pos, v2(0, 0), 0.35f, 200.0f, col_hex(ACTIVES[ACT_NOVA].color, 0.8f), 0.0f, PT_RING);
}

static void use_active(int a) {
    Player *p = &G.pl;
    ActiveSlot *k = &p->act[a];
    SDL_FColor c = col_hex(ACTIVES[a].color, 1);
    switch (a) {
    case ACT_DASH: use_dash(); break;
    case ACT_SHIELD:
        sfx_play(SFX_SHIELD);
        fx_particle(p->pos, v2(0, 0), 0.35f, 140.0f, c, 0.0f, PT_RING);
        break;
    case ACT_BOOM: use_boom(); break;
    case ACT_STRENGTH:
        sfx_play(SFX_POWER_UP);
        fx_particle(p->pos, v2(0, 0), 0.35f, 160.0f, c, 0.0f, PT_RING);
        fx_burst(p->pos, 30, 150.0f, 600.0f, 0.4f, 3.0f, c);
        break;
    case ACT_CHAOS:
        sfx_play(SFX_CHAOS);
        use_chaos();
        break;
    case ACT_REVERSAL: use_reversal(); break;
    case ACT_TURRET:
        p->turret_pos = p->pos;
        p->turret_cd = 0.0f;
        sfx_play(SFX_POWER_UP);
        fx_particle(p->pos, v2(0, 0), 0.3f, 90.0f, c, 0.0f, PT_RING);
        break;
    case ACT_BLINK: use_blink(); break;
    case ACT_NOVA: use_nova(); break;
    case ACT_TIME_STOP:
        sfx_play(SFX_TIME_STOP);
        fx_particle(p->pos, v2(0, 0), 0.6f, 2400.0f, col_a(c, 0.5f), 0.0f, PT_RING);
        break;
    case ACT_BLADES:
        sfx_play(SFX_SWORD);
        fx_particle(p->pos, v2(0, 0), 0.3f, 2.0f * BLADE_ORBIT, c, 0.0f, PT_RING);
        break;
    default: return;
    }
    k->t = a == ACT_DASH ? 0.0f : active_duration(a, k->level);
    k->cd = k->cd_max = active_cooldown(a, k->level);
}

void actives_update(void) {
    Player *p = &G.pl;
    int owned = 0;
    for (int a = ACT_NONE + 1; a < ACT_COUNT; a++) {
        ActiveSlot *k = &p->act[a];
        if (k->level == 0) continue;
        owned++;
        if (k->cd > 0.0f) {
            k->cd -= DT;
            if (k->cd <= 0.0f) {
                k->cd = 0.0f;
                if (a != ACT_DASH && !k->off) { /* the dash recharges too often to announce it */
                    sfx_play_ex(SFX_WEAPON_SWITCH, 0.7f, 1.6f, 0.0f);
                    fx_particle(p->pos, v2(0, 0), 0.25f, 70.0f, col_hex(ACTIVES[a].color, 0.8f), 0.0f, PT_RING);
                }
            }
        }
        if (k->t > 0.0f) {
            k->t -= DT;
            if (k->t <= 0.0f) end_effect(a);
        }
    }
    if (G.in.active_pressed) {
        /* Every item turned on and ready goes off at once, in table order (BOOM before NOVA:
         * it only blows up the shots already out). */
        int used = 0, on = 0;
        for (int a = ACT_NONE + 1; a < ACT_COUNT; a++) {
            ActiveSlot *k = &p->act[a];
            if (k->level == 0 || k->off) continue;
            on++;
            if (k->cd <= 0.0f && k->t <= 0.0f) { /* ready, and its last effect is over */
                use_active(a);
                used++;
            }
        }
        if (used) {
            G.active_uses++;
            upg_on_active(); /* NOVA TRIGGER: once per press */
        } else if (on == 0) {
            static float last = -10.0f;
            if (G.time - last > 1.0f || G.time < last) {
                last = G.time;
                sfx_play(SFX_DENIED);
                fx_popup(v2_add(p->pos, v2(0, -40)), col_hex(0xff5f7e, 1), owned ? "ACTIVE ITEMS TURNED OFF" : "NO ACTIVE ITEM");
            }
        }
    }
    /* TURRET: shoots the nearest enemy while it lasts. */
    if (active_running(ACT_TURRET)) {
        p->turret_cd -= DT;
        while (p->turret_cd <= 0.0f) {
            p->turret_cd += turret_rate(p->act[ACT_TURRET].level);
            Enemy *t = enemy_nearest(p->turret_pos, 1200.0f);
            if (!t) break;
            V2 dir = v2_norm(v2_sub(t->pos, p->turret_pos));
            PBullet *b = pbullet_spawn(v2_add(p->turret_pos, v2_scale(dir, 20.0f)), v2_scale(dir, 1500.0f), 4.5f, 8.0f,
                                       col_hex(ACTIVES[ACT_TURRET].color, 1));
            if (!b) break;
            b->life = 0.9f;
            b->knockback = 30.0f;
            pbullet_arm(b, false);
            sfx_play_ex(SFX_DRONE_SHOT, 0.6f, 0.8f, 0.0f);
        }
    }
    /* BLADE STORM: orbiting blades cut bullets and grind enemies. */
    if (active_running(ACT_BLADES)) {
        p->blade_a += DT * 7.0f;
        int n = blade_count(p->act[ACT_BLADES].level);
        float dps = 220.0f * p->dmg_mult * power(p->act[ACT_BLADES].level);
        for (int k = 0; k < n; k++) {
            V2 bp = blade_pos(k, n);
            for (int i = 0; i < G.neb; i++) {
                EBullet *b = &G.eb[i];
                float r = 20.0f + b->radius;
                if (b->life > 0.0f && v2_dist2(b->pos, bp) < r * r) {
                    b->life = -1.0f;
                    fx_particle(b->pos, v2(0, 0), 0.12f, 12.0f, col_hex(ACTIVES[ACT_BLADES].color, 0.8f), 0.0f, PT_GLOW);
                }
            }
            for (int i = 0; i < G.nen; i++) {
                Enemy *e = &G.en[i];
                float r = e->radius + 18.0f;
                if (e->hp > 0.0f && v2_dist2(e->pos, bp) < r * r) enemy_damage(e, dps * DT, v2(0, 0));
            }
        }
    }
    /* SHIELD: the bubble eats the bullets that touch it (marked dead, removed by their loop). */
    if (active_shield_on()) {
        for (int i = 0; i < G.neb; i++) {
            EBullet *b = &G.eb[i];
            float r = shield_radius(p->act[ACT_SHIELD].level) + b->radius;
            if (b->life > 0.0f && v2_dist2(b->pos, p->pos) < r * r) {
                b->life = -1.0f;
                fx_particle(b->pos, v2(0, 0), 0.15f, b->radius * 2.5f, col_hex(ACTIVES[ACT_SHIELD].color, 0.8f), 0.0f, PT_GLOW);
            }
        }
    }
}

/* ---- Drawing ------------------------------------------------------------------ */

static void draw_effect(int a) {
    const Player *p = &G.pl;
    const ActiveSlot *k = &p->act[a];
    SDL_FColor c = col_hex(ACTIVES[a].color, 1);
    /* Flickers during its last half second: about to end. */
    float fade = k->t < 0.5f && fmodf(G.time, 0.1f) < 0.05f ? 0.35f : 1.0f;
    draw_blend_add();
    switch (a) {
    case ACT_SHIELD: {
        float r = shield_radius(k->level);
        draw_glow(p->pos, r * 1.75f, col_a(c, 0.25f * fade));
        draw_ring(p->pos, r + 1.5f * sinf(G.time * 12.0f), 3.0f, col_a(c, 0.8f * fade));
        draw_ngon_outline(p->pos, r - 4.0f, 6, G.time * 1.5f, 1.5f, col_a(c, 0.4f * fade));
    } break;
    case ACT_STRENGTH:
        draw_glow(p->pos, 70.0f, col_a(c, (0.22f + 0.08f * sinf(G.time * 20.0f)) * fade));
        for (int i = 0; i < 4; i++) {
            V2 d = v2_angle(G.time * 4.0f + (float)i * PI_F * 0.5f);
            draw_circle(v2_add(p->pos, v2_scale(d, 30.0f)), 3.5f, col_a(c, 0.9f * fade));
        }
        break;
    case ACT_TURRET: {
        V2 t = p->turret_pos;
        Enemy *e = enemy_nearest(t, 1200.0f);
        float aim = e ? v2_heading(v2_sub(e->pos, t)) : G.time;
        draw_glow(t, 40.0f, col_a(c, 0.25f * fade));
        draw_blend_alpha();
        draw_ngon(t, 16.0f, 6, 0.0f, col_hex(0x1d3a12, 1));
        draw_ngon_outline(t, 16.0f, 6, 0.0f, 2.5f, col_a(c, fade));
        draw_line(t, v2_add(t, v2_scale(v2_angle(aim), 24.0f)), 6.0f, col_a(c, fade));
        draw_blend_add();
    } break;
    case ACT_BLADES: {
        int n = blade_count(k->level);
        for (int b = 0; b < n; b++) {
            V2 bp = blade_pos(b, n), d = v2_angle(p->blade_a + TAU_F * (float)b / (float)n + PI_F * 0.5f);
            draw_glow(bp, 26.0f, col_a(c, 0.3f * fade));
            draw_line(v2_sub(bp, v2_scale(d, 18.0f)), v2_add(bp, v2_scale(d, 18.0f)), 5.0f, col_a(COL_WHITE, 0.9f * fade));
        }
        draw_ring(p->pos, BLADE_ORBIT, 1.5f, col_a(c, 0.15f * fade));
    } break;
    case ACT_TIME_STOP:
        draw_ring(p->pos, 60.0f + 8.0f * sinf(G.time * 3.0f), 2.0f, col_a(c, 0.5f * fade));
        break;
    case ACT_CHAOS:
        for (int i = 0; i < 6; i++) {
            float a = G.time * 3.0f + (float)i * TAU_F / 6.0f;
            SDL_FColor rc = col_hsv(fmodf(G.time * 0.5f + (float)i / 6.0f, 1.0f), 0.7f, 1.0f, 0.9f * fade);
            draw_circle(v2_add(p->pos, v2_scale(v2_angle(a), 40.0f)), 3.5f, rc);
        }
        draw_glow(p->pos, 64.0f, col_a(c, 0.18f * fade));
        break;
    default: break;
    }
    draw_blend_alpha();
}

void actives_draw(void) {
    for (int a = ACT_NONE + 1; a < ACT_COUNT; a++)
        if (active_running(a)) draw_effect(a);
}

/* ACT_NONE draws a generic lightning bolt (door preview of an active room). */
void draw_active_icon(int a, V2 p, float s, float t) {
    SDL_FColor c = col_hex(a > ACT_NONE && a < ACT_COUNT ? ACTIVES[a].color : COLOR_ACTIVE, 1);
    switch (a) {
    case ACT_DASH:
        for (int k = 0; k < 2; k++) {
            float x = p.x - s * 0.55f + (float)k * s * 0.7f;
            draw_line(v2(x - s * 0.3f, p.y - s * 0.6f), v2(x + s * 0.3f, p.y), s * 0.25f, c);
            draw_line(v2(x + s * 0.3f, p.y), v2(x - s * 0.3f, p.y + s * 0.6f), s * 0.25f, c);
        }
        break;
    case ACT_SHIELD:
        draw_ngon_outline(p, s * 1.0f, 6, PI_F / 6.0f, s * 0.2f, c);
        draw_ngon(p, s * 0.55f, 6, PI_F / 6.0f, col_a(c, 0.5f + 0.3f * sinf(t * 4.0f)));
        break;
    case ACT_BOOM:
        for (int k = 0; k < 8; k++) {
            V2 d = v2_angle(t * 1.5f + (float)k * TAU_F / 8.0f);
            draw_line(v2_add(p, v2_scale(d, s * 0.45f)), v2_add(p, v2_scale(d, s * (k & 1 ? 0.8f : 1.1f))), s * 0.18f, c);
        }
        draw_circle(p, s * 0.42f, c);
        draw_circle(p, s * 0.2f, COL_WHITE);
        break;
    case ACT_STRENGTH:
        /* x4: four arrows */
        for (int k = 0; k < 4; k++) {
            float x = p.x + (k & 1 ? s * 0.45f : -s * 0.45f), y = p.y + (k & 2 ? s * 0.45f : -s * 0.45f);
            draw_tri(v2(x, y - s * 0.4f), v2(x + s * 0.32f, y + s * 0.3f), v2(x - s * 0.32f, y + s * 0.3f), c);
        }
        break;
    case ACT_CHAOS: {
        /* A die whose pips change colour. */
        draw_rect_outline(p.x - s * 0.85f, p.y - s * 0.85f, s * 1.7f, s * 1.7f, s * 0.18f, c);
        static const float px[5] = {-0.45f, 0.45f, 0.0f, -0.45f, 0.45f}, py[5] = {-0.45f, -0.45f, 0.0f, 0.45f, 0.45f};
        for (int k = 0; k < 5; k++)
            draw_circle(v2(p.x + px[k] * s, p.y + py[k] * s), s * 0.16f, col_hsv(fmodf(t * 0.6f + (float)k * 0.2f, 1.0f), 0.6f, 1.0f, 1.0f));
    } break;
    case ACT_REVERSAL: /* two arrows chasing each other */
        draw_arc(p, s * 0.8f, s * 0.2f, t * 2.0f, t * 2.0f + 2.4f, c);
        draw_arc(p, s * 0.8f, s * 0.2f, t * 2.0f + PI_F, t * 2.0f + PI_F + 2.4f, c);
        draw_circle(v2_add(p, v2_scale(v2_angle(t * 2.0f + 2.4f), s * 0.8f)), s * 0.25f, COL_WHITE);
        draw_circle(v2_add(p, v2_scale(v2_angle(t * 2.0f + PI_F + 2.4f), s * 0.8f)), s * 0.25f, COL_WHITE);
        break;
    case ACT_TURRET:
        draw_ngon(v2(p.x, p.y + s * 0.2f), s * 0.7f, 6, 0.0f, c);
        draw_line(v2(p.x, p.y + s * 0.2f), v2_add(v2(p.x, p.y + s * 0.2f), v2_scale(v2_angle(-PI_F * 0.5f + 0.5f * sinf(t * 2.0f)), s * 1.1f)), s * 0.3f, COL_WHITE);
        break;
    case ACT_BLINK:
        draw_ring(v2(p.x - s * 0.5f, p.y + s * 0.4f), s * 0.35f, s * 0.12f, col_a(c, 0.6f));
        draw_line(v2(p.x - s * 0.35f, p.y + s * 0.25f), v2(p.x + s * 0.35f, p.y - s * 0.35f), s * 0.15f, col_a(c, 0.6f));
        draw_circle(v2(p.x + s * 0.5f, p.y - s * 0.5f), s * 0.42f, c);
        break;
    case ACT_NOVA:
        for (int k = 0; k < 10; k++) {
            V2 d = v2_angle(t + (float)k * TAU_F / 10.0f);
            draw_circle(v2_add(p, v2_scale(d, s * 0.85f)), s * 0.16f, c);
        }
        draw_circle(p, s * 0.35f, COL_WHITE);
        break;
    case ACT_TIME_STOP: /* a clock with frozen hands */
        draw_ring(p, s * 0.9f, s * 0.16f, c);
        draw_line(p, v2(p.x, p.y - s * 0.6f), s * 0.16f, COL_WHITE);
        draw_line(p, v2(p.x + s * 0.45f, p.y), s * 0.16f, COL_WHITE);
        break;
    case ACT_BLADES:
        for (int k = 0; k < 3; k++) {
            V2 d = v2_angle(t * 4.0f + (float)k * TAU_F / 3.0f);
            V2 bp = v2_add(p, v2_scale(d, s * 0.6f));
            draw_line(v2_sub(bp, v2_scale(v2_perp(d), s * 0.4f)), v2_add(bp, v2_scale(v2_perp(d), s * 0.4f)), s * 0.2f, c);
        }
        draw_circle(p, s * 0.2f, COL_WHITE);
        break;
    default:
        draw_tri(v2(p.x + s * 0.25f, p.y - s * 1.0f), v2(p.x - s * 0.55f, p.y + s * 0.15f), v2(p.x + s * 0.05f, p.y + s * 0.1f), c);
        draw_tri(v2(p.x - s * 0.25f, p.y + s * 1.0f), v2(p.x + s * 0.55f, p.y - s * 0.15f), v2(p.x - s * 0.05f, p.y - s * 0.1f), c);
        break;
    }
}

void active_spawn_choices(V2 center) {
    int cand[ACT_COUNT], n = 0;
    for (int a = ACT_NONE + 1; a < ACT_COUNT; a++)
        if (G.pl.act[a].level < MAX_ACTIVE_LEVEL) cand[n++] = a; /* one owned = a level up */
    int k = n < REWARD_CHOICES ? n : REWARD_CHOICES;
    for (int i = 0; i < k; i++) {
        int j = irand(i, n - 1), t = cand[i];
        cand[i] = cand[j];
        cand[j] = t;
        V2 p = v2(center.x + ((float)i - (float)(k - 1) * 0.5f) * 420.0f, center.y - 20.0f);
        pickup_spawn_choice(PK_ACTIVE, cand[i], p);
        fx_particle(p, v2(0, 0), 0.5f, 160.0f, col_hex(ACTIVES[cand[i]].color, 1), 0.0f, PT_RING);
    }
}
