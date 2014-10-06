/*
  This file is part of avrtest -- A simple simulator for the
  Atmel AVR family of microcontrollers designed to test the compiler.

  Copyright (C) 2001, 2002, 2003   Theodore A. Roth, Klaus Rudolph
  Copyright (C) 2007 Paulo Marques
  Copyright (C) 2008-2014 Free Software Foundation, Inc.
   
  avrtest is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.
  
  avrtest is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with avrtest; see the file COPYING.  If not, write to
  the Free Software Foundation, 59 Temple Place - Suite 330,
  Boston, MA 02111-1307, USA.  */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>

#include "testavr.h"
#include "options.h"
#include "sreg.h"

#ifndef AVRTEST_LOG
#error no function herein is needed without AVRTEST_LOG
#endif // AVRTEST_LOG

const char s_SREG[] = "CZNVSHTI";

// ports used for application <-> simulator interactions
#define IN_AVRTEST
#include "avrtest.h"

#define LEN_PERF_TAG_STRING  50
#define LEN_PERF_TAG_FMT    200
#define LEN_PERF_LABEL      100
#define LEN_LOG_STRING      500
#define LEN_LOG_XFMT        500

#define NUM_PERFS 8
#define NUM_PERF_CMDS 8

#include "logging.h"

static alog_t alog;
static perf_t perf;
static perfs_t perfs[NUM_PERFS];

void
log_append (const char *fmt, ...)
{
  if (alog.unused)
    return;

  va_list args;
  va_start (args, fmt);
  alog.pos += vsprintf (alog.pos, fmt, args);
  va_end (args);
}

static INLINE int
mask_to_bit (int val)
{
  switch (val)
    {
    default: return -1;
    case 1<<0 : return 0;
    case 1<<1 : return 1;
    case 1<<2 : return 2;
    case 1<<3 : return 3;
    case 1<<4 : return 4;
    case 1<<5 : return 5;
    case 1<<6 : return 6;
    case 1<<7 : return 7;
    }
}


// Patch the instruction mnemonic to be more familiar
// and more specific about bits
static void
log_patch_mnemo (const decoded_op *op, char *buf)
{
  int id, mask, style = 0;

  switch (id = op->data_index)
    {
    default:
      return;
    case ID_BLD:  case ID_SBI:  case ID_SBIS:  case ID_SBRS:
    case ID_BST:  case ID_CBI:  case ID_SBIC:  case ID_SBRC:
      mask = op->oper2;
      style = 1;
      break;
    case ID_BRBS:  case ID_BRBC:
      mask = op->oper2;
      style = 2;
      break;
    case ID_BSET: case ID_BCLR:
      mask = op->oper1;
      style = 3;
      break;
    }

  int val = mask_to_bit (mask);

  switch (style)
    {
    case 1:
      // CBI.* --> CBI.4 etc.
      buf[-1] = "01234567"[val];
      return;
    case 2:
      {
        const char *s = NULL;
        switch (mask)
          {
          // "BR*S" --> "BREQ" etc.
          // "BR*C" --> "BRNE" etc.
          case FLAG_Z: s = id == ID_BRBS ? "EQ" : "NE"; break;
          case FLAG_N: s = id == ID_BRBS ? "MI" : "PL"; break;
          case FLAG_S: s = id == ID_BRBS ? "LT" : "GE"; break;
          // "BR*C" --> "BRVC" etc.
          // "BR*S" --> "BRVS" etc.
          default: buf[-2] = s_SREG[val]; return;
          }
        buf[-2] = s[0];
        buf[-1] = s[1];
        return;
      }
    case 3:
      // SE* --> SEI  etc.
      // CL* --> CLI  etc.
      buf[-1] = s_SREG[val];
      return;
    }
}

void
log_add_instr (const decoded_op *op)
{
  char mnemo_[16];
  alog.id = op->data_index;
  const char *fmt, *mnemo = opcode_func_array[alog.id].mnemo;

  // OUT and ST* might turn on logging: always log them to alog.data[].
  alog.maybe_OUT = (alog.id == ID_OUT || mnemo[1] == 'T');

  int maybe_used = alog.maybe_log || alog.maybe_OUT;

  if ((alog.unused = !maybe_used))
    return;

  strcpy (mnemo_, mnemo);
  log_patch_mnemo (op, mnemo_ + strlen (mnemo));

  fmt = arch.pc_3bytes ? "%06x: %-7s " : "%04x: %-7s ";
  log_append (fmt, cpu_PC * 2, mnemo_);
}


void
log_add_flag_read (int mask, int value)
{
  if (alog.unused)
    return;

  int bit = mask_to_bit (mask);
  log_append (" %c->%c", s_SREG[bit], '0' + !!value);
}

void
log_add_data_mov (const char *format, int addr, int value)
{
  if (alog.unused)
    return;

  char name[16];
  const char *s_name = name;

  if (addr_SREG == addr)
    {
      char *s = name;
      for (const char *f = s_SREG; *f; f++, value >>= 1)
        if (value & 1)
          *s++ = *f;
      *s++ = '\0';
      log_append (format, s_name);
      return;
    }

  for (const magic_t *p = named_port; ; p++)
    {
      if (addr == p->addr)
        s_name = p->name;
      else if (p->name == NULL)
        sprintf (name, addr < 256 ? "%02x" : "%04x", addr);
      else
        continue;
      break;
    }

  log_append (format, s_name, value);
}


static void
putchar_escaped (char c)
{
  if (options.do_quiet)
    return;

  if (c == '\0') {}
  else if (c == '\n')  { putchar ('\\'); putchar ('n'); }
  else if (c == '\t')  { putchar ('\\'); putchar ('t'); }
  else if (c == '\r')  { putchar ('\\'); putchar ('r'); }
  else if (c == '\"')  { putchar ('\\'); putchar ('\"'); }
  else if (c == '\\')  { putchar ('\\'); putchar ('\\'); }
  else putchar (c);
}

// set argc and argv[] from -args
// LOG_PORT = LOG_GET_ARGS_CMD;
static void
do_put_args (byte x)
{
  // We have been asked to transfer command line arguments.
  if (--args.request == 1)
    {
      args.addr = (byte) x;
      return;
    }

  args.addr |= (byte) x << 8;

  // strip directory to save space
  const char *p, *program = options.program_name;
  if ((p = strrchr (program, '/')))    program = p;
  if ((p = strrchr (program, '\\')))   program = p;

  // put strings to args.addr 
  int argc = args.argc - args.i;
  int a = args.addr;
  byte* b = log_cpu_address (args.addr, 0);
  for (int i = args.i; i < args.argc; i++)
    {
      const char *arg = i == args.i ? program : args.argv[i];
      int len = 1 + strlen (arg);
      qprintf ("*** (%04x) <-- *argv[%d] = \"", a, i - args.i);
      strcpy ((char*) b, arg);
      a += len;
      b += len;
      for (int j = 0; j < len; j++)
        putchar_escaped (arg[j]);
      qprintf ("\"\n");
    }

  // put their addresses to argv[]
  int argv = a;
  int aa = args.addr;
  for (int i = args.i; i < args.argc; i++)
    {
      const char *arg = i == args.i ? program : args.argv[i];
      int len = 1 + strlen (arg);
      qprintf ("*** (%04x) <-- argv[%d] = %04x\n", a, i - args.i, aa);
      log_data_write_word (a, aa, 0);
      a += 2;
      aa += len;
    }
  qprintf ("*** (%04x) <-- argv[%d] = NULL\n", a, argc);
  log_data_write_word (a, aa, 0);

  // set argc, argc: picked up by exit.c:init_args() in .init8
  qprintf ("*** -args: at=%04x, argc=%d, argv=%04x\n", args.addr, argc, argv);
  qprintf ("*** R24 = %04x\n", argc);
  qprintf ("*** R22 = %04x\n", argv);

  log_put_word_reg (24, argc, 0);
  log_put_word_reg (22, argv, 0);
}


// IEEE 754 single
static avr_float_t
decode_avr_float (unsigned val)
{
  // float =  s  bbbbbbbb mmmmmmmmmmmmmmmmmmmmmmm
  //         31
  // s = sign (1)
  // b = biased exponent
  // m = mantissa

  int one;
  const int DIG_MANT = 23;
  const int DIG_EXP  = 8;
  const int EXP_BIAS = 127;
  avr_float_t af;

  int r = (1 << DIG_EXP) -1;
  unsigned mant = af.mant = val & ((1 << DIG_MANT) -1);
  val >>= DIG_MANT;
  af.exp_biased = val & r;
  af.exp = af.exp_biased - EXP_BIAS;

  val >>= DIG_EXP;
  af.sign_bit = val & 1;

  // Denorm?
  if (af.exp_biased == 0)
    af.fclass = FT_DENORM;
  else if (af.exp_biased < r)
    af.fclass = FT_NORM;
  else if (mant == 0)
    af.fclass = FT_INF;
  else
    af.fclass = FT_NAN;

  switch (af.fclass)
    {
    case FT_NORM:
    case FT_DENORM:
      one = af.fclass == FT_NORM;
      af.mant1 = mant | (one << DIG_MANT);
      af.x = ldexp ((double) af.mant1, af.exp - DIG_MANT);
      af.x = copysign (af.x, af.sign_bit ? -1.0 : 1.0);
      break;
    case FT_NAN:
      af.x = nan ("");
      break;
    case FT_INF:
      af.x = af.sign_bit ? -HUGE_VAL : HUGE_VAL;
      break;
    }

  return af;
}

// Copy a string from AVR target to the host, but not more than
// LEN_MAX characters.
static char*
read_string (char *p, unsigned addr, int flash_p, size_t len_max)
{
  char c;
  size_t n = 0;
  byte *p_avr = log_cpu_address (addr, flash_p);

  while (++n < len_max && (c = *p_avr++))
    if (c != '\r')
      *p++ = c;

  *p = '\0';
  return p;
}

// Read a value as unsigned from TICKS_PORT.  Bytesize (1..4) and
// signedness are determined by respective layout[].
// If the value is signed a cast to signed will do the conversion.
static unsigned
get_raw_value (const layout_t *lay)
{
  byte *p = log_cpu_address (addr_TICKS_PORT, 0);
  unsigned val = 0;

  if (lay->signed_p && (0x80 & p[lay->size - 1]))
    val = -1U;

  for (int n = lay->size; n;)
    val = (val << 8) | p[--n];

  return val;
}

static void
do_log_dump (int what)
{
  static int fmt_once = 0;
  static char xfmt[LEN_LOG_XFMT];
  static char string[LEN_LOG_STRING];
  const layout_t *lay = & layout[what];

  unsigned val = get_raw_value (lay);
  const char *fmt = fmt_once ? xfmt : lay->fmt;

  if (fmt_once == 1)
    fmt_once = 0;

  switch (what)
    {
    default:
      printf (fmt, val);
      break;

    case LOG_SET_FMT_ONCE_CMD:
    case LOG_SET_PFMT_ONCE_CMD:
      fmt_once = 1;
      read_string (xfmt, val, lay->in_rom, sizeof (xfmt));
      break;

    case LOG_SET_FMT_CMD:
    case LOG_SET_PFMT_CMD:
      fmt_once = -1;
      read_string (xfmt, val, lay->in_rom, sizeof (xfmt));
      break;

    case LOG_TAG_FMT_CMD:
    case LOG_TAG_PFMT_CMD:
      perf.pending_LOG_TAG_FMT = 1;
      read_string (perfs[0].tag.fmt, val, lay->in_rom,
                   sizeof (perfs[0].tag.fmt));
      break;

    case LOG_PSTR_CMD:
    case LOG_STR_CMD:
      read_string (string, val, lay->in_rom, sizeof (string));
      printf (fmt, string);
      break;

    case LOG_FLOAT_CMD:
      {
        avr_float_t af = decode_avr_float (val);
        printf (fmt, af.x);
      }
      break;
    }
}

static int
print_tag (const perf_tag_t *t, const char *no_tag, const char *tag_prefix)
{
  printf ("%s", tag_prefix);

  if (t->cmd < 0)
    return printf (no_tag);

  const char *fmt = *t->fmt ? t->fmt : layout[t->cmd].fmt;

  if (t->cmd == LOG_STR_CMD)
    return printf (fmt, t->string);
  else if (t->cmd == LOG_FLOAT_CMD)
    return printf (fmt, t->dval);
  else
    return printf (fmt, t->val);
}

static int
print_tags (const minmax_t *mm, const char *text)
{
  int pos;
  printf ("%s", text);
  if (mm->r_min == mm->r_max)
    return printf ("         -all-same-                      /\n");

  printf ("%9d %9d", mm->r_min, mm->r_max);
  pos = print_tag (& mm->tag_min, " -no-tag- ", "    ");
  printf ("%*s", pos >= 20 ? 0 : 20 - pos, " / ");
  print_tag (& mm->tag_max, " -no-tag- ", " ");
  return printf ("\n");
}

NOINLINE void
do_log_port_cmd (int x)
{
#define SET_LOGGING(F, P, C)                                            \
  do { options.do_log=(F); alog.perf_only=(P); alog.countdown=(C); } while(0)

  if (args.request)
    {
      do_put_args (x);
      return;
    }

  switch (LOG_CMD (x))
    {
    case 0:
      // Do perf-meter stuff only in avrtest*_log in order
      // to avoid impact on execution speed.
      perf.cmd[PERF_CMD(x)] = PERF_N (x) ? 1 << PERF_N (x) : PERF_ALL;
      perf.will_be_on = perf.cmd[PERF_START];
      break;

      // LOG_TAG_FMT sent the address of the format string, then
      // LOG_TAG_PERF to use that format on a specific perf-meter
    case LOG_TAG_PERF:
      {
        perfs_t *p = & perfs[PERF_N (x)];
        int cmd = 0, tag_cmd = PERF_TAG_CMD (x);

        switch (tag_cmd)
          {
          case PERF_TAG_STR:   cmd = LOG_STR_CMD;  break;
          case PERF_TAG_U16:   cmd = LOG_U16_CMD;  break;
          case PERF_TAG_U32:   cmd = LOG_U32_CMD;  break;
          case PERF_TAG_FLOAT: cmd = LOG_FLOAT_CMD; break;
          case PERF_LABEL:    cmd = LOG_STR_CMD;  break;
          case PERF_PLABEL:   cmd = LOG_PSTR_CMD; break;
          }

        const layout_t *lay = & layout[cmd];
        unsigned raw = get_raw_value (lay);

        if (PERF_LABEL == tag_cmd
            || PERF_PLABEL == tag_cmd)
          {
            if (raw)
              read_string (p->label, raw, lay->in_rom, sizeof (p->label));
            else
              * p->label = '\0';
            break;
          }

        perf_tag_t *t = & p->tag_for_start;

        t->cmd = cmd;
        t->val = raw;

        if (cmd == LOG_STR_CMD)
          read_string (t->string, t->val, 0, sizeof (t->string));
        else if (cmd == LOG_FLOAT_CMD)
          t->dval = decode_avr_float (t->val).x;

        if (perf.pending_LOG_TAG_FMT)
          strcpy (t->fmt, perfs[0].tag.fmt);
        else
          * t->fmt = '\0';
        perf.pending_LOG_TAG_FMT = 0;
      }
      break; // LOG_TAG_PERF

    case LOG_DUMP:
      // Dumping values to host's stdout.
      do_log_dump (LOG_NUM (x));
      break;

    case LOG_SET:
      // Turning logging on / off
      switch (LOG_NUM (x))
        {
        case LOG_GET_ARGS_CMD:
          args.request = 2;
          qprintf ("*** transfer %s-args\n", options.do_args ? "" : "-no");
          break;
        case LOG_ON_CMD:
          qprintf ("*** log On\n");
          SET_LOGGING (1, 0, 0);
          break;
        case LOG_OFF_CMD:
          qprintf ("*** log Off\n");
          SET_LOGGING (0, 0, 0);
          break;
        case LOG_PERF_CMD:
          qprintf ("*** performance log\n");
          SET_LOGGING (0, 1, 0);
          break;
        default:
          alog.count_val = LOG_NUM (x);
          qprintf ("*** start log %u\n", alog.count_val);
          SET_LOGGING (1, 0, 1 + alog.count_val);
          break;
        } // LOG_NUM
      break; // LOG_SET
    } // LOG_CMD
#undef SET_LOGGING
}


static INLINE void
minmax_update (minmax_t *mm, long x, const perfs_t *p)
{
  if (x < mm->min && p->tag.cmd >= 0) mm->tag_min = p->tag;
  if (x > mm->max && p->tag.cmd >= 0) mm->tag_max = p->tag;
  if (x < mm->min) { mm->min = x; mm->min_at = perf.pc; mm->r_min = p->n; }
  if (x > mm->max) { mm->max = x; mm->max_at = perf.pc; mm->r_max = p->n; }
}

static INLINE void
minmax_update_double (minmax_t *mm, double x, const perfs_t *p)
{
  if (x < mm->dmin && p->tag.cmd >= 0) mm->tag_min = p->tag;
  if (x > mm->dmax && p->tag.cmd >= 0) mm->tag_max = p->tag;
  if (x < mm->dmin) { mm->dmin = x; mm->min_at = perf.pc; mm->r_min = p->n; }
  if (x > mm->dmax) { mm->dmax = x; mm->max_at = perf.pc; mm->r_max = p->n; }
}

static INLINE void
minmax_init (minmax_t *mm, long at_start)
{
  mm->min = LONG_MAX;
  mm->max = LONG_MIN;
  mm->at_start = at_start;
  mm->tag_min.cmd = mm->tag_max.cmd = -1;
  mm->dmin = HUGE_VAL;
  mm->dmax = -HUGE_VAL;
  mm->ev2 = 0.0;
}

static int
perf_verbose_start (perfs_t *p, int i, int mode)
{
  qprintf ("\n--- ");

  if (!p->valid)
    {
      if (PERF_START == mode)
        qprintf ("Start T%d (round 1", i);
    }
  else if (PERF_START == mode)
    {
      if (PERF_STAT == p->valid)
        qprintf ("Start T%d ignored: T%d in Stat mode (%d values",
                 i, i, p->n);
      else if (p->on)
        qprintf ("Start T%d ignored: T%d already started (round %d",
                 i, i, p->n);
      else
        qprintf ("reStart T%d (round %d", i, 1 + p->n);
    }
  else if (PERF_START == p->valid)
    qprintf ("Stat T%d ignored: T%d is in Start/Stop mode (%s "
             "round %d", i, i, p->on ? "in" : "after", p->n);

  if (!options.do_quiet && mode == PERF_START)
    {
      perf_tag_t *t = & p->tag_for_start;
      print_tag (t->cmd >= 0 ? t : & p->tag, "", ", ");
      qprintf (")\n");
    }

  return (!p->valid
          || (mode >= PERF_STAT && p->valid == PERF_STAT)
          || (mode == PERF_START && p->valid == PERF_START && !p->on));
}

static void
perf_stat (perfs_t *p, int i, int stat)
{
  if (p->tag_for_start.cmd >= 0)
    p->tag = p->tag_for_start;
  else
    p->tag.cmd = -1;
  p->tag_for_start.cmd = -1;

  if (!p->valid)
    {
      // First value
      p->valid = PERF_STAT;
      p->on = p->n = 0;
      p->val_ev = 0.0;
      minmax_init (& p->val, 0);
    }

  double dval;
  signed sraw = get_raw_value (& layout[LOG_S32_CMD]);
  unsigned uraw = (unsigned) sraw & 0xffffffff;
  if (PERF_STAT_U32 == stat)       dval = (double) uraw;
  else if (PERF_STAT_S32 == stat)  dval = (double) sraw;
  else                             dval = decode_avr_float (uraw).x;

  p->n++;
  minmax_update_double (& p->val, dval, p);
  p->val.ev2 += dval * dval;
  p->val_ev += dval;

  if (!options.do_quiet)
    {
      qprintf ("Stat T%d (value %d = %e", i, p->n, dval);
      print_tag (& p->tag, "", ", ");
      qprintf (")\n");
    }
}

// LOG_PORT = PERF_START (i)
static void
perf_start (perfs_t *p, int i)
{
  {
    if (p->tag_for_start.cmd >= 0)
      p->tag = p->tag_for_start;
    else
      p->tag.cmd = -1;
    p->tag_for_start.cmd = -1;
  }

  if (!p->valid)
    {
      // First round begins
      p->valid = PERF_START;
      p->n = 0;
      p->insns = p->ticks = 0;
      minmax_init (& p->insn,  instr_count);
      minmax_init (& p->tick,  program_cycles);
      minmax_init (& p->calls, perf.calls);
      minmax_init (& p->sp,    perf.sp);
      minmax_init (& p->pc,    p->pc_start = cpu_PC);
    }

  // (Re)start
  p->on = 1;
  p->n++;
  p->insn.at_start = instr_count;
  p->tick.at_start = program_cycles;
}


// LOG_PORT = PERF_STOP (i)
static void
perf_stop (perfs_t *p, int i, int dumps, int dump, int sp)
{
  if (!dump)
    {
      int ret = 1;
      if (!p->valid)
        qprintf ("\n--- Stop T%d ignored: -unused-\n", i);
      else if (p->valid == PERF_START && !p->on)
        qprintf ("\n--- Stop T%d ignored: T%d already stopped (after "
                 "round %d)\n", i, i, p->n);
      else if (p->valid == PERF_STAT)
        qprintf ("\n--- Stop T%d ignored: T%d used for Stat (%d Values)\n",
                 i, i, p->n);
      else
        ret = 0;

      if (ret)
        return;
    }

  if (p->valid == PERF_START && p->on)
    {
      p->on = 0;
      p->pc.at_end = p->pc_end = perf.pc2;
      p->insn.at_end = instr_count -1;
      p->tick.at_end = perf.tick;
      p->calls.at_end = perf.calls;
      p->sp.at_end = sp;
      long ticks = p->tick.at_end - p->tick.at_start;
      int insns = p->insn.at_end - p->insn.at_start;
      p->tick.ev2 += (double) ticks * ticks;
      p->insn.ev2 += (double) insns * insns;
      p->ticks += ticks;
      p->insns += insns;
      minmax_update (& p->insn, insns, p);
      minmax_update (& p->tick, ticks, p);

      qprintf ("%sStop T%d (round %d",
               dumps == PERF_ALL ? "  " : "\n--- ", i, p->n);
      if (!options.do_quiet)
        print_tag (& p->tag, "", ", ");

      qprintf (", %04lx--%04lx, %ld Ticks)\n",
               2 * p->pc.at_start, 2 * p->pc.at_end, ticks);
    }
}


// LOG_PORT = PERF_DUMP (i)
static void
perf_dump (perfs_t *p, int i, int dumps)
{
  if (!p->valid)
    {
      if (dumps != PERF_ALL)
        printf (" Timer T%d \"%s\": -unused-\n\n", i, p->label);
      return;
    }

  long c = p->calls.at_start;
  long s = p->sp.at_start;
  if (p->valid == PERF_START)
    printf (" Timer T%d \"%s\" (%d round%s):  %04x--%04x\n"
            "              Instructions        Ticks\n"
            "    Total:      %7u"  "         %7u\n",
            i, p->label, p->n, p->n == 1 ? "" : "s",
            2 * p->pc_start, 2 * p->pc_end, p->insns, p->ticks);
  else
    printf (" Stat  T%d \"%s\" (%d Value%s)\n",
            i, p->label, p->n, p->n == 1 ? "" : "s");

  double e_x2, e_x;

  if (p->valid == PERF_START)
    {
      if (p->n > 1)
        {
          // Var(X) = E(X^2) - E^2(X)
          e_x2 = p->tick.ev2 / p->n; e_x = (double) p->ticks / p->n;
          double tick_sigma = sqrt (e_x2 - e_x*e_x);
          e_x2 = p->insn.ev2 / p->n; e_x = (double) p->insns / p->n;
          double insn_sigma = sqrt (e_x2 - e_x*e_x);

          printf ("    Mean:       %7d"  "         %7d\n"
                  "    Stand.Dev:  %7.1f""         %7.1f\n"
                  "    Min:        %7ld" "         %7ld\n"
                  "    Max:        %7ld" "         %7ld\n",
                  p->insns / p->n, p->ticks / p->n, insn_sigma, tick_sigma,
                  p->insn.min, p->tick.min, p->insn.max, p->tick.max);
        }

      printf ("    Calls (abs) in [%4ld,%4ld] was:%4ld now:%4ld\n"
              "    Calls (rel) in [%4ld,%4ld] was:%4ld now:%4ld\n"
              "    Stack (abs) in [%04lx,%04lx] was:%04lx now:%04lx\n"
              "    Stack (rel) in [%4ld,%4ld] was:%4ld now:%4ld\n",
              p->calls.min,   p->calls.max,     c, p->calls.at_end,
              p->calls.min-c, p->calls.max-c, c-c, p->calls.at_end-c,
              p->sp.max,      p->sp.min,        s, p->sp.at_end,
              s-p->sp.max,    s-p->sp.min,    s-s, s-p->sp.at_end);
      if (p->n > 1)
        {
          printf ("\n           Min round Max round    "
                  "Min tag           /   Max tag\n");
          print_tags (& p->calls, "    Calls  ");
          print_tags (& p->sp,    "    Stack  ");
          print_tags (& p->insn,  "    Instr. ");
          print_tags (& p->tick,  "    Ticks  ");
        }
    }
  else /* PERF_STAT */
    {
      e_x2 = p->val.ev2 / p->n;
      e_x =  p->val_ev  / p->n;
      double val_sigma = sqrt (e_x2 - e_x*e_x);
      printf ("    Mean:       %e     round    tag\n"
              "    Stand.Dev:  %e\n", e_x, val_sigma);
      printf ("    Min:        %e  %8d", p->val.dmin, p->val.r_min);
      print_tag (& p->val.tag_min, " -no-tag-", "    ");
      printf ("\n"
              "    Max:        %e  %8d", p->val.dmax, p->val.r_max);
      print_tag (& p->val.tag_max, " -no-tag-", "    ");
      printf ("\n");
    }

  printf ("\n");

  p->valid = 0;
  * p->label = '\0';
}


static void
perf_instruction (int id)
{
  // call depth
  switch (id)
    {
    case ID_RCALL: case ID_ICALL: case ID_CALL: case ID_EICALL:
      perf.calls++;
      break;
    case ID_RET:
      // GCC might use push/push/ret for indirect jump,
      // don't account these for call depth
      if (perf.id != ID_PUSH)
        perf.calls--;
      break;
    case ID_STS:
      alog.stat.sts++;
      break;
    case ID_LDS:
      alog.stat.lds++;
      break;
    case ID_SBRC: case ID_SBRS: case ID_SBIC: case ID_SBIS: case ID_CPSE:
      alog.stat.skip++;
      break;
    }
  if (id >= ID_LDD_Y && id <= ID_LD_Z_incr)
    alog.stat.load++;
  if (id >= ID_STD_Y && id <= ID_ST_Z_incr)
    alog.stat.store++;

  perf.id = id;
  perf.will_be_on = 0;

  // actions requested by LOG_PORT

  int dumps  = perf.cmd[PERF_DUMP];
  int starts = perf.cmd[PERF_START];
  int stops  = perf.cmd[PERF_STOP];
  int stats_u32   = perf.cmd[PERF_STAT_U32];
  int stats_s32   = perf.cmd[PERF_STAT_S32];
  int stats_float = perf.cmd[PERF_STAT_FLOAT];
  int stats = stats_u32 | stats_s32 | stats_float;

  int sp = log_data_read_SP();
  int cmd = starts || stops || dumps || stats;

  if (!perf.on && !cmd)
    goto done;

  perf.on = 0;

  if (dumps)
    printf ("\n--- Dump # %d:\n", ++perf.n_dumps);

  for (int i = 1; i < NUM_PERFS; i++)
    {
      perfs_t *p = &perfs[i];
      int start = (starts & (1 << i)) ? PERF_START : 0;
      int stop  = stops & (1 << i);
      int dump  = dumps & (1 << i);
      int stat_u32   = (stats_u32   & (1 << i)) ? PERF_STAT_U32 : 0;
      int stat_s32   = (stats_s32   & (1 << i)) ? PERF_STAT_S32 : 0;
      int stat_float = (stats_float & (1 << i)) ? PERF_STAT_FLOAT : 0;
      int stat_val = stat_u32 | stat_s32 | stat_float;

      if (stop | dump)
        perf_stop (p, i, dumps, dump, sp);

      if (dump)
        perf_dump (p, i, dumps);

      if (p->on)
        {
          minmax_update (& p->sp, sp, p);
          minmax_update (& p->calls, perf.calls, p);
        }

      if (start && perf_verbose_start (p, i, start))
        perf_start (p, i);
      else if (stat_val && perf_verbose_start (p, i, stat_val))
        perf_stat (p, i, stat_val);

      perf.on |= p->on;
    }

  memset (perf.cmd, 0, sizeof (perf.cmd));
 done:;
  // Store for the next call of ours.  Needed because log_dump_line()
  // must run after the instruction has performed and we might need
  // the values from before the instruction.
  perf.sp  = sp;
  perf.pc2 = perf.pc;
  perf.pc  = cpu_PC;
  perf.tick = program_cycles;
}


void
log_init (void)
{
  alog.pos = alog.data;
  alog.maybe_log = 1;

  for (int i = 1; i < NUM_PERFS; i++)
    perfs[i].tag_for_start.cmd = -1;
}


void
log_dump_line (int id)
{
  if (id && alog.countdown && --alog.countdown == 0)
    {
      options.do_log = 0;
      qprintf ("*** done log %u", alog.count_val);
    }

  int log_this = options.do_log
    || (alog.perf_only
        && (perf.on || perf.will_be_on));
  if (log_this || (log_this != alog.log_this))
    {
      alog.maybe_log = 1;
      puts (alog.data);
      if (id && log_this && alog.unused){
        leave (EXIT_STATUS_FATAL, "problem in log_dump_line");
      }
    }
  else
    alog.maybe_log = 0;

  alog.log_this = log_this;

  alog.stat.logged += log_this == 1;
  alog.stat.not_logged += log_this == 0;
  alog.stat.guess_good += log_this != alog.unused;
  alog.stat.guess_bad  += log_this == alog.unused;

  alog.pos = alog.data;
  *alog.pos = '\0';
  perf_instruction (id);
}

void
log_stat_guesses (void)
{
  const __typeof (alog.stat) *s = &alog.stat;
  unsigned n_insns = s->logged + s->not_logged;
  printf ("   %u Instr.:  log: %u, no log: %u, STS: %.3f%%, LDS: %.3f%%"
          ", Skips: %.3f%%, Loads: %.3f%%, Stores: %.3f%%\n"
          "   Bad Guesses: %u (%.2f%% of all, %.2f%% of unlogged)\n",
          n_insns, s->logged, s->not_logged,  100. * s->sts / n_insns,
          100. * s->lds / n_insns, 100. * s->skip / n_insns,
          100. * s->load / n_insns, 100. * s->store / n_insns,
          s->guess_bad, 100. * s->guess_bad / n_insns,
          s->not_logged ? 100. * s->guess_bad /  s->not_logged : 0.0);
}
