import { cleanup, fireEvent, render, screen, within } from '@testing-library/react'
import { afterEach, describe, expect, it } from 'vitest'
import { MemoryRouter, Route, Routes } from 'react-router-dom'
import { AcademicProgressPage } from '../src/pages/AcademicProgressPage'
import { AttendancePage } from '../src/pages/AttendancePage'
import { SchoolConfigurationPage } from '../src/pages/SchoolConfigurationPage'
import { DemoPanelDataService } from '../src/services/DemoPanelDataService'

afterEach(cleanup)
function at(path: string, element: React.ReactNode) { return render(<MemoryRouter initialEntries={[path]}><Routes><Route path={path} element={element} /></Routes></MemoryRouter>) }

describe('PM.8 interfaz de progreso, asistencia y reglas', () => {
  it('captura dictado, calcula porcentaje, conserva vacío como Sin evaluar y no mueve dinero al guardar', async () => {
    const service = new DemoPanelDataService()
    at('/progreso', <AcademicProgressPage service={service} />)
    await screen.findByRole('tab', { name: 'Resumen' })
    fireEvent.click(screen.getByText(/Capturar o corregir/))
    fireEvent.change(screen.getByLabelText('Evaluación'), { target: { value: 'DICTATION' } })
    fireEvent.change(screen.getByLabelText('Total de palabras'), { target: { value: '10' } })
    fireEvent.change(screen.getByLabelText('Palabras correctas'), { target: { value: '8' } })
    expect(screen.getByText(/80%/)).toBeInTheDocument()
    fireEvent.click(await screen.findByRole('button', { name: 'Guardar evaluación' }))
    expect(await screen.findByRole('status')).toHaveTextContent('Registro guardado')
    expect((await service.getAcademicRecords())[0]).toMatchObject({ kind: 'DICTATION', total_words: 10, correct_words: 8, version: 1 })
    expect((await service.getMovements()).length).toBe(10)

    fireEvent.change(await screen.findByLabelText('Evaluación'), { target: { value: 'COMPREHENSION' } })
    fireEvent.click(screen.getByRole('button', { name: 'Guardar evaluación' }))
    expect((await screen.findAllByText(/Sin evaluar/)).length).toBeGreaterThan(0)
    expect((await service.getAcademicRecords()).some((record) => record.kind === 'COMPREHENSION' && record.scores.LITERAL === null)).toBe(true)
  })

  it('guarda una falta de forma explícita, ofrece confirmación y actualiza saldo/movimientos', async () => {
    const service = new DemoPanelDataService()
    at('/asistencia', <AttendancePage service={service} />)
    await screen.findByRole('heading', { name: 'Lista del día escolar' })
    const row = document.querySelector('.attendance-row') as HTMLElement
    fireEvent.change(within(row).getByLabelText('Estado'), { target: { value: 'ABSENT_UNJUSTIFIED' } })
    fireEvent.click(within(row).getByRole('button', { name: 'Guardar estado' }))
    expect(await screen.findByRole('status')).toHaveTextContent('actualizada')
    expect(await service.getAttendanceRecords()).toMatchObject([{ student_id: 201, status: 'ABSENT_UNJUSTIFIED' }])
    const apply = await screen.findByRole('button', { name: 'Aplicar Áureos' })
    fireEvent.click(apply)
    const review = await screen.findByRole('region', { name: 'Revisión de aplicación de Áureos' })
    expect(review).toHaveTextContent('-20 Áureos')
    fireEvent.click(within(review).getByRole('button', { name: 'Confirmar aplicación' }))
    expect(await screen.findByText('Aplicación registrada.')).toBeInTheDocument()
    expect((await service.getAccounts()).find(({ student_id }) => student_id === 201)?.balance).toBe(105)
    expect((await service.getMovements()).at(-1)).toMatchObject({ amount: -20, type: 'EXIT', origin: 'DEMO' })
  })

  it('presenta rangos iniciales y mantiene pesos, resultados generales e importes sin configurar', async () => {
    const service = new DemoPanelDataService()
    at('/configuracion', <SchoolConfigurationPage service={service} />)
    await screen.findByRole('heading', { name: 'Lectura · PPM por grado' })
    expect(screen.getByText(/1° 15\/35\/60/)).toBeInTheDocument()
    expect(screen.getAllByPlaceholderText('Sin configurar').length).toBeGreaterThan(5)
    expect((await service.getSchoolRules()).academic.comprehension.weights).toBeNull()
    expect((await service.getSchoolRules()).academic.readingAureos.REQUIRES_SUPPORT).toBeNull()
  })

  it('mantiene filtros entre subpestañas y alinea barras, etiquetas y tabla accesible', async () => {
    const service = new DemoPanelDataService()
    await service.saveAcademicRecord({ student_id: 201, school_date: '2026-10-01', kind: 'READING', ppm: 80 })
    await service.saveAcademicRecord({ student_id: 202, school_date: '2026-10-02', kind: 'READING', ppm: 90 })
    at('/progreso', <AcademicProgressPage service={service} />)
    await screen.findByRole('tab', { name: 'Resumen' })
    fireEvent.click(screen.getByRole('tab', { name: 'Fluidez lectora' }))
    fireEvent.change(screen.getByLabelText('Grado'), { target: { value: '3' } })
    fireEvent.change(screen.getByLabelText('Desde'), { target: { value: '2026-10-02' } })
    const chart = await screen.findByRole('heading', { name: 'Fluidez lectora · 3° grado' })
    const graphRow = Array.from(chart.closest('.academic-chart-card')?.querySelectorAll('.academic-chart-row') ?? []).find((row) => row.textContent?.includes('Est'))
    expect(graphRow).toHaveTextContent(/Est/)
    expect(graphRow).toHaveTextContent('1')
    const chartCard = chart.closest('.academic-chart-card') as HTMLElement
    fireEvent.click(within(chartCard).getByText(/Ver datos/))
    const accessibleRow = within(chartCard).getAllByRole('row').find((row) => row.textContent?.includes('Est') && row.textContent?.includes('1')) as HTMLElement
    expect(accessibleRow).toHaveTextContent('1')
    fireEvent.click(screen.getByRole('tab', { name: 'Dictado' }))
    expect(screen.getByLabelText('Grado')).toHaveValue('3')
    expect(screen.getByLabelText('Desde')).toHaveValue('2026-10-02')
    fireEvent.change(screen.getByLabelText('Vista'), { target: { value: 'student' } })
    fireEvent.change(screen.getAllByLabelText('Alumno')[0], { target: { value: '202' } })
    fireEvent.click(screen.getByRole('tab', { name: 'Comprensión' }))
    expect(screen.getAllByLabelText('Alumno')[0]).toHaveValue('202')
  })

  it('presenta pendientes sin dibujar distribuciones vac?as y abre la captura del indicador de la subpesta?a', async () => {
    const service = new DemoPanelDataService()
    at('/progreso', <AcademicProgressPage service={service} />)
    await screen.findByRole('tab', { name: 'Resumen' })
    expect(screen.getAllByText('6 pendientes')).toHaveLength(3)
    expect(screen.getByText(/hay evaluaciones en este/)).toBeInTheDocument()
    expect(document.querySelectorAll('.academic-chart-card')).toHaveLength(0)
    fireEvent.click(screen.getByRole('tab', { name: 'Dictado' }))
    fireEvent.click(screen.getAllByRole('button', { name: /Registrar/ })[0])
    expect(await screen.findByLabelText('Evaluación')).toHaveValue('DICTATION')
    fireEvent.change(screen.getByLabelText('Total de palabras'), { target: { value: '10' } })
    fireEvent.click(screen.getByText(/Capturar o corregir/))
    fireEvent.click(screen.getByRole('tab', { name: /Comprensi/ }))
    fireEvent.click(screen.getAllByRole('button', { name: /Registrar/ })[0])
    expect(await screen.findByLabelText('Evaluación')).toHaveValue('COMPREHENSION')
    fireEvent.change(screen.getByLabelText('Grado'), { target: { value: '3' } })
    fireEvent.click(screen.getByRole('tab', { name: 'Resumen' }))
    expect(screen.getByLabelText('Grado')).toHaveValue('3')
  })

  it('muestra porcentaje con escala fija, cero real y cobertura parcial de comprensi?n', async () => {
    const service = new DemoPanelDataService()
    await service.saveAcademicRecord({ student_id: 201, school_date: '2026-10-01', kind: 'DICTATION', total_words: 10, correct_words: 0 })
    await service.saveAcademicRecord({ student_id: 201, school_date: '2026-10-02', kind: 'COMPREHENSION', scores: { LITERAL: 0, INFERENTIAL: null, CRITICAL: 2 } })
    at('/progreso', <AcademicProgressPage service={service} />)
    await screen.findByRole('tab', { name: 'Resumen' })
    fireEvent.click(screen.getByRole('tab', { name: 'Dictado' }))
    expect(await screen.findByRole('heading', { name: 'Porcentaje de palabras correctas' })).toBeInTheDocument()
    expect(document.querySelector('.academic-chart-bar-line strong')).toHaveTextContent('0%')
    fireEvent.click(screen.getByRole('tab', { name: /Comprensi/ }))
    expect(await screen.findByText(/ponderación general sigue sin configurar/i)).toBeInTheDocument()
    expect(screen.getAllByText(/Literal/).length).toBeGreaterThan(0)
    expect(screen.getAllByText(/Sin evaluar/).length).toBeGreaterThan(0)
    const comprehensionChart = screen.getByRole('heading', { name: /niveles por aspecto/ }).closest('.academic-chart-card') as HTMLElement
    expect(Array.from(comprehensionChart.querySelectorAll('.academic-chart-row')).some((row) => row.textContent?.includes('Inferencial'))).toBe(false)
  })


  it('permite recorrer subpesta?as con teclado y mantiene el foco en la pesta?a activada', async () => {
    at('/progreso', <AcademicProgressPage service={new DemoPanelDataService()} />)
    const summary = await screen.findByRole('tab', { name: /Resumen/ })
    summary.focus()
    fireEvent.keyDown(summary, { key: 'ArrowRight' })
    const reading = screen.getByRole('tab', { name: /Fluidez lectora/ })
    expect(reading).toHaveAttribute('aria-selected', 'true')
    expect(document.activeElement).toBe(reading)
    fireEvent.keyDown(reading, { key: 'End' })
    const comprehension = screen.getByRole('tab', { name: /Comprensi/ })
    expect(comprehension).toHaveAttribute('aria-selected', 'true')
    expect(document.activeElement).toBe(comprehension)
  })

})
