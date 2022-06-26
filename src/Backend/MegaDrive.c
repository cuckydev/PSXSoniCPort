#include "MegaDrive.h"

//MegaDrive interface
void MegaDrive_Start(const MD_Header *header)
{
	//Run entry point
	header->entry_point();
}
