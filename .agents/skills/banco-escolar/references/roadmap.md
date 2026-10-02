# Roadmap y estado de fases

## Cerrado según historial del proyecto

- Fase 4F de investigación y optimización de frame timing: cerrada. Se quitaron los dos pulsos de opacidad; ver datos históricos en `performance.md`.
- Fase 5A.2E: prueba física de LittleFS cerrada; montó en dos arranques consecutivos sin volver a formatearse.
- Fase 5A.2F: permiso de formato temporal cerrado; formato de una sola vez y self-test están desactivados.
- LittleFS: validado físicamente en el equipo.
- Fase 0S.1: Skill creada y validada; BUILD SUCCESS.
- Fase 0S.2: documentación base actualizada; BUILD SUCCESS.

## Roadmap oficial

| Fase | Alcance | Estado |
|---|---|---|
| 0S.1 | Skill creada | COMPLETADA |
| 0S.2 | Documentación base | COMPLETADA |
| 0S.2B | Arquitectura PWA/offline-first | COMPLETADA |
| 0S.3 | Git estable y commit local | FASE ACTUAL |
| 5A.4 | Persistencia real de Áureos | FUTURO |
| 5A.5 | Validación de persistencia y reinicios | FUTURO |
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

La fase en curso es 0S.3: Git estable y commit local. No hacer push sin autorización separada. 5A.4 sigue siendo la siguiente implementación de firmware, después de 0S.3, y requiere solicitud aparte. La fase 6 define sincronización; el Panel PWA 1.0 es 7, su integración con ESP32 es 7.1 y la operación offline completa es 7.2. Consulta [panel-master.md](panel-master.md) antes de esos trabajos.

BLE aún está pendiente; no habilitarlo sin caso de uso. No inferir asistencia de dictados no aplicados. Excel corresponde al Panel del Maestro/servidor, no al ESP32.

## Documentación de estado

La Fase 0S.2 corrigió estados históricos del firmware en `docs/FUTURE_ARCHITECTURE.md`. La Fase 0S.2B incorpora PWA multiplataforma y offline-first como decisión oficial, con API y base central futuras. No elige stack final ni implementa Panel, servidor o nueva persistencia.
