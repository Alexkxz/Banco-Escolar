import type { ActivitySession, MovementRecord, Student } from '../models/domain'
import type { PanelDataService } from './PanelDataService'

export interface DashboardMovement extends MovementRecord {
  studentName: string
}

export interface DashboardSnapshot {
  studentCount: number
  accountCount: number
  accountAureos: number
  activeActivityCount: number
  movementCount: number
  studentsWithoutAccount: readonly Student[]
  recentMovements: readonly DashboardMovement[]
}

export function createDashboardSnapshot(data: {
  students: readonly Student[]
  accounts: readonly { student_id: number; balance: number }[]
  activities: readonly ActivitySession[]
  movements: readonly MovementRecord[]
}): DashboardSnapshot {
  const studentById = new Map(data.students.map((student) => [student.student_id, student]))
  const accountIds = new Set(data.accounts.map((account) => account.student_id))
  const recentMovements = [...data.movements]
    .sort((left, right) => right.timestamp - left.timestamp || right.id - left.id)
    .slice(0, 5)
    .map((movement) => ({
      ...movement,
      studentName: studentById.get(movement.student_id)?.preferred_name
        || studentById.get(movement.student_id)?.name
        || `Alumno ${movement.student_id}`,
    }))

  return {
    studentCount: data.students.length,
    accountCount: data.accounts.length,
    // Sum the account snapshot directly. History is not the source of balances.
    accountAureos: data.accounts.reduce((total, account) => total + account.balance, 0),
    activeActivityCount: data.activities.filter((activity) => activity.status === 'ACTIVE').length,
    movementCount: data.movements.length,
    studentsWithoutAccount: data.students.filter((student) => !accountIds.has(student.student_id)),
    recentMovements,
  }
}

export async function loadDashboardSnapshot(service: PanelDataService): Promise<DashboardSnapshot> {
  const [students, accounts, activities, movements] = await Promise.all([
    service.getStudents(),
    service.getAccounts(),
    service.getActivities(),
    service.getMovements(),
  ])
  return createDashboardSnapshot({ students, accounts, activities, movements })
}
