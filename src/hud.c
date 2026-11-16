#include "draw.h"
#include "game.h"
#include "audio.h"

#define HUD_H (ARENA_Y0 - WALL_T)

static void resource(PickupType t, float x, float y, int value) {
    draw_pickup_icon(t, -1, v2(x, y), 11.0f, G.time);
    draw_text(x + 22.0f, y - 8.0f, 2.0f, COL_WHITE, "%d", value);
}

/* Upgrades, top right: an icon per upgrade turned on, and the upgrades menu key. */
static void draw_upgrades_panel(void) {
    const Player *p = &G.pl;
    const float w = 600.0f, h = 60.0f, x = SCREEN_W - 40.0f - w, y = 14.0f;
    SDL_FColor c = col_hex(0xb57cff, 1);
    draw_rect(x, y, w, h, col_a(col_mul(c, 0.14f), 1.0f));
    draw_rect_outline(x, y, w, h, 2.0f, col_a(c, 0.6f));
    int list[UPG_COUNT];
    int found = upgrade_menu_list(list), off = 0, shown = 0, on = 0;
    for (int i = 0; i < found; i++) off += p->upg_off[list[i]];
    on = found - off;
    const float step = 26.0f, right = x + w - 150.0f;
    int fit = (int)((right - (x + 24.0f)) / step);
    for (int i = 0; i < found; i++) {
        if (p->upg_off[list[i]]) continue;
        if (shown == fit - 1 && on > fit) break; /* room left for "+N" */
        draw_pickup_icon(PK_UPGRADE, list[i], v2(x + 24.0f + step * (float)shown, y + h * 0.5f), 9.0f, G.time);
        shown++;
    }
    if (shown < on) draw_text(x + 14.0f + step * (float)shown, y + h * 0.5f - 6.0f, 1.5f, col_a(COL_WHITE, 0.7f), "+%d", on - shown);
    if (found == 0) draw_text(x + 14.0f, y + h * 0.5f - 6.0f, 1.5f, col_a(COL_WHITE, 0.35f), "NO UPGRADE YET");
    draw_text(x + w - 130.0f, y + 12.0f, 1.5f, col_hex(0xb57cff, 0.9f), "TAB  %d UPG", found);
    if (off) draw_text(x + w - 130.0f, y + 36.0f, 1.25f, col_hex(0xff5f7e, 0.9f), "%d TURNED OFF", off);
    else draw_text(x + w - 130.0f, y + 36.0f, 1.25f, col_a(COL_WHITE, 0.4f), "ON / OFF");
}

/* Active items, next to the resources: one cell per item owned (icon, level pips, recharge
 * bar, or the effect draining), lit when ready; all of them go off with SHIFT / right click. */
static void draw_active_slot(void) {
    const Player *p = &G.pl;
    const float x = 380.0f, y = 14.0f, h = 60.0f, cw = 32.0f;
    int list[ACT_COUNT];
    int n = actives_owned(list);
    if (n == 0) {
        const float w = 230.0f;
        draw_rect(x, y, w, h, col_hex(0x0b0e18, 1));
        draw_rect_outline(x, y, w, h, 1.5f, col_hex(0x2a3040, 1));
        draw_text_centered(x + w * 0.5f, y + 14.0f, 1.5f, col_hex(0x4a5470, 1), "NO ACTIVE ITEM");
        draw_text_centered(x + w * 0.5f, y + 36.0f, 1.25f, col_hex(0x4a5470, 1), "find one in an ACTIVE room");
        return;
    }
    const float w = 12.0f + cw * (float)n;
    draw_rect(x, y, w, h, col_hex(0x0b0e18, 1));
    draw_rect_outline(x, y, w, h, 1.5f, col_hex(0x2a3040, 1));
    for (int i = 0; i < n; i++) {
        int a = list[i];
        const ActiveSlot *k = &p->act[a];
        float cx = x + 6.0f + cw * (float)i;
        SDL_FColor c = col_hex(k->off ? 0x4a5470 : ACTIVES[a].color, 1);
        bool ready = !k->off && k->cd <= 0.0f;
        if (ready) draw_rect(cx + 1.0f, y + 4.0f, cw - 2.0f, h - 8.0f, col_a(col_mul(c, 0.2f), 1.0f));
        if (ready && k->t <= 0.0f)
            draw_rect_outline(cx + 1.0f, y + 4.0f, cw - 2.0f, h - 8.0f, 1.5f, col_a(c, 0.5f + 0.4f * sinf(G.time * 5.0f)));
        draw_active_icon(a, v2(cx + cw * 0.5f, y + 22.0f), 9.0f, G.time);
        if (k->off) {
            draw_rect(cx + 2.0f, y + 6.0f, cw - 4.0f, 32.0f, rgba(0.03f, 0.03f, 0.06f, 0.7f));
            draw_text_centered(cx + cw * 0.5f, y + 18.0f, 1.0f, col_hex(0xff5f7e, 1), "OFF");
        }
        for (int l = 0; l < k->level; l++) /* level pips */
            draw_rect(cx + 4.0f + (float)l * 5.0f, y + 39.0f, 3.0f, 4.0f, c);
        float bx = cx + 4.0f, bw = cw - 8.0f, by = y + 47.0f;
        draw_rect(bx, by, bw, 5.0f, col_hex(0x1a2035, 1));
        float dur = active_duration(a, k->level);
        if (k->t > 0.0f && dur > 0.0f) draw_rect(bx, by, bw * fminf(k->t / dur, 1.0f), 5.0f, COL_WHITE); /* effect draining */
        else draw_rect(bx, by, bw * (ready || k->cd_max <= 0.0f ? 1.0f : 1.0f - k->cd / k->cd_max), 5.0f, ready ? c : col_a(c, 0.5f));
    }
}

/* ---- Stats overlay (F2, top-left of the arena, semi-transparent) ---------- */

typedef struct {
    const char *label;
    float value, base;
    int better; /* +1: higher is better, -1: lower is better */
    const char *fmt;
} SideStat;

enum { SIDE_STATS = 13 };

/* Effective values: include the situational boosts (Bloodlust, Adrenaline). */
static int side_stats(SideStat *s) {
    const Player *p = &G.pl;
    int n = 0;
    s[n++] = (SideStat){"DAMAGE", p->dmg_mult * upg_volley_damage_mult(), 1.0f, 1, "x%.2f"};
    s[n++] = (SideStat){"FIRE RATE", p->rate_mult * upg_fire_rate_mult(), 1.0f, 1, "x%.2f"};
    s[n++] = (SideStat){"RANGE", p->range_mult, 1.0f, 1, "x%.2f"};
    s[n++] = (SideStat){"SHOT SPEED", p->shot_speed_mult * upg_shot_speed_mult(), 1.0f, 1, "x%.2f"};
    s[n++] = (SideStat){"MOVE SPEED", p->speed * upg_move_speed_mult() / 540.0f, 1.0f, 1, "x%.2f"};
    s[n++] = (SideStat){"RECHARGE", p->dash_cooldown / DASH_BASE_CD, 1.0f, -1, "x%.2f"};
    s[n++] = (SideStat){"HITBOX", p->hit_radius, PLAYER_HIT_R, -1, "%.1fpx"};
    s[n++] = (SideStat){"LUCK", (float)p->luck, 0.0f, 1, "%+.0f"};
    s[n++] = (SideStat){"SPREAD", p->spread_mult, 1.0f, -1, "x%.2f"};
    s[n++] = (SideStat){"PROJECTILES", (float)p->extra_proj, 0.0f, 1, "+%.0f"};
    s[n++] = (SideStat){"PIERCE", (float)p->pierce, 0.0f, 1, "+%.0f"};
    s[n++] = (SideStat){"BOUNCE", (float)p->bounce, 0.0f, 1, "+%.0f"};
    s[n++] = (SideStat){"DRONES", (float)p->drones, 0.0f, 1, "%.0f"};
    return n;
}

static void draw_stats_overlay(void) {
    static float last[SIDE_STATS], changed_at[SIDE_STATS];
    static bool seen;
    SideStat st[SIDE_STATS];
    int n = side_stats(st);

    /* Drawn over the play area: keep it light so bullets stay readable underneath. */
    const float x0 = ARENA_X0 + 10.0f, x1 = x0 + 200.0f, top = ARENA_Y0 + 10.0f, row = 28.0f;
    const float h = 30.0f * 2.0f + 14.0f + row * (float)n + 22.0f;
    draw_rect(x0, top, x1 - x0, h, rgba(0.02f, 0.03f, 0.06f, 0.45f));
    draw_rect_outline(x0, top, x1 - x0, h, 1.0f, col_hex(0x66f0ff, 0.18f));

    float y = top + 14.0f;
    for (int i = 0; i < n; i++) {
        if (i == 0 || i == 9) {
            if (i) y += 14.0f;
            draw_text(x0 + 12.0f, y, 1.5f, col_hex(0x66f0ff, 0.6f), "%s", i ? "SHOTS" : "STATS");
            draw_line(v2(x0 + 12.0f, y + 18.0f), v2(x1 - 12.0f, y + 18.0f), 1.0f, col_hex(0x66f0ff, 0.15f));
            y += 30.0f;
        }
        /* Briefly highlight a stat that just changed (pickup, trade, Bloodlust...). */
        if (!seen || fabsf(st[i].value - last[i]) > 1e-4f) {
            changed_at[i] = seen ? G.time : -10.0f;
            last[i] = st[i].value;
        }
        float age = G.time - changed_at[i];
        if (age >= 0.0f && age < 1.2f)
            draw_rect(x0 + 4.0f, y - 6.0f, x1 - x0 - 8.0f, row - 4.0f, col_hex(0x66f0ff, 0.18f * (1.0f - age / 1.2f)));

        float diff = (st[i].value - st[i].base) * (float)st[i].better;
        SDL_FColor c = diff > 1e-4f ? col_hex(0x7dff9a, 0.75f) : diff < -1e-4f ? col_hex(0xff5f7e, 0.75f) : col_a(COL_WHITE, 0.65f);
        char val[24];
        SDL_snprintf(val, sizeof val, st[i].fmt, (double)st[i].value);
        draw_text(x0 + 12.0f, y + 2.0f, 1.25f, col_a(COL_WHITE, 0.4f), "%s", st[i].label);
        draw_text(x1 - 12.0f - draw_text_width(val, 1.5f), y, 1.5f, c, "%s", val);
        y += row;
    }
    seen = true;
}

/* Boss health bar, top of the screen: phase marks at each threshold, a white trail for
 * recent damage, a golden frame while the boss is invulnerable. */
static void draw_boss_bar(void) {
    const Enemy *b = boss_alive();
    float k = 0.0f, intro = 1.0f;
    if (b) {
        k = clampf(b->hp / b->max_hp, 0.0f, 1.0f);
    } else {
        for (int i = 0; i < G.nsp; i++)
            if (G.sp[i].type == EN_BOSS) { intro = 1.0f - G.sp[i].t / G.sp[i].max_t; k = 1.0f; }
        if (k <= 0.0f) return; /* not there yet (or already dead) */
        k *= intro;
    }
    float trail = fmaxf(G.room.boss_trail, k);
    const float w = 600.0f, h = 16.0f, x = SCREEN_W * 0.5f - w * 0.5f, y = 54.0f; /* between the side panels */
    draw_rect(x - 3, y - 3, w + 6, h + 6, rgba(0, 0, 0, 0.75f));
    draw_rect(x, y, w * trail, h, col_a(COL_WHITE, 0.7f));
    SDL_FColor fill = b && b->phase >= BOSS_PHASES - 1 ? col_hex(0xff4040, 1) : col_hex(COLOR_BOSS, 1);
    draw_rect(x, y, w * k, h, fill);
    draw_rect(x, y, w * k, h * 0.35f, col_a(COL_WHITE, 0.25f));
    for (int p = 1; p < BOSS_PHASES; p++) {
        float mx = x + w * boss_phase_threshold(p);
        draw_rect(mx - 1.5f, y - 5, 3, h + 10, col_hex(0xffd23d, 1));
    }
    bool inv = b && b->inv_t > 0.0f;
    draw_rect_outline(x - 3, y - 3, w + 6, h + 6, 2, inv ? col_hex(0xffd23d, 0.6f + 0.4f * sinf(G.time * 30.0f)) : col_hex(COLOR_BOSS, 0.8f));
    draw_text_centered(SCREEN_W * 0.5f, y + h + 6.0f, 1.5f, col_hex(0xffd6dc, 0.9f), "THE OVERSEER%s", b && b->phase >= BOSS_PHASES - 1 ? " - ENRAGED" : "");
}

void hud_draw(void) {
    const Player *p = &G.pl;
    const Room *r = &G.room;

    draw_rect(0, 0, SCREEN_W, HUD_H, col_hex(0x05060c, 1));
    draw_line(v2(0, HUD_H), v2(SCREEN_W, HUD_H), 2.0f, col_hex(0x1a2035, 1));

    /* Hearts (2 hp each) */
    int containers = (p->max_hp + 1) / 2;
    for (int i = 0; i < containers; i++) {
        V2 c = v2(48.0f + (float)i * 36.0f, 30.0f);
        int here = p->hp - i * 2;
        draw_heart(c, 12.0f, col_hex(0x3a1420, 1), false);
        if (here >= 2) draw_heart(c, 12.0f, col_hex(0xff3355, 1), false);
        else if (here == 1) draw_heart(c, 12.0f, col_hex(0xff3355, 1), true);
    }
    /* Shields: temporary blue hearts after the containers. */
    for (int i = 0; i * 2 < p->shield; i++) {
        V2 c = v2(48.0f + (float)(containers + i) * 36.0f, 30.0f);
        draw_heart(c, 12.0f, col_hex(0x66d9ff, 1), p->shield - i * 2 == 1);
    }
    if (p->revives > 0)
        draw_text(48.0f + (float)(containers + (p->shield + 1) / 2) * 36.0f, 22.0f, 2.0f, col_hex(0xfff5c0, 1), "+%d", p->revives);

    resource(PK_COIN, 48.0f, 68.0f, p->coins);
    resource(PK_BOMB, 160.0f, 68.0f, p->bombs);
    resource(PK_KEY, 272.0f, 68.0f, p->keys);
    draw_active_slot();

    if (r->type == ROOM_START) {
        draw_text_centered(SCREEN_W * 0.5f, 16.0f, 3.0f, COL_WHITE, "LAUNCH BAY");
    } else if (r->type == ROOM_STATS) {
        draw_text_centered(SCREEN_W * 0.5f, 16.0f, 3.0f, col_hex(COLOR_STATS, 1), "ROOM %d - STATS", r->depth);
    } else if (r->type == ROOM_SECRET) {
        draw_text_centered(SCREEN_W * 0.5f, 16.0f, 3.0f, col_hex(COLOR_SECRET, 1), "%s", r->secret_shop ? "SECRET SHOP" : "SECRET ROOM");
    } else if (r->type == ROOM_WORKSHOP) {
        draw_text_centered(SCREEN_W * 0.5f, 16.0f, 3.0f, col_hex(COLOR_WORKSHOP, 1), "ROOM %d - WORKSHOP", r->depth);
    } else if (r->type == ROOM_SHOP) {
        draw_text_centered(SCREEN_W * 0.5f, 16.0f, 3.0f, col_hex(COLOR_SHOP, 1), "ROOM %d - SHOP", r->depth);
    } else if (r->type == ROOM_BOSS) {
        draw_text_centered(SCREEN_W * 0.5f, 16.0f, 3.0f, col_hex(COLOR_BOSS, 1), "ROOM %d - BOSS", r->depth);
    } else {
        draw_text_centered(SCREEN_W * 0.5f, 16.0f, 3.0f, COL_WHITE, "ROOM %d", r->depth);
    }
    if (!r->cleared && r->type == ROOM_BOSS) {
        draw_boss_bar();
    } else if (!r->cleared) {
        draw_text_centered(SCREEN_W * 0.5f, 52.0f, 2.0f, col_hex(0xff5f7e, 1), "WAVE %d / %d", r->wave, r->nwaves);
    } else {
        bool offers = false, items = false;
        for (int i = 0; i < G.npk; i++) {
            offers |= G.pk[i].choice;
            items |= G.pk[i].price > 0;
        }
        float a = 0.55f + 0.45f * sinf(G.time * 5.0f);
        if (r->type == ROOM_WORKSHOP && G.nmc > 0) draw_text_centered(SCREEN_W * 0.5f, 52.0f, 2.0f, col_hex(COLOR_WORKSHOP, a), "STEP ON A MACHINE TO USE IT");
        else if (offers && r->type == ROOM_STATS) draw_text_centered(SCREEN_W * 0.5f, 52.0f, 2.0f, col_hex(COLOR_STATS, a), "TAKE ONE TRADE - OR SKIP IT");
        else if (offers) draw_text_centered(SCREEN_W * 0.5f, 52.0f, 2.0f, col_hex(0xb57cff, a), "PICK ONE - THE OTHERS VANISH");
        else if (r->type == ROOM_SECRET && !items) draw_text_centered(SCREEN_W * 0.5f, 52.0f, 2.0f, col_hex(COLOR_SECRET, a), "GRAB THE LOOT - EXIT AT THE BOTTOM");
        else if (items) draw_text_centered(SCREEN_W * 0.5f, 52.0f, 2.0f, col_hex(COLOR_SHOP, a), "SPEND COINS - OR SKIP IT");
        else draw_text_centered(SCREEN_W * 0.5f, 52.0f, 2.0f, col_hex(0x66f0ff, a), "CHOOSE A DOOR");
    }

    draw_upgrades_panel();
    if (G.show_stats) draw_stats_overlay();

    if (audio_muted()) draw_text(SCREEN_W - 120.0f, SCREEN_H - 24.0f, 2.0f, col_a(COL_WHITE, 0.5f), "MUTED");
    if (G.show_debug)
        draw_text(10.0f, SCREEN_H - 20.0f, 1.5f, col_hex(0x9dff6a, 1), "FPS %3.0f  EB %d  PB %d  EN %d  PT %d",
                  (double)G.fps, G.neb, G.npb, G.nen, G.npt);
}

static void dim(float a) { draw_rect(0, 0, SCREEN_W, SCREEN_H, rgba(0.01f, 0.01f, 0.03f, a)); }

static void draw_vignette(SDL_FColor c) {
    SDL_FColor z = col_a(c, 0.0f);
    const float s = 220.0f, W = SCREEN_W, H = SCREEN_H;
    draw_quad_grad(v2(0, 0), v2(W, 0), v2(W, s), v2(0, s), c, c, z, z);
    draw_quad_grad(v2(0, H - s), v2(W, H - s), v2(W, H), v2(0, H), z, z, c, c);
    draw_quad_grad(v2(0, 0), v2(s, 0), v2(s, H), v2(0, H), c, z, z, c);
    draw_quad_grad(v2(W - s, 0), v2(W, 0), v2(W, H), v2(W - s, H), z, c, c, z);
}

static void draw_crosshair(V2 m) {
    SDL_FColor c = col_a(COL_WHITE, 0.9f);
    draw_ring(m, 13.0f, 2.0f, c);
    for (int i = 0; i < 4; i++) {
        V2 d = v2_angle((float)i * PI_F * 0.5f);
        draw_line(v2_add(m, v2_scale(d, 7.0f)), v2_add(m, v2_scale(d, 20.0f)), 2.0f, c);
    }
    draw_circle(m, 2.0f, col_hex(0xff3355, 1));
}

static void format_time(char *buf, size_t n, float t) {
    int s = (int)t;
    SDL_snprintf(buf, n, "%d:%02d", s / 60, s % 60);
}

static void draw_stats(float y) {
    const Player *p = &G.pl;
    SDL_FColor c = col_a(COL_WHITE, 0.75f);
    draw_text_centered(SCREEN_W * 0.5f, y, 2.0f, c, "DAMAGE x%.2f   FIRE RATE x%.2f   RANGE x%.2f   SHOT SPEED x%.2f",
                       (double)p->dmg_mult, (double)p->rate_mult, (double)p->range_mult, (double)p->shot_speed_mult);
    draw_text_centered(SCREEN_W * 0.5f, y + 30.0f, 2.0f, c, "MOVE SPEED x%.2f   RECHARGE x%.2f   HITBOX %.1fpx   LUCK %+d   SPREAD x%.2f",
                       (double)(p->speed / 540.0f), (double)(p->dash_cooldown / DASH_BASE_CD), (double)p->hit_radius, p->luck, (double)p->spread_mult);
    draw_text_centered(SCREEN_W * 0.5f, y + 60.0f, 2.0f, c, "PROJECTILES +%d   PIERCE +%d   BOUNCE +%d   DRONES %d",
                       p->extra_proj, p->pierce, p->bounce, p->drones);
    /* Upgrades, wrapped over as many lines as needed. */
    char line[160];
    int len = 0, row = 0;
    for (int u = 0; u <= UPG_COUNT; u++) {
        char item[48] = "";
        const char *off = u < UPG_COUNT && p->upg_off[u] ? " (OFF)" : "";
        if (u < UPG_COUNT && p->upg_count[u] == 1) SDL_snprintf(item, sizeof item, "%s%s", UPGRADES[u].name, off);
        else if (u < UPG_COUNT && p->upg_count[u] > 1) SDL_snprintf(item, sizeof item, "%s x%d%s", UPGRADES[u].name, p->upg_count[u], off);
        if (u < UPG_COUNT && !item[0]) continue;
        if (len > 0 && (u == UPG_COUNT || len + 3 + (int)SDL_strlen(item) > 120)) {
            draw_text_centered(SCREEN_W * 0.5f, y + 100.0f + (float)row++ * 22.0f, 1.5f, col_hex(0xb57cff, 0.9f), "%s", line);
            len = 0;
        }
        if (u < UPG_COUNT) len += SDL_snprintf(line + len, sizeof line - (size_t)len, "%s%s", len ? "   " : "", item);
    }
}

void overlay_draw(void) {
    if (G.hurt_flash > 0.0f && !demo_visible()) draw_vignette(col_hex(0xff1030, 0.55f * G.hurt_flash));

    if (G.bomb_flash > 0.0f) {
        draw_blend_add();
        draw_rect(0, 0, SCREEN_W, SCREEN_H, col_hex(0xffe0b0, 0.6f * G.bomb_flash));
        draw_blend_alpha();
    }

    if (G.banner_t > 0.0f && G.state == GS_PLAYING) {
        float age = G.banner_max - G.banner_t;
        float a = clampf(G.banner_t / 0.4f, 0.0f, 1.0f) * clampf(age / 0.08f, 0.0f, 1.0f);
        float scale = 5.0f + 1.5f * fmaxf(0.0f, 1.0f - age / 0.12f);
        draw_text_centered(SCREEN_W * 0.5f, 250.0f, scale, col_a(G.banner_col, a), "%s", G.banner);
        if (G.banner_sub[0]) { /* upgrades: "bonus  /  malus", shrunk to fit */
            float ss = draw_text_width(G.banner_sub, 2.5f) <= SCREEN_W - 120.0f ? 2.5f : 1.75f;
            draw_text_centered(SCREEN_W * 0.5f, 310.0f, ss, col_a(COL_WHITE, a * 0.85f), "%s", G.banner_sub);
        }
    }

    if (G.transitioning) {
        float k = G.trans_t / TRANS_TIME;
        draw_rect(0, 0, SCREEN_W, SCREEN_H, rgba(0, 0, 0, clampf(1.0f - fabsf(k * 2.0f - 1.0f), 0.0f, 1.0f) * 1.2f));
    }

    char tbuf[16];
    switch (G.state) {
    case GS_TITLE:
        title_draw();
        break;
    case GS_PAUSED:
        dim(0.75f);
        draw_text_centered(SCREEN_W * 0.5f, 280.0f, 8.0f, COL_WHITE, "PAUSED");
        draw_stats(420.0f);
        draw_text_centered(SCREEN_W * 0.5f, 820.0f, 2.0f, col_a(COL_WHITE, 0.7f), "ESC resume    TAB upgrades    R restart    C change ship    Q main menu");
        break;
    case GS_GAMEOVER: {
        dim(fminf(G.gameover_t, 0.75f));
        if (G.gameover_t < 0.3f) break;
        draw_text_centered(SCREEN_W * 0.5f, 260.0f, 8.0f, col_hex(0xff3355, 1), "SHIP DESTROYED");
        format_time(tbuf, sizeof tbuf, G.run_time);
        draw_text_centered(SCREEN_W * 0.5f, 380.0f, 3.0f, COL_WHITE, "ROOMS CLEARED %d    KILLS %d    TIME %s",
                           G.rooms_cleared, G.kills, tbuf);
        draw_stats(460.0f);
        if (G.gameover_t > 0.8f) {
            float pulse = 0.5f + 0.5f * sinf(G.time * 3.0f);
            draw_text_centered(SCREEN_W * 0.5f, 840.0f, 3.0f, col_a(COL_WHITE, 0.4f + 0.6f * pulse), "CLICK OR R TO RETRY");
            draw_text_centered(SCREEN_W * 0.5f, 895.0f, 2.0f, col_a(COL_WHITE, 0.6f), "C   change ship     Q   main menu (your unlocks are saved)");
        }
    } break;
    case GS_CHAR_SELECT:
        char_select_draw();
        break;
    case GS_DEBUG:
        debug_menu_draw();
        break;
    case GS_UPGRADES:
        upgrade_menu_draw();
        break;
    default:
        break;
    }

    draw_crosshair(G.in.mouse);
}
