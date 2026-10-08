# Banco Escolar · Panel Maestro · Fase PM.7

## Alcance

PM.7 implementa consulta de Cobros y operaciones de anulación, autorización para volver a cobrar y simulación de nuevo cobro exclusivamente en `DemoPanelDataService`. Las rutas son `/cobros` y `/cobros/:claimId`. No se agrega ninguna operación general para crear cobros iniciales. Los cambios se comparten entre Dashboard, Cuentas, Movimientos, Actividades y Cobros durante la sesión de la aplicación; solo viven en memoria y se descartan al recargar. No se envían a una terminal.

PM.7A añade ajustes manuales de cuenta desde el perfil, sin alterar las reglas de reclamos descritas en esta fase. Consulta [PM.7A](PANEL_MAESTRO_PM7A.md) para su alcance y límites.

`PanelDataService` conserva la frontera de consulta. Las operaciones adicionales se exponen en `DemoClaimOperations`, implementada por el servicio demo y no por una API o servicio real. Cada operación valida referencias, estados y elegibilidad, prepara las colecciones resultantes y aplica sus cambios juntos; un fallo previo a la aplicación no modifica ninguna colección ni consume IDs.

## Consulta

La lista presenta alumno, actividad, importe histórico, fecha y estado. Combina filtros por alumno, actividad, `PAID`/`VOIDED` y rango de calendario en `America/Mexico_City`; usa inicio local incluido y el inicio exclusivo del día siguiente como extremo final. Ordena por fecha original descendente. Tiene limpiar filtros, contador, sin coincidencias, colección vacía, carga, error con reintento y detalle inexistente.

El detalle enlaza al perfil, actividad, cuenta cuando existe, movimiento original y movimiento inverso. Una cuenta ausente se muestra como “Sin cuenta”. Si faltan referencias o no concuerdan alumno, importe o tipo, la UI explica la inconsistencia y bloquea la operación afectada. Movimientos y detalle de actividad enlazan al cobro correspondiente; Cuentas asocia el inverso al mismo reclamo anulado.

## Anulación

Solo se anula un `PAID` sin anulación anterior. La confirmación presenta alumno, actividad, recompensa pagada originalmente, saldo actual y saldo resultante; permite motivo vacío y permite saldo negativo según la decisión del usuario. La salida inversa usa exactamente el importe pagado originalmente, aunque cambie la recompensa vigente. Conserva el cobro y el movimiento de entrada y enlaza el inverso con ambos IDs. El motivo vacío se presenta como “Sin motivo”.

Los metadatos ficticios registran hora, motivo y actor “Panel Maestro en modo demostración”; no asignan una identidad autenticada. No se permite anular dos veces el mismo reclamo. Saldos se actualizan desde el valor de `StudentAccount`, sin reconstruirlos desde movimientos.

## Autorización y nuevo cobro

La anulación no autoriza automáticamente. Desde el cobro anulado hay una acción separada de autorización con confirmación. La autorización no toca saldo ni crea movimientos. A continuación aparece “Simular nuevo cobro” solo si hay una autorización sin consumir. El servicio vuelve a comprobar al ejecutar: actividad existente y `ACTIVE`, horario verificable y no vencido si es cronometrada, selección de participante vigente y cuenta del alumno. `PARTICIPANTS_DISABLED` no impone un filtro de lista, de acuerdo con la semántica del modo en PM.6.

La simulación muestra y aplica la recompensa vigente de la actividad, crea IDs nuevos para el reclamo y el movimiento de entrada, conserva el reclamo anulado anterior y consume la autorización una sola vez. Se bloquean confirmaciones repetidas y cobros vigentes duplicados por alumno/actividad. Un cierre o vencimiento ocurrido después de autorizar bloquea la simulación y deja la autorización sin consumir; no se crea movimiento ni se cambia el saldo.

Los reclamos históricos, incluidos anulados, continúan bloqueando la edición de actividades. El servicio de actividades de PM.6 usa la colección mutable actual de reclamos para revalidar esta regla.

## Datos ficticios para reproducir

Las personas, IDs, horas y montos son inventados. Las fechas preexistentes de PM.3 se conservan. Se añadió únicamente el movimiento 7110, salida de 70 Áureos de Bruno Vega, para que su saldo de demostración pase de 80 a 10 manteniendo coherencia con su historial. Así, anular el cobro 9102 (15 Áureos) genera el inverso −15 y saldo −5. El movimiento original 7107 permanece. Deja el motivo vacío para revisar “Sin motivo” o escribe texto para comprobar su conservación.

El cobro 9103 (Ximena, actividad activa 8104) permite probar el flujo separado. En una sesión nueva, anularlo deja el saldo en 115; autorizarlo no altera movimientos ni saldo; “Simular nuevo cobro” usa la recompensa vigente de 10, crea el reclamo 9104 y restaura el saldo a 125. Los IDs de operación se derivan de las colecciones que existan en esa sesión y pueden aumentar al repetir pruebas.

## Diferencias respecto al firmware

`src/activity_claim.h` define `ActivityClaim` con estado `PAID`/`VOIDED`, monto, IDs de actividad/alumno/movimiento y `void_movement_id`. Sin embargo, el gestor firmware actual expone lectura, comprobación/reserva de IDs y `ensureClaimPersisted` para reclamos pagados; no ofrece operación para anular, motivo, fecha/actor de anulación, registro de autorización ni flujo de re-cobro. El motor actual de recompensa rechaza un segundo reclamo previo para el par actividad/alumno, exige cuenta y monto/hora válidos, y registra movimiento, saldo y reclamo mediante su journal.

PM.7 agrega los metadatos de anulación y autorización solo a los tipos/colecciones de cliente de demostración. Las reglas de participación para el maestro, las autorizaciones de repetición y los enlaces de auditoría son políticas nuevas de esta simulación, no capacidades firmware ni un contrato definitivo de API. El firmware no fue modificado.

## Pruebas y resultados

- `npm.cmd test`: 54 pruebas aprobadas en 9 archivos. PM.7 cubre filtros y rangos locales, motivo vacío/con texto, saldo negativo, monto inverso original ante recompensa distinta, doble anulación, fallos sin cambios parciales, autorización separada, consumo único, doble confirmación, recompensa esperada, vencimiento/cierre/participante/cuenta al revalidar, referencias inconsistentes, historial de actor y propagación a Dashboard, Cuentas, Movimientos, Actividades y Cobros. La UI prueba filtros, reintento, colección vacía, detalle inexistente, confirmación de anulación/autorización/simulación y enlace/ruta activa de Cobros.
- `npm.cmd run build`: TypeScript y Vite terminaron correctamente.
- `git diff --check`: pasó; Git mostró únicamente avisos LF/CRLF en archivos locales preexistentes.
- Revisión visual y capturas PM.7 de Cobros en escritorio y a 390/320 px, ambos temas: pendiente. La carpeta ya contiene capturas v01 anteriores al PM.7; se deben conservar y generar v02 cuando Chrome headless exponga CDP. No se generaron capturas PM.7 ni se declara revisión visual.

## Pendientes

- Revisión visual real en ambos temas y tres tamaños, incluyendo confirmaciones, texto largo, foco y desbordamiento horizontal; guardar capturas v02 sin sobrescribir capturas anteriores.
- La duración de cambios solo en memoria y la falta de identidad autenticada son límites de esta demostración, no comportamiento listo para sincronizar.
- Cualquier persistencia real, motivo/actor, autorización de recobro, reglas por roles, idempotencia y resolución de concurrencia requiere diseño de dominio/API y validación específica antes de conectar firmware.

No se modificaron firmware ni imágenes originales. No se hizo Upload, Commit ni Push.
