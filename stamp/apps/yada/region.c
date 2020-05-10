/* =============================================================================
 *
 * region.c
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


#include "tm.h"
#include <assert.h>
#include <stdlib.h>
#include "region.h"
#include "coordinate.h"
#include "element.h"
#include "list.h"
#include "map.h"
#include "queue.h"
#include "mesh.h"


struct region {
    coordinate_t centerCoordinate;
    queue_t* expandQueuePtr;
    list_t* beforeListPtr; /* before retriangulation; list to avoid duplicates */
    list_t* borderListPtr; /* edges adjacent to region; list to avoid duplicates */
    vector_t* badVectorPtr;
};

/* =============================================================================
 * DECLARATION OF TM_SAFE FUNCTIONS
 * =============================================================================
 */

#ifdef HW_SW_PATHS
static
void
HW_TMaddToBadVector (vector_t* badVectorPtr, element_t* badElementPtr);
#endif /* HW_SW_PATHS */

static TM_SAFE
void
TMaddToBadVector (TM_ARGDECL  vector_t* badVectorPtr, element_t* badElementPtr);

#ifdef HW_SW_PATHS
static
long
HW_TMretriangulate (element_t* elementPtr,
                 region_t* regionPtr,
                 mesh_t* meshPtr,
                 MAP_T* edgeMapPtr);
#endif /* HW_SW_PATHS */

static TM_SAFE
long
TMretriangulate (TM_ARGDECL
                 element_t* elementPtr,
                 region_t* regionPtr,
                 mesh_t* meshPtr,
                 MAP_T* edgeMapPtr);

#ifdef HW_SW_PATHS
static
element_t*
HW_TMgrowRegion (element_t* centerElementPtr,
              region_t* regionPtr,
              mesh_t* meshPtr,
              MAP_T* edgeMapPtr);
#endif /* HW_SW_PATHS */

static TM_SAFE
element_t*
TMgrowRegion (TM_ARGDECL
              element_t* centerElementPtr,
              region_t* regionPtr,
              mesh_t* meshPtr,
              MAP_T* edgeMapPtr);

/* =============================================================================
 * Pregion_alloc
 * =============================================================================
 */
region_t*
Pregion_alloc ()
{
    region_t* regionPtr;

    regionPtr = (region_t*)P_MALLOC(sizeof(region_t));
    if (regionPtr) {
        regionPtr->expandQueuePtr = PQUEUE_ALLOC(-1);
        assert(regionPtr->expandQueuePtr);
        regionPtr->beforeListPtr = PLIST_ALLOC(&element_listCompare);
        assert(regionPtr->beforeListPtr);
        regionPtr->borderListPtr = PLIST_ALLOC(&element_listCompareEdge);
        assert(regionPtr->borderListPtr);
        regionPtr->badVectorPtr = PVECTOR_ALLOC(1);
        assert(regionPtr->badVectorPtr);
    }

    return regionPtr;
}


/* =============================================================================
 * Pregion_free
 * =============================================================================
 */
void
Pregion_free (region_t* regionPtr)
{
    PVECTOR_FREE(regionPtr->badVectorPtr);
    PLIST_FREE(regionPtr->borderListPtr);
    PLIST_FREE(regionPtr->beforeListPtr);
    PQUEUE_FREE(regionPtr->expandQueuePtr);
    P_FREE(regionPtr);
}

#ifdef HW_SW_PATHS
/* =============================================================================
 * HW_TMaddToBadVector
 * =============================================================================
 */
static
void
HW_TMaddToBadVector (vector_t* badVectorPtr, element_t* badElementPtr)
{
    bool_t status = PVECTOR_PUSHBACK(badVectorPtr, (void*)badElementPtr);
    assert(status);
    HW_TMELEMENT_SETISREFERENCED(badElementPtr, TRUE);
}
#endif /* HW_SW_PATHS */

/* =============================================================================
 * TMaddToBadVector
 * =============================================================================
 */
static TM_SAFE
void
TMaddToBadVector (TM_ARGDECL  vector_t* badVectorPtr, element_t* badElementPtr)
{
    bool_t status = PVECTOR_PUSHBACK(badVectorPtr, (void*)badElementPtr);
    assert(status);
    TMELEMENT_SETISREFERENCED(badElementPtr, TRUE);
}

static void
addToBadVector (vector_t* badVectorPtr, element_t* badElementPtr)
{
    bool_t status = PVECTOR_PUSHBACK(badVectorPtr, (void*)badElementPtr);
    assert(status);
    element_setIsReferenced(badElementPtr, TRUE);
}

#ifdef HW_SW_PATHS
/* =============================================================================
 * HW_TMretriangulate
 * -- Returns net amount of elements added to mesh
 * =============================================================================
 */
static
long
HW_TMretriangulate (element_t* elementPtr,
                 region_t* regionPtr,
                 mesh_t* meshPtr,
                 MAP_T* edgeMapPtr)
{
    vector_t* badVectorPtr = regionPtr->badVectorPtr; /* private */
    list_t* beforeListPtr = regionPtr->beforeListPtr; /* private */
    list_t* borderListPtr = regionPtr->borderListPtr; /* private */
    list_iter_t it;
    long numDelta = 0L;

    assert(edgeMapPtr);

    coordinate_t centerCoordinate = element_getNewPoint(elementPtr);

    /*
     * Remove the old triangles
     */

    list_iter_reset(&it, beforeListPtr);
    while (list_iter_hasNext(&it, beforeListPtr)) {
        element_t* beforeElementPtr =
            (element_t*)list_iter_next(&it, beforeListPtr);
        HW_TMMESH_REMOVE(meshPtr, beforeElementPtr);
    }

    numDelta -= PLIST_GETSIZE(beforeListPtr);

    /*
     * If segment is encroached, split it in half
     */

    if (element_getNumEdge(elementPtr) == 1) {

        coordinate_t coordinates[2];

        edge_t* edgePtr = element_getEdge(elementPtr, 0);
        coordinates[0] = centerCoordinate;

        coordinates[1] = *(coordinate_t*)(edgePtr->firstPtr);
        element_t* aElementPtr = HW_TMELEMENT_ALLOC(coordinates, 2);
        assert(aElementPtr);
        HW_TMMESH_INSERT(meshPtr, aElementPtr, edgeMapPtr);

        coordinates[1] = *(coordinate_t*)(edgePtr->secondPtr);
        element_t* bElementPtr = HW_TMELEMENT_ALLOC(coordinates, 2);
        assert(bElementPtr);
        HW_TMMESH_INSERT(meshPtr, bElementPtr, edgeMapPtr);

        bool_t status;
        status = HW_TMMESH_REMOVEBOUNDARY(meshPtr, element_getEdge(elementPtr, 0));
        assert(status);
        status = HW_TMMESH_INSERTBOUNDARY(meshPtr, element_getEdge(aElementPtr, 0));
        assert(status);
        status = HW_TMMESH_INSERTBOUNDARY(meshPtr, element_getEdge(bElementPtr, 0));
        assert(status);

        numDelta += 2;
    }

    /*
     * Insert the new triangles. These are contructed using the new
     * point and the two points from the border segment.
     */

    list_iter_reset(&it, borderListPtr);
    while (list_iter_hasNext(&it, borderListPtr)) {
        element_t* afterElementPtr;
        coordinate_t coordinates[3];
        edge_t* borderEdgePtr = (edge_t*)list_iter_next(&it, borderListPtr);
        assert(borderEdgePtr);
        coordinates[0] = centerCoordinate;
        coordinates[1] = *(coordinate_t*)(borderEdgePtr->firstPtr);
        coordinates[2] = *(coordinate_t*)(borderEdgePtr->secondPtr);
        afterElementPtr = HW_TMELEMENT_ALLOC(coordinates, 3);
        assert(afterElementPtr);
        HW_TMMESH_INSERT(meshPtr, afterElementPtr, edgeMapPtr);
        if (element_isBad(afterElementPtr)) {
            HW_TMaddToBadVector(badVectorPtr, afterElementPtr);
        }
    }

    numDelta += PLIST_GETSIZE(borderListPtr);

    return numDelta;
}
#endif /* HW_SW_PATHS */

/* =============================================================================
 * TMretriangulate
 * -- Returns net amount of elements added to mesh
 * =============================================================================
 */
static TM_SAFE
long
TMretriangulate (TM_ARGDECL
                 element_t* elementPtr,
                 region_t* regionPtr,
                 mesh_t* meshPtr,
                 MAP_T* edgeMapPtr)
{
    vector_t* badVectorPtr = regionPtr->badVectorPtr; /* private */
    list_t* beforeListPtr = regionPtr->beforeListPtr; /* private */
    list_t* borderListPtr = regionPtr->borderListPtr; /* private */
    list_iter_t it;
    long numDelta = 0L;

    assert(edgeMapPtr);

    coordinate_t centerCoordinate = element_getNewPoint(elementPtr);

    /*
     * Remove the old triangles
     */

    list_iter_reset(&it, beforeListPtr);
    while (list_iter_hasNext(&it, beforeListPtr)) {
        element_t* beforeElementPtr =
            (element_t*)list_iter_next(&it, beforeListPtr);
        TMMESH_REMOVE(meshPtr, beforeElementPtr);
    }

    numDelta -= PLIST_GETSIZE(beforeListPtr);

    /*
     * If segment is encroached, split it in half
     */

    if (element_getNumEdge(elementPtr) == 1) {

        coordinate_t coordinates[2];

        edge_t* edgePtr = element_getEdge(elementPtr, 0);
        coordinates[0] = centerCoordinate;

        coordinates[1] = *(coordinate_t*)(edgePtr->firstPtr);
        element_t* aElementPtr = TMELEMENT_ALLOC(coordinates, 2);
        assert(aElementPtr);
        TMMESH_INSERT(meshPtr, aElementPtr, edgeMapPtr);

        coordinates[1] = *(coordinate_t*)(edgePtr->secondPtr);
        element_t* bElementPtr = TMELEMENT_ALLOC(coordinates, 2);
        assert(bElementPtr);
        TMMESH_INSERT(meshPtr, bElementPtr, edgeMapPtr);

        bool_t status;
        status = TMMESH_REMOVEBOUNDARY(meshPtr, element_getEdge(elementPtr, 0));
        assert(status);
        status = TMMESH_INSERTBOUNDARY(meshPtr, element_getEdge(aElementPtr, 0));
        assert(status);
        status = TMMESH_INSERTBOUNDARY(meshPtr, element_getEdge(bElementPtr, 0));
        assert(status);

        numDelta += 2;
    }

    /*
     * Insert the new triangles. These are contructed using the new
     * point and the two points from the border segment.
     */

    list_iter_reset(&it, borderListPtr);
    while (list_iter_hasNext(&it, borderListPtr)) {
        element_t* afterElementPtr;
        coordinate_t coordinates[3];
        edge_t* borderEdgePtr = (edge_t*)list_iter_next(&it, borderListPtr);
        assert(borderEdgePtr);
        coordinates[0] = centerCoordinate;
        coordinates[1] = *(coordinate_t*)(borderEdgePtr->firstPtr);
        coordinates[2] = *(coordinate_t*)(borderEdgePtr->secondPtr);
        afterElementPtr = TMELEMENT_ALLOC(coordinates, 3);
        assert(afterElementPtr);
        TMMESH_INSERT(meshPtr, afterElementPtr, edgeMapPtr);
        if (element_isBad(afterElementPtr)) {
            TMaddToBadVector(TM_ARG  badVectorPtr, afterElementPtr);
        }
    }

    numDelta += PLIST_GETSIZE(borderListPtr);

    return numDelta;
}

static long
retriangulate (element_t* elementPtr,
               region_t* regionPtr,
               mesh_t* meshPtr,
               MAP_T* edgeMapPtr)
{
    vector_t* badVectorPtr = regionPtr->badVectorPtr; /* private */
    list_t* beforeListPtr = regionPtr->beforeListPtr; /* private */
    list_t* borderListPtr = regionPtr->borderListPtr; /* private */
    list_iter_t it;
    long numDelta = 0L;

    assert(edgeMapPtr);

    coordinate_t centerCoordinate = element_getNewPoint(elementPtr);

    /*
     * Remove the old triangles
     */

    list_iter_reset(&it, beforeListPtr);
    while (list_iter_hasNext(&it, beforeListPtr)) {
        element_t* beforeElementPtr =
            (element_t*)list_iter_next(&it, beforeListPtr);
        mesh_remove(meshPtr, beforeElementPtr);
    }

    numDelta -= PLIST_GETSIZE(beforeListPtr);

    /*
     * If segment is encroached, split it in half
     */

    if (element_getNumEdge(elementPtr) == 1) {

        coordinate_t coordinates[2];

        edge_t* edgePtr = element_getEdge(elementPtr, 0);
        coordinates[0] = centerCoordinate;

        coordinates[1] = *(coordinate_t*)(edgePtr->firstPtr);
				element_t* aElementPtr = element_alloc(coordinates, 2);
        assert(aElementPtr);
        mesh_insert(meshPtr, aElementPtr, edgeMapPtr);

        coordinates[1] = *(coordinate_t*)(edgePtr->secondPtr);
				element_t* bElementPtr = element_alloc(coordinates, 2);
        assert(bElementPtr);
        mesh_insert(meshPtr, bElementPtr, edgeMapPtr);

        bool_t status;
        status = mesh_removeBoundary(meshPtr, element_getEdge(elementPtr, 0));
        assert(status);
        status = mesh_insertBoundary(meshPtr, element_getEdge(aElementPtr, 0));
        assert(status);
        status = mesh_insertBoundary(meshPtr, element_getEdge(bElementPtr, 0));
        assert(status);

        numDelta += 2;
    }

    /*
     * Insert the new triangles. These are contructed using the new
     * point and the two points from the border segment.
     */

    list_iter_reset(&it, borderListPtr);
    while (list_iter_hasNext(&it, borderListPtr)) {
        element_t* afterElementPtr;
        coordinate_t coordinates[3];
        edge_t* borderEdgePtr = (edge_t*)list_iter_next(&it, borderListPtr);
        assert(borderEdgePtr);
        coordinates[0] = centerCoordinate;
        coordinates[1] = *(coordinate_t*)(borderEdgePtr->firstPtr);
        coordinates[2] = *(coordinate_t*)(borderEdgePtr->secondPtr);
        afterElementPtr = element_alloc(coordinates, 3);
        assert(afterElementPtr);
        mesh_insert(meshPtr, afterElementPtr, edgeMapPtr);
        if (element_isBad(afterElementPtr)) {
            addToBadVector(badVectorPtr, afterElementPtr);
        }
    }

    numDelta += PLIST_GETSIZE(borderListPtr);

    return numDelta;
}

#ifdef HW_SW_PATHS
/* =============================================================================
 * HW_TMgrowRegion
 * -- Return NULL if success, else pointer to encroached boundary
 * =============================================================================
 */
static
element_t*
HW_TMgrowRegion (element_t* centerElementPtr,
              region_t* regionPtr,
              mesh_t* meshPtr,
              MAP_T* edgeMapPtr)
{
    bool_t isBoundary = FALSE;

    if (element_getNumEdge(centerElementPtr) == 1) {
        isBoundary = TRUE;
    }

    list_t* beforeListPtr = regionPtr->beforeListPtr;
    list_t* borderListPtr = regionPtr->borderListPtr;
    queue_t* expandQueuePtr = regionPtr->expandQueuePtr;

    PLIST_CLEAR(beforeListPtr);
    PLIST_CLEAR(borderListPtr);
    PQUEUE_CLEAR(expandQueuePtr);

    coordinate_t centerCoordinate = element_getNewPoint(centerElementPtr);
    coordinate_t* centerCoordinatePtr = &centerCoordinate;

    PQUEUE_PUSH(expandQueuePtr, (void*)centerElementPtr);
    while (!PQUEUE_ISEMPTY(expandQueuePtr)) {

        element_t* currentElementPtr = (element_t*)PQUEUE_POP(expandQueuePtr);

        PLIST_INSERT(beforeListPtr, (void*)currentElementPtr); /* no duplicates */
        list_t* neighborListPtr = element_getNeighborListPtr(currentElementPtr);

        list_iter_t it;
        HW_TMLIST_ITER_RESET(&it, neighborListPtr);
        while (HW_TMLIST_ITER_HASNEXT(&it, neighborListPtr)) {
            element_t* neighborElementPtr =
                (element_t*)HW_TMLIST_ITER_NEXT(&it, neighborListPtr);
            HW_TMELEMENT_ISGARBAGE(neighborElementPtr); /* so we can detect conflicts */
            if (!list_find(beforeListPtr, (void*)neighborElementPtr)) {
                if (element_isInCircumCircle(neighborElementPtr, centerCoordinatePtr)) {
                    /* This is part of the region */
                    if (!isBoundary && (element_getNumEdge(neighborElementPtr) == 1)) {
                        /* Encroached on mesh boundary so split it and restart */
                        return neighborElementPtr;
                    } else {
                        /* Continue breadth-first search */
                        bool_t isSuccess;
                        isSuccess = PQUEUE_PUSH(expandQueuePtr,
                                                (void*)neighborElementPtr);
                        assert(isSuccess);
                    }
                } else {
                    /* This element borders region; save info for retriangulation */
                    edge_t* borderEdgePtr =
                        element_getCommonEdge(neighborElementPtr, currentElementPtr);
                    if (!borderEdgePtr) {
                        HW_TM_RESTART();
                    }
                    PLIST_INSERT(borderListPtr,
                                 (void*)borderEdgePtr); /* no duplicates */
                    if (!MAP_CONTAINS(edgeMapPtr, borderEdgePtr)) {
                        PMAP_INSERT(edgeMapPtr, borderEdgePtr, neighborElementPtr);
                    }
                }
            } /* not visited before */
        } /* for each neighbor */

    } /* breadth-first search */

    return NULL;
}
#endif /* HW_SW_PATHS */

/* =============================================================================
 * TMgrowRegion
 * -- Return NULL if success, else pointer to encroached boundary
 * =============================================================================
 */
static TM_SAFE
element_t*
TMgrowRegion (TM_ARGDECL
              element_t* centerElementPtr,
              region_t* regionPtr,
              mesh_t* meshPtr,
              MAP_T* edgeMapPtr)
{
    bool_t isBoundary = FALSE;

    if (element_getNumEdge(centerElementPtr) == 1) {
        isBoundary = TRUE;
    }

    list_t* beforeListPtr = regionPtr->beforeListPtr;
    list_t* borderListPtr = regionPtr->borderListPtr;
    queue_t* expandQueuePtr = regionPtr->expandQueuePtr;

    PLIST_CLEAR(beforeListPtr);
    PLIST_CLEAR(borderListPtr);
    PQUEUE_CLEAR(expandQueuePtr);

    coordinate_t centerCoordinate = element_getNewPoint(centerElementPtr);
    coordinate_t* centerCoordinatePtr = &centerCoordinate;

    PQUEUE_PUSH(expandQueuePtr, (void*)centerElementPtr);
    while (!PQUEUE_ISEMPTY(expandQueuePtr)) {

        element_t* currentElementPtr = (element_t*)PQUEUE_POP(expandQueuePtr);

        PLIST_INSERT(beforeListPtr, (void*)currentElementPtr); /* no duplicates */
        list_t* neighborListPtr = element_getNeighborListPtr(currentElementPtr);

        list_iter_t it;
        TMLIST_ITER_RESET(&it, neighborListPtr);
        while (TMLIST_ITER_HASNEXT(&it, neighborListPtr)) {
            element_t* neighborElementPtr =
                (element_t*)TMLIST_ITER_NEXT(&it, neighborListPtr);
            TMELEMENT_ISGARBAGE(neighborElementPtr); /* so we can detect conflicts */
            if (!list_find(beforeListPtr, (void*)neighborElementPtr)) {
                if (element_isInCircumCircle(neighborElementPtr, centerCoordinatePtr)) {
                    /* This is part of the region */
                    if (!isBoundary && (element_getNumEdge(neighborElementPtr) == 1)) {
                        /* Encroached on mesh boundary so split it and restart */
                        return neighborElementPtr;
                    } else {
                        /* Continue breadth-first search */
                        bool_t isSuccess;
                        isSuccess = PQUEUE_PUSH(expandQueuePtr,
                                                (void*)neighborElementPtr);
                        assert(isSuccess);
                    }
                } else {
                    /* This element borders region; save info for retriangulation */
                    edge_t* borderEdgePtr =
                        element_getCommonEdge(neighborElementPtr, currentElementPtr);
                    if (!borderEdgePtr) {
                        TM_RESTART();
                    }
                    PLIST_INSERT(borderListPtr,
                                 (void*)borderEdgePtr); /* no duplicates */
                    if (!MAP_CONTAINS(edgeMapPtr, borderEdgePtr)) {
                        PMAP_INSERT(edgeMapPtr, borderEdgePtr, neighborElementPtr);
                    }
                }
            } /* not visited before */
        } /* for each neighbor */

    } /* breadth-first search */

    return NULL;
}

static element_t*
growRegion (element_t* centerElementPtr,
              region_t* regionPtr,
              mesh_t* meshPtr,
              MAP_T* edgeMapPtr)
{
    bool_t isBoundary = FALSE;

    if (element_getNumEdge(centerElementPtr) == 1) {
        isBoundary = TRUE;
    }

    list_t* beforeListPtr = regionPtr->beforeListPtr;
    list_t* borderListPtr = regionPtr->borderListPtr;
    queue_t* expandQueuePtr = regionPtr->expandQueuePtr;

    PLIST_CLEAR(beforeListPtr);
    PLIST_CLEAR(borderListPtr);
    PQUEUE_CLEAR(expandQueuePtr);

    coordinate_t centerCoordinate = element_getNewPoint(centerElementPtr);
    coordinate_t* centerCoordinatePtr = &centerCoordinate;

    PQUEUE_PUSH(expandQueuePtr, (void*)centerElementPtr);
    while (!PQUEUE_ISEMPTY(expandQueuePtr)) {

        element_t* currentElementPtr = (element_t*)PQUEUE_POP(expandQueuePtr);

        PLIST_INSERT(beforeListPtr, (void*)currentElementPtr); /* no duplicates */
        list_t* neighborListPtr = element_getNeighborListPtr(currentElementPtr);

        list_iter_t it;
        list_iter_reset(&it, neighborListPtr);
        while (list_iter_hasNext(&it, neighborListPtr)) {
            element_t* neighborElementPtr =
                (element_t*)list_iter_next(&it, neighborListPtr);
            element_isGarbage(neighborElementPtr); /* so we can detect conflicts */
            if (!list_find(beforeListPtr, (void*)neighborElementPtr)) {
                if (element_isInCircumCircle(neighborElementPtr, centerCoordinatePtr)) {
                    /* This is part of the region */
                    if (!isBoundary && (element_getNumEdge(neighborElementPtr) == 1)) {
                        /* Encroached on mesh boundary so split it and restart */
                        return neighborElementPtr;
                    } else {
                        /* Continue breadth-first search */
                        bool_t isSuccess;
                        isSuccess = PQUEUE_PUSH(expandQueuePtr,
                                                (void*)neighborElementPtr);
                        assert(isSuccess);
                    }
                } else {
                    /* This element borders region; save info for retriangulation */
                    edge_t* borderEdgePtr =
                        element_getCommonEdge(neighborElementPtr, currentElementPtr);
                    if (!borderEdgePtr) {
                        TM_RESTART();
                    }
                    PLIST_INSERT(borderListPtr,
                                 (void*)borderEdgePtr); /* no duplicates */
                    if (!MAP_CONTAINS(edgeMapPtr, borderEdgePtr)) {
                        PMAP_INSERT(edgeMapPtr, borderEdgePtr, neighborElementPtr);
                    }
                }
            } /* not visited before */
        } /* for each neighbor */

    } /* breadth-first search */

    return NULL;
}

#ifdef HW_SW_PATHS
/* =============================================================================
 * HW_TMregion_refine
 * -- Returns net number of elements added to mesh
 * =============================================================================
 */
long
HW_TMregion_refine (
                 region_t* regionPtr, element_t* elementPtr, mesh_t* meshPtr)
{

    long numDelta = 0L;
    MAP_T* edgeMapPtr = NULL;
    element_t* encroachElementPtr = NULL;

    HW_TMELEMENT_ISGARBAGE(elementPtr); /* so we can detect conflicts */

    while (1) {
        edgeMapPtr = PMAP_ALLOC(NULL, &element_mapCompareEdge);
        assert(edgeMapPtr);
        encroachElementPtr = HW_TMgrowRegion(elementPtr,
                                          regionPtr,
                                          meshPtr,
                                          edgeMapPtr);

        if (encroachElementPtr) {
            HW_TMELEMENT_SETISREFERENCED(encroachElementPtr, TRUE);
            numDelta += HW_TMregion_refine(regionPtr,
                                        encroachElementPtr,
                                        meshPtr);
            if (HW_TMELEMENT_ISGARBAGE(elementPtr)) {
                break;
            }
        } else {
            break;
        }
        PMAP_FREE(edgeMapPtr);
    }

    /*
     * Perform retriangulation.
     */

    if (!HW_TMELEMENT_ISGARBAGE(elementPtr)) {
        numDelta += HW_TMretriangulate(elementPtr,
                                    regionPtr,
                                    meshPtr,
                                    edgeMapPtr);
    }

    PMAP_FREE(edgeMapPtr); /* no need to free elements */

    return numDelta;
}
#endif /* HW_SW_PATHS */

/* =============================================================================
 * TMregion_refine
 * -- Returns net number of elements added to mesh
 * =============================================================================
 */
TM_SAFE
long
TMregion_refine (TM_ARGDECL
                 region_t* regionPtr, element_t* elementPtr, mesh_t* meshPtr)
{

    long numDelta = 0L;
    MAP_T* edgeMapPtr = NULL;
    element_t* encroachElementPtr = NULL;

    TMELEMENT_ISGARBAGE(elementPtr); /* so we can detect conflicts */

    while (1) {
        edgeMapPtr = PMAP_ALLOC(NULL, &element_mapCompareEdge);
        assert(edgeMapPtr);
        encroachElementPtr = TMgrowRegion(TM_ARG
                                          elementPtr,
                                          regionPtr,
                                          meshPtr,
                                          edgeMapPtr);

        if (encroachElementPtr) {
            TMELEMENT_SETISREFERENCED(encroachElementPtr, TRUE);
            numDelta += TMregion_refine(TM_ARG
                                        regionPtr,
                                        encroachElementPtr,
                                        meshPtr);
            if (TMELEMENT_ISGARBAGE(elementPtr)) {
                break;
            }
        } else {
            break;
        }
        PMAP_FREE(edgeMapPtr);
    }

    /*
     * Perform retriangulation.
     */

    if (!TMELEMENT_ISGARBAGE(elementPtr)) {
        numDelta += TMretriangulate(TM_ARG
                                    elementPtr,
                                    regionPtr,
                                    meshPtr,
                                    edgeMapPtr);
    }

    PMAP_FREE(edgeMapPtr); /* no need to free elements */

    return numDelta;
}

long
region_refine (region_t* regionPtr, element_t* elementPtr, mesh_t* meshPtr)
{

    long numDelta = 0L;
    MAP_T* edgeMapPtr = NULL;
    element_t* encroachElementPtr = NULL;

    element_isGarbage(elementPtr); /* so we can detect conflicts */

    while (1) {
        edgeMapPtr = PMAP_ALLOC(NULL, &element_mapCompareEdge);
        assert(edgeMapPtr);
        encroachElementPtr = growRegion(elementPtr,
                                          regionPtr,
                                          meshPtr,
                                          edgeMapPtr);

        if (encroachElementPtr) {
            element_setIsReferenced(encroachElementPtr, TRUE);
            numDelta += region_refine(regionPtr,
                                        encroachElementPtr,
                                        meshPtr);
            if (element_isGarbage(elementPtr)) {
                break;
            }
        } else {
            break;
        }
        PMAP_FREE(edgeMapPtr);
    }

    /*
     * Perform retriangulation.
     */

    if (!element_isGarbage(elementPtr)) {
        numDelta += retriangulate(elementPtr,
                                    regionPtr,
                                    meshPtr,
                                    edgeMapPtr);
    }

    PMAP_FREE(edgeMapPtr); /* no need to free elements */

    return numDelta;
}

/* =============================================================================
 * Pregion_clearBad
 * =============================================================================
 */
TM_PURE
void
Pregion_clearBad (region_t* regionPtr)
{
    PVECTOR_CLEAR(regionPtr->badVectorPtr);
}

#ifdef HW_SW_PATHS
/* =============================================================================
 * HW_TMregion_transferBad
 * =============================================================================
 */
void
HW_TMregion_transferBad (region_t* regionPtr, heap_t* workHeapPtr)
{
    vector_t* badVectorPtr = regionPtr->badVectorPtr;
    long numBad = PVECTOR_GETSIZE(badVectorPtr);
    long i;

    for (i = 0; i < numBad; i++) {
        element_t* badElementPtr = (element_t*)vector_at(badVectorPtr, i);
        if (HW_TMELEMENT_ISGARBAGE(badElementPtr)) {
            HW_TMELEMENT_FREE(badElementPtr);
        } else {
            bool_t status = HW_TMHEAP_INSERT(workHeapPtr, (void*)badElementPtr);
            assert(status);
        }
    }
}
#endif /* HW_SW_PATHS */

/* =============================================================================
 * TMregion_transferBad
 * =============================================================================
 */
TM_SAFE
void
TMregion_transferBad (TM_ARGDECL  region_t* regionPtr, heap_t* workHeapPtr)
{
    vector_t* badVectorPtr = regionPtr->badVectorPtr;
    long numBad = PVECTOR_GETSIZE(badVectorPtr);
    long i;

    for (i = 0; i < numBad; i++) {
        element_t* badElementPtr = (element_t*)vector_at(badVectorPtr, i);
        if (TMELEMENT_ISGARBAGE(badElementPtr)) {
            TMELEMENT_FREE(badElementPtr);
        } else {
            bool_t status = TMHEAP_INSERT(workHeapPtr, (void*)badElementPtr);
            assert(status);
        }
    }
}

void
region_transferBad (region_t* regionPtr, heap_t* workHeapPtr)
{
    vector_t* badVectorPtr = regionPtr->badVectorPtr;
    long numBad = PVECTOR_GETSIZE(badVectorPtr);
    long i;

    for (i = 0; i < numBad; i++) {
        element_t* badElementPtr = (element_t*)vector_at(badVectorPtr, i);
				// We add all the bad triangles to workHeap
				// The garbage ones will be freed after tx1
        /*if (element_isGarbage(badElementPtr)) {
            element_free(badElementPtr);
        } else {*/
            bool_t status = heap_insert(workHeapPtr, (void*)badElementPtr);
            assert(status);
        //}
    }
}

/* =============================================================================
 *
 * End of region.c
 *
 * =============================================================================
 */
