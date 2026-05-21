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

export default function ClassAnalysisTab() {
  const [classes, setClasses] = useState<string[]>([]);
  const [selectedClass, setSelectedClass] = useState("");
  const [subjects, setSubjects] = useState<string[]>([]);
  
  const [loading, setLoading] = useState<"serial" | "parallel" | "pthread" | "mpi" | "compare" | null>(null);
  const [error, setError] = useState<string | null>(null);
  
  // Store results mapped by subject name
  const [results, setResults] = useState<Record<string, CalcResult | CompareData>>({});

  // Store selected mode for each subject card (when compared)
  const [selectedModes, setSelectedModes] = useState<Record<string, "serial" | "parallel" | "pthread" | "mpi">>({});

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
    setSelectedModes({});
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
    setSelectedModes({});

    try {
      if (mode === "compare") {
        // Option A: Single backend request for Compare All
        const res = await fetch(`/api/calculate/class/compare?class=${encodeURIComponent(selectedClass)}`);
        const json = await res.json();
        
        if (!res.ok || json.error) {
          throw new Error(json.message || "Failed to compare all subjects");
        }
        
        const newResults: Record<string, CompareData> = {};
        json.data.subjects.forEach((item: any) => {
          newResults[item.subject] = {
            serial: item.serial,
            parallel: item.parallel,
            pthread: item.pthread,
            mpi: item.mpi, // Can be null if MPI is down, handled by UI
            comparison: item.comparison
          };
          // Preserve any per-subject MPI errors if needed by the UI
          if (item.mpi_error) {
            (newResults[item.subject] as any).mpi_error = item.mpi_error;
          }
        });
        
        setResults(newResults);
      } else {
        // Individual sequential calls for specific modes
        const promises = subjects.map(async (subject) => {
          const url = `${API}/api/calculate/${mode}?class=${encodeURIComponent(selectedClass)}&subject=${encodeURIComponent(subject)}`;
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
          <span className="bg-blue-500/20 text-blue-400 p-2 rounded-lg mr-3">
            <svg className="w-5 h-5" fill="none" viewBox="0 0 24 24" stroke="currentColor">
              <path strokeLinecap="round" strokeLinejoin="round" strokeWidth={2} d="M19 21V5a2 2 0 00-2-2H7a2 2 0 00-2 2v16m14 0h2m-2 0h-5m-9 0H3m2 0h5M9 7h1m-1 4h1m4-4h1m-1 4h1m-5 10v-5a1 1 0 011-1h2a1 1 0 011 1v5m-4 0h4" />
            </svg>
          </span>
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
          <span className="bg-purple-500/20 text-purple-400 p-2 rounded-lg mr-3">
            <svg className="w-5 h-5" fill="none" viewBox="0 0 24 24" stroke="currentColor">
              <path strokeLinecap="round" strokeLinejoin="round" strokeWidth={2} d="M13 10V3L4 14h7v7l9-11h-7z" />
            </svg>
          </span>
          2. Calculate Subject Statistics
        </h2>

        <div className="flex flex-wrap gap-4">
          <button
            onClick={() => runCalculation("serial")}
            disabled={!canRun}
            className="bg-blue-600 hover:bg-blue-500 disabled:opacity-50 px-6 py-2 rounded font-semibold transition-colors text-white"
          >
            {loading === "serial" ? "Running..." : "Run Serial"}
          </button>
          <button
            onClick={() => runCalculation("parallel")}
            disabled={!canRun}
            className="bg-purple-600 hover:bg-purple-500 disabled:opacity-50 px-6 py-2 rounded font-semibold transition-colors text-white"
          >
            {loading === "parallel" ? "Running..." : "Run Parallel (OpenMP)"}
          </button>
          <button
            onClick={() => runCalculation("pthread")}
            disabled={!canRun}
            className="bg-cyan-600 hover:bg-cyan-500 disabled:opacity-50 px-6 py-2 rounded font-semibold transition-colors text-white"
          >
            {loading === "pthread" ? "Running..." : "Run Pthreads"}
          </button>
          <button
            onClick={() => runCalculation("mpi")}
            disabled={!canRun}
            className="bg-green-600 hover:bg-green-500 disabled:opacity-50 px-6 py-2 rounded font-semibold transition-colors text-white"
          >
            {loading === "mpi" ? "Running..." : "Run MPI (Distributed)"}
          </button>
          <button
            onClick={() => runCalculation("compare")}
            disabled={!canRun}
            className="bg-amber-600 hover:bg-amber-500 disabled:opacity-50 px-6 py-2 rounded font-semibold transition-colors text-white"
          >
            {loading === "compare" ? "Running..." : "Compare All"}
          </button>
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
          <h3 className="text-2xl font-bold text-white mb-6">Subject Breakdown</h3>
          <div className="grid md:grid-cols-2 lg:grid-cols-3 gap-6">
            {subjects.map((subject) => {
              const res = results[subject];
              if (!res) return null;
              
              const isCompare = "comparison" in res;
              const activeMode = isCompare ? (selectedModes[subject] ?? "serial") : (res as CalcResult).mode;
              
              let calcRes: CalcResult;
              if (isCompare) {
                const compareData = res as CompareData;
                if (activeMode === "parallel") {
                  calcRes = compareData.parallel;
                } else if (activeMode === "pthread" && compareData.pthread) {
                  calcRes = compareData.pthread;
                } else if (activeMode === "mpi" && compareData.mpi) {
                  calcRes = compareData.mpi;
                } else {
                  calcRes = compareData.serial;
                }
              } else {
                calcRes = res as CalcResult;
              }
              
              let borderColor = "border-blue-700";
              if (activeMode === "parallel") borderColor = "border-purple-700";
              else if (activeMode === "pthread") borderColor = "border-cyan-700";
              else if (activeMode === "mpi") borderColor = "border-green-700";

              return (
                <div key={subject} className="flex flex-col bg-zinc-900 rounded-xl overflow-hidden shadow-lg border border-zinc-800">
                  <div className={`bg-zinc-800 text-white font-bold py-3 px-4 flex justify-between items-center border-b ${borderColor}`}>
                    <span>{subject}</span>
                    {isCompare && (
                      <span className="text-xs bg-amber-500/20 text-amber-400 px-2 py-0.5 rounded font-semibold uppercase tracking-wider">
                        Compared
                      </span>
                    )}
                  </div>
                  
                  {isCompare && (
                    <div className="bg-zinc-950 p-4 border-b border-zinc-800 text-xs space-y-2">
                      <div className="flex justify-between items-center text-zinc-400">
                        <span>Serial:</span>
                        <span className="font-mono text-blue-400">
                          {((res as CompareData).comparison.serial_time_ms / 1000).toFixed(4)} s
                        </span>
                      </div>
                      <div className="flex justify-between items-center text-zinc-400">
                        <span>Parallel (OpenMP):</span>
                        <div className="text-right">
                          <span className="font-mono text-purple-400">
                            {((res as CompareData).comparison.parallel_time_ms / 1000).toFixed(4)} s
                          </span>
                          <span className="ml-1.5 text-emerald-400 font-bold">
                            ({(res as CompareData).comparison.speedup.toFixed(2)}x)
                          </span>
                        </div>
                      </div>
                      {(res as CompareData).comparison.pthread_time_ms !== undefined && (res as CompareData).comparison.pthread_time_ms! > 0 && (
                        <div className="flex justify-between items-center text-zinc-400">
                          <span>POSIX Threads (pthreads):</span>
                          <div className="text-right">
                            <span className="font-mono text-cyan-400">
                              {((res as CompareData).comparison.pthread_time_ms! / 1000).toFixed(4)} s
                            </span>
                            {(res as CompareData).comparison.speedup_pthread !== undefined && (res as CompareData).comparison.speedup_pthread! > 0 && (
                              <span className="ml-1.5 text-cyan-400 font-bold">
                                ({(res as CompareData).comparison.speedup_pthread!.toFixed(2)}x)
                              </span>
                            )}
                          </div>
                        </div>
                      )}
                      {(res as CompareData).comparison.mpi_time_ms !== undefined && (res as CompareData).comparison.mpi_time_ms! > 0 ? (
                        <div className="flex justify-between items-center text-zinc-400">
                          <span>MPI (Distributed):</span>
                          <div className="text-right">
                            <span className="font-mono text-green-400">
                              {((res as CompareData).comparison.mpi_time_ms! / 1000).toFixed(4)} s
                            </span>
                            {(res as CompareData).comparison.speedup_mpi !== undefined && (res as CompareData).comparison.speedup_mpi! > 0 && (
                              <span className="ml-1.5 text-green-400 font-bold">
                                ({(res as CompareData).comparison.speedup_mpi!.toFixed(2)}x)
                              </span>
                            )}
                          </div>
                        </div>
                      ) : (res as CompareData).mpi_error ? (
                        <div className="flex justify-between items-center text-zinc-400">
                          <span>MPI (Distributed):</span>
                          <span className="text-red-400 font-mono text-[10px] uppercase truncate max-w-[150px] title={(res as CompareData).mpi_error}">
                            Failed: {(res as CompareData).mpi_error}
                          </span>
                        </div>
                      ) : null}
                    </div>
                  )}

                  {isCompare && (
                    <div className="px-4 py-3 bg-zinc-950 border-b border-zinc-800 flex gap-2 justify-center">
                      <button
                        onClick={() => setSelectedModes(prev => ({ ...prev, [subject]: "serial" }))}
                        className={`flex-1 py-1 px-1.5 rounded text-[11px] font-bold transition-all ${
                          activeMode === "serial"
                            ? "bg-blue-600 text-white shadow-sm"
                            : "bg-zinc-900 text-zinc-400 hover:bg-zinc-800 hover:text-zinc-300"
                        }`}
                      >
                        Serial
                      </button>
                      <button
                        onClick={() => setSelectedModes(prev => ({ ...prev, [subject]: "parallel" }))}
                        className={`flex-1 py-1 px-1.5 rounded text-[11px] font-bold transition-all ${
                          activeMode === "parallel"
                            ? "bg-purple-600 text-white shadow-sm"
                            : "bg-zinc-900 text-zinc-400 hover:bg-zinc-800 hover:text-zinc-300"
                        }`}
                      >
                        OpenMP
                      </button>
                      {(res as CompareData).pthread && (
                        <button
                          onClick={() => setSelectedModes(prev => ({ ...prev, [subject]: "pthread" }))}
                          className={`flex-1 py-1 px-1.5 rounded text-[11px] font-bold transition-all ${
                            activeMode === "pthread"
                              ? "bg-cyan-600 text-white shadow-sm"
                              : "bg-zinc-900 text-zinc-400 hover:bg-zinc-800 hover:text-zinc-300"
                          }`}
                        >
                          Pthreads
                        </button>
                      )}
                      {(res as CompareData).mpi && (
                        <button
                          onClick={() => setSelectedModes(prev => ({ ...prev, [subject]: "mpi" }))}
                          className={`flex-1 py-1 px-1.5 rounded text-[11px] font-bold transition-all ${
                            activeMode === "mpi"
                              ? "bg-green-600 text-white shadow-sm"
                              : "bg-zinc-900 text-zinc-400 hover:bg-zinc-800 hover:text-zinc-300"
                          }`}
                        >
                          MPI
                        </button>
                      )}
                    </div>
                  )}

                  <div className="flex-1">
                    <ResultPanel result={calcRes} color={borderColor} hideRounding={true} />
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
