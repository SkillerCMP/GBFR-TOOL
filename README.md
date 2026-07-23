<p align="center">
  <img
    src="Assets/GBFR-TOOL-Icon.png"
    alt="GBFR - TOOL Logo"
    width="220"
  >
</p>
<h1 align="center">Granblue Fantasy: Relink - Save Tool</h1>
<h3 align="center">GBFR - TOOL v1.10</h3>

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
- Combined Current Sigil MOD window with both linked trait IDs and levels
- Combined Wrightstone MOD window with parent fields, lock state, three linked traits, and derived attachment status
- Character-grouped Weapons tree with equipped markers and a combined Weapon MOD window
- Shared Weapon Game Rules mode for normal setup-trait and appearance restrictions or unrestricted modding
- Right-click bulk MOD editors for Summons, Weapons, Wrightstones, Sigils, Items, OverMastery, and Mastery Tree groups
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
<summary><strong>⚔️ Character-Grouped Weapon MOD Window</strong></summary>

<br>

Owned weapons are grouped in the tree under the character they belong to. The grouping
is resolved from a compact editor-specific mapping compiled into the program. The raw
`weapon.tbl` and `weapon_skill_level_rebuild.tbl` files are not included or required at runtime.

The currently equipped copy is marked using the character equipment reference stored in
`0x057A`:

```text
Weapons
└─ Maglielle
   ├─ Excalibur - Slot 154 [Equipped]
   └─ Exo Maitrah Karuna - Slot 178
```

Clicking a weapon opens one combined **Weapon MOD** window containing:

- Weapon ID, XP, Level Cap, and Mirage Munitions
- Weapon Appearance / Skin ID (`0x0AFE`)
- Four visible Weapon Setup trait slots
- Attached Wrightstone Item ID
- Three copied attached-Wrightstone Trait IDs and levels (`130...` namespace)
- Equipped status and an option to equip the selected weapon to its owning character

### Weapon Game Rules

**Game Rules ON** is the default whenever the window opens. Fixed setup slots are read-only,
swap-active slots only accept permitted traits, and the appearance picker lists only skins
from the same character that are marked available in the loaded save. `887AE0B0` restores
the weapon's normal/default appearance.

With **Game Rules OFF**, all four visible setup slots accept any known trait, an empty value,
or a manually entered hash. Weapon Appearance also accepts every known appearance save key
and manual hashes, including cross-character models. Unsupported combinations may display
incorrectly, be invisible, or have no effect in-game. The fifth underlying setup value is
reserved and is never changed by this window.

Appearance availability is read from the paired `0x1CE9` / `0x1CEB` lists. GBFR-TOOL treats
state bit `0x04` as available for filtering and does not modify either unlock list.

The save-side Weapon ID can differ from the public weapon hash used by the name database.
GBFR-TOOL preserves both mappings so names remain correct while the proper save selector key
is written back.

All fields are applied together to the loaded save in memory with one **MOD** button.

The Weapon window no longer edits the `140...` namespace. Those records belong to Wrightstone inventory entries and are edited from the Wrightstones tree.

</details>

<details>
<summary><strong>💎 Combined Wrightstone MOD Window</strong></summary>

<br>

Only occupied Wrightstone inventory slots are shown in the normal tree. Locked entries are marked `[Locked]`. A unique composite attachment is marked `[Attached]`; duplicate matches are marked `[Possible Attached]`. Clicking one opens a combined window containing:

- Wrightstone ID (`FF3608` / `0x0836`)
- Instance / companion value (`FF3708` / `0x0837`)
- Locked flag (`FF3808` / `0x0838`)
- State / type value (`FF3908` / `0x0839`)
- Three linked Trait IDs and levels (`FFA506` / `FFA606`)

The linked UnitID relationship is:

```text
Wrightstone slot = Wrightstone UnitID - 50000
Trait base       = 140000000 + (Wrightstone slot * 100)

Trait Slot 1 = Trait base
Trait Slot 2 = Trait base + 1
Trait Slot 3 = Trait base + 2
```

The window includes an **Attachment Status** indicator. The save does not expose a confirmed unique pointer from a weapon back to one Wrightstone inventory copy, so GBFR-TOOL compares the Wrightstone ID plus all three Trait IDs and levels with the copied `130...` weapon traits. A unique composite match is shown as attached; duplicate copies are labelled as an ambiguous possible match rather than falsely identifying one inventory copy.

</details>

<details>
<summary><strong>💠 Combined Current Sigil MOD Window</strong></summary>

<br>

Clicking an owned sigil in the `Current Sigils` tree opens one combined window containing:

- Sigil ID (`FF8F0A` / `0x0A8F`)
- Sigil level (`FF900A` / `0x0A90`)
- Equipped / Worn By (`FF920A` / `0x0A92`)
- Trait Slot 1 ID and level (`FFA506` / `FFA606`)
- Trait Slot 2 ID and level (`FFA506` / `FFA606`)

The sigil inventory record and trait records are physically separate in the save. GBFR-TOOL
links them with the confirmed UnitID relationship:

```text
Sigil index = Sigil UnitID - 30000
Trait base  = 120000000 + (Sigil index * 100)

Trait Slot 1 UnitID = Trait base
Trait Slot 2 UnitID = Trait base + 1
```

Example for Sigil UnitID `30259`:

```text
Sigil Slot 259
├── Trait Slot 1: UnitID 120025900
└── Trait Slot 2: UnitID 120025901
```

Normal one-trait sigils still contain the second linked record. Its Trait Slot 2 ID is
`887AE0B0` (`Global Empty Slot`). Selecting that value preserves a single-trait sigil.

The single **MOD** button writes the sigil and both linked trait slots together to the loaded
save in memory. The normal `Current Traits` logical tree is no longer shown because the known
`120...`, `130...`, and `140...` namespaces are now exposed through their parent Sigil, Weapon,
and Wrightstone editors. The raw physical `FFA506` and `FFA606` sections remain available for
research and recovery.

</details>


<details>
<summary><strong>🖱️ Right-Click Bulk MOD Editors</strong></summary>

<br>

The following tree groups provide a right-click **MOD All Non-Empty Slots** command:

- `Summon Inventory`
- `Weapons`
- `Wrightstones`
- `Current Sigils`
- `Items`
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

## v1.09.1 Windows build note

The Windows resource file is now static and the provided build script builds projects serially. This prevents overlapping Visual Studio CMake regeneration checks from racing on generated resource files.
