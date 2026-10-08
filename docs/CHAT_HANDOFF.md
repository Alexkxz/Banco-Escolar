# Banco Escolar — Handoff

## Objetivo del proyecto

Terminal educativa local para primaria sobre Waveshare ESP32-S3-Touch-LCD-7. El firmware usa LVGL, LittleFS y NVS; el saldo y movimientos persistentes conviven con datos académicos provisionales. El Panel Maestro PWA, API y sincronización son futuros. Este archivo resume el estado para retomar el desarrollo en un chat nuevo.

## Hardware

- Waveshare ESP32-S3-Touch-LCD-7 Rev 1.2; LCD RGB ST7262 800×480, touch GT911, expansor CH422G.
- Flash física reportada: 16 MB; PSRAM física reportada: 8 MB. No cambies RGB, PSRAM, GT911/I²C0 o buffers por warnings sin una fase de diagnóstico.
- microSD de 64 GB instalada según el usuario, pero el firmware `SDManager` es todavía un placeholder: no accede a GPIO ni intenta montar. Se decidió omitir microSD en las próximas fases inmediatas. No formatear ni cambiar su conexión en este cierre.
- PN532 está desconectado y `PN532_ENABLED=false`. El uso futuro por I²C1 GPIO43/44 requiere una fase explícita y confirmar cableado, alimentación y niveles.

## Software

- PlatformIO, C++17, Arduino-ESP32 3.1.1, LVGL 8.4.0 y ESP32 Display Panel.
- `src/main.cpp`: UI, callbacks, temporizadores y flujo principal.
- `src/storage/storage_manager.*`: LittleFS, movimientos, cuentas y recuperación transaccional.
- `src/storage/sd_manager.*`: stub de microSD; no detecta ni monta la tarjeta.
- `src/data/provisional_data.*`: datos provisionales de alumnos y registros académicos.
- `lib/lvgl/src/draw/sw/lv_draw_sw_layer.c`: parche local para comprobar `lv_mem_alloc()` antes de inicializar la capa; revisarlo si se actualiza LVGL.

## Estado estable de Git previo

- Rama `main`.
- Último commit estable previo a este cierre: `317b02db39fd11e227835ef1862618ea59470d0f` — `Implementa saldo persistente real de Áureos`.
- Antes del nuevo commit, `origin/main` y `HEAD` estaban ambos en ese SHA. Los cambios pendientes de 5D, 5E, 5E.1, 5E.1A, 5E.1B y documentación se deben respaldar juntos en un commit normal.

## Funcionalidades completadas

- LittleFS monta normalmente con formato automático cerrado. `STORAGE_ALLOW_ONE_TIME_FORMAT=false` y `STORAGE_SELF_TEST=false`.
- `StudentAccount` y movimientos ENTRY/EXIT se escriben de forma coordinada mediante `/data/pending_transaction.json`; la recuperación compara el journal, el movimiento y el saldo para evitar duplicados.
- El historial lee movimientos de LittleFS y resuelve el nombre por `student_id`; no reconstruye el saldo desde el historial.
- Registrar salida usa saldo persistente. Registrar entrada manual añade una cantidad positiva con máximo individual de 10,000 Áureos.
- El Menú Maestro DEV está implementado. ActivityDraft permanece en RAM hasta iniciar; 5E.2 ahora persiste ActivitySession en LittleFS tras validar hora.
- Timeout de sesión de alumno: `SESSION_TIMEOUT_SECONDS=60`. El código ya suspende el timer en el contexto Maestro; ver “Pendientes conocidos”.

## Saldo y movimientos actuales

Validación física 5D indicada por el usuario para Darío (`student_id=7`): saldo inicial 120 Áureos, entrada +10, saldo final 130, movimiento ID 5, `amount=+10`, `synced=false`; tras reinicio, saldo 130 y cinco movimientos sin duplicación. Los movimientos conocidos son IDs 1–3, salidas -5 históricas; ID 4, salida -5; ID 5, entrada +10. Este es el estado de prueba de la terminal, no un valor que se deba hardcodear.

## Storage

- `/data/movements.ndjson`: movimientos NDJSON con `student_id` como identidad, `schema_version=1`, `synced=false` hasta una sincronización futura.
- `/data/accounts.ndjson`: snapshots append-only de `StudentAccount` por alumno.
- `/data/pending_transaction.json`: journal de recuperación para movimiento más saldo.
- NVS namespace `bank_storage`; `next_mv_id` se reserva antes de escribir y los IDs no se reutilizan. `clearLocalData()` conserva NVS y el contador.
- Prueba controlada de corte de energía aún pendiente. No ejecutar durante este cierre.
- microSD no está integrada en Storage y está fuera del alcance inmediato.

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

ActivitySession ya está implementada en 5E.2; ActivityClaim, cobros y deduplicación siguen futuros.

## ActivitySession persistente (5E.2)

ActivitySession contiene ID, esquema, número opcional, recompensa, duración, modo y snapshot de participantes, inicio y estado; ActivityClaim sigue pendiente.

## ActivityClaim propuesta

Campos futuros de ActivityClaim: id, activity_id, student_id, claimed_at, movement_id, status y void_movement_id.

## Anulaciones

Una reclamación incorrecta no se elimina físicamente. En el futuro, el Panel la marcará `VOIDED` y creará un movimiento inverso que retire los Áureos incorrectos, conservando el historial.

## Panel Maestro

Futuro Panel como PWA multiplataforma y offline-first, con API y base central. Podrá revisar alumnos, saldos, historial, entradas manuales, actividades activas, ActivityClaims, anulaciones/reversos y sincronización diferida. No se han implementado Panel, API, servidor, base central, Service Worker ni IndexedDB.

Nota de estado del cliente demo: el directorio y las funciones PM.3–PM.8 sí están implementados en `panel-maestro/`, en memoria y sin conexión con firmware. PM.8 incorpora progreso académico, asistencia, reglas escolares y aplicaciones ficticias de Áureos. No modifica las decisiones del panel real futuro; ver [PANEL_MAESTRO_PM8.md](PANEL_MAESTRO_PM8.md).

## NFC

`PN532_ENABLED=false`; lector desconectado. En el futuro las tarjetas solo aportarán UID para buscar `student_id`; nunca guardarán saldo, historial, actividad ni datos académicos. No habilitarlo ni inventar UIDs sin fase y comprobación física autorizadas.

## Pendientes conocidos

- **5E.1A:** la lógica de suspensión del timeout ya está implementada en código y compiló. `is_master_context_active()` cubre Menú Maestro, Configurar actividad, Seleccionar alumnos, keypad y Configuración preparada; el timer normal se conserva fuera del contexto y el contador se reinicia al salir desde el menú principal. La validación física de más de 60 segundos en cada pantalla sigue pendiente. La instrucción de este cierre que etiqueta 5E.1A como no implementada contradice el código y el trabajo anterior; no rehacerla ni quitarla.
- **5E.1B:** pulido visual y glifos compilados; pendiente de validación física final en LIGHT/DARK. “Más logros próximamente” queda pendiente tipográfico fuera de la pantalla Logros que 5E.1B prohibió modificar.
- **Wi-Fi:** se observaron reconexiones repetidas aunque la aplicación continuaba; diagnóstico separado pendiente, no bloqueante.
- **Energía:** falta prueba controlada de corte durante una transacción.
- **microSD:** tarjeta 64 GB instalada según el usuario pero no detectada porque `SDManager` no implementa detección/montaje. Se omitió por ahora; no incluir en el próximo trabajo inmediato.
- Los datos académicos, Logros y varias vistas siguen siendo provisionales/demo; mantener `N/A` distinto de cero y respetar los umbrales actuales.

## Próximas fases

Orden recomendado, distinguiendo implementación de validación:

1. Validar físicamente 5E.1A (ya implementada en código): dejar cada pantalla del contexto Maestro abierta por más de 60 s y confirmar que no vuelve a Inicio; comprobar que las pantallas de alumno mantienen su timeout normal.
2. Validar físicamente 5E.1B en 800×480, tema claro y oscuro: acentos, `Áureos`, `número`, `selección`, `3.º`/`4.º B`, resumen en cuatro bloques, selección de alumnos y ausencia de cortes.
3. Validar físicamente la Fase 5E.2 después de revisar el BUILD y autorizar carga.
4. 5E.3: múltiples actividades activas simultáneamente.
5. 5E.4: ActivityClaim, deduplicación y uso del motor monetario existente.
6. 5E.5: integración NFC de alumno y tarjeta maestra, únicamente con autorización y cableado seguro.
7. Después: Panel Maestro, sincronización, asistencia, logros y microSD si se retoma.

## Reglas críticas de desarrollo

- No asumir que microSD es prioridad; respetar la decisión de omitirla por ahora.
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
- DIAG.1 está definida como trabajo de diagnóstico, pero la auditoría del código no encontró una implementación identificada como DIAG.1. Solo hay constantes y rutinas temporales de diagnóstico/rendimiento en `src/main.cpp`, desactivadas por defecto; no se ejecutaron en la terminal.
- En Comprensión, la demo permite clasificar cada aspecto como Requiere apoyo, En desarrollo o Logrado con denominadores configurables. Los pesos y los límites del resultado ponderado general permanecen sin configurar por defecto; por eso todavía no hay rangos/niveles definidos para emitir una clasificación general ponderada. No inventar una clasificación general.
- Las siete imágenes nuevas de `Imagenes/Alumnos/` se revisaron: son ilustraciones pixel-art de avatares escolares ficticios, no fotografías originales de alumnos. Se incluyen como recursos destinados al proyecto. Las capturas versionadas de `panel-maestro/capturas/` corresponden a evidencia visual del panel; se excluyen artefactos generados, dependencias, salidas de build, secretos y archivos temporales.
- No hay pantalla conectada para pruebas físicas en el estado reportado para esta publicación. No se hizo ni se debe inferir Upload.

## Validaciones y auditoría de publicación (2026-10-07)

**Ejecutado manualmente por el usuario:** BUILD PlatformIO para `esp32-s3-devkitc-1`: SUCCESS; RAM 101,436 / 327,680 bytes (31.0%); Flash 1,949,356 / 6,553,600 bytes (29.7%); 116.89 s; sin warnings visibles en la salida compartida. En esta auditoría se compararon los cambios locales y la configuración: no se encontraron cambios de firmware/configuración posteriores a ese BUILD. No se repitió PlatformIO por la restricción de permisos local reportada; este resultado corresponde al BUILD manual del usuario, no a una ejecución del auditor.

**Ejecutado por el auditor:** pruebas web PM.8, 82/82 aprobadas en 13 archivos; build web TypeScript/Vite correcto; `git diff --check` correcto. La revisión visual headless de PM.8 registrada arriba también fue hecha por el auditor en ambos temas y tamaños desktop/móvil; no equivale a una prueba física del dispositivo.

El intento previo del auditor de iniciar PlatformIO se detuvo antes de compilar porque Windows denegó escritura en `C:\Users\alexk\.platformio\platforms.lock` y PlatformIO reportó `HomeDirPermissionsError` para `.platformio\.cache`. No se cambiaron esos permisos. La salida manual SUCCESS aportada por el usuario es la validación de BUILD vigente.
