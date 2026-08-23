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
//  Una FDN de cuatro lineas con matriz Hadamard es el paso siguiente y cabe de
//  sobra en el presupuesto: cuatro lineas de retardo, un polo de amortiguacion
//  en cada una, y una matriz ortogonal que las mezcla entre si. Ortogonal
//  importa: conserva la energia, asi que la cola decae por el amortiguador y
//  por la ganancia de realimentacion, y NO por como se mezclan las lineas. Con
//  una matriz cualquiera la cola se descompensa y aparecen resonancias.
//
//  Delante, dos allpass de difusion: un impulso entra como un impulso y sale
//  como una nube. Sin ellos la FDN suena a cuatro ecos separados durante los
//  primeros cien milisegundos, que es exactamente donde el oido decide si algo
//  es una sala o un delay.
//
//  TIEMPO REAL: todo se reserva en prepare(). process() solo lee y escribe
//  dentro de buffers que ya existen, no reserva, no bloquea y no libera.
class Fdn
{
public:
    void prepare (double sampleRate, int channels)
    {
        fs = juce::jmax (8000.0, sampleRate);
        chans = juce::jlimit (1, 2, channels);

        //  Longitudes en muestras a la frecuencia de trabajo, escaladas desde
        //  las de 44.1 kHz. PRIMAS ENTRE SI a proposito: si dos lineas
        //  comparten divisor, sus ecos coinciden una y otra vez y la cola
        //  suena a tubo en vez de a sala.
        static constexpr int base44[kLines] = { 1447, 1637, 1861, 2053 };
        //  Los dos difusores, tambien primos y mucho mas cortos: su trabajo es
        //  romper el impulso, no crear cola.
        static constexpr int dif44[kDiff]   = { 241, 359 };

        int maxLen = 0;
        for (int i = 0; i < kLines; ++i)
        {
            lineLen[i] = juce::jmax (16, (int) std::lround (base44[i] * fs / 44100.0));
            maxLen = juce::jmax (maxLen, lineLen[i]);
        }
        for (int i = 0; i < kDiff; ++i)
            difLen[i] = juce::jmax (8, (int) std::lround (dif44[i] * fs / 44100.0));

        for (int i = 0; i < kLines; ++i)
        {
            line[i].setSize (chans, lineLen[i], false, true, true);
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
        for (auto& d : damp) d = { 0.0f, 0.0f };
        for (auto& i : lineIdx) i = 0;
        for (auto& i : difIdx)  i = 0;
    }

    //  size 0..1 -> cuanto dura la cola. damping 0..1 -> cuanto se apagan los
    //  agudos en cada vuelta, que es lo que separa una sala con cortinas de
    //  una con azulejos.
    void setParameters (float size, float damping) noexcept
    {
        //  0.72 a 0.94: por debajo no es una sala y por encima de 0.95 una FDN
        //  de cuatro lineas empieza a sonar a bucle infinito antes que a sitio.
        fb   = 0.72f + 0.22f * juce::jlimit (0.0f, 1.0f, size);
        //  El polo va al reves que el mando: damping alto = mas oscuro.
        dampC = 0.05f + 0.75f * juce::jlimit (0.0f, 1.0f, damping);
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

        for (int n = 0; n < num; ++n)
        {
            for (int c = 0; c < ch; ++c)
            {
                float x = w[c][n];

                //  1. Difusion: dos allpass en serie. y = -g*x + z ; z' = x + g*y
                for (int d = 0; d < kDiff; ++d)
                {
                    float* dl = dif[d].getWritePointer (c);
                    const int   di = difIdx[d];
                    const float z  = dl[di];
                    const float y  = -kDiffG * x + z;
                    dl[di] = x + kDiffG * y;
                    x = y;
                }

                //  2. Leer las cuatro lineas.
                float s[kLines];
                for (int i = 0; i < kLines; ++i)
                    s[i] = line[i].getReadPointer (c)[lineIdx[i]];

                //  3. Hadamard normalizada: ortogonal, conserva la energia.
                //     Escrita a mano porque son ocho sumas y ninguna
                //     multiplicacion mas que la de 0.5.
                const float h0 = 0.5f * ( s[0] + s[1] + s[2] + s[3]);
                const float h1 = 0.5f * ( s[0] - s[1] + s[2] - s[3]);
                const float h2 = 0.5f * ( s[0] + s[1] - s[2] - s[3]);
                const float h3 = 0.5f * ( s[0] - s[1] - s[2] + s[3]);
                const float hz[kLines] = { h0, h1, h2, h3 };

                //  4. Escribir de vuelta: entrada + realimentacion amortiguada.
                for (int i = 0; i < kLines; ++i)
                {
                    float v = x + fb * hz[i];
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
                //     cuatro lineas sumadas no lleguen cuatro veces mas fuerte.
                w[c][n] = 0.35f * (s[0] + s[1] + s[2] + s[3]);
            }

            //  Los indices avanzan UNA vez por muestra, no una por canal: son
            //  la posicion en la linea, no un estado del canal.
            for (int i = 0; i < kLines; ++i)
                if (++lineIdx[i] >= lineLen[i]) lineIdx[i] = 0;
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
    static constexpr int   kLines = 4;
    static constexpr int   kDiff  = 2;
    static constexpr float kDiffG = 0.62f;

    double fs = 44100.0;
    int    chans = 2;

    std::array<juce::AudioBuffer<float>, kLines> line;
    std::array<juce::AudioBuffer<float>, kDiff>  dif;
    std::array<int, kLines> lineLen {}, lineIdx {};
    std::array<int, kDiff>  difLen {},  difIdx {};
    std::array<std::array<float, 2>, kLines> damp {};

    float fb    = 0.85f;
    float dampC = 0.4f;
};
