#include "MegaDrive.h"

#include "VDP.h"
#include "Joypad.h"

// MegaDrive interface
void MegaDrive_Start(const MD_Header *header)
{
	// Initialize the VDP
	VDP_Init(header);
	Joypad_Init();

	// Run entry point
	header->entry_point();
}
