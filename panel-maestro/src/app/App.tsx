import { Suspense, lazy, useEffect, useState } from 'react'
import { BrowserRouter, HashRouter, Navigate, Route, Routes } from 'react-router-dom'
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
import { appRoutes } from './navigation'
import { initializeDemoPanelService } from '../services/demoPanelService'
import { PanelStorageError } from '../services/demoPersistence'
import { StudentPhotoProvider } from '../students/StudentPhotoContext'

const SchoolConfigurationPage = lazy(() => import('../pages/SchoolConfigurationPage').then(({ SchoolConfigurationPage: page }) => ({ default: page })))
const PanelRouter = import.meta.env.MODE === 'sd' ? HashRouter : BrowserRouter

export function App() {
  const [storage, setStorage] = useState<{ status: 'loading' } | { status: 'ready' } | { status: 'error'; error: PanelStorageError }>({ status: 'loading' })
  const [attempt, setAttempt] = useState(0)
  const [backupRestoreKey, setBackupRestoreKey] = useState(0)
  const [backupRestoreNotice, setBackupRestoreNotice] = useState('')
  useEffect(() => {
    let active = true
    setStorage({ status: 'loading' })
    initializeDemoPanelService().then(() => { if (active) setStorage({ status: 'ready' }) }).catch((error: unknown) => {
      if (active) setStorage({ status: 'error', error: error instanceof PanelStorageError ? error : new PanelStorageError('No se pudo abrir el almacenamiento local. No se cargaron datos en memoria.', 'UNAVAILABLE') })
    })
    return () => { active = false }
  }, [attempt])
  useEffect(() => {
    const restored = () => { setBackupRestoreKey((value) => value + 1); setBackupRestoreNotice('Respaldo restaurado. Las vistas y las fotos se actualizaron desde IndexedDB.') }
    window.addEventListener('panel-demo-backup-restored', restored)
    return () => window.removeEventListener('panel-demo-backup-restored', restored)
  }, [])
  if (storage.status === 'loading') return <StorageStatus title="Abriendo almacenamiento local" detail="El Panel espera a IndexedDB antes de cargar o aceptar cambios." />
  if (storage.status === 'error') return <StorageFailure error={storage.error} retry={() => setAttempt((value) => value + 1)} />
  return <>{backupRestoreNotice && <div className="backup-restore-global-notice" role="status">{backupRestoreNotice}</div>}<PanelRouter key={backupRestoreKey}><StudentPhotoProvider><AppRoutes /></StudentPhotoProvider></PanelRouter></>
}

function StorageStatus({ title, detail }: { title: string; detail: string }) { return <main className="storage-gate" role="status"><h1>{title}</h1><p>{detail}</p></main> }
function StorageFailure({ error, retry }: { error: PanelStorageError; retry: () => void }) { return <main className="storage-gate" role="alert"><h1>{error.code === 'INCOMPATIBLE' ? 'Versión de almacenamiento incompatible' : 'Almacenamiento local no disponible'}</h1><p>{error.message}</p><p>No se cargaron datos de demostración en memoria. Los cambios no se confirmarán mientras IndexedDB no esté disponible.</p><button onClick={retry}>Reintentar conexión</button></main> }

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
    <Route path="/configuracion" element={<Suspense fallback={<StorageStatus title="Cargando configuración" detail="Preparando las opciones locales del Panel." />}><SchoolConfigurationPage /></Suspense>} />
    {appRoutes.filter((route) => !['/dashboard', '/alumnos', '/cuentas', '/movimientos', '/actividades', '/cobros', '/progreso', '/asistencia', '/configuracion'].includes(route.path)).map((route) => <Route key={route.path} path={route.path} element={<PlaceholderPage />} />)}
    <Route path="*" element={<PlaceholderPage />} />
  </Route></Routes>
}
