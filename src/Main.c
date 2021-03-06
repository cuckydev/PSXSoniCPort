#include "Backend/MegaDrive.h"

#include "Game.h"

// Sonic 1 ROM header
static const MD_Header s1_header = {
	// Vectors
	/* Start of program     */ EntryPoint,
	/* Horizontal interrupt */ HBlank,
	/* Vertical interrupt   */ VBlank,
	
	// Game information
	/* Game title           */ "SONIC THE HEDGEHOG",
};

// MegaDrive entry point
void main()
{
	MegaDrive_Start(&s1_header);
}
