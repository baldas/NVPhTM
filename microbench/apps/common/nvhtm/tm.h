#ifndef TM_H
#define TM_H 1

#include <nh.h>

#define TM_START(tid, ro)  NH_begin()

#define TM_COMMIT  NH_commit()

#define TM_MALLOC(size)                    NH_alloc(size)
#define TM_FREE(addr)                      NH_free(addr)
#define TM_FREE2(addr, size)               NH_free(addr)

#define SEQ_MALLOC(size)                   malloc(size)
#define SEQ_FREE(addr)                     free(addr)
#define P_MALLOC(size)                     NH_alloc(size)
#define P_FREE(addr)                       NH_free(addr)

#define TM_INIT(nThreads)    \
	NVHTM_init(nThreads);      \
	NVHTM_start_stats();       \
  printf("Budget=%i\n", HTM_SGL_INIT_BUDGET); \
  HTM_set_budget(HTM_SGL_INIT_BUDGET)

#define TM_EXIT(nThreads)    \
	NVHTM_end_stats();         \
  NVHTM_shutdown()

#define TM_INIT_THREAD(tid)  \
	set_affinity(tid);         \
  NVHTM_thr_init();          \
  HTM_set_budget(HTM_SGL_INIT_BUDGET)

#define TM_EXIT_THREAD(tid)  \
	NVHTM_thr_exit()


#define TM_ARGDECL_ALONE               /* Nothing */
#define TM_ARGDECL                     /* Nothing */
#define TM_ARG                         /* Nothing */
#define TM_ARG_ALONE                   /* Nothing */
#define TM_CALLABLE                    TM_SAFE

#define TM_SHARED_READ(var)            NH_read(&(var))
#define TM_SHARED_READ_P(var)          NH_read_P(&(var))

#define TM_SHARED_WRITE(var, val)      NH_write(&(var), val)
#define TM_SHARED_WRITE_P(var, val)    NH_write_P(&(var), val)

#endif /* TM_H */
