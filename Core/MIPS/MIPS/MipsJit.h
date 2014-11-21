// Copyright (c) 2012- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#pragma once

#include "Core/MIPS/JitCommon/JitState.h"
#include "Core/MIPS/JitCommon/JitBlockCache.h"
#include "../MIPSVFPUUtils.h"

#ifndef offsetof
#include "stddef.h"
#endif

// RegCache
#define BASEREG S7
#define CTXREG S6
#define CODEREG S5
#define DOWNCOUNTREG S4

#define DISABLE { Comp_Generic(op); return; }

namespace MIPSComp
{

struct MIPSJitOptions
{
	MIPSJitOptions();

	bool enableBlocklink;
	bool immBranches;
	bool continueBranches;
	bool continueJumps;
	int continueMaxInstructions;
};

class Jit : public MIPSGen::MIPSCodeBlock
{
public:
	Jit(MIPSState *mips);

	void DoState(PointerWrap &p);
	static void DoDummyState(PointerWrap &p);

	// Compiled ops should ignore delay slots
	// the compiler will take care of them by itself
	// OR NOT
	void Comp_Generic(MIPSOpcode op);

	void RunLoopUntil(u64 globalticks);

	void Compile(u32 em_address);	// Compiles a block at current MIPS PC
	const u8 *DoJit(u32 em_address, JitBlock *b);

	bool DescribeCodePtr(const u8 *ptr, std::string &name);

	void CompileDelaySlot(int flags);
	void EatInstruction(MIPSOpcode op);
	void AddContinuedBlock(u32 dest);

	void Comp_RunBlock(MIPSOpcode op);
	void Comp_ReplacementFunc(MIPSOpcode op);

	// Ops
	void Comp_ITypeMem(MIPSOpcode op) { DISABLE; }
	void Comp_Cache(MIPSOpcode op) { DISABLE; }

	MIPSGen::FixupBranch BranchTypeComp(int type, MIPSReg op1, MIPSReg op2);
	void Comp_RelBranch(MIPSOpcode op);
	void Comp_RelBranchRI(MIPSOpcode op);
	void Comp_FPUBranch(MIPSOpcode op) { DISABLE; }
	void Comp_FPULS(MIPSOpcode op) { DISABLE; }
	void Comp_FPUComp(MIPSOpcode op) { DISABLE; }
	void Comp_Jump(MIPSOpcode op);
	void Comp_JumpReg(MIPSOpcode op);
	void Comp_Syscall(MIPSOpcode op);
	void Comp_Break(MIPSOpcode op) { DISABLE; }

	void Comp_IType(MIPSOpcode op) { DISABLE; }
	void Comp_RType2(MIPSOpcode op) { DISABLE; }
	void Comp_RType3(MIPSOpcode op) { DISABLE; }
	void Comp_ShiftType(MIPSOpcode op) { DISABLE; }
	void Comp_Allegrex(MIPSOpcode op) { DISABLE; }
	void Comp_Allegrex2(MIPSOpcode op) { DISABLE; }
	void Comp_VBranch(MIPSOpcode op) { DISABLE; }
	void Comp_MulDivType(MIPSOpcode op) { DISABLE; }
	void Comp_Special3(MIPSOpcode op) { DISABLE; }

	void Comp_FPU3op(MIPSOpcode op) { DISABLE; }
	void Comp_FPU2op(MIPSOpcode op) { DISABLE; }
	void Comp_mxc1(MIPSOpcode op) { DISABLE; }

	void Comp_DoNothing(MIPSOpcode op) { DISABLE; }

	void Comp_SV(MIPSOpcode op) { DISABLE; }
	void Comp_SVQ(MIPSOpcode op) { DISABLE; }
	void Comp_VPFX(MIPSOpcode op) { DISABLE; }
	void Comp_VVectorInit(MIPSOpcode op) { DISABLE; }
	void Comp_VMatrixInit(MIPSOpcode op) { DISABLE; }
	void Comp_VDot(MIPSOpcode op) { DISABLE; }
	void Comp_VecDo3(MIPSOpcode op) { DISABLE; }
	void Comp_VV2Op(MIPSOpcode op) { DISABLE; }
	void Comp_Mftv(MIPSOpcode op) { DISABLE; }
	void Comp_Vmfvc(MIPSOpcode op) { DISABLE; }
	void Comp_Vmtvc(MIPSOpcode op) { DISABLE; }
	void Comp_Vmmov(MIPSOpcode op) { DISABLE; }
	void Comp_VScl(MIPSOpcode op) { DISABLE; }
	void Comp_Vmmul(MIPSOpcode op) { DISABLE; }
	void Comp_Vmscl(MIPSOpcode op) { DISABLE; }
	void Comp_Vtfm(MIPSOpcode op) { DISABLE; }
	void Comp_VHdp(MIPSOpcode op) { DISABLE; }
	void Comp_VCrs(MIPSOpcode op) { DISABLE; }
	void Comp_VDet(MIPSOpcode op) { DISABLE; }
	void Comp_Vi2x(MIPSOpcode op) { DISABLE; }
	void Comp_Vx2i(MIPSOpcode op) { DISABLE; }
	void Comp_Vf2i(MIPSOpcode op) { DISABLE; }
	void Comp_Vi2f(MIPSOpcode op) { DISABLE; }
	void Comp_Vh2f(MIPSOpcode op) { DISABLE; }
	void Comp_Vcst(MIPSOpcode op) { DISABLE; }
	void Comp_Vhoriz(MIPSOpcode op) { DISABLE; }
	void Comp_VRot(MIPSOpcode op) { DISABLE; }
	void Comp_VIdt(MIPSOpcode op) { DISABLE; }
	void Comp_Vcmp(MIPSOpcode op) { DISABLE; }
	void Comp_Vcmov(MIPSOpcode op) { DISABLE; }
	void Comp_Viim(MIPSOpcode op) { DISABLE; }
	void Comp_Vfim(MIPSOpcode op) { DISABLE; }
	void Comp_VCrossQuat(MIPSOpcode op) { DISABLE; }
	void Comp_Vsgn(MIPSOpcode op) { DISABLE; }
	void Comp_Vocp(MIPSOpcode op) { DISABLE; }

	int Replace_fabsf() { return 0; }

	JitBlockCache *GetBlockCache() { return &blocks; }

	void ClearCache();
	void InvalidateCache();
	void InvalidateCacheAt(u32 em_address, int length = 4);

	void EatPrefix() { js.EatPrefix(); }

private:
	void GenerateFixedCode();
	void FlushAll();
	void FlushPrefixV();

	void WriteDownCount(int offset = 0);
	void WriteDownCountR(MIPSReg reg);
	void RestoreRoundingMode(bool force = false);
	void ApplyRoundingMode(bool force = false);
	void UpdateRoundingMode();
	void MovFromPC(MIPSReg r);
	void MovToPC(MIPSReg r);

	bool ReplaceJalTo(u32 dest);

	void SaveDowncount();
	void RestoreDowncount();

	void WriteExit(u32 destination, int exit_num);
	void WriteExitDestInR(MIPSReg Reg);
	void WriteSyscallExit();

	JitBlockCache blocks;
	MIPSJitOptions jo;
	JitState js;

	MIPSState *mips_;

	int dontLogBlocks;
	int logBlocks;

public:
	// Code pointers
	const u8 *enterCode;

	const u8 *outerLoop;
	const u8 *outerLoopPCInR0;
	const u8 *dispatcherCheckCoreState;
	const u8 *dispatcherPCInR0;
	const u8 *dispatcher;
	const u8 *dispatcherNoCheck;

	const u8 *breakpointBailout;
};

typedef void (Jit::*MIPSCompileFunc)(MIPSOpcode opcode);
typedef int (Jit::*MIPSReplaceFunc)();

}	// namespace MIPSComp

