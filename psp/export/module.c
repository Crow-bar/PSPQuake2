#include <pspkernel.h>

PSP_MODULE_INFO ("gamepsp", PSP_MODULE_USER, 1, 1);

void *GetGameAPI (void *import);

int module_start (SceSize arglen, void *argp)
{
	void **expptr;

	expptr  = (void **)((unsigned int *)argp)[0];
	*expptr = (void *)GetGameAPI;

	return 0;
}

int module_stop (SceSize arglen, void *argp)
{
	return 0;
}
