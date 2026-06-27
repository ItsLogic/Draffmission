import { useRef, useState, useEffect, useCallback } from 'react'

interface Props {
  src: string
  alt: string
}

export function BiomeMapViewer({ src, alt }: Props) {
  const containerRef = useRef<HTMLDivElement>(null)
  const [scale, setScale] = useState(1)
  const [pos, setPos] = useState({ x: 0, y: 0 })
  const [loaded, setLoaded] = useState(false)
  const dragging = useRef(false)
  const lastPos = useRef({ x: 0, y: 0 })

  const reset = useCallback(() => {
    setScale(1)
    setPos({ x: 0, y: 0 })
  }, [])

  const clampPos = (x: number, y: number, s: number) => {
    if (!containerRef.current) return { x, y }
    const rect = containerRef.current.getBoundingClientRect()
    const maxX = Math.max(0, (rect.width * (s - 1)) / 2 + rect.width * s - rect.width)
    const maxY = Math.max(0, (rect.height * (s - 1)) / 2 + rect.height * s - rect.height)
    return {
      x: Math.max(-maxX, Math.min(maxX, x)),
      y: Math.max(-maxY, Math.min(maxY, y)),
    }
  }

  const onWheel = useCallback((e: React.WheelEvent) => {
    e.preventDefault()
    const delta = -e.deltaY * 0.001
    setScale(prev => {
      const next = Math.max(1, Math.min(20, prev + delta * prev))
      return next
    })
  }, [])

  const onMouseDown = (e: React.MouseEvent) => {
    if (scale <= 1) return
    dragging.current = true
    lastPos.current = { x: e.clientX, y: e.clientY }
  }

  const onMouseMove = (e: React.MouseEvent) => {
    if (!dragging.current) return
    const dx = e.clientX - lastPos.current.x
    const dy = e.clientY - lastPos.current.y
    lastPos.current = { x: e.clientX, y: e.clientY }
    setPos(prev => clampPos(prev.x + dx, prev.y + dy, scale))
  }

  const onMouseUp = () => { dragging.current = false }

  useEffect(() => {
    const handler = () => {}
    window.addEventListener('mouseup', handler)
    return () => window.removeEventListener('mouseup', handler)
  }, [])

  useEffect(() => { setLoaded(false); reset() }, [src, reset])

  return (
    <div className="relative overflow-hidden rounded-xl border border-dark-700 bg-dark-900">
      <div
        ref={containerRef}
        className="map-container relative aspect-square w-full"
        onWheel={onWheel}
        onMouseDown={onMouseDown}
        onMouseMove={onMouseMove}
        onMouseUp={onMouseUp}
        onMouseLeave={onMouseUp}
      >
        {!loaded && (
          <div className="shimmer absolute inset-0" />
        )}
        <img
          src={src}
          alt={alt}
          draggable={false}
          onLoad={() => setLoaded(true)}
          className="absolute left-0 top-0 h-full w-full"
          style={{
            transform: `translate(calc(-50% + ${pos.x}px), calc(-50% + ${pos.y}px)) scale(${scale})`,
            transformOrigin: 'center center',
            opacity: loaded ? 1 : 0,
            transition: dragging.current ? 'none' : 'transform 0.05s ease-out, opacity 0.3s',
            imageRendering: scale > 2 ? 'pixelated' : 'auto',
          }}
        />
      </div>

      <div className="absolute bottom-3 right-3 flex flex-col gap-1">
        <button
          onClick={() => setScale(s => Math.min(20, s * 1.3))}
          className="flex h-9 w-9 items-center justify-center rounded-lg bg-dark-800/90 text-lg font-bold text-dark-100 hover:bg-dark-700"
        >
          +
        </button>
        <button
          onClick={() => setScale(s => Math.max(1, s / 1.3))}
          className="flex h-9 w-9 items-center justify-center rounded-lg bg-dark-800/90 text-lg font-bold text-dark-100 hover:bg-dark-700"
        >
          −
        </button>
        <button
          onClick={reset}
          className="flex h-9 w-9 items-center justify-center rounded-lg bg-dark-800/90 text-xs font-bold text-dark-100 hover:bg-dark-700"
          title="Reset view"
        >
          ⟲
        </button>
      </div>

      <div className="absolute bottom-3 left-3 rounded-lg bg-dark-950/80 px-2 py-1 text-xs text-dark-300">
        {scale.toFixed(1)}x
      </div>
    </div>
  )
}
