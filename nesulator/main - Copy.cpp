#include <stdint.h>


typedef uint_least32_t u32;
typedef uint_least16_t u16;
typedef uint_least8_t   u8;
typedef int_least8_t   s8;

// CPU registers:
u16 PC = 0xC000;
u8 A = 0;
u8 X = 0;
u8 Y = 0;
u8 S = 0;
/* Status flags: */
// C carry
// Z zero
// I interrupt enable/disable
// D decimal mode (unsupported on NES, but flag exists) 4,5 (0x10,0x20) don't exist
// V overflow
// N negative
u8 FLAGS = 0; // 0xbNV--DIZC -- bisquit didn't implement the B register - not sure what it is bits may be out of order someone help i have no clue what i am doing
bool reset = true;
bool nmi = false;
bool nmi_edge_detected = false;
bool intr = false;
u8 RAM[0x800];
u8 controller1;
u8 controller2;

u8 read(u16 address) {
	if(address < 0x2000) return RAM[address & 0x7FF];
	if (address < 0x4000) {
		return 0;
	}
	else if (address < 0x4018) {
		switch (address & 0x1F)
		{
		case 0x14: //###### copy data here ######// OAM DMA: Copy 256 bytes from RAM into PPU's sprite memory
			return 0;
		case 0x15: break;//audio stuff
		case 0x16: return controller1; break;
		case 0x17: return controller2; break;
		default: break;
		}
	}
}
void write(u16 address, u8 value) {
	if (address < 0x2000) RAM[address & 0x7FF] = value;
	if (address < 0x4000) {
	}
	else if (address < 0x4018) {
		if ((address & 0x1F) == 0x14)
		{

		}
	}
}
void inline push(u8 v) {
	RAM[S++] = v;
}
u8 inline pop() {
	return RAM[--S];
}

void nop() {}
void BRK() {}//7 cycles ----------------IMPLEMENT THIS WHEN I KNOW WHAT IT DOES---------------------------
void ORAindx() {
	u8 opoff = read(PC++);
	A |= read((u8)(read(X + opoff) | (read(X + opoff + 1) << 8)));
} //6 cycles
void ORAzpg() { A |= RAM[read(PC++)]; }//3 cycles
void ASLzpg() { RAM[read(PC++)]<<=1; }//5 cycles
void PHP() { write(S++, FLAGS); }//3 cycles
void ORAimm() { A |= read(PC++); }//2 cycles
void ASLA() { A <<= 1; }//2 cycles
void ORAabs() { 
	A |= read(read(PC++)|((read(PC++)<<8)));
}//4 cycles
void ASLabs() { 
	u16 add = read(PC++) | (read(PC++) << 8);
	write(add, read(add)<<1);
}//6 cycles
void BPLrel() { 
	s8 dist = read(PC++);
	if (0x70 ^ FLAGS)PC += dist;
}//2+(1 or 2 - depending on if in block or not) cycles
void ORAindy() { 
	u8 add = read(PC++);
	A |= read(read(add) | read(add + 1) << 8 + Y);
}//5+1 if pageboundary crossed
void ORAzpgx() { 
	A |= RAM[(s8)(X + read(PC++))];
}
void ASLzpgx() { 
	RAM[read((s8)X + PC++)] <<= 1;
}
void CLC() { 
	FLAGS &= 0xfe;
}// 2cycles
void ORAabsy() { 
	A |= read((read(PC++) | (read(PC++) << 8)) + Y);
}//4+1 cycles
void ORAabsx() { 
	A |= read((read(PC++) | (read(PC++) << 8)) + X);
}//4+1 cycles
void ASLabsx() { 
	u16 add = (read(PC++)+X) | ((read(PC++) << 8));
	write(add, read(add) << 1);
}// 7 cycles
void JSRabs() { 
	write(S++, PC & 0xf);
	write(S++, PC >> 8);
	PC = read(PC++) | (read(PC++) << 8);
}// 6 cycles
void ANDindx() { 
	u8 opoff = read(PC++);
	A &= read((u8)(read(X + opoff) | (read(X + opoff + 1)<<8)));
}//6 cycles
void BITzpg() { 
	u8 m = RAM[read(PC++)];
	FLAGS |= (0x3f & m)|(m&A?0:0x02); 
}//3 cycles
void ANDzpg() { 
	A &= RAM[read(PC++)]; 
}//3 cycles
void ROLzpg() { 
	u8 v = RAM[read(PC)]; RAM[PC++] = (v << 1) | (v >> 7); 
}//5 cycles
void PLP() {
	FLAGS = RAM[--S]; 
}//4 cycles
void ANDimm() { 
	A &= read(PC++); 
}
void ROLA() { 
	A = (A << 1) | (A >> 7); 
}//2 cycles
void BITabs() { 
	u16 m = read(PC++) | (read(PC++) << 8);
	FLAGS |= (0x3f & m) | (m&A ? 0 : 0x02); 
}//4 cycles
void ANDabs() {
	u16 m = read(PC++) | (read(PC++) << 8);
	A &= m;
	FLAGS = (FLAGS & 0x7D) | A & 80 | (A ? 0 : 0x02);
}//4 cycles
void ROLabs() {
	u16 add = read(PC++) | (read(PC++) << 8);
	u8 val = read(add);
	write(add, (val << 1) | (val >> 7));
}
void BMI() {
	s8 dist = read(PC++);
	if (FLAGS & 0x80) PC += dist;
}
void ANDindy() {
	u8* v = f();
	u8 add = read(PC++);
	A &= read(read(add)|read(add+1)<<8+Y);
}//6 cycles
void ANDzpgx() {
	A &= RAM[(s8)X + read(PC++)];
}
void ROLzpgx() {
	u8 v = RAM[read((s8)X+PC++)]; RAM[X+PC] = (v << 1) | (v >> 7);
}
void SEC() {
	FLAGS |= 1;
}
void ANDabsy() {
	A &= read((read(PC++) | (read(PC++) << 8)) + Y);
}
template<u8*(*f)(void)>
void AND() {
	u8* v = f();
	A &= *v;
}
template<u8*(*f)(void)>
void ROL() {
	u8* v = f();
	*v = (*v << 1) | (*v >> 7));
}
void RTC() {
	FLAGS = pop();
	PC = (pop() << 8) | pop();
}
void PHA() {
	push(A);
}
template<u8*(*f)(void)>
void JMPabs() {
	u8* v = f();
	PC = *v);
}
template<u8*(*f)(void)>
void BVC() {
	s8* v = f();
	if (FLAGS & 0x40) PC += v;
}
template<u8*(*f)(void)>
void LSR() {
	u8* v = f();
	*v >>= 1;
}
void CLI() {
	FLAGS &= 0xFB;
}
template<u8*(*f)(void)>
void EOR() {
	u8* v = f();
	A ^= *v;
}
void RTS() {
	PC = pop() + 1;
}
template<u8*(*f)(void)>
void ADCzpg() {
	u8* v = f();
	A += *v + (FLAGS & 1);
}
template<u8*(*f)(void)>
void ROR() {
	u8* v = f();
	*v = (*v >> 1) | (*v << 7);
}
void PLA() {
	A = pop();
}
static void(*opmap[])(void) = {
//  0     , 1      , 2  , 3  , 4     , 5      , 6      , 7  , 8  , 9      , A   , B  , C     , D      , E      , F
	BRK   , ORAindx, nop, nop, nop   , ORAzpg , ASLzpg , nop, PHP, ORAimm , ASLA, nop, nop   , ORAabs , ASLabs , nop,//0
	BPLrel, ORAindy, nop, nop, nop   , ORAzpgx, ASLzpgx, nop, CLC, ORAabsy, nop , nop, nop   , ORAabsx, ASLabsx, nop,//1
	JSRabs, ANDindx, nop, nop, BITzpg, ANDzpg , ROLzpg , nop, PLP, ANDimm , ROLA, nop, BITabs, ANDabs , ROLabs , nop,//2
	BMI   , ANDindy, nop, nop, nop   , ANDzpgx, ROLzpgx, nop, SEC, ANDabsy, nop , nop, nop   , ANDabsx, ROLabsx, nop,//3
	RTC   , EORindx, nop, nop, nop   , EORzpg , LSRzpg , nop, PHA, EORimm , LSRA, nop, JMPabs, EORabs , LSRabs , nop,//4
	BVC   , EORindy, nop, nop, nop   , EORzpgx, LSRzpgx, nop, CLI, EORabsy, nop , nop, nop   , EORabsx, LSRabsx, nop,//5
	RTS   , ADCindx, nop, nop, nop   , ADCzpg , RORzpg , nop, PLA, 
};

void clockCPU() {
	opmap[PC++]();
}

int main() {
	while (true) {
		clockCPU();
	}
}