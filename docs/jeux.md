# Jeux — Échecs, Dames et Backgammon (X4 Pro)

Ce fork ajoute un menu **Jeux** à l'écran d'accueil de CrossPoint Reader, avec trois jeux exploitant l'écran tactile du Xteink X4 Pro.

## Menu Jeux

Six entrées :

| Entrée | Description |
|---|---|
| Échecs - 2 joueurs | Deux joueurs à tour de rôle sur le même appareil |
| Échecs - contre l'IA | Vous jouez les blancs, l'IA joue les noirs |
| Dames - 2 joueurs | Dames internationales 10×10, deux joueurs |
| Dames - contre l'IA | Vous jouez les blancs, l'IA joue les noirs |
| Backgammon - 2 joueurs | Backgammon classique, deux joueurs |
| Backgammon - contre l'IA | Vous jouez les blancs, l'IA joue les noirs |

## Commandes

- **Tactile** : touchez une pièce pour la sélectionner — les cases de destination légales sont marquées d'un point — puis touchez la destination.
- **Boutons avant** : Gauche/Droite/Haut/Bas déplacent le curseur, **Confirmer** sélectionne puis joue.
- **Retour court** : désélectionne la pièce ; sans sélection, quitte vers le menu Jeux. Pendant une rafle obligatoire ou un tour IA, il permet de quitter et de reprendre ensuite.
- **Nouvelle partie** : touchez le bouton dédié ou maintenez **Retour pendant 700 ms**. Confirmez avec **Confirmer** ou le bouton tactile de confirmation ; **Retour** ou **Annuler** conserve la partie. Ce fonctionnement reste le même après une victoire : un appui quelconque ne recommence plus la partie.

## Reprendre une partie

Chaque entrée du menu garde sa propre partie : échecs, dames et backgammon, chacun en mode deux joueurs ou contre l'IA. Quittez avec Retour, puis rouvrez la même entrée pour reprendre. La sortie du jeu et la préparation de la mise en veille sauvegardent les changements sur la carte SD.

La reprise conserve le joueur, les coups déjà joués et les tours en cours. Au backgammon, les dés déjà lancés et ceux qui restent à jouer sont conservés. Aux dames, une rafle interrompue reprend avec le même pion et ses prises restantes. Un tour IA reprend à son étape sauvegardée.

La carte SD doit être accessible. Sans sauvegarde exploitable, le jeu ouvre une nouvelle partie. Aux échecs, une promotion encore non confirmée est annulée à la reprise ; le déplacement correspondant reste à jouer.

## Échecs

- **Pièces dessinées** : les pièces utilisent de véritables silhouettes standard (roi, dame, tour, fou, cavalier, pion), blanches ou noires, et non de simples jetons.
- Règles complètes : roque (petit et grand), prise en passant, promotion.
- **Promotion au choix** : quand un pion atteint la dernière rangée, un menu s'affiche pour choisir la pièce — **dame, cavalier, tour ou fou** (naviguez avec Gauche/Droite ou touchez directement la pièce, puis Confirmer ; Retour annule).
- Seuls les coups légaux sont jouables : impossible de laisser son roi en échec, de roquer en passant par une case attaquée, etc.
- États affichés dans l'en-tête : trait (blancs/noirs), **Échec !**, **Échec et mat !**, **Pat - partie nulle**.
- **IA renforcée** : recherche alpha-bêta en profondeur 3 avec tables positionnelles (piece-square tables), tri des coups MVV-LVA (captures et promotions en priorité), détection de mat/pat et une pointe d'aléatoire à égalité pour varier les parties.

## Dames (internationales, 10×10)

- 20 pièces par camp sur les cases sombres.
- **Prise obligatoire** : si une prise existe, seules les prises sont jouables (« Prise obligatoire » s'affiche).
- **Prise majoritaire** : quand plusieurs prises sont possibles, celle qui capture le plus de pièces est imposée.
- **Rafles au choix** : après sélection du pion, les points marquent la prochaine arrivée et les carrés les destinations finales. Choisissez un point pour avancer prise par prise ou une destination finale pour jouer toute la suite.
- Si une case est à la fois une prochaine arrivée et une arrivée finale, un appui court joue une seule étape. Maintenez **Confirmer pendant 700 ms**, avec le curseur sur la destination finale, ou faites un appui tactile long sur cette case pour jouer toute la suite. Un appui long sur le pion sélectionné termine aussi une suite proposée ; choisissez la destination finale précise pour imposer un branchement.
- Pendant la rafle, seul le pion engagé peut continuer. Les deux façons de jouer respectent la prise obligatoire et le maximum de prises du tour.
- **Dames volantes** : une dame se déplace et capture à n'importe quelle distance en diagonale.
- Un pion atteignant la dernière rangée est promu dame — le jeton affiche alors une **couronne** ; une prise qui couronne s'arrête là.
- États affichés : trait, **Prise obligatoire**, victoire des blancs/des noirs.
- **IA** : recherche alpha-bêta en profondeur 6 avec évaluation du matériel et de la position. Chaque déplacement, y compris chaque prise d’une rafle, est affiché séparément. Une pause de **800 ms après le rendu** laisse lire l’étape ; la dernière arrivée reste affichée avant le changement de joueur. Retour et Nouvelle partie restent accessibles entre les étapes.

## Backgammon

- Règles complètes : dés (le doublet se joue 4 fois), **pion battu envoyé sur la barre**, **rentrée obligatoire depuis la barre** avant tout autre coup, **sortie des pions** (bearing off) avec règle du dé supérieur.
- Le jeu impose le maximum de dés jouables. Si les deux dés peuvent chacun être joués, mais qu’un seul peut être utilisé dans le tour, le **plus fort est imposé**. Si seul le plus petit est légal, il reste jouable.
- **Commandes** : touchez l'écran (ou Confirmer) pour lancer les dés ; touchez un de vos pions pour le sélectionner — les destinations légales sont marquées d'un point — puis touchez la destination. Pour sortir un pion, sélectionnez-le puis touchez la barre centrale de votre côté.
- **Déplacement cumulé** : sélectionnez un pion puis une destination plus éloignée proposée pour utiliser plusieurs dés en une action, jusqu'aux quatre mouvements d'un doublet. Chaque étape intermédiaire doit être légale : un point bloqué ne peut pas être franchi en additionnant les dés. Les obligations de rentrée et le maximum de dés jouables restent appliqués. Vous pouvez aussi avancer dé par dé.
- **Dés lisibles** : le lancer complet reste affiché avec une bordure noire renforcée. Les dés consommés gardent leurs points, avec fond atténué et soulignement pour les distinguer des dés disponibles.
- Les pions sortis sont comptés au centre (blancs en bas, noirs en haut) ; les pions sur la barre sont affichés sur la colonne centrale.
- « Aucun coup possible » s'affiche quand les dés ne permettent aucun mouvement : le tour passe automatiquement.
- **IA** : elle choisit une séquence légale, affiche d’abord son lancer, puis déplace les pions un par un. Une pause de **800 ms après chaque rendu** laisse lire le plateau et les dés consommés. Le lancer reste affiché pendant la pause qui suit le dernier déplacement. Retour et Nouvelle partie restent accessibles entre les étapes.

## Stockage et maintenance

Les six sauvegardes sont stockées sous `/.crosspoint/` :

| Jeu | Deux joueurs | Contre l'IA |
| --- | --- | --- |
| Échecs | `game-chess-local.bin` | `game-chess-ai.bin` |
| Dames | `game-checkers-local.bin` | `game-checkers-ai.bin` |
| Backgammon | `game-backgammon-local.bin` | `game-backgammon-ai.bin` |

Chaque fichier possède un temporaire `.tmp` et une copie précédente `.bak`. Le chargement cherche un temporaire complet valide, puis le fichier principal valide, puis la copie précédente. Un temporaire restant contient la dernière écriture préparée : il est récupérable si son installation a échoué, puis installé avant la sauvegarde suivante. Une somme de contrôle détecte les fichiers incomplets ou altérés. Les écritures passent par `HalStorage` et évitent de réécrire un état inchangé. Une carte défaillante ou une coupure électrique pendant une écriture peut encore faire perdre le dernier état ; ce journal ne garantit pas une écriture atomique sur FAT.

Le plateau seul ne suffit pas à reprendre un tour. La sauvegarde conserve aussi sa phase et son parcours : sans cela, revenir au backgammon pourrait relancer les dés ou rejouer un déplacement IA. Aux dames, elle garde le plateau de début de tour et le préfixe de la rafle ; les suites proposées restent ainsi conformes au maximum de prises initial.

## Vérification sur appareil

Les vérifications logicielles passent : **495 tests hôte**, dont **45 scénarios jeux** avec contrôle tactile en portrait et paysage. Les 45 scénarios passent aussi sous ASan/UBSan sans diagnostic. La compilation X4 Pro réussit ; la revue de non-régression finale n’a relevé aucun problème dans le périmètre contrôlé.

Le binaire `firmware-x4pro-jeux.bin` a été remplacé sur la [release v1.6.5-games.3](https://github.com/ronylicha/crosspoint-reader/releases/tag/v1.6.5-games.3), puis téléchargé à nouveau et comparé au fichier local : ils sont identiques. Taille : **5 872 144 octets**. SHA-256 : `484aec821cd308985e1da57de2b393d7c5567949ce626ca850adc14b13ef4b3f`.

Aucun essai physique ni mesure de mémoire disponible pendant le jeu n’a été réalisé. Les contrôles suivants restent à faire sur l’appareil :

1. Jouez quelques coups dans chacune des six entrées, quittez et rouvrez la même entrée.
2. Reprenez après une mise en veille, au milieu d’une rafle et après consommation partielle des dés.
3. Ouvrez Nouvelle partie, annulez, puis recommencez en confirmant.
4. Aux dames, comparez une rafle par étapes avec la même rafle jouée jusqu’au bout ; vérifiez aussi une case qui représente les deux destinations.
5. Au backgammon, essayez un déplacement cumulé dégagé, puis un parcours avec point intermédiaire bloqué et un doublet.
6. Contre l’IA, vérifiez les déplacements séparés, les dés visibles et la possibilité de quitter entre deux étapes.
7. Vérifiez les boutons, les zones tactiles et le contraste dans les quatre orientations. Le rythme et la lisibilité sur E-Ink restent à confirmer sur l’appareil.
