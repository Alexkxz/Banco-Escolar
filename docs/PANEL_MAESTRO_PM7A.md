# Banco Escolar · Panel Maestro · Fase PM.7A

## Alcance

PM.7A agrega ajustes manuales de Áureos al perfil individual del alumno, únicamente en `DemoPanelDataService`. La tarjeta de saldo comparte la fila con “Imagen del perfil” en escritorio y se apila en pantallas pequeñas. El perfil muestra saldo consultado, acciones “Agregar Áureos”, “Retirar Áureos” y “Ver cuenta”. La falta de cuenta se representa como “Sin cuenta” y nunca crea cuenta automáticamente. Carga, error y cuenta ausente son estados separados del saldo numérico cero.

El ajuste requiere una cantidad entera positiva y motivo opcional. Antes de aplicar, “Revisar ajuste” presenta alumno, operación, cantidad, motivo (o “Sin motivo”), saldo actual y resultado. Confirmar revalida cuenta y saldo en el servicio; si cambió, actualiza la vista y obliga a revisar y confirmar un resultado nuevo. Los retiros pueden dejar saldo negativo y ese caso se advierte antes de confirmar. Cancelar no escribe. La interfaz deshabilita el envío mientras procesa, y el servicio consume un ID de confirmación una sola vez.

## Servicio e historial

El servicio prepara el saldo y el movimiento, ejecuta el punto de fallo de prueba y solo después aplica ambas colecciones. Un error deja saldo, movimientos, cobros y autorizaciones sin cambios parciales. El saldo nuevo se calcula desde el `StudentAccount` vigente, nunca desde el historial.

Cada movimiento usa ID nuevo, hora actual de la computadora y `origin: DEMO`; la fecha de éxito se presenta con `America/Mexico_City`. El concepto indica “Ajuste manual de demostración” y el tipo es entrada o salida. El motivo vacío se conserva como “Sin motivo”. No se asigna usuario autenticado ni se atribuye la operación a la terminal. El ajuste no modifica reclamos, anulaciones ni autorizaciones PM.7. Las consultas de Dashboard, Cuentas, Movimientos y perfil leen las mismas colecciones de la instancia compartida.

## Límites del modelo

En firmware, `StudentMovement.amount` es `int32_t`; el almacenamiento limita un abono individual con `MAX_SINGLE_CREDIT = 10000` y limita el saldo con `MAX_ACCOUNT_BALANCE = 1,000,000,000,000`. PM.7A usa 10,000 Áureos como máximo por movimiento y el límite absoluto de 1e12 para el saldo. La demostración permite un saldo negativo por decisión del usuario; el firmware actual rechaza saldos negativos y no contiene esta operación administrativa del Panel Maestro. Por ello, la capacidad de saldo negativo es una regla exclusiva de PM.7A demo y no debe tratarse como contrato de API ni como capacidad lista para firmware.

Los importes y saldos de ejemplo continúan siendo ficticios. Los cambios viven en memoria durante la navegación, se pierden al recargar y no se envían a la terminal. No se agregaron persistencia real, backend, autenticación ni sincronización.

## Verificaciones

- Pruebas de servicio: entradas, retiros a cero y negativo, motivo vacío y texto, fecha y tipo de movimiento; valores no válidos; alumno sin cuenta; revalidación de saldo; confirmación duplicada; error sin cambios parciales; propagación a consultas de Dashboard, Cuentas y Movimientos; conservación de cobros y autorizaciones.
- Pruebas de UI: saldo cero distinto de ausencia de cuenta, alumno sin cuenta sin acciones, cancelar sin mutación, error de cantidad, aviso de saldo negativo, saldo concurrente que exige nueva revisión y éxito después de aplicar.
- `npm.cmd test`: 63 pruebas aprobadas en 11 archivos.
- `npm.cmd run build`: TypeScript y Vite terminaron correctamente.
- `git diff --check`: pasó; Git informó avisos LF/CRLF en documentos y archivos firmware locales ya modificados.
- Revisión real con navegador en ambos temas, escritorio y anchos 390/320 px; capturas versionadas: pendiente. El entorno de navegador/Chrome headless no estuvo disponible durante esta tarea; no se generaron capturas ni se declara validación visual.

## Pendientes

- Revisar visualmente perfil y formulario de ajuste con navegador real en tema claro y oscuro, escritorio y móvil a 390 y 320 px; comprobar wrapping, foco, advertencia negativa y ausencia de desbordamiento; guardar capturas versionadas sin sobrescribir las anteriores.
- Las reglas de PM.7A son solo de demostración. Cualquier ajuste real requiere definir actor/roles, persistencia, límites operativos, auditoría e integración API antes de conectar firmware.

No se modificaron firmware ni imágenes originales. No se hizo Upload, Commit ni Push.
