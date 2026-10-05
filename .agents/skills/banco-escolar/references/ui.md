# Interfaz

Verifica siempre el estado actual en `src/main.cpp`; la pantalla usa LVGL a 800×480 y admite temas LIGHT/DARK.

## Pantallas actuales

Incluye Inicio/espera, detección/demo de alumno, menú, cuenta, historial real de movimientos, progreso, Fluidez lectora, Dictado de oraciones, metas, logros, transferencia y Configuración con Wi-Fi y almacenamiento local. Inicio, Mi cuenta e Historial comparten el encabezado `[logo] + título contextual`: el logo compacto de 56 × 54 px queda a la izquierda y el título, en fuente española de 16 px, alineado a su derecha; no se duplica el texto BANCO ESCOLAR. Fecha y hora permanecen en el bloque derecho con el contenido y jerarquía aprobados; en Inicio, Salir es una acción secundaria visible y táctil (104 × 44 px), integrada junto al reloj. El encabezado usa márgenes consistentes y superficies de borde neutro fino. El resto de la interfaz usa la escala de espaciado 8/12/16/24. El menú usa dos columnas con tarjetas equivalentes, iconos uniformes y sin barras decorativas inferiores. Botones secundarios como Volver son compactos y conservan área táctil cómoda.

El historial de Mi cuenta consulta bloques de cuatro registros de LittleFS en orden más reciente primero y filtra por el `student_id` del alumno seleccionado; resuelve su nombre con `getStudentById()`. La vista es de solo lectura. Las filas usan dos columnas: alumno, motivo y fecha/hora a la izquierda; cantidad resaltada, tipo, estado e ID diferenciados a la derecha. La paginación se agrupa en una barra y conserva visibles los controles deshabilitados. El saldo de Inicio y Mi cuenta sigue siendo demo; varias vistas académicas, logros y transferencias también siguen siendo demo.

La pantalla de espera conserva tarjeta, tres ondas NFC, ocho partículas, reloj, sombras estáticas, punto de espera fijo y borde fijo. Los pulsos de opacidad del punto y del borde se retiraron por rendimiento. El splash presenta `Logo y nombre.png` como imagen LVGL estática RGB565 con alpha, centrada arriba a 640 × 240 px; el texto de marca separado se retiró para evitar duplicación. No tiene aro circular de progreso ni icono de banco central; la barra, el porcentaje y los mensajes de estado mantienen sus posiciones, y la hora real depende de NTP/fallback.

## Navegación y animaciones

La navegación normal actual crea pantalla opaca en (0,0), la carga sin fade/slide/zoom y elimina la anterior con `lv_obj_del_async()`. El feedback se limita al botón pulsado (escala LVGL 256 → 250 → 256, alrededor de 75 ms). El fade propio del splash es independiente. No reintroduzcas transiciones globales o pulsos sin solicitud y medición.

## Texto, fuente e iconos

UTF-8 se usa en cadenas; la fuente española DejaVuSans está convertida para LVGL 8.4, 16 px, 4 bpp. Si aparece un glifo cuadrado (por ejemplo, una letra acentuada), comprueba la cobertura real del font y la codificación del literal antes de reemplazar texto. Evita PUA, iconos Unicode sin verificar y símbolos como ⚙; prefiere formas LVGL o recursos gráficos probados. El engranaje de Configuración se dibuja como partes geométricas estáticas sobre el botón, no como una animación.

## Hábitos de edición

Los widgets LVGL se crean/modifican en el contexto admitido por el adaptador y respetando sus locks. Reutiliza patrones de estilo existentes y no cambies tamaños, refresh, tick ni buffers como efecto secundario de un ajuste de texto/UI.
