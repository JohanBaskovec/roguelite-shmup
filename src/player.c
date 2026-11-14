#include "draw.h"
#include "game.h"

#include <string.h>

#define SHIP_COLOR 0x66f0ff

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
    p->dash_cooldown = 0.6f;
}

static void player_bomb(void) {
    Player *p = &G.pl;
    p->bombs--;
    ebullets_clear(true);
    float scale = enemy_hp_scale();
    for (int i = 0; i < G.nen; i++) {
        Enemy *e = &G.en[i];
        float d = v2_dist(e->pos, p->pos);
        float dmg = (d < 650.0f ? 90.0f : 35.0f) * scale;
        enemy_damage(e, dmg, v2_scale(v2_norm(v2_sub(e->pos, p->pos)), 700.0f));
    }
    p->invuln = fmaxf(p->invuln, 0.7f);
    G.bomb_flash = 1.0f;
    G.hitstop = 0.06f;
    fx_shake(1.0f);
    fx_particle(p->pos, v2(0, 0), 0.6f, 1500.0f, col_hex(0xffffff, 0.9f), 0.0f, PT_RING);
    fx_particle(p->pos, v2(0, 0), 0.45f, 900.0f, col_hex(0xff8a3d, 1.0f), 0.0f, PT_RING);
    fx_burst(p->pos, 120, 300.0f, 1600.0f, 0.6f, 4.0f, col_hex(0xffc04d, 1));
}

void player_hurt(int dmg) {
    Player *p = &G.pl;
    if (G.state != GS_PLAYING || G.transitioning) return;
    if (p->invuln > 0.0f || p->dash_t > 0.0f) return;
    if (G.god) dmg = 0;
    p->hp -= dmg;
    p->invuln = 1.2f;
    G.hurt_flash = 1.0f;
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
    if (p->hp <= 0) {
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
    p->dash_cd -= DT;

    V2 to_mouse = v2_sub(in->mouse, p->pos);
    if (v2_len2(to_mouse) > 4.0f) p->aim_angle = v2_heading(to_mouse);

    if (in->dash_pressed && p->dash_cd <= 0.0f) {
        p->dash_dir = (mv.x != 0.0f || mv.y != 0.0f) ? mv : v2_angle(p->aim_angle);
        p->dash_t = 0.13f;
        p->dash_cd = p->dash_cooldown;
        fx_burst(p->pos, 12, 100.0f, 350.0f, 0.25f, 2.5f, col_hex(SHIP_COLOR, 1));
    }

    if (p->dash_t > 0.0f) {
        p->dash_t -= DT;
        p->vel = v2_scale(p->dash_dir, p->speed * 3.4f);
        fx_particle(p->pos, v2(0, 0), 0.2f, 22.0f, col_hex(SHIP_COLOR, 0.45f), 0.0f, PT_GLOW);
    } else {
        p->vel = v2_lerp(p->vel, v2_scale(mv, p->speed), smooth_k(24.0f));
    }

    p->pos = v2_add(p->pos, v2_scale(p->vel, DT));
    V2 n;
    if (room_collide_circle(&p->pos, PLAYER_BODY_R, &n)) {
        float into = v2_dot(p->vel, n);
        if (into < 0.0f) p->vel = v2_sub(p->vel, v2_scale(n, into));
    }

    if (in->bomb_pressed) {
        if (p->bombs > 0) player_bomb();
        else fx_popup(v2_add(p->pos, v2(0, -40)), col_hex(0xff5f7e, 1), "NO BOMBS");
    }

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

    /* Engine exhaust, proportional to speed. */
    float spd = v2_len(p->vel);
    if (spd > 40.0f && (G.ticks & 1)) {
        V2 back = v2_scale(v2_norm(p->vel), -1.0f);
        V2 v = v2_add(v2_scale(back, frand(150.0f, 260.0f)), v2(frand(-40, 40), frand(-40, 40)));
        fx_particle(v2_add(p->pos, v2_scale(back, 12.0f)), v, 0.18f, 7.0f, col_hex(0xff9a3c, 0.8f), 3.0f, PT_GLOW);
    }
}

void player_draw(void) {
    const Player *p = &G.pl;
    bool blink = p->invuln > 0.0f && p->dash_t <= 0.0f && fmodf(G.time, 0.12f) < 0.06f;
    V2 f = v2_angle(p->aim_angle), s = v2_perp(f);
    SDL_FColor cyan = col_hex(SHIP_COLOR, 1);

    draw_blend_add();
    draw_glow(p->pos, 48.0f, col_a(cyan, 0.18f));
    float flick = 10.0f + 8.0f * sinf(G.time * 70.0f);
    V2 rear = v2_sub(p->pos, v2_scale(f, 10.0f));
    draw_tri(v2_add(rear, v2_scale(s, 7)), v2_sub(rear, v2_scale(s, 7)), v2_sub(rear, v2_scale(f, flick)), col_hex(0xff9a3c, 0.9f));
    draw_blend_alpha();

    if (!blink) {
        V2 nose = v2_add(p->pos, v2_scale(f, 24.0f));
        V2 lw = v2_add(v2_sub(p->pos, v2_scale(f, 14.0f)), v2_scale(s, 16.0f));
        V2 rw = v2_sub(v2_sub(p->pos, v2_scale(f, 14.0f)), v2_scale(s, 16.0f));
        V2 tail = v2_sub(p->pos, v2_scale(f, 5.0f));
        SDL_FColor fill = col_hex(0x0f4d5c, 1);
        draw_tri(nose, lw, tail, fill);
        draw_tri(nose, tail, rw, fill);
        draw_line(nose, lw, 3.0f, cyan);
        draw_line(lw, tail, 3.0f, cyan);
        draw_line(tail, rw, 3.0f, cyan);
        draw_line(rw, nose, 3.0f, cyan);
        draw_circle(v2_add(p->pos, v2_scale(f, 5.0f)), 4.5f, col_hex(0xd8fbff, 1));
    }

    /* The real hitbox, always visible: classic bullet-hell readability. */
    draw_circle(p->pos, PLAYER_HIT_R + 2.0f, col_hex(0xff3355, 1));
    draw_circle(p->pos, PLAYER_HIT_R, COL_WHITE);

    if (p->dash_cd > 0.0f) {
        float k = 1.0f - p->dash_cd / p->dash_cooldown;
        draw_arc(p->pos, 30.0f, 3.0f, -PI_F * 0.5f, -PI_F * 0.5f + TAU_F * k, col_a(COL_WHITE, 0.35f));
    }
}
