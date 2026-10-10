# PM.10A — Vinculación entre Panel Maestro y terminal

**Estado:** diseño de arquitectura más una prueba inicial implementada de archivos HTTP estáticos desde SD. La API escolar, autenticación, sincronización, PIN y Panel real siguen sin implementar. La optimización de capturas USB queda fuera de esta fase.

## Estado comprobado y límites

- El Panel en `panel-maestro/` usa `DemoPanelDataService` y persistencia IndexedDB de espacio demo (PM.9B/9C). Sus datos y respaldos son ficticios; `PanelDataService` y los tipos `models/domain.ts` son interfaces del cliente, no contrato de servidor. No hay backend, autenticación, Service Worker ni sincronización.
- Firmware actual corre en ESP32-S3. `StorageManager` conserva cuentas y movimientos en LittleFS, con journal local `/data/pending_transaction.json`; movimientos tienen ID local, `student_id`, `synced=false` y esquema actual. NVS reserva `next_mv_id`, que no es identidad global. No existe cliente de sincronización ni cola de red con `operation_id`/ACK. `src/data/provisional_data.*` y NFC siguen siendo provisionales; PN532 está deshabilitado.
- La detección/montaje/listado de raíz se validó antes físicamente en modo de solo lectura. La API local FAT permite operaciones genéricas; su ruta de escritura aún no se valida físicamente. El firmware ahora implementa un servidor HTTP de prueba solo lectura para archivos de `panel-test/` y `GET /status`, con transmisión por bloques de 1 KiB. Este servidor aún no se ha validado físicamente. Se preparó un paquete estático local del Panel para `panel-test/maestro/`, pero todavía no se ha copiado a una tarjeta ni probado desde el firmware. No aloja la API escolar ni escribe/instala actualizaciones. Servir el paquete real desde la SD requiere primero prepararlo y validar físicamente sus recursos.
- Los datos demo persistentes del Panel y los datos de LittleFS son almacenes independientes y no deben mezclarse ni tratarse como registros centrales.

### Prueba HTTP de archivos desde microSD

Ya existe un servidor local de archivos de prueba (puerto 80), implementado con `WebServer` del core Arduino. Atiende desde el ciclo normal sin utilizar entradas táctiles. `GET /` mapea a `panel-test/index.html`; `GET /status` expone `sd_mounted` y `page_version`, leída de `panel-test/version.txt`; CSS, JavaScript e imágenes se sirven solo desde `panel-test/`. Archivos faltantes responden 404, rutas inválidas 400, SD desmontada 503 y métodos distintos de GET/HEAD 405. No incluye operaciones HTTP de escritura.

Las lecturas usan un buffer fijo de 1 KiB por bloque (`SDManager::streamFile`), sin invocar las funciones SD que recuperan temporales/backups y pueden modificar FAT. Los ejemplos de archivos preparados para copiar manualmente a una tarjeta están en `tools/web/sample_sd/panel-test/`. Ni la tarjeta ni la red se validaron físicamente con esta función aún. El HTTP de prueba no tiene autenticación/TLS, no sirve el build real del Panel y no implementa API de datos o actualización.

## Arquitectura acordada de primera versión

En las primeras versiones habrá una terminal ESP32-S3. La propia terminal será el centro local: alojará la API y servirá los archivos estáticos del Panel Maestro desde microSD. El maestro, desde otro equipo conectado al mismo router Wi-Fi, abrirá la dirección local de la terminal en un navegador. El Panel y la API funcionarán dentro de la LAN sin Internet. La terminal sigue siendo fuente primaria de alumnos, IDs y operaciones aplicadas.

```text
Maestro / navegador
        │ Wi-Fi local (mismo router; Internet no requerido)
        ▼
Router Wi-Fi ───── Internet opcional ───── Nube (backup/acceso remoto)
        │                                      ▲
        ▼                                      │ replica al reconectar
ESP32-S3 (API local + servidor de archivos del Panel)
        │
        ├── LittleFS: datos/operaciones de la terminal y cola de sync
        └── microSD: archivos versionados del Panel (propuesta futura)
```

El camino preferido del navegador será HTTPS en red local, con certificado/distribución de confianza definidos en implementación. Si el navegador no puede confiar en el origen local, resolverlo como decisión de despliegue segura; no degradar a mandar credenciales o PIN por HTTP abierto. El equipo del maestro no necesita abrir Internet para acceder al Panel local.

La nube es opcional para operar en la escuela. Cuando Internet esté disponible, la terminal sincronizará backups cifrados y permitirá acceso remoto según permisos; una caída de Internet no detiene API local, Panel ni operaciones de terminal. La primera versión tiene una terminal escritora y evita competencia entre dispositivos por cuenta.

Al crecer a más terminales, el coordinador local valida y ordena operaciones concurrentes en transacciones por cuenta. Una opción inicial es mantener una ESP32-S3 designada como coordinadora; si el volumen, conexiones, memoria o necesidades de base transaccional exceden sus límites, migrar API/coordinador a una computadora o mini PC local conectada al router. La computadora ejecutaría una base transaccional y conservaría la misma API; las ESP32 quedarían como terminales cliente y continuarían con cola local durante cortes de Wi-Fi. La decisión de migración debe basarse en mediciones de carga, capacidad SD y disponibilidad, no solo en el número de terminales.

## Archivos del Panel y actualización por Wi-Fi (diseño futuro)

El ESP32 servirá archivos estáticos exportados del Panel desde la microSD. La actualización del Panel será un flujo separado: el maestro autorizado sube un paquete nuevo desde una computadora conectada a la LAN; la terminal verifica y lo instala en la microSD. El firmware de la ESP32 no forma parte del paquete y se seguirá cargando por separado desde una computadora mediante el proceso firmware autorizado. Actualizar la web no flashea, cambia ni reinicia deliberadamente el firmware.

Requisitos antes de permitir actualizaciones:

1. Autenticar al maestro y exigir reautenticación/permiso de administración para iniciar instalación. Usar sesión breve, protección CSRF, límites de tamaño y transporte cifrado; no exponer una ruta de escritura anónima en la red local.
2. Aceptar solo un paquete versionado con manifiesto (versión de Panel, versión mínima de API, lista de archivos, tamaños y hashes). Verificar firma del publicador y hashes antes de activar; el checksum por sí solo detecta corrupción pero no un paquete malicioso.
3. Escribir los archivos en una carpeta temporal/versionada distinta de la versión activa. Rechazar rutas absolutas, `..`, nombres duplicados, tipos inesperados, exceso de archivos/tamaño y contenido fuera de la raíz aprobada.
4. Tras escribir, releer y verificar todos los archivos desde SD. Activar con un marcador/manifiesto de versión escrito de forma atómica y durable; conservar intacta al menos la versión anterior.
5. Si se corta energía, falla escritura/verificación o la nueva UI no inicia, servir automáticamente la última versión marcada válida y dejar el paquete fallido aislado para diagnóstico. No borrar la versión anterior hasta confirmar arranque saludable y política de retención.
6. Mostrar versión activa, versión disponible, resultado, progreso y recuperación. Registrar actor/fecha/resultado sin guardar credenciales ni PIN. Tener procedimiento para recuperación manual si la microSD falla.

Esta función no está implementada. Aunque ya existe escritura genérica local FAT con temporal, respaldo y recuperación por ruta, aún faltan permisos de red, límites de desgaste/capacidad, política de actualización versionada y validación física ante interrupción de energía. Tampoco es el flujo de actualización del firmware.

## Autoridad de datos

| Datos | Autoridad primaria | Flujo del Panel y modo offline |
|---|---|---|
| Alumnos e identidad | Padron de terminal; se conserva el `student_id` existente. | Servidor LAN conserva replica coordinadora y Panel la consulta. Nube recibe backup cuando hay Internet. IDs ficticios de IndexedDB demo no se migran ni confunden con reales. |
| NFC | Asociacion local de terminal; UID reemplazable, no clave primaria ni saldo. | Ambos accesos, tarjeta y usuario/PIN, resuelven el mismo `student_id`. |
| Cuentas, movimientos y saldo | Operaciones aplicadas/persistidas por terminal; movimiento append-only y saldo juntos en transaccion local. | Servidor LAN coordina y recibe eventos confirmados; Panel presenta su proyeccion y frescura. Nube respalda cuando hay Internet. |
| Reglas | Configuracion vigente persistida por terminal y versionada. | Panel/servicio proponen comandos; terminal valida y confirma antes de mostrarlos vigentes. |
| PIN | Version activa aplicada en terminal; servicio conserva copia cifrada recuperable y comandos cifrados pendientes. | Maestro puede consultar/cambiar con controles siguientes. Nuevo PIN no esta activo hasta aplicacion y ACK terminal. Demo IndexedDB no guarda PIN. |

Panel muestra estado de sync y frescura por terminal. Demo IndexedDB permanece aislada; sus IDs ficticios no se importan ni reasignan al padron real.

## Identidad, versiones y contrato

Conservar sin cambios el ID oficial de alumno existente en terminal. No sustituirlo por UUID ni emparejarlo automaticamente con IDs demo. Conservar tambien IDs locales de movimiento; para transporte/deduplicacion usar `operation_id` UUID estable y registrar `(device_id, local_movement_id)`. UID NFC es asociacion protegida, nunca clave primaria. Ambos metodos de acceso resuelven el mismo `student_id`.

Cada entidad tiene `schema_version`; padron/configuracion tienen `revision` monotona asignada por terminal al aplicar cambios. Operaciones/comandos llevan `operation_id` creado una sola vez, `device_id`, `student_id` cuando aplique, tipo/payload, `base_revision`, secuencia por dispositivo y tiempo opcional. Timestamps no determinan autoridad. Central conserva `received_at` y ACK.

API propuesta HTTPS JSON `/api/v1`, no implementada:

| Ruta | Funcion |
|---|---|
| `POST /auth/sessions` | Autenticacion de maestro; reautenticacion para consultar/cambiar PIN. |
| `POST /devices/enroll` | Alta aprobada y revocable de terminal con permisos minimos. |
| `GET /sync/terminal/{device_id}/commands?cursor=...` | Terminal obtiene comandos pendientes del Panel, ordenados por secuencia/version. |
| `POST /sync/terminal/{device_id}/events` | Terminal envia operaciones y confirmaciones; ACK durable por ID. |
| `GET /sync/panel/{device_id}/snapshot?cursor=...` | Panel lee replica confirmada, comandos y frescura. |
| `POST /students/{id}/pin/commands` | Maestro crea cambio de PIN con estado `PENDING_TERMINAL`. |
| `POST /students/{id}/pin/reveal` | Descifrado autorizado y auditado, sin cache/log. |

Evento monetario: `{schema_version, operation_id, device_id, local_movement_id, student_id, kind, amount_signed, reason, local_sequence, created_at?}`. Terminal persiste movimiento y saldo juntos; ACK central confirma recepcion durable, no cambia la autoridad primaria.

Comando PIN: `{schema_version, command_id, student_id, target_pin_version, expected_pin_version, encrypted_pin_for_device, created_by, created_at}`. Nunca contiene PIN en texto claro almacenado. Terminal acepta solo version superior y `expected_pin_version` coincidente, persiste verificador/version y confirma `APPLIED` con version activa. Estados: `PENDING_TERMINAL`, `APPLIED`, `REJECTED_STALE`, `REJECTED_INVALID`, `DELIVERY_FAILED`. Misma operacion/ID repetida devuelve resultado anterior; ID igual con payload distinto se rechaza/audita. Panel marca PIN nuevo pendiente y no activo hasta ACK terminal. Version monotona y `expected_pin_version` bloquean reactivacion atrasada del PIN viejo. ACK perdido se reintenta sin revertir version activa.

## Operacion offline, saldos y concurrencia

1. Terminal crea `operation_id` una vez y persiste operacion, movimiento/saldo via journal y estado `PENDING` antes de informar exito. La cola local append-only guarda payload, hash, `device_id`, `local_movement_id`, `student_id`, secuencia y estado. Si pierde Wi-Fi, nada se descarta ni se vuelve a crear; la terminal puede seguir operando offline con sus datos/reglas locales.
2. Al reconectar a la LAN, la terminal envia pendientes en orden de secuencia al servidor local. Reintenta con el mismo `operation_id` y payload (backoff con jitter). Si no recibe ACK, mantiene pendiente y reenvia, incluso tras reinicio.
3. Servidor LAN procesa cada evento en transaccion: unicidad por `(school_id, operation_id)` y referencia `(device_id, local_movement_id)`. Primer envio valida y agrega movimiento una sola vez; reintento identico devuelve el ACK/resultado ya guardado. Mismo ID con payload/hash diferente se rechaza y queda en conflicto auditado. El ACK solo se emite despues del commit durable.
4. Terminal al recibir ACK verifica el ID y hash, marca esa operacion `ACKED` en LittleFS y avanza cursor; si se pierde la respuesta, vuelve a enviar y recibe el mismo ACK. Panel muestra solo eventos confirmados por LAN y conserva el estado de frescura. El servidor local replica hacia nube cuando hay Internet; caida de nube no bloquea LAN.
5. El saldo se actualiza localmente junto con el movimiento mediante el journal existente; servidor mantiene una proyeccion derivada de eventos terminales y aplica cada `operation_id` una sola vez. No se sincronizan snapshots de saldo como escritura autoritativa ni se sobrescribe una cuenta por una version atrasada. Si evento esperado falta o hay secuencia/hash inconsistente, se detiene esa cadena, se conserva la cola y se requiere reconciliacion; no se inventa saldo.
6. En primera version hay una terminal escritora. Al agregar terminales, el coordinador local valida, ordena y aplica operaciones concurrentes transaccionalmente. Para gasto offline sobre la misma cuenta, asignar escritor por cuenta o reservas de gasto no superpuestas; si una operacion local no pasa la validacion final de fondos, queda visible para conciliacion, nunca reemplaza saldo ni desaparece. Los comandos del Panel (incluido PIN) conservan IDs/versiones y quedan en cola hasta ACK de aplicacion terminal.
7. Correcciones/anulaciones generan evento compensatorio enlazado. Estados/versiones PIN nunca retroceden.

## PIN de cinco cifras, consulta y acceso offline

El alumno usa tarjeta NFC o usuario + PIN numerico de cinco cifras; ambos resuelven el mismo `student_id` terminal. Maestro puede consultar PIN vigente y solicitar otro desde Panel. Nunca guardar PIN en texto normal en IndexedDB, LittleFS, logs, auditoria, backup o base de datos.

**Consulta segura:** servicio central guarda PIN recuperable cifrado AES-256-GCM con claves envueltas por KMS/HSM no exportable y separadas de la base. Consulta requiere cuenta individual, reautenticacion fuerte reciente (p. ej. segundo factor), permiso, motivo y confirmacion. Auditoria inmutable registra maestro, alumno, fecha, contexto y resultado, nunca PIN/respuesta. Entrega por TLS sin cache, historial ni analytics; limpiar memoria al cerrar. Una clave/servicio comprometido podria exponer varios PIN: limitar consultas, alertar patrones y rotar/revocar claves.

**Cambio Panel a terminal:** Panel cifra el PIN nuevo para terminal enrolada y crea comando con version esperada/objetivo. Central conserva ciphertext como `PENDING_TERMINAL`; Panel muestra pendiente y PIN previo vigente. Terminal valida orden/version, deriva verificador local, persiste verificador+version y limpia PIN de memoria; luego confirma `APPLIED`. Solo tras ACK Panel muestra activo. Sin conexion se conserva PIN previo. Comando idempotente, `expected_pin_version`, version monotona y tombstone evitan reactivar version vieja. Si ACK se pierde despues de aplicar, reintento devuelve version activa sin revertirla.

**Verificacion offline:** almacenamiento protegido local conserva solo verificador derivado por dispositivo/alumno, `student_id`, version y politica; nunca PIN ni ciphertext recuperable. Elegir algoritmo/coste tras prueba de rendimiento y amenaza. Cinco digitos solo dan 100,000 combinaciones; verificador extraido admite fuerza bruta. Mitigar con secreto protegido, proteccion fisica, roster local minimo, contador persistente y sincronizacion de revocaciones. Online valida contra servicio; offline contra verificador vigente precargado. Cambio Panel no aplica hasta comando confirmado.

**Intentos:** cinco fallos consecutivos por alumno bloquean PIN 15 minutos y luego espera progresiva. Contador/bloqueo persiste al reinicio; sumar limitador por terminal sin que un alumno bloquee a otros. Maestro desbloquea con accion auditada. Sin reloj fiable, conservar bloqueo y requerir hora confiable o intervencion autorizada. NFC cacheada puede usarse offline si asociacion sigue vigente. PIN offline solo para identidad/verificador precargados, desbloqueados y sin revocacion conocida. Mensajes no revelan si usuario o PIN fue incorrecto.

Terminal tiene permisos minimos, nunca sesion maestra ni permiso de revelar PIN. Maestro tiene cuenta individual, rol, reautenticacion y auditoria.

## Propuesta visual: inicio de terminal (solo diseño)

Diseñar para niños de primaria, en la pantalla 800×480 ya existente, texto breve y controles táctiles amplios. Mantener estilo Banco Escolar, ilustraciones simples y buen contraste en temas claro/oscuro. Dos tarjetas de igual importancia y siempre visibles:

```text
┌──────────────────────────────────────────────────────────────────┐
│ Banco Escolar                               ● Conectado / Offline │
│                                                                  │
│                    ¡Hola! ¿Cómo quieres entrar?                  │
│                                                                  │
│  ┌──────────────────────────┐   ┌─────────────────────────────┐  │
│  │       [dibujo tarjeta]   │   │       [teclado grande]      │  │
│  │       ACERCA TU TARJETA  │   │       ESCRIBE TUS DATOS     │  │
│  │     Ponla sobre el lector│   │      Usuario + PIN de 5     │  │
│  │                          │   │      cifras                 │  │
│  └──────────────────────────┘   └─────────────────────────────┘  │
│                                                                  │
│              Necesitas ayuda? Llama a tu maestro                 │
└──────────────────────────────────────────────────────────────────┘
```

| Estado | Presentación y acción |
|---|---|
| Espera de tarjeta | Resaltar suavemente el área del lector; texto “Acerca tu tarjeta”. No mostrar UID. Permitir elegir usuario/PIN en todo momento. |
| Lectura | Animación breve y texto “Leyendo tu tarjeta… déjala un momento”. Evitar doble lectura mientras se resuelve. Si lector no disponible, explicar y ofrecer acceso por usuario/PIN. |
| Captura PIN | Vista clara con usuario, cinco círculos vacíos/llenos, teclado 0–9 amplio, borrar y aceptar; dígitos siempre ocultos. “Escribe tu PIN de 5 cifras”. Accesibilidad para toque repetido y confirmación. |
| Error | Mensaje amable y concreto (“No pudimos reconocer esos datos. Revisa e inténtalo de nuevo”), sin revelar si falló usuario o PIN; limpia PIN, conserva alternativa tarjeta, presenta intentos/bloqueo sin exponer datos. No avergonzar ni mostrar saldos/nombres de otros alumnos. |
| Sin conexión | Indicador visible “Sin conexión · acceso local”. Tarjeta y PIN solo si identidad/verificador ya está en caché y dentro de política; informar “Los cambios se guardarán y se enviarán después”. Si caché no está vigente o PIN requiere servidor, explicarlo y ofrecer tarjeta cacheada/ayuda del maestro. Nunca fingir que sincronizó. |

Tras éxito, resolver identidad y abrir la experiencia del mismo `student_id` sin importar el método. Esta propuesta no altera UI ni activa hardware NFC.

## Riesgos y decisiones antes de código

- El acceso desde la LAN requiere proteger el origen local, enrolamiento, sesiones y credenciales frente a otros equipos conectados al router; HTTPS/certificados locales y aislamiento de red necesitan diseño.
- Definir protección/cifrado de datos locales, roles, retención, backups fuera de escuela y simulacros de recuperación si se pierde terminal, servidor o microSD.
- Verificar capacidad, rendimiento y desgaste de microSD para lectura web y escrituras de paquetes; definir límites, espacio libre mínimo, sustitución y mantenimiento.
- Un corte de energía durante escritura/activación del Panel puede dejar paquete incompleto; staging, marcador durable, rollback y recuperación ante FAT corrupto requieren pruebas antes de habilitar instalación.
- La nube es opcional y depende de Internet para copia/acceso remoto; establecer frecuencia/reintentos de backup, retención y qué pasa si hay conflicto entre réplica y terminal fuente.
- Definir concurrencia y particion si multiples terminales modifican la misma cuenta offline.
- Conservar student_id oficial de terminal y definir solo su formato de transporte; IDs ficticios de demo quedan fuera.
- Elegir KMS/HSM, reautenticacion/MFA, retencion de auditoria y algoritmo/coste del verificador offline; consulta recuperable y cambio Panel->terminal son requisitos acordados.
- Definir política de hora sin NTP para movimientos y expiraciones; actualmente el almacenamiento monetario requiere hora válida.
- Elegir framework API, base de datos, autenticación, formato de delta, compatibilidad/esquemas, límites de almacenamiento local y soporte de cifrado hardware tras una fase de viabilidad.

## Fases siguientes sugeridas

1. PM.10B: validar viabilidad/rendimiento de ESP32 como API/servidor, seleccionar tarjeta/espacio/estrategia SD, definir seguridad LAN y recuperacion, elegir destino de replica cloud, resolver concurrencia multi-terminal y cerrar KMS/MFA/verificador offline.
2. PM.10C: especificación OpenAPI/JSON Schema y prototipo de API sin datos reales; pruebas de idempotencia, revisión optimista y autorización.
3. PM.11: migración del modelo/almacenamiento firmware y cliente de dispositivo; BUILD antes de cualquier autorización de carga.
4. PM.12: servicio Panel real, autenticacion maestro, demo aislada sin migrar IDs y cola IndexedDB/Service Worker con estado de comandos PIN.
5. PM.13: sincronización y reconciliación, pruebas de reinicio, cortes, duplicados, carreras y recuperación de backup en entorno ficticio.
6. Fase NFC autorizada independiente: confirmar cableado/alimentación antes de habilitar PN532 y verificar la pantalla de acceso físicamente.

## Criterios de aceptación de PM.10A

- [x] Compara LAN y nube y recomienda ubicación considerando disponibilidad, respaldo e Internet.
- [x] Declara terminal como fuente primaria, IDs terminales preservados y demo IndexedDB aislada.
- [x] Define propuesta de IDs, versiones, API, ACK durable, reintentos e idempotencia.
- [x] Explica colas offline, cambios simultáneos y prevención de sobrescritura/doble movimiento.
- [x] Define autenticación/permisos de maestro y terminal sin credenciales reales.
- [x] Define consulta recuperable auditada, cambio pendiente hasta ACK terminal, verificador offline e intentos limitados.
- [x] Requiere NFC y usuario+PIN de cinco cifras con el mismo `student_id`.
- [x] Incluye diseño visual y estados infantiles de inicio, sin cambiar interfaz ni firmware.
- [ ] Revisión de privacidad y decisiones de PM.10B completadas antes de implementar.

## Comprobación documental

La arquitectura de API, sincronización, inicio de sesión y Panel real sigue siendo propuesta. El build estático del Panel para `/panel-test/maestro/` se prepara localmente con `npm run build:sd` y `tools/web/prepare_panel_sd_package.py`; el paquete y `manifest.json` incluyen los archivos emitidos, tamaños y hashes. Esto no significa que esté copiado en la tarjeta: el servidor de archivos sigue pendiente de validación física de FAT, MIME, rutas y carga desde navegador. No se implementaron endpoints ni funciones escolares, no se conectó a datos reales y no se modificaron saldos, movimientos, pines, hardware o datos reales. Consultar el procedimiento y límites locales en [`tools/web/README.md`](../tools/web/README.md).
