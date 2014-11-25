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

#include "base/logging.h"
#include "Common/ChunkFile.h"
#include "Core/Reporting.h"
#include "Core/Config.h"
#include "Core/Core.h"
#include "Core/CoreTiming.h"
#include "Core/Debugger/SymbolMap.h"
#include "Core/MemMap.h"
#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/MIPSCodeUtils.h"
#include "Core/MIPS/MIPSInt.h"
#include "Core/MIPS/MIPSTables.h"
#include "Core/HLE/ReplaceTables.h"

#include "MipsJit.h"
#include "CPUDetect.h"

void DisassembleMIPS(const u8 *data, int size) {
}

namespace MIPSComp
{

MIPSJitOptions::MIPSJitOptions() {
	enableBlocklink = true;
	immBranches = false;
	continueBranches = false;
	continueJumps = false;
	continueMaxInstructions = 300;
}

Jit::Jit(MIPSState *mips) : blocks(mips, this), mips_(mips) {
	logBlocks = 0;
	dontLogBlocks = 0;
	blocks.Init();
	AllocCodeSpace(1024 * 1024 * 16);
	GenerateFixedCode();
	js.startDefaultPrefix = mips_->HasDefaultPrefix();
}

void Jit::DoState(PointerWrap &p) {
	auto s = p.Section("Jit", 1, 2);
	if (!s)
		return;

	p.Do(js.startDefaultPrefix);
	if (s >= 2) {
		p.Do(js.hasSetRounding);
		js.lastSetRounding = 0;
	} else {
		js.hasSetRounding = 1;
	}
}

void Jit::DoDummyState(PointerWrap &p) {
	auto s = p.Section("Jit", 1, 2);
	if (!s)
		return;

	bool dummy = false;
	p.Do(dummy);
	if (s >= 2) {
		dummy = true;
		p.Do(dummy);
	}
}

void Jit::FlushAll() {
	//gpr.FlushAll();
	//fpr.FlushAll();
	FlushPrefixV();
}

void Jit::FlushPrefixV() {
	if ((js.prefixSFlag & JitState::PREFIX_DIRTY) != 0) {
		MOVI2R(V0, js.prefixS);
		SW(V0, CTXREG, offsetof(MIPSState, vfpuCtrl[VFPU_CTRL_SPREFIX]));
		js.prefixSFlag = (JitState::PrefixState) (js.prefixSFlag & ~JitState::PREFIX_DIRTY);
	}

	if ((js.prefixTFlag & JitState::PREFIX_DIRTY) != 0) {
		MOVI2R(V0, js.prefixT);
		SW(V0, CTXREG, offsetof(MIPSState, vfpuCtrl[VFPU_CTRL_TPREFIX]));
		js.prefixTFlag = (JitState::PrefixState) (js.prefixTFlag & ~JitState::PREFIX_DIRTY);
	}

	if ((js.prefixDFlag & JitState::PREFIX_DIRTY) != 0) {
		MOVI2R(V0, js.prefixD);
		SW(V0, CTXREG, offsetof(MIPSState, vfpuCtrl[VFPU_CTRL_DPREFIX]));
		js.prefixDFlag = (JitState::PrefixState) (js.prefixDFlag & ~JitState::PREFIX_DIRTY);
	}
}

void Jit::ClearCache() {
	blocks.Clear();
	ClearCodeSpace();
	GenerateFixedCode();
}

void Jit::InvalidateCache() {
	blocks.Clear();
}

void Jit::InvalidateCacheAt(u32 em_address, int length) {
	blocks.InvalidateICache(em_address, length);
}

void Jit::EatInstruction(MIPSOpcode op) {
	MIPSInfo info = MIPSGetInfo(op);
	if (info & DELAYSLOT) {
		ERROR_LOG_REPORT_ONCE(ateDelaySlot, JIT, "Ate a branch op.");
	}
	if (js.inDelaySlot) {
		ERROR_LOG_REPORT_ONCE(ateInDelaySlot, JIT, "Ate an instruction inside a delay slot.");
	}

	js.numInstructions++;
	js.compilerPC += 4;
	js.downcountAmount += MIPSGetInstructionCycleEstimate(op);
}

void Jit::CompileDelaySlot(int flags) {
	js.inDelaySlot = true;
	MIPSOpcode op = Memory::Read_Opcode_JIT(js.compilerPC + 4);
	MIPSCompileOp(op);
	js.inDelaySlot = false;

	if (flags & DELAYSLOT_FLUSH)
		FlushAll();
}


void Jit::Compile(u32 em_address) {
	if (GetSpaceLeft() < 0x10000 || blocks.IsFull()) {
		ClearCache();
	}
	int block_num = blocks.AllocateBlock(em_address);
	JitBlock *b = blocks.GetBlock(block_num);
	DoJit(em_address, b);
	blocks.FinalizeBlock(block_num, jo.enableBlocklink);

	if (js.startDefaultPrefix && js.MayHavePrefix()) {
		WARN_LOG(JIT, "An uneaten prefix at end of block: %08x", js.compilerPC - 4);
		js.LogPrefix();

		// Let's try that one more time.  We won't get back here because we toggled the value.
		js.startDefaultPrefix = false;
		ClearCache();
		Compile(em_address);
	}
}

void Jit::RunLoopUntil(u64 globalticks) {
	((void (*)())enterCode)();
}

const u8 *Jit::DoJit(u32 em_address, JitBlock *b) {
	js.cancel = false;
	js.blockStart = js.compilerPC = mips_->pc;
	js.lastContinuedPC = 0;
	js.initialBlockSize = 0;
	js.nextExit = 0;
	js.downcountAmount = 0;
	js.curBlock = b;
	js.compiling = true;
	js.inDelaySlot = false;
	js.PrefixStart();

	b->checkedEntry = GetCodePtr();
	// Check downcount
	FixupBranch noskip = BGEZ(DOWNCOUNTREG);
	MOVI2R(R_AT, js.blockStart);
	J((const void *)outerLoopPCInAT);
	SetJumpTarget(noskip);

	b->normalEntry = GetCodePtr();

	js.numInstructions = 0;
	while (js.compiling)
	{
		MIPSOpcode inst = Memory::Read_Opcode_JIT(js.compilerPC);
		js.downcountAmount += MIPSGetInstructionCycleEstimate(inst);

		MIPSCompileOp(inst);

		js.compilerPC += 4;
		js.numInstructions++;

		// Safety check, in case we get a bunch of really large jit ops without a lot of branching.
		if (GetSpaceLeft() < 0x800 || js.numInstructions >= JitBlockCache::MAX_BLOCK_INSTRUCTIONS)
		{
			FlushAll();
			WriteExit(js.compilerPC, js.nextExit++);
			js.compiling = false;
		}
	}

	b->codeSize = GetCodePtr() - b->normalEntry;

	// Don't forget to zap the newly written instructions in the instruction cache!
	FlushIcache();

	if (js.lastContinuedPC == 0)
		b->originalSize = js.numInstructions;
	else
	{
		// We continued at least once.  Add the last proxy and set the originalSize correctly.
		blocks.ProxyBlock(js.blockStart, js.lastContinuedPC, (js.compilerPC - js.lastContinuedPC) / sizeof(u32), GetCodePtr());
		b->originalSize = js.initialBlockSize;
	}

	return b->normalEntry;
}

void Jit::AddContinuedBlock(u32 dest) {
	// The first block is the root block.  When we continue, we create proxy blocks after that.
	if (js.lastContinuedPC == 0)
		js.initialBlockSize = js.numInstructions;
	else
		blocks.ProxyBlock(js.blockStart, js.lastContinuedPC, (js.compilerPC - js.lastContinuedPC) / sizeof(u32), GetCodePtr());
	js.lastContinuedPC = dest;
}

bool Jit::DescribeCodePtr(const u8 *ptr, std::string &name) {
	// TODO: Not used by anything yet.
	return false;
}

void Jit::Comp_RunBlock(MIPSOpcode op) {
	// This shouldn't be necessary, the dispatcher should catch us before we get here.
	ERROR_LOG(JIT, "Comp_RunBlock should never be reached!");
}

bool Jit::ReplaceJalTo(u32 dest) {
	return false;
}

void Jit::Comp_ReplacementFunc(MIPSOpcode op) {
	// We get here if we execute the first instruction of a replaced function. This means
	// that we do need to return to RA.

	// Inlined function calls (caught in jal) are handled differently.

	int index = op.encoding & MIPS_EMUHACK_VALUE_MASK;

	const ReplacementTableEntry *entry = GetReplacementFunc(index);
	if (!entry) {
		ERROR_LOG(HLE, "Invalid replacement op %08x", op.encoding);
		return;
	}

	if (entry->flags & REPFLAG_DISABLED) {
		MIPSCompileOp(Memory::Read_Instruction(js.compilerPC, true));
	} else if (entry->jitReplaceFunc) {
		MIPSReplaceFunc repl = entry->jitReplaceFunc;
		int cycles = (this->*repl)();

		if (entry->flags & (REPFLAG_HOOKENTER | REPFLAG_HOOKEXIT)) {
			// Compile the original instruction at this address.  We ignore cycles for hooks.
			MIPSCompileOp(Memory::Read_Instruction(js.compilerPC, true));
		} else {
			FlushAll();
			LW(R_AT, CTXREG, MIPS_REG_RA * 4);
			js.downcountAmount += cycles;
			WriteExitDestInR(R_AT);
			js.compiling = false;
		}
	} else if (entry->replaceFunc) {
		FlushAll();
		RestoreRoundingMode();
		MOVI2R(R_AT, js.compilerPC);
		MovToPC(R_AT);

		// Standard function call, nothing fancy.
		// The function returns the number of cycles it took in EAX.
		if (JInRange((const void *)(entry->replaceFunc))) {
			J((const void *)(entry->replaceFunc));
		} else {
			MOVI2R(R_AT, (u32)entry->replaceFunc);
			JR(R_AT);
		}

		if (entry->flags & (REPFLAG_HOOKENTER | REPFLAG_HOOKEXIT)) {
			// Compile the original instruction at this address.  We ignore cycles for hooks.
			ApplyRoundingMode();
			MIPSCompileOp(Memory::Read_Instruction(js.compilerPC, true));
		} else {
			ApplyRoundingMode();
			LW(R_AT, CTXREG, MIPS_REG_RA * 4);
			WriteDownCountR(V0);
			WriteExitDestInR(R_AT);
			js.compiling = false;
		}
	} else {
		ERROR_LOG(HLE, "Replacement function %s has neither jit nor regular impl", entry->name);
	}
}

void Jit::Comp_Generic(MIPSOpcode op) {
	FlushAll();
	MIPSInterpretFunc func = MIPSGetInterpretFunc(op);
	if (func)
	{
		SaveDowncount();
		RestoreRoundingMode();
		MOVI2R(V0, js.compilerPC); // TODO: Use gpr
		MovToPC(V0);
		MOVI2R(A0, op.encoding);
		QuickCallFunction(V0, (void *)func);
		ApplyRoundingMode();
		RestoreDowncount();
	}

	const MIPSInfo info = MIPSGetInfo(op);
	if ((info & IS_VFPU) != 0 && (info & VFPU_NO_PREFIX) == 0)
	{
		// If it does eat them, it'll happen in MIPSCompileOp().
		if ((info & OUT_EAT_PREFIX) == 0)
			js.PrefixUnknown();
	}
}

void Jit::MovFromPC(MIPSReg r) {
	LW(r, CTXREG, offsetof(MIPSState, pc));
}

void Jit::MovToPC(MIPSReg r) {
	SW(r, CTXREG, offsetof(MIPSState, pc));
}

void Jit::SaveDowncount() {
	SW(DOWNCOUNTREG, CTXREG, offsetof(MIPSState, downcount));
}

void Jit::RestoreDowncount() {
	LW(DOWNCOUNTREG, CTXREG, offsetof(MIPSState, downcount));
}

void Jit::WriteDownCount(int offset) {
	MOVI2R(V1, (u32)(js.downcountAmount + offset));
	SUBU(DOWNCOUNTREG, DOWNCOUNTREG, V1);
}

void Jit::WriteDownCountR(MIPSReg reg) {
	SUBU(DOWNCOUNTREG, DOWNCOUNTREG, reg);
}

void Jit::RestoreRoundingMode(bool force) {
}

void Jit::ApplyRoundingMode(bool force) {
}

void Jit::UpdateRoundingMode() {
}

void Jit::WriteExit(u32 destination, int exit_num) {
	WriteDownCount();
	JitBlock *b = js.curBlock;
	b->exitAddress[exit_num] = destination;
	b->exitPtrs[exit_num] = GetWritableCodePtr();

	// Link opportunity!
	int block = blocks.GetBlockNumberFromStartAddress(destination);
	if (block >= 0 && jo.enableBlocklink) {
		// It exists! Joy of joy!
		J(blocks.GetBlock(block)->checkedEntry);
		b->linkStatus[exit_num] = true;
	} else {
		MOVI2R(R_AT, destination);
		J((const void *)dispatcherPCInAT);
	}
}

void Jit::WriteExitDestInR(MIPSReg Reg)  {
	MovToPC(Reg);
	WriteDownCount();
	// TODO: shouldn't need an indirect branch here...
	J((const void *)dispatcher);
}

void Jit::WriteSyscallExit() {
	WriteDownCount();
	J((const void *)dispatcherCheckCoreState);
}

#define _RS ((op>>21) & 0x1F)
#define _RT ((op>>16) & 0x1F)
#define _RD ((op>>11) & 0x1F)
#define _FS ((op>>11) & 0x1F)
#define _FT ((op>>16) & 0x1F)
#define _FD ((op>>6) & 0x1F)
#define _POS ((op>>6) & 0x1F)
#define _SIZE ((op>>11) & 0x1F)

//memory regions:
//
// 08-0A
// 48-4A
// 04-05
// 44-45
// mov eax, addrreg
	// shr eax, 28
// mov eax, [table+eax]
// mov dreg, [eax+offreg]
	
}
