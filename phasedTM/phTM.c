#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <pthread.h>

#include <htm.h>
#include <aborts_profiling.h>
#include <types.h>

#define HTM_MAX_RETRIES 9

#define MIN_STAG_RETRIES_AFTER_SW 10
#define SAMPLING_RATE             1000
#define WRITESET_THRESHOLD        0.15
#define SW_CYCLES_THRESHOLD       0.15
#define ABORT_RATE_THRESHOLD      75

#define USE_SERIAL_OPTIMIZATION

#ifdef USE_SERIAL_OPTIMIZATION
#define SERIAL_THRESHOLD          0.15
#endif

//#define PRINTF_DEBUG

#ifdef PRINTF_DEBUG        
static uint64_t btime __ALIGN__ = 0;
#endif

const modeIndicator_t NULL_INDICATOR = { .value = 0 };

static __thread bool deferredTx __ALIGN__ = false;
static __thread bool decUndefCounter __ALIGN__ = false;
volatile modeIndicator_t modeIndicator	__ALIGN__ = {
																	.mode = HW,
																	.deferredCount = 0,
										  						.undeferredCount = 0
																};
volatile char padding1[__CACHE_LINE_SIZE__ - sizeof(modeIndicator_t)] __ALIGN__;


__thread uint32_t __tx_tid __ALIGN__;
__thread uint64_t htm_retries __ALIGN__;
__thread uint32_t abort_reason __ALIGN__ = 0;
#if DESIGN == OPTIMIZED
__thread uint32_t previous_abort_reason __ALIGN__;
__thread bool htm_global_lock_is_mine __ALIGN__ = false;
__thread bool isCapacityAbortPersistent __ALIGN__;
#if defined(USE_NVM_HEURISTIC) || defined(STAGNATION_PROFILING)
#define EXPLICIT_NVM_CONFLICT 0x01000001
/* Used to avoid txs from going HW->SW before the checkpointing is done.
 * The idea here is to have the tx which changed the mode (HW->SW) to
 * force the checkpointing and set the flag afterwards.
 * When returning (SW->HW), the tx which forced the change resets the flags,
 * while the others wait for it to become 0 again.
*/
volatile uint64_t hw_sw_wait_chk_flag __ALIGN__ = 1; 
/* flag to control the transitions and reset the variables/wait consolidation */
__thread bool switched_to_sw __ALIGN__ = 0;

#define EXPLICIT_RATE_THRESHOLD (45)
__thread uint64_t hw_committed_txs __ALIGN__ = 0;
__thread uint64_t hw_committed_cycles __ALIGN__ = 0;
__thread uint64_t hw_aborted_cycles __ALIGN__ = 0;
__thread uint64_t hw_explicit_cycles __ALIGN__ = 0;
__thread uint64_t explicit_abort_rate __ALIGN__ = 0;
__thread uint32_t hw_mean_cycles __ALIGN__ = 0;
#ifdef USE_SERIAL_OPTIMIZATION
__thread uint64_t hw_serial_cycles __ALIGN__ = 0;
__thread uint64_t hw_stalled_cycles __ALIGN__ = 0;
__thread uint64_t hw_serial_execs __ALIGN__ = 0;
__thread uint64_t hw_mean_serial __ALIGN__ = 0;
#endif

// only used in SW mode to account for increased/decreased write rates
__thread float sw_last_write_rate __ALIGN__ = 0;
__thread uint64_t sw_current_writes __ALIGN__ = 0;

__thread float sw_last_cycle_rate __ALIGN__ = 0;
__thread float hw_last_cycle_rate __ALIGN__ = 0;
// if the value reach a given constant, then we change (valid only for
// stagnation causes)
// already starts at the threshold so as to allow a first transition as soon as
// explicit threshold is reached
__thread uint64_t retries_due_stag = MIN_STAG_RETRIES_AFTER_SW;

#if STAGNATION_PROFILING
__thread uint32_t mean_writes __ALIGN__ = 0;
#endif

static uint64_t sw_last_time_cycles __ALIGN__ = 0;

static uint32_t total_threads __ALIGN__ = 0;

#define WAIT_CHECKPOINTING while (atomic_load(&hw_sw_wait_chk_flag) != 0) pthread_yield();
#define WAIT_TO_REENTER_HW while (atomic_load(&hw_sw_wait_chk_flag) == 0) pthread_yield();
#endif /* USE_NVM_HEURISTIC */
__thread uint32_t abort_rate __ALIGN__ = 0;
__thread uint64_t num_htm_runs __ALIGN__ = 0;
#define MAX_STM_RUNS 1000
#define MAX_GLOCK_RUNS 100
__thread uint64_t max_stm_runs __ALIGN__ = 100;
__thread uint64_t num_stm_runs __ALIGN__;
__thread uint64_t t0 __ALIGN__ = 0;
__thread uint64_t sum_cycles __ALIGN__ = 0;
#define TX_CYCLES_THRESHOLD (30000) // HTM-friendly apps in STAMP have tx with 20k cycles or less

#if defined(__powerpc__) || defined(__ppc__) || defined(__PPC__)
static inline uint64_t getCycles()
{
		uint32_t upper, lower,tmp;
		__asm__ volatile(
			"0:                  \n"
			"\tmftbu   %0        \n"
			"\tmftb    %1        \n"
			"\tmftbu   %2        \n"
			"\tcmpw    %2,%0     \n"
			"\tbne     0b        \n"
   	 : "=r"(upper),"=r"(lower),"=r"(tmp)
	  );
		return  (((uint64_t)upper) << 32) | lower;
}
#elif defined(__x86_64__)
static inline uint64_t getCycles()
{
    uint32_t tmp[2];
    __asm__ ("rdtsc" : "=a" (tmp[1]), "=d" (tmp[0]) : "c" (0x10) );
    return (((uint64_t)tmp[0]) << 32) | tmp[1];
}
#else
#error "unsupported architecture!"
#endif

#endif /* DESIGN == OPTIMIZED */

#include <utils.h>
#include <phase_profiling.h>

#ifdef USE_ABORT_LOG_CHECK
#ifndef EXPLICIT_NVM_CONFLIC
#define EXPLICIT_NVM_CONFLICT 0x01000001
#endif
// same fix we did to nvm_rtm
#define USE_MIN_NVM 1
#include "log.h"
#include "../nvhtm/nh/common/utils.h"  // phasedTM already includes its own version of 'utils.h'
#endif

int
changeMode(uint64_t newMode, transition_cause cause) {
	
	bool success;
	modeIndicator_t indicator;
	modeIndicator_t expected;
	modeIndicator_t new;

	switch(newMode) {
		case SW:
			setProfilingReferenceTime();
			do {
				indicator = atomicReadModeIndicator();
				expected = setMode(indicator, HW);
				new = incDeferredCount(expected);
				success = boolCAS(&(modeIndicator.value), &(expected.value), new.value);
			} while (!success && (indicator.mode != SW));
			if (success) deferredTx = true;
			do {
				indicator = atomicReadModeIndicator();
				expected = setMode(indicator, HW);
				new = setMode(indicator, SW);
				success = boolCAS(&(modeIndicator.value), &(expected.value), new.value);
			} while (!success && (indicator.mode != SW));
			if(success){

#ifdef USE_NVM_HEURISTIC        
#ifdef PRINTF_DEBUG        
        printf("id %u --- switched to SW --- caused by %d at %f (abort rate = %u)\n",
            __tx_tid, cause, (float)(getCycles()-btime)/CPU_MAX_FREQ, abort_rate);
#endif
#endif

        // must be here to become part of the profiling (time)
#ifdef USE_NVM_HEURISTIC
#include <semaphore.h>
        extern volatile int *NH_checkpointer_state;
        extern sem_t *NH_chkp_sem;
        
        // force checkpointing state (note: we are the only one writing 2 to
        // it)
        atomic_store(NH_checkpointer_state, 2);

        // wake up the checkphandler
        sem_post(NH_chkp_sem);
        
        // wait for it to finish
        while (atomic_load(NH_checkpointer_state) != 0) _mm_pause();

        // logs are drained, start SW mode
        atomic_store(&hw_sw_wait_chk_flag, 0);
#endif
				updateTransitionProfilingData(SW, cause);
#if DESIGN == OPTIMIZED
				t0 = getCycles();
#endif /* DESIGN == OPTIMIZED */
			}
#ifdef USE_NVM_HEURISTIC
        // just in case we have more than 1 deferred tx -> the one that did
        // not perform the mode change must also wait
        else {
          WAIT_CHECKPOINTING;
        }
#endif
			break;
		case HW:
			setProfilingReferenceTime();
			do {
				indicator = atomicReadModeIndicator();
				expected = setMode(NULL_INDICATOR, SW);
				new = setMode(NULL_INDICATOR, HW);
				success = boolCAS(&(modeIndicator.value), &(expected.value), new.value);
			} while (!success && (indicator.mode != HW));
			if(success){

#ifdef PRINTF_DEBUG        
        printf("id %u --- switched to hw --- at %f\n", __tx_tid,  
            (float)(getCycles()-btime)/CPU_MAX_FREQ);
#endif
				updateTransitionProfilingData(HW, cause);
#ifdef USE_NVM_HEURISTIC
        atomic_store(&hw_sw_wait_chk_flag, 1); // reentering HW from SW

        /* TODO
         * Is it possible for this transaction to start in HW mode and change
         * the flag back to 0 before the remaining txs (below) do SW->HW? If so, we
         * might have a problem with this code (the remaining txs will be
         * stuck in the wait condition below)
         */
#endif
			}
#ifdef USE_NVM_HEURISTIC
      else { // txs that did not succeeded in changing the mode
        // note: this is here because every tx going from SW to HW calls changeMode(HW)
        // note2: if code is not running txs, it will return to HW without
        // coming this way
          WAIT_TO_REENTER_HW; // waits for the checkpointing flag to be reset
      }
#endif
			break;
#if DESIGN == OPTIMIZED
		case GLOCK:
			do {
				indicator = atomicReadModeIndicator();
				if ( indicator.mode == SW ) return 0;
				if ( indicator.mode == GLOCK) return -1;
				expected = setMode(NULL_INDICATOR, HW);
				new = setMode(NULL_INDICATOR, GLOCK);
				success = boolCAS(&(modeIndicator.value), &(expected.value), new.value);
			} while (!success);
			updateTransitionProfilingData(GLOCK, cause);
			break;
#endif /* DESIGN == OPTIMIZED */
		default:
			fprintf(stderr,"error: unknown mode %lu\n", newMode);
			exit(EXIT_FAILURE);
	}
	return 1;
}

#if DESIGN == OPTIMIZED
static inline
void
unlockMode(){
	bool success;
	do {
		modeIndicator_t expected = setMode(NULL_INDICATOR, GLOCK);
		modeIndicator_t new = setMode(NULL_INDICATOR, HW);
		success = boolCAS(&(modeIndicator.value), &(expected.value), new.value);
	} while (!success);
	updateTransitionProfilingData(HW, 0);
}
#endif /* DESIGN == OPTIMIZED */

bool
HTM_Start_Tx() {

	phase_profiling_start();
	
	htm_retries = 0;
	abort_reason = 0;
#if DESIGN == OPTIMIZED
	isCapacityAbortPersistent = 0;
	t0 = getCycles();
#endif /* DESIGN == OPTIMIZED */
    
#ifdef USE_NVM_HEURISTIC
  if (switched_to_sw == 1) {
    switched_to_sw = 0;
#ifdef PRINTF_DEBUG       
            printf("tid %u returning to hw\n", __tx_tid);
#endif

  }
#endif

	while (true) {
		uint32_t status = htm_begin();
		if (htm_has_started(status)) {
			if (modeIndicator.value == 0) {
				return false;
			} else {
				htm_abort();
			}
		}


		if ( isModeSW() ){
			return true;
		}

#if DESIGN == OPTIMIZED
		if ( isModeGLOCK() ){
			// lock acquired
			// wait till lock release and restart

#if defined(USE_NVM_HEURISTIC) || defined(STAGNATION_PROFILING)
#ifdef USE_SERIAL_OPTIMIZATION
      uint64_t startser = getCycles();
#endif
#endif
			while( isModeGLOCK() ) pthread_yield();
#if defined(USE_NVM_HEURISTIC) || defined(STAGNATION_PROFILING)
	    /*
       * If we simply reset the time here we will not take into account the
       * stalled time 
       */
		  
#ifdef USE_SERIAL_OPTIMIZATION
      uint64_t t1 = getCycles();
      uint64_t tx_cycles = t1 - startser;

      hw_stalled_cycles += tx_cycles;
#endif
      explicit_abort_rate = 0;
      retries_due_stag = 0;
#endif
			continue;
		}
#endif /* DESIGN == OPTIMIZED */

#if DESIGN == OPTIMIZED
		previous_abort_reason = abort_reason; 
#endif /* DESIGN == OPTIMIZED */
		abort_reason = htm_abort_reason(status);
		__inc_abort_counter(__tx_tid, abort_reason);
		
#ifndef DISABLE_PHASE_TRANSITIONS
		modeIndicator_t indicator = atomicReadModeIndicator();
		if (indicator.value != 0) {
			return true;
		}
#endif

#if DESIGN == PROTOTYPE
		htm_retries++;
		if ( htm_retries >= HTM_MAX_RETRIES ) {
			changeMode(SW, TCAPACITY);
			return true;
		}
#else  /* DESIGN == OPTIMIZED */
		htm_retries++;
#ifndef DISABLE_PHASE_TRANSITIONS
		isCapacityAbortPersistent = (abort_reason & ABORT_CAPACITY)
		                 && (previous_abort_reason == abort_reason);

		if ( (abort_reason & ABORT_TX_CONFLICT) 
				&& (previous_abort_reason == abort_reason) )
			abort_rate = (abort_rate * 75 + 25*100) / 100;




#endif // !DISABLE_PHASE_TRANSITIONS

		num_htm_runs++;
		uint64_t t1 = getCycles();
		uint64_t tx_cycles = t1 - t0;
		t0 = t1;
		sum_cycles += tx_cycles;



		uint64_t mean_cycles = 0;
		if (htm_retries >= 2) {
			mean_cycles = sum_cycles / num_htm_runs;
		}

#if defined(USE_NVM_HEURISTIC) || defined(STAGNATION_PROFILING)
    hw_aborted_cycles += tx_cycles;
#endif

#ifdef USE_ABORT_LOG_CHECK

  // CHECK_LOG_ABORT

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

#if defined(USE_NVM_HEURISTIC) || defined(STAGNATION_PROFILING)
    hw_explicit_cycles += ts2_wait_log_time - ts1_wait_log_time;
#endif
	}
#endif


#ifndef DISABLE_PHASE_TRANSITIONS
		if ( (isCapacityAbortPersistent
					&& (mean_cycles > TX_CYCLES_THRESHOLD))
				||
				 (isCapacityAbortPersistent
					&& (abort_rate >= ABORT_RATE_THRESHOLD)) ) {

#ifdef USE_NVM_HEURISTIC      
#ifdef USE_SERIAL_OPTIMIZATION
      float serial_percentage;

      if (sw_last_time_cycles != 0) {
      // we have been in SW mode already
        if ((hw_mean_serial == 0)) {
        // still have not computed any serial time.. force calculating the
        // mean
          hw_mean_serial = (hw_serial_cycles+hw_stalled_cycles)/(hw_serial_execs+1);

          hw_mean_cycles = (hw_committed_cycles+hw_aborted_cycles+hw_explicit_cycles+hw_stalled_cycles)/(hw_committed_txs+1);

        }
      }      

      /* The idea here is to look at the time it is spent in serial mode... if
       * it is significant we use it to decide whether we should go to SW or
       * not
       * */
      serial_percentage = (float)hw_mean_serial/(float)hw_mean_cycles;
#ifdef PRINTF_DEBUG       
      printf("-- tid %u serialp %f hwmeanc %u hwmeans %lu swlt %lu meancycs %lu\n", 
              __tx_tid, serial_percentage, hw_mean_cycles, hw_mean_serial,
              sw_last_time_cycles, mean_cycles);
#endif
      if (serial_percentage > SERIAL_THRESHOLD) {

        if ((hw_mean_serial*(1+((total_threads-1)>>2))) < sw_last_time_cycles) {
          // reset variables
          hw_committed_cycles = 0;
          hw_aborted_cycles = 0;
          hw_explicit_cycles = 0;
          hw_committed_txs = 0;
          hw_serial_cycles = 0;
          hw_stalled_cycles = 0;
          hw_serial_execs  = 0;
          previous_abort_reason = 0;
          abort_rate = 0;
          continue;
        } else 
          hw_mean_cycles = hw_mean_serial*(1+((total_threads-1)>>2));
      }
#endif
#endif
			num_stm_runs = 0;
      num_htm_runs = 0;
      max_stm_runs = 100;
      sum_cycles = 0; // otherwise SW will start with some leftover cycles from HW
#if STAGNATION_PROFILING
      mean_writes = 0;
#endif
#ifdef USE_NVM_HEURISTIC
      switched_to_sw = 1;
      sw_last_write_rate = 0;
      // sample the current total number of writes
      sw_current_writes = MN_count_writes;
      sw_last_cycle_rate = 0;

      // it is migrating due to capicity.. it might be the case that the
      // sampling rate count was not achieved yet and we were not able to compute 
      // explicit_abort_rate

      if (hw_mean_cycles == 0) {
        uint64_t total_cycles = hw_committed_cycles+hw_aborted_cycles+hw_explicit_cycles;
        hw_mean_cycles = (uint32_t)(total_cycles/(hw_committed_txs+1));
      }
      
      retries_due_stag = 0;
#endif /* USE_NVM_HEURISTIC */
			changeMode(SW, TCAPACITY);
			return true;
#ifdef USE_NVM_HEURISTIC
		} else if (retries_due_stag > MIN_STAG_RETRIES_AFTER_SW) {
      // check if we were already in STM mode and change only if its faster
      num_stm_runs = 0;
      num_htm_runs = 0;
      max_stm_runs = 100;
      sum_cycles = 0; // otherwise SW will start with some leftover cycles from HW
      switched_to_sw = 1;
      
      sw_last_write_rate = 0;
      // sample the current total number of writes
      sw_current_writes = MN_count_writes;
      sw_last_cycle_rate = 0;
#if STAGNATION_PROFILING
      mean_writes = 0;
#endif

      changeMode(SW, TEXPLICIT);
      return true;
#endif
    } else if (htm_retries >= HTM_MAX_RETRIES) {
#endif // !DISABLE_PHASE_TRANSITIONS
#ifdef DISABLE_PHASE_TRANSITIONS
    if (htm_retries >= HTM_MAX_RETRIES) {
#endif
			int status = changeMode(GLOCK, TCAPACITY);
			if(status == 0){
				// Mode already changed to SW
				return true;
			} else {
				// Success! We are in LOCK mode
				if ( status == 1 ){
					htm_retries = 0;
					// I own the lock, so return and
					// execute in mutual exclusion
					htm_global_lock_is_mine = true;
					t0 = getCycles();
					return false;
				} else {
					// I don't own the lock, so wait
					// till lock is release and restart
#if defined(USE_NVM_HEURISTIC) || defined(STAGNATION_PROFILING)
#ifdef USE_SERIAL_OPTIMIZATION
          uint64_t startser = getCycles();
#endif
#endif
					while( isModeGLOCK() ) pthread_yield();
#if defined(USE_NVM_HEURISTIC) || defined(STAGNATION_PROFILING)
        /*
         * If we simply reset the time here we will not take into account the
         * stalled time
         */

#ifdef USE_SERIAL_OPTIMIZATION
          uint64_t t1 = getCycles();
          uint64_t tx_cycles = t1 - startser;

          hw_stalled_cycles += tx_cycles;
#endif

          explicit_abort_rate = 0;
          retries_due_stag = 0;
#endif
					continue;
				}
			}
		}
#endif /* DESIGN == OPTIMIZED */
	}
}

void
HTM_Commit_Tx() {

#if	DESIGN == PROTOTYPE
	htm_end();
	__inc_commit_counter(__tx_tid);
#else  /* DESIGN == OPTIMIZED */
	if (htm_global_lock_is_mine){
		unlockMode();
		htm_global_lock_is_mine = false;
		uint64_t t1 = getCycles();
		uint64_t tx_cycles = t1 - t0;
		t0 = t1;
		sum_cycles += tx_cycles;

#if defined(USE_NVM_HEURISTIC) || defined(STAGNATION_PROFILING)
  // still counting this as 1 hw transaction for statistics issues
    hw_committed_cycles += tx_cycles;
    hw_committed_txs++;
#ifdef USE_SERIAL_OPTIMIZATION
    hw_serial_cycles += tx_cycles;
    hw_serial_cycles += hw_stalled_cycles;
    hw_serial_execs++;
      
    if (hw_serial_execs > 100) {
      hw_mean_serial = (hw_serial_cycles+hw_stalled_cycles)/hw_serial_execs;

#ifdef PRINTF_DEBUG       
    float serial_percentage = (float)hw_mean_serial/(hw_mean_cycles+hw_mean_serial);
    printf("tid %u - serial time %lu sw-last %lu - true val %lu - serial perc %f\n", 
        __tx_tid, hw_mean_serial, sw_last_time_cycles,
        hw_mean_serial*total_threads, serial_percentage);
#endif
      
      hw_serial_execs  = 0;
      hw_serial_cycles = 0;

      // also updates hw mean to keep both serial and committed cycles in sync
      uint64_t total_cycles = hw_committed_cycles+hw_aborted_cycles+hw_explicit_cycles;
      explicit_abort_rate = hw_explicit_cycles*100 / total_cycles;

      hw_mean_cycles = (uint32_t)(total_cycles/hw_committed_txs);
      
      hw_committed_cycles = 0;
      hw_aborted_cycles = 0;
      hw_explicit_cycles = 0;
      hw_committed_txs = 0;
    }
    hw_stalled_cycles = 0;
#endif /* USE_SERIAL_OPTIMIZATION */

#if STAGNATION_PROFILING
    mean_writes += LOG_nb_writes;
#endif
      

  // to simplify things, we delay the counting/resetting until execution reaches the HTM

#endif

	} else {
		htm_end();
#if defined(USE_NVM_HEURISTIC) || defined(STAGNATION_PROFILING)
    hw_committed_cycles += (getCycles() - t0);
    hw_committed_txs++;

#if STAGNATION_PROFILING
    mean_writes += LOG_nb_writes;
#endif
    
#ifdef USE_SERIAL_OPTIMIZATION
    if (hw_stalled_cycles > 0) {
      hw_serial_cycles += hw_stalled_cycles;
      hw_serial_execs++;
      hw_stalled_cycles = 0;
    }
#endif

    if ((hw_committed_txs % SAMPLING_RATE) == 0) {
    
#ifdef USE_SERIAL_OPTIMIZATION
      if (hw_serial_execs > 0) {
        hw_mean_serial = hw_serial_cycles/hw_serial_execs;
      
        hw_serial_execs  = 0;
        hw_serial_cycles = 0;
      }

#endif /* USE_SERIAL_OPTIMIZATION */

      uint64_t total_cycles = hw_committed_cycles+hw_aborted_cycles+hw_explicit_cycles;
      explicit_abort_rate = hw_explicit_cycles*100 / total_cycles;

      hw_mean_cycles = (uint32_t)(total_cycles/hw_committed_txs);

#if STAGNATION_PROFILING
      float wrate = (float)((float)mean_writes/(float)hw_committed_txs);

      if (__tx_tid == 0)
        stag_profiling_collect(explicit_abort_rate, wrate, hw_mean_cycles);
      
      mean_writes = 0;
#endif

    if (explicit_abort_rate >= EXPLICIT_RATE_THRESHOLD) {
                
      if (hw_mean_cycles > sw_last_time_cycles) 
        retries_due_stag++;
      else
        retries_due_stag = 0;


      float current_cycle_rate = (float)total_cycles/(float)hw_committed_txs;

      float derivative = hw_last_cycle_rate-current_cycle_rate;
        
#ifdef PRINTF_DEBUG       
      if (__tx_tid == 0)
          printf("hwcurrent-cyc %f - hwlast-cyc %f swlast-time %lu -- derivative %f - perc: %f -stag %lu\n", 
            current_cycle_rate, hw_last_cycle_rate, sw_last_time_cycles, derivative,
            derivative/hw_last_cycle_rate, retries_due_stag);
#endif

      if (derivative < 0.0f) { // decreasing
        hw_last_cycle_rate = current_cycle_rate;
      } else if (derivative/hw_last_cycle_rate >= 0.3) {
        // increasing at a good rate - reset retries for stag
        retries_due_stag = 0;
        hw_last_cycle_rate = 0;
      }

    } else retries_due_stag = 0;

      // reset variables
      hw_committed_cycles = 0;
      hw_aborted_cycles = 0;
      hw_explicit_cycles = 0;
      hw_committed_txs = 0;
    }
#endif // !DISABLE_PHASE_TRANSITIONS
		__inc_commit_counter(__tx_tid);
  }
  abort_rate = (abort_rate * 75) / 100;
#endif /* DESIGN == OPTIMIZED */

}


bool
STM_PreStart_Tx(bool restarted) {

	modeIndicator_t indicator = { .value = 0 };
	modeIndicator_t expected = { .value = 0 };
	modeIndicator_t new = { .value = 0 };
	bool success;

	if (!deferredTx) {
		do {
			indicator = atomicReadModeIndicator();
				if (indicator.deferredCount == 0 || indicator.mode == HW) {
          if (decUndefCounter) atomicDecUndeferredCount();
          decUndefCounter = false;
					changeMode(HW, TCAPACITY);
#if defined(USE_NVM_HEURISTIC) || defined(STAGNATION_PROFILING)
          switched_to_sw = 0;
#endif
#ifdef PRINTF_DEBUG       
            printf("tid %u returning to hw\n", __tx_tid);
#endif
					return true;
				}

#if defined(USE_NVM_HEURISTIC) || defined(STAGNATION_PROFILING)
      if (switched_to_sw == 0) {
        switched_to_sw = 1;
#ifdef PRINTF_DEBUG        
        printf("id %u --- moving to SW --- undeferred at %f\n", __tx_tid,  
            (float)(getCycles()-btime)/CPU_MAX_FREQ);
#endif

    // we need to reset all counting variables used in the HW mode
    // these are executed by the undeferred transactions - deferred ones reset
    // them before doing a HW->SW migration
        hw_committed_cycles = 0;
        hw_aborted_cycles = 0;
        hw_explicit_cycles = 0;
        hw_committed_txs = 0;
#ifdef USE_SERIAL_OPTIMIZATION
        hw_serial_cycles = 0;
        hw_stalled_cycles = 0;
        hw_serial_execs  = 0;
        hw_mean_serial = 0;
#endif
        explicit_abort_rate = 0;
        hw_mean_cycles = 0;
        retries_due_stag = 0;
        hw_last_cycle_rate = 0;
#if STAGNATION_PROFILING
        mean_writes = 0;
#endif

// wait for the checkpointing to finish (done by the tx who is forcing HW->SW)
#ifdef USE_NVM_HEURISTIC
        WAIT_CHECKPOINTING;
#endif

      /*
       * This variable is used to calculate the cycle means for HW -AND- SW
       * transactions - initially, it was only reset by deferred transactions
       *
       * We need to reset them here for the undeferred ones, as well as the
       * counting variables
       * */
        sum_cycles = 0;
        num_stm_runs = 0;
        num_htm_runs = 0;
      }
#endif
      if(decUndefCounter) break;
			expected = setMode(indicator, SW);
			new = incUndeferredCount(expected);
			success = boolCAS(&(modeIndicator.value), &(expected.value), new.value);
		} while (!success);
    decUndefCounter = true;
	}
#if DESIGN == OPTIMIZED
	else {
    if (!restarted)
		  t0 = getCycles();
#if STAGNATION_PROFILING
    if (!restarted)
      MN_count_writes = 0;  // should this be performed by the STM? - no, it is a total count variable!
#endif
	}
#endif /* DESIGN == OPTIMIZED */
	return false;
}


void
STM_PostCommit_Tx() {
	

#if DESIGN == OPTIMIZED
	if (deferredTx) {
		uint64_t t1 = getCycles();
		uint64_t tx_cycles = t1 - t0;
		t0 = t1;
		sum_cycles += tx_cycles;
		num_stm_runs++;
#if STAGNATION_PROFILING
    mean_writes += MN_count_writes/2;
#endif
		if (num_stm_runs < max_stm_runs) {
			return;
		}
		uint64_t mean_cycles = sum_cycles / max_stm_runs;


#if STAGNATION_PROFILING
    float wrate = (float)((float)mean_writes/(float)num_stm_runs);

    stag_profiling_collect(111, wrate, mean_cycles);
      
    mean_writes = 0;
#endif

#ifdef USE_NVM_HEURISTIC
    uint64_t my_num_stm_runs = num_stm_runs;
#endif
		
    num_stm_runs = 0;
		sum_cycles = 0;


#ifdef USE_NVM_HEURISTIC
/*
 * The idea of the 'new' heuristic is split into two parts:
 *
 * 1) if HW cycles are less than measured SW ones, we go back do HW
 * immediately
 *    - whatever was the cause for HW->SW, if we notice that HW is still faster,
 *    there is no reason to stay here - this will probably be true for
 *    the transitions caused by sporadic capacity issues
 *
 *
 * 2) from here we distinguish between capacity and stagnation induced
 * transitions
 *
 *  2a) stagnation
 *    - calculate the derivative of #writes. If it is decreasing by a certain
 * margin, return to HW. The ideia is that the number of writes have decrease
 * and it makes sense to return to HW to try again
 *    - in order to avoid coming back to SW too frequently, after SW->HW, the
 * system need to detected log-induced stagnation for N consecutive times
 * (another parameter) -> this apparently stabilized Intruder
 *
 * 2b) capacity
 *   - use the regular heuristic, i.e., if above a certain threshold, continue
 *   in HW and double the number of transactions to execute before a new
 *   measurement is made
 *
 *
 * */

    sw_last_time_cycles = mean_cycles;


#ifdef PRINTF_DEBUG        
    printf("---\nid %u - sw mode! hw_cyc %u - sw_cyc %lu  stagrate %lu\n", 
        __tx_tid, hw_mean_cycles, mean_cycles, explicit_abort_rate);
#endif    

    /*
     * Return if:
     * 1) hw cycles are less than measured sw cycles
     *   OR
     * 2) sw cycles are less than the threshold
     *    AND
     *    HW->SW happened because of capacity/abort rate issue (not
     *    stagnation)
     */
    if ( (hw_mean_cycles <= mean_cycles) ||
         ((mean_cycles < TX_CYCLES_THRESHOLD) &&
          retries_due_stag == 0) )
      goto going_back_to_hw;


    if (max_stm_runs < MAX_STM_RUNS) max_stm_runs = 2*max_stm_runs;

    /*
     * WRITE-SET changes
     */

    // PSTM counts 2 writes to PM for each application's write (an extra for
    // the log)
    uint64_t delta_writes = (MN_count_writes - sw_current_writes)/2;
    sw_current_writes = MN_count_writes;  // reset


    float current_write_rate = (float)delta_writes/(float)my_num_stm_runs;
    
    // actually, the delta ...
    float derivative = (sw_last_write_rate-current_write_rate);
    
#ifdef PRINTF_DEBUG        
    printf("id %u - current %f - last %f -- derivative %f\n", 
        __tx_tid, current_write_rate, sw_last_write_rate, derivative);
#endif
    
    if (derivative < 0.0f) {
      // it is increasing, stay in SW mode (update with the current value)
      // this is pessimistic in the sense that it always stores the largest
      // value seen up until this point
      sw_last_write_rate = current_write_rate;
    } 

    if ((derivative/sw_last_write_rate) >= WRITESET_THRESHOLD)
      goto going_back_to_hw;

    
    /*
     * Cycles changes
     */
    
    float current_cycle_rate = (float)mean_cycles;
  
    derivative = (sw_last_cycle_rate-current_cycle_rate);
  
#ifdef PRINTF_DEBUG        
    printf("id %u - current-cyc %f - last-cyc %f -- derivative %f - perc: %f\n", 
      __tx_tid, current_cycle_rate, sw_last_cycle_rate, derivative,
      derivative/sw_last_cycle_rate);
#endif

    if (derivative < 0.0f) {
      // it is increasing, stay in SW mode (update with the current value)
      sw_last_cycle_rate = current_cycle_rate;
    }
    
    if (derivative/sw_last_cycle_rate >= SW_CYCLES_THRESHOLD)
      goto going_back_to_hw;


    // if we got here, none of the conditions for SW->HW were met
    return; 


going_back_to_hw:
    // we need to reset all counting variables used in the HW mode before
    // returning... undeferred transactions reset tem before HW->SW
    hw_committed_cycles = 0;
    hw_aborted_cycles = 0;
    hw_explicit_cycles = 0;
    hw_committed_txs = 0;
#ifdef USE_SERIAL_OPTIMIZATION
    hw_serial_cycles = 0;
    hw_stalled_cycles = 0;
    hw_serial_execs  = 0;
    hw_mean_serial = 0;
#endif
    explicit_abort_rate = 0;
    hw_mean_cycles = 0;
    retries_due_stag = 0;
    hw_last_cycle_rate = 0;
    switched_to_sw = 0;
#if STAGNATION_PROFILING
    mean_writes = 0;
#endif
#ifdef PRINTF_DEBUG        
    printf("id %u - returning at: %f\n", __tx_tid,
        (float)(getCycles()-btime)/CPU_MAX_FREQ);
#endif
#else /*!USE_NVM_HEURISTICS */
		if (mean_cycles > TX_CYCLES_THRESHOLD){
			if (max_stm_runs < MAX_STM_RUNS) max_stm_runs = 2*max_stm_runs;
			return;
		}
#endif /* USE_NVM_HEURISTIC */
	} /* if deferred */

#endif /* DESIGN == OPTIMIZED */


  if (!deferredTx) {
    atomicDecUndeferredCount();
    decUndefCounter = false;
  } else { // deferredTx

    deferredTx = false;

    atomicDecDeferredCount();
    modeIndicator_t new = atomicReadModeIndicator();

    if (new.deferredCount == 0) {
      changeMode(HW, TCAPACITY);
#ifdef PRINTF_DEBUG       
      printf("tid %u returning to hw\n", __tx_tid);
#endif
    }
  }
}

void
phTM_init(long nThreads){
	printf("DESIGN: %s\n", (DESIGN == PROTOTYPE) ? "PROTOTYPE" : "OPTIMIZED");
	__init_prof_counters(nThreads);
	phase_profiling_init();
	stag_profiling_init();
#ifdef PRINTF_DEBUG        
  btime = getCycles();
#endif
#if defined(USE_NVM_HEURISTIC) || defined(STAGNATION_PROFILING)
  total_threads = nThreads;
#endif
}

void
phTM_thread_init(long tid){
	__tx_tid = tid;
#if DESIGN == OPTIMIZED
  abort_rate = 0.0;
#endif
#if defined(USE_NVM_HEURISTIC) || defined(STAGNATION_PROFILING)
  hw_committed_cycles = 0;
  hw_aborted_cycles = 0;
  hw_explicit_cycles = 0;
  hw_committed_txs = 0;
#ifdef USE_SERIAL_OPTIMIZATION
  hw_serial_cycles = 0;
  hw_stalled_cycles = 0;
  hw_serial_execs  = 0;
  hw_mean_serial = 0;
#endif
  explicit_abort_rate = 0;
  retries_due_stag = 0;
  hw_mean_cycles = 0;
  switched_to_sw = 0;
  hw_last_cycle_rate = 0;
#if STAGNATION_PROFILING
  mean_writes = 0;
#endif
#ifdef PRINTF_DEBUG        
  printf("id %u - entering thread at: %f\n", __tx_tid,
        (float)(getCycles()-btime)/CPU_MAX_FREQ);
#endif
#endif
}

void
phTM_thread_exit(void){
	phase_profiling_stop();
#if DESIGN == OPTIMIZED
	if (deferredTx) {
#ifdef PRINTF_DEBUG        
    printf("id %u - deferred tx leaving thread!\n", __tx_tid);
#endif    
		deferredTx = false;
		atomicDecDeferredCount();
	}
#ifdef PRINTF_DEBUG        
  printf("id %u - exiting thread at: %f\n", __tx_tid,
        (float)(getCycles()-btime)/CPU_MAX_FREQ);
#endif
#endif /* DESIGN == OPTIMIZED */
}

void
phTM_term(long nThreads, long nTxs, uint64_t **stmCommits, uint64_t **stmAborts){
#ifndef DISABLE_PHASE_TRANSITIONS
	__term_prof_counters(nThreads, nTxs, stmCommits, stmAborts);
#else
	__term_prof_counters(nThreads);
#endif
#ifdef PRINTF_DEBUG        
        printf("--- finishing at %f\n", (float)(getCycles()-btime)/CPU_MAX_FREQ);
#endif
	phase_profiling_report();
	stag_profiling_report();
}


