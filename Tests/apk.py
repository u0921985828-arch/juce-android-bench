#!/usr/bin/env python3
"""Comprueba un APK -o un AAB- sin herramientas de Android: firma, paquete,
permisos, alineacion de las bibliotecas y lo que va ESCRITO dentro. A mano
porque este entorno no trae el SDK.

Y EL AAB TAMBIEN, que es el unico artefacto que Play acepta y era el unico que
no miraba nadie: esto corria solo sobre el APK, asi que lo que se publica de
verdad se subia sin comprobar. Cambia donde estan las cosas -el manifiesto vive
en `base/manifest/`, la .so en `base/lib/`- y cambia la firma, que en un bundle
es de `jarsigner` y no el bloque v2 de un APK. La alineacion no aplica: de un
bundle Play GENERA los APK, asi que quien decide el alineado es Play con las
banderas del enlazador, y esas se comprueban sobre el APK de al lado."""
import os, re, sys, zipfile, struct

sys.path.insert (0, os.path.dirname (os.path.abspath (__file__)))
from marcas import prohibido

apk = sys.argv[1]

#  Si el paquete no esta, se dice y no se lanza un traceback. El paso que
#  llama a esto lleva `if: !cancelled()` a proposito -un paso saltado no es un
#  paso verde- asi que corre TAMBIEN cuando la compilacion ha fallado antes, y
#  entonces un FileNotFoundError de cinco lineas se lee como si el fallo
#  estuviera aqui. Medido: la corrida que subio JUCE a 8.0.15 fallo en el
#  --resave de Projucer y este fichero dejo un traceback encima del error de
#  verdad.
if not os.path.exists (apk):
    print ("no existe %s: la compilacion no llego a producirlo, "
           "el fallo esta antes" % apk)
    sys.exit (1)

raw = open(apk, 'rb').read()

z = zipfile.ZipFile(apk)
nombres = set(z.namelist())
#  Que clase de paquete es se MIRA, no se deduce de la extension: un fichero
#  renombrado no cambia de forma.
BUNDLE = 'base/manifest/AndroidManifest.xml' in nombres
QUE = 'AAB' if BUNDLE else 'APK'
print(f"paquete: {QUE}")

# --- 1. La firma -----------------------------------------------------------
#  En un APK, el bloque v2 justo antes del directorio central. En un bundle no
#  existe: va firmado como un jar, con su MANIFEST.MF y su .RSA/.DSA/.EC.
jar_firmado = any(n.startswith('META-INF/') and n.endswith(('.RSA', '.DSA', '.EC'))
                  for n in nombres)
eocd = raw.rfind(b'PK\x05\x06')
cd_off = struct.unpack_from('<I', raw, eocd + 16)[0]
magic = raw[cd_off - 16:cd_off]
v2 = magic == b'APK Sig Block 42'
esquemas = []
if v2:
    tam = struct.unpack_from('<Q', raw, cd_off - 24)[0]
    ini = cd_off - 8 - tam
    p = ini + 8
    while p < cd_off - 24:
        n = struct.unpack_from('<Q', raw, p)[0]
        if n <= 4 or p + 8 + n > cd_off:
            break
        ident = struct.unpack_from('<I', raw, p + 8)[0]
        esquemas.append(ident)
        p += 8 + n

if BUNDLE:
    print(f"firma de jar (META-INF): {'SI' if jar_firmado else 'NO'}")
else:
    print(f"firma v2 (APK Sig Block 42): {'SI' if v2 else 'NO'}")
for i in esquemas:
    nom = {0x7109871a: 'v2', 0xf05368c0: 'v3', 0x1b93ad61: 'sello v3.1/otros'}.get(i, hex(i))
    print(f"  bloque {nom}")

# --- 2. El manifiesto: paquete y permisos ----------------------------------
#
#  Y SON DOS FORMATOS. Un APK lleva el manifiesto en XML binario, con su pool
#  de cadenas en UTF-16. Un bundle lo lleva en PROTOBUF, que es otro fichero
#  con el mismo nombre: leerlo con el lector del APK da un pool de siete mil
#  millones de cadenas y revienta. Ahi los nombres viajan como texto plano, asi
#  que se sacan las tiras imprimibles - que es todo lo que hace falta para
#  contrastar el paquete y los cuatro permisos.
if BUNDLE:
    ax = z.read('base/manifest/AndroidManifest.xml')
    #  Y se buscan por su FORMA y no como tiras imprimibles sueltas: en
    #  protobuf cada nombre va pegado al byte de longitud del campo siguiente,
    #  asi que una tira cruda sale como `android.permission.RECORD_AUDIO(` y
    #  como `com.artifacts.zati"N` - ni contrasta ni se lee.
    cadenas = [t.decode('ascii') for t in
               re.findall(rb'android\.permission\.[A-Z_]+|com\.[a-z0-9_.]+', ax)]
else:
    ax = z.read('AndroidManifest.xml')
    # Cadenas del pool: cabecera 8 + tipo 0x0001, cuenta en +8
    n_str = struct.unpack_from('<I', ax, 16)[0]
    off_str = struct.unpack_from('<I', ax, 28)[0]
    offs = struct.unpack_from(f'<{n_str}I', ax, 36)
    base = 8 + off_str
    cadenas = []
    for o in offs:
        p = base + o
        ln = struct.unpack_from('<H', ax, p)[0]
        cadenas.append(ax[p + 2:p + 2 + ln * 2].decode('utf-16-le', 'replace'))

paquete = [c for c in cadenas if c.startswith('com.') and ' ' not in c]
permisos = sorted({c for c in cadenas if c.startswith('android.permission.')})

#  LOS PERMISOS SE CONTRASTAN, no se imprimen.
#
#  Se listaban y ya: uno que DESAPARECIERA -y con el la grabacion por micro, o
#  la escritura en los telefonos viejos- salia como texto y no como fallo, y
#  uno nuevo que apareciera sin querer es lo que hace que Play pida una
#  declaracion mas y la ficha se quede en revision. Son cuatro y estan
#  decididos: el de escritura va acotado a SDK 28 en el manifiesto, que es la
#  forma que Play acepta.
ESPERADOS = ['android.permission.READ_EXTERNAL_STORAGE',
             'android.permission.READ_MEDIA_AUDIO',
             'android.permission.RECORD_AUDIO',
             'android.permission.WRITE_EXTERNAL_STORAGE']
print(f"paquete: {paquete[:2]}")
print(f"permisos ({len(permisos)}):")
for p in permisos:
    print("  " + p)

# --- 3. Las .so: alineadas a 16 KB por su OFFSET DE DATOS -------------------
#  No por el de la cabecera: lo que mapea el cargador es donde empiezan los
#  datos, y comprobar el otro da un si a un fichero que Android rechaza.
malas = []
for i in ([] if BUNDLE else z.infolist()):
    if not i.filename.endswith('.so'):
        continue
    h = i.header_offset
    nlen, elen = struct.unpack_from('<HH', raw, h + 26)
    datos = h + 30 + nlen + elen
    if datos % 16384 != 0:
        malas.append((i.filename, datos % 16384))
    print(f"  {i.filename}  datos en {datos}  {'alineado' if datos % 16384 == 0 else 'DESALINEADO'}"
          f"  {'sin comprimir' if i.compress_type == 0 else 'COMPRIMIDO'}")

if BUNDLE:
    print("  alineacion: no aplica a un bundle - los APK los genera Play")

print(f"\ntamano: {len(raw)} bytes")

# --- 4. LO QUE VIAJA ESCRITO DENTRO ----------------------------------------
#
#  Las tres comprobaciones de arriba miran la FORMA del paquete -firma,
#  permisos, alineacion- y ninguna mira lo que hay ESCRITO dentro. Un literal
#  de C++ acaba en la .so, y ahi se queda: es lo unico de este proyecto que
#  llega al telefono de otra persona.
#
#  Se midio antes de escribir la regla y hoy sale a cero, que es justo cuando
#  hay que ponerla: una afirmacion sin medida se publica en cuanto deja de ser
#  verdad, y este banco lleva pagandolo once veces. Ver EXPORTAR, ver la
#  cabecera de la cara.
#
#  DOS LISTAS, Y SOLO UNA VIVE AQUI. Las marcas ajenas las lee `marcas.py` de
#  las reglas 1 y 2 de `Tests/marcas.md`, asi que se importan: una regla escrita
#  dos veces son dos reglas, y esa ya tiene dueno. La otra no esta escrita en
#  ningun documento del repositorio y por eso se escribe aqui.
#
#  Y LOS TERMINOS SE ELIGIERON MIDIENDO sobre un APK que se sabe limpio, no a
#  ojo: en un binario de 16 MB cualquier subcadena corta sale por azar -un
#  barrido de `\bIA\b` da veinte coincidencias y las veinte son ruido, `$(IA!`,
#  `9BpC(Ia`-. Los de abajo dan CERO en el binario de hoy, `gpt` y `llm`
#  incluidos, asi que un uno significa algo.
IA = ["claude", "anthropic", "openai", "chatgpt", "copilot", "gemini", "llama",
      "sonnet", "gpt", "llm",
      "co-authored-by", "generated with", "claude.ai",
      "inteligencia artificial", "artificial intelligence"]

#  Y DOS QUE SON ADEMAS PALABRAS DE AUDIO, asi que se piden CON CONTEXTO.
#
#  «opus» a secas suspendio la primera APK que llego hasta aqui: es
#  `AudioFormat::OPUS`, un formato NUEVO de Oboe 1.10.0 -la version que trajo
#  el salto a JUCE 8.0.15- cuyo `Utilities.cpp` devuelve el literal "OPUS" y lo
#  deja en la .rodata. Un codec de audio en una app de audio no es una alusion
#  a nada, y «haiku» es ademas un sistema operativo que JUCE nombra.
#
#  Quitarlos de la lista seria dejar ciega la comprobacion justo en los dos
#  nombres que mas probable es que aparezcan. Lo que se pide es lo que los
#  separa: un modelo se nombra con su familia delante o con su numero detras
#  -«Claude Opus», «Opus 5»- y un formato de audio va suelto entre AAC y PCM.
#  Roto a proposito con «generated with Claude Opus 5», los dos saltan.
IA_CON_CONTEXTO = [
    (t, [r"claude[- ]" + t, t + r"[- ]?[0-9]", r"modelo[- ]" + t, r"model[- ]" + t])
    for t in ("opus", "haiku")]

#  LA CADENA DE CONTROL, que es lo que separa este cero de una linea que
#  imprime OK. Si el barrido no encuentra el texto de la app es que no esta
#  llegando a el -otra compresion, otro formato, un `strings` que no ve
#  UTF-16- y entonces daria verde sin haber mirado nada. Falla en vez de pasar,
#  igual que `marcas.py` cuando no puede leer su documento.
CONTROL = "ZATI SAMPLER"


def cadenas (b):
    """Lo imprimible de un miembro del zip, de cuatro caracteres en adelante."""
    return [t.decode ("latin1") for t in re.findall (rb"[\x20-\x7e]{4,}", b)]


miembros = {i.filename: z.read (i.filename) for i in z.infolist()}
texto = {f: cadenas (b) for f, b in miembros.items()}

marcas, numeros = prohibido()
if not marcas or not numeros:
    escrito = ["no puedo leer las reglas 1 y 2 de Tests/marcas.md: sin lista no hay barrido"]
else:
    #  Las mismas guardas que `marcas.py`, y por lo mismo: letra alrededor de
    #  una palabra, y los numeros SOLO con forma de modelo, que 404 es tambien
    #  una coordenada.
    reglas =  [(t, re.compile (r"(?<![A-Za-z0-9])" + re.escape (t) + r"(?![A-Za-z0-9])", re.I))
               for t in IA]
    reglas += [(t, re.compile ("|".join (formas), re.I)) for t, formas in IA_CON_CONTEXTO]
    reglas += [(t, re.compile (r"(?<![A-Za-z0-9])" + re.escape (t) + r"(?![A-Za-z0-9])"))
               for t in sorted (marcas)]
    reglas += [("*" + n, re.compile (r"(?<![A-Za-z0-9])[A-Za-z]{1,3}[- ]?" + n + r"(?![0-9])"))
               for n in sorted (numeros)]

    escrito = []
    for nombre, rx in reglas:
        donde = {}
        for f, ss in texto.items():
            n = sum (1 for t in ss if rx.search (t))
            if n: donde[f] = n
        if donde:
            escrito.append ("«%s» en %s"
                            % (nombre, ", ".join ("%s x%d" % (f, n) for f, n in donde.items())))

visto = any (CONTROL in t for ss in texto.values() for t in ss)
print()
print ("lo escrito dentro: %d miembros, %d cadenas, %d terminos vigilados"
       % (len (miembros), sum (len (ss) for ss in texto.values()), len (IA) + len (marcas) + len (numeros)))
print ("  control «%s»: %s" % (CONTROL, "encontrado" if visto else "NO ENCONTRADO"))
for e in escrito:
    print ("  FALLA  " + e)
if not escrito and visto:
    print ("  ni una marca ni una alusion en lo que se instala")

#  Y EL VEREDICTO SALE POR EL CODIGO DE SALIDA.
#
#  Imprimia OK o REVISAR y terminaba con cero pasara lo que pasara, asi que
#  ponerlo en el CI no habria servido de nada: una APK sin firma v2, con la .so
#  desalineada o con un permiso de mas se publicaba igual y lo unico que
#  quedaba era una linea de texto que hay que acordarse de leer.
faltan = [p for p in ESPERADOS if p not in permisos]
sobran = [p for p in permisos if p not in ESPERADOS]
mal = []
if BUNDLE:
    if not jar_firmado: mal.append("el bundle no esta firmado")
elif not v2:            mal.append("sin firma v2")
if malas:      mal.append("%d .so desalineadas" % len(malas))
if faltan:     mal.append("faltan permisos: " + ", ".join(faltan))
if sobran:     mal.append("permisos de mas: " + ", ".join(sobran))
if not any(p == 'com.artifacts.zati' for p in paquete):
    mal.append("el paquete no es com.artifacts.zati")
if not visto:  mal.append("el barrido no ve el texto de la app: no mide nada")
if escrito:    mal.append("escrito dentro: " + "; ".join(escrito))

print("VEREDICTO:", "OK" if not mal else "REVISAR — " + "; ".join(mal))
sys.exit (0 if not mal else 1)
