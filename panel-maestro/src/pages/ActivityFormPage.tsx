import { useEffect, useMemo, useState, type FormEvent } from 'react'
import { Link, useNavigate, useParams } from 'react-router-dom'
import { ArrowLeft, CheckCircle2 } from 'lucide-react'
import { Button, Card, EmptyState, PageHeader } from '../components/ui'
import { demoPanelService } from '../services/demoPanelService'
import { ActivityOperationError, canEditDemoActivity, validateDemoActivityConfiguration, type ActivityConfiguration, type DemoActivityOperations } from '../services/DemoPanelDataService'
import type { ActivityClaim, ActivitySession, Student } from '../models/domain'
import type { PanelDataService } from '../services/PanelDataService'
import { activityDateFormatter, activityDisplayName } from '../services/activityQueries'

type ActivityService = PanelDataService & DemoActivityOperations
type FormLoadState = { status: 'loading' } | { status: 'error' } | { status: 'ready'; students: readonly Student[]; activity: ActivitySession | null; claims: readonly ActivityClaim[] }
type Draft = { numberEnabled: boolean; number: string; timed: boolean; minutes: string; seconds: string; reward: string; mode: ActivityConfiguration['participant_mode']; selectedIds: number[] }
const emptyDraft: Draft = { numberEnabled: false, number: '', timed: false, minutes: '5', seconds: '0', reward: '10', mode: 'ALL', selectedIds: [] }

function toConfiguration(draft: Draft): ActivityConfiguration {
  const minutes = Number(draft.minutes || 0)
  const seconds = Number(draft.seconds || 0)
  return {
    activity_number_enabled: draft.numberEnabled, activity_number: Number(draft.number || 0),
    timed: draft.timed, duration_seconds: draft.timed ? minutes * 60 + seconds : 0,
    reward_amount: Number(draft.reward), participant_mode: draft.mode,
    selected_student_ids: draft.mode === 'SELECTED' ? [...draft.selectedIds] : [],
  }
}

function dateForExpiration(activity: ActivitySession, duration: number) {
  return duration > 0 ? activityDateFormatter.format((activity.started_at + duration) * 1000) : 'Sin vencimiento (sin límite de tiempo)'
}

export function ActivityFormPage({ mode, service = demoPanelService }: { mode: 'create' | 'edit'; service?: ActivityService }) {
  const { activityId: rawId } = useParams()
  const activityId = /^\d+$/.test(rawId ?? '') ? Number(rawId) : NaN
  const editing = mode === 'edit'
  const navigate = useNavigate()
  const [loadState, setLoadState] = useState<FormLoadState>({ status: 'loading' })
  const [attempt, setAttempt] = useState(0)
  const [draft, setDraft] = useState<Draft>(emptyDraft)
  const [confirming, setConfirming] = useState(false)
  const [validationError, setValidationError] = useState('')
  const [operationError, setOperationError] = useState('')
  const [saving, setSaving] = useState(false)

  useEffect(() => {
    let current = true
    setLoadState({ status: 'loading' })
    const request = editing
      ? Promise.all([service.getStudents(), service.getActivities(), service.getClaims()]).then(([students, activities, claims]) => ({ students, activity: activities.find((activity) => activity.id === activityId) ?? null, claims }))
      : service.getStudents().then((students) => ({ students, activity: null, claims: [] as readonly ActivityClaim[] }))
    request.then((result) => {
      if (!current) return
      setLoadState({ status: 'ready', ...result })
      if (result.activity) setDraft({
        numberEnabled: result.activity.activity_number_enabled, number: result.activity.activity_number_enabled ? String(result.activity.activity_number) : '',
        timed: result.activity.duration_seconds > 0, minutes: String(Math.floor(result.activity.duration_seconds / 60)), seconds: String(result.activity.duration_seconds % 60),
        reward: String(result.activity.reward_amount), mode: result.activity.participant_mode, selectedIds: [...result.activity.participant_student_ids],
      })
    }).catch(() => { if (current) setLoadState({ status: 'error' }) })
    return () => { current = false }
  }, [service, attempt, editing, activityId])

  const configuration = useMemo(() => toConfiguration(draft), [draft])
  const activity = loadState.status === 'ready' ? loadState.activity : null
  const availableStudentIds = loadState.status === 'ready' ? loadState.students.map(({ student_id }) => student_id) : []
  const locked = Boolean(editing && activity && !canEditDemoActivity(activity, loadState.status === 'ready' ? loadState.claims : []))
  const updateDraft = (change: Partial<Draft>) => { setDraft((current) => ({ ...current, ...change })); setConfirming(false); setOperationError(''); setValidationError('') }
  const chooseAllStudents = () => updateDraft({ selectedIds: [...availableStudentIds] })
  const clearStudents = () => updateDraft({ selectedIds: [] })
  const invertStudents = () => updateDraft({ selectedIds: availableStudentIds.filter((id) => !draft.selectedIds.includes(id)) })

  const requestConfirmation = (event: FormEvent) => {
    event.preventDefault()
    const error = loadState.status === 'ready' ? validateDemoActivityConfiguration(configuration, loadState.students.map(({ student_id }) => student_id)) : 'No se pudo validar el borrador.'
    if (error) { setValidationError(error); setConfirming(false); return }
    setValidationError(''); setOperationError(''); setConfirming(true)
  }

  const confirmOperation = async () => {
    if (loadState.status !== 'ready') return
    setSaving(true); setOperationError('')
    try {
      const saved = editing
        ? await service.updateActiveActivity(activityId, configuration)
        : await service.createActivity(configuration)
      navigate(`/actividades/${saved.id}`, { state: { activityNotice: editing ? 'Los cambios de la actividad se guardaron en memoria.' : 'La actividad se inició en memoria con la hora de la computadora.' } })
    } catch (error) {
      setOperationError(error instanceof ActivityOperationError ? error.message : error instanceof Error ? error.message : 'No se pudo completar la operación. El borrador se conserva.')
    } finally { setSaving(false) }
  }

  return <>
    <PageHeader eyebrow="GESTIÓN · DEMOSTRACIÓN" title={editing ? 'Editar actividad' : 'Crear actividad'} description={editing ? `Editar ${activity ? activityDisplayName(activity) : 'actividad activa'}.` : 'Configura e inicia una sesión temporal de demostración.'} />
    <div className="dashboard-demo-note" role="note">El borrador no se guarda. La confirmación actualiza el servicio demo en memoria; se pierde al recargar y no se envía a la terminal.</div>
    {loadState.status === 'loading' && <Card className="directory-state" role="status"><span className="status-dot" />{editing ? 'Cargando actividad…' : 'Cargando alumnos…'}</Card>}
    {loadState.status === 'error' && <Card className="directory-state directory-error" role="alert"><div><strong>No se pudo cargar la información del formulario.</strong><p>El borrador no se guarda automáticamente.</p></div><Button variant="primary" onClick={() => setAttempt((value) => value + 1)}>Reintentar</Button></Card>}
    {loadState.status === 'ready' && editing && !activity && <Card><EmptyState title="Actividad inexistente" description="No se encontró una actividad con ese ID." action={<Link className="button button-secondary" to="/actividades">Regresar a actividades</Link>} /></Card>}
    {loadState.status === 'ready' && editing && activity && locked && <Card><EmptyState title="La actividad no se puede editar" description={activity.status !== 'ACTIVE' ? 'Solo se editan actividades activas; las finalizadas o canceladas no se reabren.' : 'Esta actividad tiene un reclamo previo, por lo que la edición está bloqueada.'} action={<Link className="button button-secondary" to={`/actividades/${activity.id}`}>Volver al detalle</Link>} /></Card>}
    {loadState.status === 'ready' && (!editing || (activity && !locked)) && <>
      <Link className="back-link" to={editing && activity ? `/actividades/${activity.id}` : '/actividades'}><ArrowLeft size={16} />{editing ? 'Regresar al detalle' : 'Regresar a actividades'}</Link>
      <form className="activity-form" onSubmit={requestConfirmation} noValidate>
        <Card className="activity-form-card">
          <div className="activity-form-layout">
            <section className="activity-config-column" aria-labelledby="activity-config-heading">
              <h2 id="activity-config-heading">Configuración de la actividad</h2>
              <div className="activity-config-grid">
                <fieldset className="activity-setting-group">
                  <legend>Número de actividad</legend>
                  <label className="activity-setting-toggle"><input type="checkbox" checked={draft.numberEnabled} onChange={(event) => updateDraft({ numberEnabled: event.target.checked })} />Asignar número</label>
                  <label className="activity-setting-field">Número (1–9999)<input type="number" min="1" max="9999" value={draft.number} onChange={(event) => updateDraft({ number: event.target.value })} disabled={!draft.numberEnabled} placeholder="Opcional" /></label>
                </fieldset>
                <fieldset className="activity-setting-group activity-duration-group">
                  <legend>Duración</legend>
                  <label className="activity-setting-toggle"><input type="checkbox" checked={draft.timed} onChange={(event) => updateDraft({ timed: event.target.checked })} />Usar duración con tiempo</label>
                  <div className="activity-duration-fields">
                    <label className="activity-setting-field">Minutos (0–999)<input type="number" min="0" max="999" value={draft.minutes} onChange={(event) => updateDraft({ minutes: event.target.value })} disabled={!draft.timed} /></label>
                    <label className="activity-setting-field">Segundos (0–59)<input type="number" min="0" max="59" value={draft.seconds} onChange={(event) => updateDraft({ seconds: event.target.value })} disabled={!draft.timed} /></label>
                  </div>
                </fieldset>
                <label className="activity-reward-field">Recompensa por alumno (1–10000 Áureos)<input type="number" min="1" max="10000" value={draft.reward} onChange={(event) => updateDraft({ reward: event.target.value })} /></label>
              </div>
              <div className="activity-form-summary" aria-live="polite"><strong>Resumen</strong><span>{draft.numberEnabled ? `Actividad ${draft.number || '—'}` : 'Sin número'}</span><span>{draft.timed ? `${draft.minutes || 0}:${String(Number(draft.seconds || 0)).padStart(2, '0')}` : 'Sin límite de tiempo'}</span><span>{draft.reward || '—'} Áureos</span><span>{draft.mode === 'ALL' ? 'Todos los alumnos registrados' : draft.mode === 'SELECTED' ? `${draft.selectedIds.length} de ${loadState.students.length} alumnos seleccionados` : 'Sin filtro previo de participantes'}</span></div>
            </section>
            <section className="activity-participants-column" aria-labelledby="activity-participants-heading">
              <h2 id="activity-participants-heading">Selección de participantes</h2>
              <fieldset className="participant-mode-field"><legend>Modalidad</legend>
                <label><input type="radio" name="participant-mode" checked={draft.mode === 'ALL'} onChange={() => updateDraft({ mode: 'ALL' })} />Todos los alumnos registrados al iniciar</label>
                <label><input type="radio" name="participant-mode" checked={draft.mode === 'SELECTED'} onChange={() => updateDraft({ mode: 'SELECTED' })} />Seleccionar alumnos</label>
                <label><input type="radio" name="participant-mode" checked={draft.mode === 'PARTICIPANTS_DISABLED'} onChange={() => updateDraft({ mode: 'PARTICIPANTS_DISABLED' })} />Sin filtro previo de participantes</label>
              </fieldset>
              {draft.mode === 'SELECTED' && <fieldset className="participant-choice-field"><legend>Selecciona uno o más alumnos</legend>
                <div className="participant-selection-actions" role="group" aria-label="Acciones de selección de alumnos">
                  <button type="button" className="button button-secondary" onClick={chooseAllStudents}>Elegir todos</button>
                  <button type="button" className="button button-secondary" onClick={clearStudents}>Limpiar</button>
                  <button type="button" className="button button-secondary" onClick={invertStudents}>Invertir</button>
                </div>
                <span className="participant-selection-count" role="status" aria-live="polite">{draft.selectedIds.length} de {loadState.students.length} alumnos seleccionados</span>
                <div className="participant-student-list">{loadState.students.map((student) => <label key={student.student_id}><input type="checkbox" checked={draft.selectedIds.includes(student.student_id)} onChange={(event) => updateDraft({ selectedIds: event.target.checked ? [...draft.selectedIds, student.student_id] : draft.selectedIds.filter((id) => id !== student.student_id) })} />{student.name} · {student.grade}° {student.group}</label>)}</div>
              </fieldset>}
            </section>
          </div>
          {validationError && <p className="activity-form-error" role="alert">{validationError}</p>}
          {operationError && <p className="activity-form-error" role="alert">No se completó la operación. {operationError} El borrador se conserva.</p>}
          {!confirming && <Button variant="primary" type="submit">{editing ? 'Revisar cambios' : 'Revisar y confirmar inicio'}</Button>}
        </Card>
        {confirming && <Card className="activity-confirm-card" role="region" aria-label="Confirmación de actividad">
          <div className="section-header"><div><h2>Confirma la operación</h2><p>{editing ? 'El ID y la hora de inicio se conservan.' : 'Al confirmar, la sesión empieza con la hora actual de la computadora.'}</p></div><CheckCircle2 size={20} /></div>
          {editing && activity && <div className="expiration-comparison"><div><span>Vencimiento actual</span><strong>{dateForExpiration(activity, activity.duration_seconds)}</strong></div><div><span>Vencimiento propuesto</span><strong>{dateForExpiration(activity, configuration.timed ? configuration.duration_seconds : 0)}</strong></div></div>}
          <div className="activity-confirm-actions"><Button type="button" variant="primary" onClick={confirmOperation} disabled={saving}>{saving ? 'Procesando…' : editing ? 'Confirmar cambios' : 'Confirmar e iniciar actividad'}</Button><Button type="button" onClick={() => { setConfirming(false); setOperationError('') }} disabled={saving}>Seguir editando</Button></div>
        </Card>}
      </form>
    </>}
  </>
}
