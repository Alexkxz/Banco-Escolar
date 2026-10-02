# Interfaz

Verifica siempre el estado actual en `src/main.cpp`; la pantalla usa LVGL a 800×480 y admite temas LIGHT/DARK.

## Pantallas actuales

Incluye Inicio/espera, detección/demo de alumno, menú, cuenta, progreso, Fluidez lectora, Dictado de oraciones, metas, logros, transferencia y Configuración con Wi-Fi y almacenamiento local. Muchas vistas académicas, saldo, movimientos, logros y transferencias son datos o acciones demo.

La pantalla de espera conserva tarjeta, tres ondas NFC, ocho partículas, reloj, sombras estáticas, punto de espera fijo y borde fijo. Los pulsos de opacidad del punto y del borde se retiraron por rendimiento. El splash mantiene progreso de barra/aro/porcentaje coordinado; la hora real depende de NTP/fallback.

## Navegación y animaciones

La navegación normal actual crea pantalla opaca en (0,0), la carga sin fade/slide/zoom y elimina la anterior con `lv_obj_del_async()`. El feedback se limita al botón pulsado (escala LVGL 256 → 250 → 256, alrededor de 75 ms). El fade propio del splash es independiente. No reintroduzcas transiciones globales o pulsos sin solicitud y medición.

## Texto, fuente e iconos

UTF-8 se usa en cadenas; la fuente española DejaVuSans está convertida para LVGL 8.4, 16 px, 4 bpp. Si aparece un glifo cuadrado (por ejemplo, una letra acentuada), comprueba la cobertura real del font y la codificación del literal antes de reemplazar texto. Evita PUA, iconos Unicode sin verificar y símbolos como ⚙; prefiere formas LVGL o recursos gráficos probados. El engranaje de Configuración se dibuja como partes geométricas estáticas sobre el botón, no como una animación.

## Hábitos de edición

Los widgets LVGL se crean/modifican en el contexto admitido por el adaptador y respetando sus locks. Reutiliza patrones de estilo existentes y no cambies tamaños, refresh, tick ni buffers como efecto secundario de un ajuste de texto/UI.
