#include "min_nvm.h"
#include <string.h>
#include <mutex>

#ifndef ALLOC_FN
#define ALLOC_FN(ptr, type, size) \
ptr = (type*) malloc(size * sizeof(type))
#endif

#define SIZE_AUX_POOL 4096*sizeof(int)
static int *aux_pool;

/* #ifdef OLD_ALLOC
#define ALLOC_FN(ptr, type, size) posix_memalign((void **) &ptr, \
CACHE_LINE_SIZE, (size * sizeof(type)) * CACHE_LINE_SIZE)
#else
#define ALLOC_FN(ptr, type, size) ptr = ((type*) aligned_alloc(CACHE_LINE_SIZE, \
(size * sizeof(type)) * CACHE_LINE_SIZE))
#endif */

/*
TODO shm

*/

CL_ALIGN int SPINS_PER_100NS;
long long MN_count_spins_total;
unsigned long long MN_time_spins_total;
long long MN_count_writes_to_PM_total;

__thread CL_ALIGN NH_spin_info_s MN_info;

static std::mutex mtx;

int SPIN_PER_WRITE(int nb_writes)
{
	ts_s _ts1_ = rdtscp();
	SPIN_10NOPS(NH_spins_per_100 * nb_writes);
	MN_count_spins += nb_writes;
	MN_time_spins += rdtscp() - _ts1_;
	return nb_writes;
}

int MN_write(void *addr, void *buf, size_t size, int to_aux)
{
	MN_count_writes++;
	if (to_aux) {
		// it means it does not support CoW (dynamic mallocs?)
		if (aux_pool == NULL) aux_pool = (int*)malloc(SIZE_AUX_POOL);
		uintptr_t given_addr = (uintptr_t)addr;
		uintptr_t pool_addr = (uintptr_t)aux_pool;
		// place at random within the boundry
		given_addr = given_addr % (SIZE_AUX_POOL - size);
		given_addr = given_addr + pool_addr;
		void *new_addr = (void*)given_addr;
		memcpy(new_addr, buf, size);
		return 0;
	}

	memcpy(addr, buf, size);

	return 0;
}

void *MN_alloc(const char *file_name, size_t size)
{
	// TODO: do with mmap
	char *res;
	size_t missing = size % CACHE_LINE_SIZE;

	ALLOC_FN(res, char, size + missing);
	//    res = aligned_alloc(CACHE_LINE_SIZE, size + missing);
	//    res = malloc(size);

	return (void*) res;
}

void MN_free(void *ptr)
{
	// TODO: do with mmap
	free(ptr);
}

void MN_thr_enter()
{
	NH_spins_per_100 = SPINS_PER_100NS;
	MN_count_spins = 0;
	MN_time_spins = 0;
	MN_count_writes = 0;
}

void MN_thr_exit()
{
	mtx.lock();
	MN_count_spins_total        += MN_count_spins;
	MN_time_spins_total         += MN_time_spins;
	MN_count_writes_to_PM_total += MN_count_writes;
	mtx.unlock();
}

void MN_flush(void *addr, size_t size, int do_flush)
{
	int i;
	int size_cl = CACHE_LINE_SIZE / sizeof (char);
	int new_size = size / size_cl;

	// TODO: not cache_align flush

	if (size < size_cl) {
		new_size = 1;
	}

	for (i = 0; i < new_size; i += size_cl) {
		// TODO: addr may not be aligned
		if (do_flush) {
			ts_s _ts1_ = rdtscp();
			clflush(((char*) addr) + i); // does not need fence
			MN_count_spins++;
			MN_time_spins += rdtscp() - _ts1_;
		} else
			SPIN_PER_WRITE(1);
	}
}

void MN_drain()
{
	mfence();
}

void MN_learn_nb_nops() {
	const char *save_file = "ns_per_10_nops";
	FILE *fp = fopen(save_file, "r");

	if (fp == NULL) {
		// File does not exist
		unsigned long long ts1, ts2;
		double time;
		// in milliseconds (CPU_MAX_FREQ is in kilo)
		double ns100 = (double)NVM_LATENCY_NS * 1e-6; // moved to 500ns
		const unsigned long long test = 99999999;
		unsigned long long i = 0;
		double measured_cycles = 0;

		// CPU_MAX_FREQ is in kiloHz

		printf("CPU_MAX_FREQ=%llu\n", CPU_MAX_FREQ);

		fp = fopen(save_file, "w");

		ts1 = rdtscp();
		SPIN_10NOPS(test);
		ts2 = rdtscp();

		measured_cycles = ts2 - ts1;

		time = measured_cycles / (double) CPU_MAX_FREQ; // TODO:

		SPINS_PER_100NS = (double) test * (ns100 / time) + 1; // round up
		fprintf(fp, "%i\n", SPINS_PER_100NS);
		fclose(fp);
		printf("measured spins per 100ns: %i\n", SPINS_PER_100NS);
	} else {
		fscanf(fp, "%i\n", &SPINS_PER_100NS);
		fclose(fp);
	}
}
