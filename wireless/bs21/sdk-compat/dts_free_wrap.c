/*
 * Workaround for an Ai-BS21_SDK (libbgtp.a) bug.
 *
 * Root cause:
 *   The closed-source event-scheduler code (evt_sched_ram.c:evt_sched_delete_task)
 *   releases a task node with dts_free(). But task nodes are created by
 *   es_create_task_node() -> es_allocate_eeq_elt(), which serves elements from a
 *   static BSS pool (g_es_eeq_elt_pool, 6 x 32 bytes) and overflows to
 *   dts_malloc() only once the pool is full.
 *
 *   The correct release is es_free_eeq_elt(), which returns static-pool elements
 *   to the pool and calls dts_free() only for heap elements. Calling dts_free()
 *   directly on a static-pool element makes LOS_MemFree reject the out-of-range
 *   address and print:
 *
 *     "<addr> out of range!"
 *     "fail to free memory, pool=[0x20002660], mem=[<addr>]"
 *
 *   The element is then leaked (its in-use flag is never cleared), so later
 *   es_allocate_eeq_elt() calls overflow to the heap.
 *
 * Fix approach:
 *   evt_sched_ram.c is closed source, so we cannot fix the call site. We wrap
 *   dts_free() instead and redirect static-pool addresses to es_free_eeq_elt(),
 *   which already has the correct pool check. dts_free() is the single sink that
 *   every wrong free funnels through, so this shim covers all such call sites
 *   with minimal risk. Heap elements still go to the real dts_free() unchanged.
 *
 * SDK-update note:
 *   This is a workaround, not a permanent fix. After upgrading the SDK, check
 *   whether evt_sched_delete_task still frees the task node with dts_free() by
 *   disassembling libbgtp.a (evt_sched_ram.c.obj): look for "jal dts_free" next
 *   to "jal es_free_eeq_elt". If it now uses es_free_eeq_elt(), this shim is no
 *   longer needed and should be removed. Runtime check: after a board reset, the
 *   "fail to free memory" lines should no longer appear even without this wrap.
 */

#include "soc_osal.h"

extern unsigned char g_es_eeq_elt_pool[];
extern void es_free_eeq_elt(void *elt);

void __real_dts_free(void *ptr);

void __wrap_dts_free(void *ptr)
{
    unsigned long addr = (unsigned long)ptr;
    unsigned long pool = (unsigned long)g_es_eeq_elt_pool;

    /* Static pool spans [pool, pool + 0xC0): 6 elements x 32 bytes. */
    if (addr >= pool && addr < pool + 0xC0) {
        es_free_eeq_elt(ptr);
        return;
    }

    __real_dts_free(ptr);
}
