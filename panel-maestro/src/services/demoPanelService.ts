import { DemoPanelDataService } from './DemoPanelDataService'

/** Instancia compartida durante la sesión del navegador; no persiste al recargar. */
export const demoPanelService = new DemoPanelDataService()
