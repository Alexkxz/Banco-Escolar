# Panel Maestro PWA: arquitectura oficial

## Decisión y alcance

El Panel Maestro será una PWA multiplataforma + API + base de datos central, con filosofía offline-first. Todo este Panel, servidor y sincronización es FUTURO; esta referencia define arquitectura, no implementación.

Debe funcionar desde navegador en Windows, Android, iPhone/iPad, macOS, Linux y tablets, y como PWA instalada donde la plataforma lo permita. Una única interfaz responsive adapta menú lateral en escritorio y menú compacto en móvil; la lógica principal no dependerá de APIs exclusivas de Windows. No se inicia una aplicación exclusiva .exe, WinForms, WPF o UWP.

El empaquetado nativo no es prioridad inicial. Si se necesita después, la misma interfaz web podría empaquetarse con Tauri o equivalente; Electron es alternativa, no obligación. No hay framework de frontend/backend ni motor de base de datos elegido.

## Offline-first

ESP32 y PWA deben poder continuar temporalmente con operaciones válidas sin Internet, guardar localmente primero cuando sea necesario y sincronizar después. El servidor no debe ser un requisito obligatorio para usar la terminal. Esta conducta completa aún requiere integración de movimientos y sincronización.

La PWA deberá poder abrirse offline después de haber sido cargada/instalada y de conservar sus recursos en el dispositivo. No se garantiza una primera apertura sin descarga previa. Se prevé Service Worker para cachear interfaz/recursos estáticos, iniciar offline y facilitar actualizaciones; se prevé IndexedDB para alumnos en caché, movimientos recientes, operaciones pendientes, configuración y datos académicos necesarios offline.

La UI futura debe mostrar “Sin conexión” y número de cambios pendientes. Al recuperar acceso a la API, intentará sincronización automática o mediante acción clara, con estados “Sincronizando”, “Sincronizado” y “Errores pendientes”. Internet, LAN y acceso a la API son estados distintos.

## Capas y fuente de verdad

```text
Terminal ESP32 ↔ API ↔ Servidor / Data Service ↔ Base de datos central
                 ↕
          Panel Maestro PWA
```

Hoy LittleFS es almacenamiento persistente de la terminal. Cuando exista servidor, la base central será fuente de verdad global; ambos clientes conservarán caché, cola y estado de sincronización. La PWA y el ESP32 no accederán directamente a la base central. La arquitectura puede crecer hacia nube/remoto sin fijar aún tecnologías.

La API futura será responsable de autenticación, autorización, recepción de movimientos, consultas, sincronización, control de duplicados, validaciones y resolución de conflictos. Inicialmente habrá acceso protegido para un administrador/docente principal; podrán añadirse maestros, roles, permisos, grupos y escuelas después.

## Movimientos y sincronización

Cada cambio de Áureos desde terminal o Panel debe generar `MovementRecord` o evento equivalente en el servidor; el saldo debe poder reconstruirse o validarse a partir del historial. No modificar solo el saldo sin movimiento trazable.

Flujo futuro: operación local → guardar local → marcar pendiente → detectar acceso a API → enviar → servidor valida → servidor confirma → marcar sincronizado. Si falla antes de confirmación, conservar el registro pendiente. Los reintentos no deben duplicar movimientos: cada operación requiere identificador único estable y reconocimiento de operaciones ya recibidas (idempotencia).

El `next_mv_id` actual es local a la terminal. El esquema de identidad global entre dispositivos y Panel sigue PENDIENTE; no inventar protocolo. La escritura actual del ESP32 exige hora válida; resolver el arranque sin hora NTP en su fase sin relajar la validación como efecto colateral.

ESP32 y Panel pueden operar desconectados simultáneamente. La futura sincronización debe contemplar origen, timestamps, IDs, estado `synced`, orden de eventos y conflictos entre operaciones. La estrategia final de conflictos está PENDIENTE y se define en la fase de sincronización.

## Módulos previstos

| Módulo | Alcance futuro |
|---|---|
| Inicio / Dashboard | Alumnos, actividad reciente, movimientos del día, pendientes, sincronización y dispositivos. |
| Alumnos | Perfil como núcleo: datos generales, saldo, movimientos, NFC, lectura, dictado, asistencia, logros y gráficas; crecimiento modular. |
| Áureos | Sumar, descontar, transferir, ajustar y operaciones grupales, siempre con movimiento trazable. |
| Movimientos | Estado de cuenta; filtros por alumno, fecha, tipo, concepto, cantidad, origen y sincronización. |
| Asistencia | Historial real cuando exista, separado del saldo y de evaluaciones no aplicadas. |
| Académico | Lectura: PPM, histórico, nivel y gráficas. Dictado: palabras, errores e histórico. No mezclar con Áureos. |
| Logros | Reglas, fecha, alumno, insignia y posible recompensa; criterios aún sin definir. |
| Dispositivos | Nombre, online/offline, última conexión, versión firmware, almacenamiento, pendientes, NFC, microSD y última sincronización. |
| Configuración | Ajustes necesarios para administración y funcionamiento; diseño exacto pendiente. |

No se define todavía el diseño visual exacto ni se implementa login o pantallas.

## Exportaciones y respaldos

Se prevén CSV y Excel desde Panel/servidor; PDF después si se requiere. El ESP32 no leerá Excel. Respaldos futuros: datos locales de la terminal en LittleFS, base central con backups periódicos y microSD como respaldo local opcional. Estos mecanismos no se implementan en esta fase.

## Fases

Consulta [roadmap.md](roadmap.md): 0S.3 prepara Git/commit, 5A.4 integra persistencia real, 5A.5 valida reinicios y 5B muestra historial. Fase 6 define sincronización; 7 entrega Panel PWA 1.0; 7.1 integra Panel ↔ ESP32; 7.2 completa operación offline. Dispositivos, asistencia, académico, gráficas, logros, respaldos y exportaciones se desarrollan en las fases posteriores indicadas allí.
