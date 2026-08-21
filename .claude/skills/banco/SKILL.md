---
name: banco
description: Correr el banco de pruebas de ZATI y juzgar lo que sale. Usar SIEMPRE antes de dar por terminado cualquier cambio en la app - interfaz, motor, idiomas, carcasas, fabrica o proyecto por defecto - y antes de compilar una APK. Tambien cuando haya que escribir una prueba nueva, porque aqui esta la regla de como se valida una prueba antes de creerla.
---

# El banco de ZATI

Nada se entrega sin medirlo. Este proyecto tiene banco propio y existe porque
cada fallo serio que ha aparecido lo encontro una medida y no una captura de
pantalla. `CLAUDE.md` cuenta QUE encontro cada prueba; esto es como se corren y
como se juzgan.

## Antes de nada

Sin pantalla virtual, JUCE se cae en `Component::centreWithSize` y parece un
fallo del codigo. Comprobar SIEMPRE primero:

```
DISPLAY=:99 xdpyinfo >/dev/null 2>&1 || nohup Xvfb :99 -screen 0 1920x1080x24 >/dev/null 2>&1 </dev/null &
cmake -B build -DZATI_BENCH=ON && cmake --build build -j"$(nproc)"
```

Y no recompilar mientras el banco corre: los procesos hijos se quedan sin
binario a medias y sale un `PermissionError` que no tiene nada que ver con el
codigo.

## Que se corre segun lo que se toco

| Se toco | Se corre |
|---|---|
| cualquier cosa de interfaz | `Tests/expo.py` (756 corridas, ~14 min) |
| textos, rotulos, `T()` | `Tests/lang.py` **y** `expo.py` |
| colores, tokens, pieles | `Tests/skins.py` |
| motor, voces, efectos, envios | `build/StressTest_artefacts/Release/StressTest` |
| guardar/abrir/sesion | `Tests/session.py` |
| arreglo, cancion, patrones | `Tests/arr.py` |
| exportacion, carpetas, permisos | `Tests/export.py` |
| sonidos de fabrica | `Tests/kits.py` |
| defectos del proyecto nuevo | `Tests/nuevo.py` |
| guardar o cargar un kit | `Tests/kit.py` |
| piano roll, notas, compas, EUCLIDES | `Tests/piano.py` |
| instrumentos, packs, licencias | `Tests/dlc.py` |
| el tour de bienvenida | `Tests/tour.py` |
| una APK | `Tests/apk.py <fichero>` |

Ante la duda, todo. El banco entero son unos quince minutos y una APK mal son
doce de CI mas el tiempo de la persona que se la instala.

## Como se juzga

- **Cero es cero.** `expo.py` tiene que salir con 0 solapes, 0 fuera de ventana,
  0 celdas por debajo del dedo minimo, 0 rotulos cortados, 0 sin traducir y 0
  residuo al cambiar de pagina. Los apretones conocidos -las tres pestanas de la
  cara en 915x412 y tres rotulos arabes- son la unica excepcion y estan contados.
- **Un numero que empeora es un fallo aunque el resto pase.** Dos arreglos de
  maquetado se revirtieron por eso: cambiaban 73 apretones por 126 cortes.
  Cambiar un apreton por un corte no es un arreglo.
- **Se compara con la corrida anterior**, no solo con el umbral. Un banco que
  pasa con menos margen que ayer esta avisando.

## LA PRUEBA SE PRUEBA ANTES DE CREERLA

Una prueba que nunca se ha visto fallar no es una prueba: es una linea que
imprime OK. Antes de fiarse de una nueva, o de una que acaba de empezar a pasar,
**se rompe el codigo a proposito y se comprueba que sale FALLA**, con el numero
que se esperaba.

Se ha pagado cuatro veces por saltarse esto y se ha usado bien varias:

- El defecto de los envios: se volvio a poner el uno de antes y salio
  `envios max 1.000 suma 384.000` -64 x 6- y ademas "los dos caminos a un
  proyecto vacio no coinciden". Sin esa vuelta, un cero podria ser un getter roto.
- El master contra el aviso del sistema: con los dos escribiendo en el mismo
  numero sale `puesto 1.00` donde tiene que salir 0.50.
- La tabla de sonidos vieja saca cinco pares como FALLA.

Y su reverso, que es la regla hermana: **cuando una prueba falle, primero se
duda de la prueba**. Dieciseis senos en fase metian el saturador en accion
permanente; una muestra con envolvente hacia que "volvio el nivel?" dependiera
de cuando se preguntaba; una prueba de pan medio cero porque la muestra empieza
con medio segundo de silencio. Una medida que no se ha corregido antes de
creerla no es una medida.

## Al escribir una prueba nueva

- **Los umbrales y las respuestas van en la prueba, no en el codigo que se
  prueba.** Una prueba que lee la constante que juzga cambia de opinion a la vez
  que el fallo.
- **Los coeficientes se escriben desde la norma**, no se copian del C++, y la
  FFT del banco esta escrita ahi y no importada: el banco no puede depender de
  un paquete que puede no estar en la maquina que lo corre.
- **Dos numeros a la vez cuando uno solo se puede enganar.** Quitar ruido se
  juzga por cuanto baja el suelo Y cuanto sobrevive el tono: solo el primero lo
  saca un silenciador, solo el segundo lo saca no hacer nada.
- **Con HOME temporal** cuando lo que se mide es un arranque limpio, o la app
  restaura la sesion que hubiera y no pasa nunca por el camino de la primera vez.
- **La corrida de control no toca el parametro** en vez de ponerlo a su valor
  por defecto: una maquina recien encendida y una que alguien ha puesto a cero
  no son lo mismo aunque den el mismo numero.
- **Medir la forma, no el nivel**, cuando lo que se compara son timbres. Y en
  decibelios: un coseno sobre bandas normalizadas da siempre un numero altisimo
  y no separa "parecido" de "identico".

## Al terminar

Los numeros van en el mensaje de commit, en espanol y sin tildes, contando el
fallo antes que el arreglo. Y una APK al final de la tanda, no una por commit.
