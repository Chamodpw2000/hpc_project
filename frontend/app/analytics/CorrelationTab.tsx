"use client";
import { useState, useEffect } from "react";
import {
  Line, LineChart, XAxis, YAxis,
  CartesianGrid, ResponsiveContainer,
} from "recharts";

interface SubjectOption { name: string; class_name: string; }
interface CorrelationResult {
  correlation_coefficient: number;
  elapsed_ms: number;
  db_fetch_ms: number;
  threads_used: number;
  n_pairs: number;
  total_students?: number;
  excluded?: number;
  best_fit_slope: number;
  best_fit_intercept: number;
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
    serial_db_fetch_ms: number;
    parallel_db_fetch_ms: number;
    pthread_db_fetch_ms?: number;
    mpi_db_fetch_ms?: number;
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

interface MethodEntry {
  r: number;
  elapsed_ms: number;
  threads_used: number;
  slope: number;
  intercept: number;
  speedup?: number;
}
interface SubjectLineResult {
  subject: string;
  n_pairs: number;
  serial: MethodEntry;
  parallel: MethodEntry;
  pthread: MethodEntry;
  mpi?: MethodEntry;
  speedup_mpi?: number;
}
interface AllSubjectsResult {
  reference_subject: string;
  class_name: string;
  openmp_threads: number;
  pthread_threads: number;
  subjects: SubjectLineResult[];
  timing: {
    fetch_phase_ms: number;
    calc_phase_ms: number;
    serial_total_ms: number;
    parallel_total_ms: number;
    pthread_total_ms: number;
    mpi_total_ms: number;
    db_fetch_ms: number;
    speedup_parallel: number;
    speedup_pthread: number;
    speedup_mpi: number;
  };
}

interface SingleMethodSubjectEntry {
  subject: string;
  n_pairs: number;
  r: number;
  elapsed_ms: number;
  threads_used: number;
  slope: number;
  intercept: number;
}
interface SingleMethodAllResult {
  method: string;
  reference_subject: string;
  class_name: string;
  threads: number;
  fetch_ms: number;
  calc_ms: number;
  subjects: SingleMethodSubjectEntry[];
}

type AllDisplayMode = "serial" | "parallel" | "pthread" | "mpi" | "compare" | null;

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


// ── Sub-components ────────────────────────────────────────────────────────────

function BestFitLineChart({ slope, intercept, color, refSubject, subject }: Readonly<{
  slope: number;
  intercept: number;
  color: string;
  refSubject: string;
  subject: string;
}>) {
  const lineData = [
    { x: 0,   y: Math.max(0, Math.min(100, intercept)) },
    { x: 100, y: Math.max(0, Math.min(100, slope * 100 + intercept)) },
  ];
  return (
    <ResponsiveContainer width="100%" height={140}>
      <LineChart data={lineData} margin={{ top: 6, right: 8, bottom: 28, left: 8 }}>
        <CartesianGrid strokeDasharray="3 3" stroke="#3f3f46" />
        <XAxis
          dataKey="x"
          type="number"
          domain={[0, 100]}
          label={{ value: refSubject, position: "insideBottom", offset: -16, fill: "#a1a1aa", fontSize: 10 }}
          tick={{ fill: "#71717a", fontSize: 10 }}
          tickLine={false}
        />
        <YAxis
          dataKey="y"
          type="number"
          domain={[0, 100]}
          label={{ value: subject, angle: -90, position: "insideLeft", fill: "#a1a1aa", fontSize: 10 }}
          tick={{ fill: "#71717a", fontSize: 10 }}
          tickLine={false}
          axisLine={false}
          width={30}
        />
        <Line
          type="linear"
          dataKey="y"
          stroke={color}
          strokeWidth={2}
          dot={false}
          activeDot={false}
          isAnimationActive={false}
        />
      </LineChart>
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
    : 'Correlation';


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

      {/* Student counts */}
      <div className="mb-2 grid grid-cols-3 gap-2 text-center">
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
      </div>

      {/* Timing breakdown */}
      <div className="mb-4 grid grid-cols-2 gap-2 text-center">
        <div className="bg-zinc-800 rounded-lg p-3">
          <div className="text-xs text-zinc-400">Calc Time</div>
          <div className="text-sm font-bold text-white font-mono">{result.elapsed_ms.toFixed(2)} ms</div>
        </div>
        <div className="bg-zinc-800 rounded-lg p-3">
          <div className="text-xs text-yellow-400/80">DB Fetch</div>
          <div className="text-sm font-bold text-yellow-400 font-mono">{result.db_fetch_ms.toFixed(2)} ms</div>
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
          Best-Fit Line
        </div>
        <BestFitLineChart
          slope={result.best_fit_slope}
          intercept={result.best_fit_intercept}
          color={chartColor}
          refSubject={subject1Name}
          subject={subject2Name}
        />
      </div>
    </div>
  );
}

// ── One vs All — subject card for a single method (individual run mode) ───────

const METHOD_CFG = {
  serial:   { border: "border-blue-700",   chart: "#3b82f6", name: "Serial",            unit: "thread",    unitPlural: "threads"   },
  parallel: { border: "border-purple-700", chart: "#a855f7", name: "Parallel (OpenMP)", unit: "thread",    unitPlural: "threads"   },
  pthread:  { border: "border-cyan-700",   chart: "#06b6d4", name: "POSIX Threads",     unit: "thread",    unitPlural: "threads"   },
  mpi:      { border: "border-green-700",  chart: "#22c55e", name: "MPI Distributed",   unit: "process",   unitPlural: "processes" },
} as const;

function SubjectResultCard({ s, method, refSubject }: Readonly<{
  s: SubjectLineResult;
  method: Exclude<AllDisplayMode, "compare" | null>;
  refSubject: string;
}>) {
  const entry = method === "mpi" ? s.mpi
              : method === "parallel" ? s.parallel
              : method === "pthread"  ? s.pthread
              : s.serial;
  if (!entry) return null;
  const { label, color: labelColor } = correlationLabel(entry.r);
  const cfg = METHOD_CFG[method];
  const unitStr = entry.threads_used === 1 ? cfg.unit : cfg.unitPlural;

  return (
    <div className={`border ${cfg.border} rounded-xl p-5 bg-zinc-900/50`}>
      <div className="flex items-center justify-between mb-3">
        <h3 className="font-bold text-white text-sm truncate">{s.subject}</h3>
        <span className="text-xs bg-zinc-800 text-zinc-300 px-2 py-1 rounded font-mono shrink-0 ml-2">
          {entry.threads_used} {unitStr}
        </span>
      </div>

      <div className="mb-3 p-3 bg-zinc-800/50 rounded-xl text-center">
        <div className="text-xs text-zinc-400 uppercase tracking-wider font-semibold mb-1">Pearson r</div>
        <div className="text-3xl font-black text-white font-mono">{entry.r.toFixed(4)}</div>
        <div className={`text-xs font-semibold mt-1 ${labelColor}`}>{label} · {directionLabel(entry.r)}</div>
        <div className="text-xs text-zinc-500 mt-1">{s.n_pairs} pairs</div>
      </div>

      <BestFitLineChart slope={entry.slope} intercept={entry.intercept} color={cfg.chart} refSubject={refSubject} subject={s.subject} />

      <div className="mt-2 pt-2 border-t border-zinc-800 flex items-center justify-between text-xs">
        <span className="text-zinc-400">Calc time</span>
        <span className="font-mono text-white">{entry.elapsed_ms.toFixed(2)} ms</span>
      </div>

      {entry.speedup !== undefined && entry.speedup > 0 && (
        <div className="mt-1 text-center">
          <span className={`text-xs font-mono font-bold px-2 py-0.5 rounded ${entry.speedup >= 1 ? "bg-emerald-900/40 text-emerald-300" : "bg-red-900/40 text-red-400"}`}>
            {entry.speedup.toFixed(2)}x speedup
          </span>
        </div>
      )}
    </div>
  );
}

// ── One vs All — per-method card inside a compare subject section (no timing) ─

function CompareMethodCard({ entry, method, nPairs, subject, refSubject }: Readonly<{
  entry: MethodEntry;
  method: Exclude<AllDisplayMode, "compare" | null>;
  nPairs: number;
  subject: string;
  refSubject: string;
}>) {
  const { label, color: labelColor } = correlationLabel(entry.r);
  const cfg = METHOD_CFG[method];
  const unitStr = entry.threads_used === 1 ? cfg.unit : cfg.unitPlural;

  return (
    <div className={`border ${cfg.border} rounded-xl p-4 bg-zinc-900/40`}>
      <div className="flex items-center justify-between mb-2">
        <span className="text-xs font-bold text-zinc-300 uppercase tracking-wider">{cfg.name}</span>
        <span className="text-xs text-zinc-500 font-mono">{entry.threads_used} {unitStr}</span>
      </div>

      <div className="mb-2 p-2 bg-zinc-800/50 rounded-lg text-center">
        <div className="text-2xl font-black font-mono text-white">{entry.r.toFixed(4)}</div>
        <div className={`text-xs font-semibold mt-0.5 ${labelColor}`}>{label} · {directionLabel(entry.r)}</div>
        <div className="text-xs text-zinc-500 mt-0.5">{nPairs} pairs</div>
      </div>

      <BestFitLineChart slope={entry.slope} intercept={entry.intercept} color={cfg.chart} refSubject={refSubject} subject={subject} />

      {entry.speedup !== undefined && entry.speedup > 0 && (
        <div className="mt-1 text-center">
          <span className={`text-xs font-mono font-bold px-2 py-0.5 rounded ${entry.speedup >= 1 ? "bg-emerald-900/40 text-emerald-300" : "bg-red-900/40 text-red-400"}`}>
            {entry.speedup.toFixed(2)}x
          </span>
        </div>
      )}
    </div>
  );
}

// ── Main component ────────────────────────────────────────────────────────────

const SELECT_CLS =
  "bg-zinc-800 border border-zinc-700 rounded-lg px-3 py-2 text-white text-sm " +
  "focus:border-indigo-500 focus:outline-none disabled:opacity-50 w-full";

export default function CorrelationTab() {
  // Mode
  const [mode, setMode] = useState<"pair" | "all">("pair");

  // Selector state
  const [classes, setClasses]             = useState<string[]>([]);
  const [loadingClasses, setLoadingClasses] = useState(true);

  const [selectedClass, setSelectedClass] = useState("");
  const [subjects, setSubjects]           = useState<SubjectOption[] | null>(null);
  const [subject1, setSubject1]           = useState("");
  const [subject2, setSubject2]           = useState("");
  const [refSubject, setRefSubject]       = useState("");

  // Action state
  const [loading, setLoading]             = useState<"serial" | "parallel" | "pthread" | "mpi" | "compare" | "compare-fast" | "all" | "all-compare" | null>(null);
  const [error, setError]                 = useState<string | null>(null);
  const [openmpThreads, setOpenmpThreads] = useState(4);
  const [pthreadThreads, setPthreadThreads] = useState(4);
  const [allOpenmpThreads, setAllOpenmpThreads]   = useState(4);
  const [allPthreadThreads, setAllPthreadThreads] = useState(4);
  const [mpiProcesses, setMpiProcesses]           = useState(2);
  const [allMpiProcesses, setAllMpiProcesses]     = useState(2);

  // Result state
  const [serialResult, setSerialResult]           = useState<CorrelationResult | null>(null);
  const [parallelResult, setParallelResult]       = useState<CorrelationResult | null>(null);
  const [pthreadResult, setPthreadResult]         = useState<CorrelationResult | null>(null);
  const [mpiResult, setMpiResult]                 = useState<CorrelationResult | null>(null);
  const [compareResult, setCompareResult]         = useState<CorrelationCompare | null>(null);
  const [allSubjectsResult, setAllSubjectsResult] = useState<AllSubjectsResult | null>(null);
  const [allDisplayMode, setAllDisplayMode]       = useState<AllDisplayMode>(null);
  // non-null only when "Compare All" (separate-fetch) produced the current result
  const [allMethodFetchMs, setAllMethodFetchMs]   = useState<{serial:number;parallel:number;pthread:number;mpi?:number} | null>(null);

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
    if (!selectedClass) { setSubjects([]); setSubject1(""); setSubject2(""); setRefSubject(""); return; }
    setSubjects(null);
    setSubject1(""); setSubject2(""); setRefSubject("");
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
      setRefSubject(list[0].name);
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
      const res  = await fetch(`${API}/api/calculate/correlation/parallel?${buildParams()}&threads=${openmpThreads}`);
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
      const res  = await fetch(`${API}/api/calculate/correlation/pthread?${buildParams()}&threads=${pthreadThreads}`);
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
      const res  = await fetch(`${API}/api/calculate/correlation/mpi?${buildParams()}&mpi_processes=${mpiProcesses}`);
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
    setLoading("compare"); setError(null);
    setSerialResult(null); setParallelResult(null); setPthreadResult(null); setMpiResult(null);
    try {
      const base = `${API}/api/calculate/correlation`;
      const p    = buildParams();

      // Run sequentially — all handlers compete for the same calc_lock mutex, so
      // concurrent requests race and the slower ones hit their 30 s timeout (503).
      const sJson   = await fetch(`${base}/serial?${p}`).then(r => r.json());
      const parJson = await fetch(`${base}/parallel?${p}&threads=${openmpThreads}`).then(r => r.json());
      const ptJson  = await fetch(`${base}/pthread?${p}&threads=${pthreadThreads}`).then(r => r.json());
      const mpiJson = await fetch(`${base}/mpi?${p}&mpi_processes=${mpiProcesses}`).then(r => r.json()).catch(() => null);

      if (!sJson?.data || !parJson?.data || !ptJson?.data)
        throw new Error(sJson?.message ?? parJson?.message ?? ptJson?.message ?? "Request failed");

      const s:   CorrelationResult = sJson.data;
      const par: CorrelationResult = parJson.data;
      const pt:  CorrelationResult = ptJson.data;
      const mpi: CorrelationResult | undefined = mpiJson?.data ?? undefined;

      const speedup         = par.elapsed_ms > 0 ? s.elapsed_ms / par.elapsed_ms : 0;
      const speedup_pthread = pt.elapsed_ms  > 0 ? s.elapsed_ms / pt.elapsed_ms  : 0;
      const speedup_mpi     = mpi && mpi.elapsed_ms > 0 ? s.elapsed_ms / mpi.elapsed_ms : undefined;

      setCompareResult({
        serial: s, parallel: par, pthread: pt, mpi,
        comparison: {
          serial_time_ms:        s.elapsed_ms,
          parallel_time_ms:      par.elapsed_ms,
          pthread_time_ms:       pt.elapsed_ms,
          mpi_time_ms:           mpi?.elapsed_ms,
          serial_db_fetch_ms:    s.db_fetch_ms,
          parallel_db_fetch_ms:  par.db_fetch_ms,
          pthread_db_fetch_ms:   pt.db_fetch_ms,
          mpi_db_fetch_ms:       mpi?.db_fetch_ms,
          speedup,
          speedup_pthread,
          speedup_mpi,
          serial_threads:        s.threads_used,
          parallel_threads:      par.threads_used,
          pthread_threads:       pt.threads_used,
          mpi_threads:           mpi?.threads_used,
          n_pairs:               s.n_pairs,
          total_students:        s.total_students,
          excluded:              s.excluded,
          improvement_pct:       (speedup - 1) * 100,
        },
      });
    } catch (e) {
      setError(`Correlation comparison failed: ${e}`);
    }
    setLoading(null);
  }

  async function runCompareFast() {
    setLoading("compare-fast"); setError(null);
    setSerialResult(null); setParallelResult(null); setPthreadResult(null); setMpiResult(null);
    try {
      const p   = buildParams();
      const res = await fetch(
        `${API}/api/calculate/correlation/compare?${p}&threads=${openmpThreads}&pthread_threads=${pthreadThreads}&mpi_processes=${mpiProcesses}`
      );
      const json = await res.json();
      if (!res.ok || !json?.data?.serial || !json?.data?.parallel || !json?.data?.pthread)
        throw new Error(json?.message ?? "Request failed");

      const d = json.data;
      const shared = d.comparison.db_fetch_ms;
      setCompareResult({
        serial: d.serial, parallel: d.parallel, pthread: d.pthread, mpi: d.mpi ?? undefined,
        comparison: {
          serial_time_ms:       d.comparison.serial_time_ms,
          parallel_time_ms:     d.comparison.parallel_time_ms,
          pthread_time_ms:      d.comparison.pthread_time_ms,
          mpi_time_ms:          d.comparison.mpi_time_ms,
          serial_db_fetch_ms:   shared,
          parallel_db_fetch_ms: shared,
          pthread_db_fetch_ms:  shared,
          mpi_db_fetch_ms:      shared,
          speedup:              d.comparison.speedup,
          speedup_pthread:      d.comparison.speedup_pthread,
          speedup_mpi:          d.comparison.speedup_mpi,
          serial_threads:       d.comparison.serial_threads,
          parallel_threads:     d.comparison.parallel_threads,
          pthread_threads:      d.comparison.pthread_threads,
          mpi_threads:          d.comparison.mpi_threads,
          n_pairs:              d.comparison.n_pairs,
          total_students:       d.comparison.total_students,
          excluded:             d.comparison.excluded,
          improvement_pct:      d.comparison.improvement_pct,
        },
      });
    } catch (e) {
      setError(`Correlation comparison failed: ${e}`);
    }
    setLoading(null);
  }

  async function runAllSubjects(displayMode: Exclude<AllDisplayMode, null> = "compare") {
    if (!selectedClass || !refSubject) return;
    setLoading("all"); setError(null); setAllSubjectsResult(null);
    setAllDisplayMode(displayMode);
    setAllMethodFetchMs(null);
    try {
      const params = new URLSearchParams({
        class: selectedClass,
        subject: refSubject,
        openmp_threads:  String(allOpenmpThreads),
        pthread_threads: String(allPthreadThreads),
        mpi_processes:   String(allMpiProcesses),
      });
      const res  = await fetch(`${API}/api/calculate/correlation/all-subjects?${params}`);
      const json = await res.json();
      if (!res.ok) throw new Error(json.message ?? "Request failed");
      setAllSubjectsResult(json.data);
    } catch (e) {
      setError(`One vs All correlation failed: ${e}`);
    }
    setLoading(null);
  }

  async function runAllSubjectsCompare() {
    if (!selectedClass || !refSubject) return;
    setLoading("all-compare"); setError(null); setAllSubjectsResult(null);
    setAllDisplayMode("compare");
    setAllMethodFetchMs(null);
    try {
      const base = new URLSearchParams({
        class: selectedClass, subject: refSubject,
        openmp_threads:  String(allOpenmpThreads),
        pthread_threads: String(allPthreadThreads),
        mpi_processes:   String(allMpiProcesses),
      });
      const url = `${API}/api/calculate/correlation/all-subjects-method?${base}`;

      // Run sequentially to avoid calc_lock contention
      const sJson   = await fetch(`${url}&method=serial`).then(r => r.json());
      const parJson = await fetch(`${url}&method=parallel`).then(r => r.json());
      const ptJson  = await fetch(`${url}&method=pthread`).then(r => r.json());
      const mpiJson = await fetch(`${url}&method=mpi`).then(r => r.json()).catch(() => null);

      if (!sJson?.data || !parJson?.data || !ptJson?.data)
        throw new Error(sJson?.message ?? parJson?.message ?? "Request failed");

      const s:   SingleMethodAllResult = sJson.data;
      const par: SingleMethodAllResult = parJson.data;
      const pt:  SingleMethodAllResult = ptJson.data;
      const mpi: SingleMethodAllResult | null = mpiJson?.data ?? null;

      // Merge 4 per-method responses into AllSubjectsResult
      const subjectMap = new Map<string, SubjectLineResult>();
      s.subjects.forEach(e => subjectMap.set(e.subject, {
        subject: e.subject, n_pairs: e.n_pairs,
        serial:   { r: e.r, elapsed_ms: e.elapsed_ms, threads_used: e.threads_used, slope: e.slope, intercept: e.intercept },
        parallel: { r: 0, elapsed_ms: 0, threads_used: 0, slope: 0, intercept: 0 },
        pthread:  { r: 0, elapsed_ms: 0, threads_used: 0, slope: 0, intercept: 0 },
      }));
      par.subjects.forEach(e => { const ex = subjectMap.get(e.subject); if (ex) ex.parallel = { r: e.r, elapsed_ms: e.elapsed_ms, threads_used: e.threads_used, slope: e.slope, intercept: e.intercept, speedup: s.subjects.find(x => x.subject === e.subject) ? (s.subjects.find(x => x.subject === e.subject)!.elapsed_ms > 0 ? s.subjects.find(x => x.subject === e.subject)!.elapsed_ms / e.elapsed_ms : 0) : 0 }; });
      pt.subjects.forEach(e  => { const ex = subjectMap.get(e.subject); if (ex) ex.pthread  = { r: e.r, elapsed_ms: e.elapsed_ms, threads_used: e.threads_used, slope: e.slope, intercept: e.intercept, speedup: s.subjects.find(x => x.subject === e.subject) ? (s.subjects.find(x => x.subject === e.subject)!.elapsed_ms > 0 ? s.subjects.find(x => x.subject === e.subject)!.elapsed_ms / e.elapsed_ms : 0) : 0 }; });
      if (mpi) mpi.subjects.forEach(e => { const ex = subjectMap.get(e.subject); if (ex) ex.mpi = { r: e.r, elapsed_ms: e.elapsed_ms, threads_used: e.threads_used, slope: e.slope, intercept: e.intercept, speedup: s.subjects.find(x => x.subject === e.subject) ? (s.subjects.find(x => x.subject === e.subject)!.elapsed_ms > 0 ? s.subjects.find(x => x.subject === e.subject)!.elapsed_ms / e.elapsed_ms : 0) : 0 }; });

      const combined: AllSubjectsResult = {
        reference_subject: s.reference_subject,
        class_name: s.class_name,
        openmp_threads: par.threads,
        pthread_threads: pt.threads,
        subjects: Array.from(subjectMap.values()),
        timing: {
          fetch_phase_ms:    Math.max(s.fetch_ms, par.fetch_ms, pt.fetch_ms, mpi?.fetch_ms ?? 0),
          calc_phase_ms:     Math.max(s.calc_ms,  par.calc_ms,  pt.calc_ms,  mpi?.calc_ms  ?? 0),
          serial_total_ms:   s.calc_ms,
          parallel_total_ms: par.calc_ms,
          pthread_total_ms:  pt.calc_ms,
          mpi_total_ms:      mpi?.calc_ms ?? 0,
          db_fetch_ms:       s.fetch_ms,
          speedup_parallel:  s.calc_ms > 0 && par.calc_ms > 0 ? s.calc_ms / par.calc_ms : 0,
          speedup_pthread:   s.calc_ms > 0 && pt.calc_ms  > 0 ? s.calc_ms / pt.calc_ms  : 0,
          speedup_mpi:       s.calc_ms > 0 && (mpi?.calc_ms ?? 0) > 0 ? s.calc_ms / (mpi!.calc_ms) : 0,
        },
      };
      setAllMethodFetchMs({ serial: s.fetch_ms, parallel: par.fetch_ms, pthread: pt.fetch_ms, mpi: mpi?.fetch_ms });
      setAllSubjectsResult(combined);
    } catch (e) {
      setError(`One vs All comparison failed: ${e}`);
    }
    setLoading(null);
  }

  const canRun    = !loading && !!subject1 && !!subject2;
  const canRunAll = !loading && !!selectedClass && !!refSubject;
  const speedup   = compareResult ? compareResult.comparison.speedup : 0;

  // Determine which button shows loading spinner in One vs All
  const allRunning = loading === "all" ? allDisplayMode : (loading === "all-compare" ? "compare" : null);

  return (
    <>
      {/* ── Mode Switcher ── */}
      <div className="flex gap-2 mb-6">
        <button
          onClick={() => setMode("pair")}
          className={`px-5 py-2 rounded-lg font-semibold text-sm transition-colors ${
            mode === "pair"
              ? "bg-indigo-600 text-white"
              : "bg-zinc-800 text-zinc-400 hover:text-white hover:bg-zinc-700"
          }`}
        >
          Pair Analysis
        </button>
        <button
          onClick={() => setMode("all")}
          className={`px-5 py-2 rounded-lg font-semibold text-sm transition-colors ${
            mode === "all"
              ? "bg-indigo-600 text-white"
              : "bg-zinc-800 text-zinc-400 hover:text-white hover:bg-zinc-700"
          }`}
        >
          One vs All
        </button>
      </div>

      {/* ── PAIR ANALYSIS MODE ── */}
      {mode === "pair" && (
        <>
          {/* Subject Selectors */}
          <div className="bg-zinc-900 border border-zinc-800 rounded-xl p-6 mb-6">
            <h2 className="text-lg font-semibold mb-4">1. Select Subjects</h2>

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

          {/* Calculation Buttons */}
          <div className="bg-zinc-900 border border-zinc-800 rounded-xl p-6 mb-6">
            <h2 className="text-lg font-semibold mb-4">2. Run Correlation</h2>
            <div className="space-y-3">
              <div className="flex flex-wrap gap-3 items-end">
                <button onClick={runSerial} disabled={!canRun}
                  className="bg-blue-600 hover:bg-blue-500 disabled:opacity-50 px-5 py-2 rounded font-semibold transition-colors">
                  {loading === "serial" ? "Running..." : "Run Serial"}
                </button>
                <div>
                  <label className="block text-xs text-zinc-400 mb-1">MPI Processes</label>
                  <input type="number" min={1} max={64} value={mpiProcesses}
                    onChange={e => setMpiProcesses(Math.max(1, parseInt(e.target.value) || 1))}
                    disabled={!canRun}
                    className="bg-zinc-800 border border-zinc-700 rounded px-3 py-2 w-20 text-white font-mono disabled:opacity-50" />
                </div>
                <button onClick={runMpi} disabled={!canRun}
                  className="bg-green-600 hover:bg-green-500 disabled:opacity-50 px-5 py-2 rounded font-semibold transition-colors">
                  {loading === "mpi" ? "Running..." : "Run MPI (Distributed)"}
                </button>
              </div>

              <div className="flex flex-wrap gap-4 items-end">
                <div>
                  <label className="block text-xs text-zinc-400 mb-1">OpenMP Threads</label>
                  <input type="number" min={1} max={256} value={openmpThreads}
                    onChange={e => setOpenmpThreads(Math.max(1, parseInt(e.target.value) || 1))}
                    disabled={!canRun}
                    className="bg-zinc-800 border border-zinc-700 rounded px-3 py-2 w-20 text-white font-mono disabled:opacity-50" />
                </div>
                <button onClick={runParallel} disabled={!canRun}
                  className="bg-purple-600 hover:bg-purple-500 disabled:opacity-50 px-5 py-2 rounded font-semibold transition-colors">
                  {loading === "parallel" ? "Running..." : "Run Parallel (OpenMP)"}
                </button>
                <div>
                  <label className="block text-xs text-zinc-400 mb-1">Pthreads</label>
                  <input type="number" min={1} max={256} value={pthreadThreads}
                    onChange={e => setPthreadThreads(Math.max(1, parseInt(e.target.value) || 1))}
                    disabled={!canRun}
                    className="bg-zinc-800 border border-zinc-700 rounded px-3 py-2 w-20 text-white font-mono disabled:opacity-50" />
                </div>
                <button onClick={runPthread} disabled={!canRun}
                  className="bg-cyan-600 hover:bg-cyan-500 disabled:opacity-50 px-5 py-2 rounded font-semibold transition-colors">
                  {loading === "pthread" ? "Running..." : "Run Pthreads"}
                </button>
              </div>

              <div className="flex flex-wrap gap-4 items-end pt-1 border-t border-zinc-800">
                <button onClick={runCompare} disabled={!canRun}
                  className="bg-amber-600 hover:bg-amber-500 disabled:opacity-50 px-6 py-2 rounded font-bold transition-colors text-lg">
                  {loading === "compare" ? "Comparing..." : "Compare All"}
                </button>
                <button onClick={runCompareFast} disabled={!canRun}
                  className="bg-emerald-700 hover:bg-emerald-600 disabled:opacity-50 px-6 py-2 rounded font-bold transition-colors text-lg">
                  {loading === "compare-fast" ? "Comparing..." : "Compare All (Fast)"}
                </button>
                <span className="text-xs text-zinc-500 self-end pb-2">In fast compare data fetch for one time for all calculations</span>
              </div>
            </div>
          </div>

          {/* Error */}
          {error && (
            <div className="bg-red-900/30 border border-red-800 rounded-xl p-4 mb-6 text-red-400">{error}</div>
          )}

          {/* Individual Results — max 3 per row */}
          {(serialResult || parallelResult || pthreadResult || mpiResult) && !compareResult && (
            <div className="grid md:grid-cols-2 lg:grid-cols-3 gap-6 mb-6">
              {serialResult   && <CorrelationPanel result={serialResult}   borderColor="border-blue-700"   subject1Name={subject1} subject2Name={subject2} chartColor="#3b82f6" />}
              {parallelResult && <CorrelationPanel result={parallelResult} borderColor="border-purple-700" subject1Name={subject1} subject2Name={subject2} chartColor="#a855f7" />}
              {pthreadResult  && <CorrelationPanel result={pthreadResult}  borderColor="border-cyan-700"   subject1Name={subject1} subject2Name={subject2} chartColor="#06b6d4" />}
              {mpiResult      && <CorrelationPanel result={mpiResult}      borderColor="border-green-700"  subject1Name={subject1} subject2Name={subject2} chartColor="#22c55e" />}
            </div>
          )}

          {/* Compare Results */}
          {compareResult && (
            <>
              <div className="bg-linear-to-r from-amber-900/40 to-amber-800/20 border border-amber-700 rounded-xl p-6 mb-6">
                <div className="text-sm text-amber-400 mb-1 uppercase tracking-widest font-semibold text-center">Performance Comparison</div>
                <div className="text-xs text-zinc-500 text-center mb-4">
                  <span className="text-white font-semibold font-mono">{compareResult.comparison.n_pairs}</span> student pairs analysed
                </div>

                {/* Per-method: calc time + db fetch */}
                <div className="grid grid-cols-2 md:grid-cols-4 gap-3 mb-4">
                  {/* Serial */}
                  <div className="bg-zinc-800/50 rounded-xl p-3 text-center">
                    <div className="text-xs text-blue-400 font-semibold mb-2">Serial</div>
                    <div className="text-xs text-zinc-400">Calc</div>
                    <div className="text-xl font-bold text-blue-400 font-mono">{compareResult.comparison.serial_time_ms.toFixed(3)} ms</div>
                    <div className="mt-1 text-xs text-yellow-400/80">DB Fetch</div>
                    <div className="text-base font-semibold text-yellow-400 font-mono">{compareResult.comparison.serial_db_fetch_ms.toFixed(3)} ms</div>
                    <div className="mt-1 text-xs text-zinc-500">{compareResult.comparison.serial_threads} thread</div>
                  </div>

                  {/* OpenMP */}
                  <div className="bg-zinc-800/50 rounded-xl p-3 text-center">
                    <div className="text-xs text-purple-400 font-semibold mb-2">Parallel (OpenMP)</div>
                    <div className="text-xs text-zinc-400">Calc</div>
                    <div className="text-xl font-bold text-purple-400 font-mono">{compareResult.comparison.parallel_time_ms.toFixed(3)} ms</div>
                    <div className="mt-1 text-xs text-yellow-400/80">DB Fetch</div>
                    <div className="text-base font-semibold text-yellow-400 font-mono">{compareResult.comparison.parallel_db_fetch_ms.toFixed(3)} ms</div>
                    <div className="mt-1 text-xs text-zinc-500">{compareResult.comparison.parallel_threads} threads</div>
                  </div>

                  {/* Pthreads */}
                  {compareResult.comparison.pthread_time_ms !== undefined && compareResult.comparison.pthread_time_ms > 0 && (
                    <div className="bg-zinc-800/50 rounded-xl p-3 text-center">
                      <div className="text-xs text-cyan-400 font-semibold mb-2">POSIX Threads</div>
                      <div className="text-xs text-zinc-400">Calc</div>
                      <div className="text-xl font-bold text-cyan-400 font-mono">{compareResult.comparison.pthread_time_ms.toFixed(3)} ms</div>
                      <div className="mt-1 text-xs text-yellow-400/80">DB Fetch</div>
                      <div className="text-base font-semibold text-yellow-400 font-mono">{(compareResult.comparison.pthread_db_fetch_ms ?? 0).toFixed(3)} ms</div>
                      <div className="mt-1 text-xs text-zinc-500">{compareResult.comparison.pthread_threads} threads</div>
                    </div>
                  )}

                  {/* MPI */}
                  {compareResult.comparison.mpi_time_ms !== undefined && compareResult.comparison.mpi_time_ms > 0 && (
                    <div className="bg-zinc-800/50 rounded-xl p-3 text-center">
                      <div className="text-xs text-green-400 font-semibold mb-2">MPI Distributed</div>
                      <div className="text-xs text-zinc-400">Calc</div>
                      <div className="text-xl font-bold text-green-400 font-mono">{compareResult.comparison.mpi_time_ms.toFixed(3)} ms</div>
                      <div className="mt-1 text-xs text-yellow-400/80">DB Fetch</div>
                      <div className="text-base font-semibold text-yellow-400 font-mono">{(compareResult.comparison.mpi_db_fetch_ms ?? 0).toFixed(3)} ms</div>
                      <div className="mt-1 text-xs text-zinc-500">{compareResult.comparison.mpi_threads} processes</div>
                    </div>
                  )}
                </div>

                {/* Speedups */}
                <div className="grid grid-cols-1 sm:grid-cols-3 gap-4 pt-3 border-t border-amber-800/50 text-center">
                  <div>
                    <div className="text-xs text-zinc-400">Speedup (OpenMP)</div>
                    <div className={`text-4xl font-black font-mono ${speedup >= 1 ? "text-emerald-400" : "text-red-400"}`}>{speedup.toFixed(2)}x</div>
                  </div>
                  {compareResult.comparison.speedup_pthread !== undefined && compareResult.comparison.speedup_pthread > 0 && (
                    <div>
                      <div className="text-xs text-zinc-400">Speedup (Pthreads)</div>
                      <div className={`text-4xl font-black font-mono ${compareResult.comparison.speedup_pthread >= 1 ? "text-cyan-400" : "text-red-400"}`}>{compareResult.comparison.speedup_pthread.toFixed(2)}x</div>
                    </div>
                  )}
                  {compareResult.comparison.speedup_mpi !== undefined && compareResult.comparison.speedup_mpi > 0 && (
                    <div>
                      <div className="text-xs text-zinc-400">Speedup (MPI)</div>
                      <div className={`text-4xl font-black font-mono ${compareResult.comparison.speedup_mpi >= 1 ? "text-green-400" : "text-red-400"}`}>{compareResult.comparison.speedup_mpi.toFixed(2)}x</div>
                    </div>
                  )}
                </div>
              </div>

              {/* Max 3 per row */}
              <div className="grid md:grid-cols-2 lg:grid-cols-3 gap-6">
                <CorrelationPanel result={compareResult.serial}   borderColor="border-blue-700"   subject1Name={subject1} subject2Name={subject2} chartColor="#3b82f6" />
                <CorrelationPanel result={compareResult.parallel} borderColor="border-purple-700" subject1Name={subject1} subject2Name={subject2} chartColor="#a855f7" />
                {compareResult.pthread && <CorrelationPanel result={compareResult.pthread} borderColor="border-cyan-700"  subject1Name={subject1} subject2Name={subject2} chartColor="#06b6d4" />}
                {compareResult.mpi     && <CorrelationPanel result={compareResult.mpi}    borderColor="border-green-700" subject1Name={subject1} subject2Name={subject2} chartColor="#22c55e" />}
              </div>
            </>
          )}
        </>
      )}

      {/* ── ONE VS ALL MODE ── */}
      {mode === "all" && (
        <>
          {/* 1. Select Reference Subject */}
          <div className="bg-zinc-900 border border-zinc-800 rounded-xl p-6 mb-6">
            <h2 className="text-lg font-semibold mb-4">1. Select Reference Subject</h2>
            <p className="text-sm text-zinc-400 mb-4">
              Pick a class and a reference subject. The backend computes correlation of every other subject against it.
            </p>

            <div className="flex flex-wrap gap-4">
              <div className="flex-1 min-w-48">
                <div className="text-xs text-zinc-400 mb-1 font-semibold">Class</div>
                <select
                  value={selectedClass}
                  onChange={e => setSelectedClass(e.target.value)}
                  disabled={loadingClasses}
                  aria-label="Class"
                  className={SELECT_CLS}
                >
                  <option value="">— Class —</option>
                  {classes.map(c => <option key={c} value={c}>{c}</option>)}
                </select>
              </div>

              <div className="flex-1 min-w-48">
                <div className="text-xs text-zinc-400 mb-1 font-semibold">Reference Subject</div>
                <select
                  value={refSubject}
                  onChange={e => setRefSubject(e.target.value)}
                  disabled={!selectedClass || loadingSubjects}
                  aria-label="Reference Subject"
                  className={SELECT_CLS}
                >
                  <option value="">— Subject —</option>
                  {(subjects ?? []).map(s => <option key={s.name} value={s.name}>{s.name}</option>)}
                </select>
              </div>
            </div>
          </div>

          {/* 2. Run Correlation — similar layout to Pair Analysis */}
          <div className="bg-zinc-900 border border-zinc-800 rounded-xl p-6 mb-6">
            <h2 className="text-lg font-semibold mb-4">2. Run Correlation</h2>
            <div className="space-y-3">
              {/* Row 1: Serial | MPI Processes | Run MPI */}
              <div className="flex flex-wrap gap-3 items-end">
                <button
                  onClick={() => runAllSubjects("serial")}
                  disabled={!canRunAll}
                  className="bg-blue-600 hover:bg-blue-500 disabled:opacity-50 px-5 py-2 rounded font-semibold transition-colors"
                >
                  {allRunning === "serial" ? "Running..." : "Run Serial"}
                </button>
                <div>
                  <label className="block text-xs text-zinc-400 mb-1">MPI Processes</label>
                  <input
                    type="number" min={1} max={64} value={allMpiProcesses}
                    onChange={e => setAllMpiProcesses(Math.max(1, parseInt(e.target.value) || 1))}
                    disabled={!canRunAll}
                    className="bg-zinc-800 border border-zinc-700 rounded px-3 py-2 w-20 text-white font-mono disabled:opacity-50"
                  />
                </div>
                <button
                  onClick={() => runAllSubjects("mpi")}
                  disabled={!canRunAll}
                  className="bg-green-600 hover:bg-green-500 disabled:opacity-50 px-5 py-2 rounded font-semibold transition-colors"
                >
                  {allRunning === "mpi" ? "Running..." : "Run MPI (Distributed)"}
                </button>
              </div>

              {/* Row 2: OpenMP Threads | Run OpenMP | Pthreads | Run Pthreads */}
              <div className="flex flex-wrap gap-4 items-end">
                <div>
                  <label className="block text-xs text-zinc-400 mb-1">OpenMP Threads</label>
                  <input
                    type="number" min={1} max={256} value={allOpenmpThreads}
                    onChange={e => setAllOpenmpThreads(Math.max(1, parseInt(e.target.value) || 1))}
                    disabled={!canRunAll}
                    className="bg-zinc-800 border border-zinc-700 rounded px-3 py-2 w-20 text-white font-mono disabled:opacity-50"
                  />
                </div>
                <button
                  onClick={() => runAllSubjects("parallel")}
                  disabled={!canRunAll}
                  className="bg-purple-600 hover:bg-purple-500 disabled:opacity-50 px-5 py-2 rounded font-semibold transition-colors"
                >
                  {allRunning === "parallel" ? "Running..." : "Run Parallel (OpenMP)"}
                </button>
                <div>
                  <label className="block text-xs text-zinc-400 mb-1">Pthreads</label>
                  <input
                    type="number" min={1} max={256} value={allPthreadThreads}
                    onChange={e => setAllPthreadThreads(Math.max(1, parseInt(e.target.value) || 1))}
                    disabled={!canRunAll}
                    className="bg-zinc-800 border border-zinc-700 rounded px-3 py-2 w-20 text-white font-mono disabled:opacity-50"
                  />
                </div>
                <button
                  onClick={() => runAllSubjects("pthread")}
                  disabled={!canRunAll}
                  className="bg-cyan-600 hover:bg-cyan-500 disabled:opacity-50 px-5 py-2 rounded font-semibold transition-colors"
                >
                  {allRunning === "pthread" ? "Running..." : "Run Pthreads"}
                </button>
              </div>

              {/* Row 3: Compare All */}
              <div className="flex flex-wrap gap-4 items-end pt-1 border-t border-zinc-800">
                <button
                  onClick={runAllSubjectsCompare}
                  disabled={!canRunAll}
                  className="bg-amber-600 hover:bg-amber-500 disabled:opacity-50 px-6 py-2 rounded font-bold transition-colors text-lg"
                >
                  {loading === "all-compare" ? "Comparing..." : "Compare All"}
                </button>
                <button
                  onClick={() => runAllSubjects("compare")}
                  disabled={!canRunAll}
                  className="bg-emerald-700 hover:bg-emerald-600 disabled:opacity-50 px-6 py-2 rounded font-bold transition-colors text-lg"
                >
                  {loading === "all" && allDisplayMode === "compare" ? "Comparing..." : "Compare All (Fast)"}
                </button>
                <span className="text-xs text-zinc-500 self-end pb-2">
                  In fast compare data fetch for one time for all calculations
                </span>
              </div>
            </div>
          </div>

          {/* Error */}
          {error && (
            <div className="bg-red-900/30 border border-red-800 rounded-xl p-4 mb-6 text-red-400">{error}</div>
          )}

          {/* ── Results: individual method mode ── */}
          {allSubjectsResult && allDisplayMode && allDisplayMode !== "compare" && (() => {
            const t   = allSubjectsResult.timing;
            const methodTotalMs =
              allDisplayMode === "serial"   ? t.serial_total_ms   :
              allDisplayMode === "parallel" ? t.parallel_total_ms :
              allDisplayMode === "pthread"  ? t.pthread_total_ms  : t.mpi_total_ms;
            const speedupVal =
              allDisplayMode === "serial"   ? null :
              allDisplayMode === "parallel" ? t.speedup_parallel :
              allDisplayMode === "pthread"  ? t.speedup_pthread  : t.speedup_mpi;
            const cfg = METHOD_CFG[allDisplayMode];
            const bannerBorder =
              allDisplayMode === "serial"   ? "border-blue-700"   :
              allDisplayMode === "parallel" ? "border-purple-700" :
              allDisplayMode === "pthread"  ? "border-cyan-700"   : "border-green-700";
            const bannerBg =
              allDisplayMode === "serial"   ? "from-blue-900/30 to-blue-800/10"     :
              allDisplayMode === "parallel" ? "from-purple-900/30 to-purple-800/10" :
              allDisplayMode === "pthread"  ? "from-cyan-900/30 to-cyan-800/10"     :
              "from-green-900/30 to-green-800/10";
            const timeColor =
              allDisplayMode === "serial"   ? "text-blue-400"   :
              allDisplayMode === "parallel" ? "text-purple-400" :
              allDisplayMode === "pthread"  ? "text-cyan-400"   : "text-green-400";

            return (
              <>
                {/* Summary banner */}
                <div className={`bg-linear-to-r ${bannerBg} border ${bannerBorder} rounded-xl p-6 mb-6`}>
                  <div className={`text-sm font-semibold uppercase tracking-widest mb-1 text-center ${timeColor}`}>
                    {cfg.name} — One vs All
                  </div>
                  <div className="text-xs text-zinc-400 text-center mb-4">
                    {allSubjectsResult.reference_subject} in {allSubjectsResult.class_name} ·{" "}
                    {allSubjectsResult.subjects.length} subjects
                  </div>
                  <div className={`grid grid-cols-2 ${speedupVal !== null && speedupVal !== undefined && speedupVal > 0 ? "sm:grid-cols-3" : "sm:grid-cols-2"} gap-4`}>
                    {/* Calc time */}
                    <div className="bg-zinc-800/50 rounded-xl p-4 text-center">
                      <div className="text-xs text-zinc-400 mb-1 uppercase tracking-wider">Total Calc Time</div>
                      <div className={`text-2xl font-black font-mono ${timeColor}`}>
                        {methodTotalMs.toFixed(2)} ms
                      </div>
                      <div className="text-xs text-zinc-500 mt-1">across {allSubjectsResult.subjects.length} subjects</div>
                    </div>

                    {/* DB fetch (shared) */}
                    <div className="bg-zinc-800/50 rounded-xl p-4 text-center">
                      <div className="text-xs text-yellow-400/80 mb-1 uppercase tracking-wider">DB Fetch (shared)</div>
                      <div className="text-2xl font-black font-mono text-yellow-400">
                        {t.fetch_phase_ms.toFixed(2)} ms
                      </div>
                      <div className="text-xs text-zinc-500 mt-1">wall-clock, all subjects</div>
                    </div>

                    {/* Speedup */}
                    {speedupVal !== null && speedupVal !== undefined && speedupVal > 0 && (
                      <div className="bg-zinc-800/50 rounded-xl p-4 text-center">
                        <div className="text-xs text-zinc-400 mb-1 uppercase tracking-wider">Speedup vs Serial</div>
                        <div className={`text-2xl font-black font-mono ${speedupVal >= 1 ? "text-emerald-400" : "text-red-400"}`}>
                          {speedupVal.toFixed(2)}x
                        </div>
                        <div className="text-xs text-zinc-500 mt-1">calc time only</div>
                      </div>
                    )}
                  </div>
                </div>

                {/* Subject cards — 3 per row */}
                <div className="grid md:grid-cols-2 lg:grid-cols-3 gap-6">
                  {allSubjectsResult.subjects.map(s =>
                    (allDisplayMode === "mpi" && !s.mpi) ? null : (
                      <SubjectResultCard
                        key={s.subject}
                        s={s}
                        method={allDisplayMode}
                        refSubject={allSubjectsResult.reference_subject}
                      />
                    )
                  )}
                </div>
              </>
            );
          })()}

          {/* ── Results: compare mode ── */}
          {allSubjectsResult && allDisplayMode === "compare" && (() => {
            const t = allSubjectsResult.timing;
            const hasMpi = t.mpi_total_ms > 0;

            return (
              <>
                {/* Comparison banner — aggregate totals only, similar to pair compare */}
                <div className="bg-linear-to-r from-amber-900/40 to-amber-800/20 border border-amber-700 rounded-xl p-6 mb-8">
                  <div className="text-sm text-amber-400 mb-1 uppercase tracking-widest font-semibold text-center">
                    Performance Comparison — One vs All
                  </div>
                  <div className="text-xs text-zinc-400 text-center mb-4">
                    {allSubjectsResult.reference_subject} in {allSubjectsResult.class_name} ·{" "}
                    {allSubjectsResult.subjects.length} subjects ·
                    OpenMP: {allSubjectsResult.openmp_threads} threads ·
                    Pthreads: {allSubjectsResult.pthread_threads} threads
                    {hasMpi ? ` · MPI: ${allMpiProcesses} processes` : ""}
                  </div>

                  {/* DB fetch banner — allMethodFetchMs non-null = separate per-method, null = shared */}
                  {allMethodFetchMs ? (
                    <div className="bg-yellow-950/30 border border-yellow-700/40 rounded-xl p-3 mb-4">
                      <div className="text-xs text-yellow-400/80 font-semibold uppercase tracking-wider mb-2">DB Fetch — Per Method (separate parallel fetch)</div>
                      <div className={`grid grid-cols-2 ${hasMpi ? "md:grid-cols-4" : "md:grid-cols-3"} gap-2 text-center`}>
                        <div><div className="text-xs text-blue-400">Serial</div><div className="text-sm font-bold text-yellow-400 font-mono">{(allMethodFetchMs.serial ?? 0).toFixed(2)} ms</div></div>
                        <div><div className="text-xs text-purple-400">OpenMP</div><div className="text-sm font-bold text-yellow-400 font-mono">{(allMethodFetchMs.parallel ?? 0).toFixed(2)} ms</div></div>
                        <div><div className="text-xs text-cyan-400">Pthreads</div><div className="text-sm font-bold text-yellow-400 font-mono">{(allMethodFetchMs.pthread ?? 0).toFixed(2)} ms</div></div>
                        {hasMpi && <div><div className="text-xs text-green-400">MPI</div><div className="text-sm font-bold text-yellow-400 font-mono">{(allMethodFetchMs.mpi ?? 0).toFixed(2)} ms</div></div>}
                      </div>
                    </div>
                  ) : (
                    <div className="bg-yellow-950/30 border border-yellow-700/40 rounded-xl p-3 mb-4 flex flex-wrap items-center justify-between gap-3">
                      <div>
                        <div className="text-xs text-yellow-400/80 font-semibold uppercase tracking-wider">DB Fetch — Shared across all methods</div>
                        <div className="text-xs text-zinc-500 mt-0.5">{allSubjectsResult.subjects.length} subjects fetched once in a shared parallel phase</div>
                      </div>
                      <div className="flex gap-6 text-center">
                        <div>
                          <div className="text-xs text-zinc-400">Wall-clock</div>
                          <div className="text-xl font-bold text-yellow-400 font-mono">{(t.fetch_phase_ms ?? 0).toFixed(2)} ms</div>
                        </div>
                        <div>
                          <div className="text-xs text-zinc-400">Accumulated</div>
                          <div className="text-xl font-bold text-yellow-300 font-mono">{(t.db_fetch_ms ?? 0).toFixed(2)} ms</div>
                        </div>
                      </div>
                    </div>
                  )}

                  {/* Per-method: calc time + DB fetch side-by-side */}
                  <div className={`grid grid-cols-2 ${hasMpi ? "md:grid-cols-4" : "md:grid-cols-3"} gap-3 mb-4`}>
                    {/* Serial */}
                    <div className="bg-zinc-800/50 rounded-xl p-3 text-center">
                      <div className="text-xs text-blue-400 font-semibold mb-2">Serial</div>
                      <div className="text-xs text-zinc-400">Calc</div>
                      <div className="text-xl font-bold text-blue-400 font-mono">{(t.serial_total_ms ?? 0).toFixed(2)} ms</div>
                      <div className="mt-1 text-xs text-yellow-400/70">{allMethodFetchMs ? "DB Fetch" : "DB Fetch (shared)"}</div>
                      <div className="text-sm font-semibold text-yellow-400/80 font-mono">{allMethodFetchMs ? (allMethodFetchMs.serial ?? 0).toFixed(2) : (t.fetch_phase_ms ?? 0).toFixed(2)} ms</div>
                      <div className="mt-1 text-xs text-zinc-500">1 thread</div>
                    </div>
                    {/* OpenMP */}
                    <div className="bg-zinc-800/50 rounded-xl p-3 text-center">
                      <div className="text-xs text-purple-400 font-semibold mb-2">OpenMP</div>
                      <div className="text-xs text-zinc-400">Calc</div>
                      <div className="text-xl font-bold text-purple-400 font-mono">{(t.parallel_total_ms ?? 0).toFixed(2)} ms</div>
                      <div className="mt-1 text-xs text-yellow-400/70">{allMethodFetchMs ? "DB Fetch (OpenMP)" : "DB Fetch (shared)"}</div>
                      <div className="text-sm font-semibold text-yellow-400/80 font-mono">{allMethodFetchMs ? (allMethodFetchMs.parallel ?? 0).toFixed(2) : (t.fetch_phase_ms ?? 0).toFixed(2)} ms</div>
                      <div className="mt-1 text-xs text-zinc-500">{allSubjectsResult.openmp_threads} threads</div>
                    </div>
                    {/* Pthreads */}
                    <div className="bg-zinc-800/50 rounded-xl p-3 text-center">
                      <div className="text-xs text-cyan-400 font-semibold mb-2">Pthreads</div>
                      <div className="text-xs text-zinc-400">Calc</div>
                      <div className="text-xl font-bold text-cyan-400 font-mono">{(t.pthread_total_ms ?? 0).toFixed(2)} ms</div>
                      <div className="mt-1 text-xs text-yellow-400/70">{allMethodFetchMs ? "DB Fetch (Pthreads)" : "DB Fetch (shared)"}</div>
                      <div className="text-sm font-semibold text-yellow-400/80 font-mono">{allMethodFetchMs ? (allMethodFetchMs.pthread ?? 0).toFixed(2) : (t.fetch_phase_ms ?? 0).toFixed(2)} ms</div>
                      <div className="mt-1 text-xs text-zinc-500">{allSubjectsResult.pthread_threads} threads</div>
                    </div>
                    {/* MPI */}
                    {hasMpi && (
                      <div className="bg-zinc-800/50 rounded-xl p-3 text-center">
                        <div className="text-xs text-green-400 font-semibold mb-2">MPI</div>
                        <div className="text-xs text-zinc-400">Calc</div>
                        <div className="text-xl font-bold text-green-400 font-mono">{(t.mpi_total_ms ?? 0).toFixed(2)} ms</div>
                        <div className="mt-1 text-xs text-yellow-400/70">{allMethodFetchMs ? "DB Fetch (MPI)" : "DB Fetch (shared)"}</div>
                        <div className="text-sm font-semibold text-yellow-400/80 font-mono">{allMethodFetchMs ? (allMethodFetchMs.mpi ?? 0).toFixed(2) : (t.fetch_phase_ms ?? 0).toFixed(2)} ms</div>
                        <div className="mt-1 text-xs text-zinc-500">{allMpiProcesses} processes</div>
                      </div>
                    )}
                  </div>

                  {/* Speedups row */}
                  <div className={`grid grid-cols-1 ${hasMpi ? "sm:grid-cols-3" : "sm:grid-cols-2"} gap-4 text-center pt-3 border-t border-amber-800/50`}>
                    <div>
                      <div className="text-xs text-zinc-400">Speedup (OpenMP)</div>
                      <div className={`text-4xl font-black font-mono ${t.speedup_parallel >= 1 ? "text-emerald-400" : "text-red-400"}`}>
                        {t.speedup_parallel.toFixed(2)}x
                      </div>
                      <div className="text-xs text-zinc-500">calc time only</div>
                    </div>
                    <div>
                      <div className="text-xs text-zinc-400">Speedup (Pthreads)</div>
                      <div className={`text-4xl font-black font-mono ${t.speedup_pthread >= 1 ? "text-cyan-400" : "text-red-400"}`}>
                        {t.speedup_pthread.toFixed(2)}x
                      </div>
                      <div className="text-xs text-zinc-500">calc time only</div>
                    </div>
                    {hasMpi && (
                      <div>
                        <div className="text-xs text-zinc-400">Speedup (MPI)</div>
                        <div className={`text-4xl font-black font-mono ${t.speedup_mpi >= 1 ? "text-green-400" : "text-red-400"}`}>
                          {t.speedup_mpi.toFixed(2)}x
                        </div>
                        <div className="text-xs text-zinc-500">calc time only</div>
                      </div>
                    )}
                  </div>
                </div>

                {/* Per-subject sections */}
                {allSubjectsResult.subjects.map(s => {
                  const { label, color: labelColor } = correlationLabel(s.serial.r);
                  return (
                    <div key={s.subject} className="mb-10">
                      {/* Section heading */}
                      <div className="flex items-center justify-between mb-4 pb-2 border-b border-zinc-800">
                        <div>
                          <h3 className="text-lg font-bold text-white">{s.subject}</h3>
                          <div className={`text-sm font-semibold ${labelColor}`}>
                            r = {s.serial.r.toFixed(4)} · {label} · {directionLabel(s.serial.r)}
                          </div>
                        </div>
                        <span className="text-xs text-zinc-500 font-mono bg-zinc-800 px-2 py-1 rounded">
                          {s.n_pairs} pairs
                        </span>
                      </div>

                      {/* Method cards — 3 per row, no timing */}
                      <div className="grid md:grid-cols-2 lg:grid-cols-3 gap-5">
                        <CompareMethodCard
                          entry={s.serial}
                          method="serial"
                          nPairs={s.n_pairs}
                          subject={s.subject}
                          refSubject={allSubjectsResult.reference_subject}
                        />
                        <CompareMethodCard
                          entry={s.parallel}
                          method="parallel"
                          nPairs={s.n_pairs}
                          subject={s.subject}
                          refSubject={allSubjectsResult.reference_subject}
                        />
                        <CompareMethodCard
                          entry={s.pthread}
                          method="pthread"
                          nPairs={s.n_pairs}
                          subject={s.subject}
                          refSubject={allSubjectsResult.reference_subject}
                        />
                        {s.mpi && (
                          <CompareMethodCard
                            entry={{ ...s.mpi, speedup: s.speedup_mpi }}
                            method="mpi"
                            nPairs={s.n_pairs}
                            subject={s.subject}
                            refSubject={allSubjectsResult.reference_subject}
                          />
                        )}
                      </div>
                    </div>
                  );
                })}
              </>
            );
          })()}
        </>
      )}
    </>
  );
}
