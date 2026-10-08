# Banco Escolar · Panel Maestro · Fase PM.3

## Alcance

PM.3 alimenta únicamente el Dashboard con un conjunto local, determinista y ficticio de solo lectura. Conserva el diseño, temas, navegación y comportamiento responsive de PM.2. Los otros nueve módulos mantienen sus estados de preparación.

No añade backend, autenticación, sincronización, almacenamiento escolar, ni operaciones para altas, ediciones, cobros, retiros o anulaciones. El proveedor demo no lee firmware, archivos, red ni almacenamiento.

## Datos de demostración

`DemoPanelDataService` contiene seis alumnos ficticios: Ximena Sol, Bruno Vega, Lía Robles, Gael Luna, Mara Ríos y Teo Nube. No se usan fotos; los seis tienen `avatar_asset: null` y UID no asignado. Teo (ID ficticio 206) no tiene cuenta. La interfaz informa esa ausencia sin mostrarle saldo cero.

Hay cinco cuentas con saldos enteros 125, 80, 45, 45 y 35; suman **330 Áureos**. Cada balance se lee directamente de la colección de cuentas. `createDashboardSnapshot` no lo reconstruye desde el historial. Los nueve movimientos fijos, con fechas UTC, dejan los mismos netos por alumno como comprobación de coherencia; esa comprobación no cambia la regla de fuente de saldo en la aplicación.

El conjunto incluye cuatro actividades: una activa cuya duración ya venció, una finalizada, una cancelada y otra activa dentro de su duración. La activa vencida conserva estado `ACTIVE`, distinto de `FINISHED`, y no tiene cobros. Los tres cobros pagados de las otras actividades enlazan a un movimiento por ID, alumno, importe y fecha; no se repite el par actividad-alumno.

Los IDs 201–206, 7101–7109, 8101–8104 y 9101–9103, nombres, grupos, cuentas, fechas y movimientos son inventados para la demostración. Las fechas están fijas para que el orden y los ejemplos sean reproducibles. No representan registros de alumnos reales.

## Correspondencia con firmware y límites de los modelos

Los tipos de cliente reutilizan los campos conceptuales de `Student`, `StudentAccount`, `MovementRecord`, `ActivitySession` y `ActivityClaim` revisados en `src/student_model.h`, `src/storage/storage_manager.h`, `src/activity_session.h` y `src/activity_claim.h`.

- En el cliente, los identificadores, balances, importes y timestamps son `number`; la muestra mantiene valores enteros pequeños y timestamps Unix en segundos. El firmware usa anchos explícitos (`uint16_t`, `uint64_t`, `int32_t`/`int64_t`).
- `MovementRecord.origin: 'DEMO'` es exclusivo de este cliente. El firmware solo define origen terminal o panel; no se etiqueta ningún movimiento ficticio como originado por hardware o por una operación real del panel.
- `amount` conserva signo: entradas positivas y salidas negativas. Los balances ficticios concuerdan con el neto de muestra, pero la vista suma la colección de cuentas.
- Son tipos orientativos de lectura, no un contrato definitivo de API ni un cambio al esquema del firmware.

No se infiere que el vencimiento cierre una actividad. El Dashboard cuenta por `status === 'ACTIVE'`; no calcula duración para cambiar estados.

## Cálculo y presentación

`loadDashboardSnapshot` obtiene en paralelo alumnos, cuentas, actividades y movimientos mediante `PanelDataService`. El Dashboard calcula:

| Indicador | Alcance en el conjunto PM.3 |
|---|---|
| Alumnos registrados | Los seis perfiles ficticios, incluido Teo, que no tiene cuenta. |
| Áureos en cuentas demo | Suma directa de las cinco cuentas: 330 Áureos. No es una suma del historial. |
| Actividades activas | Dos actividades con estado `ACTIVE`, aunque una ya excedió su duración. |
| Movimientos del conjunto | Los nueve movimientos, no solo las filas visibles. |

La lista muestra los cinco movimientos más recientes ordenados por timestamp descendente; en empate usa ID descendente. Cada fila muestra alumno, concepto, importe firmado y fecha UTC.

Se conserva «Modo demostración» en el layout y se añade una nota explícita de que son datos ficticios y de solo lectura, sin conexión a terminal ni a fuente real. Carga, colección vacía y error de consulta son estados distintos. Una excepción no se transforma en `[]`; el estado de error permite reintentar.

## Pruebas y revisión visual

- `npm run test`: SUCCESS, 11 pruebas. Comprueba los indicadores, suma directa de cuentas, orden de movimientos, alumno sin cuenta, estados de actividades, coherencia de cobros y movimientos, estados de carga/vacío/error y reintento.
- `npm run build`: SUCCESS con TypeScript y Vite; JavaScript 288.09 kB (91.37 kB gzip), CSS 17.12 kB (4.29 kB gzip). Sin errores ni warnings de compilación.
- `git diff --check`: SUCCESS; conserva los avisos CRLF de archivos locales preexistentes.
- Chrome headless comprobó el Dashboard de escritorio (1366×768), móvil (390×844) y móvil (320×844) en tema claro y oscuro. Las seis capturas verifican el tema aplicado, cuatro indicadores, cinco filas recientes y ausencia de overflow horizontal (`scrollWidth <= clientWidth`). Son capturas estáticas de viewport; la pantalla conserva scroll vertical para el resto del Dashboard.

Capturas v03, sin sobrescribir las v01/v02:

- [Dashboard escritorio claro](../panel-maestro/capturas/escritorio/2026-10-07--dashboard--escritorio-claro--v03.png)
- [Dashboard escritorio oscuro](../panel-maestro/capturas/escritorio/2026-10-07--dashboard--escritorio-oscuro--v03.png)
- [Dashboard móvil 390 px claro](../panel-maestro/capturas/movil/2026-10-07--dashboard--movil-390px-claro--v03.png)
- [Dashboard móvil 390 px oscuro](../panel-maestro/capturas/movil/2026-10-07--dashboard--movil-390px-oscuro--v03.png)
- [Dashboard móvil 320 px claro](../panel-maestro/capturas/movil/2026-10-07--dashboard--movil-320px-claro--v03.png)
- [Dashboard móvil 320 px oscuro](../panel-maestro/capturas/movil/2026-10-07--dashboard--movil-320px-oscuro--v03.png)

## Pendientes

PM.4 implementa el directorio y perfiles ficticios, documentados en [PANEL_MAESTRO_PM4.md](PANEL_MAESTRO_PM4.md). La afirmación histórica de que los otros módulos siguen preparados aplica al cierre de PM.3.

PM.5 conecta las consultas demo de cuentas y movimientos y cambia la presentación de fechas del Dashboard de UTC a `America/Mexico_City`, sin modificar timestamps. El conjunto, sus fechas y cálculos definidos en este documento permanecen sin cambio.

- Los indicadores son datos de demostración; no expresan conteos, saldos ni conectividad reales.
- Los demás módulos permanecen vacíos/en preparación y no deben leer este conjunto como si fueran datos escolares reales.
- No se define API, autenticación, persistencia, sincronización ni estrategia de conflictos en esta fase.
- PM.3 verificó el aspecto de ambos temas, pero no la persistencia de la preferencia después de cambiarla con el control y recargar; esa comprobación sigue pendiente de PM.2.

No se modificó firmware ni imágenes originales. No se realizó Upload, Commit ni Push.
