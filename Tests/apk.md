# La firma de la APK: con que clave va

`Tests/apk.py` lee este fichero. La linea que importa es la unica que empieza
por `firmante:` y lleva el **SHA-256 del certificado** con el que va firmada la
release, o `(sin declarar)` mientras no haya una.

Android compara ese certificado para decidir si una APK puede actualizar a
otra. Si cambia, la instalacion se niega con «package signature mismatch» y hay
que DESINSTALAR — o sea que quien ya la tuviera pierde su sesion y sus
proyectos. Un cambio de clave compila, pasa las tres comprobaciones de siempre
y se publica: por eso se fija aqui y se contrasta.

La de hoy es la de `ci/zati-debug.keystore`, que esta EN el repositorio a
proposito —es lo que hace que el telefono acepte actualizar entre corridas—.
Sirve para el bucle de pruebas y no para Play: con la clave publicada,
cualquiera podria firmar una actualizacion de esta app. El dia que los cuatro
secretos de subida esten puestos, esta huella cambia una vez, a proposito, y se
actualiza esta linea.

firmante: 17b0ef1a5d8b824fe9273e351b430e1d3ae68c8b6e283448c5a40f640483d72c
