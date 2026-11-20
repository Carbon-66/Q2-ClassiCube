/* Compiler support routines for 32-bit MIPS that libgcc would give (there is no libgcc in the
 * build): 64-bit division, which ClassiCube's timing and formatting use. Shift-and-subtract, so
 * the compiler can't turn it back into a call to itself. */
typedef unsigned long long u64;
typedef long long s64;

u64 __udivdi3(u64 n, u64 d);
u64 __umoddi3(u64 n, u64 d);
s64 __divdi3(s64 n, s64 d);
s64 __moddi3(s64 n, s64 d);

static u64 udivmod(u64 n, u64 d, u64* rem) {
	u64 q = 0, r = 0;
	int i;
	if (!d) { *rem = 0; return 0; } /* as MIPS's divu: no trap */
	if (!(n >> 32) && !(d >> 32)) { /* the common case: the hardware's 32-bit divide */
		*rem = (unsigned)n % (unsigned)d;
		return (unsigned)n / (unsigned)d;
	}
	for (i = 63; i >= 0; --i) {
		r = r << 1 | (n >> i & 1);
		if (r >= d) { r -= d; q |= 1ull << i; }
	}
	*rem = r;
	return q;
}

u64 __udivdi3(u64 n, u64 d) { u64 r; return udivmod(n, d, &r); }
u64 __umoddi3(u64 n, u64 d) { u64 r; udivmod(n, d, &r); return r; }

s64 __divdi3(s64 n, s64 d) {
	u64 r, q = udivmod(n < 0 ? -(u64)n : (u64)n, d < 0 ? -(u64)d : (u64)d, &r);
	return (n < 0) != (d < 0) ? -(s64)q : (s64)q;
}

s64 __moddi3(s64 n, s64 d) {
	u64 r;
	udivmod(n < 0 ? -(u64)n : (u64)n, d < 0 ? -(u64)d : (u64)d, &r);
	return n < 0 ? -(s64)r : (s64)r;
}
