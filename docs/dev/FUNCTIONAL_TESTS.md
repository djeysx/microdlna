# Tests fonctionnels — MicroDLNA

Complète les [tests unitaires](TESTS.md) : ceux-ci ne lancent jamais le
vrai binaire (`microdlna.o` n'est jamais lié, `main()` n'existe pas dans
les binaires de test). Les tests fonctionnels lancent le vrai
`microdlnad` comme un processus séparé et lui parlent par de vraies
sockets (HTTP/SOAP en TCP, SSDP en UDP), pour vérifier le comportement de
bout en bout — y compris sous AddressSanitizer/UBSan en conditions de
trafic réel.

## Lancer les tests

```sh
./configure.sh && make check-functional
```

Construit d'abord une copie de `microdlnad` compilée avec
`-fsanitize=address,undefined` (dans `build/tests/functional/sanitized/`,
jamais `build/microdlnad`), puis lance chaque binaire
`build/tests/functional/bin/ftest_*` contre elle. Plus lent que
`make check` (vrais processus, vraies sockets) : volontairement séparé, pas
un pré-requis de `check`/`test`.

Pour déboguer un seul scénario :

```sh
MICRODLNAD_BIN=build/tests/functional/sanitized/microdlnad \
  build/tests/functional/bin/ftest_fileserve
```

## Organisation

```
tests/functional/
├── support/
│   ├── process.h/.c      # spawn/attente-ready/arrêt de microdlnad
│   ├── httpclient.h/.c   # client HTTP/1.1 minimal sur socket brute
│   ├── netutil.h/.c      # allocation d'un port TCP libre
│   ├── fixtures.h/.c     # répertoire média temporaire par test
│   └── ssdpclient.h/.c   # client M-SEARCH, routé via lo
└── ftest_*.c             # un TEST() par scénario, même harness que
                            tests/support/test.h (TEST()/ASSERT_*)
```

Contrairement aux tests unitaires, ces binaires ne lient **aucun** `.o` de
microdlna : ils parlent uniquement au processus réel, lancé par
`mdlna_start()`. `tests/support/test_main.c` est réutilisé tel quel (même
objet, voir Makefile) ; `tests/support/stubs.c`/`helpers.c` ne le sont pas
(ils stubent `main()`/les globales, qui ne sont jamais linkés ici — la
raison d'être ne s'applique pas).

## SSDP multicast sans carte réseau réelle

`ftest_ssdp.c` démarre `microdlnad -i lo`. `getifaddr.c` n'exclut
`IFF_LOOPBACK` que si **aucune** interface n'est demandée explicitement :
avec `-i lo`, le socket de réception SSDP rejoint le groupe multicast
`239.255.255.250` directement sur `lo`, qui n'a pas de filtrage matériel.
Le client de test (`ssdpclient.c`) force sa sortie multicast sur `lo` via
`IP_MULTICAST_IF`, exactement comme le fait microdlnad pour sa propre
interface. Les réponses M-SEARCH partent en unicast (`send_ssdp_response()`,
`minissdp.c`), donc le `IP_MULTICAST_LOOP=0` du socket `notify` du serveur
(qui ne concerne que ses propres `NOTIFY` périodiques) n'interfère pas.
Fonctionne sans root et sans carte réseau réelle — y compris en CI.

## Défaut de production trouvé et corrigé en écrivant ces tests

**La détection de la boucle locale dans `microdlna.c` était cassée sur les
machines little-endian** (x86_64, ARM — donc la quasi-totalité des
machines de dev/CI actuelles) :

```c
if (clientname.sin_addr.s_addr != INADDR_LOOPBACK)
```

`sin_addr.s_addr` est en network byte order (big-endian), alors que la
macro `INADDR_LOOPBACK` est une constante en host byte order. Sur une
machine little-endian, cette comparaison ne correspond donc (presque)
jamais à une vraie connexion depuis `127.0.0.1` : dès qu'une interface
réseau réelle est configurée (le cas le plus courant en dehors d'un
conteneur minimal), toute connexion HTTP locale est renvoyée dans
`get_interface()`, qui ne la reconnaît pas, et donc **rejetée** —
visible dans les logs comme `Rejected HTTP connection from 127.0.0.1:...`.
Repéré immédiatement en essayant de faire parler les premiers tests
fonctionnels à un `microdlnad` lancé en local. Corrigé en comparant à
`htonl(INADDR_LOOPBACK)` (`microdlna.c`).
