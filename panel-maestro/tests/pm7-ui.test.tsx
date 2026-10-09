import { afterEach, describe, expect, it } from 'vitest'
import { cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react'
import { MemoryRouter, Route, Routes } from 'react-router-dom'
import { ClaimDetailPage } from '../src/pages/ClaimDetailPage'
import { ClaimsPage } from '../src/pages/ClaimsPage'
import { DemoPanelDataService } from '../src/services/DemoPanelDataService'
import type { ActivityClaim } from '../src/models/domain'
import type { PanelDataService } from '../src/services/PanelDataService'
import { AppRoutes } from '../src/app/App'

afterEach(cleanup)
const fixedNow = () => Date.parse('2026-10-07T12:00:00Z')

function copyService(base: DemoPanelDataService, getClaims: () => Promise<readonly ActivityClaim[]>): PanelDataService {
  return {
    getStudents: () => base.getStudents(), getAccounts: () => base.getAccounts(), getMovements: () => base.getMovements(),
    getActivities: () => base.getActivities(), getClaims,
  }
}

function renderCobros(path: string, service = new DemoPanelDataService(fixedNow)) {
  return { service, ...render(<MemoryRouter initialEntries={[path]}><Routes>
    <Route path="/cobros" element={<ClaimsPage service={service} />} />
    <Route path="/cobros/:claimId" element={<ClaimDetailPage service={service} now={fixedNow} />} />
  </Routes></MemoryRouter>) }
}

describe('PM.7 vistas de Cobros', () => {
  it('muestra filtros combinables y estados de coincidencias y detalle inexistente', async () => {
    renderCobros('/cobros')
    expect(await screen.findByText('3 de 3 cobros')).toBeInTheDocument()
    fireEvent.change(screen.getByLabelText('Alumno'), { target: { value: '202' } })
    fireEvent.change(screen.getByLabelText('Actividad'), { target: { value: '8102' } })
    fireEvent.change(screen.getByLabelText('Estado'), { target: { value: 'PAID' } })
    fireEvent.change(screen.getByLabelText(/Desde/), { target: { value: '2026-10-06' } })
    fireEvent.change(screen.getByLabelText(/Hasta/), { target: { value: '2026-10-06' } })
    expect(await screen.findByText('1 de 3 cobros')).toBeInTheDocument()
    expect(screen.getByRole('link', { name: 'Bruno Vega' })).toHaveAttribute('href', '/cobros/9102')
    fireEvent.click(screen.getByRole('button', { name: 'Limpiar filtros' }))
    expect(await screen.findByText('3 de 3 cobros')).toBeInTheDocument()
    cleanup()
    renderCobros('/cobros/99999')
    expect(await screen.findByRole('heading', { name: 'Cobro inexistente' })).toBeInTheDocument()
  })

  it('confirma una anulación con saldo negativo, motivo vacío y sin permitir repetirla', async () => {
    const { service } = renderCobros('/cobros/9102')
    await screen.findByRole('heading', { name: 'Bruno Vega' })
    fireEvent.click(screen.getByRole('button', { name: 'Anular cobro' }))
    expect(screen.getByText('El saldo quedará negativo; esta operación se permite en la demostración.')).toBeInTheDocument()
    expect(screen.getByText('-5 Áureos')).toBeInTheDocument()
    fireEvent.click(screen.getByRole('button', { name: 'Confirmar anulación' }))
    expect(await screen.findByText(/Anulación guardada localmente en modo demostración/)).toBeInTheDocument()
    expect(await screen.findByText('Sin motivo')).toBeInTheDocument()
    expect(screen.getAllByText(/Panel Maestro · modo demostración/).length).toBeGreaterThan(0)
    expect((await service.getAccounts()).find(({ student_id }) => student_id === 202)?.balance).toBe(-5)
    await waitFor(() => expect(screen.queryByRole('button', { name: 'Anular cobro' })).not.toBeInTheDocument())
  })

  it('autoriza por separado y simula solo una vez con la recompensa vigente', async () => {
    const { service } = renderCobros('/cobros/9103')
    await screen.findByRole('heading', { name: 'Ximena Sol' })
    fireEvent.click(screen.getByRole('button', { name: 'Anular cobro' }))
    fireEvent.click(screen.getByRole('button', { name: 'Confirmar anulación' }))
    await screen.findByRole('button', { name: 'Autorizar nuevo cobro' })
    fireEvent.click(screen.getByRole('button', { name: 'Autorizar nuevo cobro' }))
    fireEvent.click(screen.getByRole('button', { name: 'Confirmar autorización' }))
    expect(await screen.findByText(/Autorización de demostración registrada/)).toBeInTheDocument()
    expect((await service.getAccounts()).find(({ student_id }) => student_id === 201)?.balance).toBe(115)
    expect(await service.getMovements()).toHaveLength(11)
    fireEvent.click(await screen.findByRole('button', { name: 'Simular nuevo cobro' }))
    expect(screen.getByText(/El importe histórico anulado/)).toBeInTheDocument()
    fireEvent.click(screen.getByRole('button', { name: /Confirmar simulación/ }))
    expect(await screen.findByRole('heading', { name: 'Ximena Sol' })).toBeInTheDocument()
    expect(await screen.findByText(/Nuevo cobro de demostración registrado por 10 Áureos/)).toBeInTheDocument()
    expect(await screen.findByText('Cobro · ID 9104')).toBeInTheDocument()
    expect((await service.getAccounts()).find(({ student_id }) => student_id === 201)?.balance).toBe(125)
    expect(await service.getMovements()).toHaveLength(12)
  })

  it('separa una colección vacía de un error con opción de reintento', async () => {
    const base = new DemoPanelDataService(fixedNow)
    const empty = copyService(base, async () => [])
    renderCobros('/cobros', empty as DemoPanelDataService)
    expect(await screen.findByRole('heading', { name: 'Sin cobros registrados' })).toBeInTheDocument()
    cleanup()
    let fail = true
    const broken = copyService(base, async () => { if (fail) throw new Error('offline'); return base.getClaims() })
    renderCobros('/cobros', broken as unknown as DemoPanelDataService)
    expect(await screen.findByRole('alert')).toHaveTextContent('No se pudieron consultar los cobros')
    fail = false
    fireEvent.click(screen.getByRole('button', { name: 'Reintentar' }))
    await waitFor(() => expect(screen.getByText('3 de 3 cobros')).toBeInTheDocument())
  })

  it('mantiene activo el módulo Cobros al abrir un detalle por ID', async () => {
    render(<MemoryRouter initialEntries={['/cobros/9103']}><AppRoutes /></MemoryRouter>)
    await screen.findByText('Movimiento original')
    const link = screen.getByRole('navigation', { name: 'Navegación principal' }).querySelector('a[href="/cobros"]')
    expect(link).toHaveClass('active')
    expect(screen.getByText('Cobros', { selector: '.topbar-context strong' })).toBeInTheDocument()
  })
})
