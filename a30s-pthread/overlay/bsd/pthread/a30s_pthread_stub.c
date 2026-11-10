/*
 * A30S: built-in stub for the pthread kext function table (bring-up only).
 *
 * pthread.kext (libpthread/kern) is not part of this kernel, so pthread_functions stayed NULL
 * and pthread_init() panicked: "pthread kernel extension not loaded (function table is NULL)".
 * This table lets bsd_init() continue. The pthread/psynch/workqueue syscalls return ENOSYS,
 * so user space (launchd) cannot use threads yet; embedding the real libpthread kernel
 * sources is a later step.
 */
#include <stdatomic.h>
#include <kern/debug.h>
#include <kern/task.h>
#include <kern/thread.h>
#include <mach/task.h>
#include <mach/thread_act.h>
#include <sys/param.h>
#include <sys/pthread_shims.h>
#include <sys/proc_internal.h>
#include <sys/proc_info.h>
#include <sys/systm.h>
#include <vm/vm_map.h>
#include <vm/vm_protos.h>

static void a30s_pthread_init(void) {}
static void a30s_pth_proc_hashinit(proc_t p) { (void)p; }
static void a30s_pth_proc_hashdelete(proc_t p) { (void)p; }

static int a30s_bsdthread_create(struct proc *p, user_addr_t f, user_addr_t a, user_addr_t s, user_addr_t t, uint32_t fl, user_addr_t *r) { return ENOSYS; }
static int a30s_bsdthread_register(struct proc *p, user_addr_t ts, user_addr_t wq, int sz, user_addr_t d, user_addr_t tc, uint64_t dq, int32_t *r) { return ENOSYS; }
static int a30s_bsdthread_terminate(struct proc *p, user_addr_t sa, size_t sz, uint32_t kp, uint32_t sem, int32_t *r) { return ENOSYS; }
static int a30s_thread_selfid(struct proc *p, uint64_t *r) { return ENOSYS; }

static int a30s_psynch_mutexwait(proc_t p, user_addr_t m, uint32_t mg, uint32_t ug, uint64_t tid, uint32_t fl, uint32_t *r) { return ENOSYS; }
static int a30s_psynch_mutexdrop(proc_t p, user_addr_t m, uint32_t mg, uint32_t ug, uint64_t tid, uint32_t fl, uint32_t *r) { return ENOSYS; }
static int a30s_psynch_cvbroad(proc_t p, user_addr_t cv, uint64_t ls, uint64_t ud, uint32_t fl, user_addr_t m, uint64_t mg, uint64_t tid, uint32_t *r) { return ENOSYS; }
static int a30s_psynch_cvsignal(proc_t p, user_addr_t cv, uint64_t ls, uint32_t ug, int tp, user_addr_t m, uint64_t mg, uint64_t tid, uint32_t fl, uint32_t *r) { return ENOSYS; }
static int a30s_psynch_cvwait(proc_t p, user_addr_t cv, uint64_t ls, uint32_t ug, user_addr_t m, uint64_t mg, uint32_t fl, int64_t sec, uint32_t nsec, uint32_t *r) { return ENOSYS; }
static int a30s_psynch_cvclrprepost(proc_t p, user_addr_t cv, uint32_t g, uint32_t ug, uint32_t sg, uint32_t pc, uint32_t ps, uint32_t fl, int *r) { return ENOSYS; }
static int a30s_psynch_rw(proc_t p, user_addr_t rw, uint32_t l, uint32_t u, uint32_t wc, int fl, uint32_t *r) { return ENOSYS; }

static int a30s_bsdthread_register2(struct proc *p, user_addr_t ts, user_addr_t wq, uint32_t fl, user_addr_t sh, user_addr_t tc, uint32_t dq, uint32_t tsd, int32_t *r) { return ENOSYS; }

static void a30s_pthread_find_owner(thread_t th, struct stackshot_thread_waitinfo *w) { (void)th; (void)w; }
static void *a30s_pthread_get_thread_kwq(thread_t th) { (void)th; return NULL; }

static int a30s_workq_handle_stack_events(proc_t p, thread_t th, vm_map_t map, user_addr_t sa, mach_port_name_t kp, user_addr_t ev, int n, int uf) { return ENOSYS; }
static int a30s_workq_create_threadstack(proc_t p, vm_map_t vm, mach_vm_offset_t *out) { return ENOSYS; }
static int a30s_workq_destroy_threadstack(proc_t p, vm_map_t vm, mach_vm_offset_t sa) { return ENOSYS; }
static void a30s_workq_setup_thread(proc_t p, thread_t th, vm_map_t map, user_addr_t sa, mach_port_name_t kp, int qos, int sf, int uf) {}
static void a30s_workq_markfree_threadstack(proc_t p, thread_t th, vm_map_t map, user_addr_t sa) {}

static const struct pthread_functions_s a30s_pthread_functions = {
	.version = PTHREAD_FUNCTIONS_TABLE_VERSION,
	.pthread_init = a30s_pthread_init,
	.pth_proc_hashinit = a30s_pth_proc_hashinit,
	.pth_proc_hashdelete = a30s_pth_proc_hashdelete,
	.bsdthread_create = a30s_bsdthread_create,
	.bsdthread_register = a30s_bsdthread_register,
	.bsdthread_terminate = a30s_bsdthread_terminate,
	.thread_selfid = a30s_thread_selfid,
	.psynch_mutexwait = a30s_psynch_mutexwait,
	.psynch_mutexdrop = a30s_psynch_mutexdrop,
	.psynch_cvbroad = a30s_psynch_cvbroad,
	.psynch_cvsignal = a30s_psynch_cvsignal,
	.psynch_cvwait = a30s_psynch_cvwait,
	.psynch_cvclrprepost = a30s_psynch_cvclrprepost,
	.psynch_rw_longrdlock = a30s_psynch_rw,
	.psynch_rw_rdlock = a30s_psynch_rw,
	.psynch_rw_unlock = a30s_psynch_rw,
	.psynch_rw_wrlock = a30s_psynch_rw,
	.psynch_rw_yieldwrlock = a30s_psynch_rw,
	.bsdthread_register2 = a30s_bsdthread_register2,
	.pthread_find_owner = a30s_pthread_find_owner,
	.pthread_get_thread_kwq = a30s_pthread_get_thread_kwq,
	.workq_handle_stack_events = a30s_workq_handle_stack_events,
	.workq_create_threadstack = a30s_workq_create_threadstack,
	.workq_destroy_threadstack = a30s_workq_destroy_threadstack,
	.workq_setup_thread = a30s_workq_setup_thread,
	.workq_markfree_threadstack = a30s_workq_markfree_threadstack,
};

void a30s_pthread_stub_register(void);

void
a30s_pthread_stub_register(void)
{
	pthread_callbacks_t cb = NULL;

	printf("A30S pthread: registering built-in stub function table (bring-up)\n");
	pthread_kext_register(&a30s_pthread_functions, &cb);
}
