/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                           */
/*                  This file is part of the program                         */
/*          GCG --- Generic Column Generation                                */
/*                  a Dantzig-Wolfe decomposition based extension            */
/*                  of the branch-cut-and-price framework                    */
/*         SCIP --- Solving Constraint Integer Programs                      */
/*                                                                           */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/**@file   solver_knapsack.c
 * @brief  knapsack solver for pricing problems
 * @author Gerald Gamrath
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#include <assert.h>

#include "solver_knapsack.h"
#include "scip/cons_linear.h"
#include "scip/cons_knapsack.h"
#include "type_solver.h"
#include "pricer_gcg.h"
#include "relax_gcg.h"


#define SOLVER_NAME          "knapsack"
#define SOLVER_DESC          "knapsack solver for pricing problems"
#define SOLVER_PRIORITY      -100



/** knapsack pricing solverdata */
struct GCG_SolverData
{
   SCIP* origprob;      /**< original problem */
   SCIP_Real** solvals; /**< two dimensional array of solution values */
   SCIP_VAR*** solvars; /**< two dimensional array of solution variables */
   int* nsolvars;       /**< array of number of variables per solution */
   SCIP_Bool* solisray; /**< array indicating whether a solution represents a ray */
   int nsols;           /**< number of solutions */
   int maxvars;         /**< maximal number of variables in a solution */
};


/*
 * Callback methods for pricing problem solver
 */

static
GCG_DECL_SOLVERFREE(solverFreeKnapsack)
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
GCG_DECL_SOLVERINITSOL(solverInitsolKnapsack)
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

   solverdata->nsols = 5;
   SCIP_CALL( SCIPallocMemoryArray(scip, &(solverdata->nsolvars), solverdata->nsols) );
   SCIP_CALL( SCIPallocMemoryArray(scip, &(solverdata->solisray), solverdata->nsols) );
   SCIP_CALL( SCIPallocMemoryArray(scip, &(solverdata->solvars), solverdata->nsols) );
   SCIP_CALL( SCIPallocMemoryArray(scip, &(solverdata->solvals), solverdata->nsols) );

   for( i = 0; i < solverdata->nsols; i++ )
   {
      solverdata->nsolvars[i] = 0;
      SCIP_CALL( SCIPallocMemoryArray(scip, &(solverdata->solvars[i]), solverdata->maxvars) ); /*lint !e866*/
      SCIP_CALL( SCIPallocMemoryArray(scip, &(solverdata->solvals[i]), solverdata->maxvars) ); /*lint !e866*/
   }

   return SCIP_OKAY;
}

static
GCG_DECL_SOLVEREXITSOL(solverExitsolKnapsack)
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

   SCIPfreeMemoryArray(scip, &(solverdata->nsolvars));
   SCIPfreeMemoryArray(scip, &(solverdata->solisray));
   SCIPfreeMemoryArray(scip, &(solverdata->solvars));
   SCIPfreeMemoryArray(scip, &(solverdata->solvals));

   return SCIP_OKAY;
}

#define solverInitKnapsack NULL
#define solverExitKnapsack NULL

static
GCG_DECL_SOLVERSOLVE(solverSolveKnapsack)
{  /*lint --e{715}*/
   GCG_SOLVERDATA* solverdata;
   SCIP_CONS* cons;
   SCIP_VAR** consvars;
   int nconsvars;
   SCIP_Real* consvals;

   SCIP_VAR** pricingprobvars;
   int npricingprobvars;
   int nconss;

   int                   nitems;
   SCIP_Longint*         weights;
   SCIP_Real*            profits;
   SCIP_Longint          capacity;
   int*                  items;
   int*                  solitems;
   int                   nsolitems;
   int*                  nonsolitems;
   int                   nnonsolitems;
   SCIP_Real             solval;
   SCIP_Bool success;

   int i;
   int k;

   assert(pricingprob != NULL);
   assert(scip != NULL);
   assert(result != NULL);
   assert(solver != NULL);

   solverdata = GCGpricerGetSolverdata(scip, solver);
   assert(solverdata != NULL);

   pricingprobvars = SCIPgetVars(pricingprob);
   npricingprobvars = SCIPgetNVars(pricingprob);

   nconss = SCIPgetNConss(pricingprob);
   if( nconss != 1 )
   {
      *result = SCIP_STATUS_UNKNOWN;
      return SCIP_OKAY;
   }

   cons = SCIPgetConss(pricingprob)[0];
   assert(cons != NULL);

   nitems = 0;

   for( i = 0; i < npricingprobvars; i++ )
   {
      if( SCIPvarGetUbLocal(pricingprobvars[i]) > SCIPvarGetLbLocal(pricingprobvars[i]) + 0.5 )
         nitems++;
   }

   if( !SCIPisIntegral(scip, SCIPgetRhsLinear(pricingprob, cons)) ||
      !SCIPisInfinity(scip, - SCIPgetLhsLinear(pricingprob, cons)) )
   {
      *result = SCIP_STATUS_UNKNOWN;
      return SCIP_OKAY;
   }

   capacity = (SCIP_Longint)SCIPfloor(scip, SCIPgetRhsLinear(pricingprob, cons));
   consvars = SCIPgetVarsLinear(pricingprob, cons);
   nconsvars = SCIPgetNVarsLinear(pricingprob, cons);
   consvals = SCIPgetValsLinear(pricingprob, cons);

   for( i = 0; i < nconsvars; i++ )
   {
      if( !SCIPisIntegral(scip, consvals[i]) )
      {
         *result = SCIP_STATUS_UNKNOWN;
         return SCIP_OKAY;
      }
   }

   SCIP_CALL( SCIPallocBufferArray(scip, &items, nitems) );
   SCIP_CALL( SCIPallocBufferArray(scip, &weights, nitems) );
   SCIP_CALL( SCIPallocBufferArray(scip, &profits, nitems) );
   SCIP_CALL( SCIPallocBufferArray(scip, &solitems, nitems) );
   SCIP_CALL( SCIPallocBufferArray(scip, &nonsolitems, nitems) );

   BMSclearMemoryArray(weights, nitems);

   k = 0;
   for( i = 0; i < npricingprobvars; i++ )
   {
      if( SCIPvarGetUbLocal(pricingprobvars[i]) > SCIPvarGetLbLocal(pricingprobvars[i]) + 0.5 )
      {
         items[k] = i;
         profits[k] = - SCIPvarGetObj(pricingprobvars[i]);
         k++;
      }
   }
   assert(k == nitems);

   for( i = 0; i < nconsvars; i++ )
   {
      assert(SCIPisIntegral(scip, consvals[i]));

      if( SCIPisEQ(scip, SCIPvarGetUbLocal(consvars[i]), 0.0) )
         continue;
      if( SCIPisEQ(scip, SCIPvarGetLbLocal(consvars[i]), 1.0) )
      {
         capacity -= (SCIP_Longint)SCIPfloor(scip, consvals[i]);
         continue;
      }
      for( k = 0; k < nitems; k++ )
      {
         if( pricingprobvars[items[k]] == consvars[i] )
         {
            if( SCIPisPositive(scip, consvals[i]) )
            {
               weights[k] = (SCIP_Longint)SCIPfloor(scip, consvals[i]);
               break;
            }
            else
            {
               capacity -= (SCIP_Longint)SCIPfloor(scip, consvals[i]);
               weights[k] = (SCIP_Longint)SCIPfloor(scip, -1.0*consvals[i]);
               profits[k] *= -1.0;

               break;
            }
         }
      }
      assert(k < nitems);
   }

   /* solve knapsack problem exactly, all result pointers are needed! */
   SCIP_CALL( SCIPsolveKnapsackExactly(pricingprob, nitems, weights, profits, capacity, items, solitems,
         nonsolitems, &nsolitems, &nnonsolitems, &solval, &success ));

   assert(success);
   SCIPdebugMessage("knapsack solved, solval = %g\n", solval);

   solverdata->nsolvars[0] = 0;
   solverdata->solisray[0] = FALSE;

   for( i = 0; i < nsolitems; i++ )
   {
      if( !SCIPisNegative(scip, consvals[solitems[i]]) )
      {
         solverdata->solvars[0][solverdata->nsolvars[0]] = pricingprobvars[solitems[i]];
         solverdata->solvals[0][solverdata->nsolvars[0]] = 1;
         solverdata->nsolvars[0]++;
      }
   }

   for( i = 0; i < nnonsolitems; i++ )
   {
      if( SCIPisNegative(scip, consvals[nonsolitems[i]]) )
      {
         solverdata->solvars[0][solverdata->nsolvars[0]] = pricingprobvars[nonsolitems[i]];
         solverdata->solvals[0][solverdata->nsolvars[0]] = 1;
         solverdata->nsolvars[0]++;
      }
   }

   for( i = 0; i < npricingprobvars; i++ )
   {
      if( SCIPvarGetLbLocal(pricingprobvars[i]) > 0.5 )
      {
         solverdata->solvars[0][solverdata->nsolvars[0]] = pricingprobvars[i];
         solverdata->solvals[0][solverdata->nsolvars[0]] = 1;
         solverdata->nsolvars[0]++;
      }
   }

   SCIPfreeBufferArray(scip, &nonsolitems);
   SCIPfreeBufferArray(scip, &solitems);
   SCIPfreeBufferArray(scip, &profits);
   SCIPfreeBufferArray(scip, &weights);
   SCIPfreeBufferArray(scip, &items);

   *solvars = solverdata->solvars;
   *solvals = solverdata->solvals;
   *nsolvars = solverdata->nsolvars;
   *solisray = solverdata->solisray;
   *nsols = 1;


   *result = SCIP_STATUS_OPTIMAL;

   return SCIP_OKAY;
}


static
GCG_DECL_SOLVERSOLVEHEUR(solverSolveHeurKnapsack)
{  /*lint --e{715}*/
   SCIP_CONS* cons;
   SCIP_VAR** consvars;
   int nconsvars;
   SCIP_Real* consvals;

   SCIP_VAR** pricingprobvars;
   int npricingprobvars;
   int nconss;

   int                   nitems;
   SCIP_Longint*         weights;
   SCIP_Real*            profits;
   SCIP_Longint          capacity;
   int*                  items;
   int*                  solitems;
   int                   nsolitems;
   int*                  nonsolitems;
   int                   nnonsolitems;
   SCIP_Real             solval;

   int i;
   int k;

   SCIP_SOL* sol;

   SCIP_Bool stored;

   assert(pricingprob != NULL);
   assert(scip != NULL);
   assert(result != NULL);

   pricingprobvars = SCIPgetVars(pricingprob);
   npricingprobvars = SCIPgetNVars(pricingprob);

   nconss = SCIPgetNConss(pricingprob);
   if( nconss != 1 )
   {
      *result = SCIP_STATUS_UNKNOWN;
      return SCIP_OKAY;
   }

   cons = SCIPgetConss(pricingprob)[0];
   assert(cons != NULL);

   nitems = 0;

   for( i = 0; i < npricingprobvars; i++ )
   {
      if( SCIPvarGetUbLocal(pricingprobvars[i]) > SCIPvarGetLbLocal(pricingprobvars[i]) + 0.5 )
         nitems++;
   }

   if( !SCIPisIntegral(scip, SCIPgetRhsLinear(pricingprob, cons)) ||
      !SCIPisInfinity(scip, - SCIPgetLhsLinear(pricingprob, cons)) )
   {
      *result = SCIP_STATUS_UNKNOWN;
      return SCIP_OKAY;
   }

   capacity = (SCIP_Longint)SCIPfloor(scip, SCIPgetRhsLinear(pricingprob, cons));
   consvars = SCIPgetVarsLinear(pricingprob, cons);
   nconsvars = SCIPgetNVarsLinear(pricingprob, cons);
   consvals = SCIPgetValsLinear(pricingprob, cons);

   for( i = 0; i < nconsvars; i++ )
   {
      if( !SCIPisIntegral(scip, consvals[i]) )
      {
         *result = SCIP_STATUS_UNKNOWN;
         return SCIP_OKAY;
      }
   }

   SCIP_CALL( SCIPallocBufferArray(scip, &items, nitems) );
   SCIP_CALL( SCIPallocBufferArray(scip, &weights, nitems) );
   SCIP_CALL( SCIPallocBufferArray(scip, &profits, nitems) );
   SCIP_CALL( SCIPallocBufferArray(scip, &solitems, nitems) );
   SCIP_CALL( SCIPallocBufferArray(scip, &nonsolitems, nitems) );

   BMSclearMemoryArray(weights, nitems);

   k = 0;
   for( i = 0; i < npricingprobvars; i++ )
   {
      if( SCIPvarGetUbLocal(pricingprobvars[i]) > SCIPvarGetLbLocal(pricingprobvars[i]) + 0.5 )
      {
         items[k] = i;
         profits[k] = - SCIPvarGetObj(pricingprobvars[i]);
         k++;
      }
   }
   assert(k == nitems);

   for( i = 0; i < nconsvars; i++ )
   {
      assert(SCIPisIntegral(scip, consvals[i]));

      if( SCIPisEQ(scip, SCIPvarGetUbLocal(consvars[i]), 0.0) )
         continue;
      if( SCIPisEQ(scip, SCIPvarGetLbLocal(consvars[i]), 1.0) )
      {
         capacity -= (SCIP_Longint)SCIPfloor(scip, consvals[i]);
         continue;
      }
      for( k = 0; k < nitems; k++ )
      {
         if( pricingprobvars[items[k]] == consvars[i] )
         {
            if( SCIPisPositive(scip, consvals[i]) )
            {
               weights[k] = (SCIP_Longint)SCIPfloor(scip, consvals[i]);
               break;
            }
            else
            {
               capacity -= (SCIP_Longint)SCIPfloor(scip, consvals[i]);
               weights[k] = (SCIP_Longint)SCIPfloor(scip, -1.0*consvals[i]);
               profits[k] *= -1.0;

               break;
            }
         }
      }
      assert(k < nitems);
   }

   /* solve knapsack problem exactly, all result pointers are needed! */
   SCIP_CALL( SCIPsolveKnapsackApproximately(pricingprob, nitems, weights, profits, capacity, items, solitems,
         nonsolitems, &nsolitems, &nnonsolitems, &solval ));

   SCIPdebugMessage("knapsack solved, solval = %g\n", solval);

   SCIP_CALL( SCIPtransformProb(pricingprob) );

   SCIP_CALL( SCIPcreateSol( pricingprob, &sol, NULL) );

   for( i = 0; i < nsolitems; i++ )
   {
      if( !SCIPisNegative(scip, consvals[solitems[i]]) )
      {
         SCIP_CALL( SCIPsetSolVal(pricingprob, sol, pricingprobvars[solitems[i]], 1.0) );
      }
   }

   for( i = 0; i < nnonsolitems; i++ )
   {
      if( SCIPisNegative(scip, consvals[nonsolitems[i]]) )
      {
         SCIP_CALL( SCIPsetSolVal(pricingprob, sol, pricingprobvars[nonsolitems[i]], 1.0) );
      }
   }

   for( i = 0; i < npricingprobvars; i++ )
   {
      if( SCIPvarGetLbLocal(pricingprobvars[i]) > 0.5 )
      {
         SCIP_CALL( SCIPsetSolVal(pricingprob, sol, pricingprobvars[i], 1.0) );
      }
   }

   SCIP_CALL( SCIPaddSolFree(pricingprob, &sol, &stored) );
   assert(stored);

   SCIPfreeBufferArray(scip, &nonsolitems);
   SCIPfreeBufferArray(scip, &solitems);
   SCIPfreeBufferArray(scip, &profits);
   SCIPfreeBufferArray(scip, &weights);
   SCIPfreeBufferArray(scip, &items);

   *result = SCIP_STATUS_OPTIMAL;

   return SCIP_OKAY;
}


/** creates the knapsack solver for pricing problems and includes it in GCG */
SCIP_RETCODE GCGincludeSolverKnapsack(
   SCIP*                 scip                /**< SCIP data structure */
   )
{
   GCG_SOLVERDATA* data;

   SCIP_CALL( SCIPallocMemory( scip, &data) );
   data->nsols = 0;
   data->nsolvars = NULL;
   data->solisray = NULL;
   data->solvars = NULL;
   data->solvals = NULL;
   data->origprob = GCGpricerGetOrigprob(scip);

   SCIP_CALL( GCGpricerIncludeSolver(scip, SOLVER_NAME, SOLVER_DESC, SOLVER_PRIORITY, solverSolveKnapsack,
         solverSolveHeurKnapsack, solverFreeKnapsack, solverInitKnapsack, solverExitKnapsack,
         solverInitsolKnapsack, solverExitsolKnapsack, data) );

   return SCIP_OKAY;
}
