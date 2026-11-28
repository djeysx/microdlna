# Analyse : version, noms, chemins périmés et page GitHub

Constat au commit `0cfe0f2` (`git describe` : `1.1-12-g0cfe0f2`). Les numéros de
ligne se rapportent à cet état. Aucune correction n'est encore appliquée.

## 1. Version restée à « 1.0 »

`MICRODLNA_VERSION` est définie dans [upnphttp.h:60](../../upnphttp.h#L60), alors
que le dépôt a un tag `1.1`, suivi de 12 commits. La valeur apparaît dans
`--version`, dans les logs de démarrage et dans l'en-tête `Server` des réponses
HTTP et SSDP.

Correction recommandée :

1. Remplacer `"1.0"` par `"1.2"`, numéro proposé pour la prochaine version (à
   confirmer).
2. Si un changelog est créé, y nommer la section correspondante `1.2`.
3. Committer, puis poser le tag `1.2` sur ce commit.

Déduire la version du tag avec `git describe` est possible, mais déconseillé ici :
une compilation depuis une archive ou un clone sans tags n'aurait plus de numéro
correct.

## 2. Noms erronés

| Fichier | Actuel | Correction |
|---|---|---|
| [version.c:31](../../version.c#L31) | `minidlna - lightweight…` | `MicroDLNA - lightweight…` |
| [microdlna.pod:3](../../microdlna.pod#L3) | `microndla - …` | `microdlnad - …` |
| [microdlna.pod:7](../../microdlna.pod#L7) | `B<microndla>` | `B<microdlnad>` |

`microdlnad` est le nom réel du binaire et de la page de manuel.

Autres erreurs de la page de manuel ([microdlna.pod](../../microdlna.pod)) :

- `-v` est décrit comme un alias de `--log-level verbose`. Ce niveau n'existe pas,
  et `-v` active en réalité le niveau `error`.
- La liste des niveaux de log omet `off`.
- La ligne de syntaxe présente `-u` comme obligatoire, alors qu'il est facultatif.

## 3. Anciens chemins dans les commentaires

`docs/dev/` contient des notes de travail : ces renvois sont à supprimer plutôt
qu'à mettre à jour.

| Emplacement | Action |
|---|---|
| [tests/test_utils.c:3](../../tests/test_utils.c#L3) | garder `see Makefile`, retirer `docs/dev/TESTS.md` |
| [tests/test_upnphttp.c:6](../../tests/test_upnphttp.c#L6) | retirer la parenthèse `see …` |
| [tests/test_upnphttp.c:305](../../tests/test_upnphttp.c#L305) | retirer le renvoi, en gardant l'explication de ce qui doit changer |
| [tests/functional/ftest_http.c:363](../../tests/functional/ftest_http.c#L363) | retirer la parenthèse `see …` |
| [README.md:137](../../README.md#L137) | supprimer la phrase `See docs/dev/…` |

## 4. Sur GitHub

- **Description** (roue crantée de « About ») :
  `Lightweight, stateless DLNA/UPnP media server in C, with no database and no dependencies`
- **Mots-clés** (même endroit) : `dlna`, `upnp`, `media-server`, `raspberry-pi`, `c`
- **Release** : après le point 1 et un passage réussi de la CI, créer la release
  `1.2` (« Releases » → « Draft a new release »), avec les changements de la
  version en notes.
- **Image** : une photo de la liste des vidéos sur la TV, par exemple
  `docs/images/tv.jpg`, affichée sous l'accroche du README. Vérifier qu'elle ne
  montre pas de titres ou d'informations à garder privés.

## Ordre conseillé

1. Corrections des noms, de la page de manuel et des commentaires, dans un commit.
2. Refonte de la documentation.
3. Version, tag et release en dernier.

## Vérification

Après les points 1 à 3 :

- `make`, `make check`, `make check-functional`
- `build/microdlnad --version`
- `man ./build/microdlnad.8`
