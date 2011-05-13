/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                           */
/*                  This file is part of the program and library             */
/*         SCIP --- Solving Constraint Integer Programs                      */
/*                                                                           */
/*    Copyright (C) 2002-2011 Konrad-Zuse-Zentrum                            */
/*                            fuer Informationstechnik Berlin                */
/*                                                                           */
/*  SCIP is distributed under the terms of the ZIB Academic License.         */
/*                                                                           */
/*  You should have received a copy of the ZIB Academic License              */
/*  along with SCIP; see the file COPYING. If not email to scip@zib.de.      */
/*                                                                           */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/**@file   nlpi/expr.c
 * @brief  methods for expressions and expression trees
 * @author Stefan Vigerske
 * @author Thorsten Gellermann
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#include <stdarg.h>
#include <string.h>

#include "nlpi/pub_expr.h"
#include "nlpi/struct_expr.h"
#include "nlpi/exprinterpret.h"

#include "scip/intervalarith.h"
#include "scip/pub_misc.h"

#define SCIP_EXPRESSION_MAXCHILDEST 20       /**< estimate on maximal number of children */

/** sign of a value (-1 or +1)
 * 0.0 has sign +1
 */
#define SIGN(x) ((x) >= 0.0 ? 1.0 : -1.0)

/** signature of an expression (pointwise) evaluation function
 * The function should return nan, inf, or -inf in result if the function is undefined for the given arguments.
 *   
 * - opdata    operand data
 * - nargs     number of arguments
 * - argvals   values of arguments 
 * - varvals   values for variables 
 * - paramvals values for parameters 
 * - result    buffer where to store result of evaluation
 */
#define SCIP_DECL_EVAL(x) SCIP_RETCODE x (SCIP_EXPROPDATA opdata, int nargs, SCIP_Real* argvals, SCIP_Real* varvals, SCIP_Real* paramvals, SCIP_Real* result)

/** signature of an expression (interval) evaluation function
 * The function should return and empty interval if the function is undefined for the given arguments.
 *
 * - infinity  value for infinity
 * - opdata    operand data
 * - nargs     number of arguments
 * - argvals   interval values of arguments
 * - varvals   interval values for variables
 * - paramvals values for parameters
 * - result    buffer where to store result of evaluation
 */
#define SCIP_DECL_INTEVAL(x) SCIP_RETCODE x (SCIP_Real infinity, SCIP_EXPROPDATA opdata, int nargs, SCIP_INTERVAL* argvals, SCIP_INTERVAL* varvals, SCIP_Real* paramvals, SCIP_INTERVAL* result)

/* element in table of expression operands */
struct SCIPexprOpTableElement
{
   const char*           name;               /**< name of operand (used for printing) */
   int                   nargs;              /**< number of arguments (negative if not fixed) */
   SCIP_DECL_EVAL        ((*eval));          /**< evaluation function */
   SCIP_DECL_INTEVAL     ((*inteval));       /**< interval evaluation function */
};

static
SCIP_DECL_EVAL( SCIPexprevalPushVar )
{
   assert(result  != NULL);
   assert(varvals != NULL);
   
   *result = varvals[opdata.intval];
   
   return SCIP_OKAY;
} /*lint !e715*/

static
SCIP_DECL_INTEVAL( SCIPexprevalPushVarInt )
{
   assert(result  != NULL);
   assert(varvals != NULL);

   *result = varvals[opdata.intval];

   return SCIP_OKAY;
} /*lint !e715*/

static
SCIP_DECL_EVAL( SCIPexprevalPushValue )
{
   assert(result != NULL);
   
   *result = opdata.dbl;
   
   return SCIP_OKAY;
} /*lint !e715*/

static
SCIP_DECL_INTEVAL( SCIPexprevalPushValueInt )
{
   assert(result != NULL);

   SCIPintervalSet(result, opdata.dbl);

   return SCIP_OKAY;
} /*lint !e715*/

static
SCIP_DECL_EVAL( SCIPexprevalPushParameter )
{
   assert(result    != NULL);
   assert(paramvals != NULL );
   
   *result = paramvals[opdata.intval];
   
   return SCIP_OKAY;
} /*lint !e715*/

static
SCIP_DECL_INTEVAL( SCIPexprevalPushParameterInt )
{
   assert(result    != NULL);
   assert(paramvals != NULL );

   SCIPintervalSet(result, paramvals[opdata.intval]);

   return SCIP_OKAY;
} /*lint !e715*/

static
SCIP_DECL_EVAL( SCIPexprevalPlus )
{
   assert(result  != NULL);
   assert(argvals != NULL);

   *result = argvals[0] + argvals[1];
   
   return SCIP_OKAY;
} /*lint !e715*/

static
SCIP_DECL_INTEVAL( SCIPexprevalPlusInt )
{
   assert(result  != NULL);
   assert(argvals != NULL);

   SCIPintervalAdd(infinity, result, argvals[0], argvals[1]);

   return SCIP_OKAY;
} /*lint !e715*/

static
SCIP_DECL_EVAL( SCIPexprevalMinus )
{
   assert(result  != NULL);
   assert(argvals != NULL);

   *result = argvals[0] - argvals[1];
   
   return SCIP_OKAY;
} /*lint !e715*/

static
SCIP_DECL_INTEVAL( SCIPexprevalMinusInt )
{
   assert(result  != NULL);
   assert(argvals != NULL);

   SCIPintervalSub(infinity, result, argvals[0], argvals[1]);

   return SCIP_OKAY;
} /*lint !e715*/

static
SCIP_DECL_EVAL( SCIPexprevalMult )
{
   assert(result  != NULL);
   assert(argvals != NULL);

   *result = argvals[0] * argvals[1];

   return SCIP_OKAY;
} /*lint !e715*/

static
SCIP_DECL_INTEVAL( SCIPexprevalMultInt )
{
   assert(result  != NULL);
   assert(argvals != NULL);

   SCIPintervalMul(infinity, result, argvals[0], argvals[1]);

   return SCIP_OKAY;
} /*lint !e715*/

static
SCIP_DECL_EVAL( SCIPexprevalDiv )
{
   assert(result  != NULL);
   assert(argvals != NULL);

   *result = argvals[0] / argvals[1];
   
   return SCIP_OKAY;
} /*lint !e715*/

static
SCIP_DECL_INTEVAL( SCIPexprevalDivInt )
{
   assert(result  != NULL);
   assert(argvals != NULL);

   SCIPintervalDiv(infinity, result, argvals[0], argvals[1]);

   return SCIP_OKAY;
} /*lint !e715*/

static
SCIP_DECL_EVAL( SCIPexprevalSqr )
{
   assert(result  != NULL);
   assert(argvals != NULL);

   *result = argvals[0] * argvals[0];
   
   return SCIP_OKAY;
} /*lint !e715*/

static
SCIP_DECL_INTEVAL( SCIPexprevalSqrInt )
{
   assert(result  != NULL);
   assert(argvals != NULL);

   SCIPintervalSquare(infinity, result, argvals[0]);

   return SCIP_OKAY;
} /*lint !e715*/

static
SCIP_DECL_EVAL( SCIPexprevalSqrt )
{
   assert(result  != NULL);
   assert(argvals != NULL);

   *result = sqrt(argvals[0]);
   
   return SCIP_OKAY;
} /*lint !e715*/

static
SCIP_DECL_INTEVAL( SCIPexprevalSqrtInt )
{
   assert(result  != NULL);
   assert(argvals != NULL);

   SCIPintervalSquareRoot(infinity, result, argvals[0]);

   return SCIP_OKAY;
} /*lint !e715*/

static
SCIP_DECL_EVAL( SCIPexprevalPower )
{
   assert(result  != NULL);
   assert(argvals != NULL);

   *result = pow(argvals[0], argvals[1]);

   return SCIP_OKAY;
} /*lint !e715*/

static
SCIP_DECL_INTEVAL( SCIPexprevalPowerInt )
{
   assert(result  != NULL);
   assert(argvals != NULL);

   SCIPintervalPower(infinity, result, argvals[0], argvals[1]);

   return SCIP_OKAY;
} /*lint !e715*/

static
SCIP_DECL_EVAL( SCIPexprevalExp )
{
   assert(result  != NULL);
   assert(argvals != NULL);

   *result = exp(argvals[0]);
   
   return SCIP_OKAY;
} /*lint !e715*/

static
SCIP_DECL_INTEVAL( SCIPexprevalExpInt )
{
   assert(result  != NULL);
   assert(argvals != NULL);

   SCIPintervalExp(infinity, result, argvals[0]);

   return SCIP_OKAY;
} /*lint !e715*/

static
SCIP_DECL_EVAL( SCIPexprevalLog )
{
   assert(result  != NULL);
   assert(argvals != NULL);

   *result = log(argvals[0]);
   
   return SCIP_OKAY;
} /*lint !e715*/

static
SCIP_DECL_INTEVAL( SCIPexprevalLogInt )
{
   assert(result  != NULL);
   assert(argvals != NULL);

   SCIPintervalLog(infinity, result, argvals[0]);

   return SCIP_OKAY;
} /*lint !e715*/

static
SCIP_DECL_EVAL( SCIPexprevalSin )
{
   assert(result  != NULL);
   assert(argvals != NULL);

   *result = sin(argvals[0]);

   return SCIP_OKAY;
} /*lint !e715*/

static
SCIP_DECL_INTEVAL( SCIPexprevalSinInt )
{
   assert(result  != NULL);
   assert(argvals != NULL);

   /* @todo implement SCIPintervalSin */
   SCIPwarningMessage("SCIPexprevalSinInt gives only trivial bounds so far\n");
   SCIPintervalSetBounds(result, -1.0, 1.0);

   return SCIP_OKAY;
} /*lint !e715*/

static
SCIP_DECL_EVAL( SCIPexprevalCos )
{
   assert(result  != NULL);
   assert(argvals != NULL);

   *result = cos(argvals[0]);
   
   return SCIP_OKAY;
} /*lint !e715*/

static
SCIP_DECL_INTEVAL( SCIPexprevalCosInt )
{
   assert(result  != NULL);
   assert(argvals != NULL);

   /* @todo implement SCIPintervalCos */
   SCIPwarningMessage("SCIPexprevalCosInt gives only trivial bounds so far\n");
   SCIPintervalSetBounds(result, -1.0, 1.0);

   return SCIP_OKAY;
} /*lint !e715*/

static
SCIP_DECL_EVAL( SCIPexprevalTan )
{
   assert(result  != NULL);
   assert(argvals != NULL);

   *result = tan(argvals[0]);
   
   return SCIP_OKAY;
} /*lint !e715*/

/* @todo implement SCIPintervalTan */
#define SCIPexprevalTanInt NULL

#if 0
static
SCIP_DECL_EVAL( SCIPexprevalErf )
{
   assert(result  != NULL);
   assert(argvals != NULL);

   *result = erf(argvals[0]);
   
   return SCIP_OKAY;
} /*lint !e715*/

/* @todo implement SCIPintervalErf */
#define SCIPexprevalErfInt NULL

static
SCIP_DECL_EVAL( SCIPexprevalErfi )
{
   assert(result  != NULL);
   assert(argvals != NULL);

   /* @TODO implement erfi evaluation */
   SCIPerrorMessage("erfi not implemented");
   
   return SCIP_ERROR;
} /*lint !e715*/

/* @todo implement SCIPintervalErfi */
#define SCIPexprevalErfiInt NULL
#endif

static
SCIP_DECL_EVAL( SCIPexprevalMin )
{
   assert(result  != NULL);
   assert(argvals != NULL);

   *result = MIN(argvals[0], argvals[1]);
   
   return SCIP_OKAY;
} /*lint !e715*/

static
SCIP_DECL_INTEVAL( SCIPexprevalMinInt )
{
   assert(result  != NULL);
   assert(argvals != NULL);

   SCIPintervalMin(result, argvals[0], argvals[1]);

   return SCIP_OKAY;
} /*lint !e715*/

static
SCIP_DECL_EVAL( SCIPexprevalMax )
{
   assert(result  != NULL);
   assert(argvals != NULL);

   *result = MAX(argvals[0], argvals[1]);

   return SCIP_OKAY;
} /*lint !e715*/

static
SCIP_DECL_INTEVAL( SCIPexprevalMaxInt )
{
   assert(result  != NULL);
   assert(argvals != NULL);

   SCIPintervalMax(result, argvals[0], argvals[1]);

   return SCIP_OKAY;
} /*lint !e715*/

static
SCIP_DECL_EVAL( SCIPexprevalAbs )
{
   assert(result  != NULL);
   assert(argvals != NULL);

   *result = ABS(argvals[0]);
   
   return SCIP_OKAY;
} /*lint !e715*/

static
SCIP_DECL_INTEVAL( SCIPexprevalAbsInt )
{
   assert(result  != NULL);
   assert(argvals != NULL);

   SCIPintervalAbs(result, argvals[0]);

   return SCIP_OKAY;
} /*lint !e715*/

static
SCIP_DECL_EVAL( SCIPexprevalSign )
{
   assert(result  != NULL);
   assert(argvals != NULL);

   *result = SIGN(argvals[0]);
   
   return SCIP_OKAY;
} /*lint !e715*/

static
SCIP_DECL_INTEVAL( SCIPexprevalSignInt )
{
   assert(result  != NULL);
   assert(argvals != NULL);

   SCIPintervalSign(result, argvals[0]);

   return SCIP_OKAY;
} /*lint !e715*/

static
SCIP_DECL_EVAL( SCIPexprevalSignPower )
{
   assert(result  != NULL);
   assert(argvals != NULL);

   if( argvals[0] > 0 )
     *result =  pow( argvals[0], argvals[1]);
   else
     *result = -pow(-argvals[0], argvals[1]);

   return SCIP_OKAY;
} /*lint !e715*/

#if 0 /* @todo implement SCIPintervalSignPower */
static
SCIP_DECL_INTEVAL( SCIPexprevalSignPowerInt )
{
   assert(result  != NULL);
   assert(argvals != NULL);

   SCIPintervalSignPowerScalar(infinity, result, argvals[0], argvals[1]);

   return SCIP_OKAY;
} /*lint !e715*/
#endif

#define SCIPexprevalSignPowerInt NULL

static
SCIP_DECL_EVAL( SCIPexprevalIntPower )
{
   assert(result  != NULL);
   assert(argvals != NULL);

   switch( opdata.intval )
   {
      case -1:
         *result = 1.0 / argvals[0];
         return SCIP_OKAY;

      case 0:
         *result = 1.0;
         return SCIP_OKAY;

      case 1:
         *result = argvals[0];
         return SCIP_OKAY;

      case 2:
         *result = argvals[0] * argvals[0];
         return SCIP_OKAY;

      default:
         *result = pow(argvals[0], (SCIP_Real)opdata.intval);
   }

   return SCIP_OKAY;
} /*lint !e715*/

static
SCIP_DECL_INTEVAL( SCIPexprevalIntPowerInt )
{
   assert(result  != NULL);
   assert(argvals != NULL);

   SCIPintervalPowerScalar(infinity, result, argvals[0], (SCIP_Real)opdata.intval);

   return SCIP_OKAY;
} /*lint !e715*/

static
SCIP_DECL_EVAL( SCIPexprevalSum )
{
   int i;
   
   assert(result  != NULL);
   assert(argvals != NULL);
   
   *result = 0.0;
   for( i = 0; i < nargs; ++i )
      *result += argvals[i];
   
   return SCIP_OKAY;
} /*lint !e715*/

static
SCIP_DECL_INTEVAL( SCIPexprevalSumInt )
{
   int i;

   assert(result  != NULL);
   assert(argvals != NULL);

   SCIPintervalSet(result, 0.0);

   for( i = 0; i < nargs; ++i )
      SCIPintervalAdd(infinity, result, *result, argvals[i]);

   return SCIP_OKAY;
} /*lint !e715*/

static
SCIP_DECL_EVAL( SCIPexprevalProduct )
{
   int i;
   
   assert(result  != NULL);
   assert(argvals != NULL);
   
   *result = 1.0;
   for( i = 0; i < nargs; ++i )
      *result *= argvals[i];
   
   return SCIP_OKAY;
} /*lint !e715*/

static
SCIP_DECL_INTEVAL( SCIPexprevalProductInt )
{
   int i;

   assert(result  != NULL);
   assert(argvals != NULL);

   SCIPintervalSet(result, 1.0);

   for( i = 0; i < nargs; ++i )
      SCIPintervalMul(infinity, result, *result, argvals[i]);

   return SCIP_OKAY;
} /*lint !e715*/

static
SCIP_DECL_EVAL( SCIPexprevalLinear )
{
   SCIP_Real* coef;
   int i;

   assert(result  != NULL);
   assert(argvals != NULL || nargs == 0);
   assert(opdata.data != NULL);

   coef = &((SCIP_Real*)opdata.data)[nargs];

   *result = *coef;
   for( i = nargs-1, --coef; i >= 0; --i, --coef )
      *result += *coef * argvals[i];  /*lint !e613*/

   assert(++coef == (SCIP_Real*)opdata.data);

   return SCIP_OKAY;
} /*lint !e715*/

static
SCIP_DECL_INTEVAL( SCIPexprevalLinearInt )
{
   assert(result  != NULL);
   assert(argvals != NULL || nargs == 0);
   assert(opdata.data != NULL);

   SCIPintervalScalprodScalars(infinity, result, nargs, argvals, (SCIP_Real*)opdata.data);
   SCIPintervalAddScalar(infinity, result, *result, ((SCIP_Real*)opdata.data)[nargs]);

   return SCIP_OKAY;
} /*lint !e715*/

static
SCIP_DECL_EVAL( SCIPexprevalQuadratic )
{
   SCIP_QUADELEM* quadelems;
   int nquadelems;
   int i;

   assert(result  != NULL);
   assert(argvals != NULL || nargs == 0);
   assert(opdata.data != NULL);

   nquadelems = ((SCIP_EXPRDATA_QUADRATIC*)opdata.data)->nquadelems;
   quadelems  = ((SCIP_EXPRDATA_QUADRATIC*)opdata.data)->quadelems;
   
   assert(quadelems != NULL || nquadelems == 0);
   assert(argvals != NULL   || nquadelems == 0);
   
   *result = 0.0;
   for( i = nquadelems; i > 0 ; --i, ++quadelems )  /*lint !e613*/
      *result += quadelems->coef * argvals[quadelems->idx1] * argvals[quadelems->idx2];  /*lint !e613*/

   return SCIP_OKAY;
} /*lint !e715*/

static
SCIP_DECL_INTEVAL( SCIPexprevalQuadraticInt )
{
   SCIP_QUADELEM* quadelems;
   int nquadelems;
   int i;
   SCIP_INTERVAL tmp;

   assert(result  != NULL);
   assert(argvals != NULL || nargs == 0);
   assert(opdata.data != NULL);

   nquadelems = ((SCIP_EXPRDATA_QUADRATIC*)opdata.data)->nquadelems;
   quadelems  = ((SCIP_EXPRDATA_QUADRATIC*)opdata.data)->quadelems;
   
   assert(quadelems != NULL || nquadelems == 0);
   assert(argvals   != NULL || nquadelems == 0);
   
   SCIPintervalSet(result, 0.0);
   for( i = nquadelems; i > 0 ; --i, ++quadelems )  /*lint !e613*/
   {
      if( quadelems->idx1 == quadelems->idx2 )  /*lint !e613*/
         SCIPintervalSquare(infinity, &tmp, argvals[quadelems->idx1]);  /*lint !e613*/
      else
         SCIPintervalMul(infinity, &tmp, argvals[quadelems->idx1], argvals[quadelems->idx2]);  /*lint !e613*/
      
      if( quadelems->coef != 1.0 )  /*lint !e613*/
         SCIPintervalMulScalar(infinity, &tmp, tmp, quadelems->coef);  /*lint !e613*/
      
      SCIPintervalAdd(infinity, result, *result, tmp);
   }

   return SCIP_OKAY;
} /*lint !e715*/

static
SCIP_DECL_EVAL( SCIPexprevalPolynomial )
{
   SCIP_EXPRDATA_POLYNOMIAL* polynomialdata;
   SCIP_EXPRDATA_MONOMIAL*   monomialdata;
   SCIP_Real childval;
   SCIP_Real exponent;
   SCIP_Real monomialval;
   int i;
   int j;

   assert(result != NULL);
   assert(argvals != NULL || nargs == 0);
   assert(opdata.data != NULL);

   polynomialdata = (SCIP_EXPRDATA_POLYNOMIAL*)opdata.data;
   assert(polynomialdata != NULL);

   *result = polynomialdata->constant;

   for( i = 0; i < polynomialdata->nmonomials; ++i )
   {
      monomialdata = polynomialdata->monomials[i];
      assert(monomialdata != NULL);

      monomialval = monomialdata->coef;
      for( j = 0; j < monomialdata->nfactors; ++j )
      {
         assert(monomialdata->childidxs[j] >= 0);
         assert(monomialdata->childidxs[j] < nargs);

         childval = argvals[monomialdata->childidxs[j]];  /*lint !e613*/
         if( childval == 1.0 )  /* 1^anything == 1 */
            continue;

         exponent = monomialdata->exponents[j];

         if( childval == 0.0 )
         {
            if( exponent > 0.0 )
            {
               /* 0^positive == 0 */
               monomialval = 0.0;
               break;
            }
            else if( exponent < 0.0 )
            {
               /* 0^negative = nan */
               *result = log(-1.0);
               return SCIP_OKAY;
            }
            /* 0^0 == 1 */
            continue;
         }

         /* cover some special exponents separately to avoid calling expensive pow function */
         if( exponent == 0.0 )
            continue;
         if( exponent == 1.0 )
         {
            monomialval *= childval;
            continue;
         }
         if( exponent == 2.0 )
         {
            monomialval *= childval * childval;
            continue;
         }
         if( exponent == 0.5 )
         {
            monomialval *= sqrt(childval);
            continue;
         }
         if( exponent == -1.0 )
         {
            monomialval /= childval;
            continue;
         }
         if( exponent == -2.0 )
         {
            monomialval /= childval * childval;
            continue;
         }
         monomialval *= pow(childval, exponent);
      }

      *result += monomialval;
   }

   return SCIP_OKAY;
} /*lint !e715*/

static
SCIP_DECL_INTEVAL( SCIPexprevalPolynomialInt )
{
   SCIP_EXPRDATA_POLYNOMIAL* polynomialdata;
   SCIP_EXPRDATA_MONOMIAL*   monomialdata;
   SCIP_INTERVAL childval;
   SCIP_INTERVAL monomialval;
   SCIP_Real exponent;
   int i;
   int j;

   assert(result != NULL);
   assert(argvals != NULL || nargs == 0);
   assert(opdata.data != NULL);

   polynomialdata = (SCIP_EXPRDATA_POLYNOMIAL*)opdata.data;
   assert(polynomialdata != NULL);

   SCIPintervalSet(result, polynomialdata->constant);

   for( i = 0; i < polynomialdata->nmonomials; ++i )
   {
      monomialdata = polynomialdata->monomials[i];
      assert(monomialdata != NULL);

      SCIPintervalSet(&monomialval, monomialdata->coef);
      for( j = 0; j < monomialdata->nfactors && !SCIPintervalIsEntire(infinity, monomialval); ++j )
      {
         assert(monomialdata->childidxs[j] >= 0);
         assert(monomialdata->childidxs[j] < nargs);

         childval = argvals[monomialdata->childidxs[j]];  /*lint !e613*/

         exponent = monomialdata->exponents[j];

         /* cover some special exponents separately to avoid calling expensive pow function */
         if( exponent == 0.0 )
            continue;

         if( exponent == 1.0 )
         {
            SCIPintervalMul(infinity, &monomialval, monomialval, childval);
            continue;
         }

         if( exponent == 2.0 )
         {
            SCIPintervalSquare(infinity, &childval, childval);
            SCIPintervalMul(infinity, &monomialval, monomialval, childval);
            continue;
         }

         if( exponent == 0.5 )
         {
            SCIPintervalSquareRoot(infinity, &childval, childval);
            SCIPintervalMul(infinity, &monomialval, monomialval, childval);
            continue;
         }
         else if( exponent == -1.0 )
         {
            SCIPintervalDiv(infinity, &monomialval, monomialval, childval);
         }
         else if( exponent == -2.0 )
         {
            SCIPintervalSquare(infinity, &childval, childval);
            SCIPintervalDiv(infinity, &monomialval, monomialval, childval);
         }
         else
         {
            SCIPintervalPowerScalar(infinity, &childval, childval, exponent);
            SCIPintervalMul(infinity, &monomialval, monomialval, childval);
         }

         if( SCIPintervalIsEmpty(monomialval) )
         {
            SCIPintervalSetEmpty(result);
            return SCIP_OKAY;
         }
      }

      SCIPintervalAdd(infinity, result, *result, monomialval);
   }

   return SCIP_OKAY;
} /*lint !e715*/

/** table containing for each operand the name, the number of children, and some evaluation functions */
struct SCIPexprOpTableElement SCIPexprOpTable[] =
{
   {NULL,-1,NULL,NULL},
   { "variable",          0, SCIPexprevalPushVar,       SCIPexprevalPushVarInt       },
   { "constant",          0, SCIPexprevalPushValue,     SCIPexprevalPushValueInt     },
   { "parameter",         0, SCIPexprevalPushParameter, SCIPexprevalPushParameterInt },
   {NULL,-1,NULL,NULL},{NULL,-1,NULL,NULL},{NULL,-1,NULL,NULL},{NULL,-1,NULL,NULL},
   { "plus",              2, SCIPexprevalPlus,          SCIPexprevalPlusInt          },
   { "minus",             2, SCIPexprevalMinus,         SCIPexprevalMinusInt         },
   { "mul",               2, SCIPexprevalMult,          SCIPexprevalMultInt          },
   { "div",               2, SCIPexprevalDiv,           SCIPexprevalDivInt           },
   { "sqr",               1, SCIPexprevalSqr,           SCIPexprevalSqrInt           },
   { "sqrt",              1, SCIPexprevalSqrt,          SCIPexprevalSqrtInt          },
   { "power",             2, SCIPexprevalPower,         SCIPexprevalPowerInt         },
   { "exp",               1, SCIPexprevalExp,           SCIPexprevalExpInt           },
   { "log",               1, SCIPexprevalLog,           SCIPexprevalLogInt           },
   { "sin",               1, SCIPexprevalSin,           SCIPexprevalSinInt           },
   { "cos",               1, SCIPexprevalCos,           SCIPexprevalCosInt           },
   { "tan",               1, SCIPexprevalTan,           SCIPexprevalTanInt           },
   {NULL,-1,NULL,NULL}, /* { "erf",               1, SCIPexprevalErf,           SCIPexprevalErfInt           }, */
   {NULL,-1,NULL,NULL}, /* { "erfi",              1, SCIPexprevalErfi,          SCIPexprevalErfiInt          }, */
   { "min",               2, SCIPexprevalMin,           SCIPexprevalMinInt           },
   { "max",               2, SCIPexprevalMax,           SCIPexprevalMaxInt           },
   { "abs",               1, SCIPexprevalAbs,           SCIPexprevalAbsInt           },
   { "sign",              1, SCIPexprevalSign,          SCIPexprevalSignInt          },
   { "signpower",         2, SCIPexprevalSignPower,     SCIPexprevalSignPowerInt     },
   { "intpower",          1, SCIPexprevalIntPower,      SCIPexprevalIntPowerInt      },
   {NULL,-1,NULL,NULL},{NULL,-1,NULL,NULL},{NULL,-1,NULL,NULL},
   {NULL,-1,NULL,NULL},{NULL,-1,NULL,NULL},{NULL,-1,NULL,NULL},{NULL,-1,NULL,NULL},{NULL,-1,NULL,NULL},{NULL,-1,NULL,NULL},{NULL,-1,NULL,NULL},{NULL,-1,NULL,NULL},{NULL,-1,NULL,NULL},{NULL,-1,NULL,NULL},
   {NULL,-1,NULL,NULL},{NULL,-1,NULL,NULL},{NULL,-1,NULL,NULL},{NULL,-1,NULL,NULL},{NULL,-1,NULL,NULL},{NULL,-1,NULL,NULL},{NULL,-1,NULL,NULL},{NULL,-1,NULL,NULL},{NULL,-1,NULL,NULL},{NULL,-1,NULL,NULL},
   {NULL,-1,NULL,NULL},{NULL,-1,NULL,NULL},{NULL,-1,NULL,NULL},{NULL,-1,NULL,NULL},{NULL,-1,NULL,NULL},{NULL,-1,NULL,NULL},{NULL,-1,NULL,NULL},{NULL,-1,NULL,NULL},{NULL,-1,NULL,NULL},{NULL,-1,NULL,NULL},
   {NULL,-1,NULL,NULL},{NULL,-1,NULL,NULL},{NULL,-1,NULL,NULL},
   { "sum",              -2, SCIPexprevalSum,           SCIPexprevalSumInt           },
   { "prod",             -2, SCIPexprevalProduct,       SCIPexprevalProductInt       },
   { "linear",           -2, SCIPexprevalLinear,        SCIPexprevalLinearInt        },
   { "quadratic",        -2, SCIPexprevalQuadratic,     SCIPexprevalQuadraticInt     },
   { "polynomial",       -2, SCIPexprevalPolynomial,    SCIPexprevalPolynomialInt    }
};

/** gives the name of an operand as string */
const char* SCIPexpropGetName(
   SCIP_EXPROP           op                  /**< expression operand */
)
{
   assert(op < SCIP_EXPR_LAST);

   return SCIPexprOpTable[op].name;
}

/** gives the number of children of a simple operand */
int SCIPexpropGetNChildren(
   SCIP_EXPROP           op                  /**< expression operand */
)
{
   assert(op < SCIP_EXPR_LAST);

   return SCIPexprOpTable[op].nargs;
}

/** calculate memory size for dynamically allocated arrays (copied from scip/set.c) */
static
int calcGrowSize(
   int                   num                 /**< minimum number of entries to store */
   )
{
   int size;

   /* calculate the size with this loop, such that the resulting numbers are always the same (-> block memory) */
   size = 4;
   while( size < num )
      size = (int)(1.2 * size + 4);

   return size;
}

/** creates an expression
 * Note, that the expression is allocated but for the children only the pointer is copied.
 */
static
SCIP_RETCODE exprCreate(
   BMS_BLKMEM*           blkmem,             /**< block memory data structure */
   SCIP_EXPR**           expr,               /**< pointer to buffer for expression address */
   SCIP_EXPROP           op,                 /**< operand of expression */
   int                   nchildren,          /**< number of children */
   SCIP_EXPR**           children,           /**< children */
   SCIP_EXPROPDATA       opdata              /**< operand data */
)
{
   assert(blkmem != NULL);
   assert(expr   != NULL);
   assert(children != NULL || nchildren == 0);
   assert(children == NULL || nchildren >  0);

   SCIP_ALLOC( BMSallocBlockMemory(blkmem, expr) );

   (*expr)->op        = op;
   (*expr)->nchildren = nchildren;
   (*expr)->children  = children;
   (*expr)->data      = opdata;

   return SCIP_OKAY;
}

/** creates a simple expression */
SCIP_RETCODE SCIPexprCreate(
   BMS_BLKMEM*           blkmem,             /**< block memory data structure */
   SCIP_EXPR**           expr,               /**< pointer to buffer for expression address */
   SCIP_EXPROP           op,                 /**< operand of expression */
   ...                                       /**< arguments of operand */
)
{
   va_list         ap;
   SCIP_EXPR**     children;
   SCIP_EXPROPDATA opdata;
   
   assert(blkmem != NULL);
   assert(expr   != NULL);

   switch( op )
   {
      case SCIP_EXPR_VARIDX:
      case SCIP_EXPR_PARAM:
      {
         va_start( ap, op );  /*lint !e826*/
         opdata.intval = va_arg( ap, int );  /*lint !e416 !e826*/
         va_end( ap );  /*lint !e826*/
         
         assert( opdata.intval >= 0 );
         
         SCIP_CALL( exprCreate( blkmem, expr, op, 0, NULL, opdata ) );
         break;
      }
         
      case SCIP_EXPR_CONST:
      {
         va_start(ap, op );  /*lint !e826*/
         opdata.dbl = va_arg( ap, SCIP_Real );  /*lint !e416 !e826*/
         va_end( ap );  /*lint !e826*/
         
         SCIP_CALL( exprCreate( blkmem, expr, op, 0, NULL, opdata ) );
         break;
      }

      /* operands with two children */
      case SCIP_EXPR_PLUS     :
      case SCIP_EXPR_MINUS    :
      case SCIP_EXPR_MUL      :
      case SCIP_EXPR_DIV      :
      case SCIP_EXPR_POWER    :
      case SCIP_EXPR_MIN      :
      case SCIP_EXPR_MAX      :
      case SCIP_EXPR_SIGNPOWER:
      {
         SCIP_ALLOC( BMSallocBlockMemoryArray(blkmem, &children, 2) );
         
         va_start(ap, op );  /*lint !e826*/
         children[0] = va_arg( ap, SCIP_EXPR* );  /*lint !e416 !e826*/
         children[1] = va_arg( ap, SCIP_EXPR* );  /*lint !e416 !e826*/
         assert(children[0] != NULL);
         assert(children[1] != NULL);
         va_end( ap );  /*lint !e826*/
         opdata.data = NULL; /* to avoid compiler warning about use of uninitialised value */
         
         SCIP_CALL( exprCreate( blkmem, expr, op, 2, children, opdata ) );
         break;
      }

      /* operands with one child */
      case SCIP_EXPR_SQUARE:
      case SCIP_EXPR_SQRT  :
      case SCIP_EXPR_EXP   :
      case SCIP_EXPR_LOG   :
      case SCIP_EXPR_SIN   :
      case SCIP_EXPR_COS   :
      case SCIP_EXPR_TAN   :
      /* case SCIP_EXPR_ERF   : */
      /* case SCIP_EXPR_ERFI  : */
      case SCIP_EXPR_ABS   :
      case SCIP_EXPR_SIGN  :
      {
         SCIP_ALLOC( BMSallocBlockMemoryArray(blkmem, &children, 1) );
         
         va_start(ap, op );  /*lint !e826*/
         children[0] = va_arg( ap, SCIP_EXPR* );  /*lint !e416 !e826*/
         assert(children[0] != NULL);
         va_end( ap );  /*lint !e826*/
         opdata.data = NULL; /* to avoid compiler warning about use of uninitialised value */
         
         SCIP_CALL( exprCreate( blkmem, expr, op, 1, children, opdata ) );
         break;
      }
         
      case SCIP_EXPR_INTPOWER:
      {
         SCIP_ALLOC( BMSallocBlockMemoryArray(blkmem, &children, 1) );

         va_start(ap, op );  /*lint !e826*/
         children[0] = va_arg( ap, SCIP_EXPR* );  /*lint !e416 !e826*/
         assert(children[0] != NULL);
         opdata.intval = va_arg( ap, int);  /*lint !e416 !e826*/
         va_end( ap );  /*lint !e826*/

         SCIP_CALL( exprCreate( blkmem, expr, op, 1, children, opdata ) );
         break;
      }

      /* complex operands */
      case SCIP_EXPR_SUM    :
      case SCIP_EXPR_PRODUCT:
      {
         int nchildren;
         SCIP_EXPR** childrenarg;

         opdata.data = NULL; /* to avoid compiler warning about use of uninitialised value */

         va_start(ap, op );  /*lint !e826*/
         /* first argument should be number of children */
         nchildren = va_arg( ap, int );  /*lint !e416 !e826*/
         assert(nchildren >= 0);

         /* for a sum or product of 0 terms we can finish here */
         if( nchildren == 0 )
         {
            SCIP_CALL( exprCreate( blkmem, expr, op, 0, NULL, opdata) );
            va_end( ap );  /*lint !e826*/
            break;
         }

         /* next argument should be array of children expressions */
         childrenarg = va_arg( ap, SCIP_EXPR** );  /*lint !e416 !e826*/
         assert(childrenarg != NULL);
         va_end( ap );  /*lint !e826*/

         SCIP_ALLOC( BMSduplicateBlockMemoryArray(blkmem, &children, childrenarg, nchildren) );

         SCIP_CALL( exprCreate( blkmem, expr, op, nchildren, children, opdata) );
         break;
      }

      case SCIP_EXPR_LINEAR :
      case SCIP_EXPR_QUADRATIC:
      case SCIP_EXPR_POLYNOMIAL:
      {
         SCIPerrorMessage("cannot create complex expression linear, quadratic, or polynomial with SCIPexprCreate\n");
         return SCIP_INVALIDDATA;
      }

      case SCIP_EXPR_LAST:
      default:
         SCIPerrorMessage("unknown operand: %d\n", op);
         return SCIP_INVALIDDATA;
   }
   
   return SCIP_OKAY;
}

/** creates SCIP_EXPRDATA_QUADRATIC data structure from given quadratic elements */
static
SCIP_RETCODE quadraticdataCreate(
   BMS_BLKMEM*           blkmem,             /**< block memory data structure */
   SCIP_EXPRDATA_QUADRATIC** quadraticdata,  /**< buffer to store pointer to quadratic data */
   int                   nquadelems,         /**< number of quadratic elements */
   SCIP_QUADELEM*        quadelems           /**< quadratic elements */
   )
{
   assert(blkmem != NULL);
   assert(quadraticdata != NULL);
   assert(quadelems != NULL || nquadelems == 0);

   SCIP_ALLOC( BMSallocBlockMemory(blkmem, quadraticdata) );

   (*quadraticdata)->nquadelems = nquadelems;
   (*quadraticdata)->quadelems  = NULL;
   (*quadraticdata)->sorted     = (nquadelems <= 1);

   if( nquadelems > 0 )
   {
      SCIP_ALLOC( BMSduplicateBlockMemoryArray(blkmem, &(*quadraticdata)->quadelems, quadelems, nquadelems) );
   }

   return SCIP_OKAY;
}

/** frees SCIP_EXPRDATA_QUADRATIC data structure */
static
void quadraticdataFree(
   BMS_BLKMEM*           blkmem,             /**< block memory data structure */
   SCIP_EXPRDATA_QUADRATIC** quadraticdata   /**< buffer to store pointer to quadratic data */
   )
{
   assert(blkmem != NULL);
   assert(quadraticdata != NULL);
   assert(*quadraticdata != NULL);

   if( (*quadraticdata)->nquadelems > 0 )
   {
      assert((*quadraticdata)->quadelems != NULL);
      BMSfreeBlockMemoryArray(blkmem, &(*quadraticdata)->quadelems, (*quadraticdata)->nquadelems);
   }

   BMSfreeBlockMemory(blkmem, quadraticdata);
}

/** sorts quadratic elements in a SCIP_EXPRDATA_QUADRATIC data structure */
static
void quadraticdataSort(
   SCIP_EXPRDATA_QUADRATIC* quadraticdata    /**< quadratic data */
   )
{
   assert(quadraticdata != NULL);

   if( quadraticdata->sorted )
      return;

   if( quadraticdata->nquadelems > 0 )
      SCIPquadelemSort((void*)quadraticdata->quadelems, quadraticdata->nquadelems);

   quadraticdata->sorted = TRUE;
}

/** compares two monomials
 * gives 0 if monomials are equal */
static
SCIP_DECL_SORTPTRCOMP(monomialdataCompare)
{
   SCIP_EXPRDATA_MONOMIAL* monomial1;
   SCIP_EXPRDATA_MONOMIAL* monomial2;

   int i;

   assert(elem1 != NULL);
   assert(elem2 != NULL);

   monomial1 = (SCIP_EXPRDATA_MONOMIAL*)elem1;
   monomial2 = (SCIP_EXPRDATA_MONOMIAL*)elem2;

   /* make sure, both monomials are equal */
   SCIPexprSortMonomialFactors(monomial1);
   SCIPexprSortMonomialFactors(monomial2);

   /* for the first factor where both monomials differ,
    * we return either the difference in the child indices, if children are different
    * or the sign of the difference in the exponents
    */
   for( i = 0; i < monomial1->nfactors && i < monomial2->nfactors; ++i )
   {
      if( monomial1->childidxs[i] != monomial2->childidxs[i] )
         return monomial1->childidxs[i] - monomial2->childidxs[i];
      if( monomial1->exponents[i] > monomial2->exponents[i] )
         return 1;
      else if( monomial1->exponents[i] < monomial2->exponents[i] )
         return -1;
   }

   /* if the factors of one monomial are a proper subset of the factors of the other monomial,
    * we return the difference in the number of monomials
    */
   return monomial1->nfactors - monomial2->nfactors;
}

/** creates SCIP_EXPRDATA_POLYNOMIAL data structure from given monomials */
static
SCIP_RETCODE polynomialdataCreate(
   BMS_BLKMEM*           blkmem,             /**< block memory data structure */
   SCIP_EXPRDATA_POLYNOMIAL** polynomialdata,/**< buffer to store pointer to polynomial data */
   int                   nmonomials,         /**< number of monomials */
   SCIP_EXPRDATA_MONOMIAL** monomials,       /**< monomials */
   SCIP_Real             constant,           /**< constant part */
   SCIP_Bool             copymonomials       /**< whether to copy monomials, or copy only given pointers, in which case polynomialdata assumes ownership of monomial structure */
   )
{
   assert(blkmem != NULL);
   assert(polynomialdata != NULL);
   assert(monomials != NULL || nmonomials == 0);

   SCIP_ALLOC( BMSallocBlockMemory(blkmem, polynomialdata) );

   (*polynomialdata)->constant = constant;
   (*polynomialdata)->nmonomials  = nmonomials;
   (*polynomialdata)->monomialssize = nmonomials;
   (*polynomialdata)->monomials   = NULL;
   (*polynomialdata)->sorted   = (nmonomials <= 1);

   if( nmonomials > 0 )
   {
      int i;

      if( copymonomials )
      {
         SCIP_ALLOC( BMSallocBlockMemoryArray(blkmem, &(*polynomialdata)->monomials, nmonomials) );

         for( i = 0; i < nmonomials; ++i )
         {
            assert(monomials[i] != NULL);  /*lint !e613*/
            SCIP_CALL( SCIPexprCreateMonomial(blkmem, &(*polynomialdata)->monomials[i],
               monomials[i]->coef, monomials[i]->nfactors, monomials[i]->childidxs, monomials[i]->exponents) );  /*lint !e613*/
         }
      }
      else
      {
         SCIP_ALLOC( BMSduplicateBlockMemoryArray(blkmem, &(*polynomialdata)->monomials, monomials, nmonomials) );
      }
   }

   return SCIP_OKAY;
}

/** creates a copy of a SCIP_EXPRDATA_POLYNOMIAL data structure */
static
SCIP_RETCODE polynomialdataCopy(
   BMS_BLKMEM*           blkmem,             /**< block memory data structure */
   SCIP_EXPRDATA_POLYNOMIAL** polynomialdata,/**< buffer to store pointer to polynomial data */
   SCIP_EXPRDATA_POLYNOMIAL* sourcepolynomialdata /**< polynomial data to copy */
   )
{
   assert(blkmem != NULL);
   assert(polynomialdata != NULL);
   assert(sourcepolynomialdata != NULL);

   SCIP_ALLOC( BMSduplicateBlockMemory(blkmem, polynomialdata, sourcepolynomialdata) );

   (*polynomialdata)->monomialssize = sourcepolynomialdata->nmonomials;
   if( sourcepolynomialdata->nmonomials > 0 )
   {
      int i;

      SCIP_ALLOC( BMSallocBlockMemoryArray(blkmem, &(*polynomialdata)->monomials, (*polynomialdata)->monomialssize) );

      for( i = 0; i < sourcepolynomialdata->nmonomials; ++i )
      {
         assert(sourcepolynomialdata->monomials[i] != NULL);  /*lint !e613*/
         SCIP_CALL( SCIPexprCreateMonomial(blkmem, &(*polynomialdata)->monomials[i], sourcepolynomialdata->monomials[i]->coef,
            sourcepolynomialdata->monomials[i]->nfactors, sourcepolynomialdata->monomials[i]->childidxs, sourcepolynomialdata->monomials[i]->exponents) );
         (*polynomialdata)->monomials[i]->sorted = sourcepolynomialdata->monomials[i]->sorted;
      }
   }
   else
   {
      (*polynomialdata)->monomials = NULL;
   }

   return SCIP_OKAY;
}

/** ensures that the monomials array of a polynomial has at least a given size */
static
SCIP_RETCODE polynomialdataEnsureMonomsSize(
   BMS_BLKMEM*           blkmem,             /**< block memory data structure */
   SCIP_EXPRDATA_POLYNOMIAL* polynomialdata, /**< polynomial data */
   int                   minsize             /**< minimal size of monomials array */
   )
{
   assert(blkmem != NULL);
   assert(polynomialdata != NULL);

   if( minsize > polynomialdata->monomialssize )
   {
      int newsize;

      newsize = calcGrowSize(minsize);
      SCIP_ALLOC( BMSreallocBlockMemoryArray(blkmem, &polynomialdata->monomials, polynomialdata->monomialssize, newsize) );
      polynomialdata->monomialssize = newsize;
   }
   assert(minsize <= polynomialdata->monomialssize);

   return SCIP_OKAY;
}

/** frees a SCIP_EXPRDATA_POLYNOMIAL data structure */
static
void polynomialdataFree(
   BMS_BLKMEM*           blkmem,             /**< block memory data structure */
   SCIP_EXPRDATA_POLYNOMIAL** polynomialdata /**< pointer to polynomial data to free */
   )
{
   assert(blkmem != NULL);
   assert(polynomialdata != NULL);
   assert(*polynomialdata != NULL);

   if( (*polynomialdata)->monomialssize > 0 )
   {
      int i;

      for( i = 0; i < (*polynomialdata)->nmonomials; ++i )
      {
         assert((*polynomialdata)->monomials[i] != NULL);
         SCIPexprFreeMonomial(blkmem, &(*polynomialdata)->monomials[i]);
         assert((*polynomialdata)->monomials[i] == NULL);
      }

      BMSfreeBlockMemoryArray(blkmem, &(*polynomialdata)->monomials, (*polynomialdata)->monomialssize);
   }
   assert((*polynomialdata)->monomials == NULL);

   BMSfreeBlockMemory(blkmem, polynomialdata);
}

/** adds an array of monomials to a polynomial */
static
SCIP_RETCODE polynomialdataAddMonoms(
   BMS_BLKMEM*           blkmem,             /**< block memory of expression */
   SCIP_EXPRDATA_POLYNOMIAL* polynomialdata, /**< polynomial data */
   int                   nmonomials,         /**< number of monomials to add */
   SCIP_EXPRDATA_MONOMIAL** monomials,       /**< the monomials to add */
   SCIP_Bool             copymonomials       /**< whether to copy monomials or to assume ownership */
)
{
   int i;

   assert(blkmem != NULL);
   assert(polynomialdata != NULL);
   assert(monomials != NULL || nmonomials == 0);

   if( nmonomials == 0 )
      return SCIP_OKAY;

   SCIP_CALL( polynomialdataEnsureMonomsSize(blkmem, polynomialdata, polynomialdata->nmonomials + nmonomials) );
   assert(polynomialdata->monomialssize >= polynomialdata->nmonomials + nmonomials);

   if( copymonomials )
   {
      for( i = 0; i < nmonomials; ++i )
      {
         assert(monomials[i] != NULL);  /*lint !e613*/
         SCIP_CALL( SCIPexprCreateMonomial(blkmem, &polynomialdata->monomials[polynomialdata->nmonomials + i],
            monomials[i]->coef, monomials[i]->nfactors, monomials[i]->childidxs, monomials[i]->exponents) );  /*lint !e613*/
      }
   }
   else
   {
      BMScopyMemoryArray(&polynomialdata->monomials[polynomialdata->nmonomials], monomials, nmonomials);
   }
   polynomialdata->nmonomials += nmonomials;

   polynomialdata->sorted = (polynomialdata->nmonomials <= 1);

   return SCIP_OKAY;
}

/** ensures that monomials of a polynomial are sorted */
static
void polynomialdataSortMonoms(
   SCIP_EXPRDATA_POLYNOMIAL* polynomialdata  /**< polynomial expression */
)
{
   assert(polynomialdata != NULL);

   if( polynomialdata->sorted )
      return;

   if( polynomialdata->nmonomials > 0 )
      SCIPsortPtr((void*)polynomialdata->monomials, monomialdataCompare, polynomialdata->nmonomials);

   polynomialdata->sorted = TRUE;
}

/** copies an expression including its children */
SCIP_RETCODE SCIPexprCopyDeep(
   BMS_BLKMEM*           blkmem,             /**< block memory data structure */
   SCIP_EXPR**           targetexpr,         /**< buffer to store pointer to copied expression */
   SCIP_EXPR*            sourceexpr          /**< expression to copy */
)
{
   assert(blkmem     != NULL);
   assert(targetexpr != NULL);
   assert(sourceexpr != NULL);

   SCIP_ALLOC( BMSduplicateBlockMemory(blkmem, targetexpr, sourceexpr) );
   
   if( sourceexpr->nchildren )
   {
      int i;
      
      /* alloc memory for children expressions */
      SCIP_ALLOC( BMSallocBlockMemoryArray(blkmem, &(*targetexpr)->children, sourceexpr->nchildren) );

      /* copy children expressions */
      for( i = 0; i < sourceexpr->nchildren; ++i )
      {
         SCIP_CALL( SCIPexprCopyDeep(blkmem, &(*targetexpr)->children[i], sourceexpr->children[i]) );
      }
   }
   else
   {
      assert((*targetexpr)->children == NULL); /* otherwise, sourceexpr->children was not NULL, which is wrong */
   }

   /* copy data for more complex operands
    * for simple operands BMSduplicate above should have done the job */
   switch( sourceexpr->op )
   {
      case SCIP_EXPR_LINEAR:
      {
         SCIP_Real* targetdata;

         /* for a linear expression, we need to copy the array that holds the coefficients and constant term */
         assert(sourceexpr->data.data != NULL);
         SCIP_ALLOC( BMSduplicateBlockMemoryArray(blkmem, &targetdata, (SCIP_Real*) sourceexpr->data.data, sourceexpr->nchildren + 1) );  /*lint !e866*/
         (*targetexpr)->data.data = (void*) targetdata;

         break;
      }
      
      case SCIP_EXPR_QUADRATIC:
      {
         SCIP_EXPRDATA_QUADRATIC* sourcedata;
         
         sourcedata = (SCIP_EXPRDATA_QUADRATIC*)sourceexpr->data.data;
         assert(sourcedata != NULL);
         
         SCIP_CALL( quadraticdataCreate(blkmem, (SCIP_EXPRDATA_QUADRATIC**)&(*targetexpr)->data.data,
            sourcedata->nquadelems, sourcedata->quadelems) );
         break;
      }

      case SCIP_EXPR_POLYNOMIAL:
      {
         SCIP_EXPRDATA_POLYNOMIAL* sourcedata;
         SCIP_EXPRDATA_POLYNOMIAL* targetdata;

         sourcedata = (SCIP_EXPRDATA_POLYNOMIAL*)sourceexpr->data.data;
         assert(sourcedata != NULL);

         SCIP_CALL( polynomialdataCopy(blkmem, &targetdata, sourcedata) );

         (*targetexpr)->data.data = (void*)targetdata;

         break;
      }

     default: ;
   }  /*lint !e788*/

   return SCIP_OKAY;
}

/** frees an expression including its children */
void SCIPexprFreeDeep(
   BMS_BLKMEM*           blkmem,             /**< block memory data structure */
   SCIP_EXPR**           expr                /**< pointer to expression to free */
   )
{
   assert(blkmem != NULL);
   assert(expr   != NULL);
   assert(*expr  != NULL);
   
   /* free data of more complex operands */
   switch( (*expr)->op )
   {
      case SCIP_EXPR_LINEAR:
      {
         SCIP_Real* freedata;

         freedata = (SCIP_Real*)(*expr)->data.data;
         assert(freedata != NULL);

         BMSfreeBlockMemoryArray(blkmem, &freedata, (*expr)->nchildren + 1);  /*lint !e866*/
         break;
      }
      
      case SCIP_EXPR_QUADRATIC:
      {
         assert((*expr)->data.data != NULL);
         quadraticdataFree(blkmem, (SCIP_EXPRDATA_QUADRATIC**)&(*expr)->data.data);
         break;
      }

      case SCIP_EXPR_POLYNOMIAL:
      {
         assert((*expr)->data.data != NULL);
         polynomialdataFree(blkmem, (SCIP_EXPRDATA_POLYNOMIAL**)&(*expr)->data.data);
         break;
      }

      default: ;
   }  /*lint !e788*/

   if( (*expr)->nchildren )
   {
      int i;
      
      assert( (*expr)->children != NULL );
      
      for( i = 0; i < (*expr)->nchildren; ++i )
      {
         SCIPexprFreeDeep(blkmem, &(*expr)->children[i]);
         assert((*expr)->children[i] == NULL);
      }

      BMSfreeBlockMemoryArray(blkmem, &(*expr)->children, (*expr)->nchildren);
   }
   else
   {
      assert( (*expr)->children == NULL );
   }
   
   BMSfreeBlockMemory(blkmem, expr);
}

/** gives operator of expression */
SCIP_EXPROP SCIPexprGetOperator(
   SCIP_EXPR*            expr                /**< expression */
)
{
   assert(expr != NULL);
   
   return expr->op;
}

/** gives number of children of an expression */
int SCIPexprGetNChildren(
   SCIP_EXPR*            expr                /**< expression */
)
{
   assert(expr != NULL);
   
   return expr->nchildren;
}

/** gives pointer to array with children of an expression */
SCIP_EXPR** SCIPexprGetChildren(
   SCIP_EXPR*            expr                /**< expression */
)
{
   assert(expr != NULL);
   
   return expr->children;
}

/** gives index belonging to a SCIP_EXPR_VARIDX or SCIP_EXPR_PARAM operand */
int SCIPexprGetOpIndex(
   SCIP_EXPR*            expr                /**< expression */
)
{
   assert(expr != NULL);
   assert(expr->op == SCIP_EXPR_VARIDX || expr->op == SCIP_EXPR_PARAM);
   
   return expr->data.intval;
}

/** gives real belonging to a SCIP_EXPR_CONST operand */ 
SCIP_Real SCIPexprGetOpReal(
   SCIP_EXPR* expr                           /**< expression */
)
{
   assert(expr != NULL);
   assert(expr->op == SCIP_EXPR_CONST);
   
   return expr->data.dbl;
}

/** gives void* belonging to a complex operand */
void* SCIPexprGetOpData(
   SCIP_EXPR*            expr                /**< expression */
)
{
   assert(expr != NULL);
   assert(expr->op >= SCIP_EXPR_SUM); /* only complex operands store their data as void* */
   
   return expr->data.data;
}

/** gives exponent belonging to a SCIP_EXPR_INTPOWER expression */
int SCIPexprGetIntPowerExponent(
   SCIP_EXPR*            expr                /**< expression */
)
{
   assert(expr != NULL);
   assert(expr->op == SCIP_EXPR_INTPOWER);

   return expr->data.intval;
}

/** creates a SCIP_EXPR_LINEAR expression that is (affine) linear in its children: constant + sum_i coef_i child_i */
SCIP_RETCODE SCIPexprCreateLinear(
   BMS_BLKMEM*           blkmem,             /**< block memory data structure */
   SCIP_EXPR**           expr,               /**< pointer to buffer for expression address */
   int                   nchildren,          /**< number of children */
   SCIP_EXPR**           children,           /**< children of expression */
   SCIP_Real*            coefs,              /**< coefficients of children */
   SCIP_Real             constant            /**< constant part */
)
{
   SCIP_EXPROPDATA opdata;
   SCIP_EXPR**     childrencopy;
   SCIP_Real*      data;

   assert(nchildren >= 0);
   assert(children != NULL || nchildren == 0);
   assert(coefs    != NULL || nchildren == 0);

   if( nchildren > 0 )
   {
      SCIP_ALLOC( BMSduplicateBlockMemoryArray(blkmem, &childrencopy, children, nchildren) );
   }
   else
      childrencopy = NULL;

   /* we store the coefficients and the constant in a single array and make this our operand data */
   SCIP_ALLOC( BMSallocBlockMemoryArray(blkmem, &data, nchildren + 1) );
   BMScopyMemoryArray(data, coefs, nchildren);
   data[nchildren] = constant;

   opdata.data = (void*)data;

   SCIP_CALL( exprCreate( blkmem, expr, SCIP_EXPR_LINEAR, nchildren, childrencopy, opdata) );

   return SCIP_OKAY;
}

/** gives linear coefficients belonging to a SCIP_EXPR_LINEAR expression */
SCIP_Real* SCIPexprGetLinearCoefs(
   SCIP_EXPR*            expr                /**< expression */
)
{
   assert(expr != NULL);
   assert(expr->op == SCIP_EXPR_LINEAR);
   assert(expr->data.data != NULL);

   /* the coefficients are stored in the first nchildren elements of the array stored as expression data */
   return (SCIP_Real*)expr->data.data;
}

/** gives constant belonging to a SCIP_EXPR_LINEAR expression */
SCIP_Real SCIPexprGetLinearConstant(
   SCIP_EXPR*            expr                /**< expression */
)
{
   assert(expr != NULL);
   assert(expr->op == SCIP_EXPR_LINEAR);
   assert(expr->data.data != NULL);

   /* the constant is stored in the nchildren's element of the array stored as expression data */
   return ((SCIP_Real*)expr->data.data)[expr->nchildren];
}

/** creates a SCIP_EXPR_QUADRATIC expression: sum_i coef_i child1_i child2_i */
SCIP_RETCODE SCIPexprCreateQuadratic(
   BMS_BLKMEM*           blkmem,             /**< block memory data structure */
   SCIP_EXPR**           expr,               /**< pointer to buffer for expression address */
   int                   nchildren,          /**< number of children */
   SCIP_EXPR**           children,           /**< children of expression */
   int                   nquadelems,         /**< number of quadratic elements */
   SCIP_QUADELEM*        quadelems           /**< quadratic elements specifying coefficients and child indices */
)
{
   SCIP_EXPROPDATA opdata;
   SCIP_EXPR**     childrencopy;
   SCIP_EXPRDATA_QUADRATIC* data;

   assert(nchildren >= 0);
   assert(children  != NULL || nchildren == 0);
   assert(quadelems != NULL || nquadelems == 0);

   if( nchildren > 0 )
   {
      SCIP_ALLOC( BMSduplicateBlockMemoryArray(blkmem, &childrencopy, children, nchildren) );
   }
   else
      childrencopy = NULL;

   SCIP_CALL( quadraticdataCreate(blkmem, &data, nquadelems, quadelems) );

   opdata.data = (void*)data;

   SCIP_CALL( exprCreate( blkmem, expr, SCIP_EXPR_QUADRATIC, nchildren, childrencopy, opdata) );

   return SCIP_OKAY;
}

/** gives quadratic elements belonging to a SCIP_EXPR_QUADRATIC expression */
SCIP_QUADELEM* SCIPexprGetQuadElements(
   SCIP_EXPR*            expr                /**< quadratic expression */
)
{
   assert(expr != NULL);
   assert(expr->op == SCIP_EXPR_QUADRATIC);
   assert(expr->data.data != NULL);

   return ((SCIP_EXPRDATA_QUADRATIC*)expr->data.data)->quadelems;
}

/** gives number of quadratic elements belonging to a SCIP_EXPR_QUADRATIC expression */
int SCIPexprGetNQuadElements(
   SCIP_EXPR*            expr                /**< quadratic expression */
)
{
   assert(expr != NULL);
   assert(expr->op == SCIP_EXPR_QUADRATIC);
   assert(expr->data.data != NULL);

   return ((SCIP_EXPRDATA_QUADRATIC*)expr->data.data)->nquadelems;
}

/** ensures that quadratic elements of a quadratic expression are sorted */
void SCIPexprSortQuadElems(
   SCIP_EXPR*            expr                /**< quadratic expression */
)
{
   assert(expr != NULL);
   assert(expr->op == SCIP_EXPR_QUADRATIC);
   assert(expr->data.data != NULL);

   quadraticdataSort((SCIP_EXPRDATA_QUADRATIC*)expr->data.data);
}

/** creates a SCIP_EXPR_POLYNOMIAL expression from an array of monomials: constant + sum_i monomial_i */
SCIP_RETCODE SCIPexprCreatePolynomial(
   BMS_BLKMEM*           blkmem,             /**< block memory data structure */
   SCIP_EXPR**           expr,               /**< pointer to buffer for expression address */
   int                   nchildren,          /**< number of children */
   SCIP_EXPR**           children,           /**< children of expression */
   int                   nmonomials,         /**< number of monomials */
   SCIP_EXPRDATA_MONOMIAL** monomials,       /**< monomials */
   SCIP_Real             constant,           /**< constant part */
   SCIP_Bool             copymonomials       /**< should monomials by copied or ownership be assumed? */
)
{
   SCIP_EXPROPDATA opdata;
   SCIP_EXPR**     childrencopy;
   SCIP_EXPRDATA_POLYNOMIAL* data;

   assert(nchildren >= 0);
   assert(children != NULL || nchildren == 0);
   assert(monomials   != NULL || nmonomials   == 0);

   if( nchildren > 0 )
   {
      SCIP_ALLOC( BMSduplicateBlockMemoryArray(blkmem, &childrencopy, children, nchildren) );
   }
   else
      childrencopy = NULL;

   SCIP_CALL( polynomialdataCreate(blkmem, &data, nmonomials, monomials, constant, copymonomials) );
   opdata.data = (void*)data;

   SCIP_CALL( exprCreate( blkmem, expr, SCIP_EXPR_POLYNOMIAL, nchildren, childrencopy, opdata) );

   return SCIP_OKAY;
}

/** gives the monomials belonging to a SCIP_EXPR_POLYNOMIAL expression */
SCIP_EXPRDATA_MONOMIAL** SCIPexprGetMonomials(
   SCIP_EXPR*            expr                /**< expression */
)
{
   assert(expr != NULL);
   assert(expr->op == SCIP_EXPR_POLYNOMIAL);
   assert(expr->data.data != NULL);

   return ((SCIP_EXPRDATA_POLYNOMIAL*)expr->data.data)->monomials;
}

/** gives the number of monomials belonging to a SCIP_EXPR_POLYNOMIAL expression */
int SCIPexprGetNMonomials(
   SCIP_EXPR*            expr                /**< expression */
)
{
   assert(expr != NULL);
   assert(expr->op == SCIP_EXPR_POLYNOMIAL);
   assert(expr->data.data != NULL);

   return ((SCIP_EXPRDATA_POLYNOMIAL*)expr->data.data)->nmonomials;
}

/** gives the constant belonging to a SCIP_EXPR_POLYNOMIAL expression */
SCIP_Real SCIPexprGetPolynomialConstant(
   SCIP_EXPR*            expr                /**< expression */
)
{
   assert(expr != NULL);
   assert(expr->op == SCIP_EXPR_POLYNOMIAL);
   assert(expr->data.data != NULL);

   return ((SCIP_EXPRDATA_POLYNOMIAL*)expr->data.data)->constant;
}

/** adds an array of monomials to a SCIP_EXPR_POLYNOMIAL expression */
SCIP_RETCODE SCIPexprAddMonomials(
   BMS_BLKMEM*           blkmem,             /**< block memory of expression */
   SCIP_EXPR*            expr,               /**< expression */
   int                   nmonomials,         /**< number of monomials to add */
   SCIP_EXPRDATA_MONOMIAL** monomials,       /**< the monomials to add */
   SCIP_Bool             copymonomials       /**< should monomials by copied or ownership be assumed? */
)
{
   assert(blkmem != NULL);
   assert(expr != NULL);
   assert(expr->op == SCIP_EXPR_POLYNOMIAL);
   assert(monomials != NULL || nmonomials == 0);

   if( nmonomials == 0 )
      return SCIP_OKAY;

   SCIP_CALL( polynomialdataAddMonoms(blkmem, (SCIP_EXPRDATA_POLYNOMIAL*)expr->data.data, nmonomials, monomials, copymonomials) );

   return SCIP_OKAY;
}

/** changes the constant in a SCIP_EXPR_POLYNOMIAL expression */
void SCIPexprChgPolynomialConstant(
   SCIP_EXPR*            expr,               /**< expression */
   SCIP_Real             constant            /**< new value for constant */
)
{
   assert(expr != NULL);
   assert(expr->op == SCIP_EXPR_POLYNOMIAL);
   assert(expr->data.data != NULL);

   ((SCIP_EXPRDATA_POLYNOMIAL*)expr->data.data)->constant = constant;
}

/** ensures that monomials of a polynomial are sorted */
void SCIPexprSortMonomials(
   SCIP_EXPR*            expr                /**< polynomial expression */
)
{
   assert(expr != NULL);
   assert(expr->op == SCIP_EXPR_POLYNOMIAL);
   assert(expr->data.data != NULL);

   polynomialdataSortMonoms((SCIP_EXPRDATA_POLYNOMIAL*)expr->data.data);
}

/** creates a monomial */
SCIP_RETCODE SCIPexprCreateMonomial(
   BMS_BLKMEM*           blkmem,             /**< block memory */
   SCIP_EXPRDATA_MONOMIAL** monomial,        /**< buffer where to store pointer to new monomial */
   SCIP_Real             coef,               /**< coefficient of monomial */
   int                   nfactors,           /**< number of factors in monomial */
   int*                  childidxs,          /**< indices of children corresponding to factors */
   SCIP_Real*            exponents           /**< exponent in each factor */
)
{
   assert(blkmem != NULL);
   assert(monomial  != NULL);
   assert(childidxs != NULL || nfactors == 0);
   assert(exponents != NULL || nfactors == 0);

   SCIP_ALLOC( BMSallocBlockMemory(blkmem, monomial) );

   (*monomial)->coef     = coef;
   (*monomial)->nfactors = nfactors;
   (*monomial)->factorssize = nfactors;
   (*monomial)->sorted = (nfactors <= 1);

   if( nfactors > 0 )
   {
      SCIP_ALLOC( BMSduplicateBlockMemoryArray(blkmem, &(*monomial)->childidxs, childidxs, nfactors) );
      SCIP_ALLOC( BMSduplicateBlockMemoryArray(blkmem, &(*monomial)->exponents, exponents, nfactors) );
   }
   else
   {
      (*monomial)->childidxs = NULL;
      (*monomial)->exponents = NULL;
   }

   return SCIP_OKAY;
}

/** frees a monomial */
void SCIPexprFreeMonomial(
   BMS_BLKMEM*           blkmem,             /**< block memory */
   SCIP_EXPRDATA_MONOMIAL** monomial         /**< pointer to monomial that should be freed */
)
{
   assert(blkmem != NULL);
   assert( monomial != NULL);
   assert(*monomial != NULL);

   if( (*monomial)->factorssize > 0 )
   {
      assert((*monomial)->childidxs != NULL);
      assert((*monomial)->exponents != NULL);

      BMSfreeBlockMemoryArray(blkmem, &(*monomial)->childidxs, (*monomial)->factorssize);
      BMSfreeBlockMemoryArray(blkmem, &(*monomial)->exponents, (*monomial)->factorssize);
   }
   assert((*monomial)->childidxs == NULL);
   assert((*monomial)->exponents == NULL);

   BMSfreeBlockMemory(blkmem, monomial);
}

/** gets coefficient of a monomial */
SCIP_Real SCIPexprGetMonomialCoef(
   SCIP_EXPRDATA_MONOMIAL* monomial          /**< monomial */
)
{
   assert(monomial != NULL);

   return monomial->coef;
}

/** gets number of factors of a monomial */
int SCIPexprGetMonomialNFactors(
   SCIP_EXPRDATA_MONOMIAL* monomial          /**< monomial */
)
{
   assert(monomial != NULL);

   return monomial->nfactors;
}

/** gets indices of children corresponding to factors of a monomial */
int* SCIPexprGetMonomialChildIndices(
   SCIP_EXPRDATA_MONOMIAL* monomial          /**< monomial */
)
{
   assert(monomial != NULL);

   return monomial->childidxs;
}

/** gets exponents in factors of a monomial */
SCIP_Real* SCIPexprGetMonomialExponents(
   SCIP_EXPRDATA_MONOMIAL* monomial          /**< monomial */
)
{
   assert(monomial != NULL);

   return monomial->exponents;
}

/** ensures that factors in a monomial are sorted */
void SCIPexprSortMonomialFactors(
   SCIP_EXPRDATA_MONOMIAL* monomial          /**< monomial */
   )
{
   assert(monomial != NULL);

   if( monomial->sorted )
      return;

   if( monomial->nfactors > 0 )
      SCIPsortIntReal(monomial->childidxs, monomial->exponents, monomial->nfactors);

   monomial->sorted = TRUE;
}

/** finds a factor corresponding to a given child index in a monomial
 * note that if the factors have not been merged, the position of some factor corresponding to a given child is given
 * returns TRUE if a factor is found, FALSE if not
 */
SCIP_Bool SCIPexprFindMonomialFactor(
   SCIP_EXPRDATA_MONOMIAL* monomial,         /**< monomial */
   int                   childidx,           /**< index of the child which factor to search for */
   int*                  pos                 /**< buffer to store position of factor */
)
{
   assert(monomial != NULL);

   if( monomial->nfactors == 0 )
      return FALSE;

   SCIPexprSortMonomialFactors(monomial);

   return SCIPsortedvecFindInt(monomial->childidxs, childidx, monomial->nfactors, pos);
}

/** indicates whether the expression contains a SCIP_EXPR_PARAM */
SCIP_Bool SCIPexprHasParam(
   SCIP_EXPR*            expr                /**< expression */
)
{
   int i;

   assert(expr != NULL);

   if( expr->op == SCIP_EXPR_PARAM )
      return TRUE;

   for( i = 0; i < expr->nchildren; ++i )
      if( SCIPexprHasParam(expr->children[i]) )
         return TRUE;

   return FALSE;
}

/** gets maximal degree of expression, or SCIP_EXPR_DEGREEINFINITY if not a polynomial */
SCIP_RETCODE SCIPexprGetMaxDegree(
   SCIP_EXPR*            expr,               /**< expression */
   int*                  maxdegree           /**< buffer to store maximal degree */
)
{
   int child1;
   int child2;

   assert(expr      != NULL);
   assert(maxdegree != NULL);

   switch( expr->op )
   {
      case SCIP_EXPR_VARIDX:
         *maxdegree = 1;
         break;

      case SCIP_EXPR_CONST:         
      case SCIP_EXPR_PARAM:
         *maxdegree = 0;
         break;

      case SCIP_EXPR_PLUS:
      case SCIP_EXPR_MINUS:
      {
         assert(expr->children[0] != NULL);
         assert(expr->children[1] != NULL);
         
         SCIP_CALL( SCIPexprGetMaxDegree(expr->children[0], &child1) );
         SCIP_CALL( SCIPexprGetMaxDegree(expr->children[1], &child2) );
         
         *maxdegree = MAX(child1, child2);
         break;
      }

      case SCIP_EXPR_MUL:
      {
         assert(expr->children[0] != NULL);
         assert(expr->children[1] != NULL);
         
         SCIP_CALL( SCIPexprGetMaxDegree(expr->children[0], &child1) );
         SCIP_CALL( SCIPexprGetMaxDegree(expr->children[1], &child2) );
         
         *maxdegree = child1 + child2;
         break;
      }

      case SCIP_EXPR_DIV:
      {
         assert(expr->children[0] != NULL);
         assert(expr->children[1] != NULL);
         
         SCIP_CALL( SCIPexprGetMaxDegree(expr->children[0], &child1) );
         SCIP_CALL( SCIPexprGetMaxDegree(expr->children[1], &child2) );
         
         /* if not division by constant, then it is not a polynomial */
         *maxdegree = (child2 != 0) ? SCIP_EXPR_DEGREEINFINITY : child1;
         break;
      }

      case SCIP_EXPR_SQUARE:
      {
         assert(expr->children[0] != NULL);
         
         SCIP_CALL( SCIPexprGetMaxDegree(expr->children[0], &child1) );
         
         *maxdegree = 2 * child1;
         break;
      }

      case SCIP_EXPR_SQRT:
      {
         assert(expr->children[0] != NULL);
         
         SCIP_CALL( SCIPexprGetMaxDegree(expr->children[0], &child1) );
         
         /* if not squareroot of constant, then no polynomial */
         *maxdegree = (child1 != 0) ? SCIP_EXPR_DEGREEINFINITY : 0;
         break;
      }

      case SCIP_EXPR_POWER:
      {
         SCIP_Real val;
         
         assert(expr->children[0] != NULL);
         assert(expr->children[1] != NULL);

         SCIP_CALL( SCIPexprGetMaxDegree(expr->children[0], &child1) );
         SCIP_CALL( SCIPexprGetMaxDegree(expr->children[1], &child2) );

         /* constant ^ constant has degree 0 */
         if( child1 == 0 && child2 == 0 )
         {
            *maxdegree = 0;
            break;
         }
         
         /* non-polynomial ^ non-constant is not a polynomial */
         if( child1 >= SCIP_EXPR_DEGREEINFINITY || child2 > 0 || SCIPexprHasParam(expr->children[1]) )
         {
            *maxdegree = SCIP_EXPR_DEGREEINFINITY;
            break;
         }

         /* so it is polynomial ^ constant
          * let's see whether the constant is integral */
         SCIP_CALL( SCIPexprEval(expr->children[1], NULL, NULL, &val) );

         if( val == 0.0 ) /* polynomial ^ 0 == 0 */
            *maxdegree = 0;
         else if( val > 0.0 && floor(val) == val ) /* natural exponent gives polynomial again */  /*lint !e777*/
            *maxdegree = child1 * (int)floor(val);
         else /* negative or nonintegral exponent does not give polynomial */
            *maxdegree = SCIP_EXPR_DEGREEINFINITY;

         break;
      }

      case SCIP_EXPR_EXP:
      case SCIP_EXPR_LOG:
      case SCIP_EXPR_SIN:
      case SCIP_EXPR_COS:
      case SCIP_EXPR_TAN:
      /* case SCIP_EXPR_ERF: */
      /* case SCIP_EXPR_ERFI: */
      case SCIP_EXPR_ABS:
      case SCIP_EXPR_SIGN:
      {
         assert(expr->children[0] != NULL);
         
         SCIP_CALL( SCIPexprGetMaxDegree(expr->children[0], &child1) );
         
         /* if argument is not a constant, then no polynomial, otherwise it is a constant */
         *maxdegree = (child1 != 0) ? SCIP_EXPR_DEGREEINFINITY : 0;
         break;
      }

      case SCIP_EXPR_MIN:
      case SCIP_EXPR_MAX:
      case SCIP_EXPR_SIGNPOWER:
      {
         assert(expr->children[0] != NULL);
         assert(expr->children[1] != NULL);

         SCIP_CALL( SCIPexprGetMaxDegree(expr->children[0], &child1) );
         SCIP_CALL( SCIPexprGetMaxDegree(expr->children[1], &child2) );

         /* if any of the operands is not constant, then it is no polynomial */
         *maxdegree = (child1 != 0 || child2 != 0) ? SCIP_EXPR_DEGREEINFINITY : 0;
         break;
      }

      case SCIP_EXPR_INTPOWER:
      {
         assert(expr->children[0] != NULL);

         SCIP_CALL( SCIPexprGetMaxDegree(expr->children[0], &child1) );

         /* constant ^ integer or something ^ 0 has degree 0 */
         if( child1 == 0 || expr->data.intval == 0 )
         {
            *maxdegree = 0;
            break;
         }

         /* non-polynomial ^ integer  or  something ^ negative  is not a polynomial */
         if( child1 >= SCIP_EXPR_DEGREEINFINITY || expr->data.intval < 0 )
         {
            *maxdegree = SCIP_EXPR_DEGREEINFINITY;
            break;
         }

         /* so it is polynomial ^ natural, which gives a polynomial again */
         *maxdegree = child1 * expr->data.intval;

         break;
      }

      case SCIP_EXPR_SUM:
      case SCIP_EXPR_LINEAR:
      {
         int i;

         *maxdegree = 0;
         for( i = 0; i < expr->nchildren && *maxdegree < SCIP_EXPR_DEGREEINFINITY; ++i )
         {
            SCIP_CALL( SCIPexprGetMaxDegree(expr->children[i], &child1) );
            if( child1 > *maxdegree )
               *maxdegree = child1;
         }

         break;
      }

      case SCIP_EXPR_PRODUCT:
      {
         int i;

         *maxdegree = 0;
         for( i = 0; i < expr->nchildren; ++i )
         {
            SCIP_CALL( SCIPexprGetMaxDegree(expr->children[i], &child1) );
            if( child1 >= SCIP_EXPR_DEGREEINFINITY )
            {
               *maxdegree = SCIP_EXPR_DEGREEINFINITY;
               break;
            }
            *maxdegree += child1;
         }

         break;
      }
      
      case SCIP_EXPR_QUADRATIC:
      {
         SCIP_EXPRDATA_QUADRATIC* quadraticdata;
         int i;
         
         quadraticdata = (SCIP_EXPRDATA_QUADRATIC*)expr->data.data;

         *maxdegree = 0;
         for( i = 0; i < quadraticdata->nquadelems; ++i )
         {
            /* compute degree of both children */
            SCIP_CALL( SCIPexprGetMaxDegree(expr->children[quadraticdata->quadelems[i].idx1], &child1) );
            if( quadraticdata->quadelems[i].idx1 != quadraticdata->quadelems[i].idx2 )
            {
               SCIP_CALL( SCIPexprGetMaxDegree(expr->children[quadraticdata->quadelems[i].idx1], &child2) );
            }
            else
            {
               child2 = child1;
            }
            
            if( child1 == SCIP_EXPR_DEGREEINFINITY || child2 == SCIP_EXPR_DEGREEINFINITY )
            {
               *maxdegree = SCIP_EXPR_DEGREEINFINITY;
               break;
            }
            
            /* degree of term is sum of degrees of children */
            if( child1 + child2 > *maxdegree )
               *maxdegree = child1 + child2;
         }

         break;
      }

      case SCIP_EXPR_POLYNOMIAL:
      {
         SCIP_EXPRDATA_POLYNOMIAL* polynomialdata;
         SCIP_EXPRDATA_MONOMIAL* monomialdata;
         int monomialdegree;
         int i;
         int j;

         polynomialdata = (SCIP_EXPRDATA_POLYNOMIAL*)expr->data.data;

         *maxdegree = 0;
         for( i = 0; i < polynomialdata->nmonomials && *maxdegree < SCIP_EXPR_DEGREEINFINITY; ++i )
         {
            monomialdata = polynomialdata->monomials[i];
            assert(monomialdata != NULL);

            /* compute degree of monomial = sum of degree of factors */
            monomialdegree = 0;
            for( j = 0; j < monomialdata->nfactors; ++j )
            {
               SCIP_CALL( SCIPexprGetMaxDegree(expr->children[monomialdata->childidxs[j]], &child1) );

               /* if the exponent of the factor is not a natural number and the child is not constant (degree 0),
                * then we report that we are not really a polynomial */
               if( child1 != 0 && (monomialdata->exponents[j] < 0.0 || (int)monomialdata->exponents[j] != monomialdata->exponents[j]) )
               {
                  *maxdegree = SCIP_EXPR_DEGREEINFINITY;
                  break;
               }

               monomialdegree += child1 * (int)monomialdata->exponents[j];
            }

            if( monomialdegree > *maxdegree )
               *maxdegree = monomialdegree;
         }

         break;
      }

      case SCIP_EXPR_LAST:
      default:
         SCIPerrorMessage("unknown operand: %d\n", expr->op);
         return SCIP_ERROR;
   }

   return SCIP_OKAY;
}

/** evaluates an expression w.r.t. a point */
SCIP_RETCODE SCIPexprEval(
   SCIP_EXPR*            expr,               /**< expression */
   SCIP_Real*            varvals,            /**< values for variables, can be NULL if the expression is constant */
   SCIP_Real*            param,              /**< values for parameters, can be NULL if the expression is not parameterized */
   SCIP_Real*            val                 /**< buffer to store value */
)
{
   int i;
   SCIP_Real  staticbuf[SCIP_EXPRESSION_MAXCHILDEST];
   SCIP_Real* buf;

   /* if many children, get large enough memory to store argument values */
   if( expr->nchildren > SCIP_EXPRESSION_MAXCHILDEST )
   {
      SCIP_ALLOC( BMSallocMemoryArray(&buf, expr->nchildren) );
   }
   else
   {
      buf = staticbuf;
   }

   /* evaluate children */
   for( i = 0; i < expr->nchildren; ++i )
   {
      SCIP_CALL( SCIPexprEval(expr->children[i], varvals, param, &buf[i]) );
   }

   /* evaluate this expression */
   assert( SCIPexprOpTable[expr->op].eval != NULL );
   SCIP_CALL( SCIPexprOpTable[expr->op].eval(expr->data, expr->nchildren, buf, varvals, param, val) );

   /* free memory, if allocated before */
   if( staticbuf != buf )
   {
      BMSfreeMemoryArray(&buf);
   }

   return SCIP_OKAY;
}

/** evaluates an expression w.r.t. an interval */
SCIP_RETCODE SCIPexprEvalInt(
   SCIP_EXPR*            expr,               /**< expression */
   SCIP_Real             infinity,           /**< value to use for infinity */
   SCIP_INTERVAL*        varvals,            /**< interval values for variables, can be NULL if the expression is constant */
   SCIP_Real*            param,              /**< values for parameters, can be NULL if the expression is not parameterized */
   SCIP_INTERVAL*        val                 /**< buffer to store value */
)
{
   int i;
   SCIP_INTERVAL  staticbuf[SCIP_EXPRESSION_MAXCHILDEST];
   SCIP_INTERVAL* buf;

   /* if many children, get large enough memory to store argument values */
   if( expr->nchildren > SCIP_EXPRESSION_MAXCHILDEST )
   {
      SCIP_ALLOC( BMSallocMemoryArray(&buf, expr->nchildren) );
   }
   else
   {
      buf = staticbuf;
   }

   /* evaluate children */
   for( i = 0; i < expr->nchildren; ++i )
   {
      SCIP_CALL( SCIPexprEvalInt(expr->children[i], infinity, varvals, param, &buf[i]) );
   }

   /* evaluate this expression */
   assert( SCIPexprOpTable[expr->op].inteval != NULL );
   SCIP_CALL( SCIPexprOpTable[expr->op].inteval(infinity, expr->data, expr->nchildren, buf, varvals, param, val) );

   /* free memory, if allocated before */
   if( staticbuf != buf )
   {
      BMSfreeMemoryArray(&buf);
   }

   return SCIP_OKAY;
}

/** substitutes variables (SCIP_EXPR_VARIDX) by expressions
 * a variable with index i is replaced by a copy of substexprs[i], if that latter is not NULL
 * if substexprs[i] == NULL, then the variable expression i is not touched */
SCIP_RETCODE SCIPexprSubstituteVars(
   BMS_BLKMEM*           blkmem,             /**< block memory data structure */
   SCIP_EXPR*            expr,               /**< expression, which of the children may be replaced */
   SCIP_EXPR**           substexprs          /**< array of substitute expressions; single entries can be NULL */
)
{
   int i;

   assert(blkmem != NULL);
   assert(expr   != NULL);
   assert(substexprs != NULL);

   for( i = 0; i < expr->nchildren; ++i )
   {
      if( expr->children[i]->op == SCIP_EXPR_VARIDX )
      {
         int varidx;
         varidx = expr->children[i]->data.intval;

         assert(varidx >= 0);
         if( substexprs[varidx] != NULL )
         {
            /* replace child i by copy of substexprs[expr->children[i]->opdata.intval] */
            SCIPexprFreeDeep(blkmem, &expr->children[i]);
            SCIP_CALL( SCIPexprCopyDeep(blkmem, &expr->children[i], substexprs[varidx]) );
         }
      }
      else
      {
         /* call recursively */
         SCIP_CALL( SCIPexprSubstituteVars(blkmem, expr->children[i], substexprs) );
      }
   }

   return SCIP_OKAY;
}

/** updates variable indices in expression tree */
void SCIPexprReindexVars(
   SCIP_EXPR*            expr,               /**< expression to update */
   int*                  newindices          /**< new indices of variables */
)
{
   int i;

   assert(expr != NULL);
   assert(newindices != NULL);

   if( expr->op == SCIP_EXPR_VARIDX )
   {
      expr->data.intval = newindices[expr->data.intval];
      assert(expr->data.intval >= 0);
   }

   for( i = 0; i < expr->nchildren; ++i )
      SCIPexprReindexVars(expr->children[i], newindices);
}

/** prints an expression */
void SCIPexprPrint(
   SCIP_EXPR*            expr,               /**< expression */
   FILE*                 file,               /**< file for printing, or NULL for stdout */
   const char**          varnames,           /**< names of variables, or NULL for default names */
   const char**          paramnames          /**< names of parameters, or NULL for default names */
)
{
   assert( expr != NULL );

   switch( expr->op )
   {
      case SCIP_EXPR_VARIDX:
         if( varnames != NULL )
         {
            assert(varnames[expr->data.intval] != NULL);
            SCIPmessageFPrintInfo(file, "%s", varnames[expr->data.intval]);
         }
         else
         {
            SCIPmessageFPrintInfo(file, "var%d", expr->data.intval);
         }
         break;
         
      case SCIP_EXPR_PARAM:
         if( paramnames != NULL )
         {
            assert(paramnames[expr->data.intval] != NULL);
            SCIPmessageFPrintInfo(file, "%s", paramnames[expr->data.intval]);
         }
         else
         {
            SCIPmessageFPrintInfo(file, "param%d", expr->data.intval );
         }
         break;
         
      case SCIP_EXPR_CONST:
         if (expr->data.dbl < 0.0 )
            SCIPmessageFPrintInfo(file, "(%lf)", expr->data.dbl );
         else
            SCIPmessageFPrintInfo(file, "%lf", expr->data.dbl );
         break;

      case SCIP_EXPR_PLUS:
         SCIPmessageFPrintInfo(file, "(");
         SCIPexprPrint(expr->children[0], file, varnames, paramnames);
         SCIPmessageFPrintInfo(file, " + ");
         SCIPexprPrint(expr->children[1], file, varnames, paramnames);
         SCIPmessageFPrintInfo(file, ")");
         break;
         
      case SCIP_EXPR_MINUS:
         SCIPmessageFPrintInfo(file, "(");
         SCIPexprPrint(expr->children[0], file, varnames, paramnames);
         SCIPmessageFPrintInfo(file, " - ");
         SCIPexprPrint(expr->children[1], file, varnames, paramnames);
         SCIPmessageFPrintInfo(file, ")");
         break;
         
      case SCIP_EXPR_MUL:
         SCIPmessageFPrintInfo(file, "(");
         SCIPexprPrint(expr->children[0], file, varnames, paramnames);
         SCIPmessageFPrintInfo(file, " * ");
         SCIPexprPrint(expr->children[1], file, varnames, paramnames);
         SCIPmessageFPrintInfo(file, ")");
         break;
         
      case SCIP_EXPR_DIV:
         SCIPmessageFPrintInfo(file, "(");
         SCIPexprPrint(expr->children[0], file, varnames, paramnames);
         SCIPmessageFPrintInfo(file, " / ");
         SCIPexprPrint(expr->children[1], file, varnames, paramnames);
         SCIPmessageFPrintInfo(file, ")");
         break;

      case SCIP_EXPR_POWER:
         SCIPmessageFPrintInfo(file, "(");
         SCIPexprPrint(expr->children[0], file, varnames, paramnames);
         SCIPmessageFPrintInfo(file, " ** ");
         SCIPexprPrint(expr->children[1], file, varnames, paramnames);
         SCIPmessageFPrintInfo(file, ")");
         break;
        
      case SCIP_EXPR_INTPOWER:
         SCIPmessageFPrintInfo(file, "power(");
         SCIPexprPrint(expr->children[0], file, varnames, paramnames);
         SCIPmessageFPrintInfo(file, ", %d)", expr->data.intval);
         break;

      case SCIP_EXPR_SQUARE:
      case SCIP_EXPR_SQRT:
      case SCIP_EXPR_EXP:
      case SCIP_EXPR_LOG:
      case SCIP_EXPR_SIN:
      case SCIP_EXPR_COS:
      case SCIP_EXPR_TAN:
      /* case SCIP_EXPR_ERF: */
      /* case SCIP_EXPR_ERFI: */
      case SCIP_EXPR_MIN:
      case SCIP_EXPR_MAX:
      case SCIP_EXPR_ABS:
      case SCIP_EXPR_SIGN:
      case SCIP_EXPR_SIGNPOWER:
      {
         int i;
         
         SCIPmessageFPrintInfo(file, "%s(", SCIPexprOpTable[expr->op].name);
         
         for( i = 0; i < expr->nchildren; ++i )
         {
            SCIPexprPrint(expr->children[i], file, varnames, paramnames);
            if( i + 1 < expr->nchildren )
            {
               SCIPmessageFPrintInfo(file, ", ");
            }
         }

         SCIPmessageFPrintInfo(file, ")");
         break;
      }

      case SCIP_EXPR_SUM:
      case SCIP_EXPR_PRODUCT:
      {
         switch( expr->nchildren )
         {
            case 0:
               SCIPmessageFPrintInfo(file, expr->op == SCIP_EXPR_SUM ? "0" : "1");
               break;
            case 1:
               SCIPexprPrint(expr->children[0], file, varnames, paramnames);
               break;
            default:
            {
               int i;
               const char* opstr = expr->op == SCIP_EXPR_SUM ? " + " : " * ";

               SCIPmessageFPrintInfo(file, "(");
               for( i = 0; i < expr->nchildren; ++i )
               {
                  if( i > 0 )
                  {
                     SCIPmessageFPrintInfo(file, opstr);
                  }
                  SCIPexprPrint(expr->children[i], file, varnames, paramnames);
               }
               SCIPmessageFPrintInfo(file, ")");
            }
         }
         break;
      }

      case SCIP_EXPR_LINEAR:
      {
         SCIP_Real constant;
         int i;

         constant = ((SCIP_Real*)expr->data.data)[expr->nchildren];

         if( expr->nchildren == 0 )
         {
            SCIPmessageFPrintInfo(file, "%.20g", constant);
            break;
         }

         SCIPmessageFPrintInfo(file, "(");

         if( constant != 0.0 )
         {
            SCIPmessageFPrintInfo(file, "%.20g", constant);
         }

         for( i = 0; i < expr->nchildren; ++i )
         {
            SCIPmessageFPrintInfo(file, " %+.20g ", ((SCIP_Real*)expr->data.data)[i]);
            SCIPexprPrint(expr->children[i], file, varnames, paramnames);
         }

         SCIPmessageFPrintInfo(file, ")");
         break;
      }

      case SCIP_EXPR_QUADRATIC:
      {
         SCIP_EXPRDATA_QUADRATIC* quadraticdata;
         int i;
         
         quadraticdata = (SCIP_EXPRDATA_QUADRATIC*)expr->data.data;
         assert(quadraticdata != NULL);

         SCIPmessageFPrintInfo(file, "(");

         for( i = 0; i < quadraticdata->nquadelems; ++i )
         {
            SCIPmessageFPrintInfo(file, " %+.20g ", quadraticdata->quadelems[i].coef);
            SCIPexprPrint(expr->children[quadraticdata->quadelems[i].idx1], file, varnames, paramnames);
            if( quadraticdata->quadelems[i].idx1 == quadraticdata->quadelems[i].idx2 )
            {
               SCIPmessageFPrintInfo(file, "^2");
            }
            else
            {
               SCIPmessageFPrintInfo(file, " * ");
               SCIPexprPrint(expr->children[quadraticdata->quadelems[i].idx2], file, varnames, paramnames);
            }
         }

         SCIPmessageFPrintInfo(file, ")");
         break;         
      }

      case SCIP_EXPR_POLYNOMIAL:
      {
         SCIP_EXPRDATA_POLYNOMIAL* polynomialdata;
         SCIP_EXPRDATA_MONOMIAL*   monomialdata;
         int i;
         int j;

         SCIPmessageFPrintInfo(file, "(");

         polynomialdata = (SCIP_EXPRDATA_POLYNOMIAL*)expr->data.data;
         assert(polynomialdata != NULL);

         if( polynomialdata->constant != 0.0 || polynomialdata->nmonomials == 0 )
         {
            SCIPmessageFPrintInfo(file, "%.20g", polynomialdata->constant);
         }

         for( i = 0; i < polynomialdata->nmonomials; ++i )
         {
            monomialdata = polynomialdata->monomials[i];
            SCIPmessageFPrintInfo(file, " %+.20g", monomialdata->coef);

            for( j = 0; j < monomialdata->nfactors; ++j )
            {
               SCIPmessageFPrintInfo(file, " * ");

               SCIPexprPrint(expr->children[monomialdata->childidxs[j]], file, varnames, paramnames);
               if( monomialdata->exponents[j] < 0.0 )
               {
                  SCIPmessageFPrintInfo(file, "^(%.20g)", monomialdata->exponents[j]);
               }
               else if( monomialdata->exponents[j] != 1.0 )
               {
                  SCIPmessageFPrintInfo(file, "^%.20g", monomialdata->exponents[j]);
               }
            }
         }

         SCIPmessageFPrintInfo(file, ")");
         break;
      }

      case  SCIP_EXPR_LAST:
      {
         SCIPerrorMessage("invalid expression\n");
         SCIPABORT();
      }
   }
}

/** creates an expression tree */
SCIP_RETCODE SCIPexprtreeCreate(
   BMS_BLKMEM*           blkmem,             /**< block memory data structure */
   SCIP_EXPRTREE**       tree,               /**< buffer to store address of created expression tree */
   SCIP_EXPR*            root,               /**< pointer to root expression, not copied deep !, can be NULL */
   int                   nvars,              /**< number of variables in variable mapping */
   int                   nparams,            /**< number of parameters in expression */
   SCIP_Real*            params              /**< values for parameters, or NULL (if NULL but nparams > 0, then params is initialized with zeros) */
)
{
   assert(blkmem != NULL);
   assert(tree   != NULL);

   SCIP_ALLOC( BMSallocBlockMemory(blkmem, tree) );
   
   (*tree)->blkmem    = blkmem;
   (*tree)->root      = root;
   (*tree)->nvars     = nvars;
   (*tree)->vars      = NULL;
   (*tree)->nparams   = nparams;
   (*tree)->interpreterdata = NULL;
   
   if( params != NULL )
   {
      assert(nparams > 0);
      SCIP_ALLOC( BMSduplicateBlockMemoryArray(blkmem, &(*tree)->params, params, nparams) );
   }
   else if( nparams > 0 )
   {
      SCIP_ALLOC( BMSallocBlockMemoryArray(blkmem, &(*tree)->params, nparams) );
      BMSclearMemoryArray((*tree)->params, nparams);
   }
   else
   {
      assert(nparams == 0);
      (*tree)->params = NULL;
   }

   return SCIP_OKAY;
}

/** copies an expression tree */
SCIP_RETCODE SCIPexprtreeCopy(
   BMS_BLKMEM*           blkmem,             /**< block memory that should be used in new expression tree */
   SCIP_EXPRTREE**       targettree,         /**< buffer to store address of copied expression tree */
   SCIP_EXPRTREE*        sourcetree          /**< expression tree to copy */
)
{
   assert(blkmem     != NULL);
   assert(targettree != NULL);
   assert(sourcetree != NULL);

   /* copy expression tree "header" */
   SCIP_ALLOC( BMSduplicateBlockMemory(blkmem, targettree, sourcetree) );
   
   /* we may have a new block memory; and we do not want to keep the others interpreter data */
   (*targettree)->blkmem          = blkmem;
   (*targettree)->interpreterdata = NULL;
   
   /* copy variables, if any */
   if( sourcetree->vars != NULL )
   {
      assert(sourcetree->nvars > 0);
      
      SCIP_ALLOC( BMSduplicateBlockMemoryArray(blkmem, &(*targettree)->vars, sourcetree->vars, sourcetree->nvars) );
   }
   
   /* copy parameters, if any */
   if( sourcetree->params != NULL )
   {
      assert(sourcetree->nparams > 0);
      
      SCIP_ALLOC( BMSduplicateBlockMemoryArray(blkmem, &(*targettree)->params, sourcetree->params, sourcetree->nparams) );
   }

   /* copy expression */
   SCIP_CALL( SCIPexprCopyDeep(blkmem, &(*targettree)->root, sourcetree->root) );
   
   return SCIP_OKAY;
}

/** frees an expression tree */
SCIP_RETCODE SCIPexprtreeFree(
   SCIP_EXPRTREE**       tree                /**< pointer to expression tree that is freed */
)
{
   assert( tree != NULL);
   assert(*tree != NULL);
   
   SCIP_CALL( SCIPexprtreeFreeInterpreterData(*tree) );
   
   if( (*tree)->root != NULL )
   {
      SCIPexprFreeDeep((*tree)->blkmem, &(*tree)->root);
      assert((*tree)->root == NULL);
   }
   
   BMSfreeBlockMemoryArrayNull((*tree)->blkmem, &(*tree)->vars,   (*tree)->nvars  );
   BMSfreeBlockMemoryArrayNull((*tree)->blkmem, &(*tree)->params, (*tree)->nparams);

   BMSfreeBlockMemory((*tree)->blkmem, tree);
   
   return SCIP_OKAY;
}

/** returns root expression of an expression tree */
SCIP_EXPR* SCIPexprtreeGetRoot(
   SCIP_EXPRTREE*        tree                /**< expression tree */
)
{
   assert(tree != NULL);
   
   return tree->root;
}

/** returns number of variables in expression tree */
int SCIPexprtreeGetNVars(
   SCIP_EXPRTREE*        tree                /**< expression tree */
)
{
   assert(tree != NULL);
   
   return tree->nvars;
}

/** returns number of parameters in expression tree */
int SCIPexprtreeGetNParams(
   SCIP_EXPRTREE*        tree                /**< expression tree */
)
{
   assert(tree != NULL);

   return tree->nparams;
}

/** returns values of parameters or NULL if none */
SCIP_Real* SCIPexprtreeGetParamVals(
   SCIP_EXPRTREE*        tree                /**< expression tree */
)
{
   assert(tree != NULL);
   
   return tree->params;
}

/** sets value of a single parameter in expression tree */
void SCIPexprtreeSetParamVal(
   SCIP_EXPRTREE*        tree,               /**< expression tree */
   int                   paramidx,           /**< index of parameter */
   SCIP_Real             paramval            /**< new value of parameter */
)
{
   assert(tree != NULL);
   assert(paramidx >= 0);
   assert(paramidx < tree->nparams);
   assert(tree->params != NULL);

   tree->params[paramidx] = paramval;
}

/** sets values of all parameters in expression tree */
void SCIPexprtreeSetParamVals(
   SCIP_EXPRTREE*        tree,               /**< expression tree */
   SCIP_Real*            paramvals           /**< new values of parameters */
)
{
   assert(tree != NULL);
   assert(paramvals != NULL);
   assert(tree->params != NULL);

   BMScopyMemoryArray(tree->params, paramvals, tree->nparams);
}

/** gets data of expression tree interpreter
 * @return NULL if not set
 */
SCIP_EXPRINTDATA* SCIPexprtreeGetInterpreterData(
   SCIP_EXPRTREE*        tree                /**< expression tree */
)
{
   assert(tree != NULL);
   
   return tree->interpreterdata;
}

/** sets data of expression tree interpreter */
void SCIPexprtreeSetInterpreterData(
   SCIP_EXPRTREE*        tree,               /**< expression tree */
   SCIP_EXPRINTDATA*     interpreterdata     /**< expression interpreter data */
)
{
   assert(tree != NULL);
   assert(interpreterdata != NULL);
   assert(tree->interpreterdata == NULL);

   tree->interpreterdata = interpreterdata;
}

/** frees data of expression tree interpreter, if any */
SCIP_RETCODE SCIPexprtreeFreeInterpreterData(
   SCIP_EXPRTREE*        tree                /**< expression tree */
   )
{
   if( tree->interpreterdata != NULL )
   {
      SCIP_CALL( SCIPexprintFreeData(&tree->interpreterdata) );
      assert(tree->interpreterdata == NULL);
   }

   return SCIP_OKAY;
}

/** indicates whether there are parameterized constants (SCIP_EXPR_PARAM) in expression tree */
SCIP_Bool SCIPexprtreeHasParam(
   SCIP_EXPRTREE*        tree                /**< expression tree */
)
{
   assert(tree != NULL);

   return SCIPexprHasParam(tree->root);
}

/** Gives maximal degree of expression in expression tree.
 * If constant expression, gives 0,
 * if linear expression, gives 1,
 * if polynomial expression, gives its maximal degree,
 * otherwise (nonpolynomial nonconstant expressions) gives at least SCIP_EXPR_DEGREEINFINITY.
 */
SCIP_RETCODE SCIPexprtreeGetMaxDegree(
   SCIP_EXPRTREE*        tree,               /**< expression tree */
   int*                  maxdegree           /**< buffer to store maximal degree */
)
{
   assert(tree != NULL);
   
   SCIP_CALL( SCIPexprGetMaxDegree(tree->root, maxdegree) );

   return SCIP_OKAY;
}

/** evaluates an expression tree w.r.t. a point */
SCIP_RETCODE SCIPexprtreeEval(
   SCIP_EXPRTREE*        tree,               /**< expression tree */
   SCIP_Real*            varvals,            /**< values for variables */
   SCIP_Real*            val                 /**< buffer to store expression tree value */
)
{
   assert(tree    != NULL);
   assert(varvals != NULL || tree->nvars == 0);
   assert(val     != NULL);

   SCIP_CALL( SCIPexprEval(tree->root, varvals, tree->params, val) );
   
   return SCIP_OKAY;
}

/** evaluates an expression tree w.r.t. an interval */
SCIP_RETCODE SCIPexprtreeEvalInt(
   SCIP_EXPRTREE*        tree,               /**< expression tree */
   SCIP_Real             infinity,           /**< value for infinity */
   SCIP_INTERVAL*        varvals,            /**< intervals for variables */
   SCIP_INTERVAL*        val                 /**< buffer to store expression tree value */
)
{
   assert(tree    != NULL);
   assert(varvals != NULL || tree->nvars == 0);
   assert(val     != NULL);

   SCIP_CALL( SCIPexprEvalInt(tree->root, infinity, varvals, tree->params, val) );

   return SCIP_OKAY;
}

/** substitutes variables (SCIP_EXPR_VARIDX) in an expression tree by expressions
 * A variable with index i is replaced by a copy of substexprs[i], if that latter is not NULL
 * if substexprs[i] == NULL, then the variable expression i is not touched */
SCIP_RETCODE SCIPexprtreeSubstituteVars(
   SCIP_EXPRTREE*        tree,               /**< expression tree */
   SCIP_EXPR**           substexprs          /**< array of substitute expressions; single entries can be NULL */
)
{
   assert(tree != NULL);
   assert(tree->root != NULL);

   if( tree->root->op == SCIP_EXPR_VARIDX )
   {
      int varidx;

      varidx = tree->root->data.intval;
      assert(varidx >= 0);
      if( substexprs[varidx] != NULL )
      {
         /* substitute root expression */
         SCIPexprFreeDeep(tree->blkmem, &tree->root);
         SCIP_CALL( SCIPexprCopyDeep(tree->blkmem, &tree->root, substexprs[varidx]) );
      }
   }
   else
   {
      /* check children (and grandchildren and so on...) of root expression */
      SCIP_CALL( SCIPexprSubstituteVars(tree->blkmem, tree->root, substexprs) );
   }

   /* substitution of variables should invalidate interpreter data */
   SCIP_CALL( SCIPexprtreeFreeInterpreterData(tree) );

   return SCIP_OKAY;
}

/** prints an expression tree */
void SCIPexprtreePrint(
   SCIP_EXPRTREE*        tree,               /**< expression tree */
   FILE*                 file,               /**< file for printing, or NULL for stdout */
   const char**          varnames,           /**< names of variables, or NULL for default names */
   const char**          paramnames          /**< names of parameters, or NULL for default names */
)
{
   assert(tree != NULL);

   SCIPexprPrint(tree->root, file, varnames, paramnames);
}

/** comparing two quadratic elements
 * a is better than b if index1 of a is smaller than index1 of b or index1 of both is equal but index2 of a is smaller than index2 of b
 */
#define QUADELEMS_ISBETTER(a, b) ( ((a).idx1 < (b).idx1) || ((a).idx1 == (b).idx1 && (a).idx2 < (b).idx2) )
/** swaps two quadratic elements */
#define QUADELEMS_SWAP(x,y) \
   {                \
      SCIP_QUADELEM temp = x;   \
      x = y;        \
      y = temp;     \
   }

/** quicksort an array of quadratic elements; pivot is the medial element
 * taken from scip/sorttpl.c */
static
void quadelemsQuickSort(
   SCIP_QUADELEM*       elems,               /**< array to be sorted */
   int                  start,               /**< starting index */
   int                  end                  /**< ending index */
   )
{
   assert(start <= end);

   /* use quick sort for long lists */
   while( end - start >= 25 ) /* 25 was SORTTPL_SHELLSORTMAX in sorttpl.c */
   {
      SCIP_QUADELEM pivotkey;
      int lo;
      int hi;
      int mid;

      /* select pivot element */
      mid = (start+end)/2;
      pivotkey = elems[mid];

      /* partition the array into elements < pivot [start,hi] and elements >= pivot [lo,end] */
      lo = start;
      hi = end;
      for( ;; )
      {
         while( lo < end   &&  QUADELEMS_ISBETTER(elems[lo], pivotkey) )
            lo++;
         while( hi > start && !QUADELEMS_ISBETTER(elems[hi], pivotkey) )
            hi--;

         if( lo >= hi )
            break;

         QUADELEMS_SWAP(elems[lo], elems[hi]);

         lo++;
         hi--;
      }
      assert(hi == lo-1 || hi == start);

      /* skip entries which are equal to the pivot element (three partitions, <, =, > than pivot)*/
      while( lo < end && !QUADELEMS_ISBETTER(pivotkey, elems[lo]) )
         lo++;

      /* make sure that we have at least one element in the smaller partition */
      if( lo == start )
      {
         /* everything is greater or equal than the pivot element: move pivot to the left (degenerate case) */
         assert(!QUADELEMS_ISBETTER(elems[mid], pivotkey)); /* the pivot element did not change its position */
         assert(!QUADELEMS_ISBETTER(pivotkey, elems[mid]));
         QUADELEMS_SWAP(elems[lo], elems[mid]);
         lo++;
      }

      /* sort the smaller partition by a recursive call, sort the larger part without recursion */
      if( hi - start <= end - lo )
      {
         /* sort [start,hi] with a recursive call */
         if( start < hi )
            quadelemsQuickSort(elems, start, hi);

         /* now focus on the larger part [lo,end] */
         start = lo;
      }
      else
      {
         /* sort [lo,end] with a recursive call */
         if( lo < end )
            quadelemsQuickSort(elems, lo, end);

         /* now focus on the larger part [start,hi] */
         end = hi;
      }
   }

   /* use shell sort on the remaining small list */
   if( end - start >= 1 )
   {
      static const int incs[3] = {1, 5, 19}; /* sequence of increments */
      int k;

      for( k = 2; k >= 0; --k )
      {
         int h;
         int i;

         for( h = incs[k], i = h + start; i <= end; ++i )
         {
            int j;
            SCIP_QUADELEM tempkey = elems[i];

            j = i;
            while( j >= h && QUADELEMS_ISBETTER(tempkey, elems[j-h]) )
            {
               elems[j] = elems[j-h];
               j -= h;
            }

            elems[j] = tempkey;
         }
      }
   }
}

/** sorts an array of quadratic elements
 * The elements are sorted such that the first index is increasing and
 * such that among elements with the same first index, the second index is increasing.
 * For elements with same first and second index, the order is not defined.
 */
void SCIPquadelemSort(
   SCIP_QUADELEM*        quadelems,          /**< array of quadratic elements */
   int                   nquadelems          /**< number of quadratic elements */
)
{
   if( nquadelems == 0 )
      return;

   quadelemsQuickSort(quadelems, 0, nquadelems-1);
}

/** Finds an index pair in a sorted array of quadratic elements.
 * If (idx1,idx2) is found in quadelems, then returns TRUE and stores position of quadratic element in *pos.
 * If (idx1,idx2) is not found in quadelems, then returns FALSE and stores position where a quadratic element with these indices would be inserted in *pos.
 * Assumes that idx1 <= idx2.
 */
SCIP_Bool SCIPquadelemSortedFind(
   SCIP_QUADELEM*        quadelems,          /**< array of quadratic elements */
   int                   idx1,               /**< index of first  variable in element to search for */
   int                   idx2,               /**< index of second variable in element to search for */
   int                   nquadelems,         /**< number of quadratic elements in array */
   int*                  pos                 /**< buffer to store position of found quadratic element or position where it would be inserted, or NULL */
)
{
   int left;
   int right;

   assert(quadelems != NULL || nquadelems == 0);
   assert(idx1 <= idx2);

   if( nquadelems == 0 )
   {
      if( pos != NULL )
         *pos = 0;
      return FALSE;
   }

   left = 0;
   right = nquadelems - 1;
   while( left <= right )
   {
      int middle;

      middle = (left+right)/2;
      assert(0 <= middle && middle < nquadelems);

      if( idx1 < quadelems[middle].idx1 || (idx1 == quadelems[middle].idx1 && idx2 < quadelems[middle].idx2) )  /*lint !e613*/
         right = middle - 1;
      else if( quadelems[middle].idx1 < idx1 || (quadelems[middle].idx1 == idx1 && quadelems[middle].idx2 < idx2) )  /*lint !e613*/
         left  = middle + 1;
      else
      {
         if( pos != NULL )
            *pos = middle;
         return TRUE;
      }
   }
   assert(left == right+1);

   if( pos != NULL )
      *pos = left;
   return FALSE;
}

/** Adds quadratic elements with same index and removes elements with coefficient 0.0.
 * Assumes that elements have been sorted before.
 */
void SCIPquadelemSqueeze(
   SCIP_QUADELEM*        quadelems,          /**< array of quadratic elements */
   int                   nquadelems,         /**< number of quadratic elements */
   int*                  nquadelemsnew       /**< pointer to store new (reduced) number of quadratic elements */
)
{
   int i;
   int next;
   
   assert(quadelems     != NULL);
   assert(nquadelemsnew != NULL);
   assert(nquadelems    >= 0);
   
   i = 0;
   next = 0;
   while( next < nquadelems )
   {
      /* assert that array is sorted */
      assert(QUADELEMS_ISBETTER(quadelems[i], quadelems[next]) ||
         (quadelems[i].idx1 == quadelems[next].idx1 && quadelems[i].idx2 == quadelems[next].idx2));
      
      /* skip elements with coefficient 0.0 */
      if( quadelems[next].coef == 0.0 )
      {
         ++next;
         continue;
      }
      
      /* if next element has same index as previous one, add it to the previous one */
      if( i >= 1 &&
         quadelems[i-1].idx1 == quadelems[next].idx1 &&
         quadelems[i-1].idx2 == quadelems[next].idx2 )
      {
         quadelems[i-1].coef += quadelems[next].coef;
         ++next;
         continue;
      }
      
      /* otherwise, move next element to current position */
      quadelems[i] = quadelems[next];
      ++i;
      ++next;
   }
   assert(next == nquadelems);

   /* now i should point to the position after the last valid element, i.e., it is the remaining number of elements */
   *nquadelemsnew = i;
}
