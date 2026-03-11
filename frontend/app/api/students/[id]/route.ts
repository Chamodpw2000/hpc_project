import { NextRequest, NextResponse } from "next/server";

const BACKEND = process.env.NEXT_PUBLIC_API_URL ?? "http://localhost:8090";

export async function GET(
  req: NextRequest,
  { params }: { params: Promise<{ id: string }> }
) {
  try {
    const { id } = await params;
    const encodedId = encodeURIComponent(id);
    const res = await fetch(`${BACKEND}/api/students/${encodedId}`, {
      cache: "no-store",
    });
    const json = await res.json();
    return NextResponse.json(json, { status: res.status });
  } catch {
    return NextResponse.json(
      { success: false, message: "Backend unreachable" },
      { status: 502 }
    );
  }
}

export async function PUT(
  req: NextRequest,
  { params }: { params: Promise<{ id: string }> }
) {
  try {
    const { id } = await params;
    const body = await req.json();
    const encodedId = encodeURIComponent(id);
    const res = await fetch(`${BACKEND}/api/students/${encodedId}`, {
      method: "PUT",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(body),
    });
    const json = await res.json();
    return NextResponse.json(json, { status: res.status });
  } catch {
    return NextResponse.json(
      { success: false, message: "Backend unreachable" },
      { status: 502 }
    );
  }
}

export async function DELETE(
  req: NextRequest,
  { params }: { params: Promise<{ id: string }> }
) {
  try {
    const { id } = await params;
    const encodedId = encodeURIComponent(id);
    const res = await fetch(`${BACKEND}/api/students/${encodedId}`, {
      method: "DELETE",
    });
    if (res.status === 204) return new NextResponse(null, { status: 204 });
    const json = await res.json();
    return NextResponse.json(json, { status: res.status });
  } catch {
    return NextResponse.json(
      { success: false, message: "Backend unreachable" },
      { status: 502 }
    );
  }
}
