# ZATI Sampler — la máquina, entera

Sampler y groovebox nativo para Android, escrito en C++ sobre JUCE 8.
Estudio: **ARTiFACTS**. Identificador de Android: `com.artifacts.zati`.

Este documento describe **qué es la app y cómo está hecha**, una vez y en orden.
Es la pieza que faltaba: los otros documentos del repositorio son evidencia
(`CLAUDE.md`), defectos (`AUDITORIA-2026-08.md`), producto (`ESTUDIO-2026.md`),
papeleo (`GOOGLE-PLAY.md`, `PRIVACY.md`, `THIRD-PARTY.md`) o proceso
(`.claude/skills/banco/SKILL.md`, `MODO_ABSOLUTO.md`). Ninguno contesta la
pregunta estructural.

Aquí no se repite `CLAUDE.md`. Allí vive **por qué** cada decisión se tomó, con
la medida que la motivó; aquí vive **qué** hace la máquina. Una cifra aparece
sólo cuando define el comportamiento — un suelo, un tope, un plazo, un tamaño —
y donde hace falta el razonamiento, se cita dónde está.

---

## Índice

1. [Qué es](#1-qué-es)
2. [Cómo se lee esta máquina](#2-cómo-se-lee-esta-máquina) — el glosario
3. [La cara](#3-la-cara)
4. [Las fichas](#4-las-fichas)
5. [Los gestos](#5-los-gestos)
6. [Lo que se puede hacer](#6-lo-que-se-puede-hacer)
7. [El motor](#7-el-motor)
8. [Los instrumentos](#8-los-instrumentos)
9. [La fábrica](#9-la-fábrica)
10. [Lo que se guarda](#10-lo-que-se-guarda)
11. [El aparato](#11-el-aparato)
12. [Cómo se compila](#12-cómo-se-compila)
13. [El banco](#13-el-banco)
14. [Lo que no cuadra](#14-lo-que-no-cuadra)
15. [Dónde está cada cosa](#15-dónde-está-cada-cosa)

---

## 1. Qué es

Una máquina de **64 pads en cuatro bancos de dieciséis**. La rejilla enseña un
banco y los otros cuarenta y ocho siguen sonando: cambiar de banco cambia lo que
se ve, no lo que suena.

Cada pad guarda una muestra —tuya, de fábrica, grabada por el micro o
remuestreada de la propia máquina— con su recorte, su afinación, su envolvente,
su filtro, su pan, su ancho estéreo y sus seis envíos. Un pad también puede
llevar un **instrumento**: dieciséis familias sintetizadas de dieciséis presets
cada una, multizona, que se tocan con el dedo como una tecla.

Encima de eso hay un secuenciador de ocho patrones con cadena y una canción de
cuatro carriles por sesenta y cuatro compases, seis efectos de envío con un
mando por pad, una mesa de dieciséis canales con máster, y una exportación que
saca el máster o las pistas por separado.

**Lo que la separa de las demás**, y es lo que hay que saber para entender por
qué está hecha así:

- **La latencia se mide desde dentro.** AJUSTES → AUDIO → MEDIR emite un clic,
  lo graba por el micrófono a través de la misma llamada de audio y dice los
  milisegundos de ida y vuelta. No es una estimación.
- **Cuatro idiomas** (español, inglés, chino, árabe) con la maqueta espejada en
  árabe, y **cuatro carcasas** medidas en contraste y ΔE, no elegidas a ojo.
- **Un banco de pruebas propio**: veintinueve programas de Python y tres de C++
  que miden la interfaz en siete pantallas por cuatro idiomas por treinta y tres
  fichas, el motor por etapas, los sonidos de fábrica por sonoridad, y un millón
  de sesiones distintas. La regla de la casa es que **nada se entrega sin
  medirlo**, y existe porque cada fallo serio lo encontró una medida.
- **Sin permiso de INTERNET.** La app no puede mandar nada a ningún sitio; eso
  es un argumento de privacidad y no una carencia.

---

## 2. Cómo se lee esta máquina

El vocabulario, porque todo lo que sigue lo usa.

| palabra | qué es |
|---|---|
| **pad** | una de las 64 casillas que suenan. Lleva una muestra o un instrumento |
| **banco** | un grupo de dieciséis pads: A, B, C, D. La rejilla enseña uno |
| **zati** | el color de un pad. Ocho, del sistema `Zati.h`; los lleva el pad, su onda y su bloque en la canción |
| **carcasa** | la piel de la máquina: PAPEL, GRAFITO, ACERO, LACA. Es una preferencia de la **persona**, no del proyecto. De fábrica abre en LACA |
| **la cara** | la pantalla que no se puede evitar: pads, transporte, efectos, cristal |
| **ficha** (`Sheet`) | una tarjeta que se levanta sobre la cara. Ocupa la ventana entera, echa un velo del 45 % y centra su tarjeta al 78 % del alto |
| **página** | una pestaña dentro de una ficha. La ficha no es la página: cosas que se refrescan por ficha y se dibujan por página han costado medidas |
| **tapa** | un botón. Se dibuja como una tecla de un aparato, con su bloque de profundidad debajo |
| **el cristal** | la pantalla de la cara: silueta del máster, medidores y tempo |
| **la tira del paso** | los mandos del paso tocado, debajo de la rejilla de pasos |
| **canal** | la franja del borde de una rejilla donde un toque significa otra cosa (silenciar un carril, oír una tecla) |
| **muelle** | la tarjeta de texto del tour, siempre en la mitad contraria a lo que señala |
| **capa** | el número de profundidad que lleva una ficha, para que el banco no compare un rótulo con lo que hay tapado debajo |

Y dos reglas de diseño que explican la mitad de las decisiones:

- **Una función, un dueño.** Si dos sitios mueven lo mismo, uno se va y deja una
  puerta. Una puerta —una tapa que lleva a otro sitio— no es un duplicado.
- **Lo que no puede encoger se aparta primero.** Los mandos tienen suelo y se
  reparten lo que queda; una fila de tapas no. Donde no cabe todo, se cae por
  orden y se dice cuál.

---

## 3. La cara

De arriba abajo. Girado, la cara se parte en dos columnas: los mandos a un lado
y los pads al otro.

**La cabecera.** La marca **ZATI SAMPLER** a la izquierda —o `ZATI` a secas
cuando la ventana no llega a 260 px— y a la derecha el nombre del proyecto
abierto, o `SIN GUARDAR`. Debajo, una tira de ocho segmentos de color: se
enciende el segmento de cada zati que algún pad cargado lleve puesto.

**El cristal.** La pieza más grande de la cara. Arriba el pico de salida en
decibelios y el pico retenido, que se queda en rojo tres segundos si has
recortado. En medio, la silueta del máster de los últimos 0.74 s —no es un
espectro: es la onda, decimada a 256 columnas de mínimo y máximo—. Abajo, los
medidores L/R y el tempo. **Arrastrar por el cristal cambia de patrón**: es la
pieza más grande, no tiene otro gesto encima y ya es donde estás mirando.

**La fila de módulos.** Seis pestañas que abren las fichas: `PAD`, `SEC`,
`SONG`, `MIX`, `XY`, `SET`.

**El transporte.** `LOAD`, `REC`, el interruptor de modo (`PATRÓN` / `CANCIÓN`)
y `PLAY`. LOAD arma el siguiente toque en un pad para abrir la biblioteca ahí;
mantenerlo la abre directamente en el pad elegido. **Mantener PLAY es el
pánico**: para el transporte y corta todo lo que esté sonando. El interruptor de
modo dice el ESTADO —lo que va a tocar PLAY— y no un verbo, y las tres tapas que
lo llevan (cara, secuenciador y canción) dicen siempre lo mismo, igual que las
tres de PLAY.

**Los tres mandos CTRL.** Una ventana a los tres parámetros del efecto que tenga
el foco. Encima de cada uno, el nombre del parámetro; debajo, su lectura con
unidades. No guardan nada: escriben directamente sobre el parámetro y lo leen de
vuelta de él.

**Los seis efectos.** `FLT`, `HPF`, `DRV`, `DLY`, `BIT`, `REV`. Tocar uno lo
enciende **y** le da los tres mandos; mantenerlo le da los mandos **sin**
encenderlo, que es como se prepara un efecto antes de abrirlo.

**Los cuatro bancos.** A, B, C y D, repartidos a los dos lados de la palabra
PADS grabada en el chasis.

**La rejilla de pads.** Cuatro por cuatro, cuadrada. Cada tapa lleva su número,
el nombre de la muestra, una onda en miniatura **de su propio recorte** —o el
dibujo de la familia si el pad lleva un instrumento— y su color.

**El renglón de estado**, con `DESHACER` y `REHACER` a la derecha, que sólo
aparecen cuando hay algo que deshacer.

Los suelos que gobiernan estas filas viven en `Metrics` (`ZatiLookAndFeel.h`):
el dedo mínimo es **40 px** (`hit`), una tapa cómoda **44** (`btn`), una pestaña
**44** (`tab`), y mantener son **420 ms** (`holdMs`). Ninguna constante de
maquetado se escribe fuera de ahí.

---

## 4. Las fichas

Catorce fichas y un selector. Todas se cierran tocando fuera de la tarjeta —
menos el tour, que no puede cerrarse por un roce— y **el toque que cae sobre un
pad que asoma por debajo toca ese pad** y deja la ficha abierta: la tarjeta se
centra al 78 % precisamente para que la máquina se siga viendo.

Sólo dos fichas se desplazan (AJUSTES y RACK) más las de instrumentos. Las de
**lienzo** —la rejilla de pasos, el piano y la canción— no se desplazan nunca:
se pintan con el dedo arrastrado, y un arrastre vertical que a veces escribe una
nota y a veces mueve la página es un gesto que no se puede aprender.

### EL PAD

Todo lo que es de un pad. Su cabecera lleva `OIR` —escucharlo desde arriba— y
`PAD`, que abre el selector de dieciséis. Tres páginas:

- **SONIDO** — nueve mandos: afinación en semitonos, afinado fino en centésimas,
  ganancia en decibelios (−60 a +12), pan, ataque, caída, corte del filtro,
  resonancia y **ancho estéreo** (que a cero es mono de verdad). Debajo: el grupo
  de **choke** (off, 1–8), `CINTA`/`TONO` y `NORMALIZAR`.
- **RECORTE** — inicio, fin y los dos fundidos, en segundos; `REV`, `LOOP` y
  `QUITAR RUIDO`; y la **onda**, con sus asas y tres tapas de zoom.
- **EL PAD** — `ENVIOS` (puerta al RACK), `PIANO` (puerta a la página del
  piano), `16 NIVELES` y, si el pad lleva un instrumento, `PRESETS`. Más
  `AUTOCUT` y `BOMBEO`, la fila de fuente (`AUTO CHOP`, `GRABAR MIC`,
  `REMUESTREAR`) y los ocho colores del pad.

### SEC — el secuenciador

Su cabecera lleva `PAD` —el selector— y la ventana de pistas (`1-16`, `1-8`,
`9-16`). Tres páginas, que son **un trabajo con dos vistas más el patrón
entero**:

- **PASOS** — la rejilla de dieciséis pistas por dieciséis columnas, los cuatro
  bancos, los cuatro compases, y el transporte con `TAP`, `VACIAR` y `SEGUIR`.
  Y **la tira del paso**, que sólo existe cuando hay un paso tocado: NOTA,
  GOLPE, REPETIR y CORTE, más los cuatro bloqueos giratorios (ATAQUE, CAÍDA,
  INICIO, PAN) cuando cabe una tercera fila. Donde no cabe, esos mandos se caen
  a la página PATRÓN — cada uno en **un** sitio en cada pantalla, nunca en dos.
- **PIANO** — el mismo patrón visto por tono: trece filas, una octava y su raíz,
  con `OCTAVA −/+` embaldosando el rango entero que el motor admite. Una nota es
  una **barra** y su largo se estira arrastrando por la misma fila; cambiar de
  fila no escribe nada, que si no bajar el dedo dejaría una nota en cada fila.
  Tres herramientas exclusivas —`LÁPIZ`, `GOMA`, `TIJERAS`— y sin ninguna, un
  toque alterna. `PAD −/+` salta a los pads que tienen sonido.
- **PATRÓN** — lo que le pasa al patrón entero: la cadena de ocho, desplazar,
  doblar, `HUMANIZAR`, copiar y pegar (el patrón entero o la fila de un pad),
  el patrón y su largo, el swing, `EUCLIDES` y la rejilla (siete posiciones,
  de 1/8 a 1/64 con sus tresillos).

### CANCIÓN

Cuatro carriles por sesenta y cuatro compases. Arriba la paleta P1…P8 y las
brochas (`SONIDO` pinta un golpe suelto del pad elegido, `VACIAR` borra), luego
nueve herramientas de arreglo —atrás, adelante, acortar, alargar, insertar,
quitar, copiar, pegar y el tramo en bucle—, el transporte y el largo, las
páginas, y la línea de tiempo. **Un bloque dura lo que ocupa** y no lo que mide
su patrón: si ocupa menos, el patrón se corta ahí; si ocupa más, da la vuelta
dentro del bloque. El canal de la izquierda silencia el carril y sólo responde
al toque.

### MEZCLA

Dieciséis canales del banco que elijas: color, nombre, fader en decibelios, pan,
`M` y `S`. Debajo y **fuera del desplazamiento**, el **máster** —que se recuerda
con las preferencias de la persona y no con el proyecto— más `SIN SOLO` y la
puerta al `RACK`.

### AJUSTES

Cinco páginas, y se desplaza:

- **AUDIO** — la ruta, el reloj, el búfer, la salida con su veredicto, si hay
  MMAP y la latencia medida. Debajo, las tres pruebas (`CUADRAR`, `MEDIR`,
  `TEST`) y los chips de búfer y de reloj.
- **MIDI** — a dónde se mandan las notas y de dónde se reciben. El pad 1 es la
  nota 36 y de ahí hacia arriba.
- **GESTOS** — los seis gestos escritos, y las puertas al `MANUAL` y al `TOUR`.
- **PROYECTOS** — el nombre, la carpeta, `EXPORTAR`, `GUARDAR KIT`, y guardar,
  abrir, nuevo y borrar, con la lista de proyectos.
- **ASPECTO** — los cuatro idiomas, cada uno escrito en su propia escritura, y
  las cuatro carcasas.

Las acciones que destruyen algo piden **dos toques**: la tapa se rearma con la
pregunta (`BORRA TODO?`, `BORRAR X?`, `SOBRESCRIBIR X?`) y se desarma sola a los
tres segundos.

### RACK

Un efecto contra los sesenta y cuatro pads. El selector de pad es una rejilla de
cuatro por cuatro por banco —dieciséis en fila no caben: en 280 px les tocan 26—
y debajo los seis envíos del pad elegido, con el envío de un efecto apagado
atenuado: *lo que pongas ahora es lo que usará cuando lo enciendas*.

### XY

No es una ficha: es un panel opaco en la mitad de arriba de la cara, **para que
los dieciséis pads sigan tocándose debajo**, que es la razón de ser del gesto.
Los seis efectos, el cuadro XY que barre los dos primeros parámetros, y `FIJO`:
momentáneo —entra al tocar y sale al soltar— o enganchado.

### AUTO CHOP

Trocear un pad. `IGUALES` parte por aritmética; `GOLPES` los busca
(`Source/Onsets.h`). Dos, cuatro, ocho o dieciséis trozos, respetando o no los
pads que ya tienen sonido. La vista previa **es la lista de cortes que se va a
aplicar**: se arrastra una marca para moverla, se toca en un hueco para añadir
una y se arrastra fuera para quitarla. Los N trozos comparten **un solo buffer**
con N recortes distintos: eso es lo que los hace un troceado y no N sonidos.

### INSTRUMENTOS

El catálogo. Los packs se pasan con `PACK −/+`. Para los instrumentos
sintetizados son **dos rejillas**, porque son dos preguntas: la de arriba son
los dieciséis pads del banco y dice **dónde**; la de abajo son los dieciséis
instrumentos, con su nombre y su dibujo, y dice **cuál**. Los packs de disco y
la fábrica van a un banco entero y se quedan en lista, que es donde un nombre se
lee. Un pack de pago sin licencia sale con candado y lo dice al tocarlo.

### La ficha del instrumento

Con forma de aparato y no de menú: el dibujo grande de la familia, sus dos
nombres, el preset leído **sobre cristal** entre dos flechas, las cinco raíces
con la que va a sonar encendida, y **un teclado de una octava que se toca**.
Elegir un preset no suena: quien suena es el teclado, que está justo encima y
además deja elegir la nota.

### El navegador

Un `FileBrowserComponent` enraizado en `ZATI/Samples`. En modo pad, cinco
acciones: `CARGAR`, `INSTRUMENTOS`, `CARGAR KIT`, `MIS KITS` y `SISTEMA`.
Señalar un fichero **lo carga y lo dispara** en el pad de destino, así que se
elige de oído; la cruz devuelve el pad a como estaba. En modo carpeta —para
elegir dónde cae la exportación— la fila de acciones es **una** tapa y no las
cinco de cargar apagadas.

### EXPORTAR

Dice la fuente, la duración, cuántas pistas, el destino y el progreso.
`CAMBIAR` elige la carpeta, `WAV`/`OGG` el formato, y `MASTER` o `PISTAS` lo que
sale. Mientras el rebote corre, la ficha no se cierra.

### El manual y el tour

El **manual** son renglones de consulta, no de lectura: se mira con el teléfono
en la mano y en mitad de algo, así que cada línea se vale sola.

El **tour** son quince pasos y cada uno señala un control **de verdad**: abre la
ficha que explica, agujerea el velo alrededor del control para que se vea con su
color real, le pone un anillo y un número, y manda el texto a la mitad contraria
de la pantalla. Se enseña una vez, y `AJUSTES → GESTOS → TOUR` lo vuelve a abrir
cuando quieras.

### El selector de pads

Una rejilla de cuatro por cuatro más los cuatro bancos, encima de la ficha que
lo abrió. Cuesta cero de alto permanente, se cierra al elegir, y las dos fichas
que editan «el pad que tengas elegido» —EL PAD y SEC— lo abren desde el mismo
rincón.

---

## 5. Los gestos

| gesto | dónde | qué hace |
|---|---|---|
| **tocar un pad** | `PadButton::mouseDown` | Suena **al apoyar el dedo**, no al levantarlo. La fuerza sale de por dónde lo golpeas —arriba 1.0, abajo 0.35— o de la presión real donde el aparato la dé |
| **mantener un pad** (420 ms) | `PadButton::onHold` | Abre sus ajustes. Salta al llegar al umbral, con el dedo todavía puesto |
| **el dedo como tecla** | modo nota | En un pad de instrumento el dedo es una tecla: la nota empieza al apretar y se suelta al levantar, aunque el dedo se haya salido del pad. Varios pads son varias notas. Ahí no hay mantener-para-editar: una nota de más de 420 ms abriría la ficha a media frase |
| **arrastrar por la rejilla de pasos** | `StepGrid::hit` | Pinta pasos. Una celda no se reescribe dos veces seguidas |
| **arrastrar por el piano** | `PianoRoll::gesto` | Por la MISMA fila estira la nota; cambiando de fila no escribe. El canal de la izquierda es el teclado: suena y nunca escribe |
| **arrastrar por la canción** | `Playlist::toca` | Pinta bloques. El canal de la izquierda silencia el carril y sólo responde al toque |
| **arrastrar las asas de la onda** | `WaveformDisplay` | Recorta. El agarre son 24 px de pantalla, escalados con el zoom |
| **pellizcar la onda** | `WaveformDisplay` | Amplía hasta ×128, anclado entre los dos dedos. Un pellizco nunca dispara |
| **arrastrar la onda ampliada** | `WaveformDisplay` | Mueve la vista; al soltar no suena |
| **tocar la onda** | `WaveformDisplay::mouseUp` | Toca **el pad dueño del trozo que hay bajo el dedo**, desde ahí |
| **arrastrar el cristal** | `SpectrumDisplay` | Cambia de banco de patrones. Un tercio del ancho, o 60 px |
| **tocar fuera de una tarjeta** | `Sheet::mouseDown` | Cierra la ficha… salvo si el toque cae sobre un pad que asoma, y entonces toca ese pad y la ficha se queda |
| **mantener un efecto** | `HoldButton` | Le da los tres mandos sin encenderlo |
| **mantener CARGAR** | `HoldButton` | Abre la biblioteca en el pad elegido |
| **mantener PLAY** | `HoldButton` | Para y corta todo lo que suene |
| **doble toque en un mando** | JUCE | Lo devuelve a su valor por defecto |
| **dos toques en una tapa roja** | `armConfirm` | Confirma lo que destruye. Se desarma sola a los 3 s |

Los números de gesto viven en `Metrics` como los de maqueta: `holdMs = 420`, y
los tres canales de rejilla —`canalPasos = 30`, `canalPiano = 26`,
`canalCancion = 26`—, que son tres y no uno a propósito porque llevan contenido
distinto y cada uno está medido contra su fila.

---

## 6. Lo que se puede hacer

| | qué | dónde |
|---|---|---|
| 1 | **Grabar del micro** a un pad, con el límite de segundos que dé la gama del aparato | EL PAD → EL PAD → `GRABAR MIC` |
| 2 | **Remuestrear** lo que la máquina está tocando a un pad vacío | EL PAD → EL PAD → `REMUESTREAR` |
| 3 | **Trocear** un pad en 2, 4, 8 o 16, por aritmética o por golpes | EL PAD → EL PAD → `AUTO CHOP` |
| 4 | **Kits**: repartir una carpeta de sonidos por el banco de delante, y guardar el banco que has montado con la misma forma | navegador → `CARGAR KIT` · AJUSTES → PROYECTOS → `GUARDAR KIT` |
| 5 | **Instrumentos**: poner un sintetizador multizona en un pad y elegir entre sus dieciséis presets | `INSTRUMENTOS` · EL PAD → `PRESETS` |
| 6 | **16 niveles**: los dieciséis pads tocan el mismo sonido a dieciséis fuerzas. El pad se captura **al encender el modo**, no al disparar | EL PAD → EL PAD → `16 NIVELES` |
| 7 | **Humanizar**: escribe un empujón de hasta doce centésimas de paso y una fuerza distinta por golpe. Se **escribe**, no se sortea: así se puede deshacer, guardar y volver a oír igual | SEC → PATRÓN → `HUMANIZAR` |
| 8 | **Euclides**: reparte N golpes lo más uniformemente posible en la fila del pad | SEC → PATRÓN → `EUCLIDES` |
| 9 | **Bloqueos de paso**: corte del filtro, ataque, caída, inicio y pan, por paso. El extremo de abajo de cada mando dice `off`, así que no hace falta un interruptor al lado | la tira del paso, y SEC → PATRÓN |
| 10 | **Choke**: ocho grupos; un pad del grupo corta a los demás. Distinto del autocorte, que es un pad cortándose a sí mismo | EL PAD → SONIDO → `CHOKE` · EL PAD → EL PAD → `AUTOCUT` |
| 11 | **Cuadrar**: los pads tocados a mano caen en el paso | AJUSTES → AUDIO → `CUADRAR` |
| 12 | **Deshacer y rehacer**, dieciséis niveles. Cada paso guarda el árbol **y** los sesenta y cuatro buffers: sin eso, deshacer un troceado no podría devolver el audio | el renglón de estado |
| 13 | **Pánico** | mantener `PLAY` |
| 14 | **Tap tempo**, promediando hasta cuatro toques | SEC → PASOS → `TAP` |
| 15 | **MIDI** de entrada y de salida — los pads y el secuenciador mandan | AJUSTES → MIDI |
| 16 | **Exportar** el máster o las pistas, en WAV o en OGG, a la carpeta que elijas | AJUSTES → PROYECTOS → `EXPORTAR` |

---

## 7. El motor

`Source/AudioEngine.h` / `.cpp`, `Voice.h`, `Fdn.h`, `CommandFifo.h`.

### La cadena, en el orden en que corre

1. **Un solo renderizador a la vez.** Un cambio de ruta puede dejar dos hilos de
   llamada vivos un instante.
2. **La entrada del micro se captura antes de limpiar** la salida.
3. **Se adoptan** los buffers que la interfaz haya publicado, por intercambio de
   punteros.
4. **El reparto por bloque**: para cada pad, seis ganancias de envío suavizadas y
   una ganancia seca. Cuatro de los seis efectos (FLT, HPF, DRV, BIT) **restan**
   el seco en la misma medida —son inserciones—; DLY y REV suman encima. Un pad
   que no manda a ningún efecto y no tiene filtro se salta este bucle entero.
5. **Las dos colas de comandos** se vacían, cada una en su propio cubo.
6. **El transporte y las voces**, con el bloque partido en los bordes de paso.
   El filtro por pad se aplica aquí, en el camino separado.
7. **Los seis buses**, y un bus que nadie alimenta y que no está sonando **ni se
   limpia siquiera**.
8. **La seguridad del máster**: por encima de −0.5 dBFS se dobla con una
   tangente rápida, y lo no finito se pone a cero **fuera** de esa rama.
9. **El remuestreo** captura el máster aquí, después de todo.
10. **El bombeo** —el sidechain de un pad— agacha el máster entero, colas
    incluidas.
11. **El fader del máster**, con rampa de 12 ms.
12. **El osciloscopio y los medidores**.

### Las voces

Una reserva **común** de hasta 64, no dos voces atadas a cada pad. Cuántas y
cuántas por pad las decide la gama del aparato:

| gama | voces | por pad |
|---|---|---|
| básica | 16 | 4 |
| media | 32 | 6 |
| alta | 48 | 8 |
| muy alta | 64 | 12 |

Una voz es un acumulador de fase con interpolación de Hermite de cuatro puntos.
**CINTA** afina cambiando la duración; **TONO** la mantiene, con dos granos
solapados y una búsqueda por correlación. Lleva anti-aliasing que además se
inclina con la fuerza del golpe, fundidos de canto en coseno alzado, ancho
estéreo en medio/lado **antes** del pan, y pan de potencia constante que desliza
en exactamente un bloque.

### Los seis efectos

| | mando 1 | mando 2 | mando 3 |
|---|---|---|---|
| **FLT** | BARRIDO −1…+1, con el centro neutro | RESO | MIX |
| **HPF** | FREQ | RESO | MIX |
| **DRV** | DRIVE | TONE | MIX |
| **DLY** | TIME 20–1000 ms | FBK | MIX |
| **BIT** | BITS 1–16 | RATE | MIX |
| **REV** | SIZE | DAMP | MIX |

FLT es un barrido bidireccional con zona muerta: negativo cierra por arriba,
positivo abre por abajo, el centro se salta la etapa. HPF tiene **su propio**
filtro, así que FLT + HPF es un paso banda. El delay interpola con Lagrange de
tercer orden y no lineal, porque la realimentación compone la pérdida. La reverb
es una red de retardos realimentada con matriz de Hadamard, y pregunta por su
estado interno para saber si sigue sonando —una FDN no emite nada durante los
primeros ~1600 muestras.

### Lo que guarda un pad

Afinación, ganancia, inicio y fin del recorte, bucle, reverso, CINTA/TONO, grupo
de choke, autocorte, pan, ancho estéreo, ataque, caída, los dos fundidos de
canto, corte y resonancia del filtro, silencio, solo, y los seis envíos. Todos
se acotan **en la puerta** —el mismo sitio por donde entran el fichero, el mando,
el bloqueo de un paso y un kit— y no en quien llama.

### El secuenciador

Ocho patrones de 16 a 64 pasos, un bit por pad y por paso. Cada casilla
(patrón, paso, pad) puede además llevar: **nota** (±24 semitonos), **acorde**
(tres notas más, aparte, para que un patrón viejo vuelva exactamente igual),
**empujón** (centésimas de paso con signo), **largo** (en cuartos de paso; cero
es «suelta»), **bloqueo de corte**, **cuatro bloqueos empaquetados** (ataque,
caída, inicio, pan), **fuerza** y **repetición**.

Encima: la **cadena** de hasta dieciséis patrones, y la **canción** de cuatro
carriles por sesenta y cuatro compases, donde una casilla es o vacía, o el
comienzo de un patrón, o un golpe suelto de un pad, o la continuación del bloque
de al lado. El valor se acota **en la puerta**, porque sale de un fichero.

El swing va del recto al 75 %; la rejilla tiene siete posiciones; y cuantizar en
directo lleva un toque al paso más cercano.

### Las colas, y los invariantes

Dos colas de comandos —una de la interfaz y otra de MIDI— porque una
`AbstractFifo` es de **un solo productor por contrato** y el hilo por el que
llega el MIDI no es el de mensajes. El hilo de audio es el único consumidor y
vacía las dos, **en cubos separados**: con un cubo compartido, una ráfaga de la
interfaz se comía el presupuesto del MIDI y lo tiraba sin contarlo.

Y los invariantes que no se tocan, que están escritos en `CLAUDE.md` y se
repiten aquí porque gobiernan todo lo demás:

- **En el hilo de audio: cero reservas, cero cerrojos, cero E/S, cero `String`,
  cero `delete`.**
- **El audio nunca suelta la última referencia de un buffer**, porque soltarla es
  un `delete`. Lo hacen el hilo de mensajes y el de sesión.
- **Un aviso del sistema atenúa; no para nada ni suelta nada.**
- **Ningún camino puede dejar la app en silencio**, y hay un vigilante que mira
  las dos formas de que pase.

---

## 8. Los instrumentos

`Source/Sintes.h` / `.cpp` / `SintesTabla.inc`.

**Dieciséis familias de dieciséis presets**, 256 sonidos. Un pad con un
instrumento deja de ser un golpe y pasa a ser una nota: sostiene mientras el
dedo esté puesto, y el piano roll —que es de un pad y lleva el tono en el eje
vertical— escribe sobre eso.

| | familia | sostiene |
|---|---|---|
| 1 | BAJOS | sí |
| 2 | SUBS | sí |
| 3 | PIANO ELEC | no |
| 4 | ORGANOS | sí |
| 5 | CUERDAS | sí |
| 6 | COLCHONES | sí |
| 7 | PLUCKS | no |
| 8 | CAMPANAS | no |
| 9 | METALES | sí |
| 10 | LEADS | sí |
| 11 | COROS | sí |
| 12 | CUERDA PULS | no |
| 13 | MAZOS | no |
| 14 | CLAVES | no |
| 15 | VIENTOS | sí |
| 16 | ARPAS | no |

Cada familia es un **algoritmo distinto** y no un oscilador con los números
movidos — eso es exactamente lo que le pasó a los bancos A y B de la fábrica en
su día, y por eso se mide por pares y no de uno en uno.

**Multizona**, que es lo que separa «una muestra afinada» de un instrumento:
cinco raíces (−24, −12, 0, +12, +24) por dos capas de fuerza, **diez zonas en un
solo buffer**. Cinco raíces porque el motor acota en ±24 semitonos y con esas
cinco el rango queda embaldosado sin huecos ni sobras; al tocar se elige la más
cercana, así que se estira como mucho seis semitonos en vez de veinticuatro. La
capa fuerte no es la suave más alta: abre el filtro **y** escala el contenido.

El bucle vuelve a un punto **interior** —un instrumento tiene un ataque que suena
una vez y un régimen que se repite— con un fundido cruzado sobre el principio, y
las envolventes se **congelan** en el punto de bucle, para que de ahí en adelante
el sonido sea estacionario. Los LFO siguen con el tiempo de verdad.

**Las cifras que gobiernan el diseño**: diez zonas de tres segundos serían 5.7 MB
por pad, así que las zonas se acotan; sueltos por los sesenta y cuatro pads el
peor caso serían **147 MB** y confinados a un banco son **29.5 MB**. Por eso el
instrumento número *n* va siempre al pad *n* del **banco D**, que ya era el
melódico: que esté clavado es la función —el 07 está donde la mano lo busca— y
además acota la memoria. Llenar el banco entero cuesta **1238 ms**, o sea 77 ms
por pad, y por eso los instrumentos **no vienen puestos de fábrica**: el proyecto
se restaura sintetizando, así que venir puestos costaría ese segundo en cada
arranque y no sólo el primero.

Un proyecto guarda la **receta** —familia y preset— y no el audio. Escribir el
WAV devolvería un pad que suena parecido y que ha dejado de ser un instrumento:
una zona en vez de diez, sin capas y sin las otras cuatro octavas.

---

## 9. La fábrica

`Source/Kits.h`, `Source/kits/*.flac`, `Tools/fabrica.py`.

Cuatro bancos de dieciséis, uno por máquina:

| banco | nombre | qué es | los dieciséis |
|---|---|---|---|
| **A** | ACUSTICA | la batería de toda la vida | KICK, SNARE, HAT, OPEN, RIM, TOM LO, TOM MI, TOM HI, CLAP, RIDE, CRASH, SHAKE, CONGA, COWBEL, TAMB, SPLASH |
| **B** | MAQUINA | la caja de ritmos de los ochenta | BD 808, SD 808, CH 808, OH 808, RIM 808, LT 808, MT 808, HT 808, CLAP 9, CYM808, COW808, CLAVE, MARACA, LC 808, MC 808, HC 808 |
| **C** | TEXTURA | lo que no es un golpe | VINYL, HISS, RISER, FALL, IMPACT, CLICK, STATIC, WIND, THUMP, GLITCH, SCRAPE, BOOM, TICK, SWELL, CRACKL, DRONE |
| **D** | TONOS | lo melódico | BASS, SAW BS, SUB BS, STAB, CHORD, MINOR, PAD, BELL, PLUCK, KEY, ORGAN, BRASS, STRING, FIFTH, LEAD, ARP |

**Treinta y uno son grabaciones** y treinta y tres se sintetizan. Los dos bancos
de percusión vienen de máquinas de verdad; los otros dos siguen sintetizados
porque no hay de dónde sacarlos — un riser, un impacto o un colchón no los ha
grabado nadie en un banco de percusión, y ahí la síntesis no es un sucedáneo
sino la única forma. Las grabaciones van en **FLAC** —sin pérdidas y poco más de
la mitad de tamaño— y no en OGG, porque lo que se pierde en un códec con
pérdidas es exactamente el ataque, que es lo único que tiene un golpe de
batería. Treinta y un ficheros, **0.92 MB** dentro del binario.

Cada fila que tiene grabación **conserva sus parámetros de síntesis**: son lo que
suena si el recurso no está, y un hueco mudo es peor que un sonido parecido.
`COWBEL` sigue sintetizado a propósito, porque lo más parecido que hay en esos
bancos es una castañuela y un cencerro que en realidad es otra cosa es peor que
un cencerro sintético.

**Las dos mitades se igualan por el MISMO camino.** La normalización salió de
dentro del renderizado en cuanto hubo dos formas de llegar al final: dos caminos
que se igualan por su cuenta se separan, y el síntoma habría sido «la sonoridad
baila» sin poder decir por qué. Se iguala por **sonoridad y no por pico**, con la
curva K de la norma BS.1770 sobre la ventana de 400 ms más sonora — medir el
fichero entero castiga a lo largo y disperso. Resultado con las dos mitades
juntas: **1.7 dB** entre el más y el menos sonoro.

`Tools/fabrica.py` **no corre en la compilación**: los `.flac` están en el
repositorio y el script está para que se sepa de dónde sale cada uno. Pasa a
mono sumando los dos canales, a 48 kHz —la frecuencia del aparato, para que un
transitorio no pase por la interpolación—, recorta el silencio de delante y la
cola muda de detrás, y **no nivela nada**: eso es trabajo de la fábrica.

---

## 10. Lo que se guarda

### El árbol

```
ZATI/
  Samples/        tu audio — el navegador abre aquí
  Projects/       una carpeta por proyecto
    <nombre>/
      project.xml            la máquina entera
      samples/pad01.wav …    una copia de cada pad cargado, 24 bits
  Presets/        ajustes de pad guardados
  Kits/           los kits que guardas
  Instrumentos/   los packs que te bajas
  Recordings/     lo que graba el micro
  Exports/        los rebotes, si no eliges otra carpeta
  .sesion/        la sesión invisible
  idioma.txt · buffer.txt · zati-bitacora.txt
```

**Dónde está `ZATI/` se recuerda, no se vuelve a adivinar.** La ruta se guarda en
un fichero de la app, y una carpeta recordada que todavía **existe** gana aunque
el sondeo de hoy falle: una sonda sin memoria mudó la biblioteca entera y dejó
huérfana la sesión. Cada candidata se prueba **escribiendo** —crear un fichero,
escribir un byte, leerlo y borrarlo—, porque el almacenamiento por ámbitos
contesta «se puede escribir» a un `stat` y después dice que no.

### `project.xml`

Lleva la máquina entera: el swing, la rejilla (por **índice**, no por su valor en
negro sobre blanco), el tempo, el efecto con el foco y el patrón que se edita.
**La carcasa no se guarda**: es de la persona y no de la canción.

Dentro: la **canción** (los cuatro carriles, los silenciados y el tramo en
bucle), los **efectos** (los seis por tres parámetros, más el pad del bombeo y el
estado del XY), los **sesenta y cuatro pads** con todos sus parámetros —incluidos
la **fuente**, que es el pad más bajo que comparte el mismo buffer y es lo que
hace que dieciséis pads sean un *troceado* y no dieciséis sonidos sueltos, y la
**receta** del instrumento si lo lleva— y los **ocho bancos** de patrones.

Los pasos se escriben densos donde casi todos los llevan (encendido, nota, fuerza
y repetición) y **dispersos** donde los lleva un puñado de casillas: acorde,
empujón, bloqueos y largo van como tripletes «paso pad valor», y lo que no está
vale su defecto. Eso es además lo que hace que un proyecto de otra época —que ni
siquiera tiene la propiedad— suene exactamente igual que el día que se guardó.

Al abrir, **los sesenta y cuatro pads se ponen a su defecto antes de aplicar
nada**: vaciar la mitad de un proyecto es peor que no vaciar nada, porque lo que
queda parece tuyo.

### La sesión

Vive en `.sesion/`, **fuera de `Projects/`**, para que no aparezca en la lista. El
audio lo escribe un hilo propio un par de segundos después de que un pad cambie;
cuando Android manda la app a segundo plano sólo queda por escribir el XML
pequeño, que es lo que cabe en los segundos que da antes de declararla colgada.
Nadie tiene que acordarse de avisar: la sesión compara el estado vivo con lo
último escrito **puntero a puntero**.

### Los kits

Una carpeta en `ZATI/Kits/<nombre>/` con dieciséis WAV numerados `01 NOMBRE.wav`.
Con las **dos cifras siempre**, que sin ellas «10» se ordena antes que «2» y el
kit vuelve barajado; el número **es** el orden y se le quita al rótulo. Se
escribe **lo que suena** —el trozo recortado, no el fichero entero— porque un pad
de un break de cuatro minutos con el recorte en un golpe es ese golpe. Los
fundidos y el revés no van: son ajustes del pad y viven en el proyecto; un kit es
el material.

Sale con **la forma de un banco descargado** a propósito: el kit que guardas y el
pack que te bajas entran por la misma puerta.

### Los packs

```
ZATI/Instrumentos/<PACK>/
  pack.txt                nombre=…  pago=1
  01 <INSTRUMENTO>/
    01 <PRESET>.wav …
```

Una carpeta de carpetas y no un formato propio, así que un pack se monta a mano,
se mira desde el gestor de ficheros del teléfono, se copia a otro aparato y se
arregla cuando algo sale mal. Hasta dieciséis instrumentos por pack, porque el
menú es una rejilla de cuatro por cuatro y pasado ese número deja de ser un menú
y es un directorio — y un directorio ya lo tiene esta app: es el navegador.

**La licencia no vive dentro del pack.** Un fichero ahí dentro viaja con la
carpeta, así que copiar el pack copiaría el derecho a usarlo. Vive donde el
idioma y la carcasa. Un pack sin `pago=1` está abierto a propósito: quien se
monte uno a mano tiene que poder usarlo, y un candado que no se puede abrir es
peor que ningún candado.

### La exportación

Corre en su propio hilo sobre un **segundo motor** construido por copia, que
comparte los mismos buffers contados por referencia: no cuesta memoria de audio
de más, la máquina sigue sonando y el render no va atado al tiempo real.

Dos productos: **MASTER**, un fichero con lo que oyes, y **PISTAS**, el máster más
un fichero por pad cargado, con el mismo escalado de ganancia para que las pistas
sumen de vuelta al máster. En **WAV** de 24 bits o en **OGG**, que viene dentro de
JUCE y no tiene patentes — los mismos segundos pesan unas veinte veces menos.

Se escribe **por bloques**, con dos pasadas: la primera mide el pico y la segunda
escribe. La memoria del rebote es constante, así que la canción más larga que la
app admite sale entera con el montón limitado a 64 MB.

El destino se elige, y **se comprueba escribiendo en cada exportación**: en
Android la mitad de las carpetas que se pueden listar no aceptan que dejes nada
dentro, y enterarse al final de un rebote de cuarenta segundos es enterarse
tarde. De Android 10 en adelante, cuando **nadie ha elegido carpeta**, el fichero
se publica además en `Music/` por MediaStore — que no pide ningún permiso, deja
el fichero donde un músico lo busca, lo indexa el escáner y sobrevive a
desinstalar la app. Se **copia y no se mueve**: el rebote ya está escrito y
comprobado, así que un fallo ahí no puede costar el trabajo.

### Las preferencias

De la persona, no del proyecto, y en un sitio que sobrevive a que la biblioteca
se mueva: la carpeta recordada, el destino de exportación, la carcasa, las
licencias, el nombre de artista, el máster, las pistas, el estado del piano y la
marca de que el tour ya se vio. Más el idioma y el tamaño de búfer, que viven en
`ZATI/`. Las doce se escriben por la misma puerta —temporal, validador,
renombrado— porque una regla escrita doce veces son doce reglas y la última es la
que un día se escribe mal.

---

## 11. El aparato

### `Zati.jucer`

| | |
|---|---|
| paquete | `com.artifacts.zati` — **no se toca**: cambiarlo sería otra app, sin actualización posible desde ésta |
| nombre en la tienda | Zati Sampler |
| SDK mínimo / objetivo | 24 / 36 |
| orientación | libre. Estuvo clavada en vertical mientras una séptima parte del banco medía apaisado, y con `targetSdk 36` Android ignora la restricción en tableta y plegable — o sea que giraba justo donde nadie lo había decidido |
| tema de arranque | `@style/ZatiLaunch`, con el chasis de la carcasa de fábrica |
| permisos | micrófono, leer audio, escribir almacenamiento (acotado a SDK 28). **Sin INTERNET** |
| arquitectura | `arm64-v8a` |
| páginas de 16 KB | los dos parámetros de enlazado, comprobados en el CI |

### La ruta de audio

`Source/AudioPath.h`. Dos mitades.

Las **propiedades del sistema** dicen la política de MMAP y la de exclusividad,
cada una bajo tres claves distintas porque cada fabricante publica la misma
respuesta bajo la suya. Un aparato puede anunciar MMAP y traer la exclusividad
apagada, y entonces ninguna app de ese teléfono consigue nunca un punto final
exclusivo.

Y el **sondeo**, que es lo único que contesta de verdad: se abre un stream
pidiendo modo exclusivo y baja latencia y se lee lo que vuelve, en seis intentos
de menos a más específico. Fijar 48 kHz, estéreo y coma flotante —la versión
obvia del sondeo— puede **fabricar** el rechazo que venía a detectar, porque un
punto final exclusivo es una pieza de hardware con su frecuencia, sus canales y
su formato. Todo por carga dinámica, porque `libaaudio` sólo existe de la API 26
en adelante y la app admite la 24.

De ahí salen tres cosas que la app le pasa a JUCE antes de abrir su stream: para
qué se usa el audio, qué preajuste de entrada —un sampler graba fuentes, no voz—
y si conviene pedir enteros de 16 bits. Y en marcha, la app se cuenta los
under-runs sola y sube el búfer cuando aparecen, con la cuenta **volviendo a
cero tras un tramo limpio**: cuatro chasquidos repartidos en una hora no son lo
mismo que cuatro seguidos, y ese búfer es latencia.

### Las cuatro gamas

`Source/DeviceTier.cpp`. Se clasifica por núcleos, memoria y frecuencia — el
**modelo no se usa**: una tabla de teléfonos es una mentira que envejece mal y
que hace falta publicar una versión para arreglar.

| | básica | media | alta | muy alta |
|---|---|---|---|---|
| voces | 16 | 32 | 48 | 64 |
| voces por pad | 4 | 6 | 8 | 12 |
| tic de interfaz | 100 ms | 60 | 40 | 33 |
| puntos de osciloscopio | 256 | 512 | 1024 | 1024 |
| segundos de grabación | 20 | 45 | 60 | 120 |
| grabación estéreo | no | sí | sí | sí |
| presupuesto de muestras | 64 MB | 128 | 192 | 384 |
| ráfagas de búfer | 2 | 1 | 1 | 1 |
| onda en el pad | no | sí | sí | sí |

Por eso **todo plazo se cuenta en milisegundos y no en tics**: el tic va de 33 a
100 ms, así que «treinta tics» son un segundo en un móvil bueno y tres en uno de
gama baja — o sea que el teléfono que más tarda en arrancar era el que más se
quedaba mirando la portada.

### Los márgenes del sistema

Con `targetSdk 36` la app va de borde a borde: la ventana es la pantalla entera y
la barra de estado y la pastilla de gestos se pintan encima, así que la cara
tiene que restarse esos márgenes. Y **no se pueden pedir en el constructor**: la
respuesta no llega hasta que la vista está enganchada a una ventana.

Por eso **la cara no se enseña hasta que ha dejado de moverse**, y mientras tanto
se pinta el chasis con la marca, sobre el mismo fondo que el arranque del
sistema, para que la entrega se lea como una sola pantalla. Se espera a las dos
cosas —que los márgenes hayan **contestado** y que la sesión haya vuelto— con un
tope por encima: un aparato que no conteste nunca no puede dejar la portada
puesta. Y **contestar no es valer algo distinto de cero**: antes de Android 15 el
cero es correcto, porque el sistema ya coloca la ventana debajo de las barras.

### La caja negra

`Source/Bitacora.h`. Un fichero de texto en la carpeta ZATI, **no** el log del
sistema — ése se lo lleva el reinicio y hace falta un cable.

El último paso vive en un buffer **fijo** de caracteres y el descriptor se abre al
arrancar: un manejador de señal no puede reservar memoria ni tomar un cerrojo, y
si la app se está cayendo por corromper el montón, pedirle memoria al montón es
como se pierde la única línea que importaba. Se anota también la **salida
limpia**, porque sin esa línea no hay forma de distinguir «se cerró a la mitad»
de «se cerró bien»: la ausencia del final es lo que convierte el último paso en
un culpable. Y al abrir, si la vez anterior no acabó bien, la app lo dice en su
propia línea de estado.

---

## 12. Cómo se compila

### Escritorio — CMake

```
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target Zati
```

JUCE se descarga sola, **clavada en la etiqueta 8.0.4**, nunca en una rama. El
escritorio vale para el DSP y la fontanería, y para el banco entero; **no vale
para juzgar la latencia**, que la pone el teléfono.

Con `-DZATI_BENCH=ON` se compilan además los tres bancos de C++
(`StressTest`, `Soak`, `Cpu`). Los tres tienen que enlazar los datos binarios:
desde que la mitad de la fábrica son grabaciones, un objetivo sin ellos no
compila — y un banco que no compila no falla, es que no está.

### Android — Projucer → Gradle

La API de CMake de JUCE no admite Android, así que el único camino es compilar
Projucer, regenerar el proyecto de Gradle desde `Zati.jucer` y llamar a Gradle.
Lo hace `.github/workflows/build-apk.yml`, **a mano** (`workflow_dispatch`), y
por el camino:

- **numera la versión** con el número de corrida, de forma monótona;
- **escribe el tema de arranque leyendo el color de la tabla de carcasas**, no
  copiándolo, contando los campos del `struct` para que añadir un token falle en
  vez de coger el color de al lado — y **se comprueba a sí mismo**: si el color
  del tema no es el chasis de la carcasa con la que la app abre, el paso falla;
- pone el nombre de la tienda y **apaga la copia de seguridad automática**, que
  sin declarar viene encendida, se rinde a los 25 MB y deja tus muestras en un
  sitio del que se pueden sacar;
- instala **la clave de depuración del repositorio**, que está ahí a propósito:
  Android se niega a actualizar una app cuya firma cambió, así que una clave
  nueva por corrida hacía cada APK imposible de instalar encima de la anterior;
- **comprueba las páginas de 16 KB** sobre el binario **ya firmado**, y lo repite
  sobre el `.aab`, que es lo que Play acepta de verdad;
- **refirma con la clave de subida** si los cuatro secretos están puestos, y
  **falla si la huella coincide con la de depuración**;
- **publica la release primero** y sube la copia de comodidad después: una cuota
  de almacenamiento que no tiene nada que ver con la APK no puede decidir si la
  APK se publica;
- y **poda** lo que ya no se publica, tanto en la release como en los artefactos.

### El banco, en cada empujón

`.github/workflows/banco.yml` corre en cada push las pruebas que devuelven código
de salida y no dependen del reloj, **cada una en su propio paso** — encadenadas,
la primera que falla esconde a las demás y el informe dice una cosa donde hay
cinco. Quedan fuera a propósito `Tests/cpu.py`, que mide por reloj y con la
máquina ocupada no es una medida, y `Soak`, que es un millón de sesiones.

---

## 13. El banco

Veintinueve programas de Python y tres de C++. La regla de cabecera: **nada se
entrega sin medirlo**, y **una prueba que nunca se ha visto fallar no es una
prueba: es una línea que imprime OK**, así que cada comprobación nueva se valida
rompiendo el código a propósito y comprobando que sale FALLA con el número que se
esperaba. Y su hermana: **cuando una prueba falle, primero se duda de la
prueba** — ha pasado ocho veces.

### Las ocho reglas duras

`Tests/expo.py` monta la app **924 veces** —siete pantallas por cuatro idiomas
por treinta y tres fichas— y cada una tiene que dar cero en: solapes entre
hermanos, controles fuera de la ventana, celdas de rejilla por debajo de su
suelo, rótulos cortados o apretados, textos sin traducir, controles encendidos
que miden cero, rótulos pintados debajo de un control, y residuo al cambiar de
página. Los incumplimientos del dedo mínimo se cuentan y **no fallan**: son una
escalera medida y escrita, y hoy son 2282.

### Los tres del motor

| | qué mide |
|---|---|
| `StressTest` | el motor bajo carga: dieciséis pads a la vez, un pad redisparado cada bloque, la cola saturada, cuarenta cambios de ruta en mitad de una frase, ocho muestras hostiles, el delay contra lo que promete el mando, y todo el comportamiento del secuenciador |
| `Soak` | **mucha gente, que no es lo mismo que mucha carga**: cada sesión es una semilla —un aparato de los que hay en la calle, sonidos de formas distintas y una tirada de acciones— y se comprueba lo único que no puede pasar nunca: NaN, salida fuera de rango y voz colgada tras el pánico |
| `Cpu` | el coste por etapa, con la **mediana** de dos mil bloques y no la media, contra el presupuesto de 2.67 ms de un bloque de 128 a 48 kHz |

### Los veintinueve de Python

`analiza` lo que cada grabación es de verdad · `apk` una APK sin herramientas de
Android · `arr` las ocho herramientas de arreglo, dos de ellas por **identidad**
· `arranque` la portada y los márgenes · `clon` cuánto se parece cada receta a su
grabación · `cpu` lo que la app cuesta quieta **y sonando**, contando píxeles y
no llamadas · `desglose` qué controles tiene cada pantalla y qué está repetido ·
`dlc` el catálogo y el candado por sus dos mitades · `export` máster, pistas, OGG
y destino · `expo` la maqueta · `fuentes` que las dos listas de ficheros digan lo
mismo · `icono` el icono del lanzador · `iconos` que no haya dos dibujos iguales ·
`informe` y `maquetas`, que dibujan · `instr` los 256 instrumentos y sus 32 640
pares · `kit` guardar un kit, juzgado leyendo de vuelta del **disco** · `kits` los
64 de fábrica por **sonoridad y no por pico** · `lang` la tabla de idiomas ·
`niveles` los dieciséis niveles · `nuevo` con qué abre la máquina, por sus **dos**
caminos · `paneles` los paneles al píxel · `piano` el compás del piano por el
camino de verdad · `plano` y `planos`, la estructura y el dibujo de cada
pantalla · `session` que la sesión vuelva entera, por repetición · `skins` las
cuatro carcasas en contraste y ΔE · `store` las fotos de la ficha de Play ·
`tour` que cada paso señale algo.

### Las entradas

Cincuenta y una variables `ZATI_*` convierten en **entrada** lo que si no sería
«lo que hubiera»: la carcasa (`ZATI_SKIN`), los packs instalados (`ZATI_DLC`),
los márgenes del sistema y cuándo contestan (`ZATI_INSETS`, `ZATI_INSETS_TICK`),
la máquina **sonando** cuando no hay tarjeta de sonido (`ZATI_SONANDO`), el fondo
sin hornear (`ZATI_FONDO_VIVO`), o la fábrica sintetizando aunque haya grabación
(`ZATI_SIN_MUESTRA`). Más las de medida —`ZATI_AUDIT`, `ZATI_SIZE`, `ZATI_LANG`,
`ZATI_OPEN`, `ZATI_PAINT`, `ZATI_SPIN`, `ZATI_PAGES`, `ZATI_FUZZ`— y las que
generan los gráficos de la tienda y el icono. La lista completa está en la
cabecera de `Source/UiAudit.h`.

---

## 14. Lo que no cuadra

Cuatro contradicciones aparecieron al inventariar la máquina para escribir esto.
Se anotan con su ruta, porque un documento que las tapa es *una línea que imprime
OK*, y ninguna se arregla aquí: lo pedido era el documento.

**1 · `THIRD-PARTY.md` dice que la app sale vacía, y no es verdad.** Su texto
afirma que «no se distribuye ni un solo sample, loop, preset ni sonido de
fábrica». `Source/kits/` lleva **31 ficheros FLAC, 916 250 bytes**, empotrados en
el binario. Y su propia frase de cierre —«si algún día se meten packs de fábrica,
cada uno necesita su propia licencia por escrito antes de entrar aquí»— es la
condición que falta cumplir: **no hay fila de licencia para esas grabaciones en
ningún sitio del repositorio**.

**2 · `README.md` y `GOOGLE-PLAY.md` afirman que no hay marcas ajenas en
`Source/`.** `Source/Kits.h` lleva **trece nombres de pad visibles con `808`** —
`BD 808`, `SD 808`, `CH 808`, `OH 808`, `RIM 808`, `LT 808`, `MT 808`, `HT 808`,
`CYM808`, `COW808`, `LC 808`, `MC 808`, `HC 808` — y comentarios que nombran tres
máquinas reales. La regla 1 de `GOOGLE-PLAY.md` §1.5 prohíbe exactamente eso «ni
en código, ni en comentarios, ni en la ficha, ni en capturas». **Es el punto de
riesgo de marca más alto que queda**, y es anterior a este documento.

**3 · El manual promete ocho capítulos, define diez y dibuja nueve.**
`kManualChapterCount = 10` (`Source/MainComponentInterno.h:285`) contra
`kManualChapters = 9` (`Source/MainComponent.h:1809`), y el pintor recorre el
segundo (`Source/MainComponent_Paint.cpp:1066`). O sea que **«SI ALGO NO SUENA»
está traducido a cuatro idiomas, reserva su alto en el desplazamiento y no se
pinta nunca**: un hueco en blanco al final. El alto sí cuenta los diez. Y la
palabra «ocho» está escrita en siete sitios. Ninguna de las ocho reglas del banco
puede verlo, porque un capítulo que no se dibuja no es un componente.

**4 · Tres documentos dan tres listas de efectos.** `README.md` dice `FLT` y
`BIT`; `ESTUDIO-2026.md` dice `ISO` y `CRUSH`; `GOOGLE-PLAY.md` dice `CRSH` y
añade un `BEAT REPEAT` que no existe. La buena es la del código —la tabla
`fxDefs` de `Source/MainComponent.cpp:3216`—: **FLT, HPF, DRV, DLY, BIT, REV**,
que es la que da este documento.

Y dos huecos de medida menores: `ZATI_OPEN=lang` está implementado
(`Source/MainComponent_Audit.cpp:1118`) y no está en la lista `SHEETS` de
`Tests/expo.py`, así que no entra en el barrido de 924; y la tabla de
`.claude/skills/banco/SKILL.md` sigue diciendo «812 corridas» donde hoy son 924.

---

## 15. Dónde está cada cosa

### `Source/`

| fichero | qué lleva |
|---|---|
| `Main.cpp` | el arranque, y el despacho de todas las entradas `ZATI_*` |
| `MainComponent.h` | la declaración de la cara y de las catorce fichas |
| `MainComponent.cpp` | el constructor, el estado y el comportamiento |
| `MainComponent_Layout.cpp` | `resized()` y toda la geometría |
| `MainComponent_Paint.cpp` | los veintisiete pintores |
| `MainComponent_Audit.cpp` | los ganchos del banco |
| `MainComponentInterno.h` | lo que comparten las cuatro unidades: el manual, el tour, los ayudantes |
| `AudioEngine.h` / `.cpp` | el motor |
| `Voice.h` | una voz |
| `Fdn.h` | la reverb |
| `CommandFifo.h` | las colas hacia el hilo de audio |
| `Sintes.h` / `.cpp` / `SintesTabla.inc` | los 256 instrumentos |
| `Kits.h`, `kits/*.flac` | la fábrica |
| `Instrumentos.h` | el catálogo de packs y el candado |
| `ProjectStore.h` | el árbol, los nombres y la escritura segura |
| `SessionKeeper.h` / `.cpp` | la sesión |
| `Exporter.h` | el rebote |
| `MediaStore.h` / `.cpp`, `AppStorage.cpp` | el almacenamiento de Android |
| `AudioPath.h` | el sondeo de la ruta rápida |
| `DeviceTier.h` / `.cpp` | las cuatro gamas |
| `SystemInsets.h` / `.cpp` | los márgenes del sistema |
| `Bitacora.h` | la caja negra |
| `Lang.cpp` | los cuatro idiomas |
| `ZatiLookAndFeel.h` | `Metrics`, las cuatro carcasas y el dibujo de una tapa |
| `Zati.h` | los ocho colores |
| `Iconos.h`, `PadArt.h`, `StoreArt.h` | los ochenta y seis dibujos, la tapa de un pad y la marca |
| `UiAudit.h` | el volcado que lee el banco |
| `StepGrid.h`, `PianoRoll.h`, `Playlist.h`, `WaveformDisplay.h`, `SpectrumDisplay.h`, `XyPad.h`, `Teclado.h`, `ChopPreview.h`, `PadButton.h` | los componentes que se pintan enteros |
| `Onsets.h`, `Denoise.h`, `MidiIo.h`, `AudioFocus.cpp`, `SampleLoader.cpp` | troceo por golpes, quitar ruido, MIDI, el foco de audio y la carga |

### Qué documento contesta qué

| pregunta | dónde |
|---|---|
| ¿qué es la app y cómo está hecha? | **este fichero** |
| ¿por qué está hecha así, y qué midió cada decisión? | `CLAUDE.md` |
| ¿cómo se corre el banco y cómo se juzga? | `.claude/skills/banco/SKILL.md` |
| ¿qué defectos se encontraron en agosto? | `AUDITORIA-2026-08.md` |
| ¿dónde está el producto y qué **no** hay que hacer? | `ESTUDIO-2026.md` |
| ¿qué falta para publicar? | `GOOGLE-PLAY.md` |
| ¿qué lleva dentro que no es nuestro? | `THIRD-PARTY.md` |
| ¿qué se le dice a quien la instala? | `PRIVACY.md` |
| ¿cómo se compila y qué hace el proyecto? | `README.md` |
| ¿cómo se trabaja aquí? | `MODO_ABSOLUTO.md` |
