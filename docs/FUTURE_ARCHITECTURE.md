# Banco-Escolar: arquitectura y estado del proyecto

Este documento conserva las decisiones arquitectónicas de las Fases 1 y 2 y
las actualiza con el estado vigente del firmware. Los marcadores distinguen
funcionalidad **IMPLEMENTADA**, vistas o datos **DEMO**, infraestructura
**PREPARADA**, validación física **VALIDADA** y trabajo **FUTURO/PENDIENTE**.
El código fuente es la referencia para el comportamiento actual; una prueba
física se indica por separado y no se deduce de compilar.

## Estado de la Fase 1

- **IMPLEMENTADO:** Waveshare ESP32-S3-Touch-LCD-7 a 800x480 con ST7262,
  GT911, CH422G y LVGL 8.4.0.
- **IMPLEMENTADO:** fuente española, fecha/hora de demo, navegación y
  animaciones actuales.
- **IMPLEMENTADO:** Wi-Fi en modo estación (`WIFI_STA`), escaneo asíncrono,
  selección de red, conexión protegida/abierta, guardado de credenciales en
  Preferences tras una conexión exitosa y reconexión. La conexión fue validada
  físicamente.
- **IMPLEMENTADO:** `TimeManager` con NTP y zona `America/Mexico_City` usando
  `CST6`; el arranque espera sincronización con fallback no bloqueante.
- **PREPARADO / DESHABILITADO:** lógica de lectura PN532 desarrollada, pero
  `PN532_ENABLED=false` y el lector está desconectado. No se considera operativo.
- **IMPLEMENTADO / DEMO:** modelo de 13 alumnos provisionales y vistas académicas; las cuentas y movimientos monetarios usan persistencia local.
- **IMPLEMENTADO / VALIDADO:** registrar salida y entrada manual usan `StudentAccount` y el journal transaccional; la entrada 5D se validó tras reinicio.
- **IMPLEMENTADO / VALIDADO:** el historial guarda movimientos en LittleFS y resuelve el nombre visible desde el `student_id` persistido.
- **PREPARADO:** nombre de moneda configurable en `src/app_config.h` mediante
  `CURRENCY_NAME`.
- **IMPLEMENTADO:** umbrales de Fluidez lectora por grado en
  `src/academic_config.h`; otros niveles, logros y reglas de asistencia quedan
  pendientes.
- **IMPLEMENTADO / VALIDADO:** LittleFS y `StorageManager` guardan movimientos
  reales de salida individual; la validación física confirmó persistencia y continuidad de IDs.
- **FUTURO:** servidor/API, Panel del Maestro, sincronización y OTA.

## Modelo de dominio

`src/student_model.h` define `Student` con identidad, UID, nombre preferido,
grado, grupo, número de lista, referencia opcional a avatar y nivel temporal.
`AccountRecord` conserva el saldo demo provisional. `StudentAccount` persiste aparte `student_id`, saldo y versión en LittleFS; la UI monetaria lee esa cuenta y no reconstruye el saldo desde movimientos.

Las funciones `setStudentBalance()` y `updateBalanceUI()` permanecen para vistas demo; no son la fuente de saldo de Inicio, Mi cuenta ni las operaciones reales.

`StudentMovement` es el modelo de presentación. `MovementRecord` persiste `student_id`, cantidad, tipo, motivo, fecha, origen y estado de sincronización; `getStudentById()` resuelve el nombre para mostrar. Registrar salida y entrada manual actualizan cuenta y movimiento con el journal transaccional. La transferencia entre alumnos y sincronización siguen pendientes.

El Panel Maestro PM.4 presenta un directorio con seis alumnos ficticios y una selección de imagen solo en memoria, asociada al `student_id`; no escribe imagen ni estado al firmware. El modelo actual conserva `avatar_asset` como referencia opcional, pero la UI consultada dibuja avatares genéricos con LVGL y no se encontró decodificación PNG/JPEG general ni ruta de recepción/almacenamiento. Una futura vinculación debe identificar alumno por ID e incluir referencia y versión de imagen, y solo indicar sincronización tras confirmación. No se define formato final hasta evaluar decoder LVGL, dimensiones, memoria disponible, flash/particiones, LittleFS y costo de conversión. Ver [PM.4](PANEL_MAESTRO_PM4.md).

PM.5 añade consultas de lectura al conjunto de cuentas y movimientos demo. La cuenta muestra el snapshot de saldo directamente; el resumen de entradas/salidas describe solo el historial disponible y no ajusta ese saldo. Para el cliente, un detalle de cuenta demo se direcciona mediante `student_id`, ya que el modelo actual no provee ID separado de cuenta. Los filtros de fecha usan días locales de `America/Mexico_City` convertidos con zona IANA, manteniendo inalterados los timestamps. Ninguna ruta agrega escritura o sincronización. Ver [PM.5](PANEL_MAESTRO_PM5.md).

PM.6 agrega crear, editar, finalizar y cancelar actividades únicamente al servicio de demostración del Panel Maestro. Sus cambios duran en memoria durante la navegación y dependen del reloj de la computadora; recargar los descarta. PM.7 agrega consulta y operaciones ficticias de anulación y re-cobro autorizado en el mismo servicio demo; no implementa la anulación ni autorización en firmware, no persiste y no sincroniza. PM.7A añade ajustes manuales de saldo desde el perfil, en memoria y solo en el servicio demo; el firmware conserva su límite de abono individual y rechaza saldo negativo. PM.8 añade registros académicos, asistencia y reglas configurables con aplicaciones monetarias simuladas y versionadas, también en memoria; los modelos de evaluación, estados de asistencia y aplicaciones no existen así en el firmware. Estas funciones de cliente no son un contrato de API ni capacidades reales de la terminal. Ver [PM.6](PANEL_MAESTRO_PM6.md), [PM.7](PANEL_MAESTRO_PM7.md), [PM.7A](PANEL_MAESTRO_PM7A.md) y [PM.8](PANEL_MAESTRO_PM8.md).
El nombre actual de la moneda es **Áureos**, pero la interfaz lo obtiene de
`CURRENCY_NAME`; para renombrarlo en el futuro se cambia un solo lugar.

## Arquitectura prevista

```text
Student
 ├── Account
 │    └── Áureos (nombre configurable)
 ├── AcademicProgress
 │    ├── ReadingFluency
 │    │    ├── palabras por minuto
 │    │    └── registros históricos
 │    ├── Writing
 │    │    ├── cantidad/tipo de errores
 │    │    └── registros históricos
 │    └── futuros indicadores
 ├── Achievements
 └── Level (pendiente de definición; basado en logros/progreso, no en saldo)
```

`AcademicProgress`, `Achievements` y las reglas de `Level` son **FUTURO**.
Deberán permanecer independientes de LVGL y de las pantallas.

La separación de capas prevista es:

```text
UI LVGL
   ↓
Student / Account / AcademicProgress
   ↓
Persistencia local / cola pendiente (integración futura)
   ↓
API ↔ Servidor / Data Service ↔ Base de datos central (futuros)
 ↕
Panel Maestro PWA (futuro)
```

## Escuela multigrado

La arquitectura acepta cualquier combinación de grados y grupos. Los valores
`4° B` del alumno demo son solo datos de prueba, no una lista cerrada. En una
fase posterior el administrador podrá definir combinaciones como `3° A`, `3° B`,
`4° A`, `4° B` u otras, mediante configuración inicial (**FUTURO**).

La primera versión contempla un solo administrador (**PREPARADO como decisión
de alcance**). No se implementan todavía múltiples maestros, roles ni permisos.

## Panel Maestro PWA y red: decisión oficial 0S.2B

- **IMPLEMENTADO:** conectividad Wi-Fi del terminal para redes locales e
  Internet, con estado mostrado en Configuración.
- **DECISIÓN / FUTURO:** Panel Maestro como PWA multiplataforma + API + base de
  datos central, con filosofía offline-first para Panel y terminal.
- **FUTURO:** uso desde navegador en Windows, Android, iPhone/iPad, macOS,
  Linux y tablets; instalación PWA cuando la plataforma lo permita.
- **FUTURO:** una interfaz responsive con menú lateral en escritorio y compacto
  en móvil; la lógica principal no dependerá de APIs exclusivas de Windows.
- **FUTURO:** servidor/API local, con posible crecimiento posterior a nube/remoto.

La UI no debe contener IPs, URLs ni detalles del servidor. La comunicación debe
pertenecer a un servicio de datos independiente.

La aplicación nativa de escritorio no es prioridad inicial: no se inicia como
.exe, WinForms, WPF o UWP. La misma interfaz web podría empaquetarse después
con Tauri o equivalente; Electron queda como alternativa opcional. No se ha
elegido framework frontend/backend ni tecnología de base de datos.

### Offline-first y almacenamiento de la PWA — FUTURO

Guardar localmente primero cuando sea necesario y sincronizar después. La PWA
deberá abrirse sin Internet después de una carga/instalación previa que haya
conservado sus recursos; la primera apertura no puede presuponerse offline.
Se prevé Service Worker para cachear interfaz, administrar recursos estáticos,
permitir arranque offline y facilitar actualizaciones. Se prevé IndexedDB
para caché de alumnos, movimientos recientes, operaciones pendientes,
configuración y datos académicos necesarios. Ninguno está implementado.

### API, fuente de verdad y eventos — FUTURO

Hoy LittleFS es almacenamiento persistente de la terminal. Cuando exista
servidor, la base central será la fuente de verdad global y ESP32/PWA mantendrán
caché, cola pendiente y estado de sincronización. Ambos clientes pasarán por
la API para acceder a la base central. El servidor no debe ser un requisito
obligatorio para operaciones locales válidas.

La API asumirá autenticación, autorización, recepción de movimientos,
consultas, sincronización, validaciones, deduplicación y conflictos. El acceso
del Panel será protegido, inicialmente para un administrador/docente principal,
con crecimiento posterior a maestros, roles, permisos, grupos y escuelas.

Cada cambio de Áureos desde ESP32 o Panel deberá generar un movimiento
trazable; el saldo persistente se actualiza con el movimiento y no se reconstruye desde el historial.
Flujo previsto: operación local → guardar → marcar pendiente → detectar acceso
a API → enviar → validar → confirmar → marcar sincronizado. Sin confirmación
del servidor, el registro permanece pendiente.

Los reintentos no deben duplicar movimientos (idempotencia): identificador
único estable y reconocimiento de operaciones ya recibidas. El contador actual
de la terminal es local; la identidad entre dispositivos y PWA queda pendiente.
Origen, timestamps, IDs, `synced`, orden de eventos y conflictos por operación
offline simultánea se resolverán en la fase de sincronización, sin definir
todavía protocolo o estrategia final.

### Módulos y experiencia previstos — FUTURO

Inicio/Dashboard, Alumnos, Áureos, Movimientos, Asistencia, Académico, Logros,
Dispositivos y Configuración. El perfil del alumno será el núcleo modular:
datos generales, saldo, historial, NFC, lectura, dictado, asistencia, logros y
gráficas. Dashboard mostrará alumnos, actividad, movimientos del día,
pendientes, sincronización y dispositivos.

Áureos incluirá sumar, descontar, transferir, ajustar y operaciones grupales,
siempre con movimiento. El estado de cuenta permitirá filtros por alumno,
fecha, tipo, concepto, cantidad, origen y sincronización. La UI mostrará
“Sin conexión” y cantidad pendiente; al volver acceso a API, sincronizará
automáticamente o mediante acción clara y mostrará “Sincronizando”,
“Sincronizado” y “Errores pendientes”. Sin Internet aún puede haber API en LAN.

Dispositivos podrá mostrar nombre, estado online/offline, última conexión,
firmware, almacenamiento, pendientes, NFC, microSD y última sincronización.
Lectura incluirá PPM, histórico, nivel y gráficas; dictado, palabras, errores e
histórico; asistencia, historial real cuando exista. Esos datos permanecen
separados del saldo. Logros permitirá futuras reglas, fecha, alumno, insignia y
posible recompensa; los criterios siguen pendientes.

Los detalles y límites operativos se mantienen en la referencia
[panel-master.md](../.agents/skills/banco-escolar/references/panel-master.md).

## Importación académica desde Excel

- **FUTURO:** el Panel del Maestro/servidor podrá importar archivos Excel con
  alumno, fecha, palabras por minuto, errores de escritura y otros indicadores.
- **NO previsto en la Waveshare:** el ESP32 no leerá Excel directamente.

## Operación local y sin conexión

- **IMPLEMENTADO / VALIDADO:** LittleFS monta localmente; el área de datos
  corresponde al label `spiffs`, offset `0xC90000`, tamaño `0x360000` (3,538,944
  bytes). En la medición inicial se reportaron 16,384 bytes usados y 3,522,560
  libres.
- **IMPLEMENTADO / PREPARADO:** `StorageManager` usa
  `/data/movements.ndjson` y ofrece `MovementRecord`, `appendMovement()`,
  `readMovements()`, `getMovementCount()`, `getPendingMovementCount()` y
  `clearLocalData()`.
- **IMPLEMENTADO / VALIDADO — FASES 5A.4–5A.7:** la UI registra salidas
  individuales reales; se comprobaron físicamente los movimientos ID 1, 2 y 3,
  su conteo tras reinicio y la resolución de `student_id=7` como Darío. El
  historial visible completo sigue pendiente para 5B.
- **FUTURO:** caché/cola de sincronización con el servidor y sincronización al
  recuperar la red. Los registros persistentes actuales no equivalen a una
  sincronización implementada.

El diseño requiere que terminal y Panel sigan temporalmente con operaciones
válidas sin Internet. En el ESP32 las salidas individuales se guardan localmente;
la escritura actual exige hora válida. El caso de arranque sin hora NTP queda
por resolver y no autoriza omitir esa validación.

## Exportaciones y respaldos — FUTURO

CSV y Excel se exportarán desde Panel/servidor; PDF podrá incorporarse si se
requiere. El ESP32 no leerá Excel. Se prevén respaldos de datos locales en
LittleFS, de la base central mediante backups periódicos y opcionalmente en
microSD. No hay implementación nueva de respaldos en esta fase.

## OTA y recuperación

- **FUTURO:** OTA por Wi-Fi desde Configuración: mostrar versión, buscar,
  descargar, instalar y reiniciar.
- **PREPARADO:** USB/UART seguirá siendo el método de recuperación.
- **NO IMPLEMENTADO:** OTA y servidor/API.
- **IMPLEMENTADO:** Wi-Fi (ver sección «Panel del Maestro y red»).

## Identificación NFC

El UID NFC identifica al alumno y no contiene su saldo. La lectura UID se
desarrolló anteriormente, pero el lector está actualmente desconectado y
`PN532_ENABLED=false`; el código no inicia I²C1 ni crea el dispositivo mientras
esté deshabilitado. I²C1 GPIO43/44 queda reservado para una futura conexión
separada de I²C0. El cableado, alimentación y niveles lógicos seguros siguen
pendientes. El futuro servicio de datos buscará el `Student` correspondiente.
Nunca escribir saldo o historial en la tarjeta.

## Fase 2: interfaz y navegación del alumno

- **IMPLEMENTADO / DEMO:** alumno temporal Darío (`CAMACHO ARREOLA ALONZO
  DARIO`) en el modelo `Student`, con nombre amigable para la interfaz.
- **IMPLEMENTADO:** menú del alumno con `Mi cuenta`, `Mi progreso`, `Mis metas`
  y `Logros`. `Tienda` no forma parte de esta fase.
- **IMPLEMENTADO:** Mi cuenta muestra `StudentAccount` persistente e historial real de movimientos, filtrado por `student_id`; el historial es de solo lectura.
- **IMPLEMENTADO / DEMO:** la información académica y Logros usan datos provisionales; las transferencias entre alumnos son futuras.
- **IMPLEMENTADO / DEMO:** `Mi progreso` enlaza a Fluidez lectora y Dictado de
  oraciones. Las pantallas consumen estructuras `ReadingRecord` y
  `WritingRecord`.
- **IMPLEMENTADO / DEMO:** hay 39 `ReadingRecord` con fechas de septiembre de
  2026 para los 13 alumnos. Darío muestra 82, 83 y 107 PPM con fechas
  01/09/2026, 07/09/2026 y 11/09/2026.
- **PREPARADO:** navegación mensual de progreso; por ahora solo existe
  septiembre de 2026 con datos demo.
- **IMPLEMENTADO / DEMO:** hay 52 `WritingRecord` con fecha, palabras, errores
  y estado `applied`; Dictado de Darío incluye sus registros de septiembre.
  `applied=false` representa no aplicado, no cero errores ni asistencia.
- **IMPLEMENTADO / DEMO:** `Mis metas` muestra el estándar actual y el reto
  Avanzado sin calcular puntos faltantes.
- **IMPLEMENTADO / DEMO:** `Logros` contiene únicamente un elemento visual
  marcado como demostración; no afirma un logro real.
- **IMPLEMENTADO / DEMO:** la bienvenida de entrada utiliza la hora del reloj
  interno actual; no guarda asistencia ni modifica el PN532.
- **PREPARADO:** cada futura entrada podrá contener `student_id`, fecha, hora
  de entrada y estado de asistencia, con consulta mensual desde el Panel del
  Maestro (**FUTURO**).

## Sesión del alumno

- **IMPLEMENTADO:** cierre automático por inactividad mediante temporizador
  LVGL, sin `delay()`.
- **IMPLEMENTADO:** el tiempo inicial es `SESSION_TIMEOUT_SECONDS = 60` en
  `src/app_config.h`; se puede cambiar en un único lugar.
- **IMPLEMENTADO:** las interacciones táctiles válidas de navegación y los
  botones `Volver`/`Salir` reinician el temporizador.
- **IMPLEMENTADO:** al vencer el tiempo, la interfaz regresa a `Acerca tu
  tarjeta` / pantalla de espera.

Los umbrales iniciales por grado para Fluidez lectora están **IMPLEMENTADOS**
en `src/academic_config.h` (tabla en la sección anterior). La política final de
niveles, logros y asistencia sigue **PENDIENTE DE DEFINICIÓN**. Los indicadores
académicos deben continuar independientes de LVGL y ser reemplazables por
datos del Panel del Maestro/servidor en una fase posterior.

## Configuración académica por grado

`src/academic_config.h` contiene la configuración general de Fluidez lectora
para 1.º a 6.º. Los límites iniciales son:

| Grado | Requiere apoyo | Cerca del estándar | Estándar | Avanzado |
|---|---:|---:|---:|---:|
| 1.º | < 15 | 15–34 | 35–59 | ≥ 60 |
| 2.º | < 35 | 35–59 | 60–84 | ≥ 85 |
| 3.º | < 60 | 60–84 | 85–99 | ≥ 100 |
| 4.º | < 85 | 85–99 | 100–114 | ≥ 115 |
| 5.º | < 100 | 100–114 | 115–124 | ≥ 125 |
| 6.º | < 115 | 115–124 | 125–134 | ≥ 135 |

`Student` conserva únicamente el grado asignado (`1` a `6`); no contiene una
copia de los rangos. `getReadingFluencyThresholds()` selecciona la
configuración general correspondiente y `evaluateReadingFluency()` calcula el
nivel, el siguiente umbral y las PPM faltantes. Así, cambiar el grado del
alumno entre ciclos cambia automáticamente la configuración aplicable sin
modificar la UI, las fórmulas ni los registros académicos.

Para Darío, el modelo contiene grado `4` y la última medición demo es 107 PPM:
la evaluación devuelve **Estándar**, siguiente nivel **Avanzado**, meta 115 PPM
y 8 PPM faltantes. Un alumno en Avanzado no obtiene un quinto nivel; se muestra
que alcanzó Avanzado y los retos posteriores quedarán a cargo de Logros.

La selección futura de grados y grupos activos del ciclo escolar es **FUTURO**;
no existe todavía una pantalla de configuración. La configuración general, los
datos personales de `Student` y los registros académicos permanecen separados.

## Datos DEMO de Dictado de oraciones

`WritingRecord` conserva `date`, `total_words`, `errors` y `applied`. Los datos
reales de septiembre de Darío cargados en DEMO son:

| Fecha | Palabras | Errores | Aplicó |
|---|---:|---:|---|
| 01 de septiembre | 34 | 8 | Sí |
| 07 de septiembre | 45 | 5 | Sí |
| 11 de septiembre | — | — | No |
| 21 de septiembre | 58 | 7 | Sí |

El registro del 11 de septiembre conserva `applied = false` y no se interpreta
como cero errores. `0 errores` significa una evaluación realizada sin errores;
`No aplicado` significa que el alumno no realizó la evaluación, en este caso
por inasistencia.

La pantalla de Dictado calcula el último registro aplicado y el mejor resultado
mensual usando el menor número absoluto de errores entre evaluaciones aplicadas.
La cantidad total de palabras se conserva para una futura métrica más completa
que también considere la extensión del dictado. Por ahora no se calculan
porcentajes, aciertos, niveles ni metas automáticas de escritura.

## Reloj y NTP

- **IMPLEMENTADO:** `TimeManager` sincroniza mediante NTP con los servidores
  configurados en código y aplica la zona `America/Mexico_City` mediante la
  regla POSIX actual `CST6`.
- **IMPLEMENTADO:** el splash comparte una fuente de progreso para barra, aro y
  porcentaje, dura al menos 6 segundos y espera sincronización al disponer de
  Wi-Fi, con fallback para no bloquear indefinidamente. Si NTP responde, la
  fecha/hora se actualiza antes de mostrar Inicio.
- **IMPLEMENTADO:** al alcanzar 100 % muestra “Sistema listo” y hace una pausa
  breve antes de Inicio; el flujo usa timers, no un `delay()` bloqueante.
- **HISTÓRICO:** el reloj interno y la hora demo se usaron antes de incorporar
  NTP; no representan la hora actual cuando la sincronización está pendiente.

## Fase 2B: capa de datos provisional

La implementación provisional utiliza estructuras C++ en archivos separados,
no JSON. Esta decisión evita añadir un parser o una dependencia al firmware y
mantiene bajo el consumo de recursos. La UI consulta funciones de
`src/data/provisional_data.h`; no conoce si una futura implementación obtiene
los datos desde arreglos, archivos, caché o una base de datos.

### Identidad y relación NFC

- **IMPLEMENTADO / PROVISIONAL:** existen 13 alumnos con `student_id` estable,
  del 1 al 13.
- **IMPLEMENTADO / PROVISIONAL:** `Student.nfc_uid` está vacío/no asignado en
  todos los alumnos.
- **IMPLEMENTADO / PROVISIONAL:** Darío es `student_id = 7` y permanece como
  alumno seleccionado de DEMO MODE.
- **FUTURO:** la tarjeta NFC será una asignación reemplazable que apuntará a un
  `student_id`; nunca será la identidad principal ni almacenará la cuenta.

### Registros provisionales

- **IMPLEMENTADO / PROVISIONAL:** 39 `ReadingRecord` (13 alumnos × 3 fechas)
  para septiembre de 2026.
- **IMPLEMENTADO / PROVISIONAL:** 52 `WritingRecord` (13 alumnos × 4 fechas),
  con fecha, total de palabras, errores y `applied`.
- **IMPLEMENTADO / PROVISIONAL:** 3 movimientos y una cuenta de DEMO para
  Darío, asociados mediante `student_id`.
- **PREPARADO:** `AttendanceRecord` con `student_id`, fecha, hora y estado;
  no se cargan asistencias históricas.

Los registros con `applied = false` conservan estado No aplicado y no se
interpretan como cero. En Fluidez, el 07/09/2026 de Alexia (student_id 2) es
No aplicado. En Dictado, las fechas sin aplicación se conservan sin errores
válidos. Los guiones del origen no generan registros oficiales de asistencia.

Las consultas disponibles son `getStudentById()`, `getReadingRecords()`,
`getWritingRecords()`, `getStudentMovements()`, `getAccountByStudentId()` y
`getAttendanceRecords()`. La interfaz demo de Darío consume las consultas por
ID 7. Además existe `StoredAttendanceRecord` en el modelo de StorageManager,
pero no hay historial real de asistencia ni API de almacenamiento de asistencia.

### Flujo NFC futuro

```text
Leer tarjeta → obtener UID → buscar Student.nfc_uid
→ obtener student_id → cargar cuenta, progreso, metas y logros
→ registrar entrada
```

Este flujo está documentado, pero la vinculación UID ↔ alumno todavía no está
implementada y el PN532 permanece deshabilitado. Wi-Fi, NTP y almacenamiento
local LittleFS sí están implementados; servidor, API, SQL, sincronización y
OTA siguen pendientes.

## StorageManager y movimientos locales

LittleFS monta con formato automático deshabilitado. En modo normal los flags
son `STORAGE_ALLOW_ONE_TIME_FORMAT=false` y `STORAGE_SELF_TEST=false`; ante un
fallo de montaje no debe formatear ni borrar datos automáticamente. La
validación física registró dos arranques consecutivos con montaje correcto.

El archivo `/data/movements.ndjson` usa NDJSON, `schema_version=1`; `next_mv_id` se reserva en NVS y los IDs no se reutilizan. `clearLocalData()` borra solo el archivo de movimientos conocido y conserva NVS y el contador. LittleFS mantiene `/data/accounts.ndjson` y `/data/pending_transaction.json` para cuenta y recuperación transaccional.

La sincronización y el marcado durable como sincronizado siguen pendientes; los registros físicos actuales conservan `synced=false`. La UI resuelve el nombre desde `student_id`; `MovementRecord` no almacena el nombre. El saldo visible procede de `StudentAccount` y no se reconstruye desde el historial.
## microSD

La pantalla de Configuración y `SDManager` son placeholders. El usuario reporta una microSD de 64 GB instalada, pero el firmware no accede a GPIO ni realiza I/O; por eso no la detecta ni monta. Se decidió omitir microSD de los próximos pasos inmediatos.
## Rendimiento de Inicio

Inicio presentaba stutter. La medición previa informó promedio ~112.4 ms y
máximo ~136.5 ms. Se identificaron como causa principal los pulsos continuos de
opacidad del punto de espera y del borde NFC, que se eliminaron. La medición
posterior informó promedio ~46.4 ms, máximo ~56.5 ms y cero intervalos >100 ms,
una mejora promedio aproximada de 58.7 %. El P95 disponible es aproximado por
categorías, no una medida exacta. Los cuatro tests automáticos permanecen
desactivados en firmware normal.

## Navegación actual

La navegación normal es instantánea: pantalla nueva opaca en (0,0), sin fade,
slide, zoom ni animación de opacidad a pantalla completa; la pantalla anterior
se elimina con `lv_obj_del_async()`. El feedback local del botón dura unos
75 ms. El fade de 220 ms del splash al entrar a Inicio es independiente de la
navegación normal.

## Roadmap estable

**COMPLETADO:** interfaz base y datos provisionales; Wi-Fi; NTP y splash;
LittleFS validado y base de StorageManager; UI de almacenamiento; optimización
de rendimiento; Skill `banco-escolar`.

| Fase | Alcance | Estado |
|---|---|---|
| 0S.1 | Skill creada | COMPLETADA |
| 0S.2 | Documentación base | COMPLETADA |
| 0S.2B | Arquitectura PWA/offline-first | COMPLETADA |
| 0S.3 | Git estable y commit local | COMPLETADA |
| 5A.4 | Persistencia real de Áureos | COMPLETADA |
| 5A.5 | Validación de persistencia y reinicios | COMPLETADA |
| 5A.6 | Continuidad secuencial de IDs | COMPLETADA |
| 5A.7 | Vínculo del movimiento con alumno mediante `student_id` | COMPLETADA |
| 5B.1 | Historial real de movimientos desde LittleFS | COMPLETADA; validada físicamente |
| 5B.1C | Rediseño coordinado de Inicio, Mi cuenta e Historial | COMPLETADA; validada físicamente |
| 5B.1D | Encabezado común y acción Salir | COMPLETADA; validada físicamente |
| 5B.1E | Logo estático en el splash | COMPLETADA; validada físicamente |
| 5B.1F | Logo compacto en el encabezado | COMPLETADA; validada físicamente |
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

Las fases 5A.4–5A.7, 5B.1 y 5B.1C–5B.1F quedaron completadas y validadas físicamente.
5C/5C.1 validaron saldo persistente y salidas; 5D validó físicamente una entrada +10 que llevó el saldo de 120 a 130, movimiento ID 5, cinco movimientos tras reinicio sin duplicación.
5E Menú Maestro fue reportada validada físicamente. 5E.1 implementa ActivityDraft en RAM y 5E.2 persiste ActivitySession en LittleFS; aún no crea participaciones, movimientos ni recompensas. 5E.1A está implementada en código con prueba física pendiente. 5E.1B compiló y espera validación física visual en temas claro y oscuro.
El saldo monetario persistente ya está implementado localmente; sincronización, transferencias entre alumnos y Panel siguen pendientes.
Todos los componentes de
Panel/API/base central y sincronización aquí descritos son futuros: no se crean
backend, endpoints, esquema de base de datos, manifest, Service Worker,
IndexedDB ni pantallas en esta fase documental.



## Estado de actividades — Fase 5E.2

ActivitySession ya está implementada en firmware como modelo independiente de LVGL y se persiste en `/data/activities.ndjson` mediante snapshots append-only. El menú permite iniciar sesiones independientes y consultar las guardadas después de reiniciar. Los IDs usan NVS `next_act_id`, separado de `next_mv_id`. Las sesiones guardan el snapshot de participantes y la fecha de inicio; la duración se calcula contra hora válida, sin escritura por segundo. Sin hora válida, el tiempo queda pendiente de verificar. El vencimiento no cierra la sesión automáticamente; queda por decidir en 5E.3. ActivityClaim, NFC y movimientos/saldos por actividad siguen fuera de alcance. Pruebas lógicas/BUILD y validación física deben documentarse con sus resultados reales al cerrar esta fase.

## Fase 5E.3 — Consulta y gestión de actividades

La consulta existente del Menú Maestro filtra actividades activas, finalizadas y canceladas, ordenadas por ID descendente. El detalle permite finalizar o cancelar mediante confirmación; cada cambio guarda un snapshot append-only con hora válida. El vencimiento se calcula en tiempo de consulta: la sesión permanece ACTIVE con la leyenda “Tiempo agotado” y no hay cobros. Las horas no verificables se identifican como tales. La elegibilidad temporal está centralizada en el dominio. La edición se ofrece solo para sesiones ACTIVE y actualmente usa la ausencia de historial de claims; 5E.4 debe sustituirla por la consulta real de ActivityClaim, incluyendo claims anulados. No se generan cobros, claims ni cambios monetarios.

BUILD PlatformIO (06/10/2026): SUCCESS. RAM 101,076/327,680 bytes (30.8%); Flash 1,929,336/6,553,600 bytes (29.4%). Sin warnings de compilador observados. `git diff --check` pasó. Las pruebas de filtros/orden, límites temporales, reinicio/apagado, hora inválida, recuperación de snapshots y fallos de guardado requieren validación física o pruebas de dominio dedicadas; ninguna prueba física se infiere del BUILD. No Upload, Commit ni Push.
\r\n
\r\n

## Phase 5E.4 - Activity rewards

Activity rewards extend the pending account journal to schema 2 while preserving schema 1 entry/exit recovery. A v2 journal captures both reserved IDs, the original timestamp, movement, claim, and balance delta. Recovery completes movement, account snapshot, and claim in order, verifies each exact record, and removes the journal only after all three exist. Damaged or ambiguous state preserves the journal and blocks further Storage operations. Existing claims block ordinary repeat rewards; an explicitly authorized re-claim is future work.
