#include "F4TF4SECapabilityPolicy.h"

#include <cassert>

int main()
{
	using namespace F4T::F4SECapabilities;

	assert(!SupportsSaveFolderName(0, 6, 23));
	assert(!SupportsSaveFolderName(0, 6, 99));
	assert(!SupportsSaveFolderName(0, 7, 0));
	assert(SupportsSaveFolderName(0, 7, 1));
	assert(SupportsSaveFolderName(0, 7, 2));
	assert(SupportsSaveFolderName(0, 8, 0));
	assert(SupportsSaveFolderName(1, 0, 0));

	assert(!SupportsPermanentTasks(0, 6));
	assert(SupportsPermanentTasks(0, 7));
	assert(SupportsPermanentTasks(0, 8));
	assert(SupportsPermanentTasks(1, 0));

	return 0;
}
