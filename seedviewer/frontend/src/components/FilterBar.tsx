import { useState, useEffect, useRef } from 'react'

export interface FilterState {
  sort: string
  order: string
  search: string
  min_size: string
  max_size: string
  mode: string
  in_bounds: string
}

interface Props {
  filters: FilterState
  onChange: (filters: FilterState) => void
}

export function FilterBar({ filters, onChange }: Props) {
  const [local, setLocal] = useState<FilterState>(filters)
  const debounceRef = useRef<ReturnType<typeof setTimeout>>()

  useEffect(() => { setLocal(filters) }, [filters])

  const update = (key: keyof FilterState, value: string) => {
    const next = { ...local, [key]: value }
    setLocal(next)
    if (debounceRef.current) clearTimeout(debounceRef.current)
    debounceRef.current = setTimeout(() => onChange(next), 350)
  }

  const selectClass = "rounded-lg border border-surface0 bg-mantle px-3 py-2 text-sm text-text focus:border-mauve focus:outline-none focus:ring-1 focus:ring-mauve/50"
  const inputClass = "rounded-lg border border-surface0 bg-mantle px-3 py-2 text-sm text-text placeholder-overlay0 focus:border-mauve focus:outline-none focus:ring-1 focus:ring-mauve/50"

  return (
    <div className="flex flex-wrap items-center gap-3 border-b border-surface0 bg-mantle px-4 py-3">
      <div className="relative">
        <svg className="absolute left-3 top-1/2 h-4 w-4 -translate-y-1/2 text-overlay1" fill="none" stroke="currentColor" viewBox="0 0 24 24">
          <path strokeLinecap="round" strokeLinejoin="round" strokeWidth={2} d="M21 21l-6-6m2-5a7 7 0 11-14 0 7 7 0 0114 0z" />
        </svg>
        <input
          type="text"
          placeholder="Search seed..."
          value={local.search}
          onChange={e => update('search', e.target.value)}
          className={`${inputClass} w-48 pl-9`}
        />
      </div>

      <select
        value={local.mode}
        onChange={e => update('mode', e.target.value)}
        className={selectClass}
      >
        <option value="">All Modes</option>
        <option value="sb">Small Biomes</option>
        <option value="lb">Large Biomes</option>
        <option value="usb">Unbound SB</option>
        <option value="ulb">Unbound LB</option>
      </select>

      <select
        value={local.in_bounds}
        onChange={e => update('in_bounds', e.target.value)}
        className={selectClass}
      >
        <option value="">All Locations</option>
        <option value="true">In Bounds</option>
        <option value="false">Out of Bounds</option>
      </select>

      <div className="flex items-center gap-1.5">
        <input
          type="number"
          placeholder="Min size"
          value={local.min_size}
          onChange={e => update('min_size', e.target.value)}
          className={`${inputClass} w-28`}
        />
        <span className="text-overlay1">—</span>
        <input
          type="number"
          placeholder="Max size"
          value={local.max_size}
          onChange={e => update('max_size', e.target.value)}
          className={`${inputClass} w-28`}
        />
      </div>

      <div className="ml-auto flex items-center gap-2">
        <select
          value={local.sort}
          onChange={e => update('sort', e.target.value)}
          className={selectClass}
        >
          <option value="size">Size</option>
          <option value="seed">Seed</option>
          <option value="distance">Distance</option>
          <option value="created_at">Date Added</option>
        </select>
        <button
          onClick={() => update('order', local.order === 'desc' ? 'asc' : 'desc')}
          className={`${selectClass} hover:bg-surface0`}
          title={local.order === 'desc' ? 'Descending' : 'Ascending'}
        >
          {local.order === 'desc' ? '↓' : '↑'}
        </button>
      </div>
    </div>
  )
}
