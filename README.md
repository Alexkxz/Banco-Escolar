# Banco Escolar

Banco Escolar es una terminal educativa para primaria basada en la pantalla
Waveshare ESP32-S3-Touch-LCD-7. El firmware ofrece interfaz LVGL, datos
académicos provisionales, cuentas/movimientos locales y gestión local de
actividades/cobros. El Panel Maestro implementa PM.9C: persistencia IndexedDB y respaldo/restauración versionados solo para el espacio demo.
Backend, autenticación y sincronización continúan pendientes. La validación con Chromium e IndexedDB nativo está aprobada; consulta [resultados de PM.9C](docs/PANEL_MAESTRO_PM9C.md).

## Hardware y estado

- ESP32-S3; pantalla RGB ST7262 de 800×480; touch GT911; expansor CH422G.
- Wi-Fi en modo estación y sincronización NTP implementados.
- LittleFS validado físicamente; `StorageManager` persiste `StudentAccount` y
  movimientos de entradas/salidas mediante un journal transaccional. El flujo
  de salida y la entrada manual 5D se validaron físicamente.
- PN532 está deshabilitado (`PN532_ENABLED=false`) y desconectado. No conectar
  hasta verificar pines, alimentación y niveles lógicos de la placa lectora.

Una prueba física histórica validó movimientos y persistencia tras reinicio.
Los nombres, IDs y saldos de esa sesión no se incluyen aquí ni deben
hardcodearse; el firmware debe tomar los valores del almacenamiento vigente.

La entrada `MENÚ MAESTRO DEV` es de desarrollo. ActivitySession y
ActivityClaim están implementadas; su validación física sigue pendiente.
DIAG.1 se probó con comando USB y botón; el receptor validó ambos BMP.
La ranura microSD se sondea en solo lectura. Consulta
[`docs/CHAT_HANDOFF.md`](docs/CHAT_HANDOFF.md) para resultados y límites.

## Compilar

Con PlatformIO Core instalado, desde la raíz del proyecto:

```powershell
pio run
```

La configuración usa PlatformIO (`platformio.ini`) y C++17. La compilación no
sustituye una prueba física. No cargar firmware sin autorización explícita.

## Documentación

- Arquitectura y estado: [`docs/FUTURE_ARCHITECTURE.md`](docs/FUTURE_ARCHITECTURE.md)
- Persistencia local del Panel (diseño PM.9A, no implementado): [`docs/PANEL_MAESTRO_PM9A.md`](docs/PANEL_MAESTRO_PM9A.md)
- Skill del proyecto: [`.agents/skills/banco-escolar/SKILL.md`](.agents/skills/banco-escolar/SKILL.md)
- Reglas persistentes para Codex: [`AGENTS.md`](AGENTS.md)

El repositorio remoto usa la rama principal `main`:
<https://github.com/Alexkxz/Banco-Escolar>
