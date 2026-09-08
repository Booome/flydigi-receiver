/* ROM evt_prog_finish_eeq_isr frees pool memory via LOS_MemFree (bug); this shim
 * redirects it. Removal test & full analysis: docs/los-memfree-wrap-workaround.md */

#include "soc_osal.h"

extern unsigned char g_es_eeq_elt_pool[];
extern void es_free_eeq_elt(void *elt);

unsigned int __real_LOS_MemFree(void *pool, void *mem);

unsigned int __wrap_LOS_MemFree(void *pool, void *mem) {
    unsigned int addr = (unsigned int)mem;
    unsigned int base = (unsigned int)g_es_eeq_elt_pool;

    /* Static pool spans [base, base + 0xC0): 6 elements x 32 bytes. */
    if (addr >= base && addr < base + 0xC0) {
        es_free_eeq_elt(mem);
        return 0;
    }

    return __real_LOS_MemFree(pool, mem);
}
