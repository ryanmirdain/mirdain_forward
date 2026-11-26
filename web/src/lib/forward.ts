import type { PrinterPreset } from '../printer-presets/PrinterPresetDialog'
export type { PrinterPreset } from '../printer-presets/PrinterPresetDialog'

// Forward serves these pages and answers /api/app/*. It keeps the Mirdain session and
// calls the Mirdain API for the page, so no tokens reach the browser.
export type Preferences = {
  saved: boolean
  launchAtSignIn: boolean
  launchSupported: boolean
  launchNeedsApproval: boolean
  notifications: boolean
  updates: boolean
  updatesAvailable: boolean
}
export type Organization = { id: number; name: string }
export type AppState = {
  preferences: Preferences
  signedIn: boolean
  email: string | null
  signInError: string | null
  organization: Organization | null
  printers: number
  slicerUrl: string
}
export type Membership = { id: number; name: string; role: string; accepted: boolean }
export type Connection = { protocol: 'elegoo_sdcp' | 'prusalink' | 'bambu_lan' | 'moonraker' | 'octoprint' | 'flashforge_lan' | 'creality_lan' | 'duet' | 'snapmaker_http' | 'ultimaker_api' | 'dremel_lan' | 'esp3d' | 'mks_wifi' | 'simplyprint'; host: string; [key: string]: string | number }

async function request<T>(path: string, init: RequestInit = {}): Promise<T> {
  let response: Response
  try {
    response = await fetch(`/api/app/${path}`, { ...init, cache: 'no-store',
      headers: init.body ? { 'Content-Type': 'application/json' } : undefined })
  } catch { throw new Error('Mirdain Forward is not responding. Make sure it is still running.') }
  const body = response.status === 204 ? null : await response.json()
  if (!response.ok) throw new Error(body?.error || `Forward returned HTTP ${response.status}.`)
  return body
}
const post = <T>(path: string, body: unknown = {}) => request<T>(path, { method: 'POST', body: JSON.stringify(body) })

export const loadState = () => request<AppState>('state')
export const savePreferences = (preferences: { launchAtSignIn: boolean; notifications: boolean; updates: boolean }) => post<AppState>('preferences', preferences)
export const openStartupSettings = () => post<null>('startup-settings')
// Opens Mirdain sign-in in the default browser; the page polls loadState until it completes.
export const startSignIn = () => post<null>('sign-in')
export const signOut = () => post<AppState>('sign-out')
export const loadOrganizations = () => request<{ memberships: Membership[]; selected: number | null }>('organizations')
// Registers Forward with the organization and loads its Forward printers into the menu bar.
export const chooseOrganization = (organization: Organization) => post<AppState>('organization', organization)
// All presets; Add Printer keeps the vendors that have a Forward connection method.
export const loadForwardPresets = () => request<PrinterPreset[]>('printer-presets')
export const saveForwardPrinter = (name: string, description: string, preset: string, connection: Connection) =>
  post<{ id: string; name: string }>('printers', { name, description: description.trim() || null, base_printer_preset_id: preset, connection })

export function slicerDestination(slicerUrl: string, organization: number, printer?: string) {
  const url = new URL(slicerUrl)
  // force_org keeps the slicer on Forward's organization, as Forward's own slicer page does.
  url.search = new URLSearchParams({ organization: String(organization), force_org: 'true', ...printer ? { custom_printer: printer } : {} }).toString()
  return url.href
}
