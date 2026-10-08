import { useState } from 'react'
import { Link } from 'react-router-dom'
import { Button } from './ui'
import { DemoSchoolError, DemoPanelDataService, type SchoolApplicationPreview, type SchoolApplicationRequest } from '../services/DemoPanelDataService'
import type { AcademicIndicator } from '../services/schoolDomain'

export function SchoolApplicationReview({ service, recordType, recordId, studentId, indicator, alreadyApplied = false, onApplied }: { service: DemoPanelDataService; recordType: 'ACADEMIC' | 'ATTENDANCE'; recordId: number; studentId: number; indicator: AcademicIndicator | 'ATTENDANCE'; alreadyApplied?: boolean; onApplied: () => void }) {
  const [preview, setPreview] = useState<SchoolApplicationPreview | null>(null)
  const [error, setError] = useState('')
  const [busy, setBusy] = useState(false)
  const [notice, setNotice] = useState('')
  const [reviewing, setReviewing] = useState(false)
  const [confirmationId, setConfirmationId] = useState('')
  const review = async () => {
    setError(''); setNotice('')
    try { const value = await service.previewSchoolApplication(recordType, recordId, indicator); setPreview(value); setConfirmationId(`${Date.now()}-${Math.random().toString(36).slice(2)}`); setReviewing(true) }
    catch (reason) { setError(reason instanceof Error ? reason.message : 'No se pudo calcular la propuesta.') }
  }
  const confirm = async () => {
    if (!preview || busy) return
    setBusy(true); setError('')
    const request: SchoolApplicationRequest = { recordType, recordId, indicator, expectedRecordVersion: preview.record.version, expectedAcademicRulesVersion: preview.academicRulesVersion, expectedAttendanceRulesVersion: preview.attendanceRulesVersion, expectedBalance: preview.account?.balance ?? 0, expectedAmount: preview.amount, expectedSignature: preview.signature, confirmationId }
    try { await service.applySchoolAureos(request); setPreview(null); setReviewing(false); setNotice('Aplicación registrada.'); onApplied() }
    catch (reason) {
      if (reason instanceof DemoSchoolError && reason.code === 'STALE_REVIEW') {
        setError('El registro, la regla o el saldo cambió. Revisa la propuesta actualizada y confirma otra vez.')
        try { const fresh = await service.previewSchoolApplication(recordType, recordId, indicator); setPreview(fresh); setConfirmationId(`${Date.now()}-${Math.random().toString(36).slice(2)}`); setReviewing(true) } catch { setPreview(null); setReviewing(false) }
      } else setError(reason instanceof Error ? reason.message : 'No se aplicó el ajuste; puedes reintentar.')
    } finally { setBusy(false) }
  }
  const cancel = () => { setPreview(null); setReviewing(false); setError(''); setNotice('') }
  return <div className="school-application-control">
    {!reviewing && !alreadyApplied && <Button variant="secondary" onClick={review}>Aplicar Áureos</Button>}
    {alreadyApplied && <p className="school-help">Esta aplicación ya está registrada; no se permite repetirla.</p>}
    {error && <p role="alert" className="school-error">{error}</p>}
    {notice && <p role="status" className="school-success">{notice}</p>}
    {reviewing && preview && <div className="school-application-review" role="region" aria-label="Revisión de aplicación de Áureos">
      <h4>Revisar aplicación · {preview.student.name}</h4>
      <dl><div><dt>Resultado</dt><dd>{preview.resultLabel}</dd></div><div><dt>Regla / versión</dt><dd>{recordType === 'ACADEMIC' ? `Académica v${preview.academicRulesVersion}` : `Asistencia v${preview.attendanceRulesVersion}`}</dd></div><div><dt>Importe</dt><dd>{preview.amount > 0 ? '+' : ''}{preview.amount} Áureos</dd></div><div><dt>Saldo actual</dt><dd>{preview.account ? `${preview.account.balance} Áureos` : 'No disponible'}</dd></div><div><dt>Saldo resultante</dt><dd>{preview.resultingBalance !== null ? `${preview.resultingBalance} Áureos` : 'Bloqueado'}</dd></div></dl>
      {!preview.account && <p role="alert" className="school-error">El alumno no tiene cuenta; la aplicación está bloqueada.</p>}
      {preview.resultingBalance !== null && preview.resultingBalance < 0 && <p role="alert" className="school-warning">El saldo resultante será negativo.</p>}
      {preview.amount === 0 && <p className="school-help">El resultado es cero: se registrará la aplicación sin crear movimiento monetario.</p>}
      <div className="school-actions"><Button variant="primary" disabled={!preview.account || busy} onClick={confirm}>{busy ? 'Aplicando…' : 'Confirmar aplicación'}</Button><Button disabled={busy} onClick={cancel}>Cancelar</Button></div>
    </div>}
    <Link className="school-manual-link" to={`/alumnos/${studentId}?schoolRecord=${recordId}&schoolIndicator=${indicator}&schoolRecordType=${recordType}`}>Ajuste manual PM.7A con referencia a este registro</Link>
  </div>
}
