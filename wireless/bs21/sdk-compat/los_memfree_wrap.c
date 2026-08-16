/*
 * Workaround for an Ai-BS21_SDK (libbgtp.a / ROM) bug.
 *
 * Root cause:
 *   ROM function evt_prog_finish_eeq_isr() (0x1aa3e) releases an event-scheduler
 *   element by calling LOS_MemFree() directly, bypassing es_free_eeq_elt(). The
 *   element comes from es_allocate_eeq_elt()'s static BSS pool
 *   (g_es_eeq_elt_pool, 6 x 32 bytes), so LOS_MemFree rejects the out-of-range
 *   address and prints:
 *
 *     "<addr> out of range!"
 *     "fail to free memory, pool=[0x20002660], mem=[<addr>]"
 *
 *   A correct RAM patch exists (evt_prog_finish_eeq_isr_patch uses
 *   es_free_eeq_elt), but the SDK patch mechanism does not remap
 *   evt_prog_finish_eeq_isr on this build, so the ROM version runs.
 *
 * Fix approach:
 *   ROM and SDK libraries are closed source, so we cannot fix the call site.
 *   We wrap LOS_MemFree() instead and redirect static-pool addresses to
 *   es_free_eeq_elt(), which returns them to the pool and only calls
 *   LOS_MemFree() for heap elements. This shim is the single sink every wrong
 *   free funnels through, so it covers all such call sites with minimal risk.
 *   Heap elements still go to the real LOS_MemFree() unchanged.
 *
 * SDK-update note:
 *   This is a workaround, not a permanent fix. After upgrading the SDK, check
 *   whether evt_prog_finish_eeq_isr is remapped to its RAM patch (which uses
 *   es_free_eeq_elt): disassemble libbgtp.a / the ROM and look for a LOS_MemFree
 *   call at evt_prog_finish_eeq_isr+0x34 (0x1aa72). If the patch now takes
 *   effect, this shim is no longer needed and should be removed. Runtime check:
 *   after a board reset, the "fail to free memory" lines should no longer appear
 *   even without this wrap.
 */

#include "soc_osal.h"

extern unsigned char g_es_eeq_elt_pool[];
extern void es_free_eeq_elt(void *elt);

unsigned int __real_LOS_MemFree(void *pool, void *mem);

unsigned int __wrap_LOS_MemFree(void *pool, void *mem)
{
    unsigned int addr = (unsigned int)mem;
    unsigned int base = (unsigned int)g_es_eeq_elt_pool;

    /* Static pool spans [base, base + 0xC0): 6 elements x 32 bytes. */
    if (addr >= base && addr < base + 0xC0) {
        es_free_eeq_elt(mem);
        return 0;
    }

    return __real_LOS_MemFree(pool, mem);
}
