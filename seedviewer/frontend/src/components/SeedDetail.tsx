import { useEffect, useState } from 'react'
import { useParams, Link, useNavigate } from 'react-router-dom'
import { fetchSeed, deleteSeed } from '../api'
import type { Seed } from '../types'
import { SeedMap } from './SeedMap'

function formatNum(n: number): string {
  return n.toLocaleString()
}

function formatSeed(n: number): string {
  return String(n)
}

function formatCoord(n: number): string {
  const sign = n < 0 ? '-' : ''
  return `${sign}${Math.abs(n).toLocaleString()}`
}

function formatTime(utcStr: string): string {
  const d = new Date(utcStr.replace(' ', 'T') + 'Z')
  return d.toLocaleString('en-GB', {
    timeZone: 'Europe/London',
    year: 'numeric',
    month: 'short',
    day: 'numeric',
    hour: '2-digit',
    minute: '2-digit',
    hour12: false,
  })
}

const BIOME_COLORS: Record<string, string> = {
  mushroom_fields: '#ff00ff',
  mushroom_field_shore: '#a000ff',
  deep_ocean: '#000030',
  ocean: '#000070',
  river: '#0000ff',
  beach: '#fade55',
  plains: '#8db360',
  forest: '#056621',
  desert: '#fa9418',
  mountains: '#606060',
  taiga: '#0b6a5f',
}

export function SeedDetail() {
  const { id } = useParams<{ id: string }>()
  const navigate = useNavigate()
  const [seed, setSeed] = useState<Seed | null>(null)
  const [loading, setLoading] = useState(true)
  const [error, setError] = useState<string | null>(null)

  useEffect(() => {
    if (!id) return
    setLoading(true)
    fetchSeed(parseInt(id))
      .then(s => { setSeed(s); setError(null) })
      .catch(e => setError(e.message))
      .finally(() => setLoading(false))
  }, [id])

  const handleDelete = async () => {
    if (!seed) return
    if (!confirm(`Delete seed ${seed.seed}?`)) return
    await deleteSeed(seed.id)
    navigate('/')
  }

  if (loading) {
    return (
      <div className="mx-auto max-w-[1200px] px-4 py-8">
        <div className="shimmer h-[70vh] w-full rounded-xl" />
      </div>
    )
  }

  if (error || !seed) {
    return (
      <div className="mx-auto max-w-[1200px] px-4 py-8">
        <div className="rounded-lg border border-red/30 bg-red/10 p-4 text-red">
          {error || 'Seed not found'}
        </div>
        <Link to="/" className="mt-4 inline-block text-mauve hover:underline">← Back to grid</Link>
      </div>
    )
  }

  const largeBiomes = seed.mode.includes('lb')

  return (
    <div className="fade-in mx-auto max-w-[1400px] px-4 py-6">
      <div className="mb-4 flex items-center gap-3">
        <Link
          to="/"
          className="flex items-center gap-1.5 rounded-lg border border-surface0 bg-surface0 px-3 py-2 text-sm text-subtext1 hover:bg-surface1"
        >
          <svg className="h-4 w-4" fill="none" stroke="currentColor" viewBox="0 0 24 24">
            <path strokeLinecap="round" strokeLinejoin="round" strokeWidth={2} d="M10 19l-7-7m0 0l7-7m-7 7h18" />
          </svg>
          Back
        </Link>
        <h2 className="font-mono text-xl font-bold text-text">
          Seed {seed.seed}
        </h2>
        <span className="rounded-md bg-surface0 px-2 py-1 text-xs font-semibold text-subtext0">
          {seed.mode.toUpperCase()}
        </span>
      </div>

      <div className="grid gap-6 lg:grid-cols-[1fr_320px]">
        <div>
          <SeedMap
            seed={seed.seed}
            cx={seed.x}
            cz={seed.z}
            largeBiomes={largeBiomes}
          />
        </div>

        <div className="space-y-4">
          <div className="rounded-xl border border-surface0 bg-mantle p-5">
            <h3 className="mb-3 text-sm font-semibold uppercase tracking-wider text-overlay1">
              Island Info
            </h3>
            <dl className="space-y-3">
              <div>
                <dt className="text-xs text-overlay1">World Seed</dt>
                <dd className="font-mono text-sm text-text">{formatSeed(seed.seed)}</dd>
              </div>
              <div>
                <dt className="text-xs text-overlay1">Island Size</dt>
                <dd className="text-lg font-bold text-pink">
                  {formatNum(seed.size)} <span className="text-sm font-normal text-overlay1">blocks</span>
                </dd>
              </div>
              <div className="grid grid-cols-2 gap-3">
                <div>
                  <dt className="text-xs text-overlay1">Center X</dt>
                  <dd className="font-mono text-sm text-text">{formatCoord(seed.x)}</dd>
                </div>
                <div>
                  <dt className="text-xs text-overlay1">Center Z</dt>
                  <dd className="font-mono text-sm text-text">{formatCoord(seed.z)}</dd>
                </div>
              </div>
              <div>
                <dt className="text-xs text-overlay1">Distance from Origin</dt>
                <dd className="font-mono text-sm text-text">
                  {(Math.sqrt(seed.x * seed.x + seed.z * seed.z) / 1e6).toFixed(2)}M blocks
                </dd>
              </div>
              <div>
                <dt className="text-xs text-overlay1">Search Mode</dt>
                <dd className="text-sm text-text">{seed.mode.toUpperCase()}</dd>
              </div>
              <div>
                <dt className="text-xs text-overlay1">Added</dt>
                <dd className="text-sm text-text">{formatTime(seed.created_at)}</dd>
              </div>
            </dl>
          </div>

          <div className="rounded-xl border border-surface0 bg-mantle p-5">
            <h3 className="mb-3 text-sm font-semibold uppercase tracking-wider text-overlay1">
              Biome Legend
            </h3>
            <div className="space-y-1.5">
              {Object.entries(BIOME_COLORS).map(([name, color]) => (
                <div key={name} className="flex items-center gap-2">
                  <div
                    className="h-3 w-3 rounded"
                    style={{ backgroundColor: color }}
                  />
                  <span className="text-xs text-subtext0">{name.replace(/_/g, ' ')}</span>
                </div>
              ))}
            </div>
          </div>

          <div className="rounded-xl border border-surface0 bg-mantle p-5">
            <h3 className="mb-3 text-sm font-semibold uppercase tracking-wider text-overlay1">
              Commands
            </h3>
            <div className="space-y-2">
              <div>
                <div className="mb-1 text-xs text-overlay1">Sizecheck</div>
                <code className="block rounded-lg bg-crust p-2 font-mono text-xs text-green">
                  ./sizecheck --{seed.mode === 'lb' || seed.mode === 'ulb' ? 'lb' : 'sb'} {seed.seed} {seed.x} {seed.z}
                </code>
              </div>
              <div>
                <div className="mb-1 text-xs text-overlay1">Teleport</div>
                <code className="block rounded-lg bg-crust p-2 font-mono text-xs text-blue">
                  /tp {seed.x} 100 {seed.z}
                </code>
              </div>
            </div>
          </div>

          <button
            onClick={handleDelete}
            className="w-full rounded-lg border border-red/30 bg-red/10 px-4 py-2 text-sm font-semibold text-red hover:bg-red/20"
          >
            Delete Seed
          </button>
        </div>
      </div>
    </div>
  )
}
