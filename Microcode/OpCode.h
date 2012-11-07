#ifndef _OPCODE_H_
#define _OPCODE_H_

#include <vector>

// Decoder 1
const unsigned char kD1PCInc = (1<<0);			// It is possible to do a kD1PCInc and a kD2CycleReset and still have the PC increment
const unsigned char kD1OpCodeLoad = (1<<1);		// Must not be executed directly after a kD1CycleReset
const unsigned char kD1PCToAddress = (1<<2);	// Otherwise the address from the ADDRL latches is loaded.
const unsigned char kD1AddrToAddress = 0;		// i.e. Not kD1PCToAddress
const unsigned char kD1AddrLLoad = (1<<3);
const unsigned char kD1AddrHLoad = (1<<4);
const unsigned char kD1PCLoad = (1<<5);			// Loads whatever is on the address bus to the PC. Needs to present the data in two ticks, one without kD1PCInc then one with kD1PCInc. This does not inc the PC but sets the output to the input.
const unsigned char kD1RAMWrite = (1<<6);		// The address lines need to be stable one tick before and after writing.
const unsigned char kD1CycleReset = (1<<7);		// This cycle state is executed and the cycle starts counting from 0 next tick. The fetched opcode in the temporary opcode latch is then used as the real opcode at tick zero.

// Decoder 2
const unsigned char kD2Unused = 0;
const unsigned char kD2R0ToDB = 1;
const unsigned char kD2R1ToDB = 2;
const unsigned char kD2R2ToDB = 3;
const unsigned char kD2R3ToDB = 4;
const unsigned char kD2R4ToDB = 5;
const unsigned char kD2R5ToDB = 6;
const unsigned char kD2R6ToDB = 7;
const unsigned char kD2STToDB = 8;				// For transferring the ST to the ALU
const unsigned char kD2ZeroToDB = 9;
const unsigned char kD2ADDRWLToDB = 10;			// Writes the address bus lo to the data bus
const unsigned char kD2ADDRWHToDB = 11;			// Writes the address bus hi to the data bus
const unsigned char kD2ALUResToDB = 12;			// Outputs the result of the last ALU calculation to the data bus
const unsigned char kD2ALUTempSTToDB = 13;		// From the last result of the ALU
const unsigned char kD2MemoryToDB = 14;			// When reading from the memory it likes to not have a data bus write straight after it, otherwise it may cause a contention. Also the memory should remain stable one tick after the read.
const unsigned char kD2FFToDB = 15;				// The default state when under reset or when the decoder output latch is not set.

const unsigned char kD2DoBranchLoad = (1<<4);	// Load the ALU carry result on a positive edge. Needs to present the data in two ticks, one without kD2BranchLoad then one with kD2BranchLoad.
const unsigned char kD2CPUWantBus = (1<<5);
const unsigned char kD2CPUHasBus = (1<<6);

const unsigned char kD2BUSDDR = (1<<7);			// To set the data direction for the external data bus

// Decoder 3
const unsigned char kD3ALUIn1Load = (1<<0);
const unsigned char kD3ALUIn2Load = (1<<1);
const unsigned char kD3ALUIn3Load = (1<<2);
const unsigned char kD3ALUOp_Dec = 0 << 3;		// Both inputs set to be the same
const unsigned char kD3ALUOp_Inc = 1 << 3;		// Both inputs set to be the same
const unsigned char kD3ALUOp_Add = 2 << 3;
const unsigned char kD3ALUOp_Sub = 3 << 3;
const unsigned char kD3ALUOp_Or  = 4 << 3;
const unsigned char kD3ALUOp_And = 5 << 3;
const unsigned char kD3ALUOp_Xor = 6 << 3;
const unsigned char kD3ALUOp_Lsl = 7 << 3;		// Both inputs set to be the same
const unsigned char kD3ALUOp_Lsr = 8 << 3;		// Both inputs set to be the same
const unsigned char kD3ALUOp_Rol = 9 << 3;		// Both inputs set to be the same
const unsigned char kD3ALUOp_Ror = 10 << 3;		// Both inputs set to be the same
const unsigned char kD3ALUOp_Cmp = 11 << 3;
const unsigned char kD3ALUOp_Sec = 12 << 3;		// Both inputs set to be status. Output back to status. Ignore ALU status.
		// Input ALU status *must be* zero else the extended operation is used
		// If ALUST != 0 then outputs input AND %00000010 = 0x02
const unsigned char kD3ALUOp_Clc = 13 << 3;		// Both inputs set to be status. Output back to status. Ignore ALU status.
		// Input ALU status *must be* zero else the extended operation is used
		// If ALUST != 0 then outputs input AND %00111101 = 0x3d
const unsigned char kD3ALUOp_Clv = 14 << 3;		// Both inputs set to be status. Output back to status.
		// Input ALU status *must be* zero else the extended operation is used
		// If ALUST != 0 then outputs input AND %11000000 = 0xc0
const unsigned char kD3ALUOp_Flags = 15 << 3;	// Both inputs set to be the same. Input ALUST. Output preserves flags except ZN and sets ZN depending on the number. For example, reading data from memory to a register needs to set the status so use this ALU function.

const unsigned char kD3ALUResLoad = (1<<7);

const unsigned char kALUInFlg_D = (1<<0);		// Not in terms of the real ALU input but in terms of the flags input variable
const unsigned char kALUInFlg_C = (1<<1);		// Not in terms of the real ALU input but in terms of the flags input variable
const unsigned char kALUInFlg_V = (1<<2);		// Not in terms of the real ALU input but in terms of the flags input variable
	// kALUInFlg_Special comes from either bit 4 (lowest bit of the high nybble) of the second ALU input or the output of ALU 1 for those instructions that need to know what is coming from the high nybble.
const unsigned char kALUInFlg_Special = (1<<3);	// Not in terms of the real ALU input but in terms of the flags input variable
const unsigned char kALUOutFlg_C = (1<<4);
const unsigned char kALUOutFlg_Z = (1<<5);
const unsigned char kALUOutFlg_V = (1<<6);
const unsigned char kALUOutFlg_N = (1<<7);

const unsigned char kALU1OutFlg_Special = (1<<7);

// Decoder 4
const unsigned char kD4DBToR0 = (1<<0);	// A
const unsigned char kD4DBToR1 = (1<<1);	// X
const unsigned char kD4DBToR2 = (1<<2);	// Y
const unsigned char kD4DBToR3 = (1<<3);	// SP lo
const unsigned char kD4DBToR4 = (1<<4);	// SP hi
const unsigned char kD4DBToR5 = (1<<5);
const unsigned char kD4DBToR6 = (1<<6);
const unsigned char kD4DBToST = (1<<7);

// Decoder 5
const unsigned char kD5IRQStateLE = (1<<0);
const unsigned char kD5IllegalOp = (1<<1);
const unsigned char kD5IRQLineRST = (1<<2);

class State
{
public:
	State();
	State(const int state);
	virtual ~State();
	void SetState(const unsigned char state);
	unsigned char GetState(void);
private:
	unsigned char mState;
};

class OpCode
{
public:
	OpCode();
	virtual ~OpCode();

	void AddState(State &decoder1 = State(),State &decoder2 = State(),State &decoder3 = State(),State &decoder4 = State(),State &decoder5 = State());

	void Append(const OpCode &fragment);

	void Write(size_t index,FILE *fp);

	// Validates the states currently in the opcode using the design rules for the hardware
	bool ValidateStates(void);

	size_t GetLength(void)
	{
		return mRealSize;
	}

protected:
	std::vector<unsigned char> mDecoders[5];
	bool mGotResetCycle;
	size_t mRealSize;
};

extern unsigned char PreserveCarryFlag(unsigned char inFlags);

extern unsigned char PreserveOverflowFlag(unsigned char inFlags);

extern unsigned char CalculateZeroFlag(unsigned char work);

extern unsigned char CalculateNegativeFlag(unsigned char work);

extern unsigned char CalculateCarryFlag(unsigned char work);

extern unsigned char CalculateNCZFlags(unsigned char work);

extern unsigned char CalculateNZFlags(unsigned char work);

extern unsigned char CalculateCZFlags(unsigned char work);

#endif
