//------------------------------------------------------------------------------
// test_louvain_lsmp: read in graphchallenge matrix and test the
// louvain_lsmp community detection.
// ------------------------------------------------------------------------------

// LAGraph, (c) 2021 by The LAGraph Contributors, All Rights Reserved.
// SPDX-License-Identifier: BSD-2-Clause

//------------------------------------------------------------------------------

// Contributed by Michel Pelletier

#include "LAGraph_Test.h"

#define NTHREAD_LIST 1
// #define NTHREAD_LIST 2
#define THREAD_LIST 0

// #define NTHREAD_LIST 6
// #define THREAD_LIST 64, 32, 24, 12, 8, 4

#define LAGRAPH_FREE_ALL                        \
{                                               \
    if (f != NULL) fclose (f) ;                 \
}

int main (int argc, char **argv)
{

    printf ("Louvain_Lsmp test with %s v%d.%d.%d [%s]\n",
        GxB_IMPLEMENTATION_NAME,
        GxB_IMPLEMENTATION_MAJOR,
        GxB_IMPLEMENTATION_MINOR,
        GxB_IMPLEMENTATION_SUB,
        GxB_IMPLEMENTATION_DATE) ;

    char msg [LAGRAPH_MSG_LEN] ;


    LAGraph_Graph G = NULL ;
    GrB_Matrix A = NULL ;
    GrB_Vector C = NULL ;
    FILE *f = NULL ;

    // start GraphBLAS and LAGraph
    LAGraph_TRY (LAGraph_Init (msg)) ;
    GrB_TRY (GxB_set (GxB_BURBLE, false)) ;

    int nt = NTHREAD_LIST ;
    int Nthreads [20] = { 0, THREAD_LIST } ;
    int nthreads_max ;
    GrB_TRY (GxB_get (GxB_NTHREADS, &nthreads_max)) ;
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

    char *matrix_name = (argc > 1) ? argv [1] : "stdin" ; 
    LAGraph_TRY (LAGraph_Test_ReadProblem (&G, NULL,
        false, false, true, NULL, false, argc, argv, msg)) ;
    
    GrB_Index n, nvals ;
    GrB_TRY (GrB_Matrix_nrows (&n, G->A)) ;
    GrB_TRY (GrB_Matrix_nvals (&nvals, G->A)) ;

    //--------------------------------------------------------------------------
    // compute the louvain_lsmp
    //--------------------------------------------------------------------------

    int ntrials = 16 ;
    ntrials = 1 ;    // HACK to run just one trial
    printf ("# of trials: %d\n", ntrials) ;

    int iters = 0, itermax = 10 ;

    for (int kk = 1 ; kk <= nt ; kk++)
    {
        int nthreads = Nthreads [kk] ;
        if (nthreads > nthreads_max) continue ;
        GxB_set (GxB_NTHREADS, nthreads) ;
        printf ("\n--------------------------- nthreads: %2d\n", nthreads) ;

        double total_time = 0 ;

        for (int trial = 0 ; trial < ntrials ; trial++)
        {
            GrB_free (&C) ;
            LAGraph_TRY (LAGraph_Tic (tic, NULL)) ;
            LAGraph_TRY (LAGraph_Louvain_LSMP (&C, G, itermax, &iters, msg)) ;
            double t1 ;
            LAGraph_TRY (LAGraph_Toc (&t1, tic, NULL)) ;
            printf ("trial: %2d time: %10.4f sec\n", trial, t1) ;
            total_time += t1 ;
        }

        double t = total_time / ntrials ;
        printf ("3f:%3d: avg time: %10.3f (sec), "
                "rate: %10.3f iters: %d\n", nthreads,
                t, 1e-6*((double) nvals) * iters / t, iters) ;
        fprintf (stderr, "Avg:  %3d: %10.3f sec: %s\n",
             nthreads, t, matrix_name) ;

    }
    
    //--------------------------------------------------------------------------
    // check result
    //--------------------------------------------------------------------------

    GxB_print (C, 2) ;

    //--------------------------------------------------------------------------
    // free all workspace and finish
    //--------------------------------------------------------------------------

    LAGRAPH_FREE_ALL ;
    LAGraph_TRY (LAGraph_Finalize (msg)) ;
    return (0) ;
}

