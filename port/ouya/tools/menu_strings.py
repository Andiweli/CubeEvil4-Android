# port/ouya/tools/menu_strings.py REPO (the repository root): run after the main app's menu texts change.
# Copies the OUYA menu's texts from the main app's in-game menu (all its languages) and adds the
# OUYA's own (English, German; the other languages fall back to English).
import os, re, sys
repo = sys.argv[1]
src = os.path.join(repo, 'port/android-decomp/app/src/main/res')
dst = os.path.join(repo, 'port/ouya/app/src/main/res')
keys = ['ingame_title', 'ingame_back', 'ingame_graphics', 'ingame_resolution', 'ingame_on', 'ingame_off',
        'ingame_toggle_value', 'ingame_potato', 'ingame_potato_weather', 'ingame_potato_special',
        'ingame_potato_help', 'ingame_comfort', 'ingame_auto_aim', 'ingame_always_run', 'ingame_far_camera',
        'ingame_auto_aim_help', 'ingame_always_run_help', 'ingame_far_camera_help', 'ingame_perf_section',
        'ingame_frame_skip', 'ingame_frame_skip_help']
Q = "\\'"  # an apostrophe in an Android string
own = {
    'values': {
        'ouya_view_distance': 'View distance',
        'ouya_colours': 'Colour calculation',
        'ouya_colours_simple': 'Simplified',
        'ouya_colours_exact': 'Exact',
        'ouya_graphics_help': 'Resolution: the game' + Q + 's picture as a share of the GameCube' + Q + 's 640 × 528 '
                              'points; lower is faster. The simplified colour calculation saves graphics work; colours '
                              'may differ very slightly. View distance: how far you see before the distance fog; what '
                              'lies behind it is not drawn. It takes effect at the next door or cut scene.',
    },
    'values-de': {
        'ouya_view_distance': 'Sichtweite',
        'ouya_colours': 'Farbberechnung',
        'ouya_colours_simple': 'Vereinfacht',
        'ouya_colours_exact': 'Genau',
        'ouya_graphics_help': 'Auflösung: das Bild des Spiels als Anteil der 640 × 528 Punkte des GameCube; weniger ist '
                              'schneller. Die vereinfachte Farbberechnung spart Grafikarbeit; Farben können ganz leicht '
                              'abweichen. Sichtweite: wie weit du bis zum Distanznebel siehst; was dahinter liegt, wird '
                              'nicht gezeichnet. Sie wirkt ab der nächsten Tür oder Zwischensequenz.',
    },
}
for d in sorted(os.listdir(src)):
    f = os.path.join(src, d, 'strings_ingame.xml')
    if not d.startswith('values') or not os.path.isfile(f):
        continue
    text = open(f, encoding='utf-8').read()
    lines = []
    for k in keys:
        m = re.search(r'^\s*<string name="%s"[^>]*>.*?</string>' % k, text, re.M | re.S)
        if m:
            lines.append('    ' + m.group(0).strip())
        elif d == 'values':
            sys.exit('missing ' + k)
    for k, v in own.get(d, {}).items():
        lines.append('    <string name="%s">%s</string>' % (k, v))
    os.makedirs(os.path.join(dst, d), exist_ok=True)
    with open(os.path.join(dst, d, 'strings_menu.xml'), 'w', encoding='utf-8', newline='\n') as out:
        out.write('<?xml version="1.0" encoding="utf-8"?>\n')
        out.write('<!-- The OUYA app menu (L3): the texts of the main app\'s in-game menu\n'
                  '     (port/android-decomp/app/src/main/res/values*/strings_ingame.xml, copied with their\n'
                  '     translations by a script) and the OUYA\'s own (English and German). -->\n<resources>\n')
        out.write('\n'.join(lines) + '\n</resources>\n')
    print(d, len(lines))
