# Gegner-Randomizer: Prüfung und Plan (0.99.12, 12.11.2026)

**Stand: Stufe 1 gebaut (0.99.12, Entscheidung des Projektinhabers 12.11.2026: „Ok den Randomizer Schalter wie von
dir vorgeschlagen ‚Stufe 1' und als Button neben dem Item Randomizer“).** Cheat „Random enemies“ / „Zufällige Gegner“
neben „Random items“, eigener Seed (`core_game` `enemySeed`), Patch 0231 (`em_set.cpp`), `runtime/enemy_random.cpp`,
Schutzliste `port/tools/enemy_random_table.py` → `runtime/enemy_random_rooms.inc`; Einzelheiten und Prüfung in
`whole-program-status.md` (0.99.12, Abschnitt 7). Abweichungen vom Plan: Räume ohne eigenes Skript bleiben ebenfalls
unangetastet; der weggelassene Gegner wird unter denen gewählt, die gerade erscheinen; eine Kopie darf auch an die
Stelle eines Gegners, der gerade nicht da ist (tot oder später im Kapitel).

Auftrag (Wortlaut des Projektinhabers): „Prüfe ob auch ein ‚Randomizer' wie bei den Objekten auch für Gegner möglich
wäre: bei Aktivierung sollen in jedem Raum die Gegner zufällig gespawnt werden. Mit einer Menge +/- 10%. Für die Story
oder Filmszenen wichtige Gegner müssen bleiben.“

Vorbild ist der Gegenstands-Randomizer (Cheat „Random items“, Patch 0218, `runtime/cheats.cpp` `dealRoom`): er
verteilt die frei liegenden Gegenstände eines Raums aus Seed, Raum und Datensatz neu, nur mit Modellen, die der Raum
schon geladen hat, nie Schlüssel, Akten, Waffen oder Gegenstände eines Skripts, nur in der Story.

## 1. Wie das Spiel Gegner in einen Raum setzt

- **Die Gegnerliste („ESL“):** keine Liste je Raum, sondern eine je Kapitelabschnitt (11 Dateien `etc/emleon0X.esl`,
  `omake0X.esl`, `src/game/stage.cpp:57-144`) mit 256 Plätzen zu je 32 Byte (`EM_LIST`, `include/global.h:67-81`).
  Jeder Platz nennt Gegnerart (`id`), Modellvariante (`type`), eine Einstellung für besondere Auftritte (`set`, z. B.
  „R11C 1F IN“, „R300 Take Ashley“), Verhalten (`Character`, `flag`), Lebenspunkte, Position, Drehung, Wachradius und
  den Raum, in dem er steht. Die Liste liegt im Speicher des Spiels (`pG->Em_list`) **und wird mit jedem Spielstand
  gespeichert** (`game.cpp:833`, `869`).
- **Tote Gegner merkt sich das Spiel nach Platznummer** (`pG->Em_flg`, ein Bit je Platz, `em_set.cpp:18-40`, `320`).
  Kommt man wieder, erscheinen die toten nicht mehr.
- **Beim Betreten des Raums** erschafft `EmSetFromList` (`em_set.cpp:137-188`) alle eingeschalteten, nicht toten
  Plätze dieses Raums. Vorher läuft das Raumskript (`sce_sys.cpp:78-85`) und kann Plätze ein- oder ausschalten.
- **Später** holen Raumskripte Gegner gezielt über ihre Platznummer (`EmSetFromList2`, `cEmWrap::setEm`, Wellen,
  Nachschub), über die Gegnerart (`SceCountEmAlive(0x10, 0x20)`: „wie viele Ganados leben noch“) oder über einen Tod
  (`SceExecLinkEmDead`, Bereiche, die auf einen Platz warten).
- **Grenzen eines Raums:**
  - höchstens **4 Gegnerarten** gleichzeitig geladen (`EmReadModule[4]`, `read.cpp:78`); Ashley oder Luis belegen
    eine davon, jede Ganado-Unterart (0x10…0x20) ist eine eigene. Eine fünfte Art erscheint nicht („Em set failed“).
  - etwa **60 Plätze** für alles Bewegliche im Raum (Gegner, Türen, Fässer, Gegenstände; `game.cpp:394-399`).
  - der Speicher für Gegnerdaten teilt sich den Raumspeicher mit dem Raumarchiv.
  - Zwischensequenzen leihen sich den Speicher einer geladenen Gegnerart (`event.cpp:2327-2351`); fehlt die erwartete
    Art, stürzt die Szene ab (Patch 0157 behebt einen solchen Fall in r203).
  - Gegnercode kennt Räume beim Namen (z. B. `em10.cpp:5900`, Bosse prüfen ihren eigenen Raum, `em2b.cpp:968`).

## 2. Was „zufällig gespawnt“ heißen kann

### (a) Andere Gegnerarten an den Plätzen

Nur innerhalb der Arten, die der Raum ohnehin lädt (4er-Grenze, Speicher, Zwischensequenzen). In vielen Räumen
gibt es nur eine oder zwei Ganado-Arten; ein Tausch wäre meist nur ein anderes Aussehen. Riskant: `set`, `Character`
und `flag` bedeuten je Art etwas anderes (ein „Fenster-Ganado“ als Hund), Skripte zählen nach Art (r101 zählt die
Ganados für die Glocke, r224 öffnet den Ausgang, wenn kein El Gigante mehr lebt, r327 füllt nach, wenn 0x1D fehlen).
Auch die Modellvariante (`type`) ist nicht nur Aussehen: Kettensägen-, Gatling- und Garrador-Ganados sind
Untervarianten. **Ergebnis: nur sehr eingeschränkt machbar, hohes Risiko, wenig Wirkung.**

### (b) Gleiche Arten, andere Plätze

- **Ganz neue Positionen** (zufällige Stelle im Raum): nicht sicher machbar. Das Spiel hat keine Abfrage „ist hier
  begehbarer Boden“; Gegner würden in Wänden, im Wasser oder auf Dächern landen.
- **Plätze tauschen** zwischen geeigneten Gegnern derselben Art im selben Raum (Position samt Drehung, Wachradius und
  Verhalten, damit ein Wachposten Wachposten bleibt): sicher, aber für den Spieler kaum sichtbar.
- **Positionen anderer Listeneinträge desselben Raums** nutzen (Stellen, an denen schon einmal ein Gegner stand, etwa
  einer späteren Welle oder eines toten): diese Stellen sind gültiger Boden; das Spiel selbst macht das in r403. Das
  ist der brauchbare Weg für „anders verteilt“ und für zusätzliche Gegner.

### (c) Anzahl ±10 %

- **Weniger:** einzelne geeignete Gegner beim Raumbeginn nicht erschaffen. Ihr Platz bleibt unverändert, ein später
  ausgeschalteter Randomizer bringt sie zurück.
- **Mehr:** Kopien vorhandener Arten (keine neue Art, kein neuer Speicher) an Stellen anderer Listeneinträge des
  Raums. Ihr Tod wird über eine freie Platznummer derselben Liste gemerkt (ein Platz mit `id` 0, der nie einen Gegner
  hat), damit sie nicht bei jedem Betreten wiederkommen (sonst endlose Beute). Die Gegnerliste selbst wird dabei
  **nicht** verändert. Ist der Raum voll, scheitert die Kopie still (das Spiel protokolliert nur).
- **Nicht in Räumen, die Gegner zählen:** Belagerung im Dorf (r101, Glocke nach 15 Toten), Hütte (r11c, 40 Tote oder
  9000 Bilder), r104 (Wellen nach Toten), r11d (Schwestern, Nachschub unter 11 Lebenden), r208/r327 (Nachfüllen),
  r224/r218 (Ausgang erst, wenn alle tot sind) und ähnliche: dort würde ein Gegner mehr oder weniger Skripte aus dem
  Takt bringen oder eine Tür nie öffnen.

## 3. Wichtige Gegner erkennen und auslassen

Es gibt im Spiel **kein Merkmal „wichtig“ oder „Boss“** an einem Gegner. Sicher erkennbar ist es nur so:

1. **Nur in der Story:** nicht in Adas Spiel, nicht in den Mercenaries (`SYS_OMAKE_ADA_GAME`, `SYS_OMAKE_ETC_GAME`,
   wie beim Gegenstands-Randomizer), nicht in Ashleys Teil.
2. **Nur Gegner, die beim Raumbeginn aus der Liste kommen** (`EmSetFromList`), nie die, die ein Skript holt
   (`EmSetFromList2`, `setEm`, `EmSetEvent`): Wellen, Nachschub, Szenen-Gegner.
3. **Nur gewöhnliche Arten:** Ganados 0x10–0x20 außer dem Händler (0x18), vielleicht Hunde (0x21); nie Ashley,
   Luis, Bosse und Sonderfiguren (El Gigante 0x2B, Del Lago 0x2F, Mendez/Salazar 0x34/0x35/0x38, Krauser 0x39,
   Saddler 0x30/0x31, U-3 0x32, Regenerator 0x36, Novistador 0x2D, Armaduras 0x3C, Fahrzeuge, Tiere, Fallen, Objekte
   ab 0x40); nur `set` 0 (keine Sonder-Auftritte wie „R11C 1F IN“), keine Sondervarianten (Kettensäge, Gatling,
   Garrador).
4. **Eine Schutzliste aus den Raumskripten:** jede Platznummer, die ein Raumskript anspricht (`EmSetFromList2`,
   `EmListSetAlive`, `GetEmPtrFromList`, `setEm`/`setPtr`, verknüpfte Tode, wartende Bereiche), bleibt unangetastet;
   Räume mit Zählskripten (`SceCountEmAlive`, Wellen, Bossräume mit `lifeMeterBoss`/`GamePointBossReset`: r10b, r119,
   r11e, r11f, r221, r224, r228, r31b, r31c, r332) ändern keine Anzahl. Von 122 Raumskripten sprechen 90 Gegner an;
   ganze Räume auszulassen ließe zu wenig übrig, darum die Liste je Platznummer. Sie wird einmal mit einem Werkzeug aus
   `src/st*/r*.cpp` erstellt und von Hand geprüft; sie gilt für alle Disc-Arten (die Skripte sind dieselben).

Anmerkung zur Aufgabe: in r104 sind die Plätze 240–254 die Ankunftsgruppe, Streifen und „Renner“; eine Szene s27 und
den Dynamit-Pfosten gibt es in r104 nicht (Dynamit: r223).

## 4. Gleichbleibend und Spielstände

- Alles aus einem Hash von Seed, Listennummer, Platznummer und Raum wie bei den Gegenständen: gleicher Seed, gleicher
  Raum, gleiche Gegner, auch nach Tod, Neuladen oder Raumwechsel.
- **Die gespeicherte Gegnerliste wird nie verändert:** der Randomizer arbeitet mit einer Kopie des Eintrags genau in
  dem Moment, in dem ein Gegner erschaffen wird. Ein Spielstand bleibt mit und ohne Randomizer spielbar, auch auf dem
  GameCube und in Dolphin. Wer den Schalter ausmacht, hat ab dem nächsten Raum wieder das Original.
- Schon getötete Gegner bleiben tot (ihr Bit gilt weiter). Ausgelassene Gegner sind nicht tot; ohne Randomizer
  erscheinen sie wieder. Zusätzliche Gegner merken ihren Tod an freien Platznummern; ohne Randomizer bedeuten diese
  Bits nichts.

## 5. Schwierigkeit

- ±10 % ist in einem Raum mit 3–12 Gegnern oft **0 oder 1 Gegner**. Damit es spürbar wird, besser „je Raum zufällig
  −1, 0 oder +1, über ein Kapitel etwa ±10 %“, oder ein Schalter mit Stufen (±10 %, ±25 %).
- Die **eigene Schwierigkeitsanpassung des Spiels** (`pG->point`/`Game_level`, `game.cpp:1368-1502`) gleicht aus:
  mehr Treffer gegen Leon senken die Stufe (Gegner schwächer, weniger aggressiv), viele Kopftreffer heben sie. Sie
  ändert nicht die Lebenspunkte der Gegner, sondern Schaden, Angriffslust und Beute, und in r208 kommen bei hoher
  Stufe von selbst mehr Ganados. Mehr Gegner werden also teilweise abgefedert.
- Mehr Gegner kosten Prozessorzeit: auf schwachen Handys (siehe Benchmark, Abschnitt 3 der 0.99.12) eher „−“.

## 6. Übersicht

| Variante | Machbar? | Aufwand | Risiko | Empfehlung |
|---|---|---|---|---|
| (a) andere Gegnerarten | nur innerhalb der geladenen Ganado-Arten, oft nur anderes Aussehen | hoch (Tabelle je Art: was `set`, `type`, `Character` bedeuten) | hoch (Zählskripte, Zwischensequenzen, Sondervarianten) | nicht jetzt |
| (b1) zufällige neue Positionen | nein (kein „Boden hier?“) | – | sehr hoch (Gegner in Wänden) | nein |
| (b2) Plätze tauschen / Stellen anderer Einträge des Raums | ja | mittel | gering | ja, Stufe 1 |
| (c−) weniger Gegner | ja, ohne Zählräume | gering | gering bis mittel | ja, Stufe 1 |
| (c+) mehr Gegner (Kopien) | ja, ohne Zählräume, im Rahmen der 60 Plätze | mittel | mittel (Raum voll, Leistung, Beute) | ja, Stufe 1 oder 2 |
| Schutzliste aus den Skripten | ja | mittel (Werkzeug + Durchsicht) | – | Voraussetzung |

## 7. Vorschlag für einen ersten, sicheren Schritt

**Stufe 1 „Gegner verteilen“** (Cheat-Schalter „Random enemies“ neben „Random items“, eigener Seed):
1. Werkzeug, das aus den Raumskripten die geschützten Platznummern je Liste und die Zählräume sammelt; Durchsicht.
2. Ein Patch in `EmSetFromList` (wie 0218): vor dem Erschaffen fragt er `runtime/cheats.cpp`, ob ein geeigneter
   Gegner ausgelassen wird oder mit der Stelle eines anderen Eintrags desselben Raums erscheint; danach, ob Kopien
   dazukommen (je Raum −1/0/+1, nie in Zählräumen). Die Liste selbst bleibt unverändert.
3. Protokoll je Raum („cheat: random enemies in r103 (seed N): 7 of 9 placed elsewhere, 1 extra, 0 left out“).
4. Prüfung am PC: Raum-Rundgang über alle Räume mit Debug-, EU- und US-Discs (keine Abstürze, keine „Em set failed“
   außer gewollt), die Zählräume unverändert, Durchspielen der Dorf-Belagerung und der Hütte zum Vergleich;
   danach Gerätetest.

Aufwand Stufe 1: etwa ein bis zwei Arbeitstage einer Sitzung. Stufe 2 (andere Ganado-Arten im selben Raum) erst nach
Erfahrung mit Stufe 1 und einer Tabelle, welche `set`/`type`-Werte nur Aussehen sind.

## Quellen der Prüfung

Gegnerliste und Erschaffen: `include/global.h:66-81, 202, 211`, `src/game/em_set.cpp`, `src/game/stage.cpp:57-251`,
`src/game/sce_sys.cpp:74-86`, `src/game/read.cpp:78, 272-573`, `src/game/em.cpp:76-206`, `src/game/game.cpp:394-399,
833, 869, 1259, 1368-1506`. Skripte: `src/st1/r100.cpp`, `r101.cpp:316-535, 716-826`, `r104.cpp`, `r11c.cpp:285-527`,
`r11d.cpp`, `src/st2/r208.cpp`, `r218.cpp:201-215`, `r224.cpp:632-641`, `src/st3/r327.cpp`, `r31c.cpp`, `r332.cpp`,
`src/st4/r403.cpp`. Gegnerarten: `src/t_emlist/t_emlist.cpp:31-437`.
