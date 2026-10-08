# Banco Escolar · Panel Maestro

Aplicación web independiente del firmware. PM.1 estableció la base React, TypeScript, Vite, React Router y el proveedor demo. PM.2 desarrolla el layout administrativo, navegación visual y páginas preparadas sin conectar API ni datos reales.

PM.7A agrega ajustes de saldo solo en memoria y en el servicio de demostración. Consulta [alcance, límites y verificaciones de PM.7A](../docs/PANEL_MAESTRO_PM7A.md). La revisión visual con navegador y capturas nuevas sigue pendiente; las pruebas automatizadas no equivalen a validación visual.

PM.8 activa consultas y capturas/correcciones demo de progreso académico y asistencia, reglas escolares versionadas y aplicaciones separadas de Áureos. Progreso incluye subpestañas de resumen, fluidez lectora, dictado y comprensión con filtros compartidos, gráficas y tablas accesibles. Las operaciones se pierden al recargar y no se sincronizan. Consulta [PANEL_MAESTRO_PM8.md](../docs/PANEL_MAESTRO_PM8.md). Se comprobó responsive con Chrome headless; queda pendiente la inspección visual de las gráficas con evaluaciones registradas y la revisión de teclado.

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
  students/    imágenes temporales en memoria para perfiles demo
  styles/      tokens de tema y estilos responsive
 tests/         navegación, tema, estados y aislamiento demo
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

Los perfiles usan iniciales y color determinista. El control excepcional de imagen acepta PNG/JPEG hasta 5 MiB después de revisar MIME, firma y decodificación. Las imágenes elegidas solo viven en memoria, asociadas a `student_id`; desaparecen al recargar y no se envían a la terminal. Las URLs temporales se liberan al reemplazarlas, quitarlas o desmontar el proveedor.

La marca usa el logo original de `Imagenes/Logo y nombre.png`; el recurso se importa sin alterar el original. Los iconos SVG proceden de una biblioteca pequeña única.

## Límites y fases futuras

El proveedor `DemoPanelDataService` sirve colecciones ficticias y no lee ni escribe datos del firmware. No existe API, base central, autenticación, operaciones escolares, movimiento monetario, sincronización, Service Worker ni funcionamiento offline garantizado. Los tipos del directorio `models/` tampoco definen contratos de servidor. El estado ACTIVE/INACTIVE de los estudiantes es solo de la demo; no existe en el modelo actual del firmware.

PM.2 documenta el diseño base en [PANEL_MAESTRO_PM2.md](../docs/PANEL_MAESTRO_PM2.md); PM.3 documenta el Dashboard demo en [PANEL_MAESTRO_PM3.md](../docs/PANEL_MAESTRO_PM3.md); PM.4 documenta directorio, perfiles e imagen temporal en [PANEL_MAESTRO_PM4.md](../docs/PANEL_MAESTRO_PM4.md); PM.5 documenta consultas de cuenta y movimiento en [PANEL_MAESTRO_PM5.md](../docs/PANEL_MAESTRO_PM5.md); PM.6 documenta actividades de demostración en [PANEL_MAESTRO_PM6.md](../docs/PANEL_MAESTRO_PM6.md). Las actividades se mutan solo en memoria, usan la hora de la computadora y no se sincronizan con la terminal.
