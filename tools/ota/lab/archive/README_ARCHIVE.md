# OTA-LAB histórico (archivado)

El firmware normal ya no incorpora el bootstrap ni el instalador OTA-LAB. Los cinco archivos de firmware se conservan bajo `firmware_src/network/` únicamente como referencia histórica y quedan fuera de `src/`, por lo que PlatformIO no los compila.

Los contratos y pruebas exclusivos de OTA-LAB se conservan en `tests/`. Los perfiles PlatformIO de laboratorio se conservan en `configs/`, pero no son perfiles soportados: sus hooks ya no están integrados en el firmware normal. No usarlos para compilar ni instalar firmware.

Las herramientas y manifiestos históricos permanecen en el directorio padre `tools/ota/lab/`. La ruta OTA de producción continúa en `src/network/ota_manager.*`, con `BANCO_OTA_ENABLED=0`.
