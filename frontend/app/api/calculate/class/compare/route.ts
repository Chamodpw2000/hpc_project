import { NextRequest, NextResponse } from "next/server";

const BACKEND = process.env.NEXT_PUBLIC_API_URL ?? "http://localhost:8090";

export async function GET(req: NextRequest) {
  try {
    const { searchParams } = new URL(req.url);
    const cls = searchParams.get("class");
    if (!cls) {
      return NextResponse.json(
        { error: true, message: "Missing class parameter", code: 400 },
        { status: 400 }
      );
    }

    const res = await fetch(`${BACKEND}/api/calculate/class/compare?class=${encodeURIComponent(cls)}`, {
      cache: "no-store",
    });

    const json = await res.json();

    // The backend now returns standard errors { error: true, code: 503, ... }
    if (json.error) {
      return NextResponse.json(json, { status: json.code || res.status });
    }

    return NextResponse.json(json, { status: res.status });
  } catch (error: any) {
    return NextResponse.json(
      { error: true, message: "Backend unreachable", code: 502 },
      { status: 502 }
    );
  }
}
