# Neon Breach

Roguelite shmup ultra nerveux en vue de dessus — C17, SDL3, CMake. Tous les graphismes sont générés en code (triangles, polygones, cercles, glow additif).

## Build

```sh
cmake --preset release      # SDL3 est récupéré via FetchContent s'il n'est pas installé
cmake --build build/release -j
./build/release/neon_breach
```

Preset `debug` disponible aussi (`build/debug`).

## Contrôles

| Action | Touche |
|---|---|
| Déplacement | WASD / ZQSD (positionnel, marche en AZERTY) / flèches |
| Viser & tirer | Souris (clic gauche maintenu) |
| Dash (invulnérable) | Espace / Shift / clic droit |
| Bombe (efface toutes les balles) | E |
| Changer d'arme | Molette / 1-5 |
| Pause | Échap (R = restart, Q = quitter) |
| Plein écran / debug FPS | F11 / F3 |

## Boucle de jeu

- On démarre dans la *Launch Bay*, puis chaque salle est une arène pleine d'ennemis (2 à 5 vagues).
- Une fois la salle nettoyée : 3 portes (gauche / haut / droite). Chaque porte affiche la récompense de la salle suivante. Une porte peut être verrouillée (coûte 1 clef) et contient toujours une arme ou une amélioration.
- Ressources : pièces, clefs, bombes. Les ressources au sol sont aspirées automatiquement à la fin de la salle.
- 5 armes (Blaster, Scatter, Needler, Seeker, Bouncer) avec 5 niveaux chacune, 11 améliorations passives qui s'appliquent à toutes les armes (tir multiple, perçant, rebond, homing, drones...).

## Architecture (`src/`)

| Fichier | Rôle |
|---|---|
| `main.c` | Init SDL, boucle à pas fixe (120 Hz), entrées, options de dev |
| `game.c/h` | État global, machine d'états, ordre update/render, toutes les structures |
| `draw.c/h` | Renderer de formes batché (`SDL_RenderGeometry`) + texte |
| `room.c` | Génération des salles, obstacles, vagues, portes, collisions |
| `player.c` | Vaisseau, dash, bombe, dégâts |
| `weapons.c` | Définitions des armes / améliorations, tir, drones |
| `enemies.c` | 7 types d'ennemis et leurs patterns de balles |
| `bullets.c` | Balles joueur / ennemies, collisions |
| `pickups.c` | Pièces, clefs, bombes, cœurs, objets sur piédestal, icônes |
| `fx.c` | Particules, screen shake, popups, bannières |
| `hud.c` | HUD et écrans (titre, pause, game over) |

Les types de salles `ROOM_TREASURE`, `ROOM_SHOP` et `ROOM_BOSS` sont déjà prévus dans `RoomType` (`game.h`) : il suffit de les générer dans `gen_doors()` (`room.c`) et de leur donner un comportement dans `room_enter()`.

## Options de dev

```sh
neon_breach --seed 42             # run reproductible
neon_breach --depth 8             # démarre directement en salle 8
neon_breach --bot --god           # autopilote invincible
neon_breach --bot --novsync --frames 5000   # bench (log FPS)
SDL_VIDEO_DRIVER=offscreen neon_breach --depth 4 --frames 600 --shot out.bmp   # capture headless
```
