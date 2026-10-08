import { BrowserRouter, Navigate, Route, Routes } from 'react-router-dom'
import { AppLayout } from '../layouts/AppLayout'
import { DevelopmentHome } from '../pages/DevelopmentHome'
import { PlaceholderPage } from '../pages/PlaceholderPage'
import { StudentsDirectoryPage } from '../pages/StudentsDirectoryPage'
import { StudentProfilePage } from '../pages/StudentProfilePage'
import { AccountsPage } from '../pages/AccountsPage'
import { AccountDetailPage } from '../pages/AccountDetailPage'
import { MovementsPage } from '../pages/MovementsPage'
import { MovementDetailPage } from '../pages/MovementDetailPage'
import { ActivitiesPage } from '../pages/ActivitiesPage'
import { ActivityDetailPage } from '../pages/ActivityDetailPage'
import { ActivityFormPage } from '../pages/ActivityFormPage'
import { ClaimsPage } from '../pages/ClaimsPage'
import { ClaimDetailPage } from '../pages/ClaimDetailPage'
import { AcademicProgressPage } from '../pages/AcademicProgressPage'
import { AttendancePage } from '../pages/AttendancePage'
import { SchoolConfigurationPage } from '../pages/SchoolConfigurationPage'
import { appRoutes } from './navigation'

export function App() {
  return <BrowserRouter><AppRoutes /></BrowserRouter>
}

export function AppRoutes() {
  return <Routes><Route element={<AppLayout />}>
    <Route path="/" element={<Navigate to="/dashboard" replace />} />
    <Route path="/dashboard" element={<DevelopmentHome />} />
    <Route path="/alumnos" element={<StudentsDirectoryPage />} />
    <Route path="/alumnos/:studentId" element={<StudentProfilePage />} />
    <Route path="/cuentas" element={<AccountsPage />} />
    <Route path="/cuentas/:accountId" element={<AccountDetailPage />} />
    <Route path="/movimientos" element={<MovementsPage />} />
    <Route path="/movimientos/:movementId" element={<MovementDetailPage />} />
    <Route path="/actividades" element={<ActivitiesPage />} />
    <Route path="/actividades/nueva" element={<ActivityFormPage mode="create" />} />
    <Route path="/actividades/:activityId/editar" element={<ActivityFormPage mode="edit" />} />
    <Route path="/actividades/:activityId" element={<ActivityDetailPage />} />
    <Route path="/cobros" element={<ClaimsPage />} />
    <Route path="/cobros/:claimId" element={<ClaimDetailPage />} />
    <Route path="/progreso" element={<AcademicProgressPage />} />
    <Route path="/asistencia" element={<AttendancePage />} />
    <Route path="/configuracion" element={<SchoolConfigurationPage />} />
    {appRoutes.filter((route) => !['/dashboard', '/alumnos', '/cuentas', '/movimientos', '/actividades', '/cobros', '/progreso', '/asistencia', '/configuracion'].includes(route.path)).map((route) => <Route key={route.path} path={route.path} element={<PlaceholderPage />} />)}
    <Route path="*" element={<PlaceholderPage />} />
  </Route></Routes>
}
