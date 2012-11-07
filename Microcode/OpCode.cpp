#include <stdio.h>
#include <stdlib.h>
#include <vector>
#include <assert.h>
#include "OpCode.h"

State::State() : mState(0)
{
}

State::State(const int state)
{
	mState = (unsigned char) state;
}

State::~State()
{
}

void State::SetState(const unsigned char state)
{
	mState = state;
}

unsigned char State::GetState(void)
{
	return mState;
}


OpCode::OpCode() : mGotResetCycle(false) , mRealSize(0)
{
}

OpCode::~OpCode()
{
}

void OpCode::AddState(State &decoder1,State &decoder2,State &decoder3,State &decoder4,State &decoder5)
{
	mDecoders[0].push_back(decoder1.GetState());
	mDecoders[1].push_back(decoder2.GetState());
	mDecoders[2].push_back(decoder3.GetState());
	mDecoders[3].push_back(decoder4.GetState());
	mDecoders[4].push_back(decoder5.GetState());

	mRealSize++;

	assert(!mGotResetCycle && "This opcode has already got a kD1CycleReset, no more states allowed.");
	if ( (decoder1.GetState() & kD1CycleReset) == kD1CycleReset)
	{
		mGotResetCycle = true;
	}

	assert(ValidateStates() && "Adding state fails design rules");

	assert(mDecoders[0].size() <= 63 && "Too many states!");
}

void OpCode::Append(const OpCode &fragment)
{
	size_t i;
	for (i=0;i<fragment.mDecoders[0].size();i++)
	{
		AddState(State(fragment.mDecoders[0][i]),State(fragment.mDecoders[1][i]),State(fragment.mDecoders[2][i]),State(fragment.mDecoders[3][i]),State(fragment.mDecoders[4][i]));
	}
}

void OpCode::Write(size_t index,FILE *fp)
{
	// Make sure all the decoder is of a correct length and zeroed if needs be
	mDecoders[index].resize(64,0);
	size_t i;
	for (i=0;i<64;i++)
	{
		fwrite(&mDecoders[index][i],1,sizeof(mDecoders[index][i]),fp);
	}
}

// Validates the states currently in the opcode using the design rules for the hardware
bool OpCode::ValidateStates(void)
{
	if (mDecoders[0].size() == 0)
	{
		return true;
	}

	size_t pos = mDecoders[0].size() - 1;

	// Check kD1OpCodeLoad is not executed in tick 0.
	if ( (mDecoders[0][0] & kD1OpCodeLoad) == kD1OpCodeLoad )
	{
		return false;
	}

	// Check kD5IRQStateLE is not executed in tick 0.
	if ( (mDecoders[4][0] & kD5IRQStateLE) == kD5IRQStateLE )
	{
		return false;
	}

	// If kD4DBToR*, kD4DBToST are being used check that the DB is stable one cycle before and that we are not on the zeroth tick
/*
	if ( mDecoders[3][pos] )
	{
		if (pos == 0)
		{
			return false;
		}
		// Check we don't have a double high state, because it is useless and indicates a possible typo/bug
		if (mDecoders[3][pos] & mDecoders[3][pos-1])
		{
			return false;
		}
		// The data bus must be stable one cycle before
		if ((mDecoders[1][pos] & 15) != (mDecoders[1][pos-1] & 15))
		{
			return false;
		}
	}
*/

	// If kD3ALUIn*Load are being used check that the DB is stable one cycle before and that we are not on the zeroth tick
	if ( mDecoders[2][pos] & (kD3ALUIn1Load | kD3ALUIn2Load | kD3ALUIn3Load) )
	{
		if (pos == 0)
		{
			return false;
		}
		// Check we don't have a double high state, because it is useless and indicates a possible typo/bug
		if ( (mDecoders[2][pos] & (kD3ALUIn1Load | kD3ALUIn2Load | kD3ALUIn3Load)) & (mDecoders[2][pos-1] & (kD3ALUIn1Load | kD3ALUIn2Load | kD3ALUIn3Load)) )
		{
			return false;
		}
		// The data bus must be stable one cycle before
		if ((mDecoders[1][pos] & 15) != (mDecoders[1][pos-1] & 15))
		{
			return false;
		}
	}

	// If kD1Addr*Load are being used check that the DB is stable one cycle before and that we are not on the zeroth tick
	if ( mDecoders[0][pos] & (kD1AddrLLoad | kD1AddrHLoad) )
	{
		if (pos == 0)
		{
			return false;
		}
		// Check we don't have a double high state, because it is useless and indicates a possible typo/bug
		if ( (mDecoders[0][pos] & (kD1AddrLLoad | kD1AddrHLoad)) & (mDecoders[0][pos-1] & (kD1AddrLLoad | kD1AddrHLoad)) )
		{
			return false;
		}
		// The data bus must be stable one cycle before
		if ((mDecoders[1][pos] & 15) != (mDecoders[1][pos-1] & 15))
		{
			return false;
		}
	}

	if (mDecoders[0].size() == 1)
	{
		return true;
	}

	// Check we do not have any early kD2DoBranchLoad or kD3ALUResLoad
	if (pos <= 2)
	{
		if ( mDecoders[1][0] & kD2DoBranchLoad )
		{
			return false;
		}
		if ( mDecoders[2][0] & kD3ALUResLoad )
		{
			return false;
		}
	}
	else
	{
		// If kD2DoBranchLoad check the kD3ALUOp_ is stable before
		if ( mDecoders[1][pos] & kD2DoBranchLoad )
		{
			if ((mDecoders[2][pos] & (15<<3)) != (mDecoders[2][pos-1] & (15<<3)))
			{
				return false;
			}
//			if ((mDecoders[2][pos-1] & (15<<3)) != (mDecoders[2][pos-2] & (15<<3)))
//			{
//				return false;
//			}
		}

		// One cycle before kD3ALUResLoad the ALU op must be stable.
		if ( mDecoders[2][pos] & kD3ALUResLoad )
		{
			// No double state for kD3ALUResLoad
			if (mDecoders[2][pos-1] & kD3ALUResLoad)
			{
				return false;
			}
			// The ALU op must be stable before
			if ((mDecoders[2][pos] & (15<<3)) != (mDecoders[2][pos-1] & (15<<3)))
			{
				return false;
			}
//			if ((mDecoders[2][pos-1] & (15<<3)) != (mDecoders[2][pos-2] & (15<<3)))
//			{
//				return false;
//			}

			// Must not alter ALU inputs while trying to kD3ALUResLoad
			if (mDecoders[2][pos] & (kD3ALUIn1Load | kD3ALUIn2Load | kD3ALUIn3Load))
			{
				return false;
			}
			// Must not alter ALU inputs one cycle before while trying to kD3ALUResLoad
//			if (mDecoders[2][pos-1] & (kD3ALUIn1Load | kD3ALUIn2Load | kD3ALUIn3Load))
//			{
//				return false;
//			}
		}
	}

	// One cycle after kD1OpCodeLoad the data bus must be stable.
	if ( mDecoders[0][pos-1] & kD1OpCodeLoad)
	{
		// No double state for kD1OpCodeLoad
		if (mDecoders[0][pos] & kD1OpCodeLoad)
		{
			return false;
		}
		// The data bus must be stable one cycle after
		if ((mDecoders[1][pos] & 15) != (mDecoders[1][pos-1] & 15))
		{
			return false;
		}
	}

	// Must not swap between memory read and write memory, or vice versa, in next tick.
	if ( ((mDecoders[1][pos] & kD2MemoryToDB) == kD2MemoryToDB) )
	{
		if ( ((mDecoders[0][pos-1] & kD1RAMWrite) == kD1RAMWrite) )
		{
			return false;
		}
	}

	if ( ((mDecoders[0][pos] & kD1RAMWrite) == kD1RAMWrite) )
	{
		if ( ((mDecoders[1][pos-1] & kD2MemoryToDB) == kD2MemoryToDB) )
		{
			return false;
		}
	}

	// The address bus *must* be stable one tick before the memory is read from or written to.
	if ( ((mDecoders[1][pos] & kD2MemoryToDB) == kD2MemoryToDB) || ((mDecoders[0][pos] & kD1RAMWrite) == kD1RAMWrite) )
	{
		// Check for stable address bus
		if ( (mDecoders[0][pos] & kD1PCToAddress) != (mDecoders[0][pos-1] & kD1PCToAddress) )
		{
			return false;
		}
	}

	// The data bus and address bus *must* be stable one tick after memory is written to.
	if ( ((mDecoders[0][pos-1] & kD1RAMWrite) == kD1RAMWrite) )
	{
		// Check for stable address bus
		if ( (mDecoders[0][pos] & kD1PCToAddress) != (mDecoders[0][pos-1] & kD1PCToAddress) )
		{
			return false;
		}
	}

	// Check that kD1PCLoad does not immediately happen before kD1PCToAddress to avoid timing problems.
	if ( ((mDecoders[0][pos] & kD1PCToAddress) == kD1PCToAddress) )
	{
		if ( ((mDecoders[0][pos] & kD1PCLoad) == kD1PCLoad) || ((mDecoders[0][pos-1] & kD1PCLoad) == kD1PCLoad))
		{
			return false;
		}
	}


	// Check that kD5IRQStateLE also has kD2STToDB and that kD2STToDB is stable one tick before.
	if ( (mDecoders[4][pos] & kD5IRQStateLE) == kD5IRQStateLE )
	{
		// Check for stable kD2STToDB
		if ( (mDecoders[1][pos] & kD2STToDB) != kD2STToDB )
		{
			return false;
		}
		if ( (mDecoders[1][pos-1] & kD2STToDB) != kD2STToDB )
		{
			return false;
		}
	}



	return true;
}

unsigned char PreserveCarryFlag(unsigned char inFlags)
{
	int flags = 0;
	if (inFlags & kALUInFlg_C)
	{
		flags |= kALUOutFlg_C;
	}
	return flags;
}

unsigned char PreserveOverflowFlag(unsigned char inFlags)
{
	int flags = 0;
	if (inFlags & kALUInFlg_V)
	{
		flags |= kALUOutFlg_V;
	}
	return flags;
}

unsigned char CalculateZeroFlag(unsigned char work)
{
	if ( (work & 15) == 0)
	{
		return kALUOutFlg_Z;
	}
	return 0;
}

unsigned char CalculateNegativeFlag(unsigned char work)
{
	if ( (work & (1<<3)))
	{
		return kALUOutFlg_N;
	}
	return 0;
}

unsigned char CalculateCarryFlag(unsigned char work)
{
	if ( (work & 16))
	{
		return kALUOutFlg_C;
	}
	return 0;
}

unsigned char CalculateNCZFlags(unsigned char work)
{
	return CalculateNegativeFlag(work) | CalculateCarryFlag(work) | CalculateZeroFlag(work);
}

unsigned char CalculateNZFlags(unsigned char work)
{
	return CalculateNegativeFlag(work) | CalculateZeroFlag(work);
}

unsigned char CalculateCZFlags(unsigned char work)
{
	return CalculateCarryFlag(work) | CalculateZeroFlag(work);
}
