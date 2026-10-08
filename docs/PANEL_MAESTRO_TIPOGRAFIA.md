# Ajuste general de tipografía del Panel Maestro

Fecha: 2026-10-07

## Alcance

Se aplicó una escala compartida en rem a las páginas, formularios, tablas, navegación, estados y confirmaciones del Panel Maestro. El ajuste conserva el logotipo, la estructura del panel y sus distribuciones por módulo. Se aumentaron la altura base de controles y el interlineado, y se permiten saltos de línea en etiquetas, botones y textos extensos para que el contenido no dependa de ocultar texto ni de reducir el zoom.

Los tokens están definidos en `panel-maestro/src/styles/tokens.css` y los estilos compartidos en `panel-maestro/src/styles/global.css`:

| Uso | Token | Tamaño |
| --- | --- | ---: |
| Texto general, tablas, formularios, botones y navegación | `--type-body` | 1 rem (16 px) |
| Texto secundario, ayudas, fechas y estados | `--type-secondary` | 0.875 rem (14 px) |
| Títulos de sección | `--type-section` | 1.25 rem (20 px) |
| Títulos principales | `--type-main-title` | 1.75–2 rem (28–32 px) |
| Valores destacados | `--type-value` | 1.375 rem (22 px) |
| Interlineado general | `--line-body` | 1.5 |
| Altura mínima de control | `--control-height` | 2.75 rem (44 px) |

Los títulos principales usan `clamp()` con rem para conservar el rango solicitado entre tamaños de viewport. Las confirmaciones usan la misma escala de botones y texto general.

## Resultado de comprobaciones

- Búsqueda de fuentes de tamaño explícito en px dentro de `panel-maestro/src/styles`: no encontró declaraciones `font-size` o `font` con tamaños en px; los tamaños de texto se resuelven mediante los tokens compartidos.
- Pruebas web: `npm.cmd test`, 41 pruebas aprobadas en 7 archivos.
- Build web: `npm.cmd run build`, TypeScript y Vite terminaron correctamente.
- `git diff --check`: pasó; Git mostró solo avisos de conversión LF/CRLF en archivos locales preexistentes.
- Revisión visual escritorio, 390 px y 320 px, en ambos temas: pendiente. Chrome headless no expuso el endpoint CDP en este entorno, y la automatización disponible para capturas no pudo inicializarse. No se generaron capturas v06 ni se afirma validación visual.
- La accesibilidad a todos los módulos con ratón y teclado y la ausencia visual de cortes, superposiciones y desbordamiento requieren verificación en navegador.

Al retomar la revisión visual, generar capturas nuevas en `panel-maestro/capturas/escritorio/` y `panel-maestro/capturas/movil/` con el siguiente número de versión libre, conservando todas las capturas anteriores. Las comprobaciones automatizadas de CSS/build no sustituyen esa revisión visual.
