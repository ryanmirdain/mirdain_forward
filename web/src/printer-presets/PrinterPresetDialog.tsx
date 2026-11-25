// Copied from aperture/vendor/mirdain-chat-ui/src/printer-presets/PrinterPresetDialog.tsx.
import { useEffect, useId, useMemo, useRef, useState } from 'react'
import './printer-presets.css'

export type PrinterPreset = {
  id: string; name: string; display_name: string; vendor: string; vendor_id: string
  model: string; variant: string | null; nozzle_diameters: number[]
}

const names = new Intl.Collator(undefined, { numeric: true, sensitivity: 'base' })

function nozzleLabel(preset: PrinterPreset) {
  if (!preset.display_name.toLocaleLowerCase().startsWith(preset.model.toLocaleLowerCase())) return preset.display_name
  return preset.display_name.slice(preset.model.length).trim() || `${preset.nozzle_diameters.join(' / ')} mm nozzle`
}

export function PrinterPresetDialog({ loadPresets, onSelect, onClose, multiple = false, selectedId, addedIds = [], disabled = false }: {
  loadPresets: () => Promise<PrinterPreset[]>
  onSelect: (presets: PrinterPreset[]) => void | Promise<void>
  onClose: () => void
  multiple?: boolean
  selectedId?: string
  addedIds?: string[]
  disabled?: boolean
}) {
  const dialog = useRef<HTMLDialogElement>(null)
  const searchId = useId()
  const titleId = useId()
  const selectionName = useId()
  const [catalog, setCatalog] = useState<PrinterPreset[]>([])
  const [query, setQuery] = useState('')
  const [selectedIds, setSelectedIds] = useState<string[]>(selectedId ? [selectedId] : [])
  const [activeVendor, setActiveVendor] = useState('')
  const [loading, setLoading] = useState(true)
  const [adding, setAdding] = useState(false)
  const [error, setError] = useState('')
  const working = disabled || loading || adding

  useEffect(() => {
    const modal = dialog.current!
    const opener = document.activeElement as HTMLElement | null
    modal.showModal()
    return () => {
      modal.close()
      if (opener?.isConnected) opener.focus()
    }
  }, [])

  useEffect(() => {
    let active = true
    loadPresets().then(presets => {
      if (!active) return
      const sorted = [...presets].sort((a, b) => names.compare(a.vendor, b.vendor) || names.compare(a.model, b.model)
        || (a.nozzle_diameters[0] ?? 0) - (b.nozzle_diameters[0] ?? 0) || names.compare(a.display_name, b.display_name))
      setCatalog(sorted)
      if (selectedId) setActiveVendor(sorted.find(preset => preset.id === selectedId)?.vendor || '')
    }).catch((value: Error) => { if (active) setError(value.message) })
      .finally(() => { if (active) setLoading(false) })
    return () => { active = false }
  }, [loadPresets, selectedId])

  const vendors = useMemo(() => {
    const filter = query.trim().toLocaleLowerCase()
    const groups = new Map<string, Map<string, PrinterPreset[]>>()
    for (const preset of catalog) {
      if (filter && !`${preset.vendor} ${preset.model} ${preset.name} ${preset.display_name} ${preset.variant || ''} ${preset.nozzle_diameters.join(' ')}`.toLocaleLowerCase().includes(filter)) continue
      const models = groups.get(preset.vendor) || new Map<string, PrinterPreset[]>()
      const items = models.get(preset.model) || []
      items.push(preset); models.set(preset.model, items); groups.set(preset.vendor, models)
    }
    return [...groups].map(([name, models]) => ({ name, models: [...models].map(([name, presets]) => ({ name, presets })) }))
  }, [catalog, query])
  const shownVendor = vendors.find(vendor => vendor.name === activeVendor) || vendors[0]

  function close() {
    if (!adding) onClose()
  }

  async function select() {
    if (working || !selectedIds.length) return
    setAdding(true); setError('')
    try {
      await onSelect(selectedIds.map(id => catalog.find(preset => preset.id === id)!))
    } catch (value) {
      setError((value as Error).message)
      setAdding(false)
      return
    }
    onClose()
  }

  return (
    <dialog ref={dialog} className="printer-catalog-dialog" aria-labelledby={titleId} aria-modal="true"
      onCancel={event => { event.preventDefault(); event.stopPropagation(); close() }}
      onClick={event => { if (event.target === event.currentTarget) {
        const bounds = event.currentTarget.getBoundingClientRect()
        if (event.clientX < bounds.left || event.clientX > bounds.right || event.clientY < bounds.top || event.clientY > bounds.bottom) close()
      } }}>
      <h2 id={titleId} className="printer-catalog-sr-only">{multiple ? 'Add generic printers' : 'Select base printer preset'}</h2>
      <form onSubmit={event => { event.preventDefault(); void select() }}>
        <div className="printer-picker-search-row">
          <label htmlFor={searchId} className="printer-catalog-sr-only">Search printer types</label>
          <input autoFocus id={searchId} className="printer-preset-search" type="search" placeholder="Search printer type or nozzle size…"
            value={query} disabled={adding} onChange={event => setQuery(event.target.value)} />
          <button type="button" className="printer-picker-close" disabled={adding} aria-label="Close printer picker" onClick={close}>×</button>
        </div>
        <div className="printer-preset-panels">
          <div className="printer-brand-panel">
            <h3>Brand</h3>
            <div className="printer-brand-list" role="group" aria-label="Printer brands">
              {vendors.map(vendor => <button type="button" key={vendor.name} disabled={working}
                aria-pressed={vendor.name === shownVendor?.name} onClick={() => setActiveVendor(vendor.name)}>
                <span>{vendor.name}</span><small>{vendor.models.length}</small>
              </button>)}
            </div>
          </div>
          <div className="printer-model-panel">
            <fieldset className="printer-model-options" disabled={working}>
              <legend className="printer-catalog-sr-only">{shownVendor?.name || 'Orca'} printer presets</legend>
              <h3>Printer</h3>
              <div className="printer-preset-list" key={shownVendor?.name}>
                {loading ? <p role="status">Loading printer presets…</p> : shownVendor ? shownVendor.models.map(model => {
                  const selectedCount = model.presets.filter(preset => selectedIds.includes(preset.id)).length
                  return <details className="printer-model-group" key={model.name} open={model.presets.some(preset => preset.id === selectedId)}>
                    <summary><span className="printer-model-chevron" aria-hidden /><span>{model.name}</span>
                      <small>{selectedCount ? `${selectedCount} selected` : `${model.presets.length} options`}</small></summary>
                    <div className="printer-nozzle-options" role="group" aria-label={`${model.name} nozzle options`}>
                      {model.presets.map(preset => <label className={`printer-preset-option${selectedIds.includes(preset.id) ? ' is-selected' : ''}`} key={preset.id}>
                        <input type={multiple ? 'checkbox' : 'radio'} name={selectionName} className="printer-preset-checkbox" value={preset.id}
                          checked={selectedIds.includes(preset.id)} onChange={() => setSelectedIds(ids => multiple
                            ? ids.includes(preset.id) ? ids.filter(id => id !== preset.id) : [...ids, preset.id]
                            : [preset.id])} />
                        <span>{nozzleLabel(preset)}</span>
                        {addedIds.includes(preset.id) && <small>Added</small>}
                      </label>)}
                    </div>
                  </details>
                }) : <p>{query ? 'No matching printer presets.' : 'No printer presets available.'}</p>}
              </div>
            </fieldset>
            {error && <p className="printer-picker-error" role="alert">{error}</p>}
            <footer><span className="printer-picker-selection" role="status">{selectedIds.length} selected</span>
              <button type="button" className="button button-ghost" disabled={adding} onClick={close}>Cancel</button>
              <button type="submit" className="button button-primary" disabled={working || !selectedIds.length}>
                {adding ? multiple ? 'Adding…' : 'Selecting…' : multiple ? selectedIds.length === 1 ? 'Add Printer' : 'Add Printers' : 'Select preset'}
              </button>
            </footer>
          </div>
        </div>
      </form>
    </dialog>
  )
}
