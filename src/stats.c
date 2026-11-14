#include "game.h"

#include <stdio.h>

/*
 * Stat trades offered in Stats rooms: every offer raises one stat and lowers
 * another. Gains are a bit larger than losses so a trade is a net upgrade that
 * mostly shapes the build. Each stat has a floor/cap so trades that would push
 * a stat out of range are never offered.
 */

const StatDef STATS[ST_COUNT] = {
    [ST_DAMAGE] = {"DAMAGE", "+25% DAMAGE", "-15% DAMAGE"},
    [ST_FIRE_RATE] = {"FIRE RATE", "+25% FIRE RATE", "-15% FIRE RATE"},
    [ST_RANGE] = {"RANGE", "+30% RANGE", "-20% RANGE"},
    [ST_MAX_HP] = {"MAX HP", "+1 HEART", "-1 HEART"},
    [ST_MOVE_SPEED] = {"SPEED", "+12% MOVE SPEED", "-10% MOVE SPEED"},
    [ST_DASH] = {"DASH", "+33% DASH RECHARGE", "-25% DASH RECHARGE"},
    [ST_SHOT_SPEED] = {"SHOT SPEED", "+25% SHOT SPEED", "-15% SHOT SPEED"},
    [ST_HITBOX] = {"HITBOX", "-20% HITBOX SIZE", "+25% HITBOX SIZE"},
    [ST_LUCK] = {"LUCK", "+1 LUCK", "-1 LUCK"},
};

bool stat_can_change(int s, bool up) {
    const Player *p = &G.pl;
    switch (s) {
    case ST_DAMAGE: return up || p->dmg_mult * 0.85f >= 0.3f;
    case ST_FIRE_RATE: return up || p->rate_mult * 0.85f >= 0.3f;
    case ST_RANGE: return up ? p->range_mult < 3.0f : p->range_mult * 0.8f >= 0.35f;
    case ST_MAX_HP: return up ? p->max_hp + 2 <= 24 : p->max_hp - 2 >= 2;
    case ST_MOVE_SPEED: return up ? p->speed * 1.12f <= 900.0f : p->speed * 0.9f >= 330.0f;
    case ST_DASH: return up ? p->dash_cooldown * 0.75f >= 0.15f : p->dash_cooldown * 1.33f <= 2.0f;
    case ST_SHOT_SPEED: return up ? p->shot_speed_mult < 3.0f : p->shot_speed_mult * 0.85f >= 0.4f;
    case ST_HITBOX: return up ? p->hit_radius * 0.8f >= 2.0f : p->hit_radius * 1.25f <= 9.0f;
    case ST_LUCK: return up ? p->luck < 5 : p->luck > -3;
    default: return false;
    }
}

void stat_apply(int s, bool up) {
    Player *p = &G.pl;
    switch (s) {
    case ST_DAMAGE: p->dmg_mult *= up ? 1.25f : 0.85f; break;
    case ST_FIRE_RATE: p->rate_mult *= up ? 1.25f : 0.85f; break;
    case ST_RANGE: p->range_mult *= up ? 1.3f : 0.8f; break;
    case ST_MAX_HP:
        p->max_hp += up ? 2 : -2;
        if (up) p->hp += 2;
        if (p->hp > p->max_hp) p->hp = p->max_hp;
        break;
    case ST_MOVE_SPEED: p->speed *= up ? 1.12f : 0.9f; break;
    case ST_DASH: p->dash_cooldown *= up ? 0.75f : 1.33f; break;
    case ST_SHOT_SPEED: p->shot_speed_mult *= up ? 1.25f : 0.85f; break;
    case ST_HITBOX: p->hit_radius *= up ? 0.8f : 1.25f; break;
    case ST_LUCK: p->luck += up ? 1 : -1; break;
    default: break;
    }
}

float luck_drop_factor(void) { return fmaxf(0.25f, 1.0f + 0.3f * (float)G.pl.luck); }

int stats_roll_offers(int *out, int max) {
    /* Distinct raised stats so the choice is about direction, not magnitude. */
    int ups[ST_COUNT], nup = 0;
    for (int s = 0; s < ST_COUNT; s++)
        if (stat_can_change(s, true)) ups[nup++] = s;
    for (int i = nup - 1; i > 0; i--) {
        int j = irand(0, i), tmp = ups[i];
        ups[i] = ups[j];
        ups[j] = tmp;
    }
    int n = 0;
    for (int i = 0; i < nup && n < max; i++) {
        int downs[ST_COUNT], nd = 0;
        for (int s = 0; s < ST_COUNT; s++)
            if (s != ups[i] && stat_can_change(s, false)) downs[nd++] = s;
        if (nd == 0) continue;
        out[n++] = stat_offer_encode(ups[i], downs[irand(0, nd - 1)]);
    }
    return n;
}

void stat_offer_take(int offer) {
    int up = stat_offer_up(offer), down = stat_offer_down(offer);
    stat_apply(up, true);
    stat_apply(down, false);
    fx_banner(STATS[up].up_text, STATS[down].down_text, col_hex(0x4dffd2, 1), 2.6f);
}
