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
| Afficher / cacher les stats | F2 |
| Menu de debug | F3 |

## Boucle de jeu

- **Toutes les salles démarrent pleines d'ennemis**, sauf deux exceptions : la *Launch Bay* (première salle, portes ouvertes tout de suite) et les salles secrètes (sauf leur variante « embuscade »). Ce qui rend une salle spéciale n'apparaît qu'une fois tous les ennemis morts.
- Une fois la salle nettoyée : 3 portes (gauche / haut / droite). Chaque porte affiche la récompense de la salle suivante. Une porte peut être verrouillée (coûte 1 clef) et contient toujours une arme ou une amélioration.
- **Salles d'arme et d'amélioration : toujours 3 objets au choix** (avec leur description), on en prend un et les deux autres disparaissent. S'il reste moins de 3 armes à obtenir, une salle d'arme complète avec des améliorations.
- **Stats du vaisseau** (F2, cachées par défaut) : panneau semi-transparent en haut à gauche de l'arène, avec dégâts, cadence, portée, vitesse des tirs, vitesse, dash, hitbox, chance, projectiles, perçage, rebonds, drones. Valeurs effectives (bonus situationnels de Bloodlust / Adrenaline inclus), en vert au-dessus de la base et en rouge en dessous ; une stat qui vient de changer clignote.
- Ressources : pièces, clefs, bombes (+ cœurs). **Les ennemis ne lâchent jamais rien** : les ressources sont uniquement des récompenses de salle, aspirées automatiquement une fois la salle nettoyée.
- **Salles Stats** (porte turquoise, ~20 % des portes après un combat) : une fois les ennemis tués, 4 échanges apparaissent, on en prend un (ou aucun). Chaque échange augmente une stat et en baisse une autre. Stats : dégâts, cadence, portée, PV max, vitesse de déplacement, recharge du dash, vitesse des projectiles, taille de la hitbox, chance (quantité des récompenses de salle). Échange spécial **+3 boucliers** : 3 cœurs bleus temporaires (absorbent les dégâts avant les vrais cœurs, ne se régénèrent pas, max 6) contre une baisse de stat. Valeurs et bornes dans `src/stats.c`.
- **Shops** (porte rose, à partir de la salle 2, jamais deux d'affilée) : une fois les ennemis tués, toujours 5 objets à vendre — au moins une arme, une amélioration et un échange de stats, plus 2 emplacements aléatoires. On achète en marchant dessus ; prix affiché en doré si on peut payer, en rouge sinon. Prix : `shop_price()` dans `src/pickups.c`.
- **Salles secrètes** (~15 % des salles de combat, Stats et shops) : pendant le combat, un coin de la salle flashe très brièvement (petit carré presque invisible). Une bombe dans ce coin ouvre la porte du bas, vers la salle secrète : pas d'ennemis, le butin attend déjà — 1 arme ou amélioration (rarement 2) + pièces, bombes ou clefs. Variantes : 10 % de chance d'un **shop secret** (5 objets à vendre à la place de l'objet gratuit) et 10 % de chance d'une **embuscade** (une vague d'ennemis avant que le butin n'apparaisse). La sortie ramène dans la salle d'origine.
- 5 armes (Blaster, Scatter, Needler, Seeker, Bouncer) avec 5 niveaux chacune.
- **52 améliorations passives** qui s'appliquent à toutes les armes (table `UPGRADES` et effets dans `src/upgrades.c`) :
  - *Tir* : Overclock, Heavy Rounds, Split Shot, Piercer, Rubber Core, Smart Rounds, Accelerator, Big Iron, Wide Lens, Focus Lens, Long Barrel, Hair Trigger.
  - *Effets sur les balles* : Cryo Rounds (ralentit), Venom Tips (poison), Thermite (brûlure), Arc Emitter (éclair en chaîne), Payload (explosion), Shrapnel (fragments), Lucky Strike (coups x3), Guillotine (exécute sous 15 % PV), Return Vector (boomerang), Phase Rounds (traverse les obstacles), Impact Driver (recul).
  - *Tirs supplémentaires* : Tail Gun (arrière), Flank Cannons (côtés), Capacitor (méga tir toutes les 10 salves), Wing Drone, Dash Nova, Mine Layer.
  - *Stats* : Thrusters, Hull Plating, Glass Cannon, Titan Hull, Featherweight, Four-Leaf Chip, Micro Hull, Magnet.
  - *Économie* : Piggy Bank, Bomb Belt, Skeleton Key, Coupon (shop -30 %).
  - *Défense / déclencheurs* : Reactive Armor, Second Wind (survit à un coup mortel), Barrier Cell (bouclier rechargé à chaque salle), Guardian Orb, Tesla Field, Leech Core, Adrenaline, Bloodlust, Ram Plating, Chrono Field, Volatile Targets.
- Difficulté progressive : PV des ennemis +9 % par salle, vitesse des balles ennemies plafonnée à +35 %, budget des vagues `5 + 1.7 x profondeur`, ennemis avancés débloqués progressivement (burster salle 3, spinner 4, sniper 5, tank 7).

## Architecture (`src/`)

| Fichier | Rôle |
|---|---|
| `main.c` | Init SDL, boucle à pas fixe (120 Hz), entrées, options de dev |
| `game.c/h` | État global, machine d'états, ordre update/render, toutes les structures |
| `draw.c/h` | Renderer de formes batché (`SDL_RenderGeometry`) + texte |
| `room.c` | Génération des salles, obstacles, vagues, portes, collisions |
| `player.c` | Vaisseau, dash, bombe, dégâts |
| `weapons.c` | Définitions des armes, tir, drones |
| `upgrades.c` | Table des 52 améliorations et leurs effets (hooks appelés par les balles, ennemis, joueur, salles) |
| `enemies.c` | 7 types d'ennemis et leurs patterns de balles |
| `bullets.c` | Balles joueur / ennemies, collisions |
| `pickups.c` | Pièces, clefs, bombes, cœurs, objets sur piédestal, icônes |
| `fx.c` | Particules, screen shake, popups, bannières |
| `bot.c` | Autopilote (visée, esquive, navigation, choix de porte) pour les tests |
| `headless.c` | Simulation sans fenêtre + vérification d'invariants |
| `stats.c` | Stats échangeables des salles Stats (gains, pertes, bornes) |
| `audio.c/h` | Synthèse procédurale des sons + mixeur (voix, panoramique, limitation de fréquence) |
| `hud.c` | HUD, overlay des stats (F2), écrans (titre, pause, game over) |
| `debug_menu.c` | Menu de debug F3 : téléportation dans chaque type de salle (avec / sans ennemis), cheats |

Les types de salles `ROOM_TREASURE` et `ROOM_BOSS` sont déjà prévus (sur le modèle de `ROOM_STATS` / `ROOM_SHOP`) dans `RoomType` (`game.h`) : il suffit de les générer dans `gen_doors()` (`room.c`) et de leur donner un comportement dans `room_enter()`.

## Tests (sans fenêtre)

```sh
ctest --preset release      # ~6 s
ctest --preset asan         # même suite sous AddressSanitizer + UBSan (cmake --preset asan && cmake --build --preset asan)
```

Aucun test n'ouvre de fenêtre :

- **Simulation headless** (`--headless`) : pas de vidéo ni d'audio, un autopilote (`bot.c`) joue aussi vite que le CPU le permet (~500x le temps réel). Des invariants sont vérifiés à chaque tick (positions finies et dans l'arène, PV cohérents, arme possédée, aucun objet au sol avant la fin d'une salle (pas de drop d'ennemi), shop à 5 objets, 3 choix dans les salles d'arme / d'amélioration, butin et retour des salles secrètes, pas de salle bloquée — aucun ennemi touché pendant 60 s —, pas de salle nettoyée jamais quittée). En cas d'échec, `--fail-shot` enregistre l'image de l'instant fautif (rendue offscreen).
- **Rendu** : `--shot` dessine dans le pilote vidéo `offscreen` de SDL et sauve un BMP.
- **Audio** : `--dump-sfx` synthétise tous les sons en WAV.

Les sorties des tests sont dans `build/<preset>/test-output/`.

## Menu de debug (F3)

Fige le jeu et permet de se téléporter dans chaque type de salle (Launch Bay, combat, Stats, Shop, salle secrète), **avec ses ennemis** ou **sans ennemis** (salle déjà nettoyée, récompense déjà sortie) ; la Launch Bay et la salle secrète n'ont que « sans ennemis ». On choisit aussi la profondeur de la salle et la récompense des salles de combat (aléatoire ou imposée). Pour la salle secrète, on arrive depuis une salle de combat nettoyée, où l'on revient en sortant. Bonus : god mode, overlay FPS, « secret dans chaque salle », « afficher le coin secret » (carré, libellé et rayon de la bombe ; le coin est aussi indiqué en bas du menu), +50 pièces, +3 bombes, « give upgrade » (choisir n'importe quelle amélioration). Pour la salle secrète, on choisit aussi son contenu (aléatoire / objet / shop) ; « avec ennemis » donne l'embuscade. Navigation flèches / WASD + Entrée, ou à la souris ; F3 ou Échap pour fermer.

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
neon_breach --secret                                    # chaque salle cache une salle secrète
neon_breach --show-secret                               # affiche le coin secret (comme dans le menu F3)
neon_breach --show-stats                                # démarre avec l'overlay des stats (F2) visible
neon_breach --headless --debug-rooms                    # teste les téléportations du menu de debug
neon_breach --headless --god --all-upgrades             # démarre avec toutes les améliorations au max
neon_breach --hp 1                                      # démarre avec 1 PV (tester boucliers / Second Wind)
neon_breach --bot --god                                 # regarder l'autopilote jouer (ouvre une fenêtre)
```

Options headless : `--ticks N` (durée max par partie, 120 ticks = 1 s, défaut 10 min), `--runs N`, `--expect-rooms N` (échec si moins de N salles nettoyées).
