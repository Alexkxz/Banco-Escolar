import { DemoPanelDataService } from './DemoPanelDataService'
import { IndexedDbDemoPersistence, PanelStorageError } from './demoPersistence'
import type { ValidatedDemoBackup } from './demoBackup'

const persistence = new IndexedDbDemoPersistence()
let initialization: Promise<{ seeded: boolean }> | null = null

/** Servicio demo: cada mutación se confirma en IndexedDB antes de publicarse en memoria/UI. */
export const demoPanelService = new DemoPanelDataService(undefined, async (_operation, snapshot) => {
  if (!snapshot) throw new PanelStorageError('No se preparó el estado para guardar.', 'FAILED')
  try { await persistence.commit(snapshot) }
  catch (error) {
    if (error instanceof PanelStorageError && error.code === 'CONFLICT') {
      demoPanelService.restore(await persistence.refresh(demoPanelService.snapshot()))
    }
    throw error
  }
})

export type DemoServiceStatus = { status: 'ready'; seeded: boolean } | { status: 'error'; error: PanelStorageError }

export function initializeDemoPanelService(): Promise<{ seeded: boolean }> {
  if (!initialization) initialization = (async () => {
    await persistence.open()
    const result = await persistence.initialize(demoPanelService.snapshot())
    demoPanelService.restore(result.snapshot)
    return { seeded: result.seeded }
  })().catch((error) => { initialization = null; throw error })
  return initialization
}

export async function loadPersistedStudentImages() { return persistence.listImages() }
export async function persistStudentImage(studentId: number, blob: Blob, mimeType: string, fileName: string, dimensions: { width: number; height: number }) {
  if (!Number.isInteger(studentId) || studentId <= 0) throw new PanelStorageError('El ID del alumno no es válido.', 'FAILED')
  if (!(blob instanceof Blob) || !['image/png', 'image/jpeg'].includes(mimeType) || blob.type !== mimeType || blob.size <= 0 || blob.size > 5 * 1024 * 1024 || !Number.isInteger(dimensions.width) || !Number.isInteger(dimensions.height) || dimensions.width <= 0 || dimensions.height <= 0 || dimensions.width * dimensions.height > 25_000_000) throw new PanelStorageError('La imagen no cumple los límites de almacenamiento local.', 'FAILED')
  await persistence.putImage({ student_id: studentId, blob, mimeType: mimeType as 'image/png' | 'image/jpeg', fileName, byteLength: blob.size, width: dimensions.width, height: dimensions.height, updatedAt: Date.now() })
}
export async function removePersistedStudentImage(studentId: number) { await persistence.removeImage(studentId) }
export async function createDemoBackupText() { return persistence.createBackupText() }
export async function getDemoStorageRevision() { return persistence.getRevision() }
export async function restoreDemoBackup(backup: ValidatedDemoBackup, expectedCurrentRevision: number) {
  await persistence.restoreBackup(backup, expectedCurrentRevision)
  const snapshot = await persistence.refresh(demoPanelService.snapshot())
  demoPanelService.restore(snapshot)
  window.dispatchEvent(new CustomEvent('panel-demo-backup-restored'))
}
export function closeDemoPersistence() { persistence.close() }
