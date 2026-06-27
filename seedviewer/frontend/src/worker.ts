/// <reference lib="WebWorker" />

let mod: any = null

async function loadModule(): Promise<any> {
  if (mod) return mod
  const dynImport = new Function('return import("/cubiomes.js")') as () => Promise<any>
  const result = await dynImport()
  const factory = result.default || result
  mod = await factory({ locateFile: (path: string) => '/' + path })
  return mod
}

let ready = false

const ctx = self as unknown as DedicatedWorkerGlobalScope

ctx.onmessage = async (e: MessageEvent) => {
  const { type, data } = e.data

  if (type === 'init') {
    await loadModule()
    mod._cubiomes_init(data.seedLo >>> 0, data.seedHi >>> 0, data.largeBiomes ? 1 : 0)
    ready = true
    ctx.postMessage({ type: 'ready' })
    return
  }

  if (type === 'tile' && ready) {
    const { originX, originZ, y4, stride, w, h, id } = data
    const bufSize = w * h * 4
    const buf = mod._malloc(bufSize)
    mod._cubiomes_sample_tile(originX, originZ, y4, stride, w, h, buf)
    const out = new Uint8Array(bufSize)
    out.set(mod.HEAPU8.subarray(buf, buf + bufSize))
    mod._free(buf)
    ctx.postMessage({ type: 'tile_result', id, pixels: out.buffer }, [out.buffer])
    return
  }
}
