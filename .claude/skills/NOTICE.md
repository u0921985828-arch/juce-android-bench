# De donde salen estas habilidades

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
