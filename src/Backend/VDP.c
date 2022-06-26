#include "VDP.h"

#include "MegaDrive.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <sys/types.h>
#include <psxetc.h>
#include <psxgte.h>
#include <psxgpu.h>

// VDP internal state
static ALIGNED4 uint8_t vdp_vram[VRAM_SIZE];
static uint8_t *vdp_vram_p;
static size_t vdp_vram_i;

static uint8_t vdp_dirty[VRAM_SIZE / (512 * 4)];

static uint16_t vdp_cram[16 * 4];
static uint16_t *vdp_cram_p;

static size_t vdp_plane_a_location, vdp_plane_b_location, vdp_sprite_location, vdp_hscroll_location;
static size_t vdp_plane_w, vdp_plane_h;
static uint8_t vdp_background_colour;

static int16_t vdp_vscroll_a, vdp_vscroll_b;

static int16_t vdp_hint_pos;

static MD_Vector vdp_hint, vdp_vint;

// GPU state
#define GFX_OTLEN 1

static struct GpuState
{
	// Environments
	DISPENV disp;
	DRAWENV draw;
	
	// Buffers
	u_long ot[1 + GFX_OTLEN]; // Ordering table
	uint8_t pri[0x8000]; // Primitive buffer
	uint8_t *prip;
} gpu_state[2];
struct GpuState *gpu_statep;

// VDP interface
int VDP_Init(const MD_Header *header)
{
	// Initialize GPU
	ResetGraph(0);
	
	// Define display environments, first on top and second on bottom
	SetDefDispEnv(&gpu_state[0].disp, 0, 0, SCREEN_WIDTH, 240);
	SetDefDispEnv(&gpu_state[1].disp, 0, 256, SCREEN_WIDTH, 240);
	
	gpu_state[0].disp.screen.y = 8;
	gpu_state[0].disp.screen.w = 320;
	gpu_state[0].disp.screen.h = 224;
	
	gpu_state[1].disp.screen.y = 8;
	gpu_state[1].disp.screen.w = 320;
	gpu_state[1].disp.screen.h = 224;
	
	// Define drawing environments, first on bottom and second on top
	SetDefDrawEnv(&gpu_state[0].draw, 0, 256, SCREEN_WIDTH, 240);
	SetDefDrawEnv(&gpu_state[1].draw, 0, 0, SCREEN_WIDTH, 240);
	
	// Select GPU state
	gpu_statep = &gpu_state[0];
	
	// Initialize VDP state
	vdp_plane_a_location = 0;
	vdp_plane_b_location = 0;
	vdp_sprite_location  = 0;
	vdp_hscroll_location = 0;
	vdp_plane_w = 32;
	vdp_plane_h = 32;
	vdp_background_colour = 0;
	vdp_vscroll_a = 0;
	vdp_vscroll_b = 0;
	vdp_hint_pos = -1;
	
	vdp_hint = header->h_interrupt;
	vdp_vint = header->v_interrupt;
	
	return 0;
}

void VDP_SeekVRAM(size_t offset)
{
	vdp_vram_p = vdp_vram + offset;
	vdp_vram_i = offset;
}

static void VDP_WriteByte(uint8_t data)
{
	*vdp_vram_p++ = data;
	vdp_dirty[vdp_vram_i / (512 * 4)] = 1;
	vdp_vram_i++;
}

void VDP_WriteVRAM(const uint8_t *data, size_t len)
{
	while (len-- > 0)
		VDP_WriteByte(*data++);
}

void VDP_FillVRAM(uint8_t data, size_t len)
{
	while (len-- > 0)
		VDP_WriteByte(data);
}

void VDP_SeekCRAM(size_t offset)
{
	vdp_cram_p = vdp_cram + offset;
}

void VDP_WriteCRAM(const uint16_t *data, size_t len)
{
	memcpy(vdp_cram_p, data, len << 1);
	vdp_cram_p += len;
}

void VDP_FillCRAM(uint16_t data, size_t len)
{
	while (len-- > 0)
		*vdp_cram_p++ = data;
}

void VDP_SetPlaneALocation(size_t loc)
{
	loc &= ~0x3FF;
	vdp_plane_a_location = loc;
}

void VDP_SetPlaneBLocation(size_t loc)
{
	loc &= ~0x1FFF;
	vdp_plane_b_location = loc;
}

void VDP_SetSpriteLocation(size_t loc)
{
	loc &= ~0x1FF;
	vdp_sprite_location = loc;
}

void VDP_SetHScrollLocation(size_t loc)
{
	loc &= ~0x3FF;
	vdp_hscroll_location = loc;
}

void VDP_SetPlaneSize(size_t w, size_t h)
{
	vdp_plane_w = w;
	vdp_plane_h = h;
}

void VDP_SetBackgroundColour(uint8_t index)
{
	vdp_background_colour = index;
}

void VDP_SetVScroll(int16_t scroll_a, int16_t scroll_b)
{
	vdp_vscroll_a = scroll_a;
	vdp_vscroll_b = scroll_b;
}

void VDP_SetHIntPosition(int16_t pos)
{
	vdp_hint_pos = pos;
}

// VDP rendering
void VDP_Render()
{
	// Flip GPU state
	gpu_statep = (gpu_statep == &gpu_state[0]) ? &gpu_state[1] : &gpu_state[0];
	
	gpu_statep->prip = gpu_statep->pri;
	ClearOTagR(gpu_statep->ot, 1 + GFX_OTLEN);
	
	// Update CRAM
	uint16_t cram_fmt[4 * 16];
	for (size_t i = 0; i < 4 * 16; i++)
	{
		if (i & 0x0F)
		{
			// Get colour values
			uint16_t cv = vdp_cram[i];
			uint8_t r = (cv & 0x00E) >> 1;
			uint8_t g = (cv & 0x0E0) >> 5;
			uint8_t b = (cv & 0xE00) >> 9;

			// Get formatted colour
			static const uint16_t col_level[] = {
				0 * 31 / 255,
				52 * 31 / 255,
				87 * 31 / 255,
				116 * 31 / 255,
				144 * 31 / 255,
				172 * 31 / 255,
				206 * 31 / 255,
				255 * 31 / 255
			};
			cram_fmt[i] = 0x8000 | (col_level[b] << 10) | (col_level[g] << 5) | col_level[r];
		}
		else
		{
			// Transparent
			cram_fmt[i] = 0x0000;
		}
	}
	
	// Flush GPU
	DrawSync(0);
	
	// Transfer formatted CRAM to VRAM
	RECT cram_rect = {0, 511, 16 * 4, 1};
	LoadImage(&cram_rect, (u_long*)cram_fmt);
	
	// Update dirty VRAM
	uint8_t *dirty_p = vdp_vram;
	for (size_t i = 0; i < (VRAM_SIZE / (512 * 4)); i++)
	{
		if (vdp_dirty[i])
		{
			// Deconstruct VRAM here
			ALIGNED4 uint8_t dec[512 * 8];
			uint8_t *decp = dec;
			for (size_t j = 0; j < (512 * 4); j++)
			{
				*decp++ = (*dirty_p & 0xF0) >> 4;
				*decp++ = (*dirty_p & 0x0F) >> 0;
				dirty_p++;
			}
			
			// Transfer to VRAM
			RECT dec_rect = {384 + i * 4, 0, 4, 512};
			LoadImage(&dec_rect, (u_long*)dec);
			
			// Clear dirty flag
			vdp_dirty[i] = 0;
		}
		else
		{
			dirty_p += (512 * 4);
		}
	}
	
	// Display screen
	VSync(0);
	
	PutDispEnv(&gpu_statep->disp);
	PutDrawEnv(&gpu_statep->draw);
	
	// Draw state
	DrawOTag(&gpu_statep->ot[GFX_OTLEN]);
	
	// Enable display output
	SetDispMask(1);
	
	// Send vertical interrupt
	vdp_vint();
}
