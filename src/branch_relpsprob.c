/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                           */
/*                  This file is part of the program                         */
/*          GCG --- Generic Colum Generation                                 */
/*                  a Dantzig-Wolfe decomposition based extension            */
/*                  of the branch-cut-and-price framework                    */
/*         SCIP --- Solving Constraint Integer Programs                      */
/*                                                                           */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
#pragma ident "@(#) $Id: branch_relpsprob.c,v 1.60 2010/02/04 10:23:56 bzfheinz Exp $"

//#define SCIP_DEBUG

/**@file   branch_relpsprob.c
 * @ingroup BRANCHINGRULES
 * @brief  generalized reliable pseudo costs branching rule 
 * @author Tobias Achterberg
 * @author Timo Berthold
 * @author Jens Schulz
 * @author Gerald Gamrath
 *
 * - probing is executed until depth 10 and afterwards with stepsize 5
 *   by that all pseudocost scores and inference informations are updated
 *   otherwise the variable with best score is branched on
 * - NEW! probing is done according to reliability values per candidate depending on tree size and probing rounds
 * - the node is reevaluated immediately if MAXBDCHGS occur during probing
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#include <assert.h>
#include <string.h>

#include "branch_relpsprob.h"
#include "relax_gcg.h"
#include "cons_origbranch.h"
#include "scip/type_var.h"
#include "scip/scip.h"


#define BRANCHRULE_NAME          "relpsprob"
#define BRANCHRULE_DESC          "generalized reliability branching using probing"
#define BRANCHRULE_PRIORITY      -100
#define BRANCHRULE_MAXDEPTH      -1
#define BRANCHRULE_MAXBOUNDDIST  1.0

#define DEFAULT_CONFLICTWEIGHT   0.01   /**< weight in score calculations for conflict score */
#define DEFAULT_CONFLENGTHWEIGHT 0.0001 /**< weight in score calculations for conflict length score*/
#define DEFAULT_INFERENCEWEIGHT  0.1    /**< weight in score calculations for inference score */
#define DEFAULT_CUTOFFWEIGHT     0.0001 /**< weight in score calculations for cutoff score */
#define DEFAULT_PSCOSTWEIGHT     1.0    /**< weight in score calculations for pseudo cost score */
#define DEFAULT_MINRELIABLE      1.0    /**< minimal value for minimum pseudo cost size to regard pseudo cost value as reliable */
#define DEFAULT_MAXRELIABLE      8.0    /**< maximal value for minimum pseudo cost size to regard pseudo cost value as reliable */
#define DEFAULT_ITERQUOT         0.5    /**< maximal fraction of branching LP iterations compared to normal iters */
#define DEFAULT_ITEROFS     100000      /**< additional number of allowed LP iterations */
#define DEFAULT_MAXLOOKAHEAD     8      /**< maximal number of further variables evaluated without better score */
#define DEFAULT_INITCAND       100      /**< maximal number of candidates initialized with strong branching per node */
#define DEFAULT_INITITER         0      /**< iteration limit for strong branching init of pseudo cost entries (0: auto) */
#define DEFAULT_MAXBDCHGS       20      /**< maximal number of bound tightenings before the node is immediately reevaluated (-1: unlimited) */
#define DEFAULT_MINBDCHGS        1      /**< minimal number of bound tightenings before the node is reevaluated */
#define DEFAULT_USELP            TRUE   /**< shall the lp be solved during probing? */
#define DEFAULT_RELIABILITY      0.8    /**< reliability value for probing */

#define HASHSIZE_VARS            131101 /**< minimal size of hash table in bdchgdata */


/** branching rule data */
struct SCIP_BranchruleData
{
   SCIP_Real             conflictweight;     /**< weight in score calculations for conflict score */
   SCIP_Real             conflengthweight;   /**< weight in score calculations for conflict length score */
   SCIP_Real             inferenceweight;    /**< weight in score calculations for inference score */
   SCIP_Real             cutoffweight;       /**< weight in score calculations for cutoff score */
   SCIP_Real             pscostweight;       /**< weight in score calculations for pseudo cost score */
   SCIP_Real             minreliable;        /**< minimal value for minimum pseudo cost size to regard pseudo cost value as reliable */
   SCIP_Real             maxreliable;        /**< maximal value for minimum pseudo cost size to regard pseudo cost value as reliable */
   SCIP_Real             iterquot;           /**< maximal fraction of branching LP iterations compared to normal iters */
   SCIP_Longint          nlpiterations;      /**< total number of used LP iterations */
   int                   iterofs;            /**< additional number of allowed LP iterations */
   int                   maxlookahead;       /**< maximal number of further variables evaluated without better score */
   int                   initcand;           /**< maximal number of candidates initialized with strong branching per node */
   int                   inititer;           /**< iteration limit for strong branching init of pseudo cost entries (0: auto) */
   int                   maxbdchgs;          /**< maximal number of bound tightenings before the node is immediately reevaluated (-1: unlimited) */
   int                   minbdchgs;          /**< minimal number of bound tightenings before bound changes are applied */
   SCIP_Bool             uselp;              /**< shall the lp be solved during probing? */
   int                   nprobingnodes;      /**< counter to store the total number of probing nodes */
   int                   ninfprobings;       /**< counter to store the number of probings which led to an infeasible branch */
   SCIP_Real             reliability;        /**< reliability value for branching variables */
   int                   nbranchings;        /**< counter to store the total number of nodes that are branched */
   int                   nresolvesminbdchgs; /**< counter to store how often node is reevaluated due to min bdchgs */
   int                   nresolvesinfcands;   /**< counter to store how often node is reevaluated since candidate with inf branch is chosen */
   int                   nprobings;          /**< counter to store the total number of probings that were performed */
   SCIP_HASHMAP*         varhashmap;         /**< hash storing variables; image is position in following arrays */
   int*                  nvarbranchings;     /**< array to store number of branchings per variable */
   int*                  nvarprobings;       /**< array to store number of probings per variable */
   int                   nvars;              /**< number of variables that are in hashmap */
};

/** data for pending bound changes */
struct BdchgData
{
   SCIP_HASHMAP*         varhashmap;         /**< hash storing variables; image is position in lbchgs-array */
   int*                  lbchgs;             /**< array containing lower bounds per variable */         
   int*                  ubchgs;             /**< array containing upper bounds per variable */
   SCIP_Bool*            infroundings;       /**< array to store for each var if some rounding is infeasible */
   int                   nvars;              /**< number of variables that are considered so far */
};
typedef struct BdchgData BDCHGDATA;




/*
 * local methods
 */

/* creates bound change data structure: 
 * all variables are put into a hashmap and arrays containig current lower and upper bounds are created 
 */
static
SCIP_RETCODE createBdchgData(
   SCIP*                 scip,               /**< SCIP data structure */
   BDCHGDATA**           bdchgdata,          /**< bound change data to be allocated */
   SCIP_VAR**            vars,               /**< array of variables to be watched */
   int                   nvars               /**< number of variables to be watched */
   )
{

   int i;

   assert(scip != NULL);
   assert(*bdchgdata == NULL);

   /* get memory for bound change data structure */
   SCIP_CALL( SCIPallocBuffer(scip, bdchgdata) );

   /* create hash map */
   SCIP_CALL( SCIPhashmapCreate(&(*bdchgdata)->varhashmap, SCIPblkmem(scip), HASHSIZE_VARS) );
   
   /* get all variables */
   SCIP_CALL( SCIPallocBufferArray(scip, &(*bdchgdata)->lbchgs, nvars) ); 
   SCIP_CALL( SCIPallocBufferArray(scip, &(*bdchgdata)->ubchgs, nvars) ); 
   SCIP_CALL( SCIPallocBufferArray(scip, &(*bdchgdata)->infroundings, nvars) ); 
   (*bdchgdata)->nvars = nvars;
   
   /* store current local bounds and capture variables */
   for( i = 0; i < nvars; ++i )
   {
      SCIP_CALL( SCIPhashmapInsert((*bdchgdata)->varhashmap, vars[i], (void*) (size_t)i) );

      (*bdchgdata)->lbchgs[i] = SCIPfeasCeil(scip, SCIPvarGetLbLocal(vars[i]));
      (*bdchgdata)->ubchgs[i] = SCIPfeasFloor(scip, SCIPvarGetUbLocal(vars[i]));
      (*bdchgdata)->infroundings[i] = FALSE;
   }
     
   return SCIP_OKAY;
}

/** method to free bound change data strucutre */
static
SCIP_RETCODE freeBdchgData(   
   SCIP*                 scip,               /**< SCIP data structure */
   BDCHGDATA*            bdchgdata           /**< bound change data to be allocated */
   )
{

   assert(scip != NULL);
   assert(bdchgdata != NULL);
   
   /* free arrays & hashmap */
   SCIPfreeBufferArray(scip, &bdchgdata->infroundings);
   SCIPfreeBufferArray(scip, &bdchgdata->ubchgs);
   SCIPfreeBufferArray(scip, &bdchgdata->lbchgs);
   
   SCIPhashmapFree(&(bdchgdata->varhashmap));
   
   /* free memory for bound change data structure */
   SCIPfreeBuffer(scip, &bdchgdata);
  
   return SCIP_OKAY;
}


/** adds given variable and bound change to hashmap and bound change arrays */
static
SCIP_RETCODE addBdchg(
   SCIP*                 scip,               /**< SCIP data structure */
   BDCHGDATA*            bdchgdata,          /**< structure to keep bound chage data */
   SCIP_VAR*             var,                /**< variable to store bound change */
   int                   newbound,           /**< new bound for given variable */
   SCIP_BOUNDTYPE        boundtype,          /**< lower or upper bound change */
   SCIP_Bool             infrounding,        /**< is the bdchg valid due to an infeasible rounding of the given var */
   int*                  nbdchgs,            /**< total number of bound changes occured so far */
   SCIP_Bool*            infeasible          /**< pointer to store whether bound change makes the node infeasible */
   )
{
   int nvars; 
   int pos;

   assert(scip != NULL);
   assert(bdchgdata != NULL);
   assert(bdchgdata->varhashmap != NULL);
   assert(bdchgdata->lbchgs != NULL);
   assert(bdchgdata->ubchgs != NULL);
   assert(var != NULL);
   
   nvars = bdchgdata->nvars;

   if( infeasible != NULL )
      *infeasible = FALSE;

   //   if( SCIPvarGetType(var) == SCIP_VARTYPE_BINARY )
   //      return SCIP_OKAY;

   /* if variable is not in hashmap insert it and increase array sizes */
   if( !SCIPhashmapExists(bdchgdata->varhashmap, var) )
   {
      //      printf("capture additional variable <%s>\n", SCIPvarGetName(var) );
      //SCIP_CALL( SCIPcaptureVar(scip, var) );
      SCIPhashmapInsert(bdchgdata->varhashmap, var, (void*) (size_t)nvars);
      SCIP_CALL( SCIPreallocBufferArray(scip, &bdchgdata->lbchgs, nvars + 1) );
      SCIP_CALL( SCIPreallocBufferArray(scip, &bdchgdata->ubchgs, nvars + 1) );

      bdchgdata->lbchgs[nvars] = SCIPfeasCeil(scip, SCIPvarGetLbLocal(var));
      bdchgdata->ubchgs[nvars] = SCIPfeasFloor(scip, SCIPvarGetUbLocal(var));
      (bdchgdata->nvars)++;

      assert(SCIPhashmapExists(bdchgdata->varhashmap, var) 
         && (int)(size_t) SCIPhashmapGetImage(bdchgdata->varhashmap, var) == nvars);

   }
   
   /* get position of this variable */
   pos = (int)(size_t) SCIPhashmapGetImage(bdchgdata->varhashmap, var);
   
   if( infrounding )
   {
      bdchgdata->infroundings[pos] = TRUE;
   }

   /* update bounds if necessary */
   if( boundtype == SCIP_BOUNDTYPE_LOWER )
   {
      if( bdchgdata->lbchgs[pos] < newbound )
      {
         bdchgdata->lbchgs[pos] = newbound;
         (*nbdchgs)++;
      }
      
      if( newbound > bdchgdata->ubchgs[pos] )
      {
         *infeasible = TRUE;
      }
      
   } 
   else 
   {
      if( newbound < bdchgdata->ubchgs[pos] )
      {
         bdchgdata->ubchgs[pos] = newbound;
         (*nbdchgs)++;
      }
      if( newbound < bdchgdata->lbchgs[pos] )
      {
         *infeasible = TRUE;
      }
   }  

   return SCIP_OKAY;
}



/** applies bound changes stored in bound change arrays */
static
SCIP_RETCODE applyBdchgs(
   SCIP*                 scip,               /**< SCIP data structure */
   BDCHGDATA*            bdchgdata,          /**< structure containing bound changes for almost all variables */
   SCIP_NODE*            node                /**< node for which bound changes are applied, NULL for curnode */
   )
{
   SCIP_VAR** vars;
         
   int nintvars;
   int nbinvars;
   int nvars; 
   int nbdchgs;
   int i; 

   assert(scip != NULL);
   assert(bdchgdata != NULL);

   SCIPdebugMessage("apply bound changes\n");

   nbdchgs = 0;
   
   /* get all variables */
   SCIP_CALL( SCIPgetVarsData(scip, &vars, NULL, &nbinvars, &nintvars, NULL, NULL) );
   nvars = nbinvars + nintvars;
   assert(vars != NULL);
   
   /* get variable image in hashmap and update bounds if better ones found  */
   for( i = 0; i < nvars; ++i )
   {
      if( SCIPhashmapExists(bdchgdata->varhashmap, vars[i]) )
      {
         int pos; 
         pos = (int)(size_t)SCIPhashmapGetImage(bdchgdata->varhashmap, vars[i]);

         /* update lower bounds */
         if( SCIPisFeasGT(scip, (bdchgdata->lbchgs)[pos], SCIPvarGetLbLocal(vars[i])) )
         {
            SCIPdebugMessage("branch_relpsprob: update lower bound of <%s> from %g to %d\n",
               SCIPvarGetName(vars[i]), SCIPvarGetLbLocal(vars[i]), (bdchgdata->lbchgs)[pos]);
            SCIP_CALL( SCIPchgVarLbNode(scip, node, vars[i], (bdchgdata->lbchgs)[pos]) );
            ++nbdchgs;
         }
         /* update upper bounds */
         if( SCIPisFeasLT(scip, (bdchgdata->ubchgs)[pos], SCIPvarGetUbLocal(vars[i])) )
         {
            SCIPdebugMessage("branch_relpsprob: update upper bound of <%s> from %g to %d\n",
               SCIPvarGetName(vars[i]), SCIPvarGetUbLocal(vars[i]), (bdchgdata->ubchgs)[pos]);

            SCIP_CALL( SCIPchgVarUbNode(scip, node, vars[i], (bdchgdata->ubchgs)[pos]) );
            ++nbdchgs;
         }
      }
   }
 
   SCIPdebugMessage("applied %d bound changes\n", nbdchgs);
   printf("applied %d bound changes\n", nbdchgs);

   return SCIP_OKAY;
}





/** calculates an overall score value for the given individual score values */
static
SCIP_Real calcScore(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_BRANCHRULEDATA*  branchruledata,     /**< branching rule data */
   SCIP_Real             conflictscore,      /**< conflict score of current variable */
   SCIP_Real             avgconflictscore,   /**< average conflict score */
   SCIP_Real             conflengthscore,    /**< conflict length score of current variable */
   SCIP_Real             avgconflengthscore, /**< average conflict length score */
   SCIP_Real             inferencescore,     /**< inference score of current variable */
   SCIP_Real             avginferencescore,  /**< average inference score */
   SCIP_Real             cutoffscore,        /**< cutoff score of current variable */
   SCIP_Real             avgcutoffscore,     /**< average cutoff score */
   SCIP_Real             pscostscore,        /**< pscost score of current variable */
   SCIP_Real             avgpscostscore,     /**< average pscost score */
   SCIP_Real             frac                /**< fractional value of variable in current solution */
   )
{
   SCIP_Real score;

   assert(branchruledata != NULL);
   //   assert(0.0 < frac && frac < 1.0);

   score = branchruledata->conflictweight * (1.0 - 1.0/(1.0+conflictscore/avgconflictscore))
      + branchruledata->conflengthweight * (1.0 - 1.0/(1.0+conflengthscore/avgconflengthscore))
      + branchruledata->inferenceweight * (1.0 - 1.0/(1.0+inferencescore/avginferencescore))
      + branchruledata->cutoffweight * (1.0 - 1.0/(1.0+cutoffscore/avgcutoffscore))
      + branchruledata->pscostweight * (1.0 - 1.0/(1.0+pscostscore/avgpscostscore));

   /* values close to integral are possible and are adjusted to small non-zero values */
   if( frac < 0.00000001 || frac > 0.999999 )
      frac = 0.0001;
   if( MIN(frac, 1.0 - frac) < 10.0*SCIPfeastol(scip) )
      score *= 1e-6;

   return score;
}


/* calculates variable bounds for an up-branch and a down-branch, supposig a LP or pseudo solution is given */
static
SCIP_RETCODE calculateBounds(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_VAR*             branchvar,          /**< branching variable */
   int*                  downlb,             /**< lower bound of variable in down branch */
   int*                  downub,             /**< upper bound of variable in down branch */
   int*                  uplb,               /**< lower bound of variable in up branch */
   int*                  upub                /**< upper bound of variable in up branch */
   )
{
   SCIP_Real varsol;
   int lbdown;
   int ubdown;
   int lbup;
   int ubup;

   int lblocal;
   int ublocal;

   assert(scip != NULL);
   assert(branchvar != NULL);

   varsol = SCIPgetVarSol(scip, branchvar);

   lblocal = SCIPfeasCeil(scip, SCIPvarGetLbLocal(branchvar));
   ublocal = SCIPfeasFloor(scip, SCIPvarGetUbLocal(branchvar));

   /* calculate bounds in down branch */
   lbdown = lblocal;
   
   /* in down branch: new upper bound is at most local upper bound - 1 */
   ubdown = SCIPfeasFloor(scip, varsol) ;
   if( ubdown == ublocal )
      ubdown--;      

   assert(lbdown <= ubdown);

   /* calculate bounds in up branch */
   ubup = ublocal;
      
   /* in right branch: new lower bound is at least local lower bound + 1 */
   lbup = SCIPfeasCeil(scip, varsol);
   if( lbup == lblocal )
      lbup++;

   assert(lbup <= ubup);

   /* ensure that both branches partition the domain */
   if( lbup == ubdown )
   {
      int middle = (lblocal + ublocal) / 2; /* implicit rounding */
      
      if( lbup <= middle )
         ubdown--;
      else 
         lbup++;
   }

   /* ensure a real partition of the domain */
   assert(ubdown < lbup);
   assert(lbdown <= ubdown);
   assert(lbup <= ubup);

   /* set return values */
   if( downlb != NULL )
      *downlb = lbdown;
   if( downub != NULL )
      *downub = ubdown;
   if( uplb != NULL )
      *uplb = lbup;
   if( upub != NULL )
      *upub = ubup;
 
   return SCIP_OKAY;
}


/** applies probing of a single variable in the given direction, and stores evaluation in given arrays */
static
SCIP_RETCODE applyProbing(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_VAR**            vars,               /**< problem variables */
   int                   nvars,              /**< number of problem variables */
   SCIP_VAR*             probingvar,         /**< variable to perform probing on */
   SCIP_Bool             probingdir,         /**< value to fix probing variable to */
   SCIP_Bool             solvelp,            /**< value to decide whether pricing loop shall be performed */
   SCIP_Longint*         nlpiterations,      /**< pointert to store the number of used LP iterations */
   SCIP_Real*            proplbs,            /**< array to store lower bounds after full propagation */
   SCIP_Real*            propubs,            /**< array to store upper bounds after full propagation */
   SCIP_Real*            lpobjvalue,         /**< pointer to store the lp obj value if lp was solved */
   SCIP_Bool*            lpsolved,           /**< pointer to store whether the lp was solved */
   SCIP_Bool*            lperror,            /**< pointer to store whether an unresolved LP error occured or the
                                              *   solving process should be stopped (e.g., due to a time limit) */
   SCIP_Bool*            cutoff              /**< pointer to store whether the probing direction is infeasible */
   )
{
   SCIP* masterscip;
   SCIP_NODE* probingnode;
   SCIP_CONS* probingcons;

   SCIP_Real varsol;
   int leftlbprobing;
   int leftubprobing;
   int rightlbprobing;
   int rightubprobing;
   /* TODO: handle the feasible result */
   SCIP_Bool feasible;

   leftubprobing = -1;
   leftlbprobing = -1;
   rightlbprobing = -1;
   rightubprobing = -1;

   assert(proplbs != NULL);
   assert(propubs != NULL);
   assert(cutoff != NULL);
   assert(SCIPvarGetLbLocal(probingvar) - 0.5 < SCIPvarGetUbLocal(probingvar));
   assert(SCIPisFeasIntegral(scip, SCIPvarGetLbLocal(probingvar)));
   assert(SCIPisFeasIntegral(scip, SCIPvarGetUbLocal(probingvar)));
         
   assert(!solvelp || (lpsolved!=NULL && lpobjvalue!=NULL && lperror!=NULL));

   /* get SCIP data structure of master problem */
   masterscip = GCGrelaxGetMasterprob(scip);
   assert(masterscip != NULL);
      
   varsol = SCIPgetRelaxSolVal(scip, probingvar);
   
   if( probingdir == FALSE )
   {

      SCIP_CALL( calculateBounds(scip, probingvar, 
            &leftlbprobing, &leftubprobing, NULL, NULL) );     
   }
   else
   {
      SCIP_CALL( calculateBounds(scip, probingvar, 
            NULL, NULL, &rightlbprobing, &rightubprobing) );
   }

   SCIPdebugMessage("applying probing on variable <%s> == %u [%d,%d] (nlocks=%d/%d, impls=%d/%d, clqs=%d/%d)\n",
      SCIPvarGetName(probingvar), probingdir,
      probingdir ? rightlbprobing : leftlbprobing, probingdir ? rightubprobing : leftubprobing,
      SCIPvarGetNLocksDown(probingvar), SCIPvarGetNLocksUp(probingvar),
      SCIPvarGetNImpls(probingvar, FALSE), SCIPvarGetNImpls(probingvar, TRUE),
      SCIPvarGetNCliques(probingvar, FALSE), SCIPvarGetNCliques(probingvar, TRUE));

   /* start probing mode */
   SCIP_CALL( SCIPstartProbing(scip) );
   SCIPnewProbingNode(scip);

   probingnode = SCIPgetCurrentNode(scip);
   SCIP_CALL( GCGcreateConsOrigbranch(scip, &probingcons, "probingcons", probingnode, 
         GCGconsOrigbranchGetActiveCons(scip), NULL, NULL) );
   SCIP_CALL( SCIPaddConsNode(scip, probingnode, probingcons, NULL) );
   SCIP_CALL( SCIPreleaseCons(scip, &probingcons) );
   
   *lpsolved = FALSE;
   *lperror = FALSE;

   /* change variable bounds for the probing directions*/
   if( probingdir == FALSE )
   {
      SCIP_CALL( SCIPchgVarUbProbing(scip, probingvar, leftubprobing) );
   }
   else
   {
      SCIP_CALL( SCIPchgVarLbProbing(scip, probingvar, rightlbprobing) );
   }

   /* apply propagation */
   if( !(*cutoff) )
   {
      SCIP_CALL( SCIPpropagateProbing(scip, -1 /*@todo maxproprounds */, cutoff, NULL) );
   }

   /* evaluate propagation */
   if( !(*cutoff) )
   {
      int i;

      for( i = 0; i < nvars; ++i )
      {
         proplbs[i] = SCIPvarGetLbLocal(vars[i]);
         propubs[i] = SCIPvarGetUbLocal(vars[i]);
      }
   }

   /* if parameter is set, we want to use the outcome of the LP relaxation */
   if( !(*cutoff) && solvelp )
   {
      //printf("before probing = %lld\n", *nlpiterations);
      *nlpiterations -= SCIPgetNLPIterations(masterscip);

      SCIP_CALL( GCGrelaxPerformProbing(scip, nlpiterations, lpobjvalue, lpsolved, lperror, cutoff, &feasible) );
      
      //printf("after probing = %lld\n", *nlpiterations);
   }

   /* exit probing mode */
   SCIP_CALL( SCIPendProbing(scip) );

   SCIPdebugMessage("probing results in cutoff/lpsolved/lpobj: %s / %s / %g\n", 
      *cutoff?"cutoff":"no cutoff", *lpsolved?"lpsolved":"lp not solved", *lpobjvalue);

   return SCIP_OKAY;
}


/** gets generalized strong branching information on problem variable */
static
SCIP_RETCODE SCIPgetVarProbingbranch(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_VAR*             probingvar,         /**< variable to get strong probing branching values for */
   BDCHGDATA*            bdchgdata,          /**< structure containing bound changes for almost all variables */
   int                   itlim,              /**< iteration limit for strong branchings */
   SCIP_Bool             solvelp,            /**< value to decide whether pricing loop shall be performed */
   SCIP_Longint*         nlpiterations,      /**< pointert to stroe the number of used LP iterations */
   SCIP_Real*            down,               /**< stores dual bound after branching column down */
   SCIP_Real*            up,                 /**< stores dual bound after branching column up */
   SCIP_Bool*            downvalid,          /**< stores whether the returned down value is a valid dual bound, or NULL;
                                              *   otherwise, it can only be used as an estimate value */
   SCIP_Bool*            upvalid,            /**< stores whether the returned up value is a valid dual bound, or NULL;
                                              *   otherwise, it can only be used as an estimate value */
   SCIP_Bool*            downinf,            /**< pointer to store whether the downwards branch is infeasible, or NULL */
   SCIP_Bool*            upinf,              /**< pointer to store whether the upwards branch is infeasible, or NULL */
   SCIP_Bool*            lperror,            /**< pointer to store whether an unresolved LP error occured or the
                                              *   solving process should be stopped (e.g., due to a time limit) */
   int*                  nbdchgs             /**< pointer to store number of total bound changes */
   )
{
   /* data for variables and bdchg arrays */
   SCIP_VAR** probvars;
   SCIP_VAR** vars;
   int nvars;
   int nintvars;
   int nbinvars;

   SCIP_Real* leftproplbs;
   SCIP_Real* leftpropubs;
   SCIP_Real* rightproplbs;
   SCIP_Real* rightpropubs;
   SCIP_VARTYPE vartype;

   SCIP_Real leftlpbound;
   SCIP_Real rightlpbound;
   SCIP_Bool leftlpsolved;
   SCIP_Bool rightlpsolved;
   SCIP_Bool leftlperror;
   SCIP_Bool rightlperror;
   SCIP_Bool leftcutoff;
   SCIP_Bool rightcutoff;
   
   SCIP_Bool delay;
   SCIP_Bool cutoff;

   int i;

   SCIP_Real varsol;
   int j;

   assert(lperror != NULL);

   if( downvalid != NULL )
      *downvalid = FALSE;
   if( upvalid != NULL )
      *upvalid = FALSE;
   if( downinf != NULL )
      *downinf = FALSE;
   if( upinf != NULL )
      *upinf = FALSE;
   
   vartype = SCIPvarGetType(probingvar);

   if( SCIPisStopped(scip) )
   {
      SCIPverbMessage(scip, SCIP_VERBLEVEL_HIGH, NULL,
         "   (%.1fs) probing aborted: solving stopped\n", SCIPgetSolvingTime(scip));
      return SCIP_OKAY;
   }   
   
   /* get lp solution value of last run */
   varsol = SCIPgetRelaxSolVal(scip, probingvar);

   /* get all variables to store branching deductions of variable bounds */
   /* get all variables and store them in array 'vars' */
   SCIP_CALL( SCIPgetVarsData(scip, &probvars, NULL, &nbinvars, &nintvars, NULL, NULL) );
   nvars = nbinvars + nintvars; /* continuous variables are not considered here */

   SCIP_CALL( SCIPduplicateMemoryArray(scip, &vars, probvars, nvars) );
   
   /* capture variables to make sure, the variables are not deleted */
   for( i = 0; i < nvars; ++i )
   {
      SCIP_CALL( SCIPcaptureVar(scip, vars[i]) );
   }
   
   /* get temporary memory for storing probing results */
   SCIP_CALL( SCIPallocBufferArray(scip, &leftproplbs, nvars) );
   SCIP_CALL( SCIPallocBufferArray(scip, &leftpropubs, nvars) );
   SCIP_CALL( SCIPallocBufferArray(scip, &rightproplbs, nvars) );
   SCIP_CALL( SCIPallocBufferArray(scip, &rightpropubs, nvars) );

   /* for each binary variable, probe fixing the variable to left and right */
   delay = FALSE;
   cutoff = FALSE;
   leftcutoff = FALSE;
   rightcutoff = FALSE;
  
   /* left branch: apply probing for setting ub to LP solution value  */
   SCIP_CALL( applyProbing(scip, vars, nvars, probingvar, FALSE, solvelp, nlpiterations,
         leftproplbs, leftpropubs,
         &leftlpbound, &leftlpsolved, &leftlperror, &leftcutoff) );
   
   if( leftcutoff )
   {
      int newbound;

      SCIP_CALL( calculateBounds(scip, probingvar, 
            NULL, NULL, &newbound, NULL) );

      // newbound = SCIPfeasCeil(scip, varsol);
//       if( SCIPisFeasEQ(scip, newbound, SCIPvarGetLbLocal(probingvar)) )
//          newbound++;

      /* lower bound can be updated */
      SCIPdebugMessage("change lower bound of probing variable <%s> from %g to %d, nlocks=(%d/%d)\n", 
         SCIPvarGetName(probingvar), SCIPvarGetLbLocal(probingvar), newbound,
         SCIPvarGetNLocksDown(probingvar), SCIPvarGetNLocksUp(probingvar));

      SCIP_CALL( addBdchg(scip, bdchgdata, probingvar, newbound, SCIP_BOUNDTYPE_LOWER, TRUE, nbdchgs, &cutoff) );
   }
      
   if( !cutoff )
   {
      /* right branch: apply probing for setting lb to LP solution value  */
      SCIP_CALL( applyProbing(scip, vars, nvars, probingvar, TRUE, solvelp, nlpiterations,
            rightproplbs, rightpropubs,
            &rightlpbound, &rightlpsolved, &rightlperror, &rightcutoff) );
      
      if( rightcutoff )
      {
         int newbound;

         SCIP_CALL( calculateBounds(scip, probingvar, 
               NULL, &newbound, NULL, NULL) );

         // newbound = SCIPfeasFloor(scip, varsol);
//          if( SCIPisFeasEQ(scip, newbound, SCIPvarGetUbLocal(probingvar)) )
//          newbound--;

         /* upper bound can be updated */
         SCIPdebugMessage("change probing variable <%s> upper bound from %g to %d, nlocks=(%d/%d)\n",
            SCIPvarGetName(probingvar), SCIPvarGetUbLocal(probingvar), newbound,
            SCIPvarGetNLocksDown(probingvar), SCIPvarGetNLocksUp(probingvar));
         
         SCIP_CALL( addBdchg(scip, bdchgdata, probingvar, newbound, SCIP_BOUNDTYPE_UPPER, TRUE, nbdchgs, &cutoff) );
      }
   }

   /* set return value of lperror */
   cutoff = cutoff || (leftcutoff && rightcutoff);
   *lperror = leftlperror || rightlperror;
   

   /* analyze probing deductions */
   
   /* 1. dualbounds */
   if( leftlpsolved  )
      *down = leftlpbound;
   if( rightlpsolved )
      *up = rightlpbound;
   
   /* 2. update bounds */
   for( j = 0; j < nvars && !cutoff; ++j )
   {
      SCIP_Real newlb;
      SCIP_Real newub;
      
      if( vars[j] == probingvar )
         continue;
      
      /* new bounds of the variable is the union of the propagated bounds of the left and right case */
      newlb = MIN(leftproplbs[j], rightproplbs[j]);
      newub = MAX(leftpropubs[j], rightpropubs[j]);
      
      /* check for fixed variables */
      if( SCIPisFeasEQ(scip, newlb, newub) )
      {
         /* in both probings, variable j is deduced to a fixed value */
         SCIP_CALL( addBdchg(scip, bdchgdata, vars[j], newlb, SCIP_BOUNDTYPE_LOWER, FALSE, nbdchgs, &cutoff) );
         SCIP_CALL( addBdchg(scip, bdchgdata, vars[j], newub, SCIP_BOUNDTYPE_UPPER, FALSE, nbdchgs, &cutoff) );
         continue;
      } 
      else 
      {
         SCIP_Real oldlb;
         SCIP_Real oldub;

         assert(SCIPvarGetType(vars[j]) == SCIP_VARTYPE_BINARY || SCIPvarGetType(vars[j]) == SCIP_VARTYPE_INTEGER);

         /* check for bound tightenings */
         oldlb = SCIPvarGetLbLocal(vars[j]);
         oldub = SCIPvarGetUbLocal(vars[j]);
         if( SCIPisLbBetter(scip, newlb, oldlb, oldub) )
         {
            /* in both probings, variable j is deduced to be at least newlb: tighten lower bound */
            SCIP_CALL( addBdchg(scip, bdchgdata, vars[j], newlb, SCIP_BOUNDTYPE_LOWER, FALSE, nbdchgs, &cutoff) );
         }
         if( SCIPisUbBetter(scip, newub, oldlb, oldub) && !cutoff )
         {
            /* in both probings, variable j is deduced to be at most newub: tighten upper bound */
            SCIP_CALL( addBdchg(scip, bdchgdata, vars[j], newub, SCIP_BOUNDTYPE_UPPER, FALSE, nbdchgs, &cutoff) );
         }
         
      } 
            
   } /* end check for deductions */

   /* set correct return values */
   if( down != NULL && leftlpsolved )
      *down = leftlpbound;
   if( up != NULL && rightlpsolved )
      *up = rightlpbound;
   if( downvalid != NULL && leftlpsolved )
      *downvalid = TRUE;
   if( downvalid != NULL && !leftlpsolved )
      *downvalid = FALSE;
   if( upvalid != NULL && rightlpsolved )
      *upvalid = TRUE;
   if( upvalid != NULL && !rightlpsolved )
      *upvalid = FALSE;
   if( downinf != NULL )
      *downinf = leftcutoff;
   if( upinf != NULL )
      *upinf = rightcutoff;

   if( cutoff )
   {
      *downinf = cutoff;
      *upinf = cutoff;
   }

   /* free temporary memory */
   SCIPfreeBufferArray(scip, &rightpropubs);
   SCIPfreeBufferArray(scip, &rightproplbs);
   SCIPfreeBufferArray(scip, &leftpropubs);
   SCIPfreeBufferArray(scip, &leftproplbs);

   /* capture variables to make sure, the variables are not deleted */
   for( i = 0; i < nvars; ++i )
   {
      SCIP_CALL( SCIPreleaseVar(scip, &vars[i]) );
   }
   
   SCIPfreeMemoryArray(scip, &vars);

  
   return SCIP_OKAY;
}




/** adds branching candidates to branchruledata to collect infos about it */
static
SCIP_RETCODE addBranchcandsToData(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_BRANCHRULE*      branchrule,         /**< branching rule */
   SCIP_VAR**            branchcands,        /**< branching candidates */
   int                   nbranchcands       /**< number of branching candidates */
   )
{

   SCIP_BRANCHRULEDATA* branchruledata;
   int j;


   /* get branching rule data */
   branchruledata = SCIPbranchruleGetData(branchrule);
   assert(branchruledata != NULL);

   if( branchruledata->nvars == 0 )
   {     /* no variables known before, reinitialized hashmap and variable info storage */
      
      /* create hash map */
      SCIP_CALL( SCIPhashmapCreate(&(branchruledata->varhashmap), SCIPblkmem(scip), HASHSIZE_VARS) );
    
      SCIP_CALL( SCIPallocMemoryArray(scip, &branchruledata->nvarprobings, nbranchcands) ); 
      SCIP_CALL( SCIPallocMemoryArray(scip, &branchruledata->nvarbranchings, nbranchcands) ); 
      branchruledata->nvars = nbranchcands;

      /* store each variable in hashmap and initialize array entries */
      for( j = 0; j < nbranchcands; ++j )
      {
         //   SCIP_CALL( SCIPcaptureVar(scip, branchcands[j]) );
         SCIP_CALL( SCIPhashmapInsert(branchruledata->varhashmap, branchcands[j], (void*) (size_t)j) );
         branchruledata->nvarprobings[j] = 0;
         branchruledata->nvarbranchings[j] = 0;
      }
   }
   else  /* possibly new variables need to be added */
   {
      
      /* if var is not in hashmap, insert it */
      for( j = 0; j < nbranchcands; ++j )
      {
         SCIP_VAR* var;
         int nvars; 

         var = branchcands[j];
         assert(var != NULL);
         nvars = branchruledata->nvars;
         
         /* if variable is not in hashmap insert it and increase array sizes */
         if( !SCIPhashmapExists(branchruledata->varhashmap, var) )
         {
            //  SCIP_CALL( SCIPcaptureVar(scip, var) );
            SCIPhashmapInsert(branchruledata->varhashmap, var, (void*) (size_t)nvars);
            SCIP_CALL( SCIPreallocMemoryArray(scip, &branchruledata->nvarprobings, nvars + 1) );
            SCIP_CALL( SCIPreallocMemoryArray(scip, &branchruledata->nvarbranchings, nvars + 1) );

            branchruledata->nvarprobings[nvars] = 0;
            branchruledata->nvarbranchings[nvars] = 0;
      
            assert(SCIPhashmapExists(branchruledata->varhashmap, var) 
               && (int)(size_t) SCIPhashmapGetImage(branchruledata->varhashmap, var) == nvars);

            (branchruledata->nvars)++;
            nvars++;
         }

      }
   }

   return SCIP_OKAY;
}

/** increases number of branchings that took place on the given variable */
static
SCIP_RETCODE incNVarBranchings(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_BRANCHRULE*      branchrule,         /**< branching rule */
   SCIP_VAR*             var                 /**< variable to increase number of branchings on */
   )
{
   SCIP_BRANCHRULEDATA* branchruledata;
   int pos;

   assert(scip != NULL);
   assert(branchrule != NULL);
   assert(var != NULL);

   /* get branching rule data */
   branchruledata = SCIPbranchruleGetData(branchrule);
   assert(branchruledata != NULL);

   assert(SCIPhashmapExists(branchruledata->varhashmap, var) );

   pos = (int)(size_t) SCIPhashmapGetImage(branchruledata->varhashmap, var);
   (branchruledata->nvarbranchings[pos])++;

   (branchruledata->nbranchings)++;

   return SCIP_OKAY;
}

/** increases number of probings that took place on the given variable */
static
SCIP_RETCODE incNVarProbings(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_BRANCHRULE*      branchrule,         /**< branching rule */
   SCIP_VAR*             var                 /**< variable to increase number of branchings on */
   )
{
   SCIP_BRANCHRULEDATA* branchruledata;
   int pos;

   assert(scip != NULL);
   assert(branchrule != NULL);
   assert(var != NULL);

   /* get branching rule data */
   branchruledata = SCIPbranchruleGetData(branchrule);
   assert(branchruledata != NULL);

   assert(SCIPhashmapExists(branchruledata->varhashmap, var) );

   pos = (int)(size_t) SCIPhashmapGetImage(branchruledata->varhashmap, var);
   (branchruledata->nvarprobings[pos])++;

   (branchruledata->nprobings)++;

   return SCIP_OKAY;
}

/** evaluates whether probing should be performed on the given variable */
static
SCIP_Bool shallProbingBeUsed(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_BRANCHRULE*      branchrule,         /**< branching rule */
   SCIP_VAR*             var                 /**< variable to increase number of branchings on */
   )
{
   SCIP_BRANCHRULEDATA* branchruledata;
   int pos;
   int maxdepth;
   int nvarprobings;
   int nvarbranchings;

   assert(scip != NULL);
   assert(branchrule != NULL);
   assert(var != NULL);

   /* get branching rule data */
   branchruledata = SCIPbranchruleGetData(branchrule);
   assert(branchruledata != NULL);

   assert(SCIPhashmapExists(branchruledata->varhashmap, var) );

   pos = (int)(size_t) SCIPhashmapGetImage(branchruledata->varhashmap, var);
   
   maxdepth = SCIPgetMaxDepth(scip);
   nvarprobings = branchruledata->nvarprobings[pos];
   nvarbranchings = branchruledata->nvarbranchings[pos];
   
   //return ( nvarprobings < (SCIP_Real) branchruledata->reliability * ( 1. - log(nvarprobings)/log(2) /maxdepth) );

   //return 1. - (log(nvarprobings)/log(2)) / maxdepth < branchruledata->reliability;

   if( SCIPgetDepth(scip) <= 2 )
      return TRUE;
   
   return (nvarprobings+nvarbranchings) / (branchruledata->nbranchings+1) < branchruledata->reliability;


   //  return (log(nvarprobings+nvarbranchings)/log(2)) / maxdepth < branchruledata->reliability;
}  


/** execute generalized reliability pseudo cost probing branching */
static
SCIP_RETCODE execRelpsprob(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_BRANCHRULE*      branchrule,         /**< branching rule */
   SCIP_Bool             allowaddcons,       /**< is the branching rule allowed to add constraints to the current node
                                              *   in order to cut off the current solution instead of creating a branching? */
   SCIP_VAR**            branchcands,        /**< branching candidates */
   SCIP_Real*            branchcandssol,     /**< solution value for the branching candidates */
   SCIP_Real*            branchcandsfrac,    /**< fractional part of the branching candidates, zero possible */
   int                   nbranchcands,       /**< number of branching candidates */
   int                   nvars,              /**< number of variables to be watched be bdchgdata */
   SCIP_RESULT*          result,             /**< pointer to the result of the execution */
   SCIP_VAR**            branchvar           /**< pointer to the variable to branch on */
   )
{
   SCIP* masterscip;
   SCIP_BRANCHRULEDATA* branchruledata;
   BDCHGDATA* bdchgdata;
   SCIP_Real lpobjval;
   SCIP_Real cutoffbound;
   SCIP_Real bestsbdown;
   SCIP_Real bestsbup;
   SCIP_Real provedbound;
   SCIP_Bool bestsbdownvalid;
   SCIP_Bool bestsbupvalid;
   SCIP_Bool bestisstrongbranch;
   int ninitcands;
   int bestcand;

   *result = SCIP_DIDNOTRUN;

   /* get SCIP pointer of master problem */
   masterscip = GCGrelaxGetMasterprob(scip);
   assert(masterscip != NULL);

   /* get branching rule data */
   branchruledata = SCIPbranchruleGetData(branchrule);
   assert(branchruledata != NULL);

   /* add all branching candidates into branchruledata if not yet inserted */
   SCIP_CALL( addBranchcandsToData(scip, branchrule, branchcands, nbranchcands) );

   bdchgdata = NULL;
   /* create data structure for bound change infos */
   SCIP_CALL( createBdchgData(scip, &bdchgdata, branchcands, nvars) );
   assert(bdchgdata != NULL);

   /* get current LP objective bound of the local sub problem and global cutoff bound */
   lpobjval = SCIPgetLocalLowerbound(scip);
   cutoffbound = SCIPgetCutoffbound(scip);

   bestcand = -1;
   bestisstrongbranch = FALSE;
   bestsbdown = SCIP_INVALID;
   bestsbup = SCIP_INVALID;
   bestsbdownvalid = FALSE;
   bestsbupvalid = FALSE;
   provedbound = lpobjval;

   if( nbranchcands == 1 )
   {
      /* only one candidate: nothing has to be done */
      bestcand = 0;
      ninitcands = 0;
   }
   else
   {
      SCIP_Real* initcandscores;
      int* initcands;
      int maxninitcands;
      int nuninitcands;
      int nbdchgs;
      SCIP_Real avgconflictscore;
      SCIP_Real avgconflengthscore;
      SCIP_Real avginferencescore;
      SCIP_Real avgcutoffscore;
      SCIP_Real avgpscostscore;
      SCIP_Real bestpsscore;
      SCIP_Real bestpsfracscore;
      SCIP_Real bestpsdomainscore;
      SCIP_Real bestsbscore;
      SCIP_Real bestuninitsbscore;
      SCIP_Real bestsbfracscore;
      SCIP_Real bestsbdomainscore;
      SCIP_Longint nodenum;
      SCIP_Bool usesb;
      int ninfprobings;
      int maxbdchgs;
      int bestpscand;
      int bestsbcand;
      int inititer;
      int i;
      int c;

      /* get average conflict, inference, and pseudocost scores */
      avgconflictscore = SCIPgetAvgConflictScore(scip);
      avgconflictscore = MAX(avgconflictscore, 0.1);
      avgconflengthscore = SCIPgetAvgConflictlengthScore(scip);
      avgconflengthscore = MAX(avgconflengthscore, 0.1);
      avginferencescore = SCIPgetAvgInferenceScore(scip);
      avginferencescore = MAX(avginferencescore, 0.1);
      avgcutoffscore = SCIPgetAvgCutoffScore(scip);
      avgcutoffscore = MAX(avgcutoffscore, 0.1);
      avgpscostscore = SCIPgetAvgPseudocostScore(scip);
      avgpscostscore = MAX(avgpscostscore, 0.1);

      /* get maximal number of candidates to initialize with strong branching; if the current solutions is not basic,
       * we cannot apply the simplex algorithm and therefore don't initialize any candidates
       */
      maxninitcands = MIN(nbranchcands, branchruledata->initcand);
      
      if( !SCIPisLPSolBasic(masterscip) )
      {
         maxninitcands = 0;
         printf("solution is not basic\n");
      }

      printf("maxninitcands = %d\n", maxninitcands);
      
      /* get buffer for storing the unreliable candidates */
      SCIP_CALL( SCIPallocBufferArray(scip, &initcands, maxninitcands+1) ); /* allocate one additional slot for convenience */
      SCIP_CALL( SCIPallocBufferArray(scip, &initcandscores, maxninitcands+1) );
      ninitcands = 0;

      /* get current node number */
      nodenum = SCIPgetNNodes(scip);

      /* initialize bound change arrays */
      nbdchgs = 0;
      maxbdchgs = branchruledata->maxbdchgs;

      ninfprobings = 0;


      /* search for the best pseudo cost candidate, while remembering unreliable candidates in a sorted buffer */
      nuninitcands = 0;
      bestpscand = -1;
      bestpsscore = -SCIPinfinity(scip);
      bestpsfracscore = -SCIPinfinity(scip);
      bestpsdomainscore = -SCIPinfinity(scip);
      for( c = 0; c < nbranchcands; ++c )
      {
         SCIP_Real conflictscore;
         SCIP_Real conflengthscore;
         SCIP_Real inferencescore;
         SCIP_Real cutoffscore;
         SCIP_Real pscostscore;
         SCIP_Real score;
         

         assert(branchcands[c] != NULL);
         
         /* get conflict, inference, cutoff, and pseudo cost scores for candidate */
         conflictscore = SCIPgetVarConflictScore(scip, branchcands[c]);
         conflengthscore = SCIPgetVarConflictlengthScore(scip, branchcands[c]);
         inferencescore = SCIPgetVarAvgInferenceScore(scip, branchcands[c]);
         cutoffscore = SCIPgetVarAvgCutoffScore(scip, branchcands[c]);
         pscostscore = SCIPgetVarPseudocostScore(scip, branchcands[c], branchcandssol[c]);


         /* combine the four score values */
         score = calcScore(scip, branchruledata, conflictscore, avgconflictscore, conflengthscore, avgconflengthscore, 
            inferencescore, avginferencescore, cutoffscore, avgcutoffscore, pscostscore, avgpscostscore, branchcandsfrac[c]);
         
         /* just for testing: variable dependent reliability-probing */
         usesb = shallProbingBeUsed(scip, branchrule, branchcands[c]);
         usesb = TRUE;

         if( usesb )
         {
            int j;

            //printf("var <%s> is not reliable\n",SCIPvarGetName(branchcands[c]));
            /* pseudo cost of variable is not reliable: insert candidate in initcands buffer */
            for( j = ninitcands; j > 0 && score > initcandscores[j-1]; --j )
            {
               initcands[j] = initcands[j-1];
               initcandscores[j] = initcandscores[j-1];
            }
            initcands[j] = c;
            initcandscores[j] = score;
            ninitcands++;
            ninitcands = MIN(ninitcands, maxninitcands);
         }
         else
         {
            /* variable will keep it's pseudo cost value: check for better score of candidate */
            if( SCIPisSumGE(scip, score, bestpsscore) )
            {
               SCIP_Real fracscore;
               SCIP_Real domainscore;

               fracscore = MIN(branchcandsfrac[c], 1.0 - branchcandsfrac[c]);
               domainscore = -(SCIPvarGetUbLocal(branchcands[c]) - SCIPvarGetLbLocal(branchcands[c]));
               if( SCIPisSumGT(scip, score, bestpsscore)
                  || SCIPisSumGT(scip, fracscore, bestpsfracscore)
                  || (SCIPisSumGE(scip, fracscore, bestpsfracscore) && domainscore > bestpsdomainscore) )
               {
                  bestpscand = c;
                  bestpsscore = score;
                  bestpsfracscore = fracscore;
                  bestpsdomainscore = domainscore;
               }
            }
         }
      }

      /* initialize unreliable candidates with probing,
       * search best strong branching candidate
       */
      inititer = branchruledata->inititer;
      if( inititer == 0 )
      {
         /* @todo: use high value for number of lp iterations */
         inititer = 500;
      }
      
      printf("ninitcands = %d\n", ninitcands);

      bestsbcand = -1;
      bestsbscore = -SCIPinfinity(scip);
      bestsbfracscore = -SCIPinfinity(scip);
      bestsbdomainscore = -SCIPinfinity(scip);
      for( i = 0; i < ninitcands; ++i )
      {
         SCIP_Real down;
         SCIP_Real up;
         SCIP_Real downgain;
         SCIP_Real upgain;
         SCIP_Bool downvalid;
         SCIP_Bool upvalid;
         SCIP_Bool lperror;
         SCIP_Bool downinf;
         SCIP_Bool upinf;
         
         lperror = FALSE;
         up = 0.;
         down = 0.;

         /* get candidate number to initialize */
         c = initcands[i];
         
         SCIPdebugMessage("init pseudo cost (%g/%g) of <%s> with bounds [%g,%g] at %g (score:%g)\n",
            SCIPgetVarPseudocostCountCurrentRun(scip, branchcands[c], SCIP_BRANCHDIR_DOWNWARDS), 
            SCIPgetVarPseudocostCountCurrentRun(scip, branchcands[c], SCIP_BRANCHDIR_UPWARDS), 
            SCIPvarGetName(branchcands[c]), SCIPvarGetLbLocal(branchcands[c]), SCIPvarGetUbLocal(branchcands[c]),
            branchcandssol[c], initcandscores[i]);

         /* try branching on this variable (propagation + lp solving (pricing) ) */
         SCIP_CALL( SCIPgetVarProbingbranch(scip, branchcands[c], bdchgdata, inititer, branchruledata->uselp, &branchruledata->nlpiterations,
               &down, &up, &downvalid, &upvalid, &downinf, &upinf, &lperror, &nbdchgs) );

         branchruledata->nprobingnodes++;
         branchruledata->nprobingnodes++;
         SCIP_CALL( incNVarProbings(scip, branchrule, branchcands[c]) );

         /* check for an error in strong branching */
         if( lperror )
         {
            if( !SCIPisStopped(scip) )
            {
               SCIPverbMessage(scip, SCIP_VERBLEVEL_HIGH, NULL,
                  "(node %"SCIP_LONGINT_FORMAT") error in strong branching call for variable <%s> with solution %g\n", 
                  SCIPgetNNodes(scip), SCIPvarGetName(branchcands[c]), branchcandssol[c]);
            }
            break;
         }

         
         if( SCIPisStopped(scip) )
         {
            break;
         }               

         /* check if there are infeasible roundings */
         if( downinf && upinf )
         {
            /* both roundings are infeasible -> node is infeasible */
            SCIPdebugMessage(" -> variable <%s> is infeasible in both directions\n",
               SCIPvarGetName(branchcands[c]));
            printf(" -> variable <%s> is infeasible in both directions\n",
               SCIPvarGetName(branchcands[c]));
            *result = SCIP_CUTOFF;
            break; /* terminate initialization loop, because node is infeasible */
         }


         /* evaluate strong branching */
         down = MAX(down, lpobjval);
         up = MAX(up, lpobjval);
         downgain = down - lpobjval;
         upgain = up - lpobjval;
         assert(!downvalid || downinf == SCIPisGE(scip, down, cutoffbound));
         assert(!upvalid || upinf == SCIPisGE(scip, up, cutoffbound));
         
         /* the minimal lower bound of both children is a proved lower bound of the current subtree */
         if( downvalid && upvalid )
         {
            SCIP_Real minbound;
            
            minbound = MIN(down, up);
            provedbound = MAX(provedbound, minbound);
         }

         
         /* terminate initialization loop, if enough roundings are performed */
         // if( maxbdchgs >= 0 && nbdchgs >= maxbdchgs )
//             break; 
         
         /* case one rounding is infeasible is regarded in method SCIPgetVarProbingbranch */
         if( downinf || upinf )
         {
            branchruledata->ninfprobings++;
            ninfprobings++;
         }

         /* if both roundings are valid, update scores */
         if( !downinf && !upinf )
         {
            SCIP_Real conflictscore;
            SCIP_Real conflengthscore;
            SCIP_Real inferencescore;
            SCIP_Real cutoffscore;
            SCIP_Real pscostscore;
            SCIP_Real score;

            /* check for a better score */
            conflictscore = SCIPgetVarConflictScore(scip, branchcands[c]);
            conflengthscore = SCIPgetVarConflictlengthScore(scip, branchcands[c]);
            inferencescore = SCIPgetVarAvgInferenceScore(scip, branchcands[c]);
            cutoffscore = SCIPgetVarAvgCutoffScore(scip, branchcands[c]);
            pscostscore = SCIPgetBranchScore(scip, branchcands[c], downgain, upgain);
            score = calcScore(scip, branchruledata, conflictscore, avgconflictscore, conflengthscore, avgconflengthscore, 
               inferencescore, avginferencescore, cutoffscore, avgcutoffscore, pscostscore, avgpscostscore, branchcandsfrac[c]);

            if( SCIPisSumGE(scip, score, bestsbscore) )
            {
               SCIP_Real fracscore;
               SCIP_Real domainscore;
               
               fracscore = MIN(branchcandsfrac[c], 1.0 - branchcandsfrac[c]);
               domainscore = -(SCIPvarGetUbLocal(branchcands[c]) - SCIPvarGetLbLocal(branchcands[c]));
               if( SCIPisSumGT(scip, score, bestsbscore)
                  || SCIPisSumGT(scip, fracscore, bestsbfracscore)
                  || (SCIPisSumGE(scip, fracscore, bestsbfracscore) && domainscore > bestsbdomainscore) )
               {
                  bestsbcand = c;
                  bestsbscore = score;
                  bestsbdown = down;
                  bestsbup = up;
                  bestsbdownvalid = downvalid;
                  bestsbupvalid = upvalid;
                  bestsbfracscore = fracscore;
                  bestsbdomainscore = domainscore;
               }
            }
               
            /* update pseudo cost values */
            assert(!SCIPisFeasNegative(scip, branchcandsfrac[c]));
            SCIP_CALL( SCIPupdateVarPseudocost(scip, branchcands[c], 0.0-branchcandsfrac[c], downgain, 1.0) );
            SCIP_CALL( SCIPupdateVarPseudocost(scip, branchcands[c], 1.0-branchcandsfrac[c], upgain, 1.0) );
            
            SCIPdebugMessage(" -> variable <%s> (solval=%g, down=%g (%+g), up=%g (%+g), score=%g/ %g/%g %g/%g -> %g)\n",
               SCIPvarGetName(branchcands[c]), branchcandssol[c], down, downgain, up, upgain, 
               pscostscore, conflictscore, conflengthscore, inferencescore, cutoffscore,  score);
            
         }
      }
#ifdef SCIP_DEBUG
      if ( bestsbcand >= 0 )
      {
	 SCIPdebugMessage(" -> best: <%s> (%g / %g / %g)\n",
	    SCIPvarGetName(branchcands[bestsbcand]), bestsbscore, bestsbfracscore, bestsbdomainscore);
      }
#endif
      if ( bestsbcand >= 0 )
      {
         printf(" -> best: <%s> (%g / %g / %g)\n",
            SCIPvarGetName(branchcands[bestsbcand]), bestsbscore, bestsbfracscore, bestsbdomainscore);     
      }

      /* get the score of the best uninitialized strong branching candidate */
      if( i < ninitcands )
         bestuninitsbscore = initcandscores[i];
      else
         bestuninitsbscore = -SCIPinfinity(scip);

      /* if the best pseudo cost candidate is better than the best uninitialized strong branching candidate,
       * compare it to the best initialized strong branching candidate
       */
      if( bestpsscore > bestuninitsbscore && SCIPisSumGT(scip, bestpsscore, bestsbscore) )
      {
         bestcand = bestpscand;
         bestisstrongbranch = FALSE;
      }
      else if( bestsbcand >= 0 )
      {
         bestcand = bestsbcand;
         bestisstrongbranch = TRUE;
      }
      else
      {
         /* no candidate was initialized, and the best score is the one of the first candidate in the initialization
          * queue
          */
         assert(ninitcands >= 1);
         bestcand = initcands[0];
         bestisstrongbranch = FALSE;
      }

      /* apply domain reductions */
      if( (nbdchgs >= branchruledata->minbdchgs || ninfprobings >= 5) 
         && *result != SCIP_CUTOFF && !SCIPisStopped(scip) )
      {      
         SCIP_CALL( applyBdchgs(scip, bdchgdata, SCIPgetCurrentNode(scip)) );
         branchruledata->nresolvesminbdchgs++;
         // *result = SCIP_REDUCEDDOM;
      }
      
      /* free buffer for the unreliable candidates */
      SCIPfreeBufferArray(scip, &initcandscores);
      SCIPfreeBufferArray(scip, &initcands);
   }

   
   /* check whether the best branching candidate leads to an infeasible rounding; if so: apply bdchgs and solve node again */
   // if( *result != SCIP_CUTOFF && *result != SCIP_REDUCEDDOM 
   //    && *result != SCIP_CONSADDED && !SCIPisStopped(scip) )
   // {
   //    int pos;
      
   //    assert(*result == SCIP_DIDNOTRUN);
   //    assert(0 <= bestcand && bestcand < nbranchcands);
   //    assert( SCIPhashmapExists(bdchgdata->varhashmap, branchcands[bestcand]) );
   
   //    pos = (int)(size_t) SCIPhashmapGetImage(bdchgdata->varhashmap, branchcands[bestcand]);

   //    if( bdchgdata->infroundings[pos] )
   //    {
   //       SCIP_CALL( applyBdchgs(scip, bdchgdata, SCIPgetCurrentNode(scip)) );
   //       branchruledata->nresolvesinfcands++;
   //       // *result = SCIP_REDUCEDDOM;
   //    }
   // }


   /* if no domain could be reduced, create the branching */
   if( *result != SCIP_CUTOFF && *result != SCIP_REDUCEDDOM 
      && *result != SCIP_CONSADDED && !SCIPisStopped(scip) )
   {
      assert(*result == SCIP_DIDNOTRUN);
      assert(0 <= bestcand && bestcand < nbranchcands);
      assert(SCIPisLT(scip, provedbound, cutoffbound));

      printf(" -> best: <%s> (strongbranch = %d)\n",
         SCIPvarGetName(branchcands[bestcand]), bestisstrongbranch);  

      *branchvar = branchcands[bestcand];
      incNVarBranchings(scip, branchrule, *branchvar);
   }
   
   /* free data structure for bound change infos */
   SCIP_CALL( freeBdchgData(scip, bdchgdata) );

   return SCIP_OKAY;
}
   

/*
 * Callback methods
 */

/** copy method for branchrule plugins (called when SCIP copies plugins) */
static
SCIP_DECL_BRANCHCOPY(branchCopyRelpsprob)
{  /*lint --e{715}*/
   assert(scip != NULL);
   assert(branchrule != NULL);
   assert(strcmp(SCIPbranchruleGetName(branchrule), BRANCHRULE_NAME) == 0);

   /* call inclusion method of branching rule  */
   SCIP_CALL( SCIPincludeBranchruleRelpsprob(scip) );
 
   return SCIP_OKAY;
}

/** destructor of branching rule to free user data (called when SCIP is exiting) */
static
SCIP_DECL_BRANCHFREE(branchFreeRelpsprob)
{  /*lint --e{715}*/
   SCIP_BRANCHRULEDATA* branchruledata;

   /* free branching rule data */
   branchruledata = SCIPbranchruleGetData(branchrule);

   //SCIPinfoMessage(scip, NULL, "**needed in total %d probing nodes\n", branchruledata->nprobingnodes);

   SCIPfreeMemory(scip, &branchruledata);
   SCIPbranchruleSetData(branchrule, NULL);

   return SCIP_OKAY;
}


/** initialization method of branching rule (called after problem was transformed) */
#define branchInitRelpsprob NULL


/** deinitialization method of branching rule (called before transformed problem is freed) */
#define branchExitRelpsprob NULL


/** solving process initialization method of branching rule (called when branch and bound process is about to begin) */
#define branchInitsolRelpsprob NULL


/** solving process deinitialization method of branching rule (called before branch and bound process data is freed) */
static
SCIP_DECL_BRANCHEXITSOL(branchExitsolRelpsprob)
{  /*lint --e{715}*/
   SCIP_BRANCHRULEDATA* branchruledata;
   
   /* free branching rule data */
   branchruledata = SCIPbranchruleGetData(branchrule);

   //SCIPinfoMessage(scip, NULL, "**in total: nprobings = %d; part of it are ninfprobings = %d\n", 
   //   branchruledata->nprobings, branchruledata->ninfprobings );
   
   //SCIPinfoMessage(scip, NULL, "**nbranchings = %d, nresolvesinfcands = %d, nresolvesminbdchgs = %d\n", 
   //   branchruledata->nbranchings, branchruledata->nresolvesinfcands, branchruledata->nresolvesminbdchgs );   


   /* free arrays for variables & hashmap */
   SCIPfreeMemoryArrayNull(scip, &branchruledata->nvarprobings);
   SCIPfreeMemoryArrayNull(scip, &branchruledata->nvarbranchings);
   branchruledata->nvars = 0;
   
   if( branchruledata->varhashmap != NULL )
   {
      SCIPhashmapFree(&(branchruledata->varhashmap));
   }

   return SCIP_OKAY;
}


/** branching execution method for fractional LP solutions */
#define branchExeclpRelpsprob NULL
#define branchExecextRelpsprob NULL

/** branching execution method for not completely fixed pseudo solutions */
#define branchExecpsRelpsprob NULL




/*
 * branching specific interface methods
 */

/** creates the reliable pseudo cost braching rule and includes it in SCIP */
SCIP_RETCODE SCIPincludeBranchruleRelpsprob(
   SCIP*                 scip                /**< SCIP data structure */
   )
{
   SCIP_BRANCHRULEDATA* branchruledata;

   /* create relpsprob branching rule data */
   SCIP_CALL( SCIPallocMemory(scip, &branchruledata) );
   
   /* include branching rule */
   SCIP_CALL( SCIPincludeBranchrule(scip, BRANCHRULE_NAME, BRANCHRULE_DESC, BRANCHRULE_PRIORITY, 
         BRANCHRULE_MAXDEPTH, BRANCHRULE_MAXBOUNDDIST, branchCopyRelpsprob,
         branchFreeRelpsprob, branchInitRelpsprob, branchExitRelpsprob, branchInitsolRelpsprob, branchExitsolRelpsprob, 
         branchExeclpRelpsprob, branchExecextRelpsprob, branchExecpsRelpsprob,
         branchruledata) );

   /* relpsprob branching rule parameters */
   SCIP_CALL( SCIPaddRealParam(scip,
         "branching/relpsprob/conflictweight", 
         "weight in score calculations for conflict score",
         &branchruledata->conflictweight, TRUE, DEFAULT_CONFLICTWEIGHT, SCIP_REAL_MIN, SCIP_REAL_MAX, NULL, NULL) );
   SCIP_CALL( SCIPaddRealParam(scip,
         "branching/relpsprob/conflictlengthweight", 
         "weight in score calculations for conflict length score",
         &branchruledata->conflengthweight, TRUE, DEFAULT_CONFLENGTHWEIGHT, SCIP_REAL_MIN, SCIP_REAL_MAX, NULL, NULL) );
   SCIP_CALL( SCIPaddRealParam(scip,
         "branching/relpsprob/inferenceweight", 
         "weight in score calculations for inference score",
         &branchruledata->inferenceweight, TRUE, DEFAULT_INFERENCEWEIGHT, SCIP_REAL_MIN, SCIP_REAL_MAX, NULL, NULL) );
   SCIP_CALL( SCIPaddRealParam(scip,
         "branching/relpsprob/cutoffweight", 
         "weight in score calculations for cutoff score",
         &branchruledata->cutoffweight, TRUE, DEFAULT_CUTOFFWEIGHT, SCIP_REAL_MIN, SCIP_REAL_MAX, NULL, NULL) );
   SCIP_CALL( SCIPaddRealParam(scip,
         "branching/relpsprob/pscostweight", 
         "weight in score calculations for pseudo cost score",
         &branchruledata->pscostweight, TRUE, DEFAULT_PSCOSTWEIGHT, SCIP_REAL_MIN, SCIP_REAL_MAX, NULL, NULL) );
   SCIP_CALL( SCIPaddRealParam(scip,
         "branching/relpsprob/minreliable", 
         "minimal value for minimum pseudo cost size to regard pseudo cost value as reliable",
         &branchruledata->minreliable, TRUE, DEFAULT_MINRELIABLE, 0.0, SCIP_REAL_MAX, NULL, NULL) );
   SCIP_CALL( SCIPaddRealParam(scip,
         "branching/relpsprob/maxreliable", 
         "maximal value for minimum pseudo cost size to regard pseudo cost value as reliable",
         &branchruledata->maxreliable, TRUE, DEFAULT_MAXRELIABLE, 0.0, SCIP_REAL_MAX, NULL, NULL) );
   SCIP_CALL( SCIPaddRealParam(scip,
         "branching/relpsprob/iterquot", 
         "maximal fraction of branching LP iterations compared to node relaxation LP iterations",
         &branchruledata->iterquot, FALSE, DEFAULT_ITERQUOT, 0.0, SCIP_REAL_MAX, NULL, NULL) );
   SCIP_CALL( SCIPaddIntParam(scip,
         "branching/relpsprob/iterofs", 
         "additional number of allowed LP iterations",
         &branchruledata->iterofs, FALSE, DEFAULT_ITEROFS, 0, INT_MAX, NULL, NULL) );
   SCIP_CALL( SCIPaddIntParam(scip,
         "branching/relpsprob/maxlookahead", 
         "maximal number of further variables evaluated without better score",
         &branchruledata->maxlookahead, TRUE, DEFAULT_MAXLOOKAHEAD, 1, INT_MAX, NULL, NULL) );
   SCIP_CALL( SCIPaddIntParam(scip,
         "branching/relpsprob/initcand", 
         "maximal number of candidates initialized with strong branching per node",
         &branchruledata->initcand, FALSE, DEFAULT_INITCAND, 0, INT_MAX, NULL, NULL) );
   SCIP_CALL( SCIPaddIntParam(scip,
         "branching/relpsprob/inititer", 
         "iteration limit for strong branching initializations of pseudo cost entries (0: auto)",
         &branchruledata->inititer, FALSE, DEFAULT_INITITER, 0, INT_MAX, NULL, NULL) );
   SCIP_CALL( SCIPaddIntParam(scip,
         "branching/relpsprob/maxbdchgs", 
         "maximal number of bound tightenings before the node is immediately reevaluated (-1: unlimited)",
         &branchruledata->maxbdchgs, TRUE, DEFAULT_MAXBDCHGS, -1, INT_MAX, NULL, NULL) );
   SCIP_CALL( SCIPaddIntParam(scip,
         "branching/relpsprob/minbdchgs", 
         "minimal number of bound tightenings before bound changes are applied",
         &branchruledata->minbdchgs, TRUE, DEFAULT_MINBDCHGS, 1, INT_MAX, NULL, NULL) );
   SCIP_CALL( SCIPaddBoolParam(scip,
         "branching/relpsprob/uselp", 
         "shall the LP be solved during probing? (TRUE)",
         &branchruledata->uselp, FALSE, DEFAULT_USELP, NULL, NULL) );
   SCIP_CALL( SCIPaddRealParam(scip,
         "branching/relpsprob/reliability", 
         "reliability value for probing ",
         &branchruledata->reliability, FALSE, DEFAULT_RELIABILITY, 0.0, 1.0, NULL, NULL) );

   branchruledata->nprobingnodes = 0;
   branchruledata->nlpiterations = 0;
   
   
   branchruledata->nprobings = 0;
   branchruledata->nbranchings = 0;
   branchruledata->ninfprobings = 0;
   branchruledata->nresolvesminbdchgs = 0; 
   branchruledata->nresolvesinfcands = 0; 
   
   branchruledata->varhashmap = NULL;
   branchruledata->nvarbranchings = NULL;
   branchruledata->nvarprobings = NULL; 
   branchruledata->nvars = 0;
   
   return SCIP_OKAY;
}

/** execution reliability pseudo cost probing branching with the given branching candidates */
SCIP_RETCODE SCIPgetRelpsprobBranchVar(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_Bool             allowaddcons,       /**< is the branching rule allowed to add constraints to the current node
                                              *   in order to cut off the current solution instead of creating a branching? */
   SCIP_VAR**            branchcands,        /**< brancing candidates */
   SCIP_Real*            branchcandssol,     /**< solution value for the branching candidates */
   SCIP_Real*            branchcandsfrac,    /**< fractional part of the branching candidates, zero possible */
   int                   nbranchcands,       /**< number of branching candidates */
   int                   nvars,              /**< number of variables to be watched by bdchgdata */
   SCIP_RESULT*          result,             /**< pointer to the result of the execution */
   SCIP_VAR**            branchvar           /**< pointer to the variable to branch on */
   )
{
   SCIP_BRANCHRULE* branchrule;

   assert(scip != NULL);
   assert(result != NULL);
   
   /* find branching rule */
   branchrule = SCIPfindBranchrule(scip, BRANCHRULE_NAME);
   assert(branchrule != NULL);
   
   /* execute branching rule */
   SCIP_CALL( execRelpsprob(scip, branchrule, allowaddcons, branchcands, branchcandssol, branchcandsfrac, nbranchcands, nvars, result, branchvar) );
   
   return SCIP_OKAY;
}