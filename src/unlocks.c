#include "audio.h"
#include "game.h"
#include "save.h"

#include <string.h>

/*
 * Unlocks: what a save file has opened up, kept from one run to the next (G.prof, written in the
 * save file with the run and rewritten at once when something is unlocked - save_profile_update).
 *
 * A new save starts with the first ship and UNLOCK_START_COUNT upgrades picked for a first run:
 * plain stat boosts, simple shot effects, a drone, a couple of shot mods. Every boss beaten
 * unlocks the next UNLOCK_BATCH upgrades in a fixed order (the first boss: the FIRST_BOSS list,
 * then every other upgrade in a mixed order), and the first one also the Knight; reaching
 * room UNLOCK_SHOTGUNNER_DEPTH unlocks the Shotgunner. Rolls never offer a locked upgrade
 * (upgrade_roll, upgrade_fresh, upgrade_roll_exclusive, CHAOS); a ship starts with its own mod
 * even while it is locked for the others.
 */

static const int START_UPGRADES[UNLOCK_START_COUNT] = {
    UPG_RAPID,    UPG_DAMAGE,      UPG_MULTI,      UPG_PIERCE,     UPG_BOUNCE,    UPG_HOMING,
    UPG_THRUSTER, UPG_HULL,        UPG_DRONE,      UPG_MAGNET,     UPG_VELOCITY,  UPG_FROST,
    UPG_VENOM,    UPG_CRIT,        UPG_BIG_IRON,   UPG_LONG_BARREL, UPG_PIGGY_BANK, UPG_BOMB_BELT,
    UPG_SKELETON_KEY, UPG_BARRIER, UPG_ORB,        UPG_LEECH,      UPG_W_SCATTER, UPG_W_SEEKER,
};

/* The first boss beaten: a few showy shot mods, a kill trigger, a safety net, a first workshop augment. */
static const int FIRST_BOSS[UNLOCK_BATCH] = {
    UPG_W_LASER, UPG_W_ROCKETS, UPG_W_FLAMER, UPG_CHAIN_REACTION, UPG_SECOND_WIND, UPG_X_BLOOD_ROUNDS,
};

/* Every upgrade in the order it unlocks (start, first boss, then the rest in a fixed mixed order).
 * A constant built once, not game state. */
static int order[UPG_COUNT], norder;

static uint32_t mix(uint32_t u) {
    u = (u + 0x7f4a7c15u) * 0x85ebca6bu;
    u ^= u >> 13;
    return u * 0xc2b2ae35u;
}

static void build_order(void) {
    if (norder) return;
    bool used[UPG_COUNT] = {0};
    for (int i = 0; i < UNLOCK_START_COUNT; i++) order[norder++] = START_UPGRADES[i], used[START_UPGRADES[i]] = true;
    for (int i = 0; i < UNLOCK_BATCH; i++) order[norder++] = FIRST_BOSS[i], used[FIRST_BOSS[i]] = true;
    int first = norder;
    for (int u = 0; u < UPG_COUNT; u++)
        if (!used[u]) order[norder++] = u;
    for (int a = first + 1; a < norder; a++) /* fixed shuffle: categories mixed */
        for (int b = a; b > first && mix((uint32_t)order[b]) < mix((uint32_t)order[b - 1]); b--) {
            int t = order[b];
            order[b] = order[b - 1];
            order[b - 1] = t;
        }
}

void profile_fresh(Profile *p) {
    memset(p, 0, sizeof *p);
    for (int i = 0; i < UNLOCK_START_COUNT; i++) p->upg[START_UPGRADES[i]] = 1;
    p->chr[CHAR_NORMAL] = 1;
}

void profile_unlock_all(Profile *p) {
    memset(p, 0, sizeof *p);
    memset(p->upg, 1, sizeof p->upg);
    memset(p->chr, 1, sizeof p->chr);
}

bool upgrade_unlocked(int u) { return u >= 0 && u < UPG_COUNT && G.prof.upg[u]; }

bool character_unlocked(int c) { return c >= 0 && c < CHAR_COUNT && G.prof.chr[c]; }

int profile_upgrades_unlocked(const Profile *p) {
    int n = 0;
    for (int u = 0; u < UPG_COUNT; u++) n += p->upg[u] != 0;
    return n;
}

int profile_unlocked(const Profile *p) {
    int n = profile_upgrades_unlocked(p);
    for (int c = 0; c < CHAR_COUNT; c++) n += p->chr[c] != 0;
    return n;
}

int unlocks_total(void) { return UPG_COUNT + CHAR_COUNT; }

int unlock_percent(int unlocked, int total) { return total > 0 ? unlocked * 100 / total : 0; }

const char *character_unlock_hint(int c) {
    switch (c) {
    case CHAR_SHOTGUNNER: return "Reach room 5";
    case CHAR_KNIGHT: return "Defeat a boss";
    default: return "";
    }
}

int unlocks_next(int *out, int max) {
    build_order();
    int n = 0;
    for (int i = 0; i < norder && n < max; i++)
        if (!G.prof.upg[order[i]]) out[n++] = order[i];
    return n;
}

static void unlock_ship(int c, char *names, size_t size) {
    if (G.prof.chr[c]) return;
    G.prof.chr[c] = 1;
    size_t len = strlen(names);
    SDL_snprintf(names + len, size - len, "%sNEW SHIP: %s", len ? "  +  " : "", CHARACTERS[c].name);
}

void unlocks_on_room(int depth) {
    if (G.demo) return;
    if (depth > G.prof.best_depth) G.prof.best_depth = depth;
    if (depth >= UNLOCK_SHOTGUNNER_DEPTH && !G.prof.chr[CHAR_SHOTGUNNER]) {
        char sub[64] = "";
        unlock_ship(CHAR_SHOTGUNNER, sub, sizeof sub);
        fx_banner("UNLOCKED", sub, col_hex(CHARACTERS[CHAR_SHOTGUNNER].color, 1), 3.0f);
        sfx_play(SFX_POWER_UP);
        /* saved with the room, a moment later (room entry) */
    }
}

void unlocks_boss_beaten(void) {
    if (G.demo) return;
    G.prof.bosses++;
    char sub[200] = "";
    unlock_ship(CHAR_KNIGHT, sub, sizeof sub);
    int next[UNLOCK_BATCH], n = unlocks_next(next, UNLOCK_BATCH);
    for (int i = 0; i < n; i++) {
        G.prof.upg[next[i]] = 1;
        size_t len = strlen(sub);
        SDL_snprintf(sub + len, sizeof sub - len, "%s%s", i == 0 ? (len ? "  +  " : "") : ", ", UPGRADES[next[i]].name);
    }
    if (sub[0]) {
        fx_banner("BOSS DEFEATED - UNLOCKED", sub, col_hex(COLOR_BOSS, 1), 4.0f);
        sfx_play(SFX_POWER_UP);
    }
    save_profile_update(); /* at once: the unlocks are kept even if the run ends in this room */
}
