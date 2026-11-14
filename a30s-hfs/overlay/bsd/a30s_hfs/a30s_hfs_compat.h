/*
 * Compile Apple's HFS kext sources inside the A30S kernel the way the kext is
 * built: with the kext (KPI) view of the kernel headers, and with only HFS's own
 * feature macros.  The kernel's -DCONFIG_* options are not visible to a kext
 * build, so the ones HFS tests are cleared first.
 *
 * This is the iOS configuration of core/kext-config.h with content protection
 * off (there is no AppleKeyStore here).  kext-config.h itself is bypassed by
 * defining its include guard, because <TargetConditionals.h> is not available
 * to a -nostdinc kernel build.
 */
#ifndef A30S_HFS_COMPAT_H
#define A30S_HFS_COMPAT_H

#undef BSD_KERNEL_PRIVATE
#undef XNU_KERNEL_PRIVATE

#undef CONFIG_PROTECT
#undef CONFIG_SEARCHFS
#undef CONFIG_HFS_DIRLINK
#undef CONFIG_SECLUDED_RENAME
#undef CONFIG_MACF
#undef CONFIG_HFS_RBTREE
#undef QUOTA
#undef NAMEDSTREAMS
#undef FIFO
#undef HFS_COMPRESSION
#undef TARGET_OS_IPHONE
#undef TARGET_OS_OSX
#undef TARGET_OS_SIMULATOR

#define _hfs_config_            /* skip core/kext-config.h */
#define TARGET_OS_IPHONE 1
#define TARGET_OS_OSX 0
#define TARGET_OS_SIMULATOR 0
#define HFS_COMPRESSION 1
#define FIFO 1
#define QUOTA 0
#define CONFIG_PROTECT 0
#define CONFIG_SECLUDED_RENAME 0

/*
 * hfs_readwrite.c tests FSIOC_CAS_BSDFLAGS, which xnu-4903's <sys/fsctl.h> does
 * not define (its handler body is compiled out).  Give it the value newer
 * xnu uses so the case label compiles; nothing else uses it.
 */
#ifndef FSIOC_CAS_BSDFLAGS
#define FSIOC_CAS_BSDFLAGS _IOWR('A', 2, uint64_t[2])
#endif

/* Built in, not a loadable kext (see a30s_hfs_glue.c). */
#define OSKextGetCurrentLoadTag a30s_hfs_current_load_tag

#endif /* A30S_HFS_COMPAT_H */
