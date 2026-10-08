import { describe, expect, it } from 'vitest'
import { DemoPanelDataService } from '../src/services/DemoPanelDataService'
import { filterAccounts, loadAccountDetail, loadAccountDirectory, summarizeMovementHistory } from '../src/services/accountQueries'
import { filterMovements, getDateRangeBounds, loadMovementDirectory, movementDateFormatter, PANEL_TIME_ZONE, type MovementListEntry } from '../src/services/movementQueries'
import type { PanelDataService } from '../src/services/PanelDataService'

describe('PM.5 consultas de cuentas y movimientos', () => {
  it('asocia cuenta, alumno y movimientos por student_id sin derivar el saldo', async () => {
    const demo = new DemoPanelDataService()
    const detail = await loadAccountDetail(demo, 201)
    expect(detail?.student.name).toBe('Ximena Sol')
    expect(detail?.account).toMatchObject({ student_id: 201, balance: 125 })
    expect(detail?.movements.map(({ id }) => id)).toEqual([7109, 7108, 7106, 7101])
    const totals = summarizeMovementHistory(detail!.movements)
    expect(totals).toEqual({ entries: 145, exits: 20 })
    expect(detail?.account.balance).toBe(125)
  })

  it('mantiene el saldo directo aunque cambie el total del historial consultado', async () => {
    const demo = new DemoPanelDataService()
    const changedHistory = Object.assign(demo, { getMovements: async () => [{ id: 8801, student_id: 201, amount: 7, type: 'ENTRY' as const, reason: 'Muestra independiente', timestamp: 1791378900, origin: 'DEMO' as const, synced: false, schema_version: 1 }] })
    const detail = await loadAccountDetail(changedHistory, 201)
    expect(detail?.account.balance).toBe(125)
    expect(summarizeMovementHistory(detail!.movements)).toEqual({ entries: 7, exits: 0 })
  })

  it('muestra alumnos sin cuenta sin asignarles cero ni crear detalle', async () => {
    const demo = new DemoPanelDataService()
    const entries = await loadAccountDirectory(demo)
    expect(entries.find(({ student }) => student.student_id === 206)).toMatchObject({ account: null })
    expect(await loadAccountDetail(demo, 206)).toBeNull()
  })

  it('busca cuentas por nombre sin distinguir acentos ni mayúsculas', async () => {
    const entries = await loadAccountDirectory(new DemoPanelDataService())
    expect(filterAccounts(entries, 'LIA').map(({ student }) => student.student_id)).toEqual([203])
  })

  it('conserva los vínculos de actividad y cobro definidos para cada movimiento', async () => {
    const entries = await loadMovementDirectory(new DemoPanelDataService())
    expect(entries.find(({ movement }) => movement.id === 7106)).toMatchObject({ claim: { id: 9101, activity_id: 8102 }, activity: { id: 8102 } })
    expect(entries.find(({ movement }) => movement.id === 7101)).toMatchObject({ claim: null, activity: null })
    expect(entries.map(({ movement }) => movement.id)).toEqual([7110, 7109, 7108, 7107, 7106, 7105, 7104, 7103, 7102, 7101])
  })

  it('combina filtros de alumno, tipo y rango con inicio incluido y final exclusivo en la zona local', async () => {
    const entries = await loadMovementDirectory(new DemoPanelDataService())
    const bounds = getDateRangeBounds('2026-10-07', '2026-10-07')
    expect(bounds.fromInclusive).toBe(Date.parse('2026-10-07T06:00:00Z'))
    expect(bounds.toExclusive).toBe(Date.parse('2026-10-08T06:00:00Z'))
    const atStart = { ...entries[0], movement: { ...entries[0].movement, id: 111, student_id: 201, type: 'ENTRY' as const, timestamp: bounds.fromInclusive! / 1000 } }
    const atEnd = { ...entries[0], movement: { ...entries[0].movement, id: 112, student_id: 201, type: 'ENTRY' as const, timestamp: bounds.toExclusive! / 1000 } }
    const candidates: MovementListEntry[] = [atStart, atEnd, ...entries]
    const matches = filterMovements(candidates, { studentId: '201', type: 'ENTRY', from: '2026-10-07', through: '2026-10-07' })
    expect(matches.map(({ movement }) => movement.id)).toEqual([111, 7108])
    expect(() => getDateRangeBounds('2026-10-08', '2026-10-07')).toThrow(/anterior o igual/)
  })

  it('formatea instantes reproducibles en español y America/Mexico_City', () => {
    const formatted = movementDateFormatter.format(Date.parse('2026-10-07T09:15:00Z'))
    expect(PANEL_TIME_ZONE).toBe('America/Mexico_City')
    expect(formatted).toMatch(/07 oct 2026/i)
    expect(formatted).not.toContain('UTC')
  })

  it('da totales cero para una consulta exitosa vacía y conserva el error de consulta', async () => {
    expect(summarizeMovementHistory([])).toEqual({ entries: 0, exits: 0 })
    const empty = Object.assign(new DemoPanelDataService(), { getMovements: async () => [] }) as PanelDataService
    const failed = Object.assign(new DemoPanelDataService(), { getMovements: async () => { throw new Error('offline') } }) as PanelDataService
    expect((await loadAccountDetail(empty, 201)?.then((detail) => summarizeMovementHistory(detail!.movements)))).toEqual({ entries: 0, exits: 0 })
    await expect(loadMovementDirectory(failed)).rejects.toThrow('offline')
  })
})
