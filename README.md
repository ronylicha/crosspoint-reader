# CrossPoint Reader — fork Xteink X4 Pro, X4 et X3

> Fork communautaire de [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader) : **nouvelle interface à onglets**, **accueil épuré**, **statistiques de lecture**, **bibliothèque refondue** (étagères de couvertures, séries) et, sur le **Xteink X4 Pro** tactile, un **menu Jeux** avec **Échecs**, **Dames** et **Backgammon**.

**Version actuelle : 1.6.5-games.10** — basée sur CrossPoint Reader officiel **1.6.5**. Téléchargement : [release v1.6.5-games.10](https://github.com/ronylicha/crosspoint-reader/releases/tag/v1.6.5-games.10).

| Fichier | Liseuse |
|---|---|
| `firmware-x4pro-jeux.bin` | Xteink **X4 Pro** (ESP32-S3, tactile) — avec les jeux |
| `firmware-x4.bin` | Xteink **X4** et **X3** (ESP32-C3, boutons) — sans les jeux |

Les jeux se jouent au doigt : ils ne sont compilés que pour les liseuses tactiles. Les essais sur liseuse de cette version restent à effectuer.

---

## Politique de versionnement

Ce fork suit le versionning officiel de CrossPoint Reader :

- Le numéro de version reprend celui de la release officielle sur laquelle le fork est basé (actuellement **1.6.5**).
- Un suffixe `-games.N` identifie l'itération du fork sur cette base (ex. `1.6.5-games.1`).
- À chaque nouvelle release officielle de CrossPoint, le fork est resynchronisé (merge de l'upstream) puis une nouvelle release est publiée : `1.6.6-games.1`, etc.

### Automatisation

Un workflow GitHub Actions (`.github/workflows/sync-upstream.yml`, actif sur la branche `develop`) automatise la resynchronisation :

1. Chaque jour, il vérifie si une nouvelle release officielle de CrossPoint est sortie.
2. Si oui : merge de l'upstream dans `develop`, compilation des deux firmwares (`x4pro` avec les jeux, `gh_release` pour X4/X3 sans les jeux), puis publication de la release `v<version>-games.1` avec `firmware-x4pro-jeux.bin` et `firmware-x4.bin` en pièces jointes. La description de la release reprend le modèle `.github/release-notes-fork.md`.
3. Si la version officielle est déjà intégrée, il ne fait rien : la dernière base intégrée est relue depuis les tags `v*-games.*`.
4. En cas de conflit de merge ou d'échec de compilation, il ouvre un ticket sur ce dépôt avec les instructions de résolution manuelle.

Le workflow est actif sur la branche par défaut `develop`. Un déclenchement manuel est possible via l'onglet **Actions** → *Run workflow* (avec option `force_version` pour viser une version précise).

> Note : GitHub désactive les workflows planifiés après 60 jours d'inactivité du dépôt ; un simple commit ou déclenchement manuel les réactive.

## Qu'est-ce que ce fork ajoute ?

Tout le firmware CrossPoint 1.6.5 officiel, **plus** :

- **Une barre d'onglets à icônes** en haut de chaque écran principal : Accueil, Bibliothèque (sous-barre Bibliothèque | Fichiers, plus Catalogues si un serveur OPDS est configuré ou Plugins si des plugins sont installés), Jeux (écrans tactiles), Transfert de fichiers, Statistiques et Réglages (sous-barre Affichage | Lecteur | Commandes | Système). Elle remplace l'ancien menu d'accueil pour tous les thèmes.
- **Un accueil épuré, sans cadres** : les sections flottent sur la page, séparées par des filets pointillés. Couverture et progression du livre en cours ; à côté, les chiffres du jour précédés de leur icône (temps de lecture et pages avec la tendance par rapport à la veille, jours d'affilée, livres terminés) ; histogramme des 7 derniers jours ; puis deux autres livres récents, soit trois livres à l'écran en portrait.
- **Une pastille de tome** (**T1**, **T2**…) en haut à droite des couvertures de la bibliothèque et de celle du livre en cours à l'accueil, pour les livres d'une série.
- **Une synchronisation KOReader automatique** : dès qu'un compte est configuré, la position du livre est envoyée à la fermeture et à la mise en veille, sans écran d'attente. La position locale remplace celle du serveur. Sans réseau, les positions s'accumulent sur la carte SD (une par livre) et partent toutes à la connexion suivante. Sur X4 Pro l'envoi se fait en tâche de fond ; sur X4/X3, au moment de la mise en veille. Détails dans le [guide utilisateur](./USER_GUIDE.md#automatic-progress-upload).
- **Des statistiques de lecture** : onglet dédié avec le jour, la semaine, les totaux depuis le début et les livres lus récemment. La date et l'heure se règlent à la main dans les Réglages ; tant qu'elles sont inconnues, l'accueil et l'onglet Statistiques affichent un bouton **Régler la date**. Le détail du comptage figure dans le [guide utilisateur](./USER_GUIDE.md#39-reading-statistics).
- **Un onglet « Jeux »** (X4 Pro uniquement) avec une carte illustrée par jeu et deux boutons de mode (2 joueurs, contre l'IA).
- **Échecs** complets :
  - Mode 2 joueurs (sur le même écran) ou contre l'IA.
  - Véritables silhouettes de pièces standard (roi, dame, tour, fou, cavalier, pion).
  - Coups légaux complets : roque, prise en passant, promotion **au choix** (dame, cavalier, tour ou fou).
  - Détection d'échec, échec et mat, pat.
  - Indicateurs visuels des coups légaux après sélection d'une pièce.
  - IA renforcée : recherche alpha-bêta (profondeur 3) avec tables positionnelles, tri MVV-LVA et une part d'aléatoire à égalité.
- **Dames internationales (10×10)** :
  - Mode 2 joueurs ou contre l'IA.
  - Prise obligatoire avec **règle de la prise majoritaire**.
  - Rafles (prises en chaîne), dames volantes, promotion en dame marquée d'une **couronne**.
  - IA renforcée : recherche alpha-bêta (profondeur 6) avec évaluation positionnelle.
- **Backgammon** :
  - Mode 2 joueurs ou contre l'IA.
  - Règles complètes : doublets, barre, rentrée obligatoire, sortie des pions.
  - IA : énumération des séquences légales du jet et évaluation positionnelle.
- **Interface construite avec FreeInkUI** : sur le X4 Pro, chaque onglet, carte, ligne et bouton réagit au toucher ; sur X4/X3, les mêmes écrans se pilotent aux boutons. Aux jeux, touchez une pièce puis sa destination, ou utilisez le curseur + Confirmer. Retour désélectionne ou quitte.
- **Traductions** française et anglaise de toute l'interface des jeux.

Détail des règles et commandes : [docs/jeux.md](./docs/jeux.md).

### Bibliothèque sur étagères

La bibliothèque affiche jusqu'à **12 livres par page**, avec leurs couvertures sur **trois étagères de quatre livres**. Un cadre repère le livre sélectionné. Les onglets **Récents**, **Titre** et **Auteur** proposent chacun les deux sens de tri ; la recherche filtre les titres et les auteurs.

Le quatrième onglet **Séries** affiche des dossiers avec leur nom et leur nombre de livres, ainsi qu'un dossier **Sans série**. Ouvrez un dossier pour retrouver la grille 3×4, classée par numéro de tome croissant puis par titre ; les livres sans numéro viennent après les tomes numérotés. **Retour** restaure le dossier sélectionné avant d'effacer une éventuelle recherche. Le sens du tri de Séries concerne les dossiers ; les tomes gardent leur ordre de lecture.

Activez **Réglages → Système → Utiliser les métadonnées** pour lire les séries et tomes renseignés dans les EPUB. Après un transfert ou une modification de ces informations, reconstruisez l'index. À la première ouverture avec Séries, l'ancien index est mis à jour automatiquement en conservant l'ordre d'arrivée des livres déjà connus. Si les métadonnées sont désactivées, les livres apparaissent dans **Sans série** après reconstruction.

Un appui court ouvre le livre. Un appui long sur sa couverture, ou **Confirmer maintenu pendant une seconde**, ouvre une fiche avec les informations disponibles : titre, auteur, nom du fichier, chemin sur la carte SD, format, taille, série et tome. La fiche donne accès à l'ouverture, à la suppression avec confirmation, à la réindexation et au retrait des récents lorsque le livre figure dans l'historique. Les vignettes sont conservées sur la carte SD, et leur image décodée est rangée dans un fichier par taille de vignette : une page dont les couvertures sont prêtes s'affiche en une seule fois, et déplacer la sélection ne relit plus la carte. Sur X4 Pro, les couvertures vues restent en mémoire pendant la session et celles des pages voisines sont préchargées. Une couverture absente garde une vignette de remplacement. La construction de l'index et la préparation des couvertures manquantes affichent une barre de progression.

Commandes et essais sur appareil : [docs/bibliotheque.md](./docs/bibliotheque.md).

Validation de la version 1.6.5-games.10 : **590 tests hôte passent**. Les compilations `x4pro` et `gh_release` réussissent sans avertissement ; le firmware X4/X3 ne contient aucun symbole de jeu. Tailles et SHA-256 des binaires publiés :

- `firmware-x4pro-jeux.bin` : **5 935 008 octets**, `dfe057a3f722959cdb8b782838b8e0cc4545847639e25db0eafc89024c706476`
- `firmware-x4.bin` : **5 812 448 octets**, `e32e753fba286d5612a805c69ed2910fa7456152ebbc70718fe9ee1a97b0fd78`

## Fonctionnalités CrossPoint (base officielle 1.6.5)

- **Moteur de lecture** : rendu EPUB 2/3 avec option de style embarqué, gestion des images, césure, crénage, tableaux adaptatifs, annotations ruby CJK, navigation par chapitre, notes de bas de page, marque-pages, dictionnaire ([StarDict](docs/dictionary.md)), aller-à-%, tourne-page automatique, contrôle d'orientation, lecture focalisée, synchro de progression KOReader, et plus.
- **Formats** : `.epub`, `.xtc/.xtch`, `.txt`, `.bmp` en natif.
- **Lecture tactile** : suivi des liens EPUB et recherche au dictionnaire sur les appareils tactiles.
- **Polices personnalisées** sur carte SD.
- **Mode lecteur USB (X4 Pro)** : la carte SD vue comme stockage de masse USB.
- **Bibliothèque** : recherche par titre ou auteur, classement des livres, navigateur de dossiers et cache sur carte SD. La présentation sur étagères est décrite dans les [ajouts du fork](./docs/bibliotheque.md).
- **Sans fil** : interface web de transfert de fichiers, EPUB Optimizer, réglages web, WebDAV, mode AP (hotspot) et STA avec QR codes, connexion Calibre sans fil, navigateur OPDS (jusqu'à 8 serveurs), mises à jour OTA depuis les releases GitHub.
- **Personnalisation** : mode nuit, thèmes (Classic, Lyra, RoundedRaff ; les anciens choix Lyra Extended et Grille de couvertures, propres à l'accueil, basculent sur Lyra), écrans de veille, remappage des boutons, barre d'état, etc.
- **Localisation** : 34 langues d'interface, dont CJK et RTL.

## Installation

> ⚠️ Choisissez le binaire de votre liseuse : `firmware-x4pro-jeux.bin` pour le **X4 Pro** (ESP32-S3), `firmware-x4.bin` pour le **X4** et le **X3** (ESP32-C3). Les deux puces sont incompatibles : ne croisez pas les fichiers.

### Méthode 1 — Mise à jour par carte SD (la plus simple)

1. Téléchargez le fichier de votre liseuse depuis la [page Releases](https://github.com/ronylicha/crosspoint-reader/releases).
2. Renommez le fichier en **`firmware.bin`** et copiez-le **à la racine** de la carte SD.
3. Sur la liseuse : **Réglages → Mise à jour du firmware par carte SD**, sélectionnez le fichier.
4. N'éteignez pas l'appareil pendant la mise à jour. Il redémarre tout seul.

### Méthode 2 — Flasher web (USB)

1. Connectez la liseuse en USB-C, allumez-la.
2. Allez sur https://crosspointreader.com/#flash-tools, choisissez votre modèle (**Xteink X4Pro**, **X4** ou **X3**), cliquez **« Custom .bin »** et envoyez le fichier téléchargé.

> Si votre appareil a été acheté sur une boutique tierce (AliExpress…) et n'apparaît pas dans le sélecteur série, il est peut-être verrouillé en USB : utilisez d'abord le [Xteink Unlocker](https://crosspointreader.com/#unlock-tool). Les appareils achetés sur xteink.com ne sont pas verrouillés.

### Méthode 3 — Ligne de commande (esptool)

```bash
pip install esptool
# X4 Pro
esptool.py --chip esp32s3 --port /dev/ttyACM0 --baud 921600 write_flash 0x10000 firmware-x4pro-jeux.bin
# X4 / X3
esptool.py --chip esp32c3 --port /dev/ttyACM0 --baud 921600 write_flash 0x10000 firmware-x4.bin
```

(Sous Linux, trouvez le port avec `dmesg` après connexion ; souvent `/dev/ttyACM0` ou `/dev/ttyUSB0`.)

### Revenir au firmware officiel

Flashez simplement la release officielle correspondante via https://crosspointreader.com/#flash-tools (web flasher ou esptool, même procédure).

## Utilisation des jeux

Les jeux sont réservés au X4 Pro : ils se jouent au toucher et ne sont pas inclus dans `firmware-x4.bin`.

1. Touchez l'onglet **Jeux** de la barre du haut.
2. Choisissez parmi six entrées : **Échecs**, **Dames** ou **Backgammon**, chacun en mode **2 joueurs** ou **contre l'IA**.
3. En jeu : touchez une pièce pour la sélectionner — les destinations légales s'affichent — puis touchez la destination. Aux dames, si une prise est obligatoire, « Prise obligatoire » s'affiche et seules les prises sont jouables.
4. Au backgammon : touchez l'écran (ou Confirmer) pour lancer les dés, touchez un pion puis sa destination ; pour sortir un pion, touchez la barre centrale de votre côté après l'avoir sélectionné.
5. **Retour** : désélectionne la pièce, puis quitte la partie. Après une partie terminée (mat, pat ou victoire), **Confirmer** ou un appui relance une nouvelle partie.

Détails : [docs/jeux.md](./docs/jeux.md).

## Compiler soi-même

### Prérequis

- [pioarduino PlatformIO Core](https://github.com/pioarduino/platformio-core) ou [VS Code + pioarduino IDE](https://github.com/pioarduino/pioarduino-vscode-ide)
- Python 3.8+
- Câble USB-C data

### Build

```bash
git clone --recursive https://github.com/ronylicha/crosspoint-reader
cd crosspoint-reader

# si cloné sans --recursive :
git submodule update --init --recursive

# X4 Pro (tactile, avec les jeux) :
pio run -e x4pro
# le binaire est dans .pio/build/x4pro/firmware.bin

# X4 / X3 (boutons, sans les jeux) :
pio run -e gh_release
# le binaire est dans .pio/build/gh_release/firmware.bin

# flasher directement en USB :
pio run -e x4pro -t upload
```

Les jeux sont compilés quand `CROSSPOINT_GAMES` vaut 1, ce qui est le cas par défaut sur les cartes tactiles (`FREEINK_CAP_TOUCH`). Les sources ESP-IDF refusent un chemin contenant une espace : clonez le dépôt dans un dossier sans espace pour compiler la cible ESP32-C3.

## Documentation

- [Bibliothèque : étagères, recherche et fiche du livre](./docs/bibliotheque.md)
- [Statistiques de lecture et réglage de la date](./USER_GUIDE.md#39-reading-statistics)
- [Jeux : règles et commandes](./docs/jeux.md)
- [Guide utilisateur CrossPoint](./USER_GUIDE.md)
- [Serveur web](./docs/webserver.md) · [Endpoints](./docs/webserver-endpoints.md)
- [Polices sur carte SD](./docs/sd-card-fonts.md)
- [Dépannage](./docs/troubleshooting.md)
- [Contribution](./docs/contributing/README.md)

## Remerciements

Tout le mérite du firmware de base revient à la communauté [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader). Ce fork ajoute les jeux pour le X4 Pro et une présentation de la bibliothèque sur étagères.

CrossPoint Reader n'est **affilié ni à Xteink ni à aucun fabricant**. Ce fork non plus.
