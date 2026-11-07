# port/ouya/tools/sync_disc_import.py REPO (the repository root): run after the main app's disc import changes.
#
# The OUYA app's disc installer (wish of the project owner, 07.11.2026: disc 1 and 2 of the debug build and
# the US, European and Japanese releases) reads and checks the discs with the main app's classes
# (port/android-decomp/app/src/main/java/com/ast/re4decomp/disc): copied here into the OUYA app, with
# what Android 4.1 lacks replaced (java.nio.charset.StandardCharsets comes with Android 4.4), and the code
# check catalogs of the releases (assets references*.tsv). The RVZ reader's native side (disc_rvz*.cpp,
# zstd) the OUYA app's CMakeLists.txt takes from the main app's folder as it is.
import os
import re
import shutil
import sys

repo = sys.argv[1]
main = os.path.join(repo, 'port/android-decomp/app/src/main')
ouya = os.path.join(repo, 'port/ouya/app/src/main')
classes = ['GameCubeDisc.java', 'DiscInstaller.java', 'ReferenceCatalog.java', 'RvzImage.java']
src = os.path.join(main, 'java/com/ast/re4decomp/disc')
dst = os.path.join(ouya, 'java/com/ast/re4decomp/disc')
os.makedirs(dst, exist_ok=True)
note = ('// Copied from the main app (port/android-decomp/.../disc/%s) by port/ouya/tools/sync_disc_import.py:\n'
        '// do not change here, change there and run the script again (Android 4.1: no StandardCharsets).\n')
for name in classes:
    text = open(os.path.join(src, name), encoding='utf-8').read()
    text = text.replace('import java.nio.charset.StandardCharsets;\n', '')
    text = text.replace('StandardCharsets.UTF_8', 'java.nio.charset.Charset.forName("UTF-8")')
    text = text.replace('StandardCharsets.US_ASCII', 'java.nio.charset.Charset.forName("US-ASCII")')
    if 'StandardCharsets' in text:
        sys.exit('%s: StandardCharsets left' % name)
    with open(os.path.join(dst, name), 'w', encoding='utf-8', newline='\n') as out:
        out.write(note % name)
        out.write(text)
    print('java', name)
assets = os.path.join(ouya, 'assets')
os.makedirs(assets, exist_ok=True)
for name in sorted(os.listdir(os.path.join(main, 'assets'))):
    if re.match(r'references(-[a-z]+)?\.tsv$', name):
        shutil.copyfile(os.path.join(main, 'assets', name), os.path.join(assets, name))
        print('asset', name)
