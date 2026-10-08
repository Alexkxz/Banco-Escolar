import { describe, expect, it } from 'vitest'
import { DemoPanelDataService, type DemoMutationName } from '../src/services/DemoPanelDataService'
import { academicLevelLabels, academicResult, aspectLabel, classifyBand, createDefaultAcademicRules, createDefaultAttendanceRules, readingLevel, schoolDateAt, validateAcademicDraft, validateAcademicRules, validateAttendanceRules } from '../src/services/schoolDomain'
import { loadDashboardSnapshot } from '../src/services/dashboardSnapshot'
import { loadAccountDetail } from '../src/services/accountQueries'
import { loadMovementSnapshot } from '../src/services/movementQueries'

const utc = (date: string) => Date.parse(date)
const academicDraft = { student_id: 201, school_date: '2026-10-07', kind: 'READING' as const, ppm: 100 }
async function configuredReading(service: DemoPanelDataService) {
  const current = await service.getSchoolRules()
  const academic = { ...current.academic, readingAureos: { REQUIRES_SUPPORT: -2, NEAR_STANDARD: 0, STANDARD: 4, ADVANCED: 7 } }
  return service.updateSchoolRules(academic, current.attendance, current.academic.version, current.attendance.version)
}

describe('PM.8 reglas y registros académicos de demostración', () => {
  it('clasifica límites de lectura por grado y conserva rangos iniciales', () => {
    const rules = createDefaultAcademicRules()
    expect([14, 15, 34, 35, 59, 60].map((ppm) => ppm < rules.reading[1][0] ? 'REQUIRES_SUPPORT' : ppm < rules.reading[1][1] ? 'NEAR_STANDARD' : ppm < rules.reading[1][2] ? 'STANDARD' : 'ADVANCED')).toEqual(['REQUIRES_SUPPORT', 'NEAR_STANDARD', 'NEAR_STANDARD', 'STANDARD', 'STANDARD', 'ADVANCED'])
    expect(rules.reading[6]).toEqual([115, 125, 135])
    expect(readingLevel(1, 15, rules)).toBe('NEAR_STANDARD')
    expect(readingLevel(3, 100, rules)).toBe('ADVANCED')
    expect(readingLevel(6, 134, rules)).toBe('STANDARD')
    expect(academicLevelLabels.ADVANCED).toBe('Avanzado')
    expect(classifyBand(49.99, [50, 70, 90])).toBe('REQUIRES_SUPPORT')
    expect(classifyBand(50, [50, 70, 90])).toBe('NEAR_STANDARD')
  })

  it('valida dictado, aspectos no evaluados, rangos sin huecos y pesos', () => {
    const rules = createDefaultAcademicRules()
    expect(validateAcademicDraft({ student_id: 201, school_date: '2026-10-07', kind: 'DICTATION', total_words: 10, correct_words: 11 }, [{ student_id: 201 } as never], rules)).toContain('correctas')
    expect(validateAcademicDraft({ student_id: 201, school_date: '2026-10-07', kind: 'DICTATION', total_words: 10, correct_words: 0 }, [{ student_id: 201 } as never], rules)).toBe('')
    expect(academicResult({ id: 1, student_id: 201, school_date: '2026-10-07', kind: 'COMPREHENSION', version: 1, recorded_at: 0, scores: { LITERAL: 3, INFERENTIAL: null, CRITICAL: 1 }, denominators: { LITERAL: 3, INFERENTIAL: 3, CRITICAL: 3 } }, rules).total).toBe(4)
    expect(academicResult({ id: 2, student_id: 201, school_date: '2026-10-07', kind: 'COMPREHENSION', version: 1, recorded_at: 0, scores: { LITERAL: null, INFERENTIAL: null, CRITICAL: null }, denominators: { LITERAL: 3, INFERENTIAL: 3, CRITICAL: 3 } }, rules).label).toBe('Sin evaluar')
    rules.comprehension.weights = { LITERAL: 20, INFERENTIAL: 30, CRITICAL: 50 }; rules.comprehension.overall.boundaries = [25, 50, 75]
    expect(academicResult({ id: 3, student_id: 201, school_date: '2026-10-07', kind: 'COMPREHENSION', version: 1, recorded_at: 0, scores: { LITERAL: 3, INFERENTIAL: null, CRITICAL: null }, denominators: { LITERAL: 3, INFERENTIAL: 3, CRITICAL: 3 } }, rules).percentage).toBe(100)
    expect(aspectLabel(3, 3)).toBe('Logrado'); expect(aspectLabel(2, 3)).toBe('En desarrollo'); expect(aspectLabel(1, 3)).toBe('Requiere apoyo')
    rules.dictation.boundaries = [20, 70, 60]
    expect(validateAcademicRules(rules)).toContain('importes')
    rules.dictation.boundaries = [20, 60, 85]; rules.comprehension.weights = { LITERAL: 30, INFERENTIAL: 30, CRITICAL: 30 }
    expect(validateAcademicRules(rules)).toContain('sumar exactamente')
  })

  it('guarda y corrige por alumno/fecha/tipo y muestra Sin configurar hasta editar importes', async () => {
    const service = new DemoPanelDataService(() => utc('2026-10-07T14:00:00Z'))
    const first = await service.saveAcademicRecord(academicDraft)
    expect(first).toMatchObject({ id: 1, student_id: 201, grade: 3, ppm: 100, version: 1 })
    const corrected = await service.saveAcademicRecord({ ...academicDraft, ppm: 99 })
    expect(corrected).toMatchObject({ id: first.id, ppm: 99, version: 2 })
    await expect(service.previewSchoolApplication('ACADEMIC', first.id, 'READING')).rejects.toMatchObject({ code: 'NOT_CONFIGURED' })
    await configuredReading(service)
    expect((await service.previewSchoolApplication('ACADEMIC', first.id, 'READING')).amount).toBe(4)
  })

  it('rechaza aplicación sin cuenta, revalida corrección, conserva la anterior y bloquea duplicados', async () => {
    const service = new DemoPanelDataService(() => utc('2026-10-07T14:00:00Z'))
    const record = await service.saveAcademicRecord({ ...academicDraft, student_id: 206 })
    await configuredReading(service)
    const preview = await service.previewSchoolApplication('ACADEMIC', record.id, 'READING')
    await expect(service.applySchoolAureos({ recordType: 'ACADEMIC', recordId: record.id, indicator: 'READING', expectedRecordVersion: preview.record.version, expectedAcademicRulesVersion: preview.academicRulesVersion, expectedAttendanceRulesVersion: preview.attendanceRulesVersion, expectedBalance: 0, expectedAmount: preview.amount, expectedSignature: preview.signature, confirmationId: 'no-account' })).rejects.toMatchObject({ code: 'NO_ACCOUNT' })

    const withAccount = await new DemoPanelDataService(() => utc('2026-10-07T14:00:00Z'))
    const saved = await withAccount.saveAcademicRecord(academicDraft); await configuredReading(withAccount)
    const stale = await withAccount.previewSchoolApplication('ACADEMIC', saved.id, 'READING')
    await withAccount.saveAcademicRecord({ ...academicDraft, ppm: 90 })
    const staleRequest = { recordType: 'ACADEMIC' as const, recordId: saved.id, indicator: 'READING' as const, expectedRecordVersion: stale.record.version, expectedAcademicRulesVersion: stale.academicRulesVersion, expectedAttendanceRulesVersion: stale.attendanceRulesVersion, expectedBalance: stale.account!.balance, expectedAmount: stale.amount, expectedSignature: stale.signature, confirmationId: 'stale' }
    await expect(withAccount.applySchoolAureos(staleRequest)).rejects.toMatchObject({ code: 'STALE_REVIEW' })
    const current = await withAccount.previewSchoolApplication('ACADEMIC', saved.id, 'READING')
    const request = { ...staleRequest, expectedRecordVersion: current.record.version, expectedAcademicRulesVersion: current.academicRulesVersion, expectedAttendanceRulesVersion: current.attendanceRulesVersion, expectedBalance: current.account!.balance, expectedAmount: current.amount, expectedSignature: current.signature, confirmationId: 'apply-once' }
    const before = await withAccount.getMovements()
    const applied = await withAccount.applySchoolAureos(request)
    expect(applied).toMatchObject({ application: { record_version: 2, movement_id: expect.any(Number) }, account: { balance: 129 }, movement: { amount: 4, origin: 'DEMO' } })
    await expect(withAccount.applySchoolAureos({ ...request, confirmationId: 'second' })).rejects.toMatchObject({ code: 'DUPLICATE' })
    expect(await withAccount.getMovements()).toHaveLength(before.length + 1)
    expect((await withAccount.getClaims())).toHaveLength(3)
  })

  it('no combina las modalidades monetarias de comprensión al cambiar reglas después de una aplicación', async () => {
    const service = new DemoPanelDataService(() => utc('2026-10-07T14:00:00Z'))
    const record = await service.saveAcademicRecord({ student_id: 201, school_date: '2026-10-07', kind: 'COMPREHENSION', scores: { LITERAL: 3, INFERENTIAL: 2, CRITICAL: 1 } })
    const initial = await service.getSchoolRules()
    const academic = { ...initial.academic, comprehension: { ...initial.academic.comprehension, weights: { LITERAL: 40, INFERENTIAL: 30, CRITICAL: 30 }, overall: { boundaries: [25, 50, 75] as [number, number, number], aureos: { REQUIRES_SUPPORT: 1, NEAR_STANDARD: 2, STANDARD: 3, ADVANCED: 4 } }, aspectAureos: { LITERAL: { REQUIRES_SUPPORT: 1, NEAR_STANDARD: null, STANDARD: 2, ADVANCED: 3 }, INFERENTIAL: { REQUIRES_SUPPORT: 1, NEAR_STANDARD: null, STANDARD: 2, ADVANCED: 3 }, CRITICAL: { REQUIRES_SUPPORT: 1, NEAR_STANDARD: null, STANDARD: 2, ADVANCED: 3 } } } }
    await service.updateSchoolRules(academic, initial.attendance, initial.academic.version, initial.attendance.version)
    const preview = await service.previewSchoolApplication('ACADEMIC', record.id, 'COMPREHENSION_LITERAL')
    await service.applySchoolAureos({ recordType: 'ACADEMIC', recordId: record.id, indicator: 'COMPREHENSION_LITERAL', expectedRecordVersion: record.version, expectedAcademicRulesVersion: preview.academicRulesVersion, expectedAttendanceRulesVersion: preview.attendanceRulesVersion, expectedBalance: preview.account!.balance, expectedAmount: preview.amount, expectedSignature: preview.signature, confirmationId: 'aspect-once' })
    const updated = await service.getSchoolRules()
    await service.updateSchoolRules({ ...updated.academic, comprehension: { ...updated.academic.comprehension, rewardMode: 'OVERALL' } }, updated.attendance, updated.academic.version, updated.attendance.version)
    await expect(service.previewSchoolApplication('ACADEMIC', record.id, 'COMPREHENSION_OVERALL')).rejects.toMatchObject({ code: 'DUPLICATE' })
    expect(await service.getSchoolApplications()).toHaveLength(1)
  })

  it('registra una aplicación de cero sin movimiento y revierte nada cuando falla el commit', async () => {
    let fail: DemoMutationName | '' = ''
    const service = new DemoPanelDataService(() => utc('2026-10-07T14:00:00Z'), (operation) => { if (operation === fail) throw new Error('test') })
    const record = await service.saveAcademicRecord(academicDraft); const current = await service.getSchoolRules()
    const edited = { ...current.academic, readingAureos: { REQUIRES_SUPPORT: 0, NEAR_STANDARD: 0, STANDARD: 0, ADVANCED: 0 } }
    await service.updateSchoolRules(edited, current.attendance, current.academic.version, current.attendance.version)
    const preview = await service.previewSchoolApplication('ACADEMIC', record.id, 'READING')
    const request = { recordType: 'ACADEMIC' as const, recordId: record.id, indicator: 'READING' as const, expectedRecordVersion: record.version, expectedAcademicRulesVersion: preview.academicRulesVersion, expectedAttendanceRulesVersion: preview.attendanceRulesVersion, expectedBalance: preview.account!.balance, expectedAmount: 0, expectedSignature: preview.signature, confirmationId: 'zero-app' }
    const count = (await service.getMovements()).length
    fail = 'APPLY_SCHOOL_AUREOS'
    await expect(service.applySchoolAureos(request)).rejects.toMatchObject({ code: 'OPERATION_FAILED' })
    expect((await service.getAccounts()).find(({ student_id }) => student_id === 201)?.balance).toBe(125)
    expect(await service.getMovements()).toHaveLength(count); expect(await service.getSchoolApplications()).toHaveLength(0)
    fail = ''
    const result = await service.applySchoolAureos(request)
    expect(result).toMatchObject({ application: { amount: 0, movement_id: null, balance_after: 125 }, movement: null })
    expect(await service.getMovements()).toHaveLength(count)
  })

  it('no confirma reglas inválidas o cambios de configuración con fallo de escritura', async () => {
    let fail: DemoMutationName | '' = ''
    const service = new DemoPanelDataService(() => utc('2026-10-07T14:00:00Z'), (operation) => { if (operation === fail) throw new Error('test') })
    const before = await service.getSchoolRules()
    const invalid = { ...before.academic, comprehension: { ...before.academic.comprehension, weights: { LITERAL: 33, INFERENTIAL: 33, CRITICAL: 33 } } }
    await expect(service.updateSchoolRules(invalid, before.attendance, before.academic.version, before.attendance.version)).rejects.toMatchObject({ code: 'INVALID' })
    fail = 'UPDATE_SCHOOL_RULES'
    await expect(service.updateSchoolRules(before.academic, before.attendance, before.academic.version, before.attendance.version)).rejects.toMatchObject({ code: 'OPERATION_FAILED' })
    expect(await service.getSchoolRules()).toEqual(before)
    fail = ''
    const saved = await service.updateSchoolRules(before.academic, before.attendance, before.academic.version, before.attendance.version)
    expect(saved.academic.version).toBe(before.academic.version + 1)
  })
})

describe('PM.8 asistencia de demostración', () => {
  it('usa fecha local Mexico City, empata por segundo y clasifica 08:00, 08:10 y después', async () => {
    expect(schoolDateAt(utc('2026-10-07T14:00:00Z'))).toBe('2026-10-07')
    const exact = new DemoPanelDataService(() => utc('2026-10-07T14:00:00Z'))
    const a = await exact.markDemoArrival(201); const b = await exact.markDemoArrival(202)
    expect(a.status).toBe('PRESENT'); expect(b.status).toBe('PRESENT')
    expect((await exact.previewSchoolApplication('ATTENDANCE', a.id, 'ATTENDANCE')).amount).toBe(5)
    expect((await exact.previewSchoolApplication('ATTENDANCE', b.id, 'ATTENDANCE')).amount).toBe(5)
    await expect(exact.markDemoArrival(201)).rejects.toMatchObject({ code: 'ALREADY_MARKED' })
    const atGrace = new DemoPanelDataService(() => utc('2026-10-07T14:10:00Z')); const late = await atGrace.markDemoArrival(203)
    expect(late.status).toBe('LATE'); expect((await atGrace.previewSchoolApplication('ATTENDANCE', late.id, 'ATTENDANCE')).amount).toBe(-5)
    const pastGrace = new DemoPanelDataService(() => utc('2026-10-07T14:10:01Z')); const later = await pastGrace.markDemoArrival(204)
    expect((await pastGrace.previewSchoolApplication('ATTENDANCE', later.id, 'ATTENDANCE')).amount).toBe(-10)
  })

  it('da ajuste cero a otra llegada puntual y no premia a quien llega tarde primero', async () => {
    let instant = utc('2026-10-07T13:50:00Z')
    const service = new DemoPanelDataService(() => instant)
    const first = await service.markDemoArrival(201)
    instant = utc('2026-10-07T13:59:59Z')
    const next = await service.markDemoArrival(202)
    expect((await service.previewSchoolApplication('ATTENDANCE', first.id, 'ATTENDANCE')).amount).toBe(5)
    expect((await service.previewSchoolApplication('ATTENDANCE', next.id, 'ATTENDANCE')).amount).toBe(0)
    const lateFirst = new DemoPanelDataService(() => utc('2026-10-07T14:01:00Z'))
    const late = await lateFirst.markDemoArrival(203)
    expect((await lateFirst.previewSchoolApplication('ATTENDANCE', late.id, 'ATTENDANCE')).amount).toBe(-5)
  })

  it('revalida saldo actual al aplicar y mantiene intactas las referencias escolares en el ajuste PM.7A', async () => {
    const service = new DemoPanelDataService(() => utc('2026-10-07T14:00:00Z'))
    const record = await service.saveAcademicRecord(academicDraft); await configuredReading(service)
    const preview = await service.previewSchoolApplication('ACADEMIC', record.id, 'READING')
    await service.adjustDemoAccount({ studentId: 201, operation: 'ADD', amount: 2, reason: 'Ajuste de prueba', expectedBalance: 125, confirmationId: 'concurrent-balance', relatedSchoolRecordId: record.id })
    await expect(service.applySchoolAureos({ recordType: 'ACADEMIC', recordId: record.id, indicator: 'READING', expectedRecordVersion: preview.record.version, expectedAcademicRulesVersion: preview.academicRulesVersion, expectedAttendanceRulesVersion: preview.attendanceRulesVersion, expectedBalance: preview.account!.balance, expectedAmount: preview.amount, expectedSignature: preview.signature, confirmationId: 'stale-balance' })).rejects.toMatchObject({ code: 'STALE_REVIEW', currentBalance: 127 })
    expect((await service.getMovements()).at(-1)).toMatchObject({ related_school_record_id: record.id, origin: 'DEMO' })
    expect(await service.getSchoolApplications()).toHaveLength(0)
  })

  it('mantiene Sin registrar distinto de falta, guarda faltas explícitas y no fabrica hora histórica', async () => {
    const service = new DemoPanelDataService(() => utc('2026-10-07T14:00:00Z'))
    expect(await service.getAttendanceRecords()).toHaveLength(0)
    const unjustified = await service.saveAttendanceStatus(201, '2026-10-06', 'ABSENT_UNJUSTIFIED')
    expect(unjustified.arrival_at).toBeNull(); expect((await service.previewSchoolApplication('ATTENDANCE', unjustified.id, 'ATTENDANCE')).amount).toBe(-20)
    const justified = await service.saveAttendanceStatus(202, '2026-10-06', 'ABSENT_JUSTIFIED', 'Aviso familiar')
    expect((await service.previewSchoolApplication('ATTENDANCE', justified.id, 'ATTENDANCE')).amount).toBe(0)
    await expect(service.saveAttendanceStatus(203, '2026-10-06', 'PRESENT')).rejects.toMatchObject({ code: 'NOT_TODAY' })
    expect(validateAttendanceRules(createDefaultAttendanceRules())).toBe('')
  })

  it('aplica faltas con saldo negativo, conserva historial y refleja los módulos; corrección posterior no vuelve a cobrar', async () => {
    const service = new DemoPanelDataService(() => utc('2026-10-07T14:00:00Z'))
    const absent = await service.saveAttendanceStatus(202, '2026-10-07', 'ABSENT_UNJUSTIFIED')
    const preview = await service.previewSchoolApplication('ATTENDANCE', absent.id, 'ATTENDANCE')
    const application = await service.applySchoolAureos({ recordType: 'ATTENDANCE', recordId: absent.id, indicator: 'ATTENDANCE', expectedRecordVersion: absent.version, expectedAcademicRulesVersion: preview.academicRulesVersion, expectedAttendanceRulesVersion: preview.attendanceRulesVersion, expectedBalance: preview.account!.balance, expectedAmount: preview.amount, expectedSignature: preview.signature, confirmationId: 'attendance-application' })
    expect(application.account.balance).toBe(-10)
    const movementCount = (await service.getMovements()).length
    const corrected = await service.saveAttendanceStatus(202, '2026-10-07', 'ABSENT_JUSTIFIED', 'Justificada')
    expect(corrected.version).toBe(2)
    await expect(service.previewSchoolApplication('ATTENDANCE', corrected.id, 'ATTENDANCE')).rejects.toMatchObject({ code: 'DUPLICATE' })
    expect((await service.getSchoolApplications())[0].record_version).toBe(1)
    expect(await service.getMovements()).toHaveLength(movementCount)
    expect((await loadAccountDetail(service, 202))?.account.balance).toBe(-10)
    expect((await loadMovementSnapshot(service)).entries[0].movement.amount).toBe(-20)
    expect((await loadDashboardSnapshot(service)).accountAureos).toBe(240)
  })
})
