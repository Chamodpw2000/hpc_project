import { SignJWT } from "jose";
import { NextRequest, NextResponse } from "next/server";

const SECRET = new TextEncoder().encode(
  process.env.JWT_SECRET ?? "openmp-score-analyzer-secret-key-2026"
);

const VALID_USER = process.env.ADMIN_USERNAME ?? "admin";
const VALID_PASS = process.env.ADMIN_PASSWORD ?? "admin123";

export async function POST(req: NextRequest) {
  try {
    const { username, password } = await req.json();

    if (username !== VALID_USER || password !== VALID_PASS) {
      return NextResponse.json(
        { success: false, message: "Invalid username or password." },
        { status: 401 }
      );
    }

    /* Sign a JWT – expires in 8 hours */
    const token = await new SignJWT({ sub: username, role: "admin" })
      .setProtectedHeader({ alg: "HS256" })
      .setIssuedAt()
      .setExpirationTime("8h")
      .sign(SECRET);

    const res = NextResponse.json({ success: true, message: "Login successful." });

    /* Store token in an httpOnly cookie so JS cannot read it */
    res.cookies.set("auth_token", token, {
      httpOnly: true,
      secure: process.env.NODE_ENV === "production",
      sameSite: "lax",
      path: "/",
      maxAge: 60 * 60 * 8, // 8 hours in seconds
    });

    return res;
  } catch {
    return NextResponse.json(
      { success: false, message: "Server error." },
      { status: 500 }
    );
  }
}
