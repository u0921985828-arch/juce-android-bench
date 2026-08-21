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
        { "Pads sensibles a la fuerza del golpe", "",
          "Pads respond to how hard you hit them",
          "打击力度感应已启用",
          "الوسادات تستجيب لقوة الضغط" },
        { "Sesion recuperada  [%1 pads, %2 sin audio]", "",
          "Session recovered  [%1 pads, %2 without audio]",
          "会话已恢复  [%1 个音垫，%2 个缺少音频]",
          "استُعيدت الجلسة  [%1 باد، %2 بلا صوت]" },
        // --- GESTOS (la tercera pagina de AJUSTES) --------------------------
        { "GESTOS",             "",  "GESTURES",  "手势",   "إيماءات" },
        { "CARCASA",            "",  "CHASSIS",   "外壳",   "الهيكل" },
        { "MANTEN UN PAD",      "",  "HOLD A PAD",
                                     "长按音垫", "اضغط بادًا مطولًا" },
        { "abre sus ajustes sin sonar", "",
                                     "opens its settings, silently",
                                     "打开它的设置，不发声",
                                     "يفتح إعداداته دون صوت" },
        { "MANTEN UN EFECTO",   "",  "HOLD AN EFFECT",
                                     "长按效果", "اضغط مؤثرًا مطولًا" },
        { "coge los mandos sin apagarlo", "",
                                     "takes the knobs without switching it off",
                                     "接管旋钮而不关闭它",
                                     "يأخذ المقابض دون إيقافه" },
        { "MANTEN CARGAR",      "",  "HOLD LOAD",
                                     "长按载入", "اضغط تحميل مطولًا" },
        { "abre la biblioteca en el pad elegido", "",
                                     "opens the library on the selected pad",
                                     "在所选音垫上打开素材库",
                                     "يفتح المكتبة على الباد المحدد" },
        { "MANTEN PLAY",        "",  "HOLD PLAY",
                                     "长按播放", "اضغط تشغيل مطولًا" },
        { "para y corta todo lo que suene", "",
                                     "stops and cuts everything still sounding",
                                     "停止并切断所有仍在发声的内容",
                                     "يوقف ويقطع كل ما يزال يُسمع" },
        { "ARRASTRA LA PANTALLA", "", "DRAG THE SCREEN",
                                     "拖动屏幕", "اسحب الشاشة" },
        { "cambia de patron",   "",  "changes pattern bank",
                                     "切换乐句", "يغيّر النمط" },
        { "GOLPEA ARRIBA O ABAJO", "", "STRIKE HIGH OR LOW",
                                     "打上或打下", "اضرب أعلى أو أسفل" },
        { "toca mas fuerte o mas flojo", "",
                                     "plays harder or softer",
                                     "力度更强或更弱",
                                     "يعزف أقوى أو أخف" },
        { "Todo parado",        "",  "Everything stopped",
                                     "全部停止", "توقف كل شيء" },

        { "Bajando un momento por un aviso del sistema", "",
          "Turning down for a system alert",
          "系统提示音，暂时降低音量",
          "خفض الصوت مؤقتًا لتنبيه من النظام" },
        { "%1 OFF - manten pulsado para ajustar sin apagar", "",
          "%1 OFF - hold to tune it without switching it off",
          "%1 已关闭 - 长按可在不关闭的情况下调节",
          "%1 متوقف - اضغط مطولًا للضبط دون الإيقاف" },
        { "TEST",           "",         "TEST",       "测试",       "اختبار" },
        { "MEDIR",          "",         "MEASURE",    "测量",       "قياس" },
        { "CUADRAR",        "",         "QUANT",      "对齐",       "ضبط" },   // abreviado: QUANTISE no cabe en la tapa del Fold
        { "Los pads suenan cuadrados al paso", "", "pads now land on the step",
                                        "音垫将对齐到步", "الوسادات تنضبط على الخطوة" },
        { "Los pads suenan cuando los tocas", "", "pads sound the moment you hit them",
                                        "音垫在触碰瞬间发声", "الوسادات تصدر عند اللمس" },
        { "elige uno de la lista", "",   "pick one from the list", "从列表中选一个", "اختر واحدًا من القائمة" },
        { "OUT",            "",         "OUT",        "输出",       "خرج" },

        // --- PADS sheet ----------------------------------------------------
        { "SONIDO",         "",         "SOUND",      "声音",       "الصوت" },
        { "RECORTE",        "",         "TRIM",       "裁剪",       "القص" },
        { "EL PAD",         "",         "THE PAD",    "此音垫", "الباد" },
        { "PITCH",          "",         "PITCH",      "音高",       "الطبقة" },
        { "FINO",           "",         "FINE",       "微调",       "دقيق" },
        //  Era VOLUME y ahora es GANANCIA, porque el mando dejo de ser una
        //  proporcion de 0 a 100 y paso a ser decibelios con +12 de margen.
        { "GANANCIA",       "",         "GAIN",       "增益",       "الكسب" },
        { "NORMALIZAR",     "",         "NORMALIZE",  "标准化",     "توحيد" },
        { "El pad %1 no tiene sonido", "", "pad %1 has no sound",
                                        "音垫 %1 没有声音", "الباد %1 بلا صوت" },
        { "El recorte esta en silencio", "", "the trimmed part is silent",
                                        "所裁剪的部分是静音的", "الجزء المقصوص صامت" },
        { "Pico a -0.3 dBFS con %1", "", "peak at -0.3 dBFS with %1",
                                        "峰值 -0.3 dBFS，增益 %1", "الذروة عند -0.3 ديسيبل بـ %1" },
        { "GANANCIA al tope: %1", "",   "GAIN maxed out: %1",
                                        "增益已到上限：%1", "الكسب في أقصاه: %1" },
        { "Ganancia",       "",         "Gain",       "增益",       "الكسب" },
        { "decibelios",     "",         "decibels",   "分贝",       "ديسيبل" },
        { "PAN",            "",         "PAN",        "声像",       "الاتزان" },
        { "ATTACK",         "ATAQUE",   "ATTACK",     "起音",       "الهجوم" },
        { "RELEASE",        "CAIDA",    "RELEASE",    "释音",       "التلاشي" },
        { "CHOKE",          "",         "CHOKE",      "组切",       "الخنق" },
        { "CORTE|filtro",   "",         "CUTOFF",     "截止",       "القطع" },
        { "COMO",           "",         "HOW",        "方式",       "الطريقة" },
        { "IGUALES",        "",         "EVEN",       "均分",       "متساوٍ" },
        { "GOLPES",         "",         "HITS",       "打点",       "الضربات" },
        { "TROZOS (como mucho)", "",    "SLICES (max)", "片段（最多）", "مقاطع (بحد أقصى)" },
        { "%1 golpes encontrados", "",  "%1 hits found", "找到 %1 个打点", "عُثر على %1 ضربة" },
        { "no hay golpes que separar aqui", "",
          "no separate hits in here", "这里没有可分离的打点",
          "لا توجد ضربات منفصلة هنا" },
        { "Cortado en %1 golpes - DESHACER para volver", "",
          "Chopped into %1 hits - UNDO to go back", "已按 %1 个打点切分 - 撤销可还原",
          "قُطّع إلى %1 ضربة - تراجع للعودة" },
        { "RESON",          "",         "RESO",       "共振",       "الرنين" },
        { "ABIERTO",        "",         "OPEN",       "开",         "مفتوح" },
        { "SUAVE IN",       "",         "FADE IN",    "淡入",       "تلاشٍ داخل" },
        { "SUAVE OUT",      "",         "FADE OUT",   "淡出",       "تلاشٍ خارج" },
        { "SECO",           "",         "HARD",       "硬切",       "حاد" },
        { "DOBLAR",         "",         "DOUBLE",     "加倍",       "مضاعفة" },
        { "Cancion doblada a %1 compases", "",
          "Song doubled to %1 bars", "歌曲已加倍为 %1 小节",
          "تضاعفت الأغنية إلى %1 مازورة" },
        { "La cancion ya no cabe doblada", "",
          "The song will not fit doubled", "歌曲加倍后放不下",
          "الأغنية لا تتسع بعد المضاعفة" },
        //  Las tres herramientas del patron, en la pagina PASO. Ver patLeftBtn.
        { "DESPLAZAR",      "",         "NUDGE",      "位移",       "إزاحة" },
        { "ATRAS",          "",         "BACK",       "前移",       "للخلف" },
        { "ADELANTE",       "",         "FWD",        "后移",       "للأمام" },
        { "Patron un paso a la derecha", "",
          "Pattern nudged one step right", "图案右移一步",
          "أُزيح النمط خطوة إلى اليمين" },
        { "Patron un paso a la izquierda", "",
          "Pattern nudged one step left", "图案左移一步",
          "أُزيح النمط خطوة إلى اليسار" },
        { "Patron doblado a %1 pasos", "",
          "Pattern doubled to %1 steps", "图案已加倍为 %1 步",
          "تضاعف النمط إلى %1 خطوة" },
        { "El patron ya no cabe doblado", "",
          "The pattern will not fit doubled", "图案加倍后放不下",
          "النمط لا يتسع بعد المضاعفة" },
        //  Las herramientas de arreglo de la ficha CANCION. Ver songCursor.
        { "INSERTAR",       "",         "INSERT",     "插入",       "إدراج" },
        //  ACORTAR y no CORTAR: la fila CORTAR ya existe y dice CHOP, que es
        //  trocear una muestra. Un bloque no se trocea, se acorta.
        { "ACORTAR",        "",         "SHORTEN",    "缩短",       "تقصير" },
        //  El piano roll. Ver PianoRoll.h.
        { "PIANO",          "",         "PIANO",      "钢琴",       "بيانو" },
        //  Las dos que faltaban del editor de patrones. Ver humanizePattern.
        { "HUMANIZAR",      "",         "HUMANIZE",   "人性化",     "أنسنة" },
        { "SEGUIR",         "",         "FOLLOW",     "跟随",       "تتبع" },
        { "Humanizados %1 golpes", "",
          "Humanised %1 hits", "已人性化 %1 个打点",
          "أُنسنت %1 ضربة" },
        { "La vista sigue al compas que suena", "",
          "The view follows the playing bar", "视图跟随播放的小节",
          "العرض يتبع المازورة التي تُعزف" },
        { "La vista se queda donde la dejes", "",
          "The view stays where you leave it", "视图保持不动",
          "يبقى العرض حيث تتركه" },
        { "OCTAVA",         "",         "OCTAVE",     "八度",       "أوكتاف" },
        { "PAD",            "",         "PAD",        "音垫",       "باد" },
        { "La vez anterior se cerro en: %1", "",
          "Last time it closed at: %1", "上次在此处关闭：%1",
          "أُغلق آخر مرة عند: %1" },
        { "toca el teclado para oir, la rejilla para escribir", "",
          "tap the keys to hear, the grid to write",
          "点击琴键试听，点击网格书写",
          "المس المفاتيح للسماع والشبكة للكتابة" },
        { "Un paso admite %1 notas", "",
          "A step holds %1 notes", "每步最多 %1 个音",
          "الخطوة تتسع لـ %1 نغمات" },
        { "ALARGAR",        "",         "EXTEND",     "延长",       "إطالة" },
        { "Bloque de %1 compases", "",
          "Block is %1 bars", "块为 %1 小节",
          "الكتلة %1 مازورة" },
        { "No hay ningun bloque en este compas", "",
          "There is no block on this bar", "此小节没有块",
          "لا توجد كتلة في هذه المازورة" },
        { "El bloque no puede medir eso", "",
          "The block cannot be that long", "块无法达到该长度",
          "لا يمكن للكتلة أن تبلغ ذلك" },
        { "El compas siguiente ya esta ocupado", "",
          "The next bar is already taken", "下一小节已被占用",
          "المازورة التالية مشغولة" },
        //  El rotulo del deshacer, que no es ninguna tapa: la barra de estado
        //  dice "Deshecho: MOVER" y esa palabra tambien se lee.
        { "MOVER",          "",         "MOVE",       "移动",       "نقل" },
        { "Compas movido al %1", "",
          "Bar moved to %1", "小节已移到 %1",
          "نُقلت المازورة إلى %1" },
        { "El compas ya esta en el borde", "",
          "The bar is already at the edge", "小节已在边缘",
          "المازورة عند الحافة بالفعل" },
        { "QUITAR",         "",         "REMOVE",     "删除",       "حذف" },
        { "Compas metido en %1", "",
          "Bar inserted at %1", "已在 %1 插入小节",
          "أُدرجت مازورة عند %1" },
        { "Compas %1 quitado", "",
          "Bar %1 removed", "已删除第 %1 小节",
          "حُذفت المازورة %1" },
        { "Compas %1 copiado", "",
          "Bar %1 copied", "已复制第 %1 小节",
          "نُسخت المازورة %1" },
        { "Pegado en el compas %1", "",
          "Pasted at bar %1", "已粘贴到第 %1 小节",
          "لُصق عند المازورة %1" },
        { "No hay ningun compas copiado", "",
          "No bar has been copied", "尚未复制任何小节",
          "لم تُنسخ أي مازورة" },
        { "La cancion ya esta en su maximo", "",
          "The song is already at its maximum", "歌曲已达最大长度",
          "الأغنية بلغت حدها الأقصى" },
        { "Una cancion no puede quedarse sin compases", "",
          "A song cannot be left with no bars", "歌曲不能没有小节",
          "لا يمكن أن تبقى الأغنية بلا مازورات" },
        { "Bucle en los compases %1 a %2", "",
          "Looping bars %1 to %2", "循环第 %1 至 %2 小节",
          "تكرار المازورات %1 إلى %2" },
        { "Bucle quitado", "",
          "Loop cleared", "已取消循环",
          "أُلغي التكرار" },
        { "Carril %1 en silencio", "",
          "Lane %1 muted", "轨道 %1 已静音",
          "المسار %1 صامت" },
        { "Carril %1 suena", "",
          "Lane %1 unmuted", "轨道 %1 已取消静音",
          "المسار %1 يعمل" },
        { "Audio entrecortado - buffer a %1 muestras", "",
          "Audio glitching - buffer raised to %1 samples", "音频断续 - 缓冲区提高到 %1 采样",
          "صوت متقطع - رفع المخزن إلى %1 عينة" },
        { "MODO",           "",         "MODE",       "模式",       "الوضع" },
        { "CINTA",          "",         "TAPE",       "磁带",       "شريط" },
        { "TONO",           "",         "TONE",       "变调",       "نغمة" },
        { "START",          "INICIO",   "START",      "起点",       "البداية" },
        { "END",            "FIN",      "END",        "终点",       "النهاية" },
        { "REV|reverso",    "REV",      "REV",        "倒放",       "عكس" },
        { "LOOP",           "BUCLE",    "LOOP",       "循环",       "تكرار" },
        { "AUTOCUT",        "",         "AUTOCUT",    "自切",       "قطع تلقائي" },
        { "BOMBEO",         "",         "DUCK",       "闪避",       "خفض" },
        { "El pad %1 hace bombear al resto", "", "pad %1 now ducks everything else",
                                        "音垫 %1 将压低其他声音", "الباد %1 يخفض الباقي" },
        { "Bombeo apagado", "",         "duck off",   "闪避已关闭", "الخفض متوقف" },
        //  Los rotulos de la segunda pagina de la ficha del pad.
        { "ENVIOS",         "",         "SENDS",      "发送",       "إرسال" },
        { "CORTE",          "",         "CUT",        "切断",       "القطع" },
        { "FUENTE",         "",         "SOURCE",     "来源",       "المصدر" },
        { "Envio a %1",     "",         "Send to %1", "发送到 %1",  "إرسال إلى %1" },
        { "QUITAR RUIDO",   "",         "DENOISE",    "降噪",       "تنقية" },
        { "La muestra es demasiado corta para medir el ruido", "",
          "the sample is too short to measure the noise",
          "样本太短，无法测量噪声",
          "العينة أقصر من أن يُقاس ضجيجها" },
        { "Ruido fuera - el pico baja %1 dB", "", "noise gone - peak down %1 dB",
                                        "噪声已去除 - 峰值降低 %1 dB", "أُزيل الضجيج - انخفضت الذروة %1 ديسيبل" },
        //  Los partes de la exportacion. Se escriben en un hilo de fondo y se
        //  leen en la barra de estado: eran castellano en las cuatro
        //  compilaciones, y lang.py no podia verlos porque no pasaban por T().
        { "Nada que exportar: no hay pasos con sonido", "",
          "nothing to export: no steps with sound",
          "无可导出：没有带声音的步", "لا شيء للتصدير: لا خطوات بصوت" },
        { "No se pudo crear %1", "",     "could not create %1",
          "无法创建 %1", "تعذر إنشاء %1" },
        { "Sin memoria para %1 s", "",   "not enough memory for %1 s",
          "内存不足，无法容纳 %1 秒", "لا ذاكرة تكفي لـ %1 ثانية" },
        { "No se pudo escribir %1", "",  "could not write %1",
          "无法写入 %1", "تعذرت الكتابة في %1" },
        { "Cancelado",      "",         "cancelled",  "已取消",     "أُلغي" },
        { "%1 archivo, %2 s", "",       "%1 file, %2 s",
          "%1 个文件，%2 秒", "%1 ملف، %2 ثانية" },
        { "%1 archivos, %2 s", "",      "%1 files, %2 s",
          "%1 个文件，%2 秒", "%1 ملفات، %2 ثانية" },
        { "(bajado %1 dB para no saturar)", "", "(lowered %1 dB to avoid clipping)",
          "（降低 %1 dB 以避免削波）", "(خُفض %1 ديسيبل لتفادي القص)" },
        { "Quitando ruido...", "",      "removing noise...",
                                        "正在降噪...", "جارٍ إزالة الضجيج..." },
        { "El pad cambio mientras se limpiaba", "", "the pad changed while it was being cleaned",
                                        "清理期间该音垫已更改", "تغيّر الباد أثناء التنقية" },
        // --- El manual, dentro de la app -----------------------------------
        { "MANUAL", "",
          "MANUAL",
          "手册", "الدليل" },
        { "lo que hay que saber, en ocho capitulos", "",
          "what you need to know, in eight chapters",
          "八章要点", "ما يلزم معرفته، في ثمانية فصول" },
        { "EMPEZAR", "",
          "GETTING STARTED",
          "开始", "البداية" },
        { "CARGAR y luego un pad abre la biblioteca en ese pad", "",
          "LOAD then a pad opens the library on that pad",
          "先按载入再按音垫，即在该音垫上打开素材库", "اضغط تحميل ثم بادًا لفتح المكتبة على ذلك الباد" },
        { "Un toque toca; una pulsacion larga configura", "",
          "a tap plays; a long press configures",
          "轻触发声，长按设置", "النقر يعزف، والضغط المطول يضبط" },
        { "Manten un pad para abrir su ficha sin que suene", "",
          "hold a pad to open its settings without a sound",
          "长按音垫可打开其设置而不发声", "اضغط بادًا مطولًا لفتح إعداداته دون صوت" },
        { "PADS Y BANCOS", "",
          "PADS AND BANKS",
          "音垫与音库", "الوسادات والبنوك" },
        { "Cuatro bancos de dieciseis pads: los otros 48 siguen sonando", "",
          "four banks of sixteen: the other 48 keep sounding",
          "四个音库各十六个音垫，其余 48 个继续发声", "أربعة بنوك من ستة عشر: الباقي 48 يستمر في الصوت" },
        { "Arrastra la rejilla para cambiar de banco", "",
          "drag the grid to change bank",
          "拖动网格可切换音库", "اسحب الشبكة لتغيير البنك" },
        { "El color de un pad lo acompana en la onda y en la rejilla", "",
          "a pad's colour follows it into the wave and the grid",
          "音垫的颜色会带到波形与网格中", "لون الباد يرافقه في الموجة والشبكة" },
        { "CARGAR KIT reparte una carpeta entera por los pads", "",
          "LOAD KIT spreads a whole folder across the pads",
          "载入套件会把整个文件夹分配到各音垫", "تحميل الطقم يوزّع مجلدًا كاملًا على الوسادات" },
        { "Arrastra las asas para mover el inicio y el fin", "",
          "drag the handles to move start and end",
          "拖动手柄可移动起点和终点", "اسحب المقبضين لتحريك البداية والنهاية" },
        { "Toca la onda en medio y suena desde ahi", "",
          "tap the wave in open water and it plays from there",
          "轻触波形中部即从该处播放", "انقر وسط الموجة ليعزف من هناك" },
        { "Pellizca para ampliar hasta x64; arrastra para mover la vista", "",
          "pinch to zoom up to x64; drag to move the view",
          "双指捏合可放大至 x64，拖动可移动视图", "اقرص للتكبير حتى x64، واسحب لتحريك العرض" },
        { "El zoom se centra en el recorte, no en donde estas mirando", "",
          "zoom centres on the trim, not on where you were looking",
          "缩放以裁剪区为中心，而非当前视野", "التكبير يتمركز على القص لا على موضع نظرك" },
        { "SONIDO DEL PAD", "",
          "THE PAD'S SOUND",
          "音垫的声音", "صوت الباد" },
        { "CINTA afina cambiando la duracion; TONO la mantiene", "",
          "TAPE tunes by changing the length; TONE keeps it",
          "磁带模式改变时长来变调，变调模式保持时长", "الشريط يغيّر المدة عند التنغيم، والنغمة تحافظ عليها" },
        { "La ganancia va en decibelios, de -60 a +12", "",
          "gain is in decibels, -60 to +12",
          "增益以分贝计，从 -60 到 +12", "الكسب بالديسيبل، من -60 إلى +12" },
        { "NORMALIZAR deja el pico del recorte en -0.3 dBFS", "",
          "NORMALIZE puts the trim's peak at -0.3 dBFS",
          "标准化把裁剪区峰值置于 -0.3 dBFS", "التوحيد يضع ذروة القص عند -0.3 ديسيبل" },
        { "QUITAR RUIDO saca el siseo sin comerse lo que suena", "",
          "DENOISE removes hiss without eating the sound",
          "降噪去除嘶声而不损伤声音", "إزالة الضجيج تزيل الهسهسة دون التهام الصوت" },
        { "Doble toque en un mando: vuelve a su valor de siempre", "",
          "double-tap a knob: back to its default",
          "双击旋钮即回到默认值", "انقر المقبض مرتين ليعود إلى قيمته الأصلية" },
        { "SECUENCIADOR", "",
          "SEQUENCER",
          "音序器", "المتتابع" },
        { "Toca una celda para poner un paso; arrastra para pintar varios", "",
          "tap a cell for one step; drag to paint several",
          "轻触格子放置一步，拖动可连续绘制", "انقر خلية لوضع خطوة، واسحب لرسم عدة خطوات" },
        { "La pestana PASO dice que hace: nota, golpe y repeticion", "",
          "the STEP page says what it does: note, hit and roll",
          "“步”页说明其作用：音符、力度与滚奏", "صفحة الخطوة تحدد ما تفعله: النغمة والضربة والتكرار" },
        { "REJILLA es lo que dura un paso, tresillos incluidos", "",
          "GRID is how long a step lasts, triplets included",
          "网格决定每步的时值，含三连音", "الشبكة هي مدة الخطوة، والثلاثيات منها" },
        { "Ocho patrones, y la cadena decide en que orden suenan", "",
          "eight patterns, and the chain decides their order",
          "八个乐句，链决定播放顺序", "ثمانية أنماط، والسلسلة تحدد ترتيبها" },
        { "MEZCLA Y EFECTOS", "",
          "MIX AND EFFECTS",
          "混音与效果", "المزج والمؤثرات" },
        { "Tocar un efecto lo enciende y le da los tres mandos", "",
          "tapping an effect switches it on and hands it the knobs",
          "轻触效果即开启并接管三个旋钮", "لمس مؤثر يشغّله ويمنحه المقابض الثلاثة" },
        { "Mantenlo pulsado para cogerle los mandos sin encenderlo", "",
          "hold it to take its knobs without switching it on",
          "长按可接管其旋钮而不开启", "اضغطه مطولًا لأخذ مقابضه دون تشغيله" },
        { "RACK: un efecto y los 64 pads. EL PAD: los seis envios de uno", "",
          "RACK: one effect, 64 pads. THE PAD: one pad's six sends",
          "机架：一个效果对 64 个音垫；此音垫：一个音垫的六路发送", "الرف: مؤثر واحد و64 بادًا. الباد: إرسالاته الستة" },
        { "El XY deja los pads tocables debajo, para las dos manos", "",
          "XY leaves the pads playable underneath, for two hands",
          "XY 让下方音垫仍可弹奏，双手并用", "XY يترك الوسادات قابلة للعزف تحته، لليدين" },
        { "Verde hasta -12 dB, amarillo hasta -3, y el rojo se queda puesto", "",
          "green to -12 dB, amber to -3, and the red one stays lit",
          "绿色到 -12 dB，黄色到 -3，红色会保持点亮",
          "أخضر حتى ‎-12‎ dB، وأصفر حتى ‎-3‎، والأحمر يبقى مضيئًا" },
        { "GUARDAR Y EXPORTAR", "",
          "SAVING AND EXPORTING",
          "保存与导出", "الحفظ والتصدير" },
        { "Un proyecto lleva sus muestras dentro y se puede mover entero", "",
          "a project carries its samples inside and moves whole",
          "项目自带素材，可整体移动", "المشروع يحمل عيّناته بداخله ويُنقل كاملًا" },
        { "La sesion se recupera sola al abrir la app", "",
          "the session comes back on its own when you open the app",
          "打开应用时会自动恢复会话", "تعود الجلسة وحدها عند فتح التطبيق" },
        { "MASTER es lo que oyes; PISTAS son los stems que suman a el", "",
          "MASTER is what you hear; STEMS sum back to it",
          "主输出即所听；分轨相加还原主输出", "الماستر ما تسمعه، والمسارات تجمع إليه" },
        { "Deshacer y rehacer, dieciseis pasos", "",
          "undo and redo, sixteen steps",
          "撤销与重做，十六步", "تراجع وإعادة، ست عشرة خطوة" },
        { "AJUSTES > MIDI: manda las notas de lo que suena a otro aparato", "",
          "SETUP > MIDI: sends the notes of whatever sounds to another device",
          "设置 > MIDI：把发声的音符发送到别的设备",
          "الإعدادات > ميدي: يرسل نوتات ما يُسمع إلى جهاز آخر" },
        { "El pad 1 es la nota 36, y de ahi hacia arriba", "",
          "pad 1 is note 36, and up from there",
          "音垫 1 是音符 36，依次向上",
          "الباد 1 هو النوتة 36، وصعودًا من هناك" },
        { "RECIBIR deja que un teclado dispare los pads", "",
          "RECEIVE lets a keyboard fire the pads",
          "接收让键盘触发音垫",
          "استقبال يتيح للوحة مفاتيح تشغيل الوسادات" },
        { "El secuenciador manda tambien, no solo tus dedos", "",
          "the sequencer sends too, not just your fingers",
          "音序器也会发送，不只是你的手指",
          "المتتابع يرسل أيضًا، وليس أصابعك فقط" },
        { "SI ALGO NO SUENA", "",
          "IF SOMETHING IS SILENT",
          "若没有声音", "إذا لم يصدر صوت" },
        { "Mira la ganancia del pad y si hay un SOLO puesto en otro", "",
          "check the pad's gain, and whether another pad is SOLO",
          "检查该音垫增益，以及是否有其他音垫处于独奏", "افحص كسب الباد وهل هناك باد آخر منفرد" },
        { "Mira su envio al efecto que estas oyendo", "",
          "check its send to the effect you are listening to",
          "检查它到当前所听效果的发送量", "افحص إرساله إلى المؤثر الذي تسمعه" },
        { "Si la onda no reacciona estas ampliado: toca la tapa del medio", "",
          "if the wave will not move you are zoomed in: tap the middle cap",
          "若波形无反应说明已放大：按中间的键", "إن لم تستجب الموجة فأنت مكبّر: انقر المفتاح الأوسط" },
        { "AJUSTES > AUDIO ensena la latencia y el tamano de bloque", "",
          "SETUP > AUDIO shows the latency and the block size",
          "设置 > 音频 显示延迟与缓冲大小", "إعداد < الصوت يعرض الكمون وحجم الكتلة" },
        //  La barra de trabajo: tres palabras y ningun punto suspensivo, que
        //  el que la cosa sigue lo dice la barra moviendose.
        { "Cargando",       "",         "loading",    "载入中",     "جارٍ التحميل" },
        { "Quitando ruido", "",         "removing noise", "降噪中", "إزالة الضجيج" },
        { "Exportando",     "",         "exporting",  "导出中",     "جارٍ التصدير" },
        { "Repartiendo kit","",         "spreading kit", "分配套件", "توزيع الطقم" },
        { "REJILLA",        "",         "GRID",       "网格",       "الشبكة" },
        { "Un paso dura %1", "",         "one step lasts %1",
                                        "每步时值 %1", "الخطوة تساوي %1" },
        { "Rejilla",        "",         "Grid",       "网格",       "الشبكة" },
        { "cuanto dura un paso", "",    "how long a step lasts",
                                        "每一步的时值", "مدة الخطوة الواحدة" },
        { "AUTO CHOP",      "",         "AUTO CHOP",  "切片",       "تقطيع" },
        { "GRABAR MIC",     "",         "MIC REC",    "录音",       "ميكروفون" },
        { "REMUESTREAR",    "",         "RESAMPLE",   "重采样",     "إعادة" },
        { "Remuestreando al pad %1", "", "Resampling to pad %1",
                                        "正在重采样到音垫 %1", "إعادة أخذ إلى الباد %1" },
        { "Pad %1 remuestreado", "",    "Pad %1 resampled",
                                        "音垫 %1 已重采样", "أُعيد أخذ الباد %1" },
        { "Nada que remuestrear", "",   "Nothing to resample",
                                        "没有可重采样的内容", "لا شيء لإعادة أخذه" },
        { "PARAR",          "",         "STOP",       "停止",       "إيقاف" },
        { "OIR",            "",         "HEAR",       "试听",       "استمع" },
        { "CARGAR KIT",     "",         "LOAD KIT",   "载入套件",   "تحميل طقم" },
        { "MASTER = un WAV con lo que oyes.  PISTAS = el master mas un WAV "
          "por pad, para mezclar fuera.", "",
          "MASTER = one WAV of what you hear.  TRACKS = the master plus one "
          "WAV per pad, to mix elsewhere.",
          "主控 = 你听到的一个 WAV。分轨 = 主控加上每个音垫一个 WAV，便于外部混音。",
          "ماستر = ملف WAV واحد لما تسمعه. المسارات = الماستر بالإضافة إلى ملف "
          "WAV لكل باد، للمزج خارجًا." },
        { "SOBRESCRIBIR %1?", "",       "OVERWRITE %1?", "覆盖 %1？", "استبدال %1؟" },
        { "No hay audio en esta carpeta", "", "no audio in this folder",
                                        "此文件夹没有音频", "لا صوت في هذا المجلد" },
        { "Kit de %1 sonidos en el banco %2", "", "kit of %1 sounds into bank %2",
                                        "%1 个声音已载入 %2 库", "طقم من %1 صوت في بنك %2" },
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
        //  La vista previa del troceado cuando el pad no tiene sonido: sin
        //  esto, la unica ficha que se abre vacia lo decia en ingles.
        { "sin muestra",    "",         "no sample",  "无采样",     "لا توجد عينة" },
        //  La vista previa del troceado, cuando se intenta anadir una marca mas
        //  que pads libres hay: una marca de mas seria un trozo que se dibuja y
        //  no llega a ningun sitio.
        { "No caben mas trozos", "",     "No room for more slices",
                                        "放不下更多切片", "لا مساحة لمزيد من المقاطع" },

        // --- Ficha XY (superficie de directo) ------------------------------
        { "XY",             "",         "XY",         "XY",         "XY" },
        { "FIJO",           "",         "LATCH",      "锁定",       "تثبيت" },
        { "MOMENTANEO",     "",         "MOMENTARY",  "瞬时",       "لحظي" },
        { "SUENA",          "",         "LIVE",       "响着",       "يعمل" },
        { "EN ESPERA",      "",         "STANDBY",    "待命",       "بالانتظار" },
        { "se queda donde lo dejes", "", "stays where you leave it",
                                                      "停在你放开的位置", "يبقى حيث تتركه" },
        { "entra al tocar y sale al soltar", "", "in on touch, out on release",
                                                      "触摸进入，松开退出", "يدخل باللمس ويخرج بالرفع" },
        { "XY fijo - se queda donde lo dejes", "", "XY latch - stays where you leave it",
                                                      "XY 锁定 - 停在你放开的位置", "XY تثبيت - يبقى حيث تتركه" },
        { "XY momentaneo - suena mientras tocas", "", "XY momentary - sounds while you touch",
                                                      "XY 瞬时 - 触摸时发声", "XY لحظي - يعمل أثناء اللمس" },
        // --- Los parametros de los seis efectos ----------------------------
        //  No pasaban por T(): dieciocho rotulos en la cara de la maquina, en
        //  espanol, en las cuatro compilaciones. No los vio el banco de
        //  traduccion porque compara el TEXTO DE LOS COMPONENTES y estos se
        //  pintan a mano en paint() - un punto ciego que ahora esta anotado en
        //  Tests/expo.py.
        { "BARRIDO",        "",         "SWEEP",      "扫频",       "مسح" },
        { "fuera",          "",         "off",        "关闭",       "خارج" },
        { "RESO",           "",         "RESO",       "共振",       "رنين" },
        { "FREQ",           "",         "FREQ",       "频率",       "تردد" },
        { "DRIVE",          "",         "DRIVE",      "驱动",       "إشباع" },
        { "TIME",           "",         "TIME",       "时间",       "زمن" },
        { "FBK",            "",         "FBK",        "反馈",       "ارتجاع" },
        { "BITS",           "",         "BITS",       "位深",       "بِتّات" },
        { "RATE",           "",         "RATE",       "采样率",     "معدل" },
        { "SIZE",           "",         "SIZE",       "空间",       "حجم" },
        { "DAMP",           "",         "DAMP",       "阻尼",       "تخميد" },

        // --- SEC sheet -----------------------------------------------------
        { "PASOS",          "",         "STEPS",      "步数",       "خطوات" },
        //  The second page of the card: one step, not all of them. Kept short
        //  in every language - it is a tab caption sharing a row with another.
        { "PASO",           "",         "STEP",       "单步",       "خطوة" },
        { "toca un paso en PASOS para editarlo", "",
                                        "tap a step in STEPS to edit it",
                                                      "在步数页点一个步来编辑",
                                                                  "المس خطوة في صفحة الخطوات لتحريرها" },
        { "editando el paso %1", "",    "editing step %1",
                                                      "正在编辑第 %1 步", "تحرير الخطوة %1" },
        { "PATRON",         "",         "PATTERN",    "乐句",       "نمط" },
        { "LARGO",          "",         "LENGTH",     "长度",       "الطول" },
        { "CADENA",         "",         "CHAIN",      "链接",       "سلسلة" },
        { "NOTA DEL PASO",  "",         "STEP NOTE",  "该步音符", "نغمة الخطوة" },
        //  Los cuatro de la tira que va debajo de la rejilla. NOTA y REPETIR
        //  son nuevos: la pagina PASO los llamaba "NOTA DEL PASO" y "GOLPE"
        //  porque alli habia sitio para el nombre largo, y en la tira la
        //  columna mide la mitad.
        { "NOTA",           "",         "NOTE",       "音符",       "نغمة" },
        { "EUCLIDES",       "",         "EUCLID",     "欧几里得",   "إقليدس" },
        { "COPIAR FILA",    "",         "COPY ROW",   "复制该行",   "نسخ الصف" },
        { "PEGAR FILA",     "",         "PASTE ROW",  "粘贴该行",   "لصق الصف" },
        { "Fila del pad %1 copiada", "",
          "Pad %1 row copied", "已复制音垫 %1 的行", "تم نسخ صف الباد %1" },
        { "Fila pegada en el pad %1", "",
          "Row pasted into pad %1", "已粘贴到音垫 %1", "تم لصق الصف في الباد %1" },
        { "GOMA",           "",         "ERASE",      "橡皮",       "ممحاة" },
        { "TIJERAS",        "",         "CUT",        "剪刀",       "مقص" },
        { "Fila vacia",     "",         "Row cleared", "该行已清空", "تم مسح الصف" },
        { "%1 golpes repartidos en %2 pasos", "",
          "%1 hits spread over %2 steps", "%1 个音符分布在 %2 步中",
          "%1 ضربات موزعة على %2 خطوة" },
        { "toca un paso en la rejilla y sus mandos salen debajo", "",
          "tap a step in the grid and its controls appear below",
          "点击网格中的某一步，其控制项会出现在下方",
          "المس خطوة في الشبكة وتظهر مفاتيحها بالأسفل" },
        { "REPETIR",        "",         "REPEAT",     "重复",       "تكرار" },
        { "GOLPE",          "",         "HIT",        "力度",       "الضربة" },
        { "SWING",          "",         "SWING",      "摇摆",       "سوينغ" },
        { "BLOQUEOS",       "",         "P-LOCKS",    "参数锁定",   "قفل المعامل" },
        //  Elegir donde cae el rebote. Ver ProjectStore::setExports.
        { "CAMBIAR",        "",         "CHANGE",     "更改",       "تغيير" },
        //  LOS QUINCE PASOS DEL TOUR. Ver ZatiTour: son quince titulos y quince
        //  cuerpos, y van aqui como todo lo que se lee.
        { "LOS PADS",       "",         "THE PADS",   "音垫",       "البادات" },
        { "CUATRO BANCOS",  "",         "FOUR BANKS", "四个音垫组", "أربعة بنوك" },
        { "CARGAR, GRABAR, TOCAR", "",
          "LOAD, RECORD, PLAY", "载入、录音、播放", "تحميل وتسجيل وعزف" },
        { "LOS SEIS EFECTOS", "",       "THE SIX EFFECTS", "六个效果", "المؤثرات الستة" },
        { "LOS TRES MANDOS", "",        "THE THREE KNOBS", "三个旋钮", "المقابض الثلاثة" },
        { "LA REJILLA DE PASOS", "",    "THE STEP GRID", "步进网格", "شبكة الخطوات" },
        { "LO QUE HACE UN PASO", "",    "WHAT A STEP DOES", "每一步的作用", "ما تفعله الخطوة" },
        { "EL PIANO",       "",         "THE PIANO",  "钢琴",       "البيانو" },
        { "EL PATRON ENTERO", "",       "THE WHOLE PATTERN", "整个图案", "النمط كله" },
        { "DENTRO DE UN PAD", "",       "INSIDE A PAD", "音垫内部", "داخل الباد" },
        { "LA MESA Y EL RACK", "",      "MIXER AND RACK", "混音台与机架", "المازج والرف" },
        { "LA CANCION",     "",         "THE SONG",   "歌曲",       "الأغنية" },
        { "SACARLO DE AQUI", "",        "TAKE IT OUT", "导出成品", "أخرجه من هنا" },
        { "Y LO DEMAS",     "",         "AND THE REST", "其余部分", "وما تبقى" },

        { "Un sampler entero en el telefono. Este recorrido senala cada pieza en su "
          "sitio; se salta cuando quieras y se vuelve a abrir desde AJUSTES.", "",
          "A whole sampler on your phone. This tour points at each piece where it "
          "actually is; skip it whenever you like and reopen it from SETTINGS.",
          "手机上的完整采样器。本导览会逐一指出每个部件的实际位置；可随时跳过，也可在设置中重新打开。",
          "آلة أخذ عينات كاملة في الهاتف. تشير هذه الجولة إلى كل قطعة في مكانها؛ "
          "تخطَّها متى شئت وأعد فتحها من الإعدادات." },

        { "Dieciseis a la vista. Toca uno y suena; mantenlo pulsado y se abre todo "
          "lo que se le puede hacer.", "",
          "Sixteen on screen. Tap one and it sounds; hold it and everything you can "
          "do to it opens up.",
          "屏幕上十六个。轻触即发声；长按可打开该音垫的全部设置。",
          "ستة عشر على الشاشة. المس واحدًا فيصدر صوتًا؛ واستمر بالضغط ليفتح كل ما يمكن فعله به." },

        { "A, B, C y D: sesenta y cuatro pads en total. La rejilla ensena uno y "
          "los otros tres siguen sonando.", "",
          "A, B, C and D: sixty-four pads in all. The grid shows one and the other "
          "three keep playing.",
          "A、B、C、D 共六十四个音垫。网格只显示一组，其余三组照常发声。",
          "A و B و C و D: أربعة وستون بادًا. تعرض الشبكة واحدًا وتستمر الثلاثة الأخرى في العزف." },

        { "CARGAR trae un fichero a un pad. REC graba lo que oiga el microfono. "
          "PLAY pone en marcha el patron.", "",
          "LOAD brings a file into a pad. REC records whatever the microphone "
          "hears. PLAY starts the pattern.",
          "载入可将文件放入音垫，录音可采集麦克风输入，播放则启动图案。",
          "«تحميل» يجلب ملفًا إلى باد، و«تسجيل» يسجل ما يسمعه الميكروفون، و«تشغيل» يبدأ النمط." },

        { "Filtro, paso alto, saturacion, eco, reduccion y reverberacion. Son de "
          "la maquina, no del pad: cada pad decide cuanto les manda.", "",
          "Filter, high-pass, drive, delay, bit crush and reverb. They belong to "
          "the machine and not to the pad: each pad decides how much it sends.",
          "滤波、高通、失真、延迟、位压缩与混响。它们属于整机而非单个音垫：各音垫自行决定发送量。",
          "مرشح ومرشح عالٍ وتشبع وصدى وتقليل بتات وارتداد. هي ملك للآلة لا للباد: "
          "كل باد يقرر مقدار ما يرسله." },

        { "Los tres de arriba mueven el efecto que tengas abierto. Debajo de cada "
          "uno pone lo que hace en ese momento.", "",
          "The three at the top move whichever effect you have open. Under each "
          "one it says what it is doing right now.",
          "顶部三个旋钮控制当前开启的效果，每个下方会显示其当前作用。",
          "الثلاثة في الأعلى تحرك المؤثر المفتوح، وتحت كل واحد مكتوب ما يفعله في تلك اللحظة." },

        { "Dieciseis pasos por dieciseis pads. Toca una casilla y ese pad suena "
          "ahi; arrastra el dedo para escribir varias seguidas.", "",
          "Sixteen steps by sixteen pads. Tap a cell and that pad plays there; "
          "drag your finger to write several in a row.",
          "十六步乘十六个音垫。点触格子即让该音垫在此发声；拖动手指可连续写入多个。",
          "ستة عشر خطوة في ستة عشر بادًا. المس خانة فيُعزف ذلك الباد هناك؛ "
          "واسحب إصبعك لكتابة عدة خانات متتالية." },

        { "Con un paso tocado aparecen debajo sus mandos: nota, fuerza, "
          "repeticion, filtro y los cuatro bloqueos.", "",
          "With a step selected its controls appear underneath: note, velocity, "
          "roll, filter and the four p-locks.",
          "选中某一步后，其参数会出现在下方：音高、力度、连打、滤波与四项参数锁定。",
          "عند اختيار خطوة تظهر مقابضها أسفلها: النغمة والقوة والتكرار والمرشح والأقفال الأربعة." },

        { "La misma musica por tono en vez de por pasos. Varias notas en una "
          "columna son un acorde, y arrastrando se estira lo que dura cada una.", "",
          "The same music by pitch instead of by step. Several notes in one column "
          "make a chord, and dragging stretches how long each one lasts.",
          "以音高而非步进来书写同一段音乐。同一列的多个音符构成和弦，拖动可延长每个音符的时值。",
          "الموسيقى نفسها بالنغمة بدل الخطوة. عدة نوتات في عمود واحد تكوّن وترًا، "
          "والسحب يمدّ مدة كل نوتة." },

        { "Aqui vive lo que le pasa al patron entero: cadena, desplazar, doblar, "
          "humanizar, copiar y pegar, swing y rejilla.", "",
          "This is where whatever happens to the whole pattern lives: chain, nudge, "
          "double, humanise, copy and paste, swing and grid.",
          "这里是作用于整个图案的操作：链接、位移、加倍、人性化、复制粘贴、摇摆与网格。",
          "هنا يعيش ما يحدث للنمط كله: السلسلة والإزاحة والمضاعفة والأنسنة "
          "والنسخ واللصق والسوينغ والشبكة." },

        { "Recorte, afinado, filtro, envolvente y bucle. AUTO CHOP parte un break "
          "por sus golpes y lo reparte por los pads.", "",
          "Trim, tuning, filter, envelope and loop. AUTO CHOP splits a break at its "
          "hits and spreads it across the pads.",
          "裁切、调音、滤波、包络与循环。自动切片会按鼓点切开一段循环并分配到各音垫。",
          "القص والدوزنة والمرشح والمغلف والحلقة. و«التقطيع التلقائي» يقسم اللفة "
          "عند ضرباتها ويوزعها على البادات." },

        { "La mesa pone los dieciseis a su nivel. El RACK dice cuanto manda cada "
          "pad a cada efecto, sin cerrar nada.", "",
          "The mixer sets all sixteen to their level. The RACK says how much each "
          "pad sends to each effect, without closing anything.",
          "混音台设定十六个音垫的电平。机架页可在不关闭任何界面的情况下设定各音垫到各效果的发送量。",
          "المازج يضبط مستوى الستة عشر. و«الرف» يحدد كم يرسل كل باد إلى كل مؤثر دون إغلاق شيء." },

        { "Los patrones colocados en el tiempo, en cuatro carriles. Un bloque "
          "dura lo que ocupa, no lo que dure su patron.", "",
          "The patterns placed in time, across four lanes. A block lasts as long as "
          "it occupies, not as long as its pattern.",
          "将图案排布在时间轴的四条轨道上。区块的长度取决于它占据的小节数，而非其图案的长度。",
          "الأنماط موضوعة في الزمن على أربعة مسارات. يدوم البلوك بقدر ما يشغل، لا بقدر نمطه." },

        { "La mezcla entera o una pista por pad, en WAV o en OGG, y a la carpeta "
          "que tu elijas.", "",
          "The whole mix or one track per pad, as WAV or OGG, and into whichever "
          "folder you choose.",
          "可导出整体混音或每个音垫一条分轨，格式为 WAV 或 OGG，并写入你选择的文件夹。",
          "المزيج كاملًا أو مسارًا لكل باد، بصيغة WAV أو OGG، وإلى المجلد الذي تختاره." },

        { "El idioma, las cuatro carcasas y el MANUAL, que cuenta todo esto con "
          "calma. Ya puedes empezar.", "",
          "The language, the four chassis and the MANUAL, which tells all of this "
          "properly. You are ready to start.",
          "语言、四种机身外观，以及会把这些从头讲清楚的手册。现在可以开始了。",
          "اللغة والهياكل الأربعة و«الدليل» الذي يشرح هذا كله على مهل. يمكنك البدء الآن." },
        { "%1 en %2", "",
          "%1 in %2",
          "%1 个，位于 %2", "%1 في %2" },
        { "CARPETA DE EXPORTAR", "",
          "EXPORT FOLDER",
          "导出文件夹", "مجلد التصدير" },
        { "entra donde quieras y pulsa USAR ESTA CARPETA", "",
          "go wherever you like and press USE THIS FOLDER",
          "进入任意文件夹后按“使用此文件夹”", "ادخل حيث تشاء ثم اضغط استخدم هذا المجلد" },
        { "USAR ESTA CARPETA", "",
          "USE THIS FOLDER",
          "使用此文件夹", "استخدم هذا المجلد" },
        { "Esa carpeta no deja escribir - prueba otra", "",
          "That folder will not accept a write - try another",
          "该文件夹不可写，请换一个", "هذا المجلد لا يقبل الكتابة - جرّب غيره" },
        { "El rebote caera en %1", "",
          "The bounce will land in %1",
          "导出将写入 %1", "سيقع التصدير في %1" },

        //  EL TOUR DE BIENVENIDA. ATRAS y EMPEZAR llevan clave propia porque
        //  esas dos palabras ya existen en la tabla con OTRO sentido - ATRAS
        //  es la herramienta que desplaza el patron, y en chino dice "mover"; y
        //  EMPEZAR es el titulo del primer capitulo del manual. Una clave que
        //  se reaprovecha por parecerse en espanol sale mal en las otras tres.
        { "TOUR",           "",         "TOUR",       "导览",       "جولة" },
        { "TOUR ATRAS",     "ATRAS",    "BACK",       "返回",       "رجوع" },
        { "SALTAR",         "",         "SKIP",       "跳过",       "تخطٍ" },
        { "SIGUIENTE",      "",         "NEXT",       "下一步",     "التالي" },
        { "TOUR EMPEZAR",   "EMPEZAR",  "START",      "开始",       "ابدأ" },
        { "SESENTA Y CUATRO PADS", "",
          "SIXTY-FOUR PADS",
          "六十四个音垫", "أربعة وستون بادًا" },
        { "METE UN SONIDO", "",
          "GET A SOUND IN",
          "载入声音", "أدخل صوتًا" },
        { "ESCRIBE UN PATRON", "",
          "WRITE A PATTERN",
          "编写图案", "اكتب نمطًا" },
        { "MOLDEA EL SONIDO", "",
          "SHAPE THE SOUND",
          "塑造声音", "شكّل الصوت" },
        { "SACALO DE AQUI", "",
          "TAKE IT OUT",
          "导出成品", "أخرجه من هنا" },
        { "Dieciseis a la vista y cuatro bancos: A, B, C y D. La rejilla ensena "
          "uno y los otros tres siguen sonando. Toca uno y suena; mantenlo "
          "pulsado y se abre lo que se le puede hacer.", "",
          "Sixteen on screen and four banks: A, B, C and D. The grid shows one "
          "and the other three keep playing. Tap one and it sounds; hold it and "
          "everything you can do to it opens up.",
          "屏幕上十六个，共四组：A、B、C、D。网格只显示一组，其余三组照常发声。轻触即发声；"
          "长按可打开该音垫的全部设置。",
          "ستة عشر على الشاشة وأربعة بنوك: A و B و C و D. تعرض الشبكة واحدًا "
          "وتستمر الثلاثة الأخرى في العزف. المس واحدًا فيصدر صوتًا؛ واستمر بالضغط "
          "ليفتح كل ما يمكن فعله به." },
        { "CARGAR trae un fichero, GRABAR toma lo que oiga el microfono y "
          "FABRICA rellena los 64 con sonidos que se sintetizan aqui dentro, "
          "sin ocupar sitio. AUTO CHOP parte un break por sus golpes y lo "
          "reparte por los pads.", "",
          "LOAD brings in a file, REC takes whatever the microphone hears, and "
          "FACTORY fills all 64 with sounds synthesised in here, taking up no "
          "space at all. AUTO CHOP splits a break at its hits and spreads it "
          "across the pads.",
          "载入可导入文件，录音可采集麦克风输入，工厂音色则用应用内合成的声音填满全部 64 个音垫，"
          "不占任何安装空间。自动切片会按鼓点切开一段循环并分配到各音垫。",
          "«تحميل» يجلب ملفًا، و«تسجيل» يأخذ ما يسمعه الميكروفون، و«المصنع» يملأ "
          "الأربعة والستين بأصوات تُركَّب هنا بالكامل دون أن تشغل أي مساحة. "
          "و«التقطيع التلقائي» يقسم اللفة عند ضرباتها ويوزعها على البادات." },
        { "En SEC la rejilla son dieciseis pasos por dieciseis pads: toca una "
          "casilla y suena ahi. Con un paso tocado aparecen debajo sus mandos "
          "- nota, fuerza, repeticion, filtro y los bloqueos. Y en PIANO se "
          "escribe por tono, con notas que duran lo que quieras.", "",
          "In SEQ the grid is sixteen steps by sixteen pads: tap a cell and it "
          "plays there. With a step selected its controls appear underneath - "
          "note, velocity, roll, filter and the p-locks. And PIANO writes by "
          "pitch, with notes that last as long as you want.",
          "在音序页，网格为十六步乘十六个音垫：点触格子即在该处发声。选中某一步后，"
          "其参数会出现在下方——音高、力度、连打、滤波与参数锁定。钢琴页则按音高书写，"
          "音符时值可任意设定。",
          "في «التتابع» الشبكة ستة عشر خطوة في ستة عشر بادًا: المس خانة فتُعزف هناك. "
          "وعند اختيار خطوة تظهر مقابضها أسفلها - النغمة والقوة والتكرار والمرشح "
          "والأقفال. وفي «البيانو» تكتب بالنغمة، بنوتات تدوم ما تشاء." },
        { "Cada pad tiene su filtro, su recorte y sus seis envios. Los efectos "
          "son de la maquina y no del pad: se abren desde la cara y cada pad "
          "decide cuanto le manda, en el RACK. La ficha XY mueve dos a la vez "
          "con el dedo.", "",
          "Every pad has its own filter, its own trim and its own six sends. "
          "The effects belong to the machine and not to the pad: you switch "
          "them on from the front and each pad decides how much it sends, in "
          "the RACK. The XY card moves two of them at once with one finger.",
          "每个音垫都有自己的滤波、裁切与六路发送。效果属于整机而非单个音垫："
          "在面板上开启，各音垫在机架页决定各自的发送量。XY 页可用一根手指同时控制两个参数。",
          "لكل باد مرشحه وقصّه وإرسالاته الستة. المؤثرات ملك للآلة لا للباد: "
          "تُشغَّل من الواجهة ويقرر كل باد مقدار ما يرسله إليها في «الرف». "
          "وبطاقة XY تحرك اثنين منها معًا بإصبع واحد." },
        { "EXPORTAR saca la mezcla entera o una pista por pad, en WAV o en OGG. "
          "El proyecto se guarda solo, y en AJUSTES estan el idioma, las cuatro "
          "carcasas y el MANUAL, que cuenta todo esto con calma.", "",
          "EXPORT writes out the whole mix or one track per pad, as WAV or OGG. "
          "The project saves itself, and SETTINGS holds the language, the four "
          "chassis and the MANUAL, which tells all of this properly.",
          "导出可输出整体混音，或每个音垫一条分轨，格式为 WAV 或 OGG。"
          "工程会自动保存；语言、四种机身外观与手册都在设置页，手册会把这些从头讲清楚。",
          "«التصدير» يُخرج المزيج كاملًا أو مسارًا لكل باد، بصيغة WAV أو OGG. "
          "والمشروع يحفظ نفسه، وفي «الإعدادات» تجد اللغة والهياكل الأربعة "
          "و«الدليل» الذي يشرح هذا كله على مهل." },
        { "recto",          "",         "straight",   "平直",       "مستقيم" },
        { "COMPAS",         "",         "BAR",        "小节",       "مازورة" },
        { "TEMPO",          "",         "TEMPO",      "速度",       "الإيقاع" },
        { "TAP",            "",         "TAP",        "打点",       "نقر" },
        { "BANCO",          "",         "BANK",       "乐句库",     "بنك" },
        { "COPIAR",         "",         "COPY",       "复制",       "نسخ" },
        { "PEGAR",          "",         "PASTE",      "粘贴",       "لصق" },
        { "P%1 copiado",    "",         "P%1 copied", "已复制 P%1", "نُسخ P%1" },
        { "Pegado en P%1",  "",         "pasted into P%1", "已粘贴到 P%1", "لُصق في P%1" },
        { "Sigue marcando el tempo", "", "keep tapping",
                                        "继续打点", "واصل النقر" },
        { "Tempo %1",       "",         "Tempo %1",   "速度 %1",    "إيقاع %1" },
        { "QUITAR CADENA",  "",         "CLEAR CHAIN","清除链接", "مسح السلسلة" },
        { "VACIAR",         "",         "CLEAR",      "清空",       "تفريغ" },
        { "GUARDAR KIT",    "",         "SAVE KIT",   "保存音色组",   "حفظ الطقم" },
        { "Guardando kit",  "",         "Saving kit", "正在保存音色组", "جارٍ حفظ الطقم" },
        { "Ponle nombre primero", "",    "Name it first", "请先命名",  "سمّه أولاً" },
        { "No se pudo escribir el kit", "", "Could not write the kit", "无法写入音色组", "تعذّر حفظ الطقم" },
        { "No hay sonidos en este banco", "", "No sounds in this bank", "此音色库没有声音", "لا أصوات في هذا البنك" },
        { "Kit \"%1\": %2 sonidos", "", "Kit \"%1\": %2 sounds", "音色组 \"%1\"：%2 个声音", "الطقم \"%1\": %2 صوت" },
        //  Cuantas octavas se ven a la vez en el piano roll. Clave propia y no
        //  reaprovechada: "OCTAVA" ya existe para el boton que MUEVE la ventana
        //  y significa otra cosa - una clave que se reaprovecha por parecerse
        //  en espanol sale mal en las otras tres.
        { "1 OCTAVA",       "",         "1 OCTAVE",   "1 个八度",     "أوكتاف واحد" },
        { "2 OCTAVAS",      "",         "2 OCTAVES",  "2 个八度",     "أوكتافان" },
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
        { "Busca donde empieza cada golpe y corta ahi, no a intervalos iguales. El pad de origen se queda con el primero.",
          "",
          "Finds where each hit starts and cuts there, not at even intervals. The source pad keeps the first one.",
          "找出每个打点的起始并在那里切分，而不是等分。源音垫保留第一段。",
          "يبحث عن بداية كل ضربة ويقطع هناك، لا على فترات متساوية. يحتفظ الباد الأصلي بالأول." },
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
        // --- MIDI ----------------------------------------------------------
        { "MIDI",           "",         "MIDI",       "MIDI",       "ميدي" },
        { "MANDAR NOTAS A", "",         "SEND NOTES TO", "发送音符到", "إرسال النوتات إلى" },
        { "RECIBIR NOTAS DE", "",       "RECEIVE NOTES FROM", "接收音符来自", "استقبال النوتات من" },
        { "MANDAR",         "",         "SEND",       "发送",       "إرسال" },
        { "RECIBIR",        "",         "RECEIVE",    "接收",       "استقبال" },
        { "nada enchufado", "",         "nothing plugged in", "未连接设备", "لا شيء موصول" },
        { "No se pudo abrir %1", "",    "Could not open %1", "无法打开 %1", "تعذّر فتح %1" },
        { "El pad 1 es la nota %1, y de ahi hacia arriba. Canal %2.", "",
          "Pad 1 is note %1, and up from there. Channel %2.",
          "音垫 1 是音符 %1，依次向上。通道 %2。",
          "الباد 1 هو النوتة %1، وصعودًا من هناك. القناة %2." },

        { "CARPETA",        "",         "FOLDER",     "文件夹",     "المجلد" },
        { "NOMBRE",         "",         "NAME",       "名称",       "الاسم" },
        { "Sobrescribir \"%1\"?", "",  "Overwrite \"%1\"?", "覆盖“%1”？", "استبدال \"%1\"؟" },
        { "Escribe un nombre para el proyecto", "", "Type a name for the project",
                                        "给工程取个名字",
                                        "اكتب اسمًا للمشروع" },
        //  Los rotulos de la barra de carga. Van aqui, con los del proyecto,
        //  porque nombran el mismo trabajo: los tres son 64 ficheros.
        { "Iniciando",      "",         "Starting up", "正在启动",   "جارٍ البدء" },
        { "Recuperando sesion", "",     "Restoring session", "正在恢复会话", "جارٍ استعادة الجلسة" },
        { "Abriendo proyecto", "",      "Opening project",   "正在打开工程", "جارٍ فتح المشروع" },
        { "Guardando proyecto", "",     "Saving project",    "正在保存工程", "جارٍ حفظ المشروع" },
        { "Espera a que termine %1", "", "Wait until %1 finishes", "请等待%1结束", "انتظر حتى ينتهي %1" },
        { "Sesion recuperada - %1", "", "Session restored - %1", "会话已恢复 — %1", "استُعيدت الجلسة — %1" },
        { "Sesion recuperada  [%1 pads]", "", "Session restored  [%1 pads]", "会话已恢复 [%1 个音垫]", "استُعيدت الجلسة [%1 باد]" },
        { "Sesion recuperada  [1 pad]", "", "Session restored  [1 pad]", "会话已恢复 [1 个音垫]", "استُعيدت الجلسة [باد واحد]" },

        // --- Browser -------------------------------------------------------
        // --- Los sonidos de fabrica (Kits.h) --------------------------------
        { "FABRICA",        "",         "FACTORY",    "内置",       "المصنع" },

        // --- INSTRUMENTOS: el contenido descargable (Instrumentos.h) --------
        //  La tapa que decia FABRICA lleva ahora al catalogo, donde la fabrica
        //  es el primer pack. La fila FABRICA se queda porque el manual la
        //  nombra al contar de donde salen los sesenta y cuatro.
        { "INSTRUMENTOS",   "",         "INSTRUMENTS", "乐器",     "آلات" },
        { "PACK",           "",         "PACK",       "音色包",     "حزمة" },
        { "Los packs van en %1", "",
          "Packs go in %1", "音色包放在 %1", "توضع الحزم في %1" },
        { "BANCO %1",       "",         "BANK %1",    "库 %1",     "البنك %1" },
        { "%1 de %2",       "",         "%1 of %2",   "%1 / %2",    "%1 من %2" },
        { "No hay instrumentos instalados", "",
          "No instruments installed", "尚未安装任何乐器",
          "لا توجد آلات مثبتة" },
        { "Toca uno y sus 16 presets van al banco %1. Lo que hubiera se pierde.", "",
          "Tap one and its 16 presets go to bank %1. Whatever was there is lost.",
          "轻触其中一个，它的 16 个预设会进入库 %1。原有内容将丢失。",
          "المس واحدًا فتنتقل إعداداته الـ 16 إلى البنك %1. ويضيع ما كان فيه." },
        { "Este pack no esta comprado.", "",
          "This pack has not been purchased.", "此音色包尚未购买。",
          "لم يتم شراء هذه الحزمة." },
        { "%1 no esta comprado", "",
          "%1 has not been purchased", "%1 尚未购买",
          "%1 غير مشترى" },
        { "Banco %1: %2",   "",         "Bank %1: %2", "库 %1：%2",  "البنك %1: %2" },
        { "ACUSTICA",       "",         "ACOUSTIC",   "原声",       "أكوستيك" },
        { "MAQUINA",        "",         "MACHINE",    "机器",       "آلة" },
        { "TEXTURA",        "",         "TEXTURE",    "质感",       "نسيج" },
        { "TONOS",          "",         "TONES",      "音调",       "نغمات" },
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
        //  MIS KITS y no "KITS" a secas: la carpeta guarda los que ha hecho
        //  esta persona, y al lado de FABRICA -que son los de la casa- la
        //  palabra sola no distingue nada.
        { "MIS KITS",       "",         "MY KITS",    "我的套件",   "أطقمي" },
        { "Aun no has guardado ningun kit", "",
          "No kits saved yet", "还没有保存任何套件", "لم تحفظ أي طقم بعد" },
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
        { "Ganancia canal %1", "",      "Gain channel %1",   "通道 %1 增益", "كسب القناة %1" },
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
