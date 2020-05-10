/* =============================================================================
 *
 * router.c
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
#include "coordinate.h"
#include "grid.h"
#include "queue.h"
#include "router.h"
#include "tm.h"
#include "thread.h"
#include "vector.h"


typedef enum momentum {
    MOMENTUM_ZERO = 0,
    MOMENTUM_POSX = 1,
    MOMENTUM_POSY = 2,
    MOMENTUM_POSZ = 3,
    MOMENTUM_NEGX = 4,
    MOMENTUM_NEGY = 5,
    MOMENTUM_NEGZ = 6
} momentum_t;

typedef struct point {
    long x;
    long y;
    long z;
    long value;
    momentum_t momentum;
} point_t;

point_t MOVE_POSX = { 1,  0,  0,  0, MOMENTUM_POSX};
point_t MOVE_POSY = { 0,  1,  0,  0, MOMENTUM_POSY};
point_t MOVE_POSZ = { 0,  0,  1,  0, MOMENTUM_POSZ};
point_t MOVE_NEGX = {-1,  0,  0,  0, MOMENTUM_NEGX};
point_t MOVE_NEGY = { 0, -1,  0,  0, MOMENTUM_NEGY};
point_t MOVE_NEGZ = { 0,  0, -1,  0, MOMENTUM_NEGZ};

/* =============================================================================
 * router_alloc
 * =============================================================================
 */
router_t*
router_alloc (long xCost, long yCost, long zCost, long bendCost)
{
    router_t* routerPtr;

    routerPtr = (router_t*)SEQ_MALLOC(sizeof(router_t));
    if (routerPtr) {
        routerPtr->xCost = xCost;
        routerPtr->yCost = yCost;
        routerPtr->zCost = zCost;
        routerPtr->bendCost = bendCost;
    }

    return routerPtr;
}


/* =============================================================================
 * router_free
 * =============================================================================
 */
void
router_free (router_t* routerPtr)
{
    SEQ_FREE(routerPtr);
}


/* =============================================================================
 * PexpandToNeighbor
 * =============================================================================
 */
static void
PexpandToNeighbor (grid_t* myGridPtr,
                   long x, long y, long z, long value, queue_t* queuePtr)
{
    if (grid_isPointValid(myGridPtr, x, y, z)) {
        long* neighborGridPointPtr = grid_getPointRef(myGridPtr, x, y, z);
        long neighborValue = *neighborGridPointPtr;
        if (neighborValue == GRID_POINT_EMPTY) {
            (*neighborGridPointPtr) = value;
            PQUEUE_PUSH(queuePtr, (void*)neighborGridPointPtr);
        } else if (neighborValue != GRID_POINT_FULL) {
            /* We have expanded here before... is this new path better? */
            if (value < neighborValue) {
                (*neighborGridPointPtr) = value;
                PQUEUE_PUSH(queuePtr, (void*)neighborGridPointPtr);
            }
        }
    }
}


/* =============================================================================
 * PdoExpansion
 * =============================================================================
 */
static TM_PURE
bool_t
PdoExpansion (router_t* routerPtr, grid_t* myGridPtr, queue_t* queuePtr,
              coordinate_t* srcPtr, coordinate_t* dstPtr)
{
    long xCost = routerPtr->xCost;
    long yCost = routerPtr->yCost;
    long zCost = routerPtr->zCost;

    /*
     * Potential Optimization: Make 'src' the one closest to edge.
     * This will likely decrease the area of the emitted wave.
     */

    PQUEUE_CLEAR(queuePtr);
    long* srcGridPointPtr =
        grid_getPointRef(myGridPtr, srcPtr->x, srcPtr->y, srcPtr->z);
    PQUEUE_PUSH(queuePtr, (void*)srcGridPointPtr);
    grid_setPoint(myGridPtr, srcPtr->x, srcPtr->y, srcPtr->z, 0);
    grid_setPoint(myGridPtr, dstPtr->x, dstPtr->y, dstPtr->z, GRID_POINT_EMPTY);
    long* dstGridPointPtr =
        grid_getPointRef(myGridPtr, dstPtr->x, dstPtr->y, dstPtr->z);
    bool_t isPathFound = FALSE;

    while (!PQUEUE_ISEMPTY(queuePtr)) {

        long* gridPointPtr = (long*)PQUEUE_POP(queuePtr);
        if (gridPointPtr == dstGridPointPtr) {
            isPathFound = TRUE;
            break;
        }

        long x;
        long y;
        long z;
        grid_getPointIndices(myGridPtr, gridPointPtr, &x, &y, &z);
        long value = (*gridPointPtr);

        /*
         * Check 6 neighbors
         *
         * Potential Optimization: Only need to check 5 of these
         */
        PexpandToNeighbor(myGridPtr, x+1, y,   z,   (value + xCost), queuePtr);
        PexpandToNeighbor(myGridPtr, x-1, y,   z,   (value + xCost), queuePtr);
        PexpandToNeighbor(myGridPtr, x,   y+1, z,   (value + yCost), queuePtr);
        PexpandToNeighbor(myGridPtr, x,   y-1, z,   (value + yCost), queuePtr);
        PexpandToNeighbor(myGridPtr, x,   y,   z+1, (value + zCost), queuePtr);
        PexpandToNeighbor(myGridPtr, x,   y,   z-1, (value + zCost), queuePtr);

    } /* iterate over work queue */

#ifdef DEBUG
    printf("Expansion (%li, %li, %li) -> (%li, %li, %li):\n",
           srcPtr->x, srcPtr->y, srcPtr->z,
           dstPtr->x, dstPtr->y, dstPtr->z);
    grid_print(myGridPtr);
#endif /*  DEBUG */

    return isPathFound;
}


/* =============================================================================
 * traceToNeighbor
 * =============================================================================
 */
static void
traceToNeighbor (grid_t* myGridPtr,
                 point_t* currPtr,
                 point_t* movePtr,
                 bool_t useMomentum,
                 long bendCost,
                 point_t* nextPtr)
{
    long x = currPtr->x + movePtr->x;
    long y = currPtr->y + movePtr->y;
    long z = currPtr->z + movePtr->z;

    if (grid_isPointValid(myGridPtr, x, y, z) &&
        !grid_isPointEmpty(myGridPtr, x, y, z) &&
        !grid_isPointFull(myGridPtr, x, y, z))
    {
        long value = grid_getPoint(myGridPtr, x, y, z);
        long b = 0;
        if (useMomentum && (currPtr->momentum != movePtr->momentum)) {
            b = bendCost;
        }
        if ((value + b) <= nextPtr->value) { /* '=' favors neighbors over current */
            nextPtr->x = x;
            nextPtr->y = y;
            nextPtr->z = z;
            nextPtr->value = value;
            nextPtr->momentum = movePtr->momentum;
        }
    }
}


/* =============================================================================
 * PdoTraceback
 * =============================================================================
 */
static TM_PURE
vector_t*
PdoTraceback (grid_t* gridPtr, grid_t* myGridPtr,
              coordinate_t* dstPtr, long bendCost)
{
    vector_t* pointVectorPtr = PVECTOR_ALLOC(1);
    assert(pointVectorPtr);

    point_t next;
    next.x = dstPtr->x;
    next.y = dstPtr->y;
    next.z = dstPtr->z;
    next.value = grid_getPoint(myGridPtr, next.x, next.y, next.z);
    next.momentum = MOMENTUM_ZERO;

    while (1) {

        long* gridPointPtr = grid_getPointRef(gridPtr, next.x, next.y, next.z);
        PVECTOR_PUSHBACK(pointVectorPtr, (void*)gridPointPtr);
        grid_setPoint(myGridPtr, next.x, next.y, next.z, GRID_POINT_FULL);

        /* Check if we are done */
        if (next.value == 0) {
            break;
        }
        point_t curr = next;

        /*
         * Check 6 neighbors
         *
         * Potential Optimization: Only need to check 5 of these
         */
        traceToNeighbor(myGridPtr, &curr, &MOVE_POSX, TRUE, bendCost, &next);
        traceToNeighbor(myGridPtr, &curr, &MOVE_POSY, TRUE, bendCost, &next);
        traceToNeighbor(myGridPtr, &curr, &MOVE_POSZ, TRUE, bendCost, &next);
        traceToNeighbor(myGridPtr, &curr, &MOVE_NEGX, TRUE, bendCost, &next);
        traceToNeighbor(myGridPtr, &curr, &MOVE_NEGY, TRUE, bendCost, &next);
        traceToNeighbor(myGridPtr, &curr, &MOVE_NEGZ, TRUE, bendCost, &next);

#ifdef DEBUG
        printf("(%li, %li, %li)\n", next.x, next.y, next.z);
#endif /* DEBUG */
        /*
         * Because of bend costs, none of the neighbors may appear to be closer.
         * In this case, pick a neighbor while ignoring momentum.
         */
        if ((curr.x == next.x) &&
            (curr.y == next.y) &&
            (curr.z == next.z))
        {
            next.value = curr.value;
            traceToNeighbor(myGridPtr, &curr, &MOVE_POSX, FALSE, bendCost, &next);
            traceToNeighbor(myGridPtr, &curr, &MOVE_POSY, FALSE, bendCost, &next);
            traceToNeighbor(myGridPtr, &curr, &MOVE_POSZ, FALSE, bendCost, &next);
            traceToNeighbor(myGridPtr, &curr, &MOVE_NEGX, FALSE, bendCost, &next);
            traceToNeighbor(myGridPtr, &curr, &MOVE_NEGY, FALSE, bendCost, &next);
            traceToNeighbor(myGridPtr, &curr, &MOVE_NEGZ, FALSE, bendCost, &next);

            if ((curr.x == next.x) &&
                (curr.y == next.y) &&
                (curr.z == next.z))
            {
                PVECTOR_FREE(pointVectorPtr);
#ifdef DEBUG
                puts("[dead]");
#endif
                return NULL; /* cannot find path */
            }
        }
    }

#ifdef DEBUG
    puts("");
#endif /* DEBUG */

    return pointVectorPtr;
}


/* =============================================================================
 * router_solve
 * =============================================================================
 */
void
router_solve (void* argPtr)
{
    TM_THREAD_ENTER();

    router_solve_arg_t* routerArgPtr = (router_solve_arg_t*)argPtr;
    router_t* routerPtr = routerArgPtr->routerPtr;
    maze_t* mazePtr = routerArgPtr->mazePtr;
    vector_t* myPathVectorPtr = PVECTOR_ALLOC(1);
    assert(myPathVectorPtr);

    queue_t* workQueuePtr = mazePtr->workQueuePtr;
    grid_t* gridPtr = mazePtr->gridPtr;
    grid_t* myGridPtr =
        PGRID_ALLOC(gridPtr->width, gridPtr->height, gridPtr->depth);
    assert(myGridPtr);
    long bendCost = routerPtr->bendCost;
//#if defined(TRANSMEM_MODIFICATION)
//    queue_t* myExpansionQueuePtr = TMQUEUE_ALLOC(-1);
//#else /* ! TRANSMEM_MODIFICATION */
    queue_t* myExpansionQueuePtr = PQUEUE_ALLOC(-1);
//#endif /* ! TRANSMEM_MODIFICATION */

    /*
     * Iterate over work list to route each path. This involves an
     * 'expansion' and 'traceback' phase for each source/destination pair.
     */
    while (1) {

        pair_t* coordinatePairPtr;
	#ifdef HW_SW_PATHS
		IF_HTM_MODE
			START_HTM_MODE
        if (HW_TMQUEUE_ISEMPTY(workQueuePtr)) {
            coordinatePairPtr = NULL;
        } else {
            coordinatePairPtr = (pair_t*)HW_TMQUEUE_POP(workQueuePtr);
        }
			COMMIT_HTM_MODE
		ELSE_STM_MODE
			START_STM_MODE(RW)
	#else /* !HW_SW_PATHS */
      TM_BEGIN();
	#endif /* !HW_SW_PATHS */
        if (TMQUEUE_ISEMPTY(workQueuePtr)) {
            coordinatePairPtr = NULL;
        } else {
            coordinatePairPtr = (pair_t*)TMQUEUE_POP(workQueuePtr);
        }
	#ifdef HW_SW_PATHS
			COMMIT_STM_MODE
	#else /* !HW_SW_PATHS */
      TM_END();
	#endif /* !HW_SW_PATHS */
        if (coordinatePairPtr == NULL) {
            break;
        }

        coordinate_t* srcPtr = (coordinate_t*)coordinatePairPtr->firstPtr;
        coordinate_t* dstPtr = (coordinate_t*)coordinatePairPtr->secondPtr;

        pair_free(coordinatePairPtr);

        bool_t success = FALSE;
        vector_t* pointVectorPtr = NULL;

#if defined(TRANSMEM_MODIFICATION)

        while (TRUE) {
          success = FALSE;
          // get a snapshot of the grid... may be inconsistent, but that's OK
          grid_copy(myGridPtr, gridPtr);
          /* ok if not most up-to-date */
          // see if there is a valid path we can use
          if (PdoExpansion(routerPtr, myGridPtr, myExpansionQueuePtr,
                         srcPtr, dstPtr)) {
            pointVectorPtr = PdoTraceback(gridPtr, myGridPtr, dstPtr, bendCost);

            if (pointVectorPtr) {
              // we've got a valid path.  Use a transaction to validate and finalize it
              bool_t validity = FALSE;

	#ifdef HW_SW_PATHS
		          IF_HTM_MODE
			          START_HTM_MODE
                  validity = grid_addpath(gridPtr, pointVectorPtr);
			          COMMIT_HTM_MODE
		          ELSE_STM_MODE
			          START_STM_MODE(RW)
	#else /* !HW_SW_PATHS */
                TM_BEGIN();
	#endif /* !HW_SW_PATHS */
                  validity = TMGRID_ADDPATH(gridPtr, pointVectorPtr);
	#ifdef HW_SW_PATHS
			          COMMIT_STM_MODE
	#else /* !HW_SW_PATHS */
                TM_END();
	#endif /* !HW_SW_PATHS */

              // if the operation was valid, we just finalized the path
              if (validity) {
                success = TRUE;
                break;
              } else {
                // otherwise we need to resample the grid
                PVECTOR_FREE(pointVectorPtr);
                continue;
              }
            } else {
              // if the traceback failed, we need to resample the grid
              continue;
            }
          } else {
            // if the traceback failed, then the current path is not possible,
            // so we should skip it
            break;
          }
        }

#else /* ! TRANSMEM_MODIFICATION */

	#ifdef HW_SW_PATHS
		IF_HTM_MODE
			START_HTM_MODE
        grid_copy(myGridPtr, gridPtr); /* ok if not most up-to-date */
        if (PdoExpansion(routerPtr, myGridPtr, myExpansionQueuePtr,
                         srcPtr, dstPtr)) {
            pointVectorPtr = PdoTraceback(gridPtr, myGridPtr, dstPtr, bendCost);
            /*
             * TODO: fix memory leak
             *
             * pointVectorPtr will be a memory leak if we abort this transaction
             */
            if (pointVectorPtr) {
                HW_TMGRID_ADDPATH(gridPtr, pointVectorPtr);
                HW_TM_LOCAL_WRITE(success, TRUE);
            }
        }
			COMMIT_HTM_MODE
		ELSE_STM_MODE
			START_STM_MODE(RW)
	#else /* !HW_SW_PATHS */
      TM_BEGIN();
	#endif /* !HW_SW_PATHS */
        grid_copy(myGridPtr, gridPtr); /* ok if not most up-to-date */
        if (PdoExpansion(routerPtr, myGridPtr, myExpansionQueuePtr,
                         srcPtr, dstPtr)) {
            pointVectorPtr = PdoTraceback(gridPtr, myGridPtr, dstPtr, bendCost);
            /*
             * TODO: fix memory leak
             *
             * pointVectorPtr will be a memory leak if we abort this transaction
             */
            if (pointVectorPtr) {
                TMGRID_ADDPATH(gridPtr, pointVectorPtr);
                TM_LOCAL_WRITE(success, TRUE);
            }
        }
	#ifdef HW_SW_PATHS
			COMMIT_STM_MODE
	#else /* !HW_SW_PATHS */
      TM_END();
	#endif /* !HW_SW_PATHS */

#endif /* ! TRANSMEM_MODIFICATION */

        if (success) {
            bool_t status = PVECTOR_PUSHBACK(myPathVectorPtr,
                                             (void*)pointVectorPtr);
            assert(status);
        }

    }

    /*
     * Add my paths to global list
     */
    list_t* pathVectorListPtr = routerArgPtr->pathVectorListPtr;
#ifdef HW_SW_PATHS
	IF_HTM_MODE
		START_HTM_MODE
   		HW_TMLIST_INSERT(pathVectorListPtr, (void*)myPathVectorPtr);
		COMMIT_HTM_MODE
	ELSE_STM_MODE
		START_STM_MODE(RW)
#else /* !HW_SW_PATHS */
    TM_BEGIN();
#endif /* !HW_SW_PATHS */
   		TMLIST_INSERT(pathVectorListPtr, (void*)myPathVectorPtr);
#ifdef HW_SW_PATHS
		COMMIT_STM_MODE
#else /* !HW_SW_PATHS */
    TM_END();
#endif /* !HW_SW_PATHS */

#if defined(TRANSMEM_MODIFICATION)
    grid_free(myGridPtr);
    TMQUEUE_FREE(myExpansionQueuePtr);
#else /* ! TRANSMEM_MODIFICATION */
    PGRID_FREE(myGridPtr);
    PQUEUE_FREE(myExpansionQueuePtr);
#endif /* ! TRANSMEM_MODIFICATION */

#ifdef DEBUG
    puts("\nFinal Grid:");
    grid_print(gridPtr);
#endif /* DEBUG */

    TM_THREAD_EXIT();
}


/* =============================================================================
 *
 * End of router.c
 *
 * =============================================================================
 */
