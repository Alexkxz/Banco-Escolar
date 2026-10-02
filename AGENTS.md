# Banco Escolar: reglas del repositorio

- Para tareas de Banco Escolar, incluida la arquitectura del Panel Maestro y la sincronización, consulta la Skill `.agents/skills/banco-escolar/SKILL.md` y las referencias pertinentes.
- Ejecuta BUILD y reporta el resultado antes de cualquier carga. No ejecutes Upload ni Upload and Monitor sin autorización explícita.
- No inventes pines, estado físico del hardware, UIDs NFC, datos académicos ni resultados de prueba.
- No cambies GT911/I²C0 ni RGB/PSRAM/buffers solo para ocultar warnings conocidos; investiga en una fase específica.
- En firmware normal conserva desactivados los tests de rendimiento, `STORAGE_SELF_TEST` y `STORAGE_ALLOW_ONE_TIME_FORMAT`.
- Mantén `PN532_ENABLED=false` hasta una fase NFC autorizada y verificación de cableado/alimentación seguros.
- No incluyas ni imprimas contraseñas, tokens, API keys u otros secretos.
