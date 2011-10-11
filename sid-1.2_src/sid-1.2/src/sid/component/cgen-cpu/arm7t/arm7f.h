// arm7f.h - Main header for the ARM7 CPU family.  -*- C++ -*-

// Copyright (C) 1999, 2000 Red Hat.
// Portions Copyright (C) 2004 Sirius Satellite Radio Inc.
// This file is part of SID and is licensed under the GPL.
// See the file COPYING.SID for conditions for redistribution.

// A "cpu family" is a CGEN notion to put related variants under one roof.
// The "f" suffix in "arm7f" is for "family".

#ifndef ARM7F_H
#define ARM7F_H

#include "cgen-cpu.h"
#include "arm-desc.h"
// #include "arm-defs.h"
#include "arm-decode.h"
#include "thumb-decode.h"


namespace arm7f {

using namespace cgen;
using namespace arm;


// Put machine generated elements in base class as we want to override
// some of its methods.
class arm7f_cpu_cgen
{
// Include cgen generated elements.
#include "arm-cpu.h"

protected:
  // These are called from within inline functions in arm-cpu.h
  virtual void arm_tbit_set (BI newval) = 0;
  virtual void arm_mbits_set (UINT newval) = 0;

  inline void cgen_rtx_error (const char* msg) const
    {
      cerr << "arm7f-cpu rtx error: " << msg << endl;
      // throw cpu_exception ();
    }
};


enum eit 
{
  EIT_NONE = 0,
  EIT_RESET,
  EIT_DATA_ABORT,
  EIT_FIQ,
  EIT_IRQ,
  EIT_PREFETCH_ABORT,
  EIT_UNDEFINED_INSN,
  EIT_SWI_INSN
};


class arm7f_cpu: public arm7f_cpu_cgen, public cgen_bi_endian_cpu
{
public:
  arm7f_cpu ();
  ~arm7f_cpu () throw() {}

  // Called by the semantic code to perform a branch.
  // The result is the new address.
  inline void
  branch (PCADDR new_val, PCADDR& npc, sem_status& status)
    {
      npc = new_val;
    }

  // Called by the semantic code at the end of a non-cti insn.
  inline void
  done_insn (PCADDR npc, sem_status& status)
    {
      this->h_pc_set (npc);
    }

  // Called by the semantic code at the end of a cti insn.
  inline void
  done_cti_insn (PCADDR npc, sem_status& status)
    {
      this->h_pc_set (npc);
    }

  // Called by the semantic code to perform the swi insn.
  SI arm_swi (PCADDR pc, UINT trap);
  SI thumb_swi (PCADDR pc, UINT trap);

  void invalid_insn (PCADDR pc);
  void reset ();
  bool initialized_p;

private:
  // pbb engine [also includes scache engine]
  typedef pbb_engine<arm7f_cpu, arm_scache> arm_engine_t;
  typedef pbb_engine<arm7f_cpu, thumb_scache> thumb_engine_t;
  arm_engine_t arm_engine;
  thumb_engine_t thumb_engine;

  // ??? no need for one copy per cpu of some of this
  // Install a pbb or scache engine.
  void set_pbb_engine ();
  void set_scache_engine ();
  // Update the engine according to current_engine_type.
  void update_engine ();
  // Extra support is needed to handle the engine-type attribute.
  component::status set_engine_type (const string& s);

  void arm_tbit_set (BI newval);
  void arm_mbits_set (UINT newval);

  void step_insns (); // dispatches to one of following "workers"
  void step_arm ();
  void step_thumb ();
  void step_arm_pbb ();
  void step_thumb_pbb ();

  // PBB engine support.
  void arm_pbb_run ();
  arm_scache* arm_pbb_begin (PCADDR pc);
  void thumb_pbb_run ();
  thumb_scache* thumb_pbb_begin (PCADDR pc);

  void
  set_pc (host_int_4 v)
    {
      this->h_pc_set ((PCADDR) v);
    }

  host_int_4
  get_pc ()
    {
      return this->h_pc_get ();
    }

  // debug support routines
  string dbg_get_reg (host_int_4 n);
  component::status dbg_set_reg (host_int_4 n, const string& s);

  // processor mode (h-mbits inverted)
  output_pin nm_pin;
  arm::ARM_MODE mode ();

  // Exceptions, interrupts, and traps support

  eit pending_eit;
  void queue_eit (eit new_eit);
  int eit_priority (eit e);
  void process_eit (eit new_eit);

  void memory_trap (const cpu_memory_fault&);

  // current state of h-tbit
  binary_output_pin tbit_pin;

  // exception generating and support pins

  // Specifies asynchronous/synchronous nature of interrupts.
  // ??? Of little utility at the moment.
  binary_input_pin isync_pin;

  // FIQ/IRQ are generated by driving these pins (low).
  // It is synchronous if ISYNC is high, asynchronous if ISYNC is low.
  // If asynchronous, a cycle delay for synchronization is incurred.
  input_pin nfiq_pin;
  input_pin nirq_pin;

  // cpu is reset by driving this pin low
#if 0
  callback_pin<arm7f_cpu> nreset_pin;
  void do_nreset_pin (host_int_4 value);
#endif

  void flush_icache ();

  // Attribute helper functions.
  // cpsr
  string get_h_cpsr_for_attr () { return make_attribute (h_cpsr_get ()); }
  string get_h_cpsr2_for_attr ();
  component::status set_h_cpsr2_for_attr (const string& s) { return component::bad_value; }
  component::status set_h_cpsr_for_attr (const string& s)
    {
      host_int_4 value;
      component::status stat = parse_attribute (s, value);

      // save last cpsr
      host_int_4 last_cpsr = h_cpsr_get ();

      // this may fail via an exception thrown by cgen_rtx_error()
      try
	{
	  if (stat == component::ok)
	    h_cpsr_set (value);
	}
      catch (cpu_exception& t)
	{
	  try { h_cpsr_set (last_cpsr); } catch (...) { }
	  stat = component::bad_value;
	}

      return stat;
    }

  // overload state save & restore
  void stream_state (ostream& o) const;
  void destream_state (istream& i);

  // Override GETMEMSI, which has odd semantics for misaligned accesses.
public:
  inline
  SI GETMEMSI (PCADDR pc, ADDR addr)
  {
    SI word;
    unsigned short offset = addr % 4;

    switch (offset)
      {
      case 0:
	return cgen_bi_endian_cpu::GETMEMSI (pc, addr);
      case 1:
	word = cgen_bi_endian_cpu::GETMEMSI (pc, addr-1);
	return (word >> 8) | ((word & 0xFF) << 24); 
      case 2:
	word = cgen_bi_endian_cpu::GETMEMSI (pc, addr-2);
	return ((word & 0xFFFFU) << 16) | ((word & 0xFFFF0000U) >> 16);
      case 3:
	word = cgen_bi_endian_cpu::GETMEMSI (pc, addr-3);
	return (word << 8) | ((word & 0xFF000000) >> 24); 
      }
  }


  SI compute_operand2_immshift (SI rm, int type, int shift);
  SI compute_operand2_regshift (SI rm, int type, SI shift);
  BI compute_carry_out_immshift (SI rm, int type, int shift, BI cbit);
  BI compute_carry_out_regshift (SI rm, int type, SI shift, BI cbit);

  // FIXME: To be moved to rtl.
  inline BI 
  eval_cond (UINT cond, PCADDR pc)
    {
      switch (cond) {
      case arm::COND_EQ:
	return this->h_zbit_get ();
      case arm::COND_NE:
	return !(this->h_zbit_get ());
      case arm::COND_CS:
	return this->h_cbit_get ();
      case arm::COND_CC:
	return !(this->h_cbit_get ());
      case arm::COND_MI:
	return this->h_nbit_get ();
      case arm::COND_PL:
	return !(this->h_nbit_get ());
      case arm::COND_VS:
	return this->h_vbit_get ();
      case arm::COND_VC:
	return !(this->h_vbit_get ());
      case arm::COND_HI:
	return this->h_cbit_get () && !(this->h_zbit_get ());
      case arm::COND_LS:
	return !(this->h_cbit_get ()) || this->h_zbit_get ();
      case arm::COND_GE:
	return this->h_nbit_get () == this->h_vbit_get ();
      case arm::COND_LT:
	return this->h_nbit_get () != this->h_vbit_get ();
      case arm::COND_GT:
	return !(this->h_zbit_get ()) && 
	  (this->h_nbit_get () == this->h_vbit_get ());
      case arm::COND_LE:
	return this->h_zbit_get() ||
	  (this->h_nbit_get () != this->h_vbit_get ());
      case arm::COND_AL:
	return 1;
      default:
	this->invalid_insn (pc);
	return 0; // XXX: notreached?
      }
    }
}; // arm7_cpu

// RTL attribute accessors.
// ??? Need to allow application specific rtl generation.  Later.

#define GET_ATTR_R15_OFFSET() (abuf->idesc->attrs.get_r15_offset_attr ())

} // namespace arm7f

#endif // ARM7F_H
