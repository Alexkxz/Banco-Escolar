import { useEffect, useState } from 'react'
import { Activity, ArrowDownRight, ArrowRight, ArrowUpRight, Coins, MonitorCog, Users, WalletCards } from 'lucide-react'
import { Link } from 'react-router-dom'
import { appRoutes } from '../app/navigation'
import type { PanelDataService } from '../services/PanelDataService'
import { demoPanelService } from '../services/demoPanelService'
import { loadDashboardSnapshot, type DashboardSnapshot } from '../services/dashboardSnapshot'
import { movementDateFormatter, PANEL_TIME_ZONE } from '../services/movementQueries'
import { Card, EmptyState, SectionHeader } from '../components/ui'
import { PageHeader } from '../components/ui'

type LoadState = { status: 'loading' } | { status: 'error' } | { status: 'ready'; snapshot: DashboardSnapshot }

const shortcuts = [
  { label: 'Alumnos', icon: Users, path: '/alumnos' },
  { label: 'Cuentas', icon: WalletCards, path: '/cuentas' },
  { label: 'Actividades', icon: Activity, path: '/actividades' },
  { label: 'Movimientos', icon: Coins, path: '/movimientos' },
]

function formatAmount(amount: number) {
  const absolute = Math.abs(amount).toLocaleString('es-MX')
  return `${amount < 0 ? '−' : '+'}${absolute} Áureos`
}

export function DevelopmentHome({ dataService = demoPanelService }: { dataService?: PanelDataService }) {
  const [loadState, setLoadState] = useState<LoadState>({ status: 'loading' })
  const [attempt, setAttempt] = useState(0)

  useEffect(() => {
    let current = true
    setLoadState({ status: 'loading' })
    loadDashboardSnapshot(dataService)
      .then((snapshot) => { if (current) setLoadState({ status: 'ready', snapshot }) })
      .catch(() => { if (current) setLoadState({ status: 'error' }) })
    return () => { current = false }
  }, [dataService, attempt])

  return <>
    <PageHeader eyebrow="VISTA GENERAL" title="Dashboard" description="Resumen del entorno ficticio de demostración. No se muestran datos escolares reales." />
    <div className="dashboard-demo-note" role="note">Datos ficticios de solo lectura. La terminal y la fuente de datos reales no están conectadas.</div>

    {loadState.status === 'loading' && <Card className="dashboard-query-state" role="status"><span className="status-dot" />Consultando el conjunto local de demostración…</Card>}

    {loadState.status === 'error' && <Card className="dashboard-query-state dashboard-query-error" role="alert">
      <div><strong>No se pudieron cargar los datos de demostración.</strong><p>El resultado de la consulta no está disponible. Intenta de nuevo.</p></div>
      <button className="button button-primary" onClick={() => setAttempt((value) => value + 1)}>Reintentar consulta</button>
    </Card>}

    {loadState.status === 'ready' && <DashboardContent snapshot={loadState.snapshot} />}
  </>
}

function DashboardContent({ snapshot }: { snapshot: DashboardSnapshot }) {
  const accountlessCount = snapshot.studentsWithoutAccount.length
  const accountlessMessage = accountlessCount === 1
    ? '1 alumno ficticio no tiene cuenta; no se le asigna saldo.'
    : `${accountlessCount} alumnos ficticios no tienen cuenta; no se les asigna saldo.`
  const metrics = [
    { id: 'students', label: 'Alumnos registrados', value: snapshot.studentCount.toLocaleString('es-MX'), note: 'Incluye alumnos sin cuenta', icon: Users, path: '/alumnos' },
    { id: 'aureos', label: 'Áureos en cuentas demo', value: snapshot.accountAureos.toLocaleString('es-MX'), note: `${snapshot.accountCount} cuentas · saldo directo`, icon: WalletCards, path: '/cuentas' },
    { id: 'active-activities', label: 'Actividades activas', value: snapshot.activeActivityCount.toLocaleString('es-MX'), note: 'Vencimiento no significa finalización', icon: Activity, path: '/actividades' },
    { id: 'movements', label: 'Movimientos del conjunto', value: snapshot.movementCount.toLocaleString('es-MX'), note: 'Conteo total · no solo los recientes', icon: Coins, path: '/movimientos' },
  ]

  return <>
    <section className="metrics-grid" aria-label="Indicadores del conjunto de demostración">
      {metrics.map(({ id, label, value, note, icon: Icon, path }) => <Link className="metric-card" to={path} key={label}>
        <span className="metric-icon"><Icon size={19} /></span><span className="metric-label">{label}</span><strong data-testid={`metric-${id}`}>{value}</strong><span className="metric-note">{note}</span><ArrowRight className="metric-arrow" size={16} />
      </Link>)}
    </section>

    <div className="dashboard-grid">
      <Card className="dashboard-activity">
        <SectionHeader title="Últimos movimientos" description={`Los cinco más recientes del conjunto · ${PANEL_TIME_ZONE}`} />
        {snapshot.recentMovements.length === 0
          ? <EmptyState title="No hay movimientos de demostración" description="La consulta terminó correctamente, pero el conjunto está vacío." />
          : <div className="movement-list" role="list" aria-label="Movimientos recientes">
            {snapshot.recentMovements.map((movement) => {
              const Arrow = movement.amount < 0 ? ArrowDownRight : ArrowUpRight
              return <article className="movement-row" role="listitem" key={movement.id}>
                <div className="movement-student"><strong>{movement.studentName}</strong><span>{movement.type === 'ENTRY' ? 'Entrada' : 'Salida'}</span></div>
                <span className="movement-reason">{movement.reason}</span>
                <strong className={`movement-amount ${movement.amount < 0 ? 'is-debit' : 'is-credit'}`}><Arrow size={15} />{formatAmount(movement.amount)}</strong>
                <time className="movement-date" dateTime={new Date(movement.timestamp * 1000).toISOString()}>{movementDateFormatter.format(movement.timestamp * 1000)}</time>
              </article>
            })}
          </div>}
      </Card>

      <Card className="system-card">
        <SectionHeader title="Estado del sistema" />
        <div className="system-row"><span>Modo actual</span><strong>Demostración</strong></div>
        <div className="system-row"><span>Terminal</span><strong>No conectada</strong></div>
        <div className="system-row"><span>Fuente de datos real</span><strong>No conectada</strong></div>
        <div className="system-row"><span>Sesión del maestro</span><strong>Sin iniciar</strong></div>
        <div className="system-row"><span>Cuentas demo</span><strong>{snapshot.accountCount}</strong></div>
        <div className="system-note"><MonitorCog size={17} /><span>{accountlessMessage}</span></div>
        <div className="system-note"><Activity size={17} /><span>Una actividad puede conservar estado activa aunque su duración haya vencido.</span></div>
      </Card>
    </div>

    <Card className="shortcuts-card"><SectionHeader title="Accesos rápidos" description="Los otros módulos permanecen en preparación." /><div className="shortcut-grid">{shortcuts.map(({ label, icon: Icon, path }) => <Link to={path} className="shortcut-link" key={path}><span><Icon size={18} /></span><strong>{label}</strong><ArrowRight size={15} /></Link>)}</div></Card>
    <Card className="overview-card"><SectionHeader title="Áreas del panel" description="Los demás módulos están preparados para etapas futuras." /><div className="overview-links">{appRoutes.slice(4).map(({ path, label, icon: Icon }) => <Link to={path} key={path}><Icon size={16} />{label}<ArrowRight size={14} /></Link>)}</div></Card>
  </>
}
