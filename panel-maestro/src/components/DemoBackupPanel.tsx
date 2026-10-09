import { useRef, useState } from 'react'
import { Button, Card } from './ui'
import { createDemoBackupText, getDemoStorageRevision, restoreDemoBackup } from '../services/demoPanelService'
import { BACKUP_STORE_NAMES, PANEL_BACKUP_MAX_BYTES, PANEL_BACKUP_VERSION, validateBackupText, type ValidatedDemoBackup } from '../services/demoBackup'
import { DEMO_DATABASE_NAME, DEMO_DATABASE_VERSION } from '../services/demoPersistence'

type Candidate = { backup: ValidatedDemoBackup; sourceName: string; protectedRevision: number }
const visibleCollections = BACKUP_STORE_NAMES.filter((store) => store !== 'meta')

function downloadText(text: string, name: string) {
  const url = URL.createObjectURL(new Blob([text], { type: 'application/json;charset=utf-8' }))
  const anchor = document.createElement('a')
  anchor.href = url; anchor.download = name; anchor.click()
  window.setTimeout(() => URL.revokeObjectURL(url), 1000)
}

function backupFileName() { return `banco-escolar-demo-respaldo-${new Date().toISOString().replace(/[:.]/g, '-')}.json` }

export function DemoBackupPanel() {
  const fileInput = useRef<HTMLInputElement>(null)
  const restoring = useRef(false)
  const [busy, setBusy] = useState(false)
  const [error, setError] = useState('')
  const [notice, setNotice] = useState('')
  const [candidate, setCandidate] = useState<Candidate | null>(null)
  const [confirmReplace, setConfirmReplace] = useState(false)
  const [confirmKeepBackup, setConfirmKeepBackup] = useState(false)

  const resetCandidate = () => { setCandidate(null); setConfirmReplace(false); setConfirmKeepBackup(false) }

  const downloadCurrent = async () => {
    setBusy(true); setError(''); setNotice('')
    try {
      const text = await createDemoBackupText()
      downloadText(text, backupFileName())
      setNotice('El navegador inició la descarga del respaldo del espacio demo. Comprueba que el archivo se haya descargado y consérvalo fuera de este navegador.')
    } catch (reason) { setError(reason instanceof Error ? reason.message : 'No se pudo generar el respaldo; no se descargó ningún archivo.') }
    finally { setBusy(false) }
  }

  const selectRestoreFile = async (file?: File) => {
    resetCandidate(); setError(''); setNotice('')
    if (!file) return
    setBusy(true)
    try {
      // Always initiate an up-to-date safety download before validating/restoring another file.
      const safetyText = await createDemoBackupText()
      const safetyBackup = JSON.parse(safetyText) as { revision?: unknown }
      if (!Number.isSafeInteger(safetyBackup.revision)) throw new Error('No se pudo fijar la revisión del respaldo de seguridad.')
      downloadText(safetyText, backupFileName())
      setNotice('Se inició la descarga del respaldo actualizado del estado actual. El navegador no puede garantizar que hayas conservado el archivo; guárdalo fuera de este navegador antes de continuar.')
      if (file.size > PANEL_BACKUP_MAX_BYTES) throw new Error('El archivo supera el límite máximo de 100 MiB.')
      const source = await file.text()
      const backup = await validateBackupText(source, DEMO_DATABASE_NAME, DEMO_DATABASE_VERSION)
      setCandidate({ backup, sourceName: file.name, protectedRevision: safetyBackup.revision as number })
    } catch (reason) {
      setError(reason instanceof Error ? reason.message : 'El archivo no es válido. No se modificó el espacio actual.')
    } finally {
      setBusy(false)
      if (fileInput.current) fileInput.current.value = ''
    }
  }

  const confirmRestore = async () => {
    if (!candidate || !confirmReplace || !confirmKeepBackup || restoring.current) return
    restoring.current = true; setBusy(true); setError(''); setNotice('')
    try {
      const actualRevision = await getDemoStorageRevision()
      if (actualRevision !== candidate.protectedRevision) {
        resetCandidate()
        throw new Error('Los datos cambiaron después de descargar el respaldo de seguridad. Descarga uno actualizado y selecciona de nuevo el archivo a restaurar.')
      }
      await restoreDemoBackup(candidate.backup, candidate.protectedRevision)
      setCandidate(null)
      setNotice('Respaldo restaurado. Las vistas y las fotos se actualizaron desde IndexedDB.')
    } catch (reason) { setError(reason instanceof Error ? reason.message : 'No se pudo restaurar el respaldo. Los datos actuales se conservaron.') }
    finally { restoring.current = false; setBusy(false) }
  }

  const counts = candidate ? visibleCollections.map((store) => ({ store, count: candidate.backup.collections[store].length })) : []

  return <Card className="demo-backup-panel" aria-labelledby="demo-backup-title">
    <div className="demo-backup-heading"><div><p className="eyebrow">COPIA LOCAL · ESPACIO DEMO</p><h2 id="demo-backup-title">Respaldos y restauración</h2><p>El archivo incluye las colecciones persistidas, metadatos y fotos. Formato {PANEL_BACKUP_VERSION}; máximo 100 MiB.</p></div><span className="demo-backup-format">JSON · v{PANEL_BACKUP_VERSION}</span></div>
    <p className="demo-backup-warning">Antes de restaurar se descargará un respaldo actualizado. El navegador inicia la descarga, pero no puede garantizar que hayas conservado el archivo. La restauración reemplaza todo el espacio demo; no mezcla datos, crea movimientos ni recalcula saldos o resultados. El espacio real no está habilitado.</p>
    <div className="demo-backup-actions">
      <Button variant="primary" disabled={busy} onClick={() => void downloadCurrent()}>{busy ? 'Preparando…' : 'Descargar respaldo'}</Button>
      <Button disabled={busy} onClick={() => fileInput.current?.click()}>Restaurar respaldo</Button>
      <input ref={fileInput} className="demo-backup-file-input" type="file" accept="application/json,.json" aria-label="Seleccionar archivo de respaldo" onChange={(event) => void selectRestoreFile(event.target.files?.[0])} />
    </div>
    {notice && <p className="school-success" role="status">{notice}</p>}
    {error && <p className="school-error" role="alert">{error}</p>}
    {busy && <p role="status">Procesando el archivo; todavía no se ha modificado el almacenamiento.</p>}
    {candidate && <section className="demo-backup-preview" aria-label="Vista previa del respaldo" aria-live="polite">
      <h3>Vista previa antes de reemplazar</h3>
      <dl><div><dt>Archivo</dt><dd>{candidate.sourceName}</dd></div><div><dt>Fecha del respaldo</dt><dd><time dateTime={candidate.backup.createdAt}>{new Date(candidate.backup.createdAt).toLocaleString('es-MX')}</time></dd></div><div><dt>Espacio</dt><dd>{candidate.backup.workspace === DEMO_DATABASE_NAME ? 'Demostración' : candidate.backup.workspace}</dd></div><div><dt>Tamaño</dt><dd>{(candidate.backup.bytes / 1024 / 1024).toFixed(2)} MiB</dd></div></dl>
      <ul className="demo-backup-counts">{counts.map(({ store, count }) => <li key={store}><span>{store === 'studentImages' ? 'Fotos' : store}</span><strong>{count}</strong></li>)}</ul>
      <p>La estructura, referencias, valores, reglas y bytes de las fotos se validaron. No se aplican reglas monetarias ni se recalculan saldos, movimientos o resultados históricos.</p>
      <label className="demo-backup-confirm"><input type="checkbox" checked={confirmKeepBackup} onChange={(event) => setConfirmKeepBackup(event.target.checked)} />Comprobé que la descarga de seguridad se inició y conservaré el archivo fuera de este navegador.</label>
      <label className="demo-backup-confirm"><input type="checkbox" checked={confirmReplace} onChange={(event) => setConfirmReplace(event.target.checked)} />Confirmo reemplazar todos los datos y fotos actuales del espacio demo por este respaldo.</label>
      <div className="school-actions"><Button variant="primary" disabled={busy || !confirmKeepBackup || !confirmReplace} onClick={() => void confirmRestore()}>{busy ? 'Restaurando…' : 'Confirmar restauración'}</Button><Button disabled={busy} onClick={() => { resetCandidate(); setError(''); setNotice('Restauración cancelada; no se cambió ningún dato.') }}>Cancelar</Button></div>
    </section>}
  </Card>
}
