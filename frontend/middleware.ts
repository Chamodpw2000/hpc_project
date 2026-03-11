import { jwtVerify } from "jose";
import { NextRequest, NextResponse } from "next/server";

const SECRET = new TextEncoder().encode(
  process.env.JWT_SECRET ?? "openmp-score-analyzer-secret-key-2026"
);

export async function middleware(req: NextRequest) {
  const { pathname } = req.nextUrl;

  /* Let the login page and auth API routes through without checking */
  if (
    pathname.startsWith("/login") ||
    pathname.startsWith("/api/auth") ||
    pathname.startsWith("/_next") ||
    pathname.startsWith("/favicon.ico")
  ) {
    return NextResponse.next();
  }

  const token = req.cookies.get("auth_token")?.value;

  if (!token) {
    return NextResponse.redirect(new URL("/login", req.url));
  }

  try {
    await jwtVerify(token, SECRET);
    return NextResponse.next();
  } catch {
    /* Token invalid or expired */
    const res = NextResponse.redirect(new URL("/login", req.url));
    res.cookies.set("auth_token", "", { maxAge: 0, path: "/" });
    return res;
  }
}

export const config = {
  matcher: ["/((?!_next/static|_next/image|favicon.ico).*)"],
};
