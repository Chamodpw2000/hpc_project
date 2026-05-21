"use client";
import { useState, useEffect } from "react";
import {
  ComposedChart, Scatter, Line, XAxis, YAxis,
  CartesianGrid, Tooltip, ResponsiveContainer,
} from "recharts";

interface SubjectOption { name: string; class_name: string; }
interface ScatterPoint  { x: number; y: number; }
interface CorrelationResult {
  correlation_coefficient: number;
  elapsed_ms: number;
  db_fetch_ms: number;
  threads_used: number;
  n_pairs: number;
  total_students?: number;
  excluded?: number;
  data_points: ScatterPoint[];
}
interface CorrelationCompare {
  serial: CorrelationResult;
  parallel: CorrelationResult;
  pthread?: CorrelationResult;
  mpi?: CorrelationResult;
  comparison: {
    serial_time_ms: number;
    parallel_time_ms: number;
    pthread_time_ms?: number;
    mpi_time_ms?: number;
    db_fetch_ms: number;
    speedup: number;
    speedup_pthread?: number;
    speedup_mpi?: number;
    serial_threads: number;
    parallel_threads: number;
    pthread_threads?: number;
    mpi_threads?: number;
    n_pairs: number;
    total_students?: number;
    excluded?: number;
    improvement_pct: number;
  };
}

const API = "http://localhost:8090";

// ── Helpers ───────────────────────────────────────────────────────────────────

function correlationLabel(r: number): { label: string; color: string } {
  const abs = Math.abs(r);
  if (abs >= 0.7) return { label: "Strong",    color: "text-emerald-400" };
  if (abs >= 0.5) return { label: "Moderate",  color: "text-blue-400"   };
  if (abs >= 0.3) return { label: "Weak",      color: "text-yellow-400" };
  return             { label: "Very Weak", color: "text-red-400"    };
}

function directionLabel(r: number): string {
  if (r > 0) return "Positive";
  if (r < 0) return "Negative";
  return "No";
}

function trendLinePoints(pts: ScatterPoint[]): { x: number; y: number }[] {
  const n = pts.length;
  if (n < 2) return [];
  const sumX  = pts.reduce((a, p) => a + p.x, 0);
  const sumY  = pts.reduce((a, p) => a + p.y, 0);
  const sumXY = pts.reduce((a, p) => a + p.x * p.y, 0);
  const sumX2 = pts.reduce((a, p) => a + p.x * p.x, 0);
  const slope     = (n * sumXY - sumX * sumY) / (n * sumX2 - sumX * sumX);
  const intercept = (sumY - slope * sumX) / n;
  const minX = Math.min(...pts.map(p => p.x));
  const maxX = Math.max(...pts.map(p => p.x));
  return [
    { x: minX, y: slope * minX + intercept },
    { x: maxX, y: slope * maxX + intercept },
  ];
}

// ── Sub-components ────────────────────────────────────────────────────────────

function CorrelationChart({ points, color, subject1Name, subject2Name }: Readonly<{
  points: ScatterPoint[];
  color: string;
  subject1Name: string;
  subject2Name: string;
}>) {
  const trend = trendLinePoints(points);
  return (
    <ResponsiveContainer width="100%" height={260}>
      <ComposedChart margin={{ top: 10, right: 10, bottom: 35, left: 10 }}>
        <CartesianGrid strokeDasharray="3 3" stroke="#3f3f46" />
        <XAxis
          dataKey="x"
          type="number"
          domain={[0, 100]}
          name={subject1Name}
          label={{ value: subject1Name, position: "insideBottom", offset: -20, fill: "#a1a1aa", fontSize: 11 }}
          tick={{ fill: "#71717a", fontSize: 11 }}
          tickLine={false}
        />
        <YAxis
          dataKey="y"
          type="number"
          domain={[0, 100]}
          name={subject2Name}
          label={{ value: subject2Name, angle: -90, position: "insideLeft", fill: "#a1a1aa", fontSize: 11 }}
          tick={{ fill: "#71717a", fontSize: 11 }}
          tickLine={false}
          axisLine={false}
        />
        <Tooltip
          cursor={{ strokeDasharray: "3 3", stroke: "#52525b" }}
          contentStyle={{ background: "#18181b", border: "1px solid #3f3f46", borderRadius: 8, fontSize: 12 }}
        />
        <Scatter data={points} fill={color} opacity={0.85} />
        <Line
          data={trend}
          dataKey="y"
          stroke={color}
          strokeWidth={1.5}
          strokeDasharray="6 3"
          dot={false}
          activeDot={false}
          legendType="none"
        />
      </ComposedChart>
    </ResponsiveContainer>
  );
}

function CorrelationPanel({ result, borderColor, subject1Name, subject2Name, chartColor }: Readonly<{
  result: CorrelationResult;
  borderColor: string;
  subject1Name: string;
  subject2Name: string;
  chartColor: string;
}>) {
  const r = result.correlation_coefficient;
  const { label, color: labelColor } = correlationLabel(r);
  const title = (result as any).mode
    ? ((result as any).mode === 'mpi'
      ? 'MPI Distributed'
      : ((result as any).mode === 'pthread'
        ? 'POSIX Threads (pthreads)'
        : ((result as any).mode === 'parallel' ? 'Parallel (OpenMP)' : 'Serial')))
    : modeLabel;


  return (
    <div className={`border ${borderColor} rounded-xl p-5 bg-zinc-900/50`}>
      <div className="flex items-center justify-between mb-4">
        <h3 className="text-lg font-bold text-white uppercase tracking-wider">{title}</h3>
        <span className="text-xs bg-zinc-800 text-zinc-300 px-2 py-1 rounded font-mono">
          {result.threads_used} {result.threads_used === 1 ? "thread" : ((result as any).mode === 'mpi' ? "processes" : "threads")}
        </span>
      </div>

      <div className="mb-4 p-4 bg-zinc-800/50 rounded-xl text-center">
        <div className="text-xs text-zinc-400 uppercase tracking-wider font-semibold mb-1">
          Pearson Correlation Coefficient
        </div>
        <div className="text-4xl font-black text-white font-mono">{r.toFixed(4)}</div>
        <div className={`text-sm font-semibold mt-1 ${labelColor}`}>{label} Correlation</div>
        <div className="text-xs text-zinc-500 mt-1">
          {directionLabel(r)} linear relationship
        </div>
        <div className="mt-2 pt-2 border-t border-zinc-700 text-xs text-zinc-400">
          Based on <span className="text-white font-semibold font-mono">{result.n_pairs}</span> students with scores in both subjects
        </div>
      </div>

      <div className="mb-4 grid grid-cols-4 gap-2 text-center">
        <div className="bg-zinc-800 rounded-lg p-3 relative group">
          <div className="text-xs text-zinc-400">Total</div>
          <div className="text-sm font-bold text-white font-mono">{result.total_students || result.n_pairs}</div>
          <div className="absolute opacity-0 group-hover:opacity-100 transition-opacity bg-black/90 text-xs p-2 rounded -top-8 left-1/2 -translate-x-1/2 whitespace-nowrap z-10 border border-zinc-700 pointer-events-none">
            Total students in class
          </div>
        </div>
        <div className="bg-zinc-800 rounded-lg p-3 relative group">
          <div className="text-xs text-emerald-400/70">Analyzed</div>
          <div className="text-sm font-bold text-emerald-400 font-mono">{result.n_pairs}</div>
          <div className="absolute opacity-0 group-hover:opacity-100 transition-opacity bg-black/90 text-xs p-2 rounded -top-8 left-1/2 -translate-x-1/2 whitespace-nowrap z-10 border border-zinc-700 pointer-events-none">
            Students with both scores
          </div>
        </div>
        <div className={`bg-zinc-800 rounded-lg p-3 relative group ${result.excluded ? 'ring-1 ring-amber-500/50 bg-amber-950/20' : ''}`}>
          <div className={`text-xs ${result.excluded ? 'text-amber-400/80 font-semibold' : 'text-zinc-400'}`}>Excluded</div>
          <div className={`text-sm font-bold font-mono ${result.excluded ? 'text-amber-400' : 'text-white'}`}>{result.excluded || 0}</div>
          <div className="absolute opacity-0 group-hover:opacity-100 transition-opacity bg-black/90 text-xs p-2 rounded -top-12 left-1/2 -translate-x-1/2 whitespace-nowrap z-10 border border-zinc-700 pointer-events-none">
            {result.excluded ? `Skipped ${result.excluded} students missing grades` : 'All students had both grades'}
          </div>
        </div>
        <div className="bg-zinc-800 rounded-lg p-3 relative group">
          <div className="text-xs text-zinc-400">Calc Time</div>
          <div className="text-sm font-bold text-white font-mono">{result.elapsed_ms.toFixed(1)} ms</div>
        </div>
      </div>

      {result.excluded ? (
        <div className="mb-4 text-xs text-amber-400/80 bg-amber-500/10 border border-amber-500/20 p-2 rounded flex items-center justify-center">
          <svg className="w-4 h-4 mr-1.5" fill="none" viewBox="0 0 24 24" stroke="currentColor">
            <path strokeLinecap="round" strokeLinejoin="round" strokeWidth={2} d="M12 9v2m0 4h.01m-6.938 4h13.856c1.54 0 2.502-1.667 1.732-3L13.732 4c-.77-1.333-2.694-1.333-3.464 0L3.34 16c-.77 1.333.192 3 1.732 3z" />
          </svg>
          {result.excluded} students excluded due to missing scores
        </div>
      ) : null}

      <div>
        <div className="text-xs text-zinc-400 mb-2 font-semibold uppercase tracking-wider">
          Score Distribution
        </div>
        <CorrelationChart
          points={result.data_points}
          color={chartColor}
          subject1Name={subject1Name}
          subject2Name={subject2Name}
        />
      </div>
    </div>
  );
}

// ── Main component ────────────────────────────────────────────────────────────

const SELECT_CLS =
  "bg-zinc-800 border border-zinc-700 rounded-lg px-3 py-2 text-white text-sm " +
  "focus:border-indigo-500 focus:outline-none disabled:opacity-50 w-full";

export default function CorrelationTab() {
  // Selector state
  const [classes, setClasses]             = useState<string[]>([]);
  const [loadingClasses, setLoadingClasses] = useState(true);

  const [selectedClass, setSelectedClass] = useState("");
  const [subjects, setSubjects]           = useState<SubjectOption[] | null>(null);
  const [subject1, setSubject1]           = useState("");
  const [subject2, setSubject2]           = useState("");

  // Action state
  const [loading, setLoading]             = useState<"serial" | "parallel" | "pthread" | "mpi" | "compare" | null>(null);
  const [error, setError]                 = useState<string | null>(null);

  // Result state
  const [serialResult, setSerialResult]       = useState<CorrelationResult | null>(null);
  const [parallelResult, setParallelResult]   = useState<CorrelationResult | null>(null);
  const [pthreadResult, setPthreadResult]     = useState<CorrelationResult | null>(null);
  const [mpiResult, setMpiResult]             = useState<CorrelationResult | null>(null);
  const [compareResult, setCompareResult]     = useState<CorrelationCompare | null>(null);

  // ── Fetch classes on mount ──
  useEffect(() => {
    async function loadClasses() {
      setLoadingClasses(true);
      try {
        const res  = await fetch("/api/classes", { cache: "no-store" });
        const json = await res.json();
        const raw: unknown[] = json.data ?? [];
        const names: string[] = Array.from(new Set(
          raw
            .map(c => typeof c === "string" ? c : (c as { name: string })?.name)
            .filter((n): n is string => typeof n === "string" && n.trim() !== "")
        ));
        setClasses(names);
        if (names[0]) setSelectedClass(names[0]);
      } catch { /* leave dropdowns empty */ }
      finally { setLoadingClasses(false); }
    }
    loadClasses();
  }, []);

  // null = loading; [] = loaded/empty; [...] = loaded with data
  const loadingSubjects = subjects === null;

  // ── Fetch subjects when selectedClass changes ──
  useEffect(() => {
    if (!selectedClass) { setSubjects([]); setSubject1(""); setSubject2(""); return; }
    setSubjects(null);
    setSubject1(""); setSubject2("");
    fetch(`/api/subjects?class=${encodeURIComponent(selectedClass)}`, { cache: "no-store" })
      .then(r => r.json())
      .then(j => setSubjects(j.data ?? []))
      .catch(() => setSubjects([]));
  }, [selectedClass]);

  useEffect(() => {
    const list = subjects ?? [];
    if (list.length > 0) {
      setSubject1(list[0].name);
      setSubject2(list.length > 1 ? list[1].name : list[0].name);
    }
  }, [subjects]);

  // ── Button handlers ──
  function buildParams() {
    return new URLSearchParams({
      subject1, class1: selectedClass,
      subject2, class2: selectedClass,
    }).toString();
  }

  async function runSerial() {
    setLoading("serial"); setError(null); setCompareResult(null);
    try {
      const res  = await fetch(`${API}/api/calculate/correlation/serial?${buildParams()}`);
      const json = await res.json();
      if (!res.ok) throw new Error(json.message ?? "Request failed");
      setSerialResult(json.data);
      setParallelResult(null);
    } catch (e) {
      setError(`Serial correlation failed: ${e}`);
    }
    setLoading(null);
  }

  async function runParallel() {
    setLoading("parallel"); setError(null); setCompareResult(null);
    try {
      const res  = await fetch(`${API}/api/calculate/correlation/parallel?${buildParams()}`);
      const json = await res.json();
      if (!res.ok) throw new Error(json.message ?? "Request failed");
      setParallelResult(json.data);
      setSerialResult(null);
      setPthreadResult(null);
      setMpiResult(null);
    } catch (e) {
      setError(`Parallel correlation failed: ${e}`);
    }
    setLoading(null);
  }

  async function runPthread() {
    setLoading("pthread"); setError(null); setCompareResult(null);
    try {
      const res  = await fetch(`${API}/api/calculate/correlation/pthread?${buildParams()}`);
      const json = await res.json();
      if (!res.ok) throw new Error(json.message ?? "Request failed");
      setPthreadResult(json.data);
      setSerialResult(null);
      setParallelResult(null);
      setMpiResult(null);
    } catch (e) {
      setError(`Pthread correlation failed: ${e}`);
    }
    setLoading(null);
  }

  async function runMpi() {
    setLoading("mpi"); setError(null); setCompareResult(null);
    try {
      const res  = await fetch(`${API}/api/calculate/correlation/mpi?${buildParams()}`);
      const json = await res.json();
      if (!res.ok) throw new Error(json.message ?? "Request failed");
      setMpiResult(json.data);
      setSerialResult(null);
      setParallelResult(null);
      setPthreadResult(null);
    } catch (e) {
      setError(`MPI correlation failed: ${e}`);
    }
    setLoading(null);
  }

  async function runCompare() {
    setLoading("compare"); setError(null); setSerialResult(null); setParallelResult(null); setPthreadResult(null); setMpiResult(null);
    try {
      const res  = await fetch(`${API}/api/calculate/correlation/compare?${buildParams()}`);
      const json = await res.json();
      if (!res.ok) throw new Error(json.message ?? "Request failed");
      setCompareResult(json.data);
    } catch (e) {
      setError(`Correlation comparison failed: ${e}`);
    }
    setLoading(null);
  }

  const canRun = !loading && !!subject1 && !!subject2;
  const speedup = compareResult ? compareResult.comparison.speedup : 0;

  return (
    <>
      {/* ── Subject Selectors ── */}
      <div className="bg-zinc-900 border border-zinc-800 rounded-xl p-6 mb-6">
        <h2 className="text-lg font-semibold mb-4">1. Select Subjects</h2>

        {/* Class selector (shared) */}
        <div className="mb-4">
          <div className="text-xs text-zinc-400 mb-1 font-semibold">Class</div>
          <select
            value={selectedClass}
            onChange={e => setSelectedClass(e.target.value)}
            disabled={loadingClasses}
            aria-label="Class"
            className={SELECT_CLS + " max-w-xs"}
          >
            <option value="">— Class —</option>
            {classes.map(c => <option key={c} value={c}>{c}</option>)}
          </select>
        </div>

        {/* Subject pair */}
        <div className="flex flex-wrap items-center gap-4">
          <div className="flex-1 min-w-48">
            <div className="text-xs text-zinc-400 mb-1 font-semibold">Subject 1</div>
            <select
              value={subject1}
              onChange={e => setSubject1(e.target.value)}
              disabled={!selectedClass || loadingSubjects}
              aria-label="Subject 1"
              className={SELECT_CLS}
            >
              <option value="">— Subject —</option>
              {(subjects ?? []).map(s => <option key={s.name} value={s.name}>{s.name}</option>)}
            </select>
          </div>

          <div className="flex items-center pt-4">
            <span className="text-zinc-600 font-bold text-lg px-2">vs</span>
          </div>

          <div className="flex-1 min-w-48">
            <div className="text-xs text-zinc-400 mb-1 font-semibold">Subject 2</div>
            <select
              value={subject2}
              onChange={e => setSubject2(e.target.value)}
              disabled={!selectedClass || loadingSubjects}
              aria-label="Subject 2"
              className={SELECT_CLS}
            >
              <option value="">— Subject —</option>
              {(subjects ?? []).map(s => <option key={s.name} value={s.name}>{s.name}</option>)}
            </select>
          </div>
        </div>

        {(!subject1 || !subject2) && !loadingClasses && (
          <p className="text-xs text-zinc-500 mt-3">Select a class and both subjects to enable calculations.</p>
        )}
      </div>

      {/* ── Calculation Buttons ── */}
      <div className="bg-zinc-900 border border-zinc-800 rounded-xl p-6 mb-6">
        <h2 className="text-lg font-semibold mb-4">2. Run Correlation</h2>
        <div className="flex flex-wrap gap-3">
          <button
            onClick={runSerial}
            disabled={!canRun}
            className="bg-blue-600 hover:bg-blue-500 disabled:opacity-50 px-5 py-2 rounded font-semibold transition-colors"
          >
            {loading === "serial" ? "Running..." : "Run Serial"}
          </button>
          <button
            onClick={runParallel}
            disabled={!canRun}
            className="bg-purple-600 hover:bg-purple-500 disabled:opacity-50 px-5 py-2 rounded font-semibold transition-colors"
          >
            {loading === "parallel" ? "Running..." : "Run Parallel (OpenMP)"}
          </button>
          <button
            onClick={runPthread}
            disabled={!canRun}
            className="bg-cyan-600 hover:bg-cyan-500 disabled:opacity-50 px-5 py-2 rounded font-semibold transition-colors"
          >
            {loading === "pthread" ? "Running..." : "Run Pthreads"}
          </button>
          <button
            onClick={runMpi}
            disabled={!canRun}
            className="bg-green-600 hover:bg-green-500 disabled:opacity-50 px-5 py-2 rounded font-semibold transition-colors"
          >
            {loading === "mpi" ? "Running..." : "Run MPI (Distributed)"}
          </button>
          <button
            onClick={runCompare}
            disabled={!canRun}
            className="bg-amber-600 hover:bg-amber-500 disabled:opacity-50 px-6 py-2 rounded font-bold transition-colors text-lg"
          >
            {loading === "compare" ? "Comparing..." : "Compare All"}
          </button>
        </div>
      </div>

      {/* Error */}
      {error && (
        <div className="bg-red-900/30 border border-red-800 rounded-xl p-4 mb-6 text-red-400">
          {error}
        </div>
      )}

      {/* ── Individual Results ── */}
      {(serialResult || parallelResult || pthreadResult || mpiResult) && !compareResult && (
        <div className="grid md:grid-cols-2 lg:grid-cols-3 gap-6 mb-6">
          {serialResult && (
            <CorrelationPanel
              result={serialResult}
              borderColor="border-blue-700"
              subject1Name={subject1}
              subject2Name={subject2}
              chartColor="#3b82f6"
            />
          )}
          {parallelResult && (
            <CorrelationPanel
              result={parallelResult}
              borderColor="border-purple-700"
              subject1Name={subject1}
              subject2Name={subject2}
              chartColor="#a855f7"
            />
          )}
          {pthreadResult && (
            <CorrelationPanel
              result={pthreadResult}
              borderColor="border-cyan-700"
              subject1Name={subject1}
              subject2Name={subject2}
              chartColor="#06b6d4"
            />
          )}
          {mpiResult && (
            <CorrelationPanel
              result={mpiResult}
              borderColor="border-green-700"
              subject1Name={subject1}
              subject2Name={subject2}
              chartColor="#22c55e"
            />
          )}
        </div>
      )}

      {/* ── Compare Results ── */}
      {compareResult && (
        <>
          {/* Speedup banner */}
          <div className="bg-linear-to-r from-amber-900/40 to-amber-800/20 border border-amber-700 rounded-xl p-6 mb-6 text-center">
            <div className="text-sm text-amber-400 mb-1 uppercase tracking-widest font-semibold">
              Performance Comparison
            </div>
            <div className="grid grid-cols-2 md:grid-cols-4 gap-4 mt-4">
              <div>
                <div className="text-xs text-zinc-400">Serial Time</div>
                <div className="text-2xl font-bold text-blue-400 font-mono">
                  {compareResult.comparison.serial_time_ms.toFixed(4)} ms
                </div>
                <div className="text-xs text-zinc-500">{compareResult.comparison.serial_threads} thread</div>
              </div>
              <div>
                <div className="text-xs text-zinc-400">Parallel (OpenMP)</div>
                <div className="text-2xl font-bold text-purple-400 font-mono">
                  {compareResult.comparison.parallel_time_ms.toFixed(4)} ms
                </div>
                <div className="text-xs text-zinc-500">{compareResult.comparison.parallel_threads} threads</div>
              </div>
              {compareResult.comparison.pthread_time_ms !== undefined && compareResult.comparison.pthread_time_ms > 0 && (
              <div>
                <div className="text-xs text-zinc-400">POSIX Threads</div>
                <div className="text-2xl font-bold text-cyan-400 font-mono">
                  {compareResult.comparison.pthread_time_ms.toFixed(4)} ms
                </div>
                <div className="text-xs text-zinc-500">{compareResult.comparison.pthread_threads} threads</div>
              </div>
              )}
              {compareResult.comparison.mpi_time_ms !== undefined && compareResult.comparison.mpi_time_ms > 0 && (
              <div>
                <div className="text-xs text-zinc-400">MPI Distributed</div>
                <div className="text-2xl font-bold text-green-400 font-mono">
                  {compareResult.comparison.mpi_time_ms.toFixed(4)} ms
                </div>
                <div className="text-xs text-zinc-500">{compareResult.comparison.mpi_threads} processes</div>
              </div>
              )}
            </div>
            <div className="grid grid-cols-1 sm:grid-cols-3 gap-4 mt-4">
              <div>
                <div className="text-xs text-zinc-400">Speedup (OpenMP)</div>
                <div className={`text-4xl font-black font-mono ${speedup >= 1 ? "text-emerald-400" : "text-red-400"}`}>
                  {speedup.toFixed(2)}x
                </div>
              </div>
              {compareResult.comparison.speedup_pthread !== undefined && compareResult.comparison.speedup_pthread > 0 && (
              <div>
                <div className="text-xs text-zinc-400">Speedup (Pthreads)</div>
                <div className={`text-4xl font-black font-mono ${compareResult.comparison.speedup_pthread >= 1 ? "text-cyan-400" : "text-red-400"}`}>
                  {compareResult.comparison.speedup_pthread.toFixed(2)}x
                </div>
              </div>
              )}
              {compareResult.comparison.speedup_mpi !== undefined && compareResult.comparison.speedup_mpi > 0 && (
              <div>
                <div className="text-xs text-zinc-400">Speedup (MPI)</div>
                <div className={`text-4xl font-black font-mono ${compareResult.comparison.speedup_mpi >= 1 ? "text-green-400" : "text-red-400"}`}>
                  {compareResult.comparison.speedup_mpi.toFixed(2)}x
                </div>
              </div>
              )}
            </div>
            <div className="mt-3 pt-3 border-t border-amber-800/50 text-xs text-zinc-400 text-center">
              <span className="text-white font-semibold font-mono">{compareResult.comparison.n_pairs}</span> students considered
            </div>
          </div>

          {/* Side-by-side panels */}
          <div className={`grid md:grid-cols-2 ${
            compareResult.mpi ? 'lg:grid-cols-3 xl:grid-cols-4' : 'lg:grid-cols-3'
          } gap-6`}>
            <CorrelationPanel
              result={compareResult.serial}
              borderColor="border-blue-700"
              subject1Name={subject1}
              subject2Name={subject2}
              chartColor="#3b82f6"
            />
            <CorrelationPanel
              result={compareResult.parallel}
              borderColor="border-purple-700"
              subject1Name={subject1}
              subject2Name={subject2}
              chartColor="#a855f7"
            />
            {compareResult.pthread && (
              <CorrelationPanel
                result={compareResult.pthread}
                borderColor="border-cyan-700"
                subject1Name={subject1}
                subject2Name={subject2}
                chartColor="#06b6d4"
              />
            )}
            {compareResult.mpi && (
              <CorrelationPanel
                result={compareResult.mpi}
                borderColor="border-green-700"
                subject1Name={subject1}
                subject2Name={subject2}
                chartColor="#22c55e"
              />
            )}
          </div>
        </>
      )}
    </>
  );
}
