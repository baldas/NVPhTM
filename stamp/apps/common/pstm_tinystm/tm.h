#ifndef TM_H
#define TM_H 1

#define MAIN(argc, argv)            int main (int argc, char** argv)
#define MAIN_RETURN(val)            return val

#define GOTO_SIM()                  /* nothing */
#define GOTO_REAL()                 /* nothing */
#define IS_IN_SIM()                 (0)

#define SIM_GET_NUM_CPU(var)        /* nothing */

#define P_MEMORY_STARTUP(numThread) /* nothing */
#define P_MEMORY_SHUTDOWN()         /* nothing */

#include <stm.h>
#include <mod_mem.h>
#include <mod_stats.h>

#include <min_nvm.h>

#if defined(__x86_64__) || defined(__i386)
#include <msr.h>
#include <pmu.h>
#else
#define msrInitialize()         		/* nothing */
#define msrTerminate()          		/* nothing */
#endif

#define TM_ARG                      /* nothing */
#define TM_ARG_ALONE                /* nothing */
#define TM_ARGDECL                  /* nothing */
#define TM_ARGDECL_ALONE            /* nothing */
#define TM_SAFE                     /* nothing */
#define TM_PURE                     /* nothing */

#define SEQ_MALLOC(size)            malloc(size)
#define SEQ_FREE(ptr)               free(ptr)
#define P_MALLOC(size)              malloc(size)
#define P_FREE(ptr)                 free(ptr)
#define TM_MALLOC(size)             stm_malloc(size)
/* Note that we only lock the first word and not the complete object */
#define TM_FREE(ptr)                stm_free(ptr, sizeof(stm_word_t))

/**
 *  Pesistency delay emulation macros
 */
#define PSTM_COMMIT_MARKER \
	MN_count_writes++;       \
  MN_count_writes++;       \
  SPIN_PER_WRITE(1);       \
  SPIN_PER_WRITE(1);

// TODO: is crashing in TPC-C
#define PSTM_LOG_ENTRY(addr, val) \
	{ \
		/*int log_buf = (int)val;*/ \
		MN_count_writes += 2; \
		/*memcpy(PSTM_log + PSTM_log_ptr, &log_buf, sizeof(int));*/ \
		SPIN_PER_WRITE(1); \
		/*PSTM_log_ptr += sizeof(int); \
		if (PSTM_log_ptr >= PSTM_LOG_SIZE - sizeof(int)) PSTM_log_ptr = 0;*/\
	}

#define STM_ON_ABORT() \
	{ \
		int abort_marker = 1, val_buf = 1; \
		PSTM_LOG_ENTRY(&abort_marker, val_buf); \
	}


#if STM_VERSION_NB <= 103
# define TM_START(ro, tx_count)     do { \
																			int is_abort = 0; \
                                      sigjmp_buf *_e; \
                                      stm_tx_attr_t _a = {0, ro}; \
                                      _e = stm_start(&_a, tx_count); \
                                      sigsetjmp(*_e, 0); \
																			is_abort += 1; \
																			if (is_abort > 1) STM_ON_ABORT(); \
                                    } while (0)
#else /* STM_VERSION_NB > 103 */
# define TM_START(ro, tx_count)			do { \
																			int is_abort = 0; \
                                      sigjmp_buf *_e; \
                                      stm_tx_attr_t _a = {{.id = 0, .read_only = ro, .visible_reads = 0}}; \
                                      _e = stm_start(_a, tx_count); \
                                      sigsetjmp(*_e, 0); \
																			is_abort += 1; \
																			if (is_abort > 1) STM_ON_ABORT(); \
                                    } while (0)
#endif /* STM_VERSION_NB > 103 */

#if defined(COMMIT_RATE_PROFILING) || defined(RW_SET_PROFILING)
#include <locale.h>
extern __thread long __txId__;
#ifdef COMMIT_RATE_PROFILING
#define TM_STARTUP(numThread)       msrInitialize(); \
  																	MN_learn_nb_nops();      \
																		pmuStartup(NUMBER_OF_TRANSACTIONS); \
																		coreSTM_commits = (unsigned int **)malloc(sizeof(unsigned int *)*numThread); \
																		coreSTM_aborts  = (unsigned int **)malloc(sizeof(unsigned int *)*numThread); \
																		stm_init(); \
                                    mod_mem_init(0);

extern unsigned int **coreSTM_commits;
extern unsigned int **coreSTM_aborts;

#define TM_SHUTDOWN()								setlocale(LC_ALL, ""); \
		int __numThreads__  = thread_getNumThread(); \
		int nfixedCounters  = pmuNumberOfFixedCounters(); \
		int ncustomCounters = pmuNumberOfCustomCounters(); \
		int ntotalCounters  = nfixedCounters + ncustomCounters; \
		int nmeasurements = pmuNumberOfMeasurements(); \
		int ii; \
		printf("\nTx #  | %10s | %19s | %24s | %24s | %24s", \
		"STM START", "STM COMMIT", "INSTRUCTIONS", "CYCLES", "CYCLES REF"); \
		for(ii=0; ii < __numThreads__; ii++){ \
			uint64_t **measurements = pmuGetMeasurements(ii); \
			printf("\nThread %d\n",ii); \
			int i, j; \
			uint64_t total[3] = {0,0,0}; \
			for(j=ncustomCounters; j < ntotalCounters; j++) \
				for(i=0; i < nmeasurements; i++) \
					total[j-ncustomCounters] += measurements[i][j]; \
			for(i=0; i < nmeasurements; i++){ \
				printf("Tx %2d",i); \
				uint64_t start = coreSTM_commits[ii][i] + coreSTM_aborts[ii][i]; \
				uint64_t commit = coreSTM_commits[ii][i]; \
				printf(" | %'10lu | %'10lu (%'6.2lf)", start, commit, 100.0*((double)commit/(double)start)); \
				for(j=ncustomCounters; j < ntotalCounters; j++){ \
					printf(" | %'15lu ",measurements[i][j]); \
					printf("(%'6.2lf)", 100.0*((double)measurements[i][j]/(double)total[j-ncustomCounters])); \
				} \
				printf("\n"); \
			} \
		} \
		printf("\n|===============%38s%s%38s===============|\n","", "END OF REPORT", " "); \
		printf("\n=== PSTM stats\n"); \
		printf("MN_count_spins_total : %lli\n", MN_count_spins_total); \
		printf("MN_count_writes_to_PM_total : %lli\n", MN_count_writes_to_PM_total); \
																		stm_exit(); \
																		pmuShutdown(); \
																		msrTerminate()

#define TM_THREAD_ENTER()           long __threadId__ = thread_getId(); \
																	  MN_thr_enter(); \
																		stm_init_thread(NUMBER_OF_TRANSACTIONS); \
																		set_affinity(__threadId__)

#define TM_THREAD_EXIT()            if(stm_get_stats("nb_commits",&coreSTM_commits[__threadId__]) == 0){ \
																			fprintf(stderr,"error: get nb_commits failed!\n"); \
																		} \
																		if(stm_get_stats("nb_aborts",&coreSTM_aborts[__threadId__]) == 0){ \
																			fprintf(stderr,"error: get nb_aborts failed!\n"); \
																		} \
																		MN_thr_exit(); \
																		stm_exit_thread()

#define TM_BEGIN()									__txId__ = __COUNTER__; \
																		pmuStartCounting(__threadId__, __txId__); \
																		TM_START(0, __txId__)
#define TM_BEGIN_RO()               __txId__ = __COUNTER__; \
																		pmuStartCounting(__threadId__, __txId__); \
																		TM_START(1, __txId__)
#define TM_END()                    stm_commit(); \
																		PSTM_COMMIT_MARKER; \
																		pmuStopCounting(__threadId__)
#endif /* COMMIT_RATE_PROFILING */

#ifdef RW_SET_PROFILING
#define TM_STARTUP(numThread)       msrInitialize(); \
  																	MN_learn_nb_nops();      \
																		pmuStartup(NUMBER_OF_TRANSACTIONS); \
																		coreSTM_r_set_size = (unsigned int ***)malloc(sizeof(unsigned int **)*numThread); \
																		coreSTM_w_set_size = (unsigned int ***)malloc(sizeof(unsigned int **)*numThread); \
																		coreSTM_counter    = (unsigned int **)malloc(sizeof(unsigned int *)*numThread); \
																		stm_init(); \
                                    mod_mem_init(0);

extern unsigned int ***coreSTM_r_set_size;
extern unsigned int ***coreSTM_w_set_size;
extern unsigned int **coreSTM_counter;

#define TM_SHUTDOWN() setlocale(LC_ALL, ""); \
		{                                                         \
			long i, __numThreads__  = thread_getNumThread();         \
			for(i=0; i < __numThreads__; i++) {                     \
				long j;                                               \
				printf("Thread %ld\n",i);                             \
		  	for(j=0; j < NUMBER_OF_TRANSACTIONS; j++) {           \
					printf("Tx %2ld\n", j);                             \
					long k;                                             \
					for(k=0; k < coreSTM_counter[i][j]; k++)            \
						printf("%u %u\n", coreSTM_r_set_size[i][j][k]   \
							,coreSTM_w_set_size[i][j][k]);                  \
					free(coreSTM_r_set_size[i][j]);                     \
					free(coreSTM_w_set_size[i][j]);                     \
				}                                                     \
				free(coreSTM_counter[i]);                             \
				free(coreSTM_r_set_size[i]);                          \
				free(coreSTM_w_set_size[i]);                          \
			}                                                       \
			free(coreSTM_counter);                                  \
			free(coreSTM_r_set_size);                               \
			free(coreSTM_w_set_size);                               \
		}                                                         \
		printf("\n=== PSTM stats\n"); \
		printf("MN_count_spins_total : %lli\n", MN_count_spins_total); \
		printf("MN_count_writes_to_PM_total : %lli\n", MN_count_writes_to_PM_total); \
											stm_exit();            \
										  pmuShutdown();         \
											msrTerminate()

#define TM_THREAD_ENTER()           long __threadId__ = thread_getId(); \
  																	MN_thr_enter(); \
																		stm_init_thread(NUMBER_OF_TRANSACTIONS); \
																		set_affinity(__threadId__)

#define TM_THREAD_EXIT()            if(stm_get_stats("r_set_nb_entries",&coreSTM_r_set_size[__threadId__]) == 0){ \
																			fprintf(stderr,"error: get nb_commits failed!\n"); \
																		} \
																		if(stm_get_stats("w_set_nb_entries",&coreSTM_w_set_size[__threadId__]) == 0){ \
																			fprintf(stderr,"error: get nb_aborts failed!\n"); \
																		} \
																		if(stm_get_stats("tx_count_counter",&coreSTM_counter[__threadId__]) == 0){ \
																			fprintf(stderr,"error: get nb_aborts failed!\n"); \
																		} \
  																	MN_thr_exit(); \
																		stm_exit_thread()

#define TM_BEGIN()									__txId__ = __COUNTER__; \
																		pmuStartCounting(__threadId__, __txId__); \
																		TM_START(0, __txId__)
#define TM_BEGIN_RO()               __txId__ = __COUNTER__; \
																		pmuStartCounting(__threadId__, __txId__); \
																		TM_START(1, __txId__)
#define TM_END()                    stm_commit(); \
																		PSTM_COMMIT_MARKER; \
																		pmuStopCounting(__threadId__)

#endif  /* RW_SET_PROFILING */

#else /* NO PROFILING */
#define TM_STARTUP(numThread)       msrInitialize(); \
  																	MN_learn_nb_nops();      \
																		stm_init(); \
                                    mod_mem_init(0)
#define TM_SHUTDOWN()               stm_exit(); \
	                                  printf("TOTAL_WRITES          %lli\n", MN_count_writes_to_PM_total); \
                                    printf("TOTAL_SPINS (workers) %lli\n", MN_count_spins_total); \
																		msrTerminate()

#define TM_THREAD_ENTER()           long __threadId__ = thread_getId(); \
  																	MN_thr_enter(); \
																		stm_init_thread(NUMBER_OF_TRANSACTIONS); \
																		set_affinity(__threadId__)

#define TM_THREAD_EXIT()            MN_thr_exit(); \
																		stm_exit_thread()


#define TM_BEGIN()									TM_START(0, __COUNTER__)
#define TM_BEGIN_RO()               TM_START(1, __COUNTER__)
#define TM_END()                    stm_commit(); PSTM_COMMIT_MARKER
#endif /* NO PROFILING */

#define TM_RESTART()                stm_abort(0)
#define TM_EARLY_RELEASE(var)       /* nothing */

#include <wrappers.h>

/* We could also map macros to the stm_(load|store)_long functions if needed */

#define TM_SHARED_READ(var)           stm_load((volatile stm_word_t *)(void *)&(var))
#define TM_SHARED_READ_P(var)         stm_load_ptr((volatile void **)(void *)&(var))
#define TM_SHARED_READ_F(var)         stm_load_float((volatile float *)(void *)&(var))

#define TM_SHARED_WRITE(var, val)     \
	PSTM_LOG_ENTRY(&(var), val); \
	stm_store((volatile stm_word_t *)(void *)&(var), (stm_word_t)val)
#define TM_SHARED_WRITE_P(var, val)   \
	PSTM_LOG_ENTRY(&(var), val); \
	stm_store_ptr((volatile void **)(void *)&(var), val)
#define TM_SHARED_WRITE_F(var, val)   \
	PSTM_LOG_ENTRY(&(var), val); \
	stm_store_float((volatile float *)(void *)&(var), val)

/* TODO: test with mod_log */
#define TM_LOCAL_WRITE(var, val)      var = val
#define TM_LOCAL_WRITE_P(var, val)    var = val
#define TM_LOCAL_WRITE_F(var, val)    var = val

#define TM_IFUNC_DECL
#define TM_IFUNC_CALL1(r, f, a1)      r = f(a1)
#define TM_IFUNC_CALL2(r, f, a1, a2)  r = f((a1), (a2))

#endif /* TM_H */

#ifdef MAIN_FUNCTION_FILE
#if defined(COMMIT_RATE_PROFILING)
__thread long __txId__;
unsigned int **coreSTM_commits;
unsigned int **coreSTM_aborts;
#elif defined(RW_SET_PROFILING)
__thread long __txId__;
unsigned int ***coreSTM_r_set_size;
unsigned int ***coreSTM_w_set_size;
unsigned int **coreSTM_counter;
#else
#endif
#endif /* MAIN_FUNCTION_FILE */
