"use client";

import { useState, useEffect } from "react";
import { ResultPanel } from "./page";

const API = process.env.NEXT_PUBLIC_API_URL || "http://localhost:8090";

interface CalcResult {
  mode: string;
  threads_used: number;
  scores_count: number;
  elapsed_ms: number;
  db_fetch_ms: number;
  statistics: {
    mean: number;
    median: number;
    min: number;
    max: number;
    stddev: number;
    variance: number;
  };
  grade_distribution: Record<string, number>;
}

interface SubjectItem {
  name: string;
  class_name: string;
}

interface Comparison {
  serial_time_ms: number;
  parallel_time_ms: number;
  pthread_time_ms?: number;
  mpi_time_ms?: number;
  db_fetch_ms: number;
  serial_db_fetch_ms?: number;
  parallel_db_fetch_ms?: number;
  pthread_db_fetch_ms?: number;
  mpi_db_fetch_ms?: number;
  speedup: number;
  speedup_pthread?: number;
  speedup_mpi?: number;
  serial_threads: number;
  parallel_threads: number;
  pthread_threads?: number;
  mpi_threads?: number;
  data_size: number;
  improvement_pct: number;
}

interface CompareData {
  serial: CalcResult;
  parallel: CalcResult;
  pthread?: CalcResult;
  mpi?: CalcResult | null;
  comparison: Comparison;
  mpi_error?: string;
}

function isCompareData(value: CalcResult | CompareData | undefined): value is CompareData {
  return !!value && typeof value === "object" && "comparison" in value;
}

export default function ClassAnalysisTab() {
  const [classes, setClasses] = useState<string[]>([]);
  const [selectedClass, setSelectedClass] = useState("");
  const [subjects, setSubjects] = useState<string[]>([]);
  
  const [loading, setLoading] = useState<"serial" | "parallel" | "pthread" | "mpi" | "compare" | "compare_shared" | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [openmpThreads, setOpenmpThreads] = useState(4);
  const [pthreadThreads, setPthreadThreads] = useState(4);
  const [mpiProcesses, setMpiProcesses] = useState(2);

  // Store results mapped by subject name
  const [results, setResults] = useState<Record<string, CalcResult | CompareData>>({});

  const compareItems: CompareData[] = subjects
    .map((subject) => results[subject])
    .filter(isCompareData);

  const overallCompare = compareItems.length > 0 ? (() => {
    const totals = compareItems.reduce(
      (acc, item) => {
        const c = item.comparison;
        acc.serial_ms += c.serial_time_ms ?? 0;
        acc.parallel_ms += c.parallel_time_ms ?? 0;
        acc.pthread_ms += c.pthread_time_ms ?? 0;
        acc.mpi_ms += c.mpi_time_ms ?? 0;
        acc.serial_db_fetch_ms += c.serial_db_fetch_ms ?? c.db_fetch_ms ?? 0;
        acc.parallel_db_fetch_ms += c.parallel_db_fetch_ms ?? c.db_fetch_ms ?? 0;
        acc.pthread_db_fetch_ms += c.pthread_db_fetch_ms ?? c.db_fetch_ms ?? 0;
        acc.mpi_db_fetch_ms += c.mpi_db_fetch_ms ?? c.db_fetch_ms ?? 0;
        acc.data_size += c.data_size ?? 0;
        acc.mpi_ok = acc.mpi_ok && !!c.mpi_time_ms && c.mpi_time_ms > 0;
        acc.parallel_threads = Math.max(acc.parallel_threads, c.parallel_threads ?? 0);
        acc.pthread_threads = Math.max(acc.pthread_threads, c.pthread_threads ?? 0);
        acc.mpi_threads = Math.max(acc.mpi_threads, c.mpi_threads ?? 0);
        return acc;
      },
      {
        serial_ms: 0,
        parallel_ms: 0,
        pthread_ms: 0,
        mpi_ms: 0,
        serial_db_fetch_ms: 0,
        parallel_db_fetch_ms: 0,
        pthread_db_fetch_ms: 0,
        mpi_db_fetch_ms: 0,
        data_size: 0,
        mpi_ok: true,
        parallel_threads: 0,
        pthread_threads: 0,
        mpi_threads: 0,
      }
    );

    const speedup_openmp = totals.parallel_ms > 0 ? totals.serial_ms / totals.parallel_ms : 0;
    const speedup_pthread = totals.pthread_ms > 0 ? totals.serial_ms / totals.pthread_ms : 0;
    const speedup_mpi = totals.mpi_ok && totals.mpi_ms > 0 ? totals.serial_ms / totals.mpi_ms : 0;

    return {
      totals,
      speedup_openmp,
      speedup_pthread,
      speedup_mpi,
      subject_count: compareItems.length,
    };
  })() : null;

  async function runCompareAllSeparate() {
    if (!selectedClass || subjects.length === 0) return;
    setLoading("compare");
    setError(null);
    setResults({});

    const newResults: Record<string, CompareData> = {};
    try {
      // Run sequentially to avoid calc_lock contention.
      for (const subject of subjects) {
        const url = `${API}/api/calculate/serial?class=${encodeURIComponent(selectedClass)}&subject=${encodeURIComponent(subject)}`;
        const res = await fetch(url);
        const json = await res.json();
        if (!res.ok) throw new Error(json.message || `Serial failed for ${subject}`);
        newResults[subject] = {
          serial: json.data,
          parallel: json.data,
          comparison: {
            serial_time_ms: 0,
            parallel_time_ms: 0,
            db_fetch_ms: 0,
            speedup: 0,
            serial_threads: 1,
            parallel_threads: 1,
            data_size: 0,
            improvement_pct: 0,
          },
        };
      }

      for (const subject of subjects) {
        const url = `${API}/api/calculate/parallel?class=${encodeURIComponent(selectedClass)}&subject=${encodeURIComponent(subject)}&threads=${openmpThreads}`;
        const res = await fetch(url);
        const json = await res.json();
        if (!res.ok) throw new Error(json.message || `OpenMP failed for ${subject}`);
        newResults[subject].parallel = json.data;
      }

      for (const subject of subjects) {
        const url = `${API}/api/calculate/pthread?class=${encodeURIComponent(selectedClass)}&subject=${encodeURIComponent(subject)}&threads=${pthreadThreads}`;
        const res = await fetch(url);
        const json = await res.json();
        if (res.ok) {
          newResults[subject].pthread = json.data;
        }
      }

      for (const subject of subjects) {
        const url = `${API}/api/calculate/mpi?class=${encodeURIComponent(selectedClass)}&subject=${encodeURIComponent(subject)}&mpi_processes=${mpiProcesses}`;
        const res = await fetch(url);
        const json = await res.json();
        if (res.ok && json?.data) {
          newResults[subject].mpi = json.data;
        } else if (json?.message) {
          newResults[subject].mpi = null;
          newResults[subject].mpi_error = json.message;
        }
      }

      // Build comparison blocks (per-subject) from the fetched method results.
      for (const subject of subjects) {
        const item = newResults[subject];
        const s = item.serial;
        const p = item.parallel;
        const pt = item.pthread;
        const mpi = item.mpi ?? undefined;

        const speedup = p?.elapsed_ms > 0 ? s.elapsed_ms / p.elapsed_ms : 0;
        const speedup_pthread = pt && pt.elapsed_ms > 0 ? s.elapsed_ms / pt.elapsed_ms : 0;
        const speedup_mpi = mpi && mpi.elapsed_ms > 0 ? s.elapsed_ms / mpi.elapsed_ms : 0;

        item.comparison = {
          serial_time_ms: s.elapsed_ms,
          parallel_time_ms: p.elapsed_ms,
          pthread_time_ms: pt?.elapsed_ms ?? 0,
          mpi_time_ms: mpi?.elapsed_ms ?? 0,
          db_fetch_ms: 0,
          serial_db_fetch_ms: s.db_fetch_ms,
          parallel_db_fetch_ms: p.db_fetch_ms,
          pthread_db_fetch_ms: pt?.db_fetch_ms ?? 0,
          mpi_db_fetch_ms: mpi?.db_fetch_ms ?? 0,
          speedup,
          speedup_pthread,
          speedup_mpi,
          serial_threads: s.threads_used,
          parallel_threads: p.threads_used,
          pthread_threads: pt?.threads_used ?? 0,
          mpi_threads: mpi?.threads_used ?? 0,
          data_size: s.scores_count,
          improvement_pct: speedup > 1 ? (speedup - 1) * 100 : 0,
        };
      }

      setResults(newResults);
    } catch (e: any) {
      setError(e.message || "An error occurred during comparison");
    } finally {
      setLoading(null);
    }
  }

  async function runCompareAllShared() {
    if (!selectedClass || subjects.length === 0) return;
    setLoading("compare_shared");
    setError(null);
    setResults({});

    try {
      const url = `/api/calculate/class/compare?class=${encodeURIComponent(selectedClass)}&threads=${openmpThreads}&pthread_threads=${pthreadThreads}&mpi_processes=${mpiProcesses}`;
      const res = await fetch(url);
      const json = await res.json();
      if (!res.ok || json.error) {
        throw new Error(json.message || "Failed to compare all subjects");
      }

      const newResults: Record<string, CompareData> = {};
      json.data.subjects.forEach((item: any) => {
        const c = item.comparison as Comparison;
        newResults[item.subject] = {
          serial: item.serial,
          parallel: item.parallel,
          pthread: item.pthread,
          mpi: item.mpi,
          comparison: {
            ...c,
            serial_db_fetch_ms: c.db_fetch_ms,
            parallel_db_fetch_ms: c.db_fetch_ms,
            pthread_db_fetch_ms: c.db_fetch_ms,
            mpi_db_fetch_ms: c.db_fetch_ms,
          },
        };
        if (item.mpi_error) {
          (newResults[item.subject] as CompareData).mpi_error = item.mpi_error;
        }
      });

      setResults(newResults);
    } catch (e: any) {
      setError(e.message || "An error occurred during calculation");
    } finally {
      setLoading(null);
    }
  }

  // 1. Fetch classes on mount
  useEffect(() => {
    fetch(`${API}/api/classes`)
      .then((res) => res.json())
      .then((json) => {
        if (json.data && json.data.length > 0) {
          const names: string[] = json.data.map((c: unknown) =>
            typeof c === "string" ? c : (c as { name: string }).name
          ).filter(Boolean);
          setClasses(names);
          setSelectedClass(names[0] ?? "");
        }
      })
      .catch((err) => console.error("Failed to load classes:", err));
  }, []);

  // 2. Fetch subjects when selectedClass changes
  useEffect(() => {
    if (!selectedClass) return;
    setSubjects([]);
    setResults({});
    fetch(`${API}/api/subjects?class=${encodeURIComponent(selectedClass)}`)
      .then((res) => res.json())
      .then((json) => {
        if (json.data) {
          const names: string[] = (json.data as (string | SubjectItem)[]).map((s) =>
            typeof s === "string" ? s : s.name
          ).filter(Boolean);
          setSubjects(names);
        }
      })
      .catch((err) => console.error("Failed to load subjects:", err));
  }, [selectedClass]);

  const runCalculation = async (mode: "serial" | "parallel" | "pthread" | "mpi" | "compare") => {
    if (!selectedClass || subjects.length === 0) return;

    setLoading(mode);
    setError(null);
    setResults({}); // clear old results

    try {
      if (mode !== "compare") {
        const threadParam = mode === "parallel" ? openmpThreads
          : mode === "pthread" ? pthreadThreads
          : null;

        const promises = subjects.map(async (subject) => {
          let url = `${API}/api/calculate/${mode}?class=${encodeURIComponent(selectedClass)}&subject=${encodeURIComponent(subject)}`;
          if (threadParam !== null) url += `&threads=${threadParam}`;
          if (mode === "mpi") url += `&mpi_processes=${mpiProcesses}`;
          const res = await fetch(url);
          const json = await res.json();
          if (!res.ok) throw new Error(json.message || `Failed for ${subject}`);
          return { subject, data: json.data };
        });

        const completed = await Promise.all(promises);

        const newResults: Record<string, CalcResult | CompareData> = {};
        completed.forEach((item) => {
          newResults[item.subject] = item.data;
        });

        setResults(newResults);
      }
    } catch (e: any) {
      setError(e.message || "An error occurred during calculation");
    } finally {
      setLoading(null);
    }
  };

  const canRun = selectedClass !== "" && subjects.length > 0 && loading === null;

  return (
    <div className="space-y-8 animate-in fade-in slide-in-from-bottom-4 duration-500">
      <div className="bg-zinc-800/50 p-6 rounded-2xl border border-zinc-700/50">
        <h2 className="text-xl font-bold text-white mb-6 flex items-center">
         
          1. Select Class
        </h2>

        <div className="max-w-md">
          <label className="block text-sm font-medium text-zinc-400 mb-2">Target Class</label>
          <select
            className="w-full bg-zinc-900 border border-zinc-700 text-white rounded-lg p-3 outline-none focus:border-blue-500 transition-colors"
            value={selectedClass}
            onChange={(e) => setSelectedClass(e.target.value)}
          >
            {classes.map((c) => (
              <option key={c} value={c}>{c}</option>
            ))}
          </select>
          
          <div className="mt-2 text-xs text-zinc-500">
            {subjects.length} subject{subjects.length !== 1 ? "s" : ""} found for this class.
          </div>
        </div>
      </div>

      <div className="bg-zinc-800/50 p-6 rounded-2xl border border-zinc-700/50">
        <h2 className="text-xl font-bold text-white mb-6 flex items-center">
      
          2. Calculate Subject Statistics
        </h2>

        <div className="space-y-3">
          {/* Serial & MPI */}
          <div className="flex flex-wrap gap-3 items-end">
            <button
              onClick={() => runCalculation("serial")}
              disabled={!canRun}
              className="bg-blue-600 hover:bg-blue-500 disabled:opacity-50 px-6 py-2 rounded font-semibold transition-colors text-white"
            >
              {loading === "serial" ? "Running..." : "Run Serial"}
            </button>
            <div>
              <label className="block text-xs text-zinc-400 mb-1">MPI Processes</label>
              <input
                type="number"
                min={1}
                max={64}
                value={mpiProcesses}
                onChange={e => setMpiProcesses(Math.max(1, parseInt(e.target.value) || 1))}
                disabled={!canRun}
                className="bg-zinc-900 border border-zinc-700 rounded px-3 py-2 w-20 text-white font-mono disabled:opacity-50"
              />
            </div>
            <button
              onClick={() => runCalculation("mpi")}
              disabled={!canRun}
              className="bg-green-600 hover:bg-green-500 disabled:opacity-50 px-6 py-2 rounded font-semibold transition-colors text-white"
            >
              {loading === "mpi" ? "Running..." : "Run MPI (Distributed)"}
            </button>
          </div>

          {/* Parallel methods with thread count inputs */}
          <div className="flex flex-wrap gap-4 items-end">
            <div>
              <label className="block text-xs text-zinc-400 mb-1">OpenMP Threads</label>
              <input
                type="number"
                min={1}
                max={256}
                value={openmpThreads}
                onChange={e => setOpenmpThreads(Math.max(1, parseInt(e.target.value) || 1))}
                disabled={!canRun}
                className="bg-zinc-900 border border-zinc-700 rounded px-3 py-2 w-20 text-white font-mono disabled:opacity-50"
              />
            </div>
            <button
              onClick={() => runCalculation("parallel")}
              disabled={!canRun}
              className="bg-purple-600 hover:bg-purple-500 disabled:opacity-50 px-6 py-2 rounded font-semibold transition-colors text-white"
            >
              {loading === "parallel" ? "Running..." : "Run Parallel (OpenMP)"}
            </button>

            <div>
              <label className="block text-xs text-zinc-400 mb-1">Pthreads</label>
              <input
                type="number"
                min={1}
                max={256}
                value={pthreadThreads}
                onChange={e => setPthreadThreads(Math.max(1, parseInt(e.target.value) || 1))}
                disabled={!canRun}
                className="bg-zinc-900 border border-zinc-700 rounded px-3 py-2 w-20 text-white font-mono disabled:opacity-50"
              />
            </div>
            <button
              onClick={() => runCalculation("pthread")}
              disabled={!canRun}
              className="bg-cyan-600 hover:bg-cyan-500 disabled:opacity-50 px-6 py-2 rounded font-semibold transition-colors text-white"
            >
              {loading === "pthread" ? "Running..." : "Run Pthreads"}
            </button>
          </div>

          {/* Compare All */}
          <div className="flex flex-wrap gap-4 items-end pt-1 border-t border-zinc-700/50">
            <button
              onClick={runCompareAllSeparate}
              disabled={!canRun}
              className="bg-amber-600 hover:bg-amber-500 disabled:opacity-50 px-6 py-2 rounded font-semibold transition-colors text-white"
            >
              {loading === "compare" ? "Running..." : "Compare All"}
            </button>
            <button
              onClick={runCompareAllShared}
              disabled={!canRun}
              className="bg-emerald-700 hover:bg-emerald-600 disabled:opacity-50 px-6 py-2 rounded font-semibold transition-colors text-white"
            >
              {loading === "compare_shared" ? "Running..." : "Compare All (Fast)"}
            </button>
            <span className="text-xs text-zinc-500 self-end pb-2">Uses thread counts from above · MPI Processes applies to MPI</span>
          </div>
        </div>

        {loading && (
          <p className="mt-4 text-sm text-zinc-400 animate-pulse">
            Running {loading === "compare" ? "full comparison" : `${loading} calculation`} across {subjects.length} subjects…
          </p>
        )}

        {error && (
          <div className="mt-4 p-4 bg-red-900/30 border border-red-500/50 text-red-400 rounded-lg">
            {error}
          </div>
        )}
      </div>

      {/* Results Grid */}
      {Object.keys(results).length > 0 && (
        <div className="mt-8">
          {overallCompare && (
            <div className="bg-linear-to-r from-amber-900/40 to-amber-800/20 border border-amber-700 rounded-xl p-6 mb-6">
              <div className="text-sm text-amber-400 mb-1 uppercase tracking-widest font-semibold text-center">Performance Comparison</div>
              <div className="text-xs text-zinc-500 text-center mb-4">
                <span className="text-white font-semibold font-mono">{overallCompare.subject_count}</span> subject{overallCompare.subject_count !== 1 ? "s" : ""} analysed ·
                <span className="text-white font-semibold font-mono ml-1">{overallCompare.totals.data_size}</span> total scores
              </div>

              <div className="grid grid-cols-2 md:grid-cols-4 gap-3 mb-4">
                <div className="bg-zinc-800/50 rounded-xl p-3 text-center">
                  <div className="text-xs text-blue-400 font-semibold mb-2">Serial</div>
                  <div className="text-xs text-zinc-400">Calc</div>
                  <div className="text-xl font-bold text-blue-400 font-mono">{overallCompare.totals.serial_ms.toFixed(3)} ms</div>
                  <div className="mt-1 text-xs text-yellow-400/80">DB Fetch</div>
                  <div className="text-base font-semibold text-yellow-400 font-mono">{overallCompare.totals.serial_db_fetch_ms.toFixed(3)} ms</div>
                  <div className="mt-1 text-xs text-zinc-500">1 thread</div>
                </div>

                <div className="bg-zinc-800/50 rounded-xl p-3 text-center">
                  <div className="text-xs text-purple-400 font-semibold mb-2">Parallel (OpenMP)</div>
                  <div className="text-xs text-zinc-400">Calc</div>
                  <div className="text-xl font-bold text-purple-400 font-mono">{overallCompare.totals.parallel_ms.toFixed(3)} ms</div>
                  <div className="mt-1 text-xs text-yellow-400/80">DB Fetch</div>
                  <div className="text-base font-semibold text-yellow-400 font-mono">{overallCompare.totals.parallel_db_fetch_ms.toFixed(3)} ms</div>
                  <div className="mt-1 text-xs text-zinc-500">{overallCompare.totals.parallel_threads} threads</div>
                </div>

                {overallCompare.totals.pthread_ms > 0 && (
                  <div className="bg-zinc-800/50 rounded-xl p-3 text-center">
                    <div className="text-xs text-cyan-400 font-semibold mb-2">POSIX Threads</div>
                    <div className="text-xs text-zinc-400">Calc</div>
                    <div className="text-xl font-bold text-cyan-400 font-mono">{overallCompare.totals.pthread_ms.toFixed(3)} ms</div>
                    <div className="mt-1 text-xs text-yellow-400/80">DB Fetch</div>
                    <div className="text-base font-semibold text-yellow-400 font-mono">{overallCompare.totals.pthread_db_fetch_ms.toFixed(3)} ms</div>
                    <div className="mt-1 text-xs text-zinc-500">{overallCompare.totals.pthread_threads} threads</div>
                  </div>
                )}

                {overallCompare.speedup_mpi > 0 && (
                  <div className="bg-zinc-800/50 rounded-xl p-3 text-center">
                    <div className="text-xs text-green-400 font-semibold mb-2">MPI Distributed</div>
                    <div className="text-xs text-zinc-400">Calc</div>
                    <div className="text-xl font-bold text-green-400 font-mono">{overallCompare.totals.mpi_ms.toFixed(3)} ms</div>
                    <div className="mt-1 text-xs text-yellow-400/80">DB Fetch</div>
                    <div className="text-base font-semibold text-yellow-400 font-mono">{overallCompare.totals.mpi_db_fetch_ms.toFixed(3)} ms</div>
                    <div className="mt-1 text-xs text-zinc-500">{overallCompare.totals.mpi_threads} processes</div>
                  </div>
                )}
              </div>

              <div className="grid grid-cols-1 sm:grid-cols-3 gap-4 pt-3 border-t border-amber-800/50 text-center">
                <div>
                  <div className="text-xs text-zinc-400">Speedup (OpenMP)</div>
                  <div className={`text-4xl font-black font-mono ${overallCompare.speedup_openmp >= 1 ? "text-emerald-400" : "text-red-400"}`}>{overallCompare.speedup_openmp.toFixed(2)}x</div>
                </div>
                <div>
                  <div className="text-xs text-zinc-400">Speedup (Pthreads)</div>
                  <div className={`text-4xl font-black font-mono ${overallCompare.speedup_pthread >= 1 ? "text-cyan-400" : "text-red-400"}`}>{overallCompare.speedup_pthread.toFixed(2)}x</div>
                </div>
                {overallCompare.speedup_mpi > 0 && (
                  <div>
                    <div className="text-xs text-zinc-400">Speedup (MPI)</div>
                    <div className={`text-4xl font-black font-mono ${overallCompare.speedup_mpi >= 1 ? "text-green-400" : "text-red-400"}`}>{overallCompare.speedup_mpi.toFixed(2)}x</div>
                  </div>
                )}
              </div>
            </div>
          )}

          <h3 className="text-2xl font-bold text-white mb-6">Subject Breakdown</h3>
          <div className="space-y-6">
            {subjects.map((subject) => {
              const res = results[subject];
              if (!res) return null;

              const isCompare = isCompareData(res);

              if (isCompare) {
                const compareData = res;
                const methodPanels: { key: string; title: string; color: string; data: CalcResult }[] = [
                  { key: "serial",   title: "Serial",             color: "border-blue-700",   data: { ...compareData.serial,   mode: "Serial" } },
                  { key: "parallel", title: "Parallel (OpenMP)",  color: "border-purple-700", data: { ...compareData.parallel, mode: "Parallel (OpenMP)" } },
                ];
                if (compareData.pthread) {
                  methodPanels.push({ key: "pthread", title: "POSIX Threads (pthreads)", color: "border-cyan-700", data: { ...compareData.pthread, mode: "POSIX Threads (pthreads)" } });
                }
                if (compareData.mpi) {
                  methodPanels.push({ key: "mpi", title: "MPI Distributed", color: "border-green-700", data: { ...compareData.mpi, mode: "MPI Distributed" } });
                }

                return (
                  <div key={subject} className="bg-zinc-900/40 border border-zinc-800 rounded-xl p-5">
                    <div className="flex items-center justify-between mb-4">
                      <h4 className="text-lg font-bold text-white truncate">{subject}</h4>
                      <span className="text-xs bg-amber-500/20 text-amber-400 px-2 py-1 rounded font-semibold uppercase tracking-wider">Compared</span>
                    </div>

                    <div className="grid md:grid-cols-2 lg:grid-cols-3 gap-6">
                      {methodPanels.map((p) => (
                        <ResultPanel key={p.key} result={p.data} color={p.color} hideRounding={true} hideTiming={true} />
                      ))}
                    </div>
                  </div>
                );
              }

              const single = res as CalcResult;
              return (
                <div key={subject} className="bg-zinc-900/40 border border-zinc-800 rounded-xl p-5">
                  <div className="flex items-center justify-between mb-4">
                    <h4 className="text-lg font-bold text-white truncate">{subject}</h4>
                    <span className="text-xs bg-zinc-800 text-zinc-300 px-2 py-1 rounded font-mono">{single.mode}</span>
                  </div>

                  <div className="max-w-xl">
                    <ResultPanel result={single} color="border-zinc-700" hideRounding={true} />
                  </div>
                </div>
              );
            })}
          </div>
        </div>
      )}
    </div>
  );
}
