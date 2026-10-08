# Jeux — Échecs et Dames (X4 Pro)

Ce fork ajoute un menu **Jeux** à l'écran d'accueil de CrossPoint Reader, avec deux jeux exploitant l'écran tactile du Xteink X4 Pro.

## Menu Jeux

Quatre entrées :

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
- **Retour** : désélectionne la pièce en cours ; un second appui quitte la partie et revient au menu Jeux.
- Après une partie terminée : **Confirmer** ou un appui sur l'écran lance une **nouvelle partie**.

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
- **Rafles** : les prises en chaîne se jouent en une fois (la destination finale est proposée directement).
- **Dames volantes** : une dame se déplace et capture à n'importe quelle distance en diagonale.
- Un pion atteignant la dernière rangée est promu dame — le jeton affiche alors une **couronne** ; une prise qui couronne s'arrête là.
- États affichés : trait, **Prise obligatoire**, victoire des blancs/des noirs.
- **IA renforcée** : recherche alpha-bêta en profondeur 6 (les captures sont explorées en priorité), évaluation prenant en compte le matériel, l'avancement des pions, la garde de la dernière rangée et les dames, avec choix aléatoire entre les coups équivalents.

## Backgammon

- Règles complètes : dés (le doublet se joue 4 fois), **pion battu envoyé sur la barre**, **rentrée obligatoire depuis la barre** avant tout autre coup, **sortie des pions** (bearing off) avec règle du dé supérieur.
- Si un seul des deux dés est jouable, le **dé le plus fort est imposé**, comme le veut la règle.
- **Commandes** : touchez l'écran (ou Confirmer) pour lancer les dés ; touchez un de vos pions pour le sélectionner — les destinations légales sont marquées d'un point — puis touchez la destination. Pour sortir un pion, sélectionnez-le puis touchez la barre centrale de votre côté.
- Les pions sortis sont comptés au centre (blancs en bas, noirs en haut) ; les pions sur la barre sont affichés sur la colonne centrale.
- « Aucun coup possible » s'affiche quand les dés ne permettent aucun mouvement : le tour passe automatiquement.
- **IA** : énumération exhaustive de toutes les séquences de coups légales pour le jet de dés, chaque position finale étant notée (pip count, pions isolés, points faits, barre, sorties) — l'IA joue la meilleure.
