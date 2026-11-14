# CubeEvil4 Texture Packer 1.4 für Windows: das Fenster (14.11.2026, Branch `converter-tool`)

Wunsch des Projektinhabers (14.11.2026): „Ich möchte zusätzlich zur Commandline Version auch eine richtige Windows
Version mit Fenster. … 1. Fenster öffnet sich 2. Man kann das ZIP Archiv oder den entpackten Ordner direkt ins Fenster
ziehen (im Fenster einen Hinweis dass Drag&Drop möglich ist). 3. Im Fenster dann Auswahlmöglichkeiten für die Optionen
wie auch in der bisherigen Commandline- und Android-Version. 4. Danach wird ein Dateibrowser geöffnet wo man das
Zielverzeichnis für die .c4tex Datei gespeichert wird. 5. Danach läuft die Konvertierung, schön Visualisiert was gerade
passiert. 6. Das Programm weist auf den erfolgreichen (oder fehlgeschlagenen) Abschluss hin und kann beendet werden.“

Nur das PC-Programm ändert sich. CubeEvil4 selbst und die Android-Packer-App bleiben, wie sie sind (Feature-Freeze bis
1.0). Die `.c4tex`-Datei ist dieselbe wie bisher.

## Zwei Dateien statt einer (Entscheidung dieser Sitzung, begründet)

| Datei | Was | Für wen |
|---|---|---|
| `CubeEvil4-TexturePacker.exe` | das neue Fenster | alle: Doppelklick |
| `CubeEvil4-TexturePacker-Console.exe` | die bisherige Version (Fragen im Konsolenfenster, Befehlszeile) | Tests, Skripte, Fortgeschrittene |

Warum nicht eine Datei für beides: Windows legt beim Start fest, ob ein Programm ein Fenster-Programm oder ein
Konsolen-Programm ist.
- Als Konsolen-Programm würde beim Doppelklick zuerst kurz ein schwarzes Konsolenfenster aufblitzen (unter Windows 11
  oft ein ganzes Terminal-Fenster), bevor das richtige Fenster kommt.
- Als Fenster-Programm wartet die Befehlszeile nicht auf das Ende: PowerShell und die Eingabeaufforderung kämen sofort
  zurück, die Ausgabe liefe danach quer durch die Eingabezeile.

Mit zwei Dateien funktioniert beides sauber. Der Name, den Tester kennen (`CubeEvil4-TexturePacker.exe`, „Doppelklick und
Paket hineinziehen“), ist jetzt das Fenster. Startet man das Fenster mit Befehlszeilen-Optionen (`-in`, `-size` …), sagt
es, dass dafür `CubeEvil4-TexturePacker-Console.exe` da ist (in die Konsole oder als Meldung) und beendet sich.

Ein Paket, das man im Explorer auf das Programm-Symbol zieht (oder „Öffnen mit“), öffnet das Fenster gleich mit
diesem Paket.

## Das Fenster

Eine einzige `.exe` (etwa 0,6 MB), ohne Installation, ohne .NET oder andere Laufzeit: nur, was in Windows steckt
(Win32, GDI, GDI+). Ab Windows 10. Der Kern ist derselbe wie bei der Konsolen-Version und der Android-App
(`packer.h/.cpp`), daher entsteht dieselbe Datei.

Vier Seiten in einem Fenster:

1. **Paket:** Kurzerklärung, der Rechtshinweis (keine Texturen, keine Links), eine große Ablagefläche mit
   „Drag your texture pack here – a ZIP file or the unpacked folder“ und den Knöpfen „Choose ZIP file…“ und
   „Choose folder…“. Beim Darüberziehen färbt sich die Fläche und sagt „Release to open the pack“ (auf den anderen
   Seiten sagt es die untere Leiste). Danach „Reading the pack …“ mit Laufbalken und Zähler (Dateien, dann
   Bildgrößen). 7z/RAR: Hinweis, erst auszupacken.
2. **Optionen:** das Paket (Name, Bilder, Größe, ZIP oder Ordner, ausgelassene Dateien) mit „Other pack…“; die
   Auswahl im Paket (Varianten als Knöpfe nebeneinander, z. B. Kofferfarbe; Zusatzordner als Haken); die Größe 256 / 512
   / 1024 mit denselben Kurzerklärungen wie in der Android-App und je „up to … · about … min“ (Dateigröße höchstens
   und Dauer auf diesem PC). „Save and start…“.
3. **Speichern-Dialog von Windows** mit dem üblichen Namen `CubeEvil4-textures-512.c4tex`. Er beginnt beim ersten Mal im
   Ordner des Pakets, danach im zuletzt gewählten Ordner, und fragt vor dem Ersetzen einer vorhandenen Datei. Danach die
   Platzprüfung (Schätzung plus 100 MB, wie die Android-App): Reicht der Platz nicht, steht auf der Optionsseite, wie
   viel nötig und frei ist und welche kleinere Größe passen würde. Speichern im Paketordner selbst wird abgelehnt.
4. **Umwandeln:** drei Schritte mit Häkchen (Prepare – Convert the images – Finish the file, der laufende dreht sich),
   großer Prozentwert, Fortschrittsbalken, „x of y images“, vier Zähler (Converted, Written, Skipped, Refused), das
   Bild in Arbeit, vergangene Zeit und Restzeit. Der Fortschritt steht auch in der Titelleiste und im
   Taskleisten-Symbol (grüner Balken). Der PC geht währenddessen nicht in den Ruhezustand. „Cancel“ fragt nach
   („Stop converting?“ – Stop / Keep converting) und löscht dann die halbe Datei; dasselbe beim Schließen des Fensters.
5. **Ende:** grünes Häkchen „Done!“ (oder rot „Converting failed“ mit Grund, grau „Cancelled“): Datei, Größe, Ordner,
   Zahl der Bilder und Dauer, wie viele deckend / harte / weiche Transparenz, ausgelassene Dateien, abgelehnte Bilder
   mit Grund (bis 6, der Rest im Bericht), der nächste Schritt in CubeEvil4 (app menu (SELECT) → Cheats & Mods → Mods →
   Import texture pack). Knöpfe „Open folder“ (Explorer mit markierter Datei), „Show report“ (die `.txt` neben der
   Datei), „Back“ (zu den Optionen, z. B. für eine zweite Größe) und „Close“.

Weitere Eigenschaften:
- **Sprache: nur Englisch** (Entscheidung des Projektinhabers, 14.11.2026: „die windows app braucht nur englisch
  sein!“). Keine Spracherkennung, keine weiteren Sprachen. Texte, die es in der Android-Packer-App schon gibt, haben
  dieselben Worte. Der Bericht (`.txt`) ist wie bisher englisch. (Eine erste Fassung mit den 11 Sprachen der
  Android-App wurde danach wieder entfernt.)
- **Hell und dunkel** wie Windows (auch die Titelleiste), Hoher Kontrast mit den Systemfarben. Zum Ausprobieren:
  `-theme light` / `-theme dark`.
- **Scharf auf jedem Bildschirm** (Skalierung je Monitor, auch beim Verschieben auf einen anderen Monitor).
- **Bedienbar mit der Tastatur** (Tab, Leertaste, Enter, Esc bricht das Umwandeln ab); die Knöpfe sind echte
  Windows-Knöpfe, also auch für Bildschirmleser lesbar.
- Das Fenster bleibt während des Lesens und Umwandelns bedienbar: beides läuft in eigenen Threads.
- Zeitschätzung: dieselben Werte wie die Android-App (Ryzen 7 5700X, 16 Threads), geteilt nach den Threads dieses PCs.

## Code

- `port/tools/texpack/gui.cpp`: das Fenster. `gui_text.h/.cpp`: die englischen Texte (90), Zahlen, Größen und
  Zeiten („5,916“, „0.72 GB“, „4 min 22 s“; „1 file“ statt „1 files“). `pc.h`: was Fenster und Konsole teilen
  (Datei schreiben, Speicher, Datum).
  `res/`: Symbol (`packer.ico`, aus dem Zeichen der Android-Packer-App mit `res/make_ico.py`), Versionsangaben
  (`texpack.rc`), Manifest (DPI, Windows-10/11-Dialoge).
- Kern (`packer.h/.cpp`), nur Ergänzungen, die Datei bleibt gleich: der Fortschritt nennt das Bild in Arbeit und die
  bisher abgelehnten, das Ergebnis die abgelehnten Bilder mit Grund, `EstimateEach` liefert die Schätzung je Bild (das
  Fenster rechnet die Größe bei jeder Auswahl ohne neues Lesen nach). Versionsnummer 1.4.
- `build.ps1` baut beide `.exe` und die Tests; `-Out <Ordner>` kopiert beide `.exe`.
- Für die Tests (nicht für Nutzer): `-testdrop <Paket>` übergibt das Paket so an die Ablagefläche, wie der Explorer es
  beim Hineinziehen tut (dasselbe Windows-Datenobjekt); `-testsave <Datei>` nimmt diese Datei statt des
  Speichern-Dialogs.

## Releasetext (Vorlage für das nächste Release mit dem Packer)

Abschnitt „Update“, ersetzt den bisherigen Packer-Absatz:

> **HD textures (optional):** the CubeEvil4 Texture Packer comes as files of its own with this release. All read your
> own Dolphin texture pack as a folder or a ZIP file, with DDS (BC1–BC5, BC7, uncompressed), PNG and JPG images, and
> make the same `.c4tex` file. Sizes: 256 (weak devices, about 0.3 GB), 512 (recommended, about 0.7 GB) or 1024 (strong
> devices, about 2.3 GB); no image gets smaller than the game's own.
>
> - Windows: `CubeEvil4-TexturePacker.exe` 1.4 (no installation). Double-click it, drag the pack's folder or ZIP file
>   into its window, choose a size and where to save; it shows the progress and a summary. Copy the `.c4tex` file to
>   the phone and import it under app menu → Cheats & Mods → Mods. `CubeEvil4-TexturePacker-Console.exe` does the same
>   on the command line.
> - Android: install `CubeEvil4-TexturePacker-1.4.apk` (Android 7 or later), choose the pack's ZIP file or folder and a
>   size, then Start (a phone needs far longer than a PC: keep it on the charger). The file is saved in the Download
>   folder; "Import into CubeEvil4" hands it straight to CubeEvil4.

Zeile für „New“: `- Texture Packer 1.4 for Windows: a real window – drag the pack in, choose the size and where to
save, watch the progress, see a summary at the end; the command line version is now CubeEvil4-TexturePacker-Console.exe.`

Die Android-Packer-App geht mit auf 1.4 (Projektinhaber, 14.11.2026: „Ja, auf 1.4“): `versionName` 1.4,
`versionCode` 2, weil sie denselben Kern übersetzt und „1.4“ in die Datei schreibt. Ihre `.c4tex` bleibt gleich; mit
dem Release kommt `CubeEvil4-TexturePacker-1.4.apk` statt der 1.3.

**Alle drei Werkzeuge nur Englisch** (Projektinhaber, 14.11.2026: „alle 3 tools sollen nur englisch sein“): das Fenster,
die Konsolen-Version (war schon nur Englisch) und die Android-Packer-App (bis 1.3 in der Handy-Sprache, 11 Sprachen;
jetzt nur `res/values`, ohne Sprachliste und ohne Rechts-nach-links). Zeile für „Changed“:
`- Texture Packer for Android 1.4: English only, as the Windows version.`

## Geprüft (14.11.2026, Windows-PC des Projektinhabers)

- `build.ps1` (Visual Studio 2026, x64): beide `.exe` ohne Fehler und Warnungen. Abhängig nur von Windows-eigenen DLLs
  (user32, gdi32, shell32, ole32, comctl32, dwmapi, uxtheme, gdiplus, kernel32, advapi32), keine Laufzeit nötig.
- `texpack_tests`: 601 Prüfungen ohne Fehler, mit allen Testbildern (PNG, BC7, JPG) 966 (vorher 910). Neu: alle 90
  Fenstertexte an ihrem Platz, keiner leer, Platzhalter ohne Lücke; Zahlen, Größen, Zeiten, Einzahl („1 file“);
  `EstimateEach` gleich `EstimateSizes`, Fortschritt mit Bildnamen und abgelehnten Bildern, abgelehnte Bilder mit Grund
  im Ergebnis. Unter Linux (WSL, GCC) ebenfalls 601 ohne Fehler und ohne Warnung.
- `run_e2e.py` gegen `CubeEvil4-TexturePacker-Console.exe`: 36 von 36 Punkten.
- Das HD Project des Inhabers als ZIP (11,9 GB, 5.916 Bilder), Größe 256:
  - im Fenster: Paket gelesen (beim ersten Mal 40 s, die Festplatte war noch kalt), Optionen mit beiden Auswahlgruppen,
    umgewandelt in 2 min 22 s, 271 MB;
  - mit der Konsolen-Version (gleicher Tag): **Byte für Byte dieselbe Datei**;
  - gegen die 1.3-Datei vom 13.11.: genau 2 Bytes anders (Versionsnummer und Datum im Info-Text), alle Bilder gleich.
- Kleines selbst gemachtes Testpaket (48 Bilder, Auswahlgruppe, Zusatzordner, ein kaputtes PNG, ein BC6H-DDS): beide
  abgelehnten Bilder mit Grund auf der Fertig-Seite; aus ZIP und aus Ordner dieselbe Datei.
- Abbrechen-Knopf (Nachfrage, „Abgebrochen“, keine Datei übrig) und Schließen während des Umwandelns (Nachfrage,
  Fenster zu, keine Datei übrig).
- Der echte Speichern-Dialog von Windows: Ordner gewählt, Name gesetzt, gespeichert, umgewandelt. Das Ersetzen einer
  vorhandenen Datei fragt Windows vorher (ausgelöst, mit „Nein“ beendet; die Datei blieb unverändert).
- „ZIP-Datei wählen …“ und „Ordner wählen …“ mit den Öffnen-Dialogen von Windows: führen zur Optionsseite.
- Hineinziehen: mit demselben Windows-Datenobjekt, das der Explorer beim Ziehen liefert (`-testdrop`): Markierung
  „Loslassen …“, danach wird gelesen. Falsche Dateien: `.txt` und `.7z` mit Hinweis.
- Fenster mit Befehlszeilen-Optionen: nennt die Konsolen-Version, Rückgabewert 2.
- Hell und dunkel angesehen (`-theme light` / `-theme dark`).
- Bildschirmfotos (englisch): `E:\Development\Android\builds\converter-tool\screenshots\`, auf einem
  unsichtbaren zweiten Desktop gemacht, damit die Tests den Inhaber nicht stören; dort zeigt Windows keinen
  Fensterrahmen (die Fotos zeigen nur den Inhalt des Fensters; der Speichern-Dialog und die Nachfrage beim Abbrechen
  haben dort den alten Rahmen, und ihre Windows-eigenen Knöpfe sind in der Sprache von Windows).

**Nicht geprüft:**
- Das Ziehen mit der echten Maus aus dem Explorer: Windows lässt diese Sitzung die Maus nicht bewegen. Der Weg dahinter
  (Windows-Datenobjekt → Fenster) ist geprüft; der Inhaber probiert es einmal selbst.
- Andere Bildschirm-Skalierungen als 100 % und das Verschieben auf einen zweiten Monitor (der PC hat nur 100 %).
- Hoher Kontrast, Bildschirmleser, Windows 10 (nur Windows 11 vorhanden).
