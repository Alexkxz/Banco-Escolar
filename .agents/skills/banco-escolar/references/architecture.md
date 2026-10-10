# Arquitectura

## Implementación actual

- Firmware PlatformIO con Arduino sobre ESP32-S3 y C++17; UI LVGL 8.4.0, integración ESP32_Display_Panel 1.0.0 y ESP32_IO_Expander 1.0.1.
- `src/main.cpp` construye y coordina pantallas, eventos, timers y ciclo de arranque.
- `src/student_model.h` define tipos de dominio desacoplados de LVGL; `src/data/provisional_data.*` ofrece datos C++ provisionales.
- `src/network/`, `src/time/` y `src/storage/` mantienen gestores separados para Wi-Fi, tiempo, LittleFS y microSD.
- `StudentAccount` persistente y vistas académicas provisionales coexisten; Registrar salida e entrada usan el motor local transaccional de LittleFS.

## Dirección futura

La decisión oficial es Panel Maestro PWA multiplataforma + API + base de datos central, con filosofía offline-first en terminal y Panel. Ambos clientes acceden a los datos centrales mediante la API:

```text
Terminal ESP32 ↔ API ↔ Servidor / Data Service ↔ Base de datos central
                 ↕
          Panel Maestro PWA
```

PM.10A propone que la ESP32-S3 sea centro local: alojar API y servir archivos del Panel desde microSD a equipos del mismo router Wi-Fi; nube opcional para backup/acceso remoto. Terminal es fuente primaria. La API local de archivos FAT existe, pero las escrituras físicas siguen pendientes. Ya se implementó un servidor HTTP temporal de solo lectura para archivos de prueba `panel-test/` y `/status`; no sirve el Panel real, no aloja la API ni sincroniza. Primera version: una terminal; cola durable e idempotente si pierde Wi-Fi.

Consulta [panel-master.md](panel-master.md) antes de planificar Panel/API/sincronización. Allí se definen plataformas, IndexedDB y Service Worker previstos, módulos, idempotencia y conflictos. No hay framework, motor de base de datos ni protocolo final seleccionado. Una migración futura a nube/remoto conserva esta separación.

Excel se importa desde Panel del Maestro/servidor: no hagas que el ESP32 lea hojas de cálculo. Mantén UI, dominio y persistencia separados y no introduzcas lógica de backend en widgets LVGL.

## Reglas de identidad y dominio

- `student_id` es la identidad estable local; el UID NFC es un identificador reemplazable, no una clave primaria.
- No mezcles nivel académico, saldo de Áureos ni logros.
- Cada cambio futuro de Áureos desde terminal o Panel debe generar un movimiento trazable. El saldo persistente se actualiza junto con el movimiento; no reconstruirlo desde el historial.
- El historial de movimientos reales en LittleFS y la UI de solo lectura están implementados; la sincronización sigue pendiente.
- `AttendanceRecord` y `StoredAttendanceRecord` están definidos como estructuras
  preparadas, pero no hay API de almacenamiento ni historial real de asistencia.

## Stack declarado

`platformio.ini` fija pioarduino `platform-espressif32` 53.03.11, C++17 y dependencias directas Adafruit PN532 1.3.4, Adafruit BusIO 1.17.4, ArduinoJson 6.21.x y esp-lib-utils referenciado en v0.2.0. LVGL y los componentes ESP32 Display Panel/IO Expander están vendorizados bajo `lib/`. El core reportado para el entorno es Arduino-ESP32 3.1.1 con ESP-IDF base 5.3.x. Verifica versiones en los manifiestos instalados y archivos vendorizados si cambian las dependencias.

## Menú Maestro y actividades futuras (Fase 5E)

La Fase 5E incorpora una entrada temporal `MENÚ MAESTRO DEV` desde Configuración y un Menú Maestro local. La base 5E fue reportada validada físicamente; no detecta tarjeta maestra ni usa PN532. 5E.1 agrega configuración mediante `ActivityDraft` en RAM, sin crear sesiones, participaciones, movimientos ni recompensas.

La arquitectura futura prevé entidades `ActivitySession` y `ActivityClaim` (propuestas en `data-model.md`). Debe admitir múltiples sesiones activas simultáneamente: un alumno elegible para una actividad puede registrarse directamente; si es elegible para varias, deberá elegir cuál completó. Una participación válida no puede cobrarse dos veces. Las actividades deben reutilizar `StudentAccount`, `MovementRecord` y el motor transaccional/journal existente, conservando la trazabilidad del origen (entrada manual, actividad o Panel Maestro) sin cambiar ahora el esquema de movimiento.

El futuro Panel Maestro podrá crear y revisar actividades, revisar participaciones por alumno, agregar dinero manualmente y anular participaciones erróneas. Una anulación será lógica (`VOIDED`/anulada) y generará un movimiento inverso; nunca borrará físicamente la participación o el movimiento original. Tarjeta maestra, NFC, persistencia de actividades, cobro, Panel y sincronización siguen fuera de 5E.

La Fase 5E.1 añade `ActivityDraft`, configuración funcional en RAM. 5E.1A ya está implementada en código para suspender el timeout normal en el contexto Maestro; su prueba física de más de 60 segundos está pendiente. 5E.1B está implementada y compilada, con validación visual física pendiente.


### PM.10A - vinculacion Panel-terminal

ESP32-S3 es centro local propuesto: API y archivos del Panel desde microSD, accesible por el router Wi-Fi; nube opcional de backup/remoto. Terminal conserva sus `student_id` y es fuente primaria. La lectura/escritura genérica local FAT está en código; escritura física no validada. Un servidor HTTP de prueba de archivos SD y estado ya está implementado, sin API escolar ni integración del Panel. IDs ficticios demo quedan aislados. Cambio PIN es comando cifrado pendiente hasta ACK terminal. Ver [PANEL_MAESTRO_PM10A.md](../../../../docs/PANEL_MAESTRO_PM10A.md).
