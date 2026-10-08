# Capturas del Panel Maestro

Este directorio conserva la evolución visual real del panel. Las capturas se generan desde la aplicación ejecutándose en un navegador; no guardar maquetas ni imágenes generadas como si fueran vistas reales.

## Carpetas

- `escritorio/`: vistas de escritorio.
- `movil/`: vistas móviles.

## Nombre de archivo

Usa `AAAA-MM-DD--apartado--vista--vNN.png`, por ejemplo:

- `escritorio/2026-10-07--dashboard--escritorio--v01.png`
- `movil/2026-10-07--alumnos--movil--v01.png`
- `escritorio/2026-10-07--dashboard--sidebar-contraido--v02.png`

Conserva las versiones anteriores para comparar cambios.

## Línea base 2026-10-07

Hay capturas PNG de las diez rutas y la página 404 en escritorio (1366×768) y móvil (390×844). Son vistas estáticas de cada ruta; no incluyen el sidebar contraído ni el drawer abierto.

La inspección de `movil/2026-10-07--dashboard--movil--v01.png` y `movil/2026-10-07--404--movil--v01.png` mostró contenido cortado en el borde derecho a 390 px: las métricas conservaban dos columnas y el topbar/descripción no se ajustaban bien.

## Corrección v02

Chrome headless guardó vistas estáticas de las diez rutas y 404 en 390×844 y 320×844. Las 22 vistas comprobaron `scrollWidth` igual al ancho de viewport. Dashboard y 404 a 320 px se revisaron visualmente; los textos se leen completos y las tarjetas no exceden el ancho.

- `movil/2026-10-07--dashboard--390px--v02.png` y `movil/2026-10-07--404--390px--v02.png`
- `movil/2026-10-07--dashboard--320px--v02.png` y `movil/2026-10-07--404--320px--v02.png`
- Las otras rutas siguen el mismo patrón de nombre para ambos anchos.
- `escritorio/2026-10-07--configuracion--escritorio--sidebar-scroll--v02.png` muestra Configuración completa a 1366×768, encima del footer de demostración.
- `escritorio/2026-10-07--configuracion--escritorio-sidebar-contraido--v02.png` documenta el sidebar contraído.
- `movil/2026-10-07--dashboard--movil-drawer-abierto--v02.png` y `movil/2026-10-07--dashboard--320px-movil-drawer-abierto--v02.png` documentan el drawer abierto y el logo completo a ambos anchos.

Las capturas anteriores son vistas estáticas. Las pruebas de interacción se registran aparte: Chrome verificó selección de las diez rutas con ratón y teclado, cierre del drawer con Escape, botón y scrim, y desplazamiento de la navegación hasta Configuración sin que el footer tape enlaces. La persistencia del tema al recargar sigue pendiente; las vistas oscuras de PM.3 se comprobaron en Chrome headless.

## Dashboard PM.3 — v03

Se generaron seis capturas estáticas del Dashboard en Chrome headless: escritorio 1366×768, móvil 390×844 y móvil 320×844; cada tamaño en tema claro y oscuro. Las imágenes usan `dashboard--escritorio-claro/oscuro--v03` y `dashboard--movil-390px/movil-320px-claro/oscuro--v03`. Se verificó que el ancho desplazable no supera el ancho útil, que el tema aplicado coincide con cada captura y que los cuatro indicadores y cinco movimientos se cargan en las seis vistas.

## Directorio y perfiles PM.4 — v04 pendiente

Se requieren capturas estáticas del directorio y el perfil con iniciales y con una imagen genérica, en escritorio 1366×768 y móvil 390×844/320×844, ambos temas. No se generaron en esta ejecución: Chrome headless terminó con error GPU de acceso denegado antes de abrir la aplicación. No hay imágenes v04 y no se declara revisión visual. Mantener todas las versiones previas.

## Cuentas y movimientos PM.5 — v05 pendiente

Se requieren capturas de `/cuentas`, detalle de cuenta, `/movimientos` y detalle de movimiento en escritorio 1366×768 y móvil 390×844/320×844, con tema claro y oscuro. Chrome headless volvió a salir antes de exponer el endpoint CDP; no se generaron capturas v05 ni se declara revisión visual. Conservar todas las versiones anteriores.

## Actividades PM.6 — v02

Chrome headless generó capturas estáticas nuevas de la lista, detalle de la actividad 8104 y formulario de creación; cada vista se capturó en escritorio 1366 px, móvil 390 px y móvil 320 px, en tema claro y oscuro. Los archivos usan los prefijos `actividades`, `actividad-8104` y `crear-actividad`, con `--v02`, en `escritorio/` o `movil/`. CDP confirmó el tema y `scrollWidth` igual o menor que el viewport en los 18 casos; también se inspeccionaron visualmente capturas móviles representativas. Las capturas no validan por sí solas teclado, filtros, formularios ni confirmaciones.

Después de ajustar la presentación de duraciones largas, el detalle 8104 tiene capturas estáticas v03 en los mismos tres tamaños y dos temas; las v02 se conservan.

### Formulario PM.6 — distribución y selección v03

Las capturas `crear-actividad-seleccion` v03 muestran las dos columnas de escritorio y el formulario apilado en móvil con la lista de alumnos desplegada. Se guardaron en ambos temas a 1366×768, 390×844 y 320×844. CDP confirmó tema correcto y que `scrollWidth` de documento/cuerpo no excedió el viewport. Se inspeccionaron visualmente escritorio claro, móvil 390 claro y móvil 320 oscuro. Las 41 pruebas web ejecutan Elegir todos, Limpiar, Invertir, selección vacía, preservación al cambiar de modalidad, actualización del resumen y orden de tabulación.

Los valores, relaciones, estados y cálculos de dominio se cubren además mediante 38 pruebas automatizadas y el build web. La revisión manual de las interacciones y del sidebar/drawer dentro de PM.6 queda pendiente.

## Apartados

Dashboard, Alumnos, Cuentas, Movimientos, Actividades, Cobros, Progreso académico, Asistencia, Dispositivos y Configuración. Añade 404 y estados interactivos (sidebar contraído o drawer abierto) cuando sean relevantes para el cambio.

Cuando una modificación afecte una página, informa al usuario de la página ajustada y guarda al final una captura actualizada de esa página en la carpeta de viewport correspondiente. Si no hay navegador disponible, indícalo y deja la captura pendiente; no inventes una captura.

## Tipografía compartida — 2026-10-07

Se aplicó la escala tipográfica general descrita en `docs/PANEL_MAESTRO_TIPOGRAFIA.md`, con tokens rem compartidos para texto general, texto secundario, encabezados, valores y controles. Las capturas de esta revisión quedan pendientes: Chrome headless no expuso CDP y no fue posible capturar vistas posteriores al ajuste. Las capturas anteriores se mantienen como referencias previas y no representan esta versión tipográfica. La inspección visual de escritorio, 390 px y 320 px en ambos temas, así como el acceso al menú con ratón y teclado, también queda pendiente.

## Cobros PM.7 — v02 pendiente

Se implementó el listado, detalle, confirmación de anulación, confirmación de autorización y simulación del nuevo cobro dentro del entorno demo. Las capturas v01 existentes son anteriores a PM.7 y se conservan. Las nuevas capturas estáticas v02, en escritorio y 390/320 px, tema claro y oscuro, quedan pendientes porque Chrome headless no expuso CDP en este entorno. Las pruebas de componentes y servicios no se consideran revisión visual ni sustituyen esas capturas.
