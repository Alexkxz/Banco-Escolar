# Banco Escolar · Panel Maestro · Fase PM.8

## Alcance

PM.8 activa **Progreso académico** (`/progreso`), **Asistencia** (`/asistencia`) y **Configuración escolar** (`/configuracion`) sobre las seis identidades ficticias existentes y únicamente en `DemoPanelDataService`. Alumnos, cuentas, movimientos, reclamos, recompensas de actividad e imágenes de perfil de fases previas se conservan. Capturas y correcciones guardan datos en la sesión; guardar una evaluación o asistencia no aplica dinero. La acción **Aplicar Áureos** es independiente.

Registros, reglas y aplicaciones escolares solo existen en memoria, se comparten al navegar por la aplicación, se pierden al recargar y no se envían a la terminal. Los tipos TypeScript y esta simulación no constituyen un contrato de API.

## Progreso académico

Cada tipo permite un registro por alumno, fecha escolar y tipo. Al capturar de nuevo esa combinación se corrige el registro existente y aumenta su versión; su ID se conserva. La consulta presenta historial con filtros combinables por alumno, fecha inicial/final y tipo, estados distintos de carga, error con reintento, colección vacía y filtros sin coincidencias.

La vista analítica se divide en **Resumen**, **Fluidez lectora**, **Dictado** y **Comprensión**; abre Resumen por defecto. El selector **Todo el grupo / Alumno** y los filtros compartidos de grado, grupo y período controlan las gráficas y el historial, y conservan su selección al cambiar de subpestaña. Para las distribuciones generales se toma el registro más reciente de cada alumno dentro del período, según fecha escolar y, en empate, instante/ID de captura. Las tendencias individuales usan todos los registros del período y muestran puntos sin unir cuando hay menos de tres observaciones.

Cada gráfica incluye unidad, período, muestra, etiquetas y una tabla accesible construida con las mismas filas y valores. Las tablas anchas se desplazan dentro de su propio contenedor en móvil. Fluidez general separa grupos por grado y usa los umbrales de la evaluación guardados para ese grado. Dictado conserva porcentajes sin nivel mientras sus rangos sigan sin configurar. Comprensión clasifica cada aspecto como Logrado (verde), En desarrollo (amarillo) o Requiere apoyo (rojo), y presenta aparte los aspectos Sin evaluar; no genera un resultado ponderado con pesos no configurados. Los enlaces de cada tendencia o gráfica llevan al historial filtrado, donde se puede consultar, capturar/corregir y revisar por separado la aplicación de Áureos.

- **Lectura:** captura PPM entero de 0 a 65,535 y clasifica con los límites inferiores para el grado del alumno. Se cargaron los límites proporcionados por el usuario: 1.º 15/35/60; 2.º 35/60/85; 3.º 60/85/100; 4.º 85/100/115; 5.º 100/115/125; 6.º 115/125/135. Las cuatro bandas son continuas, no se superponen y las fronteras pasan al nivel siguiente. No se verificaron como estándares oficiales. Los límites se pueden editar.
- **Dictado:** requiere total mayor que cero y palabras correctas entre cero y el total; calcula el porcentaje correcto con decimales. Sus tres fronteras de nivel porcentual empiezan **Sin configurar** y se editan como bandas contiguas de 0 a 100 %.
- **Comprensión:** registra aciertos literal, inferencial y crítico contra denominadores configurables que comienzan en 3. Un aspecto vacío se mantiene **Sin evaluar**, no se transforma en cero ni entra al total bruto. Con las tres preguntas iniciales, 3/3 se presenta como “Logrado”, 2/3 “En desarrollo” y 0–1/3 “Requiere apoyo”, con texto y color. Al cambiar denominadores, la clasificación usa 100 %, al menos dos tercios y debajo de dos tercios del denominador, respectivamente. El total bruto muestra aciertos/preguntas solo de los aspectos evaluados. El resultado general ponderado usa las proporciones de cada aspecto y normaliza con los pesos de los aspectos evaluados.
- Los pesos literal/inferencial/crítica comienzan **Sin configurar** y deben sumar 100 %. Los límites del nivel general ponderado comienzan **Sin configurar**. Se puede elegir aplicación monetaria **por nivel de cada aspecto** o **por resultado general**; no se permite combinar ambos métodos para un mismo registro, incluso si se cambia de modalidad después de una aplicación.

## Configuración

La vista presenta límites de lectura por grado, bandas porcentuales de dictado y resultado general, denominadores, pesos, modalidad de aplicación y reglas de asistencia. Cada nivel monetario permite entero positivo, negativo o cero. Un campo vacío sigue **Sin configurar** y bloquea la aplicación correspondiente. Las bandas se describen por tres límites inferiores crecientes dentro de su escala: de 0 al primer límite, de cada límite al siguiente y del tercero a 100 %/el máximo de lectura; por diseño no hay huecos ni superposiciones. Se admiten fronteras porcentuales decimales.

Las configuraciones se revisan y confirman. El servicio vuelve a validar bandas, suma exacta de pesos, denominadores e importes enteros, y asigna una versión creciente en memoria. Cambiar reglas solo afecta aplicaciones posteriores: no modifica evaluaciones, aplicaciones, saldos ni movimientos anteriores.

Los importes de recompensas de lectura, dictado, comprensión y resultado general comienzan sin configurar. Los valores iniciales de asistencia sí están configurados conforme a los valores proporcionados: primera llegada puntual +5; retardo hasta el límite inclusive −5; retardo posterior −10; falta injustificada −20; otros presentes puntuales, faltas justificadas y sin registrar, cero. Horarios e importes se pueden cambiar para aplicaciones futuras.

## Asistencia

El índice único de demo es alumno y fecha escolar `America/Mexico_City`. Una fecha sin registro se muestra **Sin registrar**, nunca se convierte en falta automáticamente. Los estados explícitos son Presente, Retardo, Falta injustificada y Falta justificada. Para el día actual, **Marcar llegada ahora** toma la hora de la computadora; no presenta un campo editable y rechaza una segunda captura que sobrescriba la primera. Esa hora es una captura del Panel Maestro, no de NFC. La presencia/retardo se clasifica usando el instante completo y el horario vigente.

Para días históricos se puede consultar o guardar/corregir falta justificada o injustificada, pero el servicio no permite crear una llegada con hora ficticia. Corregir una justificación no devuelve importes anteriores ni hace disponible una segunda aplicación.

El primer premio se calcula con las llegadas puntuales del mismo día. Llegadas con el mismo segundo empatan y todas reciben el valor configurado; la hora completa se conserva para contrastar 08:00 y 08:10. Llegar exactamente a 08:00 no recibe descuento; cualquier instante posterior y hasta 08:10 inclusive usa el descuento de retardo corto; después de 08:10 usa el tardío. El primero que llega tarde nunca recibe premio de primer lugar. Si una corrección o una regla nueva cambia una aplicación previa, la interfaz avisa y conserva la aplicación y el movimiento original.

## Aplicación de Áureos y correcciones

Lectura, dictado, cada aspecto de comprensión en modalidad por aspecto, resultado general en su modalidad, y asistencia se aplican de forma separada. El servicio demo calcula el importe, saldo resultante, versión y firma del resultado. La revisión previa muestra alumno, resultado, versión de regla, desglose/importe, saldo actual y saldo final. Permite saldo negativo y lo advierte. Un importe cero crea un registro de aplicación sin movimiento de importe cero. Un alumno sin cuenta bloquea la operación y no recibe una cuenta automática.

Al confirmar, el servicio vuelve a leer registro, versión de reglas y saldo. Cualquier diferencia requiere una propuesta revisada. Un ID de registro e indicador solo se aplica una vez, incluso si luego se corrige el registro o cambia la configuración. Para comprensión se pueden aplicar aspectos distintos, pero no se mezcla el modo por aspectos con el modo general para el mismo registro. Saldo, movimiento no nulo y aplicación se preparan antes del punto de commit y se actualizan juntos; los errores de prueba no dejan cambios parciales. El saldo parte del snapshot actual de `StudentAccount`, nunca del historial.

Cada aplicación conserva ID, alumno, registro e indicador, versión del registro, versiones académica/asistencia, resultado y firma usados, importe, balances anterior/nuevo, timestamp y vínculo al movimiento. No crea `ActivityClaim`. Una evaluación o asistencia corregida conserva aplicaciones y movimientos anteriores y muestra una discrepancia si su versión cambia; no recalcula ni genera reembolsos. El aviso ofrece abrir el ajuste PM.7A del perfil con referencia al ID escolar; el movimiento manual demo conserva ese ID de registro. Justificar una falta ya descontada no aplica un reembolso ni habilita otra aplicación.

Dashboard, Cuentas, Movimientos y perfil consultan la misma instancia del servicio. Por eso, una aplicación no nula aparece como saldo directo actualizado y nuevo movimiento al volver a esas vistas; una aplicación cero solo aparece en historial de aplicaciones escolares.

## Diferencias respecto al firmware

`src/student_model.h` define `ReadingRecord` con `uint16_t ppm`, `WritingRecord` con total de palabras y errores, y `AttendanceRecord` con fecha/hora/estado textual. `src/academic_config.h` contiene los límites iniciales de lectura que ya existían en el firmware. La forma PM.8 de capturar palabras correctas y calcular porcentaje extiende la presentación demo; no cambia `WritingRecord`. No se encontró un modelo de comprensión ni una configuración escolar persistida equivalente.

`src/storage/storage_manager.h` tiene `StoredAttendanceRecord` de marca temporal y tipo `CHECK_IN`/`CHECK_OUT`, no los cuatro estados escolares de PM.8 ni falta justificada. El reloj y la fecha de llegada del Panel son del computador, no una observación de la terminal. Las reglas y versiones académicas/asistencia, versiones de evaluación, firmas, applications y sus enlaces son exclusivos del cliente demo. `MovementRecord.origin = DEMO` también es exclusivo del cliente; el firmware no fue modificado.

## Ajuste visual de Progreso académico (2026-10-07)

Se reorganizó la página conservando las cuatro subpestañas y filtros compartidos. Resumen presenta primero cobertura evaluada/pendiente por indicador y después accesos compactos a los resultados. El aviso demo y filtros ocupan menos espacio. La metodología se agrupa en “Cómo se calculan los resultados”; período y muestra permanecen visibles.

Los estados sin datos ya no generan gráficas con barras vacías. El mensaje distingue la falta global de registros de filtros sin coincidencias, e incluye acceso para registrar el indicador correspondiente. “Sin evaluar” queda fuera de las bandas académicas y se presenta como cobertura pendiente. Las distribuciones horizontales usan escalas comunes en conteos comparables; las tarjetas analíticas forman dos columnas en escritorio y una en móvil. Dictado conserva escala porcentual fija de 0–100 y, solo con rangos configurados, añade su distribución por los cuatro niveles. Comprensión muestra Literal, Inferencial y Crítica por sus tres niveles; aspectos sin dato se conservan como pendientes. No se alteraron rangos, ponderaciones ni reglas monetarias.

Las tablas de cada gráfica se abren con “Ver datos de la gráfica”, usan encabezados específicos y alinean numéricamente sus valores. Las tendencias muestran unidad vertical, fechas horizontales y puntos aislados para una sola medición. La captura/corrección se encuentra en un disclosure y su estado se conserva en memoria al cerrarlo y cambiar de indicador; “Registrar evaluación” selecciona Fluidez, Dictado o Comprensión en sus respectivas subpestañas. Resumen conserva la elección del indicador en el formulario. Guardar evaluación y Aplicar Áureos permanecen separados.

## Pruebas y resultados

- `npm.cmd test`: **82 pruebas aprobadas en 13 archivos**, incluida la navegación por teclado de las pestañas, filtros compartidos, concordancia de gráfica/tabla, cobertura parcial, cero real y formulario por indicador.
- `npm.cmd run build`: TypeScript y Vite completaron correctamente.
- `git diff --check`: aprobado; Git reportó avisos de conversión LF/CRLF para algunos archivos locales existentes.
- Revisión en Chrome headless: las cuatro subpestañas se revisaron en 1440×844, 390×844 y 320×844, con tema claro y oscuro, tanto sin registros como con cinco registros ficticios temporales (incluye 0 PPM, 0 % correcto y comprensión parcial). Las 48 capturas v04 quedaron en `panel-maestro/capturas/escritorio` y `panel-maestro/capturas/movil`; las capturas con `datos` muestran las gráficas pobladas. El documento no excedió el ancho visible en ninguno de los tamaños. La navegación por teclado mostró el anillo de foco al avanzar con Tab en los 48 casos; las pruebas cubren flechas, Home y End en las subpestañas. Las tablas plegables conservaron los valores de sus gráficas en las pruebas.
- Las capturas de prueba usan solamente datos temporales de la instancia de demostración durante la sesión headless; no se agregaron registros al conjunto inicial. Dictado conserva sus rangos **Sin configurar**, así que se revisaron porcentajes (0–100 %) sin asignar niveles. Comprensión general omitió el aspecto no evaluado del gráfico y lo mostró como pendiente gris.

## Pendientes

- No se realizó una evaluación con lector de pantalla. La verificación visual y de teclado reportada corresponde a Chrome headless, no a una revisión manual con tecnología asistiva.
- La modalidad de dictado con rangos configurados mantiene su representación mediante cuatro niveles; los rangos siguen Sin configurar por defecto y no se modificaron en este ajuste.
- No hay persistencia, API, autorización, identidad de usuario ni sincronización. Los movimientos quedan identificados como demo y no se atribuyen a la terminal.

No se modificaron firmware ni imágenes originales. No se hizo Upload, Commit ni Push.
