#ifndef BOT_H
#define BOT_H

/*
 * Autopilot used by automated tests (headless simulation, screenshots).
 * Writes into G.in like a player would: aims at the nearest enemy, fires
 * constantly, dodges nearby bullets, dashes / bombs in emergencies, grabs the
 * room reward and walks through the first door it can open.
 */
void bot_input(void);

#endif
