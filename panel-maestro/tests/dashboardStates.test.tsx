import { afterEach, describe, expect, it } from 'vitest'
import { cleanup, fireEvent, render, screen } from '@testing-library/react'
import { MemoryRouter } from 'react-router-dom'
import type { ActivityClaim, ActivitySession, MovementRecord, Student, StudentAccount } from '../src/models/domain'
import type { PanelDataService } from '../src/services/PanelDataService'
import { DevelopmentHome } from '../src/pages/DevelopmentHome'

afterEach(cleanup)

const emptyService: PanelDataService = {
  async getStudents(): Promise<readonly Student[]> { return [] },
  async getAccounts(): Promise<readonly StudentAccount[]> { return [] },
  async getMovements(): Promise<readonly MovementRecord[]> { return [] },
  async getActivities(): Promise<readonly ActivitySession[]> { return [] },
  async getClaims(): Promise<readonly ActivityClaim[]> { return [] },
}

function renderDashboard(dataService: PanelDataService) {
  return render(<MemoryRouter><DevelopmentHome dataService={dataService} /></MemoryRouter>)
}

describe('Dashboard query states', () => {
  it('shows loading instead of presenting an unresolved query as empty', () => {
    const pendingService: PanelDataService = {
      ...emptyService,
      getStudents: () => new Promise<readonly Student[]>(() => undefined),
    }
    renderDashboard(pendingService)

    expect(screen.getByRole('status')).toHaveTextContent('Consultando el conjunto local de demostración')
    expect(screen.queryByText('No hay movimientos de demostración')).not.toBeInTheDocument()
  })

  it('shows zero counts and a distinct empty state for a successful empty collection', async () => {
    renderDashboard(emptyService)

    expect(await screen.findByTestId('metric-students')).toHaveTextContent('0')
    expect(screen.getByTestId('metric-aureos')).toHaveTextContent('0')
    expect(screen.getByTestId('metric-active-activities')).toHaveTextContent('0')
    expect(screen.getByTestId('metric-movements')).toHaveTextContent('0')
    expect(screen.getByText('No hay movimientos de demostración')).toBeInTheDocument()
    expect(screen.queryByRole('alert')).not.toBeInTheDocument()
  })

  it('shows query errors separately and can retry to a valid empty result', async () => {
    let shouldFail = true
    const recoveringService: PanelDataService = {
      ...emptyService,
      async getMovements() {
        if (shouldFail) throw new Error('demo query failed')
        return []
      },
    }
    renderDashboard(recoveringService)

    expect(await screen.findByRole('alert')).toHaveTextContent('No se pudieron cargar los datos de demostración')
    expect(screen.queryByText('No hay movimientos de demostración')).not.toBeInTheDocument()
    shouldFail = false
    fireEvent.click(screen.getByRole('button', { name: 'Reintentar consulta' }))
    expect(await screen.findByText('No hay movimientos de demostración')).toBeInTheDocument()
    expect(screen.queryByRole('alert')).not.toBeInTheDocument()
  })
})
