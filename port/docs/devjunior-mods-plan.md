# Plan: Funktionen aus DevJuniors Mod-Fork für CubeEvil4 (Prüfung, 11.11.2026)

**Entscheidung des Projektinhabers (11.11.2026):** keine dieser Funktionen wird übernommen („nicht nützlich für unseren
Port“). Der Plan bleibt nur als Nachweis der Prüfung; die lokale Referenz `refs/scratch/devjunior` ist gelöscht.

Auftrag des Projektinhabers („Ja, prüfen lassen“): welche Funktionen aus DevJuniors Fork
`necroxmar01-prog/CubeEvil4-Android-remaked-mods`, Branch `mods/free-movement`, in CubeEvil4 kommen könnten und wie.
**Nur Prüfung und Plan, nichts gebaut.** Welche Funktionen in welche Version kommen, entscheidet der Projektinhaber;
0.99.10 wartet noch auf seinen Gerätetest.

## Was geprüft wurde

- Der Branch (83 Commits von DevJunior und „Claude“, 08.–10.11.2026) liegt zum Lesen in der lokalen Referenz
  `refs/scratch/devjunior` (nicht gepusht). Er baut auf unserer 0.99.6 (`d57121de`) auf; wir sind bei 0.99.10.
- Den Code jeder Funktion gelesen (Spielkern `port/whole/runtime/*.cpp`, Patches 0900–0908, App-Seite).
- **Probe auf unserem Stand:** Seine Kern-Patches 0900 (freie Bewegung, Parade), 0902 (Kleidung), 0904 (Nahkampf) und
  0905 (Logos überspringen) lassen sich ohne Änderung auf unseren heutigen Spielkern legen; 0901 (Mod-Ordner, für die
  Skins) nicht (ein Teil passt nicht mehr). App-Seite: ein Probe-Zusammenführen ergibt Text-Konflikte nur in
  `CoreInput.java` (Tastenbelegung), `CMakeLists.txt` und der Status-Datei; der Rest lässt sich textlich mischen, muss
  aber inhaltlich an unsere neuere Tastenbelegung (Kombinationen seit 0.99.9) angepasst werden.
- **Nicht geprüft:** sein Stand lief nicht am PC. Ihn zu bauen und zu starten wurde in dieser Sitzung nicht erlaubt
  (fremder Code); die Bewertung „wie gut es heute läuft“ stützt sich deshalb auf den Code, seine Commit-Texte und
  seine eigenen Tests (Laufzeittests im Fork), nicht auf eigene Läufe. Vor dem Einbau läuft jede Funktion am PC und
  auf dem Retroid.

## Übersicht (nach Nutzen zu Aufwand)

Aufwand: **klein** = eine überschaubare Änderung mit wenigen Dateien; **mittel** = Kern und App, neue Menüpunkte in
11 Sprachen, eigene PC-Prüfungen; **groß** = viele Stellen, Gerätetests zum Spielgefühl nötig.

| # | Funktion | Was sie macht | Wie gut sie heute ist (Code) | Aufwand bei uns | Berührt bei uns | Risiko | Empfehlung |
|---|---|---|---|---|---|---|---|
| 1 | Intro-Logos überspringen (Patch 0905) | Das Spiel startet sofort im Titelmenü, ohne Nintendo-, Capcom-, CRI- und Dolby-Logos. | Eine Zeile im Titel: derselbe Weg, den das Spiel beim zweiten Mal selbst nimmt. Ausgereift. | klein | Titelmenü (Sofortspeicher-Laden aus dem Titel, erster Benchmark wartet aufs Titelmenü) – passt, muss aber geprüft werden | gering | **so übernehmen**, aber standardmäßig **aus** (bei ihm an) |
| 2 | Schnell nachladen ohne Zielen | Eine eigene Taste (bei ihm L1, Tastatur R) lädt nach, ohne erst zu zielen; Leon bleibt dabei stehen. | Nutzt das eingebaute „Direkt-Nachladen“ des Prototyps (`PlReloadDirect`), das das Spiel schon kennt. Gut. | klein bis mittel | Tastenbelegung (neue Aktion; unsere Kombinationen seit 0.99.9), Touch (Knopf „RLOAD“) | gering | **überarbeitet übernehmen**: nur das Nachladen im Stand, als neue Aktion der Tastenbelegung; ohne „Feuertaste ohne Zielen lädt nach“ (stört unsere Kombinationen); Touch-Knopf nur im Editor, nicht im Standard-Layout |
| 3 | Schnellwahl übers Steuerkreuz | Steuerkreuz rüstet Waffen nach Art aus wie im Remake (oben Pistolen, rechts Schrotflinten/MP, unten Gewehre, links Granaten); gedrückt halten = Waffe weg. | Sauber gebaut (Waffenwechsel wie der Koffer, in eigener Aufgabe), mit Laufzeittest. | mittel | Steuerkreuz läuft dann nicht mehr (nur wenn eingeschaltet); Bogen und Schalldämpfer (0.99.10) passen in seine Listen | mittel | **überarbeitet übernehmen** (eigener Schalter statt seiner Bit-Belegung, Prüfung mit Stellvertretern, Ashley, Mercenaries) |
| 4 | Leons Kleidung wählen | Erstes Outfit (Jacke), zweites (ohne) oder wie die Geschichte es will. | Klein, nur Leon in der Geschichte, Spezialkostüm bleibt. | klein bis mittel | unser Schalter „Spezialkostüm“, Raumneustart der Figurenwahl | gering | **so übernehmen** (nur die Kleidung, ohne „Waffe auf dem Rücken“) |
| 5 | Messer-Parade | Kurz vor einem Treffer Messer drücken: der Schlag geht daneben, Leon schlägt zurück. | Einfach: der Treffer wird verworfen, keine eigene Parade-Bewegung; 8 Bilder Fenster, danach 24 Bilder Schutz. Funktioniert, sieht schlichter aus als im Remake. | klein bis mittel | Messer-Zielen (0.99.9), Unsichtbarkeits-Cheat | mittel (macht das Spiel leichter) | **überarbeitet übernehmen** als Option, standardmäßig aus |
| 6 | Genickbruch für Leon | Leon erledigt einen betäubten Ganado wie HUNK. | Nutzt HUNKs Bewegungen von unserer Disc (gleiches Skelett wie Leon); immer tödlich. | mittel | unsere Stellvertreter-Bewegungen (`loadStandInSet`) | mittel (Spielbalance) | **überarbeitet übernehmen**, wenn gewünscht (Option, aus) |
| 7 | Nachladen im Gehen, Bewegen beim Zielen | Leon läuft beim Nachladen weiter; mit gezogener Waffe kann er gehen und seitlich ausweichen. | Aufwendig (Bein-Bewegung über die Ziel-Haltung gelegt), viele Nachbesserungen in seinen Commits. | groß | rechter Stick/C-Stick (Messer- und Waffen-Zielen 0.99.9, Gyro, Touch-Zielen), alternative Steuerung, Kombinationen | hoch | **später**, eigener Meilenstein mit Gerätetests |
| 8 | Freie Bewegung | Stick bewegt Leon in Blickrichtung der Kamera, die Kamera dreht frei um ihn (wie im Remake). | Dreht weich statt schlagartig (bis ~24° je Bild im Stand, ~15° beim Gehen), Kamera folgt von selbst. Besser als unser entferntes „Modernes Bewegen“, aber noch sehr schnell und nur am Gerät beurteilbar. | groß | Kamera des Spiels (Patch in `cam_qfps.cpp`), C-Stick, Zielen, Gyro, Touch, „Weite Kamera“, Sichtfeld | hoch | **später**, nur nach Probe auf dem Retroid durch dich |
| 9 | Axthieb, Zufalls-Finisher | Leon schlägt knienden Gegnern mit der Ganado-Axtbewegung zu; zufällige Abschlussbewegungen. | Fremde Bewegung auf Leons Skelett mit ausgeblendeten Gelenken; ohne Skin hält Leon nur das Messer. | mittel | Nahkampf, Stellvertreter | hoch (sieht leicht falsch aus) | **weglassen** |
| 10 | Waffe auf dem Rücken | Die zuletzt gehaltene lange Waffe hängt sichtbar auf Leons Rücken. | Viele Nachbesserungen (Abstürze beim Neuplatzieren), eigene Ladevorgänge, Ereignis-Körper. | groß | Speicherbereich, Zwischensequenzen, Stellvertreter | hoch | **weglassen** (vorerst) |
| 11 | Mod-Ordner (Patch 0901, `dvd.cpp`) | Dateien in einem Ordner ersetzen die der Disc, Pakete ein-/ausschaltbar. | Gebaut vor allem für Skins aus der PC-Fassung; ändert Ladeweg, Speicherplätze und Zwischensequenzen. | groß | Disc-Import, Speicher, Zwischensequenzen | hoch, rechtlich heikel | **weglassen** |
| 12 | Eigene Nahkampf-Bewegungen aus Paketen, Skin-Werkzeuge (`uhd_skin_convert.py`, `skin_*.py`, `re4lfs.py`, `uhd_motion_convert.py`), Patches 0903, 0906–0908 | Figuren und Bewegungen aus der PC-Fassung umwandeln. | – | – | – | rechtlich | **nicht übernehmen** (fremde geschützte Daten, gegen die Linie der README) |
| 13 | GitHub-Workflow mit Test-APK, Debug-Schlüssel | Baut bei jedem Push eine APK. | – | – | – | – | **weglassen** (wir bauen lokal) |

## Vorschlag zur Reihenfolge

1. **Paket A (klein, sicher), z. B. 0.99.11:** Intro-Logos überspringen (aus), Schnell-Nachladen im Stand (neue Aktion),
   Leons Kleidung wählen. Alles als eigene Schalter im Menü (Steuerung bzw. Anzeige), 11 Sprachen, PC-Prüfungen mit
   allen drei Disc-Arten, dann Retroid.
2. **Paket B (mittel):** Steuerkreuz-Schnellwahl, Messer-Parade, Genickbruch für Leon – jeweils als Option, aus.
3. **Paket C (groß, eigener Meilenstein):** Bewegen beim Zielen und freie Bewegung, nur wenn dir die Probe auf dem
   Retroid gefällt (am besten zuerst DevJuniors Test-APK selbst anspielen).

Für alle Pakete gilt: seine Einstellungs-Bits (er hängt alles an das Wort der Zieleinstellungen: Bits 4–7, 20–23
und 30 sowie ein zweites Wort mit Bit 31) übernehmen wir nicht, sondern geben jeder Funktion einen eigenen, sauberen Weg; seine Patch-Nummern 0900–0905
werden zu unseren nächsten freien (ab 0229).

## Lizenz und Nennung

Sein Fork trägt unsere CC0-`LICENSE`; rechtlich dürften wir den Code nehmen. Trotzdem: vorher fragen und ihn im
Changelog nennen („based on DevJunior's mods fork“). Kontakt ist dein Schritt; ich poste nichts auf GitHub.

Entwurf (z. B. als Issue in seinem Fork):

```
Title: May I bring some of your mods into CubeEvil4?

Hi DevJunior,

thanks for your work on the mods fork - it's great to see what you built on top of CubeEvil4.

I'd like to bring a few of your features into the official CubeEvil4, adapted to the current code:
skip intro logos, quick reload without aiming, D-pad quick slots, Leon's outfit choice, and maybe
knife parry and the neck break for Leon. Free movement / move while aiming might follow later.

Your fork carries our CC0 license, but I'd still rather ask first: is that okay with you? You will be
credited in the changelog and the release notes ("based on DevJunior's mods fork"). If you'd like a
different name or a link, just tell me.

I won't take the skin tools or anything built on the PC release's assets, as CubeEvil4 must stay free
of copyrighted game data.

Thanks again!
```
