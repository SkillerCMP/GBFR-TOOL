<p align="center">
  <img
    src="Assets/GBFR-TOOL-Icon.png"
    alt="GBFR - TOOL Logo"
    width="220"
  >
</p>
<h1 align="center">Granblue Fantasy: Relink - Save Tool</h1>
<h3 align="center">GBFR - TOOL v1.06</h3>

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

- Logical save-data editors with one in-memory `MOD` action per editor
- Right-click bulk MOD editors for Summons, Weapons, Sigils, Items, Traits, OverMastery, and Mastery Tree groups
- Per-field `ON` / `OFF` exclusion controls in every bulk editor; all fields start disabled
- Protected Item bulk editing that excludes Curios, Rupies, Mastery Points, Conflux Points, and Resonance Points
- Item `MAX All` command for setting eligible non-empty Item counts to `9999`
- Curio Slot Counter (`FFD307`) displayed, editable, and automatically normalized with each Curio slot
- Hash-name resolution
- Section metadata and naming
- Relationship mapping
- Save comparison tools
- Data and relationship exports
- Graphical and command-line interfaces
- Editable external hash and mapping databases

</details>


<details>
<summary><strong>🖱️ Right-Click Bulk MOD Editors</strong></summary>

<br>

The following tree groups provide a right-click **MOD All Non-Empty Slots** command:

- `Summon Inventory`
- `Weapons`
- `Current Sigils`
- `Items`
- Character groups under `Current Traits`
- Character groups under `OverMastery`
- Character groups and `Shared / Global` under `Mastery Tree`

Every field in a bulk editor starts **OFF** and is grayed out. Turn a field **ON**
only when that field should be written to every eligible non-empty slot. Pressing
the single `MOD` button updates the loaded save in memory; the save is not written
to disk until **File > Save Edited ... As** is used.

The `Items` right-click menu also provides **MAX All Item Counts to 9999**.
Curio T1-T4, Rupies, Mastery Points (MSP), Conflux Points (CP), and Resonance Points (RP) entries are
excluded from both Item bulk operations.

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
<summary><strong>🔎 Global and Section Search</strong></summary>

<br>

The main search at the top of the window searches every loaded logical section using:

- `HASH[BE]`
- `HASH[LE]`
- `TYPE`
- `INGAMENAME`
- `INTERNALNAME`

For example, a slot containing this database entry can be found using any value on the row:

```text
DB1D4F35    354F1DDB    Treasure    Cobblestone    ITEM_01_0000
```

Search results display a `current/total` counter and can be navigated with **Previous** and **Next**.

Right-click a logical family, supported character group, Mastery Tree group, or Curio group and choose:

```text
Search This Section...
```

The section window searches only within that selected scope and provides the same `1/N`, Previous, and Next navigation.

</details>


<details>
<summary><strong>🔢 Curio Slot Counter</strong></summary>

<br>

The Curios logical section includes both slot-level companion records:

- `FFD207` (`0x07D2`) — Curio Type / Tier
- `FFD307` (`0x07D3`) — Curio Slot Counter

Both values use the Curio slot UnitID rather than the individual reward-entry UnitID.
The counter is shown once under each slot and can also be changed through the normal
Curio MOD window.

Curio MOD applies these consistency rules automatically:

- A completely empty Curio slot always uses counter `0`.
- Adding the first reward to an empty slot activates that reward entry.
- The new slot counter is one more than the nearest earlier active Curio slot counter.
- When no earlier active Curio exists, the new sequence starts at `1`.
- An empty or unknown Curio Type defaults to Tier 1.
- A user-selected Tier 1, 2, 3, or 4 value is preserved.

For the supplied validation save, Slot 86 has counter `755`, so activating Slot 87
automatically assigns counter `756`.

</details>

<details>
<summary><strong>🔗 Protected Item Entry Redirects</strong></summary>

<br>

Rupies, Mastery Points, Conflux Points, Resonance Points, and Curio T1-T4 Item entries do not use their ordinary Item Count / flag fields as normal inventory items.

For these IDs, the Item Entry MOD window disables:

- Item Count
- Item Flags
- Both Unknown Item Fields

Rupies, Mastery Points, CP, and RP provide **Go to Global Values** buttons. Curio T1-T4 entries provide **Go to Curios** buttons.

The safeguards also apply to the raw-tree value editor so the protected fields cannot be modified through the alternate view.

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
0-Finish/
├── GBFR-TOOL.exe
├── GBFR-TOOL-CLI.exe
├── build-windows-x64-warnings-errors.log
├── Hashfolder/
├── Licenses/
├── LICENSE.txt
└── README.md
```

After a successful package copy, the script moves the build log into `0-Finish`
and removes the temporary `build-windows-x64` directory.

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
