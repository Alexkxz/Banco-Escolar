import { useEffect, useMemo, useState } from 'react'
import { Link } from 'react-router-dom'
import { Button, Card, PageHeader } from '../components/ui'
import { SchoolApplicationReview } from '../components/SchoolApplicationReview'
import { DemoPanelDataService } from '../services/DemoPanelDataService'
import { demoPanelService } from '../services/demoPanelService'
import { attendanceAmount, schoolDateAt, type AttendanceRecord, type AttendanceRules, type SchoolApplication } from '../services/schoolDomain'
import { movementDateFormatter, PANEL_TIME_ZONE } from '../services/movementQueries'
import type { Student } from '../models/domain'

type State = { status: 'loading' } | { status: 'error' } | { status: 'ready'; students: readonly Student[]; records: AttendanceRecord[]; applications: SchoolApplication[]; rules: AttendanceRules }
const statusLabel: Record<AttendanceRecord['status'], string> = { PRESENT: 'Presente', LATE: 'Retardo', ABSENT_UNJUSTIFIED: 'Falta injustificada', ABSENT_JUSTIFIED: 'Falta justificada' }
const nowDate = () => schoolDateAt(Date.now())

export function AttendancePage({ service = demoPanelService as DemoPanelDataService }: { service?: DemoPanelDataService }) {
  const [state, setState] = useState<State>({ status: 'loading' }); const [attempt, setAttempt] = useState(0); const [date, setDate] = useState(nowDate()); const [statusDraft, setStatusDraft] = useState<Record<number, string>>({}); const [reasonDraft, setReasonDraft] = useState<Record<number, string>>({}); const [errors, setErrors] = useState<Record<number, string>>({}); const [notice, setNotice] = useState('')
  const reload = () => setAttempt((value) => value + 1)
  useEffect(() => { let live = true; setState({ status: 'loading' }); Promise.all([service.getStudents(), service.getAttendanceRecords(), service.getSchoolApplications(), service.getSchoolRules()]).then(([students, records, applications, rules]) => { if (live) setState({ status: 'ready', students, records, applications, rules: rules.attendance }) }).catch(() => { if (live) setState({ status: 'error' }) }); return () => { live = false } }, [service, attempt])
  useEffect(() => { if (state.status !== 'ready') return; setStatusDraft(Object.fromEntries(state.students.map((student) => { const record = state.records.find((entry) => entry.student_id === student.student_id && entry.school_date === date); return [student.student_id, record?.status ?? ''] }))); setReasonDraft(Object.fromEntries(state.students.map((student) => [student.student_id, state.records.find((entry) => entry.student_id === student.student_id && entry.school_date === date)?.justification ?? '']))) }, [state, date])
  const recordsForDate = useMemo(() => state.status === 'ready' ? state.records.filter((record) => record.school_date === date) : [], [state, date])
  const today = date === nowDate()
  const markArrival = async (id: number) => { setErrors((value) => ({ ...value, [id]: '' })); setNotice(''); try { await service.markDemoArrival(id); setNotice('Llegada registrada con la hora actual de la computadora; no proviene de NFC.'); reload() } catch (reason) { setErrors((value) => ({ ...value, [id]: reason instanceof Error ? reason.message : 'No se registró la llegada.' })) } }
  const saveStatus = async (student: Student) => {
    const status = statusDraft[student.student_id]
    if (!status) return
    setErrors((value) => ({ ...value, [student.student_id]: '' })); setNotice('')
    try { await service.saveAttendanceStatus(student.student_id, date, status as AttendanceRecord['status'], reasonDraft[student.student_id] ?? ''); setNotice(`Asistencia de ${student.name} actualizada.`); reload() }
    catch (reason) { setErrors((value) => ({ ...value, [student.student_id]: reason instanceof Error ? reason.message : 'No se guardó la asistencia.' })) }
  }
  return <>
    <PageHeader eyebrow="GESTIÓN · DATOS FICTICIOS" title="Asistencia" description="Registro manual de presencia y consulta de asistencia de demostración." />
    {state.status === 'loading' && <Card role="status">Cargando asistencia…</Card>}
    {state.status === 'error' && <Card role="alert"><strong>No se pudo consultar asistencia.</strong><p>El error no se considera una colección vacía.</p><Button variant="primary" onClick={reload}>Reintentar</Button></Card>}
    {state.status === 'ready' && <>
      <Card className="school-demo-note"><strong>Modo demostración · {PANEL_TIME_ZONE}</strong><span>Registros, reglas y aplicaciones permanecen en memoria, se pierden al recargar y no se envían a la terminal. “Sin registrar” no se convierte automáticamente en falta. La llegada registra la hora local actual del computador, no una lectura NFC.</span></Card>
      <Card className="school-filter-card"><div className="section-header"><div><h2>Lista del día escolar</h2><p>{recordsForDate.length} de {state.students.length} alumnos con registro</p></div><label className="school-date-filter">Fecha escolar<input type="date" value={date} onChange={(event) => setDate(event.target.value)} /></label></div>
        {notice && <p className="school-success" role="status">{notice}</p>}
        <div className="attendance-list">{state.students.map((student) => {
          const record = recordsForDate.find((entry) => entry.student_id === student.student_id) ?? null
          const status = record ? statusLabel[record.status] : 'Sin registrar'
          const matching = record ? state.applications.filter((app) => app.record_type === 'ATTENDANCE' && app.record_id === record.id && app.indicator === 'ATTENDANCE') : []
          const currentAdjustment = record ? attendanceAmount(record, state.records, state.rules) : null
          const mismatch = matching.some((app) => app.signature !== currentAdjustment?.signature || app.record_version !== record?.version)
          const recordedArrival = record?.arrival_at ? movementDateFormatter.format(record.arrival_at * 1000) : ''
          return <article className="attendance-row" key={student.student_id}><div className="attendance-student"><strong>{student.name}</strong><span>{student.grade}° · Grupo {student.group} · {status}</span>{recordedArrival && <time dateTime={new Date(record!.arrival_at! * 1000).toISOString()}>Captura {recordedArrival} · {PANEL_TIME_ZONE}</time>}{record?.status === 'ABSENT_JUSTIFIED' && <span>Justificación: {record.justification || 'Sin detalle'}</span>}</div>
            <div className="attendance-row-controls"><label>Estado<select value={statusDraft[student.student_id] ?? ''} onChange={(event) => setStatusDraft((current) => ({ ...current, [student.student_id]: event.target.value }))}><option value="">Sin registrar</option><option value="PRESENT" disabled={!record?.arrival_at}>Presente (solo con hora capturada)</option><option value="LATE" disabled={!record?.arrival_at}>Retardo (solo con hora capturada)</option><option value="ABSENT_UNJUSTIFIED">Falta injustificada</option><option value="ABSENT_JUSTIFIED">Falta justificada</option></select></label>{statusDraft[student.student_id] === 'ABSENT_JUSTIFIED' && <label>Motivo de justificación<textarea rows={2} value={reasonDraft[student.student_id] ?? ''} onChange={(event) => setReasonDraft((current) => ({ ...current, [student.student_id]: event.target.value }))} placeholder="Opcional" /></label>}
              <div className="school-actions">{today && !record?.arrival_at && <Button variant="primary" onClick={() => markArrival(student.student_id)}>Marcar llegada ahora</Button>}<Button disabled={!statusDraft[student.student_id]} onClick={() => saveStatus(student)}>Guardar estado</Button></div>
              {errors[student.student_id] && <p className="school-error" role="alert">{errors[student.student_id]}</p>}
              {record && <><p className="attendance-adjustment">Ajuste actual: {currentAdjustment ? `${currentAdjustment.label} · ${currentAdjustment.amount > 0 ? '+' : ''}${currentAdjustment.amount} Áureos` : 'Sin ajuste aplicable'}</p>{currentAdjustment && <SchoolApplicationReview service={service} recordType="ATTENDANCE" recordId={record.id} studentId={student.student_id} indicator="ATTENDANCE" alreadyApplied={matching.length > 0} onApplied={reload} />}</>}
              {mismatch && <p className="school-warning" role="status">La asistencia ya aplicada cambió desde su registro. El movimiento se conserva; no se devuelve ni recalcula. <Link to={`/alumnos/${student.student_id}?schoolRecord=${record!.id}&schoolIndicator=ATTENDANCE`}>Usar ajuste manual PM.7A con referencia a la asistencia</Link>.</p>}
              {matching.map((application) => <p className="school-application-meta" key={application.id}>Aplicación {application.id} · {application.result_label} · asistencia v{application.attendance_rules_version} · {application.amount} Áureos · movimiento {application.movement_id ?? 'ninguno'}.</p>)}
            </div>
          </article>
        })}</div>
      </Card>
    </>}
  </>
}
