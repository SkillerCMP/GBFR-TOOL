Granblue Fantasy: Relink - Save Tool v1.00
GBFR - TOOL
==========================================

ABOUT
-----
GBFR - TOOL is a native C++17 save-data viewer and editor for Granblue Fantasy:
Relink GameData saves. It includes logical editors, hash-name resolution,
section metadata, relationship mapping, comparison, and export tools.

PROGRAM FILES
-------------
The Windows build produces:

  GBFR-TOOL.exe
  GBFR-TOOL-CLI.exe

HASHFOLDER DATA
---------------
The four editable data and mapping files are stored in Hashfolder:

  Hashfolder/GameData-Section-Cross-Reference.csv
  Hashfolder/GBFR-Character-Sections.txt
  Hashfolder/GBFR-Hash-Database.txt
  Hashfolder/GBFR-Section-Names.txt

At runtime, the GUI and CLI prefer the copy inside Hashfolder. If a specific
file is missing there, they fall back to the same file beside the EXE for
compatibility with older layouts. When saving the hash database, the program
keeps using the location from which it was loaded.

LICENSES
--------
The original GBFR - TOOL source is licensed under the MIT License. See:

  LICENSE.txt

Third-party license notices are stored in:

  Licenses/THIRD-PARTY-GBFRDataTools-LICENSE.txt

BUILD
-----
Run build-windows-x64.cmd on Windows with Visual Studio 2022 Build Tools.
The script builds the GUI, CLI, and tests, then creates 0-Finished with the
executables, Hashfolder, root MIT license, third-party Licenses folder, and
README.
