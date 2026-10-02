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
| 5B | Historial real de movimientos | FUTURO |
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

La fase 0S.3 quedó cerrada con el commit estable en GitHub. Las fases 5A.4–5A.7 quedaron validadas: LittleFS conserva los movimientos 1–3 y su conteo tras reinicio, y la UI resuelve el nombre del alumno desde el `student_id` persistido. El saldo continúa siendo demo/RAM y no se reconstruye desde los movimientos; `synced=false` permanece porque no existe servidor ni sincronización operativa. La siguiente fase es 5B, historial real de movimientos. La fase 6 define sincronización; el Panel PWA 1.0 es 7, su integración con ESP32 es 7.1 y la operación offline completa es 7.2. Consulta [panel-master.md](panel-master.md) antes de esos trabajos.

BLE aún está pendiente; no habilitarlo sin caso de uso. No inferir asistencia de dictados no aplicados. Excel corresponde al Panel del Maestro/servidor, no al ESP32.

## Documentación de estado

La Fase 0S.2 corrigió estados históricos del firmware en `docs/FUTURE_ARCHITECTURE.md`. La Fase 0S.2B incorpora PWA multiplataforma y offline-first como decisión oficial, con API y base central futuras. No elige stack final ni implementa Panel, servidor o nueva persistencia.
