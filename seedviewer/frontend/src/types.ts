export interface Seed {
  id: number
  seed: string
  x: number
  z: number
  size: number
  mode: string
  has_thumb: number
  has_map: number
  created_at: string
  in_bounds?: boolean
  at_origin?: boolean
}

export interface SeedListResponse {
  total: number
  page: number
  limit: number
  pages: number
  seeds: Seed[]
}

export interface Stats {
  total: number
  avg_size: number
  max_size: number
  min_size: number
  modes: Record<string, number>
  thumbnails: number
  maps: number
}
