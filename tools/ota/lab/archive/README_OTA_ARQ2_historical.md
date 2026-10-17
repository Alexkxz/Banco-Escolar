# ARCHIVADO — OTA-ARQ.2 laboratorio (sin instalación física)

> Este material es histórico y no forma parte del firmware normal. Los fuentes y pruebas específicos están en `archive/`; los perfiles PlatformIO retirados están en `archive/configs/`. No usar estos perfiles ni comandos para preparar una instalación.

Esta configuración genera dos artefactos separados:

- `platformio.lab-installer.ini`: instalador de un solo uso, destinado a ejecutarse desde `ota_0`.
- `platformio.lab.ini`: candidata destinada exclusivamente a `ota_1`, con validación de arranque OTA existente.

Ambos mantienen `BANCO_OTA_ENABLED=0`. Los directorios de compilación quedan fuera del proyecto. El instalador no inicia servicios: mantiene `ota_maintenance_active`, requiere LittleFS montado en solo lectura y sin transacciones incompletas, y espera la línea exacta `INSTALL OTA1 OTA-ARQ.2`.

## Identidad del instalador (OTA-DEV.9)

El `firmware.bin` generado por PlatformIO es la imagen ESP sin sellar y no es el artefacto apto para instalar. Antes de cualquier instalación física, el binario se sella fuera del proceso de compilación con `seal_lab_installer.mjs`, usando exclusivamente la clave privada de laboratorio ubicada fuera del repositorio. El resultado es un segundo archivo `firmware.lab-installer.bin`; el script no sobrescribe archivos y nunca imprime la clave.

El trailer `BANCOID1` contiene un manifiesto ECDSA P-256 que vincula modelo, versión, build ID, longitud exacta y SHA-256 de todos los bytes de la imagen ESP. En el arranque, el instalador comprueba la geometría completa de las seis particiones esperadas, que `ota_0` esté en ejecución y seleccionado, valida la imagen ESP, calcula el hash completo y verifica la firma con la clave pública de laboratorio compilada. Solo después consulta `otadata`: permite `VALID`, o `UNDEFINED` únicamente tras autenticar esa identidad. `NEW`, `PENDING_VERIFY`, `INVALID`, `ABORTED`, errores y valores desconocidos se rechazan.

Ejemplo de sellado, que solo crea un artefacto local y no abre un puerto:

```powershell
node tools/ota/lab/seal_lab_installer.mjs <firmware.bin> <firmware.lab-installer.bin> <clave-privada-lab.pem>
```

La firma de laboratorio demuestra identidad para este ensayo; no sustituye Secure Boot ni una raíz de confianza hardware y no es una clave de producción.

## Paquete autenticado

`create_lab_package.mjs` crea un paquete binario con prefijo de longitud de manifiesto de 16 bits little endian, JSON UTF-8 y después los bytes exactos del `.bin`. El manifiesto firma estos campos en este orden:

`BANCO-OTA-LAB-MANIFEST-V1`, modelo, versión, destino, offset, tamaño de slot, longitud de imagen, SHA-256, ID de build, versión agregada de datos y compatibilidad con rollback.

La firma es ECDSA P-256/SHA-256 en DER, codificada en Base64. El instalador verifica la firma antes de borrar/escribir `ota_1`, valida la imagen ESP, compara el SHA-256 del flujo y vuelve a leer y resumir exactamente `size` bytes en `ota_1`. Solo entonces ejecuta una llamada a `esp_ota_set_boot_partition(ota_1)`. La clave pública incluida en el árbol es **solo de prueba**; la clave privada de prueba se guarda fuera del repositorio y no es apta para producción.

`send_lab_package.py` valida localmente el encuadre, destino, tamaño y hash del paquete y puede transmitirlo por un puerto indicado explícitamente. Requiere la frase `--confirm-physical` exacta, no autodetecta puertos ni solicita DTR/RTS para reiniciar. El dispositivo vuelve a autenticar criptográficamente el manifiesto; la validación local del emisor no sustituye esa comprobación.

`rollback_compatible=true` es una declaración autenticada del firmante, no una prueba automática de equivalencia semántica de los datos. Se exige además el mismo `data_schema`; no se permiten migraciones durante `PENDING_VERIFY`.

## Comportamiento ante interrupción

- Antes de la llamada selectora: el boot seleccionado permanece en `ota_0`; un `ota_1` parcial no se arranca.
- Durante la actualización de `otadata`: IDF usa los dos registros alternos; tras reinicio debe elegirse el registro íntegro más reciente. Si permanece `ota_0`, la escritura puede repetirse; si queda seleccionada `ota_1`, su primer arranque queda sujeto al rollback.
- Si `ota_1` no confirma salud dentro de la ventana, el bootloader puede marcarla inválida y volver al slot anterior, siempre que la configuración y el estado de `ota_0` sean válidos. Esto requiere prueba física supervisada; el BUILD no lo demuestra.
- Si falla una comprobación o una API, el instalador deja mantenimiento activo y no solicita reinicio automático. Reiniciar manualmente no escribe flash.

## Limitaciones

- El instalador requiere que el slot en ejecución y seleccionado sea `ota_0`, que su estado sea `VALID`, y que la tabla coincida exactamente con `otadata=0xE000/0x2000`, `ota_0=0x10000/0x640000`, `ota_1=0x650000/0x640000`, `spiffs=0xC90000/0x360000`.
- El instalador obtiene huella y longitud de la imagen `ota_0` en ejecución y las vuelve a comparar después de escribir; así detecta alteración durante el proceso, pero no prueba que el firmware instalador sea la recuperación conocida.
- La orden local de consola confirma intención operativa; no sustituye autorización administrativa LOGIN-R. Este mecanismo es exclusivamente de banco/laboratorio.
- No se incluyen claves de producción, transporte USB automatizado, ni pruebas del comportamiento real del bootloader. No se modifica bootloader, tabla ni datos persistentes.
- La utilidad de paquete es software de preparación. No abre puertos y no escribe dispositivos.

## Recuperacion USB documentada (no ejecutada)

La herramienta oficial `components/app_update/otatool.py` de ESP-IDF v5.3.2 permite leer `otadata` y seleccionar un slot. Sintaxis validada contra el parser del script:

```powershell
python "$env:IDF_PATH\components\app_update\otatool.py" --port COMx --partition-table-file "<tabla.csv-o-bin>" read_otadata
python "$env:IDF_PATH\components\app_update\otatool.py" --port COMx --partition-table-file "<tabla.csv-o-bin>" switch_ota_partition --slot 0
```

`--partition-table-file` debe apuntar a la tabla que corresponde exactamente a la placa. Antes de cambiar el slot, registrar/respaldar `otadata` y confirmar que `ota_0` contiene la imagen recuperable. `switch_ota_partition --slot 0` modifica `otadata`; no es una consulta de solo lectura y puede reiniciar temporalmente el ESP32 al abrir el puerto. No usar `erase_otadata`, `write_ota_partition`, `erase_ota_partition` ni comandos de esptool de escritura como parte del procedimiento de selección.

Si el firmware nuevo no arranca o deja pantalla negra, el orden propuesto para una futura intervención autorizada es: detener la prueba, recuperar comunicación manual USB/BOOT si se requiere, hacer primero lecturas autorizadas de tabla, `otadata` y estado de imagen, y solo tras revisar evidencia cambiar el slot a `ota_0` con el comando anterior. Luego reiniciar manualmente y comprobar arranque, pantalla, touch y almacenamiento. No se afirma atomicidad absoluta de una operación USB interrumpida: conservar evidencia previa y no manipular los dos sectores de `otadata` directamente.

Esta documentación no ejecuta la herramienta y no habilita una operación física. La sintaxis y las funciones disponibles corresponden al script oficial fijado en ESP-IDF v5.3.2.
