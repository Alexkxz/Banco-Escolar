# Banco Escolar · Panel Maestro · Fase PM.4

## Alcance

PM.4 agrega el directorio y los perfiles de alumnos ficticios mediante `PanelDataService`, usando los seis alumnos de PM.3. La vista conserva el layout, temas y navegación existentes. Incluye búsqueda por nombre que ignora mayúsculas y acentos, filtros combinables por grado, grupo y disponibilidad de cuenta, contador, limpieza de filtros y estado sin coincidencias.

Cada perfil usa `/alumnos/:studentId`; el ID es la clave para el perfil y la selección temporal de imagen. Hay regreso al directorio, alumno inexistente y estados separados de carga, error con reintento y colección vacía. No se crean ni editan datos básicos.

## Datos y límites

El directorio muestra nombre, grado, grupo, estado y situación de cuenta. Los estados `ACTIVE`/`INACTIVE` se agregaron solo al modelo de cliente y a la demo para presentación. No son campo de `Student` del firmware ni contrato definitivo. Gael (ID ficticio 204) aparece inactivo para demostrar ambos estados; los otros cinco figuran activos. Teo (ID ficticio 206) sigue sin cuenta. La UI no muestra balance y no le asigna saldo cero.

Los datos proceden de `DemoPanelDataService`; la disponibilidad de cuenta se deriva de los IDs de `getAccounts()`, sin pasar balances a la vista del directorio. Los seis IDs y todos los datos continúan siendo ficticios. No se agregan operaciones monetarias, backend, sincronización, autenticación ni persistencia escolar.

## Imagen temporal de demostración

El perfil ofrece **Cargar imagen** solo como excepción de demostración. Se aceptan archivos PNG/JPEG con tipo MIME y firma coincidentes, decodificación correcta y dimensiones válidas. El límite es **5 MiB** y 25 megapíxeles por imagen para acotar carga y memoria del navegador. Un archivo inválido informa el error y conserva la imagen previamente elegida. Quitar imagen recupera las iniciales y el color determinista del avatar.

La selección se guarda únicamente en memoria del proveedor React, indexada por `student_id`; persiste al navegar por el panel y se pierde al recargar. Se informa junto al control que la imagen no se envía ni se sincroniza. Las URLs `blob:` se revocan al reemplazar, quitar o desmontar el proveedor. No se lee ni se modifica ninguna imagen original de alumnos.

### Vinculación futura panel–terminal

La vinculación futura debe referir la imagen al `student_id` estable e incluir una referencia de imagen y una versión, por ejemplo un identificador/versionado de contenido acordado por el protocolo. La interfaz solo debe marcarla sincronizada después de una transferencia y confirmación verificables. PM.4 no implementa selección persistente, almacenamiento definitivo, transferencia ni conversión.

La revisión de firmware encontró `Student.avatar_asset` como referencia opcional y avatares dibujados con formas LVGL; no encontró decodificación PNG/JPEG general, recepción de imágenes ni almacenamiento/transferencia de fotos. El proyecto también tiene recursos visuales embebidos para LVGL, con representación propia. Por esos límites no se propone todavía un formato final: antes hay que estudiar RAM/PSRAM, flash y particiones, capacidad de LittleFS, decoder disponible, dimensiones y costo de conversión a los recursos que LVGL pueda mostrar. Esta nota es arquitectura pendiente, no contrato de API.

## Pruebas y revisión

- `npm run test`: SUCCESS, 18 pruebas; incluyen búsqueda sin acentos, filtros combinados, perfil por ID, alumno sin cuenta, firma/tipo/tamaño/decodificación de PNG y JPEG, conservación y retiro de vista previa, liberación de URLs temporales, y colección vacía separada de error.
- `npm run build`: SUCCESS con TypeScript y Vite; JavaScript 301.30 kB (94.86 kB gzip), CSS 22.00 kB (5.13 kB gzip).
- `git diff --check`: SUCCESS; no detecta errores. Git muestra avisos LF→CRLF preexistentes en documentos/archivos locales modificados.
- Revisión visual y capturas v04 de directorio y perfiles (iniciales e imagen genérica) en escritorio 1366×768 y móvil 390×844/320×844, ambos temas: **pendiente**. Chrome headless se cerró al iniciar por error de acceso denegado en el proceso GPU (`GPU process isn't usable`); no produjo capturas. No se declara validación visual.

## Pendientes

- Repetir la revisión visual de directorio y perfiles en seis combinaciones de viewport/tema cuando Chrome headless esté disponible; guardar capturas v04 sin sobrescribir las versiones previas.
- Revisar manualmente el flujo de selección, cambio, retiro y persistencia solo durante navegación. La suite simula selección de archivo y validación, pero no sustituye esa revisión visual/interactiva del navegador.
- Definir en una fase futura protocolo y almacenamiento de imagen, formato compatible con firmware y confirmación de transferencia; no asumir compatibilidad PNG/JPEG en la terminal.
- Los datos, estados e imágenes de esta fase son de demostración. No representan alumnos reales ni conectividad.

No se modificó firmware ni imágenes originales. No se hizo Upload, Commit ni Push.
