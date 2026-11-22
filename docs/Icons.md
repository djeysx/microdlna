# Icônes MicroDLNA (`icons.h`)

**Fichier source :** `icons.h`
**Consommateur :** `upnphttp.c` (fonction `send_resp_icon`, lignes 641-687)

`icons.h` embarque, sous forme de tableaux C, les icônes de présentation du
pingouin Tux utilisées par le serveur UPnP/DLNA. Quatre constantes sont
définies, correspondant à deux tailles et deux formats de la même image.

| Constante    | Format | Dimensions | URL servie |
|--------------|--------|------------|------------|
| `png_sm`     | PNG    | 41 × 48    | `sm.png`   |
| `png_lrg`    | PNG    | 102 × 120  | `lrg.png`  |
| `jpeg_sm`    | JPEG   | 41 × 48    | `sm.jpg`   |
| `jpeg_lrg`   | JPEG   | 102 × 120  | `lrg.jpg`  |

Les images sont la création de Larry Ewing (`lewing@isc.tamu.edu`), réalisées
avec The GIMP (voir l'en-tête de licence de `icons.h`).

---

## 1. Format PNG (`png_sm`, `png_lrg`)

| Propriété                     | Valeur                                      |
|--------------------------------|----------------------------------------------|
| Bit depth                      | 8                                            |
| Color type                     | 3 → **indexé (palette)**                    |
| Méthode de compression         | 0 (deflate/inflate, standard PNG)            |
| Méthode de filtre               | 0                                             |
| Entrelacement                   | 0 (non entrelacé)                            |
| Chunks présents                 | `IHDR` → `PLTE` → `tRNS` → `IDAT` → `IEND`   |
| Taille de la palette            | 200 entrées (sm) / 256 entrées (lrg)         |
| Transparence                    | `tRNS` présent → transparence par entrée de palette |
| Métadonnées                     | aucune (pas de `gAMA`, `sRGB`, texte, etc.)  |

**Pour reproduire :** exporter l'image en **PNG indexé 8 bits** (palette
≤ 256 couleurs), avec la transparence conservée, sans métadonnées
supplémentaires. Correspond à un export GIMP standard pour une image à
aplats de couleur.

---

## 2. Format JPEG (`jpeg_sm`, `jpeg_lrg`)

| Propriété                     | Valeur                                      |
|--------------------------------|----------------------------------------------|
| Marqueur `APP0`                 | JFIF v1.1, unités = dpi, densité 90 × 90, pas de vignette |
| Marqueur `SOF`                  | `0xC0` → **baseline DCT**, précision 8 bits, 3 composantes (Y, Cb, Cr) |
| Sous-échantillonnage chroma     | Luma H=2,V=2 / Chroma H=1,V=1 → **4:2:0**    |
| Tables de Huffman (`DHT`)       | 4 tables standard (2 DC + 2 AC), non optimisées |
| Tables de quantification (`DQT`)| valeurs quasi toutes entre 1 et 10 → qualité très élevée (≈ 95-100) |
| Progressif ?                    | non (baseline séquentiel)                    |

**Pour reproduire :** exporter en **JPEG qualité ~95-100, sous-échantillonnage
4:2:0, encodage baseline (non progressif)**.

---

## 3. Format du tableau C dans `icons.h`

```c
/* <Description courte de l'image> */
static unsigned char <nom>[] =
{ 0xXX, 0xXX, ... (14 octets par ligne, hex majuscule, "0x" + 2 chiffres)
  ...
};
```

- Indentation de 2 espaces, 14 octets par ligne, séparateur `", "`, pas de
  virgule finale sur le dernier octet.
- Chaque tableau contient **exactement** les octets bruts du fichier image
  correspondant — aucun octet de padding, aucun `'\0'` final ajouté.
- Chaque tableau est précédé d'un commentaire d'une ligne décrivant l'image.

### Correctif appliqué : `sizeof(...) - 1` dans `upnphttp.c`

`send_resp_icon` calculait la taille envoyée avec, par exemple :

```c
size = sizeof(png_sm) - 1;
```

Or les tableaux ne contiennent **aucun** octet supplémentaire après la fin
réelle du fichier (dernier octet du CRC du chunk `IEND` pour les PNG,
dernier octet `0xD9` du marqueur `EOI` pour les JPEG). Ce `-1` tronquait donc
systématiquement le tout dernier octet de chaque icône envoyée au client.

Analyse historique (git blame / `git log -S`) : ce `-1` était présent depuis
le commit initial du projet et n'avait jamais été modifié. Aucune autre
partie du code ni aucun générateur n'ajoute d'octet de padding aux tableaux
d'`icons.h` — les seuls autres usages de `sizeof(x) - 1` dans le projet
portent sur de vraies chaînes C terminées par `'\0'`, où le `-1` est
légitime. Il s'agissait donc d'un bug d'inattention, sans raison valable,
présent depuis l'import initial.

**Correctif :** le `-1` a été retiré sur les 4 occurrences (`png_sm`,
`png_lrg`, `jpeg_sm`, `jpeg_lrg`) dans `send_resp_icon` (`upnphttp.c`). Les
icônes sont désormais envoyées avec leur taille exacte (`sizeof(array)`),
sans troncature.

Si de nouvelles constantes sont générées à partir d'images personnalisées en
copiant les octets bruts du fichier (comme décrit en section 4), aucun
ajustement supplémentaire n'est nécessaire : `sizeof(array)` donne
directement la taille correcte à envoyer.

---

## 4. Générer un tableau à partir d'une image personnalisée

Une fois l'image encodée avec les paramètres ci-dessus (palette PNG 8 bits
ou JPEG 4:2:0 qualité ~95-100, dimensions 41×48 ou 102×120 selon la variante
sm/lrg) :

```python
import sys

data = open(sys.argv[1], "rb").read()
name = sys.argv[2]

print(f"static unsigned char {name}[] =")
print("{", end="")
for i, b in enumerate(data):
    sep = "\n  " if i % 14 == 0 else " "
    print(f"{sep}0x{b:02X}" + ("," if i != len(data) - 1 else ""), end="")
print("\n};")
```

Usage : `python3 gen.py mon_icone.png png_sm > out.txt`, puis coller le
résultat dans `icons.h` à la place de l'ancien tableau correspondant.
