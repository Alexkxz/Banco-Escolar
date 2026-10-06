# Rendimiento

## Decisión conservada

Se atribuyó el stutter de Inicio principalmente a dos pulsos continuos de opacidad: el punto de espera y el borde del panel NFC. Ambos se eliminaron. No los restaures sin una nueva medición de frame timing.

Se conservan tarjeta flotante, tres ondas NFC, ocho partículas, reloj y sombras estáticas. Las partículas de Inicio usan movimiento vertical espaciado; las ondas y la tarjeta conservan sus animaciones. El reloj actualiza texto periódicamente.

## Mediciones de hardware reportadas

La prueba anterior de Inicio con todos los efectos midió promedio ~112.4 ms, máximo ~136.5 ms, 44/45 intervalos >100 ms. Después de quitar los pulsos, una prueba de 10.055 s registró 215 muestras, mínimo 38.9 ms, promedio 46.4 ms, máximo 56.5 ms, 29/215 >50 ms y 0 >100 ms. Mejora de promedio aproximada 58.7 %.

Estos son resultados físicos históricos proporcionados en las fases 4F; no son una garantía de cada build futuro. El P95 disponible era aproximado por categorías y no una medición exacta; no puede superar coherentemente al máximo observado.

## Instrumentación

Los flags actuales en `src/main.cpp` deben permanecer `false` en firmware normal:

- `PERFORMANCE_TEST_ENABLED`
- `FRAME_TIMING_TEST_ENABLED`
- `HOME_EFFECT_ISOLATION_TEST_ENABLED`
- `HOME_POST_OPT_TEST_ENABLED`

El código diagnóstico puede quedarse disponible. Activa una prueba solo con solicitud explícita y mide condiciones descritas por esa fase. No cambies LVGL refresh/tick, buffers de 40 líneas, RGB, GT911, CH422G o PSRAM por una optimización de animación sin aislar causa y medirla.

## Capas LVGL y feedback en controles grandes

En LVGL 8.4, una transformación de escala puede requerir una capa temporal completa que no se subdivide. No apliques escala táctil a tarjetas grandes si el buffer transformado puede exceder el pool LVGL disponible; para ellas usa borde, color o un acento estático. Los botones pequeños pueden conservar el feedback de escala breve.

La revisión de un `StoreProhibited` con `EXCVADDR=0x00000000` localizó un caso en `lib/lvgl/src/draw/sw/lv_draw_sw_layer.c`: `lv_mem_alloc()` podía devolver `NULL` y el código llamaba a `lv_memset_00()` antes de comprobarlo. El proyecto conserva un parche local que comprueba la asignación antes de inicializar el buffer y devuelve `NULL`; `lv_draw_layer_create()` y `refr_obj()` ya manejan ese retorno fallido. Las tarjetas grandes de progreso usan ahora feedback de borde para evitar la solicitud de una capa transformada superior al pool de 48 KiB. Revisar este parche si se actualiza LVGL y volver a contrastarlo con la versión nueva.
