# Banco Escolar · Panel Maestro · Fase PM.6

## Alcance

PM.6 agrega consulta y administración temporal de actividades ficticias en `/actividades`, `/actividades/nueva`, `/actividades/:id` y `/actividades/:id/editar`. Usa `PanelDataService` y extiende únicamente `DemoPanelDataService` con crear, editar, finalizar y cancelar. Una instancia compartida conserva estos cambios al navegar entre rutas; todo se pierde al recargar. No hay envío a la terminal, backend ni persistencia escolar.

No se modificaron firmware, cuentas, movimientos ni reclamos/cobros. Los importes y registros relacionados conservan los valores de PM.3. La interfaz de Cobros sigue en preparación: el detalle de actividad consulta los reclamos existentes y enlaza únicamente movimientos que ya tienen una referencia válida.

## Reglas y comportamiento

- El firmware no define un campo de nombre/descripción para una actividad. La vista deriva el rótulo de su número opcional (`Actividad N`) o usa su ID (`Actividad ID`); esto es una presentación del panel, no un nuevo contrato de datos.
- La creación y edición validan número opcional de 1–9999, recompensa entera de 1–10000 Áureos, duración cronometrada de 1 segundo a 999:59 (o cero si no se usa tiempo), y el modo de participantes. `TODOS` guarda los IDs del padrón consultado en ese momento; `SELECCIONADOS` requiere IDs válidos, únicos y no vacíos; `PARTICIPANTS_DISABLED` guarda una lista vacía.
- Las actividades con todos los participantes guardan una instantánea de IDs al iniciar. Editar requiere estado ACTIVE y ausencia de todo reclamo previo; el servicio vuelve a comprobar ambas condiciones. Editar conserva ID e inicio y presenta los vencimientos anterior/propuesto antes de confirmar.
- Solo una actividad ACTIVE puede finalizarse o cancelarse. La confirmación usa la hora de la computadora y actualiza estado/fecha de cierre después de que el servicio completa la operación. No se finaliza automáticamente por vencimiento, no se crean movimientos y no se revierten reclamos.
- El reloj de operación es `Date.now()` de la computadora; se inyecta una función de reloj al servicio para pruebas, sin cambiar el reloj del sistema ni añadir controles de reloj. La presentación usa `America/Mexico_City`. Las fechas ficticias de PM.3 no se desplazan. El tiempo restante se deriva del inicio y duración; al alcanzar exactamente el vencimiento se muestra “Tiempo agotado” y el estado se mantiene ACTIVE. La vista actualiza el reloj una vez por segundo solo mientras hace falta y libera el intervalo al salir.
- La lista combina búsqueda por rótulo y filtro de estado; permite ordenar por inicio o cierre descendente. Para ordenar por cierre, las sesiones sin fecha de cierre quedan al final y dentro de ese grupo se ordenan por inicio más reciente. Incluye contador, limpiar filtros, colección vacía, sin coincidencias y error con reintento.
- El formulario no descarta el borrador si falla una mutación. Los detalles distinguen cuenta asignada/sin cuenta y cobró/no ha cobrado; presentan enlaces de perfil y a movimientos existentes. Para `PARTICIPANTS_DISABLED` no se infiere quién era elegible: se muestran solo reclamos existentes.

## Datos y diferencias de dominio

Las estructuras TypeScript reutilizan `ActivitySession` y `ActivityClaim` como modelos orientativos del cliente; no son un contrato definitivo de API. Las reglas de validación de creación/edición se alinean con `src/activity_session.h`, `src/activity_session.cpp` y `src/storage/activity_storage_manager.cpp`. Los cambios de PM.6 son una capa temporal exclusiva de demostración; no se escribió firmware.

Se conservaron sin desplazamiento las actividades históricas de PM.3. En particular, la sesión demo 8104 tiene duración de 86,400 segundos (24 horas), por encima del límite que se permite configurar ahora (999:59). No se reescribió esa fixture preexistente; duración es solo lectura en el detalle y cualquier nueva creación/edición sí aplica el límite del firmware.

## Pruebas y resultados

- `npm test`: 41 pruebas pasan. PM.6 cubre búsqueda/filtro/orden, sesiones sin cierre, snapshot de participantes, IDs únicos, reloj inyectable, bloqueo de edición tras reclamo y tras cierre, conservación de ID/inicio, vencimiento exacto sin transición automática, cierre/cancelación, enlaces con datos relacionados y conservación de cuentas/movimientos/reclamos. También comprueba que un error de creación conserva el borrador, que el timer de detalle se libera al desmontar y, en el formulario, las tres acciones masivas, selección vacía bloqueada, resumen inmediato, conservación de selección manual entre modalidades y acceso por tabulación.
- `npm run build`: TypeScript y Vite compilan correctamente.
- `git diff --check`: termina con código 0. Git informó avisos LF/CRLF en cuatro archivos locales preexistentes; no reportó errores de whitespace.
- Chrome headless produjo capturas estáticas v02 de la lista, detalle de actividad y formulario, en escritorio y a 390/320 px, tema claro y oscuro. CDP confirmó el tema aplicado y que `document`/`body.scrollWidth` no supera el viewport en las 18 combinaciones; las capturas móviles representativas se inspeccionaron visualmente. Las capturas y mediciones no validan interacción real de ratón/teclado.
- Tras mejorar la presentación de duraciones largas, se actualizaron únicamente las seis capturas del detalle con versión v03. La v02 se conserva como evolución previa.

Las imágenes PM.6 están en `panel-maestro/capturas/escritorio/` y `panel-maestro/capturas/movil/`. Las anteriores se conservaron. El formulario se actualizó en v03 para mostrar la selección manual y sus controles en escritorio y en 390/320 px, ambos temas. El formulario usa dos columnas en escritorio, apila configuración y participantes en móvil, y emplea botones nativos, casillas etiquetadas, estados anunciados y foco visible. La comprobación visual por capturas es estática; no se declara una auditoría manual de cada estado de error/vacío.

## Pendientes

- Revisar manualmente en navegador todas las interacciones de filtros, selección, confirmaciones y enlaces.
- Volver a revisar sidebar completo con scroll y drawer móvil dentro de PM.6, además de foco/teclado.
- PM.6 no implementa ni afirma que Cobros consulte o administre reclamos.
- Cualquier futura conexión real necesita resolver contratos, identidad estable, validación de fecha/hora y persistencia; estos modelos demo no deben asumirse como API.
