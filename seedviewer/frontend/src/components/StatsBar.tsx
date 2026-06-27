import { useEffect, useState } from 'react'
import { fetchStats } from '../api'
import type { Stats } from '../types'

export function StatsBar() {
  const [stats, setStats] = useState<Stats | null>(null)

  useEffect(() => {
    let active = true
    const load = () => {
      fetchStats().then(s => { if (active) setStats(s) }).catch(() => {})
    }
    load()
    const interval = setInterval(load, 5000)
    return () => { active = false; clearInterval(interval) }
  }, [])

  if (!stats) return null

  const fmt = (n: number) => {
    if (n >= 1_000_000_000) return `${(n / 1e9).toFixed(1)}B`
    if (n >= 1_000_000) return `${(n / 1e6).toFixed(1)}M`
    if (n >= 1_000) return `${(n / 1e3).toFixed(1)}K`
    return String(n)
  }

  return (
    <div className="flex items-center gap-3 text-sm">
      <div className="rounded-lg bg-surface0 px-3 py-1.5">
        <span className="text-overlay1">Seeds </span>
        <span className="font-semibold text-text">{stats.total.toLocaleString()}</span>
      </div>
      <div className="rounded-lg bg-surface0 px-3 py-1.5">
        <span className="text-overlay1">Avg </span>
        <span className="font-semibold text-text">{fmt(stats.avg_size)}</span>
      </div>
      <div className="rounded-lg bg-surface0 px-3 py-1.5">
        <span className="text-overlay1">Max </span>
        <span className="font-semibold text-pink">{fmt(stats.max_size)}</span>
      </div>
    </div>
  )
}
