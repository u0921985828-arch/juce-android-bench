# Lo que esta en dos sitios

Esta lista es la mitad del desglose que **no sale del volcado**. `Tests/desglose.py`
encuentra automaticamente dos clases de repeticion —el mismo rotulo y el mismo
dibujo en controles distintos— y hay una tercera que no puede ver: dos botones
cuyo `onClick` hace lo mismo aunque se llamen distinto, y un rotulo que solo se
repite en UN estado. Eso se lee del codigo, y por eso va aqui con su ruta:
para que se pueda comprobar en vez de creerse.

Regla de la casa que decide: **una funcion, un dueno; la que se va deja una
puerta y nunca una copia**. Una PUERTA —una tapa que lleva a otra ficha— no es
un duplicado.

---

## 1. ~~CANCION dicho dos veces~~ — ARREGLADO

Era la queja que motivo este desglose y era real:

| control | rotulo ES | rotulo EN | que hace | donde |
|---|---|---|---|---|
| `songButton` | CANCION | SONG | abre la ficha CANCION | la cara |
| las 4 tapas de modo | **CANCION** | SONG MODE | `engine.setSongMode` | cara, sec, piano, song |

`modoTapa` (`Source/MainComponent.cpp:200`) pone `T("CANCION")` **y**
`Iconos::Id::cancion` cuando el modo esta encendido. `songButton` usa `T("SONG")`
—que en espanol tambien es "CANCION"— y lleva ese mismo icono. Asi que **en modo
cancion, en la cara, hay dos tapas seguidas con la misma palabra y el mismo
dibujo**: una abre una ficha y la otra cambia lo que toca PLAY.

En ingles se separan (`SONG` / `SONG MODE`), o sea que es un homonimo **de esta
compilacion**: quien lea el codigo en ingles no lo ve nunca.

Y `Tests/desglose.py` tampoco lo veia, porque mide el estado apagado —ahi las
cuatro dicen PATRON—. Es el mismo agujero que ya obligo a medir `secp` (la
pagina con un paso tocado) e `instd` (con packs instalados): **un estado que el
banco no abre es un estado sin medir**.

**Arreglado dos veces, y la primera salio peor.** La fila de la tapa de modo ya
decia «SONG MODE», «歌曲模式» y «وضع الأغنية» en las otras tres lenguas: era el
ESPANOL el que estaba prestado de otra clave. Se le pusieron claves propias
—`MODO CANCION|modo` y `MODO PATRON|modo`— y el texto paso a «MODO CANCION», y
**eso se deshizo con su cifra**: esa fila se reparte por el TEXTO y «MODO
CANCION» pide 70 px donde tiene 51, «PATTERN MODE» 70 donde tiene 54. De 0 TRUNC
a **211**. La clave se quedo -el desacople era lo que estaba bien- y la palabra
volvio a la corta, o sea que **el homonimo volvio con ella** y este apartado lo
dio por cerrado una tanda entera.

**Arreglado de verdad: la palabra tampoco es la de nadie.** `CICLO` y `ARREGLO`.
La segunda es la que esta casa ya usa para la linea de tiempo —«grabar al
arreglo», «las herramientas de arreglo»— y la primera no es BUCLE porque BUCLE
ya es el tramo de la cancion y volveria a chocar en la ficha donde esta tapa
tambien vive. Las dos son mas cortas que las que sustituyen: **3736 TOUCH a
3726**, cero TRUNC.

**Y ahora lo juzga algo.** `Tests/planos.py` gana la regla `gemelas`: dos tapas
de la MISMA capa con el mismo rotulo y el mismo dibujo. Dentro de la misma capa
y no de la misma pantalla, que es lo que separa un homonimo de una ficha abierta
encima de la cara —con `ZATI_OPEN=sec` el PLAY de la ficha y el de la cara son
dos tapas visibles con la misma palabra, y eso NO es un duplicado: son dos tapas
de un estado—. Y `planos.py` si corre en CI, que es lo que a este fichero le
faltaba.

**Y en su primera corrida saco una SEGUNDA que no habia visto nadie:** en la
pagina del PIANO, la pestana `PATRON` (265,181 113x44) y la tapa de modo
(35,741 63x40), misma palabra, mismo dibujo, misma ficha. Cuatro pantallas
—`piano`, `pianod`, `pick` y `llena-piano`—. Es exactamente la misma familia, y
el mismo arreglo la cierra.

Y de paso salio una segunda: la fila «CANCION» que la tapa dejaba de usar tenia
un unico cliente mas, `exportSourceLabel`, que con ella escribia **«Origen: SONG
MODE»** en tres de los cuatro idiomas. Lo que se exporta es la cancion y no un
modo, asi que esa linea pasa a `T("SONG")` y la fila se retira.

**Y el estado entra en el banco**: `ZATI_OPEN=songm` abre la cara EN MODO
CANCION, que es donde el duplicado vivia. Sin esa entrada, arreglarlo y volver a
romperlo darian la misma corrida en verde.

## 2. El mismo dibujo en rotulos distintos

Sale del volcado, y no todos son un fallo. Los que lo son estan marcados.

| dibujo | lo llevan | juicio |
|---|---|---|
| `carpeta` | ~~CAMBIAR, CARGAR KIT, PROYECTOS,~~ USAR ESTA CARPETA | **arreglado a medias, y a proposito**: ver debajo |
| `exportar` | EXPORTAR, MASTER, PISTAS | MASTER y PISTAS son dos formas de exportar y estan en la MISMA fila |
| `play` | OIR, PLAY | OIR es escuchar el pad, PLAY es el transporte |
| `sec` | PASOS, SEC | puerta y pagina: correcto |
| `rack` | ENVIOS, RACK | puerta y ficha: correcto |
| `sonido` | AUDIO, SONIDO | pagina de ajustes contra pagina del pad |
| `mano` | GESTOS, TOUR | dos cosas distintas de la misma ficha |
| `abrir` | ABRIR, MIS KITS | dos cosas |
| `copiar`/`pegar`/`guardar` | COPIAR / COPIAR FILA, etc. | el verbo es el mismo, el alcance no |

**Lo de `carpeta`, con su razon.** De las cuatro, tres son literalmente la misma
accion —«senala una carpeta»: CAMBIAR elige donde cae el rebote, CARGAR KIT
elige la carpeta de sonidos y USAR ESTA CARPETA la confirma— y ademas **nunca
salen dos en la misma fila**: el navegador tiene un modo y en modo carpeta su
fila de acciones es UNA tapa. Ahi el dibujo repetido es correcto: una funcion,
un dueno.

La cuarta si era un fallo. **PROYECTOS** es una PAGINA de AJUSTES, hermana de
AUDIO, MIDI y ASPECTO, y lo que ensena es una LISTA de cosas guardadas con su
nombre; una carpeta ahi dice «ficheros» donde pone «proyectos». Lleva dibujo
propio (`lista`): tres renglones con su punto delante, horizontales a proposito
—`mezcla` son lineas VERTICALES y `sec` una fila de barras, asi que el eje ya lo
separa de los dos con los que se podria confundir—.

## 3. El mismo rotulo en controles distintos

Lo que el volcado saco, quitando lo que es correcto por diseno:

- **PLAY** y **PATRON/CANCION** en `cara, sec, piano, song`: NO es un duplicado.
  Son varias tapas de UN estado, sincronizadas —lo mismo que ya se hacia con
  PLAY— y la alternativa es peor: obligar a cerrar la ficha para pulsar.
- **COPIAR / PEGAR** en `paso, sec, song`: tres alcances distintos (patron, fila
  de un pad, compas de la cancion) con la misma palabra y el mismo dibujo.
  Ninguno dice de que.
- **VACIAR** en `piano, sec, song`: igual, y ademas el de `song` **no vacia
  nada** —pone la brocha a cero—.
- **SONIDO** en `pads, song`: en ingles son `ONE SHOT` y `SOUND`. Otro homonimo
  solo de la compilacion espanola.
- **ATRAS** en `paso, song, tour`: en el tour ya tiene clave propia por esto
  mismo; en `paso` y `song` son dos desplazamientos distintos.
- **REV** en `cara/xy` (reverb) y `pad2` (reves): dos cosas que **no tienen nada
  que ver** con la misma abreviatura de tres letras.
- **BUCLE** en `pad2` (el bucle de la muestra) y `song` (el tramo en bucle de la
  cancion).
- Los seis efectos (**FLT HPF DRV DLY BIT REV**) en `cara` y `xy`: es el mismo
  selector en dos sitios, correcto.

## 4. Botones que escriben el mismo estado del motor

| estado | quien lo escribe | ruta |
|---|---|---|
| `setPlaying` | `playButton`, `seqPlayBtn`, `songPlayBtn`, y **`recButton`** | `MainComponent.cpp` |
| `setSongMode` | las 4 tapas de modo, **y `songPlayBtn`** de rebote | idem |

`recButton` arranca el transporte si estaba parado, y `songPlayBtn` enciende el
modo cancion si estaba apagado: los dos son **unidireccionales** —encienden y no
apagan—, que es la clase de control que deja la maquina en un estado que no
elegiste y no puedes deshacer con la misma tapa.
