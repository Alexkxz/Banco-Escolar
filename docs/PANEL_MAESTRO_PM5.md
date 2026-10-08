# Banco Escolar · Panel Maestro · Fase PM.5

## Alcance

PM.5 implementa consulta de cuentas y movimientos del conjunto determinista ficticio de PM.3. Reutiliza `PanelDataService`; componentes no replican arreglos de muestra. PM.2–PM.4 conservan diseño, perfiles y selección de imagen temporal. Esta fase no añade escritura, operaciones monetarias, backend, persistencia escolar ni conexión a terminal.

Las diez rutas principales siguen vigentes. `/cuentas` lista las seis personas del conjunto, las cinco cuentas disponibles y a Teo (ID 206) como **Sin cuenta**. Las filas de cuenta muestran alumno, grado, grupo y saldo leído directamente de `StudentAccount`. El modelo no define un ID de cuenta independiente; para esta demo, el detalle `/cuentas/:accountId` usa `student_id` como ID de la cuenta existente. Una cuenta ausente no tiene saldo ni detalle ficticio. Hay enlaces entre cuenta y perfil.

El detalle de cuenta presenta saldo directo, movimientos del alumno ordenados por fecha descendente, y suma separada de entradas/salidas devueltas por la consulta. El resumen advierte que el historial disponible puede estar incompleto. Una consulta correcta vacía da totales 0; un error conserva el estado de error y ofrece reintento. Los totales nunca reconstruyen ni ajustan el saldo actual.

`/movimientos` muestra alumno, concepto, entrada/salida, importe y fecha. Combina filtros de alumno, tipo y días locales desde/hasta; informa contador, limpiar filtros, rango invertido y ausencia de coincidencias. `/movimientos/:movementId` muestra detalle o registro inexistente. Carga, error con reintento e historial vacío permanecen separados. Solo se vincula actividad/cobro si `ActivityClaim` real de la muestra referencia ese `movement_id` y la actividad correspondiente existe; PM.5 no administra esos módulos ni inventa relaciones.

## Zona horaria y fechas

Se conservan en los datos los timestamps Unix originales, en segundos. Dashboard, cuentas y movimientos muestran hora en español (`es-MX`) usando `America/Mexico_City`, con referencia de zona visible en la interfaz. El rango de fechas traduce la medianoche local a UTC mediante `Intl.DateTimeFormat` y esa zona IANA: incluye `fromInclusive` e interpreta el día final como el inicio exclusivo del día siguiente. No se resta un offset fijo. Si ambas fechas existen y el inicio es posterior al final, se informa el rango inválido y no se ejecuta ese filtro.

## Verificación

- `npm run test`: SUCCESS, 30 pruebas en cinco archivos. Incluye búsqueda sin acentos, relación cuenta/alumno/movimientos, saldo independiente de los totales, alumno sin cuenta, historial vacío/error, enlaces claim/activity, orden, filtros combinados y bordes de rango local.
- `npm run build`: SUCCESS con TypeScript y Vite; JavaScript 321.56 kB (98.20 kB gzip), CSS 28.53 kB (5.99 kB gzip).
- `git diff --check`: pendiente de la verificación final documental.
- Revisión visual y capturas PM.5 v05 de cuentas, detalle de cuenta, movimientos y detalle de movimiento, en escritorio 1366×768 y móvil 390×844/320×844, ambos temas: **pendiente**. Chrome headless terminó antes de exponer CDP (proceso Chrome salió; no hubo endpoint ni capturas). No se declara validación visual.

## Pendientes y límites

- Repetir la revisión visual y guardar capturas v05 cuando Chrome headless esté disponible. Las vistas son completamente nuevas frente a PM.4 y se deben revisar en ambos temas y anchos móviles.
- Aunque las consultas y estados se prueban con Vitest/Testing Library, falta revisar manualmente filtros, detalle, recarga, foco y layout desde un navegador.
- Datos, saldos, personas, IDs, timestamps y enlaces de cobro son demostrativos. Los tipos cliente no son contrato definitivo de API.
- No se modifica firmware ni imágenes originales. No se hizo Upload, Commit ni Push.
