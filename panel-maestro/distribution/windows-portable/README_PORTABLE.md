# Banco Escolar — Panel Maestro portable

## Uso

1. Descomprime esta carpeta en una ubicación local.
2. Ejecuta `Abrir-Banco-Escolar.bat` con doble clic.
3. Mantén abierta la ventana del lanzador mientras utilizas el Panel.
4. Escribe `salir` y presiona Enter para detener el servidor de forma ordenada.

El paquete incluye Node.js v24.22.0 para Windows x64. No requiere instalar Node.js, tener `npm` ni conexión a Internet para iniciar. El servidor escucha solo en `127.0.0.1:4173`.

Si el puerto está ocupado, cierra la instancia del Panel que lo usa o revisa qué aplicación ocupa el puerto antes de detenerla. En la ventana de otra instancia del Panel, escribe `salir`. El lanzador no termina procesos ajenos. Los errores y eventos de inicio/parada aparecen en la ventana y en `%LOCALAPPDATA%\BancoEscolar\logs\panel-server.log`.

La documentación técnica está en `SERVER_LOCAL.md`. El origen del runtime y su hash están en `NODE_RUNTIME_SOURCE.txt`; los avisos de terceros y textos de licencia están incluidos en `THIRD_PARTY_NOTICES.md`, `licenses/` y dentro de la carpeta del runtime.

La interfaz sigue en modo demostración y conserva su almacenamiento IndexedDB del navegador. Mantén estable la dirección `http://127.0.0.1:4173/` para reutilizar los datos locales del mismo perfil del navegador. No se incluye comunicación con la terminal.
