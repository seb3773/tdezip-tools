# TdeZip-Tools

Dépôt APT officiel et code source des compresseurs / archiveurs dédiés pour **TdeZip** (portage natif Trinity Desktop / C++ de PeaZip).

🌐 **Page du dépôt / GitHub Pages** : [https://seb3773.github.io/tdezip-tools/](https://seb3773.github.io/tdezip-tools/)

---

## 📦 Utilisation avec APT

Ajouter le dépôt aux sources du système :

```bash
echo "deb [trusted=yes] https://seb3773.github.io/tdezip-tools/ stable main" | sudo tee /etc/apt/sources.list.d/tdezip-tools.list
sudo apt-get update
```

Installer un outil (ex: LHA avec support création) :

```bash
sudo apt-get install lha
```

---

## 🛠️ Outils Disponibles et Prévus

| Outil | Version | Statut | Description |
| :--- | :--- | :--- | :--- |
| **`lha`** | 1.14i-1 | ✅ Disponible | Archiveur LHA/LZH en C natif pur (96 Ko, 0 dépendance, 0 Java). Prend en charge la création (`-a`), mise à jour (`-u`), extraction (`-x`). |
| **`pea`** | 1.0 (C) | 🚧 En cours | Moteur de format PEA réécrit en C pur, ultra-léger et moderne, remplaçant le binaire Pascal original. |
| **`uharc`** | 0.6b (C) | 🚧 En cours | Compresseur et décompresseur UHARC natif en C (interopérabilité européenne). |

---

## 🔍 Transparence & Compilation depuis les Sources

Chaque sous-dossier à la racine contient l'intégralité du code source C de l'outil et un script de packaging autonome :

```bash
cd lha
./build_deb.sh
```

Pour mettre à jour le dépôt APT après ajout d'un nouveau paquet :

```bash
./update_repo.sh
```
