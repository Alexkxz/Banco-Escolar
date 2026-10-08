import { useEffect, useMemo, useState } from 'react'
import { Link } from 'react-router-dom'
import { Search, Users } from 'lucide-react'
import { Button, Card, EmptyState, PageHeader } from '../components/ui'
import { StudentAvatar } from '../components/StudentAvatar'
import { demoPanelService } from '../services/demoPanelService'
import type { PanelDataService } from '../services/PanelDataService'
import { filterStudents, loadStudentDirectory, type StudentDirectoryEntry, type StudentFilters } from '../services/studentDirectory'

type LoadState = { status: 'loading' } | { status: 'error' } | { status: 'ready'; entries: StudentDirectoryEntry[] }
const emptyFilters: StudentFilters = { query: '', grade: '', group: '', account: 'all' }

export function StudentsDirectoryPage({ dataService = demoPanelService }: { dataService?: PanelDataService }) {
  const [state, setState] = useState<LoadState>({ status: 'loading' })
  const [attempt, setAttempt] = useState(0)
  const [filters, setFilters] = useState<StudentFilters>(emptyFilters)
  useEffect(() => {
    let current = true
    setState({ status: 'loading' })
    loadStudentDirectory(dataService).then((entries) => { if (current) setState({ status: 'ready', entries }) }).catch(() => { if (current) setState({ status: 'error' }) })
    return () => { current = false }
  }, [dataService, attempt])
  const grades = useMemo(() => state.status === 'ready' ? [...new Set(state.entries.map(({ student }) => student.grade))].sort((a, b) => a - b) : [], [state])
  const groups = useMemo(() => state.status === 'ready' ? [...new Set(state.entries.map(({ student }) => student.group))].sort() : [], [state])
  const visible = state.status === 'ready' ? filterStudents(state.entries, filters) : []
  const clearFilters = () => setFilters(emptyFilters)

  return <>
    <PageHeader eyebrow="GESTIÓN · SOLO LECTURA" title="Alumnos" description="Directorio de perfiles ficticios del entorno de demostración." />
    <div className="dashboard-demo-note" role="note">Datos ficticios. “Sin cuenta” significa que no existe una cuenta asignada; no representa un saldo de cero.</div>
    {state.status === 'loading' && <Card className="directory-state" role="status"><span className="status-dot" />Cargando directorio de demostración…</Card>}
    {state.status === 'error' && <Card className="directory-state directory-error" role="alert"><div><strong>No se pudo cargar el directorio.</strong><p>La consulta falló; no se interpreta como una colección vacía.</p></div><Button variant="primary" onClick={() => setAttempt((value) => value + 1)}>Reintentar</Button></Card>}
    {state.status === 'ready' && state.entries.length === 0 && <Card><EmptyState title="No hay alumnos en el conjunto de demostración" description="La consulta terminó correctamente, pero la colección está vacía." icon={Users} /></Card>}
    {state.status === 'ready' && state.entries.length > 0 && <>
      <Card className="directory-filters">
        <label className="directory-search"><span>Buscar por nombre</span><span className="input-with-icon"><Search size={17} aria-hidden="true" /><input type="search" value={filters.query} onChange={(event) => setFilters((current) => ({ ...current, query: event.target.value }))} placeholder="Nombre del alumno" /></span></label>
        <label>Grado<select value={filters.grade} onChange={(event) => setFilters((current) => ({ ...current, grade: event.target.value }))}><option value="">Todos</option>{grades.map((grade) => <option key={grade} value={grade}>{grade}°</option>)}</select></label>
        <label>Grupo<select value={filters.group} onChange={(event) => setFilters((current) => ({ ...current, group: event.target.value }))}><option value="">Todos</option>{groups.map((group) => <option key={group}>{group}</option>)}</select></label>
        <label>Cuenta<select value={filters.account} onChange={(event) => setFilters((current) => ({ ...current, account: event.target.value as StudentFilters['account'] }))}><option value="all">Todas</option><option value="assigned">Con cuenta</option><option value="missing">Sin cuenta</option></select></label>
        <div className="directory-filter-bottom"><span role="status" aria-live="polite">{visible.length} de {state.entries.length} alumnos</span><Button onClick={clearFilters} disabled={JSON.stringify(filters) === JSON.stringify(emptyFilters)}>Limpiar filtros</Button></div>
      </Card>
      {visible.length === 0 ? <Card><EmptyState title="Sin coincidencias" description="No hay alumnos que coincidan con la búsqueda y los filtros seleccionados." icon={Search} action={<Button onClick={clearFilters}>Limpiar filtros</Button>} /></Card> : <div className="student-grid" aria-label="Resultados de alumnos">{visible.map(({ student, hasAccount }) => <Link className="student-card" to={`/alumnos/${student.student_id}`} key={student.student_id}>
        <StudentAvatar student={student} /><span className="student-card-main"><strong>{student.name}</strong><span>{student.grade}° grado · Grupo {student.group}</span></span><span className={`student-status ${student.status === 'ACTIVE' ? 'is-active' : 'is-inactive'}`}>{student.status === 'ACTIVE' ? 'Activo' : 'Inactivo'}</span><span className={`account-status ${hasAccount ? 'has-account' : ''}`}>{hasAccount ? 'Cuenta asignada' : 'Sin cuenta'}</span>
      </Link>)}</div>}
    </>}
  </>
}
