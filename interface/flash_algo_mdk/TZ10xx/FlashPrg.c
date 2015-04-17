/***********************************************************************/
/*                                                                     */
/*  FlashDev.C:  Flash Programming Functions adapted                   */
/*               for TZ10xx 1024kB Flash                               */
/*                                                                     */
/***********************************************************************/

/*
 * COPYRIGHT (C) 2014
 * TOSHIBA CORPORATION SEMICONDUCTOR & STORAGE PRODUCTS COMPANY
 * ALL RIGHTS RESERVED
 *
 * THE SOURCE CODE AND ITS RELATED DOCUMENTATION IS PROVIDED "AS IS". TOSHIBA
 * CORPORATION MAKES NO OTHER WARRANTY OF ANY KIND, WHETHER EXPRESS, IMPLIED OR,
 * STATUTORY AND DISCLAIMS ANY AND ALL IMPLIED WARRANTIES OF MERCHANTABILITY,
 * SATISFACTORY QUALITY, NON INFRINGEMENT AND FITNESS FOR A PARTICULAR PURPOSE.
 * 
 * THE SOURCE CODE AND DOCUMENTATION MAY INCLUDE ERRORS. TOSHIBA CORPORATION
 * RESERVES THE RIGHT TO INCORPORATE MODIFICATIONS TO THE SOURCE CODE IN LATER
 * REVISIONS OF IT, AND TO MAKE IMPROVEMENTS OR CHANGES IN THE DOCUMENTATION OR
 * THE PRODUCTS OR TECHNOLOGIES DESCRIBED THEREIN AT ANY TIME.
 * 
 * TOSHIBA CORPORATION SHALL NOT BE LIABLE FOR ANY DIRECT, INDIRECT OR
 * CONSEQUENTIAL DAMAGE OR LIABILITY ARISING FROM YOUR USE OF THE SOURCE CODE OR
 * ANY DOCUMENTATION, INCLUDING BUT NOT LIMITED TO, LOST REVENUES, DATA OR
 * PROFITS, DAMAGES OF ANY SPECIAL, INCIDENTAL OR CONSEQUENTIAL NATURE, PUNITIVE
 * DAMAGES, LOSS OF PROPERTY OR LOSS OF PROFITS ARISING OUT OF OR IN CONNECTION
 * WITH THIS AGREEMENT, OR BEING UNUSABLE, EVEN IF ADVISED OF THE POSSIBILITY OR
 * PROBABILITY OF SUCH DAMAGES AND WHETHER A CLAIM FOR SUCH DAMAGE IS BASED UPON
 * WARRANTY, CONTRACT, TORT, NEGLIGENCE OR OTHERWISE.
 */

#include "..\FlashOS.H"        // FlashOS Structures

#ifdef __arm
#define rev(x)     __rev(x)
#define nop()      __nop()
#else
#define rev(x)     __builtin_bswap32(x)
#define nop()      asm volatile("nop")
#endif

#define REG(adr)   (*((volatile unsigned long*)(CNT_BASE + (adr))))
#define CNF(adr)   (*((volatile unsigned long*)(CNF_BASE + (adr))))

#define CORE_CLOCK   48000000

#define CNT_BASE     0x40004000 // Controller base address
#define CNF_BASE     0x4004A000
#define TIME_LIMIT   128000 /*8000*/

#if   defined ( __CC_ARM )
#elif defined ( __ICCARM__ )
#pragma optimize=no_unroll
#elif defined ( __GNUC__ )
__attribute__((optimize("no-unroll-loops")))
#endif
static void BusyWait(unsigned long usec)
{
  unsigned long i;

  /* 4 cycles for loop overhead */
#if (CORE_CLOCK < (1000000 * 4))
  i = (CORE_CLOCK * usec / (1000000 * 4);
#else
  i = CORE_CLOCK / (1000000 * 4) * usec;
#endif
  do {
    nop();
  } while (--i);
}

static int ReadStatus(unsigned int *st)
{
  unsigned int s;
  unsigned int cnt;
  // Read Status Command
  REG(0x028) = 0x00000100;
  REG(0x02C) = 0x00000400;
  REG(0x030) = 0x00000230;
  REG(0x100) = 0x00000005;
  REG(0x034) = 0x00000001;
  // wait for done
  for (cnt = TIME_LIMIT; cnt; --cnt) {
    s  = REG(0x0A0);
    if (s & 1) {
      break;  // done
    }
  }
  if (!cnt) {
    return 1; // timeout
  }
  REG(0x0A0) = 0x0000000F;
  *st = REG(0x200);
  return 0;
}

#ifdef ENABLE_QUAD
static int ReadStatus2(unsigned int *st)
{
  unsigned int s;
  unsigned int cnt;
  // Read Status Command
  REG(0x028) = 0x00000100;
  REG(0x02C) = 0x00000400;
  REG(0x030) = 0x00000230;
  REG(0x100) = 0x00000035;
  REG(0x034) = 0x00000001;
  // wait for done
  for (cnt = TIME_LIMIT; cnt; --cnt) {
    s  = REG(0x0A0);
    if (s & 1) {
      break;  // done
    }
  }
  if (!cnt) {
    return 1; // timeout
  }
  REG(0x0A0) = 0x0000000F;
  *st = REG(0x200);
  return 0;
}
#endif

static int WriteCommand(unsigned long r0, unsigned int r1, unsigned long r2)
{
  unsigned int s;
  unsigned int cnt;

  REG(0x028) = r0;
  REG(0x02C) = 0x00000400;
  REG(0x030) = r1;
  REG(0x100) = r2;
  REG(0x034) = 0x00000001;
  // wait for done
  for (cnt = TIME_LIMIT; cnt; --cnt) {
    s  = REG(0x0A0);
    if (s & 2) {
      break;  // done
    }
  }
  if (!cnt) {
    return 1; // timeout
  }
  REG(0x0A0) = 0x0000000F;
  return 0;
}

static int PrepareWrite(void)
{
  unsigned int s;
  unsigned int cnt;

  // Write Enable Command
  if (WriteCommand(0x00000100, 0x00000310, 0x00000006)) {
    return 1;
  }
  // wait for change state
  for (cnt = TIME_LIMIT; cnt; --cnt) {
    if (ReadStatus(&s)) {
      return 1;
    }
    if (s & 2) {
      break; // done
    }
  }
  return 0;
}

static int Polling(void)
{
  unsigned int s;
  unsigned int cnt;
  int i;

  // wait for done
  for (cnt = TIME_LIMIT; cnt; --cnt) {
    if (ReadStatus(&s)) {
      return 1;
    }
    if (0 == (s & 1)) {
      break;
    }
    for (i = 0; i < 100; ++i) {
      ;
    }
  }
  return 0;
}

static int SingleInputProgramPage(unsigned long adr, unsigned long sz, unsigned char *buf)
{
  unsigned long i;
  unsigned long *p0;
  unsigned char *p1;
  unsigned long ofs;
  unsigned long val;

  if (PrepareWrite()) {
    return 1;
  }

  p0 = (unsigned long*)buf;
  ofs = sz & ~3;
  for (i = 0; i < ofs; i += 4) {
    REG(0x200 + i) = *p0++;
  }
  if (sz != ofs) {
    val = 0;
    p1  = buf + ofs;
    for (i = 0; i < (sz & 3); ++i) {
      val |= *p1++ << (i * 8);
    }
    REG(0x200 + ofs) = val;
  }
  if (sz < 224) {
    /* already 1us passed. */
    BusyWait(8 - (sz / 32));
  }
  // Write Page Program Command
  if (WriteCommand(0x00000100,
                   (0x00030330 | ((sz - 1) << 24)),
                   (rev(adr) | 0x02))) {
    return 1;
  }
  if (Polling()) {
    return 1;
  }
  return 0;
}

#ifdef ENABLE_QUAD
static int QuadInputProgramPage(unsigned long adr, unsigned long sz, unsigned char *buf)
{
  unsigned long i;
  unsigned long *p0;
  unsigned char *p1;
  unsigned long ofs;
  unsigned long val;

  if (PrepareWrite()) {
    return 1;
  }

  p0 = (unsigned long*)buf;
  ofs = sz & ~3;
  for (i = 0; i < ofs; i += 4) {
    REG(0x200 + i) = *p0++;
  }
  if (sz != ofs) {
    val = 0;
    p1  = buf + ofs;
    for (i = 0; i < (sz & 3); ++i) {
      val |= *p1++ << (i * 8);
    }
    REG(0x200 + ofs) = val;
  }
  if (sz < 224) {
    /* already 1us passed. */
    BusyWait(8 - (sz / 32));
  }
  // Write Page Program Command
  if (WriteCommand(0x00000102,
                   (0x00030330 | ((sz - 1) << 24)),
                   (rev(adr) | 0x32))) {
    return 1;
  }
  if (Polling()) {
    return 1;
  }
  return 0;
}
#endif

/* Flash Programming Functions (Called by FlashOS) */

int Init(unsigned long adr, unsigned long clk, unsigned long fnc)
{
  CNF(0x154) = 0;
  if (3 == fnc) {
    REG(0x050) = 1;
  }
  return 0;
}

int UnInit(unsigned long fnc)
{
  return 0;
}

/*
 *  Erase complete Flash Memory
 *    Return Value:   0 - OK,  1 - Failed
 */
int EraseChip(void)
{
  if (PrepareWrite()) {
    return 1;
  }
  /* already 1us passed. */
  BusyWait(8);
  // Write Chip Erase Command
  if (WriteCommand(0x00000100,
                   0x00000310, 0x000000C7)) {
    return 1;
  }
  if (Polling()) {
    return 1;
  }
  return 0;
}

/*
 *  Erase Sector in Flash Memory
 *    Parameter:      adr:  Sector Address
 *    Return Value:   0 - OK,  1 - Failed
 */
int EraseSector(unsigned long adr)
{
  if (PrepareWrite()) {
    return 1;
  }
  /* already 1us passed. */
  BusyWait(8);
  // Write Sector Erase Command
  if (WriteCommand(0x00000100,
                   0x00030310,
                   (rev(adr) | 0x20))) {
    return 1;
  }
  if (Polling()) {
    return 1;
  }
  return 0;
}

/*
 *  Program Page in Flash Memory
 *    Parameter:      adr:  Page Start Address
 *                    sz:   Page Size
 *                    buf:  Page Data
 *    Return Value:   0 - OK,  1 - Failed
 */
int ProgramPage(unsigned long adr, unsigned long sz, unsigned char *buf)
{
#ifdef ENABLE_QUAD
  unsigned int quad;
  int err;

  quad = 0;
  err = ReadStatus2(&quad);
  if (err) {
    return 1;
  }
  if (quad & 2) {
    return QuadInputProgramPage(adr, sz, buf);
  } else {
    return SingleInputProgramPage(adr, sz, buf);
  }
#else
  return SingleInputProgramPage(adr, sz, buf);
#endif
}
