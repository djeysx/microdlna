# Icônes MicroDLNA

**Consommateur :** `upnphttp.c` (fonction `send_resp_icon`, lignes 641-687)
**Générateur :** `gen_icon.sh`
**Sélection au build :** variable Make `ICON` (défaut : `clap`)

Le serveur UPnP/DLNA embarque 4 images (une icône, en deux tailles et deux
formats) compilées directement dans le binaire. Elles ne sont plus écrites à
la main : elles sont générées par `gen_icon.sh` à partir d'une image source,
rangées dans `icons/<nom>/`, et le `Makefile` choisit quel jeu utiliser via
`ICON=<nom>`.

| Tableau C  | Format | Dimensions | URL servie     |
|------------|--------|------------|----------------|
| `png_sm`   | PNG    | 41 × 48    | `/icons/sm.png`|
| `png_lrg`  | PNG    | 102 × 120  | `/icons/lrg.png`|
| `jpeg_sm`  | JPEG   | 41 × 48    | `/icons/sm.jpg`|
| `jpeg_lrg` | JPEG   | 102 × 120  | `/icons/lrg.jpg`|

Le jeu par défaut est `clap` (icône de clap de cinéma). Le jeu `tux` reste
disponible dans `icons/tux/` : il reprend le pingouin historique de Larry
Ewing (`lewing@isc.tamu.edu`), réalisé avec The GIMP, extrait tel quel de
l'ancien `icons.h` lors de la mise en place de ce mécanisme.

---

## 1. Arborescence `icons/<nom>/`

Chaque jeu d'icônes vit dans son propre dossier :

```
icons/
  clap/
    clap_sm.png
    clap_lrg.png
    clap_sm.jpg
    clap_lrg.jpg
    icons.h        <- généré, contient png_sm/png_lrg/jpeg_sm/jpeg_lrg
  mon_icone/
    mon_icone_sm.png
    ...
    icons.h
```

`icons/<nom>/icons.h` déclare toujours les 4 tableaux avec les **mêmes noms
canoniques** (`png_sm`, `png_lrg`, `jpeg_sm`, `jpeg_lrg`), quel que soit
`<nom>` — c'est ce qui permet à `upnphttp.c` de rester inchangé (il fait
simplement `#include "icons.h"`) alors que le contenu réel dépend du dossier
sélectionné au build.

Ces dossiers sont des fichiers source normaux : ils sont commités comme
n'importe quel autre fichier (pas de `.gitignore`), contrairement à
`build/version_info.h` qui lui est régénéré à chaque build et jamais commité.

---

## 2. Générer un jeu d'icônes avec `gen_icon.sh`

```sh
./gen_icon.sh <nom> <image_source>
```

- Convertit `<image_source>` (n'importe quel format lisible par ImageMagick)
  en 4 fichiers via `convert`, aux dimensions fixes attendues par
  `upnphttp.c` (41×48 et 102×120) :
  - PNG : `-background white -flatten -resize WxH! -colors 256 -type Palette`
    (toute transparence est aplatie sur un fond blanc, puis indexé 8 bits).
  - JPEG : `-resize WxH! -background white -flatten -quality 95
    -sampling-factor 2x2,1x1,1x1` (baseline, 4:2:0, qualité ~95).
- Empaquette les 4 fichiers en C avec `xxd -i`, renomme les tableaux générés
  vers les noms canoniques, et écrit `icons/<nom>/icons.h`.

```sh
./gen_icon.sh --pack <nom>
```

Variante qui ignore la conversion ImageMagick et empaquette directement les
4 fichiers déjà présents dans `icons/<nom>/` (utile si les images ont déjà
les bonnes dimensions/format, ou pour reconstruire `icons.h` après une
modification manuelle des images).

---

## 3. Choisir le jeu d'icônes au build

```sh
make                 # ICON=clap (défaut)
make ICON=mon_icone  # utilise icons/mon_icone/icons.h
```

Mécanisme :
- `ICON ?= clap` dans le `Makefile`.
- `-Iicons/$(ICON)` est ajouté aux flags de compilation, donc
  `#include "icons.h"` dans `upnphttp.c` résout vers
  `icons/$(ICON)/icons.h`.
- Un fichier témoin `build/.icon` mémorise le dernier `ICON` utilisé et force
  la recompilation de `upnphttp.o` quand il change — sans lui, passer de
  `ICON=a` à `ICON=b` sans `make clean` ne recompilerait pas (Make ne détecte
  que des fichiers modifiés, pas des variables).
- Si `icons/<nom>/icons.h` n'existe pas, le build échoue avec un message Make
  explicite (`No rule to make target 'icons/<nom>/icons.h'`) : il faut
  d'abord lancer `gen_icon.sh`.

---

## 4. Formats produits

### PNG (`png_sm`, `png_lrg`)

| Propriété          | Valeur                                    |
|---------------------|--------------------------------------------|
| Bit depth           | 8                                          |
| Color type           | 3 → indexé (palette, ≤ 256 couleurs)      |
| Compression/filtre   | 0 / 0, non entrelacé                      |
| Chunks               | `IHDR` → `PLTE` → `IDAT` → `IEND` (pas de `tRNS` : la transparence source est aplatie sur fond blanc par `gen_icon.sh`) |

### JPEG (`jpeg_sm`, `jpeg_lrg`)

| Propriété              | Valeur                                  |
|--------------------------|-------------------------------------------|
| Marqueur `APP0`           | JFIF                                      |
| `SOF`                      | `0xC0` baseline DCT, 8 bits, 3 composantes |
| Sous-échantillonnage       | 4:2:0 (luma H2V2 / chroma H1V1)          |
| Qualité                    | ~95                                       |

`icons/tux/` (extrait tel quel de l'ancien `icons.h`, non régénéré par
`gen_icon.sh`) garde ses caractéristiques d'origine : PNG indexé 200/256
couleurs **avec transparence** (`tRNS`), JPEG qualité ≈ 95-100. Tout jeu
généré via `gen_icon.sh` (dont `clap`) suit en revanche les paramètres fixes
ci-dessus — fond blanc à la place de la transparence, qualité JPEG 95 — sans
viser une reproduction bit-à-bit.

---

## 5. Historique : correctif `sizeof(...) - 1`

`send_resp_icon` calculait autrefois la taille à envoyer avec
`sizeof(png_sm) - 1` (et pareil pour les 3 autres tableaux). Les tableaux ne
contenant aucun octet de padding, ce `-1` tronquait systématiquement le
dernier octet de chaque icône envoyée (CRC `IEND` pour les PNG, marqueur
`EOI` pour les JPEG). Le `-1` était présent depuis le commit initial du
projet, sans justification ni convention qui l'expliquerait ; il a été
retiré sur les 4 occurrences dans `upnphttp.c`.
