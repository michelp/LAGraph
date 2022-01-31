//------------------------------------------------------------------------------
// LAGraph_Random_Matrix: generate a random matrix
//------------------------------------------------------------------------------

// SuiteSparse:GraphBLAS, Timothy A. Davis, (c) 2017-2021, All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0
// Contributed by Tim Davis, Texas A&M University

//------------------------------------------------------------------------------

// Constructs a sparse roughly uniformly distributed random matrix with roughly
// density*nrows*ncols entries.  If density == INFINITY then the matrix is
// generated with all entries present.

// If the type is GrB_FP32 or GrB_FP64, the values of A are returned in the
// range [0,1].  If any duplicate entries are generated, the largest one is
// take, so the distribution can be skewed towards 1 if the density is large.
// This could be fixed by using the GxB_IGNORE_DUP operator, but this would
// require SuiteSparse:GraphBLAS.

#define LAGraph_FREE_WORK               \
{                                       \
    LAGraph_Free ((void **) &I) ;       \
    LAGraph_Free ((void **) &J) ;       \
    LAGraph_Free ((void **) &ignore) ;  \
    LAGraph_Free (&X) ;                 \
    GrB_free (&Mod) ;                   \
    GrB_free (&Rows) ;                  \
    GrB_free (&Cols) ;                  \
    GrB_free (&Values) ;                \
    GrB_free (&Seed) ;                  \
    GrB_free (&T) ;                     \
}

#define LAGraph_FREE_ALL                \
{                                       \
    LAGraph_FREE_WORK ;                 \
    GrB_free (A) ;                      \
}

#include "LG_internal.h"
#include "LAGraphX.h"

// uncomment these to test vanilla case for just this file:
// #undef LG_SUITESPARSE
// #define LG_SUITESPARSE 0

//------------------------------------------------------------------------------
// mod function for uint64: z = x % y
//------------------------------------------------------------------------------

void mod_function (void *z, const void *x, const void *y)
{
    uint64_t a = (*((uint64_t *) x)) ;
    uint64_t b = (*((uint64_t *) y)) ;
    (*((uint64_t *) z)) = a % b ;
}

//------------------------------------------------------------------------------
// LAGraph_Random_Matrix
//------------------------------------------------------------------------------

GrB_Info LAGraph_Random_Matrix    // random matrix of any built-in type
(
    // output
    GrB_Matrix *A,      // A is constructed on output
    // input
    GrB_Type type,      // type of matrix to construct
    GrB_Index nrows,    // # of rows of A
    GrB_Index ncols,    // # of columns of A
    double density,     // density: build a sparse matrix with
                        // density*nrows*cols values if not INFINITY;
                        // build a dense matrix if INFINITY.
    uint64_t seed,      // random number seed
    char *msg
)
{

    //--------------------------------------------------------------------------
    // check inputs
    //--------------------------------------------------------------------------

    LG_CLEAR_MSG ;
    GrB_BinaryOp Mod = NULL ;
    GrB_Vector Rows = NULL, Cols = NULL, Values = NULL, Seed = NULL ;
    GrB_Matrix T = NULL ;
    GrB_Index *I = NULL, *J = NULL, *ignore = NULL ;
    GrB_Index I_size = 0, J_size = 0, X_size = 0 ;
    void *X = NULL ;
    LG_ASSERT (A != NULL && type != NULL, GrB_NULL_POINTER) ;
    LG_ASSERT_MSG (density >= 0, GrB_INVALID_VALUE, "invalid density") ;    // FIXME:RETVAL

    LG_ASSERT_MSG (type == GrB_BOOL
        || type == GrB_INT8   || type == GrB_INT16 || type == GrB_INT32
        || type == GrB_INT64  || type == GrB_UINT8 || type == GrB_UINT16
        || type == GrB_UINT32 || type == GrB_UINT64
        || type == GrB_FP32   || type == GrB_FP64,
        GrB_NOT_IMPLEMENTED, "unsupported type") ;    // FIXME:RETVAL

    GrB_TRY (GrB_Matrix_new (A, type, nrows, ncols)) ;
    if (nrows == 0 || ncols == 0)
    {
        // nothing to do: return A as the requested empty matrix
        return (GrB_SUCCESS) ;
    }

    //--------------------------------------------------------------------------
    // create the Mod operator
    //--------------------------------------------------------------------------

    GrB_TRY (GrB_BinaryOp_new (&Mod, mod_function,
        GrB_UINT64, GrB_UINT64, GrB_UINT64)) ;

    //--------------------------------------------------------------------------
    // determine the number of entries to generate
    //--------------------------------------------------------------------------

    bool A_is_full = isinf (density) ;
    GrB_Index nvals ;
    if (A_is_full)
    {
        // determine number of tuples for building a random dense matrix
        double nx = (double) nrows * (double) ncols ;
        LG_ASSERT_MSG (nx < (double) GrB_INDEX_MAX, GrB_OUT_OF_MEMORY,
            "Problem too large") ;
        nvals = nrows * ncols ;
    }
    else
    {
        // determine number of tuples for building a random sparse matrix
        double nx = density * (double) nrows * (double) ncols ;
        nx = round (nx) ;
        nx = fmax (nx, (double) 0) ;
        nx = fmin (nx, (double) GrB_INDEX_MAX) ;
        nvals = (GrB_Index) nx ;
    }

    //--------------------------------------------------------------------------
    // allocate workspace
    //--------------------------------------------------------------------------

    #if !LG_SUITESPARSE
    {
        ignore = LAGraph_Malloc (nvals, sizeof (GrB_Index)) ;
        I = LAGraph_Malloc (nvals, sizeof (GrB_Index)) ;
        J = LAGraph_Malloc (nvals, sizeof (GrB_Index)) ;
        LG_ASSERT (I != NULL && J != NULL && ignore != NULL,
            GrB_OUT_OF_MEMORY) ;
    }
    #endif

    //--------------------------------------------------------------------------
    // construct the random Seed vector
    //--------------------------------------------------------------------------

    GrB_TRY (GrB_Vector_new (&Seed, GrB_UINT64, nvals)) ;
    GrB_TRY (GrB_assign (Seed, NULL, NULL, 0, GrB_ALL, nvals, NULL)) ;
    LG_TRY (LAGraph_Random_Seed (Seed, seed, msg)) ;

    //--------------------------------------------------------------------------
    // construct the random indices if A is sparse, or all indices if full
    //--------------------------------------------------------------------------

    if (!A_is_full)
    {

        //----------------------------------------------------------------------
        // construct random indices for a sparse matrix
        //----------------------------------------------------------------------

        // Rows = mod (Seed, nrows) ;
        GrB_TRY (GrB_Vector_new (&Rows, GrB_UINT64, nvals)) ;
        GrB_TRY (GrB_apply (Rows, NULL, NULL, Mod, Seed, nrows, NULL)) ;

        // Seed = next (Seed)
        LG_TRY (LAGraph_Random_Next (Seed, msg)) ;

        // Cols = mod (Seed, ncols) ;
        GrB_TRY (GrB_Vector_new (&Cols, GrB_UINT64, nvals)) ;
        GrB_TRY (GrB_apply (Cols, NULL, NULL, Mod, Seed, ncols, NULL)) ;

        // Seed = next (Seed)
        LG_TRY (LAGraph_Random_Next (Seed, msg)) ;

        //----------------------------------------------------------------------
        // extract the indices
        //----------------------------------------------------------------------

        #if LG_SUITESPARSE
        {
            // this takes O(1) time and space
            GrB_TRY (GxB_Vector_unpack_Full (Rows, (void **) &I, &I_size,
                NULL, NULL)) ;
            GrB_TRY (GxB_Vector_unpack_Full (Cols, (void **) &J, &J_size,
                NULL, NULL)) ;
        }
        #else
        {
            // this takes O(nvals) time and space
            GrB_TRY (GrB_Vector_extractTuples_UINT64 (ignore, I, &nvals,
                Rows)) ;
            GrB_TRY (GrB_Vector_extractTuples_UINT64 (ignore, J, &nvals,
                Cols)) ;
        }
        #endif

        GrB_free (&Rows) ;
        GrB_free (&Cols) ;

    }
    else
    {

        //----------------------------------------------------------------------
        // construct indices for a full matrix
        //----------------------------------------------------------------------

        #if !LG_SUITESPARSE
        {
            // T = true (nrows, ncols) ;
            GrB_TRY (GrB_Matrix_new (&T, GrB_BOOL, nrows, ncols)) ;
            GrB_TRY (GrB_assign (T, NULL, NULL, true,
                GrB_ALL, nrows, GrB_ALL, ncols, NULL)) ;
            // extract the row and column indices from T
            GrB_TRY (GrB_Matrix_extractTuples (I, J, (bool *) ignore, &nvals,
                T)) ;
            GrB_free (&T) ;
        }
        #endif
    }

    //-------------------------------------------------------------------------
    // construct the random values
    //-------------------------------------------------------------------------

    if (type == GrB_BOOL)
    {
        // Values = (Seed < UINT64_MAX / 2)
        GrB_TRY (GrB_Vector_new (&Values, GrB_BOOL, nvals)) ;
        GrB_TRY (GrB_apply (Values, NULL, NULL,
            GrB_LT_UINT64, Seed, UINT64_MAX / 2, NULL)) ;
    }
    else if (type == GrB_UINT64)
    {
        // no need to allocate the Values vector; just use the Seed itself
        Values = Seed ;
        Seed = NULL ;
    }
    else
    {
        // Values = (type) Seed
        GrB_TRY (GrB_Vector_new (&Values, type, nvals)) ;
        GrB_TRY (GrB_assign (Values, NULL, NULL, Seed, GrB_ALL, nvals,
            NULL)) ;
    }
    GrB_free (&Seed) ;

    // scale the values to the range [0,1], if floating-point
    if (type == GrB_FP32)
    {
        // Values = Values / (float) UINT64_MAX
        GrB_TRY (GrB_apply (Values, NULL, NULL, GrB_DIV_FP32,
            Values, (float) UINT64_MAX, NULL)) ;
    }
    else if (type == GrB_FP64)
    {
        // Values = Values / (double) UINT64_MAX
        GrB_TRY (GrB_apply (Values, NULL, NULL, GrB_DIV_FP64,
            Values, (double) UINT64_MAX, NULL)) ;
    }

    //--------------------------------------------------------------------------
    // extract the values
    //--------------------------------------------------------------------------

    #if LG_SUITESPARSE
    {
        // this takes O(1) time and space and works for any data type
        GrB_TRY (GxB_Vector_unpack_Full (Values, &X, &X_size, NULL, NULL)) ;
    }
    #else
    {
        // this takes O(nvals) time and space
        if (type == GrB_BOOL)
        {
            X = LAGraph_Malloc (nvals, sizeof (bool)) ;
            LG_ASSERT (X != NULL, GrB_OUT_OF_MEMORY) ;
            GrB_TRY (GrB_Vector_extractTuples_BOOL (ignore, X, &nvals,
                Values)) ;
        }
        else if (type == GrB_INT8)
        {
            X = LAGraph_Malloc (nvals, sizeof (int8_t)) ;
            LG_ASSERT (X != NULL, GrB_OUT_OF_MEMORY) ;
            GrB_TRY (GrB_Vector_extractTuples_INT8 (ignore, X, &nvals,
                Values)) ;
        }
        else if (type == GrB_INT16)
        {
            X = LAGraph_Malloc (nvals, sizeof (int16_t)) ;
            LG_ASSERT (X != NULL, GrB_OUT_OF_MEMORY) ;
            GrB_TRY (GrB_Vector_extractTuples_INT16 (ignore, X, &nvals,
                Values)) ;
        }
        else if (type == GrB_INT32)
        {
            X = LAGraph_Malloc (nvals, sizeof (int32_t)) ;
            LG_ASSERT (X != NULL, GrB_OUT_OF_MEMORY) ;
            GrB_TRY (GrB_Vector_extractTuples_INT32 (ignore, X, &nvals,
                Values)) ;
        }
        else if (type == GrB_INT64)
        {
            X = LAGraph_Malloc (nvals, sizeof (int64_t)) ;
            LG_ASSERT (X != NULL, GrB_OUT_OF_MEMORY) ;
            GrB_TRY (GrB_Vector_extractTuples_INT64 (ignore, X, &nvals,
                Values)) ;
        }
        else if (type == GrB_UINT8)
        {
            X = LAGraph_Malloc (nvals, sizeof (uint8_t)) ;
            LG_ASSERT (X != NULL, GrB_OUT_OF_MEMORY) ;
            GrB_TRY (GrB_Vector_extractTuples_UINT8 (ignore, X, &nvals,
                Values)) ;
        }
        else if (type == GrB_UINT16)
        {
            X = LAGraph_Malloc (nvals, sizeof (uint16_t)) ;
            LG_ASSERT (X != NULL, GrB_OUT_OF_MEMORY) ;
            GrB_TRY (GrB_Vector_extractTuples_UINT16 (ignore, X, &nvals,
                Values)) ;
        }
        else if (type == GrB_UINT32)
        {
            X = LAGraph_Malloc (nvals, sizeof (uint32_t)) ;
            LG_ASSERT (X != NULL, GrB_OUT_OF_MEMORY) ;
            GrB_TRY (GrB_Vector_extractTuples_UINT32 (ignore, X, &nvals,
                Values)) ;
        }
        else if (type == GrB_UINT64)
        {
            X = LAGraph_Malloc (nvals, sizeof (uint64_t)) ;
            LG_ASSERT (X != NULL, GrB_OUT_OF_MEMORY) ;
            GrB_TRY (GrB_Vector_extractTuples_UINT64 (ignore, X, &nvals,
                Values)) ;
        }
        else if (type == GrB_FP32)
        {
            X = LAGraph_Malloc (nvals, sizeof (float)) ;
            LG_ASSERT (X != NULL, GrB_OUT_OF_MEMORY) ;
            GrB_TRY (GrB_Vector_extractTuples_FP32 (ignore, X, &nvals,
                Values)) ;
        }
        else // if (type == GrB_FP64)
        {
            X = LAGraph_Malloc (nvals, sizeof (double)) ;
            LG_ASSERT (X != NULL, GrB_OUT_OF_MEMORY) ;
            GrB_TRY (GrB_Vector_extractTuples_FP64 (ignore, X, &nvals,
                Values)) ;
        }
        LAGraph_Free ((void **) &ignore) ;
    }
    #endif

    GrB_free (&Values) ;

    //--------------------------------------------------------------------------
    // build the matrix
    //--------------------------------------------------------------------------

    // Using GxB_IGNORE_DUP for the dup operator would be faster for
    // SuiteSparse, but it would result in a different matrix as compared to
    // the pure GrB case.

    #if LG_SUITESPARSE
    if (A_is_full)
    {
        // this takes O(1) time and space
        GrB_TRY (GxB_Matrix_pack_FullR (*A, &X, X_size, false, NULL)) ;
    }
    else
    #endif
    if (type == GrB_BOOL)
    {
        GrB_TRY (GrB_Matrix_build_BOOL   (*A, I, J, X, nvals, GrB_LXOR)) ;
    }
    else if (type == GrB_INT8)
    {
        GrB_TRY (GrB_Matrix_build_INT8   (*A, I, J, X, nvals, GrB_PLUS_INT8)) ;
    }
    else if (type == GrB_INT16)
    {
        GrB_TRY (GrB_Matrix_build_INT16  (*A, I, J, X, nvals, GrB_PLUS_INT16)) ;
    }
    else if (type == GrB_INT32)
    {
        GrB_TRY (GrB_Matrix_build_INT32  (*A, I, J, X, nvals, GrB_PLUS_INT32)) ;
    }
    else if (type == GrB_INT64)
    {
        GrB_TRY (GrB_Matrix_build_INT64  (*A, I, J, X, nvals, GrB_PLUS_INT64)) ;
    }
    else if (type == GrB_UINT8)
    {
        GrB_TRY (GrB_Matrix_build_UINT8  (*A, I, J, X, nvals, GrB_PLUS_UINT8)) ;
    }
    else if (type == GrB_UINT16)
    {
        GrB_TRY (GrB_Matrix_build_UINT16 (*A, I, J, X, nvals, GrB_PLUS_UINT16));
    }
    else if (type == GrB_UINT32)
    {
        GrB_TRY (GrB_Matrix_build_UINT32 (*A, I, J, X, nvals, GrB_PLUS_UINT32));
    }
    else if (type == GrB_UINT64)
    {
        GrB_TRY (GrB_Matrix_build_UINT64 (*A, I, J, X, nvals, GrB_PLUS_UINT64));
    }
    else if (type == GrB_FP32)
    {
        GrB_TRY (GrB_Matrix_build_FP32   (*A, I, J, X, nvals, GrB_MAX_FP32)) ;
    }
    else // if (type == GrB_FP64)
    {
        GrB_TRY (GrB_Matrix_build_FP64   (*A, I, J, X, nvals, GrB_MAX_FP64)) ;
    }

    //--------------------------------------------------------------------------
    // free workspace and return result
    //--------------------------------------------------------------------------

    LAGraph_FREE_WORK ;
    return (GrB_SUCCESS) ;
}

