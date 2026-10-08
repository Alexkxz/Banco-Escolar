import type { ActivityClaim, ActivitySession, MovementRecord, Student, StudentAccount } from '../models/domain'
import type { PanelDataService } from './PanelDataService'
import { normalizeStudentSearch } from './studentDirectory'

export type AccountDirectoryEntry = { student: Student; account: StudentAccount | null }
export type AccountMovementEntry = { movement: MovementRecord; claim: ActivityClaim | null; activity: ActivitySession | null }
export type AccountDetail = { student: Student; account: StudentAccount; movements: MovementRecord[]; movementEntries: AccountMovementEntry[] }

export async function loadAccountDirectory(service: PanelDataService): Promise<AccountDirectoryEntry[]> {
  const [students, accounts] = await Promise.all([service.getStudents(), service.getAccounts()])
  const accountsByStudent = new Map(accounts.map((account) => [account.student_id, account]))
  return students.map((student) => ({ student, account: accountsByStudent.get(student.student_id) ?? null }))
}

export function filterAccounts(entries: readonly AccountDirectoryEntry[], query: string): AccountDirectoryEntry[] {
  const normalized = normalizeStudentSearch(query)
  return entries.filter(({ student }) => !normalized || normalizeStudentSearch(`${student.name} ${student.preferred_name}`).includes(normalized))
}

export async function loadAccountDetail(service: PanelDataService, accountId: number): Promise<AccountDetail | null> {
  const [students, accounts, movements, claims, activities] = await Promise.all([service.getStudents(), service.getAccounts(), service.getMovements(), service.getClaims(), service.getActivities()])
  const account = accounts.find((item) => item.student_id === accountId)
  const student = students.find((item) => item.student_id === accountId)
  if (!account || !student) return null
  const accountMovements = movements.filter((movement) => movement.student_id === account.student_id)
    .sort((left, right) => right.timestamp - left.timestamp || right.id - left.id)
  const claimsByMovement = new Map<number, ActivityClaim>()
  for (const claim of claims) {
    claimsByMovement.set(claim.movement_id, claim)
    if (claim.void_movement_id) claimsByMovement.set(claim.void_movement_id, claim)
  }
  const activitiesById = new Map(activities.map((activity) => [activity.id, activity]))
  return {
    student,
    account,
    movements: accountMovements,
    movementEntries: accountMovements.map((movement) => {
      const claim = claimsByMovement.get(movement.id) ?? null
      return { movement, claim, activity: claim ? activitiesById.get(claim.activity_id) ?? null : null }
    }),
  }
}

export function summarizeMovementHistory(movements: readonly MovementRecord[]) {
  return movements.reduce((totals, movement) => {
    if (movement.amount > 0) totals.entries += movement.amount
    else if (movement.amount < 0) totals.exits += Math.abs(movement.amount)
    return totals
  }, { entries: 0, exits: 0 })
}
