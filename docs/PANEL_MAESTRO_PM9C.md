# Banco Escolar · Panel Maestro · Fase PM.9C

## Alcance y estado

PM.9C implementa respaldo y restauración local del espacio demo del Panel Maestro. El espacio activo permanece seleccionado: un archivo de otro espacio se rechaza y la restauración nunca cambia al modo real ni mezcla sus colecciones. No hay cifrado ni copia remota.

La configuración ofrece **Descargar respaldo** y **Restaurar respaldo**. Antes de validar el archivo seleccionado, el Panel crea y descarga una copia nueva del estado vigente. Informa que el navegador inicia la descarga, pero no puede garantizar que la persona conserve el archivo. La vista previa muestra fecha, espacio, número de registros por colección y fotos. Dos confirmaciones explícitas preceden al reemplazo. Cancelar no escribe datos. Si la revisión IndexedDB cambia tras esa descarga, se invalida la confirmación y se exige repetir el flujo.

La restauración reemplaza todas las colecciones del espacio seleccionado en una transacción nativa `readwrite` de IndexedDB. No mezcla filas ni ejecuta reglas monetarias: saldos, movimientos, aplicaciones escolares, comprobantes e historiales se restauran tal como estaban en el archivo. Un aborto conserva el estado anterior. Al completar, se recargan las vistas y se desmonta el proveedor de fotos anterior para liberar sus URLs temporales.

## Formato y validación

Formato JSON versionado:

- `format`: `banco-escolar-panel-backup`.
- `version`: `1` (versión explícita del formato).
- `workspace`: nombre exacto de la base/espacio; actualmente solo `banco-escolar-panel-demo`.
- `schemaVersion`, `createdAt` (ISO 8601), `revision`, `collections` y `checksum` SHA-256 del payload canónico.
- `collections` contiene `meta`, `students`, `studentImages`, `accounts`, `movements`, `activities`, `claims`, `claimAuthorizations`, `claimEvents`, `academicRecords`, `attendanceRecords`, `academicRuleVersions`, `attendanceRuleVersions`, `schoolApplications` y `operationReceipts`.
- Las fotos se codifican en Base64 con MIME, tamaño, nombre, dimensiones y fecha; al importar vuelven a ser `Blob`.

Antes de escribir, el lector comprueba JSON, formato y versión compatibles, espacio y esquema, checksum, colecciones exactas, límites, claves únicas, estructura y rangos de valores, referencias entre colecciones, metadatos/revisión y bytes de fotos. Las fotos requieren PNG/JPEG decodificable, firma y dimensiones verificadas.

Límites explícitos: archivo completo 100 MiB; 10,000 filas por colección; 1,000 fotos; 5 MiB por foto; 25,000,000 píxeles por imagen. Se rechaza el archivo entero si excede un límite. Un error produce mensaje y no modifica IndexedDB.

## Verificación comprobada (08/10/2026)

`npm.cmd test -- --reporter=dot`: 13 archivos y 83 pruebas aprobadas. `npm.cmd run test:browser:pm9c`: aprobado con Chromium de Playwright, IndexedDB nativo y perfil temporal aislado; el runner lo cerró y eliminó después de confirmar la salida. Escenarios ejecutados:

1. Exportación con todas las colecciones y Blob original; restauración exacta de datos y foto.
2. JSON malformado, colección faltante, valor decimal inválido, versión futura, espacio incorrecto, referencia de alumno rota y foto inválida: rechazo sin escrituras.
3. Aborto inyectado en transacción IndexedDB durante `movements.put`: estado previo y foto intactos.
4. Cancelación de la vista previa sin cambios.
5. Cambio desde otra pestaña tras la descarga de seguridad: revisión obsoleta invalidada y transacción más reciente conservada.
6. Doble confirmación: una sola restauración. Aplicaciones escolares, comprobantes, saldo y movimientos coinciden con el respaldo; no se añadieron movimientos ni aplicaciones ni se recalculó el saldo.
7. Cierre y reapertura de Chromium con el mismo perfil aislado: registros y foto continuaron presentes.
8. Revisión visual de Configuración en escritorio y 390/320 px, temas claro/oscuro; capturas versionadas y foco visible mediante Tab en los seis tamaños/temas.

Las capturas visuales de esta ejecución y sus iteraciones anteriores se conservaron localmente y se excluyen de la publicación porque muestran valores de configuración escolar que no se han autorizado para difusión. La revisión visual cubrió escritorio y 390/320 px, temas claro/oscuro y foco visible mediante Tab.

`npm.cmd run build`: TypeScript y Vite SUCCESS. `git diff --check`: SUCCESS, sin errores de whitespace. El build marcó `index-DyZot8Bm.js` en 516.03 kB minificados (143.66 kB gzip), sobre el aviso predeterminado de 500 kB. El aviso desapareció después de separar dinámicamente la ruta de Configuración, que contiene respaldo/importación: el chunk principal quedó en 498.87 kB minificados (139.57 kB gzip) y el chunk de Configuración en 17.85 kB (5.47 kB gzip). El límite predeterminado de 500 kB no se modificó.

El runner corrigió además el chequeo de 4187. La captura de PowerShell confirmó que la consulta sin coincidencias devolvía exit code 1 y stderr CIM en español (“no encontró objetos MSFT_NetTCPConnection que coincidan”). Eso significaba puerto libre, pero el script anterior podía tratarlo como consulta fallida. Ahora reconoce solo el mensaje CIM de ausencia (inglés/español); errores de consulta y permisos conservan el código de salida y stderr, y un listener ocupado se identifica por dirección, puerto, PID y datos CIM sin cerrar procesos ajenos.

## Pendientes

No se probaron el límite de 100 MiB con un archivo real de ese tamaño ni cuotas agotadas del navegador. Tampoco se probó recuperación ante cierre abrupto del proceso o pérdida de energía durante la transacción. Se admite únicamente formato 1 y esquema IndexedDB compatible actual; futuras migraciones exigirán una versión explícita nueva. El navegador no confirma que la descarga se haya guardado fuera del dispositivo.
