import type { ActivityClaim, ActivitySession, MovementRecord, Student, StudentAccount } from '../models/domain'

/** Lectura abstracta para que la UI no dependa de una fuente concreta. */
export interface PanelDataService {
  getStudents(): Promise<readonly Student[]>
  getAccounts(): Promise<readonly StudentAccount[]>
  getMovements(): Promise<readonly MovementRecord[]>
  getActivities(): Promise<readonly ActivitySession[]>
  getClaims(): Promise<readonly ActivityClaim[]>
}
