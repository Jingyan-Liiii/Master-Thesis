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
/* #define SCIP_DEBUG */
/* #define SCIP_OUTPUT */
/* #define SCIP_ENABLE_IISCHECK */

/**@file   cons_indicator.c
 * @ingroup CONSHDLRS
 * @brief  constraint handler for indicator constraints
 * @author Marc Pfetsch
 *
 * An indicator constraint is given by a binary variable \f$y\f$ and an inequality \f$ax \leq
 * b\f$. It states that if \f$y = 1\f$ then \f$ax \leq b\f$ holds.
 *
 * This constraint is handled by adding a slack variable \f$s:\; ax - s \leq b\f$ with \f$s \geq
 * 0\f$. The constraint is enforced by fixing \f$s\f$ to 0 if \f$y = 1\f$.
 *
 * @note The constraint only implements an implication not an equivalence, i.e., it does not ensure
 * that \f$y = 1\f$ if \f$ax \leq b\f$ or equivalently if \f$s = 0\f$ holds.
 *
 * This constraint is equivalent to a linear constraint \f$ax - s \leq b\f$ and an SOS1 constraint on
 * \f$y\f$ and \f$s\f$ (at most one should be nonzero). In the indicator context we can, however,
 * separate more inequalities.
 *
 * The name indicator apparently comes from ILOG CPLEX.
 *
 *
 * @section SEPARATION Separation Methods
 *
 * We now explain the handling of indicator constraints in more detail.  The indicator constraint
 * handler adds an inequality for each indicator constraint. We assume that this system (with added
 * slack variables) is \f$ Ax - s \leq b \f$, where \f$ x \f$ are the original variables and \f$ s
 * \f$ are the slack variables added by the indicator constraint. Variables \f$ y \f$ are the binary
 * variables corresponding to the indicator constraints.
 *
 * @note In the implementation, we assume that bounds on the original variables \f$x\f$ cannot be
 * influenced by the indicator constraint. If it should be possible to relax these constraints as
 * well, then these constraints have to be added as indicator constraints.
 *
 * We separate inequalities by using the so-called alternative polyhedron.
 *
 *
 * @section ALTERNATIVEPOLYHEDRON Separation via the Alternative Polyhedron
 *
 * We now describe the separation method of the first method in more detail.
 *
 * Consider the LP-relaxation of the current subproblem:
 * \f[
 * \begin{array}{ll}
 *  min & c^T x + d^T z\\
 *      & A x - s \leq b, \\
 *      & D x + C z \leq f, \\
 *      & l \leq x \leq u, \\
 *      & u \leq z \leq v, \\
 *      & 0 \geq s.
 * \end{array}
 * \f]
 * As above \f$Ax - s \leq b\f$ contains all inequalities corresponding to indicator constraints,
 * while the system \f$Dx + Cy \leq f\f$ contains all other inequalities (which are ignored in the
 * following). Similarly, variables \f$z\f$ not appearing in indicator constraints are
 * ignored. Bounds for the variables \f$x_j\f$ can be given, in particular, variables can be
 * fixed. Note that \f$s \leq 0\f$ renders the system infeasible.
 *
 * To generate cuts, we construct the so-called @a alternative @a polyhedron:
 * \f[
 * \begin{array}{ll}
 *      P = \{ (w,r,t) : & A^T w - r + t = 0,\\
 *                       & b^T w - l^T r + u^T t = -1,\\
 *                       & w, r, t \geq 0 \}.
 * \end{array}
 * \f]
 * Here, \f$r\f$ and \f$t\f$ correspond to the lower and upper bounds on \f$x\f$, respectively.
 *
 * It turns out that the vertices of \f$P\f$ correspond to minimal infeasible subsystems of \f$A x
 * \leq b\f$. If \f$I\f$ is the index set of such a system, it follows that not all \f$s_i\f$ for
 * \f$i \in I\f$ can be 0, i.e., \f$y_i\f$ can be 1. In other words, the following cut is valid:
 * \f[
 *      \sum_{i \in I} y_i \leq |I| - 1.
 * \f]
 *
 *
 * @subsection DETAIL Separation heuristic
 *
 * We separate the above inequalities by a heuristic described in
 *
 *   Branch-And-Cut for the Maximum Feasible Subsystem Problem,@n
 *   Marc Pfetsch, SIAM Journal on Optimization 19, No.1, 21-38 (2008)
 *
 * The first step in the separation heuristic is to apply the transformation \f$\bar{y} = 1 - y\f$, which
 * transforms the above inequality into the constraint
 * \f[
 *      \sum_{i \in I} \bar{y}_i \geq 1,
 * \f]
 * that is, it is a set covering constraint on the negated variables.
 *
 * The basic idea is to use the current solution to the LP relaxation and use it as the objective,
 * when optimizing of the alternative polyhedron. Since any vertex corresponds to such an
 * inequality, we can check whether it is violated. To enlarge the chance that we find a @em
 * violated inequality, we perform a fixing procedure, in which the variable corresponding to an
 * arbitrary element of the last IIS \f$I\f$ is fixed to zero, i.e., cannot be used in the next
 * IISs. This is repeated until the corresponding alternative polyhedron is infeasible, i.e., we
 * have obtained an IIS-cover. For more details see the paper above.
 *
 *
 * @subsection PREPROC Preprocessing
 *
 * Since each indicator constraint adds a linear constraint to the formulation, preprocessing of the
 * linear constraints change the above approach as follows.
 *
 * The system as present in the formulation is the following (ignoring variables that are not
 * contained in indicator constraints and the objective function):
 * \f[
 *    \begin{array}{ll}
 *      & A x - s \leq b, \\
 *      & l \leq x \leq u, \\
 *      & s \leq 0.
 *    \end{array}
 * \f]
 * Note again that the requirement \f$s \leq 0\f$ leads to an infeasible system. Consider now the
 * preprocessing of the linear constraint (aggregation, bound strengthening, etc.) and assume that
 * this changes the above system to the following:
 * \f[
 *    \begin{array}{ll}
 *      & \tilde{A} x - \tilde{B} s \leq \tilde{b}, \\
 *      & \tilde{l} \leq x \leq \tilde{u}, \\
 *      & s \leq 0. \\
 *    \end{array}
 * \f]
 * Note that we forbid multi-aggregation of the \f$s\f$ variables in order to be able to change their
 * bounds in propagation/branching. The corresponding alternative system is the following:
 * \f[
 *    \begin{array}{ll}
 *      & \tilde{A}^T w - r + t = 0,\\
 *      & - \tilde{B}^T w + v = 0,\\
 *      & b^T w - l^T r + u^T t = -1,\\
 *      & w, v, r, t \geq 0
 *    \end{array}
 *    \qquad \Leftrightarrow \qquad
 *    \begin{array}{ll}
 *      & \tilde{A}^T w - r + t = 0,\\
 *      & \tilde{B}^T w \geq 0,\\
 *      & b^T w - l^T r + u^T t = -1,\\
 *      & w, r, t \geq 0,
 *    \end{array}
 * \f]
 * where the second form arises by substituting \f$v \geq 0\f$. A closer look at this system reveals
 * that it is not larger than the original one:
 *
 * - (Multi-)Aggregation of variables \f$x\f$ will remove these variables from the formulation, such that
 *   the corresponding column of \f$\tilde{A}\f$ (row of \f$\tilde{A}^T\f$) will be zero.
 *
 * - The rows of \f$\tilde{B}^T\f$ are not unit vectors, i.e., do not correspond to redundant
 *   nonnegativity constraints, only if the corresponding slack variables appear in an aggregation.
 *
 * Taken together, these two observations yield the conclusion that the new system is roughly as
 * large as the original one.
 *
 * @note Because of possible (multi-)aggregation it might happen that the linear constraint
 * corresponding to an indicator constraint becomes redundant and is deleted. From this we cannot
 * conclude that the indicator constraint is redundant as well (i.e. always fulfilled), because the
 * corresponding slack variable is still present and its setting to 0 might influence other
 * (linear) constraints. Thus, we have to rely on the dual presolving of the linear constraints to
 * detect this case: If the linear constraint is really redundant, i.e., is always fulfilled, it is
 * deleted and the slack variable can is fixed to 0. In this case, the indicator constraint can be
 * deleted as well.
 *
 * @todo Accept arbitrary ranged linear constraints as input (in particular: equations). Internally
 * create two indicator constraints or correct alternative polyhedron accordingly (need to split the
 * variables there, but not in original problem).
 *
 * @todo Treat variable upper bounds in a special way: Do not create the artificial slack variable,
 * but directly enforce the propagations etc.
 *
 * @todo Turn off separation if the alternative polyhedron is infeasible and updateBounds is false.
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#include <assert.h>
#include <string.h>

#include "scip/cons_indicator.h"
#include "scip/cons_linear.h"
#include "scip/cons_logicor.h"
#include "scip/cons_varbound.h"
#include "scip/cons_quadratic.h"
#include "scip/heur_trysol.h"
#include "scip/pub_misc.h"


/* constraint handler properties */
#define CONSHDLR_NAME          "indicator"
#define CONSHDLR_DESC          "indicator constraint handler"
#define CONSHDLR_SEPAPRIORITY        10 /**< priority of the constraint handler for separation */
#define CONSHDLR_ENFOPRIORITY      -100 /**< priority of the constraint handler for constraint enforcing */
#define CONSHDLR_CHECKPRIORITY -1000000 /**< priority of the constraint handler for checking feasibility */
#define CONSHDLR_SEPAFREQ            10 /**< frequency for separating cuts; zero means to separate only in the root node */
#define CONSHDLR_PROPFREQ             1 /**< frequency for propagating domains; zero means only preprocessing propagation */
#define CONSHDLR_EAGERFREQ          100 /**< frequency for using all instead of only the useful constraints in separation,
                                         *   propagation and enforcement, -1 for no eager evaluations, 0 for first only */
#define CONSHDLR_MAXPREROUNDS        -1 /**< maximal number of presolving rounds the constraint handler participates in (-1: no limit) */
#define CONSHDLR_DELAYSEPA        FALSE /**< should separation method be delayed, if other separators found cuts? */
#define CONSHDLR_DELAYPROP        FALSE /**< should propagation method be delayed, if other propagators found reductions? */
#define CONSHDLR_DELAYPRESOL      FALSE /**< should presolving method be delayed, if other presolvers found reductions? */
#define CONSHDLR_NEEDSCONS         TRUE /**< should the constraint handler be skipped, if no constraints are available? */


/* event handler properties */
#define EVENTHDLR_NAME         "indicator"
#define EVENTHDLR_DESC         "bound change event handler for indicator constraints"


/* default values for parameters */
#define DEFAULT_BRANCHINDICATORS   FALSE
#define DEFAULT_GENLOGICOR         FALSE
#define DEFAULT_SEPAALTERNATIVELP  FALSE
#define DEFAULT_ADDCOUPLING        TRUE
#define DEFAULT_MAXCOUPLINGVALUE   1e4
#define DEFAULT_ADDCOUPLINGCONS    FALSE
#define DEFAULT_REMOVEINDICATORS   FALSE
#define DEFAULT_UPDATEBOUNDS       FALSE
#define DEFAULT_TRYSOLUTIONS       TRUE
#define DEFAULT_NOLINCONSCONT      FALSE
#define DEFAULT_ENFORCECUTS        FALSE
#define DEFAULT_MAXCONDITIONALTLP  0.0
#define DEFAULT_GENERATEBILINEAR   FALSE


/* other values */
#define OBJEPSILON                 0.001 /**< value to add to objective in alt. LP if the binary variable is 1 in order to get small IISs */


/** constraint data for indicator constraints */
struct SCIP_ConsData
{
   SCIP_VAR*             binvar;             /**< binary variable for indicator constraint */
   SCIP_VAR*             slackvar;           /**< slack variable of inequality of indicator constraint */
   SCIP_CONS*            lincons;            /**< linear constraint corresponding to indicator constraint */
   int                   nFixedNonzero;      /**< number of variables among binvar and slackvar fixed to be nonzero */
   int                   colIndex;           /**< column index in alternative LP */
   SCIP_Bool             linconsActive;      /**< whether linear constraint and slack variable are active */
};


/** indicator constraint handler data */
struct SCIP_ConshdlrData
{
   SCIP_EVENTHDLR*       eventhdlr;          /**< event handler for bound change events */
   SCIP_Bool             removable;          /**< whether the separated cuts should be removable */
   SCIP_Bool             scaled;             /**< if first row of alt. LP has been scaled */
   SCIP_LPI*             altLP;              /**< alternative LP for cut separation */
   int                   nRows;              /**< # rows in the alt. LP corr. to original variables in linear constraints and slacks */
   int                   nLbBounds;          /**< # lower bounds of original variables */
   int                   nUbBounds;          /**< # upper bounds of original variables */
   SCIP_HASHMAP*         varHash;            /**< hash map from variable to row index in alternative LP */
   SCIP_HASHMAP*         lbHash;             /**< hash map from variable to index of lower bound column in alternative LP */
   SCIP_HASHMAP*         ubHash;             /**< hash map from variable to index of upper bound column in alternative LP */
   SCIP_HASHMAP*         slackHash;          /**< hash map from slack variable to row index in alternative LP */
   int                   nSlackVars;         /**< # slack variables */
   int                   roundingRounds;     /**< number of rounds in separation */
   SCIP_Real             roundingMinThres;   /**< minimal value for rounding in separation */
   SCIP_Real             roundingMaxThres;   /**< maximal value for rounding in separation */
   SCIP_Real             roundingOffset;     /**< offset for rounding in separation */
   SCIP_Bool             branchIndicators;   /**< Branch on indicator constraints in enforcing? */
   SCIP_Bool             genLogicor;         /**< Generate logicor constraints instead of cuts? */
   SCIP_Bool             sepaAlternativeLP;  /**< Separate using the alternative LP? */
   SCIP_Bool             addCoupling;        /**< whether the coupling inequalities should be added */
   SCIP_Bool             addCouplingCons;    /**< whether the coupling inequalities should be added as variable bound constraints, if 'addCoupling' is true*/
   SCIP_Bool             removeIndicators;   /**< remove indicator constraint if corresponding variable bound constraint has been added? */
   SCIP_Bool             updateBounds;       /**< whether the bounds of the original variables should be changed for separation */
   SCIP_Bool             trySolutions;       /**< Try to make solutions feasible by setting indicator variables? */
   SCIP_Bool             noLinconsCont;      /**< decompose problem - do not generate linear constraint if all variables are continuous */
   SCIP_Bool             enforceCuts;        /**< in enforcing try to generate cuts (only if sepaAlternativeLP is true) */
   SCIP_Real             maxCouplingValue;   /**< maximum coefficient for binary variable in coupling constraint */
   SCIP_Real             maxConditionAltLP;  /**< maximum estimated condition of the LP solution of the alternative LP to trust its solution */
   SCIP_Bool             generateBilinear;   /**< do not generate indicator constraint, but a bilinear constraint instead */
   SCIP_HEUR*            heurTrySol;         /**< trysol heuristic */
   SCIP_Bool             addedCouplingCons;  /**< whether the coupling constraints have been added already */
   SCIP_CONS**           addLinCons;         /**< additional linear constraints that should be added to the alternative LP */
   int                   nAddLinCons;        /**< number of additional constraints */
   int                   maxAddLinCons;      /**< maximal number of additional constraints */
};


/* Macro for parameters */
#define SCIP_CALL_PARAM(x) do                                                                   \
{                                                                                               \
   SCIP_RETCODE _restat_;                                                                       \
   if ( (_restat_ = (x)) != SCIP_OKAY && (_restat_ != SCIP_PARAMETERUNKNOWN) )                  \
   {                                                                                            \
      SCIPerrorMessage("[%s:%d] Error <%d> in function call\n", __FILE__, __LINE__, _restat_);  \
      SCIPABORT();                                                                              \
   }                                                                                            \
}                                                                                               \
while( FALSE )


/* ------------------------ debugging routines ---------------------------------*/

#ifdef SCIP_ENABLE_IISCHECK
/** Check that indicator constraints corresponding to nonnegative entries in @a vector are infeasible in original problem
 *
 *  This function will probably fail if the has been presolved by the cons_linear presolver - to
 *  make it complete we would have to substitute active variables.
 */
static
SCIP_RETCODE checkIIS(
   SCIP*                 scip,               /**< SCIP pointer */
   int                   nconss,             /**< number of constraints */
   SCIP_CONS**           conss,              /**< indicator constraints */
   SCIP_Real*            vector              /**< vector */
   )
{
   SCIP_CONSHDLR* conshdlr;
   SCIP_HASHMAP* varHash;           /**< hash map from variable to column index in auxiliary LP */
   SCIP_LPI* lp;
   int nvars;
   int c;

   assert( scip != NULL );
   assert( vector != NULL );

   nvars = 0;

   SCIPdebugMessage("Checking IIS ...\n");

   /* now check indicator constraints */
   conshdlr = SCIPfindConshdlr(scip, "indicator");
   assert( conshdlr != NULL );

   conss = SCIPconshdlrGetConss(conshdlr);
   nconss = SCIPconshdlrGetNConss(conshdlr);

   /* create LP */
   SCIP_CALL( SCIPlpiCreate(&lp, "checkLP", SCIP_OBJSEN_MINIMIZE) );

   /* set up hash map */
   SCIP_CALL( SCIPhashmapCreate(&varHash, SCIPblkmem(scip), SCIPcalcHashtableSize(10 * SCIPgetNVars(scip))) );

   /* loop through indicator constraints */
   for (c = 0; c < nconss; ++c)
   {
      SCIP_CONSDATA* consdata;
      consdata = SCIPconsGetData(conss[c]);
      assert( consdata != NULL );

      /* check whether constraint should be included */
      if ( consdata->colIndex >= 0 && (! SCIPisFeasZero(scip, vector[consdata->colIndex]) || ! SCIPconsIsEnabled(conss[c])) )
      {
         SCIP_CONS* lincons;
         SCIP_VAR** linvars;
         SCIP_Real* linvals;
         SCIP_Real linrhs;
         SCIP_Real linlhs;
         SCIP_VAR* slackvar;
         int nlinvars;
         SCIP_Real sign;
         int matbeg;
         int* matind;
         SCIP_Real* matval;
         SCIP_VAR** newVars;
         int nNewVars;
         SCIP_Real lhs;
         SCIP_Real rhs;
         int cnt;
         int v;

         sign = 1.0;
         cnt = 0;

         lincons = consdata->lincons;
         assert( lincons != NULL );
         assert( ! SCIPconsIsEnabled(conss[c]) || SCIPconsIsActive(lincons) );
         assert( ! SCIPconsIsEnabled(conss[c]) || SCIPconsIsEnabled(lincons) );

         slackvar = consdata->slackvar;
         assert( slackvar != NULL );

         /* if the slack variable is aggregated (multi-aggregation should not happen) */
         assert( SCIPvarGetStatus(slackvar) != SCIP_VARSTATUS_MULTAGGR );
         if ( SCIPvarGetStatus(slackvar) == SCIP_VARSTATUS_AGGREGATED )
         {
            SCIP_VAR* var;
            SCIP_Real scalar;
            SCIP_Real constant;

            var = slackvar;
            scalar = 1.0;
            constant = 0.0;

            SCIP_CALL( SCIPvarGetProbvarSum(&var, &scalar, &constant) );
            assert( ! SCIPisZero(scip, scalar) );

            /*  SCIPdebugMessage("slack variable aggregated (scalar: %f, constant: %f)\n", scalar, constant); */

            /* otherwise construct a linear constraint */
            SCIP_CALL( SCIPallocBufferArray(scip, &linvars, 1) );
            SCIP_CALL( SCIPallocBufferArray(scip, &linvals, 1) );
            linvars[0] = var;
            linvals[0] = scalar;
            nlinvars = 1;
            linlhs = -SCIPinfinity(scip);
            linrhs = constant;
         }
         else
         {
            /* in this case, the linear constraint is directly usable */
            linvars = SCIPgetVarsLinear(scip, lincons);
            linvals = SCIPgetValsLinear(scip, lincons);
            nlinvars = SCIPgetNVarsLinear(scip, lincons);
            linlhs = SCIPgetLhsLinear(scip, lincons);
            linrhs = SCIPgetRhsLinear(scip, lincons);
         }

         /* adapt rhs of linear constraint */
         assert( SCIPisInfinity(scip, -linlhs) || SCIPisInfinity(scip, linrhs) );
         if ( SCIPisInfinity(scip, linrhs) )
         {
            linrhs = linlhs;
            assert( linrhs > -SCIPinfinity(scip) );
            sign = -1.0;
         }

         SCIP_CALL( SCIPallocBufferArray(scip, &matind, 4*nlinvars) );
         SCIP_CALL( SCIPallocBufferArray(scip, &matval, 4*nlinvars) );
         SCIP_CALL( SCIPallocBufferArray(scip, &newVars, nlinvars) );

         /* set up row */
         nNewVars = 0;
         for (v = 0; v < nlinvars; ++v)
         {
            SCIP_VAR* var;
            var = linvars[v];
            assert( var != NULL );

            /* skip slack variable */
            if ( var == slackvar )
               continue;

            /* if variable new */
            if ( ! SCIPhashmapExists(varHash, var) )
            {
               /* add variable in map */
               SCIP_CALL( SCIPhashmapInsert(varHash, var, (void*) (size_t) nvars) );
               assert( nvars == (int) (size_t) SCIPhashmapGetImage(varHash, var) );
               /* SCIPdebugMessage("Inserted variable <%s> into hashmap (%d).\n", SCIPvarGetName(var), nvars); */
               nvars++;

               /* store new variables */
               newVars[nNewVars++] = var;
            }
            assert( SCIPhashmapExists(varHash, var) );
         }

         /* add new columns */
         if ( nNewVars > 0 )
         {
            SCIP_Real* lb;
            SCIP_Real* ub;
            SCIP_Real* obj;
            char** colnames;

            SCIP_CALL( SCIPallocBufferArray(scip, &lb, nNewVars) );
            SCIP_CALL( SCIPallocBufferArray(scip, &ub, nNewVars) );
            SCIP_CALL( SCIPallocBufferArray(scip, &obj, nNewVars) );
            SCIP_CALL( SCIPallocBufferArray(scip, &colnames, nNewVars) );

            for (v = 0; v < nNewVars; ++v)
            {
               SCIP_VAR* var;
               var = newVars[v];
               obj[v] = 0.0;
               lb[v] = SCIPvarGetLbLocal(var);
               ub[v] = SCIPvarGetUbLocal(var);
               SCIP_CALL( SCIPallocBufferArray(scip, &(colnames[v]), SCIP_MAXSTRLEN) );
               (void) SCIPsnprintf(colnames[v], SCIP_MAXSTRLEN, "%s", SCIPvarGetName(var));
            }

            /* now add columns */
            SCIP_CALL( SCIPlpiAddCols(lp, nNewVars, obj, lb, ub, colnames, 0, NULL, NULL, NULL) );

            for (v = nNewVars - 1; v >= 0; --v)
            {
               SCIPfreeBufferArray(scip, &(colnames[v]));
            }
            SCIPfreeBufferArray(scip, &colnames);
            SCIPfreeBufferArray(scip, &obj);
            SCIPfreeBufferArray(scip, &ub);
            SCIPfreeBufferArray(scip, &lb);
         }

         /* set up row */
         cnt = 0;
         for (v = 0; v < nlinvars; ++v)
         {
            SCIP_VAR* var;
            var = linvars[v];
            assert( var != NULL );

            /* skip slack variable */
            if ( var == slackvar )
               continue;

            assert( SCIPhashmapExists(varHash, var) );
            matind[cnt] = (int) (size_t) SCIPhashmapGetImage(varHash, var);
            matval[cnt] = sign * linvals[v];
            ++cnt;
         }

         lhs = -SCIPlpiInfinity(lp);
         rhs = linrhs;

         /* add new row */
         matbeg = 0;
         SCIP_CALL( SCIPlpiAddRows(lp, 1, &lhs, &rhs, NULL, cnt, &matbeg, matind, matval) );

         SCIPfreeBufferArray(scip, &matind);
         SCIPfreeBufferArray(scip, &matval);
         SCIPfreeBufferArray(scip, &newVars);

         assert( slackvar != NULL );
         if ( SCIPvarGetStatus(slackvar) == SCIP_VARSTATUS_AGGREGATED )
         {
            SCIPfreeBufferArray(scip, &linvals);
            SCIPfreeBufferArray(scip, &linvars);
         }
      }
   }

   /* solve LP and check status */
   SCIP_CALL( SCIPlpiSolvePrimal(lp) );

   if ( ! SCIPlpiIsPrimalInfeasible(lp) )
   {
      SCIP_CONSHDLRDATA* conshdlrdata;

      SCIPerrorMessage("Detected IIS is not infeasible in original problem!\n");

      conshdlrdata = SCIPconshdlrGetData(conshdlr);
      assert( conshdlrdata != NULL );

      SCIP_CALL( SCIPlpiWriteLP(lp, "check.lp") );
      SCIP_CALL( SCIPlpiWriteLP(conshdlrdata->altLP, "altdebug.lp") );
      SCIPABORT();
   }
   SCIPdebugMessage("Check successful!\n");

   SCIPhashmapFree(&varHash);
   SCIP_CALL( SCIPlpiFree(&lp) );

   return SCIP_OKAY;
}
#endif


/* ------------------------ auxiliary operations -------------------------------*/

/** ensures that the addLinCons array can store at least num entries */
static
SCIP_RETCODE consdataEnsureAddLinConsSize(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONSHDLR*        conshdlr,           /**< constraint handler */
   int                   num                 /**< minimum number of entries to store */
   )
{
   SCIP_CONSHDLRDATA* conshdlrdata;

   assert( scip != NULL );
   assert( conshdlr != NULL );
   assert( strcmp(SCIPconshdlrGetName(conshdlr), CONSHDLR_NAME) == 0 );

   conshdlrdata = SCIPconshdlrGetData(conshdlr);
   assert( conshdlrdata != NULL );
   assert( conshdlrdata->nAddLinCons <= conshdlrdata->maxAddLinCons );

   if ( num > conshdlrdata->maxAddLinCons )
   {
      int newsize;

      newsize = SCIPcalcMemGrowSize(scip, num);
      SCIP_CALL( SCIPreallocBlockMemoryArray(scip, &conshdlrdata->addLinCons, conshdlrdata->maxAddLinCons, newsize) );
      conshdlrdata->maxAddLinCons = newsize;
   }
   assert( num <= conshdlrdata->maxAddLinCons );

   return SCIP_OKAY;
}



/* ------------------------ operations on the alternative LP -------------------*/

/** initialize alternative LP
 *
 *  The alternative system is organized as follows:
 *  - The first row corresponds to the right hand side of the original system.
 *  - The next nconss constraints correspond to the slack variables.
 *  - The rows after that correspond to the original variables.
 */
static
SCIP_RETCODE initAlternativeLP(
   SCIP*                 scip,               /**< SCIP pointer */
   SCIP_CONSHDLR*        conshdlr            /**< constraint handler */
   )
{
   SCIP_CONSHDLRDATA* conshdlrdata;
   SCIP_Real lhs;
   SCIP_Real rhs;

   assert( scip != NULL );
   assert( strcmp(SCIPconshdlrGetName(conshdlr), CONSHDLR_NAME) == 0 );

   conshdlrdata = SCIPconshdlrGetData(conshdlr);
   assert( conshdlrdata != NULL );
   assert( conshdlrdata->altLP == NULL );
   assert( conshdlrdata->varHash == NULL );
   assert( conshdlrdata->lbHash == NULL );
   assert( conshdlrdata->ubHash == NULL );
   assert( conshdlrdata->slackHash != NULL );

   SCIPdebugMessage("Initializing alternative LP ...\n");

   /* create hash map of variables */
   SCIP_CALL( SCIPhashmapCreate(&conshdlrdata->varHash, SCIPblkmem(scip), SCIPcalcHashtableSize(10 * SCIPgetNVars(scip))) );
   SCIP_CALL( SCIPhashmapCreate(&conshdlrdata->lbHash, SCIPblkmem(scip), SCIPcalcHashtableSize(10 * SCIPgetNVars(scip))) );
   SCIP_CALL( SCIPhashmapCreate(&conshdlrdata->ubHash, SCIPblkmem(scip), SCIPcalcHashtableSize(10 * SCIPgetNVars(scip))) );

   /* create alternative LP */
   SCIP_CALL( SCIPlpiCreate(&conshdlrdata->altLP, "altLP", SCIP_OBJSEN_MINIMIZE) );

   /* add first row */
   lhs = -1.0;
   rhs = -1.0;
   SCIP_CALL( SCIPlpiAddRows(conshdlrdata->altLP, 1, &lhs, &rhs, NULL, 0, NULL, NULL, NULL) );
   conshdlrdata->nRows = 1;

   /* set parameters */
   SCIP_CALL_PARAM( SCIPlpiSetIntpar(conshdlrdata->altLP, SCIP_LPPAR_FROMSCRATCH, FALSE) );
   SCIP_CALL_PARAM( SCIPlpiSetIntpar(conshdlrdata->altLP, SCIP_LPPAR_PRESOLVING, TRUE) );
   SCIP_CALL_PARAM( SCIPlpiSetIntpar(conshdlrdata->altLP, SCIP_LPPAR_SCALING, TRUE) );
   SCIP_CALL_PARAM( SCIPlpiSetIntpar(conshdlrdata->altLP, SCIP_LPPAR_FASTMIP, FALSE) );

   /* set constraint handler data */
   SCIPconshdlrSetData(conshdlr, conshdlrdata);

   /* uncomment the following for debugging */
   /* SCIP_CALL_PARAM( SCIPlpiSetIntpar(conshdlrdata->altLP, SCIP_LPPAR_LPINFO, TRUE) ); */

   return SCIP_OKAY;
}


/** Check whether the bounds int given (alternative) LP are set correctly (for debugging) */
#ifndef NDEBUG
static
SCIP_RETCODE checkLPBoundsClean(
   SCIP*                 scip,               /**< SCIP pointer */
   SCIP_LPI*             lp,                 /**< LP for which bounds should be checked */
   int                   nconss,             /**< number of constraints */
   SCIP_CONS**           conss               /**< constraints */
   )
{
   SCIP_Real* lb;
   SCIP_Real* ub;
   SCIP_Bool* covered;
   int nCols;
   int j;

   assert( scip != NULL );
   assert( lp != NULL );

   SCIP_CALL( SCIPlpiGetNCols(lp, &nCols) );

   SCIP_CALL( SCIPallocBufferArray(scip, &lb, nCols) );
   SCIP_CALL( SCIPallocBufferArray(scip, &ub, nCols) );
   SCIP_CALL( SCIPallocBufferArray(scip, &covered, nCols) );

   for (j = 0; j < nCols; ++j)
      covered[j] = FALSE;

   /* check columns used by contraints */
   SCIP_CALL( SCIPlpiGetBounds(lp, 0, nCols-1, lb, ub) );
   for (j = 0; j < nconss; ++j)
   {
      SCIP_CONSDATA* consdata;
      int ind;

      assert( conss[j] != NULL );
      consdata = SCIPconsGetData(conss[j]);
      assert( consdata != NULL );
      ind = consdata->colIndex;

      if ( ind >= 0 )
      {
         assert( ind < nCols );
         covered[ind] = TRUE;
         if ( ! SCIPisFeasZero(scip, lb[ind]) || ! SCIPlpiIsInfinity(lp, ub[ind]) )
         {
            SCIPABORT();
         }
      }
   }

   /* check other columns */
   for (j = 0; j < nCols; ++j)
   {
      if (! covered[j] )
      {
         /* some columns can be fixed to 0, since they correspond to disabled constraints */
         if ( ( ! SCIPlpiIsInfinity(lp, -lb[j]) && ! SCIPisFeasZero(scip, lb[j])) || (! SCIPlpiIsInfinity(lp, ub[j]) && ! SCIPisFeasZero(scip, ub[j])) )
         {
            SCIPABORT();
         }
      }
   }

   SCIPfreeBufferArray(scip, &covered);
   SCIPfreeBufferArray(scip, &lb);
   SCIPfreeBufferArray(scip, &ub);

   return SCIP_OKAY;
}
#endif


/** Set the alternative system objective function
 *
 *  We assume that the objective function coefficients of the variables other than the binary
 *  indicators are always 0 and hence do not have to be changed.
 *
 *  We already use the tranformation \f$y' = 1 - y\f$.
 */
static
SCIP_RETCODE setAltLPObj(
   SCIP*                 scip,               /**< SCIP pointer */
   SCIP_LPI*             lp,                 /**< alternative LP */
   SCIP_SOL*             sol,                /**< solution to be dealt with */
   int                   nconss,             /**< number of constraints */
   SCIP_CONS**           conss               /**< indicator constraints */
   )
{
   int j;
   SCIP_Real* obj;
   int* indices;
   int cnt = 0;

   assert( scip != NULL );
   assert( lp != NULL );
   assert( conss != NULL );

   SCIP_CALL( SCIPallocBufferArray(scip, &obj, nconss) );
   SCIP_CALL( SCIPallocBufferArray(scip, &indices, nconss) );

   for (j = 0; j < nconss; ++j)
   {
      SCIP_CONSDATA* consdata;

      assert( conss[j] != NULL );
      consdata = SCIPconsGetData(conss[j]);
      assert( consdata != NULL );

      if ( consdata->colIndex >= 0 )
      {
         SCIP_Real val = SCIPgetSolVal(scip, sol, consdata->binvar);
         if ( SCIPisFeasEQ(scip, val, 1.0) )
            obj[cnt] = OBJEPSILON;      /* set objective to some small number to get small IISs */
         else
            obj[cnt] = 1.0 - val;
         indices[cnt++] = consdata->colIndex;
      }
   }

   SCIP_CALL( SCIPlpiChgObj(lp, cnt, indices, obj) );

   SCIPfreeBufferArray(scip, &indices);
   SCIPfreeBufferArray(scip, &obj);

   return SCIP_OKAY;
}


/** Set the alternative system objective function to some small value */
static
SCIP_RETCODE setAltLPObjZero(
   SCIP*                 scip,               /**< SCIP pointer */
   SCIP_LPI*             lp,                 /**< alternative LP */
   int                   nconss,             /**< number of constraints */
   SCIP_CONS**           conss               /**< indicator constraints */
   )
{
   int j;
   SCIP_Real* obj;
   int* indices;
   int cnt = 0;

   assert( scip != NULL );
   assert( lp != NULL );
   assert( conss != NULL );

   SCIP_CALL( SCIPallocBufferArray(scip, &obj, nconss) );
   SCIP_CALL( SCIPallocBufferArray(scip, &indices, nconss) );

   for (j = 0; j < nconss; ++j)
   {
      SCIP_CONSDATA* consdata;

      assert( conss[j] != NULL );
      consdata = SCIPconsGetData(conss[j]);
      assert( consdata != NULL );

      if ( consdata->colIndex >= 0 )
      {
         obj[cnt] = OBJEPSILON;
         indices[cnt++] = consdata->colIndex;
      }
   }

   SCIP_CALL( SCIPlpiChgObj(lp, cnt, indices, obj) );

   SCIPfreeBufferArray(scip, &indices);
   SCIPfreeBufferArray(scip, &obj);

   return SCIP_OKAY;
}


/** Fix variable given by @a S to 0 */
static
SCIP_RETCODE fixAltLPVariables(
   SCIP*                 scip,               /**< SCIP pointer */
   SCIP_LPI*             lp,                 /**< alternative LP */
   int                   nconss,             /**< number of constraints */
   SCIP_CONS**           conss,              /**< indicator constraints */
   SCIP_Bool*            S                   /**< bitset of variables */
   )
{
   SCIP_Real* lb;
   SCIP_Real* ub;
   int* indices;
   int cnt;
   int j;

   assert( scip != NULL );
   assert( lp != NULL );
   assert( conss != NULL );

   cnt = 0;

   SCIP_CALL( SCIPallocBufferArray(scip, &lb, nconss) );
   SCIP_CALL( SCIPallocBufferArray(scip, &ub, nconss) );
   SCIP_CALL( SCIPallocBufferArray(scip, &indices, nconss) );

   /* collect bounds to be changed */
   for (j = 0; j < nconss; ++j)
   {
      SCIP_CONSDATA* consdata;

      assert( conss[j] != NULL );
      consdata = SCIPconsGetData(conss[j]);
      assert( consdata != NULL );

      if ( consdata->colIndex >= 0 )
      {
         if ( S[j] )
         {
            indices[cnt] = consdata->colIndex;
            lb[cnt] = 0.0;
            ub[cnt] = 0.0;
            ++cnt;
         }
      }
   }
   /* change bounds */
   SCIP_CALL( SCIPlpiChgBounds(lp, cnt, indices, lb, ub) );

   SCIPfreeBufferArray(scip, &indices);
   SCIPfreeBufferArray(scip, &ub);
   SCIPfreeBufferArray(scip, &lb);

   return SCIP_OKAY;
}


/** Fix variable @a ind to 0 */
static
SCIP_RETCODE fixAltLPVariable(
   SCIP_LPI*             lp,                 /**< alternative LP */
   int                   ind                 /**< variable that should be fixed to 0 */
   )
{
   SCIP_Real lb;
   SCIP_Real ub;

   lb = 0.0;
   ub = 0.0;

   /* change bounds */
   SCIP_CALL( SCIPlpiChgBounds(lp, 1, &ind, &lb, &ub) );

   return SCIP_OKAY;
}


/** unfix variable @a ind to 0 */
#if 1
static
SCIP_RETCODE unfixAltLPVariable(
   SCIP_LPI*             lp,                 /**< alternative LP */
   int                   ind                 /**< variable that should be fixed to 0 */
   )
{
   SCIP_Real lb = 0.0;
   SCIP_Real ub = SCIPlpiInfinity(lp);

   /* change bounds */
   SCIP_CALL( SCIPlpiChgBounds(lp, 1, &ind, &lb, &ub) );

   return SCIP_OKAY;
}
#endif


/** unfix variable given by @a S to 0 */
static
SCIP_RETCODE unfixAltLPVariables(
   SCIP*                 scip,               /**< SCIP pointer */
   SCIP_LPI*             lp,                 /**< alternative LP */
   int                   nconss,             /**< number of constraints */
   SCIP_CONS**           conss,              /**< indicator constraints */
   SCIP_Bool*            S                   /**< bitset of variables */
   )
{
   SCIP_Real* lb;
   SCIP_Real* ub;
   int* indices;
   int cnt = 0;
   int j;

   assert( scip != NULL );
   assert( lp != NULL );
   assert( conss != NULL );

   SCIP_CALL( SCIPallocBufferArray(scip, &lb, nconss) );
   SCIP_CALL( SCIPallocBufferArray(scip, &ub, nconss) );
   SCIP_CALL( SCIPallocBufferArray(scip, &indices, nconss) );

   /* collect bounds to be changed */
   for (j = 0; j < nconss; ++j)
   {
      if ( S[j] )
      {
         SCIP_CONSDATA* consdata;

         assert( conss[j] != NULL );
         consdata = SCIPconsGetData(conss[j]);
         assert( consdata != NULL );

         if ( consdata->colIndex >= 0 )
         {
            indices[cnt] = consdata->colIndex;
            lb[cnt] = 0.0;
            ub[cnt] = SCIPlpiInfinity(lp);
            ++cnt;
         }
      }
   }
   /* change bounds */
   SCIP_CALL( SCIPlpiChgBounds(lp, cnt, indices, lb, ub) );

   SCIPfreeBufferArray(scip, &indices);
   SCIPfreeBufferArray(scip, &ub);
   SCIPfreeBufferArray(scip, &lb);

   return SCIP_OKAY;
}


/** update bounds in first row to the current ones */
static
SCIP_RETCODE updateFirstRow(
   SCIP*                 scip,               /**< SCIP pointer */
   SCIP_CONSHDLRDATA*    conshdlrdata        /**< constraint handler */
   )
{
   SCIP_HASHMAP* lbHash;
   SCIP_HASHMAP* ubHash;
   SCIP_VAR** vars;
   SCIP_LPI* altLP;
   int nvars;
   int cnt;
   int v;

   assert( scip != NULL );
   assert( conshdlrdata != NULL );

   altLP = conshdlrdata->altLP;
   lbHash = conshdlrdata->lbHash;
   ubHash = conshdlrdata->ubHash;
   assert( lbHash != NULL && ubHash != NULL );

   /* check all variables */
   vars = SCIPgetVars(scip);
   nvars = SCIPgetNVars(scip);
   cnt = 0;

   for (v = 0; v < nvars; ++v)
   {
      SCIP_VAR* var;
      var = vars[v];
      if ( SCIPhashmapExists(lbHash, var) )
      {
         int col;
         col = (int) (size_t) SCIPhashmapGetImage(lbHash, var);
         SCIP_CALL( SCIPlpiChgCoef(altLP, 0, col, -SCIPvarGetLbLocal(var)) );
         if ( ! SCIPisEQ(scip, SCIPvarGetLbLocal(var), SCIPvarGetLbGlobal(var)) )
            ++cnt;
      }
      if ( SCIPhashmapExists(ubHash, var) )
      {
         int col = (int) (size_t) SCIPhashmapGetImage(ubHash, var);
         SCIP_CALL( SCIPlpiChgCoef(altLP, 0, col, SCIPvarGetUbLocal(var)) );
         if ( ! SCIPisEQ(scip, SCIPvarGetUbLocal(var), SCIPvarGetUbGlobal(var)) )
            ++cnt;
      }
   }
   if ( cnt > 10 )
   {
      /* possible force a rescaling: */
      conshdlrdata->scaled = FALSE;

      /* SCIP_CALL( SCIPlpiWriteLP(altLP, "altChg.lp") ); */
      SCIPdebugMessage("Updated bounds of original variables: %d\n", cnt);
   }

   return SCIP_OKAY;
}


/** update bounds in first row to the global bounds */
static
SCIP_RETCODE updateFirstRowGlobal(
   SCIP*                 scip,               /**< SCIP pointer */
   SCIP_CONSHDLRDATA*    conshdlrdata        /**< constraint handler */
   )
{

   SCIP_HASHMAP* lbHash;
   SCIP_HASHMAP* ubHash;
   SCIP_VAR** vars;
   SCIP_LPI* altLP;
   int nvars;
   int cnt;
   int v;

   assert( scip != NULL );
   assert( conshdlrdata != NULL );

   altLP = conshdlrdata->altLP;
   lbHash = conshdlrdata->lbHash;
   ubHash = conshdlrdata->ubHash;
   assert( lbHash != NULL && ubHash != NULL );

   /* check all variables */
   vars = SCIPgetVars(scip);
   nvars = SCIPgetNVars(scip);
   cnt = 0;

   for (v = 0; v < nvars; ++v)
   {
      SCIP_VAR* var;
      var = vars[v];
      if ( SCIPhashmapExists(lbHash, var) )
      {
         int col;
         col = (int) (size_t) SCIPhashmapGetImage(lbHash, var);
         SCIP_CALL( SCIPlpiChgCoef(altLP, 0, col, -SCIPvarGetLbGlobal(var)) );
         ++cnt;
      }
      if ( SCIPhashmapExists(ubHash, var) )
      {
         int col = (int) (size_t) SCIPhashmapGetImage(ubHash, var);
         SCIP_CALL( SCIPlpiChgCoef(altLP, 0, col, SCIPvarGetUbGlobal(var)) );
         ++cnt;
      }
   }
   if ( cnt > 0 )
   {
      /* SCIP_CALL( SCIPlpiWriteLP(altLP, "altChg.lp") ); */
      SCIPdebugMessage("Updated bounds of original variables: %d\n", cnt);
   }

   /* possible force a rescaling: */
   /* conshdlrdata->scaled = FALSE; */

   return SCIP_OKAY;
}


/** Check whether IIS defined by @a vector corresponds to a local cut */
static
SCIP_RETCODE checkIISlocal(
   SCIP*                 scip,               /**< SCIP pointer */
   SCIP_CONSHDLRDATA*    conshdlrdata,       /**< constraint handler */
   SCIP_Real*            vector,             /**< solution to alternative LP defining IIS */
   SCIP_Bool*            isLocal             /**< whether the IIS uses local bounds different from the global ones */
   )
{
   SCIP_HASHMAP* lbHash;
   SCIP_HASHMAP* ubHash;
   SCIP_VAR** vars;
#ifndef NDEBUG
   int nCols;
#endif
   int nvars;
   int v;

   assert( scip != NULL );
   assert( conshdlrdata != NULL );
   assert( vector != NULL );
   assert( isLocal != NULL );

   *isLocal = FALSE;

#ifndef NDEBUG
   SCIP_CALL( SCIPlpiGetNCols(conshdlrdata->altLP, &nCols) );
#endif

   lbHash = conshdlrdata->lbHash;
   ubHash = conshdlrdata->ubHash;
   assert( lbHash != NULL && ubHash != NULL );

   /* get all variables */
   vars = SCIPgetVars(scip);
   nvars = SCIPgetNVars(scip);

   /* check all variables */
   for (v = 0; v < nvars; ++v)
   {
      SCIP_VAR* var;
      var = vars[v];

      /* if local bound is different from global bound */
      if ( ! SCIPisEQ(scip, SCIPvarGetLbLocal(var), SCIPvarGetLbGlobal(var)) )
      {
         /* check whether the variable corresponding to the lower bounds has been used */
         if ( SCIPhashmapExists(lbHash, var) )
         {
            int col;

            col = (int) (size_t) SCIPhashmapGetImage(lbHash, var);
            assert( 0 <= col && col < nCols );
            if ( ! SCIPisFeasZero(scip, vector[col]) )
            {
               *isLocal = FALSE;
               return SCIP_OKAY;
            }
         }
      }

      /* if local bound is different from global bound */
      if ( ! SCIPisEQ(scip, SCIPvarGetUbLocal(var), SCIPvarGetUbGlobal(var)) )
      {
         /* check whether the variable corresponding to the upper bounds has been used */
         if ( SCIPhashmapExists(ubHash, var) )
         {
            int col;

            col = (int) (size_t) SCIPhashmapGetImage(ubHash, var);
            assert( 0 <= col && col < nCols );
            if ( ! SCIPisFeasZero(scip, vector[col]) )
            {
               *isLocal = FALSE;
               return SCIP_OKAY;
            }
         }
      }
   }

   return SCIP_OKAY;
}


/** compute scaling for first row
 *
 *  If the coefficients in the first row are large, a right hand side of -1 might not be
 *  adequate. Here, we replace the right hand side by the sum of the coefficients divided by the
 *  number of nonzeros.
 */
static
SCIP_RETCODE scaleFirstRow(
   SCIP*                 scip,               /**< SCIP pointer */
   SCIP_CONSHDLRDATA*    conshdlrdata        /**< constraint handler */
   )
{
   SCIP_LPI* altLP;
   SCIP_Real* val;
   SCIP_Real sum = 0.0;
   int j;
   int nCols;
   int cnt;
   int beg;
   int* ind;

   assert( scip != NULL );
   assert( conshdlrdata != NULL );

   if ( ! conshdlrdata->scaled )
   {
      altLP = conshdlrdata->altLP;
      SCIP_CALL( SCIPlpiGetNCols(altLP, &nCols) );
      SCIP_CALL( SCIPallocBufferArray(scip, &ind, nCols) );
      SCIP_CALL( SCIPallocBufferArray(scip, &val, nCols) );

      SCIP_CALL( SCIPlpiGetRows(altLP, 0, 0, NULL, NULL, &cnt, &beg, ind, val) );

      /* compute sum */
      for (j = 0; j < cnt; ++j)
         sum += REALABS(val[j]);

      /* set rhs */
      sum = - REALABS(sum) / ((double) cnt);
      j = 0;
      SCIP_CALL( SCIPlpiChgSides(altLP, 1, &j, &sum, &sum) );

      SCIPfreeBufferArray(scip, &val);
      SCIPfreeBufferArray(scip, &ind);

      conshdlrdata->scaled = TRUE;
   }

   return SCIP_OKAY;
}


/** add column corresponding to constraint to alternative LP
 *
 *  See the description at the top of the file for more information.
 */
static
SCIP_RETCODE addAltLPConstraint(
   SCIP*                 scip,               /**< SCIP pointer */
   SCIP_CONSHDLR*        conshdlr,           /**< constraint handler */
   SCIP_CONS*            lincons,            /**< linear constraint */
   SCIP_VAR*             slackvar,           /**< slack variable or NULL */
   SCIP_Real             objcoef,            /**< objective coefficient */
   int*                  colIndex            /**< index of new column */
   )
{
   SCIP_CONSHDLRDATA* conshdlrdata;
   SCIP_VAR** newVars;
   SCIP_VAR** linvars;
   SCIP_Real* linvals;
   SCIP_Real linrhs;
   SCIP_Real linlhs;
   SCIP_Real val;
   SCIP_Real sign;
   int* matbeg;
   int* matind;
   SCIP_Real* matval;
   SCIP_Bool* newRowsSlack;
   SCIP_Real* obj;
   SCIP_Real* lb;
   SCIP_Real* ub;
   int nlinvars;
   int nNewVars;
   int nNewCols;
   int nNewRows;
   int nCols;
   int cnt;
   int v;

   assert( scip != NULL );
   assert( conshdlr != NULL );
   assert( lincons != NULL );
   assert( colIndex != NULL );

   assert( strcmp(SCIPconshdlrGetName(conshdlr), CONSHDLR_NAME) == 0 );

   *colIndex = -1;
   sign = 1.0;
   nNewVars = 0;
   nNewRows = 0;
   cnt = 0;

   conshdlrdata = SCIPconshdlrGetData(conshdlr);
   assert( conshdlrdata != NULL );

   /* if the slack variable is aggregated (multi-aggregation should not happen) */
   assert( slackvar == NULL || SCIPvarGetStatus(slackvar) != SCIP_VARSTATUS_MULTAGGR );
   if ( slackvar != NULL && SCIPvarGetStatus(slackvar) == SCIP_VARSTATUS_AGGREGATED )
   {
      SCIP_VAR* var;
      SCIP_Real scalar;
      SCIP_Real constant;

      var = slackvar;
      scalar = 1.0;
      constant = 0.0;

      SCIP_CALL( SCIPvarGetProbvarSum(&var, &scalar, &constant) );

      SCIPdebugMessage("slack variable aggregated (scalar: %f, constant: %f)\n", scalar, constant);

      /* if the slack variable is fixed */
      if ( SCIPisZero(scip, scalar) && ! SCIPconsIsActive(lincons) )
         return SCIP_OKAY;

      /* otherwise construct a linear constraint */
      SCIP_CALL( SCIPallocBufferArray(scip, &linvars, 1) );
      SCIP_CALL( SCIPallocBufferArray(scip, &linvals, 1) );
      linvars[0] = var;
      linvals[0] = scalar;
      nlinvars = 1;
      linlhs = -SCIPinfinity(scip);
      linrhs = constant;
   }
   else
   {
      /* exit if linear constraint is not active */
      if ( ! SCIPconsIsActive(lincons) && slackvar != NULL )
         return SCIP_OKAY;

      /* in this case, the linear constraint is directly usable */
      linvars = SCIPgetVarsLinear(scip, lincons);
      linvals = SCIPgetValsLinear(scip, lincons);
      nlinvars = SCIPgetNVarsLinear(scip, lincons);
      linlhs = SCIPgetLhsLinear(scip, lincons);
      linrhs = SCIPgetRhsLinear(scip, lincons);
   }

   if ( conshdlrdata->altLP == NULL )
   {
      SCIP_CALL( initAlternativeLP(scip, conshdlr) );
   }
   assert( conshdlrdata->varHash != NULL );
   assert( conshdlrdata->lbHash != NULL );
   assert( conshdlrdata->ubHash != NULL );
   assert( conshdlrdata->slackHash != NULL );

#ifndef NDEBUG
   {
      int nRows;
      SCIP_CALL( SCIPlpiGetNRows(conshdlrdata->altLP, &nRows) );
      assert( nRows == conshdlrdata->nRows );
   }
#endif

   SCIP_CALL( SCIPallocBufferArray(scip, &matbeg, nlinvars) );
   SCIP_CALL( SCIPallocBufferArray(scip, &matind, 4*nlinvars) );
   SCIP_CALL( SCIPallocBufferArray(scip, &matval, 4*nlinvars) );
   SCIP_CALL( SCIPallocBufferArray(scip, &obj, 2*nlinvars) );
   SCIP_CALL( SCIPallocBufferArray(scip, &lb, 2*nlinvars) );
   SCIP_CALL( SCIPallocBufferArray(scip, &ub, 2*nlinvars) );
   SCIP_CALL( SCIPallocBufferArray(scip, &newVars, nlinvars) );
   SCIP_CALL( SCIPallocBufferArray(scip, &newRowsSlack, 2 * nlinvars) );

   /* store index of column in constraint */
   SCIP_CALL( SCIPlpiGetNCols(conshdlrdata->altLP, &nCols) );
   *colIndex = nCols;

   /* adapt rhs of linear constraint */
   val = linrhs;
   if ( SCIPisInfinity(scip, val) )
   {
      val = linlhs;
      assert( val > -SCIPinfinity(scip) );
      sign = -1.0;
   }

   /* handle first row */
   if (! SCIPisFeasZero(scip, val) )
   {
      matind[cnt] = 0;
      matval[cnt] = sign * val;
      assert( ! SCIPisInfinity(scip, val) && ! SCIPisInfinity(scip, -val) );
      ++cnt;
   }

   /* set up column (recognize new original variables) */
   for (v = 0; v < nlinvars; ++v)
   {
      SCIP_VAR* var;
      var = linvars[v];
      assert( var != NULL );

      /* if variable is a slack variable */
      if ( SCIPhashmapExists(conshdlrdata->slackHash, var) )
      {
         /* to avoid trivial rows: only add row corresponding to slack variable if it appears outside its own constraint */
         if ( var != slackvar )
         {
            int ind = (int) (size_t) SCIPhashmapGetImage(conshdlrdata->slackHash, var);
            if ( ind < INT_MAX )
               matind[cnt] = ind;
            else
            {
               /* add variable in map and array and remember to add a new row */
               SCIP_CALL( SCIPhashmapInsert(conshdlrdata->slackHash, var, (void*) (size_t) conshdlrdata->nRows) );
               assert( conshdlrdata->nRows == (int) (size_t) SCIPhashmapGetImage(conshdlrdata->slackHash, var) );
               SCIPdebugMessage("Inserted slack variable <%s> into hashmap (row: %d).\n", SCIPvarGetName(var), conshdlrdata->nRows);
               matind[cnt] = (conshdlrdata->nRows)++;

               /* store new variables */
               newRowsSlack[nNewRows++] = TRUE;
            }
            assert( conshdlrdata->nRows >= (int) (size_t) SCIPhashmapGetImage(conshdlrdata->slackHash, var) );
            matval[cnt] = sign * linvals[v];
            ++cnt;
         }
      }
      else
      {
         /* if variable exists */
         if ( SCIPhashmapExists(conshdlrdata->varHash, var) )
            matind[cnt] = (int) (size_t) SCIPhashmapGetImage(conshdlrdata->varHash, var);
         else
         {
            /* add variable in map and array and remember to add a new row */
            SCIP_CALL( SCIPhashmapInsert(conshdlrdata->varHash, var, (void*) (size_t) conshdlrdata->nRows) );
            assert( conshdlrdata->nRows == (int) (size_t) SCIPhashmapGetImage(conshdlrdata->varHash, var) );
            SCIPdebugMessage("Inserted variable <%s> into hashmap (row: %d).\n", SCIPvarGetName(var), conshdlrdata->nRows);
            matind[cnt] = (conshdlrdata->nRows)++;

            /* store new variables */
            newRowsSlack[nNewRows++] = FALSE;
            newVars[nNewVars++] = var;
         }
         assert( SCIPhashmapExists(conshdlrdata->varHash, var) );
         matval[cnt] = sign * linvals[v];
         ++cnt;
      }
   }

   /* add new rows */
   if ( nNewRows > 0 )
   {
      SCIP_Real* lhs;
      SCIP_Real* rhs;
      int i;

      SCIP_CALL( SCIPallocBufferArray(scip, &lhs, nNewRows) );
      SCIP_CALL( SCIPallocBufferArray(scip, &rhs, nNewRows) );
      for (i = 0; i < nNewRows; ++i)
      {
         if ( newRowsSlack[i] )
            lhs[i] = -SCIPlpiInfinity(conshdlrdata->altLP);
         else
            lhs[i] = 0.0;
         rhs[i] = 0.0;
      }
      /* add new rows */
      SCIP_CALL( SCIPlpiAddRows(conshdlrdata->altLP, nNewRows, lhs, rhs, NULL, 0, NULL, NULL, NULL) );

      SCIPfreeBufferArray(scip, &lhs);
      SCIPfreeBufferArray(scip, &rhs);
   }

   /* now add column */
   obj[0] = objcoef;
   lb[0] = 0.0;
   ub[0] = SCIPlpiInfinity(conshdlrdata->altLP);
   matbeg[0] = 0;

   /* create a free variable for equations -> should only happen for additional linear constraints */
   if ( SCIPisEQ(scip, linlhs, linrhs) )
   {
      assert( slackvar == NULL );
      lb[0] = -SCIPlpiInfinity(conshdlrdata->altLP);
   }

   SCIP_CALL( SCIPlpiAddCols(conshdlrdata->altLP, 1, obj, lb, ub, NULL, cnt, matbeg, matind, matval) );

   /* add columns corresponding to bounds of original variables - no bounds needed for slack vars */
   cnt = 0;
   nNewCols = 0;
   for (v = 0; v < nNewVars; ++v)
   {
      SCIP_VAR* var = newVars[v];

      /* if the lower bound is finite */
      val  = SCIPvarGetLbGlobal(var);
      if ( ! SCIPisInfinity(scip, -val) )
      {
         matbeg[nNewCols] = cnt;
         if ( ! SCIPisZero(scip, val) )
         {
            matind[cnt] = 0;
            matval[cnt] = -val;
            ++cnt;
         }
         assert( SCIPhashmapExists(conshdlrdata->varHash, var) );
         matind[cnt] = (int) (size_t) SCIPhashmapGetImage(conshdlrdata->varHash, var);
         matval[cnt] = -1.0;
         ++cnt;
         obj[nNewCols] = 0.0;
         lb[nNewCols] = 0.0;
         ub[nNewCols] = SCIPlpiInfinity(conshdlrdata->altLP);
         ++conshdlrdata->nLbBounds;
         SCIP_CALL( SCIPhashmapInsert(conshdlrdata->lbHash, var, (void*) (size_t) (nCols + 1 + nNewCols)) );
         assert( SCIPhashmapExists(conshdlrdata->lbHash, var) );
         SCIPdebugMessage("added column corr. to lower bound (%f) of variable <%s> to alternative polyhedron (col: %d).\n",
            val, SCIPvarGetName(var), nCols + 1 + nNewCols);
         ++nNewCols;
      }

      /* if the upper bound is finite */
      val = SCIPvarGetUbGlobal(var);
      if ( ! SCIPisInfinity(scip, val) )
      {
         matbeg[nNewCols] = cnt;
         if ( ! SCIPisZero(scip, val) )
         {
            matind[cnt] = 0;
            matval[cnt] = val;
            ++cnt;
         }
         assert( SCIPhashmapExists(conshdlrdata->varHash, var) );
         matind[cnt] = (int) (size_t) SCIPhashmapGetImage(conshdlrdata->varHash, var);
         matval[cnt] = 1.0;
         ++cnt;
         obj[nNewCols] = 0.0;
         lb[nNewCols] = 0.0;
         ub[nNewCols] = SCIPlpiInfinity(conshdlrdata->altLP);
         ++conshdlrdata->nUbBounds;
         SCIP_CALL( SCIPhashmapInsert(conshdlrdata->ubHash, var, (void*) (size_t) (nCols + 1 + nNewCols)) );
         assert( SCIPhashmapExists(conshdlrdata->ubHash, var) );
         SCIPdebugMessage("added column corr. to upper bound (%f) of variable <%s> to alternative polyhedron (col: %d).\n",
            val, SCIPvarGetName(var), nCols + 1 + nNewCols);
         ++nNewCols;
      }
   }

   /* add columns if necessary */
   if ( nNewCols > 0 )
   {
      SCIP_CALL( SCIPlpiAddCols(conshdlrdata->altLP, nNewCols, obj, lb, ub, NULL, cnt, matbeg, matind, matval) );
   }

#ifndef NDEBUG
   SCIP_CALL( SCIPlpiGetNCols(conshdlrdata->altLP, &cnt) );
   assert( cnt == nCols + nNewCols + 1 );
#endif

   SCIPfreeBufferArray(scip, &ub);
   SCIPfreeBufferArray(scip, &lb);
   SCIPfreeBufferArray(scip, &obj);
   SCIPfreeBufferArray(scip, &matind);
   SCIPfreeBufferArray(scip, &matval);
   SCIPfreeBufferArray(scip, &matbeg);
   SCIPfreeBufferArray(scip, &newVars);
   SCIPfreeBufferArray(scip, &newRowsSlack);

   if ( slackvar != NULL && SCIPvarGetStatus(slackvar) == SCIP_VARSTATUS_AGGREGATED )
   {
      SCIPfreeBufferArray(scip, &linvals);
      SCIPfreeBufferArray(scip, &linvars);
   }
   conshdlrdata->scaled = FALSE;

#ifdef SCIP_OUTPUT
   SCIP_CALL( SCIPlpiWriteLP(conshdlrdata->altLP, "alt.lp") );
#endif

   return SCIP_OKAY;
}


/** delete column corresponding to constraint in alternative LP
 *
 *  We currently just fix the corresponding variable to 0.
 */
static
SCIP_RETCODE deleteAltLPConstraint(
   SCIP*                 scip,               /**< SCIP pointer */
   SCIP_CONSHDLR*        conshdlr,           /**< constraint handler */
   SCIP_CONS*            cons                /**< indicator constraint */
   )
{
   SCIP_CONSHDLRDATA* conshdlrdata;

   assert( scip != NULL );
   assert( conshdlr != NULL );
   assert( cons != NULL );

   assert( strcmp(SCIPconshdlrGetName(conshdlr), CONSHDLR_NAME) == 0 );

   conshdlrdata = SCIPconshdlrGetData(conshdlr);
   assert( conshdlrdata != NULL );

   if ( conshdlrdata->altLP != NULL )
   {
      SCIP_CONSDATA* consdata;

      SCIPdebugMessage("Deleting column from alternative LP ...\n");

      consdata = SCIPconsGetData(cons);
      assert( consdata != NULL );

      if ( consdata->colIndex >= 0 )
      {
         SCIP_CALL( fixAltLPVariable(conshdlrdata->altLP, consdata->colIndex) );
      }
      consdata->colIndex = -1;
   }
   conshdlrdata->scaled = FALSE;

   return SCIP_OKAY;
}


/** Check whether the given LP is infeasible
 *
 *  If @a primal is false we assume that the problem is <em>dual feasible</em>, e.g., the problem
 *  was only changed by fixing bounds!
 *
 *  This is the workhorse for all methods that have to solve the alternative LP. We try in several
 *  ways to recover from possible stability problems.
 *
 *  @pre It is assumed that all parameters for the alternative LP are set.
 */
static
SCIP_RETCODE checkAltLPInfeasible(
   SCIP*                 scip,               /**< SCIP pointer */
   SCIP_LPI*             lp,                 /**< LP */
   SCIP_Real             maxcondition,       /**< maximal allowed condition of LP solution basis matrix */
   SCIP_Bool             primal,             /**< whether we are using the primal or dual simplex */
   SCIP_Bool*            infeasible,         /**< output: whether the LP is infeasible */
   SCIP_Bool*            error               /**< output: whether an error occured */
   )
{
   SCIP_RETCODE retcode;
   SCIP_Real condition;

   assert( scip != NULL );
   assert( lp != NULL );
   assert( infeasible != NULL );
   assert( error != NULL );

   *error = FALSE;

   /* solve LP */
   if ( primal )
      retcode = SCIPlpiSolvePrimal(lp);  /* use primal simplex */
   else
      retcode = SCIPlpiSolveDual(lp);    /* use dual simplex */
   if ( retcode == SCIP_LPERROR )
   {
      *error = TRUE;
      return SCIP_OKAY;
   }
   SCIP_CALL( retcode );

   /* resolve if LP is not stable */
   if ( ! SCIPlpiIsStable(lp) )
   {
      SCIP_CALL_PARAM( SCIPlpiSetIntpar(lp, SCIP_LPPAR_FROMSCRATCH, TRUE) );
      SCIP_CALL_PARAM( SCIPlpiSetIntpar(lp, SCIP_LPPAR_PRESOLVING, FALSE) );
      SCIPwarningMessage("Numerical problems, retrying ...\n");

      /* re-solve LP */
      if ( primal )
         retcode = SCIPlpiSolvePrimal(lp);  /* use primal simplex */
      else
         retcode = SCIPlpiSolveDual(lp);    /* use dual simplex */
      
      if ( retcode == SCIP_LPERROR )
      {
         /* reset parameters */
         SCIP_CALL_PARAM( SCIPlpiSetIntpar(lp, SCIP_LPPAR_FROMSCRATCH, FALSE) );
         SCIP_CALL_PARAM( SCIPlpiSetIntpar(lp, SCIP_LPPAR_PRESOLVING, TRUE) );
         
         *error = TRUE;
         return SCIP_OKAY;
      }
      SCIP_CALL( retcode );

      /* reset parameters */
      SCIP_CALL_PARAM( SCIPlpiSetIntpar(lp, SCIP_LPPAR_FROMSCRATCH, FALSE) );
      SCIP_CALL_PARAM( SCIPlpiSetIntpar(lp, SCIP_LPPAR_PRESOLVING, TRUE) );
   }

   /* check whether we want to ignore the result, because the condition number is too large */
   if ( maxcondition > 0.0 )
   {
      /* check estimated condition number of basis matrix */
      SCIP_CALL( SCIPlpiGetRealSolQuality(lp, SCIP_LPSOLQUALITY_ESTIMCONDITION, &condition) );
      if ( condition != SCIP_INVALID && condition > maxcondition )
      {
         SCIPdebugMessage("estim. condition number of basis matrix (%e) exceeds maximal allowance (%e).\n", condition, maxcondition);

         *error = TRUE;

         return SCIP_OKAY;
      }
      else if ( condition != SCIP_INVALID )
      {
         SCIPdebugMessage("estim. condition number of basis matrix (%e) is below maximal allowance (%e).\n", condition, maxcondition);
      }
      else
      {
         SCIPdebugMessage("estim. condition number of basis matrix not available.\n");
      }
   }

   /* check whether we are in the paradoxical situtation that
    * - the primal is not infeasible
    * - the primal is not unbounded
    * - the LP is not optimal
    * - we have a primal ray
    *
    * If we ran the dual simplex algorithm, then we run again with the primal simplex
    */
   if ( ! SCIPlpiIsPrimalInfeasible(lp) && ! SCIPlpiIsPrimalUnbounded(lp) &&
        ! SCIPlpiIsOptimal(lp) && SCIPlpiExistsPrimalRay(lp) && !primal )
   {
      SCIPwarningMessage("The dual simplex produced a primal ray. Retrying with primal ...\n");
      /* the following settings might be changed: */
      SCIP_CALL_PARAM( SCIPlpiSetIntpar(lp, SCIP_LPPAR_FROMSCRATCH, TRUE) );
      SCIP_CALL_PARAM( SCIPlpiSetIntpar(lp, SCIP_LPPAR_PRESOLVING, TRUE) );
      SCIP_CALL_PARAM( SCIPlpiSetIntpar(lp, SCIP_LPPAR_SCALING, TRUE) );

      SCIP_CALL( SCIPlpiSolvePrimal(lp) );   /* use primal simplex */

      /* reset parameters */
      SCIP_CALL_PARAM( SCIPlpiSetIntpar(lp, SCIP_LPPAR_FROMSCRATCH, FALSE) );
      SCIP_CALL_PARAM( SCIPlpiSetIntpar(lp, SCIP_LPPAR_PRESOLVING, TRUE) );
      SCIP_CALL_PARAM( SCIPlpiSetIntpar(lp, SCIP_LPPAR_SCALING, TRUE) );
   }

   /* examine LP solution status */
   if ( SCIPlpiIsPrimalInfeasible(lp) )     /* the LP is provably infeasible */
   {
      assert( ! SCIPlpiIsPrimalUnbounded(lp) );   /* can't be unbounded or optimal */
      assert( ! SCIPlpiIsOptimal(lp) );           /* if it is infeasible! */
      *infeasible = TRUE;                         /* LP is infeasible */
      return SCIP_OKAY;
   }
   else
   {
      /* By assumption the dual is feasible if the dual simplex is run, therefore
       * the status has to be primal unbounded or optimal. */
      if ( ! SCIPlpiIsPrimalUnbounded(lp) && ! SCIPlpiIsOptimal(lp) )
      {
         /* We have a status different from unbounded or optimal. This should not be the case ... */
         if (primal)
         {
            SCIPwarningMessage("Primal simplex returned with unknown status: %d\n", SCIPlpiGetInternalStatus(lp));
         }
         else
         {
            SCIPwarningMessage("Dual simplex returned with unknown status: %d\n", SCIPlpiGetInternalStatus(lp));
         }
         /* SCIP_CALL( SCIPlpiWriteLP(lp, "debug.lp") ); */
         *error = TRUE;
         return SCIP_OKAY;
      }
   }

   /* at this point we have a feasible solution */
   *infeasible = FALSE;
   return SCIP_OKAY;
}


/** Tries to extend a given set of variables to a cover.
 *
 *  At each step we include a variable which covers a new IIS. Ties are broken according to the
 *  number of IISs a variable is contained in.  The corresponding IIS inequalities are added to the
 *  LP if this not already happend.
 *
 *  @pre It is assumed that all parameters for the alternative LP are set and that the variables
 *  corresponding to @a S are fixed. Furthermore @c xVal_ should contain the current LP solution.
 */
static
SCIP_RETCODE extendToCover(
   SCIP*                 scip,               /**< SCIP pointer */
   SCIP_CONSHDLRDATA*    conshdlrdata,       /**< constraint handler */
   SCIP_LPI*             lp,                 /**< LP */
   SCIP_SOL*             sol,                /**< solution to be separated */
   SCIP_Bool             removable,          /**< whether cuts should be removable */
   SCIP_Bool             genLogicor,         /**< should logicor constraints be generated? */
   int                   nconss,             /**< number of constraints */
   SCIP_CONS**           conss,              /**< indicator constraints */
   SCIP_Bool*            S,                  /**< bitset of variables */
   int*                  size,               /**< size of S */
   SCIP_Real*            value,              /**< objective value of S */
   SCIP_Bool*            error,              /**< output: whether an error occured */
   int*                  nGen                /**< number of generated cuts */
   )
{
   int step = 0;
   SCIP_Real* primsol = NULL;
   int nCols = 0;

   assert( scip != NULL );
   assert( lp != NULL );
   assert( conss != NULL );
   assert( S != NULL );
   assert( size != NULL );
   assert( value != NULL );
   assert( nGen != NULL );

   SCIP_CALL( SCIPlpiGetNCols(lp, &nCols) );
   SCIP_CALL( SCIPallocBufferArray(scip, &primsol, nCols) );
   assert( nconss <= nCols );

   *nGen = 0;
   *error = FALSE;
   do
   {
      SCIP_Bool infeasible = FALSE;
      SCIP_Real sum = 0.0;
      int sizeIIS = 0;
      int candidate = -1;
      int candIndex = -1;
      SCIP_Real candObj = -1.0;
      int j;

      if ( step == 0 )
      {
         /* the first LP is solved without warm start, after that we use a warmstart. */
         SCIP_CALL_PARAM( SCIPlpiSetIntpar(lp, SCIP_LPPAR_FROMSCRATCH, TRUE) );
         SCIP_CALL( checkAltLPInfeasible(scip, lp, conshdlrdata->maxConditionAltLP, TRUE, &infeasible, error) );
         SCIP_CALL_PARAM( SCIPlpiSetIntpar(lp, SCIP_LPPAR_FROMSCRATCH, FALSE) );
      }
      else
         SCIP_CALL( checkAltLPInfeasible(scip, lp, conshdlrdata->maxConditionAltLP, FALSE, &infeasible, error) );

      if ( *error )
         break;

      if ( infeasible )
         break;

      /* get solution of alternative LP */
      SCIP_CALL( SCIPlpiGetSol(lp, NULL, primsol, NULL, NULL, NULL) );

      /* get value of cut and find candidate for variable to add */
      for (j = 0; j < nconss; ++j)
      {
         SCIP_CONSDATA* consdata;
         int ind;

         consdata = SCIPconsGetData(conss[j]);
         assert( consdata != NULL );
         ind = consdata->colIndex;

         if ( ind >= 0 )
         {
            assert( ind < nCols );

            /* check support of the solution, i.e., the corresponding IIS */
            if ( ! SCIPisFeasZero(scip, primsol[ind]) )
            {
               assert( ! S[j] );
               ++sizeIIS;
               sum += SCIPgetSolVal(scip, sol, consdata->binvar);
               /* take first element */
               if ( candidate < 0 )
               {
                  candidate = j;
                  candIndex = ind;
                  candObj = SCIPvarGetObj(consdata->binvar);
               }
            }
         }
      }

      /* check for error */
      if ( candidate < 0 )
      {
         /* Because of numerical problem it might happen that the solution primsol above is zero
            within the tolerances. In this case we quit. */
         break;
      }
      assert( candidate >= 0 );
      assert( ! S[candidate] );

      /* update new set S */
      SCIPdebugMessage("   size: %4d  add %4d with objective value %f and alt-LP solution value %g  (IIS size: %d)\n", *size, candidate, candObj, primsol[SCIPconsGetData(conss[candidate])->colIndex], sizeIIS);
      S[candidate] = TRUE;
      ++(*size);
      *value += candObj;

      /* fix chosen variable to 0 */
      SCIP_CALL( fixAltLPVariable(lp, candIndex) );

      /* if cut is violated, i.e., sum - sizeIIS + 1 > 0 */
      if ( SCIPisEfficacious(scip, sum - (SCIP_Real) (sizeIIS - 1)) )
      {
         SCIP_Bool isLocal = TRUE;

#ifdef SCIP_ENABLE_IISCHECK
         /* check whether we really have an infeasible subsystem */
         SCIP_CALL( checkIIS(scip, nconss, conss, primsol) );
#endif

         /* check whether IIS corresponds to a local cut */
         SCIP_CALL( checkIISlocal(scip, conshdlrdata, primsol, &isLocal) );

         if ( genLogicor )
         {
            SCIP_CONS* cons;
            SCIP_VAR** vars;
            int cnt =0;

            SCIP_CALL( SCIPallocBufferArray(scip, &vars, nconss) );

            /* collect variables corresponding to support to cut */
            for (j = 0; j < nconss; ++j)
            {
               int ind;
               SCIP_CONSDATA* consdata;
               consdata = SCIPconsGetData(conss[j]);
               ind = consdata->colIndex;

               if ( ind >= 0 )
               {
                  assert( ind < nCols );
                  assert( consdata->binvar != NULL );

                  /* check support of the solution, i.e., the corresponding IIS */
                  if ( ! SCIPisFeasZero(scip, primsol[ind]) )
                  {
                     SCIP_VAR* var;
                     SCIP_CALL( SCIPgetNegatedVar(scip, consdata->binvar, &var) );
                     vars[cnt++] = var;
                  }
               }
            }
            assert( cnt == sizeIIS );

            SCIP_CALL( SCIPcreateConsLogicor(scip, &cons, "iis", cnt, vars, FALSE, TRUE, TRUE, TRUE, TRUE, isLocal, FALSE, TRUE, removable, FALSE) );

#ifdef SCIP_OUTPUT
            SCIP_CALL( SCIPprintCons(scip, cons, NULL) );
#endif

            SCIP_CALL( SCIPaddCons(scip, cons) );
            SCIP_CALL( SCIPreleaseCons(scip, &cons) );

            SCIPfreeBufferArray(scip, &vars);
            ++(*nGen);
         }
         else
         {
            SCIP_ROW* row;

            /* create row */
            SCIP_CALL( SCIPcreateEmptyRow(scip, &row, "iis", -SCIPinfinity(scip), (SCIP_Real) (sizeIIS - 1), isLocal, FALSE, removable) );
            SCIP_CALL( SCIPcacheRowExtensions(scip, row) );

            /* add variables corresponding to support to cut */
            for (j = 0; j < nconss; ++j)
            {
               int ind;
               SCIP_CONSDATA* consdata;
               consdata = SCIPconsGetData(conss[j]);
               ind = consdata->colIndex;

               if ( ind >= 0 )
               {
                  assert( ind < nCols );
                  assert( consdata->binvar != NULL );

                  /* check support of the solution, i.e., the corresponding IIS */
                  if ( ! SCIPisFeasZero(scip, primsol[ind]) )
                  {
                     SCIP_VAR* var = consdata->binvar;
                     SCIP_CALL( SCIPaddVarToRow(scip, row, var, 1.0) );
                  }
               }
            }
            SCIP_CALL( SCIPflushRowExtensions(scip, row) );
#ifdef SCIP_OUTPUT
            SCIProwPrint(row, NULL);
#endif
            SCIP_CALL( SCIPaddCut(scip, sol, row, FALSE) );

            /* cut should be violated: */
            assert( SCIPisFeasNegative(scip, SCIPgetRowSolFeasibility(scip, row, sol)) );

            /* add cuts to pool if they are globally valid */
            if ( ! isLocal )
               SCIP_CALL( SCIPaddPoolCut(scip, row) );
            SCIP_CALL( SCIPreleaseRow(scip, &row));
            ++(*nGen);
         }
      }
      ++step;
   }
   while (step < nconss);

   SCIPfreeBufferArray(scip, &primsol);

   return SCIP_OKAY;
}






/* ---------------------------- constraint handler local methods ----------------------*/

/** creates and intializes consdata */
static
SCIP_RETCODE consdataCreate(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONSHDLR*        conshdlr,           /**< constraint handler */
   const char*           consname,           /**< name of constraint (or NULL) */
   SCIP_CONSDATA**       consdata,           /**< pointer to linear constraint data */
   SCIP_EVENTHDLR*       eventhdlr,          /**< event handler to call for the event processing */   
   SCIP_VAR*             binvar,             /**< binary variable */
   SCIP_VAR*             slackvar,           /**< slack variable */
   SCIP_CONS*            lincons,            /**< linear constraint (or NULL) */
   SCIP_Bool             linconsactive,      /**< whether the linear constraint is active */
   SCIP_Bool             sepaAlternativeLP   /**< whether the alternative lp is used for separation */
   )
{
   assert( scip != NULL );
   assert( consdata != NULL );

   /* create constraint data */
   SCIP_CALL( SCIPallocBlockMemory(scip, consdata) );
   (*consdata)->nFixedNonzero = 0;
   (*consdata)->colIndex = -1;
   (*consdata)->linconsActive = linconsactive;
   (*consdata)->binvar = binvar;
   (*consdata)->slackvar = slackvar;
   (*consdata)->lincons = lincons;

   /* if we are transformed, obtain transformed variables and catch events */
   if ( SCIPisTransformed(scip) )
   {
      SCIP_VAR* var;

      /* handle binary variable */
      if ( binvar == NULL )
         (*consdata)->binvar = NULL;
      else
      {
         SCIP_CALL( SCIPgetTransformedVar(scip, binvar, &var) );
         assert( var != NULL );
         (*consdata)->binvar = var;

         /* check type */
         if ( SCIPvarGetType(var) != SCIP_VARTYPE_BINARY )
         {
            SCIPerrorMessage("Indicator variable <%s> is not binary %d.\n", SCIPvarGetName(var), SCIPvarGetType(var));
            return SCIP_ERROR;
         }

         /* catch bound change events on binary variable */
         if ( linconsactive )
         {
            SCIP_CALL( SCIPcatchVarEvent(scip, var, SCIP_EVENTTYPE_BOUNDCHANGED, eventhdlr, (SCIP_EVENTDATA*)*consdata, NULL) );
         }

         /* if binary variable is fixed to be nonzero */
         if ( SCIPvarGetLbLocal(var) > 0.5 )
            ++((*consdata)->nFixedNonzero);
      }

      /* handle slack variable */
      if ( slackvar != NULL )
      {
         SCIP_CALL( SCIPgetTransformedVar(scip, slackvar, &var) );
         assert( var != NULL );
         (*consdata)->slackvar = var;

         /* catch bound change events on slack variable and adjust nFixedNonzero */
         if ( linconsactive )
         {
            SCIP_CALL( SCIPcatchVarEvent(scip, var, SCIP_EVENTTYPE_BOUNDCHANGED, eventhdlr, (SCIP_EVENTDATA*)*consdata, NULL) );

            /* if slack variable is fixed to be nonzero */
            if ( SCIPisFeasPositive(scip, SCIPvarGetLbLocal(var)) )
               ++((*consdata)->nFixedNonzero);
         }
      }

      /* add corresponding column to alternative LP if the constraint is new */
      if ( sepaAlternativeLP && SCIPgetStage(scip) >= SCIP_STAGE_INITSOLVE && lincons != NULL )
      {
         assert( lincons != NULL );
         assert( consname != NULL );
         SCIPdebugMessage("Adding column for <%s> to alternative LP ...\n", consname);
#ifdef SCIP_OUTPUT
         SCIP_CALL( SCIPprintCons(scip, lincons, NULL) );
#endif
         SCIP_CALL( addAltLPConstraint(scip, conshdlr, lincons, var, 1.0, &(*consdata)->colIndex) );
         SCIPdebugMessage("Colum index for <%s>: %d\n", consname, (*consdata)->colIndex);
      }
      
#ifdef SCIP_DEBUG
      if ( (*consdata)->nFixedNonzero > 0 )
      {
         SCIPdebugMessage("constraint <%s> has %d variables fixed to be nonzero.\n", consname, (*consdata)->nFixedNonzero);
      }
#endif
   }

   return SCIP_OKAY;
}


/** create variable upper bounds for constraints */
static
SCIP_RETCODE createVarUbs(
   SCIP*                 scip,               /**< SCIP pointer */
   SCIP_CONSHDLRDATA*    conshdlrdata,       /**< constraint handler data */
   SCIP_CONS**           conss,              /**< constraints */
   int                   nconss,             /**< number of constraints */
   int*                  ngen                /**< number of successful operations */
   )
{
   char name[50];
   int c;

   assert( scip != NULL );
   assert( conshdlrdata != NULL );
   assert( ngen != NULL );

   *ngen = 0;

   /* check each constraint */
   for (c = 0; c < nconss; ++c)
   {
      SCIP_CONSDATA* consdata;
      SCIP_Real ub;

      consdata = SCIPconsGetData(conss[c]);
      assert( consdata != NULL );

      ub = SCIPvarGetUbGlobal(consdata->slackvar);
      assert( ! SCIPisNegative(scip, ub) );

      /* insert corresponding row if helpful and coefficient is not too large */
      if ( ub <= conshdlrdata->maxCouplingValue )
      {
         SCIP_CONS* cons;

#ifndef NDEBUG
         (void) SCIPsnprintf(name, 50, "couple%d", c);
#else
         name[0] = '\0';
#endif

         SCIPdebugMessage("Insert coupling varbound constraint for indicator constraint <%s> (coeff: %f).\n", 
            SCIPconsGetName(conss[c]), ub);

         /* add variable upper bound:
          * - check constraint if we remove the indicator constraint afterwards 
          * - constraint is dynamic if we do not remove indicator constraints
          * - constraint is removable if we do not remove indicator constraints 
          */
         SCIP_CALL( SCIPcreateConsVarbound(scip, &cons, name, consdata->slackvar, consdata->binvar, ub, -SCIPinfinity(scip), ub,
               TRUE, TRUE, TRUE, conshdlrdata->removeIndicators, TRUE, FALSE, FALSE, 
               !conshdlrdata->removeIndicators, !conshdlrdata->removeIndicators, FALSE) );

         SCIP_CALL( SCIPaddCons(scip, cons) );
         SCIP_CALL( SCIPreleaseCons(scip, &cons) );

         /* remove indicator constraint if required */
         if ( conshdlrdata->removeIndicators )
         {
            SCIPdebugMessage("Removing indicator constraint <%s>.\n", SCIPconsGetName(conss[c]));
            assert( ! SCIPconsIsModifiable(conss[c]) );
            SCIP_CALL( SCIPdelCons(scip, conss[c]) );
         }

         ++(*ngen);
      }
   }

   return SCIP_OKAY;
}


/** propagate indicator constraint */
static
SCIP_RETCODE propIndicator(
   SCIP*                 scip,               /**< SCIP pointer */
   SCIP_CONS*            cons,               /**< constraint */
   SCIP_CONSDATA*        consdata,           /**< constraint data */
   SCIP_Bool*            cutoff,             /**< whether a cutoff happend */
   int*                  nGen                /**< number of domain changes */
   )
{
   assert( scip != NULL );
   assert( cons != NULL );
   assert( consdata != NULL );
   assert( cutoff != NULL );
   assert( nGen != NULL );

   *cutoff = FALSE;
   *nGen = 0;

   /* if the linear constraint has not been generated, we do nothing */
   if ( ! consdata->linconsActive )
      return SCIP_OKAY;

   /* if both slackvar and binvar are fixed to be nonzero */
   if ( consdata->nFixedNonzero > 1 )
   {
      SCIPdebugMessage("the node is infeasible, both the slackvariable and the binary variable are fixed to be nonzero.\n");
      SCIP_CALL( SCIPresetConsAge(scip, cons) );
      *cutoff = TRUE;
      return SCIP_OKAY;
   }

   /* if exactly one of the variables is fixed to be nonzero */
   if ( consdata->nFixedNonzero == 1 )
   {
      SCIP_Bool infeasible, tightened;

      /* increase age of constraint; age is reset to zero, if a conflict or a propagation was found */
      if( !SCIPinRepropagation(scip) )
         SCIP_CALL( SCIPincConsAge(scip, cons) );

      /* if binvar is fixed to be nonzero */
      if ( SCIPvarGetLbLocal(consdata->binvar) > 0.5 )
      {
         assert( SCIPvarGetStatus(consdata->slackvar) != SCIP_VARSTATUS_MULTAGGR );

         SCIPdebugMessage("binary variable <%s> is fixed to be nonzero, fixing slack variable <%s> to 0.\n",
            SCIPvarGetName(consdata->binvar), SCIPvarGetName(consdata->slackvar));

         /* fix slackvariable to 0 */
         SCIP_CALL( SCIPinferVarUbCons(scip, consdata->slackvar, 0.0, cons, 0, FALSE, &infeasible, &tightened) );
         assert( ! infeasible );
         if ( tightened )
            ++(*nGen);
      }

      /* if slackvar is fixed to be nonzero */
      if ( SCIPisFeasPositive(scip, SCIPvarGetLbLocal(consdata->slackvar)) )
      {
         SCIPdebugMessage("slack variable <%s> is fixed to be nonzero, fixing binary variable <%s> to 0.\n",
            SCIPvarGetName(consdata->slackvar), SCIPvarGetName(consdata->binvar));

         /* fix binary variable to 0 */
         SCIP_CALL( SCIPinferVarUbCons(scip, consdata->binvar, 0.0, cons, 1, FALSE, &infeasible, &tightened) );
         assert( ! infeasible );
         if ( tightened )
            ++(*nGen);
      }

      /* reset constraint age counter */
      if ( *nGen > 0 )
         SCIP_CALL( SCIPresetConsAge(scip, cons) );

      /* delete constraint locally */
      assert( !SCIPconsIsModifiable(cons) );
      SCIP_CALL( SCIPdelConsLocal(scip, cons) );
   }
   else
   {
      /* if the slack variable is fixed to zero */
      if ( SCIPisFeasZero(scip, SCIPvarGetUbLocal(consdata->slackvar)) )
      {
         SCIPdebugMessage("Slack variable fixed to zero, delete redundant indicator constraint <%s>.\n",   SCIPconsGetName(cons));

         /* delete constraint */
         assert( ! SCIPconsIsModifiable(cons) );
         SCIP_CALL( SCIPdelConsLocal(scip, cons) );
         SCIP_CALL( SCIPresetConsAge(scip, cons) );
         ++(*nGen);
      }

      /* Note that because of possible mulit-aggregation we cannot simply remove the indicator
       * constraint if the linear constraint is not active or disabled - see the note in @ref
       * PREPROC.
       */

      /* We cannot remove linear constraints, because of the reasons stated in
       * consPresolIndicator(). Moreover, it would drastically increase memory consumption, because
       * the linear constraints have to be stored in each node. */
   }

   return SCIP_OKAY;
}


/** enforcement method that produces cuts if possible
 *
 *  This is a variant of the enforcement method that generates cuts/constraints via the alternative
 *  LP, if possible.
 */
static
SCIP_RETCODE enforceCuts(
   SCIP*                 scip,               /**< SCIP pointer */
   SCIP_CONSHDLR*        conshdlr,           /**< constraint handler */
   int                   nconss,             /**< number of constraints */
   SCIP_CONS**           conss,              /**< indicator constraints */
   SCIP_SOL*             sol,                /**< solution to be enforced */
   SCIP_Bool             genLogicor,         /**< whether logicor constraint should be generated */
   int*                  nGen                /**< number of cuts generated */
)
{
   SCIP_CONSHDLRDATA* conshdlrdata;
   SCIP_LPI* lp;
   SCIP_Bool* S;
   SCIP_Real value;
   SCIP_Bool error;
   int size;
   int nCuts;
   int j;

   assert( scip != NULL );
   assert( conshdlr != NULL );
   assert( conss != NULL );
   assert( nGen != NULL );

   SCIPdebugMessage("Enforcing via cuts ...\n");
   *nGen = 0;

   conshdlrdata = SCIPconshdlrGetData(conshdlr);
   assert( conshdlrdata != NULL );
   lp = conshdlrdata->altLP;
   assert( lp != NULL );

#ifndef NDEBUG
   SCIP_CALL( checkLPBoundsClean(scip, lp, nconss, conss) );
#endif

   /* change coefficients of bounds in alternative LP */
   if ( conshdlrdata->updateBounds )
      SCIP_CALL( updateFirstRowGlobal(scip, conshdlrdata) );

   /* scale first row if necessary */
   SCIP_CALL( scaleFirstRow(scip, conshdlrdata) );

   /* set obj. func. to current solution */
   SCIP_CALL( setAltLPObjZero(scip, lp, nconss, conss) );

   SCIP_CALL( SCIPallocBufferArray(scip, &S, nconss) );

   /* set up variables fixed to 1 */
   size = 0;
   value = 0.0;
   for (j = 0; j < nconss; ++j)
   {
      SCIP_CONSDATA* consdata;

      assert( conss[j] != NULL );
      consdata = SCIPconsGetData(conss[j]);
      assert( consdata != NULL );

      assert( SCIPisFeasIntegral(scip, SCIPgetSolVal(scip, sol, consdata->binvar)) );
      if ( SCIPisFeasZero(scip, SCIPgetSolVal(scip, sol, consdata->binvar)) )
      {
         ++size;
         value += SCIPvarGetObj(consdata->binvar);
         S[j] = TRUE;
      }
      else
         S[j] = FALSE;
   }

   /* fix the variables in S */
   SCIP_CALL( fixAltLPVariables(scip, lp, nconss, conss, S) );

   /* extend set S to a cover and generate cuts */
   error = FALSE;
   SCIP_CALL( extendToCover(scip, conshdlrdata, lp, sol, conshdlrdata->removable, genLogicor, nconss, conss, S, &size, &value, &error, &nCuts) );
   *nGen = nCuts;

   /* return with an error if no cuts have been produced and and error occured in extendToCover() */
   if ( nCuts == 0 && error )
      return SCIP_LPERROR;

   SCIPdebugMessage("Generated %d IIS-cuts.\n", nCuts);

   /* reset bounds */
   SCIP_CALL( unfixAltLPVariables(scip, lp, nconss, conss, S) );

#ifndef NDEBUG
   SCIP_CALL( checkLPBoundsClean(scip, lp, nconss, conss) );
#endif

   SCIPfreeBufferArray(scip, &S);

   return SCIP_OKAY;
}


/** enforcement method
 *
 *  We check whether the current solution is feasible, i.e., if binvar = 1
 *  implies that slackvar = 0. If not, we branch as follows:
 *
 *  In one branch we fix binvar = 1 and slackvar = 0. In the other branch
 *  we fix binvar = 0 and leave slackvar unchanged.
 */
static
SCIP_RETCODE enforceIndicators(
   SCIP*                 scip,               /**< SCIP pointer */
   SCIP_CONSHDLR*        conshdlr,           /**< constraint handler */
   int                   nconss,             /**< number of constraints */
   SCIP_CONS**           conss,              /**< indicator constraints */
   SCIP_Bool             genLogicor,         /**< whether logicor constraint should be generated */
   SCIP_RESULT*          result              /**< result */
   )
{
   SCIP_CONSDATA* consdata;
   SCIP_CONSHDLRDATA* conshdlrdata;
   SCIP_NODE* node1;
   SCIP_NODE* node2;
   SCIP_VAR* slackvar;
   SCIP_VAR* binvar;
   SCIP_CONS* branchCons = NULL;
   SCIP_Real maxSlack = -1.0;
   SCIP_Bool someLinconsNotActive = FALSE;
   int c;

   assert( scip != NULL );
   assert( conshdlr != NULL );
   assert( conss != NULL );
   assert( result != NULL );

   *result = SCIP_FEASIBLE;

   SCIPdebugMessage("Enforcing indicator constraints <%s>.\n", SCIPconshdlrGetName(conshdlr) );

   /* check each constraint */
   for (c = 0; c < nconss; ++c)
   {
      SCIP_Bool cutoff;
      SCIP_Real valSlack;
      int cnt = 0;

      assert( conss[c] != NULL );
      consdata = SCIPconsGetData(conss[c]);
      assert( consdata != NULL );
      assert( consdata->lincons != NULL );

      /* if the linear constraint has not been generated, we do nothing */
      if ( ! consdata->linconsActive )
      {
         someLinconsNotActive = TRUE;
         continue;
      }

      /* first perform propagation (it might happen that standard propagation is turned off) */
      SCIP_CALL( propIndicator(scip, conss[c], consdata, &cutoff, &cnt) );
      if ( cutoff )
      {
         SCIPdebugMessage("propagation in enforcing <%s> detected cutoff.\n", SCIPconsGetName(conss[c]));
         *result = SCIP_CUTOFF;
         return SCIP_OKAY;
      }
      if ( cnt > 0 )
      {
         SCIPdebugMessage("propagation in enforcing <%s> reduced domains: %d.\n", SCIPconsGetName(conss[c]), cnt);
         *result = SCIP_REDUCEDDOM;
         return SCIP_OKAY;
      }

      /* check whether constraint is infeasible */
      binvar = consdata->binvar;
      valSlack = SCIPgetSolVal(scip, NULL, consdata->slackvar);
      assert( ! SCIPisFeasNegative(scip, valSlack) );
      if ( ! SCIPisFeasZero(scip, SCIPgetSolVal(scip, NULL, binvar)) && ! SCIPisFeasZero(scip, valSlack) )
      {
         /* binary variable is not fixed - otherwise we would not be infeasible */
         assert( SCIPvarGetLbLocal(binvar) < 0.5 && SCIPvarGetUbLocal(binvar) > 0.5 );

         if ( valSlack > maxSlack )
         {
            maxSlack = valSlack;
            branchCons = conss[c];
         }
      }
   }

   /* get constraint handler data */
   conshdlrdata = SCIPconshdlrGetData(conshdlr);
   assert( conshdlrdata != NULL );

   /* if some constraint has a linear constraint that is not active, we need to check feasibility via the alternative polyhedron */
   if ( (someLinconsNotActive || conshdlrdata->enforceCuts) && conshdlrdata->sepaAlternativeLP )
   {
      int nGen;

      SCIP_CALL( enforceCuts(scip, conshdlr, nconss, conss, NULL, genLogicor, &nGen) );
      if ( nGen > 0 )
      {
         if ( genLogicor )
         {
            SCIPdebugMessage("Generated %d constraints.\n", nGen);
            *result = SCIP_CONSADDED;
         }
         else
         {
            SCIPdebugMessage("Generated %d cuts.\n", nGen);
            *result = SCIP_SEPARATED;
         }
         return SCIP_OKAY;
      }
      SCIPdebugMessage("Enforcing produced no cuts.\n");

      assert( ! someLinconsNotActive || branchCons == NULL );
   }

   /* if all constraints are feasible */
   if ( branchCons == NULL )
   {
      SCIPdebugMessage("All indicator constraints are feasible.\n");
      return SCIP_OKAY;
   }

   /* skip branching if required */
   if ( ! conshdlrdata->branchIndicators )
   {
      *result = SCIP_INFEASIBLE;
      return SCIP_OKAY;
   }

   /* otherwise create branches */
   SCIPdebugMessage("Branching on constraint <%s> (slack value: %f).\n", SCIPconsGetName(branchCons), maxSlack);
   consdata = SCIPconsGetData(branchCons);
   assert( consdata != NULL );
   binvar = consdata->binvar;
   slackvar = consdata->slackvar;

   /* node1: binvar = 1, slackvar = 0 */
   SCIP_CALL( SCIPcreateChild(scip, &node1, 0.0, SCIPcalcChildEstimate(scip, binvar, 1.0) ) );

   if ( SCIPvarGetLbLocal(binvar) < 0.5 )
   {
      SCIP_CALL( SCIPchgVarLbNode(scip, node1, binvar, 1.0) );
   }

   /* if slack-variable is multi-aggregated */
   assert( SCIPvarGetStatus(slackvar) != SCIP_VARSTATUS_MULTAGGR );
   if ( ! SCIPisFeasZero(scip, SCIPvarGetUbLocal(slackvar)) )
   {
      SCIP_CALL( SCIPchgVarUbNode(scip, node1, slackvar, 0.0) );
   }

   /* node2: binvar = 0, no restriction on slackvar */
   SCIP_CALL( SCIPcreateChild(scip, &node2, 0.0, SCIPcalcChildEstimate(scip, binvar, 0.0) ) );

   if ( SCIPvarGetUbLocal(binvar) > 0.5 )
   {
      SCIP_CALL( SCIPchgVarUbNode(scip, node2, binvar, 0.0) );
   }

   SCIP_CALL( SCIPresetConsAge(scip, branchCons) );
   *result = SCIP_BRANCHED;

   return SCIP_OKAY;
}


/** separate IIS-cuts via rounding */
static
SCIP_RETCODE separateIISRounding(
   SCIP*                 scip,               /**< SCIP pointer */
   SCIP_CONSHDLR*        conshdlr,           /**< constraint handler */
   SCIP_SOL*             sol,                /**< solution to be separated */
   int                   nconss,             /**< number of constraints */
   SCIP_CONS**           conss,              /**< indicator constraints */
   int*                  nGen                /**< number of domain changes */
   )
{
   SCIP_CONSHDLRDATA* conshdlrdata;
   SCIP_LPI* lp;
   int rounds;
   SCIP_Real threshold;
   SCIP_Bool* S;
   SCIP_Bool error;
   int nGenOld;

   assert( scip != NULL );
   assert( conshdlr != NULL );
   assert( conss != NULL );
   assert( nGen != NULL );

   rounds = 0;

   conshdlrdata = SCIPconshdlrGetData(conshdlr);
   assert( conshdlrdata != NULL );
   lp = conshdlrdata->altLP;
   assert( lp != NULL );

   nGenOld = *nGen;
   SCIPdebugMessage("Separating IIS-cuts by rounding ...\n");

#ifndef NDEBUG
   SCIP_CALL( checkLPBoundsClean(scip, lp, nconss, conss) );
#endif

   /* change coefficients of bounds in alternative LP */
   if ( conshdlrdata->updateBounds )
   {
      /* update to local bounds */
      SCIP_CALL( updateFirstRow(scip, conshdlrdata) );
   }

   /* scale first row if necessary */
   SCIP_CALL( scaleFirstRow(scip, conshdlrdata) );

   /* set obj. func. to current solution */
   SCIP_CALL( setAltLPObj(scip, lp, sol, nconss, conss) );

   SCIP_CALL( SCIPallocBufferArray(scip, &S, nconss) );

   /* loop through the possible thresholds */
   for (threshold = conshdlrdata->roundingMaxThres; rounds < conshdlrdata->roundingRounds && threshold >= conshdlrdata->roundingMinThres;
        threshold -= conshdlrdata->roundingOffset)
   {
      SCIP_Real value;
      int size;
      int nCuts;
      int j;

      value = 0.0;
      size = 0;
      nCuts = 0;

      SCIPdebugMessage("Threshold: %f\n", threshold);

      /* choose variables that have a value < current threshold value */
      for (j = 0; j < nconss; ++j)
      {
         SCIP_CONSDATA* consdata;

         assert( conss[j] != NULL );
         consdata = SCIPconsGetData(conss[j]);
         assert( consdata != NULL );

         if ( SCIPisFeasLT(scip, SCIPgetVarSol(scip, consdata->binvar), threshold) )
         {
            S[j] = TRUE;
            value += SCIPvarGetObj(consdata->binvar);
            ++size;
         }
         else
            S[j] = FALSE;
      }

      if (size == nconss)
      {
         SCIPdebugMessage("All variables in the set. Continue ...\n");
         continue;
      }

      /* fix the variables in S */
      SCIP_CALL( fixAltLPVariables(scip, lp, nconss, conss, S) );

      /* extend set S to a cover and generate cuts */
      SCIP_CALL( extendToCover(scip, conshdlrdata, lp, sol, conshdlrdata->removable, conshdlrdata->genLogicor, nconss, conss, S, &size, &value, &error, &nCuts) );

      /* we ignore errors in extendToCover */
      if ( nCuts > 0 )
      {
         *nGen += nCuts;
         ++rounds;
      }

      SCIPdebugMessage("Produced cover of size %d with value %f\n", size, value);

      /* todo: check whether the cover is a feasible solution */

      /* reset bounds */
      SCIP_CALL( unfixAltLPVariables(scip, lp, nconss, conss, S) );
   }
   SCIPdebugMessage("Generated %d IISs.\n", *nGen - nGenOld);

#ifndef NDEBUG
   SCIP_CALL( checkLPBoundsClean(scip, lp, nconss, conss) );
#endif

   SCIPfreeBufferArray(scip, &S);

   return SCIP_OKAY;
}




/* ---------------------------- constraint handler callback methods ----------------------*/

/** copy method for constraint handler plugins (called when SCIP copies plugins) */
static
SCIP_DECL_CONSHDLRCOPY(conshdlrCopyIndicator)
{  /*lint --e{715}*/
   assert( scip != NULL );
   assert( conshdlr != NULL );
   assert( strcmp(SCIPconshdlrGetName(conshdlr), CONSHDLR_NAME) == 0 );
   assert( valid != NULL );

   /* call inclusion method of constraint handler */
   SCIP_CALL( SCIPincludeConshdlrIndicator(scip) );

   *valid = TRUE;

   return SCIP_OKAY;
}


/** initialization method of constraint handler (called after problem was transformed) */
static
SCIP_DECL_CONSINIT(consInitIndicator)
{  /*lint --e{715}*/
   SCIP_CONSHDLRDATA* conshdlrdata;

   assert( scip != NULL );
   assert( conshdlr != NULL );
   assert( strcmp(SCIPconshdlrGetName(conshdlr), CONSHDLR_NAME) == 0 );

   conshdlrdata = SCIPconshdlrGetData(conshdlr);
   assert( conshdlrdata != NULL );

   /* find trysol heuristic */
   if ( conshdlrdata->trySolutions && conshdlrdata->heurTrySol == NULL )
   {
      conshdlrdata->heurTrySol = SCIPfindHeur(scip, "trysol");
   }

   return SCIP_OKAY;
}


/** destructor of constraint handler to free constraint handler data (called when SCIP is exiting) */
static
SCIP_DECL_CONSFREE(consFreeIndicator)
{
   SCIP_CONSHDLRDATA* conshdlrdata;

   assert( scip != NULL );
   assert( conshdlr != NULL );
   assert( strcmp(SCIPconshdlrGetName(conshdlr), CONSHDLR_NAME) == 0 );

   conshdlrdata = SCIPconshdlrGetData(conshdlr);
   assert( conshdlrdata != NULL );
   assert( conshdlrdata->altLP == NULL );
   assert( conshdlrdata->varHash == NULL );
   assert( conshdlrdata->lbHash == NULL );
   assert( conshdlrdata->ubHash == NULL );
   assert( conshdlrdata->slackHash == NULL );

   SCIPfreeBlockMemoryArrayNull(scip, &conshdlrdata->addLinCons, conshdlrdata->maxAddLinCons);

   SCIPfreeMemory(scip, &conshdlrdata);

   return SCIP_OKAY;
}


/** solving process initialization method of constraint handler (called when branch and bound process is about to begin) */
static
SCIP_DECL_CONSINITSOL(consInitsolIndicator)
{
   SCIP_CONSHDLRDATA* conshdlrdata;
   int c;

   assert( scip != NULL );
   assert( conshdlr != NULL );
   assert( strcmp(SCIPconshdlrGetName(conshdlr), CONSHDLR_NAME) == 0 );

   conshdlrdata = SCIPconshdlrGetData(conshdlr);
   assert( conshdlrdata != NULL );
   assert( conshdlrdata->slackHash == NULL );

   if ( conshdlrdata->sepaAlternativeLP )
   {
      /* generate hash for storing all slack variables (size is just a guess) */
      SCIP_CALL( SCIPhashmapCreate(&conshdlrdata->slackHash, SCIPblkmem(scip), SCIPcalcHashtableSize(10 * SCIPgetNVars(scip))) );
      assert( conshdlrdata->slackHash != NULL );

      /* first initialize slack hash */
      for (c = 0; c < nconss; ++c)
      {
         SCIP_CONSDATA* consdata;

         assert( conss != NULL );
         assert( conss[c] != NULL );
         assert( SCIPconsIsTransformed(conss[c]) );

         consdata = SCIPconsGetData(conss[c]);
         assert( consdata != NULL );

         assert( consdata->slackvar != NULL );

         /* insert slack variable into hash */
         SCIP_CALL( SCIPhashmapInsert(conshdlrdata->slackHash, consdata->slackvar, (void*) (size_t) (INT_MAX)) );
         assert( SCIPhashmapExists(conshdlrdata->slackHash, consdata->slackvar) );
         ++conshdlrdata->nSlackVars;
      }
   }

   /* check each constraint */
   for (c = 0; c < nconss; ++c)
   {
      SCIP_CONSDATA* consdata;

      assert( conss != NULL );
      assert( conss[c] != NULL );
      assert( SCIPconsIsTransformed(conss[c]) );

      consdata = SCIPconsGetData(conss[c]);
      assert( consdata != NULL );

      /* SCIPdebugMessage("Initializing indicator constraint <%s>.\n", SCIPconsGetName(conss[c]) ); */

      /* deactivate */
      if ( ! consdata->linconsActive )
      {
         SCIP_CALL( SCIPdisableCons(scip, consdata->lincons) );
      }
      else
      {
         /* add constraint to alternative LP if not already done */
         if ( conshdlrdata->sepaAlternativeLP && consdata->colIndex < 0 )
         {
            SCIPdebugMessage("Adding column for <%s> to alternative LP ...\n", SCIPconsGetName(conss[c]));
            SCIP_CALL( addAltLPConstraint(scip, conshdlr, consdata->lincons, consdata->slackvar, 1.0, &consdata->colIndex) );
            SCIPdebugMessage("Colum index for <%s>: %d\n", SCIPconsGetName(conss[c]), consdata->colIndex);
#ifdef SCIP_OUTPUT
            SCIP_CALL( SCIPprintCons(scip, consdata->lincons, NULL) );
#endif
         }
      }
   }

   SCIPdebugMessage("Initialized %d indicator constraints.\n", nconss);

   /* check additional constraints */
   if ( conshdlrdata->sepaAlternativeLP )
   {
      int colIndex;
      int cnt = 0;
      for (c = 0; c < conshdlrdata->nAddLinCons; ++c)
      {
         SCIP_CONS* cons;

         cons = conshdlrdata->addLinCons[c];

         /* get transformed constraint - since it is needed only here, we do not store the information */
         if ( ! SCIPconsIsTransformed(cons) )
         {
            SCIP_CALL( SCIPgetTransformedCons(scip, conshdlrdata->addLinCons[c], &cons) );

            /* @todo check when exactly the transformed constraint does not exist - SCIPisActive() does not suffice */
            if ( cons == NULL )
               continue;
         }
         SCIP_CALL( addAltLPConstraint(scip, conshdlr, cons, NULL, 0.0, &colIndex) );
         ++cnt;

#ifdef SCIP_OUTPUT
         SCIP_CALL( SCIPprintCons(scip, cons, NULL) );
#endif
      }
#ifndef NDEBUG
      if ( conshdlrdata->nAddLinCons > 0 )
      {
         SCIPdebugMessage("Added %d additional columns to alternative LP.\n", cnt);
      }
#endif
   }

   return SCIP_OKAY;
}


/** solving process deinitialization method of constraint handler (called before branch and bound process data is freed) */
static
SCIP_DECL_CONSEXITSOL(consExitsolIndicator)
{  /*lint --e{715}*/
   SCIP_CONSHDLRDATA* conshdlrdata;
   int c;

   assert( scip != NULL );
   assert( conshdlr != NULL );
   assert( strcmp(SCIPconshdlrGetName(conshdlr), CONSHDLR_NAME) == 0 );

   conshdlrdata = SCIPconshdlrGetData(conshdlr);
   assert( conshdlrdata != NULL );

   if ( conshdlrdata->sepaAlternativeLP )
   {
      assert( conshdlrdata->altLP != NULL || nconss == 0 );
      assert( conshdlrdata->slackHash != NULL );

#ifdef SCIP_DEBUG
      SCIPinfoMessage(scip, NULL, "\nStatistics for slack hash:\n");
      SCIPhashmapPrintStatistics(conshdlrdata->slackHash);
#endif

      if ( conshdlrdata->altLP != NULL )
      {
         assert( conshdlrdata->varHash != NULL );
         assert( conshdlrdata->lbHash != NULL );
         assert( conshdlrdata->ubHash != NULL );

#ifdef SCIP_DEBUG
         SCIPinfoMessage(scip, NULL, "\nStatistics for var hash:\n");
         SCIPhashmapPrintStatistics(conshdlrdata->varHash);
         SCIPinfoMessage(scip, NULL, "\nStatistics for slack hash:\n");
         SCIPhashmapPrintStatistics(conshdlrdata->slackHash);
         SCIPinfoMessage(scip, NULL, "\nStatistics for lower bound hash:\n");
         SCIPhashmapPrintStatistics(conshdlrdata->lbHash);
         SCIPinfoMessage(scip, NULL, "\nStatistics for upper bound hash:\n");
         SCIPhashmapPrintStatistics(conshdlrdata->ubHash);
#endif

         SCIPhashmapFree(&conshdlrdata->varHash);
         SCIPhashmapFree(&conshdlrdata->lbHash);
         SCIPhashmapFree(&conshdlrdata->ubHash);

         SCIP_CALL( SCIPlpiFree(&conshdlrdata->altLP) );

         /* save the information that the columns have been deleted */
         for (c = 0; c < nconss; ++c)
         {
            SCIP_CONSDATA* consdata;

            assert( conss != NULL );
            assert( conss[c] != NULL );

            consdata = SCIPconsGetData(conss[c]);
            assert( consdata != NULL );
            consdata->colIndex = -1;
         }
      }
      SCIPhashmapFree(&conshdlrdata->slackHash);
   }

   return SCIP_OKAY;
}


/** frees specific constraint data */
static
SCIP_DECL_CONSDELETE(consDeleteIndicator)
{
   assert( scip != NULL );
   assert( conshdlr != NULL );
   assert( cons != NULL );
   assert( consdata != NULL );
   assert( strcmp(SCIPconshdlrGetName(conshdlr), CONSHDLR_NAME) == 0 );

   SCIPdebugMessage("Deleting indicator constraint <%s>.\n", SCIPconsGetName(cons) );

   /* drop events on transformed variables */
   if ( SCIPconsIsTransformed(cons) )
   {
      SCIP_CONSHDLRDATA* conshdlrdata;

      /* get constraint handler data */
      conshdlrdata = SCIPconshdlrGetData(conshdlr);
      assert( conshdlrdata != NULL );

      if ( conshdlrdata->sepaAlternativeLP )
      {
         SCIP_CALL( deleteAltLPConstraint(scip, conshdlr, cons) );
      }

      assert( (*consdata)->slackvar != NULL );
      assert( (*consdata)->binvar != NULL );

      if ( (*consdata)->linconsActive )
      {
         assert( conshdlrdata->eventhdlr != NULL );
         SCIP_CALL( SCIPdropVarEvent(scip, (*consdata)->binvar, SCIP_EVENTTYPE_BOUNDCHANGED, conshdlrdata->eventhdlr,
               (SCIP_EVENTDATA*)*consdata, -1) );
         SCIP_CALL( SCIPdropVarEvent(scip, (*consdata)->slackvar, SCIP_EVENTTYPE_BOUNDCHANGED, conshdlrdata->eventhdlr,
               (SCIP_EVENTDATA*)*consdata, -1) );
      }

      /* can there be cases where lincons is NULL, e.g., if presolve found the problem infeasible */
      assert( (*consdata)->lincons != NULL );

      /* release linear constraint if it is transformed as well - otherwise initpre has not been called */
      if ( SCIPconsIsTransformed((*consdata)->lincons) )
      {
         SCIP_CALL( SCIPreleaseCons(scip, &(*consdata)->lincons) );
      }
   }
   else
   {
      /* release linear constraint and slack variable only for nontransformed constraint */
      SCIP_CALL( SCIPreleaseVar(scip, &(*consdata)->slackvar) );
      SCIP_CALL( SCIPreleaseCons(scip, &(*consdata)->lincons) );
   }

   SCIPfreeBlockMemory(scip, consdata);

   return SCIP_OKAY;
}


/** transforms constraint data into data belonging to the transformed problem */
static
SCIP_DECL_CONSTRANS(consTransIndicator)
{
   SCIP_CONSDATA* consdata;
   SCIP_CONSHDLRDATA* conshdlrdata;
   SCIP_CONSDATA* sourcedata;
   char s[SCIP_MAXSTRLEN];

   assert( scip != NULL );
   assert( conshdlr != NULL );
   assert( strcmp(SCIPconshdlrGetName(conshdlr), CONSHDLR_NAME) == 0 );
   assert( sourcecons != NULL );
   assert( targetcons != NULL );

   /* get constraint handler data */
   conshdlrdata = SCIPconshdlrGetData(conshdlr);
   assert( conshdlrdata != NULL );
   assert( conshdlrdata->eventhdlr != NULL );

   SCIPdebugMessage("Transforming indicator constraint: <%s>.\n", SCIPconsGetName(sourcecons) );

   /* get data of original constraint */
   sourcedata = SCIPconsGetData(sourcecons);
   assert( sourcedata != NULL );
   assert( sourcedata->binvar != NULL );

   /* check for slackvar */
   if ( sourcedata->slackvar == NULL )
   {
      SCIPerrorMessage("The indicator constraint <%s> needs a slack variable.\n", SCIPconsGetName(sourcecons));
      return SCIP_INVALIDDATA;
   }

   /* check for linear constraint */
   if ( sourcedata->lincons == NULL )
   {
      SCIPerrorMessage("The indicator constraint <%s> needs a linear constraint variable.\n", SCIPconsGetName(sourcecons));
      return SCIP_INVALIDDATA;
   }
   assert( sourcedata->lincons != NULL );
   assert( sourcedata->slackvar != NULL );

   /* create constraint data */
   consdata = NULL;
   SCIP_CALL( consdataCreate(scip, conshdlr, SCIPconsGetName(sourcecons), &consdata, conshdlrdata->eventhdlr, 
         sourcedata->binvar, sourcedata->slackvar, sourcedata->lincons, sourcedata->linconsActive, conshdlrdata->sepaAlternativeLP) );
   assert( consdata != NULL );

   /* check if slack variable can be made implicity integer. We repeat the check from
    * SCIPcreateConsIndicator(), since when reading files in LP-format the type is only determined
    * after creation of the constraint. */
   if ( SCIPvarGetType(consdata->slackvar) != SCIP_VARTYPE_IMPLINT )
   {
      SCIP_Real* vals;
      SCIP_VAR** vars;
      SCIP_VAR* slackvar;
      SCIP_Bool foundslackvar;
      int nvars;
      int i;
      
      vars = SCIPgetVarsLinear(scip, sourcedata->lincons);
      vals = SCIPgetValsLinear(scip, sourcedata->lincons);
      nvars = SCIPgetNVarsLinear(scip, sourcedata->lincons);
      slackvar = sourcedata->slackvar;
      foundslackvar = FALSE;
      for (i = 0; i < nvars; ++i)
      {
         if ( vars[i] == slackvar )
            foundslackvar = TRUE;
         else
         {
            if ( ! SCIPvarIsIntegral(vars[i]) || ! SCIPisIntegral(scip, vals[i]))
               break;
         }
      }
      /* something is strange if the slack variable does not appear in the linear constraint (possibly because it is an artificial constraint) */
      if ( i == nvars && foundslackvar )
      {
         SCIP_Bool infeasible;

         SCIP_CALL( SCIPchgVarType(scip, consdata->slackvar, SCIP_VARTYPE_IMPLINT, &infeasible) );
         /* don't assert feasibility here because the presolver will and should detect a infeasibility */
      }
   }

   /* create transformed constraint with the same flags */
   (void) SCIPsnprintf(s, SCIP_MAXSTRLEN, "t_%s", SCIPconsGetName(sourcecons));
   SCIP_CALL( SCIPcreateCons(scip, targetcons, s, conshdlr, consdata,
         SCIPconsIsInitial(sourcecons), SCIPconsIsSeparated(sourcecons),
         SCIPconsIsEnforced(sourcecons), SCIPconsIsChecked(sourcecons),
         SCIPconsIsPropagated(sourcecons), SCIPconsIsLocal(sourcecons),
         SCIPconsIsModifiable(sourcecons), SCIPconsIsDynamic(sourcecons),
         SCIPconsIsRemovable(sourcecons), SCIPconsIsStickingAtNode(sourcecons)) );

   return SCIP_OKAY;
}


/** presolving initialization method of constraint handler (called when presolving is about to begin) */
static
SCIP_DECL_CONSINITPRE(consInitpreIndicator)
{  /*lint --e{715}*/
   int c;

   assert( scip != NULL );
   assert( conshdlr != NULL );
   assert( strcmp(SCIPconshdlrGetName(conshdlr), CONSHDLR_NAME) == 0 );

   /* check each constraint and get transformed linear constraint */
   for (c = 0; c < nconss; ++c)
   {
      SCIP_CONSDATA* consdata;

      assert( conss != NULL );
      assert( conss[c] != NULL );
      assert( SCIPconsIsTransformed(conss[c]) );

      consdata = SCIPconsGetData(conss[c]);
      assert( consdata != NULL );

      /* if not happend already, get transformed linear constraint */
      assert( consdata->lincons != NULL );
      assert( strcmp(SCIPconshdlrGetName(SCIPconsGetHdlr(consdata->lincons)), "linear") == 0 );

      /* in a restart the linear constraint might already be transformed */
      if ( ! SCIPconsIsTransformed(consdata->lincons) )
      {
         SCIP_CONS* translincons;

         SCIP_CALL( SCIPgetTransformedCons(scip, consdata->lincons, &translincons) );
         assert( translincons != NULL );
         SCIP_CALL( SCIPcaptureCons(scip, translincons) );
         consdata->lincons = translincons;
      }
   }

   return SCIP_OKAY;
}


/** presolving method of constraint handler 
 *
 *  For an indicator constraint with binary variable \f$y\f$ and slack variable \f$s\f$ the coupling
 *  inequality \f$s \le M (1-y)\f$ (equivalently: \f$s + M y \le M\f$) is inserted, where \f$M\f$ is
 *  an upper bound on the value of \f$s\f$. If \f$M\f$ is too large the inequality is not
 *  inserted. Depending on the parameter @a addCouplingCons we add a variable upper bound or a row
 *  (in consInitlpIndicator()).
 *
 *  @warning We can never delete linear constraints, because we need them to get the right values
 *  for the slack variables!
 */
static
SCIP_DECL_CONSPRESOL(consPresolIndicator)
{  /*lint --e{715}*/
   SCIP_CONSHDLRDATA* conshdlrdata;
   SCIP_EVENTHDLR* eventhdlr;
   SCIP_Bool noReductions;
   int oldnfixedvars;
   int oldndelconss;
   int removedvars = 0;
   int c;

   assert( scip != NULL );
   assert( conshdlr != NULL );
   assert( strcmp(SCIPconshdlrGetName(conshdlr), CONSHDLR_NAME) == 0 );
   assert( result != NULL );

   *result = SCIP_DIDNOTRUN;
   oldnfixedvars = *nfixedvars;
   oldndelconss = *ndelconss;

   /* get constraint handler data */
   conshdlrdata = SCIPconshdlrGetData(conshdlr);
   assert( conshdlrdata != NULL );
   eventhdlr = conshdlrdata->eventhdlr;
   assert( eventhdlr != NULL );

   SCIPdebugMessage("Presolving indicator constraints.\n");

   /* check each constraint */
   for (c = 0; c < nconss; ++c)
   {
      SCIP_CONS* cons;
      SCIP_CONSDATA* consdata;

      assert( conss != NULL );
      assert( conss[c] != NULL );
      cons = conss[c];
      consdata = SCIPconsGetData(cons);
      assert( consdata != NULL );
      assert( consdata->binvar != NULL );
      assert( ! SCIPconsIsModifiable(cons) );

      /* SCIPdebugMessage("Presolving indicator constraint <%s>.\n", SCIPconsGetName(cons) ); */
      *result = SCIP_DIDNOTFIND;

      /* do nothing if the linear constraint is not active */
      if ( ! consdata->linconsActive )
         continue;

      assert( consdata->lincons != NULL );
      assert( consdata->slackvar != NULL );
      assert( strcmp(SCIPconshdlrGetName(SCIPconsGetHdlr(consdata->lincons)), "linear") == 0 );
      assert( SCIPconsIsTransformed(consdata->lincons) );

      /* only run if success is possible */
      if ( nrounds == 0 || nnewfixedvars > 0 || nnewchgbds > 0 || nnewaggrvars > 0 || *nfixedvars > oldnfixedvars )
      {
         SCIP_Bool infeasible;
         SCIP_Bool fixed;

         /* if the binary variable is fixed to nonzero */
         if ( SCIPvarGetLbLocal(consdata->binvar) > 0.5 )
         {
            SCIPdebugMessage("Presolving <%s>: Binary variable fixed to 1.\n", SCIPconsGetName(cons));

            /* if slack variable is fixed to nonzero, we are infeasible */
            if ( SCIPisFeasPositive(scip, SCIPvarGetLbLocal(consdata->slackvar)) )
            {
               SCIPdebugMessage("The problem is infeasible: binary and slack variable are fixed to be nonzero.\n");
               *result = SCIP_CUTOFF;
               return SCIP_OKAY;
            }

            /* otherwise fix slack variable to 0 */
            SCIPdebugMessage("Fix slack variable to 0 and delete constraint.\n");
            SCIP_CALL( SCIPfixVar(scip, consdata->slackvar, 0.0, &infeasible, &fixed) );
            assert( ! infeasible );
            if ( fixed )
               ++(*nfixedvars);

            /* delete indicator constraint (leave linear constraint) */
            assert( ! SCIPconsIsModifiable(cons) );
            SCIP_CALL( SCIPdelCons(scip, cons) );
            ++(*ndelconss);
            *result = SCIP_SUCCESS;
            continue;
         }

         /* if the binary variable is fixed to zero */
         if ( SCIPvarGetUbLocal(consdata->binvar) < 0.5 )
         {
            SCIPdebugMessage("Presolving <%s>: Binary variable fixed to 0, deleting indicator and linear constraints.\n", 
               SCIPconsGetName(cons));

            /* delete indicator constraint */
            assert( ! SCIPconsIsModifiable(cons) );
            SCIP_CALL( SCIPdelCons(scip, cons) );
            ++(*ndelconss);

            *result = SCIP_SUCCESS;
            continue;
         }

         /* if the slack variable is fixed to nonzero */
         if ( SCIPisFeasPositive(scip, SCIPvarGetLbLocal(consdata->slackvar)) )
         {
            SCIPdebugMessage("Presolving <%s>: Slack variable fixed to nonzero.\n", SCIPconsGetName(cons));

            /* if binary variable is fixed to nonzero, we are infeasible */
            if ( SCIPvarGetLbLocal(consdata->binvar) > 0.5 )
            {
               SCIPdebugMessage("The problem is infeasible: binary and slack variable are fixed to be nonzero.\n");
               *result = SCIP_CUTOFF;
               return SCIP_OKAY;
            }

            /* otherwise fix binary variable to 0 */
            SCIPdebugMessage("Fix binary variable to 0 and delete indicator constraint.\n");
            SCIP_CALL( SCIPfixVar(scip, consdata->binvar, 0.0, &infeasible, &fixed) );
            assert( ! infeasible );
            if ( fixed )
               ++(*nfixedvars);

            /* delete constraint */
            assert( ! SCIPconsIsModifiable(cons) );
            SCIP_CALL( SCIPdelCons(scip, cons) );
            ++(*ndelconss);
            *result = SCIP_SUCCESS;
            continue;
         }

         /* if the slack variable is fixed to zero */
         if ( SCIPisFeasZero(scip, SCIPvarGetUbLocal(consdata->slackvar)) )
         {
            SCIPdebugMessage("Presolving <%s>: Slack variable fixed to zero, delete redundant indicator constraint.\n", 
               SCIPconsGetName(cons));

            /* delete constraint */
            assert( ! SCIPconsIsModifiable(cons) );
            SCIP_CALL( SCIPdelCons(scip, cons) );
            ++(*ndelconss);
            *result = SCIP_SUCCESS;
            continue;
         }

         /* Note that because of possible mulit-aggregation we cannot simply remove the indicator
          * constraint if the linear constraint is not active or disabled - see the note in @ref
          * PREPROC.
          */
      }
   }

   /* determine whether other methods have found reductions */
   noReductions = nnewfixedvars == 0 && nnewaggrvars == 0 && nnewchgvartypes == 0 && nnewchgbds == 0
      && nnewdelconss == 0 && nnewchgcoefs == 0 && nnewchgsides == 0;

   /* add variable upper bounds after bounds are likely to be strengthend */
   if ( noReductions && *result != SCIP_SUCCESS && conshdlrdata->addCouplingCons && ! conshdlrdata->addedCouplingCons )
   {
      int ngen;

      /* create variable upper bounds, possibly removing indicator constraints */
      SCIP_CALL( createVarUbs(scip, conshdlrdata, conss, nconss, &ngen) );

      if ( ngen > 0 )
      {
         *result = SCIP_SUCCESS;
         *nupgdconss += ngen;
         if ( conshdlrdata->removeIndicators )
            *ndelconss += ngen;
      }
      conshdlrdata->addedCouplingCons = TRUE;
   }

   SCIPdebugMessage("Presolved %d constraints (fixed %d variables, removed %d variables, and deleted %d constraints).\n",
      nconss, *nfixedvars - oldnfixedvars, removedvars, *ndelconss - oldndelconss);

   return SCIP_OKAY;
}


/** presolving deinitialization method of constraint handler (called after presolving has been finished) */
static
SCIP_DECL_CONSEXITPRE(consExitpreIndicator)
{  /*lint --e{715}*/
   SCIP_CONSHDLRDATA* conshdlrdata;
   int c;

   assert( scip != NULL );
   assert( conshdlr != NULL );
   assert( strcmp(SCIPconshdlrGetName(conshdlr), CONSHDLR_NAME) == 0 );
   assert( result != NULL );

   *result = SCIP_FEASIBLE;
   SCIPdebugMessage("Exitpre method for indicator constraints.\n");

   /* get constraint handler data */
   conshdlrdata = SCIPconshdlrGetData(conshdlr);
   assert( conshdlrdata != NULL );

   /* if variable upper bounds should be added, but have not yet been */
   if ( conshdlrdata->addCouplingCons && ! conshdlrdata->addedCouplingCons )
   {
      int ngen;

      /* create variable upper bounds, possibly removing indicator constraints */
      SCIP_CALL( createVarUbs(scip, conshdlrdata, conss, nconss, &ngen) );
      conshdlrdata->addedCouplingCons = TRUE;
   }

   /* add implications */
   for (c = 0; c < nconss; ++c)
   {
      SCIP_CONSDATA* consdata;
      SCIP_Bool infeasible;
      int nbdchgs;

      consdata = SCIPconsGetData(conss[c]);
      assert( consdata != NULL );
      
      if ( ! consdata->linconsActive )
         continue;
      
      /* add implications */
      SCIP_CALL( SCIPaddVarImplication(scip, consdata->binvar, TRUE, consdata->slackvar, SCIP_BOUNDTYPE_UPPER, 0.0, &infeasible, &nbdchgs) );
      
      /* infeasible might be true if preprocessing was truncated */
      if ( infeasible )
      {
         *result = SCIP_CUTOFF;
         break;
      }
      /* note: nbdchgs == 0 is not necessarily true, because preprocessing might be truncated. */
   }

   return SCIP_OKAY;
}


/** LP initialization method of constraint handler
 *
 *  For an indicator constraint with binary variable \f$y\f$ and slack variable \f$s\f$ the coupling
 *  inequality \f$s \le M (1-y)\f$ (equivalently: \f$s + M y \le M\f$) is inserted, where \f$M\f$ is
 *  an upper bound on the value of \f$s\f$. If \f$M\f$ is too large the inequality is not inserted.
 */
static
SCIP_DECL_CONSINITLP(consInitlpIndicator)
{
   int c;
   SCIP_CONSHDLRDATA* conshdlrdata;

   assert( scip != NULL );
   assert( conshdlr != NULL );
   assert( strcmp(SCIPconshdlrGetName(conshdlr), CONSHDLR_NAME) == 0 );

   conshdlrdata = SCIPconshdlrGetData(conshdlr);
   assert( conshdlrdata != NULL );

   /* SCIPdebugMessage("Checking for initial rows for indicator constraints <%s>.\n", SCIPconshdlrGetName(conshdlr) ); */

   /* check each constraint */
   for (c = 0; c < nconss; ++c)
   {
      SCIP_CONSDATA* consdata;

      assert( conss != NULL );
      assert( conss[c] != NULL );
      consdata = SCIPconsGetData(conss[c]);
      assert( consdata != NULL );

      /* add coupling if required */
      if ( conshdlrdata->addCoupling && consdata->linconsActive )
      {
         SCIP_Real ub;

         /* get upper bound for slack variable in linear constraint */
         ub = SCIPvarGetUbGlobal(consdata->slackvar);
         assert( ! SCIPisNegative(scip, ub) );

         /* insert corresponding row if helpful and coefficient is not too large */
         if ( ub <= conshdlrdata->maxCouplingValue )
         {
            char name[50];

#ifndef NDEBUG
            (void) SCIPsnprintf(name, 50, "couple%d", c);
#else
            name[0] = '\0';
#endif

            /* add variable upper bound if required */
            if ( conshdlrdata->addCouplingCons && ! conshdlrdata->addedCouplingCons )
            {
               SCIP_CONS* cons;

               SCIPdebugMessage("Insert coupling varbound constraint for indicator constraint <%s> (coeff: %f).\n", SCIPconsGetName(conss[c]), ub);

               SCIP_CALL( SCIPcreateConsVarbound(scip, &cons, name, consdata->slackvar, consdata->binvar, ub, -SCIPinfinity(scip), ub,
                     TRUE, TRUE, TRUE, FALSE, TRUE, FALSE, FALSE, TRUE, TRUE, FALSE) );

               SCIP_CALL( SCIPaddCons(scip, cons) );
               SCIP_CALL( SCIPreleaseCons(scip, &cons) );
            }
            else
            {
               SCIP_ROW* row;

               SCIP_CALL( SCIPcreateEmptyRow(scip, &row, name, -SCIPinfinity(scip), ub, FALSE, FALSE, FALSE) );
               SCIP_CALL( SCIPcacheRowExtensions(scip, row) );

               SCIP_CALL( SCIPaddVarToRow(scip, row, consdata->slackvar, 1.0) );
               SCIP_CALL( SCIPaddVarToRow(scip, row, consdata->binvar, ub) );
               SCIP_CALL( SCIPflushRowExtensions(scip, row) );

               SCIPdebugMessage("Insert coupling inequality for indicator constraint <%s> (coeff: %f).\n", SCIPconsGetName(conss[c]), ub);
#ifdef SCIP_OUTPUT
               SCIProwPrint(row, NULL);
#endif
               SCIP_CALL( SCIPaddCut(scip, NULL, row, FALSE) );

               SCIP_CALL( SCIPaddPoolCut(scip, row) );
               SCIP_CALL( SCIPreleaseRow(scip, &row));
            }
         }
      }
   }

   return SCIP_OKAY;
}


/** separation method of constraint handler for LP solutions */
static
SCIP_DECL_CONSSEPALP(consSepalpIndicator)
{  /*lint --e{715}*/
   SCIP_CONSHDLRDATA* conshdlrdata;

   assert( scip != NULL );
   assert( conshdlr != NULL );
   assert( conss != NULL );
   assert( strcmp(SCIPconshdlrGetName(conshdlr), CONSHDLR_NAME) == 0 );
   assert( result != NULL );

   *result = SCIP_DIDNOTRUN;

   conshdlrdata = SCIPconshdlrGetData(conshdlr);
   assert( conshdlrdata != NULL );

   if ( conshdlrdata->sepaAlternativeLP && nconss > 0 )
   {
      int nGen;

      nGen = 0;

      SCIPdebugMessage("Separating inequalities for indicator constraints.\n");

      *result = SCIP_DIDNOTFIND;

      /* start separation */
      SCIP_CALL( separateIISRounding(scip, conshdlr, NULL, nconss, conss, &nGen) );
      SCIPdebugMessage("Separated %d cuts from indicator constraints.\n", nGen);

      if ( nGen > 0 )
      {
         if ( conshdlrdata->genLogicor )
            *result = SCIP_CONSADDED;
         else
            *result = SCIP_SEPARATED;
      }
   }

   return SCIP_OKAY;
}


/** separation method of constraint handler for arbitrary primal solutions */
static
SCIP_DECL_CONSSEPASOL(consSepasolIndicator)
{  /*lint --e{715}*/
   SCIP_CONSHDLRDATA* conshdlrdata;

   assert( scip != NULL );
   assert( conshdlr != NULL );
   assert( conss != NULL );
   assert( strcmp(SCIPconshdlrGetName(conshdlr), CONSHDLR_NAME) == 0 );
   assert( result != NULL );

   conshdlrdata = SCIPconshdlrGetData(conshdlr);
   assert( conshdlrdata != NULL );

   if ( conshdlrdata->sepaAlternativeLP && nconss > 0 )
   {
      int nGen;

      nGen = 0;

      SCIPdebugMessage("Separating inequalities for indicator constraints.\n");

      *result = SCIP_DIDNOTFIND;
      /* start separation */
      SCIP_CALL( separateIISRounding(scip, conshdlr, sol, nconss, conss, &nGen) );
      SCIPdebugMessage("Separated %d cuts from indicator constraints.\n", nGen);

      if ( nGen > 0 )
      {
         if ( conshdlrdata->genLogicor )
            *result = SCIP_CONSADDED;
         else
            *result = SCIP_SEPARATED;
      }
   }

   return SCIP_OKAY;
}


/** constraint enforcing method of constraint handler for LP solutions */
static
SCIP_DECL_CONSENFOLP(consEnfolpIndicator)
{  /*lint --e{715}*/
   SCIP_CONSHDLRDATA* conshdlrdata;

   assert( scip != NULL );
   assert( conshdlr != NULL );
   assert( conss != NULL );
   assert( strcmp(SCIPconshdlrGetName(conshdlr), CONSHDLR_NAME) == 0 );
   assert( result != NULL );

   if ( solinfeasible )
   {
      *result = SCIP_FEASIBLE;
      return SCIP_OKAY;
   }

   /* get constraint handler data */
   conshdlrdata = SCIPconshdlrGetData(conshdlr);
   assert( conshdlrdata != NULL );

   SCIP_CALL( enforceIndicators(scip, conshdlr, nconss, conss, conshdlrdata->genLogicor, result) );

   return SCIP_OKAY;
}


/** constraint enforcing method of constraint handler for pseudo solutions */
static
SCIP_DECL_CONSENFOPS(consEnfopsIndicator)
{  /*lint --e{715}*/
   assert( scip != NULL );
   assert( conshdlr != NULL );
   assert( conss != NULL );
   assert( strcmp(SCIPconshdlrGetName(conshdlr), CONSHDLR_NAME) == 0 );
   assert( result != NULL );

   if ( solinfeasible )
   {
      *result = SCIP_FEASIBLE;
      return SCIP_OKAY;
   }

   if ( objinfeasible )
   {
      *result = SCIP_DIDNOTRUN;
      return SCIP_OKAY;
   }

   SCIP_CALL( enforceIndicators(scip, conshdlr, nconss, conss, TRUE, result) );

   return SCIP_OKAY;
}


/** feasibility check method of constraint handler for integral solutions */
static
SCIP_DECL_CONSCHECK(consCheckIndicator)
{  /*lint --e{715}*/
   SCIP_SOL* trysol = NULL;
   SCIP_CONSHDLRDATA* conshdlrdata;
   SCIP_Bool someLinconsNotActive;
   SCIP_Bool changedSol;
   int c;

   assert( scip != NULL );
   assert( conshdlr != NULL );
   assert( conss != NULL );
   assert( strcmp(SCIPconshdlrGetName(conshdlr), CONSHDLR_NAME) == 0 );
   assert( result != NULL );

   SCIPdebugMessage("Checking %d indicator constraints <%s>.\n", nconss, SCIPconshdlrGetName(conshdlr) );

   conshdlrdata = SCIPconshdlrGetData(conshdlr);
   assert( conshdlrdata != NULL );

   /* copy solution if it makes sense */
   if ( SCIPgetStage(scip) < SCIP_STAGE_SOLVED && conshdlrdata->trySolutions && conshdlrdata->heurTrySol != NULL )
   {
      SCIP_CALL( SCIPcreateSolCopy(scip, &trysol, sol) );
      assert( trysol != NULL );
      SCIP_CALL( SCIPunlinkSol(scip, trysol) );
   }

   /* check each constraint */
   *result = SCIP_FEASIBLE;
   changedSol = FALSE;
   someLinconsNotActive = FALSE;
   for (c = 0; c < nconss; ++c)
   {
      SCIP_CONSDATA* consdata;

      assert( conss[c] != NULL );
      consdata = SCIPconsGetData(conss[c]);
      assert( consdata != NULL );
      assert( consdata->binvar != NULL );

      /* if the linear constraint has not been generated, we do nothing */
      if ( ! consdata->linconsActive )
      {
         someLinconsNotActive = TRUE;
         continue;
      }

      assert( consdata->slackvar != NULL );
      /* if printreason is true it can happen that non-integral solutions are checked */
      assert( checkintegrality || SCIPisFeasIntegral(scip, SCIPgetSolVal(scip, sol, consdata->binvar)) );

      if ( ! SCIPisFeasZero(scip, SCIPgetSolVal(scip, sol, consdata->binvar)) &&
           ! SCIPisFeasZero(scip, SCIPgetSolVal(scip, sol, consdata->slackvar)) )
      {
         SCIP_CALL( SCIPresetConsAge(scip, conss[c]) );
         *result = SCIP_INFEASIBLE;

         if ( printreason )
         {
            SCIP_CALL( SCIPprintCons(scip, conss[c], NULL) );
            SCIPinfoMessage(scip, NULL, "violation:  <%s> = %g and <%s> = %.15g\n",
               SCIPvarGetName(consdata->binvar), SCIPgetSolVal(scip, sol, consdata->binvar),
               SCIPvarGetName(consdata->slackvar), SCIPgetSolVal(scip, sol, consdata->slackvar));
         }

         /* try to make solution feasible if it makes sense - otherwise exit */
         if ( trysol != NULL )
         {
            SCIP_Bool changed;
            SCIP_CALL( SCIPmakeIndicatorFeasible(scip, conss[c], trysol, &changed) );
            changedSol = changedSol || changed;
         }
         else
         {
            SCIPdebugMessage("Indicator constraints are not feasible.\n");
            return SCIP_OKAY;
         }
      }
      else
      {
         if ( trysol != NULL )
         {
            SCIP_Bool changed;
            SCIP_CALL( SCIPmakeIndicatorFeasible(scip, conss[c], trysol, &changed) );
            changedSol = changedSol || changed;
         }
      }
   }

   /* if some linear constraints are not active, we need to check feasibility via the alternative polyhedron */
   if ( someLinconsNotActive )
   {
      SCIP_LPI* lp;
      SCIP_Bool infeasible;
      SCIP_Bool error;
      SCIP_Bool* S;

      lp = conshdlrdata->altLP;
      assert( conshdlrdata->sepaAlternativeLP );

      /* the check maybe called before we have build the alternativ polyhedron -> return SCIP_INFEASIBLE */
      if ( lp != NULL )
      {
#ifndef NDEBUG
         SCIP_CALL( checkLPBoundsClean(scip, lp, nconss, conss) );
#endif

         /* change coefficients of bounds in alternative LP */
         if ( conshdlrdata->updateBounds )
            SCIP_CALL( updateFirstRowGlobal(scip, conshdlrdata) );

         /* scale first row if necessary */
         SCIP_CALL( scaleFirstRow(scip, conshdlrdata) );

         /* set obj. func. to current solution */
         SCIP_CALL( setAltLPObjZero(scip, lp, nconss, conss) );

         SCIP_CALL( SCIPallocBufferArray(scip, &S, nconss) );

         /* set up variables fixed to 1 */
         for (c = 0; c < nconss; ++c)
         {
            SCIP_CONSDATA* consdata;

            assert( conss[c] != NULL );
            consdata = SCIPconsGetData(conss[c]);
            assert( consdata != NULL );

            /* if printreason is true it can happen that non-integral solutions are checked */
            assert( checkintegrality || SCIPisFeasIntegral(scip, SCIPgetSolVal(scip, sol, consdata->binvar)) );
            if ( SCIPisFeasZero(scip, SCIPgetSolVal(scip, sol, consdata->binvar)) )
               S[c] = TRUE;
            else
               S[c] = FALSE;
         }

         /* fix the variables in S */
         SCIP_CALL( fixAltLPVariables(scip, lp, nconss, conss, S) );

         /* check feasibility */
         SCIP_CALL_PARAM( SCIPlpiSetIntpar(lp, SCIP_LPPAR_FROMSCRATCH, TRUE) );
         SCIP_CALL( checkAltLPInfeasible(scip, lp, conshdlrdata->maxConditionAltLP, TRUE, &infeasible, &error) );
         SCIP_CALL_PARAM( SCIPlpiSetIntpar(lp, SCIP_LPPAR_FROMSCRATCH, FALSE) );

         if ( error )
            return SCIP_LPERROR;

         if ( ! infeasible )
            *result = SCIP_INFEASIBLE;

         /* reset bounds */
         SCIP_CALL( unfixAltLPVariables(scip, lp, nconss, conss, S) );

#ifndef NDEBUG
         SCIP_CALL( checkLPBoundsClean(scip, lp, nconss, conss) );
#endif

         SCIPfreeBufferArray(scip, &S);
      }
      else
         *result = SCIP_INFEASIBLE;
   }
   else
   {
      /* tell heur_trysol about solution - it will pass it to SCIP */
      if ( trysol != NULL && changedSol )
      {
         assert(conshdlrdata->heurTrySol != NULL);
         SCIP_CALL( SCIPheurPassSolTrySol(scip, conshdlrdata->heurTrySol, trysol) );
      }
   }

   if ( trysol != NULL )
      SCIP_CALL( SCIPfreeSol(scip, &trysol) );

   if ( *result == SCIP_INFEASIBLE )
   {
      SCIPdebugMessage("Indicator constraints are not feasible.\n");
      return SCIP_OKAY;
   }

   /* at this point we are feasible */
   SCIPdebugMessage("Indicator constraints are feasible.\n");

   return SCIP_OKAY;
}


/** domain propagation method of constraint handler */
static
SCIP_DECL_CONSPROP(consPropIndicator)
{  /*lint --e{715}*/
   int c;
   int nGen = 0;

   assert( scip != NULL );
   assert( conshdlr != NULL );
   assert( conss != NULL );
   assert( strcmp(SCIPconshdlrGetName(conshdlr), CONSHDLR_NAME) == 0 );
   assert( result != NULL );
   *result = SCIP_DIDNOTRUN;

   assert( SCIPisTransformed(scip) );

   SCIPdebugMessage("Start propagation of constraint handler <%s>.\n", SCIPconshdlrGetName(conshdlr));

   /* check each constraint */
   for (c = 0; c < nconss; ++c)
   {
      SCIP_CONS* cons;
      SCIP_CONSDATA* consdata;
      SCIP_Bool cutoff;

      *result = SCIP_DIDNOTFIND;
      assert( conss[c] != NULL );
      cons = conss[c];
      consdata = SCIPconsGetData(cons);
      assert( consdata != NULL );
      /* SCIPdebugMessage("Propagating indicator constraint <%s>.\n", SCIPconsGetName(cons) ); */

      *result = SCIP_DIDNOTFIND;
      SCIP_CALL( propIndicator(scip, cons, consdata, &cutoff, &nGen) );
      if ( cutoff )
      {
         *result = SCIP_CUTOFF;
         return SCIP_OKAY;
      }
   }
   SCIPdebugMessage("Propagated %d domains in constraint handler <%s>.\n", nGen, SCIPconshdlrGetName(conshdlr));
   if ( nGen > 0 )
      *result = SCIP_REDUCEDDOM;

   return SCIP_OKAY;
}


/** propagation conflict resolving method of constraint handler
 *
 *  We check which bound changes were the reason for infeasibility. We use that @a inferinfo is 0 if
 *  the binary variable has bounds that fix it to be nonzero (these bounds are the reason). Likewise
 *  @a inferinfo is 1 if the slack variable has bounds that fix it to be nonzero.
 */
static
SCIP_DECL_CONSRESPROP(consRespropIndicator)
{  /*lint --e{715}*/
   SCIP_CONSDATA* consdata;

   assert( scip != NULL );
   assert( cons != NULL );
   assert( strcmp(SCIPconshdlrGetName(conshdlr), CONSHDLR_NAME) == 0 );
   assert( infervar != NULL );
   assert( bdchgidx != NULL );
   assert( result != NULL );

   *result = SCIP_DIDNOTFIND;
   SCIPdebugMessage("Propagation resolution method of indicator constraint <%s>.\n", SCIPconsGetName(cons));

   consdata = SCIPconsGetData(cons);
   assert( consdata != NULL );
   assert( inferinfo == 0 || inferinfo == 1 );
   assert( consdata->linconsActive );

   /* if the binary variable was the reason */
   if ( inferinfo == 0 )
   {
      assert( SCIPvarGetLbAtIndex(consdata->binvar, bdchgidx, FALSE) > 0.5 );
      assert( infervar != consdata->binvar );

      SCIP_CALL( SCIPaddConflictLb(scip, consdata->binvar, bdchgidx) );
      *result = SCIP_SUCCESS;
   }
   else
   {
      /* if the slack variable was the reason */
      assert( inferinfo == 1 );
      assert( SCIPisFeasPositive(scip, SCIPvarGetLbAtIndex(consdata->slackvar, bdchgidx, FALSE)) );
      assert( infervar != consdata->slackvar );

      SCIP_CALL( SCIPaddConflictLb(scip, consdata->slackvar, bdchgidx) );
      *result = SCIP_SUCCESS;
   }

   return SCIP_OKAY;
}


/** variable rounding lock method of constraint handler
 *
 *  The up-rounding of the binary and slack variable may violate the constraint. If the linear
 *  constraint is not active, we lock all variables in the depending constraint - otherwise they
 *  will be fixed by dual presolving methods.
 */
static
SCIP_DECL_CONSLOCK(consLockIndicator)
{
   SCIP_CONSDATA* consdata;

   assert( scip != NULL );
   assert( conshdlr != NULL );
   assert( cons != NULL );
   assert( strcmp(SCIPconshdlrGetName(conshdlr), CONSHDLR_NAME) == 0 );
   consdata = SCIPconsGetData(cons);
   assert( consdata != NULL );
   assert( consdata->binvar != NULL );

   SCIPdebugMessage("%socking constraint <%s>.\n", (nlocksneg < 0) || (nlockspos < 0) ? "Unl" : "L", SCIPconsGetName(cons));

   SCIP_CALL( SCIPaddVarLocks(scip, consdata->binvar, nlocksneg, nlockspos) );

   if ( consdata->linconsActive )
   {
      assert( consdata->slackvar != NULL );
      SCIP_CALL( SCIPaddVarLocks(scip, consdata->slackvar, nlocksneg, nlockspos) );
   }
   else
   {
      int i;
      SCIP_VAR** vars;
      int nvars;
      assert( consdata->lincons != NULL );

      nvars = SCIPgetNVarsLinear(scip, consdata->lincons);
      vars = SCIPgetVarsLinear(scip, consdata->lincons);
      for (i = 0; i < nvars; ++i)
         SCIP_CALL( SCIPaddVarLocks(scip, vars[i], nlockspos+nlocksneg, nlocksneg+nlockspos) );
   }

   return SCIP_OKAY;
}


/** constraint display method of constraint handler */
static
SCIP_DECL_CONSPRINT(consPrintIndicator)
{
   SCIP_CONSDATA* consdata;
   SCIP_VAR* binvar;
   int rhs;

   assert( scip != NULL );
   assert( conshdlr != NULL );
   assert( cons != NULL );
   assert( strcmp(SCIPconshdlrGetName(conshdlr), CONSHDLR_NAME) == 0 );

   consdata = SCIPconsGetData(cons);
   assert( consdata != NULL );
   assert( consdata->binvar != NULL );

   binvar = consdata->binvar;
   rhs = 1;
   if ( SCIPvarGetStatus(binvar) == SCIP_VARSTATUS_NEGATED )
   {
      rhs = 0;
      binvar = SCIPvarGetNegatedVar(binvar);
   }
   SCIPinfoMessage(scip, file, "<%s> = %d", SCIPvarGetName(binvar), rhs);

   assert( consdata->slackvar != NULL );
   assert( consdata->lincons != NULL );
   SCIPinfoMessage(scip, file, " -> <%s> = 0", SCIPvarGetName(consdata->slackvar));

   return SCIP_OKAY;
}

/** constraint copying method of constraint handler */
static
SCIP_DECL_CONSCOPY(consCopyIndicator)
{  /*lint --e{715}*/
   SCIP_CONSDATA* sourceconsdata;
   SCIP_CONS* targetlincons;
   SCIP_VAR* targetbinvar;
   SCIP_VAR* targetslackvar;
   SCIP_CONS* sourcelincons;
   SCIP_CONSHDLR* conshdlrlinear;
   const char* consname;

   assert( scip != NULL );
   assert( sourcescip != NULL );
   assert( sourcecons != NULL );
   assert( strcmp(SCIPconshdlrGetName(SCIPconsGetHdlr(sourcecons)), CONSHDLR_NAME) == 0 );

   *valid = TRUE;

   if( name != NULL )
      consname = name;
   else
      consname = SCIPconsGetName(sourcecons);

   SCIPdebugMessage("Copying indicator constraint <%s> ...\n", consname);

   sourceconsdata = SCIPconsGetData(sourcecons);
   assert( sourceconsdata != NULL );

   /* get linear constraint */
   sourcelincons = sourceconsdata->lincons;

   /* if the constraint has been deleted -> create empty constraint (multi-aggregation might still contain slackvariable, so indicator is valid) */
   if( SCIPconsIsDeleted(sourcelincons) )
   {
      SCIPdebugMessage("Linear constraint <%s> deleted! Create empty linear constraint.\n", SCIPconsGetName(sourceconsdata->lincons));

      SCIP_CALL( SCIPcreateConsLinear(scip, &targetlincons, "dummy", 0, NULL, NULL, 0.0, SCIPinfinity(scip),
            FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE) );
      SCIP_CALL( SCIPaddCons(scip, targetlincons) );
   }
   else
   {
      /* get copied version of linear constraint */
      assert( sourcelincons != NULL );
      conshdlrlinear = SCIPfindConshdlr(sourcescip, "linear");
      assert(conshdlrlinear != NULL);
      SCIP_CALL( SCIPgetConsCopy(sourcescip, scip, sourcelincons, &targetlincons, conshdlrlinear, varmap, consmap, SCIPconsGetName(sourcelincons),
            SCIPconsIsInitial(sourcelincons), SCIPconsIsSeparated(sourcelincons), SCIPconsIsEnforced(sourcelincons), SCIPconsIsChecked(sourcelincons),
            SCIPconsIsPropagated(sourcelincons), SCIPconsIsLocal(sourcelincons), SCIPconsIsModifiable(sourcelincons), SCIPconsIsDynamic(sourcelincons),
            SCIPconsIsRemovable(sourcelincons), SCIPconsIsStickingAtNode(sourcelincons), global, valid) );
   }

   /* find copied variable corresponding to binvar */
   if( *valid )
   {
      SCIP_VAR* sourcebinvar;
      
      sourcebinvar = sourceconsdata->binvar;
      assert(sourcebinvar != NULL);

      SCIP_CALL( SCIPgetVarCopy(sourcescip, scip, sourcebinvar, &targetbinvar, varmap, consmap, global, valid) );
   }

   /* find copied variable corresponding to slackvar */
   if( *valid )
   {
      SCIP_VAR* sourceslackvar;

      sourceslackvar = sourceconsdata->slackvar;
      assert(sourceslackvar != NULL);

      SCIP_CALL( SCIPgetVarCopy(sourcescip, scip, sourceslackvar, &targetslackvar, varmap, consmap, global, valid) );
   }

   /* create indicator constraint */
   if( *valid )
   {
      assert(targetlincons != NULL);
      assert(targetbinvar != NULL);
      assert(targetslackvar != NULL);

      SCIP_CALL( SCIPcreateConsIndicatorLinCons(scip, cons, consname, targetbinvar, targetlincons, targetslackvar,
            initial, separate, enforce, check, propagate, local, dynamic, modifiable, stickingatnode) );
   }

   if( !(*valid) )
   {
      SCIPverbMessage(scip, SCIP_VERBLEVEL_MINIMAL, NULL, "could not copy linear constraint <%s>\n", SCIPconsGetName(sourcelincons));
   }

   /* release empty constraint */
   if( SCIPconsIsDeleted(sourcelincons) )
   {
      SCIP_CALL( SCIPreleaseCons(scip, &targetlincons) );
   }   

   return SCIP_OKAY;
}


/** constraint parsing method of constraint handler */
static
SCIP_DECL_CONSPARSE(consParseIndicator)
{  /*lint --e{715}*/
   char binvarname[1024];
   char slackvarname[1024];
   SCIP_VAR* binvar;
   SCIP_VAR* slackvar;
   SCIP_CONS* lincons;
   char* posstr;
   int zeroone;
   int nargs;

   *success = TRUE;

   /* read indicator constraint */
   nargs = sscanf(str, " <%1023[^>]> = %d -> <%1023[^>]> = 0", binvarname, &zeroone, slackvarname);

   if ( nargs != 3 )
   {
      SCIPverbMessage(scip, SCIP_VERBLEVEL_MINIMAL, NULL, "Syntax error: expected the following form: <var> = [0|1] -> <var> = 0.\n%s\n", str);
      *success = FALSE;
      return SCIP_OKAY;
   }

   if ( zeroone != 0 && zeroone != 1 )
   {
      SCIPverbMessage(scip, SCIP_VERBLEVEL_MINIMAL, NULL, "Syntax error: expected the following form: <var> = [0|1] -> <var> = 0.\n%s\n", str);
      *success = FALSE;
      return SCIP_OKAY;
   }

   /* get binary variable */
   binvar = SCIPfindVar(scip, binvarname);
   if ( binvar == NULL )
   {
      SCIPverbMessage(scip, SCIP_VERBLEVEL_MINIMAL, NULL, "unknown variable <%s>\n", binvarname);
      *success = FALSE;
      return SCIP_OKAY;
   }
   /* check whether we need the complemented variable */
   if ( zeroone == 0 )
      SCIP_CALL( SCIPgetNegatedVar(scip, binvar, &binvar) );

   /* get slack variable */
   slackvar = SCIPfindVar(scip, slackvarname);
   if ( slackvar == NULL )
   {
      SCIPverbMessage(scip, SCIP_VERBLEVEL_MINIMAL, NULL, "unknown variable <%s>\n", slackvarname);
      *success = FALSE;
      return SCIP_OKAY;
   }

   /* find matching linear constraint */
   posstr = strstr(slackvarname, "indslack");
   if ( posstr == NULL )
   {
      SCIPverbMessage(scip, SCIP_VERBLEVEL_MINIMAL, NULL, "strange slack variable name: <%s>\n", binvarname);
      *success = FALSE;
      return SCIP_OKAY;
   }

   /* overwrite binvarname */
   (void) SCIPsnprintf(binvarname, 1023, "indlin%s", posstr+8);

   lincons = SCIPfindCons(scip, binvarname);
   if ( lincons == NULL )
   {
      SCIPverbMessage(scip, SCIP_VERBLEVEL_MINIMAL, NULL, "while parsing indicator constraint <%s>: unknown linear constraint <%s>\n", name, binvarname);
      *success = FALSE;
      return SCIP_OKAY;
   }

   /* create indicator constraint */
   SCIP_CALL( SCIPcreateConsIndicatorLinCons(scip, cons, name, binvar, lincons, slackvar,
         initial, separate, enforce, check, propagate, local, dynamic, removable, stickingatnode) );

   return SCIP_OKAY;
}


/** constraint enabling notification method of constraint handler */
static
SCIP_DECL_CONSENABLE(consEnableIndicator)
{
   SCIP_CONSHDLRDATA* conshdlrdata;
   SCIP_CONSDATA* consdata;

   assert( scip != NULL );
   assert( conshdlr != NULL );
   assert( cons != NULL );
   assert( strcmp(SCIPconshdlrGetName(conshdlr), CONSHDLR_NAME) == 0 );

   SCIPdebugMessage("Enabling constraint <%s>.\n", SCIPconsGetName(cons));

   conshdlrdata = SCIPconshdlrGetData(conshdlr);
   assert( conshdlrdata != NULL );

   consdata = SCIPconsGetData(cons);
   assert( consdata != NULL );

   if ( conshdlrdata->altLP != NULL )
   {
      assert( conshdlrdata->sepaAlternativeLP );

      if ( consdata->colIndex >= 0 )
      {
         SCIP_CALL( unfixAltLPVariable(conshdlrdata->altLP, consdata->colIndex) );
      }
   }

   return SCIP_OKAY;
}


/** constraint disabling notification method of constraint handler */
static
SCIP_DECL_CONSDISABLE(consDisableIndicator)
{
   SCIP_CONSHDLRDATA* conshdlrdata;

   assert( scip != NULL );
   assert( conshdlr != NULL );
   assert( cons != NULL );
   assert( strcmp(SCIPconshdlrGetName(conshdlr), CONSHDLR_NAME) == 0 );

   SCIPdebugMessage("Disabling constraint <%s>.\n", SCIPconsGetName(cons));

   conshdlrdata = SCIPconshdlrGetData(conshdlr);
   assert( conshdlrdata != NULL );

   if ( conshdlrdata->altLP != NULL )
   {
      SCIP_CONSDATA* consdata;
      consdata = SCIPconsGetData(cons);
      assert( consdata != NULL );
      assert( conshdlrdata->sepaAlternativeLP );

      if ( consdata->colIndex >= 0 )
      {
         SCIP_CALL( fixAltLPVariable(conshdlrdata->altLP, consdata->colIndex) );
      }
   }

   return SCIP_OKAY;
}


/** constraint activation notification method of constraint handler */
#define consActiveIndicator NULL

/** constraint deactivation notification method of constraint handler */
#define consDeactiveIndicator NULL

/** deinitialization method of constraint handler (called before transformed problem is freed) */
#define consExitIndicator NULL






/* ---------------- Callback methods of event handler ---------------- */

/* exec the event handler
 *
 * We update the number of variables fixed to be nonzero
 */
static
SCIP_DECL_EVENTEXEC(eventExecIndicator)
{
   SCIP_EVENTTYPE eventtype;
   SCIP_CONSDATA* consdata;
   SCIP_Real oldbound, newbound;

   assert( eventhdlr != NULL );
   assert( eventdata != NULL );
   assert( strcmp(SCIPeventhdlrGetName(eventhdlr), EVENTHDLR_NAME) == 0 );
   assert( event != NULL );

   consdata = (SCIP_CONSDATA*)eventdata;
   assert( consdata != NULL );
   assert( 0 <= consdata->nFixedNonzero && consdata->nFixedNonzero <= 2 );
   assert( consdata->linconsActive );

   oldbound = SCIPeventGetOldbound(event);
   newbound = SCIPeventGetNewbound(event);

   eventtype = SCIPeventGetType(event);
   switch ( eventtype )
   {
   case SCIP_EVENTTYPE_LBTIGHTENED:
      /* if variable is now fixed to be nonzero */
      if ( ! SCIPisFeasPositive(scip, oldbound) && SCIPisFeasPositive(scip, newbound) )
         ++(consdata->nFixedNonzero);
      SCIPdebugMessage("changed lower bound of variable <%s> from %g to %g (nFixedNonzero: %d).\n",
         SCIPvarGetName(SCIPeventGetVar(event)), oldbound, newbound, consdata->nFixedNonzero);
      break;
   case SCIP_EVENTTYPE_UBTIGHTENED:
      /* if variable is now fixed to be nonzero */
      if ( ! SCIPisFeasNegative(scip, oldbound) && SCIPisFeasNegative(scip, newbound) )
         ++(consdata->nFixedNonzero);
      SCIPdebugMessage("changed upper bound of variable <%s> from %g to %g (nFixedNonzero: %d).\n",
         SCIPvarGetName(SCIPeventGetVar(event)), oldbound, newbound, consdata->nFixedNonzero);
      break;
   case SCIP_EVENTTYPE_LBRELAXED:
      /* if variable is not fixed to be nonzero anymore */
      if ( SCIPisFeasPositive(scip, oldbound) && ! SCIPisFeasPositive(scip, newbound) )
         --(consdata->nFixedNonzero);
      SCIPdebugMessage("changed lower bound of variable <%s> from %g to %g (nFixedNonzero: %d).\n",
         SCIPvarGetName(SCIPeventGetVar(event)), oldbound, newbound, consdata->nFixedNonzero);
      break;
   case SCIP_EVENTTYPE_UBRELAXED:
      /* if variable is not fixed to be nonzero anymore */
      if ( SCIPisFeasNegative(scip, oldbound) && ! SCIPisFeasNegative(scip, newbound) )
         --(consdata->nFixedNonzero);
      SCIPdebugMessage("changed upper bound of variable <%s> from %g to %g (nFixedNonzero: %d).\n",
         SCIPvarGetName(SCIPeventGetVar(event)), oldbound, newbound, consdata->nFixedNonzero);
      break;
   default:
      SCIPerrorMessage("invalid event type.\n");
      return SCIP_INVALIDDATA;
   }
   assert( 0 <= consdata->nFixedNonzero && consdata->nFixedNonzero <= 2 );

   return SCIP_OKAY;
}




/* ---------------- Constraint specific interface methods ---------------- */

/** creates the handler for indicator constraints and includes it in SCIP */
SCIP_RETCODE SCIPincludeConshdlrIndicator(
   SCIP*                 scip                /**< SCIP data structure */
   )
{
   SCIP_CONSHDLRDATA* conshdlrdata;

   /* create event handler for bound change events */
   SCIP_CALL( SCIPincludeEventhdlr(scip, EVENTHDLR_NAME, EVENTHDLR_DESC,
         NULL, NULL, NULL, NULL, NULL, NULL, NULL, eventExecIndicator, NULL) );

   /* create constraint handler data */
   SCIP_CALL( SCIPallocMemory(scip, &conshdlrdata) );

   /* get event handler for bound change events */
   conshdlrdata->eventhdlr = SCIPfindEventhdlr(scip, EVENTHDLR_NAME);
   if ( conshdlrdata->eventhdlr == NULL )
   {
      SCIPerrorMessage("event handler for indicator constraints not found.\n");
      return SCIP_PLUGINNOTFOUND;
   }
   conshdlrdata->removable = TRUE;
   conshdlrdata->scaled = FALSE;
   conshdlrdata->altLP = NULL;
   conshdlrdata->nRows = 0;
   conshdlrdata->varHash = NULL;
   conshdlrdata->slackHash = NULL;
   conshdlrdata->lbHash = NULL;
   conshdlrdata->ubHash = NULL;
   conshdlrdata->nLbBounds = 0;
   conshdlrdata->nUbBounds = 0;
   conshdlrdata->nSlackVars = 0;
   conshdlrdata->roundingMinThres =     0.1;
   conshdlrdata->roundingMaxThres =     0.6;
   conshdlrdata->roundingRounds = 1;
   conshdlrdata->roundingOffset = 0.1;
   conshdlrdata->branchIndicators = TRUE;
   conshdlrdata->genLogicor = TRUE;
   conshdlrdata->sepaAlternativeLP = TRUE;
   conshdlrdata->addCoupling = FALSE;
   conshdlrdata->addCouplingCons = FALSE;
   conshdlrdata->heurTrySol = NULL;
   conshdlrdata->addedCouplingCons = FALSE;
   conshdlrdata->addLinCons = NULL;
   conshdlrdata->nAddLinCons = 0;
   conshdlrdata->maxAddLinCons = 0;
   conshdlrdata->generateBilinear = FALSE;

   /* include constraint handler */
   SCIP_CALL( SCIPincludeConshdlr(scip, CONSHDLR_NAME, CONSHDLR_DESC,
         CONSHDLR_SEPAPRIORITY, CONSHDLR_ENFOPRIORITY, CONSHDLR_CHECKPRIORITY,
         CONSHDLR_SEPAFREQ, CONSHDLR_PROPFREQ, CONSHDLR_EAGERFREQ, CONSHDLR_MAXPREROUNDS,
         CONSHDLR_DELAYSEPA, CONSHDLR_DELAYPROP, CONSHDLR_DELAYPRESOL, CONSHDLR_NEEDSCONS,
         conshdlrCopyIndicator,
         consFreeIndicator, consInitIndicator, consExitIndicator,
         consInitpreIndicator, consExitpreIndicator, consInitsolIndicator, consExitsolIndicator,
         consDeleteIndicator, consTransIndicator, consInitlpIndicator, consSepalpIndicator,
         consSepasolIndicator, consEnfolpIndicator, consEnfopsIndicator, consCheckIndicator,
         consPropIndicator, consPresolIndicator, consRespropIndicator, consLockIndicator,
         consActiveIndicator, consDeactiveIndicator, consEnableIndicator, consDisableIndicator,
         consPrintIndicator, consCopyIndicator, consParseIndicator, conshdlrdata) );

   /* add indicator constraint handler parameters */
   SCIP_CALL( SCIPaddBoolParam(scip,
         "constraints/indicator/branchIndicators",
         "Branch on indicator constraints in enforcing?",
         &conshdlrdata->branchIndicators, TRUE, DEFAULT_BRANCHINDICATORS, NULL, NULL) );

   SCIP_CALL( SCIPaddBoolParam(scip,
         "constraints/indicator/genLogicor",
         "Generate logicor constraints instead of cuts?",
         &conshdlrdata->genLogicor, TRUE, DEFAULT_GENLOGICOR, NULL, NULL) );

   SCIP_CALL( SCIPaddBoolParam(scip,
         "constraints/indicator/sepaAlternativeLP",
         "Separate using the alternative LP?",
         &conshdlrdata->sepaAlternativeLP, TRUE, DEFAULT_SEPAALTERNATIVELP, NULL, NULL) );

   SCIP_CALL( SCIPaddBoolParam(scip,
         "constraints/indicator/addCoupling",
         "add initial coupling inequalities",
         &conshdlrdata->addCoupling, TRUE, DEFAULT_ADDCOUPLING, NULL, NULL) );

   SCIP_CALL( SCIPaddBoolParam(scip,
         "constraints/indicator/addCouplingCons",
         "add initial coupling inequalities as linear constraints, if 'addCoupling' is true",
         &conshdlrdata->addCouplingCons, TRUE, DEFAULT_ADDCOUPLINGCONS, NULL, NULL) );

   SCIP_CALL( SCIPaddBoolParam(scip,
         "constraints/indicator/removeIndicators",
         "remove indicator constraint if corresponding variable bound constraint has been added?",
         &conshdlrdata->removeIndicators, TRUE, DEFAULT_REMOVEINDICATORS, NULL, NULL) );

   SCIP_CALL( SCIPaddBoolParam(scip,
         "constraints/indicator/updateBounds",
         "Update bounds of original variables for separation?",
         &conshdlrdata->updateBounds, TRUE, DEFAULT_UPDATEBOUNDS, NULL, NULL) );

   SCIP_CALL( SCIPaddBoolParam(scip,
         "constraints/indicator/trySolutions",
         "Try to make solutions feasible by setting indicator variables?",
         &conshdlrdata->trySolutions, TRUE, DEFAULT_TRYSOLUTIONS, NULL, NULL) );

   SCIP_CALL( SCIPaddBoolParam(scip,
         "constraints/indicator/noLinconsCont",
         "decompose problem - do not generate linear constraint if all variables are continuous",
         &conshdlrdata->noLinconsCont, TRUE, DEFAULT_NOLINCONSCONT, NULL, NULL) );

   SCIP_CALL( SCIPaddBoolParam(scip,
         "constraints/indicator/enforceCuts",
         "in enforcing try to generate cuts (only if sepaAlternativeLP is true)",
         &conshdlrdata->enforceCuts, TRUE, DEFAULT_ENFORCECUTS, NULL, NULL) );

   SCIP_CALL( SCIPaddRealParam(scip,
         "constraints/indicator/maxCouplingValue",
         "maximum coefficient for binary variable in coupling constraint",
         &conshdlrdata->maxCouplingValue, TRUE, DEFAULT_MAXCOUPLINGVALUE, 0.0, 1e9, NULL, NULL) );

   SCIP_CALL( SCIPaddRealParam(scip,
         "constraints/indicator/maxConditionAltLP",
         "maximum estimated condition of the solution basis matrix of the alternative LP to be trustworthy (0.0 to disable check)",
         &conshdlrdata->maxConditionAltLP, TRUE, DEFAULT_MAXCONDITIONALTLP, 0.0, SCIP_REAL_MAX, NULL, NULL) );

   SCIP_CALL( SCIPaddBoolParam(scip,
         "constraints/indicator/generateBilinear",
         "do not generate indicator constraint, but a bilinear constraint instead",
         &conshdlrdata->generateBilinear, TRUE, DEFAULT_GENERATEBILINEAR, NULL, NULL) );

   return SCIP_OKAY;
}


/** creates and captures an indicator constraint
 *
 *  Note: @a binvar is checked to be binary only later. This enables a change of the type in
 *  procedures reading an instance.
 */
SCIP_RETCODE SCIPcreateConsIndicator(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONS**           cons,               /**< pointer to hold the created constraint */
   const char*           name,               /**< name of constraint */
   SCIP_VAR*             binvar,             /**< binary indicator variable (or NULL) */
   int                   nvars,              /**< number of variables in the inequality */
   SCIP_VAR**            vars,               /**< array with variables of inequality (or NULL) */
   SCIP_Real*            vals,               /**< values of variables in inequality (or NULL) */
   SCIP_Real             rhs,                /**< rhs of the inequality */
   SCIP_Bool             initial,            /**< should the LP relaxation of constraint be in the initial LP? Usually set to TRUE. */
   SCIP_Bool             separate,           /**< should the constraint be separated during LP processing?
                                              *   Usually set to TRUE. */
   SCIP_Bool             enforce,            /**< should the constraint be enforced during node processing?
                                              *   TRUE for model constraints, FALSE for additional, redundant constraints. */
   SCIP_Bool             check,              /**< should the constraint be checked for feasibility?
                                              *   TRUE for model constraints, FALSE for additional, redundant constraints. */
   SCIP_Bool             propagate,          /**< should the constraint be propagated during node processing?
                                              *   Usually set to TRUE. */
   SCIP_Bool             local,              /**< is constraint only valid locally?
                                              *   Usually set to FALSE. Has to be set to TRUE, e.g., for branching constraints. */
   SCIP_Bool             dynamic,            /**< is constraint subject to aging?
                                              *   Usually set to FALSE. Set to TRUE for own cuts which
                                              *   are seperated as constraints. */
   SCIP_Bool             removable,          /**< should the relaxation be removed from the LP due to aging or cleanup?
                                              *   Usually set to FALSE. Set to TRUE for 'lazy constraints' and 'user cuts'. */
   SCIP_Bool             stickingatnode      /**< should the constraint always be kept at the node where it was added, even
                                              *   if it may be moved to a more global node?
                                              *   Usually set to FALSE. Set to TRUE to for constraints that represent node data. */
   )
{
   SCIP_CONSHDLR* conshdlr;
   SCIP_CONSHDLRDATA* conshdlrdata;
   SCIP_CONSDATA* consdata;
   SCIP_CONS* lincons;
   SCIP_VAR* slackvar;
   SCIP_Bool modifiable;
   SCIP_Bool linconsactive;
   SCIP_VARTYPE slackvartype;
   char s[SCIP_MAXSTRLEN];
   int i;

   if ( nvars < 0 )
   {
      SCIPerrorMessage("Indicator constraint <%s> needs nonnegative number of variables in linear constraint.\n", name);
      return SCIP_INVALIDDATA;
   }

   modifiable = FALSE;

   /* find the indicator constraint handler */
   conshdlr = SCIPfindConshdlr(scip, CONSHDLR_NAME);
   if ( conshdlr == NULL )
   {
      SCIPerrorMessage("<%s> constraint handler not found\n", CONSHDLR_NAME);
      return SCIP_PLUGINNOTFOUND;
   }

   conshdlrdata = SCIPconshdlrGetData(conshdlr);
   assert( conshdlrdata != NULL );

   if ( conshdlrdata->noLinconsCont && ! conshdlrdata->sepaAlternativeLP )
   {
      SCIPerrorMessage("constraint handler <%s>: need parameter <sepaAlternativeLP> to be true if parameter <noLinconsCont> is true.\n", CONSHDLR_NAME);
      return SCIP_INVALIDDATA;
   }

   if ( conshdlrdata->noLinconsCont && conshdlrdata->generateBilinear )
   {
      SCIPerrorMessage("constraint handler <%s>: parameters <noLinconsCont> and <generateBilinear> cannot both be true.\n", CONSHDLR_NAME);
      return SCIP_INVALIDDATA;
   }

   /* check if slack variable can be made implicity integer */
   slackvartype = SCIP_VARTYPE_IMPLINT;
   for (i = 0; i < nvars; ++i)
   {
      if ( ! SCIPvarIsIntegral(vars[i]) || ! SCIPisIntegral(scip, vals[i]) )
      {
         slackvartype = SCIP_VARTYPE_CONTINUOUS;
         break;
      }
   }

   /* create slack variable */
   (void) SCIPsnprintf(s, SCIP_MAXSTRLEN, "indslack_%s", name);
   SCIP_CALL( SCIPcreateVar(scip, &slackvar, s, 0.0, SCIPinfinity(scip), 0.0, slackvartype, TRUE, FALSE,
         NULL, NULL, NULL, NULL, NULL) );

   SCIP_CALL( SCIPaddVar(scip, slackvar) );

   /* mark slack variable not to be multi-aggregated */
   SCIP_CALL( SCIPmarkDoNotMultaggrVar(scip, slackvar) );

   /* if the problem should be decomposed if only non-integer variables are present */
   linconsactive = TRUE;
   if ( conshdlrdata->noLinconsCont )
   {
      SCIP_Bool onlyCont = TRUE;
      int v;

      assert( ! conshdlrdata->generateBilinear );

      /* check whether call variables are non-integer */
      for (v = 0; v < nvars; ++v)
      {
         SCIP_VARTYPE vartype;

         vartype = SCIPvarGetType(vars[v]);
         if ( vartype != SCIP_VARTYPE_CONTINUOUS && vartype != SCIP_VARTYPE_IMPLINT )
         {
            onlyCont = FALSE;
            break;
         }
      }

      if ( onlyCont )
         linconsactive = FALSE;
   }

   /* create linear constraint */
   (void) SCIPsnprintf(s, SCIP_MAXSTRLEN, "indlin_%s", name);

   /* if the linear constraint should be activated */
   if ( linconsactive )
   {
      /* the constraint is inital, enforced, separated, and checked */
      SCIP_CALL( SCIPcreateConsLinear(scip, &lincons, s, nvars, vars, vals, -SCIPinfinity(scip), rhs,
            initial, TRUE, TRUE, TRUE, TRUE, FALSE, FALSE, FALSE, FALSE, FALSE) );
   }
   else
   {
      /* the constraint is inital, enforced, separated, and checked */
      SCIP_CALL( SCIPcreateConsLinear(scip, &lincons, s, nvars, vars, vals, -SCIPinfinity(scip), rhs,
            FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE) );
   }

   /* mark linear constraint not to be upgraded - otherwise we loose control over it */
   SCIP_CALL( SCIPmarkDoNotUpgradeConsLinear(scip, lincons) );

   /* add slack variable */
   SCIP_CALL( SCIPaddCoefLinear(scip, lincons, slackvar, -1.0) );
   SCIP_CALL( SCIPaddCons(scip, lincons) );

   /* check whether we should generate a bilinear constraint instead of a indicator contraint */
   if ( conshdlrdata->generateBilinear )
   {
      SCIP_Real val;
   
      /* create a quadratic constraint with a single bilinear term - note cons is used */
      val = 1.0;
      SCIP_CALL( SCIPcreateConsQuadratic(scip, cons, name, 0, NULL, NULL, 1, &binvar, &slackvar, &val, 0.0, 0.0,
            TRUE, TRUE, TRUE, TRUE, TRUE, FALSE, FALSE, FALSE, FALSE) );
   }
   else
   {
      /* create constraint data */
      consdata = NULL;
      SCIP_CALL( consdataCreate(scip, conshdlr, name, &consdata, conshdlrdata->eventhdlr, 
            binvar, slackvar, lincons, linconsactive, conshdlrdata->sepaAlternativeLP) );
      assert( consdata != NULL );
      
      /* create constraint */
      SCIP_CALL( SCIPcreateCons(scip, cons, name, conshdlr, consdata, initial, separate, enforce, check, propagate,
            local, modifiable, dynamic, removable, stickingatnode) );
   }

   return SCIP_OKAY;
}


/** creates and captures an indicator constraint with given linear constraint and slack variable
 *
 *  Note: @a binvar is checked to be binary only later. This enables a change of the type in
 *  procedures reading an instance.
 *
 *  Note: we assume that @a slackvar actually appears in @a lincons and we also assume that it takes
 *  the role of a slackvariable!
 */
SCIP_RETCODE SCIPcreateConsIndicatorLinCons(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONS**           cons,               /**< pointer to hold the created constraint */
   const char*           name,               /**< name of constraint */
   SCIP_VAR*             binvar,             /**< binary indicator variable */
   SCIP_CONS*            lincons,            /**< linear constraint */
   SCIP_VAR*             slackvar,           /**< slack variable */
   SCIP_Bool             initial,            /**< should the LP relaxation of constraint be in the initial LP? Usually set to TRUE. */
   SCIP_Bool             separate,           /**< should the constraint be separated during LP processing?
                                              *   Usually set to TRUE. */
   SCIP_Bool             enforce,            /**< should the constraint be enforced during node processing?
                                              *   TRUE for model constraints, FALSE for additional, redundant constraints. */
   SCIP_Bool             check,              /**< should the constraint be checked for feasibility?
                                              *   TRUE for model constraints, FALSE for additional, redundant constraints. */
   SCIP_Bool             propagate,          /**< should the constraint be propagated during node processing?
                                              *   Usually set to TRUE. */
   SCIP_Bool             local,              /**< is constraint only valid locally?
                                              *   Usually set to FALSE. Has to be set to TRUE, e.g., for branching constraints. */
   SCIP_Bool             dynamic,            /**< is constraint subject to aging?
                                              *   Usually set to FALSE. Set to TRUE for own cuts which
                                              *   are seperated as constraints. */
   SCIP_Bool             removable,          /**< should the relaxation be removed from the LP due to aging or cleanup?
                                              *   Usually set to FALSE. Set to TRUE for 'lazy constraints' and 'user cuts'. */
   SCIP_Bool             stickingatnode      /**< should the constraint always be kept at the node where it was added, even
                                              *   if it may be moved to a more global node?
                                              *   Usually set to FALSE. Set to TRUE to for constraints that represent node data. */
   )
{
   SCIP_CONSHDLR* conshdlr;
   SCIP_CONSHDLRDATA* conshdlrdata;
   SCIP_CONSDATA* consdata;
   SCIP_Bool modifiable;
   SCIP_Bool linconsactive;

   assert( scip != NULL );
   assert( lincons != NULL );
   assert( slackvar != NULL );

   modifiable = FALSE;

   /* check whether lincons is really a linear constraint */
   conshdlr = SCIPconsGetHdlr(lincons);
   if ( strcmp(SCIPconshdlrGetName(conshdlr), "linear") != 0 )
   {
      SCIPerrorMessage("Lincons constraint is not linear.\n");
      return SCIP_INVALIDDATA;
   }

   /* find the indicator constraint handler */
   conshdlr = SCIPfindConshdlr(scip, CONSHDLR_NAME);
   if ( conshdlr == NULL )
   {
      SCIPerrorMessage("<%s> constraint handler not found.\n", CONSHDLR_NAME);
      return SCIP_PLUGINNOTFOUND;
   }

   conshdlrdata = SCIPconshdlrGetData(conshdlr);
   assert( conshdlrdata != NULL );

   if ( conshdlrdata->noLinconsCont && ! conshdlrdata->sepaAlternativeLP )
   {
      SCIPerrorMessage("constraint handler <%s>: need parameter <sepaAlternativeLP> to be true if parameter <noLinconsCont> is true.\n", CONSHDLR_NAME);
      return SCIP_INVALIDDATA;
   }

   /* mark slack variable not to be multi-aggregated */
   SCIP_CALL( SCIPmarkDoNotMultaggrVar(scip, slackvar) );

   /* capture slack variable and linear constraint */
   SCIP_CALL( SCIPcaptureVar(scip, slackvar) );
   SCIP_CALL( SCIPcaptureCons(scip, lincons) );

   /* if the problem should be decomposed if only non-integer variables are present */
   linconsactive = TRUE;
   if ( conshdlrdata->noLinconsCont )
   {
      SCIP_Bool onlyCont = TRUE;
      int v;
      int nvars;
      SCIP_VAR** vars;

      nvars = SCIPgetNVarsLinear(scip, lincons);
      vars = SCIPgetVarsLinear(scip, lincons);

      /* check whether call variables are non-integer */
      for (v = 0; v < nvars; ++v)
      {
         SCIP_VARTYPE vartype;

         vartype = SCIPvarGetType(vars[v]);
         if ( vartype != SCIP_VARTYPE_CONTINUOUS && vartype != SCIP_VARTYPE_IMPLINT )
         {
            onlyCont = FALSE;
            break;
         }
      }

      if ( onlyCont )
         linconsactive = FALSE;
   }

   /* mark linear constraint no to be upgraded - otherwise we loos control over it */
   SCIP_CALL( SCIPmarkDoNotUpgradeConsLinear(scip, lincons) );

   /* create constraint data */
   consdata = NULL;
   SCIP_CALL( consdataCreate(scip, conshdlr, name, &consdata, conshdlrdata->eventhdlr, 
         binvar, slackvar, lincons, linconsactive, conshdlrdata->sepaAlternativeLP) );
   assert( consdata != NULL );

   /* create constraint */
   SCIP_CALL( SCIPcreateCons(scip, cons, name, conshdlr, consdata, initial, separate, enforce, check, propagate,
         local, modifiable, dynamic, removable, stickingatnode) );

   return SCIP_OKAY;
}


/** adds variable to the inequality of the indicator constraint */
SCIP_RETCODE SCIPaddVarIndicator(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONS*            cons,               /**< indicator constraint */
   SCIP_VAR*             var,                /**< variable to add to the inequality */
   SCIP_Real             val                 /**< value of variable */
   )
{
   SCIP_CONSDATA* consdata;

   assert( cons != NULL );
   assert( strcmp(SCIPconshdlrGetName(SCIPconsGetHdlr(cons)), CONSHDLR_NAME) == 0 );

   consdata = SCIPconsGetData(cons);
   assert( consdata != NULL );

   SCIP_CALL( SCIPaddCoefLinear(scip, consdata->lincons, var, val) );

   /* possibly adapt variable type */
   if ( SCIPvarGetType(consdata->slackvar) != SCIP_VARTYPE_CONTINUOUS && (! SCIPvarIsIntegral(var) || ! SCIPisIntegral(scip, val) ) )
   {
      SCIP_Bool infeasible;

      SCIP_CALL( SCIPchgVarType(scip, consdata->slackvar, SCIP_VARTYPE_CONTINUOUS, &infeasible) );
      assert(!infeasible);
   }

   return SCIP_OKAY;
}


/** gets the linear constraint corresponding to the indicator constraint (may be NULL) */
SCIP_CONS* SCIPgetLinearConsIndicator(
   SCIP_CONS*            cons                /**< indicator constraint */
   )
{
   SCIP_CONSDATA* consdata;

   assert( cons != NULL );
   assert( strcmp(SCIPconshdlrGetName(SCIPconsGetHdlr(cons)), CONSHDLR_NAME) == 0 );

   consdata = SCIPconsGetData(cons);
   assert( consdata != NULL );

   return consdata->lincons;
}


/** sets the linear constraint corresponding to the indicator constraint (may be NULL) */
SCIP_RETCODE SCIPsetLinearConsIndicator(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONS*            cons,               /**< indicator constraint */
   SCIP_CONS*            lincons             /**< linear constraint */
   )
{
   SCIP_CONSHDLR* conshdlr;
   SCIP_CONSHDLRDATA* conshdlrdata;
   SCIP_CONSDATA* consdata;

   if ( SCIPgetStage(scip) != SCIP_STAGE_PROBLEM )
   {
      SCIPerrorMessage("Cannot set linear constraint in SCIP stage <%d>\n", SCIPgetStage(scip) );
      return SCIP_INVALIDCALL;
   }

   assert( cons != NULL );
   conshdlr = SCIPconsGetHdlr(cons);

   assert( strcmp(SCIPconshdlrGetName(conshdlr), CONSHDLR_NAME) == 0 );
   conshdlrdata = SCIPconshdlrGetData(conshdlr);
   assert( conshdlrdata != NULL );

   consdata = SCIPconsGetData(cons);
   assert( consdata != NULL );

   /* free old linear constraint */
   assert( consdata->lincons != NULL );
   SCIP_CALL( SCIPdelCons(scip, consdata->lincons) );
   SCIP_CALL( SCIPreleaseCons(scip, &(consdata->lincons) ) );

   assert( lincons != NULL );
   consdata->lincons = lincons;
   consdata->linconsActive = TRUE;
   SCIP_CALL( SCIPcaptureCons(scip, lincons) );

   /* if the problem should be decomposed if only non-integer variables are present */
   if ( conshdlrdata->noLinconsCont )
   {
      SCIP_Bool onlyCont = TRUE;
      int v;
      int nvars;
      SCIP_VAR** vars;

      nvars = SCIPgetNVarsLinear(scip, lincons);
      vars = SCIPgetVarsLinear(scip, lincons);
      assert( vars != NULL );

      /* check whether call variables are non-integer */
      for (v = 0; v < nvars; ++v)
      {
         SCIP_VARTYPE vartype;

         vartype = SCIPvarGetType(vars[v]);
         if ( vartype != SCIP_VARTYPE_CONTINUOUS && vartype != SCIP_VARTYPE_IMPLINT )
         {
            onlyCont = FALSE;
            break;
         }
      }

      if ( onlyCont )
         consdata->linconsActive = FALSE;
   }

   return SCIP_OKAY;
}


/** gets binary variable corresponding to indicator constraint */
SCIP_VAR* SCIPgetBinaryVarIndicator(
   SCIP_CONS*            cons                /**< indicator constraint */
   )
{
   SCIP_CONSDATA* consdata;

   assert( cons != NULL );
   assert( strcmp(SCIPconshdlrGetName(SCIPconsGetHdlr(cons)), CONSHDLR_NAME) == 0 );

   consdata = SCIPconsGetData(cons);
   assert( consdata != NULL );

   return consdata->binvar;
}


/** sets binary indicator variable for indicator constraint */
SCIP_RETCODE SCIPsetBinaryVarIndicator(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONS*            cons,               /**< indicator constraint */
   SCIP_VAR*             binvar              /**< binary variable to add to the inequality */
   )
{
   SCIP_CONSDATA* consdata;

   assert( cons != NULL );
   assert( binvar != NULL );
   assert( strcmp(SCIPconshdlrGetName(SCIPconsGetHdlr(cons)), CONSHDLR_NAME) == 0 );

   consdata = SCIPconsGetData(cons);
   assert( consdata != NULL );

   /* check type */
   if ( SCIPvarGetType(binvar) != SCIP_VARTYPE_BINARY )
   {
      SCIPerrorMessage("Indicator variable <%s> is not binary %d.\n", SCIPvarGetName(binvar), SCIPvarGetType(binvar));
      return SCIP_ERROR;
   }

   /* check previous binary variable */
   if ( consdata->binvar != NULL )
   {
      /* to allow replacement of binary variables, we would need to drop events etc. */
      SCIPerrorMessage("Cannot replace binary variable <%s> for indicator constraint <%s>.\n", SCIPvarGetName(binvar), SCIPconsGetName(cons));
      return SCIP_INVALIDCALL;
   }

   /* if we are transformed, obtain transformed variables and catch events */
   if ( SCIPconsIsTransformed(cons) )
   {
      SCIP_VAR* var;
      SCIP_CONSHDLR* conshdlr;
      SCIP_CONSHDLRDATA* conshdlrdata;

      /* make sure we have a transformed binary variable */
      SCIP_CALL( SCIPgetTransformedVar(scip, binvar, &var) );
      assert( var != NULL );
      consdata->binvar = var;

      conshdlr = SCIPconsGetHdlr(cons);
      assert( conshdlr != NULL );
      assert( strcmp(SCIPconshdlrGetName(conshdlr), CONSHDLR_NAME) == 0 );
      conshdlrdata = SCIPconshdlrGetData(conshdlr);
      assert( conshdlrdata != NULL );

      /* catch bound change events on binary variable */
      if ( consdata->linconsActive )
      {
         SCIP_CALL( SCIPcatchVarEvent(scip, var, SCIP_EVENTTYPE_BOUNDCHANGED, conshdlrdata->eventhdlr, (SCIP_EVENTDATA*) consdata, NULL) );
      }

      /* if binary variable is fixed to be nonzero */
      if ( SCIPvarGetLbLocal(var) > 0.5 )
         ++(consdata->nFixedNonzero);
   }
   else
      consdata->binvar = binvar;

   return SCIP_OKAY;
}


/** gets slack variable corresponding to indicator constraint */
SCIP_VAR* SCIPgetSlackVarIndicator(
   SCIP_CONS*            cons                /**< indicator constraint */
   )
{
   SCIP_CONSDATA* consdata;

   assert( cons != NULL );
   assert( strcmp(SCIPconshdlrGetName(SCIPconsGetHdlr(cons)), CONSHDLR_NAME) == 0 );

   consdata = SCIPconsGetData(cons);
   assert( consdata != NULL );

   return consdata->slackvar;
}


/** sets slack variable corresponding to indicator constraint */
SCIP_RETCODE SCIPsetSlackVarIndicator(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONS*            cons,               /**< indicator constraint */
   SCIP_VAR*             slackvar            /**< slack variable */
   )
{
   SCIP_CONSDATA* consdata;
   SCIP_CONSHDLR* conshdlr;
   SCIP_CONSHDLRDATA* conshdlrdata;

   assert( cons != NULL );
   assert( strcmp(SCIPconshdlrGetName(SCIPconsGetHdlr(cons)), CONSHDLR_NAME) == 0 );
   assert( slackvar != NULL );

   if ( SCIPgetStage(scip) != SCIP_STAGE_PROBLEM )
   {
      SCIPerrorMessage("Cannot set slack variable in SCIP stage <%d>\n", SCIPgetStage(scip) );
      return SCIP_INVALIDCALL;
   }

   /* get constraint data */
   consdata = SCIPconsGetData(cons);
   assert( consdata != NULL );
   assert( consdata->slackvar != NULL );

   /* free event on previous slack variable */
   conshdlrdata = NULL;
   if ( SCIPconsIsTransformed(cons) )
   {
      conshdlr = SCIPconsGetHdlr(cons);
      assert( conshdlr != NULL );
      assert( strcmp(SCIPconshdlrGetName(conshdlr), CONSHDLR_NAME) == 0 );
      conshdlrdata = SCIPconshdlrGetData(conshdlr);
      assert( conshdlrdata != NULL );
      
      SCIP_CALL( SCIPdropVarEvent(scip, consdata->slackvar, SCIP_EVENTTYPE_BOUNDCHANGED, conshdlrdata->eventhdlr, (SCIP_EVENTDATA*) consdata, -1) );
   }

   /* free old slack variable */
   SCIP_CALL( SCIPdelVar(scip, consdata->slackvar) );
   SCIP_CALL( SCIPreleaseVar(scip, &(consdata->slackvar) ) );

   /* mark new slack variable not to be multi-aggregated */
   SCIP_CALL( SCIPmarkDoNotMultaggrVar(scip, slackvar) );

   /* handle transformed case  */
   if ( SCIPconsIsTransformed(cons) )
   {
      SCIP_VAR* var;

      /* make sure we have the transformed variable */
      SCIP_CALL( SCIPgetTransformedVar(scip, slackvar, &var) );
      assert( var != NULL );
      consdata->slackvar = var;
      SCIP_CALL( SCIPcaptureVar(scip, var) );

      /* catch bound change events on slack variable and adjust nFixedNonzero */
      if ( consdata->linconsActive )
      {
         assert( conshdlrdata != NULL );
         SCIP_CALL( SCIPcatchVarEvent(scip, var, SCIP_EVENTTYPE_BOUNDCHANGED, conshdlrdata->eventhdlr, (SCIP_EVENTDATA*) consdata, NULL) );

         /* if slack variable is fixed to be nonzero */
         if ( SCIPisFeasPositive(scip, SCIPvarGetLbLocal(var)) )
            ++(consdata->nFixedNonzero);
      }
   }
   else
   {
      consdata->slackvar = slackvar;
      SCIP_CALL( SCIPcaptureVar(scip, slackvar) );
   }

   return SCIP_OKAY;
}


/** checks whether indicator constraint is violated w.r.t. sol */
SCIP_Bool SCIPisViolatedIndicator(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONS*            cons,               /**< indicator constraint */
   SCIP_SOL*             sol                 /**< solution, or NULL to use current node's solution */
   )
{
   SCIP_CONSDATA* consdata;

   assert( cons != NULL );
   consdata = SCIPconsGetData(cons);
   assert( consdata != NULL );

   if ( consdata->linconsActive )
   {
      assert( consdata->slackvar != NULL );
      assert( consdata->binvar != NULL );
      return(
         SCIPisFeasPositive(scip, SCIPgetSolVal(scip, sol, consdata->slackvar)) &&
         SCIPisFeasPositive(scip, SCIPgetSolVal(scip, sol, consdata->binvar)) );
   }

   /* @todo: check how this can be decided for linconsActive == FALSE */
   return TRUE;
}


/** Based on values of other variables, computes slack and binary variable to turn constraint feasible */
SCIP_RETCODE SCIPmakeIndicatorFeasible(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONS*            cons,               /**< indicator constraint */
   SCIP_SOL*             sol,                /**< solution */
   SCIP_Bool*            changed             /**< whether the solution has been changed */
   )
{
   SCIP_CONSDATA* consdata;
   SCIP_CONS* lincons;
   SCIP_VAR** linvars;
   SCIP_Real* linvals;
   SCIP_VAR* slackvar;
   SCIP_VAR* binvar;
   int nlinvars;
   SCIP_Real sum;
   SCIP_Real val;
   int v;

   assert( cons != NULL );
   assert( strcmp(SCIPconshdlrGetName(SCIPconsGetHdlr(cons)), CONSHDLR_NAME) == 0 );
   assert( sol != NULL );
   assert( changed != NULL );

   *changed = FALSE;

   /* avoid deleted indicator constraints, e.g., due to preprocessing */
   if ( ! SCIPconsIsActive(cons) && SCIPgetStage(scip) >= SCIP_STAGE_PRESOLVING )
      return SCIP_OKAY;

   assert( cons != NULL );
   consdata = SCIPconsGetData(cons);
   assert( consdata != NULL );

   /* if the linear constraint is not present, we cannot do anything */
   if ( ! consdata->linconsActive )
      return SCIP_OKAY;

   slackvar = consdata->slackvar;
   binvar = consdata->binvar;
   lincons = consdata->lincons;
   assert( slackvar != NULL );
   assert( lincons != NULL );
   assert( binvar != NULL );

   /* avoid non-active linear constraints, e.g., due to preprocessing */
   if ( SCIPconsIsActive(lincons) || SCIPgetStage(scip) < SCIP_STAGE_PRESOLVING)
   {
      nlinvars = SCIPgetNVarsLinear(scip, lincons);
      linvars = SCIPgetVarsLinear(scip, lincons);
      linvals = SCIPgetValsLinear(scip, lincons);

      /* compute value of regular variables */
      sum = 0.0;
      for (v = 0; v < nlinvars; ++v)
      {
         SCIP_VAR* var;
         var = linvars[v];
         if ( var != slackvar )
            sum += linvals[v] * SCIPgetSolVal(scip, sol, var);
      }

      assert( SCIPisInfinity(scip, -SCIPgetLhsLinear(scip, lincons)) ||
         SCIPisInfinity(scip, SCIPgetRhsLinear(scip, lincons)) );

      val = SCIPgetRhsLinear(scip, lincons);
      if ( ! SCIPisInfinity(scip, val) )
         sum -= val;
      else
      {
         val = SCIPgetLhsLinear(scip, lincons);
         if ( ! SCIPisInfinity(scip, -val) )
            sum = val - sum;
      }

      /* check if linear constraint w/o slack variable is violated */
      if ( SCIPisFeasPositive(scip, sum) )
      {
         /* the original constraint is violated */
         if ( ! SCIPisFeasEQ(scip, SCIPgetSolVal(scip, sol, slackvar), sum) )
         {
            SCIP_CALL( SCIPsetSolVal(scip, sol, slackvar, sum) );
            *changed = TRUE;
         }
         if ( ! SCIPisFeasEQ(scip, SCIPgetSolVal(scip, sol, binvar), 0.0) )
         {
            SCIP_CALL( SCIPsetSolVal(scip, sol, binvar, 0.0) );
            *changed = TRUE;
         }
      }
      else
      {
         /* the original constraint is satisfied - we can set the slack variable to 0 (slackvar should only occur in this indicator constraint) */
         if ( ! SCIPisFeasEQ(scip, SCIPgetSolVal(scip, sol, slackvar), 0.0) )
         {
            SCIP_CALL( SCIPsetSolVal(scip, sol, slackvar, 0.0) );
            *changed = TRUE;
         }
         /* we might also set the binary variable - if no other constraints prevent it */
         if ( SCIPvarGetObj(binvar) < 0 )
         {
            if ( SCIPvarMayRoundUp(binvar) && ! SCIPisFeasEQ(scip, SCIPgetSolVal(scip, sol, binvar), 1.0) )
            {
               SCIP_CALL( SCIPsetSolVal(scip, sol, binvar, 1.0) );
               *changed = TRUE;
            }
         }
         else
         {
            if ( SCIPvarMayRoundDown(binvar) && ! SCIPisFeasEQ(scip, SCIPgetSolVal(scip, sol, binvar), 0.0) )
            {
               SCIP_CALL( SCIPsetSolVal(scip, sol, binvar, 0.0) );
               *changed = TRUE;
            }
         }
      }
   }

   return SCIP_OKAY;
}


/** adds additional linear constraint that is not connected with an indicator constraint, but can be used for separation */
SCIP_RETCODE SCIPaddLinearConsIndicator(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONSHDLR*        conshdlr,           /**< indicator constraint handler */
   SCIP_CONS*            lincons             /**< linear constraint */
   )
{
   SCIP_CONSHDLRDATA* conshdlrdata;

   assert( scip != NULL );
   assert( strcmp(SCIPconshdlrGetName(conshdlr), CONSHDLR_NAME) == 0 );
   assert( lincons != NULL );

   conshdlrdata = SCIPconshdlrGetData(conshdlr);
   assert( conshdlrdata != NULL );

   SCIP_CALL( consdataEnsureAddLinConsSize(scip, conshdlr, conshdlrdata->nAddLinCons+1) );
   assert( conshdlrdata->nAddLinCons+1 <= conshdlrdata->maxAddLinCons );

   conshdlrdata->addLinCons[conshdlrdata->nAddLinCons++] = lincons;

   return SCIP_OKAY;
}
