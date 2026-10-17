# Servidor local del Panel Maestro para Windows

## Requisitos

- Windows.
- Node.js 20.19+ o 22.12+. Para desarrollo se acepta Node del sistema; el paquete portable futuro incluirá un runtime junto a la aplicación.
- Un build normal reciente en `panel-maestro/dist/` (`npm run build`). El lanzador no ejecuta Vite, no descarga Node ni instala dependencias.

## Iniciar y detener

Haz doble clic en `Abrir-Banco-Escolar.bat`. Busca primero `runtime/node.exe` y `runtime/node-v*-win-x64/node.exe`; si no encuentra runtime portable, utiliza Node del sistema. Comprueba el servidor, el build y la versión del runtime; luego inicia el proceso en primer plano. Cuando Node confirma que escucha, abre `http://127.0.0.1:4173/` en el navegador predeterminado.

Deja abierta la ventana del lanzador mientras uses el Panel. Para cerrarlo sin el aviso de `cmd.exe`, escribe `salir` y presiona Enter. El proceso deja de aceptar solicitudes, termina las solicitudes activas y cierra el puerto. Si una solicitud queda bloqueada, tras cinco segundos se fuerzan las conexiones pendientes. Si se cierra la entrada de la consola, el servidor también inicia el cierre ordenado.

## Puerto 4173 ocupado

El servidor no cambia de puerto automáticamente para mantener estable el origen del navegador y sus datos demo. Si el arranque indica que el puerto está ocupado:

1. Revisa si hay otra ventana del Panel Maestro abierta y detén esa instancia escribiendo `salir`.
2. Si no encuentras una instancia del Panel, revisa qué aplicación usa el puerto 4173 antes de cerrarla.
3. Vuelve a abrir el lanzador cuando el puerto quede libre.

El lanzador no termina procesos ajenos ni intenta cerrar el listener que ocupa el puerto.

## Registros

El servidor escribe eventos de inicio, parada y error en:

```text
%LOCALAPPDATA%\BancoEscolar\logs\panel-server.log
```

El puerto ocupado, Node incompatible y un build ausente también se explican en la ventana del lanzador. Si el registro no puede escribirse, el servidor informa el problema en consola.

## Seguridad y alcance actual

El servidor solo hace `bind` en `127.0.0.1`, admite GET/HEAD y entrega archivos desde `dist/`. Las rutas desconocidas sin extensión sirven `index.html` para React Router; las extensiones inexistentes devuelven 404. El directorio no se lista, las rutas se canonizan y las referencias simbólicas que escapen de `dist/` se rechazan.

No hay API escolar ni endpoints bancarios. El servidor no lee ni modifica IndexedDB, alumnos, cuentas, saldos, movimientos o cobros. La página sigue usando el proveedor demo y IndexedDB del navegador. Los datos de IndexedDB están ligados al origen: conserva `http://127.0.0.1:4173` estable para acceder a la misma base local de este servidor. Esta no es una migración ni un respaldo nuevo.

## Preparar una distribución portable

Se preparó una carpeta portable Windows x64 en `distribution/windows-portable/`, con el lanzador, servidor, build actual, Node.js portable v24.22.0 y licencias. El runtime se verificó contra el SHA-256 publicado en `SHASUMS256.txt` de Node.js. El paquete no incluye `node_modules` del Panel ni descarga dependencias al arrancar.

La validación de este paquete en una cuenta Windows sin Node instalado todavía corresponde a una comprobación manual. El paquete incluido es x64; Windows ARM64 sin emulación x64 requerirá su runtime correspondiente.

Los logos eliminados no se restauraron. `src/layouts/AppLayout.tsx` usa dos SVG transparentes temporales en `src/assets/`; para incorporar los recursos definitivos, Diseño debe reemplazar esos imports por los archivos aprobados. El menú, la navegación y las dimensiones existentes no se alteraron.

No existe todavía un ejecutable ni una base SQLite. La selección y empaquetado de SQLite pertenece a una fase futura y debe conservar la separación respecto de IndexedDB demo.

## Pruebas

Desde `panel-maestro/`, con Node compatible:

```powershell
npm run test:server
```

Las pruebas crean un `dist` fixture y servidores temporales ligados a loopback/puertos efímeros; los detienen y eliminan el fixture temporal al terminar. Si existe `dist/index.html`, también prueban el artefacto compilado local, que puede ser anterior al código fuente actual.

El build de esta copia debe realizarse solo después de resolver el estado de los imports visuales reportado en la auditoría. No se restauran ni sustituyen logos como parte de las instrucciones de servidor.
