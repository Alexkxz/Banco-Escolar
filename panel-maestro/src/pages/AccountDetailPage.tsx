import { useEffect, useState } from 'react'
import { Link, useParams } from 'react-router-dom'
import { ArrowLeft, ArrowDownRight, ArrowUpRight, WalletCards } from 'lucide-react'
import { Card, EmptyState, PageHeader } from '../components/ui'
import { StudentAvatar } from '../components/StudentAvatar'
import { MovementSummaryList } from '../components/MovementSummaryList'
import { demoPanelService } from '../services/demoPanelService'
import type { PanelDataService } from '../services/PanelDataService'
import { loadAccountDetail, summarizeMovementHistory, type AccountDetail } from '../services/accountQueries'

type LoadState = { status: 'loading' } | { status: 'error' } | { status: 'ready'; detail: AccountDetail | null }

export function AccountDetailPage({ dataService = demoPanelService }: { dataService?: PanelDataService }) {
  const { accountId: rawId } = useParams()
  const accountId = /^\d+$/.test(rawId ?? '') ? Number(rawId) : NaN
  const [state, setState] = useState<LoadState>({ status: 'loading' })
  const [attempt, setAttempt] = useState(0)
  useEffect(() => {
    let current = true
    setState({ status: 'loading' })
    loadAccountDetail(dataService, accountId).then((detail) => { if (current) setState({ status: 'ready', detail }) }).catch(() => { if (current) setState({ status: 'error' }) })
    return () => { current = false }
  }, [dataService, attempt, accountId])
  return <>
    <PageHeader eyebrow="GESTIÓN · SOLO LECTURA" title="Detalle de cuenta" description="Saldo de cuenta y movimientos disponibles para consulta." />
    {state.status === 'loading' && <Card className="directory-state" role="status"><span className="status-dot" />Cargando cuenta…</Card>}
    {state.status === 'error' && <Card className="directory-state directory-error" role="alert"><div><strong>No se pudo consultar la cuenta.</strong><p>El error de consulta no se interpreta como saldo ni historial en cero.</p></div><button className="button button-primary" onClick={() => setAttempt((value) => value + 1)}>Reintentar</button></Card>}
    {state.status === 'ready' && !state.detail && <Card><EmptyState title="Cuenta inexistente" description="No se encontró una cuenta con ese ID. Un alumno sin cuenta no tiene un detalle ni un saldo asignado." icon={WalletCards} action={<Link className="button button-secondary" to="/cuentas">Regresar a cuentas</Link>} /></Card>}
    {state.status === 'ready' && state.detail && <>
      <Link className="back-link" to="/cuentas"><ArrowLeft size={16} />Regresar a cuentas</Link>
      <Card className="account-detail-card"><div className="account-detail-identity"><StudentAvatar student={state.detail.student} size="large" /><div><span className="eyebrow">Cuenta · ID de alumno {state.detail.account.student_id}</span><h2><Link to={`/alumnos/${state.detail.student.student_id}`}>{state.detail.student.name}</Link></h2><p>{state.detail.student.grade}° grado · Grupo {state.detail.student.group}</p></div></div><div className="account-detail-balance"><span>Saldo actual de la cuenta</span><strong>{state.detail.account.balance.toLocaleString('es-MX')} <small>Áureos</small></strong></div></Card>
      <Card className="account-history"><div className="section-header"><div><h2>Resumen del historial disponible</h2><p>Solo suma los movimientos devueltos por esta consulta; no necesariamente es todo el historial de la cuenta.</p></div></div>
        {(() => { const totals = summarizeMovementHistory(state.detail!.movements); return <div className="history-totals"><div><span><ArrowUpRight size={15} />Entradas disponibles</span><strong>{totals.entries.toLocaleString('es-MX')} Áureos</strong></div><div><span><ArrowDownRight size={15} />Salidas disponibles</span><strong>{totals.exits.toLocaleString('es-MX')} Áureos</strong></div></div> })()}
        {state.detail.movements.length === 0 ? <EmptyState title="Sin movimientos en el historial consultado" description="La consulta terminó correctamente sin devolver movimientos. Los totales disponibles son 0 Áureos." /> : <MovementSummaryList entries={state.detail.movementEntries.map((entry) => ({ ...entry, student: state.detail!.student }))} label="Movimientos de la cuenta" />}
      </Card>
    </>}
  </>
}
