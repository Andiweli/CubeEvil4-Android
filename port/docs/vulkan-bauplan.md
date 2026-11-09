# Vulkan als zweiter Renderpfad – Bauplan

Auftrag des Projektinhabers (07.11.2026, über die Hauptsitzung): „einen zweiten Renderpfad über Vulkan planen und
bauen“, Branch `vulkan`. GLES bleibt Standard und Rückfall. Grundlage ist die Studie `vulkan-plan.md`
(31.10.2026, Teile T1–T15, Stufen V0–V8); dieser Plan bringt sie auf den Stand 0.99.2-rc und legt die Etappen
fest, in denen gebaut wird. Begriffe wie in der Studie (Abschnitt „Begriffe“).

## Ziel (wie bestellt)

- Vulkan **zusätzlich** zu GLES. GLES bleibt Standard und Rückfall: Geräte ohne oder mit schlechtem Vulkan, alte
  Android-Versionen, jeder Startfehler von Vulkan.
- Wahl im App-Menü, Seite Anzeige: „Renderer: OpenGL ES / Vulkan (experimental)“, mit Neustart-Frage wie beim
  Sprachwechsel; Texte und Hilfetext in allen 11 Menüsprachen.
- Kann Vulkan nicht starten, schaltet die App von selbst auf GLES und sagt es dem Spieler.
- Der Fehlerbericht nennt den aktiven Renderer und Gerät/Treiber.
- Fertig, wenn Vulkan am Gerät wählbar läuft, bildgleich zu GLES, mit allen Bildformaten und Filtern, stabilem
  Rückfall und **gemessenem** Vorteil – und der Projektinhaber die Übernahme nach `master` freigibt.

## Was seit der Studie (0.96.4) dazukam

| Neu seit 31.10. | Wo | Was es für Vulkan heißt |
|---|---|---|
| `gx_gl.cpp` wuchs von 5 293 auf 6 613 Zeilen | Renderer | mehr Stellen mit GL-Aufrufen; die Aufteilung (Etappe E1) wird größer |
| Nachschärfen (0.99.2, `Sharpen`) | ein Durchgang nach FXAA | eigener kleiner Durchgang, Shader nach SPIR-V |
| Original-TV-Filter (0.96.7) | Werte im Kopie-Shader | nur Shader-Werte, kein eigener Durchgang |
| PSone-Retrofilter (0.97.0, `Psx`) | Schlüssel-Bit, letzter Durchgang, Bild davor als Textur | eigener Durchgang mit zwei Bildern im Wechsel |
| Linsenreflexe (0.98.x, `Flare*`) | Suche nach Lichtquellen (Logik), ein Durchgang, liest die Tiefe des EFB | Logik geteilt, Durchgang neu, Tiefe als Textur lesbar nötig |
| Tiefenschärfe „Stark“ (0.98.4) | Werte der Autofokus-Shader | nur Werte |
| 60 FPS je Objekt (S0–S3), Laser (0.98.7) | reine Logik plus Eck-Ring | Logik geteilt; Ring wie bei den Eckdaten |
| Dynamic-Auflösung mit Benchmark (0.98.4/0.98.5) | `gx_auto_level.h`, `Rescale`, „wie weit hinten“ (Fences je Bildstück) | Vulkan muss dieselben Messwerte liefern (Fences, Wartezeit des Zeichen-Threads) |
| Bildformat „Native“, Widen/View-Aspect (0.99.0) | `WidenFor` (Shader-Wert), `XfbWide`, Rechtecke in `core_mode.cpp` | Rechnung geteilt; nur `Present` ist Backend-Sache |
| Vertex-Ring (V0b, Schalter seit 0.96.7) | GLES-Ring mit Fences | in Vulkan der normale Weg (immer an) |
| Zeichenaufrufe bündeln (Schalter) | `MergePlan` (Logik), Bündel-Shader | Logik geteilt; Vulkan hat die Basis-Ecke immer |
| Programme je Raum, höchstens 3 000 (0.96.7) | Raumlisten, Löschen unbenutzter Programme | gleich für Pipelines (die kosten auch Speicher im Treiber) |
| Minikarte (0.99.1) | eigener GL-Zeichner in `core_mode.cpp` | braucht einen Vulkan-Weg (als kleine Überlagerung über `Present`) |
| Force Performance (Kerne), Prewarm in Bildpausen | Zeichen-Thread | unter Vulkan ein eigener Zeichen-Thread, der genauso platziert wird |

Nicht dazugekommen: ein ANGLE-Ergebnis (V0) eines Mali-Testers ist nicht im Status vermerkt. Die Messung des
Mali-G52-Testers mit dem Vertex-Ring (Eckdaten 20–35 % billiger, Zeichen-Thread bis 15 % weniger, keine
höhere Bildrate) zeigt: der Treiber kostet dort viel, aber kleine GLES-Verbesserungen reichen nicht.

## Aufbau: ein gemeinsamer Teil, zwei Backends

Heute steckt alles in `GxRenderer::Impl` (`gx_gl.cpp`): GX-Register, Shader-Schlüssel, Zwischenbilder, Bündeln,
Effekt-Logik – und dazwischen etwa 900 GL-Aufrufe. Künftig:

```
core_mode.cpp ──► GxRenderer (gx_renderer.cpp: der GX-Nachbau, einmal für beide)
                     │  ruft nur die Schnittstelle GxBackend (gx_backend.h)
                     ├──► GxBackendGl  (gx_backend_gl.cpp: genau die heutigen GL-Aufrufe)
                     └──► GxBackendVk  (gx_backend_vk.cpp + vk_*.cpp: Vulkan)
```

- **Die Schnittstelle spricht GX-Renderer-Sprache, nicht GL-Sprache:** „zeichne mit diesem Shader-Schlüssel und
  diesem Zeichenzustand (Mischen, Tiefe, Seite, Farbmaske, Grundform)“, „lade diesen Uniform-Block“, „lege eine
  Textur an / lade eine Stufe / vergiss sie“, „Ziel: EFB / Kopie X / Bildschirm“, „Hilfsdurchgang Kopie / Palette /
  FXAA / … mit diesen Werten“, „lies Pixel“, „Fence nach dem Bildstück“. Was heute `ApplyState` aus den
  BP-Registern rechnet, wird eine reine Rechnung (`DrawState`), die beide Backends bekommen.
- **Fehler im GX-Nachbau werden nur einmal behoben** (Empfehlung der Studie, Pflichtpunkt der Hauptsitzung).
- **GL bleibt bytegleich:** die GL-Ausgabe zeichnet nach der Aufteilung jedes Prüfbild Byte für Byte wie vorher, und
  ihre Shader-Texte bleiben gleich (sonst baut jedes Gerät seinen Programm-Cache neu).
- **Durchgänge (Render-Passes) verwaltet das Vulkan-Backend selbst:** der gemeinsame Teil sagt nur „Ziel X“; das
  Backend beendet den laufenden Durchgang, wenn das Ziel wechselt oder eine Kopie das EFB liest, setzt die
  Barriere und beginnt den nächsten mit „alten Inhalt laden“ (oder „löschen“, wenn das Spiel ohnehin alles löscht).
- Die **Zustandsbrücke** (alte GL-Zeichner, nur Diagnose, `RE4_VIEW=bridge`) bleibt GL. Mit ihr startet immer GLES.
- Merge-Risiko: `master`/`work` ändern `gx_gl.cpp` weiter. Ich ziehe `master` regelmäßig nach `vulkan` und
  übertrage neue Renderer-Änderungen von Hand in den gemeinsamen Teil; je früher E1 steht, desto kleiner der Abstand.

## Etappen

Jede Etappe wird einzeln committet, mit Eintrag in `whole-program-status.md`, und endet in etwas Prüfbarem –
erst am PC (WSL, Mesa: llvmpipe für GL, lavapipe für Vulkan, mit Validation Layer), dann am Gerät.
Aufwand in Arbeitstagen eines Entwicklers (wie in der Studie), Risiko: wie wahrscheinlich es länger dauert.

| Etappe | Inhalt | Prüfung am PC | Prüfung am Gerät | Tage | Risiko |
|---|---|---|---|---|---|
| **E0** Werkzeuge | Vulkan-Laden per `dlopen` (aus der alten App), glslang aus dem NDK für Android und PC bauen, kleines Prüfprogramm `vk_probe` (Gerät anlegen, GLSL nach SPIR-V, Dreieck zeichnen, Pixel lesen); in WSL Validation Layer, `spirv-val`, `glslangValidator` (ohne Systemänderung in einen eigenen Ordner) | `vk_probe` auf lavapipe ohne Meldung der Prüfschicht; Größe von glslang in der APK gemessen | – | 1–2 | niedrig |
| **E1** Aufteilung | `GxBackend`-Schnittstelle, GL-Backend mit den heutigen Aufrufen, Schritt für Schritt (Zustand und Zeichnen, Uniforms, Texturen und Sampler, EFB und Kopien, Extras, Lesen); `Xfb()`/`EfbColor()` liefern Pixel statt GL-Nummern | `gx_gl_check` 0 Fehler; `gx_chunk_render` mit frischen GX-Folgen (r100, r101, r11d, r11b, r204, Film) in allen Arten (normal, gebündelt, 60 FPS, 2×/4× MSAA, FXAA, Nachschärfen, TV-Filter, PSone, Linsenreflexe, Tiefenschärfe leicht/stark) **bytegleich** vor/nach; Kernprüfung grün | Retroid: Kurztest mit Debug-APK, keine Änderung sichtbar | 6–9 | mittel |
| **E2** Shader-Dialekt | Shader-Erzeuger und feste Shader schreiben zusätzlich GLSL 450 für Vulkan (Bindungen, Push-Konstanten, Tiefe 0..1); GL-Text unverändert | jeder Schlüssel der App-Liste `gx-programs.keys` (über 9 000) übersetzt nach SPIR-V, `spirv-val` fehlerfrei; GL-Text vor/nach gleich | – | 2–3 | niedrig |
| **E3** Vulkan-Grundgerüst | Gerät, Speicher (Blöcke, eigener kleiner Verwalter), EFB, Uniform- und Eck-Ring je Bild im Flug, Pipelines mit Cache, Deskriptoren, Texturen (Hochladen vor dem Bild), Zeichnen, Bildschirmkopie, Pixel lesen; `gx_chunk_render` und `gx_gl_check` mit Schalter `RE4_GX_API=vulkan` | erste GX-Bilder (Logos, Titelmenü, r100 ohne Effekte) GL gegen Vulkan verglichen; Prüfschicht ohne Meldung | – | 8–12 | hoch |
| **E4** Kopien und Durchgänge | EFB-Kopien aller Formate, Paletten-Kopien, Löschen nach der Kopie (auch Farbe ohne Alpha), Tiefenkopien, GXPeekZ-Tiefenkarte, MSAA (Auflösen vor jeder Kopie), Auflösungswechsel (`Rescale`), Übergabe der Kopien | alle Bilder der GX-Folgen GL gegen Vulkan (`picture_compare.py`), auch 2×/4× MSAA; Prüfschicht ohne Meldung | – | 7–10 | hoch |
| **E5** Extras | FXAA, Nachschärfen, TV-Filter, PSone, Linsenreflexe, Autofokus/Tiefenschärfe (leicht/stark, halbe Auflösung), 60-FPS-Zwischenbilder, Frame skip, Bündeln, Messungen (Vulkan-Zeitstempel), Selbstprüfung der Filmfarben | alle Arten aus E1 GL gegen Vulkan; `gx_gl_check`-Gruppen auch gegen Vulkan | – | 7–10 | mittel |
| **E6** Android | Zeichenfläche `SurfaceView` mit eigenem Zeichen-Thread (gleicher Takt wie heute: `FramePacing`, Choreographer), Swapchain mit Pre-Rotation, Pausieren/Fortsetzen ohne neuen Renderer, Größenwechsel, 180°-Drehung, `Present` für alle Bildformate, Minikarte, Bildschirmfoto/GX-Dump; Rückfall auf GLES bei jedem Startfehler; Schalter zunächst versteckt (`RE4_GX_API=vulkan`) | Java-Bau, App-Tests | Retroid: r100/r101/Film mit Vulkan, Bilder wie GLES (Fotos verglichen), Pausieren/Fortsetzen, Drehen, Rückfall erzwungen; **Test-APK `0.99.2-vulkan1`** | 6–9 | mittel–hoch |
| **E7** Menü, Bericht, Rückfall | „Renderer: OpenGL ES / Vulkan (experimental)“ auf der Seite Anzeige, Neustart-Frage, 11 Sprachen mit Hilfetext; Fehlerbericht mit Renderer, Gerät, Treiber, Vulkan-Version; Absturz-Wächter (stürzt Vulkan beim Start ab, startet die App danach mit GLES und sagt es); Sperrliste | `check_translations.py`, App-Tests | Retroid: Umschalten hin und zurück, erzwungener Fehler, Bericht; **Test-APK `vulkan2`** | 2–4 | niedrig |
| **E8** Leistung | Pipelines vorab auf Hilfs-Threads und als Datei-Cache, Raumlisten, Prozessorzeit je Bildstück GL gegen Vulkan messen, Durchgänge für Kachel-GPUs feinstellen (Laden/Speichern/Löschen) | Messzeilen im Protokoll gleich für beide | Retroid messen; **Test-APK an Mali-Tester** (S10e, Redmi 15C …), mehrere Runden | 5–8 + Tester | hoch |
| **E9** Abschluss | `master` nachziehen, Doku, Status, Releasetext; Freigabe durch den Projektinhaber | volle Kernprüfung | Kurztest | 1–2 | niedrig |
| | **Zusammen** | | | **45–69** + Testerrunden | |

Weniger als die 53–78 Tage der Studie, weil V0b (Ring) schon gebaut ist und die Studie die Teile T1–T15 einzeln
zählte; mehr Extras als damals (Nachschärfen, PSone, Linsenreflexe, Minikarte, Native) gleichen einen Teil
wieder aus. Die Kalenderzeit hängt vor allem an den Gerätetests: der Retroid zeigt, ob die Bilder stimmen; ob es
schneller wird, zeigen nur Mali-Geräte der Tester.

## Die Pflichtpunkte im Einzelnen

### Aufteilung (E1)
Siehe „Aufbau“. Reihenfolge innerhalb von E1, jeder Schritt mit Bildvergleich: (a) `DrawState` als reine Rechnung,
GL setzt sie wie bisher; (b) Programme, Uniform-Blöcke und Zeichenaufrufe hinter die Schnittstelle; (c) Texturen,
Sampler, Paletten; (d) Ziele, Kopien, Löschen, MSAA-Auflösen; (e) FXAA, Nachschärfen, PSone, Linsenreflexe,
Autofokus, Tiefenkarte, Zwischenbild-Ring, Zeitmessung; (f) Lesen von Bildern statt GL-Nummern (Tests,
Bildschirmfoto). GL-Aufrufe außerhalb des Renderers (Minikarte, Bildschirmfoto in `core_mode.cpp`) gehen über
dieselbe Schnittstelle.

### Shader nach SPIR-V (E2)
- Der Erzeuger schreibt den Körper (TEV, Licht, Nebel) für beide gleich; nur Kopf, Bindungen und zwei Zeilen
  ändern sich: `#version 450`, `layout(set, binding)` für die Uniform-Blöcke (XFM, XFP, XFL, PX, XFMM) und die
  acht Texturen, `uMtx`/`uWiden` als Push-Konstanten, `gl_VertexID` → `gl_VertexIndex`, Tiefe 0..1 statt −1..1.
  Die Bildzeilen liegen in Vulkan wie heute (Zeile 0 des EFB = Speicherzeile 0, keine Spiegelung), nur die
  Anzeige auf dem Bildschirm spiegelt nicht mehr.
- Übersetzt wird **zur Laufzeit** mit **glslang** (die TEV-Kombinationen lassen sich nicht alle vorab
  übersetzen), auf dem Gerät in einen SPIR-V-Zwischenspeicher. Die festen Shader (Kopie, Palette, Anzeige, FXAA …)
  genauso; das ist einfacher als ein zweiter Weg mit `glslc` beim Bauen.

### Pipeline-Cache (E3, E8)
- Pipeline-Schlüssel = Shader-Schlüssel + Zeichenzustand (Mischen, Tiefe, Seite, Farbmaske, Grundform,
  Eckabstand, Probenzahl, Zielart). Viewport, Scissor und Linienbreite sind dynamisch. Die Erweiterung
  „extended dynamic state“ (Tiefe und Seite im Befehl) nehme ich, wo es sie gibt, aber es muss ohne gehen.
- Zwei Zwischenspeicher: SPIR-V je Shader-Schlüssel (spart glslang) und Vulkans eigener `VkPipelineCache` als Datei
  (spart das Übersetzen im Treiber; Vulkan prüft selbst, ob er zum Treiber passt). Raumlisten wie bei GL
  (`room-XXX.vkeys`), höchstens 3 000 Pipelines, die am längsten unbenutzten gehen.
- Vorwärmen auf Hilfs-Threads (in Vulkan ohne zweiten Kontext erlaubt) – hilft schwachen Geräten wie dem moto e13
  (9 135 Programme, 21,7 s).
- Zähler „Pipelines je Raum“ zuerst messen; Erwartung 1,5- bis 3-mal so viele wie GL-Programme.

### EFB-Kopien und Render-Passes für Kachel-GPUs (E4, E8)
- EFB als Bild (Farbe RGBA8, Tiefe `D24_UNORM_S8_UINT` oder sonst `D32_SFLOAT`, als Textur lesbar), in der
  gewählten Auflösung; mit MSAA ein mehrfach abgetastetes EFB, das vor jeder Kopie aufgelöst wird (wie heute
  „eigene Puffer“, Entscheidung vom 19.10.2026).
- Je Kopie: EFB-Durchgang beenden (speichern), Barriere, Kopie-Durchgang, EFB-Durchgang mit „laden“ weiter.
  Heute 10–15 Durchgänge je Bild in den Belagerungsräumen. Für Kachel-GPUs: nach der Bildschirmkopie mit Löschen
  beginnt der nächste Durchgang mit „löschen“ statt „laden“; Tiefe wird nicht gespeichert, wenn sie danach niemand
  liest; das Löschen nach einer Kopie in derselben Region geht als `vkCmdClearAttachments` innerhalb des
  Durchgangs, nur bei getrennter Farb-/Alphamaske als gezeichnetes Rechteck.
- Paletten-Kopien und Hilfsdurchgänge als eigene kleine Durchgänge, wie heute.

### Synchronisation (E3, E4)
- Zwei Bilder im Flug, je eigener Befehlspuffer, Deskriptor-Vorrat, Abschnitt der Ringe und eine Lösch-Warteliste
  (Texturen und Kopien werden erst gelöscht, wenn der Grafikchip das Bild fertig hat). Ein Fence je Abgabe;
  daraus auch „wie weit hinten“ für die Dynamic-Auflösung.
- Texturen, die ein Bildstück neu bringt, werden in einem eigenen Befehlspuffer vor den Zeichnungen hochgeladen
  (Kopieren ist in einem Durchgang nicht erlaubt; eine neue Textur kann vorher niemand gelesen haben).
- Barrieren an genau drei Stellen: EFB → Kopie lesen, Kopie-Ziel → Textur lesen, Hochladen → lesen. Die
  Validation Layer mit Synchronisationsprüfung findet vergessene.
- Lesen (Tiefenkarte, Bildschirmfoto, Tests) per Kopie in einen lesbaren Puffer; die Tiefenkarte wie heute ein
  Bild später, ohne zu warten.

### Swapchain und Pre-Rotation (E6)
- `SurfaceView` statt `GLSurfaceView`; ihre Fläche geht per JNI an C++ (`ANativeWindow`), dort Fläche und
  Swapchain (FIFO, wartet auf den Bildwechsel; 2–3 Bilder).
- Pre-Rotation: die Swapchain bekommt die Drehung, die Android meldet; `Present` zeichnet das Bild gleich gedreht
  (sonst dreht Android jedes Bild noch einmal). `sensorLandscape` kann um 180° kippen: dann meldet Vulkan
  „suboptimal“, und die Swapchain wird neu angelegt.

### Pausieren und Fortsetzen der Android-Oberfläche (E6)
- Beim Pausieren und bei verlorener Fläche gehen nur Fläche und Swapchain; Gerät, Texturen, Kopien und Pipelines
  bleiben. Danach muss der Kern nichts neu schicken (heute bei GLES: neuer Kontext = neuer Renderer). Vorsicht
  bei Geräten, die den Grafikspeicher im Hintergrund freigeben: meldet Vulkan „Gerät verloren“, entsteht der
  Renderer neu wie heute bei GL.

### Dynamic-Auflösung (E4, E8)
- `RescaleHalves`, `HandOverCopies`, Benchmark und `AutoLevel` bleiben gemeinsamer Teil. Vulkan liefert dieselben
  Eingaben: Fences je Bildstück („behind“), Laufzeit und Prozessorzeit des Zeichen-Threads (Warten im Treiber),
  ausgelassene Zwischenbilder. Neue EFB-Größe: neue Bilder, die Kopien werden skaliert übernommen; die alten erst
  nach ihrem Fence gelöscht.

### Alle Bildformate und Filter (E5, E6)
- 4:3 (GC), 4:3, 16:9, Gestreckt, Native (mit Widen/View-Aspect): die Rechtecke rechnet `core_mode.cpp` für beide
  gleich; das Backend zeigt nur das Rechteck aus `Present` an. `SetShownRows`, `SetWiden`, Lebensanzeige und
  Minikarte gleich.
- FXAA, Nachschärfen, TV-Filter, PSone, Linsenreflexe, Tiefenschärfe (leicht/stark, halbe Auflösung), 60 FPS,
  Frame skip, Bündeln: alle als eigene Vulkan-Durchgänge mit denselben Shadern (Dialekt aus E2). Prüfung je Art
  GL gegen Vulkan in `gx_chunk_render`.

### Fehlerbericht und Rückfall (E6, E7)
- Protokoll und Bericht: „GX renderer: Vulkan 1.x.y, <Gerät>, driver <Version>, <Erweiterungen>“ bzw. „OpenGL ES …“;
  die Zeile „GX renderer ready …“ nennt die API.
- Rückfall: fehlt Vulkan, ist die Version zu alt, fehlt ein nötiges Format/Merkmal, schlägt das Anlegen oder die
  Selbstprüfung (Filmfarben) fehl → GLES, Zeile im Protokoll, Meldung an den Spieler („Vulkan could not start on
  this device, the game uses OpenGL ES.“). Absturz-Wächter: vor dem Start von Vulkan ein Merker in den
  Einstellungen, nach den ersten Sekunden ohne Absturz gelöscht; steht er beim nächsten Start noch, startet GLES
  und die App sagt es. Seit 0.99.6 (`VulkanGuard`): einmal je Prozess gesetzt, auch beim Beenden und im
  Hintergrund gelöscht, und ab Android 11 zählt nur ein echter Absturz, ANR oder Startfehler dieses Prozesses
  (`ApplicationExitInfo`). Sperrliste bekannter schlechter Treiber, wächst mit den Berichten.

### Mindest-Android/Vulkan-Version (Entscheidung, siehe unten)
- Die App läuft ab Android 7 (`minSdk 24` des Kernmodus). `libvulkan.so` nur per `dlopen` laden, nie fest
  verlinken; im Manifest Vulkan als „nicht nötig“.
- Vorschlag: Vulkan erst ab **Android 10 und Vulkan 1.1** anbieten (reifere Treiber; ab Android 10 müssen neue
  64-bit-Geräte Vulkan 1.1 können). Darunter steht im Menü nur OpenGL ES (der Vulkan-Knopf ausgegraut mit Hinweis).

### APK-Größe
- Neu kommt nur glslang dazu (ohne HLSL und ohne SPIR-V-Optimierer). **Gemessen in E0** (NDK r28c, `-O2`,
  gestrippt): 2,1 MB (arm64-v8a) und 1,6 MB (armeabi-v7a) ungepackt, gepackt zusammen etwa **1,3 MB** mehr APK auf
  heute 20,8 MB (≈ +6 %); installiert auf einem 64-bit-Gerät +2,1 MB. Der Vulkan-Teil selbst kommt später dazu
  (geschätzt einige hundert KB).

### Lizenzen von Fremdbibliotheken
- **glslang** (Khronos; nötig): kommt mit dem NDK (`sources/third_party/shaderc/third_party/glslang`, Fassung
  11.11.0), wird aus dem NDK gebaut, also kein Download und keine fremden Quellen im Repository. Lizenzen:
  überwiegend BSD-3-Clause, dazu BSD-2-Clause, MIT-ähnliche Khronos-Lizenz, Apache-2.0 für einzelne Dateien und
  GPL-3.0 **mit Bison-Ausnahme** für die erzeugte Grammatikdatei – die Ausnahme erlaubt die Weitergabe in einem
  Programm ohne GPL-Pflichten. Pflicht: die Lizenztexte mit der App weitergeben (Datei in den APK-Assets und im
  Repository, wie bei zstd `third_party/zstd/LICENSE`). Vermerk in der Status-Datei.
- **VMA** (Vulkan Memory Allocator, AMD, MIT): nicht nötig. Vorschlag: ein eigener kleiner Speicherverwalter
  (große Blöcke, darin Texturen und Puffer), ~300 Zeilen; Android-Treiber erlauben oft nur 4 096 Speicherblöcke,
  die Spieltexturen brauchen also auf jeden Fall Unterteilung.
- Validation Layer: nur am PC (WSL) und optional in Debug-APKs, nie in der verteilten APK.
- Dolphin (GPL) nur als Ideengeber, kein Code.

## Leistung: wo der Nutzen liegt

- Hauptnutzen auf **Mali-Geräten mit hoher Treiberlast je Zeichenbefehl** (Galaxy S10e: Zeichen-Thread 19,8 ms von
  21,1 ms je Bild, fast alles im Treiber; Redmi 15C ~35 µs je Zeichnung gegen ~3 µs auf dem Retroid). Erwartung
  dort (Schätzung aus anderen Emulatoren, nicht gemessen): der Zeichen-Thread etwa halb so lang.
- Wenig Nutzen, wo der Grafikchip selbst zu langsam ist (Galaxy S7, Mali-G71 MP2) oder der Spiel-Thread (Redmi
  15C schafft ~23 Bilder/s im Spiel selbst). **Dem Retroid bringt es nichts**; er bleibt das Gerät für richtige
  Bilder.
- Gemessen wird mit den vorhandenen Zeilen („GX render thread: … ms of processor time per chunk“, „GX render
  thread split per chunk“, GPU-Zeit), für beide Renderer gleich, damit Berichte vergleichbar sind.

## Fragen an den Projektinhaber (mit Empfehlung)

**Entschieden (08.11.2026, über die Hauptsitzung): alle vier Empfehlungen angenommen** – Wortlaut des
Projektinhabers: „Ja ich akzeptiere alle deine Empfehlungen“. Damit gilt:
1. Vulkan erst ab Android 10 mit Vulkan 1.1, darunter nur OpenGL ES (der Vulkan-Knopf im Menü ausgegraut, E7).
2. glslang aus dem NDK ja, mit den Lizenztexten in der App; VMA nein, ein eigener kleiner Speicherverwalter
   (`vk_memory`, E3).
3. Die APK darf etwa 2–3 MB größer werden (gemessen: +1,3 MB für glslang in E0; mit dem Vulkan-Renderer siehe
   „APK-Größe“). Wird es deutlich mehr, melde ich mich.
4. Die ersten Test-APKs dürfen unter Vulkan einzelne Extras noch ohne Wirkung haben; fertig ist es erst mit allen
   (seit E5 sind alle da).

1. **Mindestversion:** Vulkan erst ab Android 10 mit Vulkan 1.1 anbieten, darunter nur OpenGL ES?
   *Empfehlung: ja.* (Alternative: ab Android 7 / Vulkan 1.0 – mehr Geräte, aber alte Treiber mit vielen Fehlern.)
2. **Fremdbibliothek glslang** (nötig, BSD u. a., aus dem NDK, Lizenztexte in die App) – einverstanden?
   **VMA** nicht nehmen, sondern einen eigenen kleinen Speicherverwalter? *Empfehlung: glslang ja, VMA nein.*
3. **APK größer** für glslang – in Ordnung? *Empfehlung: ja.* Geschätzt waren 2–3 MB; in E0 gemessen: **≈ 1,3 MB**
   (beide ABIs gepackt, ≈ +6 %), installiert +2,1 MB auf 64-bit.
4. **Anfangs fehlende Funktionen:** die ersten Test-APKs (E6) zeigen das Spiel mit Vulkan noch ohne einen Teil der
   Extras (z. B. Linsenreflexe, PSone, Tiefenschärfe), die dann unter Vulkan ausgegraut sind; fertig wird es erst
   mit allen. *Empfehlung: ja, so testen wir früher am Gerät.*

(Bis zur Antwort lief der Bau an dem weiter, was davon nicht abhängt.)

## Gerätetests (für den Projektinhaber)

- Test-APKs baue ich selbst (Variante `coreDebug`, beide ABIs) nach `E:\Development\Android\builds\vulkan\`,
  `versionName` `0.99.2-vulkan1`, `-vulkan2` …, gleiche App-ID `com.ast.cubeevil4`, `versionCode` nicht unter 104;
  installierbar über die vorhandene Version, Discs, Spielstände und Einstellungen bleiben (Signatur prüfe ich vor dem
  ersten APK).
- Jede Etappe mit Gerätetest bekommt eine kurze Anleitung mit den PowerShell-Befehlen.

## Stand der Etappen

| Etappe | Stand |
|---|---|
| E0 | **fertig am PC (07.11.2026):** `vk_api` (Laden per `dlopen`), `vk_context` (Gerät, Prüfschicht), `vk_shader` (glslang 11.11.0 aus dem NDK, `cmake/glslang.cmake`), `tools/vk_probe.cpp`; Lizenztexte `third_party/glslang/LICENSE.txt`; `port/tools/vk_tools_linux.sh` (Werkzeuge ohne Root nach `~/vk-tools`). Gemessen: glslang gestrippt 2,1 MB (arm64) / 1,6 MB (armv7), gepackt zusammen ≈ 1,3 MB mehr APK. Für Android gebaut (beide ABIs), noch nicht in `libre4decomp.so` gelinkt |
| E1 | **fertig am PC (07.11.2026):** `GxCore` (gemeinsamer GX-Nachbau, `gx_core.h/.cpp`) mit rund 40 GPU-Aufrufen, `GlRenderer` (`gx_gl.cpp`) nur noch GPU-Arbeit; Bilder über `ReadXfb`/`ReadPresented`; nach jedem Schritt 4 371 Bilder bytegleich, Gradle-Bau beider ABIs fehlerfrei. Extras (Autofokus, FXAA, Schärfen, PSone, Reflexe, Tiefenkarte) noch ganze GL-Durchgänge, ihre Logik wird in E5 geteilt. Gerät: nicht geprüft |
| E2 | **fertig am PC (07.11.2026):** `gx_vk_shader` (GLSL ES → GLSL 450 für Vulkan); 27 522 Shader der App-Liste nach SPIR-V, 0 Fehler, `spirv-val` 0 ungültig; glslang 1,45 ms je Shader am PC. Feste Shader in E3/E5 |
| E3 | **großteils am PC (08.11.2026):** `gx_vk.cpp` zeichnet alle GX-Folgen (normal, gebündelt, 60 FPS, 1,5×, Native) auf lavapipe ohne Meldung der Prüfschicht; kein Bild mit mehr als 1 % Pixeln über 16 Stufen Abweichung zu GL; Rest = Treiber-Rundung (Zink zeigt dasselbe), Kantenregel, 1-Pixel-Linien. `ImageHeap`, Paletten-Kopien, Tiefenkarte gebaut. Offen: Pipeline-Cache auf Platte |
| E4 | **fertig am PC (08.11.2026):** gemeinsames Gerät aller Renderer eines Laufs, Übergabe der EFB-Kopien, Auflösungswechsel im Lauf, MSAA (Farbe per Resolve-Anhang, Tiefe per Shader beim Lesen); lavapipe ohne Meldung, kein Bild über der Grenze, Tiefenkarten wie GL. Gerät: nicht geprüft |
| E5 | **fertig am PC (08.11.2026):** FXAA, Nachschärfen, PSone-Filter, TV-Filter, Linsenreflexe, Autofokus (halbe und volle Auflösung, mit MSAA) und Minikarte unter Vulkan wie GL (lavapipe, kein Bild über der Grenze, 0 Meldungen). Offen: 1-Pixel-Linien (andere Linienregel), Gerät |
| E6 | **gebaut (08.11.2026, vor E4/E5 gezogen):** `VulkanSurface` (eigener Zeichen-Thread), Swapchain, `Present`, Rückfall auf GLES an Ort und Stelle mit Meldung, `renderer.txt` im Fehlerbericht; Vulkan nur per Test-Schalter `RE4_GX_API=vulkan` (ab Android 10, Vulkan 1.1), Rückfall erzwingbar mit `RE4_VK_FAIL=1`. Test-APK `0.99.2-vulkan1` (22,8 MB). Am PC: GL bytegleich, Vulkan-Folgen ohne Meldung. Gerät: noch nicht geprüft |
| E7 | **gebaut (08.11.2026):** Menüschalter „Renderer“ (Display → GRAPHICS, Neustart-Frage, Neustart direkt ins Spiel), 8 Texte in 11 Sprachen, Wahl gespeichert, Absturz-Wächter (`vulkanStarting`), Rückfall stellt die Wahl zurück, Test-Schalter `RE4_GX_API=vulkan`/`gles`. Sperrliste noch leer. Gerät: nicht geprüft |
| E8 | **gebaut (08.11.2026):** SPIR-V-Speicher (`vk-spirv.bin`, bis 160 MB), Pipeline-Cache des Treibers auf der Platte, Pipelines je Raum vorab (`room-XXX.vkpipes`, `Prewarm`), Messzeile „Vulkan per picture“; dazu die Korrektur des ersten Gerätetests (Swapchain jedes Bild neu: jetzt pre-rotation, „suboptimal“ nur angesehen). Am PC: zweiter Lauf ohne Übersetzen, Bilder identisch. Gerät: nicht geprüft |
| E9 | **am PC abgeschlossen (08.11.2026):** alle Etappen gebaut. Test-APK `0.99.2-vulkan5` (versionCode 105, 22,9 MB, beide ABIs), Kontroll-APK `0.99.2-vulkan0` (nur GLES, Stand E2, 21,3 MB): Vulkan kostet ≈ 1,6 MB. Testanleitung im Notizordner der Hauptsitzung. **Gerätetest vulkan5 (Retroid, 08.11.2026):** Läufe 1a–1c ohne Drehen – Swapchain einmal gebaut, Vulkan warm 60,0 Bilder/s (mindestens 58,9) mit ≈ 9 % weniger Prozessorzeit als GLES; der kalte Lauf ruckelt beim ersten Auftreten neuer Szenen (bekanntes Verhalten, ab dem zweiten Lauf flüssig). Benchmark unter Vulkan wie unter GLES (×3 bei 30 FPS, ×2 bei 60 FPS): auf dem Adreno 650 gleich schnell, der Grafikchip begrenzt. Läufe 2–4 mit 0.99.3-rc: Rückfall richtig, Vulkan 60/s; Menü-Lauf am Skript gescheitert (behoben), Home/Drehen ohne Spur im Protokoll (Protokollzeilen ergänzt). **Fertig erst**, wenn auch die gelaufen sind, der Nutzen auf Mali gemessen ist und der Projektinhaber den Merge nach `master` freigibt |
