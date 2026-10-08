import { cleanup, fireEvent, render, screen, within } from '@testing-library/react'
import { afterEach, describe, expect, it } from 'vitest'
import { MemoryRouter } from 'react-router-dom'
import { AppRoutes } from '../src/app/App'
import { appRoutes } from '../src/app/navigation'
import { DemoPanelDataService } from '../src/services/DemoPanelDataService'
import { filterStudents, loadStudentDirectory } from '../src/services/studentDirectory'
import { MAX_STUDENT_IMAGE_BYTES, validateStudentImage } from '../src/services/studentImageValidation'
import { StudentProfilePage } from '../src/pages/StudentProfilePage'
import { StudentPhotoProvider } from '../src/students/StudentPhotoContext'
import { useStudentPhotos } from '../src/students/StudentPhotoContext'
import { Route, Routes } from 'react-router-dom'
import type { PanelDataService } from '../src/services/PanelDataService'

afterEach(cleanup)
function renderAt(path: string) { return render(<MemoryRouter initialEntries={[path]}><AppRoutes /></MemoryRouter>) }

describe('Panel Maestro PM.2', () => {
  it('renders the demo dashboard indicators and all ten navigation routes', async () => {
    renderAt('/dashboard')
    expect(screen.getByRole('heading', { name: 'Dashboard' })).toBeInTheDocument()
    expect(screen.getByText('MODO DEMOSTRACIÓN')).toBeInTheDocument()
    const nav = screen.getByRole('navigation', { name: 'Navegación principal' })
    for (const route of appRoutes) expect(within(nav).getByRole('link', { name: route.label })).toBeInTheDocument()
    expect(await screen.findByTestId('metric-students')).toHaveTextContent('6')
    expect(screen.getByTestId('metric-aureos')).toHaveTextContent('260')
    expect(screen.getByTestId('metric-active-activities')).toHaveTextContent('2')
    expect(screen.getByTestId('metric-movements')).toHaveTextContent('10')
    expect(screen.getByText(/Datos ficticios de solo lectura/)).toBeInTheDocument()
    const areas = screen.getByRole('heading', { name: 'Áreas del panel' }).closest('.overview-card') as HTMLElement
    expect(within(areas).getAllByRole('link', { name: 'Asistencia' })).toHaveLength(1)
    for (const route of appRoutes.slice(4)) expect(within(areas).getAllByRole('link', { name: route.label })).toHaveLength(1)
    expect(within(areas).getAllByRole('link')).toHaveLength(6)
  })

  it('navigates between modules from the persistent sidebar', () => {
    renderAt('/dashboard')
    const nav = screen.getByRole('navigation', { name: 'Navegación principal' })
    fireEvent.click(within(nav).getByRole('link', { name: 'Alumnos' }))
    expect(screen.getByRole('heading', { name: 'Alumnos' })).toBeInTheDocument()
    expect(screen.getByText('Directorio de perfiles ficticios del entorno de demostración.')).toBeInTheDocument()
    expect(within(nav).getByRole('link', { name: 'Alumnos' })).toHaveAttribute('aria-current', 'page')
  })
  it('marks the selected route active and renders the student directory', async () => {
    renderAt('/alumnos')
    const nav = screen.getByRole('navigation', { name: 'Navegación principal' })
    expect(within(nav).getByRole('link', { name: 'Alumnos' })).toHaveAttribute('aria-current', 'page')
    expect(screen.getByRole('heading', { name: 'Alumnos' })).toBeInTheDocument()
    expect(await screen.findByText('6 de 6 alumnos')).toBeInTheDocument()
    expect(screen.getAllByText('Sin cuenta').length).toBeGreaterThan(1)
    expect(screen.queryByText(/Saldo/)).not.toBeInTheDocument()
  })

  it('changes the visual theme', () => {
    renderAt('/dashboard')
    fireEvent.click(screen.getByRole('button', { name: 'Cambiar a tema oscuro' }))
    expect(document.documentElement).toHaveAttribute('data-theme', 'dark')
    expect(screen.getByRole('button', { name: 'Cambiar a tema claro' })).toBeInTheDocument()
  })

  it('renders a not-found view with a Dashboard link', () => {
    renderAt('/ruta-inexistente')
    expect(screen.getByRole('heading', { name: 'Página no encontrada' })).toBeInTheDocument()
    expect(screen.getByRole('link', { name: 'Volver al Dashboard' })).toHaveAttribute('href', '/dashboard')
  })

  it('searches without case or accent sensitivity and combines grade, group, and account filters', async () => {
    const entries = await loadStudentDirectory(new DemoPanelDataService())
    expect(filterStudents(entries, { query: 'LIA', grade: '', group: '', account: 'all' }).map(({ student }) => student.student_id)).toEqual([203])
    expect(filterStudents(entries, { query: '', grade: '4', group: 'B', account: 'missing' }).map(({ student }) => student.student_id)).toEqual([206])
  })

  it('opens a student profile by ID and distinguishes account absence from a zero balance', async () => {
    renderAt('/alumnos/206')
    expect(await screen.findByRole('heading', { name: 'Teo Nube' })).toBeInTheDocument()
    expect(within(screen.getByRole('navigation', { name: 'Navegación principal' })).getByRole('link', { name: 'Alumnos' })).toHaveAttribute('aria-current', 'page')
    expect(screen.getAllByText('Sin cuenta').length).toBeGreaterThanOrEqual(1)
    expect(screen.queryByText(/0 Áureos|saldo cero/i)).not.toBeInTheDocument()
    expect(screen.getByRole('link', { name: /Regresar al directorio/ })).toHaveAttribute('href', '/alumnos')
  })

  it('shows the unknown student state for an absent profile ID', async () => {
    renderAt('/alumnos/9999')
    expect(await screen.findByRole('heading', { name: 'Alumno inexistente' })).toBeInTheDocument()
  })

  it('keeps the old preview when an invalid image is selected and removes it on request', async () => {
    const decoder = async () => ({ width: 16, height: 16, close: () => undefined })
    function ProfileAt({ path }: { path: string }) { return <MemoryRouter initialEntries={[path]}><StudentPhotoProvider><Routes><Route path="/alumnos/:studentId" element={<StudentProfilePage decoder={decoder} makePreviewUrl={() => 'blob:demo-valid'} />} /></Routes></StudentPhotoProvider></MemoryRouter> }
    const { rerender } = render(<ProfileAt path="/alumnos/201" />)
    await screen.findByRole('heading', { name: 'Ximena Sol' })
    const valid = new File([new Uint8Array([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a])], 'demo.png', { type: 'image/png' })
    fireEvent.change(screen.getByLabelText('Cargar imagen'), { target: { files: [valid] } })
    expect(await screen.findByText(/Vista previa local: demo.png/)).toBeInTheDocument()
    const invalid = new File(['bad'], 'bad.png', { type: 'image/png' })
    fireEvent.change(screen.getByLabelText('Cargar imagen'), { target: { files: [invalid] } })
    expect(await screen.findByRole('alert')).toHaveTextContent('El contenido no coincide')
    expect(screen.getByText(/Vista previa local: demo.png/)).toBeInTheDocument()
    fireEvent.click(screen.getByRole('button', { name: 'Quitar imagen' }))
    expect(screen.queryByText(/Vista previa local/)).not.toBeInTheDocument()
    rerender(<ProfileAt path="/alumnos/201" />)
  })

  it('validates PNG/JPEG signatures, size, and decoded dimensions', async () => {
    const decoder = async () => ({ width: 20, height: 10, close: () => undefined })
    await expect(validateStudentImage(new File([new Uint8Array([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a])], 'ok.png', { type: 'image/png' }), decoder)).resolves.toBeUndefined()
    await expect(validateStudentImage(new File([new Uint8Array([0xff, 0xd8, 0xff, 0x00])], 'ok.jpg', { type: 'image/jpeg' }), decoder)).resolves.toBeUndefined()
    await expect(validateStudentImage(new File(['text'], 'wrong.jpg', { type: 'image/jpeg' }), decoder)).rejects.toThrow(/contenido no coincide/)
    const large = new File([new Uint8Array(MAX_STUDENT_IMAGE_BYTES + 1)], 'large.png', { type: 'image/png' })
    await expect(validateStudentImage(large, decoder)).rejects.toThrow(/5 MiB/)
    await expect(validateStudentImage(new File([new Uint8Array([0xff, 0xd8, 0xff])], 'broken.jpg', { type: 'image/jpeg' }), async () => { throw new Error('decode') })).rejects.toThrow(/decodificar/)
  })

  it('separates an empty successful collection from a query error', async () => {
    const empty = Object.assign(new DemoPanelDataService(), { getStudents: async () => [] }) as PanelDataService
    const failed = Object.assign(new DemoPanelDataService(), { getStudents: async () => { throw new Error('offline') } }) as PanelDataService
    const emptyEntries = await loadStudentDirectory(empty)
    expect(emptyEntries).toEqual([])
    await expect(loadStudentDirectory(failed)).rejects.toThrow('offline')
  })

  it('revokes temporary preview URLs when replacing and removing them', () => {
    const revoked: string[] = []
    function Controls() {
      const { setPhoto, removePhoto } = useStudentPhotos()
      return <><button onClick={() => setPhoto(201, { url: 'blob:first', fileName: 'first.png' })}>Primera</button><button onClick={() => setPhoto(201, { url: 'blob:second', fileName: 'second.png' })}>Reemplazar</button><button onClick={() => removePhoto(201)}>Retirar</button></>
    }
    render(<StudentPhotoProvider revoke={(url) => revoked.push(url)}><Controls /></StudentPhotoProvider>)
    fireEvent.click(screen.getByRole('button', { name: 'Primera' }))
    fireEvent.click(screen.getByRole('button', { name: 'Reemplazar' }))
    expect(revoked).toEqual(['blob:first'])
    fireEvent.click(screen.getByRole('button', { name: 'Retirar' }))
    expect(revoked).toEqual(['blob:first', 'blob:second'])
  })

  it('serves fictitious read-only data with unique activity payouts', async () => {
    const demo = new DemoPanelDataService()
    const [students, accounts, movements, activities, claims] = await Promise.all([demo.getStudents(), demo.getAccounts(), demo.getMovements(), demo.getActivities(), demo.getClaims()])
    expect(students).toHaveLength(6)
    expect(accounts).toHaveLength(5)
    expect(students.find((student) => student.student_id === 206)?.avatar_asset).toBeNull()
    expect(accounts.some((account) => account.student_id === 206)).toBe(false)
    expect(activities.map((activity) => activity.status)).toEqual(['ACTIVE', 'FINISHED', 'CANCELLED', 'ACTIVE'])
    expect(claims).toHaveLength(3)
    expect(new Set(claims.map((claim) => `${claim.activity_id}:${claim.student_id}`)).size).toBe(claims.length)
    for (const claim of claims) {
      const movement = movements.find((item) => item.id === claim.movement_id)
      expect(movement).toMatchObject({ student_id: claim.student_id, amount: claim.reward_amount, timestamp: claim.claimed_at, type: 'ENTRY', origin: 'DEMO' })
    }
    expect(Object.keys(demo).filter((key) => /write|save|create|update|delete|firmware/i.test(key))).toEqual([])
  })
})
