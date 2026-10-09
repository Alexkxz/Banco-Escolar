# Banco Escolar: reglas del repositorio

- Para tareas de Banco Escolar, incluida la arquitectura del Panel Maestro y la sincronización, consulta la Skill `.agents/skills/banco-escolar/SKILL.md` y las referencias pertinentes.
- Ejecuta BUILD y reporta el resultado antes de cualquier carga. No ejecutes Upload ni Upload and Monitor sin autorización explícita.
- No inventes pines, estado físico del hardware, UIDs NFC, datos académicos ni resultados de prueba.
- No cambies GT911/I²C0 ni RGB/PSRAM/buffers solo para ocultar warnings conocidos; investiga en una fase específica.
- En firmware normal conserva desactivados los tests de rendimiento, `STORAGE_SELF_TEST` y `STORAGE_ALLOW_ONE_TIME_FORMAT`.
- Mantén `PN532_ENABLED=false` hasta una fase NFC autorizada y verificación de cableado/alimentación seguros.
- No incluyas ni imprimas contraseñas, tokens, API keys u otros secretos.

## Cierre de procesos y conexiones temporales

- Al crear un proceso, registra su PID, propósito y si debe cerrarse al terminar o permanecer activo por autorización del usuario.
- Registra también las conexiones, puertos, sesiones de automatización y perfiles temporales creados durante la tarea.
- Al finalizar, incluso si hubo errores, cierra las conexiones y recursos temporales propios mediante su mecanismo normal. Usa bloques `finally` o mecanismos equivalentes cuando corresponda.
- Espera la terminación y compruébala cuando sea posible. Si un proceso propio no termina, intenta un cierre dirigido a su PID, verificando antes su identidad y pertenencia a la tarea.
- No termines procesos por nombre de forma indiscriminada: `node.exe`, Chrome, PowerShell, `Code.exe` o `node_repl.exe` pueden pertenecer a otras sesiones.
- No cierres el runtime que sostiene la sesión actual de Codex. Si sospechas que un `node_repl.exe` bloquea la preparación del entorno, informa al usuario para que cierre la sesión y realice la limpieza externa.
- Conserva el servidor de revisión autorizado, los navegadores habituales y cualquier recurso preexistente que no hayas creado.
- Antes de eliminar perfiles o archivos temporales, cierra las conexiones y procesos que los utilizan.
- No declares un recurso cerrado si no lo comprobaste. Si la comprobación falla, indícalo.
- Si la preparación del entorno falla con “setup refresh had errors”, no repitas intentos continuamente ni ejecutes una limpieza masiva. Detente y reporta el error y los recursos conocidos.
- En el informe final resume qué recursos cerraste, cuáles conservaste y cualquier cierre que quedó sin confirmar.

Estas medidas reducen recursos huérfanos, pero no garantizan resolver los bloqueos internos del entorno de Codex.
