Fork communautaire de CrossPoint Reader, basé sur la release officielle **${UPSTREAM_VERSION}**.

## Nouvelle interface
- **Barre d'onglets à icônes** en haut de chaque écran principal : Accueil, Bibliothèque, Jeux (écrans tactiles), Transfert de fichiers, Statistiques et Réglages. Elle remplace l'ancien menu d'accueil, pour tous les thèmes.
- **Sous-barres** : Bibliothèque | Fichiers (plus Catalogues OPDS ou Plugins selon la configuration), et les catégories Affichage | Lecteur | Commandes | Système dans les Réglages.
- **Accueil épuré, sans cadres** : les sections flottent sur la page, séparées par des filets pointillés. Couverture et progression du livre en cours ; à côté, les chiffres du jour avec leurs icônes (temps de lecture, pages, jours d'affilée, livres terminés) et la tendance par rapport à la veille ; histogramme des 7 derniers jours ; deux autres livres récents, soit trois livres à l'écran.
- Interface entièrement construite avec FreeInkUI : le tactile du X4 Pro et les boutons passent par le même système.
- Les anciens thèmes « Lyra Extended » et « Grille de couvertures », propres à l'ancien accueil, basculent automatiquement sur Lyra.

## Statistiques de lecture
- Onglet **Statistiques** : aujourd'hui, 7 derniers jours, totaux depuis le début (temps, pages, chapitres, temps moyen par page, livres terminés) et livres lus récemment.
- Le temps est compté à chaque page tournée vers l'avant (de 2 s à 10 min par page). Les données sont enregistrées dans `/.crosspoint/reading_stats.json`.
- **Date et heure** réglables à la main (Réglages → Système → Horloge sur X4 Pro, Réglages → Système sur X4). Tant que la date est inconnue, l'accueil et l'onglet Statistiques proposent un bouton **Régler la date**.

## Bibliothèque refondue
- Couvertures sur **trois étagères de quatre livres**, jusqu'à 12 par page.
- Onglets **Récents, Titre, Auteur et Séries**, chacun dans les deux sens de tri, et recherche par titre ou auteur.
- **Séries** : dossiers virtuels avec leur nombre de livres, tomes classés par numéro ; les livres sans série sont réunis dans **Sans série**.
- Fiche détaillée par appui long : titre, auteur, série, tome, fichier, chemin, format, taille ; ouverture, suppression confirmée, réindexation.
- **Chargement visible** : la construction de l'index et la préparation des couvertures affichent une barre de progression, au lieu d'un écran figé.
- Libellés des onglets de tri en petite police : « Récents » et « Auteur » ne sont plus tronqués.

## Jeux (écrans tactiles uniquement)
- **Échecs**, **Dames internationales 10×10** et **Backgammon**, en 2 joueurs ou contre l'IA, avec reprise des parties.
- Les jeux ne sont inclus que dans le firmware des liseuses tactiles (X4 Pro).

## Fichiers
| Fichier | Liseuse |
|---|---|
| `firmware-x4pro-jeux.bin` | Xteink **X4 Pro** (ESP32-S3, tactile) — avec les jeux |
| `firmware-x4.bin` | Xteink **X4** et **X3** (ESP32-C3, boutons) — sans les jeux |

## Installation
1. Téléchargez le fichier correspondant à votre liseuse.
2. Renommez-le en `firmware.bin` et copiez-le à la racine de la carte SD.
3. Réglages → Système → Mise à jour du firmware par carte SD.

Documentation : [README](https://github.com/ronylicha/crosspoint-reader/blob/develop/README.md) · [Guide utilisateur](https://github.com/ronylicha/crosspoint-reader/blob/develop/USER_GUIDE.md) · [Bibliothèque](https://github.com/ronylicha/crosspoint-reader/blob/develop/docs/bibliotheque.md) · [Jeux](https://github.com/ronylicha/crosspoint-reader/blob/develop/docs/jeux.md)
