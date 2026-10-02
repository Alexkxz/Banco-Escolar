# NFC y PN532

## Estado actual

`PN532_ENABLED = false` en `src/main.cpp`. El lector está reportado físicamente desconectado; mientras siga así, el selector Waveshare permanece en UART1, no se inicia I²C1 ni se construye el objeto Adafruit PN532. No uses GPIO43/44 ni actives PN532 fuera de una fase explícita.

## Diseño previsto

Se prevé Adafruit PN532 1.3.4 por I²C1 separado: GPIO43 SDA, GPIO44 SCL. I²C0 ya atiende GT911/CH422G; PN532 suele usar dirección 0x24, en conflicto con CH422G. La tarjeta solo debe aportar UID: nunca guardar saldo ni historial en ella. El UID se vinculará a `student_id` estable.

## Fallos históricos que no deben repetirse

- Inicializar `pn532_wire.begin(43,44,...)` mientras esos pines están asociados a UART alteró el monitor Serial.
- En Adafruit PN532 1.3.4, usar el constructor global con pines `-1` terminó pasando 255 y reportó `Invalid IO 255`.
- Mantener la construcción e inicio del bus dentro de una condición habilitada; cuando está desactivado, no debe existir construcción global que produzca efectos.

La alimentación y niveles lógicos del módulo no están cerrados. Antes de una conexión física, verifica la especificación exacta de la placa PN532; no prescribas 5 V automáticamente. No reutilices los pines UART ni I²C0.
