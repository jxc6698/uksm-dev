#ifndef __LINUX_UKSM_H
#define __LINUX_UKSM_H
/*
 * Memory merging support.
 *
 * This code enables dynamic sharing of identical pages found in different
 * memory areas, even if they are not shared by fork().
 */

/* if !CONFIG_UKSM this file should not be compiled at all. */
#ifdef CONFIG_UKSM

#include <linux/bitops.h>
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/rmap.h>
#include <linux/sched.h>

#include <linux/ksm.h>



extern wait_queue_head_t uksm_frontswap_wait ;
extern struct task_struct *uksm_task ;
extern struct mutex uksm_frontswap_wait_mutex ;
//extern spinlock_t uksm_run_data_lock ;
extern struct raw_spinlock uksm_run_data_lock ;
extern long uksm_run_data_lock_flag ;

#define UKSM_ZSWAP

#ifdef UKSM_ZSWAP

#define UKSM_IN  (0x00000001 << 16)


/**
*	already held uksm_run_data_lock 
*/
static int test_uksm_in(void)
{
	return uksm_run_data_lock_flag & UKSM_IN ;
}
static long get_uksm_wait_num(void)
{
	//	equals uksm_run_data_lock_flag
	return (uksm_run_data_lock_flag & (~UKSM_IN) ) >= 0 ? (uksm_run_data_lock_flag & (~UKSM_IN) ) : -1 ;
}
static int add_uksm_wait(void)
{
	uksm_run_data_lock_flag ++ ;
	return uksm_run_data_lock_flag ;
}
static int set_uksm_in(void)
{
	uksm_run_data_lock_flag |= UKSM_IN ;
	return uksm_run_data_lock_flag ;
}
static int set_uksm_out(void)
{
//	return uksm_run_data_lock_flag &= (~UKSM_IN) ;
	uksm_run_data_lock_flag = 0 ;
	return uksm_run_data_lock_flag ;
}

#else


static int test_uksm_in(void)
{
	return 0 ;
}
static long get_uksm_wait_num(void)
{
	return 0 ;
}

static int add_uksm_wait(void)
{
	return 0 ;
}

static int set_uksm_in(void)
{
	return 1 ;
}
static int set_uksm_out(void)
{
	return 0 ;
}
#endif


#define SCAN_LADDER_SIZE 4


struct uksm_real_runtime_flags{
	/*	
	*	flags = 1 , runtime data haa been backup
	*	in case uksm_do_scan runs, another process put itselt to the wait list
	*/
	unsigned int flags ;
	int cpu_ratio[ SCAN_LADDER_SIZE ] ;
	unsigned int cover_msecs[ SCAN_LADDER_SIZE ] ;
	unsigned int max_cpu ;
	unsigned int uksm_cpu_governor ;

	unsigned long long uksm_eval_round ;
	unsigned long uksm_sleep_real ;
};

#undef SCAN_LADDER_SIZE

extern struct uksm_real_runtime_flags uksm_runtime_backup ;
extern void save_uksm_runtime_data(void) ;
extern void restore_uksm_runtime_data(void) ;
extern int test_uksm_backup(void) ;
extern void save_sleep_time(void) ;
extern void clear_sleep_time(void) ;
extern int judge_whether_sleep(void) ;
extern int whether_wake_up(void) ;



extern unsigned long zero_pfn __read_mostly;
extern unsigned long uksm_zero_pfn __read_mostly;
extern struct page *empty_uksm_zero_page;



/* must be done before linked to mm */
extern void uksm_vma_add_new(struct vm_area_struct *vma);
extern void uksm_remove_vma(struct vm_area_struct *vma);

#define UKSM_SLOT_NEED_SORT	(1 << 0)
#define UKSM_SLOT_NEED_RERAND 	(1 << 1)
#define UKSM_SLOT_SCANNED     	(1 << 2) /* It's scanned in this round */
#define UKSM_SLOT_FUL_SCANNED 	(1 << 3)
#define UKSM_SLOT_IN_UKSM 	(1 << 4)

struct vma_slot {
	struct sradix_tree_node *snode;
	unsigned long sindex;

	struct list_head slot_list;
	unsigned long fully_scanned_round;
	unsigned long dedup_num;
	unsigned long pages_scanned;
	unsigned long last_scanned;
	unsigned long pages_to_scan;
	struct scan_rung *rung;
	struct page **rmap_list_pool;
	unsigned int *pool_counts;
	unsigned long pool_size;
	struct vm_area_struct *vma;
	struct mm_struct *mm;
	unsigned long ctime_j;
	unsigned long pages;
	unsigned long flags;
	unsigned long pages_cowed; /* pages cowed this round */
	unsigned long pages_merged; /* pages merged this round */
	unsigned long pages_bemerged;

	/* when it has page merged in this eval round */
	struct list_head dedup_list;
};

static inline void uksm_unmap_zero_page(pte_t pte)
{
	if (pte_pfn(pte) == uksm_zero_pfn)
		__dec_zone_page_state(empty_uksm_zero_page, NR_UKSM_ZERO_PAGES);
}

static inline void uksm_map_zero_page(pte_t pte)
{
	if (pte_pfn(pte) == uksm_zero_pfn)
		__inc_zone_page_state(empty_uksm_zero_page, NR_UKSM_ZERO_PAGES);
}

static inline void uksm_cow_page(struct vm_area_struct *vma, struct page *page)
{
	if (vma->uksm_vma_slot && PageKsm(page))
		vma->uksm_vma_slot->pages_cowed++;
}

static inline void uksm_cow_pte(struct vm_area_struct *vma, pte_t pte)
{
	if (vma->uksm_vma_slot && pte_pfn(pte) == uksm_zero_pfn)
		vma->uksm_vma_slot->pages_cowed++;
}

static inline int uksm_flags_can_scan(unsigned long vm_flags)
{
#ifndef VM_SAO
#define VM_SAO 0
#endif
	return !(vm_flags & (VM_PFNMAP | VM_IO  | VM_DONTEXPAND |
			     VM_HUGETLB | VM_NONLINEAR | VM_MIXEDMAP |
			     VM_SHARED  | VM_MAYSHARE | VM_GROWSUP | VM_GROWSDOWN | VM_SAO));
}

static inline void uksm_vm_flags_mod(unsigned long *vm_flags_p)
{
	if (uksm_flags_can_scan(*vm_flags_p))
		*vm_flags_p |= VM_MERGEABLE;
}

/*
 * Just a wrapper for BUG_ON for where ksm_zeropage must not be. TODO: it will
 * be removed when uksm zero page patch is stable enough.
 */
static inline void uksm_bugon_zeropage(pte_t pte)
{
	BUG_ON(pte_pfn(pte) == uksm_zero_pfn);
}
#else
static inline void uksm_vma_add_new(struct vm_area_struct *vma)
{
}

static inline void uksm_remove_vma(struct vm_area_struct *vma)
{
}

static inline void uksm_unmap_zero_page(pte_t pte)
{
}

static inline void uksm_map_zero_page(pte_t pte)
{
}

static inline void uksm_cow_page(struct vm_area_struct *vma, struct page *page)
{
}

static inline void uksm_cow_pte(struct vm_area_struct *vma, pte_t pte)
{
}

static inline int uksm_flags_can_scan(unsigned long vm_flags)
{
	return 0;
}

static inline void uksm_vm_flags_mod(unsigned long *vm_flags_p)
{
}

static inline void uksm_bugon_zeropage(pte_t pte)
{
}
#endif /* !CONFIG_UKSM */
#endif /* __LINUX_UKSM_H */
