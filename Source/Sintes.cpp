#include "Sintes.h"
#include "Diezmador.h"

#include <cmath>
#include <cstring>
#include <vector>
#include <thread>
#include <atomic>

// ============================================================================
//  Sintes — la parte que suena. Ver Sintes.h para el porque de las zonas.
// ============================================================================
namespace Sintes
{
    using namespace Kits::detail;
    static constexpr double kRate = Kits::kRate;

    //  LA TASA A LA QUE SE GENERA, que no es a la que se guarda.
    //
    //  Ver `Diezmador.h` para el porque entero. El resumen con su cifra: PIANO
    //  ELEC / GLASS pone el modulador de su FM en 14.65 kHz en la raiz +24, y su
    //  banda lateral de orden dos caia plegada en 18.7 kHz DENTRO de la muestra.
    //  Generando a 4x cabe, y el diezmador la tira antes de guardar.
    static constexpr int    kOs     = Diezmador::kOs;
    static constexpr double kRender = kRate * (double) kOs;

    //  DOS MUESTRAS DE GUARDA DETRAS DE CADA ZONA QUE DA VUELTAS.
    //
    //  El cuerpo del bucle tiene periodo exacto `bucleFin - bucleIni`: el
    //  fundido cruzado hace que la muestra que vendria en `bucleFin` sea la de
    //  `bucleIni`. Pero esa muestra NO ESTABA ESCRITA: lo que hay en el indice
    //  `bucleFin` es la primera muestra de la zona siguiente, o sea otra octava.
    //  Y `hermite4` lee `idx-1 .. idx+2`, asi que la ultima fraccion de muestra
    //  antes de dar la vuelta se interpolaba contra material de otra nota.
    //
    //  Medido contra **el mismo cuerpo pegado ocho veces a mano** -que es la
    //  señal que un bucle continuo tiene que dar por definicion-: con las dos
    //  guardas, el largo de vuelta arreglado en `Voice` y el tope de lectura en
    //  `winEnd`, la diferencia cae a **-96.2 dB** contra un liston de -60, y de
    //  las 240 640 muestras solo una difiere -y es de la referencia-. Sin las
    //  guardas, la ultima fraccion antes de la vuelta se interpolaba contra la
    //  octava de al lado.
    //
    //  Dos y no una porque `hermite4` mira dos por delante. No son una copia de
    //  la continuacion cruda -`tmp[zonaLen + k]`- sino de **la cabeza ya
    //  fundida** (`tmp[pre + k]`), que es la muestra que la vuelta va a leer de
    //  verdad: en `k = 0` las dos coinciden por construccion, en `k = 1` ya no.
    static constexpr int kGuardas = 2;

    //  EL RUIDO SE SORTEA A LA TASA DE SALIDA Y SE MANTIENE.
    //
    //  Lo primero que sale es generarlo a 4x y compensar los 6.02 dB que el
    //  diezmado le quita. Se hizo, y la medida lo tumbo: el patron a 16x llama
    //  al sorteo dieciseis veces por muestra y el producto cuatro, asi que **el
    //  ruido de los dos no es el mismo ruido** y la regla del pliegue medía esa
    //  diferencia en vez del pliegue. Medido en PIANO ELEC / GLASS: mediana
    //  **+3.11 dB** de exceso con el sonido ya identico -rms 0.523070 contra
    //  0.523068-, o sea que todo el exceso era la semilla.
    //
    //  Sorteado a 48 kHz y mantenido entre medias, el flujo de la semilla es
    //  funcion pura del indice de SALIDA: el mismo ruido a cualquier factor, sin
    //  compensacion que ajustar y sin una constante que la regla pueda estar
    //  midiendo por error. El escalon del mantenedor le mete su propia caida
    //  -3.9 dB en 24 kHz- y es la MISMA a los dos factores, asi que no entra en
    //  la comparacion.
    //
    //  Y ademas es lo correcto por si solo: el ruido de un instrumento es aire y
    //  raspado, no material de banda ancha hasta 96 kHz.
    struct RuidoMantenido
    {
        //  EL AIRE DE LOS DOS CANALES NO ES EL MISMO AIRE, PERO TAMPOCO SON DOS.
        //  Ver `Kits::detail::kCorrAire` y `Kits::detail::Aire`: la correlacion
        //  se declara, no sale a cero por descuido.
        Aire  r;
        int   paso = 1, quedan = 0;
        float v = 0.0f;

        RuidoMantenido (int semilla, int canal, bool ancho, double fs)
            : r (semilla, canal, ancho), paso (juce::jmax (1, (int) std::lround (fs / kRate))) {}

        float operator()() noexcept
        {
            if (quedan <= 0) { v = r(); quedan = paso; }
            --quedan;
            return v;
        }
    };

    #include "SintesTabla.inc"
    #include "SintesMandos.inc"

    static_assert (sizeof (kMandosDeForma) / sizeof (kMandosDeForma[0]) == kFamilias,
                   "una fila de mas o de menos pone los nombres de una forma en la de al lado");

    const Familia* tabla() { return kTabla; }

    // ------------------------------------------------------------------------
    //  LOS OCHO POR INDICE. Escrito UNA vez: la ficha, el fichero de proyecto y
    //  la puerta acotada preguntan los tres por aqui, y con ocho ramas en cada
    //  sitio la tercera es la que un dia se escribe con el indice cambiado.
    // ------------------------------------------------------------------------
    float valor (const Preset& r, int i)
    {
        switch (i)
        {
            case 0: return r.p1;   case 1: return r.p2;
            case 2: return r.p3;   case 3: return r.p4;
            case 4: return r.atk;  case 5: return r.dec;
            case 6: return r.rel;  case 7: return r.brillo;
            default: return 0.0f;
        }
    }

    void ponValor (Preset& r, int i, float v)
    {
        switch (i)
        {
            case 0: r.p1 = v; break;   case 1: r.p2 = v; break;
            case 2: r.p3 = v; break;   case 3: r.p4 = v; break;
            case 4: r.atk = v; break;  case 5: r.dec = v; break;
            case 6: r.rel = v; break;  case 7: r.brillo = v; break;
            default: break;
        }
    }

    const char* mando (int familia, int i)
    {
        if (! juce::isPositiveAndBelow (i, kMandos)) return "";
        if (i >= 4) return kMandosComunes[i - 4];
        const int f = juce::jlimit (0, kFamilias - 1, familia);
        return kMandosDeForma[(int) kTabla[f].forma][i];
    }

    // ------------------------------------------------------------------------
    //  EL RECORRIDO SE DERIVA DE LA TABLA, en dos poblaciones. Ver Sintes.h.
    //
    //  Se calcula una vez y se guarda: son 16 x 8 numeros y esto lo pregunta el
    //  maquetado de la ficha, o sea muchas veces por segundo.
    // ------------------------------------------------------------------------
    Rango rango (int familia, int i)
    {
        //  Y CON LA INICIALIZACION DEL LENGUAJE Y NO CON UN BOOLEANO. Esto lo
        //  pregunta el maquetado de la ficha -hilo de mensajes- y tambien
        //  `acota`, que corre dentro de `sintetiza`, o sea en el hilo del
        //  cargador: un `static bool hecha` es una carrera entre los dos, con
        //  la tabla a medio llenar durante el par de microsegundos que dura.
        //  Un `static` de funcion con inicializador SI lo garantiza el
        //  lenguaje desde C++11.
        struct Tabla { Rango r[kFamilias][kMandos]; };
        static const Tabla tablaRango = []
        {
            Tabla t {};
            //  Los cuatro comunes, de las 384: significan lo mismo en las
            //  veinticuatro formas, asi que su limite es musical y no de la forma.
            Rango comun[4];
            for (int k = 0; k < 4; ++k) comun[k] = { 1.0e30f, -1.0e30f };
            for (int f = 0; f < kFamilias; ++f)
                for (int pz = 0; pz < kPresets; ++pz)
                    for (int k = 0; k < 4; ++k)
                    {
                        const float v = valor (kTabla[f].p[pz], 4 + k);
                        comun[k].lo = juce::jmin (comun[k].lo, v);
                        comun[k].hi = juce::jmax (comun[k].hi, v);
                    }

            for (int f = 0; f < kFamilias; ++f)
            {
                //  Y los cuatro de forma, de las dieciseis filas de SU familia.
                for (int k = 0; k < 4; ++k)
                {
                    Rango r { 1.0e30f, -1.0e30f };
                    for (int pz = 0; pz < kPresets; ++pz)
                    {
                        const float v = valor (kTabla[f].p[pz], k);
                        r.lo = juce::jmin (r.lo, v);
                        r.hi = juce::jmax (r.hi, v);
                    }
                    //  UN MANDO QUE NO SE MUEVE ES PEOR QUE NO TENERLO, y una
                    //  familia cuyas dieciseis filas escriben el mismo numero
                    //  dejaria el suyo clavado. Hoy no pasa en ninguna de las
                    //  sesenta y cuatro -medido- y esto es lo que hace que una
                    //  fila nueva no pueda crearlo sin que nadie se entere.
                    if (r.hi - r.lo < 1.0e-6f) r.hi = r.lo + juce::jmax (1.0e-3f, std::abs (r.lo));
                    t.r[f][k] = r;
                }
                for (int k = 0; k < 4; ++k) t.r[f][4 + k] = comun[k];
            }
            return t;
        }();

        const int f = juce::jlimit (0, kFamilias - 1, familia);
        return juce::isPositiveAndBelow (i, kMandos) ? tablaRango.r[f][i] : Rango { 0.0f, 1.0f };
    }

    Preset acota (int familia, const Preset& r)
    {
        Preset out = r;
        for (int i = 0; i < kMandos; ++i)
        {
            const auto ra = rango (familia, i);
            const float v = valor (r, i);
            //  Un NaN de un fichero a medio escribir no lo tapa un jlimit:
            //  comparar con NaN siempre es falso. Es la misma barrera que ya
            //  esta en `Voice::start` y en `fastTanh`.
            ponValor (out, i, std::isfinite (v) ? juce::jlimit (ra.lo, ra.hi, v) : ra.lo);
        }
        return out;
    }

    juce::String nombreDe (int familia, int preset)
    {
        const int f = juce::jlimit (0, kFamilias - 1, familia);
        const int p = juce::jlimit (0, kPresets  - 1, preset);
        return juce::String (kTabla[f].nombre) + " " + kTabla[f].p[p].nombre;
    }

    //  LAS CUATRO CATEGORIAS Y EL ORDEN DE MENU. Ver la declaracion.
    //
    //  El reparto es por FUENTE y no por registro: donde nace el sonido, que es
    //  lo que alguien busca cuando abre el menu -«un piano», «algo de cuerda»-.
    //  Por registro saldria BAJOS con CUERDA PULS y SUBS con MAZOS, que es una
    //  lista de graves y no una familia de instrumentos.
    //
    //  ARCO Y PUA y no «CUERDAS»: CUERDAS es el nombre de UNA de las cuatro que
    //  contiene, y una categoria que se llama igual que su primera fila no se
    //  lee como categoria.
    namespace
    {
        const char* const kCatNombres[kCategorias] =
            { "SINTESIS", "TECLAS", "ARCO Y PUA", "SOPLO Y METAL" };

        //  Indices de `kTabla`, en el orden en que se enseñan.
        constexpr int kOrden[kFamilias] =
        {
            0, 1, 9, 5, 16, 17,      // SINTESIS      BAJOS SUBS LEADS COLCHONES FM SYNC
            2, 3, 13, 12, 18, 19,    // TECLAS        PIANO ELEC ORGANOS CLAVES MAZOS PIANOS ACORDEON
            4, 6, 11, 15, 20, 21,    // ARCO Y PUA    CUERDAS PLUCKS CUERDA PULS ARPAS SITAR CELLOS
            14, 10, 8, 7, 22, 23     // SOPLO Y METAL VIENTOS COROS METALES CAMPANAS CANAS TUBOS
        };

        //  Y la vuelta, que es la que contesta `categoriaDe`. Se deriva de
        //  `kOrden` en vez de escribirse: dos tablas que dicen lo mismo son dos
        //  reglas, y la que se quede vieja pondria un nombre en el grupo
        //  equivocado sin que nada falle.
        int catDe (int familia)
        {
            for (int i = 0; i < kFamilias; ++i)
                if (kOrden[i] == familia) return i / (kFamilias / kCategorias);
            return 0;
        }
    }

    const char* const* categorias()      { return kCatNombres; }
    const int*         ordenDeMenu()     { return kOrden; }
    int                categoriaDe (int familia)
    {
        return catDe (juce::jlimit (0, kFamilias - 1, familia));
    }

    namespace
    {
        //  Pulso limitado en banda a partir de DOS sierras. Es el mismo truco
        //  que ya usa sqrBl para el caso ancho=0.5, generalizado: dos sierras
        //  desplazadas se restan y lo que queda es un pulso con su polyBLEP
        //  puesto en los dos flancos.
        inline float pulsoBl (double ph, double inc, double ancho) noexcept
        {
            const double h = (ph + ancho >= 1.0) ? ph + ancho - 1.0 : ph + ancho;
            return sawBl (ph, inc) - sawBl (h, inc);
        }

        //  DESAFINACIONES DESIGUALES, Y FASES DESIGUALES.
        //
        //  Cuatro osciladores separados por la MISMA cantidad de cents baten
        //  todos con el mismo periodo, asi que cada cierto tiempo se ponen en
        //  contrafase a la vez y la suma se cae a casi nada. Medido en el motor
        //  con COLCHONES PWM PAD: una nota sostenida bajaba **35.5 dB** entre su
        //  maximo y su minimo, o sea que desaparecia sola cada segundo y medio.
        //  No era el bucle - era el pad batiendo consigo mismo.
        //
        //  Con separaciones inconmensurables los batidos no coinciden nunca y el
        //  hueco mas profundo es mucho mas corto. Es lo que hace una supersaw de
        //  verdad, y por eso no basta con "repartir del -1 al +1".
        constexpr float kDesigual[8] =
            { 0.0f, -1.000f, 0.618f, -0.414f, 0.883f, -0.732f, 0.271f, -0.947f };

        //  Y CENTRADO, que es lo que faltaba y lo que el banco encontro.
        //
        //  Estos ocho numeros son inconmensurables a proposito -ver arriba- pero
        //  NO SUMAN CERO, y cada familia usa un trozo distinto: los siete
        //  primeros suman -0.374, los cuatro primeros -0.796 y los tres primeros
        //  -0.382. O sea que el conjunto entero sale desafinado EN BLOQUE, tanto
        //  mas cuanta mas dispersion tenga el preset. Medido: **CUERDAS -11
        //  cents en la raiz** y **COLCHONES +14**, con un liston de 5.
        //
        //  No se toca la tabla -los batidos son lo que son- sino que se le resta
        //  la media del TROZO QUE SE USA, que es lo unico que deja el centro del
        //  conjunto donde dice la nota. Un instrumento once cents bajo no suena
        //  mal solo: suena mal CONTRA los demas.
        inline float desigual (int k, int n) noexcept
        {
            float media = 0.0f;
            for (int i = 0; i < n; ++i) media += kDesigual[i];
            return kDesigual[juce::jlimit (0, 7, k)] - media / (float) juce::jmax (1, n);
        }

        //  CUANTO DE UNA PIEZA SUENA EN ESTE CANAL, y como se hace el ancho.
        //
        //  La regla entera es una: **lo que ya es plural se reparte; lo que es
        //  singular saca el ancho del ruido y de las envolventes**. Siete
        //  sierras desafinadas no necesitan que nadie invente nada, solo que se
        //  sienten en sitios distintos del atril.
        //
        //  Lo que NO se hace, y va escrito porque es lo primero que sale:
        //   · retardo entre canales — es un peine en cuanto alguien escucha en
        //     mono, y un groovebox se toca en el altavoz de un telefono;
        //   · todo-paso de fase aleatoria — es el coro barato, y se oye;
        //   · copiar y desafinar — eso es un chorus, no un instrumento ancho.
        //
        //  Potencia constante, y con la raiz de dos delante a proposito: una
        //  pieza CENTRADA (x = 0) sale con ganancia 1.0 en los dos canales, o
        //  sea **exactamente lo que salia en mono**, y una pieza abierta del
        //  todo reparte la misma energia en un solo lado. Sin la raiz de dos,
        //  poner ancho bajaria el volumen de todo el instrumento 3 dB.
        inline float ladoDe (int canal, double x) noexcept
        {
            const double a = juce::MathConstants<double>::pi * 0.25
                           * (1.0 + juce::jlimit (-1.0, 1.0, x));
            return (float) ((canal == 1) ? std::sin (a) : std::cos (a))
                 * juce::MathConstants<float>::sqrt2;
        }

        //  LAS DOS FORMAS QUE SE QUEDAN EN MONO, BIT A BIT, y por que.
        //
        //  Un grave descorrelado pierde hasta 3 dB al sumarse en mono -que es lo
        //  que hace el altavoz de un telefono, y un groovebox se toca ahi- y
        //  ademas mueve de sitio el unico elemento que tiene que estar clavado
        //  en el centro. No es una excepcion a la regla del ancho: es una regla
        //  con su propio liston (`r >= 0.9999` y perdida en mono 0.00 dB).
        inline bool monoDeVerdad (Forma f) noexcept { return f == fBajo || f == fSub; }

        //  Y las fases de arranque por la proporcion aurea: arrancar todos en
        //  cero es un pico enorme en la primera muestra y ademas los pone de
        //  acuerdo justo cuando el ataque los esta destapando.
        inline double faseDe (int k) noexcept
        {
            const double x = 0.6180339887 * (double) (k + 1);
            return x - std::floor (x);
        }

        //  A QUE VELOCIDAD VA EL LFO DE ESTA FAMILIA, escrito UNA vez.
        //
        //  Estaba metido dentro de cada rama del `switch` -`5.6 / fs` en el
        //  organo, `P.p3 / fs` en las cuerdas...- y ahora hacen falta DOS sitios
        //  mas que lo sepan: el que cuadra el largo del bucle con su periodo y
        //  el que decide si se congela. Escrito en tres sitios serian tres
        //  reglas, y la que se quedara vieja dejaria un bucle cuadrado contra
        //  una velocidad que el generador ya no usa.
        //
        //  Cero significa «esta forma no lleva LFO».
        inline double lfoHzDe (Forma forma, const Preset& P) noexcept
        {
            switch (forma)
            {
                case fOrgano:  return 5.6;
                case fCuerdas: return (double) P.p3;
                case fColchon: return (double) P.p2 * 0.7;
                case fLead:    return 4.2;
                case fCoro:    return 5.1;
                case fFlauta:  return 5.4;
                //  Y las dos nuevas que llevan LFO. CELLOS vibra como cualquier
                //  instrumento de arco; TUBOS no vibra: le TIEMBLA EL VIENTO, y
                //  por eso va a menos de un hercio -un fuelle no es un vibrato-.
                case fCello:   return 4.8;
                case fTubo:    return 0.8;
                default:       return 0.0;
            }
        }

        inline float limita (float x) noexcept
        {
            return soft (juce::jlimit (-2.5f, 2.5f, x));
        }

        //  Los cinco formantes de las vocales, en Hz. De las tablas de siempre
        //  (Peterson-Barney redondeadas): a e i o u.
        const double kVocal[5][3] =
        {
            { 730.0, 1090.0, 2440.0 }, { 530.0, 1840.0, 2480.0 },
            { 270.0, 2290.0, 3010.0 }, { 570.0,  840.0, 2410.0 },
            { 300.0,  870.0, 2240.0 }
        };
    }

    // ------------------------------------------------------------------------
    //  UNA ZONA: una raiz y una capa.
    //
    //  `capa` no es un volumen. La capa suave abre el filtro menos de la mitad
    //  y baja el indice de FM, asi que lo que cambia es el TIMBRE - que es lo
    //  que hace que un piano electrico responda al toque en vez de sonar igual
    //  mas bajo. La prueba lo mide con dos numeros por esto mismo.
    // ------------------------------------------------------------------------
    //  EL GENERADOR, A LA TASA QUE SE LE DIGA.
    //
    //  `fs` entra POR ARGUMENTO y no por miembro ni por constante, que es la
    //  misma razon ya escrita en `Voice::updateAntiAlias`: *un orden que hay que
    //  respetar es un fallo esperando*. Y ademas hace que la auditoria pueda
    //  pedir el MISMO generador a 16x para usarlo de patron sin una sola rama
    //  nueva — una rama aparte para el patron mediria la rama y no el producto.
    //  Y `canal` -0 izquierda, 1 derecha- porque el ANCHO SE GENERA, no se
    //  procesa. Ver `ladoDe`: cada forma reparte SUS PROPIAS piezas entre los
    //  dos lados, asi que un canal es una pasada entera del generador con otro
    //  reparto y otra semilla de ruido. Cuesta el doble de tiempo de sintesis y
    //  esa era la decision; lo que no cuesta es una sola multiplicacion en el
    //  hilo de audio, porque `Voice` ya sabia leer dos canales.
    static void rindeCrudo (float* d, int len, const Familia& F, const Preset& P,
                            double hz, int capa, int semilla, float congelaEn,
                            double fs, double lfoHz = -1.0, int canal = 0)
    {
        //  DOS SEMILLAS DE RUIDO, que es la descorrelacion mas honesta que hay:
        //  el aire de los dos canales no es el mismo aire. En las nueve formas
        //  con ruido esto es TODO el ancho que hace falta, y no cuesta nada.
        //
        //  BAJOS y SUBS se quedan con la misma: ver `monoDeVerdad`.
        const bool  anchoOk = ! monoDeVerdad (F.forma);
        RuidoMantenido rnd (semilla, canal, anchoOk, fs);

        //  Y LA PUA DE LA CUERDA TIENE SU PROPIO SORTEO, compartido por los dos
        //  canales. El estado inicial del retardo de Karplus-Strong no es aire:
        //  es LA CUERDA. Sorteandolo del mismo `rnd` que el aire, los dos
        //  canales tendrian cuerdas distintas -o sea dos guitarras- en vez de
        //  una guitarra con dos micros, que es lo que se busca.
        Rng pua (semilla ^ 0x7C4B);

        //  EL LFO, CUADRADO O CONGELADO. Ver `largoBucle` para el porque.
        //
        //  Quien llama puede pasar la velocidad ya CUADRADA con el largo del
        //  bucle -de modo que el cuerpo sea periodico tambien para el LFO- y un
        //  cero significa CONGELADO. Sin argumento se usa la de la tabla, que es
        //  lo que quieren las siete formas que no sostienen.
        const double lfoUsa = (lfoHz >= 0.0) ? lfoHz : lfoHzDe (F.forma, P);
        const double incLfo = lfoUsa / fs;
        Svf f1, f2, f3;
        //  SIN ESTO EL FILTRO CORTA CUATRO VECES MAS ABAJO de lo que se le pide
        //  y todo suena apagado sin que nada lo cante. Ver `Kits::detail::Svf`.
        f1.prepara (fs); f2.prepara (fs); f3.prepara (fs);

        //  LA CAPA ES UNA FRACCION Y NO UNA BANDERA.
        //
        //  Con `duro = (capa == 1)` la tercera capa habria salido identica a la
        //  segunda -y el banco lo habria cantado como dos zonas iguales-. Los
        //  cuatro numeros se interpolan entre los MISMOS extremos que tenian:
        //  `cap = 0` da exactamente la suave de antes y `cap = 1` la fuerte, asi
        //  que las dos puntas del recorrido no se mueven y lo unico que aparece
        //  es el escalon del medio.
        const float cap    = (kCapas > 1) ? (float) capa / (float) (kCapas - 1) : 1.0f;
        //  LA FUERZA INTERPOLA EN GEOMETRICO Y NO EN LINEAL, y la razon es que
        //  el oido cuenta en decibelios. Lineal, 0.52 -> 0.76 -> 1.00 son
        //  **+3.3 dB y luego +2.4**: el primer escalon se lleva el 57 % del
        //  recorrido y el segundo el 43, o sea que la tercera capa reparte peor
        //  de lo que podria. Geometrico, 0.52 -> 0.721 -> 1.00 son +2.85 y
        //  +2.85, clavados.
        const float fuerza = 0.52f * std::pow (1.0f / 0.52f, cap);
        //  Y LOS TRES DE TIMBRE, EN GEOMETRICO TAMBIEN. Con la fuerza
        //  geometrica y estos lineales seguian saliendo escalones de 82 y 85 %
        //  del recorrido en CLAVES y CUERDA PULS, que son las dos familias donde
        //  el nivel lo pone el CONTENIDO -cuanta cuerda se excita, cuanto pulso
        //  pasa el paso banda- y no la ganancia. El oido cuenta en razones para
        //  todo, no solo para el volumen.
        const float brillo = P.brillo * (0.45f * std::pow (1.0f / 0.45f, cap));
        const float indice = 0.42f * std::pow (1.0f / 0.42f, cap);
        //  Y EL TERCER MANDO DE LA CAPA, que hizo falta despues de medir: en
        //  media familia el filtro no puede cambiar el timbre porque no hay
        //  nada que filtrar -un seno, tres parciales, ocho barras- y con la
        //  capa metida solo en el corte, seis familias median centroide x1.00.
        //  Esto escala el CONTENIDO: armonicos de mas, ruido de mas, parciales
        //  de mas. Ver Tests/instr.py, que mide las dos cosas a la vez.
        const float capaMix = 0.30f * std::pow (1.0f / 0.30f, cap);

        const double inc  = hz / fs;

        //  El estado del quita-continua. Ver abajo, donde se aplica.
        const float dcR = (float) (1.0 - juce::MathConstants<double>::twoPi * 5.0 / fs);
        float dcX = 0.0f, dcY = 0.0f;

        //  EL TECHO DE PARCIALES NO SUBE CON LA TASA, y eso es una decision.
        //
        //  `nyq` valia `kRate * 0.48` y lo primero que sale es ponerlo en
        //  `fs * 0.48`. No vale: un parcial por encima de la banda util es CPU
        //  tirada -el diezmador lo va a borrar- y uno que caiga DENTRO de la
        //  transicion del filtro saldria a medio volumen segun el numero de
        //  taps, o sea que el timbre dependeria del largo del filtro. El limite
        //  lo pone un numero con razon -la banda de paso, que es el Nyquist del
        //  render entre el orden de `soft()`- y no la pared de un filtro.
        const double nyq  = Diezmador::bandaHz (kRate);

        double ph = 0.0, ph2 = 0.0, ph3 = 0.0, ph4 = 0.0, phm = 0.0, lfo = 0.0;
        double arm[16] = {};                      // fases de los aditivos
        for (int k = 0; k < 16; ++k) arm[k] = faseDe (k);
        std::vector<float> cuerda;                // Karplus-Strong
        int    ksPos = 0, ksLen = 0;
        float  ksPrev = 0.0f;

        //  La cuerda: perdida por vuelta, paso bajo del bucle y todo paso que
        //  afina. Los tres se mueven con la tasa; ver el bloque de `fGuitarra`.
        float ksPerd = 1.0f, ksLp = 1.0f, ksAp = 0.0f;
        float ksApX = 0.0f, ksApY = 0.0f;

        //  LA MUESTRA ANTERIOR DEL PORTADOR DE FM. De las veinticuatro formas es
        //  la unica que necesita su propia salida de vuelta: un operador que se
        //  modula a si mismo pasa de seno a sierra de forma continua, y eso no
        //  hay indice ni filtro que lo imite. Vive aqui y no dentro del `case`
        //  porque el `switch` se ejecuta una vez por muestra.
        float fbFm = 0.0f;

        if (F.forma == fGuitarra)
        {
            //  EL PERIODO DEL BUCLE SE REPARTE ENTRE TRES PIEZAS, y hasta ahora
            //  solo se contaba una.
            //
            //  `lround (fs/hz)` cuantizaba el periodo a muestra entera, y encima
            //  el promediador `0.5*(x + ksPrev)` añadia MEDIA MUESTRA de retardo
            //  que nadie restaba. En la raiz +24 salian 92 + 0.5 = 92.5 muestras,
            //  o sea 518.9 Hz contra los 523.25 que tocaban: **-14.4 cents en
            //  toda la octava alta**. La prueba de octavas no lo veia porque
            //  compara bandas logaritmicas y catorce cents caben dentro de una.
            //
            //  Ahora el periodo se reparte: el retardo entero, mas el retardo de
            //  grupo del paso bajo -que se CALCULA en vez de suponerse-, mas un
            //  todo paso de primer orden que se come lo que sobra. Asi la cuerda
            //  afina a cualquier tasa y a cualquier raiz.
            const double periodo = fs / juce::jmax (20.0, hz);

            //  EL PASO BAJO DEL BUCLE, CON SU CORTE EN HERCIOS.
            //
            //  Era `0.5*(x + ksPrev)`, cuyo -3 dB cae en `fs/4`: a 48 kHz son
            //  12 kHz, y a 4x se iria a 48, o sea que la cuerda saldria mucho
            //  mas brillante y con otro decaimiento por armonico solo por
            //  cambiar la tasa. Se fija en los 12 kHz que el promediador tenia a
            //  la tasa de salida: el timbre no se mueve y ya no depende de fs.
            const double a = 1.0 - std::exp (-juce::MathConstants<double>::twoPi * 12000.0 / fs);
            ksLp = (float) a;
            const double gdLp = (1.0 - a) / a;      // retardo de grupo en continua

            //  Lo que queda para el todo paso, dejado entre 0.5 y 1.5 muestras,
            //  que es donde un todo paso de primer orden es exacto y estable.
            ksLen = juce::jmax (2, (int) std::floor (periodo - gdLp - 0.5));
            const double frac = juce::jlimit (0.1, 1.9, periodo - gdLp - (double) ksLen);
            ksAp = (float) ((1.0 - frac) / (1.0 + frac));

            cuerda.assign ((size_t) ksLen, 0.0f);

            //  LA PUA SE GENERA A 48 kHz Y SE SUBE, y esto no es un detalle.
            //
            //  `cuerda[i] = rnd()` no es señal muestreada: es el ESTADO INICIAL
            //  de un retardo. A 4x tendria cuatro veces mas muestras y su
            //  espectro seria blanco hasta 96 kHz en vez de hasta 24, o sea que
            //  la cuerda arrancaria con un chasquido que hoy no tiene. Se sortea
            //  a la tasa de salida y se interpola, que es lo que conserva el
            //  color de la pua.
            for (int i = 0; i < ksLen; ++i) cuerda[(size_t) i] = pua();
            //  El peine que dice DONDE se pulsa. Sin el, todas las pulsaciones
            //  suenan al mismo sitio. `pos` es una fraccion de la cuerda, asi
            //  que escala sola con la tasa.
            const int pos = juce::jlimit (1, ksLen - 1, (int) (P.p2 * (float) ksLen));
            for (int i = ksLen - 1; i >= pos; --i)
                cuerda[(size_t) i] -= cuerda[(size_t) (i - pos)];

            //  Y LA PERDIDA, QUE ES POR MUESTRA Y NO POR SEGUNDO.
            //
            //  `0.998 - p1*0.05` se aplica una vez por vuelta del retardo, y a
            //  4x el retardo da cuatro veces mas vueltas en el mismo tiempo: la
            //  cuerda se apagaria **cuatro veces mas rapido**. No truena, no
            //  suena mal: solo se queda corta, que es el peor sitio donde puede
            //  esconderse un fallo. La raiz cuarta lo deja igual en tiempo real:
            //  0.998 pasa a 0.99950.
            ksPerd = (float) std::pow ((double) (0.998f - P.p1 * 0.05f), kRate / fs);
        }

        //  Y EL TUBO TAPADO DE LAS CANAS, QUE NO ES UN FILTRO SINO UN PEINE.
        //
        //  Un tubo cerrado por un extremo solo deja vivir los armonicos
        //  IMPARES: es lo que hace que un clarinete suene hueco y no brillante,
        //  y es geometria, no timbre. Restar la senal retrasada MEDIO PERIODO
        //  cancela exactamente los pares y dobla los impares, que es la misma
        //  cuenta con otro nombre.
        //
        //  Hacia delante y sin realimentacion a proposito: un peine realimentado
        //  es un modelo de guia de ondas -lo que ya es `fGuitarra`- y ahi la
        //  estabilidad depende de la perdida por vuelta, o sea que un preset mal
        //  puesto entre los 384 se iria a infinito sin que nadie lo viera hasta
        //  oirlo. Este no puede: lo que sale es a lo sumo el doble de lo que
        //  entra, siempre.
        if (F.forma == fCana)
        {
            ksLen = juce::jmax (2, (int) std::lround (fs / juce::jmax (20.0, hz) * 0.5));
            cuerda.assign ((size_t) ksLen, 0.0f);
        }

        for (int n = 0; n < len; ++n)
        {
            const float t = (float) n / (float) fs;

            //  LO QUE SOSTIENE, SOSTIENE DE VERDAD.
            //
            //  Cuadrar el bucle en ciclos enteros arreglo la FASE y dejo un
            //  bache de 9.5 dB en el bajo. El resto no era la fase: era que la
            //  envolvente del FILTRO seguia cayendo dentro del bucle, asi que
            //  cada vuelta reiniciaba el barrido - un "wah" a la velocidad del
            //  bucle, que es exactamente lo que se oye como "hace cosas raras".
            //
            //  Desde el punto de bucle las envolventes se CONGELAN: de ahi en
            //  adelante el sonido es estacionario y la vuelta no cambia nada.
            //  El ataque suena una vez, que es lo que un ataque es.
            //
            //  Los LFO siguen con el tiempo de verdad: un colchon que deja de
            //  moverse deja de ser un colchon, y para su desfase esta el
            //  fundido cruzado - que ahora si puede hacer su trabajo, porque ya
            //  no tiene que tapar una contrafase.
            const float te = (congelaEn > 0.0f) ? juce::jmin (t, congelaEn) : t;
            float v = 0.0f;

            //  La envolvente de amplitud. Lo que SOSTIENE sube y se queda: el
            //  final lo pone la nota (Voice::gate) y no la muestra, que es
            //  justo lo que separa un instrumento de un golpe.
            const float amp = F.sostiene
                ? ((P.atk <= 0.0f) ? 1.0f : juce::jmin (1.0f, t / P.atk))
                : ad (te, P.atk, P.dec);

            switch (F.forma)
            {
                case fBajo:
                {
                    //  p1 desafinacion de la 2a sierra (cents), p2 cuanto abre
                    //  el filtro la envolvente, p3 resonancia, p4 sub seno.
                    const double i2 = inc * std::pow (2.0, (double) P.p1 / 1200.0);
                    ph  += inc; if (ph  >= 1.0) ph  -= 1.0;
                    ph2 += i2;  if (ph2 >= 1.0) ph2 -= 1.0;
                    ph3 += inc * 0.5; if (ph3 >= 1.0) ph3 -= 1.0;
                    const float mezcla = 0.5f * (sawBl (ph, inc) + sawBl (ph2, i2))
                                       + P.p4 * (float) std::sin (juce::MathConstants<double>::twoPi * ph3);
                    const float ef = env (te, juce::jmax (0.02f, P.dec));
                    f1.set (juce::jlimit (40.0, nyq, hz * brillo * (1.0 + (double) (P.p2 * ef))), P.p3);
                    v = limita (1.3f * f1.lp (mezcla));
                    break;
                }

                case fSub:
                {
                    //  p1 2o armonico, p2 saturacion, p3 caida de afinacion
                    //  (semitonos al arrancar), p4 aire.
                    const double caida = std::pow (2.0, (double) P.p3 * (double) env (te, 0.045f) / 12.0);
                    const double i1 = inc * caida;
                    ph  += i1;       if (ph  >= 1.0) ph  -= 1.0;
                    ph2 += i1 * 2.0; if (ph2 >= 1.0) ph2 -= 1.0;
                    ph3 += i1 * 3.0; if (ph3 >= 1.0) ph3 -= 1.0;
                    //  LA CAPA SE OYE EN LOS ARMONICOS Y NO EN EL FILTRO. Un
                    //  seno no tiene nada que filtrar: con la capa metida solo
                    //  en el corte, las dos median el MISMO centroide -x1.00- y
                    //  eso es un fader con pasos. Ver Tests/instr.py.
                    const float s = (float) std::sin (juce::MathConstants<double>::twoPi * ph)
                                  + P.p1 * (0.4f + 1.2f * capaMix)
                                        * (float) std::sin (juce::MathConstants<double>::twoPi * ph2)
                                  + (P.p1 * 0.5f + 0.09f) * capaMix
                                        * (float) std::sin (juce::MathConstants<double>::twoPi * ph3);
                    f1.set (juce::jlimit (60.0, nyq, hz * 12.0 * (double) brillo), 0.7f);
                    v = limita (s * (1.0f + P.p2 * (0.6f + 2.4f * capaMix)))
                      + P.p4 * f1.hp (rnd()) * 0.25f * fuerza;
                    break;
                }

                case fEp:
                {
                    //  FM de dos operadores. p1 razon, p2 indice, p3 caida del
                    //  indice, p4 martillo. El indice cayendo es lo que hace
                    //  que un Rhodes empiece con campana y acabe con seno.
                    //  UN SOLO OSCILADOR: el ancho sale de separar la CAIDA
                    //  DEL INDICE y el martillo un 4% entre los dos lados. Es lo
                    //  que separa dos microfonos delante de la misma pua, y no
                    //  un chorus: la nota es la misma nota en los dos canales.
                    //  Y LA FASE DEL MODULADOR, que es lo que de verdad abre
                    //  un solo portador. Con el 4% de la caida del indice como
                    //  unico ancho, los dieciseis PIANO ELEC median **r = 0.999**:
                    //  un ancho que no existe. Un cuarto de ciclo de modulador
                    //  cambia la FORMA de onda sin mover ni un cent la
                    //  afinacion -las bandas laterales estan en las mismas
                    //  frecuencias, con otra fase- y no es ni un retardo ni un
                    //  desafine. El portador arranca igual en los dos, asi que
                    //  el fundamental sigue sumandose en fase en mono.
                    const float sesgo = (canal == 1) ? 1.04f : 0.96f;
                    //  0.12 de ciclo, y los dos extremos estan medidos: con
                    //  0.25 la onda de los dos canales ya no se parecia en nada
                    //  -r = 0.078 y **-2.68 dB** al sumarse en mono- y con
                    //  0.0625 se quedaba corta en los presets de indice bajo
                    //  -MELLOW a r = 0.9902, por encima del liston-.
                    //  Y ESCALADA CON EL INDICE, que es lo que decide cuantas
                    //  bandas laterales hay que mover. Con 0.12 fijo, los
                    //  presets de indice alto -DX7 con 5.6, HARD EP con 7.4- se
                    //  pasaban al otro lado del intervalo: **-1.64 dB** en mono.
                    //  0.144/indice deja MELLOW (1.2) en 0.12 y HARD EP en 0.02.
                    if (n == 0 && canal == 1)
                        phm = juce::jlimit (0.02, 0.15, 0.144 / (double) juce::jmax (0.2f, P.p2));
                    phm += inc * (double) P.p1; if (phm >= 1.0) phm -= 1.0;
                    const float ei = env (te, juce::jmax (0.02f, P.p3 * sesgo));
                    const double mod = (double) (P.p2 * indice * ei)
                                     * std::sin (juce::MathConstants<double>::twoPi * phm);
                    ph += inc; if (ph >= 1.0) ph -= 1.0;
                    v = (float) std::sin (juce::MathConstants<double>::twoPi * ph + mod);
                    f1.set (juce::jlimit (200.0, nyq, 3000.0 * (double) brillo), 1.2f);
                    v += P.p4 * f1.bpf (rnd()) * env (te, 0.006f * sesgo) * 2.4f * fuerza;
                    break;
                }

                case fOrgano:
                {
                    //  Aditivo de ocho barras. p1 inclinacion (brillo), p2
                    //  balance impares/pares, p3 percusion del 3er armonico,
                    //  p4 leslie.
                    lfo += incLfo; if (lfo >= 1.0) lfo -= 1.0;
                    //  EL LESLIE GIRA, o sea que los dos lados no lo ven a la
                    //  vez. 0.15 de vuelta entre canales y no 0.5: media vuelta
                    //  es CONTRAFASE, y en cuanto alguien escucha en mono el
                    //  vibrato se cancela solo. 0.15 se oye girar y sobrevive a
                    //  la suma.
                    const double gira = lfo + ((canal == 1) ? 0.075 : -0.075);
                    const double vib = 1.0 + (double) P.p4 * 0.004
                                             * std::sin (juce::MathConstants<double>::twoPi * gira);
                    static const int mult[8] = { 1, 2, 3, 4, 6, 8, 12, 16 };
                    float suma = 0.0f;
                    for (int k = 0; k < 8; ++k)
                    {
                        const double fk = hz * (double) mult[k] * vib;
                        if (fk >= nyq) continue;
                        arm[k] += fk / fs; if (arm[k] >= 1.0) arm[k] -= 1.0;
                        const float par = (mult[k] % 2 == 0) ? (1.0f - P.p2) : P.p2;
                        //  Y la capa fuerte tira de las barras de arriba, que
                        //  es lo que hace un organista al empujar: con la
                        //  inclinacion fija las dos capas median x1.40 de
                        //  centroide solo por el filtro, y el organo es aditivo.
                        const float incl = -1.0f + (P.p1 * 0.9f) * (0.45f + 0.55f * capaMix);
                        const float a = std::pow ((float) (k + 1), incl) * (0.55f + par);
                        float g = a;
                        if (k == 2) g *= 1.0f + P.p3 * 6.0f * env (te, 0.09f) * fuerza;
                        //  Y LAS BARRAS SE REPARTEN 1-3-6-12 contra 2-4-8-16,
                        //  que es como estan cableadas de verdad las dos mitades
                        //  de un tirador: no es un reparto inventado, es el que
                        //  la tabla `mult` ya tenia.
                        g *= ladoDe (canal, (k % 2 == 0) ? -0.55 : 0.55);
                        suma += g * (float) std::sin (juce::MathConstants<double>::twoPi * arm[k]);
                    }
                    f1.set (juce::jlimit (200.0, nyq, 2200.0 * (double) brillo), 0.7f);
                    v = limita (0.55f * f1.lp (suma));
                    break;
                }

                case fCuerdas:
                {
                    //  Siete sierras desafinadas. p1 dispersion en cents, p2
                    //  vibrato, p3 su velocidad, p4 ruido de arco.
                    lfo += incLfo; if (lfo >= 1.0) lfo -= 1.0;
                    const double vib = std::pow (2.0, (double) P.p2 * 0.01
                                        * std::sin (juce::MathConstants<double>::twoPi * lfo));
                    float suma = 0.0f;
                    for (int k = 0; k < 7; ++k)
                    {
                        const double det = std::pow (2.0, (double) P.p1 * (double) desigual (k, 7) / 1200.0);
                        const double ik = inc * det * vib;
                        arm[k] += ik; if (arm[k] >= 1.0) arm[k] -= 1.0;
                        //  LOS SIETE ATRILES. Se reparten por `kDesigual`, que es
                        //  **el mismo vector que ya decide su desafinacion**: el
                        //  que suena mas arriba se sienta mas a un lado, que es
                        //  lo que pasa en una cuerda de verdad. Cero osciladores
                        //  nuevos.
                        suma += ladoDe (canal, kApertura * (double) kDesigual[k]) * sawBl (arm[k], ik);
                    }
                    f1.set (juce::jlimit (200.0, nyq, hz * 9.0 * (double) brillo), 0.6f);
                    f2.set (juce::jlimit (400.0, nyq, 2600.0), 0.8f);
                    v = limita (0.22f * f1.lp (suma)
                                + P.p4 * f2.bpf (rnd()) * (0.15f + 1.10f * capaMix));
                    break;
                }

                case fColchon:
                {
                    //  Cuatro pulsos con el ancho moviendose. p1 profundidad,
                    //  p2 velocidad, p3 paso alto, p4 dispersion.
                    lfo += incLfo; if (lfo >= 1.0) lfo -= 1.0;
                    //  Y el ancho lo estrecha la capa fuerte: un pulso ancho
                    //  es casi un seno y uno estrecho tiene todos los armonicos,
                    //  asi que aqui es donde se oye el toque. Con el ancho fijo
                    //  las dos capas median x1.03.
                    //  Y CON SUELO. Un pulso de 0.055 de ancho tiene la
                    //  quinta parte de la amplitud de uno de 0.27: con la capa
                    //  fuerte estrechandolo tanto, el propio movimiento del
                    //  ancho valia 14 dB de la nota. Entre 0.10 y 0.90 el timbre
                    //  cambia igual y el nivel no se cae.
                    const double base = 0.5 - 0.22 * (double) capaMix;
                    const double ancho = juce::jlimit (0.10, 0.90,
                        base + (double) P.p1 * 0.22 * std::sin (juce::MathConstants<double>::twoPi * lfo));
                    float suma = 0.0f;
                    for (int k = 0; k < 4; ++k)
                    {
                        const double det = std::pow (2.0, (double) P.p4 * (double) desigual (k, 4) / 1200.0);
                        const double ik = inc * det;
                        arm[k] += ik; if (arm[k] >= 1.0) arm[k] -= 1.0;
                        //  Los cuatro pulsos, repartidos por el mismo vector que
                        //  los desafina. Ver CUERDAS.
                        suma += ladoDe (canal, kApertura * (double) kDesigual[k]) * pulsoBl (arm[k], ik, ancho);
                    }
                    f1.set (juce::jlimit (200.0, nyq, hz * 7.0 * (double) brillo), 0.5f);
                    f2.set (juce::jlimit (30.0, 900.0, hz * (double) P.p3 * 2.0 + 40.0), 0.7f);
                    v = limita (0.26f * f2.hp (f1.lp (suma)));
                    break;
                }

                case fPluck:
                {
                    //  p1 sierra contra cuadrada, p2 caida del filtro, p3
                    //  resonancia, p4 cuerpo una octava abajo.
                    ph  += inc;       if (ph  >= 1.0) ph  -= 1.0;
                    ph2 += inc * 0.5; if (ph2 >= 1.0) ph2 -= 1.0;
                    //  El cuerpo de una octava abajo a un lado: es lo unico
                    //  plural que un pluck tiene.
                    const float osc = (1.0f - P.p1) * sawBl (ph, inc) + P.p1 * sqrBl (ph, inc)
                                    + P.p4 * ladoDe (canal, -0.75) * sawBl (ph2, inc * 0.5) * 0.6f;
                    //  DOS PASTILLAS: la envolvente del filtro cae un 3%
                    //  distinta en cada lado. Un solo oscilador no se puede
                    //  repartir, asi que el ancho sale de lo unico que se mueve.
                    //  Un 22% y no un 3%: con el 3% los dieciseis PLUCKS median
                    //  r = 0.99 -dos canales iguales- y con el 12% cuatro se
                    //  quedaban en 0.984, justo por encima del liston. Dos
                    //  pastillas de verdad no se parecen tanto.
                    const float ef = env (te, juce::jmax (0.01f, P.p2 * ((canal == 1) ? 1.22f : 0.78f)));
                    f1.set (juce::jlimit (60.0, nyq, hz * brillo * (1.0 + 14.0 * (double) ef)), P.p3);
                    v = limita (1.2f * f1.lp (osc));
                    break;
                }

                case fCampana:
                {
                    //  FM inarmonica mas dos parciales sueltos. p1 razon del
                    //  modulador, p2 indice, p3 y p4 los dos parciales. Lo que
                    //  hace campana a una campana es que NADA es multiplo.
                    phm += inc * (double) P.p1; if (phm >= 1.0) phm -= 1.0;
                    const double mod = (double) (P.p2 * indice * env (te, P.dec * 0.35f))
                                     * std::sin (juce::MathConstants<double>::twoPi * phm);
                    ph  += inc;                    if (ph  >= 1.0) ph  -= 1.0;
                    ph2 += inc * (double) P.p3;    if (ph2 >= 1.0) ph2 -= 1.0;
                    ph3 += inc * (double) P.p4;    if (ph3 >= 1.0) ph3 -= 1.0;
                    v = (float) std::sin (juce::MathConstants<double>::twoPi * ph + mod);
                    //  Y LOS PARCIALES MANDAN, no acompanan. Una campana no es
                    //  una nota con adornos: es un monton de parciales que no
                    //  son multiplos de nada, y el "tono" se lo pone el oido.
                    //  Con estos al 0.42 y al 0.26 esto era un piano electrico
                    //  con la caida larga - medido, 2.05 dB contra VINTAGE.
                    //  El fundamental CENTRADO y los dos parciales a un lado
                    //  cada uno: una campana suena ancha porque sus parciales
                    //  salen de sitios distintos del bronce, no porque nadie le
                    //  meta un retardo.
                    if (hz * (double) P.p3 < nyq)
                        v += 0.95f * ladoDe (canal, -0.70) * env (te, P.dec * 0.80f)
                             * (float) std::sin (juce::MathConstants<double>::twoPi * ph2);
                    if (hz * (double) P.p4 < nyq)
                        v += 0.70f * ladoDe (canal, 0.70) * env (te, P.dec * 0.55f)
                             * (float) std::sin (juce::MathConstants<double>::twoPi * ph3);
                    //  Y el fundamental se apaga antes que ellos, que es lo que
                    //  hace que una campana "cante" mas agudo segun decae.
                    v *= 0.55f + 0.45f * env (te, P.dec * 0.30f);
                    break;
                }

                case fMetales:
                {
                    //  El filtro SE PASA y vuelve: un metal no abre y se queda,
                    //  pega un empujon y se asienta. p1 sobrepaso, p2 tiempo,
                    //  p3 desafinacion, p4 soplo.
                    const float sub = juce::jmin (1.0f, te / juce::jmax (0.005f, P.p2));
                    const float over = sub * (1.0f + P.p1 * (1.0f - sub) * 2.4f);
                    //  PULSO ESTRECHO Y NO UNA PILA DE SIERRAS.
                    //
                    //  Con tres sierras esto era CUERDAS con otro filtro, y el
                    //  banco lo canto: VIOLA contra HORN, 2.35 dB. Un metal es
                    //  una columna de aire con un labio: pulso estrecho, y el
                    //  ancho ABRIENDOSE con la envolvente - que es la otra mitad
                    //  del "empujon" de un metal, ademas del filtro.
                    const double ancho = juce::jlimit (0.05, 0.48, 0.10 + 0.34 * (double) over);
                    float suma = 0.0f;
                    for (int k = 0; k < 3; ++k)
                    {
                        const double det = std::pow (2.0, (double) P.p3 * (double) desigual (k, 3) / 1200.0);
                        const double ik = inc * det;
                        arm[k] += ik; if (arm[k] >= 1.0) arm[k] -= 1.0;
                        //  Los tres, repartidos por el vector que los desafina.
                        suma += ladoDe (canal, kApertura * (double) kDesigual[k]) * pulsoBl (arm[k], ik, ancho);
                    }
                    f1.set (juce::jlimit (120.0, nyq, hz * brillo * (1.5 + 7.0 * (double) over)), 1.1f);
                    f2.set (juce::jlimit (600.0, nyq, 3400.0), 1.0f);
                    v = limita (0.30f * f1.lp (suma)
                                + P.p4 * f2.bpf (rnd()) * (0.2f + 1.0f * capaMix));
                    break;
                }

                case fLead:
                {
                    //  p1 ancho del pulso, p2 su modulacion, p3 resonancia,
                    //  p4 sub cuadrada.
                    lfo += incLfo; if (lfo >= 1.0) lfo -= 1.0;
                    const double ancho = juce::jlimit (0.04, 0.96, (double) P.p1
                        + (double) P.p2 * 0.4 * std::sin (juce::MathConstants<double>::twoPi * lfo));
                    //  Y UNA QUINTA ENCIMA, fija. Un pulso con filtro es lo
                    //  mismo que un bajo con filtro con otros numeros -SAW BS
                    //  contra FAT LEAD, 2.62 dB- y lo que hace que un lead
                    //  corte por encima de una mezcla es que no es una sola
                    //  nota. La quinta es consonante con cualquier acorde.
                    const double i3 = inc * 1.498307;
                    ph  += inc;       if (ph  >= 1.0) ph  -= 1.0;
                    ph2 += inc * 0.5; if (ph2 >= 1.0) ph2 -= 1.0;
                    ph3 += i3;        if (ph3 >= 1.0) ph3 -= 1.0;
                    //  LA QUINTA A UN LADO y la nota en el centro. El sub se
                    //  queda centrado a proposito: es grave, y lo grave no se
                    //  abre (ver `monoDeVerdad`).
                    const float osc = pulsoBl (ph, inc, ancho)
                                    + 0.42f * ladoDe (canal, -0.60) * pulsoBl (ph3, i3, ancho * 0.7)
                                    + P.p4 * sqrBl (ph2, inc * 0.5) * 0.5f;
                    f1.set (juce::jlimit (120.0, nyq, hz * brillo * 3.0), P.p3);
                    v = limita (0.62f * f1.lp (osc));
                    break;
                }

                case fCoro:
                {
                    //  Tres formantes sobre un pulso. p1 vocal, p2 aire, p3
                    //  vibrato, p4 dispersion. Lo que hace voz a una voz no es
                    //  la forma de onda, son las tres bandas fijas.
                    lfo += incLfo; if (lfo >= 1.0) lfo -= 1.0;
                    const double vib = std::pow (2.0, (double) P.p3 * 0.008
                                        * std::sin (juce::MathConstants<double>::twoPi * lfo));
                    float suma = 0.0f;
                    for (int k = 0; k < 3; ++k)
                    {
                        const double det = std::pow (2.0, (double) P.p4 * (double) desigual (k, 3) / 1200.0);
                        const double ik = inc * det * vib;
                        arm[k] += ik; if (arm[k] >= 1.0) arm[k] -= 1.0;
                        //  Tres voces, tres sitios. Y el aire ya viene de su
                        //  propia semilla, que en un coro es la mitad del ancho.
                        suma += ladoDe (canal, kApertura * (double) kDesigual[k]) * sawBl (arm[k], ik);
                    }
                    suma = suma * 0.33f + P.p2 * rnd() * 0.5f * fuerza;
                    const int vocal = juce::jlimit (0, 4, (int) std::lround ((double) P.p1 * 4.0));
                    f1.set (juce::jmin (nyq, kVocal[vocal][0] * (double) brillo), 5.0f);
                    f2.set (juce::jmin (nyq, kVocal[vocal][1] * (double) brillo), 7.0f);
                    f3.set (juce::jmin (nyq, kVocal[vocal][2] * (double) brillo), 9.0f);
                    v = limita (1.6f * f1.bpf (suma) + 1.0f * f2.bpf (suma) + 0.55f * f3.bpf (suma));
                    break;
                }

                case fGuitarra:
                {
                    //  KARPLUS-STRONG: una cuerda de verdad, o sea un retardo
                    //  realimentado con perdidas. Lo que da el timbre es cuanto
                    //  se pierde en cada vuelta (p1) y donde se pulso (p2, ya
                    //  metido en el peine de arriba). p3 cuerpo, p4 ruido.
                    const float x = cuerda[(size_t) ksPos];
                    //  Paso bajo del bucle -corte en Hz, ver arriba- y detras el
                    //  todo paso que afina. El orden importa: el todo paso tiene
                    //  que ver lo mismo que va a dar la vuelta.
                    ksPrev += ksLp * (x - ksPrev);
                    const float apY = ksAp * ksPrev + ksApX - ksAp * ksApY;
                    ksApX = ksPrev; ksApY = apY;
                    cuerda[(size_t) ksPos] = apY * ksPerd;
                    ksPos = (ksPos + 1) % ksLen;
                    //  UNA SOLA CUERDA, CENTRADA -ver `pua`-, y el CUERPO
                    //  con su resonancia un 2% distinta a cada lado. Una caja de
                    //  madera no resuena igual por los dos costados, y eso es
                    //  ancho de verdad sin tocar la cuerda.
                    //  Un 12% y no un 2%, y ademas el cuerpo se reparte: con el
                    //  2% los dieciseis CUERDA PULS median **r = 0.999**. Una
                    //  caja de madera no resuena igual por los dos costados, y
                    //  esa es toda la diferencia que hay entre una guitarra con
                    //  dos micros y una con uno.
                    f1.set (juce::jlimit (90.0, nyq, (220.0 + 900.0 * (double) P.p3)
                                                      * ((canal == 1) ? 1.12 : 0.88)), 2.4f);
                    v = x + P.p3 * ladoDe (canal, -0.5) * f1.bpf (x) * 0.8f;
                    v += P.p4 * rnd() * env (te, 0.004f) * 0.7f * fuerza;
                    //  Y EL PASO BAJO DE SALIDA TAMBIEN SE SEPARA, un 10%.
                    //
                    //  Con el cuerpo solo -12% y repartido- los dieciseis CUERDA
                    //  PULS seguian midiendo **r = 0.99**: en la mitad de los
                    //  presets `p3` vale casi cero, o sea que el cuerpo no pesa y
                    //  la unica diferencia entre canales se quedaba sin sonar. El
                    //  filtro de salida lo ve TODO, asi que ahi si se nota.
                    f2.set (juce::jlimit (400.0, nyq, hz * 12.0 * (double) brillo)
                              * ((canal == 1) ? 1.18 : 0.82), 0.6f);
                    v = limita (f2.lp (v) * 1.4f);
                    break;
                }

                case fMazo:
                {
                    //  Tres parciales inarmonicos con caidas distintas: es lo
                    //  que separa una marimba de un seno. p1 y p2 las razones,
                    //  p3 el golpe, p4 el balance.
                    ph  += inc;                 if (ph  >= 1.0) ph  -= 1.0;
                    ph2 += inc * (double) P.p1; if (ph2 >= 1.0) ph2 -= 1.0;
                    ph3 += inc * (double) P.p2; if (ph3 >= 1.0) ph3 -= 1.0;
                    //  Y LOS DOS INARMONICOS ARRANCAN CON OTRA FASE EN EL
                    //  CANAL DERECHO, que es lo unico que abre un golpe que es
                    //  casi un seno.
                    //
                    //  El intento anterior fue un TUBO RESONADOR -que una
                    //  marimba lleva de verdad- y hubo que quitarlo: con el, dos
                    //  presets de la familia pasaban a ser el mismo sonido -SOFT
                    //  MAL contra TUNED, **1.12 dB** contra un liston de 1.5-
                    //  porque un filtro les ponia el mismo color a los dieciseis.
                    //  La fase no toca el espectro -la distancia entre presets no
                    //  se mueve ni una centesima- y descorrela igual: en SOFT MAL
                    //  los inarmonicos valen el 28 % de la amplitud, que es de
                    //  sobra para bajar de **r = 0.9806** al intervalo.
                    //
                    //  Y no es un desafine ni un retardo: las frecuencias son las
                    //  mismas y el fundamental arranca igual en los dos, asi que
                    //  la nota sigue sumandose en fase en mono.
                    if (n == 0 && canal == 1) { ph2 += 0.31; ph3 += 0.62; }
                    const float fund = (float) std::sin (juce::MathConstants<double>::twoPi * ph);
                    v = fund;
                    //  Los dos parciales de arriba los saca la BAQUETA DURA:
                    //  con ellos fijos las dos capas median centroide x1.00, o
                    //  sea que el toque solo cambiaba el volumen.
                    const float par = P.p4 * (0.25f + 1.30f * capaMix);
                    //  Fundamental centrado, los dos inarmonicos a un lado cada
                    //  uno. Misma figura que CAMPANAS y por la misma razon.
                    if (hz * (double) P.p1 < nyq)
                        v += par * ladoDe (canal, -0.70) * env (te, P.dec * 0.30f)
                             * (float) std::sin (juce::MathConstants<double>::twoPi * ph2);
                    if (hz * (double) P.p2 < nyq)
                        v += par * 0.45f * ladoDe (canal, 0.70) * env (te, P.dec * 0.14f)
                             * (float) std::sin (juce::MathConstants<double>::twoPi * ph3);
                    f1.set (juce::jlimit (400.0, nyq, 2800.0 * (double) brillo), 1.4f);
                    v += P.p3 * ladoDe (canal, 0.80) * f1.bpf (rnd()) * env (te, 0.005f) * 3.0f * (0.2f + 1.1f * capaMix);
                    break;
                }

                case fClav:
                {
                    //  Pulso MUY estrecho por un paso banda: el clavinet es una
                    //  cuerda golpeada y captada, o sea casi un impulso con una
                    //  resonancia encima. p1 ancho, p2 centro, p3 Q, p4 muerte.
                    ph += inc; if (ph >= 1.0) ph -= 1.0;
                    const float osc = pulsoBl (ph, inc, juce::jlimit (0.02, 0.45, (double) P.p1));
                    //  DOS PASTILLAS otra vez, y aqui el numero no es fijo.
                    //  Y EL SESGO VA DESPUES DEL ACOTADO. Dentro, los dos
                    //  canales caian en el mismo tope y CLAVES OCT CLV medía
                    //  **r = 1.0000**: un ancho que el limite se comia.
                    //  Y EL DESPLAZAMIENTO SE MIDE EN ANCHOS DE BANDA Y NO EN
                    //  FRECUENCIA, que es la unica forma de que un numero valga
                    //  para los dieciseis: el ancho de un paso banda es `f/Q`,
                    //  asi que un 6% fijo mueve medio ancho en CLAV (Q = 2) y
                    //  siete anchos en RES CLV (Q = 10). Se midio: con el 6%
                    //  fijo, WAH CLV se caia a **-2.15 dB** en mono y RES CLV a
                    //  **-3.17**, mientras CLAV seguia corto con el 1.5%.
                    //  0.20 y no 0.30: con 0.30, RES CLV -Q = 10 sobre un
                    //  parcial casi puro- se quedaba en **-1.67 dB** en mono.
                    //  Una resonancia estrecha descorrela con muy poco.
                    const double sep = juce::jlimit (0.015, 0.08, 0.20 / (double) juce::jmax (0.5f, P.p3));
                    f1.set (juce::jlimit (150.0, nyq, hz * (double) P.p2 * (double) brillo)
                              * (1.0 + (canal == 1 ? sep : -sep)), P.p3);
                    const float ef = env (te, juce::jmax (0.02f, P.dec * 0.5f));
                    v = limita (1.5f * f1.bpf (osc) * (0.35f + 0.65f * ef));
                    v += P.p4 * rnd() * env (te, 0.003f) * fuerza;
                    break;
                }

                case fFlauta:
                {
                    //  Seno con AIRE, que es lo unico que separa una flauta de
                    //  un seno. p1 cuanto aire, p2 vibrato, p3 cuando entra,
                    //  p4 segundo armonico.
                    lfo += incLfo; if (lfo >= 1.0) lfo -= 1.0;
                    const float entra = juce::jmin (1.0f, juce::jmax (0.0f, te - P.p3) / 0.35f);
                    const double vib = std::pow (2.0, (double) (P.p2 * entra) * 0.006
                                        * std::sin (juce::MathConstants<double>::twoPi * lfo));
                    const double i1 = inc * vib;
                    ph  += i1;       if (ph  >= 1.0) ph  -= 1.0;
                    ph2 += i1 * 2.0; if (ph2 >= 1.0) ph2 -= 1.0;
                    //  EL AIRE ES LA FLAUTA. Con poco, esto es un seno y el
                    //  banco lo dijo: SUBS DEEP contra PICCOLO, 2.17 dB. Y el
                    //  soplo fuerte SOBREsopla -sube el segundo armonico- que es
                    //  lo que hace de verdad un instrumento de viento y lo que
                    //  separa las dos capas, porque un seno no se filtra.
                    //  Y EL SOBRESOPLO A UN LADO. El tubo es uno y el
                    //  fundamental se queda centrado, pero lo que el labio saca
                    //  de mas no sale del mismo sitio. Sin esto VIENTOS media
                    //  **r = 0.99** aunque el aire fuera de dos sorteos: el seno
                    //  se comia la cuenta.
                    v = (float) std::sin (juce::MathConstants<double>::twoPi * ph)
                      + (P.p4 + 0.55f * capaMix) * ladoDe (canal, 0.70)
                            * (float) std::sin (juce::MathConstants<double>::twoPi * ph2);
                    //  Banda ANCHA y alrededor del tercer armonico: el aire de
                    //  una flauta no esta en su fundamental, esta arriba.
                    f1.set (juce::jlimit (300.0, nyq, hz * 3.2 * (double) brillo), 1.1f);
                    const float soplo = 0.55f + 0.45f * env (te, 0.05f);
                    v = limita (0.8f * v
                                + (P.p1 + 0.30f) * f1.bpf (rnd()) * 3.2f * soplo
                                      * (0.35f + 0.9f * capaMix));
                    break;
                }

                case fArpa:
                {
                    //  Aditivo pulsado: doce armonicos y cada uno cae MAS
                    //  rapido que el de abajo, que es lo que hace una cuerda
                    //  pulsada. p1 la inclinacion de esas caidas, p2 cuantos,
                    //  p3 inarmonicidad, p4 el ruido del dedo.
                    //  CUANTOS armonicos, y la capa manda: pulsar fuerte saca
                    //  parciales que pulsar flojo no saca. Con el numero fijo
                    //  las dos capas median x1.00.
                    const int cuantos = juce::jlimit (2, 12,
                        (int) std::lround ((double) P.p2 * 12.0 * (double) (0.35f + 0.85f * capaMix)));
                    float suma = 0.0f;
                    for (int k = 0; k < cuantos; ++k)
                    {
                        const double mult = (double) (k + 1)
                                          * std::sqrt (1.0 + (double) P.p3 * (double) (k * k));
                        const double fk = hz * mult;
                        if (fk >= nyq) break;
                        arm[k] += fk / fs; if (arm[k] >= 1.0) arm[k] -= 1.0;
                        const float tau = P.dec / (1.0f + P.p1 * (float) k);
                        //  Y la inclinacion del reparto tambien: pulsar fuerte
                        //  no solo saca mas parciales, los saca menos apagados.
                        //  Con 1/k fijo, doce armonicos contra siete median x1.10
                        //  - los de arriba pesan demasiado poco para notarse.
                        const float amp = std::pow ((float) (k + 1), -1.0f + 0.45f * capaMix);
                        //  EL FUNDAMENTAL CENTRADO Y LOS PARCIALES ABRIENDOSE
                        //  SEGUN SUBEN, alternando lado. Es lo que hace un arpa
                        //  de verdad: el tono viene de una cuerda y el brillo de
                        //  las que vibran por simpatia a los lados.
                        const double x = (k == 0) ? 0.0
                            : (((k & 1) != 0) ? 1.0 : -1.0) * juce::jmin (1.0, (double) k / 2.0);
                        suma += ladoDe (canal, x) * amp * env (te, tau)
                                * (float) std::sin (juce::MathConstants<double>::twoPi * arm[k]);
                    }
                    f1.set (juce::jlimit (400.0, nyq, 3000.0 * (double) brillo), 1.0f);
                    v = limita (1.1f * suma + P.p4 * f1.bpf (rnd()) * env (te, 0.004f) * 1.8f * fuerza);
                    break;
                }

                // ============================================================
                //  LAS OCHO NUEVAS. Dos por categoria, y OCHO ALGORITMOS y no
                //  ocho juegos de numeros: `Sintes.h` ya cuenta que rellenar una
                //  familia con variantes de un generador que existe es lo que
                //  paso con los bancos A y B de la fabrica -seis sonidos que
                //  eran literalmente el mismo- y que la prueba de pares lo caza.
                //  Cada una de estas ocho hace algo que ninguna de las dieciseis
                //  de antes hacia, y en el comentario esta escrito QUE.
                // ============================================================

                case fFm:
                {
                    //  FM DE TRES OPERADORES EN CADENA, CON REALIMENTACION.
                    //  p1 razon del modulador A, p2 indice, p3 razon del B -que
                    //  modula al A, no al portador-, p4 realimentacion.
                    //
                    //  NO ES PIANO ELEC CON OTROS NUMEROS, y las dos diferencias
                    //  son de algoritmo: alli el indice CAE -por eso empieza en
                    //  campana y acaba en seno- y aqui SOSTIENE, que es lo que
                    //  hace que una pila de FM suene a metal y no a martillo; y
                    //  alli hay UN modulador y aqui hay dos en cadena mas la
                    //  realimentacion, que es lo unico que mueve un espectro de
                    //  FM sin tocar el indice.
                    ph4 += inc * (double) P.p3; if (ph4 >= 1.0) ph4 -= 1.0;
                    const double modB = (double) (P.p2 * indice * 0.55f)
                                      * std::sin (juce::MathConstants<double>::twoPi * ph4);
                    phm += inc * (double) P.p1; if (phm >= 1.0) phm -= 1.0;
                    const double modA = (double) (P.p2 * indice)
                                      * std::sin (juce::MathConstants<double>::twoPi * phm + modB);
                    ph += inc; if (ph >= 1.0) ph -= 1.0;
                    //  EL ANCHO SALE DE LA PILA Y NO DE UN DESAFINE. Los dos
                    //  moduladores arrancan con otra fase en el canal derecho y
                    //  la realimentacion vale un 25 % menos: las bandas
                    //  laterales caen en las MISMAS frecuencias con otra fase y
                    //  otro peso, o sea otra forma de onda sin mover un cent.
                    //  Un desafine aqui seria un chorus, y ademas partiria el
                    //  bucle, que en esta forma sostiene.
                    //  Y EL DESFASE SE ESCALA CON EL INDICE, que es la misma
                    //  cuenta que ya esta escrita en PIANO ELEC y por la misma
                    //  razon medida: con un cuarto de ciclo fijo, los catorce
                    //  presets de indice alto se iban al otro lado del intervalo
                    //  -hasta **-4.03 dB** al sumarse en mono, contra un liston
                    //  de -1.5-. Cuantas mas bandas laterales hay, menos hay que
                    //  moverlas. 0.06/indice deja SOFT FM (0.6) en 0.10 y HARSH
                    //  (8.0) en 0.0075. El numero de arriba salio de tres
                    //  medidas seguidas del mismo preset: HARSH -indice 8, el
                    //  mas alto de los dieciseis- iba **-2.78 dB** con 0.02 de
                    //  desfase, **-2.02** con 0.0125 y entra con 0.0075. La
                    //  relacion es lineal en el desfase, no en el indice.
                    if (n == 0 && canal == 1)
                    {
                        const double sep = juce::jlimit (0.006, 0.15,
                                               0.06 / (double) juce::jmax (0.2f, P.p2));
                        phm += sep; ph4 += sep * 1.4;
                    }
                    const float rea = P.p4 * 0.5f * ((canal == 1) ? 0.94f : 1.0f);
                    const float y = (float) std::sin (juce::MathConstants<double>::twoPi * ph
                                                      + modA + (double) (rea * fbFm));
                    //  Y LA REALIMENTACION SE PROMEDIA CON LA ANTERIOR. Sin
                    //  esto, un operador realimentado por encima de ~0.6 entra
                    //  en caos y deja de tener tono; el promedio es el paso bajo
                    //  de un polo que todos los motores de FM le ponen, y lo que
                    //  hace es que el tope de 0.5 sea de verdad un tope.
                    fbFm = 0.5f * (fbFm + y);
                    //  Y UN PASO BAJO A LA SALIDA, que no esta por gusto: sin
                    //  el, BRILLO -que es uno de los cuatro mandos comunes a las
                    //  veinticuatro- no tendria nada que mover en esta forma,
                    //  porque una pila de FM no lleva filtro. Un mando que no se
                    //  mueve es peor que no tenerlo, y esa regla ya esta escrita
                    //  arriba para los rangos de la tabla.
                    f1.set (juce::jlimit (300.0, nyq, hz * 10.0 * (double) brillo + 400.0), 0.7f);
                    v = f1.lp (y);
                    break;
                }

                case fSync:
                {
                    //  EL FORMANTE QUE BARRE: un grano de seno dentro de una
                    //  ventana que dura una fraccion del periodo. p1 la razon
                    //  del formante, p2 cuanto la barre la envolvente, p3 el
                    //  ancho de la ventana, p4 el sub.
                    //
                    //  ESTO ES UN SYNC SIN DOBLEZ, y esa es toda la decision.
                    //  Un oscilador esclavo reiniciado por el maestro tiene un
                    //  salto vertical en cada reinicio, o sea armonicos hasta el
                    //  infinito que ni el 4x ni los 513 taps del diezmador
                    //  pueden quitar, porque no estan por encima de Nyquist:
                    //  vuelven doblados y caen inarmonicos. La ventana de Hann
                    //  vale cero en los dos extremos del grano, asi que la
                    //  costura es continua POR CONSTRUCCION y el pico del
                    //  espectro se coloca donde diga la razon - que es lo que un
                    //  sync hace y lo que el oido oye de el.
                    //
                    //  Y el tono lo pone el periodo del grano, no el seno: la
                    //  razon puede barrer del 1 al 12 sin desafinar ni un cent.
                    //  Por eso BRILLO multiplica la RAZON y no un corte: en una
                    //  forma sin filtro, subir el formante ES subir el brillo, y
                    //  un mando comun que no mueva nada seria peor que no estar.
                    const float ef = env (te, juce::jmax (0.02f, P.dec * 0.6f));
                    //  El barrido va con el INDICE y no plano: es el mando de
                    //  capa que le toca -un sync pulsado fuerte barre mas- y sin
                    //  el las tres capas medirian el mismo centroide.
                    const double razonBase = juce::jlimit (1.0, 24.0,
                        (double) P.p1 * (double) brillo * (1.0 + (double) (P.p2 * indice * ef)));
                    //  EL ANCHO SE PONE DESPUES DEL ACOTADO Y ES INVERSO A LA
                    //  RAZON, y las dos mitades salieron de medir.
                    //
                    //  Dentro del `jlimit`, CHIRP -razon 11 por brillo 3.4- caia
                    //  en el tope de 24 por los dos canales y media **r = 0.9808**:
                    //  un ancho que el limite se comia, que es exactamente lo que
                    //  ya le paso a CLAVES. Y con un 1.5 % fijo, los de razon
                    //  baja -SUB S, WIDE, SOFT S- se quedaban en 0.98 y pico
                    //  mientras SIREN se caia **-1.50 dB** en mono: un formante
                    //  bajo casi no tiene bandas que mover y uno alto las tiene
                    //  todas, asi que el mismo porcentaje no vale para los dos.
                    const double sep = juce::jlimit (0.010, 0.090, 0.09 / razonBase);
                    const double razon = razonBase * (1.0 + (canal == 1 ? sep : -sep));
                    const double anchoV = juce::jlimit (0.08, 1.0, (double) P.p3)
                                            * ((canal == 1) ? 0.90 : 1.10);
                    ph  += inc;       if (ph  >= 1.0) ph  -= 1.0;
                    ph2 += inc * 0.5; if (ph2 >= 1.0) ph2 -= 1.0;
                    float grano = 0.0f;
                    if (ph < anchoV)
                    {
                        const double g = ph / anchoV;
                        const float ven = 0.5f * (1.0f - (float) std::cos (
                                              juce::MathConstants<double>::twoPi * g));
                        grano = ven * (float) std::sin (
                                    juce::MathConstants<double>::twoPi * razon * ph);
                    }
                    //  El sub centrado, que es grave y lo grave no se abre.
                    v = limita (1.4f * grano + P.p4 * 0.45f * sqrBl (ph2, inc * 0.5));
                    break;
                }

                case fPiano:
                {
                    //  PIANO ACUSTICO. p1 inarmonicidad, p2 el desafine de la
                    //  cuerda gemela en cents, p3 el martillo, p4 la caja.
                    //
                    //  POR QUE NO ES ARPAS CON OTROS NUMEROS, que es la pregunta
                    //  que esta familia tiene que contestar porque las dos son
                    //  aditivas y pulsadas. Tres cosas, y las tres son del
                    //  instrumento y no del ajuste:
                    //   · CADA NOTA SON DOS O TRES CUERDAS, afinadas casi igual.
                    //     Eso no es un coro: es lo que hace que un piano tenga
                    //     una caida en dos tiempos y un latido lento encima.
                    //   · LA CAIDA ES DOBLE. Un parcial de piano se cae rapido
                    //     y luego se queda -el "aftersound"-, que es exactamente
                    //     lo que un arpa no hace.
                    //   · Y LA CAJA, dos resonancias fijas que no se mueven con
                    //     la nota: un arpa no tiene tabla armonica.
                    //
                    //  Los parciales van ESTIRADOS por la rigidez de la cuerda
                    //  -la misma cuenta que `fArpa`- porque eso si es fisica
                    //  compartida: una cuerda tensa no da multiplos exactos.
                    const int cuantos = juce::jlimit (2, 14,
                        (int) std::lround (11.0 * (double) (0.55f + 0.75f * capaMix)));
                    //  El desafine de la gemela, EN HERCIOS sobre el fundamental.
                    const double dHz = hz * (std::pow (2.0, (double) P.p2 / 1200.0) - 1.0);
                    float suma = 0.0f;
                    for (int k = 0; k < cuantos; ++k)
                    {
                        const double mult = (double) (k + 1)
                                          * std::sqrt (1.0 + (double) P.p1 * (double) (k * k));
                        const double fk = hz * mult;
                        if (fk >= nyq) break;
                        arm[k] += fk / fs; if (arm[k] >= 1.0) arm[k] -= 1.0;
                        //  LA GEMELA SIN UN SEGUNDO OSCILADOR, y no es una
                        //  aproximacion: cos(a) + cos(b) ES 2 cos((a+b)/2)
                        //  cos((a-b)/2), o sea que multiplicar un parcial por un
                        //  coseno lento da EXACTAMENTE los dos parciales
                        //  separados. Doce osciladores mas por nota habrian sido
                        //  el doble de tiempo de sintesis para escribir la misma
                        //  igualdad.
                        const float bat = (float) std::cos (
                            juce::MathConstants<double>::pi * dHz * mult * (double) te);
                        //  LA CAIDA DOBLE, que es la firma del piano.
                        const float tau = P.dec / (1.0f + 0.85f * (float) k);
                        const float dob = 0.72f * env (te, tau) + 0.28f * env (te, tau * 4.5f);
                        //  Y el reparto de los parciales lo mueve la capa, como
                        //  en ARPAS: golpear fuerte saca parciales que golpear
                        //  flojo no saca.
                        const float amp = std::pow ((float) (k + 1), -1.25f + 0.55f * capaMix);
                        const double x = (k == 0) ? 0.0
                            : (((k & 1) != 0) ? 1.0 : -1.0) * juce::jmin (1.0, (double) k / 3.0);
                        suma += ladoDe (canal, x * 0.6) * amp * dob * bat
                                * (float) std::sin (juce::MathConstants<double>::twoPi * arm[k]);
                    }
                    //  LA TABLA ARMONICA: dos resonancias en HERCIOS FIJOS y no
                    //  en multiplos de la nota. Una caja de madera resuena donde
                    //  resuena toque uno lo que toque, y eso es lo que hace que
                    //  las notas graves y las agudas de un piano suenen al mismo
                    //  instrumento.
                    f1.set (juce::jlimit (90.0,  nyq,  190.0 * (double) brillo), 1.6f);
                    f2.set (juce::jlimit (300.0, nyq, 1350.0 * (double) brillo), 1.1f);
                    f3.set (juce::jlimit (500.0, nyq, 2600.0 * (double) brillo), 1.3f);
                    v = limita (1.15f * suma
                                + P.p4 * (0.9f * f1.bpf (suma) + 0.6f * f2.bpf (suma))
                                + P.p3 * f3.bpf (rnd()) * env (te, 0.008f) * 2.2f
                                       * (0.25f + 1.1f * capaMix));
                    break;
                }

                case fAcordeon:
                {
                    //  LENGUETA LIBRE. p1 el musette en cents, p2 el ancho del
                    //  pulso, p3 cuanto pega la lengueta, p4 el fuelle.
                    //
                    //  Tres lenguetas y no una: un acordeon de verdad lleva dos
                    //  o tres por nota, deliberadamente desafinadas, y ESE
                    //  latido es el instrumento. Sin el, esto seria un organo.
                    //
                    //  Y LO QUE LO SEPARA DE COLCHONES -que tambien son pulsos
                    //  desafinados- no es el ajuste: alli el ancho lo mueve un
                    //  LFO y el sonido es un filtro pasa bajo; aqui el ancho no
                    //  se mueve y el timbre lo pone un TOPE -la lengueta que
                    //  choca- mas una resonancia fija. Un tope no es un filtro:
                    //  crea armonicos en vez de quitarlos, y por eso una
                    //  lengueta suena aspera con la nota quieta.
                    const double anchoL = juce::jlimit (0.06, 0.45, (double) P.p2);
                    float suma = 0.0f;
                    for (int k = 0; k < 3; ++k)
                    {
                        const double det = std::pow (2.0, (double) P.p1 * (double) desigual (k, 3) / 1200.0);
                        const double ik = inc * det;
                        arm[k] += ik; if (arm[k] >= 1.0) arm[k] -= 1.0;
                        //  Las tres, repartidas por el mismo vector que las
                        //  desafina. Ver CUERDAS: cero osciladores nuevos.
                        //  DOS QUINTOS DE APERTURA Y NO LA ENTERA. Con la de CUERDAS
                        //  -que son siete y estas son tres- DRY se caia **-2.35
                        //  dB** al sumarse en mono: tres piezas repartidas a lo
                        //  ancho dejan cada lado casi solo con una.
                        suma += ladoDe (canal, 0.42 * kApertura * (double) kDesigual[k])
                                * pulsoBl (arm[k], ik, anchoL);
                    }
                    //  EL TOPE. Con la capa metida dentro, porque apretar el
                    //  fuelle es exactamente esto: la lengueta llega mas lejos y
                    //  choca mas.
                    const float lengueta = soft (0.45f * suma * (1.0f + P.p3 * 3.2f * (0.4f + 0.9f * capaMix)));
                    //  Y LA CAJA DE LA LENGUETA, en Hercios fijos como la tabla
                    //  de un piano y por lo mismo.
                    //  Y LA CAJA DE LA LENGUETA RESUENA UN 12 % DISTINTA A CADA
                    //  LADO, que es lo que le faltaba a CLARIN: con el musette a
                    //  cero las tres lenguetas son la misma onda y el reparto no
                    //  tiene nada que repartir -**r = 0.9818**-. Una caja de
                    //  madera no resuena igual por los dos costados; misma figura
                    //  que el cuerpo de CUERDA PULS.
                    f1.set (juce::jlimit (300.0, nyq, 1250.0 * (double) brillo
                                                        * ((canal == 1) ? 1.12 : 0.88)), 1.9f);
                    f2.set (juce::jlimit (800.0, nyq, 2900.0), 1.2f);
                    v = limita (0.85f * lengueta + 0.55f * f1.bpf (lengueta)
                                + P.p4 * f2.bpf (rnd()) * (0.25f + 1.0f * capaMix) * 1.6f);
                    break;
                }

                case fSitar:
                {
                    //  EL PUENTE PLANO. p1 el zumbido, p2 la caida del filtro,
                    //  p3 las simpaticas, p4 el dron.
                    //
                    //  Un sitar no suena asi por el filtro: suena asi porque el
                    //  puente es ANCHO Y PLANO y la cuerda rebota contra el en
                    //  cada vuelta. Eso es un PLEGADO -la onda se dobla sobre si
                    //  misma cuando se pasa- y no hay filtro que lo imite,
                    //  porque un filtro solo puede quitar armonicos y esto los
                    //  crea. Es la unica de las veinticuatro con un plegador.
                    //
                    //  Y detras van las SIMPATICAS, que en un sitar son once o
                    //  trece cuerdas que nadie toca y que suenan solas: dos
                    //  parciales de caida larga en quinta y octava, uno a cada
                    //  lado, mas el dron una octava abajo. Eso es lo que hace que
                    //  una sola nota suene a instrumento entero.
                    ph  += inc;       if (ph  >= 1.0) ph  -= 1.0;
                    ph2 += inc * 1.5; if (ph2 >= 1.0) ph2 -= 1.0;
                    ph3 += inc * 2.0; if (ph3 >= 1.0) ph3 -= 1.0;
                    ph4 += inc * 0.5; if (ph4 >= 1.0) ph4 -= 1.0;
                    const float ef = env (te, juce::jmax (0.02f, P.p2));
                    //  El plegado, y su fuerza la manda la capa: pulsar fuerte
                    //  hace que la cuerda llegue al puente y pulsar flojo no.
                    //  Y EL PLEGADO PEGA UN 7 % DISTINTO A CADA LADO. Es lo
                    //  unico que MUTED tiene -sin simpaticas y sin dron media
                    //  **r = 0.9997**, o sea un solo canal- y ademas es lo que
                    //  mas rinde: un plegador es no lineal, asi que un 7 % de
                    //  entrada cambia CUANTOS armonicos salen, no cuanto suenan.
                    //  Y por eso es 7 y no 10: con el 10, JAWARI y BUZZ S -los
                    //  dos de mas zumbido- se pasaban al otro lado y se caian
                    //  **-1.76 dB** en mono. El intervalo es estrecho porque la
                    //  pieza es no lineal.
                    const float pega = (1.0f + P.p1 * 5.0f * (0.35f + 0.9f * capaMix))
                                       * ((canal == 1) ? 1.07f : 0.93f);
                    const float z = (float) std::sin (juce::MathConstants<double>::halfPi
                                                      * (double) (pega * sawBl (ph, inc)));
                    //  Y detras el pasa bajo que se cierra, que es lo que hace
                    //  que el zumbido dure menos que la nota.
                    f1.set (juce::jlimit (200.0, nyq, hz * (double) brillo * (2.0 + 16.0 * (double) ef))
                              * ((canal == 1) ? 1.12 : 0.88), 1.0f);
                    const float sim = P.p3
                        * (0.55f * ladoDe (canal, -0.75) * env (te, P.dec * 1.4f)
                                 * (float) std::sin (juce::MathConstants<double>::twoPi * ph2)
                         + 0.40f * ladoDe (canal,  0.75) * env (te, P.dec * 1.1f)
                                 * (float) std::sin (juce::MathConstants<double>::twoPi * ph3));
                    //  El dron CENTRADO: es lo mas grave que hay aqui y lo grave
                    //  no se abre. Ver `monoDeVerdad`.
                    const float dron = P.p4 * 0.45f * env (te, P.dec * 2.0f)
                                     * (float) std::sin (juce::MathConstants<double>::twoPi * ph4);
                    v = limita (1.1f * f1.lp (z) + sim + dron);
                    break;
                }

                case fCello:
                {
                    //  ARCO SOLO. p1 la presion del arco, p2 el cuerpo, p3 el
                    //  vibrato, p4 la crin.
                    //
                    //  CUERDAS son siete atriles desafinados: un conjunto. Esto
                    //  es UNA cuerda, y lo que la hace sonar a instrumento y no
                    //  a sierra filtrada son dos resonancias del cajon en
                    //  Hercios fijos -las de un violonchelo de verdad, sobre los
                    //  220 y los 600- mas la crin, que en un arco es la mitad
                    //  del sonido y en un conjunto no se oye porque se promedia.
                    //
                    //  Y ese cajon es tambien el ancho: una caja de madera no
                    //  resuena igual por los dos costados. Es la misma figura que
                    //  el cuerpo de CUERDA PULS y por la misma razon -un solo
                    //  oscilador no se puede repartir-.
                    lfo += incLfo; if (lfo >= 1.0) lfo -= 1.0;
                    const double vib = std::pow (2.0, (double) P.p3 * 0.009
                                        * std::sin (juce::MathConstants<double>::twoPi * lfo));
                    const double i1 = inc * vib;
                    ph += i1; if (ph >= 1.0) ph -= 1.0;
                    //  LA PRESION DEL ARCO NO ES VOLUMEN: aprieta la onda contra
                    //  el tope, o sea que le mete diente. Un arco flojo da casi
                    //  un seno y uno fuerte raspa, y eso no es un filtro.
                    const float diente = soft (sawBl (ph, i1) * (0.8f + P.p1 * 2.4f)) * 0.8f;
                    const double lado = (canal == 1) ? 1.10 : 0.90;
                    f1.set (juce::jlimit (90.0,  nyq, 220.0 * (double) P.p2 * lado), 2.2f);
                    //  UNA resonancia de cajon y no dos, y el segundo filtro se
                    //  gasta en el paso bajo de BRILLO: con los dos formantes,
                    //  el unico mando comun que quedaba vivo era el ruido del
                    //  arco, o sea que BRILLO movia el 10 % del sonido.
                    f2.set (juce::jlimit (300.0, nyq, hz * 7.0 * (double) brillo + 900.0), 0.8f);
                    f3.set (juce::jlimit (800.0, nyq, hz * 6.0 * (double) brillo + 1800.0), 1.2f);
                    v = limita (f2.lp (0.55f * diente + 0.95f * f1.bpf (diente))
                                + P.p4 * f3.bpf (rnd()) * (0.25f + 1.2f * capaMix) * 2.0f);
                    break;
                }

                case fCana:
                {
                    //  DOBLE LENGUETA Y TUBO TAPADO. p1 el cierre de la caña,
                    //  p2 el aliento, p3 cuanto tapa el tubo, p4 la lengueta.
                    //
                    //  Lo que separa esto de VIENTOS y de METALES no es el
                    //  ajuste: VIENTOS es un seno con aire -un tubo abierto, sin
                    //  caña- y METALES es un pulso con el filtro pegando un
                    //  empujon. Aqui el timbre lo pone la GEOMETRIA: el peine de
                    //  medio periodo de arriba borra los armonicos pares, que es
                    //  literalmente lo que hace un tubo cerrado por un extremo, y
                    //  eso ningun filtro lo puede fingir porque no es una banda:
                    //  son uno si, uno no, hasta arriba.
                    ph += inc; if (ph >= 1.0) ph -= 1.0;
                    //  La caña abre y cierra: cuanto mas cerrada, mas estrecho el
                    //  pulso y mas armonicos hay que tapar.
                    float pulso = pulsoBl (ph, inc, juce::jlimit (0.06, 0.45, (double) P.p1));
                    //  Y la lengueta CHOCA, que es un tope y no un filtro. Con
                    //  la capa dentro: soplar fuerte la cierra del todo.
                    pulso = soft (pulso * (1.0f + P.p4 * 3.0f * (0.4f + 0.9f * capaMix)));
                    const float viejo = cuerda[(size_t) ksPos];
                    cuerda[(size_t) ksPos] = pulso;
                    ksPos = (ksPos + 1) % ksLen;
                    //  UN 6 % DE PROFUNDIDAD DE DIFERENCIA ENTRE CANALES. El
                    //  peine es la unica pieza que esta forma tiene y no se puede
                    //  repartir -es una sola columna de aire-, asi que el ancho
                    //  sale de que tape un poco distinto a cada lado. No es un
                    //  retardo: las dos senales son la misma en el tiempo.
                    //  UN 15 % Y NO UN 6 %: con el 6, TIGHT -poco aliento y el
                    //  peine a medio tapar- media **r = 0.9962**. Lo que el
                    //  peine tapa es armonicos enteros, asi que para que se note
                    //  tiene que tapar bastante mas a un lado que al otro.
                    const float prof = juce::jlimit (0.0f, 0.97f,
                                            P.p3 * ((canal == 1) ? 1.15f : 0.85f));
                    const float tubo = pulso - prof * viejo;
                    //  Y EL PASO BAJO DE SALIDA TAMBIEN SE SEPARA, un 10 %: lo
                    //  ve TODO, que es lo que ya hizo falta en CUERDA PULS
                    //  cuando el cuerpo solo no llegaba.
                    f1.set ((juce::jlimit (200.0, nyq, hz * 4.0 * (double) brillo + 300.0))
                              * ((canal == 1) ? 1.10 : 0.90), 0.9f);
                    f2.set (juce::jlimit (600.0, nyq, 2200.0), 1.4f);
                    //  El aliento entra con el ataque y se queda: una caña sopla
                    //  todo el rato, no solo al principio.
                    v = limita (0.75f * f1.lp (tubo)
                                + P.p2 * f2.bpf (rnd()) * (0.30f + 1.1f * capaMix)
                                       * (0.70f + 0.30f * env (te, 0.02f)) * 2.4f);
                    break;
                }

                case fTubo:
                {
                    //  FLAUTADO TAPADO DE ORGANO. p1 la mezcla, p2 el chiff,
                    //  p3 el viento, p4 la quinta.
                    //
                    //  ORGANOS son ocho barras en 1 2 3 4 6 8 12 16, o sea que
                    //  los PARES mandan, y llevan leslie. Esto es un tubo TAPADO:
                    //  solo impares -1 3 5 7 9 11-, que es la razon de que un
                    //  bordon suene hueco y un principal no. Y no lleva leslie
                    //  sino CHIFF -el golpe de aire del primer instante, que es
                    //  como se reconoce un organo de tubos de uno electronico- y
                    //  un viento que tiembla por debajo del hercio.
                    lfo += incLfo; if (lfo >= 1.0) lfo -= 1.0;
                    //  El viento no es constante y por eso un organo respira.
                    //  1.5 por mil: mas seria un vibrato, y un fuelle no vibra.
                    const double viento = 1.0 + (double) P.p3 * 0.0015
                                        * std::sin (juce::MathConstants<double>::twoPi * lfo);
                    //  CUANTOS REGISTROS, y la capa manda: abrir mas registros es
                    //  literalmente lo que hace un organista para tocar fuerte.
                    //  Con el numero fijo las tres capas medirian el mismo
                    //  centroide, que es el fallo que ya cazo la prueba en ARPAS.
                    //  Y BRILLO ABRE REGISTROS en vez de mover un corte, que es
                    //  lo que un organista hace de verdad para sonar mas claro.
                    //  `brillo` ya lleva la capa multiplicada dentro, asi que la
                    //  capa fuerte saca registros por si sola.
                    const int ranks = juce::jlimit (2, 6,
                        (int) std::lround (1.5 + (double) P.p1 * 3.5 * (double) brillo));
                    float suma = 0.0f;
                    for (int k = 0; k < ranks; ++k)
                    {
                        const double mult = (double) (2 * k + 1);
                        const double fk = hz * mult * viento;
                        if (fk >= nyq) break;
                        arm[k] += fk / fs; if (arm[k] >= 1.0) arm[k] -= 1.0;
                        const float amp = std::pow ((float) (2 * k + 1), -1.1f + 0.4f * capaMix);
                        //  Cada registro sale de un tubo distinto, o sea de un
                        //  sitio distinto del mueble. El fundamental centrado.
                        const double x = (k == 0) ? 0.0 : (((k & 1) != 0) ? 0.8 : -0.8);
                        suma += ladoDe (canal, x) * amp
                                * (float) std::sin (juce::MathConstants<double>::twoPi * arm[k]);
                    }
                    //  Y EL NAZARDO, que es la quinta de la octava de arriba y el
                    //  registro que le da a un organo el color que no tiene
                    //  ningun otro instrumento. Templada -2.9966 y no 3- porque
                    //  un tubo se afina a mano y contra los demas.
                    ph2 += inc * 2.9966 * viento; if (ph2 >= 1.0) ph2 -= 1.0;
                    suma += P.p4 * 0.5f * ladoDe (canal, -0.45)
                            * (float) std::sin (juce::MathConstants<double>::twoPi * ph2);
                    f1.set (juce::jlimit (800.0, nyq, hz * 6.0 * (double) brillo + 1500.0), 1.2f);
                    f2.set (juce::jlimit (400.0, nyq, 1800.0), 0.8f);
                    v = limita (0.85f * suma
                                + P.p2 * f1.bpf (rnd()) * env (te, 0.02f) * 3.0f
                                       * (0.3f + 1.0f * capaMix)
                                + P.p3 * f2.bpf (rnd()) * 0.5f);
                    break;
                }
            }

            //  La forma ya trae su propia caida cuando no sostiene (arm/env
            //  por parcial); `amp` es el ataque y el sobre general.
            //
            //  Y EL QUITA-CONTINUA, que hizo falta despues de medir.
            //
            //  Una suma de senos no tiene continua, y `soft()` es un polinomio
            //  IMPAR, asi que tampoco deberia crearla. La crea igual: un
            //  polinomio impar sobre una onda ASIMETRICA de media cero devuelve
            //  media distinta de cero, porque comprime mas el lado que llega mas
            //  lejos. Medido en ORGANOS -ocho barras aditivas con las fases de la
            //  proporcion aurea, que es asimetrico por construccion-: **VOX sale
            //  a -20.5 dBFS de continua contra un liston de -60**, y ROCK ORG a
            //  -22.3. Con un pico de 0.0657, esos 0.0062 son el **9 %** del
            //  margen, tirado en un desplazamiento que no se oye.
            //
            //  Un polo a 5 Hz: a 32.7 Hz -la raiz mas grave- eso son 0.1 dB de
            //  atenuacion y 8.7 grados de fase, y a los 130 Hz de la raiz
            //  central ni eso. Y se asienta en 32 ms, o sea mucho antes del
            //  punto de bucle, asi que el cuerpo sigue siendo estacionario y la
            //  vuelta sigue siendo continua.
            const float x = v * amp * fuerza;
            dcY = x - dcX + dcR * dcY;
            dcX = x;
            d[n] = dcY;
        }

        juce::ignoreUnused (ph4);
    }

    // ------------------------------------------------------------------------
    //  LA PUERTA: se genera a 4x y se baja. Es la unica que el resto llama.
    //
    //  El retardo del filtro se compensa RINDIENDO DE MAS por delante y tirando
    //  esa cabecera, que es lo unico que deja el punto de bucle donde `pre` dice
    //  que esta. Rellenar con ceros seria meter un flanco, y un flanco es
    //  exactamente lo que este filtro esta aqui para no dejar pasar.
    //
    //  La memoria: para una zona de 1.3 s el intermedio son 1.3 * 192000 * 4 B =
    //  un mega. Se reserva por zona y se suelta al salir, en el hilo del
    //  cargador y jamas en el de audio.
    // ------------------------------------------------------------------------
    static void rinde (float* dL, float* dR, int len, const Familia& F, const Preset& P,
                       double hz, int capa, int semilla, float congelaEn = -1.0f,
                       double lfoHz = -1.0)
    {
        const int crudoLen = Diezmador::largoDeRender (len);
        std::vector<float> crudo ((size_t) crudoLen, 0.0f);

        //  LA CABECERA ES SILENCIO DE VERDAD Y NO SEÑAL ADELANTADA.
        //
        //  El centro del filtro cae en `kMitad`, asi que la muestra 0 de la
        //  salida se forma alrededor de la muestra `kMitad` del render. Si la
        //  generacion empezara en la muestra 0 del buffer intermedio, TODAS las
        //  envolventes saldrian 1.33 ms adelantadas y -peor- `congelaEn`
        //  congelaria en un sitio distinto del que el punto de bucle dice.
        //
        //  Asi que la generacion empieza en `kMitad` y lo de delante se queda a
        //  cero. Y eso no es un apaño: antes de que la nota arranque **no hay
        //  sonido**, o sea que los ceros son la verdad y no un relleno. Lo que
        //  el filtro hace con ellos es darle al ataque su subida natural.
        const int mitad = Diezmador::mitadDe (Diezmador::kOs);
        rindeCrudo (crudo.data() + mitad, crudoLen - mitad,
                    F, P, hz, capa, semilla, congelaEn, kRender, lfoHz, 0);
        Diezmador::diezma (crudo.data(), dL, len);

        //  Y EL DERECHO. Las dos formas que se quedan en mono se COPIAN y no se
        //  vuelven a generar: ademas de ahorrar la mitad del tiempo, es lo unico
        //  que garantiza `L == R` **bit a bit**, que es lo que su regla pide.
        //  Generar dos veces con la misma semilla tambien daria lo mismo hoy, y
        //  dejaria de darlo el dia que alguien meta un sorteo mas en medio: una
        //  regla que depende de que nadie toque nada no protege nada.
        //  `dR` nulo es «la gama de este aparato rinde en mono»: ni se genera
        //  ni se copia. Ver `Sintes::Gama`.
        if (dR == nullptr) return;

        if (monoDeVerdad (F.forma))
        {
            std::memcpy (dR, dL, sizeof (float) * (size_t) len);
            return;
        }

        std::fill (crudo.begin(), crudo.end(), 0.0f);
        rindeCrudo (crudo.data() + mitad, crudoLen - mitad,
                    F, P, hz, capa, semilla, congelaEn, kRender, lfoHz, 1);
        Diezmador::diezma (crudo.data(), dR, len);
    }

    // ------------------------------------------------------------------------
    //  EL BUCLE MIDE UN NUMERO ENTERO DE CICLOS DE LA RAIZ.
    //
    //  Sin esto el bucle duraba 0.42 s clavados, y 0.42 s de un bajo de 32.7 Hz
    //  son **13.735 ciclos**: al dar la vuelta la onda salta 265 grados de fase,
    //  y el fundido cruzado -que mezcla el final con el principio- suma dos
    //  trozos casi en CONTRAFASE. Medido en el bajo: un bache de **13.8 dB**
    //  veintiun milisegundos despues de cada vuelta. Eso es lo que se oye como
    //  "el bucle hace cosas raras", y no la costura, que estaba continua.
    //
    //  Se busca el numero de ciclos cuyo largo en MUESTRAS cae mas cerca de un
    //  entero, dentro de un margen alrededor del objetivo. No se puede
    //  desafinar el oscilador para que cuadre -a 32.7 Hz y catorce ciclos, el
    //  ajuste serian sesenta centesimas de tono- asi que lo que se mueve es el
    //  LARGO, que no se oye: cuatro centesimas de segundo arriba o abajo en un
    //  bucle es exactamente nada.
    //
    //  Y con la fase cuadrada el fundido cruzado deja de cancelar y pasa a ser
    //  lo que era: un seguro para las familias desafinadas, que no tienen
    //  periodo comun y por eso no se pueden arreglar solo con esto.
    static int largoBucle (double hz, double lfoHz, double segundos, double* lfoCuadrado)
    {
        const double P = kRate / juce::jmax (1.0, hz);        // muestras por ciclo
        const double objetivo = kRate * segundos;

        //  EL LFO TAMBIEN ES FASE, y por eso pesa lo mismo que la raiz.
        //
        //  El cuerpo ya cerraba en un numero entero de ciclos de la fundamental
        //  y aun asi la vuelta se oia: el LFO seguia corriendo con el tiempo de
        //  verdad y su fase NO cuadraba con el largo. Un leslie de 5.6 Hz en un
        //  cuerpo de 1.0091 s da **5.65 vueltas**, o sea que en cada vuelta del
        //  bucle el timbre salta de golpe a un sitio distinto del barrido. Siete
        //  grados de la raiz son un salto de fase; siete grados de un LFO son un
        //  salto de TIMBRE, y eso se oye mucho antes.
        //
        //  Asi que se busca un largo que sea a la vez numero entero de ciclos de
        //  la raiz Y del LFO, con el segundo termino pesando lo mismo que el
        //  primero — la filosofia de esta funcion no cambia: *la fase pesa mil
        //  veces mas que la duracion*, y el LFO es fase.
        const double Q = (lfoHz > 1.0e-6) ? kRate / lfoHz : 0.0;   // muestras por vuelta

        int mejor = (int) std::lround (objetivo);
        double coste = 1.0e30;
        const int nMin = juce::jmax (1, (int) std::floor (objetivo * 0.55 / P));
        const int nMax = juce::jmax (nMin + 1, (int) std::ceil (objetivo * 1.45 / P));

        for (int n = nMin; n <= nMax; ++n)
        {
            const double L = (double) n * P;
            double c = std::abs (L - (double) std::lround (L)) * 1000.0
                     + std::abs (L - objetivo) / kRate;
            if (Q > 0.0)
            {
                const double m = std::round (L / Q);
                //  Si el LFO no llega a dar una vuelta entera dentro del cuerpo
                //  no hay nada que cuadrar: se congela, y de eso se encarga
                //  quien llama. Aqui solo se evita pedirle lo imposible.
                if (m >= 1.0) c += std::abs (L - m * Q) / Q * 1000.0;
            }
            if (c < coste) { coste = c; mejor = (int) std::lround (L); }
        }

        const int largo = juce::jmax (256, mejor);

        //  Y SE DEVUELVE LA VELOCIDAD QUE EL BUCLE ADMITE, no la de la tabla.
        //
        //  Cero quiere decir CONGELADO, y le toca a catorce de los dieciseis
        //  COLCHONES: su `p2*0.7` va de 0.028 a 0.70 Hz, o sea que en un cuerpo
        //  de un segundo no dan ni una vuelta. Un LFO que no cierra no es deriva,
        //  es una RAMPA, y una rampa en bucle es un diente de sierra a la
        //  cadencia del bucle — exactamente el defecto que ya se arreglo para la
        //  envolvente del filtro («un wah a la velocidad del bucle»).
        //
        //  El precio esta dicho: DRIFT deja de moverse dentro del cuerpo. Pero
        //  con 0.028 Hz -un ciclo de treinta y seis segundos- hoy tampoco deriva;
        //  hoy reinicia un trozo de rampa en cada vuelta, que es peor.
        if (lfoCuadrado != nullptr)
        {
            if (Q <= 0.0) { *lfoCuadrado = 0.0; }
            else
            {
                const double m = std::round ((double) largo / Q);
                *lfoCuadrado = (m >= 1.0) ? m * kRate / (double) largo : 0.0;
            }
        }
        return largo;
    }

    // ------------------------------------------------------------------------
    //  EL PRESET ENTERO: cinco raices por dos capas en un solo buffer.
    // ------------------------------------------------------------------------
    //  LA RECETA ES UN ARGUMENTO, que es lo que hace editable un instrumento.
    //
    //  Hasta aqui esta funcion leia `kTabla[familia].p[preset]` y no habia
    //  forma de rendir otra cosa: los dieciseis por dieciseis eran una tabla
    //  de solo lectura, asi que la ficha del pad podia ELEGIR un sonido y no
    //  TOCARLO. La receta pasa a entrar por la puerta y el `preset` se queda
    //  para decir de que FILA salio -que es lo que el fichero de proyecto
    //  guarda y lo que el interruptor de presets mueve-.
    //
    //  Y las dos, con la de la tabla como el CASO en que nadie la ha movido:
    //  es la misma forma que `cargaFabricaEnBanco` contra `loadFactoryKits`, y
    //  por lo mismo - dos caminos que rinden por su cuenta se separan, y el
    //  sintoma seria «el preset y el editado no suenan igual» sin poder decir
    //  por que.
    SampleBuffer::Ptr sintetiza (int familia, int preset, Gama g)
    {
        const int fi = juce::jlimit (0, kFamilias - 1, familia);
        const int pi = juce::jlimit (0, kPresets  - 1, preset);
        return sintetiza (fi, pi, kTabla[fi].p[pi], g);
    }

    SampleBuffer::Ptr sintetiza (int familia, int preset, const Preset& receta, Gama g)
    {
        //  LA GAMA SE ACOTA DONDE SE ENTRA, igual que la receta: llega de
        //  `DeviceTier` pero tambien podria llegar de un banco o de un fichero.
        const double cuerpoSeg = juce::jlimit (0.30, 2.00, g.cuerpoSeg);
        const bool   est       = g.estereo;
        const int fi = juce::jlimit (0, kFamilias - 1, familia);
        const int pi = juce::jlimit (0, kPresets  - 1, preset);
        const auto& F = kTabla[fi];
        const Preset P = acota (fi, receta);

        //  CUANTO DURA UNA ZONA.
        //
        //  Lo que sostiene se rinde ataque + bucle + una cola que se cruza
        //  sobre el principio del bucle; lo que no sostiene se rinde entero
        //  hasta que se apaga, con tope. El tope no es timidez: diez zonas de
        //  tres segundos son 5.7 MB por pad, y este buffer se queda en memoria
        //  mientras el pad exista.
        const int pre  = F.sostiene ? (int) (kRate * juce::jlimit (0.06f, 0.70f, P.atk + 0.10f)) : 0;

        //  EL FUNDIDO CRUZADO, Y NO ES UNO SOLO.
        //
        //  Cuarenta y cinco milisegundos LINEALES siguen siendo lo correcto para
        //  las cinco familias sin conjunto desafinado: con el cuerpo periodico
        //  el fundido es solo un seguro, y su material a los dos lados esta
        //  correlacionado -es la misma muestra-, que es el argumento ya escrito
        //  en `Sintes.h`.
        //
        //  Pero CUERDAS, COLCHONES, METALES y COROS llevan conjuntos de tres a
        //  siete osciladores desafinados entre si, y a **un segundo** de
        //  distancia un conjunto desafinado YA NO ESTA CORRELACIONADO consigo
        //  mismo: el fundido lineal le mete un hoyo en mitad de la costura. Esos
        //  cuatro llevan **150 ms en raiz-coseno** -potencia constante-, y los
        //  150 no son redondos: son **4.9 periodos de la raiz mas grave** (32.7
        //  Hz, 30.6 ms), o sea que hasta el bajo tiene ciclos enteros dentro del
        //  fundido.
        const bool conjunto = (F.forma == fCuerdas || F.forma == fColchon
                            || F.forma == fMetales || F.forma == fCoro);
        const int cruce = F.sostiene ? (int) (kRate * (conjunto ? 0.150 : 0.045)) : 0;

        //  EL LARGO ES DE CADA RAIZ, no de todas. El bucle mide un numero
        //  entero de ciclos y un ciclo dura lo que dura, asi que la zona de
        //  DO1 y la de DO5 no pueden medir lo mismo. Ver largoBucle.
        int cuerpoDe[kRaices] {}, zonaDe[kRaices] {};
        //  La velocidad que el bucle de CADA raiz admite para el LFO. Cero es
        //  congelado. Es por raiz porque el largo del cuerpo es por raiz.
        double lfoDe[kRaices] {};
        int total = 0;
        for (int r = 0; r < kRaices; ++r)
        {
            const double hz = kHzRaiz * std::pow (2.0, (double) kRaiz[r] / 12.0);
            //  UN SEGUNDO DE CUERPO, Y NO 0.42.
            //
            //  Con 0.42 s el cuerpo daba **2.4 vueltas por segundo**, y eso se
            //  oye como lo que es: una repeticion. El oido perdona mucho peor un
            //  periodo corto que uno largo, y a un segundo la vuelta deja de ser
            //  un ritmo y pasa a ser una textura. Cuesta memoria -ver la cuenta
            //  en la cabecera- y esa era la decision a tomar.
            cuerpoDe[r] = F.sostiene
                ? largoBucle (hz, lfoHzDe (F.forma, P), cuerpoSeg, &lfoDe[r])
                : (int) (kRate * juce::jlimit (0.25f, 1.80f, P.atk + P.dec * 2.2f + P.rel));
            zonaDe[r] = pre + cuerpoDe[r];
            total += (zonaDe[r] + kGuardas) * kCapas;
        }

        auto sb = new SampleBuffer();
        sb->sourceSampleRate = kRate;
        //  DE DONDE SALIO, para que el fichero de proyecto pueda guardar la
        //  receta en vez del audio. Ver MainComponent::captureState.
        sb->familia = fi;
        sb->preset  = pi;
        //  DOS CANALES. Ver `ladoDe`: el ancho se genera, no se procesa. Uno
        //  solo en la gama baja, donde el presupuesto de muestra son 64 MB y la
        //  fabrica ya se lleva cuarenta: ver `DeviceTier::instrumentoEstereo`.
        sb->buffer.setSize (est ? 2 : 1, total);
        sb->buffer.clear();
        float* dstL = sb->buffer.getWritePointer (0);
        float* dstR = est ? sb->buffer.getWritePointer (1) : dstL;

        int maxRinde = 0;
        for (int r = 0; r < kRaices; ++r) maxRinde = juce::jmax (maxRinde, zonaDe[r] + cruce);

        //  EL MAPA SE ESCRIBE ANTES DE RENDIR, y las quince zonas se rinden en
        //  PARALELO. Son independientes -cada una lleva su propia semilla, su
        //  propio buffer y su propio sitio en el destino- asi que repartirlas no
        //  cambia ni una muestra: el determinismo sale de la semilla y no del
        //  orden. Lo que cambia es el reloj.
        //
        //  Hacia falta, medido: un preset costaba **x104.6 un golpe de fabrica**
        //  de mediana y **x343.2 el peor**, o sea 1.7 s y 5.6 s. Quince zonas
        //  por dos canales a 4x son treinta veces el trabajo que habia, y eso no
        //  se negocia -es lo que compra la calidad-; lo que si se puede es no
        //  hacerlo en un solo nucleo.
        {
            int z0 = 0, off0 = 0;
            for (int r = 0; r < kRaices; ++r)
                for (int c = 0; c < kCapas; ++c, ++z0)
                {
                    auto& Z = sb->zonas[(size_t) z0];
                    Z.raiz     = kRaiz[r];
                    Z.capa     = c;
                    Z.ini      = off0;
                    Z.fin      = off0 + zonaDe[r];
                    Z.bucleIni = F.sostiene ? (off0 + pre) : 0;
                    Z.bucleFin = F.sostiene ? (off0 + zonaDe[r]) : 0;
                    off0 += zonaDe[r] + kGuardas;
                }
        }
        sb->nZonas = kZonas;

        const int hilos = juce::jlimit (1, 4, juce::SystemStats::getNumCpus());
        std::atomic<int> siguiente { 0 };
        auto unaZona = [&] (int z)
        {
            const int r = z / kCapas, c = z % kCapas;
            const double hz = kHzRaiz * std::pow (2.0, (double) kRaiz[r] / 12.0);
            const int zonaLen  = zonaDe[r];
            const int rindeLen = zonaLen + cruce;
            const int off      = sb->zonas[(size_t) z].ini;
            std::vector<float> tmpL ((size_t) maxRinde), tmpR ((size_t) maxRinde);
            {
                //  SEMILLA FIJA por familia, preset, raiz y capa: dos arranques
                //  tienen que dar el MISMO instrumento, o un proyecto guardado
                //  suena distinto al abrirlo. Es la misma razon por la que
                //  HUMANIZAR se escribe en vez de sortearse.
                const int semilla = ((fi * 97 + pi) * 13 + r) * 7 + c + 1;
                std::fill (tmpL.begin(), tmpL.end(), 0.0f);
                std::fill (tmpR.begin(), tmpR.end(), 0.0f);
                //  Se congela EN EL PUNTO DE BUCLE: de ahi en adelante el
                //  sonido tiene que ser estacionario o cada vuelta reinicia lo
                //  que se estuviera moviendo. Lo que no sostiene no se congela.
                rinde (tmpL.data(), est ? tmpR.data() : nullptr, rindeLen, F, P, hz, c, semilla,
                       F.sostiene ? (float) pre / (float) kRate : -1.0f,
                       F.sostiene ? lfoDe[r] : -1.0);

                //  EL FUNDIDO CRUZADO DEL BUCLE. Ver Sintes.h: en la costura
                //  las dos mitades son la misma muestra, asi que la union es
                //  continua por construccion y no por un fundido que la tape.
                if (cruce > 0)
                    for (int i = 0; i < cruce; ++i)
                    {
                        //  Los dos canales con LA MISMA curva: dos curvas
                        //  distintas serian un panoramico moviendose en cada
                        //  vuelta del bucle.
                        const float x = (float) i / (float) cruce;
                        //  LINEAL cuando el material de los dos lados esta
                        //  correlacionado, RAIZ-COSENO cuando no. Ver el porque
                        //  de `conjunto` arriba: a un segundo de distancia siete
                        //  sierras desafinadas ya no se parecen a si mismas, y el
                        //  lineal -que suma amplitudes- les mete un hoyo en mitad
                        //  de la costura. El de potencia constante suma ENERGIAS,
                        //  que es lo que hay que sumar cuando no hay fase comun.
                        const float a = conjunto ? std::sin (0.5f * juce::MathConstants<float>::pi * x) : x;
                        const float b = conjunto ? std::cos (0.5f * juce::MathConstants<float>::pi * x) : (1.0f - x);
                        tmpL[(size_t) (pre + i)] = tmpL[(size_t) (pre + i)] * a
                                                 + tmpL[(size_t) (zonaLen + i)] * b;
                        if (est)
                            tmpR[(size_t) (pre + i)] = tmpR[(size_t) (pre + i)] * a
                                                     + tmpR[(size_t) (zonaLen + i)] * b;
                    }

                std::memcpy (dstL + off, tmpL.data(), sizeof (float) * (size_t) zonaLen);
                if (est)
                    std::memcpy (dstR + off, tmpR.data(), sizeof (float) * (size_t) zonaLen);
            }
        };

        if (hilos <= 1)
        {
            for (int z = 0; z < kZonas; ++z) unaZona (z);
        }
        else
        {
            std::vector<std::thread> hebras;
            hebras.reserve ((size_t) hilos);
            for (int h = 0; h < hilos; ++h)
                hebras.emplace_back ([&]
                {
                    for (int z = siguiente.fetch_add (1); z < kZonas; z = siguiente.fetch_add (1))
                        unaZona (z);
                });
            for (auto& x : hebras) x.join();
        }

        //  Y LA CONTINUA QUE QUEDA SE RESTA, que es exacto y sale gratis.
        //
        //  El quita-continua de `rindeCrudo` baja el grueso -de -20.5 dBFS a
        //  -52- y no llega al liston de -60 en cinco presets: un polo a 5 Hz
        //  tarda 32 ms en asentarse y lo que el saturador mete no es constante,
        //  es una media que se mueve despacio. Aqui la muestra ya esta escrita y
        //  es finita, asi que la media del CUERPO se puede medir y restar sin
        //  filtro, sin fase y sin margen de error. Solo en lo que da vueltas:
        //  restarle una constante a una campana que decae a cero dejaria un
        //  escalon al final, que es un chasquido.
        if (F.sostiene)
            for (int q = 0; q < kZonas; ++q)
            {
                const auto& Z = sb->zonas[(size_t) q];
                const int n = Z.bucleFin - Z.bucleIni;
                if (n <= 0) continue;
                for (int ch = 0; ch < (est ? 2 : 1); ++ch)
                {
                    float* d = (ch == 0) ? dstL : dstR;
                    double suma = 0.0;
                    for (int i = Z.bucleIni; i < Z.bucleFin; ++i) suma += d[i];
                    const float media = (float) (suma / (double) n);
                    for (int i = Z.ini; i < Z.fin; ++i) d[i] -= media;
                }
            }

        //  UNA GANANCIA POR OCTAVA, Y LAS DOS CAPAS DE ESA OCTAVA LA COMPARTEN.
        //
        //  Ni una sola para las diez ni una por zona. Con una sola -que fue la
        //  primera version- la sonoridad bailaba hasta **9.9 dB entre las cinco
        //  octavas** de un mismo preset: la curva K realza los 1-4 kHz, asi que
        //  la misma receta rendida dos octavas arriba mide mas fuerte, y el
        //  instrumento subia de volumen solo al subir de registro. Con una por
        //  zona -que es lo corto- la capa suave saldria tan alta como la fuerte
        //  y el instrumento dejaria de responder al toque, que es la mitad de la
        //  funcion.
        //
        //  Por octava y compartida: se quita la deriva -que es un artefacto de
        //  la ponderacion, no musica- y se conserva la unica diferencia de nivel
        //  que tiene que existir, que es la del golpe.
        for (int r = 0; r < kRaices; ++r)
        {
            //  Y LA MISMA GANANCIA A LOS DOS CANALES, medida sumando sus
            //  energias como manda BS.1770. Ver `Kits::gananciaSonoridad`.
            //  En mono la de un canal da EL MISMO numero: la estereo suma las
            //  dos energias y sube el objetivo la raiz de dos, asi que con
            //  `L == R` las dos cuentas coinciden. Se llama a la que toca en vez
            //  de duplicar el buffer para que la de dos canales sirva.
            float gCapa[kCapas] = {};
            for (int c = 0; c < kCapas; ++c)
            {
                const auto& Z = sb->zonas[(size_t) (r * kCapas + c)];
                gCapa[c] = est
                    ? Kits::gananciaSonoridad (dstL + Z.ini, dstR + Z.ini, Z.fin - Z.ini)
                    : Kits::gananciaSonoridad (dstL + Z.ini, Z.fin - Z.ini);
            }
            const float gz = gCapa[kCapas - 1];                 // la capa fuerte manda

            //  EL ESCALON DE UNA CAPA A OTRA SE REPARTE, y no se deja al azar
            //  de la forma.
            //
            //  Los escalares de capa -`fuerza`, `brillo`, `indice`, `capaMix`-
            //  son geometricos, o sea parejos en decibelios, pero lo que la
            //  forma HACE con ellos no lo es: medido en la raiz 0, CUERDA PULS
            //  subia **+3.71 dB y luego +1.02** -el primer escalon se llevaba el
            //  78 % del recorrido- porque el brillo abre el filtro de la cuerda
            //  y ahi satura, y CLAVES **+1.73 y luego +3.41** -66 %- porque su
            //  paso de banda se lleva el centro con el brillo y ahi arranca
            //  tarde. Los dos torcidos, y en sentidos contrarios: no hay un
            //  escalar comun que los enderece a la vez.
            //
            //  Lo que se corrige es el resultado medido, que es lo unico que las
            //  dos formas comparten. Los EXTREMOS no se tocan -la capa suave y
            //  la fuerte se quedan donde la receta las puso, o sea el recorrido
            //  entero del toque es el que era- y las de en medio se colocan en
            //  la escalera geometrica que va de una a otra. Con tres capas eso
            //  es UNA ganancia que se mueve, y CUERDA PULS pasa a +2.36 +2.36.
            const bool  medible = (gCapa[0] > 0.0f && gz > 0.0f);
            const float razon   = medible ? (gz / gCapa[0]) : 1.0f;   // sonoridad suave/fuerte

            for (int c = 0; c < kCapas; ++c)
            {
                const auto& Z = sb->zonas[(size_t) (r * kCapas + c)];
                float gAplica = gz;
                if (medible && gCapa[c] > 0.0f && kCapas > 1)
                {
                    const float t = (float) (kCapas - 1 - c) / (float) (kCapas - 1);
                    gAplica = gCapa[c] * std::pow (razon, t);
                }
                Kits::aplicaGanancia (dstL + Z.ini, Z.fin - Z.ini, gAplica, false);
                if (est) Kits::aplicaGanancia (dstR + Z.ini, Z.fin - Z.ini, gAplica, false);
            }
        }

        //  Y LAS GUARDAS SE ESCRIBEN AL FINAL, DESPUES DE LA GANANCIA.
        //
        //  Ver `kGuardas` para que son. Escribirlas dentro del bucle de arriba
        //  -que fue la primera version- las dejaba **sin la ganancia por
        //  octava**, porque `aplicaGanancia` corre sobre `Z.fin - Z.ini` y las
        //  guardas viven justo detras de `Z.fin`. El resultado medido: en la
        //  vuelta, dos muestras crudas contra un cuerpo escalado por 0.09, o sea
        //  un pico de **-0.549 donde la señal valia -0.045**. Un chasquido de
        //  libro, una vez por vuelta, puesto por el arreglo que venia a quitar
        //  los chasquidos. Copiando despues, la guarda es por construccion la
        //  misma muestra que el cuerpo ya publicado.
        //
        //  Lo que no da vueltas las deja a cero -el buffer ya viene limpio-, que
        //  es la verdad: detras del final de una campana no hay campana.
        if (F.sostiene)
            for (int q = 0; q < kZonas; ++q)
            {
                const auto& Z = sb->zonas[(size_t) q];
                std::memcpy (dstL + Z.fin, dstL + Z.bucleIni,
                             sizeof (float) * (size_t) kGuardas);
                if (est)
                    std::memcpy (dstR + Z.fin, dstR + Z.bucleIni,
                                 sizeof (float) * (size_t) kGuardas);
            }

        return SampleBuffer::Ptr (sb);
    }

    void lfoDeZona (int familia, int preset, const Preset& receta, int raiz,
                    double cuerpoSeg, double* lfoHz, double* bucleSeg)
    {
        const int fi = juce::jlimit (0, kFamilias - 1, familia);
        const int ri = juce::jlimit (0, kRaices   - 1, raiz);
        juce::ignoreUnused (preset);
        const auto& F = kTabla[fi];
        const Preset P = acota (fi, receta);

        if (! F.sostiene) { if (lfoHz) *lfoHz = -1.0; if (bucleSeg) *bucleSeg = 0.0; return; }

        const double hz = kHzRaiz * std::pow (2.0, (double) kRaiz[ri] / 12.0);
        double lfo = 0.0;
        const int cuerpo = largoBucle (hz, lfoHzDe (F.forma, P),
                                       juce::jlimit (0.30, 2.00, cuerpoSeg), &lfo);
        if (lfoHz)    *lfoHz = lfo;
        if (bucleSeg) *bucleSeg = (double) cuerpo / kRate;
    }

    // ------------------------------------------------------------------------
    //  UNA ZONA SUELTA, AL FACTOR QUE SE PIDA. Ver Sintes.h.
    // ------------------------------------------------------------------------
    void rindeZona (int familia, int preset, const Preset& receta,
                    int raiz, int capa, float* destino, int len, int os)
    {
        if (destino == nullptr || len <= 0) return;

        const int fi = juce::jlimit (0, kFamilias - 1, familia);
        const int pi = juce::jlimit (0, kPresets  - 1, preset);
        const int ri = juce::jlimit (0, kRaices   - 1, raiz);
        const int ci = juce::jlimit (0, kCapas    - 1, capa);
        const auto& F = kTabla[fi];
        const Preset P = acota (fi, receta);

        const double hz = kHzRaiz * std::pow (2.0, (double) kRaiz[ri] / 12.0);
        const double fs = kRate * (double) os;

        //  LA MISMA SEMILLA QUE EL PRODUCTO, o el patron mediria otro ruido y la
        //  diferencia entre los dos seria el ruido y no el pliegue.
        const int semilla = ((fi * 97 + pi) * 13 + ri) * 7 + ci + 1;

        //  SE SALTA EL ATAQUE: lo que se compara es el cuerpo. El salto es el
        //  mismo `pre` que usa `sintetiza`, para que las dos miren el mismo
        //  trozo del mismo sonido.
        const int pre = F.sostiene
            ? (int) (kRate * juce::jlimit (0.06f, 0.70f, P.atk + 0.10f))
            : (int) (kRate * juce::jmin (0.30f, P.atk + 0.02f));

        const int salida   = pre + len;
        const int crudoLen = Diezmador::largoDeRender (salida, os);
        const int mitad    = Diezmador::mitadDe (os);

        std::vector<float> crudo ((size_t) crudoLen, 0.0f);
        std::vector<float> baja  ((size_t) salida,   0.0f);

        rindeCrudo (crudo.data() + mitad, crudoLen - mitad, F, P, hz, ci, semilla,
                    F.sostiene ? (float) pre / (float) kRate : -1.0f, fs);
        Diezmador::diezma (crudo.data(), baja.data(), salida, os);

        std::memcpy (destino, baja.data() + pre, sizeof (float) * (size_t) len);
    }
}
