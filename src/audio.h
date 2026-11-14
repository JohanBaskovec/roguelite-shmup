#ifndef AUDIO_H
#define AUDIO_H

/*
 * Procedural sound effects. Every sound is synthesized at startup (sfxr-style
 * oscillators + noise + filters) and mixed by a small software mixer running
 * in the SDL audio callback. Game code only queues play requests; they are
 * handed to the mixer once per frame by audio_flush().
 */

#include "common.h"

typedef enum {
    /* player weapons */
    SFX_SHOT_BLASTER, SFX_SHOT_SCATTER, SFX_SHOT_NEEDLER, SFX_SHOT_SEEKER, SFX_SHOT_BOUNCER, SFX_DRONE_SHOT,
    /* enemies */
    SFX_ENEMY_SHOT, SFX_ENEMY_RING, SFX_ENEMY_SPIRAL, SFX_SNIPER_CHARGE, SFX_SNIPER_FIRE, SFX_TANK_FIRE,
    SFX_ENEMY_APPEAR, SFX_WAVE,
    /* impacts */
    SFX_HIT, SFX_BOUNCE, SFX_EXPLODE_SMALL, SFX_EXPLODE_BIG,
    /* player */
    SFX_PLAYER_HURT, SFX_PLAYER_DEATH, SFX_DASH, SFX_BOMB, SFX_WEAPON_SWITCH, SFX_DENIED,
    /* pickups */
    SFX_COIN, SFX_KEY, SFX_BOMB_PICKUP, SFX_HEART, SFX_ITEM,
    /* rooms & UI */
    SFX_ROOM_CLEAR, SFX_DOOR_OPEN, SFX_UNLOCK, SFX_SECRET, SFX_DOOR_ENTER, SFX_UI_START, SFX_PAUSE,
    SFX_SWORD, SFX_DEFLECT, SFX_SHIELD, SFX_POWER_UP, SFX_CHAOS,
    SFX_COUNT
} SfxId;

bool audio_init(void);
void audio_shutdown(void);
/* Hand queued requests to the mixer. Call once per frame. */
void audio_flush(void);
void audio_toggle_mute(void);
/* Dev tool: synthesize every sound and write it as a WAV file into dir. */
bool audio_dump_wavs(const char *dir);
bool audio_muted(void);

void sfx_play(SfxId id);
/* Panned from the horizontal position in the arena. */
void sfx_play_at(SfxId id, V2 pos);
void sfx_play_ex(SfxId id, float vol, float pitch, float pan);

#endif
