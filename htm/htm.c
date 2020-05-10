
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <limits.h>
#include <math.h>
#include <string.h>

#include <htm.h>
#include <aborts_profiling.h>
#include <locks.h>

/*
 * There was a bug previously that caused transactions that aborted due to
 * explicit aborts (log full) to starve because they never noticed its log had been
 * cleared.
 * This includes the code to check for that condition - we neeed to merge some
 * of the code from NVHTM here (not the prettiest thing on earth, mind you).
 */
#ifdef USE_ABORT_LOG_CHECK
#include "log.h"
#endif

#define likely(x)       __builtin_expect((x),1)
#define unlikely(x)     __builtin_expect((x),0)

static __thread long __tx_id __ALIGN__;  // tx thread id
#define HTM_MAX_RETRIES 9
static __thread long __tx_retries __ALIGN__; // current number of retries for non-lock aborts

static lock_t __htm_global_lock __ALIGN__ = LOCK_INITIALIZER;
volatile char padding1[__CACHE_LINE_SIZE__ - sizeof(lock_t)] __ALIGN__;

static long __nThreads __ALIGN__;


#if defined(PHASE_PROFILING) || defined(TIME_MODE_PROFILING)
#include <sys/time.h>
#define INIT_MAX_TRANS 1000000
static uint64_t started __ALIGN__ = 0;
static uint64_t start_time __ALIGN__ = 0;
static uint64_t end_time __ALIGN__ = 0;
static uint64_t trans_index __ALIGN__ = 1;
static uint64_t trans_timestamp_size __ALIGN__ = INIT_MAX_TRANS;
#endif /* PHASE_PROFILING || TIME_MODE_PROFILING */
static uint64_t hw_lock_transitions __ALIGN__ = 0;
#if defined(PHASE_PROFILING) || defined(TIME_MODE_PROFILING)
static uint64_t *trans_timestamp __ALIGN__;

static uint64_t getTime(){
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return (uint64_t)(t.tv_sec*1.0e9) + (uint64_t)(t.tv_nsec);
}

void increaseTransTimestampSize(uint64_t **ptr, uint64_t *oldLength, uint64_t newLength) {
	uint64_t *newPtr = malloc(newLength*sizeof(uint64_t));
	if ( newPtr == NULL ) {
		perror("malloc");
		fprintf(stderr, "error: failed to increase trans_timestamp array!\n");
		exit(EXIT_FAILURE);
	}
	memcpy((void*)newPtr, (const void*)*ptr, (*oldLength)*sizeof(uint64_t));
	free(*ptr);
	*ptr = newPtr;
	*oldLength = newLength;
}
#endif /* PHASE_PROFILING || TIME_MODE_PROFILING */


void TX_START(){
	
	__tx_retries = 0;
#if defined(PHASE_PROFILING) || defined(TIME_MODE_PROFILING)
	if(started == 0){
		started = 1;
		start_time = getTime();
	}
#endif /* PHASE_PROFILING || TIME_MODE_PROFILING */

	do{
		uint32_t __tx_status = htm_begin();
		if ( htm_has_started(__tx_status) ) {
			if( isLocked(&__htm_global_lock) ){
				htm_abort();
			}
			else return;
		}
		// execution flow continues here on transaction abort
		/* Avoid Lemming effect by delaying tx retry till lock is free. */
		if ( isLocked(&__htm_global_lock) ) {
			while( isLocked(&__htm_global_lock) ) pthread_yield();
		} else {
			uint32_t abort_reason __ALIGN__ = htm_abort_reason(__tx_status);
			__inc_abort_counter(__tx_id, abort_reason);


#ifdef USE_ABORT_LOG_CHECK

  // CHECK_LOG_ABORT
#define EXPLICIT_NVM_CONFLICT 0x01000001

  if (abort_reason == EXPLICIT_NVM_CONFLICT)
  {
    ts_s ts1_wait_log_time, ts2_wait_log_time; 
	  ts1_wait_log_time = rdtscp();
		NVLog_s *log = NH_global_logs[TM_tid_var]; 
		while ((LOG_local_state.counter == distance_ptr(log->start, log->end) 
			&& (LOG_local_state.size_of_log - LOG_local_state.counter) < WAIT_DISTANCE) 
			|| (distance_ptr(log->end, log->start) < WAIT_DISTANCE 
			&& log->end != log->start)) { 
				if (*NH_checkpointer_state == 0) { 
					sem_post(NH_chkp_sem); 
					NOTIFY_CHECKPOINT; 
				} 
				PAUSE(); 
		} 
		NH_count_blocks++; 
		LOG_before_TX();
		ts2_wait_log_time = rdtscp(); 
	  NH_time_blocked += ts2_wait_log_time - ts1_wait_log_time; 
	}
#endif

			if (htm_abort_persistent(abort_reason)){
				__tx_retries = HTM_MAX_RETRIES;
			} else {
				__tx_retries++;
			}
		
			if(__tx_retries >= HTM_MAX_RETRIES){
				__tx_retries = HTM_MAX_RETRIES;
				lock(&__htm_global_lock);
#if defined(PHASE_PROFILING) || defined(TIME_MODE_PROFILING)
				uint64_t t = getTime();
				if ( unlikely(trans_index >= trans_timestamp_size) ) {
					increaseTransTimestampSize(&trans_timestamp,
						&trans_timestamp_size, 2*trans_timestamp_size);
				}
				trans_timestamp[trans_index++] = t;
#endif /* PHASE_PROFILING || TIME_MODE_PROFILING */
				hw_lock_transitions++;
				return;
			}
		}
	} while(1);
}

void TX_END(){
	
	if(__tx_retries >= HTM_MAX_RETRIES){
		unlock(&__htm_global_lock);
#if defined(PHASE_PROFILING) || defined(TIME_MODE_PROFILING)
		uint64_t t = getTime();
		if ( unlikely(trans_index >= trans_timestamp_size) ) {
			increaseTransTimestampSize(&trans_timestamp,
				&trans_timestamp_size, 2*trans_timestamp_size);
		}
		trans_timestamp[trans_index++] = t;
#endif /* PHASE_PROFILING || TIME_MODE_PROFILING */
	}
	else{
		htm_end();
		__inc_commit_counter(__tx_id);
	}
}


void HTM_STARTUP(long numThreads){

	__nThreads = numThreads;
	__init_prof_counters(__nThreads);
#if defined(PHASE_PROFILING) || defined(TIME_MODE_PROFILING)
	trans_timestamp = (uint64_t*)malloc(sizeof(uint64_t)*INIT_MAX_TRANS);
	//memset(trans_timestamp, 0,sizeof(uint64_t)*INIT_MAX_TRANS);
#endif /* PHASE_PROFILING || TIME_MODE_PROFILING */
}

void HTM_SHUTDOWN(){

	__term_prof_counters(__nThreads);

	printf("hw_lock_transitions: %lu\n", hw_lock_transitions);
#ifdef PHASE_PROFILING
	
	FILE *f = fopen("transitions.timestamp", "w");
	if(f == NULL){
		perror("fopen");
	}

	trans_timestamp[0] = start_time;

	uint64_t i, ttime = 0;
	for (i=1; i < trans_index; i++){
		uint64_t dx = trans_timestamp[i] - trans_timestamp[i-1];
		fprintf(f, "%lu %d\n", dx, i % 2 == 1);
		ttime += dx;
	}
	if(ttime < end_time){
		uint64_t dx = end_time - trans_timestamp[i-1];
		fprintf(f, "%lu %d\n", dx, i % 2 == 1);
	}
	fprintf(f, "\n\n");
	fclose(f);
/* PHASE_PROFILING */
#elif defined(TIME_MODE_PROFILING)
	trans_timestamp[0] = start_time;

	uint64_t i, hw_time = 0, lock_time = 0, ttime = 0;
	for (i=1; i < trans_index; i++){
		uint64_t dx = trans_timestamp[i] - trans_timestamp[i-1];
		if( i % 2 == 1 ){ hw_time += dx; }
		else { lock_time += dx; }
		ttime += dx;
	}

	if(ttime < end_time){
		uint64_t dx = end_time - trans_timestamp[i-1];
		if( i % 2 == 1 ){ hw_time += dx; }
		else { lock_time += dx; }
		ttime += dx;
	}

	printf("hw:   %6.2lf\n", 100.0*((double)hw_time/(double)ttime));
	printf("lock: %6.2lf\n", 100.0*((double)lock_time/(double)ttime));
	
#endif /* TIME_MODE_PROFILING */
#if defined(PHASE_PROFILING) || defined(TIME_MODE_PROFILING)
	free(trans_timestamp);
#endif /* PHASE_PROFILING || TIME_MODE_PROFILING */
}

void HTM_THREAD_ENTER(long id){

	__tx_id      = id;
	__tx_retries = 0;
}


void HTM_THREAD_EXIT(){

#if defined(PHASE_PROFILING) || defined(TIME_MODE_PROFILING)
	end_time = getTime();
#endif /* PHASE_PROFILING || TIME_MODE_PROFILING */
}
