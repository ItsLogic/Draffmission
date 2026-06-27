import { Link } from 'react-router-dom'
import type { Seed } from '../types'
import { thumbUrl } from '../api'
import { useState } from 'react'

function formatSize(n: number): string {
  if (n >= 1_000_000) return `${(n / 1e6).toFixed(2)}M`
  if (n >= 1_000) return `${(n / 1e3).toFixed(0)}K`
  return String(n)
}

function formatCoord(n: number): string {
  const sign = n < 0 ? '-' : ''
  const abs = Math.abs(n)
  if (abs >= 1_000_000) return `${sign}${(abs / 1e6).toFixed(2)}M`
  if (abs >= 1_000) return `${sign}${(abs / 1e3).toFixed(1)}K`
  return `${sign}${abs}`
}

const MODE_LABELS: Record<string, { label: string; color: string }> = {
  sb: { label: 'SB', color: 'bg-blue/20 text-blue' },
  lb: { label: 'LB', color: 'bg-peach/20 text-peach' },
  usb: { label: 'USB', color: 'bg-mauve/20 text-mauve' },
  ulb: { label: 'ULB', color: 'bg-maroon/20 text-maroon' },
}

export function SeedCard({ seed }: { seed: Seed }) {
  const [loaded, setLoaded] = useState(false)
  const modeInfo = MODE_LABELS[seed.mode] || { label: seed.mode.toUpperCase(), color: 'bg-surface2/20 text-overlay2' }

  return (
    <Link
      to={`/seed/${seed.id}`}
      className="card-hover group block overflow-hidden rounded-xl border border-surface0 bg-mantle"
    >
      <div className="relative aspect-square w-full overflow-hidden bg-crust">
        {!loaded && <div className="shimmer absolute inset-0" />}
        <img
          src={thumbUrl(seed.id)}
          alt={`Seed ${seed.seed}`}
          loading="lazy"
          onLoad={() => setLoaded(true)}
          className={`h-full w-full object-cover transition-opacity duration-300 ${loaded ? 'opacity-100' : 'opacity-0'}`}
        />
        <div className="absolute right-2 top-2">
          <span className="rounded-md bg-crust/80 px-1.5 py-0.5 font-mono text-xs text-pink">
            {formatSize(seed.size)}
          </span>
        </div>
      </div>
      <div className="p-3">
        <div className="truncate font-mono text-sm text-text" title={String(seed.seed)}>
          {seed.seed}
        </div>
        <div className="mt-0.5 flex items-center justify-between gap-2">
          <span className="text-xs text-overlay1">
            X: {formatCoord(seed.x)} Z: {formatCoord(seed.z)}
          </span>
          <span className={`rounded-md px-1.5 py-0.5 text-xs font-semibold ${modeInfo.color}`}>
            {modeInfo.label}
          </span>
        </div>
      </div>
    </Link>
  )
}
