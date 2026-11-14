// The window version's texts (see gui_text.h). Each row: English, German, French, Spanish, Italian, Portuguese
// (Brazil), Russian, Arabic, Hindi, Indonesian, Chinese (simplified), the order of guitext::Lang. Our own code (CC0).
#include "gui_text.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace guitext {

namespace {

struct Row {
  Id id;
  const char *t[kLangCount];
};

// clang-format off
const Row kRows[] = {
{kVersion, {"Version %1", "Version %1", "Version %1", "Versión %1", "Versione %1", "Versão %1", "Версия %1",
  "الإصدار %1", "संस्करण %1", "Versi %1", "版本 %1"}},
{kIntro, {
  "Turns an HD texture pack in the Dolphin format (a ZIP file or a folder) into one .c4tex file for CubeEvil4. The pack is only read, never changed; a ZIP file is read as it is, nothing is unpacked.",
  "Wandelt ein HD-Texturpaket im Dolphin-Format (eine ZIP-Datei oder einen Ordner) in eine .c4tex-Datei für CubeEvil4 um. Das Paket wird nur gelesen, nie verändert; eine ZIP-Datei wird direkt gelesen, nichts wird ausgepackt.",
  "Convertit un pack de textures HD au format Dolphin (un fichier ZIP ou un dossier) en un seul fichier .c4tex pour CubeEvil4. Le pack est seulement lu, jamais modifié ; un fichier ZIP est lu tel quel, rien n’est décompressé.",
  "Convierte un paquete de texturas HD en formato Dolphin (un archivo ZIP o una carpeta) en un único archivo .c4tex para CubeEvil4. El paquete solo se lee, nunca se modifica; un archivo ZIP se lee tal cual, no se descomprime nada.",
  "Converte un pacchetto texture HD in formato Dolphin (un file ZIP o una cartella) in un unico file .c4tex per CubeEvil4. Il pacchetto viene solo letto, mai modificato; un file ZIP viene letto così com’è, non si estrae nulla.",
  "Converte um pacote de texturas HD no formato do Dolphin (um arquivo ZIP ou uma pasta) em um único arquivo .c4tex para o CubeEvil4. O pacote é apenas lido, nunca alterado; um arquivo ZIP é lido como está, nada é descompactado.",
  "Преобразует пакет HD-текстур в формате Dolphin (ZIP-файл или папку) в один файл .c4tex для CubeEvil4. Пакет только читается и никогда не изменяется; ZIP-файл читается как есть, ничего не распаковывается.",
  "يحوّل حزمة أنسجة عالية الدقة بصيغة Dolphin (ملف ZIP أو مجلدًا) إلى ملف واحد بصيغة c4tex لـ CubeEvil4. تُقرأ الحزمة فقط ولا تُغيَّر أبدًا؛ ويُقرأ ملف ZIP كما هو دون فك أي شيء.",
  "Dolphin फ़ॉर्मेट वाले HD टेक्सचर पैक (एक ZIP फ़ाइल या फ़ोल्डर) को CubeEvil4 के लिए एक .c4tex फ़ाइल में बदलता है। पैक को सिर्फ़ पढ़ा जाता है, कभी बदला नहीं जाता; ZIP फ़ाइल को सीधे पढ़ा जाता है, कुछ भी अनपैक नहीं होता।",
  "Mengubah paket tekstur HD berformat Dolphin (file ZIP atau folder) menjadi satu file .c4tex untuk CubeEvil4. Paket hanya dibaca, tidak pernah diubah; file ZIP dibaca apa adanya, tidak ada yang diekstrak.",
  "把 Dolphin 格式的高清纹理包（ZIP 文件或文件夹）转换成一个供 CubeEvil4 使用的 .c4tex 文件。纹理包只会被读取，不会被修改；ZIP 文件会被直接读取，不会解压任何内容。"}},
{kLegal, {
  "CubeEvil4 and this program include no textures and link to none. Use only packs you obtained yourself and may use.",
  "CubeEvil4 und dieses Programm enthalten keine Texturen und verlinken auf keine. Nutze nur Pakete, die du selbst beschafft hast und nutzen darfst.",
  "CubeEvil4 et ce programme ne contiennent aucune texture et ne renvoient vers aucune. Utilisez uniquement des packs que vous avez obtenus vous-même et que vous avez le droit d’utiliser.",
  "CubeEvil4 y este programa no incluyen texturas ni enlaces a ellas. Usa solo paquetes que hayas conseguido tú y que puedas usar.",
  "CubeEvil4 e questo programma non includono texture né link a esse. Usa solo pacchetti che ti sei procurato da solo e che puoi usare.",
  "O CubeEvil4 e este programa não incluem texturas nem links para elas. Use apenas pacotes que você mesmo obteve e pode usar.",
  "CubeEvil4 и эта программа не содержат текстур и не дают ссылок на них. Используйте только пакеты, которые вы получили сами и имеете право использовать.",
  "لا يتضمن CubeEvil4 ولا هذا البرنامج أي أنسجة ولا روابط إليها. استخدم فقط حزمًا حصلت عليها بنفسك ويحق لك استخدامها.",
  "CubeEvil4 और इस प्रोग्राम में कोई टेक्सचर नहीं है और न ही किसी का लिंक। सिर्फ़ वही पैक इस्तेमाल करें जो आपने खुद हासिल किए हैं और जिन्हें इस्तेमाल करने का आपको अधिकार है।",
  "CubeEvil4 dan program ini tidak menyertakan tekstur apa pun dan tidak memberi tautan ke mana pun. Pakai hanya paket yang kamu dapatkan sendiri dan boleh kamu pakai.",
  "CubeEvil4 和本程序不包含任何纹理，也不提供任何链接。请只使用你自己获得且有权使用的纹理包。"}},
{kDropTitle, {"Drag your texture pack here", "Zieh dein Texturpaket hierher", "Faites glisser votre pack de textures ici",
  "Arrastra aquí tu paquete de texturas", "Trascina qui il tuo pacchetto texture", "Arraste seu pacote de texturas para cá",
  "Перетащите сюда пакет текстур", "اسحب حزمة الأنسجة إلى هنا", "अपना टेक्सचर पैक यहाँ खींचकर लाएँ",
  "Seret paket teksturmu ke sini", "把纹理包拖到这里"}},
{kDropSub, {"a ZIP file or the unpacked folder", "eine ZIP-Datei oder den ausgepackten Ordner",
  "un fichier ZIP ou le dossier décompressé", "un archivo ZIP o la carpeta descomprimida", "un file ZIP o la cartella estratta",
  "um arquivo ZIP ou a pasta descompactada", "ZIP-файл или распакованную папку", "ملف ZIP أو المجلد بعد فكّه",
  "एक ZIP फ़ाइल या अनपैक किया हुआ फ़ोल्डर", "file ZIP atau folder yang sudah diekstrak", "ZIP 文件或解压后的文件夹"}},
{kDropRelease, {"Release to open the pack", "Loslassen, um das Paket zu öffnen", "Relâchez pour ouvrir le pack",
  "Suelta para abrir el paquete", "Rilascia per aprire il pacchetto", "Solte para abrir o pacote",
  "Отпустите, чтобы открыть пакет", "أفلِت لفتح الحزمة", "पैक खोलने के लिए छोड़ें", "Lepaskan untuk membuka paket",
  "松开即可打开纹理包"}},
{kChooseZip, {"Choose ZIP file…", "ZIP-Datei wählen …", "Choisir un fichier ZIP…", "Elegir archivo ZIP…", "Scegli file ZIP…",
  "Escolher arquivo ZIP…", "Выбрать ZIP-файл…", "اختيار ملف ZIP…", "ZIP फ़ाइल चुनें…", "Pilih file ZIP…", "选择 ZIP 文件…"}},
{kChooseFolder, {"Choose folder…", "Ordner wählen …", "Choisir un dossier…", "Elegir carpeta…", "Scegli cartella…",
  "Escolher pasta…", "Выбрать папку…", "اختيار مجلد…", "फ़ोल्डर चुनें…", "Pilih folder…", "选择文件夹…"}},
{kReading, {"Reading the pack …", "Paket wird gelesen …", "Lecture du pack …", "Leyendo el paquete …",
  "Lettura del pacchetto …", "Lendo o pacote …", "Чтение пакета …", "جارٍ قراءة الحزمة …", "पैक पढ़ा जा रहा है …",
  "Membaca paket …", "正在读取纹理包 …"}},
{kReadingFiles, {"Reading the pack: %1 files …", "Paket wird gelesen: %1 Dateien …", "Lecture du pack : %1 fichiers …",
  "Leyendo el paquete: %1 archivos …", "Lettura del pacchetto: %1 file …", "Lendo o pacote: %1 arquivos …",
  "Чтение пакета, найдено файлов: %1 …", "جارٍ قراءة الحزمة: %1 ملف …", "पैक पढ़ा जा रहा है: %1 फ़ाइलें …",
  "Membaca paket: %1 file …", "正在读取纹理包：%1 个文件 …"}},
{kReadingSizes, {"Reading the image sizes: %1 of %2 …", "Bildgrößen werden gelesen: %1 von %2 …",
  "Lecture des tailles d’image : %1 sur %2 …", "Leyendo los tamaños de las imágenes: %1 de %2 …",
  "Lettura delle dimensioni delle immagini: %1 di %2 …", "Lendo os tamanhos das imagens: %1 de %2 …",
  "Чтение размеров изображений: %1 из %2 …", "جارٍ قراءة أحجام الصور: %1 من %2 …",
  "इमेज के आकार पढ़े जा रहे हैं: %2 में से %1 …", "Membaca ukuran gambar: %1 dari %2 …", "正在读取图片尺寸：%1 / %2 …"}},
{kNotFound, {"The folder or ZIP file was not found: %1", "Der Ordner oder die ZIP-Datei wurde nicht gefunden: %1",
  "Dossier ou fichier ZIP introuvable : %1", "No se encontró la carpeta o el archivo ZIP: %1",
  "Cartella o file ZIP non trovato: %1", "A pasta ou o arquivo ZIP não foi encontrado: %1",
  "Папка или ZIP-файл не найдены: %1", "لم يُعثر على المجلد أو ملف ZIP: %1", "फ़ोल्डर या ZIP फ़ाइल नहीं मिली: %1",
  "Folder atau file ZIP tidak ditemukan: %1", "找不到文件夹或 ZIP 文件：%1"}},
{kZipFailed, {"The ZIP file cannot be read: %1", "Die ZIP-Datei lässt sich nicht lesen: %1",
  "Impossible de lire le fichier ZIP : %1", "No se puede leer el archivo ZIP: %1", "Impossibile leggere il file ZIP: %1",
  "Não é possível ler o arquivo ZIP: %1", "Не удаётся прочитать ZIP-файл: %1", "تعذّرت قراءة ملف ZIP: %1",
  "ZIP फ़ाइल पढ़ी नहीं जा सकती: %1", "File ZIP tidak bisa dibaca: %1", "无法读取 ZIP 文件：%1"}},
{kNoImages, {
  "No Dolphin texture images found (tex1_… files as .dds, .png or .jpg).",
  "Keine Dolphin-Texturbilder gefunden (tex1_…-Dateien als .dds, .png oder .jpg).",
  "Aucune image de texture Dolphin trouvée (fichiers tex1_… en .dds, .png ou .jpg).",
  "No se encontraron imágenes de texturas de Dolphin (archivos tex1_… en .dds, .png o .jpg).",
  "Nessuna immagine texture di Dolphin trovata (file tex1_… come .dds, .png o .jpg).",
  "Nenhuma imagem de textura do Dolphin encontrada (arquivos tex1_… em .dds, .png ou .jpg).",
  "Изображения текстур Dolphin не найдены (файлы tex1_… в .dds, .png или .jpg).",
  "لم يُعثر على صور أنسجة Dolphin (ملفات tex1_… بصيغة dds أو png أو jpg).",
  "कोई Dolphin टेक्सचर इमेज नहीं मिली (.dds, .png या .jpg वाली tex1_… फ़ाइलें)।",
  "Tidak ada gambar tekstur Dolphin yang ditemukan (file tex1_… berformat .dds, .png, atau .jpg).",
  "未找到 Dolphin 纹理图片（.dds、.png 或 .jpg 格式的 tex1_… 文件）。"}},
{kOneAtATime, {"Please drop one ZIP file or one folder.", "Bitte genau eine ZIP-Datei oder einen Ordner hineinziehen.",
  "Déposez un seul fichier ZIP ou un seul dossier.", "Suelta un solo archivo ZIP o una sola carpeta.",
  "Trascina un solo file ZIP o una sola cartella.", "Solte apenas um arquivo ZIP ou uma pasta.",
  "Перетащите один ZIP-файл или одну папку.", "أفلِت ملف ZIP واحدًا أو مجلدًا واحدًا فقط.",
  "कृपया एक ही ZIP फ़ाइल या एक ही फ़ोल्डर छोड़ें।", "Seret satu file ZIP atau satu folder saja.",
  "请只拖入一个 ZIP 文件或一个文件夹。"}},
{kNotZip, {"Not a ZIP file or a folder: %1", "Keine ZIP-Datei und kein Ordner: %1", "Ni un fichier ZIP ni un dossier : %1",
  "No es un archivo ZIP ni una carpeta: %1", "Non è un file ZIP né una cartella: %1", "Não é um arquivo ZIP nem uma pasta: %1",
  "Это не ZIP-файл и не папка: %1", "ليس ملف ZIP ولا مجلدًا: %1", "यह न ZIP फ़ाइल है, न फ़ोल्डर: %1",
  "Bukan file ZIP maupun folder: %1", "不是 ZIP 文件，也不是文件夹：%1"}},
{kUnpackFirst, {
  "A 7z or RAR file: unpack it first, then drag the folder here.",
  "Eine 7z- oder RAR-Datei: zuerst auspacken, dann den Ordner hierher ziehen.",
  "Un fichier 7z ou RAR : décompressez-le d’abord, puis faites glisser le dossier ici.",
  "Un archivo 7z o RAR: descomprímelo primero y luego arrastra aquí la carpeta.",
  "Un file 7z o RAR: estrailo prima, poi trascina qui la cartella.",
  "Um arquivo 7z ou RAR: descompacte-o primeiro e depois arraste a pasta para cá.",
  "Файл 7z или RAR: сначала распакуйте его, затем перетащите сюда папку.",
  "ملف 7z أو RAR: فكّه أولًا، ثم اسحب المجلد إلى هنا.",
  "7z या RAR फ़ाइल: पहले उसे अनपैक करें, फिर फ़ोल्डर यहाँ खींचकर लाएँ।",
  "File 7z atau RAR: ekstrak dulu, lalu seret foldernya ke sini.",
  "7z 或 RAR 文件：请先解压，再把文件夹拖到这里。"}},
{kImagesSize, {"%1 images, %2", "%1 Bilder, %2", "%1 images, %2", "%1 imágenes, %2", "%1 immagini, %2", "%1 imagens, %2",
  "изображений — %1, %2", "%1 صورة، %2", "%1 इमेज, %2", "%1 gambar, %2", "%1 张图片，%2"}},
{kKindZip, {"ZIP file", "ZIP-Datei", "fichier ZIP", "archivo ZIP", "file ZIP", "arquivo ZIP", "ZIP-файл", "ملف ZIP",
  "ZIP फ़ाइल", "file ZIP", "ZIP 文件"}},
{kKindFolder, {"folder", "Ordner", "dossier", "carpeta", "cartella", "pasta", "папка", "مجلد", "फ़ोल्डर", "folder",
  "文件夹"}},
{kLeftOut, {
  "Left out (not images of the pack, or the same image twice): %1 files",
  "Ausgelassen (keine Bilder des Pakets oder dasselbe Bild doppelt): %1 Dateien",
  "Ignorés (pas des images du pack, ou la même image en double) : %1 fichiers",
  "Omitidos (no son imágenes del paquete o la misma imagen dos veces): %1 archivos",
  "Esclusi (non sono immagini del pacchetto, o la stessa immagine due volte): %1 file",
  "Deixados de fora (não são imagens do pacote, ou a mesma imagem duas vezes): %1 arquivos",
  "Пропущено файлов (не изображения пакета или повторы): %1",
  "تم تخطي (ليست صورًا من الحزمة، أو الصورة نفسها مرتين): %1 ملف",
  "छोड़ी गईं (पैक की इमेज नहीं, या एक ही इमेज दो बार): %1 फ़ाइलें",
  "Dilewati (bukan gambar paket, atau gambar yang sama dua kali): %1 file",
  "已跳过（不是纹理包的图片，或重复的图片）：%1 个文件"}},
{kOtherPack, {"Other pack…", "Anderes Paket …", "Autre pack…", "Otro paquete…", "Altro pacchetto…", "Outro pacote…",
  "Другой пакет…", "حزمة أخرى…", "दूसरा पैक…", "Paket lain…", "其他纹理包…"}},
{kChoices, {"Choices of the pack", "Auswahl im Paket", "Choix du pack", "Opciones del paquete", "Scelte del pacchetto",
  "Opções do pacote", "Варианты пакета", "خيارات الحزمة", "पैक के विकल्प", "Pilihan dalam paket", "纹理包选项"}},
{kAsMain, {"As in the main folder", "Wie im Hauptordner", "Comme dans le dossier principal", "Como en la carpeta principal",
  "Come nella cartella principale", "Como na pasta principal", "Как в основной папке", "كما في المجلد الرئيسي",
  "मुख्य फ़ोल्डर जैसा", "Seperti di folder utama", "与主文件夹相同"}},
{kSameAsMain, {"%1 (the same as the main folder)", "%1 (wie im Hauptordner)", "%1 (identique au dossier principal)",
  "%1 (igual que la carpeta principal)", "%1 (uguale alla cartella principale)", "%1 (igual à pasta principal)",
  "%1 (как в основной папке)", "‏%1 (مثل المجلد الرئيسي)", "%1 (मुख्य फ़ोल्डर जैसा)", "%1 (sama dengan folder utama)",
  "%1（与主文件夹相同）"}},
{kAdd, {"add these images", "diese Bilder hinzufügen", "ajouter ces images", "añadir estas imágenes",
  "aggiungi queste immagini", "adicionar estas imagens", "добавить эти изображения", "إضافة هذه الصور", "ये इमेज जोड़ें",
  "tambahkan gambar-gambar ini", "添加这些图片"}},
{kSize, {"Texture size", "Texturgröße", "Taille des textures", "Tamaño de las texturas", "Dimensione delle texture",
  "Tamanho das texturas", "Размер текстур", "حجم الأنسجة", "टेक्सचर का आकार", "Ukuran tekstur", "纹理大小"}},
{kSize256, {"256: smallest, saves memory and space", "256: am kleinsten, spart Speicher und Platz",
  "256 : la plus petite, économise mémoire et espace", "256: el más pequeño, ahorra memoria y espacio",
  "256: la più piccola, risparmia memoria e spazio", "256: o menor, economiza memória e espaço",
  "256: самый маленький, экономит память и место", "256: الأصغر، يوفّر الذاكرة والمساحة",
  "256: सबसे छोटा, मेमोरी और जगह बचाता है", "256: paling kecil, hemat memori dan ruang", "256：最小，节省内存和空间"}},
{kSize512, {"512: recommended for every device", "512: empfohlen für jedes Gerät", "512 : recommandée pour tous les appareils",
  "512: recomendado para cualquier dispositivo", "512: consigliata per ogni dispositivo",
  "512: recomendado para qualquer dispositivo", "512: рекомендуется для любого устройства", "512: موصى به لكل الأجهزة",
  "512: हर डिवाइस के लिए सुझाया गया", "512: disarankan untuk semua perangkat", "512：推荐，适合所有设备"}},
{kSize1024, {"1024: sharpest, for strong devices with enough space", "1024: am schärfsten, für starke Geräte mit genug Platz",
  "1024 : la plus nette, pour les appareils puissants avec assez d’espace",
  "1024: el más nítido, para dispositivos potentes con espacio suficiente",
  "1024: la più nitida, per dispositivi potenti con spazio sufficiente",
  "1024: o mais nítido, para dispositivos potentes com espaço suficiente",
  "1024: самый чёткий, для мощных устройств с достаточным местом", "1024: الأوضح، للأجهزة القوية ذات المساحة الكافية",
  "1024: सबसे साफ़, पर्याप्त जगह वाले ताक़तवर डिवाइस के लिए",
  "1024: paling tajam, untuk perangkat kuat dengan ruang yang cukup", "1024：最清晰，适合空间充足的高性能设备"}},
{kNeverSmaller, {"No image gets smaller than the game's own.", "Kein Bild wird kleiner als das des Spiels.",
  "Aucune image ne devient plus petite que celle du jeu.", "Ninguna imagen queda más pequeña que la del juego.",
  "Nessuna immagine diventa più piccola di quella del gioco.", "Nenhuma imagem fica menor que a do jogo.",
  "Ни одно изображение не станет меньше, чем в самой игре.", "لن تصبح أي صورة أصغر من صورة اللعبة الأصلية.",
  "कोई भी इमेज गेम की अपनी इमेज से छोटी नहीं होती।", "Tidak ada gambar yang menjadi lebih kecil daripada gambar asli game.",
  "任何图片都不会小于游戏自带的尺寸。"}},
{kEstimate, {"up to %1 · about %2", "bis %1 · etwa %2", "jusqu’à %1 · environ %2", "hasta %1 · unos %2",
  "fino a %1 · circa %2", "até %1 · cerca de %2", "до %1 · около %2", "حتى %1 · نحو %2", "%1 तक · लगभग %2",
  "hingga %1 · sekitar %2", "最多 %1 · 约 %2"}},
{kEstimating, {"estimating …", "wird geschätzt …", "estimation …", "calculando …", "stima in corso …", "estimando …",
  "оценка …", "جارٍ التقدير …", "अनुमान लगाया जा रहा है …", "memperkirakan …", "正在估算 …"}},
{kSeconds, {"%1 s", "%1 s", "%1 s", "%1 s", "%1 s", "%1 s", "%1 с", "%1 ثانية", "%1 सेकंड", "%1 dtk", "%1 秒"}},
{kMinutes, {"%1 min", "%1 min", "%1 min", "%1 min", "%1 min", "%1 min", "%1 мин", "%1 دقيقة", "%1 मिनट", "%1 mnt",
  "%1 分钟"}},
{kMinSec, {"%1 min %2 s", "%1 min %2 s", "%1 min %2 s", "%1 min %2 s", "%1 min %2 s", "%1 min %2 s", "%1 мин %2 с",
  "%1 دقيقة %2 ثانية", "%1 मिनट %2 सेकंड", "%1 mnt %2 dtk", "%1 分 %2 秒"}},
{kHours, {"%1 h %2 min", "%1 h %2 min", "%1 h %2 min", "%1 h %2 min", "%1 h %2 min", "%1 h %2 min", "%1 ч %2 мин",
  "%1 ساعة %2 دقيقة", "%1 घंटे %2 मिनट", "%1 j %2 mnt", "%1 小时 %2 分钟"}},
{kStart, {"Save and start…", "Speichern und starten …", "Enregistrer et démarrer…", "Guardar e iniciar…",
  "Salva e avvia…", "Salvar e iniciar…", "Сохранить и начать…", "حفظ وبدء…", "सेव करें और शुरू करें…",
  "Simpan dan mulai…", "保存并开始…"}},
{kSaveTitle, {"Where should the texture pack be saved?", "Wo soll das Texturpaket gespeichert werden?",
  "Où enregistrer le pack de textures ?", "¿Dónde se guarda el paquete de texturas?", "Dove salvare il pacchetto texture?",
  "Onde salvar o pacote de texturas?", "Куда сохранить пакет текстур?", "أين تريد حفظ حزمة الأنسجة؟",
  "टेक्सचर पैक कहाँ सेव करना है?", "Di mana paket tekstur disimpan?", "纹理包保存到哪里？"}},
{kFileType, {"CubeEvil4 texture pack", "CubeEvil4-Texturpaket", "Pack de textures CubeEvil4",
  "Paquete de texturas de CubeEvil4", "Pacchetto texture di CubeEvil4", "Pacote de texturas do CubeEvil4",
  "Пакет текстур CubeEvil4", "حزمة أنسجة CubeEvil4", "CubeEvil4 टेक्सचर पैक", "Paket tekstur CubeEvil4", "CubeEvil4 纹理包"}},
{kNoSpace, {"Not enough free space: about %1 needed, %2 free.", "Nicht genug freier Platz: etwa %1 nötig, %2 frei.",
  "Pas assez d’espace libre : environ %1 nécessaires, %2 libres.",
  "No hay suficiente espacio libre: se necesitan unos %1, hay %2 libres.",
  "Spazio libero insufficiente: servono circa %1, liberi %2.",
  "Espaço livre insuficiente: são necessários cerca de %1, há %2 livres.",
  "Недостаточно свободного места: нужно около %1, свободно %2.", "المساحة الحرة غير كافية: يلزم نحو %1، والمتاح %2.",
  "पर्याप्त खाली जगह नहीं: लगभग %1 चाहिए, %2 खाली है।", "Ruang kosong tidak cukup: perlu sekitar %1, kosong %2.",
  "可用空间不足：约需 %1，可用 %2。"}},
{kChooseOther, {"Choose another place or a smaller size.", "Wähle einen anderen Ort oder eine kleinere Größe.",
  "Choisissez un autre emplacement ou une taille plus petite.", "Elige otro lugar o un tamaño más pequeño.",
  "Scegli un altro percorso o una dimensione più piccola.", "Escolha outro local ou um tamanho menor.",
  "Выберите другое место или меньший размер.", "اختر مكانًا آخر أو حجمًا أصغر.", "कोई दूसरी जगह या छोटा आकार चुनें।",
  "Pilih tempat lain atau ukuran yang lebih kecil.", "请选择其他位置或更小的大小。"}},
{kSmallerFits, {"Size %1 fits.", "Größe %1 passt.", "La taille %1 tient.", "El tamaño %1 cabe.", "La dimensione %1 ci sta.",
  "O tamanho %1 cabe.", "Размер %1 помещается.", "الحجم %1 يتسع.", "आकार %1 समा जाता है।", "Ukuran %1 muat.",
  "大小 %1 可以放下。"}},
{kInsidePack, {
  "The file cannot be saved inside the texture pack's folder. Please choose another folder.",
  "Die Datei kann nicht im Ordner des Texturpakets gespeichert werden. Bitte wähle einen anderen Ordner.",
  "Le fichier ne peut pas être enregistré dans le dossier du pack de textures. Choisissez un autre dossier.",
  "El archivo no se puede guardar dentro de la carpeta del paquete de texturas. Elige otra carpeta.",
  "Il file non può essere salvato nella cartella del pacchetto texture. Scegli un’altra cartella.",
  "O arquivo não pode ser salvo dentro da pasta do pacote de texturas. Escolha outra pasta.",
  "Файл нельзя сохранить в папке пакета текстур. Выберите другую папку.",
  "لا يمكن حفظ الملف داخل مجلد حزمة الأنسجة. اختر مجلدًا آخر.",
  "फ़ाइल को टेक्सचर पैक के फ़ोल्डर के अंदर सेव नहीं किया जा सकता। कृपया कोई दूसरा फ़ोल्डर चुनें।",
  "File tidak bisa disimpan di dalam folder paket tekstur. Pilih folder lain.",
  "文件不能保存在纹理包的文件夹内。请选择其他文件夹。"}},
{kCannotWrite, {"The file cannot be written there: %1", "Die Datei lässt sich dort nicht schreiben: %1",
  "Impossible d’écrire le fichier à cet endroit : %1", "No se puede escribir el archivo ahí: %1",
  "Impossibile scrivere il file in quella posizione: %1", "Não é possível gravar o arquivo ali: %1",
  "Не удаётся записать файл туда: %1", "تعذّرت كتابة الملف هناك: %1", "फ़ाइल वहाँ लिखी नहीं जा सकती: %1",
  "File tidak bisa ditulis di sana: %1", "无法在该位置写入文件：%1"}},
{kConverting, {"Converting the texture pack", "Texturpaket wird umgewandelt", "Conversion du pack de textures",
  "Convirtiendo el paquete de texturas", "Conversione del pacchetto texture", "Convertendo o pacote de texturas",
  "Преобразование пакета текстур", "جارٍ تحويل حزمة الأنسجة", "टेक्सचर पैक बदला जा रहा है", "Mengonversi paket tekstur",
  "正在转换纹理包"}},
{kStepPrepare, {"Prepare", "Vorbereiten", "Préparation", "Preparar", "Preparazione", "Preparar", "Подготовка", "التحضير",
  "तैयारी", "Persiapan", "准备"}},
{kStepPrepareSub, {
  "Enough free space (%1 free, about %2 needed), file created",
  "Genug freier Platz (%1 frei, etwa %2 nötig), Datei angelegt",
  "Assez d’espace libre (%1 libres, environ %2 nécessaires), fichier créé",
  "Espacio libre suficiente (%1 libres, se necesitan unos %2), archivo creado",
  "Spazio libero sufficiente (%1 liberi, servono circa %2), file creato",
  "Espaço livre suficiente (%1 livres, são necessários cerca de %2), arquivo criado",
  "Места достаточно (свободно %1, нужно около %2), файл создан",
  "مساحة حرة كافية (المتاح %1، ويلزم نحو %2)، وأُنشئ الملف",
  "पर्याप्त खाली जगह (%1 खाली, लगभग %2 चाहिए), फ़ाइल बनाई गई",
  "Ruang kosong cukup (%1 kosong, perlu sekitar %2), file dibuat",
  "可用空间充足（可用 %1，约需 %2），已创建文件"}},
{kStepConvert, {"Convert the images", "Bilder umwandeln", "Conversion des images", "Convertir las imágenes",
  "Conversione delle immagini", "Converter as imagens", "Преобразование изображений", "تحويل الصور", "इमेज बदलना",
  "Konversi gambar", "转换图片"}},
{kStepConvertSub, {
  "Each image is read from the pack, brought to the chosen size, compressed for the phone's graphics chip (ETC2) and written into the file.",
  "Jedes Bild wird aus dem Paket gelesen, auf die gewählte Größe gebracht, für den Grafikchip des Handys gepackt (ETC2) und in die Datei geschrieben.",
  "Chaque image est lue dans le pack, mise à la taille choisie, compressée pour la puce graphique du téléphone (ETC2) et écrite dans le fichier.",
  "Cada imagen se lee del paquete, se ajusta al tamaño elegido, se comprime para el chip gráfico del móvil (ETC2) y se escribe en el archivo.",
  "Ogni immagine viene letta dal pacchetto, portata alla dimensione scelta, compressa per il chip grafico del telefono (ETC2) e scritta nel file.",
  "Cada imagem é lida do pacote, ajustada ao tamanho escolhido, compactada para o chip gráfico do celular (ETC2) e gravada no arquivo.",
  "Каждое изображение читается из пакета, приводится к выбранному размеру, сжимается для графического чипа телефона (ETC2) и записывается в файл.",
  "تُقرأ كل صورة من الحزمة، وتُضبط على الحجم المختار، وتُضغط لمعالج رسوميات الهاتف (ETC2)، ثم تُكتب في الملف.",
  "हर इमेज पैक से पढ़ी जाती है, चुने गए आकार में लाई जाती है, फ़ोन के ग्राफ़िक्स चिप के लिए कंप्रेस की जाती है (ETC2) और फ़ाइल में लिखी जाती है।",
  "Setiap gambar dibaca dari paket, disesuaikan ke ukuran yang dipilih, dikompres untuk chip grafis ponsel (ETC2), lalu ditulis ke file.",
  "每张图片从纹理包中读取，调整到所选大小，为手机的图形芯片压缩（ETC2），然后写入文件。"}},
{kStepFinish, {"Finish the file", "Datei fertigstellen", "Finalisation du fichier", "Terminar el archivo",
  "Completamento del file", "Concluir o arquivo", "Завершение файла", "إنهاء الملف", "फ़ाइल पूरी करना",
  "Menyelesaikan file", "完成文件"}},
{kStepFinishSub, {"Index and checksums; then the report next to the file",
  "Verzeichnis und Prüfsummen, danach der Bericht neben der Datei",
  "Index et sommes de contrôle, puis le rapport à côté du fichier",
  "Índice y sumas de comprobación; después, el informe junto al archivo",
  "Indice e checksum, poi il rapporto accanto al file",
  "Índice e somas de verificação; depois, o relatório ao lado do arquivo",
  "Оглавление и контрольные суммы, затем отчёт рядом с файлом", "الفهرس ومجاميع التحقق، ثم التقرير بجانب الملف",
  "इंडेक्स और चेकसम, फिर फ़ाइल के पास रिपोर्ट", "Indeks dan checksum, lalu laporan di samping file",
  "索引和校验和，然后在文件旁生成报告"}},
{kStatConverted, {"Converted", "Umgewandelt", "Converties", "Convertidas", "Convertite", "Convertidas", "Готово",
  "حُوِّلت", "बदली गईं", "Dikonversi", "已转换"}},
{kStatWritten, {"Written", "Geschrieben", "Écrits", "Escritos", "Scritti", "Gravados", "Записано", "كُتب", "लिखा गया",
  "Ditulis", "已写入"}},
{kStatSkipped, {"Skipped", "Ausgelassen", "Ignorés", "Omitidos", "Esclusi", "Deixados de fora", "Пропущено",
  "تم تخطيها", "छोड़ी गईं", "Dilewati", "已跳过"}},
{kStatRefused, {"Refused", "Abgelehnt", "Refusées", "Rechazadas", "Rifiutate", "Recusadas", "Отклонено", "مرفوضة",
  "अस्वीकृत", "Ditolak", "已拒绝"}},
{kOfImages, {"%1 of %2 images", "%1 von %2 Bildern", "%1 sur %2 images", "%1 de %2 imágenes", "%1 di %2 immagini",
  "%1 de %2 imagens", "%1 из %2 изображений", "‏%1 من %2 صورة", "%2 में से %1 इमेज", "%1 dari %2 gambar",
  "%1 / %2 张图片"}},
{kNow, {"Now: %1", "Gerade: %1", "En cours : %1", "Ahora: %1", "Ora: %1", "Agora: %1", "Сейчас: %1", "الآن: %1", "अभी: %1",
  "Sekarang: %1", "当前：%1"}},
{kElapsed, {"Elapsed: %1", "Vergangen: %1", "Écoulé : %1", "Transcurrido: %1", "Trascorso: %1", "Decorrido: %1",
  "Прошло: %1", "المنقضي: %1", "बीता समय: %1", "Berlalu: %1", "已用时间：%1"}},
{kTimeLeft, {"About %1 left", "Noch etwa %1", "Encore environ %1", "Quedan unos %1", "Mancano circa %1",
  "Faltam cerca de %1", "Осталось около %1", "يتبقى نحو %1", "लगभग %1 बाकी", "Sekitar %1 lagi", "约剩 %1"}},
{kCancel, {"Cancel", "Abbrechen", "Annuler", "Cancelar", "Annulla", "Cancelar", "Отмена", "إلغاء", "रद्द करें", "Batal",
  "取消"}},
{kCancelling, {"Cancelling … the images in work are finished first.",
  "Wird abgebrochen … die Bilder in Arbeit werden noch fertig gemacht.",
  "Annulation … les images en cours sont d’abord terminées.",
  "Cancelando … primero se terminan las imágenes en curso.",
  "Annullamento … prima vengono completate le immagini in lavorazione.",
  "Cancelando … primeiro as imagens em andamento são concluídas.",
  "Отмена … сначала доделываются изображения в работе.", "جارٍ الإلغاء … تُستكمل أولًا الصور قيد المعالجة.",
  "रद्द किया जा रहा है … पहले जिन इमेज पर काम चल रहा है, वे पूरी होंगी।",
  "Membatalkan … gambar yang sedang dikerjakan diselesaikan dulu.", "正在取消 … 先完成正在处理的图片。"}},
{kCancelAsk, {"Stop converting?", "Umwandeln abbrechen?", "Arrêter la conversion ?", "¿Detener la conversión?",
  "Interrompere la conversione?", "Parar a conversão?", "Остановить преобразование?", "إيقاف التحويل؟", "बदलना रोकें?",
  "Hentikan konversi?", "停止转换？"}},
{kCancelAskText, {"The unfinished file will be deleted. The pack stays as it is.",
  "Die unfertige Datei wird gelöscht. Das Paket bleibt, wie es ist.",
  "Le fichier inachevé sera supprimé. Le pack reste tel quel.",
  "El archivo sin terminar se borrará. El paquete queda como está.",
  "Il file incompleto verrà eliminato. Il pacchetto resta com’è.",
  "O arquivo incompleto será excluído. O pacote continua como está.",
  "Незаконченный файл будет удалён. Пакет останется как есть.", "سيُحذف الملف غير المكتمل. تبقى الحزمة كما هي.",
  "अधूरी फ़ाइल हटा दी जाएगी। पैक जैसा है वैसा ही रहेगा।",
  "File yang belum selesai akan dihapus. Paketnya tetap seperti semula.", "未完成的文件将被删除。纹理包保持不变。"}},
{kStop, {"Stop", "Abbrechen", "Arrêter", "Detener", "Interrompi", "Parar", "Остановить", "إيقاف", "रोकें", "Hentikan",
  "停止"}},
{kKeepGoing, {"Keep converting", "Weiter umwandeln", "Continuer la conversion", "Seguir convirtiendo",
  "Continua la conversione", "Continuar convertendo", "Продолжить", "متابعة التحويل", "बदलना जारी रखें",
  "Lanjutkan konversi", "继续转换"}},
{kDone, {"Done!", "Fertig!", "Terminé !", "¡Listo!", "Fatto!", "Pronto!", "Готово!", "تم!", "हो गया!", "Selesai!", "完成！"}},
{kSaved, {"Saved: %1 (%2)", "Gespeichert: %1 (%2)", "Enregistré : %1 (%2)", "Guardado: %1 (%2)", "Salvato: %1 (%2)",
  "Salvo: %1 (%2)", "Сохранено: %1 (%2)", "حُفظ: %1 (%2)", "सेव हुआ: %1 (%2)", "Disimpan: %1 (%2)", "已保存：%1（%2）"}},
{kInFolder, {"in %1", "in %1", "dans %1", "en %1", "in %1", "em %1", "в папке %1", "في %1", "%1 में", "di %1", "位于 %1"}},
{kConvertedIn, {"%1 images converted in %2.", "%1 Bilder umgewandelt in %2.", "%1 images converties en %2.",
  "%1 imágenes convertidas en %2.", "%1 immagini convertite in %2.", "%1 imagens convertidas em %2.",
  "Преобразовано изображений: %1 за %2.", "حُوِّلت %1 صورة خلال %2.", "%1 इमेज %2 में बदली गईं।",
  "%1 gambar dikonversi dalam %2.", "已转换 %1 张图片，用时 %2。"}},
{kKinds, {"%1 opaque, %2 with cut-out transparency, %3 with soft transparency.",
  "%1 deckend, %2 mit harter Transparenz, %3 mit weicher Transparenz.",
  "%1 opaques, %2 avec transparence nette, %3 avec transparence douce.",
  "%1 opacas, %2 con transparencia recortada, %3 con transparencia suave.",
  "%1 opache, %2 con trasparenza netta, %3 con trasparenza morbida.",
  "%1 opacas, %2 com transparência recortada, %3 com transparência suave.",
  "непрозрачных: %1, с резкой прозрачностью: %2, с плавной прозрачностью: %3.",
  "%1 معتمة، و%2 بشفافية حادة، و%3 بشفافية ناعمة.",
  "%1 अपारदर्शी, %2 कटी हुई पारदर्शिता वाली, %3 नरम पारदर्शिता वाली।",
  "%1 buram, %2 dengan transparansi tegas, %3 dengan transparansi halus.",
  "不透明 %1 张，硬边透明 %2 张，柔和透明 %3 张。"}},
{kSkippedDone, {
  "Left out: %1 files (not images of the pack, or the same image twice). The report lists them.",
  "Ausgelassen: %1 Dateien (keine Bilder des Pakets oder dasselbe Bild doppelt). Der Bericht nennt sie.",
  "Ignorés : %1 fichiers (pas des images du pack, ou la même image en double). Le rapport les liste.",
  "Omitidos: %1 archivos (no son imágenes del paquete o la misma imagen dos veces). El informe los enumera.",
  "Esclusi: %1 file (non sono immagini del pacchetto, o la stessa immagine due volte). Il rapporto li elenca.",
  "Deixados de fora: %1 arquivos (não são imagens do pacote, ou a mesma imagem duas vezes). O relatório os lista.",
  "Пропущено файлов: %1 (не изображения пакета или повторы). Они перечислены в отчёте.",
  "تم تخطي %1 ملف (ليست صورًا من الحزمة، أو الصورة نفسها مرتين). يذكرها التقرير.",
  "छोड़ी गईं: %1 फ़ाइलें (पैक की इमेज नहीं, या एक ही इमेज दो बार)। रिपोर्ट में उनकी सूची है।",
  "Dilewati: %1 file (bukan gambar paket, atau gambar yang sama dua kali). Laporan mencantumkannya.",
  "已跳过 %1 个文件（不是纹理包的图片，或重复的图片）。报告中列出了它们。"}},
{kRefusedDone, {
  "Refused: %1 images. The game keeps its own textures for them:",
  "Abgelehnt: %1 Bilder. Für sie bleibt die Textur des Spiels:",
  "Refusées : %1 images. Le jeu garde ses propres textures pour elles :",
  "Rechazadas: %1 imágenes. El juego conserva sus propias texturas para ellas:",
  "Rifiutate: %1 immagini. Per queste il gioco mantiene le proprie texture:",
  "Recusadas: %1 imagens. O jogo mantém as próprias texturas para elas:",
  "Отклонено изображений: %1. Для них игра оставит свои текстуры:",
  "رُفضت %1 صورة. تحتفظ اللعبة بأنسجتها الأصلية لها:",
  "अस्वीकृत: %1 इमेज। इनके लिए गेम अपने टेक्सचर रखेगा:",
  "Ditolak: %1 gambar. Game tetap memakai tekstur aslinya untuk gambar ini:",
  "已拒绝 %1 张图片。游戏会为它们保留自带的纹理："}},
{kMore, {"… and %1 more (see the report).", "… und %1 weitere (siehe Bericht).", "… et %1 de plus (voir le rapport).",
  "… y %1 más (ver el informe).", "… e altre %1 (vedi il rapporto).", "… e mais %1 (veja o relatório).",
  "… и ещё %1 (см. отчёт).", "… و%1 أخرى (انظر التقرير).", "… और %1 (रिपोर्ट देखें)।", "… dan %1 lagi (lihat laporan).",
  "… 还有 %1 个（见报告）。"}},
{kNext, {
  "Next: copy the file to your device. In CubeEvil4: app menu (SELECT) → Cheats & Mods → Mods → Import texture pack.",
  "Als Nächstes: Kopiere die Datei auf dein Gerät. In CubeEvil4: App-Menü (SELECT) → Cheats & Mods → Mods → Texturpaket importieren.",
  "Ensuite : copiez le fichier sur votre appareil. Dans CubeEvil4 : menu de l’app (SELECT) → Triches et mods → Mods → Importer un pack de textures.",
  "Después: copia el archivo a tu dispositivo. En CubeEvil4: menú de la app (SELECT) → Trucos y mods → Mods → Importar paquete de texturas.",
  "Poi: copia il file sul tuo dispositivo. In CubeEvil4: menu dell’app (SELECT) → Trucchi e mod → Mod → Importa pacchetto texture.",
  "Depois: copie o arquivo para o seu dispositivo. No CubeEvil4: menu do app (SELECT) → Trapaças e mods → Mods → Importar pacote de texturas.",
  "Дальше: скопируйте файл на устройство. В CubeEvil4: меню приложения (SELECT) → Читы и моды → Моды → Импорт пакета текстур.",
  "التالي: انسخ الملف إلى جهازك. في CubeEvil4: قائمة التطبيق (SELECT) ← الغش والتعديلات ← التعديلات ← استيراد حزمة الأنسجة.",
  "आगे: फ़ाइल को अपने डिवाइस पर कॉपी करें। CubeEvil4 में: ऐप मेन्यू (SELECT) → चीट्स और मॉड्स → मॉड्स → टेक्सचर पैक इम्पोर्ट करें।",
  "Berikutnya: salin file ke perangkatmu. Di CubeEvil4: menu aplikasi (SELECT) → Cheat & Mod → Mod → Impor paket tekstur.",
  "下一步：把文件复制到你的设备上。在 CubeEvil4 中：应用菜单（SELECT）→ 作弊与模组 → 模组 → 导入纹理包。"}},
{kOpenFolder, {"Open folder", "Ordner öffnen", "Ouvrir le dossier", "Abrir carpeta", "Apri cartella", "Abrir pasta",
  "Открыть папку", "فتح المجلد", "फ़ोल्डर खोलें", "Buka folder", "打开文件夹"}},
{kShowReport, {"Show report", "Bericht zeigen", "Afficher le rapport", "Ver informe", "Mostra rapporto",
  "Mostrar relatório", "Показать отчёт", "عرض التقرير", "रिपोर्ट दिखाएँ", "Tampilkan laporan", "显示报告"}},
{kClose, {"Close", "Schließen", "Fermer", "Cerrar", "Chiudi", "Fechar", "Закрыть", "إغلاق", "बंद करें", "Tutup", "关闭"}},
{kBack, {"Back", "Zurück", "Retour", "Atrás", "Indietro", "Voltar", "Назад", "رجوع", "वापस", "Kembali", "返回"}},
{kFailed, {"Converting failed", "Umwandeln fehlgeschlagen", "La conversion a échoué", "La conversión falló",
  "Conversione non riuscita", "A conversão falhou", "Ошибка преобразования", "فشل التحويل", "बदलना विफल रहा",
  "Konversi gagal", "转换失败"}},
{kReason, {"Reason: %1", "Grund: %1", "Raison : %1", "Motivo: %1", "Motivo: %1", "Motivo: %1", "Причина: %1",
  "السبب: %1", "कारण: %1", "Penyebab: %1", "原因：%1"}},
{kNoFileLeft, {"No file was left behind.", "Es ist keine Datei zurückgeblieben.", "Aucun fichier n’a été laissé.",
  "No ha quedado ningún archivo.", "Non è rimasto nessun file.", "Nenhum arquivo ficou para trás.", "Файлов не осталось.",
  "لم يتبقَّ أي ملف.", "कोई फ़ाइल पीछे नहीं छूटी।", "Tidak ada file yang tertinggal.", "没有留下任何文件。"}},
{kCancelled, {"Cancelled", "Abgebrochen", "Annulé", "Cancelado", "Annullato", "Cancelado", "Отменено", "أُلغي",
  "रद्द किया गया", "Dibatalkan", "已取消"}},
{kDiskFull, {"the disk is full", "der Datenträger ist voll", "le disque est plein", "el disco está lleno",
  "il disco è pieno", "o disco está cheio", "диск заполнен", "القرص ممتلئ", "डिस्क भर गई है", "disk penuh", "磁盘已满"}},
{kNoImageConverted, {"no image could be converted", "kein Bild ließ sich umwandeln",
  "aucune image n’a pu être convertie", "no se pudo convertir ninguna imagen", "nessuna immagine è stata convertita",
  "nenhuma imagem pôde ser convertida", "не удалось преобразовать ни одного изображения", "تعذّر تحويل أي صورة",
  "कोई भी इमेज बदली नहीं जा सकी", "tidak ada gambar yang bisa dikonversi", "没有任何图片能被转换"}},
{kWriteFailed, {"writing the file failed", "das Schreiben der Datei ist fehlgeschlagen", "l’écriture du fichier a échoué",
  "no se pudo escribir el archivo", "la scrittura del file non è riuscita", "a gravação do arquivo falhou",
  "не удалось записать файл", "فشلت كتابة الملف", "फ़ाइल लिखना विफल रहा", "gagal menulis file", "写入文件失败"}},
{kConsoleHint, {
  "This is the window version. For the command line, use CubeEvil4-TexturePacker-Console.exe (same options).",
  "Das ist die Version mit Fenster. Für die Kommandozeile nimm CubeEvil4-TexturePacker-Console.exe (gleiche Optionen).",
  "Ceci est la version avec fenêtre. Pour la ligne de commande, utilisez CubeEvil4-TexturePacker-Console.exe (mêmes options).",
  "Esta es la versión con ventana. Para la línea de comandos, usa CubeEvil4-TexturePacker-Console.exe (mismas opciones).",
  "Questa è la versione con finestra. Per la riga di comando usa CubeEvil4-TexturePacker-Console.exe (stesse opzioni).",
  "Esta é a versão com janela. Para a linha de comando, use CubeEvil4-TexturePacker-Console.exe (mesmas opções).",
  "Это версия с окном. Для командной строки используйте CubeEvil4-TexturePacker-Console.exe (те же параметры).",
  "هذا هو الإصدار ذو النافذة. لسطر الأوامر استخدم CubeEvil4-TexturePacker-Console.exe (الخيارات نفسها).",
  "यह विंडो वाला संस्करण है। कमांड लाइन के लिए CubeEvil4-TexturePacker-Console.exe इस्तेमाल करें (वही विकल्प)।",
  "Ini versi dengan jendela. Untuk baris perintah, pakai CubeEvil4-TexturePacker-Console.exe (opsi yang sama).",
  "这是窗口版本。命令行请使用 CubeEvil4-TexturePacker-Console.exe（选项相同）。"}},
{kPercent, {"%1%%", "%1 %%", "%1 %%", "%1 %%", "%1%%", "%1%%", "%1 %%", "%1%%", "%1%%", "%1%%", "%1%%"}},
{kMB, {"%1 MB", "%1 MB", "%1 Mo", "%1 MB", "%1 MB", "%1 MB", "%1 МБ", "%1 ميغابايت", "%1 MB", "%1 MB", "%1 MB"}},
{kGB, {"%1 GB", "%1 GB", "%1 Go", "%1 GB", "%1 GB", "%1 GB", "%1 ГБ", "%1 غيغابايت", "%1 GB", "%1 GB", "%1 GB"}},
};
// clang-format on

static_assert(sizeof(kRows) / sizeof(kRows[0]) == kIdCount, "one row per text");

const char *const kCodes[kLangCount] = {"en", "de", "fr", "es", "it", "pt", "ru", "ar", "hi", "id", "zh"};
// thousands: "5,916" / "5.916" / "5 916" (narrow no-break space) / "5 916" (no-break space)
const char *const kGroup[kLangCount] = {",", ".", " ", ".", ".", ".", " ", ",", ",", ".", ","};
const char kDecimal[kLangCount] = {'.', ',', ',', ',', ',', ',', ',', '.', '.', ',', '.'};

}  // namespace

const char *Text(Lang lang, Id id) {
  if (lang < 0 || lang >= kLangCount) lang = kEn;
  if (id < 0 || id >= kIdCount) return "";
  const char *t = kRows[id].t[lang];
  return t && *t ? t : kRows[id].t[kEn];
}

Lang FromCode(const std::string &code) {
  std::string c;
  for (char ch : code) {
    if (ch == '-' || ch == '_') break;
    c.push_back(static_cast<char>(ch >= 'A' && ch <= 'Z' ? ch - 'A' + 'a' : ch));
  }
  if (c == "in") c = "id";
  for (int i = 0; i < kLangCount; i++)
    if (c == kCodes[i]) return static_cast<Lang>(i);
  return kEn;
}

const char *Code(Lang lang) { return kCodes[lang >= 0 && lang < kLangCount ? lang : kEn]; }

bool RightToLeft(Lang lang) { return lang == kAr; }

std::string Format(const std::string &pattern, std::initializer_list<std::string> args) {
  std::string out;
  out.reserve(pattern.size() + 32);
  for (std::size_t i = 0; i < pattern.size(); i++) {
    const char c = pattern[i];
    if (c == '%' && i + 1 < pattern.size()) {
      const char n = pattern[i + 1];
      if (n == '%') {
        out.push_back('%');
        i++;
        continue;
      }
      if (n >= '1' && n <= '9') {
        const std::size_t k = static_cast<std::size_t>(n - '1');
        if (k < args.size()) out += *(args.begin() + k);
        i++;
        continue;
      }
    }
    out.push_back(c);
  }
  return out;
}

std::string Count(Lang lang, std::uint64_t v) {
  std::string s = std::to_string(v);
  const std::string group = kGroup[lang >= 0 && lang < kLangCount ? lang : kEn];
  for (int i = static_cast<int>(s.size()) - 3; i > 0; i -= 3) s.insert(static_cast<std::size_t>(i), group);
  return s;
}

std::string Bytes(Lang lang, std::uint64_t b) {
  char buf[64];
  if (b >= 1000ull * 1000 * 1000) {
    std::snprintf(buf, sizeof(buf), b >= 10ull * 1000 * 1000 * 1000 ? "%.1f" : "%.2f", b / 1e9);
    std::string s = buf;
    for (char &c : s)
      if (c == '.') c = kDecimal[lang >= 0 && lang < kLangCount ? lang : kEn];
    return Format(Text(lang, kGB), {s});
  }
  std::snprintf(buf, sizeof(buf), "%.0f", b / 1e6);
  return Format(Text(lang, kMB), {buf});
}

std::string Duration(Lang lang, double seconds) {
  const long s = static_cast<long>(seconds + 0.5);
  if (s >= 3600) return Format(Text(lang, kHours), {std::to_string(s / 3600), std::to_string((s / 60) % 60)});
  if (s >= 60) return Format(Text(lang, kMinSec), {std::to_string(s / 60), std::to_string(s % 60)});
  return Format(Text(lang, kSeconds), {std::to_string(s)});
}

std::string About(Lang lang, double seconds) {
  if (seconds < 55) {
    const long s = std::max(10L, static_cast<long>(std::lround(seconds / 10.0)) * 10);
    return Format(Text(lang, kSeconds), {std::to_string(s)});
  }
  long minutes = std::max(1L, static_cast<long>(std::lround(seconds / 60.0)));
  if (minutes >= 10) minutes = static_cast<long>(std::lround(minutes / 5.0)) * 5;
  if (minutes >= 60) return Format(Text(lang, kHours), {std::to_string(minutes / 60), std::to_string(minutes % 60)});
  return Format(Text(lang, kMinutes), {std::to_string(minutes)});
}

}  // namespace guitext
