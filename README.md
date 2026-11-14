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
| Plein écran | F11 |
| Menu de debug | F3 |

## Boucle de jeu

- **Toutes les salles démarrent pleines d'ennemis** (y compris la *Launch Bay*, avec une vague légère). Ce qui rend une salle spéciale n'apparaît qu'une fois tous les ennemis morts.
- Une fois la salle nettoyée : 3 portes (gauche / haut / droite). Chaque porte affiche la récompense de la salle suivante. Une porte peut être verrouillée (coûte 1 clef) et contient toujours une arme ou une amélioration.
- **Salles d'arme et d'amélioration : toujours 3 objets au choix** (avec leur description), on en prend un et les deux autres disparaissent. S'il reste moins de 3 armes à obtenir, une salle d'arme complète avec des améliorations.
- Ressources : pièces, clefs, bombes (+ cœurs). **Les ennemis ne lâchent jamais rien** : les ressources sont uniquement des récompenses de salle, aspirées automatiquement une fois la salle nettoyée.
- **Salles Stats** (porte turquoise, ~20 % des portes après un combat) : une fois les ennemis tués, 4 échanges apparaissent, on en prend un (ou aucun). Chaque échange augmente une stat et en baisse une autre. Stats : dégâts, cadence, portée, PV max, vitesse de déplacement, recharge du dash, vitesse des projectiles, taille de la hitbox, chance (quantité des récompenses de salle). Valeurs et bornes dans `src/stats.c`.
- **Shops** (porte rose, à partir de la salle 2, jamais deux d'affilée) : une fois les ennemis tués, toujours 5 objets à vendre — au moins une arme, une amélioration et un échange de stats, plus 2 emplacements aléatoires. On achète en marchant dessus ; prix affiché en doré si on peut payer, en rouge sinon. Prix : `shop_price()` dans `src/pickups.c`.
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
| `stats.c` | Stats échangeables des salles Stats (gains, pertes, bornes) |
| `audio.c/h` | Synthèse procédurale des sons + mixeur (voix, panoramique, limitation de fréquence) |
| `hud.c` | HUD et écrans (titre, pause, game over) |
| `debug_menu.c` | Menu de debug F3 : téléportation dans chaque type de salle (avec / sans ennemis), cheats |

Les types de salles `ROOM_TREASURE` et `ROOM_BOSS` sont déjà prévus (sur le modèle de `ROOM_STATS` / `ROOM_SHOP`) dans `RoomType` (`game.h`) : il suffit de les générer dans `gen_doors()` (`room.c`) et de leur donner un comportement dans `room_enter()`.

## Tests (sans fenêtre)

```sh
ctest --preset release      # ~6 s
ctest --preset asan         # même suite sous AddressSanitizer + UBSan (cmake --preset asan && cmake --build --preset asan)
```

Aucun test n'ouvre de fenêtre :

- **Simulation headless** (`--headless`) : pas de vidéo ni d'audio, un autopilote (`bot.c`) joue aussi vite que le CPU le permet (~500x le temps réel). Des invariants sont vérifiés à chaque tick (positions finies et dans l'arène, PV cohérents, arme possédée, aucun objet au sol avant la fin d'une salle (pas de drop d'ennemi), shop à 5 objets, pas de salle bloquée — aucun ennemi touché pendant 60 s —, pas de salle nettoyée jamais quittée). En cas d'échec, `--fail-shot` enregistre l'image de l'instant fautif (rendue offscreen).
- **Rendu** : `--shot` dessine dans le pilote vidéo `offscreen` de SDL et sauve un BMP.
- **Audio** : `--dump-sfx` synthétise tous les sons en WAV.

Les sorties des tests sont dans `build/<preset>/test-output/`.

## Menu de debug (F3)

Fige le jeu et permet de se téléporter dans chaque type de salle (Launch Bay, combat, Stats, Shop), **avec ses ennemis** ou **sans ennemis** (salle déjà nettoyée, récompense déjà sortie). On choisit aussi la profondeur de la salle et la récompense des salles de combat (aléatoire ou imposée). Bonus : god mode, overlay FPS, +50 pièces. Navigation flèches / WASD + Entrée, ou à la souris ; F3 ou Échap pour fermer.

## Options de dev

```sh
neon_breach --headless --god --seed 1 --runs 20         # 20 parties invincibles, résumé par partie
neon_breach --headless --seed 100 --runs 20             # 20 parties normales (jusqu'à la mort)
neon_breach --headless --depth 15 --god --fail-shot f.bmp   # démarre en salle 15, image si échec
neon_breach --shot out.bmp --depth 4 --frames 600       # capture offscreen après 600 frames
neon_breach --dump-sfx /tmp/sfx                         # exporte chaque son en WAV
neon_breach --seed 42 --depth 8                         # jouer : run reproductible, démarrage en salle 8
neon_breach --stats                                     # démarrer directement dans une salle Stats
neon_breach --shop --coins 50                           # démarrer dans un shop avec 50 pièces
neon_breach --debug-menu                                # ouvrir le menu de debug au démarrage
neon_breach --headless --debug-rooms                    # teste les téléportations du menu de debug
neon_breach --bot --god                                 # regarder l'autopilote jouer (ouvre une fenêtre)
```

Options headless : `--ticks N` (durée max par partie, 120 ticks = 1 s, défaut 10 min), `--runs N`, `--expect-rooms N` (échec si moins de N salles nettoyées).
