# Bibliothèque : couvertures sur étagères

La bibliothèque sur étagères et l'onglet **Séries** sont disponibles dans le [firmware X4 Pro de v1.6.5-games.3](https://github.com/ronylicha/crosspoint-reader/releases/tag/v1.6.5-games.3). L'asset `firmware-x4pro-jeux.bin` a été mis à jour depuis le [commit source `f537c15c`](https://github.com/ronylicha/crosspoint-reader/commit/f537c15c1168c50767e824873a45fdba5ea0a9f9), sans changer de version. Les essais sur liseuse restent à effectuer.

La bibliothèque affiche les livres sous forme de couvertures posées sur trois étagères. Chaque page contient jusqu'à **12 livres : quatre colonnes et trois rangées**. Les emplacements restants restent vides sur la dernière page. Un cadre indique le livre sélectionné.

## Retrouver un livre

Les quatre onglets permettent de retrouver les livres :

| Onglet | Classement |
| --- | --- |
| Récents | Livres récents ; l'historique de lecture apparaît en tête lorsque ce classement est affiché du plus récent au plus ancien, sans recherche active |
| Titre | Ordre alphabétique des titres |
| Auteur | Ordre alphabétique des auteurs |
| Séries | Dossiers virtuels par série, avec leur nom et leur nombre de livres |

Activez de nouveau l'onglet sélectionné pour inverser son ordre. La flèche de l'onglet indique le sens du tri. La recherche réduit la bibliothèque aux livres dont le titre ou l'auteur correspond au texte saisi. Par exemple, recherchez `verne` pour retrouver les livres de Jules Verne.

Pour retrouver toute la bibliothèque, effacez le texte dans la recherche. Un livre sans titre renseigné utilise son nom de fichier comme titre de remplacement ; un auteur manquant apparaît comme inconnu.

## Parcourir les séries

Ouvrez l'onglet **Séries**, puis choisissez un dossier. Les livres apparaissent sur les mêmes étagères de quatre colonnes et trois rangées. Les dossiers regroupent les livres d'une série, même si les fichiers sont répartis dans plusieurs dossiers de la carte SD. **Sans série** rassemble ceux dont aucune série n'est renseignée.

Dans une série, les livres avec un numéro de tome apparaissent par ordre numérique croissant : `3`, `3.5`, puis `10`, par exemple. Les livres sans numéro viennent ensuite. Le titre départage les livres au même numéro et ceux sans numéro. Inverser le tri de l'onglet **Séries** inverse l'ordre des dossiers ; les tomes restent dans l'ordre de lecture à l'intérieur.

La recherche porte sur le titre et l'auteur. Dans la liste des séries, elle conserve seulement les dossiers contenant un livre correspondant et leur compteur indique le nombre de résultats. Dans un dossier ouvert, elle réduit les livres affichés à ceux de cette série. **Retour** ramène d'abord à la liste des dossiers et restaure le dossier sélectionné, en conservant la recherche. Un nouvel appui sur **Retour** efface ensuite la recherche.

Les noms de série et les numéros de tome proviennent des informations enregistrées dans l'EPUB, notamment par Calibre ou dans les collections de type série d'un EPUB 3. Un nom de fichier ne suffit pas à définir une série. Activez **Réglages → Système → Utiliser les métadonnées**, puis reconstruisez l'index de la bibliothèque pour lire ces informations. Si ce réglage est désactivé, les livres se retrouvent dans **Sans série** après reconstruction.

Après avoir renseigné une série dans Calibre, transférez le fichier EPUB mis à jour sur la carte SD puis reconstruisez l'index. Faites de même après l'ajout ou le transfert de nouveaux tomes. À la première ouverture avec la nouvelle fonctionnalité, l'ancien index est mis à jour automatiquement ; l'ordre d'arrivée des livres déjà connus est conservé.

## Ouvrir un livre et consulter sa fiche

Sur une liseuse tactile, touchez une couverture pour ouvrir le livre. Avec les boutons, sélectionnez le livre puis appuyez brièvement sur **Confirmer**.

Un appui court sur les boutons de navigation parcourt les livres. Maintenez un bouton de navigation pour changer de page de douze livres. Sur un appareil tactile, balayez vers le haut ou le bas pour changer de page.

Maintenez le doigt sur une couverture, ou maintenez **Confirmer pendant une seconde** sur le livre sélectionné, pour ouvrir sa fiche. L'appui long consulte les informations du livre ; la suppression se choisit ensuite dans le menu.

La fiche présente les informations disponibles :

- titre et auteur ;
- nom du fichier et chemin sur la carte SD ;
- format du fichier et taille ;
- série et numéro de tome lorsqu'ils sont renseignés.

Le titre, l'auteur, la série et le tome proviennent des informations relevées dans le livre. Le format est identifié à partir du fichier. La taille correspond à celle relevée lors de la dernière mise à jour de la bibliothèque. Si une information manque, la fiche utilise les informations disponibles sur le fichier. Elle n'affiche pas d'ISBN, de résumé ou de langue qui n'ont pas été collectés.

Fermez la fiche avec **Retour** pour reprendre la navigation.

Le bouton **Actions du livre** est accessible dès la première page de la fiche. Si les informations occupent plusieurs pages, **Plus de détails** affiche la page suivante ; les actions restent accessibles sans avoir à parcourir toute la fiche. Le fond est redessiné avec chaque page pour effacer les contours de la fiche précédente.

## Actions sur un livre

| Action | Effet |
| --- | --- |
| Ouvrir | Ouvre le livre sélectionné |
| Retirer des récents | Retire le livre de l'historique de lecture, lorsqu'il en fait partie ; le fichier reste sur la carte SD |
| Supprimer | Demande une confirmation, puis supprime le fichier de la carte SD |
| Réindexer la bibliothèque | Relit la carte SD pour mettre à jour les livres et leurs informations |

Pour vérifier la différence entre retirer et supprimer, choisissez **Retirer des récents**, puis retrouvez le même livre dans l'onglet **Titre**. Utilisez **Supprimer** uniquement pour effacer le fichier ; **Annuler** conserve le livre.

## Couvertures et carte SD

Les vignettes de couverture sont conservées sur la carte SD. La bibliothèque prépare les vignettes manquantes l'une après l'autre. Un livre sans couverture exploitable garde une vignette de remplacement et reste accessible.

Après avoir ajouté, renommé ou déplacé des livres sur la carte SD, utilisez **Réindexer la bibliothèque**. Le résultat de la recherche et l'ordre affiché sont recalculés après cette mise à jour.

## Vérification sur liseuse

La suite complète passe avec **555 tests hôte**. Les **104 tests ciblés de format, lecture d'index, construction d'index, étagères et navigation** passent aussi sous ASan/UBSan. La revue ciblée n'a relevé aucun problème restant après validation de deux corrections. La compilation X4 Pro réussit sans avertissement en **285,938 secondes** ; les **659 fichiers de production** sont identiques au snapshot avant et après compilation. Aucun résultat matériel n'est acquis.

Le [firmware publié avec Séries](https://github.com/ronylicha/crosspoint-reader/releases/download/v1.6.5-games.3/firmware-x4pro-jeux.bin) compte **5 886 192 octets**. Son SHA-256 est `91c2ddb900278bd8c72ff9cff7770075a312c7d3c728cd7f5770d3866c400cc1`. Le téléchargement depuis l'URL publique a été comparé au build local : les fichiers sont identiques et le digest GitHub correspond. La release garde sa version ; ses notes précisent le commit source du binaire actualisé. Pour l'installation, suivez les [instructions du README](../README.md#installation).

Les essais suivants vérifient le rendu E-Ink et les commandes sur l'appareil :

1. Préparez une série d'au moins **13 tomes**, dont des numéros `3.5` et `10` et un livre sans numéro. Ajoutez une deuxième série, un livre sans série et un livre sans couverture. Activez les métadonnées et reconstruisez l'index.
2. Parcourez les livres avec les boutons, puis avec le tactile si l'appareil le permet. Vérifiez le cadre de sélection et l'ouverture du bon fichier.
3. Ouvrez **Séries** : vérifiez les noms, les compteurs et **Sans série**. Dans la série de 13 livres, vérifiez les quatre colonnes, trois rangées et la page suivante. Vérifiez l'ordre numérique `3.5` avant `10`, puis les livres sans numéro. Inversez l'ordre des dossiers : celui des tomes doit rester croissant.
4. Recherchez un titre puis un auteur : vérifiez les dossiers conservés et leurs compteurs. Entrez dans un dossier, puis appuyez sur **Retour** : le même dossier doit rester sélectionné avec la recherche active. Un nouvel appui sur Retour efface la recherche.
5. Ouvrez la fiche par un appui long. Vérifiez le titre, l'auteur, le fichier, le chemin, le format, la taille, la série et le tome. Relâchez l'appui : le livre doit rester dans la bibliothèque.
6. Choisissez **Supprimer**, puis annulez. Avec un livre de test, confirmez ensuite la suppression. Supprimez le dernier livre d'une série de test et vérifiez la disparition du dossier vide ainsi que le retour à une sélection accessible.
7. Retirez un livre des récents et retrouvez-le par son titre. Réindexez après le transfert d'un nouveau tome et vérifiez son dossier, son compteur et sa place. Désactivez les métadonnées puis reconstruisez : vérifiez **Sans série** ; réactivez et reconstruisez pour retrouver les groupes.
8. Répétez les essais en portrait et en paysage. Vérifiez que les couvertures, les titres et la fiche restent lisibles, sans éléments coupés par les marges de l'écran.
9. Parcourez plusieurs pages puis quittez et rouvrez la bibliothèque. Avec le moniteur série de développement, recherchez les erreurs de carte SD ou de mémoire et vérifiez que l'appareil reste stable.

Les temps de rafraîchissement, la lisibilité des couvertures et le confort de l'appui long doivent être jugés sur l'écran de la liseuse.
