# Vulkan zusätzlich zu GLES – Studie

> **Seit 07.11.2026 wird gebaut** (Auftrag des Projektinhabers, Branch `vulkan`): der Bauplan mit den Etappen
> E0–E9, dem Stand seit 0.96.4 und den offenen Fragen steht in **`vulkan-bauplan.md`**. Diese Studie bleibt die
> Grundlage (Teile T1–T15, Risiken); ihre Zahlen zu `gx_gl.cpp` sind vom 31.10.2026.

Wunsch des Projektinhabers (31.10.2026): prüfen, was CubeEvil4 für eine **zusätzliche** Vulkan-Ausgabe
bräuchte. Nur eine Bestandsaufnahme, kein Code geändert. Branch `vulkan` (von `master`, 0.96.4-beta).
Vorgeschichte: Am 29.10.2026 hatte der Projektinhaber Vulkan nach einer ersten groben Schätzung „bis auf
Weiteres ausgeschlossen“ (`whole-program-status.md`, Abschnitt „Leistung auf Handys mit zwei schnellen
Kernen“). Diese Studie macht die Schätzung genauer, Teil für Teil.

## Kurzfassung

- **Was Vulkan bringen würde:** weniger Prozessorzeit im Grafiktreiber je Zeichenbefehl. Genau das ist auf den
  Mali-Handys der Tester der größte Posten (Galaxy S10e: der Zeichen-Thread rechnet 19,8 ms von 21,1 ms je
  Bild, fast alles im Treiber; Redmi 15C ~35 µs je Zeichenbefehl gegen ~3 µs auf dem Retroid). Auf Geräten,
  deren **Grafikchip** zu langsam ist (Galaxy S7, kleine Mali-G71 MP2), oder deren **Spiel-Thread** zu
  langsam ist (Redmi 15C), bringt Vulkan wenig. Dem Retroid bringt es nichts.
- **Was es kostet:** eine zweite Grafikausgabe neben `gx_gl.cpp` (heute ~6 400 Zeilen mit Shader-Erzeuger),
  eine neue Android-Anbindung (eigene Zeichenfläche, Drehung, Pausieren), eine Shader-Übersetzung in der App,
  neue PC-Prüfwerkzeuge. Geschätzt **50–75 Arbeitstage**, dazu Gerätetests auf Mali bei Testern. GLES bleibt
  in jedem Fall (Android 6, Geräte mit schlechten Vulkan-Treibern, Rückfall bei Fehlern).
- **Größte Risiken:** Ruckler beim ersten Auftreten neuer Effekte (Vulkan baut „Pipelines“, noch mehr als die
  heutigen 9 174 Programme), die vielen EFB-Kopien mitten im Bild (Durchgänge, Synchronisation), Mali- und
  PowerVR-Treiberfehler, die nur auf Geräten der Tester auftreten.
- **Empfehlung:** erst zwei billige Schritte (V0: ANGLE-Versuch beim Mali-Tester ohne Code; V0b: Eckdaten über
  einen Dauer-Ring wie die Uniforms, hilft auch GLES), dann entscheiden. Wenn ja: zuerst die Aufteilung des
  Renderers (V1), dann Vulkan am PC (V2–V4), erst danach auf Android.

## Begriffe

- **GLES / OpenGL ES:** die Grafikschnittstelle, die CubeEvil4 heute nutzt. Der Treiber des Handys übernimmt viel
  Verwaltung selbst (prüfen, umsortieren, Speicher) – das kostet bei jedem Zeichenbefehl Prozessorzeit.
- **Vulkan:** die neuere Schnittstelle. Die App sagt alles selbst und vorab (welcher Shader mit welchem Mischen,
  wann ein Bild fertig sein muss). Weniger Treiberarbeit je Befehl, dafür viel mehr Code in der App.
- **Treiber:** die Software des Grafikchip-Herstellers (Qualcomm/Adreno, ARM/Mali, PowerVR) auf dem Handy.
- **Shader:** kleines Programm, das auf dem Grafikchip läuft (Ecken umrechnen, Farbe je Pixel). Der Renderer
  erzeugt sie aus dem TEV-Zustand des Spiels.
- **GLSL ES / SPIR-V:** GLES nimmt Shader als Text (GLSL) und übersetzt sie im Treiber. Vulkan nimmt nur
  **SPIR-V**, ein vorübersetztes Binärformat; den Text übersetzt die App selbst (Werkzeug **glslang**).
- **Pipeline:** in Vulkan ein fertig gebautes Paket aus Shadern **und** Zeichenzustand (Mischen, Tiefe, Seite,
  Punkte/Linien/Dreiecke, Kantenglättung). Was in GLES ein „Programm“ plus Schalter ist, sind in Vulkan oft
  mehrere Pipelines. Bauen kostet wie heute das Linken (auf dem Retroid ~16 ms).
- **Pipeline-Cache:** fertig gebaute Pipelines als Datei, wie heute der Programm-Cache.
- **Render-Pass / Durchgang:** ein Abschnitt, in dem in ein Ziel (EFB, Kopie, Bildschirm) gezeichnet wird.
  Vulkan verlangt, dass die App sagt, ob der alte Inhalt geladen und ob das Ergebnis gespeichert wird.
- **Deskriptor:** in Vulkan der „Zettel“, der einem Shader sagt, welche Texturen und Uniform-Blöcke er liest.
- **Uniforms:** Konstanten für die Shader (Matrizen, Licht, TEV-Farben). Heute ein großer Ring-Puffer
  („Uniform-Stream“), aus dem jeder Zeichenbefehl seinen Platz bekommt.
- **Synchronisation / Barriere / Fence:** in GLES automatisch, in Vulkan Sache der App: „erst lesen, wenn fertig
  gezeichnet“, „diesen Puffer erst wieder beschreiben, wenn der Grafikchip ihn gelesen hat“.
- **Swapchain:** die Bilder, die Vulkan dem Bildschirm abwechselnd übergibt (in GLES macht das `GLSurfaceView`).
- **Pre-Rotation:** Handy-Bildschirme sind meist hochkant gebaut; spielt man quer, dreht entweder Android jedes
  Bild noch einmal (Zusatzarbeit) oder die App zeichnet es gleich gedreht. Vulkan zeigt das offen an.
- **EFB / XFB:** der Bildspeicher des GameCube, in den das Spiel zeichnet (EFB), und das fertige Bild zum Anzeigen
  (XFB). **EFB-Kopie:** das Spiel kopiert einen Teil des EFB in eine Textur (Wasser, Hitze, Schatten, Spiegel).
- **MSAA:** Kantenglättung mit mehreren Proben je Pixel. **Kachel-GPU:** Mali und Adreno zeichnen in kleinen
  Kacheln im Chip und schreiben am Ende eines Durchgangs in den Speicher – jeder Durchgangswechsel kostet.
- **lavapipe / llvmpipe:** Mesa-Software für den PC, die Vulkan bzw. GLES ohne Grafikkarte nachrechnet (für
  Bildvergleiche in WSL und in der Cloud). **Validation Layer:** Prüfschicht von Khronos, die falsche
  Vulkan-Nutzung meldet (nur zum Testen).
- **ANGLE:** eine Bibliothek von Google, die GLES-Befehle auf Vulkan umsetzt. Auf vielen Handys ab Android 10
  in den Entwickleroptionen je App einschaltbar.

## Bestand heute

| Teil | Datei(en) | Zeilen | Vulkan-Bezug |
|---|---|---|---|
| GX-Renderer (Zustand, Zeichnen, Kopien, Autofokus, FXAA, 60 FPS, Bündeln, Cache, Messungen) | `port/android-decomp/app/src/main/cpp/gx_gl.cpp`, `gx_gl.h` | 5 293 + 336 | etwa die Hälfte ist GL-frei (Register, Schlüssel, Zwischenbild-Logik, Bündel-Planung) und könnte geteilt werden |
| Shader-Erzeuger (GLSL ES 3.00) | `gx_gl_shader.cpp`, `gx_gl_shader.h` | 631 + 110 | Text bleibt fast gleich, Kopfzeilen und Bindungen anders |
| Texturformate (CPU) | `gx_texture.cpp` | – | unverändert nutzbar (dekodiert nach RGBA8) |
| GPU-Selbstprüfung Filmfarben | `gx_selfcheck.cpp` | 262 | braucht eine Vulkan-Fassung (prüft den Treiber) |
| Kernmodus der App, EGL, Bildtakt, Menü-Optionen | `core_mode.cpp` | 2 591 | GL-Teil klein (Bildschirm löschen, Bildschirmfoto, zweiter GL-Kontext fürs Vorwärmen), aber die Bild-Schleife hängt an `GLSurfaceView` |
| Java-Seite | `CoreActivity.java` (`GLSurfaceView`, Choreographer, 60-Hz-Wunsch), `FramePacing.java`, `CoreBridge.java`, `CoreMenu.java` | 1 806 / 70 / 239 / 1 305 | Zeichenfläche und Schleife neu, Menü-Schalter |
| PC-Prüfung | `tools/test_gx_gl.cpp` (`gx_gl_check`, 38 Testgruppen), `tools/gx_chunk_render.cpp` | 2 951 / 342 | brauchen eine Vulkan-Fassung bzw. einen Schalter |
| Zustandsbrücke (alte GL-Zeichner) | `scene_gl.cpp`, `core_scene.cpp` … | – | bleibt GLES (nur Diagnose) |
| Alte Port-App mit Vulkan | `port/platform/android/vulkan_bootstrap.cpp`, `port/shaders/`, `port/CMakeLists.txt` | 538 | siehe unten „Was aus der alten App taugt“ |

## Was es bringt – und wem (Testerberichte aus `whole-program-status.md`)

| Gerät (Bericht) | Befund | Was Vulkan ändert |
|---|---|---|
| Galaxy S10e, Exynos 9820, **Mali-G76** (31.10.) | Zeichen-Thread 19,8 ms Rechenzeit von 21,1 ms je Bild; davon Zeichenaufrufe 7,0 ms, **Eckpuffer 5,9 ms** (Retroid ~1 ms), BP 4,6 ms; der Kern wartet auf den Renderer | **am meisten:** Zeichenaufrufe und Zustand billiger, Eckdaten per Kopie in einen Dauer-Ring (das ginge teils auch in GLES, Schritt V0b) |
| Redmi 15C, Helio G81, **Mali-G52 MC2** (28.10.) | 29,8 ms Rechenzeit für 862 Zeichnungen (≈ 35 µs je Zeichnung); das Spiel selbst schafft dort nur ~23 Bilder/s | Renderer deutlich schneller, das Spiel nicht – Gewinn begrenzt, bis der Spiel-Thread mithält |
| Galaxy A30s, Exynos 7904, **Mali-G71 MP2** (22.10.) | Renderer 40–74 ms für 450–890 Befehle, ob Chip oder Treiber unklar | ungewiss: sehr kleiner Chip, alte Mali-Vulkan-Treiber gelten als fehleranfällig |
| moto e13, Unisoc T606, **Mali-G57** (28.10.) | 9 135 Programme vorladen: 21,7 s Arbeit, Ruckler beim ersten Schuss | Pipelines kosten ähnlich; Vorteil nur, weil Vulkan sie auf mehreren Threads bauen kann |
| moto g72, **Mali-G57 MC2** (21.10.) | GPU-Zeitmessung stürzte im GL-Treiber ab, seitdem auf Mali aus | Vulkan-Zeitstempel funktionieren auf Mali meist: **bessere Messungen** |
| POCO X7 Pro, Mali (19.10.) | Grafikchip zu 96 % ausgelastet | wenig; etwas durch genau gesteuertes Laden/Speichern der Durchgänge |
| Anbernic RG 477V, **Mali-G615** (22.10.) | Mali-Übersetzer lehnte einen Shader ab, den Adreno nahm | mit SPIR-V übersetzt die App selbst: weniger Unterschiede zwischen Treibern |
| Galaxy A36, Adreno 710 (22.10.) | Rechenfehler im GL-Shader-Übersetzer (Filmfarben) | anderer Treiberweg: kann es beheben oder eigene Fehler haben – die Selbstprüfung bleibt nötig |
| Galaxy S7, Adreno 530 (29.10.) | Grafikchip ist der Engpass (23–31 ms) | kaum |
| Xiaomi 11 Lite NE, Adreno 642L | ~14 µs je Zeichnung | etwas |
| **Retroid Pocket Flip 2**, Adreno 650 | ~3 µs je Zeichnung, 0,76 ms für 268 Aufrufe | nichts – bleibt aber das Prüfgerät für richtige Bilder |

Grobe Erwartung für das S10e (nicht gemessen): der Zeichen-Thread von ~20 ms auf ~8–12 ms je Bild. Das ist
eine Schätzung aus Erfahrungswerten anderer Emulatoren (Dolphin, PPSSPP), keine Messung an CubeEvil4.

## Teil für Teil

Aufwand in Arbeitstagen eines Entwicklers, der Vulkan kennt (grob); Risiko: wie wahrscheinlich es länger dauert
oder auf Geräten Fehler zeigt.

### T1 Renderer-Schnittstelle: GL und Vulkan trennen

- **Heute:** `GxRenderer` (`gx_gl.h`) ist die Schnittstelle zum Rest der App, aber nach außen gibt es
  GL-Dinge her (`Xfb()` und `EfbColor()` liefern GL-Textur-Nummern, `Present` zeichnet in den aktuellen
  GL-Bildschirm, `WarmProgramCache` braucht einen GL-Kontext). Im Inneren (`GxRenderer::Impl`) sind
  GX-Register, Shader-Schlüssel, Bündel-Planung, Zwischenbild-Logik (60 FPS) und GL-Aufrufe vermischt.
- **Nötig:** eine gemeinsame Schicht „GX-Zustand“ (BP/XF-Register, `MakeKey`, `FillPixel`, `FillXfRegs`,
  Scissor/Viewport-Rechnung, `MergePlan`/`MergeQuiet`, `BlendDraw`/`RecordDraw`, `CanSkip`, Statistik) und zwei
  Ausgaben „GL“ und „Vulkan“ dahinter. Nach außen: Bild als Pixel lesen statt GL-Nummer (Tests,
  Bildschirmfoto), `Present` mit einem Ziel-Objekt statt „aktueller GL-Bildschirm“. `core_mode.cpp` wählt die
  Ausgabe beim Start.
- **Prüfung:** die GL-Ausgabe muss danach **bytegleich** zeichnen (`gx_chunk_render` mit den Bildfolgen,
  `gx_gl_check` 0 Fehler) – wie beim Bündel-Versuch.
- **Aufwand 4–6 Tage, Risiko mittel:** berührt den funktionierenden Renderer; wird schrittweise und mit
  Bildvergleich gemacht. Dieser Schritt lohnt sich nur, wenn Vulkan wirklich kommt.

### T2 Shader: GLSL ES → SPIR-V

- **Heute:** `GxVertexShader`/`GxFragmentShader`/`GxMergedVertexShader` erzeugen GLSL ES 3.00 als Text, der
  Treiber übersetzt. Die festen Shader des Renderers (Kopie, Palette, Anzeige, Zurückzeichnen, FXAA, Tiefenkarte,
  Autofokus) stehen als Text in `gx_gl.cpp`.
- **Nötig:**
  - Der Erzeuger schreibt eine zweite Fassung: `#version 450`, `layout(set, binding)` für die Uniform-Blöcke und
    die acht Texturen, `uMtx`/`uWiden` als „Push-Konstanten“ (kleine Werte direkt im Befehl), `gl_VertexID` →
    `gl_VertexIndex`. Die eigentliche TEV-, Licht- und Nebel-Rechnung bleibt Zeile für Zeile gleich.
  - **Tiefe:** Vulkan rechnet die Tiefe von 0 bis 1, GLES von −1 bis 1 – eine Zeile im Vertex-Shader (die Formel
    mit `16777215.0`). Die Bildzeilen liegen in Vulkan genauso im Speicher wie heute (Zeile 0 oben im EFB), die
    Pixel-Mitte-Korrektur (7/12) bleibt.
  - **Übersetzen zur Laufzeit:** die TEV-Shader sind Kombinationen (heute 9 174 Schlüssel in der Liste), sie
    lassen sich nicht alle beim Bauen vorübersetzen. Also **glslang** als Bibliothek in die App (Khronos,
    freie Lizenz, grob 2–3 MB je ABI in der APK). Dazu ein Zwischenspeicher für SPIR-V auf dem Gerät.
  - Die **festen** Shader dagegen beim Bauen mit `glslc` aus dem NDK übersetzen (so machte es die alte App,
    `port/CMakeLists.txt` + `tools/embed_spirv.py`).
- **Gewinn:** der Treiber übersetzt keinen Text mehr (Mali-Strenge wie beim RG 477V entfällt).
- **Aufwand 3–4 Tage, Risiko niedrig–mittel.** Die strenge Prüfung `check_shaders_strict.sh` nutzt schon
  `glslangValidator`; sie kann jede erzeugte Fassung zusätzlich nach SPIR-V übersetzen und mit `spirv-val`
  prüfen.

### T3 Pipelines und Programm-Cache, Vorwärmen

- **Heute:** ein GL-Programm je Shader-Schlüssel (`ProgramFor`), Mischen/Tiefe/Seite/Farbmaske/Linienbreite
  werden je Zeichenbefehl als Schalter gesetzt (`ApplyState`). Programm-Binärdateien im Cache (`RE4P`),
  Schlüssel daneben (`RE4K`), Liste der App `gx-programs.keys` (`RE4L`), Vorwärmen auf einem zweiten
  GL-Kontext (`WarmProgramCache`) und in Bildpausen (`Prewarm`).
- **Nötig:**
  - Pipeline-Schlüssel = Shader-Schlüssel **plus** Zeichenzustand: Mischen (Faktoren, Subtrahieren,
    Logik-Ersatz), Tiefe (an/aus, Vergleich, Schreiben), Seite (Culling), Farbmaske, Punkte/Linien/Dreiecke,
    Probenzahl (MSAA). Scissor, Viewport und Linienbreite sind in Vulkan zum Glück „dynamisch“ (im Befehl).
    Mit der Erweiterung `VK_EXT_extended_dynamic_state` werden auch Tiefe und Seite dynamisch – nicht alle
    älteren Mali-Treiber haben sie, also muss es auch ohne gehen.
  - Erwartung: **1,5- bis 3-mal so viele Pipelines** wie heute Programme (nicht gemessen; der Zähler dafür ginge
    schon heute in GL einzubauen: wie viele verschiedene Paare Programm + Zustand ein Raum braucht).
  - Pipeline-Cache als Datei (`vkGetPipelineCacheData`; Vulkan prüft selbst, ob er zum Treiber passt). Neue
    Schlüssel-Liste mit Zustand (z. B. `RE4V`), `gx_program_list.py` lernt sie. Vorwärmen auf mehreren
    Hilfs-Threads (in Vulkan erlaubt, ohne zweiten Kontext) – das hilft schwachen Geräten wie dem moto e13.
  - Später möglich, nicht Teil des Plans: ein „Ubershader“ wie bei Dolphin (ein großer Shader für alle
    TEV-Fälle, kein Bauen mehr, dafür langsamer auf dem Chip).
- **Aufwand 5–7 Tage, Risiko hoch:** Ruckler bei neuen Effekten sind das bekannte Problem aller
  Vulkan-Emulatoren; das Bauen einer Pipeline kostet etwa so viel wie heute ein Link (Retroid ~16 ms, auf Mali
  nicht gemessen), und es werden mehr.

### T4 Durchgänge: EFB, EFB-Kopien, Löschen, Bildschirmkopie, MSAA

- **Heute:** EFB als GL-Framebuffer (Farbe RGBA8, Tiefe 24 Bit) in der gewählten Auflösung (`halves`).
  `EfbCopy` wechselt in ein Kopie-Ziel und zeichnet die Region mit dem Kopie-Shader (Formatumwandlung,
  Halbierung, Letterbox-Ränder), `ClearAfterCopy` löscht die Region, die Bildschirmkopie geht ins XFB, danach
  FXAA. MSAA in eigenen Puffern mit `glBlitFramebuffer` vor jeder Kopie (Entscheidung 19.10.2026: der
  Kachel-Weg `EXT_multisampled_render_to_texture` verlor auf dem Adreno 650 Tiefe und Haare).
- **Nötig:**
  - Jede EFB-Kopie: EFB-Durchgang beenden (Inhalt speichern), Barriere, Kopie-Durchgang (liest das EFB als
    Textur), EFB-Durchgang mit „alten Inhalt laden“ wieder beginnen. Heute 8–10 Kopien je Bild in den
    Belagerungsräumen, dazu Bildschirmkopie und Autofokus: **10–15 Durchgänge je Bild**. Vulkan macht das
    nicht teurer als GLES, aber die App muss jeden Übergang richtig angeben – häufigste Fehlerquelle.
  - Einfache Kopien (volle Größe, keine Umwandlung) könnten `vkCmdBlitImage` nehmen; der Rest bleibt ein
    kleiner Zeichen-Durchgang wie heute.
  - **Löschen nach der Kopie:** `vkCmdClearAttachments` innerhalb des Durchgangs, mit Rechteck. Achtung: es
    beachtet die Farbmaske nicht; löscht das Spiel nur Farbe ohne Alpha (oder umgekehrt), braucht es ein
    gezeichnetes Rechteck.
  - **MSAA:** EFB mit mehreren Proben plus „Auflösen“ am Durchgangsende. Zwei Wege: (a) Proben am Ende jedes
    Durchgangs speichern und wieder laden (wie heute „eigene Puffer“, teuer auf Kachel-GPUs bei 10–15
    Durchgängen); (b) Proben gar nicht speichern, nur das Aufgelöste, und am Anfang des nächsten Durchgangs
    zurückzeichnen (wie Schritt d2 heute). In Vulkan ist (b) **genau festgelegt** (keine „undefinierten“
    Proben wie bei der GL-Erweiterung) und auf Kachel-GPUs billig – Hoffnung auf günstigeres MSAA, auf dem
    Gerät zu prüfen.
  - Tiefenformat: GLES `DEPTH_COMPONENT24`; in Vulkan `D24_UNORM_S8_UINT`, wo vorhanden (Adreno, Mali),
    sonst `D32_SFLOAT`. Der Kopie-Shader liest die Tiefe als 24-Bit-Zahl (Tiefenkopien, GXPeekZ); mit D32F
    muss er runden. Das Format muss als Textur lesbar sein (Gerätefähigkeit prüfen).
  - Auflösungswechsel (`Rescale`, „Auto“) und Übergabe der Kopien an einen neuen Renderer
    (`HandOverCopies`): neue Bilder und Framebuffer; einfacher als in GL, weil das Gerät bleibt.
- **Aufwand 7–10 Tage, Risiko hoch.**

### T5 Uniforms und Deskriptoren

- **Heute:** fünf Uniform-Blöcke (XFM, XFP, XFL, PX, Bündel-Block XFMM), jeder geänderte Teil an eine neue Stelle
  im Uniform-Stream (4 × 4 MB, Dauer-Mapping, Fence je Viertel); acht Texturen mit Samplern je Zeichnung.
- **Nötig:** der Stream passt fast 1:1: Vulkan kennt „dynamische Uniform-Puffer“, bei denen nur die Stelle
  (Offset) je Zeichnung mitgegeben wird. Dauer-Mapping ist in Vulkan der Normalfall. Für die acht Texturen
  je Zeichnung: Deskriptor-Sätze aus einem Vorrat je Bild, ein Satz je neuer Kombination aus Texturen und
  Samplern (in r101 360–815 Textur-Bindungen je Bild, A30s-Bericht), innerhalb eines Bildes wiederverwendet.
  Sampler (Wiederholen/Spiegeln, Filter, LOD, Anisotropie) wie heute aus einem Zwischenspeicher.
  Grenzen: Uniform-Blöcke bis 16 KB gehen überall (`MergeBlock` ist dafür schon ausgelegt).
- **Aufwand 3–4 Tage, Risiko mittel** (Speicher und Geschwindigkeit der Deskriptor-Verwaltung auf Mali).

### T6 Eckdaten, Indizes, gebündelte Zeichenbefehle

- **Heute:** je Bildstück die ganzen Eckdaten mit `glBufferData` neu (`Run`), Indizes ebenso; „Basis-Ecke“ nur
  mit GLES 3.2 oder Erweiterung; Zwischenbild-Ecken (60 FPS) in einem eigenen Ring.
- **Nötig:** ein Ring-Puffer für Ecken und Indizes wie für die Uniforms (Kopie per `memcpy`, kein
  Treiberaufruf). Die Basis-Ecke hat Vulkan immer (`vkCmdDrawIndexed`), das Bündeln braucht keinen Sonderfall.
  Die Eckformate bleiben (der Kern liefert schon Gleitkommazahlen; das Layout gehört zum Shader-Schlüssel).
- **Aufwand 2–3 Tage, Risiko niedrig.** (Der Ring ließe sich auch in GLES bauen: Schritt V0b.)

### T7 Texturen und Paletten

- **Heute:** `Upload` dekodiert auf der CPU nach RGBA8 (`gx_texture.cpp`) und lädt jede Stufe mit `glTexImage2D`;
  `Forget`/`DropTextures` löschen sofort; Kopien mit Palette (`PalettedCopy`) zeichnen über eine
  Paletten-Textur in eine neue Textur; weiße Ersatztextur.
- **Nötig:** Bilder mit Speicher anlegen (Bibliothek **VMA**, „Vulkan Memory Allocator“, MIT-Lizenz), Daten
  über einen Zwischenpuffer hochladen, Barrieren. **Löschen erst, wenn der Grafikchip fertig ist** (eine
  Warteliste je Bild) – heute macht das der GL-Treiber still. Budget 96 MB wie im Kern. Paletten-Kopien als
  kleiner Durchgang wie heute.
- **Aufwand 3–5 Tage, Risiko mittel** (Speicher auf Geräten mit wenig RAM).

### T8 Synchronisation und Bildtakt

- **Heute:** `GLSurfaceView` ruft `draw` bei jedem Vsync (oder jedem n-ten bei 90/120 Hz, `FramePacing`), der
  Choreographer gibt dem Kern den VI-Takt (`CoreBridge.vsync`), `eglSwapBuffers` wartet. Fences nur für den
  Uniform-Stream, die Tiefenkarte und die Messung „wie weit hinten“.
- **Nötig:** zwei bis drei „Bilder im Flug“ mit je eigenem Befehlspuffer, Deskriptor-Vorrat und Ring-Abschnitt;
  Fences je Bild; Semaphoren für Swapchain (Bild holen, Bild zeigen); Present-Modus FIFO (wartet auf Vsync).
  Eigener Zeichen-Thread in C++ statt des Threads von `GLSurfaceView`; das „jedes n-te Vsync“ und der 60-Hz-Wunsch
  bleiben (Java), die Darstellung folgt ihnen. Optional später die Android-Bibliothek „Frame Pacing“ (Swappy).
  60 FPS (Zwischenbilder) und Frame skip ändern sich dadurch nicht, sie bleiben Renderer-Logik.
- **Aufwand 4–6 Tage, Risiko hoch:** Ruckeln und Hänger entstehen genau hier; auf Mali sind Wartezeiten anders
  als auf Adreno.

### T9 Tiefe für GXPeekZ

- **Heute:** bei der Bildschirmkopie eines markierten Bildes ein kleiner Durchgang (320 × 264 Zellen,
  24-Bit-Werte als RGBA8), `glReadPixels` in einen PBO, Fence, ein Bild später an den Kern (`DeliverDepth`).
- **Nötig:** derselbe Durchgang, `vkCmdCopyImageToBuffer` in einen lesbaren Puffer, Fence, ein Bild später
  lesen. Passt gut zu Vulkan.
- **Aufwand 1–2 Tage, Risiko niedrig.**

### T10 Die Extras des Renderers

| Extra | Heute | Vulkan | Aufwand |
|---|---|---|---|
| Autofokus / Tiefenschärfe (`Autofocus`, halbe Auflösung) | 3–4 eigene Durchgänge, Fokus als 3 × 1-Textur | dieselben Durchgänge, Shader nach SPIR-V | 2–3 Tage |
| FXAA | ein Durchgang nach der Bildschirmkopie | gleich | 0,5 Tage |
| 60 FPS (`Interpolate`, `BlendDraw`) | Bildstück ein zweites Mal mit gemischten Matrizen, eigene Kopien | Logik geteilt (T1), Ecken-Ring, eigene Kopie-Bilder | 2–3 Tage |
| Frame skip (`Skip`) | Zustand ohne Zeichnen | Logik geteilt | 0,5 Tage |
| Gebündelte Zeichenbefehle (`SetMerge`) | Ableger-Shader, Bündel-Block | eher einfacher (Basis-Ecke immer da) | 1 Tag |
| GPU-Selbstprüfung Filmfarben (`gx_selfcheck.cpp`, `TevMath`) | rechnet TEV-Stufen auf dem Chip nach | eigene Fassung, weil anderer Treiberweg (seit 0.99.13 gebaut: dieselbe Prüfung, gelesen über die Bildschirmkopie, `MovieColourCheck(…, GxApi::kVulkan)`) | 1 Tag |
| Messungen (`GxStats`, GPU-Zeit, „wie weit hinten“, `RE4_GX_COPY_TIME`, GPU-Messlauf) | GL-Zeitabfrage (auf Mali aus) | Vulkan-Zeitstempel, auch auf Mali | 1–2 Tage |
| Bildschirmfoto, R3-Aufnahme, `gxDump`/`-GxReplay` | `glReadPixels`, `glFinish` | Bild in Puffer kopieren | 1 Tag |

Zusammen **9–12 Tage, Risiko mittel.** Die Zustandsbrücke (alte GL-Zeichner) bleibt GL-only und ist mit Vulkan
nicht verfügbar (nur Diagnose).

### T11 Android: Zeichenfläche, Swapchain, Drehung, Pausieren, Menü

- **Heute:** `CoreActivity` erzeugt `GLSurfaceView` (GLES 3, Kontext bleibt beim Pausieren), dessen Thread ruft
  `CoreBridge.surfaceCreated/resize/draw`; ein neuer GL-Kontext heißt „Renderer neu, der Kern schickt alles
  noch einmal“. `screenOrientation="sensorLandscape"` (das Bild kann um 180° kippen).
- **Nötig:**
  - Für Vulkan eine einfache `SurfaceView`, deren `Surface` per JNI an C++ geht (`ANativeWindow`), dort
    Vulkan-Fläche und Swapchain; neu anlegen bei Größenwechsel, nach dem Pausieren und bei 180°-Drehung.
    Beim Pausieren bleibt das Vulkan-Gerät mit allen Texturen bestehen – **nach dem Zurückkehren muss der Kern
    nicht alles neu schicken** (ein kleiner Gewinn gegenüber heute).
  - **Pre-Rotation:** Vulkan meldet, wie der Bildschirm gedreht ist; die Anzeige (`Present`) zeichnet das
    Bild gleich gedreht und zeigt es mit dieser Drehung an, sonst dreht Android jedes Bild noch einmal (kostet
    auf manchen Geräten spürbar). Die alte App verlangte „keine Drehung“ und brach sonst ab – das reicht nicht.
  - Menü: Schalter unter Leistung → Experimentell, etwa „Graphics API: OpenGL ES / Vulkan (experimental)“, in
    allen zehn Sprachen (`strings_ingame.xml`, `check_translations.py`), Hilfetext. Weil die Zeichenfläche eine
    andere Art ist, wirkt der Wechsel vermutlich erst nach einem Neustart der App (zu klären).
  - Fehlerbericht: welche Ausgabe lief, Vulkan-Gerät, Treiber- und API-Version, Erweiterungen; Abstürze im
    Vulkan-Treiber erkennbar machen.
- **Aufwand 4–6 Tage, Risiko mittel–hoch** (Lebenszyklus der App, Geräteunterschiede).

### T12 Gerätefähigkeiten, Rückfall auf GLES, Mindestversion

- **Android-Versionen:** die App (Kernmodus) läuft ab Android 6 (`minSdk 23`). Vulkan gibt es erst ab Android 7
  (API 24); ab Android 10 müssen neue 64-bit-Geräte Vulkan 1.1 können. Folge: `libvulkan.so` nur per `dlopen`
  laden (wie die alte App), nie fest verlinken, sonst startet die App auf Android 6 nicht. Im Manifest Vulkan
  nur als „nicht nötig“ angeben (`android.hardware.vulkan.version`, `required="false"`).
- **Prüfung beim Start:** Vulkan-Version, Tiefenformat lesbar, Anisotropie, MSAA-Stufen, Puffergrößen, optionale
  Erweiterungen (dynamischer Zustand). Fehlt etwas, schlägt das Anlegen fehl oder meldet die Selbstprüfung
  falsche Pixel: **automatisch GLES** und eine Zeile im Protokoll.
- **Sperrliste:** bekannte schlechte Treiber (alte Mali-Bifrost-Treiber, PowerVR GE8xxx in billigen Handys)
  bleiben auf GLES – die Liste wächst mit den Berichten.
- **Aufwand 2–3 Tage, Risiko mittel.**

### T13 Bauen

- Das NDK (r28) bringt die Vulkan-Kopfdateien und `glslc` mit. Neu: glslang als Quelltext (oder fertige
  Bibliothek) in `app/src/main/cpp/CMakeLists.txt`, VMA (eine Kopfdatei), die festen Shader beim Bauen nach
  SPIR-V. Die Validation Layer nur in Debug-APKs (die alte App hatte dafür `re4.validationLayers`).
- **Aufwand 1–2 Tage, Risiko niedrig.** APK etwa 2–3 MB größer je ABI.

### T14 PC-Werkzeuge: `gx_chunk_render`, `gx_gl_check` mit lavapipe

- **Heute:** beide laufen in WSL/Cloud mit Mesa (llvmpipe, für MSAA auf den Kacheln `zink` auf lavapipe), lesen
  Bilder mit `glReadPixels` (7 Stellen in `test_gx_gl.cpp`) und vergleichen Pixel; `whole_r100_check.sh`
  Abschnitt 5 zeichnet alle GX-Bilder in mehreren Arten.
- **Nötig:** `gx_chunk_render` mit Schalter `RE4_GX_API=vulkan` (Bilder über die neue Pixel-Lese-Funktion aus
  T1); `gx_gl_check` die 38 Testgruppen über die Schnittstelle statt GL-Nummern, einmal je Ausgabe;
  Vulkan-Lauf auf **lavapipe** mit Validation Layer (meldet jede vergessene Barriere – die wichtigste Hilfe ohne
  Gerät). Pakete: `mesa-vulkan-drivers` (schon in `whole_setup_linux.sh`), dazu `vulkan-validationlayers`,
  `spirv-tools`; der 32-bit-Host-Build braucht ggf. die i386-Fassung.
- **Bildvergleich GL gegen Vulkan:** llvmpipe und lavapipe rechnen mit demselben Mesa-Rasterer; erwartet werden
  (fast) gleiche Bilder – ein sehr strenger Vergleich für jedes GX-Bild der PC-Prüfung (`picture_compare.py`).
  Ob sie wirklich bytegleich sind, zeigt erst der Versuch.
- **Aufwand 4–6 Tage, Risiko mittel.**

### T15 Testskripte

- `test-core-app.ps1`: mit `-CoreSettings RE4_GX_API=vulkan` auf dem Retroid; `-GxReplay` und `-GpuBench` für
  beide Ausgaben; `whole_r100_check.sh`: Vulkan-Lauf und Vergleich; `check_shaders_strict.sh`: SPIR-V-Fassung;
  Startskript der Cloud-Sitzung: Vulkan-Pakete.
- **Aufwand 1–2 Tage, Risiko niedrig.**

## Was aus der alten App taugt (`port/android/`, `port/platform/android/vulkan_bootstrap.cpp`)

Die alte App hatte nur einen **Einstieg**: Vulkan starten, ein festes 512 × 448-EFB löschen, wenige feste
Pipelines eines Diagnose-Shaders, ein Befehlspuffer mit Warten nach jedem Bild.

- **Brauchbar (als Vorlage, ~200 Zeilen):** Laden von `libvulkan.so` per `dlopen` mit Funktionstabellen
  (Makro-Listen), Wahl von Gerät und Warteschlange, Validation Layer mit Meldungs-Rückruf (nur Debug), Swapchain
  mit Prüfung von Format und Alpha-Modus, Neuanlegen bei „veraltet“, `glslc`-Schritt und `embed_spirv.py` für
  feste Shader, Pipeline-Cache-Objekt, Gradle-Eigenschaft für die Layer.
- **Nicht brauchbar:** das Zeichnen selbst (feste Größe, `D32_SFLOAT` ohne Prüfung, 15 feste Pipelines, kein
  GX-Zustand), die Serialisierung (`QueueWaitIdle`), die Drehung (bricht ohne „keine Drehung“ ab),
  `GraphicsBootstrapView.java`.

## Aufwand und Risiko im Überblick

| Teil | Aufwand (Tage) | Risiko |
|---|---|---|
| T1 Schnittstelle trennen | 4–6 | mittel |
| T2 Shader nach SPIR-V | 3–4 | niedrig–mittel |
| T3 Pipelines, Cache, Vorwärmen | 5–7 | hoch |
| T4 Durchgänge, Kopien, MSAA | 7–10 | hoch |
| T5 Uniforms, Deskriptoren | 3–4 | mittel |
| T6 Eckdaten, Bündeln | 2–3 | niedrig |
| T7 Texturen, Paletten | 3–5 | mittel |
| T8 Synchronisation, Bildtakt | 4–6 | hoch |
| T9 Tiefe für GXPeekZ | 1–2 | niedrig |
| T10 Extras | 9–12 | mittel |
| T11 Android-Anbindung | 4–6 | mittel–hoch |
| T12 Fähigkeiten, Rückfall | 2–3 | mittel |
| T13 Bauen | 1–2 | niedrig |
| T14 PC-Werkzeuge | 4–6 | mittel |
| T15 Testskripte | 1–2 | niedrig |
| **Zusammen** | **53–78** | |

Dazu kommen Gerätetests: der Retroid zeigt, ob die Bilder stimmen; ob es schneller wird, zeigen nur Mali-Geräte
der Tester (Test-APKs, mehrere Runden). Neue Spiel-Fehlerberichte laufen in dieser Zeit weiter über GLES.

## Vorgeschlagene Reihenfolge

| Schritt | Inhalt | Wo | Ergebnis |
|---|---|---|---|
| **V0** | **ANGLE-Versuch ohne Code:** ein Tester mit Mali-Handy (S10e oder Redmi 15C) schaltet in den Entwickleroptionen für CubeEvil4 ANGLE ein (Android 10+, je nach Hersteller „ANGLE-Einstellungen“ bzw. „Grafiktreiber-Einstellungen“) und schickt einen Fehlerbericht aus demselben Raum | Tester | zeigt, ob GLES-über-Vulkan dort schneller ist. Schneller: deutlicher Hinweis, dass Vulkan lohnt. Gleich oder langsamer: noch kein Beweis dagegen (ANGLE hat eigene Kosten). Nicht jedes Gerät bietet den Schalter |
| **V0b** | GLES: Eckdaten und Indizes über einen Dauer-Ring wie den Uniform-Stream (T6, aber für GLES) – **umgesetzt 31.10.2026**, am PC bytegleich, Gerät offen (Abschnitt „V0b“ unten) | PC + Retroid + Mali-Tester | trifft die 5,9 ms „vertex buffers“ des S10e direkt, hilft allen Geräten, 1–2 Tage, abschaltbar |
| *Entscheidung* | Weiter mit Vulkan oder nicht (Projektinhaber) | | |
| V1 | T1: Schnittstelle trennen, GL bytegleich | PC (Cloud oder WSL) | GL unverändert, Grundlage |
| V2 | T13 + Gerüst: Vulkan auf lavapipe, EFB, einfache Zeichnung, Bildschirmkopie in ein Bild, Pixel lesen | PC | erste `gx_gl_check`-Gruppen grün |
| V3 | T2, T3, T5, T6, T7: alle Zeichnungen, Texturen, Cache | PC | alle Grundtests, erste GX-Bilder gleich wie GL |
| V4 | T4, T9: Kopien, Paletten, Löschen, MSAA, Tiefenkarte | PC | alle GX-Bilder der PC-Prüfung GL gegen Vulkan verglichen |
| V5 | T11, T12, T8: Android, Swapchain, Drehung, Rückfall, Schalter (versteckt) | Retroid | Spiel läuft mit Vulkan, Bilder wie GLES |
| V6 | T10: Autofokus, FXAA, 60 FPS, Frame skip, Bündeln, Messungen, Selbstprüfung | PC + Retroid | alle Menü-Optionen auch mit Vulkan |
| V7 | Test-APK an Mali-Tester, messen (Zeitstempel, Render-Thread) | Tester | Entscheidung, ob und für welche Geräte Vulkan Standard wird |
| V8 | T14/T15 fertig, Doku, Release | PC | |

## V0: ANGLE-Versuch beim Tester (Anleitung)

Entscheidung des Projektinhabers (31.10.2026): erst V0 und V0b. Kein Code nötig. Was dabei passiert: Android
schickt die GLES-Befehle von CubeEvil4 durch ANGLE, das sie in Vulkan-Befehle übersetzt. Läuft das Spiel so auf
einem Mali-Handy flüssiger, ist der GLES-Treiber des Herstellers der Bremsklotz – genau das, was eine eigene
Vulkan-Ausgabe beheben würde.

- **Woran man sieht, dass ANGLE lief:** im Fehlerbericht steht in der Zeile `RE4GX: GPU: …` dann
  `ANGLE (…, Vulkan …)` statt nur `Mali-…`. Fehlt das, hat das Gerät kein ANGLE (bei manchen Herstellern nicht
  eingebaut) – dann sagt der Versuch nichts.
- **Was wir vergleichen:** dieselbe Version, dieselben Einstellungen, derselbe Raum (am besten das Dorf r101,
  ein paar Minuten nach dem Laden), einmal ohne, einmal mit ANGLE; aus den Berichten die Zeilen
  „GX render thread: … ms of processor time per chunk“, „GX core: …“ und die Bilder je Sekunde.
- **Achtung:** mit ANGLE baut die App beim ersten Start alle Shader neu (der Programm-Cache gehört zum
  Treiber): die ersten Minuten ruckeln, das Vorladen dauert länger. Erst danach messen.

Text für den Tester (englisch, zum Weiterleiten):

```
Quick performance experiment (no new APK needed, takes ~15 minutes):

1. Play CubeEvil4 as usual for 3-4 minutes in the village (r101) with your normal settings,
   then send an error report from the in-game menu ("before").
2. Switch CubeEvil4 to ANGLE (Android 10 or newer):
   Settings > Developer options > "ANGLE Preferences" (on some phones "Graphics driver
   preferences"), choose CubeEvil4, select "angle".
   Or with a PC and ADB:
     adb shell settings put global angle_gl_driver_selection_pkgs com.ast.cubeevil4
     adb shell settings put global angle_gl_driver_selection_values angle
3. Start CubeEvil4 again. The first minutes may stutter while shaders are rebuilt - that is
   expected. Load the same save, play 3-4 minutes in the same place, then send an error
   report ("with ANGLE"). Tell us whether it felt smoother, the same, or worse.
4. To switch back: select "default" in the same menu, or
     adb shell settings delete global angle_gl_driver_selection_pkgs
     adb shell settings delete global angle_gl_driver_selection_values

If your phone has no such option, or nothing changes, that is a useful answer too.
```

## V0b: Eckdaten-Ring in GLES (umgesetzt 31.10.2026, Branch `vulkan`)

- **Was:** die Ecken und Indizes jedes Bildstücks kommen nicht mehr mit `glBufferData` in einen frisch angelegten
  Puffer (der Treiber legt dabei jedes Mal neuen Speicher an und kopiert), sondern per einfacher Kopie in zwei
  Ring-Puffer, die einmal für immer eingeblendet sind („Dauer-Mapping“, `EXT_buffer_storage`), in Vierteln mit
  einem Fence wie der Uniform-Stream. Ein Viertel fasst mindestens das größte Bildstück (anfangs 2 MB Ecken,
  512 KB Indizes; größere Bildstücke legen den Ring größer an). Die Plätze werden vor dem ersten Zeichenbefehl
  eines Bildstücks vergeben, nie mitten im Bild. Gebündelte Zeichenbefehle und die 60-FPS-Zwischenbilder nehmen
  denselben Ring.
- **Schalter:** an als Standard (`GxOptions::vertexRing`); `RE4_GX_VERTEX_RING=0` oder `RE4_GX_PERSISTENT=0` (in
  den Start-Einstellungen bzw. `-CoreSettings`) oder ein Treiber ohne `EXT_buffer_storage`: wie bisher. Im
  Protokoll „GX vertex ring: on …/off …“, in der Zeile „GX work per chunk“ neu „N vertex ring waits“
  (Wartezeiten auf den Grafikchip, sollten 0 sein).
- **Geprüft am PC** (WSL `~/re4-vulkan/RE4`, `app-host-gl`, llvmpipe): `gx_gl_check` 0 Fehler mit und ohne Ring, neue
  Prüfung `TestVertexRing` (48 Bildstücke, der Ring läuft mehrmals um, wächst einmal für 2,8 MB, jedes Bild wie
  ohne Ring); `gx_chunk_render` mit 802 Spielbildern (Folgen r11d, r11b, r204, r101, Film; je normal, gebündelt,
  mit Zwischenbildern, 4× MSAA): **alle bytegleich** mit und ohne Ring. Auf zink (lavapipe) dieselben 9 Fehler der
  Tiefenkarte wie ohne Ring und wie der ältere Stand in `~/re4-kern` (nicht von dieser Änderung).
- **Retroid (Adreno 650, 31.10.2026):** zwei Läufe in r100 mit und ohne Ring, dieselbe Arbeit je Bild: „vertex
  buffers“ 1,1 statt 1,4 ms, Zeichen-Thread 4,15 statt 4,48 ms je Bildstück (−7 %), GPU-Zeit gleich, 0 Wartezeiten,
  Bilder richtig. Kleiner Gewinn, kein Verlust.
- **Nicht geprüft:** auf einem Mali-Gerät (S10e: 5,9 ms „vertex buffers“ – dort sollte es deutlich mehr bringen).
- **Mali-Test-APK** (Wunsch des Projektinhabers, 31.10.2026: erst testen, dann `master`): Version
  **0.96.4-beta-ring**, versionCode 87, Branch `vulkan` = `master` 0.96.4 plus Ring. Installiert sich über 0.96.4;
  die Update-Prüfung der App hält sie für neuer als 0.96.4-beta und älter als 0.96.5 (der nächste echte Release
  braucht versionCode 88). Signieren wie immer in Android Studio aus `E:\Development\Android\RE4-vulkan\port\android-decomp`
  (Variante `coreRelease`). Vergleich: ein Bericht des Testers mit 0.96.x aus demselben Raum, Zeilen „GX render
  thread split per chunk … vertex buffers“ und „GX render thread: … ms of processor time“.

Text für den Tester (englisch, zum Weiterleiten):

```
Test build CubeEvil4 0.96.4-beta-ring (for Mali phones):
same as 0.96.4, plus one change - vertex data goes to the GPU through a ring buffer that stays
mapped, instead of a new buffer every frame. On your phone the graphics driver spent a lot of
time exactly there, so this may give a few more FPS.

1. Install it over your current version (discs, saves and settings stay).
2. Play 3-4 minutes in the village (r101) or wherever it was slow before, with your usual
   settings, then send an error report from the in-game menu (SELECT > Management > Send report).
3. Tell us whether it feels smoother, the same or worse than 0.96.4.
```

## Alternativen

- **ANGLE mitliefern:** ANGLE-Bibliotheken in die APK, GLES-Aufrufe laufen dann auf jedem Gerät über Vulkan.
  `gx_gl.cpp` bliebe ganz, Aufwand eher Tage als Monate; aber ANGLE kostet selbst Prozessorzeit, die APK wird
  größer (~10 MB+ je ABI) und Fehler lägen in fremdem Code. V0 zeigt, ob sich das lohnt.
- **GLES weiter verbessern:** V0b (Eckdaten-Ring), weniger Zustandswechsel, das Bündeln für Mali messen. Bringt
  weniger als Vulkan, aber ohne zweite Ausgabe.
- **Dolphin als Vorbild:** Dolphin (GameCube/Wii-Emulator) hat GX auf Vulkan, auch für Android und Mali. Seine
  Lösungen (Pipeline-Schlüssel, Ubershader, Umgang mit EFB-Kopien) taugen als Ideen; **Code nicht übernehmen**
  (Dolphin ist GPL, CubeEvil4s eigener Code MIT).

## Offene Entscheidungen (Projektinhaber)

1. **Ob:** erst V0 und V0b, dann entscheiden (Empfehlung) – oder gleich mit V1 beginnen, oder Vulkan weiter
   ausschließen wie am 29.10.2026.
2. **Aufbau:** gemeinsamer GX-Zustandsteil mit zwei Ausgaben (Empfehlung: Fehler im GX-Nachbau werden nur einmal
   behoben) – oder eine eigene Kopie `gx_vk.cpp` neben `gx_gl.cpp` (schneller begonnen, danach alles doppelt
   pflegen).
3. **Shader-Übersetzung:** glslang in der App zur Laufzeit (Empfehlung) – oder nur die Liste vorab übersetzen
   und neue Schlüssel ohne Vulkan zeichnen (geht nicht sauber).
4. **Mindestanforderung für Vulkan:** Vulkan 1.1 und Android 10 (Empfehlung: reifere Treiber) – oder schon ab
   Android 7 / Vulkan 1.0. Darunter immer GLES.
5. **Standard:** GLES bleibt Standard, Vulkan als Schalter „experimentell“ (Empfehlung) – später je Gerät
   automatisch, wenn Messungen es stützen.
