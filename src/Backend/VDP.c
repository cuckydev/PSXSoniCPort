#include "VDP.h"

#include "MegaDrive.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <sys/types.h>
#include <psxetc.h>
#include <psxgte.h>
#include <psxgpu.h>

// VDP constants
#define VDP_DIRTY_HEIGHT 128

static const uint8_t VDP_COLLEVEL_8[] = {
	0,
	52,
	87,
	116,
	144,
	172,
	206,
	255
};

static const uint16_t VDP_COLLEVEL_5[] = {
	VDP_COLLEVEL_8[0] * 31 / 255,
	VDP_COLLEVEL_8[1] * 31 / 255,
	VDP_COLLEVEL_8[2] * 31 / 255,
	VDP_COLLEVEL_8[3] * 31 / 255,
	VDP_COLLEVEL_8[4] * 31 / 255,
	VDP_COLLEVEL_8[5] * 31 / 255,
	VDP_COLLEVEL_8[6] * 31 / 255,
	VDP_COLLEVEL_8[7] * 31 / 255
};

// VDP internal state
static ALIGNED4 uint8_t vdp_vram[VRAM_SIZE];
static ALIGNED4 uint8_t vdp_vram_8[VRAM_SIZE * 2];
static uint8_t *vdp_vram_p;

static enum
{
	VDPPlot_Null,
	VDPPlot_PlaneA,
	VDPPlot_PlaneB
} vdp_vram_plot;

static uint8_t vdp_vram_dirty[VRAM_SIZE / (VDP_DIRTY_HEIGHT * 4)];
static uint8_t vdp_vram_plane_dirty[2][PLANE_WIDTH * PLANE_HEIGHT];

static uint16_t vdp_cram[16 * 4];
static uint16_t *vdp_cram_p;

static size_t vdp_plane_a_location, vdp_plane_b_location, vdp_sprite_location, vdp_hscroll_location;
static size_t vdp_plane_w, vdp_plane_h, vdp_plane_size;
static uint8_t vdp_background_colour;

static int16_t vdp_vscroll_a, vdp_vscroll_b;

static int16_t vdp_hint_pos;

static MD_Vector vdp_hint, vdp_vint;

static unsigned int vdp_last_time;

// GPU state
#define GFX_OTLEN 1

static struct GpuState
{
	// Environments
	DISPENV disp;
	DRAWENV draw;
	
	// Buffers
	u_long ot[1 + GFX_OTLEN]; // Ordering table
	uint8_t pri[0x10000]; // Primitive buffer
	uint8_t *prip;
} gpu_state[2];
static struct GpuState *gpu_statep;

// VDP interface
int VDP_Init(const MD_Header *header)
{
	// Initialize GPU
	ResetGraph(0);
	
	// Define display environments, first on top and second on bottom
	SetDefDispEnv(&gpu_state[0].disp, 0, 0, SCREEN_WIDTH, 240);
	SetDefDispEnv(&gpu_state[1].disp, 0, 256, SCREEN_WIDTH, 240);
	
	gpu_state[0].disp.screen.y = 8;
	gpu_state[0].disp.screen.w = SCREEN_WIDTH;
	gpu_state[0].disp.screen.h = 224;
	
	gpu_state[1].disp.screen.y = 8;
	gpu_state[1].disp.screen.w = SCREEN_WIDTH;
	gpu_state[1].disp.screen.h = 224;
	
	// Define drawing environments, first on bottom and second on top
	SetDefDrawEnv(&gpu_state[0].draw, 0, 256, SCREEN_WIDTH, 224);
	SetDefDrawEnv(&gpu_state[1].draw, 0, 0, SCREEN_WIDTH, 224);
	
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
	
	// Enable display output
	SetDispMask(1);
	
	return 0;
}

void VDP_SeekVRAM(size_t offset)
{
	vdp_vram_p = vdp_vram + offset;
	if (offset >= (vdp_plane_a_location) && offset < (vdp_plane_a_location + vdp_plane_size))
		vdp_vram_plot = VDPPlot_PlaneA;
	else if (offset >= (vdp_plane_b_location) && offset < (vdp_plane_b_location + vdp_plane_size))
		vdp_vram_plot = VDPPlot_PlaneB;
	else
		vdp_vram_plot = VDPPlot_Null;
}

static void VDP_DirtyVRAM(size_t a, size_t b)
{
	b -= 1; // B represents the last byte written
	
	if (vdp_vram_plot == VDPPlot_Null)
	{
		// General dirty
		a /= (VDP_DIRTY_HEIGHT * 4);
		b /= (VDP_DIRTY_HEIGHT * 4);
		for (size_t i = a; i <= b; i++)
			vdp_vram_dirty[i] = 1;
	}
	else
	{
		// Get plane to write
		uint8_t *dirtyp;
		if (vdp_vram_plot == VDPPlot_PlaneA)
		{
			dirtyp = vdp_vram_plane_dirty[0];
			a -= vdp_plane_a_location;
			b -= vdp_plane_a_location;
		}
		else
		{
			dirtyp = vdp_vram_plane_dirty[1];
			a -= vdp_plane_b_location;
			b -= vdp_plane_b_location;
		}
		
		// Get bounding box of dirty
		size_t xa = (a >> 1) & (PLANE_WIDTH - 1);
		size_t ya = (a >> 1) / PLANE_WIDTH;
		size_t xb = (b >> 1) & (PLANE_WIDTH - 1);
		size_t yb = (b >> 1) / PLANE_WIDTH;
		
		size_t temp;
		if (xa > xb)
		{
			temp = xa;
			xa = xb;
			xb = temp;
		}
		if (ya > yb)
		{
			temp = ya;
			ya = yb;
			yb = temp;
		}
		
		// Write dirty
		for (size_t x = xa; x <= xb; x++)
			for (size_t y = ya; y <= yb; y++)
				dirtyp[x + (y * PLANE_WIDTH)] = 1;
	}
}

void VDP_WriteVRAM(const uint8_t *data, size_t len)
{
	uint8_t *vdp_vram_start = vdp_vram_p;
	memcpy(vdp_vram_start, data, len);
	vdp_vram_p += len;
	VDP_DirtyVRAM(vdp_vram_start - vdp_vram, vdp_vram_p - vdp_vram);
}

void VDP_FillVRAM(uint8_t data, size_t len)
{
	uint8_t *vdp_vram_start = vdp_vram_p;
	memset(vdp_vram_start, data, len);
	vdp_vram_p += len;
	VDP_DirtyVRAM(vdp_vram_start - vdp_vram, vdp_vram_p - vdp_vram);
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
	vdp_plane_a_location = loc;
}

void VDP_SetPlaneBLocation(size_t loc)
{
	vdp_plane_b_location = loc;
}

void VDP_SetSpriteLocation(size_t loc)
{
	vdp_sprite_location = loc;
}

void VDP_SetHScrollLocation(size_t loc)
{
	vdp_hscroll_location = loc;
}

void VDP_SetPlaneSize(size_t w, size_t h)
{
	vdp_plane_w = w;
	vdp_plane_h = h;
	vdp_plane_size = w * h * 2;
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
void *VDP_AllocPrim(size_t size)
{
	// Allocate and link primitive of given size
	void *pri = gpu_statep->prip;
	gpu_statep->prip += size;
	addPrim(&gpu_statep->ot[1], pri);
	return pri;
}

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
			cram_fmt[i] = 0x8000 | (VDP_COLLEVEL_5[b] << 10) | (VDP_COLLEVEL_5[g] << 5) | VDP_COLLEVEL_5[r];
		}
		else
		{
			// Transparent
			cram_fmt[i] = 0x0000;
		}
	}
	
	// Draw sprites
	for (uint8_t i = 0;;)
	{
		// Get sprite values
		const uint16_t *sprite = (const uint16_t*)(vdp_vram + vdp_sprite_location + ((size_t)i << 3));
		uint16_t sprite_y = sprite[0];
		uint16_t sprite_sl = sprite[1];
		uint8_t sprite_width = (sprite_sl & SPRITE_SL_W_AND) >> SPRITE_SL_W_SHIFT;
		uint8_t sprite_height = (sprite_sl & SPRITE_SL_H_AND) >> SPRITE_SL_H_SHIFT;
		uint8_t sprite_link = (sprite_sl & SPRITE_SL_L_AND) >> SPRITE_SL_L_SHIFT;
		uint16_t sprite_tile = sprite[2];
		uint16_t sprite_x = sprite[3];
		
		// Write sprites
		for (uint8_t x = 0; x <= sprite_width; x++)
		{
			TILE *tile = VDP_AllocPrim(sizeof(TILE));
			setTile(tile);
			tile->x0 = (int)sprite_x - 0x80 + (x << 3);
			tile->y0 = (int)sprite_y - 0x80;
			tile->w = 8;
			tile->h = 8 + (sprite_height << 3);
			setRGB0(tile, 255, 255, 255);
		}
		
		// Go to next sprite
		if (sprite_link != 0)
			i = sprite_link;
		else
			break;
	}
	
	// Fill background
	{
		uint16_t cv = vdp_cram[vdp_background_colour];
		uint8_t r = (cv & 0x00E) >> 1;
		uint8_t g = (cv & 0x0E0) >> 5;
		uint8_t b = (cv & 0xE00) >> 9;
		r = VDP_COLLEVEL_8[r];
		g = VDP_COLLEVEL_8[g];
		b = VDP_COLLEVEL_8[b];
		
		TILE *bg_fill = VDP_AllocPrim(sizeof(TILE));
		setTile(bg_fill);
		bg_fill->x0 = 0;
		bg_fill->y0 = 0;
		bg_fill->w = 240;
		bg_fill->h = SCREEN_HEIGHT;
		setRGB0(bg_fill, r, g, b);
		
		bg_fill = VDP_AllocPrim(sizeof(TILE));
		setTile(bg_fill);
		bg_fill->x0 = 240;
		bg_fill->y0 = 0;
		bg_fill->w = SCREEN_WIDTH - 240;
		bg_fill->h = SCREEN_HEIGHT;
		setRGB0(bg_fill, r, g, b);
	}
	
	// Flush GPU
	DrawSync(0);
	
	// Transfer formatted CRAM to VRAM
	RECT cram_rect = {0, 511, 16 * 4, 1};
	LoadImage(&cram_rect, (u_long*)cram_fmt);
	
	// Update dirty VRAM
	RECT dec_rect = {SCREEN_WIDTH, 0, 4, VDP_DIRTY_HEIGHT};
	
	uint8_t *vram_dirtyp = vdp_vram_dirty;
	uint8_t *dirtyp = vdp_vram;
	uint8_t *vram8p = vdp_vram_8;
	
	for (size_t i = 0; i < (VRAM_SIZE / (VDP_DIRTY_HEIGHT * 4)); i++)
	{
		if (*vram_dirtyp)
		{
			// Deconstruct VRAM here
			uint8_t *vram8 = vram8p;
			for (size_t j = 0; j < (VDP_DIRTY_HEIGHT * 4); j++)
			{
				*vram8p++ = (*dirtyp & 0xF0) >> 4;
				*vram8p++ = (*dirtyp & 0x0F) >> 0;
				dirtyp++;
			}
			
			// Transfer to VRAM
			LoadImage(&dec_rect, (u_long*)vram8);
			
			// Clear dirty flag
			*vram_dirtyp = 0;
		}
		else
		{
			dirtyp += (VDP_DIRTY_HEIGHT * 4);
			vram8p += (VDP_DIRTY_HEIGHT * 8);
		}
		
		// Move rect
		if ((dec_rect.y += VDP_DIRTY_HEIGHT) & 0x200)
		{
			dec_rect.x += 4;
			dec_rect.y = 0;
		}
		vram_dirtyp++;
	}
	
	// Update dirty planes
	ALIGNED4 uint8_t plane[8 * 8];
	RECT plane_rect = {768, 0, 4, 8};
	
	uint8_t *plane_dirtyp = &vdp_vram_plane_dirty[0][0];
	
	for (size_t i = 0; i < 2; i++)
	{
		// Update dirty tiles
		size_t vram_i = i ? vdp_plane_b_location : vdp_plane_a_location;
		for (size_t j = 0; j < (PLANE_WIDTH * PLANE_HEIGHT); j++)
		{
			if (*plane_dirtyp)
			{
				// Get tile
				const uint16_t tile = *((uint16_t*)(vdp_vram + vram_i));
				uint8_t palette = ((tile & TILE_PALETTE_AND) >> TILE_PALETTE_SHIFT) << 4;
				uint8_t y_flip = (tile & TILE_Y_FLIP_AND) != 0;
				uint8_t x_flip = (tile & TILE_X_FLIP_AND) != 0;
				uint16_t pattern = (tile & TILE_PATTERN_AND) >> TILE_PATTERN_SHIFT;
				
				// Deconstruct tile
				uint8_t *patternp = vdp_vram_8 + ((size_t)pattern * 0x40);
				uint8_t *planep = plane;
				
				if (x_flip)
				{
					if (y_flip)
					{
						for (size_t l = 63; l <= 63; l--)
							*planep++ = patternp[l] | palette;
					}
					else
					{
						for (size_t k = 0; k < 8; k++)
						{
							for (size_t l = 7; l <= 7; l--)
								*planep++ = patternp[l] | palette;
							patternp += 8;
						}
					}
				}
				else
				{
					if (y_flip)
					{
						for (signed int l = 56; l >= -56; l -= 16)
							for (size_t k = 0; k < 8; k++)
								*planep++ = (patternp++)[l] | palette;
					}
					else
					{
						for (size_t k = 0; k < 64; k++)
							*planep++ = *patternp++ | palette;
					}
				}
				
				// Transfer to VRAM
				LoadImage(&plane_rect, (u_long*)plane);
				
				// Clear dirty flag
				*plane_dirtyp = 0;
			}
			
			// Move rect
			if ((plane_rect.x += 4) & 0x400)
			{
				plane_rect.x = 768;
				plane_rect.y += 8;
			}
			plane_dirtyp++;
			vram_i += 2;
		}
	}
	
	// Display screen
	VSync(0);
	
	PutDispEnv(&gpu_statep->disp);
	PutDrawEnv(&gpu_statep->draw);
	
	// Draw state
	DrawOTag(&gpu_statep->ot[GFX_OTLEN]);
	
	// Send vertical interrupt
	vdp_vint();
}
