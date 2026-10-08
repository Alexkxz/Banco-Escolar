import type { Student } from '../models/domain'

export const SCHOOL_TIME_ZONE = 'America/Mexico_City'
export const academicLevels = ['REQUIRES_SUPPORT', 'NEAR_STANDARD', 'STANDARD', 'ADVANCED'] as const
export type AcademicLevel = typeof academicLevels[number]
export const academicLevelLabels: Record<AcademicLevel, string> = { REQUIRES_SUPPORT: 'Requiere apoyo', NEAR_STANDARD: 'Se acerca al estándar', STANDARD: 'Estándar', ADVANCED: 'Avanzado' }
export type AcademicKind = 'READING' | 'DICTATION' | 'COMPREHENSION'
export type ComprehensionAspect = 'LITERAL' | 'INFERENTIAL' | 'CRITICAL'
export type AcademicIndicator = 'READING' | 'DICTATION' | 'COMPREHENSION_LITERAL' | 'COMPREHENSION_INFERENTIAL' | 'COMPREHENSION_CRITICAL' | 'COMPREHENSION_OVERALL'
export type AcademicBase = { id: number; student_id: number; school_date: string; kind: AcademicKind; version: number; recorded_at: number }
export type ReadingRecord = AcademicBase & { kind: 'READING'; grade: number; ppm: number }
export type DictationRecord = AcademicBase & { kind: 'DICTATION'; total_words: number; correct_words: number }
export type ComprehensionRecord = AcademicBase & { kind: 'COMPREHENSION'; scores: Record<ComprehensionAspect, number | null>; denominators: Record<ComprehensionAspect, number> }
export type AcademicRecord = ReadingRecord | DictationRecord | ComprehensionRecord
export type SchoolBandRules = { boundaries: [number, number, number] | null; aureos: Record<AcademicLevel, number | null> }
export type AcademicRules = {
  version: number
  reading: Record<number, [number, number, number]>
  dictation: SchoolBandRules
  comprehension: {
    denominators: Record<ComprehensionAspect, number>
    weights: Record<ComprehensionAspect, number> | null
    overall: SchoolBandRules
    rewardMode: 'ASPECTS' | 'OVERALL'
    aspectAureos: Record<ComprehensionAspect, Record<AcademicLevel, number | null>>
  }
  readingAureos: Record<AcademicLevel, number | null>
}
export type AttendanceStatus = 'PRESENT' | 'LATE' | 'ABSENT_UNJUSTIFIED' | 'ABSENT_JUSTIFIED'
export type AttendanceRecord = { id: number; student_id: number; school_date: string; status: AttendanceStatus; arrival_at: number | null; justification: string; version: number; recorded_at: number }
export type AttendanceRules = { version: number; punctual_until: string; grace_until: string; first_amount: number; late_grace_amount: number; late_after_amount: number; absent_unjustified_amount: number }
export type SchoolApplication = { id: number; student_id: number; record_type: 'ACADEMIC' | 'ATTENDANCE'; record_id: number; indicator: AcademicIndicator | 'ATTENDANCE'; record_version: number; academic_rules_version: number; attendance_rules_version: number; result_label: string; signature: string; amount: number; balance_before: number; balance_after: number; movement_id: number | null; created_at: number }
export type AcademicDraft = { student_id: number; school_date: string; kind: AcademicKind; ppm?: number; total_words?: number; correct_words?: number; scores?: Record<ComprehensionAspect, number | null> }
export const aspects: ComprehensionAspect[] = ['LITERAL', 'INFERENTIAL', 'CRITICAL']
export const aspectLabels: Record<ComprehensionAspect, string> = { LITERAL: 'Literal', INFERENTIAL: 'Inferencial', CRITICAL: 'Crítica' }

export function emptyBand(): SchoolBandRules { return { boundaries: null, aureos: { REQUIRES_SUPPORT: null, NEAR_STANDARD: null, STANDARD: null, ADVANCED: null } } }
export function createDefaultAcademicRules(): AcademicRules {
  const aspectAureos = Object.fromEntries(aspects.map((aspect) => [aspect, { ...emptyBand().aureos }])) as AcademicRules['comprehension']['aspectAureos']
  return {
    version: 1,
    reading: { 1: [15, 35, 60], 2: [35, 60, 85], 3: [60, 85, 100], 4: [85, 100, 115], 5: [100, 115, 125], 6: [115, 125, 135] },
    dictation: emptyBand(),
    comprehension: { denominators: { LITERAL: 3, INFERENTIAL: 3, CRITICAL: 3 }, weights: null, overall: emptyBand(), rewardMode: 'ASPECTS', aspectAureos },
    readingAureos: { ...emptyBand().aureos },
  }
}
export function createDefaultAttendanceRules(): AttendanceRules { return { version: 1, punctual_until: '08:00', grace_until: '08:10', first_amount: 5, late_grace_amount: -5, late_after_amount: -10, absent_unjustified_amount: -20 } }
export function cloneAcademicRules(rules: AcademicRules): AcademicRules { return structuredClone(rules) }
export function cloneAttendanceRules(rules: AttendanceRules): AttendanceRules { return { ...rules } }

export function validateSchoolDate(value: string) {
  const match = /^(\d{4})-(\d{2})-(\d{2})$/.exec(value)
  if (!match) return false
  const date = new Date(Date.UTC(Number(match[1]), Number(match[2]) - 1, Number(match[3])))
  return date.getUTCFullYear() === Number(match[1]) && date.getUTCMonth() === Number(match[2]) - 1 && date.getUTCDate() === Number(match[3])
}
export function schoolDateAt(timestampMs: number) { return new Intl.DateTimeFormat('en-CA', { timeZone: SCHOOL_TIME_ZONE, year: 'numeric', month: '2-digit', day: '2-digit' }).format(timestampMs) }
export function schoolClockSeconds(timestampMs: number) {
  const parts = Object.fromEntries(new Intl.DateTimeFormat('en-GB', { timeZone: SCHOOL_TIME_ZONE, hour: '2-digit', minute: '2-digit', second: '2-digit', hourCycle: 'h23' }).formatToParts(timestampMs).map(({ type, value }) => [type, value]))
  return Number(parts.hour) * 3600 + Number(parts.minute) * 60 + Number(parts.second)
}
function clockValue(value: string) { const match = /^(\d{2}):(\d{2})$/.exec(value); if (!match || Number(match[1]) > 23 || Number(match[2]) > 59) return null; return Number(match[1]) * 3600 + Number(match[2]) * 60 }
function validBoundaries(boundaries: [number, number, number] | null) { return boundaries === null || (boundaries.every((value) => Number.isFinite(value) && value > 0 && value < 100) && boundaries[0] < boundaries[1] && boundaries[1] < boundaries[2]) }
function validAureos(aureos: Record<AcademicLevel, number | null>) { return academicLevels.every((level) => aureos[level] === null || Number.isSafeInteger(aureos[level])) }
export function validateAcademicRules(rules: AcademicRules) {
  for (const grade of [1, 2, 3, 4, 5, 6]) { const values = rules.reading[grade]; if (!values || !values.every((value) => Number.isInteger(value) && value >= 0 && value <= 65535) || !(values[0] < values[1] && values[1] < values[2])) return `Los rangos de lectura de ${grade}° deben ser enteros crecientes entre 0 y 65,535 PPM.` }
  if (!validBoundaries(rules.dictation.boundaries) || !validAureos(rules.dictation.aureos) || !validAureos(rules.readingAureos)) return 'Los rangos de dictado o importes académicos no son válidos.'
  if (rules.comprehension.denominators && aspects.some((aspect) => !Number.isInteger(rules.comprehension.denominators[aspect]) || rules.comprehension.denominators[aspect] < 1 || rules.comprehension.denominators[aspect] > 100)) return 'Cada denominador debe ser un entero entre 1 y 100.'
  if (rules.comprehension.weights && (aspects.some((aspect) => !Number.isFinite(rules.comprehension.weights![aspect]) || rules.comprehension.weights![aspect] < 0 || rules.comprehension.weights![aspect] > 100) || Math.abs(aspects.reduce((sum, aspect) => sum + rules.comprehension.weights![aspect], 0) - 100) > 1e-8)) return 'Los pesos deben ser porcentajes entre 0 y 100 y sumar exactamente 100%.'
  if (!validBoundaries(rules.comprehension.overall.boundaries) || !validAureos(rules.comprehension.overall.aureos) || aspects.some((aspect) => !validAureos(rules.comprehension.aspectAureos[aspect]))) return 'Los rangos o importes de comprensión no son válidos.'
  return ''
}
export function validateAttendanceRules(rules: AttendanceRules) {
  const punctual = clockValue(rules.punctual_until); const grace = clockValue(rules.grace_until)
  if (punctual === null || grace === null || grace < punctual) return 'Configura horas válidas y un límite de retardo igual o posterior al horario puntual.'
  if (![rules.first_amount, rules.late_grace_amount, rules.late_after_amount, rules.absent_unjustified_amount].every(Number.isSafeInteger)) return 'Los importes de asistencia deben ser enteros.'
  return ''
}
export function validateAcademicDraft(draft: AcademicDraft, students: readonly Student[], rules: AcademicRules) {
  if (!students.some(({ student_id }) => student_id === draft.student_id)) return 'Selecciona un alumno registrado.'
  if (!validateSchoolDate(draft.school_date)) return 'Escribe una fecha escolar válida.'
  if (draft.kind === 'READING' && (!Number.isInteger(draft.ppm) || draft.ppm! < 0 || draft.ppm! > 65535)) return 'PPM debe ser un entero entre 0 y 65,535.'
  if (draft.kind === 'DICTATION' && (!Number.isInteger(draft.total_words) || draft.total_words! <= 0 || !Number.isInteger(draft.correct_words) || draft.correct_words! < 0 || draft.correct_words! > draft.total_words!)) return 'Escribe total mayor que cero y correctas entre cero y el total.'
  if (draft.kind === 'COMPREHENSION' && (!draft.scores || aspects.some((aspect) => draft.scores![aspect] !== null && (!Number.isInteger(draft.scores![aspect]) || draft.scores![aspect]! < 0 || draft.scores![aspect]! > rules.comprehension.denominators[aspect])))) return 'Cada aspecto evaluado debe tener aciertos entre cero y su denominador; deja vacío el aspecto sin evaluar.'
  return ''
}
export function classifyBand(value: number, boundaries: [number, number, number] | null): AcademicLevel | null {
  if (!Number.isFinite(value) || !boundaries) return null
  if (value < boundaries[0]) return 'REQUIRES_SUPPORT'; if (value < boundaries[1]) return 'NEAR_STANDARD'; if (value < boundaries[2]) return 'STANDARD'; return 'ADVANCED'
}
export function dictationPercentage(correct: number, total: number) { return total > 0 ? correct / total * 100 : null }
export function comprehensionTotal(scores: Record<ComprehensionAspect, number | null>, denominators: Record<ComprehensionAspect, number>) {
  const evaluated = aspects.filter((aspect) => scores[aspect] !== null)
  return { correct: evaluated.reduce((sum, aspect) => sum + scores[aspect]!, 0), questions: evaluated.reduce((sum, aspect) => sum + denominators[aspect], 0) }
}
export function readingLevel(grade: number, ppm: number, rules: AcademicRules) { const boundaries = rules.reading[grade]; return boundaries ? (ppm < boundaries[0] ? 'REQUIRES_SUPPORT' : ppm < boundaries[1] ? 'NEAR_STANDARD' : ppm < boundaries[2] ? 'STANDARD' : 'ADVANCED') as AcademicLevel : null }
export function aspectLevel(score: number, denominator: number): AcademicLevel { const percentage = score / denominator * 100; return percentage >= 100 ? 'ADVANCED' : percentage >= 2 / 3 * 100 ? 'STANDARD' : 'REQUIRES_SUPPORT' }
export function aspectLabel(score: number, denominator: number) { return score === denominator ? 'Logrado' : score >= Math.ceil(denominator * 2 / 3) ? 'En desarrollo' : 'Requiere apoyo' }
export function academicResult(record: AcademicRecord, rules: AcademicRules): { label: string; signature: string; total?: number; level?: AcademicLevel | null; percentage?: number | null; aspectLevels?: Partial<Record<ComprehensionAspect, AcademicLevel>> } {
  if (record.kind === 'READING') { const level = readingLevel(record.grade, record.ppm, rules); return { label: `${record.ppm} PPM · ${level ? academicLevelLabels[level] : 'Sin configurar'}`, signature: JSON.stringify([record.ppm, level]), level } }
  if (record.kind === 'DICTATION') { const percentage = dictationPercentage(record.correct_words, record.total_words)!; const level = classifyBand(percentage, rules.dictation.boundaries); return { label: `${percentage.toLocaleString('es-MX', { maximumFractionDigits: 2 })}% · ${level ? academicLevelLabels[level] : 'Rangos sin configurar'}`, signature: JSON.stringify([record.correct_words, record.total_words, level]), percentage, level } }
  const aspectsDone = aspects.filter((aspect) => record.scores[aspect] !== null)
  const { correct: total, questions: totalQuestions } = comprehensionTotal(record.scores, record.denominators)
  if (aspectsDone.length === 0) return { label: 'Sin evaluar', signature: JSON.stringify([record.scores, record.denominators]), total: 0, level: null, percentage: null, aspectLevels: {} }
  const percentageByAspect = Object.fromEntries(aspectsDone.map((aspect) => [aspect, record.scores[aspect]! / record.denominators[aspect] * 100])) as Record<ComprehensionAspect, number>
  const weights = rules.comprehension.weights
  const includedWeight = weights ? aspectsDone.reduce((sum, aspect) => sum + weights[aspect], 0) : 0
  const percentage = weights && includedWeight > 0 ? aspectsDone.reduce((sum, aspect) => sum + percentageByAspect[aspect] * weights[aspect], 0) / includedWeight : null
  const level = percentage === null ? null : classifyBand(percentage, rules.comprehension.overall.boundaries)
  const aspectLevels = Object.fromEntries(aspectsDone.map((aspect) => [aspect, aspectLevel(record.scores[aspect]!, record.denominators[aspect])])) as Partial<Record<ComprehensionAspect, AcademicLevel>>
  return { label: `${total}/${totalQuestions} aciertos${percentage === null ? ' · Resultado ponderado sin configurar' : ` · ${percentage.toLocaleString('es-MX', { maximumFractionDigits: 2 })}%`}`, signature: JSON.stringify([record.scores, record.denominators, percentage, aspectLevels, level]), total, level, percentage, aspectLevels }
}
export function attendanceAmount(record: AttendanceRecord, all: readonly AttendanceRecord[], rules: AttendanceRules): { amount: number; label: string; signature: string } | null {
  if (record.status === 'ABSENT_JUSTIFIED') return { amount: 0, label: 'Falta justificada · sin ajuste', signature: JSON.stringify([record.status, 0]) }
  if (record.status === 'ABSENT_UNJUSTIFIED') return { amount: rules.absent_unjustified_amount, label: 'Falta injustificada', signature: JSON.stringify([record.status, rules.absent_unjustified_amount]) }
  if (!record.arrival_at || !['PRESENT', 'LATE'].includes(record.status)) return null
  const seconds = schoolClockSeconds(record.arrival_at * 1000)
  const punctual = clockValue(rules.punctual_until)!; const grace = clockValue(rules.grace_until)!
  if (seconds <= punctual) {
    const punctualArrivals = all.filter((entry) => entry.school_date === record.school_date && (entry.status === 'PRESENT' || entry.status === 'LATE') && entry.arrival_at !== null && schoolClockSeconds(entry.arrival_at * 1000) <= punctual).map((entry) => entry.arrival_at!).filter((value) => value <= record.arrival_at!).sort((a, b) => a - b)
    const first = punctualArrivals.length ? punctualArrivals[0] : record.arrival_at
    const isFirstTie = Math.floor(record.arrival_at / 1) === Math.floor(first / 1) && punctualArrivals.some((value) => Math.floor(value) === Math.floor(first))
    const amount = isFirstTie ? rules.first_amount : 0
    return { amount, label: isFirstTie ? 'Primera llegada puntual (empate por segundo)' : 'Presente puntual', signature: JSON.stringify([record.status, record.arrival_at, amount, first]) }
  }
  const amount = seconds <= grace ? rules.late_grace_amount : rules.late_after_amount
  return { amount, label: seconds <= grace ? 'Retardo dentro del margen' : 'Retardo posterior al margen', signature: JSON.stringify([record.status, record.arrival_at, amount]) }
}
export function studentFor(students: readonly Student[], id: number) { return students.find(({ student_id }) => student_id === id) ?? null }
