# Roadmap y estado de fases

## Cerrado según historial del proyecto

- Fase 4F de investigación y optimización de frame timing: cerrada. Se quitaron los dos pulsos de opacidad; ver datos históricos en `performance.md`.
- Fase 5A.2E: prueba física de LittleFS cerrada; montó en dos arranques consecutivos sin volver a formatearse.
- Fase 5A.2F: permiso de formato temporal cerrado; formato de una sola vez y self-test están desactivados.
- Fases 5A.4–5A.7: persistencia física de movimientos, continuidad de IDs y resolución del alumno desde `student_id` validadas; IDs 1, 2 y 3 sobrevivieron según las pruebas registradas.
- LittleFS: validado físicamente en el equipo.
- Fase 0S.1: Skill creada y validada; BUILD SUCCESS.
- Fase 0S.2: documentación base actualizada; BUILD SUCCESS.

## Roadmap oficial

| Fase | Alcance | Estado |
|---|---|---|
| 0S.1 | Skill creada | COMPLETADA |
| 0S.2 | Documentación base | COMPLETADA |
| 0S.2B | Arquitectura PWA/offline-first | COMPLETADA |
| 0S.3 | Git estable y commit local | COMPLETADA |
| 5A.4 | Persistencia mínima de salida individual de Áureos | COMPLETADA |
| 5A.5 | Validación de persistencia y reinicios | COMPLETADA |
| 5A.6 | Continuidad secuencial de IDs | COMPLETADA |
| 5A.7 | Vínculo de movimiento con alumno mediante `student_id` | COMPLETADA |
| 5B.1 | Vista paginada de historial real de movimientos desde LittleFS | COMPLETADA; validada físicamente |
| 5B.1B | Pulido visual inicial del historial | PARCIAL; integrado al ajuste 5B.1C |
| 5B.1C | Rediseño visual coordinado de Inicio, Mi cuenta e Historial | COMPLETADA; validada físicamente |
| 5B.1D | Ajuste final del encabezado superior | COMPLETADA; validada físicamente |
| 5B.1E | Integrar logo oficial estático al splash | COMPLETADA; validada físicamente |
| 5B.1F | Integrar logo compacto en el encabezado común | COMPLETADA; validada físicamente |
| 5B.2 | Filtros y resumen de historial local de movimientos | COMPLETADA; validada físicamente |
| 5B.3 | Rediseño visual global y tema tecnológico | COMPLETADA; validada físicamente en 800 × 480 |
| 5B.3A.1 | Corregir reinicio al abrir Fluidez y Dictado | COMPLETADA; validada físicamente sin reinicios |
| 5C | Saldo persistente por alumno y operación coordinada de salida | COMPLETADA; validada físicamente en Fase 5C.1 |
| 5D | Entradas manuales de Áureos con saldo y movimiento transaccionales | COMPLETADA; validada físicamente tras reinicio |
| 6 | Sincronización | FUTURO |
| 7 | Panel Maestro PWA 1.0 | FUTURO |
| 7.1 | Sincronización Panel ↔ ESP32 | FUTURO |
| 7.2 | Operación offline completa | FUTURO |
| 8 | Gestión de dispositivos | FUTURO |
| 9 | NFC | FUTURO |
| 10 | Asistencia | FUTURO |
| 11 | Integración académica | FUTURO |
| 12 | Gráficas y análisis | FUTURO |
| 13 | Logros y recompensas | FUTURO |
| 14 | microSD | FUTURO |
| 15 | Respaldos | FUTURO |
| 16 | Exportaciones | FUTURO |
| 17 | Administración avanzada | FUTURO |
| 18 | BLE solo si existe caso de uso | FUTURO |
| 19 | OTA | FUTURO |

La fase 0S.3 quedó cerrada con el commit estable en GitHub. Las fases 5A.4–5A.7 quedaron validadas: LittleFS conserva los movimientos 1–3 y su conteo tras reinicio, y la UI resuelve el nombre del alumno desde el `student_id` persistido. La vista 5B.1 del historial real fue validada físicamente; es de solo lectura, no altera el esquema y no reconstruye saldo. El pulido 5B.1B se integró en 5B.1C. Las fases visuales 5B.1C–5B.1F fueron validadas físicamente; el bloque visual 5B.1 queda cerrado con Inicio, Mi cuenta e Historial coordinados, encabezado común, logos del splash y del encabezado, fecha/hora y Salir. `synced=false` permanece porque no existe servidor ni sincronización operativa. La fase 6 define sincronización; el Panel PWA 1.0 es 7, su integración con ESP32 es 7.1 y la operación offline completa es 7.2. Consulta [panel-master.md](panel-master.md) antes de esos trabajos.

La Fase 5B.2 implementa filtros temporales y conteos de entradas, salidas y pendientes sobre los movimientos reales cargados para el alumno. Es de solo lectura, no reconstruye saldo ni modifica el modelo o el almacenamiento; fue validada físicamente.

La Fase 5B.3 integra una identidad visual común inspirada en el logo para las 12 pantallas principales, con encabezado, paleta, tarjetas y variantes de botones reutilizables. Se validó físicamente en 800 × 480; navegación y callbacks continúan funcionando.

La Fase 5B.3A.1 investigó el `StoreProhibited` (`EXCVADDR=0`) al abrir Fluidez y Dictado. El backtrace ubicó `lv_memset_00()` durante la creación de una capa del renderizador software de LVGL. Se protegió la inicialización ante fallo de `lv_mem_alloc()` y se evitó la escala táctil en controles grandes mediante feedback de borde. La prueba física posterior fue satisfactoria, sin reinicios.

BLE aún está pendiente; no habilitarlo sin caso de uso. No inferir asistencia de dictados no aplicados. Excel corresponde al Panel del Maestro/servidor, no al ESP32.

La Fase 5C guarda snapshots de `StudentAccount` en `/data/accounts.ndjson`, mantiene separado el historial, inicializa de forma idempotente el saldo vigente de 125 Áureos para `student_id=7`, y coordina cada salida con un journal de recuperación. En 5C.1 se reportó validación física: salida de 5, movimiento ID 4, saldo 120, cuatro movimientos y persistencia tras reinicio sin duplicación ni reinicialización. 5C queda cerrada; no se inició la siguiente fase.

La Fase 5D añade el flujo manual de entrada desde Inicio y reutiliza `ENTRY`, `StudentAccount` y el journal de 5C. Prueba física reportada: saldo 120 + 10 = 130, movimiento ID 5, cinco movimientos tras reinicio y sin duplicación.

## Documentación de estado

La Fase 0S.2 corrigió estados históricos del firmware en `docs/FUTURE_ARCHITECTURE.md`. La Fase 0S.2B incorpora PWA multiplataforma y offline-first como decisión oficial, con API y base central futuras. No elige stack final ni implementa Panel, servidor o nueva persistencia.

## Fase 5E y siguientes subfases

- 5E — Menú Maestro base: IMPLEMENTADA y validada físicamente según reporte de fase.
- 5E.1 — Configuración funcional de ActivityDraft en memoria: IMPLEMENTADA; validación física pendiente.
- 5E.1A — Suspensión del timeout del contexto Maestro: IMPLEMENTADA en código y BUILD validado previamente; falta prueba física >60 s.
- 5E.1B — Pulido visual y glifos del Menú Maestro: IMPLEMENTADA y BUILD validado; validación física en claro/oscuro pendiente.
- 5E.2 — Persistencia de ActivitySession: FUTURO.
- 5E.3 — Gestión de actividades activas simultáneas: FUTURO.
- 5E.4 — Registro de ActivityClaim: FUTURO.
- 5E.5 — Integración futura con NFC: FUTURO.
