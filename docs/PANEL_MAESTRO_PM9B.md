# Banco Escolar · Panel Maestro · Fase PM.9B

## Estado

**Implementación local demo terminada; seis escenarios nativos de navegador aprobados.** La aplicación abre exclusivamente `banco-escolar-panel-demo` (esquema 1). El espacio `banco-escolar-panel-real` no se abre ni se crea. No hay backend, autenticación, sincronización con firmware ni respaldo.

## Validación de navegador real (08/10/2026)

- Se corrigió el cierre en Windows de `scripts/pm9b-browser-test.mjs`: el runner observa `close` desde inmediatamente después de cada `spawn`, antes de cerrar Chromium. La ejecución identificó además que CDP `browser.close()` no termina el Chromium externo; el runner espera, verifica por CIM el PID actual junto con ejecutable y argumentos del perfil/puerto, envía una señal dirigida solo si esa identidad coincide y vuelve a esperar `close`. Si no confirma la salida, conserva el perfil temporal. Esto distingue el cierre real del evento de salida perdido por el listener tardío del código anterior.
- Antes de cerrar recursos de la ejecución anterior se inspeccionaron proceso, ejecutable, perfil temporal, hora de inicio y puerto local. No se encontró Chromium/Vite de esa prueba, perfil temporal ni listener; no se cerró ningún recurso de ese intento.
- `npm.cmd run test:browser` pasó los **6/6 escenarios** usando Chromium de Playwright, IndexedDB nativo y perfiles desechables aislados:
  1. Seed único de datos ficticios; un ajuste de prueba se conservó al recargar sin reseed.
  2. Saldo, movimiento y Blob conservados al recargar y cerrar/reabrir Chromium con el mismo perfil.
  3. Reemplazo de foto asociado únicamente al perfil ficticio seleccionado; eliminación conservada al recargar.
  4. Aborto controlado de transacción IndexedDB nativa en `movements.put`: saldo y movimiento revirtieron juntos, sin mensaje de éxito.
  5. Dos confirmaciones concurrentes con estado obsoleto: solo una aplicación monetaria y un movimiento.
  6. Fallo controlado de `indexedDB.open`: pantalla de error, sin dashboard, falso éxito ni fallback en memoria.
- Recursos de la ejecución final: el runner cerró Chromium y Vite de forma dirigida, esperó la terminación y eliminó los perfiles temporales al confirmar la salida. La inspección posterior no encontró procesos del runner, listener en el puerto local ni perfiles temporales. Se conservó el navegador habitual y el servidor de revisión autorizado.
- `npm.cmd test -- --reporter=dot`: **13 archivos, 83 pruebas aprobadas**. `npm.cmd run build`: **SUCCESS**, TypeScript y Vite. `git diff --check`: **SUCCESS**; Git solo mostró avisos informativos de conversión LF/CRLF.

## Intento automatizado anterior (08/10/2026)

- `@playwright/test` 1.64.0 está presente en las dependencias locales; Chromium de Playwright estaba disponible. No se reinstaló nada.
- Se añadió `npm run test:browser` con Playwright, Chromium CDP y lectura directa de stores nativos; incluye los seis escenarios requeridos, un perfil efímero y aborto controlado en `movements.put`. El runner está en `panel-maestro/scripts/pm9b-browser-test.mjs`.
- `npm.cmd test -- --reporter=dot` pasó: 13 archivos, 83 pruebas.
- En un intento anterior la prueba de navegador no llegó a CDP ni ejecutó escenarios porque Chromium no publicó `DevToolsActivePort`. Ese estado fue superado por la validación nativa 6/6 descrita arriba.
- El runner inició Vite y Chromium con un perfil temporal. Después del cierre reportado como fallido, se verificó que ambos procesos y el directorio temporal ya no existían. El navegador habitual y el servidor de revisión autorizado no se tocaron.
- En aquel intento se detuvo el flujo antes de build y `git diff --check`. Ya no representa el estado vigente. La guía manual siguiente queda como referencia alternativa y no como resultado ejecutado.
## Implementación

- IndexedDB se inicializa antes de mostrar las rutas. Si no está disponible o la apertura/seed falla, la aplicación presenta un error y no cambia a datos en memoria.
- El seed conocido de PM.3 y sus metadatos se escriben en una transacción única solo para una base nueva. Un seed incompleto o stores con datos sin marcador bloquean la inicialización para conservarlos. Las siguientes aperturas cargan el snapshot guardado; no vuelven a sembrar.
- Las mutaciones existentes de actividades, cobros, ajustes, registros académicos, asistencia y reglas esperan la confirmación de persistencia antes de publicar el nuevo estado en el servicio/UI. Una mutación reemplaza atómicamente los stores del snapshot y actualiza revisión/contadores; la revisión evita aplicar un snapshot obsoleto de otra pestaña. Un conflicto refresca el estado actual y exige reintento.
- Las fotos guardan Blob por `student_id`, MIME PNG/JPEG, nombre, tamaño, dimensiones validadas y fecha. Reemplazo y eliminación esperan la transacción; un fallo conserva el preview vigente. Las URLs `blob:` son temporales y se revocan al sustituir, quitar o desmontar.
- La conexión responde a `versionchange`; los errores de cuota, incompatibilidad, apertura y transacción se muestran al usuario. El modo real no tiene proveedor.

## Verificación comprobada

- `npm.cmd test -- --reporter=dot` (08/10/2026): **13 archivos, 83 pruebas aprobadas**. Incluye el contrato Blob/dimensiones, persistencia inyectada en el contexto de fotos, fallo de reemplazo conservando el preview y errores de mutaciones sin confirmar el estado local.
- `npm.cmd run build` (08/10/2026): **SUCCESS**, TypeScript y Vite.
- `git diff --check` (08/10/2026): SUCCESS, sin errores de whitespace. Git muestra avisos informativos LF/CRLF de los archivos locales modificados.

La suite Vitest usa jsdom y almacenamiento inyectado para el contexto de imágenes. Los seis escenarios nativos de navegador de la sección anterior cubren seed, fotos, operaciones, abort transaccional, concurrencia y fallo de apertura. No cubren cuota real del navegador, migraciones posteriores, `versionchange` ni recuperación tras cierre abrupto del proceso/energía; esos casos siguen pendientes.

Intento de validación manual en navegador (08/10/2026): la superficie de automatización falló antes de listar o abrir ventanas con el error exacto `tool call failed for cua_repl/js: Transport closed`. En ese intento no se creó perfil temporal ni se abrió el navegador habitual y no se ejecutaron escenarios. Este antecedente quedó superado por las pruebas Playwright 6/6 descritas arriba.

## Disponibilidad de automatización y guía manual

Comprobación del primer intento (08/10/2026): Playwright 1.64.0 y Chromium estaban instalados. El fallo CDP fue resuelto durante la ejecución posterior; la validación actual es 6/6 como se reporta arriba.

### Preparación segura

1. Conserva activo el servidor de revisión autorizado, si existe; no lo reinicies ni lo cierres. Abre Chrome o Edge con un perfil temporal nuevo, aislado del perfil habitual. Comprueba que el perfil temporal sea el activo antes de navegar.
2. Abre el origen local del Panel desde ese navegador. Todos los escenarios deben usar el mismo origen y perfil; no uses el perfil habitual ni borres los datos del sitio durante las pruebas.
3. Usa dos imágenes PNG/JPEG de prueba desechables guardadas fuera del repositorio. No selecciones ni alteres imágenes originales del proyecto.

### Escenarios de navegador

| N.º | Acciones concretas | Resultado esperado | Tipo |
|---|---|---|---|
| 1. Seed único | En el perfil temporal abre Dashboard y Alumnos; registra los totales iniciales de la demo. En un perfil ficticio, agrega un importe sintético y confirma. Recarga y revisa las colecciones. | El seed aparece una vez. El cambio de prueba aparece una sola vez y los perfiles demo permanecen; recargar no restaura el estado inicial ni duplica filas. | UI normal; sin inyección. |
| 2. Recarga/reapertura | Conserva el cambio sintético anterior. Carga una imagen de prueba desechable en el perfil ficticio. Recarga y verifica cuenta, movimiento e imagen. Reabre el mismo perfil temporal y origen. | El cambio de prueba, el movimiento único y la imagen aparecen tras recarga y reapertura del navegador. | UI normal; sin inyección. |
| 3. Reemplazar/quitar imagen | Reemplaza la imagen desechable; comprueba que otro perfil ficticio no la herede. Pulsa **Quitar imagen**, recarga y abre de nuevo el perfil. | La nueva imagen queda asociada solo al perfil ficticio seleccionado. Tras quitarla, no reaparece al recargar. | UI normal; sin inyección. |
| 4. Cuenta y movimiento atómicos | Anota el estado de cuenta e historial del perfil ficticio seleccionado. En DevTools Console instala el inyector de aborto que aparece abajo. Intenta una operación de importe sintético y confirma. Espera el error; restaura el método y recarga. | No aparece éxito; el formulario/error se conserva. El estado de cuenta y la cantidad de movimientos son exactamente los previos. Ningún store queda actualizado parcialmente. | Requiere inyección manual de aborto en una transacción readwrite; el uso normal no provoca el fallo. |
| 5. Doble confirmación | En el perfil ficticio seleccionado prepara un ajuste sintético y pulsa **Confirmar ajuste** dos veces rápidamente. Recarga y revisa el historial. | La operación de prueba ocurre como máximo una vez antes y después de recargar. Un conflicto en la segunda solicitud es admisible; no un segundo efecto monetario. | UI normal; repetición rápida, sin inyección. No retransmite un `operationId` idéntico. |
| 6. Error de almacenamiento | **Fallo de inicio:** en el perfil temporal, añade un override de `index.html` que simule IndexedDB no disponible y recarga. Después quita el override y recarga normalmente. **Fallo de escritura:** repite el aborto del escenario 4. | En fallo de inicio se muestra almacenamiento no disponible; no se monta el Panel ni aparece fallback en RAM. En fallo de escritura no hay éxito, se conserva el formulario y los datos no cambian. Al quitar la inyección y recargar, el Panel vuelve a abrir la base previa. | Ambos fallos requieren inyección manual; el fallo de escritura comprueba también la atomicidad del escenario 4. |

Inyector para los escenarios 4 y 6 (pégalo solo en DevTools del origen local y perfil temporal):

```js
(() => {
  const prototype = IDBObjectStore.prototype;
  const originalPut = prototype.put;
  let armed = true;
  prototype.put = function (...args) {
    if (armed && this.name === 'movements') {
      armed = false;
      this.transaction.abort();
      throw new DOMException('Fallo de transacción inyectado para PM.9B', 'AbortError');
    }
    return originalPut.apply(this, args);
  };
  window.restorePm9bPut = () => { prototype.put = originalPut; delete window.restorePm9bPut; };
  return 'Fallo de escritura armado para movements';
})();
```

Después, ejecuta `window.restorePm9bPut?.()` en la consola y recarga. Si el aborto no se dispara, no aparece el mensaje armado, los datos cambian parcialmente o no se puede retirar el override, registra el escenario como fallido. Esta guía describe pasos pendientes; no son resultados de prueba ejecutados.
## Pendientes

- En navegador: quedan pendientes los casos de cero/sin cuenta/saldo negativo, `versionchange`, cuota, fallo de guardado de imagen y observación explícita de revocación de object URLs.
- Validar recuperación tras cierre abrupto del proceso o energía; el escenario de aborto actual verifica atomicidad en una transacción viva, no recuperación de journal después de un crash.
- Añadir pruebas con una implementación de IndexedDB de prueba o una suite de navegador que ejercite el adaptador completo. Confirmar también la retención de reglas/versiones y referencias.
- `operationReceipts`, migraciones posteriores a esquema 1, checksum/hash de imágenes, respaldo/restauración PM.9C, PWA offline y todos los espacios reales siguen fuera de esta implementación.

Los cambios de esta fase no modifican firmware ni imágenes originales; los cambios locales DIAG.1 se conservaron. No se ejecutó Upload, Commit ni Push.
