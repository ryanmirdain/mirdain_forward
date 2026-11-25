import { flushSync } from 'react-dom'

// Copied from aperture/src/lib/step-transition.ts (the Order Parts step animation).
// Slides between steps with a view transition where supported. The update runs synchronously
// inside the transition so the new step is captured. progressShift slides the progress bubbles
// by one too; otherwise they cross-fade.
export function stepTransition(direction: 'forward' | 'back', update: () => void, progressShift = false) {
  if (!document.startViewTransition || matchMedia('(prefers-reduced-motion: reduce)').matches) return update()
  const root = document.documentElement
  root.dataset.step = direction
  if (progressShift) root.dataset.progress = 'shift'
  const transition = document.startViewTransition(() => flushSync(update))
  // A skipped transition (a hidden tab, say) still runs the update; only its animation is lost.
  transition.ready.catch(() => {})
  void transition.finished.finally(() => { delete root.dataset.step; delete root.dataset.progress })
}
