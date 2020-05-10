
#define likely(x)       __builtin_expect((x),1)
#define unlikely(x)     __builtin_expect((x),0)



#if defined(STAGNATION_PROFILING)

static inline uint64_t rdtscp( uint32_t *aux )
{
    uint64_t rax,rdx;
    __asm__ __volatile__ ( "rdtscp\n" : "=a" (rax), "=d" (rdx), "=c" (aux) : : );
    return (rdx << 32) + rax;
}

#define MAX_STAG_DATA 100000
typedef struct _stag_time_t {
	uint64_t timestamp;
	uint8_t  stag_rate;
	//uint8_t  write_rate;
	float  write_rate;
	uint32_t tx_mean_cycles;
} stag_time_t;

static stag_time_t *stag_data __ALIGN__;
static uint32_t stag_index __ALIGN__ = 0;
//static uint64_t stag_started __ALIGN__ = 0;
//static uint64_t stag_start_time __ALIGN__ = 0;


static inline
void stag_profiling_init(){
	stag_data = (stag_time_t*)malloc(sizeof(stag_time_t)*MAX_STAG_DATA);
  if (stag_data == NULL) perror("malloc");
}

/*
static inline
void stag_profiling_start(){
	if(stag_started == 0){
		stag_started = 1;
		stag_start_time = rdtscp();
	}
}
*/

static inline
void stag_profiling_collect(uint8_t srate, float wrate, uint32_t mean_cycles) {

  if (stag_index > MAX_STAG_DATA) perror("stag-size");

  uint32_t aux;
  stag_data[stag_index].timestamp = rdtscp(&aux);
  stag_data[stag_index].stag_rate = srate;
  stag_data[stag_index].write_rate = wrate;
  stag_data[stag_index].tx_mean_cycles = mean_cycles;

  stag_index++;
}

static inline
void stag_profiling_report(){
	
  FILE *fs = fopen("stagnation.dat", "w");
	if (fs == NULL) perror("fopen");

  uint64_t t0 = stag_data[0].timestamp;

  for (int i=0; i<stag_index; i++) {
    fprintf(fs, "%lu  %u %.2f %d\n", stag_data[i].timestamp-t0, 
        stag_data[i].stag_rate, stag_data[i].write_rate,
        stag_data[i].tx_mean_cycles);
  }
  fclose(fs);

  free(stag_data);
}

#else

#define stag_profiling_init(); 
#define stag_profiling_collect(r,w,m);
#define stag_profiling_report();

#endif /* STAGNATION_PROFILING */


#if defined(PHASE_PROFILING) || defined(TIME_MODE_PROFILING)
#include <time.h>
#define INIT_MAX_TRANS 1000000
static uint64_t started __ALIGN__ = 0;
static uint64_t start_time __ALIGN__ = 0;
static uint64_t end_time __ALIGN__ = 0;
static uint64_t trans_index __ALIGN__ = 1;
static uint64_t trans_labels_size __ALIGN__ = INIT_MAX_TRANS;
//#endif /* PHASE_PROFILING || TIME_MODE_PROFILING */
typedef enum { TCAPACITY, TEXPLICIT } transition_cause;
static uint64_t hw_sw_capacity_transitions __ALIGN__ = 0;
//static uint64_t hw_sw_abortrate_transitions __ALIGN__ = 0;
static uint64_t hw_sw_explicit_transitions __ALIGN__ = 0;
#if DESIGN == OPTIMIZED
static uint64_t hw_lock_transitions __ALIGN__ = 0;
#endif /* DESIGN == OPTIMIZED */
//#if defined(PHASE_PROFILING) || defined(TIME_MODE_PROFILING)
typedef struct _trans_label_t {
	uint64_t timestamp;
	unsigned char mode;
} trans_label_t __ALIGN__;
static trans_label_t *trans_labels __ALIGN__;
static uint64_t hw_sw_wait_time  __ALIGN__ = 0;
static uint64_t sw_hw_wait_time  __ALIGN__ = 0;

static __thread uint64_t __before__ __ALIGN__ = 0;

static inline
uint64_t getTime(){
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return (uint64_t)(t.tv_sec*1.0e9) + (uint64_t)(t.tv_nsec);
}


static inline
void increaseTransLabelsSize(trans_label_t **ptr, uint64_t *oldLength, uint64_t newLength) {
	trans_label_t *newPtr = (trans_label_t*)malloc(newLength*sizeof(trans_label_t));
	if ( newPtr == NULL ) {
		perror("malloc");
		fprintf(stderr, "error: failed to increase trans_labels array!\n");
		exit(EXIT_FAILURE);
	}
	memcpy((void*)newPtr, (const void*)*ptr, (*oldLength)*sizeof(trans_label_t));
	free(*ptr);
	*ptr = newPtr;
	*oldLength = newLength;
}

static inline
void updateTransitionProfilingData(uint64_t mode, int cause){
	uint64_t now = getTime();
	if(mode == SW){
    if (cause == TCAPACITY) hw_sw_capacity_transitions++;
    else if (cause == TEXPLICIT) hw_sw_explicit_transitions++; 
		if (__before__ != 0) {
			hw_sw_wait_time += now - __before__;
		}
	} else if (mode == HW){
		if (__before__ != 0) {
			sw_hw_wait_time += now - __before__;
		}
	} else { // GLOCK
#if DESIGN == OPTIMIZED
		hw_lock_transitions++;
#endif /* DESIGN == OPTIMIZED */
	}
	if ( unlikely(trans_index >= trans_labels_size) ) {
		increaseTransLabelsSize(&trans_labels, &trans_labels_size, 2*trans_labels_size);
	}
	trans_labels[trans_index++].timestamp = now;
	trans_labels[trans_index-1].mode = mode;
	__before__ = 0;
}

static inline
void setProfilingReferenceTime(){
	__before__ = getTime();
}

static inline
void phase_profiling_init(){
	trans_labels = (trans_label_t*)malloc(sizeof(trans_label_t)*INIT_MAX_TRANS);
}

static inline
void phase_profiling_start(){
	if(started == 0){
		started = 1;
		start_time = getTime();
	}
}

static inline
void phase_profiling_stop(){
	end_time = getTime();
}

static inline
void phase_profiling_report(){
	printf("hw_sw_transitions: %lu - capacity: %lu - explicit: %lu\n", 
      hw_sw_capacity_transitions + hw_sw_explicit_transitions, 
      hw_sw_capacity_transitions, hw_sw_explicit_transitions);
#if DESIGN == OPTIMIZED	
	printf("hw_lock_transitions: %lu \n", hw_lock_transitions);
#endif /* DESIGN == OPTIMIZED */
	
#ifdef PHASE_PROFILING
	FILE *f = fopen("transitions.timestamp", "w");
	if(f == NULL){
		perror("fopen");
	}
#endif /* PHASE_PROFILING*/
	
	trans_labels[0].timestamp = start_time;
	trans_labels[0].mode = HW;

	uint64_t i, ttime = 0;
#ifdef TIME_MODE_PROFILING
	uint64_t hw_time = 0, sw_time = 0;
#if DESIGN == OPTIMIZED
	uint64_t lock_time = 0;
#endif /* DESIGN == OPTIMIZED */
#endif /* TIME_MODE_PROFILING */
	for (i=1; i < trans_index; i++){
		uint64_t dx = trans_labels[i].timestamp - trans_labels[i-1].timestamp;
		unsigned char mode = trans_labels[i-1].mode;
#ifdef PHASE_PROFILING
		fprintf(f, "%lu %d\n", dx, mode);
#else /* TIME_MODE_PROFILING */
		switch (mode) {
			case HW:
				hw_time += dx;
				break;
			case SW:
				sw_time += dx;
				break;
#if DESIGN == OPTIMIZED
			case GLOCK:
				lock_time += dx;
				break;
#endif /* DESIGN == OPTIMIZED */
			default:
				fprintf(stderr, "error: invalid mode in trans_labels array!\n");
				exit(EXIT_FAILURE);
		}
#endif /* TIME_MODE_PROFILING */
		ttime += dx;
	}
	if(ttime < end_time){
		uint64_t dx = end_time - trans_labels[i-1].timestamp;
		unsigned char mode = trans_labels[i-1].mode;
#ifdef PHASE_PROFILING
		fprintf(f, "%lu %d\n", dx, mode);
#else /* TIME_MODE_PROFILING */
		switch (mode) {
			case HW:
				hw_time += dx;
				break;
			case SW:
				sw_time += dx;
				break;
#if DESIGN == OPTIMIZED
			case GLOCK:
				lock_time += dx;
				break;
#endif /* DESIGN == OPTIMIZED */
			default:
				fprintf(stderr, "error: invalid mode in trans_labels array!\n");
				exit(EXIT_FAILURE);
		}
#endif /* TIME_MODE_PROFILING */
		ttime += dx;
	}

#ifdef PHASE_PROFILING
	fprintf(f, "\n\n");
	fclose(f);
#endif /* PHASE_PROFILING */

#ifdef TIME_MODE_PROFILING
	printf("hw:   %6.2lf\n", 100.0*((double)hw_time/(double)ttime));
	printf("sw:   %6.2lf\n", 100.0*((double)sw_time/(double)ttime));
#if DESIGN == OPTIMIZED
	printf("lock: %6.2lf\n", 100.0*((double)lock_time/(double)ttime));
#endif /* DESIGN == OPTIMIZED */
	printf("hw_sw_wtime: %lu (%6.2lf)\n", hw_sw_wait_time,100.0*((double)hw_sw_wait_time/ttime));
	printf("sw_hw_wtime: %lu (%6.2lf)\n", sw_hw_wait_time,100.0*((double)sw_hw_wait_time/ttime));
#endif /* PHASE_PROFILING */
	
	free(trans_labels);
}

#else /* NO PROFILING */

#define setProfilingReferenceTime();         /* nothing */
#define updateTransitionProfilingData(m,c);  /* nothing */
#define phase_profiling_init();              /* nothing */
#define phase_profiling_start();             /* nothing */
#define phase_profiling_stop();              /* nothing */
#define phase_profiling_report();            /* nothing */

#endif /* NO PROFILING */
