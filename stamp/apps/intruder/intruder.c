#include <assert.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include "decoder.h"
#include "detector.h"
#include "dictionary.h"
#include "packet.h"
#include "stream.h"
#include "thread.h"
#include "timer.h"

#define MAIN_FUNCTION_FILE 1
#include "tm.h"

enum param_types {
    PARAM_ATTACK = (unsigned char)'a',
    PARAM_LENGTH = (unsigned char)'l',
    PARAM_NUM    = (unsigned char)'n',
    PARAM_SEED   = (unsigned char)'s',
    PARAM_THREAD = (unsigned char)'t'
};

enum param_defaults {
    PARAM_DEFAULT_ATTACK = 10,
    PARAM_DEFAULT_LENGTH = 128,
    PARAM_DEFAULT_NUM    = 1 << 18,
    PARAM_DEFAULT_SEED   = 1,
    PARAM_DEFAULT_THREAD = 1
};

long global_params[256] = { /* 256 = ascii limit */ };

static void global_param_init()
{
    global_params[PARAM_ATTACK] = PARAM_DEFAULT_ATTACK;
    global_params[PARAM_LENGTH] = PARAM_DEFAULT_LENGTH;
    global_params[PARAM_NUM]    = PARAM_DEFAULT_NUM;
    global_params[PARAM_SEED]   = PARAM_DEFAULT_SEED;
    global_params[PARAM_THREAD] = PARAM_DEFAULT_THREAD;
}

typedef struct arg {
  /* input: */
    stream_t* streamPtr;
    decoder_t* decoderPtr;
  /* output: */
    vector_t** errorVectors;
} arg_t;


/* =============================================================================
 * displayUsage
 * =============================================================================
 */
static void
displayUsage (const char* appName)
{
    printf("Usage: %s [options]\n", appName);
    puts("\nOptions:                            (defaults)\n");
    printf("    a <UINT>   Percent [a]ttack     (%i)\n", PARAM_DEFAULT_ATTACK);
    printf("    l <UINT>   Max data [l]ength    (%i)\n", PARAM_DEFAULT_LENGTH);
    printf("    n <UINT>   [n]umber of flows    (%i)\n", PARAM_DEFAULT_NUM);
    printf("    s <UINT>   Random [s]eed        (%i)\n", PARAM_DEFAULT_SEED);
    printf("    t <UINT>   Number of [t]hreads  (%i)\n", PARAM_DEFAULT_THREAD);
    exit(1);
}


/* =============================================================================
 * parseArgs
 * =============================================================================
 */
static void
parseArgs (long argc, char* const argv[])
{
    long i;
    long opt;

    opterr = 0;

    while ((opt = getopt(argc, argv, "a:l:n:s:t:")) != -1) {
        switch (opt) {
            case 'a':
            case 'l':
            case 'n':
            case 's':
            case 't':
                global_params[(unsigned char)opt] = atol(optarg);
                break;
            case '?':
            default:
                opterr++;
                break;
        }
    }

    for (i = optind; i < argc; i++) {
        fprintf(stderr, "Non-option argument: %s\n", argv[i]);
        opterr++;
    }

    if (opterr) {
        displayUsage(argv[0]);
    }
}


/* =============================================================================
 * processPackets
 * =============================================================================
 */
static void
processPackets (void* argPtr)
{
    TM_THREAD_ENTER();

    long threadId = thread_getId();

    stream_t*   streamPtr    = ((arg_t*)argPtr)->streamPtr;
    decoder_t*  decoderPtr   = ((arg_t*)argPtr)->decoderPtr;
    vector_t**  errorVectors = ((arg_t*)argPtr)->errorVectors;

    detector_t* detectorPtr = PDETECTOR_ALLOC();
    assert(detectorPtr);
    PDETECTOR_ADDPREPROCESSOR(detectorPtr, &preprocessor_toLower);

    vector_t* errorVectorPtr = errorVectors[threadId];

    while (1) {

        char* bytes;
	#ifdef HW_SW_PATHS
		IF_HTM_MODE
			START_HTM_MODE
        bytes = HW_TMSTREAM_GETPACKET(streamPtr);
			COMMIT_HTM_MODE
		ELSE_STM_MODE
			START_STM_MODE(RW)
	#else /* !HW_SW_PATHS */
      TM_BEGIN();
	#endif /* !HW_SW_PATHS */
        bytes = TMSTREAM_GETPACKET(streamPtr);
	#ifdef HW_SW_PATHS
			COMMIT_STM_MODE
	#else /* !HW_SW_PATHS */
      TM_END();
	#endif /* !HW_SW_PATHS */
        if (!bytes) {
            break;
        }

        packet_t* packetPtr = (packet_t*)bytes;
        long flowId = packetPtr->flowId;

        error_t error;
	#ifdef HW_SW_PATHS
		IF_HTM_MODE
			START_HTM_MODE
        error = HW_TMDECODER_PROCESS(decoderPtr,
                                  bytes,
                                  (PACKET_HEADER_LENGTH + packetPtr->length));
			COMMIT_HTM_MODE
		ELSE_STM_MODE
			START_STM_MODE(RW)
	#else /* !HW_SW_PATHS */
      TM_BEGIN();
	#endif /* !HW_SW_PATHS */
        error = TMDECODER_PROCESS(decoderPtr,
                                  bytes,
                                  (PACKET_HEADER_LENGTH + packetPtr->length));
	#ifdef HW_SW_PATHS
			COMMIT_STM_MODE
	#else /* !HW_SW_PATHS */
      TM_END();
	#endif /* !HW_SW_PATHS */
        if (error) {
            /*
             * Currently, stream_generate() does not create these errors.
             */
            assert(0);
            bool_t status = PVECTOR_PUSHBACK(errorVectorPtr, (void*)flowId);
            assert(status);
        }

        char* data;
        long decodedFlowId;
	#ifdef HW_SW_PATHS
		IF_HTM_MODE
			START_HTM_MODE
        data = HW_TMDECODER_GETCOMPLETE(decoderPtr, &decodedFlowId);
			COMMIT_HTM_MODE
		ELSE_STM_MODE
			START_STM_MODE(RW)
	#else /* !HW_SW_PATHS */
      TM_BEGIN();
	#endif /* !HW_SW_PATHS */
        data = TMDECODER_GETCOMPLETE(decoderPtr, &decodedFlowId);
	#ifdef HW_SW_PATHS
			COMMIT_STM_MODE
	#else /* !HW_SW_PATHS */
      TM_END();
	#endif /* !HW_SW_PATHS */
        if (data) {
            error_t error = PDETECTOR_PROCESS(detectorPtr, data);
            P_FREE(data);
            if (error) {
                bool_t status = PVECTOR_PUSHBACK(errorVectorPtr,
                                                 (void*)decodedFlowId);
                assert(status);
            }
        }

    }

    PDETECTOR_FREE(detectorPtr);

    TM_THREAD_EXIT();
}


/* =============================================================================
 * main
 * =============================================================================
 */
MAIN(argc, argv)
{
    GOTO_REAL();

    /*
     * Initialization
     */
#if defined(__x86_64__) || defined(__i386)
		unsigned int counterBefore,counterAfter;
#endif /* Intel RAPL */

    global_param_init();
    parseArgs(argc, (char** const)argv);
    long numThread = global_params[PARAM_THREAD];
    SIM_GET_NUM_CPU(numThread);
    TM_STARTUP(numThread);
    P_MEMORY_STARTUP(numThread);
    thread_startup(numThread);

    long percentAttack = global_params[PARAM_ATTACK];
    long maxDataLength = global_params[PARAM_LENGTH];
    long numFlow       = global_params[PARAM_NUM];
    long randomSeed    = global_params[PARAM_SEED];
    printf("Percent attack  = %li\n", percentAttack);
    printf("Max data length = %li\n", maxDataLength);
    printf("Num flow        = %li\n", numFlow);
    printf("Random seed     = %li\n", randomSeed);

    dictionary_t* dictionaryPtr = dictionary_alloc();
    assert(dictionaryPtr);
    stream_t* streamPtr = stream_alloc(percentAttack);
    assert(streamPtr);
    long numAttack = stream_generate(streamPtr,
                                     dictionaryPtr,
                                     numFlow,
                                     randomSeed,
                                     maxDataLength);
    printf("Num attack      = %li\n", numAttack);

    decoder_t* decoderPtr = decoder_alloc();
    assert(decoderPtr);

    vector_t** errorVectors = (vector_t**)SEQ_MALLOC(numThread * sizeof(vector_t*));
    assert(errorVectors);
    long i;
    for (i = 0; i < numThread; i++) {
        vector_t* errorVectorPtr = vector_alloc(numFlow);
        assert(errorVectorPtr);
        errorVectors[i] = errorVectorPtr;
    }

    arg_t arg;
    arg.streamPtr    = streamPtr;
    arg.decoderPtr   = decoderPtr;
    arg.errorVectors = errorVectors;

    /*
     * Run transactions
     */

    TIMER_T startTime;
#if defined(__x86_64__) || defined(__i386)
		counterBefore = msrGetCounter();
#endif /* Intel RAPL */
    TIMER_READ(startTime);
    GOTO_SIM();
#ifdef OTM
#pragma omp parallel
    {
        processPackets((void*)&arg);
    }
    
#else
    thread_start(processPackets, (void*)&arg);
#endif
    GOTO_REAL();
    TIMER_T stopTime;
    TIMER_READ(stopTime);
#if defined(__x86_64__) || defined(__i386)
		counterAfter = msrGetCounter();
#endif /* Intel RAPL */
    printf("Time            = %f\n", TIMER_DIFF_SECONDS(startTime, stopTime));

    /*
     * Check solution
     */

    long numFound = 0;
    for (i = 0; i < numThread; i++) {
        vector_t* errorVectorPtr = errorVectors[i];
        long e;
        long numError = vector_getSize(errorVectorPtr);
        numFound += numError;
        for (e = 0; e < numError; e++) {
            long flowId = (long)vector_at(errorVectorPtr, e);
            bool_t status = stream_isAttack(streamPtr, flowId);
            assert(status);
        }
    }
    printf("Num found       = %li\n", numFound);
    assert(numFound == numAttack);

    /*
     * Clean up
     */

    for (i = 0; i < numThread; i++) {
        vector_free(errorVectors[i]);
    }
    SEQ_FREE(errorVectors);
    decoder_free(decoderPtr);
    stream_free(streamPtr);
    dictionary_free(dictionaryPtr);
		
#if defined(__x86_64__) || defined(__i386)
		printf("\nEnergy = %lf J\n",msrDiffCounter(counterBefore,counterAfter));
#endif /* Intel RAPL */

    TM_SHUTDOWN();
    P_MEMORY_SHUTDOWN();
    GOTO_SIM();
    thread_shutdown();

    MAIN_RETURN(0);
}


/* =============================================================================
 *
 * End of intruder.c
 *
 * =============================================================================
 */
