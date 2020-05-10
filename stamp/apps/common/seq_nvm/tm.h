#ifndef TM_H
#define TM_H 1

#define MAIN(argc, argv)              int main (int argc, char** argv)
#define MAIN_RETURN(val)              return val

#define GOTO_SIM()                    /* nothing */
#define GOTO_REAL()                   /* nothing */
#define IS_IN_SIM()                   (0)

#define SIM_GET_NUM_CPU(var)          /* nothing */

#define P_MEMORY_STARTUP(numThread)   /* nothing */
#define P_MEMORY_SHUTDOWN()           /* nothing */

#include <assert.h>

#if defined(__x86_64__) || defined(__i386)
#include <msr.h>
#else
#define msrInitialize()         			/* nothing */
#define msrTerminate()          			/* nothing */
#endif

#include <string.h>

#define TM_ARG                        /* nothing */
#define TM_ARG_ALONE                  /* nothing */
#define TM_ARGDECL                    /* nothing */
#define TM_ARGDECL_ALONE              /* nothing */
#define TM_PURE                       /* nothing */
#define TM_SAFE                       /* nothing */


#include <min_nvm.h>

//#define TM_STARTUP(numThread)         msrInitialize()
#define TM_STARTUP(numThread)	msrInitialize(); \
                              MN_learn_nb_nops()

#define TM_SHUTDOWN()	        printf("TOTAL_WRITES          %lli\n", MN_count_writes_to_PM_total); \
                              printf("TOTAL_SPINS (workers) %lli\n", MN_count_spins_total); \
											        msrTerminate()

#define TM_THREAD_ENTER()     MN_thr_enter()

#define TM_THREAD_EXIT()      MN_thr_exit()

#define SEQ_MALLOC(size)              malloc(size)
#define SEQ_FREE(ptr)                 free(ptr)
#define P_MALLOC(size)                malloc(size)
#define P_FREE(ptr)                   free(ptr)
#define TM_MALLOC(size)               malloc(size)
#define TM_FREE(ptr)                  free(ptr)




/**
 *  Persistency delay emulation macros 
 */
#define PSTM_COMMIT_MARKER \
	MN_count_writes++;       \
  MN_count_writes++;       \
  SPIN_PER_WRITE(1);       \
  SPIN_PER_WRITE(1);

#define PSTM_LOG_ENTRY(addr, val) \
	{ \
		MN_count_writes += 2; \
		SPIN_PER_WRITE(1); \
	}



#define TM_BEGIN()                    /* nothing */
#define TM_BEGIN_RO()                 /* nothing */
#define TM_END()                      PSTM_COMMIT_MARKER
#define TM_RESTART()                  assert(0)

#define TM_EARLY_RELEASE(var)         /* nothing */

#define TM_SHARED_READ(var)           (var)
#define TM_SHARED_READ_P(var)         (var)
#define TM_SHARED_READ_F(var)         (var)

//#define TM_SHARED_WRITE(var, val)     var = val
//#define TM_SHARED_WRITE_P(var, val)   var = val
//#define TM_SHARED_WRITE_F(var, val)   var = val


// We have an extra SPIN_PER_WRITE to emulate the write to the NVM
// (writethrough)

#define TM_SHARED_WRITE(var, val)     \
  PSTM_LOG_ENTRY(&(var), val); \
  SPIN_PER_WRITE(1); \
  var = val
#define TM_SHARED_WRITE_P(var, val)   \
  PSTM_LOG_ENTRY(&(var), val); \
  SPIN_PER_WRITE(1); \
  var = val
#define TM_SHARED_WRITE_F(var, val)   \
  PSTM_LOG_ENTRY(&(var), val); \
  SPIN_PER_WRITE(1); \
  var = val

#define TM_LOCAL_WRITE(var, val)      var = val
#define TM_LOCAL_WRITE_P(var, val)    var = val
#define TM_LOCAL_WRITE_F(var, val)    var = val

#define TM_IFUNC_DECL                 /* nothing */
#define TM_IFUNC_CALL1(r, f, a1)      r = f(a1)
#define TM_IFUNC_CALL2(r, f, a1, a2)  r = f((a1), (a2))

#endif /* TM_H */

