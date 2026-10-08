import { useEffect, useState } from 'react'
import { Link } from 'react-router-dom'
import { WalletCards } from 'lucide-react'
import { Card, EmptyState, PageHeader } from '../components/ui'
import { StudentAvatar } from '../components/StudentAvatar'
import { demoPanelService } from '../services/demoPanelService'
import type { PanelDataService } from '../services/PanelDataService'
import { filterAccounts, loadAccountDirectory, type AccountDirectoryEntry } from '../services/accountQueries'

type LoadState = { status: 'loading' } | { status: 'error' } | { status: 'ready'; entries: AccountDirectoryEntry[] }

export function AccountsPage({ dataService = demoPanelService }: { dataService?: PanelDataService }) {
  const [state, setState] = useState<LoadState>({ status: 'loading' })
  const [attempt, setAttempt] = useState(0)
  const [query, setQuery] = useState('')
  useEffect(() => {
    let current = true
    setState({ status: 'loading' })
    loadAccountDirectory(dataService).then((entries) => { if (current) setState({ status: 'ready', entries }) }).catch(() => { if (current) setState({ status: 'error' }) })
    return () => { current = false }
  }, [dataService, attempt])
  const visible = state.status === 'ready' ? filterAccounts(state.entries, query) : []
  return <>
    <PageHeader eyebrow="GESTIÓN · SOLO LECTURA" title="Cuentas" description="Consulta de cuentas ficticias y saldos directos en Áureos." />
    <div className="dashboard-demo-note" role="note">Datos ficticios de PM.3. El saldo se obtiene directamente de la cuenta; no se recalcula desde movimientos.</div>
    {state.status === 'loading' && <Card className="directory-state" role="status"><span className="status-dot" />Cargando cuentas de demostración…</Card>}
    {state.status === 'error' && <Card className="directory-state directory-error" role="alert"><div><strong>No se pudieron cargar las cuentas.</strong><p>La consulta falló; no se presenta como una colección vacía.</p></div><button className="button button-primary" onClick={() => setAttempt((value) => value + 1)}>Reintentar</button></Card>}
    {state.status === 'ready' && state.entries.length === 0 && <Card><EmptyState title="No hay alumnos en la consulta" description="La consulta terminó correctamente, pero no devolvió alumnos." icon={WalletCards} /></Card>}
    {state.status === 'ready' && state.entries.length > 0 && <>
      <Card className="record-filter-card"><label>Buscar por nombre<input type="search" value={query} onChange={(event) => setQuery(event.target.value)} placeholder="Nombre del alumno" /></label><span role="status" aria-live="polite">{visible.length} de {state.entries.length} alumnos</span><button className="button button-secondary" onClick={() => setQuery('')} disabled={!query}>Limpiar búsqueda</button></Card>
      {visible.length === 0 ? <Card><EmptyState title="Sin coincidencias" description="No hay alumnos que coincidan con la búsqueda." action={<button className="button button-secondary" onClick={() => setQuery('')}>Limpiar búsqueda</button>} /></Card> : <div className="account-list" aria-label="Cuentas y situación de cuenta">
        {visible.map(({ student, account }) => <article className="account-row" key={student.student_id}>
          <StudentAvatar student={student} /><div className="account-student">{account ? <Link to={`/cuentas/${account.student_id}`}><strong>{student.name}</strong></Link> : <strong>{student.name}</strong>}<Link className="account-profile-link" to={`/alumnos/${student.student_id}`}>Ver alumno</Link><span>{student.grade}° grado · Grupo {student.group}</span></div>
          {account ? <><div className="account-balance"><span>Saldo actual</span><strong>{account.balance.toLocaleString('es-MX')} Áureos</strong></div><Link className="button button-secondary account-open" to={`/cuentas/${account.student_id}`}>Ver cuenta</Link></> : <><div className="account-balance account-missing"><span>Situación</span><strong>Sin cuenta</strong></div><span className="account-open account-open-empty" aria-hidden="true">—</span></>}
        </article>)}
      </div>}
    </>}
  </>
}
