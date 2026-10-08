import { useEffect, useMemo, useState } from 'react'
import { Link } from 'react-router-dom'
import { Activity, Plus } from 'lucide-react'
import { Card, EmptyState, PageHeader } from '../components/ui'
import { DemoPanelDataService, canEditDemoActivity } from '../services/DemoPanelDataService'
import { demoPanelService } from '../services/demoPanelService'
import type { PanelDataService } from '../services/PanelDataService'
import { activityDateFormatter, activityDisplayName, filterAndSortActivities, type ActivitySort, type ActivityStatusFilter } from '../services/activityQueries'
import type { ActivityClaim, ActivitySession } from '../models/domain'

type DemoService = PanelDataService & DemoPanelDataService
type LoadState = { status: 'loading' } | { status: 'error' } | { status: 'ready'; activities: readonly ActivitySession[]; claims: readonly ActivityClaim[] }

export function ActivitiesPage({ service = demoPanelService }: { service?: DemoService }) {
  const [state, setState] = useState<LoadState>({ status: 'loading' })
  const [attempt, setAttempt] = useState(0)
  const [query, setQuery] = useState('')
  const [status, setStatus] = useState<ActivityStatusFilter>('all')
  const [sort, setSort] = useState<ActivitySort>('started')
  useEffect(() => {
    let current = true
    setState({ status: 'loading' })
    Promise.all([service.getActivities(), service.getClaims()])
      .then(([activities, claims]) => { if (current) setState({ status: 'ready', activities, claims }) })
      .catch(() => { if (current) setState({ status: 'error' }) })
    return () => { current = false }
  }, [service, attempt])
  const results = useMemo(() => state.status === 'ready' ? filterAndSortActivities(state.activities, query, status, sort) : [], [state, query, status, sort])
  const clear = () => { setQuery(''); setStatus('all'); setSort('started') }
  const filtersDirty = Boolean(query) || status !== 'all' || sort !== 'started'

  return <>
    <PageHeader eyebrow="GESTIÓN · DEMOSTRACIÓN" title="Actividades" description="Consulta y administra sesiones temporales de demostración." />
    <div className="dashboard-demo-note" role="note">Los cambios se conservan solo en memoria durante esta navegación, se pierden al recargar y no se envían a la terminal.</div>
    {state.status === 'loading' && <Card className="directory-state" role="status"><span className="status-dot" />Cargando actividades…</Card>}
    {state.status === 'error' && <Card className="directory-state directory-error" role="alert"><div><strong>No se pudieron consultar las actividades.</strong><p>El error de consulta no se presenta como una colección vacía.</p></div><button className="button button-primary" onClick={() => setAttempt((value) => value + 1)}>Reintentar</button></Card>}
    {state.status === 'ready' && state.activities.length === 0 && <Card><EmptyState title="No hay actividades de demostración" description="La consulta terminó correctamente y no devolvió actividades." icon={Activity} /></Card>}
    {state.status === 'ready' && state.activities.length > 0 && <>
      <Card className="activity-filters">
        <label>Buscar por nombre<input type="search" value={query} onChange={(event) => setQuery(event.target.value)} placeholder="Actividad 1" /></label>
        <label>Estado<select value={status} onChange={(event) => setStatus(event.target.value as ActivityStatusFilter)}><option value="all">Todos los estados</option><option value="ACTIVE">Activas</option><option value="FINISHED">Finalizadas</option><option value="CANCELLED">Canceladas</option></select></label>
        <label>Ordenar por<select value={sort} onChange={(event) => setSort(event.target.value as ActivitySort)}><option value="started">Inicio más reciente</option><option value="closed">Cierre más reciente</option></select></label>
        <p className="activity-sort-note">Al ordenar por cierre, las actividades sin fecha de cierre aparecen al final.</p>
        <div className="activity-filter-footer"><span role="status" aria-live="polite">{results.length} de {state.activities.length} actividades</span><button className="button button-secondary" disabled={!filtersDirty} onClick={clear}>Limpiar filtros</button><Link className="button button-primary activity-create-link" to="/actividades/nueva"><Plus size={16} />Crear actividad</Link></div>
      </Card>
      {results.length === 0 ? <Card><EmptyState title="Sin coincidencias" description="No hay actividades que coincidan con la búsqueda y el estado seleccionados." action={<button className="button button-secondary" onClick={clear}>Limpiar filtros</button>} /></Card> : <div className="activity-list" aria-label="Lista de actividades">
        {results.map((activity) => {
          const claimCount = state.claims.filter((claim) => claim.activity_id === activity.id).length
          const editable = canEditDemoActivity(activity, state.claims)
          return <article className="activity-list-row" key={activity.id}>
            <span className={`activity-list-icon status-${activity.status.toLowerCase()}`}><Activity size={20} /></span>
            <div className="activity-list-main"><Link to={`/actividades/${activity.id}`}><strong>{activityDisplayName(activity)}</strong></Link><span>Recompensa: {activity.reward_amount.toLocaleString('es-MX')} Áureos · {activity.participant_mode === 'PARTICIPANTS_DISABLED' ? 'Sin filtro previo de participantes' : `${activity.participant_student_ids.length} alumnos en la lista`}</span></div>
            <div className="activity-list-date"><span>Inicio</span><time dateTime={new Date(activity.started_at * 1000).toISOString()}>{activityDateFormatter.format(activity.started_at * 1000)}</time></div>
            <span className={`activity-status status-${activity.status.toLowerCase()}`}>{activity.status === 'ACTIVE' ? 'Activa' : activity.status === 'FINISHED' ? 'Finalizada' : 'Cancelada'}</span>
            <div className="activity-list-actions"><Link className="button button-secondary" to={`/actividades/${activity.id}`}>Ver detalle</Link>{editable && <Link className="button button-quiet" to={`/actividades/${activity.id}/editar`}>Editar</Link>}{claimCount > 0 && <small>{claimCount} reclamo(s)</small>}</div>
          </article>
        })}
      </div>}
    </>}
  </>
}
