# Analyse : avance/retour rapide saccadés dans VLC (microdlna)

## Contexte
L'utilisateur regarde des vidéos servies par ce serveur DLNA (`microdlna`, C, serveur mono-processus basé sur `select()`) via VLC, et constate que le seek (avance/retour rapide) n'est pas fluide. Objectif : identifier la ou les causes et proposer des pistes d'amélioration.

Vérifications préalables : pas de transcodage (aucun appel ffmpeg/popen/exec), tous les médias sont servis tels quels depuis le disque via `sendfile(2)`. Le problème n'est donc pas lié à un pipeline de transcodage mais à la façon dont les requêtes HTTP/Range sont traitées.

## Flux actuel d'une requête de seek
1. VLC envoie une requête `GET /MediaItems/... HTTP/1.1` avec un en-tête `Range: bytes=X-` à chaque déplacement dans la timeline (souvent plusieurs requêtes rapprochées pendant un scrub).
2. `microdlna.c:688-724` (boucle `select()`) : un seul `accept()` par itération (`if`, pas `while`), puis `process_upnphttp_http_query()` est appelé **de façon synchrone dans le thread principal**.
3. `upnphttp.c:358-379` (`readline`) : parsing des en-têtes **octet par octet** (un `read()` par caractère) toujours dans le thread principal — donc tout accept()/traitement d'une autre connexion (SSDP ou HTTP) attend que ce parsing soit terminé.
4. `upnphttp.c:912` → un thread dédié est créé (`threads.c:43`) pour `serve_file()` qui fait `stat()` + `open()` + double `lseek()` (`upnphttp.c:734-760`) **à chaque requête**, même pour la même vidéo.
5. Réponse envoyée via `send_file()` (`sendfile.c:89-165`, `sendfile(2)` zero-copy avec repli read/write 64 KiB).
6. **`Connection: close` est codé en dur** dans les deux chemins d'en-têtes (`upnphttp.c:597` et `upnphttp.c:631`) → la socket TCP est fermée après chaque réponse.

## Causes identifiées (classées par impact probable)

1. **Pas de connexions HTTP persistantes (`Connection: close` systématique) — cause la plus probable.**
   Chaque seek dans VLC = une nouvelle connexion TCP complète (handshake SYN/SYN-ACK/ACK) qui redémarre en plus en **TCP slow-start** (fenêtre de congestion minimale). Le débit met donc plusieurs aller-retours réseau à remonter en pleine vitesse après *chaque* saut, ce qui se traduit par la sensation de saccade/lenteur juste après un seek, avant que ça ne redevienne fluide.
   → Fichiers concernés : `upnphttp.c:593-639` (en-têtes), `upnphttp.c:709-910` (`serve_file`, qui ferme le fd via `fclose` en fin de transfert).

2. **Parsing des en-têtes fait de façon synchrone et bloquante dans le thread unique de la boucle `select()`.**
   VLC/DLNA ouvrent souvent plusieurs connexions en parallèle pendant un scrub (requêtes de sondage sur plusieurs plages). Comme le parsing (octet par octet) de chaque requête se fait dans l'unique thread principal avant tout renvoi à `select()`, ces connexions parallèles sont **sérialisées** à ce stade, et un seul `accept()` est traité par itération de boucle (`microdlna.c:688`, backlog d'écoute de 6 seulement, `microdlna.c:169`).
   → Fichiers : `microdlna.c:688-724`, `upnphttp.c:383-589`.

3. **Pas de `TCP_NODELAY`** sur le socket client (seuls `SO_REUSEADDR`/`SO_RCVTIMEO` sont positionnés — `microdlna.c:153-154`, `upnphttp.c:393-394`). Combiné au point 1, l'algorithme de Nagle ajoute un délai supplémentaire à chaque nouvelle connexion avant que les petits paquets (en-têtes de réponse, débuts de segment) ne partent.

4. **`open()` + `stat()` + double `lseek()` refaits à chaque requête de plage** (`upnphttp.c:734-760`), même pour rejouer la même vidéo. Peu coûteux sur disque local rapide, mais s'ajoute à la latence de chaque seek, et serait plus pénalisant sur un stockage réseau/lent.

5. **Bug latent (pas directement lié au seek actuel mais à surveiller) :** l'indicateur `try_sendfile` dans `sendfile.c:95` est une variable **statique locale à la fonction** `send_file()` (pas une statique de fichier), donc une seule instance partagée par tous les appels/threads/connexions. Une seule erreur `sendfile()` (hors `EPIPE`) désactive **définitivement** le chemin zero-copy pour tout le processus, forçant un repli permanent sur read/write 64 Ko pour toutes les connexions suivantes.

6. Le serveur rejette explicitement `TimeSeekRange.dlna.org` (`upnphttp.c:526-534`) et n'annonce que `DLNA.ORG_OP=01` (seek par octet). Ce n'est pas un bug mais cela signifie que toute la précision du seek dépend de l'estimation d'octet faite par VLC — comportement normal, pas une piste d'amélioration ici.

## Recommandations (par ordre d'impact attendu)

1. **Supporter les connexions HTTP persistantes (keep-alive), de bout en bout et pas seulement pour le flux média.** C'est le changement qui aurait le plus d'impact : réutiliser la même connexion TCP pour les requêtes `Range` successives pendant un seek évite de repayer handshake + slow-start à chaque saut.

   Décision de conception retenue : plutôt que de limiter le keep-alive aux seules requêtes `GET`/`HEAD` sur `/MediaItems` et fermer la connexion pour tout autre type de requête suivante, le thread par connexion doit pouvoir traiter **n'importe quelle requête suivante** (SOAP, description XML, icônes, SUBSCRIBE/UNSUBSCRIBE) tant que le client ne ferme pas et ne dépasse pas le timeout. Trois chantiers en découlent :

   **a) Extraire le parsing de requête en fonction partagée (éviter la duplication).**
   Le parsing (ligne de requête + boucle d'en-têtes + `parse_http_header` + validations) n'existe aujourd'hui qu'à l'intérieur de `process_upnphttp_http_query()` (`upnphttp.c:383-534`), appelé une seule fois par connexion, dans le thread principal `select()`. Il faut extraire ce bloc en une fonction réutilisable (ex. `parse_http_request(h)` retournant succès/échec), appelée depuis le thread principal pour la première requête d'une connexion (comme aujourd'hui), et depuis la nouvelle boucle "par connexion" (voir c) pour chaque requête suivante.
   Pré-requis pour que la boucle sache distinguer une fermeture normale d'une erreur réseau entre deux requêtes keep-alive : `readline()` (`upnphttp.c:358-379`) doit renvoyer un code distinct pour "EOF avant tout octet lu" (le client a fermé proprement, premier `read()` de la ligne retourne 0) par rapport aux autres échecs (timeout `SO_RCVTIMEO`, erreur, `\r` sans `\n`, `limit` atteint) — aujourd'hui les deux cas renvoient `-1` sans distinction.

   **b) Reconstruire le cycle de vie de `h->st` à chaque requête de la boucle.**
   Pour le chemin fichier, le code détruit délibérément le flux bufferisé avant l'envoi zero-copy : `free(h->st); h->st = NULL; delete_upnphttp_struct(h); h = NULL;` (`upnphttp.c:892-896`), puis `send_file(fd, sendfh, start, end); fclose(fh);` (`upnphttp.c:899-900`) ferme la socket via `fclose(fh)`. Pour boucler, il faut distinguer trois objets dont le cycle de vie diverge : le wrapper `struct stream` (`h->st`), le `FILE*` sous-jacent (`fh`), et le fd socket brut (`fd`). Le wrapper `struct stream` doit être libéré et **reconstruit à chaque requête** de la boucle (que la connexion persiste ou non, pour écrire les en-têtes de la réponse suivante via `stream_printf(h->st, ...)`), tandis que le `fclose(fh)` (qui ferme en réalité le fd socket) doit devenir **conditionnel** : ne fermer que si la connexion ne persiste pas (`Connection: close` reçu — actuellement **non parsé côté requête**, à ajouter dans `parse_http_header` — timeout, ou erreur de parsing) ; sinon le fd reste ouvert pour la requête suivante.
   État "keep-alive demandé" : à stocker comme un nouveau bit dans le bitfield existant `reqflags` (`uint32_t`, déjà présent dans `struct upnphttp`) plutôt que d'ajouter un champ séparé.

   **c) Remplacer "thread dédié à `serve_file`" par "thread dédié à la connexion" avec dispatch générique.**
   Le point d'entrée du thread (`create_thread(serve_file, h)`, `upnphttp.c:924`, lancé depuis `send_resp_dlnafile`, `upnphttp.c:912-929`) doit être généralisé pour boucler sur : lire une requête (via a), exécuter le même switch de dispatch qu'aujourd'hui dans `process_upnphttp_http_query()` (`upnphttp.c:536-584`) — y compris SOAP (`invalid_soap_action`/`req_soap_action`), description XML, icônes, SUBSCRIBE/UNSUBSCRIBE — puis recommencer, sauf si : le client envoie `Connection: close`, le timeout `SO_RCVTIMEO` de 20s expire (`upnphttp.c:393`), ou une erreur de parsing survient.
   Comme ce thread est déjà détaché et indépendant du thread principal `select()`, étendre son rôle à tous les types de requêtes ne bloque pas l'acceptation d'autres connexions ni le traitement SSDP — pas de dépendance dure avec le point 2 ci-dessous pour que ce choix soit sûr.
   `decrement_thread_count()` (`threads.c`) ne doit être appelé **qu'une seule fois**, à la sortie définitive de la boucle (fin de vie du thread/connexion), et non plus une fois par requête traitée comme c'est le cas aujourd'hui (actuellement 1 thread = 1 requête = 1 décrément).
   **Point de concurrence à résoudre, pas seulement à vérifier :** `subscriberlist` (`upnpevents.c`, utilisée par `process_http_subscribe_upnphttp`/`process_http_un_subscribe_upnphttp` et par `add_upnpevent_subscriber`/`renew_upnpevent_subscriber`/`remove_upnpevent_subscriber`) est une liste globale aujourd'hui **non protégée par mutex**, ce qui est sûr uniquement parce que SOAP/SUBSCRIBE/UNSUBSCRIBE s'exécutent toujours dans le thread principal `select()`. Une fois ce dispatch déplacé dans le thread par connexion, elle serait modifiée concurremment par ce thread **et** par `upnpevents_removed_timedout_subs()` appelée à chaque tour de la boucle `select()` du thread principal (`microdlna.c:681`) → race condition réelle (corruption de liste chaînée possible). Solution retenue : ajouter un mutex dédié dans `upnpevents.c` (sur le modèle du mutex existant dans `threads.c` pour `active_threads`), à verrouiller dans `add_upnpevent_subscriber`/`renew_upnpevent_subscriber`/`remove_upnpevent_subscriber` et dans `upnpevents_removed_timedout_subs()`.

2. **Sortir le parsing des en-têtes du thread principal `select()`** (optionnel, priorité basse) : créer le thread de traitement immédiatement après `accept()` (au lieu d'appeler `process_upnphttp_http_query()` de façon synchrone), pour que les connexions parallèles pendant un scrub ne soient plus sérialisées. Indépendant du point 1 (non requis pour sa sécurité, cf. ci-dessus), mais réduirait aussi la latence initiale. Changement contenu, risque faible, à faire après le point 1 si le gain est jugé nécessaire.

3. **Activer `TCP_NODELAY`** sur chaque connexion HTTP acceptée. Changement isolé : un `setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one))` juste après `accept()` dans `microdlna.c` (à côté des `SO_REUSEADDR`/`SO_RCVTIMEO` déjà positionnés, `microdlna.c:153-154`, `upnphttp.c:393-394`). Complémentaire du point 1 : réduit la latence des petits paquets sur une connexion qui persiste.

4. **Éviter le `open()`/`stat()`/`lseek()` répété** pour des requêtes successives sur le même fichier : devient naturel une fois le point 1 fait — si deux requêtes `Range` successives sur la même connexion visent le même chemin média, garder le descripteur fichier ouvert (variable locale dans la boucle du thread par connexion, pas un nouveau champ de `struct upnphttp`) au lieu de refaire `stat()`+`open()`+double `lseek()` (`upnphttp.c:734-760`) à chaque fois. Ne fermer/réouvrir que si le chemin demandé change.

5. **Corriger le flag `try_sendfile` statique** : `try_sendfile` (`sendfile.c:95`) est une statique locale à `send_file()`, donc partagée par tous les threads/connexions — une seule erreur `sendfile()` (hors `EPIPE`) désactive définitivement le zero-copy pour tout le processus. À faire porter par connexion en le passant en paramètre (pointeur) à `send_file()`, la valeur étant une variable locale à la boucle du thread par connexion (pas un nouveau champ dans `struct upnphttp`), initialisée à 1 au début de chaque connexion.

## Ordre d'implémentation suggéré

1 (avec a/b/c) → 4 → 3 → 5, le point 2 restant optionnel et réalisable à part, à n'importe quel moment après le point 1.

## Fichiers critiques

- `upnphttp.c` : `process_upnphttp_http_query`, `readline`, `parse_http_header`, `send_http_headers`/`start_send_http_headers`, `serve_file`, `send_resp_dlnafile` — cœur du changement (1a/1b/1c, 4, 5 partiel).
- `threads.c` : `create_thread`/`decrement_thread_count` — inchangé en soi, mais la discipline d'appel de `decrement_thread_count()` change (1c : une fois par connexion, plus par requête).
- `upnpevents.c` : `subscriberlist`, `add_upnpevent_subscriber`/`renew_upnpevent_subscriber`/`remove_upnpevent_subscriber`, `upnpevents_removed_timedout_subs` — nouveau mutex à ajouter (1c).
- `microdlna.c` : boucle `select()`/`accept()` (2, 3).
- `sendfile.c` : `send_file`, flag `try_sendfile` (5).

## Vérification

- Compiler et lancer microdlna en premier plan (`-d`/`--debug`) pour voir les logs de connexion/thread.
- Avec `curl --http1.1` ou un client maison : ouvrir une connexion, envoyer plusieurs requêtes `Range` successives sans la fermer, vérifier dans les logs qu'une seule connexion TCP est utilisée et que les réponses n'incluent plus `Connection: close` tant que le client ne le demande pas.
- Vérifier qu'une requête SOAP ou une description XML envoyée juste après une requête média, sur la même connexion persistante, reçoit bien une réponse correcte (valide le dispatch générique).
- Tester avec VLC : faire plusieurs seeks rapprochés dans une vidéo et constater (via `tcpdump`/Wireshark) qu'une seule connexion TCP est ouverte pour la session de scrub, et que la fluidité perçue s'améliore.
- Vérifier qu'une connexion inactive se ferme bien après ~20s (timeout `SO_RCVTIMEO`) et qu'un `Connection: close` explicite du client est respecté.

## Tests : mettre en place un framework de tests unitaires

Le projet n'a aujourd'hui **aucun test automatisé** (build par `Makefile` artisanal, zéro dépendance externe, pas d'autotools/CMake). Les changements ci-dessus (parsing extrait en fonction partagée, boucle de dispatch par connexion, correctif `try_sendfile`) touchent du code de parsing et de décisions de branchement sensibles — à tester avant/pendant leur implémentation plutôt qu'après coup.

**Framework recommandé : Unity (ThrowTheSwitch/Unity).**
- Zéro dépendance système : 1 `.c` + 2 `.h` (~1500 lignes), licence MIT, vendored directement dans le dépôt (`tests/unity/`) — pas de `pkg-config`, pas d'install système, cohérent avec le choix actuel de zéro dépendance externe.
- S'intègre dans le `Makefile` existant sans changer d'outillage : un binaire de test se compile avec le même `$(CC)`/`$(CFLAGS)` que le reste.
- Pas besoin de mocking : les fonctions prioritaires à tester sont pures sur des buffers, ou prennent un simple fd (`readline`, future `parse_http_request`) — un `socketpair()`/`pipe()` dans le test suffit à simuler un client, sans framework de mock.
- Alternatives écartées : **Check** (dépendance autotools/pkg-config, modèle un-process-par-test trop lourd pour le gain ici), **CMocka** (utile seulement si le besoin de mocker de vrais appels système comme `sendfile()` devient central — à reconsidérer plus tard si ça devient un point de friction, mais l'injection d'un fd invalide suffit pour l'instant).

**Intégration :**
- Nouveau répertoire `tests/` : `tests/unity/` (fichiers vendored), `tests/test_<module>.c` (un fichier par thème).
- Nouvelle cible `.PHONY: test` dans le `Makefile` racine, suivant le style existant (règles explicites par objet) : compile chaque `tests/test_*.c` avec `tests/unity/unity.c` et les `.o` du projet concernés, puis exécute les binaires.
- Pas de CI dans ce plan — seulement `make test` en local pour l'instant.

**Priorités de test (dans l'ordre utile pour sécuriser ce plan) :**
1. **`sanitise_path`** (sécurité — empêche de sortir de `media_dir`) : `../`, chemins absolus, chemins encodés. Critique dès aujourd'hui, à tester avant toute autre modification.
2. **`readline()`** (`upnphttp.c:358-379`) : ligne vide, pas de `\r\n` final, `limit` atteint, lecture partielle — sollicité bien plus souvent une fois la connexion persistante en place.
3. **`parse_http_header`** et la future `parse_http_request()` (point 1a) : en-têtes bien/mal formés, `Range`, et détection de `Connection: close` côté requête (absente aujourd'hui, à tester dès son ajout).
4. **`url_unescape`**, **`get_srt_path`**, détection MIME (`mime.c`) : fonctions pures sur le chemin de `serve_file`.
5. **Boucle de continuation par connexion** (point 1c, une fois implémentée) : simuler un client via `socketpair()` envoyant 2-3 requêtes successives sur le même fd, vérifier que la connexion persiste par défaut et se termine sur `Connection: close`/timeout/erreur de parsing.
6. **Flag `try_sendfile`** (point 5, une fois déplacé par-connexion) : une erreur sur une connexion ne doit pas désactiver le zero-copy des autres.

**Vérification du harnais :** `make test` compile et exécute tous les tests sans erreur ; casser volontairement `sanitise_path` ou `readline` doit faire échouer les tests correspondants (sanity check du harnais lui-même) ; aucune dépendance système supplémentaire ne doit être nécessaire au-delà du compilateur C déjà requis pour `microdlnad`.

Aucune modification de code n'a été faite à ce stade — ce document sert de base si vous souhaitez prioriser et implémenter un ou plusieurs de ces points plus tard.
