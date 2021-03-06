#pragma once

// Screen dimensions
#define SCREEN_WIDTH  320
#define SCREEN_HEIGHT 224

#define SCREEN_WIDEADD  (SCREEN_WIDTH - 320)
#define SCREEN_WIDEADD2 (SCREEN_WIDEADD / 2)

#define SCREEN_TALLADD  (SCREEN_HEIGHT - 224)
#define SCREEN_TALLADD2 (SCREEN_TALLADD / 2)

#define PLANE_WIDEADD ((SCREEN_WIDEADD / 8 + 1) & ~1)
#define PLANE_TALLADD (((SCREEN_TALLADD + 8) / 16) * (PLANE_WIDTH << 1))

// VRAM data
#define VRAM_FG      0xC000 // Foreground nametable
#define VRAM_BG      0xE000 // Fackground nametable
#define VRAM_SONIC   0xF000 // Sonic graphics
#define VRAM_SPRITES 0xF800 // Sprite table
#define VRAM_HSCROLL 0xFC00 // horizontal scroll table

#define PLANE_WIDTH  64
#define PLANE_HEIGHT 32 // NOTE: Changing this doesn't work properly yet
