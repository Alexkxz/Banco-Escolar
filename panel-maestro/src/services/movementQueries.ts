import type { ActivityClaim, ActivitySession, MovementRecord, Student } from '../models/domain'
import type { PanelDataService } from './PanelDataService'
import { normalizeStudentSearch } from './studentDirectory'

export const PANEL_TIME_ZONE = 'America/Mexico_City'
export const movementDateFormatter = new Intl.DateTimeFormat('es-MX', {
  day: '2-digit', month: 'short', year: 'numeric', hour: '2-digit', minute: '2-digit',
  timeZone: PANEL_TIME_ZONE, timeZoneName: 'short',
})

export type MovementListEntry = {
  movement: MovementRecord
  student: Student | null
  claim: ActivityClaim | null
  activity: ActivitySession | null
}
export type MovementFilters = { studentId: string; type: 'all' | 'ENTRY' | 'EXIT'; from: string; through: string }
export type DateRangeBounds = { fromInclusive: number | null; toExclusive: number | null }

function parseCalendarDate(value: string): { year: number; month: number; day: number } {
  const match = /^(\d{4})-(\d{2})-(\d{2})$/.exec(value)
  if (!match) throw new Error('Escribe una fecha válida.')
  const [, yearText, monthText, dayText] = match
  const year = Number(yearText); const month = Number(monthText); const day = Number(dayText)
  const check = new Date(Date.UTC(year, month - 1, day))
  if (check.getUTCFullYear() !== year || check.getUTCMonth() !== month - 1 || check.getUTCDate() !== day) throw new Error('Escribe una fecha válida.')
  return { year, month, day }
}

function zonedMidnightUtc({ year, month, day }: { year: number; month: number; day: number }): number {
  const desiredUtc = Date.UTC(year, month - 1, day)
  let estimate = desiredUtc
  const partsFormatter = new Intl.DateTimeFormat('en-CA', {
    timeZone: PANEL_TIME_ZONE, year: 'numeric', month: '2-digit', day: '2-digit',
    hour: '2-digit', minute: '2-digit', second: '2-digit', hourCycle: 'h23',
  })
  for (let attempt = 0; attempt < 4; attempt += 1) {
    const parts = Object.fromEntries(partsFormatter.formatToParts(estimate).map((part) => [part.type, part.value]))
    const representedUtc = Date.UTC(Number(parts.year), Number(parts.month) - 1, Number(parts.day), Number(parts.hour), Number(parts.minute), Number(parts.second))
    const adjustment = desiredUtc - representedUtc
    estimate += adjustment
    if (adjustment === 0) break
  }
  return estimate
}

function nextCalendarDay(date: { year: number; month: number; day: number }) {
  const next = new Date(Date.UTC(date.year, date.month - 1, date.day + 1))
  return { year: next.getUTCFullYear(), month: next.getUTCMonth() + 1, day: next.getUTCDate() }
}

export function getDateRangeBounds(from: string, through: string): DateRangeBounds {
  const first = from ? parseCalendarDate(from) : null
  const last = through ? parseCalendarDate(through) : null
  if (first && last && Date.UTC(first.year, first.month - 1, first.day) > Date.UTC(last.year, last.month - 1, last.day)) {
    throw new Error('El inicio del rango debe ser anterior o igual al final.')
  }
  return {
    fromInclusive: first ? zonedMidnightUtc(first) : null,
    toExclusive: last ? zonedMidnightUtc(nextCalendarDay(last)) : null,
  }
}

export async function loadMovementSnapshot(service: PanelDataService): Promise<{ entries: MovementListEntry[]; students: readonly Student[] }> {
  const [students, movements, claims, activities] = await Promise.all([
    service.getStudents(), service.getMovements(), service.getClaims(), service.getActivities(),
  ])
  const studentsById = new Map(students.map((student) => [student.student_id, student]))
  const claimsByMovement = new Map<number, ActivityClaim>()
  for (const claim of claims) {
    claimsByMovement.set(claim.movement_id, claim)
    if (claim.void_movement_id) claimsByMovement.set(claim.void_movement_id, claim)
  }
  const activitiesById = new Map(activities.map((activity) => [activity.id, activity]))
  const entries = movements.map((movement) => {
    const claim = claimsByMovement.get(movement.id) ?? null
    return { movement, student: studentsById.get(movement.student_id) ?? null, claim, activity: claim ? activitiesById.get(claim.activity_id) ?? null : null }
  }).sort((left, right) => right.movement.timestamp - left.movement.timestamp || right.movement.id - left.movement.id)
  return { entries, students }
}

export async function loadMovementDirectory(service: PanelDataService): Promise<MovementListEntry[]> {
  return (await loadMovementSnapshot(service)).entries
}

export function filterMovements(entries: readonly MovementListEntry[], filters: MovementFilters): MovementListEntry[] {
  const bounds = getDateRangeBounds(filters.from, filters.through)
  return entries.filter(({ movement, student }) => {
    const time = movement.timestamp * 1000
    return (!filters.studentId || String(movement.student_id) === filters.studentId)
      && (filters.type === 'all' || movement.type === filters.type)
      && (bounds.fromInclusive === null || time >= bounds.fromInclusive)
      && (bounds.toExclusive === null || time < bounds.toExclusive)
      && (!filters.studentId || Boolean(student))
  })
}

export function searchStudents(students: readonly Student[], query: string): Student[] {
  const normalized = normalizeStudentSearch(query)
  return students.filter((student) => !normalized || normalizeStudentSearch(`${student.name} ${student.preferred_name}`).includes(normalized))
}
