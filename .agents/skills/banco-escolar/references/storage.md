# Almacenamiento

## LittleFS

LittleFS quedó validado físicamente en el historial del proyecto, con dos montajes consecutivos sin reformateo. Tamaño informado: total 3,538,944 bytes, usado inicial 16,384 bytes, libre 3,522,560 bytes (0.46 %). En el CSV de 16 MB la partición aparece como label `spiffs`, offset `0xC90000`, size `0x360000`; el mount path del gestor es `/littlefs`.

Estado normal de `src/storage/storage_manager.h`:

```cpp
STORAGE_ALLOW_ONE_TIME_FORMAT = false
STORAGE_SELF_TEST = false
```

El montaje normal usa `LittleFS.begin(false, ..., 10, "spiffs")`. La rama que puede llamar `LittleFS.format()` existe como infraestructura, pero queda cerrada por el flag global; un fallo normal reporta error y no debe reparar ni borrar automáticamente. No actives el permiso, no ejecutes formato ni modifiques datos/NVS sin una fase autorizada.

## NVS y movimientos

Namespace: `bank_storage`. Keys usadas incluyen `fs_initialized`, `fs_fmt_try`, `next_mv_id`, `st_test_done` y `storage_test_id`; NVS limita nombres a 15 caracteres. No amplíes nombres fuera del límite.

Movimientos NDJSON: `/data/movements.ndjson`; esquema actual 1, motivo hasta 95 bytes. El ID siguiente se reserva en NVS antes de escribir; gaps se aceptan e IDs nunca se reutilizan. Si fecha/hora no es válida (epoch mínimo 2025), se rechaza con `STORAGE_TIME_UNSYNCED`.

`MovementRecord`, `appendMovement()`, `readMovements()`, los conteos y lectura de pendientes existen. `StoredAttendanceRecord` también está definido, pero no existe API que escriba/lea asistencia ni historial real de asistencia. La ruta de `Registrar salida` guarda un solo movimiento `EXIT` para el `student_id` seleccionado, con cantidad negativa, motivo `Salida manual`, origen `TERMINAL` y `synced=false`. Comprueba que `TimeManager` esté sincronizado antes de guardar; si la hora o Storage no están disponibles, conserva el saldo sin cambios y deja reintentar. Después de `appendMovement()` exitoso, el resultado relee el registro por ID y resuelve su `student_id` mediante la búsqueda de alumnos provisionales; el nombre no se guarda en NDJSON. Si la relectura falla, la UI usa el ID de la operación solo para presentar el nombre y reporta que no pudo verificar la relectura. Solo después del intento de relectura actualiza el saldo demo en RAM y muestra ID/conteo leídos desde StorageManager. El saldo demo no se reconstruye ni persiste al reiniciar. La transferencia entre dos alumnos sigue siendo futura hasta definir atomicidad para débito y crédito.

La validación física de las fases 5A.4–5A.7 confirmó tres salidas persistidas en secuencia: ID 1, 2 y 3, todas con `student_id=7` y cantidad `-5`; el conteo avanzó de 1 a 2 y de 2 a 3, y se conservó tras reinicio. La relectura de ID 3 recuperó `student_id=7` y la UI lo resolvió como Darío. `schema_version` permanece en 1 y `synced=false`; no se guarda el nombre en `MovementRecord`. El saldo que muestra la UI sigue siendo demo en RAM y no se reconstruye desde el historial persistido.

`clearLocalData()` borra y recrea solo el archivo de movimientos conocido; conserva el namespace NVS y `next_mv_id`. Conserva las API `totalBytes()`, `usedBytes()`, `freeBytes()`, `getMovementCount()` y `clearLocalData()` al hacer cambios relacionados.

## microSD

`SDManager` existe, pero hardware no instalado y sin montaje físico. Mantén sin montar, escribir o formatear hasta autorización/fase específica. Una capacidad de compra práctica (32 GB, Class 10, UHS-I, FAT32) no establece el máximo oficial admitido.

## Almacenamiento offline futuro

LittleFS es persistencia local del ESP32; cuando exista servidor, la base de datos central será la fuente de verdad global. El diseño exige guardar operaciones válidas localmente primero, mantenerlas pendientes y marcarlas sincronizadas solo después de confirmación del servidor. Sin confirmación, siguen pendientes. Las APIs de pendientes existentes preparan este flujo; no lo implementan todavía.

El contador `next_mv_id` produce IDs estables dentro de la terminal, pero no garantiza unicidad entre terminales y PWA. La fase de sincronización debe definir identidad global e idempotencia sin reutilizar IDs locales. Tampoco debe saltarse el requisito actual de hora válida: el arranque offline sin hora válida requiere una política futura, no un cambio implícito del gestor.

Para la PWA se prevé IndexedDB como almacenamiento estructurado de caché y operaciones pendientes; consulta [panel-master.md](panel-master.md). Respaldos futuros: datos locales en LittleFS, base central con backups periódicos y microSD como respaldo opcional. Ningún respaldo nuevo ni IndexedDB está implementado en esta fase.
