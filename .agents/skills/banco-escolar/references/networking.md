# Wi-Fi, NTP y arranque

## Wi-Fi

`WiFiManager` opera en `WIFI_STA`, usa scan asíncrono, elimina SSID duplicados conservando la señal más fuerte y ordena resultados por RSSI. Redes abiertas se conectan sin contraseña; las protegidas usan teclado LVGL. Los datos persistidos viven en Preferences, namespace `banco_wifi`, keys `ssid` y `password`; el gestor confirma conexión y guardado antes de conservar las nuevas credenciales. No imprimas contraseñas en Serial ni las documentes aquí.

`src/network/wifi_credentials.h` es configuración de ejemplo y debe permanecer sin credenciales no vacías. La conexión Wi-Fi ya fue reportada físicamente validada; una compilación no vuelve a validar acceso al punto de acceso.

## Hora

`TimeManager` sincroniza vía NTP con `pool.ntp.org` y `time.nist.gov`. La zona lógica del proyecto es `America/Mexico_City`; la cadena POSIX que hoy usa libc es `CST6`. La implementación valida epoch desde 2025, comprueba sincronización de forma periódica y tiene timeout/reintentos.

El formato usual de UI es día abreviado, fecha y hora con segundos; confirma los widgets y nombres concretos antes de cambiarlo. No cambies TZ, servidores, Wi-Fi/NVS ni política NTP al trabajar en otras áreas.

## Splash

Las constantes de `src/main.cpp` establecen duración mínima de splash de 6000 ms y espera inicial NTP de 10000 ms cuando Wi-Fi conecta; `TimeManager` tiene timeout propio de sincronización. El arranque espera sincronización o fallback y evita bloquear con `delay()`. Barra, aro y porcentaje comparten progreso; al 100 % aparece “Sistema listo” con una pausa breve antes de Inicio. Verifica las constantes actuales, no copies tiempos de reportes anteriores si el código cambió.

## API y conectividad futuras

Panel Maestro PWA y ESP32 deberán conservar operación local válida cuando falte Internet y sincronizar mediante API al recuperar acceso. Distingue Internet, conexión Wi-Fi y disponibilidad de API: un servidor en LAN puede ser accesible sin Internet. La primera apertura de la PWA requiere descargar recursos; el arranque offline posterior se prevé mediante Service Worker. Nada de esto está implementado aún.

La API futura intermedia consultas y movimientos de ambos clientes con el servidor/base de datos; asumirá autenticación, autorización, validación, deduplicación y resolución de conflictos. No se elige framework ni protocolo final. Consulta [panel-master.md](panel-master.md) para el flujo local → pendiente → envío → confirmación → sincronizado.

### Servidor HTTP microSD de prueba

`PanelHttpServer` en `src/network/` ya usa `WebServer` del core Arduino en el puerto 80 para lectura estática de `panel-test/` desde FAT. `GET /` abre la página de ejemplo; `/status` informa montaje SD y versión de la página. No requiere conectividad a Internet, pero sí que el dispositivo y el navegador compartan LAN Wi-Fi. El ciclo principal atiende HTTP sin usar la UI táctil. Las lecturas salen por bloques de 1 KiB y el flujo HTTP evita las funciones SD que recuperan/eliminan temporales. No hay rutas para modificar archivos.

Este servidor es una prueba local sin autenticación ni TLS; no publicar en una red no confiable ni exponer al Internet. Todavía no es API del Panel Maestro, ni sirve su build real, ni permite subir/actualizar archivos. Requiere prueba física antes de considerarse verificado en hardware. Ver [PM.10A](../../../../docs/PANEL_MAESTRO_PM10A.md) y los recursos para tarjeta en `tools/web/sample_sd/`.

### Motor OTA (OTA.2A)

`src/network/ota_manager.*` contiene el motor OTA y `BANCO_OTA_ENABLED` vale 0 por defecto; no existe ruta HTTP de actualizacion. No se encontró LOGIN-R implementado en el firmware ni en el Panel Maestro. `OtaAuthorizationProvider` falla cerrado si no recibe un verificador; el adaptador futuro debe validar sesión autenticada y vigente del maestro, rol administrativo, permiso específico, y consumir atómicamente un identificador único de aprobación. La capacidad vincula maestro, aprobación, versión, URL y SHA-256 del manifiesto; para reiniciar se exige otra aprobación reciente del mismo maestro y con ID distinto. Cuando se habilite en una fase de release, requiere versión actual, CA raíz HTTPS y clave pública de firma válidas; no se deben guardar claves privadas de firma en el firmware. El manifiesto autentica modelo, versión, tamaño, SHA-256, URL de imagen y `data_schema` con ECDSA P-256; solo HTTPS, sin redirecciones, y la imagen se escribe por bloques al slot OTA inactivo.

`BANCO_OTA_DATA_SCHEMA_VERSION` representa el contrato agregado actual de datos persistentes y vale 1. Incluye cuentas y movimientos LittleFS, diario de transacciones pendientes (incluido formato 2), sesiones y reclamaciones de actividades, y claves NVS usadas por sus contadores/estado. Releases OTA solo pueden declarar el mismo `data_schema` mientras se permita rollback a la imagen anterior. Un valor ausente o diferente se rechaza antes de entrar en mantenimiento o escribir la partición. No hay migraciones automáticas. El arranque actual puede completar un diario de movimiento pendiente con el mismo esquema; no transforma formatos. La microSD no recibe escrituras de arranque identificadas. Cualquier cambio de esquema requiere una política/versionado y pruebas específicas de compatibilidad con la imagen de rollback antes de aceptar una actualización.

La tabla `default_16MB.csv` ya contiene `otadata`, `ota_0` y `ota_1`. El motor rechaza solicitudes si el rollback no esta habilitado en la configuracion efectiva del bootloader/app. En una configuracion OTA explicita, se difiere la confirmacion automatica del core Arduino y el gestor evalua salud tras 30 s; si no confirma antes de 60 s intenta rollback. La comprobacion de salud espera 30 s y solo acredita que `setup()` termino, que la placa/adapter/display estan inicializados y que el loop sigue atendiendo el monitor; no diagnostica tactil, microSD ni fallos funcionales de UI. Los errores fatales que usan `halt_on_error()` solicitan rollback en ese punto. El limite de 60 s solo corre cuando el loop sigue activo; una detencion total antes de entrar al loop depende del watchdog/reset del sistema. Requiere autotests de hardware y pruebas fisicas antes de produccion.

OTA-DEV.2 agrega el contrato de adaptador de LOGIN-R y el campo firmado `data_schema` a las herramientas/manifiestos. No hay validador LOGIN-R, ni objeto/caller de autorización configurado. El proveedor sin callback rechaza. La suite host valida el contrato y usa modelos de estados; no prueba una sesión real ni una aprobación de servidor. `BANCO_OTA_ENABLED=0` sigue impidiendo solicitudes. No hay CA/clave de producción ni prueba mbedTLS/TLS/descarga en la placa; consulta [OTA_3A_PREPARACION_SEGURIDAD.md](../../../../docs/OTA_3A_PREPARACION_SEGURIDAD.md) para el estado y límites.
