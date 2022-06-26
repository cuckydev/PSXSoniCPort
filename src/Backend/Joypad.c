#include "Joypad.h"

#include <stddef.h>

#include <sys/types.h>
#include <psxpad.h>
#include <psxapi.h>
#include <psxetc.h>

#include <stdio.h>

// Pad constants
#define PAD_SELECT   1
#define PAD_L3       2
#define PAD_R3       4
#define PAD_START    8
#define PAD_UP       16
#define PAD_RIGHT    32
#define PAD_DOWN     64
#define PAD_LEFT     128
#define PAD_L2       256
#define PAD_R2       512
#define PAD_L1       1024
#define PAD_R1       2048
#define PAD_TRIANGLE 4096
#define PAD_CIRCLE   8192
#define PAD_CROSS    16384
#define PAD_SQUARE   32768

// Pad state
typedef struct
{
	uint16_t held;
	uint8_t left_x, left_y;
	uint8_t right_x, right_y;
} Pad;

static uint16_t pad_buff[2][34/2];
static Pad pad_state, pad_state_2;

// Joypad information
void Joypad_Init()
{
	// Clear pad states
	pad_state.held = 0;
	pad_state.left_x = pad_state.left_y = pad_state.right_x = pad_state.right_y = 0;
	
	pad_state_2.held = 0;
	pad_state_2.left_x = pad_state_2.left_y = pad_state_2.right_x = pad_state_2.right_y = 0;

	// Initialize system pads
	InitPAD((char*)pad_buff[0], 34, (char*)pad_buff[1], 34);
	pad_buff[0][0] = 0xFFFF;
	pad_buff[1][0] = 0xFFFF;
	StartPAD();
	
	ChangeClearPAD(0);
}

static uint8_t Joypad_UpdateState(Pad *this, PADTYPE *pad)
{
	if (pad->stat == 0)
	{
		// Read pad information
		if ((pad->type == 0x4) ||
			(pad->type == 0x5) ||
			(pad->type == 0x7))
		{
			// Set pad state
			this->held = ~pad->btn;
			this->left_x  = pad->ls_x;
			this->left_y  = pad->ls_y;
			this->right_x = pad->rs_x;
			this->right_y = pad->rs_y;
			
			// Get MegaDrive format bitfield
			uint8_t start = (this->held & PAD_START)  ? JPAD_START : 0;
			uint8_t a     = (this->held & PAD_SQUARE) ? JPAD_A     : 0;
			uint8_t b     = (this->held & PAD_CROSS)  ? JPAD_B     : 0;
			uint8_t c     = (this->held & PAD_CIRCLE) ? JPAD_C     : 0;
			uint8_t right = (this->held & PAD_RIGHT)  ? JPAD_RIGHT : 0;
			uint8_t left  = (this->held & PAD_LEFT)   ? JPAD_LEFT  : 0;
			uint8_t down  = (this->held & PAD_DOWN)   ? JPAD_DOWN  : 0;
			uint8_t up    = (this->held & PAD_UP)     ? JPAD_UP    : 0;
			
			return start | a | c | b | right | left | down | up;
		}
		else
		{
			// Clear pad state
			this->held = 0;
			this->left_x = 0;
			this->left_y = 0;
			this->right_x = 0;
			this->right_y = 0;
			return 0;
		}
	}
	else
	{
		// Clear pad state
		this->held = 0;
		this->left_x = 0;
		this->left_y = 0;
		this->right_x = 0;
		this->right_y = 0;
		return 0;
	}
}

uint8_t Joypad_GetState1()
{
	// Update pad state and get MegaDrive format bitfield
	return Joypad_UpdateState(&pad_state, (PADTYPE*)pad_buff[0]);
}

uint8_t Joypad_GetState2()
{
	// Update pad state and get MegaDrive format bitfield
	return Joypad_UpdateState(&pad_state_2, (PADTYPE*)pad_buff[1]);
}
