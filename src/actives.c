#include "audio.h"
#include "draw.h"
#include "game.h"

/*
 * Active items: a single slot, used with SPACE / SHIFT / right click, then it recharges.
 * The ship starts without one; active rooms (RW_ACTIVE) offer a choice of 3. Taking a
 * different one replaces the item held; taking the same one again levels it up (like
 * weapons): longer effects, bigger explosions, faster recharge. Every recharge time also
 * scales with the RECHARGE stat (Player.dash_cooldown, which is the dash's own recharge).
 */

const ActiveDef ACTIVES[ACT_COUNT] = {
    [ACT_NONE] = {"NONE", 0x8899aa, 0.0f},
    [ACT_DASH] = {"DASH", 0x7dfcff, DASH_BASE_CD},
    [ACT_SHIELD] = {"SHIELD", 0x9fe8ff, 10.0f},
    [ACT_BOOM] = {"BOOM", 0xff8a3d, 6.0f},
    [ACT_STRENGTH] = {"STRENGTH", 0xff4d4d, 18.0f},
    [ACT_CHAOS] = {"CHAOS", 0xd77dff, 40.0f},
};

/* ---- Numbers per level ------------------------------------------------------- */

static int lv(int level) { return (level < 1 ? 1 : level > MAX_ACTIVE_LEVEL ? MAX_ACTIVE_LEVEL : level) - 1; }

float active_duration(int a, int level) {
    switch (a) {
    case ACT_DASH: return 0.13f + 0.02f * (float)lv(level);
    case ACT_SHIELD: return 2.5f + 0.5f * (float)lv(level);
    case ACT_STRENGTH: return 4.0f + 1.0f * (float)lv(level);
    case ACT_CHAOS: return 8.0f + 1.5f * (float)lv(level);
    default: return 0.0f;
    }
}

int active_chaos_count(int level) { return 20 + 2 * lv(level); }
static float shield_radius(int level) { return 34.0f + 5.0f * (float)lv(level); }
static float boom_radius(int level) { return 70.0f * (1.0f + 0.15f * (float)lv(level)); }
static float boom_power(int level) { return 1.0f + 0.25f * (float)lv(level); } /* damage multiplier */

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
    default: b[0] = 0; break;
    }
    return b;
}

static const char *const OPTION_NAMES[ACT_COUNT] = {"none", "dash", "shield", "boom", "strength", "chaos"};

const char *active_option_name(int a) { return a >= 0 && a < ACT_COUNT ? OPTION_NAMES[a] : "?"; }

/* CHAOS lends upgrades whose whole effect lives in the hooks (nothing applied once by
 * upgrade_apply), so taking them back is exact. Economy / stat upgrades are excluded. */
static const int CHAOS_POOL[] = {
    UPG_FROST, UPG_VENOM, UPG_THERMITE, UPG_ARC, UPG_PAYLOAD, UPG_SHRAPNEL, UPG_CRIT, UPG_GUILLOTINE,
    UPG_BOOMERANG, UPG_PHASE, UPG_TAIL_GUN, UPG_FLANK, UPG_CAPACITOR, UPG_REACTIVE, UPG_ORB, UPG_TESLA,
    UPG_LEECH, UPG_ADRENALINE, UPG_BLOODLUST, UPG_RAM, UPG_DASH_NOVA, UPG_CHRONO, UPG_VOLATILE, UPG_MINES,
    UPG_WOUNDED_FURY, UPG_SCAR_TISSUE, UPG_LAST_STAND, UPG_BLOOD_SCENT, UPG_LOADED_DICE,
};

/* -12% per level, and the RECHARGE stat. */
float active_cooldown(int a, int level) {
    const Player *p = &G.pl;
    if (a <= ACT_NONE || a >= ACT_COUNT) return 0.0f;
    float base = a == ACT_DASH ? p->dash_cooldown : ACTIVES[a].cooldown * p->dash_cooldown / DASH_BASE_CD;
    return base * powf(0.88f, (float)lv(level));
}

bool active_strength_on(void) { return G.pl.active == ACT_STRENGTH && G.pl.act_t > 0.0f; }
bool active_shield_on(void) { return G.pl.active == ACT_SHIELD && G.pl.act_t > 0.0f; }

void active_end_chaos(void) {
    Player *p = &G.pl;
    for (int u = 0; u < UPG_COUNT; u++) p->chaos[u] = false;
}

static void end_effect(void) {
    Player *p = &G.pl;
    bool lent = false;
    for (int u = 0; u < UPG_COUNT; u++) lent |= p->chaos[u];
    if (lent) {
        active_end_chaos();
        fx_popup(v2_add(p->pos, v2(0, -40)), col_hex(ACTIVES[ACT_CHAOS].color, 1), "CHAOS FADES");
    }
    p->act_t = 0.0f;
}

void active_give(int a) {
    Player *p = &G.pl;
    char title[48], desc[96];
    if (a == p->active) {
        /* The same item again: a level up (a running effect keeps going). */
        if (p->act_level < MAX_ACTIVE_LEVEL) p->act_level++;
        SDL_snprintf(title, sizeof title, "%s - LEVEL %d", ACTIVES[a].name, p->act_level);
    } else {
        end_effect();
        p->active = a;
        p->act_level = 1;
        SDL_snprintf(title, sizeof title, "%s", ACTIVES[a].name);
    }
    p->act_cd = p->act_cd_max = 0.0f; /* ready at once */
    SDL_snprintf(desc, sizeof desc, "%s", active_desc(a, p->act_level));
    for (char *c = desc; *c; c++)
        if (*c == '|') *c = ' ';
    fx_banner(title, desc, col_hex(ACTIVES[a].color, 1), 2.6f);
}

/* ---- Effects ---------------------------------------------------------------- */

static void use_dash(void) {
    Player *p = &G.pl;
    const Input *in = &G.in;
    V2 mv = v2_norm(v2((in->right ? 1.0f : 0.0f) - (in->left ? 1.0f : 0.0f), (in->down ? 1.0f : 0.0f) - (in->up ? 1.0f : 0.0f)));
    p->dash_dir = (mv.x != 0.0f || mv.y != 0.0f) ? mv : v2_angle(p->aim_angle);
    p->dash_t = active_duration(ACT_DASH, p->act_level);
    sfx_play_at(SFX_DASH, p->pos);
    fx_burst(p->pos, 12, 100.0f, 350.0f, 0.25f, 2.5f, col_hex(CHARACTERS[G.character].color, 1));
}

/* Every shot in flight blows up where it is, and keeps going. */
static void use_boom(void) {
    Player *p = &G.pl;
    float radius = fminf(boom_radius(p->act_level) * sqrtf(p->bullet_size), 170.0f);
    float power = boom_power(p->act_level);
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
    if (n == 0 || p->weapon == WPN_SWORD) {
        float big = 240.0f * (1.0f + 0.1f * (float)lv(p->act_level));
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
    int want = active_chaos_count(p->act_level), give = n < want ? n : want;
    for (int i = 0; i < give; i++) { /* partial Fisher-Yates */
        int j = irand(i, n - 1), t = cand[i];
        cand[i] = cand[j];
        cand[j] = t;
        p->chaos[cand[i]] = true;
    }
    char sub[64];
    SDL_snprintf(sub, sizeof sub, "+%d random upgrades for %.1f s", give, (double)active_duration(ACT_CHAOS, p->act_level));
    fx_banner("CHAOS", sub, col_hex(ACTIVES[ACT_CHAOS].color, 1), 1.6f);
    fx_burst(p->pos, 60, 200.0f, 800.0f, 0.6f, 3.0f, col_hex(ACTIVES[ACT_CHAOS].color, 1));
}

static void use_active(void) {
    Player *p = &G.pl;
    int a = p->active;
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
    default: return;
    }
    p->act_t = a == ACT_DASH ? 0.0f : active_duration(a, p->act_level);
    p->act_cd = p->act_cd_max = active_cooldown(a, p->act_level);
    G.active_uses++;
    upg_on_active();
}

void actives_update(void) {
    Player *p = &G.pl;
    if (p->act_cd > 0.0f) {
        p->act_cd -= DT;
        if (p->act_cd <= 0.0f) {
            p->act_cd = 0.0f;
            if (p->active != ACT_DASH) { /* the dash recharges too often to announce it */
                sfx_play_ex(SFX_WEAPON_SWITCH, 0.7f, 1.6f, 0.0f);
                fx_particle(p->pos, v2(0, 0), 0.25f, 70.0f, col_hex(ACTIVES[p->active].color, 0.8f), 0.0f, PT_RING);
            }
        }
    }
    if (p->act_t > 0.0f) {
        p->act_t -= DT;
        if (p->act_t <= 0.0f) end_effect();
    }
    if (G.in.active_pressed) {
        if (p->active == ACT_NONE) {
            static float last = -10.0f;
            if (G.time - last > 1.0f || G.time < last) {
                last = G.time;
                sfx_play(SFX_DENIED);
                fx_popup(v2_add(p->pos, v2(0, -40)), col_hex(0xff5f7e, 1), "NO ACTIVE ITEM");
            }
        } else if (p->act_cd <= 0.0f) {
            use_active();
        }
    }
    /* SHIELD: the bubble eats the bullets that touch it (marked dead, removed by their loop). */
    if (active_shield_on()) {
        for (int i = 0; i < G.neb; i++) {
            EBullet *b = &G.eb[i];
            float r = shield_radius(p->act_level) + b->radius;
            if (b->life > 0.0f && v2_dist2(b->pos, p->pos) < r * r) {
                b->life = -1.0f;
                fx_particle(b->pos, v2(0, 0), 0.15f, b->radius * 2.5f, col_hex(ACTIVES[ACT_SHIELD].color, 0.8f), 0.0f, PT_GLOW);
            }
        }
    }
}

/* ---- Drawing ------------------------------------------------------------------ */

void actives_draw(void) {
    const Player *p = &G.pl;
    if (p->act_t <= 0.0f) return;
    SDL_FColor c = col_hex(ACTIVES[p->active].color, 1);
    /* Flickers during its last half second: about to end. */
    float fade = p->act_t < 0.5f && fmodf(G.time, 0.1f) < 0.05f ? 0.35f : 1.0f;
    draw_blend_add();
    switch (p->active) {
    case ACT_SHIELD: {
        float r = shield_radius(p->act_level);
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
    default:
        draw_tri(v2(p.x + s * 0.25f, p.y - s * 1.0f), v2(p.x - s * 0.55f, p.y + s * 0.15f), v2(p.x + s * 0.05f, p.y + s * 0.1f), c);
        draw_tri(v2(p.x - s * 0.25f, p.y + s * 1.0f), v2(p.x + s * 0.55f, p.y - s * 0.15f), v2(p.x - s * 0.05f, p.y - s * 0.1f), c);
        break;
    }
}

void active_spawn_choices(V2 center) {
    int cand[ACT_COUNT], n = 0;
    for (int a = ACT_NONE + 1; a < ACT_COUNT; a++)
        if (a != G.pl.active || G.pl.act_level < MAX_ACTIVE_LEVEL) cand[n++] = a; /* the held one = level up */
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
