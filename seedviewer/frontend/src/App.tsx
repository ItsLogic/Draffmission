import { Routes, Route, Link } from 'react-router-dom'
import { SeedGrid } from './components/SeedGrid'
import { SeedDetail } from './components/SeedDetail'
import { StatsBar } from './components/StatsBar'

function Header() {
  return (
    <header className="sticky top-0 z-50 border-b border-surface0 bg-base/95 backdrop-blur">
      <div className="mx-auto flex max-w-[1600px] items-center gap-4 px-4 py-3">
        <Link to="/" className="flex items-center gap-2.5">
          <div className="flex h-9 w-9 items-center justify-center rounded-lg bg-gradient-to-br from-mauve to-pink text-sm font-bold text-crust">
            MF
          </div>
          <div>
            <h1 className="text-lg font-bold leading-tight text-text">Mushroom Finder</h1>
            <p className="text-xs text-overlay1">Seed Viewer</p>
          </div>
        </Link>
        <div className="ml-auto">
          <StatsBar />
        </div>
      </div>
    </header>
  )
}

export default function App() {
  return (
    <div className="min-h-screen bg-base">
      <Header />
      <Routes>
        <Route path="/" element={<SeedGrid />} />
        <Route path="/seed/:id" element={<SeedDetail />} />
      </Routes>
    </div>
  )
}
