/* =============================================================================
 *
 * list.c
 * -- Sorted singly linked list
 * -- Options: -DLIST_NO_DUPLICATES (default: allow duplicates)
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


#include <stdlib.h>
#include <assert.h>
#include "list.h"
#include "types.h"
#include "tm.h"

#ifdef __cplusplus
extern "C" {
#endif

/* =============================================================================
 * DECLARATION OF TM_SAFE FUNCTIONS
 * =============================================================================
 */

#ifdef HW_SW_PATHS
static
list_node_t*
HW_TMfindPrevious (list_t* listPtr, void* dataPtr);
#endif /* HW_SW_PATHS */

static TM_SAFE
list_node_t*
TMfindPrevious (TM_ARGDECL  list_t* listPtr, void* dataPtr);

#ifdef HW_SW_PATHS
static
void
HW_TMfreeList (list_node_t* nodePtr);
#endif /* HW_SW_PATHS */

static TM_SAFE
void
TMfreeList (TM_ARGDECL  list_node_t* nodePtr);

#ifdef HW_SW_PATHS
static
list_node_t*
HW_TMallocNode (void* dataPtr);
#endif /* HW_SW_PATHS */

static TM_SAFE
list_node_t*
TMallocNode (TM_ARGDECL  void* dataPtr);

#ifdef HW_SW_PATHS
static
void
HW_TMfreeNode (list_node_t* nodePtr);
#endif /* HW_SW_PATHS */

static TM_SAFE
void
TMfreeNode (TM_ARGDECL  list_node_t* nodePtr);

/* =============================================================================
 * compareDataPtrAddresses
 * -- Default compare function
 * =============================================================================
 */
static long
compareDataPtrAddresses (const void* a, const void* b)
{
    return ((long)a - (long)b);
}


/* =============================================================================
 * list_iter_reset
 * =============================================================================
 */
TM_PURE
void
list_iter_reset (list_iter_t* itPtr, list_t* listPtr)
{
    *itPtr = &(listPtr->head);
}

#ifdef HW_SW_PATHS
/* =============================================================================
 * HW_TMlist_iter_reset
 * =============================================================================
 */
void
HW_TMlist_iter_reset (list_iter_t* itPtr, list_t* listPtr)
{
    HW_TM_LOCAL_WRITE_P(*itPtr, &(listPtr->head));
}
#endif /* HW_SW_PATHS */

/* =============================================================================
 * TMlist_iter_reset
 * =============================================================================
 */
TM_SAFE
void
TMlist_iter_reset (TM_ARGDECL  list_iter_t* itPtr, list_t* listPtr)
{
    TM_LOCAL_WRITE_P(*itPtr, &(listPtr->head));
}


/* =============================================================================
 * list_iter_hasNext
 * =============================================================================
 */
TM_PURE
bool_t
list_iter_hasNext (list_iter_t* itPtr, list_t* listPtr)
{
    return (((*itPtr)->nextPtr != NULL) ? TRUE : FALSE);
}

#ifdef HW_SW_PATHS
/* =============================================================================
 * HW_TMlist_iter_hasNext
 * =============================================================================
 */
bool_t
HW_TMlist_iter_hasNext (list_iter_t* itPtr, list_t* listPtr)
{
    list_iter_t next = (list_iter_t)HW_TM_SHARED_READ_P((*itPtr)->nextPtr);

    return ((next != NULL) ? TRUE : FALSE);
}
#endif /* HW_SW_PATHS */

/* =============================================================================
 * TMlist_iter_hasNext
 * =============================================================================
 */
TM_SAFE
bool_t
TMlist_iter_hasNext (TM_ARGDECL  list_iter_t* itPtr, list_t* listPtr)
{
    list_iter_t next = (list_iter_t)TM_SHARED_READ_P((*itPtr)->nextPtr);

    return ((next != NULL) ? TRUE : FALSE);
}


/* =============================================================================
 * list_iter_next
 * =============================================================================
 */
TM_PURE
void*
list_iter_next (list_iter_t* itPtr, list_t* listPtr)
{
    *itPtr = (*itPtr)->nextPtr;

    return (*itPtr)->dataPtr;
}

#ifdef HW_SW_PATHS
/* =============================================================================
 * HW_TMlist_iter_next
 * =============================================================================
 */
void*
HW_TMlist_iter_next (list_iter_t* itPtr, list_t* listPtr)
{
    list_iter_t next = (list_iter_t)HW_TM_SHARED_READ_P((*itPtr)->nextPtr);
    HW_TM_LOCAL_WRITE_P(*itPtr, next);

    return HW_TM_SHARED_READ_P(next->dataPtr);
}
#endif /* HW_SW_PATHS */

/* =============================================================================
 * TMlist_iter_next
 * =============================================================================
 */
TM_SAFE
void*
TMlist_iter_next (TM_ARGDECL  list_iter_t* itPtr, list_t* listPtr)
{
    list_iter_t next = (list_iter_t)TM_SHARED_READ_P((*itPtr)->nextPtr);
    TM_LOCAL_WRITE_P(*itPtr, next);

    return TM_SHARED_READ_P(next->dataPtr);
}


/* =============================================================================
 * allocNode
 * -- Returns NULL on failure
 * =============================================================================
 */
static list_node_t*
allocNode (void* dataPtr)
{
    list_node_t* nodePtr = (list_node_t*)SEQ_MALLOC(sizeof(list_node_t));
    if (nodePtr == NULL) {
        return NULL;
    }

    nodePtr->dataPtr = dataPtr;
    nodePtr->nextPtr = NULL;

    return nodePtr;
}


/* =============================================================================
 * PallocNode
 * -- Returns NULL on failure
 * =============================================================================
 */
static list_node_t*
PallocNode (void* dataPtr)
{
    list_node_t* nodePtr = (list_node_t*)P_MALLOC(sizeof(list_node_t));
    if (nodePtr == NULL) {
        return NULL;
    }

    nodePtr->dataPtr = dataPtr;
    nodePtr->nextPtr = NULL;

    return nodePtr;
}

#ifdef HW_SW_PATHS
/* =============================================================================
 * HW_TMallocNode
 * -- Returns NULL on failure
 * =============================================================================
 */
static
list_node_t*
HW_TMallocNode (void* dataPtr)
{
    list_node_t* nodePtr = (list_node_t*)HW_TM_MALLOC(sizeof(list_node_t));
    if (nodePtr == NULL) {
        return NULL;
    }

    nodePtr->dataPtr = dataPtr;
    nodePtr->nextPtr = NULL;

    return nodePtr;
}
#endif /* HW_SW_PATHS */

/* =============================================================================
 * TMallocNode
 * -- Returns NULL on failure
 * =============================================================================
 */
static TM_SAFE
list_node_t*
TMallocNode (TM_ARGDECL  void* dataPtr)
{
    list_node_t* nodePtr = (list_node_t*)TM_MALLOC(sizeof(list_node_t));
    if (nodePtr == NULL) {
        return NULL;
    }

    nodePtr->dataPtr = dataPtr;
    nodePtr->nextPtr = NULL;

    return nodePtr;
}


/* =============================================================================
 * list_alloc
 * -- If NULL passed for 'compare' function, will compare data pointer addresses
 * -- Returns NULL on failure
 * =============================================================================
 */
list_t*
list_alloc (long (*compare)(const void*, const void*))
{
    list_t* listPtr = (list_t*)SEQ_MALLOC(sizeof(list_t));
    if (listPtr == NULL) {
        return NULL;
    }

    listPtr->head.dataPtr = NULL;
    listPtr->head.nextPtr = NULL;
    listPtr->size = 0;

    if (compare == NULL) {
        listPtr->compare = &compareDataPtrAddresses; /* default */
    } else {
        listPtr->compare = compare;
    }

    return listPtr;
}


/* =============================================================================
 * Plist_alloc
 * -- If NULL passed for 'compare' function, will compare data pointer addresses
 * -- Returns NULL on failure
 * =============================================================================
 */
list_t*
Plist_alloc (long (*compare)(const void*, const void*))
{
    list_t* listPtr = (list_t*)P_MALLOC(sizeof(list_t));
    if (listPtr == NULL) {
        return NULL;
    }

    listPtr->head.dataPtr = NULL;
    listPtr->head.nextPtr = NULL;
    listPtr->size = 0;

    if (compare == NULL) {
        listPtr->compare = &compareDataPtrAddresses; /* default */
    } else {
        listPtr->compare = compare;
    }

    return listPtr;
}

#ifdef HW_SW_PATHS
/* =============================================================================
 * HW_TMlist_alloc
 * -- If NULL passed for 'compare' function, will compare data pointer addresses
 * -- Returns NULL on failure
 * =============================================================================
 */
list_t*
HW_TMlist_alloc (long (*compare)(const void*, const void*))
{
    list_t* listPtr = (list_t*)HW_TM_MALLOC(sizeof(list_t));
    if (listPtr == NULL) {
        return NULL;
    }

    listPtr->head.dataPtr = NULL;
    listPtr->head.nextPtr = NULL;
    listPtr->size = 0;

    if (compare == NULL) {
        listPtr->compare = &compareDataPtrAddresses; /* default */
    } else {
        listPtr->compare = compare;
    }

    return listPtr;
}
#endif /* HW_SW_PATHS */

/* =============================================================================
 * TMlist_alloc
 * -- If NULL passed for 'compare' function, will compare data pointer addresses
 * -- Returns NULL on failure
 * =============================================================================
 */
TM_SAFE
list_t*
TMlist_alloc (TM_ARGDECL  long (*compare)(const void*, const void*))
{
    list_t* listPtr = (list_t*)TM_MALLOC(sizeof(list_t));
    if (listPtr == NULL) {
        return NULL;
    }

    listPtr->head.dataPtr = NULL;
    listPtr->head.nextPtr = NULL;
    listPtr->size = 0;

    if (compare == NULL) {
        listPtr->compare = &compareDataPtrAddresses; /* default */
    } else {
        listPtr->compare = compare;
    }

    return listPtr;
}


/* =============================================================================
 * freeNode
 * =============================================================================
 */
static void
freeNode (list_node_t* nodePtr)
{
    SEQ_FREE(nodePtr);
}


/* =============================================================================
 * PfreeNode
 * =============================================================================
 */
static void
PfreeNode (list_node_t* nodePtr)
{
    P_FREE(nodePtr);
}

#ifdef HW_SW_PATHS
/* =============================================================================
 * HW_TMfreeNode
 * =============================================================================
 */
static
void
HW_TMfreeNode (list_node_t* nodePtr)
{
    HW_TM_FREE(nodePtr);
}
#endif /* HW_SW_PATHS */

/* =============================================================================
 * TMfreeNode
 * =============================================================================
 */
static TM_SAFE
void
TMfreeNode (TM_ARGDECL  list_node_t* nodePtr)
{
    TM_FREE(nodePtr);
}


/* =============================================================================
 * freeList
 * =============================================================================
 */
static void
freeList (list_node_t* nodePtr)
{
    if (nodePtr != NULL) {
        freeList(nodePtr->nextPtr);
        freeNode(nodePtr);
    }
}


/* =============================================================================
 * PfreeList
 * =============================================================================
 */
static void
PfreeList (list_node_t* nodePtr)
{
    if (nodePtr != NULL) {
        PfreeList(nodePtr->nextPtr);
        PfreeNode(nodePtr);
    }
}

#ifdef HW_SW_PATHS
/* =============================================================================
 * HW_TMfreeList
 * =============================================================================
 */
static
void
HW_TMfreeList (list_node_t* nodePtr)
{
    if (nodePtr != NULL) {
        list_node_t* nextPtr = (list_node_t*)HW_TM_SHARED_READ_P(nodePtr->nextPtr);
        HW_TMfreeList(nextPtr);
        HW_TMfreeNode(nodePtr);
    }
}
#endif /* HW_SW_PATHS */

/* =============================================================================
 * TMfreeList
 * =============================================================================
 */
static TM_SAFE
void
TMfreeList (TM_ARGDECL  list_node_t* nodePtr)
{
    if (nodePtr != NULL) {
        list_node_t* nextPtr = (list_node_t*)TM_SHARED_READ_P(nodePtr->nextPtr);
        TMfreeList(TM_ARG  nextPtr);
        TMfreeNode(TM_ARG  nodePtr);
    }
}


/* =============================================================================
 * list_free
 * =============================================================================
 */
void
list_free (list_t* listPtr)
{
    freeList(listPtr->head.nextPtr);
    SEQ_FREE(listPtr);
}


/* =============================================================================
 * Plist_free
 * =============================================================================
 */
void
Plist_free (list_t* listPtr)
{
    PfreeList(listPtr->head.nextPtr);
    P_FREE(listPtr);
}

#ifdef HW_SW_PATHS
/* =============================================================================
 * HW_TMlist_free
 * =============================================================================
 */
void
HW_TMlist_free (list_t* listPtr)
{
    list_node_t* nextPtr = (list_node_t*)HW_TM_SHARED_READ_P(listPtr->head.nextPtr);
    HW_TMfreeList(nextPtr);
    HW_TM_FREE(listPtr);
}
#endif /* HW_SW_PATHS */

/* =============================================================================
 * TMlist_free
 * =============================================================================
 */
TM_SAFE
void
TMlist_free (TM_ARGDECL  list_t* listPtr)
{
    list_node_t* nextPtr = (list_node_t*)TM_SHARED_READ_P(listPtr->head.nextPtr);
    TMfreeList(TM_ARG  nextPtr);
    TM_FREE(listPtr);
}


/* =============================================================================
 * list_isEmpty
 * -- Return TRUE if list is empty, else FALSE
 * =============================================================================
 */
bool_t
list_isEmpty (list_t* listPtr)
{
    return (listPtr->head.nextPtr == NULL);
}

#ifdef HW_SW_PATHS
/* =============================================================================
 * HW_TMlist_isEmpty
 * -- Return TRUE if list is empty, else FALSE
 * =============================================================================
 */
bool_t
HW_TMlist_isEmpty (list_t* listPtr)
{
    return (((void*)HW_TM_SHARED_READ_P(listPtr->head.nextPtr) == NULL) ?
            TRUE : FALSE);
}
#endif /* HW_SW_PATHS */

/* =============================================================================
 * TMlist_isEmpty
 * -- Return TRUE if list is empty, else FALSE
 * =============================================================================
 */
TM_SAFE
bool_t
TMlist_isEmpty (TM_ARGDECL  list_t* listPtr)
{
    return (((void*)TM_SHARED_READ_P(listPtr->head.nextPtr) == NULL) ?
            TRUE : FALSE);
}


/* =============================================================================
 * list_getSize
 * -- Returns the size of the list
 * =============================================================================
 */
TM_PURE
long
list_getSize (list_t* listPtr)
{
    return listPtr->size;
}

#ifdef HW_SW_PATHS
/* =============================================================================
 * HW_TMlist_getSize
 * -- Returns the size of the list
 * =============================================================================
 */
long
HW_TMlist_getSize (list_t* listPtr)
{
    return (long)HW_TM_SHARED_READ(listPtr->size);
}
#endif /* HW_SW_PATHS */

/* =============================================================================
 * TMlist_getSize
 * -- Returns the size of the list
 * =============================================================================
 */
TM_SAFE
long
TMlist_getSize (TM_ARGDECL  list_t* listPtr)
{
    return (long)TM_SHARED_READ(listPtr->size);
}


/* =============================================================================
 * findPrevious
 * =============================================================================
 */
static list_node_t*
findPrevious (list_t* listPtr, void* dataPtr)
{
    list_node_t* prevPtr = &(listPtr->head);
    list_node_t* nodePtr = prevPtr->nextPtr;

    for (; nodePtr != NULL; nodePtr = nodePtr->nextPtr) {
        if (listPtr->compare(nodePtr->dataPtr, dataPtr) >= 0) {
            return prevPtr;
        }
        prevPtr = nodePtr;
    }

    return prevPtr;
}

#ifdef HW_SW_PATHS
/* =============================================================================
 * HW_TMfindPrevious
 * =============================================================================
 */
static
list_node_t*
HW_TMfindPrevious (list_t* listPtr, void* dataPtr)
{
    long ret;
    void *curDataPtr;
    list_node_t* nodePtr;
    list_node_t* prevPtr = &(listPtr->head);
    long (*compare)(const void*, const void*) = listPtr->compare;

    for (nodePtr = (list_node_t*)HW_TM_SHARED_READ_P(prevPtr->nextPtr);
         nodePtr != NULL;
         nodePtr = (list_node_t*)HW_TM_SHARED_READ_P(nodePtr->nextPtr))
    {
        curDataPtr = HW_TM_SHARED_READ_P(nodePtr->dataPtr);
        ret = compare(curDataPtr, dataPtr);
        if (ret >= 0) {
            return prevPtr;
        }
        prevPtr = nodePtr;
    }

    return prevPtr;
}
#endif /* HW_SW_PATHS */

/* =============================================================================
 * TMfindPrevious
 * =============================================================================
 */
static TM_SAFE
list_node_t*
TMfindPrevious (TM_ARGDECL  list_t* listPtr, void* dataPtr)
{
    long ret;
    void *curDataPtr;
    list_node_t* nodePtr;
    list_node_t* prevPtr = &(listPtr->head);
    long (*compare)(const void*, const void*) TM_IFUNC_DECL = listPtr->compare;

    for (nodePtr = (list_node_t*)TM_SHARED_READ_P(prevPtr->nextPtr);
         nodePtr != NULL;
         nodePtr = (list_node_t*)TM_SHARED_READ_P(nodePtr->nextPtr))
    {
        curDataPtr = TM_SHARED_READ_P(nodePtr->dataPtr);
        TM_IFUNC_CALL2(ret, compare, curDataPtr, dataPtr);
        if (ret >= 0) {
            return prevPtr;
        }
        prevPtr = nodePtr;
    }

    return prevPtr;
}


/* =============================================================================
 * list_find
 * -- Returns NULL if not found, else returns pointer to data
 * =============================================================================
 */
TM_PURE
void*
list_find (list_t* listPtr, void* dataPtr)
{
    list_node_t* nodePtr;
    list_node_t* prevPtr = findPrevious(listPtr, dataPtr);

    nodePtr = prevPtr->nextPtr;

    if ((nodePtr == NULL) ||
        (listPtr->compare(nodePtr->dataPtr, dataPtr) != 0)) {
        return NULL;
    }

    return (nodePtr->dataPtr);
}

#ifdef HW_SW_PATHS
/* =============================================================================
 * HW_TMlist_find
 * -- Returns NULL if not found, else returns pointer to data
 * =============================================================================
 */
void*
HW_TMlist_find (list_t* listPtr, void* dataPtr)
{
    long (*compare)(const void*, const void*) = listPtr->compare;
    long ret;
    void *curDataPtr;
    list_node_t* nodePtr;
    list_node_t* prevPtr = HW_TMfindPrevious(listPtr, dataPtr);

    nodePtr = (list_node_t*)HW_TM_SHARED_READ_P(prevPtr->nextPtr);
    if (nodePtr == NULL)
        return NULL;

    curDataPtr = HW_TM_SHARED_READ_P(nodePtr->dataPtr);
    ret = compare(curDataPtr, dataPtr);
    if (ret != 0)
        return NULL;

    return curDataPtr;
}
#endif /* HW_SW_PATHS */

/* =============================================================================
 * TMlist_find
 * -- Returns NULL if not found, else returns pointer to data
 * =============================================================================
 */
TM_SAFE
void*
TMlist_find (TM_ARGDECL  list_t* listPtr, void* dataPtr)
{
    long (*compare)(const void*, const void*) TM_IFUNC_DECL = listPtr->compare;
    long ret;
    void *curDataPtr;
    list_node_t* nodePtr;
    list_node_t* prevPtr = TMfindPrevious(TM_ARG  listPtr, dataPtr);

    nodePtr = (list_node_t*)TM_SHARED_READ_P(prevPtr->nextPtr);
    if (nodePtr == NULL)
        return NULL;

    curDataPtr = TM_SHARED_READ_P(nodePtr->dataPtr);
    TM_IFUNC_CALL2(ret, compare, curDataPtr, dataPtr);
    if (ret != 0)
        return NULL;

    return curDataPtr;
}


/* =============================================================================
 * list_insert
 * -- Return TRUE on success, else FALSE
 * =============================================================================
 */
bool_t
list_insert (list_t* listPtr, void* dataPtr)
{
    list_node_t* prevPtr;
    list_node_t* nodePtr;
    list_node_t* currPtr;

    prevPtr = findPrevious(listPtr, dataPtr);
    currPtr = prevPtr->nextPtr;

#ifdef LIST_NO_DUPLICATES
    if ((currPtr != NULL) &&
        listPtr->compare(currPtr->dataPtr, dataPtr) == 0) {
        return FALSE;
    }
#endif

    nodePtr = allocNode(dataPtr);
    if (nodePtr == NULL) {
        return FALSE;
    }

    nodePtr->nextPtr = currPtr;
    prevPtr->nextPtr = nodePtr;
    listPtr->size++;

    return TRUE;
}


/* =============================================================================
 * Plist_insert
 * -- Return TRUE on success, else FALSE
 * =============================================================================
 */
TM_PURE
bool_t
Plist_insert (list_t* listPtr, void* dataPtr)
{
    list_node_t* prevPtr;
    list_node_t* nodePtr;
    list_node_t* currPtr;

    prevPtr = findPrevious(listPtr, dataPtr);
    currPtr = prevPtr->nextPtr;

#ifdef LIST_NO_DUPLICATES
    if ((currPtr != NULL) &&
        listPtr->compare(currPtr->dataPtr, dataPtr) == 0) {
        return FALSE;
    }
#endif

    nodePtr = PallocNode(dataPtr);
    if (nodePtr == NULL) {
        return FALSE;
    }

    nodePtr->nextPtr = currPtr;
    prevPtr->nextPtr = nodePtr;
    listPtr->size++;

    return TRUE;
}

#ifdef HW_SW_PATHS
/* =============================================================================
 * HW_TMlist_insert
 * -- Return TRUE on success, else FALSE
 * =============================================================================
 */
bool_t
HW_TMlist_insert (list_t* listPtr, void* dataPtr)
{
    list_node_t* prevPtr;
    list_node_t* nodePtr;
    list_node_t* currPtr;

    prevPtr = HW_TMfindPrevious(listPtr, dataPtr);
    currPtr = (list_node_t*)HW_TM_SHARED_READ_P(prevPtr->nextPtr);

#ifdef LIST_NO_DUPLICATES
    if (currPtr != NULL) {
        long ret;
        void *curDataPtr = HW_TM_SHARED_READ_P(currPtr->dataPtr);
        long (*compare)(const void*, const void*) = listPtr->compare;

        ret = compare(curDataPtr, dataPtr);
        if (ret == 0) {
            return FALSE;
        }
    }
#endif

    nodePtr = HW_TMallocNode(dataPtr);
    if (nodePtr == NULL) {
        return FALSE;
    }

    nodePtr->nextPtr = currPtr;
    HW_TM_SHARED_WRITE_P(prevPtr->nextPtr, nodePtr);
    HW_TM_SHARED_WRITE(listPtr->size, (HW_TM_SHARED_READ(listPtr->size) + 1));

    return TRUE;
}
#endif /* HW_SW_PATHS */

/* =============================================================================
 * TMlist_insert
 * -- Return TRUE on success, else FALSE
 * =============================================================================
 */
TM_SAFE
bool_t
TMlist_insert (TM_ARGDECL  list_t* listPtr, void* dataPtr)
{
    list_node_t* prevPtr;
    list_node_t* nodePtr;
    list_node_t* currPtr;

    prevPtr = TMfindPrevious(TM_ARG  listPtr, dataPtr);
    currPtr = (list_node_t*)TM_SHARED_READ_P(prevPtr->nextPtr);

#ifdef LIST_NO_DUPLICATES
    if (currPtr != NULL) {
        long ret;
        void *curDataPtr = TM_SHARED_READ_P(currPtr->dataPtr);
        long (*compare)(const void*, const void*) TM_IFUNC_DECL = listPtr->compare;

        TM_IFUNC_CALL2(ret, compare, curDataPtr, dataPtr);
        if (ret == 0) {
            return FALSE;
        }
    }
#endif

    nodePtr = TMallocNode(TM_ARG  dataPtr);
    if (nodePtr == NULL) {
        return FALSE;
    }

    nodePtr->nextPtr = currPtr;
    TM_SHARED_WRITE_P(prevPtr->nextPtr, nodePtr);
    TM_SHARED_WRITE(listPtr->size, (TM_SHARED_READ(listPtr->size) + 1));

    return TRUE;
}


/* =============================================================================
 * list_remove
 * -- Returns TRUE if successful, else FALSE
 * =============================================================================
 */
bool_t
list_remove (list_t* listPtr, void* dataPtr)
{
    list_node_t* prevPtr;
    list_node_t* nodePtr;

    prevPtr = findPrevious(listPtr, dataPtr);

    nodePtr = prevPtr->nextPtr;
    if ((nodePtr != NULL) &&
        (listPtr->compare(nodePtr->dataPtr, dataPtr) == 0))
    {
        prevPtr->nextPtr = nodePtr->nextPtr;
        nodePtr->nextPtr = NULL;
        freeNode(nodePtr);
        listPtr->size--;
        assert(listPtr->size >= 0);
        return TRUE;
    }

    return FALSE;
}


/* =============================================================================
 * Plist_remove
 * -- Returns TRUE if successful, else FALSE
 * =============================================================================
 */
bool_t
Plist_remove (list_t* listPtr, void* dataPtr)
{
    list_node_t* prevPtr;
    list_node_t* nodePtr;

    prevPtr = findPrevious(listPtr, dataPtr);

    nodePtr = prevPtr->nextPtr;
    if ((nodePtr != NULL) &&
        (listPtr->compare(nodePtr->dataPtr, dataPtr) == 0))
    {
        prevPtr->nextPtr = nodePtr->nextPtr;
        nodePtr->nextPtr = NULL;
        PfreeNode(nodePtr);
        listPtr->size--;
        assert(listPtr->size >= 0);
        return TRUE;
    }

    return FALSE;
}

#ifdef HW_SW_PATHS
/* =============================================================================
 * HW_TMlist_remove
 * -- Returns TRUE if successful, else FALSE
 * =============================================================================
 */
bool_t
HW_TMlist_remove (list_t* listPtr, void* dataPtr)
{
    long (*compare)(const void*, const void*) = listPtr->compare;
    long ret;
    list_node_t* prevPtr;
    list_node_t* nodePtr;

    prevPtr = HW_TMfindPrevious(listPtr, dataPtr);

    nodePtr = (list_node_t*)HW_TM_SHARED_READ_P(prevPtr->nextPtr);
    if (nodePtr != NULL)
    {
        void * curDataPtr = HW_TM_SHARED_READ_P(nodePtr->dataPtr);
        ret = compare(curDataPtr, dataPtr);
        if (ret == 0)
        {
            HW_TM_SHARED_WRITE_P(prevPtr->nextPtr, HW_TM_SHARED_READ_P(nodePtr->nextPtr));
            HW_TM_SHARED_WRITE_P(nodePtr->nextPtr, (struct list_node*)NULL);
            HW_TMfreeNode(nodePtr);
            HW_TM_SHARED_WRITE(listPtr->size, (HW_TM_SHARED_READ(listPtr->size) - 1));
            assert(listPtr->size >= 0);
            return TRUE;
        }
    }

    return FALSE;
}
#endif /* HW_SW_PATHS */

/* =============================================================================
 * TMlist_remove
 * -- Returns TRUE if successful, else FALSE
 * =============================================================================
 */
TM_SAFE
bool_t
TMlist_remove (TM_ARGDECL  list_t* listPtr, void* dataPtr)
{
    long (*compare)(const void*, const void*) TM_IFUNC_DECL = listPtr->compare;
    long ret;
    list_node_t* prevPtr;
    list_node_t* nodePtr;

    prevPtr = TMfindPrevious(TM_ARG  listPtr, dataPtr);

    nodePtr = (list_node_t*)TM_SHARED_READ_P(prevPtr->nextPtr);
    if (nodePtr != NULL)
    {
        void * curDataPtr = TM_SHARED_READ_P(nodePtr->dataPtr);
        TM_IFUNC_CALL2(ret, compare, curDataPtr, dataPtr);
        if (ret == 0)
        {
            TM_SHARED_WRITE_P(prevPtr->nextPtr, TM_SHARED_READ_P(nodePtr->nextPtr));
            TM_SHARED_WRITE_P(nodePtr->nextPtr, (struct list_node*)NULL);
            TMfreeNode(TM_ARG  nodePtr);
            TM_SHARED_WRITE(listPtr->size, (TM_SHARED_READ(listPtr->size) - 1));
            assert(listPtr->size >= 0);
            return TRUE;
        }
    }

    return FALSE;
}


/* =============================================================================
 * list_clear
 * -- Removes all elements
 * =============================================================================
 */
void
list_clear (list_t* listPtr)
{
    freeList(listPtr->head.nextPtr);
    listPtr->head.nextPtr = NULL;
    listPtr->size = 0;
}


/* =============================================================================
 * Plist_clear
 * -- Removes all elements
 * =============================================================================
 */
TM_PURE
void
Plist_clear (list_t* listPtr)
{
    PfreeList(listPtr->head.nextPtr);
    listPtr->head.nextPtr = NULL;
    listPtr->size = 0;
}


/* =============================================================================
 * TEST_LIST
 * =============================================================================
 */
#ifdef TEST_LIST


#include <assert.h>
#include <stdio.h>


static long
compare (const void* a, const void* b)
{
    return (*((const long*)a) - *((const long*)b));
}


static void
printList (list_t* listPtr)
{
    list_iter_t it;
    printf("[");
    list_iter_reset(&it, listPtr);
    while (list_iter_hasNext(&it, listPtr)) {
        printf("%li ", *((long*)(list_iter_next(&it, listPtr))));
    }
    puts("]");
}


static void
insertInt (list_t* listPtr, long* data)
{
    printf("Inserting: %li\n", *data);
    list_insert(listPtr, (void*)data);
    printList(listPtr);
}


static void
removeInt (list_t* listPtr, long* data)
{
    printf("Removing: %li\n", *data);
    list_remove(listPtr, (void*)data);
    printList(listPtr);
}


int
main ()
{
    list_t* listPtr;
#ifdef LIST_NO_DUPLICATES
    long data1[] = {3, 1, 4, 1, 5, -1};
#else
    long data1[] = {3, 1, 4, 5, -1};
#endif
    long data2[] = {3, 1, 4, 1, 5, -1};
    long i;

    puts("Starting...");

    puts("List sorted by values:");

    listPtr = list_alloc(&compare);

    for (i = 0; data1[i] >= 0; i++) {
        insertInt(listPtr, &data1[i]);
        assert(*((long*)list_find(listPtr, &data1[i])) == data1[i]);
    }

    for (i = 0; data1[i] >= 0; i++) {
        removeInt(listPtr, &data1[i]);
        assert(list_find(listPtr, &data1[i]) == NULL);
    }

    list_free(listPtr);

    puts("List sorted by addresses:");

    listPtr = list_alloc(NULL);

    for (i = 0; data2[i] >= 0; i++) {
        insertInt(listPtr, &data2[i]);
        assert(*((long*)list_find(listPtr, &data2[i])) == data2[i]);
    }

    for (i = 0; data2[i] >= 0; i++) {
        removeInt(listPtr, &data2[i]);
        assert(list_find(listPtr, &data2[i]) == NULL);
    }

    list_free(listPtr);

    puts("Done.");

    return 0;
}


#endif /* TEST_LIST */

#ifdef __cplusplus
}
#endif

/* =============================================================================
 *
 * End of list.c
 *
 * =============================================================================
 */
