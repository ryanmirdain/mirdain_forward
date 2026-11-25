// Copied from aperture/src/components/SelectPicker.tsx (originally mirdain_client/src/shared/ui/SelectPicker.tsx).
import { Fragment, useRef, useState, type ReactNode } from 'react'
import * as Select from '@radix-ui/react-select'

export type SelectPickerOption = {
  value: string
  label: ReactNode
  disabled?: boolean
  separatorBefore?: boolean
}

type SelectPickerProps = {
  value: string
  options: SelectPickerOption[]
  onValueChange: (value: string) => void
  placeholder?: string
  disabled?: boolean
  id?: string
  ariaLabel?: string
  ariaLabelledBy?: string
  className?: string
}

export function SelectPicker({
  value,
  options,
  onValueChange,
  placeholder = 'Select an option',
  disabled = false,
  id,
  ariaLabel,
  ariaLabelledBy,
  className = '',
}: SelectPickerProps) {
  const triggerRef = useRef<HTMLButtonElement>(null)
  const [portalContainer, setPortalContainer] = useState<HTMLElement | null>(null)

  return (
    <Select.Root
      value={value}
      onValueChange={onValueChange}
      disabled={disabled}
      onOpenChange={(open) => {
        if (open) setPortalContainer(triggerRef.current?.closest('dialog') ?? null)
      }}
    >
      <Select.Trigger
        ref={triggerRef}
        id={id}
        className={`picker-trigger${className ? ` ${className}` : ''}`}
        aria-label={ariaLabel}
        aria-labelledby={ariaLabelledBy}
      >
        <Select.Value placeholder={placeholder} />
        <Select.Icon className="picker-chevron" aria-hidden>
          <span />
        </Select.Icon>
      </Select.Trigger>

      <Select.Portal container={portalContainer}>
        <Select.Content
          className="picker-content"
          position="popper"
          sideOffset={5}
          collisionPadding={12}
        >
          <Select.Viewport className="picker-viewport">
            {options.map((option) => (
              <Fragment key={option.value}>
                {option.separatorBefore ? <Select.Separator className="picker-separator" /> : null}
                <Select.Item
                  className="picker-item"
                  value={option.value}
                  disabled={option.disabled}
                >
                  <Select.ItemText>{option.label}</Select.ItemText>
                  <Select.ItemIndicator className="picker-indicator">
                    <span aria-hidden />
                  </Select.ItemIndicator>
                </Select.Item>
              </Fragment>
            ))}
          </Select.Viewport>
        </Select.Content>
      </Select.Portal>
    </Select.Root>
  )
}
