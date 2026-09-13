# LA ANATOMIA DE UNA FICHA

Este documento es el DUEÑO de las normas de maquetado de un pop-up, y
`Tests/maqueta.py` lo lee. No es prosa de acompañamiento: si cambia de forma y
el parser no puede sacar la tabla, la prueba **FALLA** en vez de dar verde con
el contrato vacio — que es como una regla de esta clase se muere sin que nadie
se entere.

Existe porque las normas ya estaban escritas en tres sitios y ninguno las
comprobaba: la lista `MINIMOS` de `Tests/desglose.py`, que terminaba en
`return 0` pasara lo que pasara y no la corria ningun workflow; la aritmetica de
la tarjeta COPIADA en `Tests/maquetas.py`, con el tope viejo en 0.78 cuando la
app ya lo deriva a 803 px en 412x915; y `ZATI.md`, que describe QUE contiene
cada ficha y no su anatomia. Y de las reglas duras de `expo.py`, ninguna
comparaba una ficha con OTRA: veintiuna anatomias distintas, cada una coherente
consigo misma, las pasaban todas.


## 1. El numero vive en `Metrics`, y aqui vive el NOMBRE

**Este documento no lleva un solo pixel dentro.** Repetir los valores serian dos
reglas, y no es teorico: es letra por letra lo que le paso a
`Tests/maquetas.py`, que llevaba tandas dibujando una tarjeta noventa pixeles
mas corta que la de la app por haber copiado el `0.78`.

El reparto de dueños:

    Tests/maqueta.md   que pieza usa que token, y que categoria esta exenta
    Metrics            cuanto vale ese token         (Source/ZatiLookAndFeel.h)
    la app             que coloco de verdad          (UiAudit, linea `anatomia`)
    Tests/maqueta.py   parsea este documento y lo resuelve contra Metrics
    Tests/expo.py      importa el contrato y juzga el volcado

Y la app publica lo que HIZO y no lo que declara, igual que `costura` publica el
rayado que dibujo y `fila` el rectangulo que se dio. Publicar la constante seria
el banco repitiendo el codigo, que es como `Tests/icono.py` dio verde dos veces
con la mascara del lanzador rota.


## 2. La anatomia, pieza por pieza

Una ficha se lee de arriba abajo asi: el marco, el renglon de cabecera con su
banda de titulo -y su subtitulo dentro del mismo renglon, centrados los dos
juntos-, la fila de pestañas si la ficha tiene paginas, la frontera con el
cuerpo, y al fondo el renglon de ayuda. Entre dos grupos del cuerpo va otra
frontera; entre dos filas del mismo grupo, el aire de fila.

| pieza | token |
|---|---|
| `marcoX` | `margenFichaX` |
| `marcoY` | `margenFichaY` |
| `cabecera` | `hit` |
| `titulo` | `bandaTitulo` |
| `subtitulo` | `bandaSubtitulo` |
| `pestanas` | `tab` |
| `frontera` | `sm` |
| `fila` | `xs` |
| `seccion` | `bandaSubtitulo` |
| `pie` | `bandaSubtitulo` |
| `canalon` | `canalonSeccion` |

Tres cosas que la tabla no dice y hay que leer aqui:

**El subtitulo no cuesta alto.** Vive DENTRO del renglon de cabecera:
`centraEnRenglon (banda, altoCabecera (haySubtitulo))` centra el par titulo +
subtitulo en los `hit` del renglon. Por eso una ficha con subtitulo y otra sin
el miden lo mismo por arriba, que es lo que hace que las veintiuna se lean como
la misma familia.

**El pie lo reserva el MAQUETADO y no el pintor.** Cuando lo talla el pintor de
`inner`, el presupuesto no se entera y la ficha pide menos de lo que ocupa —
que es la familia de descuadres que este banco lleva contando desde la fila de
CADENA. Y se dibuja con `pintaAyuda`, que mide con el mismo apreton con el que
dibuja y ademas lo apunta en el volcado: los dos que lo pintaban con
`drawFittedText` a pelo eran invisibles para `CORTADO`, `TAPADO` y `PISADO`.

**La frontera es `sm` y el aire de fila es `xs`.** No es una preferencia:
`Metrics::panelAireY` esta DERIVADO de que la frontera valga `sm`, asi que con
`xs` dos paneles quedan a cero de hueco y con `md` a ocho, el doble de lo
previsto. Esa mitad ya la vigila la pregunta 2 de `Tests/maqueta.py` sobre la
COLOCACION; lo que faltaba es que el PRESUPUESTO dijera lo mismo, y no lo decia:
cuatro fichas pedian `md` donde colocaban `sm`.


## 3. Los papeles, y por que hacen falta

Cada rotulo PINTADO lleva el papel que hace, y el banco juzga la banda por el
papel. Esto no es taxonomia: sin ello la anatomia no se puede medir. Censados
los rotulos de las siete pantallas en dos idiomas ANTES de poner ningun liston,
el papel `titulo` salia con **cuatro alturas** -14, 16, 24 y 30- y ni una era un
fallo de maqueta: eran cuatro cosas distintas compartiendo el mismo papel. Un
papel que significa cuatro cosas no se puede juzgar, que es la marca `valor` de
los iconos contada desde el otro lado.

| papel | que es | su banda |
|---|---|---|
| `titulo` | el nombre de la ficha. Uno por ficha | `bandaTitulo` |
| `subtitulo` | la segunda linea de la cabecera | `bandaSubtitulo` |
| `dato` | el renglon de ayuda, una banda por linea | `bandaSubtitulo` |
| `seccion` | el rotulo de un grupo del cuerpo | se imprime, no se juzga |
| `chapa` | el rotulo grande de identidad -la familia del instrumento- | suya |
| `cristal` | una lectura sobre pantalla -el preset- | del cristal |

**El rotulo de seccion se imprime y no se juzga**, y esa es la unica pieza de la
anatomia que no entra en el veredicto. Tiene DOS formas legitimas y el volcado
no las separa: encima de un grupo es una banda de `bandaSubtitulo`, y al LADO de
una fila de chips ocupa el renglon entero -son los 40 px de las siete filas de
AJUSTES-. Un numero que no separa el fallo del caso legitimo no puede ser un
veredicto, que es la leccion de TARJETA y antes la del porcentaje de iconos.


## 4. Quien esta exento

**Nadie, y eso es una medida y no una promesa.** La pregunta se hizo al reves de
lo que parece: se temia que la anatomia le costara alto a las tres fichas de
LIENZO -la rejilla de pasos, el piano y la linea de tiempo, que se pintan con el
dedo arrastrado y cuya celda tiene un suelo medido- y por eso el plan de esta
tanda traia una excepcion declarada, `Sheet::lienzo`, hermana de `listaPropia`.

Medido, no hace falta: las cinco piezas que el contrato juzga -el marco, la
banda de titulo, la de subtitulo y el pie, este ultimo por linea- son bandas que
las tres ya cumplian. La anatomia no le cuesta un pixel al lienzo, asi que la
excepcion no se escribe: una categoria declarada que no usa nadie es la mitad de
una regla, y se lee como si alguien hubiera cedido.

Lo que si tienen su forma propia y esta declarado en la app -no aqui, o serian
dos listas- son `Sheet::listaPropia` -la mesa, el manual y el navegador, que se
llenan con una lista que ya se desplaza- y `Sheet::pintaTodo` -el tour, que no
es una tarjeta sino un foco y un muelle con su tope propio-. Las dos categorias
existian antes de esta tanda y ninguna necesito tocarse.

Y una excepcion que no esta declarada no es una excepcion: es un fallo, y sale
como tal.
