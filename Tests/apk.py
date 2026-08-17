#!/usr/bin/env python3
"""Comprueba un APK sin herramientas de Android: firma v2, paquete, permisos y
alineacion de las bibliotecas. Escrito a mano porque este entorno no trae el
SDK, y porque lo que hace falta comprobar son cuatro cosas concretas."""
import sys, zipfile, struct

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
print("VEREDICTO:", "OK" if (v2 and not malas) else "REVISAR")
