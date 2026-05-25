"use client";
import { useState, useEffect } from "react";
import {
  LineChart, Line, XAxis, YAxis, CartesianGrid, Tooltip, Legend, ResponsiveContainer
} from "recharts";

interface BenchmarkEntry {
  threads: number;
  time_ms: number;
  speedup: number;
  efficiency: number;
}

interface BenchmarkData {
  data_size: number;
  db_fetch_ms: number;
  serial_time_ms: number;
  openmp: BenchmarkEntry[];
  pthread: BenchmarkEntry[];
  mpi: BenchmarkEntry[];
}

interface SubjectOption { name: string; class_name: string; }

const API = "http://localhost:8090";

export default function ScalingTab() {
  const [classes, setClasses] = useState<string[]>([]);
  const [selectedClass, setSelectedClass] = useState("");
  const [subjects, setSubjects] = useState<SubjectOption[]>([]);
  const [selectedSubject, setSelectedSubject] = useState("");
  
  const [loading, setLoading] = useState(false);
  const [data, setData] = useState<BenchmarkData | null>(null);
  const [error, setError] = useState<string | null>(null);

  const [showIdeal, setShowIdeal] = useState(true);
  const [showOpenMP, setShowOpenMP] = useState(true);
  const [showPthreads, setShowPthreads] = useState(true);
  const [showMPI, setShowMPI] = useState(true);

  // Fetch classes on mount
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
        setClasses(names);
        if (names[0]) setSelectedClass(names[0]);
      })
      .catch(() => { /* leave empty */ });
  }, []);

  // Fetch subjects when class changes
  useEffect(() => {
    if (!selectedClass) {
      setSubjects([]);
      setSelectedSubject("");
      return;
    }
    fetch(`/api/subjects?class=${encodeURIComponent(selectedClass)}`, { cache: "no-store" })
      .then(r => r.json())
      .then(j => {
        const list = j.data ?? [];
        setSubjects(list);
        if (list[0]) setSelectedSubject(list[0].name);
      })
      .catch(() => setSubjects([]));
  }, [selectedClass]);

  async function runBenchmark() {
    setLoading(true);
    setError(null);
    setData(null);
    try {
      const params = new URLSearchParams();
      if (selectedClass) params.append("class", selectedClass);
      if (selectedSubject) params.append("subject", selectedSubject);
      
      const res = await fetch(`${API}/api/calculate/scaling?${params.toString()}`);
      const json = await res.json();
      if (!res.ok) throw new Error(json.message ?? "Failed to run benchmark");
      setData(json.data);
    } catch (err: unknown) {
      setError(err instanceof Error ? err.message : String(err));
    } finally {
      setLoading(false);
    }
  }

  // Prepares data for chart. Recharts needs a combined array of objects like:
  // { threads: 1, OpenMP_Time: X, Pthread_Time: Y, MPI_Time: Z, Ideal_Speedup: 1, OpenMP_Speedup: A, ... }
  const getCombinedChartData = () => {
    if (!data) return [];
    
    // We assume same thread inputs for comparison (1, 2, 4, 8, 12, 16)
    const threads = [1, 2, 4, 8, 12, 16];
    return threads.map(t => {
      const omp = data.openmp.find(e => e.threads === t);
      const pt = data.pthread.find(e => e.threads === t);
      const mpi = data.mpi.find(e => e.threads === t);
      
      return {
        threads: t,
        "Ideal Speedup": t,
        "OpenMP Time (ms)": omp ? parseFloat(omp.time_ms.toFixed(2)) : null,
        "Pthreads Time (ms)": pt ? parseFloat(pt.time_ms.toFixed(2)) : null,
        "MPI Time (ms)": mpi ? parseFloat(mpi.time_ms.toFixed(2)) : null,
        "OpenMP Speedup": omp ? parseFloat(omp.speedup.toFixed(2)) : null,
        "Pthreads Speedup": pt ? parseFloat(pt.speedup.toFixed(2)) : null,
        "MPI Speedup": mpi ? parseFloat(mpi.speedup.toFixed(2)) : null,
        "OpenMP Efficiency (%)": omp ? parseFloat((omp.efficiency * 100).toFixed(1)) : null,
        "Pthreads Efficiency (%)": pt ? parseFloat((pt.efficiency * 100).toFixed(1)) : null,
        "MPI Efficiency (%)": mpi ? parseFloat((mpi.efficiency * 100).toFixed(1)) : null,
      };
    });
  };

  const chartData = getCombinedChartData();

  return (
    <div className="space-y-6">
      {/* Configuration & Controls */}
      <div className="bg-zinc-900 border border-zinc-800 rounded-xl p-6">
        <h2 className="text-lg font-semibold mb-4">Run Scaling Benchmark</h2>
        <p className="text-xs text-zinc-500 mb-6">
          Compare the performance scaling of OpenMP (loop-level multicore), POSIX Threads (chunked multicore), and MPI (distributed processes). This will run the statistical algorithms iteratively using different thread count configurations on the active database dataset.
        </p>

        <div className="flex flex-wrap gap-4 items-end">
          <div className="w-48">
            <label htmlFor="scaling-class" className="block text-xs text-zinc-400 mb-1 font-semibold">Target Class</label>
            <select
              id="scaling-class"
              value={selectedClass}
              onChange={e => setSelectedClass(e.target.value)}
              className="bg-zinc-800 border border-zinc-700 rounded-lg px-3 py-2 text-white text-sm focus:border-indigo-500 focus:outline-none w-full"
            >
              <option value="">— All classes —</option>
              {classes.map(c => <option key={c} value={c}>{c}</option>)}
            </select>
          </div>

          <div className="w-48">
            <label htmlFor="scaling-subject" className="block text-xs text-zinc-400 mb-1 font-semibold">Subject Filter</label>
            <select
              id="scaling-subject"
              value={selectedSubject}
              onChange={e => setSelectedSubject(e.target.value)}
              disabled={!selectedClass}
              className="bg-zinc-800 border border-zinc-700 rounded-lg px-3 py-2 text-white text-sm focus:border-indigo-500 focus:outline-none w-full disabled:opacity-50"
            >
              <option value="">— All subjects —</option>
              {subjects.map(s => <option key={s.name} value={s.name}>{s.name}</option>)}
            </select>
          </div>

          <button
            onClick={runBenchmark}
            disabled={loading}
            className="bg-indigo-600 hover:bg-indigo-500 disabled:bg-zinc-800 disabled:text-zinc-600 px-6 py-2 rounded-lg font-bold transition-all active:scale-95 flex items-center gap-2"
          >
            {loading ? (
              <>
                <svg className="animate-spin -ml-1 mr-2 h-4 w-4 text-zinc-400" fill="none" viewBox="0 0 24 24">
                  <circle className="opacity-25" cx="12" cy="12" r="10" stroke="currentColor" strokeWidth="4" />
                  <path className="opacity-75" fill="currentColor" d="M4 12a8 8 0 018-8V0C5.373 0 0 5.373 0 12h4zm2 5.291A7.962 7.962 0 014 12H0c0 3.042 1.135 5.824 3 7.938l3-2.647z" />
                </svg>
                Running Benchmarks...
              </>
            ) : (
              "Run Scaling Benchmark"
            )}
          </button>
        </div>

        {error && (
          <div className="bg-red-900/30 border border-red-800 rounded-xl p-4 mt-6 text-red-400 text-sm">
            {error}
          </div>
        )}
      </div>

      {/* Results & Visualizations */}
      {data && (
        <div className="space-y-6 animate-fadeIn">
          {/* Metadata Card */}
          <div className="grid grid-cols-2 sm:grid-cols-4 gap-4 bg-zinc-900 border border-zinc-800 rounded-xl p-5 text-center">
            <div>
              <div className="text-xs text-zinc-500 mb-1">Dataset Size</div>
              <div className="text-xl font-bold text-white font-mono">{data.data_size.toLocaleString()} scores</div>
            </div>
            <div>
              <div className="text-xs text-zinc-500 mb-1">MongoDB Fetch Time</div>
              <div className="text-xl font-bold text-yellow-400 font-mono">{(data.db_fetch_ms / 1000).toFixed(4)} s</div>
            </div>
            <div>
              <div className="text-xs text-zinc-500 mb-1">Serial (1 thread) Baseline</div>
              <div className="text-xl font-bold text-blue-400 font-mono">{(data.serial_time_ms / 1000).toFixed(4)} s</div>
            </div>
            <div>
              <div className="text-xs text-zinc-500 mb-1">Max Speedup Achieved</div>
              <div className="text-xl font-bold text-emerald-400 font-mono">
                {Math.max(
                  ...data.openmp.map(e => e.speedup),
                  ...data.pthread.map(e => e.speedup),
                  ...data.mpi.map(e => e.speedup),
                  1
                ).toFixed(2)}x
              </div>
            </div>
          </div>

          {/* Graphs Grid */}
          <div className="grid md:grid-cols-2 gap-6">
            {/* 1. Execution Time Graph */}
            <div className="bg-zinc-900 border border-zinc-800 rounded-xl p-5">
              <h3 className="text-sm font-bold uppercase tracking-wider text-zinc-300 mb-4">Execution Time (ms) vs. Threads</h3>
              <div className="h-64">
                <ResponsiveContainer width="100%" height="100%">
                  <LineChart data={chartData} margin={{ top: 10, right: 30, left: 0, bottom: 0 }}>
                    <CartesianGrid strokeDasharray="3 3" stroke="#27272a" />
                    <XAxis dataKey="threads" stroke="#71717a" label={{ value: 'Thread Count', position: 'insideBottom', offset: -5, fill: '#71717a', fontSize: 11 }} />
                    <YAxis stroke="#71717a" label={{ value: 'Time (ms)', angle: -90, position: 'insideLeft', fill: '#71717a', fontSize: 11 }} />
                    <Tooltip contentStyle={{ backgroundColor: '#18181b', borderColor: '#27272a', color: '#fff' }} />
                    <Legend verticalAlign="top" height={36}/>
                    <Line type="monotone" dataKey="OpenMP Time (ms)" stroke="#a855f7" strokeWidth={2.5} activeDot={{ r: 6 }} connectNulls />
                    <Line type="monotone" dataKey="Pthreads Time (ms)" stroke="#06b6d4" strokeWidth={2.5} activeDot={{ r: 6 }} connectNulls />
                    {data.mpi.length > 0 && (
                      <Line type="monotone" dataKey="MPI Time (ms)" stroke="#22c55e" strokeWidth={2.5} activeDot={{ r: 6 }} connectNulls />
                    )}
                  </LineChart>
                </ResponsiveContainer>
              </div>
            </div>

            {/* 2. Speedup Graph (Strong Scaling) */}
            <div className="bg-zinc-900 border border-zinc-800 rounded-xl p-5">
              <div className="flex flex-wrap items-center justify-between mb-4 gap-2 border-b border-zinc-800/50 pb-2">
                <h3 className="text-sm font-bold uppercase tracking-wider text-zinc-300">Speedup vs. Threads (Strong Scaling)</h3>
                
                {/* Toggles */}
                <div className="flex items-center gap-3 text-xs">
                  <label className="flex items-center gap-1.5 cursor-pointer text-zinc-400 hover:text-white select-none">
                    <input
                      type="checkbox"
                      checked={showIdeal}
                      onChange={e => setShowIdeal(e.target.checked)}
                      className="rounded bg-zinc-800 border-zinc-700 text-indigo-600 focus:ring-0 focus:ring-offset-0 w-3 h-3"
                    />
                    Ideal
                  </label>
                  <label className="flex items-center gap-1.5 cursor-pointer text-zinc-400 hover:text-white select-none">
                    <input
                      type="checkbox"
                      checked={showOpenMP}
                      onChange={e => setShowOpenMP(e.target.checked)}
                      className="rounded bg-zinc-800 border-zinc-700 text-purple-600 focus:ring-0 focus:ring-offset-0 w-3 h-3"
                    />
                    OpenMP
                  </label>
                  <label className="flex items-center gap-1.5 cursor-pointer text-zinc-400 hover:text-white select-none">
                    <input
                      type="checkbox"
                      checked={showPthreads}
                      onChange={e => setShowPthreads(e.target.checked)}
                      className="rounded bg-zinc-800 border-zinc-700 text-cyan-600 focus:ring-0 focus:ring-offset-0 w-3 h-3"
                    />
                    Pthreads
                  </label>
                  {data.mpi.length > 0 && (
                    <label className="flex items-center gap-1.5 cursor-pointer text-zinc-400 hover:text-white select-none">
                      <input
                        type="checkbox"
                        checked={showMPI}
                        onChange={e => setShowMPI(e.target.checked)}
                        className="rounded bg-zinc-800 border-zinc-700 text-emerald-600 focus:ring-0 focus:ring-offset-0 w-3 h-3"
                      />
                      MPI
                    </label>
                  )}
                </div>
              </div>
              <div className="h-64">
                <ResponsiveContainer width="100%" height="100%">
                  <LineChart data={chartData} margin={{ top: 10, right: 30, left: 0, bottom: 0 }}>
                    <CartesianGrid strokeDasharray="3 3" stroke="#27272a" />
                    <XAxis dataKey="threads" stroke="#71717a" label={{ value: 'Thread Count', position: 'insideBottom', offset: -5, fill: '#71717a', fontSize: 11 }} />
                    <YAxis stroke="#71717a" label={{ value: 'Speedup Factor', angle: -90, position: 'insideLeft', fill: '#71717a', fontSize: 11 }} />
                    <Tooltip contentStyle={{ backgroundColor: '#18181b', borderColor: '#27272a', color: '#fff' }} />
                    <Legend verticalAlign="top" height={36}/>
                    {showIdeal && (
                      <Line type="monotone" dataKey="Ideal Speedup" stroke="#71717a" strokeDasharray="5 5" strokeWidth={1.5} dot={false} />
                    )}
                    {showOpenMP && (
                      <Line type="monotone" dataKey="OpenMP Speedup" stroke="#a855f7" strokeWidth={2.5} activeDot={{ r: 6 }} connectNulls />
                    )}
                    {showPthreads && (
                      <Line type="monotone" dataKey="Pthreads Speedup" stroke="#06b6d4" strokeWidth={2.5} activeDot={{ r: 6 }} connectNulls />
                    )}
                    {showMPI && data.mpi.length > 0 && (
                      <Line type="monotone" dataKey="MPI Speedup" stroke="#22c55e" strokeWidth={2.5} activeDot={{ r: 6 }} connectNulls />
                    )}
                  </LineChart>
                </ResponsiveContainer>
              </div>
            </div>

            {/* 3. Efficiency Graph */}
            <div className="bg-zinc-900 border border-zinc-800 rounded-xl p-5">
              <h3 className="text-sm font-bold uppercase tracking-wider text-zinc-300 mb-4">Parallel Efficiency (%) vs. Threads</h3>
              <div className="h-64">
                <ResponsiveContainer width="100%" height="100%">
                  <LineChart data={chartData} margin={{ top: 10, right: 30, left: 0, bottom: 0 }}>
                    <CartesianGrid strokeDasharray="3 3" stroke="#27272a" />
                    <XAxis dataKey="threads" stroke="#71717a" label={{ value: 'Thread Count', position: 'insideBottom', offset: -5, fill: '#71717a', fontSize: 11 }} />
                    <YAxis stroke="#71717a" label={{ value: 'Efficiency (%)', angle: -90, position: 'insideLeft', fill: '#71717a', fontSize: 11 }} domain={[0, 100]} />
                    <Tooltip contentStyle={{ backgroundColor: '#18181b', borderColor: '#27272a', color: '#fff' }} />
                    <Legend verticalAlign="top" height={36}/>
                    <Line type="monotone" dataKey="OpenMP Efficiency (%)" stroke="#a855f7" strokeWidth={2.5} activeDot={{ r: 6 }} connectNulls />
                    <Line type="monotone" dataKey="Pthreads Efficiency (%)" stroke="#06b6d4" strokeWidth={2.5} activeDot={{ r: 6 }} connectNulls />
                    {data.mpi.length > 0 && (
                      <Line type="monotone" dataKey="MPI Efficiency (%)" stroke="#22c55e" strokeWidth={2.5} activeDot={{ r: 6 }} connectNulls />
                    )}
                  </LineChart>
                </ResponsiveContainer>
              </div>
            </div>

            {/* Educational / Explanatory Text */}
            <div className="bg-zinc-900 border border-zinc-800 rounded-xl p-6 flex flex-col justify-center space-y-4">
              <h4 className="text-sm font-bold text-zinc-200 uppercase tracking-wider">HPC Scaling Concepts</h4>
              <div className="space-y-3 text-xs text-zinc-400">
                <p>
                  <strong className="text-zinc-200">Amdahl's Law:</strong> States that the overall speedup of a program is limited by its sequential (non-parallelizable) portion. In this app, database fetching and initial setup act as sequential overhead.
                </p>
                <p>
                  <strong className="text-zinc-200">Strong Scaling (Speedup):</strong> Demonstrates how the execution time decreases as you add more threads/processors for a <span className="text-white font-semibold">fixed dataset size</span>. If execution time drops linearly with cores, scaling is ideal.
                </p>
                <p>
                  <strong className="text-zinc-200">Parallel Efficiency:</strong> Measures the fraction of time cores spent doing useful work. It is calculated as <span className="text-white font-mono">Speedup / Cores</span>. A value of 100% means perfect core utilization. Efficiency typically decreases at higher core counts due to communication overhead, thread scheduling, and lock contention.
                </p>
              </div>
            </div>
          </div>

          {/* Details Table */}
          <div className="bg-zinc-900 border border-zinc-800 rounded-xl p-5 overflow-x-auto">
            <h3 className="text-sm font-bold uppercase tracking-wider text-zinc-300 mb-4">Detailed Performance Metrics</h3>
            <table className="w-full text-left text-xs border-collapse">
              <thead>
                <tr className="border-b border-zinc-800 text-zinc-500">
                  <th className="py-2.5">Cores/Threads</th>
                  <th className="py-2.5">Method</th>
                  <th className="py-2.5">Execution Time</th>
                  <th className="py-2.5">Speedup Factor</th>
                  <th className="py-2.5">Parallel Efficiency</th>
                </tr>
              </thead>
              <tbody className="divide-y divide-zinc-800/50 text-zinc-300 font-mono">
                {/* Serial baseline row */}
                <tr className="bg-blue-950/10 text-blue-300 font-semibold">
                  <td className="py-2.5">1</td>
                  <td className="py-2.5">Serial (Baseline)</td>
                  <td className="py-2.5">{data.serial_time_ms.toFixed(2)} ms</td>
                  <td className="py-2.5">1.00x</td>
                  <td className="py-2.5">100.0%</td>
                </tr>
                {/* OMP rows */}
                {data.openmp.map(e => (
                  <tr key={`omp-${e.threads}`}>
                    <td className="py-2.5 text-zinc-400">{e.threads}</td>
                    <td className="py-2.5 text-purple-400">OpenMP</td>
                    <td className="py-2.5">{e.time_ms.toFixed(2)} ms</td>
                    <td className="py-2.5">{e.speedup.toFixed(2)}x</td>
                    <td className="py-2.5">{(e.efficiency * 100).toFixed(1)}%</td>
                  </tr>
                ))}
                {/* Pthreads rows */}
                {data.pthread.map(e => (
                  <tr key={`pt-${e.threads}`}>
                    <td className="py-2.5 text-zinc-400">{e.threads}</td>
                    <td className="py-2.5 text-cyan-400">Pthreads</td>
                    <td className="py-2.5">{e.time_ms.toFixed(2)} ms</td>
                    <td className="py-2.5">{e.speedup.toFixed(2)}x</td>
                    <td className="py-2.5">{(e.efficiency * 100).toFixed(1)}%</td>
                  </tr>
                ))}
                {/* MPI rows */}
                {data.mpi.map(e => (
                  <tr key={`mpi-${e.threads}`}>
                    <td className="py-2.5 text-zinc-400">{e.threads}</td>
                    <td className="py-2.5 text-emerald-400 font-semibold">MPI</td>
                    <td className="py-2.5">{e.time_ms.toFixed(2)} ms</td>
                    <td className="py-2.5">{e.speedup.toFixed(2)}x</td>
                    <td className="py-2.5">{(e.efficiency * 100).toFixed(1)}%</td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
        </div>
      )}
    </div>
  );
}
