import { Construction } from 'lucide-react'
import { Link, useLocation } from 'react-router-dom'
import { appRoutes } from '../app/navigation'
import { EmptyState, StatusBadge } from '../components/ui'
import { PageHeader } from '../components/ui'

export function PlaceholderPage() {
  const route = appRoutes.find((item) => item.path === useLocation().pathname)
  if (!route) return <><PageHeader eyebrow="ERROR 404" title="Página no encontrada" description="La dirección solicitada no corresponde a una página disponible." /><div className="not-found"><EmptyState title="No encontramos esta página" description="Revisa la dirección o vuelve al inicio del panel." icon={Construction} action={<Link className="button button-primary" to="/dashboard">Volver al Dashboard</Link>} /></div></>
  const Icon = route.icon
  return <>
    <PageHeader eyebrow="MÓDULO PREPARADO" title={route.label} description={route.description} />
    <section className="module-overview"><div className="module-overview-icon"><Icon size={24} /></div><div><span className="eyebrow">ESTADO DEL MÓDULO</span><h2>Área en preparación</h2><p>La estructura visual está lista para una fase posterior. Este espacio todavía no administra ni presenta datos escolares reales.</p></div><StatusBadge /></section>
    <section className="module-empty"><EmptyState title="Todavía no hay información disponible" description="El contenido de este módulo se incorporará después de definir sus datos y funciones." /></section>
  </>
}
