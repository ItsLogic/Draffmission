import { useEffect, useState, useRef, useCallback } from 'react'
import { fetchSeeds } from '../api'
import type { Seed } from '../types'
import { SeedCard } from './SeedCard'
import { FilterBar, type FilterState } from './FilterBar'

const PAGE_SIZE = 50

export function SeedGrid() {
  const [seeds, setSeeds] = useState<Seed[]>([])
  const [total, setTotal] = useState(0)
  const [loading, setLoading] = useState(false)
  const [error, setError] = useState<string | null>(null)
  const [filters, setFilters] = useState<FilterState>({
    sort: 'size',
    order: 'desc',
    search: '',
    min_size: '',
    max_size: '',
    mode: '',
    in_bounds: '',
  })

  const sentinelRef = useRef<HTMLDivElement>(null)
  const pageRef = useRef(1)
  const hasMoreRef = useRef(true)
  const loadingRef = useRef(false)
  const reqIdRef = useRef(0)

  const fetchPage = useCallback(async (pageNum: number, replace: boolean) => {
    const reqId = ++reqIdRef.current
    loadingRef.current = true
    setLoading(true)
    setError(null)
    try {
      const res = await fetchSeeds({
        page: pageNum,
        limit: PAGE_SIZE,
        sort: filters.sort,
        order: filters.order,
        search: filters.search,
        min_size: filters.min_size ? parseInt(filters.min_size) : undefined,
        max_size: filters.max_size ? parseInt(filters.max_size) : undefined,
        mode: filters.mode || undefined,
        in_bounds: filters.in_bounds === '' ? null : filters.in_bounds === 'true',
      })
      if (reqId !== reqIdRef.current) return
      if (replace) {
        setSeeds(res.seeds)
      } else {
        setSeeds(prev => [...prev, ...res.seeds])
      }
      setTotal(res.total)
      hasMoreRef.current = pageNum < res.pages
      pageRef.current = pageNum
    } catch (e) {
      if (reqId !== reqIdRef.current) return
      setError(e instanceof Error ? e.message : 'Failed to load')
    } finally {
      if (reqId === reqIdRef.current) {
        loadingRef.current = false
        setLoading(false)
      }
    }
  }, [filters])

  useEffect(() => {
    pageRef.current = 1
    hasMoreRef.current = true
    fetchPage(1, true)
  }, [fetchPage])

  const onIntersect = useCallback(() => {
    if (!loadingRef.current && hasMoreRef.current) {
      fetchPage(pageRef.current + 1, false)
    }
  }, [fetchPage])

  useEffect(() => {
    const el = sentinelRef.current
    if (!el) return
    const obs = new IntersectionObserver(
      entries => { if (entries[0].isIntersecting) onIntersect() },
      { rootMargin: '300px' }
    )
    obs.observe(el)
    return () => obs.disconnect()
  }, [onIntersect])

  const handleFilterChange = (newFilters: FilterState) => {
    setFilters(newFilters)
  }

  return (
    <div>
      <FilterBar filters={filters} onChange={handleFilterChange} />

      <div className="mx-auto max-w-[1600px] px-4 py-4">
        <div className="mb-3 text-sm text-overlay1">
          {total > 0 && (
            <span>
              Showing <span className="text-text">{seeds.length}</span> of{' '}
              <span className="text-text">{total.toLocaleString()}</span> seeds
            </span>
          )}
        </div>

        {error && (
          <div className="rounded-lg border border-red/30 bg-red/10 p-4 text-sm text-red">
            {error}
          </div>
        )}

        {seeds.length === 0 && !loading && !error && (
          <div className="flex flex-col items-center justify-center py-20 text-overlay1">
            <svg className="mb-3 h-12 w-12" fill="none" stroke="currentColor" viewBox="0 0 24 24">
              <path strokeLinecap="round" strokeLinejoin="round" strokeWidth={1.5} d="M9 17v-2m3 2v-4m3 4v-6m2 10H7a2 2 0 01-2-2V5a2 2 0 012-2h5.586a1 1 0 01.707.293l5.414 5.414a1 1 0 01.293.707V19a2 2 0 01-2 2z" />
            </svg>
            <p className="text-lg font-medium">No seeds found</p>
            <p className="text-sm">Import seeds or adjust your filters</p>
          </div>
        )}

        <div className="grid grid-cols-2 gap-4 md:grid-cols-3 lg:grid-cols-4 xl:grid-cols-5">
          {seeds.map(seed => (
            <SeedCard key={seed.id} seed={seed} />
          ))}
        </div>

        {loading && (
          <div className="mt-4 grid grid-cols-2 gap-4 md:grid-cols-3 lg:grid-cols-4 xl:grid-cols-5">
            {Array.from({ length: 10 }).map((_, i) => (
              <div key={i} className="overflow-hidden rounded-xl border border-surface0 bg-surface0/50">
                <div className="shimmer aspect-square w-full" />
                <div className="p-3">
                  <div className="shimmer h-4 w-3/4 rounded" />
                  <div className="shimmer mt-2 h-3 w-1/2 rounded" />
                </div>
              </div>
            ))}
          </div>
        )}

        <div ref={sentinelRef} className="h-4" />
      </div>
    </div>
  )
}
