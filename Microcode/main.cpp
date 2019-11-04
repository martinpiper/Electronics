#include <stdio.h>
#include <stdlib.h>
#include <vector>
#include <assert.h>
#include "OpCode.h"

// General overview of the schematic and design
// The processor can:
// Read/write to RAM and read from ROM.
// Latch an 8 bit opcode.
// Read/write the 16 bit address bus to/from the 8bit data bus in Lo/HI parts.
// Use any of seven general purpose 8 bit registers.
// For example: In 6502 terms R0-R2 A,X,Y and R3/4 being used as a stack pointer lo and hi. R5/6 used as temporary storage
// Maintain processor flags into the ALU from the data bus and storage of flags to the data bus.
// During reset being held low the processor has opcode 0xff and tick cycle 0 set. This enters the bootstrap phase.
// The special "boot" instruction 0xff goes through a sequence of loading the contents of 0xfffc/0xfffd into the program counter and executing code from there.

// When EXTWANTIRQ goes lo it causes the next instruction to start the IRQ when U34:B is used to branch the end of the opcode between fetching the next instruction or starting the IRQ.
// U4:C is used to test the loaded to data bus ST disable IRQ flag with EXTWANTIRQ.
// The CIA1 Emulation layer will use IRQTIMERCLOCK or the output of U204/U205 timers (if the counters are enabled in the simulation) which latch low to EXTWANTIRQ.
// The CIA1 Emulation layer for the LCD example board will ACK the IRQ request (return EXTWANTIRQ to high) when the memory location CIA1InterruptControl is read. This is the same as the C64.
// See the code around FindIRQLEAndReplace() and kD5IRQStateLE.

// RAM is located $0000-$9fff and $c000-$dfff
// ROM is located $a000-$bfff and $e000-$ffff

// U290:D0CHECK will check for the address being $dxxxx.
// It activates U291:2KCHUNKTEST which tests for $dYxxx. This maps to memory U292:VICSIDRAM and U293:COLCIARAM except for $dexx and $dfxx
// $dcxx goes to CIA1 as well as RAM
// $ddxx goes to CIA2 as well as RAM
// $dexx goes to EXTDEV and $df00 goes to DBG2

// The clock input generates up to 64 input states to the processor called "ticks".
// In terms of something like the 6502 four ticks could be thought of as a clock cycle but this number is not fixed as
// the exact design of the instruction timing is left to the microcode.
// The clock rising edge (from DOCLK) causes the microcode counters to increase for the decoders.
// The decoder ROM's output is buffered with the DCDRxLTCH ICs which only loads the decoder output on
// the high signal level from NOTCLK. This means there is half a clock cycle time for the decoder
// ROMs to output the correct data before the rest of the CPU logic gets to know about it.
// Since the decoder ROMs have a minimum access time of 150ns then this means there is a
// theoretical maximum clock speed of 3MHz for the internals of the CPU. (~6MHz for the full cycle
// with a half-cycle limit due to the later phase latch load.)
// DCDR4DLY Delays the register loads from the data bus (caused by decoder 4) so that register
// loads can be completed in one cycle as the data bus is initialised by the decoder 2 at the
// start of that cycle.

// Useful CPU references:
// http://www.6502.org/tutorials/6502opcodes.html
// http://www.oxyron.de/html/opcodes02.html

// C64 Kernal ROM patches to get things running (Stored in Electronics\C64ROMs)
// *Because the normal C64 kernal boot process does a memory check at $fd50 which takes ages under simulation
// replace $fd69 ($03) with $9f so it find the top of memory much more quickly.
// *The raster test needs NOPing ($EA) out at FF61 "D0 FB  BNE $FF5E" since we have no video hardware.
// The raster register RAM starts off as 00 but during the boot process it is quickly changed to be something else so the test is an endless tight loop.
// *Remove the following four addresses for the serial routines with RTS ($60) since they just waste time
// ED0E   20 A4 F0   JSR $F0A4
// ED40   78         SEI
// EE13   78         SEI
// EEB3   8A         TXA
// When testing with the C64 ROMs the IRQ must trigger at a slow rate to avoid the IRQ taking most of the time.
// For example at 1MHz the IRQ should trigger at 1Hz.
// TODO - find out why the IRQ takes so long, probably something to do with waiting for the keyboard device scan or something like that.

// Running at 1000Hz a capture resolution of 0.5m is good. (Which seems to be the default.) 5m on the reset edge is good.
// With EXTWANTBUS and C64 ROMs:
// 12:15.00 is when the BASIC screen banners start to display.
// 16:52.00 is around about when the BASIC ready prompt should be displayed.
// Running at 100000Hz a capture resolution of 5u is good.

// https://www.unitjuggler.com/convert-frequency-from-Hz-to-%C2%B5s(p).html?val=2000000
// e.g. 2000000 Hz = 0.5 µs
// Toolbar menu->System->Set Animation Options->Single Step Time: 0.5u (500n)
// This will be enough to show the opcode code on root sheet 2 advancing for each tick



// The simulation has been tested with the C64 ROMs at 1MHz CLK without any EXTWANTBUS and displays the BASIC startup screen at roughly 1.12 seconds.
// With IRQs enabled the simulation displays a flashing cursor around 3.4 secs into the simulation.

// Options for the main board, called CPU
// From Processor8BitData16BitAddress.DSN use the layers called:
//		ALU, registers, hard-coded values
//		Clock, decoders, power and other logic
//		Address calculation and address bus logic
//		Interrupt logic
//		External bus logic
// Then save as CPU.DSN.
// For Ares auto-place on ~150mm x ~160mm
// From Processor8BitData16BitAddress.DSN use the layers called:
//		LCD Expansion card
//		Debug LEDs
//		CIA1 Emulation
//		Memory
// Then save as ExpansionCard_LCD2.DSN.
// Note the component annotation values for ICs etc all start at 200.
// Don't forget to copy the parts for the actual LCD displays.
// For Ares auto-place on ~168mm x ~110mm
// Technology->Set Layer Stackup:Stackup Wizard:No. of Copper Layers:4

// Both boards use the same layers options from, Technology->Design Rule Manager->Net Classes tab
// Power 2 layers
// Signal 2 layers
// Use layers control 1, power, ground, control 2
// Fill the layers with unused copper

// TODO: Check with Proteus documentation about power plane defaults and layer options

// Notes for MK II
// In the layout of the BASIC and KERNAL ROM sockets it may be useful to replace their pads using the S-75-40 pad for a ZIF socket.
// Just select the S-75-40 pad mode, highlight the pin on the IC, then press the left mouse button twice. Or choose the "Edit Pin" option with the pin selected.

// Power planes
// Top copper : Net : VCC/VDD=POWER
// Bottom copper : Net : GND=POWER

// If space allows include the debugging header.

// ****************
// TODO
// ****************

// Must test if a counter/373/374 is enough to light eight LEDs with their own resistors.

// Remember that digital resistors are much better than analogue resistors in this simulation
// since digital ones greatly improve the speed of the simulation.

// Layout needs updating!

// Simplify U5 by removing and replacing with other logic. It seems to be a waste to use one huge
// IC for just a simple bit of logic.

// Add a parallel port input/output to the expansion board.

// Now when booting up the emulation will loop around this check waiting for ZPKeyBufferLength is to non-zero.
// Which of course will never happen because the keyboard is not connected even though there is a keyboard interrupt. :)
// E5CD   A5 C6      LDA $C6
// E5CF   85 CC      STA $CC
// E5D1   8D 92 02   STA $0292
// E5D4   F0 F7      BEQ $E5CD
// MPi: TODO: Add extra hardware to simulate the keyboard read with CIA1KeyboardColumnJoystickA and CIA1KeyboardRowsJoystickB.

// To get this design running at 4MHz the ALU operations need slowing down.
// Specifically kD3ALUResLoad and kD2DoBranchLoad need to have kD3ALUOp_* and kD3ALUIn*Load stable two cycles before.
// However opCmp_IndZPAddr_Y runs out of opcode space.
// * But since the whole design uses ROMs for the decoders then this isn't going to have any
// helpful improvement.

// If extra interrupts (like NMI not just IRQ) are supported at a later date then do what IRQ does, (NMI or IRQ), can be read from one of the other external lines perhaps.

// Check kD2ALUResToDB and does not conflict with any ALU in load.

// Check kD1PCInc does not happen for more than one consecutive state

// Instead of using an ALU dec for the lo SP, maybe try hooking up the PC counter registers so that they can count backwards.
// Can do this now because there is an extra decoder5 with free outputs.

// Opcodes to be properly implemented at a later date but emulated with NOP for now
// d8 f8	CLD/SED Don't make much sense since decimal mode is not yet supported by the ALU.


// At the moment the ALU shifts around the ST into kD2DoBranchLoad which gets its logic level from the ALU carry result ALUCARRY before it gets loaded by ALUTEMPST.
// MPi: TODO: ** For the branch instruction logic decode use a small ROM of 2x4 bit inputs and at least a bit output to set the "do branch" bit.
//		Or maybe a few logic gates and a selector


// Now the state is emulated at roughly four ticks per "cycle" now.


// Create an IO buffer board.
// This will store/latch several inputs from the MemoryMappedIOArea1/2 area coming from a real C64.
// This design microcode and ALU can then be re-purposed to provide a fast 16/32 addition, multiply, divide.
// Then the result can be output to several output latches, that can be read in the MemoryMappedIOArea1/2 space.
// TODO - Will need an edge connector for the cartridge port.
// Also some kind of latch/buffer with power separation in mind because the C64 power probably won't be enough to power all the ICs.

class Extensions : public OpCode
{
public:
	void FindIRQLEAndReplace(void)
	{
		size_t i;
		for (i=0;i<mDecoders[4].size();i++)
		{
			if ( (mDecoders[4][i] & kD5IRQStateLE) == kD5IRQStateLE )
			{
				// We are replacing the actual state at the position plus one
				i++;
				mRealSize = i;
				mGotResetCycle = false;

				// MPi: TODO: Tidy this so that it uses kNumDecoders
				mDecoders[0].resize(i,0);
				mDecoders[1].resize(i,0);
				mDecoders[2].resize(i,0);
				mDecoders[3].resize(i,0);
				mDecoders[4].resize(i,0);

				// Calculate $fc ($ff << 2) using temp R5
				AddState(State(),				State(kD2FFToDB));
				AddState(State(),				State(kD2FFToDB),					State(kD3ALUOp_Lsl | kD3ALUIn1Load | kD3ALUIn2Load));
				AddState(State(),				State(kD2ALUResToDB),				State(kD3ALUOp_Lsl | kD3ALUResLoad)		,State(kD4DBToR5));

				AddState(State(),				State(kD2R5ToDB));
				AddState(State(),				State(kD2R5ToDB),					State(kD3ALUOp_Lsl | kD3ALUIn1Load | kD3ALUIn2Load));

				// Load into the opcode
				AddState(State(kD1OpCodeLoad),	State(kD2ALUResToDB),				State(kD3ALUOp_Lsl | kD3ALUResLoad));
				AddState(State(),				State(kD2ALUResToDB),				State(kD3ALUOp_Lsl));
				AddState(State(kD1CycleReset));
				return;
			}
		}
	}
	void FetchExec(const bool doIRQCheck = true)
	{
		// If the opcode is quite short then we can do extra IRQ logic processing
		if (doIRQCheck && (GetLength() < 50))
		{
			// This does an automatic comparison in hardware on latch load with the wantIRQ and the ST interrupt disable flag state
			AddState(State(),			State(kD2STToDB),	State(),	State());
			AddState(State(),			State(kD2STToDB),	State(),	State(),	State(kD5IRQStateLE));
			AddState();	// Blank state to allow sync
			// To get the interrupt to work at this point the IRQ version of the opcode needs to:
			// Latch the IRQ request state with kD5IRQStateLE which is processed by the FindIRQLEAndReplace
			// and if true and the IRQ disable bit from the ST is false (tested by the NOR from U4:C) it executes the extra pseudo-instruction 0xfc into the next opcode to do.
		}

		// Must always be this end for every opcode
		LoadRegisterFromMemory(0,kD1OpCodeLoad | kD1PCToAddress);

		AddState(State(kD1CycleReset));
	}

	void FetchExecPreInc(const bool doIRQCheck = true)
	{
		AddState(State(kD1PCInc));
		FetchExec(doIRQCheck);
	}

	// Get the ST into the ALU
	void STToALU(void)
	{
		AddState(State(),			State(kD2STToDB));
		AddState(State(),			State(kD2STToDB),		State(kD3ALUIn3Load));
	}

	// Just do ST load from the last ALU result loading them
	void LoadSTFromALUFlags(void)
	{
		AddState(State(),			State(kD2ALUTempSTToDB),	State(),	State(kD4DBToST));
	}

	// Load flags into ALU then calculate output SZ flags using whatever is in the ALU input
	void LoadFlagsDoFlags(void)
	{
		AddState(State(),			State(kD2STToDB));
		AddState(State(),			State(kD2STToDB),		State(kD3ALUOp_Flags | kD3ALUIn3Load));
		AddState(State(),			State(kD2ALUTempSTToDB),	State(kD3ALUOp_Flags | kD3ALUResLoad)	,	State(kD4DBToST));
	}

	// Also primes the ALU
	void LoadImmediatePrimeALUPreInc(unsigned char d4Registers)
	{
		AddState(State(kD1PCInc));
		LoadRegisterFromMemory(d4Registers,kD1PCToAddress,true);
	}

	void TransferAToBPrimeALU(unsigned char d2A,unsigned char d4B)
	{
		AddState(State(),								State(d2A));
		AddState(State(),								State(d2A),	State(kD3ALUIn1Load | kD3ALUIn2Load),	State(d4B));
	}

	// Useful for absolute addressing opcodes
	void LoadAbsoluteAddressFromPCMemoryWithPreInc(void)
	{
		AddState(State(kD1PCInc));
		// Proceed to load the memory into the address lo and hi
		LoadRegisterFromMemory(0,kD1PCToAddress | kD1AddrLLoad);

		AddState(State(kD1PCInc));

		LoadRegisterFromMemory(0,kD1PCToAddress | kD1AddrHLoad);
	}

	// Useful for zero page addressing opcodes
	void LoadZeroPageAddressFromPCMemoryWithPreInc(void)
	{
		AddState(State(kD1PCInc));
		// Proceed to load the memory into the address lo and hi
		LoadRegisterFromMemory(0,kD1PCToAddress | kD1AddrLLoad);

		AddState(State(),				State(kD2ZeroToDB));
		AddState(State(kD1AddrHLoad),	State(kD2ZeroToDB));
	}

	// This state code reproduces the bug in the 6502
	// For example: JMP ($37FF) will fetch the low-byte from $37FF and the high-byte from $3700
	// Or ($ff),y will get the lo byte from $ff and the hi byte from $00
	// Proceed to load the memory into the address lo and hi
	// Corrupts r5 and r6
	void LoadIndAddrWith6502WrapBug(void)
	{
		LoadRegisterFromMemory(kD4DBToR5);
		// Load the ALU with addrl
		AddState(State(kD1AddrToAddress),		State(kD2ADDRWLToDB));
		AddState(State(kD1AddrToAddress),		State(kD2ADDRWLToDB),	State(kD3ALUOp_Inc | kD3ALUIn1Load | kD3ALUIn2Load));
		// ALU inc and write the ALU result to the addrl
		AddState(State(),				State(kD2ALUResToDB),					State(kD3ALUOp_Inc | kD3ALUResLoad));
		AddState(State(kD1AddrLLoad),	State(kD2ALUResToDB));
		// Load hi addr
		LoadRegisterFromMemory(kD4DBToR6);
		// Transfer r5/r6 to addr for PC loading
		AddState(State(),					State(kD2R5ToDB));
		AddState(State(kD1AddrLLoad),		State(kD2R5ToDB));
		AddState(State(),					State(kD2R6ToDB));
		AddState(State(kD1AddrHLoad),		State(kD2R6ToDB));
	}

	void LoadRegisterFromMemory(unsigned char d4Register,unsigned char d1Source = kD1AddrToAddress,const bool primeALU = false)
	{
		if (d1Source & kD1OpCodeLoad)
		{
			AddState(State(),State(kD2CPUWantBus));
			AddState(State(d1Source & kD1PCToAddress),		State(kD2BUSDDR | kD2CPUHasBus));
			AddState(State(d1Source & kD1PCToAddress),		State(kD2BUSDDR | kD2CPUHasBus | kD2MemoryToDB));
			if (primeALU)
			{
				AddState(State(d1Source),		State(kD2BUSDDR | kD2CPUHasBus | kD2MemoryToDB),	State(kD3ALUIn1Load | kD3ALUIn2Load),	State(d4Register));
			}
			else
			{
				AddState(State(d1Source),		State(kD2BUSDDR | kD2CPUHasBus | kD2MemoryToDB),	State(),	State(d4Register));
			}
			AddState(State(d1Source & kD1PCToAddress),		State(kD2BUSDDR | kD2CPUHasBus | kD2MemoryToDB));
			AddState(State(d1Source & kD1PCToAddress),		State(kD2BUSDDR | kD2CPUHasBus));
		}
		else
		{
			AddState(State(),State(kD2CPUWantBus));
			AddState(State(d1Source & kD1PCToAddress),		State(kD2BUSDDR | kD2CPUHasBus));
			AddState(State(d1Source & kD1PCToAddress),		State(kD2BUSDDR | kD2CPUHasBus | kD2MemoryToDB));
			if (primeALU)
			{
				AddState(State(d1Source),		State(kD2BUSDDR | kD2CPUHasBus | kD2MemoryToDB),	State(kD3ALUIn1Load | kD3ALUIn2Load),	State(d4Register));
			}
			else
			{
				AddState(State(d1Source),		State(kD2BUSDDR | kD2CPUHasBus | kD2MemoryToDB),	State(),	State(d4Register));
			}
			AddState(State(d1Source & kD1PCToAddress),		State(kD2BUSDDR | kD2CPUHasBus));
		}
	}

	void WriteRegisterToMemory(unsigned char d2Register,unsigned char d3ALUOp = 0,unsigned char d1Source = kD1AddrToAddress)
	{
		AddState(State(),State(kD2CPUWantBus));
		AddState(State(d1Source),						State(kD2CPUHasBus | d2Register),	State(d3ALUOp));
		AddState(State(d1Source | kD1RAMWrite),			State(kD2CPUHasBus | d2Register),	State(d3ALUOp));
		AddState(State(d1Source),						State(kD2CPUHasBus | d2Register),	State(d3ALUOp));
	}

	void AddRegisterToAddress(unsigned char d2Register)
	{
		// Add whatever is in XXX to the lo addr using the ALU
		AddState(State(kD1AddrToAddress),			State(kD2ADDRWLToDB));
		AddState(State(kD1AddrToAddress),			State(kD2ADDRWLToDB),	State(kD3ALUIn1Load));
		AddState(State(),			State(d2Register));
		AddState(State(),			State(d2Register),	State(kD3ALUIn2Load));
		AddState(State(),			State(kD2ZeroToDB),	State());
		AddState(State(),			State(kD2ZeroToDB),	State(kD3ALUOp_Add | kD3ALUIn3Load));
		// Do the add without carry and store the result
		AddState(State(),				State(kD2ALUResToDB),	State(kD3ALUOp_Add | kD3ALUResLoad));
		AddState(State(kD1AddrLLoad),	State(kD2ALUResToDB),	State(kD3ALUOp_Add));
		// Use the carry
		AddState(State(),			State(kD2ALUTempSTToDB));
		AddState(State(),			State(kD2ALUTempSTToDB),	State(kD3ALUIn3Load));
		AddState(State(kD1AddrToAddress),			State(kD2ADDRWHToDB));
		AddState(State(kD1AddrToAddress),			State(kD2ADDRWHToDB),	State(kD3ALUIn1Load));
		AddState(State(),			State(kD2ZeroToDB));
		AddState(State(),			State(kD2ZeroToDB),	State(kD3ALUOp_Add | kD3ALUIn2Load));
		// Do the add with zero and carry and store the result
		AddState(State(),				State(kD2ALUResToDB),	State(kD3ALUOp_Add | kD3ALUResLoad));
		AddState(State(kD1AddrHLoad),	State(kD2ALUResToDB),	State(kD3ALUOp_Add));
	}

	void AddRegisterToZeroPageAddress(unsigned char d2Register)
	{
		// Add whatever is in XXX to the lo addr using the ALU
		AddState(State(kD1AddrToAddress),			State(kD2ADDRWLToDB));
		AddState(State(kD1AddrToAddress),			State(kD2ADDRWLToDB),	State(kD3ALUIn1Load));
		AddState(State(),			State(d2Register));
		AddState(State(),			State(d2Register),	State(kD3ALUIn2Load));
		AddState(State(),			State(kD2ZeroToDB),	State());
		AddState(State(),			State(kD2ZeroToDB),	State(kD3ALUOp_Add | kD3ALUIn3Load));
		// Do the add without carry and store the result
		AddState(State(),				State(kD2ALUResToDB),	State(kD3ALUOp_Add | kD3ALUResLoad));
		AddState(State(kD1AddrLLoad),	State(kD2ALUResToDB));
	}

	void CompareRegisterWithImmediate(unsigned char d2Register)
	{
		// Read from registers and memory into ALU
		LoadImmediatePrimeALUPreInc(0);
		CompareCommon(d2Register);
	}

	// By default if the index register = 0 then no index is used
	void CompareRegisterWithAddrPlusRegister(unsigned char d2RegisterSource,unsigned char d2RegisterIndex = 0)
	{
		LoadAbsoluteAddressFromPCMemoryWithPreInc();
		if (d2RegisterIndex)
		{
			AddRegisterToAddress(d2RegisterIndex);
		}
		// Sets both ALU inputs with the memory loaded
		LoadRegisterFromMemory(0,kD1AddrToAddress,true);
		CompareCommon(d2RegisterSource);
	}

	void CompareRegisterWithZeroPageAddrPlusRegister(unsigned char d2RegisterSource,unsigned char d2RegisterIndex = 0)
	{
		LoadZeroPageAddressFromPCMemoryWithPreInc();
		if (d2RegisterIndex)
		{
			AddRegisterToZeroPageAddress(d2RegisterIndex);
		}
		// Sets both ALU inputs with the memory loaded
		LoadRegisterFromMemory(0,kD1AddrToAddress,true);
		CompareCommon(d2RegisterSource);
	}

	void CompareCommon(unsigned char d2RegisterSource)
	{
		STToALU();
		AddState(State(),			State(d2RegisterSource));
		AddState(State(),			State(d2RegisterSource),		State(kD3ALUOp_Cmp | kD3ALUIn1Load));
		// Do ALU compare and write ALU ST result to ST
		AddState(State(),			State(kD2ALUTempSTToDB),		State(kD3ALUOp_Cmp | kD3ALUResLoad)	,	State(kD4DBToST));
	}

	// Single input (in1 and in2 both the same) ALU operation
	void RegisterSimpleALUOp(unsigned char d2Register,unsigned char d3ALUOp,unsigned char d4Register)
	{
		// Load the ALU
		STToALU();
		AddState(State(),		State(d2Register));
		AddState(State(),		State(d2Register),	State(d3ALUOp | kD3ALUIn1Load | kD3ALUIn2Load));
		// Write the ALU result to the register
		AddState(State(),		State(kD2ALUResToDB),		State(d3ALUOp | kD3ALUResLoad)	,	State(d4Register));
		LoadSTFromALUFlags();
	}

	// Only fills in ALU in 1, not both inputs
	void RegisterALUOp(unsigned char d2Register,unsigned char d3ALUOp,unsigned char d4Register)
	{
		STToALU();
		AddState(State(),		State(d2Register));
		AddState(State(),		State(d2Register),	State(d3ALUOp | kD3ALUIn1Load));
		// Write the ALU result to the register
		AddState(State(),		State(kD2ALUResToDB),		State(d3ALUOp | kD3ALUResLoad)	,	State(d4Register));
		LoadSTFromALUFlags();
	}

	// All the logic that will take a branch. This needs to be appended onto a branch stub
	void TakeBranch(void)
	{
		// Get the next byte (branch offset) into ALU in1/2 and also int temp R5
		LoadImmediatePrimeALUPreInc(kD4DBToR5);

		// Get the upper bit into carry and sign extend it into temp R6
		// Shift b7 to carry
		AddState(State(),	State(kD2ALUTempSTToDB),	State(kD3ALUOp_Lsl));
//		AddState(State(),	State(kD2ALUTempSTToDB),	State(kD3ALUOp_Lsl));	// Remove this?
		AddState(State(),	State(kD2ALUTempSTToDB),	State(kD3ALUOp_Lsl | kD3ALUResLoad)	,	State(kD4DBToR6));
		// Get carry into bit by shifting it into the bottom of 0
		AddState(State(),	State(kD2R6ToDB));
		AddState(State(),	State(kD2R6ToDB),	State(kD3ALUIn3Load));
		AddState(State(),	State(kD2ZeroToDB));
		AddState(State(),	State(kD2ZeroToDB),	State(kD3ALUOp_Rol | kD3ALUIn1Load | kD3ALUIn2Load));
//		AddState(State(),	State(),	State(kD3ALUOp_Rol));	// Remove this?
		AddState(State(),	State(),	State(kD3ALUOp_Rol | kD3ALUResLoad));
		// Get 0 or 1 and xor with 0xff
		AddState(State(),	State(kD2ALUResToDB));
		AddState(State(),	State(kD2ALUResToDB),	State(kD3ALUIn1Load));
		AddState(State(),	State(kD2FFToDB));
		AddState(State(),	State(kD2FFToDB),	State(kD3ALUOp_Xor | kD3ALUIn2Load));
//		AddState(State(),	State(),	State(kD3ALUOp_Xor));	// Remove this?
		AddState(State(),	State(),	State(kD3ALUOp_Xor | kD3ALUResLoad));
		// Now inc
		AddState(State(),	State(kD2ALUResToDB));
		AddState(State(),	State(kD2ALUResToDB),	State(kD3ALUOp_Inc | kD3ALUIn1Load | kD3ALUIn2Load));
		AddState(State(),	State(kD2ALUResToDB),	State(kD3ALUOp_Inc | kD3ALUResLoad)	,	State(kD4DBToR6));
		// Get the lo byte of the PC to ALU in1
		AddState(State(kD1PCToAddress),	State(kD2ADDRWLToDB),		State());
		AddState(State(kD1PCToAddress),	State(kD2ADDRWLToDB),		State(kD3ALUIn1Load));
		// Get the offset to ALU in2
		AddState(State(kD1PCToAddress),	State(kD2R5ToDB),		State());
		AddState(State(kD1PCToAddress),	State(kD2R5ToDB),		State(kD3ALUIn2Load));
		// No carry or anything else
		AddState(State(),	State(kD2ZeroToDB),		State());
		AddState(State(),	State(kD2ZeroToDB),		State(kD3ALUOp_Add | kD3ALUIn3Load));
		// Add address
		AddState(State(),			State(),		State(kD3ALUOp_Add | kD3ALUResLoad));
		// lo to lo addr
		AddState(State(),				State(kD2ALUResToDB));
		AddState(State(kD1AddrLLoad),	State(kD2ALUResToDB));
		// Preserve carry for the PC hi byte calculation
		AddState(State(),	State(kD2ALUTempSTToDB),		State());
		AddState(State(),	State(kD2ALUTempSTToDB),		State(kD3ALUIn3Load));
		// Get the hi byte of the PC to ALU in1
		AddState(State(kD1PCToAddress),	State(kD2ADDRWHToDB),		State());
		AddState(State(kD1PCToAddress),	State(kD2ADDRWHToDB),		State(kD3ALUIn1Load));
		// Get the sign extended value from the branch offset
		AddState(State(kD1PCToAddress),	State(kD2R6ToDB),		State());
		AddState(State(kD1PCToAddress),	State(kD2R6ToDB),		State(kD3ALUOp_Add | kD3ALUIn2Load));
		// Add PC hi and offset hi plus carry for final PC hi
		AddState(State(),				State(kD2ALUResToDB),			State(kD3ALUOp_Add | kD3ALUResLoad));
		AddState(State(kD1AddrHLoad),	State(kD2ALUResToDB));
		// Load resulting PC from address latches
		AddState(State(kD1PCLoad));
		AddState(State(kD1PCLoad | kD1PCInc));
		AddState();
		FetchExecPreInc();
	}

	// Skips a byte since we don't want to take the branch.
	void SkipBranch(void)
	{
		// Skip the next byte
		AddState(State(kD1PCInc));
		AddState();
		FetchExecPreInc();
	}

	// BIT sets the Z flag as though the value in the address tested were ANDed with the accumulator.
	// The N and V flags are set to match bits 7 and 6 respectively in the value stored at the tested address. 
	// Load mem into ALU
	void CommonBITOpcode(void)
	{
		LoadRegisterFromMemory(0,kD1AddrToAddress,true);
		// Blank ST for this temp calc AND
		AddState(State(),			State(kD2R0ToDB));
		AddState(State(),			State(kD2R0ToDB),			State(kD3ALUIn1Load));
		AddState(State(),			State(kD2ZeroToDB));
		AddState(State(),			State(kD2ZeroToDB),			State(kD3ALUOp_And | kD3ALUIn3Load));
		AddState(State(),			State(),		State(kD3ALUOp_And | kD3ALUResLoad));
		// Setup the extended operation flag once here
		AddState(State(),			State(kD2FFToDB));
		AddState(State(),			State(kD2FFToDB),			State(kD3ALUIn3Load));
		// Now extract just the Z flag using the pattern generator
		AddState(State(),			State(kD2ALUTempSTToDB));
		AddState(State(),			State(kD2ALUTempSTToDB),	State(kD3ALUOp_Sec | kD3ALUIn1Load | kD3ALUIn2Load));
		AddState(State(),			State(kD2ALUResToDB),		State(kD3ALUOp_Sec | kD3ALUResLoad)	,	State(kD4DBToR5));
		// Get the real status and AND out the bits we want into temp r6
		AddState(State(),			State(kD2STToDB));
		AddState(State(),			State(kD2STToDB),	State(kD3ALUOp_Clc | kD3ALUIn1Load | kD3ALUIn2Load));
		AddState(State(),			State(kD2ALUResToDB),		State(kD3ALUOp_Clc | kD3ALUResLoad)	,	State(kD4DBToR6));
		// Now extract the NV flags from the memory
		LoadRegisterFromMemory(0,kD1AddrToAddress,true);
		AddState(State(),			State(),	State(kD3ALUOp_Clv));
		AddState(State(),			State(),	State(kD3ALUOp_Clv | kD3ALUResLoad));
		// Or both results together and then or into the ANDed ST in temp r6
		AddState(State(),			State(kD2ALUResToDB));
		AddState(State(),			State(kD2ALUResToDB),	State(kD3ALUIn1Load));
		AddState(State(),			State(kD2R5ToDB));
		AddState(State(),			State(kD2R5ToDB),		State(kD3ALUIn2Load));
		AddState(State(),			State(kD2ZeroToDB));
		AddState(State(),			State(kD2ZeroToDB),		State(kD3ALUOp_Or | kD3ALUIn3Load));
		AddState(State(),			State(),		State(kD3ALUOp_Or | kD3ALUResLoad));
		AddState(State(),			State(kD2ALUResToDB));
		AddState(State(),			State(kD2ALUResToDB),	State(kD3ALUIn1Load));
		AddState(State(),			State(kD2R6ToDB));
		AddState(State(),			State(kD2R6ToDB),		State(kD3ALUOp_Or | kD3ALUIn2Load));
		AddState(State(),			State(kD2ALUResToDB),		State(kD3ALUOp_Or | kD3ALUResLoad)	,	State(kD4DBToST));
		FetchExecPreInc();
	}
};



int main(int argc,char **argv)
{
	// Add an opcode that deliberately causes a hardware breakpoint to allow me to trap unimplemented opcodes.
	Extensions opIllegal;
	opIllegal.AddState(State(),State(),State(),State(),State(kD5IllegalOp));
	opIllegal.AddState();
	opIllegal.FetchExecPreInc();

	Extensions opNOP;
	opNOP.FetchExecPreInc();

	Extensions opLDA_Immediate;
	opLDA_Immediate.LoadImmediatePrimeALUPreInc(kD4DBToR0);
	opLDA_Immediate.LoadFlagsDoFlags();
	opLDA_Immediate.FetchExecPreInc();

	Extensions opLDX_Immediate;
	opLDX_Immediate.LoadImmediatePrimeALUPreInc(kD4DBToR1);
	opLDX_Immediate.LoadFlagsDoFlags();
	opLDX_Immediate.FetchExecPreInc();

	Extensions opLDY_Immediate;
	opLDY_Immediate.LoadImmediatePrimeALUPreInc(kD4DBToR2);
	opLDY_Immediate.LoadFlagsDoFlags();
	opLDY_Immediate.FetchExecPreInc();

	Extensions opTXA;
	opTXA.TransferAToBPrimeALU(kD2R1ToDB,kD4DBToR0);
	opTXA.LoadFlagsDoFlags();
	opTXA.FetchExecPreInc();

	Extensions opTAX;
	opTAX.TransferAToBPrimeALU(kD2R0ToDB,kD4DBToR1);
	opTAX.LoadFlagsDoFlags();
	opTAX.FetchExecPreInc();

	Extensions opTYA;
	opTYA.TransferAToBPrimeALU(kD2R2ToDB,kD4DBToR0);
	opTYA.LoadFlagsDoFlags();
	opTYA.FetchExecPreInc();

	Extensions opTAY;
	opTAY.TransferAToBPrimeALU(kD2R0ToDB,kD4DBToR2);
	opTAY.LoadFlagsDoFlags();
	opTAY.FetchExecPreInc();


	Extensions opTXS;
	opTXS.TransferAToBPrimeALU(kD2R1ToDB,kD4DBToR3);
	opTXS.LoadFlagsDoFlags();
	opTXS.FetchExecPreInc();

	Extensions opTSX;
	opTSX.TransferAToBPrimeALU(kD2R3ToDB,kD4DBToR1);
	opTSX.LoadFlagsDoFlags();
	opTSX.FetchExecPreInc();

	Extensions opJMP_Addr;
	opJMP_Addr.LoadAbsoluteAddressFromPCMemoryWithPreInc();
	// Load PC from address fetched from memory and held in the memory input latches
	opJMP_Addr.AddState(State(kD1PCLoad));
	opJMP_Addr.AddState(State(kD1PCLoad | kD1PCInc));	// The kD1PCInc doesn't inc, it loads due to the kD1PCLoad
	opJMP_Addr.AddState();
	opJMP_Addr.FetchExec();

	Extensions opJMP_IndAddr;
	opJMP_IndAddr.LoadAbsoluteAddressFromPCMemoryWithPreInc();
	// This state code fixes the jmp (addr) bug in the 6502 but we do not use it because it is not the same as the 6502
/*
	// Load PC from address fetched from memory and held in the memory input latches
	opJMP_IndAddr.AddState(State(kD1PCLoad));
	opJMP_IndAddr.AddState(State(kD1PCLoad | kD1PCInc));	// The kD1PCInc doesn't inc, it loads due to the kD1PCLoad
	opJMP_IndAddr.AddState();
	// Proceed to load the memory into the address lo and hi
	opJMP_IndAddr.LoadRegisterFromMemory(0,kD1PCToAddress | kD1AddrLLoad);
	opJMP_IndAddr.AddState(State(kD1PCInc));
	opJMP_IndAddr.LoadRegisterFromMemory(0,kD1PCToAddress | kD1AddrHLoad);
*/
	opJMP_IndAddr.LoadIndAddrWith6502WrapBug();
	// Load PC from address fetched from memory and held in the memory input latches
	opJMP_IndAddr.AddState(State(kD1PCLoad));
	opJMP_IndAddr.AddState(State(kD1PCLoad | kD1PCInc));	// The kD1PCInc doesn't inc, it loads due to the kD1PCLoad
	opJMP_IndAddr.AddState();
	opJMP_IndAddr.FetchExec();


	Extensions opJSR_Addr;
	opJSR_Addr.AddState(State(kD1PCInc));
	// Proceed to load the memory into the temp lo R5
	opJSR_Addr.LoadRegisterFromMemory(kD4DBToR5,kD1PCToAddress);
	opJSR_Addr.AddState(State(kD1PCInc));
	opJSR_Addr.AddState();

	// Now store the PC hi then lo onto the stack
	// Prepare the address bus with the stack pointer
	// First SP hi
	opJSR_Addr.AddState(State(),					State(kD2R4ToDB));
	opJSR_Addr.AddState(State(kD1AddrHLoad),		State(kD2R4ToDB));
	// Loading the SP lo also prepare the ALU to dec the lo SP value
	opJSR_Addr.AddState(State(),					State(kD2R3ToDB));
	opJSR_Addr.AddState(State(kD1AddrLLoad),		State(kD2R3ToDB),	State(kD3ALUIn1Load | kD3ALUIn2Load));

	// Get PC hi to temp R6 and push onto stack
	opJSR_Addr.AddState(State(kD1PCToAddress),		State(kD2ADDRWHToDB),	State(),	State(kD4DBToR6));
	opJSR_Addr.WriteRegisterToMemory(kD2R6ToDB);
	// Dec lo SP and load into addr lo
	opJSR_Addr.AddState(State(),		State(kD2ALUResToDB),		State(kD3ALUOp_Dec | kD3ALUResLoad)		,	State(kD4DBToR3));

	opJSR_Addr.AddState(State(),					State(kD2R3ToDB));
	opJSR_Addr.AddState(State(kD1AddrLLoad),		State(kD2R3ToDB),	State(kD3ALUIn1Load | kD3ALUIn2Load));
	// Get PC lo to temp R6 and push onto stack
	opJSR_Addr.AddState(State(kD1PCToAddress),		State(kD2ADDRWLToDB),	State(),	State(kD4DBToR6));
	opJSR_Addr.WriteRegisterToMemory(kD2R6ToDB);
	// Dec lo SP
	opJSR_Addr.AddState(State(),		State(kD2ALUResToDB),		State(kD3ALUOp_Dec | kD3ALUResLoad)	,	State(kD4DBToR3));


	// Load PC from address fetched from temp R5 and current memory
	opJSR_Addr.AddState(State(),					State(kD2R5ToDB));
	opJSR_Addr.AddState(State(kD1AddrLLoad),		State(kD2R5ToDB));
	opJSR_Addr.LoadRegisterFromMemory(0,kD1AddrHLoad | kD1PCToAddress);
	opJSR_Addr.AddState(State(kD1PCLoad));
	opJSR_Addr.AddState(State(kD1PCLoad | kD1PCInc));	// The kD1PCInc doesn't inc, it loads due to the kD1PCLoad
	opJSR_Addr.AddState();
	opJSR_Addr.FetchExec();

	Extensions opRTS;
	// First load SP lo/hi into PC addr lo/hi then load PC with this address. We use the fact that PC can auto increment.
	opRTS.AddState(State(),				State(kD2R3ToDB));
	opRTS.AddState(State(kD1AddrLLoad),	State(kD2R3ToDB));
	opRTS.AddState(State(),				State(kD2R4ToDB));
	opRTS.AddState(State(kD1AddrHLoad),	State(kD2R4ToDB));
	opRTS.AddState(State(kD1PCLoad));
	opRTS.AddState(State(kD1PCLoad | kD1PCInc));	// The kD1PCInc doesn't inc, it loads due to the kD1PCLoad
	opRTS.AddState();
	// Now pull the contents of the SP into addr lo/hi for eventual PC load
	opRTS.LoadAbsoluteAddressFromPCMemoryWithPreInc();
	// Now store the PC lo (which is pretending to be the SP lo) to the real SP lo. Don't bother with the SP hi since it doesn't change
	opRTS.AddState(State(kD1PCToAddress),	State(kD2ADDRWLToDB),	State(),	State(kD4DBToR3));
	// Now finally load the PC with the return address (which was pushed -1) and then fetch exec with pre-inc
	opRTS.AddState(State(kD1PCLoad));
	opRTS.AddState(State(kD1PCLoad | kD1PCInc));	// The kD1PCInc doesn't inc, it loads due to the kD1PCLoad
	opRTS.AddState();
	opRTS.FetchExecPreInc();


	Extensions opPHA;
	// Load SP into addr and also ALU
	opPHA.AddState(State(),				State(kD2R3ToDB));
	opPHA.AddState(State(kD1AddrLLoad),	State(kD2R3ToDB),	State(kD3ALUIn1Load | kD3ALUIn2Load));
	opPHA.AddState(State(),				State(kD2R4ToDB));
	opPHA.AddState(State(kD1AddrHLoad),	State(kD2R4ToDB));
	opPHA.WriteRegisterToMemory(kD2R0ToDB);
	// Dec lo SP
	opPHA.AddState(State(),		State(kD2ALUResToDB),		State(kD3ALUOp_Dec | kD3ALUResLoad)	,	State(kD4DBToR3));
	opPHA.FetchExecPreInc();

	Extensions opPLA;
	// Load lo SP into ALU
	opPLA.AddState(State(),		State(kD2R3ToDB));
	opPLA.AddState(State(),		State(kD2R3ToDB),	State(kD3ALUOp_Inc | kD3ALUIn1Load | kD3ALUIn2Load));
	// Inc lo SP
	opPLA.AddState(State(),		State(kD2ALUResToDB),		State(kD3ALUOp_Inc | kD3ALUResLoad),	State(kD4DBToR3));
	// Load SP into addr and also ALU
	opPLA.AddState(State(),				State(kD2R3ToDB));
	opPLA.AddState(State(kD1AddrLLoad),	State(kD2R3ToDB));
	opPLA.AddState(State(),				State(kD2R4ToDB));
	opPLA.AddState(State(kD1AddrHLoad),	State(kD2R4ToDB));
	opPLA.LoadRegisterFromMemory(kD4DBToR0,kD1AddrToAddress,true);
	opPLA.LoadFlagsDoFlags();
	opPLA.FetchExecPreInc();



	Extensions opPHP;
	// Load SP into addr and also ALU
	opPHP.AddState(State(),				State(kD2R3ToDB));
	opPHP.AddState(State(kD1AddrLLoad),	State(kD2R3ToDB),	State(kD3ALUIn1Load | kD3ALUIn2Load));
	opPHP.AddState(State(),				State(kD2R4ToDB));
	opPHP.AddState(State(kD1AddrHLoad),	State(kD2R4ToDB));
	opPHP.WriteRegisterToMemory(kD2STToDB);
	// Dec lo SP
	opPHP.AddState(State(),		State(kD2ALUResToDB),		State(kD3ALUOp_Dec | kD3ALUResLoad)	,	State(kD4DBToR3));
	opPHP.FetchExecPreInc();

	Extensions opPLP;
	// Load lo SP into ALU
	opPLP.AddState(State(),		State(kD2R3ToDB));
	opPLP.AddState(State(),		State(kD2R3ToDB),	State(kD3ALUOp_Inc | kD3ALUIn1Load | kD3ALUIn2Load));
	// Inc lo SP
	opPLP.AddState(State(),		State(kD2ALUResToDB),		State(kD3ALUOp_Inc | kD3ALUResLoad)	,	State(kD4DBToR3));
	// Load SP into addr and also ALU
	opPLP.AddState(State(),				State(kD2R3ToDB));
	opPLP.AddState(State(kD1AddrLLoad),	State(kD2R3ToDB));
	opPLP.AddState(State(),				State(kD2R4ToDB));
	opPLP.AddState(State(kD1AddrHLoad),	State(kD2R4ToDB));
	opPLP.LoadRegisterFromMemory(kD4DBToST,kD1AddrToAddress);
	opPLP.FetchExecPreInc();


	Extensions opLDA_Addr;
	opLDA_Addr.LoadAbsoluteAddressFromPCMemoryWithPreInc();
	opLDA_Addr.LoadRegisterFromMemory(kD4DBToR0,kD1AddrToAddress,true);
	opLDA_Addr.LoadFlagsDoFlags();
	opLDA_Addr.FetchExecPreInc();

	Extensions opSTA_Addr;
	opSTA_Addr.LoadAbsoluteAddressFromPCMemoryWithPreInc();
	opSTA_Addr.WriteRegisterToMemory(kD2R0ToDB);
	opSTA_Addr.FetchExecPreInc();

	Extensions opLDX_Addr;
	opLDX_Addr.LoadAbsoluteAddressFromPCMemoryWithPreInc();
	opLDX_Addr.LoadRegisterFromMemory(kD4DBToR1,kD1AddrToAddress,true);
	opLDX_Addr.LoadFlagsDoFlags();
	opLDX_Addr.FetchExecPreInc();

	Extensions opSTX_Addr;
	opSTX_Addr.LoadAbsoluteAddressFromPCMemoryWithPreInc();
	opSTX_Addr.WriteRegisterToMemory(kD2R1ToDB);
	opSTX_Addr.FetchExecPreInc();

	Extensions opLDY_Addr;
	opLDY_Addr.LoadAbsoluteAddressFromPCMemoryWithPreInc();
	opLDY_Addr.LoadRegisterFromMemory(kD4DBToR2,kD1AddrToAddress,true);
	opLDY_Addr.LoadFlagsDoFlags();
	opLDY_Addr.FetchExecPreInc();

	Extensions opSTY_Addr;
	opSTY_Addr.LoadAbsoluteAddressFromPCMemoryWithPreInc();
	opSTY_Addr.WriteRegisterToMemory(kD2R2ToDB);
	opSTY_Addr.FetchExecPreInc();

	Extensions opLDA_Addr_X;
	opLDA_Addr_X.LoadAbsoluteAddressFromPCMemoryWithPreInc();
	opLDA_Addr_X.AddRegisterToAddress(kD2R1ToDB);
	opLDA_Addr_X.LoadRegisterFromMemory(kD4DBToR0,kD1AddrToAddress,true);
	opLDA_Addr_X.LoadFlagsDoFlags();
	opLDA_Addr_X.FetchExecPreInc();

	Extensions opLDA_Addr_Y;
	opLDA_Addr_Y.LoadAbsoluteAddressFromPCMemoryWithPreInc();
	opLDA_Addr_Y.AddRegisterToAddress(kD2R2ToDB);
	opLDA_Addr_Y.LoadRegisterFromMemory(kD4DBToR0,kD1AddrToAddress,true);
	opLDA_Addr_Y.LoadFlagsDoFlags();
	opLDA_Addr_Y.FetchExecPreInc();

	Extensions opSTA_Addr_X;
	opSTA_Addr_X.LoadAbsoluteAddressFromPCMemoryWithPreInc();
	opSTA_Addr_X.AddRegisterToAddress(kD2R1ToDB);
	opSTA_Addr_X.WriteRegisterToMemory(kD4DBToR0);
	opSTA_Addr_X.FetchExecPreInc();

	Extensions opSTA_Addr_Y;
	opSTA_Addr_Y.LoadAbsoluteAddressFromPCMemoryWithPreInc();
	opSTA_Addr_Y.AddRegisterToAddress(kD2R2ToDB);
	opSTA_Addr_Y.WriteRegisterToMemory(kD4DBToR0);
	opSTA_Addr_Y.FetchExecPreInc();

	Extensions opLDY_Addr_X;
	opLDY_Addr_X.LoadAbsoluteAddressFromPCMemoryWithPreInc();
	opLDY_Addr_X.AddRegisterToAddress(kD2R1ToDB);
	opLDY_Addr_X.LoadRegisterFromMemory(kD4DBToR2,kD1AddrToAddress,true);
	opLDY_Addr_X.LoadFlagsDoFlags();
	opLDY_Addr_X.FetchExecPreInc();

	Extensions opLDX_Addr_Y;
	opLDX_Addr_Y.LoadAbsoluteAddressFromPCMemoryWithPreInc();
	opLDX_Addr_Y.AddRegisterToAddress(kD2R2ToDB);
	opLDX_Addr_Y.LoadRegisterFromMemory(kD4DBToR1,kD1AddrToAddress,true);
	opLDX_Addr_Y.LoadFlagsDoFlags();
	opLDX_Addr_Y.FetchExecPreInc();





	Extensions opLDA_ZPAddr;
	opLDA_ZPAddr.LoadZeroPageAddressFromPCMemoryWithPreInc();
	opLDA_ZPAddr.LoadRegisterFromMemory(kD4DBToR0,kD1AddrToAddress,true);
	opLDA_ZPAddr.LoadFlagsDoFlags();
	opLDA_ZPAddr.FetchExecPreInc();

	Extensions opSTA_ZPAddr;
	opSTA_ZPAddr.LoadZeroPageAddressFromPCMemoryWithPreInc();
	opSTA_ZPAddr.WriteRegisterToMemory(kD2R0ToDB);
	opSTA_ZPAddr.FetchExecPreInc();

	Extensions opLDX_ZPAddr;
	opLDX_ZPAddr.LoadZeroPageAddressFromPCMemoryWithPreInc();
	opLDX_ZPAddr.LoadRegisterFromMemory(kD4DBToR1,kD1AddrToAddress,true);
	opLDX_ZPAddr.LoadFlagsDoFlags();
	opLDX_ZPAddr.FetchExecPreInc();

	Extensions opLDX_ZPAddr_Y;
	opLDX_ZPAddr_Y.LoadZeroPageAddressFromPCMemoryWithPreInc();
	opLDX_ZPAddr_Y.AddRegisterToZeroPageAddress(kD2R2ToDB);
	opLDX_ZPAddr_Y.LoadRegisterFromMemory(kD4DBToR1,kD1AddrToAddress,true);
	opLDX_ZPAddr_Y.LoadFlagsDoFlags();
	opLDX_ZPAddr_Y.FetchExecPreInc();

	Extensions opSTX_ZPAddr;
	opSTX_ZPAddr.LoadZeroPageAddressFromPCMemoryWithPreInc();
	opSTX_ZPAddr.WriteRegisterToMemory(kD2R1ToDB);
	opSTX_ZPAddr.FetchExecPreInc();

	Extensions opSTX_ZPAddr_Y;
	opSTX_ZPAddr_Y.LoadZeroPageAddressFromPCMemoryWithPreInc();
	opSTX_ZPAddr_Y.AddRegisterToZeroPageAddress(kD2R2ToDB);
	opSTX_ZPAddr_Y.WriteRegisterToMemory(kD2R1ToDB);
	opSTX_ZPAddr_Y.FetchExecPreInc();

	Extensions opLDY_ZPAddr;
	opLDY_ZPAddr.LoadZeroPageAddressFromPCMemoryWithPreInc();
	opLDY_ZPAddr.LoadRegisterFromMemory(kD4DBToR2,kD1AddrToAddress,true);
	opLDY_ZPAddr.LoadFlagsDoFlags();
	opLDY_ZPAddr.FetchExecPreInc();

	Extensions opLDY_ZPAddr_X;
	opLDY_ZPAddr_X.LoadZeroPageAddressFromPCMemoryWithPreInc();
	opLDY_ZPAddr_X.AddRegisterToZeroPageAddress(kD2R1ToDB);
	opLDY_ZPAddr_X.LoadRegisterFromMemory(kD4DBToR2,kD1AddrToAddress,true);
	opLDY_ZPAddr_X.LoadFlagsDoFlags();
	opLDY_ZPAddr_X.FetchExecPreInc();

	Extensions opSTY_ZPAddr;
	opSTY_ZPAddr.LoadZeroPageAddressFromPCMemoryWithPreInc();
	opSTY_ZPAddr.WriteRegisterToMemory(kD2R2ToDB);
	opSTY_ZPAddr.FetchExecPreInc();

	Extensions opSTY_ZPAddr_X;
	opSTY_ZPAddr_X.LoadZeroPageAddressFromPCMemoryWithPreInc();
	opSTY_ZPAddr_X.AddRegisterToZeroPageAddress(kD2R1ToDB);
	opSTY_ZPAddr_X.WriteRegisterToMemory(kD2R2ToDB);
	opSTY_ZPAddr_X.FetchExecPreInc();



	Extensions opLDA_ZPAddr_X;
	opLDA_ZPAddr_X.LoadZeroPageAddressFromPCMemoryWithPreInc();
	opLDA_ZPAddr_X.AddRegisterToZeroPageAddress(kD2R1ToDB);
	opLDA_ZPAddr_X.LoadRegisterFromMemory(kD4DBToR0,kD1AddrToAddress,true);
	opLDA_ZPAddr_X.LoadFlagsDoFlags();
	opLDA_ZPAddr_X.FetchExecPreInc();

	Extensions opSTA_ZPAddr_X;
	opSTA_ZPAddr_X.LoadZeroPageAddressFromPCMemoryWithPreInc();
	opSTA_ZPAddr_X.AddRegisterToZeroPageAddress(kD2R1ToDB);
	opSTA_ZPAddr_X.WriteRegisterToMemory(kD2R0ToDB);
	opSTA_ZPAddr_X.FetchExecPreInc();

	Extensions opLDA_IndZPAddr_X;
	opLDA_IndZPAddr_X.LoadZeroPageAddressFromPCMemoryWithPreInc();
	opLDA_IndZPAddr_X.AddRegisterToZeroPageAddress(kD2R1ToDB);
	opLDA_IndZPAddr_X.LoadIndAddrWith6502WrapBug();
	opLDA_IndZPAddr_X.LoadRegisterFromMemory(kD4DBToR0,kD1AddrToAddress,true);
	opLDA_IndZPAddr_X.LoadFlagsDoFlags();
	opLDA_IndZPAddr_X.FetchExecPreInc();

	Extensions opLDA_IndZPAddr_Y;
	opLDA_IndZPAddr_Y.LoadZeroPageAddressFromPCMemoryWithPreInc();
	opLDA_IndZPAddr_Y.LoadIndAddrWith6502WrapBug();
	opLDA_IndZPAddr_Y.AddRegisterToAddress(kD2R2ToDB);
	opLDA_IndZPAddr_Y.LoadRegisterFromMemory(kD4DBToR0,kD1AddrToAddress,true);
	opLDA_IndZPAddr_Y.LoadFlagsDoFlags();
	opLDA_IndZPAddr_Y.FetchExecPreInc();

	Extensions opSTA_IndZPAddr_X;
	opSTA_IndZPAddr_X.LoadZeroPageAddressFromPCMemoryWithPreInc();
	opSTA_IndZPAddr_X.AddRegisterToZeroPageAddress(kD2R1ToDB);
	opSTA_IndZPAddr_X.LoadIndAddrWith6502WrapBug();
	opSTA_IndZPAddr_X.WriteRegisterToMemory(kD2R0ToDB);
	opSTA_IndZPAddr_X.FetchExecPreInc();

	Extensions opSTA_IndZPAddr_Y;
	opSTA_IndZPAddr_Y.LoadZeroPageAddressFromPCMemoryWithPreInc();
	opSTA_IndZPAddr_Y.LoadIndAddrWith6502WrapBug();
	opSTA_IndZPAddr_Y.AddRegisterToAddress(kD2R2ToDB);
	opSTA_IndZPAddr_Y.WriteRegisterToMemory(kD2R0ToDB);
	opSTA_IndZPAddr_Y.FetchExecPreInc();




	Extensions opINC_Addr;
	opINC_Addr.LoadAbsoluteAddressFromPCMemoryWithPreInc();
	// Read from memory into ALU
	opINC_Addr.STToALU();
	opINC_Addr.LoadRegisterFromMemory(0,kD1AddrToAddress,true);
	// Write to memory the ALU result
	opINC_Addr.AddState(State(kD1AddrToAddress),					State(kD2ALUResToDB),		State(kD3ALUOp_Inc));
	opINC_Addr.AddState(State(kD1AddrToAddress),					State(kD2ALUResToDB),		State(kD3ALUOp_Inc | kD3ALUResLoad));
	opINC_Addr.WriteRegisterToMemory(kD2ALUResToDB,			kD3ALUOp_Inc);
	opINC_Addr.LoadSTFromALUFlags();
	opINC_Addr.FetchExecPreInc();

	Extensions opINC_Addr_X;
	opINC_Addr_X.LoadAbsoluteAddressFromPCMemoryWithPreInc();
	opINC_Addr_X.AddRegisterToAddress(kD2R1ToDB);
	// Read from memory into ALU
	opINC_Addr_X.STToALU();
	opINC_Addr_X.LoadRegisterFromMemory(0,kD1AddrToAddress,true);
	// Write to memory the ALU result
	opINC_Addr_X.AddState(State(kD1AddrToAddress),					State(kD2ALUResToDB),		State(kD3ALUOp_Inc));
	opINC_Addr_X.AddState(State(kD1AddrToAddress),					State(kD2ALUResToDB),		State(kD3ALUOp_Inc | kD3ALUResLoad));
	opINC_Addr_X.WriteRegisterToMemory(kD2ALUResToDB,		kD3ALUOp_Inc);
	opINC_Addr_X.LoadSTFromALUFlags();
	opINC_Addr_X.FetchExecPreInc();

	Extensions opINC_ZPAddr;
	opINC_ZPAddr.LoadZeroPageAddressFromPCMemoryWithPreInc();
	// Read from memory into ALU
	opINC_ZPAddr.STToALU();
	opINC_ZPAddr.LoadRegisterFromMemory(0,kD1AddrToAddress,true);
	// Write to memory the ALU result
	opINC_ZPAddr.AddState(State(kD1AddrToAddress),					State(kD2ALUResToDB),		State(kD3ALUOp_Inc));
	opINC_ZPAddr.AddState(State(kD1AddrToAddress),					State(kD2ALUResToDB),		State(kD3ALUOp_Inc | kD3ALUResLoad));
	opINC_ZPAddr.WriteRegisterToMemory(kD2ALUResToDB,		kD3ALUOp_Inc);
	opINC_ZPAddr.LoadSTFromALUFlags();
	opINC_ZPAddr.FetchExecPreInc();

	Extensions opINC_ZPAddr_X;
	opINC_ZPAddr_X.LoadZeroPageAddressFromPCMemoryWithPreInc();
	opINC_ZPAddr_X.AddRegisterToZeroPageAddress(kD2R1ToDB);
	// Read from memory into ALU
	opINC_ZPAddr_X.STToALU();
	opINC_ZPAddr_X.LoadRegisterFromMemory(0,kD1AddrToAddress,true);
	// Write to memory the ALU result
	opINC_ZPAddr_X.AddState(State(),				State(kD2ALUResToDB),		State(kD3ALUOp_Inc));
	opINC_ZPAddr_X.AddState(State(),				State(kD2ALUResToDB),		State(kD3ALUOp_Inc | kD3ALUResLoad));
	opINC_ZPAddr_X.WriteRegisterToMemory(kD2ALUResToDB);
	opINC_ZPAddr_X.LoadSTFromALUFlags();
	opINC_ZPAddr_X.FetchExecPreInc();


	Extensions opDEC_Addr;
	opDEC_Addr.LoadAbsoluteAddressFromPCMemoryWithPreInc();
	// Read from memory into ALU
	opDEC_Addr.STToALU();
	opDEC_Addr.LoadRegisterFromMemory(0,kD1AddrToAddress,true);
	// Write to memory the ALU result
	opDEC_Addr.AddState(State(kD1AddrToAddress),					State(kD2ALUResToDB),		State(kD3ALUOp_Dec));
	opDEC_Addr.AddState(State(kD1AddrToAddress),					State(kD2ALUResToDB),		State(kD3ALUOp_Dec | kD3ALUResLoad));
	opDEC_Addr.WriteRegisterToMemory(kD2ALUResToDB,			kD3ALUOp_Dec);
	opDEC_Addr.LoadSTFromALUFlags();
	opDEC_Addr.FetchExecPreInc();

	Extensions opDEC_Addr_X;
	opDEC_Addr_X.LoadAbsoluteAddressFromPCMemoryWithPreInc();
	opDEC_Addr_X.AddRegisterToAddress(kD2R1ToDB);
	// Read from memory into ALU
	opDEC_Addr_X.STToALU();
	opDEC_Addr_X.LoadRegisterFromMemory(0,kD1AddrToAddress,true);
	// Write to memory the ALU result
	opDEC_Addr_X.AddState(State(kD1AddrToAddress),					State(kD2ALUResToDB),		State(kD3ALUOp_Dec));
	opDEC_Addr_X.AddState(State(kD1AddrToAddress),					State(kD2ALUResToDB),		State(kD3ALUOp_Dec | kD3ALUResLoad));
	opDEC_Addr_X.WriteRegisterToMemory(kD2ALUResToDB,		kD3ALUOp_Dec);
	opDEC_Addr_X.LoadSTFromALUFlags();
	opDEC_Addr_X.FetchExecPreInc();

	Extensions opDEC_ZPAddr;
	opDEC_ZPAddr.LoadZeroPageAddressFromPCMemoryWithPreInc();
	// Read from memory into ALU
	opDEC_ZPAddr.STToALU();
	opDEC_ZPAddr.LoadRegisterFromMemory(0,kD1AddrToAddress,true);
	// Write to memory the ALU result
	opDEC_ZPAddr.AddState(State(kD1AddrToAddress),					State(kD2ALUResToDB),		State(kD3ALUOp_Dec));
	opDEC_ZPAddr.AddState(State(kD1AddrToAddress),					State(kD2ALUResToDB),		State(kD3ALUOp_Dec | kD3ALUResLoad));
	opDEC_ZPAddr.WriteRegisterToMemory(kD2ALUResToDB,		kD3ALUOp_Dec);
	opDEC_ZPAddr.LoadSTFromALUFlags();
	opDEC_ZPAddr.FetchExecPreInc();

	Extensions opDEC_ZPAddr_X;
	opDEC_ZPAddr_X.LoadZeroPageAddressFromPCMemoryWithPreInc();
	opDEC_ZPAddr_X.AddRegisterToZeroPageAddress(kD2R1ToDB);
	// Read from memory into ALU
	opDEC_ZPAddr_X.STToALU();
	opDEC_ZPAddr_X.LoadRegisterFromMemory(0,kD1AddrToAddress,true);
	// Write to memory the ALU result
	opDEC_ZPAddr_X.AddState(State(kD1AddrToAddress),				State(kD2ALUResToDB),		State(kD3ALUOp_Dec));
	opDEC_ZPAddr_X.AddState(State(kD1AddrToAddress),				State(kD2ALUResToDB),		State(kD3ALUOp_Dec | kD3ALUResLoad));
	opDEC_ZPAddr_X.WriteRegisterToMemory(kD2ALUResToDB,		kD3ALUOp_Dec);
	opDEC_ZPAddr_X.LoadSTFromALUFlags();
	opDEC_ZPAddr_X.FetchExecPreInc();


	Extensions opBIT_ZPAddr;
	opBIT_ZPAddr.LoadZeroPageAddressFromPCMemoryWithPreInc();
	opBIT_ZPAddr.CommonBITOpcode();

	Extensions opBIT_Addr;
	opBIT_Addr.LoadAbsoluteAddressFromPCMemoryWithPreInc();
	opBIT_Addr.CommonBITOpcode();

	Extensions opINX;
	opINX.RegisterSimpleALUOp(kD2R1ToDB,kD3ALUOp_Inc,kD4DBToR1);
	opINX.FetchExecPreInc();

	Extensions opINY;
	opINY.RegisterSimpleALUOp(kD2R2ToDB,kD3ALUOp_Inc,kD4DBToR2);
	opINY.FetchExecPreInc();

	Extensions opDEX;
	opDEX.RegisterSimpleALUOp(kD2R1ToDB,kD3ALUOp_Dec,kD4DBToR1);
	opDEX.FetchExecPreInc();

	Extensions opDEY;
	opDEY.RegisterSimpleALUOp(kD2R2ToDB,kD3ALUOp_Dec,kD4DBToR2);
	opDEY.FetchExecPreInc();



	Extensions opASL;
	opASL.RegisterSimpleALUOp(kD2R0ToDB,kD3ALUOp_Lsl,kD4DBToR0);
	opASL.FetchExecPreInc();

	Extensions opASL_ZPAddr;
	opASL_ZPAddr.LoadZeroPageAddressFromPCMemoryWithPreInc();
	opASL_ZPAddr.LoadRegisterFromMemory(kD4DBToR5);
	opASL_ZPAddr.RegisterSimpleALUOp(kD2R5ToDB,kD3ALUOp_Lsl,kD4DBToR5);
	opASL_ZPAddr.WriteRegisterToMemory(kD2R5ToDB);
	opASL_ZPAddr.FetchExecPreInc();

	Extensions opASL_ZPAddr_X;
	opASL_ZPAddr_X.LoadZeroPageAddressFromPCMemoryWithPreInc();
	opASL_ZPAddr_X.AddRegisterToZeroPageAddress(kD2R1ToDB);
	opASL_ZPAddr_X.LoadRegisterFromMemory(kD4DBToR5);
	opASL_ZPAddr_X.RegisterSimpleALUOp(kD2R5ToDB,kD3ALUOp_Lsl,kD4DBToR5);
	opASL_ZPAddr_X.WriteRegisterToMemory(kD2R5ToDB);
	opASL_ZPAddr_X.FetchExecPreInc();

	Extensions opASL_Addr;
	opASL_Addr.LoadAbsoluteAddressFromPCMemoryWithPreInc();
	opASL_Addr.LoadRegisterFromMemory(kD4DBToR5);
	opASL_Addr.RegisterSimpleALUOp(kD2R5ToDB,kD3ALUOp_Lsl,kD4DBToR5);
	opASL_Addr.WriteRegisterToMemory(kD2R5ToDB);
	opASL_Addr.FetchExecPreInc();

	Extensions opASL_Addr_X;
	opASL_Addr_X.LoadAbsoluteAddressFromPCMemoryWithPreInc();
	opASL_Addr_X.AddRegisterToAddress(kD2R1ToDB);
	opASL_Addr_X.LoadRegisterFromMemory(kD4DBToR5);
	opASL_Addr_X.RegisterSimpleALUOp(kD2R5ToDB,kD3ALUOp_Lsl,kD4DBToR5);
	opASL_Addr_X.WriteRegisterToMemory(kD2R5ToDB);
	opASL_Addr_X.FetchExecPreInc();


	Extensions opROL;
	opROL.RegisterSimpleALUOp(kD2R0ToDB,kD3ALUOp_Rol,kD4DBToR0);
	opROL.FetchExecPreInc();

	Extensions opROL_ZPAddr;
	opROL_ZPAddr.LoadZeroPageAddressFromPCMemoryWithPreInc();
	opROL_ZPAddr.LoadRegisterFromMemory(kD4DBToR5);
	opROL_ZPAddr.RegisterSimpleALUOp(kD2R5ToDB,kD3ALUOp_Rol,kD4DBToR5);
	opROL_ZPAddr.WriteRegisterToMemory(kD2R5ToDB);
	opROL_ZPAddr.FetchExecPreInc();

	Extensions opROL_ZPAddr_X;
	opROL_ZPAddr_X.LoadZeroPageAddressFromPCMemoryWithPreInc();
	opROL_ZPAddr_X.AddRegisterToZeroPageAddress(kD2R1ToDB);
	opROL_ZPAddr_X.LoadRegisterFromMemory(kD4DBToR5);
	opROL_ZPAddr_X.RegisterSimpleALUOp(kD2R5ToDB,kD3ALUOp_Rol,kD4DBToR5);
	opROL_ZPAddr_X.WriteRegisterToMemory(kD2R5ToDB);
	opROL_ZPAddr_X.FetchExecPreInc();

	Extensions opROL_Addr;
	opROL_Addr.LoadAbsoluteAddressFromPCMemoryWithPreInc();
	opROL_Addr.LoadRegisterFromMemory(kD4DBToR5);
	opROL_Addr.RegisterSimpleALUOp(kD2R5ToDB,kD3ALUOp_Rol,kD4DBToR5);
	opROL_Addr.WriteRegisterToMemory(kD2R5ToDB);
	opROL_Addr.FetchExecPreInc();

	Extensions opROL_Addr_X;
	opROL_Addr_X.LoadAbsoluteAddressFromPCMemoryWithPreInc();
	opROL_Addr_X.AddRegisterToAddress(kD2R1ToDB);
	opROL_Addr_X.LoadRegisterFromMemory(kD4DBToR5);
	opROL_Addr_X.RegisterSimpleALUOp(kD2R5ToDB,kD3ALUOp_Rol,kD4DBToR5);
	opROL_Addr_X.WriteRegisterToMemory(kD2R5ToDB);
	opROL_Addr_X.FetchExecPreInc();


	Extensions opLSR;
	opLSR.RegisterSimpleALUOp(kD2R0ToDB,kD3ALUOp_Lsr,kD4DBToR0);
	opLSR.FetchExecPreInc();

	Extensions opLSR_ZPAddr;
	opLSR_ZPAddr.LoadZeroPageAddressFromPCMemoryWithPreInc();
	opLSR_ZPAddr.LoadRegisterFromMemory(kD4DBToR5);
	opLSR_ZPAddr.RegisterSimpleALUOp(kD2R5ToDB,kD3ALUOp_Lsr,kD4DBToR5);
	opLSR_ZPAddr.WriteRegisterToMemory(kD2R5ToDB);
	opLSR_ZPAddr.FetchExecPreInc();

	Extensions opLSR_ZPAddr_X;
	opLSR_ZPAddr_X.LoadZeroPageAddressFromPCMemoryWithPreInc();
	opLSR_ZPAddr_X.AddRegisterToZeroPageAddress(kD2R1ToDB);
	opLSR_ZPAddr_X.LoadRegisterFromMemory(kD4DBToR5);
	opLSR_ZPAddr_X.RegisterSimpleALUOp(kD2R5ToDB,kD3ALUOp_Lsr,kD4DBToR5);
	opLSR_ZPAddr_X.WriteRegisterToMemory(kD2R5ToDB);
	opLSR_ZPAddr_X.FetchExecPreInc();

	Extensions opLSR_Addr;
	opLSR_Addr.LoadAbsoluteAddressFromPCMemoryWithPreInc();
	opLSR_Addr.LoadRegisterFromMemory(kD4DBToR5);
	opLSR_Addr.RegisterSimpleALUOp(kD2R5ToDB,kD3ALUOp_Lsr,kD4DBToR5);
	opLSR_Addr.WriteRegisterToMemory(kD2R5ToDB);
	opLSR_Addr.FetchExecPreInc();

	Extensions opLSR_Addr_X;
	opLSR_Addr_X.LoadAbsoluteAddressFromPCMemoryWithPreInc();
	opLSR_Addr_X.AddRegisterToAddress(kD2R1ToDB);
	opLSR_Addr_X.LoadRegisterFromMemory(kD4DBToR5);
	opLSR_Addr_X.RegisterSimpleALUOp(kD2R5ToDB,kD3ALUOp_Lsr,kD4DBToR5);
	opLSR_Addr_X.WriteRegisterToMemory(kD2R5ToDB);
	opLSR_Addr_X.FetchExecPreInc();


	Extensions opROR;
	opROR.RegisterSimpleALUOp(kD2R0ToDB,kD3ALUOp_Ror,kD4DBToR0);
	opROR.FetchExecPreInc();

	Extensions opROR_ZPAddr;
	opROR_ZPAddr.LoadZeroPageAddressFromPCMemoryWithPreInc();
	opROR_ZPAddr.LoadRegisterFromMemory(kD4DBToR5);
	opROR_ZPAddr.RegisterSimpleALUOp(kD2R5ToDB,kD3ALUOp_Ror,kD4DBToR5);
	opROR_ZPAddr.WriteRegisterToMemory(kD2R5ToDB);
	opROR_ZPAddr.FetchExecPreInc();

	Extensions opROR_ZPAddr_X;
	opROR_ZPAddr_X.LoadZeroPageAddressFromPCMemoryWithPreInc();
	opROR_ZPAddr_X.AddRegisterToZeroPageAddress(kD2R1ToDB);
	opROR_ZPAddr_X.LoadRegisterFromMemory(kD4DBToR5);
	opROR_ZPAddr_X.RegisterSimpleALUOp(kD2R5ToDB,kD3ALUOp_Ror,kD4DBToR5);
	opROR_ZPAddr_X.WriteRegisterToMemory(kD2R5ToDB);
	opROR_ZPAddr_X.FetchExecPreInc();

	Extensions opROR_Addr;
	opROR_Addr.LoadAbsoluteAddressFromPCMemoryWithPreInc();
	opROR_Addr.LoadRegisterFromMemory(kD4DBToR5);
	opROR_Addr.RegisterSimpleALUOp(kD2R5ToDB,kD3ALUOp_Ror,kD4DBToR5);
	opROR_Addr.WriteRegisterToMemory(kD2R5ToDB);
	opROR_Addr.FetchExecPreInc();

	Extensions opROR_Addr_X;
	opROR_Addr_X.LoadAbsoluteAddressFromPCMemoryWithPreInc();
	opROR_Addr_X.AddRegisterToAddress(kD2R1ToDB);
	opROR_Addr_X.LoadRegisterFromMemory(kD4DBToR5);
	opROR_Addr_X.RegisterSimpleALUOp(kD2R5ToDB,kD3ALUOp_Ror,kD4DBToR5);
	opROR_Addr_X.WriteRegisterToMemory(kD2R5ToDB);
	opROR_Addr_X.FetchExecPreInc();




	Extensions opCmp_Immediate;
	opCmp_Immediate.CompareRegisterWithImmediate(kD2R0ToDB);
	opCmp_Immediate.FetchExecPreInc();

	Extensions opCmp_ZPAddr;
	opCmp_ZPAddr.CompareRegisterWithZeroPageAddrPlusRegister(kD2R0ToDB);
	opCmp_ZPAddr.FetchExecPreInc();

	Extensions opCmp_ZPAddr_X;
	opCmp_ZPAddr_X.CompareRegisterWithZeroPageAddrPlusRegister(kD2R0ToDB,kD2R1ToDB);
	opCmp_ZPAddr_X.FetchExecPreInc();

	Extensions opCmp_Addr;
	opCmp_Addr.CompareRegisterWithAddrPlusRegister(kD2R0ToDB);
	opCmp_Addr.FetchExecPreInc();

	Extensions opCmp_Addr_X;
	opCmp_Addr_X.CompareRegisterWithAddrPlusRegister(kD2R0ToDB,kD2R1ToDB);
	opCmp_Addr_X.FetchExecPreInc();

	Extensions opCmp_Addr_Y;
	opCmp_Addr_Y.CompareRegisterWithAddrPlusRegister(kD2R0ToDB,kD2R2ToDB);
	opCmp_Addr_Y.FetchExecPreInc();

	Extensions opCmp_IndZPAddr_Y;
	opCmp_IndZPAddr_Y.LoadZeroPageAddressFromPCMemoryWithPreInc();
	opCmp_IndZPAddr_Y.LoadIndAddrWith6502WrapBug();
	opCmp_IndZPAddr_Y.AddRegisterToAddress(kD2R2ToDB);
	opCmp_IndZPAddr_Y.LoadRegisterFromMemory(0,kD1AddrToAddress,true);
	opCmp_IndZPAddr_Y.CompareCommon(kD2R0ToDB);
	opCmp_IndZPAddr_Y.FetchExecPreInc();

	Extensions opCmp_IndZPAddr_X;
	opCmp_IndZPAddr_X.LoadZeroPageAddressFromPCMemoryWithPreInc();
	opCmp_IndZPAddr_X.AddRegisterToZeroPageAddress(kD2R1ToDB);
	opCmp_IndZPAddr_X.LoadIndAddrWith6502WrapBug();
	opCmp_IndZPAddr_X.LoadRegisterFromMemory(0,kD1AddrToAddress,true);
	opCmp_IndZPAddr_X.CompareCommon(kD2R0ToDB);
	opCmp_IndZPAddr_X.FetchExecPreInc();




	Extensions opCpx_Immediate;
	opCpx_Immediate.CompareRegisterWithImmediate(kD2R1ToDB);
	opCpx_Immediate.FetchExecPreInc();

	Extensions opCpx_ZPAddr;
	opCpx_ZPAddr.CompareRegisterWithZeroPageAddrPlusRegister(kD2R1ToDB);
	opCpx_ZPAddr.FetchExecPreInc();

	Extensions opCpx_Addr;
	opCpx_Addr.CompareRegisterWithAddrPlusRegister(kD2R1ToDB);
	opCpx_Addr.FetchExecPreInc();


	Extensions opCpy_Immediate;
	opCpy_Immediate.CompareRegisterWithImmediate(kD2R2ToDB);
	opCpy_Immediate.FetchExecPreInc();

	Extensions opCpy_ZPAddr;
	opCpy_ZPAddr.CompareRegisterWithZeroPageAddrPlusRegister(kD2R2ToDB);
	opCpy_ZPAddr.FetchExecPreInc();

	Extensions opCpy_Addr;
	opCpy_Addr.CompareRegisterWithAddrPlusRegister(kD2R2ToDB);
	opCpy_Addr.FetchExecPreInc();

	Extensions opExtractZFlag;
	// Read ST into ALU
	opExtractZFlag.AddState(State(),			State(kD2STToDB));
	opExtractZFlag.AddState(State(),			State(kD2STToDB),		State(kD3ALUOp_Lsr | kD3ALUIn1Load | kD3ALUIn2Load));
	// Shift Z flag into carry by doing two LSR
	// First LSR
	opExtractZFlag.AddState(State(),			State(kD2ALUResToDB),		State(kD3ALUOp_Lsr | kD3ALUResLoad));
	opExtractZFlag.AddState(State(),			State(kD2ALUResToDB),		State(kD3ALUOp_Lsr | kD3ALUIn1Load | kD3ALUIn2Load));
	// Second LSR copy ALU carry to kD2DoBranchLoad
	opExtractZFlag.AddState(State(),			State(),					State(kD3ALUOp_Lsr));
	opExtractZFlag.AddState(State(),			State(kD2DoBranchLoad),		State(kD3ALUOp_Lsr));
	opExtractZFlag.AddState();	// Blank state to allow sync
	// At this point the instruction will split due to the kD2DoBranchLoad flag being set or clear

	Extensions opExtractCFlag;
	// Read ST into ALU
	opExtractCFlag.AddState(State(),			State(kD2STToDB));
	opExtractCFlag.AddState(State(),			State(kD2STToDB),		State(kD3ALUIn1Load | kD3ALUIn2Load));
	// Shift C flag into carry by doing one LSR
	// LSR and copy ALU carry to kD2DoBranchLoad
	opExtractCFlag.AddState(State(),			State(),					State(kD3ALUOp_Lsr));
	opExtractCFlag.AddState(State(),			State(kD2DoBranchLoad),		State(kD3ALUOp_Lsr));
	opExtractCFlag.AddState();	// Blank state to allow sync
	// At this point the instruction will split due to the kD2DoBranchLoad flag being set or clear

	Extensions opExtractNFlag;
	// Read ST into ALU
	opExtractNFlag.AddState(State(),			State(kD2STToDB));
	opExtractNFlag.AddState(State(),			State(kD2STToDB),		State(kD3ALUIn1Load | kD3ALUIn2Load));
	// Shift N flag into carry by doing one LSL
	// LSL and copy ALU carry to kD2DoBranchLoad
	opExtractNFlag.AddState(State(),			State(),					State(kD3ALUOp_Lsl));
	opExtractNFlag.AddState(State(),			State(kD2DoBranchLoad),		State(kD3ALUOp_Lsl));
	opExtractNFlag.AddState();	// Blank state to allow sync
	// At this point the instruction will split due to the kD2DoBranchLoad flag being set or clear

	Extensions opExtractVFlag;
	// Read ST into ALU
	opExtractVFlag.AddState(State(),			State(kD2STToDB));
	opExtractVFlag.AddState(State(),			State(kD2STToDB),		State(kD3ALUOp_Lsl | kD3ALUIn1Load | kD3ALUIn2Load));
	// Shift Z flag into carry by doing two LSL
	// First LSR
	opExtractVFlag.AddState(State(),			State(kD2ALUResToDB),		State(kD3ALUOp_Lsl | kD3ALUResLoad));
	opExtractVFlag.AddState(State(),			State(kD2ALUResToDB));
	opExtractVFlag.AddState(State(),			State(kD2ALUResToDB),		State(kD3ALUIn1Load | kD3ALUIn2Load));
	// Second LSR copy ALU carry to kD2DoBranchLoad
	opExtractVFlag.AddState(State(),			State(),					State(kD3ALUOp_Lsl));
	opExtractVFlag.AddState(State(),			State(kD2DoBranchLoad),		State(kD3ALUOp_Lsl));
	opExtractVFlag.AddState();	// Blank state to allow sync
	// At this point the instruction will split due to the kD2DoBranchLoad flag being set or clear

	// This is what happens with no Z flag
	Extensions opBNE0;
	opBNE0.Append(opExtractZFlag);
	opBNE0.TakeBranch();
	// This is what happens with Z flag
	Extensions opBNE1;
	opBNE1.Append(opExtractZFlag);
	opBNE1.SkipBranch();

	// This is what happens with no Z flag
	Extensions opBEQ0;
	opBEQ0.Append(opExtractZFlag);
	opBEQ0.SkipBranch();
	// This is what happens with Z flag
	Extensions opBEQ1;
	opBEQ1.Append(opExtractZFlag);
	opBEQ1.TakeBranch();


	// This is what happens with no C flag
	Extensions opBCC0;
	opBCC0.Append(opExtractCFlag);
	opBCC0.TakeBranch();
	// This is what happens with C flag
	Extensions opBCC1;
	opBCC1.Append(opExtractCFlag);
	opBCC1.SkipBranch();

	// This is what happens with no C flag
	Extensions opBCS0;
	opBCS0.Append(opExtractCFlag);
	opBCS0.SkipBranch();
	// This is what happens with C flag
	Extensions opBCS1;
	opBCS1.Append(opExtractCFlag);
	opBCS1.TakeBranch();


	// This is what happens with no C flag
	Extensions opBPL0;
	opBPL0.Append(opExtractNFlag);
	opBPL0.TakeBranch();
	// This is what happens with C flag
	Extensions opBPL1;
	opBPL1.Append(opExtractNFlag);
	opBPL1.SkipBranch();

	// This is what happens with no C flag
	Extensions opBMI0;
	opBMI0.Append(opExtractNFlag);
	opBMI0.SkipBranch();
	// This is what happens with C flag
	Extensions opBMI1;
	opBMI1.Append(opExtractNFlag);
	opBMI1.TakeBranch();


	// This is what happens with no V flag
	Extensions opBVC0;
	opBVC0.Append(opExtractVFlag);
	opBVC0.TakeBranch();
	// This is what happens with V flag
	Extensions opBVC1;
	opBVC1.Append(opExtractVFlag);
	opBVC1.SkipBranch();

	// This is what happens with no V flag
	Extensions opBVS0;
	opBVS0.Append(opExtractVFlag);
	opBVS0.SkipBranch();
	// This is what happens with V flag
	Extensions opBVS1;
	opBVS1.Append(opExtractVFlag);
	opBVS1.TakeBranch();

	Extensions opAnd_Immediate;
	opAnd_Immediate.LoadImmediatePrimeALUPreInc(0);
	opAnd_Immediate.RegisterALUOp(kD2R0ToDB,kD3ALUOp_And,kD4DBToR0);
	opAnd_Immediate.FetchExecPreInc();

	Extensions opAnd_ZPAddr;
	opAnd_ZPAddr.LoadZeroPageAddressFromPCMemoryWithPreInc();
	opAnd_ZPAddr.LoadRegisterFromMemory(0,kD1AddrToAddress,true);
	opAnd_ZPAddr.RegisterALUOp(kD2R0ToDB,kD3ALUOp_And,kD4DBToR0);
	opAnd_ZPAddr.FetchExecPreInc();

	Extensions opAnd_ZPAddr_X;
	opAnd_ZPAddr_X.LoadZeroPageAddressFromPCMemoryWithPreInc();
	opAnd_ZPAddr_X.AddRegisterToZeroPageAddress(kD2R1ToDB);
	opAnd_ZPAddr_X.LoadRegisterFromMemory(0,kD1AddrToAddress,true);
	opAnd_ZPAddr_X.RegisterALUOp(kD2R0ToDB,kD3ALUOp_And,kD4DBToR0);
	opAnd_ZPAddr_X.FetchExecPreInc();

	Extensions opAnd_Addr;
	opAnd_Addr.LoadAbsoluteAddressFromPCMemoryWithPreInc();
	opAnd_Addr.LoadRegisterFromMemory(0,kD1AddrToAddress,true);
	opAnd_Addr.RegisterALUOp(kD2R0ToDB,kD3ALUOp_And,kD4DBToR0);
	opAnd_Addr.FetchExecPreInc();

	Extensions opAnd_Addr_X;
	opAnd_Addr_X.LoadAbsoluteAddressFromPCMemoryWithPreInc();
	opAnd_Addr_X.AddRegisterToAddress(kD2R1ToDB);
	opAnd_Addr_X.LoadRegisterFromMemory(0,kD1AddrToAddress,true);
	opAnd_Addr_X.RegisterALUOp(kD2R0ToDB,kD3ALUOp_And,kD4DBToR0);
	opAnd_Addr_X.FetchExecPreInc();

	Extensions opAnd_Addr_Y;
	opAnd_Addr_Y.LoadAbsoluteAddressFromPCMemoryWithPreInc();
	opAnd_Addr_Y.AddRegisterToAddress(kD2R2ToDB);
	opAnd_Addr_Y.LoadRegisterFromMemory(0,kD1AddrToAddress,true);
	opAnd_Addr_Y.RegisterALUOp(kD2R0ToDB,kD3ALUOp_And,kD4DBToR0);
	opAnd_Addr_Y.FetchExecPreInc();

	Extensions opAnd_IndZPAddr_X;
	opAnd_IndZPAddr_X.LoadZeroPageAddressFromPCMemoryWithPreInc();
	opAnd_IndZPAddr_X.AddRegisterToZeroPageAddress(kD2R1ToDB);
	opAnd_IndZPAddr_X.LoadIndAddrWith6502WrapBug();
	opAnd_IndZPAddr_X.LoadRegisterFromMemory(0,kD1AddrToAddress,true);
	opAnd_IndZPAddr_X.RegisterALUOp(kD2R0ToDB,kD3ALUOp_And,kD4DBToR0);
	opAnd_IndZPAddr_X.FetchExecPreInc();

	Extensions opAnd_IndZPAddr_Y;
	opAnd_IndZPAddr_Y.LoadZeroPageAddressFromPCMemoryWithPreInc();
	opAnd_IndZPAddr_Y.LoadIndAddrWith6502WrapBug();
	opAnd_IndZPAddr_Y.AddRegisterToAddress(kD2R2ToDB);
	opAnd_IndZPAddr_Y.LoadRegisterFromMemory(0,kD1AddrToAddress,true);
	opAnd_IndZPAddr_Y.RegisterALUOp(kD2R0ToDB,kD3ALUOp_And,kD4DBToR0);
	opAnd_IndZPAddr_Y.FetchExecPreInc();



	Extensions opOra_Immediate;
	opOra_Immediate.LoadImmediatePrimeALUPreInc(0);
	opOra_Immediate.RegisterALUOp(kD2R0ToDB,kD3ALUOp_Or,kD4DBToR0);
	opOra_Immediate.FetchExecPreInc();

	Extensions opOra_ZPAddr;
	opOra_ZPAddr.LoadZeroPageAddressFromPCMemoryWithPreInc();
	opOra_ZPAddr.LoadRegisterFromMemory(0,kD1AddrToAddress,true);
	opOra_ZPAddr.RegisterALUOp(kD2R0ToDB,kD3ALUOp_Or,kD4DBToR0);
	opOra_ZPAddr.FetchExecPreInc();

	Extensions opOra_ZPAddr_X;
	opOra_ZPAddr_X.LoadZeroPageAddressFromPCMemoryWithPreInc();
	opOra_ZPAddr_X.AddRegisterToZeroPageAddress(kD2R1ToDB);
	opOra_ZPAddr_X.LoadRegisterFromMemory(0,kD1AddrToAddress,true);
	opOra_ZPAddr_X.RegisterALUOp(kD2R0ToDB,kD3ALUOp_Or,kD4DBToR0);
	opOra_ZPAddr_X.FetchExecPreInc();

	Extensions opOra_Addr;
	opOra_Addr.LoadAbsoluteAddressFromPCMemoryWithPreInc();
	opOra_Addr.LoadRegisterFromMemory(0,kD1AddrToAddress,true);
	opOra_Addr.RegisterALUOp(kD2R0ToDB,kD3ALUOp_Or,kD4DBToR0);
	opOra_Addr.FetchExecPreInc();

	Extensions opOra_Addr_X;
	opOra_Addr_X.LoadAbsoluteAddressFromPCMemoryWithPreInc();
	opOra_Addr_X.AddRegisterToAddress(kD2R1ToDB);
	opOra_Addr_X.LoadRegisterFromMemory(0,kD1AddrToAddress,true);
	opOra_Addr_X.RegisterALUOp(kD2R0ToDB,kD3ALUOp_Or,kD4DBToR0);
	opOra_Addr_X.FetchExecPreInc();

	Extensions opOra_Addr_Y;
	opOra_Addr_Y.LoadAbsoluteAddressFromPCMemoryWithPreInc();
	opOra_Addr_Y.AddRegisterToAddress(kD2R2ToDB);
	opOra_Addr_Y.LoadRegisterFromMemory(0,kD1AddrToAddress,true);
	opOra_Addr_Y.RegisterALUOp(kD2R0ToDB,kD3ALUOp_Or,kD4DBToR0);
	opOra_Addr_Y.FetchExecPreInc();

	Extensions opOra_IndZPAddr_X;
	opOra_IndZPAddr_X.LoadZeroPageAddressFromPCMemoryWithPreInc();
	opOra_IndZPAddr_X.AddRegisterToZeroPageAddress(kD2R1ToDB);
	opOra_IndZPAddr_X.LoadIndAddrWith6502WrapBug();
	opOra_IndZPAddr_X.LoadRegisterFromMemory(0,kD1AddrToAddress,true);
	opOra_IndZPAddr_X.RegisterALUOp(kD2R0ToDB,kD3ALUOp_Or,kD4DBToR0);
	opOra_IndZPAddr_X.FetchExecPreInc();

	Extensions opOra_IndZPAddr_Y;
	opOra_IndZPAddr_Y.LoadZeroPageAddressFromPCMemoryWithPreInc();
	opOra_IndZPAddr_Y.LoadIndAddrWith6502WrapBug();
	opOra_IndZPAddr_Y.AddRegisterToAddress(kD2R2ToDB);
	opOra_IndZPAddr_Y.LoadRegisterFromMemory(0,kD1AddrToAddress,true);
	opOra_IndZPAddr_Y.RegisterALUOp(kD2R0ToDB,kD3ALUOp_Or,kD4DBToR0);
	opOra_IndZPAddr_Y.FetchExecPreInc();




	Extensions opAdc_Immediate;
	opAdc_Immediate.LoadImmediatePrimeALUPreInc(0);
	opAdc_Immediate.RegisterALUOp(kD2R0ToDB,kD3ALUOp_Add,kD4DBToR0);
	opAdc_Immediate.FetchExecPreInc();

	Extensions opAdc_ZPAddr;
	opAdc_ZPAddr.LoadZeroPageAddressFromPCMemoryWithPreInc();
	opAdc_ZPAddr.LoadRegisterFromMemory(0,kD1AddrToAddress,true);
	opAdc_ZPAddr.RegisterALUOp(kD2R0ToDB,kD3ALUOp_Add,kD4DBToR0);
	opAdc_ZPAddr.FetchExecPreInc();

	Extensions opAdc_ZPAddr_X;
	opAdc_ZPAddr_X.LoadZeroPageAddressFromPCMemoryWithPreInc();
	opAdc_ZPAddr_X.AddRegisterToZeroPageAddress(kD2R1ToDB);
	opAdc_ZPAddr_X.LoadRegisterFromMemory(0,kD1AddrToAddress,true);
	opAdc_ZPAddr_X.RegisterALUOp(kD2R0ToDB,kD3ALUOp_Add,kD4DBToR0);
	opAdc_ZPAddr_X.FetchExecPreInc();

	Extensions opAdc_Addr;
	opAdc_Addr.LoadAbsoluteAddressFromPCMemoryWithPreInc();
	opAdc_Addr.LoadRegisterFromMemory(0,kD1AddrToAddress,true);
	opAdc_Addr.RegisterALUOp(kD2R0ToDB,kD3ALUOp_Add,kD4DBToR0);
	opAdc_Addr.FetchExecPreInc();

	Extensions opAdc_Addr_X;
	opAdc_Addr_X.LoadAbsoluteAddressFromPCMemoryWithPreInc();
	opAdc_Addr_X.AddRegisterToAddress(kD2R1ToDB);
	opAdc_Addr_X.LoadRegisterFromMemory(0,kD1AddrToAddress,true);
	opAdc_Addr_X.RegisterALUOp(kD2R0ToDB,kD3ALUOp_Add,kD4DBToR0);
	opAdc_Addr_X.FetchExecPreInc();

	Extensions opAdc_Addr_Y;
	opAdc_Addr_Y.LoadAbsoluteAddressFromPCMemoryWithPreInc();
	opAdc_Addr_Y.AddRegisterToAddress(kD2R2ToDB);
	opAdc_Addr_Y.LoadRegisterFromMemory(0,kD1AddrToAddress,true);
	opAdc_Addr_Y.RegisterALUOp(kD2R0ToDB,kD3ALUOp_Add,kD4DBToR0);
	opAdc_Addr_Y.FetchExecPreInc();

	Extensions opAdc_IndZPAddr_X;
	opAdc_IndZPAddr_X.LoadZeroPageAddressFromPCMemoryWithPreInc();
	opAdc_IndZPAddr_X.AddRegisterToZeroPageAddress(kD2R1ToDB);
	opAdc_IndZPAddr_X.LoadIndAddrWith6502WrapBug();
	opAdc_IndZPAddr_X.LoadRegisterFromMemory(0,kD1AddrToAddress,true);
	opAdc_IndZPAddr_X.RegisterALUOp(kD2R0ToDB,kD3ALUOp_Add,kD4DBToR0);
	opAdc_IndZPAddr_X.FetchExecPreInc();

	Extensions opAdc_IndZPAddr_Y;
	opAdc_IndZPAddr_Y.LoadZeroPageAddressFromPCMemoryWithPreInc();
	opAdc_IndZPAddr_Y.LoadIndAddrWith6502WrapBug();
	opAdc_IndZPAddr_Y.AddRegisterToAddress(kD2R2ToDB);
	opAdc_IndZPAddr_Y.LoadRegisterFromMemory(0,kD1AddrToAddress,true);
	opAdc_IndZPAddr_Y.RegisterALUOp(kD2R0ToDB,kD3ALUOp_Add,kD4DBToR0);
	opAdc_IndZPAddr_Y.FetchExecPreInc();





	Extensions opSbc_Immediate;
	opSbc_Immediate.LoadImmediatePrimeALUPreInc(0);
	opSbc_Immediate.RegisterALUOp(kD2R0ToDB,kD3ALUOp_Sub,kD4DBToR0);
	opSbc_Immediate.FetchExecPreInc();

	Extensions opSbc_ZPAddr;
	opSbc_ZPAddr.LoadZeroPageAddressFromPCMemoryWithPreInc();
	opSbc_ZPAddr.LoadRegisterFromMemory(0,kD1AddrToAddress,true);
	opSbc_ZPAddr.RegisterALUOp(kD2R0ToDB,kD3ALUOp_Sub,kD4DBToR0);
	opSbc_ZPAddr.FetchExecPreInc();

	Extensions opSbc_ZPAddr_X;
	opSbc_ZPAddr_X.LoadZeroPageAddressFromPCMemoryWithPreInc();
	opSbc_ZPAddr_X.AddRegisterToZeroPageAddress(kD2R1ToDB);
	opSbc_ZPAddr_X.LoadRegisterFromMemory(0,kD1AddrToAddress,true);
	opSbc_ZPAddr_X.RegisterALUOp(kD2R0ToDB,kD3ALUOp_Sub,kD4DBToR0);
	opSbc_ZPAddr_X.FetchExecPreInc();

	Extensions opSbc_Addr;
	opSbc_Addr.LoadAbsoluteAddressFromPCMemoryWithPreInc();
	opSbc_Addr.LoadRegisterFromMemory(0,kD1AddrToAddress,true);
	opSbc_Addr.RegisterALUOp(kD2R0ToDB,kD3ALUOp_Sub,kD4DBToR0);
	opSbc_Addr.FetchExecPreInc();

	Extensions opSbc_Addr_X;
	opSbc_Addr_X.LoadAbsoluteAddressFromPCMemoryWithPreInc();
	opSbc_Addr_X.AddRegisterToAddress(kD2R1ToDB);
	opSbc_Addr_X.LoadRegisterFromMemory(0,kD1AddrToAddress,true);
	opSbc_Addr_X.RegisterALUOp(kD2R0ToDB,kD3ALUOp_Sub,kD4DBToR0);
	opSbc_Addr_X.FetchExecPreInc();

	Extensions opSbc_Addr_Y;
	opSbc_Addr_Y.LoadAbsoluteAddressFromPCMemoryWithPreInc();
	opSbc_Addr_Y.AddRegisterToAddress(kD2R2ToDB);
	opSbc_Addr_Y.LoadRegisterFromMemory(0,kD1AddrToAddress,true);
	opSbc_Addr_Y.RegisterALUOp(kD2R0ToDB,kD3ALUOp_Sub,kD4DBToR0);
	opSbc_Addr_Y.FetchExecPreInc();

	Extensions opSbc_IndZPAddr_X;
	opSbc_IndZPAddr_X.LoadZeroPageAddressFromPCMemoryWithPreInc();
	opSbc_IndZPAddr_X.AddRegisterToZeroPageAddress(kD2R1ToDB);
	opSbc_IndZPAddr_X.LoadIndAddrWith6502WrapBug();
	opSbc_IndZPAddr_X.LoadRegisterFromMemory(0,kD1AddrToAddress,true);
	opSbc_IndZPAddr_X.RegisterALUOp(kD2R0ToDB,kD3ALUOp_Sub,kD4DBToR0);
	opSbc_IndZPAddr_X.FetchExecPreInc();

	Extensions opSbc_IndZPAddr_Y;
	opSbc_IndZPAddr_Y.LoadZeroPageAddressFromPCMemoryWithPreInc();
	opSbc_IndZPAddr_Y.LoadIndAddrWith6502WrapBug();
	opSbc_IndZPAddr_Y.AddRegisterToAddress(kD2R2ToDB);
	opSbc_IndZPAddr_Y.LoadRegisterFromMemory(0,kD1AddrToAddress,true);
	opSbc_IndZPAddr_Y.RegisterALUOp(kD2R0ToDB,kD3ALUOp_Sub,kD4DBToR0);
	opSbc_IndZPAddr_Y.FetchExecPreInc();




	Extensions opEor_Immediate;
	opEor_Immediate.LoadImmediatePrimeALUPreInc(0);
	opEor_Immediate.RegisterALUOp(kD2R0ToDB,kD3ALUOp_Xor,kD4DBToR0);
	opEor_Immediate.FetchExecPreInc();

	Extensions opEor_ZPAddr;
	opEor_ZPAddr.LoadZeroPageAddressFromPCMemoryWithPreInc();
	opEor_ZPAddr.LoadRegisterFromMemory(0,kD1AddrToAddress,true);
	opEor_ZPAddr.RegisterALUOp(kD2R0ToDB,kD3ALUOp_Xor,kD4DBToR0);
	opEor_ZPAddr.FetchExecPreInc();

	Extensions opEor_ZPAddr_X;
	opEor_ZPAddr_X.LoadZeroPageAddressFromPCMemoryWithPreInc();
	opEor_ZPAddr_X.AddRegisterToZeroPageAddress(kD2R1ToDB);
	opEor_ZPAddr_X.LoadRegisterFromMemory(0,kD1AddrToAddress,true);
	opEor_ZPAddr_X.RegisterALUOp(kD2R0ToDB,kD3ALUOp_Xor,kD4DBToR0);
	opEor_ZPAddr_X.FetchExecPreInc();

	Extensions opEor_Addr;
	opEor_Addr.LoadAbsoluteAddressFromPCMemoryWithPreInc();
	opEor_Addr.LoadRegisterFromMemory(0,kD1AddrToAddress,true);
	opEor_Addr.RegisterALUOp(kD2R0ToDB,kD3ALUOp_Xor,kD4DBToR0);
	opEor_Addr.FetchExecPreInc();

	Extensions opEor_Addr_X;
	opEor_Addr_X.LoadAbsoluteAddressFromPCMemoryWithPreInc();
	opEor_Addr_X.AddRegisterToAddress(kD2R1ToDB);
	opEor_Addr_X.LoadRegisterFromMemory(0,kD1AddrToAddress,true);
	opEor_Addr_X.RegisterALUOp(kD2R0ToDB,kD3ALUOp_Xor,kD4DBToR0);
	opEor_Addr_X.FetchExecPreInc();

	Extensions opEor_Addr_Y;
	opEor_Addr_Y.LoadAbsoluteAddressFromPCMemoryWithPreInc();
	opEor_Addr_Y.AddRegisterToAddress(kD2R2ToDB);
	opEor_Addr_Y.LoadRegisterFromMemory(0,kD1AddrToAddress,true);
	opEor_Addr_Y.RegisterALUOp(kD2R0ToDB,kD3ALUOp_Xor,kD4DBToR0);
	opEor_Addr_Y.FetchExecPreInc();

	Extensions opEor_IndZPAddr_X;
	opEor_IndZPAddr_X.LoadZeroPageAddressFromPCMemoryWithPreInc();
	opEor_IndZPAddr_X.AddRegisterToZeroPageAddress(kD2R1ToDB);
	opEor_IndZPAddr_X.LoadIndAddrWith6502WrapBug();
	opEor_IndZPAddr_X.LoadRegisterFromMemory(0,kD1AddrToAddress,true);
	opEor_IndZPAddr_X.RegisterALUOp(kD2R0ToDB,kD3ALUOp_Xor,kD4DBToR0);
	opEor_IndZPAddr_X.FetchExecPreInc();

	Extensions opEor_IndZPAddr_Y;
	opEor_IndZPAddr_Y.LoadZeroPageAddressFromPCMemoryWithPreInc();
	opEor_IndZPAddr_Y.LoadIndAddrWith6502WrapBug();
	opEor_IndZPAddr_Y.AddRegisterToAddress(kD2R2ToDB);
	opEor_IndZPAddr_Y.LoadRegisterFromMemory(0,kD1AddrToAddress,true);
	opEor_IndZPAddr_Y.RegisterALUOp(kD2R0ToDB,kD3ALUOp_Xor,kD4DBToR0);
	opEor_IndZPAddr_Y.FetchExecPreInc();




	Extensions opSec;
	opSec.AddState(State(),		State(kD2STToDB));
	opSec.AddState(State(),		State(kD2STToDB),	State(kD3ALUIn1Load | kD3ALUIn2Load));
	opSec.AddState(State(),		State(kD2ZeroToDB));
	opSec.AddState(State(),		State(kD2ZeroToDB),	State(kD3ALUOp_Sec | kD3ALUIn3Load));
	opSec.AddState(State(),		State(kD2ALUResToDB),		State(kD3ALUOp_Sec | kD3ALUResLoad)	,	State(kD4DBToST));
	opSec.FetchExecPreInc();

	Extensions opClc;
	opClc.AddState(State(),		State(kD2STToDB));
	opClc.AddState(State(),		State(kD2STToDB),	State(kD3ALUIn1Load | kD3ALUIn2Load));
	opClc.AddState(State(),		State(kD2ZeroToDB));
	opClc.AddState(State(),		State(kD2ZeroToDB),	State(kD3ALUOp_Clc | kD3ALUIn3Load));
	opClc.AddState(State(),		State(kD2ALUResToDB),		State(kD3ALUOp_Clc | kD3ALUResLoad)	,	State(kD4DBToST));
	opClc.FetchExecPreInc();


	Extensions opClv;
	opClv.AddState(State(),		State(kD2STToDB));
	opClv.AddState(State(),		State(kD2STToDB),	State(kD3ALUIn1Load | kD3ALUIn2Load));
	opClv.AddState(State(),		State(kD2ZeroToDB));
	opClv.AddState(State(),		State(kD2ZeroToDB),	State(kD3ALUOp_Clv | kD3ALUIn3Load));
	opClv.AddState(State(),		State(kD2ALUResToDB),		State(kD3ALUOp_Clv | kD3ALUResLoad)	,	State(kD4DBToST));
	opClv.FetchExecPreInc();


	Extensions opCli;
	// Get 1 (ALU inc #0) to temp R5
	opCli.AddState(State(),				State(kD2ZeroToDB),					State());
	opCli.AddState(State(),				State(kD2ZeroToDB),					State(kD3ALUOp_Inc | kD3ALUIn1Load | kD3ALUIn2Load | kD3ALUIn3Load));
	opCli.AddState(State(),				State(kD2ALUResToDB),				State(kD3ALUOp_Inc | kD3ALUResLoad)	,	State(kD4DBToR5));

	// Calculate 1 << 2 using the ALU into temp R5
	opCli.AddState(State(),				State(kD2R5ToDB),					State());
	opCli.AddState(State(),				State(kD2R5ToDB),					State(kD3ALUOp_Lsl | kD3ALUIn1Load | kD3ALUIn2Load));
	opCli.AddState(State(),				State(kD2ALUResToDB),				State(kD3ALUOp_Lsl | kD3ALUResLoad)	,	State(kD4DBToR5));

	opCli.AddState(State(),				State(kD2R5ToDB),					State());
	opCli.AddState(State(),				State(kD2R5ToDB),					State(kD3ALUOp_Lsl | kD3ALUIn1Load | kD3ALUIn2Load));
	opCli.AddState(State(),				State(kD2ALUResToDB),				State(kD3ALUOp_Lsl | kD3ALUResLoad)	,	State(kD4DBToR5));

	// Now 4 XOR with 0xff
	opCli.AddState(State(),				State(kD2R5ToDB),					State());
	opCli.AddState(State(),				State(kD2R5ToDB),					State(kD3ALUIn1Load));
	opCli.AddState(State(),				State(kD2FFToDB),					State());
	opCli.AddState(State(),				State(kD2FFToDB),					State(kD3ALUOp_Xor | kD3ALUIn2Load));
	opCli.AddState(State(),				State(kD2ALUResToDB),				State(kD3ALUOp_Xor | kD3ALUResLoad)	,	State(kD4DBToR5));

	// Now ST AND 0xfb back into ST
	opCli.AddState(State(),				State(kD2STToDB),					State());
	opCli.AddState(State(),				State(kD2STToDB),					State(kD3ALUIn1Load));
	opCli.AddState(State(),				State(kD2R5ToDB),					State());
	opCli.AddState(State(),				State(kD2R5ToDB),					State(kD3ALUOp_And | kD3ALUIn2Load));
	opCli.AddState(State(),				State(kD2ALUResToDB),				State(kD3ALUOp_And | kD3ALUResLoad)	,	State(kD4DBToST));
	opCli.FetchExecPreInc();



	Extensions opSei;
	// Get 1 (ALU inc #0) to temp R5
	opSei.AddState(State(),				State(kD2ZeroToDB),					State());
	opSei.AddState(State(),				State(kD2ZeroToDB),					State(kD3ALUOp_Inc | kD3ALUIn1Load | kD3ALUIn2Load | kD3ALUIn3Load));
	opSei.AddState(State(),				State(kD2ALUResToDB),				State(kD3ALUOp_Inc | kD3ALUResLoad)	,	State(kD4DBToR5));

	// Calculate 1 << 2 using the ALU into temp R5
	opSei.AddState(State(),				State(kD2R5ToDB),					State());
	opSei.AddState(State(),				State(kD2R5ToDB),					State(kD3ALUOp_Lsl | kD3ALUIn1Load | kD3ALUIn2Load));
	opSei.AddState(State(),				State(kD2ALUResToDB),				State(kD3ALUOp_Lsl | kD3ALUResLoad)	,	State(kD4DBToR5));

	opSei.AddState(State(),				State(kD2R5ToDB),					State());
	opSei.AddState(State(),				State(kD2R5ToDB),					State(kD3ALUOp_Lsl | kD3ALUIn1Load | kD3ALUIn2Load));
	opSei.AddState(State(),				State(kD2ALUResToDB),				State(kD3ALUOp_Lsl | kD3ALUResLoad)	,	State(kD4DBToR5));

	// Now ST OR 4 back into ST
	opSei.AddState(State(),				State(kD2STToDB),					State());
	opSei.AddState(State(),				State(kD2STToDB),					State(kD3ALUIn1Load));
	opSei.AddState(State(),				State(kD2R5ToDB),					State());
	opSei.AddState(State(),				State(kD2R5ToDB),					State(kD3ALUOp_Or | kD3ALUIn2Load));
	opSei.AddState(State(),				State(kD2ALUResToDB),				State(kD3ALUOp_Or | kD3ALUResLoad)	,	State(kD4DBToST));
	opSei.FetchExecPreInc(false);	// No need for the IRQ check



	Extensions opRTI;
	// Load SP lo/hi into PC addr lo/hi then load PC with this address. The PC does not pre-inc when doing RTI.
	opRTI.AddState(State(),				State(kD2R3ToDB));
	opRTI.AddState(State(kD1AddrLLoad),	State(kD2R3ToDB));
	opRTI.AddState(State(),				State(kD2R4ToDB));
	opRTI.AddState(State(kD1AddrHLoad),	State(kD2R4ToDB));
	opRTI.AddState(State(kD1PCLoad));
	opRTI.AddState(State(kD1PCLoad | kD1PCInc));	// The kD1PCInc doesn't inc, it loads due to the kD1PCLoad
	opRTI.AddState();

	// Then pre-inc and load the ST
	opRTI.AddState(State(kD1PCInc));
	opRTI.LoadRegisterFromMemory(kD4DBToST,kD1PCToAddress);

	// Now pull the contents of the SP into addr lo/hi for eventual PC load
	// The stack is the full descending type (pre inc on read)
	opRTI.LoadAbsoluteAddressFromPCMemoryWithPreInc();
	// Now store the PC lo (which is pretending to be the SP lo) to the real SP lo. Don't bother with the SP hi since it doesn't change
	opRTI.AddState(State(kD1PCToAddress),	State(kD2ADDRWLToDB),	State(),	State(kD4DBToR3));
	// Now finally load the PC with the return address (which was pushed by entering the IRQ) and then fetch exec without pre-inc
	opRTI.AddState(State(kD1PCLoad));
	opRTI.AddState(State(kD1PCLoad | kD1PCInc));	// The kD1PCInc doesn't inc, it loads due to the kD1PCLoad
	opRTI.AddState();
	opRTI.FetchExec(false);	// When returning from an RTI I don't want to immediately process another IRQ. This may differ from the 6502, but tough. :)

	// A special case instruction that enters the IRQ operating level of the processor
	// Then set the ID flag in ST
	Extensions opStartIRQ;
	// Stack PC (actual address, so that RTI does a FetchExec without pre-inc) then ST
	// Prepare the address bus with the stack pointer
	// First SP hi
	opStartIRQ.AddState(State(),					State(kD2R4ToDB));
	opStartIRQ.AddState(State(kD1AddrHLoad),		State(kD2R4ToDB));
	// Loading the SP lo also prepare the ALU to dec the lo SP value
	opStartIRQ.AddState(State(),					State(kD2R3ToDB));
	opStartIRQ.AddState(State(kD1AddrLLoad),		State(kD2R3ToDB),	State(kD3ALUIn1Load | kD3ALUIn2Load));

	// Get PC hi to temp R6 and push onto stack
	opStartIRQ.AddState(State(kD1PCToAddress),		State(kD2ADDRWHToDB),	State(),	State(kD4DBToR6));
	opStartIRQ.WriteRegisterToMemory(kD2R6ToDB);
	// Dec lo SP and load into addr lo
	opStartIRQ.AddState(State(),		State(kD2ALUResToDB),		State(kD3ALUOp_Dec));
	opStartIRQ.AddState(State(),		State(kD2ALUResToDB),		State(kD3ALUOp_Dec | kD3ALUResLoad)	,	State(kD4DBToR3));
	opStartIRQ.AddState(State(),				State(kD2R3ToDB));
	opStartIRQ.AddState(State(kD1AddrLLoad),	State(kD2R3ToDB),	State(kD3ALUIn1Load | kD3ALUIn2Load));
	// Get PC lo to temp R6 and push onto stack
	opStartIRQ.AddState(State(kD1PCToAddress),		State(kD2ADDRWLToDB),	State(),	State(kD4DBToR6));
	opStartIRQ.WriteRegisterToMemory(kD2R6ToDB);
	// Dec lo SP
	opStartIRQ.AddState(State(),		State(kD2ALUResToDB),		State(kD3ALUOp_Dec));
	opStartIRQ.AddState(State(),		State(kD2ALUResToDB),		State(kD3ALUOp_Dec | kD3ALUResLoad)	,	State(kD4DBToR3));

	// Now push the ST
	opStartIRQ.AddState(State(),				State(kD2R3ToDB));
	opStartIRQ.AddState(State(kD1AddrLLoad),	State(kD2R3ToDB),	State(kD3ALUOp_Dec | kD3ALUIn1Load | kD3ALUIn2Load));
	opStartIRQ.WriteRegisterToMemory(kD2STToDB);
	// Dec lo SP
	opStartIRQ.AddState(State(),		State(kD2ALUResToDB),		State(kD3ALUOp_Dec));
	opStartIRQ.AddState(State(),		State(kD2ALUResToDB),		State(kD3ALUOp_Dec | kD3ALUResLoad)	,	State(kD4DBToR3));

	// Now start updating ST
	// Get 1 (ALU inc #0) to temp R5
	opStartIRQ.AddState(State(),				State(kD2ZeroToDB));
	opStartIRQ.AddState(State(),				State(kD2ZeroToDB),					State(kD3ALUOp_Inc | kD3ALUIn1Load | kD3ALUIn2Load | kD3ALUIn3Load));
	opStartIRQ.AddState(State(),				State(kD2ALUResToDB),				State(kD3ALUOp_Inc | kD3ALUResLoad)	,	State(kD4DBToR5));

	// Calculate 1 << 2 using the ALU into temp R5
	opStartIRQ.AddState(State(),				State(kD2R5ToDB),					State());
	opStartIRQ.AddState(State(),				State(kD2R5ToDB),					State(kD3ALUOp_Lsl | kD3ALUIn1Load | kD3ALUIn2Load));
	opStartIRQ.AddState(State(),				State(kD2ALUResToDB),				State(kD3ALUOp_Lsl | kD3ALUResLoad)	,	State(kD4DBToR5));

	opStartIRQ.AddState(State(),				State(kD2R5ToDB),					State());
	opStartIRQ.AddState(State(),				State(kD2R5ToDB),					State(kD3ALUOp_Lsl | kD3ALUIn1Load | kD3ALUIn2Load));
	opStartIRQ.AddState(State(),				State(kD2ALUResToDB),				State(kD3ALUOp_Lsl | kD3ALUResLoad)	,	State(kD4DBToR5));

	// Now ST OR 4 back into ST
	opStartIRQ.AddState(State(),				State(kD2STToDB),					State());
	opStartIRQ.AddState(State(),				State(kD2STToDB),					State(kD3ALUIn1Load));
	opStartIRQ.AddState(State(),				State(kD2R5ToDB),					State());
	opStartIRQ.AddState(State(),				State(kD2R5ToDB),					State(kD3ALUOp_Or | kD3ALUIn2Load));
	opStartIRQ.AddState(State(),				State(kD2ALUResToDB),				State(kD3ALUOp_Or | kD3ALUResLoad)	,	State(kD4DBToST));
	// ST Now has I bit set, interrupts disabled.

	// Get the state code for opcode $7f to execute into opStartIRQ2.
	opStartIRQ.AddState(State(),				State(kD2FFToDB));
	opStartIRQ.AddState(State(),				State(kD2FFToDB),					State(kD3ALUIn1Load | kD3ALUIn2Load));
	opStartIRQ.AddState(State(),				State(kD2ALUResToDB),				State(kD3ALUOp_Lsr));
	opStartIRQ.AddState(State(kD1OpCodeLoad),	State(kD2ALUResToDB),				State(kD3ALUOp_Lsr | kD3ALUResLoad));
	opStartIRQ.AddState(State(),				State(kD2ALUResToDB),				State(kD3ALUOp_Lsr));
	opStartIRQ.AddState(State(kD1CycleReset));

	// Extension of the IRQ start code. Entering the IRQ operating level has a lot of states. :)
	Extensions opStartIRQ2;
	// Now load the IRQ vector and start executing from there
	// 0xff to AddrH 
	opStartIRQ2.AddState(State(),				State(kD2FFToDB),					State());
	opStartIRQ2.AddState(State(kD1AddrHLoad),	State(kD2FFToDB),					State(kD3ALUOp_Dec | kD3ALUIn1Load | kD3ALUIn2Load));

	// Calc 0xfe and put into AddrL
	opStartIRQ2.AddState(State(),				State(kD2ALUResToDB),				State(kD3ALUOp_Dec | kD3ALUResLoad));
	opStartIRQ2.AddState(State(kD1AddrLLoad),	State(kD2ALUResToDB),				State(kD3ALUOp_Dec));

	// Load into PC, remembering the load is actually done on the positive edge
	opStartIRQ2.AddState(State(kD1PCLoad),				State(),				State());
	opStartIRQ2.AddState(State(kD1PCLoad | kD1PCInc),	State(),				State());

	// $fffe is now in the PC
	// Proceed to load the memory into the address lo and hi
	opStartIRQ2.LoadRegisterFromMemory(0,kD1AddrLLoad | kD1PCToAddress);
	opStartIRQ2.AddState(State(kD1PCInc));
	opStartIRQ2.AddState();
	opStartIRQ2.LoadRegisterFromMemory(0,kD1AddrHLoad | kD1PCToAddress);

	// Load PC from address fetched from memory and held in the memory input latches
	opStartIRQ2.AddState(State(kD1PCLoad));
	opStartIRQ2.AddState(State(kD1PCLoad | kD1PCInc));
	opStartIRQ2.AddState();

	opStartIRQ2.FetchExec(false);	// No need for the IRQ check



	// A rather special case opcode that bootstraps the whole processor
	Extensions opBoot;
	opBoot.AddState();

	// Just to be sure we have a couple more zero states to let the clock settle after a reset.
	opBoot.AddState();
	opBoot.AddState();

	// Get zero to ALU and status
	opBoot.AddState(State(),				State(kD2ZeroToDB));
	opBoot.AddState(State(),				State(kD2ZeroToDB),		State(kD3ALUOp_Inc | kD3ALUIn1Load | kD3ALUIn2Load | kD3ALUIn3Load),	State(kD4DBToST));

	// Get 1 (ALU inc #0) to SP hi and temp R5
	opBoot.AddState(State(),				State(kD2ALUResToDB),		State(kD3ALUOp_Inc | kD3ALUResLoad)	,	State(kD4DBToR4 | kD4DBToR5));

	// Do ALU ADD #0,#0 with carry clear to clear kD2DoBranchLoad. No need for kD3ALUResLoad since the branch logic loads from the input to the output latch
	opBoot.AddState(State(),				State(),					State(kD3ALUOp_Add));
	opBoot.AddState(State(),				State(kD2DoBranchLoad),		State(kD3ALUOp_Add));

	// Calculate 1 << 2 using the ALU and set that for the status
	opBoot.AddState(State(),				State(kD2R5ToDB),					State());
	opBoot.AddState(State(),				State(kD2R5ToDB),					State(kD3ALUOp_Lsl | kD3ALUIn1Load | kD3ALUIn2Load));

	opBoot.AddState(State(),				State(kD2ALUResToDB),				State(kD3ALUOp_Lsl | kD3ALUResLoad)	,	State(kD4DBToR5));

	opBoot.AddState(State(),				State(kD2R5ToDB),					State());
	opBoot.AddState(State(),				State(kD2R5ToDB),					State(kD3ALUOp_Lsl | kD3ALUIn1Load | kD3ALUIn2Load));

	opBoot.AddState(State(),				State(kD2ALUResToDB),				State(kD3ALUOp_Lsl | kD3ALUResLoad)	,	State(kD4DBToST));

	// 0xff to AddrH stack pointer lo and temp r0
	opBoot.AddState(State(),				State(kD2FFToDB),					State());
	opBoot.AddState(State(kD1AddrHLoad),	State(kD2FFToDB),					State(),		State(kD4DBToR0 | kD4DBToR3));

	opBoot.AddState(State(),				State(kD2FFToDB),					State());
	opBoot.AddState(State(),				State(kD2FFToDB),					State(kD3ALUOp_Dec | kD3ALUIn1Load | kD3ALUIn2Load));

	// Calc 0xfe
	opBoot.AddState(State(),				State(kD2ALUResToDB),				State(kD3ALUOp_Dec | kD3ALUResLoad)	,		State(kD4DBToR0));

	opBoot.AddState(State(),				State(kD2R0ToDB),					State());
	opBoot.AddState(State(),				State(kD2R0ToDB),					State(kD3ALUOp_Dec | kD3ALUIn1Load | kD3ALUIn2Load));

	// Calc 0xfd
	opBoot.AddState(State(),				State(kD2ALUResToDB),				State(kD3ALUOp_Dec | kD3ALUResLoad)	,		State(kD4DBToR0));

	opBoot.AddState(State(),				State(kD2R0ToDB),					State());
	opBoot.AddState(State(),				State(kD2R0ToDB),					State(kD3ALUOp_Dec | kD3ALUIn1Load | kD3ALUIn2Load));

	// Calc 0xfc
	opBoot.AddState(State(),				State(kD2ALUResToDB),				State(kD3ALUOp_Dec | kD3ALUResLoad));
	opBoot.AddState(State(kD1AddrLLoad),	State(kD2ALUResToDB),				State());

	// Load into PC, remembering the load is actually done on the positive edge
	opBoot.AddState(State(kD1PCLoad),				State(),				State());
	opBoot.AddState(State(kD1PCLoad | kD1PCInc),	State(),				State());

	// Get zero to the A,X,Y registers
	opBoot.AddState(State(),				State(kD2ZeroToDB),		State(),	State(kD4DBToR0 | kD4DBToR1 | kD4DBToR2));

	// $fffc is now in the PC
	// Proceed to load the memory into the address lo and hi
	opBoot.LoadRegisterFromMemory(0,kD1AddrLLoad | kD1PCToAddress);
	opBoot.AddState(State(kD1PCInc));
	opBoot.AddState();
	opBoot.LoadRegisterFromMemory(0,kD1AddrHLoad | kD1PCToAddress);

	// Load PC from address fetched from memory and held in the memory input latches
	opBoot.AddState(State(kD1PCLoad));
	opBoot.AddState(State(kD1PCLoad | kD1PCInc));
	opBoot.AddState();

	// Load next opcode so we don't go cycling around this JAM instruction
	opBoot.FetchExec();



	Extensions *opcodes[256] = {
		0,		// 00    BRK$
		&opOra_IndZPAddr_X,		// 01    ORA (zp,X)$
		&opIllegal,		// 02  * HALT$
		0,		// 03  * ASL-ORA (zp,X)$
		0,		// 04  * NOP zp$
		&opOra_ZPAddr,		// 05    ORA zp$
		&opASL_ZPAddr,		// 06    ASL zp$
		0,		// 07  * ASL-ORA zp$
		&opPHP,		// 08    PHP$
		&opOra_Immediate,		// 09    ORA #n$
		&opASL,		// 0A    ASL A$
		0,		// 0B  * AND #n/MOV b7->Cy$
		0,		// 0C  * NOP abs$
		&opOra_Addr,		// 0D    ORA abs$
		&opASL_Addr,		// 0E    ASL abs$
		0,		// 0F  * ASL-ORA abs$

		&opBPL0,		// 10    BPL rel$
		&opOra_IndZPAddr_Y,		// 11    ORA (zp),Y$
		0,		// 12  * HALT$
		0,		// 13  * ASL-ORA (zp),Y$
		0,		// 14  * NOP zp$
		&opOra_ZPAddr_X,		// 15    ORA zp,X$
		&opASL_ZPAddr_X,		// 16    ASL zp,X$
		0,		// 17  * ASL-ORA abs,X$
		&opClc,		// 18    CLC$
		&opOra_Addr_Y,		// 19    ORA abs,Y$
		0,		// 1A  * NOP$
		0,		// 1B  * ASL-ORA abs,Y$
		0,		// 1C  * NOP abs$
		&opOra_Addr_X,		// 1D    ORA abs,X$
		&opASL_Addr_X,		// 1E    ASL abs,X$
		0,		// 1F  * ASL-ORA abs,X$

		&opJSR_Addr,		// 20    JSR abs$
		&opAnd_IndZPAddr_X,		// 21    AND (zp,X)$
		0,		// 22  * HALT$
		0,		// 23  * ROL-AND (zp,X)$
		&opBIT_ZPAddr,		// 24    BIT zp$
		&opAnd_ZPAddr,		// 25    AND zp$
		&opROL_ZPAddr,		// 26    ROL zp$
		0,		// 27  * ROL-AND zp$
		&opPLP,		// 28    PLP$
		&opAnd_Immediate,		// 29    AND #n$
		&opROL,		// 2A    ROL A$
		0,		// 2B  * AND #n-MOV b7->Cy$
		&opBIT_Addr,		// 2C    BIT abs$
		&opAnd_Addr,		// 2D    AND abs$
		&opROL_Addr,		// 2E    ROL abs$
		0,		// 2F  * ROL-AND abs$

		&opBMI0,		// 30    BMI rel$
		&opAnd_IndZPAddr_Y,		// 31    AND (zp),Y$
		0,		// 32  * HALT$
		0,		// 33  * ROL-AND (zp),Y$
		0,		// 34  * NOP zp$
		&opAnd_ZPAddr_X,		// 35    AND zp,X$
		&opROL_ZPAddr_X,		// 36    ROL zp,X$
		0,		// 37  * ROL-AND zp,X$
		&opSec,		// 38    SEC$
		&opAnd_Addr_Y,		// 39    AND abs,Y$
		0,		// 3A  * NOP$
		0,		// 3B  * ROL-AND abs,Y$
		0,		// 3C  * NOP abs$
		&opAnd_Addr_X,		// 3D    AND abs,X$
		&opROL_Addr_X,		// 3E    ROL abs,X$
		0,		// 3F  * ROL-AND abs,X$

		&opRTI,		// 40    RTI$
		&opEor_IndZPAddr_X,		// 41    EOR (zp,X)$
		0,		// 42  * HALT$
		0,		// 43  * LSR-EOR (zp,X)$
		0,		// 44  * NOP zp$
		&opEor_ZPAddr,		// 45    EOR zp$
		&opLSR_ZPAddr,		// 46    LSR zp$
		0,		// 47  * LSR-EOR zp$
		&opPHA,		// 48    PHA$
		&opEor_Immediate,		// 49    EOR #n$
		&opLSR,		// 4A    LSR A$
		0,		// 4B  * AND #n-LSR A$
		&opJMP_Addr,		// 4C    JMP abs$
		&opEor_Addr,		// 4D    EOR abs$
		&opLSR_Addr,		// 4E    LSR abs$
		0,		// 4F  * LSR-EOR abs$

		&opBVC0,		// 50    BVC rel$
		&opEor_IndZPAddr_Y,		// 51    EOR (zp),Y$
		0,		// 52  * HALT$
		0,		// 53  * LSR-EOR (zp),Y$
		0,		// 54  * NOP zp$
		&opEor_ZPAddr_X,		// 55    EOR zp,X$
		&opLSR_ZPAddr_X,		// 56    LSR zp,X$
		0,		// 57  * LSR-EOR abs,X$
		&opCli,		// 58    CLI$
		&opEor_Addr_Y,		// 59    EOR abs,Y$
		0,		// 5A  * NOP$
		0,		// 5B  * LSR-EOR abs,Y$
		0,		// 5C  * NOP abs$
		&opEor_Addr_X,		// 5D    EOR abs,X$
		&opLSR_Addr_X,		// 5E    LSR abs,X$
		0,		// 5F  * LSR-EOR abs,X$

		&opRTS,		// 60    RTS$
		&opAdc_IndZPAddr_X,		// 61    ADC (zp,X)$
		0,		// 62  * HALT$
		0,		// 63  * ROR-ADC (zp,X)$
		0,		// 64  * NOP zp$
		&opAdc_ZPAddr,		// 65    ADC zp$
		&opROR_ZPAddr,		// 66    ROR zp$
		0,		// 67  * ROR-ADC zp$
		&opPLA,		// 68    PLA$
		&opAdc_Immediate,		// 69    ADC #n$
		&opROR,		// 6A    ROR A$
		0,		// 6B  * AND #n-ROR A$
		&opJMP_IndAddr,		// 6C    JMP (abs)$
		&opAdc_Addr,		// 6D    ADC abs$
		&opROR_Addr,		// 6E    ROR abs$
		0,		// 6F  * ROR-ADC abs$

		&opBVS0,		// 70    BVS rel$
		&opAdc_IndZPAddr_Y,		// 71    ADC (zp),Y$
		0,		// 72  * HALT$
		0,		// 73  * ROR-ADC (zp),Y$
		0,		// 74  * NOP zp$
		&opAdc_ZPAddr_X,		// 75    ADC zp,X$
		&opROR_ZPAddr_X,		// 76    ROR zp,X$
		0,		// 77  * ROR-ADC abs,X$
		&opSei,		// 78    SEI$
		&opAdc_Addr_Y,		// 79    ADC abs,Y$
		0,		// 7A  * NOP$
		0,		// 7B  * ROR-ADC abs,Y$
		0,		// 7C  * NOP abs$
		&opAdc_Addr_X,		// 7D    ADC abs,X$
		&opROR_Addr_X,		// 7E    ROR abs,X$
		&opStartIRQ2,		// 7F  * ROR-ADC abs,X$

		0,		// 80  * NOP zp$
		&opSTA_IndZPAddr_X,		// 81    STA (zp,X)$
		0,		// 82  * HALT$
		0,		// 83  * STA-STX (zp,X)$
		&opSTY_ZPAddr,		// 84    STY zp$
		&opSTA_ZPAddr,		// 85    STA zp$
		&opSTX_ZPAddr,		// 86    STX zp$
		0,		// 87  * STA-STX zp$
		&opDEY,		// 88    DEY$
		0,		// 89  * NOP zp$
		&opTXA,		// 8A    TXA A$
		0,		// 8B  * TXA-AND #n$
		&opSTY_Addr,		// 8C    STY abs$
		&opSTA_Addr,		// 8D    STA abs$
		&opSTX_Addr,		// 8E    STX abs$
		0,		// 8F  * STA-STX abs$

		&opBCC0,		// 90    BCC rel$
		&opSTA_IndZPAddr_Y,		// 91    STA (zp),Y$
		0,		// 92  * HALT$
		0,		// 93  * STA-STX (zp),Y$
		&opSTY_ZPAddr_X,		// 94    STY zp$,x
		&opSTA_ZPAddr_X,		// 95    STA zp,X$
		&opSTX_ZPAddr_Y,		// 96    STX zp,Y$
		0,		// 97  * STA-STX zp,Y$
		&opTYA,		// 98    TYA$
		&opSTA_Addr_Y,		// 99    STA abs,Y$
		&opTXS,		// 9A    TXS$
		0,		// 9B  * STA-STX abs,Y$
		0,		// 9C  * STA-STX abs,X$
		&opSTA_Addr_X,		// 9D    STA abs,X$
		0,		// 9E  * STA-STX abs,X$
		0,		// 9F  * STA-STX abs,X$

		&opLDY_Immediate,		// A0    LDY #n$
		&opLDA_IndZPAddr_X,		// A1    LDA (zp,X)$
		&opLDX_Immediate,		// A2    LDX #n$
		0,		// A3  * LDA-LDX (zp,X)$
		&opLDY_ZPAddr,		// A4    LDY zp$
		&opLDA_ZPAddr,		// A5    LDA zp$
		&opLDX_ZPAddr,		// A6    LDX zp$
		0,		// A7  * LDA-LDX zp$
		&opTAY,		// A8    TAY$
		&opLDA_Immediate,		// A9    LDA #n$
		&opTAX,		// AA    TAX$
		0,		// AB  * LDA-LDX$
		&opLDY_Addr,		// AC    LDY abs$
		&opLDA_Addr,		// AD    LDA abs$
		&opLDX_Addr,		// AE    LDX abs$
		0,		// AF  * LDA-LDX abs$

		&opBCS0,		// B0    BCS rel$
		&opLDA_IndZPAddr_Y,		// B1    LDA (zp),Y$
		0,		// B2  * HALT$
		0,		// B3  * LDA-LDX (zp),Y$
		&opLDY_ZPAddr_X,		// B4    LDY zp$
		&opLDA_ZPAddr_X,		// B5    LDA zp,X$
		&opLDX_ZPAddr_Y,		// B6    LDX zp,Y$
		0,		// B7  * LDA-LDX zp,Y$
		&opClv,		// B8    CLV$
		&opLDA_Addr_Y,		// B9    LDA abs,Y$
		&opTSX,		// BA    TSX$
		0,		// BB  * LDA-LDX abs,Y$
		&opLDY_Addr_X,		// BC    LDY abs,X$
		&opLDA_Addr_X,		// BD    LDA abs,X$
		&opLDX_Addr_Y,		// BE    LDX abs,Y$
		0,		// BF  * LDA-LDX abs,Y$

		&opCpy_Immediate,		// C0    CPY #n$
		&opCmp_IndZPAddr_X,		// C1    CMP (zp,X)$
		0,		// C2  * HALT$
		0,		// C3  * DEC-CMP (zp,X)$
		&opCpy_ZPAddr,		// C4    CPY zp$
		&opCmp_ZPAddr,		// C5    CMP zp$
		&opDEC_ZPAddr,		// C6    DEC zp$
		0,		// C7  * DEC-CMP zp$
		&opINY,		// C8    INY$
		&opCmp_Immediate,		// C9    CMP #n$
		&opDEX,		// CA    DEX$
		0,		// CB  * SBX #n$
		&opCpy_Addr,		// CC    CPY abs$
		&opCmp_Addr,		// CD    CMP abs$
		&opDEC_Addr,		// CE    DEC abs$
		0,		// CF  * DEC-CMP abs$

		&opBNE0,		// D0    BNE rel$
		&opCmp_IndZPAddr_Y,		// D1    CMP (zp),Y$
		0,		// D2  * HALT$
		0,		// D3  * DEC-CMP (zp),Y$
		0,		// D4  * NOP zp$
		&opCmp_ZPAddr_X,		// D5    CMP zp,X$
		&opINC_ZPAddr_X,		// D6    DEC zp,X$
		0,		// D7  * DEC-CMP zp,X$
		&opNOP,		// D8    CLD$
		&opCmp_Addr_Y,		// D9    CMP abs,Y$
		0,		// DA  * NOP$
		0,		// DB  * DEC-CMP abs,Y$
		0,		// DC  * NOP abs$
		&opCmp_Addr_X,		// DD    CMP abs,X$
		&opDEC_Addr_X,		// DE    DEC abs,X$
		0,		// DF  * DEC-CMP abs,X$

		&opCpx_Immediate,		// E0    CPX #n$
		&opSbc_IndZPAddr_X,		// E1    SBC (zp,X)$
		0,		// E2  * HALT$
		0,		// E3  * INC-SBC (zp,X)$
		&opCpx_ZPAddr,		// E4    CPX zp$
		&opSbc_ZPAddr,		// E5    SBC zp$
		&opINC_ZPAddr,		// E6    INC zp$
		0,		// E7  * INC-SBC zp$
		&opINX,		// E8    INX$
		&opSbc_Immediate,		// E9    SBC #n$
		&opNOP,		// EA    NOP$
		0,		// EB  *? SBC #n$
		&opCpx_Addr,		// EC    CPX abs$
		&opSbc_Addr,		// ED    SBC abs$
		&opINC_Addr,		// EE    INC abs$
		0,		// EF  * INC-SBC abs$

		&opBEQ0,		// F0    BEQ rel$
		&opSbc_IndZPAddr_Y,		// F1    SBC (zp),Y$
		0,		// F2  * HALT$
		0,		// F3  * INC-SBC (zp),Y$
		0,		// F4  * NOP zp$
		&opSbc_ZPAddr_X,		// F5    SBC zp,X$
		&opINC_ZPAddr_X,		// F6    INC zp,X$
		0,		// F7  * INC-SBC zp,X$
		&opNOP,		// F8    SED$
		&opSbc_Addr_Y,		// F9    SBC abs,Y$
		0,		// FA  * NOP$
		0,		// FB  * INC-SBC abs,Y$
		&opStartIRQ,		// FC  * NOP abs$
		&opSbc_Addr_X,		// FD    SBC abs,X$
		&opINC_Addr_X,		// FE    INC abs,X$
		&opBoot,		// FF  * INC-SBC abs,X$
	};

	// Sparse array
	Extensions *opcodesDoBranch[256];
	memset(opcodesDoBranch,0,sizeof(opcodesDoBranch));
	opcodesDoBranch[0x10] = &opBPL1;
	opcodesDoBranch[0x30] = &opBMI1;
	opcodesDoBranch[0x50] = &opBVC1;
	opcodesDoBranch[0x70] = &opBVS1;
	opcodesDoBranch[0x90] = &opBCC1;
	opcodesDoBranch[0xb0] = &opBCS1;
	opcodesDoBranch[0xd0] = &opBNE1;
	opcodesDoBranch[0xf0] = &opBEQ1;


	size_t opCodeLengths[256];
	memset(opCodeLengths,0,sizeof(opCodeLengths));

	// Output opcodes
	FILE *fp,*fp2;
	int i;
	int decoder;
	for (decoder = 1;decoder <= 5; decoder++)
	{
		char buffer[256];
		sprintf(buffer,"../DecoderROM%d.bin",decoder);
		fp = fopen(buffer,"wb");

		int op;
		// Write base opcodes
		for (op=0;op<256;op++)
		{
			if (opcodes[op])
			{
				opcodes[op]->Write(decoder-1,fp);
				opCodeLengths[op] = __max(opcodes[op]->GetLength(),opCodeLengths[op]);
			}
			else
			{
				opIllegal.Write(decoder-1,fp);
			}
		}

		// Write do branch opcodes
		for (op=0;op<256;op++)
		{
			if (opcodesDoBranch[op])
			{
				opcodesDoBranch[op]->Write(decoder-1,fp);
				opCodeLengths[op] = __max(opcodesDoBranch[op]->GetLength(),opCodeLengths[op]);
			}
			else if (opcodes[op])
			{
				opcodes[op]->Write(decoder-1,fp);
				opCodeLengths[op] = __max(opcodes[op]->GetLength(),opCodeLengths[op]);
			}
			else
			{
				opIllegal.Write(decoder-1,fp);
			}
		}

		fclose(fp);
	}



	// Output opcodes for IRQ enabled states
	for (decoder = 1;decoder <= 5; decoder++)
	{
		char buffer[256];
		sprintf(buffer,"../DecoderROM%d.bin",decoder);
		fp = fopen(buffer,"a+b");

		int op;

		for (op=0;op<256;op++)
		{
			if (opcodes[op])
			{
				opcodes[op]->FindIRQLEAndReplace();
				opcodes[op]->Write(decoder-1,fp);
				opCodeLengths[op] = __max(opcodes[op]->GetLength(),opCodeLengths[op]);
			}
			else
			{
				opIllegal.FindIRQLEAndReplace();
				opIllegal.Write(decoder-1,fp);
			}
		}
		for (op=0;op<256;op++)
		{
			if (opcodesDoBranch[op])
			{
				opcodesDoBranch[op]->FindIRQLEAndReplace();
				opcodesDoBranch[op]->Write(decoder-1,fp);
				opCodeLengths[op] = __max(opcodesDoBranch[op]->GetLength(),opCodeLengths[op]);
			}
			else if (opcodes[op])
			{
				opcodes[op]->FindIRQLEAndReplace();
				opcodes[op]->Write(decoder-1,fp);
				opCodeLengths[op] = __max(opcodes[op]->GetLength(),opCodeLengths[op]);
			}
			else
			{
				opIllegal.FindIRQLEAndReplace();
				opIllegal.Write(decoder-1,fp);
			}
		}


		fclose(fp);
	}


	for (i=0;i<256;i+=8)
	{
		printf("Opcode %2x : %2d %2d %2d %2d %2d %2d %2d %2d\n",i,opCodeLengths[i+0],opCodeLengths[i+1],opCodeLengths[i+2],opCodeLengths[i+3],opCodeLengths[i+4],opCodeLengths[i+5],opCodeLengths[i+6],opCodeLengths[i+7]);
	}

	// Write ALU1
	// ALU operations with 1 input use both inputs set the same
	fp = fopen("../ALU1.bin","wb");
	fp2 = fopen("../ALU2.bin","wb");
	int j,op,inFlags;
	for (inFlags=0;inFlags<=15;inFlags++)
	{
		for (j=0;j<16;j++)
		{
			for (i=0;i<16;i++)
			{
				for (op=kD3ALUOp_Dec;op<=kD3ALUOp_Flags;op+=8)
				{
					switch(op)
					{
						case kD3ALUOp_Dec:
						{
							// ALU1
							unsigned char work = i,flags;
							work--;
							flags = PreserveCarryFlag(inFlags) | CalculateZeroFlag(work);
							unsigned char isSpecial = 0;
							if ( (work & 15) == 15)
							{
								isSpecial |= kALU1OutFlg_Special;
							}
							fputc((work & 15) | flags | isSpecial,fp);

							// ALU2
							work = i;
							if (inFlags & kALUInFlg_Special)
							{
								work--;
							}
							flags = PreserveCarryFlag(inFlags) | CalculateZeroFlag(work) | CalculateNegativeFlag(work);
							flags |= PreserveOverflowFlag(inFlags);
							fputc((work & 15) | flags,fp2);
							break;
						}
						case kD3ALUOp_Inc:
						{
							// ALU1
							unsigned char work = i,flags;
							work++;
							flags = PreserveCarryFlag(inFlags) | CalculateZeroFlag(work);
							unsigned char isSpecial = 0;
							if ( (work & 15) == 0)
							{
								isSpecial |= kALU1OutFlg_Special;
							}
							fputc((work & 15) | flags | isSpecial,fp);

							// ALU2
							work = i;
							if (inFlags & kALUInFlg_Special)
							{
								work++;
							}
							flags = PreserveCarryFlag(inFlags) | CalculateZeroFlag(work) | CalculateNegativeFlag(work);
							flags |= PreserveOverflowFlag(inFlags);
							fputc((work & 15) | flags,fp2);
							break;
						}
						case kD3ALUOp_Add:
						{
							unsigned char work = i,flags;
							if (inFlags & kALUInFlg_C)
							{
								work++;
							}
							work += j;
							flags = CalculateNCZFlags(work);
							if (work & 16)
							{
								flags |= kALUOutFlg_C;
							}

							// pos and pos add
							if (!CalculateNegativeFlag(i) && !CalculateNegativeFlag(j))
							{
								if (CalculateNegativeFlag(work))
								{
									flags |= kALUOutFlg_V;
								}
							}

							// neg and neg add
							if (CalculateNegativeFlag(i) && CalculateNegativeFlag(j))
							{
								if (!CalculateNegativeFlag(work))
								{
									flags |= kALUOutFlg_V;
								}
							}

							fputc(((work) & 15) | flags,fp);
							fputc(((work) & 15) | flags,fp2);
							break;
						}
						case kD3ALUOp_Sub:
						{
							unsigned char work = i,flags;
							work -= j;
							if (!(inFlags & kALUInFlg_C))
							{
								work--;
							}
							flags = CalculateNZFlags(work);
							if (!(work & 16))
							{
								flags |= kALUOutFlg_C;
							}

							// pos and neg sub
							if (!CalculateNegativeFlag(i) && CalculateNegativeFlag(j))
							{
								if (!CalculateNegativeFlag(work))
								{
									flags |= kALUOutFlg_V;
								}
							}

							// neg and pos sub
							if (CalculateNegativeFlag(i) && !CalculateNegativeFlag(j))
							{
								if (!CalculateNegativeFlag(work))
								{
									flags |= kALUOutFlg_V;
								}
							}

							fputc(((work) & 15) | flags,fp);
							fputc(((work) & 15) | flags,fp2);
							break;
						}
						case kD3ALUOp_Or:
						{
							unsigned char work = i,flags;
							work |= j;
							flags = PreserveCarryFlag(inFlags) | CalculateZeroFlag(work);
							flags |= PreserveOverflowFlag(inFlags);
							fputc(((work) & 15) | flags,fp);
							fputc(((work) & 15) | flags,fp2);
							break;
						}
						case kD3ALUOp_And:
						{
							unsigned char work = i,flags;
							work &= j;
							flags = PreserveCarryFlag(inFlags) | CalculateZeroFlag(work);
							flags |= PreserveOverflowFlag(inFlags);
							fputc(((work) & 15) | flags,fp);
							fputc(((work) & 15) | flags,fp2);
							break;
						}
						case kD3ALUOp_Xor:
						{
							unsigned char work = i,flags;
							work ^= j;
							flags = PreserveCarryFlag(inFlags) | CalculateZeroFlag(work);
							flags |= PreserveOverflowFlag(inFlags);
							fputc(((work) & 15) | flags,fp);
							fputc(((work) & 15) | flags,fp2);
							break;
						}
						case kD3ALUOp_Lsl:
						{
							unsigned char work,flags;
							// ALU1
							work = i;
							flags = 0;
							if (work & (1<<3))
							{
								flags = kALUOutFlg_C;
							}
							work = work << 1;
							work = work & 15;
							flags |= CalculateZeroFlag(work) | CalculateNegativeFlag(work);
							flags |= PreserveOverflowFlag(inFlags);
							fputc((work & 15) | flags,fp);

							// ALU2
							work = i;
							flags = 0;
							if (work & (1<<3))
							{
								flags = kALUOutFlg_C;
							}
							work = work << 1;
							if (inFlags & kALUInFlg_C)
							{
								work = work | (unsigned char)(1<<0);
							}
							work = work & 15;
							flags |= CalculateZeroFlag(work) | CalculateNegativeFlag(work);
							flags |= PreserveOverflowFlag(inFlags);
							fputc((work & 15) | flags,fp2);

							break;
						}
						case kD3ALUOp_Lsr:
						{
							// ALU1
							unsigned char work = i,flags = 0;
							if (work & 1)
							{
								flags |= kALUOutFlg_C;	// We want carry output by ALU2
							}
							work = work >> 1;
							if (inFlags & kALUInFlg_Special)
							{
								work = work | (unsigned char)(1<<3);
							}
							flags |= CalculateZeroFlag(work);
							fputc((work & 15) | flags,fp);

							// ALU2
							work = i;
							work = work >> 1;
							flags = CalculateZeroFlag(work) | CalculateNegativeFlag(work);
							flags |= PreserveOverflowFlag(inFlags) | PreserveCarryFlag(inFlags);
							fputc((work & 15) | flags,fp2);
							break;
						}
						case kD3ALUOp_Rol:
						{
							unsigned char work = i,flags = 0;
							if (work & (1<<3))
							{
								flags = kALUOutFlg_C;
							}
							work = work << 1;
							if (inFlags & kALUInFlg_C)
							{
								work = work | (unsigned char)(1<<0);
							}
							work = work & 15;
							flags |= CalculateZeroFlag(work) | CalculateNegativeFlag(work);
							flags |= PreserveOverflowFlag(inFlags);
							fputc((work & 15) | flags,fp);
							fputc((work & 15) | flags,fp2);
							break;
						}
						case kD3ALUOp_Ror:
						{
							// ALU1
							unsigned char work = i,flags = 0;
							if (work & 1)
							{
								flags |= kALU1OutFlg_Special;	// Flag that we want carry output by ALU2
							}
							work = work >> 1;
							if (inFlags & kALUInFlg_Special)
							{
								work = work | (unsigned char)(1<<3);
							}
							flags |= CalculateZeroFlag(work);
							flags |= PreserveCarryFlag(inFlags);	// Preserve carry flag to ALU2
							fputc((work & 15) | flags,fp);

							// ALU2
							work = i;
							work = work >> 1;
							if (inFlags & kALUInFlg_C)
							{
								work = work | (unsigned char)(1<<3);
							}
							work = work & 15;
							flags = CalculateZeroFlag(work) | CalculateNegativeFlag(work);
							flags |= PreserveOverflowFlag(inFlags);
							if (inFlags & kALUInFlg_Special)
							{
								flags |= kALUOutFlg_C;
							}
							fputc((work & 15) | flags,fp2);
							break;
						}
						case kD3ALUOp_Cmp:
						{
							// Ignore carry on input otherwise this is much like the "sub" state.
							unsigned char work,flags;
							work = i - j;
							flags = CalculateCZFlags(work);
							flags |= PreserveOverflowFlag(inFlags);
							if (work == 0)
							{
								flags |= kALU1OutFlg_Special;
							}
							fputc(((work) & 15) | flags,fp);

							work = i - j;
							// Use carry from ALU1 to this ALU2
							if ((inFlags & kALUInFlg_C))
							{
								work--;
							}
							flags = CalculateNCZFlags(work);
							flags ^= kALUOutFlg_C;	// Invert the carry
							if ( (work == 0) && (inFlags & kALUInFlg_Special) )
							{
								flags |= kALUOutFlg_C;
							}
							flags |= PreserveOverflowFlag(inFlags);
							fputc(((work) & 15) | flags,fp2);
							break;
						}
						case kD3ALUOp_Sec:
						{
							if (inFlags == 0)
							{
								unsigned char work = i;
								fputc((work & 15) ,fp2);
								work |= 1;
								fputc((work & 15) ,fp);
							}
							else
							{
								unsigned char work;
								work = 0x02;
								fputc( i & (work & 15),fp);
								fputc( i & ((work >> 4) & 15) ,fp2);
							}
							break;
						}
						case kD3ALUOp_Clc:
						{
							if (inFlags == 0)
							{
								unsigned char work = i;
								fputc((work & 15) ,fp2);
								work &= ~1;
								fputc((work & 15) ,fp);
							}
							else
							{
								unsigned char work;
								work = 0x3d;
								fputc( i & (work & 15),fp);
								fputc( i & ((work >> 4) & 15) ,fp2);
							}
							break;
						}
						case kD3ALUOp_Clv:
						{
							if (inFlags == 0)
							{
								unsigned char work = i;
								fputc((work & 15) ,fp);
								work &= ~(1<<2);	// Bit 6 - 4 = 2 because of the hi nibble
								fputc( ((work >> 4) & 15) ,fp2);
							}
							else
							{
								unsigned char work;
								work = 0xc0;
								fputc( i & (work & 15),fp);
								fputc( i & ((work >> 4) & 15) ,fp2);
							}
							break;
						}
						case kD3ALUOp_Flags:
						{
							unsigned char work = i,flags;
							flags = PreserveCarryFlag(inFlags) | CalculateZeroFlag(work) | CalculateNegativeFlag(work);
							flags |= PreserveOverflowFlag(inFlags);
							fputc(work | flags,fp);
							fputc(work | flags,fp2);
							break;
						}
						default:
						{
							fputc(0,fp);
							fputc(0,fp2);
							break;
						}
					}
				}
			}
		}
	}
	fclose(fp);
	fclose(fp2);

	return 0;
}
