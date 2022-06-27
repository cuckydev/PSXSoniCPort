#include "MegaDrive.h"

#include <psxapi.h>
#include <psxetc.h>
#include <hwregs_c.h>

// Timer constants
#define TIMER_RATE 100

// Timer state
static volatile uint32_t ticks;

static void Timer_Callback(void)
{
	ticks++;
}

// Timer interface
void Timer_Init(void)
{
	ticks = 0;
	
	EnterCriticalSection();
	ChangeClearRCnt(2, 0);
	InterruptCallback(6, Timer_Callback);
	
	// CLK/8 input, IRQ on reload, disable one-shot IRQ
	TIMER_CTRL(2)   = 0x0258;
	TIMER_RELOAD(2) = (F_CPU / 8) / TIMER_RATE;
	
	ExitCriticalSection();
}

uint32_t Timer_GetTicks(void)
{
	return ticks;
}
