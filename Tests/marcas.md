# NINGUNA MARCA AJENA — las reglas que `Tests/marcas.py` mide

Este fichero es el **dueño** de la lista. `Tests/marcas.py` y `Tests/apk.py` la
leen de aquí y no la escriben en su propio código: una lista escrita dos veces
son dos reglas, y el día que a una se le añadiera un nombre la otra seguiría
mirando los de ayer. Si este documento cambia de forma y las dos regex ancladas
no casan, las dos pruebas **FALLAN** en vez de dar verde con la lista vacía, que
es como una prueba de esta clase se muere sin que nadie se entere.

Esto no es opinión, es la superficie por la que a una app de sampler la retiran
de Play. El mecanismo real **no es una demanda: es un formulario de reclamación
de propiedad intelectual**, que retira la ficha sin juicio, y los avisos
acumulados pueden terminar la cuenta de desarrollador.

## Reglas de marca — no reintroducir

Lo que **no puede volver a entrar**, ni en código, ni en comentarios, ni en la
ficha, ni en capturas:

1. **Nombres de modelo ajenos.** SP-404, SP-303, SP-555, TR-808, TB-303, MPC,
   y las marcas Roland, BOSS, AIRA, Akai. Tampoco "estilo SP-404" ni
   "inspirado en": aunque el uso nominativo sea defendible en algunos países,
   el formulario de Play no distingue y retira primero.
2. **Números de modelo cercanos.** El proyecto se llamaba FX-404 y ese era el
   mayor riesgo que ha tenido: misma categoría, mismo número, mismo comprador
   que el SP-404. Nada de 404, 303, 808, 909 ni 555 en el nombre.
3. **Nombres de efecto acuñados por un tercero.** "DJFX LOOPER" es una
   invención de Roland, no un término del sector. Lo que se exige a los
   nuestros es un CRITERIO y no una lista: **abreviatura genérica y
   descriptiva del sector, sin número de modelo y sin nombre acuñado por
   nadie** — que es justo lo que los hace seguros.

   Este punto **enumeraba los efectos a mano** y se quedó viejo dos veces: dio
   ISO y CRSH cuando el código decía FLT y BIT, y luego se quedó en seis
   mientras la tabla iba por once. La tercera vez habría sido con quince. Una
   lista de los nuestros escrita aquí es una copia de `fxDefs`, y una copia se
   queda vieja; el criterio no. Ahora `Tests/marcas.py` pasa **los nombres que
   `fxDefs` tiene de verdad** por las reglas 1 y 2 de arriba, así que un efecto
   llamado `LOOPER` o `TB3` falla en vez de publicarse.

4. **Trade dress.** Nada de cuerpo gris oscuro con naranja, ni réplica
   fotorrealista de un aparato, ni serigrafía calcada. Las marcas registradas
   en la UE llegan hasta la secuencia de colores de los botones de un aparato
   de ritmos, así que un chasis oscuro con acento naranja es exactamente lo que
   no se hace.
5. **Contenido de fábrica de otro.** Los samples y patrones preset de un
   aparato son grabaciones de su fabricante. Si algún día se incluye contenido
   de fábrica, tiene que ser original o CC0 **con la procedencia documentada
   por escrito**.

   Esta regla **se incumplió** durante varias tandas: el binario llevó 31
   grabaciones de máquinas reales, 916 250 bytes, sin una línea de licencia,
   mientras `THIRD-PARTY.md` afirmaba lo contrario. Se resolvió quitándolas —
   los 64 sonidos se sintetizan— y con ellas se fueron los dos ficheros que
   nombraban los aparatos. Hoy `Tests/marcas.py` corre **sin una sola
   excepción**: ver `THIRD-PARTY.md`.

## Lo que sí es libre

Conviene tenerlo claro porque es casi toda la app: la rejilla de 4×4, samplear,
chop a pads, el secuenciador de pasos, resample, los efectos como envíos, una
mesa con canales por los que pasan los efectos, modo cinta/tono, choke groups,
bancos de patrones. Son ideas y funciones, y no son protegibles por copyright
(17 USC 102(b), y art. 1.2 de la Directiva europea de software). Cualquier
patente sobre un aparato de 2005 está caducada. Nadie es dueño del concepto de
sampler.

## Cómo se lee esto desde el banco

`Tests/marcas.py::prohibido()` saca de la regla 1 los modelos con forma
`XX-999`, las marcas de la enumeración que encabeza «marcas», y `MPC` suelto;
y de la regla 2, las tres cifras. Los números se buscan **sólo con forma de
modelo** —letras, separador opcional, las tres cifras— porque la regla dice «en
el NOMBRE» y no «en ningún sitio»: `404` es también una coordenada y `808` vive
dentro de `1786902180894`, que es un nombre de fichero de Instagram citado en un
comentario.

Roto a propósito, con su cifra: `SD 808` saca
`FALLA Source/Kits.h:330 «*808»`; un comentario con `MPC`, lo suyo; y borrar
este fichero, `FALLA no puedo leer las reglas 1 y 2`.
