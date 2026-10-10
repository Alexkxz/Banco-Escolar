# Banco Escolar · Panel Maestro

Aplicación web independiente del firmware. PM.1 estableció la base React, TypeScript, Vite, React Router y el proveedor demo. PM.2 desarrolla el layout administrativo, navegación visual y páginas preparadas sin conectar API ni datos reales.

PM.7A agrega ajustes de saldo al servicio demo; desde PM.9B sus cambios se conservan localmente por IndexedDB. Consulta [alcance, límites y verificaciones de PM.7A](../docs/PANEL_MAESTRO_PM7A.md). La revisión visual con navegador y capturas nuevas sigue pendiente; las pruebas automatizadas no equivalen a validación visual.

PM.8 activa consultas y capturas/correcciones demo de progreso académico y asistencia, reglas escolares versionadas y aplicaciones separadas de Áureos. Progreso incluye subpestañas de resumen, fluidez lectora, dictado y comprensión con filtros compartidos, gráficas y tablas accesibles. Desde PM.9B las operaciones se conservan en IndexedDB local; no se sincronizan. Consulta [PANEL_MAESTRO_PM8.md](../docs/PANEL_MAESTRO_PM8.md). Se comprobó responsive con Chrome headless; queda pendiente la inspección visual de las gráficas con evaluaciones registradas y la revisión de teclado.

PM.9A define el diseño y PM.9B implementa IndexedDB solo para la demostración, con seed único, guardado de operaciones e imágenes Blob. El espacio real no se abre. Consulta [PM.9A](../docs/PANEL_MAESTRO_PM9A.md) y el estado/verificaciones de [PM.9B](../docs/PANEL_MAESTRO_PM9B.md). La validación nativa de IndexedDB de PM.9B está aprobada; detalle en [PM.9B](../docs/PANEL_MAESTRO_PM9B.md).

PM.9C agrega descarga y restauración de respaldos versionados con datos y fotos, validación completa previa, confirmación de copia de seguridad reciente y reemplazo atómico solo del espacio demo seleccionado. La prueba Chromium/IndexedDB nativo, resultados, límites y pendientes están en [PANEL_MAESTRO_PM9C.md](../docs/PANEL_MAESTRO_PM9C.md).

## Tecnologías

- React y TypeScript para interfaz y tipos.
- Vite para desarrollo y compilación.
- React Router para las diez rutas.
- Vitest, Testing Library y jsdom para pruebas de interfaz.
- Lucide React para iconos SVG coherentes.
- CSS propio y variables para temas claro y oscuro.

Las versiones están fijadas en `package-lock.json`. Requiere Node.js 20.19+ o 22.12+.

## Estructura

```text
src/
  app/         rutas y composición
  components/  controles, tarjetas, encabezados y estados reutilizables
  layouts/     sidebar, topbar y contenedor persistentes
  models/      tipos de cliente orientativos; no son contratos de API
  pages/       Dashboard visual y módulos preparados
  services/    frontera de lectura, consultas y proveedor demo
  students/    fotos demo cargadas como Blob; previews con URL temporal
  styles/      tokens de tema y estilos responsive
 tests/         navegación, tema, estados, fotos e aislamiento demo
```

## Desarrollo y validación

```sh
npm install
npm run dev
npm run test
npm run build
```

El servidor de Vite imprime su dirección local. `build` valida tipos con TypeScript y genera archivos estáticos en `dist/`. Estas tareas no compilan ni modifican firmware.

## Interfaz PM.2

El layout conserva sidebar, encabezado y contenido al navegar. En escritorio el sidebar se puede contraer; en pantallas pequeñas funciona como drawer y se cierra con Escape, con el botón o al cambiar de ruta. El tema guarda únicamente la preferencia visual local y, al iniciar sin selección anterior, respeta el tema del sistema.

El Dashboard consume `PanelDataService`. El proveedor local presenta un conjunto determinista de datos ficticios y solo lectura; calcula alumnos, suma de saldos de cuentas, actividades activas y movimientos totales, y lista los cinco movimientos más recientes. Alumnos añade un directorio filtrable y perfiles por ID con la misma colección. Cuentas presenta saldos directos, alumnos sin cuenta y un historial disponible por cuenta sin reconstruir su saldo. Movimientos permite consulta, filtros y detalle, y mantiene relaciones solo cuando existen en los claims demo. Dashboard, cuentas y movimientos presentan fechas con `America/Mexico_City`; los timestamps almacenados permanecen intactos. Se separan carga, colección vacía, error y filtros sin coincidencias. Ningún módulo realiza operaciones monetarias o sincroniza datos. Los otros módulos conservan preparación y estado vacío. Las rutas inexistentes presentan una vista 404.

Los perfiles usan iniciales y color determinista. El control excepcional de imagen acepta PNG/JPEG hasta 5 MiB después de revisar MIME, firma y decodificación. Las imágenes se guardan como Blob en IndexedDB, asociadas a `student_id`; las URLs de preview son temporales y no se envían a la terminal. Las URLs temporales se liberan al reemplazarlas, quitarlas o desmontar el proveedor.

La marca usa el logo original de `Imagenes/Logo y nombre.png`; el recurso se importa sin alterar el original. Los iconos SVG proceden de una biblioteca pequeña única.

## Límites y fases futuras

El proveedor `DemoPanelDataService` sirve colecciones ficticias y no lee ni escribe datos del firmware. PM.9B persiste en IndexedDB solo los datos demo. No existe API, base central, autenticación ni sincronización. No hay Service Worker ni funcionamiento offline garantizado. Los tipos de `models/` tampoco definen contratos de servidor. El estado ACTIVE/INACTIVE de estudiantes es solo de la demo; no existe en el modelo actual del firmware.

PM.2 documenta el diseño base en [PANEL_MAESTRO_PM2.md](../docs/PANEL_MAESTRO_PM2.md); PM.3 documenta el Dashboard demo en [PANEL_MAESTRO_PM3.md](../docs/PANEL_MAESTRO_PM3.md); PM.4 documenta directorio, perfiles e imagen temporal en [PANEL_MAESTRO_PM4.md](../docs/PANEL_MAESTRO_PM4.md); PM.5 documenta consultas de cuenta y movimiento en [PANEL_MAESTRO_PM5.md](../docs/PANEL_MAESTRO_PM5.md); PM.6 documenta actividades de demostración en [PANEL_MAESTRO_PM6.md](../docs/PANEL_MAESTRO_PM6.md). Las actividades se guardan localmente en IndexedDB, usan la hora de la computadora y no se sincronizan con la terminal.

PM.10A propone que la ESP32-S3 aloje API y sirva archivos del Panel desde microSD a equipos en el mismo router Wi-Fi; nube opcional para backup/acceso remoto. Terminal conserva IDs y es fuente primaria. Firmware ya implementa un servidor HTTP temporal de solo lectura para un paquete de prueba en `panel-test/`, sin API escolar, autenticación o actualización. La tarjeta no ha sido probada físicamente con el servidor. IndexedDB demo permanece aislada. Ver [PANEL_MAESTRO_PM10A.md](../docs/PANEL_MAESTRO_PM10A.md).

El build `npm run build:sd` prepara una variante estática para `/panel-test/maestro/`; conserva la página de muestra en `/panel-test/` y usa rutas con fragmento para funcionar sin fallback del servidor. Ejecuta luego `py ..\tools\web\prepare_panel_sd_package.py` para generar `package-sd/` y su `manifest.json` con tamaño y SHA-256 por archivo. El paquete local no significa que esté copiado en la tarjeta o validado por el firmware. Pasos y límites: [tools/web/README.md](../tools/web/README.md).
