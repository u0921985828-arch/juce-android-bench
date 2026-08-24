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

## 1. CANCION dicho dos veces, con el mismo dibujo, y solo en un estado

Es la queja que motivo este desglose y es real:

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

Y `Tests/desglose.py` tampoco lo ve, porque mide el estado apagado —ahi las
cuatro dicen PATRON—. Es el mismo agujero que ya obligo a medir `secp` (la
pagina con un paso tocado) e `instd` (con packs instalados): **un estado que el
banco no abre es un estado sin medir**.

## 2. El mismo dibujo en rotulos distintos

Sale del volcado, y no todos son un fallo. Los que lo son estan marcados.

| dibujo | lo llevan | juicio |
|---|---|---|
| `carpeta` | CAMBIAR, CARGAR KIT, PROYECTOS, USAR ESTA CARPETA | **cuatro** cosas distintas con el mismo dibujo |
| `exportar` | EXPORTAR, MASTER, PISTAS | MASTER y PISTAS son dos formas de exportar y estan en la MISMA fila |
| `play` | OIR, PLAY | OIR es escuchar el pad, PLAY es el transporte |
| `sec` | PASOS, SEC | puerta y pagina: correcto |
| `rack` | ENVIOS, RACK | puerta y ficha: correcto |
| `sonido` | AUDIO, SONIDO | pagina de ajustes contra pagina del pad |
| `mano` | GESTOS, TOUR | dos cosas distintas de la misma ficha |
| `abrir` | ABRIR, MIS KITS | dos cosas |
| `copiar`/`pegar`/`guardar` | COPIAR / COPIAR FILA, etc. | el verbo es el mismo, el alcance no |

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
