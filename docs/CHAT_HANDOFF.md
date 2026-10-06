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
- El Menú Maestro DEV y la configuración de actividad están implementados. La configuración vive solo en RAM y no inicia ni persiste una actividad.
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

El acceso temporal `MENÚ MAESTRO DEV` está en Configuración. La Fase 5E (Menú Maestro base) fue reportada como validada físicamente. El acceso no utiliza PN532. La configuración de actividad permanece en memoria y se descarta al salir del Menú Maestro.

## ActivityDraft

`ActivityDraft` es temporal y no persistente. Contiene número opcional, tiempo, recompensa, modo de participantes, `selected_student_ids` y validez. Reglas actuales:

- Número opcional; si se habilita, 1–9999.
- Tiempo: sin tiempo o con tiempo; minutos 0–999, segundos 0–59; el tiempo configurado debe ser mayor que cero.
- Recompensa por alumno: 1–10,000 Áureos.
- Participantes: `ALL`, `SELECTED` o `PARTICIPANTS_DISABLED`. `ALL` incluye alumnos registrados; `SELECTED` exige al menos un `student_id` válido; `PARTICIPANTS_DISABLED` omite el filtro previo de elegibilidad y no significa cero alumnos.
- `Iniciar actividad` solo muestra `Configuración preparada`; no crea sesión, cuenta regresiva, movimiento ni saldo.

## Arquitectura futura de actividades

La aplicación deberá permitir múltiples actividades activas simultáneamente; no diseñar alrededor de una variable única `current_activity`. El flujo futuro será: Maestro configura e inicia → alumno completa → tarjeta identifica al alumno → terminal busca actividades activas elegibles → registro directo si hay una, o elección si hay varias → `ActivityClaim` → movimiento ENTRY → saldo actualizado mediante el motor monetario existente.

La pareja conceptual `activity_id + student_id` evita cobrar dos veces la misma actividad válida. ActivitySession y ActivityClaim son propuestas, no structs ni persistencia existentes.

## ActivitySession propuesta

Campos previstos: `id`, `activity_number` opcional, `reward_amount`, `duration_seconds`, `participant_mode`, `selected_student_ids`, `started_at` y `status`; estados propuestos `ACTIVE`, `FINISHED`, `CANCELLED`. No implementar hasta 5E.2.

## ActivityClaim propuesta

Campos previstos: `id`, `activity_id`, `student_id`, `claimed_at`, `movement_id`, `status`, `void_movement_id`. No implementar antes de 5E.4. No crear todavía `claims.ndjson`.

## Anulaciones

Una reclamación incorrecta no se elimina físicamente. En el futuro, el Panel la marcará `VOIDED` y creará un movimiento inverso que retire los Áureos incorrectos, conservando el historial.

## Panel Maestro

Futuro Panel como PWA multiplataforma y offline-first, con API y base central. Podrá revisar alumnos, saldos, historial, entradas manuales, actividades activas, ActivityClaims, anulaciones/reversos y sincronización diferida. No se han implementado Panel, API, servidor, base central, Service Worker ni IndexedDB.

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
3. 5E.2: persistencia de ActivitySession, solo después de revisar el borrador y sus requisitos.
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
- `ActivityDraft` no es `ActivitySession`; alumno no cobra dos veces la misma actividad válida.
- No imprimir contraseñas, tokens, claves NVS ni secretos.

## Flujo de trabajo con Codex

Leer `.agents/skills/banco-escolar/SKILL.md` y referencias pertinentes; verificar código y estado Git; cambiar solo lo autorizado; ejecutar BUILD y reportarlo antes de cualquier carga; revisar `git diff --check`; distinguir BUILD de prueba física. El flujo es: cambios → BUILD SUCCESS → reporte → revisión/autorización → Upload. No ejecutar Upload ni Upload and Monitor sin autorización explícita. Cierre 5E.1C autorizado: BUILD, un commit local coherente y push normal a `origin/main`; nunca force push.
