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
