# Banco Escolar — Handoff

## Estado actual comprobado (2026-10-08)

- Firmware implementa 5E.2–5E.4, DIAG.1 y diagnóstico de solo lectura de microSD. El BUILD no sustituye validaciones físicas; las capturas por comando y botón, el arranque y el sondeo SD quedaron verificados en las entradas fechadas correspondientes.
- Panel Maestro PM.9B conserva datos e imágenes ficticios en IndexedDB. PM.9C agrega respaldo/restauración JSON versionada, validación previa completa y reemplazo transaccional. El 08/10/2026 pasaron Vitest (13 archivos/83 pruebas), Chromium Playwright con IndexedDB nativo (incluye abort transaccional, cancelación, doble confirmación, invalidación por revisión, integridad de fotos y cierre/reapertura de un perfil aislado), build y `git diff --check`. La inspección visual de Configuración en escritorio y 390/320 px, ambos temas y foco por teclado quedó aprobada. Las capturas permanecen locales porque muestran valores de configuración escolar. Pendientes PM.9C: cuota/límite máximo, cierre abrupto/energía y confirmar que el usuario conserve el archivo descargado. Detalle: [PANEL_MAESTRO_PM9C.md](PANEL_MAESTRO_PM9C.md).
- PM.9A documenta el diseño; PM.9B y PM.9C están implementadas en la demo local. El espacio real no se abre ni se crea. Los avisos de chunk y pendientes del navegador se detallan en PM.9C.
- DIAG.1 está implementada y probada físicamente por comando y por el botón; el BMP recibido cumplió dimensiones, formato y checksum BED1. Ver [TERMINAL_DIAG1_CAPTURAS.md](TERMINAL_DIAG1_CAPTURAS.md). Los archivos de imagen físicos no se incluyen en este documento.

Los apartados posteriores que describen fases parciales son antecedentes históricos; el estado vigente es esta sección y los cierres fechados más recientes.
## Objetivo del proyecto

Terminal educativa local para primaria sobre Waveshare ESP32-S3-Touch-LCD-7. El firmware usa LVGL, LittleFS y NVS; los datos escolares del cliente son de demostración. El Panel Maestro local es una demo sin backend ni sincronización con firmware. Este archivo resume el estado técnico para retomar el desarrollo.

## Hardware

- Waveshare ESP32-S3-Touch-LCD-7 Rev 1.2; LCD RGB ST7262 800×480, touch GT911, expansor CH422G.
- Flash física reportada: 16 MB; PSRAM física reportada: 8 MB. No cambies RGB, PSRAM, GT911/I²C0 o buffers por warnings sin una fase de diagnóstico.
- La ranura microSD integrada se sondea en solo lectura mediante SPI GPIO11/MOSI, GPIO12/SCK, GPIO13/MISO y EXIO4 del CH422G. El montaje tiene formato automático desactivado; el arranque físico confirmó detección, lectura de tipo/capacidad, listado de raíz y desmontaje. No se escriben ni reparan archivos.
- PN532 está desconectado y `PN532_ENABLED=false`. El uso futuro por I²C1 GPIO43/44 requiere una fase explícita y confirmar cableado, alimentación y niveles.

## Software

- PlatformIO, C++17, Arduino-ESP32 3.1.1, LVGL 8.4.0 y ESP32 Display Panel.
- `src/main.cpp`: UI, callbacks, temporizadores y flujo principal.
- `src/storage/storage_manager.*`: LittleFS, movimientos, cuentas y recuperación transaccional.
- `src/storage/sd_manager.*`: diagnóstico de solo lectura de microSD; ver el cierre fechado más reciente abajo.
- `src/data/provisional_data.*`: datos provisionales de alumnos y registros académicos.
- `lib/lvgl/src/draw/sw/lv_draw_sw_layer.c`: parche local para comprobar `lv_mem_alloc()` antes de inicializar la capa; revisarlo si se actualiza LVGL.

## Estado estable de Git previo

- Rama `main`.
- Historial de Git previo a la publicación: consultar `git log`; los SHA citados en este handoff son antecedentes, no estado actual de la rama.

## Funcionalidades completadas

- LittleFS monta normalmente con formato automático cerrado. `STORAGE_ALLOW_ONE_TIME_FORMAT=false` y `STORAGE_SELF_TEST=false`.
- `StudentAccount` y movimientos ENTRY/EXIT se escriben de forma coordinada mediante `/data/pending_transaction.json`; la recuperación compara el journal, el movimiento y el saldo para evitar duplicados.
- El historial lee movimientos de LittleFS y resuelve el nombre por `student_id`; no reconstruye el saldo desde el historial.
- Registrar salida usa saldo persistente. Registrar entrada manual añade una cantidad positiva con máximo individual de 10,000 Áureos.
- El Menú Maestro DEV está implementado. ActivityDraft permanece en RAM hasta iniciar; 5E.2 ahora persiste ActivitySession en LittleFS tras validar hora.
- Timeout de sesión de alumno: `SESSION_TIMEOUT_SECONDS=60`. El código ya suspende el timer en el contexto Maestro; ver “Pendientes conocidos”.

## Saldo y movimientos actuales

Validación física histórica: se revisaron movimientos de entrada/salida y recuperación tras reinicio. Se omiten nombres, identificadores y valores de cuentas usados en aquella sesión; esos datos no deben copiarse al firmware ni a documentación pública.

## Storage

- `/data/movements.ndjson`: movimientos NDJSON con `student_id` como identidad, `schema_version=1`, `synced=false` hasta una sincronización futura.
- `/data/accounts.ndjson`: snapshots append-only de `StudentAccount` por alumno.
- `/data/pending_transaction.json`: journal de recuperación para movimiento más saldo.
- NVS namespace `bank_storage`; `next_mv_id` se reserva antes de escribir y los IDs no se reutilizan. `clearLocalData()` conserva NVS y el contador.
- Prueba controlada de corte de energía aún pendiente. No ejecutar durante este cierre.
- La microSD se sondea por separado y no forma parte del almacenamiento persistente de LittleFS. La prueba documentada monta solo para leer y desmonta al terminar.

## UI

- Tema tecnológico azul marino/azul/cian/dorado; temas claro y oscuro.
- `logo_y_nombre` se usa en splash; `logo_banco_escolar` en encabezados internos.
- Navegación instantánea y feedback local. El feedback de tarjetas grandes usa borde, sin scale para evitar capas LVGL grandes.
- Splash y mejoras del Menú Maestro están implementados; los últimos ajustes 5E.1B requieren prueba física en ambos temas.

## Menú Maestro

El acceso temporal MENÚ MAESTRO DEV está en Configuración; no usa PN532. ActivityDraft vive en RAM mientras se configura. Al iniciar, una sesión persistente se crea solo después de validar y guardar correctamente.

## ActivityDraft

`ActivityDraft` es temporal y no persistente. Contiene número opcional, tiempo, recompensa, modo de participantes, `selected_student_ids` y validez. Reglas actuales:

- Número opcional; si se habilita, 1–9999.
- Tiempo: sin tiempo o con tiempo; minutos 0–999, segundos 0–59; el tiempo configurado debe ser mayor que cero.
- Recompensa por alumno: 1–10,000 Áureos.
- Participantes: `ALL`, `SELECTED` o `PARTICIPANTS_DISABLED`. `ALL` incluye alumnos registrados; `SELECTED` exige al menos un `student_id` válido; `PARTICIPANTS_DISABLED` omite el filtro previo de elegibilidad y no significa cero alumnos.
- `Iniciar actividad` exige hora válida de TimeManager; confirma solo después de persistir y conserva el borrador ante error.

## Arquitectura futura de actividades

La aplicación deberá permitir múltiples actividades activas simultáneamente; no diseñar alrededor de una variable única `current_activity`. El flujo futuro será: Maestro configura e inicia → alumno completa → tarjeta identifica al alumno → terminal busca actividades activas elegibles → registro directo si hay una, o elección si hay varias → `ActivityClaim` → movimiento ENTRY → saldo actualizado mediante el motor monetario existente.

Este texto era una propuesta histórica. `ActivitySession` y `ActivityClaim` están implementadas; la UI local de demostración consulta reclamos y coordina sus recompensas con saldo/movimiento. NFC y sincronización siguen pendientes.

## ActivitySession persistente (5E.2)

ActivitySession contiene ID, esquema, número opcional, recompensa, duración, modo y snapshot de participantes, inicio y estado. ActivityClaim está implementada y se persiste junto con las operaciones locales de recompensa; 5E.1–5E.4 aún requieren validación física.

## ActivityClaim propuesta (antecedente histórico)

La estructura vigente está en `src/activity_claim.h`; verifica su esquema y persistencia antes de usar esta propuesta histórica como contrato.

## Anulaciones

Una reclamación incorrecta no se elimina físicamente. En el futuro, el Panel la marcará `VOIDED` y creará un movimiento inverso que retire los Áureos incorrectos, conservando el historial.

## Panel Maestro

El Panel Maestro disponible es una PWA de demostración local, offline-first, con datos de ejemplo y sin conexión al firmware. El futuro servicio real aún requiere API, base central y sincronización; no asumir que la demo sincroniza datos escolares.

Nota: las funciones PM implementadas en `panel-maestro/` son de demostración local. Los ejemplos de alumnado, asistencia, progreso y cuentas son ficticios y no representan registros reales.

## NFC

`PN532_ENABLED=false`; lector desconectado. En el futuro las tarjetas solo aportarán UID para buscar `student_id`; nunca guardarán saldo, historial, actividad ni datos académicos. No habilitarlo ni inventar UIDs sin fase y comprobación física autorizadas.

## Pendientes conocidos

- **5E.1A:** la lógica de suspensión del timeout ya está implementada en código y compiló. `is_master_context_active()` cubre Menú Maestro, Configurar actividad, Seleccionar alumnos, keypad y Configuración preparada; el timer normal se conserva fuera del contexto y el contador se reinicia al salir desde el menú principal. La validación física de más de 60 segundos en cada pantalla sigue pendiente. La instrucción de este cierre que etiqueta 5E.1A como no implementada contradice el código y el trabajo anterior; no rehacerla ni quitarla.
- **5E.1B:** pulido visual y glifos compilados; pendiente de validación física final en LIGHT/DARK. “Más logros próximamente” queda pendiente tipográfico fuera de la pantalla Logros que 5E.1B prohibió modificar.
- **Wi-Fi:** se observaron reconexiones repetidas aunque la aplicación continuaba; diagnóstico separado pendiente, no bloqueante.
- **Energía:** falta prueba controlada de corte durante una transacción.
- **microSD:** la ranura integrada se detectó físicamente con CS externo EXIO4 y el sondeo de solo lectura terminó correctamente. No se ha validado la pantalla local de almacenamiento tras los últimos cambios de capacidad; no escribir, reparar ni formatear la tarjeta.
- Los datos académicos, Logros y varias vistas siguen siendo provisionales/demo; mantener `N/A` distinto de cero y respetar los umbrales actuales.

## Próximas fases

Orden recomendado, distinguiendo implementación de validación:

1. Validar físicamente 5E.1A (ya implementada en código): dejar cada pantalla del contexto Maestro abierta por más de 60 s y confirmar que no vuelve a Inicio; comprobar que las pantallas de alumno mantienen su timeout normal.
2. Validar físicamente 5E.1B en 800×480, tema claro y oscuro: acentos, `Áureos`, `número`, `selección`, `3.º`/`4.º B`, resumen en cuatro bloques, selección de alumnos y ausencia de cortes.
3. Validar físicamente la Fase 5E.2 después de revisar el BUILD y autorizar carga.
4. 5E.3: validar físicamente múltiples actividades activas simultáneamente.
5. 5E.4: validar físicamente ActivityClaim, deduplicación y motor monetario.
6. 5E.5: integración NFC de alumno y tarjeta maestra, únicamente con autorización y cableado seguro.
7. Después: backend/sincronización, asistencia y logros; el sondeo microSD queda limitado a lectura.

## Reglas críticas de desarrollo

- Mantener el sondeo microSD en solo lectura; no escribir, reparar ni formatear.
- No activar PN532 sin hardware conectado y validación de alimentación/cableado.
- No cambiar GT911/I²C0, RGB, PSRAM o buffers sin evidencia y fase de diagnóstico.
- No cambiar casualmente el parche LVGL ni usar scale en tarjetas grandes.
- No reconstruir saldo desde el historial ni borrar movimientos monetarios.
- Las anulaciones futuras usan movimiento inverso.
- Varias actividades deberán coexistir; no crear una `current_activity` única.
- `ActivityDraft` es temporal; ActivitySession persiste por separado. La participación/cobro único por alumno y actividad sigue pendiente.
- No imprimir contraseñas, tokens, claves NVS ni secretos.

## Flujo de trabajo con Codex

Leer `.agents/skills/banco-escolar/SKILL.md` y referencias pertinentes; verificar código y estado Git; cambiar solo lo autorizado; ejecutar BUILD y reportarlo antes de cualquier carga; revisar `git diff --check`; distinguir BUILD de prueba física. El flujo es: cambios → BUILD SUCCESS → reporte → revisión/autorización → Upload. No ejecutar Upload ni Upload and Monitor sin autorización explícita. Cierre 5E.1C autorizado: BUILD, un commit local coherente y push normal a `origin/main`; nunca force push.



## Fase 5E.2 — ActivitySession persistente

Las sesiones se guardan como snapshots append-only en `/data/activities.ndjson`; el último snapshot válido por ID define el estado actual. NVS `next_act_id` es independiente del contador monetario y se reserva antes de escribir. `ALL` conserva los IDs registrados al iniciar; `SELECTED` conserva sus IDs validados; `PARTICIPANTS_DISABLED` no aplica filtro y guarda una lista vacía. El guardado requiere hora válida de TimeManager. Si falla, no se muestra éxito y ActivityDraft permanece.

La lista de sesiones calcula el tiempo restante desde `started_at`; con hora no válida lo muestra pendiente de verificar. No se cierra automáticamente al vencer (decisión para 5E.3). Ante cola incompleta o registros corruptos, conserva las lecturas válidas anteriores, no borra ni repara y bloquea nuevos append. No hay ActivityClaim, movimientos ni cobros. Las pruebas físicas de 5E.2 siguen pendientes.

BUILD PlatformIO (06/10/2026): SUCCESS. RAM 100,836/327,680 bytes (30.8%); Flash 1,918,448/6,553,600 bytes (29.3%); no compiler warnings observed. git diff --check pasó. No hay compilador C++ host ni carpeta/framework de pruebas local, por lo que no se ejecutó una batería lógica automática; la compilación ESP32 validó los módulos. La validación física de persistencia/reinicio sigue pendiente. No Upload, Commit ni Push.

## Fase 5E.3 — Gestión de actividades (implementación)

La lista reutiliza la consulta del Menú Maestro y ofrece filtros por estado; presenta las sesiones más recientes primero. El detalle permite finalizar/cancelar con confirmación y exige fecha/hora válida antes de guardar. Editar crea un snapshot de la sesión ACTIVE existente conservando ID e inicio. Las sesiones vencidas permanecen activas; el estado de tiempo es calculado y no se persiste. Los registros corruptos o incompletos continúan bloqueando append según 5E.2. La elegibilidad temporal está en el dominio y no crea claims ni mueve dinero.

La edición presupone que aún no existe historial de ActivityClaim (confirmado: no hay modelo, archivo ni flujo de cobros en el código actual). La función de dominio recibe el indicador de claims previos y 5E.4 debe conectarlo a la consulta real, contando también los anulados.

BUILD PlatformIO (06/10/2026): SUCCESS. RAM 101,076/327,680 bytes (30.8%); Flash 1,929,336/6,553,600 bytes (29.4%). Pruebas lógicas y visuales/físicas pendientes. No Upload, Commit ni Push.
\r\n
\r\n

## Phase 5E.4 - ActivityClaim and coordinated rewards

Added persistent ActivityClaim records and missing-account incidents in `/data/activity_claims.ndjson`. A successful reward reserves the movement and claim IDs, writes a schema 2 pending journal, then persists the movement, account snapshot, and claim. Recovery completes the captured operation with its original IDs and timestamp; it does not re-evaluate current activity eligibility. Damaged or ambiguous data preserves the journal and blocks Storage.

The DEV UI supports reviewing eligible activity/student pairs, confirming a reward, and viewing claim history. Existing claims block ordinary repeat rewards. When a student has no account, the UI offers to record a persistent incident without creating an account. Activity edits are blocked after a claim exists.

Final PlatformIO BUILD (2026-10-06): SUCCESS. RAM 101,436/327,680 bytes (31.0%); Flash 1,949,052/6,553,600 bytes (29.7%); no compiler warnings observed. `test/` contains only the PlatformIO README; no automated suite or physical validation was available. No Upload, Commit or Push.

## Estado auditado para publicación (2026-10-07)

- El código y la documentación locales incluyen firmware 5E.2–5E.4 y el Panel Maestro PM.1–PM.8, además de los ajustes posteriores de tipografía y de Progreso académico. El Panel sigue siendo una demostración cliente: sus cambios escolares y ajustes quedan en memoria, desaparecen al recargar y no se sincronizan con el firmware.
- PM.8 y su ajuste visual están implementados. Las pruebas de la fase registraron 82 pruebas aprobadas, build web correcto y revisión headless de las cuatro subpestañas en temas claro/oscuro y 1440, 390 y 320 px. Estos resultados se refieren al cliente web y no prueban el hardware.
- Las pruebas físicas de 5E.1, 5E.2, 5E.3 y 5E.4 siguen pendientes. No hay una pantalla conectada disponible para esta validación física; no se debe presentar un BUILD como prueba de pantalla, almacenamiento o interacción real.
- La anotación anterior de que DIAG.1 no tenía implementación corresponde al estado auditado antes de esta fase. La implementación actual y sus límites están descritos en [TERMINAL_DIAG1_CAPTURAS.md](TERMINAL_DIAG1_CAPTURAS.md); las pruebas del receptor son sintéticas y no se hicieron en hardware.
- En Comprensión, la demo permite clasificar cada aspecto como Requiere apoyo, En desarrollo o Logrado con denominadores configurables. Los pesos y los límites del resultado ponderado general permanecen sin configurar por defecto; por eso todavía no hay rangos/niveles definidos para emitir una clasificación general ponderada. No inventar una clasificación general.
- Las siete imágenes nuevas de `Imagenes/Alumnos/` se revisaron: son ilustraciones pixel-art de avatares escolares ficticios, no fotografías originales de alumnos. Se incluyen como recursos destinados al proyecto. Las capturas versionadas de `panel-maestro/capturas/` corresponden a evidencia visual del panel; se excluyen artefactos generados, dependencias, salidas de build, secretos y archivos temporales.
- No hay pantalla conectada para pruebas físicas en el estado reportado para esta publicación. No se hizo ni se debe inferir Upload.

## Validaciones y auditoría de publicación (2026-10-07)

**Ejecutado manualmente por el usuario:** BUILD PlatformIO para `esp32-s3-devkitc-1`: SUCCESS; RAM 101,436 / 327,680 bytes (31.0%); Flash 1,949,356 / 6,553,600 bytes (29.7%); 116.89 s; sin warnings visibles en la salida compartida. En esta auditoría se compararon los cambios locales y la configuración: no se encontraron cambios de firmware/configuración posteriores a ese BUILD. No se repitió PlatformIO por la restricción de permisos local reportada; este resultado corresponde al BUILD manual del usuario, no a una ejecución del auditor.

**Ejecutado por el auditor:** pruebas web PM.8, 82/82 aprobadas en 13 archivos; build web TypeScript/Vite correcto; `git diff --check` correcto. La revisión visual headless de PM.8 registrada arriba también fue hecha por el auditor en ambos temas y tamaños desktop/móvil; no equivale a una prueba física del dispositivo.

El intento previo del auditor de iniciar PlatformIO se detuvo antes de compilar porque Windows denegó escritura en `.platformio\platforms.lock` y PlatformIO reportó `HomeDirPermissionsError` para `.platformio\.cache`. No se cambiaron esos permisos. La salida manual SUCCESS aportada por el usuario es la validación de BUILD vigente.

## DIAG.1 — captura por USB (2026-10-07)

- Implementación local en `src/diagnostics/`, integración de `flush_cb` en `src/esp_lv_adapter_arduino.*`, disparadores en `src/main.cpp` y receptor en `tools/diag1/`. Captura sincrónica en el contexto LVGL; reconstrucción por cobertura de píxel para regiones parciales; buffer de imagen de 768,000 bytes y mapa de cobertura de 48,000 bytes reservados bajo demanda en PSRAM. La transferencia serial se vacía por tramos no bloqueantes desde `loop()`, fuera del callback de renderizado.
- `python -m unittest discover -s tools/diag1 -p test_diag1_receiver.py -v`: 9/9 aprobadas, datos sintéticos. No son capturas físicas.
- **BUILD manual ejecutado por el usuario:** SUCCESS, 186.46 s; RAM 150,660/327,680 bytes (46.0 %); Flash 1,953,020/6,553,600 bytes (29.8 %); sin warnings visibles. Desde ese BUILD no cambiaron archivos de firmware ni configuración, así que no se repitió. El intento anterior del agente que no encontró el comando `pio` ocurrió antes de esta validación manual y queda supersedido por ella.
- `git diff --check`: aprobado (exit code 0); Git mostró avisos informativos de conversión LF→CRLF para archivos del workspace.
- Pendiente por falta de pantalla: comprobar ambos disparadores en el dispositivo (comando USB y botón del Menú Maestro), fidelidad/color/orientación, respuesta táctil, memoria dinámica, estabilidad y capturas repetidas. No hay pantalla conectada. No Upload, Commit ni Push.

## PM.9A — diseño de persistencia local (2026-10-07)

- Revisados `AGENTS.md`, skill/referencias del proyecto, `PanelDataService`, `DemoPanelDataService`, los modelos y documentos PM.1–PM.8. Solo `panel-theme` y `panel-sidebar-collapsed` persisten hoy en `localStorage`; entidades, reglas, cambios de Áureos y fotos del perfil viven en la instancia demo/React en memoria.
- `docs/PANEL_MAESTRO_PM9A.md` documenta stores, claves/índices, namespaces separados, contadores, transacciones atómicas, errores, migración, recuperación, Blobs, límites y criterios de aceptación para PM.9B y PM.9C.
- **Diseñada; no implementada.** No cambió código funcional, IndexedDB no se abre, no se activa proveedor real ni selector. Backend, sincronización y respaldo continúan pendientes. Decisiones abiertas: persistencia del seed y restauración, solicitud de cuota persistente, archivo/cifrado/conflictos de respaldo, retención de reglas y baja de alumno.
- PM.9B: implementar IndexedDB solo demo y pruebas de recarga/reapertura, identidad, saldos, duplicación, abortos, migración y recuperación de fotos. PM.9C: respaldo/importación con datos e imágenes, validación previa, checksums y conflictos con confirmación.
- Sin pruebas ni BUILD: solo cambios documentales. No Upload, Commit ni Push.

### Validación física DIAG.1 (2026-10-08)

- PlatformIO confirmó el BUILD DIAG.1 existente para `esp32-s3-devkitc-1` (RAM 150,660/327,680; Flash 1,953,020/6,553,600); sin cambios posteriores de firmware/configuración. Upload autorizado y ejecutado en `COM3` (`USB-Enhanced-SERIAL CH343`): SUCCESS, hash verificado y reinicio por RTS. No `uploadfs`, formato, Commit ni Push.
- `COM3` reapareció tras el reinicio. Receptor `request`: timeout, sin captura. Receptor `listen`: timeout; no hubo confirmación de que se pulsara el botón, por lo que el botón no se considera probado. No se produjo BMP; fidelidad/touch/memoria/repeticiones siguen pendientes.
- Ambos procesos de recepción finalizaron y cerraron su conexión. El dispositivo permaneció enumerado como `COM3`; una prueba breve de apertura/cierre confirmó que el puerto estaba libre.

### Ajuste de memoria DIAG.1 (2026-10-08)

- La cobertura de 48,000 bytes ahora se reserva en PSRAM al solicitar una captura, no en BSS interna al arrancar. Se libera tras completar el refresco o al cancelar; la imagen de 768,000 bytes se libera al terminar o cancelar la transferencia. Los fallos de PSRAM/reserva producen errores DIAG.1 y liberan la memoria parcial.
- El adaptador registra heap interno libre/bloque máximo y PSRAM total/libre/bloque máximo alrededor del registro de pantalla y de las reservas. Comprueba ambos buffers LVGL antes de registrar el display y conserva el envío LCD del callback.
- BUILD PlatformIO `esp32-s3-devkitc-1`: SUCCESS; RAM 102,660/327,680 (31.3 %), Flash 1,955,012/6,553,600 (29.8 %). Respecto al BUILD DIAG.1 anterior: RAM −48,000 bytes, Flash +1,992 bytes. Pruebas del receptor: 9/9; `git diff --check`: aprobado. No Upload, Commit ni Push.

### Arranque tras Upload de DIAG.1 corregida (2026-10-08)

- Upload autorizado en `COM3` con `esp32-s3-devkitc-1`: SUCCESS, hash verificado, sin `uploadfs`, borrado ni formato.
- Registro serie a 115200, tras una sola pulsación RESET confirmada: `Board begin success`, `40% Pantalla registrada`, `44% Pantalla táctil lista`, reservas de dos buffers LVGL de 64,000 bytes exitosas y `100% Sistema listo`, sin reinicio durante el ciclo capturado.
- El warning RGB conocido persistió, con framebuffer 1 disponible y framebuffer 2 no disponible; la ruta de buffers internos funcionó. Heap tras las reservas: interno libre 98,544, bloque máximo 34,804; PSRAM total 8,388,608, libre 7,614,368, bloque máximo 7,602,164. La visibilidad de la interfaz y la respuesta táctil esperan confirmación del usuario. No se probó captura aún.
- Monitor cerrado; `COM3` seguía enumerado y una apertura/cierre posterior confirmó el puerto libre.

### Captura física DIAG.1 por comando (2026-10-08)

- La captura `request` por COM3 completó la transferencia; el receptor validó BMP de 800×480/24 bits (1,152,054 bytes), checksum BED1 y revisión visual local. La imagen permanece como artefacto local y no se distribuye con este documento. La terminal informó `SD no disponible`; no se escribió en microSD.
- La transferencia tomó cerca de 10 minutos; el límite total predeterminado del receptor se aumentó de 180 a 900 segundos. Las pruebas del botón y de capturas repetidas estaban pendientes en ese momento; ver el resultado fechado posterior. COM3 fue cerrado por el receptor.
### Prueba física del botón Capturar por USB (2026-10-08)

- Confirmado COM3 y puerto libre antes de escuchar. Receptor `listen` a 115200 con espera de 900 s; el usuario confirmó la pulsación. DIAG.1 inició transferencia, pero terminó con `ACK_TIMEOUT_OR_RETRY_LIMIT`; no se recibió una imagen completa ni se creó BMP nuevo. El archivo de la captura por comando anterior no se alteró.
- El receptor cerró su conexión y COM3 abrió/cerró correctamente después. Sin Upload ni cambios al firmware. El disparo del botón se observó; su captura completa y la validación de imagen/checksum siguen pendientes.

### Instrumentación temporal ACK DIAG.1 (2026-10-08)

- Timeout provisional 5000 ms; empieza tras completar la escritura del frame DATA a Serial. Se conservan tres reintentos. En ERROR BED1 posterior al aborto se informan ID/bloque, reintentos, causa, último ACK/NAK recibido y coincidencia de ID/secuencia, intento y duración; sin texto diagnóstico durante la transferencia.
- Pruebas sintéticas receptor: 11/11 aprobadas (ACK exacto, NAK y reintento, ACK repetido del mismo bloque con demora sintética de 20 ms, conservación del ERROR diagnóstico). BUILD `esp32-s3-devkitc-1`: SUCCESS; RAM 102,684/327,680 (31.3 %), Flash 1,955,736/6,553,600 (29.8 %), sin warnings visibles; cambio frente al BUILD DIAG.1 anterior: +24 bytes RAM, +724 bytes Flash. `git diff --check`: aprobado. Sin Upload; prueba física pendiente; BMP anterior conservado. El registro guarda solo la última respuesta del bloque fallido y no reemplaza una traza serie física.

### Interfaz de progreso DIAG.1 (2026-10-08)

- El botón captura la pantalla actual antes de mostrar una vista independiente. Presenta los estados Preparando captura, Enviando, Reintentando, Verificando, Captura recibida y Error al enviar, y permite volver a la pantalla anterior al finalizar.
- La barra muestra 750 bloques y solo avanza con ACK BED1 del ID y secuencia esperados. Los reintentos mantienen el progreso confirmado. LVGL actualiza la vista desde su timer; no se intercalan textos de estado con el protocolo USB.
- El receptor envía `DIAG1 VERIFIED <id> <crc>` después de comprobar y escribir el BMP. El firmware espera esa respuesta antes de indicar éxito (timeout 30 s).
- BUILD `esp32-s3-devkitc-1`: SUCCESS, RAM 102,756/327,680 (31.4 %), Flash 1,958,808/6,553,600 (29.9 %). Pruebas sintéticas: 11/11; secuencia/progreso con `static_assert`; `git diff --check`: aprobado con avisos LF/CRLF.
- No Upload ni validación física. BMP anterior conservado.

### Captura DIAG.1 desde el botón (2026-10-08)

- El receptor `listen` en COM3 a 115200 recibió la captura tras la confirmación del usuario. La barra llegó al 100 % y se pulsó Volver. El BMP se conserva localmente; su ruta no se publica aquí. No se inició otra captura.
- BED1 CRC-32 coincidió entre META y END; el receptor envió `DIAG1 VERIFIED` con ID y checksum coincidentes. El BMP fue verificado como 800×480, 24 bits y 1,152,054 bytes; los hashes se conservan solo junto al artefacto local.
- BMP comprobado: 800×480, 24 bits y 1,152,054 bytes; CRC32 RGB565 reconstruido y SHA-256 coincidieron con la recepción. El archivo y los hashes se conservan localmente y no se publican.
- El receptor cerró el puerto y una apertura/cierre posterior confirmó COM3 libre. Sin Commit ni Push.


## Diagnostico de ranura microSD (2026-10-08)

Se implemento `SDManager` para la ranura TF integrada. Reutiliza el CH422G de `Board::begin()`, selecciona EXIO4 activo en bajo, SPI GPIO11/MOSI, GPIO12/SCK y GPIO13/MISO; monta `/sdcard` con formato automatico explicitamente desactivado; informa tipo y capacidad; enumera la raiz y desmonta. No escribe, elimina, renombra ni repara archivos. Distingue tarjeta no reconocida/sin respuesta de tipo reconocido con montaje FAT fallido. Waveshare documenta FAT32 para su demo de tarjeta; el usuario confirma que la tarjeta instalada de 64 GB ya esta en FAT32 y no debe reformatearse.

Validacion de software: BUILD `esp32-s3-devkitc-1` SUCCESS, RAM 103,316/327,680 bytes (31.5%), Flash 2,020,052/6,553,600 bytes (30.8%); 11 pruebas sinteticas del receptor DIAG.1 pasaron; `git diff --check` termino con codigo 0 (avisos informativos LF/CRLF).

Validacion fisica posterior al Upload autorizado: PlatformIO identifico COM3 como USB-Enhanced-SERIAL CH343 (VID:PID 1A86:55D3, serie [omitida]), reconocio ESP32-S3 con 16 MB Flash y 8 MB PSRAM, escribio el firmware normal y verifico los hashes. No se ejecuto `uploadfs`, borrado ni formato. El log de arranque mostro montaje exitoso de tarjeta SDHC, capacidad 62,568,529,920 bytes, una entrada en la raiz (`System Volume Information`) y desmontaje exitoso. No hubo escritura ni borrado.

Limitaciones observadas antes de corregir el splash: durante el acceso SPI aparecieron repetidos avisos `Invalid pin: 255` / `IO 255 is not set as GPIO`, aunque la lectura y desmontaje terminaron bien. En ese arranque previo, el registro del splash tambien dijo `MicroSD no disponible` pese al sondeo exitoso; la seccion de investigacion siguiente documenta la correccion y la verificacion serial posterior. No se validaron visualmente la interfaz ni otros archivos dentro de la tarjeta. La sesion de monitor serial propia termino con Ctrl+C; PlatformIO ya no mantiene esa sesion abierta. No se hizo Commit ni Push.

### Investigacion de avisos SPI y estado del splash (2026-10-08)

- Origen verificado de `Invalid pin: 255`: `src/storage/sd_manager.cpp` pasaba `-1` a `sdcard_init`. En Arduino-ESP32 3.1.1, `sd_diskio.h` declara el argumento como `uint8_t`; por tanto -1 llegaba como 255. `sd_diskio.cpp` guardaba el valor en `card->ssPin` y llamaba `pinMode`/`digitalWrite` con ese valor. La ruta anterior mantuvo EXIO4 bajo durante el sondeo, lo que permitio leer la tarjeta pese al aviso. La implementacion con CS externo descrita abajo reemplaza esa ruta.
- Causa del mensaje de splash: el texto solo distinguia `SD_NOT_PRESENT` y trataba todos los otros estados, incluido `SD_READY`, como `MicroSD no disponible`. Ademas, el texto `comprobando` se asignaba despues de `sd_manager.begin()`. Se corrigio la secuencia: progreso `comprobando` antes del sondeo; al retorno se muestra el estado final; el helper de estado contempla `SD_MOUNTING`, `SD_READY`, `SD_NOT_PRESENT` y `SD_ERROR`, y se reutiliza en detalle y progreso del splash.
- La correccion se cargo despues de confirmar COM3 (USB-Enhanced-SERIAL CH343, VID:PID 1A86:55D3, serie [omitida]) y el Upload normal termino SUCCESS con hashes verificados; no se ejecuto `uploadfs`, borrado ni formato. El registro posterior confirma `MicroSD: disponible` al 56% y al 85%, `Sistema listo`, montaje SDHC de 62,568,529,920 bytes, listado de `System Volume Information` (1 entrada) y desmontaje. El nuevo texto quedo verificado por serial; no se hizo inspeccion visual de la pantalla. BUILD `esp32-s3-devkitc-1`: SUCCESS, RAM 103,316/327,680 bytes (31.5%), Flash 2,020,060/6,553,600 bytes (30.8%); 11/11 pruebas sinteticas del receptor DIAG.1 y static_assert de los cuatro estados de splash; `git diff --check` exit code 0, con avisos informativos LF/CRLF. RAM queda igual y Flash aumenta 8 bytes respecto al BUILD anterior. Los avisos GPIO 255 persisten; quedan pendientes hasta contar con soporte de CS externo en el controlador. El monitor serial se cerro con Ctrl+C y su proceso termino con codigo 0.

## Correccion de capacidad en Almacenamiento local (2026-10-08)

- Origen del `4096 MB`: las cifras del sondeo se guardaban en `uint64_t`, pero `SDManager::totalBytes()/usedBytes()/freeBytes()` las saturaban a `SIZE_MAX` al convertir a `size_t` de 32 bits. El formateador de la interfaz redondeaba 4,294,967,295 bytes a 4096 MB. La capacidad fisica de la tarjeta ya se obtenia por separado de `sdcard_num_sectors * sdcard_sector_size`, pero el bloque de metricas llamaba `TOTAL` al tamano del volumen FAT y no lo distinguia claramente.
- Los getters SD conservan ahora `uint64_t`. Capacidad de tarjeta procede de sectores y tamano de sector medidos por SD; total/libre del volumen proceden de `f_getfree`/FATFS con la formula que usa Arduino-ESP32 3.1.1 para `SDFS::totalBytes()` y `usedBytes()`. Usado se calcula como total menos libre. Se marca la medicion FAT disponible solo si `f_getfree` devuelve estructuras y valores consistentes; si no, los tres campos muestran `No disponible`. Si la capacidad fisica no se mide, tambien muestra `No disponible`.
- La pantalla diferencia `Capacidad tarjeta` de `FS TOTAL`, `FS USADO` y `FS LIBRE`. El formato usa unidades binarias explicitas `B`, `KiB`, `MiB`, `GiB`. El sondeo sigue siendo de solo lectura, con montaje automatico desactivado; no se escriben, borran ni formatean archivos.
- `src/storage/storage_metrics.h` agrega pruebas `static_assert` de redondeo/formato de unidades, regresion del limite `UINT32_MAX`, capacidad fisica de 64 bits (122,204,160 sectores x 512 = 62,568,529,920 bytes, 58.3 GiB), formula de clusters FAT y rechazo de overflow.
- Validacion de software: BUILD `esp32-s3-devkitc-1` SUCCESS, RAM 103,316/327,680 bytes (31.5%), Flash 2,020,376/6,553,600 bytes (30.8%); 11 pruebas sinteticas DIAG.1 pasaron; `git diff --check` exit code 0, avisos informativos LF/CRLF.
- Upload autorizado posterior: COM3 confirmado como CH343 (VID:PID 1A86:55D3, serie [omitida]); Upload normal SUCCESS y hashes verificados, sin `uploadfs`, borrado ni formato. El arranque serial termino en `Sistema listo`; se repitio la lectura SDHC de 62,568,529,920 bytes, raiz con `System Volume Information` y desmontaje; splash en `MicroSD: disponible`. No se navego hasta Almacenamiento local, por lo que no se afirma una verificacion visual de las cifras nuevas. El registro serie propio se cerro con Ctrl+C y codigo 0. Persisten avisos `Invalid pin: 255` documentados arriba; la tarjeta no fue modificada.

## CS externo microSD por CH422G (2026-10-08)

- Se copio el controlador `sd_diskio.cpp` de Arduino-ESP32 3.1.1 a `src/storage/sd_diskio_external_cs.cpp` y `sd_defines.h` a `src/storage/`. No se modifico la instalacion global de PlatformIO. El `SDManager` ahora llama `sdcard_init_external_cs()` sin argumento de pin, usando el callback y el `esp_expander::Base*` ya inicializado por `Board::begin()`.
- El driver llama al callback para bajar EXIO4 antes de esperar/transmitir y subirlo al terminar cada seleccion. Si la activacion falla, intenta desactivar antes de retornar; si falla la espera, desactiva antes de informar error. Fallos al liberar CS quedan latched como error del dispositivo y hacen fallar la operacion. `SDManager::releaseBus()` vuelve a subir EXIO4 durante la limpieza. La funcion de escritura local esta bloqueada y devuelve `RES_WRPRT`.
- MOSI GPIO11, SCK GPIO12, MISO GPIO13 y EXIO4 activo en bajo se conservan. El montaje sigue con `format_if_empty=false`; la deteccion, lectura de capacidad/tipo y listado de raiz siguen sin escribir archivos.
- `tools/storage/check_sd_external_cs.py` pasa las comprobaciones estaticas: orden de seleccion antes de esperar, deseleccion ante timeout, ruta de error, ausencia de APIs GPIO/valor 255 en el controlador local y uso de CH422G/formato desactivado.
- BUILD final `esp32-s3-devkitc-1`: SUCCESS, RAM 102,988/327,680 bytes (31.4%) y Flash 1,998,952/6,553,600 bytes (30.5%); no reporto warnings. `git diff --check` termino con codigo 0; solo avisos informativos LF/CRLF de Git. `python tools/storage/check_sd_external_cs.py` paso las comprobaciones de orden y errores de CS, uso de EXIO4 activo en bajo, ausencia de GPIO ficticio, solo lectura y montaje sin formato.
- El mapa `firmware.map` asigna `sdcard_init_external_cs`, `sdcard_mount` y `sdcard_uninit` a `src/storage/sd_diskio_external_cs.cpp.o`; no contiene el objeto Arduino `sd_diskio.cpp.o`. La biblioteca SD sigue apareciendo en el grafo de dependencias por sus utilidades, pero el controlador enlazado para el sondeo es el local.
- No se hizo Upload ni se conecto la serie. El controlador local no tiene llamadas a `digitalPinToGPIONumber`, `pinMode` ni `digitalWrite` de Arduino para CS, y `SDManager` ya no llama `sdcard_init(uint8_t)`; por eso la ruta que generaba `Invalid pin: 255` fue retirada. La ausencia del mensaje en un arranque fisico queda pendiente de validar tras un Upload autorizado. La microSD no se probo ni modifico con esta version.
- El proceso temporal de PlatformIO termino al completar el BUILD. No se abrio un monitor serial ni otra conexion.

### Validacion fisica del CS externo (2026-10-08)

- Autorizado el Upload normal de `esp32-s3-devkitc-1`. Se detecto COM3 como USB-Enhanced-SERIAL CH343, VID:PID `1A86:55D3`, serie `[omitida]`; coincidio con la pantalla documentada. Upload SUCCESS, hashes verificados, sin `uploadfs`, borrado ni formato.
- Tras el reset automatico se identifico de nuevo el mismo COM3. Registro serie unico a 115200 desde `POWERON` hasta `Sistema listo`. No aparecio `Invalid pin: 255` ni `IO 255`. El sondeo leyo SDHC de 62,568,529,920 bytes, enumero `System Volume Information` (1 entrada), desmontó y mostro `MicroSD: disponible`. No hubo operaciones de escritura sobre la tarjeta.
- El arranque conserva los avisos conocidos `Unable to initialize the I2C address` de GT911 y `invalid frame buffer number` / `Get RGB buffer failed`; el touch configuro ID y version, y la interfaz completo el arranque. No se atribuyen ni modifican aqui esos avisos.
- La lectura serie informo `SERIAL_CLOSED`; el proceso de PlatformIO finalizo. No se hizo otra captura ni se hizo Commit o Push.
