import type { SeedListResponse, Stats, Seed } from './types'

const API = '/api'

export async function fetchSeeds(params: {
  page: number
  limit?: number
  sort?: string
  order?: string
  search?: string
  min_size?: number
  max_size?: number
  mode?: string
  biome?: string
  in_bounds?: boolean | null
  at_origin?: boolean | null
}): Promise<SeedListResponse> {
  const sp = new URLSearchParams()
  sp.set('page', String(params.page))
  if (params.limit) sp.set('limit', String(params.limit))
  if (params.sort) sp.set('sort', params.sort)
  if (params.order) sp.set('order', params.order)
  if (params.search) sp.set('search', params.search)
  if (params.min_size != null) sp.set('min_size', String(params.min_size))
  if (params.max_size != null) sp.set('max_size', String(params.max_size))
  if (params.mode) sp.set('mode', params.mode)
  if (params.biome) sp.set('biome', params.biome)
  if (params.in_bounds != null) sp.set('in_bounds', String(params.in_bounds))
  if (params.at_origin != null) sp.set('at_origin', String(params.at_origin))

  const res = await fetch(`${API}/seeds?${sp}`)
  if (!res.ok) throw new Error(`Failed to fetch seeds: ${res.statusText}`)
  return res.json()
}

export async function fetchSeed(id: number): Promise<Seed> {
  const res = await fetch(`${API}/seeds/${id}`)
  if (!res.ok) throw new Error('Seed not found')
  return res.json()
}

export async function fetchStats(): Promise<Stats> {
  const res = await fetch(`${API}/stats`)
  if (!res.ok) throw new Error('Failed to fetch stats')
  return res.json()
}

export async function deleteSeed(id: number) {
  const res = await fetch(`${API}/seeds/${id}`, { method: 'DELETE' })
  if (!res.ok) throw new Error('Delete failed')
}

export function thumbUrl(id: number) {
  return `${API}/images/thumb/${id}`
}
