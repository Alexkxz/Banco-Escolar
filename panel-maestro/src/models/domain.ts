/** Modelos orientativos de lectura del cliente; no constituyen un contrato de API. */
export interface Student {
  student_id: number
  nfc_uid: string | null
  name: string
  preferred_name: string
  grade: number
  group: string
  roster_number: number
  avatar_asset: string | null
  level: string | null
  /** Estado visual ficticio del directorio; el modelo de firmware no define este campo. */
  status: 'ACTIVE' | 'INACTIVE'
}

/** StudentAccount persistente en firmware; distinto de AccountRecord demo en RAM. */
export interface StudentAccount {
  student_id: number
  balance: number
  schema_version: number
}

export interface MovementRecord {
  id: number
  student_id: number
  amount: number // Con signo: entradas positivas y salidas negativas.
  type: 'ENTRY' | 'EXIT'
  reason: string
  timestamp: number // Segundos Unix.
  origin: 'TERMINAL' | 'PANEL' | 'DEMO' // DEMO es exclusivo del cliente; el firmware no tiene ese origen.
  synced: boolean
  schema_version: number
  /** Referencias extra exclusivas de la demo para auditar inversiones y recobros. */
  related_claim_id?: number
  related_movement_id?: number
  authorized_by_claim_id?: number
  /** Referencia exclusiva de auditoría demo para reconciliación manual PM.7A. */
  related_school_record_id?: number
  related_school_application_id?: number
}

export interface ActivitySession {
  id: number
  activity_number_enabled: boolean
  activity_number: number
  reward_amount: number
  duration_seconds: number
  participant_mode: 'ALL' | 'SELECTED' | 'PARTICIPANTS_DISABLED'
  participant_student_ids: number[]
  started_at: number
  status: 'ACTIVE' | 'FINISHED' | 'CANCELLED'
  closed_at: number
}

export interface ActivityClaim {
  id: number
  activity_id: number
  student_id: number
  reward_amount: number
  claimed_at: number
  movement_id: number
  status: 'PAID' | 'VOIDED'
  void_movement_id: number
  /** Metadatos de la demo; ActivityClaim de firmware todavía no los almacena. */
  voided_at?: number
  void_reason?: string
  voided_by?: 'PANEL_MAESTRO_DEMO'
  authorized_by_claim_id?: number
}

export interface DemoClaimAuthorization {
  id: number
  activity_id: number
  student_id: number
  source_claim_id: number
  authorized_at: number
  status: 'AUTHORIZED' | 'CONSUMED'
  consumed_at?: number
  consumed_claim_id?: number
}

export interface DemoClaimEvent {
  id: number
  claim_id: number
  activity_id: number
  student_id: number
  type: 'VOIDED' | 'REAUTHORIZED' | 'NEW_CLAIM'
  occurred_at: number
  actor: 'PANEL_MAESTRO_DEMO'
  reason?: string
  authorization_id?: number
  related_claim_id?: number
}
