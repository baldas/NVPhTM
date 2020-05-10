/* =============================================================================
 *
 * decoder.c
 *
 * =============================================================================
 *
 * Copyright (C) Stanford University, 2006.  All Rights Reserved.
 * Author: Chi Cao Minh
 *
 * =============================================================================
 *
 * For the license of bayes/sort.h and bayes/sort.c, please see the header
 * of the files.
 * 
 * ------------------------------------------------------------------------
 * 
 * For the license of kmeans, please see kmeans/LICENSE.kmeans
 * 
 * ------------------------------------------------------------------------
 * 
 * For the license of ssca2, please see ssca2/COPYRIGHT
 * 
 * ------------------------------------------------------------------------
 * 
 * For the license of lib/mt19937ar.c and lib/mt19937ar.h, please see the
 * header of the files.
 * 
 * ------------------------------------------------------------------------
 * 
 * For the license of lib/rbtree.h and lib/rbtree.c, please see
 * lib/LEGALNOTICE.rbtree and lib/LICENSE.rbtree
 * 
 * ------------------------------------------------------------------------
 * 
 * Unless otherwise noted, the following license applies to STAMP files:
 * 
 * Copyright (c) 2007, Stanford University
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * 
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 * 
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 * 
 *     * Neither the name of Stanford University nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY STANFORD UNIVERSITY ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL STANFORD UNIVERSITY BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 *
 * =============================================================================
 */

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "decoder.h"
#include "error.h"
#include "list.h"
#include "map.h"
#include "packet.h"
#include "queue.h"
#include "tm.h"
#include "types.h"


struct decoder {
    MAP_T* fragmentedMapPtr;  /* contains list of packet_t* */
    queue_t* decodedQueuePtr; /* contains decoded_t* */
};

typedef struct decoded {
    long flowId;
    char* data;
} decoded_t;


/* =============================================================================
 * decoder_alloc
 * =============================================================================
 */
decoder_t*
decoder_alloc ()
{
    decoder_t* decoderPtr;

    decoderPtr = (decoder_t*)SEQ_MALLOC(sizeof(decoder_t));
    if (decoderPtr) {
        decoderPtr->fragmentedMapPtr = MAP_ALLOC(NULL, NULL);
        assert(decoderPtr->fragmentedMapPtr);
        decoderPtr->decodedQueuePtr = queue_alloc(1024);
        assert(decoderPtr->decodedQueuePtr);
    }

    return decoderPtr;
}


/* =============================================================================
 * decoder_free
 * =============================================================================
 */
void
decoder_free (decoder_t* decoderPtr)
{
    queue_free(decoderPtr->decodedQueuePtr);
    MAP_FREE(decoderPtr->fragmentedMapPtr);
    SEQ_FREE(decoderPtr);
}


/* =============================================================================
 * decoder_process
 * =============================================================================
 */
error_t
decoder_process (decoder_t* decoderPtr, char* bytes, long numByte)
{
    bool_t status;

    /*
     * Basic error checking
     */

    if (numByte < PACKET_HEADER_LENGTH) {
        return ERROR_SHORT;
    }

    packet_t* packetPtr = (packet_t*)bytes;
    long flowId      = packetPtr->flowId;
    long fragmentId  = packetPtr->fragmentId;
    long numFragment = packetPtr->numFragment;
    long length      = packetPtr->length;

    if (flowId < 0) {
        return ERROR_FLOWID;
    }

    if ((fragmentId < 0) || (fragmentId >= numFragment)) {
        return ERROR_FRAGMENTID;
    }

    if (length < 0) {
        return ERROR_LENGTH;
    }

#if 0
    /*
     * With the above checks, this one is redundant
     */
    if (numFragment < 1) {
        return ERROR_NUMFRAGMENT;
    }
#endif

    /*
     * Add to fragmented map for reassembling
     */

    if (numFragment > 1) {

        MAP_T* fragmentedMapPtr = decoderPtr->fragmentedMapPtr;
        list_t* fragmentListPtr =
            (list_t*)MAP_FIND(fragmentedMapPtr, (void*)flowId);

        if (fragmentListPtr == NULL) {

            fragmentListPtr = list_alloc(&packet_compareFragmentId);
            assert(fragmentListPtr);
            status = list_insert(fragmentListPtr, (void*)packetPtr);
            assert(status);
            status = MAP_INSERT(fragmentedMapPtr,
                                (void*)flowId,
                                (void*)fragmentListPtr);
            assert(status);

        } else {

            list_iter_t it;
            list_iter_reset(&it, fragmentListPtr);
            assert(list_iter_hasNext(&it, fragmentListPtr));
            packet_t* firstFragmentPtr =
                (packet_t*)list_iter_next(&it, fragmentListPtr);
            long expectedNumFragment = firstFragmentPtr->numFragment;

            if (numFragment != expectedNumFragment) {
                status = MAP_REMOVE(fragmentedMapPtr, (void*)flowId);
                assert(status);
                return ERROR_NUMFRAGMENT;
            }

            status = list_insert(fragmentListPtr, (void*)packetPtr);
            assert(status);

            /*
             * If we have all the fragments we can reassemble them
             */

            if (list_getSize(fragmentListPtr) == numFragment) {

                long numByte = 0;
                long i = 0;
                list_iter_reset(&it, fragmentListPtr);
                while (list_iter_hasNext(&it, fragmentListPtr)) {
                    packet_t* fragmentPtr =
                        (packet_t*)list_iter_next(&it, fragmentListPtr);
                    assert(fragmentPtr->flowId == flowId);
                    if (fragmentPtr->fragmentId != i) {
                        status = MAP_REMOVE(fragmentedMapPtr, (void*)flowId);
                        assert(status);
                        return ERROR_INCOMPLETE; /* should be sequential */
                    }
                    numByte += fragmentPtr->length;
                    i++;
                }

                char* data = (char*)SEQ_MALLOC(numByte + 1);
                assert(data);
                data[numByte] = '\0';
                char* dst = data;
                list_iter_reset(&it, fragmentListPtr);
                while (list_iter_hasNext(&it, fragmentListPtr)) {
                    packet_t* fragmentPtr =
                        (packet_t*)list_iter_next(&it, fragmentListPtr);
                    memcpy(dst, fragmentPtr->data, fragmentPtr->length);
                    dst += fragmentPtr->length;
                }
                assert(dst == data + numByte);

                decoded_t* decodedPtr = (decoded_t*)SEQ_MALLOC(sizeof(decoded_t));
                assert(decodedPtr);
                decodedPtr->flowId = flowId;
                decodedPtr->data = data;

                queue_t* decodedQueuePtr = decoderPtr->decodedQueuePtr;
                status = queue_push(decodedQueuePtr, (void*)decodedPtr);
                assert(status);

                list_free(fragmentListPtr);
                status = MAP_REMOVE(fragmentedMapPtr, (void*)flowId);
                assert(status);
            }

        }

    } else {

        /*
         * This is the only fragment, so it is ready
         */

        if (fragmentId != 0) {
            return ERROR_FRAGMENTID;
        }

        char* data = (char*)SEQ_MALLOC(length + 1);
        assert(data);
        data[length] = '\0';
        memcpy(data, packetPtr->data, length);

        decoded_t* decodedPtr = (decoded_t*)SEQ_MALLOC(sizeof(decoded_t));
        assert(decodedPtr);
        decodedPtr->flowId = flowId;
        decodedPtr->data = data;

        queue_t* decodedQueuePtr = decoderPtr->decodedQueuePtr;
        status = queue_push(decodedQueuePtr, (void*)decodedPtr);
        assert(status);

    }

    return ERROR_NONE;
}

#ifdef HW_SW_PATHS
/* =============================================================================
 * HW_TMdecoder_process
 * =============================================================================
 */
error_t
HW_TMdecoder_process (decoder_t* decoderPtr, char* bytes, long numByte)
{
    bool_t status;

    /*
     * Basic error checking
     */

    if (numByte < PACKET_HEADER_LENGTH) {
        return ERROR_SHORT;
    }

    packet_t* packetPtr = (packet_t*)bytes;
    long flowId      = packetPtr->flowId;
    long fragmentId  = packetPtr->fragmentId;
    long numFragment = packetPtr->numFragment;
    long length      = packetPtr->length;

    if (flowId < 0) {
        return ERROR_FLOWID;
    }

    if ((fragmentId < 0) || (fragmentId >= numFragment)) {
        return ERROR_FRAGMENTID;
    }

    if (length < 0) {
        return ERROR_LENGTH;
    }

#if 0
    /*
     * With the above checks, this one is redundant
     */
    if (numFragment < 1) {
        return ERROR_NUMFRAGMENT;
    }
#endif

    /*
     * Add to fragmented map for reassembling
     */

    if (numFragment > 1) {

        MAP_T* fragmentedMapPtr = decoderPtr->fragmentedMapPtr;
        list_t* fragmentListPtr =
            (list_t*)HW_TMMAP_FIND(fragmentedMapPtr, (void*)flowId);

        if (fragmentListPtr == NULL) {

            fragmentListPtr = HW_TMLIST_ALLOC(&packet_compareFragmentId);
            assert(fragmentListPtr);
            status = HW_TMLIST_INSERT(fragmentListPtr, (void*)packetPtr);
            assert(status);
            status = HW_TMMAP_INSERT(fragmentedMapPtr,
                                  (void*)flowId,
                                  (void*)fragmentListPtr);
            assert(status);

        } else {

            list_iter_t it;
            HW_TMLIST_ITER_RESET(&it, fragmentListPtr);
            assert(HW_TMLIST_ITER_HASNEXT(&it, fragmentListPtr));
            packet_t* firstFragmentPtr =
                (packet_t*)HW_TMLIST_ITER_NEXT(&it, fragmentListPtr);
            long expectedNumFragment = firstFragmentPtr->numFragment;

            if (numFragment != expectedNumFragment) {
                status = HW_TMMAP_REMOVE(fragmentedMapPtr, (void*)flowId);
                assert(status);
                return ERROR_NUMFRAGMENT;
            }

            status = HW_TMLIST_INSERT(fragmentListPtr, (void*)packetPtr);
            assert(status);

            /*
             * If we have all the fragments we can reassemble them
             */

            if (HW_TMLIST_GETSIZE(fragmentListPtr) == numFragment) {

                long numByte = 0;
                long i = 0;
                HW_TMLIST_ITER_RESET(&it, fragmentListPtr);
                while (HW_TMLIST_ITER_HASNEXT(&it, fragmentListPtr)) {
                    packet_t* fragmentPtr =
                        (packet_t*)HW_TMLIST_ITER_NEXT(&it, fragmentListPtr);
                    assert(fragmentPtr->flowId == flowId);
                    if (fragmentPtr->fragmentId != i) {
                        status = HW_TMMAP_REMOVE(fragmentedMapPtr, (void*)flowId);
                        assert(status);
                        return ERROR_INCOMPLETE; /* should be sequential */
                    }
                    numByte += fragmentPtr->length;
                    i++;
                }

                char* data = (char*)HW_TM_MALLOC(numByte + 1);
                assert(data);
                data[numByte] = '\0';
                char* dst = data;
                HW_TMLIST_ITER_RESET(&it, fragmentListPtr);
                while (HW_TMLIST_ITER_HASNEXT(&it, fragmentListPtr)) {
                    packet_t* fragmentPtr =
                        (packet_t*)HW_TMLIST_ITER_NEXT(&it, fragmentListPtr);
                    memcpy(dst, fragmentPtr->data, fragmentPtr->length);
                    dst += fragmentPtr->length;
                }
                assert(dst == data + numByte);

                decoded_t* decodedPtr = (decoded_t*)HW_TM_MALLOC(sizeof(decoded_t));
                assert(decodedPtr);
                decodedPtr->flowId = flowId;
                decodedPtr->data = data;

                queue_t* decodedQueuePtr = decoderPtr->decodedQueuePtr;
                status = HW_TMQUEUE_PUSH(decodedQueuePtr, (void*)decodedPtr);
                assert(status);

                HW_TMLIST_FREE(fragmentListPtr);
                status = HW_TMMAP_REMOVE(fragmentedMapPtr, (void*)flowId);
                assert(status);
            }

        }

    } else {

        /*
         * This is the only fragment, so it is ready
         */

        if (fragmentId != 0) {
            return ERROR_FRAGMENTID;
        }

        char* data = (char*)HW_TM_MALLOC(length + 1);
        assert(data);
        data[length] = '\0';
        memcpy(data, packetPtr->data, length);

        decoded_t* decodedPtr = (decoded_t*)HW_TM_MALLOC(sizeof(decoded_t));
        assert(decodedPtr);
        decodedPtr->flowId = flowId;
        decodedPtr->data = data;

        queue_t* decodedQueuePtr = decoderPtr->decodedQueuePtr;
        status = HW_TMQUEUE_PUSH(decodedQueuePtr, (void*)decodedPtr);
        assert(status);

    }

    return ERROR_NONE;
}
#endif /* HW_SW_PATHS */

/* =============================================================================
 * TMdecoder_process
 * =============================================================================
 */
TM_SAFE
error_t
TMdecoder_process (TM_ARGDECL  decoder_t* decoderPtr, char* bytes, long numByte)
{
    bool_t status;

    /*
     * Basic error checking
     */

    if (numByte < PACKET_HEADER_LENGTH) {
        return ERROR_SHORT;
    }

    packet_t* packetPtr = (packet_t*)bytes;
    long flowId      = packetPtr->flowId;
    long fragmentId  = packetPtr->fragmentId;
    long numFragment = packetPtr->numFragment;
    long length      = packetPtr->length;

    if (flowId < 0) {
        return ERROR_FLOWID;
    }

    if ((fragmentId < 0) || (fragmentId >= numFragment)) {
        return ERROR_FRAGMENTID;
    }

    if (length < 0) {
        return ERROR_LENGTH;
    }

#if 0
    /*
     * With the above checks, this one is redundant
     */
    if (numFragment < 1) {
        return ERROR_NUMFRAGMENT;
    }
#endif

    /*
     * Add to fragmented map for reassembling
     */

    if (numFragment > 1) {

        MAP_T* fragmentedMapPtr = decoderPtr->fragmentedMapPtr;
        list_t* fragmentListPtr =
            (list_t*)TMMAP_FIND(fragmentedMapPtr, (void*)flowId);

        if (fragmentListPtr == NULL) {

            fragmentListPtr = TMLIST_ALLOC(&packet_compareFragmentId);
            assert(fragmentListPtr);
            status = TMLIST_INSERT(fragmentListPtr, (void*)packetPtr);
            assert(status);
            status = TMMAP_INSERT(fragmentedMapPtr,
                                  (void*)flowId,
                                  (void*)fragmentListPtr);
            assert(status);

        } else {

            list_iter_t it;
            TMLIST_ITER_RESET(&it, fragmentListPtr);
            assert(TMLIST_ITER_HASNEXT(&it, fragmentListPtr));
            packet_t* firstFragmentPtr =
                (packet_t*)TMLIST_ITER_NEXT(&it, fragmentListPtr);
            long expectedNumFragment = firstFragmentPtr->numFragment;

            if (numFragment != expectedNumFragment) {
                status = TMMAP_REMOVE(fragmentedMapPtr, (void*)flowId);
                assert(status);
                return ERROR_NUMFRAGMENT;
            }

            status = TMLIST_INSERT(fragmentListPtr, (void*)packetPtr);
            assert(status);

            /*
             * If we have all the fragments we can reassemble them
             */

            if (TMLIST_GETSIZE(fragmentListPtr) == numFragment) {

                long numByte = 0;
                long i = 0;
                TMLIST_ITER_RESET(&it, fragmentListPtr);
                while (TMLIST_ITER_HASNEXT(&it, fragmentListPtr)) {
                    packet_t* fragmentPtr =
                        (packet_t*)TMLIST_ITER_NEXT(&it, fragmentListPtr);
                    assert(fragmentPtr->flowId == flowId);
                    if (fragmentPtr->fragmentId != i) {
                        status = TMMAP_REMOVE(fragmentedMapPtr, (void*)flowId);
                        assert(status);
                        return ERROR_INCOMPLETE; /* should be sequential */
                    }
                    numByte += fragmentPtr->length;
                    i++;
                }

                char* data = (char*)TM_MALLOC(numByte + 1);
                assert(data);
                data[numByte] = '\0';
                char* dst = data;
                TMLIST_ITER_RESET(&it, fragmentListPtr);
                while (TMLIST_ITER_HASNEXT(&it, fragmentListPtr)) {
                    packet_t* fragmentPtr =
                        (packet_t*)TMLIST_ITER_NEXT(&it, fragmentListPtr);
                    memcpy(dst, fragmentPtr->data, fragmentPtr->length);
                    dst += fragmentPtr->length;
                }
                assert(dst == data + numByte);

                decoded_t* decodedPtr = (decoded_t*)TM_MALLOC(sizeof(decoded_t));
                assert(decodedPtr);
                decodedPtr->flowId = flowId;
                decodedPtr->data = data;

                queue_t* decodedQueuePtr = decoderPtr->decodedQueuePtr;
                status = TMQUEUE_PUSH(decodedQueuePtr, (void*)decodedPtr);
                assert(status);

                TMLIST_FREE(fragmentListPtr);
                status = TMMAP_REMOVE(fragmentedMapPtr, (void*)flowId);
                assert(status);
            }

        }

    } else {

        /*
         * This is the only fragment, so it is ready
         */

        if (fragmentId != 0) {
            return ERROR_FRAGMENTID;
        }

        char* data = (char*)TM_MALLOC(length + 1);
        assert(data);
        data[length] = '\0';
        memcpy(data, packetPtr->data, length);

        decoded_t* decodedPtr = (decoded_t*)TM_MALLOC(sizeof(decoded_t));
        assert(decodedPtr);
        decodedPtr->flowId = flowId;
        decodedPtr->data = data;

        queue_t* decodedQueuePtr = decoderPtr->decodedQueuePtr;
        status = TMQUEUE_PUSH(decodedQueuePtr, (void*)decodedPtr);
        assert(status);

    }

    return ERROR_NONE;
}


/* =============================================================================
 * decoder_getComplete
 * -- If none, returns NULL
 * =============================================================================
 */
char*
decoder_getComplete (decoder_t* decoderPtr, long* decodedFlowIdPtr)
{
    char* data;
    decoded_t* decodedPtr = (decoded_t*)queue_pop(decoderPtr->decodedQueuePtr);

    if (decodedPtr) {
        *decodedFlowIdPtr = decodedPtr->flowId;
        data = decodedPtr->data;
        SEQ_FREE(decodedPtr);
    } else {
        *decodedFlowIdPtr = -1;
        data = NULL;
    }

    return data;
}

#ifdef HW_SW_PATHS
/* =============================================================================
 * HW_TMdecoder_getComplete
 * -- If none, returns NULL
 * =============================================================================
 */
char*
HW_TMdecoder_getComplete (decoder_t* decoderPtr, long* decodedFlowIdPtr)
{
    char* data;
    decoded_t* decodedPtr = (decoded_t*)HW_TMQUEUE_POP(decoderPtr->decodedQueuePtr);

    if (decodedPtr) {
        *decodedFlowIdPtr = decodedPtr->flowId;
        data = decodedPtr->data;
        HW_TM_FREE(decodedPtr);
    } else {
        *decodedFlowIdPtr = -1;
        data = NULL;
    }

    return data;
}
#endif /* HW_SW_PATHS */

/* =============================================================================
 * TMdecoder_getComplete
 * -- If none, returns NULL
 * =============================================================================
 */
TM_SAFE
char*
TMdecoder_getComplete (TM_ARGDECL  decoder_t* decoderPtr, long* decodedFlowIdPtr)
{
    char* data;
    decoded_t* decodedPtr = (decoded_t*)TMQUEUE_POP(decoderPtr->decodedQueuePtr);

    if (decodedPtr) {
        *decodedFlowIdPtr = decodedPtr->flowId;
        data = decodedPtr->data;
        TM_FREE(decodedPtr);
    } else {
        *decodedFlowIdPtr = -1;
        data = NULL;
    }

    return data;
}


/* #############################################################################
 * TEST_DECODER
 * #############################################################################
 */
#ifdef TEST_DECODER

#include <stdio.h>


int
main ()
{
    decoder_t* decoderPtr;

    puts("Starting...");

    decoderPtr = decoder_alloc();
    assert(decoderPtr);

    long numDataByte = 3;
    long numPacketByte = PACKET_HEADER_LENGTH + numDataByte;

    char* abcBytes = (char*)SEQ_MALLOC(numPacketByte);
    assert(abcBytes);
    packet_t* abcPacketPtr;
    abcPacketPtr = (packet_t*)abcBytes;
    abcPacketPtr->flowId = 1;
    abcPacketPtr->fragmentId = 0;
    abcPacketPtr->numFragment = 2;
    abcPacketPtr->length = numDataByte;
    abcPacketPtr->data[0] = 'a';
    abcPacketPtr->data[1] = 'b';
    abcPacketPtr->data[2] = 'c';

    char* defBytes = (char*)SEQ_MALLOC(numPacketByte);
    assert(defBytes);
    packet_t* defPacketPtr;
    defPacketPtr = (packet_t*)defBytes;
    defPacketPtr->flowId = 1;
    defPacketPtr->fragmentId = 1;
    defPacketPtr->numFragment = 2;
    defPacketPtr->length = numDataByte;
    defPacketPtr->data[0] = 'd';
    defPacketPtr->data[1] = 'e';
    defPacketPtr->data[2] = 'f';

    assert(decoder_process(decoderPtr, abcBytes, numDataByte) == ERROR_SHORT);

    abcPacketPtr->flowId = -1;
    assert(decoder_process(decoderPtr, abcBytes, numPacketByte) == ERROR_FLOWID);
    abcPacketPtr->flowId = 1;

    abcPacketPtr->fragmentId = -1;
    assert(decoder_process(decoderPtr, abcBytes, numPacketByte) == ERROR_FRAGMENTID);
    abcPacketPtr->fragmentId = 0;

    abcPacketPtr->fragmentId = 2;
    assert(decoder_process(decoderPtr, abcBytes, numPacketByte) == ERROR_FRAGMENTID);
    abcPacketPtr->fragmentId = 0;

    abcPacketPtr->fragmentId = 2;
    assert(decoder_process(decoderPtr, abcBytes, numPacketByte) == ERROR_FRAGMENTID);
    abcPacketPtr->fragmentId = 0;

    abcPacketPtr->length = -1;
    assert(decoder_process(decoderPtr, abcBytes, numPacketByte) == ERROR_LENGTH);
    abcPacketPtr->length = numDataByte;

    assert(decoder_process(decoderPtr, abcBytes, numPacketByte) == ERROR_NONE);
    defPacketPtr->numFragment = 3;
    assert(decoder_process(decoderPtr, defBytes, numPacketByte) == ERROR_NUMFRAGMENT);
    defPacketPtr->numFragment = 2;

    assert(decoder_process(decoderPtr, abcBytes, numPacketByte) == ERROR_NONE);
    defPacketPtr->fragmentId = 0;
    assert(decoder_process(decoderPtr, defBytes, numPacketByte) == ERROR_INCOMPLETE);
    defPacketPtr->fragmentId = 1;

    long flowId;
    assert(decoder_process(decoderPtr, defBytes, numPacketByte) == ERROR_NONE);
    assert(decoder_process(decoderPtr, abcBytes, numPacketByte) == ERROR_NONE);
    char* str = decoder_getComplete(decoderPtr, &flowId);
    assert(strcmp(str, "abcdef") == 0);
    SEQ_FREE(str);
    assert(flowId == 1);

    abcPacketPtr->numFragment = 1;
    assert(decoder_process(decoderPtr, abcBytes, numPacketByte) == ERROR_NONE);
    str = decoder_getComplete(decoderPtr, &flowId);
    assert(strcmp(str, "abc") == 0);
    SEQ_FREE(str);
    abcPacketPtr->numFragment = 2;
    assert(flowId == 1);

    str = decoder_getComplete(decoderPtr, &flowId);
    assert(str == NULL);
    assert(flowId == -1);

    decoder_free(decoderPtr);

    SEQ_FREE(abcBytes);
    SEQ_FREE(defBytes);

    puts("All tests passed.");

    return 0;
}


#endif /* TEST_DECODER */


/* =============================================================================
 *
 * End of decoder.c
 *
 * =============================================================================
 */