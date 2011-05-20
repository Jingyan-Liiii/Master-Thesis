/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                           */
/*                  This file is part of the program and library             */
/*         SCIP --- Solving Constraint Integer Programs                      */
/*                                                                           */
/*    Copyright (C) 2002-2010 Konrad-Zuse-Zentrum                            */
/*                            fuer Informationstechnik Berlin                */
/*                                                                           */
/*  SCIP is distributed under the terms of the ZIB Academic License.         */
/*                                                                           */
/*  You should have received a copy of the ZIB Academic License              */
/*  along with SCIP; see the file COPYING. If not email to scip@zib.de.      */
/*                                                                           */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
#pragma ident "@(#) $Id: presol_arrowheur.c,v 1.24 2010/01/04 20:35:45 bzfheinz Exp $"

/**@file   presol_arrowheur.c
 * @ingroup PRESOLVERS
 * @brief  arrowheur presolver
 * @author Tobias Achterberg
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/
//#define SCIP_DEBUG
#include <assert.h>

#include "dec_arrowheur.h"
#include "scip_misc.h"
#include "scip/scipdefplugins.h"

//#include <cstdio>
#include <math.h>
#include <string.h>
#include <errno.h>

/* Default parameter settings*/
#define DEFAULT_BLOCKS                    2     /**< number of blocks */
#define DEFAULT_VARWEIGHT                 3     /**< weight for variable nodes */
#define DEFAULT_VARWEIGHTBIN              3     /**< weight for binary variable nodes */
#define DEFAULT_VARWEIGHTINT              3     /**< weight for integer variable nodes */
#define DEFAULT_VARWEIGHTCONT             3     /**< weight for continous variable nodes */
#define DEFAULT_VARWEIGHTIMPL             3     /**< weight for implicit integer variable nodes */
#define DEFAULT_CONSWEIGHT                1     /**< weight for constraint hyperedges */
#define DEFAULT_RANDSEED                 -1     /**< random seed for the hmetis call */
#define DEFAULT_TIDY                      TRUE  /**< whether to clean up afterwards */
#define DEFAULT_DUMMYNODES	              0.2   /**< percentage of dummy vertices*/
#define DEFAULT_CONSWEIGHT_SETCOV         1     /**< weight for constraint hyperedges that are setcovering constraints */
#define DEFAULT_CONSWEIGHT_SETPACK        1     /**< weight for constraint hyperedges that are setpacking constraints */
#define DEFAULT_CONSWEIGHT_SETPART        1     /**< weight for constraint hyperedges that are setpartitioning constraints */
#define DEFAULT_CONSWEIGHT_SETPPC         0     /**< weight for constraint hyperedges that are setpartitioning or covering constraints */
//#define DEFAULT_CONSWEIGHT_HIGH_VARIATION 1     /**< weight for constraint hyperedges that have a high variation of coefficients */
#define DEFAULT_FORCE_SETPART_MASTER      FALSE /**< whether to force setpart constraints in the master */
#define DEFAULT_FORCE_SETPACK_MASTER      FALSE /**< whether to force setpack constraints in the master */
#define DEFAULT_FORCE_SETCOV_MASTER       FALSE /**< whether to force setcov constraints in the master */
//#define DEFAULT_CONSWEIGHT_LOW_VARIATION  1     /**< weight for constraint hyperedges that have a high variation of coefficients */
//#define DEFAULT_HIGH_VARIATION            0.5   /**< value which is considered a high variation */
//#define DEFAULT_LOW_VARIATION             0.1   /**< value which is considered a low variation */
#define DEFAULT_MAXBLOCKS                 10    /**< value for the maximum number of blocks to be considered */
#define DEFAULT_MINBLOCKS                 2     /**< value for the minimum number of blocks to be considered */
#define DEFAULT_ALPHA                     0.0   /**< factor for standard deviation of constraint weights */
#define DEFAULT_BETA                      0.5   /**< factor of how the weight for equality and inequality constraints is distributed (keep 1/2 for the same on both) */

#define DWSOLVER_REFNAME(name, blocks, varcont, varint, cons, dummy, alpha, beta, conssetppc)  \
   "%s_%d_%d_%d_%d_%.1f_%.1f_%.1f_%d_ref.txt", \
   (name), (blocks), (varcont), (varint), (cons), (dummy), (alpha), (beta), (conssetppc)

#define GP_NAME(name, blocks, varcont, varint, cons, dummy, alpha, beta, conssetppc)  \
   "%s_%d_%d_%d_%d_%.1f_%.1f_%.1f_%d.gp", \
   (name), (blocks), (varcont), (varint), (cons), (dummy), (alpha), (beta), (conssetppc)

/*
 * Data structures
 */

/** score data structure **/
struct SCIP_ArrowheurScores
{
   SCIP_Real borderscore;
   SCIP_Real minkequicutscore;
   SCIP_Real equicutscorenormalized;
   SCIP_Real densityscore;
   SCIP_Real linkingscore;
};
typedef struct SCIP_ArrowheurScores SCIP_ARROWHEURSCORES;
/** presolver data */
struct SCIP_ArrowheurData
{
   DECDECOMP* decdecomp;
   SCIP_VAR*** varsperblock;
   int* nvarsperblock;
   SCIP_VAR** linkingvars;
   int nlinkingvars;
   SCIP_CONS*** consperblock;
   int *nconsperblock;
   SCIP_CONS** linkingconss;
   int nlinkingconss;

   SCIP_HASHMAP* constoblock;
   SCIP_HASHMAP* varstoblock;

   /* Graph stuff for hmetis */
   SCIP_PTRARRAY *hedges;
   SCIP_INTARRAY *copytooriginal;
   int *partition;
   int nvertices;
   int *varpart;

   /* Stuff to get the dw-solver to work*/
   SCIP_HASHMAP *constolpid;

   SCIP_Bool tidy;
   SCIP_Bool callgcg;
   SCIP_Bool visualize;
   SCIP_Bool decouplevariables;
   int blocks;
   int maxblocks;
   int minblocks;
   int varWeight;
   int varWeightBinary;
   int varWeightContinous;
   int varWeightInteger;
   int varWeightImplint;
   int consWeight;
   int randomseed;
   SCIP_Bool found;
   SCIP_Real dummynodes;
   int consWeightSetppc;
//   int consWeightSetpart;
//   int consWeightSetpack;
//   int consWeightSetcov;
//   SCIP_Bool forceSetpackMaster;
//   SCIP_Bool forceSetpartMaster;
//   SCIP_Bool forceSetcovMaster;
   SCIP_Real alpha;
   SCIP_Real beta;
//   int consWeightHighVariation;
//   int consWeightLowVariation;
//   SCIP_Real highVariation;
//   SCIP_Real lowVariation;
};

enum htype
{
   VARIABLE, CONSTRAINT
};
typedef enum htype hType;

struct hyperedge
{

   hType type;       ///< The type of the hyperegde (is it a split variable or a real constraint)
   int *variableIds; ///< the associated variable IDs that appear in the hyperedge
  // int copyId;       ///< The ids of the associated copy
   int nvariableIds; ///< number of variable ids
   int originalId;   ///< the original SCIP ID of this constraint or variable

   int cost;

};
typedef struct hyperedge HyperEdge;

/*
 * Local methods
 */

/* put your local methods here, and declare them static */

static
SCIP_RETCODE printArrowheurScores(
      SCIP*                 scip,
      SCIP_ARROWHEURDATA*   arrowheurdata,
      SCIP_ARROWHEURSCORES* scores
      )
{
   char name[SCIP_MAXSTRLEN];
   SCIPsnprintf(name, SCIP_MAXSTRLEN, DWSOLVER_REFNAME(SCIPgetProbName(scip),
         arrowheurdata->blocks,
         arrowheurdata->varWeightContinous,
         arrowheurdata->varWeightInteger,
         arrowheurdata->consWeight,
         arrowheurdata->dummynodes,
         arrowheurdata->alpha,
         arrowheurdata->beta,
         arrowheurdata->consWeightSetppc));

   SCIPinfoMessage(scip, NULL, "SCORES:\t%s\t%s\t%f\t%f\t%f\t%f\t%f\n",
         SCIPgetProbName(scip), name,
         scores->borderscore,
         scores->densityscore,
         scores->linkingscore,
         scores->minkequicutscore,
         scores->equicutscorenormalized);

   return SCIP_OKAY;
}

static
SCIP_RETCODE initArrowheurData(
      SCIP* scip,
      SCIP_ARROWHEURDATA* arrowheurdata
      )

{

   int i;
   int nvars;
   int nconss;

   assert(scip != NULL);
   assert(arrowheurdata != NULL);

   nvars = SCIPgetNVars(scip);
   nconss = SCIPgetNConss(scip);
   arrowheurdata->maxblocks = MIN(nconss, arrowheurdata->maxblocks);
   /* initialize variables and constraints per block structures*/
   SCIP_CALL(SCIPallocMemoryArray(scip, &arrowheurdata->consperblock, arrowheurdata->maxblocks));
   SCIP_CALL(SCIPallocMemoryArray(scip, &arrowheurdata->varsperblock, arrowheurdata->maxblocks));
   SCIP_CALL(SCIPallocMemoryArray(scip, &arrowheurdata->nconsperblock, arrowheurdata->maxblocks));
   SCIP_CALL(SCIPallocMemoryArray(scip, &arrowheurdata->nvarsperblock, arrowheurdata->maxblocks));
   SCIP_CALL(SCIPallocMemoryArray(scip, &arrowheurdata->linkingconss, nconss));
   SCIP_CALL(SCIPallocMemoryArray(scip, &arrowheurdata->linkingvars, nvars));
   SCIP_CALL(SCIPallocMemoryArray(scip, &arrowheurdata->varpart, nvars));
   for(i = 0; i < nvars; ++i)
   {
      arrowheurdata->varpart[i] = -1;
   }
   SCIP_CALL(SCIPcreatePtrarray(scip, &arrowheurdata->hedges));
   SCIP_CALL(SCIPcreateIntarray(scip, &arrowheurdata->copytooriginal));
   for( i = 0; i < arrowheurdata->maxblocks; ++i)
   {
      arrowheurdata->nvarsperblock[i] = 0;
      arrowheurdata->nconsperblock[i] = 0;
      SCIP_CALL(SCIPallocMemoryArray(scip, &arrowheurdata->consperblock[i], nconss));
      SCIP_CALL(SCIPallocMemoryArray(scip, &arrowheurdata->varsperblock[i], nvars));
   }

   arrowheurdata->nlinkingconss = 0;
   arrowheurdata->nlinkingvars = 0;

   /* create variable and constraint hash tables */
   SCIP_CALL(SCIPhashmapCreate(&arrowheurdata->varstoblock, SCIPblkmem(scip), nvars));
   SCIP_CALL(SCIPhashmapCreate(&arrowheurdata->constoblock, SCIPblkmem(scip), nconss));
   SCIP_CALL(SCIPhashmapCreate(&arrowheurdata->constolpid, SCIPblkmem(scip), nconss));

   /* initialise consttolpid hashmap */
   for( i = 0; i < nconss; ++i)
   {
      SCIP_CALL(SCIPhashmapInsert(arrowheurdata->constolpid, SCIPgetConss(scip)[i], (void*)(size_t)i));
   }
   return SCIP_OKAY;
}

/** copies the variable and block information to the decomp structure */
static
SCIP_RETCODE copyArrowheurDataToDecomp(
      SCIP*             scip,       /**< SCIP data structure */
      SCIP_ARROWHEURDATA*  arrowheurdata, /**< presolver data data structure */
      DECDECOMP*        decomp      /**< DECOMP data structure */
      )
{
   int i;
   assert(scip != 0);
   assert(arrowheurdata != 0);
   assert(decomp != 0);

   SCIP_CALL(SCIPallocMemoryArray(scip, &decomp->subscipvars, arrowheurdata->blocks));
   SCIP_CALL(SCIPallocMemoryArray(scip, &decomp->subscipconss, arrowheurdata->blocks));

   SCIP_CALL(SCIPduplicateMemoryArray(scip, &decomp->linkingconss, arrowheurdata->linkingconss, arrowheurdata->nlinkingconss));
   SCIP_CALL(SCIPduplicateMemoryArray(scip, &decomp->linkingvars, arrowheurdata->linkingvars,  arrowheurdata->nlinkingvars));
   decomp->nlinkingconss = arrowheurdata->nlinkingconss;
   decomp->nlinkingvars = arrowheurdata->nlinkingvars;

   for( i = 0; i < arrowheurdata->blocks; ++i)
   {
      SCIP_CALL(SCIPduplicateMemoryArray(scip, &decomp->subscipconss[i], arrowheurdata->consperblock[i], arrowheurdata->nconsperblock[i]));
      SCIP_CALL(SCIPduplicateMemoryArray(scip, &decomp->subscipvars[i], arrowheurdata->varsperblock[i], arrowheurdata->nvarsperblock[i]));
   }

   SCIP_CALL(SCIPduplicateMemoryArray(scip, &decomp->nsubscipconss, arrowheurdata->nconsperblock, arrowheurdata->blocks));
   SCIP_CALL(SCIPduplicateMemoryArray(scip, &decomp->nsubscipvars, arrowheurdata->nvarsperblock, arrowheurdata->blocks));

   decomp->constoblock = arrowheurdata->constoblock;
   decomp->vartoblock = arrowheurdata->varstoblock;
   decomp->nblocks = arrowheurdata->blocks;
   decomp->type = DEC_ARROWHEAD;
   return SCIP_OKAY;
}

/** presolving deinitialization method of presolver (called after presolving has been finished) */
static
SCIP_RETCODE freeArrowheurDataData
(
      SCIP* scip,
      SCIP_ARROWHEURDATA* arrowheurdata
)
{

   int i;

   assert(scip != NULL);
   assert(arrowheurdata != NULL);

   /* quick fix to correct the pictures */
//   SCIP_CALL(detectAndBuildArrowHead(scip, arrowheurdata));

   /* copy data to decomp structure */
   if( !arrowheurdata->found)
   {
      return SCIP_OKAY;
   }

   SCIP_CALL(copyArrowheurDataToDecomp(scip, arrowheurdata, arrowheurdata->decdecomp));
   /* free presolver data */
   for( i = 0; i < arrowheurdata->maxblocks; ++i)
   {
      SCIPfreeMemoryArray(scip, &arrowheurdata->consperblock[i]);
      SCIPfreeMemoryArray(scip, &arrowheurdata->varsperblock[i]);
   }
   SCIPfreeMemoryArray(scip, &arrowheurdata->varsperblock);
   SCIPfreeMemoryArray(scip, &arrowheurdata->nvarsperblock);
   SCIPfreeMemoryArray(scip, &arrowheurdata->consperblock);
   SCIPfreeMemoryArray(scip, &arrowheurdata->nconsperblock);
   SCIPfreeMemoryArray(scip, &arrowheurdata->linkingconss);
   SCIPfreeMemoryArray(scip, &arrowheurdata->linkingvars);
   SCIPfreeMemoryArray(scip, &arrowheurdata->partition);
   SCIPfreeMemoryArray(scip, &arrowheurdata->varpart);

   /* free hash map */
   SCIPhashmapFree(&arrowheurdata->constolpid);
   /* TODO: Hashmap is not copied! but shallow copied, so do not free here!
   SCIPhashmapFree(&arrowheurdata->varstoblock);
   SCIPhashmapFree(&arrowheurdata->constoblock);
   */

   /* free dynamic arrays */
   for( i = 0; i <= SCIPgetPtrarrayMaxIdx(scip, arrowheurdata->hedges); ++i)
   {
      HyperEdge* hedge;
      hedge = (HyperEdge*) SCIPgetPtrarrayVal(scip, arrowheurdata->hedges, i);
      assert(hedge != NULL);

      SCIPfreeMemoryArray(scip, &hedge->variableIds);
      SCIPfreeMemory(scip, &hedge);
   }
   SCIPfreePtrarray(scip, &arrowheurdata->hedges);
   SCIPfreeIntarray(scip, &arrowheurdata->copytooriginal);

   return SCIP_OKAY;
}

static
int computeHyperedgeWeight(SCIP *scip, SCIP_ARROWHEURDATA *arrowheurdata, SCIP_CONS* cons, int *cost)
{
   int j;
   int ncurvars;
   SCIP_CONS* upgdcons;
   const char* hdlrname;
   *cost = arrowheurdata->consWeight;

   SCIP_CALL(SCIPupgradeConsLinear(scip, cons, &upgdcons));

   if(upgdcons != NULL)
   {
     hdlrname =  SCIPconshdlrGetName(SCIPconsGetHdlr(upgdcons));
   }
   else
   {
     hdlrname =  SCIPconshdlrGetName(SCIPconsGetHdlr(cons));
   }
   ncurvars = SCIPgetNVarsXXX(scip, cons);

   if((strcmp("setppc", hdlrname) == 0))
   {
      switch(SCIPgetTypeSetppc(scip, upgdcons))
      {
      case SCIP_SETPPCTYPE_COVERING:
         *cost = arrowheurdata->consWeightSetppc;
         break;

      case SCIP_SETPPCTYPE_PARTITIONING:
         *cost = arrowheurdata->consWeightSetppc;
         break;
      default:
         *cost = arrowheurdata->consWeight;
           ;
      }
   }
   else if(strcmp(hdlrname, "logicor") == 0)
   {
      *cost = arrowheurdata->consWeightSetppc;
   }
   else
   {
      double mean;
      double variance;
      double stddev;
      double varcoeff;
      SCIP_Real * vals;

      mean = 0.0;
      variance = 0.0;
      varcoeff = 0.0;
      vals = SCIPgetValsXXX(scip, cons);

      *cost = arrowheurdata->consWeight;

      /* calculate variety using the normalized variance */
      for( j = 0; j < ncurvars; ++j)
      {
         mean += vals[j] / ncurvars;
      }
      if( ncurvars <= 1 )
      {
         variance = 0.0;
      }
      else
      {
         for( j = 0; j < ncurvars; ++j)
         {
            variance += pow((vals[j] - mean), 2) / (ncurvars-1);
         }
      }
      assert(variance >= 0);
      stddev = sqrt(variance);
      SCIPfreeMemoryArray(scip, &vals);

      // TODO: MAGIC NUMBER
      if(SCIPisEQ(scip, SCIPgetRhsXXX(scip, cons), SCIPgetLhsXXX(scip, cons)))
      {
         /* we are dealing with an equality*/
         *cost = SCIPceil(scip, arrowheurdata->beta*2*arrowheurdata->consWeight+arrowheurdata->alpha*stddev);
      }
      else
      {
         *cost = SCIPceil(scip, (1.0-arrowheurdata->beta)*2*arrowheurdata->consWeight+arrowheurdata->alpha*stddev);
      }

   }
   if(upgdcons != NULL)
   {
      SCIP_CALL(SCIPreleaseCons(scip, &upgdcons));
   }
   return SCIP_OKAY;
}

/**
 * Builds a graph structure out of the matrix.
 *
 * The function will create an HyperEdge for every constraint and every variable.
 * It will additionally create vertices for every variable and in particular
 * a copy of this variable for every constraint in which the variable has a
 * nonzero coefficient. The copies will be connected by the hyperedge for
 * the particular constraint and all copies of a variable will be connected by
 * the hyperedge belonging to that variable. The weight of these variable
 * hyperedges can be specified.
 *
 * @todo The nonzeroness is not checked, all variables in the variable array are considered
 */
static SCIP_RETCODE buildGraphStructure(
      SCIP*             scip,       /**< SCIP data structure */
      SCIP_ARROWHEURDATA*  arrowheurdata  /**< presolver data data structure */
      )
{
   SCIP_CONS **conss;
   SCIP_PTRARRAY *hedges;
   SCIP_INTARRAY *copytoorig;
   SCIP_INTARRAY** maporigtocopies;
   int *nmaporigtocopies;
   int nconss = SCIPgetNConss( scip );
   int nvars = SCIPgetNVars( scip );
   int id = 0;
   int nhyperedges = 0;
   int nvertices;
   int i;
   int j;
   int varWeight;
   int *copies;
   int ncopies;

   HyperEdge *hedge;
   assert(scip != NULL);
   assert(arrowheurdata != NULL);

   conss = SCIPgetConss(scip);
   hedges = arrowheurdata->hedges;
   copytoorig = arrowheurdata->copytooriginal;
   nvertices = 0;
   varWeight =  arrowheurdata->varWeight;
   SCIP_CALL(SCIPallocMemoryArray(scip, &copies, nconss));
   ncopies = 0;
   /* we need at least nconss + nvars hyperedges */
   SCIP_CALL(SCIPextendPtrarray(scip, hedges,  0, nconss+nvars));
   /* we have at least nvars may copy vertices */
   SCIP_CALL(SCIPextendIntarray(scip, copytoorig, 0, nvars));

   /* map the original variable to all of its copies */
   SCIP_CALL(SCIPallocMemoryArray(scip, &maporigtocopies, nvars));

   /* these are the number of copies for the given variable */
   SCIP_CALL(SCIPallocMemoryArray(scip, &nmaporigtocopies, nvars));

   /* initialize for every variable the list of copies */
   for( i = 0; i < nvars; ++i )
   {
      SCIP_CALL(SCIPcreateIntarray(scip, &maporigtocopies[i]));
      nmaporigtocopies[i] = 0;
   }

   /* go through all constraints */
   for( i = 0; i < nconss; ++i )
   {
      int *varids;
      SCIP_VAR **vars;
      /* get the number of nonzeros in this constraint  */

      int ncurvars = SCIPgetNVarsXXX( scip, conss[i] );

      /* if there are no variables, skip the constraint */
      if( ncurvars == 0 )
         continue;

      /*
       * may work as is, as we are copying the constraint later regardless
       * if there are variables in it or not
       */
      vars = SCIPgetVarsXXX( scip, conss[i] );

      /* TODO: skip all variables that have a zero coeffient or where all coefficients add to zero */
      /* TODO: Do more then one entry per variable actually work? */

      /* allocate a hyperedge for the constraint */
      SCIP_CALL(SCIPallocMemory(scip, &hedge));
      hedge->type = CONSTRAINT;
      hedge->originalId = i;

      /* compute its weight*/
      SCIP_CALL(computeHyperedgeWeight(scip, arrowheurdata, conss[i], &(hedge->cost)));

      /* lets collect the variable ids of the variables */
      SCIP_CALL(SCIPallocMemoryArray(scip, &varids, ncurvars));
      hedge->nvariableIds = 0;

      for( j = 0; j < ncurvars; ++j )
      {
         int varIndex = SCIPvarGetProbindex(vars[j]);

         /* if the variable is inactive, skip it */
         if(varIndex == -1)
         {
            continue;
         }
         /* assert that the variable is active and not multiaggregated, otherwise, the mapping will be wrong */
         /* the multiaggregation is useless, if we don't presolve, it might be interesting otherwise */
         assert(SCIPvarIsActive(vars[j]));
         assert(SCIPvarGetStatus(vars[j]) != SCIP_VARSTATUS_MULTAGGR);

         /* add the variable id to the end of the variable id array and update the size */
         varids[hedge->nvariableIds] = id;
         ++(hedge->nvariableIds);

         /* put the copied id (started from 0, should be the index of the nonzero entry) to the end of the map of original to copy ids  */
         SCIP_CALL(SCIPsetIntarrayVal(scip, maporigtocopies[varIndex], nmaporigtocopies[varIndex], id));
         ++(nmaporigtocopies[varIndex]);
         SCIP_CALL(SCIPsetIntarrayVal(scip, copytoorig, id, varIndex));
         ++id;
         /* Check the mapping here */
#ifdef SCIP_DEBUG
         {
            int k;
            SCIPdebugPrintf("Cons: %d: ", i);
            SCIPdebugPrintf("Var: %d: ", varIndex);
            for (k = 0; k < nmaporigtocopies[varIndex]; ++k)
            {
               int copy;
               int orig;
               copy = SCIPgetIntarrayVal(scip, maporigtocopies[varIndex], k);
               orig = SCIPgetIntarrayVal(scip, copytoorig, copy);
               SCIPdebugPrintf("%d (%d), ", copy+1, orig);
               assert(varIndex == orig);
            }
            SCIPdebugPrintf("\n");
         }
#endif
      }





      if(hedge->nvariableIds > 1)
      {
         /* if the hyperedge contains more then 0 variables, add it to the end */
         SCIP_CALL(SCIPduplicateMemoryArray(scip, &hedge->variableIds, varids, hedge->nvariableIds));
         SCIP_CALL(SCIPsetPtrarrayVal(scip, hedges, nhyperedges, hedge));
         ++nhyperedges;
      }
      else
      {
         SCIPfreeMemory(scip, &hedge);
      }
      SCIPfreeMemoryArray(scip, &varids);
      SCIPfreeMemoryArray(scip, &vars);


   }

   /* build variable hyperedges */
   for( i = 0; i < nvars; i++ )
   {
      /*
       * handle variables only appearing in the objective
       * this is in a sense useless, as it increases the work for metis, but
       * it is consistent this way
       */
      int size;
      size = nmaporigtocopies[i];
      assert( size >= 0);

      /* if the hyperedge contains only 1 variable or less, skip it */
      if(size <= 1)
         continue;

      switch ( SCIPvarGetType( SCIPgetVars( scip )[i] ) ) {
      case SCIP_VARTYPE_CONTINUOUS:
         varWeight = arrowheurdata->varWeightContinous;
         break;
      case SCIP_VARTYPE_INTEGER:
         varWeight = arrowheurdata->varWeightInteger;
         break;
      case SCIP_VARTYPE_IMPLINT:
         varWeight = arrowheurdata->varWeightImplint;
         break;
      case SCIP_VARTYPE_BINARY:
         varWeight = arrowheurdata->varWeightBinary;
         break;
      }

      SCIP_CALL(SCIPallocMemory(scip, &hedge));
      hedge->type = VARIABLE;
      hedge->originalId = i;
      hedge->nvariableIds = 0;
      hedge->variableIds = NULL;
      hedge->cost = varWeight;
      SCIP_CALL(SCIPsetPtrarrayVal(scip, hedges, nhyperedges, hedge));
      ++nhyperedges;

      /* link the copies together */
      SCIP_CALL(SCIPallocMemoryArray(scip, &hedge->variableIds, size));
      hedge->nvariableIds = size;

      for( j = 0; j < size; ++j )
      {
         hedge->variableIds[j] = SCIPgetIntarrayVal(scip, maporigtocopies[i], j);
         SCIPdebugPrintf("%d, ", hedge->variableIds[j]+1);
      }
      SCIPdebugPrintf("\n");
      SCIP_CALL(SCIPfreeIntarray(scip, &maporigtocopies[i]));
   }
   SCIPfreeMemoryArray(scip, &maporigtocopies);
   SCIPfreeMemoryArray(scip, &nmaporigtocopies);
   SCIPfreeMemoryArray(scip, &copies);
   nvertices = id;
   arrowheurdata->nvertices = nvertices;

   assert(SCIPgetPtrarrayMaxIdx(scip, hedges)+1 == nhyperedges);
   assert(nvertices == SCIPgetIntarrayMaxIdx(scip, copytoorig)+1);


   return SCIP_OKAY;
}

/** Will call hmetis via a system call */
static
SCIP_RETCODE callMetis(
      SCIP*             scip,       /**< SCIP data struture */
      SCIP_ARROWHEURDATA*  arrowheurdata  /**< presolver data data structure */
      )
{
   char metiscall[SCIP_MAXSTRLEN];
   char metisout[SCIP_MAXSTRLEN];
   char line[SCIP_MAXSTRLEN];
   const char *tempfile = "metis.temp";

   int status;
   int i;
   int j;
   int nvertices;
   int ndummyvertices;
   int* partition;

   SCIP_PTRARRAY *hedges;
   SCIP_FILE *zfile;
   FILE* file;

   assert(scip != NULL);
   assert(arrowheurdata != NULL);
   assert(!SCIPfileExists(tempfile));

   hedges = arrowheurdata->hedges;
   nvertices = arrowheurdata->nvertices;
   ndummyvertices = arrowheurdata->dummynodes*nvertices;
//   SCIPinfoMessage(scip, NULL, "DUMMY: %.2f", arrowheurdata->dummynodes);

   file = fopen(tempfile, "w");
   if(file == NULL)
   {
      SCIPerrorMessage("Could not open temporary metis file!");
      return SCIP_FILECREATEERROR;
   }


   SCIPinfoMessage(scip, file, "%d %d 1\n", SCIPgetPtrarrayMaxIdx(scip, hedges)+1, nvertices+ndummyvertices);
   for( i = 0; i <=  SCIPgetPtrarrayMaxIdx(scip, hedges); i++ )
   {
      HyperEdge* hedge;
      hedge = (HyperEdge*) SCIPgetPtrarrayVal(scip, hedges, i);
      assert(hedge != NULL);
      assert(hedge->nvariableIds != 0);
      SCIPinfoMessage(scip, file, "%d ", hedge->cost);

      for( j = 0; j < hedge->nvariableIds; ++j )
      {
         SCIPinfoMessage(scip, file, "%d ",hedge->variableIds[j] + 1 );
      }
      SCIPinfoMessage(scip, file, "\n");
   }
   status = fclose(file);

   if(status == -1)
   {
      SCIPerrorMessage("Could not close '%s'", tempfile);
      return SCIP_WRITEERROR;
   }

   /* call metis via syscall as there is no library usable ... */

   SCIPsnprintf(metiscall, SCIP_MAXSTRLEN, "./hmetis metis.temp %d -seed %d ",  arrowheurdata->blocks,  arrowheurdata->randomseed );
//   SCIPsnprintf(metiscall, SCIP_MAXSTRLEN, "./hmetis.sh metis.temp %d -seed %d",  arrowheurdata->blocks,  arrowheurdata->randomseed );
   SCIPverbMessage(scip, SCIP_VERBLEVEL_NORMAL, NULL, "\nCalling metis with '%s'.\n", metiscall);

   status = system( metiscall );

   /* check error codes */
   if( status == -1 )
   {
      SCIPerrorMessage("System call did not succed: %s", strerror( errno ));
   }
   else if( status != 0 )
   {
      SCIPerrorMessage("Calling hmetis unsuccessful! See the above error message for more details.");
   }

   /* exit gracefully in case of errors */
   if( status != 0 )
   {
      if( arrowheurdata->tidy )
      {
         status = remove( tempfile );
         if( status == -1 )
         {
            SCIPerrorMessage("Could not remove metis input file: ", strerror( errno ));
         }
      }
      return SCIP_ERROR;
   }

   /*
    * parse the output into the vector
    * alloc the memory
    */
   if(arrowheurdata->partition == NULL)
   {
      SCIP_CALL(SCIPallocMemoryArray(scip, &arrowheurdata->partition, nvertices));
   }

   assert(arrowheurdata->partition != NULL);
   partition = arrowheurdata->partition;

   SCIPsnprintf(metisout, SCIP_MAXSTRLEN, "metis.temp.part.%d",arrowheurdata->blocks);

   zfile = SCIPfopen(metisout, "r");
   i = 0;
   while( !SCIPfeof(zfile) && i < nvertices )
   {
      int temp;
      if( SCIPfgets(line, SCIP_MAXSTRLEN, zfile) == NULL )
      {
         SCIPerrorMessage("Line could not be read");
         return SCIP_READERROR;
      }

      temp = atoi(line);
      assert(temp >= 0 && temp <= arrowheurdata->blocks-1);
      partition[i] = temp;
      i++;
   }
   SCIPfclose(zfile);

   /* if desired delete the temoprary metis file */
   if( arrowheurdata->tidy )
   {
      status = remove( tempfile );
      if( status == -1 )
      {
         SCIPerrorMessage("Could not remove metis input file: ", strerror( errno ));
      }
      status = remove( metisout );
      if( status == -1 )
      {
         SCIPerrorMessage("Could not remove metis output file: ", strerror( errno ));
      }
   }

   return SCIP_OKAY;
}
/** Maps the partitions for the disaggregated vertices to the original vertices */
static
SCIP_RETCODE assignBlocksToOriginalVariables(
      SCIP*             scip,       /**< SCIP data structure */
      SCIP_ARROWHEURDATA*  arrowheurdata  /**< presolver data data structure */
      )
{

   int i;
   int nvars;
   int *partition;
   int *origpart;
   int nvertices;
   assert(scip != NULL);
   assert(arrowheurdata != NULL);

   nvertices = arrowheurdata->nvertices;
   partition = arrowheurdata->partition;
   origpart = arrowheurdata->varpart;
   nvars = SCIPgetNVars( scip );

    /* go through the new vertices */
   for( i = 0; i < nvertices ; ++i )
   {
      int originalId;
      /* find out the original id (== index of the var in the vars array) */
      originalId = SCIPgetIntarrayVal(scip, arrowheurdata->copytooriginal, i);

      /* add the id to the set of ids for the original vertex */
      assert(originalId >= 0 && originalId < nvars);
      assert(partition[i] >= 0);
      if(origpart[originalId] == -1)
      {
         origpart[originalId] = partition[i];
      }
      else if(origpart[originalId] >= 0)
      {
         if(origpart[originalId] != partition[i])
         {
            origpart[originalId] = -2;
         }
      }
      assert(origpart[originalId] == -2 || origpart[originalId] >= 0);
      assert(origpart[originalId] <= arrowheurdata->blocks);
   }

   return SCIP_OKAY;
}

/** Builds the transformed problem in the new scip instance */
static SCIP_RETCODE buildTransformedProblem(
   SCIP*                    scip,           /**< SCIP data structure */
   SCIP_ARROWHEURDATA*      arrowheurdata,  /**< presolver data data structure */
   SCIP_ARROWHEURSCORES*    score           /**< scores */
   )
{
   SCIP_Bool *isVarHandled;
   SCIP_VAR*** subscipvars;
   SCIP_VAR** linkingvars;
   SCIP_CONS*** subscipconss;
   SCIP_CONS** linkingconss;
   SCIP_HASHMAP* vartoblock;
   SCIP_HASHMAP* constoblock;

   int *nsubscipvars;
   int *nsubscipconss;
   int nlinkingvars;
   int nlinkingconss;
   int i;
   int j;
   SCIP_CONS **conss;
   int ncons;
   SCIP_VAR **vars;
   int nvars;
   assert(scip != NULL);

   subscipconss = arrowheurdata->consperblock;
   subscipvars = arrowheurdata->varsperblock;

   nsubscipconss = arrowheurdata->nconsperblock;
   nsubscipvars = arrowheurdata->nvarsperblock;

   linkingvars = arrowheurdata->linkingvars;
   linkingconss = arrowheurdata->linkingconss;

   nlinkingconss = arrowheurdata->nlinkingconss;
   nlinkingvars = arrowheurdata->nlinkingvars;

   constoblock = arrowheurdata->constoblock;
   vartoblock = arrowheurdata->varstoblock;

   ncons = SCIPgetNConss( scip );
   nvars = SCIPgetNVars( scip );

   conss = SCIPgetConss( scip );
   vars = SCIPgetVars( scip );

   score->minkequicutscore = 0;
   score->equicutscorenormalized = 0;

   SCIP_CALL(SCIPallocMemoryArray(scip, &isVarHandled, nvars));

   for(i = 0; i < nvars; ++i)
   {
      isVarHandled[i] = FALSE;
   }

   /* go through all of the constraints */
   for( i = 0; i < ncons; i++ )
   {
      long int consblock = -1;

      /* sort the variables into corresponding buckets */
      int ncurvars = SCIPgetNVarsXXX( scip, conss[i] );
      SCIP_VAR **curvars = SCIPgetVarsXXX( scip, conss[i] );
      for( j = 0; j < ncurvars; j++ )
      {
         SCIP_VAR* var;
         long int varblock;
         if(!SCIPvarIsActive(curvars[j]))
         {
            continue;
         }
         assert(SCIPvarIsActive(curvars[j]));
         assert(!SCIPvarIsDeleted(curvars[j]));

         var = curvars[j];
         varblock = -1;
         /*
          * if the variable has already been handled, we do not need to look
          * at it again and only need to set the constraint
          */
         if(!isVarHandled[SCIPvarGetProbindex( var )])
         {
            isVarHandled[SCIPvarGetProbindex( var )] = TRUE;
            /*
             * if the following assertion fails, the picture and the mapping is
             * certainly wrong!
             */
            assert(vars[SCIPvarGetProbindex(var)] == var);
            assert(SCIPvarGetProbindex(var) < nvars);
            assert(arrowheurdata->varpart[SCIPvarGetProbindex(var)] < arrowheurdata->blocks);
            /* get the number of blocks the current variable is in */
            assert(arrowheurdata->varpart[SCIPvarGetProbindex(var)] == -2 ||
                   arrowheurdata->varpart[SCIPvarGetProbindex(var)] >= 0);
            /* if the variable is in exactly one block */
            if( arrowheurdata->varpart[SCIPvarGetProbindex(var)] != -2)
            {
               /* then the partition is given */
               varblock = arrowheurdata->varpart[SCIPvarGetProbindex(var)];
               assert(varblock < arrowheurdata->blocks);
               subscipvars[varblock][nsubscipvars[varblock]] = var;
               ++(nsubscipvars[varblock]);
            }
            /*
             * if the variable is a linking variable, don't update the constraint
             * block and add the variable to the linking variables
             */
            else
            {
               varblock = arrowheurdata->blocks+1;

               linkingvars[nlinkingvars] = var;
               ++nlinkingvars;
            }

            /* finally set the hashmap image */
            assert(!SCIPhashmapExists(vartoblock, var));
            SCIP_CALL(SCIPhashmapInsert(vartoblock, var, (void*)varblock));
         }
         else
         {
            varblock = (long int)SCIPhashmapGetImage(vartoblock, var);
            assert(varblock == arrowheurdata->varpart[SCIPvarGetProbindex(var)] ||  arrowheurdata->varpart[SCIPvarGetProbindex(var)] == -2);
         }


         /*
          * if the variable is not a linking variable, add it to the correct
          * block and update the block of the constraint, if necessary
          */
         if( varblock <= arrowheurdata->blocks )
         {
            /*
             * if the block of the constraint has not been set yet, set it to
             * the block of the current variable
             */
            if( consblock == -1 )
            {
               consblock = varblock;
            }
            /*
             * if the block has been set but the current variable belongs to a
             * different block, we have a linking constraint
             */
            else if( consblock >= 0 && consblock != varblock )
            {
               consblock = -2;
            }

            /*
             * At this point the constraint is either in the border or it
             * belongs to the same block as the current variable
             */
            assert(consblock == -2 ||consblock == varblock);

         }
      }
      SCIPfreeMemoryArrayNull(scip, &curvars);

      /*
       *  sort the constraints into the corresponding bucket
       *
       *  if the constraint is linking, put it there
       */
      if( consblock < 0 )
      {
         size_t block;
         HyperEdge* hedge;
         assert(arrowheurdata->blocks >= 0);
         block = arrowheurdata->blocks +1;
         linkingconss[nlinkingconss] = conss[i];
         ++nlinkingconss;
         assert(!SCIPhashmapExists(constoblock, conss[i]));
         SCIP_CALL(SCIPhashmapInsert(constoblock, conss[i], (void*)(block)));

         hedge = (HyperEdge*) SCIPgetPtrarrayVal(scip, arrowheurdata->hedges, i);
//         assert(hedge->originalId == i);
//         assert(hedge->type == CONSTRAINT);
//         score->minkequicutscore += hedge->cost;
      }
      /* otherwise put it in its block */
      else
      {
         subscipconss[consblock][nsubscipconss[consblock]] = conss[i];
         assert(!SCIPhashmapExists(constoblock, conss[i]));
         SCIP_CALL(SCIPhashmapInsert(constoblock, conss[i], (void*)consblock));
         ++(nsubscipconss[consblock]);
      }

   }
   /*
    * go through all variables and look at the not handled variables and add
    * them to the correct partition
    */

   for( i = 0; i < nvars; i++ )
   {
      int partitionOfVar;
      if( arrowheurdata->varpart[i] < 0 )
      {
         switch(SCIPvarGetType(vars[i]))
         {
         case SCIP_VARTYPE_BINARY:
            score->minkequicutscore += arrowheurdata->varWeightBinary;
            break;
         case SCIP_VARTYPE_CONTINUOUS:
            score->minkequicutscore += arrowheurdata->varWeightContinous;
            break;
         case SCIP_VARTYPE_IMPLINT:
            score->minkequicutscore += arrowheurdata->varWeightImplint;
            break;
         case SCIP_VARTYPE_INTEGER:
            score->minkequicutscore += arrowheurdata->varWeightInteger;
            break;
         default:
            break;
         }
      }
      if( isVarHandled[i] )
      {
         continue;

      }

      partitionOfVar = -1;
      if( arrowheurdata->varpart[i] >= 0 )
      {
         partitionOfVar = arrowheurdata->varpart[i];
      }

      if( partitionOfVar != -1 )
      {
         subscipvars[partitionOfVar][nsubscipvars[partitionOfVar]] = vars[i];
         ++nsubscipvars[partitionOfVar];
      }
      else
      {
         linkingvars[nlinkingvars] = vars[i];
         ++nlinkingvars;
      }

   }
   SCIPfreeMemoryArray(scip, &isVarHandled);
   arrowheurdata->nlinkingvars = nlinkingvars;
   arrowheurdata->nlinkingconss = nlinkingconss;
   /* do some elimentary checks and report errors */

   /* first, make sure that there are constraints in every block, otherwise the hole thing is useless */
   for( i = 0; i < arrowheurdata->blocks; ++i)
   {
      if(nsubscipconss[i] == 0)
      {
         SCIPerrorMessage("Block %d does not have any constraints!\n", i);
      }
   }
   return SCIP_OKAY;
}

static
SCIP_RETCODE writeDWsolverOutput(
      SCIP*             scip,       /**< SCIP data structure */
      SCIP_ARROWHEURDATA*  arrowheurdata  /**< presolver data data structure */
   )
{
   FILE* file;
   int i;
   int j;
   char name[SCIP_MAXSTRLEN];

   SCIPsnprintf(name, SCIP_MAXSTRLEN,
         DWSOLVER_REFNAME(SCIPgetProbName(scip),
                          arrowheurdata->blocks,
                          arrowheurdata->varWeightContinous,
                          arrowheurdata->varWeightInteger,
                          arrowheurdata->consWeight,
                          arrowheurdata->dummynodes,
                          arrowheurdata->alpha,
                          arrowheurdata->beta,
                          arrowheurdata->consWeightSetppc)
                          );

   file = fopen(name, "w");
   if(file == NULL)
      return SCIP_FILECREATEERROR;

   SCIPinfoMessage(scip, file, "%d ", arrowheurdata->blocks );
   for(i = 0; i < arrowheurdata->blocks; ++i)
   {
      SCIPinfoMessage(scip, file, "%d ", arrowheurdata->nconsperblock[i]);
   }
   SCIPinfoMessage(scip, file, "\n");

   for(i = 0; i < arrowheurdata->blocks; ++i)
   {
      for(j = 0; j < arrowheurdata->nconsperblock[i]; ++j)
      {
         long int consindex = (long int) SCIPhashmapGetImage(arrowheurdata->constolpid, arrowheurdata->consperblock[i][j]);
         SCIPinfoMessage(scip, file, "%d ", consindex);
      }

      SCIPinfoMessage(scip, file, "\n");
   }

   fclose(file);

   return SCIP_OKAY;
}

static
SCIP_RETCODE evaluateDecomposition(
      SCIP*                 scip,           /**< SCIP data structure */
      SCIP_ARROWHEURDATA*   arrowheurdata,  /**< presolver data data structure */
      SCIP_ARROWHEURSCORES* score           /**< returns the score of the decomposition */
      )
{
   char name[SCIP_MAXSTRLEN];
   unsigned int matrixarea;
   unsigned int borderarea;
   int nvars;
   int nconss;
   int i;
   int j;
   int k;
   int blockarea;
   SCIP_Real varratio;
   int* nzblocks;
   int* nlinkvarsblocks;
   int* nvarsblocks;
   SCIP_Real* blockdensities;
   int* blocksizes;
   SCIP_Real density;

   assert(scip != NULL);
   assert(arrowheurdata != NULL);
   assert(score != NULL);

   matrixarea = 0;
   borderarea = 0;
   nvars = SCIPgetNVars(scip);
   nconss = SCIPgetNConss(scip);

   /* get the right name */
   SCIPsnprintf(name, SCIP_MAXSTRLEN,
         DWSOLVER_REFNAME(SCIPgetProbName(scip),
                          arrowheurdata->blocks,
                          arrowheurdata->varWeightContinous,
                          arrowheurdata->varWeightInteger,
                          arrowheurdata->consWeight,
                          arrowheurdata->dummynodes,
                          arrowheurdata->alpha,
                          arrowheurdata->beta,
                          arrowheurdata->consWeightSetppc)
                                  );

   SCIP_CALL(SCIPallocMemoryArray(scip, &nzblocks, arrowheurdata->blocks));
   SCIP_CALL(SCIPallocMemoryArray(scip, &nlinkvarsblocks, arrowheurdata->blocks));
   SCIP_CALL(SCIPallocMemoryArray(scip, &blockdensities, arrowheurdata->blocks));
   SCIP_CALL(SCIPallocMemoryArray(scip, &blocksizes, arrowheurdata->blocks));
   SCIP_CALL(SCIPallocMemoryArray(scip, &nvarsblocks, arrowheurdata->blocks));
   /*
    * 3 Scores
    *
    * - Area percentage (min)
    * - block density (max)
    * - \pi_b {v_b|v_b is linking}/#vb (min)
    */

   /* calculate matrix area */
   matrixarea = nvars*nconss;

   SCIPinfoMessage(scip, NULL, "Sizes: %d x %d (%d, %d)\n", nvars, nconss, arrowheurdata->nlinkingvars, arrowheurdata->nlinkingconss);

   /* calculate slave sizes, nonzeros and linkingvars */
   for (i = 0; i < arrowheurdata->blocks; ++i)
   {
      SCIP_CONS** curconss;
      int ncurconss;
      int nvarsblock;
      SCIP_Bool *ishandled;

      SCIP_CALL(SCIPallocMemoryArray(scip, &ishandled, nvars));
      nvarsblock = 0;
      nzblocks[i] = 0;
      nlinkvarsblocks[i] = 0;
      for( j = 0; j < nvars; ++j)
      {
         ishandled[j] = FALSE;
      }
      curconss = arrowheurdata->consperblock[i];
      ncurconss = arrowheurdata->nconsperblock[i];

      for( j = 0; j < ncurconss; ++j)
      {
         SCIP_VAR** curvars;
         int ncurvars;

         curvars = SCIPgetVarsXXX(scip, curconss[j]);
         ncurvars = SCIPgetNVarsXXX(scip, curconss[j]);
         for( k = 0; k < ncurvars; ++k)
         {
            long int block;
            if(!SCIPvarIsActive(curvars[k]))
               continue;


            ++(nzblocks[i]);
            assert(SCIPhashmapExists(arrowheurdata->varstoblock, curvars[k]));
            block = (long int) SCIPhashmapGetImage(arrowheurdata->varstoblock, curvars[k]);
            //SCIPinfoMessage(scip, NULL, "b: %d", block);
            if(block == arrowheurdata->blocks+1 && ishandled[SCIPvarGetProbindex(curvars[k])] == FALSE)
            {

               ++(nlinkvarsblocks[i]);
            }
            ishandled[SCIPvarGetProbindex(curvars[k])] = TRUE;
         }

         SCIPfreeMemoryArray(scip, &curvars);
      }

      for( j = 0; j < nvars; ++j)
      {
         if(ishandled[j])
         {
            ++nvarsblock;
         }
      }


      blocksizes[i] = nvarsblock*ncurconss;
      nvarsblocks[i] = nvarsblock;
      if(blocksizes[i] > 0)
      {
         blockdensities[i] = 1.0*nzblocks[i]/blocksizes[i];
      }
      else
      {
         blockdensities[i] = 0.0;
      }
      assert(blockdensities[i] >= 0 && blockdensities[i] <= 1.0);
      SCIPfreeMemoryArray(scip, &ishandled);
   }



   /* calculate border area */

   borderarea = arrowheurdata->nlinkingconss*nvars+arrowheurdata->nlinkingvars*(nconss-arrowheurdata->nlinkingconss);
   //SCIPinfoMessage(scip, NULL, "c: %d, v: %d", arrowheurdata->nlinkingconss*nvars, arrowheurdata->nlinkingvars*(nconss-arrowheurdata->nlinkingconss));

   blockarea = 0;
   density = 1E20;
   varratio = 1.0;
   for( i = 0; i < arrowheurdata->blocks; ++i )
   {
      /* calculate block area */
      blockarea += blocksizes[i];


      /* calculate density */
      density = MIN(density, blockdensities[i]);

      /* calculate linking var ratio */
      if(arrowheurdata->nlinkingvars > 0)
      {
         varratio *= 1.0*nlinkvarsblocks[i]/arrowheurdata->nlinkingvars;
      }
      else
      {
         varratio = 0;
      }
   }

   score->linkingscore = (0.5+0.5*varratio);
   // score->borderscore = (1.0*(blockarea+borderarea)/matrixarea);
   score->borderscore = (1.0*(borderarea)/matrixarea);
   score->densityscore = (1-density);
   //*score = scorelinking*scorecoverage*scoredensity;

   SCIPinfoMessage(scip, NULL, "Score of the decomposition: (%.6f; %.6f; %.6f)\n", score->borderscore,score->densityscore,  score->linkingscore);
//   SCIPinfoMessage(scip, NULL, "\nINDIVIDUAL\t%f\t%f\t%f\t%f\t%s_%d_%d_%d_%d_%.1f_%.1f_%.1f_%d_kind_ref1.txt\n", SCIPgetProbName(scip), *score, scorecoverage, scoredensity, scorelinking, arrowheurdata->blocks, arrowheurdata->varWeightContinous,
//        arrowheurdata->varWeightInteger, arrowheurdata->consWeight, arrowheurdata->dummynodes, arrowheurdata->alpha, arrowheurdata->beta, arrowheurdata->consWeightSetppc);
   SCIPfreeMemoryArray(scip, &nzblocks);
   SCIPfreeMemoryArray(scip, &nlinkvarsblocks);
   SCIPfreeMemoryArray(scip, &blockdensities);
   SCIPfreeMemoryArray(scip, &blocksizes);
   SCIPfreeMemoryArray(scip, &nvarsblocks);
   return SCIP_OKAY;

}

extern
SCIP_RETCODE createArrowheurData(
      SCIP*         scip,
      SCIP_ARROWHEURDATA** arrowheurdata
      )
{
   assert(scip != NULL);
   assert(arrowheurdata != NULL);
   SCIP_CALL(SCIPallocBlockMemory(scip, arrowheurdata));
   return SCIP_OKAY;
}

extern
void freeArrowheurData(
      SCIP* scip,
      SCIP_ARROWHEURDATA** arrowheurdata
   )
{
   assert(scip != NULL);
   assert(arrowheurdata != NULL);

   SCIPfreeBlockMemoryNull(scip, arrowheurdata);
}

extern
SCIP_RETCODE detectAndBuildArrowHead(
      SCIP*                scip,          /**< SCIP data structure */
      SCIP_ARROWHEURDATA*  arrowheurdata, /**< presolver data data structure */
      SCIP_RESULT*         result
      )
{

   SCIP_ARROWHEURSCORES score;
   int i;
   char filename[SCIP_MAXSTRLEN];

   SCIPinfoMessage(scip, NULL, "detectandbuild arrowhead:\n");
   assert(scip != NULL);

   SCIP_CALL(initArrowheurData(scip, arrowheurdata));
   /* build the hypergraph structure from the original problem */
   SCIP_CALL(buildGraphStructure(scip, arrowheurdata));

   if( arrowheurdata->minblocks == arrowheurdata->maxblocks)
   {
      arrowheurdata->blocks = arrowheurdata->minblocks;
     /* get the partitions for the new variables from metis */
     SCIP_CALL(callMetis(scip, arrowheurdata));

     /* deduce the partitions for the original variables */
     SCIP_CALL(assignBlocksToOriginalVariables( scip, arrowheurdata));

     SCIP_CALL(buildTransformedProblem(scip, arrowheurdata, &score));
     SCIP_CALL(evaluateDecomposition(scip, arrowheurdata, &score));
     SCIP_CALL(writeDWsolverOutput(scip, arrowheurdata));

     arrowheurdata->found = TRUE;
     SCIP_CALL(printArrowheurScores(scip, arrowheurdata, &score));
     SCIP_CALL(freeArrowheurDataData(scip, arrowheurdata));

     SCIPsnprintf(filename, SCIP_MAXSTRLEN,
           GP_NAME(SCIPgetProbName(scip),
              arrowheurdata->blocks,
              arrowheurdata->varWeightContinous,
              arrowheurdata->varWeightInteger,
              arrowheurdata->consWeight,
              arrowheurdata->dummynodes,
              arrowheurdata->alpha,
              arrowheurdata->beta,
              arrowheurdata->consWeightSetppc)
              );

     SCIP_CALL(SCIPwriteOrigProblem(scip, filename, "gp", FALSE));

   }
   else
   {
      SCIP_Real bestscore = 1E20;
      int bestsetting;
      for( i = arrowheurdata->minblocks; i <= arrowheurdata->maxblocks; ++i)
      {
         SCIP_Real cumscore;
         arrowheurdata->blocks = i;

         /* get the partitions for the new variables from metis */
         SCIP_CALL(callMetis(scip, arrowheurdata));

         /* deduce the partitions for the original variables */
         SCIP_CALL(assignBlocksToOriginalVariables( scip, arrowheurdata));

         SCIP_CALL(buildTransformedProblem(scip, arrowheurdata,  &score));
         SCIP_CALL(evaluateDecomposition(scip, arrowheurdata, &score));

         cumscore = score.borderscore*score.linkingscore*score.densityscore;
         if (cumscore < bestscore)
         {
            bestscore = cumscore;
            bestsetting = i;
         }

         SCIPhashmapFree(&arrowheurdata->varstoblock);
         SCIPhashmapFree(&arrowheurdata->constoblock);
         SCIP_CALL(SCIPhashmapCreate(&arrowheurdata->varstoblock, SCIPblkmem(scip), SCIPgetNVars(scip)));
         SCIP_CALL(SCIPhashmapCreate(&arrowheurdata->constoblock, SCIPblkmem(scip), SCIPgetNConss(scip)));
         for(i = 0; i <  SCIPgetNVars(scip); ++i)
         {
            arrowheurdata->varpart[i] = -1;
         }

         for( i = 0; i < arrowheurdata->blocks; ++i)
         {
            arrowheurdata->nvarsperblock[i] = 0;
            arrowheurdata->nconsperblock[i] = 0;
         }

         arrowheurdata->nlinkingconss = 0;
         arrowheurdata->nlinkingvars = 0;
      }

      arrowheurdata->found = TRUE;
      arrowheurdata->blocks = bestsetting;

      /* get the partitions for the new variables from metis */
      SCIP_CALL(callMetis(scip, arrowheurdata));

      /* deduce the partitions for the original variables */
      SCIP_CALL(assignBlocksToOriginalVariables( scip, arrowheurdata));

      SCIP_CALL(buildTransformedProblem(scip, arrowheurdata, &score));
      SCIP_CALL(evaluateDecomposition(scip, arrowheurdata, &score));
      SCIP_CALL(writeDWsolverOutput(scip, arrowheurdata));

      arrowheurdata->found = TRUE;
      SCIP_CALL(printArrowheurScores(scip, arrowheurdata, &score));
      SCIP_CALL(freeArrowheurDataData(scip, arrowheurdata));

      SCIPsnprintf(filename, SCIP_MAXSTRLEN,
           GP_NAME(SCIPgetProbName(scip),
              arrowheurdata->blocks,
              arrowheurdata->varWeightContinous,
              arrowheurdata->varWeightInteger,
              arrowheurdata->consWeight,
              arrowheurdata->dummynodes,
              arrowheurdata->alpha,
              arrowheurdata->beta,
              arrowheurdata->consWeightSetppc)
              );

      SCIP_CALL(SCIPwriteOrigProblem(scip, filename, "gp", FALSE));
   }
   *result = SCIP_OKAY;
   return SCIP_OKAY;
}

/** set the decomp structure */
extern
SCIP_RETCODE SCIPArrowHeurSetDecomp(
   SCIP*       scip,       /**< SCIP data structure */
   SCIP_ARROWHEURDATA* arrowheurdata,
   DECDECOMP*  decdecomp   /**< DECOMP data structure */

   )
{

   assert(scip != NULL);
   assert(decdecomp != NULL);
   assert(arrowheurdata != NULL);

   arrowheurdata->decdecomp = decdecomp;

   return SCIP_OKAY;
}

/** creates the arrowheur presolver and includes it in SCIP */
SCIP_RETCODE SCIPincludeDetectionArrowheur(
   SCIP*                 scip,                /**< SCIP data structure */
   SCIP_ARROWHEURDATA*   arrowheurdata
   )
{
   assert(scip != NULL);
   assert(arrowheurdata != NULL);

   arrowheurdata->found = FALSE;
   arrowheurdata->partition = NULL;
   arrowheurdata->blocks = -1;
   /* add arrowheur presolver parameters */
   SCIP_CALL(SCIPaddIntParam(scip, "arrowheur/maxblocks", "The maximal number of blocks", &arrowheurdata->maxblocks, FALSE, DEFAULT_MAXBLOCKS, 2, 1000000, NULL, NULL));
   SCIP_CALL(SCIPaddIntParam(scip, "arrowheur/minblocks", "The minimal number of blocks", &arrowheurdata->minblocks, FALSE, DEFAULT_MINBLOCKS, 2, 1000000, NULL, NULL));
   SCIP_CALL(SCIPaddRealParam(scip, "arrowheur/beta", "factor on how heavy equality (beta) and inequality constraints are measured", &arrowheurdata->beta, FALSE, DEFAULT_BETA, 0.0, 1.0, NULL, NULL ));
   SCIP_CALL(SCIPaddRealParam(scip, "arrowheur/alpha", "factor on how heavy the standard deviation of the coefficients is measured", &arrowheurdata->alpha, FALSE, DEFAULT_ALPHA, 0.0, 1E20, NULL, NULL ));
   SCIP_CALL(SCIPaddIntParam(scip, "arrowheur/varWeight", "Weight of a variable hyperedge", &arrowheurdata->varWeight, FALSE, DEFAULT_VARWEIGHT, 0, 1000000, NULL, NULL));
   SCIP_CALL(SCIPaddIntParam(scip, "arrowheur/varWeightBinary", "Weight of a binary variable hyperedge", &arrowheurdata->varWeightBinary, FALSE, DEFAULT_VARWEIGHTBIN, 0, 1000000, NULL, NULL));
   SCIP_CALL(SCIPaddIntParam(scip, "arrowheur/varWeightContinous", "Weight of a continuos variable hyperedge", &arrowheurdata->varWeightContinous, FALSE, DEFAULT_VARWEIGHTCONT, 0, 1000000, NULL, NULL));
   SCIP_CALL(SCIPaddIntParam(scip, "arrowheur/varWeightImplint", "Weight of a implicit integer variable hyperedge", &arrowheurdata->varWeightImplint, FALSE, DEFAULT_VARWEIGHTIMPL, 0, 1000000, NULL, NULL));
   SCIP_CALL(SCIPaddIntParam(scip, "arrowheur/varWeightInteger", "Weight of a integer variable hyperedge", &arrowheurdata->varWeightInteger, FALSE, DEFAULT_VARWEIGHTINT, 0, 1000000, NULL, NULL));
   SCIP_CALL(SCIPaddIntParam(scip, "arrowheur/consWeight", "Weight of a constraint hyperedge", &arrowheurdata->consWeight, FALSE, DEFAULT_CONSWEIGHT, 0, 1000000, NULL, NULL));
   SCIP_CALL(SCIPaddBoolParam(scip, "arrowheur/tidy", "Whether to clean up temporary files", &arrowheurdata->tidy, FALSE, DEFAULT_TIDY, NULL, NULL));
   SCIP_CALL(SCIPaddIntParam(scip, "arrowheur/randomseed", "random seed for hmetis", &arrowheurdata->randomseed, FALSE, DEFAULT_RANDSEED, -1, INT_MAX, NULL, NULL));
   SCIP_CALL(SCIPaddRealParam(scip, "arrowheur/dummynodes", "percentage of dummy nodes for metis", &arrowheurdata->dummynodes, FALSE, DEFAULT_DUMMYNODES, 0.0, 1.0, NULL, NULL));
   SCIP_CALL(SCIPaddIntParam(scip, "arrowheur/consWeightSetppc", "Weight for constraint hyperedges that are setpartitioning or covering constraints", &arrowheurdata->consWeightSetppc, FALSE, DEFAULT_CONSWEIGHT_SETPPC, 0, 1000000, NULL, NULL));
//   SCIP_CALL(SCIPaddIntParam(scip, "arrowheur/consWeightSetpart", "Weight of a setpart constraint hyperedge", &arrowheurdata->consWeightSetpart, FALSE, DEFAULT_CONSWEIGHT_SETPART, 1, 1000000, NULL, NULL));
//   SCIP_CALL(SCIPaddIntParam(scip, "arrowheur/consWeightSetpack", "Weight of a setpack constraint hyperedge", &arrowheurdata->consWeightSetpack, FALSE, DEFAULT_CONSWEIGHT_SETPACK, 1, 1000000, NULL, NULL));
//   SCIP_CALL(SCIPaddIntParam(scip, "arrowheur/consWeightSetcov", "Weight of a setcov constraint hyperedge", &arrowheurdata->consWeightSetcov, FALSE, DEFAULT_CONSWEIGHT_SETCOV, 1, 1000000, NULL, NULL));
//   SCIP_CALL(SCIPaddIntParam(scip, "arrowheur/consWeightHighVariation", "Weight of a constraint hyperedge with high variation in the coefficients", &arrowheurdata->consWeightHighVariation, FALSE, DEFAULT_CONSWEIGHT_HIGH_VARIATION, 1, 1000000, NULL, NULL));
//   SCIP_CALL(SCIPaddIntParam(scip, "arrowheur/consWeightLowVariation", "Weight of a constraint hyperedge with low variation in the coefficients", &arrowheurdata->consWeightLowVariation, FALSE, DEFAULT_CONSWEIGHT_LOW_VARIATION, 1, 1000000, NULL, NULL));
//   SCIP_CALL(SCIPaddBoolParam(scip, "arrowheur/forceSetpackMaster", "Whether to force setpack constraints in the master", &arrowheurdata->forceSetpackMaster, FALSE, DEFAULT_FORCE_SETPACK_MASTER, NULL, NULL));
//   SCIP_CALL(SCIPaddBoolParam(scip, "arrowheur/forceSetcovMaster", "Whether to force setcov constraints in the master", &arrowheurdata->forceSetcovMaster, FALSE, DEFAULT_FORCE_SETCOV_MASTER, NULL, NULL));
//   SCIP_CALL(SCIPaddBoolParam(scip, "arrowheur/forceSetpartMaster", "Whether to force setpart constraints in the master", &arrowheurdata->forceSetpartMaster, FALSE, DEFAULT_FORCE_SETPART_MASTER, NULL, NULL));
//   SCIP_CALL(SCIPaddRealParam(scip, "arrowheur/highVariation", "The value which is considered a high variation", &arrowheurdata->highVariation, FALSE, DEFAULT_HIGH_VARIATION, 0.0, SCIPinfinity(scip), NULL, NULL));
//   SCIP_CALL(SCIPaddRealParam(scip, "arrowheur/lowVariation", "The value which is considered a low variation", &arrowheurdata->lowVariation, FALSE, DEFAULT_LOW_VARIATION, 0.0, SCIPinfinity(scip), NULL, NULL));
   return SCIP_OKAY;
}