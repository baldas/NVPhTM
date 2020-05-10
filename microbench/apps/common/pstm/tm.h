/*
 * File:
 *   tm.h
 * Author(s):
 *   Pascal Felber <pascal.felber@unine.ch>
 *   Patrick Marlier <patrick.marlier@unine.ch>
 * Description:
 *   Empty file (to avoid source modifications red-black tree).
 *
 * Copyright (c) 2007-2012.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation, version 2
 * of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * This program has a dual license and can also be distributed
 * under the terms of the MIT license.
 */

#include <stm.h>
#include <mod_mem.h>
#include <mod_ab.h>
#include <mod_stats.h>

#include <min_nvm.h>

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

/*
 * Useful macros to work with transactions. Note that, to use nested
 * transactions, one should check the environment returned by
 * stm_get_env() and only call sigsetjmp() if it is not null.
 */
#define TM_START(tid, ro)  \
	{ \
		int is_abort = 0; \
		stm_tx_attr_t _a = {{.id = tid, .read_only = ro}}; \
    sigjmp_buf *_e = stm_start(_a, 0); \
    if (_e != NULL) sigsetjmp(*_e, 0); \
		is_abort += 1; \
		if (is_abort > 1) STM_ON_ABORT();

#define TM_LOAD(addr)                      stm_load((stm_word_t *)addr)
#define TM_UNIT_LOAD(addr, ts)             stm_unit_load((stm_word_t *)addr, ts)
#define TM_STORE(addr, value)              stm_store((stm_word_t *)addr, (stm_word_t)value)
#define TM_UNIT_STORE(addr, value, ts)     stm_unit_store((stm_word_t *)addr, (stm_word_t)value, ts)
#define TM_COMMIT  \
		PSTM_COMMIT_MARKER; \
		stm_commit(); \
  }
#define TM_MALLOC(size)                    stm_malloc(size)
#define TM_FREE(addr)                      stm_free(addr, sizeof(*addr))
#define TM_FREE2(addr, size)               stm_free(addr, size)

#define SEQ_MALLOC(size)                   malloc(size)
#define SEQ_FREE(addr)                     free(addr)
#define P_MALLOC(size)                     SEQ_MALLOC(size)
#define P_FREE(addr)                       SEQ_FREE(addr)

static unsigned int **coreSTM_commits;
static unsigned int **coreSTM_aborts;

#define TM_INIT(nThreads)  \
	stm_init();              \
  mod_mem_init(0);         \
  mod_ab_init(0, NULL);    \
  MN_learn_nb_nops();      \
  coreSTM_commits = (unsigned int **)malloc(sizeof(unsigned int *)*nThreads); \
	coreSTM_aborts  = (unsigned int **)malloc(sizeof(unsigned int *)*nThreads)

#define TM_EXIT(nThreads)   {                      \
	uint64_t starts = 0, aborts = 0, commits = 0;    \
	long ii;                                         \
	for(ii=0; ii < nThreads; ii++){                  \
		long i;                                        \
		for(i=0; i < NUMBER_OF_TRANSACTIONS; i++){     \
			aborts  += coreSTM_aborts[ii][i];            \
			commits += coreSTM_commits[ii][i];           \
		}                                              \
		free(coreSTM_aborts[ii]);                      \
		free(coreSTM_commits[ii]);                     \
	}                                                \
	starts = commits + aborts;                       \
	free(coreSTM_aborts); free(coreSTM_commits);     \
	printf("#starts    : %12lu\n", starts);                                              \
	printf("#commits   : %12lu %6.2f\n", commits, 100.0*((float)commits/(float)starts)); \
	printf("#aborts    : %12lu %6.2f\n", aborts, 100.0*((float)aborts/(float)starts));   \
	printf("#conflicts : %12lu\n", aborts);                                              \
	printf("#capacity  : %12lu\n", 0L);                                                  \
	printf("\n=== PSTM stats\n"); \
	printf("MN_count_spins_total : %lli\n", MN_count_spins_total); \
	printf("MN_count_writes_to_PM_total : %lli\n", MN_count_writes_to_PM_total); \
	} \
	stm_exit()

#define TM_INIT_THREAD(tid) \
	set_affinity(tid); \
  MN_thr_enter(); \
	stm_init_thread(NUMBER_OF_TRANSACTIONS)

#define TM_EXIT_THREAD(tid)                \
	if(stm_get_stats("nb_commits",&coreSTM_commits[tid]) == 0){                 \
		fprintf(stderr,"error: get nb_commits failed!\n");                        \
	}                                                                           \
	if(stm_get_stats("nb_aborts",&coreSTM_aborts[tid]) == 0){                   \
		fprintf(stderr,"error: get nb_aborts failed!\n");                         \
	}                                                                           \
  MN_thr_exit(); \
	stm_exit_thread()

#define TM_ARGDECL_ALONE               /* Nothing */
#define TM_ARGDECL                     /* Nothing */
#define TM_ARG                         /* Nothing */
#define TM_ARG_ALONE                   /* Nothing */
#define TM_CALLABLE                    TM_SAFE

#define TM_SHARED_READ(var)            TM_LOAD(&(var))
#define TM_SHARED_READ_P(var)          TM_LOAD(&(var))

#define TM_SHARED_WRITE(var, val)      \
	PSTM_LOG_ENTRY(&(var), val); \
	TM_STORE(&(var), val)
#define TM_SHARED_WRITE_P(var, val)    \
	PSTM_LOG_ENTRY(&(var), val); \
	TM_STORE(&(var), val)
