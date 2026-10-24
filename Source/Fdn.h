#pragma once

#include <JuceHeader.h>
#include <array>
#include <cmath>

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

    //  LOS DOS NUMEROS QUE MANDAN EN LA COLA, por la misma razon. 0.72 a 0.94:
    //  por debajo no es una sala y por encima de 0.95 una FDN
    //  empieza a sonar a bucle infinito antes que a sitio. Y el polo va al
    //  reves que el mando: damping alto = mas oscuro.
    static float realimentacionDe (float size) noexcept
    { return 0.72f + 0.22f * juce::jlimit (0.0f, 1.0f, size); }
    static float amortiguadoDe (float damping) noexcept
    { return 0.05f + 0.75f * juce::jlimit (0.0f, 1.0f, damping); }

    void prepare (double sampleRate, int channels)
    {
        fs = juce::jmax (8000.0, sampleRate);
        chans = juce::jlimit (1, 2, channels);

        //  Los difusores, tambien primos y mucho mas cortos: su trabajo es
        //  romper el impulso, no crear cola.
        static constexpr int dif44[kDiff]   = { 142, 107, 379, 277 };

        for (int i = 0; i < kLines; ++i)
            lineLen[i] = juce::jmax (16, (int) std::lround (kBase44[i] * fs / 44100.0));
        //  efectos#rev: cada linea pierde por vuelta segun lo que mide, asi todas caen al mismo ritmo.
        for (int i = 0; i < kLines; ++i) exponente[i] = (float) (lineLen[i] / (kRef44 * fs / 44100.0));
        //  efectos#rev: la longitud de cada linea respira unas muestras, a ritmos que no se repiten; rompe los modos fijos.
        profundidad = (float) (kModMuestras44 * fs / 44100.0);
        for (int i = 0; i < kLines; ++i)
        {
            const double hz = (0.31 + 0.137 * i);
            const double w = 2.0 * juce::MathConstants<double>::pi * hz / fs;
            rotC[i] = (float) std::cos (w); rotS[i] = (float) std::sin (w);
        }
        const int extra = (int) std::ceil (2.0f * profundidad) + 4;
        for (int i = 0; i < kDiff; ++i)
            difLen[i] = juce::jmax (8, (int) std::lround (dif44[i] * fs / 44100.0));

        for (int i = 0; i < kLines; ++i)
        {
            line[i].setSize (chans, lineLen[i] + extra, false, true, true);
            line[i].clear();
            lineIdx[i] = 0;
        }
        for (int i = 0; i < kDiff; ++i)
        {
            dif[i].setSize (chans, difLen[i], false, true, true);
            dif[i].clear();
            difIdx[i] = 0;
        }
        reset();
    }

    void reset() noexcept
    {
        for (auto& b : line) b.clear();
        for (auto& b : dif)  b.clear();
        for (auto& d : damp) d[0] = d[1] = 0.0f;
        for (auto& i : lineIdx) i = 0;
        for (auto& i : difIdx)  i = 0;
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < kLines; ++i)
            {
                const double a = 2.0 * juce::MathConstants<double>::pi * ((double) i / kLines + c * 0.31);
                osc[c][i][0] = (float) std::cos (a); osc[c][i][1] = (float) std::sin (a);
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
    static float tauSegundos (float size, float damping, double fs) noexcept
    {
        const float fb    = realimentacionDe (size);
        const float dampC = amortiguadoDe (damping);

        const double lMedia = kRef44 * fs / 44100.0;

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
        //  Ver realimentacionDe y amortiguadoDe: los dos numeros viven arriba
        //  desde que `tauSegundos` los necesita para decir cuanto dura la cola.
        fb    = realimentacionDe (size);
        dampC = amortiguadoDe (damping);
        for (int i = 0; i < kLines; ++i) fbLinea[i] = std::pow (fb, exponente[i]);
    }

    //  rev#osc: lo que el oscilador de cada linea se ha ido de radio uno; el banco lo mira.
    float oscDesvio() const noexcept
    {
        float peor = 0.0f;
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < kLines; ++i)
                peor = juce::jmax (peor, std::abs (std::hypot (osc[c][i][0], osc[c][i][1]) - 1.0f));
        return peor;
    }

    //  Sustituye el contenido del bloque por la senal HUMEDA, que es lo que
    //  este bus necesita: el seco llego al master por el camino directo y
    //  sumarlo otra vez solo lo peinaria.
    void process (juce::AudioBuffer<float>& buf, int start, int num) noexcept
    {
        const int ch = juce::jmin (chans, buf.getNumChannels());
        if (ch <= 0 || lineLen[0] <= 0) return;

        float* w[2] = { nullptr, nullptr };
        for (int c = 0; c < ch; ++c) w[c] = buf.getWritePointer (c, start);

        //  rev#osc: la rotacion en float se va de radio uno; se devuelve a uno cada bloque.
        for (auto& oc : osc)
            for (auto& o : oc)
            {
                const float r2 = o[0] * o[0] + o[1] * o[1];
                const float g = r2 > 1.0e-12f ? 1.0f / std::sqrt (r2) : 0.0f;
                if (g > 0.0f) { o[0] *= g; o[1] *= g; } else { o[0] = 1.0f; o[1] = 0.0f; }
            }

        for (int n = 0; n < num; ++n)
        {
            for (int c = 0; c < ch; ++c)
            {
                float x = w[c][n];

                //  1. Difusion: cuatro allpass en serie. y = -g*x + z ; z' = x + g*y
                for (int d = 0; d < kDiff; ++d)
                {
                    float* dl = dif[d].getWritePointer (c);
                    const int   di = difIdx[d];
                    const float z  = dl[di];
                    const float y  = -kDiffG * x + z;
                    dl[di] = x + kDiffG * y;
                    x = y;
                }

                //  2. Leer las lineas, cada una con su largo respirando.
                float s[kLines];
                for (int i = 0; i < kLines; ++i)
                {
                    auto& o = osc[c][i];
                    const float nc = o[0] * rotC[i] - o[1] * rotS[i];
                    o[1] = o[0] * rotS[i] + o[1] * rotC[i];
                    o[0] = nc;
                    const float atras = (float) lineLen[i] + profundidad * (1.0f + o[1]);
                    const int   tam   = line[i].getNumSamples();
                    float pos = (float) lineIdx[i] - atras;
                    while (pos < 0.0f) pos += (float) tam;
                    int i0 = (int) pos;
                    if (i0 >= tam) { i0 -= tam; pos -= (float) tam; }   // rev#i0: -epsilon + tam redondea a tam
                    const float fr = pos - (float) i0;
                    const float* r = line[i].getReadPointer (c);
                    const float a0 = r[i0], a1 = r[i0 + 1 < tam ? i0 + 1 : 0];
                    s[i] = a0 + fr * (a1 - a0);
                }

                //  3. Hadamard de dieciseis, normalizada: ortogonal, conserva la energia.
                float hz[kLines];
                for (int i = 0; i < kLines; ++i) hz[i] = s[i];
                for (int paso = 1; paso < kLines; paso <<= 1)
                    for (int i = 0; i < kLines; i += paso << 1)
                        for (int j = i; j < i + paso; ++j)
                        {
                            const float u = hz[j], v = hz[j + paso];
                            hz[j] = u + v; hz[j + paso] = u - v;
                        }
                constexpr float kNorm = 0.25f;   // 1/sqrt(16)

                //  4. Escribir de vuelta: entrada + realimentacion amortiguada.
                for (int i = 0; i < kLines; ++i)
                {
                    float v = kEntra[i] * x + fbLinea[i] * kNorm * hz[i];
                    //  Un polo por linea: la cola se oscurece con cada vuelta,
                    //  que es lo que hace el aire de una sala de verdad.
                    damp[i][c] += dampC * (v - damp[i][c]);
                    v = damp[i][c];
                    //  Y LA BARRERA, aqui dentro y no a la salida.
                    //
                    //  Las tres que tiene la app -Voice::start, fastTanh y el
                    //  master- protegen lo que SALE, y este estado se
                    //  realimenta: un NaN que entre una vez se queda dentro
                    //  para siempre, y solo se limpia en prepareToPlay, o sea
                    //  al cambiar de ruta. La reverb quedaba muda hasta
                    //  reiniciar la app. Comparar con NaN siempre es falso, asi
                    //  que se pregunta por lo finito y no por lo malo.
                    if (! std::isfinite (v)) { v = 0.0f; damp[i][c] = 0.0f; }
                    line[i].getWritePointer (c)[lineIdx[i]] = v;
                }

                //  5. La salida es la suma de las lineas, atenuada para que
                //     dieciseis lineas sumadas no lleguen dieciseis veces mas fuerte.
                float suma = 0.0f;
                for (int i = 0; i < kLines; ++i) suma += kSale[c][i] * s[i];
                w[c][n] = kSalida * suma;
            }

            //  Los indices avanzan UNA vez por muestra, no una por canal: son
            //  la posicion en la linea, no un estado del canal.
            for (int i = 0; i < kLines; ++i)
                if (++lineIdx[i] >= line[i].getNumSamples()) lineIdx[i] = 0;
            for (int d = 0; d < kDiff; ++d)
                if (++difIdx[d] >= difLen[d]) difIdx[d] = 0;
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
        for (const auto& d : damp)
            for (float v : d)
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
    static constexpr float kDiffG = 0.62f;
    static constexpr float kSalida = 0.125f;
    static constexpr double kModMuestras44 = 6.0;

    //  efectos#rev: entrada y salida que no son filas de Hadamard; una fila mete toda la energia en una linea.
    //  efectos#rev: los dos lados con la misma entrada y la salida con otros signos; con vectores distintos la cola salia x0.56 de un lado.
    static constexpr float kEntra[kLines] = {  1.01f, -1.15f, -1.09f, -0.81f,  0.85f,  1.14f,  0.70f, -1.11f,  1.10f, -0.93f, -0.85f, -0.84f,  0.83f,  0.92f,  0.95f, -0.98f };
    static constexpr float kSale[2][kLines]  = { { -0.72f,  1.00f,  0.78f, -1.04f, -0.71f,  0.86f, -1.17f,  0.97f,  1.11f,  1.03f,  1.01f,  0.80f, -0.99f, -0.72f, -1.10f,  1.18f },
                                                 { -0.72f, -1.00f,  0.78f, -1.04f, -0.71f, -0.86f,  1.17f,  0.97f, -1.11f, -1.03f,  1.01f,  0.80f, -0.99f,  0.72f,  1.10f, -1.18f } };

    double fs = 44100.0;
    int    chans = 2;

    juce::AudioBuffer<float> line[kLines];
    juce::AudioBuffer<float> dif[kDiff];
    int lineLen[kLines] {}, lineIdx[kLines] {};
    int difLen[kDiff] {},   difIdx[kDiff] {};
    float damp[kLines][2] {};
    float exponente[kLines] {}, fbLinea[kLines] {};
    float rotC[kLines] {}, rotS[kLines] {};
    float osc[2][kLines][2] {};
    float profundidad = 0.0f;

    float fb    = 0.85f;
    float dampC = 0.4f;
};
