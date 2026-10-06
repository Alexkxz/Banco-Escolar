# Interfaz

Verifica siempre el estado actual en `src/main.cpp`; la pantalla usa LVGL a 800×480 y admite temas LIGHT/DARK.

## Pantallas actuales

Incluye Inicio/espera, detección/demo de alumno, menú, cuenta, historial real de movimientos, progreso, Fluidez lectora, Dictado de oraciones, metas, logros, transferencia y Configuración con Wi-Fi y almacenamiento local. Las pantallas principales comparten el encabezado `[logo] + título contextual`: el logo compacto se muestra en una cápsula clara de 60 × 60 px y el título, en fuente española de 16 px, alineado a su derecha; no se duplica el texto BANCO ESCOLAR. Fecha y hora permanecen en el bloque derecho con el contenido y jerarquía aprobados; en Inicio, Salir es una acción secundaria visible y táctil (104 × 44 px), integrada junto al reloj. El encabezado usa fondo marino y línea inferior cian. El resto de la interfaz usa la escala de espaciado 8/12/16/24. El menú usa dos columnas con tarjetas equivalentes e iconos uniformes. Botones secundarios como Volver miden 120 × 48 px y quedan abajo a la izquierda.

El historial de Mi cuenta consulta bloques de cuatro registros de LittleFS en orden más reciente primero y filtra por el `student_id` del alumno seleccionado; resuelve su nombre con `getStudentById()`. La vista es de solo lectura. Las filas usan dos columnas: alumno, motivo y fecha/hora a la izquierda; cantidad resaltada, tipo, estado e ID diferenciados a la derecha. La paginación se agrupa en una barra y conserva visibles los controles deshabilitados. El saldo de Inicio y Mi cuenta sigue siendo demo; varias vistas académicas, logros y transferencias también siguen siendo demo.

Fase 5B.2 añade filtros temporales Todos, Entradas (`amount > 0`), Salidas (`amount < 0`) y Pendientes (`synced == false`). Al entrar se cargan en memoria los registros del alumno seleccionado; los filtros, el resumen y la paginación usan esa lectura sin escribir datos ni cambiar el modelo. `amount == 0` no cuenta como entrada ni salida. El resumen cuenta entradas, salidas y pendientes de todo el historial cargado del alumno, no suma importes ni calcula saldo. Cada filtro reinicia la página a 1 y los estados vacíos tienen mensajes específicos. Validada físicamente.

La pantalla de espera conserva tarjeta, tres ondas NFC, ocho partículas, reloj, sombras estáticas, punto de espera fijo y borde fijo. Los pulsos de opacidad del punto y del borde se retiraron por rendimiento. El splash presenta `Logo y nombre.png` como imagen LVGL estática RGB565 con alpha, centrada arriba a 640 × 240 px; el texto de marca separado se retiró para evitar duplicación. No tiene aro circular de progreso ni icono de banco central; la barra, el porcentaje y los mensajes de estado mantienen sus posiciones, y la hora real depende de NTP/fallback.

La Fase 5B.3 establece una paleta común inspirada en el logo: marino `#0B2B59`, azul `#1267D6`, eléctrico `#1E88FF`, cian `#27D7FF`, dorado `#F4B82E`, fondo `#F3F8FD`, superficie blanca, texto `#17324D` y secundario `#557086`; LIGHT y DARK siguen disponibles. Las pantallas internas usan una franja superior marino con línea cian, cápsula clara para `logo_banco_escolar`, título contextual claro, fecha/hora y Salir donde aplica. `logo_y_nombre` permanece exclusivo del splash.

Tarjetas usan superficie neutra, borde fino, radio de 16 px y acento estático discreto; botones primarios, secundarios y de peligro comparten radios y áreas táctiles. Volver se presenta abajo a la izquierda en control de 120 × 48 px; Salir conserva 104 × 44 px. Los paneles y encabezados secundarios Wi-Fi/almacenamiento mantienen sus callbacks, con una cabecera compacta para ajustarse a sus formularios. Historial mantiene filtros y datos reales de solo lectura, con resumen por conteos y filas espaciadas. Metas y logros elevan visualmente sus datos existentes; el logro sigue siendo de demostración. Gráficas, transferencia, configuración y almacenamiento reutilizan la paleta, sin añadir animaciones continuas ni alterar valores o reglas.

Fase 5B.3: rediseño visual global integrado en código y documentación y validado físicamente en el panel de 800 × 480. Se conservaron callbacks y navegación; LIGHT y DARK siguen disponibles. Para controles grandes, el feedback táctil usa un borde en vez de escalado que requiera una capa temporal grande; los botones pequeños conservan la escala breve.

## Navegación y animaciones

La navegación normal actual crea pantalla opaca en (0,0), la carga sin fade/slide/zoom y elimina la anterior con `lv_obj_del_async()`. El feedback se limita al control pulsado: los botones pequeños usan escala LVGL 256 → 250 → 256 por unos 75 ms; las tarjetas grandes usan borde cian estático para evitar capas transformadas mayores al pool LVGL. El fade propio del splash es independiente. No reintroduzcas transiciones globales o pulsos sin solicitud y medición.

## Texto, fuente e iconos

UTF-8 se usa en cadenas; la fuente española DejaVuSans está convertida para LVGL 8.4, 16 px, 4 bpp. Si aparece un glifo cuadrado (por ejemplo, una letra acentuada), comprueba la cobertura real del font y la codificación del literal antes de reemplazar texto. Evita PUA, iconos Unicode sin verificar y símbolos como ⚙; prefiere formas LVGL o recursos gráficos probados. El engranaje de Configuración se dibuja como partes geométricas estáticas sobre el botón, no como una animación.

## Hábitos de edición

Los widgets LVGL se crean/modifican en el contexto admitido por el adaptador y respetando sus locks. Reutiliza patrones de estilo existentes y no cambies tamaños, refresh, tick ni buffers como efecto secundario de un ajuste de texto/UI.
