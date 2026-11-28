# Tests fonctionnels — MicroDLNA

Complète les [tests unitaires](TESTS.md) : ceux-ci ne lancent jamais le
vrai binaire (`microdlna.o` n'est jamais lié, `main()` n'existe pas dans
les binaires de test). Les tests fonctionnels lancent le vrai
`microdlnad` comme un processus séparé et lui parlent par de vraies
sockets (HTTP/SOAP en TCP, SSDP en UDP), pour vérifier le comportement de
bout en bout — y compris sous AddressSanitizer/UBSan et ThreadSanitizer en
conditions de trafic réel.

## Lancer les tests

```sh
make check-functional                            # serveur sous ASan/UBSan
make check-functional-tsan                       # serveur sous ThreadSanitizer
```

`check-functional` construit d'abord une copie de `microdlnad` compilée
avec `-fsanitize=address,undefined` (dans
`build/tests/functional/sanitized/`, jamais `build/microdlnad`), puis lance
chaque binaire `build/tests/functional/bin/ftest_*` contre elle.
`check-functional-tsan` fait de même avec `-fsanitize=thread` (dans
`build/tests/functional/tsan/` — ASan et TSan ne se combinent pas) et
`TSAN_OPTIONS="halt_on_error=1 exitcode=66"` : à la première data race, le
serveur s'arrête en erreur et le test en cours échoue. Les deux sont plus
lents que `make check` (vrais processus, vraies sockets) : volontairement
séparés, pas des pré-requis de `check`/`test`. La CI lance les trois suites
dans trois jobs distincts.

Pour déboguer un seul binaire, ou filtrer par nom de test :

```sh
MICRODLNAD_BIN=build/tests/functional/sanitized/microdlnad \
  build/tests/functional/bin/ftest_events renew
```

## Organisation

```
tests/functional/
├── support/
│   ├── process.h/.c      # spawn/attente-ready/arrêt de microdlnad
│   ├── httpclient.h/.c   # client HTTP/1.1 minimal ; http_read_message()
│   │                       lit aussi une *requête* entrante
│   ├── eventsink.h/.c    # serveur de callback GENA (reçoit les NOTIFY)
│   ├── netutil.h/.c      # allocation d'un port TCP libre
│   ├── fixtures.h/.c     # répertoire média temporaire par test
│   └── ssdpclient.h/.c   # client M-SEARCH routé via lo, collecte toutes
│                           les réponses
├── ftest_rootdesc.c      # descriptions UPnP
├── ftest_browse.c        # Browse : tri, filtrage, sous-dossiers,
│                           pagination, noms à échapper, URLs <res>
├── ftest_fileserve.c     # MediaItems + Range
├── ftest_http.c          # HEAD, icônes, Host, 501, POST, actions SOAP,
│                           en-têtes DLNA, sous-titres, erreurs MediaItems
├── ftest_events.c        # GENA SUBSCRIBE/renew/UNSUBSCRIBE/NOTIFY
├── ftest_config.c        # fichier -f et options CLI visibles/invalides
├── ftest_boundaries.c    # paramètres hors bornes
├── ftest_concurrency.c   # max_connections, requêtes concurrentes
├── ftest_lifecycle.c     # SIGTERM, SIGINT, SIGHUP, arrêt pendant transfert
└── ftest_ssdp.c          # M-SEARCH multicast via lo
```

Contrairement aux tests unitaires, ces binaires ne lient **aucun** `.o` de
microdlna : ils parlent uniquement au processus réel, lancé par
`mdlna_start()`. `tests/support/test_main.c` est réutilisé tel quel (même
objet, voir Makefile) ; `tests/support/stubs.c`/`helpers.c` ne le sont pas
(ils stubent `main()`/les globales, qui ne sont jamais linkés ici — la
raison d'être ne s'applique pas).

## Conventions

- **`mdlna_start()` lance toujours `-i lo`** (avant les arguments propres
  au test, qui peuvent le remplacer) : le serveur ne sert et n'accepte que
  sur la boucle locale, quelles que soient les interfaces réelles de la
  machine.
- **Chaque test qui ne tue pas volontairement le serveur se termine par
  `ASSERT_TRUE(mdlna_stop(&proc))`** : sortie avec le statut 0 après
  SIGTERM, dans le délai de grâce. Un rapport ASan/LSan/UBSan/TSan émis par
  le serveur (y compris à l'arrêt) fait ainsi échouer le test concerné.
- `ASSERT_*` ne déroule pas la pile : un test doit appeler `mdlna_stop()`
  sur tous ses chemins de sortie (un filet `atexit()` dans `process.c` tue
  les processus restants si un test l'oublie ou plante).

## SSDP multicast sans carte réseau réelle

`getifaddr.c` n'exclut `IFF_LOOPBACK` que si **aucune** interface n'est
demandée explicitement : avec `-i lo`, le socket de réception SSDP rejoint
le groupe multicast `239.255.255.250` directement sur `lo`, qui n'a pas de
filtrage matériel. Le client de test (`ssdpclient.c`) force sa sortie
multicast sur `lo` via `IP_MULTICAST_IF`, exactement comme le fait
microdlnad pour sa propre interface. Les réponses M-SEARCH partent en
unicast (`send_ssdp_response()`, `minissdp.c`), donc le
`IP_MULTICAST_LOOP=0` du socket `notify` du serveur (qui ne concerne que
ses propres `NOTIFY` périodiques) n'interfère pas. Fonctionne sans root et
sans carte réseau réelle — y compris en CI. Le format des NOTIFY eux-mêmes
est vérifié au niveau unitaire (`tests/test_minissdp.c`).

## Comportements caractérisés, non modifiés

Ces tests figent le comportement actuel pour qu'un refactoring ne le change
pas par accident ; ce ne sont pas des bugs mais des choix à rediscuter :

- **`BrowseFlag=BrowseMetadata` est ignoré** : le flag n'est jamais parsé
  (`xmlregex.c` ne connaît que ObjectID/ContainerID/StartingIndex/
  RequestedCount), une demande de métadonnées renvoie donc la liste des
  enfants. Fonctionnalité manquante.
- **Un lien symbolique placé dans `media_dir` expose sa cible**, même hors
  de `media_dir` : `serve_file()` suit les liens (`stat()`/`open()`).
  Pratique pour agréger des médias, mais à décider explicitement.
- **Fichier de configuration** : la valeur va jusqu'au `#` ou à la fin de
  ligne sans être tronquée à droite (`friendly_name=Salon TV # x` donne
  `"Salon TV "`), et des espaces autour du `=` rendent l'option inconnue
  (arrêt au démarrage) — conforme au format documenté `option=value`.

## Défauts de production trouvés et corrigés en écrivant ces tests

Chacun est prouvé par un test qui échouait sur le code d'avant (vérifié en
remettant temporairement l'ancien code de production en place).

- **Détection de la boucle locale cassée sur machines little-endian**
  (`microdlna.c`) : `clientname.sin_addr.s_addr != INADDR_LOOPBACK`
  comparait une adresse en network byte order à une constante en host byte
  order. Dès qu'une interface réelle était configurée, toute connexion HTTP
  depuis `127.0.0.1` était rejetée (`Rejected HTTP connection from
  127.0.0.1:...`). Corrigé avec `htonl(INADDR_LOOPBACK)`.

- **Port tronqué dans les URLs du listing Browse** (`upnpsoap.c`) :
  `snprintf(listening_port_str, 6, ":%d", ...)` coupait le dernier chiffre
  de tout port ≥ 10000 (`:15124` → `:1512`). Avec un tel port, aucune URL
  `<res>` annoncée n'était lisible par un client. Testé par
  `browse_into_a_subfolder_by_object_id` et
  `odd_file_names_are_escaped_and_their_urls_download`.

- **`transferMode.dlna.org: Interactive` inversé** (`serve_file()`,
  `upnphttp.c`) : le mode Interactive (celui des images) était refusé (406)
  pour les images et accepté pour les vidéos, l'inverse de l'intention
  affichée par le message de log et du code minidlna d'origine. Testé par
  `dlna_request_headers_are_validated`.

- **GENA inutilisable** : la réponse à SUBSCRIBE ne contenait jamais le
  `SID`, et une URL d'événement inconnue répondait 200 — détails dans
  [TESTS.md](TESTS.md). Testé de bout en bout par `ftest_events.c`.

- **SIGTERM/SIGINT pouvaient être ignorés jusqu'à `notify_interval`
  (895 s par défaut)** (`microdlna.c`) : le handler ne faisait que poser
  `quitting`, et la boucle comptait sur l'interruption de `select()` par le
  signal. Un signal reçu juste avant l'entrée dans `select()` laissait le
  processus bloqué jusqu'au timeout suivant. ThreadSanitizer rend cette
  fenêtre systématique, ce qui l'a révélée. Le handler SIGHUP, lui,
  appelait `reload_ifaces()` (logs, `malloc`, sockets) depuis le contexte
  signal — non async-signal-safe, signalé par TSan. Corrigé par un
  « self-pipe » : les handlers posent un drapeau `sig_atomic_t` et écrivent
  un octet dans un pipe surveillé par `select()` ; l'arrêt et le
  rechargement se font dans la boucle principale. Testé par
  `ftest_lifecycle.c` sous `check-functional-tsan`.

- **Use-after-free possible à l'arrêt** (`microdlna.c`) : `main()`
  libérait `media_dir` et la table des interfaces pendant que des threads
  `serve_file()` détachés pouvaient encore les lire (data race signalée par
  TSan). L'arrêt attend désormais au plus 1 s la fin des transferts
  (`wait_for_threads()`, `threads.c`) et ne libère ces globales que si plus
  aucun thread ne tourne.
