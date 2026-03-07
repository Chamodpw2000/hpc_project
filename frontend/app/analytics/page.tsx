"use client";
import { useState } from "react";
import { useRouter } from "next/navigation";

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
  statistics: Statistics;
  grade_distribution: GradeDistribution;
}

interface Comparison {
  serial_time_ms: number;
  parallel_time_ms: number;
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
  scores_per_student: number;
}

interface Student {
  _id: string;
  student_id: string;
  name: string;
  email: string;
}

function StatCard({ label, value }: { label: string; value: string }) {
  return (
    <div className="bg-zinc-800 rounded-lg p-3 text-center">
      <div className="text-xs text-zinc-400 mb-1">{label}</div>
      <div className="text-lg font-semibold text-white font-mono">{value}</div>
    </div>
  );
}

function GradeBar({ grade, count, total, color }: { grade: string; count: number; total: number; color: string }) {
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

function ResultPanel({ result, color }: { result: CalcResult; color: string }) {
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

      <div className="mb-4 p-3 bg-zinc-800/50 rounded-lg text-center">
        <div className="text-xs text-zinc-400">Execution Time</div>
        <div className="text-2xl font-bold text-white font-mono">{result.elapsed_ms.toFixed(4)} ms</div>
        <div className="text-xs text-zinc-500 font-mono">sort: {result.sort_time_ms.toFixed(4)} ms</div>
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

  const [loading, setLoading] = useState<string | null>(null);
  const [seedResult, setSeedResult] = useState<SeedData | null>(null);
  const [compareData, setCompareData] = useState<CompareData | null>(null);
  const [serialOnly, setSerialOnly] = useState<CalcResult | null>(null);
  const [parallelOnly, setParallelOnly] = useState<CalcResult | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [numStudents, setNumStudents] = useState(100);
  const [scoresPerStudent, setScoresPerStudent] = useState(10);
  const [students, setStudents] = useState<Student[]>([]);
  const [studentName, setStudentName] = useState("");
  const [studentEmail, setStudentEmail] = useState("");
  const [studentStudentId, setStudentStudentId] = useState("");
  const [studentsLoading, setStudentsLoading] = useState(false);

  async function fetchStudents() {
    setStudentsLoading(true);
    setError(null);
    try {
      const res = await fetch(`${API}/api/students`);
      const json = await res.json();
      setStudents(json.data || []);
    } catch (e) {
      setError(`Fetch students failed: ${e}`);
    }
    setStudentsLoading(false);
  }

  async function createStudent() {
    setError(null);
    try {
      const res = await fetch(`${API}/api/students`, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ student_id: studentStudentId, name: studentName, email: studentEmail }),
      });
      if (!res.ok) throw new Error(`HTTP ${res.status}`);
      setStudentName("");
      setStudentEmail("");
      setStudentStudentId("");
      await fetchStudents();
    } catch (e) {
      setError(`Create student failed: ${e}`);
    }
  }

  async function deleteStudent(id: string) {
    setError(null);
    try {
      await fetch(`${API}/api/students/${id}`, { method: "DELETE" });
      await fetchStudents();
    } catch (e) {
      setError(`Delete student failed: ${e}`);
    }
  }

  async function seedData() {
    setLoading("seed");
    setError(null);
    try {
      const res = await fetch(`${API}/api/seed`, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ num_students: numStudents, scores_per_student: scoresPerStudent }),
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
    try {
      const res = await fetch(`${API}/api/calculate/serial`);
      const json = await res.json();
      setSerialOnly(json.data);
    } catch (e) {
      setError(`Serial calc failed: ${e}`);
    }
    setLoading(null);
  }

  async function runParallel() {
    setLoading("parallel");
    setError(null);
    try {
      const res = await fetch(`${API}/api/calculate/parallel`);
      const json = await res.json();
      setParallelOnly(json.data);
    } catch (e) {
      setError(`Parallel calc failed: ${e}`);
    }
    setLoading(null);
  }

  async function runCompare() {
    setLoading("compare");
    setError(null);
    try {
      const res = await fetch(`${API}/api/calculate/compare`);
      const json = await res.json();
      setCompareData(json.data);
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
      <div className="max-w-6xl mx-auto px-4 py-8">
        {/* Header */}
        <div className="flex items-center justify-between mb-8">
          <div className="text-center flex-1">
            <h1 className="text-3xl font-bold mb-2">OpenMP Score Analyzer</h1>
            <p className="text-zinc-400">Serial vs Parallel Performance Comparison using MongoDB Data</p>
          </div>
          <button
            onClick={logout}
            className="flex items-center gap-1.5 text-zinc-500 hover:text-red-400 text-sm transition-colors border border-zinc-800 hover:border-red-900 px-3 py-1.5 rounded-lg"
          >
            <svg className="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24">
              <path strokeLinecap="round" strokeLinejoin="round" strokeWidth={2}
                d="M17 16l4-4m0 0l-4-4m4 4H7m6 4v1a3 3 0 01-3 3H6a3 3 0 01-3-3V7a3 3 0 013-3h4a3 3 0 013 3v1" />
            </svg>
            Logout
          </button>
        </div>

        {/* Students Management */}
        <div className="bg-zinc-900 border border-zinc-800 rounded-xl p-6 mb-6">
          <div className="flex items-center justify-between mb-4">
            <h2 className="text-lg font-semibold">Students Management</h2>
            <button
              onClick={fetchStudents}
              disabled={studentsLoading}
              className="bg-zinc-700 hover:bg-zinc-600 disabled:opacity-50 px-3 py-1.5 rounded text-sm transition-colors"
            >
              {studentsLoading ? "Loading..." : "Refresh"}
            </button>
          </div>

          {/* Add Student Form */}
          <div className="flex flex-wrap gap-3 items-end mb-4">
            <div>
              <label className="block text-xs text-zinc-400 mb-1">Student ID</label>
              <input
                type="text"
                value={studentStudentId}
                onChange={(e) => setStudentStudentId(e.target.value)}
                placeholder="e.g. S001"
                className="bg-zinc-800 border border-zinc-700 rounded px-3 py-2 w-28 text-white font-mono text-sm"
              />
            </div>
            <div>
              <label className="block text-xs text-zinc-400 mb-1">Name</label>
              <input
                type="text"
                value={studentName}
                onChange={(e) => setStudentName(e.target.value)}
                placeholder="Full name"
                className="bg-zinc-800 border border-zinc-700 rounded px-3 py-2 w-40 text-white text-sm"
              />
            </div>
            <div>
              <label className="block text-xs text-zinc-400 mb-1">Email</label>
              <input
                type="email"
                value={studentEmail}
                onChange={(e) => setStudentEmail(e.target.value)}
                placeholder="email@example.com"
                className="bg-zinc-800 border border-zinc-700 rounded px-3 py-2 w-48 text-white text-sm"
              />
            </div>
            <button
              onClick={createStudent}
              disabled={!studentName || !studentEmail || !studentStudentId}
              className="bg-indigo-600 hover:bg-indigo-500 disabled:opacity-40 px-4 py-2 rounded font-semibold text-sm transition-colors"
            >
              Add Student
            </button>
          </div>

          {/* Students Table */}
          {students.length > 0 ? (
            <div className="overflow-x-auto">
              <table className="w-full text-sm">
                <thead>
                  <tr className="text-zinc-400 text-xs uppercase border-b border-zinc-800">
                    <th className="text-left py-2 pr-4">Student ID</th>
                    <th className="text-left py-2 pr-4">Name</th>
                    <th className="text-left py-2 pr-4">Email</th>
                    <th className="text-right py-2">Action</th>
                  </tr>
                </thead>
                <tbody>
                  {students.map((s) => (
                    <tr key={s._id} className="border-b border-zinc-800/50 hover:bg-zinc-800/30">
                      <td className="py-2 pr-4 font-mono text-indigo-400">{s.student_id}</td>
                      <td className="py-2 pr-4 text-white">{s.name}</td>
                      <td className="py-2 pr-4 text-zinc-400">{s.email}</td>
                      <td className="py-2 text-right">
                        <button
                          onClick={() => deleteStudent(s._id)}
                          className="text-red-400 hover:text-red-300 text-xs font-mono transition-colors"
                        >
                          Delete
                        </button>
                      </td>
                    </tr>
                  ))}
                </tbody>
              </table>
            </div>
          ) : (
            <div className="text-zinc-500 text-sm text-center py-4">
              No students loaded. Click Refresh to fetch from database.
            </div>
          )}
        </div>

        {/* Seed Controls */}
        <div className="bg-zinc-900 border border-zinc-800 rounded-xl p-6 mb-6">
          <h2 className="text-lg font-semibold mb-4">1. Seed Database with Test Data</h2>
          <div className="flex flex-wrap gap-4 items-end">
            <div>
              <label className="block text-xs text-zinc-400 mb-1">Students</label>
              <input
                type="number"
                title="Number of students"
                placeholder="100"
                value={numStudents}
                onChange={(e) => setNumStudents(Number(e.target.value))}
                className="bg-zinc-800 border border-zinc-700 rounded px-3 py-2 w-28 text-white font-mono"
              />
            </div>
            <div>
              <label className="block text-xs text-zinc-400 mb-1">Scores/Student</label>
              <input
                type="number"
                title="Scores per student"
                placeholder="10"
                value={scoresPerStudent}
                onChange={(e) => setScoresPerStudent(Number(e.target.value))}
                className="bg-zinc-800 border border-zinc-700 rounded px-3 py-2 w-28 text-white font-mono"
              />
            </div>
            <button
              onClick={seedData}
              disabled={loading !== null}
              className="bg-emerald-600 hover:bg-emerald-500 disabled:opacity-50 px-5 py-2 rounded font-semibold transition-colors"
            >
              {loading === "seed" ? "Seeding..." : "Seed Data"}
            </button>
            <div className="text-xs text-zinc-500 self-center">
              Total scores: {numStudents * scoresPerStudent}
            </div>
          </div>
          {seedResult && (
            <div className="mt-3 text-sm text-emerald-400 font-mono">
              Seeded {seedResult.students_created} students, {seedResult.scores_created} scores
            </div>
          )}
        </div>

        {/* Calculation Buttons */}
        <div className="bg-zinc-900 border border-zinc-800 rounded-xl p-6 mb-6">
          <h2 className="text-lg font-semibold mb-4">2. Run Calculations</h2>
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
            {serialOnly && <ResultPanel result={serialOnly} color="border-blue-700" />}
            {parallelOnly && <ResultPanel result={parallelOnly} color="border-purple-700" />}
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
                    {compareData.comparison.serial_time_ms.toFixed(4)} ms
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
                    {compareData.comparison.parallel_time_ms.toFixed(4)} ms
                  </div>
                  <div className="text-xs text-zinc-500">{compareData.comparison.parallel_threads} threads</div>
                </div>
              </div>
              <div className="mt-4 text-xs text-zinc-500 font-mono">
                Data size: {compareData.comparison.data_size.toLocaleString()} scores from MongoDB
              </div>
            </div>

            {/* Side-by-side panels */}
            <div className="grid md:grid-cols-2 gap-6">
              <ResultPanel result={compareData.serial} color="border-blue-700" />
              <ResultPanel result={compareData.parallel} color="border-purple-700" />
            </div>
          </>
        )}
      </div>
    </div>
  );
}
