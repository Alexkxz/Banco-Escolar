import type { ActivityClaim, ActivitySession, DemoClaimAuthorization, DemoClaimEvent, MovementRecord, Student, StudentAccount } from '../models/domain'
import type { AcademicRecord, AcademicRules, AttendanceRecord, AttendanceRules, SchoolApplication } from './schoolDomain'
import { BACKUP_STORE_NAMES, buildBackupText, type BackupCollections, type BackupStoreName, type ValidatedDemoBackup } from './demoBackup'

export const DEMO_DATABASE_NAME = 'banco-escolar-panel-demo'
export const DEMO_DATABASE_VERSION = 1

export type DemoPanelSnapshot = {
  students: Student[]
  accounts: StudentAccount[]
  movements: MovementRecord[]
  activities: ActivitySession[]
  claims: ActivityClaim[]
  authorizations: DemoClaimAuthorization[]
  claimEvents: DemoClaimEvent[]
  academicRecords: AcademicRecord[]
  attendanceRecords: AttendanceRecord[]
  schoolApplications: SchoolApplication[]
  academicRules: AcademicRules
  attendanceRules: AttendanceRules
  academicRuleVersions: AcademicRules[]
  attendanceRuleVersions: AttendanceRules[]
  usedAdjustmentConfirmations: string[]
  counters: { activity: number; authorization: number; claimEvent: number; schoolRecord: number; schoolApplication: number; movement: number; claim: number }
}

export type StoredStudentImage = { student_id: number; blob: Blob; mimeType: 'image/png' | 'image/jpeg'; fileName: string; byteLength: number; width: number; height: number; updatedAt: number }

const STORE_NAMES = BACKUP_STORE_NAMES
type StoreName = BackupStoreName

export class PanelStorageError extends Error {
  constructor(message: string, readonly code: 'UNAVAILABLE' | 'INCOMPATIBLE' | 'QUOTA' | 'CONFLICT' | 'FAILED') { super(message); this.name = 'PanelStorageError' }
}

function request<T>(req: IDBRequest<T>): Promise<T> {
  return new Promise((resolve, reject) => {
    req.onsuccess = () => resolve(req.result)
    req.onerror = () => reject(req.error ?? new Error('IndexedDB request failed'))
  })
}

function transactionDone(tx: IDBTransaction) {
  return new Promise<void>((resolve, reject) => {
    tx.oncomplete = () => resolve()
    tx.onabort = () => reject(tx.error ?? new Error('IndexedDB transaction aborted'))
    tx.onerror = () => reject(tx.error ?? new Error('IndexedDB transaction failed'))
  })
}

function createSchema(db: IDBDatabase) {
  const meta = db.createObjectStore('meta', { keyPath: 'key' })
  void meta
  const students = db.createObjectStore('students', { keyPath: 'student_id' }); students.createIndex('grade', 'grade'); students.createIndex('group', 'group'); students.createIndex('status', 'status')
  const images = db.createObjectStore('studentImages', { keyPath: 'student_id' }); images.createIndex('updatedAt', 'updatedAt')
  db.createObjectStore('accounts', { keyPath: 'student_id' })
  const movements = db.createObjectStore('movements', { keyPath: 'id' }); movements.createIndex('student_id', 'student_id'); movements.createIndex('timestamp', 'timestamp'); movements.createIndex('type', 'type'); movements.createIndex('origin', 'origin'); movements.createIndex('related_claim_id', 'related_claim_id'); movements.createIndex('related_movement_id', 'related_movement_id'); movements.createIndex('authorized_by_claim_id', 'authorized_by_claim_id'); movements.createIndex('related_school_record_id', 'related_school_record_id'); movements.createIndex('related_school_application_id', 'related_school_application_id'); movements.createIndex('student_timestamp', ['student_id', 'timestamp'])
  const activities = db.createObjectStore('activities', { keyPath: 'id' }); activities.createIndex('status', 'status'); activities.createIndex('started_at', 'started_at'); activities.createIndex('participant_student_ids', 'participant_student_ids', { multiEntry: true })
  const claims = db.createObjectStore('claims', { keyPath: 'id' }); claims.createIndex('activity_id', 'activity_id'); claims.createIndex('student_id', 'student_id'); claims.createIndex('status', 'status'); claims.createIndex('movement_id', 'movement_id'); claims.createIndex('void_movement_id', 'void_movement_id'); claims.createIndex('activity_student_status', ['activity_id', 'student_id', 'status'])
  const authorizations = db.createObjectStore('claimAuthorizations', { keyPath: 'id' }); authorizations.createIndex('source_claim_id', 'source_claim_id', { unique: true }); authorizations.createIndex('activity_id', 'activity_id'); authorizations.createIndex('student_id', 'student_id'); authorizations.createIndex('status', 'status')
  const events = db.createObjectStore('claimEvents', { keyPath: 'id' }); events.createIndex('claim_id', 'claim_id'); events.createIndex('activity_id', 'activity_id'); events.createIndex('student_id', 'student_id'); events.createIndex('occurred_at', 'occurred_at'); events.createIndex('type', 'type')
  const academics = db.createObjectStore('academicRecords', { keyPath: 'id' }); academics.createIndex('student_id', 'student_id'); academics.createIndex('kind', 'kind'); academics.createIndex('school_date', 'school_date'); academics.createIndex('student_date_kind', ['student_id', 'school_date', 'kind'], { unique: true })
  const attendance = db.createObjectStore('attendanceRecords', { keyPath: 'id' }); attendance.createIndex('student_id', 'student_id'); attendance.createIndex('school_date', 'school_date'); attendance.createIndex('status', 'status'); attendance.createIndex('student_date', ['student_id', 'school_date'], { unique: true })
  db.createObjectStore('academicRuleVersions', { keyPath: 'version' }).createIndex('createdAt', 'createdAt')
  db.createObjectStore('attendanceRuleVersions', { keyPath: 'version' }).createIndex('createdAt', 'createdAt')
  const applications = db.createObjectStore('schoolApplications', { keyPath: 'id' }); applications.createIndex('student_id', 'student_id'); applications.createIndex('record_type', 'record_type'); applications.createIndex('record_id', 'record_id'); applications.createIndex('indicator', 'indicator'); applications.createIndex('record_indicator', ['record_type', 'record_id', 'indicator'], { unique: true }); applications.createIndex('movement_id', 'movement_id'); applications.createIndex('created_at', 'created_at')
  const receipts = db.createObjectStore('operationReceipts', { keyPath: 'operationId' }); receipts.createIndex('type', 'type'); receipts.createIndex('createdAt', 'createdAt')
}

function objectStoreFor(snapshot: DemoPanelSnapshot, store: StoreName): unknown[] {
  switch (store) {
    case 'students': return snapshot.students
    case 'accounts': return snapshot.accounts
    case 'movements': return snapshot.movements
    case 'activities': return snapshot.activities
    case 'claims': return snapshot.claims
    case 'claimAuthorizations': return snapshot.authorizations
    case 'claimEvents': return snapshot.claimEvents
    case 'academicRecords': return snapshot.academicRecords
    case 'attendanceRecords': return snapshot.attendanceRecords
    case 'academicRuleVersions': return snapshot.academicRuleVersions.map((value) => ({ ...value, createdAt: value.version }))
    case 'attendanceRuleVersions': return snapshot.attendanceRuleVersions.map((value) => ({ ...value, createdAt: value.version }))
    case 'schoolApplications': return snapshot.schoolApplications
    default: return []
  }
}

const primaryKey: Partial<Record<StoreName, string>> = { students: 'student_id', accounts: 'student_id', movements: 'id', activities: 'id', claims: 'id', claimAuthorizations: 'id', claimEvents: 'id', academicRecords: 'id', attendanceRecords: 'id', academicRuleVersions: 'version', attendanceRuleVersions: 'version', schoolApplications: 'id' }

export class IndexedDbDemoPersistence {
  private db: IDBDatabase | null = null
  private revision = 0
  private channel: BroadcastChannel | null = null

  async open(): Promise<void> {
    if (!('indexedDB' in globalThis)) throw new PanelStorageError('Este navegador no ofrece IndexedDB; el Panel no puede guardar cambios.', 'UNAVAILABLE')
    try {
      this.db = await new Promise<IDBDatabase>((resolve, reject) => {
        const opening = indexedDB.open(DEMO_DATABASE_NAME, DEMO_DATABASE_VERSION)
        let wasBlocked = false
        opening.onupgradeneeded = (event) => { if (event.oldVersion === 0) createSchema(opening.result) }
        opening.onsuccess = () => { if (wasBlocked) opening.result.close(); else resolve(opening.result) }
        opening.onerror = () => reject(opening.error ?? new Error('No se pudo abrir la base local'))
        opening.onblocked = () => { wasBlocked = true; reject(new PanelStorageError('Otra pestaña bloquea la actualización del almacenamiento. Ciérrala y vuelve a cargar el Panel.', 'INCOMPATIBLE')) }
      })
      if (this.db.version !== DEMO_DATABASE_VERSION || STORE_NAMES.some((name) => !this.db!.objectStoreNames.contains(name))) throw new PanelStorageError('La versión local del Panel no es compatible; conserva los datos y solicita una migración.', 'INCOMPATIBLE')
      this.db.onversionchange = () => { this.db?.close(); this.db = null; window.dispatchEvent(new CustomEvent('panel-storage-versionchange')) }
      if ('BroadcastChannel' in globalThis) this.channel = new BroadcastChannel('banco-escolar-panel-demo')
      const estimate = await navigator.storage?.estimate?.()
      if (estimate?.quota && estimate.usage && estimate.quota - estimate.usage < 1024 * 1024) throw new PanelStorageError('Queda menos de 1 MiB disponible en el almacenamiento local. Libera espacio antes de guardar.', 'QUOTA')
    } catch (error) {
      this.close()
      if (error instanceof PanelStorageError) throw error
      const name = (error as DOMException)?.name
      const message = name === 'QuotaExceededError' ? 'El almacenamiento local está lleno. No se guardó ningún cambio.' : 'No se pudo abrir IndexedDB. No se guardará en memoria; revisa la disponibilidad del almacenamiento del navegador.'
      throw new PanelStorageError(message, name === 'QuotaExceededError' ? 'QUOTA' : 'UNAVAILABLE')
    }
  }

  private requireDb() { if (!this.db) throw new PanelStorageError('El almacenamiento local no está disponible. Recarga el Panel después de revisar el navegador.', 'UNAVAILABLE'); return this.db }

  async initialize(seed: DemoPanelSnapshot): Promise<{ snapshot: DemoPanelSnapshot; revision: number; seeded: boolean }> {
    const db = this.requireDb(); const tx = db.transaction([...STORE_NAMES], 'readwrite'); const done = transactionDone(tx)
    try {
      const meta = tx.objectStore('meta')
      const seedMarker = await request(meta.get('seedCompleted')) as { value?: boolean } | undefined
      const revision = await request(meta.get('revision')) as { value?: number } | undefined
      if (seedMarker && seedMarker.value !== true) {
        tx.abort(); throw new PanelStorageError('El seed demo tiene un marcador incompleto. Se bloqueó la inicialización para conservar los datos.', 'INCOMPATIBLE')
      }
      if (!seedMarker) {
        if ((await Promise.all(STORE_NAMES.filter((name) => name !== 'meta').map((name) => request(tx.objectStore(name).count())))).some((count) => count > 0)) {
          tx.abort(); throw new PanelStorageError('El espacio demo contiene datos sin marcador de inicialización. Se bloqueó el seed para preservar la información.', 'INCOMPATIBLE')
        }
        for (const store of STORE_NAMES) if (store !== 'meta' && store !== 'studentImages' && store !== 'operationReceipts') for (const row of objectStoreFor(seed, store)) tx.objectStore(store).put(row)
        meta.put({ key: 'mode', value: 'demo' }); meta.put({ key: 'schemaVersion', value: DEMO_DATABASE_VERSION })
        meta.put({ key: 'counters', value: seed.counters }); meta.put({ key: 'usedAdjustmentConfirmations', value: seed.usedAdjustmentConfirmations })
        meta.put({ key: 'activeAcademicRuleVersion', value: seed.academicRules.version }); meta.put({ key: 'activeAttendanceRuleVersion', value: seed.attendanceRules.version })
        meta.put({ key: 'seedCompleted', value: true }); meta.put({ key: 'seedVersion', value: 1 }); meta.put({ key: 'revision', value: 1 })
        await done; this.revision = 1; return { snapshot: structuredClone(seed), revision: 1, seeded: true }
      }
      const [mode, schemaVersion, seedVersion, counters] = await Promise.all([
        request(meta.get('mode')) as Promise<{ value?: string } | undefined>,
        request(meta.get('schemaVersion')) as Promise<{ value?: number } | undefined>,
        request(meta.get('seedVersion')) as Promise<{ value?: number } | undefined>,
        request(meta.get('counters')) as Promise<{ value?: DemoPanelSnapshot['counters'] } | undefined>,
      ])
      if (mode?.value !== 'demo' || schemaVersion?.value !== DEMO_DATABASE_VERSION || seedVersion?.value !== 1 || !Number.isSafeInteger(revision?.value) || !counters?.value) {
        tx.abort(); throw new PanelStorageError('Los metadatos del espacio demo están incompletos o no son compatibles. Se conservaron los datos y se bloqueó la escritura.', 'INCOMPATIBLE')
      }
      const snapshot = await this.readSnapshotInTransaction(tx, seed)
      this.revision = revision?.value ?? 1
      await done
      return { snapshot, revision: this.revision, seeded: false }
    } catch (error) { try { tx.abort() } catch { /* already completed */ }; await done.catch(() => undefined); throw this.toStorageError(error) }
  }

  private async readSnapshotInTransaction(tx: IDBTransaction, defaults: DemoPanelSnapshot): Promise<DemoPanelSnapshot> {
    const rows = async <T>(name: string) => await request(tx.objectStore(name).getAll()) as T[]
    const meta = tx.objectStore('meta')
    const [students, accounts, movements, activities, claims, authorizations, claimEvents, academicRecords, attendanceRecords, schoolApplications, academicRuleVersions, attendanceRuleVersions, counters, used, activeAcademic, activeAttendance] = await Promise.all([
      rows<Student>('students'), rows<StudentAccount>('accounts'), rows<MovementRecord>('movements'), rows<ActivitySession>('activities'), rows<ActivityClaim>('claims'), rows<DemoClaimAuthorization>('claimAuthorizations'), rows<DemoClaimEvent>('claimEvents'), rows<AcademicRecord>('academicRecords'), rows<AttendanceRecord>('attendanceRecords'), rows<SchoolApplication>('schoolApplications'), rows<AcademicRules>('academicRuleVersions'), rows<AttendanceRules>('attendanceRuleVersions'), request(meta.get('counters')) as Promise<{ value: DemoPanelSnapshot['counters'] } | undefined>, request(meta.get('usedAdjustmentConfirmations')) as Promise<{ value: string[] } | undefined>, request(meta.get('activeAcademicRuleVersion')) as Promise<{ value: number } | undefined>, request(meta.get('activeAttendanceRuleVersion')) as Promise<{ value: number } | undefined>,
    ])
    const academicRule = academicRuleVersions.find((rule) => rule.version === activeAcademic?.value) ?? academicRuleVersions.at(-1) ?? defaults.academicRules
    const attendanceRule = attendanceRuleVersions.find((rule) => rule.version === activeAttendance?.value) ?? attendanceRuleVersions.at(-1) ?? defaults.attendanceRules
    return { students, accounts, movements, activities, claims, authorizations, claimEvents, academicRecords, attendanceRecords, schoolApplications, academicRules: academicRule, attendanceRules: attendanceRule, academicRuleVersions, attendanceRuleVersions, counters: counters?.value ?? defaults.counters, usedAdjustmentConfirmations: used?.value ?? [], }
  }

  async commit(snapshot: DemoPanelSnapshot): Promise<void> {
    const db = this.requireDb(); const tx = db.transaction([...STORE_NAMES], 'readwrite'); const done = transactionDone(tx)
    try {
      const meta = tx.objectStore('meta'); const storedRevision = await request(meta.get('revision')) as { value?: number } | undefined
      if ((storedRevision?.value ?? 0) !== this.revision) { tx.abort(); throw new PanelStorageError('Otra pestaña guardó cambios. Se recargaron los datos actuales; revisa y confirma de nuevo.', 'CONFLICT') }
      for (const store of STORE_NAMES) {
        if (store === 'meta' || store === 'studentImages' || store === 'operationReceipts') continue
        const records = objectStoreFor(snapshot, store); const keyName = primaryKey[store]
        if (!keyName) continue
        const target = tx.objectStore(store)
        target.clear()
        for (const record of records) target.put(record)
      }
      meta.put({ key: 'counters', value: snapshot.counters }); meta.put({ key: 'usedAdjustmentConfirmations', value: snapshot.usedAdjustmentConfirmations })
      meta.put({ key: 'activeAcademicRuleVersion', value: snapshot.academicRules.version }); meta.put({ key: 'activeAttendanceRuleVersion', value: snapshot.attendanceRules.version })
      const nextRevision = this.revision + 1; meta.put({ key: 'revision', value: nextRevision })
      await done; this.revision = nextRevision; this.channel?.postMessage({ revision: nextRevision })
    } catch (error) { try { tx.abort() } catch { /* aborted or committed */ }; await done.catch(() => undefined); throw this.toStorageError(error) }
  }

  async refresh(defaults: DemoPanelSnapshot) {
    const tx = this.requireDb().transaction([...STORE_NAMES], 'readonly'); const done = transactionDone(tx)
    try { const snapshot = await this.readSnapshotInTransaction(tx, defaults); const rev = await request(tx.objectStore('meta').get('revision')) as { value?: number } | undefined; await done; this.revision = rev?.value ?? this.revision; return snapshot }
    catch (error) { try { tx.abort() } catch { /* already complete */ }; await done.catch(() => undefined); throw this.toStorageError(error) }
  }

  async getImage(studentId: number): Promise<StoredStudentImage | null> {
    const tx = this.requireDb().transaction('studentImages', 'readonly'); const done = transactionDone(tx)
    try { const result = await request(tx.objectStore('studentImages').get(studentId)) as StoredStudentImage | undefined; await done; return result ?? null }
    catch (error) { await done.catch(() => undefined); throw this.toStorageError(error) }
  }
  async listImages(): Promise<StoredStudentImage[]> {
    const tx = this.requireDb().transaction('studentImages', 'readonly'); const done = transactionDone(tx)
    try { const result = await request(tx.objectStore('studentImages').getAll()) as StoredStudentImage[]; await done; return result }
    catch (error) { await done.catch(() => undefined); throw this.toStorageError(error) }
  }
  async getRevision(): Promise<number> {
    const tx = this.requireDb().transaction('meta', 'readonly'); const done = transactionDone(tx)
    try { const row = await request(tx.objectStore('meta').get('revision')) as { value?: number } | undefined; await done; const value = row?.value; if (!Number.isSafeInteger(value)) throw new PanelStorageError('No se pudo leer la revisión del almacenamiento.', 'FAILED'); return value as number }
    catch (error) { await done.catch(() => undefined); throw this.toStorageError(error) }
  }
  async createBackupText(): Promise<string> {
    const tx = this.requireDb().transaction([...STORE_NAMES], 'readonly'); const done = transactionDone(tx)
    try {
      const entries = await Promise.all(STORE_NAMES.map(async (name) => [name, await request(tx.objectStore(name).getAll())] as const))
      await done
      const collections = Object.fromEntries(entries) as BackupCollections
      const meta = new Map((collections.meta as { key: string; value: unknown }[]).map((entry) => [entry.key, entry.value]))
      const revision = meta.get('revision')
      if (!Number.isSafeInteger(revision)) throw new PanelStorageError('No se pudo leer la revisión del almacenamiento.', 'FAILED')
      return await buildBackupText({ workspace: DEMO_DATABASE_NAME, schemaVersion: DEMO_DATABASE_VERSION, revision: revision as number, collections })
    } catch (error) { await done.catch(() => undefined); throw this.toStorageError(error) }
  }
  async restoreBackup(backup: ValidatedDemoBackup, expectedCurrentRevision: number): Promise<void> {
    const db = this.requireDb(); const tx = db.transaction([...STORE_NAMES], 'readwrite'); const done = transactionDone(tx)
    try {
      const meta = tx.objectStore('meta')
      const currentRevision = await request(meta.get('revision')) as { value?: number } | undefined
      if (currentRevision?.value !== expectedCurrentRevision || this.revision !== expectedCurrentRevision) {
        tx.abort(); throw new PanelStorageError('Los datos cambiaron después del respaldo de seguridad. Descarga uno actualizado antes de restaurar.', 'CONFLICT')
      }
      const nextRevision = expectedCurrentRevision + 1
      for (const name of STORE_NAMES) {
        const target = tx.objectStore(name)
        target.clear()
        for (const row of backup.collections[name]) target.put(name === 'meta' && (row as { key?: string }).key === 'revision' ? { key: 'revision', value: nextRevision } : row)
      }
      await done
      this.revision = nextRevision
      this.channel?.postMessage({ revision: nextRevision, restored: true })
    } catch (error) { try { tx.abort() } catch { /* aborted or already committed */ }; await done.catch(() => undefined); throw this.toStorageError(error) }
  }
  async putImage(image: StoredStudentImage): Promise<void> {
    const tx = this.requireDb().transaction(['meta', 'students', 'studentImages'], 'readwrite'); const done = transactionDone(tx)
    try {
      const student = await request(tx.objectStore('students').get(image.student_id))
      if (!student) { tx.abort(); throw new PanelStorageError('No se guardó la imagen porque el alumno ya no existe en el padrón demo.', 'CONFLICT') }
      tx.objectStore('studentImages').put(image)
      const meta = tx.objectStore('meta'); const current = await request(meta.get('revision')) as { value?: number } | undefined; const nextRevision = (current?.value ?? this.revision) + 1
      meta.put({ key: 'revision', value: nextRevision })
      await done
      this.revision = nextRevision; this.channel?.postMessage({ revision: nextRevision })
    } catch (error) { try { tx.abort() } catch { /* transaction already settled */ }; await done.catch(() => undefined); throw this.toStorageError(error) }
  }
  async removeImage(studentId: number): Promise<void> {
    const tx = this.requireDb().transaction(['meta', 'studentImages'], 'readwrite'); const done = transactionDone(tx)
    try {
      tx.objectStore('studentImages').delete(studentId)
      const meta = tx.objectStore('meta'); const current = await request(meta.get('revision')) as { value?: number } | undefined; const nextRevision = (current?.value ?? this.revision) + 1
      meta.put({ key: 'revision', value: nextRevision }); await done
      this.revision = nextRevision; this.channel?.postMessage({ revision: nextRevision })
    } catch (error) { try { tx.abort() } catch { /* no-op */ }; await done.catch(() => undefined); throw this.toStorageError(error) }
  }
  close() { this.channel?.close(); this.channel = null; this.db?.close(); this.db = null }
  private toStorageError(error: unknown): PanelStorageError {
    if (error instanceof PanelStorageError) return error
    if ((error as DOMException)?.name === 'QuotaExceededError') return new PanelStorageError('El almacenamiento local está lleno. No se guardó ningún cambio.', 'QUOTA')
    if ((error as DOMException)?.name === 'VersionError') return new PanelStorageError('La versión de IndexedDB no es compatible. Conserva los datos y solicita una migración.', 'INCOMPATIBLE')
    return new PanelStorageError('No se pudo completar la transacción local. No se confirmó ningún cambio; inténtalo de nuevo.', 'FAILED')
  }
}
