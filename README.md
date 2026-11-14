# Neon Breach

Roguelite shmup ultra nerveux en vue de dessus — C17, SDL3, CMake. Tous les graphismes sont générés en code (triangles, polygones, cercles, glow additif), et tous les sons sont synthétisés au démarrage (oscillateurs + bruit + filtres, façon sfxr).

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
| Couper le son | M |
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
| `bot.c` | Autopilote (visée, esquive, navigation, choix de porte) pour les tests |
| `headless.c` | Simulation sans fenêtre + vérification d'invariants |
| `audio.c/h` | Synthèse procédurale des sons + mixeur (voix, panoramique, limitation de fréquence) |
| `hud.c` | HUD et écrans (titre, pause, game over) |

Les types de salles `ROOM_TREASURE`, `ROOM_SHOP` et `ROOM_BOSS` sont déjà prévus dans `RoomType` (`game.h`) : il suffit de les générer dans `gen_doors()` (`room.c`) et de leur donner un comportement dans `room_enter()`.

## Tests (sans fenêtre)

```sh
ctest --preset release      # ~4 s
ctest --preset asan         # même suite sous AddressSanitizer + UBSan (cmake --preset asan && cmake --build --preset asan)
```

Aucun test n'ouvre de fenêtre :

- **Simulation headless** (`--headless`) : pas de vidéo ni d'audio, un autopilote (`bot.c`) joue aussi vite que le CPU le permet (~500x le temps réel). Des invariants sont vérifiés à chaque tick (positions finies et dans l'arène, PV cohérents, arme possédée, pas de salle bloquée — aucun ennemi touché pendant 60 s —, pas de salle nettoyée jamais quittée). En cas d'échec, `--fail-shot` enregistre l'image de l'instant fautif (rendue offscreen).
- **Rendu** : `--shot` dessine dans le pilote vidéo `offscreen` de SDL et sauve un BMP.
- **Audio** : `--dump-sfx` synthétise tous les sons en WAV.

Les sorties des tests sont dans `build/<preset>/test-output/`.

## Options de dev

```sh
neon_breach --headless --god --seed 1 --runs 20         # 20 parties invincibles, résumé par partie
neon_breach --headless --seed 100 --runs 20             # 20 parties normales (jusqu'à la mort)
neon_breach --headless --depth 15 --god --fail-shot f.bmp   # démarre en salle 15, image si échec
neon_breach --shot out.bmp --depth 4 --frames 600       # capture offscreen après 600 frames
neon_breach --dump-sfx /tmp/sfx                         # exporte chaque son en WAV
neon_breach --seed 42 --depth 8                         # jouer : run reproductible, démarrage en salle 8
neon_breach --bot --god                                 # regarder l'autopilote jouer (ouvre une fenêtre)
```

Options headless : `--ticks N` (durée max par partie, 120 ticks = 1 s, défaut 10 min), `--runs N`, `--expect-rooms N` (échec si moins de N salles nettoyées).
