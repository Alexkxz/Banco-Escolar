import { afterEach, describe, expect, it } from 'vitest'
import { cleanup, render, screen, within } from '@testing-library/react'
import { MemoryRouter } from 'react-router-dom'
import { AppRoutes } from '../src/app/App'

afterEach(cleanup)
function renderAt(path: string) {
  return render(<MemoryRouter initialEntries={[path]}><AppRoutes /></MemoryRouter>)
}

describe('PM.5 vistas de consulta', () => {
  it('lista las cinco cuentas directas y al alumno sin cuenta sin saldo cero', async () => {
    renderAt('/cuentas')
    expect(await screen.findByText('6 de 6 alumnos')).toBeInTheDocument()
    expect(screen.getAllByRole('link', { name: 'Ver cuenta' })).toHaveLength(5)
    const teoRow = screen.getByText('Teo Nube').closest('.account-row') as HTMLElement
    expect(within(teoRow).getByRole('link', { name: 'Ver alumno' })).toHaveAttribute('href', '/alumnos/206')
    expect(within(teoRow).getByText('Sin cuenta')).toBeInTheDocument()
    expect(within(teoRow).queryByRole('link', { name: /cuenta/i })).not.toBeInTheDocument()
    expect(screen.queryByText('0 Áureos')).not.toBeInTheDocument()
    const ximenaRow = screen.getByText('Ximena Sol').closest('.account-row') as HTMLElement
    expect(within(ximenaRow).getByRole('link', { name: 'Ximena Sol' })).toHaveAttribute('href', '/cuentas/201')
    expect(within(ximenaRow).getByRole('link', { name: 'Ver cuenta' })).toHaveAttribute('href', '/cuentas/201')
    expect(within(ximenaRow).getByRole('link', { name: 'Ver alumno' })).toHaveAttribute('href', '/alumnos/201')
  })

  it('muestra detalle de cuenta con saldo directo, totales acotados e historial reciente', async () => {
    renderAt('/cuentas/201')
    expect(await screen.findByRole('link', { name: 'Ximena Sol' })).toHaveAttribute('href', '/alumnos/201')
    expect(screen.getByText(/125/)).toBeInTheDocument()
    const totals = screen.getByText(/Entradas disponibles/).closest('.history-totals') as HTMLElement
    expect(within(totals).getByText('145 Áureos')).toBeInTheDocument()
    expect(within(totals).getByText('20 Áureos')).toBeInTheDocument()
    expect(screen.getByText(/no necesariamente es todo el historial/)).toBeInTheDocument()
    const list = screen.getByRole('list', { name: 'Movimientos de la cuenta' })
    expect(within(list).getAllByRole('listitem')[0]).toHaveTextContent('Material de aula')
  })

  it('lista movimientos con vínculos reales a actividad y cobro y fecha con zona horaria', async () => {
    renderAt('/movimientos')
    expect(await screen.findByText('10 de 10 movimientos')).toBeInTheDocument()
    expect(screen.getByText(/Fechas mostradas en America\/Mexico_City/)).toBeInTheDocument()
    expect(screen.getAllByRole('link', { name: 'Actividad 8102' })).toHaveLength(2)
    expect(screen.getAllByRole('link', { name: 'Actividad 8102' })[0]).toHaveAttribute('href', '/actividades/8102')
    expect(screen.getByRole('link', { name: 'Cobro 9101' })).toHaveAttribute('href', '/cobros/9101')
    expect(screen.getByRole('link', { name: 'Material de aula' })).toHaveAttribute('href', '/movimientos/7109')
  })

  it('muestra detalle de movimiento y mantiene el estado de registro inexistente', async () => {
    renderAt('/movimientos/7106')
    expect(await screen.findByRole('heading', { name: 'Actividad 2 · lectura' })).toBeInTheDocument()
    expect(screen.getByRole('link', { name: /Cobro 9101/ })).toHaveAttribute('href', '/cobros/9101')
    cleanup()
    renderAt('/movimientos/9999')
    expect(await screen.findByRole('heading', { name: 'Movimiento inexistente' })).toBeInTheDocument()
  })
})
