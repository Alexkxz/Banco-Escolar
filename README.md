# Banco Escolar

Banco Escolar es una terminal educativa para primaria basada en la pantalla
Waveshare ESP32-S3-Touch-LCD-7. El firmware ofrece interfaz LVGL, datos
académicos provisionales y conectividad local; el Panel del Maestro y la
sincronización siguen en el roadmap. El Panel Maestro futuro será una PWA
multiplataforma y offline-first, con arranque sin Internet después de una carga
previa y sincronización posterior mediante API.

## Hardware y estado

- ESP32-S3; pantalla RGB ST7262 de 800×480; touch GT911; expansor CH422G.
- Wi-Fi en modo estación y sincronización NTP implementados.
- LittleFS validado físicamente; StorageManager tiene infraestructura para
  movimientos, aún no conectada a las transferencias de Áureos en pantalla.
- PN532 está deshabilitado (`PN532_ENABLED=false`) y desconectado. No conectar
  hasta verificar pines, alimentación y niveles lógicos de la placa lectora.

## Compilar

Con PlatformIO Core instalado, desde la raíz del proyecto:

```powershell
pio run
```

La configuración usa PlatformIO (`platformio.ini`) y C++17. La compilación no
sustituye una prueba física. No cargar firmware sin autorización explícita.

## Documentación

- Arquitectura y estado: [`docs/FUTURE_ARCHITECTURE.md`](docs/FUTURE_ARCHITECTURE.md)
- Skill del proyecto: [`.agents/skills/banco-escolar/SKILL.md`](.agents/skills/banco-escolar/SKILL.md)
- Reglas persistentes para Codex: [`AGENTS.md`](AGENTS.md)

El repositorio remoto usa la rama principal `main`:
<https://github.com/Alexkxz/Banco-Escolar>
