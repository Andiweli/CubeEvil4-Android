# port/ouya/tools/menu_strings.py REPO (the repository root): run after the main app's menu texts change.
# Copies the OUYA menu's texts from the main app's in-game menu (all its languages) and adds the
# OUYA's own (all the main app's languages; English is Android's fallback).
import os, re, sys
repo = sys.argv[1]
src = os.path.join(repo, 'port/android-decomp/app/src/main/res')
dst = os.path.join(repo, 'port/ouya/app/src/main/res')
keys = ['ingame_title', 'ingame_back', 'ingame_graphics', 'ingame_resolution', 'ingame_on', 'ingame_off',
        'ingame_toggle_value', 'ingame_potato', 'ingame_potato_weather', 'ingame_potato_special',
        'ingame_potato_help', 'ingame_comfort', 'ingame_auto_aim', 'ingame_always_run', 'ingame_far_camera',
        'ingame_auto_aim_help', 'ingame_always_run_help', 'ingame_far_camera_help', 'ingame_perf_section',
        # master 0.99.15 (merged 14.11.2026): the particle effects and shadows of the Potato mode, the frame
        # skip Off / Normal / Aggressive (Aggressive: the game's own, runtime/cpu_skip.cpp)
        'ingame_potato_particles', 'ingame_particles_normal', 'ingame_particles_less', 'ingame_particles_none',
        'ingame_potato_shadows', 'ingame_frame_skip', 'ingame_skip_normal', 'ingame_skip_aggressive',
        'ingame_skip_normal_help', 'ingame_skip_aggressive_help', 'ingame_gentle_qte',
        # the cheats (wish of the project owner, 07.11.2026: the basic ones and the weapons, not the
        # characters and the extra games)
        'ingame_cheat_section_player', 'ingame_cheat_section_weapons', 'ingame_cheat_section_game',
        'ingame_cheat_health', 'ingame_cheat_ammo', 'ingame_cheat_invisible', 'ingame_cheat_onehit',
        'ingame_cheat_case', 'ingame_cheat_money', 'ingame_cheat_handcannon', 'ingame_cheat_killer7',
        'ingame_cheat_matilda', 'ingame_cheat_chicago', 'ingame_cheat_mine', 'ingame_cheat_rocket', 'ingame_cheat_qte',
        'ingame_cheats_help', 'ingame_cheat_done', 'ingame_cheat_already', 'ingame_cheat_raised',
        'ingame_cheat_not_now', 'ingame_cheat_no_room', 'ingame_cheat_queued', 'ingame_cheat_unsupported',
        # the disc installer again (OuyaInstaller)
        'ingame_discs', 'ingame_discs_help', 'ingame_import_discs']
Q = "\\'"  # an apostrophe in an Android string
own = {
    'values': {
        # the menu's cheats: the main app's heading says "& Mods", the OUYA has no mods
        'ouya_cheats': 'Cheats',
        'ouya_view_distance': 'View distance',
        'ouya_colours': 'Colour calculation',
        'ouya_colours_simple': 'Simplified',
        'ouya_colours_exact': 'Exact',
        'ouya_graphics_help': 'Resolution: the game' + Q + 's picture as a share of the GameCube' + Q + 's 640 × 528 '
                              'points; lower is faster. The simplified colour calculation saves graphics work; colours '
                              'may differ very slightly. View distance: how far you see before the distance fog; what '
                              'lies behind it is not drawn. It takes effect at once.',
    },
    'values-de': {
        'ouya_cheats': 'Cheats',
        'ouya_view_distance': 'Sichtweite',
        'ouya_colours': 'Farbberechnung',
        'ouya_colours_simple': 'Vereinfacht',
        'ouya_colours_exact': 'Genau',
        'ouya_graphics_help': 'Auflösung: das Bild des Spiels als Anteil der 640 × 528 Punkte des GameCube; weniger ist '
                              'schneller. Die vereinfachte Farbberechnung spart Grafikarbeit; Farben können ganz leicht '
                              'abweichen. Sichtweite: wie weit du bis zum Distanznebel siehst; was dahinter liegt, wird '
                              'nicht gezeichnet. Sie wirkt sofort.',
    },
    # the other languages of the main app's menu (08.11.2026, wish of the project owner); its words for fog,
    # doors and cut scenes; no form of address, the main app mixes them
    'values-fr': {
        'ouya_cheats': 'Triches',
        'ouya_view_distance': 'Distance de vue',
        'ouya_colours': 'Calcul des couleurs',
        'ouya_colours_simple': 'Simplifié',
        'ouya_colours_exact': 'Exact',
        'ouya_graphics_help': 'Résolution : l’image du jeu en proportion des 640 × 528 points de la GameCube ; plus '
                              'bas, c’est plus rapide. Le calcul simplifié des couleurs allège le travail graphique ; '
                              'les couleurs peuvent différer très légèrement. Distance de vue : jusqu’où porte la vue '
                              'avant le brouillard lointain ; ce qui se trouve au-delà n’est pas dessiné. Elle '
                              's’applique aussitôt.',
    },
    'values-es': {
        'ouya_cheats': 'Trucos',
        'ouya_view_distance': 'Distancia de visión',
        'ouya_colours': 'Cálculo de colores',
        'ouya_colours_simple': 'Simplificado',
        'ouya_colours_exact': 'Exacto',
        'ouya_graphics_help': 'Resolución: la imagen del juego como parte de los 640 × 528 puntos de la GameCube; '
                              'menos es más rápido. El cálculo de colores simplificado ahorra trabajo gráfico; los '
                              'colores pueden variar muy ligeramente. Distancia de visión: hasta dónde llega la vista '
                              'antes de la niebla lejana; lo que queda detrás no se dibuja. Se aplica '
                              'al instante.',
    },
    'values-it': {
        'ouya_cheats': 'Trucchi',
        'ouya_view_distance': 'Distanza visiva',
        'ouya_colours': 'Calcolo dei colori',
        'ouya_colours_simple': 'Semplificato',
        'ouya_colours_exact': 'Esatto',
        'ouya_graphics_help': 'Risoluzione: l’immagine del gioco come quota dei 640 × 528 punti del GameCube; meno è '
                              'più veloce. Il calcolo semplificato dei colori riduce il lavoro grafico; i colori possono '
                              'differire leggermente. Distanza visiva: fin dove arriva la vista prima della nebbia in '
                              'lontananza; ciò che sta oltre non viene disegnato. Vale '
                              'subito.',
    },
    'values-pt': {
        'ouya_cheats': 'Trapaças',
        'ouya_view_distance': 'Distância de visão',
        'ouya_colours': 'Cálculo de cores',
        'ouya_colours_simple': 'Simplificado',
        'ouya_colours_exact': 'Exato',
        'ouya_graphics_help': 'Resolução: a imagem do jogo como fração dos 640 × 528 pontos do GameCube; menos é mais '
                              'rápido. O cálculo de cores simplificado poupa trabalho gráfico; as cores podem variar '
                              'muito pouco. Distância de visão: até onde a vista alcança antes da neblina distante; o '
                              'que fica além não é desenhado. Vale de imediato.',
    },
    'values-ru': {
        'ouya_cheats': 'Читы',
        'ouya_view_distance': 'Дальность обзора',
        'ouya_colours': 'Расчёт цветов',
        'ouya_colours_simple': 'Упрощённый',
        'ouya_colours_exact': 'Точный',
        'ouya_graphics_help': 'Разрешение: изображение игры как доля от 640 × 528 точек GameCube; меньше — быстрее. '
                              'Упрощённый расчёт цветов снижает нагрузку на графику; цвета могут совсем немного '
                              'отличаться. Дальность обзора: как далеко видно до дальнего тумана; то, что за ним, не '
                              'рисуется. Действует сразу.',
    },
    'values-ar': {
        'ouya_cheats': 'الغش',
        'ouya_view_distance': 'مدى الرؤية',
        'ouya_colours': 'حساب الألوان',
        'ouya_colours_simple': 'مبسّط',
        'ouya_colours_exact': 'دقيق',
        'ouya_graphics_help': 'الدقة: صورة اللعبة كنسبة من 640&#x200E; × &#x200E;528 نقطة في GameCube؛ الأقل أسرع. '
                              'يخفف حساب الألوان المبسّط عبء الرسوميات؛ وقد تختلف الألوان اختلافًا طفيفًا جدًا. مدى '
                              'الرؤية: إلى أي مدى تمتد الرؤية قبل ضباب المسافة؛ وما يقع خلفه لا يُرسم. يسري '
                              'فورًا.',
    },
    'values-hi': {
        'ouya_cheats': 'चीट्स',
        'ouya_view_distance': 'दृश्य दूरी',
        'ouya_colours': 'रंग गणना',
        'ouya_colours_simple': 'सरलीकृत',
        'ouya_colours_exact': 'सटीक',
        'ouya_graphics_help': 'रिज़ॉल्यूशन: GameCube के 640 × 528 बिंदुओं के हिस्से के रूप में गेम की तस्वीर; कम होने '
                              'पर तेज़। सरलीकृत रंग गणना ग्राफ़िक्स का काम कम करती है; रंग बहुत थोड़े अलग हो सकते '
                              'हैं। दृश्य दूरी: दूरी के कोहरे से पहले कितनी दूर तक दिखता है; उसके पीछे का हिस्सा नहीं '
                              'बनाया जाता। यह तुरंत लागू होती है।',
    },
    'values-in': {
        'ouya_cheats': 'Cheat',
        'ouya_view_distance': 'Jarak pandang',
        'ouya_colours': 'Perhitungan warna',
        'ouya_colours_simple': 'Disederhanakan',
        'ouya_colours_exact': 'Tepat',
        'ouya_graphics_help': 'Resolusi: gambar game sebagai bagian dari 640 × 528 titik GameCube; lebih rendah lebih '
                              'cepat. Perhitungan warna yang disederhanakan mengurangi beban grafis; warna bisa sedikit '
                              'sekali berbeda. Jarak pandang: seberapa jauh pandangan sampai kabut jarak jauh; yang ada '
                              'di baliknya tidak digambar. Langsung berlaku.',
    },
    'values-zh': {
        'ouya_cheats': '作弊',
        'ouya_view_distance': '视距',
        'ouya_colours': '颜色计算',
        'ouya_colours_simple': '简化',
        'ouya_colours_exact': '精确',
        'ouya_graphics_help': '分辨率：游戏画面占 GameCube 640 × 528 像素点的比例；越低越快。简化的颜色计算可减轻图形'
                              '负担；颜色可能有极细微的差别。视距：在远处雾气之前能看多远；雾气后面的内容不会绘制。'
                              '立即生效。',
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
    # "Gentler QTEs": the main app's help without its second sentence (a finger on the touch screen)
    m = re.search(r'<string name="ingame_gentle_qte_help">(.*?)</string>', text, re.S)
    if m:
        first = re.match(r'(.*?(?:[.।](?=\s|$)|。))', m.group(1), re.S)
        lines.append('    <string name="ouya_gentle_qte_help">%s</string>' % (first.group(1) if first else m.group(1)))
    elif d == 'values':
        sys.exit('missing ingame_gentle_qte_help')
    os.makedirs(os.path.join(dst, d), exist_ok=True)
    with open(os.path.join(dst, d, 'strings_menu.xml'), 'w', encoding='utf-8', newline='\n') as out:
        out.write('<?xml version="1.0" encoding="utf-8"?>\n')
        out.write('<!-- The OUYA app menu (L3): the texts of the main app\'s in-game menu\n'
                  '     (port/android-decomp/app/src/main/res/values*/strings_ingame.xml, copied with their\n'
                  '     translations by a script) and the OUYA\'s own (port/ouya/tools/menu_strings.py). -->\n<resources>\n')
        out.write('\n'.join(lines) + '\n</resources>\n')
    print(d, len(lines))
