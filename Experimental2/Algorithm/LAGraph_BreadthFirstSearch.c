//------------------------------------------------------------------------------
// LAGraph_BreadthFirstSearch:  breadth-first search
//------------------------------------------------------------------------------

// LAGraph, (c) 2021 by The LAGraph Contributors, All Rights Reserved.
// SPDX-License-Identifier: BSD-2-Clause

//------------------------------------------------------------------------------

// Usage:

// status = LAGraph_BreadthFirstSearch (&parent, &level, G, src, pushpull, msg);

// GrB_Vector *parent: a vector containing the BFS tree.  parent(src) = src for
//      source node.  parent(i) = p if p is the parent of i.  If parent(i) is
//      not present, then node i has not been reached.  The parent vector is
//      not computed if &parent is NULL.

// GrB_Vector *level: a vector containing the level of each node in the BFS
//      tree.  level(src)=0, and level(i)=l if node i is in level l > 0.  If
//      level(i) is not present, then node i has not been reached.  The level
//      vector is not computed if &level is NULL.

// LAGraph_Graph G: an undirected or directed graph.  If the graph is directed
//      and G->AT does not exist, then push-only method is used.  Otherwise,
//      a push-pull direction optimizing method is used.  The G->rowdegree
//      must also be present to use the push-pull method.

// int64_t src: the source node for the BFS.

// bool pushpull:  if true, and if the G->AT and G->rowdegree properties exist
//      on input, then a push/pull method is used.  Otherwise, a push-only
//      method is used.

// int status: 0 if successful, -1 on error

// char *msg: string for error reporting

// References:

// Carl Yang, Aydin Buluc, and John D. Owens. 2018. Implementing Push-Pull
// Efficiently in GraphBLAS. In Proceedings of the 47th International
// Conference on Parallel Processing (ICPP 2018). ACM, New York, NY, USA,
// Article 89, 11 pages. DOI: https://doi.org/10.1145/3225058.3225122

// Scott Beamer, Krste Asanovic and David A. Patterson, The GAP Benchmark
// Suite, http://arxiv.org/abs/1508.03619, 2015.  http://gap.cs.berkeley.edu/

#define LAGRAPH_FREE_WORK   \
{                           \
    GrB_free (&w) ;         \
    GrB_free (&q) ;         \
}

#define LAGRAPH_FREE_ALL    \
{                           \
    LAGRAPH_FREE_WORK ;     \
    GrB_free (&pi) ;        \
    GrB_free (&v) ;         \
}

#include "LG_internal.h"

int LAGraph_BreadthFirstSearch      // returns -1 on failure, 0 if successful
(
    // outputs:
    GrB_Vector *level,      // not computed if NULL
    GrB_Vector *parent,     // not computed if NULL
    // inputs:
    LAGraph_Graph G,        // graph to traverse
    GrB_Index src,          // source node
    bool pushpull,          // if true, use push/pull, otherwise pushonly
    char *msg
)
{

    //--------------------------------------------------------------------------
    // check inputs
    //--------------------------------------------------------------------------

    LG_CLEAR_MSG ;
    GrB_Vector q = NULL ;           // the current frontier
    GrB_Vector w = NULL ;           // to compute work remaining
    GrB_Vector pi = NULL ;          // parent vector
    GrB_Vector v = NULL ;           // level vector

    bool compute_level  = (level != NULL) ;
    bool compute_parent = (parent != NULL) ;
    if (compute_level ) (*level ) = NULL ;
    if (compute_parent) (*parent) = NULL ;

    LG_CHECK (LAGraph_CheckGraph (G, msg), -1, "graph is invalid") ;

    if (!(compute_level || compute_parent))
    {
        // nothing to do
        return (0) ;
    }

    //--------------------------------------------------------------------------
    // get the problem size and properties
    //--------------------------------------------------------------------------

    GrB_Matrix A = G->A ;
    GrB_Matrix AT ;
    GrB_Vector Degree = G->rowdegree ;
    LAGraph_Kind kind = G->kind ;

    if (kind == LAGRAPH_ADJACENCY_UNDIRECTED ||
       (kind == LAGRAPH_ADJACENCY_DIRECTED &&
        G->A_pattern_is_symmetric == LAGRAPH_TRUE))
    {
        // AT and A have the same pattern and can be used in both directions
        AT = G->A ;
    }
    else
    {
        // AT = A' is different from A
        AT = G->AT ;
    }

    GrB_Index n, nvals ;
    GrB_TRY (GrB_Matrix_nrows (&n, A)) ;
    GrB_TRY (GrB_Matrix_nvals (&nvals, A)) ;

    // direction-optimization requires AT and Degree
    bool push_pull = pushpull && (AT != NULL) && (Degree != NULL) ;

    // determine the semiring type
    GrB_Type int_type = (n > INT32_MAX) ? GrB_INT64 : GrB_INT32 ;
    GrB_Semiring semiring ;

    if (compute_parent)
    {
        // use the ANY_SECONDI_INT* semiring: either 32 or 64-bit depending on
        // the # of nodes in the graph.
        semiring = (n > INT32_MAX) ?
            GxB_ANY_SECONDI_INT64 : GxB_ANY_SECONDI_INT32 ;

        // create the parent vector.  pi(i) is the parent id of node i
        GrB_TRY (GrB_Vector_new (&pi, int_type, n)) ;
        GrB_TRY (GxB_set (pi, GxB_SPARSITY_CONTROL, GxB_BITMAP + GxB_FULL)) ;
        // pi (src) = src denotes the root of the BFS tree
        GrB_TRY (GrB_Vector_setElement (pi, src, src)) ;

        // create a sparse integer vector q, and set q(src) = src
        GrB_TRY (GrB_Vector_new (&q, int_type, n)) ;
        GrB_TRY (GrB_Vector_setElement (q, src, src)) ;
    }
    else
    {
        // only the level is needed, use the ANY_PAIR_BOOL semiring
        semiring = GxB_ANY_PAIR_BOOL ;

        // create a sparse boolean vector q, and set q(src) = true
        GrB_TRY (GrB_Vector_new (&q, GrB_BOOL, n)) ;
        GrB_TRY (GrB_Vector_setElement (q, true, src)) ;
    }

    if (compute_level)
    {
        // create the level vector. v(i) is the level of node i
        // v (src) = 0 denotes the source node
        GrB_TRY (GrB_Vector_new (&v, int_type, n)) ;
        GrB_TRY (GxB_set (v, GxB_SPARSITY_CONTROL, GxB_BITMAP + GxB_FULL)) ;
        GrB_TRY (GrB_Vector_setElement (v, 0, src)) ;
    }

    if (push_pull)
    {
        // workspace for computing work remaining
        GrB_TRY (GrB_Vector_new (&w, GrB_INT64, n)) ;
    }

    GrB_Index nq = 1 ;          // number of nodes in the current level
    double alpha = 8.0 ;
    double beta1 = 8.0 ;
    double beta2 = 512.0 ;
    int64_t n_over_beta1 = (int64_t) (((double) n) / beta1) ;
    int64_t n_over_beta2 = (int64_t) (((double) n) / beta2) ;

    //--------------------------------------------------------------------------
    // BFS traversal and label the nodes
    //--------------------------------------------------------------------------

    bool do_push = true ;       // start with push
    GrB_Index last_nq = 0 ;
    int64_t edges_unexplored = nvals ;
    bool any_pull = false ;     // true if any pull phase has been done

    // {!mask} is the set of unvisited nodes
    GrB_Vector mask = (compute_parent) ? pi : v ;

    for (int64_t nvisited = 1, k = 1 ; nvisited < n ; nvisited += nq, k++)
    {

        //----------------------------------------------------------------------
        // select push vs pull
        //----------------------------------------------------------------------

        if (push_pull)
        {
            if (do_push)
            {
                // check for switch from push to pull
                bool growing = nq > last_nq ;
                bool switch_to_pull = false ;
                if (edges_unexplored < n)
                { 
                    // very little of the graph is left; disable the pull
                    push_pull = false ;
                }
                else if (any_pull)
                { 
                    // once any pull phase has been done, the # of edges in the
                    // frontier has no longer been tracked.  But now the BFS
                    // has switched back to push, and we're checking for yet
                    // another switch to pull.  This switch is unlikely, so
                    // just keep track of the size of the frontier, and switch
                    // if it starts growing again and is getting big.
                    switch_to_pull = (growing && nq > n_over_beta1) ;
                }
                else
                {
                    // update the # of unexplored edges
                    // w<q>=Degree
                    // w(i) = outdegree of node i if node i is in the queue
                    GrB_TRY (GrB_assign (w, q, NULL, Degree, GrB_ALL, n,
                        GrB_DESC_RS)) ;
                    // edges_in_frontier = sum (w) = # of edges incident on all
                    // nodes in the current frontier
                    int64_t edges_in_frontier = 0 ;
                    GrB_TRY (GrB_reduce (&edges_in_frontier, NULL,
                        GrB_PLUS_MONOID_INT64, w, NULL)) ;
                    edges_unexplored -= edges_in_frontier ;
                    switch_to_pull = growing &&
                        (edges_in_frontier > (edges_unexplored / alpha)) ;
                }
                if (switch_to_pull)
                {
                    // switch from push to pull
                    do_push = false ;
                }
            }
            else
            {
                // check for switch from pull to push
                bool shrinking = nq < last_nq ;
                if (shrinking && (nq <= n_over_beta2))
                {
                    // switch from pull to push
                    do_push = true ;
                }
            }
            any_pull = any_pull || (!do_push) ;
        }

        //----------------------------------------------------------------------
        // q = kth level of the BFS
        //----------------------------------------------------------------------

        int sparsity = do_push ? GxB_SPARSE : GxB_BITMAP ;
        GrB_TRY (GxB_set (q, GxB_SPARSITY_CONTROL, sparsity)) ;

        // mask is pi if computing parent, v if computing just level
        if (do_push)
        {
            // q'{!mask} = q'*A
            GrB_TRY (GrB_vxm (q, mask, NULL, semiring, q, A, GrB_DESC_RSC)) ;
        }
        else
        {
            // q{!mask} = AT*q
            GrB_TRY (GrB_mxv (q, mask, NULL, semiring, AT, q, GrB_DESC_RSC)) ;
        }

        //----------------------------------------------------------------------
        // done if q is empty
        //----------------------------------------------------------------------

        last_nq = nq ;
        GrB_TRY (GrB_Vector_nvals (&nq, q)) ;
        if (nq == 0)
        {
            break ;
        }

        //----------------------------------------------------------------------
        // assign parents/levels
        //----------------------------------------------------------------------

        if (compute_parent)
        {
            // q(i) currently contains the parent id of node i in tree.
            // pi<s(q)> = q
            GrB_TRY (GrB_assign (pi, q, NULL, q, GrB_ALL, n, GrB_DESC_S)) ;
        }
        if (compute_level)
        {
            // v<s(q)> = k, the kth level of the BFS
            GrB_TRY (GrB_assign (v, q, NULL, k, GrB_ALL, n, GrB_DESC_S)) ;
        }
    }

    //--------------------------------------------------------------------------
    // free workspace and return result
    //--------------------------------------------------------------------------

    if (compute_parent) (*parent) = pi ;
    if (compute_level ) (*level ) = v ;
    LAGRAPH_FREE_WORK ;
    return (0) ;
}

