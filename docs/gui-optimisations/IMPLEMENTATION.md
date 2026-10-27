# GUI und Instant-Spielstand: Umsetzung

Branch `gui-optimisations`, Grundlage `9890e2e` (die Erstanalyse auf Basis von
`beta-optimisations` / `19bd01c`). Auftrag und abgestimmte Änderungen vom 24.10.2026.

## Menü

`CoreMenu` besitzt einen gemeinsamen Rahmen und fünf Inhaltsseiten. Die bisherige
Dialoginstanz bleibt bei Seitenwechseln geöffnet und der Kern pausiert. Zurück/B
führt zur Hauptseite, dort zurück zum Spiel. START/SELECT und „Zurück zum Spiel“
schließen das Menü direkt. Die vier Untermenüs heißen im deutschen Entwurf
Anzeige, Steuerung, Performance und Management.

- Haupttitel **CubeEvil4 Ingame-Menü**, tatsächliche App-Version klein daneben in
  Klammern. Der alte Pausen-/SELECT-Hinweis entfällt.
- **Speicherslot**, darunter Kapitel/Raum/Spielzeit und das letzte Speicherdatum
  links untereinander; Speichern/Laden nebeneinander in der rechten Hälfte. Bei wenig Platz
  stehen die Aktionen unter den Informationen.
- Dunkles Anthrazit/Oliv und gedecktes Gold wie im abgestimmten Design; leicht
  gerundete Buttons und ein eigener heller Fokusrahmen für den Controller.
- Einfachauswahl für Auflösung, MSAA, Filter, Bildformat, Touch-Modus und CPU-Modus.
  Unabhängige Funktionen sind kombinierbare AN/AUS-Buttons. Keine Radio- oder
  Checkbox-Widgets mehr. Der Gyro-Regler bleibt erhalten.
- Vorhandene Präferenzschlüssel und Host-Rückrufe bleiben erhalten, ebenso
  Import/Export, Berichte, R3-Aufnahme und der Projektlink.
- Bei wenig Bildschirmbreite stehen die Einstellungsbereiche untereinander;
  der Inhalt kann scrollen. Kopf und Zurück bleiben außerhalb des Scrollbereichs.
- Alle zehn vorhandenen Menüsprachen wurden ergänzt: Englisch, Deutsch,
  Spanisch, Französisch, Italienisch, Portugiesisch, Russisch, Arabisch,
  vereinfachtes Chinesisch und Hindi. Arabische Schreibrichtung und Androids
  Platzhalter-/Zeichenregeln sind berücksichtigt.

## Layout-Anpassungen vom 24.10.2026

Das gesamte Menüfenster hat jetzt 90 % Deckkraft (10 % Transparenz; ersetzt die
erste Nachkorrektur mit 15 %).
Tiefenschärfe beginnt in der Optionsspalte unter „4:3 (GC)“. Unter Performance
stehen 60 FPS und die FPS-Anzeige in derselben eingerückten Optionsspalte;
„Bildausgabe“ ist links als Zeilenbeschriftung angeordnet. Auf schmalen
Bildschirmen bleiben die Beschriftungen oberhalb ihrer Optionen.
Die Gyro-Empfindlichkeit ist mittig überschrieben, die vier Untermenütitel
haben 16 dp zusätzlichen Abstand zur Zurück-Schaltfläche. Die deutschen
Menünamen lauten „Anzeige“ und „Steuerung“. Die übrigen neun Sprachen nutzen
weiter ihre bereits vorhandenen Übersetzungen; die Auswahl erfolgt weiterhin
nach der Geräte-/App-Sprache.

Die Hauptseiten-Buttons haben 19-sp-Titel in Fettschrift und normale
15-sp-Unterzeilen. Allgemeine Buttontexte stehen in 16 sp, Infotexte überwiegend
in 15 sp statt 12–13 sp. Keine verkleinernde automatische Schriftanpassung oder
Auslassungszeichen: längere Texte umbrechen und vergrößern ihre Zeile.

`MenuLayout` misst die tatsächlich verfügbare Breite der jeweiligen Reihe.
Auswahlbuttons nutzen so viele gleich breite Spalten, wie ihre Beschriftungen
zulassen. Einstellungsbeschriftung/Optionen, Gyro und Speicherslot werden bei
wenig Platz oder großer Systemschrift untereinander angeordnet. Im schmalen
Kopf stehen Titel und Aktionen auf getrennten Zeilen. Ein Größenwechsel des
App-Fensters passt auch das geöffnete Menü an; Buttons und laufende
Speicheranfragen bleiben dieselben Objekte. Der Navigationshinweis scrollt am
Ende mit, damit er auf niedrigen Displays keinen Platz für Optionen blockiert.
RTL wird unterstützt; die gewünschte physische Links-/Rechtsanordnung des
Speicherslots bleibt erhalten.

Die fünf breiten Screendesigns, PDF und Vorschau wurden nachgezogen. SVGs sind
feste Designansichten; die native App führt den beschriebenen Umbruch aus.

Die Instant-Aktionen teilen sich die rechte Hälfte der Kachel. Der rechte
Abstand entspricht mit 12 dp dem linken Abstand der Überschrift „Speicherslot“.
Beide Buttons füllen die verfügbare Kachelhöhe mit 12 dp Rand oben/unten;
die Höhe folgt den Metadaten und deren Umbruch. Links bleiben
Titel, Kapitel/Raum/Spielzeit und Speicherzeit vollständig erhalten. Die
Statuszeile „Bereit.“ ist ausgeblendet und beansprucht keinen Platz. Fortschritt,
Speicherbestätigung und Fehlermeldungen blenden die Zeile bei Bedarf wieder ein.
Schmale Fenster und größere Systemschrift behalten den vorhandenen Umbruch.
Die Nachprüfung umfasst auch Abstände, volle Buttonhöhe und das Wiederanzeigen
von Fortschritt/Fehlern: drei View-Tests mit 750 Layouts bestanden, zehn Sprachen
geprüft, SDK-Debug-APK und Android-Lint (0 Fehler) erfolgreich. Keine Kernänderung.
Nachtrag vom 25.10.2026: Auch der vollständige Gradle-Lauf mit
`assembleCoreDebug testCoreDebugUnitTest lintCoreDebug` ist erfolgreich; dabei
sind die erweiterten Rand-/Höhenprüfungen enthalten.

Unter Performance / Bildausgabe lautet die Reihenfolge: FPS-Anzeige,
Frame-Skip, 60 FPS (experimentell). Frame-Skip ist ein deaktivierter Platzhalter
ohne gespeicherte Einstellung oder Rückruf in den Renderer; alle zehn Sprachen
enthalten die Beschriftung.

## Instant-Speicher

Ein dauerhafter Platz unter `<game>/instant/slot-0.ce4save`. Er ist eine eigene
Datei außerhalb des emulierten Kartenabbilds und belegt keinen der zwanzig
Schreibmaschinenplätze. Kartenliste und .gci-Import/Export arbeiten weiterhin
ausschließlich auf der Speicherkarte.

`instant_game.cpp` ruft die echten Originalroutinen `GameSave.save`, `RoomData`,
`ItemMgr`, Händler und Untermenü auf. Die Nutzdaten besitzen dieselben Bereiche
wie ein Schreibmaschinen-Spielstand, einschließlich aktueller Spielerposition
und Blickrichtung, globalem Fortschritt, Gesundheit, Geld, Inventar mit
Waffenwerten und Munition, Raumdaten, Untermenü, Händler und Systemoptionen.
Der Speicherzähler wird bei erfolgreichem Speichern erhöht. Der bisherige
Continue-Puffer wird beim Instant-Speichern nicht ersetzt. Zeit im App-Menü
wird nicht als Spielzeit mitgerechnet.

Das Paket enthält einen 32-Byte-Kopf und die beiden festen Original-Payloads
(64 KiB Spielstand, 8 KiB Systemdaten). Die bestehende GCI-Feldkonvertierung
schreibt feste GameCube-Byte-Reihenfolge mit den Originalprüfsummen; hinzu kommen
Formatversion, Länge, Speicherzeit und Prüfsummen des eigenen Pakets. Der
ABI-abhängige `SAVE_DATA_HEAD` mit seinen Zeigern bleibt im Arbeitsspeicher.

Eine neue Datei wird zunächst temporär vollständig geschrieben und gesichert,
dann atomar an die Stelle des Slots gesetzt. Ein fehlgeschlagenes Erfassen oder
Schreiben verändert den vorherigen gültigen Slot nicht. Beschädigte oder
inkompatible Dateien können nicht geladen, aber durch neues Speichern ersetzt
werden. Vor Überschreiben und Laden fragt das Menü nach.

## Ablauf und sichere Zeitpunkte

Java führt die Anfrage auf einem Arbeits-Thread aus. `re4_core_instant` stellt
sie in die vorhandene Pausenschleife des Kerns; die Erfassung läuft am
angehaltenen Frame-Rand. Die API verweigert Anfragen ohne bestätigte Pause
oder bei gemounteter Karte. Während des Dateivorgangs bleibt das Spiel stehen.
Dies funktioniert über die direkte JNI-Brücke und über den separaten Kernprozess
(`kInstant`, typisierte Statusdaten ohne Zeiger).

Der tatsächliche Ladeablauf kann Waffen-/Raumdaten lesen und auf Spielaufgaben
warten. Deshalb validiert die Pause-Anfrage zunächst nur die Datei und stellt
ihre Nutzdaten bereit. Nach dem Schließen des Menüs übernimmt **Patch 0111**
zu Beginn der originalen `gameMainLoop` auf der Spielaufgabe. Er überspringt
das nächste Bild des alten Raums und verwendet den bestehenden Ladeweg aus
dem Original-Pausemenü (`GameContinue(1)` und `GameLoad`). `src/` und `include/`
bleiben im Repository unverändert.

Speichern ist bei einem lebenden, steuerbaren Spieler in einer stabilen
Spielsituation verfügbar. Laden ist zusätzlich im ruhenden Startmenü erlaubt,
ohne zuvor ein normales Spiel oder einen Schreibmaschinen-Spielstand zu öffnen.
`re4_instant_game_can_load` prüft diese Freigabe unabhängig von der Speicherfreigabe.
**Patch 0112** übernimmt den Auftrag auf der Titelaufgabe: Nutzdaten installieren,
`GameLoad`, dann original `titleExit` → `GameTask` → `gameInit`. Der Titel räumt
seine Grafik und Daten selbst auf; der Raum wird regulär initialisiert.
Während Logos, Demo-Filmen, Karten-Zugriff und anderen Übergängen bleibt Laden
gesperrt. Eine begonnene Titel-Ladung sperrt weitere Anfragen, bevor sie auf
Spielaufgaben warten kann. Es ist ein Original-Spielstand; kurzfristige Animationen oder
fliegende Projektile sind kein vollständiger RAM-Schnappschuss.

## Nachprüfung von Lesbarkeit, Umbruch und Startmenü-Laden

- Drei Android-View-Tests mit Robolectric erfolgreich: 750 Messungen der fünf
  Seiten in zehn Sprachen, 320/480/700/960 dp Breite und 1/1,5/2-facher Schrift.
  Auch Verkleinern/Vergrößern, RTL, vollständige Texthöhen, Überlappungen und
  horizontale Grenzen geprüft. Weitere Tests prüfen Schriftgewicht, getrennte
  Speichern-/Ladenfreigabe im UI sowie Reihenfolge und Inaktivität von Frame-Skip.
- Übersetzungsprüfung: 117 Texte und ein Plural je Sprache, zehn Sprachen, bestanden.
- Linux x86_64 und i686/QEMU: Kernprüfung einschließlich echter Original-
  Spielstanderfassung und kaltem Titel ohne Spieler bestanden; Dateitest mit
  nur erlaubtem Laden, gesperrtem Speichern, Neustart und Fehlerfällen bestanden.
- Android-Kern: ARM32 und ARM64, je 114 Module, fehlerfrei gebaut.
- Vollständige Java-Kompilierung, Ressourcen-Linken, DEX und Debug-APK über die
  Android-SDK-Werkzeuge erfolgreich; Ausrichtung und Signatur verifiziert.
  Android-Lint direkt mit dem bestehenden coreDebug-Projektmodell: 0 Fehler.
  Der abschließende Gradle-Lauf wurde hier durch die gesperrte lokale
  Netzwerkverbindung zwischen Build-Prozessen blockiert. Die View-Tests liefen
  deshalb direkt über JUnit mit denselben Android-Ressourcen und Abhängigkeiten.
- Der komplette Ladeablauf vom Startmenü mit echten Disc-Daten sowie Darstellung
  und Controllerbedienung auf dem Retroid bleiben als Gerätetest offen.

## Prüfungen

- `check_translations.py`: alle zehn Menüsprachen, gleiche Schlüssel und
  Platzhalter, RTL-Regeln bestanden.
- Android-Kernbuild mit NDK r28c: ARM32 und ARM64, jeweils 114 Module,
  keine Baufehler und unveränderte erwartete Stubs.
- Vollständige APK mit `assembleCoreDebug` und Android-Prüfung
  `lintCoreDebug`: erfolgreich. Ein bereits bestehender API-32-Prüffehler
  im durch Versionsprüfung geschützten Spatializer-Helfer wurde durch
  `@TargetApi(32)` gekennzeichnet; das Audioverhalten wurde nicht verändert.
- Linux x86_64: Build, Modul-/Trampolinprüfungen, Laufzeit- und Kernprüfung
  bestanden. Die taktempfindlichen Retrace-Prüfungen waren unter paralleler
  Buildlast zunächst fehlgeschlagen und im anschließenden Lauf bestanden.
- `instant_game_probe`: echte Original-Erfassung auf synthetischen
  Spielobjekten geprüft, einschließlich Position, Winkel, HP/Geld,
  Waffenwerten/Munition, Raum-, Händler-, Untermenü- und Systemdaten,
  unverändertem Continue-Puffer und Zustands-Sperren.
- `instant_store_test`: alle Payloads bytegenau hin und zurück, neuer Prozess,
  feste Byte-Reihenfolge, leere/defekte/abgeschnittene Datei, Zustands-Sperre,
  simulierter Erfassungs- und Schreibfehler. Die Speicherkarte wird dabei
  nicht einmal aufgerufen.
- Linux i686 unter QEMU: Kernprüfung inklusive Original-Erfassung bestanden,
  Dateitests mit echtem Prozess-Neustart bestanden. Der separate 32-Bit-Kernprozess
  samt Instant-Anfragen, Kartenoperationen, Grafik, Ton und Eingaben: 0 Fehler.
  Der zusätzliche Prozesstest fand eine Bindung von STL-Pufferfunktionen an den
  Spiel-Heap; ein fester Dateipuffer behebt dies und vermeidet diese Allokationen.
- Die Pausenprüfung schickt Instant-Anfragen an den echten Pausenpunkt und
  prüft, dass dabei kein Spielbild weiterläuft.

**Noch lokal zu prüfen:** echte Disc-Daten und Retroid. Insbesondere Laden
nach einem Raum-/Waffenwechsel, Position und Blickrichtung im Spiel,
Ereignisfortschritt, Menübedienung mit Touch/Controller, große Schrift und 4:3.
Ein kompletter Kampagnen-Spieltest wurde in dieser Umgebung nicht durchgeführt.

## Lokal abgleichen und bauen

PowerShell im bestehenden Windows-Repository:

```powershell
cd E:\Development\Android\RE4-Fixed
git status
git fetch origin gui-optimisations
git switch gui-optimisations
git pull --no-rebase origin gui-optimisations
wsl --exec bash -lc "if [ ! -d ~/re4-build/.git ]; then git -c safe.directory='*' clone -q /mnt/e/Development/Android/RE4-Fixed ~/re4-build; fi && cd ~/re4-build && git -c safe.directory='*' fetch -q /mnt/e/Development/Android/RE4-Fixed HEAD && git checkout -q --detach FETCH_HEAD && git log -1 --format='Stand: %h %s' && port/tools/whole_build_android.sh --into /mnt/e/Development/Android/RE4-Fixed/port/android-decomp/app --abi all"
```

Anschließend in Android Studio Variante `coreDebug` bauen und auf dem Retroid
starten. Die vorhandenen Disc-Importe und Kartenstände bleiben bestehen.

Geräteprüfung: mit vorhandenem Schreibmaschinen-Spielstand starten, einige
Schritte gehen und einen Instant-Spielstand anlegen. HP, Geld, Inventar und
Position merken. Änderungen vornehmen und den Instant-Spielstand laden,
dann App neu starten und erneut laden. Auch nach einem Raum- oder Waffenwechsel
prüfen. An der Schreibmaschine müssen dieselben Slots wie vorher sichtbar sein.

## Startprompt für den Gerätetest

```text
Prüfe gui-optimisations in Andiweli/CubeEvil4-Android.
Empfohlene Intelligenzstufe: hoch. Lokal auf dem Windows-PC mit Retroid und
Disc-Daten, Gerätetests über die bestehende ADB-Koordination.
Lies docs/gui-optimisations/IMPLEMENTATION.md. Baue beide Spielkerne und die
coreDebug-App. Prüfe die fünf Menüseiten, SELECT kurz/lang, Zurück, Touch,
Controller, Sprachwechsel, große Schrift sowie Instant-Speichern/Laden mit
aktuellem Standort, Inventar, HP/Geld und Ereignisfortschritt. Prüfe Laden nach
App-Neustart sowie Raum-/Waffenwechsel. Die Schreibmaschinenliste und ihre
Spielstände dürfen nicht verändert werden. Behebe beobachtete Fehler auf
gui-optimisations; andere Branches unverändert lassen.
```
