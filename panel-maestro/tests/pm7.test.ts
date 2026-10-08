import { describe, expect, it } from 'vitest'
import type { ActivityClaim, ActivitySession, MovementRecord } from '../src/models/domain'
import { DemoPanelDataService, type DemoMutationName } from '../src/services/DemoPanelDataService'
import { filterClaims, loadClaimDetail, loadClaimSnapshot } from '../src/services/claimQueries'
import { loadDashboardSnapshot } from '../src/services/dashboardSnapshot'
import { loadAccountDetail } from '../src/services/accountQueries'
import { loadActivityDetail } from '../src/services/activityQueries'
import { loadMovementSnapshot } from '../src/services/movementQueries'

const at = (value: string) => Date.parse(value)

describe('PM.7 cobros y operaciones de demostración', () => {
  it('filtra por alumno, actividad, estado y días completos de America/Mexico_City', async () => {
    const service = new DemoPanelDataService()
    const snapshot = await loadClaimSnapshot(service)
    expect(snapshot.entries.map(({ claim }) => claim.id)).toEqual([9103, 9102, 9101])
    expect(filterClaims(snapshot.entries, { studentId: '202', activityId: '8102', status: 'PAID', from: '2026-10-06', through: '2026-10-06' }).map(({ claim }) => claim.id)).toEqual([9102])
    expect(filterClaims(snapshot.entries, { studentId: '', activityId: '', status: 'all', from: '2026-10-06', through: '2026-10-06' }).map(({ claim }) => claim.id)).toEqual([9102, 9101])
    expect(() => filterClaims(snapshot.entries, { studentId: '', activityId: '', status: 'all', from: '2026-10-07', through: '2026-10-06' })).toThrow('anterior o igual')
    const detail = await loadClaimDetail(service, 9103)
    expect(detail).toMatchObject({ consistent: true, student: { student_id: 201 }, activity: { id: 8104 }, account: { student_id: 201 }, movement: { id: 7108 } })
  })

  it('anula el importe original, acepta motivo vacío o texto, y bloquea una segunda anulación', async () => {
    const service = new DemoPanelDataService(() => at('2026-10-07T12:00:00Z'))
    const noReason = await service.voidPaidClaim(9102)
    expect(noReason).toMatchObject({ balance: -5, claim: { id: 9102, reward_amount: 15, status: 'VOIDED', void_reason: '', voided_by: 'PANEL_MAESTRO_DEMO' }, voidMovement: { student_id: 202, amount: -15, type: 'EXIT', related_claim_id: 9102, related_movement_id: 7107, origin: 'DEMO' } })
    expect((await service.getAccounts()).find(({ student_id }) => student_id === 202)?.balance).toBe(-5)
    await expect(service.voidPaidClaim(9102)).rejects.toMatchObject({ code: 'INVALID_STATE' })

    const withReason = new DemoPanelDataService(() => at('2026-10-07T12:00:00Z'))
    const result = await withReason.voidPaidClaim(9103, 'Duplicado solicitado por el maestro')
    expect(result.claim).toMatchObject({ reward_amount: 10, void_reason: 'Duplicado solicitado por el maestro' })
    expect(result.voidMovement.amount).toBe(-10)
    expect(await withReason.getMovements()).toHaveLength(11)
    expect(await withReason.getClaims()).toHaveLength(3)
    expect((await loadClaimDetail(withReason, 9103))?.inverseMovement?.id).toBe(result.voidMovement.id)

    const changedReward = new DemoPanelDataService(() => at('2026-10-07T12:00:00Z'))
    const internals = changedReward as unknown as { activities: ActivitySession[] }
    internals.activities = internals.activities.map((activity) => activity.id === 8102 ? { ...activity, reward_amount: 50 } : activity)
    const unchangedOriginal = await changedReward.voidPaidClaim(9102)
    expect(unchangedOriginal.voidMovement.amount).toBe(-15)
  })

  it('prepara anulación, autorización y nuevo cobro como operaciones atómicas ante fallos', async () => {
    let fail: DemoMutationName | '' = 'VOID_CLAIM'
    const service = new DemoPanelDataService(() => at('2026-10-07T12:00:00Z'), (operation) => { if (operation === fail) throw new Error('fallo inyectado') })
    const initial = await Promise.all([service.getAccounts(), service.getMovements(), service.getClaims(), service.getClaimEvents()])
    await expect(service.voidPaidClaim(9103)).rejects.toThrow('fallo inyectado')
    expect(await Promise.all([service.getAccounts(), service.getMovements(), service.getClaims(), service.getClaimEvents()])).toEqual(initial)

    fail = ''
    await service.voidPaidClaim(9103)
    fail = 'AUTHORIZE_REPEAT'
    const beforeFailedAuthorization = await Promise.all([service.getAccounts(), service.getMovements(), service.getClaims(), service.getClaimAuthorizations(), service.getClaimEvents()])
    await expect(service.authorizeRepeatClaim(9103)).rejects.toThrow('fallo inyectado')
    expect(await Promise.all([service.getAccounts(), service.getMovements(), service.getClaims(), service.getClaimAuthorizations(), service.getClaimEvents()])).toEqual(beforeFailedAuthorization)
    fail = ''
    const authorization = await service.authorizeRepeatClaim(9103)
    fail = 'SIMULATE_CLAIM'
    const beforeFailedPayment = await Promise.all([service.getAccounts(), service.getMovements(), service.getClaims(), service.getClaimAuthorizations(), service.getClaimEvents()])
    await expect(service.simulateAuthorizedClaim(authorization.id, 10)).rejects.toThrow('fallo inyectado')
    expect(await Promise.all([service.getAccounts(), service.getMovements(), service.getClaims(), service.getClaimAuthorizations(), service.getClaimEvents()])).toEqual(beforeFailedPayment)
  })

  it('mantiene separadas autorización y simulación, consume una sola vez y usa la recompensa vigente', async () => {
    const service = new DemoPanelDataService(() => at('2026-10-07T12:00:00Z'))
    const before = await Promise.all([service.getAccounts(), service.getMovements(), service.getClaims()])
    await service.voidPaidClaim(9103)
    const voided = await Promise.all([service.getAccounts(), service.getMovements(), service.getClaims()])
    const authorization = await service.authorizeRepeatClaim(9103)
    expect(authorization.status).toBe('AUTHORIZED')
    expect(await Promise.all([service.getAccounts(), service.getMovements(), service.getClaims()])).toEqual(voided)
    await expect(service.simulateAuthorizedClaim(authorization.id, 99)).rejects.toMatchObject({ code: 'INVALID_STATE' })
    const result = await service.simulateAuthorizedClaim(authorization.id, 10)
    expect(result).toMatchObject({ balance: 125, claim: { id: 9104, activity_id: 8104, student_id: 201, reward_amount: 10, status: 'PAID', authorized_by_claim_id: 9103 }, movement: { id: 7112, amount: 10, type: 'ENTRY', authorized_by_claim_id: 9103 } })
    expect((await service.getClaimAuthorizations())[0]).toMatchObject({ status: 'CONSUMED', consumed_claim_id: 9104 })
    const after = await Promise.all([service.getAccounts(), service.getMovements(), service.getClaims()])
    await expect(service.simulateAuthorizedClaim(authorization.id, 10)).rejects.toMatchObject({ code: 'INVALID_STATE' })
    expect(await Promise.all([service.getAccounts(), service.getMovements(), service.getClaims()])).toEqual(after)
    expect(before[0].find(({ student_id }) => student_id === 201)?.balance).toBe(125)
  })

  it('vuelve a validar vencimiento, cierre, participación y cuenta al ejecutar el cobro', async () => {
    let clock = at('2026-10-07T12:00:00Z')
    const expired = new DemoPanelDataService(() => clock)
    await expired.voidPaidClaim(9103); const expiryAuthorization = await expired.authorizeRepeatClaim(9103)
    clock = at('2026-10-08T08:00:00Z')
    await expect(expired.simulateAuthorizedClaim(expiryAuthorization.id, 10)).rejects.toMatchObject({ code: 'INELIGIBLE', message: 'La actividad ya venció.' })
    expect((await expired.getClaimAuthorizations())[0].status).toBe('AUTHORIZED')

    clock = at('2026-10-07T12:00:00Z')
    const closed = new DemoPanelDataService(() => clock)
    await closed.voidPaidClaim(9103); const closedAuthorization = await closed.authorizeRepeatClaim(9103)
    await closed.closeActiveActivity(8104, 'FINISHED')
    await expect(closed.simulateAuthorizedClaim(closedAuthorization.id, 10)).rejects.toMatchObject({ code: 'INELIGIBLE' })

    const changed = new DemoPanelDataService(() => at('2026-10-07T12:00:00Z'))
    await changed.voidPaidClaim(9103); const changedAuthorization = await changed.authorizeRepeatClaim(9103)
    const internals = changed as unknown as { activities: ActivitySession[] }
    internals.activities = internals.activities.map((activity) => activity.id === 8104 ? { ...activity, participant_student_ids: [] } : activity)
    await expect(changed.simulateAuthorizedClaim(changedAuthorization.id, 10)).rejects.toMatchObject({ code: 'INELIGIBLE', message: 'El alumno no pertenece a la selección guardada de participantes.' })
  })

  it('bloquea alumno sin cuenta o referencia indispensable y no crea saldos ficticios', async () => {
    const service = new DemoPanelDataService(() => at('2026-10-07T12:00:00Z'))
    const internals = service as unknown as { claims: ActivityClaim[]; movements: MovementRecord[] }
    const original: ActivityClaim = { id: 9200, activity_id: 8101, student_id: 206, reward_amount: 10, claimed_at: at('2026-10-07T08:00:00Z') / 1000, movement_id: 9200, status: 'VOIDED', void_movement_id: 9201, voided_at: at('2026-10-07T09:00:00Z') / 1000, voided_by: 'PANEL_MAESTRO_DEMO' }
    internals.claims = [...internals.claims, original]
    internals.movements = [...internals.movements, { id: 9200, student_id: 206, amount: 10, type: 'ENTRY', reason: 'Fixture inválida para prueba', timestamp: original.claimed_at, origin: 'DEMO', synced: false, schema_version: 1 }, { id: 9201, student_id: 206, amount: -10, type: 'EXIT', reason: 'Movimiento inverso fixture', timestamp: original.voided_at!, origin: 'DEMO', synced: false, schema_version: 1, related_claim_id: 9200, related_movement_id: 9200 }]
    await expect(service.authorizeRepeatClaim(9200)).rejects.toMatchObject({ code: 'INELIGIBLE', message: 'El alumno no tiene cuenta; no se puede autorizar ni registrar el cobro.' })
    expect((await service.getAccounts()).some(({ student_id }) => student_id === 206)).toBe(false)

    const broken = new DemoPanelDataService(() => at('2026-10-07T12:00:00Z'))
    const brokenInternal = broken as unknown as { movements: MovementRecord[] }
    brokenInternal.movements = brokenInternal.movements.filter(({ id }) => id !== 7108)
    await expect(broken.voidPaidClaim(9103)).rejects.toMatchObject({ code: 'INCONSISTENT' })

    const missingInverse = new DemoPanelDataService(() => at('2026-10-07T12:00:00Z'))
    const voided = await missingInverse.voidPaidClaim(9103)
    const brokenInverse = missingInverse as unknown as { movements: MovementRecord[] }
    brokenInverse.movements = brokenInverse.movements.filter(({ id }) => id !== voided.voidMovement.id)
    await expect(missingInverse.authorizeRepeatClaim(9103)).rejects.toMatchObject({ code: 'INCONSISTENT' })
  })

  it('conserva historial de actor, motivo, anulación, autorización y nuevo cobro', async () => {
    const service = new DemoPanelDataService(() => at('2026-10-07T12:00:00Z'))
    await service.voidPaidClaim(9103, 'Revisión docente')
    const authorization = await service.authorizeRepeatClaim(9103)
    await service.simulateAuthorizedClaim(authorization.id, 10)
    const source = await loadClaimDetail(service, 9103)
    const newest = (await loadClaimSnapshot(service)).entries[0]
    expect(source?.claim).toMatchObject({ status: 'VOIDED', void_reason: 'Revisión docente', voided_by: 'PANEL_MAESTRO_DEMO' })
    expect(source?.authorizations).toContainEqual(expect.objectContaining({ source_claim_id: 9103, status: 'CONSUMED' }))
    expect(source?.events.map(({ type }) => type)).toEqual(expect.arrayContaining(['VOIDED', 'REAUTHORIZED', 'NEW_CLAIM']))
    expect(newest.claim).toMatchObject({ status: 'PAID', authorized_by_claim_id: 9103, reward_amount: 10 })
  })

  it('refleja anulación y nuevo cobro en Dashboard, cuenta, movimiento, actividad y Cobros', async () => {
    const service = new DemoPanelDataService(() => at('2026-10-07T12:00:00Z'))
    await service.voidPaidClaim(9102)
    const voidedDashboard = await loadDashboardSnapshot(service)
    expect(voidedDashboard).toMatchObject({ accountAureos: 245, movementCount: 11 })
    expect((await loadAccountDetail(service, 202))?.account.balance).toBe(-5)
    const inverseEntry = (await loadMovementSnapshot(service)).entries.find(({ movement }) => movement.id === 7111)
    expect(inverseEntry).toMatchObject({ claim: { id: 9102, status: 'VOIDED' }, movement: { amount: -15 } })
    expect((await loadActivityDetail(service, 8102))?.participants.find(({ studentId }) => studentId === 202)?.claim?.status).toBe('VOIDED')
    expect((await loadClaimSnapshot(service)).entries).toHaveLength(3)

    const second = new DemoPanelDataService(() => at('2026-10-07T12:00:00Z'))
    await second.voidPaidClaim(9103); const authorization = await second.authorizeRepeatClaim(9103); await second.simulateAuthorizedClaim(authorization.id, 10)
    expect(await loadDashboardSnapshot(second)).toMatchObject({ accountAureos: 260, movementCount: 12 })
    expect((await loadAccountDetail(second, 201))?.account.balance).toBe(125)
    expect((await loadActivityDetail(second, 8104))?.participants[0].claim).toMatchObject({ id: 9104, status: 'PAID' })
    expect((await loadClaimSnapshot(second)).entries[0].claim.id).toBe(9104)
    expect((await loadMovementSnapshot(second)).entries[0].claim?.id).toBe(9104)
  })
})
