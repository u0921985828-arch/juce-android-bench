#pragma once

#include <JuceHeader.h>
#include <array>
#include <cmath>

// ============================================================================
//  EL ECUALIZADOR DE CINCO BANDAS, Y EL PILOTO DE «CADA EFECTO TRAE SU PROPIA
//  SUPERFICIE».
//
//  Los seis efectos de esta maquina se tocan con CTRL 1, CTRL 2 y CTRL 3, y el
//  tercero es siempre MIX -que es ademas el interruptor-. O sea DOS mandos
//  libres por efecto, y con dos mandos un ecualizador de cinco bandas no cabe:
//  hacen falta diez numeros. Ese es justamente el efecto que obliga a que un
//  efecto pueda traer su propia cara en vez de pedir prestados los tres mandos
//  de todos.
//
//  Lo que NO cambia es el contrato del interruptor: MIX sigue siendo el
//  parametro 2 de su fila en `fxDefs`, asi que `setFxEnabled`, la luz de la
//  ranura y el fichero de proyecto siguen funcionando sin tocarlos. Los diez
//  numeros de las bandas viven APARTE, como el acorde vive aparte de la nota:
//  la raiz se queda donde estaba y lo de mas va al lado.
//
//
//  SIN RESERVAR MEMORIA, que es lo que descarta la ayuda de JUCE.
//
//  `juce::dsp::IIR::Coefficients::makePeakFilter` devuelve un
//  ReferenceCountedObjectPtr, o sea que RESERVA. Recalcular los coeficientes
//  cuando alguien mueve una banda es trabajo del hilo de audio -es donde se
//  sabe que el bloque siguiente los necesita- y ahi la regla de esta casa es
//  cero reservas, cero cerrojos, cero E/S. Asi que las formulas de RBJ estan
//  escritas a mano sobre floats planos: son quince lineas por tipo de filtro y
//  no reservan nada.
//
//
//  CINCO BANDAS Y NO TRES NI DIEZ. Tres no llegan a separar el cuerpo de la
//  presencia en una voz -que es para lo que se pidio- y diez no se pueden
//  apuntar con el dedo. Medido en la pantalla mas estrecha que nadie fabrica,
//  280x653: a cinco bandas les tocan 48 px de ancho cada una -por encima del
//  dedo minimo- y a diez les tocarian 24, la mitad. Lo publica la app y lo
//  juzga Tests/eq.py, que ademas es donde se ve el numero.
//
//  Las frecuencias de fabrica son el reparto clasico -80, 250, 1k, 3.5k, 10k-
//  y se pueden mover, pero NO cruzarse: dos bandas que se adelantan una a otra
//  dejan la curva con la que se ve y la que suena en desacuerdo, y un nodo que
//  salta al otro lado del vecino no se puede arrastrar.
// ============================================================================
class Eq5
{
public:
    static constexpr int kBands = 5;

    //  Los extremos son ESTANTES y los tres del medio CAMPANAS. Un pico en el
    //  extremo deja el subgrave y el aire sin tocar por debajo y por encima de
    //  el, que es justo donde se quiere actuar al bajar retumbe o al subir
    //  brillo.
    enum Tipo { estanteBajo = 0, campana, estanteAlto };

    static constexpr float kFreqDef[kBands] = { 80.0f, 250.0f, 1000.0f, 3500.0f, 10000.0f };
    static constexpr float kFreqMin = 30.0f;
    static constexpr float kFreqMax = 16000.0f;
    static constexpr float kGainMax = 12.0f;      // dB, a los dos lados

    //  Lo que la banda i puede recorrer sin adelantar a sus vecinas. Un tercio
    //  de octava de guarda: pegadas del todo, dos campanas se suman en vez de
    //  esculpir y la curva deja de decir lo que hace.
    static constexpr float kGuarda = 1.26f;       // ~ un tercio de octava

    Eq5() noexcept
    {
        for (int b = 0; b < kBands; ++b) freq[(size_t) b] = kFreqDef[b];
    }

    //  LAS BANDAS NO SE TOCAN AQUI, y eso no es un olvido. `prepareToPlay`
    //  vuelve a llamarse en CADA cambio de ruta de audio, asi que devolver las
    //  cinco a su sitio de fabrica aqui significaria que enchufar unos cascos
    //  deshace el ecualizador en mitad de una sesion - que es exactamente el
    //  fallo que ya costo una medida con el ancho estereo de un pad. Lo que
    //  cambia con la ruta es la frecuencia de muestreo, y por eso se recalcula.
    void prepare (double sampleRate) noexcept
    {
        fs = sampleRate > 0.0 ? sampleRate : 48000.0;
        sucio = true;
        reset();
        recalcula();
    }

    void reset() noexcept
    {
        for (auto& b : banda)
            for (auto& c : b.z)
                c = { 0.0f, 0.0f };
        //  Y la rampa de salida al valor de destino y no a uno: si no, cada
        //  cambio de ruta de audio arranca la etapa con un fundido de veinte
        //  milisegundos desde la unidad, que con la salida bajada es un golpe.
        smSalida = std::pow (10.0f, salida / 20.0f);
    }

    //  Del hilo de MENSAJES. Marca y se va: quien recalcula es el de audio, en
    //  el bloque siguiente, que es donde se sabe que hacen falta.
    void ponBanda (int b, float hz, float dB) noexcept
    {
        if (! juce::isPositiveAndBelow (b, kBands)) return;
        freq[(size_t) b] = juce::jlimit (kFreqMin, kFreqMax, hz);
        gain[(size_t) b] = juce::jlimit (-kGainMax, kGainMax, dB);
        sucio = true;
    }

    //  LOS DOS QUE LA CURVA NO PUEDE DECIR, y por eso son mandos y no nodos.
    //
    //  Un nodo lleva DOS numeros -donde y cuanto- y arrastrarlo los mueve los
    //  dos. Lo ANCHO que es una campana es el tercero, y no hay tercer eje en
    //  un dedo: meterlo como un pellizco seria un cuarto significado en el
    //  mismo gesto, que es lo que esta casa lleva escrito que no se puede
    //  aprender. Y la SALIDA no es de ninguna banda: cinco bandas subidas se
    //  comen el margen del master, y eso se corrige con un solo numero.
    //
    //  Los dos viven en `fxParams` como los de cualquier otro efecto -o sea
    //  que se guardan solos y se mueven desde el XY- y no aqui: aqui solo se
    //  aplican. Un numero, un dueno.
    void ponAncho (float a) noexcept
    {
        const float v = juce::jlimit (kAnchoMin, kAnchoMax, a);
        if (v != ancho) { ancho = v; sucio = true; }
    }
    void ponSalida (float dB) noexcept
    {
        salida = juce::jlimit (-kGainMax, kGainMax, dB);
        //  No ensucia: es una multiplicacion a la salida y no toca un solo
        //  coeficiente. Recalcular diez biquads por mover el volumen seria
        //  trabajo tirado en el hilo de audio.
    }

    static constexpr float kAnchoMin = 0.40f;   // ancha: media curva por banda
    static constexpr float kAnchoMax = 3.00f;   // estrecha: un quirurgico
    static constexpr float kAnchoDef = 1.00f;

    float freqDe (int b) const noexcept
    {
        return juce::isPositiveAndBelow (b, kBands) ? freq[(size_t) b] : 0.0f;
    }
    float gainDe (int b) const noexcept
    {
        return juce::isPositiveAndBelow (b, kBands) ? gain[(size_t) b] : 0.0f;
    }
    float anchoDe()  const noexcept { return ancho;  }
    float salidaDe() const noexcept { return salida; }

    //  El techo de cada banda, para que la cara pinte el nodo donde de verdad
    //  puede ir. Se calcula aqui y no en la cara: la regla de no cruzarse es
    //  del filtro, y escrita dos veces serian dos reglas.
    float minDe (int b) const noexcept
    {
        if (! juce::isPositiveAndBelow (b, kBands)) return kFreqMin;
        return b == 0 ? kFreqMin : freq[(size_t) (b - 1)] * kGuarda;
    }
    float maxDe (int b) const noexcept
    {
        if (! juce::isPositiveAndBelow (b, kBands)) return kFreqMax;
        return b == kBands - 1 ? kFreqMax : freq[(size_t) (b + 1)] / kGuarda;
    }

    //  DEL HILO DE AUDIO. Cinco biquads por canal, en sitio.
    void procesa (float* const* datos, int canales, int inicio, int n) noexcept
    {
        if (sucio) recalcula();

        //  La SALIDA se suaviza y se aplica en RAMPA dentro del bloque, como
        //  cualquier otra ganancia que multiplica una señal: un salto crudo es
        //  un chasquido, y eso vale igual entre bloque y bloque que dentro de
        //  uno. Los dos extremos se calculan UNA vez y los dos canales usan la
        //  misma rampa - un suavizado por canal serian dos ganancias distintas
        //  en los dos lados, o sea el ancho estereo moviendose solo.
        const float objetivo = std::pow (10.0f, salida / 20.0f);
        const float paso = 1.0f - std::exp (-(float) n / (0.020f * (float) fs));
        const float g0 = smSalida;
        smSalida += paso * (objetivo - smSalida);
        const float g1 = smSalida;
        const bool  conSalida = (std::abs (g0 - 1.0f) > 1.0e-5f || std::abs (g1 - 1.0f) > 1.0e-5f);

        for (int ch = 0; ch < canales && ch < 2; ++ch)
        {
            float* p = datos[ch] + inicio;
            for (int b = 0; b < kBands; ++b)
            {
                auto& f = banda[(size_t) b];
                //  Una banda a cero no se procesa. Y es un ahorro de CPU y NO
                //  una correccion, que es lo que salio al romperlo a proposito:
                //  a 0 dB el A del cookbook vale 1, asi que el numerador y el
                //  denominador salen identicos -b0 = 1, b1 = a1, b2 = a2- y el
                //  biquad es paso directo EXACTO, bit a bit, con guarda y sin
                //  ella. Quitarla no cambia una sola muestra; lo que cambia es
                //  que con la curva plana -que es como nace- el EQ pasa de
                //  costar cero a costar diez biquads por bloque.
                if (f.plana) continue;
                auto& z = f.z[(size_t) ch];
                for (int i = 0; i < n; ++i)
                {
                    const float x = p[i];
                    const float y = f.b0 * x + z.a;
                    z.a = f.b1 * x - f.a1 * y + z.b;
                    z.b = f.b2 * x - f.a2 * y;
                    p[i] = y;
                }
            }

            if (conSalida && n > 0)
            {
                const float d = (g1 - g0) / (float) n;
                for (int i = 0; i < n; ++i) p[i] *= g0 + d * (float) i;
            }
        }
    }

    //  La respuesta en dB a una frecuencia, para que la cara dibuje la MISMA
    //  curva que suena en vez de una parecida. Se evalua el biquad de verdad
    //  -la funcion de transferencia en z = e^{jw}- y no una campana de adorno:
    //  dibujar una aproximacion es como se llega a una curva que promete algo
    //  distinto de lo que hace.
    float respuestaEnDb (float hz) const noexcept
    {
        const double w = 2.0 * juce::MathConstants<double>::pi * (double) hz / fs;
        const double cw = std::cos (w), sw = std::sin (w);
        //  cos(2w) y sin(2w) sin volver a llamar al trigonometrico.
        const double c2 = 2.0 * cw * cw - 1.0, s2 = 2.0 * sw * cw;

        double mag = 1.0;
        for (const auto& f : banda)
        {
            if (f.plana) continue;
            const double nr = (double) f.b0 + (double) f.b1 * cw + (double) f.b2 * c2;
            const double ni = -((double) f.b1 * sw + (double) f.b2 * s2);
            const double dr = 1.0 + (double) f.a1 * cw + (double) f.a2 * c2;
            const double di = -((double) f.a1 * sw + (double) f.a2 * s2);
            const double d2 = dr * dr + di * di;
            if (d2 > 1.0e-20) mag *= std::sqrt ((nr * nr + ni * ni) / d2);
        }
        //  Y LA SALIDA ENTRA EN LO QUE SE DIBUJA. Es una ganancia plana, asi
        //  que en dB es una suma: dejarla fuera daria una curva centrada en
        //  cero mientras el efecto sube o baja seis decibelios, que es la
        //  forma exacta de que lo que se ve y lo que suena no digan lo mismo.
        return (float) (20.0 * std::log10 (juce::jmax (1.0e-6, mag))) + salida;
    }

private:
    struct Estado { float a = 0.0f, b = 0.0f; };
    struct Banda
    {
        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
        bool  plana = true;
        std::array<Estado, 2> z {};
    };

    //  Formulas de RBJ, escritas desde el cookbook y no copiadas de JUCE: asi
    //  no hay reserva y ademas los dos lados -lo que suena y lo que se dibuja-
    //  salen de los MISMOS cinco numeros.
    void recalcula() noexcept
    {
        for (int b = 0; b < kBands; ++b)
        {
            auto& f = banda[(size_t) b];
            const float dB = gain[(size_t) b];

            //  Media decima de dB no se oye y cuesta dos multiplicaciones por
            //  muestra y por banda. Plana es plana.
            if (std::abs (dB) < 0.05f) { f.plana = true; continue; }
            f.plana = false;

            const double A  = std::pow (10.0, (double) dB / 40.0);
            const double w  = 2.0 * juce::MathConstants<double>::pi
                                * (double) juce::jlimit (kFreqMin, (float) (fs * 0.45), freq[(size_t) b]) / fs;
            const double cw = std::cos (w), sw = std::sin (w);
            //  Q = 0.7 por el mando en el centro: mas estrecho y una banda no
            //  llega a la siguiente, asi que la curva sale con dientes entre
            //  nodo y nodo; mas ancho y las cinco se solapan hasta ser un tono
            //  general. Ese es el punto que ANCHO escala, y por eso el mando va
            //  de 0.40 a 3.00 y no de 0 a 1: lo que se multiplica es una Q.
            const double q  = 0.7 * (double) ancho;
            const double al = sw / (2.0 * q);

            double b0, b1, b2, a0, a1, a2;
            const Tipo t = (b == 0) ? estanteBajo : (b == kBands - 1) ? estanteAlto : campana;

            if (t == campana)
            {
                b0 = 1.0 + al * A;  b1 = -2.0 * cw;      b2 = 1.0 - al * A;
                a0 = 1.0 + al / A;  a1 = -2.0 * cw;      a2 = 1.0 - al / A;
            }
            else
            {
                //  Los estantes usan la S del cookbook y no la Q: con S = 1 la
                //  pendiente es la mas empinada que no rebota.
                const double sq = 2.0 * std::sqrt (A) * al;
                if (t == estanteBajo)
                {
                    b0 =      A * ((A + 1.0) - (A - 1.0) * cw + sq);
                    b1 =  2.0 * A * ((A - 1.0) - (A + 1.0) * cw);
                    b2 =      A * ((A + 1.0) - (A - 1.0) * cw - sq);
                    a0 =           (A + 1.0) + (A - 1.0) * cw + sq;
                    a1 =    -2.0 * ((A - 1.0) + (A + 1.0) * cw);
                    a2 =           (A + 1.0) + (A - 1.0) * cw - sq;
                }
                else
                {
                    b0 =      A * ((A + 1.0) + (A - 1.0) * cw + sq);
                    b1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * cw);
                    b2 =      A * ((A + 1.0) + (A - 1.0) * cw - sq);
                    a0 =           (A + 1.0) - (A - 1.0) * cw + sq;
                    a1 =     2.0 * ((A - 1.0) - (A + 1.0) * cw);
                    a2 =           (A + 1.0) - (A - 1.0) * cw - sq;
                }
            }

            const double inv = (std::abs (a0) > 1.0e-12) ? 1.0 / a0 : 1.0;
            f.b0 = (float) (b0 * inv); f.b1 = (float) (b1 * inv); f.b2 = (float) (b2 * inv);
            f.a1 = (float) (a1 * inv); f.a2 = (float) (a2 * inv);
        }
        sucio = false;
    }

    double fs = 48000.0;
    std::array<Banda, kBands> banda {};
    //  Los diez numeros. No son atomicos: los escribe el hilo de mensajes y los
    //  lee el de audio a traves de `sucio`, que si lo es - un float mal leido
    //  aqui vale un bloque con la banda a medio mover, no un fallo.
    std::array<float, kBands> freq {};
    std::array<float, kBands> gain {};
    float ancho  = kAnchoDef;
    float salida = 0.0f;          // dB
    float smSalida = 1.0f;        // lineal, solo del hilo de audio
    std::atomic<bool> sucio { true };
};
