"use client";
import { useRouter } from "next/navigation";
import { useState, useEffect, useCallback } from "react";

/* ── Types ── */
interface ClassItem { name: string }
interface SubjectItem { name: string; class_name: string }
interface StudentItem {
  student_id: string;
  name: string;
  email: string;
  class_name?: string;
}
interface ScoreItem {
  student_id: string;
  subject: string;
  score: number;
}

/* ── Reusable modal backdrop ── */
function Modal({ title, onClose, children, wide }: { title: string; onClose: () => void; children: React.ReactNode; wide?: boolean }) {
  return (
    <div className="fixed inset-0 z-50 flex items-center justify-center px-4">
      <div className="absolute inset-0 bg-black/70 backdrop-blur-sm" onClick={onClose} />
      <div className={`relative w-full ${wide ? "max-w-lg" : "max-w-md"} bg-zinc-900 border border-zinc-700 rounded-2xl shadow-2xl shadow-black/60 p-6`}>
        <div className="flex items-center justify-between mb-5">
          <h2 className="text-lg font-bold text-white">{title}</h2>
          <button
            onClick={onClose}
            title="Close"
            className="text-zinc-500 hover:text-white transition-colors p-1 rounded-lg hover:bg-zinc-800"
          >
            <svg className="w-5 h-5" fill="none" stroke="currentColor" viewBox="0 0 24 24">
              <path strokeLinecap="round" strokeLinejoin="round" strokeWidth={2} d="M6 18L18 6M6 6l12 12" />
            </svg>
          </button>
        </div>
        {children}
      </div>
    </div>
  );
}

export default function DashboardPage() {
  const router = useRouter();

  /* ── Data state ── */
  const [classes, setClasses]       = useState<string[]>([]);
  const [subjects, setSubjects]     = useState<SubjectItem[]>([]); // eslint-disable-line @typescript-eslint/no-unused-vars
  const [students, setStudents]     = useState<StudentItem[]>([]);
  const [loadingClasses, setLoadingClasses] = useState(true);
  const [loadingSubjects, setLoadingSubjects] = useState(false); // eslint-disable-line @typescript-eslint/no-unused-vars
  const [loadingStudents, setLoadingStudents] = useState(false);

  /* ── UI state ── */
  const [selectedClass, setSelectedClass] = useState<string>("");
  const [dropdownOpen, setDropdownOpen]   = useState(false);

  /* ── Modal state ── */
  const [showAddClass, setShowAddClass]       = useState(false);
  const [showAddSubject, setShowAddSubject]   = useState(false);
  const [showAddStudent, setShowAddStudent]   = useState(false);

  /* ── Form state ── */
  const [newClassName, setNewClassName]       = useState("");
  const [newSubjectName, setNewSubjectName]   = useState("");
  const [subjectClass, setSubjectClass]       = useState("");
  const [newStudentName, setNewStudentName]   = useState("");
  const [newStudentEmail, setNewStudentEmail] = useState("");
  const [newStudentId, setNewStudentId]       = useState("");
  const [formError, setFormError]             = useState("");
  const [formLoading, setFormLoading]         = useState(false);

  /* ── Edit class state ── */
  const [editingClass, setEditingClass]       = useState<string | null>(null);
  const [editClassName, setEditClassName]     = useState("");
  const [editError, setEditError]             = useState("");
  const [editLoading, setEditLoading]         = useState(false);

  /* ── Edit subject state ── */
  const [editingSubject, setEditingSubject]           = useState<string | null>(null); // "className__subjectName"
  const [editSubjectName, setEditSubjectName]         = useState("");
  const [editSubjectError, setEditSubjectError]       = useState("");
  const [editSubjectLoading, setEditSubjectLoading]   = useState(false);

  /* ── Edit / delete student state ── */
  const [editingStudentId, setEditingStudentId]       = useState<string | null>(null);
  const [editStudentName, setEditStudentName]         = useState("");
  const [editStudentEmail, setEditStudentEmail]       = useState("");
  const [editStudentError, setEditStudentError]       = useState("");
  const [editStudentLoading, setEditStudentLoading]   = useState(false);

  /* ── Student detail modal state ── */
  const [detailStudent, setDetailStudent]               = useState<StudentItem | null>(null);
  const [detailScores, setDetailScores]                 = useState<ScoreItem[]>([]);
  const [detailSubjects, setDetailSubjects]             = useState<SubjectItem[]>([]);
  const [detailLoading, setDetailLoading]               = useState(false);

  /* ── Score CRUD state (inside detail modal) ── */
  const [addScoreSubject, setAddScoreSubject]     = useState("");
  const [addScoreValue, setAddScoreValue]         = useState("");
  const [addScoreError, setAddScoreError]         = useState("");
  const [addScoreLoading, setAddScoreLoading]     = useState(false);
  const [editingScoreSubject, setEditingScoreSubject] = useState<string | null>(null);
  const [editScoreValue, setEditScoreValue]       = useState("");
  const [editScoreError, setEditScoreError]       = useState("");
  const [editScoreLoading, setEditScoreLoading]   = useState(false);

  /* ── Fetch classes from backend ── */
  // NOTE: no `selectedClass` in deps — avoids stale closure that caused
  //       dropdown to show only the newly-added class.
  const fetchClasses = useCallback(async (keepSelected?: string) => {
    setLoadingClasses(true);
    try {
      const res  = await fetch("/api/classes", { cache: "no-store" });
      const json = await res.json();
      // Backend returns data as either ["A","B"] (strings) or [{name:"A"}] (objects)
      const raw: unknown[] = json.data ?? [];
      const names: string[] = Array.from(
        new Set(
          raw
            .map((c: unknown) =>
              typeof c === "string" ? c : (c as ClassItem)?.name
            )
            .filter((n): n is string => typeof n === "string" && n.trim() !== "")
        )
      );
      setClasses(names);
      setSelectedClass(prev => {
        const keep = keepSelected ?? prev;
        if (keep && names.includes(keep)) return keep;
        return names[0] ?? "";
      });
      setSubjectClass(prev => {
        if (prev && names.includes(prev)) return prev;
        return names[0] ?? "";
      });
    } catch {
      /* backend unreachable – leave empty */
    } finally {
      setLoadingClasses(false);
    }
  }, []);

  /* ── All subjects cache (for modal "existing subjects" list) ── */
  const [allSubjects, setAllSubjects] = useState<SubjectItem[]>([]);

  /* ── Fetch subjects for selected class ── */
  const fetchSubjects = useCallback(async (cls: string) => {
    if (!cls) return;
    setLoadingSubjects(true);
    try {
      const res  = await fetch(`/api/subjects?class=${encodeURIComponent(cls)}`, { cache: "no-store" });
      if (!res.ok) { setSubjects([]); return; }
      const json = await res.json();
      setSubjects(json.data ?? []);
    } catch {
      setSubjects([]);
    } finally {
      setLoadingSubjects(false);
    }
  }, []);

  /* ── Fetch ALL subjects (for the Add Subject modal) ── */
  const fetchAllSubjects = useCallback(async () => {
    try {
      const res  = await fetch("/api/subjects", { cache: "no-store" });
      if (!res.ok) { setAllSubjects([]); return; }
      const json = await res.json();
      setAllSubjects(json.data ?? []);
    } catch {
      setAllSubjects([]);
    }
  }, []);

  /* ── Fetch students for a class ── */
  const fetchStudents = useCallback(async (cls: string) => {
    if (!cls) { setStudents([]); return; }
    setLoadingStudents(true);
    try {
      const res  = await fetch(`/api/students?class=${encodeURIComponent(cls)}`, { cache: "no-store" });
      if (!res.ok) { setStudents([]); return; }
      const json = await res.json();
      setStudents(json.data ?? []);
    } catch {
      setStudents([]);
    } finally {
      setLoadingStudents(false);
    }
  }, []);

  useEffect(() => { fetchClasses(); }, [fetchClasses]);
  useEffect(() => {
    if (selectedClass) {
      fetchSubjects(selectedClass);
      fetchStudents(selectedClass);
    }
  }, [selectedClass, fetchSubjects, fetchStudents]);

  /* ── Keep subjectClass dropdown in sync when classes load ── */
  useEffect(() => {
    if (classes.length > 0 && !subjectClass) setSubjectClass(classes[0]);
  }, [classes, subjectClass]);

  /* ── Logout ── */
  async function logout() {
    await fetch("/api/auth/logout", { method: "POST" });
    router.push("/login");
    router.refresh();
  }

  /* ── Add Class ── */
  async function handleAddClass(e: React.FormEvent) {
    e.preventDefault();
    const name = newClassName.trim();
    if (!name) return;
    const safeClasses = classes.filter((c): c is string => typeof c === "string");
    if (safeClasses.map(c => c.toLowerCase()).includes(name.toLowerCase())) {
      setFormError("Class already exists.");
      return;
    }
    setFormLoading(true);
    setFormError("");
    try {
      const res  = await fetch("/api/classes", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ name }),
      });
      const json = await res.json();
      if (res.ok && json.success !== false) {
        await fetchClasses(name);
        setNewClassName("");
        // Keep modal open so user can see the updated list
      } else {
        setFormError(json.message ?? "Failed to add class.");
      }
    } catch {
      setFormError("Network error. Please try again.");
    } finally {
      setFormLoading(false);
    }
  }

  /* ── Rename Class ── */
  async function handleRenameClass(e: React.FormEvent) {
    e.preventDefault();
    const newName = editClassName.trim();
    if (!newName || !editingClass) return;
    if (newName === editingClass) { setEditingClass(null); return; }
    const safeClasses = classes.filter((c): c is string => typeof c === "string");
    if (safeClasses.map(c => c.toLowerCase()).includes(newName.toLowerCase())) {
      setEditError("A class with that name already exists.");
      return;
    }
    setEditLoading(true);
    setEditError("");
    try {
      const res = await fetch(`/api/classes/${encodeURIComponent(editingClass)}`, {
        method: "PUT",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ name: newName }),
      });
      const json = await res.json();
      if (res.ok && json.success !== false) {
        const wasSelected = selectedClass === editingClass;
        await fetchClasses(wasSelected ? newName : undefined);
        setEditingClass(null);
        setEditClassName("");
      } else {
        setEditError(json.message ?? "Failed to rename class.");
      }
    } catch {
      setEditError("Network error. Please try again.");
    } finally {
      setEditLoading(false);
    }
  }

  /* ── Delete Class ── */
  async function handleDeleteClass(name: string) {
    if (!confirm(`Delete class "${name}"? This cannot be undone.`)) return;
    try {
      await fetch(`/api/classes/${encodeURIComponent(name)}`, { method: "DELETE" });
      await fetchClasses();
    } catch { /* ignore */ }
  }

  /* ── Add Subject ── */
  async function handleAddSubject(e: React.FormEvent) {
    e.preventDefault();
    const name = newSubjectName.trim();
    if (!name || !subjectClass) return;
    setFormLoading(true);
    setFormError("");
    try {
      const res  = await fetch("/api/subjects", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ name, class_name: subjectClass }),
      });
      const json = await res.json();
      if (res.ok && json.success !== false) {
        await fetchSubjects(selectedClass);
        await fetchAllSubjects();
        setNewSubjectName("");
        // Keep modal open so user can keep adding subjects
      } else {
        setFormError(json.message ?? "Failed to add subject.");
      }
    } catch {
      setFormError("Network error. Please try again.");
    } finally {
      setFormLoading(false);
    }
  }

  /* ── Delete Subject ── */
  async function handleDeleteSubject(subjectName: string, className: string) {
    if (!confirm(`Delete subject "${subjectName}" from ${className}?`)) return;
    try {
      await fetch(`/api/subjects/${encodeURIComponent(subjectName)}?class=${encodeURIComponent(className)}`, { method: "DELETE" });
      await fetchSubjects(selectedClass);
      await fetchAllSubjects();
    } catch { /* ignore */ }
  }

  /* ── Rename Subject ── */
  async function handleRenameSubject(e: React.FormEvent, oldName: string, className: string) {
    e.preventDefault();
    const newName = editSubjectName.trim();
    if (!newName || newName === oldName) { setEditingSubject(null); return; }
    setEditSubjectLoading(true);
    setEditSubjectError("");
    try {
      const res = await fetch(
        `/api/subjects/${encodeURIComponent(oldName)}?class=${encodeURIComponent(className)}`,
        {
          method: "PUT",
          headers: { "Content-Type": "application/json" },
          body: JSON.stringify({ new_name: newName }),
        }
      );
      const json = await res.json();
      if (res.ok && json.success !== false) {
        await fetchSubjects(selectedClass);
        await fetchAllSubjects();
        setEditingSubject(null);
        setEditSubjectName("");
      } else {
        setEditSubjectError(json.message ?? "Failed to rename subject.");
      }
    } catch {
      setEditSubjectError("Network error. Please try again.");
    } finally {
      setEditSubjectLoading(false);
    }
  }

  /* ── Update Student ── */
  async function handleUpdateStudent(e: React.FormEvent, studentId: string) {
    e.preventDefault();
    const name = editStudentName.trim();
    const email = editStudentEmail.trim();
    if (!name || !email) return;
    setEditStudentLoading(true);
    setEditStudentError("");
    try {
      const res = await fetch(`/api/students/${encodeURIComponent(studentId)}`, {
        method: "PUT",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ name, email }),
      });
      const json = await res.json();
      if (res.ok && json.success !== false) {
        await fetchStudents(selectedClass);
        setEditingStudentId(null);
        setEditStudentName("");
        setEditStudentEmail("");
      } else {
        setEditStudentError(json.message ?? "Failed to update student.");
      }
    } catch {
      setEditStudentError("Network error. Please try again.");
    } finally {
      setEditStudentLoading(false);
    }
  }

  /* ── Delete Student ── */
  async function handleDeleteStudent(studentId: string, studentName: string) {
    if (!confirm(`Delete student "${studentName}" (${studentId})?`)) return;
    try {
      await fetch(`/api/students/${encodeURIComponent(studentId)}`, { method: "DELETE" });
      await fetchStudents(selectedClass);
    } catch { /* ignore */ }
  }

  /* ── Open Student Detail Modal ── */
  async function openStudentDetail(stu: StudentItem) {
    setDetailStudent(stu);
    setDetailScores([]);
    setDetailSubjects([]);
    setDetailLoading(true);
    try {
      const className = stu.class_name || selectedClass;
      const [scoresRes, subjectsRes] = await Promise.all([
        fetch(`/api/students/${encodeURIComponent(stu.student_id)}/scores`, { cache: "no-store" }),
        fetch(`/api/subjects?class=${encodeURIComponent(className)}`, { cache: "no-store" }),
      ]);
      if (scoresRes.ok) {
        const scoresJson = await scoresRes.json();
        // Scores may be wrapped in { data: [...] } or be raw array
        const rawScores = scoresJson.data ?? scoresJson ?? [];
        // Normalise score values (backend may use canonical extended JSON: { "$numberDouble": "85.5" })
        const normalised: ScoreItem[] = (Array.isArray(rawScores) ? rawScores : []).map((s: Record<string, unknown>) => ({
          student_id: typeof s.student_id === "string" ? s.student_id : String(s.student_id ?? ""),
          subject: typeof s.subject === "string" ? s.subject : String(s.subject ?? ""),
          score: typeof s.score === "number"
            ? s.score
            : typeof s.score === "object" && s.score !== null && "$numberDouble" in (s.score as Record<string, unknown>)
              ? parseFloat((s.score as Record<string, string>)["$numberDouble"])
              : parseFloat(String(s.score ?? "0")),
        }));
        setDetailScores(normalised);
      }
      if (subjectsRes.ok) {
        const subjectsJson = await subjectsRes.json();
        setDetailSubjects(subjectsJson.data ?? []);
      }
    } catch { /* ignore */ }
    finally { setDetailLoading(false); }
  }

  /* ── Helper: re-fetch scores for the currently open student ── */
  async function refreshDetailScores(studentId: string) {
    try {
      const res = await fetch(`/api/students/${encodeURIComponent(studentId)}/scores`, { cache: "no-store" });
      if (res.ok) {
        const json = await res.json();
        const rawScores = json.data ?? json ?? [];
        const normalised: ScoreItem[] = (Array.isArray(rawScores) ? rawScores : []).map((s: Record<string, unknown>) => ({
          student_id: typeof s.student_id === "string" ? s.student_id : String(s.student_id ?? ""),
          subject: typeof s.subject === "string" ? s.subject : String(s.subject ?? ""),
          score: typeof s.score === "number"
            ? s.score
            : typeof s.score === "object" && s.score !== null && "$numberDouble" in (s.score as Record<string, unknown>)
              ? parseFloat((s.score as Record<string, string>)["$numberDouble"])
              : parseFloat(String(s.score ?? "0")),
        }));
        setDetailScores(normalised);
      }
    } catch { /* ignore */ }
  }

  /* ── Add Score (from detail modal) ── */
  async function handleAddScore(e: React.FormEvent) {
    e.preventDefault();
    if (!detailStudent || !addScoreSubject || !addScoreValue) return;
    const scoreNum = parseFloat(addScoreValue);
    if (isNaN(scoreNum)) { setAddScoreError("Score must be a number."); return; }
    setAddScoreLoading(true);
    setAddScoreError("");
    try {
      const res = await fetch(`/api/students/${encodeURIComponent(detailStudent.student_id)}/scores`, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ subject: addScoreSubject, score: scoreNum }),
      });
      const json = await res.json();
      if (res.ok && json.success !== false) {
        await refreshDetailScores(detailStudent.student_id);
        setAddScoreSubject("");
        setAddScoreValue("");
      } else {
        setAddScoreError(json.message ?? "Failed to add score.");
      }
    } catch { setAddScoreError("Network error."); }
    finally { setAddScoreLoading(false); }
  }

  /* ── Update Score (inline edit in detail modal) ── */
  async function handleUpdateScore(e: React.FormEvent, subject: string) {
    e.preventDefault();
    if (!detailStudent || !editScoreValue) return;
    const scoreNum = parseFloat(editScoreValue);
    if (isNaN(scoreNum)) { setEditScoreError("Score must be a number."); return; }
    setEditScoreLoading(true);
    setEditScoreError("");
    try {
      const res = await fetch(`/api/students/${encodeURIComponent(detailStudent.student_id)}/scores`, {
        method: "PUT",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ subject, score: scoreNum }),
      });
      const json = await res.json();
      if (res.ok && json.success !== false) {
        await refreshDetailScores(detailStudent.student_id);
        setEditingScoreSubject(null);
        setEditScoreValue("");
      } else {
        setEditScoreError(json.message ?? "Failed to update score.");
      }
    } catch { setEditScoreError("Network error."); }
    finally { setEditScoreLoading(false); }
  }

  /* ── Delete Score ── */
  async function handleDeleteScore(subject: string) {
    if (!detailStudent) return;
    if (!confirm(`Delete score for "${subject}"?`)) return;
    try {
      await fetch(`/api/students/${encodeURIComponent(detailStudent.student_id)}/scores?subject=${encodeURIComponent(subject)}`, { method: "DELETE" });
      await refreshDetailScores(detailStudent.student_id);
    } catch { /* ignore */ }
  }

  /* ── Generate next Student ID ── */
  async function generateNextStudentId(): Promise<string> {
    try {
      // Fetch ALL students (no class filter) to find the globally highest ID
      const res = await fetch("/api/students", { cache: "no-store" });
      if (!res.ok) return "S00001";
      const json = await res.json();
      const allStudents: StudentItem[] = json.data ?? [];
      if (allStudents.length === 0) return "S00001";

      // Extract numeric part from IDs like "S00001", "S123", etc.
      let maxNum = 0;
      for (const stu of allStudents) {
        const id = stu.student_id ?? "";
        const match = id.match(/^S(\d+)$/i);
        if (match) {
          const num = parseInt(match[1], 10);
          if (num > maxNum) maxNum = num;
        }
      }
      const next = maxNum + 1;
      return `S${String(next).padStart(5, "0")}`;
    } catch {
      return "S00001";
    }
  }

  /* ── Open Add Student modal with auto-generated ID ── */
  async function openAddStudentModal() {
    setFormError("");
    setNewStudentName("");
    setNewStudentEmail("");
    setNewStudentId("Generating…");
    setShowAddStudent(true);
    const nextId = await generateNextStudentId();
    setNewStudentId(nextId);
  }

  /* ── Add Student ── */
  async function handleAddStudent(e: React.FormEvent) {
    e.preventDefault();
    const name = newStudentName.trim();
    const email = newStudentEmail.trim();
    const sid = newStudentId.trim();
    if (!name || !email || !sid) return;
    setFormLoading(true);
    setFormError("");
    try {
      const res = await fetch("/api/students", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ name, email, student_id: sid, class_name: selectedClass }),
      });
      const json = await res.json();
      if (res.ok && json.success !== false) {
        await fetchStudents(selectedClass);
        setNewStudentName("");
        setNewStudentEmail("");
        setNewStudentId("");
        setShowAddStudent(false);
      } else {
        setFormError(json.message ?? "Failed to add student.");
      }
    } catch {
      setFormError("Network error. Please try again.");
    } finally {
      setFormLoading(false);
    }
  }

  /* ── Subject list for selected class ── */

  return (
    <div className="min-h-screen bg-black text-white">
      {/* Background glows */}
      <div className="fixed inset-0 overflow-hidden pointer-events-none">
        <div className="absolute -top-60 -left-60 w-125 h-125 bg-indigo-900/15 rounded-full blur-3xl" />
        <div className="absolute -bottom-60 -right-60 w-125 h-125 bg-purple-900/15 rounded-full blur-3xl" />
      </div>

      {/* ── Add / Manage Classes Modal ── */}
      {showAddClass && (
        <Modal title="Classes" onClose={() => { setShowAddClass(false); setNewClassName(""); setFormError(""); setEditingClass(null); setEditClassName(""); setEditError(""); }}>
          {/* ── Add new class form ── */}
          <form onSubmit={handleAddClass} className="space-y-3 mb-5">
            <label className="block text-xs font-medium text-zinc-400 uppercase tracking-wider">New Class Name</label>
            <div className="flex gap-2">
              <input
                type="text"
                value={newClassName}
                onChange={(e) => { setNewClassName(e.target.value); setFormError(""); }}
                placeholder="e.g. Class E"
                autoFocus
                className="flex-1 bg-zinc-800 border border-zinc-700 focus:border-indigo-500 focus:ring-1 focus:ring-indigo-500 rounded-lg px-4 py-2.5 text-white placeholder-zinc-500 text-sm outline-none transition-colors"
              />
              <button
                type="submit"
                disabled={!newClassName.trim() || formLoading}
                className="bg-indigo-600 hover:bg-indigo-500 disabled:opacity-40 disabled:cursor-not-allowed text-white font-semibold px-4 py-2.5 rounded-lg text-sm transition-colors shrink-0"
              >
                {formLoading ? "Adding…" : "Add"}
              </button>
            </div>
            {formError && <p className="text-red-400 text-xs">{formError}</p>}
          </form>

          {/* ── Existing classes list with edit / delete ── */}
          <div>
            <p className="text-xs text-zinc-500 font-semibold uppercase tracking-wider mb-2">Existing Classes</p>
            <div className="space-y-2 max-h-64 overflow-y-auto pr-1">
              {classes.length === 0 ? (
                <p className="text-zinc-600 text-xs text-center py-4">No classes yet.</p>
              ) : (
                classes.map((c) => (
                  <div key={`manage-${c}`} className="bg-zinc-800 border border-zinc-700 rounded-xl px-3 py-2">
                    {editingClass === c ? (
                      <>
                        <form onSubmit={handleRenameClass} className="flex items-center gap-2">
                          <input
                            type="text"
                            value={editClassName}
                            onChange={(e) => { setEditClassName(e.target.value); setEditError(""); }}
                            autoFocus
                            title="Rename class"
                            placeholder="New name"
                            className="flex-1 bg-zinc-700 border border-zinc-600 focus:border-indigo-500 rounded-lg px-3 py-1.5 text-white text-sm outline-none"
                          />
                          <button
                            type="submit"
                            disabled={!editClassName.trim() || editLoading}
                            className="bg-indigo-600 hover:bg-indigo-500 disabled:opacity-40 text-white text-xs font-semibold px-3 py-1.5 rounded-lg transition-colors"
                          >
                            {editLoading ? "…" : "Save"}
                          </button>
                          <button
                            type="button"
                            onClick={() => { setEditingClass(null); setEditClassName(""); setEditError(""); }}
                            className="text-zinc-500 hover:text-zinc-300 text-xs px-2 py-1.5 rounded-lg transition-colors"
                          >
                            Cancel
                          </button>
                        </form>
                        {editError && <p className="text-red-400 text-xs mt-1.5 px-1">{editError}</p>}
                      </>
                    ) : (
                      <div className="flex items-center justify-between gap-2">
                        <span className="text-sm text-zinc-200 font-medium truncate">{c}</span>
                        <div className="flex items-center gap-1 shrink-0">
                          <button
                            type="button"
                            onClick={() => { setEditingClass(c); setEditClassName(c); setEditError(""); }}
                            title={`Rename ${c}`}
                            className="text-zinc-500 hover:text-indigo-400 p-1.5 rounded-lg hover:bg-zinc-700 transition-colors"
                          >
                            <svg className="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                              <path strokeLinecap="round" strokeLinejoin="round" strokeWidth={2} d="M11 5H6a2 2 0 00-2 2v11a2 2 0 002 2h11a2 2 0 002-2v-5m-1.414-9.414a2 2 0 112.828 2.828L11.828 15H9v-2.828l8.586-8.586z" />
                            </svg>
                          </button>
                          <button
                            type="button"
                            onClick={() => handleDeleteClass(c)}
                            title={`Delete ${c}`}
                            className="text-zinc-500 hover:text-red-400 p-1.5 rounded-lg hover:bg-zinc-700 transition-colors"
                          >
                            <svg className="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                              <path strokeLinecap="round" strokeLinejoin="round" strokeWidth={2} d="M19 7l-.867 12.142A2 2 0 0116.138 21H7.862a2 2 0 01-1.995-1.858L5 7m5 4v6m4-6v6m1-10V4a1 1 0 00-1-1h-4a1 1 0 00-1 1v3M4 7h16" />
                            </svg>
                          </button>
                        </div>
                      </div>
                    )}
                  </div>
                ))
              )}
            </div>
          </div>

          <div className="mt-5 flex justify-end">
            <button
              type="button"
              onClick={() => { setShowAddClass(false); setNewClassName(""); setFormError(""); setEditingClass(null); setEditClassName(""); setEditError(""); }}
              className="bg-zinc-800 hover:bg-zinc-700 text-zinc-300 font-semibold px-5 py-2.5 rounded-xl text-sm transition-colors"
            >
              Done
            </button>
          </div>
        </Modal>
      )}

      {/* ── Subjects Modal (Add / Manage) ── */}
      {showAddSubject && (
        <Modal title="Subjects" onClose={() => { setShowAddSubject(false); setNewSubjectName(""); setFormError(""); setEditingSubject(null); setEditSubjectName(""); setEditSubjectError(""); }}>
          {/* Class selector */}
          <div className="mb-4">
            <label className="block text-xs font-medium text-zinc-400 mb-1.5 uppercase tracking-wider">Class</label>
            <select
              title="Select Class"
              value={subjectClass}
              onChange={(e) => { setSubjectClass(e.target.value); fetchAllSubjects(); }}
              className="w-full bg-zinc-800 border border-zinc-700 focus:border-emerald-500 focus:ring-1 focus:ring-emerald-500 rounded-lg px-4 py-2.5 text-white text-sm outline-none transition-colors"
            >
              {classes.map((c) => <option key={`opt-${c}`} value={c}>{c}</option>)}
            </select>
          </div>

          {/* Add new subject form */}
          <form onSubmit={handleAddSubject} className="space-y-3 mb-5">
            <label className="block text-xs font-medium text-zinc-400 uppercase tracking-wider">New Subject Name</label>
            <div className="flex gap-2">
              <input
                type="text"
                value={newSubjectName}
                onChange={(e) => { setNewSubjectName(e.target.value); setFormError(""); }}
                placeholder="e.g. Physics"
                autoFocus
                className="flex-1 bg-zinc-800 border border-zinc-700 focus:border-emerald-500 focus:ring-1 focus:ring-emerald-500 rounded-lg px-4 py-2.5 text-white placeholder-zinc-500 text-sm outline-none transition-colors"
              />
              <button
                type="submit"
                disabled={!newSubjectName.trim() || !subjectClass || formLoading}
                className="bg-emerald-700 hover:bg-emerald-600 disabled:opacity-40 disabled:cursor-not-allowed text-white font-semibold px-4 py-2.5 rounded-lg text-sm transition-colors shrink-0"
              >
                {formLoading ? "Adding…" : "Add"}
              </button>
            </div>
            {formError && <p className="text-red-400 text-xs">{formError}</p>}
          </form>

          {/* Existing subjects list with delete */}
          <div>
            <p className="text-xs text-zinc-500 font-semibold uppercase tracking-wider mb-2">
              Existing Subjects {subjectClass ? `— ${subjectClass}` : ""}
            </p>
            <div className="space-y-2 max-h-52 overflow-y-auto pr-1">
              {allSubjects.filter(s => s.class_name === subjectClass).length === 0 ? (
                <p className="text-zinc-600 text-xs text-center py-4">No subjects yet for this class.</p>
              ) : (
                allSubjects.filter(s => s.class_name === subjectClass).map((s) => {
                  const subjectKey = `${s.class_name}__${s.name}`;
                  return (
                  <div key={`subj-${s.class_name}__${s.name}`} className="bg-zinc-800 border border-zinc-700 rounded-xl px-3 py-2">
                    {editingSubject === subjectKey ? (
                      <>
                        <form onSubmit={(e) => handleRenameSubject(e, s.name, s.class_name)} className="flex items-center gap-2">
                          <input
                            type="text"
                            value={editSubjectName}
                            onChange={(e) => { setEditSubjectName(e.target.value); setEditSubjectError(""); }}
                            autoFocus
                            title="Rename subject"
                            placeholder="New name"
                            className="flex-1 bg-zinc-700 border border-zinc-600 focus:border-emerald-500 rounded-lg px-3 py-1.5 text-white text-sm outline-none"
                          />
                          <button
                            type="submit"
                            disabled={!editSubjectName.trim() || editSubjectLoading}
                            className="bg-emerald-700 hover:bg-emerald-600 disabled:opacity-40 text-white text-xs font-semibold px-3 py-1.5 rounded-lg transition-colors"
                          >
                            {editSubjectLoading ? "…" : "Save"}
                          </button>
                          <button
                            type="button"
                            onClick={() => { setEditingSubject(null); setEditSubjectName(""); setEditSubjectError(""); }}
                            className="text-zinc-500 hover:text-zinc-300 text-xs px-2 py-1.5 rounded-lg transition-colors"
                          >
                            Cancel
                          </button>
                        </form>
                        {editSubjectError && <p className="text-red-400 text-xs mt-1.5 px-1">{editSubjectError}</p>}
                      </>
                    ) : (
                      <div className="flex items-center justify-between gap-2">
                        <span className="text-sm text-zinc-200 font-medium truncate">{s.name}</span>
                        <div className="flex items-center gap-1 shrink-0">
                          <button
                            type="button"
                            onClick={() => { setEditingSubject(subjectKey); setEditSubjectName(s.name); setEditSubjectError(""); }}
                            title={`Rename ${s.name}`}
                            className="text-zinc-500 hover:text-emerald-400 p-1.5 rounded-lg hover:bg-zinc-700 transition-colors"
                          >
                            <svg className="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                              <path strokeLinecap="round" strokeLinejoin="round" strokeWidth={2} d="M11 5H6a2 2 0 00-2 2v11a2 2 0 002 2h11a2 2 0 002-2v-5m-1.414-9.414a2 2 0 112.828 2.828L11.828 15H9v-2.828l8.586-8.586z" />
                            </svg>
                          </button>
                          <button
                            type="button"
                            onClick={() => handleDeleteSubject(s.name, s.class_name)}
                            title={`Delete ${s.name}`}
                            className="text-zinc-500 hover:text-red-400 p-1.5 rounded-lg hover:bg-zinc-700 transition-colors shrink-0"
                          >
                            <svg className="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                              <path strokeLinecap="round" strokeLinejoin="round" strokeWidth={2} d="M19 7l-.867 12.142A2 2 0 0116.138 21H7.862a2 2 0 01-1.995-1.858L5 7m5 4v6m4-6v6m1-10V4a1 1 0 00-1-1h-4a1 1 0 00-1 1v3M4 7h16" />
                            </svg>
                          </button>
                        </div>
                      </div>
                    )}
                  </div>
                  );
                })
              )}
            </div>
          </div>

          <div className="mt-5 flex justify-end">
            <button
              type="button"
              onClick={() => { setShowAddSubject(false); setNewSubjectName(""); setFormError(""); setEditingSubject(null); setEditSubjectName(""); setEditSubjectError(""); }}
              className="bg-zinc-800 hover:bg-zinc-700 text-zinc-300 font-semibold px-5 py-2.5 rounded-xl text-sm transition-colors"
            >
              Done
            </button>
          </div>
        </Modal>
      )}

      {/* ── Add Student Modal ── */}
      {showAddStudent && (
        <Modal title={`Add Student — ${selectedClass}`} onClose={() => { setShowAddStudent(false); setNewStudentName(""); setNewStudentEmail(""); setNewStudentId(""); setFormError(""); }}>
          <form onSubmit={handleAddStudent} className="space-y-4">
            <div>
              <label className="block text-xs font-medium text-zinc-400 mb-1.5 uppercase tracking-wider">Student ID (auto-generated)</label>
              <input
                type="text"
                value={newStudentId}
                readOnly
                tabIndex={-1}
                title="Auto-generated Student ID"
                className="w-full bg-zinc-800/50 border border-zinc-700/50 rounded-lg px-4 py-2.5 text-indigo-300 font-mono text-sm outline-none cursor-default"
              />
            </div>
            <div>
              <label className="block text-xs font-medium text-zinc-400 mb-1.5 uppercase tracking-wider">Name</label>
              <input
                type="text"
                value={newStudentName}
                onChange={(e) => { setNewStudentName(e.target.value); setFormError(""); }}
                placeholder="e.g. John Doe"
                autoFocus
                className="w-full bg-zinc-800 border border-zinc-700 focus:border-amber-500 focus:ring-1 focus:ring-amber-500 rounded-lg px-4 py-2.5 text-white placeholder-zinc-500 text-sm outline-none transition-colors"
              />
            </div>
            <div>
              <label className="block text-xs font-medium text-zinc-400 mb-1.5 uppercase tracking-wider">Email</label>
              <input
                type="email"
                value={newStudentEmail}
                onChange={(e) => { setNewStudentEmail(e.target.value); setFormError(""); }}
                placeholder="e.g. john@example.com"
                className="w-full bg-zinc-800 border border-zinc-700 focus:border-amber-500 focus:ring-1 focus:ring-amber-500 rounded-lg px-4 py-2.5 text-white placeholder-zinc-500 text-sm outline-none transition-colors"
              />
            </div>
            {formError && <p className="text-red-400 text-xs">{formError}</p>}
            <div className="flex gap-3 pt-1">
              <button
                type="button"
                onClick={() => { setShowAddStudent(false); setNewStudentName(""); setNewStudentEmail(""); setNewStudentId(""); setFormError(""); }}
                className="flex-1 bg-zinc-800 hover:bg-zinc-700 text-zinc-300 font-semibold py-2.5 rounded-xl text-sm transition-colors"
              >
                Cancel
              </button>
              <button
                type="submit"
                disabled={!newStudentName.trim() || !newStudentEmail.trim() || !newStudentId.trim() || newStudentId === "Generating…" || formLoading}
                className="flex-1 bg-amber-700 hover:bg-amber-600 disabled:opacity-40 disabled:cursor-not-allowed text-white font-semibold py-2.5 rounded-xl text-sm transition-colors"
              >
                {formLoading ? "Adding…" : "Add Student"}
              </button>
            </div>
          </form>
        </Modal>
      )}

      {/* ── Student Detail Modal ── */}
      {detailStudent && (
        <Modal title="Student Details" onClose={() => setDetailStudent(null)} wide>
          {detailLoading ? (
            <div className="flex items-center justify-center py-12">
              <div className="w-6 h-6 border-2 border-indigo-500 border-t-transparent rounded-full animate-spin" />
              <span className="ml-3 text-zinc-400 text-sm">Loading…</span>
            </div>
          ) : (
            <div className="space-y-5">
              {/* Student info */}
              <div className="bg-zinc-800/60 border border-zinc-700/50 rounded-xl p-4 space-y-2.5">
                <div className="flex items-center gap-3">
                  <div className="w-10 h-10 rounded-full bg-linear-to-br from-indigo-600 to-purple-600 flex items-center justify-center text-white font-bold text-sm shadow shadow-indigo-900/40">
                    {detailStudent.name.charAt(0).toUpperCase()}
                  </div>
                  <div>
                    <p className="text-white font-semibold text-sm">{detailStudent.name}</p>
                    <p className="text-zinc-500 text-xs">{detailStudent.email}</p>
                  </div>
                </div>
                <div className="grid grid-cols-2 gap-3 pt-1">
                  <div className="bg-zinc-900/50 rounded-lg px-3 py-2">
                    <p className="text-[10px] text-zinc-500 uppercase tracking-wider font-semibold">Student ID</p>
                    <p className="text-indigo-300 text-sm font-mono mt-0.5">{detailStudent.student_id}</p>
                  </div>
                  <div className="bg-zinc-900/50 rounded-lg px-3 py-2">
                    <p className="text-[10px] text-zinc-500 uppercase tracking-wider font-semibold">Class</p>
                    <p className="text-amber-300 text-sm font-medium mt-0.5">{detailStudent.class_name || selectedClass}</p>
                  </div>
                </div>
              </div>

              {/* Subjects & Scores */}
              <div>
                <p className="text-xs text-zinc-500 font-semibold uppercase tracking-wider mb-2.5">Subjects &amp; Marks</p>

                {/* Add score form */}
                <form onSubmit={handleAddScore} className="mb-3 bg-zinc-800/40 border border-zinc-700/50 rounded-xl p-3">
                  <div className="flex items-end gap-2">
                    <div className="flex-1">
                      <label className="block text-[10px] text-zinc-500 uppercase tracking-wider font-semibold mb-1">Subject</label>
                      <select
                        title="Select subject"
                        value={addScoreSubject}
                        onChange={(e) => { setAddScoreSubject(e.target.value); setAddScoreError(""); }}
                        className="w-full bg-zinc-700 border border-zinc-600 focus:border-cyan-500 rounded-lg px-3 py-1.5 text-white text-sm outline-none"
                      >
                        <option value="">Choose…</option>
                        {detailSubjects
                          .filter(sub => !detailScores.some(sc => sc.subject.toLowerCase() === sub.name.toLowerCase()))
                          .map(sub => <option key={`add-sc-${sub.name}`} value={sub.name}>{sub.name}</option>)}
                      </select>
                    </div>
                    <div className="w-24">
                      <label className="block text-[10px] text-zinc-500 uppercase tracking-wider font-semibold mb-1">Score</label>
                      <input
                        type="number"
                        step="0.1"
                        min="0"
                        max="100"
                        value={addScoreValue}
                        onChange={(e) => { setAddScoreValue(e.target.value); setAddScoreError(""); }}
                        placeholder="0–100"
                        title="Score value"
                        className="w-full bg-zinc-700 border border-zinc-600 focus:border-cyan-500 rounded-lg px-3 py-1.5 text-white text-sm outline-none"
                      />
                    </div>
                    <button
                      type="submit"
                      disabled={!addScoreSubject || !addScoreValue || addScoreLoading}
                      className="bg-cyan-700 hover:bg-cyan-600 disabled:opacity-40 disabled:cursor-not-allowed text-white text-xs font-semibold px-3 py-1.5 rounded-lg transition-colors shrink-0"
                    >
                      {addScoreLoading ? "…" : "+ Add"}
                    </button>
                  </div>
                  {addScoreError && <p className="text-red-400 text-xs mt-1.5">{addScoreError}</p>}
                </form>

                {detailSubjects.length === 0 && detailScores.length === 0 ? (
                  <p className="text-zinc-600 text-xs text-center py-6 bg-zinc-800/40 rounded-xl border border-dashed border-zinc-700">
                    No subjects or scores found for this student.
                  </p>
                ) : (
                  <div className="space-y-2 max-h-64 overflow-y-auto pr-1">
                    {/* Show every subject for the class; if the student has a score for it, display it */}
                    {detailSubjects.map((subj) => {
                      const matchingScore = detailScores.find(
                        (sc) => sc.subject.toLowerCase() === subj.name.toLowerCase()
                      );
                      const scoreVal = matchingScore?.score;
                      const hasScore = scoreVal !== undefined && scoreVal !== null;
                      const pct = hasScore ? scoreVal : null;
                      const isEditing = editingScoreSubject === subj.name;

                      // Color coding
                      let barColor = "bg-zinc-700";
                      let textColor = "text-zinc-500";
                      if (hasScore && pct !== null) {
                        if (pct >= 80) { barColor = "bg-emerald-600"; textColor = "text-emerald-400"; }
                        else if (pct >= 60) { barColor = "bg-blue-600"; textColor = "text-blue-400"; }
                        else if (pct >= 40) { barColor = "bg-amber-600"; textColor = "text-amber-400"; }
                        else { barColor = "bg-red-600"; textColor = "text-red-400"; }
                      }

                      return (
                        <div
                          key={`detail-subj-${subj.name}`}
                          className="bg-zinc-800 border border-zinc-700/60 rounded-xl px-4 py-3"
                        >
                          {isEditing ? (
                            <>
                              <form onSubmit={(e) => handleUpdateScore(e, subj.name)} className="flex items-center gap-2">
                                <span className="text-sm text-zinc-200 font-medium flex-1 truncate">{subj.name}</span>
                                <input
                                  type="number"
                                  step="0.1"
                                  min="0"
                                  max="100"
                                  value={editScoreValue}
                                  onChange={(e) => { setEditScoreValue(e.target.value); setEditScoreError(""); }}
                                  autoFocus
                                  title="Edit score"
                                  placeholder="Score"
                                  className="w-20 bg-zinc-700 border border-zinc-600 focus:border-cyan-500 rounded-lg px-2 py-1 text-white text-sm outline-none text-right"
                                />
                                <button
                                  type="submit"
                                  disabled={!editScoreValue || editScoreLoading}
                                  className="bg-cyan-700 hover:bg-cyan-600 disabled:opacity-40 text-white text-xs font-semibold px-2.5 py-1 rounded-lg transition-colors"
                                >
                                  {editScoreLoading ? "…" : "Save"}
                                </button>
                                <button
                                  type="button"
                                  onClick={() => { setEditingScoreSubject(null); setEditScoreValue(""); setEditScoreError(""); }}
                                  className="text-zinc-500 hover:text-zinc-300 text-xs px-1.5 py-1 rounded-lg transition-colors"
                                >
                                  ✕
                                </button>
                              </form>
                              {editScoreError && <p className="text-red-400 text-xs mt-1">{editScoreError}</p>}
                            </>
                          ) : (
                            <>
                              <div className="flex items-center justify-between mb-1.5">
                                <span className="text-sm text-zinc-200 font-medium">{subj.name}</span>
                                <div className="flex items-center gap-1.5">
                                  <span className={`text-sm font-bold tabular-nums ${hasScore ? textColor : "text-zinc-600 italic"}`}>
                                    {hasScore ? `${pct!.toFixed(1)}` : "—"}
                                  </span>
                                  {hasScore && (
                                    <>
                                      <button
                                        type="button"
                                        onClick={() => { setEditingScoreSubject(subj.name); setEditScoreValue(String(pct!)); setEditScoreError(""); }}
                                        title={`Edit ${subj.name} score`}
                                        className="text-zinc-600 hover:text-cyan-400 p-0.5 rounded transition-colors"
                                      >
                                        <svg className="w-3.5 h-3.5" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                                          <path strokeLinecap="round" strokeLinejoin="round" strokeWidth={2} d="M11 5H6a2 2 0 00-2 2v11a2 2 0 002 2h11a2 2 0 002-2v-5m-1.414-9.414a2 2 0 112.828 2.828L11.828 15H9v-2.828l8.586-8.586z" />
                                        </svg>
                                      </button>
                                      <button
                                        type="button"
                                        onClick={() => handleDeleteScore(subj.name)}
                                        title={`Delete ${subj.name} score`}
                                        className="text-zinc-600 hover:text-red-400 p-0.5 rounded transition-colors"
                                      >
                                        <svg className="w-3.5 h-3.5" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                                          <path strokeLinecap="round" strokeLinejoin="round" strokeWidth={2} d="M19 7l-.867 12.142A2 2 0 0116.138 21H7.862a2 2 0 01-1.995-1.858L5 7m5 4v6m4-6v6m1-10V4a1 1 0 00-1-1h-4a1 1 0 00-1 1v3M4 7h16" />
                                        </svg>
                                      </button>
                                    </>
                                  )}
                                </div>
                              </div>
                              {/* Progress bar */}
                              <div className="w-full bg-zinc-700/40 rounded-full h-1.5 overflow-hidden">
                                <div
                                  className={`h-full rounded-full transition-all duration-500 ${barColor}`}
                                  style={{ width: hasScore ? `${Math.min(pct!, 100)}%` : "0%" }}
                                />
                              </div>
                            </>
                          )}
                        </div>
                      );
                    })}

                    {/* Show scores for subjects NOT in the class subjects list (edge case) */}
                    {detailScores
                      .filter(
                        (sc) => !detailSubjects.some(
                          (subj) => subj.name.toLowerCase() === sc.subject.toLowerCase()
                        )
                      )
                      .map((sc) => {
                        const pct = sc.score;
                        const isEditing = editingScoreSubject === sc.subject;
                        let barColor = "bg-zinc-700";
                        let textColor = "text-zinc-500";
                        if (pct >= 80) { barColor = "bg-emerald-600"; textColor = "text-emerald-400"; }
                        else if (pct >= 60) { barColor = "bg-blue-600"; textColor = "text-blue-400"; }
                        else if (pct >= 40) { barColor = "bg-amber-600"; textColor = "text-amber-400"; }
                        else { barColor = "bg-red-600"; textColor = "text-red-400"; }

                        return (
                          <div
                            key={`detail-extra-${sc.subject}`}
                            className="bg-zinc-800 border border-zinc-700/60 rounded-xl px-4 py-3"
                          >
                            {isEditing ? (
                              <>
                                <form onSubmit={(e) => handleUpdateScore(e, sc.subject)} className="flex items-center gap-2">
                                  <span className="text-sm text-zinc-200 font-medium flex-1 truncate">{sc.subject}</span>
                                  <input
                                    type="number"
                                    step="0.1"
                                    min="0"
                                    max="100"
                                    value={editScoreValue}
                                    onChange={(e) => { setEditScoreValue(e.target.value); setEditScoreError(""); }}
                                    autoFocus
                                    title="Edit score"
                                    placeholder="Score"
                                    className="w-20 bg-zinc-700 border border-zinc-600 focus:border-cyan-500 rounded-lg px-2 py-1 text-white text-sm outline-none text-right"
                                  />
                                  <button
                                    type="submit"
                                    disabled={!editScoreValue || editScoreLoading}
                                    className="bg-cyan-700 hover:bg-cyan-600 disabled:opacity-40 text-white text-xs font-semibold px-2.5 py-1 rounded-lg transition-colors"
                                  >
                                    {editScoreLoading ? "…" : "Save"}
                                  </button>
                                  <button
                                    type="button"
                                    onClick={() => { setEditingScoreSubject(null); setEditScoreValue(""); setEditScoreError(""); }}
                                    className="text-zinc-500 hover:text-zinc-300 text-xs px-1.5 py-1 rounded-lg transition-colors"
                                  >
                                    ✕
                                  </button>
                                </form>
                                {editScoreError && <p className="text-red-400 text-xs mt-1">{editScoreError}</p>}
                              </>
                            ) : (
                              <>
                                <div className="flex items-center justify-between mb-1.5">
                                  <span className="text-sm text-zinc-200 font-medium">{sc.subject}</span>
                                  <div className="flex items-center gap-1.5">
                                    <span className={`text-sm font-bold tabular-nums ${textColor}`}>
                                      {pct.toFixed(1)}
                                    </span>
                                    <button
                                      type="button"
                                      onClick={() => { setEditingScoreSubject(sc.subject); setEditScoreValue(String(pct)); setEditScoreError(""); }}
                                      title={`Edit ${sc.subject} score`}
                                      className="text-zinc-600 hover:text-cyan-400 p-0.5 rounded transition-colors"
                                    >
                                      <svg className="w-3.5 h-3.5" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                                        <path strokeLinecap="round" strokeLinejoin="round" strokeWidth={2} d="M11 5H6a2 2 0 00-2 2v11a2 2 0 002 2h11a2 2 0 002-2v-5m-1.414-9.414a2 2 0 112.828 2.828L11.828 15H9v-2.828l8.586-8.586z" />
                                      </svg>
                                    </button>
                                    <button
                                      type="button"
                                      onClick={() => handleDeleteScore(sc.subject)}
                                      title={`Delete ${sc.subject} score`}
                                      className="text-zinc-600 hover:text-red-400 p-0.5 rounded transition-colors"
                                    >
                                      <svg className="w-3.5 h-3.5" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                                        <path strokeLinecap="round" strokeLinejoin="round" strokeWidth={2} d="M19 7l-.867 12.142A2 2 0 0116.138 21H7.862a2 2 0 01-1.995-1.858L5 7m5 4v6m4-6v6m1-10V4a1 1 0 00-1-1h-4a1 1 0 00-1 1v3M4 7h16" />
                                      </svg>
                                    </button>
                                  </div>
                                </div>
                                <div className="w-full bg-zinc-700/40 rounded-full h-1.5 overflow-hidden">
                                  <div
                                    className={`h-full rounded-full transition-all duration-500 ${barColor}`}
                                    style={{ width: `${Math.min(pct, 100)}%` }}
                                  />
                                </div>
                              </>
                            )}
                          </div>
                        );
                      })}
                  </div>
                )}


              </div>

              {/* Close */}
              <div className="flex justify-end pt-1">
                <button
                  type="button"
                  onClick={() => setDetailStudent(null)}
                  className="bg-zinc-800 hover:bg-zinc-700 text-zinc-300 font-semibold px-5 py-2.5 rounded-xl text-sm transition-colors"
                >
                  Close
                </button>
              </div>
            </div>
          )}
        </Modal>
      )}

      {/* ── Navbar ── */}
      <nav className="sticky top-0 z-40 w-full bg-zinc-900/80 backdrop-blur-md border-b border-zinc-800">
        <div className="max-w-6xl mx-auto px-4 py-3 flex flex-wrap items-center gap-3">
          {/* Brand */}
          <div className="flex items-center mr-4">
            <img src="/swiftscore-logo.svg" alt="SwiftScore" className="h-10 w-auto" />
          </div>

          {/* Add Class */}
          <button
            onClick={() => { setFormError(""); setNewClassName(""); setEditingClass(null); setEditError(""); setShowAddClass(true); }}
            className="flex items-center gap-1.5 bg-indigo-600 hover:bg-indigo-500 active:scale-95 px-3.5 py-2 rounded-lg text-sm font-semibold transition-all duration-150 shadow shadow-indigo-900/40"
          >
            <svg className="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24">
              <path strokeLinecap="round" strokeLinejoin="round" strokeWidth={2} d="M12 4v16m8-8H4" />
            </svg>
            Add Class
          </button>

          {/* Add Subjects */}
          <button
            onClick={() => { setFormError(""); setSubjectClass(selectedClass || classes[0] || ""); fetchAllSubjects(); setShowAddSubject(true); }}
            disabled={classes.length === 0}
            className="flex items-center gap-1.5 bg-emerald-700 hover:bg-emerald-600 disabled:opacity-40 disabled:cursor-not-allowed active:scale-95 px-3.5 py-2 rounded-lg text-sm font-semibold transition-all duration-150 shadow shadow-emerald-900/40"
          >
            <svg className="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24">
              <path strokeLinecap="round" strokeLinejoin="round" strokeWidth={2}
                d="M12 6.253v13m0-13C10.832 5.477 9.246 5 7.5 5S4.168 5.477 3 6.253v13C4.168 18.477 5.754 18 7.5 18s3.332.477 4.5 1.253m0-13C13.168 5.477 14.754 5 16.5 5c1.747 0 3.332.477 4.5 1.253v13C19.832 18.477 18.247 18 16.5 18c-1.746 0-3.332.477-4.5 1.253" />
            </svg>
            Add Subjects
          </button>

          {/* View Analytics */}
          <button
            onClick={() => router.push("/analytics")}
            className="flex items-center gap-1.5 bg-purple-700 hover:bg-purple-600 active:scale-95 px-3.5 py-2 rounded-lg text-sm font-semibold transition-all duration-150 shadow shadow-purple-900/40"
          >
            <svg className="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24">
              <path strokeLinecap="round" strokeLinejoin="round" strokeWidth={2}
                d="M9 19v-6a2 2 0 00-2-2H5a2 2 0 00-2 2v6a2 2 0 002 2h11a2 2 0 002-2v-5m-1.414-9.414a2 2 0 112.828 2.828L11.828 15H9v-2.828l8.586-8.586z" />
            </svg>
            View Analytics
          </button>

          {/* Divider */}
          <div className="hidden sm:block w-px h-6 bg-zinc-700 mx-1" />

          {/* Choose Class dropdown */}
          <div className="relative ml-auto">
            <button
              onClick={() => setDropdownOpen((v) => !v)}
              disabled={loadingClasses}
              className="flex items-center gap-2 bg-zinc-800 hover:bg-zinc-700 border border-zinc-700 hover:border-zinc-600 active:scale-95 px-4 py-2.5 rounded-xl text-sm font-medium transition-all duration-150 min-w-44 justify-between disabled:opacity-60"
            >
              <span className="flex items-center gap-2">
                <svg className="w-4 h-4 text-indigo-400 shrink-0" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                  <path strokeLinecap="round" strokeLinejoin="round" strokeWidth={2}
                    d="M19 21V5a2 2 0 00-2-2H7a2 2 0 00-2 2v16m14 0h2m-2 0h-5m-9 0H3m2 0h5M9 7h1m-1 4h1m4-4h1m-1 4h1m-5 10v-5a1 1 0 011-1h2a1 1 0 011 1v5m-4 0h4" />
                </svg>
                {loadingClasses ? "Loading…" : (selectedClass || "No classes")}
              </span>
              <svg
                className={`w-4 h-4 text-zinc-400 transition-transform duration-200 ${dropdownOpen ? "rotate-180" : ""}`}
                fill="none" stroke="currentColor" viewBox="0 0 24 24"
              >
                <path strokeLinecap="round" strokeLinejoin="round" strokeWidth={2} d="M19 9l-7 7-7-7" />
              </svg>
            </button>

            {dropdownOpen && classes.length > 0 && (
              <div className="absolute right-0 mt-2 w-52 bg-zinc-900 border border-zinc-700 rounded-xl shadow-2xl shadow-black/60 z-50 overflow-hidden">
                <div className="px-3 py-2 border-b border-zinc-800">
                  <p className="text-xs text-zinc-500 font-semibold uppercase tracking-wider">Choose Class</p>
                </div>
                <ul className="py-1 max-h-60 overflow-y-auto">
                  {classes.map((cls) => (
                    <li key={`dd-${cls}`}>
                      <button
                        onClick={() => { setSelectedClass(cls); setDropdownOpen(false); }}
                        className={`w-full text-left px-4 py-2.5 text-sm flex items-center gap-2 transition-colors ${
                          selectedClass === cls
                            ? "bg-indigo-600/20 text-indigo-300"
                            : "text-zinc-300 hover:bg-zinc-800"
                        }`}
                      >
                        <span className="flex items-center gap-2 w-full">
                          {selectedClass === cls
                            ? <svg className="w-3.5 h-3.5 text-indigo-400 shrink-0" fill="currentColor" viewBox="0 0 20 20"><path fillRule="evenodd" d="M16.707 5.293a1 1 0 010 1.414l-8 8a1 1 0 01-1.414 0l-4-4a1 1 0 011.414-1.414L8 12.586l7.293-7.293a1 1 0 011.414 0z" clipRule="evenodd" /></svg>
                            : <span className="w-3.5 shrink-0" />
                          }
                          <span>{cls}</span>
                        </span>
                      </button>
                    </li>
                  ))}
                </ul>
              </div>
            )}

            {dropdownOpen && classes.length === 0 && !loadingClasses && (
              <div className="absolute right-0 mt-2 w-52 bg-zinc-900 border border-zinc-700 rounded-xl shadow-2xl shadow-black/60 z-50 p-4 text-center">
                <p className="text-zinc-500 text-sm">No classes yet.</p>
                <button
                  onClick={() => { setDropdownOpen(false); setShowAddClass(true); }}
                  className="mt-2 text-indigo-400 hover:text-indigo-300 text-xs underline"
                >
                  Add a class
                </button>
              </div>
            )}
          </div>

          {/* Logout */}
          <button
            onClick={logout}
            className="flex items-center gap-1.5 text-zinc-500 hover:text-red-400 text-sm transition-colors border border-zinc-800 hover:border-red-900 px-3 py-2 rounded-lg ml-2"
          >
            <svg className="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24">
              <path strokeLinecap="round" strokeLinejoin="round" strokeWidth={2}
                d="M17 16l4-4m0 0l-4-4m4 4H7m6 4v1a3 3 0 01-3 3H6a3 3 0 01-3-3V7a3 3 0 013-3h4a3 3 0 013 3v1" />
            </svg>
            Logout
          </button>
        </div>
      </nav>

      {/* ── Main content ── */}
      <div className="relative max-w-6xl mx-auto px-4 py-10">
        {!selectedClass ? (
          <div className="text-center py-24 text-zinc-600 text-sm">
            {loadingClasses ? "Loading classes…" : "No classes found. Click \"Add Class\" to get started."}
          </div>
        ) : (
          <div className="space-y-10">
            {/* ── Students section ── */}
            <section>
              <div className="flex items-center justify-between mb-4">
                <h2 className="text-base font-semibold text-zinc-300">
                  Students — <span className="text-indigo-400">{selectedClass}</span>
                  {!loadingStudents && (
                    <span className="text-zinc-500 text-xs font-normal ml-2">({students.length})</span>
                  )}
                </h2>
                <div className="flex items-center gap-3">
                  {loadingStudents && (
                    <span className="text-xs text-zinc-600 animate-pulse">Loading…</span>
                  )}
                  <button
                    onClick={() => openAddStudentModal()}
                    className="flex items-center gap-1.5 bg-amber-700 hover:bg-amber-600 active:scale-95 px-3 py-1.5 rounded-lg text-xs font-semibold transition-all duration-150 shadow shadow-amber-900/40"
                  >
                    <svg className="w-3.5 h-3.5" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                      <path strokeLinecap="round" strokeLinejoin="round" strokeWidth={2} d="M12 4v16m8-8H4" />
                    </svg>
                    Add Student
                  </button>
                </div>
              </div>

              {!loadingStudents && students.length === 0 ? (
                <div className="text-center py-10 text-zinc-600 text-sm border border-dashed border-zinc-800 rounded-2xl">
                  No students in <span className="text-zinc-400">{selectedClass}</span> yet.{" "}
                  <button
                    onClick={() => openAddStudentModal()}
                    className="text-amber-500 hover:text-amber-400 underline"
                  >
                    Add one
                  </button>
                </div>
              ) : !loadingStudents ? (
                <div className="bg-zinc-900 border border-zinc-800 rounded-2xl overflow-hidden">
                  {/* Table header */}
                  <div className="grid grid-cols-12 gap-4 px-5 py-3 border-b border-zinc-800 text-xs font-semibold text-zinc-500 uppercase tracking-wider">
                    <div className="col-span-2">Student ID</div>
                    <div className="col-span-3">Name</div>
                    <div className="col-span-4">Email</div>
                    <div className="col-span-3 text-right">Actions</div>
                  </div>
                  {/* Table rows */}
                  <div className="divide-y divide-zinc-800/60">
                    {students.map((stu, i) => (
                      <div
                        key={`stu-${stu.student_id || i}`}
                        className="px-5 py-3 hover:bg-zinc-800/40 transition-colors cursor-pointer"
                        onClick={() => { if (editingStudentId !== stu.student_id) openStudentDetail(stu); }}
                      >
                        {editingStudentId === stu.student_id ? (
                          <>
                            <form onSubmit={(e) => handleUpdateStudent(e, stu.student_id)} className="grid grid-cols-12 gap-4 items-center">
                              <div className="col-span-2 text-sm text-indigo-300 font-mono truncate">{stu.student_id}</div>
                              <div className="col-span-3">
                                <input
                                  type="text"
                                  value={editStudentName}
                                  onChange={(e) => { setEditStudentName(e.target.value); setEditStudentError(""); }}
                                  autoFocus
                                  title="Edit name"
                                  placeholder="Name"
                                  className="w-full bg-zinc-700 border border-zinc-600 focus:border-amber-500 rounded-lg px-3 py-1.5 text-white text-sm outline-none"
                                />
                              </div>
                              <div className="col-span-4">
                                <input
                                  type="email"
                                  value={editStudentEmail}
                                  onChange={(e) => { setEditStudentEmail(e.target.value); setEditStudentError(""); }}
                                  title="Edit email"
                                  placeholder="Email"
                                  className="w-full bg-zinc-700 border border-zinc-600 focus:border-amber-500 rounded-lg px-3 py-1.5 text-white text-sm outline-none"
                                />
                              </div>
                              <div className="col-span-3 flex items-center justify-end gap-2">
                                <button
                                  type="submit"
                                  disabled={!editStudentName.trim() || !editStudentEmail.trim() || editStudentLoading}
                                  className="bg-amber-700 hover:bg-amber-600 disabled:opacity-40 text-white text-xs font-semibold px-3 py-1.5 rounded-lg transition-colors"
                                >
                                  {editStudentLoading ? "…" : "Save"}
                                </button>
                                <button
                                  type="button"
                                  onClick={() => { setEditingStudentId(null); setEditStudentName(""); setEditStudentEmail(""); setEditStudentError(""); }}
                                  className="text-zinc-500 hover:text-zinc-300 text-xs px-2 py-1.5 rounded-lg transition-colors"
                                >
                                  Cancel
                                </button>
                              </div>
                            </form>
                            {editStudentError && <p className="text-red-400 text-xs mt-1.5 px-1">{editStudentError}</p>}
                          </>
                        ) : (
                          <div className="grid grid-cols-12 gap-4 items-center">
                            <div className="col-span-2 text-sm text-indigo-300 font-mono truncate">{stu.student_id}</div>
                            <div className="col-span-3 text-sm text-zinc-200 truncate">{stu.name}</div>
                            <div className="col-span-4 text-sm text-zinc-400 truncate">{stu.email}</div>
                            <div className="col-span-3 flex items-center justify-end gap-1">
                              <button
                                type="button"
                                onClick={(e) => { e.stopPropagation(); openStudentDetail(stu); }}
                                title={`View ${stu.name}`}
                                className="text-zinc-500 hover:text-indigo-400 p-1.5 rounded-lg hover:bg-zinc-700 transition-colors"
                              >
                                <svg className="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                                  <path strokeLinecap="round" strokeLinejoin="round" strokeWidth={2} d="M15 12a3 3 0 11-6 0 3 3 0 016 0z" />
                                  <path strokeLinecap="round" strokeLinejoin="round" strokeWidth={2} d="M2.458 12C3.732 7.943 7.523 5 12 5c4.478 0 8.268 2.943 9.542 7-1.274 4.057-5.064 7-9.542 7-4.477 0-8.268-2.943-9.542-7z" />
                                </svg>
                              </button>
                              <button
                                type="button"
                                onClick={(e) => { e.stopPropagation(); setEditingStudentId(stu.student_id); setEditStudentName(stu.name); setEditStudentEmail(stu.email); setEditStudentError(""); }}
                                title={`Edit ${stu.name}`}
                                className="text-zinc-500 hover:text-amber-400 p-1.5 rounded-lg hover:bg-zinc-700 transition-colors"
                              >
                                <svg className="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                                  <path strokeLinecap="round" strokeLinejoin="round" strokeWidth={2} d="M11 5H6a2 2 0 00-2 2v11a2 2 0 002 2h11a2 2 0 002-2v-5m-1.414-9.414a2 2 0 112.828 2.828L11.828 15H9v-2.828l8.586-8.586z" />
                                </svg>
                              </button>
                              <button
                                type="button"
                                onClick={(e) => { e.stopPropagation(); handleDeleteStudent(stu.student_id, stu.name); }}
                                title={`Delete ${stu.name}`}
                                className="text-zinc-500 hover:text-red-400 p-1.5 rounded-lg hover:bg-zinc-700 transition-colors"
                              >
                                <svg className="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                                  <path strokeLinecap="round" strokeLinejoin="round" strokeWidth={2} d="M19 7l-.867 12.142A2 2 0 0116.138 21H7.862a2 2 0 01-1.995-1.858L5 7m5 4v6m4-6v6m1-10V4a1 1 0 00-1-1h-4a1 1 0 00-1 1v3M4 7h16" />
                                </svg>
                              </button>
                            </div>
                          </div>
                        )}
                      </div>
                    ))}
                  </div>
                </div>
              ) : null}
            </section>
          </div>
        )}

        {/* Footer */}
        <div className="text-center text-xs text-zinc-700 mt-16">
          Developed by <span className="text-zinc-500 font-semibold">Zyberloop</span> &copy; 2026
        </div>
      </div>
    </div>
  );
}
