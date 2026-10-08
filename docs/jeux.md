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

## Commandes

- **Tactile** : touchez une pièce pour la sélectionner — les cases de destination légales sont marquées d'un point — puis touchez la destination.
- **Boutons avant** : Gauche/Droite/Haut/Bas déplacent le curseur, **Confirmer** sélectionne puis joue.
- **Retour** : désélectionne la pièce en cours ; un second appui quitte la partie et revient au menu Jeux.
- Après une partie terminée : **Confirmer** ou un appui sur l'écran lance une **nouvelle partie**.

## Échecs

- Règles complètes : roque (petit et grand), prise en passant, promotion automatique en dame.
- Seuls les coups légaux sont jouables : impossible de laisser son roi en échec, de roquer en passant par une case attaquée, etc.
- États affichés dans l'en-tête : trait (blancs/noirs), **Échec !**, **Échec et mat !**, **Pat - partie nulle**.
- IA : recherche à 1 coup avec évaluation matérielle (pion 100, cavalier 320, fou 330, tour 500, dame 900), bonus de contrôle du centre et une pointe d'aléatoire pour varier les parties.

## Dames (internationales, 10×10)

- 20 pièces par camp sur les cases sombres.
- **Prise obligatoire** : si une prise existe, seules les prises sont jouables (« Prise obligatoire » s'affiche).
- **Prise majoritaire** : quand plusieurs prises sont possibles, celle qui capture le plus de pièces est imposée.
- **Rafles** : les prises en chaîne se jouent en une fois (la destination finale est proposée directement).
- **Dames volantes** : une dame se déplace et capture à n'importe quelle distance en diagonale.
- Un pion atteignant la dernière rangée est promu dame ; une prise qui couronne s'arrête là.
- États affichés : trait, **Prise obligatoire**, victoire des blancs/des noirs.
- IA : privilégie les prises majoritaires, la capture de dames, la promotion et l'avancement.

## Note technique

Les jeux sont des *activities* FreeInkUI standard (`src/activities/games/`), compilés pour la cible `x4pro`. L'aléatoire de l'IA utilise le générateur matériel ESP32 (`esp_random()`). L'affichage utilise le rafraîchissement rapide de l'écran e-ink, avec un rafraîchissement partiel périodique pour limiter les artefacts.
