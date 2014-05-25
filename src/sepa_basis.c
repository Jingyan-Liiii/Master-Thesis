/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                           */
/*                  This file is part of the program and library             */
/*         SCIP --- Solving Constraint Integer Programs                      */
/*                                                                           */
/*    Copyright (C) 2002-2012 Konrad-Zuse-Zentrum                            */
/*                            fuer Informationstechnik Berlin                */
/*                                                                           */
/*  SCIP is distributed under the terms of the ZIB Academic License.         */
/*                                                                           */
/*  You should have received a copy of the ZIB Academic License              */
/*  along with SCIP; see the file COPYING. If not email to scip@zib.de.      */
/*                                                                           */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/**@file   sepa_basis.c
 * @brief  basis separator
 * @author Jonas Witt
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

/*#define SCIP_DEBUG*/

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "scip/scip.h"
#include "scip/lp.h"
#include "scip/scipdefplugins.h"
#include "scip_misc.h"
#include "sepa_basis.h"
#include "sepa_master.h"
#include "gcg.h"
#include "relax_gcg.h"
#include "pricer_gcg.h"
#include "pub_gcgvar.h"
#include "scip/var.h"


#define SEPA_NAME              "basis"
#define SEPA_DESC              "separator calculates a basis of the orig problem to generate cuts, which cut off the master lp sol"
#define SEPA_PRIORITY                100
#define SEPA_FREQ                     0
#define SEPA_MAXBOUNDDIST           1.0
#define SEPA_USESSUBSCIP           FALSE /**< does the separator use a secondary SCIP instance? */
#define SEPA_DELAY                FALSE /**< should separation method be delayed, if other separators found cuts? */

#define STARTMAXCUTS 50       /**< maximal cuts used at the beginning */
#define MAXCUTSINC   20       /**< increase of allowed number of cuts */


/*
 * Data structures
 */

/** separator data */
struct SCIP_SepaData
{
   SCIP_ROW**            mastercuts;         /**< cuts in the master problem */
   SCIP_ROW**            origcuts;           /**< cuts in the original problem */
   int                   norigcuts;          /**< number of cuts in the original problem */
   int                   nmastercuts;        /**< number of cuts in the master problem */
   int                   maxcuts;            /**< maximal number of allowed cuts */
   SCIP_ROW**            newcuts;            /**< new cuts to tighten original problem */
   int                   nnewcuts;           /**< number of new cuts */
   int                   maxnewcuts;         /**< maximal number of allowed new cuts */
   SCIP_ROW*             objrow;             /**< row with obj coefficients */
   int                   nlpcuts;            /**< number of cuts, which cut of the basic solution */
   int                   nprimalsols;        /**< number of primal solutions found */
   SCIP_Real             shifteddiffendgeom; /**< mean l2-norm difference between original solution and lp solution */
   SCIP_Real             shifteddiffstartgeom;/**< mean l2-norm difference between original solution and dive lp solution */
   SCIP_Real             shiftedconvexgeom;  /**< mean calculated convex coefficient */
   int                   ncalculatedconvex;  /**< number of calculated lp solution (and convex and l2 diff) */
   SCIP_Real             shiftediterationsfound; /**< mean number of iterations until usefull cuts were found */
   SCIP_Real             shiftediterationsnotfound; /**< mean number of iterations until no cuts at all were found */
   int                   nfound;             /**< number of calls where useful cuts were found */
   int                   nnotfound;          /**< number of calls where no useful cuts were found */
   SCIP_Bool             enable;             /**< parameter returns if basis separator is enabled */
   SCIP_Bool             enableobj;          /**< parameter returns if objective constraint is enabled */
   SCIP_Bool             enableobjround;     /**< parameter returns if rhs/lhs of objective constraint is rounded, when obj is int */
   SCIP_Bool             enableppcuts;       /**< parameter returns if cuts generated during pricing are added to newconss array */
   SCIP_Bool             enableppobjconss;   /**< parameter returns if objective constraint for each redcost of pp is enabled */
   SCIP_Bool             enableppobjcg;      /**< parameter returns if objective constraint for each redcost of pp is enabled during pricing */
   SCIP_Bool             aggressive;         /**< parameter returns if aggressive separation is used */
   SCIP_Bool             chgobj;             /**< parameter returns if basis is searched with different objective */
   SCIP_Bool             chgobjallways;      /**< parameter returns if obj is not only changed in first iteration */
   SCIP_Bool             genobjconvex;       /**< parameter returns if objconvex is generated dynamically */
   SCIP_Bool             enableposslack;     /**< parameter returns if positive slack should influence the dive objective function */
   int                   posslackexp;        /**< parameter return exponent of usage of positive slack */
   int                   iterations;         /**< parameter returns number of new rows adding iterations (rows just cut off dive lp sol) */
   int                   mincuts;            /**< parameter returns number of minimum cuts needed to return *result = SCIP_Separated */
   SCIP_Real             objconvex;          /**< parameter return convex combination factor */
   int                   ncgcut;             /**< number of cgcuts */
   int                   nclique;            /**< number of clique cuts */
   int                   ncmir;              /**< number of cmir cuts */
   int                   nflowcover;         /**< number of flowcover cuts */
   int                   ngom;               /**< number of gomory cuts */
   int                   nimplbd;            /**< number of implied bounds cuts */
   int                   nmcf;               /**< number of mcf cuts */
   int                   noddcycle;          /**< number of oddcycle cuts */
   int                   nscg;               /**< number of strong cg cuts */
   int                   nzerohalf;          /**< number of zero half cuts */
};

/*
 * Local methods
 */

/** allocates enough memory to hold more cuts */
static
SCIP_RETCODE ensureSizeCuts(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_SEPADATA*        sepadata,           /**< separator data data structure */
   int                   size                /**< new size of cut arrays */
   )
{
   assert(scip != NULL);
   assert(sepadata != NULL);
   assert(sepadata->mastercuts != NULL);
   assert(sepadata->origcuts != NULL);
   assert(sepadata->norigcuts <= sepadata->maxcuts);
   assert(sepadata->norigcuts >= 0);
   assert(sepadata->nmastercuts <= sepadata->maxcuts);
   assert(sepadata->nmastercuts >= 0);

   if( sepadata->maxcuts < size )
   {
      while ( sepadata->maxcuts < size )
      {
         sepadata->maxcuts += MAXCUTSINC;
      }
      SCIP_CALL( SCIPreallocMemoryArray(scip, &(sepadata->mastercuts), sepadata->maxcuts) );
      SCIP_CALL( SCIPreallocMemoryArray(scip, &(sepadata->origcuts), sepadata->maxcuts) );
   }
   assert(sepadata->maxcuts >= size);

   return SCIP_OKAY;
}

/** allocates enough memory to hold more new cuts */
static
SCIP_RETCODE ensureSizeNewCuts(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_SEPADATA*        sepadata,           /**< separator data data structure */
   int                   size                /**< new size of cut arrays */
   )
{
   assert(scip != NULL);
   assert(sepadata != NULL);
   assert(sepadata->newcuts != NULL);
   assert(sepadata->nnewcuts <= sepadata->maxnewcuts);
   assert(sepadata->nnewcuts >= 0);

   if( sepadata->maxnewcuts < size )
   {
      while ( sepadata->maxnewcuts < size )
      {
         sepadata->maxnewcuts += MAXCUTSINC;
      }
      SCIP_CALL( SCIPreallocMemoryArray(scip, &(sepadata->newcuts), sepadata->maxnewcuts) );
   }
   assert(sepadata->maxnewcuts >= size);

   return SCIP_OKAY;
}

/** allocates enough memory to hold more rows in a matrix */
static
SCIP_RETCODE ensureSizeRowsOfMatrix(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_Real***          matrix,             /**< pointer to matrix */
   int                   ncols,              /**< fixed number of columns of the matrix */
   int                   nrows,              /**< current number of rows */
   int*                  maxrows,            /**< maximal number of rows */
   int                   size                /**< new size of cut arrays */
   )
{
   assert(scip != NULL);

   if( *maxrows < size )
   {
      while ( *maxrows < size )
      {
         *maxrows += MAXCUTSINC;
      }
      SCIP_CALL( SCIPreallocMemoryArray(scip, matrix, *maxrows) );
   }
   assert(*maxrows >= size);

   return SCIP_OKAY;
}

/* computes basis^exp */
static
SCIP_Real exponentiate(
   SCIP_Real            basis,               /**< basis for exponentiation */
   int                  exponent            /**< exponent for exponentiation */
   )
{
   SCIP_Real result;
   int i;

   assert(exponent >= 0);

   result = 1.0;
   for(i = 0; i < exponent; ++i)
   {
      result *= basis;
   }

   return result;
}

/**< Initialize dive objective coefficient for each variable with original objective. */
static
SCIP_RETCODE initProbingObjWithOrigObj(
   SCIP*                origscip,           /**< orig scip problem */
   SCIP_Bool            enableobj,          /**< returns if objective row was added to the lp */
   SCIP_Real            objfactor           /**< factor, the objective is multiplied with */
)
{
   SCIP_VAR** origvars;
   int norigvars;
   SCIP_VAR* origvar;

   SCIP_Real newobj;
   int i;

   assert(SCIPinProbing(origscip));

   origvars = SCIPgetVars(origscip);
   norigvars = SCIPgetNVars(origscip);

   /** loop over original variables */
   for(i = 0; i < norigvars; ++i)
   {
      /* get variable information */
      origvar = origvars[i];
      newobj = 0.0;

      /* if objective row is enabled consider also the original objective value */
      if(enableobj)
         newobj = objfactor * SCIPvarGetObj(origvar);

      SCIPchgVarObjProbing(origscip, origvar, newobj);
   }
   return SCIP_OKAY;
}

/**< Change dive objective coefficient for each variable by adding original objective
 *   to the dive objective.
 */
static
SCIP_RETCODE chgProbingObjAddingOrigObj(
   SCIP*                origscip,           /**< orig scip problem */
   SCIP_Real            objfactor,          /**< factor the objective is multiplied with */
   SCIP_Real            objdivisor          /**< factor the objective is divided with */
)
{
   SCIP_VAR** origvars;
   int norigvars;
   SCIP_VAR* origvar;

   SCIP_Real newobj;
   int i;

   assert(SCIPinProbing(origscip));

   origvars = SCIPgetVars(origscip);
   norigvars = SCIPgetNVars(origscip);

   /** loop over original variables */
   for(i = 0; i < norigvars; ++i)
   {
      /* get variable information */
      origvar = origvars[i];

      newobj = SCIPgetVarObjProbing(origscip, origvar) + SCIPvarGetObj(origvar);

      SCIPchgVarObjProbing(origscip, origvar, (objfactor * newobj)/objdivisor);
   }
   return SCIP_OKAY;
}

/**< Initialize dive objective coefficient for each variable depending on the current origsol.
 *
 *   If variable is at upper bound set objective to -1, if variable is at lower bound set obj to 1,
 *   else set obj to 0.
 *   Additionally, add original objective to the dive objective if this is enabled.
 */
static
SCIP_RETCODE initProbingObjUsingVarBounds(
   SCIP*                origscip,           /**< orig scip problem */
   SCIP_SEPADATA*       sepadata,           /**< separator specific data */
   SCIP_SOL*            origsol,            /**< orig solution */
   SCIP_Bool            enableobj,          /**< returns if objective row was added to the lp */
   SCIP_Real            objfactor           /**< factor the objective is multiplied with */
)
{
   SCIP_Bool enableposslack;
   int posslackexp;

   SCIP_VAR** origvars;
   int norigvars;
   SCIP_VAR* origvar;

   SCIP_Real lb;
   SCIP_Real ub;
   SCIP_Real solval;
   SCIP_Real newobj;
   SCIP_Real distance;

   int i;

   origvars = SCIPgetVars(origscip);
   norigvars = SCIPgetNVars(origscip);

   enableposslack = sepadata->enableposslack;
   posslackexp = sepadata->posslackexp;

   /** loop over original variables */
   for(i = 0; i < norigvars; ++i)
   {
      /* get variable information */
      origvar = origvars[i];
      lb = SCIPvarGetLbLocal(origvar);
      ub = SCIPvarGetUbLocal(origvar);
      solval = SCIPgetSolVal(origscip, origsol, origvar);

      assert(SCIPisLE(origscip, solval, ub));
      assert(SCIPisGE(origscip, solval, lb));

      /* if solution value of variable is at ub or lb initialize objective value of the variable
       * such that the difference to this bound is minimized
       */
      if(SCIPisLT(origscip, ub, SCIPinfinity(origscip)) && SCIPisLE(origscip, ub, solval))
      {
         newobj = -1.0;
      }
      else if(SCIPisGT(origscip, lb, -SCIPinfinity(origscip)) && SCIPisGE(origscip, lb, solval))
      {
         newobj = 1.0;
      }
      else if(enableposslack)
      {
         /* compute distance from solution to variable bound */
         distance = MIN(solval - lb, ub - solval);

         assert(SCIPisPositive(origscip, distance));

         /* check if distance is lower than 1 and compute factor */
         if(SCIPisLT(origscip, distance, 1.0))
         {
            newobj = exponentiate(MAX(0.0, 1.0 - distance), posslackexp);

            /* check if algebraic sign has to be changed */
            if(SCIPisLT(origscip, distance, solval - lb))
               newobj = -newobj;
         }
         else
         {
            newobj = 0.0;
         }
      }
      else
      {
         newobj = 0.0;
      }

      /* if objective row is enabled consider also the original objective value */
      if(enableobj)
         newobj = newobj + SCIPvarGetObj(origvar);

      SCIPchgVarObjProbing(origscip, origvar, objfactor*newobj);
   }

   return SCIP_OKAY;
}

/* Change dive objective depending on the current origsol.
 *
 * Loop over all constraints lhs <= sum a_i*x_i <= rhs. If lhs == sum a_i*x_i^* add a_i to objective
 * of variable i and if rhs == sum a_i*x_i^* add -a_i to objective of variable i.
 */
static
SCIP_RETCODE chgProbingObjUsingRows(
   SCIP*                origscip,           /**< orig scip problem */
   SCIP_SEPADATA*       sepadata,           /**< separator data */
   SCIP_SOL*            origsol,            /**< orig solution */
   SCIP_Real            objfactor,          /**< factor the objective is multiplied with */
   SCIP_Real            objdivisor          /**< factor the objective is divided with */
)
{
   SCIP_Bool enableposslack;
   int posslackexp;

   SCIP_ROW** rows;
   int nrows;
   SCIP_ROW* row;
   SCIP_Real* vals;
   SCIP_VAR** vars;
   SCIP_COL** cols;
   int nvars;

   SCIP_Real lhs;
   SCIP_Real rhs;
   SCIP_Real* solvals;
   SCIP_Real activity;
   SCIP_Real factor;
   SCIP_Real objadd;
   SCIP_Real obj;
   SCIP_Real norm;
   SCIP_Real distance;

   int i;
   int j;

   rows = SCIPgetLPRows(origscip);
   nrows = SCIPgetNLPRows(origscip);

   enableposslack = sepadata->enableposslack;
   posslackexp = sepadata->posslackexp;

   assert(SCIPinProbing(origscip));

   SCIP_CALL( SCIPallocBufferArray(origscip, &solvals, SCIPgetNVars(origscip)) );
   SCIP_CALL( SCIPallocBufferArray(origscip, &vars, SCIPgetNVars(origscip)) );

   /** loop over constraint and check activity */
   for(i = 0; i < nrows; ++i)
   {
      row = rows[i];
      lhs = SCIProwGetLhs(row);
      rhs = SCIProwGetRhs(row);

      nvars = SCIProwGetNNonz(row);
      if(nvars == 0 || (sepadata->objrow != NULL && strcmp(SCIProwGetName(row),SCIProwGetName(sepadata->objrow)) == 0 ))
         continue;

      /* get values, variables and solution values */
      vals = SCIProwGetVals(row);
      cols = SCIProwGetCols(row);
      for(j = 0; j < nvars; ++j)
      {
         vars[j] = SCIPcolGetVar(cols[j]);
      }

      activity = SCIPgetRowSolActivity(origscip, row, origsol);

      if(SCIPisEQ(origscip, rhs, lhs))
      {
         continue;
      }
      if(SCIPisLT(origscip, rhs, SCIPinfinity(origscip)) && SCIPisLE(origscip, rhs, activity))
      {
         factor = -1.0;
      }
      else if(SCIPisGT(origscip, lhs, -SCIPinfinity(origscip)) && SCIPisGE(origscip, lhs, activity))
      {
         factor = 1.0;
      }
      else if(enableposslack)
      {
         assert(!(SCIPisInfinity(origscip, rhs) && SCIPisInfinity(origscip, lhs)));
         assert(!(SCIPisInfinity(origscip, activity) && SCIPisInfinity(origscip, -activity)));

         /* compute distance from solution to row */
         if(SCIPisInfinity(origscip, rhs) && SCIPisGT(origscip, lhs, -SCIPinfinity(origscip)))
            distance = activity - lhs;
         else if(SCIPisInfinity(origscip, lhs) && SCIPisLT(origscip, rhs, SCIPinfinity(origscip)))
            distance = rhs - activity;
         else
            distance = MIN(activity - lhs, rhs - activity);

         assert(SCIPisPositive(origscip, distance) || !SCIPisCutEfficacious(origscip, origsol, row));
         /* check if distance is lower than 1 and compute factor */
         if(SCIPisLT(origscip, distance, 1.0))
         {
            factor = exponentiate(MAX(0.0, 1.0 - distance), posslackexp);

            /* check if algebraic sign has to be changed */
            if(SCIPisLT(origscip, distance, activity - lhs))
               factor = -1.0*factor;
         }
         else
         {
            continue;
         }
      }
      else
      {
         continue;
      }

      norm = SCIProwGetNorm(row);

      /** loop over variables of the constraint and change objective */
      for(j = 0; j < nvars; ++j)
      {
         obj = SCIPgetVarObjProbing(origscip, vars[j]);
         objadd = (objfactor * factor * vals[j]) / norm;

         SCIPchgVarObjProbing(origscip, vars[j], obj + /*objsense **/ objadd / objdivisor);
      }
   }

   SCIPfreeBufferArray(origscip, &solvals);
   SCIPfreeBufferArray(origscip, &vars);

   return SCIP_OKAY;
}

/* returns square of number */
static
SCIP_Real getSquare(
   SCIP_Real            number
   )
{
   return number*number;
}

/* returns l2-norm of difference of solutions */
static
SCIP_Real getL2DiffSols(
   SCIP*                scip,               /**< SCIP data structure */
   SCIP_SOL*            sol1,               /**< first solution */
   SCIP_SOL*            sol2                /**< second solution */
)
{
   SCIP_VAR** vars;
   int nvars;

   SCIP_Real diff;
   SCIP_Real solval1;
   SCIP_Real solval2;
   int i;

   vars = SCIPgetVars(scip);
   nvars = SCIPgetNVars(scip);

   diff = 0.0;

   for(i = 0; i < nvars; ++i)
   {
      solval1 = SCIPgetSolVal(scip, sol1, vars[i]);
      solval2 = SCIPgetSolVal(scip, sol2, vars[i]);

      diff += getSquare(solval1 - solval2);
   }

   diff = sqrt(diff);

   return diff;
}


/**< Get matrix (including nrows and ncols) of rows that are satisfied with equality by sol */
static
SCIP_RETCODE getEqualityMatrix(
   SCIP*                scip,               /**< SCIP data structure */
   SCIP_SOL*            sol,                /**< solution */
   SCIP_Real***         matrix,             /**< pointer to store equality matrix */
   int*                 nrows,              /**< pointer to store number of rows */
   int*                 ncols               /**< pointer to store number of columns */
)
{
   int maxrows;

   SCIP_ROW** lprows;
   int nlprows;

   SCIP_COL** lpcols;
   int nlpcols;

   int i;
   int j;

   *ncols = SCIPgetNLPCols(scip);
   nlprows = SCIPgetNLPRows(scip);
   lprows = SCIPgetLPRows(scip);
   nlpcols = SCIPgetNLPCols(scip);
   lpcols = SCIPgetLPCols(scip);

   maxrows = STARTMAXCUTS;
   SCIP_CALL( SCIPallocMemoryArray(scip, matrix, maxrows) );

   *nrows = 0;

   /* loop over lp cols and check if it is at one of its bounds */
   for(i = 0; i < nlpcols; ++i)
   {
      SCIP_COL* lpcol;
      SCIP_VAR* lpvar;

      lpcol = lpcols[i];

      lpvar = SCIPcolGetVar(lpcol);

      if(SCIPisEQ(scip, SCIPgetSolVal(scip, sol, lpvar), SCIPcolGetUb(lpcol))
         || SCIPisEQ(scip, SCIPgetSolVal(scip, sol, lpvar), SCIPcolGetLb(lpcol)))
      {
         int ind;

         ind = SCIPcolGetIndex(lpcol);

         SCIP_CALL( ensureSizeRowsOfMatrix(scip, matrix, *ncols, *nrows, &maxrows, *nrows + 1) );

         SCIP_CALL( SCIPallocMemoryArray(scip, &((*matrix)[*nrows]), *ncols) );

         /* init row with zero coefficients */
         for(j = 0; j < *ncols; ++j)
         {
            if(ind != j)
               (*matrix)[*nrows][j] = 0.0;
            else
               (*matrix)[*nrows][j] = 1.0;
         }
         ++(*nrows);

      }
   }

   /* loop over lp rows and check if solution feasibility is equal to zero */
   for(i = 0; i < nlprows; ++i)
   {
      SCIP_ROW* lprow;

      lprow = lprows[i];

      /* if solution feasiblity is equal to zero, add row to matrix */
      if(SCIPisEQ(scip, SCIPgetRowSolFeasibility(scip, lprow, sol), 0.0))
      {
         SCIP_COL** cols;
         SCIP_Real* vals;
         int nnonz;

         cols = SCIProwGetCols(lprow);
         vals = SCIProwGetVals(lprow);
         nnonz = SCIProwGetNNonz(lprow);

         SCIP_CALL( ensureSizeRowsOfMatrix(scip, matrix, *ncols, *nrows, &maxrows, *nrows + 1) );

         SCIP_CALL( SCIPallocMemoryArray(scip, &((*matrix)[*nrows]), *ncols) );

         /* init row with zero coefficients */
         for(j = 0; j < *ncols; ++j)
         {
            (*matrix)[*nrows][j] = 0.0;
         }

         /* get nonzero coefficients of row */
         for(j = 0; j < nnonz; ++j)
         {
            int ind;
            ind = SCIPcolGetIndex(cols[j]);
            assert(ind >= 0 && ind < *ncols);

            (*matrix)[*nrows][ind] = vals[j];
         }
         ++(*nrows);
      }
   }
   return SCIP_OKAY;
}

/**< Get matrix (inclinding nrows and ncols) of all rows */
static
SCIP_RETCODE getRowMatrix(
   SCIP*                scip,               /**< SCIP data structure */
   SCIP_Real***         matrix,             /**< pointer to store equality matrix */
   int*                 nrows,              /**< pointer to store number of rows */
   int*                 ncols               /**< pointer to store number of columns */
)
{
   int maxrows;

   SCIP_ROW** lprows;
   int nlprows;

   SCIP_COL** lpcols;
   int nlpcols;

   int i;
   int j;

   *ncols = SCIPgetNLPCols(scip);
   nlprows = SCIPgetNLPRows(scip);
   lprows = SCIPgetLPRows(scip);
   nlpcols = SCIPgetNLPCols(scip);
   lpcols = SCIPgetLPCols(scip);

   maxrows = STARTMAXCUTS;
   SCIP_CALL( SCIPallocMemoryArray(scip, matrix, maxrows) );

   *nrows = 0;

   /* loop over lp cols and check if it is at one of its bounds */
   for(i = 0; i < nlpcols; ++i)
   {
      SCIP_COL* lpcol;
      int ind;

      lpcol = lpcols[i];
      ind = SCIPcolGetIndex(lpcol);

      SCIP_CALL( ensureSizeRowsOfMatrix(scip, matrix, *ncols, *nrows, &maxrows, *nrows + 1) );

      SCIP_CALL( SCIPallocMemoryArray(scip, &((*matrix)[*nrows]), *ncols) );

      /* init row with zero coefficients */
      for(j = 0; j < *ncols; ++j)
      {
         if(ind != j)
            (*matrix)[*nrows][j] = 0.0;
         else
            (*matrix)[*nrows][j] = 1.0;
      }
      ++(*nrows);
   }

   /* loop over lp rows and check if solution feasibility is equal to zero */
   for(i = 0; i < nlprows; ++i)
   {
      SCIP_ROW* lprow;
      SCIP_COL** cols;
      SCIP_Real* vals;
      int nnonz;

      lprow = lprows[i];
      cols = SCIProwGetCols(lprow);
      vals = SCIProwGetVals(lprow);
      nnonz = SCIProwGetNNonz(lprow);

      SCIP_CALL( ensureSizeRowsOfMatrix(scip, matrix, *ncols, *nrows, &maxrows, *nrows + 1) );

      SCIP_CALL( SCIPallocMemoryArray(scip, &((*matrix)[*nrows]), *ncols) );

      /* init row with zero coefficients */
      for(j = 0; j < *ncols; ++j)
      {
         (*matrix)[*nrows][j] = 0.0;
      }

      /* get nonzero coefficients of row */
      for(j = 0; j < nnonz; ++j)
      {
         int ind;
         ind = SCIPcolGetIndex(cols[j]);
         assert(ind >= 0 && ind < *ncols);

         (*matrix)[*nrows][ind] = vals[j];
      }
      ++(*nrows);
   }

   return SCIP_OKAY;
}

/* replace row with (row*factor) */
static
SCIP_RETCODE rowMultiplyFactor(
   SCIP_Real*           row,                /**< row */
   int                  length,             /**< length of row */
   SCIP_Real            factor              /**< factor */
)
{
   int i;

   for(i = 0; i < length; ++i)
   {
      row[i] = factor * row[i];
   }
   return SCIP_OKAY;
}

/* replace row1 with (row1 + factor * row2) */
static
SCIP_RETCODE rowAddRowFactor(
   SCIP_Real*           row1,               /**< row */
   SCIP_Real*           row2,               /**< row */
   int                  length,             /**< length of row */
   SCIP_Real            factor              /**< factor */
   )
{
   int i;

   for(i = 0; i < length; ++i)
   {
      row1[i] = row1[i] + factor * row2[i];
   }
   return SCIP_OKAY;
}

/* Apply Gaussian pivot step */
static
SCIP_RETCODE pivotStep(
   SCIP*                scip,               /**< SCIP data structure */
   SCIP_Real**          matrix,             /**< equality matrix */
   int                  nrows,              /**< store number of rows */
   int                  ncols,              /**< number of columns */
   int                  prow,               /**< pivot row */
   int                  pcol                /**< pivot column */
)
{
   int i;

   for(i = 0; i < nrows; ++i)
   {
      if(i != prow && !SCIPisEQ(scip, matrix[i][pcol], 0.0))
      {
         SCIP_CALL( rowAddRowFactor(matrix[i], matrix[prow], ncols, -1.0* matrix[i][pcol]/matrix[prow][pcol]) );
      }
      else if(i == prow)
      {
         SCIP_CALL( rowMultiplyFactor(matrix[i], ncols, 1/matrix[prow][pcol]) );
      }
   }
   return SCIP_OKAY;
}

/**< Use Gaussian elimination to get row rank of matrix and store row rank and a row basis */
static
SCIP_RETCODE gaussianElimination(
   SCIP*                scip,               /**< SCIP data structure */
   SCIP_Real**          matrix,             /**< matrix */
   int                  nrows,              /**< store number of rows */
   int                  ncols,              /**< number of columns */
   int**                basisrows,          /**< pointer to store array of basis row indices */
   int*                 rowrank             /**< pointer to store rank (number of basis rows) */
)
{
   SCIP_Bool* boolbasis;
   int c;
   int r;

   SCIP_CALL( SCIPallocMemoryArray(scip, basisrows, ncols) );
   SCIP_CALL( SCIPallocMemoryArray(scip, &boolbasis, nrows) );

   *rowrank = 0;

   for(r = 0; r < nrows; ++r)
   {
      boolbasis[r] = FALSE;
   }

   for(c = 0; c < ncols; ++c)
   {
      r = 0;
      while(r < nrows && (SCIPisEQ(scip, matrix[r][c], 0.0) || boolbasis[r] == TRUE))
      {
         ++r;
      }

      if(r == nrows)
         continue;

      /* apply Gaussian pivot step */
      SCIP_CALL( pivotStep(scip, matrix, nrows, ncols, r, c) );

      /* update basis information */
      boolbasis[r] = TRUE;
      (*basisrows)[*rowrank] = r;
      ++(*rowrank);
   }

   SCIPfreeMemoryArray(scip, &boolbasis)

   return SCIP_OKAY;
}

/**< Get rank (number of linear independent rows) of rows that are satisfied
 *   with equality by solution sol */
static
SCIP_RETCODE getEqualityRank(
   SCIP*                scip,               /**< SCIP data structure */
   SCIP_SOL*            sol,                /**< solution */
   int*                 equalityrank        /**< pointer to store rank of rows with equality */
   )
{
   SCIP_Real** matrix;
   int nrows;
   int ncols;

   int* basisrows;
   int rowrank;

   int i;

   SCIP_CALL( getEqualityMatrix(scip, sol, &matrix, &nrows, &ncols) );

   SCIP_CALL( gaussianElimination(scip, matrix, nrows, ncols, &basisrows, &rowrank) );

   SCIPfreeMemoryArray(scip, &basisrows);

   for(i = 0; i < nrows; ++i)
   {
      SCIPfreeMemoryArray(scip, &(matrix[i]) );
   }
   SCIPfreeMemoryArray(scip, &matrix);

   *equalityrank = rowrank;

   return SCIP_OKAY;
}

/**< Get rank (number of linear independent rows) of all lp rows */
static
SCIP_RETCODE getRowRank(
   SCIP*                scip,               /**< SCIP data structure */
   int*                 nbasis              /**< pointer to store number of rows needed to obtain basis */
   )
{
   SCIP_Real** matrix;
   int nrows;
   int ncols;

   int* basisrows;
   int rowrank;

   int i;

   SCIP_CALL( getRowMatrix(scip, &matrix, &nrows, &ncols) );

   SCIP_CALL( gaussianElimination(scip, matrix, nrows, ncols, &basisrows, &rowrank) );

   SCIPfreeMemoryArray(scip, &basisrows);

   for(i = 0; i < nrows; ++i)
   {
      SCIPfreeMemoryArray(scip, &(matrix[i]) );
   }
   SCIPfreeMemoryArray(scip, &matrix);

   *nbasis = rowrank;

   return SCIP_OKAY;
}

/** Add cuts which are due to the latest objective function of the pricing problems
 *  (reduced cost non-negative) */
static
SCIP_RETCODE addPPObjConss(
   SCIP*                scip,               /**< SCIP data structure */
   SCIP_SEPA*           sepa,               /**< separator basis */
   int                  ppnumber,           /**< number of pricing problem */
   SCIP_Real            dualsolconv         /**< dual solution corresponding to convexity constraint */
)
{
   SCIP* pricingscip;

   SCIP_VAR** pricingvars;
   SCIP_VAR* var;

   int npricingvars;
   int nvars;

   char name[SCIP_MAXSTRLEN];

   int j;
   int k;

   SCIP_OBJSENSE objsense;

   SCIP_Real lhs;
   SCIP_Real rhs;

   nvars = 0;
   pricingscip = GCGgetPricingprob(scip, ppnumber);
   pricingvars = SCIPgetOrigVars(pricingscip);
   npricingvars = SCIPgetNOrigVars(pricingscip);

   if(!GCGrelaxIsPricingprobRelevant(scip, ppnumber) || pricingscip == NULL)
      return SCIP_OKAY;

   objsense = SCIPgetObjsense(pricingscip);

   if(objsense == SCIP_OBJSENSE_MINIMIZE)
   {
      lhs = dualsolconv;
      rhs = SCIPinfinity(scip);
   }
   else
   {
      SCIPinfoMessage(scip, NULL, "pricing problem is maximization problem \n");
      rhs = dualsolconv;
      lhs = -SCIPinfinity(scip);
   }

   for(k = 0; k < GCGrelaxGetNIdenticalBlocks(scip, ppnumber); ++k)
   {
      SCIP_ROW* origcut;

      (void) SCIPsnprintf(name, SCIP_MAXSTRLEN, "newconstraint_%d_%d_%d", SCIPsepaGetNCalls(sepa), ppnumber, k);

      SCIP_CALL( SCIPcreateEmptyRowUnspec(scip, &origcut, name, lhs, rhs, FALSE, FALSE, TRUE) );

      nvars = 0;

      for(j = 0; j < npricingvars ; ++j)
      {
         assert( GCGvarIsPricing(pricingvars[j]) );

         if(!SCIPisEQ(scip, SCIPvarGetObj(pricingvars[j]), 0.0))
         {
            var = GCGpricingVarGetOrigvars(pricingvars[j])[k];
            assert(var != NULL);
            SCIP_CALL( SCIPaddVarToRow(scip, origcut, var, SCIPvarGetObj(pricingvars[j])) );
            ++nvars;
         }
      }

      if(nvars > 0)
      {
         SCIPdebug( SCIPprintRow(scip, origcut, NULL) );

         SCIP_CALL( SCIPaddRowProbing(scip, origcut) );
         SCIPdebugMessage("cut added to dive\n");

      }
      SCIP_CALL(SCIPreleaseRow(scip, &origcut) );
   }

   return SCIP_OKAY;
}
/*
 * Callback methods of separator
 */

/** copy method for separator plugins (called when SCIP copies plugins) */
#if 0
static
SCIP_DECL_SEPACOPY(sepaCopyBasis)
{  /*lint --e{715}*/
   SCIPerrorMessage("method of basis separator not implemented yet\n");
   SCIPABORT(); /*lint --e{527}*/

   return SCIP_OKAY;
}
#else
#define sepaCopyBasis NULL
#endif

/** destructor of separator to free user data (called when SCIP is exiting) */
static
SCIP_DECL_SEPAFREE(sepaFreeBasis)
{  /*lint --e{715}*/
   SCIP_SEPADATA* sepadata;

   int ncalls;
   int ncutsfound;
   int ncutsapplied;
   int nlpcuts;
   int nprimalsols;

   SCIP_Real meancutsfound;
   SCIP_Real meanlpcutsfound;
   SCIP_Real meancutsapplied;
   SCIP_Real time;

   sepadata = SCIPsepaGetData(sepa);

   /* get separator information */
   ncalls = SCIPsepaGetNCalls(sepa);
   ncutsfound = SCIPsepaGetNCutsFound(sepa);
   ncutsapplied = SCIPsepaGetNCutsApplied(sepa);
   nlpcuts = sepadata->nlpcuts;
   nprimalsols = sepadata->nprimalsols;
   time = SCIPsepaGetTime(sepa);

   if(ncalls > 0)
   {
      meancutsfound = (1.0*ncutsfound)/ncalls;
      meancutsapplied = (1.0*ncutsapplied)/ncalls;
      meanlpcutsfound = (1.0*nlpcuts)/ncalls;
   }
   else
   {
      meancutsfound = 0.0;
      meancutsapplied = 0.0;
      meanlpcutsfound = 0.0;
   }
   if(!sepadata->genobjconvex)
   {
      sepadata->shiftedconvexgeom = sepadata->objconvex;
   }
   else
   {
      sepadata->shiftedconvexgeom = sepadata->shiftedconvexgeom - 1.0;
   }
   sepadata->shifteddiffstartgeom = sepadata->shifteddiffstartgeom - 1.0;
   sepadata->shifteddiffendgeom = sepadata->shifteddiffendgeom - 1.0;
   sepadata->shiftediterationsfound = sepadata->shiftediterationsfound - 1.0;
   sepadata->shiftediterationsnotfound = sepadata->shiftediterationsnotfound - 1.0;

   /* print separator information */
   SCIPinfoMessage(scip, NULL, "            time ncalls ncfound ncapplied nlpcfound mncfound mncapplied mnlpcfound nprimalsols convex diffstart diffend itfound itnfound\n");
   SCIPinfoMessage(scip, NULL, "SepaBasis:  %5.2f %6d %7d %9d %9d  %7.2f %10.2f %10.2f %11d %6.6f %4.3f %6.3f %6.3f %6.3f \n", time, ncalls, ncutsfound,
                  ncutsapplied, nlpcuts, meancutsfound, meancutsapplied, meanlpcutsfound, nprimalsols, sepadata->shiftedconvexgeom, sepadata->shifteddiffstartgeom, sepadata->shifteddiffendgeom,
                  sepadata->shiftediterationsfound, sepadata->shiftediterationsnotfound);

   SCIPinfoMessage(scip, NULL, "                bCuts\n");
   SCIPinfoMessage(scip, NULL, "clique         %6d\n", sepadata->nclique);
   SCIPinfoMessage(scip, NULL, "cmir           %6d\n", sepadata->ncmir);
   SCIPinfoMessage(scip, NULL, "flowcover      %6d\n", sepadata->nflowcover);
   SCIPinfoMessage(scip, NULL, "gomory         %6d\n", sepadata->ngom);
   SCIPinfoMessage(scip, NULL, "impliedbounds  %6d\n", sepadata->nimplbd);
   SCIPinfoMessage(scip, NULL, "mcf            %6d\n", sepadata->nmcf);
   SCIPinfoMessage(scip, NULL, "oddcycle       %6d\n", sepadata->noddcycle);
   SCIPinfoMessage(scip, NULL, "strongcg       %6d\n", sepadata->nscg);
   SCIPinfoMessage(scip, NULL, "zerohalf       %6d\n", sepadata->nzerohalf);

   SCIPfreeMemoryArrayNull(scip, &(sepadata->origcuts));
   SCIPfreeMemoryArrayNull(scip, &(sepadata->mastercuts));
   SCIPfreeMemoryArrayNull(scip, &(sepadata->newcuts));

   SCIPfreeMemory(scip, &sepadata);

   return SCIP_OKAY;
}

/** initialization method of separator (called after problem was transformed) */
static
SCIP_DECL_SEPAINIT(sepaInitBasis)
{  /*lint --e{715}*/
   SCIP*   origscip;
   SCIP_SEPADATA* sepadata;

   SCIP_VAR** origvars;
   int norigvars;

   char name[SCIP_MAXSTRLEN];

   SCIP_Real obj;
   int i;

   SCIP_Bool enable;
   SCIP_Bool enableobj;

   assert(scip != NULL);

   origscip = GCGpricerGetOrigprob(scip);
   assert(origscip != NULL);

   sepadata = SCIPsepaGetData(sepa);
   assert(sepadata != NULL);

   origvars = SCIPgetVars(origscip);
   norigvars = SCIPgetNVars(origscip);

   SCIPdebugMessage("sepaInitBasis\n");

   enable = sepadata->enable;
   enableobj = sepadata->enableobj;

   /* if separator is disabled do nothing */
   if(!enable)
   {
      return SCIP_OKAY;
   }

   /* if objective row is enabled create row with objective coefficients */
   if(enableobj)
   {
      (void) SCIPsnprintf(name, SCIP_MAXSTRLEN, "objrow");
      SCIP_CALL( SCIPcreateEmptyRowUnspec(origscip, &(sepadata->objrow), name, -SCIPinfinity(origscip), SCIPinfinity(origscip), TRUE, FALSE, TRUE) );

      for(i = 0; i < norigvars; ++i)
      {
         obj = SCIPvarGetObj(origvars[i]);
         SCIP_CALL( SCIPaddVarToRow(origscip, sepadata->objrow, origvars[i], obj) );
      }
   }

   return SCIP_OKAY;
}


/** deinitialization method of separator (called before transformed problem is freed) */
static
SCIP_DECL_SEPAEXIT(sepaExitBasis)
{  /*lint --e{715}*/
   SCIP* origscip;
   SCIP_SEPADATA* sepadata;
   SCIP_Bool enableobj;

   int i;

   sepadata = SCIPsepaGetData(sepa);
   assert(sepadata != NULL);
   enableobj = sepadata->enableobj;
   assert(sepadata->nmastercuts == sepadata->norigcuts);

   origscip = GCGpricerGetOrigprob(scip);
   assert(origscip != NULL);

   for( i = 0; i < sepadata->norigcuts; i++ )
   {
      SCIP_CALL( SCIPreleaseRow(origscip, &(sepadata->origcuts[i])) );
   }

   for(i = 0; i < sepadata->nnewcuts; ++i)
   {
      if(sepadata->newcuts[i] != NULL)
         SCIP_CALL( SCIPreleaseRow(origscip, &(sepadata->newcuts[i])) );
   }

   if(enableobj)
      SCIP_CALL( SCIPreleaseRow(origscip, &(sepadata->objrow)) );

   return SCIP_OKAY;
}

/** solving process initialization method of separator (called when branch and bound process is about to begin) */
#if 0
static
SCIP_DECL_SEPAINITSOL(sepaInitsolBasis)
{  /*lint --e{715}*/
   SCIPerrorMessage("method of basis separator not implemented yet\n");
   SCIPABORT(); /*lint --e{527}*/

   return SCIP_OKAY;
}
#else
#define sepaInitsolBasis NULL
#endif


/** solving process deinitialization method of separator (called before branch and bound process data is freed) */
static
SCIP_DECL_SEPAEXITSOL(sepaExitsolBasis)
{  /*lint --e{715}*/
   SCIP_SEPADATA* sepadata;
   int i;

   sepadata = SCIPsepaGetData(sepa);
   assert(sepadata != NULL);
   assert(sepadata->nmastercuts == sepadata->norigcuts);

   assert(GCGpricerGetOrigprob(scip) != NULL);

   for( i = 0; i < sepadata->nmastercuts; i++ )
   {
      SCIP_CALL( SCIPreleaseRow(scip, &(sepadata->mastercuts[i])) );
   }

   return SCIP_OKAY;
}


/**< Initialize objective due to generation of convex combination */
static
SCIP_RETCODE initGenconv(
   SCIP*                origscip,           /**< original SCIP data structure */
   SCIP_SEPADATA*       sepadata,           /**< separator data structure */
   SCIP_SOL*            origsol,            /**< current original solution */
   int                  nbasis,             /**< rank of constraint matrix */
   SCIP_Real*           convex              /**< pointer to store convex combination coefficient */
)
{
   int rank;
   int ncalls;

   SCIP_CALL( getEqualityRank(origscip, origsol, &rank) );

   *convex = 1.0* rank/nbasis;

   SCIPinfoMessage(origscip, NULL, "genconv = %d/%d = %f\n", rank, nbasis, *convex);

   ncalls = sepadata->ncalculatedconvex;

   sepadata->shiftedconvexgeom = pow(sepadata->shiftedconvexgeom, 1.0*ncalls/(ncalls + 1))
                           * pow(MAX(*convex+1.0, 1.0),1.0/(ncalls + 1));
   ++(sepadata->ncalculatedconvex);

   return SCIP_OKAY;
}


/**< Initialize objective due to generation of convex combination */
static
SCIP_RETCODE initConvObj(
   SCIP*                origscip,           /**< original SCIP data structure */
   SCIP_SEPADATA*       sepadata,           /**< separator data structure */
   SCIP_SOL*            origsol,            /**< current original solution */
   SCIP_Real            convex,             /**< convex coefficient to initialize objective */
   SCIP_Bool            genericconv         /**< was convex coefficient calculated generically? */
)
{
   SCIP_Real objnormnull;
   SCIP_Real objnormcurrent;

   objnormnull = 1.0;
   objnormcurrent = 1.0;

   if(SCIPisEQ(origscip, convex, 0.0))
   {
      SCIP_CALL( initProbingObjWithOrigObj(origscip, TRUE, 1.0) );
   }
   else if(SCIPisLT(origscip, convex, 1.0))
   {
      SCIP_CALL( initProbingObjWithOrigObj(origscip, TRUE, 1.0) );
      objnormnull = SCIPgetObjNorm(origscip);

      SCIP_CALL( initProbingObjUsingVarBounds(origscip, sepadata, origsol, FALSE, convex) );
      SCIP_CALL( chgProbingObjUsingRows(origscip, sepadata, origsol, convex, 1.0) );

      objnormcurrent = SCIPgetObjNorm(origscip)/(convex);

      if(SCIPisEQ(origscip, objnormcurrent, 0.0))
         SCIP_CALL( initProbingObjWithOrigObj(origscip, TRUE, 1.0) );
      else if(SCIPisGT(origscip, objnormnull, 0.0) )
         SCIP_CALL( chgProbingObjAddingOrigObj(origscip, (1.0 - convex) * objnormcurrent, objnormnull) );
   }
   else if(SCIPisEQ(origscip, convex, 1.0))
   {
      SCIP_CALL( initProbingObjUsingVarBounds(origscip, sepadata, origsol, !genericconv && sepadata->enableobj, 1.0) );
      SCIP_CALL( chgProbingObjUsingRows(origscip, sepadata, origsol, 1.0, 1.0) );
   }
   return SCIP_OKAY;
}



/** LP solution separation method of separator */
static
SCIP_DECL_SEPAEXECLP(sepaExeclpBasis)
{  /*lint --e{715}*/

   SCIP* origscip;
   SCIP_SEPADATA* sepadata;

   SCIP_SEPA** sepas;
   int nsepas;

   SCIP_ROW** cuts;
   SCIP_ROW* mastercut;
   SCIP_ROW* origcut;
   SCIP_COL** cols;
   SCIP_VAR** roworigvars;
   SCIP_VAR** mastervars;
   SCIP_Real* mastervals;
   int ncols;
   int ncuts;
   SCIP_Real* vals;
   int nmastervars;

   SCIP_OBJSENSE objsense;
   SCIP_SOL* origsol;
   SCIP_Bool feasible;
   SCIP_Bool lperror;
   SCIP_Bool delayed;
   SCIP_Bool cutoff;
   SCIP_Bool infeasible;
   SCIP_Real obj;
   int ncalls;

   SCIP_Real mineff;
   SCIP_Real mineffroot;
   int maxrounds;
   int maxroundsroot;

   SCIP_Bool enable;
   SCIP_Bool enableobj;
   SCIP_Bool enableobjround;
   SCIP_Bool enableppobjconss;

   char name[SCIP_MAXSTRLEN];

   int i;
   int j;
   int iteration;
   int nbasis;
   int nlprowsstart;
   int nlprows;
   SCIP_ROW** lprows;

   assert(scip != NULL);
   assert(result != NULL);

   origscip = GCGpricerGetOrigprob(scip);
   assert(origscip != NULL);

   sepadata = SCIPsepaGetData(sepa);
   assert(sepadata != NULL);

   SCIPdebugMessage("sepaExeclpBasis\n");

   *result = SCIP_DIDNOTFIND;

   enable = sepadata->enable;
   enableobj = sepadata->enableobj;
   enableobjround = sepadata->enableobjround;
   enableppobjconss = sepadata->enableppobjconss;

   /* if separator is disabled do nothing */
   if(!enable)
   {
      *result = SCIP_DIDNOTRUN;
      return SCIP_OKAY;
   }

   /* ensure master LP is solved to optimality */
   if( SCIPgetLPSolstat(scip) != SCIP_LPSOLSTAT_OPTIMAL )
   {
      SCIPdebugMessage("master LP not solved to optimality, do no separation!\n");
      *result = SCIP_DIDNOTRUN;
      return SCIP_OKAY;
   }

   SCIP_CALL( SCIPgetRealParam(origscip, "separating/minefficacy", &mineff) );
   SCIP_CALL( SCIPgetRealParam(origscip, "separating/minefficacyroot", &mineffroot) );
   SCIP_CALL( SCIPsetRealParam(origscip, "separating/minefficacy", mineffroot) );

   SCIP_CALL( SCIPgetIntParam(origscip, "separating/maxrounds", &maxrounds) );
   SCIP_CALL( SCIPgetIntParam(origscip, "separating/maxroundsroot", &maxroundsroot) );
   SCIP_CALL( SCIPsetIntParam(origscip, "separating/maxrounds", maxroundsroot) );

   /* update current original solution */
   SCIP_CALL( GCGrelaxUpdateCurrentSol(origscip, &feasible) );

   /* get current original solution */
   origsol = GCGrelaxGetCurrentOrigSol(origscip);

   /* get obj and objsense */
   objsense = SCIPgetObjsense(origscip);
   obj = SCIPgetSolOrigObj(origscip, origsol);

   /** get number of linearly independent rows needed for basis */
   if( sepadata->genobjconvex )
   {
      SCIP_CALL(getRowRank(origscip, &nbasis));
   }

   *result = SCIP_DIDNOTFIND;

   /* init iteration counter */
   iteration = 0;

   /* set separating to aggressive or default */
   if( sepadata->aggressive )
   {
      SCIP_CALL( SCIPsetSeparating(origscip, SCIP_PARAMSETTING_AGGRESSIVE, TRUE) );
   }
   else
   {
      SCIP_CALL( SCIPsetSeparating(origscip, SCIP_PARAMSETTING_DEFAULT, TRUE) );
   }

   /* start diving */
   SCIP_CALL( SCIPstartProbing(origscip) );

   SCIP_CALL( SCIPnewProbingNode(origscip) );

   SCIP_CALL( SCIPconstructLP(origscip, &cutoff) );

   /** add origcuts to probing lp */
   for( i = 0; i < GCGsepaGetNCuts(scip); ++i )
   {
      SCIP_CALL( SCIPaddRowProbing(origscip, GCGsepaGetOrigcuts(scip)[i]) );
   }

   /** add new cuts which did not cut off master sol to probing lp */
   for( i = 0; i < sepadata->nnewcuts; ++i )
   {
      SCIP_CALL( SCIPaddRowProbing(origscip, sepadata->newcuts[i]) );
   }

   /* store numeber of lp rows in the beginning */
   nlprowsstart = SCIPgetNLPRows(origscip);

   /* while the counter is smaller than the number of allowed iterations,
    * try to separate origsol via dive lp sol */
   while( iteration < sepadata->iterations )
   {
      SCIP_CALL( SCIPapplyCutsProbing(origscip, &cutoff) );

      /* add new constraints if this is enabled  */
      if( enableppobjconss && iteration == 0 )
      {
         SCIP_Real* dualsolconv;
         SCIP_CALL( SCIPallocMemoryArray(scip, &dualsolconv, GCGrelaxGetNPricingprobs(origscip)));
         SCIP_CALL( GCGsetPricingObjs(scip, dualsolconv) );

         for( i = 0; i < GCGgetNPricingprobs(origscip); ++i )
         {
            SCIP_CALL( addPPObjConss(origscip, sepa, i, dualsolconv[i]) );
         }

         SCIPfreeMemoryArray(scip, &dualsolconv);
      }

      /* init objective */
      if( sepadata->chgobj )
      {
         if( sepadata->genobjconvex )
         {
            SCIP_Real genconvex;

            SCIP_CALL( initGenconv(origscip, sepadata, origsol, nbasis, &genconvex) );
            SCIP_CALL( initConvObj(origscip, sepadata, origsol, genconvex, TRUE) );
         }
         else
         {
            SCIP_CALL( initConvObj(origscip, sepadata, origsol, sepadata->objconvex, FALSE) );
         }
      }

      /* update rhs/lhs of objective constraint and add it to dive LP, if it exists (only in first iteration) */
      if( enableobj && iteration == 0 )
      {
         /* round rhs/lhs of objective constraint, if it exists, obj is integral and this is enabled */
         if( SCIPisObjIntegral(origscip) && enableobjround )
         {
            if( objsense == SCIP_OBJSENSE_MAXIMIZE )
            {
               obj = SCIPfloor(origscip, obj);
            }
            else
            {
               obj = SCIPceil(origscip, obj);
            }
         }

         /* update rhs/lhs of objective constraint */
         if( objsense == SCIP_OBJSENSE_MAXIMIZE )
         {
            SCIP_CALL( SCIPchgRowRhs(origscip, sepadata->objrow, obj) );
            SCIP_CALL( SCIPchgRowLhs(origscip, sepadata->objrow, -1.0*SCIPinfinity(origscip)) );
         }
         else
         {
            SCIP_CALL( SCIPchgRowLhs(origscip, sepadata->objrow, obj) );
            SCIP_CALL( SCIPchgRowRhs(origscip, sepadata->objrow, SCIPinfinity(origscip)) );
         }
         /** add row to dive lp */
         SCIP_CALL( SCIPaddRowProbing(origscip, sepadata->objrow) );
      }

      /* solve dive lp */
      SCIP_CALL( SCIPsolveProbingLP(origscip, -1, &lperror, &cutoff) );


      assert( !lperror );

      /* update mean differences */
      if( iteration == 0 )
      {
         ncalls = SCIPsepaGetNCalls(sepa);
         sepadata->shifteddiffstartgeom = pow(sepadata->shifteddiffstartgeom, 1.0*ncalls/(ncalls + 1))
                                  * pow(MAX(getL2DiffSols(origscip, origsol, NULL) + 1.0, 1.0), 1.0/(ncalls + 1));
      }

      /* get separators of origscip */
      sepas = SCIPgetSepas(origscip);
      nsepas = SCIPgetNSepas(origscip);

      /* loop over sepas and enable/disable sepa */
      for( i = 0; i < nsepas; ++i )
      {
         const char* sepaname;
         char paramname[SCIP_MAXSTRLEN];

         sepaname = SCIPsepaGetName(sepas[i]);

         (void) SCIPsnprintf(paramname, SCIP_MAXSTRLEN, "separating/%s/freq", sepaname);

         /* disable intobj, closecuts, rapidlearning and cgmip separator*/
         if( strcmp(sepaname, "intobj") == 0 || strcmp(sepaname, "closecuts") == 0
            || strcmp(sepaname, "rapidlearning") == 0
            || (strcmp(sepaname, "cgmip") == 0))
         {
            SCIP_CALL( SCIPsetIntParam(origscip, paramname, -1) );
            SCIPdebugMessage("%s = %d\n", paramname, -1);
         }
         else
         {
            SCIP_CALL( SCIPsetIntParam(origscip, paramname, 0) );
            SCIPdebugMessage("%s = %d\n", paramname, 0);
         }
      }
      /** separate current dive lp sol of origscip */
      SCIP_CALL( SCIPseparateSol(origscip, NULL, TRUE, FALSE, &delayed, &cutoff) );

      if( delayed && !cutoff )
      {
         SCIP_CALL( SCIPseparateSol(origscip, NULL, TRUE, TRUE, &delayed, &cutoff) );
      }

      /* if cut off is detected set result pointer and return SCIP_OKAY */
      if( cutoff )
      {
         *result = SCIP_CUTOFF;
         SCIPinfoMessage(scip, NULL, "SCIPseparateSol() detected cut off\n");
         SCIPendProbing(origscip);

         /* disable separating again */
         SCIP_CALL( SCIPsetSeparating(origscip, SCIP_PARAMSETTING_OFF, TRUE) );

         return SCIP_OKAY;
      }

      /* update number of lp cuts */
      sepadata->nlpcuts += SCIPgetNCuts(origscip);

      assert(sepadata->norigcuts == sepadata->nmastercuts);

      SCIPdebugMessage("SCIPseparateSol() found %d cuts!\n", SCIPgetNCuts(origscip));

      SCIPinfoMessage(scip, NULL,"SCIPseparateSol() found %d cuts!\n", SCIPgetNCuts(origscip));

      /* get separated cuts */
      cuts = SCIPgetCuts(origscip);
      ncuts = SCIPgetNCuts(origscip);

      SCIP_CALL( ensureSizeCuts(scip, sepadata, sepadata->norigcuts + ncuts) );

      mastervars = SCIPgetVars(scip);
      nmastervars = SCIPgetNVars(scip);
      SCIP_CALL( SCIPallocBufferArray(scip, &mastervals, nmastervars) );

      /** loop over cuts and transform cut to master problem (and safe cuts) if it seperates origsol */
      for( i = 0; i < ncuts; i++ )
      {
         SCIP_Bool colvarused;

         colvarused = FALSE;
         origcut = cuts[i];

         SCIPdebugMessage("cutname = %s \n", SCIProwGetName(origcut));

         /* get columns and vals of the cut */
         ncols = SCIProwGetNNonz(origcut);
         cols = SCIProwGetCols(origcut);
         vals = SCIProwGetVals(origcut);

         /* get the variables corresponding to the columns in the cut */
         SCIP_CALL( SCIPallocBufferArray(scip, &roworigvars, ncols) );
         for( j = 0; j < ncols; j++ )
         {
            roworigvars[j] = SCIPcolGetVar(cols[j]);
            assert(roworigvars[j] != NULL);
            if( !GCGvarIsOriginal(roworigvars[j]) )
            {
               colvarused = TRUE;
               break;
            }
         }

         if( colvarused )
         {
            SCIPinfoMessage(origscip, NULL, "colvar used\n");
            SCIPfreeBufferArray(scip, &roworigvars);
            continue;
         }

         if( !SCIPisCutEfficacious(origscip, origsol, origcut) )
         {
            SCIP_CALL( SCIPaddPoolCut(origscip, origcut) );
            SCIPfreeBufferArray(scip, &roworigvars);

            continue;
         }

         if( strncmp("cgcut", SCIProwGetName(origcut), 5) == 0 )
         {
            ++(sepadata->ncgcut);
         }
         else if( strncmp("clique", SCIProwGetName(origcut), 6) == 0 )
         {
            ++(sepadata->nclique);
         }
         else if( strncmp("cmir", SCIProwGetName(origcut), 4) == 0 )
         {
            ++(sepadata->ncmir);
         }
         else if( strncmp("flowcover", SCIProwGetName(origcut), 9) == 0 )
         {
            ++(sepadata->nflowcover);
         }
         else if( strncmp("gom", SCIProwGetName(origcut), 3) == 0 )
         {
            ++(sepadata->ngom);
         }
         else if( strncmp("implbd", SCIProwGetName(origcut), 6) == 0 )
         {
            ++(sepadata->nimplbd);
         }
         else if( strncmp("mcf", SCIProwGetName(origcut), 3) == 0 )
         {
            ++(sepadata->nmcf);
         }
         else if( strncmp("oddcycle", SCIProwGetName(origcut), 8) == 0 )
         {
            ++(sepadata->noddcycle);
         }
         else if( strncmp("scg", SCIProwGetName(origcut), 3) == 0 )
         {
            ++(sepadata->nscg);
         }
         else if( strncmp("zerohalf", SCIProwGetName(origcut), 8) == 0 )
         {
            ++(sepadata->nzerohalf);
         }

         /* add the cut to the original cut storage */
         sepadata->origcuts[sepadata->norigcuts] = origcut;
         SCIP_CALL( SCIPcaptureRow(origscip, sepadata->origcuts[sepadata->norigcuts]) );
         sepadata->norigcuts++;

         /* create new cut in the master problem */
         (void) SCIPsnprintf(name, SCIP_MAXSTRLEN, "mc_basis_%s", SCIProwGetName(origcut));
         SCIP_CALL( SCIPcreateEmptyRowSepa(scip, &mastercut, sepa, name,
               ( SCIPisInfinity(scip, -SCIProwGetLhs(origcut)) ?
                  SCIProwGetLhs(origcut) : SCIProwGetLhs(origcut) - SCIProwGetConstant(origcut)),
               ( SCIPisInfinity(scip, SCIProwGetRhs(origcut)) ?
                  SCIProwGetRhs(origcut) : SCIProwGetRhs(origcut) - SCIProwGetConstant(origcut)),
                  SCIProwIsLocal(origcut), TRUE, FALSE) );

         /* transform the original variables to master variables and add them to the cut */
         GCGtransformOrigvalsToMastervals(origscip, roworigvars, vals, ncols, mastervars, mastervals, nmastervars);
         SCIP_CALL( SCIPaddVarsToRow(scip, mastercut, nmastervars, mastervars, mastervals) );

         /* add the cut to the master problem and to the master cut storage */
         SCIP_CALL( SCIPaddCut(scip, NULL, mastercut, FALSE, &infeasible) );
         sepadata->mastercuts[sepadata->nmastercuts] = mastercut;
         SCIP_CALL( SCIPcaptureRow(scip, sepadata->mastercuts[sepadata->nmastercuts]) );
         sepadata->nmastercuts++;
         SCIP_CALL( GCGsepaAddMastercuts(scip, origcut, mastercut) );

         /* todo: master cut does not have to be efficacious! */
         //assert( SCIPisCutEfficacious(scip, NULL, mastercut) );

   #ifdef SCIP_DEBUG
         SCIPdebugMessage("Cut %d:\n", i);
         SCIP_CALL( SCIPprintRow(scip, mastercut, NULL) );
         SCIPdebugMessage("\n\n");
   #endif

         SCIP_CALL( SCIPreleaseRow(scip, &mastercut) );
         SCIPfreeBufferArray(scip, &roworigvars);
      }

      if( SCIPgetNCuts(scip) >= sepadata->mincuts )
      {
         *result = SCIP_SEPARATED;
         ncalls = sepadata->nfound;
         sepadata->shiftediterationsfound = pow(sepadata->shiftediterationsfound, 1.0*ncalls/(1.0*ncalls + 1))
                                        * pow(MAX(1.0*iteration + 1.0 + 1.0, 1.0), 1.0/(1.0*ncalls + 1));

         ++(sepadata->nfound);
         iteration = sepadata->iterations;

      }
      else if( SCIPgetNCuts(origscip) == 0 )
      {
         ncalls = sepadata->nnotfound;
         sepadata->shiftediterationsnotfound = pow(sepadata->shiftediterationsnotfound, 1.0*ncalls/(1.0*ncalls + 1))
                                        * pow(MAX(1.0*iteration + 1.0 + 1.0, 1.0), 1.0/(1.0*ncalls + 1));
         ++(sepadata->nnotfound);
         iteration = sepadata->iterations;
      }
      else
      {
         ++iteration;
      }

      SCIPdebugMessage("%d cuts are in the original sepastore!\n", SCIPgetNCuts(origscip));
      SCIPdebugMessage("%d cuts are in the master sepastore!\n", SCIPgetNCuts(scip));

      SCIPinfoMessage(scip, NULL, "%d cuts are in the master sepastore!\n", SCIPgetNCuts(scip));

      SCIPfreeBufferArray(scip, &mastervals);

      assert(sepadata->norigcuts == sepadata->nmastercuts );
   }

   SCIP_CALL( SCIPclearCuts(origscip) );

   lprows = SCIPgetLPRows(origscip);
   nlprows = SCIPgetNLPRows(origscip);

   assert( nlprowsstart <= nlprows );

   SCIP_CALL( ensureSizeNewCuts(scip, sepadata, sepadata->nnewcuts + nlprows - nlprowsstart) );

   for( i = nlprowsstart; i < nlprows; ++i)
   {
      if( SCIProwGetOrigintype(lprows[i]) == SCIP_ROWORIGINTYPE_SEPA )
	   {
         sepadata->newcuts[sepadata->nnewcuts] = lprows[i];
	      SCIP_CALL( SCIPcaptureRow(origscip, sepadata->newcuts[sepadata->nnewcuts]) );
	      ++(sepadata->nnewcuts);
	   }
   }

   /* end diving */
   SCIPendProbing(origscip);

   /* update mean differences */
   ncalls = SCIPsepaGetNCalls(sepa);
   sepadata->shifteddiffendgeom = pow(sepadata->shifteddiffendgeom, 1.0*ncalls/(ncalls + 1))
                                  * pow(MAX(getL2DiffSols(origscip, origsol, NULL) + 1.0, 1.0), 1.0/(ncalls + 1));

   if( SCIPgetNCuts(scip) > 0 )
   {
      *result = SCIP_SEPARATED;
   }

   /* disable separating again */
   SCIP_CALL( SCIPsetSeparating(origscip, SCIP_PARAMSETTING_OFF, TRUE) );

   SCIP_CALL( SCIPsetRealParam(origscip, "separating/minefficacy", mineff) );
   SCIP_CALL( SCIPsetIntParam(origscip, "separating/maxrounds", maxrounds) );

   SCIPdebugMessage("separated origsol\n");

   return SCIP_OKAY;
}

/** arbitrary primal solution separation method of separator */
#if 0
static
SCIP_DECL_SEPAEXECSOL(sepaExecsolBasis)
{  /*lint --e{715}*/
   SCIPerrorMessage("method of basis separator not implemented yet\n");
   SCIPABORT(); /*lint --e{527}*/

   return SCIP_OKAY;
}
#else
#define sepaExecsolBasis NULL
#endif

/*
 * separator specific interface methods
 */

/** creates the basis separator and includes it in SCIP */
SCIP_RETCODE SCIPincludeSepaBasis(
   SCIP*                 scip                /**< SCIP data structure */
   )
{
   SCIP_SEPADATA* sepadata;

   /* create master separator data */
   SCIP_CALL( SCIPallocMemory(scip, &sepadata) );

   SCIP_CALL( SCIPallocMemoryArray(scip, &(sepadata->origcuts), STARTMAXCUTS) ); /*lint !e506*/
   SCIP_CALL( SCIPallocMemoryArray(scip, &(sepadata->mastercuts), STARTMAXCUTS) ); /*lint !e506*/
   SCIP_CALL( SCIPallocMemoryArray(scip, &(sepadata->newcuts), STARTMAXCUTS) ); /*lint !e506*/
   sepadata->maxcuts = STARTMAXCUTS;
   sepadata->norigcuts = 0;
   sepadata->nmastercuts = 0;
   sepadata->maxnewcuts = 0;
   sepadata->nnewcuts = 0;
   sepadata->objrow = NULL;
   sepadata->nprimalsols = 0;
   sepadata->nlpcuts = 0;
   sepadata->shifteddiffstartgeom = 1.0;
   sepadata->shifteddiffendgeom = 1.0;
   sepadata->ncalculatedconvex = 0;
   sepadata->shiftedconvexgeom = 1.0;
   sepadata->shiftediterationsfound = 1.0;
   sepadata->shiftediterationsnotfound = 1.0;
   sepadata->nfound = 0;
   sepadata->nnotfound = 0;

   sepadata->ncgcut = 0;
   sepadata->nclique = 0;
   sepadata->ncmir = 0;
   sepadata->nflowcover = 0;
   sepadata->ngom = 0;
   sepadata->nimplbd = 0;
   sepadata->nmcf = 0;
   sepadata->noddcycle = 0;
   sepadata->nscg = 0;
   sepadata->nzerohalf = 0;

   /* include separator */
   /* use SCIPincludeSepa() if you want to set all callbacks explicitly and realize (by getting compiler errors) when
    * new callbacks are added in future SCIP versions
    */
   SCIP_CALL( SCIPincludeSepa(scip, SEPA_NAME, SEPA_DESC, SEPA_PRIORITY, SEPA_FREQ, SEPA_MAXBOUNDDIST,
         SEPA_USESSUBSCIP, SEPA_DELAY,
         sepaCopyBasis, sepaFreeBasis, sepaInitBasis, sepaExitBasis, sepaInitsolBasis, sepaExitsolBasis, sepaExeclpBasis, sepaExecsolBasis,
         sepadata) );

   /* add basis separator parameters */
   SCIPaddBoolParam(GCGmasterGetOrigprob(scip), "sepa/basis/enable", "is basis separator enabled?",
         &(sepadata->enable), FALSE, FALSE, NULL, NULL);
   SCIPaddBoolParam(GCGmasterGetOrigprob(scip), "sepa/basis/enableobj", "is objective constraint of separator enabled?",
         &(sepadata->enableobj), FALSE, FALSE, NULL, NULL);
   SCIPaddBoolParam(GCGmasterGetOrigprob(scip), "sepa/basis/enableobjround", "round obj rhs/lhs of obj constraint if obj is int?",
         &(sepadata->enableobjround), FALSE, FALSE, NULL, NULL);
   SCIPaddBoolParam(GCGmasterGetOrigprob(scip), "sepa/basis/enableppcuts", "add cuts generated during pricing to newconss array?",
         &(sepadata->enableppcuts), FALSE, FALSE, NULL, NULL);
   SCIPaddBoolParam(GCGmasterGetOrigprob(scip), "sepa/basis/enableppobjconss", "is objective constraint for redcost of each pp of separator enabled?",
         &(sepadata->enableppobjconss), FALSE, FALSE, NULL, NULL);
   SCIPaddBoolParam(GCGmasterGetOrigprob(scip), "sepa/basis/enableppobjcg", "is objective constraint for redcost of each pp during pricing of separator enabled?",
         &(sepadata->enableppobjcg), FALSE, FALSE, NULL, NULL);
   SCIPaddBoolParam(GCGmasterGetOrigprob(scip), "sepa/basis/genobjconvex", "generated obj convex dynamically",
         &(sepadata->genobjconvex), FALSE, FALSE, NULL, NULL);
   SCIPaddBoolParam(GCGmasterGetOrigprob(scip), "sepa/basis/enableposslack", "should positive slack influence the dive objective function?",
         &(sepadata->enableposslack), FALSE, FALSE, NULL, NULL);
   SCIPaddIntParam(GCGmasterGetOrigprob(scip), "sepa/basis/posslackexp", "exponent of positive slack usage",
         &(sepadata->posslackexp), FALSE, 1, 1, INT_MAX, NULL, NULL);
   SCIPaddRealParam(GCGmasterGetOrigprob(scip), "sepa/basis/objconvex", "convex combination factor",
         &(sepadata->objconvex), FALSE, 1.0, 0.0, 1.0, NULL, NULL);
   SCIPaddBoolParam(GCGmasterGetOrigprob(scip), "sepa/basis/aggressive", "parameter returns if aggressive separation is used",
      &(sepadata->aggressive), FALSE, TRUE, NULL, NULL);
   SCIPaddBoolParam(GCGmasterGetOrigprob(scip), "sepa/basis/chgobj", "parameter returns if basis is searched with different objective",
      &(sepadata->chgobj), FALSE, TRUE, NULL, NULL);
   SCIPaddIntParam(GCGmasterGetOrigprob(scip), "sepa/basis/iterations", "parameter returns if number new rows adding"
      "iterations (rows just cut off dive lp sol)", &(sepadata->iterations), FALSE, 100, 1, 10000000 , NULL, NULL);
   SCIPaddIntParam(GCGmasterGetOrigprob(scip), "sepa/basis/mincuts", "parameter returns number of minimum cuts needed to "
      "return *result = SCIP_Separated", &(sepadata->mincuts), FALSE, 1, 1, 100, NULL, NULL);
   SCIPaddBoolParam(GCGmasterGetOrigprob(scip), "sepa/basis/chgobjallways", "parameter returns if obj is changed not only in the first iteration",
      &(sepadata->chgobjallways), FALSE, FALSE, NULL, NULL);

   return SCIP_OKAY;
}


/** returns the array of original cuts saved in the separator data */
SCIP_ROW** GCGsepaBasisGetOrigcuts(
   SCIP*                 scip                /**< SCIP data structure */
   )
{
   SCIP_SEPA* sepa;
   SCIP_SEPADATA* sepadata;

   assert( scip != NULL );

   sepa = SCIPfindSepa(scip, SEPA_NAME);
   assert( sepa != NULL );

   sepadata = SCIPsepaGetData(sepa);
   assert( sepadata != NULL );

   return sepadata->origcuts;
}

/** returns the number of original cuts saved in the separator data */
int GCGsepaBasisGetNOrigcuts(
   SCIP*                 scip                /**< SCIP data structure */
   )
{
   SCIP_SEPA* sepa;
   SCIP_SEPADATA* sepadata;

   assert( scip != NULL );

   sepa = SCIPfindSepa(scip, SEPA_NAME);
   assert(sepa != NULL);

   sepadata = SCIPsepaGetData(sepa);
   assert( sepadata != NULL );

   return sepadata->norigcuts;
}

/** returns the array of master cuts saved in the separator data */
SCIP_ROW** GCGsepaBasisGetMastercuts(
   SCIP*                 scip                /**< SCIP data structure */
   )
{
   SCIP_SEPA* sepa;
   SCIP_SEPADATA* sepadata;

   assert( scip != NULL );

   sepa = SCIPfindSepa(scip, SEPA_NAME);
   assert( sepa != NULL );

   sepadata = SCIPsepaGetData(sepa);
   assert( sepadata != NULL );

   return sepadata->mastercuts;
}

/** returns the number of master cuts saved in the separator data */
int GCGsepaBasisGetNMastercuts(
   SCIP*                 scip                /**< SCIP data structure */
   )
{
   SCIP_SEPA* sepa;
   SCIP_SEPADATA* sepadata;

   assert( scip != NULL );

   sepa = SCIPfindSepa(scip, SEPA_NAME);
   assert( sepa != NULL );

   sepadata = SCIPsepaGetData(sepa);
   assert( sepadata != NULL );

   return sepadata->nmastercuts;
}

/** transforms cut in pricing variables to cut in original variables and adds it to newcuts array */
extern
SCIP_RETCODE GCGsepaBasisAddPricingCut(
   SCIP*                scip,
   int                  ppnumber,
   SCIP_ROW*            cut
   )
{
   SCIP* origscip;
   SCIP_SEPA* sepa;
   SCIP_SEPADATA* sepadata;

   SCIP* pricingprob;
   SCIP_Real* vals;
   SCIP_COL** cols;
   SCIP_VAR** pricingvars;
   int nvars;

   int i;
   int j;
   int k;

   char name[SCIP_MAXSTRLEN];

   assert( GCGisMaster(scip) );

   sepa = SCIPfindSepa(scip, SEPA_NAME);

   if( sepa == NULL )
   {
      SCIPerrorMessage("sepa basis not found\n");
      return SCIP_OKAY;
   }

   sepadata = SCIPsepaGetData(sepa);
   origscip = GCGpricerGetOrigprob(scip);
   pricingprob = GCGrelaxGetPricingprob(origscip, ppnumber);

   if( !sepadata->enableppcuts )
   {
      return SCIP_OKAY;
   }

   assert( !SCIProwIsLocal(cut) );

   nvars = SCIProwGetNNonz(cut);
   cols = SCIProwGetCols(cut);
   vals = SCIProwGetVals(cut);

   if(nvars == 0)
   {
      return SCIP_OKAY;
   }

   SCIP_CALL( SCIPallocMemoryArray(scip, &pricingvars, nvars) );

   for( i = 0; i < nvars; ++i )
   {
      pricingvars[i] = SCIPcolGetVar(cols[i]);
      assert(pricingvars[i] != NULL);
   }

   for( k = 0; k < GCGgetNIdenticalBlocks(origscip, ppnumber); ++k )
   {
      SCIP_ROW* origcut;

      (void) SCIPsnprintf(name, SCIP_MAXSTRLEN, "ppcut_%d_%d_%d", SCIPsepaGetNCalls(sepa), ppnumber, k);

      SCIP_CALL( SCIPcreateEmptyRowUnspec(origscip, &origcut, name,
         ( SCIPisInfinity(pricingprob, -SCIProwGetLhs(cut)) ?
            -SCIPinfinity(origscip) : SCIProwGetLhs(cut) - SCIProwGetConstant(cut)),
         ( SCIPisInfinity(pricingprob, SCIProwGetRhs(cut)) ?
            SCIPinfinity(origscip) : SCIProwGetRhs(cut) - SCIProwGetConstant(cut)),
             FALSE, FALSE, TRUE) );

      for( j = 0; j < nvars ; ++j )
      {
         SCIP_VAR* var;

         if( !GCGvarIsPricing(pricingvars[j]) )
         {
            nvars = 0;
            break;
         }
         assert( GCGvarIsPricing(pricingvars[j]) );

         var = GCGpricingVarGetOrigvars(pricingvars[j])[k];
         assert( var != NULL );

         SCIP_CALL( SCIPaddVarToRow(origscip, origcut, var, vals[j]) );
      }

      if( nvars > 0 )
      {
         SCIPdebug( SCIPprintRow(origscip, origcut, NULL) );
         SCIP_CALL( SCIPaddPoolCut( origscip, origcut) );

         SCIPdebugMessage("cut added to orig cut pool\n");
      }
      SCIP_CALL( SCIPreleaseRow(origscip, &origcut) );
   }

   SCIPfreeMemoryArray(scip, &pricingvars);

   return SCIP_OKAY;
}

/** Add cuts which are due to the latest objective function of the pricing problems
 *  (reduced cost non-negative) */
SCIP_RETCODE SCIPsepaBasisAddPPObjConss(
   SCIP*                scip,               /**< SCIP data structure */
   int                  ppnumber,           /**< number of pricing problem */
   SCIP_Real            dualsolconv         /**< dual solution corresponding to convexity constraint */
)
{
   SCIP_SEPA* sepa;

   assert( GCGisMaster(scip) );

   sepa = SCIPfindSepa(scip, SEPA_NAME);

   if( sepa == NULL )
   {
      SCIPerrorMessage("sepa basis not found\n");
      return SCIP_OKAY;
   }

   SCIP_CALL( addPPObjConss(GCGmasterGetOrigprob(scip), sepa, ppnumber, dualsolconv) );

   return SCIP_OKAY;
}
