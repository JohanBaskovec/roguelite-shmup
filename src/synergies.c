#include "audio.h"
#include "draw.h"
#include "game.h"

/*
 * Synergies: effects that only exist while several upgrades are turned on together. They are not
 * items: owning (and keeping on) the parts is enough, CHAOS loans included. The effects themselves
 * live where the upgrades do (upgrades.c, weapons.c, bullets.c...), read through syn().
 */

#define U(x) ((x) + 1)
#define FIRE {U(UPG_THERMITE), U(UPG_PYROMANIAC), U(UPG_W_FLAMER), U(UPG_X_HELLFIRE), U(UPG_TRAIL)}
#define CHILL {U(UPG_FROST), U(UPG_FROSTBITE), U(UPG_SHATTER)}
#define POISON {U(UPG_VENOM), U(UPG_TOXIC_CLOUD), U(UPG_X_BLOOD_ROUNDS)}
#define LIGHTNING {U(UPG_ARC), U(UPG_W_ARC), U(UPG_X_STORM), U(UPG_CONDUCTOR), U(UPG_TESLA)}

const SynergyDef SYNERGIES[SYN_COUNT] = {
    [SYN_THERMAL_SHOCK] = {"THERMAL SHOCK", "A burning enemy that gets chilled|takes a burst of damage", {0}, FIRE, CHILL, 0xfff0e0},
    [SYN_COMBUSTION] = {"COMBUSTION", "Poison on a burning enemy blows up|and sets its neighbours ablaze", {0}, FIRE, POISON, 0xc8ff4a},
    [SYN_FLASH_FREEZE] = {"FLASH FREEZE", "Lightning reaches further and deals|double damage to chilled enemies", {0}, CHILL, LIGHTNING, 0xa8e0ff},
    [SYN_PLAGUE] = {"PLAGUE", "Poisoning an enemy poisons every|enemy of the same type", {U(UPG_HIVEMIND)}, POISON, {0}, 0x9aff3a},

    [SYN_PARRY] = {"PARRY", "The blade sends enemy bullets back|instead of destroying them", {U(UPG_W_SWORD), U(UPG_DEFLECTOR)}, {0}, {0}, 0x7dffef},
    [SYN_BLADE_DANCER] = {"BLADE DANCER", "The shadow clone swings|a blade of its own", {U(UPG_W_SWORD), U(UPG_CLONE)}, {0}, {0}, 0x9aa8ff},
    [SYN_THROWN_SWORD] = {"THROWN BLADE", "Every other swing throws a|spinning blade that flies back", {U(UPG_W_SWORD), U(UPG_BOOMERANG)}, {0}, {0}, 0x66ffd9},

    [SYN_TESLA_BEAM] = {"TESLA BEAM", "While you fire, bolts leap from|the ship to the nearest enemy", {U(UPG_W_LASER), U(UPG_W_ARC)}, {0}, {0}, 0xb8d0ff},
    [SYN_PLASMA] = {"PLASMA CUTTER", "Where the beam ends, it leaves|the ground burning", {U(UPG_W_LASER), U(UPG_W_FLAMER)}, {0}, {0}, 0xff9a4a},
    [SYN_EVENT_HORIZON] = {"EVENT HORIZON", "Every shot collapses into|a vortex where it dies", {U(UPG_W_RAILGUN), U(UPG_W_GRAVITON)}, {0}, {0}, 0x8a5aff},
    [SYN_SAW_HALO] = {"SAW HALO", "The blades settle into a ring|of saws around the ship", {U(UPG_W_CHAKRAM), U(UPG_W_SPIRAL)}, {0}, {0}, 0xa8ff5a},
    [SYN_MIRV] = {"MIRV", "Cluster fragments become homing|rockets that explode", {U(UPG_W_ROCKETS), U(UPG_W_SEEKER), U(UPG_W_CLUSTER)}, {0}, {0}, 0xffb04d},

    [SYN_MIRROR_SHIELD] = {"MIRROR SHIELD", "The front shield sends the|bullets it stops back", {U(UPG_AEGIS), U(UPG_DEFLECTOR)}, {0}, {0}, 0x7dffef},
    [SYN_SHIELD_BASH] = {"SHIELD BASH", "Enemies touching the front|shield take heavy damage", {U(UPG_AEGIS)}, {U(UPG_RAM), U(UPG_SPIKED_HULL)}, {0}, 0xffc26b},
    [SYN_STORM_ORBS] = {"STORM ORBS", "The guardian orbs zap|the enemies around them", {U(UPG_ORB), U(UPG_TESLA)}, {0}, {0}, 0x80c8ff},
    [SYN_FROZEN_TIME] = {"FROZEN TIME", "Enemies inside the chrono|bubble are chilled", {U(UPG_BULLET_TIME)}, CHILL, {0}, 0xa8a0ff},
    [SYN_PHOENIX] = {"PHOENIX", "Second Wind burns every enemy and|starts a 12 s Vengeance", {U(UPG_SECOND_WIND), U(UPG_VENGEANCE)}, {0}, {0}, 0xff8a3d},
    [SYN_FAILSAFE] = {"FAILSAFE", "The emergency block also|gives a shield", {U(UPG_BARRIER), U(UPG_EMERGENCY)}, {0}, {0}, 0x66d9ff},

    [SYN_INTERCEPTORS] = {"INTERCEPTORS", "The drones fire the point defense:|faster with every drone", {U(UPG_DRONE), U(UPG_POINT_DEFENSE)}, {0}, {0}, 0xff5a5a},
    [SYN_LIGHTNING_ROD] = {"LIGHTNING ROD", "Drone shots chain lightning", {U(UPG_DRONE), U(UPG_TESLA)}, {0}, {0}, 0xb6ffb0},
    [SYN_DEMOLITION] = {"DEMOLITION", "A bomb sets off every charge and|mine at once, twice as strong", {0}, {U(UPG_STICKY), U(UPG_MINES)}, {0}, 0xffc23d},
    [SYN_SCORCHED_EARTH] = {"SCORCHED EARTH", "Bombs leave a field of fire", {U(UPG_BOMB_FACTORY), U(UPG_BOMB_BELT)}, {0}, {0}, 0xff7a2e},
    [SYN_SEEKER_MINES] = {"SEEKER MINES", "Mines crawl towards|the nearest enemy", {U(UPG_MINES)}, {U(UPG_HOMING), U(UPG_W_SEEKER)}, {0}, 0xffaa33},

    [SYN_STARBURST] = {"STARBURST", "Side shots every volley,|and four diagonal ones", {U(UPG_TAIL_GUN), U(UPG_FLANK)}, {0}, {0}, 0x7dffc8},
    [SYN_ECHO_CHAMBER] = {"ECHO CHAMBER", "The shadow clone's|volleys echo too", {U(UPG_ECHO), U(UPG_CLONE)}, {0}, {0}, 0xb0a0ff},
    [SYN_OVERLOAD] = {"OVERLOAD", "A fully charged volley also|fires a huge mega shot", {U(UPG_CAPACITOR), U(UPG_CHARGE)}, {0}, {0}, 0x9ff0ff},
    [SYN_RAIN_OF_FIRE] = {"RAIN OF FIRE", "Every orbital strike|scatters 3 homing missiles", {U(UPG_MISSILE_POD), U(UPG_ORBITAL)}, {0}, {0}, 0xffb04d},
    [SYN_CHAIN_EXPLOSION] = {"CHAIN EXPLOSION", "Death explosions 50% wider,|10 shots per kill", {U(UPG_VOLATILE), U(UPG_CHAIN_REACTION)}, {0}, {0}, 0xff5a1f},
    [SYN_SINGULARITY] = {"SINGULARITY", "Twice as many vortices; they|last longer and pull from further", {U(UPG_BLACK_HOLE), U(UPG_W_GRAVITON)}, {0}, {0}, 0x8a5aff},

    [SYN_CLOSE_CALL] = {"CLOSE CALL", "Bullets shot down by the point|defense count as grazes", {U(UPG_GRAZE), U(UPG_POINT_DEFENSE)}, {0}, {0}, 0xff8ad8},
    [SYN_UNSTOPPABLE] = {"UNSTOPPABLE", "Kills feed Momentum (+5%)|and it climbs to +80%", {U(UPG_MOMENTUM), U(UPG_RAMPAGE)}, {0}, {0}, 0xffd36b},
    [SYN_EXECUTIONER] = {"EXECUTIONER", "Guillotine executes|below 25% health", {U(UPG_GUILLOTINE), U(UPG_BULLY)}, {0}, {0}, 0xff4466},
    [SYN_DEADEYE] = {"DEADEYE", "The scope's bonus is doubled:|up to +120% damage", {U(UPG_SCOPE), U(UPG_LONG_BARREL)}, {0}, {0}, 0xd8f6ff},
    [SYN_COMPOUND] = {"COMPOUND INTEREST", "The piggy bank pays 6 coins|per room instead of 3", {U(UPG_PIGGY_BANK), U(UPG_BLOOD_MONEY)}, {0}, {0}, 0xffd23f},
    [SYN_TRICK_SHOT] = {"TRICK SHOT", "Shots ricochet 3 times,|+40% damage per jump", {U(UPG_RICOCHET), U(UPG_PINBALL)}, {0}, {0}, 0xffd0a0},
    [SYN_GRAND_FINALE] = {"GRAND FINALE", "Fireworks: 10 bigger,|stronger sparks", {U(UPG_FIREWORKS), U(UPG_SNOWBALL)}, {0}, {0}, 0xff7ae0},
    [SYN_STACKED_DECK] = {"STACKED DECK", "The wild card deals|two bonuses per room", {U(UPG_WILD_CARD), U(UPG_LOADED_DICE)}, {0}, {0}, 0xff6ad0},
    [SYN_STEADY_AIM] = {"STEADY AIM", "Charge Shot charges twice as|fast while standing still", {U(UPG_CHARGE), U(UPG_TURRET_MODE)}, {0}, {0}, 0x8ab8ff},
};

static bool group_ok(const int *g, int n) {
    if (g[0] == 0) return true; /* no such requirement */
    for (int i = 0; i < n && g[i]; i++)
        if (upg_has(g[i] - 1)) return true;
    return false;
}

bool syn(int s) {
    const SynergyDef *d = &SYNERGIES[s];
    for (int i = 0; i < 3 && d->need[i]; i++)
        if (!upg_has(d->need[i] - 1)) return false;
    return group_ok(d->any1, 6) && group_ok(d->any2, 6);
}

int synergies_active(int *out) {
    int n = 0;
    for (int s = 0; s < SYN_COUNT; s++)
        if (syn(s)) out[n++] = s;
    return n;
}

/* "SWORD + DEFLECTOR SHELLS": the parts turned on that make the synergy. */
const char *synergy_parts(int s) {
    static char buf[160];
    const SynergyDef *d = &SYNERGIES[s];
    int len = 0;
    buf[0] = 0;
    const int *groups[3] = {d->need, d->any1, d->any2};
    const int sizes[3] = {3, 6, 6};
    for (int g = 0; g < 3; g++) {
        bool one = g > 0; /* the "any" groups: the first part owned is enough to name */
        for (int i = 0; i < sizes[g] && groups[g][i]; i++) {
            int u = groups[g][i] - 1;
            if (one && !upg_has(u)) continue;
            len += SDL_snprintf(buf + len, sizeof buf - (size_t)len, "%s%s", len ? " + " : "", UPGRADES[u].name);
            if (len >= (int)sizeof buf - 1) return buf;
            if (one) break;
        }
    }
    if (s == SYN_DEMOLITION) SDL_snprintf(buf + len, sizeof buf - (size_t)len, " + A BOMB");
    return buf;
}

/* A banner names each synergy the first time it comes together in a run (one at a time, and never
 * over another banner: an upgrade just taken shows its own first). */
void synergies_update(void) {
    Player *p = &G.pl;
    if (G.banner_t > 0.0f) return;
    for (int s = 0; s < SYN_COUNT; s++) {
        if (p->syn_seen[s] || !syn(s)) continue;
        p->syn_seen[s] = true;
        char sub[200], title[64];
        SDL_snprintf(title, sizeof title, "SYNERGY: %s", SYNERGIES[s].name);
        SDL_snprintf(sub, sizeof sub, "%s", SYNERGIES[s].desc);
        for (char *c = sub; *c; c++)
            if (*c == '|') *c = ' ';
        fx_banner(title, sub, col_hex(SYNERGIES[s].color, 1), 2.4f);
        sfx_play_ex(SFX_POWER_UP, 0.8f, 1.3f, 0.0f);
        fx_particle(p->pos, v2(0, 0), 0.5f, 240.0f, col_hex(SYNERGIES[s].color, 0.9f), 0.0f, PT_RING);
        return;
    }
}
