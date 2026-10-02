# Flujo de trabajo para cambios

1. Lee alcance, restricciones y la referencia afectada. Confirma estado real en el código antes de decidir.
2. Registra flags y estado inicial relevante. No modifiques datos de LittleFS/NVS, credenciales o configuración física como efecto colateral.
3. Implementa solo la fase pedida y revisa el diff completo. Evita cambios extra de estilo, drivers y dependencias.
4. Ejecuta BUILD con el entorno PlatformIO del proyecto. Si falla, diagnostica y corrige dentro del alcance.
5. Reporta archivos, decisiones/flags, resultado BUILD, RAM, Flash, warnings y validaciones pendientes. Separa claramente BUILD de pruebas físicas.
6. No ejecutes Upload ni Upload and Monitor salvo autorización explícita del usuario. Si la instrucción dice “esperar revisión”, termina tras el reporte.
7. Después de cerrar una fase que cambie arquitectura, hardware, almacenamiento, red, UI o roadmap, evalúa actualizar referencias de esta Skill. No la actualices por cambios triviales de texto.

Cuando el usuario entregue una fase estructurada, respeta sus secciones, lista de NO TOCAR, modo de validación y límites de BUILD/Upload. No implementes fases siguientes.

Para cambios de arquitectura del Panel Maestro o sincronización, consulta [panel-master.md](panel-master.md) y el roadmap antes de editar. Conserva PWA multiplataforma y offline-first como decisiones; IndexedDB, Service Worker, API, base central y sincronización son futuros. Una fase documental no autoriza crear backend, esquema de base de datos, manifest, Service Worker ni código de Panel, ni seleccionar el stack final.

Al documentar offline, distingue operación ya implementada de requisitos futuros y verifica límites de tiempo, IDs locales y confirmación durable. El BUILD del ESP32 no valida una PWA ni sincronización futura.

No inventes pines, UIDs, datos académicos, saldos, logros, tamaños soportados por hardware, estados físicos o resultados de pruebas. Si falta evidencia, declara PENDIENTE.

En reportes de pantalla/UI, recuerda el riesgo de glifos españoles: la fuente está embebida y tiene cobertura concreta; verifica el carácter en el font antes de elegir un sustituto. Nunca copies secretos en respuesta, logs o documentación.
