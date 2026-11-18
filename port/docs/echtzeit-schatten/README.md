# Echtzeit-Schatten: Notizen zur Studie

Bericht: `port/docs/echtzeit-schatten-studie.md` (15.11.2026, Branch `realtime-shadows`).

- `prototyp/`: der Wegwerf-Versuch für das PC-Zeichenwerkzeug `gx_chunk_render`, **nicht für die App**. Er wird nur
  in einen WSL-Ordner eingesetzt: `python3 prototyp/apply_proto.py TREE [--again]`, dann
  `ninja -C TREE/port/build/app-host-gl gx_chunk_render`. Schalter und Aufbau: Bericht, T3 und T10.
  `batch.sh`, `dirs.sh` und `deliver.sh` zeichnen GX-Aufnahmen mit und ohne Versuch und machen die Vergleichsbilder
  in `E:\Development\Android\builds\realtime-shadows\`.
- `lit_survey.cpp`: zählt die Lichter aller Räume. Er liest die LIT-Dateien mit dem Yz2-Entpacker und dem LIT-Leser
  der App. Bauen in WSL:
  `clang++ -std=c++17 -include memory -include stdexcept -I$C lit_survey.cpp $C/archive.cpp $C/room_lighting.cpp $C/ports/yz2.cpp`
  mit `C=port/android-decomp/app/src/main/cpp`. Aufruf mit den `St*/r*.das` der Disc-Daten.
- `lit_survey_debug_disc.txt`: das Ergebnis für die Debug-Disc (127 Räume). Es enthält nur Raumnamen und Zahlen.
- Stufe 4 (16.11.2026), Bestandsaufnahme alles Beweglichen: `census_apply.py TREE` setzt den Messcode in einen
  WSL-Ordner ein (`runtime/gx_owner.cpp`, nur mit `RE4_OWNER_CENSUS=1` aktiv, **nicht für die App**), dann
  `whole_build.sh i686` und je Raum ein Start; `census_sum.py DIR [RAUM…]` zählt nach Art.
  `census_totals_debug_disc.txt`: die Summen über die 127 Räume der Debug-Disc (Anzahl, Art, Räume; nur Zahlen
  und Namen der Arten). `where_apply.py TREE` gibt mit `RE4_SHADOW_WHERE=1` die Orte der Gegenstände, Stoffe und
  bewegten Raum-Modelle aus (für die Bilder in `E:\Development\Android\builds\realtime-shadows\objects\`).
