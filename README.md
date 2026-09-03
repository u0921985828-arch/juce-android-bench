# Zati Sampler — sampler nativo para Android (JUCE / C++) · por ARTiFACTS

En la tienda y bajo el icono se llama **Zati Sampler**. El proyecto, las rutas
de compilación y la marca serigrafiada en la cara de la máquina siguen diciendo
**ZATI**, y el identificador de Android es `com.artifacts.zati` — ése no se
toca: cambiarlo sería otra aplicación distinta, sin actualización posible desde
ésta.

Un sampler de 64 pads en cuatro bancos escrito en C++ sobre JUCE 8, con motor propio y camino de
audio de baja latencia por Oboe/AAudio. Nació como prueba de latencia contra un
prototipo en WebView y hoy es la aplicación entera: pads, secuenciador,
mezclador, efectos, proyectos y exportación.

**La referencia completa de la máquina está en [`ZATI.md`](ZATI.md)**: qué es,
la cara y las catorce fichas control por control, los gestos, el motor, los 256
instrumentos, la fábrica, los formatos de fichero, el aparato, la compilación y
el banco. Este fichero es la portada; ése es el manual de la máquina.

El repositorio se compila de dos formas y las dos importan:

- **Escritorio (CMake):** para desarrollar y verificar DSP y lógica sin teléfono.
- **Android (Projucer → Gradle):** el objetivo real. JUCE no soporta Android
  desde su API de CMake, así que el APK sale de `Zati.jucer` por el workflow
  `.github/workflows/build-apk.yml`.

## Qué hace

- **64 pads en cuatro bancos de 16** con color propio (zati), velocidad por
  posición del dedo y una reserva común de voces —su tamaño lo decide el
  teléfono, ver `DeviceTier`— con robo por antigüedad. La rejilla muestra un
  banco y el resto sigue sonando.
- **Motor**: acumulador de fase fraccionario + interpolación Hermite de 4
  puntos, dos modos de tono — CINTA (varispeed) y TONO (mantiene la duración
  con granos solapados) —, recorte, bucle, reverso, choke, paneo y envolvente.
- **Secuenciador** de 8 bancos, longitud variable de 16 a 64 pasos, cadena de
  bancos, nota por paso y una línea de tiempo de canción.
- **Once efectos** y **seis ranuras** en la cara para ponerlos, con cuánto
  manda cada pad a cada uno: FLT (un barrido bidireccional, paso bajo a un lado
  y paso alto al otro, con el centro neutro), HPF, DRV, DLY, BIT, REV, un EQ de
  cinco bandas con su propia cara y analizador, y la familia de dinámica —CMP,
  GTE, DSS y LIM—. Una ranura vacía dice «+» y abre el menú.
- **Panel XY** para tocarlos: dos parámetros a la vez, momentáneo o fijo, en la
  mitad de arriba de la cara — los pads siguen debajo y se pueden disparar
  mientras barres.
- **Cuatro carcasas** (PAPEL, GRAFITO, ACERO, LACA) y **cuatro idiomas**
  (español, inglés, chino, árabe, con la interfaz espejada en árabe).
- **Grabación por micro** a un pad y grabación de la interpretación al patrón,
  con compensación de la latencia de salida.
- **Proyectos** autocontenidos (audio incluido) y **sesión recuperable**: lo que
  estabas haciendo vuelve al abrir aunque el sistema matara el proceso.
- **Exportación** a WAV, master o pistas.

## Disciplina de hilos (el núcleo)

| Hilo | Hace | Nunca hace |
| --- | --- | --- |
| **Mensajes** | interfaz, decodificación, **todas las liberaciones de memoria** | DSP |
| **Audio** (`getNextAudioBlock`) | renderizar | reservar, bloquear, E/S, `std::string`, **liberar** |
| **Fondo** | decodificar samples, escribir la sesión, exportar | tocar la interfaz directamente |

- **Disparos**: `CommandFifo`, un productor (mensajes) y un consumidor (audio).
- **Samples**: intercambio atómico de puntero. El hilo de audio sólo *lee* y sólo
  *incrementa* referencias — nunca decrementa, así que no puede provocar un
  `delete`. Los búferes retirados los borra un temporizador del hilo de mensajes
  (`AudioEngine::collectRetiredSamples`).

## Compilar en escritorio

Requisitos: CMake ≥ 3.22, compilador C++17 y, en Linux, las cabeceras de audio y
GUI que pide JUCE:

```sh
sudo apt install libasound2-dev libjack-jackd2-dev libx11-dev libxext-dev \
     libxinerama-dev libxrandr-dev libxcursor-dev libfreetype6-dev \
     libcurl4-openssl-dev libwebkit2gtk-4.1-dev
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/Zati_artefacts/Release/Zati
```

La primera configuración clona JUCE 8.0.15 (versión fijada).

> El escritorio sólo valida DSP y fontanería. La latencia que cuenta es la del
> teléfono: en un móvil sin MMAP el mezclador del sistema pone un suelo de unos
> 40 ms que ninguna aplicación puede bajar. El panel SET lo dice con todas las
> letras — `via: compartida MEZCLADOR` significa que el camino rápido no existe
> en ese aparato.

### La latencia se mide desde la propia app

No hace falta cable ni OboeTester: **AJUSTES → AUDIO → MEDIR** emite un clic,
lo graba por el micrófono a través de la misma llamada de audio y dice los
milisegundos de ida y vuelta, con el color diciendo si son buenos (≤30),
regulares (≤60) o malos. Es una medida y no una estimación — lo que el
dispositivo *declara* es otra cosa, y la ficha enseña las dos.

Aquí va el número cuando esté tomado, y va con las cuatro cosas sin las que no
significa nada:

| teléfono | Android | vía | bloque | ida y vuelta |
|---|---|---|---|---|
| *(pendiente: una toma con MEDIR en el teléfono de referencia)* | | | | |

## Compilar el APK

El workflow es manual (`workflow_dispatch`). Construye el Projucer, genera el
proyecto Gradle desde `Zati.jucer`, compila debug y release y los cuelga de la
etiqueta `apk-latest`. Firma con la clave de subida si están configurados los
secretos, y con la de debug si no — ver `GOOGLE-PLAY.md`.

## Antes de publicar

Tres documentos, y ninguno es opcional:

- **`GOOGLE-PLAY.md`** — qué falta para subirla, separado entre lo que se
  arregla en el repositorio y lo que sólo puedes hacer tú.
- **`THIRD-PARTY.md`** — qué lleva dentro y con qué licencia. La decisión de
  JUCE (GPLv3 o licencia comercial) se toma **antes** del primer release.
- **`PRIVACY.md`** — la política de privacidad, lista para publicar y enlazar
  desde la ficha.

## Identidad visual — mantenerse lejos del trade dress

El motor es propiedad intelectual original y no es el problema; la presentación
sí lo sería si imitara un aparato existente. Las reglas que sigue el proyecto:

- **Estética propia.** Una rejilla de 4×4 pads es un estándar funcional de la
  industria y se puede usar; copiar los colores, la tipografía, las texturas o
  la disposición exacta de un aparato concreto, no.
- **Nombres de efecto genéricos**, que describen el DSP en lugar de tomar
  prestada la etiqueta de una marca.
- **Sin "404" ni terminología de terceros** en cadenas visibles, en el
  identificador de paquete (`com.artifacts.zati`) ni en la ficha de la tienda.
  Comprobado: no hay ninguna mención a marcas ajenas en `Source/`.

(Esto no es asesoramiento legal. Antes de publicar, que lo mire alguien que sepa.)
