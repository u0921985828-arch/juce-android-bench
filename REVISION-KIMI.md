# ZATI — dossier para revisión externa

**Qué es:** sampler / groovebox nativo para Android, escrito en C++17 sobre JUCE 8,
con motor de audio propio y camino de baja latencia por Oboe/AAudio. No es un
prototipo: es una aplicación completa (pads, secuenciador, piano roll, mezclador,
32 canales, 23 efectos, 256 instrumentos, proyectos, exportación y publicación al
almacén de medios de Android).

**Repositorio público (del que sale la APK):** `u0921985828-arch/juce-android-bench`, rama `main`.
**Commit de referencia de este dossier:** `1d57ff1`.
**Estudio:** ARTiFACTS.

Este documento existe para que alguien que no ha visto el código pueda revisarlo
con criterio. No repite el manual de usuario (`ZATI.md`, 1177 líneas) ni la
historia de decisiones (`BITACORA.md`, ~790 KB). Explica la **forma** del
sistema, los **contratos** que lo sostienen, **dónde están los riesgos** y
**qué preguntas** nos gustaría que contestara la revisión.

---

## 1. Escala y forma

| | |
|---|---|
| Código de aplicación | ~68 400 líneas C++ en `Source/` (66 ficheros) |
| Banco de pruebas | 50 programas Python (~16 100 líneas) en `Tests/`, más 3 binarios C++ |
| Idiomas de interfaz | 4 — español, inglés, chino, árabe (interfaz espejada en árabe) |
| Pantallas medidas | 53 estados distintos de interfaz |
| Pads | 64 (4 bancos de 16) |
| Canales de mezcla | 32 |
| Tipos de efecto | 23, en 6 ranuras por canal |
| Instrumentos de síntesis | 256 (16 familias × 16 recetas) |
| Plataformas | Android (objetivo real) y escritorio Linux/macOS (desarrollo y banco) |

### Los ficheros que importan, por tamaño

```
16679  Source/MainComponent.cpp        la cara y las 21 fichas: toda la lógica de interfaz
 6161  Source/MainComponent_Layout.cpp  resized(): dónde va cada cosa, para cada pantalla
 6003  Source/MainComponent_Audit.cpp   instrumentación: la app se mide a sí misma (ver §6)
 4261  Source/AudioEngine.cpp           el motor
 3430  Source/MainComponent.h           estado de la interfaz, ~1000 miembros
 3043  Source/AudioEngine.h             API del motor + tablas de parámetros (263 métodos públicos)
 2884  Source/MainComponent_Paint.cpp   todo lo que se dibuja a mano
 2174  Source/Iconos.h                  ~100 sprites vectoriales
 2002  Source/Lang.cpp                  780 filas × 4 idiomas
 1738  Source/ZatiLookAndFeel.h         tokens de diseño, 4 carcasas
 1301  Source/UiAudit.h                 el volcado JSON que lee el banco
```

**Primera observación honesta para el revisor:** `MainComponent.cpp` tiene 16 679
líneas y `MainComponent.h` declara del orden de mil miembros. Es el punto más
discutible de la arquitectura y lo sabemos. Está partido en cuatro unidades de
traducción (`.cpp`, `_Layout.cpp`, `_Paint.cpp`, `_Audit.cpp`) por eje de
responsabilidad, no por tamaño, pero sigue siendo una clase-dios. **Queremos
opinión sobre si partirlo, y por dónde** — ver §9, pregunta 1.

---

## 2. El motor de audio

### Modelo de señal

```
64 pads ──> pool de voces (robo por antigüedad) ──> 32 canales ──> master ──> salida
                                                        │
                                         6 ranuras de efecto por canal
                                         (18 insertos + 5 envíos)
```

- **Voz:** acumulador de fase fraccionario + interpolación Hermite de 4 puntos.
  Dos modos de tono: CINTA (varispeed, cambia duración) y TONO (granos solapados,
  mantiene duración).
- **Pool de voces dimensionado por el aparato** (`DeviceTier.h`): 32 voces en gama
  media, con tope por pad (`voicesPerPad = 8`) para que un pad mantenido no deje
  secos a los demás. La clasificación sale de núcleos y RAM.
- **Un canal es un grupo.** Los efectos no son del pad ni de la máquina: son del
  canal. Cada pad elige el suyo. Un **inserto** es de un canal (la señal atraviesa
  la caja y sustituye lo seco); un **envío** es de todos (la señal sigue de largo
  y una rama baja a la caja). DLY, REV y los tres de modulación sólo existen como
  envío, porque el peine de un flanger *es* la suma de la copia con la seca.

### Contrato del hilo de audio — esto es lo que más nos importa que se revise

Es un contrato duro, escrito y vigilado:

1. **Cero reservas de memoria, cero cerrojos, cero E/S, cero `juce::String`, cero `delete`** en el camino de proceso.
2. **La cola de comandos es SPSC estricta**: un consumidor *y un productor*. Por
   eso la entrada MIDI tiene su propia cola (`midiCommands`) en vez de empujar a
   la de la interfaz. El hilo de audio drena las dos. Dos productores no la
   degradan: la **atascan para siempre**.
3. **Lo que sale hacia fuera tampoco sale del hilo de audio.** Mandar una nota MIDI
   reserva memoria y habla con el sistema: el audio escribe un POD en
   `MidiIo::NoteFifo` y sigue. Envía un hilo propio — deliberadamente *no* el
   temporizador de interfaz, que a 60 ms de latido metería 60 ms de retraso y otro
   tanto de *jitter* en la aplicación cuyo argumento entero es la latencia.
4. **El audio nunca llama a `decReferenceCount`.** Lo que un `SampleBuffer`
   contado por referencias no puede permitirse es que su último dueño sea el hilo
   de tiempo real, porque soltarlo es un `delete`. Los demás hilos sí pueden.

**Dónde mirar:** `AudioEngine.cpp` (proceso), `CommandFifo.h`, `MidiIo.h`,
`SampleBuffer.h`, `Voice.h`.

### Hilos vivos

| Hilo | Fichero | Qué hace |
|---|---|---|
| audio (callback) | `AudioEngine.cpp` | tiempo real, contrato de arriba |
| mensajes (JUCE) | `MainComponent*` | interfaz, temporizador de mantenimiento |
| `zati-session` | `SessionKeeper.cpp` | autoguardado continuo: pads cada 2 s, estado entero cada 20 |
| `zati-export` | `Exporter.h` | rebote offline |
| `zati-vivo` | `Exporter.h` | rebote en vivo (graba lo que suena mientras suena) |
| `zati-midi-out` | `MidiIo.h` | drena la FIFO de notas salientes |
| `denoisePool` (1) | `Denoise.h` | reducción de ruido fuera de línea |

---

## 3. Persistencia y recuperación

Hay **dos** capas y la distinción importa:

- **Sesión invisible** (`SessionKeeper`): la aplicación guarda sola, siempre. Pads
  cada 2 s, estado entero cada 20. Es lo que hace que matar el proceso desde
  Android no cueste trabajo. Escritura atómica: temporal → validación por
  `parseXML` → `moveFileTo` (rename(2), atómico). Las dos salidas de error no
  marcan la escritura como buena, a propósito.
- **Proyecto explícito** (`ProjectStore`): carpeta con `project.xml` (estado
  completo de la máquina) y los WAV de sus pads.

`ProjectStore::home()` **se recuerda, no se vuelve a adivinar**: una sonda sin
memoria mudó una vez la biblioteca entera y dejó huérfana la sesión.

Hay además publicación al **almacén de medios de Android** (`MediaStore.cpp`, JNI
directo): el rebote acaba en `Music/ZATI/` visible para cualquier gestor, y desde
esta última tanda se puede mandar con `ACTION_SEND` + `createChooser`.

**Dónde mirar:** `SessionKeeper.cpp:120-200` (la escritura atómica),
`ProjectStore.h:420-500`, `MediaStore.cpp`.

---

## 4. Interfaz

- **Una cara** (la máquina) más **21 fichas** que se abren encima. 53 estados
  distintos medibles.
- **Todos los números de maquetado viven en `Metrics` y `ZatiLookAndFeel`.** No
  hay constantes a mano. Escala: `xs=4, sm=8, md=12, lg=16, xl=24`; suelo del
  dedo `hit=40`.
- **Cuatro carcasas** (PAPEL, GRAFITO, ACERO, LACA). Todo token que una piel mueve
  es `inline`, nunca `const`, y quien lo nombre tiene que volver a leerlo — una
  copia congela la paleta del arranque. La tinta sobre una superficie se elige
  **midiendo** (`ZatiColours::textOn`), nunca con un ternario claro/oscuro.
- **Un cambio de carcasa repinta todas las tapas**: cada botón recuerda con qué
  token se pintó (propiedad `"role"`) y `applySkin` recorre el árbol entero.
- **Cuatro idiomas.** Todo texto pasa por `T()`. El árabe se escribe sin
  *tracking* (es escritura ligada) y los números latinos van dentro de
  `Lang::ltr()`. La interfaz se espeja completa.
- **Repintado colgado del VBLANK** (Choreographer en Android): 60/90/120 según el
  panel. `DeviceTier::relojMs` ya no es «cada cuánto se repinta» sino el latido
  del reloj de mantenimiento y el **suelo** al que se permite caer el dibujo.

---

## 5. Accesibilidad y seguridad de uso

Dos invariantes que valen la pena mirar porque son de los que fallan en silencio:

- **Ningún camino puede dejar la aplicación en silencio.** El temporizador vigila
  las dos formas: atenuada sin recuperar la ganancia, y sin dispositivo estando en
  primer plano. Un aviso del sistema **atenúa**; sólo `LOSS`/`LOSS_TRANSIENT`
  paran, y al recuperar el foco se restaura **el transporte**, no sólo el
  dispositivo.
- **MOVIMIENTO** apaga todo lo que se mueve solo sin parar lo que mueve el
  transporte. Para quien tiene sensibilidad vestibular, «no hay forma de apagarlo»
  es «no hay forma de usarlo».

---

## 6. El banco de pruebas — lo más particular del proyecto

Es lo que más conviene entender antes de revisar nada, porque explica por qué el
código tiene la forma que tiene.

### Cómo funciona

**La aplicación se mide a sí misma.** Con `ZATI_AUDIT=1` la aplicación se maqueta,
recorre su árbol de componentes, imprime una línea JSON por componente y por
rótulo pintado, y se va. Un script Python juzga el volcado.

```
ZATI_AUDIT=1  ZATI_SIZE=412x915  ZATI_LANG=es|en|zh|ar  ZATI_OPEN=<pantalla>
```

`Tests/expo.py` es el principal: **1484 corridas** (7 tamaños de pantalla × 4
idiomas × 53 estados). Tiene **19 reglas de veredicto que tienen que salir a
cero**:

```
TRUNC  SQUEEZE  OVERLAP  OFFSCREEN  CELDA  UNTRANSLATED  CERO  TAPADO
SPRITE  CORTADO  PISADO  FILA  CUADRADA  ASOMA  CABECERA  MARCO  CARA
CHIPS  ANATOMIA
```

Más contadores que se imprimen pero no se juzgan (TOUCH, APRETADA, TARJETA, FILO).

Ejemplos de lo que cazan: un rótulo que no cabe y JUCE encoge (`SQUEEZE`), dos
hermanos solapados (`OVERLAP`), una cadena idéntica en `es` y en `en` que por
tanto nunca pasó por `T()` (`UNTRANSLATED`), una celda de rejilla que quedó
cuadrada cuando no debía (`CUADRADA`), un pad que no asoma entero bajo una ficha
(`ASOMA`).

### Las otras 49 pruebas

Cubren motor (`StressTest`, `Soak`, `Cpu` en C++), repintados por píxel
(`cpu.py`), tasa de refresco (`fps.py`), sesión y recuperación (`session.py`),
exportación (`export.py`), piano roll (`piano.py`), los 23 efectos por familia,
los 256 instrumentos, las 4 carcasas, la APK firmada (`apk.py`), y reglas
derivadas del fuente:

- `deshacer.py` — toda función que ocupa un pad toma foto para deshacer, o declara por escrito por qué no.
- `proyecto.py` — toda puerta que sustituye el proyecto entero confirma antes.
- `profundidad.py` — histograma de cuántos toques hace falta para llegar a cada ficha.
- `maqueta.py` — ningún literal de aire vale lo que un token de `Metrics`.
- `marcas.py` — ni una marca ajena en nada que se publique.

### Las dos reglas que gobiernan el banco

Están en `CLAUDE.md` y son la parte metodológica que nos interesa que se juzgue:

> **Antes de creer una prueba, se rompe el código a propósito** y se comprueba que
> sale FALLA **con el número que se esperaba**. Una prueba que nunca se ha visto
> fallar no es una prueba: es una línea que imprime OK.

> **Y cuando una prueba falle, primero se duda de la prueba.** Ha pasado quince
> veces.

### CI

- `banco.yml` — todo el banco que devuelve código de salida, en cada push.
- `medidas.yml` — las tres que miden por reloj (`cpu.py`, `Soak`, `Cpu`), en runner propio, una vez al día.
- `build-apk.yml` — la APK, manual.

---

## 7. Estado actual conocido (nada de esto está oculto)

### Verde

En el commit `1d57ff1`: cero en las 19 reglas duras de `expo.py` (cuatro corridas),
motor `0 FALLA`, `cpu.py` sin ninguna ficha que se repinte sola y 0 píxeles fuera
de la zona repintada en 48 millones comparados, y el resto del banco en verde.
La APK firmada pasa `apk.py`.

### Rojo o discutible — lo decimos nosotros

1. **El contador `TOUCH` de `expo.py` no es reproducible.** El mismo binario da
   3451, 3449 y 3448 en tres corridas; el commit anterior da 3450. La entrada que
   baila es siempre la misma (una pestaña árabe del selector de canal, que aislada
   mide 53×40 estable). Es estado del andamio, no de la aplicación — cada
   trabajador reutiliza su `HOME` entre corridas. **No aislamos el predecesor
   exacto y no afirmamos cuál es.** Consecuencia: ese contador no sirve como cifra
   de control hasta que se cierre la fuga.

2. **`MainComponent` es una clase-dios.** 16 679 + 3 430 líneas, ~1000 miembros.

3. **Nada de `ACTION_SEND` se puede medir en el banco.** Un `Intent` no existe en
   escritorio. La tapa COMPARTIR se comprueba a mano en el teléfono. Está
   declarado como excepción en el código y en la bitácora.

4. **Deuda de motor apuntada, no hecha:** envolvente sólo AR (no ADSR completo),
   un único filtro paso bajo por voz (sin selector de tipo ni paso alto), sin LFO
   por pad aunque `Lfo.h` existe y lo usa el rack, punto de bucle booleano (el
   bucle es la ventana entera, sin *crossfade*), sin verbos de región sobre la
   selección de las asas de recorte, sin detección de tono (no hay afinador).

5. **Forma de la interfaz, medido:** la mesa de mezclas tiene 100 controles con un
   1 % de iconos; `mix` y `llena-mix` 73 con 2 %. Es el hallazgo de forma más
   grande abierto y está sin tocar a propósito.

6. **El historial tiene una inconsistencia declarada:** 416 commits van con la
   identidad de la casa (`ARTiFACTS <eddierealting@gmail.com>`) y **uno** con otra
   identidad, puesto a propósito para comprobar si GitHub verifica la firma con la
   clave que sí está registrada en la cuenta (la verifica; los 416 salen
   `verified false, no_user`). Está pendiente decidir: deshacerlo, adoptarlo, o
   registrar una clave propia para la identidad de la casa — lo último necesita
   una persona.

---

## 8. Cómo compilar y correr el banco

```bash
# escritorio (desarrollo y banco)
cmake -B build -DZATI_BENCH=ON && cmake --build build -j"$(nproc)"

# pantalla virtual: sin ella JUCE se cae en Component::centreWithSize
# y parece un fallo del código
Xvfb :99 -screen 0 1920x1080x24 &

DISPLAY=:99 python3 Tests/expo.py       # ~7 min, 1484 corridas
DISPLAY=:99 python3 Tests/cpu.py        # SOLA: mide por reloj
build/StressTest_artefacts/Release/StressTest
```

Android sale de `Zati.jucer` por Projucer → Gradle: JUCE no soporta Android desde
su API de CMake. Lo hace `.github/workflows/build-apk.yml`.

---

## 9. Qué nos gustaría que revisara esta lectura

Por orden de interés. No hace falta contestarlas todas; preferimos tres bien
argumentadas a diez superficiales.

**1. La clase-dios.** `MainComponent` son 20 000 líneas entre `.h` y `.cpp`.
¿Partirla merece la pena, o el coste de coordinar N objetos con estado compartido
es peor que el de una clase grande bien comentada? Si merece la pena: ¿por qué
corte — por ficha, por subsistema (transporte / mezcla / proyecto), o extrayendo
un modelo observable del que cuelgue todo? Nos interesa el argumento, no la
preferencia.

**2. El contrato del hilo de audio.** ¿Aguanta? Concretamente:
   - ¿Hay algún camino de `AudioEngine::process*` que pueda reservar, bloquear o
     soltar la última referencia de un `SampleBuffer`?
   - La regla «una cola por productor» está escrita, pero ¿está **vigilada**?
     Hoy no hay prueba que cace un segundo productor en `commands`. ¿Cómo la
     harías?
   - Uso de `std::memory_order`: ¿hay `relaxed` donde hace falta `acquire`/`release`?

**3. La fuga de estado del banco (§7.1).** Es un problema de andamio, pero
envenena una cifra de control. ¿Cuál es el arreglo correcto: `HOME` limpio por
corrida (coste: 64 WAV por corrida × 1484), limpiar sólo el estado de sesión entre
corridas, o hacer la corrida determinista de otra forma?

**4. Las reglas derivadas del fuente.** `deshacer.py`, `proyecto.py`,
`profundidad.py` y las dos nuevas de `plano.py` leen `Source/*.cpp` con expresiones
regulares y juzgan. Funcionan, pero son frágiles por construcción. ¿Vale la pena
pasarlas a un análisis con AST real (libclang), o el coste no compensa para reglas
que un humano revisa cuando fallan?

**5. Seguridad y privacidad.** Permisos declarados: `RECORD_AUDIO`,
`READ_MEDIA_AUDIO`, `READ_EXTERNAL_STORAGE`, `WRITE_EXTERNAL_STORAGE` (los dos
últimos acotados a SDK ≤ 28). El JNI de `MediaStore.cpp` está escrito a mano con
comprobación de excepción en cada paso. ¿Hay algo mal ahí — referencias locales
que se escapan, excepciones que se tragan, un `Intent` que otorgue más permiso del
necesario?

**6. Lo que falta para que alguien que no produce música llegue a algo.** Es la
pregunta de producto que motivó la última tanda. El estudio dice que no faltan
mandos (los mínimos de usabilidad salen a cero en las 44 fichas, y nada está a más
de tres toques). ¿Estás de acuerdo con ese diagnóstico leyendo el código, o ves
otra cosa?

**7. Cualquier cosa que te parezca mal y no esté en esta lista.** Especialmente si
contradice algo que este documento afirma: preferimos que se nos corrija una
premisa a que se nos valide una conclusión.

---

## 10. Cómo está escrito el código, para que no sorprenda

Tres convenciones de la casa que un revisor externo notará enseguida:

- **Los comentarios explican POR QUÉ, y en particular qué fallo concreto motivó
  cada decisión, con las cifras medidas.** No son documentación de la API: son el
  registro de por qué el código es así y no de la forma obvia. Son densos a
  propósito. Ejemplo real:

  > `//  La cifra va dentro de Lang::ltr, que es la regla de la casa para números`
  > `//  latinos en escritura árabe, y «HACE n D» lleva CLAVE PROPIA: reaprovechar`
  > `//  una por parecerse en español es lo que costó ATRÁS y EMPEZAR en el tour.`

- **Están en español, sin tildes en los comentarios de código** (por historia del
  proyecto). Los identificadores mezclan español e inglés según la capa: el motor
  tiende a inglés, la interfaz a español.

- **`BITACORA.md` (~790 KB) es la historia tanda a tanda.** No hay que leerla; se
  consulta con `grep` cuando una decisión no se entiende. `CLAUDE.md` tiene las
  reglas vigentes y es corto.

---

*Este dossier describe el commit `1d57ff1`. Cualquier afirmación suya que no
cuadre con el código es un error del dossier y queremos saberlo.*
