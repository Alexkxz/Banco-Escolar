---
name: banco-escolar
description: Contexto técnico y reglas de trabajo del firmware Banco Escolar en Waveshare ESP32-S3-Touch-LCD-7. Úsala para cambios, depuración, validación o planificación de este proyecto.
---

# Banco Escolar

Banco Escolar es una terminal educativa para primaria. El firmware actual combina una interfaz LVGL y datos provisionales; su dirección futura incluye identificación NFC, cuentas de Áureos, progreso académico, asistencia, operación local y sincronización con el Panel del Maestro. La tarjeta NFC solo identificará; nunca almacenará saldo ni historial.

## Reglas críticas

- Trata el código actual como fuente de verdad. Distingue implementación, demo, preparación y futuro; no conviertas datos demo en persistencia o comportamiento real.
- No cambies hardware, pines, memoria, buses, particiones ni drivers conocidos por limpiar warnings. Requiere evidencia y una fase que investigue ese efecto.
- Mantén desactivados los tests automáticos de rendimiento, el self-test de almacenamiento y el permiso de formato salvo autorización expresa para una prueba concreta.
- No imprimas ni copies credenciales, tokens, claves o secretos. No inventes GPIO, UID NFC, datos académicos, saldos ni resultados físicos.
- No implementes una fase posterior por adelantado. En particular, la persistencia real de movimientos de Áureos corresponde a la Fase 5A.4.

## Cómo trabajar

1. Lee primero las referencias pertinentes y verifica cada afirmación contra los archivos fuente indicados allí.
2. Resume alcance y límites antes de editar; modifica solo lo pedido.
3. Cuando se solicite firmware, ejecuta BUILD, corrige fallos y reporta RAM, Flash y warnings disponibles. No hagas Upload ni Upload and Monitor sin autorización explícita.
4. Separa resultados de compilación de pruebas en hardware. No afirmes una validación física a partir de un BUILD.
5. Al cerrar una fase que cambie arquitectura, hardware, almacenamiento, red, UI o roadmap, evalúa si las referencias requieren actualización.

## Referencias

- Hardware, memoria, pantalla y buses: [hardware.md](references/hardware.md)
- Componentes y límites de arquitectura: [architecture.md](references/architecture.md)
- Arquitectura del Panel Maestro PWA, modo offline y sincronización: [panel-master.md](references/panel-master.md)
- Alumnos, evaluaciones, cuentas y umbrales: [data-model.md](references/data-model.md)
- Pantallas, fuentes y navegación: [ui.md](references/ui.md)
- Wi-Fi, NTP y arranque: [networking.md](references/networking.md)
- LittleFS, NVS y microSD: [storage.md](references/storage.md)
- Decisiones y mediciones de rendimiento: [performance.md](references/performance.md)
- Estado y seguridad NFC: [nfc.md](references/nfc.md)
- Validación y reportes: [workflow.md](references/workflow.md)
- Fases cerradas, pendientes y documentos históricos: [roadmap.md](references/roadmap.md)
