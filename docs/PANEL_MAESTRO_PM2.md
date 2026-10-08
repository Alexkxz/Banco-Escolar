# Banco Escolar · Fase PM.2

## Alcance

PM.2 convierte la estructura de PM.1 en una interfaz administrativa adaptable. La fase se limita a interfaz, navegación y componentes compartidos. No conecta API o base de datos ni implementa operaciones administrativas reales.

## Diseño y layout

Se seleccionó el diseño A: sidebar izquierdo, topbar y área central. El sidebar agrupa General, Gestión y Sistema; conserva las diez rutas de PM.1 y muestra la marca Banco Escolar con el logo existente, sin modificar el archivo original. El estado activo usa fondo azul tecnológico e indicador cian. El wordmark usa un marco de 91 px de alto y compensación horizontal basada en su alfa: PNG 2048×768, con contenido visible x=382–1827 y márgenes transparentes asimétricos de 382 px a la izquierda y 220 px a la derecha. El contenido completo queda dentro del marco; el recorte CSS afecta solo a los márgenes transparentes. En escritorio contraído se presenta el emblema cuadrado `Imagenes/Logo.png` con `contain`; el drawer móvil restablece el wordmark completo y ancho normal incluso si la preferencia de contraído sigue guardada.

En escritorio se puede contraer el sidebar a iconos y guardar esa preferencia visual en el navegador. La navegación ocupa el alto disponible del sidebar y desplaza su propia lista cuando los enlaces no caben; el footer de demostración conserva su espacio al fondo. En móvil se presenta como drawer, con cierre por Escape, botón, scrim y navegación. El layout común permanece al cambiar de módulo.

La topbar refleja el título y descripción de la ruta, versión de desarrollo y el indicador «Modo demostración». La zona de notificaciones está reservada e inactiva. La sesión indica «Sin iniciar»; no presenta un maestro ficticio.

## Dashboard y módulos

El Dashboard prepara tarjetas para Alumnos registrados, Áureos en circulación, Actividades activas y Movimientos recientes. Todos muestran «Sin datos». También incluye actividad reciente vacía, accesos rápidos a Alumnos, Cuentas, Actividades y Movimientos, y estado del sistema sin conexión configurada. «Áreas del panel» se genera solo desde las rutas previstas; se retiró un enlace manual duplicado de Asistencia. Sus seis módulos aparecen una vez cada uno, con una prueba que protege ese conteo.

Alumnos, Cuentas, Movimientos, Actividades, Cobros, Progreso académico, Asistencia, Dispositivos y Configuración comparten encabezado, marca de preparación y EmptyState. La ruta desconocida muestra una página 404 con acceso al Dashboard.

## Componentes y temas

Se reutilizan Button, Card, PageHeader, SectionHeader, StatusBadge y EmptyState. Lucide React mantiene iconografía SVG homogénea. El tema claro y oscuro comparte tokens adaptados de fondo, superficies, texto y acentos. El selector guarda solo el tema localmente y utiliza la preferencia del sistema cuando todavía no hay selección. Un script temprano aplica el tema antes de montar React para reducir parpadeos.

El tema claro ahora distingue mejor el fondo y las tarjetas: canvas gris azulado, superficies blancas, bordes más visibles, sombras ligeras y texto secundario más oscuro. Los textos secundarios comunes se elevaron a 11–14 px; etiquetas compactas quedan en 10 px para conservar jerarquía.

## Responsive y accesibilidad

La cuadrícula de tarjetas cambia de cuatro columnas a dos y luego a una; en móvil las métricas apilan sus tarjetas para mantener el ancho de textos dentro del viewport. La descripción superior del módulo se ajusta en varias líneas y el topbar compacto usa una etiqueta breve «DEMO». El resto de secciones se adapta a pantallas medianas y móviles sin anchuras fijas globales. Los controles tienen etiquetas accesibles, los enlaces usan semántica de navegación, el estado activo usa `aria-current`, los botones del drawer exponen estado y control, el foco visible se conserva y el menú responde a Escape. Se respeta `prefers-reduced-motion`.

El emblema del sidebar contraído se presenta como imagen importada con `object-fit: contain`, igual que el wordmark y sin editar los PNG originales.

## Verificación y pendientes

### Alcance no implementado

- El contenido y el servicio son demostrativos; no hay datos escolares ni estadísticas reales.
- No hay formularios funcionales, autenticación, API, base de datos, sincronización ni operaciones monetarias.
- «Modo demostración» describe la interfaz; no indica conexión con un backend.

### Capturas estáticas

- Chrome headless comprobó las diez rutas y 404 a 390×844 y 320×844. Las 22 vistas quedaron con `documentElement.scrollWidth` igual al ancho emulado y sin desborde en encabezado ni topbar. Las capturas v02 documentan estas vistas estáticas; también se revisaron visualmente Dashboard/404 a 320 px, Configuración a 1366×768, sidebar contraído y drawer móvil abierto a ambos anchos. El logo completo se ve en sidebar expandido y drawer móvil; el emblema cargado se ve en sidebar contraído.

### Interacciones

- En 1366×768, la lista del sidebar midió 590 px de contenido y 558 px disponibles; desplazada al final, Configuración quedó entre y=646–689, dentro del nav (hasta y=701) y por encima del footer (desde y=701). La interacción de Tab y Enter abrió Configuración; también se verificó por ratón y teclado la selección de cada una de las diez rutas. En el drawer se comprobaron cierre con Escape, botón y scrim.

### Pendiente

- Pendiente: revisar persistencia del tema tras recargar y la apariencia oscura en un navegador visible. La automatización visual se hizo en Chrome headless; las capturas no sustituyen una revisión manual de contraste en el navegador del usuario.
- Los dos PNG originales permanecen intactos; el bundle contiene el wordmark de 1.29 MB y el emblema cuadrado de 1.33 MB. No había convertidor local disponible para crear variantes optimizadas.

## Validación

- `npm run test`: SUCCESS, 6 pruebas; incluye una aserción de seis enlaces en «Áreas del panel» y una sola aparición de Asistencia.
- `npm run build`: SUCCESS, TypeScript y Vite sin errores ni warnings de compilación. Bundle: JavaScript 279.57 kB (89.14 kB gzip), CSS 15.20 kB (3.92 kB gzip); recursos gráficos originales: wordmark 1,293.17 kB y emblema 1,332.62 kB.
- `npm run dev -- --host 127.0.0.1`: Vite v8.3.3 inició en `http://127.0.0.1:5173/`; en esta revisión sigue activo y la solicitud local respondió 200.
- `git diff --check`: SUCCESS. Git solo emitió avisos CRLF para archivos previamente modificados.
- No se compiló el firmware: PM.2 solo cambia archivos del Panel, README y esta documentación.
- No hubo validación física ni se conectó hardware. No Upload, Commit ni Push.

## Próxima fase recomendada

PM.3 implementa después un Dashboard alimentado con datos ficticios locales y de solo lectura; el alcance, las diferencias de modelo y la validación están en [PANEL_MAESTRO_PM3.md](PANEL_MAESTRO_PM3.md). Antes de conectar cualquier fuente real se deben definir el contrato de lectura, los límites de seguridad y la persistencia.
