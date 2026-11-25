import { useEffect, useRef, useState, type ReactNode, type RefObject } from 'react'
import { AddPrinter } from './AddPrinter'
import { SelectPicker } from './components/SelectPicker'
import { closeWindow, TitleBar } from './components/TitleBar'
import { chooseOrganization, loadOrganizations, loadState, openStartupSettings, savePreferences, signOut, slicerDestination, startSignIn, type AppState, type Membership } from './lib/forward'
import { stepTransition } from './lib/step-transition'

// Forward opens #/add-printer, #/organization and #/preferences from its menu bar; anything
// else is onboarding.
type Route = 'onboarding' | 'add-printer' | 'organization' | 'preferences'
type Step = 'preferences' | 'sign-in' | 'organization' | 'printer' | 'connection' | 'ready' | 'saved'
const labels: Partial<Record<Step, string>> = {
  preferences: 'Preferences', 'sign-in': 'Sign in', organization: 'Organization', printer: 'Your printer', connection: 'Connection',
}
// The steps each route counts in its progress; Ready and the saved printer sit after them.
const routeSteps: Record<Route, Step[]> = {
  onboarding: ['preferences', 'sign-in', 'organization', 'printer', 'connection'],
  'add-printer': ['printer', 'connection'],
  organization: [],
  preferences: [],
}

function currentRoute(): Route {
  const routes: Record<string, Route> = { '#/add-printer': 'add-printer', '#/organization': 'organization', '#/preferences': 'preferences' }
  return routes[location.hash] ?? 'onboarding'
}

// The first step the route still needs, given what Forward already has.
function nextStep(state: AppState, route: Route): Step {
  if (route === 'preferences') return 'preferences'
  if (route === 'onboarding' && !state.preferences.saved) return 'preferences'
  if (!state.signedIn) return 'sign-in'
  if (!state.organization || route === 'organization') return 'organization'
  return 'printer'
}

// The first of the two progress bubbles shown: the previous step and the current one, or the
// current and next on the first step. -1 when the step has none.
function progressStart(route: Route, step: Step | undefined) {
  const steps = routeSteps[route]
  const index = step ? steps.indexOf(step) : -1
  return index < 0 ? -1 : Math.max(0, Math.min(index - 1, steps.length - 2))
}

function Progress({ route, step }: { route: Route; step: Step | undefined }) {
  const steps = routeSteps[route]
  const start = progressStart(route, step)
  const index = step ? steps.indexOf(step) : -1
  // The slot stays when empty, so the step below keeps its place.
  return <div className="onboarding-progress-slot">
    {start >= 0 && <ol className="forward-steps onboarding-progress">
      {steps.slice(start, start + 2).map((item, offset) => {
        const position = start + offset
        return <li key={item} aria-current={position === index ? 'step' : undefined} className={position < index ? 'is-done' : ''}>
          <span aria-hidden="true">{position < index ? '✓' : position + 1}</span>{labels[item]}
        </li>
      })}
    </ol>}
  </div>
}

// The column is centered, so a step that grows or shrinks in place (an error, Advanced
// settings) moves it. This glides it from where it was instead. Step changes glide in their
// view transition, and window resizes re-center at once.
function useGlide(column: RefObject<HTMLDivElement | null>) {
  useEffect(() => {
    const element = column.current!
    let top = element.getBoundingClientRect().top
    let viewport = `${innerWidth}x${innerHeight}`
    const observer = new ResizeObserver(() => {
      const next = element.getBoundingClientRect().top
      const moved = top - next
      const size = `${innerWidth}x${innerHeight}`
      const resized = size !== viewport
      top = next; viewport = size
      if (!moved || resized || document.documentElement.dataset.step || matchMedia('(prefers-reduced-motion: reduce)').matches) return
      element.animate([{ transform: `translateY(${moved}px)` }, { transform: 'none' }], { duration: 300, easing: 'cubic-bezier(.2, .8, .2, 1)' })
    })
    observer.observe(element)
    return () => observer.disconnect()
  }, [column])
}

export function App() {
  const [state, setState] = useState<AppState>()
  const [route, setRoute] = useState(currentRoute)
  const [step, setStep] = useState<Step>()
  const [saved, setSaved] = useState<{ id: string; name: string }>()
  // Starts a fresh Add Printer form for "Add another printer".
  const [form, setForm] = useState(0)
  const [error, setError] = useState('')
  const column = useRef<HTMLDivElement>(null)
  useGlide(column)
  const current = useRef({ route, step })
  current.current = { route, step }

  // Moves to another step; the progress slides when its pair of bubbles changes.
  function go(direction: 'forward' | 'back', next: Step, nextRoute = current.current.route, update?: () => void) {
    // Bubbles that appear or disappear fade instead.
    const [from, to] = [progressStart(current.current.route, current.current.step), progressStart(nextRoute, next)]
    const shift = from >= 0 && to >= 0 && from !== to
    stepTransition(direction, () => { update?.(); setRoute(nextRoute); setStep(next) }, shift)
  }

  useEffect(() => {
    loadState().then(loaded => { setState(loaded); setStep(nextStep(loaded, route)) }).catch(value => setError((value as Error).message))
    const changed = () => {
      const next = currentRoute()
      // Forward reuses this page when its menu opens another route.
      loadState().then(loaded => go('forward', nextStep(loaded, next), next, () => { setState(loaded); setForm(value => value + 1) }))
    }
    window.addEventListener('hashchange', changed)
    return () => window.removeEventListener('hashchange', changed)
  }, [])

  function advance(updated: AppState, after: Step) {
    const next = after === 'organization' ? route === 'organization' ? 'ready' : 'printer' : nextStep(updated, route)
    go('forward', next, route, () => setState(updated))
  }

  let content: ReactNode
  if (error) content = <div className="forward-setup-error" role="alert"><p>{error}</p></div>
  else if (!state || !step) content = <p role="status" className="onboarding-loading">Loading…</p>
  else if (step === 'printer' || step === 'connection') content = <AddPrinter key={form} state={state} step={step === 'printer' ? 0 : 1}
    onStep={(next, direction) => go(direction, next ? 'connection' : 'printer')}
    onBack={route === 'onboarding' ? () => go('back', 'organization') : undefined}
    onSkip={route === 'onboarding' ? () => go('forward', 'ready') : undefined}
    onSaved={printer => go('forward', 'saved', route, () => setSaved(printer))} />
  else if (step === 'saved') content = <SavedStep state={state} printer={saved!}
    onAddAnother={() => go('forward', 'printer', route, () => setForm(value => value + 1))} />
  else {
    const steps = routeSteps[route]
    const index = steps.indexOf(step)
    content = <section className="forward-setup" aria-label="Set up Mirdain Forward">
      <div className="forward-step-content">
        {step === 'preferences' && <PreferencesStep state={state} standalone={route === 'preferences'}
          onDone={updated => route === 'preferences' ? setState(updated) : advance(updated, 'preferences')} />}
        {step === 'sign-in' && <SignInStep state={state} onState={setState} onDone={updated => advance(updated, 'sign-in')} />}
        {step === 'organization' && <OrganizationStep state={state} onDone={updated => advance(updated, 'organization')} />}
        {step === 'ready' && <ReadyStep state={state} />}
        {index > 0 && <button className="forward-back" onClick={() => go('back', steps[index - 1])}>← Back</button>}
        {step === 'ready' && <button className="forward-back" onClick={() => go('back', route === 'organization' ? 'organization' : 'printer')}>← Back</button>}
      </div>
    </section>
  }
  return <>
    <TitleBar />
    <main className="onboarding-main"><div className="onboarding-column" ref={column}>
      <Progress route={route} step={step} />
      <div className="onboarding-step">{content}</div>
    </div></main>
  </>
}

function useFocusedHeading() {
  const heading = useRef<HTMLHeadingElement>(null)
  useEffect(() => { heading.current?.focus() }, [])
  return heading
}

function Toggle({ id, label, description, checked, disabled, onChange, children }: {
  id: string; label: string; description: string; checked: boolean; disabled?: boolean; onChange: (checked: boolean) => void; children?: ReactNode
}) {
  return <div className="onboarding-toggle">
    <div>
      <label htmlFor={id}>{label}</label>
      <p id={`${id}-description`}>{description}</p>
      {children}
    </div>
    <input id={id} type="checkbox" role="switch" className="switch" checked={checked} disabled={disabled}
      aria-describedby={`${id}-description`} onChange={event => onChange(event.target.checked)} />
  </div>
}

// The first onboarding step, and the whole page for Preferences… in the menu (standalone).
function PreferencesStep({ state, standalone, onDone }: { state: AppState; standalone: boolean; onDone: (state: AppState) => void }) {
  const heading = useFocusedHeading()
  const preferences = state.preferences
  // Forward launches at sign-in and notifies by default for new installs.
  const [launch, setLaunch] = useState(preferences.saved ? preferences.launchAtSignIn : preferences.launchSupported)
  const [notify, setNotify] = useState(preferences.saved ? preferences.notifications : true)
  const [updates, setUpdates] = useState(preferences.saved ? preferences.updates : preferences.updatesAvailable)
  const [saved, setSaved] = useState(false)
  const [pending, setPending] = useState(false)
  const [error, setError] = useState('')
  const [needsApproval, setNeedsApproval] = useState(false)
  async function save() {
    setPending(true); setError(''); setSaved(false)
    try {
      const updated = await savePreferences({ launchAtSignIn: launch, notifications: notify, updates })
      // macOS asks the user to approve login items in System Settings before they run.
      if (updated.preferences.launchNeedsApproval && !needsApproval) { setNeedsApproval(true); return }
      setSaved(standalone)
      onDone(updated)
    } catch (value) { setError((value as Error).message) }
    finally { setPending(false) }
  }
  return <form onSubmit={event => { event.preventDefault(); void save() }}>
    <div className="forward-heading"><h2 ref={heading} tabIndex={-1}>{standalone ? 'Preferences.' : 'Welcome to Forward.'}</h2>
      {standalone && closeWindow && <button type="button" className="onboarding-close" aria-label="Close preferences" onClick={closeWindow}>
        <svg viewBox="0 0 16 16" aria-hidden="true"><path d="m3 3 10 10M13 3 3 13" /></svg>
      </button>}
    </div>
    {!standalone && <p>Forward is Mirdain’s slicer for this computer. It sends your sliced plates straight to printers on your network.</p>}
    <div className="onboarding-toggles">
      <Toggle id="launch" label="Launch at sign-in" checked={launch} disabled={!preferences.launchSupported} onChange={setLaunch}
        description={preferences.launchSupported ? 'Keep Forward running so the slicer can always reach your printers.' : 'Automatic startup requires macOS 13 or later, or Windows.'}>
        {needsApproval && <p className="onboarding-note">Approve Mirdain Forward in Login Items so it can start automatically. <button type="button" className="forward-retry" onClick={() => void openStartupSettings()}>Open Login Items</button></p>}
      </Toggle>
      <Toggle id="notifications" label="Plate notifications" checked={notify} onChange={setNotify}
        description="Notify me when a plate is received or saved to USB." />
      <Toggle id="updates" label="Automatically check for updates" checked={updates} disabled={!preferences.updatesAvailable} onChange={setUpdates}
        description={preferences.updatesAvailable ? 'Check for new versions in the background. You choose when to install.' : 'Updates are unavailable in this build or installation.'} />
    </div>
    {error && <div className="forward-setup-error" role="alert"><p>{error}</p></div>}
    <div className="forward-step-actions">
      {saved && <p className="onboarding-saved" role="status">Preferences saved.</p>}
      <button className="button" disabled={pending}>{pending ? 'Saving…' : standalone ? 'Save preferences' : <>Continue<span aria-hidden="true">→</span></>}</button>
    </div>
  </form>
}

function SignInStep({ state, onState, onDone }: { state: AppState; onState: (state: AppState) => void; onDone: (state: AppState) => void }) {
  const heading = useFocusedHeading()
  const [waiting, setWaiting] = useState(false)
  const [error, setError] = useState('')
  const done = useRef(onDone)
  done.current = onDone
  useEffect(() => {
    if (!waiting) return
    // Forward finishes the sign-in when the browser returns to it; poll until it has.
    const timer = window.setInterval(() => {
      loadState().then(updated => {
        if (updated.signedIn) { setWaiting(false); done.current(updated) }
        else if (updated.signInError) { setWaiting(false); setError(updated.signInError) }
      }).catch(() => { /* keep polling */ })
    }, 1500)
    return () => window.clearInterval(timer)
  }, [waiting])
  async function begin() {
    setError('')
    try { await startSignIn(); setWaiting(true) }
    catch (value) { setError((value as Error).message) }
  }
  async function switchAccount() {
    try { onState(await signOut()); await begin() }
    catch (value) { setError((value as Error).message) }
  }

  if (state.signedIn && !waiting) return <>
    <div className="forward-heading"><h2 ref={heading} tabIndex={-1}>You’re signed in.</h2></div>
    <p>Forward is signed in as <strong>{state.email}</strong>.</p>
    <div className="forward-step-actions">
      <button className="forward-skip" onClick={() => void switchAccount()}>Use a different account</button>
      <button className="button" onClick={() => onDone(state)}>Continue<span aria-hidden="true">→</span></button>
    </div>
  </>
  return <>
    <div className="forward-heading"><h2 ref={heading} tabIndex={-1}>Sign in to Mirdain.</h2></div>
    {waiting ? <>
      <p className="onboarding-waiting" role="status"><span className="onboarding-spinner" aria-hidden="true" />Finish signing in in your browser. Forward continues here when you’re done.</p>
      <div className="forward-step-actions">
        <button className="forward-skip" onClick={() => setWaiting(false)}>Cancel</button>
        <button className="button button-secondary" onClick={() => void begin()}>Open the sign-in page again</button>
      </div>
    </> : <>
      <p>Your browser will open Mirdain’s sign-in page. Forward uses your account to load your organization’s printers.</p>
      <div className="forward-step-actions"><button className="button" onClick={() => void begin()}>Sign in with your browser<span aria-hidden="true">↗</span></button></div>
    </>}
    {error && <div className="forward-setup-error" role="alert"><p>{error}</p></div>}
  </>
}

function OrganizationStep({ state, onDone }: { state: AppState; onDone: (state: AppState) => void }) {
  const heading = useFocusedHeading()
  const [memberships, setMemberships] = useState<Membership[]>()
  const [organization, setOrganization] = useState('')
  const [attempt, setAttempt] = useState(0)
  const [pending, setPending] = useState(false)
  const [error, setError] = useState('')
  useEffect(() => {
    let active = true
    setMemberships(undefined); setError('')
    loadOrganizations().then(result => {
      if (!active) return
      setMemberships(result.memberships)
      const preferred = [state.organization?.id, result.selected].find(id => result.memberships.some(item => item.id === id))
      setOrganization(String(preferred ?? result.memberships[0]?.id ?? ''))
    }).catch(value => { if (active) setError((value as Error).message) })
    return () => { active = false }
  }, [attempt, state.organization?.id])
  async function save() {
    const chosen = memberships!.find(item => String(item.id) === organization)!
    setPending(true); setError('')
    try { onDone(await chooseOrganization({ id: chosen.id, name: chosen.name })) }
    catch (value) { setError((value as Error).message) }
    finally { setPending(false) }
  }
  return <form onSubmit={event => { event.preventDefault(); void save() }}>
    <div className="forward-heading"><h2 ref={heading} tabIndex={-1}>Choose your organization.</h2></div>
    <p>Forward uses this organization for the slicer and to send to printers. You can change it later from the menu bar.</p>
    {memberships && !memberships.length && <p>Ask an organization owner to invite you, then <button type="button" className="forward-retry" onClick={() => setAttempt(value => value + 1)}>check again</button>.</p>}
    {(!memberships || memberships.length > 0) && <div className="forward-field" aria-busy={!memberships}><span id="organization-label" className="forward-field-label">Organization</span>
      <SelectPicker value={organization} ariaLabelledBy="organization-label" onValueChange={setOrganization} placeholder="" disabled={!memberships}
        options={(memberships ?? []).map(item => ({ value: String(item.id), label: item.name }))} /></div>}
    {error && <div className="forward-setup-error" role="alert"><p>{error}</p>{!memberships && <button type="button" className="forward-retry" onClick={() => setAttempt(value => value + 1)}>Retry</button>}</div>}
    <div className="forward-step-actions"><button className="button" disabled={pending || !organization}>{pending ? 'Loading printers…' : 'Continue'}<span aria-hidden="true">→</span></button></div>
  </form>
}

function ReadyStep({ state }: { state: AppState }) {
  const heading = useFocusedHeading()
  const organization = state.organization!
  return <div className="onboarding-ready">
    <div className="forward-heading"><h2 ref={heading} tabIndex={-1}>Forward is ready.</h2></div>
    <p>Forward is registered with <strong>{organization.name}</strong>. {state.printers
      ? `${state.printers} ${state.printers === 1 ? 'printer is' : 'printers are'} in the menu bar.`
      : 'Add a printer any time from Add Printer… in the menu bar.'}</p>
    <div className="onboarding-choices">
      <a className="button" href={slicerDestination(state.slicerUrl, organization.id)}>Open the slicer<span aria-hidden="true">→</span></a>
    </div>
  </div>
}

function SavedStep({ state, printer, onAddAnother }: { state: AppState; printer: { id: string; name: string }; onAddAnother: () => void }) {
  const heading = useFocusedHeading()
  const organization = state.organization!
  return <section className="forward-setup forward-complete" aria-labelledby="forward-complete-title">
    <h2 ref={heading} tabIndex={-1} id="forward-complete-title">You’re all set.</h2>
    <p><strong>{printer.name}</strong> is saved to {organization.name} and listed in Forward’s menu bar.</p>
    <p>In the slicer, prepare your part, slice it, and choose <strong>Send and print</strong>.</p>
    <div className="onboarding-choices">
      <a className="button" href={slicerDestination(state.slicerUrl, organization.id, printer.id)}>Open the slicer<span aria-hidden="true">→</span></a>
      <button className="button button-secondary" onClick={onAddAnother}>Add another printer</button>
    </div>
  </section>
}
