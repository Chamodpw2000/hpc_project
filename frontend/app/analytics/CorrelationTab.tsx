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
  threads_used: number;
  points: ScatterPoint[];
}
interface CorrelationCompare { serial: CorrelationResult; parallel: CorrelationResult; }

// ── Dummy data ────────────────────────────────────────────────────────────────

const DUMMY_POINTS: ScatterPoint[] = [
  { x: 72, y: 68 }, { x: 85, y: 80 }, { x: 61, y: 55 }, { x: 90, y: 88 },
  { x: 45, y: 42 }, { x: 78, y: 74 }, { x: 53, y: 50 }, { x: 95, y: 91 },
  { x: 67, y: 63 }, { x: 82, y: 76 }, { x: 70, y: 67 }, { x: 58, y: 54 },
  { x: 88, y: 83 }, { x: 74, y: 70 }, { x: 49, y: 46 }, { x: 93, y: 89 },
  { x: 63, y: 60 }, { x: 76, y: 73 }, { x: 55, y: 51 }, { x: 86, y: 81 },
  { x: 69, y: 65 }, { x: 80, y: 77 }, { x: 47, y: 43 }, { x: 91, y: 87 },
  { x: 75, y: 71 },
];

function makeDummySerial(): CorrelationResult {
  return { correlation_coefficient: 0.78, elapsed_ms: 45, threads_used: 1, points: DUMMY_POINTS };
}

function makeDummyParallel(): CorrelationResult {
  return { correlation_coefficient: 0.78, elapsed_ms: 12, threads_used: 8, points: DUMMY_POINTS };
}

// ── Helpers ───────────────────────────────────────────────────────────────────

function correlationLabel(r: number): { label: string; color: string } {
  const abs = Math.abs(r);
  if (abs >= 0.7) return { label: "Strong",    color: "text-emerald-400" };
  if (abs >= 0.5) return { label: "Moderate",  color: "text-blue-400"   };
  if (abs >= 0.3) return { label: "Weak",      color: "text-yellow-400" };
  return             { label: "Very Weak", color: "text-red-400"    };
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
  const modeLabel = result.threads_used === 1 ? "Serial" : "Parallel (OpenMP)";

  return (
    <div className={`border ${borderColor} rounded-xl p-5 bg-zinc-900/50`}>
      <div className="flex items-center justify-between mb-4">
        <h3 className="text-lg font-bold text-white uppercase tracking-wider">{modeLabel}</h3>
        <span className="text-xs bg-zinc-800 text-zinc-300 px-2 py-1 rounded font-mono">
          {result.threads_used} thread{result.threads_used !== 1 ? "s" : ""}
        </span>
      </div>

      <div className="mb-4 p-4 bg-zinc-800/50 rounded-xl text-center">
        <div className="text-xs text-zinc-400 uppercase tracking-wider font-semibold mb-1">
          Pearson Correlation Coefficient
        </div>
        <div className="text-4xl font-black text-white font-mono">{r.toFixed(4)}</div>
        <div className={`text-sm font-semibold mt-1 ${labelColor}`}>{label} Correlation</div>
        <div className="text-xs text-zinc-500 mt-1">
          {r > 0 ? "Positive" : r < 0 ? "Negative" : "No"} linear relationship
        </div>
      </div>

      <div className="mb-4 grid grid-cols-2 gap-2 text-center">
        <div className="bg-zinc-800 rounded-lg p-3">
          <div className="text-xs text-zinc-400">Calc Time</div>
          <div className="text-sm font-bold text-white font-mono">{result.elapsed_ms} ms</div>
        </div>
        <div className="bg-zinc-800 rounded-lg p-3">
          <div className="text-xs text-zinc-400">Threads</div>
          <div className="text-sm font-bold text-white font-mono">{result.threads_used}</div>
        </div>
      </div>

      <div>
        <div className="text-xs text-zinc-400 mb-2 font-semibold uppercase tracking-wider">
          Score Distribution
        </div>
        <CorrelationChart
          points={result.points}
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

  const [class1, setClass1]               = useState("");
  const [subjects1, setSubjects1]         = useState<SubjectOption[]>([]);
  const [loadingSubjects1, setLoadingSubjects1] = useState(false);
  const [subject1, setSubject1]           = useState("");

  const [class2, setClass2]               = useState("");
  const [subjects2, setSubjects2]         = useState<SubjectOption[]>([]);
  const [loadingSubjects2, setLoadingSubjects2] = useState(false);
  const [subject2, setSubject2]           = useState("");

  // Action state
  const [loading, setLoading]             = useState<"serial" | "parallel" | "compare" | null>(null);
  const [error, setError]                 = useState<string | null>(null);

  // Result state
  const [serialResult, setSerialResult]       = useState<CorrelationResult | null>(null);
  const [parallelResult, setParallelResult]   = useState<CorrelationResult | null>(null);
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
        if (names[0]) { setClass1(names[0]); setClass2(names[0]); }
      } catch { /* leave dropdowns empty */ }
      finally { setLoadingClasses(false); }
    }
    loadClasses();
  }, []);

  // ── Fetch subjects when class1 changes ──
  useEffect(() => {
    if (!class1) { setSubjects1([]); setSubject1(""); return; }
    setLoadingSubjects1(true);
    setSubject1("");
    fetch(`/api/subjects?class=${encodeURIComponent(class1)}`, { cache: "no-store" })
      .then(r => r.json())
      .then(j => setSubjects1(j.data ?? []))
      .catch(() => setSubjects1([]))
      .finally(() => setLoadingSubjects1(false));
  }, [class1]);

  useEffect(() => {
    if (subjects1.length > 0) setSubject1(subjects1[0].name);
  }, [subjects1]);

  // ── Fetch subjects when class2 changes ──
  useEffect(() => {
    if (!class2) { setSubjects2([]); setSubject2(""); return; }
    setLoadingSubjects2(true);
    setSubject2("");
    fetch(`/api/subjects?class=${encodeURIComponent(class2)}`, { cache: "no-store" })
      .then(r => r.json())
      .then(j => setSubjects2(j.data ?? []))
      .catch(() => setSubjects2([]))
      .finally(() => setLoadingSubjects2(false));
  }, [class2]);

  useEffect(() => {
    if (subjects2.length > 0) setSubject2(subjects2[0].name);
  }, [subjects2]);

  // ── Button handlers ──
  function runSerial() {
    setLoading("serial"); setError(null); setCompareResult(null);
    setTimeout(() => {
      setSerialResult(makeDummySerial());
      setParallelResult(null);
      setLoading(null);
    }, 50);
  }

  function runParallel() {
    setLoading("parallel"); setError(null); setCompareResult(null);
    setTimeout(() => {
      setParallelResult(makeDummyParallel());
      setSerialResult(null);
      setLoading(null);
    }, 50);
  }

  function runCompare() {
    setLoading("compare"); setError(null); setSerialResult(null); setParallelResult(null);
    setTimeout(() => {
      setCompareResult({ serial: makeDummySerial(), parallel: makeDummyParallel() });
      setLoading(null);
    }, 50);
  }

  const canRun = !loading && !!subject1 && !!subject2;
  const speedup = compareResult
    ? (compareResult.serial.elapsed_ms / compareResult.parallel.elapsed_ms)
    : 0;

  return (
    <>
      {/* ── Subject Selectors ── */}
      <div className="bg-zinc-900 border border-zinc-800 rounded-xl p-6 mb-6">
        <h2 className="text-lg font-semibold mb-4">1. Select Subjects</h2>
        <div className="flex flex-wrap items-start gap-4">

          {/* Subject 1 */}
          <div className="flex-1 min-w-48">
            <div className="text-xs text-zinc-400 mb-1 font-semibold">Subject 1</div>
            <div className="flex flex-col gap-2">
              <select
                value={class1}
                onChange={e => setClass1(e.target.value)}
                disabled={loadingClasses}
                aria-label="Class for Subject 1"
                className={SELECT_CLS}
              >
                <option value="">— Class —</option>
                {classes.map(c => <option key={c} value={c}>{c}</option>)}
              </select>
              <select
                value={subject1}
                onChange={e => setSubject1(e.target.value)}
                disabled={!class1 || loadingSubjects1}
                aria-label="Subject 1"
                className={SELECT_CLS}
              >
                <option value="">— Subject —</option>
                {subjects1.map(s => <option key={s.name} value={s.name}>{s.name}</option>)}
              </select>
            </div>
          </div>

          {/* VS divider */}
          <div className="flex items-center pt-8">
            <span className="text-zinc-600 font-bold text-lg px-2">vs</span>
          </div>

          {/* Subject 2 */}
          <div className="flex-1 min-w-48">
            <div className="text-xs text-zinc-400 mb-1 font-semibold">Subject 2</div>
            <div className="flex flex-col gap-2">
              <select
                value={class2}
                onChange={e => setClass2(e.target.value)}
                disabled={loadingClasses}
                aria-label="Class for Subject 2"
                className={SELECT_CLS}
              >
                <option value="">— Class —</option>
                {classes.map(c => <option key={c} value={c}>{c}</option>)}
              </select>
              <select
                value={subject2}
                onChange={e => setSubject2(e.target.value)}
                disabled={!class2 || loadingSubjects2}
                aria-label="Subject 2"
                className={SELECT_CLS}
              >
                <option value="">— Subject —</option>
                {subjects2.map(s => <option key={s.name} value={s.name}>{s.name}</option>)}
              </select>
            </div>
          </div>
        </div>

        {(!subject1 || !subject2) && !loadingClasses && (
          <p className="text-xs text-zinc-500 mt-3">Select both subjects to enable calculations.</p>
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
            onClick={runCompare}
            disabled={!canRun}
            className="bg-amber-600 hover:bg-amber-500 disabled:opacity-50 px-6 py-2 rounded font-bold transition-colors text-lg"
          >
            {loading === "compare" ? "Comparing..." : "Compare Both"}
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
      {(serialResult || parallelResult) && !compareResult && (
        <div className="grid md:grid-cols-2 gap-6 mb-6">
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
        </div>
      )}

      {/* ── Compare Results ── */}
      {compareResult && (
        <>
          {/* Speedup banner */}
          <div className="bg-gradient-to-r from-amber-900/40 to-amber-800/20 border border-amber-700 rounded-xl p-6 mb-6 text-center">
            <div className="text-sm text-amber-400 mb-1 uppercase tracking-widest font-semibold">
              Performance Comparison
            </div>
            <div className="grid grid-cols-3 gap-4 mt-4">
              <div>
                <div className="text-xs text-zinc-400">Serial Time</div>
                <div className="text-2xl font-bold text-blue-400 font-mono">
                  {compareResult.serial.elapsed_ms} ms
                </div>
                <div className="text-xs text-zinc-500">1 thread</div>
              </div>
              <div>
                <div className="text-xs text-zinc-400">Speedup</div>
                <div className={`text-4xl font-black font-mono ${speedup >= 1 ? "text-emerald-400" : "text-red-400"}`}>
                  {speedup.toFixed(2)}x
                </div>
                <div className="text-xs text-zinc-500">
                  {(((compareResult.serial.elapsed_ms - compareResult.parallel.elapsed_ms) /
                    compareResult.serial.elapsed_ms) * 100).toFixed(1)}% faster
                </div>
              </div>
              <div>
                <div className="text-xs text-zinc-400">Parallel Time</div>
                <div className="text-2xl font-bold text-purple-400 font-mono">
                  {compareResult.parallel.elapsed_ms} ms
                </div>
                <div className="text-xs text-zinc-500">{compareResult.parallel.threads_used} threads</div>
              </div>
            </div>
          </div>

          {/* Side-by-side panels */}
          <div className="grid md:grid-cols-2 gap-6">
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
          </div>
        </>
      )}
    </>
  );
}
