# Sprachpakete (Fan-Übersetzungen der europäischen Discs)

Wunsch des Projektinhabers vom 26.11.2026, nach der Machbarkeitsprüfung
`reports/custom-languages-2026-11-26/pack-loading-feasibility.md` (außerhalb des Repos). Beispiel war eine
brasilianisch-portugiesische Fan-Übersetzung, die den spanischen Platz der EU-Discs überschreibt.

## Was ein Sprachpaket ist

Ein Sprachpaket sind **ersetzte Disc-Dateien** einer Fan-Übersetzung für die **europäischen Discs (G4BP08)**:
Texte, Untertitel, Funksprüche, Dokumente und die Menüwörter als Bilder. Meist ersetzt es den Platz einer
vorhandenen Sprache (das Beispiel: Spanisch, Spielsprache 5).

- CubeEvil4 liefert **keine** Sprachpakete und verlinkt auf keine. Der Spieler importiert sein Paket selbst.
- Die Dateien des Pakets sind Spieldaten: **nie** ins Repository, in die APK, in Testdaten oder in einen
  Fehlerbericht. Der Bericht enthält nur Name, Sprache, Anzahl und Größe (`LanguagePacks.reportLine`).

## Import (App und PC, `app/src/main/cpp/language_pack.cpp`)

Quelle: ein Ordner oder eine ZIP-Datei (gespeichert oder deflate, ZIP64; eigener Entpacker ohne Bibliothek in
`language_pack_zip.cpp`). Aufbau beliebig: `Disc 1/…`, `Disc 2/…` (auch „disc2“, „Disco 2“, „CD 1“), ein
einzelner Ordner (gilt dann für jede Disc, die die Datei hat), Dolphins `files/`/`sys/`. Dateien außerhalb
der Spielordner (Lies-mich, Bilder) werden übergangen.

Geprüft wird gegen die **eigenen importierten Discs** des Spielers:

| Regel | Folge |
| --- | --- |
| Discs nicht europäisch | abgelehnt (`kNotEuropean`) |
| Datei gleich wie auf der Disc | übergangen (nur gezählt) |
| Datei nicht auf der Disc | abgelehnt (`kNotOnDisc`, anderes Release) |
| `sys/`, `.dol`, `.rel`, `Rel/` verändert | abgelehnt (`kProgramFile`) |
| andere Datei außerhalb der Erlaubnisliste verändert | abgelehnt (`kNotText`) |
| Raum `St*/r*.das` | Archiv entpackt (yz2); **jeder Teil außer den Texttabellen (MDT)** muss gleich sein – Kollision (SAT), Gegenstände (ITM), Gegnerbereiche (EAT), Kamera, Licht, Modelle … |
| Filmsequenz `Evd/*.evd` | Kopf, Pakete (Ablauf) und Namen gleich; jedes Teilstück außer `.mdt` an gleicher Stelle gleich; eine Texttabelle darf wachsen und ans Ende wandern (Zeiger und Größe im Eintrag) |
| `etc/core.das`, `etc/memcard.das`, `etc/movie.das`, `op/*.das` | gleiche Blöcke und Teile; geändert nur Texttabellen und die **Bildtabellen** der Effektdaten (EFF: TPL-Liste und ihre Kennungen); die übrigen EFF-Tabellen Eintrag für Eintrag gleich (Werkzeuge der Übersetzer legen sie neu an) |
| `ss/**/*.dat` | wie oben, dazu Bildschirm-Layouts (UWF) |
| `ss/**/*.tpl` | gültige TPL mit gleich vielen Bildern |
| `font/*.fnt` | gleicher Kopf, höchstens 4× so groß |

Welche Sprache das Paket ersetzt, ergibt sich aus den geänderten Texten (Plätze der sechssprachigen Tabellen,
Ordner `ss/<Sprache>`). Ohne erkennbare Sprache: abgelehnt.

Ergebnis im Ordner `language/pack` neben `game/` (erst `pack.work`, am Ende umbenannt; ein altes Paket geht erst
dann): `overlay.tsv` (`V 1`, `L <Sprache>`, je Datei `F <Disc> <Pfad> <Größe> <Datei>`), `pack.properties`
(Name, Sprache, Zahlen), `files/…` (eine Kopie für beide Discs), `disc2/…` (nur wenn Disc 2 abweicht).

## Kern (`port/whole/runtime/dvd.cpp`)

`RE4_LANGUAGE_PACK=<Ordner>` (immer bei EU-Discs), `RE4_LANGUAGE_PACK_ON=1` (wenn das Paket gewählt ist).
`loadDisc` → `applyPack`: nur bei EU-Discs, Overlay-Version 1 und wenn die Spielsprache die des Pakets ist. Für
jede Datei mit passender Größe wird die Länge im FST gesetzt und `entryPath` liefert die Paketdatei. Damit sehen
`lookup`, `DVDFastOpen`, `readFile`/`entryForStart`, `FileExistCheck` (Puffer nach Dateigröße) und die
Ablageorte von Kern- und Optionsdaten alle dieselbe Größe.

Umschalten im Spiel: `re4_core_restart_game(sprache | 0x200 | 0x100 an)`. Beim Neustart-Haken
(`game_language.cpp`, vor dem Soft-Reset) lädt `re4_language_pack_switch` beide Discs neu, auch das FST im MEM1.
Der Soft-Reset liest `etc/core.das` neu. **`etc/memcard.das` liest das Spiel nur beim Start:** die
Speicherkarten-Meldungen wechseln erst beim nächsten App-Start.

## App

- `LanguagePacks.java`: Ordner, Info, Import (SAF-Ordner oder ZIP über `ParcelFileDescriptor`), Umbenennen, Löschen;
  JNI-Bibliothek `re4langpack` (`language_pack_jni.cpp`), lädt das Spiel nicht.
- „Cheats & Mods“ → **Sprachpaket**: Zeile mit Name, ersetzter Sprache, Anzahl, Größe, Datum; „Sprachpaket
  importieren“ (Frage: Ordner oder ZIP), „Sprachpaket löschen“ (leises Rot). Nach dem Import: Name eingeben,
  „Jetzt spielen“ oder „Später“.
- „Spielsprache“: siebte Wahl mit dem Namen des Pakets (`LanguagePacks.CHOICE` = 7). Neustart wie bei jedem
  Sprachwechsel. Ist das Paket gerade in Gebrauch, startet das Spiel vor Ersetzen/Löschen in der Gerätesprache neu.
- Eigene Leser der App (Speicherbild `save_native.cpp`, Startbilder `startup_data.cpp`, `core_scene.cpp`) fragen
  `re4lang::DiscFile` (`language_pack_files.cpp`, gesetzt über `CoreBridge.setLanguagePack`).
- `DataFolder.ENTRIES` enthält `language` (zieht beim Verschieben des Datenordners mit).

## Prüfen

- PC: `tools/langpack_tool.cpp` (`import GAMEROOT DEST QUELLE`, `selftest-room RAUM.das`).
- Kern am PC: `RE4_LANGUAGE_PACK`, `RE4_LANGUAGE_PACK_ON=1`, `RE4_GAME_LANGUAGE=5`; Umschalten mit
  `RE4_TEST_LANGUAGE_RESTART="N:773"` (an) / `"N:517"` (aus); neuer Testschalter `RE4_TEST_SSCRN_OPEN="free+N:T"`
  (Untermenü vom Typ T, 64 die Dokumente).
- Bilder der Menüteile: `LanguagePackShotTest` (nur mit `RE4_MENU_SHOTS`).
