# Bibliothèque : couvertures sur étagères

Cette refonte est disponible dans les sources locales. Elle n'annonce pas une nouvelle release téléchargeable. Les essais sur liseuse restent à effectuer.

La bibliothèque affiche les livres sous forme de couvertures posées sur trois étagères. Chaque page contient jusqu'à **12 livres : quatre colonnes et trois rangées**. Les emplacements restants restent vides sur la dernière page. Un cadre indique le livre sélectionné.

## Retrouver un livre

Les trois onglets changent l'ordre des livres :

| Onglet | Classement |
| --- | --- |
| Récents | Livres récents ; l'historique de lecture apparaît en tête lorsque ce classement est affiché du plus récent au plus ancien, sans recherche active |
| Titre | Ordre alphabétique des titres |
| Auteur | Ordre alphabétique des auteurs |

Activez de nouveau l'onglet sélectionné pour inverser son ordre. La flèche de l'onglet indique le sens du tri. La recherche réduit la bibliothèque aux livres dont le titre ou l'auteur correspond au texte saisi. Par exemple, recherchez `verne` pour retrouver les livres de Jules Verne.

Pour retrouver toute la bibliothèque, effacez le texte dans la recherche. Un livre sans titre renseigné utilise son nom de fichier comme titre de remplacement ; un auteur manquant apparaît comme inconnu.

## Ouvrir un livre et consulter sa fiche

Sur une liseuse tactile, touchez une couverture pour ouvrir le livre. Avec les boutons, sélectionnez le livre puis appuyez brièvement sur **Confirmer**.

Un appui court sur les boutons de navigation parcourt les livres. Maintenez un bouton de navigation pour changer de page de douze livres. Sur un appareil tactile, balayez vers le haut ou le bas pour changer de page.

Maintenez le doigt sur une couverture, ou maintenez **Confirmer pendant une seconde** sur le livre sélectionné, pour ouvrir sa fiche. L'appui long consulte les informations du livre ; la suppression se choisit ensuite dans le menu.

La fiche présente les informations disponibles :

- titre et auteur ;
- nom du fichier et chemin sur la carte SD ;
- format du fichier et taille.

Le titre et l'auteur proviennent des informations relevées dans le livre. Le format est identifié à partir du fichier. La taille correspond à celle relevée lors de la dernière mise à jour de la bibliothèque. Si une information manque, la fiche utilise les informations disponibles sur le fichier. Elle n'affiche pas d'ISBN, de résumé ou de langue qui n'ont pas été collectés.

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

La suite complète passe avec **514 tests hôte**, dont **19 tests ciblés de la bibliothèque**. Ces 19 tests passent aussi sous ASan/UBSan sans diagnostic. La revue des chemins de grille, cache, navigation et fiche n'a relevé aucun problème après correction du rendu des dialogues. La compilation finale du firmware **X4 Pro réussit** ; aucun résultat matériel n'est acquis.

Le binaire est disponible localement dans `build/library-x4pro/firmware-x4pro-bibliotheque.bin` : **5 876 768 octets**. Son SHA-256 est `5443966311d1be82b2b2959db7d6747326f7b5281aeeca4ed03b13a52609876c`. L'image ESP32-S3 passe les vérifications de checksum et de hash internes d'esptool. Ce binaire n'a pas été publié dans une release.

Les essais suivants vérifient le rendu E-Ink et les commandes sur l'appareil :

1. Ajoutez au moins 13 livres à la carte SD, dont un livre sans couverture et deux livres du même auteur. Ouvrez la bibliothèque : vérifiez les quatre colonnes, les trois rangées et la page suivante.
2. Parcourez les livres avec les boutons, puis avec le tactile si l'appareil le permet. Vérifiez le cadre de sélection et l'ouverture du bon fichier.
3. Testez chaque onglet dans les deux sens, puis recherchez un titre et un auteur. Effacez la recherche pour retrouver tous les livres.
4. Ouvrez la fiche par un appui long. Vérifiez le titre, l'auteur, le nom du fichier, son chemin, son format et sa taille. Relâchez l'appui : le livre doit rester dans la bibliothèque.
5. Choisissez **Supprimer**, puis annulez. Vérifiez que le fichier reste présent. Avec un livre de test, confirmez ensuite la suppression et vérifiez sa disparition.
6. Retirez un livre des récents et retrouvez-le par son titre. Réindexez après l'ajout d'un autre livre et vérifiez qu'il apparaît.
7. Répétez les essais en portrait et en paysage. Vérifiez que les couvertures, les titres et la fiche restent lisibles, sans éléments coupés par les marges de l'écran.
8. Parcourez plusieurs pages puis quittez et rouvrez la bibliothèque. Avec le moniteur série de développement, recherchez les erreurs de carte SD ou de mémoire et vérifiez que l'appareil reste stable.

Les temps de rafraîchissement, la lisibilité des couvertures et le confort de l'appui long doivent être jugés sur l'écran de la liseuse.
