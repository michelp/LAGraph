/*
    LAGraph:  graph algorithms based on GraphBLAS

    Copyright 2019 LAGraph Contributors.

    (see Contributors.txt for a full list of Contributors; see
    ContributionInstructions.txt for information on how you can Contribute to
    this project).

    All Rights Reserved.

    NO WARRANTY. THIS MATERIAL IS FURNISHED ON AN "AS-IS" BASIS. THE LAGRAPH
    CONTRIBUTORS MAKE NO WARRANTIES OF ANY KIND, EITHER EXPRESSED OR IMPLIED,
    AS TO ANY MATTER INCLUDING, BUT NOT LIMITED TO, WARRANTY OF FITNESS FOR
    PURPOSE OR MERCHANTABILITY, EXCLUSIVITY, OR RESULTS OBTAINED FROM USE OF
    THE MATERIAL. THE CONTRIBUTORS DO NOT MAKE ANY WARRANTY OF ANY KIND WITH
    RESPECT TO FREEDOM FROM PATENT, TRADEMARK, OR COPYRIGHT INFRINGEMENT.

    Released under a BSD license, please see the LICENSE file distributed with
    this Software or contact permission@sei.cmu.edu for full terms.

    Created, in part, with funding and support from the United States
    Government.  (see Acknowledgments.txt file).

    This program includes and/or can make use of certain third party source
    code, object code, documentation and other files ("Third Party Software").
    See LICENSE file for more details.

*/

/**
 * Code is based on the algorithm described in the following paper
 * Zhang, Azad, Hu. FastSV: FastSV: A Distributed-Memory Connected Component
 * Algorithm with Fast Convergence (SIAM PP20)
 *
 * Modified by Tim Davis, Texas A&M University
 **/

// The input matrix A must be symmetric.  Self-edges (diagonal entries) are
// OK, and are ignored.  The values and type of A are ignored; just its
// pattern is accessed.

#define LAGRAPH_EXPERIMENTAL_ASK_BEFORE_BENCHMARKING
#include "LAGraph.h"

//------------------------------------------------------------------------------
// atomic_min_uint64: compute (*p) = min (*p, value), via atomic update
//------------------------------------------------------------------------------

static inline void atomic_min_uint64
(
    uint64_t *p,        // input/output
    uint64_t value      // input
)
{
    uint64_t old, new ;
    do
    {
        // get the old value at (*p)
        // #pragma omp atomic read
        old = (*p) ;
        // compute the new minimum
        new = LAGRAPH_MIN (old, value) ;
    }
    while (!__sync_bool_compare_and_swap (p, old, new)) ;
}

//------------------------------------------------------------------------------
// Reduce_assign:  w (index) += src
//------------------------------------------------------------------------------

// mask = NULL, accumulator = GrB_MIN_UINT64, descriptor = NULL
// Duplicates are summed with the accumulator, which differs from how
// GrB_assign works.

#define LAGRAPH_FREE_ALL

static GrB_Info Reduce_assign
(
#if 0
    GrB_Vector *w,          // vector of size n, all entries present
    GrB_Vector *src,        // vector of size n, all entries present
#else
    GrB_Vector *w_handle,   // vector of size n, all entries present
    GrB_Vector *s_handle,   // vector of size n, all entries present
#endif
    GrB_Index *index,       // array of size n
    GrB_Index n,
    GrB_Index *I,           // size n, containing [0, 1, 2, ..., n-1]
    GrB_Index *mem,
    int nthreads
)
{

#if 0

    GrB_Index nw, ns;
    LAGr_Vector_nvals(&nw, w);
    LAGr_Vector_nvals(&ns, src);
    GrB_Index *sval = mem, *wval = sval + nw;
    LAGr_Vector_extractTuples(NULL, wval, &nw, w);
    LAGr_Vector_extractTuples(NULL, sval, &ns, src);
    if (nthreads >= 4)
    {
        #pragma omp parallel for num_threads(nthreads) schedule(static)
        for (GrB_Index i = 0; i < n; i++)
        {
            atomic_min_uint64 (&(wval [index [i]]), sval [i]) ;
            // if (sval[i] < wval[index[i]])
            //     wval[index[i]] = sval[i];
        }
    }
    else
    {
        for (GrB_Index i = 0; i < n; i++)
        {
            if (sval[i] < wval[index[i]])
                wval[index[i]] = sval[i];
        }
    }
    LAGr_Vector_clear(w);
    LAGr_Vector_build(w, I, wval, nw, GrB_PLUS_UINT64);

#else

    GrB_Type w_type, s_type ;
    GrB_Index w_n, s_n, w_nvals, s_nvals, *w_i, *s_i ;
    uint64_t *w_x, *s_x ;

    #if GxB_IMPLEMENTATION >= GxB_VERSION (4,0,0)
    LAGr_Vector_export_Full (w_handle, &w_type, &w_n, (void **) &w_x, NULL) ;
    LAGr_Vector_export_Full (s_handle, &s_type, &s_n, (void **) &s_x, NULL) ;
    #else
    LAGr_Vector_export (w_handle, &w_type, &w_n, &w_nvals, &w_i,
        (void **) &w_x, NULL) ;
    LAGr_Vector_export (s_handle, &s_type, &s_n, &s_nvals, &s_i,
        (void **) &s_x, NULL) ;
    #endif

#if 0
    if (nthreads >= 4)
    {
        #pragma omp parallel for num_threads(nthreads) schedule(static)
        for (GrB_Index k = 0 ; k < n ; k++)
        {
            atomic_min_uint64 (&(w_x [index [k]]), s_x [k]) ;
        }
    }
    else
#endif
    {
        for (GrB_Index k = 0 ; k < n ; k++)
        {
            uint64_t i = index [k] ;
            w_x [i] = LAGRAPH_MIN (w_x [i], s_x [k]) ;
        }
    }

    #if GxB_IMPLEMENTATION >= GxB_VERSION (4,0,0)
    LAGr_Vector_import_Full (w_handle, w_type, w_n, (void **) &w_x, NULL) ;
    LAGr_Vector_import_Full (s_handle, s_type, s_n, (void **) &s_x, NULL) ;
    #else
    LAGr_Vector_import (w_handle, w_type, w_n, w_nvals, &w_i,
        (void **) &w_x, NULL) ;
    LAGr_Vector_import (s_handle, s_type, s_n, s_nvals, &s_i,
        (void **) &s_x, NULL) ;
    #endif

#endif

    return GrB_SUCCESS;
}

#undef  LAGRAPH_FREE_ALL
#define LAGRAPH_FREE_ALL            \
{                                   \
    LAGRAPH_FREE (I);               \
    LAGRAPH_FREE (V);               \
    LAGRAPH_FREE (mem);             \
    LAGr_free (&f) ;                \
    LAGr_free (&gp);                \
    LAGr_free (&mngp);              \
    LAGr_free (&gp_new);            \
    LAGr_free (&mod);               \
    if (sanitize) LAGr_free (&S);   \
}

//------------------------------------------------------------------------------
// LAGraph_cc_fastsv3
//------------------------------------------------------------------------------

GrB_Info LAGraph_cc_fastsv3
(
    GrB_Vector *result,     // output: array of component identifiers
    GrB_Matrix A,           // input matrix
    bool sanitize           // if true, ensure A is symmetric
)
{
    GrB_Info info;
    GrB_Index n, *mem = NULL, *I = NULL, *V = NULL ;
    GrB_Vector f = NULL, gp_new = NULL, mngp = NULL, mod = NULL, gp = NULL ;
    GrB_Matrix S = NULL ;

    LAGr_Matrix_nrows (&n, A) ;
    if (sanitize)
    {
        // S = A | A'
        LAGr_Matrix_new (&S, GrB_BOOL, n, n) ;
        LAGr_eWiseAdd (S, NULL, NULL, GrB_LOR, A, A, LAGraph_desc_otoo) ;
    }
    else
    {
        // Use the input as-is, and assume it is symmetric
        S = A ;
    }

    // determine # of threads to use for Reduce_assign
    int nthreads_max = LAGraph_get_nthreads ( ) ;
    int nthreads = n / (1024*1024) ;
    nthreads = LAGRAPH_MIN (nthreads, nthreads_max) ;
    nthreads = LAGRAPH_MAX (nthreads, 1) ;

    // vectors
    LAGr_Vector_new(&f,      GrB_UINT64, n);
    LAGr_Vector_new(&gp_new, GrB_UINT64, n);
    LAGr_Vector_new(&mod,    GrB_BOOL, n);
    // temporary arrays
    I = LAGraph_malloc (n, sizeof(GrB_Index));
    V = LAGraph_malloc (n, sizeof(uint64_t)) ;
    mem = (GrB_Index*) LAGraph_malloc (2*n, sizeof(GrB_Index)) ;
    // prepare vectors
    for (GrB_Index i = 0; i < n; i++)
        I[i] = V[i] = i;
    LAGr_Vector_build (f, I, V, n, GrB_PLUS_UINT64);
    LAGr_Vector_dup (&gp,  f);
    LAGr_Vector_dup (&mngp,f);
    // main computation
    bool diff = true ;
    while (diff)
    {
        // hooking & shortcutting
        LAGr_mxv (mngp, 0, GrB_MIN_UINT64, GxB_MIN_SECOND_UINT64, S, gp, 0);
        #if 0
        LAGRAPH_OK (Reduce_assign (f, mngp, V, n, I, mem, nthreads));
        #else
        LAGRAPH_OK (Reduce_assign (&f, &mngp, V, n, I, mem, nthreads));
        #endif
        LAGr_eWiseMult (f, 0, 0, GrB_MIN_UINT64, f, mngp, 0);
        LAGr_eWiseMult (f, 0, 0, GrB_MIN_UINT64, f, gp, 0);
        // calculate grandparent
        LAGr_Vector_extractTuples (NULL, V, &n, f);
        LAGr_extract (gp_new, 0, 0, f, V, n, 0);
        // check termination
        LAGr_eWiseMult (mod, 0, 0, GrB_NE_UINT64, gp_new, gp, 0);
        LAGr_reduce (&diff, 0, GxB_LOR_BOOL_MONOID, mod, 0);
        // swap gp and gp_new
        GrB_Vector t = gp ; gp = gp_new ; gp_new = t ; 
    }

    // free workspace and return result
    *result = f;
    f = NULL ;
    LAGRAPH_FREE_ALL ;
    return GrB_SUCCESS;
}

