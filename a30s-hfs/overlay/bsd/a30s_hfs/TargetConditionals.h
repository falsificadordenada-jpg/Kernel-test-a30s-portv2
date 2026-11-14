/*
 * Minimal <TargetConditionals.h> for the A30S kernel build. The kernel is built
 * with -nostdinc, and the kext view of <kern/debug.h> (used by the HFS sources)
 * includes this header. a30s_hfs_compat.h sets the TARGET_OS_* values HFS needs.
 */
#ifndef A30S_TARGETCONDITIONALS_H
#define A30S_TARGETCONDITIONALS_H
#ifndef TARGET_OS_IPHONE
#define TARGET_OS_IPHONE 1
#endif
#ifndef TARGET_OS_OSX
#define TARGET_OS_OSX 0
#endif
#ifndef TARGET_OS_SIMULATOR
#define TARGET_OS_SIMULATOR 0
#endif
#endif /* A30S_TARGETCONDITIONALS_H */
