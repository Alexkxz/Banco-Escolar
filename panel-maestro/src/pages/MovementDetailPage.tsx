import { useEffect, useState } from 'react'
import { Link, useParams } from 'react-router-dom'
import { ArrowLeft, ArrowDownRight, ArrowUpRight, ReceiptText } from 'lucide-react'
import { Card, EmptyState, PageHeader } from '../components/ui'
import { demoPanelService } from '../services/demoPanelService'
import type { PanelDataService } from '../services/PanelDataService'
import { loadMovementDirectory, movementDateFormatter, PANEL_TIME_ZONE, type MovementListEntry } from '../services/movementQueries'

type LoadState = { status: 'loading' } | { status: 'error' } | { status: 'ready'; entry: MovementListEntry | null }

export function MovementDetailPage({ dataService = demoPanelService }: { dataService?: PanelDataService }) {
  const { movementId: rawId } = useParams()
  const movementId = /^\d+$/.test(rawId ?? '') ? Number(rawId) : NaN
  const [state, setState] = useState<LoadState>({ status: 'loading' })
  const [attempt, setAttempt] = useState(0)
  useEffect(() => {
    let current = true
    setState({ status: 'loading' })
    loadMovementDirectory(dataService).then((entries) => { if (current) setState({ status: 'ready', entry: entries.find(({ movement }) => movement.id === movementId) ?? null }) }).catch(() => { if (current) setState({ status: 'error' }) })
    return () => { current = false }
  }, [dataService, attempt, movementId])
  return <>
    <PageHeader eyebrow="GESTIÓN · SOLO LECTURA" title="Detalle de movimiento" description={`Registro de demostración · fechas ${PANEL_TIME_ZONE}.`} />
    {state.status === 'loading' && <Card className="directory-state" role="status"><span className="status-dot" />Cargando movimiento…</Card>}
    {state.status === 'error' && <Card className="directory-state directory-error" role="alert"><div><strong>No se pudo consultar el movimiento.</strong><p>El error no se interpreta como un registro inexistente.</p></div><button className="button button-primary" onClick={() => setAttempt((value) => value + 1)}>Reintentar</button></Card>}
    {state.status === 'ready' && !state.entry && <Card><EmptyState title="Movimiento inexistente" description="No se encontró ese ID en el historial disponible." icon={ReceiptText} action={<Link className="button button-secondary" to="/movimientos">Regresar a movimientos</Link>} /></Card>}
    {state.status === 'ready' && state.entry && (() => {
      const { movement, student, claim, activity } = state.entry!
      const incoming = movement.type === 'ENTRY'
      return <><Link className="back-link" to="/movimientos"><ArrowLeft size={16} />Regresar a movimientos</Link><Card className="movement-detail-card">
        <div className="movement-detail-heading"><span className={`movement-detail-icon ${incoming ? 'is-credit' : 'is-debit'}`}>{incoming ? <ArrowUpRight /> : <ArrowDownRight />}</span><div><span className="eyebrow">Movimiento · ID {movement.id}</span><h2>{movement.reason}</h2><p>{incoming ? 'Entrada' : 'Salida'} de {Math.abs(movement.amount).toLocaleString('es-MX')} Áureos</p></div></div>
        <dl className="movement-detail-fields"><div><dt>Alumno</dt><dd>{student ? <Link to={`/alumnos/${student.student_id}`}>{student.name}</Link> : `Alumno ${movement.student_id}`}</dd></div><div><dt>Fecha · {PANEL_TIME_ZONE}</dt><dd><time dateTime={new Date(movement.timestamp * 1000).toISOString()}>{movementDateFormatter.format(movement.timestamp * 1000)}</time></dd></div><div><dt>Cuenta</dt><dd><Link to={`/cuentas/${movement.student_id}`}>Consultar cuenta</Link></dd></div><div><dt>Origen de la muestra</dt><dd>{movement.origin === 'DEMO' ? 'Demostración' : movement.origin}</dd></div>
          {claim && <>{activity && <div><dt>Actividad vinculada</dt><dd><Link to={`/actividades/${activity.id}`}>Actividad {activity.id}</Link></dd></div>}<div><dt>Cobro vinculado</dt><dd><Link to={`/cobros/${claim.id}`}>Cobro {claim.id} · {claim.status === 'PAID' ? 'Pagado' : 'Anulado'}</Link></dd></div>{movement.related_movement_id && <div><dt>Movimiento original vinculado</dt><dd><Link to={`/movimientos/${movement.related_movement_id}`}>Movimiento {movement.related_movement_id}</Link></dd></div>}</>}
        </dl>
      </Card></>
    })()}
  </>
}
