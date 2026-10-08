import { useEffect, useMemo, useState } from 'react'
import { Link } from 'react-router-dom'
import { Search, TicketCheck } from 'lucide-react'
import { Card, EmptyState, PageHeader } from '../components/ui'
import { demoPanelService } from '../services/demoPanelService'
import { filterClaims, loadClaimSnapshot, movementDateFormatter, PANEL_TIME_ZONE, type ClaimFilters, type ClaimListEntry, type ClaimQueryService } from '../services/claimQueries'

const emptyFilters: ClaimFilters = { studentId: '', activityId: '', status: 'all', from: '', through: '' }
type State = { status: 'loading' } | { status: 'error' } | { status: 'ready'; entries: ClaimListEntry[]; students: Awaited<ReturnType<ClaimQueryService['getStudents']>>; activities: Awaited<ReturnType<ClaimQueryService['getActivities']>> }

export function ClaimsPage({ service = demoPanelService }: { service?: ClaimQueryService }) {
  const [state, setState] = useState<State>({ status: 'loading' })
  const [attempt, setAttempt] = useState(0)
  const [filters, setFilters] = useState(emptyFilters)
  useEffect(() => {
    let current = true
    setState({ status: 'loading' })
    loadClaimSnapshot(service).then(({ entries, students, activities }) => { if (current) setState({ status: 'ready', entries, students, activities }) }).catch(() => { if (current) setState({ status: 'error' }) })
    return () => { current = false }
  }, [service, attempt])
  const dateError = useMemo(() => { try { filterClaims([], filters); return '' } catch (error) { return error instanceof Error ? error.message : 'El rango de fechas no es válido.' } }, [filters])
  const results = state.status === 'ready' && !dateError ? filterClaims(state.entries, filters) : []
  const clear = () => setFilters(emptyFilters)

  return <>
    <PageHeader eyebrow="GESTIÓN · DEMOSTRACIÓN" title="Cobros" description="Consulta de reclamos y acciones temporales de demostración." />
    <div className="dashboard-demo-note" role="note">Datos ficticios. Anulaciones, autorizaciones y nuevos cobros se conservan solo en memoria, se pierden al recargar y no se envían a la terminal. Fechas en {PANEL_TIME_ZONE}.</div>
    {state.status === 'loading' && <Card className="directory-state" role="status"><span className="status-dot" />Cargando cobros…</Card>}
    {state.status === 'error' && <Card className="directory-state directory-error" role="alert"><div><strong>No se pudieron consultar los cobros.</strong><p>El error de consulta no se interpreta como una colección vacía.</p></div><button className="button button-primary" onClick={() => setAttempt((value) => value + 1)}>Reintentar</button></Card>}
    {state.status === 'ready' && state.entries.length === 0 && <Card><EmptyState title="Sin cobros registrados" description="La consulta terminó correctamente y no devolvió reclamos." icon={TicketCheck} /></Card>}
    {state.status === 'ready' && state.entries.length > 0 && <>
      <Card className="claim-filters">
        <label>Alumno<select value={filters.studentId} onChange={(event) => setFilters((current) => ({ ...current, studentId: event.target.value }))}><option value="">Todos los alumnos</option>{state.students.map((student) => <option key={student.student_id} value={student.student_id}>{student.name}</option>)}</select></label>
        <label>Actividad<select value={filters.activityId} onChange={(event) => setFilters((current) => ({ ...current, activityId: event.target.value }))}><option value="">Todas las actividades</option>{state.activities.map((activity) => <option key={activity.id} value={activity.id}>{activity.activity_number_enabled ? `Actividad ${activity.activity_number}` : `Actividad ${activity.id}`}</option>)}</select></label>
        <label>Estado<select value={filters.status} onChange={(event) => setFilters((current) => ({ ...current, status: event.target.value as ClaimFilters['status'] }))}><option value="all">Todos</option><option value="PAID">Pagados</option><option value="VOIDED">Anulados</option></select></label>
        <label>Desde ({PANEL_TIME_ZONE})<input type="date" value={filters.from} onChange={(event) => setFilters((current) => ({ ...current, from: event.target.value }))} /></label>
        <label>Hasta ({PANEL_TIME_ZONE})<input type="date" value={filters.through} onChange={(event) => setFilters((current) => ({ ...current, through: event.target.value }))} /></label>
        <div className="claim-filter-footer"><span role="status" aria-live="polite">{dateError ? 'Rango de fechas no válido' : `${results.length} de ${state.entries.length} cobros`}</span><button className="button button-secondary" onClick={clear} disabled={JSON.stringify(filters) === JSON.stringify(emptyFilters)}>Limpiar filtros</button></div>
        {dateError && <p className="date-range-error" role="alert">{dateError}</p>}
      </Card>
      {!dateError && results.length === 0 ? <Card><EmptyState title="Sin coincidencias" description="No hay cobros que coincidan con los filtros seleccionados." icon={Search} action={<button className="button button-secondary" onClick={clear}>Limpiar filtros</button>} /></Card>
        : results.length > 0 && <div className="claim-list" role="list" aria-label="Cobros de demostración">{results.map(({ claim, student, activity }) => <article className="claim-list-row" role="listitem" key={claim.id}>
          <div className="claim-list-icon" aria-hidden="true"><TicketCheck size={20} /></div>
          <div className="claim-list-main"><Link to={`/cobros/${claim.id}`}>{student?.name ?? `Alumno ${claim.student_id}`}</Link><span>{activity ? (activity.activity_number_enabled ? `Actividad ${activity.activity_number}` : `Actividad ${activity.id}`) : `Actividad ${claim.activity_id}`} · Cobro {claim.id}</span></div>
          <strong className="claim-list-amount">{claim.reward_amount.toLocaleString('es-MX')} Áureos</strong>
          <time dateTime={new Date(claim.claimed_at * 1000).toISOString()}>{movementDateFormatter.format(claim.claimed_at * 1000)}</time>
          <span className={`claim-status ${claim.status === 'PAID' ? 'is-paid' : 'is-voided'}`}>{claim.status === 'PAID' ? 'Pagado' : 'Anulado'}</span>
          <Link className="button button-secondary" to={`/cobros/${claim.id}`}>Ver cobro</Link>
        </article>)}</div>}
    </>}
  </>
}
