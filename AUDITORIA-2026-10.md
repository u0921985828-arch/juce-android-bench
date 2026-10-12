# Auditoria de ZATI, octubre de 2026

Revision de arriba abajo de la app con un criterio: valorar lo que tiene y no dejar pasar ningun fallo, por pequeño que sea. Este documento es el inventario entero; los arreglos van en los commits de la rama, cada uno con su prueba vista fallar antes y pasar despues.

## Como se hizo

- Diez dimensiones, cada una con su revisor: ux-flujos, visual, producto, secuenciador, rendimiento, codigo, audio-dsp, textos, android y persistencia. Cada uno leyo el codigo, uso la app en un telefono simulado de 393x851 y midio con los bancos de la casa.
- Cada hallazgo lleva donde esta, la evidencia, como reproducirlo y un arreglo propuesto. Los que se verificaron los repitio un segundo revisor a partir de cero, intentando refutarlos.
- **Huecos de cobertura:** las dimensiones *estabilidad* y *accesibilidad* no terminaron su revision y no tienen apartado propio. Lo que hay de ellas llega de rebote: la feria bajo ASan (0 errores) y los hallazgos de contraste y tamaño de toque de *visual* y *ux-flujos*.

## Cifras

| | |
|---|---|
| hallazgos | 362 |
| verificados por un segundo revisor | 57 (ninguno refutado) |
| bloqueante | 2 |
| alta | 68 |
| media | 143 |
| baja | 106 |
| detalle | 43 |
| arreglados en esta rama | 19 |

Los no verificados se dan por candidatos: la tasa de acierto de los 57 verificados fue del 100 %, pero cada uno se reproduce antes de tocarlo.

## Arreglado en esta rama

| hallazgo | gravedad | commit | medida |
|---|---|---|---|
| persistencia#0 BORRAR un proyecto cuyo nombre lleva un punto borra OTRO proyecto distinto | bloqueante | fb718ae | feria.py `punto`: carpeta fuera 0, vecino 0 -> 1, 1 |
| secuenciador#0 Cuantizar en directo está al revés: lo que tocas a tiempo se retrasa casi un paso y lo que tocas tarde suena sin cuantizar | bloqueante | fb718ae | StressTest: pronto 73 bloques, tarde 0 -> pronto 0, tarde 23 |
| persistencia#1 La pila de deshacer cruza proyectos: DESHACER tras NUEVO o ABRIR trae el contenido del proyecto anterior y GUARDAR lo escribe encima del actual | alta | fb718ae | feria.py `pila`: ABRIR deja la pila vacia 0 -> 1 |
| ux-flujos#0 DESHACER después de ABRIR mezcla dos proyectos: aplica al recién abierto un estado del proyecto anterior | alta | fb718ae | el mismo arreglo que persistencia#1 |
| ux-flujos#5 NUEVO no se puede deshacer, y DESHACER después deja un estado incoherente | alta | fb718ae | NUEVO vacia la pila y deja un paso para volver |
| ux-flujos#2 DESHACER recupera el audio de los pads pero no sus nombres: los rótulos mienten | alta | fb718ae | feria.py `pila`: el nombre del pad vuelve 0 -> 1 |
| persistencia#3 Un state.xml corrupto o truncado vacía la sesión en silencio y la sobrescribe | alta | fb718ae | feria.py `corrupto`: 0 copias -> 1 |
| audio-dsp#0 La puerta GTE tiene ataque y cierre invertidos: abre con el tiempo de cierre (120 ms) y cierra con el de ataque (0,5 ms) | alta | fb718ae | StressTest: abre al 90 % en 276.35 ms -> 1.17 ms |
| audio-dsp#1 Un pad disparado con ganancia efectiva 0 (mute, solo de otro pad o gain 0) se queda mudo para siempre aunque se quite el mute o se suba el gain | alta | fb718ae | StressTest: rms tras subir 0.0000 -> 0.2505 |
| audio-dsp#6 El choke no corta: el pad ahogado suena con su propia suelta (un hat abierto de 600 ms sigue a -5 dB a los 50 ms) | alta | fb718ae | StressTest: rms a los 10 ms 0.20990 -> 0.00000 |
| textos#0 ATAQUE, CAIDA e INICIO de la tira del paso no tienen fila en la tabla: salen sin tilde y sin traducir | alta | 7499333 | lang.py: 3 claves que faltan -> tabla completa |
| textos#1 El bloqueo de corte del filtro usa la clave «CORTE» de cortar selección: en inglés dice CUT y en chino 切断 | alta | 7499333 | CORTE|filtro; revisado a mano, lang.py no distingue sentidos |
| codigo#0 store.py sale con 0 aunque la app no saque ninguna foto de Play | alta | 7499333 | store.py con una app que no saca fotos: exit 0 -> exit 1 |
| codigo#1 arr.py esta rojo en HEAD: el asa del bloque estira un compas de mas | alta | 7499333 | arr.py FALLA -> OK (la regla seguia el modelo de antes de ce495da) |
| rendimiento#0 En segundo plano no se para nada: el temporizador sigue y, sin vblank, pinta el cuadro entero con la FFT incluida | alta | 7499333 | feria.py `fondo`: cuadros al fondo 20 -> 0 |
| android#3 Exportar MIDI nunca publica el .mid y además barre wav/ogg sueltos y pisa la URI de compartir | alta | 7499333 | SIN MEDIR: MediaStore no existe en escritorio, falta un telefono |
| persistencia#5 GUARDAR KIT con «Sobrescribir» mezcla el kit viejo y el nuevo | alta | c1d7764 | kit.py `sobrescribir`: el WAV del kit viejo queda 1 -> 0 (20 FALLA -> OK) |
| codigo#24 canalDeParam protege un indice y fxParamDe usa otro fuera de rango (aviso -Warray-bounds) | alta | c1d7764 | build de StressTest: aviso -Warray-bounds en pasoMod 1 -> 0; StressTest 171 OK, 0 FALLA |
| visual#0 Cifra dB de la mesa ilegible en PAPEL y GRAFITO (1.96 a 2.36:1) | alta | 7499333 | canales.py por carcasa: 1.67/1.99/1.67/9.47 -> 9.47/9.28/9.47/9.47 :1 |

## Decisiones que no son de codigo

- **audio-dsp#5, el swing a la mitad de lo que anuncia.** Arreglarlo cambia el groove de todo proyecto guardado con swing. Queda para decidir con quien toca: o se reescala la escala (100 % = 75 %, como una MPC) y se migra el valor guardado, o se cambia el rotulo.
- **audio-dsp#17, EXC atenua en vez de realzar.** El arreglo cambia el sonido de los presets que lo usan; necesita escucharse antes de fijar la formula.
- **secuenciador#16, el paso 16 cortado a 7 pt.** Diagnosticado: el maquetado decide la barra con un lado de celda que no es el que la rejilla acaba teniendo. El arreglo toca la escalera de la tarjeta de SEC y pide su propia tanda con fotos.

## ux-flujos

**Lo que esta bien:**

- La rejilla de SEC ya no salta: el panel de pasos tiene altura fija al cambiar de pad o de patrón (b-series, comparadas con compare -metric AE).
- DESHACER y REHACER no se mueven de sitio en la barra inferior, DESHACER sigue a la izquierda y funciona aunque haya una ficha abierta.
- Cada acción importante deja una frase en el pie: 'Cortado en 4 trozos - DESHACER para volver' (e07.png), 'Abierto "Mi tema" [64 pads]' (e39.png), 'BAJOS SAW BS · PAD 5' (e12.png).
- AUTO CHOP explica en una frase lo que va a hacer, pinta las marcas de corte sobre la onda (e06c.png), el botón dice cuántos trozos ('CORTAR EN 4') y se puede deshacer.
- Las confirmaciones peligrosas (NUEVO, ABRIR, BORRAR, sobrescribir con GUARDAR) piden dos toques, el botón armado dice qué va a hacer ('¿ABRIR Mi tema?') y caducan avisando ('Sin confirmar: no se ha hecho nada').
- GUARDAR no machaca otro proyecto sin preguntar: si el nombre ya existe y no es el abierto, pide confirmación (MainComponent.cpp:1085-1088).
- La exportación dura lo que anuncia (8,5 s en un WAV de 24 bits a 44,1 kHz), informa de los LUFS (-23,7) y no escribe ficheros para pads mudos.
- Se ve qué se está exportando (PATRÓN o CANCIÓN): ya no se exporta a ciegas.
- Al poner REV, el efecto se conecta a los pads y el pie lo dice: 'REVERB en CANAL 01: entran los 64 pads'.
- En CANCIÓN, el patrón P1 ya está colocado en el compás 1 y los bloques encajan en el compás.
- Hay 64 sonidos de fábrica nombrados y coloreados por pad (e39.png). EXTRAS añade 30 instrumentos por familias con icono propio (e11.png, e15.png).
- La sesión sobrevive a cerrar y abrir la app: el proyecto abierto vuelve con sus 64 pads (e41.png) y no repite el tour.
- Las tildes y la ñ ya salen bien (CANCIÓN, ATRÁS, ÓRGANOS, CAÑAS, MOMENTÁNEO) y las rutas no muestran /tmp en crudo.
- El tour se puede saltar desde el paso 4 y no vuelve a salir en el siguiente arranque.
- El piano roll permite dibujar notas largas arrastrando (b29.png).
- La ficha del pad agrupa SONIDO, RECORTE y EL PAD con icono y palabra, y RECORTE enseña la onda con sus asas, la frecuencia de muestreo y la duración (e02.png).
- El XY muestra qué parámetro va en cada eje con su valor (AMORT. 45 %, TAMAÑO 55 %) en cuanto hay un efecto elegido (e28.png).

| # | gravedad | v | hallazgo | donde | estado |
|---|---|---|---|---|---|
| 0 | alta | si | DESHACER después de ABRIR mezcla dos proyectos: aplica al recién abierto un estado del proyecto anterior | AJUSTES > PROYECTOS > ABRIR y luego DESHACER; MainComponent.cpp:14484 (loadProject) y 12302 (performUndo) | arreglado (fb718ae) |
| 1 | alta | si | AUTO CHOP pinta el aviso de qué pads va a pisar debajo de la onda: nunca se ve | Ficha AUTO CHOP (mantener un pad > EL PAD > AUTO CHOP); MainComponent_Paint.cpp:1125-1129 y 1188-1193 frente a MainComponent_Layout.cpp:4716 | pendiente |
| 2 | alta |  | DESHACER recupera el audio de los pads pero no sus nombres: los rótulos mienten | MainComponent.cpp:12076-12080 (capturePads) y 12115-12123 (restorePads); MainComponent.h:2560 (Snapshot) | arreglado (fb718ae) |
| 3 | alta |  | 'A SALVO' sale con cambios sin guardar en el proyecto | Cabecera; MainComponent.cpp:14580-14650 (lineaDeContinuidad); Lang.cpp:1791 | pendiente |
| 4 | alta |  | ABRIR y NUEVO no avisan de cambios sin guardar: el trabajo se pierde con una confirmación genérica | AJUSTES > PROYECTOS; MainComponent.cpp:1029-1035, 1110-1120 y 1124-1129 | pendiente |
| 5 | alta |  | NUEVO no se puede deshacer, y DESHACER después deja un estado incoherente | MainComponent.cpp:14848-14955 (newProject sin pushUndo) | arreglado (fb718ae) |
| 6 | alta |  | El botón SIGUIENTE del tour cambia de sitio entre pasos: toques perdidos y cierre accidental | Tour de bienvenida; MainComponent_Layout.cpp ~8120-8200 (muelle en la mitad contraria al objetivo) | pendiente |
| 7 | media |  | Sin dispositivo de audio, PLAY y REC se encienden pero no avanza nada ni se graba nada, y no lo dicen | Transporte; MainComponent.cpp:9121-9139 (ponTransporte) | pendiente |
| 8 | media |  | Grabar del micrófono y AUTO CHOP están escondidos en la tercera pestaña de una ficha que se abre con pulsación larga | Mantener pulsado un pad > EL PAD > FUENTE (e03.png); botón REC del transporte (e17.png) | pendiente, opinion |
| 9 | media |  | CORTAR sale gris sin explicar por qué, y apagar RESPETAR no lo reactiva | Ficha AUTO CHOP; MainComponent.cpp:12477-12497 (refreshChopSheet) | pendiente |
| 10 | media |  | La rejilla de pasos dibuja columnas que no existen y se traga los toques | SEC > PASOS con zoom 3:4; StepGrid.h:119-130 | pendiente |
| 11 | media |  | MEZCLA puede abrir en la página CANALES: 32 faders por los que no pasa ningún pad | MEZCLA (e23.png) | pendiente |
| 12 | media |  | XY dice 'PON UN EFECTO' con REV puesto, y luego 'SUENA' cuando no suena nada | XY; MainComponent_Paint.cpp:3464-3470 | pendiente |
| 13 | media |  | CARGAR: biblioteca vacía con 'prueba en otra carpeta', un campo 'nombre:' inútil y el pad elegido dos veces | CARGAR > pad > ficha 'CARGAR EN PAD 5' (e10.png) y EXTRAS (e11.png) | pendiente |
| 14 | media |  | En las fichas desplazables el × se va con el scroll | EXTRAS (e15.png) | pendiente |
| 15 | media |  | Las tapas GUARDAR/ABRIR/NUEVO/BORRAR bajan 34 pt por cada proyecto de la lista | AJUSTES > PROYECTOS (e29.png frente a e30.png) | pendiente |
| 18 | media |  | Al arrancar, el pie enseña 'ATASCO 2741 ms en eventos' en vez de 'Sesión recuperada' | Pie al arrancar; MainComponent.cpp:21441-21461 | pendiente |
| 19 | media |  | VACIAR borra el patrón sin aviso ni mensaje, y el pie se queda con datos viejos | SEC > PASOS > VACIAR; MainComponent.cpp:2316-2325 | pendiente |
| 20 | media |  | DESHACER con la ficha abierta deshace y además cierra la ficha: no se ve qué se deshizo | SEC abierta y DESHACER (b07.png) | pendiente |
| 22 | media |  | El selector de zoom (1:1, 2:1, 3:4) y el de filas (1-16, 1-8, 9-16) usan rótulos crípticos | Cabecera de SEC (e21.png, h_g.png) | pendiente, opinion |
| 23 | media |  | Las filas de la rejilla de pasos solo llevan número, sin el nombre del pad | SEC > PASOS (e21.png); selector de pad (b27.png) | pendiente, opinion |
| 24 | media |  | El piano roll nombra las notas de forma relativa (C0 es la raíz) y arranca de la raíz hacia abajo | SEC > PIANO (e20.png); PianoRoll.h:406-416 | pendiente |
| 25 | media |  | Exportar PISTAS anuncia 64 pistas y escribe 6, con nombres que no casan con el destino anunciado | AJUSTES > EXPORTAR (c-series); casa/Music/ZATI/Exports/Mi-tema/ | pendiente |
| 26 | media |  | Cambiar la fuente de exportación cambia también el modo de transporte | EXPORTAR > fuente; MainComponent.cpp:9141-9152 (ponModoCancion) | pendiente |
| 28 | media |  | El tour enseña controles que el usuario no tiene y mezcla nombres | Tour, pasos 4, 7, 13, 14, 22 y 24 (hoja1-4.png) | pendiente, opinion |
| 29 | media |  | CONTROL enseña mandos de un efecto que no existe | Pantalla principal, panel CONTROL (e07.png, e41.png, d08.png) | pendiente |
| 30 | media |  | La fila de efectos dice 'CANAL 01 · 0 PADS' sin explicar que así no se oye nada | EFECTOS en la pantalla principal (e07.png) y RACK (e25.png) | pendiente |
| 16 | baja |  | Tras caducar la confirmación, el pie dice 'no se ha hecho nada' mientras la tapa vuelve a estar armada en rojo | MainComponent.cpp:12036-12054 (armConfirm) y 22273-22278 | pendiente |
| 17 | baja |  | Al elegir un proyecto para abrirlo, su nombre pisa el campo NOMBRE del abierto | AJUSTES > PROYECTOS; MainComponent.cpp:1002-1006 (onSelected) | pendiente |
| 21 | baja |  | Las etiquetas de DESHACER no describen la acción: un arrastre de 4 pasos es 'PASO' y un bloque de canción es 'LÁPIZ' | Pie tras DESHACER (b-series, c02.png) | pendiente |
| 27 | baja |  | Durante la exportación hay dos barras de progreso, una tapa el título y el ×, y REBOTE desaparece | EXPORTAR mientras escribe (c-series) | pendiente |
| 31 | baja |  | Los efectos se eligen por siglas de tres letras, y un mismo efecto tiene iconos distintos | Ranura de efecto (lista), RACK (e25.png) frente a pantalla principal (e07.png) | pendiente |
| 32 | baja |  | CANCIÓN: 7 herramientas y 6 botones solo con icono, '8 COMPASES' repetido y sin confirmación al colocar | Ficha CANCIÓN (c00-c02.png) | pendiente |
| 33 | baja |  | Pantalla principal: ninguna pestaña marcada como activa, y tocar un pad no cambia el pie | Pestañas PAD/SEC/CANCIÓN/MEZCLA/XY/AJUSTES (e41.png); pie (b01.png) | pendiente, opinion |
| 34 | baja |  | Ficha del pad: cambia de altura en cada pestaña y mezcla inglés y abreviaturas | Mantener pulsado un pad: SONIDO / RECORTE / EL PAD (e01-e03.png) | pendiente |
| 35 | baja |  | Los nombres de los instrumentos de EXTRAS son siglas internas y se cortan en el pad | Pad 05 tras EXTRAS > BAJOS (e16.png, e12_14_bajos.png) | pendiente |
| 36 | detalle |  | AJUSTES: textos con guion ASCII, comillas rectas, ruta en inglés y jerga | AJUSTES > PROYECTOS (e29.png, e30.png, h_m.png) | pendiente |
| 37 | detalle |  | El texto del pie se comprime en horizontal cuando no cabe | Pie con CARGAR armado (e09.png) | pendiente |

<details><summary>Arreglo propuesto de cada uno</summary>

- **ux-flujos#0** Vaciar undoStack y redoStack (y llamar a refrescaNombresDeshacer) al final de loadProject y de newProject. Otra opción es hacer de ABRIR una entrada propia de la pila ('ABRIR Mi tema') con la instantánea completa del proyecto anterior, para que DESHACER vuelva atrás entero y nunca a medias.
- **ux-flujos#1** En paintChopSheetContent, saltar kChopVistaH + Metrics::sm cuando chopVista.isVisible() antes de tomar `planned`, o mejor guardar en resized() el rectángulo de la banda del plan y que el pintor use ese mismo rectángulo. Añadir una regla al banco que compruebe que el texto 'va a pads' no cae dentro de chopVista.getBounds().
- **ux-flujos#2** Añadir el nombre (y el color, si aplica) a PadSet: capturePads copia padName[i] y restorePads usa el nombre de la instantánea. Que la rejilla de pads y la mesa lean el nombre de un único sitio.
- **ux-flujos#3** Llevar una marca de 'proyecto modificado desde el último GUARDAR' (dirty), que pushUndo pone a true y saveProject/loadProject a false. Si está a true, mostrar 'SIN GUARDAR' (o un punto) en la cabecera. Reservar 'A SALVO' para cuando el disco coincide con lo que se ve, o cambiarlo por 'RECUPERABLE' si solo se refiere a la sesión, y corregir la traducción china.
- **ux-flujos#4** Con la marca dirty del hallazgo anterior, armar un aviso distinto: '¿ABRIR sin guardar "Segundo"?' y, junto a él, una tapa 'GUARDAR Y ABRIR'. Si no hay cambios, abrir con un solo toque. Lo mismo para NUEVO ('¿NUEVO sin guardar?'). Es lo que hacen MPC Beats y Koala: guardar, descartar o cancelar.
- **ux-flujos#5** Hacer pushUndo(T("NUEVO")) al principio de newProject con la instantánea completa (pads, nombres, canción, ranuras, mesa), o vaciar la pila como en ABRIR. Lo que no puede pasar es una pila que sobrevive a medias.
- **ux-flujos#6** Fijar la botonera del tour (ATRÁS / SIGUIENTE / SALIR) siempre abajo, y mover solo la tarjeta de texto a la mitad libre. Si hay que moverla, poner SIGUIENTE en el mismo X e Y relativos a la tarjeta y no convertir nunca el sitio de SIGUIENTE en EMPEZAR.
- **ux-flujos#7** En ponTransporte, si no hay dispositivo, no encender el transporte y escribir en el pie 'Sin salida de audio: elige una en AJUSTES > AUDIO', con la tapa AUDIO resaltada. Como mínimo, que REC ON diga 'no graba: sin audio'.
- **ux-flujos#8** Poner en la pantalla principal una entrada directa a 'SAMPLEAR' (mic o remuestreo) que pida el pad destino, y renombrar el REC del transporte a 'REC PASOS' o 'GRABAR PATRÓN'. En el pie inicial, añadir 'Mantén un pad para editarlo'.
- **ux-flujos#9** Recalcular chopCortes en el onClick de chopSafeButton (llamar a lo mismo que los botones de trozos). Con 0 destinos, escribir dentro del propio botón 'SIN PADS LIBRES'.
- **ux-flujos#10** Pintar en gris oscuro, rayadas o sin pintar las columnas más allá de LARGO, y en mouseDown fuera del largo escribir 'El patrón tiene 1 compás: sube LARGO'. Elegir el zoom por defecto para que entren exactamente 16 pasos.
- **ux-flujos#11** Abrir siempre en PADS mientras no haya pads enrutados. En CANALES, pintar los canales vacíos atenuados con '(sin pads)'. Convertir la tapa en dos pestañas 'PADS | CANALES' con la activa marcada, como el resto de fichas.
- **ux-flujos#12** Al abrir XY, elegir como xyFx el primer efecto puesto. Si fxEncendido pero su canal tiene 0 pads, decir 'SIN PADS' en vez de 'SUENA'.
- **ux-flujos#13** Con la carpeta vacía, mostrar 'Aún no tienes muestras: prueba EXTRAS, CARGAR KIT o SISTEMA' con esas tres tapas resaltadas. Ocultar el campo de nombre del FileBrowser. En EXTRAS llegado desde un pad, plegar el selector de pad en una línea 'Pad 5 · cambiar' y subir la instrucción arriba.
- **ux-flujos#14** Dejar fija la cabecera (título y ×) fuera del viewport, como ya pasa en SEC.
- **ux-flujos#15** Dar a la lista una altura fija con scroll propio y anclar la botonera al pie de la ficha. Desactivar BORRAR y ABRIR sin fila elegida.
- **ux-flujos#18** Dejar la cifra técnica en AJUSTES > AUDIO o en un registro, y en el pie decir 'Sesión recuperada - Mi tema. La última vez la app se quedó colgada' en lenguaje llano. Si hace falta, ofrecer una tapa 'Enviar informe'.
- **ux-flujos#19** status 'Patrón P1 vaciado - DESHACER para volver', como ya hace AUTO CHOP.
- **ux-flujos#20** No cerrar la ficha activa en performUndo. Solo refrescarla.
- **ux-flujos#22** Rótulos 'ZOOM -/+' o '16 / 8 PASOS', y 'FILAS 1-16' con una flecha. Barra de posición bajo la rejilla cuando no caben todos los pasos.
- **ux-flujos#23** Poner el nombre corto del pad junto al número de fila (o al mantener pulsada la etiqueta) y en el selector.
- **ux-flujos#24** Nombrar las notas en absoluto (C3/C4 según la raíz de la muestra) o marcar la raíz como 'RAÍZ'. Centrar la vista inicial en la raíz con una octava por encima y otra por debajo. Resaltar la herramienta activa. Añadir el nombre del pad a la cabecera.
- **ux-flujos#25** Contar antes de exportar los pads que suenan en la fuente elegida ('pistas: 5 + master'). Mostrar el nombre de carpeta saneado tal y como quedará. Usar la misma base para el master ('Mi-tema_master.wav') en vez de un sufijo numérico.
- **ux-flujos#26** Separar la fuente de exportación del modo de PLAY (variable propia) o decirlo en la ficha: 'También cambia PLAY a CANCIÓN'.
- **ux-flujos#28** Reducir el tour a 6-8 pasos que pidan una acción real (tocar un pad, escribir 4 pasos, PLAY, exportar), como el primer arranque de Koala. Unificar los términos con la interfaz y quitar los pasos que señalan cosas inexistentes.
- **ux-flujos#29** Sin efecto en la ranura activa, pintar en CONTROL 'Sin efecto: toca + para poner uno'. Sincronizar CONTROL con la ranura seleccionada en applyState y newProject.
- **ux-flujos#30** Con 0 pads, mostrar en la cabecera de efectos 'sin pads: toca para enviar el pad actual' y, en RACK, una tapa 'METER PAD 01 EN ESTE CANAL'.
- **ux-flujos#16** En armConfirm, escribir en el pie 'Toca otra vez para confirmar (5 s)' y, si se puede, pintar una barra de cuenta atrás en la tapa armada.
- **ux-flujos#17** Separar 'nombre para guardar' (siempre el abierto, editable) de la fila elegida para ABRIR/BORRAR. No copiar la selección al campo.
- **ux-flujos#21** Nombrar en pushUndo el objeto y el número: '4 PASOS', 'BLOQUE P1', 'PROYECTO NUEVO'.
- **ux-flujos#27** Una sola barra dentro de la ficha, con el × siempre accesible (o 'CANCELAR'). Añadir debajo de las carpetas la frase 'abre la carpeta en el gestor'.
- **ux-flujos#31** Nombre completo bajo la sigla (FILTRO, PASO ALTO, FORMANTE…). El mismo icono en todas las vistas. 'RANURA 1 · CANAL 01' y 'MODIFICADO' en vez de 'MOVIDO'.
- **ux-flujos#32** Rótulo bajo cada icono (como ya se hace en PIANO con LÁPIZ/GOMA/TIJERAS), quitar el duplicado, pie 'P1 en compás 3' y etiqueta del bloque dentro del bloque.
- **ux-flujos#33** Marcar PAD como activa en la vista principal. En el pie, 'PAD 01 · KICK' al tocar. DESHACER en el color de las tapas normales.
- **ux-flujos#34** Altura fija para la ficha (la de la pestaña más alta). 'ENTRADA/SALIDA SUAVE', 'ESTÉREO', '48 kHz', 'AUTOCORTE', 'RESONANCIA', coma decimal según el idioma.
- **ux-flujos#35** Nombre visible corto y en castellano ('BAJO SIERRA'). Marcar la categoría elegida. Si hay más variantes, mostrarlas en lista.
- **ux-flujos#36** Raya o punto medio, comillas «», 'Música/ZATI/Proyectos' en la interfaz y '[63 pads]' en '63 pads con sonido'.
- **ux-flujos#37** Frase más corta ('Toca un pad para cargar') o dos líneas, sin escalado horizontal.

</details>

## visual

**Lo que esta bien:**

- NOTA DE RUTAS: todas las rutas 'visual/...' de este informe cuelgan de /tmp/claude-0/-home-user/5af306bb-dc23-56c1-9323-8d167bedfd27/scratchpad/audit/. Las fotos se reproducen con 'visual/shot.sh TAMAÑO CARCASA IDIOMA FICHA' (CARA es la ficha vacía; carcasas 0 PAPEL, 1 GRAFITO, 2 ACERO, 3 LACA). Las coordenadas están en px de la foto 2x: se divide entre 2 para tener pt.
- El nombre del pad ya cumple contraste en LACA: CONGA mide 5.35:1, CRASH en blanco sobre su banda (111,96,44) 5.10:1, la cabecera 4.83:1 y la línea de estado 6.70:1. La feria midió 1.8:1, así que este arreglo es real.
- Tildes y eñes arregladas en casi todo el catálogo: ESPAÑOL, MANTÉN, CAÍDA (ficha del pad), GUÍA, SÍ, BÚFER, ÓRGANOS, ACORDEÓN. La única regresión encontrada es CAIDA en la fila de bloqueos a 412.
- La rejilla de pasos ya no salta al primer toque: en secp sigue en y=470, igual que en sec. Las pestañas de las fichas de pad y de SEC se quedan fijas en y=260.
- La identidad de máquina es coherente en las 4 carcasas: franja de 8 colores, marco con esquinas, pads de color estilo MPC y rótulos tipográficos con espaciado. En ACERO y PAPEL la composición no se rompe.
- Las rutas crudas /tmp/... de la feria han desaparecido. Ahora se ve ~/Music/ZATI/..., y en inglés las carcasas se traducen (PAPER/GRAPHITE/STEEL/LACQUER, visual/shots/393x851/s3-en/asp.png).
- El grosor de trazo de los iconos de la ranura y de las herramientas es uniforme, y la cruz de cerrar sale de una sola constante (ZatiLookAndFeel.h:644).
- En árabe ya se espejan el panel CONTROL y los bancos A-D (visual/shots/393x851/s3-ar/cara.png). Los iconos de los botones pasan al lado derecho del texto.
- La mesa de 16 filas entra sin barra a 393 y 412, y los faders dejan la cifra siempre dentro de la fila (visual/shots/412x915/s3-es/mix.png).

| # | gravedad | v | hallazgo | donde | estado |
|---|---|---|---|---|---|
| 0 | alta | si | Cifra dB de la mesa ilegible en PAPEL y GRAFITO (1.96 a 2.36:1) | MEZCLA (mix), fila de pad y MASTER. ZatiLookAndFeel.h:1926-1934 | arreglado (7499333) |
| 1 | alta | si | La pista de los deslizadores no se ve: 1.00 a 1.15:1 (WCAG 1.4.11 pide 3:1) | SEC LARGO/CORTE/TEMPO, CANCIÓN y en general todo deslizador lineal simple. ZatiLookAndFeel.h:1356 y el dibujo de la pista hacia la línea 195 | pendiente |
| 2 | alta | si | Colores por defecto de JUCE (V4 oscuro) que no cambian con la carcasa: selector de ruta, botón subir y cajas NOMBRE | CARGAR (browse), CARPETA DE EXPORTAR (browsedir), NOMBRE en CANAL (cannom/cannomd) y en preset/preseteq. ZatiLookAndFeel.h:1365-1385; MainCo | pendiente |
| 3 | alta |  | Títulos de caja por debajo de 4.5:1 en las tres carcasas (la feria lo señaló y sigue igual) | Rótulos de caja de todas las fichas (PATRÓN, LARGO, NOTA, GOLPE, CUENTA, MONITOR, PRUEBAS...). Metrics::alfaSeccion=0.70f en ZatiLookAndFeel | pendiente |
| 4 | alta |  | La ruta de CARPETA en PROYECTOS sigue ilegible: 2.25 a 2.48:1 (la feria midió 2.09) | AJUSTES · PROYECTOS (proj), línea 'CARPETA ~/Music/ZATI/Projects' | pendiente |
| 5 | alta |  | Objetivos táctiles muy por debajo de 44 pt / 48 dp (la feria lo señaló y sigue igual) | Cara: pestañas, transporte y bancos. SEC: rejilla. Mesa: M/S/ST | pendiente |
| 6 | alta |  | La caja inferior se corta contra el filo de la ficha (MIDI: la feria lo señaló y sigue igual) | midi (LEER LO QUE ENTRA COMO), pads (GRUPO/MODO), proj, expo | pendiente |
| 8 | alta |  | Guía: el botón SIGUIENTE cambia de sentido y SALTAR aparece en el último paso (la feria lo señaló y sigue igual) | Guía, pasos 4/24 (tour3), 11/24 (tour10) y 24/24 (tourf) | pendiente |
| 7 | media |  | En PAPEL los nombres de los pads no llegan a 4.5:1 | Cara, carcasa PAPEL, nombres de los 16 pads | pendiente |
| 9 | media |  | Guía: el marco de resaltado atraviesa controles y el número tapa rótulos | tourf (24/24), tour10 (11/24), tour6, tourpuerta, tour1 | pendiente |
| 10 | media |  | SEC: la columna 16 queda recortada a 393 | SEC (sec, secp, secsel). StepGrid.h:115-130 y 357 | pendiente |
| 11 | media |  | CANCIÓN: números de compás debajo de las etiquetas de clip (texto encima de texto) | CANCIÓN (songsel, llena-song, songa) | pendiente |
| 12 | media |  | La altura de la ficha cambia al cambiar de pestaña | Pad (SONIDO/RECORTE/EL PAD), SEC (PASOS/PATRÓN), AJUSTES (AUDIO/ASPECTO) | pendiente |
| 13 | media |  | La × de cerrar mide 3.5 pt dentro de un botón de 38x26 pt (la feria lo señaló y sigue igual) | Todas las fichas. ZatiLookAndFeel.h:644 Metrics::cruz = "\xc3\x97" | pendiente |
| 14 | media |  | Al aparecer la barra de selección, la rejilla baja 48 pt; el piano la pone debajo | secsel, songsel, pianosel | pendiente |
| 15 | media |  | Botones de la guía y pestañas: política de iconos distinta según el tamaño de pantalla | Cara, SEC TEMPO, piano, CANCIÓN, a 360 frente a 393 | pendiente |
| 16 | media |  | 'RECIBIR NOTAS D': el rótulo se corta al ancho del botón a 360 | AJUSTES · MIDI a 360x640. MainComponent_Paint.cpp:1754 | pendiente |
| 17 | media |  | CAIDA sin tilde en la fila de bloqueos de SEC (sale a 412) | SEC a 412x915, fila ATAQUE/CAIDA/INICIO/PAN. MainComponent_Layout.cpp:7646 | pendiente |
| 18 | media |  | Títulos de sección desplazados a la izquierda por el espaciado que se suma después del último glifo | Cabeceras PADS, CONTROL, EFECTOS (cara, songm). ZatiLookAndFeel.h:442 | pendiente |
| 19 | media |  | Chips de número de la mesa: blanco sobre amarillo 1.73:1 | MEZCLA/CANALES (mix, mixc, llena-mix). MainComponent_Paint.cpp:1880-1889 | pendiente |
| 21 | media |  | Significado del color sin resolver: rojo para seleccionar y deshacer, ámbar para pestaña y acción (la feria lo señaló y sigue igual) | CANCIÓN (P1 en rojo), DESHACER (rojo), proj (GUARDAR y PROYECTOS en ámbar; BORRAR/ABRIR activos) | pendiente |
| 22 | media |  | CANCIÓN: herramientas solo con icono (en el piano llevan texto) y fila desalineada 9 pt | CANCIÓN (song), frente al piano | pendiente |
| 23 | media |  | Efectos con siglas de 3 letras en la ranura (la feria lo señaló y sigue igual) | RANURA (ranura, ranural) | pendiente |
| 24 | media |  | CORTE con cuatro significados y REV con dos | SEC (CORTE del paso), pads (CORTE=ABIERTO del filtro), barra de selección (CORTE), pad3 (AUTOCUT); RECORTE (REV) frente a efectos (REV) | pendiente, opinion |
| 25 | media |  | Formatos de número y unidad incoherentes | Cara, SEC, pad, rack, canal, recorte, mesa a 360, exportar | pendiente |
| 26 | media |  | Títulos con separadores ausentes o con espaciado desigual | midf ('MIDIPATRÓN 1PAD A01'), pick ('PAD 1  KICK'), SEC/piano. MainComponentInterno.h:577-594; MainComponent_Paint.cpp:3170-3175, 2461, 2707 | pendiente |
| 27 | media |  | Nombres de pad truncados a 5-8 caracteres con proyecto real | Cara y MEZCLA con proyecto lleno (llena, llena-mix) | pendiente, opinion |
| 28 | media |  | GRAFITO: la sombra de la ficha sale clara (un halo beige) en vez de oscura | Todas las fichas en GRAFITO (sec, pads, proj...) | pendiente |
| 29 | media |  | AUTO CHOP: CORTAR desactivado invisible en PAPEL y gris fuera de paleta en LACA | chop. MainComponent.cpp:1697 (styleButton(chopGoButton, red)) y 12496 | pendiente |
| 34 | media |  | Recuadro negro vacío sin rótulo en CONTROL (la feria lo señaló y sigue igual) | Cara, CONTROL (x 37-260, y 448-620) | pendiente |
| 35 | media |  | 'EFECTOS · CANAL 01 · 0 PADS' al arrancar: el ruteo sigue sin entenderse | Cara. MainComponent.cpp:14716-14724 (textoCosturaEfectos) | pendiente |
| 43 | media |  | Árabe: las filas de pestañas y transporte siguen sin espejarse (la feria lo señaló) | Cara en árabe | pendiente |
| 44 | media |  | Inglés en la interfaz en español | EXTRAS (instd), vst, RECORTE, rutas | pendiente |
| 47 | media |  | Piano: unos 36 pt muertos encima de la rejilla y notas escritas fuera de la vista | piano, pianod, llena-piano | pendiente |
| 20 | baja |  | Pads: número oscuro y nombre claro en el mismo pad | Cara LACA, pads 03, 04, 11, 12. PadButton.h:359 | pendiente, opinion |
| 30 | baja |  | Teclado de vst: costura vertical en cada tecla negra | vst, vstm. Teclado.h:94 | pendiente |
| 31 | baja |  | Mesa: el 0 dB cae en sitios distintos y la cifra va en sitios distintos | MEZCLA (pads/canales/master) | pendiente |
| 32 | baja |  | Canales ordenados de abajo arriba en el selector y en el rack, y de arriba abajo en la mesa | Selector de canal, rack, frente a CANALES (mixc) | pendiente, opinion |
| 33 | baja |  | Acento de los mandos rojo en la cara y ámbar en las fichas | CONTROL de la cara frente a pads/vst/mandos | pendiente, opinion |
| 36 | baja |  | Iconos rellenos mezclados con iconos de contorno | ranura, cara (PLAY), inst (COLCHONES), midi (RECIBIR frente a MANDAR), cannom, rackf | pendiente |
| 37 | baja |  | Relleno interior escaso: el texto toca el filo de su caja | vst (preset '14/16 BRIGHT GT'), PRESETS, GUARDAR | pendiente |
| 38 | baja |  | Botones de preset de EQ con el texto pegado arriba | preseteq (EQ · MOVIDO) | pendiente, opinion |
| 39 | baja |  | AJUSTES: la × va en la fila de pestañas y se mueve 8 pt en EXTRAS | set/asp/proj/midi frente a las demás fichas; inst | pendiente |
| 40 | baja |  | Estilos distintos para el título de caja | MIDI ('MANDAR NOTAS A') frente a SEC ('PATRÓN', 'NOTA') | pendiente |
| 41 | baja |  | Bordes de cajas hermanas desalineados | SEC (NOTA/GOLPE), midf (EXPORTAR/IMPORTAR) | pendiente |
| 42 | baja |  | Etiquetas en minúscula mezcladas con estados en mayúscula | Línea de estado, exportar, proyectos, SEC | pendiente, opinion |
| 45 | baja |  | Mensajes de vacío poco útiles (la feria lo señaló y sigue igual) | browse ('prueba en otra carpeta'), browsedir ('vacía, pero vale') | pendiente, opinion |
| 46 | baja |  | El selector de carpeta de exportar enseña una caja 'nombre:' que no sirve | browsedir (CARPETA DE EXPORTAR) | pendiente |
| 48 | baja |  | Paneles de la guía: espacio muerto y panel de filo a filo | tour, tour1, tour3, tour10, tourf | pendiente, opinion |
| 52 | baja |  | mandos: rótulos numerados pegados y media fila vacía | mandos | pendiente |
| 53 | baja |  | RESO en la cara y RESON en la ficha del pad | Cara frente a pads/platopad. Lang.cpp:309 y 843; MainComponent.cpp:5709 y 10004 | pendiente |
| 55 | baja |  | A 360x640 el número del pad baja a unos 6 pt, más pequeño que el nombre | Cara a 360x640 | pendiente, opinion |
| 56 | baja |  | El contenido de SEC cambia según el tamaño de pantalla | SEC a 360, 393 y 412 | pendiente, opinion |
| 59 | baja |  | La cabecera 'BORRADOR · A SALVO' sigue juntando dos estados | Cabecera (salvo) | pendiente, opinion |
| 49 | detalle |  | Etiqueta MOVIMIENTO apretada y columnas desiguales en ASPECTO | asp / lang | pendiente |
| 50 | detalle |  | GESTOS: la columna de descripciones no está alineada | gest | pendiente |
| 51 | detalle |  | Manual: viñetas centradas en elementos de dos líneas y botones con fuentes distintas | manual | pendiente |
| 54 | detalle |  | EXTRAS: nombre del pack descentrado, un pad con icono y candado diminuto | inst, instd, instg | pendiente |
| 57 | detalle |  | Cosas que asoman bajo los filos de las fichas | Todas las fichas (SIN AUDIO cortado por arriba), mandos, asp, rack | pendiente, opinion |
| 58 | detalle |  | RECORTE, EQ y XY: detalles de acabado | pad2 (RECORTE), eq, xy | pendiente |
| 60 | detalle |  | pad3: muestras de color sin título y EFECTOS que agrupa cosas que no son efectos | pad3 (EL PAD) | pendiente, opinion |

<details><summary>Arreglo propuesto de cada uno</summary>

- **visual#0** Calcular 'fondo' con el mismo color que se pinta: groove(0.55f).overlaidWith(relleno) para el tramo lleno y groove(0.55f) para el vacío. Después pasar bestOn sobre ese fondo. Si ninguna tinta llega a 4.5:1, poner la cifra en un chip screenBg. Añadir una regla en canales.py que mida el contraste de la cifra en las 4 carcasas.
- **visual#1** Pintar la pista con groove(0.55f), como el fader de la mesa (línea 1854), o con ink.withAlpha(0.35f). Exigir 3:1 contra la caja en las 4 carcasas.
- **visual#2** Fijar en ZatiLookAndFeel TextEditor::backgroundColourId/textColourId/outlineColourId y los del botón de subir, y volver a aplicarlos en cada cambio de carcasa (sendLookAndFeelChange al navegador). Dar a canNomBox y presetNombreBox la misma fuente, justificación y fondo screenBg que projNameBox.
- **visual#3** No bajar la tinta con alfa sobre las cajas claras. Usar ink a 0.92 o un token 'rotuloCaja' calculado con bestOn contra panel, con un mínimo de 4.5:1 comprobado por carcasa en expo.py.
- **visual#4** Pintar el rótulo y la ruta con ink.withAlpha(0.9) como mínimo, o con la tinta de los valores. Si se quiere un tono secundario, que esté calculado para 4.5:1.
- **visual#5** Pestañas y transporte con un mínimo de 44 pt (se pueden juntar en una barra inferior). Rejilla de 8 pasos por página (1-8 / 9-16) cuando la celda baje de 32 pt. M/S/ST de la mesa con un área táctil de 44 pt, aunque el dibujo siga siendo más pequeño.
- **visual#6** Calcular el alto de la ficha con el contenido más el margen inferior de la caja (Metrics::aireTapa), o hacer que la ficha crezca hasta el espacio disponible. Añadir una regla en expo.py: ninguna caja puede tocar el filo inferior de su ficha.
- **visual#8** Dejar SIGUIENTE siempre a la derecha y que nunca cierre la guía. Poner 'Terminar' solo en el 24/24 y con otro estilo. En el paso 4 mover VER MÁS a la derecha, con EMPEZAR como secundario a la izquierda. Quitar SALTAR del último paso.
- **visual#7** Usar en PAPEL la misma banda inferior oscurecida que en LACA, o una banda clara (chassisTop a 0.8) con tinta oscura. Así el nombre se mide contra la banda y no contra el color del pad.
- **visual#9** Calcular el marco como la unión de los límites de los componentes resaltados más el margen. Colocar la chapa fuera del marco, en la esquina libre más cercana, y comprobar en tour.py que la chapa no corta ningún texto.
- **visual#10** Calcular la celda con el ancho disponible entre 16 (o entre numCols) y no con el alto. Si no caben, mostrar una barra de desplazamiento horizontal visible o paginar 1-8 / 9-16.
- **visual#11** Sacar los números de compás a una regla propia encima del carril 1 (unos 12 pt), como en el piano (1.1 1.2...), y no pintarlos dentro de los carriles. Dar a la etiqueta del clip una tinta calculada con bestOn contra el color de selección.
- **visual#12** Fijar el alto de la ficha por familia (el máximo de sus pestañas) y desplazar el contenido dentro. Una ficha no debe encoger bajo el dedo.
- **visual#13** Dibujar la × como icono de trazo en Iconos.h, con el grosor kBanda y unos 12-14 pt dentro del botón, igual que los demás iconos.
- **visual#14** Reservar siempre el alto de la barra (desactivada cuando no hay selección) o ponerla abajo en las tres fichas, como en el piano.
- **visual#15** Una sola regla: el icono se cae solo por falta de ancho, en todos los botones a la vez y en el mismo orden (primero los secundarios). Ningún botón debería ganar icono en una pantalla más pequeña.
- **visual#16** Dar al rótulo el ancho de la caja (o de la fila), no el de midiInBtn. Hacer lo mismo con MANDAR NOTAS A (midiOutBtn), aunque hoy quepa.
- **visual#17** Usar la clave 'CAIDA|x' (o añadir 'CAIDA' a Lang.cpp) y pasar los cuatro nombres por T(). Añadir 'CAIDA' a la lista de prohibidas de lang.py.
- **visual#18** Al centrar, restar un tracking*altura del ancho medido, o dibujar con un GlyphArrangement y recortar el avance final.
- **visual#19** Pasar a bestOn una tinta oscura fija (el negro del chasis, ZatiColours::screenBg) en lugar de ink, que en LACA es crema.
- **visual#21** Usar tokens semánticos: acento ámbar solo para la selección, un relleno o borde distinto para la acción principal y rojo solo para destruir. DESHACER en neutro. Desactivar BORRAR/ABRIR sin proyectos y marcar la pestaña de la ficha abierta.
- **visual#22** Poner rótulo a las herramientas como en el piano (o un bocadillo al pulsar largo). Alinear el borde derecho de la fila con los demás. Usar un icono distinto para mover (cruz de flechas) que no choque con AYUDA.
- **visual#23** Nombre completo bajo el icono ('Filtro', 'Paso alto', 'Phaser', 'Trémolo'...) en una lista de 2 columnas, y el mismo nombre en el título de mandos.
- **visual#24** Usar CORTAR para el portapapeles, CHOKE→'CALLA A' o 'SILENCIA' en el paso, FILTRO/FRECUENCIA en el pad, INVERTIR en RECORTE y REVERB en efectos. Comprobarlo en el catálogo con una prueba de colisiones.
- **visual#25** Una única función de formato por magnitud (pct, bpm, dB, s, canal) con espacio fino antes de la unidad y la misma caja (mayúsculas en valores enumerados), usada en todas partes.
- **visual#26** Hacer que campoAcampo inserte el separador canónico ' · ' y que todas las fichas pasen solo los campos. Alinear el título a la misma x que las demás fichas.
- **visual#27** Partir el nombre en 2 líneas en los pads (sobra alto bajo la onda), cambiar '_' por espacio al mostrar y en la mesa dar al nombre el ancho que deja la cifra dB. Como alternativa, una fuente condensada para nombres de más de 8 caracteres.
- **visual#28** Pintar la sombra con negro a alfa fija y no con una tinta de la carcasa (en GRAFITO 'ink' es clara).
- **visual#29** Desactivado = el estilo común de desactivado de la carcasa, más una línea 'Elige un pad con sonido' encima. Que el fondo del visor use screenBg de la carcasa.
- **visual#34** Llenarlo con el XY/onda que representa o con un texto de estado ('sin efecto'), o eliminarlo y repartir los tres mandos a lo ancho.
- **visual#35** Si no hay pads en el canal, decir dónde va el efecto ('EFECTOS · PAD 1 → CANAL 01') o mostrar 'SIN PADS EN ESTE CANAL' con un botón ASIGNAR.
- **visual#43** Usar Lang::takeStart también en el reparto de pestañas y transporte. Envolver los números con marcas LRI/PDI o con un separador con espacios fijos.
- **visual#44** Pasar los nombres de instrumento y preset por el catálogo (BAJO SUB, PIANO ELÉCTRICO, CUERDAS...) y mostrar las carpetas con su nombre traducido ('Proyectos') aunque en disco sigan en inglés.
- **visual#47** Al abrir, centrar la vista en el rango de las notas del patrón. Igualar el hueco superior con SEC. Dar a las notas un ancho mínimo dibujado de 6 pt. Usar una mono sin cero tachado en los rótulos de nota.
- **visual#20** El número debe usar la misma tinta que el nombre (crema sobre una banda o sombra) en todos los pads. bestOn solo debería decidir cuando no hay banda.
- **visual#30** Pintar las negras con un color opaco (groove(0.85f) sobre chassisTop, ya compuesto) o dibujar las blancas sin borde bajo las negras.
- **visual#31** La misma escala (0 dB en el mismo x) para pad, canal y master, y la cifra siempre en el mismo sitio. PADS/CANALES como selector de dos segmentos.
- **visual#32** Usar el orden de rejilla MPC solo para pads. Las listas de canales siempre en 01→16 de arriba abajo.
- **visual#33** Un único token 'agujaMando'. Si el rojo de la cara significa 'mando de efecto', decirlo con un rótulo y no solo con color.
- **visual#36** Pasar todos a trazo de grosor kBanda, o definir que el relleno significa 'activo' y aplicarlo así en todos.
- **visual#37** Exigir Metrics::aireTapa a cada lado (regla PEGADO de expo.py también para cajas de valor) y truncar con … el nombre del preset.
- **visual#38** Centrar en vertical, o usar ese hueco para la mini curva del preset (que es lo que el hueco parece pedir).
- **visual#39** La × siempre en la esquina superior derecha de la fila del título, en coordenadas fijas, con la barra de desplazamiento por dentro del área de contenido.
- **visual#40** Un único estilo de rótulo de caja (Metrics::fSeccion con tracking) para todas las cajas.
- **visual#41** Todas las cajas de una ficha con el mismo inset horizontal (Metrics::margenFicha).
- **visual#42** Elegir una caja: frases en minúscula y rótulos en mayúscula, nunca mezclados en la misma línea. Reservar a la izquierda el ancho de DESHACER para que el estado no se mueva.
- **visual#45** 'No hay sonidos en esta carpeta. Abre EXTRAS o elige otra carpeta', con los dos botones dentro del hueco vacío.
- **visual#46** Ocultar el filenameBox en modo carpeta (FileBrowserComponent sin canSelectFiles) o sustituirlo por la ruta elegida en solo lectura.
- **visual#48** Alto del panel = texto + botones + aire, con 16 pt de margen lateral y esquinas como las fichas.
- **visual#52** Quitar el número del rótulo o separarlo con más aire, repartir los mandos en filas completas y usar el mismo nombre en el título y en el estado.
- **visual#53** Una sola etiqueta (RESONANCIA si cabe, si no RESO) y revisar las abreviaturas de los sintes con nombre completo y unidad.
- **visual#55** Mantener la jerarquía: número al menos al tamaño del nombre, o una sola línea '13 CONGA' con el número en negrita.
- **visual#56** Mantener las mismas funciones en todos los tamaños y mover lo que no quepa a una pestaña o a un acordeón ('MÁS'), no a tamaños concretos.
- **visual#59** Un solo estado: 'BORRADOR (autoguardado)' o un icono de nube con check junto al nombre.
- **visual#49** Columna de rótulos de ancho fijo (el del rótulo más largo) y rejilla de 4 columnas común, con SÍ/NO ocupando 2+2.
- **visual#50** Tabla de dos columnas con la x de la segunda fija y equilibrado de líneas (sin una palabra sola en la última línea).
- **visual#51** Alinear la viñeta con la línea base de la primera línea y usar el mismo estilo de botón en los dos.
- **visual#54** Centrar el nombre del pack entre los botones. Mostrar el icono en todos los pads con instrumento o en ninguno. Candado de 10-12 pt o la palabra 'BLOQUEADO'.
- **visual#57** Bajar más la opacidad del fondo (o desenfocarlo) y empezar la ficha justo bajo la franja de color para no cortar texto.
- **visual#58** Sacar el zoom a una barra fuera de la onda, poner el punto de estante sobre la curva (ganancia en la frecuencia de esquina) y darle al XY el marco de ficha común.
- **visual#60** Añadir el título COLOR a la fila de muestras y separar MODO DE DISPARO (PIANO, 16 NIVELES) de EFECTOS.

</details>

## producto

**Lo que esta bien:**

- Los 10 hallazgos de la feria, comprobados uno a uno en el binario actual: 5 arreglados del todo (crash al BORRAR, ruteo visible, acentos, cabecera «BORRADOR · A SALVO», rejilla de pasos estable) y otros 4 arreglados en parte (exportación, contraste y áreas táctiles, LUFS y PISTAS, rutas). El 10 (cabezal durante PLAY) no se pudo comprobar porque no hay dispositivo de audio. BORRAR un proyecto que no está abierto ya no hace caer la app: 3 de 3 pruebas sin crash, 2 de ellas con ASan y sin ningún informe (producto/t17s.png, t22s.png, t23s.png, tel22/app.log).
- Exportar: la duración anunciada coincide con el archivo (2.5 s = 2.0 + 0.5, medido con ffprobe), no hay clic al final (la última muestra distinta de cero está en 2.2856 s), el selector PATRÓN/CANCIÓN está a la vista (exp4s.png) y al terminar se muestran los LUFS.
- PISTAS ya no escribe archivos en silencio: solo genera los pads que tienen contenido.
- Contraste de texto de al menos 4.5:1 (WCAG AA) y áreas táctiles de 40-48 dp en los controles principales, según el JSON de ZATI_AUDIT.
- Las raíces del banco D forman una pentatónica de La menor (A, C, D, E, G): los sonidos tonales encajan entre sí sin que el usuario tenga que afinarlos.
- 4 idiomas y 4 skins. El menú tiene 30 efectos, más que el SP-404 MKII en cantidad bruta.
- La bienvenida es corta (4 pasos) y deja elegir entre EMPEZAR o VER MÁS. La portada tiene una marca reconocible: crema sobre verde azulado.
- Ahora la confirmación avisa de que caduca: «Sin confirmar: no se ha hecho nada» (MainComponent.cpp:22266-22278).
- El ruteo de efectos se explica en pantalla: «DISTORSIÓN en CANAL 01: entran los 64 pads» (t07s.png).

| # | gravedad | v | hallazgo | donde | estado |
|---|---|---|---|---|---|
| 0 | alta | si | Las capturas de la tienda muestran otra app: kit distinto, textos viejos y cabecera antigua | /home/user/juce-android-bench/store/*.png (store-es-telefono.png, store-idiomas.png) | pendiente |
| 1 | alta | si | No hay patrón de demostración: al pulsar PLAY la primera vez no suena nada | Primer arranque, pantalla principal y botón PLAY. state.xml de un proyecto nuevo. | pendiente |
| 2 | alta | si | KICK y los graves del kit casi no se oyen por el altavoz del móvil | Kit de fábrica: Source/Kits.h (KICK, BD, SUB BS, BOOM, THUMP, BASS, IMPACT, LC, LT, TOM LO) | pendiente |
| 3 | alta |  | La cola de la exportación contiene el golpe del compás siguiente | EXPORTAR > PATRÓN, archivo ZATI.wav | pendiente |
| 4 | media |  | El kit y el master salen muy bajos y no hay opción de normalizar | Kit de fábrica (gananciaSonoridad en Source/Kits.h) y EXPORTAR | pendiente |
| 5 | media |  | 17 sonidos se cortan en seco a los 3.0 s cuando aún se oyen | /home/user/juce-android-bench/Source/Kits.h:1053 | pendiente |
| 6 | media |  | En el tour, el botón SIGUIENTE cambia de sitio y en el paso 4 cierra el tour | Tour de bienvenida, pasos 1-4 (Source/Lang.cpp:1335-1420 para los textos) | pendiente |
| 7 | media |  | El menú de efectos usa códigos de 3 letras sin explicar | Menú de efectos del slot (t06s.png) | pendiente |
| 8 | media |  | El panel CONTROL conserva los valores del proyecto anterior después de NUEVO | PROYECTOS > NUEVO, pestaña CONTROL | pendiente |
| 9 | media |  | Después de GUARDAR con un nombre nuevo, el campo y la selección vuelven al proyecto anterior | PROYECTOS > GUARDAR | pendiente |
| 10 | media |  | La confirmación caduca a los 3 s fijos y no se puede ajustar | /home/user/juce-android-bench/Source/MainComponent.cpp:22266-22278 (confirmMs) | pendiente |
| 11 | media |  | Las celdas de paso miden 17 dp con un paso de 19 dp, muy por debajo de 48 dp | Hoja SEC, rejilla de pasos (sec4.png) | pendiente |
| 12 | media |  | La ficha de tienda no tiene textos: ni título, ni descripción corta, ni descripción larga | /home/user/juce-android-bench/store/ | pendiente |
| 13 | media |  | El feature graphic solo existe en español, escribe «ARTiFACTS» y cuenta los efectos distinto que la documentación | /home/user/juce-android-bench/Source/StoreArt.h, README, ZATI.md | pendiente |
| 14 | media |  | El icono no es adaptativo y no coincide con la marca de la app | Zati.jucer (smallIcon/bigIcon = ci/icon.png), entrega/Zati-release.apk (apk-iconos.png) | pendiente |
| 15 | media |  | El diálogo de exportar anuncia 64 pistas y escribe 3 | EXPORTAR > PISTAS | pendiente |
| 16 | media |  | NUEVO vacía los 64 pads en lugar de empezar con el kit de fábrica | PROYECTOS > NUEVO (t14s.png) | pendiente, opinion |
| 17 | media |  | No hay Ableton Link ni reloj MIDI | Source/ (grep sin resultados para Link ni MIDI clock) | pendiente, opinion |
| 18 | baja |  | El cabezal durante PLAY no se pudo comprobar y la pantalla principal no muestra la posición | Source/StepGrid.h:489-509 y pantalla principal | pendiente |
| 19 | baja |  | Con la lista vacía, BORRAR y ABRIR siguen activos | PROYECTOS con 0 proyectos (t25.png, t26.png) | pendiente |
| 20 | baja |  | Los botones de PROYECTOS bajan cada vez que crece la lista | PROYECTOS | pendiente |
| 21 | baja |  | Las rutas de destino siguen siendo poco claras en el diálogo de exportar | EXPORTAR y PROYECTOS (MainComponentInterno.h:637-650 rutaCorta) | pendiente |
| 22 | baja |  | Los nombres de los archivos exportados no son coherentes | Carpeta Exports | pendiente |
| 23 | baja |  | Elegir CANCIÓN en EXPORTAR cambia el botón CICLO de la pantalla principal a ARREGLO | EXPORTAR > CANCIÓN (pad1s.png) | pendiente |
| 24 | baja |  | El aviso «Preparando sonidos 11.0 s» tapa el título de la hoja SEC | Hoja SEC al arrancar (sec1s.png) | pendiente |
| 25 | baja |  | El mensaje de arranque tras un cierre forzado usa jerga interna | Arranque tras cerrar la app con SIGTERM (t19s.png, sonando2.png) | pendiente |
| 26 | baja |  | La tarjeta del tour tapa pads y sus números se pisan con otros controles | Tour, pasos 1-4 (t01.png, tour02s.png-tour04s.png) | pendiente |
| 27 | baja |  | El kit de fábrica tiene detalles de acabado técnico | Source/Kits.h, kit exportado (producto/kit/tabla.txt) | pendiente |
| 28 | detalle |  | El banco B usa abreviaturas mientras el banco A usa nombres completos | Pads del banco B y documentación ZATI.md | pendiente |
| 29 | detalle |  | La interfaz en español usa punto decimal | Toda la UI en español | pendiente |
| 30 | detalle |  | Las confirmaciones mezclan mayúsculas y estilos de comillas | NUEVO y GUARDAR | pendiente |
| 31 | detalle |  | Las capturas de la tienda son estáticas, en silencio y sin titulares | /home/user/juce-android-bench/store/*.png | pendiente, opinion |
| 32 | detalle |  | DESHACER es rojo, el color que Material reserva para acciones destructivas | Barra principal | pendiente, opinion |
| 33 | detalle |  | Añadir el primer efecto manda los 64 pads por DRV con MEZCLA al 80% | Slot de efecto del canal 01 | pendiente, opinion |

<details><summary>Arreglo propuesto de cada uno</summary>

- **producto#0** Volver a ejecutar Tests/store.py con el binario actual y añadir una comprobación en CI que falle si los PNG de store/ son más antiguos que el último commit de Source/.
- **producto#1** Incluir en el proyecto de fábrica un patrón de 1-2 compases (KICK/SNARE/HAT y un bajo del banco D) y un proyecto DEMO de solo lectura en PROYECTOS.
- **producto#2** Dar al KICK un transitorio de clic entre 1 y 4 kHz y armónicos de 2º y 3º orden (saturación) para que se oiga en altavoces pequeños. Para los subgraves, añadir una versión «móvil» o un realce psicoacústico de armónicos.
- **producto#3** Al exportar la cola, parar el secuenciador al final del patrón y seguir renderizando solo efectos y voces activas. Otra opción: ofrecer un modo «loop perfecto» que sume la cola al principio del archivo.
- **producto#4** Normalizar el kit por pico o por un LUFS de ventana corta adecuado a golpes, y añadir en EXPORTAR una opción «normalizar a −14 LUFS / −1 dBTP».
- **producto#5** Subir el límite a 6-8 s para platos y pads, o aplicar un fundido exponencial de al menos 300 ms antes del corte.
- **producto#6** Fijar la barra de navegación del tour en una posición constante (abajo) y mover solo la tarjeta. EMPEZAR no debería ocupar el sitio de SIGUIENTE.
- **producto#7** Mostrar en el menú el nombre completo traducido, con el código como dato secundario, y agruparlo por categorías (filtro, distorsión, modulación, espacio, dinámica).
- **producto#8** Al crear un proyecto o cuando el slot asociado esté vacío, poner a cero o desactivar los mandos de CONTROL.
- **producto#9** Después de guardar, seleccionar en la lista el proyecto recién guardado y poner su nombre en el campo NOMBRE.
- **producto#10** Mantener la confirmación hasta que el usuario toque en otro sitio, o usar un diálogo modal con CANCELAR/BORRAR.
- **producto#11** Mostrar 8 pasos por fila con celdas de 40 dp o más, o permitir zoom o páginas de 8 pasos.
- **producto#12** Añadir store/<idioma>/{title,short_description,full_description}.txt (formato fastlane) en los 4 idiomas.
- **producto#13** Generar un feature graphic por idioma, corregir «ARTiFACTS» y derivar en README y ZATI.md la cifra de efectos de kNumFx.
- **producto#14** Crear un icono adaptativo (capas foreground y background, más monochrome para Android 13) con la marca crema sobre verde azulado, y borrar ci/splash.png o usarlo.
- **producto#15** Contar solo los pads que suenan en la fuente elegida y anunciar esa cifra: «3 pistas».
- **producto#16** Hacer que NUEVO cargue el kit de fábrica (o dar a elegir kit) y cambiar el texto a «¿Empezar proyecto nuevo? Lo no guardado se pierde».
- **producto#17** Integrar Ableton Link (licencia GPL o comercial) y enviar y recibir reloj MIDI por USB con juce::MidiInput.
- **producto#18** Añadir un indicador de compás y paso en la cabecera, visible mientras se toca, y comprobar el cabezal en un dispositivo real.
- **producto#19** Desactivar BORRAR y ABRIR mientras no haya nada seleccionado.
- **producto#20** Poner los botones fijos arriba o abajo y dejar que la lista se desplace.
- **producto#21** Mostrar «Música › ZATI › Exportaciones» con nombres traducidos y un botón COMPARTIR.
- **producto#22** Usar <proyecto>_<fecha-hora>.wav y una subcarpeta por exportación de pistas.
- **producto#23** Separar la fuente de exportación del modo de transporte, o avisarlo en el diálogo.
- **producto#24** Mostrar el progreso en la barra de estado o en una franja que no tape el contenido.
- **producto#25** Mostrar «ZATI se cerró de forma inesperada; tu proyecto está a salvo» y dejar el detalle en un registro que se pueda compartir.
- **producto#26** Ajustar la tarjeta a su contenido, colocar los números fuera de los controles y reducir el tour a 5-6 pasos con ayuda contextual para el resto.
- **producto#27** Limitar todo a −1 dBTP, aplicar un paso alto a 10 Hz, guardar en mono lo que es mono, dar cuerpo al SNARE entre 150 y 250 Hz y añadir un transitorio de ruido a toms y congas.
- **producto#28** Unificar el criterio (nombres completos de hasta 7 caracteres), cambiar OPEN por OPEN HAT y corregir ZATI.md.
- **producto#29** Formatear los números según el locale activo.
- **producto#30** Unificar en frase con mayúscula inicial y comillas latinas: «¿Sobrescribir «UNO»?».
- **producto#31** Capturar con un patrón sonando y efectos cargados, y añadir un titular por captura.
- **producto#32** Usar el color neutro o secundario para DESHACER.
- **producto#33** Que el primer efecto entre con MEZCLA a unos 30% o se aplique solo al pad seleccionado.

</details>

## secuenciador

**Lo que esta bien:**

- El motor de pasos acierta a la muestra. Con mi banco propio (secuenciador/harness/seq.cpp, salida en secuenciador/seq.out), a 120 BPM y 48 kHz, las semicorcheas caen en 0/6000/12000/18000 sin error y la cadena P0(16)+P1(8) arranca en los pasos 0, 16, 24, 40 y 48: exacto.
- La cuenta atrás de un compás va por el hilo de audio y se mide exacta: el paso 0 suena en la muestra 96000, que es justo un compás a 120 BPM. El metrónomo da cuatro pulsos por compás también con la rejilla 1/8T (24000/48000/72000), porque saca el pulso de la rejilla guardada y no de un 4 fijo.
- El cambio de rejilla mide antes de tocar: midePaso se niega sin cambiar nada y avisa con una cifra («pediría N pasos y la máquina guarda 192»). Además apila un deshacer correcto.
- Bancos de la casa en verde en HEAD: cuenta.py, pasos.py, midi.py y piano.py (59 comprobaciones, 0 FALLA). Además pasos.py da 0 celdas ajenas.
- La tira del paso tiene altura fija: la rejilla ya no salta con el primer toque (lo comprobé en las fotos f02→f03). Es un arreglo de la feria que sí se mantiene.
- En CANCIÓN, el bloque que se pinta a media altura del compás 3 se pega al principio del compás (foto f11-c.png). La queja de la feria de que se pegaba al tiempo no se reproduce con este zoom.
- Las colas MIDI de entrada, MIDI de salida e interfaz van separadas, sin un productor doble sobre la cola de comandos, y el pánico manda un «todas fuera» de verdad.
- El texto «1 compás» va en singular, y la página PATRÓN agrupa bien CADENA, PATRÓN, BLOQUEOS, SWING, EUCLIDES y REJILLA.

| # | gravedad | v | hallazgo | donde | estado |
|---|---|---|---|---|---|
| 0 | bloqueante | si | Cuantizar en directo está al revés: lo que tocas a tiempo se retrasa casi un paso y lo que tocas tarde suena sin cuantizar | AudioEngine.cpp:1376-1424 (rama liveQuantise de renderNextBlock) | arreglado (fb718ae) |
| 1 | alta | si | Cambiar la rejilla parte la canción: los bloques y la automatización no se reescalan | MainComponent.cpp:2962-3015 (gridSlider.onValueChange) y AudioEngine.cpp:5192 remapeaPaso; BloqueSong.paso/largo/offset (AudioEngine.h:569-5 | pendiente |
| 2 | alta | si | En modo canción, el cursor y la grabación usan el paso dentro del compás y no el paso del patrón | AudioEngine.cpp fireStep, rama songMode (currentStep = songStep % pasosCompas); MainComponent.cpp:9618-9640 (REC usa getPlayStep y getPlayin | pendiente |
| 3 | alta | si | El swing deja de funcionar al afinar la rejilla: con 1/32 las semicorcheas no swinguean y con 1/32T swinguean un tercio | AudioEngine.cpp:1504-1510 (lateBy sobre los pasos GUARDADOS impares) | pendiente |
| 7 | alta | si | El fichero MIDI exportado o importado supone siempre semicorcheas aunque la rejilla guardada sea otra | MidiArchivo.h (kPasoNegra=4, t0 = paso*kTicksPaso; en lee() paso = lround(negras*4)); MainComponent.cpp:20513-20640 (notasDelPatron / import | pendiente |
| 11 | alta |  | La salida MIDI no lleva la nota del piano roll, tiene una puerta fija de 60 ms, canal 10 sin interfaz y no hay reloj | AudioEngine.cpp:467 (triggerPad: midiOut.push({slot, vel, true}) sin semitono); MidiIo.h:352-374 (kGateMs=60); MidiIo.h:298 setChannel sin l | pendiente |
| 16 | alta |  | El paso 16 de un patrón de un compás queda cortado a 7 pt y sin barra para llegar a él | StepGrid.h:125-130 (numCols cuenta solo columnas enteras); MainComponent_Layout.cpp:6176-6190 (sinBarra/seqHayBarra calculan que caben 16) | pendiente |
| 4 | media | si | El swing al 100 % equivale a un 62.5 % de MPC, y el documento promete un 75 % | AudioEngine.cpp:1504-1510; MainComponent.cpp:2833-2843 (lectura «N %»); ZATI.md:546 | pendiente |
| 5 | media | si | El empujón negativo no hace nada: HUMANIZAR solo retrasa | AudioEngine.cpp:1533-1540 y ~1601 (`at = jmax(0, lateBy+empuje+…)`); MainComponent.cpp:15347 (humanizePattern) | pendiente |
| 6 | media | si | Cambiar de patrón en marcha salta en el acto y entra a mitad de patrón | AudioEngine::setEditPattern / fireStep (se conserva currentStep; (currentStep+1)%len) | pendiente |
| 8 | media | si | Importar MIDI recorta las notas largas a 63 cuartos de paso, cuando el motor ya admite 768 | MidiArchivo.h, lee(): n.largo = jlimit(0, 63, …); AudioEngine.h:1418 kLenMax = 4*kNumSteps | pendiente |
| 9 | media | si | El título de la ficha MIDI sale pegado: «MIDIPATRÓN 1PAD A02» | MainComponent_Paint.cpp:3172-3176 (campoAcampo con campos sin separador); MainComponentInterno.h:577 | pendiente |
| 12 | media |  | Lo que entra por MIDI no se graba en el patrón | MainComponent.cpp:1899-1902 (onNoteOn → postNoteOnFromMidi solamente) | pendiente |
| 13 | media |  | Grabar con REC compensa la latencia en semicorcheas fijas, no guarda la fuerza y puede dar un doble golpe | MainComponent.cpp:9618-9640 | pendiente |
| 15 | media |  | El banco de la casa arr.py FALLA en HEAD: el asa del bloque estira un compás de más | Playlist.h:960-1050 (h = jmax(pegado + u, d + u)); Tests/arr.py:466-470; MainComponent_Audit.cpp:575-580 | pendiente |
| 17 | media |  | Las celdas de la rejilla miden unos 18x19 pt, por debajo de los mínimos táctiles | PASOS en 393x851 (f02.png); MainComponent_Layout.cpp:6050-6063 y 6213-6223 | pendiente, opinion |
| 18 | media |  | LARGO: arrastrar 9 pt salta de 1 a 4 compases | MainComponent.cpp:2660-2730 (lengthSlider, LinearHorizontal en compases) | pendiente |
| 19 | media |  | Al pasar a 4 compases aparece la barra y empuja la tira y PLAY 44 pt hacia abajo | MainComponent_Layout.cpp:7713-7722 (seqBarra) y 7798 | pendiente |
| 20 | media |  | Los cuatro BLOQUEOS de la página PATRÓN no tienen nombre | MainComponent_Layout.cpp:8043-8073 (frente a la variante de la tira en 7646, que sí lleva ATAQUE/CAIDA/INICIO/PAN) | pendiente |
| 21 | media |  | El piano roll esconde notas con la vista gruesa | MainComponent.cpp:16606-16720 (refreshPiano) | pendiente |
| 10 | baja |  | La ayuda de la ficha MIDI no aparece en el teléfono | MainComponent_Paint.cpp:3181-3183 (pintaAyuda devuelve false si no cabe); MainComponent_Layout.cpp:1538 | pendiente |
| 14 | baja |  | El metrónomo y la cuenta atrás siempre van en 4/4: un patrón de 12 pasos no mueve el acento | AudioEngine.cpp fireStep (pasosPulso = pasosCompas/4; acento si clicPaso % pasosCompas == 0); pasosPorCompas() siempre 4/4 | pendiente |
| 22 | baja |  | Los nombres de nota del piano son relativos: la raíz se llama C0 y está en la fila de arriba | PianoRoll.h:1005 (semiBase=-12) y nombreDe(); MainComponent.h:555 (pianoBase=-12) | pendiente, opinion |
| 23 | baja |  | El redoble del último paso con empujón y swing se sale del compás | AudioEngine.cpp:1540-1606 (redoble a partir de at = lateBy+empuje) | pendiente |
| 26 | baja |  | La barra de herramientas de CANCIÓN tiene once tapas de solo icono | Página CANCIÓN (f10.png) | pendiente, opinion |
| 27 | baja |  | La salida MIDI puede retrasarse hasta 50 ms tras un rato sin notas | MidiIo.h (hilo de envío con wait(50); el audio no le avisa) | pendiente |
| 24 | detalle |  | Los títulos de la ficha SEC tienen tres formatos distintos | MainComponent_Paint.cpp:2461 y 2707 | pendiente, opinion |
| 25 | detalle |  | Hay comentarios que contradicen al código del swing | AudioEngine.h:1380-1382 y AudioEngine.cpp:1533 («El swing mueve las corcheas pares») | pendiente |

<details><summary>Arreglo propuesto de cada uno</summary>

- **secuenciador#0** Usar la fase dentro del paso: si stepAccum < sps/2, sonar ya (o al borde que se acaba de pasar). Si no, ir a pending con at = toNext. Cuantizar a la rejilla VISIBLE (vistaRejilla) y aplicar el swing del paso de destino. En StressTest, comprobar las muestras exactas a 5 % y 90 %.
- **secuenciador#1** En el mismo onValueChange, cuando objetivo != pasoViejo, multiplicar o dividir por el mismo factor entero b.paso, b.largo, b.offset de cada bloque y e.paso de cada EventoAuto. Después, publicaBloques() y publicaAutomacion(). Añadir a arr.py una regla de «el bloque mide lo mismo en compases tras cambiar la rejilla».
- **secuenciador#2** Publicar en playStep el paso del patrón que dispara el carril que se está editando (el mismo valor que ultimoDisparo & 0xffff), y publicar el banco de ese carril. Que REC use ese par.
- **secuenciador#3** Calcular el swing en tiempo musical: retrasar los golpes que caen en la segunda semicorchea de cada corchea (posición en unidades de 1/48 de pulso: (paso*stepUnits) % 24 == 12), con retraso = (sw−0.5)·2·semicorchea en muestras, sin importar el paso guardado.
- **secuenciador#7** Convertir con stepBeats: ticks = paso * pasoUnidades() * kPpq / 48. Al importar, paso = lround(ticks * 48 / (kPpq * pasoUnidades())), afinando el paso si hace falta con midePaso/remapeaPaso. Los compases deben salir de pasosPorCompas() y no de 16.
- **secuenciador#11** Añadir semis y largo a NoteEvent y emitir nota = base + semis, con note-off al acabar el largo (o la puerta si es 0). Poner un selector de canal en AJUSTES·MIDI. Emitir reloj 24 ppq más Start/Stop desde renderNextBlock (con precisión de muestra) y, en una segunda fase, aceptar reloj externo.
- **secuenciador#16** Que el maquetado y StepGrid usen la misma cuenta: si caben 15.x columnas, o se encogen las 16 para que entren (anchoRejilla/16) o se enseña la barra. Usar los 22 pt sobrantes.
- **secuenciador#4** retraso = (sw−0.5)·2·paso (sin el ·0.5), de modo que 0.75 coloque el contratiempo en el 75 %. Mostrar el valor en la escala de MPC (50 %…75 %) en vez de 0…100 %.
- **secuenciador#5** Para un empujón negativo, adelantar el disparo al paso anterior: al procesar el paso N, mirar los golpes de N+1 con empujón < 0 y meterlos en pending con at = sps + empuje. Se puede hacer como el lookahead de Elektron (micro-timing ±).
- **secuenciador#6** Guardar el patrón pedido como «siguiente» y aplicarlo cuando currentStep vuelva a 0 (o en el próximo compás si se elige así). Pintar el pad del patrón parpadeando mientras espera.
- **secuenciador#8** jlimit(0, AudioEngine::kLenMax, …). Añadir a midi.py una nota de dos compases.
- **secuenciador#9** Pasar los campos como "  " + dot + "  " + T("PATRON %1", …), igual que en PIANO y PASOS. Añadir a expo.py una regla PEGADO para títulos.
- **secuenciador#12** Llevar las notas MIDI al hilo de mensajes (AsyncUpdater) y pasarlas por la misma función de grabación que el pad en pantalla, con su fuerza y semitono.
- **secuenciador#13** Usar samplesPerStep real (o engine.stepBeats) para la compensación. Guardar setStepVel con la fuerza del toque. Si se redondea hacia delante, marcar el paso como «ya sonado en esta vuelta». Reutilizar armaCuentaSiToca para REC de pasos.
- **secuenciador#15** Pegar el filo a la división más cercana (pasoPegado) sin sumar u cuando el dedo cae pasada la mitad; o, si redondear hacia arriba es lo que se quiere, cambiar la regla y el comentario. No dejar el banco en rojo.
- **secuenciador#17** Por defecto, ventana de 8 pistas (1-8 / 9-16) en el teléfono vertical, o zoom de ancho que deje 24 pt mínimos con barra horizontal (la barra ya existe).
- **secuenciador#18** Cambiarlo por un selector de pasos – / + (como EUCLIDES y REJILLA en la misma ficha) o por chips 1/2/4/8 compases.
- **secuenciador#19** Reservar siempre la banda de la barra (vacía si no hace falta) o superponer la barra sobre la rejilla.
- **secuenciador#20** Reutilizar los rótulos de la tira encima de cada dial, o poner el nombre dentro del lector («ATQ no»).
- **secuenciador#21** Dibujar todas las notas del tramo con su posición fraccionaria, o marcar la columna con un indicador de «hay más dentro».
- **secuenciador#10** Dar a midiAyudaArea dos renglones (lineas=2), o acortar el texto («Do central = nota del pad»). Que la segunda parte salga de la rejilla real.
- **secuenciador#14** Añadir un numerador de compás por patrón (o global) y que pasosPorCompas, el clic y la regla de CANCIÓN lo usen. Como mínimo, poner el acento al reiniciar el patrón.
- **secuenciador#22** Nombrar con la octava MIDI real (raíz = C3 o C4, según un ajuste) y centrar la vista en la raíz.
- **secuenciador#23** Repartir el redoble en (sps − desplazamiento)/n, o recortarlo al borde del paso.
- **secuenciador#26** Pulsación larga con tooltip, o nombre corto debajo del icono en las tapas de herramienta. Quitar uno de los dos «8 compases».
- **secuenciador#27** Llamar a notify() (o usar un semáforo sin bloqueo) cuando el audio empuja a la cola, o mandar con marca de tiempo por MidiOutput::sendBlockOfMessages.
- **secuenciador#24** Un formato fijo «PÁGINA · PAD · Pn» que cae siempre por el mismo orden.
- **secuenciador#25** Corregir el comentario a «retrasa la segunda semicorchea de cada corchea».

</details>

## rendimiento

**Lo que esta bien:**

- Callback de audio sin reservas ni cerrojos: callgrind con --toggle-collect en AudioEngine::renderNextBlock, 902 bloques de 128 muestras con ZATI_DEMO=1 ZATI_SONANDO=1. Cero llamadas a malloc, new, free o mutex dentro; solo exp, pow, sincosf, lround, memset, AbstractFifo y getMagnitude (R/cg/cg.out).
- Coste del motor contenido: 282k Ir por bloque de 128 muestras (unas 2200 Ir por muestra) con la demo sonando.
- La decodificacion de muestras va en una hebra propia (lanzaHebraPads, MainComponent.cpp:21211) y el hilo de mensajes solo coloca los pads en trozos. La fabrica se rinde fuera del hilo de mensajes (FabricaJob, MainComponent.cpp:11077); antes eran 1691 ms seguidos en el hilo de mensajes, segun el propio comentario.
- Las colas de la fabrica estan bien recortadas: de 40.61 MB en RAM, solo 0.23 MB estan por debajo de -60 dBFS (analisis de los 64 WAV en tel19/casa/Music/ZATI/.sesion/samples).
- FLAG_KEEP_SCREEN_ON solo mientras suena o graba (MainComponent.cpp:22162-22182): la pantalla no se queda encendida en reposo.
- La forma de onda de cada pad se calcula una vez (PadButton::buildSpark) y no en cada paint.
- CacheCara guarda la cara en una imagen y la repinta por rectangulos sucios. Tests/cpu.py la mide por pixeles, que no depende del reloj.
- DeviceTier escala con el aparato: voces, puntos del osciloscopio, reloj (100/60/40/33 ms) y fabrica mono en gama baja (instrumentoEstereo=false).
- SessionKeeper compara el texto antes de escribir state.xml: el disco no se toca si nada cambio.
- Las setters del VU y del osciloscopio solo piden repaint si el valor cambia. Las lamparas de efectos no repintan si ningun efecto esta encendido (umbral 0.004).
- La rejilla de pasos, el piano y el cabezal solo se alimentan con su ficha visible (MainComponent.cpp:22790-22830). Bajo una ficha se salta el repintado de la cara (caraTapada/animar).
- El cierre del puente MIDI y la espera del abridor de audio en ~MainComponent estan ordenados a proposito para evitar un uso despues de liberar (MainComponent.cpp:8892-8940).
- El paso de rejilla del vblank sustituye a los factores fijos por tick: las constantes de tiempo (kTauDestelloMs, kTauAgujaMs) son independientes de la cadencia.

| # | gravedad | v | hallazgo | donde | estado |
|---|---|---|---|---|---|
| 0 | alta | si | En segundo plano no se para nada: el temporizador sigue y, sin vblank, pinta el cuadro entero con la FFT incluida | Source/MainComponent.cpp:20946 (appSuspended), :22285 (respaldo en timerCallback), :5096 startTimer, :5141-5152 VBlankAttachment | arreglado (7499333) |
| 1 | media |  | El cuadro en reposo cuesta medio millon de instrucciones y se ejecuta a cada vblank aunque no se mueva nada | Source/MainComponent.cpp:22545-22920 (pintaCuadro), :22345 (enVBlank) | pendiente |
| 2 | media | si | La FFT del espectro se ejecuta en cada cuadro aunque haya silencio (51% del cuadro en reposo) | Source/SpectrumDisplay.h:72-141 (setSamples), Source/Analizador.h:52-73 (analiza) | pendiente |
| 4 | media | si | canalIdx recorre un bucle lineal en tiempo de ejecucion: el 41.7% de las instrucciones del motor | Source/AudioEngine.h:3568 (canalIdx), :3642 (busDe), Source/AudioEngine.cpp:2005 (lambda live) | pendiente |
| 5 | media | si | Un oyente global de raton solo para mouseUp pone a JUCE a sondear el puntero 10 veces por segundo para siempre | Source/MainComponent.cpp:5100 (addGlobalMouseListener), Source/MainComponent.h:2249 (OidoDeToques) | pendiente |
| 10 | media |  | Los sonidos mono de fabrica se guardan como estereo duplicado: 3.77 MB de RAM tirados | Source/Kits.h:688 (monoDeVerdad), :1068-1074 (R = memcpy de L) | pendiente |
| 11 | media |  | La fabrica se copia a la sesion como 64 WAV de 30.5 MB en el primer arranque, aunque se puede regenerar | Source/MainComponent.cpp:21150 (autosave), :22228-22254 (session.sync), Source/Kits.h (render puro, semilla 1000+index*37) | pendiente |
| 12 | media |  | Se construyen 2634 componentes antes del primer fotograma: todas las fichas se crean al arrancar | Source/MainComponent.cpp (constructor de MainComponent), Source/MainComponent.h (fichas como miembros por valor) | pendiente |
| 13 | media |  | El FileBrowserComponent se crea al arrancar y deja 6.68 MB de heap vivos toda la sesion | Source/MainComponent.h:2364 (std::unique_ptr<juce::FileBrowserComponent> browser), creado dentro de MainComponent::MainComponent | pendiente |
| 20 | media |  | Cerrar la app mientras se rinde la fabrica obliga a esperar el banco entero: FabricaJob no tiene bandera de parada | Source/MainComponent.h FabricaJob (~FabricaJob con join), Source/MainComponent.cpp:10990-11053 (rindeFabrica), :8892 (~MainComponent) | pendiente |
| 3 | baja | si | La ventana de Hann se recalcula entera en cada analisis: 1024 cosenos por cuadro | Source/Analizador.h:56-61 | pendiente |
| 6 | baja | si | La linea de continuidad mide hasta tres textos cuatro veces por segundo para un texto que cambia dos veces por sesion | Source/MainComponent.cpp:14581-14660 (lineaDeContinuidad), :14668 (miraContinuidad), :22567 (cada kContinuidadMs=250) | pendiente |
| 7 | baja | si | caraTapada hace un dynamic_cast por cada hijo visible en cada cuadro | Source/MainComponent.cpp:8772-8779, llamada en :22658 | pendiente |
| 8 | baja | si | miraCosturas compone dos textos y recorre 64 pads en cada vblank para compararlos con el anterior | Source/MainComponent.cpp:14726 (miraCosturas), textoCosturaEfectos y textoCosturaControl | pendiente |
| 14 | baja |  | El estado completo se serializa a XML de 100 KB cada 20 s aunque nada haya cambiado | Source/MainComponent.cpp:22228-22254 (kEstadoSesionMs=20000, session.pideEstado (textoSesion())), MainComponent.h:2211 (textoSesion) | pendiente |
| 15 | baja |  | UiAudit::enabled() lee una variable de entorno y crea Strings en cada llamada, y se llama desde paint | Source/UiAudit.h:69; llamadas en costura :392, cuna :404, vuFila :446, celda :472, vuRotulo :666 | pendiente |
| 18 | baja |  | Tres ThreadPool de un hilo cada uno: seis despertares por segundo en reposo | Source/MainComponent.h:4618 (denoisePool {1}), :4653 (sintesPool {1}), Source/SampleLoader.h:28 (pool {1}) | pendiente |
| 19 | baja |  | El vigilante zati-atasco se despierta 10 veces por segundo durante toda la vida de la app | Source/Bitacora.h:200-215 | pendiente |
| 22 | baja |  | restoreSession analiza 100 KB de XML en el hilo de mensajes antes del primer fotograma | Source/MainComponent.cpp:21320 (restoreSession) | pendiente |
| 26 | baja |  | Picos transitorios de memoria al rendir la fabrica: copia completa para la sonoridad y vectores crudos de hasta 8 MB | Source/Kits.h (Kits::gananciaSonoridad, Kits::render) | pendiente |
| 27 | baja |  | La fabrica ocupa el 63% del presupuesto de muestras de la gama baja antes de que el usuario cargue nada | Source/DeviceTier.cpp/.h (gama low: sampleBudgetMB 64, instrumentoEstereo=false), Source/Kits.h:1053 | pendiente, opinion |
| 29 | baja |  | El hilo Timer de JUCE se despierta unas 505 veces por segundo en reposo en escritorio | build/_deps/juce-src/modules/juce_gui_basics/native/juce_Windowing_linux.cpp:645-661 (vBlankManager como Timer), Source/MainComponent.cpp:50 | pendiente |
| 9 | detalle |  | El pulso de las lamparas construye un Identifier desde un literal dos veces por ranura y por cuadro | Source/MainComponent.cpp:22631 y :22634 (getWithDefault y set de "pulse") | pendiente |
| 16 | detalle |  | apunta y pintaAyuda miden el texto 2 a 4 veces solo para alimentar la auditoria, que en produccion no hace nada | Source/MainComponent.cpp:~9010-9060 (apunta), pintaAyuda; Source/UiAudit.h:252 (rotulo vuelve si !midiendo) | pendiente |
| 17 | detalle |  | El espectro rehace el rotulo CANAL nn, la fuente y dos medidas de texto en cada paint | Source/SpectrumDisplay.h:563-614 | pendiente |
| 21 | detalle |  | rindeFabrica crea y destruye cuatro std::thread por banco en vez de reutilizar hilos | Source/MainComponent.cpp:11040-11053 y :11077 | pendiente |
| 23 | detalle |  | fondoCache guarda solo un degradado a resolucion logica y se reescala 2.625x en cada repintado | Source/MainComponent.cpp:5244-5300 (pintaFondo y reconstruyeFondo), Source/MainComponent.h:4492 | pendiente, opinion |
| 24 | detalle |  | T() busca dos veces en la tabla y crea un String desde const char* en cada llamada | Source/Lang.cpp:2493 | pendiente |
| 25 | detalle |  | PadButton reconstruye el Path de la forma de onda (44 columnas) en cada paint | Source/PadButton.h (paint, construccion del Path desde la chispa) | pendiente |
| 28 | detalle |  | renderNextBlock es una funcion de unas 3500 lineas con lambdas que el compilador no inlinea | Source/AudioEngine.cpp:821-4373 (renderNextBlock); lambdas busIdx :1956, pre :1987, live :2005, blockFor :2015, returnBus :2025 | pendiente, opinion |

<details><summary>Arreglo propuesto de cada uno</summary>

- **rendimiento#0** En appSuspended: stopTimer(); vblank = {}; y parar relojDibujo si existe. En appResumed: startTimer (dev.relojMs) y volver a crear el VBlankAttachment. Ademas, `if (! appInForeground) return;` al principio del respaldo de :22285 como cinturon.
- **rendimiento#1** Llevar un `bool hayAlgoVivo` (sonando, grabando, busyJobs>0, algun padFlash>0, algun fx encendido, vu>0, espectro no en el suelo). Cuando sea falso, soltar el VBlankAttachment (vblank = {}) y dejar solo el reloj de relojMs para miraContinuidad y similares. Volver a enganchar el vblank en el primer toque, trigger o cambio del motor.
- **rendimiento#2** En SpectrumDisplay::setSamples, calcular primero el pico del bloque. Si pico < 1e-5 y todos los bines de `suave` ya estan en kPiso, no llamar a analiza ni repintar. Si solo hay silencio nuevo, dejar caer `suave` hacia kPiso con la constante de tiempo, sin FFT, hasta llegar al suelo.
- **rendimiento#4** Una tabla calculada en compilacion: `static constexpr auto kCanalIdx = [] { std::array<int8_t, kNumFx> t{}; int n = 0; for (int f = 0; f < kNumFx; ++f) t[f] = fxPorCanal[f] ? n++ : -1; return t; }();`, y canalIdx(f) devuelve `(unsigned) f < kNumFx ? kCanalIdx[f] : -1`. Mejor aun, sacar `live(f)` fuera de los bucles por voz y por muestra y calcularlo una vez por bloque.
- **rendimiento#5** Quitar addGlobalMouseListener y usar `addMouseListener (&oidoDeToques, true)` sobre MainComponent (recibe los mouseUp de todos los hijos), o sobrescribir mouseUp en el propio componente. Retirarlo en el destructor con removeMouseListener.
- **rendimiento#10** Para monoDeVerdad, devolver un SampleBuffer de 1 canal. La voz ya debe tratar muestras mono (la grabacion de microfono lo es): leer ch0 para los dos lados. El WAV de sesion de esos pads tambien baja a la mitad.
- **rendimiento#11** Marcar en state.xml los pads de fabrica no modificados (`fuente="fabrica:17"`, ya existe padDeFabrica) y no escribir su WAV. Al restaurar, re-renderizarlos con FabricaJob. Solo se escribe WAV cuando el pad se edita o se reemplaza.
- **rendimiento#12** Crear las fichas de forma perezosa: std::unique_ptr<Sheet> y construir en el primer openSheet. Empezar por las mas pesadas que no se ven al arrancar (AJUSTES, MEZCLA, navegador, manual). Medir de nuevo con la misma cuenta de gdb.
- **rendimiento#13** Construir el navegador en openBrowser y hacer browser.reset() al cerrarlo. Si hace falta conservar la carpeta, guardar solo el juce::File.
- **rendimiento#20** Anadir `std::atomic<bool> parar` a FabricaJob, ponerlo a true en el destructor antes del join, pasarlo a rindeFabrica y comprobarlo en el bucle `for (i = siguiente.fetch_add(1); ...)`. Vaciar fabricaCola en ~MainComponent.
- **rendimiento#3** Un `std::array<float, kFft> ventana` como miembro, rellenado una vez en el constructor de Analizador. El bucle queda `buf[i] = datos[n - kFft + i] * ventana[i];`. Tambien se puede usar juce::dsp::WindowingFunction<float> creada una vez.
- **rendimiento#6** Componer una clave barata (currentProject, llenos, aSalvo, horas o dias de la fecha, ancho, idioma) y medir solo si la clave cambia. Mejor: programar la siguiente revision para el momento exacto en que caduca kASalvoMs o cambia la hora, en vez de mirar cada 250 ms.
- **rendimiento#7** Llevar un contador `int fichasVisibles` que suban y bajen openSheet, closeSheet y closeAllSheets (y el panel XY), y que caraTapada devuelva `fichasVisibles > 0 || xyPanel.isVisible()`. Si se quiere conservar la deduccion, guardar los punteros Sheet* en un array al construir y recorrer solo ese array (unos 39) sin RTTI.
- **rendimiento#8** Una bandera `costuraSucia` que pongan los cambios de canal, de efecto o de asignacion de pad a canal (los mismos sitios que ya llaman a refresh). miraCosturas sale enseguida si no esta puesta.
- **rendimiento#14** Un contador `versionEstado` que suban los mutadores (o la misma marca de sucio que ya usa autosave), y pideEstado solo si cambio desde la ultima vez.
- **rendimiento#15** `inline bool enabled() { static const bool e = SystemStats::getEnvironmentVariable("ZATI_AUDIT", {}).isNotEmpty(); return e; }`. En los sitios de paint, envolver la llamada en `if (UiAudit::enabled())` para no evaluar argumentos en produccion.
- **rendimiento#18** Un unico juce::ThreadPool compartido (o el ThreadPool por defecto de JUCE 8) para cargar, sintetizar y quitar ruido. Si hace falta orden, usar colas por tarea sobre ese pool.
- **rendimiento#19** Esperar el umbral de atasco que se quiere detectar (por ejemplo wait(1000) si el umbral es de segundos) y comparar la edad del latido. En appSuspended, dormirlo del todo con wait(-1) hasta appResumed.
- **rendimiento#22** Leer y analizar el XML en la hebra zati-session (o en el pool) y entregar el ValueTree al hilo de mensajes con MessageManager::callAsync. La aplicacion al estado se queda en el hilo de mensajes.
- **rendimiento#26** Calcular la sonoridad en streaming sobre el buffer final (suma de cuadrados por ventana) sin copia. Rendir directamente en el AudioBuffer final en lugar de un vector crudo copiado despues.
- **rendimiento#27** En gama baja, guardar toda la fabrica a 1 canal (las percusiones de verdad son mono) y bajar el tope a 2 s para los sonidos con decay largo. Otra opcion es rendir en 16 bits enteros y convertir en la voz.
- **rendimiento#29** Lo mismo que en los hallazgos de reposo: soltar el vblank cuando no hay nada vivo, quitar el oyente global y no tener RelojDibujo y el reloj principal a la vez si uno basta.
- **rendimiento#9** Usar `static const juce::Identifier kPulso ("pulse");`. Mejor aun, guardar el pulso en un `std::array<float, kNumRanuras> pulsoLampara` miembro y pasarlo al boton con un setter, sin NamedValueSet.
- **rendimiento#16** Envolver las medidas que solo sirven a la auditoria en `if (UiAudit::midiendo)`. En pintaAyuda, medir una vez y reutilizar el ancho.
- **rendimiento#17** Guardar el rotulo, la fuente y su ancho en miembros, recalculados solo en setCanal cuando cambia el canal, en resized o al cambiar de idioma. La medida de auditoria, dentro de `if (UiAudit::enabled())`.
- **rendimiento#21** Usar un ThreadPool comun (ver el hallazgo de los tres pools) con tantos hilos como nucleos, encolar los 64 renders como jobs y publicar `listo[i]` igual que ahora.
- **rendimiento#23** Opcion A: quitar la cache y pintar fillRect con ColourGradient (un degradado vertical de dos paradas es lo mas barato que tiene el rasterizador). Opcion B: hornearla a escala fisica (getApproximateScaleFactorForComponent) para que sea un blit 1:1. Decidir midiendo con ZATI_FONDO_VIVO bajo callgrind, no con reloj.
- **rendimiento#24** Una sola busqueda: `if (auto* v = t.getReference... )` o HashMap::find, y devolver por referencia. Para los literales usados en paint, cachear el texto traducido en miembros que se invaliden al cambiar de idioma.
- **rendimiento#25** Guardar el Path en un miembro y reconstruirlo en buildSpark y en resized. En paint, solo fillPath o strokePath.
- **rendimiento#28** Partir renderNextBlock en etapas con nombre (voces, envios, efectos por canal, master), calcular una vez por bloque las tablas que hoy piden las lambdas (live, busIdx) y pasarlas como arrays. Eso tambien hace legible el perfil.

</details>

## codigo

**Lo que esta bien:**

- El hilo de audio no reserva memoria: renderNextBlock (AudioEngine.h) no tiene String, std::vector que crezca, new ni locks. Las retiradas van por colas sin bloqueo (RetireQueue y TablaQueueDe), y StressTest lo confirma: 'la cola de retirados no pierde: 0 punteros tirados'.
- StressTest.cpp tiene 139 veredictos que suman fallos con zatiFalla y sale con 1 si alguno falla. Comprueba muestras hostiles (NaN, infinito, frecuencia 0, negativa o enorme, 1 o 4 muestras) con y sin limitador, y ninguna cuelga ni envenena el bus.
- La mayoria de los bancos son falsables: con el binario cambiado por un script vacio, 54 de 59 salen con rc distinto de 0. De los que salen con 0 sin app, todos menos store.py son estaticos y lo hacen con razon.
- Muchos bancos llevan 'cadena de control', una medida de referencia que tiene que cambiar para que la regla cuente: feria 'guardado' (120 sin soltar, 131 al soltar), canales y paneles. deshacer.py y expo.py ademas tienen controles para que un renombre no deje la regla vacia.
- Con el binario de HEAD, feria.py da VEREDICTO OK en 14 reglas, y salen en verde dlc (19 comprobaciones), icono (23), kits, cuenta, dinamica, eq, export, canales, clips, auto, buffer, audio y arranque.
- La cadena de suministro esta cerrada: firma.py da 0 FALLA y suministro.py da '10 de 10 acciones clavadas por SHA'.
- Hay poca duplicacion literal: el detector de bloques de 8 lineas repetidas solo encontro 16 en todo Source/. Y desde la auditoria de agosto se arreglo: vuArea, stepStripArea, hitRow y knobSm/Md/Lg quitados; el recalculo de destinos con destinoCache (Paint.cpp:1266); expo.py y session.py ya devuelven codigo de salida; el literal 0.92 casi fuera de los .cpp.

| # | gravedad | v | hallazgo | donde | estado |
|---|---|---|---|---|---|
| 0 | alta |  | store.py sale con 0 aunque la app no saque ninguna foto de Play | Tests/store.py:117-120 y 150-168; .github/workflows/banco.yml:284 | arreglado (7499333) |
| 1 | alta |  | arr.py esta rojo en HEAD: el asa del bloque estira un compas de mas | Tests/arr.py:472-473; Source/MainComponent_Audit.cpp:520-595 (auditArrange); banco.yml:159 | arreglado (7499333) |
| 2 | alta |  | 15 bancos no corren en CI, entre ellos feria.py, que guarda las regresiones de la feria | .github/workflows/banco.yml, medidas.yml | pendiente |
| 5 | alta |  | canales.py: el umbral de la fila de la mesa bajo de 44 a 24 px, por debajo del dedo | Tests/canales.py:855 y 972 (fila < 24), 699 (fader_ancho < 64), 693 y 931 (mesa_pan < 30) | pendiente |
| 20 | alta |  | MainComponent::resized: 7903 lineas y CCN 588 | Source/MainComponent_Layout.cpp (MainComponent::resized) | pendiente, opinion |
| 21 | alta |  | El constructor de MainComponent tiene 5087 lineas | Source/MainComponent.cpp (MainComponent::MainComponent) | pendiente, opinion |
| 22 | alta |  | AudioEngine::renderNextBlock: 3553 lineas y CCN 520 en el hilo de audio | Source/AudioEngine.h (renderNextBlock) | pendiente, opinion |
| 24 | alta |  | canalDeParam protege un indice y fxParamDe usa otro fuera de rango (aviso -Warray-bounds) | Source/AudioEngine.h:288-292 (canalDeParam), 3096-3103 (fxParamDe), 3198 (pasoMod) | arreglado (c1d7764) |
| 3 | media |  | SKILL.md del banco no cita 20 bancos y remite a un BITACORA.md que no existe | .claude/skills/banco/SKILL.md:10 y su tabla de que correr segun lo que se toque | pendiente |
| 4 | media |  | banco.yml dice que no corre nada que dependa del reloj, pero corre atasco.py y fps.py | .github/workflows/banco.yml:11; Tests/atasco.py (presupuesto de 250 ms por tramo); Tests/fps.py | pendiente |
| 6 | media |  | feria.py acepta celdas de 19 px y filas de 36 px en su regla 'celda' | Tests/feria.py:77-80 | pendiente |
| 7 | media |  | La regla de deshacer de feria compara limites sin comprobar que DESHACER se vea | Source/MainComponent_Audit.cpp:11761-12001 (auditFeria); Tests/feria.py (reglas 'deshacer' y 'rejilla'); MainComponent_Layout.cpp:933-940 | pendiente |
| 8 | media |  | analiza.py y profundidad.py no pueden fallar, y analiza.py esta en CI | Tests/analiza.py, Tests/profundidad.py; banco.yml:222 | pendiente |
| 9 | media |  | skins.py calcula el contraste de la celda de pulso y no lo juzga | Tests/skins.py:350 (beat), CELL_BEAT=0.48 en 220 | pendiente |
| 12 | media |  | expo.py: el numero de corridas sale distinto en cada sitio que lo cita | Tests/expo.py (SIZES, LANGS, SHEETS; mensaje en 1747); .claude/skills/banco/SKILL.md:31 y :99 | pendiente |
| 13 | media |  | El juicio de idioma de expo.py solo compara es con en y se salta todo texto con '/' | Tests/expo.py:1579-1603 (judge_lang), 1600 | pendiente |
| 15 | media |  | 14 bancos dejan su casa temporal sin borrar: 486 directorios, 13 GB en /tmp | Tests/auto, canales, clips, costuras, cuenta, dinamica, eq, midi, modos, pasos, plato, rack, ranuras, tomas (tempfile.mkdtemp sin rmtree) | pendiente |
| 17 | media |  | display_alive esta copiada en 14 tests y corre() en 38 variantes | Tests/arr, buffer, carga, clips, costuras, cpu, dinamica, eq, fluidez, fps, kits, sel, session, tomas (display_alive); 38 def corre() | pendiente, opinion |
| 23 | media |  | Mas funciones gigantes: Iconos::trazo, rindeCrudo, StressTest main, auditorias | Source/Iconos.h (trazo), Sintes (rindeCrudo), Main.cpp (startAudit), MainComponent_Audit.cpp (auditArrange, auditCanales, applyState), Tests | pendiente, opinion |
| 25 | media |  | Las flags de CMake apagan avisos utiles | CMakeLists.txt; build-bench/CMakeFiles/Zati.dir/flags.make | pendiente |
| 27 | media |  | (int)(s*len) en float pierde precision en muestras largas | Source/MainComponent.cpp:3501-3502 y 13990-13991 | pendiente |
| 33 | media |  | El estado por pad son 43 std::array paralelos | Source/MainComponent.h (43 std::array<…, kNumPads>); 422 engine.get/set en MainComponent.cpp y 438 en MainComponent_Audit.cpp | pendiente, opinion |
| 34 | media |  | El codigo de auditoria entra en el binario de release | CMakeLists.txt:46-62; contadores UiAudit:: en MainComponent.cpp (38), Layout (27) y Paint (22) | pendiente, opinion |
| 10 | baja |  | instr.py calcula dos FFT por familia para una regla retirada | Tests/instr.py:1165 | pendiente |
| 11 | baja |  | deshacer.py solo mira que pushUndo aparezca en la funcion, no donde | Tests/deshacer.py | pendiente |
| 14 | baja |  | expo.py: paginas() ignora el filtro y SIN_DISENO exime dos pantallas enteras | Tests/expo.py (paginas(), SIN_DISENO) | pendiente, opinion |
| 16 | baja |  | kits.py y session.py usan una carpeta fija en build/ y dos corridas a la vez se pisan | Tests/kits.py:35 (.kits-test), Tests/session.py:34 (.session-test) | pendiente |
| 18 | baja |  | DEDO=40 escrito a mano en 5 bancos en vez de leerlo de Metrics::hit | Tests/clips.py:32, desglose.py:34, dinamica.py:37, eq.py:41, ranuras.py:50 | pendiente |
| 19 | baja |  | Bancos que dejan pasar 15 minutos sin imprimir nada | Tests/costuras.py (salida al final en 379), desglose.py; decodificador PNG en Python puro en costuras.py:106-136 | pendiente, opinion |
| 26 | baja |  | Avisos de clang en Source/: campo sin usar, variable muerta y operador coma | AudioFocus.h:115; Playlist.h:1076; MainComponent_Layout.cpp:5310 | pendiente |
| 29 | baja |  | Constantes muertas en MainComponent.h, duplicadas en SpectrumDisplay y Analizador | MainComponent.h:2305-2308 (kTauRetencionMs, kAvisoClipMs, kTauAnalizadorMs), 2668 (kStepCols); SpectrumDisplay.h:68-70; Analizador.h:38 | pendiente |
| 30 | baja |  | Teclado.h: kTeclas sin usar, literales 8/5/12 y el array blancas[8] dos veces | Source/Teclado.h:32, 67, 135 | pendiente |
| 32 | baja |  | RetireQueue y TablaQueueDe<T> son la misma cola escrita dos veces | Source/AudioEngine.h:2519 (RetireQueue, cap 128) y 2643 (TablaQueueDe<T>, cap 16) | pendiente |
| 35 | baja |  | 15 ganchos ZATI_ que no usa ningun test ni workflow | Source/Main.cpp y MainComponent*.cpp (getenv); Tests/, .github/ | pendiente |
| 28 | detalle |  | Conversiones con perdida y capturas de lambda sin usar | MainComponent_Audit.cpp:11599/11641; PadButton.h:403/409; Denoise.h:191; ZatiLookAndFeel.h:1880/2021; capturas en Main.cpp:899, MainComponen | pendiente |
| 31 | detalle |  | monoFont(11.0f) y withExtraKerningFactor(0.06f) escritos a mano en vez del token | MainComponent.cpp:8552, 9382; MainComponent_Layout.cpp:38, 2802, 5960 | pendiente |
| 36 | detalle |  | Mas de la mitad de las cabeceras son comentarios | MainComponent.h (59 %), AudioEngine.h (56 %), MainComponent_Layout.cpp (55 %), MainComponent.cpp (36 %) | pendiente, opinion |
| 37 | detalle |  | f-strings sin hueco e imports sin usar en los bancos | Tests/apk.py:330, Tests/paneles.py:523 y 6 imports | pendiente |

<details><summary>Arreglo propuesto de cada uno</summary>

- **codigo#0** Sumar bad += 1 en la rama de foto ausente (o sys.exit(1) si falta alguna de las esperadas). Vaciar o mover la carpeta de salida antes de lanzar y exigir que el numero de PNG nuevas sea el esperado (65).
- **codigo#1** Bisecar el commit que lo rompio con arr.py como oraculo. Revisar el redondeo del arrastre del asa en Playlist/auditArrange (probablemente un ceil o +1 al convertir px a compases) y no fusionar en verde mientras arr.py siga rojo.
- **codigo#2** Meter en banco.yml por lo menos feria, deshacer, proyecto, sel, limites, presets y guardado. Si alguno no puede correr en CI, anotarlo en una lista explicita dentro del workflow, con el motivo, y que un paso falle si aparece un Tests/*.py que no esta ni en CI ni en esa lista.
- **codigo#5** Leer el umbral de lo que publica la app (Metrics::hit) en vez de un literal, y si 16 filas no caben a 40 px, decidir el diseno (barra o paginas A/B) en vez de bajar el umbral. Cada cambio de umbral de un banco deberia pedir una nota con el motivo en el commit.
- **codigo#20** Partir por ficha: layoutPads(area), layoutMesa(area), layoutArrange(area)..., cada una de menos de 300 lineas, con resized() solo repartiendo areas. Mejor aun, pasar cada ficha a su propio Component con su resized().
- **codigo#21** Sacar un metodo cablea<Ficha>() por ficha, o mejor que cada ficha sea su propio Component con sus listeners.
- **codigo#22** Partir en fases con nombre (eventos MIDI, secuenciador, voces, buses de FX, master, limitador) como funciones inline o miembros que no reserven, y que StressTest juzgue cada fase por separado.
- **codigo#24** En fxParamDe, devolver 0.0f o un parametro nulo si f >= kNumFx o par >= 8 antes de indexar, y usar jassert en debug. Anadir a StressTest un caso de modulacion con destino fuera de rango.
- **codigo#3** Generar la tabla desde los propios tests (por ejemplo una cabecera '# toca: Source/X.h' en cada uno) y anadir un test estatico que compare la tabla con Tests/*.py. Quitar o crear BITACORA.md.
- **codigo#4** Pasar atasco.py y fps.py a medidas.yml, que es el workflow de lo que depende del reloj, o cambiar su criterio a cuentas que no dependan del tiempo (cuadros pintados, llamadas). Y corregir el comentario.
- **codigo#6** Exigir celda_8p >= hit con hit publicado por la auditoria. Para el ancho de 19 px, poner un minimo explicito (o justificar por escrito que la celda de paso se toca con zoom) en vez de comprobar solo que no cambie.
- **codigo#7** Publicar isVisible() y exigir visible y w*h > 0 en los dos estados antes de comparar. Publicar los limites como array JSON de enteros.
- **codigo#8** Quitarlos de banco.yml y mandarlos a medidas.yml como artefacto, o darles por lo menos una regla minima (por ejemplo, que la app arranque y publique N lineas).
- **codigo#9** Anadir la regla (contraste de beat contra la celda vacia >= el umbral del resto) o borrar el calculo y CELL_BEAT.
- **codigo#12** Imprimir y comprobar el total calculado (len(SIZES)*len(LANGS)*len(SHEETS)) y que el numero de corridas hechas lo iguale. Borrar las cifras escritas a mano en comentarios y en SKILL.md.
- **codigo#13** Comparar cada idioma con es (un texto igual al espanol en zh o ar es sospechoso salvo en la lista blanca). Sustituir la exencion por '/' por una lista explicita de siglas.
- **codigo#15** Usar 'with tempfile.TemporaryDirectory(prefix=...) as casa:' en corre(), o shutil.rmtree en un finally. Con ZATI_KEEP=1 se pueden conservar para depurar.
- **codigo#17** Crear Tests/comun.py con display_alive(), corre(size, **zati) -> list[dict] y DEDO leido de la app, e importarlo en todos los bancos.
- **codigo#23** Iconos: una tabla de icono -> funcion. StressTest: una funcion por veredicto registrada en un vector, para poder correr uno solo por nombre. applyState: un lector por bloque del estado.
- **codigo#25** Quitar -Wno-implicit-fallthrough ya. Para maybe-uninitialized, inicializar en vez de silenciar. Anadir un job con -Werror para Source/.
- **codigo#27** (int) std::llround ((double) s * (double) len), o trabajar en int64 desde el principio.
- **codigo#33** struct PadUi { ... }; std::array<PadUi, kNumPads>, con serializacion en un sitio.
- **codigo#34** Una opcion ZATI_AUDIT_BUILD en CMake que excluya MainComponent_Audit.cpp del APK de tienda, y macros que se vacien en release para los contadores.
- **codigo#10** Borrar la linea, o devolver la regla si sigue haciendo falta.
- **codigo#11** Exigir que la posicion de pushUndo sea menor que la de la primera llamada que muta, dentro del mismo bloque de llaves, y recorrer todos los Source/MainComponent*.cpp.
- **codigo#14** Aplicar el filtro en paginas(). Que SIN_DISENO imprima cuantas faltas se perdonan, para que se vea si crecen, o ponerle un tope.
- **codigo#16** tempfile.mkdtemp con limpieza, como en el resto.
- **codigo#18** Publicar Metrics::hit en la cabecera JSON de la auditoria y leerlo en comun.py.
- **codigo#19** Imprimir una linea por pantalla terminada y poner un tope global con un mensaje claro. Decodificar con zlib y memoryview por filas, o con PIL si esta disponible.
- **codigo#26** Quitar el campo y la variable. En 5310, escribir las dos llamadas como sentencias separadas o usar solo el resultado que se quiere.
- **codigo#29** Borrar las de MainComponent.h y kStepCols, y dejar una sola definicion compartida (Metrics.h o un Tiempos.h).
- **codigo#30** Una sola tabla static constexpr de blancas y negras, con los tamanos derivados de ella.
- **codigo#32** template <typename T, size_t N> struct ColaRetiro y dos alias. Factorizar los otros bloques en helpers.
- **codigo#35** Borrar los que no tienen uso o anadir un banco que los ejercite. Lo mejor es una tabla de ganchos con su banco y un test estatico que la compare con los getenv.
- **codigo#28** Casts explicitos con static_cast donde el rango esta acotado, quitar las capturas y generar FxPresets.inc con llaves dobles.
- **codigo#31** Un ZatiColours::monoTiny() o Metrics::kKern, y usarlo en todos los sitios.
- **codigo#36** La historia va en los mensajes de commit, y en el codigo solo el porque vigente. Hacer una pasada que borre los comentarios que contradicen el codigo.
- **codigo#37** Corregirlos y meter pyflakes en banco.yml para que no vuelvan.

</details>

## audio-dsp

**Lo que esta bien:**

- Tempo del secuenciador exacto: a 127 BPM, 64 negras en 30 s, el peor desvío frente a la rejilla ideal es 0,91 muestras (sonda/sonda2.log, línea TEMPO). Es mejor que el jitter de cualquier MIDI externo.
- El limitador LIM clava el techo: con un techo de -6 dB da un pico de 0,5012 desde los primeros 50 ms y la misma cifra en régimen, con H3/H1 de -82,2 dB (sonda2.log, línea LIM). No sobrepasa y no distorsiona.
- Los tiempos de DLY y PNG son exactos: ecos cada 250 ms con una caída geométrica limpia (0,453, 0,272, 0,163, 0,098) y un feedback acotado a 0,95, sin crecimiento.
- La reverb FDN es estable: no hay crecimiento de energía con el feedback por defecto, y StressTest pasa sus límites de NaN/inf.
- EQ de 5 bandas fiel al libro: la respuesta medida se separa menos de 0,024 dB de la teórica del biquad (StressTest).
- Filtro de pad TPT/SVF correcto y estable con modulación (AudioEngine.cpp:1263-1307).
- Hay guardas de denormales y NaN/inf en todo el camino. StressTest mide la guarda de denormales «33x» y pasan 167 de 168 reglas (audit/audio-dsp/stress/stress.log). La única que falla es de reloj, no de DSP.
- El AMB pone sus reflexiones donde debe a 48 kHz: el último eco cae en 212,3 ms, lo esperado con previo 120 + 92,3 (sonda2.log).
- El diezmador rechaza la imagen a -123 dB (StressTest).
- El autocorte del mismo pad (retrigger) funde sin clic, y el kit de fábrica suena sin clics en el banco de la casa.
- El duck de notificaciones del sistema rampa en 40 ms, sin escalón.
- El fader de master alcanza los +12 dB anunciados (kMasterMaxGain), y la fase del bucle es continua de vuelta a vuelta (sin deriva).
- La exportación es determinista y limpia en los extremos. BANCO_EXPORT_FUERTE/BANCO.wav acaba en 0,0 exacto, con un escalón máximo de 0,0105 en las últimas 2000 muestras, y la duración anunciada (2,5 s) coincide con la del fichero (ffprobe 2.500000 s). El clic final y el desajuste de duración que denunció la feria (J1/J2/veredicto punto 2) ya no se reproducen en este export.
- PISTAS ya no escribe ficheros de silencio. Con 60 pads mudos genera 5 ficheros y avisa: «5 archivos ... (60 pads mudos sin fichero)» (export/export.log). Eso cierra la queja J2/veredicto punto 7 sobre los 52 ficheros vacíos.
- La exportación mide la sonoridad, la comunica en el parte (-22,9 LUFS, -13,1 LUFS) y ffmpeg ebur128 la confirma (-22,9 y -13,1 LUFS I). También dice cuánto bajó: «bajado 2.2 dB para no saturar».
- DRV, FLD y BIT están sobremuestreados 2x con un halfband de 23 coeficientes (Sobre2x.h). No basta del todo (ver hallazgos), pero es mucho mejor que nada.

| # | gravedad | v | hallazgo | donde | estado |
|---|---|---|---|---|---|
| 0 | alta | si | La puerta GTE tiene ataque y cierre invertidos: abre con el tiempo de cierre (120 ms) y cierra con el de ataque (0,5 ms) | Source/Dinamica.h:274 (procesa, suavizado de ganancia) | arreglado (fb718ae) |
| 1 | alta | si | Un pad disparado con ganancia efectiva 0 (mute, solo de otro pad o gain 0) se queda mudo para siempre aunque se quite el mute o se suba el gain | Source/Voice.h:397 (stepUp = target/fadeIn) y Voice.h:480-498 (retarget); AudioEngine.h:820 (effectiveGain devuelve 0 con mute/solo) | arreglado (fb718ae) |
| 3 | alta | si | La amplitud por defecto (ancho 1) aplicada a muestras mono crea un filtro peine con 9,5 dB de desequilibrio L/R que cambia de lado cada 50 Hz | Source/Estereo.h (abre: kLadoMono 0.5, kAbreMs 10); MainComponent.cpp:206 (padAnchoUI.fill(1.0f)) y :14830 (setPadAncho(i, 1.0f)) | pendiente |
| 5 | alta | si | El swing máximo da la mitad de lo que anuncia: 100% = 62,5% del par (MPC: 75%), y 0,667 da 58,3%, no tresillo | Source/AudioEngine.cpp:1507-1509; AudioEngine.h:1499-1500 (kSwingRecto 0.50, kSwingRango 0.25); MainComponent.cpp:2833-2855 | pendiente |
| 6 | alta | si | El choke no corta: el pad ahogado suena con su propia suelta (un hat abierto de 600 ms sigue a -5 dB a los 50 ms) | Source/AudioEngine.cpp:476-478 (triggerPad, grupo de choke) | arreglado (fb718ae) |
| 7 | alta | si | El robo de voz por pad o por la más vieja reutiliza la misma Voice al instante: corte duro con clic (escalón x6,8) | Source/AudioEngine.cpp:710-719 | pendiente |
| 8 | alta |  | El efecto DUC (ducker LFO) da un escalón en cada vuelta de fase: clic periódico (x14,2) | Source/AudioEngine.cpp:3560-3587 | pendiente |
| 9 | alta |  | El efecto REP (repeat/stutter) salta sin fundido en cada repetición: clic de x52 sobre la pendiente natural | Source/AudioEngine.cpp:3601-3657 | pendiente |
| 12 | alta |  | El remuestreo y el rebote en vivo no llevan el bombeo ni el fader de master: lo grabado no es lo que suena | Source/AudioEngine.cpp:3971-4213 (orden: clipper de seguridad 3971-4025 → captura en vivo 4042 → grabar del master 4074 → duck de pad 4119-4 | pendiente |
| 15 | alta |  | PNG (ping-pong) no hace ping-pong con fuente centrada: ecos idénticos L/R, igual que DLY | Source/AudioEngine.cpp:3844-3891 (`aL = w0 + vR*fb; aR = w1 + vL*fb`) | pendiente |
| 16 | alta |  | La reverb REV da salida mono con fuente centrada (L-R = 0 exacto) | Source/Fdn.h (mismas longitudes de línea por canal); AudioEngine.cpp:3771 | pendiente |
| 17 | alta |  | El excitador EXC atenúa los agudos fuertes en lugar de realzarlos (-3,5 dB en la fundamental a 6 kHz) | Source/AudioEngine.cpp:3014-3052 (`y = b + a + fuerza*(sat - a)*4`) | pendiente |
| 18 | alta |  | Los LFO sincronizados (DUC, TRM, etc.) no se anclan al transporte: su fase depende de cuánto estuvo parado el motor | Source/AudioEngine.cpp:269-270 (único reset de relojSeg/relojPaso, en prepareToPlay) y 1953-1954 (avanzan incluso parado); AudioEngine.h:319 | pendiente |
| 2 | media | si | Subir el volumen de un pad tras un ataque largo tarda segundos (rampa heredada del ataque) | Source/Voice.h:397 y 480-498 | pendiente |
| 4 | media | si | El remuestreo (resample) de un pad estéreo centrado se guarda en mono y al reproducirlo sale descentrado (L -9,8 / R -13,1 dB) | Source/AudioEngine.cpp:5347-5382 (finishRecording guarda L==R como 1 canal) + Estereo.h abre | pendiente |
| 10 | media |  | FRZ (freeze) da un escalón en cada vuelta: el crossfade vuelve a p=0 en lugar de p=cruce | Source/AudioEngine.cpp:3124-3222 | pendiente |
| 11 | media |  | El duck de pad (bombeo sidechain) salta a la profundidad máxima en una muestra: clic en cada golpe | Source/AudioEngine.cpp:727-728 (`if (slot == duckPad) duckEnv = 1.0f;`) y 4119-4139 | pendiente |
| 13 | media |  | La exportación no aplica el fader de master y su cabecera dice «exactamente lo que oyes» | Source/Exporter.h (comentario de cabecera; master del usuario no copiado; limitador de seguridad apagado en :452) | pendiente |
| 14 | media |  | El techo de exportación es de pico de muestra: el true peak llega a -0,6 dBTP (la feria pidió margen por J2 y sigue sin -1 dBTP) | Source/Exporter.h:277-282 (kTecho = 0.8912509f, -1 dBFS sobre el pico de muestra del pase 1) | pendiente |
| 19 | media |  | El mando MIX de los efectos no tiene rampa: escalón (clic) en la frontera de bloque | Source/AudioEngine.cpp:2137 y 2188 (`const float mix = jlimit(0,1,P(f,2)) * canalSend...`), suma en 2227 | pendiente |
| 20 | media |  | CHO, FLA y PHA cambian el nivel con mix 1 (+4 dB en graves para CHO, +3,65 dB en 1 kHz para FLA) y TRM baja -3,65 dB | Source/AudioEngine.cpp:2227 (`w[i] = d[i] + mix*w[i]` para inserciones con fxSustituye=false), 2609 (CHO), 2663 (FLA), 2716 (PHA), 2771 (TRM | pendiente |
| 21 | media |  | El largo de nota (gate del paso) se cuantiza al bloque: con buffer de 512 o 1024 una nota de 1500 muestras dura 1049 (-475 muestras, -32%) | Source/Voice.h:510-514 (cuenta atrás del gate decrementada por llamada de render, no por muestra) | pendiente |
| 22 | media |  | En reversa, el fade-in queda al final del sonido y el arranque suena a nivel pleno (clic) | Source/Voice.h:575-597 (bordeGain anclado a winStart sin tener en cuenta la dirección) | pendiente |
| 23 | media |  | Un bucle sin fundidos explícitos se hunde -7,4 dB en cada vuelta por los 3 ms automáticos a cada lado | Source/Voice.h:354-361 | pendiente |
| 24 | media |  | El cambio de tono sube aliasing audible: +12 st sobre 15 kHz deja la imagen plegada a 18 kHz a -13,7 dBFS (varispeed) y -10,4 dBFS (modo TONO) | Source/Voice.h:67-92 (updateAntiAlias: un solo polo) y 653-798 (camino granular sin filtro AA) | pendiente |
| 25 | media |  | El compresor CMP no tiene ganancia de compensación: con los valores por defecto baja -8,35 dB | Source/Dinamica.h (sin makeup) | pendiente |
| 26 | baja |  | El limitador de seguridad de la salida es un clipper (tanh por encima de -0,5 dBFS): distorsión de H3 -17,8 dB e imágenes no armónicas | Source/AudioEngine.cpp:3971-4025 | pendiente |
| 27 | baja |  | AMB trunca sus reflexiones a 192 kHz: el último eco cae a 170,7 ms en vez de 212,3 ms | Source/AudioEngine.cpp:3799 (ambLine limitado a jmin(32767, fs*0.5)) | pendiente |
| 28 | baja |  | OCT: el coeficiente del bloqueador de DC (0,0008 por muestra) depende de la frecuencia de muestreo, y la onda cuadrada sin banda limitada genera aliasing | Source/AudioEngine.h (helper octava, zona 1690-1930); AudioEngine.cpp:3328 | pendiente |
| 29 | baja |  | Los coeficientes del EQ5 cambian de golpe al mover un mando, y una banda que vuelve de «plana» arranca con estado viejo | Source/Eq5.h (cálculo de coeficientes sin interpolación; la banda en estado plano se salta y conserva z1/z2) | pendiente |
| 30 | baja |  | El 2x de DRV, FLD y BIT deja aliasing de -35,6, -27,2 y -21,7 dB | Source/Sobre2x.h (halfband de 23 coeficientes, factor 2); AudioEngine.cpp:2369-2430 (DRV), 3431 (FLD), 2433 (BIT) | pendiente |
| 32 | baja |  | Una regla de StressTest depende del reloj y falla de forma intermitente («exportar con AUTO sin carrera») | Tests/StressTest.cpp (zona 4560-4597) | pendiente |
| 33 | baja |  | Pan de pads estéreo: al centro baja -3 dB (ley de igual potencia sobre balance) y a tope pierde un canal entero | AudioEngine.cpp (pan cos/sin aplicado a cada canal del pad) | pendiente, opinion |
| 31 | detalle |  | Cada muestra pierde su primera muestra y las 2 últimas (winStart ≥ 1, winEnd ≤ srcLen-2) | Source/Voice.h:277-278 | pendiente |
| 34 | detalle |  | La FDN de REV tiene baja densidad (4 líneas y 2 difusores): cola granulada con percusión | Source/Fdn.h | pendiente, opinion |
| 35 | detalle |  | PHA calcula tan() y pow() por muestra (coste estático evitable) | Source/AudioEngine.cpp:2716 (PHA) | pendiente |
| 36 | detalle |  | Comentarios que contradicen el comportamiento (swing de tresillo, reloj del LFO, rebote en vivo «con el limitador») | MainComponent.cpp:2833-2855 (0,667 «tresillo»); AudioEngine.h:3180 («negras desde que arranco el transporte»); Exporter.h cabecera («exactam | pendiente |

<details><summary>Arreglo propuesto de cada uno</summary>

- **audio-dsp#0** En Dinamica.h:274, para modo==puerta, usar aA cuando g > gSuave (abrir) y aR cuando g < gSuave (cerrar). Mantener la lógica actual para compresor y deesser. Añadir una regla en StressTest que mida el 90% de apertura en menos de 5 ms con el ataque por defecto.
- **audio-dsp#1** En Voice::retarget, recalcular el paso de subida cuando el objetivo cambia, p. ej. `stepUp = max(stepUp, (target - gain)/max(1, muestrasRestantesDeAtaque))` o, fuera del ataque, una rampa fija de 10 ms igual que la bajada (stepCtl). Así una voz arrancada a 0 sube al quitar el mute.
- **audio-dsp#3** Valor por defecto de ancho 0 para muestras de 1 canal (o aplicar la apertura solo por encima de unos 300 Hz con un filtro de paso alto en el lado). Usar un decorrelador all-pass o retardos cortos distintos en L/R en lugar de M±S con un solo retardo de 10 ms. Como mínimo, no fabricar lado por debajo de 150-200 Hz.
- **audio-dsp#5** Quitar el `* 0.5` de AudioEngine.cpp:1508 (lateBy = (sw-0.5)*2*samplesPerStep). Revisar el comentario de MainComponent.cpp:2833-2855 y la etiqueta para que 66,7% sea tresillo real. Añadir una regla en StressTest que mida la posición del golpe impar.
- **audio-dsp#6** Cambiar v.release() por v.steal(systemSampleRate) o por un fundido dedicado de 5-10 ms (`v.choke()`), independiente de la suelta del pad.
- **audio-dsp#7** Mantener la voz robada en fundido (estado `stealing`) y arrancar la nueva en otra Voice. Para ello, reservar 1-2 voces fantasma solo para fundidos de robo, o hacer que Voice tenga una cola de fundido separada (sumar las últimas N muestras de la vieja con rampa).
- **audio-dsp#8** Añadir un ataque corto (2-5 ms) al principio del ciclo, p. ej. g = 1 - prof*min(1, fase/fA)*(1-fase)^2, o suavizar g con un one-pole de 2 ms.
- **audio-dsp#9** Aplicar un fundido de 2-5 ms (coseno) en cada vuelta del fragmento repetido y en la entrada y salida del efecto.
- **audio-dsp#12** Mover la captura en vivo y la grabación desde el master al final de la cadena (después del duck y de la ganancia de master), o como mínimo después del duck. Corregir el comentario de RebotVivo para que diga lo que hace.
- **audio-dsp#15** Alimentar solo una línea con la suma: aL = (w0+w1)*0.5 + vR*fb; aR = vL*fb (ping-pong clásico), con opción de lado inicial.
- **audio-dsp#16** Usar longitudes distintas por canal (sumar un spread de unas 20-40 muestras en R) o sacar L y R de combinaciones ortogonales de las 4 líneas (p. ej. L = l0+l2, R = l1+l3). Objetivo: coherencia interaural menor que 0,3.
- **audio-dsp#17** Sumar solo los armónicos generados y con el signo correcto: y = b + a + fuerza*k*HPF(sat(a*drive) - a*drive*g), o y = b + a + fuerza*HPF(sat(a)), filtrando la parte de la fundamental. Comprobar que la fundamental no cae más de 0,5 dB.
- **audio-dsp#18** Derivar la fase de pasoMod de la posición del transporte (paso actual + fracción) mientras suena, y reiniciar relojSeg/relojPaso en PLAY. Corregir el comentario de AudioEngine.h:3180.
- **audio-dsp#2** Separar la rampa de control de la de ataque. Una vez acabado el ataque, retarget debe usar la misma rampa de 10 ms (stepCtl) en ambos sentidos.
- **audio-dsp#4** Al detectar L==R en finishRecording, marcar el pad con ancho 0 (o guardar los 2 canales). Mejor aún, arreglar el hallazgo del peine mono y que ancho 0 sea el valor por defecto en mono.
- **audio-dsp#10** Al cerrar el ciclo, saltar a p = longitudCruce (donde el crossfade termina) y no a 0, para que la cabeza continúe donde la cola mezclada acaba. Verificar con la sonda que el salto queda por debajo de 2x la pendiente natural.
- **audio-dsp#11** Hacer que duckEnv persiga 1 con un ataque de 1-3 ms (duckObjetivo = 1 y un one-pole) en lugar de asignarlo directamente.
- **audio-dsp#13** O aplicar el master al rebote (normalizando después), o decirlo en la ficha de exportar («el fader de master no se aplica: el fichero se normaliza a -1 dB») y corregir el comentario de cabecera de Exporter.h.
- **audio-dsp#14** En el pase 1, medir el true peak con sobremuestreo 4x (el FIR de Sobre2x en cascada, o un polifásico de 4 fases y unos 48 coeficientes, como BS.1770-4 anexo 2) y normalizar con kTecho sobre ese valor.
- **audio-dsp#19** Suavizar mix por muestra con un SmoothedValue o con una interpolación lineal desde mixPrev hasta mix a lo largo del bloque, en las dos ramas (sustituye/envío).
- **audio-dsp#20** Para CHO/FLA/PHA, usar mezcla equal-gain (d*(1-mix*0.5) + mix*0.5*w) o compensar 1/(1+mix) aproximado. En TRM, normalizar la media de la envolvente a 1 (o centrar el LFO en 1 - prof/2 con compensación).
- **audio-dsp#21** Llevar la cuenta del gate en muestras dentro del bucle de render de la voz y lanzar release() en la muestra exacta (o pasar un offset de release a la voz al programarla).
- **audio-dsp#22** En bordeGain, medir la distancia desde el punto de inicio de la reproducción (winEnd en reversa) para el fade-in y desde el final para el fade-out.
- **audio-dsp#23** Sustituir fade-out más fade-in por un crossfade de igual potencia de 3-10 ms entre el final del bucle y el principio (leer las muestras de antes de loopStart). No atenuar las dos caras.
- **audio-dsp#24** Usar un filtro de interpolación con banda limitada según el ratio (sinc con ventana de 8-16 coeficientes, o un FIR polifásico) o, más barato, un biquad de paso bajo de 4.º orden en cascada a fc = 0,45·fs/ratio. Aplicar también el filtro en el camino granular.
- **audio-dsp#25** Añadir un parámetro MAKEUP (0-24 dB) o auto-makeup = -(umbral·(1-1/ratio))/2 en Dinamica para el modo compresor.
- **audio-dsp#26** Poner delante un limitador con lookahead de 1-2 ms y release unos 50 ms (el mismo Dinamica en modo limitador con un buffer de retardo), dejando el tanh solo como red de seguridad. Si se mantiene el clipper, sobremuestrearlo 2x con Sobre2x.
- **audio-dsp#27** Dimensionar ambLine según fs (ceil(0,25·fs) + margen) en prepareToPlay, sin el tope de 32767 (usar int y no int16 para los índices).
- **audio-dsp#28** Calcular el coeficiente como 1 - exp(-2π·fc/fs) con fc = 5 Hz en prepareToPlay. Suavizar los flancos con polyBLEP o filtrar con un paso bajo de 2.º orden según la nota.
- **audio-dsp#29** Interpolar los coeficientes por bloque (o usar la topología SVF/TPT, ya presente en el filtro de pad, que tolera modulación). Al saltar una banda, poner z1 = z2 = 0 o seguir procesándola con ganancia 0 dB.
- **audio-dsp#30** Pasar a 4x (cascada de dos Sobre2x) para DRV y FLD a partir de drive > 0,5, o usar ADAA de primer orden en tanh y pliega. En BIT el aliasing es parte del carácter: documentarlo.
- **audio-dsp#32** Contar iteraciones fijas (p. ej. 50 clones con barrera) en lugar de una ventana de tiempo, y que el criterio sea solo «0 tablas a medias».
- **audio-dsp#33** Para pads de 2 canales, usar balance con 0 dB en el centro (gL = min(1, 2·(1-p)), gR = min(1, 2·p)) o un pan estéreo que mezcle R en L al girar (gL_R = sin, etc.). Mantener cos/sin solo para mono.
- **audio-dsp#31** Permitir winStart = 0 y winEnd = srcLen, rellenando el hermite4 con ceros o con la muestra de borde (clamp) en lugar de recortar la ventana.
- **audio-dsp#34** Pasar a 8 líneas (Hadamard 8x8, coste muy bajo) y 4 difusores de entrada, con longitudes mutuamente primas y modulación ligera de 1-2 muestras.
- **audio-dsp#35** Calcular los coeficientes cada 16-32 muestras e interpolar linealmente, o usar una tabla de tan(πf/fs) con interpolación.
- **audio-dsp#36** Corregir los cuatro comentarios al arreglar cada defecto, o ya si el comportamiento se mantiene.

</details>

## textos

**Lo que esta bien:**

- Tests/lang.py pasa ('la tabla esta completa'). Comprueba que no haya filas duplicadas, que toda fila tenga en/zh/ar y que la clave sin tilde lleve su celda es: 1014 filas en Source/Lang.cpp (kTable), parseadas a textos/tabla.tsv.
- El español visible lleva casi siempre sus tildes gracias a la columna es (GUIA→GUÍA, Lang.cpp:873; BUFER→BÚFER, 2300; STEREO→ESTÉREO, 796). Pasé pyspellchecker (textos/pylib) por todas las celdas es y no encontró tildes que falten en la tabla.
- La mayoría de lo que señaló la feria está arreglado: «LOAD armado», «ESPANOL» (ahora ESPAÑOL), carcasas sin traducir, SOLO en zh/ar, «SIN GUARDAR» y la ortografía de COWBELL.
- Los mensajes con variables usan %1/%2 en vez de concatenar, y las cifras y nombres latinos dentro de texto árabe van envueltos en Lang::ltr (LRI/PDI). En la barra de estado y en los títulos de ficha en árabe las cifras salen en su orden (fotos textos/fotos/ar_*.png).
- La cara de la máquina se respeta en árabe sin espejarse, como dice Lang.h, y los bancos A→D se ordenan bien (foto textos/fotos/ar_set_393x851.png frente a es_set).
- Las cifras de la guía coinciden con el código en casi todo: 16 deshacer (kUndoDepth=16), ganancia de -60 a +12 dB (MainComponentInterno.h:69-70), medidor amarillo en -12 y rojo en -3 (SpectrumDisplay.h:667), pad 1 = nota 36 (MidiIo.h:47), 8 patrones, 4 bancos de 16 pads.
- No hay rótulos cortados a 393x851 en en/es/ar. El volcado ZATI_AUDIT de 61 fichas no da ningún 'pide' mayor que el ancho ni ningún needW mayor que haveW. El único apretón (0.75) es el estilo de sección, igual para todos.
- Las fotos en español y en inglés se ven limpias y coherentes en cara, sec, pads, set, expo y manual.

| # | gravedad | v | hallazgo | donde | estado |
|---|---|---|---|---|---|
| 0 | alta | si | ATAQUE, CAIDA e INICIO de la tira del paso no tienen fila en la tabla: salen sin tilde y sin traducir | Source/MainComponent_Layout.cpp:7646; pintado en Source/MainComponent_Paint.cpp:2880 | arreglado (7499333) |
| 1 | alta | si | El bloqueo de corte del filtro usa la clave «CORTE» de cortar selección: en inglés dice CUT y en chino 切断 | Source/MainComponent_Layout.cpp:7634; Source/Lang.cpp:451 frente a 297 | arreglado (7499333) |
| 2 | alta | si | La barra de estado enseña líneas crudas de la bitácora: español sin tildes e identificadores internos en todos los idiomas | Source/MainComponent.cpp:21457-21467; formato en Source/Bitacora.h:175, 240, 265-277 | pendiente |
| 3 | alta | si | Los nombres de los presets de efecto están en español y no se traducen; además hay dos sin tilde (AVION, MAS GOLPE) | Source/FxPresets.inc:44-200 (CIERRA, TAPA, TELÉFONO, SE VA, GOLPE…); FxPresets.h:68 'DEFECTO'; FxPresets.inc:131 y 180 | pendiente, opinion |
| 4 | alta | si | Bidi árabe: una cadena que empieza por palabra latina toma dirección LTR y la palabra latina se va al otro extremo | Source/Lang.cpp:1074 (REC …) y todas las celdas ar que empiezan por AUTO, PACK, TAP, XY, LOAD, MIX… | pendiente |
| 6 | alta |  | Árabe: PRESETS/PRESET y AJUSTES comparten «إعداد» | Source/Lang.cpp:98, 104; 1058, 1066 | pendiente |
| 7 | alta |  | Árabe: REMUESTREAR y REHACER son la misma palabra «إعادة» | Source/Lang.cpp:771 frente a 30 | pendiente |
| 10 | alta |  | Árabe: términos técnicos traducidos con otro sentido (fader, metrónomo, semitonos, semicorchea, compás) | Source/Lang.cpp:894 (مزلاج), 950 (المسرّع), 1656 (أنصاف), 1845 (سُداسية عشرية), 768 (تنازلي / الميزان) | pendiente |
| 11 | alta |  | Árabe: el mismo concepto con varios nombres (pad, mezclador, rack, ranura, efecto, nota, carcasa, solo, bloque) | Source/Lang.cpp, columna ar (pad: 107 y 2023/2025; ranura: 1390; efecto: 2079, 2189; bloque: 1446; carcasa frente a envolvente: 946, 1187, 1 | pendiente |
| 15 | alta |  | Chino: «patrón» con cinco términos, y PATRONES choca con SEC | Source/Lang.cpp, columna zh; 2287 frente a 20 | pendiente |
| 16 | alta |  | Chino: banco, kit, proyecto, pad, master y biblioteca con dos o más nombres | Source/Lang.cpp, columna zh: 1594 (BANCO=乐句库), 1334 (音垫组), 1672 (音色库), 1776 (PROYECTOS=工程), 2023/2025 (打击垫), 2180 (MASTER=总输出) | pendiente |
| 5 | media | si | RTL en AJUSTES: el rótulo pasa a la derecha pero las fichas siguen en orden LTR y se leen al revés | Pestaña AJUSTES (ficha set), filas CUENTA y TOMAS; caja de info AUDIO/EQUIPO | pendiente |
| 8 | media |  | Árabe: preset traducido como «النمط» (patrón) en dos frases | Source/Lang.cpp:1155, 1159 | pendiente |
| 9 | media |  | Árabe: «fuera» (barrido apagado) traducido como «خارج», que significa «afuera» | Source/Lang.cpp:842; visible bajo BARRIDO en la cara | pendiente |
| 12 | media |  | Árabe: «dB» y «ms» traducidos o truncados, contra la norma de Lang.h | Source/Lang.cpp: 6 celdas con «ديسيبل» frente a 655 («dB»); 2354, 2355, 2360 («مللي») | pendiente |
| 13 | media |  | Árabe: TEMPO, RELOJ, BUFER, QUITAR RUIDO, RELEASE y SONIDO con palabras de otro sentido o incoherentes | Source/Lang.cpp:1592 (TEMPO), 1090, 2301 (RELOJ), 2300 (BUFER), 454 frente a 590, 295 frente a 860, 2124 | pendiente |
| 14 | media |  | Árabe: plurales mal concordados con el número («18 فصول», «%1 نغمات») | Source/Lang.cpp:371 y celdas con «%1 ملفات»; título del manual | pendiente |
| 17 | media |  | Chino: palabras que chocan o no dicen lo que hacen (CADENA, RECORTE/RECORTAR, TAP/GOLPES, PASOS, REC/TOMAS/GRABAR MIC) | Source/Lang.cpp:1283, 275 y 459, 1593, 1271, 25 / 1612 / 761 | pendiente |
| 18 | media |  | Chino: el botón y la guía usan palabras distintas para lo mismo (BOMBEO, CUADRAR, GRABAR MIC, CHOKE) | Source/Lang.cpp:446 frente a la guía 934/946; 265 frente a la guía; 761 frente a 986 | pendiente |
| 20 | media |  | Inglés: la pestaña se llama SETUP en unos textos y SETTINGS en otros | Source/Lang.cpp:23, 705, 900, 906 (SETUP) frente a 562, 962, 1349 (SETTINGS) | pendiente |
| 21 | media |  | Inglés: el botón y la guía usan palabras distintas (STEMS/TRACKS, SLICES/PIECES, DUCK/PUMP, TONE/PITCH, QUANT/QUANTIZE, MIC REC/REC MIC, RECORD/REC, ZOOM/magnifier) | Source/Lang.cpp:2181 frente a 781, 2221 y 1454; 1736; 446 frente a 934/946; 440 frente a 946; 265; 761 frente a 986; 766 frente a 994; 1300  | pendiente |
| 24 | media |  | El chip OFF de CUENTA sale «NO» en español, pero la guía dice «de OFF a dos compases» | Source/MainComponent.cpp:893-896; Source/Lang.cpp:811 y 903 | pendiente |
| 25 | media |  | Español: CAÍDA vale a la vez para RELEASE y DECAY, y SOLTAR/SUELTA también son RELEASE | Source/Lang.cpp:295, 860, 2245 (RELEASE) frente a 1185 (DECAY) | pendiente |
| 26 | media |  | Español: FUERZA significa AMOUNT y también velocidad, y la guía no cita las etiquetas reales del paso | Source/Lang.cpp:1648, 1409, 1550; etiquetas en Source/MainComponent_Layout.cpp:7630-7634 | pendiente |
| 27 | media |  | Español: la ficha del pad se llama «ficha» o «sus ajustes», y AJUSTES es también la pestaña | Source/Lang.cpp:174, 1564 | pendiente |
| 32 | media |  | Español: mensaje críptico «Tocando solo sales %1 ms» | Source/Lang.cpp:2360 | pendiente |
| 35 | media |  | La guía dice zoom «hasta x64» y el código llega a x128 | Source/Lang.cpp:572; Source/WaveformDisplay.h:187 y 202 | pendiente |
| 36 | media |  | La ayuda de exportar dice WAV aunque esté elegido OGG, y lleva doble espacio | Source/MainComponent_Paint.cpp:1312; Source/MainComponent.cpp:2023 (exportOgg) | pendiente |
| 37 | media |  | Nombres por defecto sin traducir: PROYECTO n, TOMA n, SIN NOMBRE | Source/MainComponent.cpp:1083, 21560, 21725; Source/ProjectStore.h:383 | pendiente |
| 44 | media |  | Tests/lang.py no ve las claves que llegan por variable | Tests/lang.py | pendiente |
| 19 | baja |  | Chino: «%1 en %2» traducido como «%1 个，位于 %2» | Source/Lang.cpp, fila «%1 en %2» | pendiente, opinion |
| 22 | baja |  | Inglés: modismo inventado y mayúscula inicial incoherente | Source/Lang.cpp:569; 266, 268, 283, 285, 447, 449, 460, 464, 468, 472, 483, 486, 492, 504, 506, 710-714, 789, 791 | pendiente |
| 28 | baja |  | Español: «maestro» en una frase frente a MASTER en todo lo demás | Source/Lang.cpp:894 | pendiente |
| 29 | baja |  | Español: la pestaña PRUEBAS tiene dentro un botón PRUEBA, y CUADRAR no es una prueba | Source/Lang.cpp:52 (PRUEBAS) y fila PRUEBA | pendiente, opinion |
| 30 | baja |  | Español: «¿BORRA TODO?» frente a «¿BORRAR %1?», «SACARLO» frente a «SÁCALO», y comillas rectas mezcladas con «» | Source/Lang.cpp:1803; 1346 frente a 1523; 1815-1829 | pendiente |
| 38 | baja |  | La onda pinta STEREO/MONO y unidades sin T(), aunque la tabla tiene ESTÉREO | Source/WaveformDisplay.h:59-62; Source/Lang.cpp:796 | pendiente |
| 39 | baja |  | El detalle de carga «2ch 44100Hz» va pegado y sin traducir | Source/SampleLoader.cpp:195-196 | pendiente |
| 40 | baja |  | Unidades con formato incoherente en el medidor: «-12dB» sin espacio y «BPM» frente a «bpm»; «LIM» suelto | Source/SpectrumDisplay.h:629, 635, 467, 323; MainComponent.cpp:2424; Layout:7555 | pendiente |
| 42 | baja |  | Accesibilidad: títulos de componente sin traducir (setTitle("Pad "+n), ST/MO) | Source/MainComponent.cpp:9936, 17350 | pendiente |
| 43 | baja |  | El título de exportar MIDI pega las palabras: «MIDIPATTERN 1PAD A01» | Ficha midf (título de la ficha de exportar MIDI) | pendiente |
| 23 | detalle |  | Inglés: comillas españolas «+» dentro de texto inglés, y una clave inglesa que no dice lo mismo que el español | Source/Lang.cpp:1390, 797 | pendiente |
| 31 | detalle |  | Español: «MANUAL» frente a «GUÍA», y la fila MANUAL no tiene cliente | Source/Lang.cpp:1579 frente a 873 | pendiente |
| 33 | detalle |  | Español: «renderizando...» es spanglish, y los tres puntos van como caracteres sueltos | Source/Lang.cpp:2183 | pendiente, opinion |
| 34 | detalle |  | Español: mayúsculas mezcladas en avisos del mismo tipo («sin dispositivo de audio» frente a «SIN AUDIO») | Ficha set; filas de Lang.cpp con «sin dispositivo de audio» y «SIN AUDIO» | pendiente |
| 41 | detalle |  | Porcentaje con y sin espacio en la misma ficha: «0 %» frente a «100%» | Ficha del pad (pads); valores de RESON/ANCHO y GOLPE | pendiente |

<details><summary>Arreglo propuesto de cada uno</summary>

- **textos#0** Añadir a kTable: { "ATAQUE","","ATTACK","起音","الهجوم" }, { "CAIDA","CAÍDA","DECAY","衰减","الخفوت" } e { "INICIO","","START","起点","البداية" }. Que Tests/lang.py busque además los literales que entran en arrays const char* que acaban en T().
- **textos#1** Usar T("CORTE|filtro") en Layout:7634 y en el nameBand equivalente (Layout:7989). Revisar que ningún otro rótulo del filtro use la clave desnuda.
- **textos#2** Guardar en la bitácora un código de tarea y traducirlo al enseñarlo: un mapa tarea→clave T("cargando el banco"…), con el número fuera del texto. Que el usuario lea «Last time it froze for 13 s while loading the factory bank». La línea cruda puede quedarse en AJUSTES · AUDIO para soporte.
- **textos#3** Pasar los nombres por T() con contexto (T("SE VA|preset")) y añadir sus filas, o dar cada preset un id y un nombre por idioma. Como mínimo, traducir DEFECTO (DEFAULT/默认/افتراضي), que es interfaz y no nombre. Corregir AVIÓN y MÁS GOLPE.
- **textos#4** Forzar dirección RTL en los textos árabes: anteponer U+200F (RLM) o envolver en RLE/PDF (U+202B…U+202C) en Lang cuando el idioma es ar. O pasar juce::AttributedString con ReadingDirection::rightToLeft en el pintado del manual y de las ayudas. Añadir a Tests/lang.py una regla: celda ar cuyo primer carácter fuerte sea latino → fallo, salvo envuelta.
- **textos#6** PRESET→«قالب جاهز» o «مسبق الضبط»; PRESETS→«القوالب الجاهزة». Reservar «الإعدادات» para AJUSTES.
- **textos#7** REMUESTREAR→«إعادة أخذ العينة» (o «تسجيل داخلي» si no cabe); REHACER→«إعادة الخطوة» o «تكرار».
- **textos#10** fader→«منزلق الصوت» o «فيدر»; metrónomo→«المترونوم» o «ضابط الإيقاع»; SEMIS→«أنصاف نغمة»; semicorchea→«نوتة سادسة عشرية» (la «نوتة» es la que da el sentido musical); cuenta atrás→«عد تنازلي»; compás→«مازورة» en toda la tabla.
- **textos#11** Fijar un glosario ar (pad=باد, mezclador=المازج, rack=الراك, ranura=خانة, efecto=مؤثر, nota=نوتة, carcasa=الواجهة, envolvente=الغلاف, solo=منفرد, bloque=المقطع) y que Tests/lang.py avise cuando un sinónimo prohibido aparezca en la columna ar.
- **textos#15** Glosario zh: patrón=样式 (pattern) o 乐句, uno solo; SEC=音序器. Sustituir en las 34 celdas.
- **textos#16** Glosario zh: banco=音色库, kit=套件, proyecto=工程, pad=打击垫 (o 音垫, uno solo), master=总输出, biblioteca=素材库. Una regla en lang.py que rechace los sinónimos.
- **textos#5** Cuando Lang::rtl(), invertir el orden de colocación de los chips de cada fila (empezar por la derecha) y justificar a la derecha las etiquetas y avisos de la caja de info, como ya se hace con el rótulo.
- **textos#8** Usar el mismo término elegido para PRESET (ver el hallazgo anterior).
- **textos#9** Usar «مغلق» o «إيقاف», igual que «off» en 811.
- **textos#12** Dejar «dB» y «ms» en latín, envueltos en Lang::ltr, en todas las celdas ar. Añadir a Tests/lang.py la regla: «ديسيبل» o «مللي» en una celda ar → fallo.
- **textos#13** TEMPO→«السرعة» o «التيمبو»; RELOJ→«مزامنة الساعة»; BUFER→«المخزن المؤقت»; QUITAR RUIDO→«إزالة الضجيج» en los dos sitios; RELEASE→«التلاشي» en los dos; ONE SHOT→«تشغيل مرة واحدة».
- **textos#14** Añadir a Lang una forma de plural por categorías CLDR (zero/one/two/few/many/other para ar) o reescribir la frase para evitar contar («الفصول: %1»).
- **textos#17** CADENA→«链式»/«串联»; TRIM→«修剪», CROP→«裁剪»; TAP→«敲击测速»; PASOS→«步»; TOMAS→«录音片段», GRABAR MIC→«麦克风录音», REC→«录音».
- **textos#18** Hacer que la guía cite exactamente el texto del botón: 闪避, 对齐 (o cambiar el botón a 量化) y 麦克风录音. CHOKE→«组切» en todas.
- **textos#20** SETUP para la pestaña en todas las celdas; «sheet» (o «card») como único nombre de ficha.
- **textos#21** Que cada guía cite el rótulo exacto del botón entre mayúsculas: STEMS, SLICES, DUCK, TONE, QUANT, MIC REC, RECORD y ZOOM.
- **textos#24** Clave propia T("APAGADO|cuenta") (es «NO» o «APAGADA», en OFF, zh 关, ar إيقاف), y que la guía cite ese mismo texto.
- **textos#25** DECAY=CAÍDA y RELEASE=SUELTA (o COLA), en todas las filas.
- **textos#26** Reescribir 1409 y 1550 con los rótulos exactos (GOLPE, REPETIR, CORTE) y quitar FUERZA como sinónimo de velocidad.
- **textos#27** «abre su ficha» en 174 y 1564, y en en «opens its sheet».
- **textos#32** «Latencia al tocar: %1 ms (lo que tarda en sonar el pad)», en «Play latency: %1 ms».
- **textos#35** Cambiar la guía (en los 4 idiomas) y el comentario a x128, o generar la cifra con %1 desde kMaxZoom.
- **textos#36** T("MASTER = un %1 de lo que oyes. PISTAS = …un %1 por pad", exportOgg ? "OGG" : "WAV") con un solo espacio. En inglés STEMS en vez de TRACKS.
- **textos#37** T("PROYECTO %1", n), T("TOMA %1", n) y T("SIN NOMBRE"), con sus filas (PROJECT/工程/مشروع, TAKE/录音片段/تسجيل, UNTITLED/未命名/بلا اسم).
- **textos#44** Añadir a lang.py una pasada dinámica: arrancar con ZATI_AUDIT=1 ZATI_LANG=en sobre todas las fichas y dar fallo por cualquier rotulo o text que coincida con una palabra española de la clave o tenga letras con tilde. Mi barrido lo hace en 61 fichas: textos/dump/.
- **textos#19** «%2 中的 %1» o «%1（%2）».
- **textos#22** 569→«tap an empty part of the wave». Capitalizar la primera letra de toda frase completa en en, o fijar la regla contraria y aplicarla igual a todas.
- **textos#28** Cambiar a «MASTER».
- **textos#29** Renombrar el botón a «PROBAR SONIDO» (en «PLAY TEST», zh «试听») y sacar CUADRAR de esa pestaña o renombrarla «HERRAMIENTAS».
- **textos#30** «¿BORRAR TODO?», «SÁCALO DE AQUÍ» en las dos y «%1» con comillas angulares en es.
- **textos#38** T("STEREO") y T("MONO") con Lang::ltr para las cifras y las unidades.
- **textos#39** T("%1 canales · %2 Hz") y «44,1 kHz» con el separador del idioma.
- **textos#40** Usar siempre « dB» y « bpm» (como dice Lang.h), con espacio fino o normal. LIM pasado por T().
- **textos#42** setTitle(T("Pad %1", n)) y T("ESTÉREO")/T("MONO") para la descripción accesible.
- **textos#43** Componer el título con T("MIDI  ·  %1  ·  %2") como el resto de títulos de ficha («PAD 1  ·  CHANNEL»).
- **textos#23** Usar "+" en en; 797→«TAP A PAD TO SEE ITS WAVE».
- **textos#31** Borrar la fila 1579 o usarla con un solo nombre en toda la app.
- **textos#33** «exportando…».
- **textos#34** Fijar avisos en versalitas o en frase normal y aplicarlo igual a toda la ficha.
- **textos#41** Una sola función de formato de porcentaje: con espacio en es y fr, sin espacio en en y zh, y en ar «٪» o «%» envuelto en Lang::ltr.

</details>

## android

**Lo que esta bien:**

- targetSdk 36 y minSdk 24 verificados en el manifiesto binario del APK firmado (entrega/Zati-release.apk, versionCode 89, decodificado con audit/android/tools/axml.py), por encima del mínimo de Play (35 desde ago-2025).
- Páginas de 16 KB conseguidas de verdad: readelf -lW libjuce_jni.so muestra LOAD Align 0x4000 en los 3 segmentos; la .so va STORED (sin comprimir) y alineada a 16384 dentro del zip; extractNativeLibs=false. El workflow lo comprueba antes de refirmar (build-apk.yml:399-428) y Tests/apk.py lo vuelve a comprobar sobre el binario firmado (547-556) -> VEREDICTO OK. Esto deja resuelto de hecho el E2 de AUDITORIA-2026-08.md.
- Cadena de suministro clavada: JUCE fijado por SHA (8.0.15, 91ad83ae) y las acciones de GitHub por SHA; workflow solo workflow_dispatch.
- Secretos de firma tratados con cuidado: contraseñas pasadas por env/stdin a apksigner (build-apk.yml:430-482), keystore decodificado a fichero temporal con trap que lo borra, y comprobación de que el APK final NO lleva la huella de depuración (484-515). Tests/apk.py fija la huella del firmante esperado.
- Superficie de permisos mínima y correcta: sin INTERNET, WRITE_EXTERNAL_STORAGE acotado maxSdkVersion=28, READ_EXTERNAL_STORAGE maxSdk=32 y READ_MEDIA_AUDIO para 33+, RECORD_AUDIO pedido en contexto (al pulsar grabar), sin POST_NOTIFICATIONS ni servicios en primer plano innecesarios.
- allowBackup=false: los proyectos y la bitácora no salen a la copia en la nube de Google sin consentimiento.
- Publicación en MediaStore bien hecha (MediaStore.cpp): inserción en Audio.Media con IS_PENDING=1, escritura y IS_PENDING=0; si falla se borra la fila. Compartir con ACTION_SEND sobre la URI de MediaStore y FLAG_GRANT_READ_URI_PERMISSION, sin necesidad de FileProvider.
- Ciclo de vida cuidado en pausa: appSuspended (MainComponent.cpp:20946-20979) autoguarda la sesión y hace un flush acotado; el foco de audio (AudioFocus.cpp + MainComponent.cpp:21028-21130) para el transporte al perderlo y lo reanuda si sonaba al recuperarlo, y atenúa al recibir ducking.
- FLAG_KEEP_SCREEN_ON solo mientras suena o graba (MainComponent.cpp:22163-22183), no permanente: respeta batería y las guías de Android.
- Oboe parcheado con criterio (ci/patch_juce_oboe.py): entrada con InputPreset UNPROCESSED (9) para grabar sin AGC/supresor, y xrun=-1 cuando el aparato no informa en lugar de inventar 0.
- La interfaz no se pinta bajo las barras: se muestra una portada hasta que llegan los insets reales y luego se maquetan con ellos (verificado en audit/android/shots/apaisado.png, 851x393 con insets 24,0,0,48: nada queda tapado).
- El tema de arranque toma el color del chasis de la tabla de carcasas (windowBackground #FF1B4A4F, LACA) y el workflow comprueba que el valor parcheado coincide: no hay destello blanco al abrir.
- La ruta de compilación no viaja en la .so (strings sin /home/runner ni rutas absolutas del CI) y se podan assets y artefactos intermedios del paquete.
- SalidaPrevia.cpp lee ApplicationExitInfo en API>=30 para saber por qué murió la sesión anterior (ANR, LMK, crash) sin telemetría externa.

| # | gravedad | v | hallazgo | donde | estado |
|---|---|---|---|---|---|
| 0 | alta | si | En Android 16 el botón/gesto ATRÁS nunca llega a la app: el gesto predictivo se traga cierraLoDeArriba | AndroidManifest generado (paso Python build-apk.yml:233-344); Source/Main.cpp:1017 backButtonPressed; JUCE ComponentPeerView.java:411 | pendiente |
| 1 | alta | si | configChanges incompleto: cambiar tamaño de letra, idioma, densidad o pantalla mata el proceso (JUCE hace System.exit al recrear) | AndroidManifest generado (activity configChanges=0xff0); JUCE juce_Messaging_android.cpp:192-203 (onActivityDestroyed -> System.exit(0)); ju | pendiente |
| 2 | alta | si | La biblioteca de proyectos acaba en Android/data y se borra al desinstalar; el cambio de clave previsto obligará a desinstalar | Source/ProjectStore.h:1-160 (canReallyWriteInto, cascada Music/ZATI -> externalFilesDir/ZATI); Source/AppStorage.cpp (getExternalFilesDir(nu | pendiente |
| 3 | alta | si | Exportar MIDI nunca publica el .mid y además barre wav/ogg sueltos y pisa la URI de compartir | Source/MainComponent.cpp:20560-20587 (exportación MIDI) y 18315-18358 (publicarExport) | arreglado (7499333) |
| 4 | media | si | ATRÁS en la raíz llama a finish() y JUCE mata el proceso: cada vuelta es un arranque en frío (y el comentario dice lo contrario) | Source/MainComponent.cpp:8816-8833 (cierraLoDeArriba devuelve false en la raíz); MainComponent.cpp:20946 comentario de appSuspended; JUCE ju | pendiente |
| 5 | media | si | El rebote en vivo no se cierra al pasar a segundo plano ni al perder el foco: WAV con cabecera rota si el proceso muere, y sin COMPARTIR | Source/MainComponent.cpp:18213-18305 (rebote en vivo), 20946-20979 (appSuspended), 21028-21130 (audioFocusLost); Source/Exporter.h:672-760 ( | pendiente |
| 6 | media | si | Insets incompletos: sin recorte de pantalla (notch) en apaisado, sin insets en API<35 y viejos hasta 2 s tras rotar | Source/SystemInsets.cpp (solo API>=35, solo WindowInsets.Type.systemBars()); MainComponent.cpp:5079, 12015 (refreshSystemInsets, cada kSyncS | pendiente |
| 7 | media | si | Iconos de la barra de estado blancos sobre carcasas claras (PAPEL, ACERO): no se ajusta la apariencia de las barras | Tema de arranque (build-apk.yml paso de tema, parent Theme.Material.Light.NoActionBar sin windowLightStatusBar); sin llamadas a setSystemBar | pendiente |
| 8 | media | si | Splash de Android 12+ recorta en círculo el icono a sangre; ci/splash.png se genera pero nunca se usa | Source/StoreArt.h:1005-1030 (splashIcon); build-apk.yml paso de tema (sin windowSplashScreenAnimatedIcon) | pendiente |
| 9 | media | si | Icono del lanzador no adaptativo y PNGs 8-10 veces más grandes de lo necesario | Zati.jucer (icono), salida del exportador: res/drawable-ldpi/mdpi/hdpi/xhdpi/icon.png | pendiente |
| 10 | media | si | libjuce_jni.so exporta 28.723 símbolos: ~4 MB de tablas y relocaciones que no hacen falta | Zati.jucer extraLinkerFlags / extraCompilerFlags (sin -fvisibility=hidden ni --gc-sections) | pendiente |
| 12 | media | si | El AAB se firma con jarsigner encima de una firma de depuración y sin secretos se publica igual como 'AAB for Play' | .github/workflows/build-apk.yml:517-531 (firma AAB) y 484-515 (comprobación de huella solo del APK); paso de release | pendiente |
| 11 | baja | si | El código de banco/auditoría viaja dentro de la release (91 funciones, 312 KB, 88 cadenas ZATI_) | Source/MainComponent_Audit.cpp (12.001 líneas), Source/UiAudit.h (1.832 líneas; enabled() con getenv en cada llamada) | pendiente |
| 13 | baja |  | versionCode = GITHUB_RUN_NUMBER: se reinicia si se migra el repo o se renombra el workflow | .github/workflows/build-apk.yml:154-169 | pendiente |
| 14 | baja |  | versionName fijo en 1.0.0 en todas las builds | Zati.jucer (version="1.0.0"); Source/Main.cpp:154 | pendiente |
| 15 | baja |  | Zati-debug.apk se publica con el mismo paquete, firma y versionCode que la release | .github/workflows/build-apk.yml (release apk-latest) | pendiente |
| 16 | baja |  | Permiso de micrófono denegado para siempre: solo un aviso, sin camino a Ajustes | Source/MainComponent.cpp:16928-16935, 19121-19127, 21636; ensureStoragePermission 12630-12662 | pendiente |
| 17 | baja |  | Foco de audio con la API obsoleta y lambda que captura this en callAsync | Source/AudioFocus.cpp (requestAudioFocus(listener, STREAM_MUSIC, AUDIOFOCUS_GAIN)) | pendiente |
| 18 | baja |  | porAltavoz no reconoce salidas de línea, auxiliar, USB ni dock: el monitor se apaga y pide cascos | Source/AudioFocus.cpp (porAltavoz); MainComponent.cpp:17741 (aplicaMonitor) | pendiente |
| 19 | baja |  | Faltan uses-feature con required=false para micrófono y MIDI | AndroidManifest generado (paso Python build-apk.yml:233-344) | pendiente |
| 20 | baja |  | MIDI sin detección en caliente y emparejado por nombre | Source/MidiIo.h | pendiente |
| 21 | baja |  | No se suben símbolos nativos de depuración para Play Console | build-apk.yml / gradle generado (sin ndk.debugSymbolLevel) | pendiente |
| 22 | baja |  | PRIVACY.md no coincide con lo que hace la app | PRIVACY.md | pendiente |
| 23 | detalle |  | Título del selector de compartir 'ZATI' fijo y sin traducir | Source/MediaStore.cpp (Intent.createChooser(intent, "ZATI")) | pendiente |
| 24 | detalle |  | Atributos muertos en Zati.jucer que aparentan configurar cosas que no se aplican | Zati.jucer (exportador ANDROIDSTUDIO) | pendiente |
| 25 | detalle |  | hardwareAccelerated=false en la aplicación | AndroidManifest generado (<application android:hardwareAccelerated="false">) | pendiente, opinion |

<details><summary>Arreglo propuesto de cada uno</summary>

- **android#0** Corto plazo: en el paso Python que parchea el manifiesto añadir android:enableOnBackInvokedCallback="false" en <activity> (o <application>) para recuperar el comportamiento clásico. Medio plazo: registrar por JNI un OnBackInvokedCallback (PRIORITY_DEFAULT) en la Activity solo cuando haya algo que cerrar (getOnBackInvokedDispatcher().registerOnBackInvokedCallback) y desregistrarlo en la raíz, para conservar la animación predictiva al salir.
- **android#1** En el paso Python del manifiesto sobrescribir android:configChanges con la lista completa: "keyboard|keyboardHidden|orientation|screenSize|smallestScreenSize|screenLayout|navigation|uiMode|density|fontScale|locale|layoutDirection|colorMode|touchscreen|mcc|mnc". JUCE ya reescala por desktop scale, así que no necesita recrear. Añadir a Tests/apk.py un check de que configChanges contiene density y fontScale.
- **android#2** 1) Antes del cambio de clave: firmar con esquema v3 y linaje (apksigner rotate --old-signer depuración --new-signer subida, y sign --lineage) para que la actualización se instale encima sin desinstalar. 2) Mover la biblioteca a un sitio que sobreviva: Documents/ZATI vía MediaStore.Files (Documents/ y Download/ aceptan cualquier tipo) o una carpeta elegida por SAF (ACTION_OPEN_DOCUMENT_TREE + takePersistableUriPermission). 3) Cambiar la sonda para usar un nombre .wav o comprobar con MediaStore en vez de File. 4) Mínimo: botón 'Exportar biblioteca (zip)' y aviso en la app antes de la versión de Play.
- **android#3** Dar a publicarExport el fichero concreto (no la carpeta). Para .mid publicar vía MediaStore.Files en Download/ZATI o Documents/ZATI con MIME audio/midi (Music/ no admite .mid en todos los OEM) y guardar su URI en exportUri; ofrecer COMPARTIR con type audio/midi.
- **android#4** En la raíz devolver true y llamar por JNI a activity.moveTaskToBack(true) (comportamiento de Android 12+ para actividades raíz), o un 'pulsa ATRÁS otra vez para salir' con toast. Corregir el comentario de appSuspended.
- **android#5** En appSuspended y en audioFocusLost llamar a terminaRebotVivo() (o al menos flush del writer y actualizar cabecera periódicamente, p.ej. cada 2 s). Al terminar, pasar el fichero a publicarExport(fichero) y guardar la URI para COMPARTIR, con mensaje 'Guardado en Música/ZATI/…'.
- **android#6** Pedir getInsets(Type.systemBars() | Type.displayCutout()) y, para API 30-34, usar también getInsets; para <30 getSystemWindowInsets + getDisplayCutout. Releer insets en parentSizeChanged/resized (o con un OnApplyWindowInsetsListener por JNI) en lugar de por sondeo.
- **android#7** Al aplicar carcasa, por JNI: window.getInsetsController().setSystemBarsAppearance(lum>0.5 ? APPEARANCE_LIGHT_STATUS_BARS|APPEARANCE_LIGHT_NAVIGATION_BARS : 0, mask) en API>=30 y SYSTEM_UI_FLAG_LIGHT_STATUS_BAR en 23-29, según la luminancia del chasis.
- **android#8** Generar res/values-v31/styles.xml con windowSplashScreenAnimatedIcon=@drawable/splash (el PNG de StoreArt con margen) y windowSplashScreenBackground=color del chasis, desde el mismo paso Python que ya parchea el tema.
- **android#9** Desde el workflow, escribir mipmap-anydpi-v26/ic_launcher.xml con <adaptive-icon> (foreground = icono con zona segura 66/108, background = color chasis, monochrome para iconos temáticos de Android 13) y mipmaps reducidos a 48/72/96/144/192 px; apuntar android:icon a @mipmap/ic_launcher.
- **android#10** Añadir en el .jucer extraCompilerFlags='-fvisibility=hidden -fvisibility-inlines-hidden -ffunction-sections -fdata-sections' y extraLinkerFlags='-Wl,--gc-sections -Wl,--icf=all -Wl,--version-script=exports.map' (exports.map: { global: JNI_OnLoad; Java_*; local: *; };), y valorar -flto=thin. Medir de nuevo con readelf/ls; esperable 25-40 % menos.
- **android#12** Quitar firmas previas (zip -d app.aab 'META-INF/*') antes de jarsigner, o configurar signingConfig nulo para release en el gradle generado. Verificar con 'keytool -printcert -jarfile app.aab' que solo hay la huella de subida y fallar si aparece la de depuración. Si faltan secretos, no adjuntar el AAB o renombrarlo 'unsigned-NO-SUBIR'.
- **android#11** Envolver MainComponent_Audit.cpp y UiAudit en #if ZATI_AUDIT_BUILD (definido solo en la build de escritorio/CI de banco) y excluir el fichero del target Android en el .jucer; cachear enabled() en una static.
- **android#13** versionCode = OFFSET + run_number (OFFSET en una variable de repo) y un paso que lea el versionCode de la release apk-latest (aapt2 dump badging) y falle si el nuevo no es mayor.
- **android#14** Pasar versionName '1.0.<run_number>' (o 1.<minor>.<versionCode>) desde el workflow al .jucer igual que el versionCode, y mostrarlo en AJUSTES con el SHA corto.
- **android#15** Para la variante debug usar applicationIdSuffix '.debug' y versionNameSuffix '-debug' (o no publicarla en la release pública).
- **android#16** Si shouldShowRequestPermissionRationale es false tras denegar, ofrecer botón 'Abrir ajustes' que lance Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS, Uri.fromParts("package", pkg, null)). Igual para READ_MEDIA_AUDIO.
- **android#17** En API>=26 construir AudioFocusRequest.Builder(AUDIOFOCUS_GAIN).setAudioAttributes(mismos atributos que Oboe).setOnAudioFocusChangeListener(...).setWillPauseWhenDucked(false) y abandonAudioFocusRequest al salir. Capturar un Component::SafePointer en lugar de this.
- **android#18** Tratar como 'no altavoz' todo tipo distinto de TYPE_BUILTIN_SPEAKER (2), TYPE_BUILTIN_SPEAKER_SAFE (24) y TYPE_BUILTIN_EARPIECE (1): invertir la lógica a lista blanca de altavoces.
- **android#19** Añadir <uses-feature android:name="android.hardware.microphone" android:required="false"/> y <uses-feature android:name="android.software.midi" android:required="false"/> en el paso Python, y comprobarlo en Tests/apk.py.
- **android#20** Usar juce::MidiDeviceListConnection::make(callback) para reabrir el dispositivo elegido al reaparecer, y guardar MidiDeviceInfo::identifier en la sesión.
- **android#21** En el gradle generado (o por el campo extra del .jucer) poner android.buildTypes.release.ndk.debugSymbolLevel 'SYMBOL_TABLE' o 'FULL', o subir native-debug-symbols.zip como artefacto del workflow.
- **android#22** Reescribir: nombre igual que en Play, tabla de permisos completa con su motivo, 'solo sale del teléfono si tú lo compartes', explicación de Música/ZATI (persistente) frente a Android/data (se borra al desinstalar), mención de los registros locales que nunca se envían, y fecha actual. Mantenerlo coherente con la sección Seguridad de datos de Play (sin recogida de datos).
- **android#23** Pasar el texto traducido (TRANS("Compartir")) a comparte() y usarlo en createChooser.
- **android#24** Borrarlos del .jucer (o renombrar al atributo real si se quería una actividad propia) para que el fichero refleje lo que de verdad se genera.
- **android#25** Dejarlo documentado en el workflow; si se pasa a JUCE_OPENGL o se registra OnBackInvokedCallback con animación, poner hardwareAccelerated=true en la activity y medir.

</details>

## persistencia

**Lo que esta bien:**

- Escritura atómica bien hecha en los ficheros sueltos: el estado de sesión, los textos y los WAV se escriben primero en un .escribiendo o .tmp, se valida el resultado (parseXML del estado, relectura del WAV) y solo entonces se hace rename(2) encima (ProjectStore ponEncima, SessionKeeper::escribeEstado). Si algo falla a medias, el fichero anterior queda intacto.
- SessionKeeper::exists() rescata state.xml.tmp cuando el proceso murió entre la escritura y el rename.
- El estado de la sesión se escribe ANTES de borrar los WAV de los pads vaciados, así que la propia sesión nunca deja un XML que apunte a un WAV ya borrado.
- No se reescribe un estado idéntico al último escrito: menos desgaste y menos ventanas de riesgo.
- La sesión invisible aguanta un kill -9: tras matar el proceso a mitad de trabajo volvieron los 64 pads, sus nombres y el BPM 95 puesto con toques (fotos p20 y p21 en .../audit/persistencia/).
- Un cambio hecho con el dedo queda a salvo en unos 1,5 s (OidoDeToques con kEstadoTrasToqueMs=1500), además de la escritura periódica cada 20 s y la de onPause con un flush de 2500 ms.
- readSample se defiende de cabeceras mentirosas (declared > possible) y de bad_alloc: un WAV hostil no tumba la app.
- El proyecto hostil RARO (bpm=0, selectedPad=-9, nombre de 300 letras K, gain=nan, pan=7, pitch=99999, choke=-3, zati=77, attack=inf) se abrió con el binario ASan sin cierre y sin ningún informe de AddressSanitizer (0 coincidencias en tel13/app.log). El BPM se acota a 60.0 y el color zati se reduce con módulo en Zati::colour (Zati.h:43). Foto p41.png: «Abierto RARO [64 pads]».
- home() está anclado en ~/.config/zati-home.txt y ProjectStore::componente() rechaza '..' y los separadores: un nombre no puede escapar de la carpeta de proyectos.
- ABRIR, NUEVO, BORRAR y sobrescribir un proyecto o un kit piden confirmación con dos toques.
- Un project.xml ilegible da un mensaje explícito en vez de abrir un proyecto vacío, y al abrir se muestra cuántos pads quedaron sin audio.
- Los presets de FX usan createLegalFileName, que quita ':' y '|', así que el separador interno del fichero no se puede inyectar desde el nombre.
- El problema de la feria (la app se cerraba al borrar) no se reprodujo: en todas mis pruebas de BORRAR la app siguió viva.
- Existe Tests/proyectos con 7 XML congelados que sirven de base para pruebas de migración.

| # | gravedad | v | hallazgo | donde | estado |
|---|---|---|---|---|---|
| 0 | bloqueante | si | BORRAR un proyecto cuyo nombre lleva un punto borra OTRO proyecto distinto | Ajustes > Proyectos > BORRAR. Source/MainComponent.cpp:14741-14751 (deleteProject), Source/ProjectStore.h:353-385 (sanitise) y 433-443 (list | arreglado (fb718ae) |
| 1 | alta | si | La pila de deshacer cruza proyectos: DESHACER tras NUEVO o ABRIR trae el contenido del proyecto anterior y GUARDAR lo escribe encima del actual | Source/MainComponent.cpp:12269-12352 (pushUndo, performUndo, performRedo), 14484-14519 (loadProject), 14848+ (newProject) | arreglado (fb718ae) |
| 2 | alta | si | Guardar encima de un proyecto existente no es atómico: si falla a mitad, queda un XML viejo con WAV nuevos o borrados | Source/MainComponent.cpp:14401-14441 (stepPadSaveJob) y 14444-14481 (finishProjectSave) | pendiente |
| 3 | alta | si | Un state.xml corrupto o truncado vacía la sesión en silencio y la sobrescribe | Source/MainComponent.cpp:21320-21370 (restoreSession) | arreglado (fb718ae) |
| 4 | alta |  | Lo que se escribe con el teclado (BPM, nombres) no dispara el guardado rápido y se pierde si el proceso muere en los 20 s siguientes | Source/MainComponent.h:2249-2256 (OidoDeToques, que solo escucha mouseUp) y 2311-2312 (kEstadoSesionMs=20000); MainComponent.cpp:22225-22256 | pendiente |
| 5 | alta |  | GUARDAR KIT con «Sobrescribir» mezcla el kit viejo y el nuevo | Source/MainComponent.cpp:1155-1175 (projKitButton) y 19520-19575 (guardarKit) | arreglado (c1d7764) |
| 6 | alta |  | No hay indicador de «proyecto sin guardar»: «A SALVO» se refiere a la sesión y ABRIR o NUEVO tiran los cambios sin avisar | Cabecera (lineaDeContinuidad, MainComponent.cpp:14600-14660) y confirmaciones de ABRIR y NUEVO (1101-1110) | pendiente, opinion |
| 7 | media |  | Tras DESHACER, las fichas de pad pierden el nombre aunque el estado lo tiene | Source/MainComponent.cpp:12269-12352 (performUndo y performRedo, que no repintan setSampleInfo) frente a 14528-14531 (finishProjectOpen, que | pendiente |
| 8 | media |  | Se reescriben los 64 WAV en cada GUARDAR aunque no hayan cambiado | Source/MainComponent.cpp:14401-14441 (stepPadSaveJob) | pendiente |
| 9 | media |  | La comprobación de sobrescritura distingue mayúsculas y minúsculas, el almacenamiento de Android no | Source/MainComponent.cpp:1072-1097 (projSaveButton: name != currentProject && ProjectStore::list().contains(name)) | pendiente |
| 10 | media |  | La fila seleccionada y el cuadro de nombre pueden apuntar a proyectos distintos: BORRAR actúa sobre la fila | Source/MainComponent.cpp:1123-1137 (BORRAR usa la fila seleccionada) frente a 1072-1097 (GUARDAR usa el cuadro) | pendiente |
| 11 | media |  | No hay fsync del fichero ni del directorio antes del rename | Source/ProjectStore.h (ponEncima, escribeTexto, writeSampleTo) y Source/SessionKeeper.cpp (escribeEstado) | pendiente |
| 12 | media |  | Los valores fuera de rango de un proyecto se cargan crudos en la interfaz y se perpetúan en la sesión y en cada nuevo guardado | Source/MainComponent.cpp:13859-13899 (applyState, restauración por pad: padGain, padPan, padPitch, padAttack, padChokeUI, padZati, padCents. | pendiente |
| 13 | baja |  | Nombres largos: sanitise corta a 40 caracteres y dos nombres distintos acaban en la misma carpeta | Source/ProjectStore.h:353-385 (sanitise), cuyo propio comentario dice TWO DIFFERENT NAMES MUST NEVER BECOME ONE FOLDER | pendiente |
| 14 | baja |  | Reglas de nombre distintas para proyecto (sanitise, 40) y kit o preset (createLegalFileName + componente, 60) | Source/ProjectStore.h:353-385 frente a MainComponent.cpp:1155-1175 y 6385-6470 | pendiente, opinion |
| 15 | baja |  | Con el nombre vacío se guarda como «SIN NOMBRE»; el fallback previsto es código muerto | Source/MainComponent.cpp:1079-1086 | pendiente |
| 16 | baja |  | BORRAR dice «Borrado» aunque deleteRecursively falle | Source/MainComponent.cpp:14741-14751 (deleteProject) | pendiente |
| 17 | baja |  | version=1 se escribe pero nunca se lee: no hay migración ni aviso ante un proyecto de una versión futura | Source/MainComponent.cpp:12935+ (captureState escribe version) y 13416+ (applyState no la lee) | pendiente |
| 18 | baja |  | readSample descarta entero un WAV truncado en vez de recuperar lo que hay | Source/ProjectStore.h:623-673 (readSample, rechazo cuando declared > possible) | pendiente |
| 19 | baja |  | Si la escritura de un pad de sesión falla (disco lleno), ese pad no se reintenta hasta que vuelva a cambiar | Source/SessionKeeper.cpp run() (dirty=false aunque writeSample falle) | pendiente |
| 20 | baja |  | NUEVO vacía la sesión antes de escribir el estado vacío: si el proceso muere en ese hueco, vuelve la FÁBRICA | Source/MainComponent.cpp:14848+ (newProject llama a session.clear()) y 21320-21370 (restoreSession) | pendiente |
| 21 | baja |  | Tras un SIGTERM, el siguiente arranque informa de un fallo de audio inexistente y no se guarda nada | Barra de estado al arrancar; manejo de señales (appSuspended, MainComponent.cpp:20946-20974) | pendiente |
| 22 | baja |  | carpeta() y root() escriben un fichero de prueba en cada llamada | Source/ProjectStore.h (canReallyWriteInto, llamado desde root(), carpeta(), list() y folderFor) | pendiente |
| 23 | detalle |  | «¿Sobrescribir KITX?» va en mayúsculas y minúsculas, cuando el resto de botones armados va en mayúsculas | Source/Lang.cpp:1894 frente a 788 y 1803-1807 | pendiente |
| 24 | detalle |  | zati-bitacora.txt vive en la carpeta visible Music/ZATI | Carpeta ~/Music/ZATI (ProjectStore home) | pendiente, opinion |

<details><summary>Arreglo propuesto de cada uno</summary>

- **persistencia#0** Que list() devuelva los File (o el nombre real de la carpeta) y que ABRIR y BORRAR operen sobre root().getChildFile(nombreDeLista) SIN sanitizar, comprobando isAChildOf(root()). sanitise() solo debe usarse al CREAR un nombre nuevo desde el cuadro de texto. Añadir un test: dos carpetas que sanitizan igual, se borra una y la otra sobrevive.
- **persistencia#1** Vaciar undoStack y redoStack (y refrescar la visibilidad del botón) en loadProject, en newProject y al terminar restoreSession. Si se quiere deshacer un NUEVO, guardar un único paso «Volver al proyecto X» que reabra el proyecto entero, no snapshots de pads.
- **persistencia#2** Guardar en Projects/.DOS.guardando/ (WAV + XML), hacer fsync y después rename de la carpeta vieja a .DOS.viejo, rename de la nueva a DOS, y borrar la vieja. O bien WAV con nombre versionado (pad01-<hash>.wav) que el XML referencie y que se limpien solo después de escribir el XML.
- **persistencia#3** Si no se puede leer: renombrar a state.xml.corrupto-AAAAMMDD-HHMMSS y no sobrescribir nunca sin esa copia. Antes de cada relevo, mover el state.xml bueno a state.xml.bak e intentar primero el .bak. Como último recurso, reconstruir desde .sesion/samples/padNN.wav. Avisar con un texto claro: «La sesión anterior estaba dañada; se guardó una copia».
- **persistencia#4** Pedir la escritura del estado (con el mismo rebote de 1,5 s) desde onTextChange y onReturnKey de los TextEditor, desde la entrada MIDI que cambie parámetros y desde Slider::Listener::valueChanged, o mejor desde un único punto «marcaSucio()» al que llamen todos los setters de estado.
- **persistencia#5** Escribir en una carpeta temporal hermana y hacer rename al terminar, o como mínimo borrar los *.wav que no estén en el conjunto nuevo DESPUÉS de escribir los nuevos. Test: guardar dos kits distintos con el mismo nombre y contar 16 ficheros.
- **persistencia#6** Llevar un contador de revisión del proyecto y otro del último guardado. Si difieren, la cabecera muestra «DOS · SIN GUARDAR» (o un punto) y ABRIR o NUEVO ofrecen GUARDAR / DESCARTAR / CANCELAR. Reservar «A SALVO» para la sesión en otro sitio o renombrarlo a «BORRADOR AL DÍA».
- **persistencia#7** Al final de performUndo y performRedo, recorrer los pads y llamar a setSampleInfo(nombre, has) como en finishProjectOpen. Mejor aún, sacar ese repintado a una función común refrescaPads().
- **persistencia#8** Recordar por proyecto qué buffer (puntero o hash) se escribió en cada padNN.wav y saltar los que no cambiaron, como hace SessionKeeper con seen. Vaciar el registro al abrir o al cambiar de nombre.
- **persistencia#9** Usar list().contains(name, true) (ignoreCase) o comparar con folderFor(name).exists() y comprobar el nombre real de la carpeta. Lo mismo para kits y presets.
- **persistencia#10** Al seleccionar una fila, copiar su nombre al cuadro. Al editar el cuadro, quitar la selección o seleccionar la fila que coincida, de modo que siempre haya un único objetivo visible. BORRAR deshabilitado si no hay fila seleccionada.
- **persistencia#11** Antes del rename: fsync(fd) del temporal (en JUCE, FileOutputStream::flush no basta; hay que abrir el fd con open() o usar fileno). Después del rename: fsync del directorio padre. Solo hace falta en los relevos de estado y de proyecto, no en cada WAV de sesión.
- **persistencia#12** En applyState, acotar cada campo al leerlo: jlimit con el rango del knob, std::isfinite y un valor por defecto si no lo es, zati en 0..kNumColours-1, choke >= 0, nombre recortado a un máximo (por ejemplo 64). Un único ayudante leeAcotado(p, clave, min, max, defecto). Añadir RARO a Tests/proyectos.
- **persistencia#13** Sustituir los caracteres prohibidos por '_' en vez de quitarlos y, si el nombre supera el límite, cortar y añadir un sufijo corto con hash (por ejemplo «nombre-largo…-3f2a»). Guardar el nombre visible completo dentro de project.xml (atributo title) y enseñar ese en la lista.
- **persistencia#14** Una sola función ProjectStore::nombreDeCarpeta() con las mismas reglas para proyectos, kits y presets, con su test.
- **persistencia#15** Comprobar box.trim().isEmpty() ANTES de sanitizar y, en ese caso, usar currentProject o la fila seleccionada; si no hay ninguno, pedir el nombre.
- **persistencia#16** if (! dir.deleteRecursively()) mostrar «No se pudo borrar X» y refrescar la lista igualmente.
- **persistencia#17** Leer version en applyState: si es mayor que la actual, avisar («hecho con una versión más nueva; guardar puede perder datos») y abrir en solo lectura o pedir confirmación. Si es menor, aplicar las migraciones en una tabla migra[v] y cubrirlas con Tests/proyectos.
- **persistencia#18** En ese caso, leer min(declared, possible) muestras, cargar el pad y marcarlo en el aviso de apertura como «recortado».
- **persistencia#19** Dejar dirty=true si falla, reintentar con espera creciente y exponer un estado de fallo que la cabecera muestre como «SIN ESPACIO».
- **persistencia#20** En newProject, escribir de inmediato un state.xml vacío válido (escribeEstado síncrono) en lugar de borrar la carpeta, y vaciar estadoPendiente dentro de clear().
- **persistencia#21** Instalar un manejador de SIGTERM que pida JUCEApplication::quit() (y con ello el autosave y el flush), y no culpar al audio cuando el cierre anterior fue una señal. Registrar la causa real en la bitácora.
- **persistencia#22** Cachear el resultado por ruta y reprobar solo si falla una escritura real o cambia la preferencia de carpeta.
- **persistencia#23** Unificar en «¿SOBRESCRIBIR %1?» en todos los idiomas.
- **persistencia#24** Moverla a la carpeta privada de la app (getSpecialLocation(userApplicationDataDirectory)) o a .sesion/.

</details>

