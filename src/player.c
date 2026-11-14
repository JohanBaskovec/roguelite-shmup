#include "draw.h"
#include "game.h"
#include "audio.h"

#include <string.h>

#define SHIP_COLOR (CHARACTERS[G.character].color)

void player_init(void) {
    Player *p = &G.pl;
    memset(p, 0, sizeof *p);
    p->hp = p->max_hp = 6;
    p->speed = 540.0f;
    p->keys = 1;
    p->bombs = 2;
    p->weapon = WPN_BLASTER;
    p->wlevel[WPN_BLASTER] = 1;
    p->dmg_mult = p->rate_mult = p->shot_speed_mult = 1.0f;
    p->magnet = 140.0f;
    p->dash_cooldown = DASH_BASE_CD;
    p->active = ACT_NONE; /* active items are found in active rooms */
    p->range_mult = 1.0f;
    p->hit_radius = PLAYER_HIT_R;
    p->luck = 0;
    p->spread_mult = p->bullet_size = 1.0f;
    character_apply();
}

static void player_bomb(void) {
    Player *p = &G.pl;
    p->bombs--;
    sfx_play(SFX_BOMB);
    ebullets_clear(true);
    for (int i = 0; i < G.nen; i++) {
        Enemy *e = &G.en[i];
        float d = v2_dist(e->pos, p->pos);
        float dmg = (d < 650.0f ? 90.0f : 35.0f) * (1.0f + 0.6f * (float)p->upg_count[UPG_BOMB_BELT]);
        enemy_damage(e, dmg, v2_scale(v2_norm(v2_sub(e->pos, p->pos)), 700.0f));
    }
    p->invuln = fmaxf(p->invuln, 0.7f);
    G.bomb_flash = 1.0f;
    G.hitstop = 0.06f;
    room_bomb_at(p->pos);
    fx_shake(1.0f);
    fx_particle(p->pos, v2(0, 0), 0.6f, 1500.0f, col_hex(0xffffff, 0.9f), 0.0f, PT_RING);
    fx_particle(p->pos, v2(0, 0), 0.45f, 900.0f, col_hex(0xff8a3d, 1.0f), 0.0f, PT_RING);
    fx_burst(p->pos, 120, 300.0f, 1600.0f, 0.6f, 4.0f, col_hex(0xffc04d, 1));
}

void player_hurt(int dmg) {
    Player *p = &G.pl;
    if (G.state != GS_PLAYING || G.transitioning) return;
    if (p->invuln > 0.0f || p->dash_t > 0.0f || active_shield_on()) return;
    if (upg_block_hit()) return; /* EMERGENCY SHIELD */
    if (G.god) dmg = 0;
    /* Shields (temporary hearts) soak damage before the hull. */
    bool shielded = p->shield > 0;
    int soak = dmg < p->shield ? dmg : p->shield;
    p->shield -= soak;
    p->hp -= dmg - soak;
    p->invuln = 1.2f;
    G.hurt_flash = shielded ? 0.4f : 1.0f;
    sfx_play_ex(SFX_PLAYER_HURT, 1.0f, shielded ? 1.6f : 1.0f, 0.0f);
    upg_on_hurt();
    G.hitstop = 0.08f;
    fx_shake(0.55f);
    /* Mercy: wipe bullets right around the ship. Marked dead, removed by the bullet loop. */
    for (int i = 0; i < G.neb; i++) {
        EBullet *b = &G.eb[i];
        if (b->life > 0.0f && v2_dist2(b->pos, p->pos) < 170.0f * 170.0f) {
            b->life = -1.0f;
            fx_particle(b->pos, v2(0, 0), 0.25f, b->radius * 2.5f, b->col, 0.0f, PT_GLOW);
        }
    }
    fx_burst(p->pos, 26, 150.0f, 650.0f, 0.4f, 3.0f, col_hex(0xff3355, 1));
    fx_particle(p->pos, v2(0, 0), 0.3f, 160.0f, col_hex(0xff3355, 1), 0.0f, PT_RING);
    if (p->hp <= 0 && !upg_on_lethal_hit()) {
        p->hp = 0;
        game_over();
    }
}

void player_update(void) {
    Player *p = &G.pl;
    const Input *in = &G.in;

    V2 mv = v2((in->right ? 1.0f : 0.0f) - (in->left ? 1.0f : 0.0f), (in->down ? 1.0f : 0.0f) - (in->up ? 1.0f : 0.0f));
    mv = v2_norm(mv);

    p->invuln -= DT;

    V2 to_mouse = v2_sub(in->mouse, p->pos);
    if (v2_len2(to_mouse) > 4.0f) p->aim_angle = v2_heading(to_mouse);

    actives_update(); /* active item: dash, shield, boom, strength, chaos */

    if (p->dash_t > 0.0f) {
        p->dash_t -= DT;
        p->vel = v2_scale(p->dash_dir, p->speed * 3.4f);
        fx_particle(p->pos, v2(0, 0), 0.2f, 22.0f, col_hex(SHIP_COLOR, 0.45f), 0.0f, PT_GLOW);
    } else {
        p->vel = v2_lerp(p->vel, v2_scale(mv, p->speed * upg_move_speed_mult()), smooth_k(24.0f));
    }

    p->pos = v2_add(p->pos, v2_scale(p->vel, DT));
    V2 n;
    if (room_collide_circle(&p->pos, PLAYER_BODY_R, &n)) {
        float into = v2_dot(p->vel, n);
        if (into < 0.0f) p->vel = v2_sub(p->vel, v2_scale(n, into));
    }

    if (in->bomb_pressed) {
        if (p->bombs > 0) player_bomb();
        else {
            fx_popup(v2_add(p->pos, v2(0, -40)), col_hex(0xff5f7e, 1), "NO BOMBS");
            sfx_play(SFX_DENIED);
        }
    }

    int prev_weapon = p->weapon;
    if (in->weapon_select >= 0 && in->weapon_select < WPN_COUNT && p->wlevel[in->weapon_select] > 0)
        p->weapon = in->weapon_select;
    if (in->weapon_cycle != 0) {
        int dir = in->weapon_cycle > 0 ? 1 : -1;
        for (int k = 1; k < WPN_COUNT; k++) {
            int w = ((p->weapon + dir * k) % WPN_COUNT + WPN_COUNT) % WPN_COUNT;
            if (p->wlevel[w] > 0) {
                p->weapon = w;
                break;
            }
        }
    }

    if (p->weapon != prev_weapon) sfx_play(SFX_WEAPON_SWITCH);

    /* Engine exhaust, proportional to speed. */
    float spd = v2_len(p->vel);
    if (spd > 40.0f && (G.ticks & 1)) {
        V2 back = v2_scale(v2_norm(p->vel), -1.0f);
        V2 v = v2_add(v2_scale(back, frand(150.0f, 260.0f)), v2(frand(-40, 40), frand(-40, 40)));
        fx_particle(v2_add(p->pos, v2_scale(back, 12.0f)), v, 0.18f, 7.0f, col_hex(0xff9a3c, 0.8f), 3.0f, PT_GLOW);
    }
}

/* Engine flame behind the hull. */
static void draw_flame(V2 pos, V2 f, V2 s, float back, float k) {
    float flick = (10.0f + 8.0f * sinf(G.time * 70.0f)) * k;
    V2 rear = v2_sub(pos, v2_scale(f, back * k));
    draw_tri(v2_add(rear, v2_scale(s, 7 * k)), v2_sub(rear, v2_scale(s, 7 * k)), v2_sub(rear, v2_scale(f, flick)), col_hex(0xff9a3c, 0.9f));
}

void draw_ship(int character, V2 pos, float angle, float k, bool blink) {
    V2 f = v2_angle(angle), s = v2_perp(f);
    SDL_FColor c = col_hex(CHARACTERS[character].color, 1);
    draw_blend_add();
    draw_glow(pos, 48.0f * k, col_a(c, 0.18f));
    draw_flame(pos, f, s, character == CHAR_NORMAL ? 10.0f : 17.0f, k);
    draw_blend_alpha();
    if (blink) return;

    if (character == CHAR_SHOTGUNNER) {
        /* Round hull with a double barrel. */
        for (int b = -1; b <= 1; b += 2) {
            V2 base = v2_add(pos, v2_scale(s, 5.5f * k * (float)b));
            draw_line(base, v2_add(base, v2_scale(f, 27.0f * k)), 5.0f * k, col_hex(0x8a5a2a, 1));
            draw_line(v2_add(base, v2_scale(f, 22.0f * k)), v2_add(base, v2_scale(f, 27.0f * k)), 5.0f * k, c);
        }
        draw_circle(pos, 17.0f * k, col_hex(0x5a3410, 1));
        draw_ring(pos, 17.0f * k, 3.0f * k, c);
        draw_ring(pos, 10.0f * k, 1.5f * k, col_a(c, 0.5f));
        draw_circle(v2_add(pos, v2_scale(f, 5.0f * k)), 4.5f * k, col_hex(0xfff0d8, 1));
    } else if (character == CHAR_KNIGHT) {
        /* Square armoured hull with a visor slit. */
        float h = 15.0f * k;
        V2 a = v2_add(pos, v2_add(v2_scale(f, h), v2_scale(s, h))), b = v2_add(pos, v2_sub(v2_scale(f, h), v2_scale(s, h)));
        V2 cc = v2_sub(pos, v2_add(v2_scale(f, h), v2_scale(s, h))), d = v2_sub(pos, v2_sub(v2_scale(f, h), v2_scale(s, h)));
        draw_quad(a, b, cc, d, col_hex(0x2a3150, 1));
        draw_line(a, b, 3.0f * k, c);
        draw_line(b, cc, 3.0f * k, c);
        draw_line(cc, d, 3.0f * k, c);
        draw_line(d, a, 3.0f * k, c);
        V2 visor = v2_add(pos, v2_scale(f, 7.0f * k));
        draw_line(v2_add(visor, v2_scale(s, 9.0f * k)), v2_sub(visor, v2_scale(s, 9.0f * k)), 3.5f * k, col_hex(0xe8f0ff, 1));
        draw_line(v2_sub(pos, v2_scale(f, 2.0f * k)), v2_sub(pos, v2_scale(f, 11.0f * k)), 2.0f * k, col_a(c, 0.6f));
    } else {
        V2 nose = v2_add(pos, v2_scale(f, 24.0f * k));
        V2 lw = v2_add(v2_sub(pos, v2_scale(f, 14.0f * k)), v2_scale(s, 16.0f * k));
        V2 rw = v2_sub(v2_sub(pos, v2_scale(f, 14.0f * k)), v2_scale(s, 16.0f * k));
        V2 tail = v2_sub(pos, v2_scale(f, 5.0f * k));
        SDL_FColor fill = col_hex(0x0f4d5c, 1);
        draw_tri(nose, lw, tail, fill);
        draw_tri(nose, tail, rw, fill);
        draw_line(nose, lw, 3.0f * k, c);
        draw_line(lw, tail, 3.0f * k, c);
        draw_line(tail, rw, 3.0f * k, c);
        draw_line(rw, nose, 3.0f * k, c);
        draw_circle(v2_add(pos, v2_scale(f, 5.0f * k)), 4.5f * k, col_hex(0xd8fbff, 1));
    }
}

void player_draw(void) {
    const Player *p = &G.pl;
    bool blink = p->invuln > 0.0f && p->dash_t <= 0.0f && !active_shield_on() && fmodf(G.time, 0.12f) < 0.06f;
    draw_ship(G.character, p->pos, p->aim_angle, 1.0f, blink);

    /* The real hitbox, always visible: classic bullet-hell readability. */
    draw_circle(p->pos, p->hit_radius + 2.0f, col_hex(0xff3355, 1));
    draw_circle(p->pos, p->hit_radius, COL_WHITE);

    /* Active item recharging: an arc around the ship fills up. */
    if (p->active != ACT_NONE && p->act_cd > 0.0f && p->act_cd_max > 0.0f) {
        float k = 1.0f - p->act_cd / p->act_cd_max;
        SDL_FColor c = p->active == ACT_DASH ? COL_WHITE : col_hex(ACTIVES[p->active].color, 1);
        draw_arc(p->pos, 30.0f, 3.0f, -PI_F * 0.5f, -PI_F * 0.5f + TAU_F * k, col_a(c, 0.35f));
    }
}
