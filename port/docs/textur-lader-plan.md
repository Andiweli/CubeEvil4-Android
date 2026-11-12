# Textur-Lader für eigene HD-Texturpakete: Plan (12.11.2026, Branch `textures`)

**Stand: Plan vom Projektinhaber freigegeben (12.11.2026, Entscheidungen in Abschnitt 9).** In welche Version der
Lader kommt, entscheidet er später.

Grundlage ist die Prüfung der Mods vom 12.11.2026 (`E:\Development\Android\reports\mod-extensions\bericht.md`, Skripte
und Zahlen in `...\mod-extensions\probe\`).

## Worum es geht

Spieler, die selbst ein HD-Texturpaket im Dolphin-Format haben (zum Beispiel das „Resident Evil 4 HD Project“ für
Dolphin), sollen es in CubeEvil4 nutzen können. CubeEvil4 liefert dafür nur die Funktion, keine Bilder. So macht es
auch Dolphin.

Drei Teile:

1. **Ein PC-Programm** wandelt das Paket in eine Form um, die Handys schnell anzeigen können, und verkleinert es.
   Heraus kommt **eine einzige Datei**.
2. **Die App importiert diese Datei** über die Dateiauswahl von Android und legt sie in einem eigenen Ordner ab.
3. **Im Spiel** ersetzt die App jedes Bild, für das es im Paket ein Ersatzbild gibt.

Begriffe, kurz erklärt:

- **Textur:** ein Bild, das auf ein 3D-Modell „geklebt“ wird (Holz, Haut, Stoff, Schrift im Menü).
- **Kennung:** ein Fingerabdruck, aus dem Bild selbst berechnet. Dolphin benennt die Ersatzbilder danach
  (`tex1_<Breite>x<Höhe>_<Kennung>_<Format>`). So findet das Programm das passende Ersatzbild, ohne Dateinamen des
  Spiels zu kennen.
- **ETC2:** ein platzsparendes Bildformat, das jeder Handy-Grafikchip mit OpenGL ES 3 direkt anzeigen kann. Das Paket
  liegt in **BC1/BC3** vor, einem PC-Format, das viele Handys (vor allem mit Mali-Chip) nicht können.
- **Mipmaps (Verkleinerungsstufen):** zu jedem Bild zusätzlich die halb, viertel, achtel so große Fassung. Der
  Grafikchip nimmt in der Ferne die kleine Fassung. Ohne sie flimmern scharfe Bilder in der Ferne.
- **Grafikspeicher:** der Speicher, in dem die Bilder für den Grafikchip liegen. Auf Handys ist das derselbe
  Arbeitsspeicher, den auch Android und die App brauchen.

## Vorgaben des Projektinhabers (12.11.2026)

- Der Lader kommt in den Menüpunkt „Cheats“; der heißt künftig **„Cheats & Mods“** (in allen 11 Sprachen).
- Die Texturdaten liegen **eigenständig** in einem eigenen Ordner im Datenbereich der App. Sie überschreiben **nie**
  Originaldaten (importierte Discs, Spielstände, Speicherkarten). Ausschalten und Löschen gehen jederzeit.
- Ein **PC-Programm** wandelt das Paket um und verkleinert es. Die App importiert nur das fertige, kleine Paket.
- **Rechtlich:** CubeEvil4 liefert keine Texturen mit und nennt keine Download-Links. Nichts davon kommt ins Repository
  oder in die APK, nur die Funktion. README und Hilfetext sagen das klar.

## 1. Das PC-Programm „CubeEvil4 Texture Packer“

### Bedienung

**Entschieden:** eine `.exe` für Windows, ohne Installation, ohne Python. Doppelklick öffnet ein Fenster, das
alles nacheinander fragt. Die Texte sind englisch (wie alle Texte für Tester):

```
CubeEvil4 Texture Packer 1.0
Drag your texture pack folder into this window and press Enter:
> E:\...\Resident Evil 4 HD Project
Found 5,927 images (11.8 GB) in 1 pack folder and 2 groups of variants.
Size on the phone (the longer side of the images; never smaller than the game's own):
  1 = 256   (saves memory and space on weak phones; big textures are not sharper than the game's)
  2 = 512   (recommended, for every device)
  3 = 1024  (for strong devices with enough space)
Choose [2]: 2
Attaché case: 1 Original, 2 Black, 3 Classic, 4 Gold, 5 Leather
> 1
Button icons: 1 GameCube, 2 Xbox, 3 Xbox One - Series
> 1
Converting ... 2,310 of 5,690 (41 %), about 6 minutes left
Done: CubeEvil4-textures-512.c4tex on your desktop (812 MB).
Copy this file to your phone, then in CubeEvil4: Menu, Cheats & Mods, Import texture pack.
```

Für Tests und für dich geht dasselbe mit einem einzigen PowerShell-Befehl, ohne Fragen:

```powershell
& "E:\Development\Android\builds\textures\CubeEvil4-TexturePacker.exe" -in "E:\Development\Android\RE4-Mods\Resident Evil 4 HD Project" -size 512 -out "E:\Development\Android\builds\textures"
```

### Was es tut

1. **Sucht alle Bilder** im Ordner und in allen Unterordnern, die nach dem Dolphin-Schema benannt sind. Andere Dateien
   (`desktop.ini`, Doppel wie „(1)“ oder „- Copy“) lässt es weg und zählt sie im Bericht auf.
2. **Varianten:** Hat das Paket einen Ordner `Extra` mit Gruppen von Varianten (beim HD Project: 5 Kofferfarben und
   Tastensymbole für GameCube, Xbox, Xbox One), fragt es je Gruppe nach einer Wahl. Ohne Wahl bleibt, was im
   Hauptordner liegt (Original-Koffer, GameCube-Tasten).
3. **Liest** DDS (BC1–BC5, BC7 und unkomprimiert; nicht BC6H, ein Format für HDR-Bilder), PNG und JPG (Baseline und
   Progressive, Grau und Farbe; nicht CMYK, mit klarer Meldung). Gibt es ein Bild mehrfach, gilt DDS vor PNG vor JPG
   (Entscheidung des Projektinhabers, 12.11.2026: „Ja, nachrüsten“; viele neuere Dolphin-Pakete speichern BC7, manche
   JPG).
4. **Verkleinert** jedes Bild, dessen längere Seite über der gewählten Stufe liegt, auf diese Stufe. Das
   Seitenverhältnis bleibt.
5. **Mipmaps:** erzeugt für jedes Bild alle Verkleinerungsstufen neu. Ausnahme: die 16 Bilder mit eigenen,
   absichtlich anderen Stufen (Dolphin-Kennzeichen `_arb`, etwa für Wasser in der Ferne) behalten ihre. Bei Bildern mit
   Durchsichtigkeit (Blätter, Gitter, Haare) achtet es darauf, dass sie in der Ferne nicht ausdünnen.
6. **Wandelt in ETC2 um.** Je nach Bild in eine von drei Arten: ohne Durchsichtigkeit (4 Bit je Bildpunkt), mit
   „ganz oder gar nicht durchsichtig“ (4 Bit, wie BC1 mit Durchsichtigkeit) oder mit weichem Übergang (8 Bit).
7. **Schreibt eine Datei** `CubeEvil4-textures-<Stufe>.c4tex` mit einem Inhaltsverzeichnis nach Kennung vorne und den
   Bildern dahinter. Die App muss so nicht 6000 Einzeldateien öffnen, sondern nur eine, und findet jedes Bild sofort.
   Die Datei entsteht erst unter einem vorläufigen Namen und wird erst am Ende umbenannt: Ein abgebrochener Lauf
   hinterlässt nie eine halbe Paketdatei, die wie eine fertige aussieht.
8. **Bericht** am Ende: wie viele Bilder umgewandelt, weggelassen (mit Grund), wie groß, wie lange.

Es **verändert nie den Paket-Ordner** und schreibt nichts hinein. Es prüft vorher den freien Platz am Zielort.

### Stufen

| Stufe (längste Seite) | Größe auf dem Handy (HD Project, gemessen) | Für wen |
|---|---|---|
| **256** | 0,27 GB | Sparstufe für schwache Handys (Entscheidung 12.11.2026); große Bilder sind nicht schärfer als das Original |
| **512** | 0,72 GB | jedes Gerät; die meisten Bilder 2- bis 4-mal schärfer als das Original |
| **1024** | 2,34 GB | starke Geräte mit genug Speicherplatz |
| 2048 | etwa 6,8 GB (Schätzung) | **nicht angeboten** (Entscheidung 12.11.2026): kaum ein Handy hat dafür Platz und Grafikspeicher |

**Nie kleiner als das Original** (Entscheidung des Projektinhabers, 12.11.2026): Eine Stufe verkleinert kein Bild unter
die Größe des Originalbilds im Spiel (die Größe im Dolphin-Namen), in keiner Richtung. Ein Vollbild mit 640 × 360
bleibt also auch bei 512 und 256 mindestens 640 × 360. Ist schon das Bild im Paket kleiner als das Original, bleibt
es so groß, wie es ist. Ebenso nimmt „Sharpness: Half“ in der App nie eine Stufe, die kleiner als das Original ist.

### Format: ETC2 (meine Entscheidung, kleine technische Frage)

- **ETC2** kann jedes Gerät, auf dem CubeEvil4 läuft (OpenGL ES 3 schreibt es vor), auch im Vulkan-Weg der Handys.
- **ASTC** wäre bei Bildern mit Durchsichtigkeit etwas kleiner und schöner, ist aber nicht überall vorhanden. Ich
  baue es vorerst nicht. Zeigen die Tests, dass ETC2 sichtbar schlechter aussieht, kommt ASTC als Wahl dazu.

### Dauer und Platz am PC

- Geschätzt 5–20 Minuten für das ganze Paket, je nach PC (es nutzt alle Prozessorkerne). Gemessen wird beim ersten
  Lauf.
- Platz am Ziel: so groß wie die Paketdatei (0,8 bzw. 2,7 GB).

### Technik (für die Akten)

- Ein kleines C++-Programm in `port/tools/texpack/`, übersetzt mit Visual Studio für Windows: eine `.exe` ohne
  Zusatzdateien und ohne Installation (Entscheidung des Projektinhabers, 12.11.2026: nur eine PC-Fassung, keine
  Linux-Fassung). Für die Prüfungen in WSL lässt sich derselbe Code auch unter Linux übersetzen.
- **Alles eigener Code** (CC0), ohne fremde Bausteine: DDS- und BC-Leser (mit BC7), PNG- und JPG-Leser, Verkleinern, Mipmaps,
  ETC2-Umwandler, Paketdatei. So braucht es keine fremden Lizenztexte und keine Downloads (meine Entscheidung,
  12.11.2026; im ersten Entwurf standen bcdec, stb_image und etcpak).
- Das Programm enthält keine Bilder und keine Daten des Spiels.
- Die Form der Paketdatei (Kopf, Inhaltsverzeichnis, Prüfsumme) steht in `port/docs/textur-paket-format.md`.

## 2. Import in der App

### Das Menü „Cheats & Mods“

Der Menüpunkt „Cheats“ heißt künftig „Cheats & Mods“ (deutsch „Cheats & Mods“, die anderen Sprachen sinngemäß). Unter
den bisherigen Cheats kommt ein neuer Abschnitt **„Mods“** mit dem Block **„HD textures“**:

```
Mods
HD textures
  Pack: CubeEvil4-textures-512 · 5,690 images · 812 MB · imported 12.11.2026
  [ Import texture pack ]  [ HD textures: On ]
  [ Sharpness: Full ]      [ Delete texture pack ]
  (Hilfetext)
```

- **Import texture pack:** öffnet die Dateiauswahl von Android (wie beim Import der Discs und Speicherkarten).
- **HD textures: On/Off:** schaltet die Ersatzbilder an und aus, sofort, auch mitten im Spiel.
- **Sharpness: Full / Half:** „Half“ nimmt aus demselben Paket die nächstkleinere Stufe (aus einem 1024er-Paket wird
  ein 512er). Das spart Grafikspeicher und Ladezeit, nicht Speicherplatz. Für schwache Geräte.
- **Delete texture pack:** löscht nach einer Rückfrage nur den Texturordner.
- **Größe:** die Zeile oben zeigt Name, Anzahl der Bilder, Größe und Datum.
- Ohne Paket zeigt der Block nur „No texture pack imported.“ und den Import-Knopf.
- Die Kurzbeschreibung des Menüpunkts auf der Startseite nennt die HD-Texturen mit.

### Der Hilfetext (englisch, 11 Sprachen)

Etwa: „HD textures replace the game's images with sharper ones from a texture pack in the Dolphin format. CubeEvil4
does not include any textures and does not link to any. Use only packs you obtained yourself and may use. Convert
the pack on a PC with the CubeEvil4 Texture Packer first, then import the .c4tex file here. The pack is kept in its own
folder; it never changes your discs, saves or memory cards. Delete it here at any time.“

### Wie der Import abläuft

1. Dateiauswahl, die Datei wählt man selbst (zum Beispiel aus „Download“).
2. Die App liest zuerst nur den Kopf der Datei: Ist es eine CubeEvil4-Paketdatei, passt die Version? Sonst bricht sie
   mit einer klaren Meldung ab.
3. **Platzprüfung:** Die App braucht die Größe der Datei und 100 MB Reserve. Reicht der Platz nicht, sagt sie, wie
   viel fehlt, und kopiert nichts.
4. **Kopieren** mit Fortschrittsanzeige im Menü, abbrechbar. Erst unter einem vorläufigen Namen, am Ende geprüft
   (Prüfsumme des Inhaltsverzeichnisses, Länge) und dann umbenannt. Ein abgebrochener Import hinterlässt nichts.
5. Nach dem Import sind die HD-Texturen an. Die Datei in „Download“ kann man danach löschen, die App braucht sie nicht
   mehr.

**Immer nur ein Paket:** Ein neuer Import ersetzt das alte Paket nach einer Rückfrage, erst wenn das neue vollständig
da ist.

### Wo die Daten liegen

- Im Datenordner der App, in einem **eigenen Unterordner `textures/`**, neben `game/` (Discs) und den Speicherkarten,
  nie darin. Im Ordner liegen nur die Paketdatei und eine kleine Liste, welche Bilder jeder Raum braucht (siehe 3).
- Der Code für Texturen schreibt und löscht nur in `textures/`. Ein Test prüft, dass Import und Löschen nichts
  anderes anfassen.
- Deinstallieren der App löscht den Ordner mit, wie die anderen Daten der App.
- Der Fehlerbericht nennt, ob ein Paket an war (Name, Stufe, Anzahl), damit wir Berichte mit Paket erkennen.

## 3. Laden im Spiel

### Das passende Ersatzbild finden

- Jedes Bild geht beim ersten Gebrauch einmal an die Grafikschicht der App (`GxCore::Upload`). Dort sieht die App
  dieselben Rohdaten wie Dolphin und bildet dieselbe Kennung (XXH64 über die erste Bildstufe, bei Bildern mit
  Farbtabelle dazu über den benutzten Teil der Farbtabelle). Das kostet je Bild Mikrosekunden.
- Mit der Kennung schlägt sie im Inhaltsverzeichnis nach (liegt klein im Arbeitsspeicher, etwa 300 KB). Kein Treffer:
  alles wie heute.
- Bilder, die das Spiel selbst zur Laufzeit erzeugt (Kopien des Bildschirms für Spiegelungen, Hitzeflimmern,
  Tiefenschärfe), gehen nie diesen Weg und **bleiben unverändert**.

### Richtig anzeigen

- Das Spiel rechnet weiter mit der Originalgröße des Bildes; das Ersatzbild füllt dieselbe Fläche, nur mit mehr
  Bildpunkten.
- Die Verkleinerungsstufen werden so begrenzt, dass ein Ersatzbild nie unschärfer wird als das Original und
  absichtliche Unschärfe des Spiels bleibt.

### Nachladen ohne Ruckeln (entschieden 12.11.2026)

Ein Ersatzbild muss erst aus der Paketdatei gelesen werden. Je nach Stufe sind das je Raum etwa 20–30 MB (512) bzw.
70–100 MB (1024), geschätzt. Auf einem schnellen Handy ist das ein Bruchteil einer Sekunde.

**So wird es gebaut:**

- **Solange das Bild schwarz ist oder stillsteht** (Ladebildschirm, Türen, Raumanfang, Koffer und Karte): Die App
  lädt das Ersatzbild sofort und wartet darauf. Das sieht niemand.
- **Mitten im Spiel** (ein Gegner taucht neu auf, ein Effekt kommt dazu): Die App zeigt erst das Original und lädt das
  Ersatzbild im Hintergrund nach; es wird einen Augenblick später scharf. So ruckelt das Spiel nie.
- **Gedächtnis je Raum:** Die App merkt sich, welche Ersatzbilder ein Raum braucht. Beim nächsten Betreten lädt sie
  diese schon während des Ladebildschirms vor. Ab dem zweiten Besuch gibt es so fast kein Nachladen mehr.

### Grenze für den Grafikspeicher

- Ersatzbilder belegen höchstens: 256 MB auf Geräten bis 4 GB Arbeitsspeicher, 512 MB bis 6 GB, 768 MB darüber, und
  höchstens 192 MB in der 32-bit-Fassung (dort ist der Adressraum knapp).
- Ist die Grenze erreicht, bleiben weitere Bilder original, bis ältere nicht mehr gebraucht werden. Das Spiel gibt
  Bilder, die es 10–20 Sekunden nicht mehr gebraucht hat, ohnehin frei.
- Ersatzbilder, die gerade nicht gebraucht werden, bleiben im Rahmen der Grenze noch eine Weile liegen, damit ein
  schneller Raumwechsel hin und zurück nicht neu lädt.

### Beide Grafikwege

- **OpenGL ES (GLES):** ETC2 geht immer.
- **Vulkan:** ETC2 geht auf allen üblichen Handy-Grafikchips (Adreno, Mali, PowerVR). Kann ein Gerät es im Vulkan-Weg
  nicht (selten), sagt das Menü: „HD textures need the OpenGL renderer on this device.“ Das Spiel läuft dann mit den
  Originalbildern weiter.
- Beim Ein- und Ausschalten schickt das Spiel seine Bilder einmal neu an die Grafikschicht. Das gibt es im Kern schon
  (etwa wenn die App aus dem Hintergrund zurückkommt); die App bekommt dafür nur einen kleinen Auslöser. Das ist ein
  kurzer Moment ohne Folgen.

### Was original bleibt

- Bilder, die das Paket nicht hat (gut die Hälfte der Bilder; vor allem kleine Schatten-, Licht- und Effektbilder).
- Bilder mit Text in anderen Sprachen (das Paket ist für die US-Fassung gemacht; deutsche Menütexte der EU-Discs
  bleiben original).
- Bilder, die das Spiel selbst erzeugt (siehe oben).
- **Im PSone-Modus** bleiben die Originalbilder (der Modus will ja den groben Look). Meine Entscheidung; sag Bescheid,
  wenn du es anders willst.

## 4. Leistung auf schwachen Geräten (Mali-G52, 3–4 GB Arbeitsspeicher)

| | Stufe 512 | Stufe 1024 |
|---|---|---|
| Speicherplatz | etwa 0,8 GB | etwa 2,7 GB |
| Grafikspeicher je Raum (Schätzung) | etwa 20–30 MB | etwa 70–100 MB |
| Laden je Raum (Schätzung, beim ersten Besuch) | etwa 0,1–0,3 s | etwa 0,3–1 s |
| Bildrate | kaum Unterschied erwartet | kleiner Verlust möglich |

- Die Ersatzbilder liegen **komprimiert** im Grafikspeicher (4–8 Bit je Bildpunkt). Die Originale liegen dort heute
  ausgepackt (32 Bit je Bildpunkt). Darum braucht ein 512er-Paket voraussichtlich nur etwa so viel bis doppelt so viel
  Grafikspeicher wie heute. Das wird gemessen.
- Der Grafikchip liest komprimierte Bilder sparsam. Wo ein Ersatzbild klein auf dem Bildschirm erscheint, nimmt er die
  kleine Verkleinerungsstufe. Großer Mehraufwand entsteht nur bei Nahaufnahmen.
- Für Geräte mit Mali-G52 und 3–4 GB: Stufe 512, bei Bedarf „Sharpness: Half“.
- **Benchmark und Leistungsprofile:** Der Benchmark misst mit den Einstellungen, wie sie sind, also auch mit den
  HD-Texturen, wenn sie an sind. Das ist der ehrliche Wert. Sein Ergebnis vermerkt, ob sie an waren. Empfiehlt er
  „Low“, schlägt das Menü „Sharpness: Half“ vor (nicht erzwungen).
- Die Zahlen oben sind Schätzungen aus der Mod-Prüfung. Gemessen wird am PC und auf dem Retroid (Schritt T7).

## 5. Disc-Arten

Die Kennung hängt nur am Bild selbst, nicht an der Disc. Ein Umbenennen ist nicht nötig. Aus der Mod-Prüfung:

| Discs | Wie viel vom Paket passt (nach Größe) |
|---|---|
| US (`G4BE08`, für sie ist das Paket gemacht) | 99,4 % |
| Debug (unsere Referenz) | 98,9 % |
| EU (`G4BP08`) | 99,2 % |
| JP | 98,6 % |

Der Name des Paketordners (`G4BE08`) spielt für CubeEvil4 keine Rolle.

## 6. Tests

**Am PC mit deinem echten Paket** (nur gelesen, der Mod-Ordner bleibt unverändert):

- Das PC-Programm wandelt dein Paket mit Stufe 512 und 1024 um. Das Ergebnis liegt außerhalb des Repositorys in
  `E:\Development\Android\builds\textures\`. Bericht: Anzahl, Größe, Dauer, weggelassene Dateien.
- Ein Prüfwerkzeug liest die Paketdatei zurück und wandelt einige Bilder zur Ansicht in PNG um (Vergleich mit dem
  Original aus dem Paket).
- Im Spielkern am PC (wie bei den bisherigen Bildvergleichen) mit Debug-, EU- und US-Discs: Wie viele der hochgeladenen
  Bilder treffen in r100, r101, r103, einem Burg- und einem Inselraum, im Koffer und im Titelmenü?
- **Vorher/Nachher-Bilder** dieser Räume, nebeneinander.
- **Messungen:** Grafikspeicher je Raum, Ladezeit beim ersten und zweiten Besuch, längste Bildzeit beim Raumwechsel.
- Prüfungen für die App: falsche oder beschädigte Paketdateien (abgeschnitten, falsche Version, unsinnige Zahlen im
  Inhaltsverzeichnis) führen zu einer Meldung, nie zu einem Absturz; Import und Löschen fassen nur `textures/` an;
  zu wenig Platz bricht vor dem Kopieren ab.
- `check_translations.py` für die neuen Texte, die bisherigen App-Tests.

**Auf dem Retroid** (nur nach deiner Freigabe über die Hauptsitzung, weil die Installation ein laufendes Spiel
beendet): Test-APK aus `E:\Development\Android\builds\textures\`, Import der 512er-Datei, ein Kapitel anspielen, GLES
und Vulkan, An/Aus mitten im Spiel, Benchmark mit und ohne. Die genauen PowerShell-Befehle bekommst du dann.

**Nicht testen kann ich:** schwache Geräte (Mali-G52); das bräuchte einen Tester.

## 7. Schritte

| Schritt | Inhalt | Test |
|---|---|---|
| T1 | PC-Programm: Lesen, Varianten, Verkleinern, Mipmaps, ETC2, Paketdatei, Bericht | dein Paket mit 512 und 1024 umgewandelt, Bilder zurückgelesen |
| T2 | Kennung in `GxCore::Upload`, Nachschlagen im Paket, Trefferliste im Protokoll | Trefferquote je Raum am PC, alle drei Disc-Arten |
| T3 | Ersatzbilder anzeigen (GLES), Verkleinerungsstufen, Grenze für den Grafikspeicher | Vorher/Nachher-Bilder am PC |
| T4 | Nachladen ohne Ruckeln: Warten bei schwarzem Bild, Hintergrund, Gedächtnis je Raum | Ladezeiten und längste Bildzeit am PC |
| T5 | Vulkan-Weg | Bilder am PC (Vulkan), gleiche Trefferzahl |
| T6 | App: Menü „Cheats & Mods“, Import mit Platzprüfung, An/Aus, Schärfe, Löschen, Größe, Hilfetext, 11 Sprachen, README | App-Tests, Übersetzungsprüfung |
| T7 | Test-APK nach `builds\textures\`, Retroid-Test (nach Freigabe), Messungen | auf dem Retroid |
| T8 | Status-Datei, Releasetext-Entwurf; du entscheidest die Version | – |

Nach jedem Schritt: Commit nach `origin/textures`, und ich sage ehrlich, was getestet ist und was nicht. Neue Commits aus
`09912` hole ich regelmäßig per Merge herein (vor allem Cheat-Menü und Texte), damit es später ohne Konflikte
zusammengeht.

## 8. Rechtliches

- CubeEvil4 enthält **keine Texturen**, auch keine Beispiele, und nennt **keine Download-Links** (weder in der App
  noch im README noch in Releases).
- Paketdateien (`*.c4tex`) und umgewandelte Bilder kommen nie ins Repository und nie in die APK. Die `.gitignore`
  schließt sie aus; Tests nutzen nur selbst erzeugte Testbilder (bunte Muster) oder liegen außerhalb des Repositorys.
- Das PC-Programm enthält keine Bilder oder Daten des Spiels, nur das Umwandeln.
- **README:** ein Abschnitt „HD texture packs (optional)“ und ein Punkt unter „Legal“, etwa: „CubeEvil4 includes no
  replacement textures and links to none. The texture loader only reads packs you converted yourself from material
  you obtained yourself, as in Dolphin.“
- Der Hilfetext in der App sagt dasselbe kurz (siehe 2).

## 9. Entscheidungen des Projektinhabers (12.11.2026)

Auf die drei Fragen des Plans: „Es reicht eine PC Version. Zu den restlichen Empfehlungen ‚OK‘.“

1. **PC-Programm:** eine `.exe` für Windows (Doppelklick, fragt alles im Fenster), als eigene Datei beim Release auf
   GitHub. Keine Linux-Fassung.
2. **Stufen:** 512 (Vorgabe, etwa 0,8 GB) und 1024 (etwa 2,7 GB), kein 2048. In der App „Sharpness: Half“ für
   schwache Geräte. Nachtrag (12.11.2026, „Ja, plus Stufe 256“): eine Stufe verkleinert nie unter die Originalgröße,
   „Half“ auch nicht; dazu die Sparstufe 256.
3. **Nachladen:** bei schwarzem oder stehendem Bild warten, mitten im Spiel im Hintergrund nachladen (kurz unscharf,
   nie ein Ruckler), ab dem zweiten Besuch eines Raums vorab laden.
