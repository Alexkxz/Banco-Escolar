import { describe, expect, it } from 'vitest'
import { DemoPanelDataService } from '../src/services/DemoPanelDataService'
import { createDashboardSnapshot, loadDashboardSnapshot } from '../src/services/dashboardSnapshot'

describe('Dashboard demo snapshot', () => {
  it('calculates counts from each collection and sums direct account balances', async () => {
    const service = new DemoPanelDataService()
    const [students, accounts, activities, movements] = await Promise.all([
      service.getStudents(), service.getAccounts(), service.getActivities(), service.getMovements(),
    ])
    const snapshot = await loadDashboardSnapshot(service)

    expect(snapshot).toMatchObject({ studentCount: 6, accountCount: 5, accountAureos: 260, activeActivityCount: 2, movementCount: 10 })
    expect(snapshot.studentsWithoutAccount.map((student) => student.student_id)).toEqual([206])
    expect(snapshot.recentMovements.map((movement) => movement.id)).toEqual([7110, 7109, 7108, 7107, 7106])

    for (const account of accounts) {
      const movementNet = movements.filter((movement) => movement.student_id === account.student_id).reduce((sum, movement) => sum + movement.amount, 0)
      expect(movementNet).toBe(account.balance)
    }

    const changedBalances = accounts.map((account) => ({ ...account, balance: account.balance + 1 }))
    const adjusted = createDashboardSnapshot({ students, accounts: changedBalances, activities, movements })
    expect(adjusted.accountAureos).toBe(265)
    expect(adjusted.movementCount).toBe(10)
  })

  it('keeps elapsed active activity status separate from its duration', async () => {
    const activities = await new DemoPanelDataService().getActivities()
    const fixedNow = Date.parse('2026-10-07T12:00:00Z') / 1000
    const elapsedButActive = activities.find((activity) => activity.id === 8101)!

    expect(elapsedButActive.started_at + elapsedButActive.duration_seconds).toBeLessThan(fixedNow)
    expect(elapsedButActive.status).toBe('ACTIVE')
    expect(activities.filter((activity) => activity.status === 'ACTIVE')).toHaveLength(2)
    const claims = await new DemoPanelDataService().getClaims()
    expect(claims.some((claim) => claim.activity_id === elapsedButActive.id)).toBe(false)
  })
})
