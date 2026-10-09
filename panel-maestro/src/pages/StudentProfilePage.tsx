import { useEffect, useState, type ChangeEvent } from 'react'
import { Link, useParams, useSearchParams } from 'react-router-dom'
import { ArrowLeft, ImagePlus, Trash2, Wallet } from 'lucide-react'
import { Button, Card, EmptyState, PageHeader } from '../components/ui'
import { StudentAvatar } from '../components/StudentAvatar'
import { demoPanelService } from '../services/demoPanelService'
import { DemoAccountAdjustmentError, MAX_DEMO_ACCOUNT_BALANCE, MAX_DEMO_MOVEMENT_AMOUNT, type DemoAccountAdjustment } from '../services/DemoPanelDataService'
import type { PanelDataService } from '../services/PanelDataService'
import type { StudentAccount } from '../models/domain'
import { loadStudentDirectory, type StudentDirectoryEntry } from '../services/studentDirectory'
import { movementDateFormatter } from '../services/movementQueries'
import { validateStudentImage, type ImageDecoder } from '../services/studentImageValidation'
import { useStudentPhotos } from '../students/StudentPhotoContext'

type LoadState = { status: 'loading' } | { status: 'error' } | { status: 'ready'; entry: StudentDirectoryEntry | null; empty: boolean }
type BalanceState = { status: 'loading' } | { status: 'error' } | { status: 'ready'; account: StudentAccount | null }
type Draft = { operation: 'ADD' | 'WITHDRAW'; amount: string; reason: string }
type Preview = { expectedBalance: number; resultBalance: number; confirmationId: string }
type Props = { dataService?: PanelDataService; decoder?: ImageDecoder }

function newConfirmationId() { return `${Date.now()}-${Math.random().toString(36).slice(2)}` }

export function StudentProfilePage({ dataService = demoPanelService, decoder }: Props) {
  const { studentId } = useParams()
  const [searchParams] = useSearchParams()
  const schoolRecordReference = /^\d+$/.test(searchParams.get('schoolRecord') ?? '') ? Number(searchParams.get('schoolRecord')) : undefined
  const id = /^\d+$/.test(studentId ?? '') ? Number(studentId) : NaN
  const [state, setState] = useState<LoadState>({ status: 'loading' })
  const [balance, setBalance] = useState<BalanceState>({ status: 'loading' })
  const [attempt, setAttempt] = useState(0)
  const [balanceAttempt, setBalanceAttempt] = useState(0)
  const [imageError, setImageError] = useState('')
  const [validating, setValidating] = useState(false)
  const [draft, setDraft] = useState<Draft | null>(null)
  const [amountError, setAmountError] = useState('')
  const [preview, setPreview] = useState<Preview | null>(null)
  const [operationError, setOperationError] = useState('')
  const [notice, setNotice] = useState('')
  const [busy, setBusy] = useState(false)
  const { photos, setPhoto, removePhoto, status: photosStatus } = useStudentPhotos()
  const photo = Number.isFinite(id) ? photos.get(id) : undefined
  useEffect(() => {
    let current = true
    setState({ status: 'loading' })
    loadStudentDirectory(dataService).then((entries) => { if (current) setState({ status: 'ready', entry: entries.find(({ student }) => student.student_id === id) ?? null, empty: entries.length === 0 }) }).catch(() => { if (current) setState({ status: 'error' }) })
    return () => { current = false }
  }, [dataService, attempt, id])
  useEffect(() => {
    let current = true
    setBalance({ status: 'loading' })
    dataService.getAccounts().then((accounts) => { if (current) setBalance({ status: 'ready', account: accounts.find((account) => account.student_id === id) ?? null }) }).catch(() => { if (current) setBalance({ status: 'error' }) })
    return () => { current = false }
  }, [dataService, balanceAttempt, id])

  const chooseImage = async (event: ChangeEvent<HTMLInputElement>) => {
    const file = event.target.files?.[0]
    event.target.value = ''
    if (!file) return
    setImageError(''); setValidating(true)
    try { const dimensions = await validateStudentImage(file, decoder); await setPhoto(id, file, file.name, dimensions) }
    catch (error) { setImageError(error instanceof Error ? error.message : 'No se pudo validar la imagen.') }
    finally { setValidating(false) }
  }

  const openAdjustment = (operation: Draft['operation']) => {
    setNotice(''); setOperationError(''); setAmountError(''); setPreview(null)
    setDraft({ operation, amount: '', reason: '' })
  }
  const reviewAdjustment = () => {
    if (!draft || balance.status !== 'ready' || !balance.account) return
    const value = draft.amount.trim()
    if (!/^\d+$/.test(value)) { setAmountError('Escribe una cantidad entera positiva, sin decimales ni signo.'); return }
    const amount = Number(value)
    if (!Number.isSafeInteger(amount) || amount < 1 || amount > MAX_DEMO_MOVEMENT_AMOUNT) { setAmountError(`La cantidad debe estar entre 1 y ${MAX_DEMO_MOVEMENT_AMOUNT.toLocaleString('es-MX')} Áureos.`); return }
    const resultBalance = balance.account.balance + (draft.operation === 'ADD' ? amount : -amount)
    if (!Number.isSafeInteger(resultBalance) || Math.abs(resultBalance) > MAX_DEMO_ACCOUNT_BALANCE) { setAmountError('El saldo resultante excede el límite admitido por el modelo de cuenta.'); return }
    setAmountError(''); setOperationError(''); setPreview({ expectedBalance: balance.account.balance, resultBalance, confirmationId: newConfirmationId() })
  }
  const confirmAdjustment = async () => {
    if (!draft || !preview || balance.status !== 'ready' || !balance.account || busy) return
    const operation = dataService as PanelDataService & { adjustDemoAccount?: (input: DemoAccountAdjustment) => Promise<{ account: StudentAccount; movement: { timestamp: number } }> }
    if (!operation.adjustDemoAccount) { setOperationError('Los ajustes están disponibles solo en el servicio de demostración.'); return }
    setBusy(true); setOperationError(''); setNotice('')
    try {
      const value = await operation.adjustDemoAccount({ studentId: id, operation: draft.operation, amount: Number(draft.amount), reason: draft.reason, expectedBalance: preview.expectedBalance, confirmationId: preview.confirmationId, relatedSchoolRecordId: schoolRecordReference })
      setBalance({ status: 'ready', account: value.account })
      setNotice(`Ajuste aplicado. Movimiento registrado el ${movementDateFormatter.format(value.movement.timestamp * 1000)}.`)
      setDraft(null); setPreview(null)
    } catch (error) {
      if (error instanceof DemoAccountAdjustmentError && error.code === 'BALANCE_CHANGED' && error.currentAccount) {
        setBalance({ status: 'ready', account: error.currentAccount }); setPreview(null)
        setOperationError('El saldo cambió desde la vista previa. Revisa el resultado actualizado y confirma de nuevo.')
      } else setOperationError(error instanceof Error ? error.message : 'No se pudo aplicar el ajuste. El formulario se conservó.')
    } finally { setBusy(false) }
  }
  const cancelAdjustment = () => { setDraft(null); setPreview(null); setAmountError(''); setOperationError(''); setNotice('') }

  return <>
    <PageHeader eyebrow="GESTIÓN · SOLO LECTURA" title="Perfil del alumno" description="Perfil individual ficticio del entorno de demostración." />
    {state.status === 'loading' && <Card className="directory-state" role="status"><span className="status-dot" />Cargando perfil…</Card>}
    {state.status === 'error' && <Card className="directory-state directory-error" role="alert"><div><strong>No se pudo consultar el perfil.</strong><p>La consulta falló; el alumno no se considera inexistente.</p></div><Button variant="primary" onClick={() => setAttempt((value) => value + 1)}>Reintentar</Button></Card>}
    {state.status === 'ready' && state.empty && <Card><EmptyState title="No hay alumnos en el conjunto de demostración" description="La consulta terminó correctamente, pero la colección está vacía." action={<Link className="button button-secondary" to="/alumnos">Regresar al directorio</Link>} /></Card>}
    {state.status === 'ready' && !state.empty && !state.entry && <Card className="profile-missing"><EmptyState title="Alumno inexistente" description="No se encontró un perfil con ese ID en el conjunto de demostración." action={<Link className="button button-secondary" to="/alumnos"><ArrowLeft size={15} />Regresar al directorio</Link>} /></Card>}
    {state.status === 'ready' && state.entry && <>
      <Link className="back-link" to="/alumnos"><ArrowLeft size={16} />Regresar al directorio</Link>
      <Card className="student-profile-card"><div className="profile-identity"><StudentAvatar student={state.entry.student} size="large" /><div><span className="eyebrow">ID de demostración {state.entry.student.student_id}</span><h2>{state.entry.student.name}</h2><p>{state.entry.student.grade}° grado · Grupo {state.entry.student.group}</p><span className={`student-status ${state.entry.student.status === 'ACTIVE' ? 'is-active' : 'is-inactive'}`}>{state.entry.student.status === 'ACTIVE' ? 'Activo' : 'Inactivo'}</span></div></div><div className="profile-details"><div><span>Situación de cuenta</span><strong>{state.entry.hasAccount ? 'Cuenta asignada' : 'Sin cuenta'}</strong></div><div><span>Fuente de datos</span><strong>Modo demostración</strong></div></div></Card>
      <div className="profile-sidecards-grid">
        <Card className="profile-image-card"><div className="section-header"><div><h2>Imagen del perfil</h2><p>La selección se asocia al ID del alumno, no al nombre ni a una tarjeta.</p></div><ImagePlus size={19} aria-hidden="true" /></div>
          <p className="image-memory-note">Imagen guardada localmente en este navegador y vinculada al ID del alumno. No se sincroniza con otras computadoras ni se envía a la terminal.</p>
          <div className="image-controls"><label className="button button-primary image-picker"><ImagePlus size={16} />Cargar imagen<input type="file" accept="image/png,image/jpeg" onChange={chooseImage} disabled={validating || photosStatus !== 'ready'} /></label>{photo && <Button onClick={() => { void removePhoto(id).then(() => setImageError('')).catch((error: unknown) => setImageError(error instanceof Error ? error.message : 'No se pudo quitar la imagen.')) }} disabled={photosStatus !== 'ready'}><Trash2 size={15} />Quitar imagen</Button>}{validating && <span role="status">Validando imagen…</span>}{photosStatus === 'loading' && <span role="status">Cargando imagen guardada…</span>}{photosStatus === 'error' && <span role="alert">No se pudieron cargar las imágenes locales; no se permiten cambios.</span>}</div>
          {photo && <p className="image-file-name">Vista previa local: {photo.fileName} · no sincronizada</p>}{imageError && <p className="image-error" role="alert">{imageError}</p>}
        </Card>
        <Card className="profile-balance-card"><div className="section-header"><div><h2>Saldo de la cuenta</h2><p>Consulta y ajustes manuales de demostración</p></div><Wallet size={19} aria-hidden="true" /></div>
          {balance.status === 'loading' && <p className="balance-query-state" role="status">Consultando saldo…</p>}
          {balance.status === 'error' && <div className="balance-query-state" role="alert"><p>No se pudo consultar el saldo; no se considera cero.</p><Button variant="secondary" onClick={() => setBalanceAttempt((value) => value + 1)}>Reintentar consulta</Button></div>}
          {balance.status === 'ready' && !balance.account && <div className="profile-no-account"><strong>Sin cuenta</strong><p>No se creó una cuenta. Los ajustes no están disponibles.</p></div>}
          {balance.status === 'ready' && balance.account && <><div className="profile-balance-value"><span>Saldo actual</span><strong>{balance.account.balance.toLocaleString('es-MX')} <small>Áureos</small></strong></div>
            <div className="profile-balance-actions"><Button variant="primary" onClick={() => openAdjustment('ADD')}>Agregar Áureos</Button><Button onClick={() => openAdjustment('WITHDRAW')}>Retirar Áureos</Button><Link className="button button-secondary" to={`/cuentas/${id}`}>Ver cuenta</Link></div>
            <p className="balance-memory-note">Los ajustes demo se guardan localmente en este navegador. No se sincronizan con otras computadoras ni se envían a la terminal.</p>
            {schoolRecordReference && <p className="balance-memory-note">Este ajuste manual quedará vinculado al registro escolar demo {schoolRecordReference}.</p>}
            {notice && <p className="adjustment-success" role="status">{notice}</p>}
            {draft && <form className="balance-adjustment-form" onSubmit={(event) => { event.preventDefault(); reviewAdjustment() }}>
              <h3>{draft.operation === 'ADD' ? 'Agregar Áureos' : 'Retirar Áureos'}</h3>
              <label>Cantidad de Áureos<input autoFocus inputMode="numeric" value={draft.amount} onChange={(event) => { setDraft({ ...draft, amount: event.target.value }); setPreview(null); setAmountError('') }} aria-describedby="adjustment-limits" /></label>
              <span id="adjustment-limits" className="adjustment-help">Entero positivo, máximo {MAX_DEMO_MOVEMENT_AMOUNT.toLocaleString('es-MX')} por movimiento.</span>
              <label>Motivo (opcional)<textarea value={draft.reason} onChange={(event) => { setDraft({ ...draft, reason: event.target.value }); setPreview(null) }} rows={2} /></label>
              {amountError && <p className="adjustment-error" role="alert">{amountError}</p>}
              {operationError && <p className="adjustment-error" role="alert">{operationError}</p>}
              {preview && <div className="adjustment-review" role="region" aria-label="Confirmación del ajuste"><h4>Confirma el ajuste</h4><dl><div><dt>Alumno</dt><dd>{state.entry.student.name}</dd></div><div><dt>Operación</dt><dd>{draft.operation === 'ADD' ? 'Agregar Áureos' : 'Retirar Áureos'}</dd></div><div><dt>Cantidad</dt><dd>{Number(draft.amount).toLocaleString('es-MX')} Áureos</dd></div><div><dt>Motivo</dt><dd>{draft.reason.trim() || 'Sin motivo'}</dd></div><div><dt>Saldo actual</dt><dd>{preview.expectedBalance.toLocaleString('es-MX')} Áureos</dd></div><div><dt>Saldo resultante</dt><dd>{preview.resultBalance.toLocaleString('es-MX')} Áureos</dd></div></dl>{preview.resultBalance < 0 && <p className="adjustment-warning" role="alert">El saldo resultante será negativo.</p>}<div className="adjustment-form-actions"><Button variant="primary" type="button" disabled={busy} onClick={confirmAdjustment}>{busy ? 'Aplicando…' : 'Confirmar ajuste'}</Button><Button type="button" disabled={busy} onClick={cancelAdjustment}>Cancelar</Button></div></div>}
              {!preview && <div className="adjustment-form-actions"><Button variant="primary" type="submit">Revisar ajuste</Button><Button type="button" onClick={cancelAdjustment}>Cancelar</Button></div>}
            </form>}
          </>}
        </Card>
      </div>
    </>}
  </>
}
