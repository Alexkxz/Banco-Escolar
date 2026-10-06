# Modelo de datos y ejemplos provisionales

La fuente primaria es `src/student_model.h`, `src/academic_config.h` y `src/data/provisional_data.*`. No uses esta referencia para reemplazar los datos del código si cambia el proyecto.

## Tipos

- `Student`: `student_id`, `nfc_uid`, `name`, `preferred_name`, grado, grupo, número de lista, referencia de avatar y nivel temporal. El modelo no incorpora saldo dentro de `Student`.
- `AccountRecord`: `student_id` y saldo demo provisional en RAM; los flujos monetarios reales usan `StudentAccount`.
- `StudentAccount` (Fase 5C): `student_id`, `int64_t balance` y `schema_version=1`; se persiste en LittleFS y no almacena nombre, UID ni número de lista.
- `ReadingRecord`: alumno, fecha, PPM y `applied`.
- `WritingRecord`: alumno, fecha, palabras, errores y `applied`.
- `StudentMovement`: alumno, cantidad firmada, tipo, motivo, fecha/hora y origen.
- `AttendanceRecord`: tipo preparado; no implica registros reales.

## Identidad de movimientos persistidos

`MovementRecord.student_id` es la clave que vincula cada movimiento persistido con `Student.student_id`. El nombre visible se resuelve al presentar el movimiento mediante `getStudentById()` y la preferencia/nombre de `Student`; no se copia el nombre al NDJSON ni se cambia el esquema del movimiento para mostrarlo. Si el ID no existe en los datos disponibles, la interfaz debe conservar el movimiento y mostrar un fallback que identifique el ID desconocido.

## Registros demo comprobados

Hay 13 alumnos provisionales: seis de 3.º y siete de 4.º; identificadores 1–13. Todos tienen `nfc_uid` sin asignar. Darío es `student_id = 7`, nombre `CAMACHO ARREOLA ALONZO DARIO`, presentación `Darío`. No inventes UID.

Hay 39 lecturas (13 × 3) y 52 dictados (13 × 4), más una cuenta y tres movimientos demo para Darío. Su lectura: 01/09/2026 82 PPM, 07/09/2026 83 PPM, 11/09/2026 107 PPM. Dictados: 01/09 34 palabras/8 errores; 07/09 45/5; 11/09 no aplicado; 21/09 58/7.

`applied = false` no significa cero errores ni genera asistencia. Cero errores solo es una evaluación aplicada sin errores.

## Umbrales de Fluidez lectora

Estos valores coinciden con `src/academic_config.h`:

| Grado | Apoyo | Cerca del estándar | Estándar | Avanzado |
|---|---:|---:|---:|---:|
| 1.º | <15 | 15–34 | 35–59 | ≥60 |
| 2.º | <35 | 35–59 | 60–84 | ≥85 |
| 3.º | <60 | 60–84 | 85–99 | ≥100 |
| 4.º | <85 | 85–99 | 100–114 | ≥115 |
| 5.º | <100 | 100–114 | 115–124 | ≥125 |
| 6.º | <115 | 115–124 | 125–134 | ≥135 |

Darío está en 4.º; con 107 PPM se clasifica Estándar, meta Avanzado 115 y diferencia 8 PPM. `Student.level` es temporal y no se deriva del saldo.

## Dinero y demo de transferencia

La moneda se configura en `src/app_config.h` con `CURRENCY_NAME`, hoy Áureos. La salida individual de Darío usa la cuenta persistente, exige fondos y persiste movimiento más saldo de forma coordinada. La transferencia Darío → Fernanda entre dos alumnos continúa siendo futura; no se implementa un crédito/débito doble.

La cuenta inicial de `student_id=7` se crea de forma idempotente con el valor demo vigente (125 Áureos), solo al entrar explícitamente en Demo y únicamente si todavía no existe. Los demás alumnos no reciben saldo automático. El historial previo no recalcula ni descuenta el saldo.

La validación física de 5C.1 reportó una salida nueva de 5 Áureos (movimiento ID 4), saldo actualizado de 125 a 120 y persistencia tras reinicio: saldo 120, cuatro movimientos y sin duplicación. Los IDs históricos 1–3 se mantuvieron intactos. El archivo `/data/accounts.ndjson` no existía antes de la primera inicialización y se creó correctamente.

Fase 5D añade entradas manuales con amount positivo y `StoredMovementType::ENTRY`, usando `StudentAccount` y el journal transaccional. No cambia `MovementRecord` ni su esquema. Prueba física reportada: saldo 120 + 10 = 130, movimiento ID 5, `student_id=7`, `synced=false`; tras reinicio hubo cinco movimientos sin duplicación. 5D está validada físicamente.

## Actividades — FUTURO / PROPUESTO (no implementado en 5E)

- `ActivitySession`: `id`, `activity_number` opcional, `reward_amount`, `duration_seconds`, `participant_mode`, `started_at`, `status`.
- `ActivityClaim`: `id`, `activity_id`, `student_id`, `claimed_at`, `movement_id`, `status`, `void_movement_id`.

Estos campos son una propuesta conceptual; no existen structs ni archivos persistentes para ellos. La arquitectura permitirá más de una actividad activa. Si un alumno solo es elegible para una, el registro futuro podrá ser directo; si hay varias, tendrá que seleccionar cuál completó. No se permitirá duplicar un cobro válido de la misma actividad por alumno. El flujo futuro identifica al alumno, valida la actividad, registra la participación, crea una entrada de Áureos mediante el motor transaccional existente y actualiza `StudentAccount`. Los cambios de dinero deberán distinguir su contexto/origen (entrada manual, actividad, Panel Maestro), pero esta fase no modifica `MovementRecord` ni define un cambio de esquema.

Las anulaciones futuras desde el Panel marcarán la participación `VOIDED`/anulada y crearán un movimiento inverso que retire los Áureos; no eliminarán físicamente registros. Nada de lo anterior se implementa ni persiste en Fase 5E.

## `ActivityDraft` — TEMPORAL EN MEMORIA (Fase 5E.1)

`ActivityDraft` vive solo en RAM mientras se configura una actividad y se descarta al salir del Menú Maestro. Contiene `activity_number_enabled`, `activity_number`, `timed`, `duration_seconds`, `reward_amount`, `participant_mode`, `selected_student_ids` (solo claves `student_id`) y `valid`. No contiene `started_at`, `ends_at`, ID de actividad ni estado ACTIVE. `DISABLED` para participantes significa que no se aplica un filtro previo de elegibilidad; no significa cero alumnos. `ALL` expresa explícitamente todos los alumnos registrados, mientras que `SELECTED` necesita al menos un ID válido.

En la futura Fase 5E.2, solo un borrador válido podrá convertirse en una entidad persistida `ActivitySession`. El formulario actual no reserva IDs, escribe archivos ni crea `ActivitySession`/`ActivityClaim`.
