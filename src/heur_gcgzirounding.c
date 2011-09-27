/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                           */
/*                  This file is part of the program                         */
/*          GCG --- Generic Column Generation                                */
/*                  a Dantzig-Wolfe decomposition based extension            */
/*                  of the branch-cut-and-price framework                    */
/*         SCIP --- Solving Constraint Integer Programs                      */
/*                                                                           */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
#pragma ident ""

/**@file   heur_gcgzirounding.c
 * @ingroup PRIMALHEURISTICS
 * @brief  zirounding primal heuristic
 * @author Gregor Hendel
 * @author Christian Puchert
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

/* toggle debug mode */
//#define SCIP_DEBUG

#include <assert.h>
#include <string.h>

#include "heur_gcgzirounding.h"
#include "relax_gcg.h"

#define HEUR_NAME             "gcgzirounding"
#define HEUR_DESC             "LP rounding heuristic on original variables as suggested by C. Wallace taking row slacks and bounds into account"
#define HEUR_DISPCHAR         'z'
#define HEUR_PRIORITY         -500
//#define HEUR_FREQ             1
#define HEUR_FREQ             -1    // TODO: heuristic deactivated due to false solutions
#define HEUR_FREQOFS          0
#define HEUR_MAXDEPTH         -1
#define HEUR_TIMING           SCIP_HEURTIMING_AFTERNODE
#define HEUR_USESSUBSCIP      FALSE

#define DEFAULT_MAXROUNDINGLOOPS   2      /**< delimits the number of main loops */
#define DEFAULT_STOPZIROUND        TRUE   /**< deactivation check is enabled by default */     
#define DEFAULT_STOPPERCENTAGE     0.02   /**< the tolerance percentage after which zirounding will not be executed anymore */
#define DEFAULT_MINSTOPNCALLS      1000   /**< number of heuristic calls before deactivation check */   

/*
 * Data structures
 */

/** primal heuristic data */
struct SCIP_HeurData
{
   SCIP_SOL*             sol;                /**< working solution */
   SCIP_Longint          lastlp;             /**< the number of the last LP for which ZIRounding was called */
   int                   maxroundingloops;   /**< limits rounding loops in execution */ 
   SCIP_Bool             stopziround;        /**< sets deactivation check */
   SCIP_Real             stoppercentage;     /**< threshold for deactivation check */
   int                   minstopncalls;      /**< number of heuristic calls before deactivation check */   
};

enum Direction
{
   DIRECTION_UP           =  1,
   DIRECTION_DOWN         = -1
};
typedef enum Direction DIRECTION;

/*
 * Local methods
 */

/** returns the fractionality of a value x, which is calculated as zivalue(x) = min(x-floor(x), ceil(x)-x) */
static 
SCIP_Real getZiValue(
   SCIP*                 scip,               /**< pointer to current SCIP data structure */
   SCIP_Real             val                 /**< the value for which the fractionality should be computed */ 
   )
{
   SCIP_Real upgap;     /* the gap between val and ceil(val) */
   SCIP_Real downgap;   /* the gap between val and floor(val) */
   
   assert(scip != NULL);
      
   upgap   = SCIPfeasCeil(scip, val) - val;
   downgap = val - SCIPfeasFloor(scip, val);

   return MIN(upgap, downgap);   
}

/** determines shifting bounds for variable; */
static
void calculateBounds(
   SCIP*                 scip,               /**< pointer to current SCIP data structure */
   SCIP_VAR*             var,                /**< the variable for which lb and ub have to be calculated */
   SCIP_Real             currentvalue,       /**< the current value of var in the working solution */
   SCIP_Real*            upperbound,         /**< pointer to store the calculated upper bound on the variable shift */
   SCIP_Real*            lowerbound,         /**< pointer to store the calculated lower bound on the variable shift */
   SCIP_Real*            upslacks,           /**< array that contains the slacks between row activities and the right hand sides of the rows */
   SCIP_Real*            downslacks,         /**< array that contains lhs slacks */
   int                   nslacks,            /**< current number of slacks */
   SCIP_Bool*            numericalerror      /**< flag to determine wether a numerical error occurred */
   )
{
   SCIP_COL*      col;
   SCIP_ROW**     colrows;
   SCIP_Real*     colvals;
   int            ncolvals;
   int i;
  
   assert(scip != NULL);
   assert(var != NULL);
   assert(upslacks != NULL || nslacks == 0);
   assert(downslacks != NULL || nslacks == 0);
   assert(upperbound != NULL);
   assert(lowerbound != NULL);

   /* get the column associated to the variable, the nonzero rows and the nonzero coefficients */  
   col       = SCIPvarGetCol(var);
   colrows   = SCIPcolGetRows(col);
   colvals   = SCIPcolGetVals(col);
   ncolvals  = SCIPcolGetNLPNonz(col);

   /* only proceed, when variable has nonzero coefficients */
   if( ncolvals == 0 )   
      return;
   
   assert(colvals != NULL);
   assert(colrows != NULL);

   /* initialize the bounds on the shift to be the gap of the current solution value to the bounds of the variable */
   if( SCIPisInfinity(scip, SCIPvarGetUbGlobal(var)) )
      *upperbound = SCIPinfinity(scip);
   else
      *upperbound = SCIPvarGetUbGlobal(var) - currentvalue;

   if( SCIPisInfinity(scip, -SCIPvarGetLbGlobal(var)) )
      *lowerbound = SCIPinfinity(scip);
   else
      *lowerbound = currentvalue - SCIPvarGetLbGlobal(var);
  
   /* go through every nonzero row coefficient corresponding to var to determine bounds for shifting
    * in such a way that shifting maintains feasibility in every LP row.
    * a lower or upper bound as it is calculated in zirounding always has to be >= 0.0.
    * if one of these values is significantly < 0.0, this will cause the abort of execution of the heuristic so that 
    * infeasible solutions are avoided 
    */
   for ( i = 0; i < ncolvals && (*lowerbound > 0.0 || *upperbound > 0.0); ++i )
   {
      SCIP_ROW* row;
      int       rowpos;

      row = colrows[i];
      rowpos = SCIProwGetLPPos(row);
      
      /* the row might currently not be in the LP, ignore it! */
      if( rowpos == -1 )
         continue;

      assert(0 <= rowpos && rowpos < nslacks);

      /** all bounds and slacks as they are calculated in zirounding always have to be greater euqal zero.
       * It might however be due to numerical issues, e.g. with scaling, that they are not. Better abort in this case.
       */      
      if( SCIPisFeasLT(scip, *lowerbound, 0.0) || SCIPisFeasLT(scip, *upperbound, 0.0) 
         || SCIPisFeasLT(scip, upslacks[rowpos], 0.0) || SCIPisFeasLT(scip, downslacks[rowpos] , 0.0) )
      {
         *numericalerror = TRUE;
         return;
      }

      /* if coefficient > 0, rounding up might violate up slack and rounding down might violate down slack
       * thus search for the minimum so that no constraint is violated;
       * if coefficient < 0, it is the other way around unless at least one row slack is infinity 
       * which has to be excluded explicitly so as not to corrupt calculations
       */
      if( colvals[i] > 0 )
      {
         if( !SCIPisInfinity(scip, upslacks[rowpos]) )
            *upperbound = MIN(*upperbound, upslacks[rowpos]/colvals[i]);

         if( !SCIPisInfinity(scip, downslacks[rowpos]) )
            *lowerbound = MIN(*lowerbound, downslacks[rowpos]/colvals[i]);
      }
      else
      {
         assert(colvals[i] != 0.0);
            
         if( !SCIPisInfinity(scip, upslacks[rowpos] ) )
            *lowerbound = MIN(*lowerbound, -upslacks[rowpos]/colvals[i]);

         if( !SCIPisInfinity(scip, downslacks[rowpos] ) )
            *upperbound = MIN(*upperbound,-downslacks[rowpos]/colvals[i]);
      }
   }
}

/**  when a variable is shifted, the activities and slacks of all rows it appears in have to be updated */
static
void updateSlacks(
   SCIP*                 scip,               /**< pointer to current SCIP data structure */
   SCIP_SOL*             sol,                /**< working solution */
   SCIP_VAR*             var,                /**< pointer to variable to be modified */
   SCIP_Real*            shiftvalue,         /**< the value by which the variable is shifted */
   SCIP_Real*            upslacks,           /**< upslacks of all rows the variable appears in */
   SCIP_Real*            downslacks,         /**< downslacks of all rows the variable appears in */
   SCIP_Real*            activities,         /**< activities of the LP rows */
   int                   nslacks             /**< size of the arrays */
   )
{
   SCIP_COL*    col;        /** the corresponding column of variable var */
   SCIP_ROW**   rows;       /** pointer to the nonzero coefficient rows for variable var */
   int          nrows;      /** the number of nonzeros */
   SCIP_Real*   colvals;    /** array to store the nonzero coefficients */
   int i;

   assert(scip != NULL);
   assert(sol != NULL);
   assert(var != NULL);
   assert(upslacks != NULL);
   assert(downslacks != NULL);
   assert(activities != NULL);
   assert(nslacks >= 0);
        
   col = SCIPvarGetCol(var);
   assert(col != NULL);
    
   rows     = SCIPcolGetRows(col);
   nrows    = SCIPcolGetNLPNonz(col);
   colvals  = SCIPcolGetVals(col);
   assert(nrows == 0 || (rows != NULL && colvals!= NULL));

   /** go through all rows the shifted variable appears in */
   for( i = 0; i < nrows; ++i )
   {
      int rowpos;       
      rowpos = SCIProwGetLPPos(rows[i]);
      assert(-1 <= rowpos && rowpos < nslacks);

      /* if the row is in the LP, update its activity, up and down slack */
      if( rowpos >= 0 ) 
      {
         SCIP_Real  val;
         val = colvals[i] * (*shiftvalue);

         activities[rowpos]  += val;
         upslacks[rowpos]    -= val;
         downslacks[rowpos]  += val;
      }
   }
}

/*
 * Callback methods of primal heuristic
 */

/** copy method for primal heuristic plugins (called when SCIP copies plugins) */
static
SCIP_DECL_HEURCOPY(heurCopyGcgzirounding)
{  /*lint --e{715}*/
   assert(scip != NULL);
   assert(heur != NULL);
   assert(strcmp(SCIPheurGetName(heur), HEUR_NAME) == 0);

   /* call inclusion method of primal heuristic */
   SCIP_CALL( SCIPincludeHeurGcgzirounding(scip) );

   return SCIP_OKAY;
}

/** destructor of primal heuristic to free user data (called when SCIP is exiting) */
static
SCIP_DECL_HEURFREE(heurFreeGcgzirounding)
{  /*lint --e{715}*/
   SCIP_HEURDATA* heurdata;

   assert(strcmp(SCIPheurGetName(heur), HEUR_NAME) == 0);

   heurdata = SCIPheurGetData(heur);
   assert(heurdata != NULL);

   /* free heuristic data */
   SCIPfreeMemory(scip, &heurdata);
   SCIPheurSetData(heur, NULL);

   return SCIP_OKAY;
}

/** initialization method of primal heuristic (called after problem was transformed) */
static
SCIP_DECL_HEURINIT(heurInitGcgzirounding)
{  /*lint --e{715}*/
   SCIP_HEURDATA* heurdata;

   assert(strcmp(SCIPheurGetName(heur), HEUR_NAME) == 0);

   heurdata = SCIPheurGetData(heur);
   assert(heurdata != NULL);

   /* create working solution */
   SCIP_CALL( SCIPcreateSol(scip, &heurdata->sol, heur) );
         
   return SCIP_OKAY;
}

/** deinitialization method of primal heuristic (called before transformed problem is freed) */
static
SCIP_DECL_HEUREXIT(heurExitGcgzirounding)  /*lint --e{715}*/
{  /*lint --e{715}*/
   SCIP_HEURDATA* heurdata;

   assert(strcmp(SCIPheurGetName(heur), HEUR_NAME) == 0);

   heurdata = SCIPheurGetData(heur);
   assert(heurdata != NULL);

   /* free working solution */
   SCIP_CALL( SCIPfreeSol(scip, &heurdata->sol) );
  
   return SCIP_OKAY;
}

/** solving process initialization method of primal heuristic (called when branch and bound process is about to begin) */
static
SCIP_DECL_HEURINITSOL(heurInitsolGcgzirounding)
{  /*lint --e{715}*/
   SCIP_HEURDATA* heurdata;

   assert(strcmp(SCIPheurGetName(heur), HEUR_NAME) == 0);

   heurdata = SCIPheurGetData(heur);
   assert(heurdata != NULL);

   heurdata->lastlp = -1;
   
   return SCIP_OKAY; 
}

/** solving process deinitialization method of primal heuristic (called before branch and bound process data is freed) */
#define heurExitsolGcgzirounding NULL

/** execution method of primal heuristic */
static
SCIP_DECL_HEUREXEC(heurExecGcgzirounding)
{  /*lint --e{715}*/
   SCIP*              masterprob;
   SCIP_HEURDATA*     heurdata;
   SCIP_SOL*          sol;
   SCIP_VAR**         lpcands;
   SCIP_VAR**         zilpcands;     
   SCIP_ROW**         rows;              
   SCIP_Real*         lpcandssol;    
   SCIP_Real*         solarray;      
   SCIP_Real*         upslacks;       
   SCIP_Real*         downslacks;     
   SCIP_Real*         activities;  
   
   SCIP_Longint       nlps;          
   int                currentlpcands; 
   int                nlpcands;       
   int                i;
   int                nslacks;        
   int                nroundings;  
     
   SCIP_Bool          improvementfound; 
   SCIP_Bool          numericalerror;   
   
   assert(heur != NULL);
   assert(strcmp(SCIPheurGetName(heur), HEUR_NAME) == 0);
   assert(scip != NULL);
   assert(result != NULL);

   /* get master problem */
   masterprob = GCGrelaxGetMasterprob(scip);
   assert(masterprob != NULL);

   *result = SCIP_DIDNOTRUN;
 
   /* only call heuristic if an optimal LP-solution is at hand */
   if( SCIPgetLPSolstat(masterprob) != SCIP_LPSOLSTAT_OPTIMAL )
      return SCIP_OKAY;
   
   /* get heuristic data */
   heurdata = SCIPheurGetData(heur);
   assert(heurdata != NULL);

   /* Do not call heuristic if deactivation check is enabled and percentage of found solutions in relation 
    * to number of calls falls below heurdata->stoppercentage */
   if( heurdata->stopziround && SCIPheurGetNCalls(heur) >= heurdata->minstopncalls 
      && SCIPheurGetNSolsFound(heur)/(SCIP_Real)SCIPheurGetNCalls(heur) < heurdata->stoppercentage )
      return SCIP_OKAY;
   
   /* assure that heuristic has not already been called after the last LP had been solved */
   nlps = SCIPgetNLPs(masterprob);
   if( nlps == heurdata->lastlp )
      return SCIP_OKAY;
   
   heurdata->lastlp = nlps;

   /* get fractional variables */
   SCIP_CALL( SCIPgetExternBranchCands(scip, &lpcands, &lpcandssol, NULL, &nlpcands, NULL, NULL, NULL, NULL) );

   /* make sure that there is at least one fractional variable that should be integral */
   if( nlpcands == 0 )
      return SCIP_OKAY;
   
   assert(nlpcands > 0);
   assert(lpcands != NULL);
   assert(lpcandssol != NULL);
   
   /* get the working solution from heuristic's local data */
   sol = heurdata->sol;
   assert(sol != NULL);

   /* copy the current LP solution to the working solution and allocate memory for local data */
   SCIP_CALL( SCIPlinkRelaxSol(scip, sol) );
   SCIP_CALL( SCIPallocBufferArray(scip, &solarray, nlpcands) );
   SCIP_CALL( SCIPallocBufferArray(scip, &zilpcands, nlpcands) );

   /* copy necessary data to local arrays */
   BMScopyMemoryArray(solarray, lpcandssol, nlpcands);
   BMScopyMemoryArray(zilpcands, lpcands, nlpcands);
   
   /* get LP rows data */
   rows    = SCIPgetLPRows(scip);
   nslacks = SCIPgetNLPRows(scip);
   assert(rows != NULL || nslacks == 0);
     
   SCIP_CALL( SCIPallocBufferArray(scip, &upslacks, nslacks) );
   SCIP_CALL( SCIPallocBufferArray(scip, &downslacks, nslacks) );
   SCIP_CALL( SCIPallocBufferArray(scip, &activities, nslacks) );

   numericalerror = FALSE;
   nroundings = 0; 

   /* calculate row slacks for every every row that belongs to the current LP and ensure, that the current solution
    * has no violated constraint -- if any constraint is violated, i.e. a slack is significantly smaller than zero, 
    * this will cause the termination of the heuristic because Zirounding does not provide feasibility recovering 
    */
   for( i = 0; i < nslacks; ++i ) 
   {
      SCIP_ROW*          row;
      SCIP_Real          lhs;      
      SCIP_Real          rhs;
             
      row = rows[i];
      
      assert(row != NULL);
      
      lhs = SCIProwGetLhs(row);
      rhs = SCIProwGetRhs(row);
       
      /* get row activity */
      activities[i] = SCIPgetRowSolActivity(scip, row, GCGrelaxGetCurrentOrigSol(scip));
      /* TODO: This assertion has been commented out due to numerical troubles */
//      assert( SCIPisFeasLE(scip, lhs, activities[i]) && SCIPisFeasLE(scip, activities[i], rhs) );
      
      /* in special case if LHS or RHS is (-)infinity slacks have to be initialized as infinity*/
      if ( SCIPisInfinity(scip, -lhs) ) 
         downslacks[i] = SCIPinfinity(scip);
      else 
         downslacks[i] = activities[i] - lhs;
      
      if( SCIPisInfinity(scip, rhs) )
         upslacks[i] = SCIPinfinity(scip);
      else
         upslacks[i] = rhs - activities[i];

      /* due to numerical inaccuracies, the rows might be feasible, even if the slacks are 
       * significantly smaller than zero -> terminate 
       */      
      if( SCIPisFeasLT(scip, upslacks[i], 0.0) || SCIPisFeasLT(scip, downslacks[i], 0.0) )
         goto TERMINATE;
   }

   assert(nslacks == 0 || (upslacks != NULL && downslacks != NULL && activities != NULL));

   /* initialize number of remaining variables and flag to enter the main loop */
   currentlpcands = nlpcands;
   improvementfound = TRUE;           
   *result = SCIP_DIDNOTFIND;

   /* check if fractional rounding candidates are left in each round, 
    * whereas number of rounds is limited by parameter maxroundingloops
    */
   while( currentlpcands > 0 && improvementfound && nroundings < heurdata->maxroundingloops )
   { 
      int c;

      improvementfound = FALSE;
      nroundings++;
      SCIPdebugMessage("GCG zirounding enters while loop for %d time with %d candidates left. \n", nroundings, currentlpcands);

      /* check for every remaining fractional variable if a shifting decreases ZI-value of the variable */
      for( c = 0; c < currentlpcands; ++c )
      {
         SCIP_VAR* var;
         SCIP_Real oldsolval;
         SCIP_Real upperbound;
         SCIP_Real lowerbound;
         SCIP_Real up;          
         SCIP_Real down;        
         SCIP_Real ziup;        
         SCIP_Real zidown;      
         SCIP_Real zicurrent;   
         SCIP_Real shiftval;

         DIRECTION direction;

         /* get values from local data */ 
         oldsolval = solarray[c];
         var = zilpcands[c];

         assert(!SCIPisFeasIntegral(scip, oldsolval));
         assert(SCIPvarGetStatus(var) == SCIP_VARSTATUS_COLUMN);

         /* calculate bounds for variable and make sure that there are no numerical inconsistencies */
         upperbound = SCIPinfinity(scip);
         lowerbound = -SCIPinfinity(scip);
         calculateBounds(scip, var, oldsolval, &upperbound, &lowerbound, upslacks, downslacks, nslacks, &numericalerror);

         if( numericalerror )
            goto TERMINATE;
            
         /* calculate the the possible values after shifting */
         up   = oldsolval + upperbound;
         down = oldsolval - lowerbound;

         /* if the variable is integer, do not shift further than the nearest integer */
         if( SCIPvarGetType(var) == SCIP_VARTYPE_INTEGER )
         {
            SCIP_Real ceilx;
            SCIP_Real floorx;

            ceilx = SCIPfeasCeil(scip, oldsolval);
            floorx = SCIPfeasFloor(scip, oldsolval);
            up   = MIN(up, ceilx);
            down = MAX(down, floorx);
         }

         /** calculate necessary values */
         ziup      = getZiValue(scip, up);
         zidown    = getZiValue(scip, down);
         zicurrent = getZiValue(scip, oldsolval);

         /* calculate the shifting direction that reduces ZI-value the most,
          * if both directions improve ZI-value equally, take the direction which improves the objective
          */
         if( SCIPisFeasLT(scip, zidown, zicurrent) || SCIPisFeasLT(scip, ziup, zicurrent) )
         {
            if( SCIPisFeasEQ(scip,ziup, zidown) )
               direction  = SCIPisFeasGE(scip, SCIPvarGetObj(var), 0.0) ? DIRECTION_DOWN : DIRECTION_UP;
            else if( SCIPisFeasLT(scip, zidown, ziup) )
               direction = DIRECTION_DOWN;
            else
               direction = DIRECTION_UP;
      
            /* once a possible shifting direction and value have been found, variable value is updated */
            shiftval = (direction == DIRECTION_UP ? up-oldsolval : down-oldsolval);
            
            /* update the solution */
            solarray[c] = oldsolval + shiftval;
            SCIP_CALL( SCIPsetSolVal(scip, sol, var, solarray[c]) );
  
            /* update the rows activities and slacks */
            updateSlacks(scip, sol, var, &shiftval, upslacks,
               downslacks, activities, nslacks);

            SCIPdebugMessage("GCG zirounding update step : %d var index, oldsolval=%g, shiftval=%g \n ",
               SCIPvarGetIndex(var), oldsolval, shiftval);
            /* since at least one improvement has been found, heuristic will enter main loop for another time because the improvement
             * might affect many LP rows and their current slacks and thus make further rounding steps possible */
            improvementfound = TRUE;
         }

         /* if solution value of variable has become feasibly integral due to rounding step,
          * variable is put at the end of remaining candidates array so as not to be considered in future loops
          */
         if( SCIPisFeasIntegral(scip, solarray[c]) )
         {
            zilpcands[c] = zilpcands[currentlpcands - 1];
            solarray[c] = solarray[currentlpcands - 1];
            currentlpcands--;

            /* counter is decreased if end of candidates array has not been reached yet */
            if( c < currentlpcands )
               c--;
         }
         else if( nroundings == heurdata->maxroundingloops - 1 )
            goto TERMINATE;
      }
   }

   /* in case that no candidate is left for rounding after the final main loop 
    * the found solution has to be checked for feasibility in the original problem
    */
   if( currentlpcands == 0 )
   {           
      SCIP_Bool stored;
      SCIP_CALL(SCIPtrySol(scip, sol, FALSE, FALSE, TRUE, FALSE, &stored));
      if ( stored )
      {
#ifdef SCIP_DEBUG
         SCIPdebugMessage("found feasible rounded solution:\n");
         SCIP_CALL( SCIPprintSol(scip, sol, NULL, FALSE) );
#endif
         *result = SCIP_FOUNDSOL;
      }
   }

   /* free memory for all locally allocated data */
 TERMINATE:
   SCIPfreeBufferArray(scip, &activities);
   SCIPfreeBufferArray(scip, &downslacks);
   SCIPfreeBufferArray(scip, &upslacks);
   SCIPfreeBufferArray(scip, &zilpcands);
   SCIPfreeBufferArray(scip, &solarray);

   return SCIP_OKAY;
}

/*
 * primal heuristic specific interface methods
 */

/** creates the GCG zirounding primal heuristic and includes it in SCIP */
SCIP_RETCODE SCIPincludeHeurGcgzirounding(
   SCIP*                 scip                /**< SCIP data structure */
   )
{
   SCIP_HEURDATA* heurdata;

   /* create GCG zirounding primal heuristic data */
   SCIP_CALL( SCIPallocMemory(scip, &heurdata) );

   /* include primal heuristic */
   SCIP_CALL( SCIPincludeHeur(scip, HEUR_NAME, HEUR_DESC, HEUR_DISPCHAR, HEUR_PRIORITY, HEUR_FREQ, HEUR_FREQOFS,
         HEUR_MAXDEPTH, HEUR_TIMING, HEUR_USESSUBSCIP,
         heurCopyGcgzirounding, heurFreeGcgzirounding, heurInitGcgzirounding, heurExitGcgzirounding,
         heurInitsolGcgzirounding, heurExitsolGcgzirounding, heurExecGcgzirounding,
         heurdata) );

   /* add GCG zirounding primal heuristic parameters */
   SCIP_CALL( SCIPaddIntParam(scip, "heuristics/gcgzirounding/maxroundingloops",
         "determines maximum number of rounding loops",
         &heurdata->maxroundingloops, TRUE, DEFAULT_MAXROUNDINGLOOPS, 0, INT_MAX, NULL, NULL) );
   SCIP_CALL( SCIPaddBoolParam(scip, "heuristics/gcgzirounding/stopziround",
         "flag to determine if Zirounding is deactivated after a certain percentage of unsuccessful calls",
         &heurdata->stopziround, TRUE, DEFAULT_STOPZIROUND, NULL, NULL) );
   SCIP_CALL( SCIPaddRealParam(scip,"heuristics/gcgzirounding/stoppercentage",
         "if percentage of found solutions falls below this parameter, Zirounding will be deactivated",
         &heurdata->stoppercentage, TRUE, DEFAULT_STOPPERCENTAGE, 0.0, 1.0, NULL, NULL) );
   SCIP_CALL( SCIPaddIntParam(scip, "heuristics/gcgzirounding/minstopncalls",
         "determines the minimum number of calls before percentage-based deactivation of"
         " Zirounding is applied", &heurdata->minstopncalls, TRUE, DEFAULT_MINSTOPNCALLS, 1, INT_MAX, NULL, NULL) );

   return SCIP_OKAY;
}