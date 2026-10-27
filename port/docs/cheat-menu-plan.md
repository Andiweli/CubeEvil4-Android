# Cheat-Menü im Ingame-Menü (Machbarkeit, 27.10.2026)

Wunsch des Projektinhabers (27.10.2026): ein Bereich „Cheats“ ganz oben auf der Seite **MANAGEMENT** des
Ingame-Menüs der App, mit Schaltern für:

1. Unendlich Gesundheit (Leon und Ashley)
2. Unendlich Munition (jede ausgerüstete Waffe)
3. One-Hit-Kill
4. Killer7 (Stufen 3/1/3/3)
5. Aktenkoffer L
6. 1.000.000 Peseten

Hintergrund: mit den EU-Discs ist das Debug-Menü des Spiels ausgeblendet (M13, Entscheidung vom 26.10.2026);
das Cheat-Menü war dafür als eigenes Vorhaben vorgesehen (`retail-eu-plan.md`). Diese Datei hält fest, was der
Spielcode dafür schon mitbringt, was neu gebaut werden müsste und was noch zu entscheiden ist. **Noch nichts gebaut.**

## Ergebnis in Kürze

Alle sechs sind machbar, mit beiden Disc-Arten (der Spielcode ist in beiden Fällen der der Debug-Version).
Drei davon sind Schalter, die der Prototyp für seine Tester schon eingebaut hat. Die anderen drei sind
einmalige Änderungen am Spielstand.

| Cheat | Art | Grundlage im Spielcode | Grenzen |
| --- | --- | --- | --- |
| Unendlich Gesundheit | Schalter | `DBG_NO_DEATH` (Debug_flg Bit 72) | Sofort-Tode in Szenen bleiben (unten) |
| Unendlich Munition | Schalter | `DBG_INF_BULLET` (Bit 73) | Munitionsanzeige zeigt „100“ (behebbar) |
| One-Hit-Kill | Schalter | `DBG_EM_WEAK` (Bit 82) | einige Bosse rechnen Schaden selbst (unten) |
| Killer7 3/1/3/3 | einmalig | Gegenstand 0x2A, Stufen in `cItem::lv` | braucht Platz im Koffer; landet im Spielstand |
| Aktenkoffer L | einmalig | Gegenstand 0x7E, `SubScreenWk.board_next = 2` | nur Leon; landet im Spielstand |
| 1.000.000 Peseten | einmalig | `pG->peseta` (Obergrenze 99 999 999, `addMoney`) | nur Leon; landet im Spielstand |

## Die drei Schalter

Die Bits liegen in `GLOBAL_WK::Debug_flg` (0x60), **außerhalb** des Spielstands (0x4F80..0x8678): ein Spielstand
merkt sich nichts davon, Speichern mit eingeschaltetem Cheat verändert ihn nicht.

### Unendlich Gesundheit: `DBG_NO_DEATH`

- `LifeDownSet2` (`src/game/em_sub.cpp`) zieht Leon (`pG->pl_life`) und dem Partner (`pG->ashley_life`, auch Luis)
  den Schaden ab und füllt mit dem Bit danach wieder auf das Maximum auf. `PlSetDamage` macht aus tödlichen Treffern
  normale Treffer (Leon fällt nicht um), `cSubChar` (`pl_npc.cpp`) ebenso für Ashley.
- Einige Fallen fragen das Bit selbst: Pendel in r205 (keine Todes-Szene), Feuer gegen Ashley in r206, ein
  Angriff des Bosses em38.
- **Grenze:** Viele Gegner und Fallen setzen bei einem Fang-Angriff das Leben direkt auf 0 und starten eine
  Todes-Szene (`DiedemoExec`), ohne das Bit zu fragen: Kettensäge (`em10_R1_C_SawHit`), Würgen, Bombe im Griff,
  Klaue (em10), El Gigante (em2b), rollende Felsen (`emrock.cpp`), Lore und Gondel und weitere Bosse (gut 50
  Stellen, `grep "pl_life = 0" src`). Dort stirbt Leon
  weiter. Auch „Ashley wird weggetragen“ ist keine Frage der Gesundheit. Das abzufangen hieße, jede Szene einzeln
  umzubauen; nicht vorgesehen.
- Vorschlag: zusätzlich in jedem Bild Leben von Leon und Partner auf das Maximum setzen (harmlos, hilft bei Schaden,
  der an `LifeDownSet2` vorbei abgezogen wird).

### Unendlich Munition: `DBG_INF_BULLET`

- `cItemMgr::trigger` (`src/game/item.cpp`) verbraucht mit dem Bit nichts: Magazin bleibt voll, nie nachladen.
  Das gilt für jede ausgerüstete Waffe, auch Granaten (`pl_grenade.cpp`), den Raketenwerfer (`objRocket.cpp`
  lädt nach, der Einweg-Werfer 0x35 wird nicht weggeworfen) und Minenwerfer.
- **Schönheitsfehler:** `cItemMgr::bulletNum` meldet mit dem Bit immer 100, die Anzeige unten rechts zeigt „100“.
  Vorschlag: ein Port-Patch, damit die Anzeige die echte (gleichbleibende) Magazinzahl zeigt; beim Einschalten
  das Magazin der ausgerüsteten Waffe einmal füllen (sonst merkt das Spiel ein leeres Magazin und will nachladen).
- Der Prototyp kennt noch `DBG_INF_BULLET2` (Nachladen ohne Munition im Koffer, Wurfwaffen bleiben bei 5); passt
  nicht zum Wunsch „nie leer“, nicht vorgesehen.

### One-Hit-Kill: `DBG_EM_WEAK`

- `LifeDownSet2` setzt mit dem Bit den Schaden jedes Treffers auf das ganze Leben des Gegners. Gilt nur für Gegner,
  nicht für Leon und Partner (eigene Zweige davor).
- **Grenze:** einige Bosse und Teile rechnen Schaden selbst an diesem Weg vorbei (El Gigante em2b, der Boss aus
  mehreren Teilen em38, Hubschrauber em3a, em3c-Köpfe, Garrador-Klaue em10): dort bleibt es beim normalen Schaden.
  Bosse mit festen Phasen sind mit dem Bit nicht geprüft; vor dem Freigeben einmal jeden Bossraum am PC anspielen.
- Ein Treffer mit „mindestens 1 Leben lassen“ (Flag 1) bleibt bei 1, wie im Original.

## Die drei einmaligen Änderungen

Sie ändern Gegenstände bzw. Geld, also das, was beim nächsten Speichern im Spielstand steht. Ausschalten kann
sie nicht sinnvoll rückgängig machen (ausgegebenes Geld, eine inzwischen benutzte Waffe). Vorschlag: **Knöpfe
statt Schalter** („Holen“), nur bedienbar, wenn Leon spielt (`pG->pl_type == 0`; Ashley-Abschnitt,
Mercenaries und Ada haben keinen Koffer bzw. eigenes Geld). Ausführen im nächsten Bild auf dem Spiel-Thread,
nur wenn kein Untermenü offen ist (`STA_SUB_SCRN` aus).

### Killer7 mit 3/1/3/3

- Gegenstand **0x2A** (Leons Killer7, Modell `ss_wep17.dat`; 0x2B ist eine zweite Fassung). Die Höchststufen in
  `wep_level_info` sind für 0x2A genau `{3, 1, 3, 3}` (Feuerkraft, Feuerrate, Nachladen, Magazin): das gewünschte
  „3/1/3/3“ ist also voll ausgebaut. Gespeichert wird Stufe − 1: `setPowerLevel(2)`, `setSpeedLevel(0)`,
  `setReloadLevel(2)`, `setBulletLevel(2)`, dann Magazin voll (`WeaponId2ChargeNum(0x2A, 3)` = 14).
- Hat Leon schon einen Killer7, wird er nur auf 3/1/3/3 gebracht.
- `ItemMgr.get(0x2A, 0)` legt die Waffe ohne Platz an; Patch 0100 legt sie beim nächsten Öffnen des Koffers an
  die erste freie Stelle (im Koffer, sonst auf das Ablagefeld). Ist nirgends Platz, bleibt der Fehler des Originals
  (Waffe ohne Platz). Vorher prüfen und sonst „Kein Platz im Koffer“ melden.
- Munition (Magnum) gibt der Knopf nicht; mit „Unendlich Munition“ braucht es keine.

### Aktenkoffer L

- Koffer-Gegenstände: 0x7C (klein), 0x7D (M), **0x7E (L)**, 0x7F (XL). `SubScreenRoomInit` (`sscrn.cpp`) nimmt die
  Größe aus dem größten vorhandenen; der Händler kauft sie nicht zurück (`checkExerciseItem`).
- Umsetzung wie ein Kauf beim Händler (`ss_shop.cpp` `buyItem`) bzw. der Koffer-Editor des Prototyps
  (`ss_debug.cpp`): kleinere Koffer entfernen, `ItemMgr.get(0x7E, 0)`, `SubScreenWk.board_next = 2`; beim nächsten
  Öffnen wächst der Koffer (`ss_pzzl.cpp`, `board_size != board_next`) und behält die Anordnung.
- Hat Leon schon L oder XL, passiert nichts (kein Verkleinern).
- Zu prüfen: das Wachsen außerhalb des Händlers (der Weg ist derselbe, aber so nie ausgelöst).

### 1.000.000 Peseten

- Geld steht in `pG->peseta`; `addMoney` deckelt bei 99 999 999. Im Ashley-Abschnitt tauscht das Spiel
  `peseta` und `peseta_bak` (`PlSelect`), deshalb nur, wenn Leon spielt.
- Offen: jedes Drücken +1.000.000, oder einmal „auf mindestens 1.000.000“.

## Umsetzung (Vorschlag, wie die Spielsprache in R7)

- **App:** `CoreMenu.management()` bekommt ganz oben eine Überschrift „Cheats“ mit drei Umschaltern und drei
  Knöpfen (je drei pro Zeile wie die Sprachwahl), dazu ein Hinweistext („Gegenstände und Geld bleiben im
  Spielstand“). Texte in allen 10 Sprachen (`strings_ingame.xml`, `check_translations.py`), `CoreMenuLayoutTest`.
- **Weg in den Kern:** `CoreBridge.setCheats(int bits)` und `CoreBridge.cheatAction(int what)` →
  `core_mode.cpp` → `re4_core_set_cheats` / `re4_core_cheat_action` (`core_api.cpp`, `re4_core.h`), über den
  eigenen Kern-Prozess mit zwei neuen Nachrichten (`re4_core_ipc.h`, `host/coreproc.cpp`). Startwert
  `RE4_CHEATS=<bits>` in den Kern-Einstellungen, wie `RE4_GAME_LANGUAGE`.
- **Kern:** neue Datei `runtime/cheats.cpp` (mit den Spiel-Schaltern übersetzt wie `game_language.cpp`), ein
  Bild-Haken (`re4_add_frame_hook`): solange ein Schalter an ist, setzt er sein Bit in jedem Bild (so überlebt es
  Titel, Soft-Reset und das Laden eines Instant-Spielstands); beim Ausschalten löscht er es einmal (das
  Debug-Menü der Debug-Discs kann die Bits weiter selbst schalten). Einmalige Aktionen laufen im nächsten
  passenden Bild. Jede Aktion eine Zeile im Log (für Fehlerberichte: „cheat: …“).
- **Patch** nur für die Munitionsanzeige (`item.cpp` `bulletNum`, siehe oben).
- **Prüfen:** Laufzeittest mit Testhilfe (`RE4_TEST_CHEATS`), PC-Läufe in r100 (Schaden, Schießen, Treffer) und in
  den Bossräumen, die Koffer-Erweiterung als GX-Bild, Prüfstrecke Debug- und EU-Discs, Gerät.

## Offene Entscheidungen (Projektinhaber)

1. Killer7, Koffer L und Geld als **einmalige Knöpfe** statt Schalter? (Empfehlung: ja, weil sie den Spielstand
   dauerhaft ändern.)
2. Geld: **+1.000.000 je Druck** oder **auf mindestens 1.000.000**? (Empfehlung: +1.000.000 je Druck.)
3. Unendlich Gesundheit mit der Grenze „Sofort-Tode in Szenen bleiben“ annehmen? (Empfehlung: ja.)
4. Schalter über App-Neustarts **gespeichert** wie die übrigen Einstellungen? (Empfehlung: ja.)
5. Auch mit den Debug-Discs anbieten? (Empfehlung: ja; dort hat zusätzlich das Debug-Menü „NO DEATH“ und
   „INF BULLET“.)
