import type { ActivityClaim, ActivitySession, DemoClaimAuthorization, DemoClaimEvent, MovementRecord, Student, StudentAccount } from '../models/domain'
import type { DemoClaimOperations } from './DemoPanelDataService'
import type { PanelDataService } from './PanelDataService'
import { getDateRangeBounds, movementDateFormatter, PANEL_TIME_ZONE } from './movementQueries'

export { movementDateFormatter, PANEL_TIME_ZONE }

export type ClaimFilters = { studentId: string; activityId: string; status: 'all' | ActivityClaim['status']; from: string; through: string }
export type ClaimListEntry = { claim: ActivityClaim; student: Student | null; activity: ActivitySession | null; movement: MovementRecord | null }
export type ClaimDetailEntry = ClaimListEntry & { inverseMovement: MovementRecord | null; account: StudentAccount | null; authorizations: readonly DemoClaimAuthorization[]; events: readonly DemoClaimEvent[]; consistent: boolean; inconsistency: string }
export type ClaimQueryService = PanelDataService & Partial<Pick<DemoClaimOperations, 'getClaimAuthorizations' | 'getClaimEvents'>>

export async function loadClaimSnapshot(service: ClaimQueryService): Promise<{ entries: ClaimListEntry[]; students: readonly Student[]; activities: readonly ActivitySession[] }> {
  const [students, activities, claims, movements] = await Promise.all([service.getStudents(), service.getActivities(), service.getClaims(), service.getMovements()])
  const studentsById = new Map(students.map((student) => [student.student_id, student]))
  const activitiesById = new Map(activities.map((activity) => [activity.id, activity]))
  const movementsById = new Map(movements.map((movement) => [movement.id, movement]))
  const entries = claims.map((claim) => ({ claim, student: studentsById.get(claim.student_id) ?? null, activity: activitiesById.get(claim.activity_id) ?? null, movement: movementsById.get(claim.movement_id) ?? null }))
    .sort((left, right) => right.claim.claimed_at - left.claim.claimed_at || right.claim.id - left.claim.id)
  return { entries, students, activities }
}

export function filterClaims(entries: readonly ClaimListEntry[], filters: ClaimFilters): ClaimListEntry[] {
  const bounds = getDateRangeBounds(filters.from, filters.through)
  return entries.filter(({ claim }) => (!filters.studentId || claim.student_id.toString() === filters.studentId)
      && (!filters.activityId || claim.activity_id.toString() === filters.activityId)
      && (filters.status === 'all' || claim.status === filters.status)
      && (bounds.fromInclusive === null || claim.claimed_at * 1000 >= bounds.fromInclusive)
      && (bounds.toExclusive === null || claim.claimed_at * 1000 < bounds.toExclusive))
}

export async function loadClaimDetail(service: ClaimQueryService, claimId: number): Promise<ClaimDetailEntry | null> {
  const [snapshot, accounts, movements, authorizations, events] = await Promise.all([
    loadClaimSnapshot(service), service.getAccounts(), service.getMovements(),
    service.getClaimAuthorizations?.() ?? Promise.resolve([]), service.getClaimEvents?.() ?? Promise.resolve([]),
  ])
  const entry = snapshot.entries.find((candidate) => candidate.claim.id === claimId)
  if (!entry) return null
  const { claim } = entry
  const account = accounts.find((candidate) => candidate.student_id === claim.student_id) ?? null
  const inverseMovement = claim.void_movement_id ? movements.find((candidate) => candidate.id === claim.void_movement_id) ?? null : null
  const consistent = Boolean(entry.student && entry.activity && entry.movement && account
    && entry.movement.student_id === claim.student_id && entry.movement.type === 'ENTRY'
    && entry.movement.amount === claim.reward_amount && entry.movement.timestamp === claim.claimed_at
    && (claim.status !== 'VOIDED' || (inverseMovement?.student_id === claim.student_id && inverseMovement.type === 'EXIT' && inverseMovement.amount === -claim.reward_amount)))
  const inconsistency = !entry.student ? 'No se encontró el alumno asociado.'
    : !entry.activity ? 'No se encontró la actividad asociada.'
      : !account ? 'El alumno no tiene una cuenta disponible.'
        : !entry.movement ? 'No se encontró el movimiento original del cobro.'
          : entry.movement.student_id !== claim.student_id || entry.movement.type !== 'ENTRY' || entry.movement.amount !== claim.reward_amount || entry.movement.timestamp !== claim.claimed_at ? 'El movimiento original no coincide con los datos del cobro.'
            : claim.status === 'VOIDED' && !inverseMovement ? 'No se encontró el movimiento inverso de la anulación.'
              : claim.status === 'VOIDED' && (inverseMovement?.student_id !== claim.student_id || inverseMovement.type !== 'EXIT' || inverseMovement.amount !== -claim.reward_amount) ? 'El movimiento inverso no coincide con el importe y alumno del cobro original.' : ''
  return { ...entry, account, inverseMovement, authorizations: authorizations.filter((authorization) => authorization.activity_id === claim.activity_id && authorization.student_id === claim.student_id), events: events.filter((event) => event.claim_id === claim.id || event.related_claim_id === claim.id), consistent, inconsistency }
}

export function formatClaimAmount(amount: number) { return `${Math.abs(amount).toLocaleString('es-MX')} Áureos` }
