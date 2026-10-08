import { useEffect, useMemo, useRef, useState } from 'react'
import { Link } from 'react-router-dom'
import { BookOpenCheck } from 'lucide-react'
import { Button, Card, EmptyState, PageHeader } from '../components/ui'
import { SchoolApplicationReview } from '../components/SchoolApplicationReview'
import { DemoPanelDataService } from '../services/DemoPanelDataService'
import { demoPanelService } from '../services/demoPanelService'
import { academicLevelLabels, academicResult, aspectLabel, aspectLevel, aspects, aspectLabels, comprehensionTotal, dictationPercentage, readingLevel, schoolDateAt, type AcademicDraft, type AcademicKind, type AcademicRecord, type AcademicRules, type ComprehensionAspect, type SchoolApplication } from '../services/schoolDomain'
import { movementDateFormatter } from '../services/movementQueries'
import type { Student } from '../models/domain'
import { AcademicAnalytics, type AcademicFilters, type AcademicTab } from './AcademicAnalytics'

type ViewState = { status: 'loading' } | { status: 'error' } | { status: 'ready'; students: readonly Student[]; records: AcademicRecord[]; applications: SchoolApplication[]; rules: AcademicRules }
type DraftInputs = { ppm: string; total: string; correct: string; literal: string; inferential: string; critical: string }
const blankInputs = (): DraftInputs => ({ ppm: '', total: '', correct: '', literal: '', inferential: '', critical: '' })
const draftKey = (studentId: number, date: string, kind: AcademicKind) => `${studentId}:${date}:${kind}`
const today = () => schoolDateAt(Date.now())
const blankDraft = (studentId: number, date = today(), kind: AcademicKind = 'READING'): AcademicDraft => kind === 'COMPREHENSION' ? { student_id: studentId, school_date: date, kind, scores: { LITERAL: null, INFERENTIAL: null, CRITICAL: null } } : { student_id: studentId, school_date: date, kind }

export function AcademicProgressPage({ service = demoPanelService as DemoPanelDataService }: { service?: DemoPanelDataService }) {
  const [state, setState] = useState<ViewState>({ status: 'loading' }); const [attempt, setAttempt] = useState(0)
  const [tab, setTab] = useState<AcademicTab>('summary'); const [gradeFilter, setGradeFilter] = useState(''); const [groupFilter, setGroupFilter] = useState(''); const [mode, setMode] = useState<'group' | 'student'>('group'); const [chartStudentId, setChartStudentId] = useState(201); const [from, setFrom] = useState(''); const [through, setThrough] = useState('')
  const [draft, setDraft] = useState<AcademicDraft>(blankDraft(201)); const [selectedStudent, setSelectedStudent] = useState(201); const [selectedDate, setSelectedDate] = useState(today()); const [kind, setKind] = useState<AcademicKind>('READING')
  const [draftValues, setDraftValues] = useState<DraftInputs>(blankInputs); const draftCache = useRef(new Map<string, { values: DraftInputs; draft: AcademicDraft }>()); const [editorOpen, setEditorOpen] = useState(false); const [message, setMessage] = useState(''); const [error, setError] = useState(''); const [busy, setBusy] = useState(false)
  const reload = () => setAttempt((value) => value + 1)
  useEffect(() => { let live = true; setState({ status: 'loading' }); Promise.all([service.getStudents(), service.getAcademicRecords(), service.getSchoolApplications(), service.getSchoolRules()]).then(([students, records, applications, rules]) => { if (live) setState({ status: 'ready', students, records, applications, rules: rules.academic }) }).catch(() => { if (live) setState({ status: 'error' }) }); return () => { live = false } }, [service, attempt])
  useEffect(() => {
    if (state.status !== 'ready') return
    const cached = draftCache.current.get(draftKey(selectedStudent, selectedDate, kind))
    if (cached) { setDraft(cached.draft); setDraftValues(cached.values); return }
    setDraft(blankDraft(selectedStudent, selectedDate, kind))
    setDraftValues(blankInputs())
    const existing = state.records.find((record) => record.student_id === selectedStudent && record.school_date === selectedDate && record.kind === kind)
    if (!existing) return
    setDraft(existing.kind === 'READING' ? { student_id: existing.student_id, school_date: existing.school_date, kind, ppm: existing.ppm }
      : existing.kind === 'DICTATION' ? { student_id: existing.student_id, school_date: existing.school_date, kind, total_words: existing.total_words, correct_words: existing.correct_words }
        : { student_id: existing.student_id, school_date: existing.school_date, kind, scores: { ...existing.scores } })
    if (existing.kind === 'READING') setDraftValues((value) => ({ ...value, ppm: String(existing.ppm) }))
    if (existing.kind === 'DICTATION') setDraftValues((value) => ({ ...value, total: String(existing.total_words), correct: String(existing.correct_words) }))
    if (existing.kind === 'COMPREHENSION') setDraftValues((value) => ({ ...value, ...Object.fromEntries(aspects.map((aspect) => [aspect.toLowerCase(), existing.scores[aspect] === null ? '' : String(existing.scores[aspect])])) }))
  }, [state, selectedStudent, selectedDate, kind])
  useEffect(() => { if (state.status !== 'ready' || mode !== 'student') return; const available = state.students.filter((student) => (!gradeFilter || String(student.grade) === gradeFilter) && (!groupFilter || student.group === groupFilter)); if (available.length && !available.some(({ student_id }) => student_id === chartStudentId)) setChartStudentId(available[0].student_id) }, [state, mode, gradeFilter, groupFilter, chartStudentId])
  const filtered = useMemo(() => state.status !== 'ready' ? [] : state.records.filter((record) => {
    const student = state.students.find((entry) => entry.student_id === record.student_id)
    const tabKind = tab === 'reading' ? 'READING' : tab === 'dictation' ? 'DICTATION' : tab === 'comprehension' ? 'COMPREHENSION' : null
    return (!tabKind || record.kind === tabKind) && (!gradeFilter || String(student?.grade) === gradeFilter) && (!groupFilter || student?.group === groupFilter) && (mode !== 'student' || record.student_id === chartStudentId) && (!from || record.school_date >= from) && (!through || record.school_date <= through)
  }).sort((a, b) => b.school_date.localeCompare(a.school_date) || b.id - a.id), [state, tab, gradeFilter, groupFilter, mode, chartStudentId, from, through])
  const selectedStudentRecord = state.status === 'ready' ? state.students.find(({ student_id }) => student_id === selectedStudent) : undefined
  const readingPreview = state.status === 'ready' && draftValues.ppm !== '' && selectedStudentRecord ? readingLevel(selectedStudentRecord.grade, Number(draftValues.ppm), state.rules) : null
  const dictationPreview = draftValues.total && Number(draftValues.total) > 0 ? dictationPercentage(Number(draftValues.correct || 0), Number(draftValues.total)) : null
  const comprehensionScores = { LITERAL: draftValues.literal === '' ? null : Number(draftValues.literal), INFERENTIAL: draftValues.inferential === '' ? null : Number(draftValues.inferential), CRITICAL: draftValues.critical === '' ? null : Number(draftValues.critical) }
  const comprehensionPreview = state.status === 'ready' ? comprehensionTotal(comprehensionScores, state.rules.comprehension.denominators) : { correct: 0, questions: 0 }
  const changeValues = (key: keyof DraftInputs, value: string) => {
    const cacheKey = draftKey(selectedStudent, selectedDate, kind)
    const nextValues = { ...draftValues, [key]: value }
    setDraftValues(nextValues)
    const numeric = value.trim() === '' ? null : Number(value)
    let nextDraft: AcademicDraft = draft
    if (kind === 'READING' && key === 'ppm') nextDraft = { student_id: selectedStudent, school_date: selectedDate, kind, ppm: numeric ?? undefined }
    if (kind === 'DICTATION') nextDraft = { ...draft, student_id: selectedStudent, school_date: selectedDate, kind, [key === 'total' ? 'total_words' : 'correct_words']: numeric ?? undefined } as AcademicDraft
    if (kind === 'COMPREHENSION' && ['literal', 'inferential', 'critical'].includes(key)) { const aspect = key.toUpperCase() as ComprehensionAspect; const old = draft.kind === 'COMPREHENSION' ? draft.scores : null; nextDraft = { ...draft, student_id: selectedStudent, school_date: selectedDate, kind, scores: { LITERAL: old?.LITERAL ?? null, INFERENTIAL: old?.INFERENTIAL ?? null, CRITICAL: old?.CRITICAL ?? null, [aspect]: numeric } } }
    setDraft(nextDraft); draftCache.current.set(cacheKey, { values: nextValues, draft: nextDraft })
  }
  const save = async (event: React.FormEvent) => { event.preventDefault(); setBusy(true); setMessage(''); setError(''); try { await service.saveAcademicRecord({ ...draft, student_id: selectedStudent, school_date: selectedDate, kind }); draftCache.current.delete(draftKey(selectedStudent, selectedDate, kind)); setMessage('Registro guardado en la demostración.'); reload() } catch (reason) { setError(reason instanceof Error ? reason.message : 'No se guardó el registro; el formulario se conservó.') } finally { setBusy(false) } }
  const openRegistration = (requestedKind: AcademicKind | null) => { if (requestedKind) setKind(requestedKind); setEditorOpen(true) }
  const clearFilters = () => { setGradeFilter(''); setGroupFilter(''); setMode('group'); setFrom(''); setThrough('') }
  return <>
    <PageHeader eyebrow="GESTIÓN · DATOS FICTICIOS" title="Progreso académico" description="Captura y consulta de evaluaciones de demostración. Guardar un resultado no modifica Áureos." />
    {state.status === 'loading' && <Card role="status">Cargando registros y reglas…</Card>}
    {state.status === 'error' && <Card role="alert"><strong>No se pudo consultar el progreso académico.</strong><p>El error no se considera una colección vacía.</p><Button variant="primary" onClick={reload}>Reintentar</Button></Card>}
    {state.status === 'ready' && <>
      <Card className="school-demo-note academic-demo-note"><strong>Modo demostración</strong><span>Registros, reglas y aplicaciones viven solo en memoria; se pierden al recargar y no se envían a la terminal. Fechas en {`America/Mexico_City`}.</span></Card>
      <nav className="academic-tabs" aria-label="Subpestañas de progreso académico" role="tablist">
        {([['summary', 'Resumen'], ['reading', 'Fluidez lectora'], ['dictation', 'Dictado'], ['comprehension', 'Comprensión']] as [AcademicTab, string][]).map(([key, label]) => <button key={key} type="button" role="tab" tabIndex={tab === key ? 0 : -1} aria-selected={tab === key} className={tab === key ? 'academic-tab is-active' : 'academic-tab'} onClick={() => setTab(key)} onKeyDown={(event) => { if (!['ArrowLeft', 'ArrowRight', 'Home', 'End'].includes(event.key)) return; event.preventDefault(); const tabs = Array.from(event.currentTarget.parentElement?.querySelectorAll<HTMLButtonElement>('[role="tab"]') ?? []); const current = tabs.indexOf(event.currentTarget); const next = event.key === 'Home' ? 0 : event.key === 'End' ? tabs.length - 1 : (current + (event.key === 'ArrowRight' ? 1 : -1) + tabs.length) % tabs.length; tabs[next]?.focus(); tabs[next]?.click() }}>{label}</button>)}
      </nav>
      <Card className="academic-shared-filters"><div className="section-header"><div><h2>Alcance y período</h2><p>Estos filtros se conservan al cambiar de subpestaña.</p></div></div><div className="school-filters academic-filter-grid">
        <label>Vista<select value={mode} onChange={(event) => setMode(event.target.value as 'group' | 'student')}><option value="group">Todo el grupo</option><option value="student">Alumno</option></select></label>
        {mode === 'student' && <label>Alumno<select value={chartStudentId} onChange={(event) => setChartStudentId(Number(event.target.value))}>{state.students.filter((student) => (!gradeFilter || String(student.grade) === gradeFilter) && (!groupFilter || student.group === groupFilter)).map((student) => <option key={student.student_id} value={student.student_id}>{student.preferred_name} · {student.grade}° {student.group}</option>)}</select></label>}
        <label>Grado<select value={gradeFilter} onChange={(event) => { setGradeFilter(event.target.value); setGroupFilter('') }}><option value="">Todos</option>{[...new Set(state.students.map(({ grade }) => grade))].sort((a, b) => a - b).map((grade) => <option key={grade} value={grade}>{grade}°</option>)}</select></label>
        <label>Grupo<select value={groupFilter} onChange={(event) => setGroupFilter(event.target.value)}><option value="">Todos</option>{[...new Set(state.students.filter((student) => !gradeFilter || String(student.grade) === gradeFilter).map(({ group }) => group))].sort().map((group) => <option key={group} value={group}>{group}</option>)}</select></label>
        <label>Desde<input type="date" value={from} onChange={(event) => setFrom(event.target.value)} /></label><label>Hasta<input type="date" value={through} onChange={(event) => setThrough(event.target.value)} /></label><Button onClick={clearFilters}>Limpiar filtros</Button>
      </div></Card>
      {tab !== 'summary' && <div className="academic-register-row"><Button variant="primary" onClick={() => openRegistration(tab === 'reading' ? 'READING' : tab === 'dictation' ? 'DICTATION' : 'COMPREHENSION')}>Registrar evaluación</Button></div>}
      <AcademicAnalytics tab={tab} filters={{ grade: gradeFilter, group: groupFilter, from, through, mode, studentId: chartStudentId } satisfies AcademicFilters} students={state.students} records={state.records} rules={state.rules} onSelectTab={setTab} onRegister={openRegistration} />
      <details className="card school-editor-card academic-editor" open={editorOpen} onToggle={(event) => setEditorOpen(event.currentTarget.open)}><summary className="academic-editor-summary"><span><strong>Capturar o corregir una evaluación</strong><small>Una evaluación por alumno, fecha escolar y tipo; al guardar no se aplican Áureos.</small></span><BookOpenCheck size={20} aria-hidden="true" /></summary>
        {!state.records.some((record) => record.student_id === selectedStudent && record.school_date === selectedDate && record.kind === kind) && <p className="school-help">Sin evaluar: todavía no hay un registro de {kind === 'READING' ? 'Lectura' : kind === 'DICTATION' ? 'Dictado' : 'Comprensión'} para este alumno y fecha.</p>}
        <form className="school-editor-grid" onSubmit={save}>
          <label>Alumno<select value={selectedStudent} onChange={(event) => setSelectedStudent(Number(event.target.value))}>{state.students.map((student) => <option key={student.student_id} value={student.student_id}>{student.name} · {student.student_id}</option>)}</select></label>
          <label>Fecha escolar<input type="date" value={selectedDate} onChange={(event) => setSelectedDate(event.target.value)} /></label>
          <label>Evaluación<select value={kind} onChange={(event) => setKind(event.target.value as AcademicKind)}><option value="READING">Lectura</option><option value="DICTATION">Dictado</option><option value="COMPREHENSION">Comprensión</option></select></label>
          {kind === 'READING' && <label>PPM<input type="number" min="0" max="65535" step="1" value={draftValues.ppm} onChange={(event) => changeValues('ppm', event.target.value)} /></label>}
          {kind === 'DICTATION' && <><label>Total de palabras<input type="number" min="1" step="1" value={draftValues.total} onChange={(event) => changeValues('total', event.target.value)} /></label><label>Palabras correctas<input type="number" min="0" step="1" value={draftValues.correct} onChange={(event) => changeValues('correct', event.target.value)} /></label></>}
          {kind === 'COMPREHENSION' && aspects.map((aspect) => <label key={aspect}>{aspectLabels[aspect]} · aciertos / {state.rules.comprehension.denominators[aspect]}<input type="number" min="0" max={state.rules.comprehension.denominators[aspect]} step="1" value={draftValues[aspect.toLowerCase() as keyof typeof draftValues]} placeholder="Sin evaluar" onChange={(event) => changeValues(aspect.toLowerCase() as keyof typeof draftValues, event.target.value)} /></label>)}
          {kind === 'DICTATION' && dictationPreview !== null && state.status === 'ready' && <p className="school-result-preview">Porcentaje correcto: {dictationPreview.toLocaleString('es-MX', { maximumFractionDigits: 2 })}% · {state.rules.dictation.boundaries ? 'clasificación según rangos configurados' : 'rangos Sin configurar'}</p>}
          {kind === 'READING' && draftValues.ppm !== '' && selectedStudentRecord && <p className="school-result-preview">Nivel actual de {selectedStudentRecord.grade}°: {readingPreview ? academicLevelLabels[readingPreview] : 'Sin configurar'}</p>}
          {kind === 'COMPREHENSION' && state.status === 'ready' && <p className="school-result-preview">Total bruto de los aspectos evaluados: {comprehensionPreview.correct}/{comprehensionPreview.questions}. Los aspectos vacíos siguen “Sin evaluar”. Ponderación: {state.rules.comprehension.weights ? 'configurada' : 'Sin configurar'}.</p>}
          {error && <p className="school-error" role="alert">{error}</p>}{message && <p className="school-success" role="status">{message}</p>}
          <div className="school-actions"><Button type="submit" variant="primary" disabled={busy}>{busy ? 'Guardando…' : 'Guardar evaluación'}</Button><span>Versión registrada: {state.records.find((record) => record.student_id === selectedStudent && record.school_date === selectedDate && record.kind === kind)?.version ?? 1}</span></div>
        </form>
      </details>
      <Card id="academic-history" className="school-filter-card"><div className="section-header"><div><h2>Historial</h2><p>{filtered.length} registro(s)</p></div></div><p className="school-help">Los filtros de grado, grupo y fechas de arriba también controlan este historial.</p>
        {state.records.length === 0 ? <EmptyState title="Sin evaluaciones" description="La consulta terminó correctamente; todavía no hay evaluaciones capturadas." /> : filtered.length === 0 ? <EmptyState title="Sin coincidencias" description="Ningún registro coincide con los filtros actuales." /> : <div className="school-history-list">{filtered.map((record) => {
          const student = state.students.find(({ student_id }) => student_id === record.student_id)!
          const result = academicResult(record, state.rules)
          const related = state.applications.filter((application) => application.record_type === 'ACADEMIC' && application.record_id === record.id)
          return <article className="school-history-row" key={record.id}><div><strong>{student.name} · {record.kind === 'READING' ? 'Lectura' : record.kind === 'DICTATION' ? 'Dictado' : 'Comprensión'}</strong><span>{record.school_date} · Versión {record.version} · {result.label}</span>{record.kind === 'COMPREHENSION' && <div className="comprehension-aspect-results">{aspects.map((aspect) => record.scores[aspect] === null ? <span className="aspect-unassessed" key={aspect}>{aspectLabels[aspect]}: Sin evaluar</span> : <span className={`aspect-level aspect-${aspectLevel(record.scores[aspect]!, record.denominators[aspect])}`} key={aspect}>{aspectLabels[aspect]}: {record.scores[aspect]}/{record.denominators[aspect]} · {aspectLabel(record.scores[aspect]!, record.denominators[aspect])}</span>)}</div>}</div><div className="school-history-actions"><Button onClick={() => { setSelectedStudent(record.student_id); setSelectedDate(record.school_date); setKind(record.kind) }}>Corregir</Button>{record.kind === 'READING' && <SchoolApplicationReview service={service} recordType="ACADEMIC" recordId={record.id} studentId={record.student_id} indicator="READING" alreadyApplied={related.some((app) => app.indicator === 'READING')} onApplied={reload} />}{record.kind === 'DICTATION' && <SchoolApplicationReview service={service} recordType="ACADEMIC" recordId={record.id} studentId={record.student_id} indicator="DICTATION" alreadyApplied={related.some((app) => app.indicator === 'DICTATION')} onApplied={reload} />}{record.kind === 'COMPREHENSION' && state.rules.comprehension.rewardMode === 'ASPECTS' && aspects.map((aspect) => record.scores[aspect] !== null && <SchoolApplicationReview key={aspect} service={service} recordType="ACADEMIC" recordId={record.id} studentId={record.student_id} indicator={`COMPREHENSION_${aspect}`} alreadyApplied={related.some((app) => app.indicator === `COMPREHENSION_${aspect}`)} onApplied={reload} />)}{record.kind === 'COMPREHENSION' && state.rules.comprehension.rewardMode === 'OVERALL' && <SchoolApplicationReview service={service} recordType="ACADEMIC" recordId={record.id} studentId={record.student_id} indicator="COMPREHENSION_OVERALL" alreadyApplied={related.some((app) => app.indicator === 'COMPREHENSION_OVERALL')} onApplied={reload} />}</div>
            {related.map((application) => application.record_version !== record.version && <p className="school-warning" key={application.id} role="status">La evaluación cambió desde la aplicación {application.id} (v{application.record_version}); no se recalculó el movimiento original. <Link to={`/alumnos/${record.student_id}?schoolRecord=${record.id}&schoolIndicator=${application.indicator}&schoolRecordType=ACADEMIC`}>Abrir ajuste manual PM.7A con referencia al registro</Link>.</p>)}
            {related.map((application) => <p className="school-application-meta" key={`app-${application.id}`}>Aplicación {application.id} · {application.result_label} · regla v{application.academic_rules_version} · {application.amount} Áureos · movimiento {application.movement_id ?? 'ninguno'} · {movementDateFormatter.format(application.created_at * 1000)}</p>)}
          </article>
        })}</div>}
      </Card>
    </>}
  </>
}
