# Revisión de arriba a abajo — agosto de 2026

Qué hay hoy en el código y qué hay que hacer con ello, en orden. Todo lo que se
afirma aquí está comprobado contra el árbol en `555b1f9`, con ruta y cifra; lo
que es opinión va marcado.

Esto **no** repite lo que ya está escrito: el cuaderno de bitácora cuenta qué
encontró cada prueba, y el estudio de producto y el papeleo de la tienda viven
fuera del repositorio. De los bloqueos de aquel estudio, `targetSDK` 36 y MIDI
**ya están hechos**.

Tamaño, para calibrar: `Source/` son 34 928 líneas, de las que `MainComponent.cpp`
son 15 994 (9 558 de código y 5 022 de comentario). `resized()` es **una sola
función de 3 428 líneas**. `Tests/` son 6 040 líneas en 21 bancos.

---

## Las cinco líneas que importan

1. **Tres de las medidas no pueden decir que no.** `expo.py`, `session.py` y
   `apk.py` devuelven cero pase lo que pase.
2. **El `project.xml` se escribe sin la red que la sesión sí tiene**, y el
   párrafo que explica por qué hace falta está ocho líneas más arriba.
3. **Abrir o vaciar un proyecto hereda del anterior** lo que el fichero no trae.
4. **Un NaN se queda encerrado en el delay y en la reverb** y no lo limpia nadie.
5. **La app está clavada en vertical** mientras una séptima parte del banco mide
   apaisado.

Si sólo hay tiempo para una cosa, la primera. Mientras el banco no pueda
suspender, cualquier arreglo de los otros se puede volver a perder sin ruido.

> **Estado: las cinco están hechas**, y con ellas el resto de este documento.
> Se deja como estaba porque es el DIAGNÓSTICO y no el estado — es la misma
> decisión que el estudio de producto tomó con su bloque de `androidTargetSDK`—,
> y lo que encontró cada una y con qué cifra vive en el cuaderno de bitácora. Lo que no puede
> quedarse es sin decir que ya no es verdad: el banco devuelve código de salida
> en las veintitantas pruebas y corre solo en `.github/workflows/banco.yml`, el
> `project.xml` pasa por `ProjectStore::escribeTexto`, abrir y vaciar un
> proyecto ya no heredan —`padPorDefecto`—, el NaN no se queda encerrado en el
> delay ni en la reverb, y el manifiesto ya no clava la app en vertical.

---

## A · El banco no puede fallar

**A1. `Tests/expo.py` —896 corridas, el banco principal— devuelve SIEMPRE 0.**
El fichero termina en `main()` sin `sys.exit`, y `main()` no devuelve nada: sus
dos únicas salidas con código son las guardas de «no hay binario» y «no hay
pantalla virtual». Los siete contadores que `SKILL.md` declara «cero es cero»
los juzga hoy un ojo humano leyendo texto. Es la regla de la casa —*una prueba
que nunca se ha visto fallar no es una prueba*— incumplida por el fichero que
más la cita.
**Medida:** mover 20 px una tapa de la cara y `python3 Tests/expo.py >/dev/null;
echo $?` → hoy **0**.

**A2. `Tests/session.py` calcula el veredicto de lo disperso y lo tira.**
La línea 236 imprime «NO VUELVEN» si `disp_ok` es falso, y la 242 dice
`return 1 if (bad or not chop_ok or not pat_ok or not proj_ok) else 0` —
**`disp_ok` no está**. O sea que acorde, empujón, bloqueo de corte, largo y los
cuatro bloqueos empaquetados pueden dejar de volver del fichero de proyecto sin
que el banco chiste.
**Medida:** romper `setStepPLockRaw` en `applyState` → hoy imprime NO VUELVEN y
sale 0.

**A3. `Tests/apk.py` tampoco falla nunca**, y además lista los cuatro permisos
sin contrastarlos con ninguna lista esperada: uno que desaparezca —o uno nuevo
que aparezca— sale como texto, no como fallo.

**A4. Ningún workflow ejecuta una sola prueba.** `.github/workflows/build-apk.yml`
es el único del repositorio y compila la APK sin correr `expo.py`, `session.py`,
`StressTest` ni `apk.py`. El banco entero depende de que una persona se acuerde.

**A5. Once ganchos huérfanos.** `ZATI_FUZZ`, `ZATI_PAINT`, `ZATI_CYCLE`,
`ZATI_TRIM`, `ZATI_VU`, `ZATI_ZOOM`, `ZATI_BANK`, `ZATI_BUSY`, `ZATI_CHECK`,
`ZATI_CHOP` y `ZATI_TOUR` existen en `Source/` y ningún script los usa — y tres
de ellos son, según el cuaderno de bitácora, la única forma de medir lo que
miden. Y cinco
bancos (`cpu.py`, `store.py`, `clon.py`, `plano.py`, `analiza.py`) más los
objetivos `Soak` y `Cpu` no están en la tabla de `SKILL.md`: por regla, no se
corren nunca.

## B · Se puede perder el trabajo de la persona

**B1. `autosave()` machaca el `project.xml` guardado sin red.**
`MainComponent.cpp:17084` — `folder.getChildFile("project.xml").replaceWithText(...)`.
Es **exactamente** el fallo que `SessionKeeper::writeState` documenta en un
párrafo de doce líneas ocho más arriba de sí mismo —temporal, `parseXML`, y sólo
entonces `moveFileTo`—, y aquí es peor: la sesión se rehace, el proyecto guardado
no. Corre en cada `onPause`, o sea justo antes de que Android mate el proceso. Un
corte a mitad deja un `project.xml` truncado encima de uno bueno, con sus 64 WAV
intactos al lado.

**B2. `finishProjectSave` borra el bueno antes de tener el nuevo.**
`replaceWithText` primero y la comprobación (`existsAsFile && getSize>0 &&
parseXML`) después: cuando falla, el anterior ya no existe.
`ProjectStore::writeSample` ya aprendió esto —«NUNCA SE BORRA EL BUENO ANTES DE
TENER EL NUEVO»— y este camino no.

**B3. `Instrumentos::concede` reescribe la lista ENTERA de licencias sin red.**
Es el único fichero de la app cuya pérdida no se puede rehacer desde la app: hay
que volver a pasar por la facturación. Un proceso muerto a mitad no pierde una
licencia, las pierde todas. El comentario de encima ya avisa de ello.

**B4. `applyState` no pone nada a su defecto: lo que el fichero no trae se hereda
del proyecto anterior.** Los cuatro bloques son `if (child.isValid())` y
`engine.clearSong()` vive DENTRO del `if`. Abrir un proyecto sin `<song>` deja
sonando el arreglo del anterior; sin `<FX>`, sus seis efectos; con `<PADS>` de 16
hijos, los pads 16..63 conservan ganancia, pan y filtro ajenos. Es la herencia
que ya se pagó dos veces en `newProject`, contada en el otro camino.

**B5. `newProject()` sigue heredando los parámetros de cada pad.** Vacía
muestras, nombres y envíos; deja `padPitch`, `padGain`, `padPan`, `padCut`,
`padReverse`, los fundidos, mute/solo, los 18 de efecto, `bpm`, `swing` y el XY.
Cargar un sonido tras NUEVO puede sonar al revés y filtrado a 200 Hz.

**B6. Los 64 pads vuelven del fichero SIN acotar.** `padGain`, `padPan`,
`padAttack`, `padRelease`, `padStart01` y `padEnd01` se leen con `getProperty` y
se empujan a unos setters que son `store()` a pelo. `loadMasterPref` lleva
escrito el porqué de acotar. Y el fader de la mesa **sí** clampa al pintarse, así
que el control enseña 0 dB y el motor tiene el número crudo.

**B7. Nadie abre nunca un proyecto de una versión anterior.** `version` se
escribe y no se lee en ningún sitio, y no hay un solo `project.xml` congelado en
`Tests/`. La regla «lo que no está vale su defecto ANTIGUO» está escrita ocho
veces en comentarios y no la comprueba nadie.

## C · El motor

**C1. Un NaN se queda ENCERRADO para siempre en el delay y en la reverb.**
Las cuatro barreras (`Voice::start`, `fastTanh`, `padFiltState`, master) no
cubren los dos estados realimentados: la línea de retardo y el amortiguamiento de
la FDN. Peor: `Fdn::ringing()` usa `std::abs(v) > 1e-6`, que con NaN es **falso**,
así que el bus se declara muerto y ya nadie lo limpia. El master evita que la app
enmudezca; el delay y la reverb quedan muertos hasta reiniciar.

**C2. Y la prueba de muestras hostiles ya no toca ningún bus.** Su comentario
dice que mide la propagación «por el bus, por el saturador y por el master», pero
desde que los envíos nacen a cero el caso no enruta a ninguna parte: hoy mide el
camino seco y el master. Un cambio de producto legítimo se llevó media medida por
delante sin que nada fallara.

**C3. La cola de la interfaz se come el presupuesto de la de MIDI.**
`AudioEngine.cpp:759-765`: los dos `drain` comparten `Command local[256]` y la
misma `n`. Si la de interfaz entrega 255, a MIDI le queda uno: el resto se
**consume** —`finishedRead` avanza— y se descarta en silencio, sin pasar por
`droppedCommands`. Dos colas por contrato, un solo cubo.

**C4. `pending` tiene 96 huecos dimensionados para 16 pads, y hay 64.**
`std::array<PendingHit, 96>` con el corte `if (numPending >= pending.size())
break;` dentro de un bucle 0→63: lo que sobra desaparece **sesgado hacia los pads
altos** y nadie lo cuenta. Un paso con los 64 pads y redoble de 2 pide 128. Es el
fallo de `layoutModuleBar` otra vez: un tope que se supera en silencio esconde.

**C5. La cola de retirados tira punteros.** `RetiredQueue::push` hace
`if (z1 + z2 < 1) return;` cuando está llena, y para entonces el hilo de audio ya
ha soltado el suyo: un `SampleBuffer` cuyo contador no baja nunca. Son 127 huecos
útiles contra un temporizador de 33-60 ms, y recargar la fábrica escribe 64 de
golpe.

**C6. El bloqueo de CORTE del paso escribe el parámetro GUARDADO del pad.**
`padCutoff[p].store(...)` desde el hilo de audio, y `padCutoff` es lo que lee el
mando CORTE y lo que escribe el fichero de proyecto: dos compases con un paso
bloqueado a 200 Hz y el proyecto guarda 200. Es el daño exacto por el que los
otros cuatro bloqueos se sacaron del pad y viajan con el disparo.

**C7. `copyStateFrom` lee desde el hilo del rebote estado declarado «sólo del
hilo de audio»** (`smSend`, `smSendHot`) mientras el motor vivo lo escribe cada
bloque. Y el comentario de encima afirma que las dos partes son del hilo de
mensajes, que ya no es verdad.

**C8. Menores, todos con medida de una línea.** `chainPos` puede apuntar a una
ranura vieja el primer compás tras encoger la cadena; tres etapas de salida leen
el canal 0 sin el guardia que sí tiene la primera; `renderNextBlock` respeta
`startSample` para `out` y no para sus buffers internos (latente: hoy nadie pasa
desplazamiento); `MidiIo::kMaxPads` es un 64 escrito por segunda vez sin
`static_assert`.

**C9. Sin medir:** el bombeo (`duckPad`) —una etapa que multiplica el master
entero, colas de delay y reverb incluidas—, cuantizar en directo y los grupos de
CHOKE. Y `duckPad` además recorre el bucle por muestra con ganancia 1.0 mientras
haya un pad armado y quieto.

**C10. Estado muerto que cuesta memoria y copias:** `fxDry` reserva 2×`maxBlock`
y no lo lee nadie; `smCutoff`, `smFxMix`, `smCrMix` y `smRvMix` sólo se asignan
en `copyStateFrom`; `fxType`/`setFxType` y `fxCutoff`/`setFxCutoff` no tienen un
llamante fuera de ahí.

## D · La interfaz

**D1. Tres VACIAR con tres definiciones distintas de «paso».** La lista canónica
son nueve campos y ya existe (`copiarFila`). `AudioEngine::clearPattern` **no**
borra `stepNote`, `stepVel` ni `stepRoll`; el VACIAR del piano no borra vel,
roll, len, nudge, lock ni plock; y `euclidesPattern` no toca len, lock, chord ni
plock — bajo un comentario que dice que dejar pasos a medias convertiría «cinco
golpes» en «cinco golpes y lo que hubiera». Síntoma: VACIAR y EUCLIDES dejan
acordes, largos y bloqueos viejos colgando de casillas nuevas. **Ninguna regla
del banco lo caza.**

**D2. `setTabsFit` mide con la fuente equivocada — el fallo de `captionOf` otra
vez.** Saca el cuerpo del alto de la FILA (14.5 px) y el dibujo lo saca del alto
de la TAPA (`capaDe`, 12.54). Pide un 16 % más de sitio del que ocupa, así que
las pestañas de AJUSTES se parten en dos filas donde caben en una. No produce
TRUNC ni SQUEEZE —sólo sobra tarjeta—, así que **ninguna regla lo caza**.

**D3. Tres clampes hacia arriba disfrazados de suelo**, que es el fallo más
repetido de esta casa: `knobH = jlimit (60, kKnobRow, forKnobs/3)` pide 180 px
donde no los hay (vivo en apaisado, página SONIDO); el `capH` de PASOS clampa al
78 % sin rama apaisada mientras `sheetFromBottom` da 0.90 —y **la otra página de
la misma función** sí lo distingue—; y `topeCancion` pregunta con `wideFace` en
vez de con la forma de la ventana, o sea con 50 px menos de los que hay.

**D4. Tres tokens de piel congelados en el arranque.** `projNameBox` (cinco
colores), `masterLabel` y `status` los fijan una sola vez, y `applySkin` sólo
relee la lista de proyectos, las tapas y el color de pista de los deslizadores.
Es el `juce::ListBox` que ya se pagó, en tres clases que no son botones: en
GRAFITO y LACA se quedan con la paleta de PAPEL. **Ninguna regla lo caza** —
`skins.py` mide la tabla y `expo.py` no vuelca colores.

**D5. El árabe, en dos sitios.** Siete deslizadores que traducen su lectura no se
retraducen al cambiar de idioma en caliente (`lockSlider`, los cuatro de bloqueo,
`euclidSlider`, `gridSlider`), y `Lang::ltr` está puesto en cuatro lecturas y
falta en ocho de la misma forma (`volSlider`, `masterFader`, los dieciséis de la
mesa, `fineSlider`, `panSlider`, `noteSlider`, `velSlider`, `rollSlider`,
`resoSlider`). `UiAudit::captionOf` excluye los `Slider` a propósito, así que
**ninguna regla lo caza**.

**D6. `armConfirm (projNewButton, "BORRA TODO?")` es la única de sus seis
llamadas sin `T()`** (`MainComponent.cpp:513`). Sale en español en las cuatro
compilaciones, en la confirmación de la única acción que vacía la máquina.

**D7. CANCIÓN no se pinta con el dedo arrastrado.** `Playlist` sólo implementa
`mouseDown`; `StepGrid` y `PianoRoll` tienen `mouseDrag`. Y la casa justifica
que esa ficha no se desplace precisamente porque «se pinta con el dedo
arrastrado». O falta el gesto o sobra el argumento.

**D8. Reglas escritas varias veces.** El 0.92 del ancho de tarjeta, seis veces
(y cuatro de ellas cambiando `full` por `safeArea()`); el 158 del recuadro de
AUDIO, cuatro veces y con dos aritméticas distintas; `kContinued = 1000` definido
en `AudioEngine.h` **y** en `Playlist.h` sin `static_assert`; `moduleBarFits`
midiendo a 11.0f fijo donde el dibujo usa 11.4 o 12.54.

**D9. Código muerto.** `vuArea` y `stepStripArea` se asignan y no los lee nadie.
En `Metrics` están muertas `hitRow`, `knobSm/Md/Lg`, `fDisplay` y `kStrip` — y
eso importa porque el fichero declara que «si un valor no está abajo, está mal»:
una constante muerta invita a usarla creyendo que es el token vigente.

**D10. Media regla aplicada.** `showSetPage` vacía los límites de una sola de las
ocho tapas que apaga, cuando la casa ya escribió que se hacen «las dos cosas».
`closeAllSheets` lleva la lista de fichas a mano —cuatro en un array y ocho
nombradas—, y la misma lista existe otras dos veces.

**D11. E/S de verdad dentro de `paint()`.** `paintExportSheetContent` llama a
`ProjectStore::exports()`, que crea la carpeta, escribe un fichero de un byte, lo
lee y lo borra — y con el rebote en marcha la ficha se repinta cada tic. La
comprobación es correcta donde se **decide** (`startExport`); en el pintado sobra.

**D12. Un `for` cuya llave cierra donde la sangría dice que no**
(`MainComponent.cpp:11092-11106`): `setDuckPad`, `xyLatch`, `selectXyFx` y su
toggle corren seis veces —una por efecto— al abrir un proyecto.

## E · Empaquetado

**E1. La app está clavada en VERTICAL y una séptima parte del banco mide
apaisado.** `Zati.jucer` dice `androidScreenOrientation="portrait"`; `expo.py`
mide `915x412` como una de sus siete pantallas (~128 corridas) y el cuaderno de
bitácora dedica cinco párrafos al girado. En un teléfono ese trabajo es inalcanzable; en
tableta y plegable con `targetSdk 36` Android **ignora** la restricción, así que
gira justo donde nadie decidió que girara. Una de las dos cosas sobra.

**E2. La alineación de 16 KB se comprueba sobre un fichero que después se
reescribe.** El paso «Páginas de 16 KB» corre antes de «Re-sign the release
APK», que reconstruye el zip: lo verificado no es lo que se sube. Y el `.aab`
—que es lo que Play acepta— no se comprueba en ningún momento.

## F · Estructura, con números

- **`resized()` son 3 428 líneas en una función.** Le siguen `paint` (356),
  `timerCallback` (323) y `applyState` (289).
- **`MainComponent` es el modelo además de la vista:** ~25 arrays de 64 pads que
  espejan el estado del motor, 413 llamadas a `engine.get/set` y el fichero de
  proyecto como tercera copia. Cada parámetro nuevo hay que añadirlo en tres
  sitios y sincronizarlo a mano. El estudio de producto ya lo señala como la
  deuda a atacar **antes** de meter stems.

## G · Lo que está bien, para no romperlo

- Los invariantes del hilo de audio se cumplen en el camino normal: sin reservas,
  sin cerrojos, sin `String`, sin `delete`; 94 atómicos y el intercambio de
  punteros por buzón.
- La disciplina de medida es real y ha encontrado fallos que ninguna captura
  habría visto. El problema no es que se mida poco: es que **tres de las medidas
  no pueden decir que no**.
- **La latencia ya se puede medir desde la app.** `startLatencyProbe` /
  `finishLatencyProbe` con eco por micrófono, botón MEDIR en AJUSTES y el
  resultado pintado en milisegundos con color. El estudio de producto dice que
  ese número «nunca se ha medido» y propone una tarde con cable y OboeTester: es un
  toque en el teléfono y escribirlo en el README.

---

## El plan

Cinco tandas, en este orden. Cada una acaba con banco en verde y sus números; la
APK, al final de la última.

**1 · Que el banco pueda decir que no** (medio día). `expo.py`, `session.py` y
`apk.py` devuelven código de salida; `apk.py` contrasta la lista de permisos; la
tabla de `SKILL.md` incluye los cinco bancos que hoy no se corren; un paso en
`build-apk.yml` que corra `apk.py` sobre el binario **ya firmado**, con la
comprobación de 16 KB movida detrás del refirmado y repetida sobre el `.aab`.
Cada uno validado rompiéndolo a propósito.

**2 · Que no se pierda trabajo** (un día). `autosave`, `finishProjectSave` e
`Instrumentos::concede` pasan por temporal + `parseXML` + `moveFileTo`,
reutilizando **la misma** función que ya tiene `SessionKeeper::writeState` — se
extrae, no se copia. `applyState` pone defectos en vez de heredar; `newProject`
vacía también los parámetros de pad; se acotan los seis parámetros al leerlos.
Tres `project.xml` congelados en `Tests/` (sin `sends`, sin `<song>`, con
`steps` viejo) y una comprobación nueva en `session.py`.

**3 · El motor** (un día). Barrera de no-finito en el delay y en la FDN, y
`Fdn::ringing()` que trate el NaN como «sigue sonando». Volver a enrutar la
prueba de muestras hostiles a un bus con envío abierto. Presupuesto propio para
la cola MIDI y contar lo tirado. `pending` dimensionado a 64 pads × redoble, con
sus descartes contados, y lo mismo en la cola de retirados. Sacar el bloqueo de
CORTE del parámetro guardado del pad. Pruebas nuevas: bombeo, cuantizar en
directo y CHOKE.

**4 · La interfaz** (un día). Un solo VACIAR con la lista canónica de nueve
campos. Los tres clampes hacia arriba piden lo que hay, y `setTabsFit` y
`moduleBarFits` miden con `reparteTapa` — una cuenta, un dueño —, comparando las
896 corridas antes y después. Los tres tokens de piel se releen en `applySkin`.
Árabe: los siete deslizadores y las ocho lecturas que faltan, y `T()` en «BORRA
TODO?». `mouseDrag` en `Playlist`. Y la limpieza con medida detrás: miembros y
constantes muertos, el 0.92 y el 158 unificados, `static_assert` para
`kContinued` y `kMaxPads`, la E/S fuera del `paint` de EXPORTAR y el `for`
cerrado donde dice la sangría.

**5 · Publicación** (medio día). Decidir el giro —recomendación: **quitar el
`portrait`**, que el trabajo apaisado ya está hecho y medido y en tableta gira
igual—, y medir la latencia con el botón que la app ya tiene, escribiendo el
número en el `README` con teléfono, versión y tamaño de bloque.

Fuera de esta lista y sin cambiar: licencia de JUCE, clave de subida y secretos,
política de privacidad publicada, y renombrar el repositorio.
