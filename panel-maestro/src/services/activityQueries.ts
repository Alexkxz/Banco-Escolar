import type { ActivityClaim, ActivitySession, MovementRecord, Student, StudentAccount } from '../models/domain'
import type { PanelDataService } from './PanelDataService'
import { normalizeStudentSearch } from './studentDirectory'

export const ACTIVITY_TIME_ZONE = 'America/Mexico_City'
export const activityDateFormatter = new Intl.DateTimeFormat('es-MX', {
  day: '2-digit', month: 'short', year: 'numeric', hour: '2-digit', minute: '2-digit',
  timeZone: ACTIVITY_TIME_ZONE, timeZoneName: 'short',
})

export type ActivitySort = 'started' | 'closed'
export type ActivityStatusFilter = 'all' | ActivitySession['status']
export type ActivityListEntry = ActivitySession & { displayName: string }
export type ActivityParticipantEntry = {
  student: Student | null
  studentId: number
  hasAccount: boolean
  claim: ActivityClaim | null
  movement: MovementRecord | null
}
export type ActivityDetailSnapshot = {
  activity: ActivitySession
  displayName: string
  participants: ActivityParticipantEntry[]
  participantRosterAvailable: boolean
  claims: readonly ActivityClaim[]
  allStudents: readonly Student[]
}

export function activityDisplayName(activity: ActivitySession): string {
  return activity.activity_number_enabled ? `Actividad ${activity.activity_number}` : `Actividad ${activity.id}`
}

export function filterAndSortActivities(activities: readonly ActivitySession[], search: string, status: ActivityStatusFilter, sort: ActivitySort): ActivityListEntry[] {
  const query = normalizeStudentSearch(search)
  return activities.filter((activity) => (status === 'all' || activity.status === status)
      && (!query || normalizeStudentSearch(activityDisplayName(activity)).includes(query)))
    .sort((left, right) => {
      if (sort === 'closed') {
        const leftClose = left.closed_at > 0 ? left.closed_at : Number.NEGATIVE_INFINITY
        const rightClose = right.closed_at > 0 ? right.closed_at : Number.NEGATIVE_INFINITY
        return rightClose - leftClose || right.started_at - left.started_at || right.id - left.id
      }
      return right.started_at - left.started_at || right.id - left.id
    })
    .map((activity) => ({ ...activity, displayName: activityDisplayName(activity) }))
}

export function getActivityRemaining(activity: ActivitySession, nowMs: number) {
  if (activity.status !== 'ACTIVE') return { state: 'closed' as const, seconds: 0 }
  if (activity.duration_seconds === 0) return { state: 'untimed' as const, seconds: 0 }
  const nowEpoch = Math.floor(nowMs / 1000)
  if (!Number.isFinite(nowMs) || nowEpoch < activity.started_at) return { state: 'pending' as const, seconds: 0 }
  const elapsed = Math.max(0, nowEpoch - activity.started_at)
  if (elapsed >= activity.duration_seconds) return { state: 'expired' as const, seconds: 0 }
  return { state: 'running' as const, seconds: activity.duration_seconds - elapsed }
}

export function formatRemaining(seconds: number) {
  const hours = Math.floor(seconds / 3600)
  const minutes = Math.floor((seconds % 3600) / 60)
  const remainder = seconds % 60
  return hours > 0 ? `${hours}:${String(minutes).padStart(2, '0')}:${String(remainder).padStart(2, '0')}` : `${minutes}:${String(remainder).padStart(2, '0')}`
}

export async function loadActivityDetail(service: PanelDataService, activityId: number): Promise<ActivityDetailSnapshot | null> {
  const [activities, students, accounts, claims, movements] = await Promise.all([
    service.getActivities(), service.getStudents(), service.getAccounts(), service.getClaims(), service.getMovements(),
  ])
  const activity = activities.find((item) => item.id === activityId)
  if (!activity) return null
  const studentById = new Map(students.map((student) => [student.student_id, student]))
  const accountIds = new Set((accounts as readonly StudentAccount[]).map((account) => account.student_id))
  const activityClaims = claims.filter((claim) => claim.activity_id === activity.id)
    .sort((a, b) => b.claimed_at - a.claimed_at || b.id - a.id)
  const claimByStudent = new Map<number, ActivityClaim>()
  for (const claim of activityClaims) if (!claimByStudent.has(claim.student_id)) claimByStudent.set(claim.student_id, claim)
  const movementById = new Map(movements.map((movement) => [movement.id, movement]))
  const knownParticipantIds = activity.participant_mode === 'PARTICIPANTS_DISABLED' ? [] : activity.participant_student_ids
  const participantIds = [...new Set([...knownParticipantIds, ...activityClaims.map((claim) => claim.student_id)])]
  const participants = participantIds.map((studentId) => {
    const claim = claimByStudent.get(studentId) ?? null
    return { student: studentById.get(studentId) ?? null, studentId, hasAccount: accountIds.has(studentId), claim, movement: claim ? movementById.get(claim.movement_id) ?? null : null }
  })
  return { activity, displayName: activityDisplayName(activity), participants, participantRosterAvailable: activity.participant_mode !== 'PARTICIPANTS_DISABLED', claims: activityClaims, allStudents: students }
}
