# DIAG.1 — Captura de pantalla por USB

## Alcance y estado

DIAG.1 implementa una captura del contenido que LVGL entrega al adaptador de pantalla, la reconstruye en RGB565 y la transfiere por USB Serial al receptor para Windows. El mismo mecanismo se solicita desde el comando `DIAG1 CAPTURE` o desde **Menú Maestro → Capturar por USB**. No ejecuta operaciones monetarias, de actividad, alumno o cobro.

DIAG.1 está implementada y probada físicamente con ambos disparadores: comando USB y botón **Capturar por USB**. El usuario confirmó que la interfaz se ve, responde al toque y permanece estable. Los BMP de ambas capturas fueron validados por formato, dimensiones y checksum BED1; los archivos e identificadores de dispositivo se conservan localmente y no se publican. Las pruebas sintéticas actuales del receptor son 11/11; el BUILD vigente `esp32-s3-devkitc-1` pasó con RAM 102,988/327,680 bytes y Flash 1,998,952/6,553,600 bytes. No se afirma una prueba prolongada de capturas repetidas.

## Captura y memoria

La integración usa LVGL 8.4 y el `flush_cb` existente; `LV_USE_SNAPSHOT=0`, por lo que no depende de la API Snapshot ni de un framebuffer de pantalla completo. Al comenzar, el temporizador de LVGL invalida la pantalla y llama `lv_refr_now()` dentro del contexto LVGL protegido por el mutex existente. Esto termina un ciclo de refresco sincrónico antes de que empiece la transferencia y mantiene los efectos visuales estables durante la reconstrucción.

El adaptador copia cada rectángulo entregado al `flush_cb` en un buffer de captura separado. Un mapa de cobertura de 1 bit por píxel (48,000 bytes, ~46.9 KiB) detecta todos los píxeles recibidos, incluidos rectángulos parciales de ancho y alto; se reserva en PSRAM solo al solicitar una captura y se libera al completar el refresco o cancelarlo. La imagen RGB565 de 800×480 requiere 768,000 bytes y también se reserva en PSRAM bajo demanda. El callback continúa llamando el flujo original de LCD y completa LVGL como antes. La captura no sustituye, redimensiona ni cambia los buffers RGB existentes.

Una vez confirmada la cobertura total, el buffer queda inmutable durante la transferencia. El envío ocurre en `loop()`, en tramos limitados por `Serial.availableForWrite()` y sin `Serial.flush()`; el callback de renderizado no espera USB ni transmite datos. Los errores, timeout, final de protocolo y cierre liberan el buffer de PSRAM. Una solicitud simultánea se rechaza como ocupada. El tiempo de transferencia depende del enlace USB; el receptor limita cada lectura, el total de transferencia y los reintentos.

## Protocolo BED1 versión 1

Los frames binarios usan una cabecera little-endian de 32 bytes, precedida por la firma `BED1`. El receptor ignora texto del monitor hasta encontrar la firma y valida el CRC de cabecera y payload. Los campos son:

| Offset | Tamaño | Campo |
|---:|---:|---|
| 0 | 4 | Firma `BED1` |
| 4 | 1 | Versión `1` |
| 5 | 1 | Tipo: META=1, DATA=2, END=3, STATUS=4, ERROR=5 |
| 6 | 2 | Longitud de cabecera (`32`) |
| 8 | 4 | ID de captura |
| 12 | 2 | Ancho |
| 14 | 2 | Alto |
| 16 | 1 | Formato: RGB565 little-endian (`1`) |
| 17 | 1 | Reservado, cero |
| 18 | 2 | Secuencia del bloque |
| 20 | 4 | Longitud del payload |
| 24 | 4 | CRC-32 del payload |
| 28 | 4 | CRC-32 de bytes 0–27 |

`META` informa longitud total y CRC-32 de imagen. `DATA` envía bloques ordenados de hasta 1024 bytes. El receptor responde `DIAG1 ACK <id-hex> <secuencia>` o `DIAG1 NAK ...`; el firmware reintenta cada bloque un máximo de tres veces. `END` repite longitud y checksum, y `STATUS` informa por separado la disponibilidad de SD. `ERROR` comunica busy o fallo. El firmware reserva un ID no cero para cada solicitud.

## Receptor Windows

Dependencia: Python 3.10 o posterior y `pyserial` (`python -m pip install -r tools/diag1/requirements.txt`). Cierra primero el Monitor Serial de PlatformIO y cualquier otra aplicación que tenga abierto el puerto COM; solo un proceso puede poseerlo. El receptor acepta `--baud 115200` o `--baud 460800`; el valor predeterminado es 115200 por compatibilidad con el firmware instalado de la línea base. Siempre cierra el puerto también cuando hay timeout o error. La métrica `receiver_configured_baud` registra el ajuste solicitado al receptor; no confirma la velocidad real configurada en firmware.

```powershell
python tools/diag1/diag1_receiver.py
```

Para establecer la velocidad de forma explícita, conserva el modo `request`:

```powershell
python tools/diag1/diag1_receiver.py --baud 115200 --metrics "$env:LOCALAPPDATA\BancoEscolar-DIAG1-metrics-115200.json"
python tools/diag1/diag1_receiver.py --baud 460800 --metrics "$env:LOCALAPPDATA\BancoEscolar-DIAG1-metrics-460800.json"
```

El modo predeterminado solicita la captura por comando; el usuario puede escoger el puerto. Para recibir la captura pedida desde el Menú Maestro:

```powershell
python tools/diag1/diag1_receiver.py --mode listen
```

También se puede indicar `--port COM5`, `--output capturas_terminal` y `--timeout 25`. `request` reintenta como máximo cinco solicitudes; la transferencia tiene un límite total de 900 segundos y un límite por lectura. La salida es BMP de 24 bits, con nombre basado en fecha e ID y sufijo si ya existe. Se escribe primero como `.part` y se publica como `.bmp` únicamente después de validar el final y el checksum; los temporales se quitan si falla la escritura. Una imagen incompleta o con checksum incorrecto nunca se presenta como una captura válida.

## microSD

`SDManager` realiza un sondeo independiente de solo lectura: usa CS externo EXIO4 del CH422G, monta con formato automático desactivado, lee tipo/capacidad y lista la raíz, luego desmonta. La recepción USB no depende de microSD; las capturas no se escriben allí.

## Validaciones y pendientes

Pruebas sintéticas actuales del receptor: 11/11 aprobadas (transferencia válida/BMP, trama truncada, desconexión, CRC incorrecto, NAK/reintento, ACK repetido, timeout, solicitudes repetidas/BUSY, diagnóstico de error y nombres sin sobrescritura). Son datos generados por pruebas. BUILD PlatformIO vigente `esp32-s3-devkitc-1`: SUCCESS con RAM 102,988/327,680 (31.4 %) y Flash 1,998,952/6,553,600 (30.5 %); sin warnings de compilación reportados. `git diff --check` pasó.

Se confirmaron físicamente ambos disparadores y los BMP recibidos cumplieron formato, dimensiones y checksum BED1. El usuario confirmó respuesta táctil y estabilidad de la interfaz. No se afirma que una prueba prolongada de capturas repetidas se haya completado. No se hicieron operaciones de escritura en microSD.

## Corrección de memoria y BUILD (2026-10-08)

- El mapa de cobertura dejó de ocupar 48,000 bytes de BSS interna durante el arranque. Se reserva en PSRAM cuando comienza una captura y se libera al completar el refresco, ante timeout o al cancelar. La imagen también se reserva bajo demanda; si falta PSRAM o falla una reserva, se informa el error y se libera la memoria ya reservada sin cambiar el flujo normal del LCD.
- El adaptador registra memoria interna libre y bloque máximo, además de PSRAM total/libre/bloque máximo, antes y después del registro del display y de las reservas de captura. Registra cada reserva de buffer LVGL y cancela el registro si no consigue ambos buffers. El callback existente sigue enviando los rectángulos al LCD.
- BUILD PlatformIO `esp32-s3-devkitc-1`: SUCCESS. RAM 102,660/327,680 bytes (31.3 %); Flash 1,955,012/6,553,600 bytes (29.8 %). Frente al BUILD DIAG.1 anterior: RAM −48,000 bytes y Flash +1,992 bytes. Frente al BUILD anterior a DIAG.1 (RAM 101,436 bytes), la nueva versión usa 1,224 bytes adicionales de RAM.
- Pruebas sintéticas del receptor: 9/9 aprobadas. `git diff --check`: aprobado. No se hizo Upload; prueba física pendiente.

## Arranque físico tras la corrección de memoria (2026-10-08)

- Upload autorizado al entorno `esp32-s3-devkitc-1` por el puerto identificado como `USB-Enhanced-SERIAL CH343`: SUCCESS; hash verificado y reinicio por RTS. No se ejecutó `uploadfs`, borrado ni formato.
- Con una única conexión serie a 115200 y una pulsación RESET confirmada por el usuario, el arranque llegó a `100% Sistema listo` sin reinicios durante ese ciclo. Superó `Pantalla registrada` y `Pantalla táctil lista`.
- Persistió el aviso RGB `invalid frame buffer number` / `Get RGB buffer failed`; el firmware encontró un framebuffer del panel, no encontró el segundo y reservó correctamente dos buffers LVGL internos de 64,000 bytes. Después: heap interno libre 98,544 bytes, bloque máximo 34,804; PSRAM total 8,388,608, libre 7,614,368, bloque máximo 7,602,164.
- El driver leyó TouchPad_ID/configuración y la inicialización de la placa tuvo éxito. En esa fecha la respuesta táctil real y visibilidad de la interfaz aún esperaban confirmación. No se solicitó ni probó captura DIAG.1. El puerto se liberó y una prueba breve de apertura/cierre fue exitosa.

## Validación física posterior (2026-10-08)

- PlatformIO confirmó que el entorno `esp32-s3-devkitc-1` conservaba el BUILD DIAG.1 documentado: RAM 150,660/327,680 bytes (46.0 %) y Flash 1,953,020/6,553,600 bytes (29.8 %). No había cambios posteriores de firmware/configuración; no se necesitó un BUILD nuevo antes de cargar.
- Upload por el puerto identificado como `USB-Enhanced-SERIAL CH343`: SUCCESS; el hash de la imagen fue verificado y la placa se reinició. No se ejecutó `uploadfs` ni se formateó almacenamiento.
- Tras el arranque, `COM3` reapareció. El receptor en modo `request` agotó el tiempo esperando DIAG.1. Una escucha posterior también agotó el tiempo; no se recibió captura y no hubo confirmación del usuario de que hubiera pulsado el botón. El disparador del botón queda sin probar.
- No se generó ningún BMP. Las capturas, fidelidad de imagen, respuesta táctil, memoria dinámica y capturas repetidas siguen pendientes. Los procesos del receptor finalizaron; `COM3` quedó enumerado y una prueba breve de apertura/cierre confirmó que el puerto estaba libre.

## Captura física por comando (2026-10-08)

- El usuario confirmó que la interfaz se veía, respondía al toque y permanecía estable. Se hizo una captura DIAG.1 en modo `request` por el puerto identificado, a 115200; el botón no se probó en esa primera sesión.
- La transferencia completa tomó cerca de 10 minutos. Los intentos anteriores se interrumpieron antes de terminar porque el límite predeterminado de 180 segundos era insuficiente. La captura exitosa se recibió con un límite temporal de 1000 segundos. El límite predeterminado del receptor se amplió a 900 segundos para las próximas transferencias.
- BMP recibido: 800×480, 24 bits, 1,152,054 bytes. El receptor validó trama END y checksum de imagen; se comprobó la firma/tamaño y se revisó visualmente la interfaz. El archivo y sus hashes permanecen locales.
- No se escribió en microSD. El receptor cerró su conexión. La prueba del botón se documenta en la sesión posterior.
## Prueba física del botón Capturar por USB (2026-10-08)

- Se confirmó el puerto y que estaba libre, y luego se inició `listen` a 115200 con espera de 900 segundos. El usuario confirmó que pulsó Capturar por USB.
- La primera transferencia desde el botón terminó con `ACK_TIMEOUT_OR_RETRY_LIMIT`; no se generó BMP y no se validaron imagen ni checksum. El BMP de la captura por comando anterior permaneció intacto. Este fallo fue investigado e instrumentado antes de la prueba exitosa descrita abajo.
- El receptor cerró su conexión y la comprobación posterior confirmó que el puerto quedó libre. No se hizo Upload ni se modificó firmware durante esa sesión.

## Preparación de línea base instrumentada DIAG.1 (2026-10-09)

- Revisión local: rama `main`, HEAD `604eaa5` (`feat: completa avances de panel y terminal`). Se conservaron los archivos sin seguimiento preexistentes; no se tocaron capturas del Panel Maestro, artefactos de terminal ni cachés.
- Configuración verificada en código: 800×480 RGB565 little-endian, 768,000 bytes; BED1 v1; tramas DATA de hasta 1,024 bytes y 750 bloques; ACK/NAK por bloque; firmware a 115,200 baudios; timeout ACK 5,000 ms y tres reintentos. El firmware solo avanza con ACK que coincide con captura y secuencia. Envía END tras los ACK y acepta `DIAG1 VERIFIED` solo en WAIT_VERIFY, con ID y CRC coincidentes. VERIFIED se transmite después de que el receptor valida END y guarda el BMP.
- `Serial.availableForWrite()` limita cada tramo que el firmware entrega desde `loop()`; `vTaskDelay(50 ms)` también limita la frecuencia de sondeo. Son posibles límites que requieren instrumentación del firmware para cuantificar; no se atribuye causalidad todavía.
- El receptor ahora sigue leyendo DATA hasta un END válido incluso después de completar los bytes. Acepta de nuevo el ACK de un duplicado byte idéntico del último bloque si coinciden captura, dimensiones, formato y checksum; no añade sus píxeles otra vez. Los bloques corruptos, con secuencia/metadata incorrectas reciben NAK con máximo de cuatro observaciones (envío original y tres reintentos). END inválido, timeout, desconexión y escritura parcial de ACK/NAK no producen VERIFIED.
- `--metrics JSON_PATH` guarda un JSON elegido por ejecución, con apertura exclusiva y un sufijo en caso de colisión, también para recepciones fallidas. Registra espera inicial, META→END, conversión y guardado BMP, duración total, caudal efectivo, bytes únicos, bloques aceptados, ACK/NAK escritos con éxito, duplicados/rechazos/errores y tiempos por bloque vistos por el receptor. `firmware_ack_received_count` queda `null`: el receptor no observa directamente la recepción de cada línea por firmware. `duplicates_observed` tampoco representa todas las retransmisiones del emisor. Métricas usan reloj monotónico; BMP y JSON se mantienen fuera de Git durante las mediciones.
- Pruebas del receptor: 17/17 aprobadas, incluidos checksum, NAK/reintento, duplicado intermedio y del bloque final, ID de captura incorrecto, rechazo acotado, escritura parcial, desconexión, timeout (incluidas métricas de fallo) y nombres exclusivos. BUILD `esp32-s3-devkitc-1`: SUCCESS; RAM 102,988/327,680 bytes (31.4 %), Flash 1,998,952/6,553,600 bytes (30.5 %), sin warnings visibles.
- Medición física instrumentada por `request` a 115,200 baudios en CH343 COM3: espera META 3.906 s; META→END validado 694.672 s; conversión y guardado BMP 0.156 s; total 698.969 s; caudal efectivo 1,105.558 bytes/s (0.001054 MiB/s). Se aceptaron 750 bloques/768,000 bytes únicos; el receptor escribió 750 ACK y 0 NAK, y observó 0 duplicados. El receptor no mide directamente cuántos ACK recibió el firmware. BMP validado por cabecera/tamaño (800×480, 24 bits, 1,152,054 bytes); BED1 CRC-32 de META/END e imagen coincidió y el receptor obtuvo la respuesta final `DIAG1 VERIFIED`. El BMP y JSON están en `%LOCALAPPDATA%\BancoEscolar-DIAG1-Baseline`, fuera del repositorio. Los ~10 minutos históricos quedan como antecedente no instrumentado.
- No se cambió firmware, velocidad, bloque, retardo del loop ni protocolo; no se hizo Upload. Próximo ensayo: cambiar a 460,800 baudios conservando 1,024 bytes y ACK por bloque, con BUILD previo y autorización de Upload antes de cargar.
- Próximo ensayo autorizado por fase posterior: 460,800 baudios conservando 1,024 bytes y ACK por bloque. Primero deberá cargarse mediante autorización explícita y medirse en hardware; esta preparación no modifica la velocidad actual de 115,200.

## Preparación del ensayo comparativo a 460800 (2026-10-09)

- Configuración local preparada, sin Upload ni medición a esta velocidad: `SERIAL_BAUD_RATE = 460800` centraliza el baud del firmware y alimenta tanto `Serial.begin()` como el mensaje de arranque. `platformio.ini` fija `monitor_speed = 460800`.
- BED1 v1, tamaño RGB565, bloques de 1,024 bytes, ACK por bloque, timeout de 5,000 ms y tres reintentos se conservan. Tampoco cambian el avance confirmado por ACK, END/CRC/VERIFIED, `availableForWrite()`, `vTaskDelay(50 ms)`, pines, buffers, memoria o almacenamiento.
- El receptor permite `--baud 115200` y `--baud 460800`; su predeterminado sigue en 115200. Las métricas guardan `receiver_configured_baud` y `firmware_baud_confirmed: false`: el host no puede inferir la velocidad del firmware a partir de su propia configuración.
- Validación de preparación: 18/18 pruebas sintéticas pasan, incluida selección 115200/460800, rechazo de 230400 y registro de velocidad configurada por el receptor. BUILD `esp32-s3-devkitc-1`: SUCCESS; RAM 102,988/327,680 bytes (31.4 %), Flash 1,998,984/6,553,600 bytes (30.5 %); sin warnings visibles. `git diff --check`: aprobado con avisos informativos LF/CRLF en los documentos y scripts.
- El usuario autorizó el Upload y las tres capturas el 2026-10-09. BUILD previo a la carga: SUCCESS, RAM 102,988/327,680, Flash 1,998,984/6,553,600. Upload normal por el mismo CH343/COM3: SUCCESS, hashes verificados y reinicio por RTS. No se ejecutó `uploadfs`.
- Se recibieron tres capturas consecutivas en modo `request` a 460800 en el CH343 usado para la línea base. En las tres el receptor validó END/CRC, guardó el BMP y recibió `DIAG1 VERIFIED`:

| Captura | Espera META | META→END | Total | Caudal | Bloques / bytes únicos | ACK / NAK / duplicados observados | Resultado BMP |
|---:|---:|---:|---:|---:|---:|---:|---|
| 1/3 | 3.500 s | 692.297 s | 696.297 s | 1,109.350 B/s | 750 / 768,000 | 750 / 0 / 0 | Verificado, 800×480×24, 1,152,054 bytes |
| 2/3 | 4.000 s | 693.860 s | 698.328 s | 1,106.852 B/s | 750 / 768,000 | 750 / 0 / 0 | Verificado, 800×480×24, 1,152,054 bytes |
| 3/3 | 3.891 s | 692.656 s | 696.938 s | 1,108.775 B/s | 750 / 768,000 | 750 / 0 / 0 | Verificado, 800×480×24, 1,152,054 bytes |

- Promedio de las tres: 692.938 s META→END, 697.188 s total y 1,108.326 B/s. Frente a la línea base de 115200 (694.672 s META→END y 1,105.558 B/s), la mejora observada es aproximadamente 0.25 % en esta muestra de tres capturas; la velocidad de baud configurada por sí sola no predijo un aumento importante del caudal. Los JSON registran la velocidad solicitada por el receptor (`receiver_configured_baud`) y dejan `firmware_baud_confirmed: false`; los resultados demuestran una transferencia BED1 verificada durante este ensayo, no una lectura independiente del ajuste serial interno.
- Los tres JSON y BMP, además de logs stdout/stderr por ejecución, se conservan en `%LOCALAPPDATA%\BancoEscolar-DIAG1-460800`, fuera del repositorio. No se descartó ningún intento. No hubo fallos. Los procesos receptores finalizaron; después del tercero se comprobó apertura y cierre del mismo puerto CH343.
- No cambian BED1, bloques de 1,024 bytes, ACK por bloque, timeout/reintentos, `availableForWrite()` ni la espera de 50 ms. Tampoco se probaron bloques mayores, ventanas o compresión.
- Comandos empleados después de la autorización, con COM3 comprobado contra el CH343 antes de cada ejecución; repetir tres veces, esperando el cierre de cada receptor:

```powershell
python tools/diag1/diag1_receiver.py --port COM3 --mode request --baud 460800 --metrics "$env:LOCALAPPDATA\BancoEscolar-DIAG1-460800\intento-1.json" --output "$env:LOCALAPPDATA\BancoEscolar-DIAG1-460800"
```

- Confirma que `COM3` siga siendo el CH343 identificado antes de ejecutar el comando; si la enumeración cambia, sustituye el puerto por el dispositivo verificado. Cada ejecución genera nombres exclusivos. Conserva BMP y JSON de todos los intentos, incluidos fallos; anota por separado estado, tiempo, caudal, bloques, ACK/NAK, duplicados observados y error. No descartes capturas lentas ni fallidas.
- Registro comparativo completado:

| Captura | Resultado/error | META→END | Total | Caudal | Bytes/bloques | ACK/NAK/duplicados observados | Artefactos |
|---:|---|---:|---:|---:|---|---|---|
| 1/3 | Verificado | 692.297 s | 696.297 s | 1,109.350 B/s | 768,000 / 750 | 750 / 0 / 0 | BMP + JSON |
| 2/3 | Verificado | 693.860 s | 698.328 s | 1,106.852 B/s | 768,000 / 750 | 750 / 0 / 0 | BMP + JSON |
| 3/3 | Verificado | 692.656 s | 696.938 s | 1,108.775 B/s | 768,000 / 750 | 750 / 0 / 0 | BMP + JSON |

- Para volver a la configuración base antes de otro BUILD/Upload autorizado: cambia `SERIAL_BAUD_RATE` y `monitor_speed` a 115200, y ejecuta el receptor con `--baud 115200` (o sin `--baud`). El valor predeterminado del receptor ya es 115200. No se requiere cambio de protocolo.

## Variante experimental de bloques de 4 KiB a 460800 (2026-10-09)

- Variante local de firmware y receptor cargada y medida físicamente en COM3/CH343 a 460800. BED1 v1, ACK/NAK por bloque, timeout ACK de 5,000 ms y máximo de tres reintentos se conservan. Para una imagen RGB565 de 768,000 bytes, el emisor genera 187 bloques de 4,096 bytes y un bloque final parcial de 2,048 bytes: 188 secuencias, 0–187. CRC por payload e imagen, avance únicamente al recibir ACK con ID/secuencia coincidentes, END válido y `DIAG1 VERIFIED` solo después de escribir completamente el BMP permanecen iguales.
- El firmware y `tools/diag1/diag1_receiver.py` aceptan como máximo 4,096 bytes de DATA; el receptor comprueba la longitud esperada de cada bloque, incluido el final parcial. La retransmisión idéntica de bloques intermedios y del último sigue idempotente y obtiene ACK sin agregar píxeles. Los bloques con CRC, secuencia, captura o metadata incorrectos reciben NAK con recuperación limitada a tres reintentos del emisor; la suite incluye rechazo/recuperación y fallos de transferencia.
- La trama estática del firmware crece de 1,056 a 4,128 bytes (+3,072). La instrumentación residente usa dos arreglos de 188 valores `uint32_t` (1,504 bytes) para sumar duración de envío y espera de ACK por secuencia, además de contadores; no reserva la imagen en RAM interna ni toca buffers de pantalla, PSRAM, almacenamiento, pines o microSD. El BUILD midió 107,588/327,680 bytes de RAM (32.8 %) y 1,999,884/6,553,600 bytes de Flash (30.5 %), un delta de +4,600 bytes de RAM y +900 de Flash frente al BUILD previo documentado. Sin warnings visibles.
- Tras terminar o fallar, una vez que la trama BED1 de estado/error ya salió, el firmware emite `DIAG1_FW_METRICS` y una línea `DIAG1_FW_BLOCK` por secuencia. Incluye baud configurado, intentos DATA enviados (incluidos reintentos), ACK válidos recibidos por firmware, NAK, timeouts, reintentos, errores y duración total desde que inicia META hasta que se verifica o falla. `send_ms` mide desde que se prepara el DATA hasta que termina su escritura serial; `ack_wait_ms` acumula espera por las respuestas observadas o timeout. Con `--metrics`, el receptor recoge estas líneas antes de cerrar USB y las guarda en `firmware_metrics` dentro del JSON; la colección tiene timeout acotado y su fallo no cambia el resultado original. No se mezclan con bytes BED1. Son métricas firmware distintas de `receiver_ack_sent`/`receiver_nak_sent` del host; estos últimos registran escrituras completas del host y no demuestran recepción por firmware. Los duplicados observados por host siguen sin ser conteo exacto de retransmisiones. Las pruebas sintéticas no miden velocidad.
- Validación del receptor: 23/23 pruebas aprobadas, incluidas longitudes y bloque parcial, checksum erróneo/recuperación, duplicados intermedio y final, secuencia incorrecta, desconexión, timeout, END/CRC inválido, BMP no guardable y colecta separada de métricas firmware. BUILD `esp32-s3-devkitc-1`: SUCCESS, RAM 107,588/327,680 (32.8 %), Flash 1,999,884/6,553,600 (30.5 %), sin warnings visibles. Upload normal autorizado: SUCCESS, escritura de imagen y hash verificados; reinicio por RTS. No se ejecutó UploadFS, Commit ni Push.
- Las tres sesiones abrieron COM3 a 460800; el firmware completó cada protocolo y su resumen registró `baud_configured=460800`. Esto confirma que la imagen cargada arrancó y terminó las transferencias con el ajuste reportado. En cada captura el receptor observó 187 payloads de 4,096 bytes y uno de 2,048; firmware y receptor reportaron 188 ACK válidos. END/CRC fueron aceptados, BMP guardado y `VERIFIED` recibido en los tres intentos.
- Tres capturas físicas consecutivas con el mismo terminal/equipo/cable informados y el CH343 identificado como COM3:

| Intento | META→END | Total | Caudal efectivo | Bloques / bytes únicos | ACK / NAK host | ACK / NAK firmware | DATA enviados / reintentos / timeouts | Resultado |
|---:|---:|---:|---:|---:|---:|---:|---:|---|
| 1 | 550.609 s | 554.438 s | 1,394.819 B/s | 188 / 768,000 | 188 / 1 | 188 / 1 | 189 / 1 / 0 | Verificado; BMP 800×480×24, CRC/END/VERIFIED válidos |
| 2 | 549.671 s | 554.109 s | 1,397.199 B/s | 188 / 768,000 | 188 / 0 | 188 / 0 | 188 / 0 / 0 | Verificado; BMP 800×480×24, CRC/END/VERIFIED válidos |
| 3 | 550.703 s | 554.969 s | 1,394.581 B/s | 188 / 768,000 | 188 / 0 | 188 / 0 | 188 / 0 / 0 | Verificado; BMP 800×480×24, CRC/END/VERIFIED válidos |

- Promedio de 4 KiB: 550.328 s META→END, 554.505 s total y 1,395.533 B/s (media de los caudales por intento). Frente a las tres capturas de 1 KiB (692.297, 693.860 y 692.656 s; media 692.938 s META→END y 1,108.326 B/s), META→END fue 20.580 % menor y el caudal medio observado 25.914 % mayor en estas muestras. Esto describe la comparación medida; no atribuye causa fuera del cambio coordinado de tamaño de bloque.
- En el intento 1 hubo un NAK y un reintento, ambos contados por host y firmware, y se recuperó sin timeout ni error. Intentos 2 y 3 no tuvieron NAK, reintentos, duplicados ni timeout. Las métricas firmware conservan 188 filas de tiempos por secuencia y `blocks_sent_attempts`; los ACK host son escrituras confirmadas por el host y se contrastan por separado con los ACK válidos recibidos por firmware. `duplicates_observed` fue 0 en los tres, sin presentarlo como total de retransmisiones.
- BMP y JSON están fuera del repositorio en `%LOCALAPPDATA%\BancoEscolar-DIAG1-4KB-460800-20261009`: `intento-1.json`, `intento-2.json`, `intento-3.json` y tres BMP con ID de captura exclusivo. El intento 1 ya estaba transfiriéndose cuando se corrigió la interfaz CLI a `--metrics JSON_PATH`; su receptor anterior guardó un JSON único que se renombró a `intento-1.json` sin modificar su contenido. Los intentos 2 y 3 usaron rutas JSON explícitas. Se validaron las cabeceras BMP de 800×480, 24 bits y 1,152,054 bytes, junto a los contadores, longitudes payload y resultado final de cada JSON. Los tres intentos fueron válidos; ninguno se descartó.

```powershell
python tools/diag1/diag1_receiver.py --port COM3 --mode request --baud 460800 --metrics "$env:LOCALAPPDATA\BancoEscolar-DIAG1-4KB-460800\intento-1.json" --output "$env:LOCALAPPDATA\BancoEscolar-DIAG1-4KB-460800"
```

- Para repetir el procedimiento después, vuelve a identificar CH343/COM antes de cada intento y usa una ruta `intento-N.json` distinta; cada ejecución guarda BMPs exclusivos y métricas aun si falla. En este ensayo todos los JSON incluyen los registros firmware recogidos antes de cerrar USB, en vez de depender de un segundo monitor serial.
- Permanecen sin cambios el bucle de espera de 50 ms, el drenaje por `Serial.availableForWrite()`, buffers de pantalla, memoria de captura en PSRAM, dependencias, pines y almacenamiento. No se probaron ventanas, compresión ni tamaños superiores a 4 KiB.

## Observación de sondeos y escritura Serial (2026-10-09)

- Se añadió instrumentación observacional por secuencia e intento (retry=0 es el envío inicial; retry=1..3 son retransmisiones). Se emite después de completar o fallar el protocolo, junto a las líneas de métricas ya existentes; no se imprime texto de diagnóstico por bloque durante la transferencia.
- `polls` cuenta llamadas a `terminal_capture_poll()` mientras la trama DATA sigue pendiente. `poll_gap_avg_us` y `poll_gap_max_us` miden el intervalo entre el inicio de esas llamadas; no prueban que todo ese intervalo sea una pausa de `vTaskDelay()`. `no_space` cuenta resultados `availableForWrite() <= 0`; `space_samples`, `space_avg` y `space_max` resumen los valores positivos devueltos.
- `write_calls` cuenta llamadas de escritura durante ese intento; `bytes_accepted` suma los retornos de `Serial.write()`. `write_active_us` suma el tiempo observado alrededor de esas llamadas, no el tiempo físico de bits en el cable. `ack_latency_us` mide desde que termina de entregarse el DATA a la API Serial hasta que el parser recibe un ACK coincidente en captura/secuencia; un cero indica que no se observó un ACK válido para ese intento (por ejemplo, NAK/reintento).
- El receptor conserva estas filas en `firmware_metrics.attempt_timings`, separadas de las filas agregadas anteriores `block_timings` y de los contadores host. La lectura de este resumen tiene un límite de 60 s después del protocolo (se amplió tras el primer intento físico parcial); su fallo no sustituye el resultado de transferencia ni forma parte del tiempo META→END. Las pruebas sintéticas validan asociación secuencia/reintento y parsing, no rendimiento físico.
- El muestreo añade llamadas a `micros()`, contadores y escrituras de métricas en RAM; puede perturbar ligeramente el tiempo observado. El almacenamiento fijo usa 32 bytes por fila × 188 secuencias × cuatro intentos = 24,064 bytes, además de contadores menores. El límite de payload de 4,128 bytes permite guardar `bytes_accepted` en 16 bits. No cambia baud (460800), payload máximo (4096), ACK por bloque, timeout/reintentos, `availableForWrite()`, `Serial.write()`, el retardo del loop de 50 ms, avance por ACK válido ni condiciones de END/CRC/VERIFIED.
- Para comparar, hacer capturas instrumentadas en las mismas condiciones y conservar por separado cada JSON/BMP. Contrastar por secuencia/retry `poll_gap_*`, `no_space`/`space_*`, `write_active_us`, bytes y llamadas con `ack_latency_us`; comparar también los agregados host y firmware sin confundirlos. Esta compilación no constituye una nueva medición física.


## Instrumentación temporal ACK (2026-10-08)

- Timeout provisional de ACK BED1: 5000 ms por bloque. El contador inicia cuando todo el frame DATA se ha entregado al buffer de salida Serial; no incluye el tiempo ocupado en fragmentar/escribir ese frame desde el buffer del protocolo. Se conservan tres reintentos (hasta cuatro envíos por bloque).
- No se imprime texto de diagnóstico mientras BED1 está transfiriendo. En el error BED1 enviado después del aborto, el firmware incluye ID, secuencia esperada, reintentos, causa final (TIMEOUT/NAK), última respuesta ACK/NAK con ID/secuencia y coincidencia, intento de respuesta y tiempos de espera/total. El receptor muestra el payload del ERROR en la computadora.
- Las pruebas sintéticas verifican ACK con ID/secuencia correctos, NAK de bloque dañado seguido del ACK de reintento, ACK repetido para retransmisión del mismo bloque y preservación del diagnóstico del firmware. Estas pruebas no miden la temporización física del ESP32/USB.
- BUILD `esp32-s3-devkitc-1`: SUCCESS; RAM 102,684/327,680 (31.3 %) y Flash 1,955,736/6,553,600 (29.8 %), sin warnings visibles. Frente al BUILD DIAG.1 previo: +24 bytes RAM y +724 bytes Flash. Pruebas sintéticas: 11/11 aprobadas; `git diff --check`: aprobado. No se hizo Upload; prueba física pendiente. El BMP previo se conserva.

### Interfaz de progreso DIAG.1 (2026-10-08)

- El botón captura y completa el refresco de la pantalla actual antes de mostrar una vista de transferencia independiente. La vista informa Preparando captura, Enviando, Reintentando, Verificando, Captura recibida o Error al enviar; deja volver a la pantalla anterior al terminar.
- La barra, porcentaje y contador usan 750 bloques. El progreso solo avanza con ACK que coincida con el ID y la secuencia esperada; los reintentos conservan el último bloque confirmado. La interfaz se actualiza desde el timer LVGL y no escribe texto en USB durante BED1.
- Tras END, el receptor confirma `DIAG1 VERIFIED <id> <crc>` solo después de validar y guardar el BMP. El firmware muestra éxito tras validar esa confirmación; vence a los 30 s si no llega.
- BUILD `esp32-s3-devkitc-1`: SUCCESS; RAM 102,756/327,680 (31.4 %), Flash 1,958,808/6,553,600 (29.9 %). Pruebas sintéticas: 11/11; incluyen confirmación final, CRC, retransmisión, secuencia/progreso comprobados con `static_assert` durante BUILD. `git diff --check`: aprobado, con avisos de conversión LF/CRLF ya presentes.
- Sin Upload ni prueba física de esta interfaz. El BMP anterior permanece conservado.

### Captura física DIAG.1 por botón con vista de progreso (2026-10-08)

- Tras Upload autorizado en `esp32-s3-devkitc-1`, el receptor `listen` recibió la captura del botón. El usuario informó barra al 100 % y volvió con Volver. El BMP quedó guardado localmente; no se inició una segunda captura.
- El receptor verificó checksum CRC-32 BED1 contra META y END, guardó el BMP y envió `DIAG1 VERIFIED` con ID/CRC coincidentes; el firmware acepta esa respuesta solo si coincide con la captura transmitida. El receptor indicó `Captura verificada guardada`.
- Comprobación local: firma/cabecera coherentes, 800×480, 24 bits y 1,152,054 bytes; CRC y SHA-256 coinciden con el archivo local. La ruta, el ID de captura y los hashes no se publican.
- El BMP previo permanece presente localmente. El receptor cerró su conexión y la comprobación posterior confirmó que el puerto quedó libre. Sin Commit ni Push.

## Medición física de sondeos y Serial (2026-10-09)

- BUILD confirmado antes de Upload: `esp32-s3-devkitc-1` SUCCESS, RAM 131,668/327,680 bytes (40.2 %), Flash 2,000,612/6,553,600 bytes (30.5 %), sin warnings visibles; suite del receptor 23/23. Upload normal por el CH343 identificado como COM3: SUCCESS, hashes verificados y reinicio RTS. No se ejecutó UploadFS, Commit ni Push.
- Se hicieron dos capturas completas, por comando `request`, a 460,800 configurados, payload máximo 4,096 y ACK por bloque. En ambas el host validó BED1 END/CRC, guardó un BMP de 800×480×24 bits y recibió `DIAG1 VERIFIED`; firmware informó `baud_configured=460800`, resultado verified, 188 DATA enviados, 188 ACK válidos y cero NAK, timeout, reintentos o errores. Host escribió 188 ACK y cero NAK; son observaciones de extremos distintos, aunque los totales coinciden.

| Intento | META→END host | Total host | Caudal host | Bloques / bytes únicos | ACK / NAK host | Filas firmware detalladas | BMP / CRC / VERIFIED |
|---:|---:|---:|---:|---:|---:|---|---|
| 1 | 567.078 s | 571.422 s | 1,354.311 B/s | 188 / 768,000 | 188 / 0 | 127/188 block; 126/188 attempt | Válido, 1,152,054 bytes |
| 2 | 566.859 s | 571.312 s | 1,354.834 B/s | 188 / 768,000 | 188 / 0 | 188/188 block; 188/188 attempt | Válido, 1,152,054 bytes |

- El primer JSON conserva transferencia verificada y resumen firmware completo, pero el receptor cerró su ventana de recolección antes de recibir todas las líneas detalladas: faltan 61 filas block y 62 filas attempt. No se modificó el JSON ni el BMP originales. Antes del segundo intento se amplió únicamente el drenaje host posterior al protocolo de 10 a 60 s; no forma parte de META→END ni modifica el firmware. El segundo JSON contiene todas las filas. Las distribuciones combinadas siguientes usan solo las 314 filas attempt disponibles de 376 esperadas y están incompletas por ese primer volcado.
- En los 126 registros attempt disponibles del intento 1, y los 188 del intento 2: los sondeos por bloque tuvieron min/mediana/P95/máx 33/33/33/33 y 17/33/33/33; el valor 17 corresponde al bloque final parcial observado. `no_space` fue cero en todas las filas recibidas; `space_avg` y `space_max` fueron 128 bytes en todas. El intento 1 está truncado, así que estos datos no certifican los bloques cuyas filas faltan.

| Distribución por bloque/intento | Intento 1 (n=126) min / mediana / P95 / máx | Intento 2 (n=188) min / mediana / P95 / máx | Filas disponibles combinadas (n=314) |
|---|---:|---:|---:|
| Intervalo promedio entre sondeos (µs) | 79,333 / 85,041 / 89,349 / 91,305 | 79,412 / 84,984 / 88,734 / 92,017 | 79,333 / 85,005 / 88,927 / 92,017 |
| Mayor intervalo entre sondeos del bloque (µs) | 87,135 / 125,393 / 134,426 / 166,866 | 86,785 / 126,534 / 135,560 / 169,879 | 86,785 / 126,032 / 135,026 / 169,879 |
| Tiempo acumulado dentro de `Serial.write()` (µs) | 2,314 / 2,564 / 44,078 / 79,215 | 1,346 / 2,550 / 44,550 / 80,973 | 1,346 / 2,557 / 44,540 / 80,973 |
| Latencia hasta ACK coincidente (µs) | 75,082 / 83,611 / 122,611 / 138,145 | 51,285 / 83,216 / 124,372 / 131,480 | 51,285 / 83,354 / 124,159 / 138,145 |
| Tiempo host leyendo cada DATA (ms) | 1,657 / 3,016 / 3,157 / 3,265 | 1,625 / 3,031 / 3,141 / 3,203 | 1,625 / 3,016 / 3,156 / 3,265 |

- En las 314 filas disponibles se acumularon 10,346 sondeos, cero lecturas sin espacio, 10,346 lecturas positivas, 10,346 llamadas `Serial.write()` y 1,294,144 bytes aceptados por la API. Son subtotales de filas observadas, no el total de las dos capturas, porque faltan 62 filas del primer intento. `write_active_us` suma 2.309 s entre esas filas; no mide bits en el cable. `ack_latency_us` es firmware desde el fin de encolado DATA hasta parsear ACK coincidente; el tiempo host por frame incluye espera/lectura y no es equivalente.
- En `block_timings`, el intento 1 contiene 127 filas (send_ms min/mediana/P95/máx 2,653/2,835/2,980/3,044; ack_wait_ms 75/83/123/139), y el intento 2, 188 filas (send_ms 1,446/2,830/2,960/3,038; ack_wait_ms 52/83/125/132). Filas disponibles combinadas: 315/376; send_ms 1,446/2,832/2,963/3,044; ack_wait_ms 52/83/124/139. P95 usa interpolación lineal.
- La muestra 4 KB anterior promedió 550.328 s META→END (550.609, 549.671, 550.703). Estos dos intentos promediaron 566.969 s, es decir +16.641 s o +3.024 %. El caudal medio reportado fue 1,354.573 B/s frente a 1,395.533 B/s anterior (−2.935 %). Esto describe las muestras; no atribuye causa. En las filas observadas, `availableForWrite()` nunca devolvió cero y devolvió 128 de espacio; los intervalos entre sondeos rondaron 85 ms. `Serial.write()` observado acumuló poco tiempo mediano por bloque pero tuvo máximos aislados de 79–81 ms. Ninguna de estas correlaciones demuestra qué aporta el loop, el planificador o la API al tiempo total.
- El receptor detalla el registro `intento-1.json` como parcial y conserva los dos BMP/JSON en `%LOCALAPPDATA%\BancoEscolar-DIAG1-poll-metrics`. Los percentiles del intento 1 y los agregados de filas detalladas deben leerse con esa limitación. No se incluyen mediciones del tercer intento.
