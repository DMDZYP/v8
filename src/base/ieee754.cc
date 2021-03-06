// The following is adapted from fdlibm (http://www.netlib.org/fdlibm).
//
// ====================================================
// Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
//
// Developed at SunSoft, a Sun Microsystems, Inc. business.
// Permission to use, copy, modify, and distribute this
// software is freely granted, provided that this notice
// is preserved.
// ====================================================
//
// The original source code covered by the above license above has been
// modified significantly by Google Inc.
// Copyright 2016 the V8 project authors. All rights reserved.

#include "src/base/ieee754.h"

#include <cmath>
#include <limits>

#include "src/base/build_config.h"
#include "src/base/macros.h"

namespace v8 {
namespace base {
namespace ieee754 {

namespace {

/* Fix-up typedefs so we can use the FreeBSD msun code mostly unmodified. */

#if V8_OS_WIN

typedef uint32_t u_int32_t;
typedef uint64_t u_int64_t;

#endif

/* Disable "potential divide by 0" warning in Visual Studio compiler. */

#if V8_CC_MSVC

#pragma warning(disable : 4723)

#endif

/*
 * The original fdlibm code used statements like:
 *  n0 = ((*(int*)&one)>>29)^1;   * index of high word *
 *  ix0 = *(n0+(int*)&x);     * high word of x *
 *  ix1 = *((1-n0)+(int*)&x);   * low word of x *
 * to dig two 32 bit words out of the 64 bit IEEE floating point
 * value.  That is non-ANSI, and, moreover, the gcc instruction
 * scheduler gets it wrong.  We instead use the following macros.
 * Unlike the original code, we determine the endianness at compile
 * time, not at run time; I don't see much benefit to selecting
 * endianness at run time.
 */

/*
 * A union which permits us to convert between a double and two 32 bit
 * ints.
 */

#if V8_TARGET_LITTLE_ENDIAN

typedef union {
  double value;
  struct {
    u_int32_t lsw;
    u_int32_t msw;
  } parts;
  struct {
    u_int64_t w;
  } xparts;
} ieee_double_shape_type;

#else

typedef union {
  double value;
  struct {
    u_int32_t msw;
    u_int32_t lsw;
  } parts;
  struct {
    u_int64_t w;
  } xparts;
} ieee_double_shape_type;

#endif

/* Get two 32 bit ints from a double.  */

#define EXTRACT_WORDS(ix0, ix1, d) \
  do {                             \
    ieee_double_shape_type ew_u;   \
    ew_u.value = (d);              \
    (ix0) = ew_u.parts.msw;        \
    (ix1) = ew_u.parts.lsw;        \
  } while (0)

/* Get a 64-bit int from a double. */
#define EXTRACT_WORD64(ix, d)    \
  do {                           \
    ieee_double_shape_type ew_u; \
    ew_u.value = (d);            \
    (ix) = ew_u.xparts.w;        \
  } while (0)

/* Get the more significant 32 bit int from a double.  */

#define GET_HIGH_WORD(i, d)      \
  do {                           \
    ieee_double_shape_type gh_u; \
    gh_u.value = (d);            \
    (i) = gh_u.parts.msw;        \
  } while (0)

/* Get the less significant 32 bit int from a double.  */

#define GET_LOW_WORD(i, d)       \
  do {                           \
    ieee_double_shape_type gl_u; \
    gl_u.value = (d);            \
    (i) = gl_u.parts.lsw;        \
  } while (0)

/* Set a double from two 32 bit ints.  */

#define INSERT_WORDS(d, ix0, ix1) \
  do {                            \
    ieee_double_shape_type iw_u;  \
    iw_u.parts.msw = (ix0);       \
    iw_u.parts.lsw = (ix1);       \
    (d) = iw_u.value;             \
  } while (0)

/* Set a double from a 64-bit int. */
#define INSERT_WORD64(d, ix)     \
  do {                           \
    ieee_double_shape_type iw_u; \
    iw_u.xparts.w = (ix);        \
    (d) = iw_u.value;            \
  } while (0)

/* Set the more significant 32 bits of a double from an int.  */

#define SET_HIGH_WORD(d, v)      \
  do {                           \
    ieee_double_shape_type sh_u; \
    sh_u.value = (d);            \
    sh_u.parts.msw = (v);        \
    (d) = sh_u.value;            \
  } while (0)

/* Set the less significant 32 bits of a double from an int.  */

#define SET_LOW_WORD(d, v)       \
  do {                           \
    ieee_double_shape_type sl_u; \
    sl_u.value = (d);            \
    sl_u.parts.lsw = (v);        \
    (d) = sl_u.value;            \
  } while (0)

/* Support macro. */

#define STRICT_ASSIGN(type, lval, rval) ((lval) = (rval))

}  // namespace

/* atan(x)
 * Method
 *   1. Reduce x to positive by atan(x) = -atan(-x).
 *   2. According to the integer k=4t+0.25 chopped, t=x, the argument
 *      is further reduced to one of the following intervals and the
 *      arctangent of t is evaluated by the corresponding formula:
 *
 *      [0,7/16]      atan(x) = t-t^3*(a1+t^2*(a2+...(a10+t^2*a11)...)
 *      [7/16,11/16]  atan(x) = atan(1/2) + atan( (t-0.5)/(1+t/2) )
 *      [11/16.19/16] atan(x) = atan( 1 ) + atan( (t-1)/(1+t) )
 *      [19/16,39/16] atan(x) = atan(3/2) + atan( (t-1.5)/(1+1.5t) )
 *      [39/16,INF]   atan(x) = atan(INF) + atan( -1/t )
 *
 * Constants:
 * The hexadecimal values are the intended ones for the following
 * constants. The decimal values may be used, provided that the
 * compiler will convert from decimal to binary accurately enough
 * to produce the hexadecimal values shown.
 */
double atan(double x) {
  static const double atanhi[] = {
      4.63647609000806093515e-01, /* atan(0.5)hi 0x3FDDAC67, 0x0561BB4F */
      7.85398163397448278999e-01, /* atan(1.0)hi 0x3FE921FB, 0x54442D18 */
      9.82793723247329054082e-01, /* atan(1.5)hi 0x3FEF730B, 0xD281F69B */
      1.57079632679489655800e+00, /* atan(inf)hi 0x3FF921FB, 0x54442D18 */
  };

  static const double atanlo[] = {
      2.26987774529616870924e-17, /* atan(0.5)lo 0x3C7A2B7F, 0x222F65E2 */
      3.06161699786838301793e-17, /* atan(1.0)lo 0x3C81A626, 0x33145C07 */
      1.39033110312309984516e-17, /* atan(1.5)lo 0x3C700788, 0x7AF0CBBD */
      6.12323399573676603587e-17, /* atan(inf)lo 0x3C91A626, 0x33145C07 */
  };

  static const double aT[] = {
      3.33333333333329318027e-01,  /* 0x3FD55555, 0x5555550D */
      -1.99999999998764832476e-01, /* 0xBFC99999, 0x9998EBC4 */
      1.42857142725034663711e-01,  /* 0x3FC24924, 0x920083FF */
      -1.11111104054623557880e-01, /* 0xBFBC71C6, 0xFE231671 */
      9.09088713343650656196e-02,  /* 0x3FB745CD, 0xC54C206E */
      -7.69187620504482999495e-02, /* 0xBFB3B0F2, 0xAF749A6D */
      6.66107313738753120669e-02,  /* 0x3FB10D66, 0xA0D03D51 */
      -5.83357013379057348645e-02, /* 0xBFADDE2D, 0x52DEFD9A */
      4.97687799461593236017e-02,  /* 0x3FA97B4B, 0x24760DEB */
      -3.65315727442169155270e-02, /* 0xBFA2B444, 0x2C6A6C2F */
      1.62858201153657823623e-02,  /* 0x3F90AD3A, 0xE322DA11 */
  };

  static const double one = 1.0, huge = 1.0e300;

  double w, s1, s2, z;
  int32_t ix, hx, id;

  GET_HIGH_WORD(hx, x);
  ix = hx & 0x7fffffff;
  if (ix >= 0x44100000) { /* if |x| >= 2^66 */
    u_int32_t low;
    GET_LOW_WORD(low, x);
    if (ix > 0x7ff00000 || (ix == 0x7ff00000 && (low != 0)))
      return x + x; /* NaN */
    if (hx > 0)
      return atanhi[3] + *(volatile double *)&atanlo[3];
    else
      return -atanhi[3] - *(volatile double *)&atanlo[3];
  }
  if (ix < 0x3fdc0000) {            /* |x| < 0.4375 */
    if (ix < 0x3e400000) {          /* |x| < 2^-27 */
      if (huge + x > one) return x; /* raise inexact */
    }
    id = -1;
  } else {
    x = fabs(x);
    if (ix < 0x3ff30000) {   /* |x| < 1.1875 */
      if (ix < 0x3fe60000) { /* 7/16 <=|x|<11/16 */
        id = 0;
        x = (2.0 * x - one) / (2.0 + x);
      } else { /* 11/16<=|x|< 19/16 */
        id = 1;
        x = (x - one) / (x + one);
      }
    } else {
      if (ix < 0x40038000) { /* |x| < 2.4375 */
        id = 2;
        x = (x - 1.5) / (one + 1.5 * x);
      } else { /* 2.4375 <= |x| < 2^66 */
        id = 3;
        x = -1.0 / x;
      }
    }
  }
  /* end of argument reduction */
  z = x * x;
  w = z * z;
  /* break sum from i=0 to 10 aT[i]z**(i+1) into odd and even poly */
  s1 = z * (aT[0] +
            w * (aT[2] + w * (aT[4] + w * (aT[6] + w * (aT[8] + w * aT[10])))));
  s2 = w * (aT[1] + w * (aT[3] + w * (aT[5] + w * (aT[7] + w * aT[9]))));
  if (id < 0) {
    return x - x * (s1 + s2);
  } else {
    z = atanhi[id] - ((x * (s1 + s2) - atanlo[id]) - x);
    return (hx < 0) ? -z : z;
  }
}

/* atan2(y,x)
 * Method :
 *  1. Reduce y to positive by atan2(y,x)=-atan2(-y,x).
 *  2. Reduce x to positive by (if x and y are unexceptional):
 *    ARG (x+iy) = arctan(y/x)       ... if x > 0,
 *    ARG (x+iy) = pi - arctan[y/(-x)]   ... if x < 0,
 *
 * Special cases:
 *
 *  ATAN2((anything), NaN ) is NaN;
 *  ATAN2(NAN , (anything) ) is NaN;
 *  ATAN2(+-0, +(anything but NaN)) is +-0  ;
 *  ATAN2(+-0, -(anything but NaN)) is +-pi ;
 *  ATAN2(+-(anything but 0 and NaN), 0) is +-pi/2;
 *  ATAN2(+-(anything but INF and NaN), +INF) is +-0 ;
 *  ATAN2(+-(anything but INF and NaN), -INF) is +-pi;
 *  ATAN2(+-INF,+INF ) is +-pi/4 ;
 *  ATAN2(+-INF,-INF ) is +-3pi/4;
 *  ATAN2(+-INF, (anything but,0,NaN, and INF)) is +-pi/2;
 *
 * Constants:
 * The hexadecimal values are the intended ones for the following
 * constants. The decimal values may be used, provided that the
 * compiler will convert from decimal to binary accurately enough
 * to produce the hexadecimal values shown.
 */
double atan2(double y, double x) {
  static volatile double tiny = 1.0e-300;
  static const double
      zero = 0.0,
      pi_o_4 = 7.8539816339744827900E-01, /* 0x3FE921FB, 0x54442D18 */
      pi_o_2 = 1.5707963267948965580E+00, /* 0x3FF921FB, 0x54442D18 */
      pi = 3.1415926535897931160E+00;     /* 0x400921FB, 0x54442D18 */
  static volatile double pi_lo =
      1.2246467991473531772E-16; /* 0x3CA1A626, 0x33145C07 */

  double z;
  int32_t k, m, hx, hy, ix, iy;
  u_int32_t lx, ly;

  EXTRACT_WORDS(hx, lx, x);
  ix = hx & 0x7fffffff;
  EXTRACT_WORDS(hy, ly, y);
  iy = hy & 0x7fffffff;
  if (((ix | ((lx | -static_cast<int32_t>(lx)) >> 31)) > 0x7ff00000) ||
      ((iy | ((ly | -static_cast<int32_t>(ly)) >> 31)) > 0x7ff00000)) {
    return x + y; /* x or y is NaN */
  }
  if (((hx - 0x3ff00000) | lx) == 0) return atan(y); /* x=1.0 */
  m = ((hy >> 31) & 1) | ((hx >> 30) & 2);           /* 2*sign(x)+sign(y) */

  /* when y = 0 */
  if ((iy | ly) == 0) {
    switch (m) {
      case 0:
      case 1:
        return y; /* atan(+-0,+anything)=+-0 */
      case 2:
        return pi + tiny; /* atan(+0,-anything) = pi */
      case 3:
        return -pi - tiny; /* atan(-0,-anything) =-pi */
    }
  }
  /* when x = 0 */
  if ((ix | lx) == 0) return (hy < 0) ? -pi_o_2 - tiny : pi_o_2 + tiny;

  /* when x is INF */
  if (ix == 0x7ff00000) {
    if (iy == 0x7ff00000) {
      switch (m) {
        case 0:
          return pi_o_4 + tiny; /* atan(+INF,+INF) */
        case 1:
          return -pi_o_4 - tiny; /* atan(-INF,+INF) */
        case 2:
          return 3.0 * pi_o_4 + tiny; /*atan(+INF,-INF)*/
        case 3:
          return -3.0 * pi_o_4 - tiny; /*atan(-INF,-INF)*/
      }
    } else {
      switch (m) {
        case 0:
          return zero; /* atan(+...,+INF) */
        case 1:
          return -zero; /* atan(-...,+INF) */
        case 2:
          return pi + tiny; /* atan(+...,-INF) */
        case 3:
          return -pi - tiny; /* atan(-...,-INF) */
      }
    }
  }
  /* when y is INF */
  if (iy == 0x7ff00000) return (hy < 0) ? -pi_o_2 - tiny : pi_o_2 + tiny;

  /* compute y/x */
  k = (iy - ix) >> 20;
  if (k > 60) { /* |y/x| >  2**60 */
    z = pi_o_2 + 0.5 * pi_lo;
    m &= 1;
  } else if (hx < 0 && k < -60) {
    z = 0.0; /* 0 > |y|/x > -2**-60 */
  } else {
    z = atan(fabs(y / x)); /* safe to do y/x */
  }
  switch (m) {
    case 0:
      return z; /* atan(+,+) */
    case 1:
      return -z; /* atan(-,+) */
    case 2:
      return pi - (z - pi_lo); /* atan(+,-) */
    default:                   /* case 3 */
      return (z - pi_lo) - pi; /* atan(-,-) */
  }
}

/* log(x)
 * Return the logrithm of x
 *
 * Method :
 *   1. Argument Reduction: find k and f such that
 *     x = 2^k * (1+f),
 *     where  sqrt(2)/2 < 1+f < sqrt(2) .
 *
 *   2. Approximation of log(1+f).
 *  Let s = f/(2+f) ; based on log(1+f) = log(1+s) - log(1-s)
 *     = 2s + 2/3 s**3 + 2/5 s**5 + .....,
 *         = 2s + s*R
 *      We use a special Reme algorithm on [0,0.1716] to generate
 *  a polynomial of degree 14 to approximate R The maximum error
 *  of this polynomial approximation is bounded by 2**-58.45. In
 *  other words,
 *            2      4      6      8      10      12      14
 *      R(z) ~ Lg1*s +Lg2*s +Lg3*s +Lg4*s +Lg5*s  +Lg6*s  +Lg7*s
 *    (the values of Lg1 to Lg7 are listed in the program)
 *  and
 *      |      2          14          |     -58.45
 *      | Lg1*s +...+Lg7*s    -  R(z) | <= 2
 *      |                             |
 *  Note that 2s = f - s*f = f - hfsq + s*hfsq, where hfsq = f*f/2.
 *  In order to guarantee error in log below 1ulp, we compute log
 *  by
 *    log(1+f) = f - s*(f - R)  (if f is not too large)
 *    log(1+f) = f - (hfsq - s*(hfsq+R)). (better accuracy)
 *
 *  3. Finally,  log(x) = k*ln2 + log(1+f).
 *          = k*ln2_hi+(f-(hfsq-(s*(hfsq+R)+k*ln2_lo)))
 *     Here ln2 is split into two floating point number:
 *      ln2_hi + ln2_lo,
 *     where n*ln2_hi is always exact for |n| < 2000.
 *
 * Special cases:
 *  log(x) is NaN with signal if x < 0 (including -INF) ;
 *  log(+INF) is +INF; log(0) is -INF with signal;
 *  log(NaN) is that NaN with no signal.
 *
 * Accuracy:
 *  according to an error analysis, the error is always less than
 *  1 ulp (unit in the last place).
 *
 * Constants:
 * The hexadecimal values are the intended ones for the following
 * constants. The decimal values may be used, provided that the
 * compiler will convert from decimal to binary accurately enough
 * to produce the hexadecimal values shown.
 */
double log(double x) {
  static const double                      /* -- */
      ln2_hi = 6.93147180369123816490e-01, /* 3fe62e42 fee00000 */
      ln2_lo = 1.90821492927058770002e-10, /* 3dea39ef 35793c76 */
      two54 = 1.80143985094819840000e+16,  /* 43500000 00000000 */
      Lg1 = 6.666666666666735130e-01,      /* 3FE55555 55555593 */
      Lg2 = 3.999999999940941908e-01,      /* 3FD99999 9997FA04 */
      Lg3 = 2.857142874366239149e-01,      /* 3FD24924 94229359 */
      Lg4 = 2.222219843214978396e-01,      /* 3FCC71C5 1D8E78AF */
      Lg5 = 1.818357216161805012e-01,      /* 3FC74664 96CB03DE */
      Lg6 = 1.531383769920937332e-01,      /* 3FC39A09 D078C69F */
      Lg7 = 1.479819860511658591e-01;      /* 3FC2F112 DF3E5244 */

  static const double zero = 0.0;
  static volatile double vzero = 0.0;

  double hfsq, f, s, z, R, w, t1, t2, dk;
  int32_t k, hx, i, j;
  u_int32_t lx;

  EXTRACT_WORDS(hx, lx, x);

  k = 0;
  if (hx < 0x00100000) { /* x < 2**-1022  */
    if (((hx & 0x7fffffff) | lx) == 0)
      return -two54 / vzero;           /* log(+-0)=-inf */
    if (hx < 0) return (x - x) / zero; /* log(-#) = NaN */
    k -= 54;
    x *= two54; /* subnormal number, scale up x */
    GET_HIGH_WORD(hx, x);
  }
  if (hx >= 0x7ff00000) return x + x;
  k += (hx >> 20) - 1023;
  hx &= 0x000fffff;
  i = (hx + 0x95f64) & 0x100000;
  SET_HIGH_WORD(x, hx | (i ^ 0x3ff00000)); /* normalize x or x/2 */
  k += (i >> 20);
  f = x - 1.0;
  if ((0x000fffff & (2 + hx)) < 3) { /* -2**-20 <= f < 2**-20 */
    if (f == zero) {
      if (k == 0) {
        return zero;
      } else {
        dk = static_cast<double>(k);
        return dk * ln2_hi + dk * ln2_lo;
      }
    }
    R = f * f * (0.5 - 0.33333333333333333 * f);
    if (k == 0) {
      return f - R;
    } else {
      dk = static_cast<double>(k);
      return dk * ln2_hi - ((R - dk * ln2_lo) - f);
    }
  }
  s = f / (2.0 + f);
  dk = static_cast<double>(k);
  z = s * s;
  i = hx - 0x6147a;
  w = z * z;
  j = 0x6b851 - hx;
  t1 = w * (Lg2 + w * (Lg4 + w * Lg6));
  t2 = z * (Lg1 + w * (Lg3 + w * (Lg5 + w * Lg7)));
  i |= j;
  R = t2 + t1;
  if (i > 0) {
    hfsq = 0.5 * f * f;
    if (k == 0)
      return f - (hfsq - s * (hfsq + R));
    else
      return dk * ln2_hi - ((hfsq - (s * (hfsq + R) + dk * ln2_lo)) - f);
  } else {
    if (k == 0)
      return f - s * (f - R);
    else
      return dk * ln2_hi - ((s * (f - R) - dk * ln2_lo) - f);
  }
}

/* double log1p(double x)
 *
 * Method :
 *   1. Argument Reduction: find k and f such that
 *      1+x = 2^k * (1+f),
 *     where  sqrt(2)/2 < 1+f < sqrt(2) .
 *
 *      Note. If k=0, then f=x is exact. However, if k!=0, then f
 *  may not be representable exactly. In that case, a correction
 *  term is need. Let u=1+x rounded. Let c = (1+x)-u, then
 *  log(1+x) - log(u) ~ c/u. Thus, we proceed to compute log(u),
 *  and add back the correction term c/u.
 *  (Note: when x > 2**53, one can simply return log(x))
 *
 *   2. Approximation of log1p(f).
 *  Let s = f/(2+f) ; based on log(1+f) = log(1+s) - log(1-s)
 *     = 2s + 2/3 s**3 + 2/5 s**5 + .....,
 *         = 2s + s*R
 *      We use a special Reme algorithm on [0,0.1716] to generate
 *  a polynomial of degree 14 to approximate R The maximum error
 *  of this polynomial approximation is bounded by 2**-58.45. In
 *  other words,
 *            2      4      6      8      10      12      14
 *      R(z) ~ Lp1*s +Lp2*s +Lp3*s +Lp4*s +Lp5*s  +Lp6*s  +Lp7*s
 *    (the values of Lp1 to Lp7 are listed in the program)
 *  and
 *      |      2          14          |     -58.45
 *      | Lp1*s +...+Lp7*s    -  R(z) | <= 2
 *      |                             |
 *  Note that 2s = f - s*f = f - hfsq + s*hfsq, where hfsq = f*f/2.
 *  In order to guarantee error in log below 1ulp, we compute log
 *  by
 *    log1p(f) = f - (hfsq - s*(hfsq+R)).
 *
 *  3. Finally, log1p(x) = k*ln2 + log1p(f).
 *           = k*ln2_hi+(f-(hfsq-(s*(hfsq+R)+k*ln2_lo)))
 *     Here ln2 is split into two floating point number:
 *      ln2_hi + ln2_lo,
 *     where n*ln2_hi is always exact for |n| < 2000.
 *
 * Special cases:
 *  log1p(x) is NaN with signal if x < -1 (including -INF) ;
 *  log1p(+INF) is +INF; log1p(-1) is -INF with signal;
 *  log1p(NaN) is that NaN with no signal.
 *
 * Accuracy:
 *  according to an error analysis, the error is always less than
 *  1 ulp (unit in the last place).
 *
 * Constants:
 * The hexadecimal values are the intended ones for the following
 * constants. The decimal values may be used, provided that the
 * compiler will convert from decimal to binary accurately enough
 * to produce the hexadecimal values shown.
 *
 * Note: Assuming log() return accurate answer, the following
 *   algorithm can be used to compute log1p(x) to within a few ULP:
 *
 *    u = 1+x;
 *    if(u==1.0) return x ; else
 *         return log(u)*(x/(u-1.0));
 *
 *   See HP-15C Advanced Functions Handbook, p.193.
 */
double log1p(double x) {
  static const double                      /* -- */
      ln2_hi = 6.93147180369123816490e-01, /* 3fe62e42 fee00000 */
      ln2_lo = 1.90821492927058770002e-10, /* 3dea39ef 35793c76 */
      two54 = 1.80143985094819840000e+16,  /* 43500000 00000000 */
      Lp1 = 6.666666666666735130e-01,      /* 3FE55555 55555593 */
      Lp2 = 3.999999999940941908e-01,      /* 3FD99999 9997FA04 */
      Lp3 = 2.857142874366239149e-01,      /* 3FD24924 94229359 */
      Lp4 = 2.222219843214978396e-01,      /* 3FCC71C5 1D8E78AF */
      Lp5 = 1.818357216161805012e-01,      /* 3FC74664 96CB03DE */
      Lp6 = 1.531383769920937332e-01,      /* 3FC39A09 D078C69F */
      Lp7 = 1.479819860511658591e-01;      /* 3FC2F112 DF3E5244 */

  static const double zero = 0.0;
  static volatile double vzero = 0.0;

  double hfsq, f, c, s, z, R, u;
  int32_t k, hx, hu, ax;

  GET_HIGH_WORD(hx, x);
  ax = hx & 0x7fffffff;

  k = 1;
  if (hx < 0x3FDA827A) {    /* 1+x < sqrt(2)+ */
    if (ax >= 0x3ff00000) { /* x <= -1.0 */
      if (x == -1.0)
        return -two54 / vzero; /* log1p(-1)=+inf */
      else
        return (x - x) / (x - x); /* log1p(x<-1)=NaN */
    }
    if (ax < 0x3e200000) {    /* |x| < 2**-29 */
      if (two54 + x > zero    /* raise inexact */
          && ax < 0x3c900000) /* |x| < 2**-54 */
        return x;
      else
        return x - x * x * 0.5;
    }
    if (hx > 0 || hx <= static_cast<int32_t>(0xbfd2bec4)) {
      k = 0;
      f = x;
      hu = 1;
    } /* sqrt(2)/2- <= 1+x < sqrt(2)+ */
  }
  if (hx >= 0x7ff00000) return x + x;
  if (k != 0) {
    if (hx < 0x43400000) {
      STRICT_ASSIGN(double, u, 1.0 + x);
      GET_HIGH_WORD(hu, u);
      k = (hu >> 20) - 1023;
      c = (k > 0) ? 1.0 - (u - x) : x - (u - 1.0); /* correction term */
      c /= u;
    } else {
      u = x;
      GET_HIGH_WORD(hu, u);
      k = (hu >> 20) - 1023;
      c = 0;
    }
    hu &= 0x000fffff;
    /*
     * The approximation to sqrt(2) used in thresholds is not
     * critical.  However, the ones used above must give less
     * strict bounds than the one here so that the k==0 case is
     * never reached from here, since here we have committed to
     * using the correction term but don't use it if k==0.
     */
    if (hu < 0x6a09e) {                  /* u ~< sqrt(2) */
      SET_HIGH_WORD(u, hu | 0x3ff00000); /* normalize u */
    } else {
      k += 1;
      SET_HIGH_WORD(u, hu | 0x3fe00000); /* normalize u/2 */
      hu = (0x00100000 - hu) >> 2;
    }
    f = u - 1.0;
  }
  hfsq = 0.5 * f * f;
  if (hu == 0) { /* |f| < 2**-20 */
    if (f == zero) {
      if (k == 0) {
        return zero;
      } else {
        c += k * ln2_lo;
        return k * ln2_hi + c;
      }
    }
    R = hfsq * (1.0 - 0.66666666666666666 * f);
    if (k == 0)
      return f - R;
    else
      return k * ln2_hi - ((R - (k * ln2_lo + c)) - f);
  }
  s = f / (2.0 + f);
  z = s * s;
  R = z * (Lp1 +
           z * (Lp2 + z * (Lp3 + z * (Lp4 + z * (Lp5 + z * (Lp6 + z * Lp7))))));
  if (k == 0)
    return f - (hfsq - s * (hfsq + R));
  else
    return k * ln2_hi - ((hfsq - (s * (hfsq + R) + (k * ln2_lo + c))) - f);
}

/*
 * k_log1p(f):
 * Return log(1+f) - f for 1+f in ~[sqrt(2)/2, sqrt(2)].
 *
 * The following describes the overall strategy for computing
 * logarithms in base e.  The argument reduction and adding the final
 * term of the polynomial are done by the caller for increased accuracy
 * when different bases are used.
 *
 * Method :
 *   1. Argument Reduction: find k and f such that
 *         x = 2^k * (1+f),
 *         where  sqrt(2)/2 < 1+f < sqrt(2) .
 *
 *   2. Approximation of log(1+f).
 *      Let s = f/(2+f) ; based on log(1+f) = log(1+s) - log(1-s)
 *            = 2s + 2/3 s**3 + 2/5 s**5 + .....,
 *            = 2s + s*R
 *      We use a special Reme algorithm on [0,0.1716] to generate
 *      a polynomial of degree 14 to approximate R The maximum error
 *      of this polynomial approximation is bounded by 2**-58.45. In
 *      other words,
 *          2      4      6      8      10      12      14
 *          R(z) ~ Lg1*s +Lg2*s +Lg3*s +Lg4*s +Lg5*s  +Lg6*s  +Lg7*s
 *      (the values of Lg1 to Lg7 are listed in the program)
 *      and
 *          |      2          14          |     -58.45
 *          | Lg1*s +...+Lg7*s    -  R(z) | <= 2
 *          |                             |
 *      Note that 2s = f - s*f = f - hfsq + s*hfsq, where hfsq = f*f/2.
 *      In order to guarantee error in log below 1ulp, we compute log
 *      by
 *          log(1+f) = f - s*(f - R)            (if f is not too large)
 *          log(1+f) = f - (hfsq - s*(hfsq+R)). (better accuracy)
 *
 *   3. Finally,  log(x) = k*ln2 + log(1+f).
 *          = k*ln2_hi+(f-(hfsq-(s*(hfsq+R)+k*ln2_lo)))
 *      Here ln2 is split into two floating point number:
 *          ln2_hi + ln2_lo,
 *      where n*ln2_hi is always exact for |n| < 2000.
 *
 * Special cases:
 *      log(x) is NaN with signal if x < 0 (including -INF) ;
 *      log(+INF) is +INF; log(0) is -INF with signal;
 *      log(NaN) is that NaN with no signal.
 *
 * Accuracy:
 *      according to an error analysis, the error is always less than
 *      1 ulp (unit in the last place).
 *
 * Constants:
 * The hexadecimal values are the intended ones for the following
 * constants. The decimal values may be used, provided that the
 * compiler will convert from decimal to binary accurately enough
 * to produce the hexadecimal values shown.
 */

static const double Lg1 = 6.666666666666735130e-01, /* 3FE55555 55555593 */
    Lg2 = 3.999999999940941908e-01,                 /* 3FD99999 9997FA04 */
    Lg3 = 2.857142874366239149e-01,                 /* 3FD24924 94229359 */
    Lg4 = 2.222219843214978396e-01,                 /* 3FCC71C5 1D8E78AF */
    Lg5 = 1.818357216161805012e-01,                 /* 3FC74664 96CB03DE */
    Lg6 = 1.531383769920937332e-01,                 /* 3FC39A09 D078C69F */
    Lg7 = 1.479819860511658591e-01;                 /* 3FC2F112 DF3E5244 */

/*
 * We always inline k_log1p(), since doing so produces a
 * substantial performance improvement (~40% on amd64).
 */
static inline double k_log1p(double f) {
  double hfsq, s, z, R, w, t1, t2;

  s = f / (2.0 + f);
  z = s * s;
  w = z * z;
  t1 = w * (Lg2 + w * (Lg4 + w * Lg6));
  t2 = z * (Lg1 + w * (Lg3 + w * (Lg5 + w * Lg7)));
  R = t2 + t1;
  hfsq = 0.5 * f * f;
  return s * (hfsq + R);
}

// ES6 draft 09-27-13, section 20.2.2.22.
// Return the base 2 logarithm of x
//
// fdlibm does not have an explicit log2 function, but fdlibm's pow
// function does implement an accurate log2 function as part of the
// pow implementation.  This extracts the core parts of that as a
// separate log2 function.
//
// Method:
// Compute log2(x) in two pieces:
// log2(x) = w1 + w2
// where w1 has 53-24 = 29 bits of trailing zeroes.
double log2(double x) {
  static const double
      bp[] = {1.0, 1.5},
      dp_h[] = {0.0, 5.84962487220764160156e-01}, /* 0x3FE2B803, 0x40000000 */
      dp_l[] = {0.0, 1.35003920212974897128e-08}, /* 0x3E4CFDEB, 0x43CFD006 */
      zero = 0.0, one = 1.0,
      // Polynomial coefficients for (3/2)*(log2(x) - 2*s - 2/3*s^3)
      L1 = 5.99999999999994648725e-01, L2 = 4.28571428578550184252e-01,
      L3 = 3.33333329818377432918e-01, L4 = 2.72728123808534006489e-01,
      L5 = 2.30660745775561754067e-01, L6 = 2.06975017800338417784e-01,
      // cp = 2/(3*ln(2)). Note that cp_h + cp_l is cp, but with more accuracy.
      cp = 9.61796693925975554329e-01, cp_h = 9.61796700954437255859e-01,
      cp_l = -7.02846165095275826516e-09, two53 = 9007199254740992, /* 2^53 */
      two54 = 1.80143985094819840000e+16; /* 0x43500000, 0x00000000 */

  static volatile double vzero = 0.0;
  double ax, z_h, z_l, p_h, p_l;
  double t1, t2, r, t, u, v;
  int32_t j, k, n;
  int32_t ix, hx;
  u_int32_t lx;

  EXTRACT_WORDS(hx, lx, x);
  ix = hx & 0x7fffffff;

  // Handle special cases.
  // log2(+/- 0) = -Infinity
  if ((ix | lx) == 0) return -two54 / vzero; /* log(+-0)=-inf */

  // log(x) = NaN, if x < 0
  if (hx < 0) return (x - x) / zero; /* log(-#) = NaN */

  // log2(Infinity) = Infinity, log2(NaN) = NaN
  if (ix >= 0x7ff00000) return x;

  ax = fabs(x);

  double ss, s2, s_h, s_l, t_h, t_l;
  n = 0;

  /* take care subnormal number */
  if (ix < 0x00100000) {
    ax *= two53;
    n -= 53;
    GET_HIGH_WORD(ix, ax);
  }

  n += ((ix) >> 20) - 0x3ff;
  j = ix & 0x000fffff;

  /* determine interval */
  ix = j | 0x3ff00000; /* normalize ix */
  if (j <= 0x3988E) {
    k = 0; /* |x|<sqrt(3/2) */
  } else if (j < 0xBB67A) {
    k = 1; /* |x|<sqrt(3)   */
  } else {
    k = 0;
    n += 1;
    ix -= 0x00100000;
  }
  SET_HIGH_WORD(ax, ix);

  /* compute ss = s_h+s_l = (x-1)/(x+1) or (x-1.5)/(x+1.5) */
  u = ax - bp[k]; /* bp[0]=1.0, bp[1]=1.5 */
  v = one / (ax + bp[k]);
  ss = u * v;
  s_h = ss;
  SET_LOW_WORD(s_h, 0);
  /* t_h=ax+bp[k] High */
  t_h = zero;
  SET_HIGH_WORD(t_h, ((ix >> 1) | 0x20000000) + 0x00080000 + (k << 18));
  t_l = ax - (t_h - bp[k]);
  s_l = v * ((u - s_h * t_h) - s_h * t_l);
  /* compute log(ax) */
  s2 = ss * ss;
  r = s2 * s2 * (L1 + s2 * (L2 + s2 * (L3 + s2 * (L4 + s2 * (L5 + s2 * L6)))));
  r += s_l * (s_h + ss);
  s2 = s_h * s_h;
  t_h = 3.0 + s2 + r;
  SET_LOW_WORD(t_h, 0);
  t_l = r - ((t_h - 3.0) - s2);
  /* u+v = ss*(1+...) */
  u = s_h * t_h;
  v = s_l * t_h + t_l * ss;
  /* 2/(3log2)*(ss+...) */
  p_h = u + v;
  SET_LOW_WORD(p_h, 0);
  p_l = v - (p_h - u);
  z_h = cp_h * p_h; /* cp_h+cp_l = 2/(3*log2) */
  z_l = cp_l * p_h + p_l * cp + dp_l[k];
  /* log2(ax) = (ss+..)*2/(3*log2) = n + dp_h + z_h + z_l */
  t = static_cast<double>(n);
  t1 = (((z_h + z_l) + dp_h[k]) + t);
  SET_LOW_WORD(t1, 0);
  t2 = z_l - (((t1 - t) - dp_h[k]) - z_h);

  // t1 + t2 = log2(ax), sum up because we do not care about extra precision.
  return t1 + t2;
}

/*
 * Return the base 10 logarithm of x.  See e_log.c and k_log.h for most
 * comments.
 *
 *    log10(x) = (f - 0.5*f*f + k_log1p(f)) / ln10 + k * log10(2)
 * in not-quite-routine extra precision.
 */
double log10Old(double x) {
  static const double
      two54 = 1.80143985094819840000e+16,     /* 0x43500000, 0x00000000 */
      ivln10hi = 4.34294481878168880939e-01,  /* 0x3fdbcb7b, 0x15200000 */
      ivln10lo = 2.50829467116452752298e-11,  /* 0x3dbb9438, 0xca9aadd5 */
      log10_2hi = 3.01029995663611771306e-01, /* 0x3FD34413, 0x509F6000 */
      log10_2lo = 3.69423907715893078616e-13; /* 0x3D59FEF3, 0x11F12B36 */

  static const double zero = 0.0;
  static volatile double vzero = 0.0;

  double f, hfsq, hi, lo, r, val_hi, val_lo, w, y, y2;
  int32_t i, k, hx;
  u_int32_t lx;

  EXTRACT_WORDS(hx, lx, x);

  k = 0;
  if (hx < 0x00100000) { /* x < 2**-1022  */
    if (((hx & 0x7fffffff) | lx) == 0)
      return -two54 / vzero;           /* log(+-0)=-inf */
    if (hx < 0) return (x - x) / zero; /* log(-#) = NaN */
    k -= 54;
    x *= two54; /* subnormal number, scale up x */
    GET_HIGH_WORD(hx, x);
  }
  if (hx >= 0x7ff00000) return x + x;
  if (hx == 0x3ff00000 && lx == 0) return zero; /* log(1) = +0 */
  k += (hx >> 20) - 1023;
  hx &= 0x000fffff;
  i = (hx + 0x95f64) & 0x100000;
  SET_HIGH_WORD(x, hx | (i ^ 0x3ff00000)); /* normalize x or x/2 */
  k += (i >> 20);
  y = static_cast<double>(k);
  f = x - 1.0;
  hfsq = 0.5 * f * f;
  r = k_log1p(f);

  /* See e_log2.c for most details. */
  hi = f - hfsq;
  SET_LOW_WORD(hi, 0);
  lo = (f - hi) - hfsq + r;
  val_hi = hi * ivln10hi;
  y2 = y * log10_2hi;
  val_lo = y * log10_2lo + (lo + hi) * ivln10lo + lo * ivln10hi;

  /*
   * Extra precision in for adding y*log10_2hi is not strictly needed
   * since there is no very large cancellation near x = sqrt(2) or
   * x = 1/sqrt(2), but we do it anyway since it costs little on CPUs
   * with some parallelism and it reduces the error for many args.
   */
  w = y2 + val_hi;
  val_lo += (y2 - w) + val_hi;
  val_hi = w;

  return val_lo + val_hi;
}

double log10(double x) {
  static const double
      two54 = 1.80143985094819840000e+16, /* 0x43500000, 0x00000000 */
      ivln10 = 4.34294481903251816668e-01,
      log10_2hi = 3.01029995663611771306e-01, /* 0x3FD34413, 0x509F6000 */
      log10_2lo = 3.69423907715893078616e-13; /* 0x3D59FEF3, 0x11F12B36 */

  static const double zero = 0.0;
  static volatile double vzero = 0.0;

  double y;
  int32_t i, k, hx;
  u_int32_t lx;

  EXTRACT_WORDS(hx, lx, x);

  k = 0;
  if (hx < 0x00100000) { /* x < 2**-1022  */
    if (((hx & 0x7fffffff) | lx) == 0)
      return -two54 / vzero;           /* log(+-0)=-inf */
    if (hx < 0) return (x - x) / zero; /* log(-#) = NaN */
    k -= 54;
    x *= two54; /* subnormal number, scale up x */
    GET_HIGH_WORD(hx, x);
    GET_LOW_WORD(lx, x);
  }
  if (hx >= 0x7ff00000) return x + x;
  if (hx == 0x3ff00000 && lx == 0) return zero; /* log(1) = +0 */
  k += (hx >> 20) - 1023;

  i = (k & 0x80000000) >> 31;
  hx = (hx & 0x000fffff) | ((0x3ff - i) << 20);
  y = k + i;
  SET_HIGH_WORD(x, hx);
  SET_LOW_WORD(x, lx);

  double z = y * log10_2lo + ivln10 * log(x);
  return z + y * log10_2hi;
}

}  // namespace ieee754
}  // namespace base
}  // namespace v8
