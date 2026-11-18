# Echtzeit-Schatten und Raytracing: Machbarkeitsstudie

Stand 15.11.2026, Branch `realtime-shadows` (Basis 9d46444b = 0.99.18-rc). Die Studie ist für die Zeit nach 1.0
gedacht (Feature-Freeze, Entscheidung des Projektinhabers vom 14.11.2026). Gebaut wurde nichts für die App. Den
Versuch gab es nur im PC-Zeichenwerkzeug `gx_chunk_render`, in einem eigenen WSL-Ordner. Vergleichsbilder:
`E:\Development\Android\builds\realtime-shadows\`. Versuchs-Code und Messwerkzeuge: `port/docs/echtzeit-schatten/`.

> **Nachtrag 15.11.2026: Weg B ist eingebaut, aber nur in Debug-Builds bis nach 1.0.** Entscheidung des
> Projektinhabers: „Wir bauen sie aktuell ein, sie sind aber nur in der Debug Version die ich nutze aktiviert, nicht
> wenn man Release baut. Erst nach 1.0 kommen sie ins Release.“
>
> - Im Debug-Build (coreDebug) gibt es unter Leistung die Zeile „Echtzeitschatten · Aus / Figuren“. Sie erscheint nur
>   auf freigegebenen Geräten (T7) und ist dort ab Werk an.
> - Release-Builds haben weder die Zeile noch die Funktion.
> - Abweichend vom Entwurf in T3/T4 (nach den Bildern mit dem echten Kern):
>   - Figuren empfangen gar keinen Schatten, statt 30 cm Toleranz. Das beseitigt die Streifen ganz.
>   - Ein Schatten reicht höchstens 4 m hinter die Figur (sonst fiel er durch Wände).
>   - Die Maske hat ein Texel je GameCube-Pixel, mit 8 Abfragen.
>   - Die Marke setzt der Kern genau hinter Ordnungstabelle 14, vor die Effekte.
>   - Der Fortsetzen-Bildschirm ist über `STA_DIEDEMO` ausgenommen.
> - GLES und Vulkan zeichnen die Schatten mit denselben Shadern. Am PC sind die Bilder beider Renderer gleich, auch
>   mit MSAA.
> - Stand, Tests und Kosten: `port/docs/whole-program-status.md`, Abschnitt „Branch `realtime-shadows`“.
> - Bilder: `E:\Development\Android\builds\realtime-shadows\debug-build\`.
>
> **Nachtrag 16.11.2026: auch Gegenstände, drei Stufen (weiter nur in Debug-Builds).** Wünsche des Projektinhabers:
> „Fässer und Windräder sollen aber auch Schatten haben.“ und „Prüfe generell alles Bewegliche, ob Schatten möglich
> sind: Flaggen wie in Map 1-2 usw. Es sollte nicht inkonsistent wirken. Von daher wäre ein Schalter für
> Aus > Figuren > Figuren und Objekte evtl. sinnvoll.“
>
> - Die Zeile heißt jetzt „Echtzeitschatten · Aus > Figuren > Figuren und Objekte“, ab Werk „Figuren und Objekte“,
>   und steht unter Anzeige → Effekte statt unter Leistung (Wunsch des Inhabers).
> - Der erste Gerätetest zeigte keine Wirkung: Die App schnitt das Schatten-Bit ab, bevor es den Kern erreichte
>   (Stufe 3b in `port/docs/whole-program-status.md`; behoben, der Kern meldet jetzt, was ankommt).
> - Bei „Figuren und Objekte“ wirft alles Bewegliche Schatten, und zwar immer ganze Arten: Türen, Kisten, Fässer,
>   Regale, Hebel, Gittertore, fallende Bäume und rollende Felsen, die drehenden und schwingenden Teile der Räume
>   (Windradflügel, Räder, Lampen), was ein Raum-Skript oder eine Szene in den Raum stellt (das Auto in r100), Stoff
>   (Fahnen, Banner) und die Maschinen (Leitern, Glocken, Gondel, Lore …). Waffen und Schilde in der Hand gehören schon
>   bei „Figuren“ dazu.
> - Nie: die feste Kulisse (Häuser, Zäune, Mauern, Seile, Brücken; ihre Schatten sind in die Raumgrafik gemalt),
>   Fenster, Fackeln, Gegenstände zum Aufheben, Haare, Laub, Effekte, und was das Spiel schon selbst beschattet.
> - **Zäune:** Sie sind feste Kulisse. Technisch ginge es, aber dann gehörte die ganze Kulisse dazu (sonst wirft der
>   Zaun und das Haus daneben nicht), und die gemalten Schatten würden doppelt dunkel. Deshalb draußen.
> - Gegenstände bekommen den Schatten der Figuren, aber nicht ihren eigenen (keine Streifen). Jeder Schatten reicht
>   höchstens 4 m hinter seinen Werfer, auch bei Gegenständen (sonst dunkelten hoch hängende Stoffbahnen ganze Böden).
> - Tabelle aller Arten, Kosten je Stufe und was geprüft ist: `port/docs/whole-program-status.md`, Abschnitt „Branch
>   `realtime-shadows`“, Stufe 4. Bilder: `E:\Development\Android\builds\realtime-shadows\objects\`.
>
> **Nachtrag 16.11.2026, Stufe 5: Die Schatten folgen dem Licht des Spiels (weiter nur in Debug-Builds).** Wunsch des
> Inhabers: nicht mehr eine feste Richtung und Stärke, sondern Fackeln, Lampen, Kerzen, Himmel und Blitze; im Dunkeln
> nur dezent; Kern- und Halbschatten bei mehreren Lichtern.
>
> - Der Kern nimmt die Lichter, die Leon in diesem Bild wirklich beleuchten (die Liste des Spiels je Modell), und
>   rechnet sie aus wie das Spiel. Die drei hellsten, die in der Welt sitzen, werfen Schatten, jeder mit seinem Anteil
>   am ganzen Licht bei Leon. Im Dunkeln wird alles schwächer. Lichter, die sich mit der Kamera drehen, werfen nur
>   beim Blitz.
> - Das Fußschatten-Licht der Entwickler bestimmt die Richtung nicht mehr. Die runden Flecken bleiben aus, auch wo kein
>   Licht wirft.
> - Der Renderer zeichnet bis zu drei Lichter; wo zwei verdeckt sind, ist es dunkler (Kernschatten).
> - Stand, Lichter je Raum, Kosten und Prüfungen: `port/docs/whole-program-status.md`, Stufe 5. Bilder:
>   `E:\Development\Android\builds\realtime-shadows\stage5\`.
>
> **Nachtrag 17.11.2026, Stufe 6: Jede Figur bekommt ihren Schatten von dort, wo sie steht.** Fehler für 1.0, vom
> Inhaber nach dem Gerätetest gemeldet: Ging Leon, wanderten die Schatten des Händlers oder Ashleys mit.
>
> - Ursache: Stufe 5 rechnete für eine Lampe eine einzige Richtung aus (von der Lampe zu Leon) und eine Stärke an
>   Leons Platz und nahm beides für alle Figuren.
> - Jetzt zählen die Lichter aller Figuren im Bild. Eine Lampe oder Fackel bekommt eine perspektivische Karte von ihrem
>   Ort aus (die „Lampen-Schatten“ aus T9, früher als geplant); ihr Kegel schaut dorthin, wo die Kamera im Licht der
>   Lampe etwas sieht. Ein Himmelslicht bekommt eine Karte auf festem Raster. Die Stärke rechnet die Maske an jeder
>   Stelle aus dem Licht dort aus. Die Wahl der Lichter bleibt ruhig, solange sich kein Licht deutlich ändert.
> - Folge: Steht eine Figur still, bleibt ihr Schatten Pixel für Pixel gleich, während eine andere geht (Test in
>   `gx_gl_check`).
> - Stand und Prüfungen: `port/docs/whole-program-status.md`, Stufe 6. Bilder:
>   `E:\Development\Android\builds\realtime-shadows\stage6\`.
>
> **Nachtrag 18.11.2026, Stufe 7: nicht mehr zu dunkel, kein wildes Flackern.** Gerätetest des Inhabers mit Dumps aus
> der Burg:
>
> - Zu dunkel: In den Burgsälen ist eine Lampe das ganze Licht einer Figur (kein Umgebungslicht). Jeder Schatten nahm
>   deshalb so viel weg, wie es geht, und zwei Lichter einer Lampe gaben zwei Schatten übereinander. Jetzt höchstens
>   60 % je Licht und 70 % zusammen; Lampen, die weniger als 1 m auseinander hängen, werfen als eine.
> - Flackern: Die Lampenkarte schaute dorthin, wo die Kamera hinsieht, drehte sich mit ihr und schnitt Figuren ab. Jetzt
>   liegt sie um die Figuren im Lichtkreis der Lampe und bleibt, solange die Kamera steht.
> - Riesige Schatten einer Lampe direkt neben einer Figur (sie vergrößerten jedes Atmen) werden blass: eine echte Lampe
>   ist kein Punkt.
> - Stand und Prüfungen: `port/docs/whole-program-status.md`, Stufe 7. Bilder:
>   `E:\Development\Android\builds\realtime-shadows\stage7\`.
>
> **Nachtrag 18.11.2026, Stufe 8: kein Schatten von unten.** Gerätetest des Inhabers in r10e: Leons Schatten lag
> umgedreht an Wänden und neben der Leiter. Ursache: parallele Fülllichter des Spiels, die von unten scheinen. Ein
> paralleles Licht wirft jetzt nur, wenn es von oben kommt (mindestens etwa 14° über dem Horizont); betroffen waren
> neun Räume. Stand und Prüfungen: `port/docs/whole-program-status.md`, Stufe 8. Bilder:
> `E:\Development\Android\builds\realtime-shadows\stage8\`.

---

## Teil 1: Für den Projektinhaber (kurz und ohne Fachsprache)

### Die Antwort in fünf Sätzen

1. **Echte Schatten für Leon, Ashley, Luis, Gegner und bewegliche Gegenstände sind machbar.** Ein Versuch am PC zeigt
   sie auf Wänden, Toren, Tischen und auf dem Boden (Bilder 01 bis 06).
2. **Schatten von Gebäuden** malt das Spiel schon heute fest in die Räume (die Raumgrafik ist „vorbeleuchtet“). Neu wäre
   nur, dass eine Figur dunkler wird, wenn sie in den Schatten eines Hauses tritt. Das ist ein zweiter, teurerer
   Schritt und gehört nicht in 1.1.
3. **Raytracing** (Lichtstrahlen-Berechnung) können nur sehr neue Spitzenhandys, ungefähr jedes fünfte. Der Retroid
   kann es nicht. Es würde kaum besser aussehen als Schritt 1, kostet aber das Doppelte an Arbeit. **Empfehlung: nicht
   für 1.1.**
4. **Empfehlung für 1.1:** eine Einstellung „Echtzeit-Schatten“, ab Werk **aus**. Sie erscheint nur auf Geräten, die
   stark genug sind. Sie ersetzt die runden Fußschatten des Spiels durch echte Schatten. Die Richtung des Lichts nimmt
   sie aus den Lichtdaten, die die Entwickler für jeden Raum angelegt haben.
5. **Aufwand:** etwa 8 bis 10 Sitzungen (rund zwei Wochen), mit Vulkan, Menü, Freigabe nach Gerät und Prüfung aller
   Räume.

### Was das Spiel heute schon macht

- **Fußschatten:** Unter jeder Figur liegt ein weicher, runder dunkler Fleck. Eigentlich sind es mehrere kleine Flecken
  an den Füßen und am Körper, verschoben in die Lichtrichtung des Raums. So sieht das Spiel fast immer aus.
- **Richtige Schattenbilder** zeichnet das Spiel fast nur in Zwischensequenzen, in 8 Räumen (Messung vom 08.11.2026).
  Dafür zeichnet es die Figur aus Sicht einer Lampe in ein kleines Bild und legt es auf den Boden.
- **Die Räume** sind vorbeleuchtet: Licht und Schatten von Häusern, Bäumen und Mauern sind fest in die Farben
  eingerechnet. Die Figuren werden zusätzlich von bis zu 8 Lichtern beleuchtet. Davon hängen viele **an der Kamera**,
  wie die Lampen bei einem Fotoshooting. So sehen die Figuren immer gut aus, egal wohin die Kamera schaut.

Das letzte ist eine wichtige Erkenntnis: Diese Kamera-Lampen darf man **nicht** für Schatten nehmen. Sonst würde der
Schatten mitwandern, wenn man die Kamera dreht. Im Dorfplatz-Kampf (r101) steht das stärkste Licht auf den Figuren in
jedem Bild an derselben Stelle neben der Kamera.

**Die gute Nachricht:** In 108 von 127 Räumen haben die Entwickler ein eigenes **„Fußschatten-Licht“** angelegt. Das ist
eine feste Lichtrichtung pro Raum: 41 Räume senkrecht von oben, die anderen schräg, bis zu 61 Grad. Genau daraus
berechnet das Spiel heute seine runden Flecken. Ein neues Schattensystem nimmt dieselbe Richtung. Dann fällt der
Schatten dorthin, wo die Entwickler ihn haben wollten.

### Die drei Wege

| | A: Die Schatten des Spiels verbessern | B: Echte Schatten für Figuren (Schattenkarte) | C: Raytracing |
|---|---|---|---|
| Was man sieht | Etwas weichere Flecken. Die Schattenbilder in Zwischensequenzen sind schon heute schärfer, weil sie mit der Bildauflösung der App wachsen. | Der echte Umriss von Leon und den Gegnern auf Boden, Wänden, Toren und Kisten. Er bewegt sich mit jeder Bewegung mit. | Wie B, dazu ganz genaue Schatten von Lampen und dunklere Ecken („Umgebungsverdeckung“). |
| Geräte | alle | starke Geräte (Retroid ja, Mali-G52/G57 nein) | nur neue Spitzenhandys: Snapdragon 8 Gen 2 und neuer, 8s Gen 3, Dimensity 9200 und neuer, Exynos 2200 und neuer. **Nicht der Retroid.** |
| Kosten pro Bild | fast nichts | geschätzt 1 bis 2 ms auf dem Retroid bei 3× (nicht auf dem Gerät gemessen) | mehrere ms, auch auf Spitzenhandys |
| Aufwand | 1 bis 2 Sitzungen | 8 bis 10 Sitzungen | 12 bis 20 Sitzungen, dazu zuerst B |
| Risiko | klein | mittel (Streifen auf Figuren, Sonderfälle wie Menüs, beides im Versuch gefunden und lösbar) | hoch (neue Treiber-Funktionen, wenige Testgeräte, der Retroid kann es nicht prüfen) |

**Empfehlung:** Weg **B** für 1.1. Weg A lohnt sich allein nicht. Weg C erst später und nur, wenn B gut ankommt. C
würde dann B auf Spitzenhandys verfeinern und nicht ersetzen.

### Welche Geräte die Einstellung sehen

Deine Bedingung war: nur auf leistungsfähigen und passenden Geräten. Vorschlag:

- Die App kennt schon heute den Grafikchip und das Ergebnis des Benchmarks. Die Einstellung erscheint nur, wenn **alles**
  zutrifft:
  - starker Grafikchip (die bekannte Liste: Adreno 640 und neuer, Mali-G6xx/G7xx, Immortalis, Xclipse),
  - der Benchmark hat mindestens 3× bei 30 Bildern oder 2× bei 60 Bildern geschafft,
  - der Prozessor ist nicht am Limit,
  - mindestens 4 GB Arbeitsspeicher.
- **Schwache Geräte sehen die Zeile gar nicht** (sie wird nicht nur grau, sondern ausgeblendet).
- Bricht beim ersten Einschalten etwas (ein Grafiktreiber kann die nötige Funktion nicht), schaltet die App die Schatten
  still ab. So macht es heute auch der Bloom.

### Wo im Menü

App-Menü → Leistung, neben Bloom: **„Real-time shadows · Off / Characters“**. Später kann „Characters + objects“ dazu
kommen. Die Texte stehen auf Englisch, das Menü im Spiel folgt der Handy-Sprache (11 Sprachen). Ab Werk **aus**.
Zusammenspiel mit anderen Einstellungen:

- **Potato „Schatten & Licht“ aus** → die Echtzeit-Schatten sind ebenfalls aus, die Zeile ist grau.
- **Dynamische Auflösung** misst die Grafikzeit schon heute. Mit Schatten pendelt sie sich etwas tiefer ein, je nach
  Gerät um eine halbe Stufe.
- **60 FPS:** Die eingeschobenen Zwischenbilder brauchen den Schatten auch. Die Kosten fallen dann für jedes gezeigte
  Bild an. Auf dem Retroid ist bei 60 FPS kaum Reserve übrig.
- **Bloom:** Die Schatten kommen vor dem Bloom ins Bild. Beides geht zusammen.
- **PSone-Modus:** Schatten aus.
- **In Zwischensequenzen** bleiben die Schatten des Spiels, die neuen sind dort aus. Sonst gäbe es doppelte Schatten.
- **Leistungsprofil „Hoch“** könnte die Schatten nur dort selbst einschalten, wo Reserve über der Bildschirmauflösung
  bleibt. Bloom wird schon heute so geregelt.

### Was bis 1.1 fertig sein kann

„Echtzeit-Schatten · Figuren“ mit OpenGL ES und Vulkan, auf allen Räumen beider Disc-Arten am PC geprüft und auf dem
Retroid getestet. Die runden Fußschatten werden ersetzt, wenn die Einstellung an ist. Nicht in 1.1: Gebäude, die
Figuren beschatten. Lampen und Fackeln, die eigene Schatten werfen. Raytracing.

### Die Bilder

Ordner `E:\Development\Android\builds\realtime-shadows\`. Links steht immer das Spiel wie heute, rechts der Versuch.
Manche Bilder haben ein drittes Feld mit der Schattenmaske (dunkel = Schatten). Die Bilder sind PC-Bilder mit der
Debug-Disc, daher stehen oben Debug-Zahlen.

| Bild | Was es zeigt |
|---|---|
| `01-r10e-door.png` | Stollen: Leons Schatten fällt auf das Eisentor. |
| `02-r207-wall.png` | Schloss: Leons Schatten an der Wand. |
| `03-r11b-table.png` | Hafen-Lager: Leons Schatten auf dem Tisch. |
| `04-r101-villager.png` | Dorfplatz: Die Dorfbewohnerin wirft einen echten Schatten statt des runden Flecks. |
| `05-r101-ganados.png` | Dorfplatz im Kampf: Schatten an den Füßen der Ganados (mit Maske). |
| `06-r403-ganado.png` | Dorf r403: Ganado und Leon, Lichtrichtung aus der Lichttabelle des Raums (mit Maske). |
| `07-problem-self-shadow-r30f.png` | **Problem 1:** Streifen auf Figuren, wenn sie sich selbst beschatten. Lösbar, siehe Anhang. |
| `08-problem-continue-screen.png` | **Problem 2:** Ein Schatten liegt über dem Fortsetzen-Bildschirm. Menüs und Sonderbilder müssen ausgenommen werden. |
| `09-r100-forest.png` | Wald: Das Licht kommt fast von oben, der Schatten liegt unter Leon. Man sieht wenig, so ist es richtig. |

Ehrlich gesagt: In vielen Räumen fällt das Licht steil von oben. Der neue Schatten liegt dann vor allem **unter** den
Figuren, wie heute der Fleck, nur mit echtem Umriss. Groß und deutlich wird er dort, wo das Licht schräg einfällt oder
eine Wand nahe ist.

---

## Teil 2: Technischer Anhang

### T1 Die Schatten des Spiels im Code

- **Fußschatten (Blob)**, `src/game/foot_shadow.cpp` `DrawFootShadow`: Am Ende jedes Modell-Zeichnens (`trans.cpp`)
  zeichnet das Spiel für jedes Licht mit `Id == 4`, `LIT04_MOVE_FREE::Kind == 5` in Reichweite eine Reihe von
  Boden-Quads (Esp-Textur 0x13). Sie liegen an den Gelenken aus `FootShadowTbl`, entlang der Lichtrichtung auf den
  Boden projiziert. Die Richtung kommt bei Typ 1/2 aus `Ang_x/Ang_y`, bei Typ 0 aus der Lichtposition. Die Stärke ist
  `Col.r × (255 − Shd_color)`. Blending `GX_BL_SRCALPHA/INVSRCALPHA`, Tiefe getestet ohne Schreiben, mit
  `GXLoadPosMtxImm(pG->Camera.v_mat)`. Das ist der Schatten des normalen Spiels.
- **Projizierte Schattenbilder**, `src/game/shadow.cpp`: `ShadowTrans` holt pro Bild bis zu 24 `ShadowMng`.
  - Lichter mit `Id == 4`, `Kind == 0`: Typ 0 „fit“ (perspektivisch vom Licht zur Figur), Typ 1 „parallel“ (von oben,
    gekippt um `shadow_add_dir_x` und `Ang_x/Ang_y`), Typ 2 „fix“ (feste Lampe, sammelt bis zu 8 Figuren).
  - `make_shadow_texture` zeichnet die Werfer mit konstantem Ziel-Alpha (`GXSetDstAlpha`) in einen 256×256-Bereich
    des EFB. `MakeSoftShadow` macht optional weiche Kopien. Danach eine EFB-Kopie als I8, 128×128.
  - `shadowScrModelRender` (OT 0xE) zeichnet die **Empfänger** ein zweites Mal mit der projizierten Textur: Objekte und
    Gegner mit `be_flag 0x80`, dazu eigene Empfänger-Modelle aus der SHD-Datei des Raums (`ShdInit`, `kindid 3`).
    Raum-Modelle sind keine Empfänger.
  - Laut Messung vom 08.11.2026 nur in Ereignissen von r11b, r206, r215, r227, r228, r22b, r300 und r316. Kosten dort
    12 bis 25 % der Grafikzeit (Potato-Messung, PC).
- **Selbstschatten**, `SelfShadowLevel`, `trans.cpp` `SelfShadowSetup`: Eine TEV-Stufe mit indirekter Rampe (`IndTex`)
  und der eigenen Schattentextur.
- **Lichtmuster** (`Kind 1..4`, Typ 2, `GetCastShadowMngPtr`, Modelle mit `be_flag 0x02000000`): z. B. das Muster
  eines Fensters auf Figuren. In 11 Räumen (T2).
- **Potato „Schatten & Licht“ aus** (Patch 0219) schaltet `ShadowTrans` und `DrawFootShadow` ab.
- **Gebäude:** Die Raum-Modelle haben Eckfarben (vorbeleuchtet, `data->flags & 0x40000000`: Umgebungsfarbe aus den
  Ecken) und dazu bis zu 8 GX-Lichter (`trans_lit.cpp` `LightSetModel`). Die Figuren haben bis zu 8 GX-Lichter aus dem
  Licht-Schnitt des Raums.
- **EFB-Kopien der Schattenbilder** haben in der App schon heute die Auflösung des EFB (`GlRenderer::CopyTarget`:
  `Hi(w)` × `Hi(h)`). Bei 3× wird aus dem 128×128-Schatten eine 384×384-Textur. „Mehr Auflösung“ für Weg A gibt es
  also schon.

**Was ein neues System doppeln oder stören würde:** die Blob-Schatten (immer), die Schattenbilder in den 8
Ereignis-Räumen, die vorbeleuchteten Schatten der Welt (Welt darf nicht auf Welt werfen) und die Kamera-Lichter (T2).

### T2 Messung: die Lichter aller Räume (LIT, Debug-Disc, 127 Räume)

Werkzeug `port/docs/echtzeit-schatten/lit_survey.cpp`. Es liest die Raumarchive mit dem Yz2-Entpacker und dem
LIT-Leser der App (`room_lighting.cpp`), alle Licht-Schnitte. Ergebnis: `lit_survey_debug_disc.txt`.

- **Fußschatten-Lichter** (`Id 4`, `Kind 5`) in **108 von 127** Räumen, fast alle Typ 1 (Richtung).
  - Neigung gegen die Senkrechte (erstes Licht des Raums): 0° in 41 Räumen, 1–20° in 30, 21–40° in 30, 41–61° in 7.
  - Stärke `Col.r`: 83 bis 255, Median 149.
  - Ohne Fußschatten-Licht: r106, r109, r10b, r10e, r120, r215, r224, r229, r22b, r306, r313, r314, r322, r323, r324,
    r328, r329, r40d, r411. 10 davon haben Welt-Parallellichter.
- **Kamera-Parallellichter** (Typ 5, `Flag` Bit 0, `trans_lit.cpp lightSetParallel`: Richtung in Kamera-Koordinaten)
  in **103** Räumen. Welt-Parallellichter in 61. Punkt- und Spotlichter in 125.
- **Lichtmuster-Lichter** (`Kind 1..4`): r216, r21d, r221, r300, r301, r315, r316, r317, r31d, r405, r406.
- **Schattenbild-Lichter** (`Kind 0`): r100, r120, r300, r30b, r405.
- **Gegenprobe im GX-Strom:** Im Versuch war das stärkste GX-Licht auf den Figuren in r101 (Dorfplatz, 10 Bilder mit
  verschiedenen Kamerarichtungen) immer bei etwa (99 600, 3 200, 1 200) in Kamera-Koordinaten, also an der Kamera.
  In r100 wanderte es mit der Kamera, war also ein Weltlicht. Das stärkste GX-Licht taugt deshalb nicht als
  Schattenlicht.

### T3 Der Versuch (Prototyp)

Code: `port/docs/echtzeit-schatten/prototyp/`. `apply_proto.py` setzt drei Schnipsel in eine Kopie von `gx_core.h`,
`gx_core.cpp` und `gx_gl.cpp` (nur GLES, nur am PC, Schalter `RE4_SHD_PROTO=1`).

1. **Werfer sammeln** (`GxCore::ShdCollect`, in `Draw`): Ein Zeichenbefehl zählt als Werfer, wenn er alle diese
   Bedingungen erfüllt:
   - Owner-Tag mit `RE4_GX_OWNER_FIGURE`,
   - Dreiecke, perspektivisch, volle Bildbreite,
   - Tiefe geschrieben,
   - Mitte höchstens 15 m von der Kamera.

   Seine Ecken werden auf der CPU mit den Positionsmatrizen aus dem XF-Speicher in Kamera-Koordinaten gerechnet, wie
   bei `FlareNote`. **Tabelle und Draw-Funktion reichen zur Unterscheidung nicht:** Figuren und Raum liegen beide in
   Ordnungstabelle 0x0B, mit derselben Funktion. Ältere Aufnahmen vor 0.99.6 haben das FIGURE-Bit nicht.
2. **Blob-Schatten weglassen** (`ShdBlob`): Erkannt an der Signatur von `drawShadowParts`: nur Position und TEX0, eine
   TEV-Stufe mit Farbe 0, SRCALPHA/INVSRCALPHA, Tiefe ohne Schreiben. Dabei wird auch die **Kamera-Matrix** abgelesen
   (der Blob wird mit `Camera.v_mat` gezeichnet).
3. **Licht:** Die Fußschatten-Richtung des Raums (`RE4_SHD_FOOT=ax,ay`, aus T2), in Kamera-Koordinaten gedreht:
   `RotY(ay)·RotX(ax)·(0,−1,0)`, wie `foot_shadow.cpp`. Ersatz, wenn das Bild keinen Blob hat: das stärkste GX-Licht
   auf den Figuren.
4. **Schattenkarte:** orthografisch entlang der Lichtrichtung, eng um die Werfer gelegt, 2048², `DEPTH_COMPONENT24`
   mit Vergleichs-Sampler. Polygon-Offset 1,5/2. Tiefe bis 15 m hinter die Werfer.
5. **Maske** (nach dem letzten 3D-Zeichenbefehl, an der Stelle von Bloom und Linsenreflexen, `FlareScan`):
   - Aus der EFB-Tiefe wird die Position in Kamera-Koordinaten zurückgerechnet, mit derselben Formel wie im
     Vertex-Shader (`vp`, `proj`).
   - Die Normale kommt aus den Nachbarn: die kleinere Tiefen-Differenz je Achse.
   - Normal-Offset 1,5 Texel, Tiefen-Bias, 12 PCF-Abfragen (Poisson, Radius 2,5 Texel).
   - Nur Flächen, die zum Licht zeigen. Ausblenden zwischen 12 und 20 m.
   - `RE4_SHD_SELF`: Toleranz gegen Selbstschatten.
6. **Auftragen:** Die Maske wird mit der EFB-Farbe multipliziert (`GL_DST_COLOR, GL_ZERO`), Stärke 0,5 bis 0,55.

**Messungen am PC** (llvmpipe, Maßstab 2, Debug-Disc):

- Werfer: 43 bis 70 Zeichenbefehle mit 9 000 bis 21 000 Dreiecken. Das ganze Bild hat 80 000 bis 130 000 Dreiecke.
- Dorfkampf r101, 10 Bilder: Der Schatten-Durchgang braucht **23 bis 33 ms von 137 bis 170 ms pro Bild (etwa 15
  bis 20 %)**. Die Maske läuft dabei in voller Auflösung mit 17 Abrufen pro Pixel.
- llvmpipe rechnet auf dem Prozessor. Das gibt nur die Größenordnung an, ähnlich den 12 bis 25 % der Spiel-Schatten
  in Zwischensequenzen.
- **Schätzung für den Retroid** (Adreno 650, 3×, nicht gemessen): 1 bis 2 ms pro Bild mit diesen Sparmaßnahmen:
  - Maske in halber Auflösung, mit tiefenbewusstem Hochskalieren,
  - Scissor auf den Bereich der Schatten,
  - 4 bis 8 Abfragen mit Hardware-2×2-PCF,
  - Karte 1024² bis 2048².

**Was der Versuch gezeigt hat:**

- Das Verfahren funktioniert mit dem GX-Strom, wie er ist. Kein Eingriff in TEV-Shader, Shader-Schlüssel oder
  Programm-Cache.
- Eine Karte über alle Figuren des Raums ist zu grob: 46 m bei 22 Einheiten pro Texel, unscharf, mit Fehlern auf
  Leon. Die Werfer müssen nahe an der Kamera liegen (15 m), sonst braucht es Kaskaden.
- **Selbstschatten der Figuren** gibt Streifen (Bild 07): Die Normalen aus der Tiefe sind kantig, und die
  GX-Beleuchtung der Figur passt nicht dazu. Eine Toleranz von 40 cm nimmt die Streifen weg, aber auch einen Teil des
  Kontaktschattens. Bessere Lösung: Figuren-Pixel markieren (Stencil-Bit beim Zeichnen der Figuren-Draws) und dort
  nur Schatten **anderer** Werfer zulassen.
- **Sonderbilder:** Der Fortsetzen-Bildschirm (Bild 08) hat noch Figuren-Draws vor seinem 2D-Bild. Ohne Ausnahme
  liegt ein Schatten darüber. Der Kern muss melden, ob gerade normales Spiel läuft (kein Ereignis, kein Menü, kein
  Fortsetzen-Bildschirm, kein Unterbildschirm).
- In Räumen mit steilem Fußschatten-Licht liegt der Schatten unter den Figuren. Die Kamera hinter Leon sieht dann
  wenig. Das ist richtig so.

### T4 Weg B im Einzelnen (Entwurf für 1.1)

- **Welt und Figuren unterscheiden:** Der Owner-Tag (`RE4_GX_OP_OWNER`, Patch 0137, `runtime/gx_owner.cpp`) hat für
  Figuren das Bit `RE4_GX_OWNER_FIGURE`: Arbeiten des `EmMgr` und daran hängende Partikel. Für bewegliche Objekte
  (`ObjMgr`: Fässer, Kisten, Türen) braucht es ein zweites Bit, im selben Kern-Code. Die Tabellennummer reicht nicht
  (T3). Werfer sind nur Draws mit Tiefe-Schreiben (keine Partikel, kein Glas).
- **Lichtrichtung pro Raum, draußen wie drinnen:**
  - Die Fußschatten-Lichter des aktiven Licht-Schnitts (`LightMgr`, `Id 4`, `Kind 5`). Bei Typ 1/2 die Richtung aus
    `Ang_x/Ang_y`, bei Typ 0 die Richtung von der Lichtposition zur Figur. Die Stärke aus `Col.r`. Es gibt sie in 108
    von 127 Räumen, drinnen genauso wie draußen (z. B. r201 25°/52°, r10a 6°/27°).
  - Ersatz: ein Welt-Parallellicht (Typ 5 ohne Flag Bit 0). Sonst senkrecht und schwach, oder aus. Das betrifft 19
    Räume.
  - Nie die Kamera-Lichter.
  - Der Kern kennt das alles direkt. Er schickt es zusammen mit der Kamera-Matrix und einem Zustandsbit für „normales
    Spiel“ als neue Op im GX-Strom, wie `RE4_GX_OP_OWNER`. Ein Renderer, der die Op nicht kennt, überspringt sie.
- **Geometrie für den Tiefen-Durchgang:**
  - Variante 1 (wie im Versuch): Die Figuren-Ecken aus dem Chunk auf der CPU in Kamera-Koordinaten rechnen, ein
    Puffer, ein Draw. Unabhängig von der API. 10 000 bis 21 000 Dreiecke. Auf einem Handy-Kern geschätzt 0,2 bis
    0,5 ms im Render-Thread, nicht gemessen.
  - Variante 2: Die aufgezeichneten Figuren-Draws noch einmal mit einem Nur-Tiefe-Programm zeichnen. Die Ecken liegen
    schon im VBO (`UploadChunk`). Das kostet etwa 50 bis 70 Draw-Calls mehr und braucht die XFM-Matrizen pro Draw.
  - Erst Variante 1 bauen und messen.
- **Shader-Schlüssel und TEV:**
  - Mit der Maske (Empfehlung) bleiben `ShaderKey`, Programm-Cache, Vorwärm-Listen und Vulkan-Pipelines unverändert.
    Der neue Durchgang ist ein fester Shader wie Bloom (`gx_fixed_shaders.h`).
  - Später der in TEV eingebaute Weg: ein Schlüssel-Bit „empfängt Schatten“, eine Lichtraum-Position als Varying und
    die Multiplikation vor dem Nebel. Er verdoppelt die Programme der Empfänger und betrifft
    `CanonicalShaderKey`/`gx_vkpipe_list.py`. Er ist nur für „Gebäude beschatten Figuren“ nötig.
- **GLES 3 und Vulkan:**
  - GLES 3.0 hat alles im Kern: Tiefentexturen, `sampler2DShadow`.
  - Vulkan: `VkSampler` mit `compareEnable`, Format D16 oder D32 (D16 als Sampled Image ist Pflicht), Shader über das
    vorhandene glslang.
  - Der Ablauf entspricht `BloomPass` in `gx_gl.cpp` und `gx_vk.cpp`.
  - Auf Kachel-GPUs beendet der Durchgang den EFB-Renderpass wie Bloom. Mit „Bandbreite sparen“ (Vulkan) zusammen
    prüfen.
- **Doppelte Schatten vermeiden:**
  - Blob-Schatten im Kern abschalten, solange die Einstellung an ist. Das geht wie Patch 0219, nur für
    `DrawFootShadow`, und nicht über eine Erkennung im Renderer.
  - In Ereignissen den neuen Schatten aus und die Schattenbilder des Spiels an lassen.
  - Lichtmuster (`Kind 1..4`) lassen.
  - Die Welt wirft nicht auf die Welt (vorbeleuchtet).
  - Nur Flächen, die zum Licht zeigen.
  - Stärke 0,4 bis 0,6 × `Col.r/255`, im Nebel ausblenden: den Nebelanteil aus den PX-Nebelwerten in der Maske
    rechnen, statt nach festem Abstand.
- **Weitere Punkte:**
  - 60 FPS: Der Durchgang läuft auch im Zwischenbild-Pass mit den gemischten Ecken.
  - MSAA: Die Tiefe vor der Maske auflösen (`Resolve(..., true)`).
  - Die Figuren-Pixel für die Selbstschatten-Regel markieren (T3).
  - Am Ende eine Raumtour mit Prüfbildern über alle Räume beider Disc-Arten und eine Prüfung in `gx_gl_check`.

### T5 Weg A im Einzelnen

- Die Schattenbilder sind durch die EFB-Skalierung schon schärfer (T1). Möglich wäre eine weichere Abfrage beim
  Projizieren (PCF im Empfänger-Shader). Das braucht ein Schlüssel-Bit, und es gibt sie nur in Ereignissen.
- Blobs: mehr und weichere Flecken, aber kein Umriss.
- `SHADOW_NUM_MAX` hochsetzen bringt nur dort etwas, wo `Kind 0`-Lichter liegen (5 Räume).
- Das Schattenbild-System in normale Räume übertragen geht nicht ohne Empfänger-Geometrie: Die Raum-Modelle sind
  keine Empfänger, und sie ein zweites Mal zu zeichnen kostet 60 000 bis 130 000 Dreiecke.

Fazit: 1 bis 2 Sitzungen, wenig sichtbar.

### T6 Weg C: Raytracing

**Nötige Vulkan-Erweiterungen:** `VK_KHR_acceleration_structure`, `VK_KHR_ray_query`, `VK_KHR_deferred_host_operations`,
`VK_KHR_buffer_device_address`, `VK_EXT_descriptor_indexing`, `VK_KHR_spirv_1_4`, `VK_KHR_shader_float_controls`. Die
App startet Vulkan heute mit Instanz-API 1.1 (`vk_context.cpp`). Das Kompilieren mit `GL_EXT_ray_query` geht über das
eingebaute glslang.

**Beschleunigungsstrukturen:**

- Raum-Geometrie: Sie ist innerhalb eines Raums fest. Ein BLAS je Raum-Modell kann man über den Inhalts-Schlüssel der
  Ecken zwischenspeichern. Dafür braucht man aber Welt- statt Kamera-Koordinaten, also die Kamera-Matrix vom Kern.
- Figuren: Sie werden auf der CPU gehäutet und jedes Bild neu geschickt. Pro Bild ein BLAS-Neubau oder Refit für
  10 000 bis 40 000 Dreiecke (die Topologie bleibt gleich, Refit geht), dazu ein TLAS-Neubau mit einigen hundert
  Instanzen.
- Öffentliche Zahlen zu BLAS-Kosten auf Mobil-GPUs gibt es nicht (unbestätigt).
- Arm rät, dynamische Meshes klein zu halten, mit `FAST_BUILD` und Refit zu bauen, Ray Query im Fragment-Shader zu
  nutzen und `rayQueryProceed` nur einmal aufzurufen, nicht in Schleifen.

**Kosten:** Arm misst (Vulkanised 2024) auf Dimensity 9200/9300 mit 1,5 Mio. Dreiecken bei 1600×900 für Ray-Query-
Schatten 17,3 ms pro Bild in voller Auflösung und 10,1 ms in halber Auflösung. RE4 hat zehnmal weniger Dreiecke.
Dafür ist fast alles dynamisch.

**Geräte** (vulkan.gpuinfo.org, Herstellertreiber, 15.11.2026):

- **Ray Query ja:**
  - Adreno 740 (ab Treiber 512.676, Android 13), 750, 735 (8s Gen 3), 830, 840/850.
  - Immortalis-G715 (r38.1), G720 (r44.1), G925 (r49.1), Mali-G1-Ultra.
  - Mali-G720 MC7/MC8 (r49.1).
  - Mali-G615 (r44.1; Hardware oder Emulation unbestätigt).
  - Xclipse 920, 940, 950, 960.
- **Nein:**
  - Adreno 650/660/640 (Retroid), Adreno 730 (8 Gen 1), 710/720/725/732, 810.
  - Mali-G52/G57/G68/G78/G610/G710.
  - Mali-G715 MC7 (Tensor G3/G4, bis r56 nicht freigeschaltet).
  - PowerVR DXT (Tensor G5).
- Anteil Android in der Datenbank: Ray Query 26 % laut Erweiterungsliste, 17 % nach Berichten.
- Die Ray-Tracing-Pipeline (`VK_KHR_ray_tracing_pipeline`) ist auf Android selten: Adreno erst ab 830 mit neuem
  Treiber, Xclipse ja, Mali meist nein. Sie wird nicht gebraucht.
- Hardware-Strahleneinheit bei Adreno erst ab 740 (XDC 2024).

**Vorteil gegenüber B:** genaue Schatten von Punktlichtern (Lampen, Fackeln) ohne Bias-Fehler, und Umgebungsverdeckung.
Die eigentlichen Schwierigkeiten bleiben dieselben wie bei B: welches Licht, Figur oder Welt, vorbeleuchtete Welt,
Sonderbilder. **C setzt B voraus** und könnte später in derselben Maske die Kartenabfrage durch eine Ray Query
ersetzen.

**Quellen:**

- <https://vulkan.gpuinfo.org/listreports.php?extension=VK_KHR_ray_query&platform=android>
- <https://vulkan.gpuinfo.org/listextensions.php?platform=android>
- Arm Vulkanised 2024/2026:
  - <https://dev.vulkan.org/user/pages/09.events/vulkanised-2024/vulkanised-2024-Iago-calvo-lista-arm-2.pdf>
  - <https://vulkan.org/user/pages/09.events/vulkanised-2026/Mobile-Ray-Tracing-Demystified-Iago-CalvoLista-Arm.pptx.pdf>
- Arm Learning Path: <https://learn.arm.com/learning-paths/mobile-graphics-and-gaming/ray_tracing/rt04_acceleration_structure/>
- Adreno/Turnip XDC 2024: <https://indico.freedesktop.org/event/6/contributions/302/attachments/225/305/Ray%20Tracing%20for%20Adreno%20GPUs%20on%20Turnip%20%28XDC%202024%29.pdf>
- Diablo Immortal: <https://developer.android.com/stories/games/diablo-raytracing>

### T7 Freigabe nach Gerät (Gating) im Einzelnen

Aufbauend auf `PerfProfile.java`:

- `strongGpu(d) && !weakGpu(d)`.
- `Bench.measured` und `cap30 >= 6 || cap60 >= 4` (3× bei 30 oder 2× bei 60 Bildern).
- `!cpuWeak(d, b) && !cpuTight(d, b)` und `ramMb >= 3800`.
- Beim ersten Einschalten: Shader gebaut und Framebuffer vollständig, sonst `shadowFailed` (wie `bloomFailed`) und
  dauerhaft aus.
- **Für C zusätzlich:**
  - Vulkan-Renderer aktiv, die Erweiterungen aus T6 vorhanden,
  - Chip und Treiber auf einer Positivliste (Adreno 740/735/750/8xx ab 512.676; Immortalis/Mali-G720 ab r44;
    Xclipse 920+),
  - eigener Kurz-Benchmark mit Ray Query.
- Profil „Hoch“: nur bei `room - screen >= 1` und ohne knappen Prozessor einschalten, wie `bloomFor`. Sonst bleibt es
  die Wahl des Spielers.
- Benchmark: Die Stufen werden ohne Schatten gemessen. Mit Schatten senkt die dynamische Auflösung selbst nach (sie
  misst die Grafikzeit).

### T8 Risiken

- Selbstschatten-Streifen und Kontaktschatten (T3). Gelöst nur mit Figuren-Markierung, die Toleranz ist ein Notbehelf.
- Sonderbilder: Fortsetzen, Unterbildschirme, Zielfernrohr, Fernglas, Karte, Ereignisse. Lösung: das Zustandsbit vom
  Kern, nicht Erkennung im Renderer.
- Kachel-GPUs (Mali): Ein zusätzlicher Renderpass-Bruch kostet Bandbreite. Die Freigabe hält Mali-G52/G57 ohnehin
  heraus.
- 60 FPS auf dem Retroid: kaum Reserve (Benchmark 0.99.14: etwa 2,5× für 60). Erwartet ist eine halbe Stufe weniger
  Auflösung.
- Räume mit mehreren Licht-Schnitten: Die Richtung wechselt mit dem Schnitt. Der Wechsel sollte weich sein, über die
  `Hokan`-Frames wie beim Nebel.
- Die Prüfung ist nur am PC möglich (llvmpipe). Echte Kosten und Treiberfehler zeigt erst das Gerät.

### T9 Aufwand in Schritten (Weg B, Ziel 1.1)

| Schritt | Inhalt | Sitzungen |
|---|---|---|
| 1 | Kern: neue GX-Op (Kamera-Matrix, Fußschatten-Licht des Schnitts, Zustandsbit), Objekt-Bit im Owner-Tag, Blob-Schatten aus | 1 |
| 2 | GLES: Werfer sammeln, Karte, Maske halbe Auflösung, Auftragen, Ausnahmen, 60-FPS-Zwischenbild, MSAA | 2–3 |
| 3 | Vulkan: dasselbe | 1–2 |
| 4 | Qualität: Figuren-Markierung, Nebel-Ausblendung, Raumtour beider Disc-Arten, `gx_gl_check`-Tests | 2 |
| 5 | App: Menüzeile, Freigabe, Profil, 11 Sprachen, `check_translations.py` | 1 |
| 6 | Gerätetest Retroid, Feinabstimmung | 1 |
| | **Summe** | **8–10** |

Später, nach 1.1: Gebäude beschatten Figuren (TEV-Weg oder Figuren-Maske mit Welt-Werfern) und Lampen-Schatten
(perspektivische Karten), zusammen 5 bis 8 Sitzungen. Raytracing (C) 12 bis 20 Sitzungen.

### T10 Dateien dieser Studie

- `port/docs/echtzeit-schatten/prototyp/`: die Schnipsel des Versuchs und `apply_proto.py`. Setzt sie in einen
  WSL-Ordner (`python3 apply_proto.py TREE [--again]`), danach `ninja gx_chunk_render`.
  - Schalter: `RE4_SHD_PROTO`, `_LOG`, `_TIME`, `_SHOW`, `_FOOT=ax,ay`, `_SELF`, `_STRENGTH`, `_SIZE`, `_RANGE`,
    `_KEEP_BLOBS`, `_BIAS`, `_SLOPE`, `_NORMAL`, `_PCF`, `_FADE0/1`, `_DIR`, `_TABLES`, `_FUNCS`.
  - `batch.sh`, `dirs.sh`, `deliver.sh` zeichnen die Aufnahmen anderer Sitzungen (WSL `~/prc/shots`) und machen die
    Vergleichsbilder.
- `port/docs/echtzeit-schatten/lit_survey.cpp` und `lit_survey_debug_disc.txt`: die Lichtzählung aus T2. Nur Zahlen
  und Raumnamen, keine Spieldaten.
- Neue Dorfkampf-Aufnahme (WSL `~/pshd/runs/r101fight`, nicht im Repository): Kern aus `~/re4-rc`, Debug-Disc,
  `RE4_TEST_PLACE="1000:-3000,0,-3000,0"`, `RE4_TEST_AIM=2`,
  `RE4_PAD_ROOM_SCRIPT="B@1200-1520/20 R@1800-3400/1 A@1810-3400/12"`. Das B führt aus dem Dateimenü des Debug-Starts.
