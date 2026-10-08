import { describe, expect, it } from 'vitest'
import { DemoPanelDataService } from '../src/services/DemoPanelDataService'
import { filterAndSortActivities, getActivityRemaining, loadActivityDetail } from '../src/services/activityQueries'
import type { ActivityConfiguration } from '../src/services/DemoPanelDataService'

const base: ActivityConfiguration = {
  activity_number_enabled: true, activity_number: 5, timed: true, duration_seconds: 300,
  reward_amount: 12, participant_mode: 'ALL', selected_student_ids: [],
}

describe('PM.6 actividades de demostración', () => {
  it('filtra estados y busca nombres; al ordenar por cierre, los abiertos quedan al final', async () => {
    const activities = await new DemoPanelDataService().getActivities()
    expect(filterAndSortActivities(activities, 'actividad 2', 'FINISHED', 'started').map(({ id }) => id)).toEqual([8102])
    expect(filterAndSortActivities(activities, '', 'ACTIVE', 'started').map(({ id }) => id)).toEqual([8104, 8101])
    expect(filterAndSortActivities(activities, '', 'all', 'closed').map(({ id }) => id)).toEqual([8102, 8103, 8104, 8101])
  })

  it('crea con participantes congelados, hora inyectada e ID único; no altera datos monetarios ni reclamos', async () => {
    const demo = new DemoPanelDataService(() => Date.parse('2026-10-08T12:34:56Z'))
    const [accountsBefore, movementsBefore, claimsBefore] = await Promise.all([demo.getAccounts(), demo.getMovements(), demo.getClaims()])
    const created = await demo.createActivity(base)
    const selected = await demo.createActivity({ ...base, participant_mode: 'SELECTED', selected_student_ids: [202, 206] })
    expect(created).toMatchObject({ id: 8105, started_at: Date.parse('2026-10-08T12:34:56Z') / 1000, participant_student_ids: [201, 202, 203, 204, 205, 206], status: 'ACTIVE' })
    expect(selected).toMatchObject({ id: 8106, participant_student_ids: [202, 206] })
    expect(new Set([created.id, selected.id]).size).toBe(2)
    expect(await demo.getAccounts()).toEqual(accountsBefore)
    expect(await demo.getMovements()).toEqual(movementsBefore)
    expect(await demo.getClaims()).toEqual(claimsBefore)
  })

  it('conserva ID e inicio al editar; bloquea actividad con cobro previo', async () => {
    const demo = new DemoPanelDataService()
    const before = (await demo.getActivities()).find(({ id }) => id === 8101)!
    const updated = await demo.updateActiveActivity(8101, { ...base, reward_amount: 20 })
    expect(updated).toMatchObject({ id: before.id, started_at: before.started_at, reward_amount: 20 })
    await expect(demo.updateActiveActivity(8104, base)).rejects.toMatchObject({ code: 'HAS_CLAIMS' })
    await expect(demo.updateActiveActivity(8102, base)).rejects.toMatchObject({ code: 'INVALID_STATE' })
  })

  it('vence exactamente al cumplir la duración sin cambiar el estado activo', async () => {
    const activity = (await new DemoPanelDataService().getActivities()).find(({ id }) => id === 8104)!
    expect(getActivityRemaining(activity, (activity.started_at + activity.duration_seconds - 1) * 1000).state).toBe('running')
    expect(getActivityRemaining(activity, (activity.started_at + activity.duration_seconds) * 1000)).toEqual({ state: 'expired', seconds: 0 })
    expect(activity.status).toBe('ACTIVE')
  })

  it('finaliza o cancela únicamente activas, registra cierre y no modifica reclamos, movimientos ni cuentas', async () => {
    const demo = new DemoPanelDataService(() => Date.parse('2026-10-08T13:00:00Z'))
    const snapshot = await Promise.all([demo.getAccounts(), demo.getMovements(), demo.getClaims()])
    const finished = await demo.closeActiveActivity(8101, 'FINISHED')
    const cancelled = await demo.closeActiveActivity(8104, 'CANCELLED')
    expect(finished).toMatchObject({ status: 'FINISHED', closed_at: Date.parse('2026-10-08T13:00:00Z') / 1000 })
    expect(cancelled.status).toBe('CANCELLED')
    await expect(demo.closeActiveActivity(8101, 'CANCELLED')).rejects.toMatchObject({ code: 'INVALID_STATE' })
    expect(await Promise.all([demo.getAccounts(), demo.getMovements(), demo.getClaims()])).toEqual(snapshot)
  })

  it('separa alumno sin cuenta, cobro ausente y enlaces solo a movimientos existentes', async () => {
    const detail = await loadActivityDetail(new DemoPanelDataService(), 8101)
    const teo = detail!.participants.find(({ studentId }) => studentId === 206)!
    expect(teo.hasAccount).toBe(false)
    expect(teo.claim).toBeNull()
    const paid = await loadActivityDetail(new DemoPanelDataService(), 8104)
    expect(paid!.participants[0]).toMatchObject({ hasAccount: true, claim: { id: 9103 }, movement: { id: 7108 } })
  })
})
