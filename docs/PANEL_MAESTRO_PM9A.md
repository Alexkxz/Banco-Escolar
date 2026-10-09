# Banco Escolar · Panel Maestro · Fase PM.9A

## Estado y alcance

**Estado: documento de diseño de PM.9A; no es una fase ejecutable.** PM.9A definió la persistencia local del Panel mediante IndexedDB sin modificar modelos ni reglas académicas/monetarias. Las fases posteriores PM.9B (persistencia demo) y PM.9C (respaldo/restauración demo) ya están implementadas; este documento conserva el diseño que guio esos cambios.

Quedan fuera de PM.9A y del siguiente paso inmediato: backend, autenticación, selector de espacio, uso de datos reales, Service Worker, sincronización, conexión con firmware y acceso a LittleFS/microSD/NVS. La base real se define como espacio reservado, pero no se abre ni usa.

## Auditoría de PM.9A (estado al 07/10/2026)

La fuente de datos común de las rutas es el singleton `demoPanelService`, instancia de `DemoPanelDataService`. `PanelDataService` solo define consultas asíncronas de alumnos, cuentas, movimientos, actividades y cobros. El proveedor demo agrega consultas y operaciones de autorización, anulación, ajustes de cuenta, actividades, registros académicos, asistencia, reglas y aplicaciones de Áureos. Sus colecciones, contadores y set de confirmaciones viven en propiedades de clase y se reinician al recargar.

El seed de demostración usa seis perfiles ficticios con IDs internos de ejemplo, cuentas, movimientos, actividades y cobros sintéticos. No contiene nombres ni identificadores de alumnos reales. Autorizaciones, eventos de cobro, evaluaciones, asistencia y aplicaciones escolares empiezan vacíos. Las reglas académicas y de asistencia nacen con versión 1; las nuevas versiones y los registros que se capturen solo quedan en memoria. PM.3–PM.8 describen su contenido y los cambios de solo demostración.

`StudentPhotoProvider` guarda un `Map<student_id, {url, fileName}>` en React; la URL procede de `URL.createObjectURL` y se revoca al reemplazar, quitar o desmontar. El archivo validado (PNG/JPEG de hasta 5 MiB y 25 megapíxeles) y sus bytes no se guardan. Los PNG originales del proyecto son recursos separados y no deben alterarse ni copiarse al almacén de fotos.

La única persistencia actual de interfaz es `localStorage`: `panel-theme` y `panel-sidebar-collapsed`. No hay IndexedDB ni localStorage con datos escolares. Filtros, formularios y borradores pertenecen al estado React y no son registros confirmados. Carga, lista vacía, error y ausencia de cuenta ya son estados distintos en la UI; PM.9B debe preservar esa distinción frente a fallos de almacenamiento.

## Separación de espacios

Usar dos bases independientes del mismo origen, con esquema compatible pero contenido aislado:

| Espacio | Nombre lógico propuesto | Uso |
|---|---|---|
| Demostración | `banco-escolar-panel-demo` | Único espacio que PM.9B abre e inicializa. Conserva datos ficticios y operaciones demo. |
| Real local | `banco-escolar-panel-real` | Reservado para una etapa futura. PM.9B no lo abre, crea ni muestra en la interfaz. |

No se agrega selector: la aplicación actual apunta de forma fija al espacio demo. El futuro proveedor real solo podrá habilitarse en una fase con revisión propia; nunca reutilizará ni convertirá registros demo. Las bases no comparten stores ni IDs. Exportar/importar entre ellas estará bloqueado salvo una conversión explícita, diseñada y revisada por separado.

## Esquema propuesto

La versión de esquema es el entero de versión de IndexedDB y se mantiene además en `meta` para inspección. Los stores usan `keyPath` estable y datos structured-cloneables. La capa de persistencia devuelve los modelos TypeScript actuales, sin tratarlos como contrato de API. `student_id` permanece como vínculo de alumno en imágenes, cuentas, movimientos, cobros, actividades, evaluaciones, asistencia y aplicaciones.

| Store | Clave primaria | Índices propuestos | Contenido y relaciones |
|---|---|---|---|
| `meta` | `key` | ninguno | Modo del espacio, versión, semilla aplicada, versiones activas de reglas, contadores transaccionales de IDs y estado de migración. Nunca almacena secretos. |
| `students` | `student_id` | `grade`, `group`, `status` | Copia del padrón de ese espacio. Imagen y cuenta no se incrustan en el registro. |
| `studentImages` | `student_id` | `updatedAt` | Una imagen vigente por alumno: `Blob`, MIME, bytes, dimensiones, versión local, hash y fechas. La clave es el mismo `student_id`; quitar imagen elimina solo este registro. |
| `accounts` | `student_id` | ninguno | Saldo entero actual tomado directamente del modelo de cuenta. **Ausencia de fila = Sin cuenta; fila con `balance: 0` = cuenta existente con saldo cero.** |
| `movements` | `id` | `student_id`, `timestamp`, `type`, `origin`, `related_claim_id`, `related_movement_id`, `authorized_by_claim_id`, `related_school_record_id`, `related_school_application_id`; compuestos alumno/fecha | Movimientos existentes, incluidos inversos y entradas/salidas escolares. No se usan para reconstruir saldo. |
| `activities` | `id` | `status`, `started_at`, `participant_student_ids` (`multiEntry`) | ActivitySession actual con snapshot de `participant_student_ids`; cada ID se valida contra `students`. La lista `ALL` persiste como snapshot actual, no como consulta dinámica. |
| `claims` | `id` | `activity_id`, `student_id`, `status`, `movement_id`, `void_movement_id`, pares actividad/alumno/estado | ActivityClaim actual, incluyendo originales y anulados. El índice actividad/alumno no es único porque se permite nuevo cobro autorizado después de anular el previo. |
| `claimAuthorizations` | `id` | `source_claim_id` (único), actividad/alumno, `status` | Autorizaciones demo; `source_claim_id` conserva el cobro que la originó; `consumed_claim_id` enlaza el consumo cuando existe. |
| `claimEvents` | `id` | `claim_id`, actividad/alumno, `occurred_at`, tipo | Eventos `VOIDED`, `REAUTHORIZED`, `NEW_CLAIM`, con referencias existentes. No inventar actor autenticado. |
| `academicRecords` | `id` | `student_id`, `kind`, `school_date`, fecha y alumno/fecha/tipo (único) | Lectura, dictado o comprensión. Guardar/corregir conserva `id` e incrementa `version`, igual que hoy. |
| `attendanceRecords` | `id` | `student_id`, `school_date` (único compuesto), `status`, fecha y alumno/fecha | Un registro por alumno/día; llegada, estado y justificación según el modelo actual. Actualizar conserva ID e incrementa versión. |
| `academicRuleVersions` | `version` | `createdAt` | Snapshots inmutables de AcademicRules, incluido cada nivel editable y sus pesos/configuración sin inventar rangos. `meta.activeAcademicRuleVersion` señala la vigente. |
| `attendanceRuleVersions` | `version` | `createdAt` | Snapshots inmutables de AttendanceRules. `meta.activeAttendanceRuleVersion` señala la vigente. |
| `schoolApplications` | `id` | `student_id`, `record_type`/`record_id`/`indicator` (único), `movement_id`, `created_at` | Auditoría de aplicaciones y versiones/firma de reglas. Relaciona evaluación o asistencia; el movimiento es nullable para una aplicación de cero Áureos. |
| `operationReceipts` | `operationId` | tipo, fecha, IDs relacionados | Clave idempotente para la confirmación aceptada y referencias a sus resultados. Protege reintentos tras reload; no representa usuario autenticado ni una cola de sincronización. |

Los índices y claves foráneas en IndexedDB no hacen cumplir referencias automáticamente. La capa debe validar cada relación al leer/escribir y aplicar transacciones que incluyan todos los stores relacionados. Colecciones de registros se consultan por índices y los cargadores deben mantener estados `loading`, `ready` (incluye vacío), `error` y `unavailable`; una consulta fallida nunca devuelve `[]` como sustituto.

### Identidades y contadores

Conservar sin renumerar los IDs numéricos actuales de alumnos, cuentas, movimientos, actividades, cobros, autorizaciones, eventos, evaluaciones, asistencias y aplicaciones. El ámbito de unicidad es el store dentro de una base (demo o real). Mantener separados los contadores que el servicio ya usa: actividad, autorización, evento, registro escolar compartido por evaluación/asistencia y aplicación escolar. La reserva del siguiente ID se hará dentro de la misma transacción que crea el registro, tomando como mínimo el mayor ID ya presente; nunca confiar en `Math.max` de una copia cargada ni reutilizar IDs borrados.

Las colecciones iniciales actuales ya tienen IDs estables. Las imágenes ya usan `student_id`. La identidad de futuras personas reales o IDs originados por servidor todavía requiere contrato en la fase que incorpore datos reales; no cambiar ahora `student_id` ni introducir UUID alternos. `operationId` es un UUID generado para idempotencia de una confirmación, independiente de los IDs de dominio.

## Inicio único de la demostración

En PM.9B, al abrir por primera vez una base demo realmente nueva, sembrar alumnos, cuentas, movimientos, actividades, cobros, reglas iniciales y metadatos en una sola transacción readwrite. Guardar `demoSeedVersion`/`seedCompleted` al final de esa transacción. Si algo falla, abortar el seed completo; al siguiente intento detectar el estado incompleto y mostrar error/recuperación, no completar silenciosamente colecciones sueltas.

Si el marcador indica seed completo, abrir y leer los datos existentes; recarga, actualización de la app y cambio de ruta nunca vuelven a sembrar ni pisan filas. Registros capturados que empiezan vacíos continúan vacíos hasta que el maestro los cree. Cambiar versión del código demo no actualiza automáticamente el seed de quien ya tiene datos; se requiere una migración aditiva que preserve ediciones o una acción explícita de restaurar la demo de fábrica.

Las operaciones que hoy viven en RAM antes de PM.9B se pierden al cerrar/recargar esa versión; no es posible migrar una sesión que nunca se escribió. Al publicar PM.9B, la primera inicialización documentada parte del seed conocido de PM.3 y queda persistida desde ese punto. En el futuro, **Restablecer demostración** requerirá acción visible, confirmación explícita, alcance exclusivo a la base demo y aviso de pérdida; nunca se ejecutará al abrir la app ni afectará el espacio real.

## Transacciones e integridad

Una operación solo se reporta guardada al recibir `transaction.oncomplete`. `request.onsuccess` no basta. Validar de nuevo dentro de la transacción las versiones, saldo, estado, elegibilidad, referencias y clave de idempotencia: el preview React puede quedar obsoleto o otra pestaña puede haber escrito. Si falla una request, validación, cuota o escritura, abortar la transacción y conservar el borrador de UI; no mostrar éxito ni cambios parciales. IDB serializa transacciones readwrite que se solapan; una transacción demasiado amplia debe reducirse sin separar la atomicidad.

| Operación existente | Stores readwrite y acción atómica |
|---|---|
| Ajuste manual PM.7A | `accounts` + `movements` + `operationReceipts` + `meta`: verificar cuenta existente y `expectedBalance`; escribir saldo firmado, movimiento ENTRY/EXIT, recibo y contador de movimiento. Cero real queda como cuenta con balance 0. |
| Anular cobro PM.7 | `claims` + `accounts` + `movements` + `claimEvents` + `operationReceipts` + `meta`: revalidar cobro pagado, referencias y ausencia de anulación; guardar reclamo anulado, movimiento inverso del importe histórico, saldo resultante incluso negativo, evento y recibo. |
| Autorizar recobro | `claims` + `movements` (solo validar referencias/estado) + `claimAuthorizations` + `claimEvents` + `operationReceipts` + `meta`: crear una autorización y evento únicos. No tocar cuenta ni crear movimiento. |
| Simular nuevo cobro autorizado | `claimAuthorizations` + `claims` + `activities` + `students` + `accounts` + `movements` + `claimEvents` + `operationReceipts` + `meta`: volver a validar cuenta, actividad, vencimiento, participante y autorización; consumir una vez; agregar claim/movimiento/evento y actualizar saldo. |
| Aplicar Áureos desde evaluación/asistencia PM.8 | `academicRecords` o `attendanceRecords` + reglas vigentes + `accounts` + `schoolApplications` + `movements` + `operationReceipts` + `meta`: validar versiones/firma/saldo y ausencia de aplicación duplicada. Guardar aplicación; si importe != 0 actualizar saldo y agregar movimiento en la misma transacción. Si importe = 0, guardar la aplicación con balance igual y movimiento null; no inventar saldo ni movimiento. |
| Guardar/corregir evaluación | `academicRecords` + `meta`: clave natural alumno/fecha/tipo, conservar `id`, incrementar `version` al corregir y reservar ID al crear. Guardar evaluación no aplica Áureos. |
| Llegada/estado de asistencia | `attendanceRecords` + reglas vigentes + `meta`: unicidad por alumno/fecha, conservar ID/arrival, incrementar versión, no aplicar Áureos en esta operación. |
| Actualizar configuración de reglas | `academicRuleVersions` + `attendanceRuleVersions` + `meta`: guardar nuevos snapshots validados y mover punteros activos en una transacción. No reescribir aplicaciones pasadas. |
| Crear/editar/cerrar actividad | `activities` + `claims` + `meta` + recibo si aplica: preservar ID/inicio, revalidar estado y bloqueo por cualquier claim histórico, y reservar ID al crear. No toca saldos. |

Los índices únicos de aplicación escolar (record type, record ID, indicator), asistencia (student/date), evaluación (student/date/kind), autorización (source claim) y el recibo bloquean duplicación concurrente; el servicio aún debe convertir `ConstraintError` en el estado de dominio apropiado. Las reglas de negocio no cambian: edición/cierre, elegibilidad, saldo negativo permitido solo donde PM.7 lo decidió, y necesidad de cuenta se mantienen.

No reconstruir saldo desde movimientos. Nunca insertar cuenta al consultar o aplicar a un alumno sin cuenta. La operación debe fallar como `NO_ACCOUNT`; en UI “Sin cuenta” es distinto de cero, y un saldo cero válido se persiste/lee como `0`.

## Errores y recuperación

- **Almacenamiento no disponible:** IndexedDB ausente, denegado, bloqueado o conexión fallida; exponer estado `unavailable/error` con causa recuperable. No cambiar automáticamente al servicio demo en memoria ni fingir datos vacíos.
- **Colección vacía:** transacción de lectura completada con cero filas; mostrar estado vacío normal.
- **Error de consulta/carga:** request o transacción fallida; mantener error con reintento, no lista vacía.
- **Cuota:** abortar toda operación; conservar el estado anterior, mostrar error explícito y explicar que liberar espacio/exportar es una decisión del usuario. No borrar datos o imágenes automáticamente.
- **Cierre inesperado:** IndexedDB garantiza atomicidad de la transacción, no reemplazo de respaldo. Al reabrir, aceptar solo transacciones completadas; validar esquema/relaciones y dejar visible cualquier registro inconsistente para recuperación. No reparar saldos reconstruyéndolos.
- **Otra pestaña/versión:** atender `versionchange` cerrando la conexión; indicar que otra pestaña requiere recarga. Si `open` queda `blocked`, informar y esperar a las conexiones existentes; no cerrar navegadores ni borrar la base.

PM.9B debe capturar errores de apertura, lectura, escritura, `abort`, `blocked`, `versionchange`, cuota y `DataCloneError`, exponerlos a la capa de servicio y no devolver éxito antes del commit. Import/export o recuperar respaldo no es mecanismo automático de startup.

## Versiones y migraciones

1. Incrementar el entero `indexedDB.open(name, version)` solo con una migración definida y revisada. La función `onupgradeneeded` crea/ajusta stores e índices dentro de la transacción versionchange.
2. Guardar versión de esquema de registro o envelope de persistencia para transformar datos sin acoplar el schema IndexedDB al `schema_version` del firmware/modelo.
3. Migraciones ordenadas, repetibles de forma segura y no destructivas: agregar campos con defaults semánticamente válidos, preservar claves, fotos, historial y valores cero/negativos admitidos. No inventar cuenta para alumno sin cuenta ni normalizar un vacío a cero.
4. Validar conteos, claves, referencias y checksums antes de marcar la migración completa. La transacción abortada deja disponible el estado previo; la app presenta error y ofrece export/reintento/recuperación asistida.
5. Nunca borrar una base desconocida, borrar stores para “arreglarla”, reiniciar seed porque falla parseo, ni sobrescribir una versión mayor. Para versión futura incompatible: bloquear escritura, explicar que la app es antigua y pedir abrir con versión compatible.
6. PM.9C define un respaldo externo previo y restauración validada. No afirmar que migración transaccional de IndexedDB sustituye una copia independiente.

## Imágenes

Guardar un `Blob` en `studentImages` por `student_id`, junto con MIME permitido (`image/png` o `image/jpeg`), tamaño, dimensiones, versión/hash y fechas. Mantener los límites PM.4 actuales de 5 MiB y 25 megapíxeles por imagen hasta que una fase con pruebas reales los revise; no convertir formatos ni modificar PNG originales del proyecto. Reemplazar el registro del mismo `student_id` en una transacción; borrar solo ante acción explícita “Quitar imagen”, sin borrar al alumno ni operaciones relacionadas.

Persistir Blob, no `File`, object URL, ruta local ni base64. Al leer se puede crear URL de preview temporal; revocarla al reemplazar/quitar el Blob, cambiar imagen visible o desmontar el proveedor. No guardar la URL `blob:` en IndexedDB. Fallo al cargar una imagen no debe borrar la vigente.

La futura terminal no ofrece actualmente decodificación PNG/JPEG general ni recepción/almacenamiento de fotos; `avatar_asset` es referencia opcional y la UI LVGL dibuja avatares. Antes de transferir, investigar decoder/formatos LVGL, dimensiones, memoria RAM/PSRAM, flash/particiones, capacidad de LittleFS y costo de conversión. Una transferencia futura deberá vincular por `student_id`, referencia y versión/hash y marcar envío solo tras confirmación verificable; queda fuera de PM.9A.

## Límites del almacenamiento local

IndexedDB persiste solamente en ese navegador, perfil y origen de esa computadora; limpiar datos del sitio, cambiar perfil, navegador, origen/puerto o equipo puede hacerla inaccesible o eliminarla. Cuota, eviction y políticas privadas dependen del navegador/OS. No es un respaldo, no sincroniza computadoras ni la terminal, no crea backend ni garantiza recuperación tras daño del perfil. `localStorage` seguirá reservado a preferencia visual (tema/sidebar); registros escolares y Blobs van al almacén IndexedDB correspondiente.

## PM.9B — implementación y pruebas previstas

1. Implementar adaptador IndexedDB para el espacio demo y mantener `PanelDataService` como frontera; inyectar servicio persistente en la app sin mezclar persistencia con componentes. Mantener `DemoPanelDataService` como proveedor de prueba aislado.
2. Crear stores/índices con esquema versionado, seed de una sola vez y contadores atómicos. Mantener fallback visual de error/unavailable; no fallback silencioso a RAM.
3. Implementar transacciones y recibos de idempotencia de operaciones monetarias/escolares existentes; mantener confirmación de evaluación separada de aplicación de Áureos.
4. Persistir/revocar Blobs y URLs; conectar perfil a imagen guardada sin cambiar PNG originales.
5. Criterios de aceptación: una recarga y cierre/reapertura conservan datos; una actualización no vuelve a sembrar ni pisa cambios; cuentas inexistentes siguen sin fila; cero real y saldo negativo permitido sobreviven; confirmación repetida no duplica movimientos, claims, aplicaciones ni eventos; abortar cuota/fallo no deja cambios parciales ni muestra éxito; versiones/migraciones preservan datos; la foto se recupera por student_id y las URLs se liberan; la base real no se abre.

Pruebas previstas: abrir por primera vez y reinicializar; recargar; cerrar/reabrir navegador y perfil; datos vacíos frente a error; cero real, negativo y alumno sin cuenta; duplicar una solicitud simultánea/en segunda pestaña; inyectar abortos en cada store de transacción; edición/confirmación concurrentes con expected balance/version; migración de fixture anterior y apertura con versión superior; guardar/reemplazar/quitar/recuperar Blob, checksum, cuota y revocación de URLs. Ejecutar navegadores soportados y validar que la UI no convierte unavailable en colección vacía.

## PM.9C — respaldos, importación y recuperación

Diseñar un paquete autocontenido para un solo espacio con manifest de nombre lógico/modo, versión del esquema, fecha, conteos, claves/IDs y checksum; export de cada store como JSON UTF-8 e imágenes como Blobs/archivos binarios identificados por `student_id`, versión y SHA-256. Validar tamaño, MIME, integridad, relaciones, duplicados y reglas antes de importar. Un archivo inválido no cambia ninguna base. Exportar demo como demo; bloquear restauración de demo sobre real y viceversa.

La importación debe preparar y validar el conjunto completo antes de escribir; probar abortos y recuperación, mostrar preview/conteos/conflictos y exigir confirmación. PM.9C decidirá si la restauración reemplaza el espacio destino o combina por clave, cómo resolver colisiones de IDs/versiones y si el paquete se cifra. Ninguna estrategia de conflicto, cifrado ni contraseña queda aprobada por PM.9A. El formato debe ser verificable y documentado, incluir todas las imágenes, evitar sobrescrituras accidentales y nunca fingir que el respaldo se sincronizó con firmware.

Criterios previstos: exportar datos + Blobs y reimportar en perfil de prueba produce equivalencia; checksum/tamaño/MIME/referencias/versión incorrectos rechazan el paquete sin efectos parciales; duplicados y colisiones se muestran antes de confirmar; cierre durante import deja el destino anterior intacto; restaurar espacio equivocado se bloquea; repetir export genera respaldo distinguible y no sobrescribe otro.

## Decisiones aún abiertas

PM.9A adopta IndexedDB local y separación demo/real conforme a las decisiones del usuario. Antes de implementar las decisiones restantes:

- **PM.9B, modo demo persistente:** se recomienda activarlo de forma predeterminada al desplegar PM.9B, con acción explícita separada para restablecer. Confirmar si debe permitirse restaurar automáticamente el seed de fábrica después de corrupción o si el único camino será recuperación/export asistida; la recomendación es no restaurar automáticamente.
- **PM.9B, espacio real:** el acceso real queda fuera. Cuando llegue esa fase, decidir cómo un proveedor autenticado/confirmado recibe la identidad y asignación del namespace sin un selector que permita mezclar datos.
- **PM.9B, cuota/persistencia del navegador:** decidir si solo se consulta la cuota y se avisa, o también se solicita `navigator.storage.persist()` al usuario; nunca borrar datos ante cuota llena.
- **PM.9C, respaldos:** elegir contenedor (ZIP u otro), cifrado opcional/obligatorio y política de conflicto (reemplazo completo o merge revisado) antes de implementar. Esas decisiones cambian seguridad y riesgo de pérdida.
- **Integridad académica:** resolver si conservar historial de todas las versiones de reglas y aplicaciones con su snapshot completo. Hoy aplicaciones anotan versiones y firmas, pero el servicio mantiene solo la regla vigente; el diseño propone guardar snapshots inmutables para que la versión citada sea recuperable, sin recalcular aplicaciones antiguas.
- **Borrado de alumno:** definir archivo lógico vs. borrado físico y retención de registros/imagenes relacionados. PM.9A no agrega una operación de baja.

## Documentación consultada

Revisados `AGENTS.md`, la skill `banco-escolar`, sus referencias de Panel/modelo/arquitectura/roadmap, `PanelDataService`, `DemoPanelDataService`, `schoolDomain`, contextos de foto y preferencias, `panel-maestro/README.md` y los documentos PM.1–PM.8. El código actual es la referencia para la implementación; este diseño no convierte los tipos del cliente en contrato de API ni modifica firmware.
