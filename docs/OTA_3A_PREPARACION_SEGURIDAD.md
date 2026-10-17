# OTA.3A — Preparación de seguridad y distribución local

## Estado de esta fase

Esta fase prepara herramientas de desarrollo y contratos internos. `BANCO_OTA_ENABLED` permanece en `0`; no habilita solicitudes OTA ni agrega rutas al firmware. No es una implementación lista para despliegue ni para probar una actualización inalámbrica real.

## Componentes

- `tools/ota/ota_manifest.mjs` genera pares ECDSA P-256 de desarrollo, firma el contrato de manifiesto vigente y verifica firma, tamaño y SHA-256. Requiere Node.js 18 o posterior y usa solo módulos integrados.
- `tools/ota/ota_https_server.mjs` sirve únicamente `GET`/`HEAD /manifest.json` y `GET`/`HEAD /firmware.bin`. Rechaza métodos de escritura, rutas adicionales, `0.0.0.0` y direcciones IPv4 públicas. Requiere certificado y clave TLS cuyo SAN coincida con `--hostname`; TLS mínimo 1.2.
- El servidor se ejecuta en primer plano y termina con Ctrl+C. No se instala como servicio. En la escuela se debe elegir explícitamente una dirección privada de la LAN y un nombre de host que resuelva a esa dirección. El firewall del equipo debe limitar el acceso a la red prevista.
- La clave privada de firma debe estar fuera del repositorio. Las claves generadas por `keygen` son solo de desarrollo. No reutilizarlas en producción ni incorporarlas al firmware.
- Se puede proporcionar `BANCO_OTA_DEV_SIGNING_KEY_PASSPHRASE` al proceso para crear/usar la clave de firma cifrada; `BANCO_OTA_TLS_KEY_PASSPHRASE` se reserva a claves TLS PEM cifradas. El código no contiene esos valores y no los imprime. Los comandos de abajo omiten ambos para mantenerlos como marcadores.

## Certificado TLS de desarrollo

El entorno comprobado para esta fase no tiene `openssl` en `PATH`; la prueba automatizada genera un certificado efímero de localhost en archivos temporales y no modifica el almacén de certificados de Windows. Para un servidor de escuela, prepara fuera del repositorio una CA de desarrollo y un certificado de servidor con SAN para el hostname/IP privado seleccionado. No uses un certificado autofirmado de producción ni copies la clave privada al proyecto.

El certificado raíz de esa CA tendría que verificarse e incorporarse mediante un proceso de configuración local antes de cualquier prueba física OTA. La ESP32 valida el certificado del servidor con `BANCO_OTA_SERVER_ROOT_CA_PEM`; no se permite omitir TLS ni usar `setInsecure()`.

Ejemplo de preparación **solo para desarrollo** con OpenSSL en un equipo donde esté instalado. Ejecuta el flujo en un directorio privado fuera del repositorio. OpenSSL solicita las contraseñas de las claves cifradas interactivamente; no las pongas en argumentos ni archivos del proyecto:

```text
openssl genpkey -algorithm EC -pkeyopt ec_paramgen_curve:P-256 -aes-256-cbc -out dev-ca.key.pem
openssl req -x509 -new -key dev-ca.key.pem -sha256 -days 90 -out dev-ca.pem -subj "/CN=Banco Escolar OTA Development CA" -addext "basicConstraints=critical,CA:TRUE" -addext "keyUsage=critical,keyCertSign,cRLSign"
openssl genpkey -algorithm RSA -pkeyopt rsa_keygen_bits:2048 -aes-256-cbc -out dev-server.key.pem
openssl req -new -key dev-server.key.pem -out dev-server.csr.pem -subj "/CN=ota-dev.school.lan"
```

Guarda un archivo local `dev-server.ext` con `subjectAltName=DNS:ota-dev.school.lan,IP:192.168.1.20`, `extendedKeyUsage=serverAuth` y `keyUsage=digitalSignature,keyEncipherment`. Sustituye el DNS/IP por el reservado en la LAN; el ejemplo `192.168.1.20` no es una dirección confirmada del plantel. Firma y revisa el certificado:

```text
openssl x509 -req -in dev-server.csr.pem -CA dev-ca.pem -CAkey dev-ca.key.pem -CAcreateserial -days 30 -sha256 -extfile dev-server.ext -out dev-server.pem
openssl verify -CAfile dev-ca.pem dev-server.pem
```

El DNS debe resolver al mismo equipo dentro de la LAN. No configures redirección de puertos en el router. Para usar la clave TLS cifrada, expón la contraseña solo al proceso de servidor mediante `BANCO_OTA_TLS_KEY_PASSPHRASE`; para la clave ECDSA de desarrollo, usa `BANCO_OTA_DEV_SIGNING_KEY_PASSPHRASE`. El entorno actual no tiene `openssl` en `PATH`, así que este aprovisionamiento queda pendiente.

## Flujo de artefactos de desarrollo

1. Genera una clave de desarrollo con `node tools/ota/ota_manifest.mjs keygen --private-key <ruta-fuera-del-repo> --public-key <ruta-publica>`.
2. Firma un firmware compilado con versión y URL exacta `https://<hostname>:<puerto>/firmware.bin` usando `node tools/ota/ota_manifest.mjs sign --firmware <firmware.bin> --version <x.y.z> --image-url <URL> --private-key <clave-privada> --output <manifest.json>`.
3. Verifica el par antes de servirlo con `node tools/ota/ota_manifest.mjs verify --manifest <manifest.json> --public-key <clave-publica> --firmware <firmware.bin>`.
4. Ejecuta el servidor de forma interactiva con `node tools/ota/ota_https_server.mjs --bind <IPv4-privada> --hostname <hostname-del-SAN> --port <puerto> --cert <cert.pem> --key <key.pem> --manifest <manifest.json> --firmware <firmware.bin>`.

Los ejemplos usan marcadores, no credenciales reales. El servidor no tiene rutas de carga ni de autorización/activación. Los artefactos se deben preparar antes de iniciar el servidor. No se debe pasar una clave privada de firma al servidor.

## Contrato de autorización del maestro

`src/network/ota_authorization.h` define una capacidad interna para una versión, URL y SHA-256 de los bytes exactos del manifiesto, con vencimiento monotónico y uso único. La emisión requiere un verificador de prueba de maestro inyectado; si el callback está ausente, falla cerrada. La gestión OTA vuelve a comparar los claims con el manifiesto descargado y comprueba el vencimiento antes de seleccionar la partición de arranque. La cancelación se acepta hasta el punto crítico inmediatamente anterior a seleccionar esa partición.

Todavía no existe identidad del maestro, mecanismo de autenticación, callback confiable, proveedor instanciado, integración con Panel Maestro ni ruta de autorización. Por eso no se puede emitir una autorización en el firmware actual y ninguna pantalla/solicitud remota puede iniciar una instalación. El futuro callback debe verificar una aprobación fresca ligada criptográficamente a la versión, URL y SHA-256 del manifiesto y consumir su reto antirrepetición; una aprobación debe referir a un manifiesto ya revisado. Definir cómo autenticar la identidad del maestro requiere una decisión de seguridad y queda pendiente.

## Contrato futuro con Panel Maestro

Se documentan operaciones conceptuales, sin escoger rutas HTTP ni una API pública definitiva:

- **Consulta:** versión instalada, plataforma/modelo y si existe una actualización firmada compatible.
- **Revisión:** metadatos de versión, tamaño y estado de firma; la autorización requiere un manifiesto específico.
- **Autorización:** identidad del maestro autenticada y aprobación explícita, limitada a un manifiesto, con caducidad y un solo uso.
- **Progreso:** estados `IDLE`, `CHECKING`, `DOWNLOADING`, `VERIFYING`, `READY_TO_REBOOT` o `FAILED`, bytes y error sin exponer secretos.
- **Cancelación:** disponible mientras no se haya cruzado el punto crítico de selección de partición.
- **Resultado:** después del reinicio, reportar versión activa y resultado de salud/rollback cuando exista un canal autorizado para consultarlo.

El Panel presenta la actualización y recoge la aprobación; un proveedor confiable verifica la identidad y el permiso; el motor del terminal valida TLS, firma, modelo, versión, tamaño y hashes, descarga y gestiona rollback. El servidor local solo distribuye artefactos estáticos y no autentica al maestro ni decide activar.

## Límites verificados

- Servidor: pruebas de integración en computadora con TLS, certificado no confiable, rutas/métodos permitidos y rechazo de enlace público.
- Firma: pruebas de criptografía real Node/OpenSSL P-256 con clave efímera, campos firmados e imagen modificada.
- Firmware: BUILD y pruebas de contrato no comprueban mbedTLS/TLS en el dispositivo, rollback físico, reinicio, recuperación USB mediante escritura ni interacción con pantalla/microSD.
- No se generaron claves de producción, no se configuró CA de desarrollo para el firmware y no se inició un servidor persistente.
