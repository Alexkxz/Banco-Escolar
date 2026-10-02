# Hardware y configuración de placa

## Estado confirmado

- Placa física: Waveshare ESP32-S3-Touch-LCD-7 Rev 1.2, MCU ESP32-S3.
- LCD RGB 800×480 con ST7262; expansor de E/S CH422G; touch GT911.
- Flash física reportada por las pruebas de carga: 16 MB; PSRAM física: 8 MB; Serial: 115200.
- La configuración de la placa está en `src/esp_panel_board_custom_conf.h`; la compilación está en `platformio.ini`.

## Diferencia entre etiqueta de PlatformIO y el equipo

PlatformIO selecciona `esp32-s3-devkitc-1`, una definición nominal N8 sin PSRAM. El hardware físico es el módulo Waveshare con 16 MB y 8 MB de PSRAM. El proyecto fuerza `board_upload.flash_size = 16MB`, `board_build.flash_size = 16MB`, `board_build.partitions = default_16MB.csv`, `board_build.arduino.memory_type = qio_opi` y `BOARD_HAS_PSRAM`.

No cambies estas opciones solo para que coincidan con la etiqueta de la board. La configuración real debe basarse en el módulo y en los resultados de carga/compilación.

## LCD, touch e I²C0

- El código configura ST7262 por RGB, 800×480, RGB565 de 16 bits.
- GT911 y CH422G comparten I²C0; SDA GPIO8, SCL GPIO9, touch INT GPIO4. El expansor aparece en la configuración como CH422G en GPIO8/9.
- Historial físico conocido: durante el inicio apareció `Unable to initialize the I2C address`, pero luego se detectaron TouchPad_ID y configuración válidos y `Board begin success`. Mientras el touch funcione, no cambies GT911/I²C0 solo para ocultar ese warning.
- Historial físico conocido: `esp_lcd_rgb_panel_get_frame_buffer: invalid frame buffer number` / `GetRGB buffer failed`. La pantalla siguió funcionando con dos buffers internos de 40 líneas en el adaptador `src/esp_lv_adapter_arduino.cpp`. No cambies driver RGB, PSRAM, DMA ni buffers solo para ocultar el mensaje; investiga en una fase específica.

## microSD

Asignación Waveshare reportada: MOSI GPIO11, SCK GPIO12, MISO GPIO13, CS por EXIO4. La tarjeta no está instalada. `SDManager` está preparado, pero debe permanecer sin montaje físico, escrituras ni formato hasta una fase autorizada. No declares un límite oficial de capacidad no verificado.

## Fuente de estos datos

La asignación de pines y drivers debe verificarse en `src/esp_panel_board_custom_conf.h`; memoria física, revisión de placa y resultados de warnings proceden del historial de pruebas del proyecto, no de `platformio.ini`.
