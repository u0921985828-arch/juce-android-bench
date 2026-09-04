#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>

// ============================================================================
//  LA FAMILIA DE DINAMICA: CMP · GTE · DSS · LIM.
//
//  UNA SOLA PIEZA CON CUATRO CONFIGURACIONES Y NO CUATRO COPIAS. Las cuatro
//  son lo mismo: un DETECTOR de envolvente y un CALCULADOR de ganancia, y lo
//  unico que cambia entre ellas es la CURVA que convierte nivel en ganancia y
//  si la deteccion esta filtrada. Cuatro clases serian la misma regla escrita
//  cuatro veces, y la cuarta es la que un dia se escribe mal — que es
//  exactamente lo que ya se pago con `normaliza` dentro de `render` y con el
//  reparto de un kit dentro de CARGAR KIT.
//
//  SIN RESERVAR MEMORIA Y SIN UN SOLO `delete`. Todo el estado son floats
//  planos declarados aqui; `prepare` solo apunta la frecuencia de muestreo y
//  pone los filtros a cero. Es la misma razon por la que `Eq5` escribe las
//  formulas de RBJ a mano en vez de llamar a `juce::dsp::IIR::Coefficients`,
//  que devuelve un `ReferenceCountedObjectPtr` y por tanto RESERVA.
//
//  DETECCION ENLAZADA, o sea el maximo de los dos canales y una sola ganancia
//  para los dos. Un compresor con un detector por canal baja el lado que pega
//  y deja el otro donde estaba, asi que la imagen estereo se mueve con cada
//  golpe — que es el fallo clasico y el que nadie oye hasta que lo señalan.
//  Es la misma leccion que el ancho de un pad: lo que estrecha sin mover el
//  centro se hace en medio/lado, y lo que no puede mover el centro es lo que
//  actua igual en los dos.
//
//  ATAQUE Y CAIDA DE CMP POR CONSTANTE MUSICAL Y NO POR MANDO. Con tres
//  parametros y MIX ocupando uno, un compresor de cinco numeros no cabe en la
//  fila — y ese es el mismo argumento que trajo la cara propia del EQ. Cuando
//  haga falta, CMP traera la suya: curva de transferencia con el umbral
//  arrastrable y el medidor de reduccion dentro. Queda anotado, no se hace
//  ahora.
// ============================================================================
struct Dinamica
{
    enum Modo { compresor = 0, puerta, deesser, limitador };

    //  Un integrador doble de un filtro de variable de estado. Aqui arriba y no
    //  con el resto del estado porque lo nombran las dos funciones de abajo.
    struct Svf { float s1 = 0.0f, s2 = 0.0f; };

    //  Ataque y caida de cada modo, en milisegundos. No son cuatro numeros a
    //  ojo: un compresor de bus quiere dejar pasar el transitorio -5 ms- y
    //  soltar con el pulso -80-; una puerta tiene que ABRIR instantanea o se
    //  come el ataque de lo que deja pasar, y su cierre lo pone la persona; y
    //  un limitador ataca en cero por definicion -si no, no limita- y suelta
    //  con su mando.
    static constexpr float kAtaqueCmp  =  5.0f;
    static constexpr float kCaidaCmp   = 80.0f;
    static constexpr float kAtaquePta  =  0.5f;
    static constexpr float kAtaqueDss  =  1.0f;
    static constexpr float kCaidaDss   = 60.0f;
    //  CERO, y es una medida: el limitador tiene que sujetar en la MUESTRA en
    //  la que se pasa. Con 0.1 ms el detector va por detras de la señal y el
    //  pico salia a 0.5361 con el techo en 0.5012, o sea un 7 % por encima de
    //  lo unico que este efecto promete. Sin mirar hacia delante no hay otra
    //  forma. La SUBIDA de la ganancia si va con su mando: eso es la
    //  recuperacion, y es lo que se oye.
    static constexpr float kAtaqueLim  =  0.0f;

    //  LA RODILLA, en dB y a los dos lados del umbral. Sin ella la curva tiene
    //  un pico en la derivada justo en el umbral y una señal que lo cruza a
    //  cada ciclo -o sea cualquier onda- suena a bombeo de grano fino. Seis dB
    //  es lo que separa un compresor de bus de uno de canal.
    static constexpr float kRodilla = 6.0f;

    //  CUANTOS dB SE QUITAN, dado cuantos sobran del umbral. Es la curva
    //  ESTATICA de un compresor: lo que su bucle calcula por muestra y lo que
    //  la fila del rack DIBUJA. Desde que hay dos clientes vive aqui, porque
    //  la rodilla es una parabola con una constante dentro y no un minimo: el
    //  dia que se moviera, un dibujo con la copia vieja seguiria pareciendo
    //  correcto. La puerta y el limitador no pasan por aqui a proposito - un
    //  escalon y un `min` no son una regla, son su propia definicion.
    static float bajaDb (float sobre, float rr) noexcept
    {
        if (sobre > kRodilla * 0.5f)  return sobre - sobre / rr;
        if (sobre > -kRodilla * 0.5f)
        {
            //  La rodilla: una parabola que empalma con pendiente 0 abajo y
            //  con 1-1/r arriba, o sea sin escalon en la derivada. Un `if` a
            //  secas ahi es lo que se oye como bombeo de grano fino.
            const float x = sobre + kRodilla * 0.5f;
            return (1.0f - 1.0f / rr) * x * x / (2.0f * kRodilla);
        }
        return 0.0f;
    }

    //  Y EL RATIO EFECTIVO, que en un de-esser no es un mando sino la mitad de
    //  lo que significa FUERZA. Ver el comentario de `procesa`.
    static float ratioDe (Modo modo, float p1) noexcept
    {
        if (modo == compresor) return juce::jmax (1.0f, p1);
        return 2.0f + 6.0f * juce::jlimit (0.0f, 1.0f, p1);       // de-esser
    }

    //  Y DONDE ESTA SU UMBRAL: el de-esser lo saca de FUERZA y los demas lo
    //  llevan en su primer mando.
    static float umbralDeDb (Modo modo, float p0, float p1) noexcept
    {
        if (modo != deesser) return p0;
        return -6.0f - 30.0f * juce::jlimit (0.0f, 1.0f, p1);
    }

    void prepare (double fs) noexcept
    {
        sr = fs > 0.0 ? fs : 48000.0;
        reset();
    }

    void reset() noexcept
    {
        env = 0.0f;
        gSuave = 1.0f;
        for (auto& c : ramaAlta) for (auto& e : c) e = {};
        for (auto& c : ramaBaja) for (auto& e : c) e = {};
        reduccion = 0.0f;
    }

    //  Cuanto esta bajando, en dB y positivo. Es lo que la casilla de lectura
    //  de CTRL 3 enseña mientras el efecto es CMP, GTE o LIM: sin ella «no se
    //  si esta comprimiendo» no tiene respuesta, y un compresor invisible es
    //  un compresor que nadie usa.
    float reduccionDb() const noexcept { return reduccion; }

    //  p0 y p1 son los dos mandos del modo -ver la tabla de `fxDefs`-. La
    //  MEZCLA no entra aqui: la hace el bus, como con todos los demas.
    void procesa (float* const* ch, int chans, int inicio, int n,
                  Modo modo, float p0, float p1) noexcept
    {
        if (n <= 0 || chans <= 0) return;

        float* l = ch[0] + inicio;
        float* r = (chans > 1 ? ch[1] : ch[0]) + inicio;

        const float ataqueMs = (modo == compresor ? kAtaqueCmp
                              : modo == puerta    ? kAtaquePta
                              : modo == deesser   ? kAtaqueDss
                                                  : kAtaqueLim);
        //  La CAIDA la pone el mando en dos de los cuatro -CIERRE en la puerta
        //  y SOLTAR en el limitador- y es constante en los otros dos.
        const float caidaMs  = (modo == compresor ? kCaidaCmp
                              : modo == puerta    ? juce::jmax (5.0f, p1)
                              : modo == deesser   ? kCaidaDss
                                                  : juce::jmax (5.0f, p1));

        const float aA = (ataqueMs <= 0.0f ? 0.0f : coef (ataqueMs));
        const float aR = coef (caidaMs);

        //  DSS DETECTA FILTRADO Y ACTUA SOLO SOBRE LO FILTRADO. Un de-esser
        //  que baja la señal entera cuando aparece una ese es un compresor con
        //  el detector torcido: baja tambien la voz. Dos polos de paso alto en
        //  la frecuencia del mando, uno para detectar y otro para separar la
        //  banda sobre la que se aplica.
        //  UN CORTE DE VERDAD Y NO «LA SEÑAL MENOS SU PASO BAJO». El primer
        //  intento partia con dos polos de uno en cascada y el banco lo canto:
        //  a 9 kHz con el corte en 6, esa banda «alta» se quedaba con el 30 %
        //  del tono y el otro 70 % pasaba intacto por debajo, asi que el
        //  de-esser no podia bajar la ese mas de un decibelio -medido, 0.96-
        //  por mucho que se le pidiera. Un `x - LP` de un polo cae SEIS dB en
        //  su propia esquina, no tres.
        //
        //  Y NO BASTA CON UN PASO ALTO: hay que partir en DOS RAMAS. El
        //  segundo intento sacaba la banda alta de verdad -0.55 de 0.60 a
        //  9 kHz, medido- y restaba `alta * (1 - g)` de la entrada, y seguia
        //  bajando UN decibelio: un paso alto de segundo orden en 9 kHz tiene
        //  0.914 de modulo y una fase de 59 grados, asi que `1 - H` vale 0.95
        //  y restarlo no cancela nada. Lo que se resta tiene que estar EN
        //  FASE, y para eso hacen falta las dos ramas del cruce.
        //
        //  En CUARTO orden -dos etapas de Butterworth por rama, o sea
        //  Linkwitz-Riley- porque es el unico cruce cuyas dos mitades SUMAN
        //  plano: con dos etapas de segundo orden sin mas, la suma tiene un
        //  bache de 3 dB justo en el corte y el de-esser bajaria la voz por
        //  no hacer nada.
        const float fc = (modo == deesser ? juce::jlimit (1000.0f, 14000.0f, p0) : 0.0f);
        const float gT = (modo == deesser
                            ? std::tan (juce::MathConstants<float>::pi * fc / (float) sr)
                            : 0.0f);
        const float kQ = 1.4142f;                       // Butterworth
        const float a1 = 1.0f / (1.0f + gT * (gT + kQ));
        const float a2 = gT * a1;
        const float a3 = gT * a2;

        //  EL UMBRAL DEL DE-ESSER SALE DE SU MANDO Y NO DE UNA CONSTANTE.
        //  Estuvo clavado en -18 dB una version y el banco lo canto: una ese
        //  de 7 kHz a 0.6 de amplitud sale del paso alto de dos polos a -21 dB
        //  -que un «x menos su paso bajo» cae 6 dB en su propia esquina, no 3-
        //  o sea POR DEBAJO del umbral, y el efecto no hacia absolutamente
        //  nada con FUERZA a tope. Y ademas FUERZA era un mando que se movia y
        //  no hacia nada, que es peor que no tenerlo.
        //
        //  Un solo mando mueve las dos cosas -el umbral y el ratio- porque eso
        //  es lo que «cuanto» significa en un de-esser: al 0 apenas toca las
        //  eses mas fuertes, y al 1 las persigue con 8:1.
        const float umbralDb = umbralDeDb (modo, p0, p1);
        const float umbral   = juce::Decibels::decibelsToGain (umbralDb, -100.0f);

        float peorG = 1.0f;

        for (int i = 0; i < n; ++i)
        {
            //  --- el detector ---------------------------------------------
            float dL = l[i], dR = r[i];
            float altaL = 0.0f, altaR = 0.0f, bajaL = 0.0f, bajaR = 0.0f;
            if (modo == deesser)
            {
                cruza (l[i], ramaAlta[0], ramaBaja[0], a1, a2, a3, kQ, bajaL, altaL);
                cruza (r[i], ramaAlta[1], ramaBaja[1], a1, a2, a3, kQ, bajaR, altaR);
                dL = altaL; dR = altaR;
            }

            const float pico = juce::jmax (std::abs (dL), std::abs (dR));
            env = (pico > env) ? aA * env + (1.0f - aA) * pico
                               : aR * env + (1.0f - aR) * pico;

            //  --- el calculador de ganancia -------------------------------
            float g = 1.0f;
            switch (modo)
            {
                case compresor:
                case deesser:
                {
                    //  Ver bajaDb: la rodilla vive alli desde que la fila del
                    //  rack dibuja esta misma curva.
                    const float rr    = ratioDe (modo, p1);
                    const float eDb   = juce::Decibels::gainToDecibels (env, -100.0f);
                    const float baja  = bajaDb (eDb - umbralDb, rr);
                    g = juce::Decibels::decibelsToGain (-baja);
                    break;
                }
                case puerta:
                {
                    //  La puerta no INTERPOLA una ganancia: abre o cierra, y
                    //  lo que suaviza el salto es el filtro de abajo con su
                    //  ataque y su cierre. Escrito como una rampa aqui, el
                    //  cierre dependeria del nivel y no del mando.
                    g = (env >= umbral) ? 1.0f : 0.0f;
                    break;
                }
                case limitador:
                {
                    //  El techo es un techo: por debajo no hace absolutamente
                    //  nada -g = 1, bit a bit- y por encima devuelve
                    //  exactamente lo que hace falta para no pasarlo.
                    g = (env > umbral && env > 1.0e-9f) ? umbral / env : 1.0f;
                    break;
                }
            }

            //  --- el suavizado, que es donde vive el tiempo ----------------
            //  La ganancia se mueve con las mismas dos constantes que el
            //  detector: bajar rapido -es la accion- y subir despacio -es la
            //  recuperacion-. Al reves, el limitador dejaria pasar el
            //  transitorio que existe para atrapar.
            //  EL LIMITADOR BAJA DE GOLPE Y NO CON SU ATAQUE. Es la
            //  diferencia entre un limitador y un compresor rapido, y es una
            //  medida: con el ataque de 0.1 ms puesto tambien en la bajada, el
            //  pico salia a 0.5403 con el techo en 0.5012 — un 8 % por encima
            //  de lo unico que este efecto promete. Sin mirar hacia delante no
            //  hay otra forma: se sujeta en la muestra en la que pasa. La
            //  SUBIDA si va con su mando, que es lo que se oye como
            //  recuperacion.
            const float aG = (g < gSuave ? (modo == limitador ? 0.0f : aA) : aR);
            gSuave = aG * gSuave + (1.0f - aG) * g;
            peorG = juce::jmin (peorG, gSuave);

            if (modo == deesser)
            {
                //  Solo la banda alta cambia: lo de abajo pasa igual. Es lo
                //  que separa un de-esser de un compresor con el detector
                //  filtrado, y se mide con DOS cifras — cuanto baja la
                //  sibilancia y cuanto NO se mueve el fundamental.
                l[i] = bajaL + altaL * gSuave;
                if (chans > 1) r[i] = bajaR + altaR * gSuave;
            }
            else
            {
                l[i] *= gSuave;
                if (chans > 1) r[i] *= gSuave;
            }
        }

        //  La lectura se queda con lo PEOR del bloque y no con el ultimo
        //  valor: a 128 muestras el ultimo cae donde caiga y la aguja
        //  parpadearia sin decir nada.
        reduccion = -juce::Decibels::gainToDecibels (juce::jmax (1.0e-4f, peorG));
    }

private:
    //  Un filtro de variable de estado por transposicion. Devuelve las dos
    //  salidas de una vez porque el cruce necesita las dos y calcularlas por
    //  separado seria correr el mismo filtro dos veces.
    static void svf (float x, Svf& st, float a1, float a2, float a3, float k,
                     float& lp, float& hp) noexcept
    {
        const float v3 = x - st.s2;
        const float v1 = a1 * st.s1 + a2 * v3;
        const float v2 = st.s2 + a2 * st.s1 + a3 * v3;
        st.s1 = 2.0f * v1 - st.s1;
        st.s2 = 2.0f * v2 - st.s2;
        lp = v2;
        hp = x - k * v1 - v2;
    }

    //  EL CRUCE, con las DOS ramas y en cuarto orden. Ver el parrafo de
    //  `procesa`: la banda alta sola no vale, porque restarsela a la entrada
    //  no cancela nada -van desfasadas-.
    static void cruza (float x, Svf (&alta)[2], Svf (&baja)[2],
                       float a1, float a2, float a3, float k,
                       float& lp, float& hp) noexcept
    {
        float l1 = 0.0f, h1 = 0.0f, l2 = 0.0f, h2 = 0.0f;
        svf (x,  alta[0], a1, a2, a3, k, l1, h1);
        svf (h1, alta[1], a1, a2, a3, k, l2, hp);
        svf (x,  baja[0], a1, a2, a3, k, l1, h2);
        svf (l1, baja[1], a1, a2, a3, k, lp, h2);
    }

    float coef (float ms) const noexcept
    {
        const float t = juce::jmax (0.01f, ms) * 0.001f;
        return std::exp (-1.0f / (t * (float) sr));
    }

    double sr = 48000.0;
    float env = 0.0f;
    float gSuave = 1.0f;
    //  Cuatro estados por canal: dos etapas para la rama alta y dos para la
    //  baja. Ver `svf` y el parrafo del cruce.
    Svf ramaAlta[2][2] {}, ramaBaja[2][2] {};
    float reduccion = 0.0f;
};
