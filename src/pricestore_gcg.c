/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                           */
/*                  This file is part of the program and library             */
/*         SCIP --- Solving Constraint Integer Programs                      */
/*                                                                           */
/*    Copyright (C) 2002-2017 Konrad-Zuse-Zentrum                            */
/*                            fuer Informationstechnik Berlin                */
/*                                                                           */
/*  SCIP is distributed under the terms of the ZIB Academic License.         */
/*                                                                           */
/*  You should have received a copy of the ZIB Academic License              */
/*  along with SCIP; see the file COPYING. If not email to scip@zib.de.      */
/*                                                                           */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/**@file   pricestore.c
 * @brief  methods for storing priced cols (based on SCIP's separation storage)
 * @author Jonas Witt
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/
//#define SCIP_DEBUG

#include <assert.h>

#include "scip/def.h"
#include "scip/set.h"
#include "scip/stat.h"
#include "scip/clock.h"
#include "scip/lp.h"
#include "scip/var.h"
#include "scip/tree.h"
#include "scip/reopt.h"
#include "scip/event.h"
#include "scip/cons.h"
#include "scip/debug.h"

#include "pricestore_gcg.h"
#include "struct_pricestore_gcg.h"

/*
 * dynamic memory arrays
 */

/** resizes cols and score arrays to be able to store at least num entries */
static
SCIP_RETCODE pricestoreEnsureColsMem(
   GCG_PRICESTORE*       pricestore,          /**< price storage */
   int                   num                 /**< minimal number of slots in array */
   )
{
   assert(pricestore != NULL);
   assert(pricestore->scip != NULL);

   if( num > pricestore->colssize )
   {
      int newsize;

      newsize = SCIPcalcMemGrowSize(pricestore->scip, num);
      SCIP_CALL( SCIPreallocMemoryArray(pricestore->scip, &pricestore->cols, newsize) );
      SCIP_CALL( SCIPreallocMemoryArray(pricestore->scip, &pricestore->objparallelisms, newsize) );
      SCIP_CALL( SCIPreallocMemoryArray(pricestore->scip, &pricestore->orthogonalities, newsize) );
      SCIP_CALL( SCIPreallocMemoryArray(pricestore->scip, &pricestore->scores, newsize) );
      pricestore->colssize = newsize;
   }
   assert(num <= pricestore->colssize);

   return SCIP_OKAY;
}

/** creates price storage */
SCIP_RETCODE GCGpricestoreCreate(
   SCIP*                 scip,                /**< SCIP data structure */
   GCG_PRICESTORE**      pricestore,          /**< pointer to store price storage */
   SCIP_Real             efficiacyfac,          /**< factor of -redcost/norm in score function */
   SCIP_Real             objparalfac,         /**< factor of objective parallelism in score function */
   SCIP_Real             orthofac,            /**< factor of orthogonalities in score function */
   SCIP_Real             mincolorth,          /**< minimal orthogonality of columns to add
                                                  (with respect to columns added in the current round) */
   SCIP_Real             maxpricecolsroot,    /**< maximum number of columns per round */
   SCIP_Real             maxpricecols,        /**< maximum number of columns per round */
   SCIP_Real             maxpricecolsfarkas,  /**< maximum number of columns per Farkas round */
   GCG_EFFICIACYCHOICE   efficiacychoice      /**< choice to base efficiacy on */
   )
{
   assert(pricestore != NULL);

   SCIP_CALL( SCIPallocMemory(scip, pricestore) );

   SCIP_CALL( SCIPcreateClock(scip, &(*pricestore)->priceclock) );

   (*pricestore)->scip = scip;
   (*pricestore)->cols = NULL;
   (*pricestore)->objparallelisms = NULL;
   (*pricestore)->orthogonalities = NULL;
   (*pricestore)->scores = NULL;
   (*pricestore)->colssize = 0;
   (*pricestore)->ncols = 0;
   (*pricestore)->nforcedcols = 0;
   (*pricestore)->ncolsfound = 0;
   (*pricestore)->ncolsfoundround = 0;
   (*pricestore)->ncolsapplied = 0;
   (*pricestore)->infarkas = FALSE;
   (*pricestore)->forcecols = FALSE;
   (*pricestore)->efficiacyfac = efficiacyfac;         /**< factor of -redcost/norm in score function */
   (*pricestore)->objparalfac = objparalfac;        /**< factor of objective parallelism in score function */
   (*pricestore)->orthofac = orthofac;           /**< factor of orthogonalities in score function */
   (*pricestore)->mincolorth = mincolorth;         /**< minimal orthogonality of columns to add
                                                       (with respect to columns added in the current round) */
   (*pricestore)->efficiacychoice = efficiacychoice;
   (*pricestore)->maxpricecolsroot = maxpricecolsroot;       /**< maximum number of columns per round */
   (*pricestore)->maxpricecols = maxpricecols;       /**< maximum number of columns per round */
   (*pricestore)->maxpricecolsfarkas = maxpricecolsfarkas; /**< maximum number of columns per Farkas round */

   return SCIP_OKAY;
}

/** frees price storage */
SCIP_RETCODE GCGpricestoreFree(
   SCIP*                 scip,                /**< SCIP data structure */
   GCG_PRICESTORE**      pricestore           /**< pointer to store price storage */
   )
{
   assert(scip == (*pricestore)->scip);
   assert(pricestore != NULL);
   assert(*pricestore != NULL);
   assert((*pricestore)->ncols == 0);

   SCIPinfoMessage(scip, NULL, "Pricing time in pricestore = %f sec\n", GCGpricestoreGetTime(*pricestore));

   /* free clock */
   SCIPfreeClock(scip, &(*pricestore)->priceclock);

   SCIPfreeMemoryArrayNull(scip, &(*pricestore)->cols);
   SCIPfreeMemoryArrayNull(scip, &(*pricestore)->objparallelisms);
   SCIPfreeMemoryArrayNull(scip, &(*pricestore)->orthogonalities);
   SCIPfreeMemoryArrayNull(scip, &(*pricestore)->scores);
   SCIPfreeMemory(scip, pricestore);

   return SCIP_OKAY;
}

/** informs price storage, that the setup in Farkas pricing starts now */
void GCGpricestoreStartFarkas(
   GCG_PRICESTORE*       pricestore           /**< price storage */
   )
{
   assert(pricestore != NULL);
   assert(pricestore->ncols == 0);

   pricestore->infarkas = TRUE;
}

/** informs price storage, that the setup in Farkas pricing is now finished */
void GCGpricestoreEndFarkas(
   GCG_PRICESTORE*       pricestore           /**< price storage */
   )
{
   assert(pricestore != NULL);
   assert(pricestore->ncols == 0);

   pricestore->infarkas = FALSE;
}

/** informs price storage, that the following cols should be used in any case */
void GCGpricestoreStartForceCols(
   GCG_PRICESTORE*       pricestore           /**< price storage */
   )
{
   assert(pricestore != NULL);
   assert(!pricestore->forcecols);

   pricestore->forcecols = TRUE;
}

/** informs price storage, that the following cols should no longer be used in any case */
void GCGpricestoreEndForceCols(
   GCG_PRICESTORE*       pricestore           /**< price storage */
   )
{
   assert(pricestore != NULL);
   assert(pricestore->forcecols);

   pricestore->forcecols = FALSE;
}

/** removes a non-forced col from the price storage */
static
SCIP_RETCODE pricestoreDelCol(
   GCG_PRICESTORE*       pricestore,          /**< price storage */
   int                   pos,                 /**< position of col to delete */
   SCIP_Bool             free                 /**< should col be freed */
   )
{
   assert(pricestore != NULL);
   assert(pricestore->cols != NULL);
   assert(pricestore->nforcedcols <= pos && pos < pricestore->ncols);

   /* release the row */
   if( free )
      GCGfreeGcgCol(&(pricestore->cols[pos]));

   /* move last col to the empty position */
   pricestore->cols[pos] = pricestore->cols[pricestore->ncols-1];
   pricestore->objparallelisms[pos] = pricestore->objparallelisms[pricestore->ncols-1];
   pricestore->orthogonalities[pos] = pricestore->orthogonalities[pricestore->ncols-1];
   pricestore->scores[pos] = pricestore->scores[pricestore->ncols-1];
   pricestore->ncols--;

   return SCIP_OKAY;
}

/** adds col to price storage and captures it;
 *  if the col should be forced to enter the LP, an infinite score has to be used
 */
SCIP_RETCODE GCGpricestoreAddCol(
   SCIP*                 scip,               /**< SCIP data structure */
   GCG_PRICESTORE*       pricestore,         /**< price storage */
   GCG_COL*              col,                /**< pricerated col */
   SCIP_Bool             forcecol            /**< should the col be forced to enter the LP? */
   )
{
   SCIP_Real colefficacy;
   SCIP_Real colobjparallelism;
   SCIP_Real colscore;

   int pos;

   assert(pricestore != NULL);
   assert(pricestore->nforcedcols <= pricestore->ncols);
   assert(col != NULL);

   /* start timing */
   SCIPstartClock(pricestore->scip, pricestore->priceclock);

   /* update statistics of total number of found cols */
   pricestore->ncolsfound++;
   pricestore->ncolsfoundround++;

   /* a col is forced to enter the LP if
    *  - we construct the initial LP, or
    *  - it has infinite score factor, or
    * if it is a non-forced col and no cols should be added, abort
    */
   forcecol = forcecol /* || pricestore->infarkas */|| pricestore->forcecols;

   /* get enough memory to store the col */
   SCIP_CALL( pricestoreEnsureColsMem(pricestore, pricestore->ncols+1) );
   assert(pricestore->ncols < pricestore->colssize);

   GCGcolComputeNorm(scip, col);

   if( forcecol )
   {
      colefficacy = SCIPinfinity(scip);
      colscore = SCIPinfinity(scip);
      colobjparallelism = 1.0;
   }
   else
   {
      /* initialize values to invalid (will be initialized during col filtering) */
      colefficacy = -1.0*GCGcolGetRedcost(col) / GCGcolGetNorm(col);
      colscore = SCIP_INVALID;

      if( SCIPisPositive(scip, pricestore->objparalfac) )
         colobjparallelism = GCGcolComputeDualObjPara(scip, col);
      else
         colobjparallelism = 0.0; /* no need to calculate it */
   }

   SCIPdebugMessage("adding col %p to price storage of size %d (forcecol=%u)\n",
      (void*)col, pricestore->ncols, forcecol);
   /*SCIPdebug(SCIProwPrint(col, set->scip->messagehdlr, NULL));*/

   /* add col to arrays */
   if( forcecol )
   {
      /* make room at the beginning of the array for forced col */
      pos = pricestore->nforcedcols;
      pricestore->cols[pricestore->ncols] = pricestore->cols[pos];
      pricestore->objparallelisms[pricestore->ncols] = pricestore->objparallelisms[pos];
      pricestore->orthogonalities[pricestore->ncols] = pricestore->orthogonalities[pos];
      pricestore->scores[pricestore->ncols] = pricestore->scores[pos];
      pricestore->nforcedcols++;
   }
   else
      pos = pricestore->ncols;

   pricestore->cols[pos] = col;
   pricestore->objparallelisms[pos] = colobjparallelism;
   pricestore->orthogonalities[pos] = 1.0;
   pricestore->scores[pos] = colscore;
   pricestore->ncols++;

   /* stop timing */
   SCIPstopClock(pricestore->scip, pricestore->priceclock);

   return SCIP_OKAY;
}

/** updates the orthogonalities and scores of the non-forced cols after the given col was added to the LP */
static
SCIP_RETCODE pricestoreUpdateOrthogonalities(
   GCG_PRICESTORE*       pricestore,          /**< price storage */
   GCG_COL*              col,                /**< col that was applied */
   SCIP_Real             mincolorthogonality /**< minimal orthogonality of cols to apply to LP */
   )
{
   int pos;

   assert(pricestore != NULL);

   pos = pricestore->nforcedcols;
   while( pos < pricestore->ncols )
   {
      SCIP_Real thisortho;

      /* update orthogonality */
      thisortho = GCGcolComputeOrth(pricestore->scip, col, pricestore->cols[pos]);

      if( thisortho < pricestore->orthogonalities[pos] )
      {
         if( thisortho < mincolorthogonality )
         {
            /* col is too parallel: release the row and delete the col */
            SCIPdebugMessage("    -> deleting parallel col %p after adding %p (pos=%d, orthogonality=%g, score=%g)\n",
               (void*) pricestore->cols[pos], (void*) col, pos, thisortho, pricestore->scores[pos]);
            SCIP_CALL( pricestoreDelCol(pricestore, pos, TRUE) );
            continue;
         }
         else
         {
            SCIP_Real colefficiacy;

            /* calculate cut's efficacy */
            switch ( pricestore->efficiacychoice )
            {
            case GCG_EFFICIACYCHOICE_DANTZIG:
               colefficiacy = -1.0 *GCGcolGetRedcost(pricestore->cols[pos]);
               break;
            case GCG_EFFICIACYCHOICE_STEEPESTEDGE:
               colefficiacy = -1.0 *GCGcolGetRedcost(pricestore->cols[pos])/ GCGcolGetNorm(col);
               break;
            case GCG_EFFICIACYCHOICE_LAMBDA:
               SCIPerrorMessage("Lambda pricing not yet implemented.\n");
               return SCIP_INVALIDCALL;
               break;
            default:
               SCIPerrorMessage("Invalid efficiacy choice.\n");
               return SCIP_INVALIDCALL;
            }

            /* recalculate score */
            pricestore->orthogonalities[pos] = thisortho;
            assert( pricestore->objparallelisms[pos] != SCIP_INVALID ); /*lint !e777*/
            assert( pricestore->scores[pos] != SCIP_INVALID ); /*lint !e777*/


            pricestore->scores[pos] = pricestore->efficiacyfac * colefficiacy
               + pricestore->objparalfac * pricestore->objparallelisms[pos]
               + pricestore->orthofac * thisortho;
         }
      }
      pos++;
   }

   return SCIP_OKAY;
}

/** applies the given col to the LP and updates the orthogonalities and scores of remaining cols */
static
SCIP_RETCODE pricestoreApplyCol(
   GCG_PRICESTORE*       pricestore,         /**< price storage */
   GCG_COL*              col,                /**< col to apply to the LP */
   SCIP_Bool             force,              /**< force column */
   SCIP_Real             mincolorthogonality,/**< minimal orthogonality of cols to apply to LP */
   int                   depth,              /**< depth of current node */
   int*                  ncolsapplied,       /**< pointer to count the number of applied cols */
   SCIP_Real             score               /**< score of column (or -1.0 if not specified) */
   )
{
   SCIP_Bool added;
   assert(pricestore != NULL);
   assert(ncolsapplied != NULL);

   /* a row could have been added twice to the price store; add it only once! */
   SCIP_CALL( GCGcreateNewMasterVarFromGcgCol(pricestore->scip, pricestore->infarkas, col, force, &added, NULL) );

   assert(added);
   /* update statistics -> only if we are not in the initial lp (cols are only counted if added during run) */
   if( added )
   {
      (*ncolsapplied)++;
   }

   /* update the orthogonalities if needed */
   if( SCIPisGT(pricestore->scip, mincolorthogonality, SCIPepsilon(pricestore->scip)) || SCIPisPositive(pricestore->scip, pricestore->orthofac))
      SCIP_CALL( pricestoreUpdateOrthogonalities(pricestore, col, mincolorthogonality) );

   return SCIP_OKAY;
}

/** returns the position of the best non-forced col in the cols array */
static
int pricestoreGetBestCol(
   GCG_PRICESTORE*       pricestore           /**< price storage */
   )
{
   SCIP_Real bestscore;
   int bestpos;
   int pos;

   assert(pricestore != NULL);

   bestscore = SCIP_REAL_MIN;
   bestpos = -1;
   for( pos = pricestore->nforcedcols; pos < pricestore->ncols; pos++ )
   {
      /* check if col is current best col */
      assert( pricestore->scores[pos] != SCIP_INVALID ); /*lint !e777*/
      if( pricestore->scores[pos] > bestscore )
      {
         bestscore = pricestore->scores[pos];
         bestpos = pos;
      }
   }

   return bestpos;
}

/** computes score for current LP solution and initialized orthogonalities */
static
SCIP_RETCODE computeScore(
   GCG_PRICESTORE*       pricestore,          /**< price storage */
   SCIP_Bool             handlepool,          /**< whether the efficacy of cols in the pool should be reduced  */
   int                   pos                  /**< position of col to handle */
   )
{
   GCG_COL* col;
   SCIP_Real colefficiacy;
   SCIP_Real colscore;

   col = pricestore->cols[pos];

   /* calculate cut's efficacy */
   switch ( pricestore->efficiacychoice )
   {
   case GCG_EFFICIACYCHOICE_DANTZIG:
      colefficiacy = -1.0 *GCGcolGetRedcost(pricestore->cols[pos]);
      break;
   case GCG_EFFICIACYCHOICE_STEEPESTEDGE:
      colefficiacy = -1.0 *GCGcolGetRedcost(pricestore->cols[pos])/ GCGcolGetNorm(col);
      break;
   case GCG_EFFICIACYCHOICE_LAMBDA:
      SCIPerrorMessage("Lambda pricing not yet implemented.\n");
      return SCIP_INVALIDCALL;
      break;
   default:
      SCIPerrorMessage("Invalid efficiacy choice.\n");
      return SCIP_INVALIDCALL;
   }

   assert( pricestore->objparallelisms[pos] != SCIP_INVALID ); /*lint !e777*/
   colscore = pricestore->efficiacyfac * colefficiacy
            + pricestore->objparalfac * pricestore->objparallelisms[pos]
            + pricestore->orthofac * 1.0;;
   assert( !SCIPisInfinity(pricestore->scip, colscore) );

   pricestore->scores[pos] = colscore;

   /* make sure that the orthogonalities are initialized to 1.0 */
   pricestore->orthogonalities[pos] = 1.0;

   return SCIP_OKAY;
}

/** adds cols to the LP and clears price storage */
SCIP_RETCODE GCGpricestoreApplyCols(
   GCG_PRICESTORE*       pricestore,          /**< price storage */
   int*                  nfoundvars           /**< pointer to store number of variables that were added to the problem */
   )
{
   SCIP* scip;
   SCIP_NODE* node;
   SCIP_Real mincolorthogonality;
   SCIP_Bool applied;
   int depth;
   int maxpricecols;
   int ncolsapplied;
   int pos;

   assert(pricestore != NULL);

   scip = pricestore->scip;

   SCIPdebugMessage("applying %d cols\n", pricestore->ncols);

   /* start timing */
   SCIPstartClock(pricestore->scip, pricestore->priceclock);

   node = SCIPgetCurrentNode(scip);
   assert(node != NULL);

   /* get maximal number of cols to add to the LP */
   /* TODO: get from pricer */
   if( pricestore->infarkas )
      maxpricecols = pricestore->maxpricecolsfarkas;
   else if( SCIPgetCurrentNode(pricestore->scip) == SCIPgetRootNode(pricestore->scip) )
      maxpricecols = pricestore->maxpricecolsroot;
   else
      maxpricecols = pricestore->maxpricecols;

   ncolsapplied = 0;

   /* get depth of current node */
   depth = SCIPnodeGetDepth(node);

   /* set minimal col orthogonality */
   mincolorthogonality = pricestore->mincolorth;
   mincolorthogonality = MAX(mincolorthogonality, SCIPepsilon(scip));

   /* Compute scores for all non-forced cols and initialize orthogonalities - make sure all cols are initialized again for the current LP solution */
   for( pos = pricestore->nforcedcols; pos < pricestore->ncols; pos++ )
   {
      SCIP_CALL( computeScore(pricestore, TRUE, pos) );
   }

   /* apply all forced cols */
   for( pos = 0; pos < pricestore->nforcedcols; pos++ )
   {
      GCG_COL* col;

      col = pricestore->cols[pos];
      assert(SCIPisInfinity(scip, pricestore->scores[pos]));

      /* if the col is a bound change (i.e. a row with only one variable), add it as bound change instead of LP row */
      applied = FALSE;

      /* add col to the LP and update orthogonalities */
      SCIPdebugMessage(" -> applying forced col %p\n", (void*) col);

      SCIP_CALL( pricestoreApplyCol(pricestore, col, TRUE, mincolorthogonality, depth, &ncolsapplied, pricestore->scores[pos]) );
   }

   /* apply non-forced cols */
   while( ncolsapplied < maxpricecols && pricestore->ncols > pricestore->nforcedcols )
   {
      GCG_COL* col;
      int bestpos;
      SCIP_Real score;

      /* get best non-forced col */
      bestpos = pricestoreGetBestCol(pricestore);
      assert(pricestore->nforcedcols <= bestpos && bestpos < pricestore->ncols);
      assert(pricestore->scores[bestpos] != SCIP_INVALID ); /*lint !e777*/
      col = pricestore->cols[bestpos];
      score = pricestore->scores[bestpos];
      assert(!SCIPisInfinity(scip, pricestore->scores[bestpos]));

      SCIPdebugMessage(" -> applying col %p (pos=%d/%d, efficacy=%g, objparallelism=%g, orthogonality=%g, score=%g)\n",
         (void*)col, bestpos, pricestore->ncols, GCGcolGetRedcost(pricestore->cols[bestpos]), pricestore->objparallelisms[bestpos],
         pricestore->orthogonalities[bestpos], pricestore->scores[bestpos]);

      /* Do not add (non-forced) non-violated cols.
       * Note: do not take SCIPsetIsEfficacious(), because constraint handlers often add cols w.r.t. SCIPsetIsFeasPositive().
       * Note2: if pricerating/feastolfac != -1, constraint handlers may even add cols w.r.t. SCIPsetIsPositive(); those are currently rejected here
       */
      if( SCIPisDualfeasNegative(scip, GCGcolGetRedcost(col)) )
      {
         /* add col to the LP and update orthogonalities */
         SCIP_CALL( pricestoreApplyCol(pricestore, col, FALSE, mincolorthogonality, depth, &ncolsapplied, score) );

         /* release the row and delete the col (also issuing ROWDELETEDPRICE event) */
         SCIP_CALL( pricestoreDelCol(pricestore, bestpos, TRUE) );
      }
      else
         break;
   }

   *nfoundvars = ncolsapplied;

   /* clear the price storage and reset statistics for price round */
   SCIP_CALL( GCGpricestoreClearCols(pricestore) );

   /* stop timing */
   SCIPstopClock(pricestore->scip, pricestore->priceclock);

   return SCIP_OKAY;
}

/** clears the price storage without adding the cols to the LP */
SCIP_RETCODE GCGpricestoreClearCols(
   GCG_PRICESTORE*       pricestore           /**< price storage */
   )
{
   int c;

   assert(pricestore != NULL);

   SCIPdebugMessage("clearing %d cols\n", pricestore->ncols);

   /* release cols */
   for( c = 0; c < pricestore->ncols; ++c )
   {
      GCGfreeGcgCol(&(pricestore->cols[c]));
   }

   /* reset counters */
   pricestore->ncols = 0;
   pricestore->nforcedcols = 0;
   pricestore->ncolsfoundround = 0;

   /* if we have just finished the initial LP construction, free the (potentially large) cols array */
   if( pricestore->infarkas )
   {
      SCIPfreeMemoryArrayNull(pricestore->scip, &pricestore->cols);
      pricestore->colssize = 0;
   }

   return SCIP_OKAY;
}

/** removes cols that are inefficacious w.r.t. the current LP solution from price storage without adding the cols to the LP */
SCIP_RETCODE GCGpricestoreRemoveInefficaciousCols(
   GCG_PRICESTORE*       pricestore,         /**< price storage */
   SCIP_Bool             root                /**< are we at the root node? */
   )
{
   int cnt;
   int c;

   assert( pricestore != NULL );

   /* check non-forced cols only */
   cnt = 0;
   c = pricestore->nforcedcols;
   while( c < pricestore->ncols )
   {
      if( !SCIPisDualfeasNegative(pricestore->scip, GCGcolGetRedcost(pricestore->cols[c])) )
      {
         SCIP_CALL( pricestoreDelCol(pricestore, c, TRUE) );
         ++cnt;
      }
      else
         ++c;
   }
   SCIPdebugMessage("removed %d non-efficacious cols\n", cnt);

   return SCIP_OKAY;
}

/** get cols in the price storage */
GCG_COL** GCGpricestoreGetCols(
   GCG_PRICESTORE*       pricestore           /**< price storage */
   )
{
   assert(pricestore != NULL);

   return pricestore->cols;
}

/** get number of cols in the price storage */
int GCGpricestoreGetNCols(
   GCG_PRICESTORE*       pricestore           /**< price storage */
   )
{
   assert(pricestore != NULL);

   return pricestore->ncols;
}

/** get total number of cols found so far */
int GCGpricestoreGetNColsFound(
   GCG_PRICESTORE*       pricestore           /**< price storage */
   )
{
   assert(pricestore != NULL);

   return pricestore->ncolsfound;
}

/** get number of cols found so far in current price round */
int GCGpricestoreGetNColsFoundRound(
   GCG_PRICESTORE*       pricestore           /**< price storage */
   )
{
   assert(pricestore != NULL);

   return pricestore->ncolsfoundround;
}

/** get total number of cols applied to the LPs */
int GCGpricestoreGetNColsApplied(
   GCG_PRICESTORE*       pricestore           /**< price storage */
   )
{
   assert(pricestore != NULL);

   return pricestore->ncolsapplied;
}

/** gets time in seconds used for pricing cols from the pricestore */
SCIP_Real GCGpricestoreGetTime(
   GCG_PRICESTORE*       pricestore           /**< price storage */
   )
{
   assert(pricestore != NULL);

   return SCIPclockGetTime(pricestore->priceclock);
}

