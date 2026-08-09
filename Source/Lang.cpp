#include "Lang.h"
#include "ProjectStore.h"

// ============================================================================
//  The table. One row per string: the key as it is written in the source, then
//  Spanish, English, Chinese and Arabic. An empty cell means "the key already
//  says it in this language".
//
//  Order follows the app, not the alphabet, so a row can be checked against
//  the screen it appears on.
// ============================================================================
namespace
{
    struct Row { const char* key; const char* es; const char* en; const char* zh; const char* ar; };

    const Row kTable[] =
    {
        // --- The face ------------------------------------------------------
        { "PADS",           "",         "PADS",       "音垫",       "باد" },
        { "SEC",            "",         "SEQ",        "音序",       "تتابع" },
        { "SONG",           "CANCION",  "SONG",       "歌曲",       "أغنية" },
        { "MIX",            "MEZCLA",   "MIX",        "混音",       "مزج" },
        { "SET",            "AJUSTES",  "SETUP",      "设置",       "إعداد" },
        { "LOAD",           "CARGAR",   "LOAD",       "载入",       "تحميل" },
        { "REC",            "",         "REC",        "录音",       "تسجيل" },
        { "REC ON",         "",         "REC ON",     "录音中", "يسجّل" },
        { "PLAY",           "",         "PLAY",       "播放",       "تشغيل" },
        { "STOP",           "",         "STOP",       "停止",       "إيقاف" },
        { "DESHACER",       "",         "UNDO",       "撤销",       "تراجع" },
        { "REHACER",        "",         "REDO",       "重做",       "إعادة" },
        { "EFECTOS",        "",         "EFFECTS",    "效果",       "مؤثرات" },
        { "CTRL %1",        "",         "CTRL %1",    "旋钮 %1",    "مقبض %1" },
        { "ESPECTRO",       "",         "SPECTRUM",   "频谱",       "الطيف" },
        { "CTRL -> %1",     "",         "CTRL -> %1", "旋钮 -> %1", "المقابض ← %1" },
        { "%1 OFF - manten pulsado para ajustar sin apagar", "",
          "%1 OFF - hold to tune it without switching it off",
          "%1 已关闭 - 长按可在不关闭的情况下调节",
          "%1 متوقف - اضغط مطولًا للضبط دون الإيقاف" },
        { "TEST",           "",         "TEST",       "测试",       "اختبار" },
        { "MEDIR",          "",         "MEASURE",    "测量",       "قياس" },
        { "elige uno de la lista", "",   "pick one from the list", "从列表中选一个", "اختر واحدًا من القائمة" },
        { "OUT",            "",         "OUT",        "输出",       "خرج" },

        // --- PADS sheet ----------------------------------------------------
        { "SONIDO",         "",         "SOUND",      "声音",       "الصوت" },
        { "RECORTE",        "",         "TRIM",       "裁剪",       "القص" },
        { "EL PAD",         "",         "THE PAD",    "此音垫", "الباد" },
        { "PITCH",          "",         "PITCH",      "音高",       "الطبقة" },
        { "FINO",           "",         "FINE",       "微调",       "دقيق" },
        { "VOLUME",         "VOLUMEN",  "VOLUME",     "音量",       "المستوى" },
        { "PAN",            "",         "PAN",        "声像",       "الاتزان" },
        { "ATTACK",         "ATAQUE",   "ATTACK",     "起音",       "الهجوم" },
        { "RELEASE",        "CAIDA",    "RELEASE",    "释音",       "التلاشي" },
        { "CHOKE",          "",         "CHOKE",      "组切",       "الخنق" },
        { "MODO",           "",         "MODE",       "模式",       "الوضع" },
        { "CINTA",          "",         "TAPE",       "磁带",       "شريط" },
        { "TONO",           "",         "TONE",       "变调",       "نغمة" },
        { "START",          "INICIO",   "START",      "起点",       "البداية" },
        { "END",            "FIN",      "END",        "终点",       "النهاية" },
        { "REV|reverso",    "REV",      "REV",        "倒放",       "عكس" },
        { "LOOP",           "BUCLE",    "LOOP",       "循环",       "تكرار" },
        { "AUTOCUT",        "",         "AUTOCUT",    "自切",       "قطع تلقائي" },
        { "AUTO CHOP",      "",         "AUTO CHOP",  "自动切片", "تقطيع تلقائي" },
        { "GRABAR MIC",     "",         "MIC REC",    "麦克风录音", "تسجيل ميكروفون" },
        { "PARAR",          "",         "STOP",       "停止",       "إيقاف" },
        { "OIR",            "",         "HEAR",       "试听",       "استمع" },
        { "PAD %1",         "",         "PAD %1",     "音垫 %1",    "باد %1" },
        { "TRIM",           "",         "TRIM",       "裁剪",       "قص" },
        { "MONO",           "",         "MONO",       "单声道", "أحادي" },
        { "STEREO",         "",         "STEREO",     "立体声", "ستيريو" },
        { "TAP A PAD TO LOAD ITS WAVEFORM", "TOCA UN PAD PARA VER SU ONDA",
                                        "TAP A PAD TO LOAD ITS WAVEFORM",
                                        "点一个音垫看它的波形",
                                        "المس بادًا لرؤية موجته" },

        // --- Zati colours --------------------------------------------------
        { "ROJO",           "",         "RED",        "红",         "أحمر" },
        { "NARANJA",        "",         "ORANGE",     "橙",         "برتقالي" },
        { "AMBAR",          "",         "AMBER",      "琥珀",       "كهرماني" },
        { "VERDE",          "",         "GREEN",      "绿",         "أخضر" },
        { "TURQUESA",       "",         "TURQUOISE",  "青",         "فيروزي" },
        { "AZUL",           "",         "BLUE",       "蓝",         "أزرق" },
        { "VIOLETA",        "",         "VIOLET",     "紫",         "بنفسجي" },
        { "MAGENTA",        "",         "MAGENTA",    "洋红",       "أرجواني" },
        { "off",            "",         "off",        "关",         "مغلق" },

        // --- SEC sheet -----------------------------------------------------
        { "PASOS",          "",         "STEPS",      "步数",       "خطوات" },
        { "PATRON",         "",         "PATTERN",    "乐句",       "نمط" },
        { "LARGO",          "",         "LENGTH",     "长度",       "الطول" },
        { "CADENA",         "",         "CHAIN",      "链接",       "سلسلة" },
        { "NOTA DEL PASO",  "",         "STEP NOTE",  "该步音符", "نغمة الخطوة" },
        { "GOLPE",          "",         "HIT",        "力度",       "الضربة" },
        { "SWING",          "",         "SWING",      "摇摆",       "سوينغ" },
        { "recto",          "",         "straight",   "平直",       "مستقيم" },
        { "COMPAS",         "",         "BAR",        "小节",       "مازورة" },
        { "TEMPO",          "",         "TEMPO",      "速度",       "الإيقاع" },
        { "QUITAR CADENA",  "",         "CLEAR CHAIN","清除链接", "مسح السلسلة" },
        { "VACIAR",         "",         "CLEAR",      "清空",       "تفريغ" },
        { "%1 pasos",       "",         "%1 steps",   "%1 步",          "%1 خطوة" },
        { "sin cadena - repite P%1", "sin cadena · repite P%1", "no chain · P%1 repeats",
                                        "无链接 · 重复 P%1",
                                        "بلا سلسلة · تكرار P%1" },
        { "cadena: ",       "",         "chain: ",    "链接：",  "سلسلة: " },
        { "suena P%1",      "",         "playing P%1","正在播放 P%1", "يعمل P%1" },

        // --- AUTO CHOP sheet -----------------------------------------------
        { "TROZOS",         "",         "PIECES",     "片数",       "عدد القطع" },
        { "RESPETAR PADS CON SONIDO", "", "KEEP PADS THAT HAVE SOUND",
                                        "保留已有声音的音垫",
                                        "لا تمسّ الباد المشغول" },
        { "CORTAR",         "",         "CHOP",       "切片",       "قطّع" },
        { "CORTAR EN %1",   "",         "CHOP INTO %1","切成 %1 片", "قطّع إلى %1" },
        { "Parte este sample en trozos iguales y los reparte por los pads. El pad de origen se queda con el primero.",
          "", "Cuts this sample into equal pieces and spreads them over the pads. The source pad keeps the first one.",
          "把这个采样切成等分的几段，分配到各个音垫。原音垫保留第一段。",
          "يقطّع هذه العينة إلى أجزاء متساوية ويوزّعها على الوسائد، ويحتفظ الباد الأصلي بالجزء الأول." },
        { "va a pads: %1",  "",         "goes to pads: %1", "分配到音垫：%1", "إلى الباد: %1" },
        { "no pisa ningun pad con sonido", "", "does not overwrite any pad that has sound",
                                        "不会覆盖任何有声音的音垫",
                                        "لن يطمس أي باد فيه صوت" },
        { "PISA %1 pads con sonido", "", "OVERWRITES %1 pads that have sound",
                                        "会覆盖 %1 个有声音的音垫",
                                        "سيطمس %1 باد فيها صوت" },
        { "PISA 1 pad con sonido", "", "OVERWRITES 1 pad that has sound",
                                        "会覆盖 1 个有声音的音垫",
                                        "سيطمس بادًا فيه صوت" },
        { "solo caben %1 sin pisar nada", "", "only %1 fit without overwriting anything",
                                        "不覆盖任何东西的话只能放 %1 段",
                                        "يتسع %1 فقط دون طمس شيء" },
        { "Este pad no tiene sonido que cortar.", "", "This pad has no sound to chop.",
                                        "这个音垫没有可切的声音。",
                                        "لا يوجد صوت في هذا الباد لتقطيعه." },
        { "Cortado en %1 trozos - DESHACER para volver", "", "Chopped into %1 - UNDO to go back",
                                        "已切成 %1 段 — 可以撤销",
                                        "قُطّع إلى %1 — تراجع للعودة" },
        { "Cortado en %1 (no cabian %2) - DESHACER para volver", "",
          "Chopped into %1 (%2 did not fit) - UNDO to go back",
          "已切成 %1 段（%2 段放不下）— 可以撤销",
          "قُطّع إلى %1 (لم تتسع %2) — تراجع للعودة" },

        // --- Projects ------------------------------------------------------
        { "PROYECTOS",      "",         "PROJECTS",   "工程",       "المشاريع" },
        { "GUARDAR",        "",         "SAVE",       "保存",       "حفظ" },
        { "ABRIR",          "",         "OPEN",       "打开",       "فتح" },
        { "NUEVO",          "",         "NEW",        "新建",       "جديد" },
        { "BORRAR",         "",         "DELETE",     "删除",       "حذف" },
        { "EXPORTAR",       "",         "EXPORT",     "导出",       "تصدير" },
        { "CONTROL",        "",         "CONTROL",    "控制",       "تحكم" },
        { "SIN GUARDAR",    "",         "UNSAVED",    "未保存",     "غير محفوظ" },
        { "BORRA TODO?",    "",         "ERASE ALL?", "全部清空？", "مسح الكل؟" },
        { "BORRAR %1?",     "",         "DELETE %1?", "删除 %1？", "حذف %1؟" },
        { "sin proyectos - GUARDAR crea el primero", "", "no projects - SAVE makes the first one",
                                        "没有工程 — 保存即可新建",
                                        "لا توجد مشاريع — احفظ لإنشاء الأول" },
        { "abierto: %1",    "",         "open: %1",   "已打开：%1", "مفتوح: %1" },
        { "Elige un proyecto de la lista", "", "Pick a project from the list",
                                        "请从列表中选一个工程",
                                        "اختر مشروعًا من القائمة" },
        { "Guardado \"%1\"  [%2 pads]", "", "Saved \"%1\"  [%2 pads]",
                                        "已保存“%1” [%2 个音垫]",
                                        "حُفظ \"%1\"  [%2 باد]" },
        { "Guardado con fallos: %1 pads no se escribieron", "", "Saved with errors: %1 pads were not written",
                                        "保存时出错：%1 个音垫未写入",
                                        "حُفظ مع أخطاء: لم تُكتب %1 باد" },
        { "Abierto \"%1\"  [%2 pads]", "", "Opened \"%1\"  [%2 pads]",
                                        "已打开“%1” [%2 个音垫]",
                                        "فُتح \"%1\"  [%2 باد]" },
        { "Abierto \"%1\"  [%2 pads, %3 sin audio]", "", "Opened \"%1\"  [%2 pads, %3 with no audio]",
                                        "已打开“%1” [%2 个音垫，%3 个无声音]",
                                        "فُتح \"%1\"  [%2 باد، %3 بلا صوت]" },
        { "Borrado \"%1\"", "",         "Deleted \"%1\"", "已删除“%1”", "حُذف \"%1\"" },
        { "Proyecto nuevo", "",         "New project", "新工程", "مشروع جديد" },
        { "No encuentro el proyecto \"%1\"", "", "Cannot find the project \"%1\"",
                                        "找不到工程“%1”",
                                        "لم أجد المشروع \"%1\"" },
        { "Proyecto ilegible: %1", "",  "Unreadable project: %1", "工程无法读取：%1", "مشروع غير مقروء: %1" },
        { "NO se pudo guardar en %1", "", "COULD NOT save into %1",
                                        "无法保存到 %1",
                                        "تعذّر الحفظ في %1" },
        { "CARPETA",        "",         "FOLDER",     "文件夹",     "المجلد" },
        { "NOMBRE",         "",         "NAME",       "名称",       "الاسم" },
        { "Sobrescribir \"%1\"?", "",  "Overwrite \"%1\"?", "覆盖“%1”？", "استبدال \"%1\"؟" },
        { "Escribe un nombre para el proyecto", "", "Type a name for the project",
                                        "给工程取个名字",
                                        "اكتب اسمًا للمشروع" },
        { "Sesion recuperada - %1", "", "Session restored - %1", "会话已恢复 — %1", "استُعيدت الجلسة — %1" },
        { "Sesion recuperada  [%1 pads]", "", "Session restored  [%1 pads]", "会话已恢复 [%1 个音垫]", "استُعيدت الجلسة [%1 باد]" },
        { "Sesion recuperada  [1 pad]", "", "Session restored  [1 pad]", "会话已恢复 [1 个音垫]", "استُعيدت الجلسة [باد واحد]" },

        // --- Browser -------------------------------------------------------
        { "CARGAR EN PAD %1", "",       "LOAD INTO PAD %1", "载入音垫 %1", "تحميل في باد %1" },
        { "Muestra para el pad %1", "", "Sample for pad %1",
                                        "音垫 %1 的采样",
                                        "عينة للباد %1" },
        { "elige una muestra  -  wav / aiff / flac / ogg / mp3", "",
          "pick a sample  -  wav / aiff / flac / ogg / mp3",
          "选一个采样  -  wav / aiff / flac / ogg / mp3",
          "اختر عينة  -  wav / aiff / flac / ogg / mp3" },
        { "CARGAR",         "",         "LOAD",       "载入",       "تحميل" },
        { "SISTEMA",        "",         "SYSTEM",     "系统",       "النظام" },
        { "Cargando pad %1...", "",     "Loading pad %1...", "正在载入音垫 %1…", "جارٍ تحميل باد %1…" },
        { "Pad %1 cargado  [%2]", "",   "Pad %1 loaded  [%2]", "音垫 %1 已载入 [%2]", "تم تحميل باد %1 [%2]" },
        { "Fallo al cargar: %1", "",    "Could not load: %1", "载入失败：%1", "تعذّر التحميل: %1" },
        { "No se pudo leer: %1", "",    "Could not read: %1", "无法读取：%1", "تعذّرت القراءة: %1" },
        { "Sin permiso de audio: no puedo leer tus carpetas de muestras", "",
          "No audio permission: I cannot read your sample folders",
          "没有音频权限：无法读取你的采样文件夹",
          "لا يوجد إذن للصوت: لا أستطيع قراءة مجلدات العينات" },

        // --- Status line ---------------------------------------------------
        { "Toca un pad para sonar", "", "Tap a pad to play it", "点音垫即可发声", "المس بادًا ليصدر صوتًا" },
        { "LOAD armado - toca un pad para cargarlo", "", "LOAD armed - tap a pad to load it",
                                        "已开启载入 — 点一个音垫来载入",
                                        "وضع التحميل — المس بادًا لتحميله" },
        { "Pad vacio - pulsa LOAD y toca el pad para cargarlo", "",
          "Empty pad - press LOAD and tap the pad to load it",
          "空音垫 — 先按载入，再点这个音垫",
          "باد فارغ — اضغط تحميل ثم المس الباد" },
        { "REC: toca pads para grabarlos en el patron", "", "REC: tap pads to write them into the pattern",
                                        "录音：点音垫就会写进乐句",
                                        "تسجيل: المس الباد لتُكتب في النمط" },
        { "REC apagado",    "",         "REC off",    "录音已关", "التسجيل متوقف" },
        { "Grabado pad %1 en paso %2 (P%3)", "", "Pad %1 written into step %2 (P%3)",
                                        "音垫 %1 已写入第 %2 步（P%3）",
                                        "كُتب باد %1 في الخطوة %2 (P%3)" },
        { "Grabando pad %1  %2s / %3s", "", "Recording pad %1  %2s / %3s",
                                        "正在录音垫 %1  %2s / %3s",
                                        "جارٍ تسجيل باد %1  %2s / %3s" },
        { "Grabando pad %1  %2s", "",   "Recording pad %1  %2s", "正在录音垫 %1  %2s", "جارٍ تسجيل باد %1  %2s" },
        { "Grabado en el pad %1  [%2s]", "", "Recorded into pad %1  [%2s]", "已录入音垫 %1 [%2s]", "سُجّل في باد %1 [%2s]" },
        { "No se grabo nada", "",       "Nothing was recorded", "什么都没录到", "لم يُسجّل شيء" },
        { "sin permiso de microfono", "", "no microphone permission", "没有麦克风权限", "لا يوجد إذن للميكروفون" },
        { "Sin permiso de microfono: no puedo grabar", "", "No microphone permission: I cannot record",
                                        "没有麦克风权限：无法录音",
                                        "لا يوجد إذن للميكروفون: لا أستطيع التسجيل" },
        { "Audio cedido a otra app", "", "Audio handed to another app", "音频已交给其他应用", "أُعطي الصوت لتطبيق آخر" },
        { "En pausa: otra app tiene el audio", "", "Paused: another app has the audio",
                                        "已暂停：其他应用占用音频",
                                        "موقف: تطبيق آخر يستخدم الصوت" },
        { "Deshecho: %1",   "",         "Undone: %1", "已撤销：%1", "تم التراجع: %1" },
        { "Rehecho: %1",    "",         "Redone: %1", "已重做：%1", "تمت الإعادة: %1" },
        { "auto chop",      "",         "auto chop",  "自动切片", "تقطيع تلقائي" },
        { "vaciar patron",  "",         "clear pattern", "清空乐句", "تفريغ النمط" },
        { "Tono de prueba", "",         "Test tone",  "测试音",  "نغمة اختبار" },
        { "Pad %1 -> zati %2 %3", "",   "Pad %1 -> zati %2 %3", "音垫 %1 → zati %2 %3", "باد %1 ← zati %2 %3" },

        // --- MIX / RACK / SONG ---------------------------------------------
        { "RACK",           "",         "RACK",       "机架",       "الرف" },
        { "SIN SOLO",       "",         "NO SOLO",    "取消独奏", "إلغاء الإفراد" },
        { "SOLO ACTIVO",    "",         "SOLO ACTIVE","独奏中",  "إفراد فعّال" },
        { "cuanto de este pad entra en cada efecto", "", "how much of this pad goes into each effect",
                                        "这个音垫进入每个效果的量",
                                        "مقدار ما يدخل من هذا الباد إلى كل مؤثر" },
        { "SONIDO|cancion", "SONIDO",   "ONE SHOT",   "单音",       "لقطة" },
        { "CANCION",        "",         "SONG MODE",  "歌曲模式", "وضع الأغنية" },
        { "toca un compas para poner el patron", "", "tap a bar to place the pattern",
                                        "点一个小节放置乐句",
                                        "المس مازورة لوضع النمط" },
        { "toca un compas para soltar el sonido", "", "tap a bar to drop the sound",
                                        "点一个小节放置声音",
                                        "المس مازورة لإسقاط الصوت" },
        { "toca un bloque para borrarlo", "", "tap a block to erase it", "点块即可删除", "المس كتلة لمسحها" },
        { "PLAY toca la cancion", "",   "PLAY plays the song", "播放键播放整首歌", "زر التشغيل يشغّل الأغنية" },
        { "PLAY toca el patron / la cadena", "", "PLAY plays the pattern / the chain",
                                        "播放键播放乐句或链接",
                                        "زر التشغيل يشغّل النمط أو السلسلة" },

        // --- Export --------------------------------------------------------
        { "MASTER",         "",         "MASTER",     "总输出", "الماستر" },
        { "PISTAS",         "",         "STEMS",      "分轨",       "المسارات" },
        { "CANCELAR",       "",         "CANCEL",     "取消",       "إلغاء" },
        { "renderizando...", "",        "rendering...", "正在渲染…", "جارٍ التصدير…" },
        { "escribiendo %1", "",         "writing %1", "正在写入 %1", "جارٍ كتابة %1" },
        { "listo: %1",      "",         "done: %1",   "完成：%1", "تمّ: %1" },
        { "destino",        "",         "goes to",    "保存到",  "إلى" },


        // --- Export panel --------------------------------------------------
        { "fuente",         "",         "source",     "来源",       "المصدر" },
        { "duracion",       "",         "length",     "时长",       "المدة" },
        { "pistas",         "",         "tracks",     "轨道",       "المسارات" },
        { "%1 compases",    "",         "%1 bars",    "%1 小节",    "%1 مازورة" },
        { "vacio",          "",         "empty",      "空",         "فارغ" },
        { "%1 pads con muestra", "",    "%1 pads with a sample", "%1 个音垫有采样", "%1 باد فيها عينة" },
        { "CADENA (%1 patrones)", "",   "CHAIN (%1 patterns)", "链接（%1 个乐句）", "سلسلة (%1 نمط)" },
        { "PATRON P%1",     "",         "PATTERN P%1","乐句 P%1",   "نمط P%1" },
        { "no hay nada grabado en %1", "", "there is nothing recorded in %1",
                                        "%1 里没有录到任何东西",
                                        "لا يوجد شيء مسجّل في %1" },

        // --- Accessible names (TalkBack) -----------------------------------
        { "Tono",           "",         "Pitch",      "音高",       "الطبقة" },
        { "Afinado",        "",         "Fine tune",  "微调",       "ضبط دقيق" },
        { "Volumen",        "",         "Volume",     "音量",       "المستوى" },
        { "Paneo",          "",         "Pan",        "声像",       "الاتزان" },
        { "Ataque",         "",         "Attack",     "起音",       "الهجوم" },
        { "Caida",          "",         "Release",    "释音",       "التلاشي" },
        { "Inicio",         "",         "Start",      "起点",       "البداية" },
        { "Fin",            "",         "End",        "终点",       "النهاية" },
        { "Choke",          "",         "Choke",      "组切",       "الخنق" },
        { "Tempo|nombre",   "Tempo",    "Tempo",      "速度",       "الإيقاع" },
        { "Patron",         "",         "Pattern",    "乐句",       "نمط" },
        { "Nota",           "",         "Note",       "音符",       "نغمة" },
        { "Compases",       "",         "Bars",       "小节数",     "عدد المازورات" },
        { "Control %1",     "",         "Control %1", "控制 %1",    "تحكم %1" },
        { "Silencio %1",    "",         "Mute %1",    "静音 %1",    "كتم %1" },
        { "Solo %1",        "",         "Solo %1",    "独奏 %1",    "إفراد %1" },
        { "Volumen canal %1", "",       "Volume channel %1", "通道 %1 音量", "مستوى القناة %1" },
        { "Paneo canal %1", "",         "Pan channel %1", "通道 %1 声像", "اتزان القناة %1" },
        { "semitonos",      "",         "semitones",  "半音",       "أنصاف نغمات" },
        { "centesimas",     "",         "cents",      "音分",       "سنتات" },
        { "del pad",        "",         "of the pad", "音垫的",     "للباد" },
        { "milisegundos",   "",         "milliseconds", "毫秒",     "مللي ثانية" },
        { "recorte",        "",         "trim",       "裁剪",       "القص" },
        { "grupo de corte", "",         "choke group","组切编组",   "مجموعة الخنق" },
        { "pulsos por minuto", "",      "beats per minute", "每分钟拍数", "نبضة في الدقيقة" },
        { "del secuenciador", "",       "of the sequencer", "音序器的", "للمتتابع" },
        { "del paso",       "",         "of the step","该步的",     "للخطوة" },
        { "del patron",     "",         "of the pattern", "乐句的", "للنمط" },
        { "del efecto",     "",         "of the effect", "效果的",  "للمؤثر" },
        { "del mezclador",  "",         "of the mixer","混音器的",  "للخلاط" },

        // --- Audio panel ---------------------------------------------------
        { "AUDIO",          "",         "AUDIO",      "音频",       "الصوت" },
        { "BUFER",          "",         "BUFFER",     "缓冲",       "المخزن" },
        { "RELOJ",          "",         "CLOCK",      "时钟",       "الساعة" },
        { "IDIOMA",         "",         "LANGUAGE",   "语言",       "اللغة" },
        { "EQUIPO",         "",         "DEVICE",     "设备",       "الجهاز" },
        { "muestras",       "",         "samples",    "采样",       "عينة" },
        { "%1 nucleos",     "",         "%1 cores",   "%1 核",      "%1 أنوية" },
        { "%1 voces",       "",         "%1 voices",  "%1 复音",    "%1 صوتًا" },
        { "basica",         "",         "entry",      "入门",       "أساسي" },
        { "media",          "",         "mid",        "中端",       "متوسط" },
        { "alta",           "",         "high",       "高端",       "عالٍ" },
        { "muy alta",       "",         "flagship",   "旗舰",       "رائد" },
        { "sin dispositivo de audio", "", "no audio device", "没有音频设备", "لا يوجد جهاز صوت" },
        { "ruta",           "",         "path",       "通路",       "المسار" },
        { "reloj",          "",         "clock",      "时钟",       "الساعة" },
        { "bufer",          "",         "buffer",     "缓冲",       "المخزن" },
        { "salida",         "",         "output",     "输出",       "الخرج" },
        { "medido",         "",         "measured",   "实测",       "المقاس" },
        { "mmap",           "",         "mmap",       "mmap",       "mmap" },
        { "via",            "",         "via",        "通过",       "عبر" },
        { "excl",           "",         "excl",       "独占",       "حصري" },
        { "rafaga, el minimo", "",      "burst, the minimum", "突发，最小值", "الدفعة، الحد الأدنى" },
        { "rafaga %1",      "",         "burst %1",   "突发 %1",    "دفعة %1" },
        { "rapida",         "",         "fast",       "快",         "سريع" },
        { "aceptable",      "",         "acceptable", "可接受",  "مقبول" },
        { "LENTA",          "",         "SLOW",       "慢",         "بطيء" },
        { "de esos, %1 ms son el bufer", "", "of that, %1 ms is the buffer",
                                        "其中 %1 毫秒是缓冲",
                                        "منها %1 مللي ثانية للمخزن" },
        { "baja el bufer",  "",         "lower the buffer", "把缓冲调小", "قلّل المخزن" },
        { "el resto es el mezclador de Android, sin MMAP en este movil", "",
          "the rest is Android's mixer, no MMAP on this phone",
          "其余是 Android 的混音器，这台手机没有 MMAP",
          "الباقي هو خلاط أندرويد، بلا MMAP في هذا الهاتف" },
        { "el resto es el telefono, no lo pone nadie mas bajo", "",
          "the rest is the phone, and nobody gets it lower",
          "其余是手机本身，谁也降不下来",
          "الباقي من الهاتف نفسه، ولا أحد يخفضه أكثر" },
        { "no soportado",   "",         "not supported", "不支持", "غير مدعوم" },
        { "disponible",     "",         "available",  "可用",       "متاح" },
        { "forzado",        "",         "forced",     "强制",       "مفروض" },
        { "desconocido",    "",         "unknown",    "未知",       "غير معروف" },
        { "sin respuesta",  "",         "no answer",  "无响应",  "بلا جواب" },
        { "compartida %1 - ni en %2", "", "shared %1 - not even at %2",
                                        "共享 %1 — 即使 %2 也不行",
                                        "مشترك %1 — ولا حتى %2" },
        { "MEZCLADOR",      "",         "MIXER",      "混音器",  "الخلاط" },
        { "MMAP",           "",         "MMAP",       "MMAP",       "MMAP" },
        { "EXCLUSIVA",      "",         "EXCLUSIVE",  "独占",       "حصري" },
        { "escuchando...",  "",         "listening...", "正在监听…", "جارٍ الإنصات…" },
        { "%1 ms ida y vuelta", "",     "%1 ms round trip", "%1 毫秒往返", "%1 مللي ذهابًا وإيابًا" },
        { "con micro abierto: salida %1 + entrada %2 ms%3", "",
          "with the mic open: output %1 + input %2 ms%3",
          "麦克风开启时：输出 %1 + 输入 %2 毫秒%3",
          "والميكروفون مفتوح: خرج %1 + دخل %2 مللي%3" },
        { "(por resta)",    "",         "(by subtraction)", "（相减得出）", "(بالطرح)" },
        { "Tocando solo sales %1 ms", "", "Playing only, you are %1 ms out",
                                        "只演奏时是 %1 毫秒",
                                        "عند العزف فقط: %1 مللي" },
        { "midiendo...",    "",         "measuring...", "正在测量…", "جارٍ القياس…" },
        { "MEDIR emite un click y lo escucha con el micro", "",
          "MEASURE plays a click and listens for it with the mic",
          "测量会发出一个声音并用麦克风听回来",
          "القياس يطلق نقرة ويلتقطها بالميكروفون" },
        { "no oi el click - sube el volumen y no tapes el micro", "",
          "I did not hear the click - turn the volume up and do not cover the mic",
          "没听到声音 — 请调高音量并不要挡住麦克风",
          "لم أسمع النقرة — ارفع الصوت ولا تغطّ الميكروفون" },
    };

    juce::HashMap<juce::String, juce::String>& table()
    {
        static juce::HashMap<juce::String, juce::String> t;
        return t;
    }

    Lang::Id currentId = Lang::es;

    juce::File preferenceFile()
    {
        return ProjectStore::home().getChildFile ("idioma.txt");
    }
}

Lang::Id Lang::current() noexcept { return currentId; }

const char* Lang::code (Id id)
{
    switch (id)
    {
        case en: return "en";
        case zh: return "zh";
        case ar: return "ar";
        case es:
        case numLanguages:
        default: return "es";
    }
}

const char* Lang::nativeName (Id id)
{
    switch (id)
    {
        case en: return "ENGLISH";
        case zh: return "中文";
        case ar: return "العربية";
        case es:
        case numLanguages:
        default: return "ESPANOL";
    }
}

void Lang::set (Id newLanguage)
{
    currentId = (newLanguage >= 0 && newLanguage < numLanguages) ? newLanguage : es;

    auto& t = table();
    t.clear();

    for (const auto& row : kTable)
    {
        const char* value = nullptr;

        switch (currentId)
        {
            case es: value = row.es; break;
            case en: value = row.en; break;
            case zh: value = row.zh; break;
            case ar: value = row.ar; break;
            default: break;
        }

        //  An empty cell means the key is already right in this language, and
        //  storing it would only cost a lookup that returns what we had.
        if (value != nullptr && *value != 0)
            t.set (juce::String::fromUTF8 (row.key), juce::String::fromUTF8 (value));
    }
}

Lang::Id Lang::detect()
{
    const auto sys = juce::SystemStats::getUserLanguage().toLowerCase();

    if (sys.startsWith ("en")) return en;
    if (sys.startsWith ("zh")) return zh;
    if (sys.startsWith ("ar")) return ar;

    return es;
}

void Lang::loadPreference()
{
    const auto f = preferenceFile();
    const auto saved = f.existsAsFile() ? f.loadFileAsString().trim() : juce::String();

    for (int i = 0; i < numLanguages; ++i)
        if (saved == code ((Id) i))
        {
            set ((Id) i);
            return;
        }

    set (detect());
}

void Lang::savePreference()
{
    preferenceFile().replaceWithText (code (currentId));
}

// ============================================================================
//  Lookup. The bar-separated context exists so two different things can be
//  called REV; when there is no translation it is cut off, because the reader
//  wants the word and not the note we left ourselves.
// ============================================================================
juce::String Lang::ltr (const juce::String& latinRun)
{
    if (! isRightToLeft (current()))
        return latinRun;

    static const auto open  = juce::String::fromUTF8 ("\xe2\x81\xa6");   // U+2066 LRI
    static const auto close = juce::String::fromUTF8 ("\xe2\x81\xa9");   // U+2069 PDI
    return open + latinRun + close;
}

juce::String T (const juce::String& key)
{
    const auto& t = table();

    if (t.contains (key))
        return t[key];

    return key.upToFirstOccurrenceOf ("|", false, false);
}

juce::String T (const juce::String& key, const juce::String& a1)
{
    return T (key).replace ("%1", a1);
}

juce::String T (const juce::String& key, const juce::String& a1, const juce::String& a2)
{
    return T (key).replace ("%1", a1).replace ("%2", a2);
}

juce::String T (const juce::String& key, const juce::String& a1,
                const juce::String& a2, const juce::String& a3)
{
    return T (key).replace ("%1", a1).replace ("%2", a2).replace ("%3", a3);
}
