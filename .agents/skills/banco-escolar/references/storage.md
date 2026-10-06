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

`MovementRecord`, `appendMovement()`, `readMovements()`, los conteos y lectura de pendientes existen. `StoredAttendanceRecord` también está definido, pero no existe API que escriba/lea asistencia ni historial real de asistencia. El movimiento persistido conserva `student_id` como identidad y la UI resuelve el nombre visible desde los datos provisionales; el nombre no se guarda en NDJSON. La ruta actual de `Registrar salida` utiliza `applyAccountMovement()` para coordinar una salida `EXIT` negativa, motivo `Salida manual`, origen `TERMINAL`, `synced=false` y el saldo persistente. Requiere hora válida y fondos suficientes; si falla la persistencia no presenta éxito ni actualiza el saldo mostrado. La transferencia entre dos alumnos sigue siendo futura hasta definir atomicidad para débito y crédito.

La validación física de las fases 5A.4–5A.7 confirmó tres salidas persistidas en secuencia: ID 1, 2 y 3, todas con `student_id=7` y cantidad `-5`; el conteo avanzó de 1 a 2 y de 2 a 3, y se conservó tras reinicio. La relectura de ID 3 recuperó `student_id=7` y la UI lo resolvió como Darío. `schema_version` permanece en 1 y `synced=false`; no se guarda el nombre en `MovementRecord`. Esos movimientos son anteriores al saldo persistente y no se aplican retroactivamente.

## Cuentas persistentes (Fase 5C)

`StudentAccount` usa `student_id`, `int64_t balance` y `schema_version=1`. Sus snapshots append-only se guardan en `/data/accounts.ndjson`, separados del historial de movimientos. El snapshot completo más reciente por ID define el saldo; no se suma el historial. La escritura agrega una línea completa, comprueba tamaño y añade un separador ante una cola incompleta. No reescribe el archivo completo. Los errores de parseo/versionado bloquean el acceso a la cuenta y no restablecen el saldo.

La cuenta de demostración para `student_id=7` se crea explícitamente al entrar a Demo con el valor inicial vigente de 125 Áureos, solo si el ID aún no tiene una cuenta. Una cuenta existente nunca se reemplaza por el valor inicial. No se crean saldos para otros alumnos. Los tres movimientos históricos IDs 1–3 no se aplican retroactivamente. En el primer arranque de la validación física, `/data/accounts.ndjson` aún no existía; su primera creación fue correcta. La escritura ahora consulta `LittleFS.exists()` antes de abrir el archivo en lectura, evitando el mensaje de VFS esperado.

La salida pasa por `applyAccountMovement()`: valida cuenta, hora, tipo, rango y fondos; reserva `next_mv_id` en NVS; guarda `/data/pending_transaction.json`; agrega el movimiento `EXIT` con valor negativo; agrega el snapshot nuevo; y elimina el journal al cerrar. Al arrancar, un journal válido se compara con el movimiento y saldo actuales. Si falta el movimiento y el saldo sigue igual al anterior, se agrega usando el ID reservado; luego se guarda el saldo nuevo. Si el movimiento ya existe, se verifica que coincida exactamente. Estados ambiguos o archivos corruptos bloquean Storage y conservan el journal para no inventar ni duplicar datos. Los huecos de IDs siguen siendo válidos.

La validación física reportada para 5C.1 confirmó `student_id=7`, saldo inicial 125, una salida nueva de 5, saldo final 120 y movimiento ID 4 (conteo de 3 a 4). Tras reiniciar, saldo 120 y conteo 4; el movimiento no se duplicó ni la cuenta volvió a 125. Los IDs 1–3 permanecen intactos. Inicio, Mi cuenta y Registrar salida usan la cuenta persistida. Si la lectura no es válida, muestran que el saldo no está disponible. La prueba informó que la ausencia inicial de `accounts.ndjson` fue normal, no un fallo funcional. 5C queda validada físicamente; BUILD no sustituye esa evidencia. No se ejecuta Upload en el cierre documental.

La Fase 5D reutiliza `applyAccountMovement()` y `/data/pending_transaction.json` para entradas manuales positivas. Usa tipo `ENTRY`, motivo `Entrada manual`, origen `TERMINAL` y `synced=false`. La recuperación valida `new_balance == old_balance + amount` para impedir aplicar dos veces una entrada tras reinicio. El límite `MAX_SINGLE_CREDIT` es 10,000 Áureos. Prueba física reportada: 120 + 10 = 130, movimiento ID 5, saldo y cinco movimientos conservados tras reinicio sin duplicación; 5D queda validada físicamente.

`clearLocalData()` borra y recrea solo el archivo de movimientos conocido; conserva el namespace NVS y `next_mv_id`. Conserva las API `totalBytes()`, `usedBytes()`, `freeBytes()`, `getMovementCount()` y `clearLocalData()` al hacer cambios relacionados.

## microSD

`SDManager` es un placeholder. El usuario reporta una microSD de 64 GB instalada, pero el firmware no intenta detectar ni montar la tarjeta; se decidió omitir microSD por ahora y no incluirla en las siguientes fases inmediatas.

## Almacenamiento offline futuro

LittleFS es persistencia local del ESP32; cuando exista servidor, la base de datos central será la fuente de verdad global. El diseño exige guardar operaciones válidas localmente primero, mantenerlas pendientes y marcarlas sincronizadas solo después de confirmación del servidor. Sin confirmación, siguen pendientes. Las APIs de pendientes existentes preparan este flujo; no lo implementan todavía.

El contador `next_mv_id` produce IDs estables dentro de la terminal, pero no garantiza unicidad entre terminales y PWA. La fase de sincronización debe definir identidad global e idempotencia sin reutilizar IDs locales. Tampoco debe saltarse el requisito actual de hora válida: el arranque offline sin hora válida requiere una política futura, no un cambio implícito del gestor.

Para la PWA se prevé IndexedDB como almacenamiento estructurado de caché y operaciones pendientes; consulta [panel-master.md](panel-master.md). Respaldos futuros: datos locales en LittleFS, base central con backups periódicos y microSD como respaldo opcional. Ningún respaldo nuevo ni IndexedDB está implementado en esta fase.
