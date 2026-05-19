"use client";
import { useState, useEffect } from "react";
import { useRouter } from "next/navigation";
import CorrelationTab from "./CorrelationTab";

const API = "http://localhost:8090";

interface GradeDistribution {
  A_90_100: number;
  B_80_89: number;
  C_70_79: number;
  D_60_69: number;
  F_below_60: number;
}

interface Statistics {
  sum: number;
  mean: number;
  median: number;
  variance: number;
  stddev: number;
  min: number;
  max: number;
}

interface CalcResult {
  mode: string;
  threads_used: number;
  scores_count: number;
  elapsed_ms: number;
  sort_time_ms: number;
  db_fetch_ms: number;
  statistics: Statistics;
  grade_distribution: GradeDistribution;
}

interface Comparison {
  serial_time_ms: number;
  parallel_time_ms: number;
  db_fetch_ms: number;
  speedup: number;
  serial_threads: number;
  parallel_threads: number;
  data_size: number;
  improvement_pct: number;
}

interface CompareData {
  serial: CalcResult;
  parallel: CalcResult;
  comparison: Comparison;
}

interface SeedData {
  students_created: number;
  scores_created: number;
  class_name?: string;
}

function formatTime(ms: number): string {
  return `${(ms / 1000).toFixed(4)} s`;
}

function StatCard({ label, value }: Readonly<{ label: string; value: string }>) {
  return (
    <div className="bg-zinc-800 rounded-lg p-3 text-center">
      <div className="text-xs text-zinc-400 mb-1">{label}</div>
      <div className="text-lg font-semibold text-white font-mono">{value}</div>
    </div>
  );
}

function GradeBar({ grade, count, total, color }: Readonly<{ grade: string; count: number; total: number; color: string }>) {
  const pct = total > 0 ? (count / total) * 100 : 0;
  return (
    <div className="flex items-center gap-2 text-sm">
      <span className="w-6 font-bold text-white">{grade}</span>
      <div className="flex-1 bg-zinc-800 rounded-full h-5 overflow-hidden">
        <div className={`${color} h-full rounded-full transition-all duration-500`} style={{ width: `${pct}%` }} />
      </div>
      <span className="w-16 text-right text-zinc-400 font-mono text-xs">{count} ({pct.toFixed(1)}%)</span>
    </div>
  );
}

function ResultPanel({ result, color, totalMs }: Readonly<{ result: CalcResult; color: string; totalMs?: number | null }>) {
  const total = result.grade_distribution.A_90_100 + result.grade_distribution.B_80_89 +
    result.grade_distribution.C_70_79 + result.grade_distribution.D_60_69 + result.grade_distribution.F_below_60;

  return (
    <div className={`border ${color} rounded-xl p-5 bg-zinc-900/50`}>
      <div className="flex items-center justify-between mb-4">
        <h3 className="text-lg font-bold text-white uppercase tracking-wider">{result.mode}</h3>
        <span className="text-xs bg-zinc-800 text-zinc-300 px-2 py-1 rounded font-mono">
          {result.threads_used} thread{result.threads_used > 1 ? "s" : ""}
        </span>
      </div>

      <div className="mb-4 p-3 bg-zinc-800/50 rounded-lg text-center space-y-1">
        <div className="text-xs text-zinc-400 font-semibold uppercase tracking-wider">Time Breakdown</div>
        <div className="grid grid-cols-3 gap-1 text-center mt-1">
          <div>
            <div className="text-xs text-zinc-500">DB Fetch</div>
            <div className="text-sm font-bold text-yellow-400 font-mono">{formatTime(result.db_fetch_ms)}</div>
          </div>
          <div>
            <div className="text-xs text-zinc-500">Calculation</div>
            <div className="text-sm font-bold text-white font-mono">{formatTime(result.elapsed_ms)}</div>
          </div>
          <div>
            <div className="text-xs text-zinc-500">Sort</div>
            <div className="text-sm font-bold text-zinc-300 font-mono">{formatTime(result.sort_time_ms)}</div>
          </div>
        </div>
        {totalMs != null && (
          <div className="mt-2 pt-2 border-t border-zinc-700">
            <div className="text-xs text-zinc-500">Total Wall Time (client)</div>
            <div className="text-lg font-black text-emerald-400 font-mono">{formatTime(totalMs)}</div>
          </div>
        )}
      </div>

      <div className="grid grid-cols-2 gap-2 mb-4">
        <StatCard label="Mean" value={result.statistics.mean.toFixed(2)} />
        <StatCard label="Median" value={result.statistics.median.toFixed(2)} />
        <StatCard label="Std Dev" value={result.statistics.stddev.toFixed(2)} />
        <StatCard label="Variance" value={result.statistics.variance.toFixed(2)} />
        <StatCard label="Min" value={result.statistics.min.toFixed(2)} />
        <StatCard label="Max" value={result.statistics.max.toFixed(2)} />
      </div>

      <div className="space-y-1.5">
        <div className="text-xs text-zinc-400 mb-2 font-semibold uppercase tracking-wider">Grade Distribution</div>
        <GradeBar grade="A" count={result.grade_distribution.A_90_100} total={total} color="bg-emerald-500" />
        <GradeBar grade="B" count={result.grade_distribution.B_80_89} total={total} color="bg-blue-500" />
        <GradeBar grade="C" count={result.grade_distribution.C_70_79} total={total} color="bg-yellow-500" />
        <GradeBar grade="D" count={result.grade_distribution.D_60_69} total={total} color="bg-orange-500" />
        <GradeBar grade="F" count={result.grade_distribution.F_below_60} total={total} color="bg-red-500" />
      </div>
    </div>
  );
}

export default function AnalyticsPage() {
  const router = useRouter();

  const [activeTab, setActiveTab] = useState<"simple" | "correlation">("simple");
  const [seedClasses, setSeedClasses] = useState<string[]>([]);
  const [seedClass, setSeedClass]     = useState("");

  useEffect(() => {
    fetch("/api/classes", { cache: "no-store" })
      .then(r => r.json())
      .then(j => {
        const raw: unknown[] = j.data ?? [];
        const names = Array.from(new Set(
          raw
            .map(c => typeof c === "string" ? c : (c as { name: string })?.name)
            .filter((n): n is string => typeof n === "string" && n.trim() !== "")
        ));
        setSeedClasses(names);
      })
      .catch(() => { /* leave empty */ });
  }, []);

  const [loading, setLoading] = useState<string | null>(null);
  const [seedResult, setSeedResult] = useState<SeedData | null>(null);
  const [removeDupResult, setRemoveDupResult] = useState<{ students_removed: number; scores_removed: number } | null>(null);
  const [compareData, setCompareData] = useState<CompareData | null>(null);
  const [serialOnly, setSerialOnly] = useState<CalcResult | null>(null);
  const [parallelOnly, setParallelOnly] = useState<CalcResult | null>(null);
  const [serialTotalMs, setSerialTotalMs] = useState<number | null>(null);
  const [parallelTotalMs, setParallelTotalMs] = useState<number | null>(null);
  const [compareTotalMs, setCompareTotalMs] = useState<number | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [numStudents, setNumStudents] = useState(100);

  async function removeDuplicates() {
    setLoading("remove-dups");
    setError(null);
    try {
      const res = await fetch(`${API}/api/students/remove-duplicates`, { method: "DELETE" });
      const json = await res.json();
      setRemoveDupResult(json.data);
    } catch (e) {
      setError(`Remove duplicates failed: ${e}`);
    }
    setLoading(null);
  }

  async function seedData() {
    setLoading("seed");
    setError(null);
    try {
      const body: Record<string, unknown> = { num_students: numStudents };
      if (seedClass) body.class_name = seedClass;
      const res = await fetch(`${API}/api/seed`, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(body),
      });
      const json = await res.json();
      setSeedResult(json.data);
      setCompareData(null);
      setSerialOnly(null);
      setParallelOnly(null);
    } catch (e) {
      setError(`Seed failed: ${e}`);
    }
    setLoading(null);
  }

  async function runSerial() {
    setLoading("serial");
    setError(null);
    const t0 = performance.now();
    try {
      const res = await fetch(`${API}/api/calculate/serial`);
      const json = await res.json();
      setSerialOnly(json.data);
      setSerialTotalMs(performance.now() - t0);
    } catch (e) {
      setError(`Serial calc failed: ${e}`);
    }
    setLoading(null);
  }

  async function runParallel() {
    setLoading("parallel");
    setError(null);
    const t0 = performance.now();
    try {
      const res = await fetch(`${API}/api/calculate/parallel`);
      const json = await res.json();
      setParallelOnly(json.data);
      setParallelTotalMs(performance.now() - t0);
    } catch (e) {
      setError(`Parallel calc failed: ${e}`);
    }
    setLoading(null);
  }

  async function runCompare() {
    setLoading("compare");
    setError(null);
    const t0 = performance.now();
    try {
      const res = await fetch(`${API}/api/calculate/compare`);
      const json = await res.json();
      setCompareData(json.data);
      setCompareTotalMs(performance.now() - t0);
    } catch (e) {
      setError(`Comparison failed: ${e}`);
    }
    setLoading(null);
  }

  async function logout() {
    await fetch("/api/auth/logout", { method: "POST" });
    router.push("/login");
    router.refresh();
  }

  return (
    <div className="min-h-screen bg-black text-white">
      {/* ── Navbar ── */}
      <nav className="sticky top-0 z-40 w-full bg-zinc-900/80 backdrop-blur-md border-b border-zinc-800">
        <div className="max-w-6xl mx-auto px-4 py-3 flex flex-wrap items-center gap-3">
          {/* Brand */}
          <div className="flex items-center mr-4">
            <img src="/swiftscore-logo.svg" alt="SwiftScore" className="h-10 w-auto" />
          </div>

          {/* Back to Classes */}
          <button
            onClick={() => router.push("/")}
            className="flex items-center gap-1.5 bg-zinc-700 hover:bg-zinc-600 active:scale-95 px-3.5 py-2 rounded-lg text-sm font-semibold transition-all duration-150"
          >
            <svg className="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24">
              <path strokeLinecap="round" strokeLinejoin="round" strokeWidth={2} d="M15 19l-7-7 7-7" />
            </svg>
            Back to Classes
          </button>

          {/* Logout */}
          <button
            onClick={logout}
            className="flex items-center gap-1.5 text-zinc-500 hover:text-red-400 text-sm transition-colors border border-zinc-800 hover:border-red-900 px-3 py-2 rounded-lg ml-auto"
          >
            <svg className="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24">
              <path strokeLinecap="round" strokeLinejoin="round" strokeWidth={2}
                d="M17 16l4-4m0 0l-4-4m4 4H7m6 4v1a3 3 0 01-3 3H6a3 3 0 01-3-3V7a3 3 0 013-3h4a3 3 0 013 3v1" />
            </svg>
            Logout
          </button>
        </div>
      </nav>

      <div className="max-w-6xl mx-auto px-4 py-8">
        {/* Page heading */}
        <div className="text-center mb-8">
          <h1 className="text-3xl font-bold mb-2">SwiftScore</h1>
          <p className="text-zinc-400">Performance Comparison</p>
        </div>

        {/* Tabs */}
        <div className="flex gap-1 bg-zinc-900 border border-zinc-800 rounded-xl p-1 mb-8">
          <button
            onClick={() => setActiveTab("simple")}
            className={`flex-1 py-2.5 px-4 rounded-lg text-sm font-semibold transition-all duration-150 ${
              activeTab === "simple"
                ? "bg-zinc-700 text-white shadow"
                : "text-zinc-400 hover:text-zinc-200"
            }`}
          >
            Simple Calculations
          </button>
          <button
            onClick={() => setActiveTab("correlation")}
            className={`flex-1 py-2.5 px-4 rounded-lg text-sm font-semibold transition-all duration-150 ${
              activeTab === "correlation"
                ? "bg-zinc-700 text-white shadow"
                : "text-zinc-400 hover:text-zinc-200"
            }`}
          >
            Correlation Based Calculations
          </button>
        </div>

        {/* ── Simple Calculations Tab ── */}
        {activeTab === "simple" && <>

        {/* Seed Controls */}
        <div className="bg-zinc-900 border border-zinc-800 rounded-xl p-6 mb-6">
          <h2 className="text-lg font-semibold mb-4">1. Seed Database with Test Data</h2>
          <p className="text-xs text-zinc-500 mb-4">
            Each student gets one score per subject in their class. Names and emails are fetched from randomuser.me.
            {!seedClass && " If no class is selected, students are distributed round-robin across all classes."}
          </p>
          <div className="flex flex-wrap gap-4 items-end">
            <div>
              <label htmlFor="num-students" className="block text-xs text-zinc-400 mb-1">Number of Students</label>
              <input
                id="num-students"
                type="number"
                title="Number of students"
                placeholder="100"
                value={numStudents}
                onChange={(e) => setNumStudents(Number(e.target.value))}
                className="bg-zinc-800 border border-zinc-700 rounded px-3 py-2 w-28 text-white font-mono"
              />
            </div>
            <div>
              <label htmlFor="seed-class" className="block text-xs text-zinc-400 mb-1">Target Class <span className="text-zinc-600">(optional)</span></label>
              <select
                id="seed-class"
                value={seedClass}
                onChange={e => setSeedClass(e.target.value)}
                className="bg-zinc-800 border border-zinc-700 rounded-lg px-3 py-2 text-white text-sm focus:border-indigo-500 focus:outline-none w-36"
              >
                <option value="">— All classes —</option>
                {seedClasses.map(c => <option key={c} value={c}>{c}</option>)}
              </select>
            </div>
            <button
              onClick={seedData}
              disabled={loading !== null}
              className="bg-emerald-600 hover:bg-emerald-500 disabled:opacity-50 px-5 py-2 rounded font-semibold transition-colors"
            >
              {loading === "seed" ? "Seeding..." : "Seed Data"}
            </button>
            <button
              onClick={removeDuplicates}
              disabled={loading !== null}
              className="bg-red-700 hover:bg-red-600 disabled:opacity-50 px-5 py-2 rounded font-semibold transition-colors"
            >
              {loading === "remove-dups" ? "Removing..." : "Remove Duplicates"}
            </button>
          </div>
          {seedResult && (
            <div className="mt-3 text-sm text-emerald-400 font-mono">
              Seeded {seedResult.students_created} students, {seedResult.scores_created} scores
              {seedResult.class_name && <span className="text-zinc-400"> → class <span className="text-white">{seedResult.class_name}</span></span>}
            </div>
          )}
          {removeDupResult && (
            <div className="mt-3 text-sm text-red-400 font-mono">
              Removed {removeDupResult.students_removed} duplicate student{removeDupResult.students_removed !== 1 ? "s" : ""} and {removeDupResult.scores_removed} score{removeDupResult.scores_removed !== 1 ? "s" : ""}
            </div>
          )}
        </div> 

        {/* Calculation Buttons */}
        <div className="bg-zinc-900 border border-zinc-800 rounded-xl p-6 mb-6">
          <h2 className="text-lg font-semibold mb-4">Get Statistics</h2>
          <div className="flex flex-wrap gap-3">
            <button
              onClick={runSerial}
              disabled={loading !== null}
              className="bg-blue-600 hover:bg-blue-500 disabled:opacity-50 px-5 py-2 rounded font-semibold transition-colors"
            >
              {loading === "serial" ? "Running..." : "Run Serial"}
            </button>
            <button
              onClick={runParallel}
              disabled={loading !== null}
              className="bg-purple-600 hover:bg-purple-500 disabled:opacity-50 px-5 py-2 rounded font-semibold transition-colors"
            >
              {loading === "parallel" ? "Running..." : "Run Parallel (OpenMP)"}
            </button>
            <button
              onClick={runCompare}
              disabled={loading !== null}
              className="bg-amber-600 hover:bg-amber-500 disabled:opacity-50 px-6 py-2 rounded font-bold transition-colors text-lg"
            >
              {loading === "compare" ? "Comparing..." : "Compare Both"}
            </button>
          </div>
        </div>

        {error && (
          <div className="bg-red-900/30 border border-red-800 rounded-xl p-4 mb-6 text-red-400">
            {error}
          </div>
        )}

        {/* Individual Results */}
        {(serialOnly || parallelOnly) && !compareData && (
          <div className="grid md:grid-cols-2 gap-6 mb-6">
            {serialOnly && <ResultPanel result={serialOnly} color="border-blue-700" totalMs={serialTotalMs} />}
            {parallelOnly && <ResultPanel result={parallelOnly} color="border-purple-700" totalMs={parallelTotalMs} />}
          </div>
        )}

        {/* Comparison Results */}
        {compareData && (
          <>
            {/* Speedup Banner */}
            <div className="bg-gradient-to-r from-amber-900/40 to-amber-800/20 border border-amber-700 rounded-xl p-6 mb-6 text-center">
              <div className="text-sm text-amber-400 mb-1 uppercase tracking-widest font-semibold">Performance Comparison</div>
              <div className="grid grid-cols-3 gap-4 mt-4">
                <div>
                  <div className="text-xs text-zinc-400">Serial Time</div>
                  <div className="text-2xl font-bold text-blue-400 font-mono">
                    {formatTime(compareData.comparison.serial_time_ms)}
                  </div>
                  <div className="text-xs text-zinc-500">{compareData.comparison.serial_threads} thread</div>
                </div>
                <div>
                  <div className="text-xs text-zinc-400">Speedup</div>
                  <div className={`text-4xl font-black font-mono ${compareData.comparison.speedup >= 1 ? "text-emerald-400" : "text-red-400"}`}>
                    {compareData.comparison.speedup.toFixed(2)}x
                  </div>
                  <div className="text-xs text-zinc-500">
                    {compareData.comparison.improvement_pct.toFixed(1)}% faster
                  </div>
                </div>
                <div>
                  <div className="text-xs text-zinc-400">Parallel Time</div>
                  <div className="text-2xl font-bold text-purple-400 font-mono">
                    {formatTime(compareData.comparison.parallel_time_ms)}
                  </div>
                  <div className="text-xs text-zinc-500">{compareData.comparison.parallel_threads} threads</div>
                </div>
              </div>
              <div className="mt-4 pt-3 border-t border-amber-800/50 grid grid-cols-3 gap-2 text-center">
                <div>
                  <div className="text-xs text-zinc-500">DB Fetch</div>
                  <div className="text-sm font-bold text-yellow-400 font-mono">{formatTime(compareData.comparison.db_fetch_ms)}</div>
                </div>
                <div>
                  <div className="text-xs text-zinc-500">Data Size</div>
                  <div className="text-sm font-bold text-zinc-300 font-mono">{compareData.comparison.data_size.toLocaleString()} scores</div>
                </div>
                <div>
                  <div className="text-xs text-zinc-500">Total Wall Time</div>
                  <div className="text-sm font-bold text-emerald-400 font-mono">{compareTotalMs === null ? "—" : formatTime(compareTotalMs)}</div>
                </div>
              </div>
            </div>

            {/* Side-by-side panels */}
            <div className="grid md:grid-cols-2 gap-6">
              <ResultPanel result={compareData.serial} color="border-blue-700" />
              <ResultPanel result={compareData.parallel} color="border-purple-700" />
            </div>
          </>
        )}

        </>}

        {/* ── Correlation Based Calculations Tab ── */}
        {activeTab === "correlation" && <CorrelationTab />}

      </div>
    </div>
  );
}
