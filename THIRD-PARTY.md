# Terceros — qué lleva ZATI dentro y con qué permiso

Todo lo que se distribuye dentro del APK y no es código de ARTiFACTS. Los
textos completos de licencia están en `LICENSES/`, y esos textos **tienen que
viajar con la app**: las dos tipografías son OFL, y la OFL obliga a incluir su
licencia allí donde se redistribuya el tipo.

| Qué | Versión | Licencia | Dónde está |
|---|---|---|---|
| JUCE | 8.0.15 | GPLv3 **o** licencia comercial de JUCE — ver abajo | se compila dentro del binario |
| Oboe (dentro de JUCE) | 1.10.0 | Apache 2.0 | `LICENSES/Oboe-Apache-2.0.txt` |
| Oswald (Bold, Medium) | Google Fonts | SIL OFL 1.1 | `LICENSES/Oswald-OFL.txt` |
| JetBrains Mono (Regular, Bold) | JetBrains | SIL OFL 1.1 | `LICENSES/JetBrainsMono-OFL.txt` |

## Audio de fábrica — ninguno es de nadie

**La app no distribuye una sola grabación.** Los 64 sonidos de fábrica se
sintetizan al arrancar desde las recetas de `Source/Kits.h`, así que dentro del
APK no hay audio con derechos de terceros: sólo las dos tipografías y el código.

No siempre fue así, y conviene que conste. Este apartado afirmaba que «la app
sale vacía» mientras `Source/kits/` llevaba **31 ficheros `.flac`, 916 250
bytes**, incrustados como recurso — quince del banco A y los dieciséis del B,
sacados de máquinas de verdad y **sin una sola línea de licencia por escrito**.
La frase de cierre de aquel párrafo —*«si algún día se meten packs de fábrica,
cada uno necesita su propia licencia por escrito antes de entrar aquí»*— era la
condición que no se había cumplido.

Se resolvió por donde no cuesta un permiso: **quitándolas**. Con ellas se fueron
`Tools/fabrica.py`, que era el único sitio donde constaba la procedencia, y
`Tests/clon.py`, que medía cuánto se parecía cada receta a la suya. Ya no hay
procedencia que documentar.

**Lo que costó y lo que se ganó, medido** (`Tests/kits.py`, los 2016 pares):

| | con grabaciones | sintetizada |
|---|---|---|
| par más parecido (listón 4.0 dB) | 5.50 dB | **5.27 dB** |
| distancia mediana | 27.1 dB | **28.8 dB** |
| sonoridad, del más al menos sonoro (tope 6.0 dB) | 1.7 dB | **2.9 dB** |
| bytes de audio en el APK | 916 250 | **0** |

Y una cosa que mejoró de verdad al hacerlo. Comparando el T60 de cada receta
contra el de su grabación —`Tests/analiza.py`, mientras todavía se podían
comparar— la síntesis sonaba **x3.36 más larga que su máquina en la mediana y
x15.5 la peor**: un CLAP de 2.1 s donde el aparato da 0.135. Ésa es la razón de
que la percusión sintetizada sonara blanda, y se corrigió receta a receta hasta
**x1.00 de mediana y x1.33 la peor**.

El ajuste **no se aplicó a `metal`**: un cúmulo inarmónico bate, así que «tiempo
hasta caer 60 dB» se corta en el primer nulo del batido y no en la caída — subir
su `decay` bajaba el T60 medido. Era la medida fallando, no el código.

**Y el banco que lo hizo posible estaba roto.** `Tests/analiza.py` enumeraba las
filas de `Kits.h` con una expresión que exigía los nueve campos, y treinta y tres
están escritas en forma corta: no casaban, el contador no avanzaba, y **cada slot
a partir de la catorceava quedaba corrido**. Cada grabación se comparaba con la
receta de su vecina. No fallaba: daba números. Se descubrió porque un `RS` con
`decay` 0.01 —un fichero de 0.05 s— seguía midiendo 1.8 s de T60, que es
imposible.

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

**Y con ella va un fichero `LICENSE` que hoy no existe.** En la raíz no hay
ninguno: si se publica en modo GPLv3, la obligación de licenciar ZATI bajo
GPLv3 no está materializada en ninguna parte, y si se compra la licencia
comercial hace falta el aviso de copyright propio. `LICENSES/` sólo lleva los
textos de los terceros que viajan dentro.
