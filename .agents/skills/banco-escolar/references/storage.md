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

`SDManager` usa la ranura TF integrada de Waveshare. Reutiliza el `esp_expander::Base` inicializado por `Board::begin()` y SPI MOSI 11, SCK 12 y MISO 13. El controlador local `sd_diskio_external_cs.cpp` acciona EXIO4 activo en bajo por callback; no usa un GPIO ficticio. `begin()` monta `/sdcard` con `format_if_empty=false` y conserva el volumen montado hasta `unmount()`.

La API de archivos acepta rutas relativas ASCII de hasta 219 bytes bajo `/sdcard`; rechaza absolutas, `.`/`..`, componentes vacíos, separadores inversos, controles, dos puntos y sufijos `.tmp`/`.bak` reservados para transacciones. `fileExists`, `fileSize`, `readFile`, `listDirectory`, `createDirectory`, `writeFile` y `removeFile` devuelven `SdFileResult`; `sd_file_error_name()` traduce el error y `system_error` conserva `errno`. La lectura binaria limita el búfer a 256 KiB y al mayor bloque libre; el listado limita a 128 entradas y omite artefactos temporales.

La copia local conserva Arduino-ESP32 3.1.1 y usa `SdExternalChipSelect` también para escribir sectores. `writeFile` escribe un temporal hermano, ejecuta `fflush`/`fsync`/`fclose`, renombra el original a `.bak` y el temporal al destino. La siguiente operación de existencia/tamaño/lectura/escritura/eliminación sobre esa ruta restaura el respaldo si falta el destino, o lo elimina si el archivo nuevo ya existe. Todas las operaciones cierran los archivos. `tools/storage/check_sd_external_cs.py` verifica el callback, manejo de errores, API, validación de rutas, montaje sin formato y secuencia de reemplazo.

El código distingue tarjeta sin respuesta (`SD_NOT_PRESENT`) de tarjeta reconocida cuyo montaje FAT falla (`SD_ERROR`, `FILESYSTEM_MOUNT_FAILED`). `SD_READY` indica que el volumen sigue montado; `SD_UNMOUNTED` indica que `unmount()` terminó. La interfaz muestra estado, error, tipo, capacidad física y cantidad de entradas de la raíz. `totalBytes()/usedBytes()/freeBytes()` describen el volumen FAT cuando `f_getfree` da datos.

Requisito de formato documentado por el demo oficial de Waveshare: FAT32. No se valida físicamente la tarjeta ni se concluye que toda tarjeta de 64 GB necesite reformateo. Si falla el montaje, se informa el error y se deja intacta. La validación de BUILD y las pruebas sintéticas no sustituyen una prueba con tarjeta real.

### Servidor HTTP de prueba de lectura

`src/network/panel_http_server.*` inicia el servidor integrado `WebServer` en el puerto 80 sin depender de callbacks ni pantallas LVGL. `GET /` sirve `panel-test/index.html`; los recursos permitidos (`html`, `css`, `js`, JSON, texto e imágenes compatibles) se limitan a esa carpeta. `GET /status` responde `sd_mounted` y la versión leída de `panel-test/version.txt`. También se admite `HEAD` para archivos. Las demás rutas/métodos no tienen operaciones de escritura: los métodos distintos de GET/HEAD reciben 405.

El contenido se transmite mediante `SDManager::streamFile()` en fragmentos de 1 KiB y no se copia el recurso completo a RAM. Este camino solo valida/abre/lee y no invoca la recuperación FAT de temporales/respaldo que usan otras operaciones de `SDManager`; por tanto, el HTTP no modifica archivos como efecto lateral. La ruta sigue pasando por la validación de SD; faltante devuelve 404, ruta no válida 400 y tarjeta no montada 503. Los recursos de ejemplo están en `tools/web/sample_sd/panel-test/` para copiarse manualmente durante una futura prueba; esta tarea no escribió la microSD.

La implementación compila pero aún necesita validación física de conexión Wi-Fi, respuesta HTTP, lectura FAT de HTML/CSS/JS/imagen, cabeceras/estado, archivos ausentes y retirada de tarjeta. El servidor ofrece HTTP sin autenticación ni TLS y solo debe tratarse como prueba temporal en una red confiable. No es API del Panel, login, sincronización ni actualizador.

### Paquete estático del Panel para el servidor HTTP de prueba

La variante local se compila con `panel-maestro/npm run build:sd` y se prepara en `panel-maestro/package-sd/` con `tools/web/prepare_panel_sd_package.py`. El árbol `panel-test/maestro/` contiene el HTML y dependencias compiladas; `manifest.json` registra ruta, tamaño y SHA-256. La variante usa hash routing y no requiere fallback. Es un artefacto local pendiente de copia manual y validación física; no convierte al servidor en servicio del Panel ni añade escrituras HTTP.

## Almacenamiento offline futuro

LittleFS es persistencia y fuente primaria de operaciones. Las escrituras genéricas FAT están disponibles para el firmware local; alojar API/Panel y sincronizar por Wi-Fi siguen fuera de esta fase. Si pierde Wi-Fi, terminal conserva cola durable local y reintenta los mismos IDs al reconectar. Nube es opcional para backup/acceso remoto. Primera version: una terminal.

PM.10A propone `operation_id` idempotente independiente de `next_mv_id`, manteniendo IDs locales. API/coordinador guarda evento en transaccion y confirma despues del commit; terminal conserva pendiente si pierde respuesta y reenvia el mismo ID. Al agregar terminales, coordinador local serializa cambios de cuenta, valida fondos y no acepta snapshots de saldo como reemplazo. Servir el build real y actualizar el Panel desde SD sigue siendo futuro; el HTTP de archivos de prueba no da esas funciones. Ver [PANEL_MAESTRO_PM10A.md](../../../../docs/PANEL_MAESTRO_PM10A.md).
