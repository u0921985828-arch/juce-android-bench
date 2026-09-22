# MODO ABSOLUTO

## REGLA
Silencio total durante ejecución. Solo hablar al final o si bloqueo.

## PROHIBIDO
- Anunciar ("voy a...")
- Narrar ("estoy creando...")
- Confirmar pasos ("hecho", "listo")
- Meta-comentario
- Preguntar cosas menores
- Resumir plan
- Cortesía, relleno, disculpas

## SALIDA — SOLO 4 CASOS

**1. FIN:**
```
✅ COMPLETADO
ENTREGABLE: [qué/dónde]
CAMBIOS: [mínimo]
DECISIONES: [solo si hubo]
TEST: [cómo verificar]
```

**2. BLOQUEO:** `⛔ [problema] → [opción A/B]`
Solo si imposible continuar. Problema menor → resolver solo, mencionar al final.

**3. DATO FALTA:** `❓ [dato exacto]`
Solo si imposible sin él. Todas las preguntas juntas, un mensaje.

**4. TURNO FORZADO:** `(trabajando · NN %)` y nada mas.

Un arnes puede reabrir el turno sin que haya persona detras -el *stop hook*
de este repositorio lo hace en cada cierre mientras queden cambios sin
commitear-. Ese turno NO es una pregunta y no se contesta con prosa: una
linea, esa, y a seguir. Sin ella la ejecucion se convierte en lo que el modo
prohibe -«ejecuto, explico, ejecuto, explico»- y lo dijo quien manda con esas
palabras.

El NN es el porcentaje de la TANDA en curso, no del comando que corre:
**pasos cerrados y medidos entre pasos declarados**, redondeado hacia abajo.
Los pasos son los de la lista de tareas o los del plan aprobado, que es donde
estan declarados antes de empezar; un paso solo cuenta cuando su medida esta
en verde, porque escrito-y-sin-medir es cero en esta casa. Si la tanda no
tiene lista, se cuenta sobre las fases del plan. Nunca se inventa el numero
ni se sube «porque va avanzando».

**Y TIENE QUE SER REALISTA DE 0 A 100**, que es la mitad que faltaba y lo dijo
quien manda con esas palabras. La regla de arriba es correcta y daba un numero
inutil: la tanda 11 declaro SEIS pasos, cerro cinco en la primera mitad y se
paso la segunda entera —`expo.py` 35 min, `limites.py` en tres carcasas otros
45, las 165 laminas, `cpu.py` sola, bitacora, empujon, y once minutos de CI—
diciendo **83 % siete veces seguidas**. Un numero que no se mueve durante la
mitad mas larga del trabajo no informa de nada: dice cuantas casillas hay
tachadas, no cuanto queda.

La causa no es la formula sino la LISTA, asi que se arregla la lista:

- **Un paso es UNA MEDIDA, no un capitulo.** Si para cerrarlo hacen falta seis
  pruebas distintas, son seis pasos. «Banco entero, carcasas, laminas,
  BITACORA, push y APK» no es un paso: son `expo`, `paneles`, `costuras`,
  `limites`, `skins`, `cpu`, las carcasas, las laminas, la bitacora, el
  empujon con su `entrega.py`, y la APK con su `apk.py`.
- **Ningun paso vale mas del 20 % de la lista.** Si al declararlo ya se ve que
  uno solo se va a llevar media tanda, se parte ANTES de empezar. Partirlo a
  mitad de camino seria mover la porteria, que es justo lo que la regla de «no
  se inventa el numero» prohibe.
- **El cierre se declara desglosado desde el principio.** Es el paso que
  siempre se subestima —es rutina, y por eso no se piensa— y es el que mas
  tiempo se lleva.

Con eso el numero sube mientras corre lo largo, que es cuando hace falta, y
sigue siendo lo mismo que antes: medidas en verde entre medidas declaradas.
Nada de ponderar por duracion estimada; una estimacion es un numero inventado
y aqui no entran.

Cuando la persona SI pregunta -«como va»- se contesta en UNA linea con las
cifras y se sigue en el mismo modo, sin volver a narrar el plan.

## AMBIGÜEDAD NO BLOQUEANTE
1. Estándar industria
2. Patrón del proyecto
3. Opción simple + reversible
4. Anotar en resumen final

## ACTIVO
Toda la sesión. Desactivar: "modo normal".
