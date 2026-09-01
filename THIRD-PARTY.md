# Terceros — qué lleva ZATI dentro y con qué permiso

Todo lo que se distribuye dentro del APK y no es código de ARTiFACTS. Los
textos completos de licencia están en `LICENSES/`, y esos textos **tienen que
viajar con la app**: las dos tipografías son OFL, y la OFL obliga a incluir su
licencia allí donde se redistribuya el tipo.

| Qué | Versión | Licencia | Dónde está |
|---|---|---|---|
| JUCE | 8.0.4 | GPLv3 **o** licencia comercial de JUCE — ver abajo | se compila dentro del binario |
| Oboe (dentro de JUCE) | la que trae JUCE 8.0.4 | Apache 2.0 | `LICENSES/Oboe-Apache-2.0.txt` |
| Oswald (Bold, Medium) | Google Fonts | SIL OFL 1.1 | `LICENSES/Oswald-OFL.txt` |
| JetBrains Mono (Regular, Bold) | JetBrains | SIL OFL 1.1 | `LICENSES/JetBrainsMono-OFL.txt` |

## Audio de fábrica — 31 grabaciones, y **sin licencia**

Este apartado decía *«no se distribuye ni un solo sample… la app sale vacía»*.
**Era falso**, y lo era desde el día en que media fábrica dejó de sintetizarse:
`Source/kits/` lleva **31 ficheros `.flac`, 916 250 bytes**, incrustados en el
binario como recursos (`ZatiData`). La frase de cierre de aquel párrafo —*«si
algún día se meten packs de fábrica, cada uno necesita su propia licencia por
escrito antes de entrar aquí»*— es exactamente la condición que **no se ha
cumplido**.

| Qué | Cuántos | De dónde |
|---|---|---|
| banco A, ACUSTICA | 15 de 16 | material de batería muestreada de terceros |
| banco B, MAQUINA | 16 de 16 | material de caja de ritmos de terceros |
| bancos C y D | 0 de 32 | sintetizados por ARTiFACTS, sin origen externo |

De dónde sale cada uno, fichero por fichero, consta en `Tools/fabrica.py`, que
no corre en la compilación y existe justo para eso. Los `.flac` están a 48 kHz,
mono, recortados por delante y por detrás, y normalizados por el mismo camino
que la síntesis.

**Esto bloquea la publicación.** No existe fila de licencia para ninguna de las
31 en ningún sitio de este repositorio, y no se puede inventar: una licencia es
un documento firmado o no es nada. Hay dos salidas y sólo dos:

1. **Licenciar por escrito** cada grabación, y anotarla en la tabla de arriba
   con su documento. Es lo que este mismo fichero exigía por adelantado.
2. **Sacarlas del APK sintetizándolas.** Las 31 filas con muestra ya llevan sus
   parámetros de síntesis —son lo que suena si el recurso no está, que en
   escritorio pasa de verdad— y lo que falta es que se parezcan lo bastante.
   `Tests/clon.py` es la puerta, medida con el mismo descriptor que usa
   `Tests/kits.py` para decir si dos sonidos de la fábrica son el mismo:
   **hoy la mejor está a 6.90 dB y la mediana en 17.81**, y el listón es 4 —
   por debajo de ahí `kits.py` las llamaría el mismo sonido y los FLAC pueden
   salir. Ninguna de las 31 llega todavía.

Lo protegido es el fichero, no el timbre: que un bombo tenga la afinación
cayendo no es de nadie. Por eso la salida 2 es una salida y no un rodeo.

**Y no hay ninguna marca ajena en lo que se publica**, que es una cosa distinta
de la licencia y sí está medida: `Tests/marcas.py` lee las reglas 1 y 2 de
`GOOGLE-PLAY.md` §1.5 y las contrasta contra `Source/` y `Zati.jucer` en cada
corrida del banco. Los nombres de los aparatos viven en `Tools/fabrica.py` y en
`Tests/clon.py`, que no entran en el APK y donde tienen que estar: sin ellos no
quedaría constancia de la procedencia, que es justo lo que este apartado
necesita para poder cerrarse algún día.

## Lo de JUCE es una decisión, no un trámite

Ahora mismo el proyecto compila en el modo GPL/prueba de JUCE
(`JUCE_DISPLAY_SPLASH_SCREEN=1`, `JUCE_REPORT_APP_USAGE=1` en `CMakeLists.txt`).
En ese modo JUCE se usa bajo **GPLv3**, y la GPLv3 es vírica: publicar la app
así obliga a publicar también el código fuente completo de ZATI bajo GPLv3 y a
ofrecérselo a cualquiera que tenga el APK. Se puede subir a Google Play — no lo
prohíbe — pero hay que asumir que el proyecto queda abierto.

La alternativa es adquirir una licencia de JUCE que permita distribuir en
binario sin abrir el código. Cuál corresponde y qué cuesta depende de la
facturación y de las condiciones vigentes de JUCE: **hay que mirarlo en
juce.com antes de publicar**, no darlo por sabido. Este archivo no fija esas
condiciones porque cambian.

Sea cual sea el camino, es una decisión que se toma **antes** del primer
release público, no después: cambiarla luego significa retirar la app.
