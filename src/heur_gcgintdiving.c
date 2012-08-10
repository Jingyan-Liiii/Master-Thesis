/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                           */
/*                  This file is part of the program                         */
/*          GCG --- Generic Column Generation                                */
/*                  a Dantzig-Wolfe decomposition based extension            */
/*                  of the branch-cut-and-price framework                    */
/*         SCIP --- Solving Constraint Integer Programs                      */
/*                                                                           */
/* Copyright (C) 2010-2012 Operations Research, RWTH Aachen University       */
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

/**@file   heur_gcgintdiving.c
 * @brief  LP diving heuristic that fixes variables with integral LP value
 * @author Tobias Achterberg
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#include <assert.h>
#include <string.h>

#include "heur_gcgintdiving.h"
#include "cons_origbranch.h"
#include "relax_gcg.h"


#define HEUR_NAME             "gcgintdiving"
#define HEUR_DESC             "LP diving heuristic that fixes binary variables with large LP value to one"
#define HEUR_DISPCHAR         'n'
#define HEUR_PRIORITY         -1003500
#define HEUR_FREQ             -1
#define HEUR_FREQOFS          9
#define HEUR_MAXDEPTH         -1
#define HEUR_TIMING           SCIP_HEURTIMING_AFTERPLUNGE
#define HEUR_USESSUBSCIP      FALSE  /**< does the heuristic use a secondary SCIP instance? */


/*
 * Default parameter settings
 */

#define DEFAULT_MINRELDEPTH         0.0 /**< minimal relative depth to start diving */
#define DEFAULT_MAXRELDEPTH         1.0 /**< maximal relative depth to start diving */
#define DEFAULT_MAXLPITERQUOT      0.05 /**< maximal fraction of diving LP iterations compared to node LP iterations */
#define DEFAULT_MAXLPITEROFS       1000 /**< additional number of allowed LP iterations */
#define DEFAULT_MAXPRICEQUOT       0.00 /**< maximal fraction of pricing rounds compared to node pricing rounds */
#define DEFAULT_MAXPRICEOFS           0 /**< additional number of allowed pricing rounds (-1: no limit) */
#define DEFAULT_MAXDIVEUBQUOT       0.8 /**< maximal quotient (curlowerbound - lowerbound)/(cutoffbound - lowerbound)
                                         *   where diving is performed (0.0: no limit) */
#define DEFAULT_MAXDIVEAVGQUOT      0.0 /**< maximal quotient (curlowerbound - lowerbound)/(avglowerbound - lowerbound)
                                         *   where diving is performed (0.0: no limit) */
#define DEFAULT_MAXDIVEUBQUOTNOSOL  0.1 /**< maximal UBQUOT when no solution was found yet (0.0: no limit) */
#define DEFAULT_MAXDIVEAVGQUOTNOSOL 0.0 /**< maximal AVGQUOT when no solution was found yet (0.0: no limit) */
#define DEFAULT_BACKTRACK          TRUE /**< use one level of backtracking if infeasibility is encountered? */

#define MINLPITER                 10000 /**< minimal number of LP iterations allowed in each LP solving call */


/* locally defined heuristic data */
struct SCIP_HeurData
{
   SCIP_SOL*             sol;                /**< working solution */
   SCIP_Real             minreldepth;        /**< minimal relative depth to start diving */
   SCIP_Real             maxreldepth;        /**< maximal relative depth to start diving */
   SCIP_Real             maxlpiterquot;      /**< maximal fraction of diving LP iterations compared to node LP iterations */
   int                   maxlpiterofs;       /**< additional number of allowed LP iterations */
   SCIP_Real             maxpricequot;       /**< maximal fraction of pricing rounds compared to node pricing rounds */
   int                   maxpriceofs;        /**< additional number of allowed pricing rounds (-1: no limit) */
   SCIP_Real             maxdiveubquot;      /**< maximal quotient (curlowerbound - lowerbound)/(cutoffbound - lowerbound)
                                              *   where diving is performed (0.0: no limit) */
   SCIP_Real             maxdiveavgquot;     /**< maximal quotient (curlowerbound - lowerbound)/(avglowerbound - lowerbound)
                                              *   where diving is performed (0.0: no limit) */
   SCIP_Real             maxdiveubquotnosol; /**< maximal UBQUOT when no solution was found yet (0.0: no limit) */
   SCIP_Real             maxdiveavgquotnosol;/**< maximal AVGQUOT when no solution was found yet (0.0: no limit) */
   SCIP_Bool             backtrack;          /**< use one level of backtracking if infeasibility is encountered? */
   SCIP_Longint          nlpiterations;      /**< LP iterations used in this heuristic */
   int                   npricerounds;       /**< pricing rounds used in this heuristic */
   int                   nsuccess;           /**< number of runs that produced at least one feasible solution */
};


/*
 * local methods
 */


/*
 * Callback methods
 */

/** copy method for primal heuristic plugins (called when SCIP copies plugins) */
static
#define heurCopyGcgintdiving NULL

/** destructor of primal heuristic to free user data (called when SCIP is exiting) */
static
SCIP_DECL_HEURFREE(heurFreeGcgintdiving) /*lint --e{715}*/
{  /*lint --e{715}*/
   SCIP_HEURDATA* heurdata;

   assert(heur != NULL);
   assert(strcmp(SCIPheurGetName(heur), HEUR_NAME) == 0);
   assert(scip != NULL);

   /* free heuristic data */
   heurdata = SCIPheurGetData(heur);
   assert(heurdata != NULL);
   SCIPfreeMemory(scip, &heurdata);
   SCIPheurSetData(heur, NULL);

   return SCIP_OKAY;
}


/** initialization method of primal heuristic (called after problem was transformed) */
static
SCIP_DECL_HEURINIT(heurInitGcgintdiving) /*lint --e{715}*/
{  /*lint --e{715}*/
   SCIP_HEURDATA* heurdata;

   assert(heur != NULL);
   assert(strcmp(SCIPheurGetName(heur), HEUR_NAME) == 0);

   /* get heuristic data */
   heurdata = SCIPheurGetData(heur);
   assert(heurdata != NULL);

   /* create working solution */
   SCIP_CALL( SCIPcreateSol(scip, &heurdata->sol, heur) );

   /* initialize data */
   heurdata->nlpiterations = 0;
   heurdata->nsuccess = 0;

   return SCIP_OKAY;
}


/** deinitialization method of primal heuristic (called before transformed problem is freed) */
static
SCIP_DECL_HEUREXIT(heurExitGcgintdiving) /*lint --e{715}*/
{  /*lint --e{715}*/
   SCIP_HEURDATA* heurdata;

   assert(heur != NULL);
   assert(strcmp(SCIPheurGetName(heur), HEUR_NAME) == 0);

   /* get heuristic data */
   heurdata = SCIPheurGetData(heur);
   assert(heurdata != NULL);

   /* free working solution */
   SCIP_CALL( SCIPfreeSol(scip, &heurdata->sol) );

   return SCIP_OKAY;
}


/** execution method of primal heuristic */
static
SCIP_DECL_HEUREXEC(heurExecGcgintdiving) /*lint --e{715}*/
{  /*lint --e{715}*/
   SCIP* masterprob;
   SCIP_HEURDATA* heurdata;
   SCIP_CONS* probingcons;
   SCIP_LPSOLSTAT lpsolstat;
   SCIP_NODE* probingnode;
   SCIP_VAR** pseudocands;
   SCIP_VAR** fixcands;
   SCIP_Real* fixcandscores;
   SCIP_Real searchubbound;
   SCIP_Real searchavgbound;
   SCIP_Real searchbound;
   SCIP_Real lpobj;
   SCIP_Real objval;
   SCIP_Bool lperror;
   SCIP_Bool lpsolved;
   SCIP_Bool cutoff;
   SCIP_Bool feasible;
   SCIP_Bool backtracked;
   SCIP_Longint ncalls;
   SCIP_Longint nsolsfound;
   SCIP_Longint nlpiterations;
   SCIP_Longint maxnlpiterations;
   int maxpricerounds;
   int npricerounds;
   int totalpricerounds;
   int nfixcands;
   int nbinfixcands;
   int depth;
   int maxdepth;
   int maxdivedepth;
   int divedepth;
   int nextcand;
   int c;

   assert(heur != NULL);
   assert(strcmp(SCIPheurGetName(heur), HEUR_NAME) == 0);
   assert(scip != NULL);
   assert(result != NULL);

   /* get master problem */
   masterprob = GCGrelaxGetMasterprob(scip);
   assert(masterprob != NULL);

   *result = SCIP_DELAYED;

   /* only call heuristic, if an optimal LP solution is at hand */
   if( SCIPgetStage(masterprob) > SCIP_STAGE_SOLVING ||
      !SCIPhasCurrentNodeLP(masterprob) || SCIPgetLPSolstat(masterprob) != SCIP_LPSOLSTAT_OPTIMAL )
      return SCIP_OKAY;

   /* only call heuristic, if the LP solution is basic (which allows fast resolve in diving) */
   if( !SCIPisLPSolBasic(masterprob) )
      return SCIP_OKAY;

   /* don't dive two times at the same node */
   if( SCIPgetLastDivenode(masterprob) == SCIPgetNNodes(masterprob) && SCIPgetDepth(masterprob) > 0 )
      return SCIP_OKAY;

   *result = SCIP_DIDNOTRUN;

   /* get heuristic's data */
   heurdata = SCIPheurGetData(heur);
   assert(heurdata != NULL);

   /* only try to dive, if we are in the correct part of the tree, given by minreldepth and maxreldepth */
   depth = SCIPgetDepth(scip);
   maxdepth = SCIPgetMaxDepth(scip);
   maxdepth = MAX(maxdepth, 100);
   if( depth < heurdata->minreldepth*maxdepth || depth > heurdata->maxreldepth*maxdepth )
      return SCIP_OKAY;

   /* calculate the maximal number of LP iterations until heuristic is aborted */
   nlpiterations = SCIPgetNNodeLPIterations(scip);
   ncalls = SCIPheurGetNCalls(heur);
   nsolsfound = 10*SCIPheurGetNBestSolsFound(heur) + heurdata->nsuccess;
   maxnlpiterations = (SCIP_Longint)((1.0 + 10.0*(nsolsfound+1.0)/(ncalls+1.0)) * heurdata->maxlpiterquot * nlpiterations);
   maxnlpiterations += heurdata->maxlpiterofs;

   /* don't try to dive, if we took too many LP iterations during diving */
   if( heurdata->nlpiterations >= maxnlpiterations )
      return SCIP_OKAY;

   /* allow at least a certain number of LP iterations in this dive */
   maxnlpiterations = MAX(maxnlpiterations, heurdata->nlpiterations + MINLPITER);

   /** @todo limit number of pricing rounds, play with parameters */
   if( heurdata->maxpriceofs > -1 )
   {
      npricerounds = SCIPgetNPriceRounds(masterprob);
      SCIPdebugMessage("GCG intdiving - pricing rounds at this node: %d\n", npricerounds);
      maxpricerounds = (int)((1.0 + 10.0*(nsolsfound+1.0)/(ncalls+1.0)) * heurdata->maxpricequot * npricerounds);
      maxpricerounds += heurdata->maxpriceofs;
   }
   else
      maxpricerounds = -1;

   SCIPdebugMessage("Maximum number of LP iters and price rounds: %"SCIP_LONGINT_FORMAT", %d\n", maxnlpiterations, maxpricerounds);

   /* get unfixed integer variables */
   SCIP_CALL( SCIPgetPseudoBranchCands(scip, &pseudocands, &nfixcands, NULL) );

   /* don't try to dive, if there are no fractional variables */
   if( nfixcands == 0 )
      return SCIP_OKAY;

   /* calculate the objective search bound */
   if( SCIPgetNSolsFound(scip) == 0 )
   {
      if( heurdata->maxdiveubquotnosol > 0.0 )
         searchubbound = SCIPgetLowerbound(scip)
            + heurdata->maxdiveubquotnosol * (SCIPgetCutoffbound(scip) - SCIPgetLowerbound(scip));
      else
         searchubbound = SCIPinfinity(scip);
      if( heurdata->maxdiveavgquotnosol > 0.0 )
         searchavgbound = SCIPgetLowerbound(scip)
            + heurdata->maxdiveavgquotnosol * (SCIPgetAvgLowerbound(scip) - SCIPgetLowerbound(scip));
      else
         searchavgbound = SCIPinfinity(scip);
   }
   else
   {
      if( heurdata->maxdiveubquot > 0.0 )
         searchubbound = SCIPgetLowerbound(scip)
            + heurdata->maxdiveubquot * (SCIPgetCutoffbound(scip) - SCIPgetLowerbound(scip));
      else
         searchubbound = SCIPinfinity(scip);
      if( heurdata->maxdiveavgquot > 0.0 )
         searchavgbound = SCIPgetLowerbound(scip)
            + heurdata->maxdiveavgquot * (SCIPgetAvgLowerbound(scip) - SCIPgetLowerbound(scip));
      else
         searchavgbound = SCIPinfinity(scip);
   }
   searchbound = MIN(searchubbound, searchavgbound);
   if( SCIPisObjIntegral(scip) )
      searchbound = SCIPceil(scip, searchbound);

   /* calculate the maximal diving depth: 10 * min{number of integer variables, max depth} */
   maxdivedepth = SCIPgetNBinVars(scip) + SCIPgetNIntVars(scip);
   maxdivedepth = MIN(maxdivedepth, maxdepth);
   maxdivedepth *= 10;

   *result = SCIP_DIDNOTFIND;

   /* start diving */
   SCIP_CALL( SCIPstartProbing(scip) );
   SCIP_CALL( GCGrelaxStartProbing(scip, heur) );

   /* enables collection of variable statistics during probing */
   SCIPenableVarHistory(scip);

   SCIPdebugMessage("(node %"SCIP_LONGINT_FORMAT") executing GCG intdiving heuristic: depth=%d, %d non-fixed, dualbound=%g, searchbound=%g\n",
      SCIPgetNNodes(scip), SCIPgetDepth(scip), nfixcands, SCIPgetDualbound(scip), SCIPretransformObj(scip, searchbound));

   /* copy the pseudo candidates into own array, because we want to reorder them */
   SCIP_CALL( SCIPduplicateBufferArray(scip, &fixcands, pseudocands, nfixcands) );

   /* sort non-fixed variables by non-increasing inference score, but prefer binaries over integers in any case */
   SCIP_CALL( SCIPallocBufferArray(scip, &fixcandscores, nfixcands) );
   nbinfixcands = 0;
   for( c = 0; c < nfixcands; ++c )
   {
      SCIP_VAR* var;
      SCIP_Real score;
      int colveclen;
      int left;
      int right;
      int i;

      assert(c >= nbinfixcands);
      var = fixcands[c];
      assert(SCIPvarIsIntegral(var));
      colveclen = (SCIPvarGetStatus(var) == SCIP_VARSTATUS_COLUMN ? SCIPcolGetNNonz(SCIPvarGetCol(var)) : 0);
      if( SCIPvarIsBinary(var) )
      {
         score = 500.0 * SCIPvarGetNCliques(var, TRUE) + 100.0 * SCIPvarGetNImpls(var, TRUE)
            + SCIPgetVarAvgInferenceScore(scip, var) + (SCIP_Real)colveclen/100.0;

         /* shift the non-binary variables one slot to the right */
         for( i = c; i > nbinfixcands; --i )
         {
            fixcands[i] = fixcands[i-1];
            fixcandscores[i] = fixcandscores[i-1];
         }
         /* put the new candidate into the first nbinfixcands slot */
         left = 0;
         right = nbinfixcands;
         nbinfixcands++;
      }
      else
      {
         score = 5.0 * (SCIPvarGetNCliques(var, FALSE) + SCIPvarGetNCliques(var, TRUE))
            + SCIPvarGetNImpls(var, FALSE) + SCIPvarGetNImpls(var, TRUE) + SCIPgetVarAvgInferenceScore(scip, var)
            + (SCIP_Real)colveclen/10000.0;

         /* put the new candidate in the slots after the binary candidates */
         left = nbinfixcands;
         right = c;
      }
      for( i = right; i > left && score > fixcandscores[i-1]; --i )
      {
         fixcands[i] = fixcands[i-1];
         fixcandscores[i] = fixcandscores[i-1];
      }
      fixcands[i] = var;
      fixcandscores[i] = score;
      SCIPdebugMessage("  <%s>: ncliques=%d/%d, nimpls=%d/%d, inferencescore=%g, colveclen=%d  ->  score=%g\n",
         SCIPvarGetName(var), SCIPvarGetNCliques(var, FALSE), SCIPvarGetNCliques(var, TRUE),
         SCIPvarGetNImpls(var, FALSE), SCIPvarGetNImpls(var, TRUE), SCIPgetVarAvgInferenceScore(scip, var),
         colveclen, score);
   }
   SCIPfreeBufferArray(scip, &fixcandscores);

   /* get LP objective value */
   lpsolstat = SCIP_LPSOLSTAT_OPTIMAL;
   objval = SCIPgetRelaxSolObj(scip);
   lpobj = objval;

   /* dive as long we are in the given objective, depth and iteration limits, but if possible, we dive at least with
    * the depth 10
    */
   lperror = FALSE;
   cutoff = FALSE;
   divedepth = 0;
   npricerounds = 0;
   totalpricerounds = 0;
   nextcand = 0;
   while( !lperror && !cutoff && lpsolstat == SCIP_LPSOLSTAT_OPTIMAL
      && (divedepth < 10
         || (divedepth < maxdivedepth && heurdata->nlpiterations < maxnlpiterations && objval < searchbound))
      && !SCIPisStopped(scip) )
   {
      SCIP_VAR* var;
      SCIP_Real bestsolval;
      SCIP_Real bestfixval;
      int bestcand;
      SCIP_Longint nnewlpiterations;
      SCIP_Longint nnewdomreds;

      SCIP_CALL( SCIPnewProbingNode(scip) );
      divedepth++;
      nnewlpiterations = 0;
      nnewdomreds = 0;

      /* fix binary variable that is closest to 1 in the LP solution to 1;
       * if all binary variables are fixed, fix integer variable with least fractionality in LP solution
       */
      bestcand = -1;
      bestsolval = -1.0;
      bestfixval = 1.0;

      /* look in the binary variables for fixing candidates */
      for( c = nextcand; c < nbinfixcands; ++c )
      {
         SCIP_Real solval;

         var = fixcands[c];

         /* ignore already fixed variables */
         if( var == NULL )
            continue;
         if( SCIPvarGetLbLocal(var) > 0.5 || SCIPvarGetUbLocal(var) < 0.5 )
         {
            fixcands[c] = NULL;
            continue;
         }

         /* get the LP solution value */
         solval = SCIPgetRelaxSolVal(scip, var);

         if( solval > bestsolval )
         {
            bestcand = c;
            bestfixval = 1.0;
            bestsolval = solval;
            if( SCIPisGE(scip, bestsolval, 1.0) )
            {
               /* we found an unfixed binary variable with LP solution value of 1.0 - there cannot be a better candidate */
               break;
            }
            else if( SCIPisLE(scip, bestsolval, 0.0) )
            {
               /* the variable is currently at 0.0 - this is the only situation where we want to fix it to 0.0 */
               bestfixval = 0.0;
            }
         }
      }

      /* if all binary variables are fixed, look in the integer variables for a fixing candidate */
      if( bestcand == -1 )
      {
         SCIP_Real bestfrac;

         bestfrac = SCIP_INVALID;
         for( c = MAX(nextcand, nbinfixcands); c < nfixcands; ++c )
         {
            SCIP_Real solval;
            SCIP_Real frac;

            var = fixcands[c];

            /* ignore already fixed variables */
            if( var == NULL )
               continue;
            if( SCIPvarGetUbLocal(var) - SCIPvarGetLbLocal(var) < 0.5 )
            {
               fixcands[c] = NULL;
               continue;
            }

            /* get the LP solution value */
            solval = SCIPgetRelaxSolVal(scip, var);
            frac = SCIPfrac(scip, solval);

            /* ignore integer variables that are currently integral */
            if( SCIPisFeasFracIntegral(scip, frac) )
               continue;

            if( frac < bestfrac )
            {
               bestcand = c;
               bestsolval = solval;
               bestfrac = frac;
               bestfixval = SCIPfloor(scip, bestsolval + 0.5);
               if( SCIPisZero(scip, bestfrac) )
               {
                  /* we found an unfixed integer variable with integral LP solution value */
                  break;
               }
            }
         }
      }
      assert(-1 <= bestcand && bestcand < nfixcands);

      /* if there is no unfixed candidate left, we are done */
      if( bestcand == -1 )
         break;

      var = fixcands[bestcand];
      assert(var != NULL);
      assert(SCIPvarIsIntegral(var));
      assert(SCIPvarGetUbLocal(var) - SCIPvarGetLbLocal(var) > 0.5);
      assert(SCIPisGE(scip, bestfixval, SCIPvarGetLbLocal(var)));
      assert(SCIPisLE(scip, bestfixval, SCIPvarGetUbLocal(var)));

      backtracked = FALSE;
      do
      {
         /* if the variable is already fixed or if the solution value is outside the domain, numerical troubles may have
          * occured or variable was fixed by propagation while backtracking => Abort diving!
          */
         if( SCIPvarGetLbLocal(var) >= SCIPvarGetUbLocal(var) - 0.5 )
         {
            SCIPdebugMessage("Selected variable <%s> already fixed to [%g,%g], diving aborted \n",
               SCIPvarGetName(var), SCIPvarGetLbLocal(var), SCIPvarGetUbLocal(var));
            cutoff = TRUE;
            break;
         }
         if( SCIPisFeasLT(scip, bestfixval, SCIPvarGetLbLocal(var)) || SCIPisFeasGT(scip, bestfixval, SCIPvarGetUbLocal(var)) )
         {
            SCIPdebugMessage("selected variable's <%s> solution value is outside the domain [%g,%g] (solval: %.9f), diving aborted\n",
               SCIPvarGetName(var), SCIPvarGetLbLocal(var), SCIPvarGetUbLocal(var), bestfixval);
            assert(backtracked);
            break;
         }

         probingnode = SCIPgetCurrentNode(scip);

         /* apply fixing of best candidate */
         SCIPdebugMessage("  dive %d/%d, LP iter %"SCIP_LONGINT_FORMAT"/%"SCIP_LONGINT_FORMAT", pricerounds %d/%d, %d unfixed: var <%s>, sol=%g, oldbounds=[%g,%g], fixed to %g\n",
            divedepth, maxdivedepth, heurdata->nlpiterations, maxnlpiterations, totalpricerounds, maxpricerounds, SCIPgetNPseudoBranchCands(scip),
            SCIPvarGetName(var), bestsolval, SCIPvarGetLbLocal(var), SCIPvarGetUbLocal(var), bestfixval);

         SCIP_CALL( GCGcreateConsOrigbranch(scip, &probingcons, "probingcons", probingnode,
            GCGconsOrigbranchGetActiveCons(scip), NULL, NULL) );
         SCIP_CALL( SCIPaddConsNode(scip, probingnode, probingcons, NULL) );
         SCIP_CALL( SCIPreleaseCons(scip, &probingcons) );
         SCIP_CALL( SCIPfixVarProbing(scip, var, bestfixval) );

         /* apply domain propagation */
         SCIP_CALL( SCIPpropagateProbing(scip, 0, &cutoff, &nnewdomreds) );
         if( !cutoff )
         {
            /* if the best candidate was just fixed to its LP value and no domain reduction was found, the LP solution
             * stays valid, and the LP does not need to be resolved
             */
            if( nnewdomreds > 0 || !SCIPisEQ(scip, bestsolval, bestfixval) )
            {
            /* resolve the diving LP */
               /* Errors in the LP solver should not kill the overall solving process, if the LP is just needed for a heuristic.
                * Hence in optimized mode, the return code is caught and a warning is printed, only in debug mode, SCIP will stop.
                */
#ifdef NDEBUG
            SCIP_RETCODE retstat;
            if( maxpricerounds == 0 )
               retstat = GCGrelaxPerformProbing(scip, MAX((int)(maxnlpiterations - heurdata->nlpiterations), MINLPITER), &nlpiterations, &lpobj, &lpsolved, &lperror, &cutoff, &feasible);
            else
            {
               retstat = GCGrelaxPerformProbingWithPricing(scip, maxpricerounds == -1 ? -1 : maxpricerounds - totalpricerounds,
                     &nlpiterations, &npricerounds, &lpobj, &lpsolved, &lperror, &cutoff, &feasible);
               npricerounds = 0;
            }
            if( retstat != SCIP_OKAY )
            {
               SCIPwarningMessage(scip, "Error while solving LP in GCG intdiving heuristic; LP solve terminated with code <%d>\n",retstat);
            }
#else
            if( maxpricerounds == 0 )
               SCIP_CALL( GCGrelaxPerformProbing(scip, MAX((int)(maxnlpiterations - heurdata->nlpiterations), MINLPITER), &nlpiterations, &lpobj, &lpsolved, &lperror, &cutoff, &feasible) );
            else
            {
               SCIP_CALL( GCGrelaxPerformProbingWithPricing(scip, maxpricerounds == -1 ? -1 : maxpricerounds - totalpricerounds,
                  &nlpiterations, &npricerounds, &lpobj, &lpsolved, &lperror, &cutoff, &feasible) );
               npricerounds = 0;
            }
#endif

               if( lperror || !lpsolved )
                  break;

               /* update iteration count */
               heurdata->nlpiterations += nlpiterations;
               heurdata->npricerounds += npricerounds;
               totalpricerounds += npricerounds;

               /* get LP solution status */
               lpsolstat = SCIPgetLPSolstat(masterprob);
//               cutoff = (lpsolstat == SCIP_LPSOLSTAT_OBJLIMIT || lpsolstat == SCIP_LPSOLSTAT_INFEASIBLE);
            }
         }

         /* perform backtracking if a cutoff was detected */
         if( cutoff && !backtracked && heurdata->backtrack )
         {
            SCIPdebugMessage("  *** cutoff detected at level %d - backtracking\n", SCIPgetProbingDepth(scip));
            SCIP_CALL( SCIPbacktrackProbing(scip, SCIPgetProbingDepth(scip)-1) );
            SCIP_CALL( SCIPbacktrackProbing(masterprob, SCIPgetProbingDepth(scip)) );
            SCIP_CALL( SCIPnewProbingNode(scip) );

            bestfixval = SCIPvarIsBinary(var)
               ? 1.0 - bestfixval
               : (SCIPisGT(scip, bestsolval, bestfixval) && SCIPisFeasLE(scip, bestfixval + 1, SCIPvarGetUbLocal(var)) ? bestfixval + 1 : bestfixval - 1);

            backtracked = TRUE;
         }
         else
            backtracked = FALSE;
      }
      while( backtracked );

      if( !lperror && !cutoff && lpsolstat == SCIP_LPSOLSTAT_OPTIMAL )
      {
         SCIP_Bool success;

         /* get new objective value */
         objval = lpobj;

         if( nnewlpiterations > 0 || !SCIPisEQ(scip, bestsolval, bestfixval) )
         {
            /* we must start again with the first candidate, since the LP solution changed */
            nextcand = 0;

            /* create solution from diving LP and try to round it */
            SCIP_CALL( SCIPlinkRelaxSol(scip, heurdata->sol) );
            SCIP_CALL( SCIProundSol(scip, heurdata->sol, &success) );
            if( success )
            {
               SCIPdebugMessage("GCG intdiving found roundable primal solution: obj=%g\n",
                  SCIPgetSolOrigObj(scip, heurdata->sol));

               /* try to add solution to SCIP */
//               SCIP_CALL( SCIPtrySol(scip, heurdata->sol, FALSE, FALSE, FALSE, FALSE, &success) );
#ifdef SCIP_DEBUG
               SCIP_CALL( SCIPtrySol(scip, heurdata->sol, TRUE, TRUE, TRUE, TRUE, &success) );
#else
               SCIP_CALL( SCIPtrySol(scip, heurdata->sol, FALSE, TRUE, TRUE, TRUE, &success) );
#endif

               /* check, if solution was feasible and good enough */
               if( success )
               {
                  SCIPdebugMessage(" -> solution was feasible and good enough\n");
                  *result = SCIP_FOUNDSOL;
               }
            }
         }
         else
            nextcand = bestcand+1; /* continue with the next candidate in the following loop */
      }
      SCIPdebugMessage("   -> lpsolstat=%d, objval=%g/%g\n", lpsolstat, objval, searchbound);
   }

   /* free temporary memory */
   SCIPfreeBufferArray(scip, &fixcands);

   /* end diving */
   SCIP_CALL( SCIPendProbing(scip) );
   SCIP_CALL( GCGrelaxEndProbing(scip) );

   if( *result == SCIP_FOUNDSOL )
      heurdata->nsuccess++;

   SCIPdebugMessage("(node %"SCIP_LONGINT_FORMAT") finished GCG intdiving heuristic: %d fractionals, dive %d/%d, LP iter %"SCIP_LONGINT_FORMAT"/%"SCIP_LONGINT_FORMAT", pricerounds %d/%d, objval=%g/%g, lpsolstat=%d, cutoff=%u\n",
         SCIPgetNNodes(scip), nfixcands, divedepth, maxdivedepth, heurdata->nlpiterations, maxnlpiterations, totalpricerounds, maxpricerounds,
         SCIPretransformObj(scip, objval), SCIPretransformObj(scip, searchbound), lpsolstat, cutoff);

   return SCIP_OKAY;
}


/*
 * heuristic specific interface methods
 */

/** creates the GCG intdiving heuristic and includes it in SCIP */
SCIP_RETCODE SCIPincludeHeurGcgintdiving(
   SCIP*                 scip                /**< SCIP data structure */
   )
{
   SCIP_HEURDATA* heurdata;
   SCIP_HEUR* heur;

   /* create GCG intdiving primal heuristic data */
   SCIP_CALL( SCIPallocMemory(scip, &heurdata) );

   /* include primal heuristic */
   SCIP_CALL( SCIPincludeHeurBasic(scip, &heur,
         HEUR_NAME, HEUR_DESC, HEUR_DISPCHAR, HEUR_PRIORITY, HEUR_FREQ, HEUR_FREQOFS,
         HEUR_MAXDEPTH, HEUR_TIMING, HEUR_USESSUBSCIP, heurExecGcgintdiving, heurdata) );

   assert(heur != NULL);

   /* set non-NULL pointers to callback methods */
   SCIP_CALL( SCIPsetHeurFree(scip, heur, heurFreeGcgintdiving) );
   SCIP_CALL( SCIPsetHeurInit(scip, heur, heurInitGcgintdiving) );
   SCIP_CALL( SCIPsetHeurExit(scip, heur, heurExitGcgintdiving) );

   /* gcgintdiving heuristic parameters */
   SCIP_CALL( SCIPaddRealParam(scip,
         "heuristics/gcgintdiving/minreldepth",
         "minimal relative depth to start diving",
         &heurdata->minreldepth, TRUE, DEFAULT_MINRELDEPTH, 0.0, 1.0, NULL, NULL) );
   SCIP_CALL( SCIPaddRealParam(scip,
         "heuristics/gcgintdiving/maxreldepth",
         "maximal relative depth to start diving",
         &heurdata->maxreldepth, TRUE, DEFAULT_MAXRELDEPTH, 0.0, 1.0, NULL, NULL) );
   SCIP_CALL( SCIPaddRealParam(scip,
         "heuristics/gcgintdiving/maxlpiterquot",
         "maximal fraction of diving LP iterations compared to node LP iterations",
         &heurdata->maxlpiterquot, FALSE, DEFAULT_MAXLPITERQUOT, 0.0, SCIP_REAL_MAX, NULL, NULL) );
   SCIP_CALL( SCIPaddIntParam(scip,
         "heuristics/gcgintdiving/maxlpiterofs",
         "additional number of allowed LP iterations",
         &heurdata->maxlpiterofs, FALSE, DEFAULT_MAXLPITEROFS, 0, INT_MAX, NULL, NULL) );
   SCIP_CALL( SCIPaddRealParam(scip,
         "heuristics/gcgintdiving/maxpricequot",
         "maximal fraction of pricing rounds compared to node pricing rounds",
         &heurdata->maxpricequot, FALSE, DEFAULT_MAXPRICEQUOT, 0.0, SCIP_REAL_MAX, NULL, NULL) );
      SCIP_CALL( SCIPaddIntParam(scip,
         "heuristics/gcgintdiving/maxpriceofs",
         "additional number of allowed pricing rounds (-1: no limit)",
         &heurdata->maxpriceofs, FALSE, DEFAULT_MAXPRICEOFS, -1, INT_MAX, NULL, NULL) );
   SCIP_CALL( SCIPaddRealParam(scip,
         "heuristics/gcgintdiving/maxdiveubquot",
         "maximal quotient (curlowerbound - lowerbound)/(cutoffbound - lowerbound) where diving is performed (0.0: no limit)",
         &heurdata->maxdiveubquot, TRUE, DEFAULT_MAXDIVEUBQUOT, 0.0, 1.0, NULL, NULL) );
   SCIP_CALL( SCIPaddRealParam(scip,
         "heuristics/gcgintdiving/maxdiveavgquot",
         "maximal quotient (curlowerbound - lowerbound)/(avglowerbound - lowerbound) where diving is performed (0.0: no limit)",
         &heurdata->maxdiveavgquot, TRUE, DEFAULT_MAXDIVEAVGQUOT, 0.0, SCIP_REAL_MAX, NULL, NULL) );
   SCIP_CALL( SCIPaddRealParam(scip,
         "heuristics/gcgintdiving/maxdiveubquotnosol",
         "maximal UBQUOT when no solution was found yet (0.0: no limit)",
         &heurdata->maxdiveubquotnosol, TRUE, DEFAULT_MAXDIVEUBQUOTNOSOL, 0.0, 1.0, NULL, NULL) );
   SCIP_CALL( SCIPaddRealParam(scip,
         "heuristics/gcgintdiving/maxdiveavgquotnosol",
         "maximal AVGQUOT when no solution was found yet (0.0: no limit)",
         &heurdata->maxdiveavgquotnosol, TRUE, DEFAULT_MAXDIVEAVGQUOTNOSOL, 0.0, SCIP_REAL_MAX, NULL, NULL) );
   SCIP_CALL( SCIPaddBoolParam(scip,
         "heuristics/gcgintdiving/backtrack",
         "use one level of backtracking if infeasibility is encountered?",
         &heurdata->backtrack, FALSE, DEFAULT_BACKTRACK, NULL, NULL) );

   return SCIP_OKAY;
}

