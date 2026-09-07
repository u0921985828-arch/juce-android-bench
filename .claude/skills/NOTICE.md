# De donde salen estas habilidades

*(Este fichero nombra `CLAUDE.md`, que es el cuaderno de bitacora de la casa y
manda sobre todo lo de aqui. No se versiona: vive en el disco de quien trabaja
en esto y no en el repositorio, asi que un clon limpio no lo trae.)*

`banco` es de la casa. Las otras son de fuera, copiadas **sin tocar** para
poder actualizarlas desde su origen:

| carpeta | origen | licencia |
|---|---|---|
| `systematic-debugging` | [obra/superpowers](https://github.com/obra/superpowers) — Jesse Vincent | MIT |
| `writing-for-agents` | [mattpocock/skills](https://github.com/mattpocock/skills) — Matt Pocock | MIT |
| `landing-page-mastery` | [abracu/claude-skills](https://github.com/abracu/claude-skills) — Mafia Claude Skills | Apache 2.0 |

La de Apache lleva su `LICENSE` dentro de la carpeta, que es lo que esa
licencia pide y las MIT no.

Se instalan **en el repositorio y no en la maquina**: esta sesion corre en un
contenedor que se recicla, asi que lo que se deja en `~/.claude/skills` se
pierde al cerrar. En `.claude/skills/` viaja con el proyecto y lo tiene
cualquiera que lo clone.

## Por que estas dos y no las cincuenta

De los once repositorios se leyeron todos. La regla que decide es la de la
casa: *una funcion, un dueno*. Una habilidad que dice lo que `CLAUDE.md` ya
dice no anade nada — anade un segundo sitio donde mantenerlo y una forma de
que los dos se contradigan.

**Entran las dos que saben algo que aqui no estaba escrito:**

- `systematic-debugging` — causa raiz antes que arreglo, e instrumentar los
  bordes entre componentes ANTES de proponer nada. Es lo que hace la caja
  negra (`Source/Bitacora.h`) para el unico fallo que solo pasa en el
  telefono, pero escrito como metodo y no como una clase suelta.
- `writing-for-agents` — como se escribe un documento que lee un agente.
  Aplica directamente a `CLAUDE.md`, que es el documento mas grande de este
  proyecto y el que mas riesgo tiene de **sedimentar**: capas viejas que se
  quedan porque anadir parece seguro y quitar parece arriesgado.

**Se quedan fuera, y por que:**

- `brainstorming`, `grilling`, `grill-me`, `wait-what`, `to-questionnaire` —
  son entrevistas: preguntan antes de hacer. `MODO_ABSOLUTO.md` dice lo
  contrario, y no se instala una habilidad que pelea con el contrato.
- `verification-before-completion` — «evidencia antes que afirmacion» ya es la
  primera regla de `CLAUDE.md` («nada se entrega sin medirlo»), con el banco
  como dueno.
- `test-driven-development`, `tdd` — la version de esta casa es mas fuerte y
  mas concreta: el banco, mas romper el codigo a proposito para ver la prueba
  en rojo.
- `code-review` (dos ejes: normas y encargo) — el metodo esta bien y ya vive
  en la seccion «Al revisar» de `CLAUDE.md`. Ademas reparte el trabajo entre
  subagentes, que esta sesion no usa salvo que se pidan.
- `writing-plans`, `executing-plans`, `to-tickets`, `triage`, `wayfinder` —
  suponen un gestor de incidencias que este proyecto no tiene.
- `using-git-worktrees` — aqui hay una rama de trabajo fija y un CI que
  compila de ella; un worktree se pelea con eso.
- `skill-creator` (Anthropic) ya viene instalado aparte.
- `obsidian-skills`, `ui-ux-pro-max-skill`, `humanizer`, `awesome-claude-code`,
  `Ay-Skills` — o son indices, o no aplican a una app JUCE. `ui-ux-pro-max`
  ademas chocaria de frente con `Metrics` y las cuatro carcasas, que son un
  sistema de diseno **medido**.
- `claude-mem`, `claude-context` — son infraestructura que se instala en la
  maquina de la persona (un servidor MCP y una base vectorial), no algo que
  se aplique a un repositorio. Y en un contenedor que se recicla no
  sobreviven.

## La cuarta, y las cuatro que venian con ella

De `abracu/claude-skills` -cinco habilidades en espanol, Apache 2.0- entra
**una**, y no por la app: entra por lo que este proyecto tiene pendiente y no
sabe hacer.

- `landing-page-mastery` **ENTRA**. ZATI no tiene una linea de web y aun asi
  necesita dos paginas: la **politica de privacidad alojada**, que Play exige
  y que aqui existe como `PRIVACY.md` sin sitio donde vivir, y la ficha de la
  tienda. Estructura, copy y auditoria de conversion no estan escritos en
  ninguna parte de esta casa, que es la regla: entra la que sabe algo que aqui
  no estaba.

Y las otras cuatro se quedan fuera:

- `frontend-design` — **choca de frente con el contrato**, que es el mismo
  motivo por el que se cayeron las de entrevista. Dice «elige un extremo:
  maximalismo caotico...», «que sea INOLVIDABLE». Aqui el chasis es acromatico
  a proposito, todos los numeros de maquetado viven en `Metrics` y un control
  activo se declara por TONO. Una habilidad que pide audacia estetica contra un
  sistema de diseno medido no anade, resta.
- `vercel-react-best-practices` — React y Next.js. Este repositorio no tiene
  una linea de ninguno de los dos.
- `template-skill` — como se escribe una habilidad. Ese trabajo ya tiene dueno:
  `writing-for-agents` para el metodo y `banco/SKILL.md` como ejemplo hecho.
- `gestor-autonomos` — administracion de un autonomo espanol. Util para la
  persona que publica esto; no tiene nada que decirle a un repositorio de C++.

## Y luego llegaron doce de golpe, pedidas por su nombre

Lo de arriba es un CRITERIO: se leyeron once repositorios y entraron tres. Lo
que hay debajo es otra cosa - una lista que la persona pidio una por una con
`npx skills add`. No se aplica el filtro a lo que alguien pide explicitamente:
se instala, y aqui queda escrito que entraron por esa puerta y no por la otra.

Instaladas, todas de `obra/superpowers` salvo donde se diga:

| carpeta | origen |
|---|---|
| `requesting-code-review` | obra/superpowers |
| `writing-plans` | obra/superpowers |
| `subagent-driven-development` | obra/superpowers |
| `test-driven-development` | obra/superpowers |
| `ai-first-engineering` | affaan-m/ECC |
| `product-capability` | affaan-m/ECC |
| `quality-nonconformance` | affaan-m/ECC |
| `brand` | nextlevelbuilder/ui-ux-pro-max-skill |
| `imagegen-frontend-mobile` | Leonxlnx/taste-skill |
| `gpt-taste` | Leonxlnx/taste-skill |

**Tres de estas las habia rechazado la lista de arriba**, y la contradiccion se
escribe en vez de taparse, que es el fallo que este proyecto persigue - un
documento que va por detras de lo que hay:

- `writing-plans` y `to-tickets` se cayeron por «suponen un gestor de
  incidencias que este proyecto no tiene». `writing-plans` esta instalada.
- `test-driven-development` se cayo por «la version de esta casa es mas fuerte:
  el banco, mas romper el codigo a proposito». Esta instalada.
- `ui-ux-pro-max-skill` se cayo por chocar con `Metrics` y las cuatro
  carcasas. Su skill `brand` esta instalada.

Quien las use tiene que saber que en un choque **manda `CLAUDE.md`**: el banco
decide si algo entra, los numeros de maquetado viven en `Metrics`, y el hilo de
audio no reserva. Una habilidad que diga lo contrario esta equivocada AQUI,
por buena que sea en general.

**Y dos no existen.** `obsidian-vault` y `ubiquitous-language` no estan en
`mattpocock/skills`: el instalador contesta «No matching skills found». Ese
repositorio ofrece `ask-matt`, `code-review`, `codebase-design`,
`diagnosing-bugs`, `domain-modeling`, `implement`, `prototype`, `research`,
`tdd`, `to-spec`, `to-tickets`, `triage`, `wayfinder`, `claude-handoff` y
`loop-me`, entre otras.

**Y `systematic-debugging` estaba a medias.** La copia del repositorio tenia
cinco ficheros y el origen tiene doce: faltaban `CREATION-LOG.md`,
`find-polluter.sh`, el ejemplo de espera por condicion y las tres pruebas a
presion. Sincronizada. La regla es «copiada SIN TOCAR para poder actualizarla
desde su origen», y media copia no se puede actualizar: se puede sustituir, que
no es lo mismo.

**Y `--global` no sobrevive aqui.** `npx skills add ... --global` deja la
habilidad en `~/.claude/skills`, y este contenedor se recicla. Todas estan
ademas en `.claude/skills/`, que viaja con el repositorio. Si alguna se
actualiza arriba, hay que volver a copiarla.

## Y dos de `curiositech/some_claude_skills` (MIT, 185 habilidades)

Pedidas por su nombre, como las de arriba, y con lo que cubren y lo que NO
escrito al lado - porque las dos suenan a esta app mas de lo que son:

- `voice-audio-engineer`. Sabe de **LUFS**, de-essing y mezcla de dialogo, y lo
  de LUFS toca de cerca: la fabrica de aqui se iguala con la curva K de
  BS.1770, que es la misma norma. El resto -TTS, clonado de voz, podcast- pasa
  por herramientas MCP de ElevenLabs que esta sesion no tiene, asi que ahi no
  hace nada por si sola.
- `2000s-visualization-expert`. Milkdrop, Butterchurn, GLSL y la
  `AnalyserNode` de la Web Audio API. Esta app tiene un espectro en su cristal
  y lo pinta en C++ con JUCE: lo que esa habilidad sabe de un analizador FFT
  vale, y todo lo que dice de WebGL y del navegador no aplica.

Y ese repositorio tiene ademas `sound-engineer` -HRTF, ambisonica, Wwise/FMOD,
sonido de interfaz- que no se pidio y no se instala: audio de juego y espacial
no es lo que hace un sampler de pads, y las otras dos ya cubren lo que si
tocaba. Queda anotada por si algun dia hacen falta los sonidos de la propia
interfaz.

## Y SIETE QUE NO NOMBRABA NADIE

Este fichero existe para que cada carpeta tenga procedencia, y al contarlas
salieron **veintitres carpetas y dieciseis citadas**. Las siete que faltaban no
son un olvido de redaccion: una carpeta sin procedencia no se puede actualizar
desde su origen —que es la regla de la casa para todo lo de fuera—, ni retirar
si ese origen cambia de licencia, ni defender si alguien pregunta de donde
salio.

Se anota lo que se puede **comprobar en el arbol**, con la evidencia al lado, y
lo que no aparece se escribe «sin declarar» en vez de rellenarse a ojo: una
procedencia inventada es peor que un hueco, porque el hueco se ve.

| carpeta | origen | licencia | como se sabe |
|---|---|---|---|
| `caveman` | Julius Brussee | MIT (con excepciones) | `LICENSE` dentro, (c) 2026 |
| `game-audio` | comunidad | **sin declarar** | `aas-source: community` en su frontmatter |
| `sleek-design-mobile-apps` | sleek.design | **sin declarar** | pide `SLEEK_API_KEY` y solo habla con `https://sleek.design` |
| `investigate-first` | **sin declarar** | **sin declarar** | familia de cuatro (abajo) |
| `safe-refactor` | **sin declarar** | **sin declarar** | familia de cuatro |
| `surgical-patch` | **sin declarar** | **sin declarar** | familia de cuatro |
| `verify-and-stop` | **sin declarar** | **sin declarar** | familia de cuatro |

**La familia de cuatro** —`investigate-first`, `safe-refactor`,
`surgical-patch`, `verify-and-stop`— se agrupa por una **firma** y no por
parecerse: las cuatro, y solo esas cuatro de las veintitres, traen un
`agents/openai.yaml` con la misma forma exacta (`interface:` con
`display_name`, `short_description` y un `default_prompt` que dice
`Use $nombre ...`). Son un juego del mismo sitio, y ese sitio no esta escrito
en ninguna de las cuatro.

Y las cuatro dicen, con otras palabras, lo que esta casa ya tiene escrito y
medido: investigar antes de tocar es `systematic-debugging`; refactorizar sin
cambiar el comportamiento observable es la seccion «Al revisar» de `CLAUDE.md`;
y «probar lo justo y parar» es el banco. Manda `CLAUDE.md`, como con las otras.

**Y lo que falta, que es de licencia y no de orden.** De las cinco carpetas
MIT, cuatro traen su `LICENSE` dentro (`2000s-visualization-expert`, `caveman`,
`voice-audio-engineer`, y `landing-page-mastery` la suya de Apache) y las
**cinco de `obra/superpowers` no traen ninguno**. Este fichero decia ademas que
llevar el `LICENSE` dentro «es lo que esa licencia pide y las MIT no», y eso es
falso: la MIT lo pide igual — *«The above copyright notice and this permission
notice shall be included in all copies or substantial portions of the
Software»*. Se anota en vez de escribir el fichero de memoria, que un `LICENSE`
reconstruido no es el `LICENSE` de nadie. Lo mismo con las tres de ECC, `brand`,
`game-audio` y las cuatro de la familia: sin licencia declarada, redistribuirlas
es una decision que nadie ha tomado por escrito.
