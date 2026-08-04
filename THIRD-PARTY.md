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

No se distribuye ni un solo sample, loop, preset ni sonido de fábrica. La app
sale vacía y sólo suena lo que el usuario mete, así que no hay nada de audio
con derechos de terceros dentro del paquete. Si algún día se meten packs de
fábrica, cada uno necesita su propia licencia por escrito antes de entrar aquí.

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
