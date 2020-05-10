#ifndef NH_SOL_H
#define NH_SOL_H

#ifdef __cplusplus
extern "C"
{
  #endif

  #define MAXIMUM_OFFSET 400 // in cycles

// TODO: remove the externs
#undef BEFORE_HTM_BEGIN_spec
#define BEFORE_HTM_BEGIN_spec(tid, budget) \
    extern __thread int global_threadId_; \
    extern int nb_transfers; \
    extern long nb_of_done_transactions; \
    while (*NH_checkpointer_state != 0 && nb_of_done_transactions < nb_transfers) { \
			PAUSE(); \
    }

#undef BEFORE_TRANSACTION_i
#define BEFORE_TRANSACTION_i(tid, budget) \
  LOG_get_ts_before_tx(tid); \
  LOG_before_TX(); \
  TM_inc_local_counter(tid);

#undef BEFORE_COMMIT
#define BEFORE_COMMIT(tid, budget, status) \
  ts_var = rdtscp(); /* must be the p version */  \
  if (LOG_count_writes(tid) > 0 && TM_nb_threads > 28) { \
    while ((rdtscp() - ts_var) < MAXIMUM_OFFSET); /* wait offset */ \
  }

#undef AFTER_TRANSACTION_i
#define AFTER_TRANSACTION_i(tid, budget) ({ \
  int nb_writes = LOG_count_writes(tid); \
  if (nb_writes) { \
    htm_tx_val_counters[tid].global_counter = ts_var; \
    __sync_synchronize(); \
    NVMHTM_commit(tid, ts_var, nb_writes); \
  } \
  /*printf("nb_writes=%i\n", nb_writes);*/ \
  CHECK_AND_REQUEST(tid); \
  TM_inc_local_counter(tid); \
  if (nb_writes) { \
    LOG_after_TX(); \
  } \
})

#undef AFTER_ABORT
#define AFTER_ABORT(tid, budget, status) \
  /* NH_tx_time += rdtscp() - TM_ts1; */ \
  CHECK_LOG_ABORT(tid, status); \
  LOG_get_ts_before_tx(tid); \
  __sync_synchronize(); \
  ts_var = rdtscp(); \
  htm_tx_val_counters[tid].global_counter = ts_var; \
  /*CHECK_LOG_ABORT(tid, status);*/ \
  /*if (status == _XABORT_CONFLICT) printf("CONFLICT: [start=%i, end=%i]\n", \
  NH_global_logs[TM_tid_var]->start, NH_global_logs[TM_tid_var]->end); */

#undef NH_before_write
#define NH_before_write(addr, val) ({ \
  LOG_nb_writes++; \
  LOG_push_addr(TM_tid_var, addr, val); \
})

#undef NH_write
#ifndef SOFTWARE_TRANSLATION
#define NH_write(addr, val) ({ \
  GRANULE_TYPE buf = val; \
  NH_before_write(addr, val); \
  memcpy(addr, &(buf), sizeof(GRANULE_TYPE)); /* *((GRANULE_TYPE*)addr) = val; */ \
  NH_after_write(addr, val); \
  val; \
})
#endif /* SOFTWARE_TRANSLATION */

#ifdef SOFTWARE_TRANSLATION

// TODO: check with paolo if he wants to show this ---

typedef struct NH_ST_alias_entry_ {
  // on write fill the entry in the alias table
  // on read check if the entry is in the alias table, if yes read else go to address directly
  void *addr;
  uintptr_t val;
  uintptr_t ts;
} NH_ST_alias_entry_s;

extern NH_ST_alias_entry_s *NH_alias_table;

// TODO: grab base pointer
#define NH_translate(addr) ({ \
  \
})

#define NH_write(addr, val) ({ \
  GRANULE_TYPE buf = val; \
  NH_before_write(addr, val); \
  memcpy(addr, &(buf), sizeof(GRANULE_TYPE)); /* *((GRANULE_TYPE*)addr) = val; */ \
  NH_after_write(addr, val); \
  val; \
})

#undef NH_read
#define NH_read(addr) ({ \
  NH_before_read(addr); \
  (__typeof__(*addr))*(addr); \
})
#endif /* SOFTWARE_TRANSLATION */

  // TODO: comment for testing with STAMP
  /* #ifndef USE_MALLOC
  #if DO_CHECKPOINT == 5
  #undef  NH_alloc
  #undef  NH_free
  #define NH_alloc(size) malloc(size)
  #define NH_free(pool)  free(pool)
  #else
  #undef  NH_alloc
  #undef  NH_free
  #define NH_alloc(size) NVHTM_malloc(size)
  #define NH_free(pool)  NVHTM_free(pool)
  #endif
  #endif */

  #ifdef __cplusplus
}
#endif

#endif /* NH_SOL_H */
