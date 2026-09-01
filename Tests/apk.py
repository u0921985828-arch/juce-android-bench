#!/usr/bin/env python3
"""Comprueba un APK sin herramientas de Android: firma v2, paquete, permisos y
alineacion de las bibliotecas. Escrito a mano porque este entorno no trae el
SDK, y porque lo que hace falta comprobar son cuatro cosas concretas."""
import os, re, sys, zipfile, struct

sys.path.insert (0, os.path.dirname (os.path.abspath (__file__)))
from marcas import prohibido

apk = sys.argv[1]
raw = open(apk, 'rb').read()

# --- 1. El bloque de firma v2, justo antes del directorio central -----------
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

print(f"firma v2 (APK Sig Block 42): {'SI' if v2 else 'NO'}")
for i in esquemas:
    nom = {0x7109871a: 'v2', 0xf05368c0: 'v3', 0x1b93ad61: 'sello v3.1/otros'}.get(i, hex(i))
    print(f"  bloque {nom}")

# --- 2. El manifiesto binario: paquete y permisos ---------------------------
z = zipfile.ZipFile(apk)
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
#  decididos: ver el bloque del permiso de escritura en CLAUDE.md.
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
for i in z.infolist():
    if not i.filename.endswith('.so'):
        continue
    h = i.header_offset
    nlen, elen = struct.unpack_from('<HH', raw, h + 26)
    datos = h + 30 + nlen + elen
    if datos % 16384 != 0:
        malas.append((i.filename, datos % 16384))
    print(f"  {i.filename}  datos en {datos}  {'alineado' if datos % 16384 == 0 else 'DESALINEADO'}"
          f"  {'sin comprimir' if i.compress_type == 0 else 'COMPRIMIDO'}")

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
#  cabecera de la cara, ver GOOGLE-PLAY.md 1.4.
#
#  DOS LISTAS, Y SOLO UNA VIVE AQUI. Las marcas ajenas las lee `marcas.py` de
#  las reglas 1 y 2 de GOOGLE-PLAY.md, asi que se importan: una regla escrita
#  dos veces son dos reglas, y esa ya tiene dueno. La otra no esta escrita en
#  ningun documento del repositorio y por eso se escribe aqui.
#
#  Y LOS TERMINOS SE ELIGIERON MIDIENDO sobre un APK que se sabe limpio, no a
#  ojo: en un binario de 16 MB cualquier subcadena corta sale por azar -un
#  barrido de `\bIA\b` da veinte coincidencias y las veinte son ruido, `$(IA!`,
#  `9BpC(Ia`-. Los diecisiete de abajo dan CERO en el binario de hoy, `opus`,
#  `gpt` y `llm` incluidos, asi que un uno significa algo.
IA = ["claude", "anthropic", "openai", "chatgpt", "copilot", "gemini", "llama",
      "opus", "sonnet", "haiku", "gpt", "llm",
      "co-authored-by", "generated with", "claude.ai",
      "inteligencia artificial", "artificial intelligence"]

#  LA CADENA DE CONTROL, que es lo que separa este cero de una linea que
#  imprime OK. Si el barrido no encuentra el texto de la app es que no esta
#  llegando a el -otra compresion, otro formato, un `strings` que no ve
#  UTF-16- y entonces daria verde sin haber mirado nada. Falla en vez de pasar,
#  igual que `marcas.py` cuando no puede leer la seccion 1.5.
CONTROL = "ZATI SAMPLER"


def cadenas (b):
    """Lo imprimible de un miembro del zip, de cuatro caracteres en adelante."""
    return [t.decode ("latin1") for t in re.findall (rb"[\x20-\x7e]{4,}", b)]


miembros = {i.filename: z.read (i.filename) for i in z.infolist()}
texto = {f: cadenas (b) for f, b in miembros.items()}

marcas, numeros = prohibido()
if not marcas or not numeros:
    escrito = ["no puedo leer las reglas 1 y 2 de GOOGLE-PLAY.md: sin lista no hay barrido"]
else:
    #  Las mismas guardas que `marcas.py`, y por lo mismo: letra alrededor de
    #  una palabra, y los numeros SOLO con forma de modelo, que 404 es tambien
    #  una coordenada.
    reglas =  [(t, re.compile (r"(?<![A-Za-z0-9])" + re.escape (t) + r"(?![A-Za-z0-9])", re.I))
               for t in IA]
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
if not v2:     mal.append("sin firma v2")
if malas:      mal.append("%d .so desalineadas" % len(malas))
if faltan:     mal.append("faltan permisos: " + ", ".join(faltan))
if sobran:     mal.append("permisos de mas: " + ", ".join(sobran))
if not any(p == 'com.artifacts.zati' for p in paquete):
    mal.append("el paquete no es com.artifacts.zati")
if not visto:  mal.append("el barrido no ve el texto de la app: no mide nada")
if escrito:    mal.append("escrito dentro: " + "; ".join(escrito))

print("VEREDICTO:", "OK" if not mal else "REVISAR — " + "; ".join(mal))
sys.exit (0 if not mal else 1)
