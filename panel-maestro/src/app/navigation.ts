import { Activity, BookOpenCheck, Coins, GraduationCap, LayoutDashboard, MonitorCog, Settings, Users, WalletCards, type LucideIcon } from 'lucide-react'

export type AppRoute = {
  path: string
  label: string
  icon: LucideIcon
  description: string
  group: 'GENERAL' | 'GESTIÓN' | 'SISTEMA'
}

export const appRoutes: AppRoute[] = [
  { path: '/dashboard', label: 'Dashboard', icon: LayoutDashboard, description: 'Resumen general del entorno de demostración', group: 'GENERAL' },
  { path: '/alumnos', label: 'Alumnos', icon: Users, description: 'Directorio y perfiles de alumnos', group: 'GESTIÓN' },
  { path: '/cuentas', label: 'Cuentas', icon: WalletCards, description: 'Consulta visual de cuentas de Áureos', group: 'GESTIÓN' },
  { path: '/movimientos', label: 'Movimientos', icon: Coins, description: 'Historial de movimientos', group: 'GESTIÓN' },
  { path: '/actividades', label: 'Actividades', icon: Activity, description: 'Actividades del grupo', group: 'GESTIÓN' },
  { path: '/cobros', label: 'Cobros', icon: Coins, description: 'Revisión de cobros', group: 'GESTIÓN' },
  { path: '/progreso', label: 'Progreso académico', icon: BookOpenCheck, description: 'Seguimiento académico', group: 'GESTIÓN' },
  { path: '/asistencia', label: 'Asistencia', icon: GraduationCap, description: 'Registro de asistencia', group: 'GESTIÓN' },
  { path: '/dispositivos', label: 'Dispositivos', icon: MonitorCog, description: 'Terminales registradas', group: 'SISTEMA' },
  { path: '/configuracion', label: 'Configuración', icon: Settings, description: 'Preferencias del panel', group: 'SISTEMA' },
]
