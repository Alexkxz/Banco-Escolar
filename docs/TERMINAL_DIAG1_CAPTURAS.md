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

Dependencia: Python 3.10 o posterior y `pyserial` (`python -m pip install -r tools/diag1/requirements.txt`). Cierra primero el Monitor Serial de PlatformIO y cualquier otra aplicación que tenga abierto el puerto COM; solo un proceso puede poseerlo. El receptor abre el puerto a 115200 baudios y siempre lo cierra, también cuando hay timeout o error.

```powershell
python tools/diag1/diag1_receiver.py
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
