/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                           */
/*                  This file is part of the program and library             */
/*         SCIP --- Solving Constraint Integer Programs                      */
/*                                                                           */
/*    Copyright (C) 2002-2009 Konrad-Zuse-Zentrum                            */
/*                            fuer Informationstechnik Berlin                */
/*                                                                           */
/*  SCIP is distributed under the terms of the ZIB Academic License.         */
/*                                                                           */
/*  You should have received a copy of the ZIB Academic License              */
/*  along with SCIP; see the file COPYING. If not email to scip@zib.de.      */
/*                                                                           */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
#pragma ident "@(#) $Id$"
//#define SCIP_DEBUG
/**@file   solver_mip.c
 * @brief  mip solver for pricing problems
 * @author Gerald Gamrath
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#include <assert.h>

#include "solver_mip.h"
#include "scip/cons_linear.h"
#include "scip/cons_knapsack.h"
#include "type_solver.h"
#include "pricer_gcg.h"



#define SOLVER_NAME          "mip"
#define SOLVER_DESC          "mip solver for pricing problems"
#define SOLVER_PRIORITY      0

#define DEFAULT_CHECKSOLS TRUE


/** branching data for branching decisions */
struct GCG_SolverData
{
   SCIP* origprob;
   SCIP_Real** solvals;
   SCIP_VAR*** solvars;
   SCIP_Real*  tmpsolvals;
   int* nsolvars;
   int nsols;
   int maxvars;

   SCIP_Bool checksols;
};

/* ensures size of solution arrays */
static
SCIP_RETCODE ensureSizeSolvers(
   SCIP*                 scip,
   GCG_SOLVERDATA*       solverdata,
   int                   nsols
   )
{
   int i;

   assert(scip != NULL);
   assert(solverdata != NULL);

   if ( solverdata->nsols < nsols )
   {
      SCIP_CALL( SCIPreallocMemoryArray(scip, &(solverdata->nsolvars), nsols) );
      SCIP_CALL( SCIPreallocMemoryArray(scip, &(solverdata->solvars), nsols) );
      SCIP_CALL( SCIPreallocMemoryArray(scip, &(solverdata->solvals), nsols) );

      for( i = solverdata->nsols; i < nsols; i++ )
      {
         solverdata->nsolvars[i] = 0;
         SCIP_CALL( SCIPallocMemoryArray(scip, &(solverdata->solvars[i]), solverdata->maxvars) );
         SCIP_CALL( SCIPallocMemoryArray(scip, &(solverdata->solvals[i]), solverdata->maxvars) );
      }

      solverdata->nsols = nsols;

   }

   return SCIP_OKAY;
}

/** checks whether the given solution is equal to one of the former solutions in the sols array */
static
SCIP_RETCODE checkSolNew(
   SCIP*                 scip,
   SCIP*                 pricingprob,
   SCIP_SOL**            sols,
   int                   idx,
   SCIP_Bool*            isnew
   )
{
   SCIP* origprob;
   SCIP_VAR** probvars;
   int nprobvars;
   SCIP_Real* newvals;

   int s;
   int i;

   assert(scip != NULL);
   assert(pricingprob != NULL);
   assert(sols != NULL);
   assert(sols[idx] != NULL);
   assert(isnew != NULL);

   origprob = GCGpricerGetOrigprob(scip);
   assert(origprob != NULL);

   probvars = SCIPgetVars(pricingprob);
   nprobvars = SCIPgetNVars(pricingprob);

   *isnew = TRUE;

   SCIP_CALL( SCIPallocBufferArray(scip, &newvals, nprobvars) );

   SCIP_CALL( SCIPgetSolVals(pricingprob, sols[idx], nprobvars, probvars, newvals) );

   for( s = 0; s < idx && *isnew == TRUE; s++ )
   {
      assert(sols[s] != NULL);
      assert(SCIPisLE(scip, SCIPgetSolOrigObj(pricingprob, sols[s]), SCIPgetSolOrigObj(pricingprob, sols[idx])));
      if( !SCIPisEQ(scip, SCIPgetSolOrigObj(pricingprob, sols[s]), SCIPgetSolOrigObj(pricingprob, sols[idx])) )
         continue;

      if( SCIPsolGetOrigin(sols[s]) != SCIP_SOLORIGIN_ORIGINAL && SCIPsolGetOrigin(sols[idx]) != SCIP_SOLORIGIN_ORIGINAL )
         continue;

      for( i = 0; i < nprobvars; i++ )
         if( !SCIPisEQ(scip, SCIPgetSolVal(pricingprob, sols[s], probvars[i]), newvals[i]) )
            break;

      if( i == nprobvars )
         *isnew = FALSE;
   }

   SCIPfreeBufferArray(scip, &newvals);

   return SCIP_OKAY;
}



/*
 * Callback methods for pricing problem solver
 */

static
GCG_DECL_SOLVERFREE(solverFreeMip)
{
   GCG_SOLVERDATA* solverdata;

   assert(scip != NULL);
   assert(solver != NULL);

   solverdata = GCGpricerGetSolverdata(scip, solver);
   assert(solverdata != NULL);

   SCIPfreeMemory(scip, &solverdata);

   GCGpricerSetSolverdata(scip, solver, NULL);

   return SCIP_OKAY;
}

static
GCG_DECL_SOLVERINITSOL(solverInitsolMip)
{
   GCG_SOLVERDATA* solverdata;
   int i;

   assert(scip != NULL);
   assert(solver != NULL);

   solverdata = GCGpricerGetSolverdata(scip, solver);
   assert(solverdata != NULL);

   solverdata->maxvars = -1;
   for( i = 0; i < GCGrelaxGetNPricingprobs(solverdata->origprob); i++ )
   {
      if( SCIPgetNVars(GCGrelaxGetPricingprob(solverdata->origprob, i)) > solverdata->maxvars )
         solverdata->maxvars = SCIPgetNVars(GCGrelaxGetPricingprob(solverdata->origprob, i));
   }

   solverdata->nsols = 10;
   SCIP_CALL( SCIPallocMemoryArray(scip, &(solverdata->nsolvars), solverdata->nsols) );
   SCIP_CALL( SCIPallocMemoryArray(scip, &(solverdata->solvars), solverdata->nsols) );
   SCIP_CALL( SCIPallocMemoryArray(scip, &(solverdata->solvals), solverdata->nsols) );

   for( i = 0; i < solverdata->nsols; i++ )
   {
      solverdata->nsolvars[i] = 0;
      SCIP_CALL( SCIPallocMemoryArray(scip, &(solverdata->solvars[i]), solverdata->maxvars) );
      SCIP_CALL( SCIPallocMemoryArray(scip, &(solverdata->solvals[i]), solverdata->maxvars) );
   }

   SCIP_CALL( SCIPallocMemoryArray(scip, &(solverdata->tmpsolvals), solverdata->maxvars) );

   return SCIP_OKAY;
}

static
GCG_DECL_SOLVEREXITSOL(solverExitsolMip)
{
   GCG_SOLVERDATA* solverdata;
   int i;

   assert(scip != NULL);
   assert(solver != NULL);

   solverdata = GCGpricerGetSolverdata(scip, solver);
   assert(solverdata != NULL);

   for( i = 0; i < solverdata->nsols; i++ )
   {
      SCIPfreeMemoryArray(scip, &(solverdata->solvars[i]));
      SCIPfreeMemoryArray(scip, &(solverdata->solvals[i]));
   }

   SCIPfreeMemoryArray(scip, &(solverdata->tmpsolvals));

   SCIPfreeMemoryArray(scip, &(solverdata->nsolvars));
   SCIPfreeMemoryArray(scip, &(solverdata->solvars));
   SCIPfreeMemoryArray(scip, &(solverdata->solvals));

   return SCIP_OKAY;
}

#define solverInitMip NULL
#define solverExitMip NULL


static
GCG_DECL_SOLVERSOLVE(solverSolveMip)
{
   GCG_SOLVERDATA* solverdata;
   SCIP_SOL** probsols;
   int nprobsols;

   SCIP_VAR** probvars;
   int nprobvars;

   SCIP_Bool newsol;

   int s;
   int i;



#ifdef DEBUG_PRICING_ALL_OUTPUT
   //if( pricetype == GCG_PRICETYPE_REDCOST )
   {
      char probname[SCIP_MAXSTRLEN];
      (void) SCIPsnprintf(probname, SCIP_MAXSTRLEN, "pricingmip_%d_%d_vars.lp", prob, SCIPgetNVars(scip));
      SCIP_CALL( SCIPwriteOrigProblem(pricingprob, probname, NULL, FALSE) );
      
      SCIP_CALL( SCIPsetIntParam(pricingprob, "display/verblevel", SCIP_VERBLEVEL_HIGH) );
      
      SCIP_CALL( SCIPwriteParams(pricingprob, "pricing.set", TRUE, TRUE) );
      
   }
#endif

   solverdata = GCGpricerGetSolverdata(scip, solver);
   assert(solverdata != NULL);

   SCIP_CALL( SCIPtransformProb(pricingprob) );

   /* presolve the pricing submip */
   if( SCIPgetStage(pricingprob) < SCIP_STAGE_PRESOLVING )
   {
      SCIP_CALL( SCIPpresolve(pricingprob) );
   }

   /* solve the pricing submip */
   SCIP_CALL( SCIPsolve(pricingprob) );
   
   /* so far, the pricing problem should be solved to optimality */
   assert( SCIPgetStatus(pricingprob) == SCIP_STATUS_OPTIMAL
      || SCIPgetStatus(pricingprob) == SCIP_STATUS_GAPLIMIT
      || SCIPgetStatus(pricingprob) == SCIP_STATUS_USERINTERRUPT 
      || SCIPgetStatus(pricingprob) == SCIP_STATUS_INFEASIBLE
      || SCIPgetStatus(pricingprob) == SCIP_STATUS_TIMELIMIT );
   /** @todo handle userinterrupt: set result pointer (and lowerbound), handle other solution states */
   
   if( SCIPgetStatus(pricingprob) == SCIP_STATUS_USERINTERRUPT
      || SCIPgetStatus(pricingprob) == SCIP_STATUS_TIMELIMIT )
   {
      *solvars = solverdata->solvars;
      *solvals = solverdata->solvals;
      *nsolvars = solverdata->nsolvars;
      *nsols = 0;
      *result = SCIP_STATUS_UNKNOWN;
   } 
   else
   {
      /* get variables of the pricing problem */
      probvars = SCIPgetOrigVars(pricingprob);
      nprobvars = SCIPgetNOrigVars(pricingprob);

      nprobsols = SCIPgetNSols(pricingprob);
      probsols = SCIPgetSols(pricingprob);

      *nsols = 0;

      SCIP_CALL( ensureSizeSolvers(scip, solverdata, nprobsols) );

      for( s = 0; s < nprobsols; s++ )
      {
#ifndef NDEBUG
         SCIP_Bool feasible;
         SCIP_CALL( SCIPcheckSolOrig(pricingprob, probsols[s], &feasible, TRUE, TRUE) );
         assert(feasible);
#endif
         
         if( solverdata->checksols )
         {
            SCIP_CALL( checkSolNew(scip, pricingprob, probsols, s, &newsol) );
            
            if( !newsol )
               continue;
         }

         solverdata->nsolvars[*nsols] = 0;

         SCIP_CALL( SCIPgetSolVals(pricingprob, probsols[s], nprobvars, probvars, solverdata->tmpsolvals) );
         
         /* for integer variable, round the solvals */
         for( i = 0; i < nprobvars; i++ )
         {
            if( SCIPisZero(scip, solverdata->tmpsolvals[i]) )
               continue;

            solverdata->solvars[*nsols][solverdata->nsolvars[*nsols]] = probvars[i];

            if( SCIPvarGetType(probvars[i]) != SCIP_VARTYPE_CONTINUOUS )
            {
               assert(SCIPisEQ(scip, solverdata->tmpsolvals[i], SCIPfloor(scip, solverdata->tmpsolvals[i])));
               solverdata->solvals[*nsols][solverdata->nsolvars[*nsols]] 
                  = SCIPfloor(scip, solverdata->tmpsolvals[i]);
            }
            else
               solverdata->solvals[*nsols][solverdata->nsolvars[*nsols]] = solverdata->tmpsolvals[i];

            solverdata->nsolvars[*nsols]++;
         }
         
         *nsols = *nsols + 1;
      }
         
      *solvars = solverdata->solvars;
      *solvals = solverdata->solvals;
      *nsolvars = solverdata->nsolvars;

      *result = SCIP_STATUS_OPTIMAL;
      //printf("Pricingprob %d has found %d sols!\n", prob, nsols);
   }

#ifdef DEBUG_PRICING_ALL_OUTPUT
   if( pricetype == GCG_PRICETYPE_REDCOST )
   {
      SCIP_CALL( SCIPsetIntParam(pricingprob, "display/verblevel", 0) );
      SCIP_CALL( SCIPprintStatistics(pricingprob, NULL) );
   }
#endif

   return SCIP_OKAY;
}


static
GCG_DECL_SOLVERSOLVEHEUR(solverSolveHeurMip)
{
   GCG_SOLVERDATA* solverdata;
   SCIP_SOL** probsols;
   int nprobsols;

   SCIP_VAR** probvars;
   int nprobvars;

   SCIP_Bool newsol;

   int s;
   int i;

#ifdef DEBUG_PRICING_ALL_OUTPUT
   //if( pricetype == GCG_PRICETYPE_REDCOST )
   {
      char probname[SCIP_MAXSTRLEN];
      (void) SCIPsnprintf(probname, SCIP_MAXSTRLEN, "pricingmip_%d_%d_vars.lp", prob, SCIPgetNVars(scip));
      SCIP_CALL( SCIPwriteOrigProblem(pricingprob, probname, NULL, FALSE) );
      
      SCIP_CALL( SCIPsetIntParam(pricingprob, "display/verblevel", SCIP_VERBLEVEL_HIGH) );
      
      SCIP_CALL( SCIPwriteParams(pricingprob, "pricing.set", TRUE, TRUE) );
      
   }
#endif

   solverdata = GCGpricerGetSolverdata(scip, solver);
   assert(solverdata != NULL);

   SCIP_CALL( SCIPsetLongintParam(pricingprob, "limits/stallnodes", 100) );
   SCIP_CALL( SCIPsetLongintParam(pricingprob, "limits/nodes", 1000) );
   SCIP_CALL( SCIPsetRealParam(pricingprob, "limits/gap", 0.2) );
   //SCIP_CALL( SCIPsetIntParam(pricingprob, "limits/bestsol", 5) );


   SCIP_CALL( SCIPtransformProb(pricingprob) );

   /* presolve the pricing submip */
   if( SCIPgetStage(pricingprob) < SCIP_STAGE_PRESOLVING )
   {
      SCIP_CALL( SCIPpresolve(pricingprob) );
   }

   /* solve the pricing submip */
   SCIP_CALL( SCIPsolve(pricingprob) );
   
   /* so far, the pricing problem should be solved to optimality */
   assert( SCIPgetStatus(pricingprob) == SCIP_STATUS_OPTIMAL
      || SCIPgetStatus(pricingprob) == SCIP_STATUS_GAPLIMIT
      || SCIPgetStatus(pricingprob) == SCIP_STATUS_USERINTERRUPT 
      || SCIPgetStatus(pricingprob) == SCIP_STATUS_INFEASIBLE
      || SCIPgetStatus(pricingprob) == SCIP_STATUS_TIMELIMIT );
   /** @todo handle userinterrupt: set result pointer (and lowerbound), handle other solution states */
   
   if( SCIPgetStatus(pricingprob) == SCIP_STATUS_USERINTERRUPT
      || SCIPgetStatus(pricingprob) == SCIP_STATUS_TIMELIMIT )
   {
      *solvars = solverdata->solvars;
      *solvals = solverdata->solvals;
      *nsolvars = solverdata->nsolvars;
      *nsols = 0;
      *result = SCIP_STATUS_UNKNOWN;
   } 
   else
   {
      /* get variables of the pricing problem */
      probvars = SCIPgetOrigVars(pricingprob);
      nprobvars = SCIPgetNOrigVars(pricingprob);

      nprobsols = SCIPgetNSols(pricingprob);
      probsols = SCIPgetSols(pricingprob);

      *nsols = 0;

      SCIP_CALL( ensureSizeSolvers(scip, solverdata, nprobsols) );

      for( s = 0; s < nprobsols; s++ )
      {
#ifndef NDEBUG
         SCIP_Bool feasible;
         SCIP_CALL( SCIPcheckSolOrig(pricingprob, probsols[s], &feasible, TRUE, TRUE) );
         assert(feasible);
#endif
         
         if( solverdata->checksols )
         {
            SCIP_CALL( checkSolNew(scip, pricingprob, probsols, s, &newsol) );
            
            if( !newsol )
               continue;
         }

         solverdata->nsolvars[*nsols] = 0;

         SCIP_CALL( SCIPgetSolVals(pricingprob, probsols[s], nprobvars, probvars, solverdata->tmpsolvals) );
         
         /* for integer variable, round the solvals */
         for( i = 0; i < nprobvars; i++ )
         {
            if( SCIPisZero(scip, solverdata->tmpsolvals[i]) )
               continue;

            solverdata->solvars[*nsols][solverdata->nsolvars[*nsols]] = probvars[i];

            if( SCIPvarGetType(probvars[i]) != SCIP_VARTYPE_CONTINUOUS )
            {
               assert(SCIPisEQ(scip, solverdata->tmpsolvals[i], SCIPfloor(scip, solverdata->tmpsolvals[i])));
               solverdata->solvals[*nsols][solverdata->nsolvars[*nsols]] 
                  = SCIPfloor(scip, solverdata->tmpsolvals[i]);
            }
            else
               solverdata->solvals[*nsols][solverdata->nsolvars[*nsols]] = solverdata->tmpsolvals[i];

            solverdata->nsolvars[*nsols]++;
         }
         
         *nsols = *nsols + 1;
      }
         
      *result = SCIP_STATUS_OPTIMAL;
      //printf("Pricingprob %d has found %d sols!\n", prob, nsols);
   }

#ifdef DEBUG_PRICING_ALL_OUTPUT
   if( pricetype == GCG_PRICETYPE_REDCOST )
   {
      SCIP_CALL( SCIPsetIntParam(pricingprob, "display/verblevel", 0) );
      SCIP_CALL( SCIPprintStatistics(pricingprob, NULL) );
   }
#endif

   SCIP_CALL( SCIPsetLongintParam(pricingprob, "limits/stallnodes", -1) );
   SCIP_CALL( SCIPsetLongintParam(pricingprob, "limits/nodes", -1) ); 
   SCIP_CALL( SCIPsetRealParam(pricingprob, "limits/gap", 0.0) ); 
   SCIP_CALL( SCIPsetIntParam(pricingprob, "limits/bestsol", -1) ); 

   return SCIP_OKAY;
}

/** creates the most infeasible LP braching rule and includes it in SCIP */
SCIP_RETCODE GCGincludeSolverMip(
   SCIP*                 scip                /**< SCIP data structure */
   )
{   
   GCG_SOLVERDATA* data;

   SCIP_CALL( SCIPallocMemory( scip, &data) );
   data->nsols = 0;
   data->nsolvars = 0;
   data->solvars = NULL;
   data->solvals = NULL;
   data->origprob = GCGpricerGetOrigprob(scip);

   SCIP_CALL( GCGpricerIncludeSolver(scip, SOLVER_NAME, SOLVER_DESC, SOLVER_PRIORITY, 
         solverSolveMip, solverSolveHeurMip, solverFreeMip, solverInitMip, solverExitMip,
         solverInitsolMip, solverExitsolMip, data) );

   SCIP_CALL( SCIPaddBoolParam(data->origprob, "pricingsolver/mip/checksols",
         "should solutions of the pricing MIPs be checked for duplicity?",
         &data->checksols, TRUE, DEFAULT_CHECKSOLS, NULL, NULL) );


   return SCIP_OKAY;
}