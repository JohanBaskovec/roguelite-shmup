#include "draw.h"
#include "game.h"

#include <string.h>

#define ARENA_W (ARENA_X1 - ARENA_X0)
#define ARENA_H (ARENA_Y1 - ARENA_Y0)

V2 door_position(Side side) {
    switch (side) {
    case SIDE_LEFT: return v2(ARENA_X0, ARENA_CY);
    case SIDE_TOP: return v2(ARENA_CX, ARENA_Y0);
    case SIDE_RIGHT: return v2(ARENA_X1, ARENA_CY);
    default: return v2(ARENA_CX, ARENA_Y1);
    }
}

static V2 side_inward(Side side) {
    switch (side) {
    case SIDE_LEFT: return v2(1, 0);
    case SIDE_TOP: return v2(0, 1);
    case SIDE_RIGHT: return v2(-1, 0);
    default: return v2(0, -1);
    }
}

/* ---- Collision ----------------------------------------------------------- */

bool room_collide_circle(V2 *p, float r, V2 *normal) {
    bool hit = false;
    V2 n = v2(0, 0);
    if (p->x < ARENA_X0 + r) { p->x = ARENA_X0 + r; n.x = 1; hit = true; }
    if (p->x > ARENA_X1 - r) { p->x = ARENA_X1 - r; n.x = -1; hit = true; }
    if (p->y < ARENA_Y0 + r) { p->y = ARENA_Y0 + r; n.y = 1; hit = true; }
    if (p->y > ARENA_Y1 - r) { p->y = ARENA_Y1 - r; n.y = -1; hit = true; }

    const Room *room = &G.room;
    for (int i = 0; i < room->nobs; i++) {
        const SDL_FRect *o = &room->obs[i];
        float cx = clampf(p->x, o->x, o->x + o->w);
        float cy = clampf(p->y, o->y, o->y + o->h);
        float dx = p->x - cx, dy = p->y - cy;
        float d2 = dx * dx + dy * dy;
        if (d2 >= r * r) continue;
        hit = true;
        if (d2 > 1e-6f) {
            float d = sqrtf(d2);
            n = v2(dx / d, dy / d);
            *p = v2_add(*p, v2_scale(n, r - d));
        } else {
            /* Center inside the rectangle: push out along the shallowest axis. */
            float l = p->x - o->x, rr = o->x + o->w - p->x;
            float t = p->y - o->y, b = o->y + o->h - p->y;
            float m = fminf(fminf(l, rr), fminf(t, b));
            if (m == l) { p->x = o->x - r; n = v2(-1, 0); }
            else if (m == rr) { p->x = o->x + o->w + r; n = v2(1, 0); }
            else if (m == t) { p->y = o->y - r; n = v2(0, -1); }
            else { p->y = o->y + o->h + r; n = v2(0, 1); }
        }
    }
    if (normal) *normal = v2_norm(n);
    return hit;
}

/* ---- Generation ---------------------------------------------------------- */

static bool rect_overlaps(SDL_FRect a, SDL_FRect b) {
    return a.x < b.x + b.w && a.x + a.w > b.x && a.y < b.y + b.h && a.y + a.h > b.y;
}

static bool rect_ok(SDL_FRect r) {
    const float margin = 80.0f;
    if (r.x < ARENA_X0 + margin || r.y < ARENA_Y0 + margin || r.x + r.w > ARENA_X1 - margin ||
        r.y + r.h > ARENA_Y1 - margin)
        return false;
    /* Keep door lanes, the entry point and the reward spot clear. */
    const SDL_FRect keep_out[] = {
        {ARENA_X0, ARENA_CY - 170, 280, 340},           /* left door */
        {ARENA_X1 - 280, ARENA_CY - 170, 280, 340},     /* right door */
        {ARENA_CX - 170, ARENA_Y0, 340, 260},           /* top door */
        {ARENA_CX - 200, ARENA_Y1 - 280, 400, 280},     /* entry */
        {ARENA_CX - 120, ARENA_CY - 120, 240, 240},     /* reward */
    };
    for (int i = 0; i < ARRAY_LEN(keep_out); i++)
        if (rect_overlaps(r, keep_out[i])) return false;
    for (int i = 0; i < G.room.nobs; i++) {
        SDL_FRect o = G.room.obs[i];
        SDL_FRect grown = {o.x - 60, o.y - 60, o.w + 120, o.h + 120};
        if (rect_overlaps(r, grown)) return false;
    }
    return true;
}

static void add_obs(float cx, float cy, float w, float h) {
    if (G.room.nobs >= MAX_OBSTACLES) return;
    SDL_FRect r = {cx - w * 0.5f, cy - h * 0.5f, w, h};
    if (rect_ok(r)) G.room.obs[G.room.nobs++] = r;
}

static void add_mirrored(float cx, float cy, float w, float h) {
    add_obs(cx, cy, w, h);
    add_obs(2.0f * ARENA_CX - cx, cy, w, h);
}

static void gen_obstacles(void) {
    G.room.nobs = 0;
    switch (irand(0, 5)) {
    case 0: /* open arena */
        break;
    case 1: /* four pillars */
        add_mirrored(ARENA_X0 + ARENA_W * 0.27f, ARENA_Y0 + ARENA_H * 0.3f, 100, 100);
        add_mirrored(ARENA_X0 + ARENA_W * 0.27f, ARENA_Y0 + ARENA_H * 0.72f, 100, 100);
        break;
    case 2: /* two vertical walls */
        add_mirrored(ARENA_X0 + ARENA_W * 0.33f, ARENA_CY, 50, 340);
        break;
    case 3: { /* random mirrored blocks */
        int n = irand(2, 5);
        for (int i = 0; i < n; i++)
            add_mirrored(frand(ARENA_X0 + 150, ARENA_CX - 130), frand(ARENA_Y0 + 140, ARENA_Y1 - 140),
                         frand(50, 150), frand(50, 150));
    } break;
    case 4: /* horizontal bars */
        add_mirrored(ARENA_X0 + ARENA_W * 0.27f, ARENA_Y0 + ARENA_H * 0.26f, 280, 44);
        add_mirrored(ARENA_X0 + ARENA_W * 0.27f, ARENA_Y0 + ARENA_H * 0.74f, 280, 44);
        break;
    default: /* ring of pillars around the center */
        for (int i = 0; i < 8; i++) {
            float a = TAU_F * ((float)i + 0.5f) / 8.0f;
            add_obs(ARENA_CX + cosf(a) * 380.0f, ARENA_CY + sinf(a) * 260.0f, 64, 64);
        }
        break;
    }
}

static RewardType roll_reward(void) {
    static const int weights[RW_COUNT] = {
        [RW_COINS] = 30, [RW_KEY] = 16, [RW_BOMBS] = 16, [RW_HEART] = 14, [RW_UPGRADE] = 14, [RW_WEAPON] = 10,
    };
    int total = 0;
    for (int i = 0; i < RW_COUNT; i++) total += weights[i];
    int roll = irand(0, total - 1);
    for (int i = 0; i < RW_COUNT; i++) {
        if (roll < weights[i]) return (RewardType)i;
        roll -= weights[i];
    }
    return RW_COINS;
}

static void gen_doors(void) {
    Room *r = &G.room;
    static const Side sides[MAX_DOORS] = {SIDE_LEFT, SIDE_TOP, SIDE_RIGHT};
    int locked = chance(0.5f) ? irand(0, MAX_DOORS - 1) : -1;
    r->ndoors = MAX_DOORS;
    for (int i = 0; i < MAX_DOORS; i++) {
        Door *d = &r->doors[i];
        memset(d, 0, sizeof *d);
        d->side = sides[i];
        d->pos = door_position(sides[i]);
        d->type = ROOM_COMBAT; /* later: treasure rooms, shops, bosses... */
        d->locked = (i == locked);
        if (d->locked) {
            /* Locked doors always hide something good. */
            d->reward = chance(0.5f) ? RW_WEAPON : RW_UPGRADE;
            continue;
        }
        for (int tries = 0; tries < 12; tries++) {
            d->reward = roll_reward();
            bool dup = false;
            for (int j = 0; j < i; j++)
                if (r->doors[j].reward == d->reward) dup = true;
            if (!dup) break;
        }
    }
}

void room_enter(RoomType type, RewardType reward, int depth) {
    Room *r = &G.room;
    memset(r, 0, sizeof *r);
    r->type = type;
    r->reward = reward;
    r->depth = depth;
    r->hue = 200.0f + (float)depth * 37.0f;

    G.npb = G.neb = G.nen = G.nsp = G.npk = 0;
    fx_clear();

    G.pl.pos = v2(ARENA_CX, ARENA_Y1 - 80.0f);
    G.pl.vel = v2(0, -250.0f);
    G.pl.dash_t = 0.0f;
    G.pl.invuln = fmaxf(G.pl.invuln, 0.6f);

    if (type == ROOM_START) {
        r->cleared = true;
        r->clear_t = 10.0f;
    } else {
        gen_obstacles();
        int extra = depth / 3;
        r->nwaves = 2 + (extra > 3 ? 3 : extra);
        r->wave_t = 0.35f;
    }
    gen_doors();
}

/* ---- Waves --------------------------------------------------------------- */

static V2 find_spawn_point(float rad) {
    for (int tries = 0; tries < 40; tries++) {
        V2 p = v2(frand(ARENA_X0 + 90, ARENA_X1 - 90), frand(ARENA_Y0 + 90, ARENA_Y1 - 90));
        if (v2_dist2(p, G.pl.pos) < 400.0f * 400.0f) continue;
        V2 q = p;
        if (room_collide_circle(&q, rad + 12.0f, NULL)) continue;
        return p;
    }
    return v2(ARENA_CX, ARENA_Y0 + 160.0f);
}

static void spawn_wave(void) {
    Room *r = &G.room;
    int depth = r->depth;
    float budget = 6.0f + (float)depth * 2.4f + (float)r->wave * 2.0f;
    int queued = 0;
    while (budget >= 1.0f && queued < 26) {
        EnemyType cand[EN_COUNT];
        int nc = 0;
        for (int t = 0; t < EN_COUNT; t++)
            if (ENEMIES[t].min_depth <= depth && (float)ENEMIES[t].cost <= budget) cand[nc++] = (EnemyType)t;
        if (nc == 0) break;
        EnemyType t = cand[irand(0, nc - 1)];
        budget -= (float)ENEMIES[t].cost;
        V2 p = find_spawn_point(ENEMIES[t].radius);
        float delay = 0.65f + frand(0.0f, 0.5f);
        if (t == EN_SWARM) {
            for (int k = 0; k < 5; k++)
                enemy_queue_spawn(EN_SWARM, v2_add(p, v2_scale(v2_angle(TAU_F * (float)k / 5.0f), 34.0f)), delay);
            queued += 5;
        } else {
            enemy_queue_spawn(t, p, delay);
            queued++;
        }
    }
    r->last_wave_size = queued;
    r->wave++;
    r->wave_t = 13.0f;
    r->since_wave = 0.0f;
}

static void room_clear(void) {
    Room *r = &G.room;
    r->cleared = true;
    r->clear_t = 0.0f;
    G.rooms_cleared++;
    ebullets_clear(true);
    if (r->reward != RW_NONE) reward_drop(r->reward, v2(ARENA_CX, ARENA_CY));
    fx_banner("ROOM CLEARED", "choose your next door", col_hex(0x66f0ff, 1), 1.4f);
    fx_shake(0.25f);
}

static bool touching_door(const Door *d) {
    V2 p = G.pl.pos;
    float reach = PLAYER_BODY_R + 6.0f;
    switch (d->side) {
    case SIDE_LEFT: return p.x <= ARENA_X0 + reach && fabsf(p.y - ARENA_CY) < DOOR_HALF;
    case SIDE_RIGHT: return p.x >= ARENA_X1 - reach && fabsf(p.y - ARENA_CY) < DOOR_HALF;
    case SIDE_TOP: return p.y <= ARENA_Y0 + reach && fabsf(p.x - ARENA_CX) < DOOR_HALF;
    default: return false;
    }
}

void room_update(void) {
    Room *r = &G.room;
    if (r->msg_cd > 0.0f) r->msg_cd -= DT;
    for (int i = 0; i < r->ndoors; i++)
        if (r->doors[i].anim > 0.0f) r->doors[i].anim -= DT;

    if (!r->cleared) {
        r->wave_t -= DT;
        r->since_wave += DT;
        int alive = G.nen + G.nsp;
        if (r->wave < r->nwaves) {
            bool trigger = r->wave_t <= 0.0f ||
                           (r->wave > 0 && r->since_wave > 1.5f && alive <= r->last_wave_size / 4);
            if (trigger) spawn_wave();
        } else if (alive == 0) {
            room_clear();
        }
        return;
    }

    r->clear_t += DT;
    for (int i = 0; i < r->ndoors; i++) {
        Door *d = &r->doors[i];
        if (!touching_door(d)) continue;
        if (d->locked) {
            if (G.pl.keys > 0) {
                G.pl.keys--;
                d->locked = false;
                d->anim = 0.3f;
                fx_popup(v2_add(d->pos, v2_scale(side_inward(d->side), 90)), col_hex(0xffe066, 1), "UNLOCKED");
                fx_burst(d->pos, 30, 100, 400, 0.4f, 3, col_hex(0xffe066, 1));
                fx_shake(0.2f);
            } else if (r->msg_cd <= 0.0f) {
                fx_popup(v2_add(d->pos, v2_scale(side_inward(d->side), 90)), col_hex(0xff5f7e, 1), "NEED A KEY");
                r->msg_cd = 1.0f;
            }
            continue;
        }
        if (d->anim > 0.0f) continue;
        game_start_transition(i);
        break;
    }
}

/* ---- Drawing ------------------------------------------------------------- */

static SDL_FColor reward_color(RewardType rw) {
    switch (rw) {
    case RW_COINS: return col_hex(0xffd23f, 1);
    case RW_KEY: return col_hex(0xd9e3f0, 1);
    case RW_BOMBS: return col_hex(0xff8a3d, 1);
    case RW_HEART: return col_hex(0xff3355, 1);
    case RW_WEAPON: return col_hex(0x66f0ff, 1);
    case RW_UPGRADE: return col_hex(0xb57cff, 1);
    default: return col_hex(0x8899aa, 1);
    }
}

void room_draw_floor(void) {
    const Room *r = &G.room;
    SDL_FColor floor = col_hsv(r->hue, 0.55f, 0.075f, 1.0f);
    SDL_FColor grid = col_hsv(r->hue, 0.6f, 0.9f, 0.05f);
    draw_rect(ARENA_X0, ARENA_Y0, ARENA_W, ARENA_H, floor);
    for (float x = ARENA_X0 + 60.0f; x < ARENA_X1; x += 60.0f)
        draw_line(v2(x, ARENA_Y0), v2(x, ARENA_Y1), 1.5f, grid);
    for (float y = ARENA_Y0 + 60.0f; y < ARENA_Y1; y += 60.0f)
        draw_line(v2(ARENA_X0, y), v2(ARENA_X1, y), 1.5f, grid);

    /* Soft inner shading towards the walls. */
    SDL_FColor dark = rgba(0, 0, 0, 0.45f), clear = rgba(0, 0, 0, 0);
    const float s = 90.0f;
    draw_quad_grad(v2(ARENA_X0, ARENA_Y0), v2(ARENA_X1, ARENA_Y0), v2(ARENA_X1, ARENA_Y0 + s), v2(ARENA_X0, ARENA_Y0 + s), dark, dark, clear, clear);
    draw_quad_grad(v2(ARENA_X0, ARENA_Y1 - s), v2(ARENA_X1, ARENA_Y1 - s), v2(ARENA_X1, ARENA_Y1), v2(ARENA_X0, ARENA_Y1), clear, clear, dark, dark);
    draw_quad_grad(v2(ARENA_X0, ARENA_Y0), v2(ARENA_X0 + s, ARENA_Y0), v2(ARENA_X0 + s, ARENA_Y1), v2(ARENA_X0, ARENA_Y1), dark, clear, clear, dark);
    draw_quad_grad(v2(ARENA_X1 - s, ARENA_Y0), v2(ARENA_X1, ARENA_Y0), v2(ARENA_X1, ARENA_Y1), v2(ARENA_X1 - s, ARENA_Y1), clear, dark, dark, clear);

    if (r->type == ROOM_START) {
        SDL_FColor c = col_a(COL_WHITE, 0.35f);
        float y = ARENA_CY - 70.0f;
        draw_text_centered(ARENA_CX, y, 2.0f, c, "WASD / ZQSD / ARROWS - MOVE");
        draw_text_centered(ARENA_CX, y + 30, 2.0f, c, "MOUSE - AIM & SHOOT");
        draw_text_centered(ARENA_CX, y + 60, 2.0f, c, "SPACE / SHIFT / RIGHT CLICK - DASH");
        draw_text_centered(ARENA_CX, y + 90, 2.0f, c, "E - BOMB    WHEEL / 1-5 - SWITCH WEAPON");
        draw_text_centered(ARENA_CX, y + 140, 2.0f, col_a(col_hex(0xffe066, 1), 0.5f), "LOCKED DOORS COST A KEY BUT HIDE BETTER LOOT");
    }
}

static void draw_padlock(V2 p, float s) {
    SDL_FColor gold = col_hex(0xffd23f, 1);
    draw_arc(v2(p.x, p.y - s * 0.35f), s * 0.55f, s * 0.25f, PI_F, TAU_F, gold);
    draw_rect(p.x - s * 0.8f, p.y - s * 0.4f, s * 1.6f, s * 1.2f, gold);
    draw_circle(v2(p.x, p.y + s * 0.1f), s * 0.2f, col_hex(0x553300, 1));
}

static void draw_door(const Door *d, bool cleared, bool entry) {
    Side side = d->side;
    V2 p = d->pos;
    bool horizontal_wall = (side == SIDE_TOP || side == SIDE_BOTTOM);
    float gx, gy, gw, gh;
    V2 in = side_inward(side);
    if (horizontal_wall) {
        gx = p.x - DOOR_HALF; gw = DOOR_HALF * 2;
        gy = side == SIDE_TOP ? p.y - WALL_T : p.y; gh = WALL_T;
    } else {
        gy = p.y - DOOR_HALF; gh = DOOR_HALF * 2;
        gx = side == SIDE_LEFT ? p.x - WALL_T : p.x; gw = WALL_T;
    }
    SDL_FColor rc = entry ? col_hex(0x445566, 1) : reward_color(d->reward);
    bool open = cleared && !d->locked && !entry;
    float pulse = 0.5f + 0.5f * sinf(G.time * 6.0f);

    if (open) {
        draw_blend_add();
        draw_glow(v2_add(p, v2_scale(in, 10)), 130.0f, col_a(rc, 0.25f + 0.15f * pulse));
        draw_blend_alpha();
    }
    draw_rect(gx, gy, gw, gh, open ? rgba(0.0f, 0.0f, 0.0f, 1.0f) : col_hex(0x10131c, 1));

    if (!open) {
        /* Energy bars across the doorway. */
        SDL_FColor bar = entry ? col_hex(0x334455, 1)
                       : d->locked && cleared ? col_hex(0xffd23f, 0.9f)
                       : col_hex(0xff3355, 0.6f + 0.4f * pulse);
        for (int i = 0; i < 4; i++) {
            float t = ((float)i + 0.5f) / 4.0f;
            if (horizontal_wall) draw_rect(gx + 2, gy + gh * t - 2, gw - 4, 4, bar);
            else draw_rect(gx + gw * t - 2, gy + 2, 4, gh - 4, bar);
        }
    } else {
        /* Animated chevrons pulling the player in. */
        V2 out = v2_scale(in, -1.0f);
        V2 side_v = v2_perp(out);
        for (int i = 0; i < 3; i++) {
            float k = fmodf(G.time * 1.6f + (float)i / 3.0f, 1.0f);
            V2 c = v2_add(p, v2_scale(in, 70.0f - k * 60.0f));
            SDL_FColor cc = col_a(rc, 0.7f * sinf(k * PI_F));
            draw_line(v2_add(c, v2_scale(side_v, 18)), v2_add(c, v2_scale(out, 12)), 4, cc);
            draw_line(v2_sub(c, v2_scale(side_v, 18)), v2_add(c, v2_scale(out, 12)), 4, cc);
        }
    }

    /* Frame posts. */
    SDL_FColor post = entry ? col_hex(0x334455, 1) : rc;
    if (horizontal_wall) {
        draw_rect(gx - 8, gy - 4, 8, gh + 8, post);
        draw_rect(gx + gw, gy - 4, 8, gh + 8, post);
    } else {
        draw_rect(gx - 4, gy - 8, gw + 8, 8, post);
        draw_rect(gx - 4, gy + gh, gw + 8, 8, post);
    }

    if (entry) return;

    /* Reward preview, always visible so the player can plan mid-fight. */
    V2 ip = v2_add(p, v2_scale(in, 78.0f));
    ip.y += sinf(G.time * 3.0f + p.x) * 3.0f;
    draw_blend_add();
    draw_glow(ip, 34.0f, col_a(rc, 0.35f));
    draw_blend_alpha();
    draw_ngon(ip, 24.0f, 6, PI_F / 6.0f, col_hex(0x0a0c14, 0.85f));
    draw_ngon_outline(ip, 24.0f, 6, PI_F / 6.0f, 2.0f, col_a(rc, 0.8f));
    draw_pickup_icon(reward_icon(d->reward), -1, ip, 11.0f, G.time);
    if (d->locked) draw_padlock(v2_add(p, v2_scale(in, -WALL_T * 0.5f)), 16.0f);
    if (cleared) draw_text_centered(ip.x, ip.y + 34.0f, 1.5f, col_a(rc, 0.9f), "%s", reward_name(d->reward));
}

void room_draw_walls(void) {
    const Room *r = &G.room;
    SDL_FColor wall = col_hsv(r->hue, 0.35f, 0.16f, 1.0f);
    SDL_FColor edge = col_hsv(r->hue, 0.7f, 1.0f, 0.75f);
    draw_rect(ARENA_X0 - WALL_T, ARENA_Y0 - WALL_T, ARENA_W + WALL_T * 2, WALL_T, wall);
    draw_rect(ARENA_X0 - WALL_T, ARENA_Y1, ARENA_W + WALL_T * 2, WALL_T, wall);
    draw_rect(ARENA_X0 - WALL_T, ARENA_Y0, WALL_T, ARENA_H, wall);
    draw_rect(ARENA_X1, ARENA_Y0, WALL_T, ARENA_H, wall);
    draw_rect_outline(ARENA_X0 - 3, ARENA_Y0 - 3, ARENA_W + 6, ARENA_H + 6, 3, edge);
    draw_rect_outline(ARENA_X0 - WALL_T, ARENA_Y0 - WALL_T, ARENA_W + WALL_T * 2, ARENA_H + WALL_T * 2, 2, col_a(edge, 0.3f));

    for (int i = 0; i < r->nobs; i++) {
        SDL_FRect o = r->obs[i];
        draw_rect(o.x + 6, o.y + 8, o.w, o.h, rgba(0, 0, 0, 0.35f)); /* drop shadow */
        draw_rect(o.x, o.y, o.w, o.h, wall);
        draw_rect(o.x + 6, o.y + 6, o.w - 12, o.h - 12, col_mul(wall, 1.35f));
        draw_rect_outline(o.x, o.y, o.w, o.h, 3, edge);
    }

    Door entry = {.side = SIDE_BOTTOM, .pos = door_position(SIDE_BOTTOM)};
    draw_door(&entry, r->cleared, true);
    for (int i = 0; i < r->ndoors; i++) draw_door(&r->doors[i], r->cleared, false);
}
