#include "Sintes.h"

#include <cmath>
#include <cstring>
#include <vector>

// ============================================================================
//  Sintes — la parte que suena. Ver Sintes.h para el porque de las zonas.
// ============================================================================
namespace Sintes
{
    using namespace Kits::detail;
    static constexpr double kRate = Kits::kRate;

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
            //  Los cuatro comunes, de las 256: significan lo mismo en las
            //  dieciseis formas, asi que su limite es musical y no de la forma.
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

        //  Y las fases de arranque por la proporcion aurea: arrancar todos en
        //  cero es un pico enorme en la primera muestra y ademas los pone de
        //  acuerdo justo cuando el ataque los esta destapando.
        inline double faseDe (int k) noexcept
        {
            const double x = 0.6180339887 * (double) (k + 1);
            return x - std::floor (x);
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
    static void rinde (float* d, int len, const Familia& F, const Preset& P,
                       double hz, int capa, int semilla, float congelaEn = -1.0f)
    {
        Rng rnd (semilla);
        Svf f1, f2, f3;

        const bool  duro   = (capa == 1);
        const float fuerza = duro ? 1.00f : 0.52f;
        const float brillo = P.brillo * (duro ? 1.00f : 0.45f);
        const float indice = duro ? 1.00f : 0.42f;
        //  Y EL TERCER MANDO DE LA CAPA, que hizo falta despues de medir: en
        //  media familia el filtro no puede cambiar el timbre porque no hay
        //  nada que filtrar -un seno, tres parciales, ocho barras- y con la
        //  capa metida solo en el corte, seis familias median centroide x1.00.
        //  Esto escala el CONTENIDO: armonicos de mas, ruido de mas, parciales
        //  de mas. Ver Tests/instr.py, que mide las dos cosas a la vez.
        const float capaMix = duro ? 1.00f : 0.30f;

        const double inc  = hz / kRate;
        const double nyq  = kRate * 0.48;

        double ph = 0.0, ph2 = 0.0, ph3 = 0.0, ph4 = 0.0, phm = 0.0, lfo = 0.0;
        double arm[16] = {};                      // fases de los aditivos
        for (int k = 0; k < 16; ++k) arm[k] = faseDe (k);
        std::vector<float> cuerda;                // Karplus-Strong
        int    ksPos = 0, ksLen = 0;
        float  ksPrev = 0.0f;

        if (F.forma == fGuitarra)
        {
            ksLen = juce::jmax (2, (int) std::lround (kRate / juce::jmax (20.0, hz)));
            cuerda.assign ((size_t) ksLen, 0.0f);
            //  La pua: ruido en toda la cuerda, y un peine que dice DONDE se
            //  pulsa. Sin el peine todas las pulsaciones suenan al mismo sitio.
            const int pos = juce::jlimit (1, ksLen - 1, (int) (P.p2 * (float) ksLen));
            for (int i = 0; i < ksLen; ++i) cuerda[(size_t) i] = rnd();
            for (int i = ksLen - 1; i >= pos; --i)
                cuerda[(size_t) i] -= cuerda[(size_t) (i - pos)];
        }

        for (int n = 0; n < len; ++n)
        {
            const float t = (float) n / (float) kRate;

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
                    phm += inc * (double) P.p1; if (phm >= 1.0) phm -= 1.0;
                    const float ei = env (te, juce::jmax (0.02f, P.p3));
                    const double mod = (double) (P.p2 * indice * ei)
                                     * std::sin (juce::MathConstants<double>::twoPi * phm);
                    ph += inc; if (ph >= 1.0) ph -= 1.0;
                    v = (float) std::sin (juce::MathConstants<double>::twoPi * ph + mod);
                    f1.set (juce::jlimit (200.0, nyq, 3000.0 * (double) brillo), 1.2f);
                    v += P.p4 * f1.bpf (rnd()) * env (te, 0.006f) * 2.4f * fuerza;
                    break;
                }

                case fOrgano:
                {
                    //  Aditivo de ocho barras. p1 inclinacion (brillo), p2
                    //  balance impares/pares, p3 percusion del 3er armonico,
                    //  p4 leslie.
                    lfo += 5.6 / kRate; if (lfo >= 1.0) lfo -= 1.0;
                    const double vib = 1.0 + (double) P.p4 * 0.004
                                             * std::sin (juce::MathConstants<double>::twoPi * lfo);
                    static const int mult[8] = { 1, 2, 3, 4, 6, 8, 12, 16 };
                    float suma = 0.0f;
                    for (int k = 0; k < 8; ++k)
                    {
                        const double fk = hz * (double) mult[k] * vib;
                        if (fk >= nyq) continue;
                        arm[k] += fk / kRate; if (arm[k] >= 1.0) arm[k] -= 1.0;
                        const float par = (mult[k] % 2 == 0) ? (1.0f - P.p2) : P.p2;
                        //  Y la capa fuerte tira de las barras de arriba, que
                        //  es lo que hace un organista al empujar: con la
                        //  inclinacion fija las dos capas median x1.40 de
                        //  centroide solo por el filtro, y el organo es aditivo.
                        const float incl = -1.0f + (P.p1 * 0.9f) * (0.45f + 0.55f * capaMix);
                        const float a = std::pow ((float) (k + 1), incl) * (0.55f + par);
                        float g = a;
                        if (k == 2) g *= 1.0f + P.p3 * 6.0f * env (te, 0.09f) * fuerza;
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
                    lfo += (double) P.p3 / kRate; if (lfo >= 1.0) lfo -= 1.0;
                    const double vib = std::pow (2.0, (double) P.p2 * 0.01
                                        * std::sin (juce::MathConstants<double>::twoPi * lfo));
                    float suma = 0.0f;
                    for (int k = 0; k < 7; ++k)
                    {
                        const double det = std::pow (2.0, (double) P.p1 * (double) kDesigual[k] / 1200.0);
                        const double ik = inc * det * vib;
                        arm[k] += ik; if (arm[k] >= 1.0) arm[k] -= 1.0;
                        suma += sawBl (arm[k], ik);
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
                    lfo += (double) P.p2 * 0.7 / kRate; if (lfo >= 1.0) lfo -= 1.0;
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
                        const double det = std::pow (2.0, (double) P.p4 * (double) kDesigual[k] / 1200.0);
                        const double ik = inc * det;
                        arm[k] += ik; if (arm[k] >= 1.0) arm[k] -= 1.0;
                        suma += pulsoBl (arm[k], ik, ancho);
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
                    const float osc = (1.0f - P.p1) * sawBl (ph, inc) + P.p1 * sqrBl (ph, inc)
                                    + P.p4 * sawBl (ph2, inc * 0.5) * 0.6f;
                    const float ef = env (te, juce::jmax (0.01f, P.p2));
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
                    if (hz * (double) P.p3 < nyq)
                        v += 0.95f * env (te, P.dec * 0.80f)
                             * (float) std::sin (juce::MathConstants<double>::twoPi * ph2);
                    if (hz * (double) P.p4 < nyq)
                        v += 0.70f * env (te, P.dec * 0.55f)
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
                        const double det = std::pow (2.0, (double) P.p3 * (double) kDesigual[k] / 1200.0);
                        const double ik = inc * det;
                        arm[k] += ik; if (arm[k] >= 1.0) arm[k] -= 1.0;
                        suma += pulsoBl (arm[k], ik, ancho);
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
                    lfo += 4.2 / kRate; if (lfo >= 1.0) lfo -= 1.0;
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
                    const float osc = pulsoBl (ph, inc, ancho)
                                    + 0.42f * pulsoBl (ph3, i3, ancho * 0.7)
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
                    lfo += 5.1 / kRate; if (lfo >= 1.0) lfo -= 1.0;
                    const double vib = std::pow (2.0, (double) P.p3 * 0.008
                                        * std::sin (juce::MathConstants<double>::twoPi * lfo));
                    float suma = 0.0f;
                    for (int k = 0; k < 3; ++k)
                    {
                        const double det = std::pow (2.0, (double) P.p4 * (double) kDesigual[k] / 1200.0);
                        const double ik = inc * det * vib;
                        arm[k] += ik; if (arm[k] >= 1.0) arm[k] -= 1.0;
                        suma += sawBl (arm[k], ik);
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
                    const float filtrado = 0.5f * (x + ksPrev);
                    ksPrev = x;
                    const float perd = 0.998f - P.p1 * 0.05f;
                    cuerda[(size_t) ksPos] = filtrado * perd;
                    ksPos = (ksPos + 1) % ksLen;
                    f1.set (juce::jlimit (90.0, nyq, 220.0 + 900.0 * (double) P.p3), 2.4f);
                    v = x + P.p3 * f1.bpf (x) * 0.8f;
                    v += P.p4 * rnd() * env (te, 0.004f) * 0.7f * fuerza;
                    f2.set (juce::jlimit (400.0, nyq, hz * 12.0 * (double) brillo), 0.6f);
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
                    v = (float) std::sin (juce::MathConstants<double>::twoPi * ph);
                    //  Los dos parciales de arriba los saca la BAQUETA DURA:
                    //  con ellos fijos las dos capas median centroide x1.00, o
                    //  sea que el toque solo cambiaba el volumen.
                    const float par = P.p4 * (0.25f + 1.30f * capaMix);
                    if (hz * (double) P.p1 < nyq)
                        v += par * env (te, P.dec * 0.30f)
                             * (float) std::sin (juce::MathConstants<double>::twoPi * ph2);
                    if (hz * (double) P.p2 < nyq)
                        v += par * 0.45f * env (te, P.dec * 0.14f)
                             * (float) std::sin (juce::MathConstants<double>::twoPi * ph3);
                    f1.set (juce::jlimit (400.0, nyq, 2800.0 * (double) brillo), 1.4f);
                    v += P.p3 * f1.bpf (rnd()) * env (te, 0.005f) * 3.0f * (0.2f + 1.1f * capaMix);
                    break;
                }

                case fClav:
                {
                    //  Pulso MUY estrecho por un paso banda: el clavinet es una
                    //  cuerda golpeada y captada, o sea casi un impulso con una
                    //  resonancia encima. p1 ancho, p2 centro, p3 Q, p4 muerte.
                    ph += inc; if (ph >= 1.0) ph -= 1.0;
                    const float osc = pulsoBl (ph, inc, juce::jlimit (0.02, 0.45, (double) P.p1));
                    f1.set (juce::jlimit (150.0, nyq, hz * (double) P.p2 * (double) brillo), P.p3);
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
                    lfo += 5.4 / kRate; if (lfo >= 1.0) lfo -= 1.0;
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
                    v = (float) std::sin (juce::MathConstants<double>::twoPi * ph)
                      + (P.p4 + 0.55f * capaMix)
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
                        arm[k] += fk / kRate; if (arm[k] >= 1.0) arm[k] -= 1.0;
                        const float tau = P.dec / (1.0f + P.p1 * (float) k);
                        //  Y la inclinacion del reparto tambien: pulsar fuerte
                        //  no solo saca mas parciales, los saca menos apagados.
                        //  Con 1/k fijo, doce armonicos contra siete median x1.10
                        //  - los de arriba pesan demasiado poco para notarse.
                        const float amp = std::pow ((float) (k + 1), -1.0f + 0.45f * capaMix);
                        suma += amp * env (te, tau)
                                * (float) std::sin (juce::MathConstants<double>::twoPi * arm[k]);
                    }
                    f1.set (juce::jlimit (400.0, nyq, 3000.0 * (double) brillo), 1.0f);
                    v = limita (1.1f * suma + P.p4 * f1.bpf (rnd()) * env (te, 0.004f) * 1.8f * fuerza);
                    break;
                }
            }

            //  La forma ya trae su propia caida cuando no sostiene (arm/env
            //  por parcial); `amp` es el ataque y el sobre general.
            d[n] = v * amp * fuerza;
        }

        juce::ignoreUnused (ph4);
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
    static int largoBucle (double hz, double segundos)
    {
        const double P = kRate / juce::jmax (1.0, hz);        // muestras por ciclo
        const double objetivo = kRate * segundos;

        int mejor = (int) std::lround (objetivo);
        double coste = 1.0e30;
        const int nMin = juce::jmax (1, (int) std::floor (objetivo * 0.55 / P));
        const int nMax = juce::jmax (nMin + 1, (int) std::ceil (objetivo * 1.45 / P));

        for (int n = nMin; n <= nMax; ++n)
        {
            const double L = (double) n * P;
            //  La fase que sobra pesa mil veces mas que la duracion: lo que no
            //  se puede negociar es la fase, y el largo si.
            const double c = std::abs (L - (double) std::lround (L)) * 1000.0
                           + std::abs (L - objetivo) / kRate;
            if (c < coste) { coste = c; mejor = (int) std::lround (L); }
        }
        return juce::jmax (256, mejor);
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
    SampleBuffer::Ptr sintetiza (int familia, int preset)
    {
        const int fi = juce::jlimit (0, kFamilias - 1, familia);
        const int pi = juce::jlimit (0, kPresets  - 1, preset);
        return sintetiza (fi, pi, kTabla[fi].p[pi]);
    }

    SampleBuffer::Ptr sintetiza (int familia, int preset, const Preset& receta)
    {
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
        const int cruce = F.sostiene ? (int) (kRate * 0.045) : 0;

        //  EL LARGO ES DE CADA RAIZ, no de todas. El bucle mide un numero
        //  entero de ciclos y un ciclo dura lo que dura, asi que la zona de
        //  DO1 y la de DO5 no pueden medir lo mismo. Ver largoBucle.
        int cuerpoDe[kRaices] {}, zonaDe[kRaices] {};
        int total = 0;
        for (int r = 0; r < kRaices; ++r)
        {
            const double hz = kHzRaiz * std::pow (2.0, (double) kRaiz[r] / 12.0);
            cuerpoDe[r] = F.sostiene
                ? largoBucle (hz, 0.42)
                : (int) (kRate * juce::jlimit (0.25f, 1.80f, P.atk + P.dec * 2.2f + P.rel));
            zonaDe[r] = pre + cuerpoDe[r];
            total += zonaDe[r] * kCapas;
        }

        auto sb = new SampleBuffer();
        sb->sourceSampleRate = kRate;
        //  DE DONDE SALIO, para que el fichero de proyecto pueda guardar la
        //  receta en vez del audio. Ver MainComponent::captureState.
        sb->familia = fi;
        sb->preset  = pi;
        sb->buffer.setSize (1, total);
        sb->buffer.clear();
        float* dst = sb->buffer.getWritePointer (0);

        int maxRinde = 0;
        for (int r = 0; r < kRaices; ++r) maxRinde = juce::jmax (maxRinde, zonaDe[r] + cruce);
        std::vector<float> tmp ((size_t) maxRinde);

        int z = 0, off = 0;
        for (int r = 0; r < kRaices; ++r)
        {
            const double hz = kHzRaiz * std::pow (2.0, (double) kRaiz[r] / 12.0);
            const int zonaLen  = zonaDe[r];
            const int rindeLen = zonaLen + cruce;
            for (int c = 0; c < kCapas; ++c, ++z)
            {
                //  SEMILLA FIJA por familia, preset, raiz y capa: dos arranques
                //  tienen que dar el MISMO instrumento, o un proyecto guardado
                //  suena distinto al abrirlo. Es la misma razon por la que
                //  HUMANIZAR se escribe en vez de sortearse.
                const int semilla = ((fi * 97 + pi) * 13 + r) * 7 + c + 1;
                std::fill (tmp.begin(), tmp.end(), 0.0f);
                //  Se congela EN EL PUNTO DE BUCLE: de ahi en adelante el
                //  sonido tiene que ser estacionario o cada vuelta reinicia lo
                //  que se estuviera moviendo. Lo que no sostiene no se congela.
                rinde (tmp.data(), rindeLen, F, P, hz, c, semilla,
                       F.sostiene ? (float) pre / (float) kRate : -1.0f);

                //  EL FUNDIDO CRUZADO DEL BUCLE. Ver Sintes.h: en la costura
                //  las dos mitades son la misma muestra, asi que la union es
                //  continua por construccion y no por un fundido que la tape.
                if (cruce > 0)
                    for (int i = 0; i < cruce; ++i)
                    {
                        const float x = (float) i / (float) cruce;
                        tmp[(size_t) (pre + i)] = tmp[(size_t) (pre + i)] * x
                                                + tmp[(size_t) (zonaLen + i)] * (1.0f - x);
                    }

                std::memcpy (dst + off, tmp.data(), sizeof (float) * (size_t) zonaLen);

                auto& Z = sb->zonas[(size_t) z];
                Z.raiz     = kRaiz[r];
                Z.capa     = c;
                Z.ini      = off;
                Z.fin      = off + zonaLen;
                Z.bucleIni = F.sostiene ? (off + pre) : 0;
                Z.bucleFin = F.sostiene ? (off + zonaLen) : 0;
                off += zonaLen;
            }
        }
        sb->nZonas = kZonas;

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
            const auto& Zf = sb->zonas[(size_t) (r * kCapas + 1)];   // la capa fuerte manda
            const float g = Kits::gananciaSonoridad (dst + Zf.ini, Zf.fin - Zf.ini);
            for (int c = 0; c < kCapas; ++c)
            {
                const auto& Z = sb->zonas[(size_t) (r * kCapas + c)];
                Kits::aplicaGanancia (dst + Z.ini, Z.fin - Z.ini, g, false);
            }
        }

        return SampleBuffer::Ptr (sb);
    }
}
