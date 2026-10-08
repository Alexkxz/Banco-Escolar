import { afterEach, describe, expect, it, vi } from 'vitest'
import { cleanup, render, screen, within } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { MemoryRouter, Route, Routes } from 'react-router-dom'
import { ActivityFormPage } from '../src/pages/ActivityFormPage'
import { ActivityDetailPage } from '../src/pages/ActivityDetailPage'
import { DemoPanelDataService } from '../src/services/DemoPanelDataService'

afterEach(() => { cleanup(); vi.restoreAllMocks() })

describe('PM.6 interfaz de actividades', () => {
  it('mantiene los controles del formulario accesibles mediante tabulación', async () => {
    const user = userEvent.setup()
    render(<MemoryRouter initialEntries={['/actividades/nueva']}><Routes><Route path="/actividades/nueva" element={<ActivityFormPage mode="create" />} /></Routes></MemoryRouter>)
    await screen.findByLabelText('Asignar número')
    await user.tab()
    expect(screen.getByRole('link', { name: 'Regresar a actividades' })).toHaveFocus()
    await user.tab()
    expect(screen.getByLabelText('Asignar número')).toHaveFocus()
    await user.tab()
    expect(screen.getByLabelText('Usar duración con tiempo')).toHaveFocus()
  })

  it('aplica Elegir todos, Limpiar e Invertir, y actualiza el contador y el resumen', async () => {
    const user = userEvent.setup()
    render(<MemoryRouter initialEntries={['/actividades/nueva']}><Routes><Route path="/actividades/nueva" element={<ActivityFormPage mode="create" />} /></Routes></MemoryRouter>)
    await user.click(await screen.findByLabelText('Seleccionar alumnos'))
    const status = screen.getByRole('status')
    const summary = screen.getByText('Resumen').parentElement as HTMLElement

    await user.click(screen.getByRole('button', { name: 'Elegir todos' }))
    expect(status).toHaveTextContent('6 de 6 alumnos seleccionados')
    expect(within(summary).getByText('6 de 6 alumnos seleccionados')).toBeInTheDocument()
    expect(screen.getByLabelText(/Ximena Sol/)).toBeChecked()

    await user.click(screen.getByRole('button', { name: 'Limpiar' }))
    expect(status).toHaveTextContent('0 de 6 alumnos seleccionados')
    expect(within(summary).getByText('0 de 6 alumnos seleccionados')).toBeInTheDocument()
    expect(screen.getByLabelText(/Ximena Sol/)).not.toBeChecked()

    await user.click(screen.getByRole('button', { name: 'Invertir' }))
    expect(status).toHaveTextContent('6 de 6 alumnos seleccionados')
    expect(screen.getByLabelText(/Teo Nube/)).toBeChecked()
    await user.click(screen.getByRole('button', { name: 'Invertir' }))
    expect(status).toHaveTextContent('0 de 6 alumnos seleccionados')
  })

  it('bloquea una selección vacía y conserva la selección manual al cambiar de modalidad', async () => {
    const user = userEvent.setup()
    render(<MemoryRouter initialEntries={['/actividades/nueva']}><Routes><Route path="/actividades/nueva" element={<ActivityFormPage mode="create" />} /></Routes></MemoryRouter>)
    await user.click(await screen.findByLabelText('Seleccionar alumnos'))
    await user.click(screen.getByRole('button', { name: 'Revisar y confirmar inicio' }))
    expect(screen.getByRole('alert')).toHaveTextContent('Selecciona uno o más alumnos registrados')
    expect(screen.queryByRole('region', { name: 'Confirmación de actividad' })).not.toBeInTheDocument()

    await user.click(screen.getByLabelText(/Ximena Sol/))
    await user.click(screen.getByLabelText('Todos los alumnos registrados al iniciar'))
    expect(screen.queryByRole('checkbox', { name: /Ximena Sol/ })).not.toBeInTheDocument()
    await user.click(screen.getByLabelText('Sin filtro previo de participantes'))
    await user.click(screen.getByLabelText('Seleccionar alumnos'))
    expect(screen.getByLabelText(/Ximena Sol/)).toBeChecked()
    expect(screen.getByRole('status')).toHaveTextContent('1 de 6 alumnos seleccionados')
  })

  it('conserva el borrador cuando falla crear la actividad', async () => {
    const user = userEvent.setup()
    const service = Object.assign(new DemoPanelDataService(), { createActivity: async () => { throw new Error('Fallo de demostración') } })
    render(<MemoryRouter initialEntries={['/actividades/nueva']}><Routes><Route path="/actividades/nueva" element={<ActivityFormPage mode="create" service={service} />} /></Routes></MemoryRouter>)
    const reward = await screen.findByLabelText(/Recompensa por alumno/)
    await user.clear(reward)
    await user.type(reward, '37')
    await user.click(screen.getByRole('button', { name: 'Revisar y confirmar inicio' }))
    await user.click(screen.getByRole('button', { name: 'Confirmar e iniciar actividad' }))
    expect(await screen.findByRole('alert')).toHaveTextContent('Fallo de demostración')
    expect(reward).toHaveValue(37)
  })

  it('libera el temporizador del detalle al salir de la vista', async () => {
    const clear = vi.spyOn(window, 'clearInterval')
    const view = render(<MemoryRouter initialEntries={['/actividades/8104']}><Routes><Route path="/actividades/:activityId" element={<ActivityDetailPage />} /></Routes></MemoryRouter>)
    expect(await screen.findByRole('heading', { name: 'Actividad 4' })).toBeInTheDocument()
    view.unmount()
    expect(clear).toHaveBeenCalled()
  })
})
