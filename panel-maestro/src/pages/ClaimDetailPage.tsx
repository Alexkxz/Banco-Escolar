import { useEffect, useMemo, useState } from 'react'
import { Link, useLocation, useNavigate, useParams } from 'react-router-dom'
import { ArrowLeft, CircleAlert, RotateCcw, TicketCheck } from 'lucide-react'
import { Card, EmptyState, PageHeader } from '../components/ui'
import { DemoClaimOperationError, type DemoClaimOperations } from '../services/DemoPanelDataService'
import { demoPanelService } from '../services/demoPanelService'
import { loadClaimDetail, movementDateFormatter, PANEL_TIME_ZONE, type ClaimDetailEntry, type ClaimQueryService } from '../services/claimQueries'
import type { ActivitySession } from '../models/domain'

type DetailService = ClaimQueryService & DemoClaimOperations
type State = { status: 'loading' } | { status: 'error' } | { status: 'ready'; entry: ClaimDetailEntry | null }
type Intent = 'void' | 'authorize' | 'simulate' | null

function eligibilityMessage(entry: ClaimDetailEntry, now: number) {
  const { activity, student, account, claim } = entry
  if (!activity || !student) return 'Falta una referencia de alumno o actividad.'
  if (!account) return 'El alumno no tiene cuenta; la acción queda bloqueada.'
  if (claim.status !== 'VOIDED') return 'Anula primero el cobro. La anulación no autoriza por sí sola otro cobro.'
  if (activity.status !== 'ACTIVE') return 'La actividad está cerrada; no se puede autorizar otro cobro.'
  if (activity.duration_seconds > 0 && Math.floor(now / 1000) >= activity.started_at + activity.duration_seconds) return 'La actividad venció; no se puede autorizar ni cobrar.'
  if (activity.participant_mode !== 'PARTICIPANTS_DISABLED' && !activity.participant_student_ids.includes(claim.student_id)) return 'El alumno no cumple la selección guardada de participantes.'
  return ''
}

function activityName(activity: ActivitySession | null, id: number) {
  return activity ? (activity.activity_number_enabled ? `Actividad ${activity.activity_number}` : `Actividad ${activity.id}`) : `Actividad ${id}`
}

export function ClaimDetailPage({ service = demoPanelService as DetailService, now = () => Date.now() }: { service?: DetailService; now?: () => number }) {
  const { claimId: rawId } = useParams()
  const claimId = /^\d+$/.test(rawId ?? '') ? Number(rawId) : NaN
  const navigate = useNavigate()
  const location = useLocation()
  const [state, setState] = useState<State>({ status: 'loading' })
  const [attempt, setAttempt] = useState(0)
  const [intent, setIntent] = useState<Intent>(null)
  const [reason, setReason] = useState('')
  const [busy, setBusy] = useState(false)
  const [operationError, setOperationError] = useState('')
  const [notice, setNotice] = useState((location.state as { claimNotice?: string } | null)?.claimNotice ?? '')
  useEffect(() => {
    let current = true
    setState({ status: 'loading' })
    loadClaimDetail(service, claimId).then((entry) => { if (current) setState({ status: 'ready', entry }) }).catch(() => { if (current) setState({ status: 'error' }) })
    return () => { current = false }
  }, [service, attempt, claimId])
  useEffect(() => {
    const stateNotice = (location.state as { claimNotice?: string } | null)?.claimNotice
    if (stateNotice) setNotice(stateNotice)
  }, [location.key, location.state])
  const entry = state.status === 'ready' ? state.entry : null
  const nowMs = now()
  const eligibility = entry ? eligibilityMessage(entry, nowMs) : ''
  const pendingAuthorization = entry?.authorizations.find((authorization) => authorization.source_claim_id === entry.claim.id && authorization.status === 'AUTHORIZED') ?? null
  const voidPreview = useMemo(() => entry?.account ? entry.account.balance - entry.claim.reward_amount : null, [entry])

  const runOperation = async () => {
    if (!entry || !intent || busy) return
    setBusy(true); setOperationError('')
    try {
      if (intent === 'void') {
        const result = await service.voidPaidClaim(entry.claim.id, reason)
        setNotice(`Anulación registrada en memoria. Saldo actual: ${result.balance.toLocaleString('es-MX')} Áureos.`)
        setReason(''); setIntent(null); setAttempt((value) => value + 1)
      } else if (intent === 'authorize') {
        await service.authorizeRepeatClaim(entry.claim.id)
        setNotice('Autorización de demostración registrada. Todavía no se modificaron saldos ni movimientos.')
        setIntent(null); setAttempt((value) => value + 1)
      } else {
        if (!pendingAuthorization || !entry.activity) throw new Error('No existe una autorización válida para simular el cobro.')
        const result = await service.simulateAuthorizedClaim(pendingAuthorization.id, entry.activity.reward_amount)
        navigate(`/cobros/${result.claim.id}`, { replace: true, state: { claimNotice: `Nuevo cobro de demostración registrado por ${result.claim.reward_amount.toLocaleString('es-MX')} Áureos. Saldo: ${result.balance.toLocaleString('es-MX')} Áureos.` } })
      }
    } catch (error) {
      setOperationError(error instanceof DemoClaimOperationError ? error.message : error instanceof Error ? error.message : 'No se completó la operación. No se aplicaron cambios parciales.')
    } finally { setBusy(false) }
  }

  return <>
    <PageHeader eyebrow="GESTIÓN · DEMOSTRACIÓN" title="Detalle de cobro" description={`Reclamo, referencias y operaciones ficticias · ${PANEL_TIME_ZONE}.`} />
    <div className="dashboard-demo-note" role="note">Las anulaciones, autorizaciones y nuevos cobros viven solo en memoria y se pierden al recargar. No se envían a la terminal ni representan acciones de un usuario autenticado.</div>
    {state.status === 'loading' && <Card className="directory-state" role="status"><span className="status-dot" />Cargando cobro…</Card>}
    {state.status === 'error' && <Card className="directory-state directory-error" role="alert"><div><strong>No se pudo consultar el cobro.</strong><p>El error no se interpreta como un registro inexistente.</p></div><button className="button button-primary" onClick={() => setAttempt((value) => value + 1)}>Reintentar</button></Card>}
    {state.status === 'ready' && !entry && <Card><EmptyState title="Cobro inexistente" description="No se encontró un cobro con ese ID en la demostración." icon={TicketCheck} action={<Link className="button button-secondary" to="/cobros">Regresar a cobros</Link>} /></Card>}
    {entry && <>
      <Link className="back-link" to="/cobros"><ArrowLeft size={16} />Regresar a cobros</Link>
      {notice && <div className="activity-success-note" role="status">{notice}</div>}
      {operationError && <div className="activity-form-error" role="alert">No se completó la operación. {operationError}</div>}
      <Card className="claim-detail-card">
        <div className="movement-detail-heading"><span className={`movement-detail-icon ${entry.claim.status === 'PAID' ? 'is-credit' : 'is-debit'}`}><TicketCheck /></span><div><span className="eyebrow">Cobro · ID {entry.claim.id}</span><h2>{entry.student?.name ?? `Alumno ${entry.claim.student_id}`}</h2><p>{entry.claim.status === 'PAID' ? 'Pagado' : 'Anulado'} · {entry.claim.reward_amount.toLocaleString('es-MX')} Áureos</p></div></div>
        {!entry.consistent && <div className="claim-inconsistency" role="alert"><CircleAlert size={18} /><span>{entry.inconsistency || 'Las referencias del registro no son coherentes.'} Las operaciones quedan bloqueadas hasta resolver esta inconsistencia.</span></div>}
        <dl className="movement-detail-fields claim-detail-fields">
          <div><dt>Alumno</dt><dd>{entry.student ? <Link to={`/alumnos/${entry.student.student_id}`}>{entry.student.name} · Ver alumno</Link> : `Alumno ${entry.claim.student_id}`}</dd></div>
          <div><dt>Actividad</dt><dd>{entry.activity ? <Link to={`/actividades/${entry.activity.id}`}>{activityName(entry.activity, entry.claim.activity_id)} · Ver actividad</Link> : `Actividad ${entry.claim.activity_id}`}</dd></div>
          <div><dt>Importe histórico</dt><dd>{entry.claim.reward_amount.toLocaleString('es-MX')} Áureos</dd></div>
          <div><dt>Fecha del cobro · {PANEL_TIME_ZONE}</dt><dd><time dateTime={new Date(entry.claim.claimed_at * 1000).toISOString()}>{movementDateFormatter.format(entry.claim.claimed_at * 1000)}</time></dd></div>
          <div><dt>Estado</dt><dd><span className={`claim-status ${entry.claim.status === 'PAID' ? 'is-paid' : 'is-voided'}`}>{entry.claim.status === 'PAID' ? 'Pagado' : 'Anulado'}</span></dd></div>
          <div><dt>Cuenta</dt><dd>{entry.account ? <Link to={`/cuentas/${entry.claim.student_id}`}>Ver cuenta · saldo actual {entry.account.balance.toLocaleString('es-MX')} Áureos</Link> : 'Sin cuenta'}</dd></div>
          <div><dt>Movimiento original</dt><dd>{entry.movement ? <Link to={`/movimientos/${entry.movement.id}`}>Movimiento {entry.movement.id} · {entry.movement.reason}</Link> : 'Referencia original no disponible'}</dd></div>
          {entry.claim.status === 'VOIDED' && <>
            <div><dt>Anulación · {PANEL_TIME_ZONE}</dt><dd>{entry.claim.voided_at ? movementDateFormatter.format(entry.claim.voided_at * 1000) : 'Fecha no registrada'} · {entry.claim.voided_by === 'PANEL_MAESTRO_DEMO' ? 'Panel Maestro · modo demostración' : 'Actor no disponible'}</dd></div>
            <div><dt>Motivo</dt><dd>{entry.claim.void_reason?.trim() || 'Sin motivo'}</dd></div>
            <div><dt>Movimiento inverso</dt><dd>{entry.inverseMovement ? <Link to={`/movimientos/${entry.inverseMovement.id}`}>Movimiento {entry.inverseMovement.id} · −{Math.abs(entry.inverseMovement.amount).toLocaleString('es-MX')} Áureos</Link> : 'Referencia inversa no disponible'}</dd></div>
          </>}
          {entry.claim.authorized_by_claim_id && <div><dt>Autorizado a partir de</dt><dd><Link to={`/cobros/${entry.claim.authorized_by_claim_id}`}>Cobro anulado {entry.claim.authorized_by_claim_id}</Link></dd></div>}
        </dl>
      </Card>

      {entry.events.length > 0 && <Card className="claim-history-card"><div className="section-header"><div><h2>Historial de acciones</h2><p>Los eventos son de demostración y no representan identidad autenticada.</p></div></div><ol className="claim-event-list">{[...entry.events].sort((left, right) => right.occurred_at - left.occurred_at || right.id - left.id).map((event) => <li key={event.id}><strong>{event.type === 'VOIDED' ? 'Cobro anulado' : event.type === 'REAUTHORIZED' ? 'Nuevo cobro autorizado' : 'Nuevo cobro simulado'}</strong><time dateTime={new Date(event.occurred_at * 1000).toISOString()}>{movementDateFormatter.format(event.occurred_at * 1000)}</time><span>{event.actor === 'PANEL_MAESTRO_DEMO' ? 'Panel Maestro · modo demostración' : 'Actor no disponible'}{event.reason ? ` · Motivo: ${event.reason}` : event.type === 'VOIDED' ? ' · Sin motivo' : ''}{event.related_claim_id ? <> · <Link to={`/cobros/${event.related_claim_id}`}>Cobro relacionado {event.related_claim_id}</Link></> : null}</span></li>)}</ol></Card>}

      {entry.claim.status === 'PAID' && <Card className="claim-action-card"><div className="section-header"><div><h2>Anular cobro</h2><p>Se conserva el cobro original y se registra una salida inversa por el importe que se pagó originalmente.</p></div></div>
        {!entry.consistent ? <p className="claim-block-note" role="note">No se puede anular porque falta una cuenta o referencia coherente.</p> : intent === 'void' ? <form className="claim-confirm-form" onSubmit={(event) => { event.preventDefault(); void runOperation() }}>
          <div className="claim-balance-preview"><span>Alumno</span><strong>{entry.student?.name}</strong><span>Actividad</span><strong>{activityName(entry.activity, entry.claim.activity_id)}</strong><span>Importe original e inverso</span><strong>{entry.claim.reward_amount.toLocaleString('es-MX')} Áureos</strong><span>Saldo actual</span><strong>{entry.account?.balance.toLocaleString('es-MX')} Áureos</strong><span>Saldo resultante</span><strong className={voidPreview !== null && voidPreview < 0 ? 'is-negative' : ''}>{voidPreview?.toLocaleString('es-MX')} Áureos</strong></div>
          {voidPreview !== null && voidPreview < 0 && <p className="claim-negative-note" role="alert">El saldo quedará negativo; esta operación se permite en la demostración.</p>}
          <label>Motivo (opcional)<textarea maxLength={240} rows={3} value={reason} onChange={(event) => setReason(event.target.value)} placeholder="Escribe el motivo o deja el campo vacío" /></label>
          <p>La acción quedará identificada como realizada desde el Panel Maestro en modo demostración.</p>
          <div className="claim-confirm-actions"><button className="button button-danger" type="submit" disabled={busy || !entry.consistent}>{busy ? 'Procesando…' : 'Confirmar anulación'}</button><button className="button button-secondary" type="button" onClick={() => setIntent(null)} disabled={busy}>Cancelar</button></div>
        </form> : <button className="button button-danger" onClick={() => { setIntent('void'); setOperationError('') }} disabled={!entry.consistent}>Anular cobro</button>}
      </Card>}

      {entry.claim.status === 'VOIDED' && <Card className="claim-action-card"><div className="section-header"><div><h2>Autorizar un nuevo cobro</h2><p>Es una confirmación separada de la anulación. La autorización no cambia saldos ni crea movimientos.</p></div></div>
        {!entry.consistent ? <p className="claim-block-note" role="note">No se puede autorizar porque falta una cuenta o referencia coherente.</p> : eligibility ? <p className="claim-block-note" role="note">{eligibility}</p> : pendingAuthorization ? <>
          <div className="claim-authorization-preview"><strong>Autorización {pendingAuthorization.id} disponible</strong><span>Recompensa vigente de {activityName(entry.activity, entry.claim.activity_id)}: {entry.activity?.reward_amount.toLocaleString('es-MX')} Áureos.</span><p>Confirma el importe actual para registrar un cobro nuevo. El importe histórico anulado de {entry.claim.reward_amount.toLocaleString('es-MX')} Áureos se conserva.</p></div>
          {intent === 'simulate' ? <div className="claim-confirm-actions"><button className="button button-primary" onClick={() => void runOperation()} disabled={busy || !entry.consistent || Boolean(eligibility)}>{busy ? 'Procesando…' : `Confirmar simulación · ${entry.activity?.reward_amount.toLocaleString('es-MX')} Áureos`}</button><button className="button button-secondary" onClick={() => setIntent(null)} disabled={busy}>Cancelar</button></div> : <button className="button button-primary" onClick={() => { setIntent('simulate'); setOperationError('') }} disabled={!entry.consistent || Boolean(eligibility)}><RotateCcw size={16} />Simular nuevo cobro</button>}
        </> : intent === 'authorize' ? <div className="claim-authorization-preview"><strong>¿Confirmar autorización de nuevo cobro?</strong><span>{entry.student?.name} · {activityName(entry.activity, entry.claim.activity_id)}</span><span>La recompensa vigente es {entry.activity?.reward_amount.toLocaleString('es-MX')} Áureos.</span><p>No se crearán movimientos ni se modificará el saldo hasta ejecutar por separado “Simular nuevo cobro”. La elegibilidad se volverá a validar al cobrar.</p><div className="claim-confirm-actions"><button className="button button-primary" onClick={() => void runOperation()} disabled={busy || !entry.consistent || Boolean(eligibility)}>{busy ? 'Procesando…' : 'Confirmar autorización'}</button><button className="button button-secondary" onClick={() => setIntent(null)} disabled={busy}>Cancelar</button></div></div> : <>
          <p>Alumno: <strong>{entry.student?.name}</strong> · Actividad: <strong>{activityName(entry.activity, entry.claim.activity_id)}</strong> · Recompensa vigente: <strong>{entry.activity?.reward_amount.toLocaleString('es-MX')} Áureos</strong>.</p>
          <button className="button button-secondary" onClick={() => { setIntent('authorize'); setOperationError('') }} disabled={!entry.consistent || Boolean(eligibility)}>Autorizar nuevo cobro</button>
        </>}
      </Card>}
    </>}
  </>
}
