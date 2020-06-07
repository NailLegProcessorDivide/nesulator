#include <stdint.h>
#include <iostream>
#include <chrono>
#include "graphics.h"
#include <thread>

//#define _CRT_SECURE_NO_WARNINGS

typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t   u8;
typedef int8_t   s8;
u8 zero = 0; // zero so i can set a pointer
template<unsigned bitno, unsigned nbits = 1, typename T = u8>
struct RegBit //regbit copy pasta'd from bisquit's nes emulator
{
	T data;
	enum { mask = (1u << nbits) - 1u };
	template<typename T2>
	RegBit& operator=(T2 val)
	{
		data = (data & ~(mask << bitno)) | ((nbits > 1 ? val & mask : !!val) << bitno);
		return *this;
	}
	template<typename T2>
	bool operator==(T2 val)//my == op for comprison
	{
		
		return ((data & mask) >> bitno) == val;
	}
	operator unsigned() const { return (data >> bitno) & mask; }
	RegBit& operator++ () { return *this = *this + 1; }
	unsigned operator++ (int) { unsigned r = *this; ++*this; return r; }
};

namespace CPU6502 {
	// CPU registers:
	u16 PC = 0x0000;//c000
	u8 A = 0;
	u8 X = 0;
	u8 Y = 0;
	u8 S = 0xFD;
	//u8 FLAGS = 0; // 0xbNV--DIZC -- bisquit didn't implement the B register - not sure what it is bits may be out of order someone help i have no clue what i am doing
	union {
		u8 FLAGS;
		RegBit<7, 1, u8>N; //N negative
		RegBit<6, 1, u8>V; //V overflow
		RegBit<5, 1, u8>BLANK; // unused always 1 but not set yet
		RegBit<4, 1, u8>B;// B brk flag
		RegBit<3, 1, u8>D;// D decimal mode (unsupported on NES, but flag exists)
		RegBit<2, 1, u8>I;// I interrupt enable/disable
		RegBit<1, 1, u8>Z;// Z zero
		RegBit<0, 1, u8>C;// C carry
	}FLAGS;
	u8 interupts = 0;//NMI = 0x1, BRK = 0x2, IRQ = 0x4, RST = 0x8
	u16 VECTOR_NMI = 0xFFFA;
	u16 VECTOR_RESET = 0xFFFC;
	u16 VECTOR_IRQ = 0xFFFE;
}
namespace PPU{
	uint8_t writeno = 0;
	u8 xoffset;
	u8 yoffset;
	u8 inline read(u8 address) {

	}


	union //PPUCTRL (write)
	{
		u8 PPUCTRL;
		RegBit<0, 8, u8> sysctrl;
		RegBit<0, 2, u8> BaseNTA;
		RegBit<2, 1, u8> Inc;
		RegBit<3, 1, u8> SPaddr;
		RegBit<4, 1, u8> BGaddr;
		RegBit<5, 1, u8> SPsize;
		RegBit<6, 1, u8> SlaveFlag;
		RegBit<7, 1, u8> NMIenabled;
	} PPUCTRL;

	union //PPUMASK (write)
	{
		u8 PPUMASK;
		RegBit< 0, 8, u8> dispctrl;
		RegBit< 0, 1, u8> Grayscale;
		RegBit< 1, 1, u8> ShowBG8;
		RegBit< 2, 1, u8> ShowSP8;

		RegBit< 3, 1, u8> ShowBG;
		RegBit< 4, 1, u8> ShowSP;
		RegBit< 3, 2, u8> ShowBGSP;

		RegBit< 5, 1, u8> EmpR;
		RegBit< 6, 1, u8> EmpG;
		RegBit< 7, 1, u8> EmpB;
		RegBit< 5, 3, u8> EmpRGB;
	} PPUMASK;

	union //status (read) only top 3 bits have values rest is junk from last write(should maybe implement junk but cba)
	{
		u8 status;
		RegBit<5, 1, u8> SPoverflow;
		RegBit<6, 1, u8> SP0hit;
		RegBit<7, 1, u8> InVBlank;
	} status;

	u8 OAMaddr;

	u8 VRAM[0x800];
	u8 colourpalate[0x20];
	u8* nt20;
	u8* nt24;
	u8* nt28;
	u8* nt2c;

	union objattrib{
		u32 objdata;
		RegBit<0, 8, u32> y;
		RegBit<8, 8, u32> tileIndex;
		RegBit<16, 8, u32> attribs;
		RegBit<24, 8, u32> x;
	};

	union {
		u8 data[256];
		objattrib objdata[64];
	}OAM;

	union {
		u8 data[32];
		objattrib objdata[8];
	} secondaryOAM;//temp sprites per scan line
}
u8 RAM[0x800];
u8 controller1;
u8 controller2;

namespace ROM {
	struct rom{
		u8* BANKS;
		u8* activeBanks[4];
		int bankSize = 0x4000;
		int bankCount;

		u8* vromBanks;
		u8* activeVpage;
		int vBankCount;
		int vBankSize = 0x2000;
	}ROM;
	int load(FILE* file) {
		uint8_t nesstring[4] = { 0 };
		uint8_t dump[16] = { 0 };

		fread(nesstring, 4, 1, file);
		if (memcmp(nesstring, "NES", 3))
		{
			return -1;
		}
		try {
			fread(&ROM.bankCount, 1, 1, file);
			ROM.BANKS = new u8[ROM.bankSize * ROM.bankCount]; 
			fread(&ROM.vBankCount, 1, 1, file);
			ROM.vromBanks = new u8[ROM.vBankSize * ROM.vBankCount];
		}
		catch (std::bad_alloc& ba) {
			printf("error alocating rom, %i bytes\n", ROM.bankCount);
			return -1;
		}
		union {
			u8 f6;
			RegBit<0, 1, u8> mirror;//0 = h, 1 = v
			RegBit<1, 1, u8> vRamPresent;
			RegBit<2, 1, u8> trainerPresent;
			RegBit<3, 1, u8> ignoreMirror;//1 = quadscreens// ignored by me
			RegBit<4, 4, u8> mapperlowNibble;
		}f6;
		fread(&f6.f6, 1, 1, file);

		if (f6.mirror == 0) {
			PPU::nt20 = PPU::VRAM;
			PPU::nt24 = PPU::VRAM;
			PPU::nt28 = PPU::VRAM+1024;
			PPU::nt2c = PPU::VRAM+1024;
		}
		else {
			PPU::nt20 = PPU::VRAM;
			PPU::nt24 = PPU::VRAM + 1024;
			PPU::nt28 = PPU::VRAM ;
			PPU::nt2c = PPU::VRAM + 1024;
		}
		fread(dump, 1, 9, file);

		printf("%i prg rom, %i chr ram\n", ROM.bankCount, ROM.vBankCount);
		for (int i = 0; i < ROM.bankCount * ROM.bankSize; i++) {
			ROM.BANKS[i] = fgetc(file);
		}
		for (int i = 0; i < ROM.vBankCount * ROM.vBankSize; i++) {
			ROM.vromBanks[i] = fgetc(file);
		}
		if (ROM.bankCount == 1) {
			ROM.activeBanks[0] = ROM.BANKS;
			ROM.activeBanks[1] = ROM.BANKS;
			ROM.activeBanks[2] = ROM.BANKS;
			ROM.activeBanks[3] = ROM.BANKS;
		}
		else if (ROM.bankCount == 2) {
			ROM.activeBanks[0] = ROM.BANKS;
			ROM.activeBanks[1] = ROM.BANKS + 0x4000;
			ROM.activeBanks[2] = ROM.BANKS;
			ROM.activeBanks[3] = ROM.BANKS + 0x4000;
		}
		else {
			printf("unable to load %i rom banks\n", ROM.bankCount);
			return -1;
		}

		fseek(file, 16, SEEK_SET);
		return 0;
	}

	int load(const char *file) {//wrapper function in an attempt to ensure file closes
		FILE* fRom = fopen(file, "rb");
		int res = load(fRom);
		fclose(fRom);
		return res;
	}
}
namespace PPU {

	void write(u8 val) {
		if (OAMaddr < 0x2000) {//tile rom(not implemented a ram)
		}
		else if (OAMaddr < 0x2400) {
			nt20[OAMaddr - 0x2000] = val;
		}
		else if (OAMaddr < 0x2800) {
			nt24[OAMaddr - 0x2400] = val;
		}
		else if (OAMaddr < 0x2C00) {
			nt28[OAMaddr - 0x2800] = val;
		}
		else if (OAMaddr < 0x3000) {
			nt2c[OAMaddr - 0x2C00] = val;
		}
		else if (OAMaddr < 0x3400) {//mirror of above
			nt20[OAMaddr - 0x3000] = val;
		}
		else if (OAMaddr < 0x3800) {
			nt24[OAMaddr - 0x3400] = val;
		}
		else if (OAMaddr < 0x3C00) {
			nt28[OAMaddr - 0x3800] = val;
		}
		else if (OAMaddr < 0x3900) {
			nt2c[OAMaddr - 0x3C00] = val;
		}
		else if (OAMaddr < 0x4000) {
			colourpalate[OAMaddr % 0x20] = val;
		}
		OAMaddr++;

	}
	u8 read() {
		if (OAMaddr < 0x2000) {//tile rom(not implemented a ram)
			return ROM::ROM.activeVpage[OAMaddr%ROM::ROM.vBankSize];
		}
		else if (OAMaddr < 0x2400) {
			return nt20[OAMaddr - 0x2000];
		}
		else if (OAMaddr < 0x2800) {
			return nt24[OAMaddr - 0x2400];
		}
		else if (OAMaddr < 0x2C00) {
			return nt28[OAMaddr - 0x2800];
		}
		else if (OAMaddr < 0x3000) {
			return nt2c[OAMaddr - 0x2C00];
		}
		else if (OAMaddr < 0x3400) {//mirror of above
			return nt20[OAMaddr - 0x3000];
		}
		else if (OAMaddr < 0x3800) {
			return nt24[OAMaddr - 0x3400];
		}
		else if (OAMaddr < 0x3C00) {
			return nt28[OAMaddr - 0x3800];
		}
		else if (OAMaddr < 0x3900) {
			return nt2c[OAMaddr - 0x3C00];
		}
		else if (OAMaddr < 0x4000) {
			return colourpalate[OAMaddr % 0x20];
		}
		return 0;
	}
	u8 *readptr() {
		if (OAMaddr < 0x2000) {//tile rom(not implemented a ram)
			return ROM::ROM.activeVpage + (OAMaddr % ROM::ROM.vBankSize);
		}
		else if (OAMaddr < 0x2400) {
			return nt20 + (OAMaddr - 0x2000);
		}
		else if (OAMaddr < 0x2800) {
			return nt24 + (OAMaddr - 0x2400);
		}
		else if (OAMaddr < 0x2C00) {
			return nt28 + (OAMaddr - 0x2800);
		}
		else if (OAMaddr < 0x3000) {
			return nt2c + (OAMaddr - 0x2C00);
		}
		else if (OAMaddr < 0x3400) {//mirror of above
			return nt20 + (OAMaddr - 0x3000);
		}
		else if (OAMaddr < 0x3800) {
			return nt24 + (OAMaddr - 0x3400);
		}
		else if (OAMaddr < 0x3C00) {
			return nt28 + (OAMaddr - 0x3800);
		}
		else if (OAMaddr < 0x3900) {
			return nt2c + (OAMaddr - 0x3C00);
		}
		else if (OAMaddr < 0x4000) {
			return colourpalate + (OAMaddr % 0x20);
		}
		if (1 == PPUCTRL.Inc) {
			OAMaddr += 32;
		}
		else {
			OAMaddr += 1;
		}
		return &zero;
	}

	void testNMI() {
		if (1==status.InVBlank && 1==PPUCTRL.NMIenabled) {
			printf("nmi, %X %i", status.InVBlank.data, 0 == PPUCTRL.NMIenabled);
			CPU6502::interupts |= 1;
		}
		//CPU6502::interupts |= status.InVBlank==1 && PPUCTRL.NMIenabled==1;
	}

	u16 line = 0;
	u16 pixel = 0;

	u8 ctile;
	u8 rowmod;
	int tx;
	int ty;

	void step() {
		pixel++;
		if (pixel > 340) {
			pixel = 0;
			line = (line + 1) % 260;
		}
		if (line < 240) {
			if (pixel == 0) {
				rowmod = xoffset & 7;
				tx = xoffset >> 4;
				ty = yoffset >> 4;
				ctile = nt20[tx + ty * 8];
			}
			else if (pixel < 257) {
				setpixel(pixel - 1, line, ctile);
				if (pixel % 7 == rowmod) {
					ctile = nt20[(++tx) + ty * 8];
				}

			}
		}
		if (line == 240 && pixel == 0) {
			status.InVBlank = 1;
			printf("invblank, a %i\n", CPU6502::A);
			testNMI();
		}
	}
}

namespace CPU6502 {

	u8 oamdmaaddr = 0;
	void pass() {}
	void (*afterfun)(void) = &pass;

	u8* readpointer(u16);

	void oamdma() {
		for (int i = 0; i < 64; i++) {//copy 256 bytes 32 bits at a time
			printf("oam\n");
			PPU::OAM.objdata[i].objdata = *(u32*)readpointer((oamdmaaddr << 8) + i * 4);
		}
		afterfun = pass;
		//clocks += 520;//should take roughly 500 clocks but that is hard to write
	}
	void resetvblankStatus() {
		PPU::status.InVBlank = 0;
		afterfun = pass;
	}

	u8 read(u16 address) {

	}



	u8 read(u16 address) {
		if (address < 0x2000) return RAM[address & 0x7FF];
		else if (address < 0x4000) {
			return 0;
		}
		else if (address < 0x4018) {
			switch (address & 0x1F)
			{
			case 0x02:
				return PPU::status.status;
				break;
			case 0x04:
				printf("ppu access");
				return PPU::OAM.data[PPU::OAMaddr];
			case 0x14: //###### copy data here ######// OAM DMA: Copy 256 bytes from RAM into PPU's sprite memory
				return 0;
			case 0x15: break;//audio stuff
			case 0x16: return controller1; break;
			case 0x17: return controller2; break;
				//default: ROM.BANKS[(address / ROM.bankSize) % ROM.bankCount] [address % ROM.bankSize];
			default:
				return 0;
			}
		}
		//printf("a%i\n", ROM::ROM.activeBanks[(address >> 13) & 3]);
		//printf("v%X\n", *ROM::ROM.activeBanks[(address >> 13) & 3]);
		return ROM::ROM.activeBanks[address >> 13 & 3][address & 0x1fff];
	}
	u8* readpointer(u16 address) {
		if (address < 0x2000) return RAM + (address & 0x7FF);
		if (address < 0x4000) {
			switch (address & 0x7)
			{
			case 0x00:
				return &PPU::PPUCTRL.PPUCTRL;
			case 0x01:
				return &PPU::PPUMASK.PPUMASK;
			case 0x02:
				PPU::writeno = 0;
				//afterfun = resetvblankStatus;
				return &PPU::status.status;
			case 0x03:
				return &PPU::OAMaddr;
			case 0x04:
				return PPU::OAM.data + PPU::OAMaddr;
			case 0x05:
				if ((PPU::writeno++) & 1) {
					return &PPU::xoffset;
				}
				else {
					return &PPU::yoffset;
				}
			case 0x06:
				if ((PPU::writeno++) & 1) {
					return &PPU::OAMaddr+1;//should be high byte
				}
				else {
					return &PPU::OAMaddr;//should be low byte
				}
			case 0x07:
				return PPU::readptr();
			default:
				return &zero;
			}
		}
		else if (address < 0x4018) {
			switch (address & 0x17)
			{
			case 0x14: //###### copy data here ######// OAM DMA: Copy 256 bytes from RAM into PPU's sprite memory
				afterfun = oamdma;
				return &oamdmaaddr;
			case 0x15: break;//audio stuff
			case 0x16: return &controller1; break;
			case 0x17: return &controller2; break;
			default:
				return &zero;
			}
		}
		int bank = (address & 0x7FFF) / ROM::ROM.bankSize;
		int offset = address % ROM::ROM.bankSize;
		return ROM::ROM.activeBanks[bank] + offset;
	}

	void r(u8* ptr) {//im a lazy sod deal with this mess
		if (ptr == &PPU::status.status) {
			PPU::status.status = 0;
		}
	}
	
	
	/*
	void write(u16 address, u8 value) {
		if (address < 0x2000) RAM[address & 0x7FF] = value;
		if (address < 0x4000) {
		}
		else if (address < 0x4018) {
			switch (address & 0x1F) {
			case 0x00:
				PPU::PPUCTRL.PPUCTRL = value;
				break;
			case 0x01:
				PPU::PPUMASK.PPUMASK = value;
				break;
			case 0x03:
				PPU::OAMaddr = value;
				break;
			case 0x04:
				PPU::write(value);
				//PPU::OAM.data [PPU::OAMaddr++] = value;
				break;
			case 0x14:
				for (int i = 0; i < 64; i++) {//copy 256 bytes 32 bits at a time
					PPU::OAM.objdata[i].objdata = *(u32*)readpointer((((u16)value) << 8) + i * 4);
				}
				break;
			}
		}
	}*/
	void inline push(u8 v) {
		RAM[S--] = v;
	}
	u8 inline pop() {
		return RAM[++S];
	}

	u8 inline *abs() {
		printf("abs\n");
		//u16 addr = (read(PC++) | (read(PC++) << 8));
		//printf("abs %4X, %2X\n", addr, *readpointer(addr));
		u8* ret = readpointer(*(u16*)readpointer(PC));
		PC += 2;
		return ret;

	}
	u8 inline *acc() {
		printf("acc\n");
		return &A;
	}
	u8 inline *absx() {
		printf("absx\n");
		PC += 2;
		return readpointer(PC - 2) + X;
	}
	u8 inline *absy() {
		printf("absy\n");
		PC += 2;
		return readpointer(PC - 2) + Y;
	}
	u8 inline *imm() {
		printf("imm\n");
		return readpointer(PC++);
	}
	u8 inline *ind() {//on amd64 or x86 it shares
		printf("ind");
		PC += 2;
		return readpointer(*(u8*)readpointer(*(u16*)readpointer(PC - 2)));
	}
	u8 inline *xind() {//on amd64 or x86 it shares
		printf("xind\n");
		//u16 address = (read(PC++) << 8) | (read(PC++)) + X;
		return RAM+(u8)(*readpointer(PC++)+X);
	}
	u8 inline *indy() {//on amd64 or x86 it shares
		printf("indy\n");
		printf("indy");
		//u16 address = (read(PC++) << 8) | (read(PC++));
		return RAM + (u8)(*readpointer(PC++)+Y);
	}
	u8 inline *zpg() {//on amd64 or x86 it shares
		printf("zpg\n");
		return RAM + read(PC++);
	}
	u8 inline *zpgx() {//on amd64 or x86 it shares
		printf("zpgx\n");
		return RAM + (u8)(read(PC++) + (s8)X);
	}
	u8 inline *zpgy() {//on amd64 or x86 it shares
		printf("zpgy\n");
		return RAM + (u8)(read(PC++) + (s8)Y);
	}
	u16 inline rel() {
		printf("rel\n");
		return (s8)read(PC++) + PC;
	}

	void donz(u8 in) {
		FLAGS.Z = in?0:1;
		FLAGS.N = in >> 7;
		if (1 == FLAGS.N) {
			printf("n");
		}
	}
	void interupt() {
		//FLAGS.B = 0;
		CPU6502::push(CPU6502::PC >> 8);
		CPU6502::push(CPU6502::PC & 0xf);
		CPU6502::push(CPU6502::FLAGS.FLAGS);
		printf("interupt %X, PC %X\n", interupts, CPU6502::PC);
		if (interupts & 8) {
			PC = *(u16*)CPU6502::readpointer(CPU6502::VECTOR_RESET);
			//return;
		}
		else if (interupts & 1) {
			PC = *(u16*)CPU6502::readpointer(CPU6502::VECTOR_NMI);
			//return;
		}
		else/* if (interupts & 6)*/ {
			PC = *(u16*)CPU6502::readpointer(CPU6502::VECTOR_IRQ);
			//PC = (read(0xFFFF) << 8) | read(0xFFFE);
			//return;
		}
		if (PC < 0x4000) {
			printf("%i pc has weird value.", PC);
			while (true);
		}
		printf("pc %x\n", CPU6502::PC);
		interupts = 0;
	}

	template <int clockcycles>
	int nop() {
		if (clockcycles == 0) {
			printf("nop or undefined at %X\n", PC);
			getchar();
		}
		return clockcycles;
	}
	template <int clockcycles>
	int BRK() {
		printf("break PC %X\n", PC);
		interupts |= 2;
		//push(PC & 0xf);
		//push(PC >> 8);
		//push(FLAGS.FLAGS);
		FLAGS.B = 1;
		return clockcycles;
		//exit(1);
	}//7 cycles ----------------IMPLEMENT THIS WHEN I KNOW WHAT IT DOES---------------------------
	template <int clockcycles>
	int PHP() {
		printf("php ");
		push(FLAGS.FLAGS);
		return clockcycles;
	}//3 cycles
	template<u16(*f)(void), int clockcycles>
	int BPL() {
		printf("bpl ");
		u16 loc = f();
		if (0==FLAGS.N)PC = loc;
		return clockcycles;
	}//2+(1 or 2 - depending on if in block or not) cycles
	template <int clockcycles>
	int CLC() {
		printf("clc ");
		FLAGS.C = 0;
		return clockcycles;
	}// 2cycles
	template<u8*(*f)(void), int clockcycles>
	int ORA() {
		printf("ora ");
		u8* v = f();
		A |= *v;
		donz(*v);
		return clockcycles;
	}//4+1 cycles
	template<u8*(*f)(void), int clockcycles>
	int ASL() {
		printf("asl ");
		u8* v = f();
		*v <<= 1;
		donz(*v);
		return clockcycles;
	}// 7 cycles
	template<u8*(*f)(void), int clockcycles>
	int JSR() {
		printf("jsr ");
		u16* v = (u16*)f();//not sure if this gets a 16 bit value llhh
		PC += 2;
		push(PC & 0xf);
		push(PC >> 8);
		PC = *v;
		return clockcycles;
	}// 6 cycles
	template <int clockcycles>
	int PLP() {
		printf("plp ");
		FLAGS.FLAGS = pop();
		return clockcycles;
	}//4 cycles
	template<u8*(*f)(void), int clockcycles>
	int BIT() {
		printf("bit ");
		u8 res = A & (*f());

		if (res == 0) { FLAGS.Z = 1; }
		else { FLAGS.Z = 0; }
		FLAGS.FLAGS &= 0x3f;
		FLAGS.FLAGS |= (0x3f & res);
		return clockcycles;
	}//4 cycles

	template <int clockcycles>
	int BMI() {
		printf("bmi ");
		s8 dist = read(PC++);
		if (FLAGS.N=1) PC += dist;

		if (PC < 0x4000) {
			printf("%i pc has weird value.", PC);
			while (true);
		}
		return clockcycles;
	}
	template <int clockcycles>
	int SEC() {
		printf("sec ");
		FLAGS.C = 1;
		return clockcycles;
	}
	template<u8*(*f)(void), int clockcycles>
	int AND() {
		printf("and ");
		u8* v = f();
		A &= *v;
		donz(*v);
		return clockcycles;
	}
	template<u8*(*f)(void), int clockcycles>
	int ROL() {
		printf("rol ");
		u8* v = f();
		*v = (*v << 1) | (*v >> 7);
		donz(*v);
		return clockcycles;
	}
	template <int clockcycles>
	int RTI() {
		printf("rti ");
		//printf("rti###################################################");
		FLAGS.FLAGS = pop();
		PC = (pop() << 8) | pop();
		return clockcycles;
	}
	template <int clockcycles>
	int PHA() {
		printf("pha ");
		push(A);
		return clockcycles;
	}
	template<u8*(*f)(void), int clockcycles>
	int JMP() {
		printf("jmp ");
		u16* v = (u16*)f();
		PC = *v;

		if (PC < 0x4000) {
			printf("%i pc has weird value.", PC);
			while (true);
		}
		return clockcycles;
	}
	template<u16(*f)(void), int clockcycles>
	int BVC() {
		printf("bvc ");
		u16 add = f();
		if (FLAGS.V == 0) PC = add;

		if (PC < 0x4000) {
			printf("%i pc has weird value.", PC);
			while (true);
		}
		return clockcycles;
	}
	template<u8*(*f)(void), int clockcycles>
	int LSR() {
		printf("lsr ");
		u8* v = f();
		*v >>= 1;
		return clockcycles;
	}
	template <int clockcycles>
	int CLI() {
		printf("cli ");
		FLAGS.I = 0;
		return clockcycles;
	}
	template<u8*(*f)(void), int clockcycles>
	int EOR() {
		printf("eor ");
		u8* v = f();
		A ^= *v;
		return clockcycles;
	}
	template <int clockcycles>
	int RTS() {
		printf("rts ");
		PC = pop() + 1;
		return clockcycles;
	}
	template<u8*(*f)(void), int clockcycles>
	int ADC() {
		printf("adc ");
		u8* v = f();
		u16 total = *v + FLAGS.C + A;
		A = (u8)total;
		r(v);
		//printf("%ddi", ((total >> 8) & 1));
		FLAGS.C = (total >> 8);//total should be no more than 511 so this is 0 for no carry and 1 for carry
		donz(A);
		return clockcycles;
	}
	template<u8*(*f)(void), int clockcycles>
	int ROR() {
		printf("ror ");
		u8* v = f();
		*v = (*v >> 1) | (*v << 7);
		donz(A);
		FLAGS.C = *v & 1;
		return clockcycles;
	}
	template <int clockcycles>
	int PLA() {
		printf("pla ");
		A = pop();
		return clockcycles;
	}
	template<u16(*f)(void), int clockcycles>
	int BVS() {
		printf("bvs ");
		u16 add = f();
		if (FLAGS.V == 1) PC = add;

		if (PC < 0x4000) {
			printf("%i pc has weird value.", PC);
			while (true);
		}
		return clockcycles;
	}
	template <int clockcycles>
	int SEI() {
		printf("sei ");
		FLAGS.I = 1;
		return clockcycles;
	}
	template<u8*(*f)(void), int clockcycles>
	int STA() {
		printf("sta ");
		u8* v = f();
		*v = A;
		return clockcycles;
	}
	template<u8*(*f)(void), int clockcycles>
	int STX() {
		printf("stx ");
		u8* v = f();
		*v = X;
		return clockcycles;
	}
	template<u8*(*f)(void), int clockcycles>
	int STY() {
		printf("sty ");
		u8* v = f();
		*v = Y;
		return clockcycles;
	}
	template <int clockcycles>
	int DEY() {
		printf("sey ");
		Y--;
		return clockcycles;
	}
	template <int clockcycles>
	int TXA() {
		printf("txa ");
		A = X;
		return clockcycles;
	}
	template <int clockcycles>
	int TYA() {
		printf("tya ");
		A = Y;
		return clockcycles;
	}
	template <int clockcycles>
	int TXS() {
		printf("txs ");
		S = X;
		return clockcycles;
	}
	template<u16(*f)(void), int clockcycles>
	int BCC() {
		printf("bcc ");
		u16 add = f();
		if (FLAGS.C == 0) { PC = add; }

		if (PC < 0x4000) {
			printf("%i pc has weird value.", PC);
			while (true);
		}
		return clockcycles;
	}
	template<u8*(*f)(void), int clockcycles>
	int LDY() {
		printf("ldy ");
		Y = *f();
		return clockcycles;
	}
	template<u8*(*f)(void), int clockcycles>
	int LDA() {
		printf("lda ");
		u8* v = f();
		A = *v;
		r(v);
		donz(A);
		return clockcycles;
	}
	template<u8*(*f)(void), int clockcycles>
	int LDX() {
		printf("ldx ");
		X = *f();
		return clockcycles;
	}
	template <int clockcycles>
	int TAY() {
		printf("tay ");
		Y = A;
		return clockcycles;
	}
	template <int clockcycles>
	int CLV() {
		printf("clv ");
		FLAGS.V = 0;
		return clockcycles;
	}
	template <int clockcycles>
	int TAX() {
		printf("tax ");
		X = A;
		return clockcycles;
	}
	template <int clockcycles>
	int TSX() {
		printf("tsx");
		X = S;
		return clockcycles;
	}
	template<u16(*f)(void), int clockcycles>
	int BCS() {
		printf("bcs ");
		u16 add = f();
		if (1 == FLAGS.C) { PC = add; }

		if (PC < 0x4000) {
			printf("%i pc has weird value.", PC);
			while (true);
		}
		return clockcycles;
	}
	template<u8*(*f)(void), int clockcycles>
	int CMP() {
		printf("cmp ");
		u8 testval = *f();
		u16 v = A - testval+0x100;
		printf("A: %i, T: %i\n", A, testval);
		FLAGS.C = (v>>8)&1;
		v -= 0x100;
		donz(v);
		//if (v == 0) FLAGS.Z = 1;
		//else FLAGS.Z = 0;
		//FLAGS.N = (v>>7)&1;
		return clockcycles;
	}
	template<u16(*f)(void), int clockcycles>
	int BNE() {
		printf("bne ");
		u16 add = f();
		if (0 == FLAGS.Z) { PC = add; }
		if (Y==255) {
		}
		if (PC < 0x4000) {
			printf("%i pc has weird value.", PC);
			while (true);
		}
		return clockcycles;
	}
	template<u8*(*f)(void), int clockcycles>
	int CPY() {
		printf("cpy ");
		u16 v = Y - *f();
		FLAGS.C = (v >> 8) & 1;
		donz(v);
		return clockcycles;
	}
	template<u8*(*f)(void), int clockcycles>
	int DEC() {
		printf("dec ");
		(*f())--;
		return clockcycles;
	}
	template <int clockcycles>
	int DEX() {
		printf("dex ");
		X--;
		return clockcycles;
	}
	template <int clockcycles>
	int INY() {
		printf("iny ");
		Y++;
		return clockcycles;
	}
	template <int clockcycles>
	int CLD() {
		printf("cld");
		FLAGS.D = 0;
		return clockcycles;
	}
	template<u8*(*f)(void), int clockcycles>
	int CPX() {
		printf("cpx ");
		u16 v = X - *f();
		FLAGS.C = (v >> 8) & 1;
		if (v == 0) FLAGS.Z = 1;
		else FLAGS.Z = 0;
		FLAGS.N = (v >> 7) & 1;
		return clockcycles;
	}
	template<u8*(*f)(void), int clockcycles>
	int SBC() {
		printf("sbc ");
		u8* v = f();
		A -= *v - FLAGS.C;
		return clockcycles;
	}
	template<u8*(*f)(void), int clockcycles>
	int INC() {
		printf("inc ");
		(*f())++;
		return clockcycles;
	}
	template<u16(*f)(void), int clockcycles>
	int BEQ() {
		printf("beq ");
		u16 add = f();
		if (FLAGS.Z == 1) { PC = add; }

		if (PC < 0x4000) {
			printf("%i pc has weird value.", PC);
			while (true);
		}
		return clockcycles;
	}
	template <int clockcycles>
	int INX() {
		printf("inx ");
		X++;
		return clockcycles;
	}
	template <int clockcycles>
	int SED() {
		printf("sed ");
		FLAGS.D = 1;
		return clockcycles;
	}
	template<u8*(*f)(void), int clockcycles>
	int print() {
		printf("print: %hhx\n", *f());
		return clockcycles;
	}
	typedef int(*op)(void);
	static op opmap[] = {
		//  0          , 1           , 2          , 3     , 4           , 5           , 6           , 7     , 8     , 9           , A          , B     , C           , D           , E           , F
			BRK<7>     , ORA<xind, 6>, nop<0>     , nop<0>, nop<0>      , ORA<zpg, 3> , ASL<zpg, 5> , nop<0>, PHP<3>, ORA<imm, 2> , ASL<acc, 2>, nop<0>, nop<0>      , ORA<abs, 4> , ASL<abs, 6> , nop<0>,//0
			BPL<rel, 2>, ORA<indy, 5>, nop<0>     , nop<0>, nop<0>      , ORA<zpgx, 4>, ASL<zpgx, 6>, nop<0>, CLC<2>, ORA<absy, 4>, nop<0>     , nop<0>, nop<0>      , ORA<absx, 4>, ASL<absx, 7>, nop<0>,//1
/*jsr==abs*/JSR<imm, 6>, AND<xind, 6>, nop<0>     , nop<0>, BIT<zpg, 3> , AND<zpg, 3> , ROL<zpg, 5> , nop<0>, PLP<4>, AND<imm, 2> , ROL<acc, 2>, nop<0>, BIT<abs, 4> , AND<abs, 4> , ROL<abs, 6> , nop<0>,//2
			BMI<2>     , AND<indy, 5>, nop<0>     , nop<0>, nop<0>      , AND<zpgx, 4>, ROL<zpgx, 6>, nop<0>, SEC<2>, AND<absy, 4>, nop<0>     , nop<0>, nop<0>      , AND<absx, 4>, ROL<absx, 7>, nop<0>,//3
			RTI<6>     , EOR<xind, 6>, nop<0>     , nop<0>, nop<0>      , EOR<zpg, 3> , LSR<zpg, 5> , nop<0>, PHA<3>, EOR<imm, 2> , LSR<acc, 2>, nop<0>, JMP<imm, 3> , EOR<abs, 4> , LSR<abs, 6> , nop<0>,//4
			BVC<rel, 2>, EOR<indy, 5>, nop<0>     , nop<0>, nop<0>      , EOR<zpgx, 4>, LSR<zpgx, 6>, nop<0>, CLI<2>, EOR<absy, 4>, nop<0>     , nop<0>, nop<0>      , EOR<absx, 4>, LSR<absx, 7>, nop<0>,//5
			RTS<6>     , ADC<xind, 6>, nop<0>     , nop<0>, nop<0>      , ADC<zpg, 3> , ROR<zpg, 5> , nop<0>, PLA<4>, ADC<imm, 2> , ROR<acc, 2>, nop<0>, JMP<abs, 5> , ADC<abs, 4> , ROR<abs, 6> , nop<0>,//6
			BVS<rel, 2>, ADC<indy, 5>, nop<0>     , nop<0>, nop<0>      , ADC<zpgx, 4>, ROR<zpgx, 6>, nop<0>, SEI<2>, ADC<absy, 4>, nop<0>     , nop<0>, nop<0>      , ADC<absx, 4>, ROR<absx, 7>, nop<0>,//7
			nop<0>     , STA<xind, 6>, nop<0>     , nop<0>, STY<zpg, 3> , STA<zpg, 3> , STX<zpg, 3> , nop<0>, DEY<2>, nop<0>      , TXA<2>     , nop<0>, STY<abs, 3> , STA<abs, 4> , STX<abs, 4> , nop<0>,//8
			BCC<rel, 2>, STA<indy, 6>, nop<0>     , nop<0>, STY<zpgx, 4>, STA<zpgx, 4>, STX<zpgx, 4>, nop<0>, TYA<2>, STA<absy, 5>, TXS<2>     , nop<0>, nop<0>      , STA<absx, 5>, nop<0>      , nop<0>,//9
			LDY<imm, 2>, LDA<xind, 6>, LDX<imm, 2>, nop<0>, LDY<zpg, 3> , LDA<zpg, 3> , LDX<zpg, 3> , nop<0>, TAY<2>, LDA<imm, 2> , TAX<2>     , nop<0>, LDY<abs, 4> , LDA<abs, 4> , LDX<abs, 4> , nop<0>,//a
			BCS<rel, 2>, LDA<indy, 5>, nop<0>     , nop<0>, LDY<zpgx, 4>, LDA<zpgx, 4>, LDX<zpgx, 4>, nop<0>, CLV<2>, LDA<absy, 4>, TSX<2>     , nop<0>, LDY<absx, 4>, LDA<absx, 4>, LDX<absx, 4>, nop<0>,//b
			CPY<imm, 2>, CMP<xind, 6>, nop<0>     , nop<0>, CPY<zpg, 4> , CMP<zpg, 3> , DEC<zpg, 5> , nop<0>, INY<2>, CMP<imm, 2> , DEX<2>     , nop<0>, CPY<abs, 4> , CMP<abs, 4> , DEC<abs, 3> , nop<0>,//c
			BNE<rel, 2>, CMP<indy, 5>, nop<0>     , nop<0>, nop<0>      , CMP<zpgx, 4>, DEC<zpgx, 6>, nop<0>, CLD<2>, CMP<absy, 4>, nop<0>     , nop<0>, nop<0>      , CMP<absx, 4>, DEC<absx, 7>, nop<0>,//d
			CPX<imm, 2>, SBC<xind, 6>, nop<0>     , nop<0>, CPX<zpg, 3> , SBC<zpg, 3> , INC<zpg, 5> , nop<0>, INX<2>, SBC<imm, 2> , nop<2>     , nop<0>, CPX<abs, 4> , SBC<abs, 4> , INC<abs, 6> , nop<0>,//e
			BEQ<rel, 2>, SBC<indy, 5>, nop<0>     , nop<0>, nop<0>      , SBC<zpgx, 4>, INC<zpgx, 6>, nop<0>, CLV<2>, SBC<absy, 4>, nop<0>     , nop<0>, nop<0>      , SBC<absx, 4>, INC<absx, 7>, nop<0>,//f
	};


	uint64_t tclocks = 0;
	void clockCPU() {
		int clocks = 0;
		if (interupts) {
			interupt();
		}
		u8 opcode = read(PC++);
		//printf("A: %2X\n", A);
		/*if (opcode != 16 && opcode != 173) {
			printf("pc %4X\n", PC);
			printf("op %2X\n", opcode);
			printf("\n");
		}*/
		//printf("opcode: %i\n", opcode);
		clocks += opmap[opcode]();
		//afterfun();
		for (int i = 0; i < clocks * 3; i++) {
			PPU::step();
		}
		tclocks += clocks;
	}
}

int main() {
	//PPU::PPUCTRL.NMIenabled = 1;
	//CPU6502::interupts = 8;
	//if (ROM::load("Super_mario_brothers.nes")) { while (true) {} };
	if (ROM::load("f:/nestest.nes")) { while (true) {} };
	setpointer(ROM::ROM.BANKS);//hacky graphics stuff;
	CPU6502::PC = 0xc000;//*(u16*)CPU6502::readpointer(CPU6502::VECTOR_RESET);
	RAM[0] = 0xA9;//load A immedeate - 2 cycles
	RAM[1] = 0xF0;//value 1
	RAM[2] = 0x65;//add with carry value in address in zero page - 2 cycles
	RAM[3] = 0x10;//address 0x10
	//RAM[2] = 0xff;//add with carry value in address in zero page - 2 cycles
	//RAM[3] = 0xff;//address 0x10
	RAM[4] = 0xFF;//hacky print - 1 cycle
	RAM[5] = 0x90;//Branch if no carry - 3 cycles
	RAM[6] = 0xfb;//address 2
	RAM[0x10] = 1;//value stored

	//std::thread t = std::thread(start);

	std::chrono::time_point<std::chrono::system_clock> now = std::chrono::system_clock::now();

	for (uint64_t i = 0; i < 200; i++) {//mean 3 cycles
		CPU6502::clockCPU();
	}

	std::chrono::time_point<std::chrono::system_clock> later = std::chrono::system_clock::now();

	std::chrono::duration<float> difference = later - now;

	float milliseconds = difference.count() * 1000;
	printf("%i clocks in %f ms\n", CPU6502::tclocks, milliseconds);
	printf("%f times speed\n", CPU6502::tclocks/1800.0f/milliseconds);
	setclose();
	//t.join();
	while (true) {}

	//programsaftywhatsthat:
	//clockCPU();
	//goto programsaftywhatsthat;
}