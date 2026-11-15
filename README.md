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
| Objets activables (dash, bouclier...) : tous ceux qui sont prêts | Shift / clic droit |
| Bombe (efface toutes les balles) | Espace |
| Prendre / acheter un objet, utiliser une machine (celui qui est encadré, marqué « E ») | E |
| Menu des améliorations (activer / désactiver) | Tab |
| Pause | Échap (R = restart, Q = quitter) |
| Couper le son | M |
| Changer de vaisseau (pause / game over) | C |
| Plein écran | F11 |

Pendant une partie, le curseur est capturé dans la fenêtre (relâché en pause, au menu principal et à l'écran de game over).
| Afficher / cacher les stats | F2 |
| Menu de debug | F3 |

## Personnages

**Menu principal** (`src/title.c`) : le nom du jeu en haut et quatre boutons, **Play**, **Options**, **Unlocks** (grisés pour l'instant) et **Quit** (flèches / molette / souris, Entrée ou clic ; Échap quitte). En fond, **une vraie partie jouée par le bot** : un vaisseau avec quelques améliorations, quelques salles plus loin, qui enchaîne les salles et recommence à sa mort. Cette démo est muette, n'affiche pas le HUD et tire ses nombres aléatoires de son propre générateur : elle ne change rien à la vraie partie. Elle continue derrière l'écran de sélection.

Play mène au choix du vaisseau (A / D, 1-3 ou souris, puis clic / Entrée ; Échap revient au menu). Le choix est gardé pour les parties suivantes ; C (en pause ou au game over) ramène à l'écran de sélection. Table `CHARACTERS` dans `src/characters.c`.

| Vaisseau | Forme | PV | Particularités | Amélioration au départ |
|---|---|---|---|---|
| **Normal** | flèche | 3 | toutes les stats x1 | aucune |
| **Shotgunner** | rond | 3 | dégâts x2, hitbox plus grosse (7 px au lieu de 4) | Scatter |
| **Knight** | carré | 3 + 3 armures | lent (vitesse x0.8) ; l'armure = 3 cœurs temporaires (boucliers) | Sword (épée) |

**L'épée** (amélioration Sword ; le Knight l'a dès le départ, les autres personnages peuvent la trouver comme n'importe quelle amélioration) : elle remplace le canon. Chaque coup balaie un arc de cercle devant le vaisseau, alternativement dans un sens puis dans l'autre. Tout ce que la lame traverse est touché : les ennemis (une fois par coup, gros recul) et les **balles ennemies, détruites**. Les améliorations s'appliquent : dégâts, cadence (comptée en racine carrée, minimum 0,16 s entre deux coups), portée et taille des balles (longueur de la lame, plafonnée à 170 px), projectiles en plus (arc plus large), effets à l'impact (gel, poison, feu, éclair, explosion, fragments, guillotine), tirs supplémentaires (Tail Gun, Flank, Capacitor) ; les améliorations Flamer et Arc Caster donnent à la lame leur brûlure / leurs éclairs. Si l'épée est désactivée (Tab), le vaisseau tire normalement, avec ses autres améliorations.

## Boucle de jeu

- **Toutes les salles démarrent pleines d'ennemis**, sauf deux exceptions : la *Launch Bay* (première salle, portes ouvertes tout de suite) et les salles secrètes (sauf leur variante « embuscade »). Ce qui rend une salle spéciale n'apparaît qu'une fois tous les ennemis morts.
- **On entre par la porte opposée à celle par laquelle on est sorti** (sortie à droite : entrée par la gauche ; la Launch Bay : par le bas). Cette porte d'entrée reste fermée (sauf si elle devient le passage secret). Une fois la salle nettoyée, le vaisseau est replacé devant elle et les 3 autres portes sont les sorties. Chaque porte affiche la récompense de la salle suivante, et **les 3 portes sont toujours différentes** (jamais deux fois la même récompense ou le même type de salle). Une porte peut être verrouillée (coûte 1 clef) et mène toujours à une salle d'amélioration.
- **Salles générées** (`src/blocks.c`) : chaque salle à ennemis (combat, Stats, shop, workshop ; pas le boss ni les salles secrètes) assemble des **murs indestructibles** (L, T, croix, U, barres, selon une quinzaine de plans, symétriques ou non, plus des pièces au hasard ; des murs qui **partent des bords** de l'arène — jamais devant une porte —, des **bras qui relient les murs** entre eux en couloirs et culs-de-sac, et parfois un plan « couloirs » : les restes d'un labyrinthe sur une grille grossière, en grande partie ouvert) et des **blocs destructibles**, en grappes ou en portes qui ferment les passages entre les murs. Les blocs ne cassent que sous les tirs du joueur (et l'épée, les explosions) — jamais sous les balles ennemies — et **une bombe les détruit tous d'un coup**. Quatre sortes :
  - **Caisse** (brune, 28 PV) : un simple obstacle.
  - **Baril** (rouge, 18 PV) : explose (rayon 135, 60 dégâts aux ennemis, un demi-cœur au vaisseau s'il est dedans) et fait sauter les blocs voisins en chaîne.
  - **Bidon d'huile** (orange, 20 PV) : laisse une **flaque enflammée** (rayon 80, 7 s) qui met le feu aux ennemis et au vaisseau (un demi-cœur à l'allumage puis chaque seconde tant qu'il brûle ; un dash saute par-dessus) et finit par faire partir les barils et bidons qui baignent dedans.
  - **Spark pod** (violet, 20 PV) : lâche un anneau de 12 boules qui blessent tout le monde, ennemis compris.
  **Tout est aligné sur la grille du sol** (cases de 60 px, arène de 30 x 15 cases, 1800 x 900) : chaque mur et chaque bloc remplit des cases entières ; un bloc fait une case, un mur une ou deux cases d'épaisseur. Les murs se touchent (ils fusionnent en une seule forme) ou laissent au moins deux cases (120 px) entre eux : jamais un passage d'une seule case, jamais deux murs qui ne se touchent que par un coin, jamais de gros pavé de mur plein. Le Crawler s'arrête contre un mur qui part du bord.
  Pendant le combat, tout reste accessible : seuls les murs comptent pour la connexité (chaque porte et chaque recoin est relié à l'entrée, `room_walls_connected`), les blocs peuvent fermer un passage mais se cassent. Seule la zone devant la porte d'entrée reste dégagée.
- **La salle se brise comme du verre** : dès que le dernier ennemi meurt, tous les obstacles (murs compris) se fissurent puis volent en éclats, en une vague qui part de l'endroit du dernier kill, et les flaques de feu s'éteignent (aucun dégât, aucune explosion). Après 1,2 s (`ROOM_REVEAL_T`) la salle est vide : c'est seulement alors qu'apparaissent la récompense, le shop, les machines du workshop ou le butin, et que les portes s'ouvrent. Une salle nettoyée est donc toujours entièrement dégagée.
- **Salles d'amélioration : toujours 3 améliorations au choix** (avec leur description), on en prend une et les deux autres disparaissent. Il n'y a plus de salles d'arme : les améliorations qui transforment le tir sont proposées comme toutes les autres.
- **Menu des améliorations (Tab)**, en jeu ou depuis la pause : la liste des objets activables possédés (en premier) et de toutes les améliorations trouvées, chacun(e) **activable / désactivable une par une** (clic, Entrée ou Espace ; flèches / molette pour naviguer ; Tab ou Échap pour revenir). Le jeu est figé pendant ce temps. Une amélioration désactivée n'a plus aucun effet (stats comprises) jusqu'à ce qu'on la réactive ; réactiver un cœur ne soigne pas ; les échanges de stats restent acquis. Seule Skeleton Key (+3 clefs, effet déjà dépensé) n'est pas désactivable. Le HUD (en haut à droite) affiche une icône par amélioration active et le nombre d'améliorations désactivées.
- **Objets activables** : **on peut en avoir plusieurs à la fois**. Shift / clic droit déclenche **tous ceux qui sont activés et prêts** en même temps, puis chacun se recharge de son côté (temps réduit par la stat RECHARGE, Thrusters, Overdrive Core). **Chaque nouvel écran (salle suivante, ou retour d'une salle secrète) recharge tous les objets** : ils sont prêts dès l'entrée (un effet en cours continue). Le HUD (en haut à gauche) montre une case par objet : icône, niveau, barre de recharge (ou durée restante de l'effet), « OFF » s'il est désactivé ; un arc par objet en recharge entoure aussi le vaisseau. Comme les améliorations, chacun peut être **désactivé / réactivé dans le menu Tab** (un objet désactivé ne se déclenche plus et son effet en cours s'arrête). **On commence sans** : on les trouve dans les **salles Active** (récompense de porte « ACTIVE », éclair jaune), qui proposent 3 objets au choix une fois les ennemis tués ; c'est une porte comme une autre (pas de garantie). Un nouvel objet s'ajoute aux autres (« NEW ») ; **le même objet peut réapparaître et être repris : il monte de niveau** (jusqu'au niveau 5 ; « LEVEL UP -> N » au-dessus du piédestal) : effets plus longs, explosions plus grosses et plus fortes, recharge -12 % par niveau. Un objet au niveau max n'est plus proposé. Table `ACTIVES` et effets dans `src/actives.c` :
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
- **Fin de salle** : le vaisseau reste où il est. **Rien ne se ramasse en marchant dessus** sauf les ressources (pièces, clefs, bombes, cœurs de récompense, aspirés automatiquement) : un objet sur piédestal, un article du shop, un augment ou une machine du workshop se prend avec **E**. L'objet (ou la machine) le plus proche à portée est encadré, avec une touche « E » au-dessus (`interact_target`, `interact_draw` dans `src/pickups.c`). **Les portes restent fermées 1,8 s après le dernier kill** (`DOORS_OPEN_T`) : la récompense apparaît d'abord (à 1,2 s), pour qu'on ne fonce pas dehors sans elle.
- **Stats du vaisseau** (F2, cachées par défaut) : panneau semi-transparent en haut à gauche de l'arène, avec dégâts, cadence, portée, vitesse des tirs, dispersion (spread), vitesse, recharge, hitbox, chance, projectiles, perçage, rebonds, drones. Valeurs effectives (bonus situationnels de Bloodlust / Adrenaline inclus), en vert au-dessus de la base et en rouge en dessous ; une stat qui vient de changer clignote.
- **Contact** : un ennemi qui percute le vaisseau lui fait perdre un demi-cœur, mais le choc le blesse aussi (30 dégâts, plus si la stat de dégâts dépasse x1, et un fort recul) : les petits ennemis qui foncent sur le vaisseau (Chaser, Swarm) meurent sur le coup. Un contact esquivé (invulnérabilité, dash, bouclier) ne blesse personne (`CONTACT_DAMAGE`, `src/enemies.c`).
- Ressources : pièces, clefs, bombes (+ cœurs). Une salle de clef en donne 1, parfois 2 (14 %), rarement 3 (4 %) ; une salle de pièces en donne `13-18 + 0,55 x profondeur` (x chance, x1,5 avec Coin Magnet). **Les ennemis ne lâchent jamais rien** : les ressources sont uniquement des récompenses de salle, aspirées automatiquement une fois la salle nettoyée.
- **Salles Stats** (porte turquoise, ~20 % des portes après un combat) : une fois les ennemis tués, 4 échanges apparaissent, on en prend un (ou aucun). Chaque échange augmente une stat et en baisse une autre (**jamais les PV max** : un échange peut donner +1 cœur, jamais en retirer ; seuls les augments du workshop coûtent des PV max) (il modifie les stats de base, avant les améliorations). Stats : dégâts, cadence, portée, PV max, vitesse de déplacement, **recharge** (de l'objet activable), vitesse des projectiles, taille de la hitbox, chance (quantité des récompenses de salle), **dispersion** (spread : -25 % = tirs plus groupés, +30 % = plus écartés, bornée entre x0.2 et x3). Échange spécial **+3 boucliers** : 3 cœurs bleus temporaires (absorbent les dégâts avant les vrais cœurs, ne se régénèrent pas, max 6) contre une baisse de stat. Valeurs et bornes dans `src/stats.c`.
- **Shops** (porte rose, à partir de la salle 2, jamais deux d'affilée) : une fois les ennemis tués, toujours 5 objets à vendre — au moins deux améliorations et un échange de stats, plus 2 emplacements aléatoires — avec leur description (bonus et malus), et un **comptoir** en bas à droite : 1 cœur (6 pièces, refusé si la santé est pleine), 1 bombe (5) et 1 clef (6). On achète avec E ; prix affiché en doré si on peut payer, en rouge sinon. Prix : `shop_price()` dans `src/pickups.c`.
- **Salles secrètes** (~15 % des salles de combat, Stats et shops) : pendant le combat, un petit fantôme semi-transparent apparaît n'importe où dans la salle toutes les 5 à 10 secondes, flotte quelques instants (0,5 à 2 s, en ondulant) avec un fondu à l'apparition et à la disparition, puis s'évanouit. Une bombe déclenchée à côté de la porte d'entrée l'ouvre, vers la salle secrète (on y entre par le côté opposé, et sa sortie ramène par cette même porte) : pas d'ennemis, le butin attend déjà — 1 amélioration (rarement 2) + pièces, bombes ou clefs. Variantes : 10 % de chance d'un **shop secret** (5 objets à vendre à la place de l'objet gratuit) et 10 % de chance d'une **embuscade** (une vague d'ennemis avant que le butin n'apparaisse). La sortie ramène dans la salle d'origine.
- **Workshop** (porte vert-jaune, à partir de la salle 2, ~14 % des portes, jamais deux d'affilée) : démarre plein d'ennemis comme les autres ; une fois nettoyé, des machines apparaissent. On utilise une machine avec E, à côté d'elle (un appui = une utilisation) :
  - **Supply Press** (en bas à gauche) : 1/2 cœur de dégâts (bouclier d'abord) contre des pièces (45 %, 5-8), une bombe (30 %) ou une clef (25 %). 3 à 5 utilisations, refusée s'il ne reste qu'un demi-cœur.
  - **Augments** (au centre) : bonus **exclusifs** au workshop, très puissants (stats + effet), payés en **PV max** : 1 cœur (Blood Rounds, Overdrive Core), 2 cœurs (Hellfire Engine, Storm Core) ou 3 cœurs (Demon Heart, God Cannon). Un emplacement par palier abordable (il faut garder au moins 1 cœur max), on en prend un seul.
  - **Slot Machine** (en bas à droite) : affiche le nom et l'effet du lot (une amélioration) ; chaque mise coûte 5 pièces, `30 % + 3 % x chance` de gagner (entre 15 et 50 %). Le lot gagné tombe devant la machine.
- **Salles de boss** (salle 10, 20, 30...) : la salle 9 (puis 19, 29...) n'a qu'**une seule porte**, rouge, marquée d'un œil : la salle du boss (jamais verrouillée). Arène sans obstacles ni blocs, une seule « vague » : **THE OVERSEER**, un grand hexagone à l'œil qui suit le vaisseau (4800 PV, `EN_BOSS` dans `src/enemies.c`). Sa **barre de vie** est affichée en haut de l'écran, avec les seuils de phase (66 % et 33 %) et une traînée blanche des dégâts récents. Il plane dans la moitié haute et enchaîne ses attaques avec une courte pause entre deux (jamais deux fois la même d'affilée) :
  - **Phase 1** : spirale à 3 bras ; 3 éventails visés de 7 balles ; anneaux de 32 balles avec une brèche de 4 qui se déplace d'un anneau à l'autre.
  - **Phase 2** (66 %) : + **charge** annoncée par un large faisceau (il vise puis se verrouille), traverse l'arène en semant des balles lentes et finit par un anneau ; + **croix tournante** de 4 flux qui change de sens à mi-parcours ; les éventails sont doublés.
  - **Phase 3 « ENRAGED »** (33 %) : tout est 30 % plus rapide, spirale double contra-rotative, croix à 6 flux, et il appelle des renforts (chasers et shooters ; `2 + profondeur / 10`, donc plus nombreux aux boss suivants — le boss, lui, ne change jamais).
  - Chaque changement de phase efface les balles, le rend invulnérable 1,4 s (anneau doré) et lâche une onde de choc ; un gros coup ne peut pas sauter une phase (les dégâts s'arrêtent au seuil). Guillotine ne l'exécute pas, Black Hole ne l'attire pas.
  - À sa mort, ses renforts et toutes les balles disparaissent, et il laisse **3 améliorations au choix** (on en prend une).
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
  - **Laser** : un rayon continu qui traverse tout (flux de segments très rapides : il rebondit, se courbe ou se divise avec les autres améliorations) ; -40 % de vitesse de déplacement pendant le tir. Combiné à autre chose, le rayon reste continu (mêmes dégâts par seconde).
  - **Orbit** : les tirs tournent autour du vaisseau en spirale qui s'élargit (+50 % dégâts, +1 perçage) : un mur de projectiles, mais on ne vise plus.
  - **Prism** : chaque tir se divise en trois en plein vol (-35 % dégâts, cadence plus lente).
  - **Graviton** : orbes lourds et lents qui entraînent les ennemis avec eux, puis implosent.
  - **Sword** : l'épée (voir Personnages), remplace le canon ; disponible pour tous les personnages.
  - **L'épée se combine avec toutes les améliorations de tir** : Laser → **sabre laser** (dégâts x2,5, lame plus longue, brûle), Railgun → espadon (dégâts x2,4, plus long, plus lent), Needler → rapière (plus rapide, plus légère), Scatter → arc plus large, Orbit → tourbillon à 360°, Vulcan → les coups s'accélèrent, Bouncer → recul énorme, Graviton → chaque coup aspire les ennemis, Flamer / Arc / Rockets / Cluster → la lame brûle / foudroie / explose / éclate en fragments. Les améliorations qui façonnent des projectiles (Seeker, Bouncer, Rockets, Chakram, Cluster, Wave, Prism, Graviton, Orbit) font lancer à chaque coup une **vague de lame** qui les porte toutes (elle cherche sa cible, rebondit, se divise en trois, revient...). Les coups critiques et Charge Shot marchent aussi sur la lame.
  - Combinaisons bornées pour rester jouables : entre 0,03 et 0,7 s entre deux salves (les dégâts compensent), 40 projectiles par salve au plus (plus forts au-delà), une portée minimale (320 px, le double pour les tirs qui reviennent). L'apparence, la couleur et le son des tirs viennent de l'amélioration la plus marquante (Railgun > Rockets > Chakram > Flamer > Cluster > Arc > Wave > ...). Une amélioration qui ne va pas avec les autres se désactive dans le menu Tab.
- **Chaque amélioration a un bonus ET un malus** (en rouge sous le bonus, sur les piédestaux, en boutique, sur la machine à sous, dans le menu Tab et dans la bannière) : c'est ce qui rend utile le choix de celles qu'on active. Désactiver une amélioration retire les deux. Les malus (table `MALUSES` dans `src/maluses.c`) : baisse d'une stat (dégâts, cadence, portée, vitesse, vitesse des tirs, hitbox plus grosse, dispersion, recharge des objets activables, chance), recul à chaque salve, vaisseau plus lent en tirant, balles ennemies plus rapides, invulnérabilité plus courte après un coup, prix des shops plus élevés, pièces perdues à chaque salle nettoyée ou à chaque coup reçu, surchauffe (le canon s'enraye après 3 s de tir continu), délai avant le premier tir, premier coup de chaque salle plus douloureux, vagues plus grosses (plus d'ennemis, jamais plus forts), vaisseau plus lourd (inertie), pas de tir au début de chaque salle, effets des objets activables plus courts, moins de cœurs dans les récompenses. Pour les améliorations de tir (Scatter, Railgun...), le malus fait partie de l'effet (cadence, portée). Chaos prête ses améliorations sans leur malus. Skeleton Key n'étant pas désactivable, son malus (+10 % aux prix) reste.
- **109 améliorations passives** (+ 17 améliorations de tir et 6 exclusives du workshop) (table `UPGRADES` et effets dans `src/upgrades.c`) :
  - *Tir* : Overclock, Heavy Rounds, Split Shot, Piercer, Rubber Core, Smart Rounds, Accelerator, Big Iron, Wide Lens, Focus Lens, Long Barrel, Hair Trigger, Magnum Rounds (+2 perçage, +15 % dégâts, -15 % cadence), Spray and Pray (+45 % cadence, +60 % dispersion), Heavy Caliber (+40 % dégâts, gros tirs, -20 % vitesse des tirs), Smart Bounce (+1 rebond, chaque rebond vise l'ennemi le plus proche).
  - *Dégâts conditionnels* : Sniper Scope (jusqu'à +60 % selon la distance parcourue), Point Blank (+50 % de près), Bully (+40 % sous la moitié des PV), Giant Slayer (+60 % contre les gros ennemis), Momentum (+1,5 % par touche, max +60 %, perdu après 1 s sans toucher ; -20 % dégâts de base), Growth (+4 % par salle nettoyée, max +80 % ; -15 % dégâts de base), Turret Mode (+40 % cadence immobile), Run and Gun (+35 % dégâts en mouvement, -25 % à l'arrêt), Rampage (+3 % cadence par kill dans la salle, max +45 %), Overkill (les dégâts en trop passent à l'ennemi le plus proche).
  - *Effets sur les balles* : Cryo Rounds (ralentit), Venom Tips (poison), Thermite (brûlure), Arc Emitter (éclair en chaîne), Payload (explosion), Shrapnel (fragments), Lucky Strike (coups x3), Guillotine (exécute sous 15 % PV), Return Vector (boomerang), Phase Rounds (traverse les obstacles), Impact Driver (recul), Frostbite (gèle parfois, +35 % dégâts sur les ennemis gelés), Pyromaniac (enflamme parfois, les ennemis en feu explosent en mourant), Toxic Cloud (empoisonne parfois, le poison se propage à la mort), Chain Reaction (6 tirs à chaque kill), Black Hole (un kill ouvre parfois un vortex qui aspire les ennemis).
  - *Tirs supplémentaires* : Tail Gun (arrière), Flank Cannons (côtés), Capacitor (méga tir toutes les 10 salves), Wing Drone, Nova Trigger (anneau de tirs à chaque utilisation de l'objet activable), Mine Layer, Missile Pod (missile à tête chercheuse toutes les 1,5 s), Orbital Strike (frappe orbitale sur un ennemi toutes les 3 s), Echo (chaque salve répétée 0,12 s plus tard à 50 %).
  - *Comportements de tir (nouveaux)* : Ricochet (un tir qui touche saute vers un autre ennemi), Snowball Rounds (les tirs grossissent en vol : jusqu'à x2,5 et +80 % dégâts), Sticky Charges (les touches posent une charge qui explose 0,7 s plus tard), Fireworks (les tirs en fin de portée éclatent en 5 étincelles), Pinball (+2 rebonds, +40 % dégâts et plus vite à chaque rebond), Shatter (les ennemis gelés éclatent en éclats de glace qui gèlent), Superconductor (tous les éclairs sautent 3 fois de plus), Alchemist (+30 % dégâts par affliction sur la cible : gel, poison, feu), Critical Mass (plus de critiques, et ils explosent), First Blood (dégâts x3 sur un ennemi à pleine vie), Voodoo Link (25 % des dégâts touchent tous les ennemis du même type).
  - *Façons de jouer (nouveaux)* : Dead Man's Trigger (+40 % cadence, +15 % dégâts, mais le canon tire sans arrêt en combat), Charge Shot (ne pas tirer charge la prochaine salve, jusqu'à x6), Graze (frôler les balles ennemies : +3 % dégâts dans la salle, max +90 %), Chrono Bubble (les balles ennemies proches du vaisseau sont 45 % plus lentes), Shadow Clone (un vaisseau fantôme suit votre trajectoire et tire la moitié de vos salves), Blood Money (+1 % dégâts par pièce, max +50 %), Wild Card (chaque salle : +40 % dégâts, +40 % cadence ou +25 % vitesse au hasard), Point Defense (un laser détruit la balle ennemie la plus proche toutes les 0,5 s), Trail Blazer (traînée de feu derrière le vaisseau), Aegis (bouclier frontal qui bloque les balles venant d'en face), Afterburner (utiliser un objet activable : +80 % cadence et +30 % vitesse pendant 4 s), Berserker (+45 % cadence, +25 % dégâts, mais chaque coup reçu coûte un demi-cœur de plus), Bomb Factory (+1 bombe par salle nettoyée tant qu'on en a moins de 4), Flawless (+8 % dégâts par salle sans être touché, max +80 %, perdu au premier coup), Vengeance (être touché : dégâts x2 pendant 6 s). Coin Magnet (ex-Magnet) donne maintenant +50 % de pièces en récompense.
  - *Malus* : en plus des baisses de stats, des malus de comportement : surchauffe, recul, délai avant le tir, premier coup de la salle plus cher, vagues plus grosses, vaisseau plus lourd, **Fragile** (chaque coup coûte plus), **Revenge** (un ennemi tué peut tirer une dernière balle vers le vaisseau), **Big Bullets** (balles ennemies plus grosses), **No Mercy** (être touché n'efface plus les balles autour du vaisseau), **cadence réduite en mouvement**, etc. (`MalusKind`, `src/maluses.c`).
- **Synergies** (38, `src/synergies.c`) : des effets en plus qui n'existent que si plusieurs améliorations sont **actives ensemble** (désactiver une pièce dans le menu Tab coupe la synergie ; les prêts de Chaos comptent). Rien à ramasser : une bannière « SYNERGY: ... » l'annonce la première fois, et le menu Tab liste les synergies actives (avec leurs pièces) avant les améliorations.
  - *Réactions élémentaires* (n'importe quelle source de feu / gel / poison / foudre) : **Thermal Shock** (feu + gel : dégâts instantanés en zone, les deux états sont consommés), **Combustion** (feu + poison : le poison explose et enflamme les voisins), **Flash Freeze** (gel + foudre : éclairs plus longs, dégâts x2 sur les ennemis gelés), **Plague** (poison + Voodoo Link : le poison touche tous les ennemis du même type).
  - *Épée* : **Parry** (+ Deflector Shells : la lame renvoie les balles au lieu de les détruire), **Blade Dancer** (+ Shadow Clone : le clone donne de vrais coups d'épée), **Thrown Blade** (+ Return Vector : un coup sur deux lance une lame qui revient).
  - *Paires de tirs* : **Tesla Beam** (Laser + Arc Caster : des éclairs partent du vaisseau pendant le tir), **Plasma Cutter** (Laser + Flamer : le bout du rayon laisse le sol en feu), **Event Horizon** (Railgun + Graviton : chaque tir s'effondre en vortex), **Saw Halo** (Chakram + Orbit : un anneau de scies autour du vaisseau), **MIRV** (Rockets + Seeker + Cluster : les fragments sont des roquettes guidées).
  - *Défense* : **Mirror Shield** (Aegis + Deflector : le bouclier renvoie les balles), **Shield Bash** (Aegis + Ram Plating ou Spiked Hull : le bouclier blesse au contact), **Storm Orbs** (Guardian Orb + Tesla Field), **Frozen Time** (Chrono Bubble + gel : les ennemis dans la bulle sont gelés), **Phoenix** (Second Wind + Vengeance : ressusciter brûle tout et lance 12 s de Vengeance), **Failsafe** (Barrier Cell + Emergency Shield : le blocage redonne un bouclier).
  - *Drones, bombes, mines* : **Interceptors** (Wing Drone + Point Defense : ce sont les drones qui tirent, plus vite à chaque drone), **Lightning Rod** (Wing Drone + Tesla Field : les tirs des drones font des éclairs en chaîne), **Demolition** (Sticky Charges ou Mine Layer + une bombe : tout saute d'un coup, deux fois plus fort), **Scorched Earth** (Bomb Factory + Bomb Belt : les bombes laissent un champ de feu), **Seeker Mines** (Mine Layer + Smart Rounds ou Seeker : les mines rampent vers les ennemis).
  - *Tirs en plus, effets à la mort* : **Starburst** (Tail Gun + Flank Cannons : tirs latéraux à chaque salve + 4 diagonales), **Echo Chamber** (Echo + Shadow Clone), **Overload** (Capacitor + Charge Shot : une charge pleine tire aussi un méga tir), **Rain of Fire** (Missile Pod + Orbital Strike : chaque frappe lâche 3 missiles), **Chain Explosion** (Volatile Targets + Chain Reaction), **Singularity** (Black Hole + Graviton : deux fois plus de vortex, plus longs, plus larges).
  - *Règles de dégâts, façons de jouer* : **Close Call** (Graze + Point Defense : les balles abattues comptent comme frôlées), **Unstoppable** (Momentum + Rampage : les kills nourrissent Momentum, max +80 %), **Executioner** (Guillotine + Bully : exécution sous 25 %), **Deadeye** (Sniper Scope + Long Barrel : bonus doublé), **Compound Interest** (Piggy Bank + Blood Money : 6 pièces par salle), **Trick Shot** (Ricochet + Pinball : 3 sauts, +40 % par saut), **Grand Finale** (Fireworks + Snowball : 10 étincelles plus grosses), **Stacked Deck** (Wild Card + Loaded Dice : deux bonus par salle), **Steady Aim** (Charge Shot + Turret Mode : charge x2 à l'arrêt).
  - **Les améliorations s'appliquent à tous les tirs du joueur**, pas seulement au canon : tirs arrière / latéraux / Capacitor, Echo, drones, missiles, tourelle, anneau Nova, balles ennemies retournées (Reversal, Deflector). Ils reçoivent les comportements des améliorations de tir (explosion, éclairs, feu, fragments, vague, retour...), le perçage, les rebonds, la tête chercheuse, la taille, les effets au tir (critique...) et les bonus de dégâts, portée et vitesse des tirs (`pbullet_arm()` dans `src/weapons.c`). Seuls les sous-produits d'effets (fragments, mines, vortex de Black Hole) n'y passent pas, pour éviter les réactions en chaîne ; les effets à l'impact (poison, gel, feu...) s'appliquent à tout.
  - *Stats* : Thrusters, Hull Plating, Glass Cannon, Titan Hull, Featherweight, Four-Leaf Chip, Micro Hull, Magnet.
  - *Économie* : Piggy Bank, Bomb Belt, Skeleton Key, Coupon (shop -30 %).
  - *Défense / déclencheurs* : Reactive Armor, Second Wind (survit à un coup mortel), Barrier Cell (bouclier rechargé à chaque salle), Guardian Orb, Tesla Field, Leech Core, Adrenaline, Bloodlust, Ram Plating, Chrono Field, Volatile Targets, Emergency Shield (le premier coup de chaque salle est bloqué), Spiked Hull (gros dégâts aux ennemis qui touchent la coque).
  - *Blessé* (actives quand il manque au moins un demi-cœur rouge) : Wounded Fury (+30 % dégâts), Scar Tissue (+25 % cadence), Last Stand (+10 % dégâts et +5 % vitesse par cœur manquant), Blood Scent (tirs à tête chercheuse).
  - *Spéciales* : Deflector Shells (tirs énormes et très lents, cadence / 3, mais ils renvoient les balles ennemies touchées comme des tirs du joueur), Loaded Dice (double la probabilité de déclenchement de tous les effets), X-Ray Visor (les salles qui cachent un secret sont toujours signalées).
  - Les descriptions des effets aléatoires affichent la **probabilité exacte** (en tenant compte du nombre d'exemplaires et de Loaded Dice), calculée par `upgrade_desc()`. Un `|` dans une description la coupe sur deux lignes.
- **Les ennemis gardent toujours les mêmes attributs** (PV, vitesse, cadence, patterns) quelle que soit la profondeur. La difficulté vient du nombre : budget de vague `4 + 1.1 x profondeur^1.15 + vague` (plafond `20 + 2 x profondeur` ennemis par vague), `2 + min(profondeur / 5, 5)` vagues par salle (la vague suivante arrive quand il reste un quart de la précédente, ou après 16 s s'il en reste au plus un tiers), et de nouveaux ennemis plus durs débloqués au fil des salles : burster (salle 3), spinner (4), sniper (5), tank (7), **warden** (9 : croix tournante de 4 flux + éventail visé), **hunter** (11 : traque, s'annonce par une ligne, fonce puis tire un anneau), **hive** (13 : pond des essaims et tire des anneaux).
- **20 ennemis aux comportements variés** (`src/enemies.c`, une fonction `update_<nom>` chacun), débloqués entre la salle 2 et la salle 14 :
  - **Rammer** (2) : se met en ligne, gratte le sol (ligne de charge qui s'épaissit), fonce tout droit jusqu'au mur, y lâche des débris et reste **sonné** (étoiles) : le moment de le punir.
  - **Grenadier** (2) : reste à distance et **lance des grenades en cloche** là où va le vaisseau ; elles passent au-dessus de tout, sont inoffensives en l'air (ombre au sol, cercle d'impact) et éclatent en anneau à l'atterrissage.
  - **Crawler** (3) : **collé au mur**, il court le long des bords (et passe les coins) pour se placer face au vaisseau, puis crache des rafales vers l'intérieur. Une meute s'étale le long du mur.
  - **Top** (3) : une **toupie** qui file en ligne droite et rebondit partout, de plus en plus vite, en semant des balles ; un anneau à chaque rebond. Au bout de 6 s, elle a le tournis, titube et ralentit.
  - **Kamikaze** (3) : fonce, s'arrête près du vaisseau, gonfle en clignotant et explose en double anneau (ça ne compte pas comme un kill).
  - **Dodger** (4) : mitraille en straffant et **esquive les tirs** qui lui arrivent dessus d'un pas de côté (recharge de 1,2 s, un anneau indique qu'il est prêt) : une rafale ou un tir large l'attrapent.
  - **Rook** (4) : comme la tour aux échecs, **ne bouge qu'en lignes et colonnes** : il glisse jusqu'à la ligne ou la colonne du vaisseau, puis tire une croix de balles le long des axes.
  - **Splitter** (5) : blob mou qui crache des balles ondulantes ; tué, il **se divise en trois splitlings** qui sautillent vers le vaisseau.
  - **Leaper** (5) : s'accroupit puis **saute sur le vaisseau** (intouchable en l'air, ombre au sol) et atterrit avec une onde de choc.
  - **Minelayer** (6) : parcourt la salle en posant des **mines** qui s'arment, puis explosent en anneau quand le vaisseau s'approche (cercle clignotant) ou au bout de 5 s.
  - **Boomerang** (6) : lance trois **boomerangs** en éventail qui s'éloignent en courbe, s'arrêtent et reviennent.
  - **Ricochet** (7) : tire des **balles qui rebondissent** deux fois, souvent visées sur le reflet du vaisseau dans un mur : elles arrivent de là où il n'y a personne.
  - **Orbiter** (7) : **tourne autour du vaisseau** à distance variable, change de sens, et s'arrête le temps de ses rafales.
  - **Blinker** (8) : tire une grosse orbe qui **éclate en anneau**, puis **se téléporte** (sa destination clignote d'abord).
  - **Serpent** (8) : ondule en grands S et laisse derrière lui un **mur de balles immobiles** qui s'éteint.
  - **Mirror** (9) : une **plaque à l'avant arrête tous les tirs** ; elle pivote lentement vers le vaisseau : il faut le contourner. Tire des balles ondulantes.
  - **Lancer** (11) : charge un **rayon** (la ligne de visée s'épaissit puis se verrouille) et tire un flot de balles très rapides qui balaie vers le vaisseau.
  - **Mortar** (12) : lourd et presque immobile, il **pilonne** la zone du vaisseau : une grenade sur lui, quatre autour (cercles d'impact au sol).
  - **Guardian** (12) : suit ses alliés et les **protège** (liens bleus : ils ne prennent que 40 % des dégâts tant qu'il vit) : à tuer en premier.
  - **Wraith** (14) : **disparaît** (les tirs le traversent) en se rapprochant, puis apparaît un instant pour lâcher des feux follets à **tête chercheuse**.
  - Nouveaux projectiles ennemis (`EBulletKind` dans `game.h`, `update_enemy_bullets` dans `bullets.c`) : grenade, mine, boomerang, balle rebondissante, orbe à fragmentation, balle à tête chercheuse, balle ondulante. Le bot les esquive (zones d'impact, mines, traînées, lignes de charge).

## Architecture (`src/`)

| Fichier | Rôle |
|---|---|
| `main.c` | Init SDL, boucle à pas fixe (120 Hz), entrées, options de dev |
| `game.c/h` | État global, machine d'états, ordre update/render, toutes les structures |
| `draw.c/h` | Renderer de formes batché (`SDL_RenderGeometry`) + texte |
| `room.c` | Salles, vagues, portes, collisions, navigation |
| `blocks.c` | Plans des salles (murs, blocs, connexité), blocs destructibles et leurs effets, flaques de feu, bris de verre en fin de salle |
| `player.c` | Vaisseau, déplacement, bombe, dégâts |
| `weapons.c` | Le tir façonné par les améliorations de tir (`gun_profile`), l'épée, leurs effets (`weapon_on_hit`, `weapon_bullet_death`), drones |
| `upgrades.c` | Table des 132 améliorations (dont 17 améliorations de tir et 6 exclusives du workshop), leurs effets (hooks appelés par les balles, ennemis, joueur, salles), stats recalculées depuis la base + les améliorations actives (`stats_compute`, `player_recompute`), activation / désactivation |
| `upgrade_menu.c` | Menu Tab : améliorations trouvées, activer / désactiver |
| `actives.c` | Objets activables (11), niveaux, tourelle et lames, icônes, choix des salles Active |
| `synergies.c` | Table des 38 synergies (pièces requises), détection (`syn()`), bannière de découverte |
| `workshop.c` | Machines du workshop (Supply Press, Slot Machine) et emplacements des augments |
| `enemies.c` | 10 types d'ennemis et leurs patterns de balles, et le boss (The Overseer : phases, attaques) |
| `bullets.c` | Balles joueur / ennemies, collisions |
| `pickups.c` | Pièces, clefs, bombes, cœurs, objets sur piédestal, icônes |
| `fx.c` | Particules, screen shake, popups, bannières |
| `bot.c` | Autopilote (visée, esquive, navigation, choix de porte) pour les tests |
| `headless.c` | Simulation sans fenêtre + vérification d'invariants |
| `stats.c` | Stats échangeables des salles Stats (gains, pertes, bornes) |
| `audio.c/h` | Synthèse procédurale des sons + mixeur (voix, panoramique, limitation de fréquence) |
| `hud.c` | HUD, overlay des stats (F2), écrans (pause, game over) |
| `title.c` | Menu principal et partie de démo jouée par le bot en fond |
| `characters.c` | Personnages (stats et amélioration de départ) et écran de sélection |
| `debug_menu.c` | Menu de debug F3 : téléportation dans chaque type de salle (avec / sans ennemis), cheats |

Le type de salle `ROOM_TREASURE` est déjà prévu (sur le modèle de `ROOM_STATS` / `ROOM_SHOP`) dans `RoomType` (`game.h`) : il suffit de le générer dans `gen_doors()` (`room.c`) et de lui donner un comportement dans `room_enter()`.

## Tests (sans fenêtre)

```sh
ctest --preset release      # ~50 s
ctest --preset asan         # même suite sous AddressSanitizer + UBSan (cmake --preset asan && cmake --build --preset asan)
```

Aucun test n'ouvre de fenêtre :

- **Simulation headless** (`--headless`) : pas de vidéo ni d'audio, un autopilote (`bot.c`) joue aussi vite que le CPU le permet (~350x le temps réel). Des invariants sont vérifiés à chaque tick (positions finies et dans l'arène, PV cohérents, stats toujours égales à la base + les améliorations actives, amélioration désactivée sans aucun effet, amélioration jamais possédée deux fois ni proposée une fois possédée, aucun objet au sol avant la fin d'une salle (pas de drop d'ennemi), shop à 5 objets, portes toujours uniques, objets activables (aucun au départ, 3 objets distincts, ceux qu'on possède seulement s'ils peuvent encore monter de niveau, objet désactivé sans effet, Chaos ne prête que des améliorations non possédées et les retire toutes), machines et augments du workshop, ennemis toujours aux PV de leur type, vaisseau replacé en fin de salle, 3 choix dans les salles d'amélioration, salle de boss toutes les 10 salles derrière l'unique porte de la salle d'avant, boss qui ne saute jamais une phase et laisse 3 améliorations au choix, butin et retour des salles secrètes, plans de salle connexes (murs seuls, chaque case libre et chaque porte accessibles au vaisseau), murs jamais cassés pendant le combat, salle nettoyée vide d'obstacles et de feu, aucune récompense avant la fin du bris de verre, aucun objet pris sans appuyer sur E, aucune balle ennemie restante une fois la salle nettoyée, pas de salle bloquée — aucun ennemi touché pendant 60 s —, pas de salle nettoyée jamais quittée). Comme un joueur dans le menu Tab, le bot désactive une de ses améliorations de tir quand ses tirs n'ont rien touché depuis 10 s (combinaison inadaptée). En cas d'échec, `--fail-shot` enregistre l'image de l'instant fautif (rendue offscreen).
- **Rendu** : `--shot` dessine dans le pilote vidéo `offscreen` de SDL et sauve un BMP.
- **Audio** : `--dump-sfx` synthétise tous les sons en WAV.

Les sorties des tests sont dans `build/<preset>/test-output/`.

## Menu de debug (F3)

Fige le jeu et permet de se téléporter dans chaque type de salle (Launch Bay, combat, Stats, Shop, Workshop, boss — à la prochaine profondeur multiple de 10 —, salle secrète), **avec ses ennemis** ou **sans ennemis** (salle déjà nettoyée, récompense déjà sortie) ; la Launch Bay et la salle secrète n'ont que « sans ennemis ». On choisit aussi la profondeur de la salle et la récompense des salles de combat (aléatoire ou imposée). Pour la salle secrète, on arrive depuis une salle de combat nettoyée, où l'on revient en sortant. Bonus : god mode, overlay FPS, « secret dans chaque salle », « afficher les secrets » (carré, libellé et rayon de la bombe à côté de la porte d'entrée ; le secret est aussi indiqué en bas du menu), +50 pièces, +3 bombes, « give upgrade » (choisir n'importe quelle amélioration), « give active » (n'importe quel objet activable). La récompense « ACTIVE » est dans la liste des récompenses de combat. Pour la salle secrète, on choisit aussi son contenu (aléatoire / objet / shop) ; « avec ennemis » donne l'embuscade. Navigation flèches / WASD + Entrée, ou à la souris ; F3 ou Échap pour fermer.

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
neon_breach --boss                                      # démarrer dans la salle du boss (salle 10, ou --depth 20...)
neon_breach --shot w.bmp --workshop --cleared --no-bot  # capture du workshop déjà nettoyé, sans autopilote
neon_breach --active shield                             # démarrer avec un objet activable, option répétable (dash / shield / boom / strength / chaos /
                                                        #   reversal / turret / blink / nova / timestop / blades)
neon_breach --weapon rockets                            # démarrer avec une amélioration de tir (scatter / needler / seeker / bouncer / railgun /
                                                        #   flamer / rockets / chakram / cluster / arc / vulcan / wave / laser / orbit / prism / graviton / sword), option répétable
neon_breach --enemy rammer --enemy mortar                # vagues composées uniquement de ces ennemis (à toute profondeur), répétable
neon_breach --upgrade aegis --upgrade deflector-shells    # démarrer avec des améliorations précises (nom du jeu, « - » pour les espaces), répétable
neon_breach --shot u.bmp --all-upgrades --upgrades-menu # capture du menu Tab
neon_breach --headless --toggle-upgrades --runs 10      # le bot désactive / réactive une amélioration au hasard toutes les 4 s
neon_breach --active-room --cleared --no-bot            # salle Active nettoyée (3 objets au choix)
neon_breach --secret                                    # chaque salle cache une salle secrète
neon_breach --show-secret                               # signale les secrets (comme dans le menu F3)
neon_breach --show-stats                                # démarre avec l'overlay des stats (F2) visible
neon_breach --char knight --depth 3                     # joue un personnage (normal / shotgunner / knight)
neon_breach --headless --char knight --runs 20          # simulations avec un personnage
neon_breach --shot sel.bmp --char-select                # capture de l'écran de sélection
neon_breach --shot menu.bmp --title --frames 1500       # capture du menu principal (la démo joue derrière)
neon_breach --headless --debug-rooms                    # teste les téléportations du menu de debug
neon_breach --headless --god --all-upgrades             # démarre avec toutes les améliorations
neon_breach --headless --dense-blocks --runs 10         # salles bourrées de blocs (budget de blocs maximal)
neon_breach --hp 1                                      # démarre avec 1 PV (tester boucliers / Second Wind)
neon_breach --bot --god                                 # regarder l'autopilote jouer (ouvre une fenêtre)
```

Options headless : `--ticks N` (durée max par partie, 120 ticks = 1 s, défaut 10 min), `--runs N`, `--expect-rooms N` (échec si moins de N salles nettoyées). Le résumé de chaque partie indique le personnage et le nombre de coups encaissés (`hits=`).
