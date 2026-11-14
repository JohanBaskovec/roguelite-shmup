#include "draw.h"
#include "game.h"
#include "audio.h"

/*
 * Playable characters and the character select screen shown before a run.
 * A character only changes the starting state (stats, hearts, armor, weapon mod)
 * and the ship's look; everything else is shared.
 */

const CharDef CHARACTERS[CHAR_COUNT] = {
    [CHAR_NORMAL] = {
        .name = "NORMAL", .desc = {"Balanced fighter", "Rapid, precise shots", "No weaknesses, no tricks"},
        .color = 0x66f0ff, .hearts = 3, .armor = 0,
        .dmg_mult = 1.0f, .speed_mult = 1.0f, .hit_radius = PLAYER_HIT_R, .start_upgrade = -1,
    },
    [CHAR_SHOTGUNNER] = {
        .name = "SHOTGUNNER", .desc = {"Round hull: bigger hitbox", "Every shot hits twice as hard", "Starts with the Scatter"},
        .color = 0xffa94d, .hearts = 3, .armor = 0,
        .dmg_mult = 2.0f, .speed_mult = 1.0f, .hit_radius = 7.0f, .start_upgrade = UPG_W_SCATTER,
    },
    [CHAR_KNIGHT] = {
        .name = "KNIGHT", .desc = {"Slow square hull + 3 armor", "Sword: slashes in an arc", "and cuts enemy bullets"},
        .color = 0xc9d4ff, .hearts = 3, .armor = 3,
        .dmg_mult = 1.0f, .speed_mult = 0.8f, .hit_radius = PLAYER_HIT_R, .start_upgrade = UPG_W_SWORD,
    },
};

static const char *const OPTION_NAMES[CHAR_COUNT] = {"normal", "shotgunner", "knight"};

const char *character_option_name(int c) { return c >= 0 && c < CHAR_COUNT ? OPTION_NAMES[c] : "?"; }

void character_apply(void) {
    Player *p = &G.pl;
    const CharDef *c = &CHARACTERS[G.character];
    p->base.max_hp = c->hearts * 2;
    p->shield = c->armor * 2;
    p->base.dmg_mult = c->dmg_mult;
    p->base.speed = 540.0f * c->speed_mult;
    p->base.hit_radius = c->hit_radius;
    player_recompute();
    if (c->start_upgrade >= 0) upgrade_grant(c->start_upgrade);
    p->hp = p->max_hp;
}

/* ---- Character select screen --------------------------------------------- */

#define CARD_W 440.0f
#define CARD_H 600.0f
#define CARD_GAP 50.0f
#define CARD_Y 230.0f

static float card_x(int i) {
    float total = CARD_W * (float)CHAR_COUNT + CARD_GAP * (float)(CHAR_COUNT - 1);
    return (SCREEN_W - total) * 0.5f + (float)i * (CARD_W + CARD_GAP);
}

static int card_at(V2 m) {
    for (int i = 0; i < CHAR_COUNT; i++)
        if (m.x >= card_x(i) && m.x <= card_x(i) + CARD_W && m.y >= CARD_Y && m.y <= CARD_Y + CARD_H) return i;
    return -1;
}

void char_select_open(void) {
    G.state = GS_CHAR_SELECT;
    G.char_hover = G.character;
    G.sel_mouse = G.in.mouse;
    G.sel_left = G.in.left; /* a key already held does not count as a press */
    G.sel_right = G.in.right;
    sfx_play(SFX_WEAPON_SWITCH);
}

void char_select_update(void) {
    const Input *in = &G.in;
    int prev = G.char_hover;
    if (in->left && !G.sel_left) G.char_hover = (G.char_hover + CHAR_COUNT - 1) % CHAR_COUNT;
    if (in->right && !G.sel_right) G.char_hover = (G.char_hover + 1) % CHAR_COUNT;
    G.sel_left = in->left;
    G.sel_right = in->right;
    if (in->number >= 0 && in->number < CHAR_COUNT) G.char_hover = in->number;
    /* The mouse only takes over when it actually moves (the bot never moves it here). */
    if (v2_dist2(in->mouse, G.sel_mouse) > 1.0f) {
        int c = card_at(in->mouse);
        if (c >= 0) G.char_hover = c;
        G.sel_mouse = in->mouse;
    }
    if (G.char_hover != prev) sfx_play(SFX_WEAPON_SWITCH);

    if (in->pause_pressed) {
        G.state = GS_TITLE;
        sfx_play(SFX_PAUSE);
    } else if (in->confirm_pressed) {
        G.character = G.char_hover;
        game_new_run();
    }
}

static void stat_line(float x, float w, float y, const char *label, const char *value, int better) {
    SDL_FColor c = better > 0 ? col_hex(0x7dff9a, 1) : better < 0 ? col_hex(0xff5f7e, 1) : col_a(COL_WHITE, 0.85f);
    draw_text(x, y, 2.0f, col_a(COL_WHITE, 0.5f), "%s", label);
    draw_text(x + w - draw_text_width(value, 2.0f), y, 2.0f, c, "%s", value);
}

static int cmp(float v, float base, int better) { return v > base + 1e-4f ? better : v < base - 1e-4f ? -better : 0; }

void char_select_draw(void) {
    draw_rect(0, 0, SCREEN_W, SCREEN_H, rgba(0.01f, 0.01f, 0.03f, 0.94f));
    draw_text_centered(SCREEN_W * 0.5f, 110.0f, 5.0f, COL_WHITE, "CHOOSE YOUR SHIP");

    for (int i = 0; i < CHAR_COUNT; i++) {
        const CharDef *c = &CHARACTERS[i];
        bool sel = i == G.char_hover;
        float x = card_x(i), y = CARD_Y, cx = x + CARD_W * 0.5f;
        SDL_FColor col = col_hex(c->color, 1);
        float pulse = 0.5f + 0.5f * sinf(G.time * 4.0f);

        draw_rect(x, y, CARD_W, CARD_H, sel ? col_a(col_mul(col, 0.12f), 1.0f) : col_hex(0x080a12, 1));
        draw_rect_outline(x, y, CARD_W, CARD_H, sel ? 3.0f : 1.5f, col_a(col, sel ? 0.7f + 0.3f * pulse : 0.3f));
        draw_text_centered(cx, y + 28.0f, 4.0f, sel ? col : col_a(col, 0.6f), "%s", c->name);

        /* The ship, slowly looking around. */
        V2 ship = v2(cx, y + 190.0f);
        draw_blend_add();
        draw_glow(ship, 120.0f, col_a(col, sel ? 0.16f : 0.06f));
        draw_blend_alpha();
        draw_ship(i, ship, -PI_F * 0.5f + 0.35f * sinf(G.time * 1.3f + (float)i), 2.6f, false);

        /* Hearts and armor. */
        float hy = y + 300.0f, total = (float)(c->hearts + c->armor);
        for (int h = 0; h < c->hearts + c->armor; h++) {
            V2 hp = v2(cx + ((float)h - (total - 1.0f) * 0.5f) * 40.0f, hy);
            draw_heart(hp, 14.0f, h < c->hearts ? col_hex(0xff3355, 1) : col_hex(0x66d9ff, 1), false);
        }
        if (c->armor > 0) draw_text_centered(cx, hy + 22.0f, 1.5f, col_hex(0x66d9ff, 0.8f), "+%d ARMOR (TEMPORARY HEARTS)", c->armor);

        char buf[32];
        float sx = x + 40.0f, sw = CARD_W - 80.0f, sy = y + 360.0f;
        const CharDef *base = &CHARACTERS[CHAR_NORMAL];
        SDL_snprintf(buf, sizeof buf, "x%.2f", (double)c->dmg_mult);
        stat_line(sx, sw, sy, "DAMAGE", buf, cmp(c->dmg_mult, base->dmg_mult, 1));
        SDL_snprintf(buf, sizeof buf, "x%.2f", (double)c->speed_mult);
        stat_line(sx, sw, sy + 32.0f, "SPEED", buf, cmp(c->speed_mult, base->speed_mult, 1));
        SDL_snprintf(buf, sizeof buf, "%.0fpx", (double)c->hit_radius);
        stat_line(sx, sw, sy + 64.0f, "HITBOX", buf, cmp(c->hit_radius, base->hit_radius, -1));
        stat_line(sx, sw, sy + 96.0f, "STARTS WITH", c->start_upgrade >= 0 ? UPGRADES[c->start_upgrade].name : "-", 0);

        for (int l = 0; l < 3; l++)
            if (c->desc[l][0])
                draw_text_centered(cx, y + 500.0f + (float)l * 26.0f, 1.5f, col_a(COL_WHITE, sel ? 0.8f : 0.45f), "%s", c->desc[l]);
        draw_text(x + 12.0f, y + 10.0f, 1.5f, col_a(COL_WHITE, 0.35f), "%d", i + 1);
    }
    float pulse = 0.5f + 0.5f * sinf(G.time * 3.0f);
    draw_text_centered(SCREEN_W * 0.5f, 880.0f, 3.0f, col_a(COL_WHITE, 0.4f + 0.6f * pulse), "CLICK OR ENTER TO LAUNCH");
    draw_text_centered(SCREEN_W * 0.5f, 935.0f, 2.0f, col_a(COL_WHITE, 0.55f), "A / D  or  1-3  or  MOUSE   choose        ESC   back");
}
