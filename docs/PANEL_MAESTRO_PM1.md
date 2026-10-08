# Panel Maestro · Fase PM.1

**Estado:** estructura y arquitectura inicial implementadas; funciones administrativas, API y sincronización siguen futuras.

## Decisiones de esta fase

- La aplicación vive en `panel-maestro/`, separada del firmware PlatformIO.
- El scaffold usa React, TypeScript y Vite; React Router organiza las rutas conceptuales. Es la selección del primer cliente web, no una decisión sobre backend, base de datos o protocolo.
- El tema usa azul marino, azul, cian y dorado; los tokens permiten preparar una paleta oscura. La disposición responde a escritorio, tableta y teléfono.
- `PanelDataService` marca la frontera de lectura prevista. `DemoPanelDataService` devuelve colecciones vacías y no tiene acceso a red, almacenamiento ni operaciones de escritura.
- La interfaz muestra una pantalla inicial de desarrollo y placeholders para `/dashboard`, `/alumnos`, `/cuentas`, `/movimientos`, `/actividades`, `/cobros`, `/progreso`, `/asistencia`, `/dispositivos` y `/configuracion`.
- Se importa el logo existente desde `Imagenes/Logo y nombre.png`; el original no se modifica.

## Correspondencia de modelos

La revisión de `src/student_model.h`, `src/storage/storage_manager.h`, `src/activity_session.h` y `src/activity_claim.h` alimenta tipos de cliente orientativos. El firmware contiene modelos de alumnos y actividades con estado real local, pero no define contratos de sincronización. Los datos académicos siguen provisionales; `AccountRecord` de demo no equivale a `StudentAccount` persistente. Ningún tipo TypeScript creado en PM.1 debe considerarse contrato de servidor.

## Fuera de alcance

No hay servidor/API, autenticación, autorización, escritura monetaria, acceso a LittleFS/microSD/NVS, IndexedDB, Service Worker ni funcionamiento offline garantizado. La arquitectura oficial offline-first y el flujo futuro de sincronización permanecen descritos en `.agents/skills/banco-escolar/references/panel-master.md`. Una fase posterior deberá definir contratos, seguridad, persistencia local, cola, idempotencia y conflictos antes de conectar datos reales.

## Validación de PM.1

Desde `panel-maestro/`: `npm install`, `npm run dev`, `npm run test` y `npm run build`. El build web no valida el firmware ni sustituye pruebas en hardware.
