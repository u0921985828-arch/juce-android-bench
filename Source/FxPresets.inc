//  LOS PRESETS DE FABRICA DE CADA EFECTO.
//
//  Se incluye DENTRO del namespace desde `FxPresets.h`, que es la misma forma
//  que `SintesTabla.inc` tiene desde que hay dieciseis familias: una tabla de
//  datos no es una cabecera, y si se pudiera incluir desde dos sitios habria
//  dos tablas.
//
//  CINCO FILAS POR TIPO Y NO SEIS. El preset cero es DEFECTO y NO SE ESCRIBE
//  aqui: sus valores salen de `AudioEngine::kFxDef`. Esta casa ya tiene DOS
//  tablas de defectos -la del motor y el `spec[pi].def` de la cara- y una
//  comprobacion que las cuadra a mano sobre 69 casillas con tolerancia 0.001
//  (`MainComponent_Audit.cpp`, juzgada por `Tests/ranuras.py`). Una tercera no
//  seria mas presets: seria un tercer sitio donde el defecto puede quedarse
//  viejo sin que nadie se entere.
//
//  Cada fila es { nombre, p0, p1, MIX, enganche }:
//
//    · p0 y p1 son los dos mandos libres, y sus rangos NO se declaran otra vez:
//      salen de `fxDefs[f].spec[pi].{lo,hi}` y la puerta los acota alli. Los
//      numeros de aqui estan dentro a proposito, y `Tests/presets.py` lo mide.
//    · MIX es el parametro 2, que es tambien el interruptor de la ranura: un
//      preset con MIX en cero APAGARIA el efecto al elegirlo, que es lo
//      contrario de lo que se pidio. Ninguno lo lleva, y la prueba lo vigila.
//    · El enganche es el cuarto parametro -cero libre en Hz, mayor que cero la
//      division del compas-. Es un parametro VIVO y medido que todavia no
//      tiene ningun mando en la app: el motor guarda cuatro y la cara solo
//      dibuja tres. Los presets de CHO, FLA, PHA y TRM son por ahora su UNICA
//      puerta, y DEFECTO lo devuelve a libre, asi que no es una trampa: es un
//      camino de ida y vuelta mientras el mando llega.
//      La division es el denominador en negras por cuatro -ver `pasoMod`-:
//      16 = un compas, 8 = blanca, 4 = negra, 2 = corchea, 1 = semicorchea.
//
//  Y LOS NOMBRES DICEN LO QUE HACEN, no a que aparato se parecen.
//  `Tests/marcas.py` barre `Source/**.{h,cpp,inc,jucer}`, asi que este fichero
//  entra en el barrido solo. Tampoco se traducen, igual que
//  `Sintes::Preset::nombre` y que los nombres de los propios efectos: un
//  preset es un nombre propio, y traducirlo lo convierte en otro preset.

//  `inline` y no `static`: este `.inc` se incluye desde una CABECERA, asi que
//  con enlace interno cada unidad de traduccion se llevaria su copia de la
//  tabla. Es la misma razon por la que en esta casa todo token que una piel
//  mueve es `inline` y nunca `const`.
inline constexpr Preset kTabla[AudioEngine::kNumFx][kEscritos] =
{
    //  FLT  barrido -1..+1 (centro neutro), reso 0.3..4
    { { "CIERRA",    -0.55f,  0.90f, 1.00f, 0.0f },
      { "TAPA",      -0.90f,  0.70f, 1.00f, 0.0f },
      { "ABRE",       0.50f,  1.20f, 1.00f, 0.0f },
      { "TELEFONO",   0.75f,  2.20f, 1.00f, 0.0f },
      { "RESUENA",   -0.35f,  3.60f, 1.00f, 0.0f } },

    //  HPF  freq 20..20000, reso 0.3..4
    { { "QUITA SUB",  150.0f, 0.707f, 1.00f, 0.0f },
      { "LIGERO",      80.0f, 0.707f, 1.00f, 0.0f },
      { "RADIO",      600.0f, 0.90f,  1.00f, 0.0f },
      { "FINO",      1800.0f, 1.40f,  1.00f, 0.0f },
      { "SILBA",     2500.0f, 3.20f,  1.00f, 0.0f } },

    //  DRV  drive 0..1, tono 200..20000
    { { "CALOR",     0.30f,  9000.0f, 0.60f, 0.0f },
      { "CINTA",     0.45f,  6000.0f, 0.80f, 0.0f },
      { "SUCIO",     0.75f,  4500.0f, 0.90f, 0.0f },
      { "ROTO",      1.00f,  3000.0f, 1.00f, 0.0f },
      { "BRILLO",    0.55f, 14000.0f, 0.70f, 0.0f } },

    //  DLY  tiempo 20..1000 ms, realimentacion 0..0.95
    { { "GOLPE",      90.0f, 0.15f, 0.30f, 0.0f },
      { "MEDIO",     250.0f, 0.35f, 0.35f, 0.0f },
      { "LARGO",     500.0f, 0.45f, 0.30f, 0.0f },
      { "SE VA",     375.0f, 0.75f, 0.40f, 0.0f },
      { "DOBLA",      45.0f, 0.05f, 0.45f, 0.0f } },

    //  BIT  bits 1..16, diezmado 1..64
    { { "OCHO BITS",  8.0f,  2.0f, 0.60f, 0.0f },
      { "CONSOLA",    6.0f,  4.0f, 0.70f, 0.0f },
      { "LINEA",      5.0f, 12.0f, 0.60f, 0.0f },
      { "DESTROZA",   3.0f, 24.0f, 0.80f, 0.0f },
      { "CRUJE",     12.0f,  8.0f, 0.50f, 0.0f } },

    //  REV  tamano 0..1, amortiguacion 0..1
    { { "CUARTO",    0.25f, 0.60f, 0.20f, 0.0f },
      { "SALA",      0.55f, 0.45f, 0.30f, 0.0f },
      { "PLACA",     0.70f, 0.25f, 0.35f, 0.0f },
      { "CATEDRAL",  0.95f, 0.30f, 0.40f, 0.0f },
      { "MUELLE",    0.40f, 0.75f, 0.25f, 0.0f } },

    //  EQ  ancho 0.4..3, salida -12..+12 dB. Sus cinco bandas van aparte, en
    //      `kBandasEq`: son otros veinte numeros y no caben en una fila.
    { { "SONRISA",   1.00f, -1.00f, 1.00f, 0.0f },
      { "VOZ",       1.20f,  0.00f, 1.00f, 0.0f },
      { "BOMBO",     1.40f, -1.00f, 1.00f, 0.0f },
      { "AIRE",      0.80f,  0.00f, 1.00f, 0.0f },
      { "LIMPIA",    1.00f,  0.00f, 1.00f, 0.0f } },

    //  CMP  umbral -60..0 dB, ratio 1..20
    { { "SUAVE",    -12.0f,  2.0f, 1.00f, 0.0f },
      { "PEGA",     -20.0f,  4.0f, 1.00f, 0.0f },
      { "APRIETA",  -28.0f,  8.0f, 1.00f, 0.0f },
      { "APLASTA",  -36.0f, 16.0f, 1.00f, 0.0f },
      { "MESA",      -8.0f,  1.8f, 1.00f, 0.0f } },

    //  GTE  umbral -80..0 dB, cierre 5..500 ms
    { { "LIMPIA",   -45.0f, 150.0f, 1.00f, 0.0f },
      { "SECA",     -30.0f,  60.0f, 1.00f, 0.0f },
      { "CORTA",    -24.0f,  20.0f, 1.00f, 0.0f },
      { "LENTA",    -55.0f, 300.0f, 1.00f, 0.0f },
      { "TAJANTE",  -18.0f,  12.0f, 1.00f, 0.0f } },

    //  DSS  frecuencia 2000..12000 Hz, fuerza 0..1
    { { "VOZ",      6500.0f, 0.55f, 1.00f, 0.0f },
      { "SUAVE",    7500.0f, 0.30f, 1.00f, 0.0f },
      { "FUERTE",   5500.0f, 0.80f, 1.00f, 0.0f },
      { "PLATOS",   9000.0f, 0.60f, 1.00f, 0.0f },
      { "AGUDOS",   4000.0f, 0.45f, 1.00f, 0.0f } },

    //  LIM  techo -24..0 dB, soltar 5..500 ms
    { { "TECHO",     -1.0f, 120.0f, 1.00f, 0.0f },
      { "SEGURO",    -3.0f, 200.0f, 1.00f, 0.0f },
      { "RAPIDO",    -2.0f,  30.0f, 1.00f, 0.0f },
      { "MURO",      -0.3f,  10.0f, 1.00f, 0.0f },
      { "SUELTA",    -6.0f, 400.0f, 1.00f, 0.0f } },

    //  CHO  rate 0.05..8 Hz, profundidad 0..1. Los dos ultimos van al tempo.
    { { "LENTO",     0.25f, 0.45f, 0.45f,  0.0f },
      { "ESPESO",    1.20f, 0.85f, 0.60f,  0.0f },
      { "DOBLA",     0.55f, 0.30f, 0.40f,  0.0f },
      { "COMPAS",    0.80f, 0.55f, 0.50f, 16.0f },
      { "NEGRA",     0.80f, 0.70f, 0.55f,  4.0f } },

    //  FLA  rate 0.05..8 Hz, realimentacion 0..1
    { { "AVION",     0.15f, 0.85f, 0.55f,  0.0f },
      { "METAL",     0.35f, 0.20f, 0.50f,  0.0f },
      { "LENTO",     0.08f, 0.60f, 0.45f,  0.0f },
      { "COMPAS",    0.25f, 0.55f, 0.50f, 16.0f },
      { "NEGRA",     0.25f, 0.65f, 0.50f,  4.0f } },

    //  PHA  rate 0.05..8 Hz, profundidad 0..1
    { { "SUAVE",     0.20f, 0.50f, 0.45f,  0.0f },
      { "HONDO",     0.45f, 0.95f, 0.60f,  0.0f },
      { "RAPIDO",    2.50f, 0.65f, 0.50f,  0.0f },
      { "COMPAS",    0.35f, 0.70f, 0.50f, 16.0f },
      { "CORCHEA",   0.35f, 0.70f, 0.50f,  2.0f } },

    //  TRM  rate 0.2..20 Hz, profundidad 0..1
    { { "LENTO",     2.00f, 0.60f, 1.00f, 0.0f },
      { "CORTA",     8.00f, 1.00f, 1.00f, 0.0f },
      { "TIMBRE",   16.00f, 0.75f, 1.00f, 0.0f },
      { "CORCHEA",   4.50f, 0.85f, 1.00f, 2.0f },
      { "SEMIS",     4.50f, 0.95f, 1.00f, 1.0f } },

    //  RNG  frecuencia 20..4000 Hz, anillo 0..1
    { { "GRAVE",      60.0f, 1.00f, 0.50f, 0.0f },
      { "METAL",     440.0f, 1.00f, 0.60f, 0.0f },
      { "CAMPANA",  1200.0f, 1.00f, 0.55f, 0.0f },
      { "TIEMBLA",    35.0f, 0.45f, 0.45f, 0.0f },
      { "ROBOT",    2200.0f, 0.85f, 0.70f, 0.0f } },

    //  PIT  semitonos -12..+12, grano 20..120 ms
    { { "OCTAVA",    12.0f,  50.0f, 1.00f, 0.0f },
      { "SUB",      -12.0f,  80.0f, 1.00f, 0.0f },
      { "QUINTA",     7.0f,  60.0f, 1.00f, 0.0f },
      { "TERCERA",    4.0f,  60.0f, 1.00f, 0.0f },
      { "METALICO",   3.0f,  20.0f, 1.00f, 0.0f } },

    //  WID  ancho 0..2 veces, mono por debajo de 20..400 Hz
    { { "ABRE",      1.40f, 120.0f, 1.00f, 0.0f },
      { "MUY ANCHO", 2.00f, 150.0f, 1.00f, 0.0f },
      { "CIERRA",    0.50f,  60.0f, 1.00f, 0.0f },
      { "MONO",      0.00f, 400.0f, 1.00f, 0.0f },
      { "SUB CENTRO",1.70f, 300.0f, 1.00f, 0.0f } },

    //  EXC  cruce 1000..10000 Hz, fuerza 0..1
    { { "AIRE",      6000.0f, 0.45f, 1.00f, 0.0f },
      { "PRESENCIA", 3000.0f, 0.60f, 1.00f, 0.0f },
      { "BRILLO",    8000.0f, 0.70f, 1.00f, 0.0f },
      { "SUAVE",     5000.0f, 0.25f, 1.00f, 0.0f },
      { "FUERTE",    2000.0f, 0.85f, 1.00f, 0.0f } },

    //  TRN  ataque -1..+1, caida -1..+1 (el centro es no hacer nada)
    { { "MAS GOLPE",  0.70f, -0.20f, 1.00f, 0.0f },
      { "SIN GOLPE", -0.60f,  0.20f, 1.00f, 0.0f },
      { "MAS COLA",   0.10f,  0.70f, 1.00f, 0.0f },
      { "SECO",       0.40f, -0.80f, 1.00f, 0.0f },
      { "PLANO",     -0.30f, -0.30f, 1.00f, 0.0f } },

    //  FRZ  ventana 20..500 ms, cruce de la costura 0..1
    { { "CORTO",      40.0f, 0.20f, 1.00f, 0.0f },
      { "MEDIO",     180.0f, 0.35f, 1.00f, 0.0f },
      { "LARGO",     420.0f, 0.55f, 1.00f, 0.0f },
      { "GRANO",      25.0f, 0.05f, 1.00f, 0.0f },
      { "COLCHON",   350.0f, 0.90f, 1.00f, 0.0f } },

    //  WAH  sensibilidad 0..1, base 200..1200 Hz
    { { "PEDAL",     0.60f, 400.0f, 1.00f, 0.0f },
      { "SUAVE",     0.35f, 500.0f, 1.00f, 0.0f },
      { "AGRESIVO",  0.95f, 300.0f, 1.00f, 0.0f },
      { "GRAVE",     0.70f, 220.0f, 1.00f, 0.0f },
      { "AGUDO",     0.50f, 900.0f, 1.00f, 0.0f } },

    //  OCT  nivel de la de arriba 0..1, nivel de la de abajo 0..1
    { { "ARRIBA",    1.00f, 0.00f, 1.00f, 0.0f },
      { "ABAJO",     0.00f, 1.00f, 1.00f, 0.0f },
      { "LAS DOS",   0.70f, 0.70f, 1.00f, 0.0f },
      { "SUB",       0.15f, 0.90f, 1.00f, 0.0f },
      { "BRILLO",    0.90f, 0.25f, 1.00f, 0.0f } },
};

//  LAS CINCO BANDAS DE CADA PRESET DE EQ, y solo del EQ.
//
//  El estado del ecualizador NO vive en `fxP`: vive en `Eq5`, que son cinco
//  bandas con frecuencia, ganancia, tipo y Q — veinte numeros mas. Es
//  exactamente el motivo por el que el EQ es el unico efecto con superficie
//  propia (ver `Eq5.h`), y por el que su preset no cabe en una fila de cuatro.
//
//  EL FORMATO ES EL QUE EL PROYECTO YA ESCRIBE -`hz:dB:tipo:Q` por banda,
//  punto y coma entre bandas, ver la propiedad `eqc` de `captureState`- y no
//  uno nuevo. Un segundo formato para lo mismo es un segundo sitio donde
//  equivocarse al leer.
//
//  Tipo: 0 estante bajo, 1 campana, 2 estante alto (ver `Eq5::Tipo`). El
//  reparto de fabrica es estante en los extremos y campana en medio.
inline constexpr const char* kBandasEq[kEscritos] =
{
    //  SONRISA: los dos extremos arriba y el medio hundido.
    "80:4.0:0:0.70;250:-2.0:1:0.90;1000:-1.5:1:0.90;3500:1.0:1:0.90;10000:4.0:2:0.70",
    //  VOZ: fuera el retumbe, dentro la presencia.
    "80:-3.0:0:0.70;250:-2.0:1:1.10;1000:1.5:1:0.90;3500:3.0:1:0.90;10000:2.0:2:0.70",
    //  BOMBO: el golpe abajo y el hueco donde vive la caja.
    "60:5.0:0:0.70;250:-4.0:1:1.20;1000:-1.0:1:0.90;3500:2.0:1:0.90;10000:0.0:2:0.70",
    //  AIRE: no toca nada por debajo y abre arriba.
    "80:0.0:0:0.70;250:0.0:1:0.90;1000:0.0:1:0.90;3500:1.0:1:0.90;12000:5.0:2:0.60",
    //  LIMPIA: quita lo que sobra y no sube nada, que es lo que suele faltar.
    "80:-4.0:0:0.70;250:-2.0:1:1.00;1000:0.0:1:0.90;3500:0.0:1:0.90;10000:1.0:2:0.70",
};
