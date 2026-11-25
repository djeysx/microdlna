# Tests unitaires — MicroDLNA

## Lancer les tests

```sh
./configure.sh && make      # build de production, inchangé
make check                  # compile et lance tous les tests unitaires
```

`make check` (ou son alias `make test`) est la seule commande à connaître :
compilation incrémentale, exécution de chaque binaire de test, résumé, code
de sortie non nul si un test échoue. AddressSanitizer, UndefinedBehaviorSanitizer
et LeakSanitizer sont actifs par défaut sur les tests — il n'y a aucune
variable à passer pour les activer ou les désactiver, et ils n'affectent
jamais le binaire `build/microdlnad` livré.

Pour déboguer un cas précis sans relancer toute la suite :

```sh
build/tests/bin/test_upnphttp            # un seul fichier de test
build/tests/bin/test_upnphttp host       # filtre : ne lance que les tests
                                          # dont le nom contient "host"
```

## Organisation

```
tests/
├── support/
│   ├── test.h          # macros ASSERT_*, TEST()
│   ├── test_main.c     # registre, runner, main(), filtre par nom
│   ├── stubs.c         # les 7 globales de microdlna.c + get_microdlna_version
│   ├── helpers.h/.c    # capture de struct stream, fd en mémoire, dechunk,
│   │                     arbre de répertoires temporaire
└── test_<module>.c     # un fichier par module de production testé
```

Un binaire est produit par fichier `tests/test_*.c`, dans
`build/tests/bin/`. La liste des modules testés est découverte par
`$(wildcard tests/test_*.c)` : **ajouter un fichier de test ne nécessite pas
de relancer `./configure.sh`**, seulement `make check`.

## Convention : accéder aux fonctions `static`

Beaucoup de fonctions intéressantes à tester sont `static` (`parse_http_header`,
`readline`, `get_srt_path`, `xml_escape_double`, `validate_uuid`...). Plutôt
que de les exposer dans un `.h` ou d'ajouter une macro `#ifdef TESTING`
(qui changerait le code de production pour les besoins des tests), chaque
fichier `tests/test_<module>.c` fait :

```c
#include "../<module>.c"
```

Le fichier de test devient alors le même translation unit que le module, et
a accès à tout. En contrepartie, le `<module>.o` de production est **exclu
du link** de ce binaire de test (sinon : symboles dupliqués) — c'est fait
automatiquement par la règle de link dans le `Makefile` généré, basée sur le
nom du fichier de test. **Zéro ligne modifiée dans le code de production.**

Exemple concret (`tests/test_mime.c`) :

```c
#include "../mime.c"     /* accès à get_mime_type_from_ext(), static */
#include "test.h"

TEST(get_mime_type_known_extension)
{
    const struct ext_info *m = get_mime_type("movie.mp4");
    ASSERT_NOT_NULL(m);
    ASSERT_INT_EQ(m->type, M_VIDEO);
}
```

## Pourquoi un harness maison plutôt que cmocka/Unity

Le projet n'a **aucune dépendance au-delà de la libc, pthreads et POSIX**
(voir `docs/ARCHITECTURE.md` §2 et §7). Un framework de test externe
casserait ce principe pour tout le monde (il faudrait l'installer pour
compiler le projet, même sans lancer les tests). Le harness ici fait ~150
lignes : `TEST(nom) { ... }` s'auto-enregistre via un constructeur GCC
(`__attribute__((constructor))`), déjà une pratique courante dans le code
existant (`stream.h`, `log.h`). Aucune installation requise.

## Contraintes à connaître avant d'ajouter un test

1. **`chdir_to_media_dir()` (`mediadir.c`) ne résout `media_dir` qu'une
   fois par processus** (`static int is_real`). Tous les tests de
   `test_dirlist.c` partagent donc une seule racine temporaire
   (`test_media_root()`, `tests/support/helpers.c`), avec un
   sous-répertoire dédié par cas de test. Elle est supprimée à la sortie du
   processus (`atexit`).

2. **`media_dir` est libéré par `chdir_to_media_dir()` à sa première
   résolution** (`free(media_dir)` dans `mediadir.c`). Il doit donc
   toujours pointer vers une chaîne allouée par `malloc`/`strdup`, jamais
   vers un buffer `static` ou un littéral.

3. **`get_interface_ip_str(-1)` renvoie `"127.0.0.1"`**
   (`getifaddr.c`) : mettre `h.iface = -1` dans un `struct upnphttp` de test
   évite d'avoir à construire une fausse interface réseau.

4. **`capture_text()` renvoie les octets bruts du format HTTP chunked**
   (`<hex>\r\n<data>\r\n...0\r\n\r\n`), pas le contenu "logique" — parce que
   c'est exactement ce que `chunk_print`/`chunk_print_end` écrivent, et que
   certains tests (`test_stream.c`) vérifient précisément ce format. Dès
   qu'une sortie peut dépasser 1024 octets (`BUFFER_SIZE`, `stream.h`) et
   qu'on veut en inspecter le contenu comme un tout (recherche de
   sous-chaîne sur plusieurs chunks, vérification d'équilibrage de balises
   XML...), passer par `dechunk()` (`tests/support/helpers.h`) avant de
   l'analyser — voir `tests/test_upnpdescgen.c`.

5. **Vérifier d'abord les lectures hors bornes avec un programme autonome.**
   Une fonction qui scrute une fenêtre fixe de caractères en partant de la
   fin d'une chaîne (ou qui avance un pointeur sur une longueur attendue
   sans vérifier le `'\0'`) peut lire au-delà du début ou de la fin d'une
   chaîne trop courte. Sous AddressSanitizer, ça fait planter le *binaire
   de test entier*, pas seulement échouer une assertion — donc avant
   d'ajouter un cas de test sur ce genre de fonction avec une entrée
   courte/vide, valider son comportement avec un petit programme autonome
   compilé en `-fsanitize=address` plutôt que de le découvrir via
   `make check`. Trois cas réels de ce type ont été trouvés et corrigés en
   écrivant cette suite (voir section suivante) ; c'est le genre de défaut
   à surveiller en en ajoutant d'autres.

## Défauts de production corrigés en écrivant ces tests

Trois défauts réels, confirmés avec AddressSanitizer sur un programme
autonome avant d'être corrigés dans le code de production :

- **`get_mime_type()` (`mime.c`) lisait jusqu'à 6 octets avant le début de
  la chaîne** pour tout nom de fichier de moins de 6 caractères sans point
  (la boucle vérifiait toujours exactement les 6 derniers octets, qu'ils
  existent ou non). Atteignable via un fichier très court sans extension
  dans le répertoire média (ex. un fichier nommé `a`). Corrigé en bornant
  la fenêtre à `max(0, len - 6)`. Testé par
  `get_mime_type_short_filename_without_dot_returns_null` dans
  `test_mime.c`.

- **`validate_uuid()` (`getifaddr.c`) lisait systématiquement 36 octets**,
  quelle que soit la longueur réelle de la chaîne passée, pour toute entrée
  plus courte qu'un UUID complet. Atteignable via l'option `--uuid` d'un
  utilisateur. Corrigé en renvoyant `0` dès qu'un `'\0'` est rencontré plus
  tôt que prévu, au lieu de continuer à avancer le pointeur. Testé par
  `validate_uuid_rejects_too_short_input`/`_rejects_empty_string` dans
  `test_getifaddr.c`.

- **`get_directory_listing()` (`dirlist.c`) pouvait renvoyer `NULL` pour un
  résultat valide mais vide** (`RequestedCount` à 0, ou `StartingIndex`
  au-delà du nombre total d'éléments), parce que `realloc(ptr, 0)` renvoie
  `NULL` sous glibc. Son unique appelant, `browse_content_directory()`
  (`upnpsoap.c`), traite tout retour `NULL` comme une erreur déjà signalée
  au client et fait `return;` sans envoyer de réponse HTTP — un client UPnP
  demandant une page au-delà de la fin restait donc sans réponse. Corrigé
  en gardant toujours au moins un slot alloué, comme le fait déjà le cas
  « répertoire vide » juste au-dessus dans la même fonction. Testé par
  `starting_index_past_the_end_returns_nothing` et
  `requested_count_zero_returns_an_empty_but_valid_result` dans
  `test_dirlist.c`.
