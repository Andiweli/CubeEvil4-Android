# Frame Skip: Machbarkeitsprüfung

Stand: 24.10.2026, Branch `gui-optimisations`, geprüfter App-Stand `54e8704`.
**Ergebnis: grundsätzlich möglich, Funktion noch nicht implementiert.** Im
Menü steht inzwischen ein deaktivierter Platzhalter nach der FPS-Anzeige und
vor den experimentellen 60 FPS; er verändert keine Einstellung. Ein belastbarer
Leistungsgewinn muss auf einem schwachen Gerät gemessen werden.

## Erwartetes Verhalten

Ein Button **„Frame Skip 1“** unter Performance / Bildausgabe könnte abwechselnd
ein Spielbild zeichnen und eines auslassen. Standard wäre AUS. Bei den üblichen
30 Spielbildern pro Sekunde bleiben dann etwa 15 unterschiedliche sichtbare
Bilder. Das Spiel soll weiterhin mit seinem ursprünglichen Takt laufen:
Spiellogik, Eingaben und Ton werden nicht übersprungen oder beschleunigt.

Das kann durch langsames Zeichnen verursachte Verlangsamung verringern. Es ist
kein garantierter Geschwindigkeitsgewinn und keine Verdopplung des Spieltempos.
Bewegungen werden ruckeliger, sichtbare Reaktionen kommen seltener. Wenn bereits
die Spiellogik oder die Aufbereitung der Grafikbefehle den Prozessor auslastet,
hilft das Auslassen der eigentlichen Zeichnung wenig oder gar nicht.

## Befund im aktuellen Code

- [`main.cpp`](../../src/game/main.cpp#L386) setzt zunächst zwei Bildschirmtakte
  pro Spielschritt (30 Hz bei rund 60 Hz Bildschirmsignal).
- [`CoreActivity.java`](../../port/android-decomp/app/src/main/java/com/ast/re4decomp/CoreActivity.java#L1122)
  gibt jeden Bildschirmtakt an den Kern weiter. Das vorhandene `FramePacing`
  begrenzt unnötige Wiederholungen auf schnellen Displays; es ist kein Frame Skip.
- [`drawGx`](../../port/android-decomp/app/src/main/cpp/core_mode.cpp#L1164)
  verarbeitet derzeit jedes fertige Spielbild. Ist der Renderer zu langsam,
  wartet der Kern auf freie Plätze in der Grafikwarteschlange.
- [`Re4GxChunk`](../../port/whole/runtime/re4_gx_frame.h#L14) ist ein fortlaufender
  Befehlsstrom: Register, Texturen und Paletten bauen auf dem vorherigen Bild auf.
  Einen kompletten Eintrag ungeprüft zu verwerfen kann spätere Bilder beschädigen.
- [`GxRenderer::Run`](../../port/android-decomp/app/src/main/cpp/gx_gl.cpp#L3181)
  verarbeitet Grafikzustand, Ressourcen und Zeichenbefehle gemeinsam. Auch
  Kopien des Bildpuffers in Texturen und Löschbefehle haben Folgen für spätere
  Zeichenoperationen. Nur `Present` auszulassen spart die teure Szenenberechnung
  nicht; seltener `requestRender` aufzurufen kann die Warteschlange stärker füllen.

## Empfehlung für eine Umsetzung

1. Den ursprünglichen Spiel-/Bildschirmtakt beibehalten. Auch ausgelassene Bilder
   müssen ihre notwendigen Zustands- und Ressourcenänderungen weitergeben.
2. Einen eigenen Renderpfad für ausgelassene Bilder entwickeln. Nur entbehrliche
   Zeichenarbeit auslassen; benötigte Bildpufferkopien, deren vorherige Zeichnung
   und bildübergreifende Abhängigkeiten erhalten. Bei unklaren Abhängigkeiten das
   betreffende Bild vollständig verarbeiten. Dieser Teil bestimmt den Aufwand
   und den tatsächlich möglichen Leistungsgewinn.
3. Den übersprungenen Eintrag zeitnah verarbeiten und freigeben, statt auf den
   nächsten sichtbaren Bildschirmaufbau zu warten. Das letzte fertige Bild bleibt
   währenddessen sichtbar.
4. Die experimentellen 60 FPS bei aktivem Frame Skip aussetzen: Die zusätzlichen
   Zwischenbilder würden dem Sparziel entgegenwirken. Als erste Version Frame
   Skip auf steuerbares Gameplay begrenzen; Menüs, Filme und Übergänge ausnehmen.
5. Auswahl und Hinweis in alle zehn vorhandenen Menüsprachen aufnehmen. Die
   FPS-Anzeige soll tatsächlich gezeichnete Bilder zählen; die Diagnose soll
   Spieltakt, übersprungene Bilder und Wartezeit getrennt erkennen lassen.

Vor einer Freigabe nötig: Prüfungen mit aufeinander aufbauenden Grafikbefehlen,
Texturen/Paletten, Bildpufferkopien und Rücksetzen des Renderers; Wechsel von
Option, Pause, Raum und Instant-Spielstand; anschließend Vergleichsläufe auf
einem schwachen Android-Gerät mit identischer Szene und Einstellung. Die
vorhandene Diagnose `GX core: ... waiting for the renderer` hilft, Grafikwartezeit
von anderer Prozessorarbeit zu unterscheiden. Ton und Spielzeit dabei mitprüfen.

Diese Prüfung ist eine Quellcodeanalyse. Der nachträglich gewünschte Platzhalter
ist in allen zehn Menüsprachen vorhanden; eine Frame-Skip-Funktion wurde nicht
eingebaut und keine Leistungswerte auf einem Gerät gemessen.
