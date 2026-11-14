#include "audio.h"
#include "draw.h"
#include "game.h"

#include <string.h>

/*
 * Workshop: like every room it starts full of enemies; once cleared it offers
 *  - the SUPPLY PRESS: half a heart of damage for coins, a bomb or a key (a few uses),
 *  - the AUGMENT pads: exclusive upgrades paid with 1, 2 or 3 heart containers
 *    (the more hearts, the stronger; take one, the others vanish - PK_AUGMENT pickups),
 *  - the SLOT MACHINE: bet GAMBLE_COST coins for a chance to win the item it shows.
 * A machine is used by stepping onto it; using it again needs a fresh step.
 */

#define PAD_SPACING 400.0f /* augment pads: a row through the middle of the room */
#define TRIGGER_R 62.0f
#define SPIN_TIME 0.7f

#define SUPPLY_COLOR 0xff3355
#define GAMBLE_COLOR 0xffd23f

float gamble_win_chance(void) { return clampf(0.3f + 0.03f * (float)G.pl.luck, 0.15f, 0.5f); }

static V2 centre(void) { return v2(ARENA_CX, ARENA_CY - 60.0f); }

/* Machines: a lower row, either side of the entry (room.c keeps obstacles off these spots). */
V2 workshop_machine_spot(int i) { return v2(ARENA_CX + (i == 0 ? -WORKSHOP_MACHINE_X : WORKSHOP_MACHINE_X), ARENA_CY + WORKSHOP_MACHINE_DY); }

/* The slot machine's prize: an upgrade not owned, a stat trade once every upgrade is owned,
 * 10 coins if even that is impossible. */
static void roll_prize(Machine *g) {
    int offer;
    g->prize_sub = upgrade_fresh();
    g->prize = PK_UPGRADE;
    if (g->prize_sub >= 0) return;
    g->prize = stats_roll_offers(&offer, 1) > 0 ? PK_STAT : PK_COIN;
    g->prize_sub = g->prize == PK_STAT ? offer : 10;
}

void workshop_refresh_prize(void) {
    for (int i = 0; i < G.nmc; i++) {
        Machine *m = &G.mc[i];
        if (m->type == MC_GAMBLE && !m->broken && m->prize == PK_UPGRADE && G.pl.upg_count[m->prize_sub] > 0)
            roll_prize(m);
    }
}

void workshop_spawn(void) {
    V2 c = centre();
    G.nmc = 0;
    Machine *s = &G.mc[G.nmc++];
    memset(s, 0, sizeof *s);
    s->type = MC_SUPPLY;
    s->pos = workshop_machine_spot(0);
    s->uses = irand(SUPPLY_USES_MIN, SUPPLY_USES_MAX);

    Machine *g = &G.mc[G.nmc++];
    memset(g, 0, sizeof *g);
    g->type = MC_GAMBLE;
    g->pos = workshop_machine_spot(1);
    roll_prize(g);

    /* One pad per tier the player can pay for (never the last heart container), centred. */
    int pads[3], npads = 0;
    for (int tier = 1; tier <= 3; tier++) {
        if (G.pl.max_hp - 2 * tier < 2) continue;
        int u = upgrade_roll_exclusive(tier);
        if (u >= 0) pads[npads++] = u;
    }
    for (int i = 0; i < npads; i++) {
        V2 p = v2(c.x + ((float)i - (float)(npads - 1) * 0.5f) * PAD_SPACING, c.y);
        pickup_spawn_choice(PK_AUGMENT, pads[i], p);
        fx_particle(p, v2(0, 0), 0.5f, 160.0f, col_hex(0xff3355, 1), 0.0f, PT_RING);
    }
    for (int i = 0; i < G.nmc; i++)
        fx_particle(G.mc[i].pos, v2(0, 0), 0.5f, 200.0f, col_hex(COLOR_WORKSHOP, 1), 0.0f, PT_RING);
}

static void deny(const Machine *m, const char *msg) {
    sfx_play_at(SFX_DENIED, m->pos);
    fx_popup(v2(m->pos.x, m->pos.y - 120.0f), col_hex(0xff4d6a, 1), "%s", msg);
}

static void use_supply(Machine *m) {
    Player *p = &G.pl;
    if (p->hp + p->shield <= 1) {
        deny(m, "TOO WEAK");
        return;
    }
    /* Half a heart of damage: shields (armor) absorb it first. */
    if (p->shield > 0) p->shield--;
    else p->hp--;
    G.hurt_flash = 0.5f;
    G.machine_uses++;
    sfx_play_ex(SFX_PLAYER_HURT, 0.8f, 1.3f, 0.0f);
    m->anim = 0.4f;
    V2 out = v2(m->pos.x, m->pos.y + 40.0f);
    float r = frand(0.0f, 1.0f);
    if (r < 0.45f) {
        int n = irand(5, 8);
        for (int i = 0; i < n; i++) pickup_spawn(PK_COIN, 1, out, v2_scale(v2_angle(frand(0.0f, TAU_F)), frand(100, 300)));
        fx_popup(v2(m->pos.x, m->pos.y - 120.0f), col_hex(0xffd23f, 1), "+%d COINS", n);
    } else if (r < 0.75f) {
        pickup_spawn(PK_BOMB, 0, out, v2(0, 200.0f));
        fx_popup(v2(m->pos.x, m->pos.y - 120.0f), col_hex(0xff8a3d, 1), "+1 BOMB");
    } else {
        pickup_spawn(PK_KEY, 0, out, v2(0, 200.0f));
        fx_popup(v2(m->pos.x, m->pos.y - 120.0f), col_hex(0xd9e3f0, 1), "+1 KEY");
    }
    if (--m->uses <= 0) {
        m->broken = true;
        fx_burst(m->pos, 30, 100.0f, 400.0f, 0.5f, 3.0f, col_hex(SUPPLY_COLOR, 1));
        sfx_play_at(SFX_EXPLODE_SMALL, m->pos);
    }
}

static void use_gamble(Machine *m) {
    if (G.pl.coins < GAMBLE_COST) {
        deny(m, "NEED 5 COINS");
        return;
    }
    G.pl.coins -= GAMBLE_COST;
    m->plays++;
    G.machine_uses++;
    m->spin = SPIN_TIME;
    sfx_play_ex(SFX_COIN, 1.0f, 0.7f, 0.0f);
    fx_popup(v2(m->pos.x, m->pos.y - 120.0f), col_hex(0xffd23f, 1), "-%d COINS", GAMBLE_COST);
}

static void resolve_gamble(Machine *m) {
    if (chance(gamble_win_chance())) {
        m->broken = true; /* one prize per machine */
        pickup_spawn(m->prize, m->prize_sub, v2(m->pos.x, m->pos.y + 150.0f), v2(0, 0));
        sfx_play_at(SFX_ITEM, m->pos);
        fx_burst(m->pos, 50, 150.0f, 600.0f, 0.6f, 3.0f, col_hex(GAMBLE_COLOR, 1));
        fx_popup(v2(m->pos.x, m->pos.y - 120.0f), col_hex(GAMBLE_COLOR, 1), "JACKPOT!");
        fx_shake(0.3f);
    } else {
        sfx_play_ex(SFX_DENIED, 0.6f, 0.8f, 0.0f);
        fx_popup(v2(m->pos.x, m->pos.y - 120.0f), col_a(COL_WHITE, 0.8f), "NO LUCK");
    }
}

void workshop_update(void) {
    for (int i = 0; i < G.nmc; i++) {
        Machine *m = &G.mc[i];
        m->anim -= DT;
        if (m->spin > 0.0f) {
            m->spin -= DT;
            if (m->spin <= 0.0f) resolve_gamble(m);
        }
        bool in = v2_dist2(G.pl.pos, m->pos) < TRIGGER_R * TRIGGER_R;
        bool fresh = in && !m->inside;
        m->inside = in;
        if (!fresh || m->spin > 0.0f) continue;
        if (m->broken) {
            deny(m, m->type == MC_SUPPLY ? "BROKEN" : "PRIZE WON");
            continue;
        }
        if (m->type == MC_SUPPLY) use_supply(m);
        else use_gamble(m);
    }
}

/* ---- Drawing --------------------------------------------------------------- */

static void draw_reel(V2 p, float s, int symbol, SDL_FColor c) {
    draw_rect(p.x - s, p.y - s, s * 2.0f, s * 2.0f, col_hex(0x0a0c14, 1));
    switch (symbol % 4) {
    case 0: draw_pickup_icon(PK_COIN, 1, p, s * 0.6f, 0.0f); break;
    case 1: draw_heart(p, s * 0.5f, col_hex(0xff3355, 1), false); break;
    case 2: draw_ngon(p, s * 0.65f, 5, 0.0f, c); break;
    default: draw_pickup_icon(PK_KEY, 0, v2(p.x - s * 0.2f, p.y), s * 0.55f, 0.0f); break;
    }
}

void workshop_draw(void) {
    for (int i = 0; i < G.nmc; i++) {
        const Machine *m = &G.mc[i];
        bool supply = m->type == MC_SUPPLY;
        SDL_FColor c = col_hex(supply ? SUPPLY_COLOR : GAMBLE_COLOR, m->broken && supply ? 0.35f : 1.0f);
        float bump = m->anim > 0.0f ? sinf(m->anim * 40.0f) * 3.0f : 0.0f;
        V2 p = v2(m->pos.x + bump, m->pos.y);

        draw_blend_add();
        draw_glow(p, 110.0f, col_a(c, m->broken ? 0.08f : 0.2f));
        draw_blend_alpha();
        /* Cabinet. */
        draw_rect(p.x - 60.0f, p.y - 70.0f, 120.0f, 140.0f, col_hex(0x10131f, 1));
        draw_rect_outline(p.x - 60.0f, p.y - 70.0f, 120.0f, 140.0f, 3.0f, c);
        draw_rect(p.x - 66.0f, p.y + 58.0f, 132.0f, 14.0f, col_a(c, 0.6f));
        draw_ring(m->pos, TRIGGER_R, 1.5f, col_a(c, m->inside ? 0.5f : 0.15f));

        if (supply) {
            /* Heart in, loot out. */
            draw_heart(v2(p.x, p.y - 30.0f), 13.0f, col_a(c, 1.0f), false);
            draw_tri(v2(p.x - 10.0f, p.y + 2.0f), v2(p.x + 10.0f, p.y + 2.0f), v2(p.x, p.y + 16.0f), col_a(COL_WHITE, 0.7f));
            draw_pickup_icon(PK_COIN, 1, v2(p.x - 26.0f, p.y + 34.0f), 8.0f, G.time);
            draw_pickup_icon(PK_BOMB, 0, v2(p.x, p.y + 34.0f), 8.0f, G.time);
            draw_pickup_icon(PK_KEY, 0, v2(p.x + 22.0f, p.y + 34.0f), 8.0f, G.time);
        } else {
            /* Three reels; the prize floats above the cabinet. */
            for (int k = 0; k < 3; k++) {
                int sym = m->spin > 0.0f ? (int)(G.time * 20.0f) + k * 3 : (m->broken ? 0 : k);
                draw_reel(v2(p.x - 36.0f + 36.0f * (float)k, p.y - 26.0f), 15.0f, sym, c);
            }
            draw_line(v2(p.x + 64.0f, p.y - 40.0f), v2(p.x + 78.0f, p.y - 64.0f), 4.0f, c);
            draw_circle(v2(p.x + 78.0f, p.y - 64.0f), 6.0f, col_hex(0xff3355, 1));
            draw_text_centered(p.x, p.y + 14.0f, 1.5f, c, "%d COINS", GAMBLE_COST);
            if (!m->broken)
                draw_pickup_icon(m->prize, m->prize_sub, v2(p.x, p.y - 112.0f + sinf(G.time * 3.0f) * 4.0f), 14.0f, G.time);
        }

        /* Labels: what it does, exactly. */
        float ty = p.y + 86.0f;
        draw_text_centered(p.x, p.y - 150.0f - (supply ? 0.0f : 20.0f), 2.0f, c, "%s", supply ? "SUPPLY PRESS" : "SLOT MACHINE");
        if (supply) {
            draw_text_centered(p.x, ty, 1.25f, col_a(COL_WHITE, 0.75f), "1/2 HEART OF DAMAGE");
            draw_text_centered(p.x, ty + 16.0f, 1.25f, col_a(COL_WHITE, 0.75f), "-> COINS, A BOMB OR A KEY");
            if (m->broken) draw_text_centered(p.x, ty + 38.0f, 1.5f, col_hex(0xff4d6a, 1), "BROKEN");
            else draw_text_centered(p.x, ty + 38.0f, 1.5f, col_a(c, 0.9f), "%d USE%s LEFT", m->uses, m->uses > 1 ? "S" : "");
        } else if (m->broken) {
            draw_text_centered(p.x, ty, 2.0f, c, "PRIZE WON");
        } else {
            if (m->prize == PK_UPGRADE) {
                draw_text_centered(p.x, ty - 4.0f, 1.75f, col_hex(UPGRADES[m->prize_sub].color, 1), "WIN: %s", UPGRADES[m->prize_sub].name);
                draw_desc(p.x, ty + 18.0f, 1.25f, col_a(COL_WHITE, 0.75f), upgrade_desc(m->prize_sub));
            } else if (m->prize == PK_STAT) {
                draw_text_centered(p.x, ty - 4.0f, 1.75f, col_hex(COLOR_STATS, 1), "WIN: A STAT TRADE");
                draw_text_centered(p.x, ty + 18.0f, 1.25f, col_hex(0x5dff8a, 1), "%s", STATS[stat_offer_up(m->prize_sub)].up_text);
                draw_text_centered(p.x, ty + 32.0f, 1.25f, col_hex(0xff4d6a, 1), "%s", STATS[stat_offer_down(m->prize_sub)].down_text);
            } else {
                draw_text_centered(p.x, ty - 4.0f, 1.75f, col_hex(0xffd23f, 1), "WIN: %d COINS", m->prize_sub);
            }
            draw_text_centered(p.x, ty + 52.0f, 1.5f, c, "%d COINS - %d%% TO WIN", GAMBLE_COST, (int)roundf(gamble_win_chance() * 100.0f));
        }
    }
}
