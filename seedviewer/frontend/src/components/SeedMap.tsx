import { useEffect, useRef, useState } from 'react'
import Map from 'ol/Map'
import View from 'ol/View'
import TileLayer from 'ol/layer/Tile'
import TileImageSource from 'ol/source/TileImage'
import { TileGrid } from 'ol/tilegrid'
import { initCubiomes, sampleTile } from '../wasm'

const EXTENT_HALF = 16384
const RESOLUTIONS = [64, 32, 16, 8, 4, 2, 1]
const TILE_SIZE = 256
const Y4 = 15

interface Props {
  seed: string | number
  cx: number
  cz: number
  largeBiomes: boolean
}

export function SeedMap({ seed, cx, cz, largeBiomes }: Props) {
  const mapRef = useRef<HTMLDivElement>(null)
  const [status, setStatus] = useState('Loading WASM...')

  useEffect(() => {
    if (!mapRef.current) return

    let map: Map | null = null
    let cancelled = false

    const extent = [0, 0, 2 * EXTENT_HALF, 2 * EXTENT_HALF]

    const tileGrid = new TileGrid({
      extent,
      resolutions: RESOLUTIONS,
      tileSize: [TILE_SIZE, TILE_SIZE],
    })

    async function init() {
      await initCubiomes(seed, largeBiomes)
      if (cancelled) return
      setStatus('')

      const source = new TileImageSource({
        tileGrid,
        tileUrlFunction: (coord: number[]) => `wasm:${coord.join('/')}`,
        tileLoadFunction: (tile: any) => {
          const [z, tx, ty] = tile.getTileCoord()
          const res = RESOLUTIONS[z]
          const tw = TILE_SIZE * res

          const mcX = Math.floor(cx - EXTENT_HALF + tx * tw)
          const mcZ = Math.floor(cz - EXTENT_HALF + ty * tw)
          const stride = Math.max(1, Math.round(res))

          sampleTile(mcX, mcZ, Y4, stride, TILE_SIZE, TILE_SIZE).then(pixels => {
            if (cancelled) return
            try {
              const imageData = new ImageData(new Uint8ClampedArray(pixels), TILE_SIZE, TILE_SIZE)
              const canvas = document.createElement('canvas')
              canvas.width = TILE_SIZE
              canvas.height = TILE_SIZE
              canvas.getContext('2d')!.putImageData(imageData, 0, 0)
              const img = tile.getImage()
              img.onload = () => tile.setStatus(2)
              img.onerror = () => tile.setStatus(3)
              img.src = canvas.toDataURL()
            } catch (e) {
              console.error('Tile render failed:', e)
              tile.setStatus(3)
            }
          }).catch(e => {
            console.error('Tile gen failed:', e)
            tile.setStatus(3)
          })
        },
      })

      const layer = new TileLayer({ source })

      map = new Map({
        target: mapRef.current!,
        layers: [layer],
        view: new View({
          center: [EXTENT_HALF, EXTENT_HALF],
          extent,
          resolutions: RESOLUTIONS,
          zoom: 2,
          showFullExtent: true,
        }),
        controls: [],
      })
    }

    init().catch(e => setStatus(`Error: ${e.message}`))

    return () => {
      cancelled = true
      map?.setTarget(undefined)
    }
  }, [seed, cx, cz, largeBiomes])

  return (
    <div className="relative overflow-hidden rounded-xl border border-surface0 bg-mantle">
      <div ref={mapRef} className="h-[70vh] min-h-[500px] w-full" />
      {status && (
        <div className="absolute inset-0 flex items-center justify-center">
          <div className="rounded-lg bg-surface0/90 px-4 py-2 text-sm text-text">
            {status}
          </div>
        </div>
      )}
    </div>
  )
}
