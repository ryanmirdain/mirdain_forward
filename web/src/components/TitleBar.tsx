// Forward's window has no native title bar: the page's header replaces it, as in the slicer.
// macOS keeps its traffic lights on the left and drags through the bridge; Windows drags
// through CSS app-region and asks Forward to run the caption buttons. In a browser (Linux
// builds) the header is plain.
type Bridge = { titleBar?: 'left' | 'right' }
declare global {
  interface Window {
    mirdainForward?: Bridge
    webkit?: { messageHandlers: { mirdainForward: { postMessage: (message: unknown) => Promise<unknown> } } }
    chrome?: { webview?: { postMessage: (message: unknown) => void } }
  }
}

const titleBar = window.mirdainForward?.titleBar
let messageId = 0
function windowAction(method: string) {
  if (titleBar === 'left') void window.webkit?.messageHandlers.mirdainForward.postMessage({ method })
  else window.chrome?.webview?.postMessage({ id: ++messageId, method })
}

// Closes Forward's window; Forward keeps running in the menu bar. Null in a browser, which
// cannot close a tab it did not open.
export const closeWindow = titleBar ? () => windowAction('close') : null

export function TitleBar() {
  return <header className={`title-bar${titleBar ? ` title-bar-${titleBar}` : ''}`}
    onMouseDown={event => { if (titleBar === 'left' && event.button === 0 && event.target === event.currentTarget) windowAction('startDrag') }}
    onDoubleClick={event => { if (titleBar === 'left' && event.target === event.currentTarget) windowAction('titleBarDoubleClick') }}>
    <span className="title-bar-name">Mirdain Forward</span>
    {titleBar === 'right' && <div className="title-bar-controls">
      <button aria-label="Minimize" onClick={() => windowAction('minimize')}><svg viewBox="0 0 10 10" aria-hidden="true"><path d="M0 5h10" /></svg></button>
      <button aria-label="Maximize" onClick={() => windowAction('toggleMaximize')}><svg viewBox="0 0 10 10" aria-hidden="true"><path d="M.5.5h9v9h-9z" /></svg></button>
      <button className="title-bar-close" aria-label="Close" onClick={() => windowAction('close')}><svg viewBox="0 0 10 10" aria-hidden="true"><path d="m0 0 10 10M10 0 0 10" /></svg></button>
    </div>}
  </header>
}
