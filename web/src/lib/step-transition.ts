import { flushSync } from 'react-dom'

// Copied from aperture/src/lib/step-transition.ts (the Order Parts step animation).
// Slides between steps with a view transition where supported. The update runs synchronously
// inside the transition so the new step is captured. progressShift slides the progress bubbles
// by one too; otherwise they cross-fade.
//
// While a transition runs, clicks reach only the root, so the page is dead until it ends. In
// Forward's WebView2 window a transition started while the window is behind the browser (a
// sign-in finishing) can stall, so steps change without animation when the window lacks
// focus, a newer step skips any transition still running, and a stalled one is cut short.
let active: ViewTransition | undefined
export function stepTransition(direction: 'forward' | 'back', update: () => void, progressShift = false) {
  active?.skipTransition()
  if (!document.startViewTransition || matchMedia('(prefers-reduced-motion: reduce)').matches
    || document.visibilityState !== 'visible' || !document.hasFocus()) return update()
  const root = document.documentElement
  root.dataset.step = direction
  if (progressShift) root.dataset.progress = 'shift'
  const transition = document.startViewTransition(() => flushSync(update))
  active = transition
  // The longest step animation is .5s.
  const timer = window.setTimeout(() => transition.skipTransition(), 1500)
  // A skipped transition (a hidden tab, say) still runs the update; only its animation is lost.
  transition.ready.catch(() => {})
  void transition.finished.finally(() => {
    window.clearTimeout(timer)
    // A newer transition owns the attributes now.
    if (active !== transition) return
    active = undefined
    delete root.dataset.step; delete root.dataset.progress
  })
}
