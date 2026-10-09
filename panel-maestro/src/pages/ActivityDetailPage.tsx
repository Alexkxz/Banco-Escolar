import { useEffect, useState } from 'react'
import { Link, useLocation, useNavigate, useParams } from 'react-router-dom'
import { Activity, ArrowDownRight, ArrowLeft, ArrowUpRight, CheckCircle2, Clock3, Pencil, XCircle } from 'lucide-react'
import { Button, Card, EmptyState, PageHeader } from '../components/ui'
import { demoPanelService } from '../services/demoPanelService'
import { canEditDemoActivity, type DemoActivityOperations } from '../services/DemoPanelDataService'
import type { ActivitySession } from '../models/domain'
import type { PanelDataService } from '../services/PanelDataService'
import { activityDateFormatter, ACTIVITY_TIME_ZONE, formatRemaining, getActivityRemaining, loadActivityDetail, type ActivityDetailSnapshot } from '../services/activityQueries'

type ActivityService = PanelDataService & DemoActivityOperations
type LoadState = { status: 'loading' } | { status: 'error' } | { status: 'ready'; detail: ActivityDetailSnapshot | null }
type CloseIntent = 'FINISHED' | 'CANCELLED' | null
const realClock = () => Date.now()

function describeStatus(status: ActivitySession['status']) {
  if (status === 'ACTIVE') return 'Activa'
  return status === 'FINISHED' ? 'Finalizada' : 'Cancelada'
}

function durationLabel(seconds: number) {
  if (seconds === 0) return 'Sin límite de tiempo'
  if (seconds >= 3600) {
    const hours = Math.floor(seconds / 3600)
    const minutes = Math.floor((seconds % 3600) / 60)
    const remainder = seconds % 60
    return `${hours} h ${minutes} min${remainder ? ` ${remainder} s` : ''}`
  }
  const minutes = Math.floor(seconds / 60)
  const remainingSeconds = seconds % 60
  return `${minutes} min ${remainingSeconds} s`
}

export function ActivityDetailPage({ service = demoPanelService, now = realClock }: { service?: ActivityService; now?: () => number }) {
  const { activityId: rawId } = useParams()
  const activityId = /^\d+$/.test(rawId ?? '') ? Number(rawId) : NaN
  const navigate = useNavigate()
  const location = useLocation()
  const [state, setState] = useState<LoadState>({ status: 'loading' })
  const [attempt, setAttempt] = useState(0)
  const [clockTime, setClockTime] = useState(now)
  const [closeIntent, setCloseIntent] = useState<CloseIntent>(null)
  const [operationError, setOperationError] = useState('')
  const [notice, setNotice] = useState((location.state as { activityNotice?: string } | null)?.activityNotice ?? '')
  const [closing, setClosing] = useState(false)
  const currentDetail = state.status === 'ready' ? state.detail : null
  const remainingNow = currentDetail ? getActivityRemaining(currentDetail.activity, clockTime) : null
  const clockActive = Boolean(currentDetail?.activity.status === 'ACTIVE' && currentDetail.activity.duration_seconds > 0 && (remainingNow?.state === 'running' || remainingNow?.state === 'pending'))

  useEffect(() => {
    let current = true
    setState({ status: 'loading' })
    loadActivityDetail(service, activityId).then((detail) => { if (current) setState({ status: 'ready', detail }) }).catch(() => { if (current) setState({ status: 'error' }) })
    return () => { current = false }
  }, [service, attempt, activityId])

  useEffect(() => {
    if (!clockActive) return
    setClockTime(now())
    const timer = window.setInterval(() => setClockTime(now()), 1000)
    return () => window.clearInterval(timer)
  }, [now, clockActive])

  const detail = state.status === 'ready' ? state.detail : null
  const editable = Boolean(detail && canEditDemoActivity(detail.activity, detail.claims))
  const closeActivity = async () => {
    if (!detail || !closeIntent) return
    setClosing(true); setOperationError('')
    try {
      const updated = await service.closeActiveActivity(detail.activity.id, closeIntent)
      setState({ status: 'ready', detail: { ...detail, activity: updated } })
      setNotice(closeIntent === 'FINISHED' ? 'La actividad se finalizó y se guardó localmente en modo demostración.' : 'La actividad se canceló y se guardó localmente en modo demostración.')
      setCloseIntent(null)
    } catch (error) {
      setOperationError(error instanceof Error ? error.message : 'No se pudo cerrar la actividad. Intenta de nuevo.')
    } finally { setClosing(false) }
  }

  return <>
    <PageHeader eyebrow="GESTIÓN · DEMOSTRACIÓN" title="Detalle de actividad" description={`Consulta de sesión y reclamos existentes · ${ACTIVITY_TIME_ZONE}.`} />
    <div className="dashboard-demo-note" role="note">Las actividades creadas, editadas o cerradas se guardan localmente en este navegador y no se envían a la terminal. Cobros sigue en preparación; aquí solo se consultan registros existentes.</div>
    {state.status === 'loading' && <Card className="directory-state" role="status"><span className="status-dot" />Cargando detalle de actividad…</Card>}
    {state.status === 'error' && <Card className="directory-state directory-error" role="alert"><div><strong>No se pudo consultar la actividad.</strong><p>El error no se interpreta como actividad inexistente.</p></div><Button variant="primary" onClick={() => setAttempt((value) => value + 1)}>Reintentar</Button></Card>}
    {state.status === 'ready' && !detail && <Card><EmptyState title="Actividad inexistente" description="No se encontró una actividad con ese ID en la demostración." action={<Link className="button button-secondary" to="/actividades">Regresar a actividades</Link>} /></Card>}
    {detail && <>
      <Link className="back-link" to="/actividades"><ArrowLeft size={16} />Regresar a actividades</Link>
      {notice && <div className="activity-success-note" role="status"><CheckCircle2 size={17} />{notice}</div>}
      {operationError && <div className="activity-form-error" role="alert">No se completó el cierre. {operationError}</div>}
      <Card className="activity-detail-card">
        <div className="activity-detail-heading"><span className={`activity-detail-icon status-${detail.activity.status.toLowerCase()}`}><Activity size={23} /></span><div><span className="eyebrow">ID de demostración {detail.activity.id}</span><h2>{detail.displayName}</h2><span className={`activity-status status-${detail.activity.status.toLowerCase()}`}>{describeStatus(detail.activity.status)}</span></div></div>
        <div className="activity-detail-facts"><div><span>Recompensa por alumno</span><strong>{detail.activity.reward_amount.toLocaleString('es-MX')} Áureos</strong></div><div><span>Participantes</span><strong>{detail.activity.participant_mode === 'ALL' ? `Todos · ${detail.activity.participant_student_ids.length} al iniciar` : detail.activity.participant_mode === 'SELECTED' ? `${detail.activity.participant_student_ids.length} seleccionados` : 'Sin filtro previo'}</strong></div><div><span>Inicio · {ACTIVITY_TIME_ZONE}</span><strong><time dateTime={new Date(detail.activity.started_at * 1000).toISOString()}>{activityDateFormatter.format(detail.activity.started_at * 1000)}</time></strong></div><div><span>Duración</span><strong>{durationLabel(detail.activity.duration_seconds)}</strong></div>
          {detail.activity.closed_at > 0 && <div><span>Cierre · {ACTIVITY_TIME_ZONE}</span><strong><time dateTime={new Date(detail.activity.closed_at * 1000).toISOString()}>{activityDateFormatter.format(detail.activity.closed_at * 1000)}</time></strong></div>}
          <div className="activity-remaining"><span><Clock3 size={15} />Tiempo restante</span><strong>{(() => { const remaining = getActivityRemaining(detail.activity, clockTime); return remaining.state === 'expired' ? 'Tiempo agotado' : remaining.state === 'running' ? formatRemaining(remaining.seconds) : remaining.state === 'untimed' ? 'Sin límite de tiempo' : remaining.state === 'pending' ? 'Pendiente de inicio' : 'Actividad cerrada' })()}</strong></div>
        </div>
        <div className="activity-detail-actions">{editable && <Button onClick={() => navigate(`/actividades/${detail.activity.id}/editar`)}><Pencil size={15} />Editar actividad</Button>}{detail.activity.status === 'ACTIVE' && <><Button onClick={() => { setCloseIntent('FINISHED'); setOperationError('') }}><CheckCircle2 size={15} />Finalizar</Button><Button onClick={() => { setCloseIntent('CANCELLED'); setOperationError('') }}><XCircle size={15} />Cancelar actividad</Button></>}</div>
      </Card>

      {closeIntent && <Card className="activity-close-confirm" role="region" aria-label="Confirmar cierre de actividad"><strong>{closeIntent === 'FINISHED' ? '¿Finalizar esta actividad?' : '¿Cancelar esta actividad?'}</strong><p>Se guardará localmente el estado y la hora actual. Los reclamos y movimientos existentes no cambian.</p><div><Button variant="primary" onClick={closeActivity} disabled={closing}>{closing ? 'Procesando…' : `Confirmar ${closeIntent === 'FINISHED' ? 'finalización' : 'cancelación'}`}</Button><Button onClick={() => setCloseIntent(null)} disabled={closing}>Volver</Button></div></Card>}

      <Card className="activity-participant-card"><div className="section-header"><div><h2>{detail.participantRosterAvailable ? 'Participantes y cobros existentes' : 'Cobros registrados'}</h2><p>{detail.participantRosterAvailable ? 'La lista refleja la selección guardada al iniciar. Cuenta y cobro son situaciones independientes.' : 'Sin filtro previo no hay una lista de elegibilidad guardada; solo se presentan reclamos existentes.'}</p></div></div>
        {!detail.participantRosterAvailable && detail.participants.length === 0 && <EmptyState title="Sin reclamos registrados" description="La consulta de reclamos terminó correctamente sin registros para esta actividad." />}
        {detail.participantRosterAvailable && detail.participants.length === 0 && <EmptyState title="Sin participantes en la lista guardada" description="La actividad conserva una selección vacía en los datos consultados." />}
        {detail.participants.length > 0 && <div className="activity-participant-list">{detail.participants.map((entry) => <article className="activity-participant-row" key={entry.studentId}>
          <div className="activity-participant-name">{entry.student ? <Link to={`/alumnos/${entry.student.student_id}`}>{entry.student.name}</Link> : <span>Alumno {entry.studentId}</span>}<small>{entry.student ? `${entry.student.grade}° grado · Grupo ${entry.student.group}` : `ID ${entry.studentId}`}</small></div>
          <span className={`participant-account-state ${entry.hasAccount ? 'has-account' : 'no-account'}`}>{entry.hasAccount ? 'Cuenta asignada' : 'Sin cuenta'}</span>
          <span className={`participant-claim-state ${entry.claim?.status === 'PAID' ? 'is-paid' : entry.claim?.status === 'VOIDED' ? 'is-voided' : 'is-unpaid'}`}>{entry.claim?.status === 'PAID' ? 'Cobró' : entry.claim?.status === 'VOIDED' ? 'Cobro anulado' : 'No ha cobrado'}</span>
          {entry.claim && <div className="participant-movement-links"><Link to={`/cobros/${entry.claim.id}`}>Cobro {entry.claim.id} · {entry.claim.status === 'PAID' ? 'Pagado' : 'Anulado'}</Link>{entry.movement ? <Link to={`/movimientos/${entry.movement.id}`}>{entry.movement.type === 'ENTRY' ? <ArrowUpRight size={14} /> : <ArrowDownRight size={14} />}Movimiento {entry.movement.id}</Link> : <span>Sin movimiento relacionado disponible</span>}{entry.claim.void_movement_id > 0 && <Link to={`/movimientos/${entry.claim.void_movement_id}`}>Movimiento inverso {entry.claim.void_movement_id}</Link>}</div>}
        </article>)}</div>}
      </Card>
    </>}
  </>
}
