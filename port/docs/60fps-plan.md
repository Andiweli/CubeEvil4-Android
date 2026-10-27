# 60 FPS ohne Flackern: Plan (Vorschlag, 27.10.2026, Cloud)

Wunsch des Projektinhabers: 60 FPS ohne flackernde Fackeln, Wasser und Texturen. Dieser Plan ist ein
**Vorschlag**; Schritt 1 ändert den Kern (ein Patch, eine neue GX-Nachricht) und wartet auf die Entscheidung des
Projektinhabers.

## Wie 60 FPS heute entsteht

Das Spiel rechnet und zeichnet 30 Bilder/s. Die App baut zwischen zwei Spielbildern ein **Zwischenbild**
(`GxRenderer::Interpolate`, `gx_gl.cpp`, Anteil 0,5): Sie zeichnet das nächste Bild ein zweites Mal und mischt
dabei die Lage (Matrizen, bei Figuren auch die Ecken) jeder Zeichnung, die sie im Bild davor **wiederfindet**.
Wiederfinden heißt raten: gleiche Signatur (Shader, Eckenzahl, Texturen), dann der kleinste Abstand, bei Teilchen
mit der abgelesenen Kamerabewegung (`MatchDraw`, `NoteCamera`, `recByLook`; Nachtests 0.91.0 in der Status-Datei).

## Warum es noch flackert

1. **Falsch oder gar nicht zugeordnet.** Flammen, Rauch und Funken sind viele gleich aussehende Vierecke; sie
   entstehen und vergehen, und eine Flamme wechselt jedes Spielbild ihre Textur (das nächste Einzelbild ihrer
   Animation). Eine Zeichnung ohne Gegenstück steht im Zwischenbild schon dort, wo sie im nächsten Bild steht (sie
   springt vor und bleibt dann stehen); ein falsches Gegenstück lässt zwei Teile die Plätze tauschen. Bericht vom
   26.10. (Xiaomi): 647 von 15 354 3D-Zeichnungen (4 %) ohne Gegenstück.
2. **Nur die Lage wird gemischt.** Texturmatrizen (fließendes Wasser, Lava, ziehender Nebel), die indirekten
   Matrizen (Verzerrung von Wasser und Hitzeflimmern), die TEV-Farben (Glühen) und die Lichtfarben (Fackelschein)
   kommen aus dem nächsten Bild: Sie springen 30-mal je Sekunde, während sich die Flächen 60-mal bewegen. Wasser
   wirkt dann zittrig. Texturmatrizen zu mischen wurde am 26.10. verworfen: ohne sichere Zuordnung zu riskant, und in
   r224 ohne messbare Wirkung.
3. **Ungleichmäßiger Takt.** Liegt beim Zeichnen schon das nächste Spielbild bereit, fällt das Zwischenbild weg
   (`gxBetweenDropped`). Xiaomi-Bericht: 101 von 476 (21 %) weggelassen. Dann wechseln Stücke mit 60 und mit 30
   Bildern/s; dieses Ruckeln sieht wie Flackern aus. Retroid: 13 von 8500, dort kaum.
4. **Animationen aus Einzelbildern** (`cEsp::m_Ptn_no`, die Flammen-Texturen) haben kein Zwischenbild. Sie wechseln
   wie im Original 30-mal je Sekunde. Das ist kein Fehler, nur nicht flüssiger als auf dem GameCube.

## Vorschlag: Namensmarken aus dem Spiel

Das Spiel zeichnet fast alles über seine Ordnungstabellen: `ExecOt` (`src/game/trans_ot.cpp`) ruft für jeden Eintrag
`func(data)` auf. `data` ist das Objekt (eine Figur `cModel`, ein Raumteil, ein Teilchen `cEsp` …). Jede einzelne
Flamme, jeder Rauchfetzen ist ein eigenes `cEsp` mit eigenem Eintrag (`esp.cpp` `AddOtDirect`, Zeichnen in
`EspCommonTrans`). Gibt der Kern diese Objekte an die App weiter, muss sie nicht mehr raten.

- **Kern (Patch, klein):** `ExecOt` meldet vor jedem Eintrag Objekt, Funktion und Tabelle an die GX-Schicht
  (`re4_gx_owner`). Die schreibt eine neue Nachricht `RE4_GX_OP_OWNER` in den Bildstrom. Ältere Renderer überspringen
  unbekannte Nachrichten an ihrer Länge (`re4_gx_frame.h`). Spiellogik und Bild des Kerns bleiben unverändert.
- **App, Zuordnung:** Eine Zeichnung ist „Objekt + Funktion + n-te Zeichnung des Objekts“. Gleiches Objekt in beiden
  Bildern bedeutet dieselbe Sache, auch wenn es anders aussieht (neue Flammen-Textur) oder sich weit bewegt hat.
  Ein Platz, den das Spiel in diesem Bild neu belegt hat (ein neues Teilchen im Speicher eines alten), wird erkannt:
  bei `cEsp` am Lebenszähler `m_Life_time` (muss um 1 steigen), sonst an einer anderen Signatur. Dann gilt die
  Zeichnung als neu und steht wie im nächsten Bild. Zeichnungen ohne Marke (Anzeigen, Text, Filme außerhalb von
  `ExecOt`) laufen weiter über den heutigen Weg.
- **App, mehr mischen:** Mit sicherer Zuordnung zusätzlich Texturmatrizen, indirekte Matrizen, TEV- und
  Konstantfarben, Lichtfarben und Nebel. Bei Texturmatrizen mit Umbruch-Erkennung: Springt die Verschiebung um mehr als
  eine halbe Texturbreite (die Textur läuft über ihren Rand und beginnt von vorn), wird nicht gemischt.
- **Einzelbild-Animationen:** bleiben 30-mal je Sekunde (wie das Original). Später vielleicht ein Überblenden der
  beiden Einzelbilder, nur für aufaddierte Teilchen; erst prüfen, ob das besser aussieht.

## Takt

Statt einzelne Zwischenbilder wegzulassen: Liegt die GPU zurück, geht zuerst die dynamische Auflösung eine Stufe
herunter (gibt es schon). Reicht das nicht, schaltet sich 60 FPS für einige Sekunden ganz ab (gleichmäßig 30 statt
abwechselnd 60 und 30) und meldet das im Fehlerprotokoll.

## Schritte

| Schritt | Inhalt | Prüfung |
|---|---|---|
| S0 Messen | Zähler im Protokoll: Zeichnungen ohne Gegenstück je Art (Teilchen, Figuren, Raum, 2D); R3-Serien vom Projektinhaber an einer Fackel, an Wasser (See, Dorfbrunnen) und an flackernden Texturen | Cloud: Zähler im Kerntest; Gerät: Serien |
| S1 Marken im Kern | Patch in `ExecOt`, `re4_gx_owner`, `RE4_GX_OP_OWNER` | Cloud: `runtime-test gx` (je Eintrag eine Marke vor seinen Zeichnungen), GameCube-Build trifft weiter die SHA1 |
| S2 Zuordnung | `MatchDraw` nach Marke, neu belegte Plätze, Rückfall auf den heutigen Weg | Cloud: `TestInterpolation*` neu (gleich aussehende Teilchen tauschen nicht, eines stirbt, ein Platz wird neu belegt), `gx_chunk_render` mit den Serien |
| S3 Mehr mischen | Texturmatrizen (mit Umbruch), indirekte Matrizen, Farben, Licht, Nebel | Cloud: Tests je Regel mit Gegenprobe; Serien: Zwischenbild mit Anteil 0 gleich dem Bild davor |
| S4 Takt | 60 FPS kurz aus statt einzelner Lücken | Cloud: `TestAutoLevel`; Gerät: Xiaomi-ähnlich langsam (kleine GPU) |
| S5 Gerät | PC-Prüfung beider Disc-Arten, dann Retroid | Projektinhaber |

## Verworfen

- **Spiellogik mit 60 Hz** (wie die PC-Fassung von 2007): Das Spiel rechnet in Bildern (Zähler, Zeitgeber,
  Physik, Tastenfolgen); nur die Bewegungen lesen `pG->Speed` (`motion.cpp`). Hunderte Stellen müssten von Hand
  angepasst werden. Das widerspricht dem Grundsatz „keine Spiellogik nachbauen“ und bringt neue Fehler (die PC-Fassung
  hatte mit 60 Hz bekannte Fehler).
- **Zwischenbild im Kern** (`Render()` zweimal je Spielbild, mit gemischten Objektzuständen): Der Kern müsste für
  jede Art Objekt wissen, wo dessen Lage steht, und die Zeichenfunktionen haben Nebenwirkungen (`EspCommonTrans`
  schreibt `esp->m_Mat`). Dazu doppelte Prozessorlast im Kern. Die Namensmarken bringen den größten Teil des Nutzens
  ohne das.
