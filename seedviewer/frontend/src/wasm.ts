let workers: Worker[] = []
let nextWorker = 0
let requestId = 0
const pending = new Map<number, (pixels: Uint8Array) => void>()
let currentSeed: number | null = null
let currentLargeBiomes: boolean | null = null

async function createAndInitWorkers(seedLo: number, seedHi: number, largeBiomes: boolean) {
  const numWorkers = Math.min(4, (navigator.hardwareConcurrency || 4) - 1, 6)

  for (let i = 0; i < numWorkers; i++) {
    const w = new Worker(new URL('./worker.ts', import.meta.url), { type: 'module' })
    workers.push(w)
  }

  await Promise.all(workers.map(w => new Promise<void>(resolve => {
    const handler = (e: MessageEvent) => {
      if (e.data.type === 'ready') {
        w.removeEventListener('message', handler)
        resolve()
      }
    }
    w.addEventListener('message', handler)
    w.postMessage({ type: 'init', data: { seedLo, seedHi, largeBiomes } })
  })))

  workers.forEach(w => {
    w.addEventListener('message', (e: MessageEvent) => {
      if (e.data.type === 'tile_result') {
        const cb = pending.get(e.data.id)
        if (cb) {
          cb(new Uint8Array(e.data.pixels))
          pending.delete(e.data.id)
        }
      }
    })
  })
}

export async function initCubiomes(seed: number, largeBiomes: boolean) {
  if (currentSeed === seed && currentLargeBiomes === largeBiomes && workers.length > 0) {
    return
  }

  if (workers.length > 0) {
    workers.forEach(w => w.terminate())
    workers = []
    pending.clear()
    nextWorker = 0
  }

  currentSeed = seed
  currentLargeBiomes = largeBiomes

  const seedLo = (seed & 0xffffffff) >>> 0
  const seedHi = Math.floor(seed / 0x100000000) >>> 0

  await createAndInitWorkers(seedLo, seedHi, largeBiomes)
}

export function sampleTile(
  originX: number,
  originZ: number,
  y4: number,
  strideBlocks: number,
  w: number,
  h: number,
): Promise<Uint8Array> {
  if (workers.length === 0) throw new Error('Workers not initialized')
  return new Promise(resolve => {
    const id = ++requestId
    pending.set(id, resolve)
    const worker = workers[nextWorker % workers.length]
    nextWorker++
    worker.postMessage({
      type: 'tile',
      data: { originX, originZ, y4, stride: strideBlocks, w, h, id },
    })
  })
}
