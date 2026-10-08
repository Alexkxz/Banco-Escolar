import { useEffect, useMemo, useState } from 'react'
import { Link } from 'react-router-dom'
import { ArrowDownRight, ArrowUpRight, Search } from 'lucide-react'
import { Card, EmptyState, PageHeader } from '../components/ui'
import { demoPanelService } from '../services/demoPanelService'
import type { PanelDataService } from '../services/PanelDataService'
import { filterMovements, loadMovementSnapshot, movementDateFormatter, PANEL_TIME_ZONE, type MovementFilters, type MovementListEntry } from '../services/movementQueries'

const cleanFilters: MovementFilters = { studentId: '', type: 'all', from: '', through: '' }
type LoadState = { status: 'loading' } | { status: 'error' } | { status: 'ready'; entries: MovementListEntry[]; students: Awaited<ReturnType<PanelDataService['getStudents']>> }

export function MovementsPage({ dataService = demoPanelService }: { dataService?: PanelDataService }) {
  const [state, setState] = useState<LoadState>({ status: 'loading' })
  const [attempt, setAttempt] = useState(0)
  const [filters, setFilters] = useState<MovementFilters>(cleanFilters)
  useEffect(() => {
    let current = true
    setState({ status: 'loading' })
    loadMovementSnapshot(dataService).then(({ entries, students }) => { if (current) setState({ status: 'ready', entries, students }) }).catch(() => { if (current) setState({ status: 'error' }) })
    return () => { current = false }
  }, [dataService, attempt])
  const students = state.status === 'ready' ? state.students : []
  const dateError = useMemo(() => {
    try { filterMovements([], filters); return '' } catch (error) { return error instanceof Error ? error.message : 'Rango de fechas inválido.' }
  }, [filters])
  const results = state.status === 'ready' && !dateError ? filterMovements(state.entries, filters) : []
  const clear = () => setFilters(cleanFilters)

  return <>
    <PageHeader eyebrow="GESTIÓN · SOLO LECTURA" title="Movimientos" description="Entradas y salidas ficticias ordenadas por fecha." />
    <div className="dashboard-demo-note" role="note">Datos ficticios de solo lectura. Fechas mostradas en {PANEL_TIME_ZONE}.</div>
    {state.status === 'loading' && <Card className="directory-state" role="status"><span className="status-dot" />Cargando movimientos…</Card>}
    {state.status === 'error' && <Card className="directory-state directory-error" role="alert"><div><strong>No se pudieron cargar los movimientos.</strong><p>La consulta falló; no se trata como un historial vacío.</p></div><button className="button button-primary" onClick={() => setAttempt((value) => value + 1)}>Reintentar</button></Card>}
    {state.status === 'ready' && state.entries.length === 0 && <Card><EmptyState title="Historial sin movimientos" description="La consulta terminó correctamente y no devolvió movimientos." /></Card>}
    {state.status === 'ready' && state.entries.length > 0 && <>
      <Card className="movement-filters">
        <label>Alumno<select value={filters.studentId} onChange={(event) => setFilters((current) => ({ ...current, studentId: event.target.value }))}><option value="">Todos los alumnos</option>{students.map((student) => <option key={student.student_id} value={student.student_id}>{student.name}</option>)}</select></label>
        <label>Tipo<select value={filters.type} onChange={(event) => setFilters((current) => ({ ...current, type: event.target.value as MovementFilters['type'] }))}><option value="all">Entradas y salidas</option><option value="ENTRY">Entradas</option><option value="EXIT">Salidas</option></select></label>
        <label>Desde ({PANEL_TIME_ZONE})<input type="date" value={filters.from} onChange={(event) => setFilters((current) => ({ ...current, from: event.target.value }))} /></label>
        <label>Hasta ({PANEL_TIME_ZONE})<input type="date" value={filters.through} onChange={(event) => setFilters((current) => ({ ...current, through: event.target.value }))} /></label>
        <div className="movement-filter-footer"><span role="status" aria-live="polite">{dateError ? 'Rango de fechas no válido' : `${results.length} de ${state.entries.length} movimientos`}</span><button className="button button-secondary" onClick={clear} disabled={JSON.stringify(filters) === JSON.stringify(cleanFilters)}>Limpiar filtros</button></div>
        {dateError && <p className="date-range-error" role="alert">{dateError}</p>}
      </Card>
      {!dateError && results.length === 0 ? <Card><EmptyState title="Sin coincidencias" description="No hay movimientos que coincidan con los filtros seleccionados." icon={Search} action={<button className="button button-secondary" onClick={clear}>Limpiar filtros</button>} /></Card> : results.length > 0 && <div className="movement-table" role="table" aria-label="Historial de movimientos"><div className="movement-table-head" role="row"><span role="columnheader">Alumno</span><span role="columnheader">Concepto</span><span role="columnheader">Tipo e importe</span><span role="columnheader">Fecha · {PANEL_TIME_ZONE}</span></div>{results.map(({ movement, student, claim, activity }) => <article role="row" className="movement-table-row" key={movement.id}>
        <span role="cell"><Link to={`/alumnos/${movement.student_id}`}>{student?.name ?? `Alumno ${movement.student_id}`}</Link></span><span role="cell" className="movement-concept"><Link to={`/movimientos/${movement.id}`}>{movement.reason}</Link>{claim && <small>{activity && <><Link to={`/actividades/${activity.id}`}>Actividad {activity.id}</Link><span> · </span></>}<Link to={`/cobros/${claim.id}`}>Cobro {claim.id}{claim.status === 'VOIDED' ? ' · anulado' : ''}</Link></small>}</span><strong role="cell" className={`movement-amount ${movement.type === 'ENTRY' ? 'is-credit' : 'is-debit'}`}>{movement.type === 'ENTRY' ? <ArrowUpRight size={15} /> : <ArrowDownRight size={15} />}{movement.type === 'ENTRY' ? 'Entrada' : 'Salida'} · {Math.abs(movement.amount).toLocaleString('es-MX')} Áureos</strong><time role="cell" dateTime={new Date(movement.timestamp * 1000).toISOString()}>{movementDateFormatter.format(movement.timestamp * 1000)}</time>
      </article>)}</div>}
    </>}
  </>
}
