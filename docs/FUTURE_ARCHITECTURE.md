# Banco-Escolar: arquitectura futura

Este documento registra las decisiones de diseño de la Fase 1. Se mantienen
separadas las funciones ya operativas de las capas que se implementarán más
adelante. En esta fase no se agregan Wi-Fi, servidor, base de datos, Excel,
almacenamiento persistente ni OTA.

## Estado de la Fase 1

- **IMPLEMENTADO:** Waveshare ESP32-S3-Touch-LCD-7 a 800x480 con ST7262,
  GT911, CH422G y LVGL 8.4.0.
- **IMPLEMENTADO:** fuente española, fecha/hora de demo, navegación y
  animaciones actuales.
- **IMPLEMENTADO:** PN532 por segundo bus I²C en GPIO43/GPIO44; se lee solo el
  UID y no se escribe en la tarjeta.
- **IMPLEMENTADO:** `DEMO_MODE` con un alumno de prueba.
- **PREPARADO:** modelo `Student`, movimientos y actualización centralizada del
  saldo.
- **PREPARADO:** nombre de moneda configurable en `src/app_config.h` mediante
  `CURRENCY_NAME`.
- **PENDIENTE DE DEFINICIÓN:** reglas del nivel del alumno; actualmente
  `Student.level` solo conserva el valor visual temporal de DEMO MODE.

## Modelo de dominio

`src/student_model.h` define `Student` con identidad, UID, nombre, grado,
grupo, número de lista, referencia opcional a avatar, Áureos y nivel temporal.
Los campos de avatar/foto y número de lista solo preparan el modelo: no hay
archivos, SD, servidor ni almacenamiento implementados.

El saldo se modifica mediante `setStudentBalance()` y la UI se refresca desde
`updateBalanceUI()`. El valor actual vive en RAM como dato demo.

`StudentMovement` contempla alumno, cantidad, tipo entrada/salida, motivo,
fecha/hora y origen (`Banco Escolar` o `Panel del Maestro`). No existe todavía
historial persistente.

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
Data Service (futuro)
   ├── caché local temporal (futuro)
   └── servidor local / API / base de datos (futuro)
```

## Escuela multigrado

La arquitectura acepta cualquier combinación de grados y grupos. Los valores
`4° B` del alumno demo son solo datos de prueba, no una lista cerrada. En una
fase posterior el administrador podrá definir combinaciones como `3° A`, `3° B`,
`4° A`, `4° B` u otras, mediante configuración inicial (**FUTURO**).

La primera versión contempla un solo administrador (**PREPARADO como decisión
de alcance**). No se implementan todavía múltiples maestros, roles ni permisos.

## Panel del Maestro y red

- **FUTURO:** Panel del Maestro para administrar alumnos, Áureos, progreso,
  logros y movimientos.
- **FUTURO:** servidor local y API dentro de la red local:
  `PC del Maestro ↔ servidor local ↔ Banco Escolar`.
- **FUTURO:** migración posterior del servidor local a un servidor web/Internet.

La UI no debe contener IPs, URLs ni detalles del servidor. La comunicación debe
pertenecer a un servicio de datos independiente.

## Importación académica desde Excel

- **FUTURO:** el Panel del Maestro/servidor podrá importar archivos Excel con
  alumno, fecha, palabras por minuto, errores de escritura y otros indicadores.
- **NO previsto en la Waveshare:** el ESP32 no leerá Excel directamente.

## Operación sin conexión

- **FUTURO:** con red local disponible, la terminal sincronizará con el servidor.
- **FUTURO:** sin red, una caché local podrá conservar temporalmente datos y
  movimientos pendientes.
- **FUTURO:** al regresar la red, el servicio sincronizará la cola pendiente.

No se implementa aún persistencia ni sincronización.

## OTA y recuperación

- **FUTURO:** OTA por Wi-Fi desde Configuración: mostrar versión, buscar,
  descargar, instalar y reiniciar.
- **PREPARADO:** USB/UART seguirá siendo el método de recuperación.
- **NO IMPLEMENTADO:** OTA, Wi-Fi y servidor.

## Identificación NFC

El UID NFC identifica al alumno y no contiene su saldo. El futuro servicio de
datos buscará el `Student` correspondiente y devolverá la cuenta y el resto de
la información del sistema. La configuración actual del PN532, GPIO, I²C,
ST7262, GT911 y CH422G queda fuera de esta arquitectura y no se modifica.

## Fase 2: interfaz y navegación del alumno

- **IMPLEMENTADO / DEMO:** alumno temporal Darío (`CAMACHO ARREOLA ALONZO
  DARIO`) en el modelo `Student`, con nombre amigable para la interfaz.
- **IMPLEMENTADO:** menú del alumno con `Mi cuenta`, `Mi progreso`, `Mis metas`
  y `Logros`. `Tienda` no forma parte de esta fase.
- **IMPLEMENTADO / DEMO:** `Mi cuenta` muestra saldo, `CURRENCY_NAME` y tres
  movimientos temporales del día. No hay historial persistente.
- **IMPLEMENTADO / DEMO:** `Mi progreso` enlaza a Fluidez lectora y Dictado de
  oraciones. Las pantallas consumen estructuras `ReadingRecord` y
  `WritingRecord`.
- **IMPLEMENTADO / DEMO:** Fluidez lectora muestra 82, 83 y 107 PPM para
  septiembre. Las fechas de esas mediciones no están presentes en los datos
  actuales, por lo que se muestran como mediciones numeradas y no se inventan
  fechas.
- **PREPARADO:** navegación mensual de progreso; por ahora solo existe
  septiembre de 2026 con datos demo.
- **PENDIENTE DE DATOS:** los valores reales de Dictado de oraciones de Darío
  no están en el código ni en la documentación disponible. La pantalla queda
  preparada y distingue conceptualmente `0 errores` de `-` (no aplicado), sin
  inventar registros.
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

Las reglas definitivas de niveles, rangos de Fluidez lectora, logros y
asistencia siguen **PENDIENTES DE DEFINICIÓN**. Los indicadores académicos
deben continuar independientes de LVGL y ser reemplazables por datos del
Panel del Maestro/servidor en una fase posterior.

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

## Reloj provisional

- **IMPLEMENTADO:** todas las pantallas consumen el mismo estado interno de
  fecha y hora.
- **IMPLEMENTADO / DEMO:** la compilación actual inicia provisionalmente en
  `23/09/2026 11:39:56` y muestra horas con segundos.
- **IMPLEMENTADO:** el avance se realiza mediante el temporizador LVGL, sin
  bloqueo ni `delay(1000)`, incluyendo los cambios de minuto, hora, día, mes y
  año.
- **PREPARADO:** la zona horaria futura es `America/Mexico_City` y existe una
  interfaz reservada para sincronización NTP.
- **FUTURO:** Wi-Fi/NTP establecerá la hora real al arrancar; la hora demo no
  debe interpretarse como sincronización de Internet.

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
`getAttendanceRecords()`. La interfaz de Darío consume las consultas por el
ID 7, por lo que cambiar de alumno no requiere reconstruir las pantallas.

### Flujo NFC futuro

```text
Leer tarjeta → obtener UID → buscar Student.nfc_uid
→ obtener student_id → cargar cuenta, progreso, metas y logros
→ registrar entrada
```

Este flujo está documentado, pero la vinculación UID ↔ alumno todavía no está
implementada. La capa provisional tampoco implementa Wi-Fi, servidor, API,
SQL, sincronización, OTA ni almacenamiento persistente.
