import type { ActivityClaim, ActivitySession, DemoClaimAuthorization, DemoClaimEvent, MovementRecord, Student, StudentAccount } from '../models/domain'
import type { PanelDataService } from './PanelDataService'
import type { DemoPanelSnapshot } from './demoPersistence'
import { academicResult, type AcademicRecord, type AcademicDraft, type AcademicRules, type AttendanceRecord, type AttendanceRules, type SchoolApplication, aspectLevel, academicLevelLabels, aspectLabels, schoolDateAt, schoolClockSeconds, validateAcademicDraft, validateAcademicRules, validateAttendanceRules, validateSchoolDate, createDefaultAcademicRules, createDefaultAttendanceRules, cloneAcademicRules, cloneAttendanceRules, attendanceAmount, type AcademicIndicator, type ComprehensionAspect } from './schoolDomain'

/**
 * Conjunto local, determinista y completamente ficticio de PM.3.
 * No lee firmware, archivos, red ni almacenamiento; sus métodos son de solo lectura.
 */
const demoStudents: readonly Student[] = [
  { student_id: 201, nfc_uid: null, name: 'Ximena Sol', preferred_name: 'Ximena', grade: 3, group: 'A', roster_number: 1, avatar_asset: null, level: null, status: 'ACTIVE' },
  { student_id: 202, nfc_uid: null, name: 'Bruno Vega', preferred_name: 'Bruno', grade: 3, group: 'A', roster_number: 2, avatar_asset: null, level: null, status: 'ACTIVE' },
  { student_id: 203, nfc_uid: null, name: 'Lía Robles', preferred_name: 'Lía', grade: 3, group: 'A', roster_number: 3, avatar_asset: null, level: null, status: 'ACTIVE' },
  { student_id: 204, nfc_uid: null, name: 'Gael Luna', preferred_name: 'Gael', grade: 4, group: 'B', roster_number: 1, avatar_asset: null, level: null, status: 'INACTIVE' },
  { student_id: 205, nfc_uid: null, name: 'Mara Ríos', preferred_name: 'Mara', grade: 4, group: 'B', roster_number: 2, avatar_asset: null, level: null, status: 'ACTIVE' },
  { student_id: 206, nfc_uid: null, name: 'Teo Nube', preferred_name: 'Teo', grade: 4, group: 'B', roster_number: 3, avatar_asset: null, level: null, status: 'ACTIVE' },
]

// Saldo directo de cada cuenta demo; el Dashboard nunca lo reconstruye desde movimientos.
const demoAccounts: readonly StudentAccount[] = [
  { student_id: 201, balance: 125, schema_version: 1 },
  // Saldo reducido para ilustrar una anulación cuyo inverso permite saldo negativo.
  { student_id: 202, balance: 10, schema_version: 1 },
  { student_id: 203, balance: 45, schema_version: 1 },
  { student_id: 204, balance: 45, schema_version: 1 },
  { student_id: 205, balance: 35, schema_version: 1 },
  // Teo (206) ilustra un alumno registrado sin cuenta y sin saldo asignado.
]

const demoActivities: readonly ActivitySession[] = [
  { id: 8101, activity_number_enabled: true, activity_number: 1, reward_amount: 10, duration_seconds: 3600, participant_mode: 'ALL', participant_student_ids: [201, 202, 203, 204, 205, 206], started_at: Date.parse('2026-10-01T08:00:00Z') / 1000, status: 'ACTIVE', closed_at: 0 },
  { id: 8102, activity_number_enabled: true, activity_number: 2, reward_amount: 15, duration_seconds: 1800, participant_mode: 'SELECTED', participant_student_ids: [201, 202, 203], started_at: Date.parse('2026-10-06T12:30:00Z') / 1000, status: 'FINISHED', closed_at: Date.parse('2026-10-06T14:30:00Z') / 1000 },
  { id: 8103, activity_number_enabled: true, activity_number: 3, reward_amount: 20, duration_seconds: 1200, participant_mode: 'ALL', participant_student_ids: [201, 202, 203, 204, 205, 206], started_at: Date.parse('2026-10-04T09:00:00Z') / 1000, status: 'CANCELLED', closed_at: Date.parse('2026-10-04T09:20:00Z') / 1000 },
  { id: 8104, activity_number_enabled: true, activity_number: 4, reward_amount: 10, duration_seconds: 86400, participant_mode: 'SELECTED', participant_student_ids: [201], started_at: Date.parse('2026-10-07T08:00:00Z') / 1000, status: 'ACTIVE', closed_at: 0 },
]

export type ActivityConfiguration = {
  activity_number_enabled: boolean
  activity_number: number
  timed: boolean
  duration_seconds: number
  reward_amount: number
  participant_mode: ActivitySession['participant_mode']
  selected_student_ids: number[]
}

export type DemoActivityOperations = {
  createActivity(configuration: ActivityConfiguration): Promise<ActivitySession>
  updateActiveActivity(id: number, configuration: ActivityConfiguration): Promise<ActivitySession>
  closeActiveActivity(id: number, status: 'FINISHED' | 'CANCELLED'): Promise<ActivitySession>
}

export class ActivityOperationError extends Error {
  constructor(message: string, readonly code: 'INVALID_CONFIGURATION' | 'NOT_FOUND' | 'INVALID_STATE' | 'HAS_CLAIMS' | 'INVALID_TIME') {
    super(message)
    this.name = 'ActivityOperationError'
  }
}

const MAX_ACTIVITY_NUMBER = 9999
const MAX_ACTIVITY_REWARD = 10000
const MAX_ACTIVITY_DURATION_SECONDS = 999 * 60 + 59
const MIN_VALID_ACTIVITY_EPOCH = 1735689600

export function validateDemoActivityConfiguration(configuration: ActivityConfiguration, registeredIds: readonly number[]) {
  if (configuration.activity_number_enabled && (!Number.isInteger(configuration.activity_number) || configuration.activity_number < 1 || configuration.activity_number > MAX_ACTIVITY_NUMBER)) return 'El número de actividad debe estar entre 1 y 9999.'
  if (!Number.isInteger(configuration.reward_amount) || configuration.reward_amount < 1 || configuration.reward_amount > MAX_ACTIVITY_REWARD) return 'La recompensa debe estar entre 1 y 10000 Áureos.'
  if (configuration.timed ? (!Number.isInteger(configuration.duration_seconds) || configuration.duration_seconds < 1 || configuration.duration_seconds > MAX_ACTIVITY_DURATION_SECONDS) : configuration.duration_seconds !== 0) return 'La duración cronometrada debe estar entre 1 segundo y 999:59; sin tiempo debe ser cero.'
  if (!['ALL', 'SELECTED', 'PARTICIPANTS_DISABLED'].includes(configuration.participant_mode)) return 'El modo de participantes no es válido.'
  if (new Set(registeredIds).size !== registeredIds.length || registeredIds.some((id) => !Number.isInteger(id) || id <= 0)) return 'El padrón de alumnos no es válido.'
  if (configuration.participant_mode === 'PARTICIPANTS_DISABLED') return ''
  if (registeredIds.length === 0) return 'No hay alumnos registrados para esta selección.'
  if (configuration.participant_mode === 'ALL') return ''
  const selected = configuration.selected_student_ids
  if (selected.length === 0 || selected.some((id) => !registeredIds.includes(id))) return 'Selecciona uno o más alumnos registrados.'
  if (new Set(selected).size !== selected.length) return 'La selección contiene alumnos duplicados.'
  return ''
}

export function canEditDemoActivity(session: ActivitySession, claims: readonly ActivityClaim[]) {
  return session.status === 'ACTIVE' && !claims.some((claim) => claim.activity_id === session.id)
}

function cloneActivity(activity: ActivitySession): ActivitySession {
  return { ...activity, participant_student_ids: [...activity.participant_student_ids] }
}

const demoMovements: readonly MovementRecord[] = [
  { id: 7101, student_id: 201, amount: 120, type: 'ENTRY', reason: 'Reconocimiento de inicio', timestamp: Date.parse('2026-10-02T08:00:00Z') / 1000, origin: 'DEMO', synced: false, schema_version: 1 },
  { id: 7102, student_id: 202, amount: 65, type: 'ENTRY', reason: 'Participación en clase', timestamp: Date.parse('2026-10-03T10:30:00Z') / 1000, origin: 'DEMO', synced: false, schema_version: 1 },
  { id: 7103, student_id: 204, amount: 45, type: 'ENTRY', reason: 'Reto semanal', timestamp: Date.parse('2026-10-04T09:00:00Z') / 1000, origin: 'DEMO', synced: false, schema_version: 1 },
  { id: 7104, student_id: 203, amount: 45, type: 'ENTRY', reason: 'Reconocimiento de lectura', timestamp: Date.parse('2026-10-05T12:00:00Z') / 1000, origin: 'DEMO', synced: false, schema_version: 1 },
  { id: 7105, student_id: 205, amount: 35, type: 'ENTRY', reason: 'Saldo inicial de demostración', timestamp: Date.parse('2026-10-05T13:00:00Z') / 1000, origin: 'DEMO', synced: false, schema_version: 1 },
  { id: 7106, student_id: 201, amount: 15, type: 'ENTRY', reason: 'Actividad 2 · lectura', timestamp: Date.parse('2026-10-06T13:00:00Z') / 1000, origin: 'DEMO', synced: false, schema_version: 1 },
  { id: 7107, student_id: 202, amount: 15, type: 'ENTRY', reason: 'Actividad 2 · lectura', timestamp: Date.parse('2026-10-06T14:00:00Z') / 1000, origin: 'DEMO', synced: false, schema_version: 1 },
  { id: 7108, student_id: 201, amount: 10, type: 'ENTRY', reason: 'Actividad 4 · lectura', timestamp: Date.parse('2026-10-07T08:40:00Z') / 1000, origin: 'DEMO', synced: false, schema_version: 1 },
  { id: 7109, student_id: 201, amount: -20, type: 'EXIT', reason: 'Material de aula', timestamp: Date.parse('2026-10-07T09:15:00Z') / 1000, origin: 'DEMO', synced: false, schema_version: 1 },
  { id: 7110, student_id: 202, amount: -70, type: 'EXIT', reason: 'Compra previa ficticia para probar una anulación', timestamp: Date.parse('2026-10-07T09:30:00Z') / 1000, origin: 'DEMO', synced: false, schema_version: 1 },
]

const demoClaims: readonly ActivityClaim[] = [
  { id: 9101, activity_id: 8102, student_id: 201, reward_amount: 15, claimed_at: Date.parse('2026-10-06T13:00:00Z') / 1000, movement_id: 7106, status: 'PAID', void_movement_id: 0 },
  { id: 9102, activity_id: 8102, student_id: 202, reward_amount: 15, claimed_at: Date.parse('2026-10-06T14:00:00Z') / 1000, movement_id: 7107, status: 'PAID', void_movement_id: 0 },
  { id: 9103, activity_id: 8104, student_id: 201, reward_amount: 10, claimed_at: Date.parse('2026-10-07T08:40:00Z') / 1000, movement_id: 7108, status: 'PAID', void_movement_id: 0 },
]

export type DemoClaimOperations = {
  voidPaidClaim(claimId: number, reason?: string): Promise<{ claim: ActivityClaim; voidMovement: MovementRecord; balance: number }>
  authorizeRepeatClaim(claimId: number): Promise<DemoClaimAuthorization>
  simulateAuthorizedClaim(authorizationId: number, expectedRewardAmount: number): Promise<{ claim: ActivityClaim; movement: MovementRecord; balance: number }>
  getClaimAuthorizations(): Promise<readonly DemoClaimAuthorization[]>
  getClaimEvents(): Promise<readonly DemoClaimEvent[]>
}

export class DemoClaimOperationError extends Error {
  constructor(message: string, readonly code: 'NOT_FOUND' | 'INVALID_STATE' | 'INCONSISTENT' | 'INELIGIBLE' | 'INVALID_TIME' | 'OPERATION_FAILED') {
    super(message)
    this.name = 'DemoClaimOperationError'
  }
}

export type DemoMutationName = 'VOID_CLAIM' | 'AUTHORIZE_REPEAT' | 'SIMULATE_CLAIM' | 'MANUAL_ACCOUNT_ADJUSTMENT' | 'SAVE_ACADEMIC_RECORD' | 'SAVE_ATTENDANCE' | 'UPDATE_SCHOOL_RULES' | 'APPLY_SCHOOL_AUREOS' | 'CREATE_ACTIVITY' | 'UPDATE_ACTIVITY' | 'CLOSE_ACTIVITY'

export type DemoAccountAdjustment = { studentId: number; operation: 'ADD' | 'WITHDRAW'; amount: number; reason: string; expectedBalance: number; confirmationId: string; relatedSchoolRecordId?: number }
export class DemoAccountAdjustmentError extends Error {
  constructor(message: string, readonly code: 'NOT_FOUND' | 'INVALID_AMOUNT' | 'INVALID_BALANCE' | 'BALANCE_CHANGED' | 'DUPLICATE_CONFIRMATION' | 'INVALID_TIME' | 'OPERATION_FAILED', readonly currentAccount?: StudentAccount) {
    super(message)
    this.name = 'DemoAccountAdjustmentError'
  }
}

// Matches firmware MAX_SINGLE_CREDIT; movement itself is signed int32_t.
export const MAX_DEMO_MOVEMENT_AMOUNT = 10_000
export const MAX_DEMO_ACCOUNT_BALANCE = 1_000_000_000_000
function aspectLabelFor(level: ReturnType<typeof aspectLevel>) { return level === 'ADVANCED' ? 'Logrado' : level === 'STANDARD' ? 'En desarrollo' : 'Requiere apoyo' }

export class DemoSchoolError extends Error {
  constructor(message: string, readonly code: 'INVALID' | 'NOT_FOUND' | 'NO_ACCOUNT' | 'NOT_CONFIGURED' | 'NOT_ASSESSED' | 'DUPLICATE' | 'STALE_REVIEW' | 'ALREADY_MARKED' | 'NOT_TODAY' | 'OPERATION_FAILED', readonly currentBalance?: number) { super(message); this.name = 'DemoSchoolError' }
}

export type SchoolApplicationRequest = { recordType: 'ACADEMIC' | 'ATTENDANCE'; recordId: number; indicator: AcademicIndicator | 'ATTENDANCE'; expectedRecordVersion: number; expectedAcademicRulesVersion: number; expectedAttendanceRulesVersion: number; expectedBalance: number; expectedAmount: number; expectedSignature: string; confirmationId: string }
export type SchoolApplicationPreview = { record: AcademicRecord | AttendanceRecord; student: Student; account: StudentAccount | null; resultingBalance: number | null; indicator: AcademicIndicator | 'ATTENDANCE'; amount: number; resultLabel: string; signature: string; recordType: 'ACADEMIC' | 'ATTENDANCE'; academicRulesVersion: number; attendanceRulesVersion: number }

export class DemoPanelDataService implements PanelDataService, DemoActivityOperations, DemoClaimOperations {
  private students: Student[] = demoStudents.map((student) => ({ ...student }))
  private activities = demoActivities.map(cloneActivity)
  private accounts = demoAccounts.map((account) => ({ ...account }))
  private movements = demoMovements.map((movement) => ({ ...movement }))
  private claims = demoClaims.map((claim) => ({ ...claim }))
  private authorizations: DemoClaimAuthorization[] = []
  private claimEvents: DemoClaimEvent[] = []
  private nextActivityId = Math.max(...this.activities.map(({ id }) => id)) + 1
  private nextAuthorizationId = 1
  private nextClaimEventId = 1
  private readonly usedAdjustmentConfirmations = new Set<string>()
  private academicRules: AcademicRules = createDefaultAcademicRules()
  private attendanceRules: AttendanceRules = createDefaultAttendanceRules()
  private academicRuleVersions: AcademicRules[] = [createDefaultAcademicRules()]
  private attendanceRuleVersions: AttendanceRules[] = [createDefaultAttendanceRules()]
  private academicRecords: AcademicRecord[] = []
  private attendanceRecords: AttendanceRecord[] = []
  private schoolApplications: SchoolApplication[] = []
  private nextSchoolRecordId = 1
  private nextSchoolApplicationId = 1

  constructor(
    private readonly clock: () => number = () => Date.now(),
    private readonly beforeCommit: (operation: DemoMutationName, snapshot?: DemoPanelSnapshot) => void | Promise<void> = () => undefined,
  ) {}

  snapshot(patch: Partial<DemoPanelSnapshot> = {}): DemoPanelSnapshot {
    return {
      students: this.students.map((entry) => ({ ...entry })), accounts: this.accounts.map((entry) => ({ ...entry })), movements: this.movements.map((entry) => ({ ...entry })),
      activities: this.activities.map(cloneActivity), claims: this.claims.map((entry) => ({ ...entry })), authorizations: this.authorizations.map((entry) => ({ ...entry })),
      claimEvents: this.claimEvents.map((entry) => ({ ...entry })), academicRecords: this.academicRecords.map((entry) => structuredClone(entry)), attendanceRecords: this.attendanceRecords.map((entry) => ({ ...entry })),
      schoolApplications: this.schoolApplications.map((entry) => ({ ...entry })), academicRules: cloneAcademicRules(this.academicRules), attendanceRules: cloneAttendanceRules(this.attendanceRules),
      academicRuleVersions: this.academicRuleVersions.map(cloneAcademicRules), attendanceRuleVersions: this.attendanceRuleVersions.map(cloneAttendanceRules),
      usedAdjustmentConfirmations: [...this.usedAdjustmentConfirmations],
      counters: { activity: this.nextActivityId, authorization: this.nextAuthorizationId, claimEvent: this.nextClaimEventId, schoolRecord: this.nextSchoolRecordId, schoolApplication: this.nextSchoolApplicationId, movement: Math.max(0, ...this.movements.map(({ id }) => id)) + 1, claim: Math.max(0, ...this.claims.map(({ id }) => id)) + 1 },
      ...patch,
    }
  }

  restore(snapshot: DemoPanelSnapshot) {
    this.students = snapshot.students.map((entry) => ({ ...entry }))
    this.accounts = snapshot.accounts.map((entry) => ({ ...entry })); this.movements = snapshot.movements.map((entry) => ({ ...entry }));
    this.activities = snapshot.activities.map(cloneActivity); this.claims = snapshot.claims.map((entry) => ({ ...entry }));
    this.authorizations = snapshot.authorizations.map((entry) => ({ ...entry })); this.claimEvents = snapshot.claimEvents.map((entry) => ({ ...entry }));
    this.academicRecords = snapshot.academicRecords.map((entry) => structuredClone(entry)); this.attendanceRecords = snapshot.attendanceRecords.map((entry) => ({ ...entry }));
    this.schoolApplications = snapshot.schoolApplications.map((entry) => ({ ...entry })); this.academicRules = cloneAcademicRules(snapshot.academicRules); this.attendanceRules = cloneAttendanceRules(snapshot.attendanceRules);
    this.academicRuleVersions = snapshot.academicRuleVersions.map(cloneAcademicRules); this.attendanceRuleVersions = snapshot.attendanceRuleVersions.map(cloneAttendanceRules);
    this.usedAdjustmentConfirmations.clear(); for (const id of snapshot.usedAdjustmentConfirmations) this.usedAdjustmentConfirmations.add(id)
    this.nextActivityId = snapshot.counters.activity; this.nextAuthorizationId = snapshot.counters.authorization; this.nextClaimEventId = snapshot.counters.claimEvent; this.nextSchoolRecordId = snapshot.counters.schoolRecord; this.nextSchoolApplicationId = snapshot.counters.schoolApplication
  }

  private async commit(operation: DemoMutationName, patch: Partial<DemoPanelSnapshot> = {}) { await this.beforeCommit(operation, this.snapshot(patch)) }

  async getStudents() { return this.students.map((student) => ({ ...student })) }
  async getAccounts() { return this.accounts.map((account) => ({ ...account })) }
  async getMovements() { return this.movements.map((movement) => ({ ...movement })) }
  async getActivities() { return this.activities.map(cloneActivity) }
  async getClaims() { return this.claims.map((claim) => ({ ...claim })) }
  async getClaimAuthorizations() { return this.authorizations.map((authorization) => ({ ...authorization })) }
  async getClaimEvents() { return this.claimEvents.map((event) => ({ ...event })) }
  async getAcademicRecords() { return this.academicRecords.map((record) => structuredClone(record)) }
  async getAttendanceRecords() { return this.attendanceRecords.map((record) => ({ ...record })) }
  async getSchoolApplications() { return this.schoolApplications.map((application) => ({ ...application })) }
  async getSchoolRules() { return { academic: cloneAcademicRules(this.academicRules), attendance: cloneAttendanceRules(this.attendanceRules) } }

  async saveAcademicRecord(draft: AcademicDraft) {
    const error = validateAcademicDraft(draft, this.students, this.academicRules)
    if (error) throw new DemoSchoolError(error, 'INVALID')
    const now = Math.floor(this.clock() / 1000)
    const existingIndex = this.academicRecords.findIndex((record) => record.student_id === draft.student_id && record.school_date === draft.school_date && record.kind === draft.kind)
    const existing = existingIndex >= 0 ? this.academicRecords[existingIndex] : null
    const common = { id: existing?.id ?? this.nextSchoolRecordId, student_id: draft.student_id, school_date: draft.school_date, kind: draft.kind, version: (existing?.version ?? 0) + 1, recorded_at: now }
    const student = this.students.find(({ student_id }) => student_id === draft.student_id)!
    const record: AcademicRecord = draft.kind === 'READING' ? { ...common, kind: 'READING', grade: student.grade, ppm: draft.ppm! }
      : draft.kind === 'DICTATION' ? { ...common, kind: 'DICTATION', total_words: draft.total_words!, correct_words: draft.correct_words! }
        : { ...common, kind: 'COMPREHENSION', scores: { ...draft.scores! }, denominators: { ...this.academicRules.comprehension.denominators } }
    try { await this.commit('SAVE_ACADEMIC_RECORD', { academicRecords: existingIndex >= 0 ? this.academicRecords.map((entry, index) => index === existingIndex ? record : entry) : [...this.academicRecords, record], counters: { ...this.snapshot().counters, schoolRecord: existingIndex >= 0 ? this.nextSchoolRecordId : this.nextSchoolRecordId + 1 } }) } catch { throw new DemoSchoolError('No se guardó el registro; conserva el formulario e inténtalo de nuevo. Revisa el almacenamiento local.', 'OPERATION_FAILED') }
    if (existingIndex >= 0) this.academicRecords = this.academicRecords.map((entry, index) => index === existingIndex ? record : entry)
    else { this.academicRecords = [...this.academicRecords, record]; this.nextSchoolRecordId += 1 }
    return structuredClone(record)
  }

  async markDemoArrival(studentId: number) {
    if (!this.students.some((student) => student.student_id === studentId)) throw new DemoSchoolError('No se encontró el alumno.', 'NOT_FOUND')
    const nowMs = this.clock(); const date = schoolDateAt(nowMs); const now = Math.floor(nowMs / 1000)
    const existing = this.attendanceRecords.find((record) => record.student_id === studentId && record.school_date === date)
    if (existing?.arrival_at !== null && existing?.arrival_at !== undefined) throw new DemoSchoolError('La llegada ya está registrada y no se puede sobrescribir.', 'ALREADY_MARKED')
    const seconds = schoolClockSeconds(nowMs); const punctualSeconds = Number(this.attendanceRules.punctual_until.slice(0, 2)) * 3600 + Number(this.attendanceRules.punctual_until.slice(3)) * 60
    const record: AttendanceRecord = { id: existing?.id ?? this.nextSchoolRecordId, student_id: studentId, school_date: date, status: seconds <= punctualSeconds ? 'PRESENT' : 'LATE', arrival_at: now, justification: '', version: (existing?.version ?? 0) + 1, recorded_at: now }
    try { await this.commit('SAVE_ATTENDANCE', { attendanceRecords: existing ? this.attendanceRecords.map((entry) => entry.id === existing.id ? record : entry) : [...this.attendanceRecords, record], counters: { ...this.snapshot().counters, schoolRecord: existing ? this.nextSchoolRecordId : this.nextSchoolRecordId + 1 } }) } catch { throw new DemoSchoolError('No se registró la llegada; inténtalo de nuevo. Revisa el almacenamiento local.', 'OPERATION_FAILED') }
    if (existing) this.attendanceRecords = this.attendanceRecords.map((entry) => entry.id === existing.id ? record : entry)
    else { this.attendanceRecords = [...this.attendanceRecords, record]; this.nextSchoolRecordId += 1 }
    return { ...record }
  }

  async saveAttendanceStatus(studentId: number, schoolDate: string, status: AttendanceRecord['status'], justification = '') {
    if (!this.students.some((student) => student.student_id === studentId)) throw new DemoSchoolError('No se encontró el alumno.', 'NOT_FOUND')
    if (!validateSchoolDate(schoolDate)) throw new DemoSchoolError('La fecha escolar no es válida.', 'INVALID')
    if ((status === 'PRESENT' || status === 'LATE') && !this.attendanceRecords.some((record) => record.student_id === studentId && record.school_date === schoolDate && record.arrival_at !== null)) throw new DemoSchoolError('No se puede crear una hora de llegada para una fecha manualmente; registra la llegada en el día actual.', 'NOT_TODAY')
    const existingIndex = this.attendanceRecords.findIndex((record) => record.student_id === studentId && record.school_date === schoolDate)
    const existing = existingIndex >= 0 ? this.attendanceRecords[existingIndex] : null
    const now = Math.floor(this.clock() / 1000)
    const arrivalSeconds = existing?.arrival_at === null || existing?.arrival_at === undefined ? null : schoolClockSeconds(existing.arrival_at * 1000)
    const punctualSeconds = Number(this.attendanceRules.punctual_until.slice(0, 2)) * 3600 + Number(this.attendanceRules.punctual_until.slice(3)) * 60
    const resolvedStatus = status === 'PRESENT' || status === 'LATE' ? (arrivalSeconds! <= punctualSeconds ? 'PRESENT' : 'LATE') : status
    const record: AttendanceRecord = { id: existing?.id ?? this.nextSchoolRecordId, student_id: studentId, school_date: schoolDate, status: resolvedStatus, arrival_at: existing?.arrival_at ?? null, justification: resolvedStatus === 'ABSENT_JUSTIFIED' ? justification.trim() : '', version: (existing?.version ?? 0) + 1, recorded_at: now }
    try { await this.commit('SAVE_ATTENDANCE', { attendanceRecords: existingIndex >= 0 ? this.attendanceRecords.map((entry, index) => index === existingIndex ? record : entry) : [...this.attendanceRecords, record], counters: { ...this.snapshot().counters, schoolRecord: existingIndex >= 0 ? this.nextSchoolRecordId : this.nextSchoolRecordId + 1 } }) } catch { throw new DemoSchoolError('No se guardó la asistencia; conserva la selección e inténtalo de nuevo. Revisa el almacenamiento local.', 'OPERATION_FAILED') }
    if (existingIndex >= 0) this.attendanceRecords = this.attendanceRecords.map((entry, index) => index === existingIndex ? record : entry)
    else { this.attendanceRecords = [...this.attendanceRecords, record]; this.nextSchoolRecordId += 1 }
    return { ...record }
  }

  async updateSchoolRules(academic: AcademicRules, attendance: AttendanceRules, expectedAcademicVersion: number, expectedAttendanceVersion: number) {
    if (this.academicRules.version !== expectedAcademicVersion || this.attendanceRules.version !== expectedAttendanceVersion) throw new DemoSchoolError('La configuración cambió; vuelve a cargarla antes de confirmar.', 'STALE_REVIEW')
    const academicError = validateAcademicRules(academic); const attendanceError = validateAttendanceRules(attendance)
    if (academicError || attendanceError) throw new DemoSchoolError(academicError || attendanceError, 'INVALID')
    const nextAcademic = { ...cloneAcademicRules(academic), version: this.academicRules.version + 1 }
    const nextAttendance = { ...cloneAttendanceRules(attendance), version: this.attendanceRules.version + 1 }
    try { await this.commit('UPDATE_SCHOOL_RULES', { academicRules: nextAcademic, attendanceRules: nextAttendance, academicRuleVersions: [...this.academicRuleVersions, nextAcademic], attendanceRuleVersions: [...this.attendanceRuleVersions, nextAttendance] }) } catch { throw new DemoSchoolError('No se guardó la configuración; el borrador se conservó. Revisa el almacenamiento local.', 'OPERATION_FAILED') }
    this.academicRules = nextAcademic; this.attendanceRules = nextAttendance
    this.academicRuleVersions = [...this.academicRuleVersions, nextAcademic]; this.attendanceRuleVersions = [...this.attendanceRuleVersions, nextAttendance]
    return { academic: cloneAcademicRules(nextAcademic), attendance: cloneAttendanceRules(nextAttendance) }
  }

  async previewSchoolApplication(recordType: 'ACADEMIC' | 'ATTENDANCE', recordId: number, indicator: AcademicIndicator | 'ATTENDANCE'): Promise<SchoolApplicationPreview> {
    const record = recordType === 'ACADEMIC' ? this.academicRecords.find((entry) => entry.id === recordId) : this.attendanceRecords.find((entry) => entry.id === recordId)
    if (!record) throw new DemoSchoolError('No se encontró el registro que se desea aplicar.', 'NOT_FOUND')
    const student = this.students.find(({ student_id }) => student_id === record.student_id)
    if (!student) throw new DemoSchoolError('El registro no tiene un alumno válido.', 'NOT_FOUND')
    if (this.schoolApplications.some((application) => application.record_type === recordType && application.record_id === recordId && application.indicator === indicator)) throw new DemoSchoolError('Este registro e indicador ya tienen una aplicación; no se puede duplicar.', 'DUPLICATE')
    let amount: number; let resultLabel: string; let signature: string
    if (recordType === 'ATTENDANCE') {
      if (indicator !== 'ATTENDANCE') throw new DemoSchoolError('El indicador no corresponde a asistencia.', 'INVALID')
      const result = attendanceAmount(record as AttendanceRecord, this.attendanceRecords, this.attendanceRules)
      if (!result) throw new DemoSchoolError('La asistencia está sin registrar; no hay ajuste que aplicar.', 'NOT_ASSESSED')
      amount = result.amount; resultLabel = result.label; signature = result.signature
    } else {
      const academic = record as AcademicRecord; const result = academicResult(academic, this.academicRules)
      if (academic.kind === 'READING' && indicator === 'READING') {
        if (!result.level) throw new DemoSchoolError('No se pudo clasificar la lectura con el grado actual.', 'NOT_CONFIGURED')
        const configured = this.academicRules.readingAureos[result.level]; if (configured === null) throw new DemoSchoolError(`El importe para ${academicLevelLabels[result.level]} está Sin configurar.`, 'NOT_CONFIGURED')
        amount = configured; resultLabel = result.label; signature = result.signature
      } else if (academic.kind === 'DICTATION' && indicator === 'DICTATION') {
        if (!result.level) throw new DemoSchoolError('Configura primero los rangos porcentuales del dictado.', 'NOT_CONFIGURED')
        const configured = this.academicRules.dictation.aureos[result.level]; if (configured === null) throw new DemoSchoolError(`El importe para ${academicLevelLabels[result.level]} está Sin configurar.`, 'NOT_CONFIGURED')
        amount = configured; resultLabel = result.label; signature = result.signature
      } else if (academic.kind === 'COMPREHENSION') {
        const existingModeApplications = this.schoolApplications.filter((application) => application.record_type === 'ACADEMIC' && application.record_id === academic.id && application.indicator.startsWith('COMPREHENSION_'))
        const alreadyAppliedByAspect = existingModeApplications.some((application) => application.indicator !== 'COMPREHENSION_OVERALL')
        const alreadyAppliedOverall = existingModeApplications.some((application) => application.indicator === 'COMPREHENSION_OVERALL')
        if (this.academicRules.comprehension.rewardMode === 'ASPECTS' && indicator.startsWith('COMPREHENSION_') && indicator !== 'COMPREHENSION_OVERALL') {
          if (alreadyAppliedOverall) throw new DemoSchoolError('Este registro ya tuvo una aplicación por resultado general; no se puede cambiar a aplicación por aspectos.', 'DUPLICATE')
          const aspect = indicator.slice('COMPREHENSION_'.length) as ComprehensionAspect; const score = academic.scores[aspect]
          if (score === null) throw new DemoSchoolError(`El aspecto ${aspectLabels[aspect]} está sin evaluar.`, 'NOT_ASSESSED')
          const level = aspectLevel(score, academic.denominators[aspect]); const configured = this.academicRules.comprehension.aspectAureos[aspect][level]
          if (configured === null) throw new DemoSchoolError(`El importe de ${aspectLabels[aspect]} para este nivel está Sin configurar.`, 'NOT_CONFIGURED')
          amount = configured; resultLabel = `${aspectLabels[aspect]} · ${score}/${academic.denominators[aspect]} · ${aspectLabelFor(level)}`; signature = JSON.stringify([aspect, score, academic.denominators[aspect], level])
        } else if (this.academicRules.comprehension.rewardMode === 'OVERALL' && indicator === 'COMPREHENSION_OVERALL') {
          if (alreadyAppliedByAspect) throw new DemoSchoolError('Este registro ya tuvo aplicaciones por aspecto; no se puede aplicar también el resultado general.', 'DUPLICATE')
          if (result.percentage === null || !result.level) throw new DemoSchoolError('Configura pesos y rangos generales para calcular el resultado ponderado.', 'NOT_CONFIGURED')
          const configured = this.academicRules.comprehension.overall.aureos[result.level]; if (configured === null) throw new DemoSchoolError(`El importe general para ${academicLevelLabels[result.level]} está Sin configurar.`, 'NOT_CONFIGURED')
          amount = configured; resultLabel = result.label; signature = result.signature
        } else throw new DemoSchoolError('El indicador no corresponde a la modalidad monetaria configurada.', 'INVALID')
      } else throw new DemoSchoolError('El indicador no corresponde al tipo de evaluación.', 'INVALID')
    }
    const account = this.accounts.find(({ student_id }) => student_id === record.student_id) ?? null
    return { record: structuredClone(record), student, account: account ? { ...account } : null, resultingBalance: account ? account.balance + amount : null, indicator, amount, resultLabel, signature, recordType, academicRulesVersion: this.academicRules.version, attendanceRulesVersion: this.attendanceRules.version }
  }

  async applySchoolAureos(request: SchoolApplicationRequest) {
    const preview = await this.previewSchoolApplication(request.recordType, request.recordId, request.indicator)
    const record = preview.record
    if (!preview.account) throw new DemoSchoolError('El alumno no tiene cuenta; no se creó ninguna.', 'NO_ACCOUNT')
    if (record.version !== request.expectedRecordVersion || preview.academicRulesVersion !== request.expectedAcademicRulesVersion || preview.attendanceRulesVersion !== request.expectedAttendanceRulesVersion || preview.account.balance !== request.expectedBalance || preview.amount !== request.expectedAmount || preview.signature !== request.expectedSignature) throw new DemoSchoolError('El registro, la regla o el saldo cambiaron. Vuelve a revisar la propuesta actualizada.', 'STALE_REVIEW', preview.account.balance)
    if (!request.confirmationId) throw new DemoSchoolError('La confirmación no tiene identificador válido.', 'INVALID')
    const balanceAfter = preview.account.balance + preview.amount
    if (!Number.isSafeInteger(balanceAfter) || Math.abs(balanceAfter) > MAX_DEMO_ACCOUNT_BALANCE) throw new DemoSchoolError('El saldo resultante excede el límite de la cuenta de demostración.', 'INVALID')
    const timestamp = Math.floor(this.clock() / 1000)
    if (!Number.isSafeInteger(timestamp)) throw new DemoSchoolError('La hora de la computadora no es válida.', 'INVALID')
    const duplicate = this.schoolApplications.find((entry) => entry.record_type === request.recordType && entry.record_id === request.recordId && entry.indicator === request.indicator)
    if (duplicate) throw new DemoSchoolError('Este registro ya se aplicó y no se permite duplicarlo.', 'DUPLICATE')
    const movementId = preview.amount === 0 ? null : Math.max(0, ...this.movements.map(({ id }) => id)) + 1
    const movement: MovementRecord | null = movementId === null ? null : { id: movementId, student_id: record.student_id, amount: preview.amount, type: preview.amount > 0 ? 'ENTRY' : 'EXIT', reason: `Aplicación académica demo · ${preview.resultLabel}`, timestamp, origin: 'DEMO', synced: false, schema_version: 1 }
    const application: SchoolApplication = { id: this.nextSchoolApplicationId, student_id: record.student_id, record_type: request.recordType, record_id: record.id, indicator: request.indicator, record_version: record.version, academic_rules_version: this.academicRules.version, attendance_rules_version: this.attendanceRules.version, result_label: preview.resultLabel, signature: preview.signature, amount: preview.amount, balance_before: preview.account.balance, balance_after: balanceAfter, movement_id: movementId, created_at: timestamp }
    const updatedAccount = { ...preview.account, balance: balanceAfter }
    try { await this.commit('APPLY_SCHOOL_AUREOS', { accounts: preview.amount !== 0 ? this.accounts.map((entry) => entry.student_id === record.student_id ? updatedAccount : entry) : this.accounts, movements: movement ? [...this.movements, movement] : this.movements, schoolApplications: [...this.schoolApplications, application], counters: { ...this.snapshot().counters, schoolApplication: this.nextSchoolApplicationId + 1 } }) } catch { throw new DemoSchoolError('No se aplicó el ajuste; saldo e historial siguen sin cambios. Revisa el almacenamiento local.', 'OPERATION_FAILED') }
    if (preview.amount !== 0) { this.accounts = this.accounts.map((entry) => entry.student_id === record.student_id ? updatedAccount : entry); this.movements = [...this.movements, movement!] }
    this.schoolApplications = [...this.schoolApplications, application]; this.nextSchoolApplicationId += 1
    return { application: { ...application }, account: { ...updatedAccount }, movement: movement ? { ...movement } : null }
  }

  async adjustDemoAccount(input: DemoAccountAdjustment) {
    const account = this.accounts.find((entry) => entry.student_id === input.studentId)
    if (!account) throw new DemoAccountAdjustmentError('Este alumno no tiene cuenta; no se creó ninguna.', 'NOT_FOUND')
    if (!Number.isSafeInteger(input.amount) || input.amount <= 0 || input.amount > MAX_DEMO_MOVEMENT_AMOUNT
      || !['ADD', 'WITHDRAW'].includes(input.operation)) throw new DemoAccountAdjustmentError('La cantidad debe ser un entero positivo dentro del límite del movimiento.', 'INVALID_AMOUNT')
    if (!Number.isSafeInteger(input.expectedBalance) || !Number.isSafeInteger(account.balance) || Math.abs(account.balance) > MAX_DEMO_ACCOUNT_BALANCE) throw new DemoAccountAdjustmentError('El saldo no es un entero válido para esta operación.', 'INVALID_BALANCE')
    if (!input.confirmationId || this.usedAdjustmentConfirmations.has(input.confirmationId)) throw new DemoAccountAdjustmentError('Esta confirmación ya se procesó.', 'DUPLICATE_CONFIRMATION')
    if (account.balance !== input.expectedBalance) throw new DemoAccountAdjustmentError('El saldo cambió. Revisa el resultado actualizado antes de confirmar.', 'BALANCE_CHANGED', { ...account })
    const delta = input.operation === 'ADD' ? input.amount : -input.amount
    const nextBalance = account.balance + delta
    if (!Number.isSafeInteger(nextBalance) || Math.abs(nextBalance) > MAX_DEMO_ACCOUNT_BALANCE) throw new DemoAccountAdjustmentError('El saldo resultante excede el límite admitido por el modelo de cuenta.', 'INVALID_BALANCE')
    const timestamp = Math.floor(this.clock() / 1000)
    if (!Number.isSafeInteger(timestamp) || timestamp < MIN_VALID_ACTIVITY_EPOCH) throw new DemoAccountAdjustmentError('La hora de la computadora no es válida para registrar el ajuste.', 'INVALID_TIME')
    const movementId = Math.max(0, ...this.movements.map(({ id }) => id)) + 1
    if (!Number.isSafeInteger(movementId) || this.movements.some(({ id }) => id === movementId)) throw new DemoAccountAdjustmentError('No se pudo reservar un ID único para el movimiento.', 'INVALID_BALANCE')
    const cleanReason = input.reason.trim() || 'Sin motivo'
    const movement: MovementRecord = {
      id: movementId, student_id: input.studentId, amount: delta, type: delta > 0 ? 'ENTRY' : 'EXIT',
      reason: `Ajuste manual de demostración · ${input.operation === 'ADD' ? 'Agregar Áureos' : 'Retirar Áureos'} · ${cleanReason}`,
      timestamp, origin: 'DEMO', synced: false, schema_version: 1,
      ...(input.relatedSchoolRecordId ? { related_school_record_id: input.relatedSchoolRecordId } : {}),
    }
    const updatedAccount = { ...account, balance: nextBalance }
    try { await this.commit('MANUAL_ACCOUNT_ADJUSTMENT', { accounts: this.accounts.map((entry) => entry.student_id === input.studentId ? updatedAccount : entry), movements: [...this.movements, movement], usedAdjustmentConfirmations: [...this.usedAdjustmentConfirmations, input.confirmationId] }) }
    catch { throw new DemoAccountAdjustmentError('No se aplicó el ajuste. Revisa el formulario e inténtalo de nuevo.', 'OPERATION_FAILED') }
    // Staged values are committed together only after all validation and the hook succeed.
    this.accounts = this.accounts.map((entry) => entry.student_id === input.studentId ? updatedAccount : entry)
    this.movements = [...this.movements, movement]
    this.usedAdjustmentConfirmations.add(input.confirmationId)
    return { account: { ...updatedAccount }, movement: { ...movement } }
  }

  private timestampNow() {
    const timestamp = Math.floor(this.clock() / 1000)
    if (!Number.isSafeInteger(timestamp) || timestamp < MIN_VALID_ACTIVITY_EPOCH) throw new DemoClaimOperationError('La hora de la computadora no es válida para esta operación.', 'INVALID_TIME')
    return timestamp
  }

  private requireOperationalEligibility(activityId: number, studentId: number, now: number) {
    const activity = this.activities.find((entry) => entry.id === activityId)
    if (!activity) throw new DemoClaimOperationError('La actividad relacionada no existe; la operación quedó bloqueada.', 'INCONSISTENT')
    if (!this.students.some((student) => student.student_id === studentId)) throw new DemoClaimOperationError('El alumno relacionado no existe; la operación quedó bloqueada.', 'INCONSISTENT')
    if (!this.accounts.some((account) => account.student_id === studentId)) throw new DemoClaimOperationError('El alumno no tiene cuenta; no se puede autorizar ni registrar el cobro.', 'INELIGIBLE')
    if (activity.status !== 'ACTIVE') throw new DemoClaimOperationError('La actividad no está activa.', 'INELIGIBLE')
    if (activity.duration_seconds > 0 && now < activity.started_at) throw new DemoClaimOperationError('La actividad todavía no ha iniciado; no se puede verificar la elegibilidad.', 'INELIGIBLE')
    if (activity.duration_seconds > 0 && now >= activity.started_at + activity.duration_seconds) throw new DemoClaimOperationError('La actividad ya venció.', 'INELIGIBLE')
    if (activity.participant_mode !== 'PARTICIPANTS_DISABLED' && !activity.participant_student_ids.includes(studentId)) throw new DemoClaimOperationError('El alumno no pertenece a la selección guardada de participantes.', 'INELIGIBLE')
    return activity
  }

  private assertVoidedClaimReferences(source: ActivityClaim) {
    const original = this.movements.find((movement) => movement.id === source.movement_id)
    const inverse = this.movements.find((movement) => movement.id === source.void_movement_id)
    if (!original || !inverse || original.student_id !== source.student_id || original.type !== 'ENTRY'
      || original.amount !== source.reward_amount || original.timestamp !== source.claimed_at
      || inverse.student_id !== source.student_id || inverse.type !== 'EXIT' || inverse.amount !== -source.reward_amount
      || inverse.related_claim_id !== source.id || inverse.related_movement_id !== original.id) {
      throw new DemoClaimOperationError('Falta una referencia coherente del cobro anulado o su movimiento inverso.', 'INCONSISTENT')
    }
  }

  async voidPaidClaim(claimId: number, reason = '') {
    const current = this.claims.find((claim) => claim.id === claimId)
    if (!current) throw new DemoClaimOperationError('No se encontró el cobro.', 'NOT_FOUND')
    if (current.status !== 'PAID' || current.void_movement_id !== 0) throw new DemoClaimOperationError('Este cobro ya fue anulado o no puede anularse.', 'INVALID_STATE')
    const activity = this.activities.find((entry) => entry.id === current.activity_id)
    const originalMovement = this.movements.find((movement) => movement.id === current.movement_id)
    const account = this.accounts.find((entry) => entry.student_id === current.student_id)
    if (!activity || !this.students.some((student) => student.student_id === current.student_id) || !account || !originalMovement
      || originalMovement.student_id !== current.student_id || originalMovement.type !== 'ENTRY'
      || originalMovement.amount !== current.reward_amount || originalMovement.timestamp !== current.claimed_at) {
      throw new DemoClaimOperationError('Falta una cuenta o referencia coherente del cobro original; la operación quedó bloqueada.', 'INCONSISTENT')
    }
    const reasonText = reason.trim()
    if (reasonText.length > 240) throw new DemoClaimOperationError('El motivo no puede superar 240 caracteres.', 'INVALID_STATE')
    const occurredAt = this.timestampNow()
    const balance = account.balance - current.reward_amount
    if (!Number.isSafeInteger(balance)) throw new DemoClaimOperationError('El saldo resultante excede el rango entero seguro.', 'INVALID_STATE')
    const movementId = Math.max(0, ...this.movements.map(({ id }) => id)) + 1
    if (!Number.isSafeInteger(movementId) || this.movements.some((movement) => movement.id === movementId)) throw new DemoClaimOperationError('No se pudo reservar un ID único para el movimiento inverso.', 'INVALID_STATE')
    const voidMovement: MovementRecord = {
      id: movementId, student_id: current.student_id, amount: -current.reward_amount, type: 'EXIT',
      reason: `Anulación de demostración del cobro ${current.id}`, timestamp: occurredAt, origin: 'DEMO', synced: false, schema_version: 1,
      related_claim_id: current.id, related_movement_id: originalMovement.id,
    }
    const updatedClaim: ActivityClaim = { ...current, status: 'VOIDED', void_movement_id: movementId, voided_at: occurredAt, void_reason: reasonText, voided_by: 'PANEL_MAESTRO_DEMO' }
    const nextAccounts = this.accounts.map((entry) => entry.student_id === account.student_id ? { ...entry, balance } : entry)
    const nextMovements = [...this.movements, voidMovement]
    const nextClaims = this.claims.map((claim) => claim.id === current.id ? updatedClaim : claim)
    const nextEvent: DemoClaimEvent = { id: this.nextClaimEventId, claim_id: current.id, activity_id: current.activity_id, student_id: current.student_id, type: 'VOIDED', occurred_at: occurredAt, actor: 'PANEL_MAESTRO_DEMO', reason: reasonText }
    await this.commit('VOID_CLAIM', { accounts: nextAccounts, movements: nextMovements, claims: nextClaims, claimEvents: [...this.claimEvents, nextEvent], counters: { ...this.snapshot().counters, claimEvent: this.nextClaimEventId + 1 } })
    this.accounts = nextAccounts; this.movements = nextMovements; this.claims = nextClaims; this.claimEvents = [...this.claimEvents, nextEvent]; this.nextClaimEventId += 1
    return { claim: { ...updatedClaim }, voidMovement: { ...voidMovement }, balance }
  }

  async authorizeRepeatClaim(claimId: number) {
    const source = this.claims.find((claim) => claim.id === claimId)
    if (!source) throw new DemoClaimOperationError('No se encontró el cobro.', 'NOT_FOUND')
    if (source.status !== 'VOIDED' || source.void_movement_id === 0) throw new DemoClaimOperationError('Solo un cobro anulado con movimiento inverso puede iniciar una autorización separada.', 'INVALID_STATE')
    this.assertVoidedClaimReferences(source)
    if (this.authorizations.some((authorization) => authorization.source_claim_id === source.id)) throw new DemoClaimOperationError('Este cobro ya originó una autorización y no puede reutilizarse.', 'INVALID_STATE')
    if (this.claims.some((claim) => claim.activity_id === source.activity_id && claim.student_id === source.student_id && claim.status === 'PAID')) throw new DemoClaimOperationError('Ya existe un cobro vigente para este alumno y actividad.', 'INVALID_STATE')
    if (this.authorizations.some((authorization) => authorization.activity_id === source.activity_id && authorization.student_id === source.student_id && authorization.status === 'AUTHORIZED')) throw new DemoClaimOperationError('Ya existe una autorización sin consumir para este alumno y actividad.', 'INVALID_STATE')
    const authorizedAt = this.timestampNow()
    this.requireOperationalEligibility(source.activity_id, source.student_id, authorizedAt)
    const authorization: DemoClaimAuthorization = { id: this.nextAuthorizationId, activity_id: source.activity_id, student_id: source.student_id, source_claim_id: source.id, authorized_at: authorizedAt, status: 'AUTHORIZED' }
    const event: DemoClaimEvent = { id: this.nextClaimEventId, claim_id: source.id, activity_id: source.activity_id, student_id: source.student_id, type: 'REAUTHORIZED', occurred_at: authorizedAt, actor: 'PANEL_MAESTRO_DEMO', authorization_id: authorization.id }
    await this.commit('AUTHORIZE_REPEAT', { authorizations: [...this.authorizations, authorization], claimEvents: [...this.claimEvents, event], counters: { ...this.snapshot().counters, authorization: this.nextAuthorizationId + 1, claimEvent: this.nextClaimEventId + 1 } })
    this.authorizations = [...this.authorizations, authorization]; this.claimEvents = [...this.claimEvents, event]; this.nextAuthorizationId += 1; this.nextClaimEventId += 1
    return { ...authorization }
  }

  async simulateAuthorizedClaim(authorizationId: number, expectedRewardAmount: number) {
    const authorization = this.authorizations.find((entry) => entry.id === authorizationId)
    if (!authorization) throw new DemoClaimOperationError('No se encontró la autorización.', 'NOT_FOUND')
    if (authorization.status !== 'AUTHORIZED') throw new DemoClaimOperationError('La autorización ya fue consumida.', 'INVALID_STATE')
    const source = this.claims.find((claim) => claim.id === authorization.source_claim_id)
    const activity = this.requireOperationalEligibility(authorization.activity_id, authorization.student_id, this.timestampNow())
    if (!source || source.status !== 'VOIDED' || source.activity_id !== authorization.activity_id || source.student_id !== authorization.student_id) throw new DemoClaimOperationError('La referencia del cobro anulado no coincide con la autorización.', 'INCONSISTENT')
    this.assertVoidedClaimReferences(source)
    if (this.claims.some((claim) => claim.activity_id === authorization.activity_id && claim.student_id === authorization.student_id && claim.status === 'PAID')) throw new DemoClaimOperationError('Ya existe un cobro vigente para este alumno y actividad.', 'INVALID_STATE')
    const account = this.accounts.find((entry) => entry.student_id === authorization.student_id)
    if (!account) throw new DemoClaimOperationError('El alumno ya no tiene cuenta; no se generó ningún cambio.', 'INELIGIBLE')
    if (!Number.isSafeInteger(activity.reward_amount) || activity.reward_amount <= 0) throw new DemoClaimOperationError('La recompensa vigente no es válida.', 'INCONSISTENT')
    if (activity.reward_amount !== expectedRewardAmount) throw new DemoClaimOperationError('La recompensa cambió desde la confirmación; revisa el importe actual antes de volver a confirmar.', 'INVALID_STATE')
    const claimedAt = this.timestampNow()
    const nextBalance = account.balance + activity.reward_amount
    if (!Number.isSafeInteger(nextBalance)) throw new DemoClaimOperationError('El saldo resultante excede el rango entero seguro.', 'INVALID_STATE')
    const claimId = Math.max(0, ...this.claims.map(({ id }) => id)) + 1
    const movementId = Math.max(0, ...this.movements.map(({ id }) => id)) + 1
    if (!Number.isSafeInteger(claimId) || this.claims.some((claim) => claim.id === claimId) || !Number.isSafeInteger(movementId) || this.movements.some((movement) => movement.id === movementId)) throw new DemoClaimOperationError('No se pudo reservar un ID único para el nuevo cobro.', 'INVALID_STATE')
    const claim: ActivityClaim = { id: claimId, activity_id: activity.id, student_id: authorization.student_id, reward_amount: activity.reward_amount, claimed_at: claimedAt, movement_id: movementId, status: 'PAID', void_movement_id: 0, authorized_by_claim_id: source.id }
    const movement: MovementRecord = { id: movementId, student_id: authorization.student_id, amount: activity.reward_amount, type: 'ENTRY', reason: `Nuevo cobro autorizado · Actividad ${activity.id}`, timestamp: claimedAt, origin: 'DEMO', synced: false, schema_version: 1, related_claim_id: claim.id, authorized_by_claim_id: source.id }
    const consumed: DemoClaimAuthorization = { ...authorization, status: 'CONSUMED', consumed_at: claimedAt, consumed_claim_id: claim.id }
    const event: DemoClaimEvent = { id: this.nextClaimEventId, claim_id: claim.id, activity_id: activity.id, student_id: authorization.student_id, type: 'NEW_CLAIM', occurred_at: claimedAt, actor: 'PANEL_MAESTRO_DEMO', authorization_id: authorization.id, related_claim_id: source.id }
    const nextAccounts = this.accounts.map((entry) => entry.student_id === authorization.student_id ? { ...entry, balance: nextBalance } : entry)
    await this.commit('SIMULATE_CLAIM', { accounts: nextAccounts, movements: [...this.movements, movement], claims: [...this.claims, claim], authorizations: this.authorizations.map((entry) => entry.id === authorization.id ? consumed : entry), claimEvents: [...this.claimEvents, event], counters: { ...this.snapshot().counters, claimEvent: this.nextClaimEventId + 1 } })
    this.accounts = nextAccounts; this.movements = [...this.movements, movement]; this.claims = [...this.claims, claim]; this.authorizations = this.authorizations.map((entry) => entry.id === authorization.id ? consumed : entry); this.claimEvents = [...this.claimEvents, event]; this.nextClaimEventId += 1
    return { claim: { ...claim }, movement: { ...movement }, balance: nextBalance }
  }

  async createActivity(configuration: ActivityConfiguration) {
    const registeredIds = this.students.map(({ student_id }) => student_id)
    const validationError = validateDemoActivityConfiguration(configuration, registeredIds)
    if (validationError) throw new ActivityOperationError(validationError, 'INVALID_CONFIGURATION')
    const startedAt = Math.floor(this.clock() / 1000)
    if (!Number.isSafeInteger(startedAt) || startedAt < MIN_VALID_ACTIVITY_EPOCH) throw new ActivityOperationError('La hora de la computadora no es válida para iniciar la actividad.', 'INVALID_TIME')
    const id = this.nextActivityId
    if (!Number.isSafeInteger(id) || id <= 0 || this.activities.some((activity) => activity.id === id)) throw new ActivityOperationError('No se pudo reservar un ID único para la actividad.', 'INVALID_STATE')
    const participantIds = configuration.participant_mode === 'ALL' ? registeredIds
      : configuration.participant_mode === 'SELECTED' ? [...configuration.selected_student_ids] : []
    const created: ActivitySession = {
      id, activity_number_enabled: configuration.activity_number_enabled,
      activity_number: configuration.activity_number_enabled ? configuration.activity_number : 0,
      reward_amount: configuration.reward_amount, duration_seconds: configuration.timed ? configuration.duration_seconds : 0,
      participant_mode: configuration.participant_mode, participant_student_ids: participantIds,
      started_at: startedAt, status: 'ACTIVE', closed_at: 0,
    }
    await this.commit('CREATE_ACTIVITY' as DemoMutationName, { activities: [...this.activities, created], counters: { ...this.snapshot().counters, activity: id + 1 } })
    this.activities = [...this.activities, created]
    this.nextActivityId += 1
    return cloneActivity(created)
  }

  async updateActiveActivity(id: number, configuration: ActivityConfiguration) {
    const current = this.activities.find((activity) => activity.id === id)
    if (!current) throw new ActivityOperationError('No se encontró la actividad.', 'NOT_FOUND')
    if (current.status !== 'ACTIVE') throw new ActivityOperationError('Solo se pueden editar actividades activas.', 'INVALID_STATE')
    if (!canEditDemoActivity(current, this.claims)) throw new ActivityOperationError('La actividad ya tiene reclamos; no se puede editar.', 'HAS_CLAIMS')
    const registeredIds = this.students.map(({ student_id }) => student_id)
    const validationError = validateDemoActivityConfiguration(configuration, registeredIds)
    if (validationError) throw new ActivityOperationError(validationError, 'INVALID_CONFIGURATION')
    const updated: ActivitySession = {
      ...current,
      activity_number_enabled: configuration.activity_number_enabled,
      activity_number: configuration.activity_number_enabled ? configuration.activity_number : 0,
      reward_amount: configuration.reward_amount,
      duration_seconds: configuration.timed ? configuration.duration_seconds : 0,
      participant_mode: configuration.participant_mode,
      participant_student_ids: configuration.participant_mode === 'ALL' ? registeredIds
        : configuration.participant_mode === 'SELECTED' ? [...configuration.selected_student_ids] : [],
    }
    await this.commit('UPDATE_ACTIVITY' as DemoMutationName, { activities: this.activities.map((activity) => activity.id === id ? updated : activity) })
    this.activities = this.activities.map((activity) => activity.id === id ? updated : activity)
    return cloneActivity(updated)
  }

  async closeActiveActivity(id: number, status: 'FINISHED' | 'CANCELLED') {
    if (status !== 'FINISHED' && status !== 'CANCELLED') throw new ActivityOperationError('El estado de cierre no es válido.', 'INVALID_STATE')
    const current = this.activities.find((activity) => activity.id === id)
    if (!current) throw new ActivityOperationError('No se encontró la actividad.', 'NOT_FOUND')
    if (current.status !== 'ACTIVE') throw new ActivityOperationError('Solo se pueden cerrar actividades activas.', 'INVALID_STATE')
    const closedAt = Math.floor(this.clock() / 1000)
    if (!Number.isSafeInteger(closedAt) || closedAt < MIN_VALID_ACTIVITY_EPOCH || closedAt < current.started_at) throw new ActivityOperationError('La hora de cierre no es válida.', 'INVALID_TIME')
    const updated = { ...current, status, closed_at: closedAt }
    await this.commit('CLOSE_ACTIVITY' as DemoMutationName, { activities: this.activities.map((activity) => activity.id === id ? updated : activity) })
    this.activities = this.activities.map((activity) => activity.id === id ? updated : activity)
    return cloneActivity(updated)
  }
}
