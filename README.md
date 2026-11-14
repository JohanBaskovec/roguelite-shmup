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
| Objets activables (dash, bouclier...) : tous ceux qui sont prêts | Espace / Shift / clic droit |
| Bombe (efface toutes les balles) | E |
| Menu des améliorations (activer / désactiver) | Tab |
| Pause | Échap (R = restart, Q = quitter) |
| Couper le son | M |
| Changer de vaisseau (pause / game over) | C |
| Plein écran | F11 |
| Afficher / cacher les stats | F2 |
| Menu de debug | F3 |

## Personnages

Après l'écran titre, on choisit son vaisseau (A / D, 1-3 ou souris, puis clic / Entrée ; Échap revient au titre). Le choix est gardé pour les parties suivantes ; C (en pause ou au game over) ramène à l'écran de sélection. Table `CHARACTERS` dans `src/characters.c`.

| Vaisseau | Forme | PV | Particularités | Amélioration au départ |
|---|---|---|---|---|
| **Normal** | flèche | 3 | toutes les stats x1 | aucune |
| **Shotgunner** | rond | 3 | dégâts x2, hitbox plus grosse (7 px au lieu de 4) | Scatter |
| **Knight** | carré | 3 + 3 armures | lent (vitesse x0.8) ; l'armure = 3 cœurs temporaires (boucliers) | Sword (épée) |

**L'épée** (amélioration Sword ; le Knight l'a dès le départ, les autres personnages peuvent la trouver comme n'importe quelle amélioration) : elle remplace le canon. Chaque coup balaie un arc de cercle devant le vaisseau, alternativement dans un sens puis dans l'autre. Tout ce que la lame traverse est touché : les ennemis (une fois par coup, gros recul) et les **balles ennemies, détruites**. Les améliorations s'appliquent : dégâts, cadence (comptée en racine carrée, minimum 0,16 s entre deux coups), portée et taille des balles (longueur de la lame, plafonnée à 170 px), projectiles en plus (arc plus large), effets à l'impact (gel, poison, feu, éclair, explosion, fragments, guillotine), tirs supplémentaires (Tail Gun, Flank, Capacitor) ; les améliorations Flamer et Arc Caster donnent à la lame leur brûlure / leurs éclairs. Si l'épée est désactivée (Tab), le vaisseau tire normalement, avec ses autres améliorations.

## Boucle de jeu

- **Toutes les salles démarrent pleines d'ennemis**, sauf deux exceptions : la *Launch Bay* (première salle, portes ouvertes tout de suite) et les salles secrètes (sauf leur variante « embuscade »). Ce qui rend une salle spéciale n'apparaît qu'une fois tous les ennemis morts.
- Une fois la salle nettoyée : 3 portes (gauche / haut / droite). Chaque porte affiche la récompense de la salle suivante, et **les 3 portes sont toujours différentes** (jamais deux fois la même récompense ou le même type de salle). Une porte peut être verrouillée (coûte 1 clef) et mène toujours à une salle d'amélioration.
- **Salles d'amélioration : toujours 3 améliorations au choix** (avec leur description), on en prend une et les deux autres disparaissent. Il n'y a plus de salles d'arme : les améliorations qui transforment le tir sont proposées comme toutes les autres.
- **Menu des améliorations (Tab)**, en jeu ou depuis la pause : la liste des objets activables possédés (en premier) et de toutes les améliorations trouvées, chacun(e) **activable / désactivable une par une** (clic, Entrée ou Espace ; flèches / molette pour naviguer ; Tab ou Échap pour revenir). Le jeu est figé pendant ce temps. Une amélioration désactivée n'a plus aucun effet (stats comprises) jusqu'à ce qu'on la réactive ; réactiver un cœur ne soigne pas ; les échanges de stats restent acquis. Seule Skeleton Key (+3 clefs, effet déjà dépensé) n'est pas désactivable. Le HUD (en haut à droite) affiche une icône par amélioration active et le nombre d'améliorations désactivées.
- **Objets activables** : **on peut en avoir plusieurs à la fois**. Espace / Shift / clic droit déclenche **tous ceux qui sont activés et prêts** en même temps, puis chacun se recharge de son côté (temps réduit par la stat RECHARGE, Thrusters, Overdrive Core). **Chaque nouvel écran (salle suivante, ou retour d'une salle secrète) recharge tous les objets** : ils sont prêts dès l'entrée (un effet en cours continue). Le HUD (en haut à gauche) montre une case par objet : icône, niveau, barre de recharge (ou durée restante de l'effet), « OFF » s'il est désactivé ; un arc par objet en recharge entoure aussi le vaisseau. Comme les améliorations, chacun peut être **désactivé / réactivé dans le menu Tab** (un objet désactivé ne se déclenche plus et son effet en cours s'arrête). **On commence sans** : on les trouve dans les **salles Active** (récompense de porte « ACTIVE », éclair jaune), qui proposent 3 objets au choix une fois les ennemis tués ; c'est une porte comme une autre (pas de garantie). Un nouvel objet s'ajoute aux autres (« NEW ») ; **le même objet peut réapparaître et être repris : il monte de niveau** (jusqu'au niveau 5 ; « LEVEL UP -> N » au-dessus du piédestal) : effets plus longs, explosions plus grosses et plus fortes, recharge -12 % par niveau. Un objet au niveau max n'est plus proposé. Table `ACTIVES` et effets dans `src/actives.c` :
  - **Dash** : petit bond invulnérable, 0,13 s (+0,02 s par niveau) (recharge 0,6 s).
  - **Shield** : invincible 2,5 s (+0,5 s par niveau), la bulle (qui grossit avec le niveau) détruit les balles qui la touchent (recharge 10 s).
  - **Boom** : toutes tes balles en vol explosent d'un coup, et continuent leur route ; rayon +15 % et dégâts +25 % par niveau (recharge 6 s ; avec l'épée ou sans balles, c'est le vaisseau qui explose).
  - **Strength** : tirs quadruplés pendant 4 s (+1 s par niveau) (chaque salve tirée 4 fois en éventail ; dégâts x4 pour l'épée) (recharge 18 s).
  - **Chaos** : 20 améliorations aléatoires que l'on n'a pas encore (+2 par niveau), pendant 8 s (+1,5 s par niveau), retirées ensuite (seulement des améliorations à effet, pas d'économie ni de stats, pour un retrait exact) (recharge 40 s).
  - **Reversal** : toutes les balles ennemies à l'écran sont retournées en tirs du joueur, visant l'ennemi le plus proche ; dégâts +25 % par niveau (recharge 20 s).
  - **Turret** : pose une tourelle qui mitraille l'ennemi le plus proche pendant 8 s (+1,5 s par niveau, cadence +10 % par niveau) (recharge 18 s).
  - **Blink** : téléportation dans la direction visée (520 px, +60 par niveau, bornée à l'arène), avec une explosion au départ et à l'arrivée, 0,4 s d'invulnérabilité (recharge 3,5 s).
  - **Nova** : anneau de 24 tirs autour du vaisseau (+4 par niveau) (recharge 5 s).
  - **Time Stop** : fige les ennemis et leurs balles pendant 2,5 s (+0,5 s par niveau) ; les balles figées blessent toujours si on les touche (recharge 22 s).
  - **Blade Storm** : 3 lames (+1 tous les 2 niveaux) tournent autour du vaisseau pendant 5 s (+1 s par niveau), tranchent les ennemis et détruisent les balles (recharge 14 s).
  - Ram Plating marche pendant le dash ou le bouclier ; Nova Trigger et Ram Plating ne sont proposés qu'avec un objet qui les déclenche.
  - En combat, le HUD affiche le nom de l'objet, son niveau (pastilles) et sa barre de recharge.
- **Fin de salle** : dès que le dernier ennemi meurt, le vaisseau est replacé en bas de l'arène, devant la porte d'entrée, pour ne pas ramasser un objet par accident.
- **Stats du vaisseau** (F2, cachées par défaut) : panneau semi-transparent en haut à gauche de l'arène, avec dégâts, cadence, portée, vitesse des tirs, dispersion (spread), vitesse, recharge, hitbox, chance, projectiles, perçage, rebonds, drones. Valeurs effectives (bonus situationnels de Bloodlust / Adrenaline inclus), en vert au-dessus de la base et en rouge en dessous ; une stat qui vient de changer clignote.
- **Contact** : un ennemi qui percute le vaisseau lui fait perdre un demi-cœur, mais le choc le blesse aussi (30 dégâts, plus si la stat de dégâts dépasse x1, et un fort recul) : les petits ennemis qui foncent sur le vaisseau (Chaser, Swarm) meurent sur le coup. Un contact esquivé (invulnérabilité, dash, bouclier) ne blesse personne (`CONTACT_DAMAGE`, `src/enemies.c`).
- Ressources : pièces, clefs, bombes (+ cœurs). **Les ennemis ne lâchent jamais rien** : les ressources sont uniquement des récompenses de salle, aspirées automatiquement une fois la salle nettoyée.
- **Salles Stats** (porte turquoise, ~20 % des portes après un combat) : une fois les ennemis tués, 4 échanges apparaissent, on en prend un (ou aucun). Chaque échange augmente une stat et en baisse une autre (**jamais les PV max** : un échange peut donner +1 cœur, jamais en retirer ; seuls les augments du workshop coûtent des PV max) (il modifie les stats de base, avant les améliorations). Stats : dégâts, cadence, portée, PV max, vitesse de déplacement, **recharge** (de l'objet activable), vitesse des projectiles, taille de la hitbox, chance (quantité des récompenses de salle), **dispersion** (spread : -25 % = tirs plus groupés, +30 % = plus écartés, bornée entre x0.2 et x3). Échange spécial **+3 boucliers** : 3 cœurs bleus temporaires (absorbent les dégâts avant les vrais cœurs, ne se régénèrent pas, max 6) contre une baisse de stat. Valeurs et bornes dans `src/stats.c`.
- **Shops** (porte rose, à partir de la salle 2, jamais deux d'affilée) : une fois les ennemis tués, toujours 5 objets à vendre — au moins deux améliorations et un échange de stats, plus 2 emplacements aléatoires — avec leur description (bonus et malus), et un **comptoir** en bas à droite : 1 cœur (6 pièces, refusé si la santé est pleine), 1 bombe (5) et 1 clef (6). On achète en marchant dessus ; prix affiché en doré si on peut payer, en rouge sinon. Prix : `shop_price()` dans `src/pickups.c`.
- **Salles secrètes** (~15 % des salles de combat, Stats et shops) : pendant le combat, un coin de la salle flashe très brièvement (petit carré presque invisible). Une bombe dans ce coin ouvre la porte du bas, vers la salle secrète : pas d'ennemis, le butin attend déjà — 1 amélioration (rarement 2) + pièces, bombes ou clefs. Variantes : 10 % de chance d'un **shop secret** (5 objets à vendre à la place de l'objet gratuit) et 10 % de chance d'une **embuscade** (une vague d'ennemis avant que le butin n'apparaisse). La sortie ramène dans la salle d'origine.
- **Workshop** (porte vert-jaune, à partir de la salle 2, ~14 % des portes, jamais deux d'affilée) : démarre plein d'ennemis comme les autres ; une fois nettoyé, des machines apparaissent. On utilise une machine en marchant sur son cercle :
  - **Supply Press** (en bas à gauche) : 1/2 cœur de dégâts (bouclier d'abord) contre des pièces (45 %, 5-8), une bombe (30 %) ou une clef (25 %). 3 à 5 utilisations, refusée s'il ne reste qu'un demi-cœur.
  - **Augments** (au centre) : bonus **exclusifs** au workshop, très puissants (stats + effet), payés en **PV max** : 1 cœur (Blood Rounds, Overdrive Core), 2 cœurs (Hellfire Engine, Storm Core) ou 3 cœurs (Demon Heart, God Cannon). Un emplacement par palier abordable (il faut garder au moins 1 cœur max), on en prend un seul.
  - **Slot Machine** (en bas à droite) : affiche le nom et l'effet du lot (une amélioration) ; chaque mise coûte 5 pièces, `30 % + 3 % x chance` de gagner (entre 15 et 50 %). Le lot gagné tombe devant la machine.
- **Pas d'armes, seulement des améliorations** : le vaisseau tire toujours de la même façon au départ (tirs rapides, un projectile par salve). Les anciennes armes sont devenues des **améliorations de tir** (13, proposées comme les autres) qui transforment le tir et **se combinent toutes entre elles** (`gun_profile()` dans `src/weapons.c` : chacune multiplie ou ajoute à la cadence, aux dégâts, au nombre de projectiles, à la vitesse, à la portée...). Aucune notion d'arme dans le jeu : ni salle d'arme, ni changement d'arme, ni arme affichée.
- **Chaque amélioration ne se prend qu'une fois** : une amélioration possédée (même désactivée) n'est plus jamais proposée, nulle part (salles, boutiques, salles secrètes, machine à sous). Si on la prend ailleurs alors qu'elle est encore proposée quelque part (ex. de retour d'une salle secrète), l'offre est remplacée par une autre. Quand toutes les améliorations sont possédées, les salles proposent des échanges de stats à la place. Aucune amélioration n'est réservée à un personnage.
  - **Scatter** : projectiles x8 en éventail large, portée courte, cadence plus lente (recul).
  - **Needler** : cadence x2, dégâts /2, +2 perçage.
  - **Seeker** : missiles à tête chercheuse (+1 tir, lents puis accélèrent, plus forts).
  - **Bouncer** : grosses sphères, +4 rebonds, +1 perçage.
  - **Railgun** : dégâts x9, traverse tout, cadence très lente.
  - **Flamer** : flammes à courte portée qui ralentissent, grossissent, percent et enflamment.
  - **Rockets** : roquettes qui accélèrent et explosent (rayon selon la taille des tirs).
  - **Chakram** : lames qui traversent tout et reviennent vers le vaisseau.
  - **Cluster** : obus qui éclatent en 6 fragments.
  - **Arc Caster** : chaque touche lance un éclair en chaîne (3 rebonds).
  - **Vulcan** : la cadence monte jusqu'à x4 en 1 s de tir continu.
  - **Wave Gun** : deux tirs en sinusoïdes miroir, +1 perçage.
  - **Sword** : l'épée (voir Personnages), remplace le canon ; disponible pour tous les personnages.
  - Combinaisons bornées pour rester jouables : entre 0,03 et 0,7 s entre deux salves (les dégâts compensent), 40 projectiles par salve au plus (plus forts au-delà), une portée minimale (320 px, le double pour les tirs qui reviennent). L'apparence, la couleur et le son des tirs viennent de l'amélioration la plus marquante (Railgun > Rockets > Chakram > Flamer > Cluster > Arc > Wave > ...). Une amélioration qui ne va pas avec les autres se désactive dans le menu Tab.
- **Chaque amélioration a un bonus ET un malus** (en rouge sous le bonus, sur les piédestaux, en boutique, sur la machine à sous, dans le menu Tab et dans la bannière) : c'est ce qui rend utile le choix de celles qu'on active. Désactiver une amélioration retire les deux. Les malus (table `MALUSES` dans `src/maluses.c`) : baisse d'une stat (dégâts, cadence, portée, vitesse, vitesse des tirs, hitbox plus grosse, dispersion, recharge des objets activables, chance), recul à chaque salve, vaisseau plus lent en tirant, balles ennemies plus rapides, invulnérabilité plus courte après un coup, prix des shops plus élevés, pièces perdues à chaque salle nettoyée ou à chaque coup reçu, surchauffe (le canon s'enraye après 3 s de tir continu), délai avant le premier tir, premier coup de chaque salle plus douloureux, vagues plus grosses (plus d'ennemis, jamais plus forts), vaisseau plus lourd (inertie), pas de tir au début de chaque salle, effets des objets activables plus courts, moins de cœurs dans les récompenses. Pour les améliorations de tir (Scatter, Railgun...), le malus fait partie de l'effet (cadence, portée). Chaos prête ses améliorations sans leur malus. Skeleton Key n'étant pas désactivable, son malus (+10 % aux prix) reste.
- **83 améliorations passives** (+ 13 améliorations de tir et 6 exclusives du workshop) (table `UPGRADES` et effets dans `src/upgrades.c`) :
  - *Tir* : Overclock, Heavy Rounds, Split Shot, Piercer, Rubber Core, Smart Rounds, Accelerator, Big Iron, Wide Lens, Focus Lens, Long Barrel, Hair Trigger, Magnum Rounds (+2 perçage, +15 % dégâts, -15 % cadence), Spray and Pray (+45 % cadence, +60 % dispersion), Heavy Caliber (+40 % dégâts, gros tirs, -20 % vitesse des tirs), Smart Bounce (+1 rebond, chaque rebond vise l'ennemi le plus proche).
  - *Dégâts conditionnels* : Sniper Scope (jusqu'à +60 % selon la distance parcourue), Point Blank (+50 % de près), Bully (+40 % sous la moitié des PV), Giant Slayer (+60 % contre les gros ennemis), Momentum (+1 % par touche, max +40 %, perdu après 1 s sans toucher), Growth (+3 % par salle nettoyée, max +60 %), Turret Mode (+40 % cadence immobile), Run and Gun (+20 % dégâts en mouvement), Rampage (+3 % cadence par kill dans la salle, max +45 %), Overkill (les dégâts en trop passent à l'ennemi le plus proche).
  - *Effets sur les balles* : Cryo Rounds (ralentit), Venom Tips (poison), Thermite (brûlure), Arc Emitter (éclair en chaîne), Payload (explosion), Shrapnel (fragments), Lucky Strike (coups x3), Guillotine (exécute sous 15 % PV), Return Vector (boomerang), Phase Rounds (traverse les obstacles), Impact Driver (recul), Frostbite (gèle parfois, +35 % dégâts sur les ennemis gelés), Pyromaniac (enflamme parfois, les ennemis en feu explosent en mourant), Toxic Cloud (empoisonne parfois, le poison se propage à la mort), Chain Reaction (6 tirs à chaque kill), Black Hole (un kill ouvre parfois un vortex qui aspire les ennemis).
  - *Tirs supplémentaires* : Tail Gun (arrière), Flank Cannons (côtés), Capacitor (méga tir toutes les 10 salves), Wing Drone, Nova Trigger (anneau de tirs à chaque utilisation de l'objet activable), Mine Layer, Missile Pod (missile à tête chercheuse toutes les 1,5 s), Orbital Strike (frappe orbitale sur un ennemi toutes les 3 s), Echo (chaque salve répétée 0,12 s plus tard à 50 %).
  - *Stats* : Thrusters, Hull Plating, Glass Cannon, Titan Hull, Featherweight, Four-Leaf Chip, Micro Hull, Magnet.
  - *Économie* : Piggy Bank, Bomb Belt, Skeleton Key, Coupon (shop -30 %).
  - *Défense / déclencheurs* : Reactive Armor, Second Wind (survit à un coup mortel), Barrier Cell (bouclier rechargé à chaque salle), Guardian Orb, Tesla Field, Leech Core, Adrenaline, Bloodlust, Ram Plating, Chrono Field, Volatile Targets, Emergency Shield (le premier coup de chaque salle est bloqué), Spiked Hull (gros dégâts aux ennemis qui touchent la coque).
  - *Blessé* (actives quand il manque au moins un demi-cœur rouge) : Wounded Fury (+30 % dégâts), Scar Tissue (+25 % cadence), Last Stand (+10 % dégâts et +5 % vitesse par cœur manquant), Blood Scent (tirs à tête chercheuse).
  - *Spéciales* : Deflector Shells (tirs énormes et très lents, cadence / 3, mais ils renvoient les balles ennemies touchées comme des tirs du joueur), Loaded Dice (double la probabilité de déclenchement de tous les effets), X-Ray Visor (les coins secrets sont toujours visibles).
  - Les descriptions des effets aléatoires affichent la **probabilité exacte** (en tenant compte du nombre d'exemplaires et de Loaded Dice), calculée par `upgrade_desc()`. Un `|` dans une description la coupe sur deux lignes.
- **Les ennemis gardent toujours les mêmes attributs** (PV, vitesse, cadence, patterns) quelle que soit la profondeur. La difficulté vient du nombre : budget de vague `5 + 1.8 x profondeur^1.2 + 1.5 x vague` (plafond `30 + 2.5 x profondeur` ennemis par vague), `2 + min(profondeur / 3, 5)` vagues par salle, et de nouveaux ennemis plus durs débloqués au fil des salles : burster (salle 3), spinner (4), sniper (5), tank (7), **warden** (9 : croix tournante de 4 flux + éventail visé), **hunter** (11 : traque, s'annonce par une ligne, fonce puis tire un anneau), **hive** (13 : pond des essaims et tire des anneaux).

## Architecture (`src/`)

| Fichier | Rôle |
|---|---|
| `main.c` | Init SDL, boucle à pas fixe (120 Hz), entrées, options de dev |
| `game.c/h` | État global, machine d'états, ordre update/render, toutes les structures |
| `draw.c/h` | Renderer de formes batché (`SDL_RenderGeometry`) + texte |
| `room.c` | Génération des salles, obstacles, vagues, portes, collisions |
| `player.c` | Vaisseau, déplacement, bombe, dégâts |
| `weapons.c` | Le tir façonné par les améliorations de tir (`gun_profile`), l'épée, leurs effets (`weapon_on_hit`, `weapon_bullet_death`), drones |
| `upgrades.c` | Table des 102 améliorations (dont 13 améliorations de tir et 6 exclusives du workshop), leurs effets (hooks appelés par les balles, ennemis, joueur, salles), stats recalculées depuis la base + les améliorations actives (`stats_compute`, `player_recompute`), activation / désactivation |
| `upgrade_menu.c` | Menu Tab : améliorations trouvées, activer / désactiver |
| `actives.c` | Objets activables (11), niveaux, tourelle et lames, icônes, choix des salles Active |
| `workshop.c` | Machines du workshop (Supply Press, Slot Machine) et emplacements des augments |
| `enemies.c` | 10 types d'ennemis et leurs patterns de balles |
| `bullets.c` | Balles joueur / ennemies, collisions |
| `pickups.c` | Pièces, clefs, bombes, cœurs, objets sur piédestal, icônes |
| `fx.c` | Particules, screen shake, popups, bannières |
| `bot.c` | Autopilote (visée, esquive, navigation, choix de porte) pour les tests |
| `headless.c` | Simulation sans fenêtre + vérification d'invariants |
| `stats.c` | Stats échangeables des salles Stats (gains, pertes, bornes) |
| `audio.c/h` | Synthèse procédurale des sons + mixeur (voix, panoramique, limitation de fréquence) |
| `hud.c` | HUD, overlay des stats (F2), écrans (titre, pause, game over) |
| `characters.c` | Personnages (stats et amélioration de départ) et écran de sélection |
| `debug_menu.c` | Menu de debug F3 : téléportation dans chaque type de salle (avec / sans ennemis), cheats |

Les types de salles `ROOM_TREASURE` et `ROOM_BOSS` sont déjà prévus (sur le modèle de `ROOM_STATS` / `ROOM_SHOP`) dans `RoomType` (`game.h`) : il suffit de les générer dans `gen_doors()` (`room.c`) et de leur donner un comportement dans `room_enter()`.

## Tests (sans fenêtre)

```sh
ctest --preset release      # ~50 s
ctest --preset asan         # même suite sous AddressSanitizer + UBSan (cmake --preset asan && cmake --build --preset asan)
```

Aucun test n'ouvre de fenêtre :

- **Simulation headless** (`--headless`) : pas de vidéo ni d'audio, un autopilote (`bot.c`) joue aussi vite que le CPU le permet (~500x le temps réel). Des invariants sont vérifiés à chaque tick (positions finies et dans l'arène, PV cohérents, stats toujours égales à la base + les améliorations actives, amélioration désactivée sans aucun effet, amélioration jamais possédée deux fois ni proposée une fois possédée, aucun objet au sol avant la fin d'une salle (pas de drop d'ennemi), shop à 5 objets, portes toujours uniques, objets activables (aucun au départ, 3 objets distincts, ceux qu'on possède seulement s'ils peuvent encore monter de niveau, objet désactivé sans effet, Chaos ne prête que des améliorations non possédées et les retire toutes), machines et augments du workshop, ennemis toujours aux PV de leur type, vaisseau replacé en fin de salle, 3 choix dans les salles d'amélioration, butin et retour des salles secrètes, pas de salle bloquée — aucun ennemi touché pendant 60 s —, pas de salle nettoyée jamais quittée). Comme un joueur dans le menu Tab, le bot désactive une de ses améliorations de tir quand ses tirs n'ont rien touché depuis 10 s (combinaison inadaptée). En cas d'échec, `--fail-shot` enregistre l'image de l'instant fautif (rendue offscreen).
- **Rendu** : `--shot` dessine dans le pilote vidéo `offscreen` de SDL et sauve un BMP.
- **Audio** : `--dump-sfx` synthétise tous les sons en WAV.

Les sorties des tests sont dans `build/<preset>/test-output/`.

## Menu de debug (F3)

Fige le jeu et permet de se téléporter dans chaque type de salle (Launch Bay, combat, Stats, Shop, Workshop, salle secrète), **avec ses ennemis** ou **sans ennemis** (salle déjà nettoyée, récompense déjà sortie) ; la Launch Bay et la salle secrète n'ont que « sans ennemis ». On choisit aussi la profondeur de la salle et la récompense des salles de combat (aléatoire ou imposée). Pour la salle secrète, on arrive depuis une salle de combat nettoyée, où l'on revient en sortant. Bonus : god mode, overlay FPS, « secret dans chaque salle », « afficher le coin secret » (carré, libellé et rayon de la bombe ; le coin est aussi indiqué en bas du menu), +50 pièces, +3 bombes, « give upgrade » (choisir n'importe quelle amélioration), « give active » (n'importe quel objet activable). La récompense « ACTIVE » est dans la liste des récompenses de combat. Pour la salle secrète, on choisit aussi son contenu (aléatoire / objet / shop) ; « avec ennemis » donne l'embuscade. Navigation flèches / WASD + Entrée, ou à la souris ; F3 ou Échap pour fermer.

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
neon_breach --workshop --coins 30                       # démarrer dans un workshop
neon_breach --shot w.bmp --workshop --cleared --no-bot  # capture du workshop déjà nettoyé, sans autopilote
neon_breach --active shield                             # démarrer avec un objet activable, option répétable (dash / shield / boom / strength / chaos /
                                                        #   reversal / turret / blink / nova / timestop / blades)
neon_breach --weapon rockets                            # démarrer avec une amélioration de tir (scatter / needler / seeker / bouncer / railgun /
                                                        #   flamer / rockets / chakram / cluster / arc / vulcan / wave / sword)
neon_breach --shot u.bmp --all-upgrades --upgrades-menu # capture du menu Tab
neon_breach --headless --toggle-upgrades --runs 10      # le bot désactive / réactive une amélioration au hasard toutes les 4 s
neon_breach --active-room --cleared --no-bot            # salle Active nettoyée (3 objets au choix)
neon_breach --secret                                    # chaque salle cache une salle secrète
neon_breach --show-secret                               # affiche le coin secret (comme dans le menu F3)
neon_breach --show-stats                                # démarre avec l'overlay des stats (F2) visible
neon_breach --char knight --depth 3                     # joue un personnage (normal / shotgunner / knight)
neon_breach --headless --char knight --runs 20          # simulations avec un personnage
neon_breach --shot sel.bmp --char-select                # capture de l'écran de sélection
neon_breach --headless --debug-rooms                    # teste les téléportations du menu de debug
neon_breach --headless --god --all-upgrades             # démarre avec toutes les améliorations
neon_breach --hp 1                                      # démarre avec 1 PV (tester boucliers / Second Wind)
neon_breach --bot --god                                 # regarder l'autopilote jouer (ouvre une fenêtre)
```

Options headless : `--ticks N` (durée max par partie, 120 ticks = 1 s, défaut 10 min), `--runs N`, `--expect-rooms N` (échec si moins de N salles nettoyées). Le résumé de chaque partie indique le personnage et le nombre de coups encaissés (`hits=`).
