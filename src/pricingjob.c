/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                           */
/*                  This file is part of the program                         */
/*          GCG --- Generic Column Generation                                */
/*                  a Dantzig-Wolfe decomposition based extension            */
/*                  of the branch-cut-and-price framework                    */
/*         SCIP --- Solving Constraint Integer Programs                      */
/*                                                                           */
/* Copyright (C) 2010-2017 Operations Research, RWTH Aachen University       */
/*                         Zuse Institute Berlin (ZIB)                       */
/*                                                                           */
/* This program is free software; you can redistribute it and/or             */
/* modify it under the terms of the GNU Lesser General Public License        */
/* as published by the Free Software Foundation; either version 3            */
/* of the License, or (at your option) any later version.                    */
/*                                                                           */
/* This program is distributed in the hope that it will be useful,           */
/* but WITHOUT ANY WARRANTY; without even the implied warranty of            */
/* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the             */
/* GNU Lesser General Public License for more details.                       */
/*                                                                           */
/* You should have received a copy of the GNU Lesser General Public License  */
/* along with this program; if not, write to the Free Software               */
/* Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.*/
/*                                                                           */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/**@file   pricingjob.c
 * @brief  methods for working with pricing jobs
 * @author Christian Puchert
 *
 * Various methods to work with pricing jobs
 *
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#include "pricingjob.h"
#include "pub_pricingjob.h"

#include "gcg.h"

#include "scip/scip.h"

#include <assert.h>

/** create a pricing job */
SCIP_RETCODE GCGpricingjobCreate(
   SCIP*                 scip,               /**< SCIP data structure (master problem) */
   GCG_PRICINGJOB**      pricingjob,         /**< pricing job to be created */
   SCIP*                 pricingscip,        /**< SCIP data structure of the corresponding pricing problem */
   int                   probnr              /**< index of the corresponding pricing problem */
)
{
   SCIP_CALL( SCIPallocMemory(scip, pricingjob) );

   (*pricingjob)->pricingscip = pricingscip;
   (*pricingjob)->probnr = probnr;
   (*pricingjob)->score = 0.0;
   (*pricingjob)->heuristic = FALSE;
   (*pricingjob)->cols = NULL;
   (*pricingjob)->ncols = 0;
   (*pricingjob)->nimpcols = 0;

   return SCIP_OKAY;
}

/** free a pricing job */
void GCGpricingjobFree(
   SCIP*                 scip,               /**< SCIP data structure (master problem) */
   GCG_PRICINGJOB**      pricingjob          /**< pricing job to be freed */
)
{
   SCIPfreeMemoryArrayNull(scip, &(*pricingjob)->cols);
   SCIPfreeMemory(scip, pricingjob);
   *pricingjob = NULL;
}

/** setup a pricing job at the beginning of the pricing loop */
SCIP_RETCODE GCGpricingjobSetup(
   SCIP*                 scip,               /**< master SCIP instance */
   GCG_PRICINGJOB*       pricingjob,         /**< pricing job */
   SCIP_Bool             heuristic,          /**< shall the pricing job be performed heuristically? */
   int                   maxcolsprob,        /**< maximum number of columns that the problem should be looking for */
   int                   scoring,            /**< scoring parameter */
   SCIP_Real             dualsolconv,        /**< dual solution value of corresponding convexity constraint */
   int                   npointsprob,        /**< total number of extreme points generated so far by the pricing problem */
   int                   nraysprob,          /**< total number of extreme rays generated so far by the pricing problem */
   int                   maxcols             /**< maximum number of columns to be generated in total */
   )
{
   pricingjob->heuristic = heuristic;

   /* set the solution limit on the pricing problem */
   SCIP_CALL( SCIPsetIntParam(pricingjob->pricingscip, "limits/solutions", SCIPgetNLimSolsFound(pricingjob->pricingscip) + maxcolsprob) );

   /* set the score */
   switch( scoring )
   {
   case 1:
      pricingjob->score = dualsolconv;
      break;
   case 2:
      pricingjob->score = -(0.2 * npointsprob + nraysprob);
      break;
   default:
      pricingjob->score = 0.0;
      break;
   }

   /* initialize result variables */
   pricingjob->nsolves = 0;
   pricingjob->pricingstatus = SCIP_STATUS_UNKNOWN;
   pricingjob->lowerbound = -SCIPinfinity(scip);
   if( pricingjob->cols == NULL )
   {
      SCIP_CALL( SCIPallocMemoryArray(scip, &pricingjob->cols, maxcols) ); /*lint !e866*/
   }
   BMSclearMemoryArray(pricingjob->cols, maxcols);
   pricingjob->ncols = 0;
   pricingjob->nimpcols = 0;

   return SCIP_OKAY;
}

/** update a pricing job after the pricing problem has been solved */
void GCGpricingjobUpdate(
   SCIP*                 scip,               /**< SCIP data structure (master problem) */
   GCG_PRICINGJOB*       pricingjob,         /**< pricing job */
   SCIP_STATUS           status,             /**< status after solving the pricing problem */
   SCIP_Real             lowerbound,         /**< lower bound returned by the pricing problem */
   GCG_COL**             cols,               /**< columns found by the last solving of the pricing problem */
   int                   ncols               /**< number of columns found */
   )
{
   ++pricingjob->nsolves;
   pricingjob->pricingstatus = status;
   pricingjob->lowerbound = lowerbound;
   for( int i = 0; i < ncols; ++i )
   {
      pricingjob->cols[pricingjob->ncols + i] = cols[i];
      if( SCIPisDualfeasNegative(scip, GCGcolGetRedcost(cols[i])) )
         ++pricingjob->nimpcols;
   }
   pricingjob->ncols += ncols;
}

/** free all columns of a pricing job */
void GCGpricingjobFreeCols(
   GCG_PRICINGJOB*       pricingjob          /**< pricing job */
   )
{
   for( int i = 0; i < pricingjob->ncols; ++i )
   {
      GCGfreeGcgCol(&pricingjob->cols[i]);
      pricingjob->cols[i] = NULL;
   }
   pricingjob->ncols = 0;
}

/** get the SCIP instance corresponding to the pricing job */
SCIP* GCGpricingjobGetPricingscip(
   GCG_PRICINGJOB*       pricingjob          /**< pricing job */
   )
{
   return pricingjob->pricingscip;
}

/** get the index of the corresponding pricing problem */
int GCGpricingjobGetProbnr(
   GCG_PRICINGJOB*       pricingjob          /**< pricing job */
   )
{
   return pricingjob->probnr;
}

/** return whether the pricing job is to be performed heuristically */
SCIP_Bool GCGpricingjobIsHeuristic(
   GCG_PRICINGJOB*       pricingjob          /**< pricing job */
   )
{
   return pricingjob->heuristic;
}

/** set the pricing job to be performed heuristically */
void GCGpricingjobSetHeuristic(
   GCG_PRICINGJOB*       pricingjob          /**< pricing job */
   )
{
   pricingjob->heuristic = TRUE;
}

/** set the pricing job to be performed exactly */
void GCGpricingjobSetExact(
   GCG_PRICINGJOB*       pricingjob          /**< pricing job */
   )
{
   pricingjob->heuristic = FALSE;
}

/** get the score of a pricing job */
SCIP_Real GCGpricingjobGetScore(
   GCG_PRICINGJOB*       pricingjob          /**< pricing job */
   )
{
   return pricingjob->score;
}

/** get the number of times the pricing job was performed during the loop */
int GCGpricingjobGetNSolves(
   GCG_PRICINGJOB*       pricingjob          /**< pricing job */
   )
{
   return pricingjob->nsolves;
}

/* get the status of a pricing job */
SCIP_STATUS GCGpricingjobGetStatus(
   GCG_PRICINGJOB*       pricingjob          /**< pricing job */
   )
{
   return pricingjob->pricingstatus;
}

/* get the lower bound of a pricing job */
SCIP_Real GCGpricingjobGetLowerbound(
   GCG_PRICINGJOB*       pricingjob          /**< pricing job */
   )
{
   return pricingjob->lowerbound;
}

/** set the lower bound of a pricing job */
void GCGpricingjobSetLowerbound(
   GCG_PRICINGJOB*       pricingjob,         /**< pricing job */
   SCIP_Real             lowerbound          /**< new lower bound */
   )
{
   pricingjob->lowerbound = lowerbound;
}

/* get a column array of a pricing job */
GCG_COL** GCGpricingjobGetCols(
   GCG_PRICINGJOB*       pricingjob          /**< pricing job */
   )
{
   return pricingjob->cols;
}

/* get a column found by a pricing job */
GCG_COL* GCGpricingjobGetCol(
   GCG_PRICINGJOB*       pricingjob,         /**< pricing job */
   int                   idx                 /**< index of a column */
   )
{
   return pricingjob->cols[idx];
}

/* get the number of columns found by a pricing job */
int GCGpricingjobGetNCols(
   GCG_PRICINGJOB*       pricingjob          /**< pricing job */
   )
{
   return pricingjob->ncols;
}

/* set the number of columns found by a pricing job */
void GCGpricingjobSetNCols(
   GCG_PRICINGJOB*       pricingjob,         /**< pricing job */
   int                   ncols               /**< number of columns */
   )
{
   pricingjob->ncols = ncols;
}

/* get the number of improving columns found by a pricing job */
int GCGpricingjobGetNImpCols(
   GCG_PRICINGJOB*       pricingjob          /**< pricing job */
   )
{
   return pricingjob->nimpcols;
}
