import { NextRequest, NextResponse } from "next/server";

const BACKEND = process.env.NEXT_PUBLIC_API_URL ?? "http://localhost:8090";

export async function GET(req: NextRequest) {
  try {
    const cls = req.nextUrl.searchParams.get("class");
    const url = cls
      ? `${BACKEND}/api/subjects?class=${encodeURIComponent(cls)}`
      : `${BACKEND}/api/subjects`;
    const res = await fetch(url, { cache: "no-store" });
    const json = await res.json();
    return NextResponse.json(json, { status: res.status });
  } catch {
    return NextResponse.json({ success: false, message: "Backend unreachable" }, { status: 502 });
  }
}

export async function POST(req: NextRequest) {
  try {
    const body = await req.json();
    const res = await fetch(`${BACKEND}/api/subjects`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(body),
    });
    const json = await res.json();
    return NextResponse.json(json, { status: res.status });
  } catch {
    return NextResponse.json({ success: false, message: "Backend unreachable" }, { status: 502 });
  }
}
