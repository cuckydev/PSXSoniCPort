#include "VDP.h"

#include "MegaDrive.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

//VDP compile options
#define VDP_SANITY //Enable sanity checks for the VDP (slower, but technically safer, basically for testing)
//#define VDP_PALETTE_DISPLAY //Enable palette display

//VDP masks
#define VDP_MASK_PLANEPRI (1 << 0)
#define VDP_MASK_SPRITE   (1 << 1)

//VDP internal state
static ALIGNED2 uint8_t vdp_vram[VRAM_SIZE];
static uint16_t vdp_cram[4][16];

static uint8_t *vdp_vram_p;
static uint16_t *vdp_cram_p;

static size_t vdp_plane_a_location, vdp_plane_b_location, vdp_sprite_location, vdp_hscroll_location;
static size_t vdp_plane_w, vdp_plane_h;
static uint8_t vdp_background_colour;

static int16_t vdp_vscroll_a, vdp_vscroll_b;

static int16_t vdp_hint_pos;

static MD_Vector vdp_hint, vdp_vint;

//VDP interface
int VDP_Init(const MD_Header *header)
{
	//Initialize VDP state
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
	#ifdef VDP_SANITY
	if (offset >= VRAM_SIZE)
	{
		puts("VDP_SeekVRAM: Out-of-bounds");
		return;
	}
	#endif
	vdp_vram_p = vdp_vram + offset;
}

void VDP_WriteVRAM(const uint8_t *data, size_t len)
{
	#ifdef VDP_SANITY
	if ((vdp_vram_p - vdp_vram) >= VRAM_SIZE || (vdp_vram_p - vdp_vram + len) > VRAM_SIZE)
	{
		puts("VDP_WriteVRAM: Out-of-bounds");
		return;
	}
	#endif
	memcpy(vdp_vram_p, data, len);
	vdp_vram_p += len;
}

void VDP_FillVRAM(uint8_t data, size_t len)
{
	#ifdef VDP_SANITY
	if ((vdp_vram_p - vdp_vram) >= VRAM_SIZE || (vdp_vram_p - vdp_vram + len) > VRAM_SIZE)
	{
		puts("VDP_WriteVRAM: Out-of-bounds");
		return;
	}
	#endif
	memset(vdp_vram_p, data, len);
	vdp_vram_p += len;
}

void VDP_SeekCRAM(size_t offset)
{
	#ifdef VDP_SANITY
	if (offset >= COLOURS)
	{
		puts("VDP_SeekCRAM: Out-of-bounds");
		return;
	}
	#endif
	vdp_cram_p = &vdp_cram[0][0] + offset;
}

void VDP_WriteCRAM(const uint16_t *data, size_t len)
{
	#ifdef VDP_SANITY
	if ((vdp_cram_p - &vdp_cram[0][0]) >= COLOURS || (vdp_cram_p - &vdp_cram[0][0] + len) > COLOURS)
	{
		puts("VDP_WriteCRAM: Out-of-bounds");
		return;
	}
	#endif
	memcpy(vdp_cram_p, data, len << 1);
	vdp_cram_p += len;
}

void VDP_FillCRAM(uint16_t data, size_t len)
{
	#ifdef VDP_SANITY
	if ((vdp_cram_p - &vdp_cram[0][0]) >= COLOURS || (vdp_cram_p - &vdp_cram[0][0] + len) > COLOURS)
	{
		puts("VDP_WriteCRAM: Out-of-bounds");
		return;
	}
	#endif
	while (len-- > 0)
		*vdp_cram_p++ = data;
}

void VDP_SetPlaneALocation(size_t loc)
{
	loc &= ~0x3FF;
	#ifdef VDP_SANITY
	if (loc > VRAM_SIZE - PLANE_SIZE)
	{
		puts("VDP_SetPlaneALocation: Out-of-bounds");
		return;
	}
	#endif
	vdp_plane_a_location = loc;
}

void VDP_SetPlaneBLocation(size_t loc)
{
	loc &= ~0x1FFF;
	#ifdef VDP_SANITY
	if (loc > VRAM_SIZE - PLANE_SIZE)
	{
		puts("VDP_SetPlaneBLocation: Out-of-bounds");
		return;
	}
	#endif
	vdp_plane_b_location = loc;
}

void VDP_SetSpriteLocation(size_t loc)
{
	loc &= ~0x1FF;
	#ifdef VDP_SANITY
	if (loc > VRAM_SIZE - SPRITES_SIZE)
	{
		puts("VDP_SetSpriteLocation: Out-of-bounds");
		return;
	}
	#endif
	vdp_sprite_location = loc;
}

void VDP_SetHScrollLocation(size_t loc)
{
	loc &= ~0x3FF;
	#ifdef VDP_SANITY
	if (loc > VRAM_SIZE - SCREEN_HEIGHT * 4)
	{
		puts("VDP_SetHScrollLocation: Out-of-bounds");
		return;
	}
	#endif
	vdp_hscroll_location = loc;
}

void VDP_SetPlaneSize(size_t w, size_t h)
{
	#ifdef VDP_SANITY
	if (((w * h) << 1) > PLANE_SIZE)
	{
		printf("VDP_SetPlaneSize: Requested plane size exceeds 0x%04X bytes\n", PLANE_SIZE);
		return;
	}
	#endif
	vdp_plane_w = w;
	vdp_plane_h = h;
}

void VDP_SetBackgroundColour(uint8_t index)
{
	#ifdef VDP_SANITY
	if (index >= COLOURS)
	{
		puts("VDP_SetBackgroundColour: Illegal colour index");
		return;
	}
	#endif
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

//VDP rendering
void VDP_Render()
{
	
}
