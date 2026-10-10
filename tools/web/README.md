# Validación HTTP de la terminal

`test_panel_http_live.py` envía solicitudes al servidor HTTP de archivos de prueba de la terminal y comprueba:

- `GET /`: HTTP 200, tipo HTML y contenido reconocible de la página de muestra.
- `GET /status`: HTTP 200, JSON válido, `sd_mounted: true` y `page_version` igual a la muestra local.
- `GET /panel-test/terminal.svg`: HTTP 200, tipo SVG y bytes iguales al archivo local.
- Archivo con nombre aleatorio inexistente: HTTP 404.
- Ruta fuera de la raíz de prueba (`/not-panel-test`): HTTP 400.
- `POST /` vacío: HTTP 405.

El cliente solo recibe la IP actual; no escanea la LAN, no contiene credenciales ni escribe en la microSD. La única solicitud que no es GET es un POST vacío para confirmar el rechazo 405. El límite de respuesta es 2 MiB por solicitud y cada conexión se cierra al terminar.

## Preparar los archivos de muestra en la microSD

Desde una computadora, copia la carpeta `tools/web/sample_sd/panel-test/` del repositorio directamente a la raíz de una microSD FAT de prueba. La tarjeta debe quedar con esta estructura:

```text
panel-test/
  index.html
  style.css
  app.js
  terminal.svg
  version.txt
```

No copies la carpeta contenedora `sample_sd`; la ruta visible para el firmware es `/sdcard/panel-test/index.html`. Conserva los demás archivos de la tarjeta y no la formatees. Para proteger datos existentes, prepara una tarjeta de prueba o haz copia de seguridad antes de modificarla. `version.txt` contiene `sd-web-test-1`, que se compara con `/status`.

## Ejecutar cuando la terminal esté conectada

1. Arranca la terminal con el firmware que incluye `PanelHttpServer` y comprueba que conectó al mismo router Wi-Fi que la computadora. Obtén la IP actual del mensaje de conexión Wi-Fi por el registro serial o de la lista de clientes DHCP del router; no uses una IP supuesta.
2. Prepara la microSD con los archivos de muestra y colócala en la terminal apagada; después inicia la terminal. El servidor actual no instala los archivos por Wi-Fi.
3. Desde la computadora conectada a esa misma LAN, en PowerShell:

   ```powershell
   $terminalIp = Read-Host "IP actual de la terminal"
   python tools/web/test_panel_http_live.py $terminalIp
   ```

   El puerto predeterminado es 80. Puedes cambiarlo con `--port` y el timeout con `--timeout 10`. Si la muestra local está en otra carpeta, indica `--sample-root RUTA`.
4. El resultado correcto termina con `HTTP CHECKS PASSED` y seis líneas `PASS`. Un fallo imprime `FAIL` con la comprobación que no coincidió.

Esta prueba requiere la terminal, la microSD preparada, Wi-Fi en la misma LAN y el puerto HTTP accesible. No confirma autenticación/TLS (que este servidor de prueba no tiene) ni prueba la API escolar o el Panel real.

## Preparar el paquete local del Panel Maestro

El Panel de `panel-maestro/` puede compilarse para quedar bajo `panel-test/maestro/`, que es una subcarpeta permitida por el servidor de archivos actual. El paquete usa rutas de React Router en el fragmento (`#/dashboard`), así no requiere que el servidor agregue rutas de aplicación ni una respuesta de escritura. El build normal del Panel sigue usando `BrowserRouter`.

Desde PowerShell, compila y prepara el paquete reproducible:

```powershell
Set-Location panel-maestro
npm run build:sd
py ..\tools\web\prepare_panel_sd_package.py
```

El resultado queda en `panel-maestro/package-sd/`:

```text
package-sd/
  manifest.json                 # lista de cada archivo, tamaño y SHA-256
  panel-test/maestro/
    index.html
    assets/                     # JS, CSS e imágenes emitidos por Vite
```

El script comprueba el cierre de dependencias del manifiesto Vite, verifica que los recursos HTML/CSS existan y copia únicamente los archivos requeridos. El manifiesto se ordena y no contiene fecha de generación. Si cambia el código o las dependencias, vuelve a compilar y ejecuta el script para regenerar el paquete y sus hashes. `dist-sd/` es la salida intermedia; `package-sd/` es el paquete a copiar.

Para la futura prueba física, con la terminal apagada, conserva el `panel-test/` de muestra y copia **el contenido** de `panel-maestro/package-sd/panel-test/` a la raíz de una microSD FAT de prueba. El destino será `panel-test/maestro/`; no reemplaces `panel-test/index.html`, `style.css`, `app.js`, `terminal.svg` ni `version.txt`. El manifiesto queda local en `package-sd/manifest.json`; no hace falta copiarlo a la microSD. Después de copiar, desde la raíz del repositorio verifica tamaños y SHA-256 contra la tarjeta montada (sustituye `E:\` por su unidad real):

```powershell
py tools\web\prepare_panel_sd_package.py --verify-root E:\
```

La comprobación también detecta archivos extra dentro de `panel-test/maestro/` y no modifica la tarjeta. No se debe copiar el directorio `package-sd` ni el manifiesto a la ruta de la aplicación.

Cuando el equipo esté disponible y la tarjeta esté preparada, abre desde un navegador de la misma LAN `http://<IP-actual-de-la-terminal>/panel-test/maestro/index.html#/dashboard`. No se incluye una IP en la documentación. `GET /` seguirá mostrando la página de muestra; la ruta directa abre la aplicación empaquetada. Esta etapa no agrega login, datos reales, saldos, movimientos, PIN, sincronización ni instalación por Wi-Fi. El Panel continúa usando su proveedor e IndexedDB de demostración en el navegador.

La preparación y sus comprobaciones son locales. No prueban acceso desde la ESP32, lectura FAT, respuesta MIME, memoria disponible ni comportamiento con tarjeta física; esas validaciones se harán cuando el usuario confirme que la terminal está disponible.

## Pruebas locales sin terminal

```powershell
$env:PYTHONDONTWRITEBYTECODE = "1"
python -m unittest tools.web.test_panel_http_contract tools.web.test_panel_http_live_client -v
```

`test_panel_http_live_client.py` ejecuta el mismo cliente HTTP contra un fixture temporal enlazado solo a loopback y puerto efímero; el test apaga el servidor y verifica que el hilo terminó. `test_panel_http_contract.py` conserva las comprobaciones estáticas del contrato del firmware. Estas pruebas locales no sustituyen el tráfico HTTP real con la ESP32.
