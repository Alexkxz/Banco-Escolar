import { useEffect, useState } from 'react'
import { NavLink, Outlet, useLocation } from 'react-router-dom'
import { Bell, ChevronLeft, Menu, Moon, Sun, X } from 'lucide-react'
import { appRoutes } from '../app/navigation'
import { DevelopmentBadge } from '../components/DevelopmentBadge'
import logo from '../../../Imagenes/Logo y nombre.png'
import emblem from '../../../Imagenes/Logo.png'
import { StudentPhotoProvider } from '../students/StudentPhotoContext'

type Theme = 'light' | 'dark'
const groups = ['GENERAL', 'GESTIÓN', 'SISTEMA'] as const

function readTheme(): Theme {
  const stored = localStorage.getItem('panel-theme')
  if (stored === 'light' || stored === 'dark') return stored
  return typeof window.matchMedia === 'function' && window.matchMedia('(prefers-color-scheme: dark)').matches ? 'dark' : 'light'
}

export function AppLayout() {
  return <StudentPhotoProvider><AppLayoutFrame /></StudentPhotoProvider>
}

function AppLayoutFrame() {
  const [theme, setTheme] = useState<Theme>(readTheme)
  const [collapsed, setCollapsed] = useState(() => localStorage.getItem('panel-sidebar-collapsed') === 'true')
  const [menuOpen, setMenuOpen] = useState(false)
  const location = useLocation()
  const current = appRoutes.find((route) => route.path === location.pathname)
    ?? (['/alumnos/', '/cuentas/', '/movimientos/', '/actividades/', '/cobros/'].some((prefix) => location.pathname.startsWith(prefix))
      ? appRoutes.find((route) => route.path === location.pathname.split('/').slice(0, 2).join('/'))
      : undefined)
  useEffect(() => { document.documentElement.dataset.theme = theme; localStorage.setItem('panel-theme', theme) }, [theme])
  useEffect(() => { setMenuOpen(false) }, [location.pathname])
  useEffect(() => {
    if (!menuOpen) return
    const closeOnEscape = (event: KeyboardEvent) => { if (event.key === 'Escape') setMenuOpen(false) }
    window.addEventListener('keydown', closeOnEscape)
    return () => window.removeEventListener('keydown', closeOnEscape)
  }, [menuOpen])

  const toggleCollapsed = () => setCollapsed((value) => { localStorage.setItem('panel-sidebar-collapsed', String(!value)); return !value })
  const closeMenu = () => setMenuOpen(false)

  return <div className={`app-shell ${collapsed ? 'sidebar-collapsed' : ''}`}>
    <aside id="primary-sidebar" className={`sidebar ${menuOpen ? 'sidebar-open' : ''}`} aria-label="Menú lateral">
      <div className="brand"><div className="brand-logo-frame"><img className="brand-wordmark" src={logo} alt="Banco Escolar" /><img className="brand-emblem" src={emblem} alt="" /></div><span className="brand-caption">PANEL MAESTRO</span></div>
      <nav className="side-nav" aria-label="Navegación principal">
        {groups.map((group) => <div className="nav-group" key={group}>
          <div className="side-section-label">{group}</div>
          {appRoutes.filter((route) => route.group === group).map(({ path, label, icon: Icon }) => <NavLink key={path} to={path} title={collapsed ? label : undefined} onClick={closeMenu} className={({ isActive }) => `nav-link ${isActive ? 'active' : ''}`}>
            <Icon size={19} strokeWidth={1.8} aria-hidden="true" /><span className="nav-label">{label}</span>
          </NavLink>)}
        </div>)}
      </nav>
      <div className="sidebar-footer"><span className="footer-mark" aria-hidden="true">BE</span><div className="footer-copy"><strong>Modo demostración</strong><span>Sin conexión a datos</span></div></div>
    </aside>
    {menuOpen && <button className="mobile-scrim" aria-label="Cerrar menú" onClick={closeMenu} />}
    <div className="main-column">
      <header className="topbar">
        <button className="icon-button menu-toggle" aria-label={menuOpen ? 'Cerrar menú' : 'Abrir menú'} aria-expanded={menuOpen} aria-controls="primary-sidebar" onClick={() => setMenuOpen((value) => !value)}>{menuOpen ? <X size={20} /> : <Menu size={20} />}</button>
        <button className="icon-button collapse-toggle" aria-label={collapsed ? 'Expandir menú lateral' : 'Contraer menú lateral'} aria-expanded={!collapsed} onClick={toggleCollapsed}><ChevronLeft size={18} /></button>
        <div className="topbar-context"><span>Panel Maestro</span><span className="crumb-separator" aria-hidden="true">/</span><strong>{current?.label ?? 'Página no encontrada'}</strong><small>{current?.description ?? 'La dirección solicitada no está disponible'}</small></div>
        <div className="top-actions"><span className="demo-indicator"><i /><span className="demo-label-full">MODO DEMOSTRACIÓN</span><span className="demo-label-compact" aria-hidden="true">DEMO</span></span><span className="version-label">DESARROLLO</span><button className="icon-button reserved-action" aria-label="Notificaciones, próximamente" title="Área reservada para futuras notificaciones" disabled><Bell size={18} /></button><button className="theme-toggle" onClick={() => setTheme((value) => value === 'light' ? 'dark' : 'light')} aria-label={`Cambiar a tema ${theme === 'light' ? 'oscuro' : 'claro'}`} title={`Tema ${theme === 'light' ? 'oscuro' : 'claro'}`}>{theme === 'light' ? <Moon size={17} /> : <Sun size={17} />}<span>{theme === 'light' ? 'Oscuro' : 'Claro'}</span></button><span className="session-placeholder">Sesión sin iniciar</span></div>
      </header>
      <main className="page-content"><Outlet /></main>
      <footer className="page-footer"><span>Banco Escolar · Panel Maestro</span><DevelopmentBadge /><span>Interfaz de demostración · sin datos reales</span></footer>
    </div>
  </div>
}
