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

    //  CINCO TIPOS Y NO TRES, y cualquier banda puede ser cualquiera.
    //
    //  Los de fabrica son estante en los extremos y campana en medio -un pico
    //  en el extremo deja el subgrave y el aire sin tocar por debajo y por
    //  encima de el, que es justo donde se quiere actuar al bajar retumbe o al
    //  subir brillo- y ese reparto es un DEFECTO, no una regla: la banda 0
    //  puede pasar a paso alto para quitar retumbe de raiz, y la 4 a paso bajo
    //  para cortar aire. Lo que un ecualizador de verdad deja hacer.
    enum Tipo { estanteBajo = 0, campana, estanteAlto, pasoAlto, pasoBajo, kNumTipos };

    //  Los dos de paso CORTAN, no realzan: su ganancia no significa nada y por
    //  eso el nodo se queda clavado en la linea de cero. Lo que los gobierna es
    //  la Q, y esa se toca en el menu de la banda.
    static bool esPaso (Tipo t) noexcept { return t == pasoAlto || t == pasoBajo; }

    static const char* nombreTipo (int t) noexcept
    {
        switch ((Tipo) t)
        {
            case estanteBajo: return "ESTANTE B";
            case campana:     return "CAMPANA";
            case estanteAlto: return "ESTANTE A";
            case pasoAlto:    return "PASO ALTO";
            case pasoBajo:    return "PASO BAJO";
            default:          return "CAMPANA";
        }
    }

    static Tipo tipoDeFabrica (int b) noexcept
    {
        return b == 0 ? estanteBajo : (b == kBands - 1 ? estanteAlto : campana);
    }

    static constexpr float kFreqDef[kBands] = { 80.0f, 250.0f, 1000.0f, 3500.0f, 10000.0f };
    //  DE 20 Hz A 20 kHz, que es la banda audible entera. Estaban en 30 y
    //  16 000 y se quedaban cortas por los dos lados: por abajo no se llegaba
    //  al sub, y 16 kHz es donde un estante alto EMPIEZA a servir, no donde
    //  acaba. Es ademas el recorrido que el HPF de `fxDefs` ya usa.
    //
    //  Y NO 22 kHz, que es lo primero que uno escribe: a 44.1 kHz -que es lo
    //  que entrega media Android- Nyquist son 22 050, asi que una banda en
    //  22 000 cae a **0.9977 de Nyquist** y ahi el mapeo bilineal de RBJ manda
    //  la frecuencia al infinito. El tope de abajo la habria arrastrado en
    //  silencio a 19 845, o sea el mando diciendo una cosa y el filtro
    //  haciendo otra - el fallo que ya costo una medida con el corte del pad.
    static constexpr float kFreqMin = 20.0f;
    static constexpr float kFreqMax = 20000.0f;
    static constexpr float kGainMax = 12.0f;      // dB, a los dos lados

    //  Lo que la banda i puede recorrer sin adelantar a sus vecinas. Un tercio
    //  de octava de guarda: pegadas del todo, dos campanas se suman en vez de
    //  esculpir y la curva deja de decir lo que hace.
    static constexpr float kGuarda = 1.26f;       // ~ un tercio de octava

    //  HASTA DONDE LLEGA UNA BANDA DE VERDAD, escrito UNA vez. Vivia dentro de
    //  `recalcula` como un `fs * 0.45` suelto, y ese numero deja 19 845 Hz a
    //  44.1 kHz: con el techo en 20 000 la banda mas aguda se habria quedado
    //  156 Hz por debajo de donde dice el mando. A 0.49 da 21 609 a 44.1 k,
    //  asi que los 20 kHz se alcanzan; por encima de esa frecuencia de
    //  muestreo el que manda es `kFreqMax` y no Nyquist.
    float topeUtil() const noexcept
    {
        return juce::jmin (kFreqMax, (float) (fs * 0.49));
    }

    Eq5() noexcept
    {
        for (int b = 0; b < kBands; ++b)
        {
            freq[(size_t) b] = kFreqDef[b];
            tipo[(size_t) b] = tipoDeFabrica (b);
            q   [(size_t) b] = kQDef;
        }
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

    //  EL TIPO Y LA Q DE UNA BANDA, que es lo que el menu de la banda toca.
    //
    //  La Q es de CADA banda y no una para las cinco: una campana estrecha para
    //  quitar un zumbido de 50 Hz y otra ancha para dar cuerpo son la misma
    //  sesion, y con una Q global hay que elegir. El mando ANCHO de la fila
    //  sigue existiendo y las MULTIPLICA a todas -es el mando de «abre o cierra
    //  el EQ entero»- que es lo que un mando de fila puede decir.
    void ponTipo (int b, int t) noexcept
    {
        if (! juce::isPositiveAndBelow (b, kBands)) return;
        const Tipo nuevo = (Tipo) juce::jlimit (0, (int) kNumTipos - 1, t);
        if (nuevo != tipo[(size_t) b]) { tipo[(size_t) b] = nuevo; sucio = true; }
    }
    void ponQ (int b, float v) noexcept
    {
        if (! juce::isPositiveAndBelow (b, kBands)) return;
        const float nueva = juce::jlimit (kQMin, kQMax, v);
        if (nueva != q[(size_t) b]) { q[(size_t) b] = nueva; sucio = true; }
    }
    Tipo  tipoDe (int b) const noexcept
    {
        return juce::isPositiveAndBelow (b, kBands) ? tipo[(size_t) b] : campana;
    }
    float qDe (int b) const noexcept
    {
        return juce::isPositiveAndBelow (b, kBands) ? q[(size_t) b] : kQDef;
    }

    //  Q de 0.3 -media curva, para dar cuerpo- a 12 -quirurgica, para sacar un
    //  zumbido-. El defecto es 0.7: mas estrecho y una banda no llega a la
    //  siguiente, asi que la curva sale con dientes entre nodo y nodo.
    static constexpr float kQMin = 0.30f;
    static constexpr float kQMax = 12.0f;
    static constexpr float kQDef = 0.70f;

    //  RECALCULAR DESDE FUERA, y esto no es una comodidad: es un FALLO que se
    //  vio en una foto. Los coeficientes solo se recalculaban dentro de
    //  `procesa`, o sea en el hilo de audio, y el ESPEJO desde el que la cara
    //  dibuja no procesa audio nunca. Resultado: `respuestaEnDb` leia una tabla
    //  de coeficientes que seguia siendo la de la curva PLANA y la cara pintaba
    //  una raya recta con los cinco nodos movidos. Ni el banco del motor lo veia
    //  -mide sobre una instancia que acaba de procesar- ni podia verlo
    //  `Tests/eq.py`, que miraba las ganancias y no la forma.
    void refresca() noexcept { if (sucio) recalcula(); }

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
    //  El nodo de un tipo de PASO se dibuja en la linea de cero: su ganancia no
    //  significa nada, asi que arrastrarlo arriba y abajo no puede mover algo
    //  que no existe. Lo dice la clase y no la cara, que es donde vive la regla.
    float gainVisible (int b) const noexcept
    {
        return esPaso (tipoDe (b)) ? 0.0f : gainDe (b);
    }

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
            const Tipo  t  = tipo[(size_t) b];

            //  Media decima de dB no se oye y cuesta dos multiplicaciones por
            //  muestra y por banda. Plana es plana - y en los tipos de PASO no
            //  aplica, porque ahi la ganancia no significa nada: un paso alto
            //  a 0 dB sigue cortando.
            if (! esPaso (t) && std::abs (dB) < 0.05f) { f.plana = true; continue; }
            f.plana = false;

            const double A  = std::pow (10.0, (double) dB / 40.0);
            const double w  = 2.0 * juce::MathConstants<double>::pi
                                * (double) juce::jlimit (kFreqMin, topeUtil(), freq[(size_t) b]) / fs;
            const double cw = std::cos (w), sw = std::sin (w);
            //  La Q de la banda por el mando ANCHO de la fila, que las escala
            //  todas: dos numeros con dos dueños distintos -uno de la banda y
            //  otro del efecto- y no dos formas de decir lo mismo.
            const double qq = juce::jlimit ((double) kQMin, (double) kQMax,
                                            (double) q[(size_t) b] * (double) ancho);
            const double al = sw / (2.0 * qq);

            double b0, b1, b2, a0, a1, a2;

            if (t == campana)
            {
                b0 = 1.0 + al * A;  b1 = -2.0 * cw;      b2 = 1.0 - al * A;
                a0 = 1.0 + al / A;  a1 = -2.0 * cw;      a2 = 1.0 - al / A;
            }
            else if (t == pasoAlto)
            {
                //  RBJ high-pass. Corta por debajo y deja pasar por encima.
                b0 =  (1.0 + cw) * 0.5;  b1 = -(1.0 + cw);  b2 = (1.0 + cw) * 0.5;
                a0 =   1.0 + al;         a1 = -2.0 * cw;    a2 =  1.0 - al;
            }
            else if (t == pasoBajo)
            {
                b0 =  (1.0 - cw) * 0.5;  b1 =  (1.0 - cw);  b2 = (1.0 - cw) * 0.5;
                a0 =   1.0 + al;         a1 = -2.0 * cw;    a2 =  1.0 - al;
            }
            else
            {
                //  Los estantes usan la S del cookbook y no la Q: con S = 1 la
                //  pendiente es la mas empinada que no rebota. La Q de la banda
                //  entra igual, que es lo que hace que un estante se pueda
                //  poner mas o menos abrupto.
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
    std::array<Tipo,  kBands> tipo {};
    std::array<float, kBands> q {};
    float ancho  = kAnchoDef;
    float salida = 0.0f;          // dB
    float smSalida = 1.0f;        // lineal, solo del hilo de audio
    std::atomic<bool> sucio { true };
};
