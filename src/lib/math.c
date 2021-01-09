/*
 * Math library.
 * Copyright (C) 2020-2021 LuaVela Authors. See Copyright Notice in COPYRIGHT
 * Copyright (C) 2015-2020 IPONWEB Ltd. See Copyright Notice in COPYRIGHT
 *
 * Portions taken verbatim or adapted from LuaJIT.
 * Copyright (C) 2005-2017 Mike Pall. See Copyright Notice in luajit.h
 */

#include <math.h>

#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"

#include "lj_obj.h"
#include "uj_lib.h"
#include "utils/uj_math.h"
#include "utils/fp.h"

/* ------------------------------------------------------------------------ */

#define LJLIB_MODULE_math

LJLIB_ASM(math_abs)             LJLIB_REC(.)
{
  uj_lib_checknum(L, 1);
  return FFH_RETRY;
}
LJLIB_ASM_(math_floor)          LJLIB_REC(.)
LJLIB_ASM_(math_ceil)           LJLIB_REC(.)

LJLIB_ASM(math_sqrt)            LJLIB_REC(.)
{
  uj_lib_checknum(L, 1);
  return FFH_RETRY;
}
LJLIB_ASM_(math_log10)          LJLIB_REC(.)
LJLIB_ASM_(math_exp)            LJLIB_REC(.)
LJLIB_ASM_(math_sin)            LJLIB_REC(.)
LJLIB_ASM_(math_cos)            LJLIB_REC(.)
LJLIB_ASM_(math_tan)            LJLIB_REC(.)
LJLIB_ASM_(math_asin)           LJLIB_REC(.)
LJLIB_ASM_(math_acos)           LJLIB_REC(.)
LJLIB_ASM_(math_atan)           LJLIB_REC(.)
LJLIB_ASM_(math_sinh)           LJLIB_REC(.)
LJLIB_ASM_(math_cosh)           LJLIB_REC(.)
LJLIB_ASM_(math_tanh)           LJLIB_REC(.)
LJLIB_ASM_(math_frexp)
LJLIB_ASM_(math_modf)

LJLIB_PUSH(57.29577951308232)
LJLIB_ASM_(math_deg)            LJLIB_REC(.)

LJLIB_PUSH(0.017453292519943295)
LJLIB_ASM_(math_rad)            LJLIB_REC(.)

LJLIB_ASM(math_log)             LJLIB_REC(.)
{
  double x = uj_lib_checknum(L, 1);
  if (L->base+1 < L->top) {
    double y = uj_lib_checknum(L, 2);
    x = log2(x); y = 1.0 / log2(y);
    setnumV(L->base-1, x*y);  /* Do NOT join the expression to x / y. */
    return FFH_RES(1);
  }
  return FFH_RETRY;
}

LJLIB_ASM(math_atan2)           LJLIB_REC(.)
{
  uj_lib_checknum(L, 1);
  uj_lib_checknum(L, 2);
  return FFH_RETRY;
}
LJLIB_ASM_(math_pow)            LJLIB_REC(.)
LJLIB_ASM_(math_fmod)

LJLIB_ASM(math_ldexp)           LJLIB_REC(.)
{
  uj_lib_checknum(L, 1);
  uj_lib_checknum(L, 2);
  return FFH_RETRY;
}

LJLIB_ASM(math_min)             LJLIB_REC(.)
{
  int i = 0;
  do { uj_lib_checknum(L, ++i); } while (L->base+i < L->top);
  return FFH_RETRY;
}
LJLIB_ASM_(math_max)            LJLIB_REC(.)

LJLIB_PUSH(3.14159265358979323846) LJLIB_SET(pi)
LJLIB_PUSH(1e310) LJLIB_SET(huge)

/* ------------------------------------------------------------------------ */

/* This implements a Tausworthe PRNG with period 2^223. Based on:
**   Tables of maximally-equidistributed combined LFSR generators,
**   Pierre L'Ecuyer, 1991, table 3, 1st entry.
** Full-period ME-CF generator with L=64, J=4, k=223, N1=49.
*/

/* PRNG state. */
struct RandomState {
  uint64_t gen[4];      /* State of the 4 LFSR generators. */
  int valid;            /* State is valid. */
};

/* Update generator i and compute a running xor of all states. */
#define TW223_GEN(i, k, q, s) \
  z = rs->gen[(i)]; \
  z = (((z<<(q))^z) >> ((k)-(s))) ^ ((z&((uint64_t)(int64_t)-1 << (64-(k))))<<(s)); \
  r ^= z; rs->gen[i] = z;

/* PRNG step function. Returns a double in the range 1.0 <= d < 2.0. */
LJ_NOINLINE uint64_t lj_math_random_step(struct RandomState *rs)
{
  uint64_t z, r = 0;
  TW223_GEN(0, 63, 31, 18)
  TW223_GEN(1, 58, 19, 28)
  TW223_GEN(2, 55, 24,  7)
  TW223_GEN(3, 47, 21,  8)
  return (r & U64x(000fffff,ffffffff)) | U64x(3ff00000,00000000);
}

/* PRNG initialization function. */
static void random_init(struct RandomState *rs, double d)
{
  uint32_t r = 0x11090601;  /* 64-k[i] as four 8 bit constants. */
  int i;
  for (i = 0; i < 4; i++) {
    FpConv conv;
    uint32_t m = 1u << (r&255);
    r >>= 8;
    conv.d = d = d * 3.14159265358979323846 + 2.7182818284590452354;
    if (conv.u < m) conv.u += m;  /* Ensure k[i] MSB of gen[i] are non-zero. */
    rs->gen[i] = conv.u;
  }
  rs->valid = 1;
  for (i = 0; i < 10; i++)
    lj_math_random_step(rs);
}

/* PRNG extract function. */
LJLIB_PUSH(top-2)  /* Upvalue holds userdata with RandomState. */
LJLIB_CF(math_random)           LJLIB_REC(.)
{
  int n = (int)(L->top - L->base);
  struct RandomState *rs = (struct RandomState *)(uddata(udataV(uj_lib_upvalue(L, 1))));
  FpConv conv;
  double d;
  if (LJ_UNLIKELY(!rs->valid)) random_init(rs, 0.0);
  conv.u = lj_math_random_step(rs);
  d = conv.d - 1.0;
  if (n > 0) {
    double r1 = uj_lib_checknum(L, 1);
    if (n == 1) {
      d = floor(d*r1) + 1.0;  /* d is an int in range [1, r1] */
    } else {
      double r2 = uj_lib_checknum(L, 2);
      d = floor(d*(r2-r1+1.0)) + r1;  /* d is an int in range [r1, r2] */
    }
  }  /* else: d is a double in range [0, 1] */
  setnumV(L->top++, d);
  return 1;
}

/* PRNG seed function. */
LJLIB_PUSH(top-2)  /* Upvalue holds userdata with RandomState. */
LJLIB_CF(math_randomseed)
{
  struct RandomState *rs = (struct RandomState *)(uddata(udataV(uj_lib_upvalue(L, 1))));
  random_init(rs, uj_lib_checknum(L, 1));
  return 0;
}

/* ------------------------------------------------------------------------ */

#include "lj_libdef.h"

LUALIB_API int luaopen_math(lua_State *L)
{
  struct RandomState *rs;
  rs = (struct RandomState *)lua_newuserdata(L, sizeof(struct RandomState));
  rs->valid = 0;  /* Use lazy initialization to save some time on startup. */
  LJ_LIB_REG(L, LUA_MATHLIBNAME, math);
#if defined(LUA_COMPAT_MOD)
  lua_getfield(L, -1, "fmod");
  lua_setfield(L, -2, "mod");
#endif
  return 1;
}

