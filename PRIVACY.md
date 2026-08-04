# Política de privacidad — ZATI

**Responsable:** ARTiFACTS
**Aplicación:** ZATI (`com.artifacts.zati`)
**Última actualización:** 4 de agosto de 2026

## Resumen

ZATI no recoge, no envía y no comparte ningún dato personal. La app no tiene
servidores, no tiene cuentas y **no pide permiso de internet** — el paquete se
declara sin `android.permission.INTERNET`, así que técnicamente no puede
enviar nada a ninguna parte aunque quisiera.

## Qué permisos pide y para qué

| Permiso | Para qué |
|---|---|
| `RECORD_AUDIO` (micrófono) | Grabar sonido en un pad cuando pulsas GRABAR MIC. Nada más. |
| `READ_MEDIA_AUDIO` (o `READ_EXTERNAL_STORAGE` en Android 12 y anteriores) | Ver y abrir tus propios archivos de audio para cargarlos en los pads. |

El micrófono sólo se abre mientras estás grabando, y se cierra al parar, al
salir de la app o si el sistema le cede el audio a otra aplicación. Lo grabado
se guarda en tu teléfono y en ningún otro sitio.

## Dónde queda lo que haces

Todo — samples, proyectos, grabaciones, exportaciones y la sesión que la app
recupera al arrancar — se guarda en el almacenamiento del propio dispositivo,
bajo una carpeta `ZATI/` en tu carpeta de música (o en el área privada de la
app si el sistema no da acceso a la compartida). Nada de eso sale del
teléfono. Si desinstalas la app, puedes borrar esa carpeta con cualquier
gestor de archivos.

## Datos de terceros

ZATI no incluye SDK de publicidad, ni analítica, ni informes de fallos, ni
redes sociales. No hay identificadores publicitarios ni seguimiento.

## Menores

La app no está dirigida a menores de 13 años en particular, y como no recoge
datos, tampoco recoge datos de menores.

## Cambios

Si alguna versión futura llegara a recoger algún dato, esta política se
actualizaría **antes** de publicarla y el cambio se indicaría en la ficha de
Google Play.

## Contacto

Para cualquier duda sobre privacidad: eddierealting@gmail.com
