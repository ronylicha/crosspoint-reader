# CrossPoint Reader + Jeux — fork Xteink X4 Pro

> Fork communautaire de [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader) qui ajoute un **menu Jeux** avec **Échecs**, **Dames** et **Backgammon** (chacun en 2 joueurs ou contre l'IA), dédié au **Xteink X4 Pro** (ESP32-S3, écran tactile).

**Version actuelle : 1.6.5-games.3** — basée sur CrossPoint Reader officiel **1.6.5**.

---

## Politique de versionnement

Ce fork suit le versionning officiel de CrossPoint Reader :

- Le numéro de version reprend celui de la release officielle sur laquelle le fork est basé (actuellement **1.6.5**).
- Un suffixe `-games.N` identifie l'itération du fork sur cette base (ex. `1.6.5-games.1`).
- À chaque nouvelle release officielle de CrossPoint, le fork est resynchronisé (merge de l'upstream) puis une nouvelle release est publiée : `1.6.6-games.1`, etc.

### Automatisation

Un workflow GitHub Actions (`.github/workflows/sync-upstream.yml`, actif sur la branche `develop`) automatise la resynchronisation :

1. Chaque jour, il vérifie si une nouvelle release officielle de CrossPoint est sortie.
2. Si oui : merge de l'upstream dans `games-x4pro`, compilation du firmware X4 Pro, puis publication automatique de la release `v<version>-games.1` avec `firmware-x4pro-jeux.bin` en pièce jointe.
3. En cas de conflit de merge, il ouvre automatiquement un ticket sur ce dépôt avec les instructions de résolution manuelle.

Le workflow est actif sur la branche par défaut `develop`. Un déclenchement manuel est possible via l'onglet **Actions** → *Run workflow* (avec option `force_version` pour viser une version précise).

> Note : GitHub désactive les workflows planifiés après 60 jours d'inactivité du dépôt ; un simple commit ou déclenchement manuel les réactive.

## Qu'est-ce que ce fork ajoute ?

Tout le firmware CrossPoint 1.6.5 officiel, **plus** :

- **Un élément « Jeux »** dans le menu d'accueil (entre « Transfert de fichiers » et « Réglages »).
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
- **Interface tactile** adaptée au X4 Pro : touchez une pièce puis sa destination, ou utilisez les boutons (curseur + Confirmer). Bouton Retour pour désélectionner / quitter.
- **Traductions** française et anglaise de toute l'interface des jeux.

Détail des règles et commandes : [docs/jeux.md](./docs/jeux.md).

## Fonctionnalités CrossPoint (base officielle 1.6.5)

- **Moteur de lecture** : rendu EPUB 2/3 avec option de style embarqué, gestion des images, césure, crénage, tableaux adaptatifs, annotations ruby CJK, navigation par chapitre, notes de bas de page, marque-pages, dictionnaire ([StarDict](docs/dictionary.md)), aller-à-%, tourne-page automatique, contrôle d'orientation, lecture focalisée, synchro de progression KOReader, et plus.
- **Formats** : `.epub`, `.xtc/.xtch`, `.txt`, `.bmp` en natif.
- **Lecture tactile** : suivi des liens EPUB et recherche au dictionnaire sur les appareils tactiles.
- **Polices personnalisées** sur carte SD.
- **Mode lecteur USB (X4 Pro)** : la carte SD vue comme stockage de masse USB.
- **Bibliothèque** : recherche indexée titre/auteur, vues récents/alphabétique, navigateur de dossiers, gestion du cache SD.
- **Sans fil** : interface web de transfert de fichiers, EPUB Optimizer, réglages web, WebDAV, mode AP (hotspot) et STA avec QR codes, connexion Calibre sans fil, navigateur OPDS (jusqu'à 8 serveurs), mises à jour OTA depuis les releases GitHub.
- **Personnalisation** : mode nuit, thèmes (Classic, Lyra, Lyra Extended, RoundedRaff), écrans de veille, remappage des boutons, barre d'état, etc.
- **Localisation** : 34 langues d'interface, dont CJK et RTL.

## Installation

> ⚠️ Ce firmware est prévu pour le **Xteink X4 Pro uniquement** (ESP32-S3). Ne flashez pas ce binaire sur un X3/X4 (ESP32-C3).

### Méthode 1 — Mise à jour par carte SD (la plus simple)

1. Téléchargez `firmware-x4pro-jeux.bin` depuis la [page Releases](https://github.com/ronylicha/crosspoint-reader/releases).
2. Renommez le fichier en **`firmware.bin`** et copiez-le **à la racine** de la carte SD.
3. Sur la liseuse : **Réglages → Mise à jour du firmware par carte SD**, sélectionnez le fichier.
4. N'éteignez pas l'appareil pendant la mise à jour. Il redémarre tout seul.

### Méthode 2 — Flasher web (USB)

1. Connectez le X4 Pro en USB-C, allumez-le.
2. Allez sur https://crosspointreader.com/#flash-tools, choisissez **Xteink X4Pro**, cliquez **« Custom .bin »** et envoyez le fichier téléchargé.

> Si votre appareil a été acheté sur une boutique tierce (AliExpress…) et n'apparaît pas dans le sélecteur série, il est peut-être verrouillé en USB : utilisez d'abord le [Xteink Unlocker](https://crosspointreader.com/#unlock-tool). Les appareils achetés sur xteink.com ne sont pas verrouillés.

### Méthode 3 — Ligne de commande (esptool)

```bash
pip install esptool
esptool.py --chip esp32s3 --port /dev/ttyACM0 --baud 921600 write_flash 0x10000 firmware-x4pro-jeux.bin
```

(Sous Linux, trouvez le port avec `dmesg` après connexion ; souvent `/dev/ttyACM0` ou `/dev/ttyUSB0`.)

### Revenir au firmware officiel

Flashez simplement la release officielle correspondante via https://crosspointreader.com/#flash-tools (web flasher ou esptool, même procédure).

## Utilisation des jeux

1. Depuis l'écran d'accueil, ouvrez **Jeux**.
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

# compiler pour le X4 Pro :
pio run -e x4pro
# le binaire est dans .pio/build/x4pro/firmware.bin

# flasher directement en USB :
pio run -e x4pro -t upload
```

## Documentation

- [Jeux : règles et commandes](./docs/jeux.md)
- [Guide utilisateur CrossPoint](./USER_GUIDE.md)
- [Serveur web](./docs/webserver.md) · [Endpoints](./docs/webserver-endpoints.md)
- [Polices sur carte SD](./docs/sd-card-fonts.md)
- [Dépannage](./docs/troubleshooting.md)
- [Contribution](./docs/contributing/README.md)

## Remerciements

Tout le mérite du firmware de base revient à la communauté [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader). Ce fork ne fait qu'y greffer des jeux pour le X4 Pro.

CrossPoint Reader n'est **affilié ni à Xteink ni à aucun fabricant**. Ce fork non plus.
