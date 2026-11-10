/*
 * A30S platform expert (built into the kernel, replaces the closed AppleARMPE kext).
 * Matches the ADT root node "A30S,Exynos7904" (see mkadt.py) with a higher probe score
 * than IOPanicPlatform, so the kernel stops panicking with
 * "Unable to find driver for this platform".
 *
 * Bring-up only: it publishes the ADT nubs (IOPlatformDevice) and nothing else.
 */
#include <IOKit/IOPlatformExpert.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOService.h>

class A30SPlatformExpert : public IODTPlatformExpert
{
	OSDeclareDefaultStructors(A30SPlatformExpert);

public:
	virtual bool start(IOService * provider) APPLE_KEXT_OVERRIDE;
	virtual const char * deleteList(void) APPLE_KEXT_OVERRIDE;
	virtual const char * excludeList(void) APPLE_KEXT_OVERRIDE;
};

#undef super
#define super IODTPlatformExpert

OSDefineMetaClassAndStructors(A30SPlatformExpert, IODTPlatformExpert);

bool
A30SPlatformExpert::start(IOService * provider)
{
	IOLog("A30S platform expert: start (provider %s)\n", provider ? provider->getName() : "?");
	if (!super::start(provider)) {
		return false;
	}
	/* IOPlatformExpert::start already did setPlatform(this) and PMInstantiatePowerDomains() */
	registerService();
	IOLog("A30S platform expert: registered\n");
	return true;
}

const char *
A30SPlatformExpert::deleteList(void)
{
	return "('packages')";
}

const char *
A30SPlatformExpert::excludeList(void)
{
	/* nodes that are consumed by the kernel itself, not published as nubs */
	return "('chosen', 'memory-map', 'defaults', 'options', 'cpus')";
}
