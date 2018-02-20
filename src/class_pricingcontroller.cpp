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

/**@file   class_pricingcontroller.cpp
 * @brief  pricing controller managing the pricing strategy
 * @author Christian Puchert
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#include "class_pricingcontroller.h"
#include "pricer_gcg.h"
#include "class_pricingtype.h"
#include "gcg.h"
#include "scip_misc.h"
#include "pub_gcgpqueue.h"
#include "pub_pricingjob.h"
#include "pricingjob.h"

#include "scip/scip.h"
#include "objscip/objscip.h"

#include <exception>

#define DEFAULT_USEHEURPRICING           FALSE      /**< should heuristic pricing be used */
#define DEFAULT_SORTING                  'r'          /**< order by which the pricing problems should be sorted:
                                                     *    'i'ndices
                                                     *    'd'ual solutions of convexity constraints
                                                     *    'r'eliability from all previous rounds
                                                     *    reliability from the 'l'ast nroundscol rounds
                                                     */
#define DEFAULT_NROUNDSCOL               15
#define DEFAULT_RELMAXSUCCESSFULPROBS    1.0        /**< maximal percentage of pricing problems that need to be solved successfully */
#define DEFAULT_CHUNKSIZE                INT_MAX    /**< maximal number of pricing problems to be solved during one pricing loop */
#define DEFAULT_EAGERFREQ                10         /**< frequency at which all pricingproblems should be solved (0 to disable) */
#define DEFAULT_JOBTIMELIMIT             1e+20      /**< time limit per iteration of a pricing job */

#define SCIP_CALL_EXC(x)   do                                                                                 \
                       {                                                                                      \
                          SCIP_RETCODE _retcode_;                                                             \
                          if( (_retcode_ = (x)) !=  SCIP_OKAY )                                               \
                          {                                                                                   \
                             SCIPerrorMessage("Error <%d> in function call\n", _retcode_);                    \
                             throw std::exception();                                                          \
                          }                                                                                   \
                       }                                                                                      \
                       while( FALSE )


namespace gcg {

Pricingcontroller::Pricingcontroller(
   SCIP*                  scip
   )
{
   scip_ = scip;
   pricingjobs = NULL;
   npricingprobs = 0;

   sorting = DEFAULT_SORTING;
   nroundscol = DEFAULT_NROUNDSCOL;
   relmaxsuccessfulprobs = DEFAULT_RELMAXSUCCESSFULPROBS;
   chunksize = DEFAULT_CHUNKSIZE;
   eagerfreq = DEFAULT_EAGERFREQ;

   pqueue = NULL;
   nchunks = 1;
   curchunk = 0;

   pricingtype_ = NULL;

   eagerage = 0;
}

Pricingcontroller::~Pricingcontroller()
{
}

SCIP_RETCODE Pricingcontroller::addParameters()
{
   SCIP* origprob = GCGmasterGetOrigprob(scip_);
   
   SCIP_CALL( SCIPaddBoolParam(origprob, "pricing/masterpricer/useheurpricing",
         "should pricing be performed heuristically before solving the MIPs to optimality?",
         &useheurpricing, TRUE, DEFAULT_USEHEURPRICING, NULL, NULL) );

   SCIP_CALL( SCIPaddCharParam(origprob, "pricing/masterpricer/sorting",
         "order by which the pricing problems should be sorted ('i'ndices, 'd'ual solutions of convexity constraints, 'r'eliability from previous rounds, reliability from the 'l'ast nroundscol rounds)",
         &sorting, FALSE, DEFAULT_SORTING, "dilr", NULL, NULL) );

   SCIP_CALL( SCIPaddIntParam(origprob, "pricing/masterpricer/nroundscol",
         "number of previous pricing rounds for which the number of improving columns should be counted",
         &nroundscol, TRUE, DEFAULT_NROUNDSCOL, 1, INT_MAX, NULL, NULL) );

   SCIP_CALL( SCIPaddRealParam(origprob, "pricing/masterpricer/relmaxsuccessfulprobs",
         "maximal percentage of pricing problems that need to be solved successfully",
         &relmaxsuccessfulprobs, FALSE, DEFAULT_RELMAXSUCCESSFULPROBS, 0.0, 1.0, NULL, NULL) );

   SCIP_CALL( SCIPaddIntParam(origprob, "pricing/masterpricer/chunksize",
         "maximal number of pricing problems to be solved during one pricing loop",
         &chunksize, TRUE, DEFAULT_CHUNKSIZE, 1, INT_MAX, NULL, NULL) );
   
   SCIP_CALL( SCIPaddIntParam(origprob, "pricing/masterpricer/eagerfreq",
         "frequency at which all pricingproblems should be solved (0 to disable)",
         &eagerfreq, FALSE, DEFAULT_EAGERFREQ, 0, INT_MAX, NULL, NULL) );

   SCIP_CALL( SCIPaddRealParam(origprob, "pricing/masterpricer/jobtimelimit",
         "time limit per iteration of a pricing job",
         &jobtimelimit, FALSE, DEFAULT_JOBTIMELIMIT, 0.0, 1e+20, NULL, NULL) );

   return SCIP_OKAY;
}

/** comparison operator for pricing jobs w.r.t. their solution priority */
SCIP_DECL_SORTPTRCOMP(Pricingcontroller::comparePricingjobs)
{
   GCG_PRICINGJOB* pricingjob1;
   GCG_PRICINGJOB* pricingjob2;
   int score1;
   int score2;

   pricingjob1 = (GCG_PRICINGJOB*) elem1;
   pricingjob2 = (GCG_PRICINGJOB*) elem2;
   score1 = GCGpricingjobGetScore(pricingjob1);
   score2 = GCGpricingjobGetScore(pricingjob2);

   /** preliminary strategy: heuristic before exact, then sorting by score */
   if( GCGpricingjobIsHeuristic(pricingjob1) != GCGpricingjobIsHeuristic(pricingjob2) )
   {
      if( GCGpricingjobIsHeuristic(pricingjob1) )
         return -1;
      else
         return 1;
   }
   else
   {
      if( score1 >= score2 )
         return -1;
      else
         return 1;
   }

   return 0;
}

/** check if the pricing job is done */
SCIP_Bool Pricingcontroller::pricingjobIsDone(
   GCG_PRICINGJOB*       pricingjob          /**< pricing job */
   ) const
{
   return GCGpricingjobGetNImpCols(pricingjob) > 0
      || GCGpricingjobGetStatus(pricingjob) == SCIP_STATUS_OPTIMAL
      || GCGpricingjobGetStatus(pricingjob) == SCIP_STATUS_INFEASIBLE
      || GCGpricingjobGetStatus(pricingjob) == SCIP_STATUS_UNBOUNDED
      || GCGpricingjobGetStatus(pricingjob) == SCIP_STATUS_INFORUNBD;
}

/** check if the pricing job has terminated with a limit */
SCIP_Bool Pricingcontroller::pricingjobHasLimit(
   GCG_PRICINGJOB*       pricingjob          /**< pricing job */
   ) const
{
   return GCGpricingjobGetStatus(pricingjob) == SCIP_STATUS_NODELIMIT
      || GCGpricingjobGetStatus(pricingjob) == SCIP_STATUS_STALLNODELIMIT
      || GCGpricingjobGetStatus(pricingjob) == SCIP_STATUS_GAPLIMIT
      || GCGpricingjobGetStatus(pricingjob) == SCIP_STATUS_SOLLIMIT;
}

SCIP_RETCODE Pricingcontroller::initSol()
{
   SCIP* origprob = GCGmasterGetOrigprob(scip_);
   int actchunksize = MIN(chunksize, GCGgetNRelPricingprobs(origprob));
   int k = 0;

   npricingprobs = GCGgetNPricingprobs(origprob);
   nchunks = (int) SCIPceil(scip_, (SCIP_Real) GCGgetNRelPricingprobs(origprob) / actchunksize);
   curchunk = nchunks - 1;
   eagerage = 0;

   /* create pricing jobs */
   SCIP_CALL_EXC( SCIPallocBlockMemoryArray(scip_, &pricingjobs, npricingprobs) );
   for( int i = 0; i < npricingprobs; ++i )
   {
      if( GCGisPricingprobRelevant(origprob, i) )
      {
         SCIP_CALL_EXC( GCGpricingjobCreate(scip_, &pricingjobs[i], GCGgetPricingprob(origprob, i), i, k / actchunksize, nroundscol) );
         ++k;
      }
      else
         pricingjobs[i] = NULL;
   }

   SCIP_CALL_EXC( GCGpqueueCreate(&pqueue, npricingprobs, 2.0, comparePricingjobs) );

   return SCIP_OKAY;
}

SCIP_RETCODE Pricingcontroller::exitSol()
{
   GCGpqueueFree(&pqueue);

   for( int i = 0; i < npricingprobs; ++i )
   {
      if( pricingjobs[i] != NULL )
         GCGpricingjobFree(scip_, &pricingjobs[i]);
   }
   SCIPfreeBlockMemoryArray(scip_, &pricingjobs, npricingprobs);

   return SCIP_OKAY;
}

/** pricing initialization, called right at the beginning of pricing */
void Pricingcontroller::initPricing(
   PricingType*          pricingtype         /**< type of pricing */
   )
{
   pricingtype_ = pricingtype;

   curchunk = (curchunk + 1) % nchunks;
   startchunk = curchunk;

   SCIPdebugMessage("initialize pricing, chunk = %d/%d\n", curchunk+1, nchunks);
}

/** pricing deinitialization, called when pricing is finished */
void Pricingcontroller::exitPricing()
{
   for( int i = 0; i < npricingprobs; ++i )
      if( pricingjobs[i] != NULL )
         GCGpricingjobUpdateNColsround(pricingjobs[i], nroundscol);

   pricingtype_ = NULL;
}

/** setup the priority queue (done once per stabilization round): add all pricing jobs to be performed */
SCIP_RETCODE Pricingcontroller::setupPriorityQueue(
   SCIP_Real*            dualsolconv,        /**< dual solution values / Farkas coefficients of convexity constraints */
   int                   maxcols,            /**< maximum number of columns to be generated */
   SCIP_Real*            bestobjvals,
   SCIP_Real*            bestredcosts
   )
{
   int maxcolsprob = INT_MAX;

   if( pricingtype_->getType() == GCG_PRICETYPE_REDCOST && GCGisRootNode(scip_) )
      maxcolsprob = pricingtype_->getMaxcolsprobroot();
   else
      maxcolsprob = pricingtype_->getMaxcolsprob();

   SCIPdebugMessage("setup pricing queue, chunk = %d/%d\n", curchunk+1, nchunks);

   GCGpqueueClear(pqueue);

   for( int i = 0; i < npricingprobs; ++i )
   {
      if( pricingjobs[i] != NULL )
      {
         SCIP_CALL_EXC( GCGpricingjobSetup(scip_, pricingjobs[i], useheurpricing, maxcolsprob,
            sorting, nroundscol, dualsolconv[i], GCGpricerGetNPointsProb(scip_, i), GCGpricerGetNRaysProb(scip_, i), maxcols) );

         bestobjvals[i] = -SCIPinfinity(scip_);
         bestredcosts[i] = 0.0;

         if( GCGpricingjobGetChunk(pricingjobs[i]) == curchunk )
         {
            SCIP_CALL_EXC( GCGpqueueInsert(pqueue, (void*) pricingjobs[i]) );
         }
      }
   }

   return SCIP_OKAY;
}

/** get the next pricing job to be performed */
GCG_PRICINGJOB* Pricingcontroller::getNextPricingjob()
{
   return (GCG_PRICINGJOB*) GCGpqueueRemove(pqueue);
}

/** set an individual time limit for a pricing job */
SCIP_RETCODE Pricingcontroller::setPricingjobTimelimit(
   GCG_PRICINGJOB*       pricingjob          /**< pricing job */
   )
{
   SCIP* pricingscip = GCGpricingjobGetPricingscip(pricingjob);
   SCIP_Real mastertimelimit;
   SCIP_Real timelimit;

   SCIP_CALL( SCIPgetRealParam(scip_, "limits/time", &mastertimelimit) );

   /* The pricing job gets an additional solving time of 'jobtimelimit',
    * but not more than is left for solving the master problem, and not less than zero
    */
   timelimit = MAX(0, MIN(SCIPgetSolvingTime(pricingscip) + jobtimelimit, mastertimelimit - SCIPgetSolvingTime(scip_)));

//   SCIPdebugMessage("(Pricing prob %d) timelimit = %f\n", GCGpricingjobGetProbnr(pricingjob), timelimit);
   SCIP_CALL( SCIPsetRealParam(pricingscip, "limits/time", timelimit) );

   return SCIP_OKAY;
}

/** update result variables of a pricing job */
SCIP_RETCODE Pricingcontroller::updatePricingjob(
   GCG_PRICINGJOB*       pricingjob,         /**< pricing job */
   SCIP_STATUS           status,             /**< status after solving the pricing problem */
   SCIP_Real             lowerbound,         /**< lower bound returned by the pricing problem */
   GCG_COL**             cols,               /**< columns found by the last solving of the pricing problem */
   int                   ncols               /**< number of columns found */
   )
{
   SCIP_CALL( GCGpricingjobUpdate(scip_, pricingjob, status, lowerbound, cols, ncols) );

   return SCIP_OKAY;
}

/** decide whether a pricing job must be treated again */
void Pricingcontroller::evaluatePricingjob(
   GCG_PRICINGJOB*       pricingjob         /**< pricing job */
   )
{
   SCIPdebugMessage("Problem %d, status = %d\n", GCGpricingjobGetProbnr(pricingjob), GCGpricingjobGetStatus(pricingjob));

   /* If the pricing job has not yielded any improving column, possibly solve it again;
    * increase at least one of its limits, or solve it exactly if it was solved heuristically before
    */
   // @todo: update score of pricing job
   if( !pricingjobIsDone(pricingjob) )
   {
      SCIPdebugMessage("Problem %d has not yielded improving columns\n", GCGpricingjobGetProbnr(pricingjob));

      if( GCGpricingjobIsHeuristic(pricingjob) )
      {
         if( !pricingjobHasLimit(pricingjob) )
         {
            assert(GCGpricingjobGetStatus(pricingjob) == SCIP_STATUS_UNKNOWN);
            GCGpricingjobSetExact(pricingjob);
            SCIPdebugMessage("  -> solve exactly\n");
         }
         else
         {
            SCIPdebugMessage("  -> increase a limit\n");
         }
         SCIP_CALL_EXC( GCGpqueueInsert(pqueue, (void*) pricingjob) );
      }
   }
}

/** return whether the reduced cost is valid */
SCIP_Bool Pricingcontroller::redcostIsValid()
{
   SCIP_Bool optimal = TRUE;

   for( int i = 0; i < npricingprobs; ++i )
   {
      if( pricingjobs[i] == NULL )
         continue;

      assert(GCGpricingjobGetStatus(pricingjobs[i]) != SCIP_STATUS_INFEASIBLE);

      if( GCGpricingjobGetNImpCols(pricingjobs[i]) > 0 )
         return TRUE;
      else if( GCGpricingjobGetStatus(pricingjobs[i]) != SCIP_STATUS_OPTIMAL )
         optimal = FALSE;
   }

   return optimal;
}

/* return whether all pricing problems have been solved to optimality */
SCIP_Bool Pricingcontroller::pricingIsOptimal()
{
   for( int i = 0; i < npricingprobs; ++i )
   {
      if( pricingjobs[i] == NULL )
         continue;

      if( GCGpricingjobGetStatus(pricingjobs[i]) != SCIP_STATUS_OPTIMAL )
         return FALSE;
   }

   return TRUE;
}

/* return whether the current node is infeasible */
SCIP_Bool Pricingcontroller::pricingIsInfeasible()
{
   SCIP_Bool infeasible = (pricingtype_->getType() == GCG_PRICETYPE_FARKAS);

   for( int i = 0; i < npricingprobs; ++i )
   {
      if( pricingjobs[i] == NULL )
         continue;

      if( GCGpricingjobGetStatus(pricingjobs[i]) == SCIP_STATUS_INFEASIBLE )
         return TRUE;

      if( pricingtype_->getType() == GCG_PRICETYPE_FARKAS && (GCGpricingjobGetStatus(pricingjobs[i]) != SCIP_STATUS_OPTIMAL || GCGpricingjobGetNImpCols(pricingjobs[i]) > 0) )
         infeasible = FALSE;
   }

   return infeasible;
}

/** reset the lower bound of a pricing job */
void Pricingcontroller::resetPricingjobLowerbound(
   GCG_PRICINGJOB*       pricingjob          /**< pricing job */
   )
{
   GCGpricingjobSetLowerbound(pricingjob, -SCIPinfinity(scip_));
}

/** for all pricing jobs, move their columns to the column pool */
SCIP_RETCODE Pricingcontroller::moveColsToColpool(
   GCG_COLPOOL*          colpool,            /**< column pool */
   GCG_PRICESTORE*       pricestore,         /**< GCG pricing store */
   SCIP_Bool             usecolpool,         /**< use column pool? */
   SCIP_Bool             usepricestore       /**< use price store? */
   )
{
   SCIPdebugMessage("Move columns to column pool\n");

   for( int i = 0; i < npricingprobs; ++i )
   {
      if( pricingjobs[i] != NULL )
      {
         GCG_COL** cols = GCGpricingjobGetCols(pricingjobs[i]);
         int ncols = GCGpricingjobGetNCols(pricingjobs[i]);
         SCIP_Bool added;

         assert(cols != NULL || ncols == 0);

         for( int j = 0; j < ncols; ++j )
         {
            SCIPdebugMessage("  (prob %d) column %d/%d <%p>: ", GCGpricingjobGetProbnr(pricingjobs[i]), j+1, ncols, (void*) cols[j]);

            added = FALSE;

            if( usepricestore && SCIPisDualfeasNegative(scip_, GCGcolGetRedcost(cols[j])) )
            {
               SCIP_CALL( GCGcomputeColMastercoefs(scip_, cols[j]) );

               SCIP_CALL( GCGpricestoreAddCol(scip_, pricestore, cols[j], FALSE) );
               added = TRUE;
            }
            else if( usecolpool )
            {
               SCIP_CALL( GCGcolpoolAddCol(colpool, cols[j], &added) );
            }

            if( !added )
            {
               GCGfreeGcgCol(&cols[j]);
               SCIPdebugPrintf("freed.\n");
            }
            else
            {
               SCIPdebugPrintf("added to column pool or price store.\n");
            }

            cols[j] = NULL;
         }

         GCGpricingjobSetNCols(pricingjobs[i], 0);
      }
   }

   return SCIP_OKAY;
}

/** check if the next chunk of pricing problems is to be used */
SCIP_Bool Pricingcontroller::checkNextChunk()
{
   int nextchunk = (curchunk + 1) % nchunks;
   
   if( nextchunk == startchunk )
   {
      SCIPdebugMessage("not considering next chunk.\n");
      return FALSE;
   }
   else
   {
      SCIPdebugMessage("need considering next chunk = %d/%d\n", nextchunk+1, nchunks);
      curchunk = nextchunk;
      return TRUE;
   }
}

/** get best columns found by the pricing jobs */
void Pricingcontroller::getBestCols(
   GCG_COL**             cols                /**< column array to be filled */
   )
{
   for( int i = 0; i < npricingprobs; ++i )
   {
      if( pricingjobs[i] == NULL )
         cols[i] = NULL;
      else
      {
         assert(pricingjobs[i]->cols != NULL);
         assert(pricingjobs[i]->ncols > 0);
         cols[i] = pricingjobs[i]->cols[0];
      }
   }
}

/** get the sum over the dual values of convexity constraints */
SCIP_Real Pricingcontroller::getDualconvsum(
   PricingType*          pricetype           /**< type of pricing (reduced cost or Farkas) */
   )
{
   SCIP* origprob = GCGmasterGetOrigprob(scip_);
   SCIP_Real dualconvsum = 0.0;

   for( int i = 0; i < npricingprobs; ++i )
   {
      if( pricingjobs[i] != NULL && !(pricingjobs[i]->ncols > 0 && GCGcolIsRay(pricingjobs[i]->cols[0])))
         dualconvsum += GCGgetNIdenticalBlocks(origprob, i) * pricetype->consGetDual(scip_, GCGgetConvCons(origprob, i));
   }

   return dualconvsum;
}

/** free all columns of the pricing jobs */
void Pricingcontroller::freeCols()
{
   for( int i = 0; i < npricingprobs; ++i )
   {
      if( pricingjobs[i] != NULL )
      {
         GCGpricingjobFreeCols(pricingjobs[i]);
      }
   }
}

/** decide whether the pricing loop can be aborted */
SCIP_Bool Pricingcontroller::canPricingloopBeAborted(
   PricingType*          pricetype,          /**< type of pricing (reduced cost or Farkas) */
   int                   nfoundcols,         /**< number of negative reduced cost columns found so far */
   int                   nsolvedprobs,       /**< number of pricing problems solved so far */
   int                   nsuccessfulprobs,   /**< number of pricing problems solved successfully so far */
   SCIP_Bool             optimal             /**< optimal or heuristic pricing */
   ) const
{
   int nrelpricingprobs = GCGgetNRelPricingprobs(GCGmasterGetOrigprob(scip_));

   if( eagerage == eagerfreq )
      return FALSE;

   if( optimal )
      return pricetype->canOptimalPricingBeAborted(nfoundcols, nsolvedprobs, nsuccessfulprobs, relmaxsuccessfulprobs, nrelpricingprobs);
   else
      return pricetype->canHeuristicPricingBeAborted(nfoundcols, nsolvedprobs, nsuccessfulprobs, relmaxsuccessfulprobs, nrelpricingprobs);
}

void Pricingcontroller::resetEagerage()
{
   eagerage = 0;
}

void Pricingcontroller::increaseEagerage()
{
   if( eagerfreq > 0 )
      eagerage++;
}

} /* namespace gcg */
