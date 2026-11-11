# Plan: Benchmark beim ersten Start und Leistungsprofile (0.99.10, Aufgaben 2 und 3)

Stand 11.11.2026, Branch `09910`. **Gebaut am 11.11.2026** nach den Antworten des Projektinhabers („Ja, beide so“:
Update-Nutzer werden einmal gefragt, der erste Benchmark ist abbrechbar); was gebaut und geprüft ist, steht in
`port/docs/whole-program-status.md`, Version 0.99.10, Abschnitt 2./3. Ergänzt um den Wunsch des Projektinhabers (11.11.2026): „Der Subthread könnte bei leistungsfähigen Geräten
vorschlagen, bestimmte Effekte wie MSAA, FXAA, Anisotropic auch einzuschalten“ (Abschnitt „Effekte nach Spielraum“). Mock-ups: `mock-home.png` (Startseite mit der Profilzeile) und `mock-popups.png` (die drei Popups) im
Notizordner der Sitzungen (`side-session-notes\…\09910-plan\`).

## Was es schon gibt

Seit 0.98.4 hat die App einen Benchmark (Anzeige → Grafik → „Benchmark“ unter „Dynamisch“). Er springt an eine
feste Stelle: den **Steg am See (Raum r11b) im Regen**, mit Wasser, Nebel, Fackel und Boot. Grundlage ist dein
eigener Sofort-Spielstand dort, fest in die App eingebaut. Er braucht also **keinen Spielstand des Nutzers** und
startet auch aus dem Titelmenü. Die Gegner stehen still, Leon steht, die Kamera kreist alle 20 Sekunden einmal um
ihn. Gemessen wird in zwei Durchgängen (30 FPS, dann 60 FPS), jeweils von 4× abwärts in halben Stufen, 10 Sekunden
je Stufe, bis eine Stufe ruhig hält. Danach steht die Auflösung auf „Dynamisch“ bis zu diesen Stufen, und das Spiel
ist wieder dort, wo es vorher war.

## Antworten auf die Fragen aus der Aufgabe

**Welche Szene?** Weiter der Steg am See. Ein Film oder das Titelbild belasten ganz anders als das Spiel, und das
Spiel hat keine eingebaute Spiel-Demo. Der Steg zeigt echte Spiellast für den Grafikchip (Wasser, Regen, Nebel,
Spiegelungen) und ist immer gleich, auch ohne Spielstand. Schwäche: Weil die Gegner stillstehen, ist der Prozessor
weniger gefordert als in einem Kampf im Dorf. Deshalb nimmt die App für die Einstufung zusätzlich die Daten des
Geräts dazu (Kerne, Takt) und misst, ob der Prozessor oder der Grafikchip bremst (Zeit für das Berechnen eines
Bildes gegen die Wartezeit auf den Grafikchip).

**Wie lange?**
- Benchmark 1 (Einstufung): etwa **1,5–3 Minuten**. Laden der Stelle, 15 Sekunden Aufwärmrunde ohne Messung (beim
  allerersten Start werden die Grafikprogramme erst gebaut und würden sonst die Messung stören), 30-FPS-Suche,
  danach die 60-FPS-Suche. Neu: Die 60-FPS-Suche beginnt bei der 30-FPS-Stufe statt bei 4× (60 FPS kann nie mehr
  schaffen als 30 FPS), und schwache Grafikchips (z. B. Mali-G52/G57) beginnen bei 2,5× statt 4×. Beides spart
  bis zu einer Minute.
- Benchmark 2 (Prüfung des Vorschlags): etwa **1 Minute**: 20 Sekunden mit den bisherigen Einstellungen, 20 Sekunden
  mit dem Vorschlag, jeweils mit kurzer Eingewöhnung. Hält der Vorschlag nicht, nimmt die App einen Effekt eine
  Stufe zurück und misst noch einmal 20 Sekunden (höchstens zweimal, dann bis etwa 1:40).

**Was ist „Bildformat nativ“?** Unser Menüpunkt Anzeige → Bildformat → **„Nativ“**: Das Bild füllt den ganzen
Bildschirm im Seitenverhältnis des Geräts. Auf einem typischen 20:9-Handy zeigt es etwa ein Viertel mehr Bild als
16:9 und ist damit das anspruchsvollste Format. Gemessen wird deshalb in „Nativ“; das Ergebnis passt dann auch für
alle anderen Formate. Nach der Messung gilt wieder das Bildformat des Nutzers (bei neuen Nutzern 16:9).
Während der Messung sind außerdem aus: Kantenglättung (MSAA), FXAA, Texturfilter, Tiefenschärfe, Lichthöfe,
TV-Filter, PSX, Nachschärfen, CRT, Farbe. Frame-Skip ist aus, Leistungskerne auf „Standard“, der
Potato-Modus steht auf „alles an“ (normales Spiel).

**Wie hängen Benchmark und Profile zusammen?** Der Benchmark liefert drei Dinge: die Stufe für 30 FPS, die Stufe
für 60 FPS und wer bremst (Prozessor oder Grafikchip). Daraus und aus den Gerätedaten (Grafikchip, Kerne,
Arbeitsspeicher, Display-Hz) rechnet die App die drei Profile Niedrig, Mittel und Hoch für **dieses** Gerät aus und
wählt eines als „empfohlen“. „Hoch“ auf einem schwachen Handy ist also anders als „Hoch“ auf dem Retroid. Ohne
Benchmark (z. B. bei Nutzern mit Update, die ihn noch nicht gemacht haben) rechnet die App die Profile nur aus den
Gerätedaten.

**Wer bekommt welche Empfehlung?** (Werte aus den Fehlerberichten in `E:\Development\Android\reports\`)

| Gerät (Beispiele aus Berichten) | Gemessen 30 / 60 FPS | Empfehlung |
|---|---|---|
| POCO F6, Xiaomi mit Adreno 840 | 4× / 4× | Hoch |
| POCO X7 Pro (Mali-G720) | 3,5× / 2,5× | Hoch |
| Retroid Pocket Flip 2 (Adreno 650), Xiaomi 11 Lite NE | 2,5–3× / 1,5–2× | Hoch |
| Infinix X6882 (Mali-G57), Redmi 10 (Mali-G52); vermutlich auch Galaxy A24 | 2–2,5× / 1× | Mittel |
| moto g15 (Mali-G52) | 1× / 1× | Niedrig |
| Galaxy A13 (nur kleine Prozessorkerne) | Prozessor bremst | Niedrig, Frame-Skip Aggressiv |
| unter 3 GB Arbeitsspeicher | – | Niedrig |

Regel: **Hoch**, wenn 60 FPS mindestens 1,5× hält; **Mittel**, wenn 30 FPS mindestens 1,5× hält; sonst **Niedrig**.
Bei einem Display mit 90 Hz bleibt 60 FPS aus (läuft dort nicht gleichmäßig, siehe 0.99.9), 120 Hz ist in Ordnung.

**Welche Einstellungen setzt jedes Profil?**

| Einstellung | Niedrig | Mittel | Hoch |
|---|---|---|---|
| Auflösung | Dynamisch (bis zur gemessenen Stufe) | Dynamisch | Dynamisch |
| 60 FPS | aus | an, wenn 60 FPS ≥ 2× gemessen und das Display passt | an, wenn 60 FPS ≥ 1,5× gemessen und das Display passt |
| Frame-Skip | Aggressiv, wenn der Prozessor bremst, sonst Normal | Normal (aus, wenn 60 FPS an) | Normal (aus, wenn 60 FPS an) |
| Kantenglättung (MSAA), FXAA, Texturfilter | aus | nach Spielraum, eine Stufe sparsamer als Hoch | nach Spielraum (Tabelle unten) |
| Leistungskerne nutzen | an | an, wenn der Prozessor bremst | Standard |
| Wetter- und Spezialeffekte | aus | an | an |
| Partikeleffekte | Weniger | Normal | Normal |
| Schatten & Licht | aus | an | an |
| Sichtweite | Mittel (Kurz, wenn der Prozessor bremst) | Normal | Normal |

**Effekte nach Spielraum** (Wunsch des Projektinhabers: starke Geräte bekommen auch Effekte vorgeschlagen, nicht nur
schwache weniger). Spielraum ist die Stufe, die bei der Bildrate des Profils (30 oder 60 FPS) im Benchmark 1 noch
ruhig lief, also mit allen Effekten aus und im anspruchsvollsten Bildformat. Je höher, desto mehr Kraft ist für
Effekte übrig:

| Spielraum bei der Bildrate des Profils | Hoch | Mittel |
|---|---|---|
| ab 3× | MSAA 4×, Texturfilter 16× | MSAA 2×, Texturfilter 8× |
| 2× bis 2,5× | MSAA 2×, Texturfilter 16× | FXAA an, Texturfilter 4× |
| 1,5× | FXAA an, Texturfilter 8× | Texturfilter 4× |
| 1× | nichts (alles aus) | nichts |

MSAA glättet die Kanten am saubersten, kostet aber am meisten; FXAA ist der günstige Ersatz, wenn MSAA zu teuer wäre
(beides zusammen schlägt die App nicht vor, das Bild würde nur weicher). Der Texturfilter macht schräge Böden und
Wände schärfer und kostet wenig. Kann ein Grafikchip eine Stufe nicht (manche Mali-Chips melden keinen Texturfilter),
nimmt die App die höchste, die er kann.

**Benchmark 2 prüft die Effekte:** Der Vorschlag gilt nur, wenn mit ihm die Bildrate des Profils ruhig hält (dieselbe
Regel wie bei der Stufensuche: im Schnitt 29 bzw. 57 Bilder je Sekunde, höchstens eine schwache Sekunde) **und** die
dynamische Auflösung dabei nicht mehr als eine halbe Stufe unter den Spielraum fällt (sonst würden die Effekte nur
Schärfe gegen Glätte tauschen). Hält er nicht, nimmt die App die teuerste Stufe zurück (MSAA 4× → 2× → FXAA → aus,
dann Texturfilter 16× → 8× → 4×) und misst erneut. Popup ③ nennt, was geblieben ist und was zurückgenommen wurde.
Bei starken Geräten ist der Gewinn oft nicht mehr FPS, sondern ein schöneres Bild bei gleicher Bildrate; Popup ③
sagt dann beides (siehe `mock-popups-stark.png`).

Profile ändern **nicht**: Renderer (Vulkan/OpenGL, braucht einen Neustart), Bildformat, Tiefenschärfe, Lichthöfe
(Lens Flares), die Filter (TV, PSX, Nachschärfen, CRT, Farbe), Minikarte, FPS-Anzeige und die drei unbewiesenen Versuche
(Zeichenaufrufe bündeln, Vertex-Ringpuffer, Bandbreite sparen). Das ist Geschmack oder noch nicht erprobt.

**Was passiert beim allerersten Start?** (siehe `mock-popups.png` für ein mittleres, `mock-popups-stark.png` für ein
starkes Gerät)
1. Spiel startet, Logos, Titelmenü erscheint zum ersten Mal → Popup ① „Willkommen“ mit einem Knopf „Messung starten“.
2. Benchmark 1 läuft (oben eine Zeile mit Fortschritt). Danach zurück ins Titelmenü.
3. Popup ②: Gerätedaten, Messwerte, wer bremst, das empfohlene Profil mit den wichtigsten Änderungen.
   „Ignorieren“: nur die gemessenen Stufen für „Dynamisch“ speichern (so wie der Benchmark heute). „Annehmen“: das
   empfohlene Profil einstellen und Benchmark 2 starten.
4. Popup ③: Vorher/Nachher in FPS und wie oft das Bild ruhig lief, dazu die Effekte, die neu an sind. „Ignorieren“: zurück zu den Einstellungen von
   vorher (die gemessenen Stufen bleiben). „Akzeptieren“: das Profil bleibt aktiv.
5. Jetzt kann man spielen.
Die Popups bekommen das Aussehen des Ingame-Menüs (dunkelgrün, goldene Auswahl), Texte in den 11 Menüsprachen.

**Die Profilzeile** (siehe `mock-home.png`): Abschnitt „LEISTUNGSPROFIL“ zwischen Speicherslot und „Einstellungen &
Verwaltung“, vier Knöpfe nebeneinander: Benutzerdefiniert / Niedrig / Mittel / Hoch. Der aktive Knopf ist golden,
das empfohlene Profil trägt „★ empfohlen“. Darunter eine Zeile mit dem letzten Messergebnis und rechts ein Knopf
„Benchmark“.
- Klick auf Niedrig, Mittel oder Hoch: Ist gerade „Benutzerdefiniert“ aktiv, merkt sich die App dessen Werte; dann
  stellt sie das Profil ein (wirkt sofort bzw. beim Schließen des Menüs, wie heute).
- Klick auf „Benutzerdefiniert“: die gemerkten Werte kommen zurück.
- Ändert man danach von Hand eine der Einstellungen aus der Tabelle oben (auf irgendeiner Menüseite), springt der
  Knopf auf „Benutzerdefiniert“, und „Benutzerdefiniert“ sind ab dann genau diese neuen Werte. Andere Einstellungen
  (z. B. Lautstärke, Filter, Bildformat) ändern das Profil nicht.

**Nutzer mit Update (schon Einstellungen vorhanden):** Ihre Einstellungen werden „Benutzerdefiniert“ und bleiben
unverändert. Ob der Benchmark für sie einmal erzwungen wird, ist Frage 1 unten.

**Später neu messen:** Knopf „Benchmark“ unter der Profilzeile und der vorhandene Knopf unter Anzeige → Grafik →
„Dynamisch“. Beide starten jetzt denselben Ablauf (Benchmark 1, Popup ②, bei „Annehmen“ Benchmark 2, Popup ③),
wie heute aus dem Titelmenü oder beim freien Spielen; danach ist man wieder dort, wo man war.

## Wie geprüft wird

- PC: Tests für die Einstufung und die Profile (die Geräte aus der Tabelle als Testfälle), für das Merken von
  „Benutzerdefiniert“ und das Zurückspringen; die Menü-Bilder in allen 11 Sprachen; die Übersetzungsprüfung; der
  Benchmark-Ablauf (neue Startstufen, Aufwärmrunde, Vorher/Nachher, Zurücknehmen der Effekte) im Host-Test
  `TestBenchSteps`; die Effekte nach Spielraum für die Geräte aus den Berichten.
- Gerät: nur durch dich bzw. die Tester (Retroid; ideal ein schwächeres Handy). Ohne Gerätetest gilt es als nicht
  geprüft.

## Offene Fragen an den Projektinhaber

(Die frühere Frage „Sollen die Profile auch Kantenglättung und Texturfilter einstellen?“ ist mit der Ergänzung des
Projektinhabers geklärt: ja, dazu FXAA, nach Spielraum.)

1. **Update-Nutzer:** Soll der Benchmark für Nutzer, die schon spielen, beim ersten Start nach dem Update auch
   einmal erzwungen werden? Empfehlung: **nein**, stattdessen fragt das Ingame-Menü beim ersten Öffnen einmal „Jetzt
   messen / Später“; ihre Einstellungen bleiben als „Benutzerdefiniert“.
2. **Abbrechen beim ersten Start:** Darf man den ersten Benchmark mit B/Zurück abbrechen? Empfehlung: **ja**, dann
   spielt man mit den Standardwerten, und die Messung wird beim nächsten Start wieder angeboten, bis sie einmal
   durchgelaufen ist.
