<p align="center">
  <img
    src="Assets/GBFR-TOOL-Icon.png"
    alt="GBFR - TOOL Logo"
    width="220"
  >
</p>

<h1 align="center">Granblue Fantasy: Relink - Save Tool</h1>
<h3 align="center">GBFR - TOOL v1.00</h3>

<p align="center">
  <a href="https://github.com/SkillerCMP/GBFR-TOOL/releases">
    <img
      alt="GitHub Downloads - All Releases"
      src="https://img.shields.io/github/downloads/SkillerCMP/GBFR-TOOL/total?style=social"
    >
  </a>
  <a href="https://github.com/SkillerCMP/GBFR-TOOL/releases/latest">
    <img
      alt="GitHub Downloads - Latest Release"
      src="https://img.shields.io/github/downloads/SkillerCMP/GBFR-TOOL/latest/total?style=social"
    >
  </a>
</p>

<p align="center">
  A native <strong>C++17 save-data viewer and editor</strong> for
  <strong>Granblue Fantasy: Relink GameData saves</strong>.
</p>

---

<details open>
<summary><strong>🛠️ About</strong></summary>

<br>

**GBFR - TOOL** provides tools for viewing, researching, comparing, and editing
Granblue Fantasy: Relink save data.

### Features

- Logical save-data editors
- Hash-name resolution
- Section metadata and naming
- Relationship mapping
- Save comparison tools
- Data and relationship exports
- Graphical and command-line interfaces
- Editable external hash and mapping databases

</details>

<details>
<summary><strong>📦 Program Files</strong></summary>

<br>

The Windows build produces:

```text
GBFR-TOOL.exe
GBFR-TOOL-CLI.exe
```

| File | Purpose |
|---|---|
| `GBFR-TOOL.exe` | Main Windows graphical interface |
| `GBFR-TOOL-CLI.exe` | Command-line analysis and export tool |

</details>

<details>
<summary><strong>🗂️ Hashfolder Data</strong></summary>

<br>

The editable data and mapping files are stored inside `Hashfolder`:

```text
Hashfolder/
├── GameData-Section-Cross-Reference.csv
├── GBFR-Character-Sections.txt
├── GBFR-Hash-Database.txt
└── GBFR-Section-Names.txt
```

### Runtime loading order

The GUI and CLI check for each file in this order:

1. `Hashfolder`
2. The folder containing the executable

This fallback preserves compatibility with older releases that stored the files
beside the EXE.

When `GBFR-Hash-Database.txt` is saved, the program writes it back to the same
location from which it was loaded.

</details>

<details>
<summary><strong>📜 Licenses</strong></summary>

<br>

The original **GBFR - TOOL** source code is licensed under the MIT License:

```text
LICENSE.txt
```

Third-party license notices are stored in:

```text
Licenses/
└── THIRD-PARTY-GBFRDataTools-LICENSE.txt
```

The third-party license notice must remain included with source and binary
distributions that contain the applicable material.

</details>

<details>
<summary><strong>🔨 Building on Windows</strong></summary>

<br>

### Requirements

- Windows x64
- Visual Studio 2022 Build Tools
- CMake with Visual Studio generator support

### Build command

Run:

```bat
build-windows-x64.cmd
```

The script builds:

- `GBFR-TOOL.exe`
- `GBFR-TOOL-CLI.exe`
- Core tests

It then creates the release folder:

```text
0-Finished/
├── GBFR-TOOL.exe
├── GBFR-TOOL-CLI.exe
├── Hashfolder/
├── Licenses/
├── LICENSE.txt
└── README.md
```

</details>

<details>
<summary><strong>🖼️ Repository Logo</strong></summary>

<br>

The README expects the PNG logo at:

```text
Assets/
└── GBFR-TOOL-Icon.png
```

The Windows application icon can remain stored separately as an `.ico` file for
embedding into the executable.

</details>

---

## ⚠️ Important

Always keep a backup of the original save before making changes.

## 👤 Credits

Created for **the community and the scene**.

Special thanks to everyone who has contributed research, testing,
documentation, tools, and knowledge to the Granblue Fantasy: Relink community.
