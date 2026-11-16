# Benchmarks d'équilibrage des upgrades

Document de conception : rien de ce qui suit n'est encore codé.

## But

Donner à chaque upgrade une note chiffrée de sa valeur réelle en combat, bonus et malus compris, en faisant
jouer le bot des milliers de fois. Aujourd'hui l'équilibrage se fait à l'impression ; ce benchmark doit dire :

- quels upgrades sont trop forts ou trop faibles ;
- quels upgrades sont **négatifs** (le malus pèse plus que le bonus) ;
- pourquoi (attaque ou défense) ;
- ce qu'une passe d'équilibrage a changé, en comparant deux rapports.

## L'idée de départ

Équiper une dizaine d'upgrades au hasard, lancer des combats à la profondeur 25, regarder combien de temps le
bot survit, et après quelques milliers de combats noter chaque upgrade de 1 à 100 selon les résultats du bot
quand il l'avait.

L'idée est bonne et l'infrastructure existe presque entièrement (headless, seeds reproductibles, `--upgrade`
répétable, `--depth`, le bot). Mais telle quelle elle mesurerait plusieurs choses mal. Les sections suivantes
listent les pièges, puis le plan retenu.

## Ce qui a été mesuré

12 parties headless à la profondeur 25, vaisseau NORMAL, 6 PV, 10 upgrades tirés au hasard parmi les 198
entrées de `UPGRADES` :

| Mesure | Valeur |
|---|---|
| Temps de calcul par partie (jusqu'à la mort) | 0,04 à 0,6 s, 0,2 s en moyenne |
| Vitesse de simulation | 350 à 800 fois le temps réel |
| Parties où le bot meurt dans la première salle | 7 sur 12 |
| Meilleur résultat | 3 salles nettoyées |
| Durée de survie (temps de jeu) | 28 s à 266 s |

Sans aucun upgrade, le bot meurt toujours dans la première salle, en 22 à 62 s.

Deux conclusions :

- **Le coût n'est pas un problème** : 20 000 parties tiennent en 10 à 15 minutes sur 8 cœurs.
- **Le bruit est énorme** : d'une salle à l'autre la survie varie d'un facteur 10. Une simple moyenne « avec /
  sans l'upgrade » serait noyée dedans. Le plan doit retirer la part de ce bruit qui vient de la salle.

## Les pièges

### 1. On mesure l'upgrade entre les mains du bot

- Un upgrade que le bot exploite mal est sous-noté : positionnement, mêlée, upgrades risqués, ceux qui
  demandent de jouer autrement.
- Un upgrade qui compense un défaut du bot est sur-noté (le homing s'il vise mal).
- Une note très basse est donc d'abord une question : l'upgrade est-il mauvais, ou le bot ne sait-il pas s'en
  servir ? Il faut vérifier le bot avant de toucher l'upgrade.
- Le bot coupe de lui-même un mod d'arme quand ses tirs ne touchent rien pendant 10 s. Dans le benchmark ce
  réflexe doit être désactivé (sinon on note un upgrade éteint) et remplacé par une mesure : le taux de tirs
  qui touchent.

### 2. Certains upgrades ne valent rien dans un combat isolé

- Économie : pièces, prix, chance, clés, bombes.
- Effets d'entrée ou de fin de salle.
- Bonus aux objets actifs : le bot commence sans aucun actif.

Leur bonus ne peut pas se voir dans ce benchmark. Mais leur malus, lui, peut peser en combat, et c'est une
information vraie. Ils restent donc dans le benchmark, avec une étiquette « bonus hors combat » : leur note
ne dit que ce qu'ils coûtent en combat. Ceux dont ni le bonus ni le malus ne touchent au combat doivent sortir
à 50 : ce sont les **témoins** de la méthode (voir plus bas).

### 3. « Temps avant de mourir » est une mauvaise métrique

- Si le bot vide la salle, le combat s'arrête et on n'apprend plus rien.
- Un build qui tue lentement se fait déborder par les timers de vague puis écraser par les murs : le temps de
  survie mélange attaque et défense de façon illisible.
- « Mort ou pas » est une mesure binaire, donc très bruitée.

### 4. Le build doit rester figé

Une partie headless normale enchaîne les salles et le bot ramasse des upgrades, des cœurs, des actifs. Le
build testé ne serait plus celui tiré au départ. Il faut un mode où rien ne s'ajoute en route.

### 5. La difficulté doit être calibrée

Si le bot meurt toujours dans la première salle (c'est presque le cas à 6 PV), ou jamais, tous les upgrades se
ressemblent. Les résultats sont les plus informatifs quand le build de référence échoue à peu près une fois
sur deux.

### 6. Une note par rang ment

Un classement en percentiles étale toujours tout de 1 à 100, même si 80 % des upgrades se valent. La note doit
venir de la taille de l'effet.

### 7. Les synergies ne se mesurent pas au hasard

Avec 198 upgrades il y a environ 19 500 paires ; des builds aléatoires donnent une poignée de combats par
paire. Les synergies se testent à part, de façon ciblée.

## Le plan retenu

### Périmètre

Tous les upgrades sont dans le benchmark, mods d'arme et épée compris. Une table d'étiquettes, relue à la
main, sert seulement à lire le rapport :

- **bonus hors combat** : économie, effets d'entrée / de fin de salle, bonus aux actifs, bombes. La note ne
  mesure que leur coût en combat.
- **témoin** : ni le bonus ni le malus ne touchent au combat. La note attendue est 50.
- **exclusif Baphomet** (`UpgradeDef.tier > 0`) : payé en cœurs dans le vrai jeu, sa note se lit avec son prix.

Une erreur dans cette table fausse une étiquette, jamais les données.

### Le scénario (mode `--bench`)

- Départ à la profondeur 25, vaisseau NORMAL.
- 10 upgrades tirés au hasard, sans doublon.
- Uniquement des salles de combat : pas de récompense, pas de modificateur, pas de secret, pas de boutique.
- PV de départ fixés par le calibrage (les conteneurs de cœur du build comptent), aucun soin entre les salles.
- Au plus 5 salles par partie, pour borner la durée.
- Le réflexe « couper un mod d'arme qui ne touche rien » est désactivé.
- **Des pistes fixes** : une piste est une suite de 5 salles, définie par un numéro. Il y en a une centaine,
  toujours les mêmes. Le contenu d'une salle (disposition, composition et cadence des vagues) est tiré d'un
  **générateur à part**, semé par (piste, numéro de salle), et non du générateur du jeu. Re-semer celui du jeu
  à l'entrée ne suffirait pas : `spawn_wave` tire la composition de chaque vague au moment où elle arrive, et
  d'ici là deux builds différents ont déjà consommé des tirages différents (effets à chance, tirs ennemis).
  Même technique que pour la démo du menu (`G.demo_rng` échangé avec `G.rng`). Avec ça, deux builds sur la même
  piste affrontent les mêmes salles et les mêmes vagues ; seul le déroulé du combat diffère.

### Les mesures

Une ligne CSV par partie : piste, build, puis par salle :

| Mesure | Sert à |
|---|---|
| Salle nettoyée ou non | score principal |
| Progression dans la salle où le bot meurt | score principal (partie décimale) |
| Dégâts subis | sous-note défense |
| Temps de nettoyage | sous-note offense |
| Taux de tirs qui touchent | diagnostic du bot |
| Murs qui se referment déclenchés ou non | diagnostic (build trop lent) |

**Score principal** = salles nettoyées + progression dans la salle où le bot est mort (vagues passées et part
des ennemis tués, entre 0 et 1). C'est un nombre continu entre 0 et 5.

Le score a un plancher et un plafond : un build très fort bute sur 5, un très faible sur 0, et l'apport d'un
upgrade y est écrasé. Le calibrage doit garder peu de parties à 0 ou à 5.

### Le plan statistique : builds aléatoires, pistes fixes, régression

- Chaque partie tire un **nouveau** build de 10 upgrades et une piste au hasard.
- Une régression linéaire sur toutes les parties : une variable 0/1 par upgrade, une variable 0/1 par piste.
  Le coefficient d'un upgrade est son apport moyen au score, toutes choses égales par ailleurs. Les variables
  de piste absorbent la difficulté propre à chaque suite de salles.
- Intervalles de confiance par bootstrap. Deux upgrades dont les intervalles se chevauchent ne sont pas
  départagés.

Ce que ça mesure : la valeur **marginale** moyenne d'un upgrade dans un build de 10. Un troisième bonus de
dégâts y vaut moins qu'un premier ; c'est voulu, c'est sa valeur réelle à cette profondeur.

**Pourquoi pas le leave-one-out** (jouer le build complet, puis 10 fois sans un de ses upgrades, et comparer).
C'était le premier plan ; le calcul le condamne. Une partie « sans X » n'y sert qu'à mesurer X, alors qu'une
partie d'un build aléatoire renseigne sur les 198 upgrades à la fois (10 présents, 188 absents). À coût égal
(11 000 parties) :

| Plan | Mesures par upgrade | Erreur type |
|---|---|---|
| Leave-one-out | 51 écarts appariés | 0,20 × le bruit propre au combat |
| Régression avec pistes | 555 parties avec, 10 445 sans | 0,044 × le bruit propre au combat |

La régression est 4,6 fois plus précise, soit 21 fois moins de parties pour la même précision. Même sans les
pistes, le leave-one-out ne gagnerait que si le hasard du combat (hors difficulté de la salle) pesait moins de
5 % du bruit total, ce qui est invraisemblable pour un combat aussi chaotique.

**Les témoins.** Deux sortes, qui doivent toutes sortir à 50 :

- les vrais : les upgrades étiquetés « témoin » (par exemple FOUR LEAF, INTEREST, LOCKSMITH : bonus et malus
  purement économiques). Il n'y en a qu'une poignée ;
- les fantômes : une vingtaine de colonnes ajoutées à la régression, tirées au hasard avec la même fréquence
  qu'un upgrade et sans aucun effet dans le jeu.

L'écart de leurs notes autour de 50 donne la précision réelle de la méthode, indépendamment des intervalles
calculés. Un vrai témoin nettement hors de 50 alors que les fantômes y sont signale un effet de combat oublié.

**Les faux extrêmes.** Sur 198 upgrades, une dizaine sortiront « significatifs » par pur hasard, et les
premiers et derniers du classement sont en moyenne surestimés. D'où la deuxième passe, sur de nouvelles
parties, dont les résultats remplacent ceux de la première pour ces upgrades au lieu de s'y ajouter.

### La note

- **Échelle fixe**, pas relative au lot : 50 = neutre, 100 = l'upgrade ajoute N salles ou plus, 1 = il en
  retire N ou plus (N fixé après le pilote).
- L'échelle n'a de sens que pour un scénario donné. Le scénario (profondeur, PV, pistes) est figé avec elle ;
  le recalibrer ouvre une nouvelle série de rapports, non comparable à l'ancienne.
- Des tiers S à D par-dessus, pour la lecture.
- Deux sous-notes, offense et défense, sur le même principe.
- Un drapeau « à vérifier côté bot » quand la note est très basse ou le taux de tirs qui touchent s'effondre.

## Déroulé

1. **Calibrage** : régler les PV de départ pour que le build moyen nettoie environ 2 salles sur 5, avec peu de
   parties à 0 ou à 5. La profondeur 25 n'est pas sacrée ; si le calibrage demande trop de PV, on descend la
   profondeur.
2. **Pilote** : 2 000 parties (environ 2 minutes). Il sert à mesurer la part du bruit due aux pistes et celle
   due au combat, à regarder les témoins, à fixer l'échelle de la note et la taille de la passe complète.
3. **Passe complète** : 10 000 à 20 000 parties, 10 à 20 minutes sur 8 cœurs. Chaque upgrade est présent dans
   500 à 1 000 d'entre elles.
4. **Deuxième passe ciblée**, sur de nouvelles parties :
   - les 20 premiers et les 20 derniers, tirés plus souvent dans les builds, pour confirmer les extrêmes ;
   - les paires de la table `SYNERGIES` : la paire ensemble contre chaque upgrade seul.
5. **Lecture** : pour chaque note extrême, d'abord le drapeau bot, ensuite l'upgrade.

## Le rapport

Un tableau trié par note :

| Colonne | Contenu |
|---|---|
| Upgrade | son nom |
| Note | 1 à 100, 50 = neutre |
| Tier | S à D |
| Apport | en salles, avec son intervalle de confiance |
| Offense / Défense | les deux sous-notes |
| Mesures | nombre de parties où il était dans le build |
| Étiquettes | bonus hors combat, témoin, exclusif Baphomet |
| Drapeaux | bot à vérifier, intervalle trop large |

Plus la dispersion des témoins et les résultats des synergies.

Les rapports sont gardés (un fichier par passe). Deux passes sur le même scénario figé se comparent note à
note : un écart plus grand que les intervalles de confiance est un vrai changement.

## Ce qu'il faut coder

- Le mode `--bench` du headless : build figé, scénario ci-dessus, pistes re-semées, sortie CSV.
- La table des étiquettes.
- Un script qui lance la passe en parallèle par tranches (pas une CTest : trop long).
- Un script d'analyse : régression, bootstrap, témoins, rapport.
- Un test rapide qui vérifie que le mode tourne et que deux builds différents sur la même piste voient bien
  les mêmes salles.

## À quoi serviront les notes

- Retoucher les upgrades négatifs ou écrasants (bonus, malus).
- Revoir les 24 upgrades de départ (`START_UPGRADES`) et l'ordre des déblocages : simples et corrects au
  début, les plus forts plus tard.
- Vérifier que les exclusifs du Baphomet valent leur prix en cœurs (un tier 3 doit nettement dépasser un
  tier 1).
- Repérer les trous du bot : chaque drapeau « à vérifier côté bot » est une amélioration possible, qui
  profite aussi aux simulations de la suite de tests.

## Limites à garder en tête

- **La précision n'est pas connue avant le pilote.** Elle dépend du bruit propre au combat, jamais mesuré. Si
  un combat varie d'environ une salle d'une fois sur l'autre, 20 000 parties séparent un upgrade qui vaut
  0,1 salle, pas un qui en vaut 0,03 : le benchmark trouvera les upgrades nettement trop forts ou trop faibles,
  pas un classement fin des 198.
- La note est celle du bot. Elle guide la décision, elle ne la prend pas : un upgrade amusant et moyen n'a pas
  à être « corrigé ».
- La régression suppose que les apports s'additionnent. Un upgrade qui ne vaut que par ses combinaisons est
  résumé par sa moyenne ; seules les synergies testées à part échappent à ça.
- Sans soin entre les salles, le score récompense surtout de ne pas se faire toucher. L'attaque n'y compte
  qu'indirectement (tuer vite, c'est moins de tirs à esquiver) ; la sous-note offense est là pour ça.
- Un seul vaisseau (NORMAL), une seule profondeur. Un upgrade peut valoir plus tôt dans la run, ou avec
  l'épée du Knight.
- Le bonus des upgrades « hors combat » reste sans mesure tant que le benchmark de runs complètes n'existe pas.

## Extensions possibles

- **Benchmark d'économie** : des runs complètes depuis la salle 1, score = profondeur atteinte, pour mesurer
  le bonus des upgrades d'économie et d'utilité sur le même principe.
- **Autres vaisseaux** : la même passe avec `--char` pour le Shotgunner et le Knight.
- **Autres profondeurs** : une passe à la profondeur 8 avec 3 ou 4 upgrades, pour la valeur en début de run.
- **Actifs** : donner au bot un jeu d'actifs standard, pour noter les upgrades qui les améliorent, puis les
  actifs eux-mêmes.
- **Modificateurs de salle** : la même méthode mesure la difficulté ajoutée par chaque modificateur.
