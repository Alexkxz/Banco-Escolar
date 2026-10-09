import { aspects, validateAcademicRules, validateAttendanceRules } from './schoolDomain'

export const PANEL_BACKUP_FORMAT = 'banco-escolar-panel-backup'
export const PANEL_BACKUP_VERSION = 1
export const PANEL_BACKUP_MAX_BYTES = 100 * 1024 * 1024
export const PANEL_BACKUP_MAX_RECORDS_PER_STORE = 10_000
export const PANEL_BACKUP_MAX_IMAGES = 1_000
export const PANEL_BACKUP_MAX_IMAGE_BYTES = 5 * 1024 * 1024
export const PANEL_BACKUP_MAX_IMAGE_PIXELS = 25_000_000

export const BACKUP_STORE_NAMES = [
  'meta', 'students', 'studentImages', 'accounts', 'movements', 'activities', 'claims',
  'claimAuthorizations', 'claimEvents', 'academicRecords', 'attendanceRecords',
  'academicRuleVersions', 'attendanceRuleVersions', 'schoolApplications', 'operationReceipts',
] as const
export type BackupStoreName = typeof BACKUP_STORE_NAMES[number]
export type BackupCollections = Record<BackupStoreName, unknown[]>
export type BackupImageRow = {
  student_id: number
  mimeType: 'image/png' | 'image/jpeg'
  fileName: string
  byteLength: number
  width: number
  height: number
  updatedAt: number
  blob: Blob
}
export type ValidatedDemoBackup = {
  format: typeof PANEL_BACKUP_FORMAT
  version: typeof PANEL_BACKUP_VERSION
  workspace: string
  schemaVersion: number
  createdAt: string
  revision: number
  collections: BackupCollections
  checksum: { algorithm: 'SHA-256'; value: string }
  bytes: number
}

type EncodedImageRow = Omit<BackupImageRow, 'blob'> & { blob: { type: string; size: number; base64: string } }

export class BackupValidationError extends Error {
  constructor(message: string) { super(message); this.name = 'BackupValidationError' }
}

function fail(message: string): never { throw new BackupValidationError(message) }
function isRecord(value: unknown): value is Record<string, unknown> { return typeof value === 'object' && value !== null && !Array.isArray(value) }
function isSafeId(value: unknown, allowZero = false): value is number { return Number.isSafeInteger(value) && (value as number) >= (allowZero ? 0 : 1) }
function isText(value: unknown, max = 10_000): value is string { return typeof value === 'string' && value.length <= max }
function assertJsonValue(value: unknown, where: string, depth = 0): void {
  if (depth > 32) fail(`${where}: la estructura tiene demasiados niveles.`)
  if (value === null || typeof value === 'string' || typeof value === 'boolean') return
  if (typeof value === 'number') { if (!Number.isFinite(value)) fail(`${where}: contiene un número no finito.`); return }
  if (Array.isArray(value)) { for (const entry of value) assertJsonValue(entry, where, depth + 1); return }
  if (isRecord(value)) { for (const [key, entry] of Object.entries(value)) { if (key === '__proto__' || key === 'constructor') fail(`${where}: contiene una propiedad no permitida.`); assertJsonValue(entry, where, depth + 1) }; return }
  fail(`${where}: contiene un valor que JSON no puede conservar.`)
}
function rows(collections: BackupCollections, name: BackupStoreName): Record<string, unknown>[] {
  const value = collections[name]
  if (!Array.isArray(value) || value.length > PANEL_BACKUP_MAX_RECORDS_PER_STORE || value.some((row) => !isRecord(row))) fail(`La colección “${name}” no tiene una estructura válida o supera ${PANEL_BACKUP_MAX_RECORDS_PER_STORE} registros.`)
  return value as Record<string, unknown>[]
}
function idMap(source: Record<string, unknown>[], key: string, label: string): Map<number, Record<string, unknown>> {
  const result = new Map<number, Record<string, unknown>>()
  for (const row of source) {
    const id = row[key]
    if (!isSafeId(id) || result.has(id)) fail(`${label}: ID “${String(id)}” inválido o duplicado.`)
    result.set(id, row)
  }
  return result
}
function requireStudent(students: Map<number, Record<string, unknown>>, id: unknown, label: string): void {
  if (!isSafeId(id) || !students.has(id)) fail(`${label}: referencia a un alumno inexistente.`)
}
function requireRef(index: Map<number, Record<string, unknown>>, id: unknown, label: string, optional = false): void {
  if (optional && (id === null || id === undefined || id === 0)) return
  if (!isSafeId(id) || !index.has(id)) fail(`${label}: referencia a un registro inexistente.`)
}
function assertUnique(rowsToCheck: Record<string, unknown>[], key: (row: Record<string, unknown>) => string, label: string): void {
  const values = new Set<string>()
  for (const row of rowsToCheck) { const value = key(row); if (values.has(value)) fail(`${label}: hay una clave duplicada.`); values.add(value) }
}
function assertAmount(value: unknown, label: string, minimum = -1_000_000_000_000, maximum = 1_000_000_000_000): void {
  if (!Number.isSafeInteger(value) || (value as number) < minimum || (value as number) > maximum) fail(`${label}: el importe debe ser un entero dentro de los límites permitidos.`)
}
function canonical(value: unknown): string { return JSON.stringify(value) }
async function sha256(value: string): Promise<string> {
  if (!globalThis.crypto?.subtle) fail('Este navegador no ofrece SHA-256; no se puede verificar el respaldo.')
  const digest = await crypto.subtle.digest('SHA-256', new TextEncoder().encode(value))
  return Array.from(new Uint8Array(digest), (byte) => byte.toString(16).padStart(2, '0')).join('')
}
function payloadOf(value: Omit<ValidatedDemoBackup, 'checksum' | 'bytes'> | Record<string, unknown>) {
  return { format: value.format, version: value.version, workspace: value.workspace, schemaVersion: value.schemaVersion, createdAt: value.createdAt, revision: value.revision, collections: value.collections }
}
function bytesToBase64(bytes: Uint8Array): string {
  let binary = ''
  for (let offset = 0; offset < bytes.length; offset += 0x8000) binary += String.fromCharCode(...bytes.subarray(offset, Math.min(offset + 0x8000, bytes.length)))
  return btoa(binary)
}
function base64ToBytes(value: string): Uint8Array {
  if (!/^(?:[A-Za-z0-9+/]{4})*(?:[A-Za-z0-9+/]{2}==|[A-Za-z0-9+/]{3}=)?$/.test(value)) fail('Una foto contiene datos Base64 inválidos.')
  const binary = atob(value)
  return Uint8Array.from(binary, (char) => char.charCodeAt(0))
}

export async function buildBackupText(input: { workspace: string; schemaVersion: number; revision: number; collections: BackupCollections; createdAt?: string }): Promise<string> {
  const createdAt = input.createdAt ?? new Date().toISOString()
  const collections: BackupCollections = { ...input.collections }
  const images = rows(collections, 'studentImages')
  if (images.length > PANEL_BACKUP_MAX_IMAGES) fail(`El respaldo supera el límite de ${PANEL_BACKUP_MAX_IMAGES} fotos.`)
  const encodedImages: EncodedImageRow[] = []
  for (const raw of images) {
    const row = raw as unknown as BackupImageRow
    if (!(row.blob instanceof Blob) || row.blob.size <= 0 || row.blob.size > PANEL_BACKUP_MAX_IMAGE_BYTES) fail('Una foto local supera el límite de 5 MiB o no se almacenó como Blob.')
    const data = new Uint8Array(await row.blob.arrayBuffer())
    encodedImages.push({ ...row, blob: { type: row.blob.type, size: row.blob.size, base64: bytesToBase64(data) } })
  }
  collections.studentImages = encodedImages as unknown as unknown[]
  const payload = { format: PANEL_BACKUP_FORMAT, version: PANEL_BACKUP_VERSION, workspace: input.workspace, schemaVersion: input.schemaVersion, createdAt, revision: input.revision, collections }
  const checksum = await sha256(canonical(payload))
  const text = canonical({ ...payload, checksum: { algorithm: 'SHA-256', value: checksum } })
  if (new TextEncoder().encode(text).byteLength > PANEL_BACKUP_MAX_BYTES) fail('El respaldo supera el límite máximo de 100 MiB.')
  return text
}

function validateRows(collections: BackupCollections, schemaVersion: number): void {
  for (const store of BACKUP_STORE_NAMES) {
    const storeRows = rows(collections, store)
    if (store !== 'studentImages') for (const row of storeRows) assertJsonValue(row, store)
  }
  const metaRows = rows(collections, 'meta')
  assertUnique(metaRows, (row) => String(row.key), 'meta')
  const meta = new Map(metaRows.map((row) => [String(row.key), row.value]))
  if (meta.get('mode') !== 'demo' || meta.get('schemaVersion') !== schemaVersion || meta.get('seedCompleted') !== true || !Number.isSafeInteger(meta.get('seedVersion'))) fail('Los metadatos no identifican un espacio demo inicializado y compatible.')

  const students = idMap(rows(collections, 'students'), 'student_id', 'Alumnos')
  for (const student of students.values()) {
    if (!isText(student.name, 160) || !student.name.trim() || !isText(student.preferred_name, 160) || !isSafeId(student.grade) || student.grade > 6 || !isText(student.group, 40) || !isSafeId(student.roster_number) || !['ACTIVE', 'INACTIVE'].includes(String(student.status)) || (student.nfc_uid !== null && !isText(student.nfc_uid, 128)) || (student.avatar_asset !== null && !isText(student.avatar_asset, 300)) || (student.level !== null && !isText(student.level, 40))) fail('Un alumno contiene campos o valores fuera del modelo permitido.')
  }
  const accounts = rows(collections, 'accounts'); const accountsById = idMap(accounts, 'student_id', 'Cuentas')
  for (const account of accounts) { requireStudent(students, account.student_id, 'Cuentas'); assertAmount(account.balance, 'Saldo', 0); if (!isSafeId(account.schema_version)) fail('Una cuenta tiene una versión de esquema inválida.') }
  const movements = rows(collections, 'movements'); const movementsById = idMap(movements, 'id', 'Movimientos')
  const claimIndex = idMap(rows(collections, 'claims'), 'id', 'Cobros')
  const applicationIndex = idMap(rows(collections, 'schoolApplications'), 'id', 'Aplicaciones escolares')
  const recordIndex = new Map<number, Record<string, unknown>>([...rows(collections, 'academicRecords'), ...rows(collections, 'attendanceRecords')].map((row) => [Number(row.id), row]))
  for (const movement of movements) {
    requireStudent(students, movement.student_id, 'Movimientos'); assertAmount(movement.amount, 'Movimiento');
    if (!['ENTRY', 'EXIT'].includes(String(movement.type)) || (movement.type === 'ENTRY' && Number(movement.amount) < 0) || (movement.type === 'EXIT' && Number(movement.amount) > 0) || !isText(movement.reason, 500) || !Number.isFinite(movement.timestamp) || Number(movement.timestamp) <= 0 || !['TERMINAL', 'PANEL', 'DEMO'].includes(String(movement.origin)) || typeof movement.synced !== 'boolean' || !isSafeId(movement.schema_version)) fail('Un movimiento contiene tipo, importe o campos inválidos.')
    for (const [field, target] of [['related_movement_id', movementsById], ['related_claim_id', claimIndex], ['authorized_by_claim_id', claimIndex], ['related_school_record_id', recordIndex], ['related_school_application_id', applicationIndex]] as const) if (movement[field] !== undefined) requireRef(target, movement[field], `Movimientos.${field}`)
  }
  const activities = rows(collections, 'activities'); const activitiesById = idMap(activities, 'id', 'Actividades')
  for (const activity of activities) {
    if (typeof activity.activity_number_enabled !== 'boolean' || !isSafeId(activity.activity_number, true) || !isSafeId(activity.reward_amount) || activity.reward_amount > 10_000 || !isSafeId(activity.duration_seconds, true) || !Number.isFinite(activity.started_at) || !Number.isFinite(activity.closed_at) || !['ALL', 'SELECTED', 'PARTICIPANTS_DISABLED'].includes(String(activity.participant_mode)) || !Array.isArray(activity.participant_student_ids) || !['ACTIVE', 'FINISHED', 'CANCELLED'].includes(String(activity.status))) fail('Una actividad contiene valores inválidos.')
    const ids = activity.participant_student_ids
    if (new Set(ids).size !== ids.length) fail('Una actividad repite participantes.')
    for (const id of ids) requireStudent(students, id, 'Actividades')
  }
  const claims = rows(collections, 'claims'); const claimsById = claimIndex
  for (const claim of claims) { requireRef(activitiesById, claim.activity_id, 'Cobros.activity_id'); requireStudent(students, claim.student_id, 'Cobros'); requireRef(movementsById, claim.movement_id, 'Cobros.movement_id'); requireRef(movementsById, claim.void_movement_id, 'Cobros.void_movement_id', true); assertAmount(claim.reward_amount, 'Recompensa', 1, 10_000); if (!Number.isFinite(claim.claimed_at) || !['PAID', 'VOIDED'].includes(String(claim.status))) fail('Un cobro contiene valores inválidos.') }
  const authorizations = rows(collections, 'claimAuthorizations'); const authorizationById = idMap(authorizations, 'id', 'Autorizaciones')
  assertUnique(authorizations, (row) => String(row.source_claim_id), 'Autorizaciones')
  for (const authorization of authorizations) { requireRef(activitiesById, authorization.activity_id, 'Autorizaciones.activity_id'); requireStudent(students, authorization.student_id, 'Autorizaciones'); requireRef(claimsById, authorization.source_claim_id, 'Autorizaciones.source_claim_id'); requireRef(claimsById, authorization.consumed_claim_id, 'Autorizaciones.consumed_claim_id', true); if (!['AUTHORIZED', 'CONSUMED'].includes(String(authorization.status)) || !Number.isFinite(authorization.authorized_at)) fail('Una autorización contiene valores inválidos.') }
  void authorizationById
  const events = rows(collections, 'claimEvents'); const eventById = idMap(events, 'id', 'Eventos de cobro')
  for (const event of events) { requireRef(claimsById, event.claim_id, 'Eventos.claim_id'); requireRef(activitiesById, event.activity_id, 'Eventos.activity_id'); requireStudent(students, event.student_id, 'Eventos'); if (!['VOIDED', 'REAUTHORIZED', 'NEW_CLAIM'].includes(String(event.type)) || event.actor !== 'PANEL_MAESTRO_DEMO' || !Number.isFinite(event.occurred_at)) fail('Un evento de cobro contiene valores inválidos.'); requireRef(authorizationById, event.authorization_id, 'Eventos.authorization_id', true); requireRef(claimsById, event.related_claim_id, 'Eventos.related_claim_id', true) }
  void eventById

  const academic = rows(collections, 'academicRecords'); const academicById = idMap(academic, 'id', 'Evaluaciones')
  assertUnique(academic, (row) => `${row.student_id}|${row.school_date}|${row.kind}`, 'Evaluaciones')
  for (const row of academic) {
    requireStudent(students, row.student_id, 'Evaluaciones')
    if (!/^\d{4}-\d{2}-\d{2}$/.test(String(row.school_date)) || !isSafeId(row.version) || !Number.isFinite(row.recorded_at)) fail('Una evaluación contiene fecha o versión inválida.')
    if (row.kind === 'READING') { if (!isSafeId(row.grade) || row.grade > 6 || !isSafeId(row.ppm, true) || row.ppm > 65_535) fail('Una evaluación de lectura contiene valores inválidos.') }
    else if (row.kind === 'DICTATION') { if (!isSafeId(row.total_words) || !isSafeId(row.correct_words, true) || row.correct_words > row.total_words) fail('Una evaluación de dictado contiene valores inválidos.') }
    else if (row.kind === 'COMPREHENSION' && isRecord(row.scores) && isRecord(row.denominators)) { for (const aspect of aspects) { const score = row.scores[aspect]; const denominator = row.denominators[aspect]; if (!(score === null || (isSafeId(score, true) && score <= Number(denominator))) || !isSafeId(denominator) || denominator > 100) fail('Una evaluación de comprensión contiene valores inválidos.') } }
    else fail('Una evaluación tiene un tipo o estructura inválida.')
  }
  const attendance = rows(collections, 'attendanceRecords'); const attendanceById = idMap(attendance, 'id', 'Asistencias')
  assertUnique(attendance, (row) => `${row.student_id}|${row.school_date}`, 'Asistencias')
  for (const row of attendance) { requireStudent(students, row.student_id, 'Asistencias'); if (!/^\d{4}-\d{2}-\d{2}$/.test(String(row.school_date)) || !['PRESENT', 'LATE', 'ABSENT_UNJUSTIFIED', 'ABSENT_JUSTIFIED'].includes(String(row.status)) || !(row.arrival_at === null || Number.isFinite(row.arrival_at)) || !isText(row.justification, 500) || !isSafeId(row.version) || !Number.isFinite(row.recorded_at)) fail('Una asistencia contiene valores inválidos.') }
  const academicRules = rows(collections, 'academicRuleVersions'); const academicRuleByVersion = idMap(academicRules, 'version', 'Versiones de reglas académicas')
  const attendanceRules = rows(collections, 'attendanceRuleVersions'); const attendanceRuleByVersion = idMap(attendanceRules, 'version', 'Versiones de reglas de asistencia')
  for (const rule of academicRules) { const error = validateAcademicRules(rule as never); if (error || !isSafeId(rule.version)) fail(`Una versión de reglas académicas es inválida: ${error || 'versión inválida'}`) }
  for (const rule of attendanceRules) { const error = validateAttendanceRules(rule as never); if (error || !isSafeId(rule.version)) fail(`Una versión de reglas de asistencia es inválida: ${error || 'versión inválida'}`) }
  if (!academicRuleByVersion.has(Number(meta.get('activeAcademicRuleVersion'))) || !attendanceRuleByVersion.has(Number(meta.get('activeAttendanceRuleVersion')))) fail('Los metadatos apuntan a una versión de reglas inexistente.')
  for (const row of rows(collections, 'schoolApplications')) {
    requireStudent(students, row.student_id, 'Aplicaciones escolares'); assertAmount(row.amount, 'Aplicación'); assertAmount(row.balance_before, 'Saldo anterior', 0); assertAmount(row.balance_after, 'Saldo posterior', 0)
    if (!isSafeId(row.id) || !isSafeId(row.record_id) || !isSafeId(row.record_version) || !isSafeId(row.academic_rules_version) || !isSafeId(row.attendance_rules_version) || !isText(row.result_label, 300) || !isText(row.signature, 1_000) || !Number.isFinite(row.created_at) || !(row.movement_id === null || (isSafeId(row.movement_id) && movementsById.has(row.movement_id)))) fail('Una aplicación escolar contiene valores o referencias inválidas.')
    if (row.record_type === 'ACADEMIC') requireRef(academicById, row.record_id, 'Aplicaciones escolares.record_id')
    else if (row.record_type === 'ATTENDANCE') requireRef(attendanceById, row.record_id, 'Aplicaciones escolares.record_id')
    else fail('Una aplicación escolar tiene un tipo de registro desconocido.')
  }
  assertUnique(rows(collections, 'schoolApplications'), (row) => `${row.record_type}|${row.record_id}|${row.indicator}`, 'Aplicaciones escolares')
  const images = rows(collections, 'studentImages'); idMap(images, 'student_id', 'Fotos')
  for (const image of images) { requireStudent(students, image.student_id, 'Fotos'); if (!(image.blob instanceof Blob) || !['image/png', 'image/jpeg'].includes(String(image.mimeType)) || image.blob.type !== image.mimeType || image.byteLength !== image.blob.size || image.blob.size <= 0 || image.blob.size > PANEL_BACKUP_MAX_IMAGE_BYTES || !isText(image.fileName, 255) || !image.fileName || !isSafeId(image.width) || !isSafeId(image.height) || image.width * image.height > PANEL_BACKUP_MAX_IMAGE_PIXELS || !Number.isFinite(image.updatedAt)) fail('Una foto no cumple el formato, referencia o límites de tamaño/dimensiones.') }
  const counters = meta.get('counters')
  if (!isRecord(counters)) fail('Faltan los contadores del espacio demo.')
  for (const [counter, key] of [['activity', 'activities'], ['authorization', 'claimAuthorizations'], ['claimEvent', 'claimEvents'], ['schoolRecord', 'all'], ['schoolApplication', 'schoolApplications'], ['movement', 'movements'], ['claim', 'claims']] as const) {
    const value = counters[counter]
    const max = key === 'all' ? Math.max(0, ...academic.map((row) => Number(row.id)), ...attendance.map((row) => Number(row.id))) : Math.max(0, ...rows(collections, key as BackupStoreName).map((row) => Number(row.id)))
    // Movement and claim IDs are allocated from their current rows; legacy metadata may lag those rows.
    if (!isSafeId(value) || (counter !== 'movement' && counter !== 'claim' && value <= max)) fail(`El contador “${counter}” no es válido o no supera los IDs existentes.`)
  }
  const used = meta.get('usedAdjustmentConfirmations')
  if (!Array.isArray(used) || used.some((value) => !isText(value, 200))) fail('La lista de confirmaciones usadas no es válida.')
  const receipts = rows(collections, 'operationReceipts'); assertUnique(receipts, (row) => String(row.operationId), 'Comprobantes de operación')
  for (const receipt of receipts) if (!isText(receipt.operationId, 200) || !receipt.operationId || !isText(receipt.type, 100) || !Number.isFinite(receipt.createdAt)) fail('Un comprobante de operación es inválido.')
  void accountsById
}

export async function validateBackupText(text: string, expectedWorkspace: string, expectedSchemaVersion: number): Promise<ValidatedDemoBackup> {
  const size = new TextEncoder().encode(text).byteLength
  if (size > PANEL_BACKUP_MAX_BYTES) fail('El archivo supera el límite máximo de 100 MiB.')
  let raw: unknown
  try { raw = JSON.parse(text) } catch { fail('El archivo no contiene JSON válido.') }
  if (!isRecord(raw)) fail('El archivo no tiene la estructura de respaldo esperada.')
  if (raw.format !== PANEL_BACKUP_FORMAT) fail('El archivo no es un respaldo del Panel Maestro.')
  if (raw.version !== PANEL_BACKUP_VERSION) fail(`La versión de formato ${String(raw.version)} no es compatible; se admite la versión ${PANEL_BACKUP_VERSION}.`)
  if (raw.workspace !== expectedWorkspace) fail('El respaldo pertenece a otro espacio. No se mezclaron los espacios demo y real.')
  if (raw.schemaVersion !== expectedSchemaVersion) fail(`La versión de datos ${String(raw.schemaVersion)} no es compatible con este Panel.`)
  if (!isText(raw.createdAt, 40) || Number.isNaN(Date.parse(raw.createdAt)) || !isSafeId(raw.revision)) fail('La fecha o revisión del respaldo no es válida.')
  const checksum = raw.checksum
  if (!isRecord(checksum) || checksum.algorithm !== 'SHA-256' || !/^[a-f0-9]{64}$/.test(String(checksum.value))) fail('Falta el checksum SHA-256 del respaldo.')
  const expectedChecksum = await sha256(canonical(payloadOf(raw)))
  if (checksum.value !== expectedChecksum) fail('El checksum del respaldo no coincide; el archivo pudo alterarse o dañarse.')
  const rawCollections = raw.collections
  if (!isRecord(rawCollections) || BACKUP_STORE_NAMES.some((store) => !Array.isArray(rawCollections[store])) || Object.keys(rawCollections).some((store) => !BACKUP_STORE_NAMES.includes(store as BackupStoreName))) fail('El respaldo no incluye exactamente todas las colecciones compatibles.')
  const collections = { ...(rawCollections as unknown as BackupCollections) }
  const storedRevision = rows(collections, 'meta').find((row) => row.key === 'revision')?.value
  if (storedRevision !== raw.revision) fail('La revisión del respaldo no coincide con los metadatos guardados.')
  const encodedImages = rows(collections, 'studentImages') as unknown as EncodedImageRow[]
  if (encodedImages.length > PANEL_BACKUP_MAX_IMAGES) fail(`El respaldo supera el límite de ${PANEL_BACKUP_MAX_IMAGES} fotos.`)
  const decoded: BackupImageRow[] = []
  for (const image of encodedImages) {
    if (!isRecord(image) || !isRecord(image.blob) || typeof image.blob.base64 !== 'string' || image.blob.base64.length > Math.ceil(PANEL_BACKUP_MAX_IMAGE_BYTES / 3) * 4 + 4 || image.blob.size > PANEL_BACKUP_MAX_IMAGE_BYTES) fail('Una foto supera los límites permitidos o tiene estructura inválida.')
    const bytes = base64ToBytes(image.blob.base64)
    if (bytes.byteLength !== image.blob.size) fail('El tamaño de una foto no coincide con sus bytes.')
    const imageBuffer = new ArrayBuffer(bytes.byteLength); new Uint8Array(imageBuffer).set(bytes)
    const blob = new Blob([imageBuffer], { type: image.blob.type })
    let bitmap: ImageBitmap
    try { bitmap = await createImageBitmap(blob) } catch { fail('Una foto está dañada o no se puede decodificar.') }
    const dimensionsValid = bitmap.width === image.width && bitmap.height === image.height && bitmap.width * bitmap.height <= PANEL_BACKUP_MAX_IMAGE_PIXELS
    bitmap.close()
    if (!dimensionsValid) fail('Las dimensiones declaradas de una foto no coinciden con la imagen o superan el límite.')
    const isPng = blob.type === 'image/png' && bytes[0] === 137 && bytes[1] === 80 && bytes[2] === 78 && bytes[3] === 71
    const isJpeg = blob.type === 'image/jpeg' && bytes[0] === 255 && bytes[1] === 216 && bytes[2] === 255
    if (!isPng && !isJpeg) fail('Una foto no coincide con su tipo PNG/JPEG declarado.')
    decoded.push({ student_id: image.student_id, mimeType: image.mimeType, fileName: image.fileName, byteLength: image.byteLength, width: image.width, height: image.height, updatedAt: image.updatedAt, blob })
  }
  collections.studentImages = decoded as unknown as unknown[]
  try { validateRows(collections, expectedSchemaVersion) }
  catch (error) { if (error instanceof BackupValidationError) throw error; fail('El respaldo contiene datos incompletos o con tipos inválidos; no se modificó el espacio actual.') }
  return { format: PANEL_BACKUP_FORMAT, version: PANEL_BACKUP_VERSION, workspace: expectedWorkspace, schemaVersion: expectedSchemaVersion, createdAt: raw.createdAt, revision: raw.revision, collections, checksum: checksum as ValidatedDemoBackup['checksum'], bytes: size }
}
