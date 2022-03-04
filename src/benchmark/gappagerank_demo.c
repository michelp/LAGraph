//------------------------------------------------------------------------------
// LAGraph/src/benchmark/gappagerank_demo.c: benchmark GAP PageRank
//------------------------------------------------------------------------------

// LAGraph, (c) 2021 by The LAGraph Contributors, All Rights Reserved.
// SPDX-License-Identifier: BSD-2-Clause
// See additional acknowledgments in the LICENSE file,
// or contact permission@sei.cmu.edu for the full terms.

// Contributed by Timothy A. Davis, Texas A&M University, and Gabor Szarnyas,
// BME

//------------------------------------------------------------------------------

#include "LAGraph_demo.h"

#define NTHREAD_LIST 1
// #define NTHREAD_LIST 2
#define THREAD_LIST 0

// #define NTHREAD_LIST 6
// #define THREAD_LIST 64, 32, 24, 12, 8, 4

#define LG_FREE_ALL                             \
{                                               \
    GrB_free (&A) ;                             \
    GrB_free (&Abool) ;                         \
    GrB_free (&PR) ;                            \
    LAGraph_Delete (&G, msg) ;                  \
    if (f != NULL) fclose (f) ;                 \
}

int main (int argc, char **argv)
{

    char msg [LAGRAPH_MSG_LEN] ;

    LAGraph_Graph G = NULL ;

    GrB_Matrix A = NULL ;
    GrB_Matrix Abool = NULL ;
    GrB_Vector PR = NULL ;
    FILE *f = NULL ;

    // start GraphBLAS and LAGraph
    bool burble = false ;
    demo_init (burble) ;

    int nt = NTHREAD_LIST ;
    int Nthreads [20] = { 0, THREAD_LIST } ;
    int nthreads_max ;
    LAGraph_TRY (LAGraph_GetNumThreads (&nthreads_max, NULL)) ;
    if (Nthreads [1] == 0)
    {
        // create thread list automatically
        Nthreads [1] = nthreads_max ;
        for (int t = 2 ; t <= nt ; t++)
        {
            Nthreads [t] = Nthreads [t-1] / 2 ;
            if (Nthreads [t] == 0) nt = t-1 ;
        }
    }
    printf ("threads to test: ") ;
    for (int t = 1 ; t <= nt ; t++)
    {
        int nthreads = Nthreads [t] ;
        if (nthreads > nthreads_max) continue ;
        printf (" %d", nthreads) ;
    }
    printf ("\n") ;

    double tic [2] ;

    //--------------------------------------------------------------------------
    // read in the graph
    //--------------------------------------------------------------------------

    char *matrix_name = (argc > 1) ? argv [1] : "stdin" ;
    LAGraph_TRY (readproblem (&G, NULL,
        false, false, true, NULL, false, argc, argv)) ;
    GrB_Index n, nvals ;
    GrB_TRY (GrB_Matrix_nrows (&n, G->A)) ;
    GrB_TRY (GrB_Matrix_nvals (&nvals, G->A)) ;

    // determine the row degree property
    LAGraph_TRY (LAGraph_Property_RowDegree (G, msg)) ;

    // check # of sinks:
    GrB_Index nsinks ;
    GrB_TRY (GrB_Vector_nvals (&nvals, G->rowdegree)) ;
    nsinks = n - nvals ;
    printf ("nsinks: %" PRIu64 "\n", nsinks) ;

    //--------------------------------------------------------------------------
    // compute the GAP pagerank
    //--------------------------------------------------------------------------

    // the GAP benchmark requires 16 trials
    int ntrials = 16 ;
    // ntrials = 1 ;    // HACK to run just one trial
    printf ("# of trials: %d\n", ntrials) ;

    float damping = 0.85 ;
    float tol = 1e-4 ;
    int iters = 0, itermax = 100 ;

    for (int kk = 1 ; kk <= nt ; kk++)
    {
        int nthreads = Nthreads [kk] ;
        if (nthreads > nthreads_max) continue ;
        LAGraph_TRY (LAGraph_SetNumThreads (nthreads, msg)) ;
        printf ("\n--------------------------- nthreads: %2d\n", nthreads) ;

        double total_time = 0 ;

        for (int trial = 0 ; trial < ntrials ; trial++)
        {
            GrB_free (&PR) ;
            LAGraph_TRY (LAGraph_Tic (tic, NULL)) ;
            LAGraph_TRY (LAGr_PageRankGAP (&PR, &iters, G,
                damping, tol, itermax, msg)) ;
            double t1 ;
            LAGraph_TRY (LAGraph_Toc (&t1, tic, NULL)) ;
            printf ("trial: %2d time: %10.4f sec\n", trial, t1) ;
            total_time += t1 ;
        }

        float rsum ;
        GrB_TRY (GrB_reduce (&rsum, NULL, GrB_PLUS_MONOID_FP32, PR, NULL)) ;

        double t = total_time / ntrials ;
        printf ("GAP: %3d: avg time: %10.3f (sec), "
                "rate: %10.3f iters: %d rsum: %e\n", nthreads,
                t, 1e-6*((double) nvals) * iters / t, iters, rsum) ;
        fprintf (stderr, "GAP: Avg: PR %3d: %10.3f sec: %s rsum: %e\n",
             nthreads, t, matrix_name, rsum) ;

    }

    //--------------------------------------------------------------------------
    // compute the standard pagerank
    //--------------------------------------------------------------------------

    // the STD pagerank may be slower than the GAP-style pagerank, because it
    // must do extra work to handle sinks.  sum(PR) will always equal 1.

    for (int kk = 1 ; kk <= nt ; kk++)
    {
        int nthreads = Nthreads [kk] ;
        if (nthreads > nthreads_max) continue ;
        LAGraph_TRY (LAGraph_SetNumThreads (nthreads, msg)) ;
        printf ("\n--------------------------- nthreads: %2d\n", nthreads) ;

        double total_time = 0 ;

        for (int trial = 0 ; trial < ntrials ; trial++)
        {
            GrB_free (&PR) ;
            LAGraph_TRY (LAGraph_Tic (tic, NULL)) ;
            LAGraph_TRY (LAGr_PageRank (&PR, &iters, G,
                damping, tol, itermax, msg)) ;
            double t1 ;
            LAGraph_TRY (LAGraph_Toc (&t1, tic, NULL)) ;
            printf ("trial: %2d time: %10.4f sec\n", trial, t1) ;
            total_time += t1 ;
        }

        float rsum ;
        GrB_TRY (GrB_reduce (&rsum, NULL, GrB_PLUS_MONOID_FP32, PR, NULL)) ;

        double t = total_time / ntrials ;
        printf ("STD: %3d: avg time: %10.3f (sec), "
                "rate: %10.3f iters: %d rsum: %e\n", nthreads,
                t, 1e-6*((double) nvals) * iters / t, iters, rsum) ;
        fprintf (stderr, "STD: Avg: PR %3d: %10.3f sec: %s rsum: %e\n",
             nthreads, t, matrix_name, rsum) ;

    }

    //--------------------------------------------------------------------------
    // free all workspace and finish
    //--------------------------------------------------------------------------

    LG_FREE_ALL ;
    LAGraph_TRY (LAGraph_Finalize (msg)) ;
    return (GrB_SUCCESS) ;
}
