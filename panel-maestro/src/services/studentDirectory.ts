import type { Student, StudentAccount } from '../models/domain'
import type { PanelDataService } from './PanelDataService'

export type StudentDirectoryEntry = { student: Student; hasAccount: boolean }
export type StudentFilters = { query: string; grade: string; group: string; account: 'all' | 'assigned' | 'missing' }

export async function loadStudentDirectory(service: PanelDataService): Promise<StudentDirectoryEntry[]> {
  const [students, accounts] = await Promise.all([service.getStudents(), service.getAccounts()])
  const accountIds = new Set(accounts.map((account: StudentAccount) => account.student_id))
  return students.map((student) => ({ student, hasAccount: accountIds.has(student.student_id) }))
}

export function normalizeStudentSearch(value: string): string {
  return value.normalize('NFD').replace(/\p{Diacritic}/gu, '').toLocaleLowerCase('es-MX').trim()
}

export function filterStudents(entries: readonly StudentDirectoryEntry[], filters: StudentFilters): StudentDirectoryEntry[] {
  const query = normalizeStudentSearch(filters.query)
  return entries.filter(({ student, hasAccount }) => {
    const nameMatches = !query || normalizeStudentSearch(`${student.name} ${student.preferred_name}`).includes(query)
    return nameMatches && (!filters.grade || String(student.grade) === filters.grade)
      && (!filters.group || student.group === filters.group)
      && (filters.account === 'all' || (filters.account === 'assigned' ? hasAccount : !hasAccount))
  })
}

export function getStudentInitials(student: Pick<Student, 'name' | 'preferred_name'>): string {
  const parts = (student.preferred_name || student.name).trim().split(/\s+/).filter(Boolean)
  const fallback = student.name.trim().split(/\s+/).filter(Boolean)
  const words = parts.length > 1 ? parts : [parts[0], fallback.find((word) => word !== parts[0])].filter(Boolean) as string[]
  return words.slice(0, 2).map((word) => Array.from(word)[0]?.toLocaleUpperCase('es-MX') ?? '').join('')
}

export const avatarColor = (studentId: number) => ['#d8eff0', '#e7e0f6', '#f5e7d4', '#dcebdc', '#f4dfe5', '#dbe8f7'][Math.abs(studentId) % 6]
