# Arquitectura

## Implementación actual

- Firmware PlatformIO con Arduino sobre ESP32-S3 y C++17; UI LVGL 8.4.0, integración ESP32_Display_Panel 1.0.0 y ESP32_IO_Expander 1.0.1.
- `src/main.cpp` construye y coordina pantallas, eventos, timers y ciclo de arranque.
- `src/student_model.h` define tipos de dominio desacoplados de LVGL; `src/data/provisional_data.*` ofrece datos C++ provisionales.
- `src/network/`, `src/time/` y `src/storage/` mantienen gestores separados para Wi-Fi, tiempo, LittleFS y microSD.
- La cuenta actual y los datos académicos mostrados son provisionales/demo. LittleFS ya está montado y disponible para datos locales, pero esto no convierte transferencias de UI en movimientos persistidos automáticamente.

## Dirección futura

La decisión oficial es Panel Maestro PWA multiplataforma + API + base de datos central, con filosofía offline-first en terminal y Panel. Ambos clientes acceden a los datos centrales mediante la API:

```text
Terminal ESP32 ↔ API ↔ Servidor / Data Service ↔ Base de datos central
                 ↕
          Panel Maestro PWA
```

LittleFS es actualmente el almacenamiento persistente de la terminal. Cuando exista servidor, la base de datos central será la fuente de verdad global; ESP32 y PWA mantendrán caché local, cola pendiente y estado de sincronización. El servidor no debe convertirse en requisito para operaciones locales válidas. Se guardará localmente primero cuando sea necesario y se sincronizará después. Esta conducta completa sigue pendiente: las transferencias actuales son demo y requieren integración en 5A.4.

Consulta [panel-master.md](panel-master.md) antes de planificar Panel/API/sincronización. Allí se definen plataformas, IndexedDB y Service Worker previstos, módulos, idempotencia y conflictos. No hay framework, motor de base de datos ni protocolo final seleccionado. Una migración futura a nube/remoto conserva esta separación.

Excel se importa desde Panel del Maestro/servidor: no hagas que el ESP32 lea hojas de cálculo. Mantén UI, dominio y persistencia separados y no introduzcas lógica de backend en widgets LVGL.

## Reglas de identidad y dominio

- `student_id` es la identidad estable local; el UID NFC es un identificador reemplazable, no una clave primaria.
- No mezcles nivel académico, saldo de Áureos ni logros.
- Cada cambio futuro de Áureos desde terminal o Panel debe generar un movimiento trazable; el saldo debe poder reconstruirse o validarse con los movimientos.
- El historial persistente de movimientos reales está pendiente (Fase 5A.4). No adelantes esa implementación en una fase de documentación/UI.
- `AttendanceRecord` y `StoredAttendanceRecord` están definidos como estructuras
  preparadas, pero no hay API de almacenamiento ni historial real de asistencia.

## Stack declarado

`platformio.ini` fija pioarduino `platform-espressif32` 53.03.11, C++17 y dependencias directas Adafruit PN532 1.3.4, Adafruit BusIO 1.17.4, ArduinoJson 6.21.x y esp-lib-utils referenciado en v0.2.0. LVGL y los componentes ESP32 Display Panel/IO Expander están vendorizados bajo `lib/`. El core reportado para el entorno es Arduino-ESP32 3.1.1 con ESP-IDF base 5.3.x. Verifica versiones en los manifiestos instalados y archivos vendorizados si cambian las dependencias.
