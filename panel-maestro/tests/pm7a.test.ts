import { describe, expect, it } from 'vitest'
import { DemoPanelDataService, MAX_DEMO_ACCOUNT_BALANCE, MAX_DEMO_MOVEMENT_AMOUNT, type DemoMutationName } from '../src/services/DemoPanelDataService'
import type { StudentAccount } from '../src/models/domain'
import { loadAccountDetail } from '../src/services/accountQueries'
import { loadDashboardSnapshot } from '../src/services/dashboardSnapshot'
import { loadMovementSnapshot } from '../src/services/movementQueries'

const now = Date.parse('2026-10-07T18:00:00Z')

describe('PM.7A ajustes manuales demo de cuenta', () => {
  it('registra entrada, retiro, fecha y motivo en la zona de presentación acordada', async () => {
    const service = new DemoPanelDataService(() => now)
    const add = await service.adjustDemoAccount({ studentId: 201, operation: 'ADD', amount: 25, reason: '  Reconocimiento  ', expectedBalance: 125, confirmationId: 'add-1' })
    expect(add).toMatchObject({ account: { balance: 150 }, movement: { amount: 25, type: 'ENTRY', origin: 'DEMO', reason: 'Ajuste manual de demostración · Agregar Áureos · Reconocimiento', timestamp: now / 1000 } })
    const withdraw = await service.adjustDemoAccount({ studentId: 201, operation: 'WITHDRAW', amount: 150, reason: '', expectedBalance: 150, confirmationId: 'withdraw-1' })
    expect(withdraw).toMatchObject({ account: { balance: 0 }, movement: { amount: -150, type: 'EXIT', reason: 'Ajuste manual de demostración · Retirar Áureos · Sin motivo' } })
    const negative = await service.adjustDemoAccount({ studentId: 201, operation: 'WITHDRAW', amount: 1, reason: '', expectedBalance: 0, confirmationId: 'withdraw-2' })
    expect(negative.account.balance).toBe(-1)
    expect((await service.getMovements()).slice(-3).map(({ id }) => id)).toEqual([7111, 7112, 7113])
  })

  it('revalida saldo y confirmación, y no modifica parcialmente ante error', async () => {
    let fail: DemoMutationName | '' = ''
    const service = new DemoPanelDataService(() => now, (operation) => { if (operation === fail) throw new Error('fallo') })
    await expect(service.adjustDemoAccount({ studentId: 201, operation: 'ADD', amount: 1, reason: '', expectedBalance: 124, confirmationId: 'stale' })).rejects.toMatchObject({ code: 'BALANCE_CHANGED', currentAccount: { balance: 125 } })
    const beforeAccounts = await service.getAccounts(); const beforeMovements = await service.getMovements(); const beforeClaims = await service.getClaims(); const beforeAuth = await service.getClaimAuthorizations()
    fail = 'MANUAL_ACCOUNT_ADJUSTMENT'
    const request = { studentId: 201, operation: 'ADD' as const, amount: 4, reason: '', expectedBalance: 125, confirmationId: 'retry-id' }
    await expect(service.adjustDemoAccount(request)).rejects.toMatchObject({ code: 'OPERATION_FAILED' })
    expect(await service.getAccounts()).toEqual(beforeAccounts); expect(await service.getMovements()).toEqual(beforeMovements)
    expect(await service.getClaims()).toEqual(beforeClaims); expect(await service.getClaimAuthorizations()).toEqual(beforeAuth)
    fail = ''
    const applied = await service.adjustDemoAccount(request)
    expect(applied.account.balance).toBe(129)
    await expect(service.adjustDemoAccount(request)).rejects.toMatchObject({ code: 'DUPLICATE_CONFIRMATION' })
    expect(await service.getMovements()).toHaveLength(beforeMovements.length + 1)
  })

  it('rechaza cantidades inválidas y alumno sin cuenta sin cambiar los datos', async () => {
    const service = new DemoPanelDataService(() => now)
    const before = await service.getMovements()
    for (const amount of [0, -1, 1.5, MAX_DEMO_MOVEMENT_AMOUNT + 1, Number.NaN]) {
      await expect(service.adjustDemoAccount({ studentId: 201, operation: 'ADD', amount, reason: '', expectedBalance: 125, confirmationId: `invalid-${amount}` })).rejects.toMatchObject({ code: 'INVALID_AMOUNT' })
    }
    await expect(service.adjustDemoAccount({ studentId: 206, operation: 'ADD', amount: 1, reason: '', expectedBalance: 0, confirmationId: 'no-account' })).rejects.toMatchObject({ code: 'NOT_FOUND' })
    expect(await service.getAccounts()).toHaveLength(5); expect(await service.getMovements()).toEqual(before)
  })

  it('rechaza un saldo resultante fuera del límite de cuenta', async () => {
    const service = new DemoPanelDataService(() => now)
    ;(service as unknown as { accounts: StudentAccount[] }).accounts[0].balance = MAX_DEMO_ACCOUNT_BALANCE
    const beforeMovements = await service.getMovements()
    await expect(service.adjustDemoAccount({ studentId: 201, operation: 'ADD', amount: 1, reason: '', expectedBalance: MAX_DEMO_ACCOUNT_BALANCE, confirmationId: 'limit' })).rejects.toMatchObject({ code: 'INVALID_BALANCE' })
    expect((await service.getAccounts())[0].balance).toBe(MAX_DEMO_ACCOUNT_BALANCE)
    expect(await service.getMovements()).toEqual(beforeMovements)
  })

  it('refleja los cambios en el detalle, historial y Dashboard usando el saldo directo', async () => {
    const service = new DemoPanelDataService(() => now)
    await service.adjustDemoAccount({ studentId: 201, operation: 'ADD', amount: 5, reason: '', expectedBalance: 125, confirmationId: 'coherence' })
    expect((await loadAccountDetail(service, 201))?.account.balance).toBe(130)
    const movements = await loadMovementSnapshot(service)
    expect(movements.entries[0]).toMatchObject({ movement: { amount: 5, type: 'ENTRY', origin: 'DEMO' }, student: { student_id: 201 } })
    const dashboard = await loadDashboardSnapshot(service)
    expect(dashboard.accountAureos).toBe(265)
    expect(dashboard.movementCount).toBe(11)
  })
})
