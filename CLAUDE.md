# Projekt: Resident Evil 4 (GameCube-Debug G4BE08) → Android

## Zusammenarbeit

- Der Projektinhaber programmiert nicht selbst. Antworten auf Deutsch, per Du,
  ohne Fachjargon. Fachbegriffe kurz erklären.
- Texte, die wir in App oder Spiel einbauen (Menüs, Meldungen, Fehlerbericht,
  Dateinamen für Tester), auf Englisch (Entscheidung des Projektinhabers,
  17.10.2026). Ausnahme: Das App-Menü im Spiel folgt der Handy-Sprache
  (Englisch als Rückfall, Entscheidung des Projektinhabers, 21.10.2026; Texte in
  `port/android-decomp/app/src/main/res/values*/strings_ingame.xml`, Prüfung mit
  `port/android-decomp/tools/check_translations.py`). Alle anderen Texte,
  Fehlerbericht und Dateinamen bleiben Englisch. Antworten im Chat bleiben deutsch.
- Am Ende jeder Aufgabe in wenigen Sätzen sagen: was erreicht wurde, was getestet
  wurde (mit Ergebnis), was als Nächstes kommt.
- Vor jeder Grundsatzentscheidung (Architektur, Regeln dieser Datei ändern)
  nachfragen. Kleine technische Entscheidungen selbst treffen.
- Braucht ein Schritt den Windows-PC oder den Retroid (Test, Bericht holen),
  immer die genauen PowerShell-Befehle zum Kopieren mitschicken (Repository
  `E:\Development\Android\RE4-Fixed`, Disc-Daten
  `E:\Development\Android\RE4-Discdata`; Ergebnisse liegen auf dem Desktop in
  `RE4-Kern-App` bzw. `RE4-Kern-Pruefung`). Entscheidung des
  Projektinhabers, 15.10.2026.
- Nach jeder Änderung, die in die App gehört, die Befehle zum Abgleich mit
  GitHub und zum Bauen mitschicken: `git pull`, Kern-Build in WSL, dann Android
  Studio; Vorlage und Hinweise in `port/docs/lokal-bauen.md` (Wunsch des
  Projektinhabers, 22.10.2026).
- Bei jedem neuen Alpha-Release die Änderungen gleich im Chat mitschicken, kurz
  und knackig (englisch, für Tester: Fixed / New / Changed, je ein Satz pro Punkt);
  der Releasetext in `port/android-decomp/docs/releases/` bleibt genauso kurz
  (Entscheidung des Projektinhabers, 21.10.2026).
- Am Ende jedes Meilensteins den fertigen Startprompt für den nächsten
  Meilenstein als Codeblock zum Kopieren ausgeben. Er nennt den Branch, die
  empfohlene Intelligenzstufe und ob die Arbeit in der Cloud oder lokal auf dem
  Windows-PC (mit Retroid/Disc-Daten) laufen muss.

## Ziel und Strategie (verbindlich)

Ziel: das komplette Originalspiel nativ auf Android, mit dem Originalcode aus `src/`.

**Endprodukt (Entscheidung des Projektinhabers, 14.10.2026):** die App
**RE4-Android-Decomp** in `port/android-decomp/` (seit 18.10.2026 heißt die App
**CubeEvil4**, Paketname `com.ast.cubeevil4`, eigenes Symbol statt der Capcom-Grafik;
Entscheidung des Projektinhabers, weil „Resident Evil“ eine Marke ist) (Android Studio 2025,
Gradle-Projekt der App; per Subtree-Merge aus dem Branch `re4-standalone`
übernommen, der Branch selbst bleibt als Vergleichsstand unverändert). Der
Original-Spielkern aus dem Ganzprogramm-Build (`port/whole/`) übernimmt dort
die gesamte Spiellogik. Die App bleibt Hülle: Disc-Import, GLES3-Grafik,
Controller, Tonausgabe, Menü. **Keine Raum-, Gegner- oder Eventlogik mehr von
Hand nachbauen**; die eigenen Spielmodule der App (`r100_scenario.cpp`,
`enemy_encounter.cpp`, `event_playback.cpp` …) werden nicht erweitert und
im Kernmodus abgeschaltet.

- **Kernmodus:** vorerst `armeabi-v7a` (ARM32), weil der Kern nur dort
  ohne Zeiger-Kürzungen läuft (siehe „ARM32 zuerst“).
- **Bisheriger ARM64-Modus** der App (eigene Spielmodule) bleibt parallel
  erhalten und baubar, bis der Kern ihn vollständig ersetzt.
- `port/android/` (alte Port-App, Vulkan) ist nicht mehr das Endprodukt; nur
  noch Fundus für einzelne bewährte Teile.

**Ganzprogramm-Ansatz:** Der gesamte Spielcode (`src/` außer `src/lib/`) wird
auf einmal kompiliert. Ersetzt wird nur die Plattformschicht
(GX, VI, OS, DVD, PAD, AX, CARD, ARAM, CRI-Video). Nicht mehr Funktion für
Funktion extrahieren. Die Generatoren und Fingerprint-Manifeste in
`port/tools/generate_*.py` und `port/config/*-sources.json` werden nicht
erweitert. Sie werden abgelöst, sobald der Ganzprogramm-Build sie abdeckt.

**GX nachbilden (Entscheidung des Projektinhabers, 15.10.2026):** Die App
zeigt, was das Spiel selbst über die Grafikschnittstelle GX zeichnet (Logos,
Titelmenü, Räume, Figuren, Effekte, Untermenüs, Anzeigen, Untertitel). Die
GX-Bibliothek aus `src/lib/` wird mitkompiliert; ihre Befehle (der GX-FIFO)
übersetzt ein Nachbau des Grafikprozessors nach GLES3. Die Zustandsbrücke
(Modelle aus dem Spielzustand nachzeichnen) bleibt Übergang und Diagnose, bis
GX alles zeigt. Plan und Schritte: `port/docs/gx-plan.md`.

**ARM32 zuerst:** Erst `armeabi-v7a` spielbar machen, danach ARM64.
Begründung und Zahlen: `port/docs/whole-program-probe.md`
(ARM32: 133 Fehler, ARM64: zusätzlich ~2900 Zeiger-Kürzungen).

**Stubs sind erlaubt und erwünscht:** Fehlende Plattformfunktionen werden
automatisch als Stub erzeugt, der beim ersten Aufruf einmal ins Log schreibt.
Dann ausführen, Absturzstelle finden, beheben, wiederholen.

## Regeln

- `src/` und `include/` nicht verändern. Der GameCube-Build (`ninja`) muss
  weiterhin die SHA1-Prüfsummen treffen. Anpassungen für den Port laufen als
  mechanische Umschreibungen beim Bauen (`port/tools/whole_prepare.py`),
  als Ersatz-Header (`port/whole/include/`) oder als kleine geprüfte Patches
  (`port/whole/patches/`). Jede neue Regel und jeder Patch wird in
  `port/docs/whole-program-status.md` eingetragen.
- Port-Code liegt in `port/`. Bewährte Teile weiterverwenden: Scheduler
  (`port/src/scheduler.cpp`, `stack_context.cpp`), Datei-I/O, Yz2,
  ADB-Skripte; aus der App (`port/android-decomp/`) Disc-Import,
  GLES3-Renderer, Controller, Tonausgabe und die geprüften Big-Endian-Leser
  (als Vergleich für die Formatumwandler).
- Disc-Daten sind big-endian: beim Laden umwandeln, nie stillschweigend annehmen.
- Fehler des Prototyps (Originalcode, der die SHA1-Prüfsumme trifft), die das
  fertige Spiel nicht hat, werden behoben: als Patch in `port/whole/patches/`,
  im Patch-Kopf als „a bug of the prototype“ gekennzeichnet (Entscheidung des
  Projektinhabers, 22.10.2026).
- Keine Spieldateien und keine Disc-Images ins Repository.

## Testen

- **Cloud (Linux):** 32-bit-Host-Build mit `-m32` (gleiche Zeigerbreite wie
  ARM32). Die Compiler installiert das Startskript
  `.claude/hooks/session-start.sh` automatisch bei jeder Cloud-Sitzung.
  Spiellogik, Laden und Abstürze werden hier geprüft. Disc-Daten gibt es in der
  Cloud nicht. Tests, die sie brauchen, laufen lokal.
- **Lokal (Windows-PC + Retroid Pocket Flip 2 per ADB):** Gerätetests nur über
  die vorhandenen Skripte mit der Sperre aus `port/docs/adb-coordination.md`.
- Ein Meilenstein gilt erst als erreicht, wenn der zugehörige Test wirklich
  gelaufen ist. Nicht Getestetes ausdrücklich so benennen.

## Meilensteine

Fortschritt und Ergebnisse stehen in `port/docs/whole-program-status.md`.
Jede Sitzung liest diese Datei zuerst und aktualisiert sie am Ende.
Bauen und prüfen: `port/tools/whole_build.sh`.

1. ✅ Probelauf Ganzprogramm-Kompilierung (`port/docs/whole-program-probe.md`)
2. ✅ Alle 600 Spieldateien kompilieren für ARM32 und 32-bit-Linux fehlerfrei
3. ✅ Linken: Stub-Generator für alle fehlenden SDK/CRI-Funktionen, lauffähiges
   32-bit-Linux-Programm, jedes REL-Modul als eigene Bibliothek
4. ✅ Booten mit echten Disc-Daten bis `gameRoomInit` (vorhandene Datei/Yz2/Scheduler nutzen)
   — Teil A ✅ (Cloud: Plattformschicht ohne Disc-Daten), Teil B ✅ (lokal mit Disc-Daten, i686 und armhf/qemu)
4.5 ✅ Kern korrekt machen: C-Ersatz für Assembler-Funktionen, Byte-Aggregate,
   `__FILE__`-Länge (Stand in der Status-Datei)
5. ✅ Erster Raum (r100): Spielschleife läuft 10.000 Bilder ohne Absturz, noch ohne Bild
   (lokal mit Disc-Daten, i686; Formatvergleich und Abdeckung ohne Fehler)
6. ✅ K2 „Kern in die App“: ARM32-Build von RE4-Android-Decomp mit den
   Kern-Bibliotheken, Zustandsbrücke zum GLES3-Renderer der App,
   Titelbild und r100 sichtbar (Retroid, 16.10.2026)
6b. ✅ GX nachbilden (`port/docs/gx-plan.md`): Logos, Titelmenü und r100 so,
   wie das Spiel sie zeichnet (Retroid, 16.10.2026; r101 flüssig bei 1080p;
   offene Grafikfehler in der Status-Datei)
7. ✅ Steuerung: Retroid-Tasten wie der GameCube-Controller, Vibration
   (Retroid, 16.10.2026; Touch später)
8. ✅ Audio: SDK-Tonbibliotheken aus `src/lib/` unverändert, DSP/AI nachgebaut
   (Retroid, 16.10.2026; CRI-ADX gehört zu den Filmen)
9. ✅ Videos (SFD/MPV): CRI-Middleware aus `src/lib`, Bild über GX, ADX-Ton,
   START überspringt (Retroid und PC-Prüfung, 16.10.2026)
10. ✅ Speichern/Laden: CARD-Bibliothek aus `src/lib`, Speicherkarte als Datei
   in der App, Speichern an der Schreibmaschine, Laden, Systemdaten
   (Retroid und PC-Prüfung, 17.10.2026)
11. ✅ Android-Feinschliff (vor 10b, Entscheidung des Projektinhabers, 17.10.2026:
   danach geht die APK an Tester): Menü der App über SELECT (Grafikoptionen,
   Speicherkarten-Import/Export `.gci` für Dolphin und GameCube,
   Fehlerprotokoll, Tiefenschärfe mit Autofokus), SELECT 1 Sekunde halten =
   Debug-Menü des Spiels, Diagnose-Anzeige ausgebaut, Leistung, Nachladen der
   Super Magnum geklärt (Retroid und PC-Prüfung, 17.10.2026)
12. ✅ ARM64: Zeiger-Kürzungen beheben, eine APK für 32- und 64-bit-Geräte
   (vor 10b, Entscheidung des Projektinhabers, 17.10.2026: ein Tester hat ein
   Gerät, das nur 64-bit-Apps ausführt; erst M11 abschließen). Weg
   (Entscheidung des Projektinhabers, 17.10.2026, nach der Bestandsaufnahme
   `port/docs/arm64-bestandsaufnahme.md`): **Weg A, nativ 64 bit** – alles,
   worauf der Spielcode zeigt, liegt unter 4 GB; Strukturen mit fester Form
   (feste Plätze, Spielstand, Daten von der Disc) behalten ihre 32-bit-Form
   (abgeschlossen nach Entscheidung des Projektinhabers, 27.10.2026: eine APK mit beiden ABIs seit
   0.9.0-beta, ARM64 auf dem Retroid gespielt)
10b. ✅ Raum-Rundgang: viele Räume per Raumauswahl anspielen, Abstürze und
   Grafikfehler sammeln (Entscheidung des Projektinhabers, 16.10.2026: nach
   Ton, Videos und Speichern/Laden; seit 17.10.2026 nach M11 und M12; seit 27.10.2026 mit beiden Disc-Arten, Branch
   `retail-eu-testrun`). Stand 27.10.2026: Rundgang am PC mit Debug- und EU-Discs je 126/126 (Stage 4 mit EU-Daten,
   Patch 0131), die drei EU-Spielmeldungen geklärt (0132 Ereignis-Untertitel auf Englisch), Funksprüche 13/14/15/17,
   achte Händler-Markierung (0133) und Cheats im Rundgang am PC geprüft; auf dem Retroid zwei weitere
   Kapitel ohne Befund, Bosse mit One-Hit-Kill bestätigt; Controller-Konfiguration
   (A, B, L2, R2, START, SELECT, Steuerkreuz fest; X, Y, L1, R1, L3, R3 frei; Vorschlag des Projektinhabers 27.10.2026) auf
   dem Retroid bestätigt; ARM32 Speichern/Laden auf dem Retroid bestätigt, abgeschlossen 27.10.2026 (Zwischenstufen 1,5×/2,5×/3,5× und Ruckler-Korrektur auf dem Retroid bestätigt)
13. ✅ Retail-Discs (EU): CubeEvil4 spielt auch mit den europäischen Verkaufs-Discs (`G4BP08`),
   parallel zur Debug-Disc, die Referenz bleibt (Entscheidungen des Projektinhabers, 26.10.2026:
   parallel, kein Mischen der Versionen; Debug-Menü mit EU-Discs ausgeblendet, dafür ein Cheat-Menü im
   Ingame-Menü, am PC geprüft 27.10.2026, `port/docs/cheat-menu-plan.md`; alle Sprachen der EU-Discs spielbar, Schritt R7, gewählt im
   Ingame-Menü der App, „LANGUAGE SELECT“ aus dem Titelmenü entfernt). Plan und
   Schritte R0–R8: `port/docs/retail-eu-plan.md`, Branch `retail-eu`; R0, R1, R2 ✅ (26.10.2026);
   R4 (App-Import) und R7 (Sprachen) auf dem Retroid bestätigt (27.10.2026); R3 (Texttabellen,
   Funkspruch-Leiste) auf dem Retroid bestätigt, R6 (kein Debug-Menü) am PC geprüft, R5 108/126 Räume (Stage 4
   offen), R8 weitgehend: ARM32 und ARM64 auf dem Retroid, Version 0.92.0-beta (27.10.2026); abgeschlossen 27.10.2026: zwei Kapitel mit
   EU-Discs auf Deutsch auf dem Retroid ohne Befund durchgespielt, Cheat-Menü auf dem Retroid bestätigt;
   Stage 4 mit EU-Daten in der Prüfstrecke und drei Spielmeldungen gehen in den Raum-Rundgang 10b

## Git

Arbeitsbranch nach Vorgabe der Sitzung. Nach jedem abgeschlossenen Schritt
committen und pushen. Commit-Nachrichten auf Englisch, kurz und beschreibend.
