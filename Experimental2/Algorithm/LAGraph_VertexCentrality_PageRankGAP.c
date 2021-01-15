//------------------------------------------------------------------------------
// LAGraph_VertexCentrality_PageRankGAP: pagerank for the GAP benchmark
//------------------------------------------------------------------------------

// LAGraph, (c) 2021 by The LAGraph Contributors, All Rights Reserved.
// SPDX-License-Identifier: BSD-2-Clause

//------------------------------------------------------------------------------

// PageRank for the GAP benchmark.  This is an "expert" method.

// This algorithm follows the specification given in the GAP Benchmark Suite:
// https://arxiv.org/abs/1508.03619 which assumes that both A and A' are
// already available, as are the row and column degrees.

// The GAP Benchmark algorithm assumes the graph has no nodes with no out-going
// edges (otherwise, a divide-by-zero occurs).  In terms of the adjacency
// matrix, it assumes there are no rows in A that have no entries.  As a
// result, this method should only be used for the GAP benchmark.  A more
// robust method would check this condition and handle nodes with no outgoing
// edges properly.

// The G->AT and G->rowdegree properties must be defined for this method.  If G
// is undirected or G->A is known to have a symmetric pattern, then G->A is
// used instead of G->AT, however.

// Contributed by Tim Davis and Mohsen Aznaveh.

#include "LAGraph_Internal.h"

#define LAGRAPH_FREE_WORK           \
{                                   \
    GrB_free (&d) ;                 \
    GrB_free (&t) ;                 \
    GrB_free (&w) ;                 \
}

#define LAGRAPH_FREE_ALL            \
{                                   \
    LAGRAPH_FREE_WORK ;             \
    GrB_free (&r) ;                 \
}

int LAGraph_VertexCentrality_PageRankGAP // returns -1 on failure, 0 on success
(
    // outputs:
    GrB_Vector *centrality, // centrality(i): GAP-style pagerank of node i
    // inputs:
    LAGraph_Graph G,        // input graph
    float damping,          // damping factor (typically 0.85)
    float tol,              // stopping tolerance (typically 1e-4) ;
    int itermax,            // maximum number of iterations (typically 100)
    int *iters,             // output: number of iterations taken
    char *msg
)
{

    //--------------------------------------------------------------------------
    // check inputs
    //--------------------------------------------------------------------------

    LAGraph_CLEAR_MSG ;
    GrB_Vector r = NULL, d = NULL, t = NULL, w = NULL ;
    LAGraph_CHECK (centrality == NULL, -1, "centrality is NULL") ;
    LAGraph_CHECK (LAGraph_CheckGraph (G, msg), -1, "graph is invalid") ;
    LAGraph_Kind kind = G->kind ; 
    int A_sym_pattern = G->A_pattern_is_symmetric ;
    GrB_Matrix AT ;
    if (kind == LAGRAPH_ADJACENCY_UNDIRECTED || A_sym_pattern == LAGRAPH_TRUE)
    {
        // A and A' have the same pattern
        AT = G->A ;
    }
    else
    {
        // A and A' differ
        AT = G->AT ;
        LAGraph_CHECK (AT == NULL, -1, "G->AT is required") ;
    }
    GrB_Vector d_out = G->rowdegree ;
    if (d_out == NULL, -1, "G->rowdegree is required") ;

    //--------------------------------------------------------------------------
    // initializations
    //--------------------------------------------------------------------------

    GrB_Index n ;
    (*centrality) = NULL ;
    GrB_TRY (GrB_Matrix_nrows (&n, AT)) ;

    const float teleport = (1 - damping) / n ;
    float rdiff = 1 ;       // first iteration is always done

    // r = 1 / n
    GrB_TRY (GrB_Vector_new (&t, GrB_FP32, n)) ;
    GrB_TRY (GrB_Vector_new (&r, GrB_FP32, n)) ;
    GrB_TRY (GrB_Vector_new (&w, GrB_FP32, n)) ;
    GrB_TRY (GrB_assign (r, NULL, NULL, 1.0 / n, GrB_ALL, n, NULL)) ;

    // prescale with damping factor, so it isn't done each iteration
    // d = d_out / damping ;
    GrB_TRY (GrB_Vector_new (&d, GrB_FP32, n)) ;
    GrB_TRY (GrB_apply (d, NULL, NULL, GrB_DIV_FP32, d_out, damping, NULL)) ;

    //--------------------------------------------------------------------------
    // pagerank iterations
    //--------------------------------------------------------------------------

    for ((*iters) = 0 ; (*iters) < itermax && rdiff > tol ; (*iters)++)
    {
        // swap t and r ; now t is the old score
        GrB_Vector temp = t ; t = r ; r = temp ;

        // w = t ./ d
        GrB_TRY (GrB_eWiseMult (w, NULL, NULL, GrB_DIV_FP32, t, d, NULL)) ;

        // r = teleport
        GrB_TRY (GrB_assign (r, NULL, NULL, teleport, GrB_ALL, n, NULL)) ;

        // r += A'*w
        GrB_TRY (GrB_mxv (r, NULL, GrB_PLUS_FP32, GxB_PLUS_SECOND_FP32, AT, w,
            NULL)) ;

        // t -= r
        GrB_TRY (GrB_assign (t, NULL, GrB_MINUS_FP32, r, GrB_ALL, n, NULL)) ;

        // t = abs (t)
        GrB_TRY (GrB_apply (t, NULL, NULL, GxB_ABS_FP32, t, NULL)) ;

        // rdiff = sum (t)
        GrB_TRY (GrB_reduce (&rdiff, NULL, GxB_PLUS_FP32_MONOID, t, NULL)) ;
    }

    //--------------------------------------------------------------------------
    // free workspace and return result
    //--------------------------------------------------------------------------

    (*centrality) = r ;
    LAGRAPH_FREE_WORK ;
    return (0) ;
}
