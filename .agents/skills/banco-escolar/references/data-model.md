# Modelo de datos y ejemplos provisionales

La fuente primaria es `src/student_model.h`, `src/academic_config.h` y `src/data/provisional_data.*`. No uses esta referencia para reemplazar los datos del código si cambia el proyecto.

## Tipos

- `Student`: `student_id`, `nfc_uid`, `name`, `preferred_name`, grado, grupo, número de lista, referencia de avatar y nivel temporal. El modelo no incorpora saldo dentro de `Student`.
- `AccountRecord`: `student_id` y saldo entero en Áureos.
- `ReadingRecord`: alumno, fecha, PPM y `applied`.
- `WritingRecord`: alumno, fecha, palabras, errores y `applied`.
- `StudentMovement`: alumno, cantidad firmada, tipo, motivo, fecha/hora y origen.
- `AttendanceRecord`: tipo preparado; no implica registros reales.

## Registros demo comprobados

Hay 13 alumnos provisionales: seis de 3.º y siete de 4.º; identificadores 1–13. Todos tienen `nfc_uid` sin asignar. Darío es `student_id = 7`, nombre `CAMACHO ARREOLA ALONZO DARIO`, presentación `Darío`. No inventes UID.

Hay 39 lecturas (13 × 3) y 52 dictados (13 × 4), más una cuenta y tres movimientos demo para Darío. Su lectura: 01/09/2026 82 PPM, 07/09/2026 83 PPM, 11/09/2026 107 PPM. Dictados: 01/09 34 palabras/8 errores; 07/09 45/5; 11/09 no aplicado; 21/09 58/7.

`applied = false` no significa cero errores ni genera asistencia. Cero errores solo es una evaluación aplicada sin errores.

## Umbrales de Fluidez lectora

Estos valores coinciden con `src/academic_config.h`:

| Grado | Apoyo | Cerca del estándar | Estándar | Avanzado |
|---|---:|---:|---:|---:|
| 1.º | <15 | 15–34 | 35–59 | ≥60 |
| 2.º | <35 | 35–59 | 60–84 | ≥85 |
| 3.º | <60 | 60–84 | 85–99 | ≥100 |
| 4.º | <85 | 85–99 | 100–114 | ≥115 |
| 5.º | <100 | 100–114 | 115–124 | ≥125 |
| 6.º | <115 | 115–124 | 125–134 | ≥135 |

Darío está en 4.º; con 107 PPM se clasifica Estándar, meta Avanzado 115 y diferencia 8 PPM. `Student.level` es temporal y no se deriva del saldo.

## Dinero y demo de transferencia

La moneda se configura en `src/app_config.h` con `CURRENCY_NAME`, hoy Áureos. La UI de transferencia Darío → Fernanda es demo, con controles +5, +10, -5, -10 y cantidad libre, y validación de saldo/rango; no representa movimientos reales persistidos.
