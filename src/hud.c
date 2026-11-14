#include "draw.h"
#include "game.h"
#include "audio.h"

#define HUD_H (ARENA_Y0 - WALL_T)

static void resource(PickupType t, float x, float y, int value) {
    draw_pickup_icon(t, -1, v2(x, y), 11.0f, G.time);
    draw_text(x + 22.0f, y - 8.0f, 2.0f, COL_WHITE, "%d", value);
}

static void draw_weapon_slots(void) {
    const Player *p = &G.pl;
    const float w = 112.0f, gap = 8.0f;
    float x0 = SCREEN_W - 40.0f - (float)WPN_COUNT * (w + gap) + gap;
    for (int i = 0; i < WPN_COUNT; i++) {
        float x = x0 + (float)i * (w + gap), y = 14.0f;
        bool owned = p->wlevel[i] > 0, cur = p->weapon == i;
        SDL_FColor c = col_hex(WEAPONS[i].color, 1);
        draw_rect(x, y, w, 60.0f, cur ? col_a(col_mul(c, 0.25f), 1.0f) : col_hex(0x0b0e18, 1));
        draw_rect_outline(x, y, w, 60.0f, cur ? 3.0f : 1.5f, owned ? col_a(c, cur ? 1.0f : 0.45f) : col_hex(0x2a3040, 1));
        draw_text(x + 6.0f, y + 6.0f, 1.0f, col_a(COL_WHITE, 0.5f), "%d", i + 1);
        if (!owned) {
            draw_text_centered(x + w * 0.5f, y + 24.0f, 1.5f, col_hex(0x2a3040, 1), "---");
            continue;
        }
        draw_text_centered(x + w * 0.5f, y + 18.0f, 1.5f, cur ? COL_WHITE : col_a(c, 0.8f), "%s", WEAPONS[i].name);
        for (int l = 0; l < MAX_WEAPON_LEVEL; l++) {
            float px = x + w * 0.5f - (float)MAX_WEAPON_LEVEL * 9.0f + (float)l * 18.0f + 2.0f;
            draw_rect(px, y + 42.0f, 14.0f, 6.0f, l < p->wlevel[i] ? c : col_hex(0x2a3040, 1));
        }
    }
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

    if (r->type == ROOM_START) {
        draw_text_centered(SCREEN_W * 0.5f, 16.0f, 3.0f, COL_WHITE, "LAUNCH BAY");
    } else if (r->type == ROOM_STATS) {
        draw_text_centered(SCREEN_W * 0.5f, 16.0f, 3.0f, col_hex(COLOR_STATS, 1), "ROOM %d - STATS", r->depth);
    } else if (r->type == ROOM_SECRET) {
        draw_text_centered(SCREEN_W * 0.5f, 16.0f, 3.0f, col_hex(COLOR_SECRET, 1), "%s", r->secret_shop ? "SECRET SHOP" : "SECRET ROOM");
    } else if (r->type == ROOM_SHOP) {
        draw_text_centered(SCREEN_W * 0.5f, 16.0f, 3.0f, col_hex(COLOR_SHOP, 1), "ROOM %d - SHOP", r->depth);
    } else {
        draw_text_centered(SCREEN_W * 0.5f, 16.0f, 3.0f, COL_WHITE, "ROOM %d", r->depth);
    }
    if (!r->cleared) {
        draw_text_centered(SCREEN_W * 0.5f, 52.0f, 2.0f, col_hex(0xff5f7e, 1), "WAVE %d / %d", r->wave, r->nwaves);
    } else {
        bool offers = false, items = false;
        for (int i = 0; i < G.npk; i++) {
            offers |= G.pk[i].choice;
            items |= G.pk[i].price > 0;
        }
        float a = 0.55f + 0.45f * sinf(G.time * 5.0f);
        if (offers && r->type == ROOM_STATS) draw_text_centered(SCREEN_W * 0.5f, 52.0f, 2.0f, col_hex(COLOR_STATS, a), "TAKE ONE TRADE - OR SKIP IT");
        else if (offers) draw_text_centered(SCREEN_W * 0.5f, 52.0f, 2.0f, col_hex(0xb57cff, a), "PICK ONE - THE OTHERS VANISH");
        else if (r->type == ROOM_SECRET && !items) draw_text_centered(SCREEN_W * 0.5f, 52.0f, 2.0f, col_hex(COLOR_SECRET, a), "GRAB THE LOOT - EXIT AT THE BOTTOM");
        else if (items) draw_text_centered(SCREEN_W * 0.5f, 52.0f, 2.0f, col_hex(COLOR_SHOP, a), "SPEND COINS - OR SKIP IT");
        else draw_text_centered(SCREEN_W * 0.5f, 52.0f, 2.0f, col_hex(0x66f0ff, a), "CHOOSE A DOOR");
    }

    draw_weapon_slots();

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
    draw_text_centered(SCREEN_W * 0.5f, y + 30.0f, 2.0f, c, "MOVE SPEED x%.2f   DASH %.2fs   HITBOX %.1fpx   LUCK %+d",
                       (double)(p->speed / 540.0f), (double)p->dash_cooldown, (double)p->hit_radius, p->luck);
    draw_text_centered(SCREEN_W * 0.5f, y + 60.0f, 2.0f, c, "PROJECTILES +%d   PIERCE +%d   BOUNCE +%d   DRONES %d",
                       p->extra_proj, p->pierce, p->bounce, p->drones);
    /* Upgrades, wrapped over as many lines as needed. */
    char line[160];
    int len = 0, row = 0;
    for (int u = 0; u <= UPG_COUNT; u++) {
        char item[48] = "";
        if (u < UPG_COUNT && p->upg_count[u] == 1) SDL_snprintf(item, sizeof item, "%s", UPGRADES[u].name);
        else if (u < UPG_COUNT && p->upg_count[u] > 1) SDL_snprintf(item, sizeof item, "%s x%d", UPGRADES[u].name, p->upg_count[u]);
        if (u < UPG_COUNT && !item[0]) continue;
        if (len > 0 && (u == UPG_COUNT || len + 3 + (int)SDL_strlen(item) > 120)) {
            draw_text_centered(SCREEN_W * 0.5f, y + 100.0f + (float)row++ * 22.0f, 1.5f, col_hex(0xb57cff, 0.9f), "%s", line);
            len = 0;
        }
        if (u < UPG_COUNT) len += SDL_snprintf(line + len, sizeof line - (size_t)len, "%s%s", len ? "   " : "", item);
    }
}

void overlay_draw(void) {
    if (G.hurt_flash > 0.0f) draw_vignette(col_hex(0xff1030, 0.55f * G.hurt_flash));
    else if (G.state == GS_PLAYING && G.pl.hp <= 2)
        draw_vignette(col_hex(0xff1030, 0.12f + 0.08f * sinf(G.time * 6.0f)));

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
        if (G.banner_sub[0]) draw_text_centered(SCREEN_W * 0.5f, 310.0f, 2.5f, col_a(COL_WHITE, a * 0.85f), "%s", G.banner_sub);
    }

    if (G.transitioning) {
        float k = G.trans_t / TRANS_TIME;
        draw_rect(0, 0, SCREEN_W, SCREEN_H, rgba(0, 0, 0, clampf(1.0f - fabsf(k * 2.0f - 1.0f), 0.0f, 1.0f) * 1.2f));
    }

    char tbuf[16];
    switch (G.state) {
    case GS_TITLE: {
        dim(0.8f);
        float pulse = 0.5f + 0.5f * sinf(G.time * 3.0f);
        draw_text_centered(SCREEN_W * 0.5f, 250.0f, 10.0f, col_hex(0x66f0ff, 1), "NEON BREACH");
        draw_text_centered(SCREEN_W * 0.5f, 350.0f, 2.5f, col_hex(0xff4fd8, 1), "a roguelite bullet storm");
        const char *lines[] = {
            "WASD / ZQSD / ARROWS   move",
            "MOUSE                  aim & shoot (hold)",
            "SPACE / SHIFT / RMB    dash (invulnerable)",
            "E                      bomb - clears every bullet",
            "WHEEL / 1-5            switch weapon",
            "ESC  pause      M  mute      F11  fullscreen",
        };
        for (int i = 0; i < ARRAY_LEN(lines); i++)
            draw_text_centered(SCREEN_W * 0.5f, 480.0f + (float)i * 34.0f, 2.0f, col_a(COL_WHITE, 0.8f), "%s", lines[i]);
        draw_text_centered(SCREEN_W * 0.5f, 760.0f, 3.0f, col_a(COL_WHITE, 0.4f + 0.6f * pulse), "CLICK TO START");
    } break;
    case GS_PAUSED:
        dim(0.75f);
        draw_text_centered(SCREEN_W * 0.5f, 280.0f, 8.0f, COL_WHITE, "PAUSED");
        draw_stats(420.0f);
        draw_text_centered(SCREEN_W * 0.5f, 820.0f, 2.0f, col_a(COL_WHITE, 0.7f), "ESC resume    R restart    Q quit");
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
        }
    } break;
    case GS_DEBUG:
        debug_menu_draw();
        break;
    default:
        break;
    }

    draw_crosshair(G.in.mouse);
}
