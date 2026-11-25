// Copied from aperture/src/components/ForwardSetup.tsx without its Verify Forward step:
// these pages are served by Forward, which is signed in and registered with an organization.
import { useEffect, useId, useRef, useState } from 'react'
import { loadForwardPresets, saveForwardPrinter, type AppState, type Connection, type PrinterPreset } from './lib/forward'
import { SelectPicker } from './components/SelectPicker'
import { PrinterPresetDialog } from './printer-presets/PrinterPresetDialog'

function SetupHelp({ text, label }: { text: string; label: string }) {
  const id = useId()
  return <span className="forward-help">
    <button type="button" className="forward-help-trigger" aria-label={`Help: ${label}`} aria-describedby={id}
      onKeyDown={event => { if (event.key === 'Escape') event.currentTarget.blur() }}>
      <svg width="16" height="16" viewBox="0 0 20 20" fill="none" aria-hidden="true">
        <circle cx="10" cy="10" r="8" stroke="currentColor" strokeWidth="1.5" />
        <path d="M7.8 7.5a2.2 2.2 0 1 1 3.3 1.9c-.8.4-1.1.8-1.1 1.6M10 14v.2" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round" />
      </svg>
    </button>
    <span id={id} role="tooltip" className="forward-help-tooltip">{text}</span>
  </span>
}
type Field = { key: string; label: string; hint: string; required?: boolean; type?: 'password' | 'number'; placeholder?: string; pattern?: string }
const protocols = [
  { value: 'elegoo_sdcp', label: 'ELEGOO SDCP', protocol: 'elegoo_sdcp' },
  { value: 'prusalink_api', label: 'PrusaLink · API key', protocol: 'prusalink' },
  { value: 'prusalink_password', label: 'PrusaLink · username and password', protocol: 'prusalink' },
  { value: 'bambu_lan', label: 'Bambu LAN', protocol: 'bambu_lan' },
] as const

function connectionFields(protocol: string): Field[] {
  const fields: Field[] = [{ key: 'host', label: 'Printer IP address or hostname', hint: 'Find this in the printer’s network settings. Use the printer’s address, not this computer’s.', required: true, placeholder: '192.168.1.100', pattern: '\\S+' }]
  if (protocol === 'bambu_lan') fields.push(
    { key: 'serial_number', label: 'Printer serial number', hint: 'Copy the serial number from the printer’s device information.', required: true, pattern: '[^\\s/+#]+' },
    { key: 'access_code', label: 'LAN access code', hint: 'Find the access code in the printer’s LAN settings.', required: true, type: 'password', pattern: '\\S+' },
  )
  if (protocol === 'prusalink_api') fields.push({ key: 'api_key', label: 'PrusaLink API key', hint: 'Open PrusaLink in your browser and copy its API key from Settings.', required: true, type: 'password', pattern: '.*\\S.*' })
  if (protocol === 'prusalink_password') fields.push(
    { key: 'username', label: 'PrusaLink username', hint: 'Use the username shown in the printer’s PrusaLink settings.', placeholder: 'maker' },
    { key: 'password', label: 'PrusaLink password', hint: 'Use the printer’s PrusaLink password, rather than your Mirdain password.', required: true, type: 'password' },
  )
  return fields
}

// The two steps are controlled by App, which shows the progress and the saved printer.
export function AddPrinter({ state, step, onStep, onBack, onSkip, onSaved }: {
  state: AppState; step: 0 | 1; onStep: (step: 0 | 1, direction: 'forward' | 'back') => void
  onBack?: () => void; onSkip?: () => void; onSaved: (printer: { id: string; name: string }) => void
}) {
  const organization = state.organization!
  const [preset, setPreset] = useState<PrinterPreset | null>(null)
  const [choosingPreset, setChoosingPreset] = useState(false)
  const [description, setDescription] = useState('')
  const [name, setName] = useState('')
  const [protocol, setProtocol] = useState('elegoo_sdcp')
  const [settings, setSettings] = useState<Record<string, string>>({})
  const [pending, setPending] = useState(false)
  const [error, setError] = useState('')
  const heading = useRef<HTMLHeadingElement>(null)
  const vendor = preset?.vendor || ''
  const presetId = preset?.id || ''
  const fields = connectionFields(protocol)
  const connectionHelp = 'Turn on your printer and connect it to the same network as this computer. ' + (protocol === 'bambu_lan'
    ? 'Enable LAN Only mode on your Bambu printer, and Developer Mode if required by its firmware. Find the IP address, serial number, and LAN access code in the printer’s settings.'
    : protocol.startsWith('prusalink')
      ? 'Enable PrusaLink in the printer’s network settings. Open its IP address in a browser to confirm it is available, then copy its login details.'
      : 'Use an ELEGOO printer with SDCP support. Find its IP address in the network settings.')

  useEffect(() => { heading.current?.focus() }, [step])

  return <section className="forward-setup" aria-label="Add a printer">
    <div className="forward-step-content" aria-busy={pending}>
      <div className="forward-heading">
        <h2 ref={heading} tabIndex={-1}>{['Meet your printer.', 'Connect your printer.'][step]}</h2>
      </div>
      {step === 0 && <form onSubmit={event => { event.preventDefault(); setError(''); onStep(1, 'forward') }}>
        <p>Choose your printer and give it a name. It is added to <strong>{organization.name}</strong>.</p>
        <div className="forward-field"><label htmlFor="forward-printer-name">Name</label>
          <input id="forward-printer-name" required maxLength={100} value={name} placeholder="Workshop printer" onChange={event => setName(event.target.value)} /></div>
        <div className="forward-field"><label htmlFor="forward-printer-description">Description</label>
          <textarea id="forward-printer-description" rows={2} value={description} onChange={event => setDescription(event.target.value)} /></div>
        <div className="forward-field"><span id="forward-base-label" className="forward-field-label">Base printer preset</span>
          <button type="button" className="picker-trigger" aria-labelledby="forward-base-label forward-base-value" aria-haspopup="dialog" onClick={() => setChoosingPreset(true)}>
            <span id="forward-base-value">{preset?.display_name || 'Select base printer preset'}</span>
            <span className="picker-chevron" aria-hidden="true"><span /></span>
          </button></div>
        <div className="forward-step-actions">
          {onSkip && <button type="button" className="forward-skip" onClick={onSkip}>Skip</button>}
          <button className="button" disabled={!name.trim() || !preset}>Continue to connection<span aria-hidden="true">→</span></button>
        </div>
      </form>}
      {step === 1 && <form onSubmit={async event => {
        event.preventDefault()
        const config = Object.fromEntries(fields.flatMap(field => {
          const value = field.key === 'password' ? settings[field.key] || '' : (settings[field.key] || '').trim()
          return value ? [[field.key, value]] : []
        }))
        if (pending) return
        setPending(true); setError('')
        try {
          const connection: Connection = { ...config, protocol: protocols.find(item => item.value === protocol)!.protocol, host: settings.host.trim() }
          if (settings.port) connection.port = Number(settings.port)
          if (protocol === 'bambu_lan' && settings.ftp_port) connection.ftp_port = Number(settings.ftp_port)
          const printer = await saveForwardPrinter(name.trim(), description, presetId, connection)
          setSettings({})
          onSaved(printer)
        } catch (value) { setError((value as Error).message) }
        finally { setPending(false) }
      }}>
        <div className="forward-field"><div className="forward-field-label"><span id="forward-protocol-label">Connection method</span><SetupHelp label="Connection method" text={connectionHelp} /></div>
          <SelectPicker value={protocol} ariaLabelledBy="forward-protocol-label" options={protocols.filter(item => item.protocol === (/prusa/i.test(vendor) ? 'prusalink' : /bambu|bbl/i.test(vendor) ? 'bambu_lan' : 'elegoo_sdcp')).map(item => ({ value: item.value, label: item.label }))}
            onValueChange={value => { setProtocol(value); setSettings({}) }} /></div>
        {fields.map(field => <div className="forward-field" key={field.key}>
          <div className="forward-field-label"><label htmlFor={`forward-${field.key}`}>{field.label}</label><SetupHelp label={field.label} text={field.hint} /></div>
          <input id={`forward-${field.key}`} type={field.type || 'text'} required={field.required} pattern={field.pattern} placeholder={field.placeholder} autoComplete="off"
            value={settings[field.key] || ''} onChange={event => { setSettings(current => ({ ...current, [field.key]: event.target.value })) }} />
        </div>)}
        <details className="forward-advanced"><summary>Advanced connection settings</summary>
          <div className="forward-field"><label htmlFor="forward-port">{protocol === 'bambu_lan' ? 'MQTT port' : 'HTTP port'} (optional)</label>
            <input id="forward-port" type="number" min={1} max={65535} placeholder={protocol === 'bambu_lan' ? '8883' : protocol === 'elegoo_sdcp' ? '3030' : '80'} value={settings.port || ''} onChange={event => setSettings(current => ({ ...current, port: event.target.value }))} /></div>
          {protocol === 'bambu_lan' && <div className="forward-field"><label htmlFor="forward-ftp-port">FTPS port (optional)</label><input id="forward-ftp-port" type="number" min={1} max={65535} placeholder="990" value={settings.ftp_port || ''} onChange={event => setSettings(current => ({ ...current, ftp_port: event.target.value }))} /></div>}
        </details>
        <div className="forward-step-actions"><button className="button" disabled={pending}>{pending ? 'Saving your printer…' : 'Save printer'}<span aria-hidden="true">→</span></button></div>
      </form>}
      {error && <div className="forward-setup-error" role="alert"><p>{error}</p></div>}
      {(step > 0 || onBack) && <button className="forward-back" disabled={pending} onClick={() => {
        setError('')
        if (step === 0) onBack!()
        else onStep(0, 'back')
      }}>← Back</button>}
    </div>
    {choosingPreset && <PrinterPresetDialog loadPresets={loadForwardPresets} selectedId={presetId || undefined}
      onClose={() => setChoosingPreset(false)} onSelect={items => {
        const selected = items[0]
        if (selected.id !== presetId) {
          setSettings({})
          setProtocol(/prusa/i.test(selected.vendor) ? 'prusalink_api' : /bambu|bbl/i.test(selected.vendor) ? 'bambu_lan' : 'elegoo_sdcp')
        }
        setPreset(selected)
      }} />}
  </section>
}
