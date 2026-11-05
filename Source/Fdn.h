#pragma once

#include <JuceHeader.h>
#include <array>
#include <cmath>
#include <limits>
#include "Analogico.h"   // muelle#: Svf para la banda del tanque y tanhR para el transductor

//  UNA REVERB DE RED DE RETARDOS REALIMENTADA (FDN).
//
//  Lo que habia era juce::dsp::Reverb, que es Freeverb: ocho peines en paralelo
//  y cuatro allpass en serie, publicado en 2000 y pensado para el presupuesto
//  de CPU de entonces. Suena a lo que es - metalico en las colas largas, con
//  una densidad que se queda corta y un tono que no se puede mover mas alla de
//  "damping". En una caja que apunta a produccion, la reverb es lo primero que
//  delata que el motor es de juguete.
//
//  Una FDN de dieciseis lineas con matriz Hadamard es el paso siguiente y cabe de
//  sobra en el presupuesto: dieciseis lineas de retardo, un polo de amortiguacion
//  en cada una, y una matriz ortogonal que las mezcla entre si. Ortogonal
//  importa: conserva la energia, asi que la cola decae por el amortiguador y
//  por la ganancia de realimentacion, y NO por como se mezclan las lineas. Con
//  una matriz cualquiera la cola se descompensa y aparecen resonancias.
//
//  Delante, cuatro allpass de difusion: un impulso entra como un impulso y sale
//  como una nube. Sin ellos la FDN suena a unos pocos ecos separados durante los
//  primeros cien milisegundos, que es exactamente donde el oido decide si algo
//  es una sala o un delay.
//
//  TIEMPO REAL: todo se reserva en prepare(). process() solo lee y escribe
//  dentro de buffers que ya existen, no reserva, no bloquea y no libera.
class Fdn
{
public:
    //  CUANTAS LINEAS Y CUANTOS DIFUSORES. Estaban abajo, en el bloque privado,
    //  y suben porque `kBase44` los necesita en su DECLARACION: el tamaño de un
    //  array se lee al vuelo y no en el contexto de clase completa, que es lo
    //  que si tiene el cuerpo de un metodo — que es donde vivia esa tabla.
    //  efectos#rev: dieciseis lineas y cuatro difusores; con cuatro y dos la densidad se quedaba en 0.60 y la cola tenia picos de 6.4 dB.
    //  calidad#: DIECISEIS LINEAS COMO TOPE Y OCHO COMO PELDANO.
    //
    //  La red se reserva siempre para dieciseis -la memoria es la misma, y
    //  reservar en marcha esta prohibido en el hilo de audio- y `nLin` dice
    //  cuantas CORREN. Con ocho se usan las pares (una de cada dos), que
    //  mantiene el reparto de largos y los signos cruzados, y la normalizacion
    //  y la salida se ajustan a la cuenta. Ver `pideLineas`: el cambio espera a
    //  que la cola haya callado, como ya hace el cambio de tipo.
    static constexpr int kLines = 16;
    static constexpr int kDiff  = 4;

    //  Longitudes en muestras a 44.1 kHz, escaladas despues a la frecuencia de
    //  trabajo. PRIMAS ENTRE SI a proposito: si dos lineas comparten divisor,
    //  sus ecos coinciden una y otra vez y la cola suena a tubo en vez de a
    //  sala.
    //
    //  Y AQUI Y NO DENTRO DE `prepare`, que es donde estaban: en cuanto
    //  `tauSegundos` tuvo que saber cuanto dura una vuelta habia DOS copias de
    //  la misma tabla, y la segunda es la que un dia se queda vieja — que es
    //  literalmente el fallo que esta tanda existe para arreglar, cometido
    //  dentro del arreglo.
    //  efectos#rev: la vuelta de referencia para fb; asi el largo de las lineas no mueve cuanto dura la cola.
    static constexpr double kRef44 = 1959.75;
    static constexpr int kBase44[kLines] = { 701, 797, 887, 997, 1123, 1277, 1423, 1597, 1801, 2027, 2281, 2579, 2879, 3251, 3659, 4111 };

    //  efectos#tipos: cinco sitios con su propia forma; SALA es la red de siempre y suena igual que antes.
    enum Tipo { kSala = 0, kCuarto, kPlaca, kMuelle, kCatedral, kNumTipos };
    struct Forma { float escala, difG, mod44, fbLo, fbSpan, amLo, amSpan, hpHz, erGan; };
    static constexpr Forma kForma[kNumTipos] =
    {
        { 1.00f, 0.62f, 6.0f, 0.72f, 0.22f, 0.05f, 0.75f,   0.0f, 0.00f },   // SALA
        { 0.45f, 0.55f, 2.0f, 0.50f, 0.34f, 0.10f, 0.70f,   0.0f, 0.90f },   // CUARTO: corto y con primeras reflexiones fuertes
        { 0.60f, 0.75f, 3.0f, 0.80f, 0.15f, 0.25f, 0.73f, 180.0f, 0.00f },   // PLACA: densa desde el golpe, brillante, sin graves
        { 1.00f, 0.62f, 0.0f, 0.55f, 0.30f, 0.20f, 0.45f, 110.0f, 0.00f },   // MUELLE: lazo propio con dispersion
        { 1.35f, 0.70f, 8.0f, 0.86f, 0.11f, 0.04f, 0.66f,   0.0f, 0.60f },   // CATEDRAL: lineas largas, ecos tardios separados
    };
    static constexpr float  kEscalaMax = 1.35f;
    static constexpr double kModMax44 = 8.0, kPrevioMaxMs = 200.0, kEcoMaxMs = 120.0;
    static constexpr int    kEcos = 8;
    struct Eco { float ms, gan, pan; };
    static constexpr Eco kEco[kNumTipos][kEcos] =
    {
        {},
        { { 2.3f, 0.80f, -0.7f }, { 3.9f, 0.72f, 0.7f }, { 6.1f, 0.62f, -0.5f }, { 8.4f, 0.55f, 0.6f },
          { 11.2f, 0.48f, -0.8f }, { 14.3f, 0.40f, 0.4f }, { 17.9f, 0.33f, -0.3f }, { 21.7f, 0.27f, 0.8f } },
        {},
        {},
        { { 18.0f, 0.45f, -0.6f }, { 31.0f, 0.40f, 0.7f }, { 47.0f, 0.36f, -0.8f }, { 63.0f, 0.32f, 0.5f },
          { 79.0f, 0.28f, -0.4f }, { 94.0f, 0.25f, 0.8f }, { 107.0f, 0.22f, -0.7f }, { 118.0f, 0.19f, 0.3f } },
    };
    //  ========================================================================
    //  muelle# - POR QUE EL MUELLE NO SONABA A MUELLE, MEDIDO
    //  ========================================================================
    //
    //  «El spring reverb me deja mucho que desear.» El banco lo convirtio en
    //  dos cifras y las dos decian lo mismo:
    //
    //    · DISPERSION 3 kHz contra 300 Hz: **-0.1 ms**. En un tanque de verdad
    //      los agudos viajan MAS DESPACIO por el muelle, asi que un golpe sale
    //      como un «boing» que baja de tono: decenas de milisegundos de retraso
    //      de los agudos. Cero dispersion no es un muelle, es una placa.
    //    · 8 kHz a **+6.0 dB** de 1 kHz. Un tanque de muelles no pasa agudos
    //      -ni graves-; lo que sonaba era una cola brillante y fina, que es
    //      exactamente «metalico y roto» en vez de «calido».
    //
    //  Y la causa de lo primero estaba en un SIGNO. La cadena era de allpass de
    //  primer orden con `a = -0.70`, y un allpass de primer orden con a
    //  NEGATIVO retrasa los GRAVES (retardo de grupo (1+|a|)/(1-|a|) en
    //  continua y (1-|a|)/(1+|a|) en Nyquist): justo al reves que un muelle.
    //
    //  LO QUE HAY AHORA:
    //    · Allpass ESTIRADOS: `(a + z^-K)/(1 + a z^-K)` con K = 4 y a = +0.80.
    //      Un estirado cuesta lo mismo que uno de primer orden -una multiplica
    //      y una suma por etapa- y da K veces su retardo, que es como se
    //      llega a veinte milisegundos sin cien etapas. Con a positivo el
    //      retraso se lo llevan los AGUDOS, que es el boing.
    //    · El TANQUE tiene banda: paso alto de dos polos en 90 Hz y paso bajo
    //      de dos polos en 4.2 kHz dentro del lazo. Eso es lo que quita el
    //      brillo de placa y deja el cuerpo de muelle.
    //    · Y el transductor SATURA: la entrada al muelle pasa por `tanhR`, que
    //      es la calidez de verdad -armonicos de orden bajo- y no un filtro.
    //  Y EL ALLPASS NO HACE EL BOING, QUE ES LO QUE DIJO LA MEDIDA.
    //
    //  El retardo de grupo de un allpass estirado K veces es un PICO ESTRECHO
    //  en fs/(2K), no una rampa: con K = 8 el pico cae justo en 3 kHz y aun asi
    //  la banda de 3 kHz -Q 4, o sea 750 Hz de ancho- solo se movio 4.1 ms,
    //  porque a los lados del pico el retardo es casi cero. Treinta etapas mas
    //  no arreglan eso; lo que hace falta es que los agudos RECORRAN mas muelle.
    //
    //  ASI QUE LA DISPERSION ES POR TOMAS. El muelle se lee en TRES sitios y
    //  cada banda sale del suyo: los graves de la toma mas corta, los agudos de
    //  la mas larga. Eso es literalmente la relacion de dispersion de un
    //  muelle -la onda de flexion viaja mas despacio cuanto mas alta es la
    //  frecuencia- y cuesta dos lecturas y dos filtros mas por muestra. La
    //  cadena de allpass se queda, mas corta, para el frote metalico que
    //  acompana al boing.
    static constexpr int   kMuelleAp = 8;       // etapas de dispersion por muelle
    static constexpr int   kMuelleK  = 8;       // el estiron de cada etapa
    //  Cuanto ANTES sale cada banda respecto de la mas alta, en ms.
    static constexpr float kMuelleAntesMs[2] = { 28.0f, 14.0f };   // graves, medios
    static constexpr float kMuelleCorte[2]   = { 700.0f, 2200.0f };
    static constexpr int   kMuelle44[2] = { 1631, 1897 };
    static constexpr float kMuelleA[2]  = { 0.86f, 0.84f };
    static constexpr float kMuelleSal = 0.6f, kMuelleCruce = 0.3f;
    static constexpr float kMuelleHz  = 90.0f, kMuelleTope = 3800.0f;
    //  Lo que entra al muelle antes de saturar: con el tanque a nivel de
    //  trabajo, la rodilla del tanh cae donde los golpes la encuentran.
    static constexpr float kMuelleDrive = 1.6f;

    //  LOS DOS NUMEROS QUE MANDAN EN LA COLA, por la misma razon. 0.72 a 0.94:
    //  por debajo no es una sala y por encima de 0.95 una FDN
    //  empieza a sonar a bucle infinito antes que a sitio. Y el polo va al
    //  reves que el mando: damping alto = mas oscuro.
    static int tipoDe (float t) noexcept { return juce::jlimit (0, kNumTipos - 1, (int) std::lround (t)); }
    static float realimentacionDe (float size, int tipo = kSala) noexcept
    { const auto& f = kForma[tipoDe ((float) tipo)]; return f.fbLo + f.fbSpan * juce::jlimit (0.0f, 1.0f, size); }
    static float amortiguadoDe (float damping, int tipo = kSala) noexcept
    { const auto& f = kForma[tipoDe ((float) tipo)]; return f.amLo + f.amSpan * juce::jlimit (0.0f, 1.0f, damping); }

    void prepare (double sampleRate, int channels)
    {
        fs = juce::jmax (8000.0, sampleRate);
        ultSize = ultDamp = -1.0f;   // rev#pow: el exponente cambia con fs
        chans = juce::jlimit (1, 2, channels);

        //  Los difusores, tambien primos y mucho mas cortos: su trabajo es
        //  romper el impulso, no crear cola.
        static constexpr int dif44[kDiff]   = { 142, 107, 379, 277 };

        //  calidad#: la tabla se llena para las dieciseis y con ocho se leen las
        //  OCHO PRIMERAS -0.31 a 1.27 Hz-, que es el abanico que la red de ocho
        //  ya tenia medido. Los largos y los signos si van de dos en dos (ver
        //  `pasoLin`): son los que marcan el reparto de la cola.
        for (int i = 0; i < kLines; ++i)
        {
            const double hz = (0.31 + 0.137 * i);
            const double w = 2.0 * juce::MathConstants<double>::pi * hz * kSub / fs;   // rev#cpu: un paso por tramo
            rotC[i] = (float) std::cos (w); rotS[i] = (float) std::sin (w);
        }
        for (int i = 0; i < kDiff; ++i)
            difLen[i] = juce::jmax (8, (int) std::lround (dif44[i] * fs / 44100.0));

        //  efectos#tipos: las lineas se reservan para el tipo mas largo; cambiar de tipo no reserva.
        const int extraMax = (int) std::ceil (2.0 * kModMax44 * fs / 44100.0) + 4;
        for (int i = 0; i < kLines; ++i)
        {
            //  calidad#: Y PARA LAS DOS CUENTAS DE LINEAS. Con ocho, la linea i
            //  usa el largo de la 2i -una de cada dos- que es MAS LARGO que el
            //  suyo: reservar por `kBase44[i]` dejaba la octava linea con un
            //  anillo de 1597 muestras pidiendo 3659, o sea escribiendo fuera
            //  del buffer. Lo canto un segfault en el primer banco con el
            //  peldano 1 forzado.
            const int base = juce::jmax (kBase44[i], kBase44[juce::jmin (kLines - 1, i * 2)]);
            const int largoMax = juce::jmax (16, (int) std::lround (base * kEscalaMax * fs / 44100.0));
            line[i].setSize (1, largoMax + extraMax, false, true, true);   // rev#cpu: una red para los dos lados
            line[i].clear();
            lineIdx[i] = 0;
        }
        preTam = (int) std::ceil ((kPrevioMaxMs + kEcoMaxMs + 10.0) * 0.001 * fs) + 4;
        pre.setSize (1, preTam, false, true, true);
        for (int s = 0; s < 2; ++s)
        {
            muLen[s] = juce::jmax (16, (int) std::lround (kMuelle44[s] * fs / 44100.0));
            //  muelle#: la banda del tanque no depende de ningun mando: es el
            //  aparato. El DAMP sigue mandando sobre `dampC`, que es la cola.
            muLp2[s].pon (fs, kMuelleTope, 0.707);
            muHp[s].pon (fs, kMuelleHz, 0.707);
            muLp2[s].limpia(); muHp[s].limpia();
            //  muelle#: el buffer lleva el lazo MAS lo que se adelantan las
            //  tomas graves: leer «antes» es leer una muestra que entro mas
            //  tarde, asi que hace falta sitio por delante del indice.
            muBaja[s].pon   (fs, kMuelleCorte[0], 0.707);
            muBajaM[s].pon  (fs, kMuelleCorte[0], 0.707);
            muMedia[s].pon  (fs, kMuelleCorte[1], 0.707);
            muMediaA[s].pon (fs, kMuelleCorte[1], 0.707);
            muBaja[s].limpia(); muBajaM[s].limpia(); muMedia[s].limpia(); muMediaA[s].limpia();
            muAntes[0] = juce::jmax (1, (int) std::lround (kMuelleAntesMs[0] * 0.001 * fs));
            muAntes[1] = juce::jmax (1, (int) std::lround (kMuelleAntesMs[1] * 0.001 * fs));
            mu[s].setSize (1, muLen[s] + muAntes[0] + 4, false, true, true);
        }
        aplicaForma (tipoPedido);
        for (int i = 0; i < kDiff; ++i)
        {
            dif[i].setSize (1, difLen[i], false, true, true);
            dif[i].clear();
            difIdx[i] = 0;
        }
        reset();
    }

    void reset (bool conPre = true) noexcept
    {
        for (auto& b : line) b.clear();
        for (auto& b : dif)  b.clear();
        for (auto& b : mu)   b.clear();
        salida = 1.0f;
        if (conPre) { pre.clear(); preIdx = preQueda = 0; }   //  efectos#corte: al cambiar de tipo la entrada sigue, y las reflexiones no arrancan de golpe
        muPico = 0.0f;
        preAct = preObj;
        hpX = hpY = 0.0f;
        for (int s = 0; s < 2; ++s)
        {
            muIdx[s] = 0; muLp[s] = 0.0f;
            for (auto& etapa : muV[s]) for (auto& v : etapa) v = 0.0f;
            muLp2[s].limpia(); muHp[s].limpia();
            muBaja[s].limpia(); muBajaM[s].limpia(); muMedia[s].limpia(); muMediaA[s].limpia();
        }
        muJ = 0;
        for (auto& d : damp) d = 0.0f;
        for (auto& i : lineIdx) i = 0;
        for (auto& i : difIdx)  i = 0;
        quedaSub = 0;
        ladoE[0] = ladoE[1] = 0.0f;
        ladoG[0] = ladoG[1] = 1.0f;
        for (int i = 0; i < nLin; ++i)
        {
            const double a = 2.0 * juce::MathConstants<double>::pi * ((double) i / nLin);
            oscC[i] = (float) std::cos (a); oscS[i] = (float) std::sin (a);
        }
    }

    //  size 0..1 -> cuanto dura la cola. damping 0..1 -> cuanto se apagan los
    //  agudos en cada vuelta, que es lo que separa una sala con cortinas de
    //  una con azulejos.
    //  CUANTO DURA LA COLA, en segundos y derivado de lo que la reverb HACE.
    //
    //  El visor del plato la dibujaba con `tau = 0.12 + 0.75*size*(1-0.55*damp)`
    //  -tres constantes que no existen en esta clase-, y medido contra lo que
    //  hay aqui coinciden abajo y se separan un 36 % arriba: 0.87 s dibujados
    //  contra 0.64 reales. La firma de un ajuste hecho a ojo.
    //
    //  La cuenta es la de la propia rejilla: cada vuelta por una linea de L
    //  muestras multiplica por `fb`, asi que la amplitud a los t segundos es
    //  `fb^(t*fs/L)` y `tau = -L / (fs * ln fb)`. La L es la MEDIA de las
    //  lineas (kRef44), que es lo que un oido oye del conjunto.
    //
    //  Y el amortiguado entra donde de verdad esta: es un paso bajo de un polo
    //  DENTRO del lazo, o sea que la ganancia por vuelta ya no es `fb` sino
    //  `fb * |H(w)|`. A continua vale uno -por eso una cola muy amortiguada
    //  sigue durando en los graves- asi que se evalua a 700 Hz, que es donde
    //  vive lo que se oye de una cola. Sin esto, DAMP no movia el dibujo.
    static float tauSegundos (float size, float damping, double fs, int tipo = kSala) noexcept
    {
        const float fb    = realimentacionDe (size, tipo);
        const float dampC = amortiguadoDe (damping, tipo);

        //  efectos#tipos: el exponente va contra la vuelta sin escalar, asi que la cola solo depende de fb; el muelle es su lazo.
        const double lMedia = (tipoDe ((float) tipo) == kMuelle ? 0.5 * (kMuelle44[0] + kMuelle44[1]) : kRef44) * fs / 44100.0;

        //  |H| de un polo `y += a (x - y)`, o sea a/(1-(1-a)z^-1).
        //  efectos#rev: a 700 Hz; con dieciseis lineas la cola que se mide por RMS cae como la de 700 Hz, no la de 1 kHz.
        const double w  = 2.0 * juce::MathConstants<double>::pi * 700.0 / fs;
        const double p  = 1.0 - (double) dampC;
        const double hm = (double) dampC / std::sqrt (1.0 - 2.0 * p * std::cos (w) + p * p);

        const double g = juce::jlimit (1.0e-4, 0.9999, (double) fb * hm);
        return (float) (-(lMedia / fs) / std::log (g));
    }

    void setParameters (float size, float damping) noexcept
    {
        if (juce::exactlyEqual (size, ultSize) && juce::exactlyEqual (damping, ultDamp)) return;   // rev#pow: sin mando movido no hay 16 pow
        ultSize = size; ultDamp = damping;
        //  Ver realimentacionDe y amortiguadoDe: los dos numeros viven arriba
        //  desde que `tauSegundos` los necesita para decir cuanto dura la cola.
        fb    = realimentacionDe (size, tipo);
        dampC = amortiguadoDe (damping, tipo);
        for (int i = 0; i < nLin; ++i) fbLinea[i] = std::pow (fb, exponente[i]);
    }

    //  efectos#tipos: el tipo se cambia cuando la salida ha bajado a cero (20 ms); el previo, deslizandose.
    void setTipo (float t, float previoMs) noexcept
    {
        tipoPedido = tipoDe (t);
        preObj = (float) (juce::jlimit (0.0, kPrevioMaxMs, (double) previoMs) * 0.001 * fs);
    }
    int getTipo() const noexcept { return tipo; }

    //  calidad#: 8 o 16 lineas. Se aplica cuando la cola calla, igual que el
    //  tipo; mientras suena, la peticion espera.
    void pideLineas (int n) noexcept { pedidoLin = n >= kLines ? kLines : kLines / 2; }
    int  lineasAhora() const noexcept { return nLin; }

    //  rev#osc: lo que el oscilador de cada linea se ha ido de radio uno; el banco lo mira.
    float oscDesvio() const noexcept
    {
        float peor = 0.0f;
        for (int i = 0; i < nLin; ++i)
            peor = juce::jmax (peor, std::abs (std::hypot (oscC[i], oscS[i]) - 1.0f));
        return peor;
    }

    //  Sustituye el contenido del bloque por la senal HUMEDA, que es lo que
    //  este bus necesita: el seco llego al master por el camino directo y
    //  sumarlo otra vez solo lo peinaria.
    //
    //  rev#cpu: UNA red para los dos lados. Eran dos redes de dieciseis lineas,
    //  una por canal, que con una muestra centrada llevaban casi lo mismo; ahora
    //  entra la media de los dos y cada lado sale de las mismas lineas con sus
    //  signos (kSale), que es lo que da el ancho. La mitad de trabajo.
    void process (juce::AudioBuffer<float>& buf, int start, int num) noexcept
    {
        const int ch = juce::jmin (chans, buf.getNumChannels());
        if (ch <= 0 || lineLen[0] <= 0) return;

        float* w[2] = { nullptr, nullptr };
        for (int c = 0; c < ch; ++c) w[c] = buf.getWritePointer (c, start);

        //  rev#osc: la rotacion en float se va de radio uno; se devuelve a uno cada bloque.
        for (int i = 0; i < nLin; ++i)
        {
            float& o0 = oscC[i];
            float& o1 = oscS[i];
            const float r2 = o0 * o0 + o1 * o1;
            const float g = r2 > 1.0e-12f ? 1.0f / std::sqrt (r2) : 0.0f;
            if (g > 0.0f) { o0 *= g; o1 *= g; } else { o0 = 1.0f; o1 = 0.0f; }
        }

        //  rev#cpu: punteros y largos una vez por bloque, no una por muestra y linea.
        //  efectos#tipos: sin nada dentro el tipo nuevo entra ya; sonando, al final del bloque y bajando.
        if (tipoPedido != tipo && ! ringing()) { aplicaForma (tipoPedido); reset(); }
        //  calidad#: y la cuenta de lineas, con la misma regla que el tipo: solo
        //  cuando no queda cola. Cambiarla sonando seria media red nueva en
        //  mitad de una nota, o sea un chasquido.
        if (pedidoLin != nLin && ! ringing())
        {
            nLin = pedidoLin;
            ultSize = ultDamp = -1.0f;   // los fbLinea son de la otra cuenta
            aplicaForma (tipo);
            reset();
        }
        const bool cambia = tipoPedido != tipo;
        const bool muelle = tipo == kMuelle;
        const bool usaPre = nEco > 0 || preAct > 0.0f || preObj > 0.0f;
        float* pp = pre.getWritePointer (0);
        float* mp[2] = { mu[0].getWritePointer (0), mu[1].getWritePointer (0) };
        muPico = 0.0f;

        float* lp[kLines];
        float* dp[kDiff];
        float tamF[kLines];
        int tam[kLines];
        for (int i = 0; i < nLin; ++i)
        {
            tam[i] = ringLen[i]; tamF[i] = (float) tam[i];   // efectos#tipos: el anillo del tipo, no el buffer reservado
            lp[i] = line[i].getWritePointer (0);
        }
        for (int d = 0; d < kDiff; ++d) dp[d] = dif[d].getWritePointer (0);

        //  rev#lado: la red es una, asi que el lado de la cola sale de cuanto entra
        //  por cada lado, con memoria de unos 300 ms; centrado da uno y uno.
        float gL0 = ladoG[0], gR0 = ladoG[1];
        if (ch > 1)
        {
            float eL = 0.0f, eR = 0.0f;
            for (int n = 0; n < num; ++n) { eL += w[0][n] * w[0][n]; eR += w[1][n] * w[1][n]; }
            const float a = 1.0f - std::exp (-(float) num / (0.3f * (float) fs));
            ladoE[0] += a * (eL - ladoE[0]);
            ladoE[1] += a * (eR - ladoE[1]);
            const float t = ladoE[0] + ladoE[1];
            if (t > 1.0e-20f)
            {
                ladoG[0] = std::sqrt (2.0f * ladoE[0] / t);
                ladoG[1] = std::sqrt (2.0f * ladoE[1] / t);
            }
        }
        const float dL = (ladoG[0] - gL0) / (float) juce::jmax (1, num);
        const float dR = (ladoG[1] - gR0) / (float) juce::jmax (1, num);

        for (int n = 0; n < num; ++n)
        {
            float x = ch > 1 ? 0.5f * (w[0][n] + w[1][n]) : w[0][n];

            //  0. Previo y primeras reflexiones, leidos de la misma memoria de entrada.
            pp[preIdx] = x;
            float erL = 0.0f, erR = 0.0f;
            if (usaPre)
            {
                if (preQueda <= 0) preAct = preObj;   // memoria en silencio: salta sin deslizar
                else if (preAct < preObj) preAct = juce::jmin (preObj, preAct + kPreLento);
                else if (preAct > preObj) preAct = juce::jmax (preObj, preAct - kPreLento);
                if (std::abs (x) > 1.0e-6f) preQueda = preTam;
                else if (preQueda > 0) --preQueda;
                for (int j = 0; j < nEco; ++j)
                {
                    const float v = leePre (pp, preAct + ecoD[j]);
                    erL += ecoL[j] * v; erR += ecoR[j] * v;
                }
                if (preAct > 0.0f) x = leePre (pp, preAct);
            }
            if (++preIdx >= preTam) preIdx = 0;
            if (hpA > 0.0f) { const float y = hpA * (hpY + x - hpX); hpX = x; hpY = y; x = y; }

            if (muelle)
            {
                float o[2];
                const int j = muJ;
                for (int sm = 0; sm < 2; ++sm)
                {
                    //  muelle#: TRES TOMAS. La mas larga -el indice de escritura,
                    //  o sea una vuelta entera- lleva los agudos; las otras dos
                    //  se adelantan y llevan medios y graves. El reparto es con
                    //  dos cortes de dos polos, que suman plano.
                    const int largo = muLen[sm] + muAntes[0] + 4;
                    auto toma = [&] (int antes) noexcept
                    { return mp[sm][(muIdx[sm] + antes) % largo]; };
                    //  Y UN FILTRO POR TOMA, no uno compartido: `medios` restaba
                    //  el grave de OTRA toma, asi que el grave se cancelaba
                    //  consigo mismo -la banda de 300 Hz desaparecia del todo-.
                    const float tB = toma (muAntes[0]), tM = toma (muAntes[1]), tA = toma (0);
                    float lp = 0.0f, bp = 0.0f;
                    muBaja[sm].paso   (tB, 0, lp, bp);  const float graves = lp;
                    muBajaM[sm].paso  (tM, 0, lp, bp);  const float bajoM  = lp;
                    muMedia[sm].paso  (tM, 0, lp, bp);  const float medios = lp - bajoM;
                    muMediaA[sm].paso (tA, 0, lp, bp);  const float agudos = tA - lp;
                    float v = graves + medios + agudos;
                    const float a = kMuelleA[sm];
                    //  muelle#: la cadena de allpass ESTIRADOS. Cada etapa es
                    //  `(a + z^-K)/(1 + a z^-K)`: una multiplica y una suma como
                    //  la de primer orden, y K veces su retardo. Con a positivo
                    //  el retardo se lo llevan los agudos: ese es el boing.
                    for (int k = 0; k < kMuelleAp; ++k)
                    {
                        float& ranura = muV[sm][k][j];
                        const float vieja = ranura;              // v[n-K]
                        const float nueva = v - a * vieja;       // v[n]
                        ranura = nueva;
                        v = a * nueva + vieja;
                    }
                    //  La banda del tanque, dentro del lazo: sin ella la cola
                    //  sale con 8 kHz por encima de 1 kHz, o sea a placa fina.
                    muLp2[sm].paso (v, 0, lp, bp);
                    v = lp;
                    //  Y EL PASO ALTO ES `x - k*bp - lp`, la salida alta del
                    //  SVF, no `x - lp`: restar solo el paso bajo deja dentro la
                    //  banda y la primera corrida salio con 60 Hz **+13.8 dB**
                    //  sobre 1 kHz, o sea un realce de graves donde tenia que
                    //  haber un corte.
                    muHp[sm].paso (v, 0, lp, bp);
                    v = v - muHp[sm].k * bp - lp;
                    muLp[sm] += dampC * (v - muLp[sm]);
                    //  Y EL TRANSDUCTOR, que es de donde sale la calidez: lo que
                    //  entra al muelle pasa por una rodilla suave, no por un
                    //  limitador. Armonicos de orden bajo, que es lo que el oido
                    //  llama «analogico».
                    const float e = Analogico::tanhR (kMuelleDrive * (x + fb * muLp[sm])) * (1.0f / kMuelleDrive);
                    const float eb = std::abs (e) <= std::numeric_limits<float>::max() ? e : 0.0f;
                    mp[sm][muIdx[sm]] = eb;
                    muPico = juce::jmax (muPico, std::abs (eb));
                    //  muelle#: la vuelta es el buffer ENTERO, porque las tomas
                    //  leen por delante del indice: dando la vuelta en `muLen`
                    //  esas muestras no existen todavia y la toma grave leia
                    //  ceros -dispersion medida: -0.1 ms, o sea ninguna-.
                    if (++muIdx[sm] >= largo) muIdx[sm] = 0;
                    o[sm] = muLp[sm];
                }
                if (++muJ >= kMuelleK) muJ = 0;
                const float oL = kMuelleSal * (o[0] + kMuelleCruce * o[1]);
                const float oR = kMuelleSal * (o[1] + kMuelleCruce * o[0]);
                if (ch > 1) { w[0][n] = oL; w[1][n] = oR; } else w[0][n] = 0.5f * (oL + oR);
            }
            else
            {
            //  1. Difusion: cuatro allpass en serie. y = -g*x + z ; z' = x + g*y
            for (int d = 0; d < kDiff; ++d)
            {
                float* dl = dp[d];
                const int   di = difIdx[d];
                const float z  = dl[di];
                const float y  = -difG * x + z;
                dl[di] = x + difG * y;
                x = y;
            }

            //  2. Leer las lineas, cada una con su largo respirando.
            //  rev#cpu: la respiracion se recalcula cada kSub muestras; dentro del
            //  tramo la cabeza avanza una muestra por muestra, como la de escritura.
            if (--quedaSub <= 0)
            {
                quedaSub = kSub;
                for (int i = 0; i < nLin; ++i)
                {
                    const float nc = oscC[i] * rotC[i] - oscS[i] * rotS[i];
                    oscS[i] = oscC[i] * rotS[i] + oscS[i] * rotC[i];
                    oscC[i] = nc;
                    const float atras = (float) lineLen[i] + profundidad * (1.0f + oscS[i]);
                    float pos = (float) lineIdx[i] - atras;
                    pos += pos < 0.0f ? tamF[i] : 0.0f;   // atras < tam, basta una vuelta
                    int k = (int) pos;
                    const bool sobra = k >= tam[i];   // rev#i0: -epsilon + tam redondea a tam
                    k -= sobra ? tam[i] : 0;
                    pos -= sobra ? tamF[i] : 0.0f;
                    leeIdx[i] = k;
                    leeFr[i] = pos - (float) k;
                }
            }
            float s[kLines];
            for (int i = 0; i < nLin; ++i)
            {
                const float* r = lp[i];
                const int k = leeIdx[i];
                const int k1 = k + 1 < tam[i] ? k + 1 : 0;
                s[i] = r[k] + leeFr[i] * (r[k1] - r[k]);
                leeIdx[i] = k1;
            }

            //  3. Hadamard de dieciseis, normalizada: ortogonal, conserva la energia.
            //
            //  rev#cpu: LAS CUATRO ETAPAS ESCRITAS, no un bucle con el paso en
            //  una variable. Las mariposas son las MISMAS y en el mismo orden
            //  -cada salida sigue siendo un u+v o un u-v de la misma pareja-,
            //  asi que la cola sale bit a bit igual; lo que se va es el bucle.
            //  Medido en el perfil: la mariposa costaba 1.92 % del render y
            //  los dos bucles que la recorren otro 3.43 %, o sea que mas de la
            //  mitad del Hadamard era contar. Con el paso constante tambien se
            //  puede vectorizar, que es lo que no podia hacer con `paso` vivo.
            static_assert (kLines == 16, "las etapas escritas son para dieciseis lineas");
            float hz[kLines];
            for (int i = 0; i < nLin; ++i) hz[i] = s[i];
            auto mariposa = [&hz] (int j, int paso) noexcept
            {
                const float u = hz[j], v = hz[j + paso];
                hz[j] = u + v; hz[j + paso] = u - v;
            };
            for (int i = 0; i < nLin; i += 2)  mariposa (i, 1);
            for (int i = 0; i < nLin; i += 4)  { mariposa (i, 2); mariposa (i + 1, 2); }
            if (nLin >= 16)
            {
                for (int i = 0; i < nLin; i += 8)  for (int j = 0; j < 4; ++j) mariposa (i + j, 4);
                for (int j = 0; j < 8; ++j) mariposa (j, 8);
            }
            else
                for (int j = 0; j < 4; ++j) mariposa (j, 4);
            const float kNorm = nLin >= kLines ? 0.25f : 0.35355339f;   // 1/sqrt(n)

            //  4. Escribir de vuelta: entrada + realimentacion amortiguada.
            const int pl = pasoLin();
            for (int i = 0; i < nLin; ++i)
            {
                const float v = kEntra[i * pl] * x + fbLinea[i] * kNorm * hz[i];
                //  Un polo por linea: la cola se oscurece con cada vuelta,
                //  que es lo que hace el aire de una sala de verdad.
                const float d = damp[i] + dampC * (v - damp[i]);
                //  Y LA BARRERA, aqui dentro y no a la salida: un NaN que
                //  entra en un estado realimentado se queda para siempre.
                damp[i] = std::abs (d) <= std::numeric_limits<float>::max() ? d : 0.0f;   // rev#cpu: isfinite sin rama
            }
            for (int i = 0; i < nLin; ++i) lp[i][lineIdx[i]] = damp[i];

            //  5. La salida es la suma de las lineas, atenuada para que
            //     dieciseis lineas sumadas no lleguen dieciseis veces mas fuerte.
            for (int c = 0; c < ch; ++c)
            {
                float suma = 0.0f;
                for (int i = 0; i < nLin; ++i) suma += kSale[c][i * pl] * s[i];
                w[c][n] = salidaDe() * suma;
            }
            }
            if (nEco > 0)
            {
                if (ch > 1) { w[0][n] += erL; w[1][n] += erR; }
                else          w[0][n] += 0.5f * (erL + erR);
            }
            if (ch > 1)
            {
                gL0 += dL; gR0 += dR;
                w[0][n] *= gL0; w[1][n] *= gR0;
            }

            for (int i = 0; i < nLin; ++i)
                if (++lineIdx[i] >= tam[i]) lineIdx[i] = 0;
            for (int d = 0; d < kDiff; ++d)
                if (++difIdx[d] >= difLen[d]) difIdx[d] = 0;
        }

        if (cambia)
        {
            //  efectos#corte: la bajada dura 20 ms y no un bloque; en un bloque chasqueaba al cambiar de preset.
            const float paso = 1.0f / (0.020f * (float) fs);
            float g = salida;
            for (int n = 0; n < num; ++n)
            {
                g = juce::jmax (0.0f, g - paso);
                for (int c = 0; c < ch; ++c) w[c][n] *= g;
            }
            salida = g;
            if (g <= 0.0f) { aplicaForma (tipoPedido); reset (false); salida = 0.0f; }
        }
        else if (salida < 1.0f)
        {
            //  efectos#corte: y el tipo nuevo sube en 150 ms, lo que tardan sus lineas en dar la primera vuelta.
            const float paso = 1.0f / (0.150f * (float) fs);
            float g = salida;
            for (int n = 0; n < num; ++n)
            {
                g = juce::jmin (1.0f, g + paso);
                for (int c = 0; c < ch; ++c) w[c][n] *= g;
            }
            salida = g;
        }
    }

    //  ¿QUEDA ENERGIA DENTRO?
    //
    //  El motor decide si un bus sigue vivo mirando la MAGNITUD DE SU SALIDA,
    //  y una FDN no saca nada hasta que la primera linea da la vuelta - unas
    //  1600 muestras, tres bloques de 512. Con Freeverb no se noto nunca
    //  porque sus peines son cortos y sueltan algo en la primera muestra; con
    //  esto, el bus se apagaba ANTES de que la cola llegase a salir y la
    //  reverb no sonaba en absoluto. Medido: pico 0.0000 en la sonda.
    //
    //  Asi que la reverb dice si tiene energia dentro, en vez de que se deduzca
    //  de lo que ya ha salido.
    bool ringing() const noexcept
    {
        //  efectos#tipos: lo que espera en el previo y lo que da vueltas en el muelle tambien es energia.
        if (preQueda > 0 || ! (muPico <= 1.0e-6f)) return true;
        for (float v : damp)
        {
            //  Y UN NaN CUENTA COMO ENERGIA. `std::abs(NaN) > 1e-6` es
            //  FALSO -comparar con NaN siempre lo es- asi que un bus
            //  envenenado se declaraba MUERTO: dejaba de renderizarse y por
            //  tanto no habia forma de que se limpiara solo. La barrera de
            //  arriba lo impide, y esto es el cinturon: lo que no es finito
            //  no es silencio.
            if (! std::isfinite (v) || std::abs (v) > 1.0e-6f) return true;
        }
        return false;
    }

private:
    //  calidad#: con ocho lineas la suma lleva la mitad de energia, asi que la
    //  salida se compensa por la raiz de la cuenta y el nivel no se mueve.
    float salidaDe() const noexcept { return nLin >= kLines ? 0.125f : 0.17677670f; }
    static constexpr float kPreLento = 0.25f;   // efectos#tipos: muestras por muestra al deslizar el previo

    void aplicaForma (int t) noexcept
    {
        tipo = tipoDe ((float) t);
        const auto& f = kForma[tipo];
        const int pl = pasoLin();
        for (int i = 0; i < nLin; ++i)
            lineLen[i] = juce::jmax (16, (int) std::lround (kBase44[i * pl] * f.escala * fs / 44100.0));
        //  efectos#rev: cada linea pierde por vuelta segun lo que mide, asi todas caen al mismo ritmo.
        for (int i = 0; i < nLin; ++i) exponente[i] = (float) (lineLen[i] / (kRef44 * fs / 44100.0));
        //  efectos#rev: la longitud de cada linea respira unas muestras, a ritmos que no se repiten; rompe los modos fijos.
        profundidad = (float) (f.mod44 * fs / 44100.0);
        const int extra = (int) std::ceil (2.0f * profundidad) + 4;
        for (int i = 0; i < nLin; ++i) ringLen[i] = lineLen[i] + extra;
        difG = f.difG;
        hpA = f.hpHz > 0.0f ? (float) std::exp (-2.0 * juce::MathConstants<double>::pi * f.hpHz / fs) : 0.0f;
        nEco = f.erGan > 0.0f ? kEcos : 0;
        for (int j = 0; j < kEcos; ++j)
        {
            const auto& e = kEco[tipo][j];
            ecoD[j] = (float) (e.ms * 0.001 * fs);
            ecoL[j] = f.erGan * e.gan * (e.pan > 0.0f ? 1.0f - e.pan : 1.0f);
            ecoR[j] = f.erGan * e.gan * (e.pan < 0.0f ? 1.0f + e.pan : 1.0f);
        }
        ultSize = ultDamp = -1.0f;
    }

    float leePre (const float* r, float d) const noexcept
    {
        float pos = (float) preIdx - d;
        pos += pos < 0.0f ? (float) preTam : 0.0f;
        int k = (int) pos;
        const bool sobra = k >= preTam;
        k -= sobra ? preTam : 0;
        pos -= sobra ? (float) preTam : 0.0f;
        const int k1 = k + 1 < preTam ? k + 1 : 0;
        return r[k] + (pos - (float) k) * (r[k1] - r[k]);
    }

    //  efectos#rev: entrada y salida que no son filas de Hadamard; una fila mete toda la energia en una linea.
    //  efectos#rev: los dos lados con la misma entrada y la salida con otros signos; con vectores distintos la cola salia x0.56 de un lado.
    static constexpr float kEntra[kLines] = {  1.01f, -1.15f, -1.09f, -0.81f,  0.85f,  1.14f,  0.70f, -1.11f,  1.10f, -0.93f, -0.85f, -0.84f,  0.83f,  0.92f,  0.95f, -0.98f };
    static constexpr float kSale[2][kLines]  = { { -0.72f,  1.00f,  0.78f, -1.04f, -0.71f,  0.86f, -1.17f,  0.97f,  1.11f,  1.03f,  1.01f,  0.80f, -0.99f, -0.72f, -1.10f,  1.18f },
                                                 { -0.72f, -1.00f,  0.78f, -1.04f, -0.71f, -0.86f,  1.17f,  0.97f, -1.11f, -1.03f,  1.01f,  0.80f, -0.99f,  0.72f,  1.10f, -1.18f } };

    //  calidad#: cuantas lineas CORREN (8 o 16) y cuantas se piden. El cambio
    //  lo aplica `process` cuando la cola ha callado.
    int nLin = kLines, pedidoLin = kLines;
    //  Con ocho se usan las PARES de las dieciseis: una de cada dos.
    int pasoLin() const noexcept { return kLines / juce::jmax (1, nLin); }

    double fs = 44100.0;
    int    chans = 2;

    juce::AudioBuffer<float> line[kLines];
    juce::AudioBuffer<float> dif[kDiff];
    int lineLen[kLines] {}, lineIdx[kLines] {};
    int difLen[kDiff] {},   difIdx[kDiff] {};
    float damp[kLines] {};
    float exponente[kLines] {}, fbLinea[kLines] {};
    float rotC[kLines] {}, rotS[kLines] {};
    float oscC[kLines] {}, oscS[kLines] {};   // rev#cpu: coseno y seno en filas separadas
    static constexpr int kSub = 32;   // rev#cpu: muestras entre dos recalculos de la respiracion
    int leeIdx[kLines] {}, quedaSub = 0;
    float leeFr[kLines] {};
    float ladoE[2] {}, ladoG[2] { 1.0f, 1.0f };   // rev#lado: energia que entra por cada lado y la ganancia de cada salida
    float profundidad = 0.0f;

    int tipo = kSala, tipoPedido = kSala;
    int ringLen[kLines] {};
    float difG = 0.62f;
    juce::AudioBuffer<float> pre;
    int preTam = 1, preIdx = 0, preQueda = 0;
    float preAct = 0.0f, preObj = 0.0f;
    float salida = 1.0f;   //  efectos#corte: ganancia de la bajada al cambiar de tipo
    float hpA = 0.0f, hpX = 0.0f, hpY = 0.0f;
    int nEco = 0;
    float ecoD[kEcos] {}, ecoL[kEcos] {}, ecoR[kEcos] {};
    juce::AudioBuffer<float> mu[2];
    int muLen[2] {}, muIdx[2] {};
    float muLp[2] {}, muPico = 0.0f;
    //  muelle#: el estado de los allpass estirados. `muJ` es el indice comun:
    //  todas las etapas usan el mismo estiron, asi que una vuelta por muestra.
    float muV[2][kMuelleAp][kMuelleK] {};
    int   muJ = 0;
    //  muelle#: la banda del tanque, un filtro por muelle y por lado.
    Analogico::Svf muHp[2], muLp2[2];
    //  muelle#: los dos cortes que reparten las tres tomas, y lo que cada toma
    //  se adelanta en muestras.
    //  Cuatro y no dos: cada banda sale de SU toma, asi que el corte de 700 Hz
    //  hace falta en la toma grave y en la media, y el de 2200 en la media y en
    //  la aguda.
    Analogico::Svf muBaja[2], muBajaM[2], muMedia[2], muMediaA[2];
    int muAntes[2] {};

    float fb    = 0.85f;
    float ultSize = -1.0f, ultDamp = -1.0f;
    float dampC = 0.4f;
};
