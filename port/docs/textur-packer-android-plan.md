# CubeEvil4 Texture Packer für Android: Plan (13.11.2026, Branch `textures`)

**Stand: nur Plan, nichts gebaut.** Wunsch des Projektinhabers (12.11.2026): „Wir brauchen den Texture Converter auch
für Android als einfache App. Erklärung, Button zum Laden des ZIPs oder Ordners, dann wird nach /Download auf dem
Speicher oder Speicherkarte entpackt. Vorher soll geprüft werden ob genug Speicher verfügbar ist.“

Grundlage: das PC-Programm „CubeEvil4 Texture Packer“ 1.2 (`port/tools/texpack/`, Plan `port/docs/textur-lader-plan.md`).

## Was die App macht

Eine eigene, kleine App neben CubeEvil4, nicht ein Teil davon. Ein Bildschirm, von oben nach unten:

1. **Erklärung** (kurz): Was ein Texturpaket ist, dass CubeEvil4 keine Texturen mitliefert und keine Links nennt, dass
   nur selbst beschaffte Pakete verwendet werden dürfen, und wie es danach weitergeht (in CubeEvil4 importieren).
2. **Paket wählen:** zwei Knöpfe „ZIP-Datei wählen“ und „Ordner wählen“ (die Dateiauswahl von Android). Danach zeigt
   die App, was sie gefunden hat: „5.927 Bilder, 11,8 GB“.
3. **Varianten** (nur wenn das Paket welche hat, wie beim HD Project: Kofferfarbe, Tastensymbole): je eine Auswahl,
   wie beim PC-Programm.
4. **Stufe:** 256, 512 (Vorgabe) oder 1024, jeweils mit der geschätzten Größe und Dauer auf diesem Gerät.
5. **Speicherort:** „Interner Speicher“ oder „Speicherkarte“ (nur wenn eingelegt), jeweils mit dem freien Platz. Die
   Datei landet dort im Ordner **Download**.
6. **Start.** Vorher die Platzprüfung (siehe unten). Danach ein Fortschrittsbalken mit Restzeit und „Abbrechen“.
7. **Fertig:** „Gespeichert: Download/CubeEvil4-textures-512.c4tex (716 MB)“ und der Hinweis, die Datei in CubeEvil4
   unter „Cheats & Mods“ zu importieren.

Die Umwandlung läuft auch bei ausgeschaltetem Bildschirm weiter: als Vordergrunddienst mit einer Benachrichtigung
(Fortschritt, „Abbrechen“). Abbrechen oder ein Fehler hinterlässt keine halbe Datei.

## Technik

### Wo und wie die App gebaut wird

- **Ort im Repository:** ein zweites Modul `packer` im bestehenden Android-Studio-Projekt `port/android-decomp/`
  (neben `app`). Du baust es in Android Studio wie CubeEvil4, nur mit der Auswahl „packer“ statt „app“. Gleicher
  Debug-Schlüssel, gleiche NDK-Version.
- **Paketname:** `com.ast.cubeevil4.texturepacker`, App-Name „CubeEvil4 Texture Packer“, eigenes Symbol (abgewandelt
  vom CubeEvil4-Symbol).
- **Android-Version:** ab Android 7.0 (wie CubeEvil4). Ab Android 10 schreibt die App direkt in den Download-Ordner des
  gewählten Speichers. Auf Android 7–9 fragt sie für die Speicherkarte einmal mit dem Speichern-Dialog von Android nach
  dem Ort (der interne Speicher geht dort direkt, mit der Speicher-Berechtigung).
- **Größe der APK:** etwa 1,5–2 MB (32 und 64 bit). Keine fremden Bibliotheken, kein AndroidX.
- **Gleicher Kern wie am PC:** der C++-Code des PC-Programms (`texpack`, `etc2`, `bc7`, `jpeg`, `image`) über das NDK.
  Dafür trenne ich im PC-Programm die Bedienung (Fragen im Fenster) vom Umwandeln. Das Umwandeln bekommt seine Dateien
  dann von „irgendwoher“ (Ordner am PC, ZIP, Android-Ordner). Ein Test prüft, dass das PC-Programm danach Byte für
  Byte dieselben Dateien macht wie jetzt. Handy und PC ergeben dieselbe `.c4tex`, bis auf das Datum im Info-Text.

### ZIP und Ordner (Zugriff über die Dateiauswahl, auch auf der Speicherkarte)

- **ZIP:** Die App liest die ZIP-Datei direkt, ohne sie auszupacken. Das erspart 12 GB Platz und viel Zeit.
  - Sie liest das Inhaltsverzeichnis der ZIP und packt jedes Bild einzeln im Speicher aus.
  - ZIP-Dateien über 4 GB (ZIP64) gehen. Gepackt sein dürfen die Bilder „gespeichert“ oder „Deflate“, die üblichen
    Arten.
  - Der Leser für Deflate ist schon da (aus dem PNG-Leser).
  - Andere Packverfahren in einer ZIP lehnt die App mit Namen ab.
- **Ordner:** Android gibt der App Zugriff auf den gewählten Ordner, mit allen Unterordnern. Ein ausgepacktes Paket auf
  dem Handy oder der Speicherkarte geht also auch.
- **Speicherkarte:** Die Dateiauswahl von Android zeigt sie mit an. Lesen geht ohne besondere Berechtigung.
- **Nicht:** 7z und RAR (Frage 2). Auch keine Dateien, die nur in der Cloud liegen (Google Drive und so weiter): die
  App sagt dann, man soll die Datei zuerst aufs Gerät laden.

### Die Platzprüfung

- **Vor dem Start:** Die App liest von jedem Bild nur den Kopf (Größe, Format, Durchsichtigkeit). Daraus rechnet sie
  die Größe der fertigen Datei mit derselben Regel wie das PC-Programm aus, also nie unter die Originalgröße und mit
  Verkleinerungsstufen.
  - Bei Bildern mit Durchsichtigkeit rechnet sie vorsichtig mit der größeren Art. Die Schätzung liegt also eher etwas
    über der echten Größe; beim HD Project mit 512 etwa 0,8 GB statt 0,72 GB.
- **Zwischendateien:** keine. Die App schreibt die Paketdatei direkt in den Download-Ordner, als versteckte, noch nicht
  fertige Datei. Erst am Ende wird sie sichtbar.
- **Anzeige:** „Braucht etwa 0,8 GB, frei sind 23,4 GB auf der Speicherkarte.“
- **Zu wenig Platz:** Start nicht möglich, mit beiden Zahlen. Dazu der Hinweis, wie viel fehlt, oder dass die kleinere
  Stufe passen würde.
- **Reserve:** Die App braucht die geschätzte Größe plus 100 MB frei.
- **Während der Umwandlung:** Die App prüft den Platz alle paar hundert Bilder noch einmal. Wird er knapp, bricht sie
  sauber ab und löscht die unfertige Datei.

### Dauer (Schätzung)

Am PC (Ryzen 7 5700X, 8 Kerne) gemessen: 256 in gut 2 Minuten, 512 in 4–5 Minuten, 1024 in 16 Minuten.

Ein Handy schafft je nach Chip ein Drittel bis ein Neuntel davon. Bei langer Volllast wird es außerdem warm und drosselt
sich.

| Stufe | Retroid (Snapdragon 865) | Mittelklasse (z. B. Helio G85) |
|---|---|---|
| 256 | etwa 10 Minuten | etwa 20 Minuten |
| 512 | etwa 20 Minuten | etwa 40–45 Minuten |
| 1024 | etwa 70 Minuten | etwa 2,5 Stunden |

- Lesen von der Speicherkarte kostet beim ganzen Paket noch etwa 2–3 Minuten, vom internen Speicher kaum etwas.
- Die echten Zeiten miss der erste Testlauf auf dem Retroid. Die App zeigt die Schätzung für das jeweilige Gerät vor
  dem Start.
- Empfehlung in der Erklärung: das Gerät während der Umwandlung ans Ladekabel hängen.

### Weitere Punkte

- **Sprache:** Die Texte der App sind Englisch. Die Erklärung und die Bedienung folgen der Handy-Sprache in denselben
  11 Sprachen wie das Ingame-Menü von CubeEvil4 (etwa 30 kurze Texte; Prüfung wie bei CubeEvil4 mit
  `check_translations.py`). Englisch ist der Rückfall.
- **Rechtliches:** keine Texturen, keine Links, derselbe Satz wie im README. Der Bericht am Ende (welche Dateien
  übersprungen wurden) liegt wie am PC als `.txt` neben der Paketdatei.
- **Speicher (RAM):** Die App nutzt wie am PC alle Kerne, begrenzt aber den Arbeitsspeicher nach dem Gerät (höchstens
  ein Viertel des RAM). Sehr große Bilder (8192 × 8192) laufen dann nacheinander statt gleichzeitig.
- **Tests:**
  - Am PC: derselbe Ablauftest wie fürs PC-Programm, dazu ZIP-Pakete (gespeichert, Deflate, über 4 GB).
  - App-Tests für Platzprüfung und Abbruch.
  - Auf dem Retroid (du installierst selbst): dein Paket als ZIP und als Ordner. Das Ergebnis vergleiche ich Byte für
    Byte mit der PC-Datei.

## Fragen an dich

1. **„Entpacken“:** Soll die App das Paket wirklich erst auspacken? Empfehlung: nein. Sie liest die ZIP direkt und
   legt in Download nur die fertige `.c4tex` ab. Auspacken bräuchte zusätzlich 12 GB und viel Zeit, und die ausgepackten
   Bilder braucht danach niemand.
2. **Nur ZIP und Ordner?** Empfehlung: ja. 7z und RAR bräuchten fremden Code oder sehr viel eigenen. Wer sein Paket als
   7z oder RAR hat, packt es mit einer anderen App aus und wählt dann den Ordner.
3. **Direkt an CubeEvil4 übergeben?** Empfehlung: ja. Am Ende gibt es zusätzlich einen Knopf „In CubeEvil4
   importieren“. Er öffnet CubeEvil4 und startet dort den Import, ohne dass man die Datei selbst suchen muss. Die Datei
   bleibt trotzdem in Download. Dafür braucht CubeEvil4 eine kleine Ergänzung (im Branch `textures`; in welche Version sie kommt, entscheidest du).
