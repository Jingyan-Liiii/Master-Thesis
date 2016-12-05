/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                           */
/*                  This file is part of the program                         */
/*          GCG --- Generic Column Generation                                */
/*                  a Dantzig-Wolfe decomposition based extension            */
/*                  of the branch-cut-and-price framework                    */
/*         SCIP --- Solving Constraint Integer Programs                      */
/*                                                                           */
/* Copyright (C) 2010-2016 Operations Research, RWTH Aachen University       */
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

/**@file   dec_isomorph.cpp
 * @ingroup DETECTORS
 * @brief  detector for pricing problems that can be aggregated (uses bliss)
 * @author Martin Bergner
 * @author Daniel Peters
 * @author Jonas Witt
 *
 * This detector finds subproblems that can be aggregated thus reducing the symmetry of the problem using color preserving
 * automorphisms and bliss.
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/
/* #define SCIP_DEBUG */

#include "dec_isomorph.h"
#include "pub_decomp.h"
#include "cons_decomp.h"
#include "scip_misc.h"
#include "gcg.h"
#include "class_seeed.h"
#include "class_seeedpool.h"

#include "graph.hh"
#include "pub_gcgvar.h"
#include <cstring>
#include <cassert>
#include <algorithm>
#include <iostream>

#include "pub_bliss.h"

/* constraint handler properties */
#define DEC_DETECTORNAME         "isomorph"  /**< name of detector */
#define DEC_DESC                 "Detector for pricing problems suitable for aggregation" /**< description of detector*/
#define DEC_FREQCALLROUND        1           /** frequency the detector gets called in detection loop ,ie it is called in round r if and only if minCallRound <= r <= maxCallRound AND  (r - minCallRound) mod freqCallRound == 0 */
#define DEC_MAXCALLROUND         INT_MAX     /** last round the detector gets called                              */
#define DEC_MINCALLROUND         0           /** first round the detector gets called                              */
#define DEC_PRIORITY             100         /**< priority of the constraint handler for separation */
#define DEC_DECCHAR              'I'         /**< display character of detector */

#define DEC_ENABLED              TRUE        /**< should the detection be enabled */
#define DEC_SKIP                 TRUE        /**< should the detector be skipped if others found decompositions */
#define DEC_USEFULRECALL         FALSE       /**< is it useful to call this detector on a descendant of the propagated seeed */


#define DEFAULT_NUMOFSOL         1          /**< default number of solutions */
#define DEFAULT_EXACT            TRUE       /**< default value using exact coefficients for detection */
#define DEFAULT_EXTEND           FALSE      /**< default value for extending detection by using the sign of the coefficients instead of the coefficients */

#define DEFAULT_MAXDECOMPS       1          /**< default maximum number of decompositions */
#define DEFAULT_EXACT            TRUE       /**< default value using exact coefficients for detection */
#define DEFAULT_EXTEND           FALSE      /**< default value for extending detection by using the sign of the coefficients instead of the coefficients */

/*
 * Data structures
 */

/** detector data */
struct DEC_DetectorData
{
   SCIP_RESULT          result;             /**< result pointer to indicate success or failure */
   int                  numofsol;           /**< number of solutions */
   int                  maxdecomps;         /**< maximum number of decompositions */
   SCIP_Bool            exact;              /**< Use exact coefficients for detection? */
   SCIP_Bool            extend;             /**< Extend detection by using the sign of the coefficients instead of the coefficients? */
};

typedef struct struct_hook AUT_HOOK;

/** saves information of the permutation */
struct struct_hook
{
   SCIP_Bool aut;                            /**< true if there is an automorphism */
   unsigned int n;                           /**< number of permutations */
   SCIP* scip;                               /**< scip to search for automorphisms */
   int* conssperm;                           /**< permutations of conss*/
   gcg::Seeed* seeed;                        /**< seeed to propagate */
   gcg::Seeedpool* seeedpool;                /**< seeedpool */

   /** constructor for the hook struct*/
   struct_hook(SCIP_Bool aut,  /**< true if there is an automorphism */
      unsigned int       n,                  /**< number of permutations */
      SCIP*              scip                /**< scip to search for automorphisms */
   );

   /** constructor for the hook struct with a seeed */
   struct_hook(SCIP_Bool aut,  /**< true if there is an automorphism */
      unsigned int       n,                  /**< number of permutations */
      SCIP*              scip,               /**< scip to search for automorphisms */
      gcg::Seeed*        seeed,              /**< seeed to propagate */
      gcg::Seeedpool*    seeedpool           /**< seeedpool */
   );

   ~struct_hook();
   /** getter for the bool aut */
   SCIP_Bool getBool();

   /** setter for the bool aut */
   void setBool(SCIP_Bool aut);

   /** getter for the SCIP */
   SCIP* getScip();

   /** getter for the seeed */
   gcg::Seeed* getSeeed();

   /** getter for the seeedpool */
   gcg::Seeedpool* getSeeedpool();
};

SCIP* struct_hook::getScip()
{
   return this->scip;
}

gcg::Seeed* struct_hook::getSeeed()
{
   return this->seeed;
}

gcg::Seeedpool* struct_hook::getSeeedpool()
{
   return this->seeedpool;
}

/** constructor of the hook struct */
struct_hook::struct_hook(
   SCIP_Bool             aut_,               /**< true if there is an automorphism */
   unsigned int          n_,                 /**< number of permutations */
   SCIP*                 scip_               /**< array of scips to search for automorphisms */
   ) : conssperm(NULL)
{
   aut = aut_;
   n = n_;
   scip = scip_;
   SCIP_CALL_ABORT( SCIPallocMemoryArray(scip, &conssperm, SCIPgetNConss(scip)) ); /*lint !e666*/
   seeed = NULL;
   seeedpool = NULL;
}

/** constructor of the hook struct */
struct_hook::struct_hook(
   SCIP_Bool             aut_,               /**< true if there is an automorphism */
   unsigned int          n_,                 /**< number of permutations */
   SCIP*                 scip_,              /**< array of scips to search for automorphisms */
   gcg::Seeed*           seeed_,             /**< seeed to Propagate */
   gcg::Seeedpool*       seeedpool_          /**< seeedpool */
   ) : conssperm(NULL)
{
   aut = aut_;
   n = n_;
   scip = scip_;
   SCIP_CALL_ABORT( SCIPallocMemoryArray(scip, &conssperm, SCIPgetNConss(scip)) ); /*lint !e666*/
   seeed = seeed_;
   seeedpool = seeedpool_;
}

struct_hook::~struct_hook()
{   /*lint -esym(1540,struct_hook::conssperm) */
   SCIPfreeMemoryArrayNull(scip, &conssperm);
   scip = NULL;
}
/** hook function to save the permutation of the graph */
static
void fhook(
   void*                 user_param,         /**< data structure to save hashmaps with permutation */
   unsigned int          N,                  /**< number of permutations */
   const unsigned int*   aut                 /**< array of permutations */
   )
{ /*lint -e715*/
   int i;
   int nconss;
   SCIP_CONS** conss;
   AUT_HOOK* hook = (AUT_HOOK*) user_param;
   int auti;
   int ind;

   nconss = SCIPgetNConss(hook->getScip());
   assert(nconss == SCIPgetNConss(hook->getScip()));
   conss = SCIPgetConss(hook->getScip());

   for( i = 0; i < nconss; i++ )
   {
      assert(aut[i] < INT_MAX);
      if( (size_t) i != aut[i])
      {
         auti = (int) aut[i];

         SCIPdebugMessage("%d <%s> <-> %d <%s>\n", i, SCIPconsGetName(conss[i]), auti, SCIPconsGetName(conss[auti]));

         ind = MIN(i, auti);

         if( hook->conssperm[i] != -1)
            ind = MIN(ind, hook->conssperm[i]);
         if( hook->conssperm[auti] != -1 )
            ind = MIN(ind, hook->conssperm[auti]);

         hook->conssperm[i] = ind;
         hook->conssperm[auti] = ind;
         hook->setBool(TRUE);
      }
   }
}

/** hook function to save the permutation of the graph */
static
void fhookForSeeeds(
   void*                 user_param,         /**< data structure to save hashmaps with permutation */
   unsigned int          N,                  /**< number of permutations */
   const unsigned int*   aut                 /**< array of permutations */
   )
{ /*lint -e715*/
   int i;
   int nconss;
   AUT_HOOK* hook = (AUT_HOOK*) user_param;
   int auti;
   int ind;
   gcg::Seeed*  seeed;
   gcg::Seeedpool*       seeedpool;

   seeed = hook->getSeeed();
   seeedpool = hook->getSeeedpool() ;
   assert(seeed != NULL);
   assert(seeedpool != NULL);
   nconss = seeed->getNOpenconss();

   for( i = 0; i < nconss; i++ )
   {
      SCIP_CONS* cons = seeedpool->getConsForIndex(seeed->getOpenconss()[i]);
      assert(aut[i] < INT_MAX);
      if( (size_t) i != aut[i])
      {
         auti = (int) aut[i];

         SCIPdebugMessage("%d <%s> <-> %d <%s>\n", i, SCIPconsGetName(cons), auti, SCIPconsGetName(seeedpool->getConsForIndex(seeed->getOpenconss()[auti])));

         ind = MIN(i, auti);

         if( hook->conssperm[i] != -1)
            ind = MIN(ind, hook->conssperm[i]);
         if( hook->conssperm[auti] != -1 )
            ind = MIN(ind, hook->conssperm[auti]);

         hook->conssperm[i] = ind;
         hook->conssperm[auti] = ind;
         hook->setBool(TRUE);
      }
   }
}

static
SCIP_RETCODE allocMemory(
    SCIP*                scip,               /**< SCIP data structure */
    AUT_COLOR*           colorinfo,          /**< struct to save intermediate information */
    int                  nconss,             /**< number of constraints */
    int                  nvars               /**< number of variables */
    )
{
   SCIP_CALL( SCIPallocMemoryArray(scip, &colorinfo->ptrarraycoefs, nvars) );
   SCIP_CALL( SCIPallocMemoryArray(scip, &colorinfo->ptrarrayvars, nvars) );
   SCIP_CALL( SCIPallocMemoryArray(scip, &colorinfo->ptrarrayconss, nconss) );
   colorinfo->alloccoefsarray = nvars;
   return SCIP_OKAY;
}

/** destructor for colorinfo */
static
void freeMemory(
   SCIP*                 scip,               /**< SCIP data structure */
   AUT_COLOR*            colorinfo           /**< struct to save intermediate information */
)
{
   int i;

   for( i = 0; i < colorinfo->lenvarsarray; i++ )
   {
      AUT_VAR* svar = (AUT_VAR*) colorinfo->ptrarrayvars[i];
      delete svar;
   }
   for( i = 0; i < colorinfo->lenconssarray; i++ )
   {
      AUT_CONS* scons = (AUT_CONS*) colorinfo->ptrarrayconss[i];
      delete scons;
   }
   for( i = 0; i < colorinfo->lencoefsarray; i++ )
   {
      AUT_COEF* scoef = (AUT_COEF*) colorinfo->ptrarraycoefs[i];
      delete scoef;
   }

   SCIPfreeMemoryArray(scip, &colorinfo->ptrarraycoefs);
   SCIPfreeMemoryArray(scip, &colorinfo->ptrarrayconss);
   SCIPfreeMemoryArray(scip, &colorinfo->ptrarrayvars);
}

/** set up a help structure for graph creation */
static
SCIP_RETCODE setupArrays(
   SCIP*                 scip,               /**< SCIP to compare */
   AUT_COLOR*            colorinfo,          /**< data structure to save intermediate data */
   SCIP_RESULT*          result              /**< result pointer to indicate success or failure */
   )
{ /*lint -esym(593,scoef) */
   int i;
   int j;
   int nconss;
   int nvars;
   SCIP_CONS** conss;
   SCIP_VAR** vars;
   AUT_COEF* scoef;
   AUT_CONS* scons;
   SCIP_Bool added;
   SCIP_Bool onlysign;

   //allocate max n of coefarray, varsarray, and boundsarray in scip
   nconss = SCIPgetNConss(scip);
   nvars = SCIPgetNVars(scip);
   SCIP_CALL( allocMemory(scip, colorinfo, nconss, nvars) );

   conss = SCIPgetConss(scip);
   vars = SCIPgetVars(scip);

   onlysign = colorinfo->getOnlySign();

   //save the properties of variables in a struct array and in a sorted pointer array
   for( i = 0; i < nvars; i++ )
   {
      AUT_VAR* svar = new AUT_VAR(scip, vars[i]);
      //add to pointer array iff it doesn't exist
      SCIP_CALL( colorinfo->insert(svar, &added) );
      SCIPdebugMessage("%s color %d %d\n", SCIPvarGetName(vars[i]), colorinfo->get(*svar), colorinfo->color);
      //otherwise free allocated memory
      if( !added )
         delete svar;
   }

   //save the properties of constraints in a struct array and in a sorted pointer array
   for( i = 0; i < nconss && *result == SCIP_SUCCESS; i++ )
   {
      SCIP_Real* curvals = NULL;
      SCIP_VAR** curvars = NULL;

      int ncurvars = GCGconsGetNVars(scip, conss[i]);
      if( ncurvars == 0 )
         continue;
      scons = new AUT_CONS(scip, conss[i]);
      //add to pointer array iff it doesn't exist
      SCIPdebugMessage("nconss %d %d\n", nconss, *result);
      SCIP_CALL( colorinfo->insert(scons, &added) );
      SCIPdebugMessage("%s color %d %d\n", SCIPconsGetName(conss[i]), colorinfo->get(*scons), colorinfo->color);
      //otherwise free allocated memory
      if( !added )
         delete scons;

      SCIP_CALL( SCIPallocBufferArray(scip, &curvars, ncurvars) );
      SCIP_CALL( SCIPallocBufferArray(scip, &curvals, ncurvars) );

      SCIP_CALL( GCGconsGetVars(scip, conss[i], curvars, ncurvars) );
      SCIP_CALL( GCGconsGetVals(scip, conss[i], curvals, ncurvars) );

   //save the properties of variables of the constraints in a struct array and in a sorted pointer array
   for( j = 0; j < ncurvars; j++ )
   {
      SCIP_Real constant;
      added = FALSE;

      if( SCIPgetStage(scip) >= SCIP_STAGE_TRANSFORMED)
         SCIPgetProbvarSum(scip, &(curvars[j]), &(curvals[j]), &constant);

      if( !onlysign )
      {
         scoef = new AUT_COEF(scip, curvals[j]);
      }
      else
      {
         if( SCIPisPositive(scip, curvals[j]) )
            scoef = new AUT_COEF(scip, 1.0);
         else if( SCIPisNegative(scip, curvals[j]) )
            scoef = new AUT_COEF(scip, -1.0);
         else
            scoef = new AUT_COEF(scip, 0.0);
      }

      //test, whether the coefficient is not zero
      if( !SCIPisZero(scip, scoef->getVal()) )
      {
         //add to pointer array iff it doesn't exist
         SCIP_CALL( colorinfo->insert(scoef, &added) );
         SCIPdebugMessage("%f color %d %d\n", scoef->getVal(), colorinfo->get(*scoef), colorinfo->color);
      }
      //otherwise free allocated memory
      if( !added )
         delete scoef;

   }
   SCIPfreeBufferArray(scip, &curvars);
   SCIPfreeBufferArray(scip, &curvals);
}
   return SCIP_OKAY;
}

/** set up a help structure for graph creation (for seeeeds) */
static
SCIP_RETCODE setupArrays(
   SCIP*                 scip,               /**< SCIP to compare */
   AUT_COLOR*            colorinfo,          /**< data structure to save intermediate data */
   SCIP_RESULT*          result,             /**< result pointer to indicate success or failure */
   gcg::Seeed*           seeed,              /**< seeed to propagate */
   gcg::Seeedpool*       seeedpool           /**< seeedpool */
   )
{ /*lint -esym(593,scoef) */
   int i;
   int j;
   int nconss;
   int nvars;
   AUT_COEF* scoef;
   AUT_CONS* scons;
   SCIP_Bool added;
   SCIP_Bool onlysign;

   //allocate max n of coefarray, varsarray, and boundsarray in scip
   nconss = seeed->getNOpenconss();
   nvars = seeed->getNVars();
   SCIP_CALL( allocMemory(scip, colorinfo, nconss, nvars) );

   onlysign = colorinfo->getOnlySign();

   //save the properties of variables in a struct array and in a sorted pointer array
   for( i = 0; i < nvars; i++ )
   {
      SCIP_VAR* var = seeedpool->getVarForIndex(i);
      AUT_VAR* svar = new AUT_VAR(scip, var);
      //add to pointer array iff it doesn't exist
      SCIP_CALL( colorinfo->insert(svar, &added) );
      SCIPdebugMessage("%s color %d %d\n", SCIPvarGetName(var), colorinfo->get(*svar), colorinfo->color);
      //otherwise free allocated memory
      if( !added )
         delete svar;
   }

   //save the properties of constraints in a struct array and in a sorted pointer array
   for( i = 0; i < nconss && *result == SCIP_SUCCESS; i++ )
   {
      int consindex = seeed->getOpenconss()[i];
      SCIP_CONS* cons = seeedpool->getConsForIndex(consindex);

      int ncurvars = seeedpool->getNVarsForCons(consindex);
      if( ncurvars == 0 )
         continue;

      scons = new AUT_CONS(scip, cons);
      //add to pointer array iff it doesn't exist
      SCIPdebugMessage("nconss %d %d\n", nconss, *result);
      SCIP_CALL( colorinfo->insert(scons, &added) );
      SCIPdebugMessage("%s color %d %d\n", SCIPconsGetName(cons), colorinfo->get(*scons), colorinfo->color);
      //otherwise free allocated memory
      if( !added )
         delete scons;

      //save the properties of variables of the constraints in a struct array and in a sorted pointer array
      for( j = 0; j < ncurvars; j++ )
      {
         added = FALSE;


//         if( SCIPgetStage(scip) >= SCIP_STAGE_TRANSFORMED)
//            SCIPgetProbvarSum(scip, &(curvars[j]), &(curvals[j]), &constant);

         if( !onlysign )
         {
            scoef = new AUT_COEF(scip, seeedpool->getValsForCons(consindex)[j]);
         }
         else
         {
            if( SCIPisPositive(scip, seeedpool->getValsForCons(consindex)[j]) )
               scoef = new AUT_COEF(scip, 1.0);
            else if( SCIPisNegative(scip, seeedpool->getValsForCons(consindex)[j]) )
               scoef = new AUT_COEF(scip, -1.0);
            else
               scoef = new AUT_COEF(scip, 0.0);
         }

         //test, whether the coefficient is not zero
         if( !SCIPisZero(scip, scoef->getVal()) )
         {
            //add to pointer array iff it doesn't exist
            SCIP_CALL( colorinfo->insert(scoef, &added) );
            SCIPdebugMessage("%f color %d %d\n", scoef->getVal(), colorinfo->get(*scoef), colorinfo->color);
         }
         //otherwise free allocated memory
         if( !added )
            delete scoef;

      }
   }
   return SCIP_OKAY;
}

/** create a graph out of an array of scips */
static
SCIP_RETCODE createGraph(
   SCIP*                 scip,               /**< SCIP to compare */
   AUT_COLOR             colorinfo,          /**< data structure to save intermediate data */
   bliss::Graph*         graph,              /**< graph needed for discovering isomorphism */
   SCIP_RESULT*          result              /**< result pointer to indicate success or failure */
   )
{
   int i;
   int j;
   int z;
   int nvars;
   int nconss;
   int ncurvars;
   int curvar;
   int color;
   SCIP_CONS** conss;
   SCIP_VAR** vars;
   SCIP_VAR** curvars = NULL;
   SCIP_Real* curvals = NULL;
   unsigned int nnodes;
   SCIP_Bool onlysign;

   nnodes = 0;
   //building the graph out of the arrays
   bliss::Graph* h = graph;
   nconss = SCIPgetNConss(scip);
   nvars = SCIPgetNVars(scip);
   conss = SCIPgetConss(scip);
   vars = SCIPgetVars(scip);
   z = 0;
   onlysign = colorinfo.getOnlySign();

   //add a node for every constraint
   for( i = 0; i < nconss && *result == SCIP_SUCCESS; i++ )
   {
      ncurvars = GCGconsGetNVars(scip, conss[i]);

      AUT_CONS scons(scip, conss[i]);
      color = colorinfo.get(scons);

      if( color == -1 )
      {
         *result = SCIP_DIDNOTFIND;
         break;
      }

      assert(color >= 0);
      (void)h->add_vertex((unsigned int) color);
      nnodes++;
   }
   //add a node for every variable
   for( i = 0; i < nvars && *result == SCIP_SUCCESS; i++ )
   {
      AUT_VAR svar(scip, vars[i]);
      color = colorinfo.get(svar);

      if( color == -1 )
      {
         *result = SCIP_DIDNOTFIND;
         break;
      }
      (void) h->add_vertex((unsigned int) (colorinfo.getLenCons() + color));
      nnodes++;
   }
   //connecting the nodes with an additional node in the middle
   //it is necessary, since only nodes have colors
   for( i = 0; i < nconss && *result == SCIP_SUCCESS; i++ )
   {
      AUT_CONS scons(scip, conss[i]);
      ncurvars = GCGconsGetNVars(scip, conss[i]);
      if( ncurvars == 0 )
         continue;
      SCIP_CALL( SCIPallocBufferArray(scip, &curvars, ncurvars) );
      SCIP_CALL( GCGconsGetVars(scip, conss[i], curvars, ncurvars) );
      SCIP_CALL( SCIPallocBufferArray(scip, &curvals, ncurvars) );
      SCIP_CALL( GCGconsGetVals(scip, conss[i], curvals, ncurvars) );

      for( j = 0; j < ncurvars; j++ )
      {
         SCIP_Real constant;

         if( SCIPgetStage(scip) >= SCIP_STAGE_TRANSFORMED)
            SCIPgetProbvarSum(scip, &(curvars[j]), &(curvals[j]), &constant);

         SCIP_Real val;

         if( !onlysign )
         {
            val = curvals[j];
         }
         else
         {
            if( SCIPisPositive(scip, curvals[j]) )
               val = 1.0;
            else if( SCIPisNegative(scip, curvals[j]) )
               val = -1.0;
            else
               val = 0.0;
         }

         AUT_COEF scoef(scip, val);
         AUT_VAR svar(scip, curvars[j]);

         color = colorinfo.get(scoef);

         if( color == -1 )
         {
            *result = SCIP_DIDNOTFIND;

            break;
         }
         curvar = SCIPvarGetProbindex(curvars[j]);
         (void) h->add_vertex((unsigned int) (colorinfo.getLenCons() + colorinfo.getLenVar() + color)); /*lint !e864 */
         nnodes++;
         h->add_edge((unsigned int)i, (unsigned int) (nconss + nvars + z));
         h->add_edge((unsigned int) (nconss + nvars + z), (unsigned int) (nconss + curvar));
         SCIPdebugMessage(
               "nz: c <%s> (id: %d, colour: %d) -> nz (id: %d) (value: %f, colour: %d) -> var <%s> (id: %d, colour: %d) \n",
               SCIPconsGetName(conss[i]), i, colorinfo.get(scons),
               nconss + nvars + z, scoef.getVal(),
               color + colorinfo.getLenCons() + colorinfo.getLenVar(), /*lint !e864 */
               SCIPvarGetName(curvars[j]), nconss + curvar,
               colorinfo.get(svar) + colorinfo.getLenCons());  /*lint !e864 */
         z++;

      }

      SCIPfreeBufferArray(scip, &curvals);
      SCIPfreeBufferArray(scip, &curvars);

   }
   SCIPdebugMessage("Iteration 1: nnodes = %ud, Cons = %d, Vars = %d\n", nnodes, colorinfo.getLenCons(), colorinfo.getLenVar()); /*lint !e864 */
   assert(*result == SCIP_SUCCESS && nnodes == h->get_nof_vertices());

   //free all allocated memory
   freeMemory(scip, &colorinfo);
   return SCIP_OKAY;
}

/** create a graph out of an array of scips (for seeeds) */
static
SCIP_RETCODE createGraph(
   SCIP*                 scip,               /**< SCIP to compare */
   AUT_COLOR             colorinfo,          /**< data structure to save intermediate data */
   bliss::Graph*         graph,              /**< graph needed for discovering isomorphism */
   SCIP_RESULT*          result,             /**< result pointer to indicate success or failure */
   gcg::Seeed*           seeed,
   gcg::Seeedpool*       seeedpool
   )
{
   int i;
   int j;
   int z;
   int nvars;
   int nconss;
   int ncurvars;
   int curvar;
   int color;
   unsigned int nnodes;
   SCIP_Bool onlysign;
   nnodes = 0;
   //building the graph out of the arrays
   bliss::Graph* h = graph;
   nconss = seeed->getNOpenconss();
   nvars = seeed->getNVars();
   z = 0;
   onlysign = colorinfo.getOnlySign();

   //add a node for every constraint
   for( i = 0; i < nconss && *result == SCIP_SUCCESS; i++ )
   {
      ncurvars = seeedpool->getNVarsForCons(seeed->getOpenconss()[i]);
      SCIP_CONS* cons = seeedpool->getConsForIndex(seeed->getOpenconss()[i]);

      AUT_CONS scons(scip, cons);
      color = colorinfo.get(scons);

      if( color == -1 )
      {
         *result = SCIP_DIDNOTFIND;
         break;
      }

      assert(color >= 0);
      (void)h->add_vertex((unsigned int) color);
      nnodes++;
   }
   //add a node for every variable
   for( i = 0; i < nvars && *result == SCIP_SUCCESS; i++ )
   {
      SCIP_VAR* var = seeedpool->getVarForIndex(i);
      AUT_VAR svar(scip, var);
      color = colorinfo.get(svar);

      if( color == -1 )
      {
         *result = SCIP_DIDNOTFIND;
         break;
      }
      (void) h->add_vertex((unsigned int) (colorinfo.getLenCons() + color));
      nnodes++;
   }
   //connecting the nodes with an additional node in the middle
   //it is necessary, since only nodes have colors
   for( i = 0; i < nconss && *result == SCIP_SUCCESS; i++ )
   {
      int consindex = seeed->getOpenconss()[i];
      SCIP_CONS* cons = seeedpool->getConsForIndex(consindex);
      AUT_CONS scons(scip, cons);
      ncurvars = seeedpool->getNVarsForCons(seeed->getOpenconss()[i]);
      if( ncurvars == 0 )
         continue;

      for( j = 0; j < ncurvars; j++ )
      {
         int varindex = seeedpool->getVarsForCons(consindex)[j];
         SCIP_VAR* var = seeedpool->getVarForIndex(varindex);


//              if( SCIPgetStage(scip) >= SCIP_STAGE_TRANSFORMED)
//                 SCIPgetProbvarSum(scip, &(curvars[j]), &(curvals[j]), &constant);

         SCIP_Real val;

         if( !onlysign )
         {
            val = seeedpool->getValsForCons(consindex)[j];
         }
         else
         {
            if( SCIPisPositive(scip, seeedpool->getValsForCons(consindex)[j]) )
               val = 1.0;
            else if( SCIPisNegative(scip, seeedpool->getValsForCons(consindex)[j]) )
               val = -1.0;
            else
               val = 0.0;
         }
         *result = SCIP_SUCCESS;

         AUT_COEF scoef(scip, val);
         AUT_VAR svar(scip, var);

         color = colorinfo.get(scoef);

         if( color == -1 )
         {
            *result = SCIP_DIDNOTFIND;
            break;
         }

         curvar = SCIPvarGetProbindex(var);
         (void) h->add_vertex((unsigned int) (colorinfo.getLenCons() + colorinfo.getLenVar() + color)); /*lint !e864 */
         nnodes++;
         h->add_edge((unsigned int)i, (unsigned int) (nconss + nvars + z));
              h->add_edge((unsigned int) (nconss + nvars + z), (unsigned int) (nconss + curvar));
              SCIPdebugMessage(
                    "nz: c <%s> (id: %d, colour: %d) -> nz (id: %d) (value: %f, colour: %d) -> var <%s> (id: %d, colour: %d) \n",
                    SCIPconsGetName(cons), i, colorinfo.get(scons),
                    nconss + nvars + z, scoef.getVal(),
                    color + colorinfo.getLenCons() + colorinfo.getLenVar(), /*lint !e864 */
                    SCIPvarGetName(var), nconss + curvar,
                    colorinfo.get(svar) + colorinfo.getLenCons());  /*lint !e864 */
              z++;

      }


   }
   SCIPdebugMessage("Iteration 1: nnodes = %ud, Cons = %d, Vars = %d\n", nnodes, colorinfo.getLenCons(), colorinfo.getLenVar()); /*lint !e864 */
   assert(*result == SCIP_SUCCESS && nnodes == h->get_nof_vertices());

   //free all allocated memory
   freeMemory(scip, &colorinfo);
   return SCIP_OKAY;
}


/** creates a seeed with provided constraints in the master
 * The function will put the remaining constraints in one or more pricing problems
 * depending on whether the subproblems decompose with no variables in common.
 */
SCIP_RETCODE createSeeedFromMasterconss(
   SCIP*                 scip,                /**< SCIP data structure */
   gcg::Seeed**          newSeeed,            /**< seeed data structure */
   int*                  masterconss,         /**< constraints to be put in the master */
   int                   nmasterconss,        /**< number of constraints in the master */
   gcg::Seeed*           seeed,               /**< seeed to propagate */
   gcg::Seeedpool*       seeedpool            /**< seeedpool */
   )
{

   SCIP_HASHMAP* constoblock;
   SCIP_HASHMAP* newconstoblock;
   int nconss;
   int nvars;
   int nblocks;
   int* blockrepresentative;
   int nextblock = 1;
   SCIP_Bool* consismaster;
   int i, j;
   int* vartoblock;
   int ncurvars;

   assert(scip != NULL);
   assert(nmasterconss == 0 || masterconss != NULL);
   assert(SCIPgetStage(scip) >= SCIP_STAGE_TRANSFORMED);

   nconss = seeed->getNOpenconss();
   nvars = seeed->getNVars();

   assert( nmasterconss <= nconss );

   nblocks = nconss-nmasterconss+1;
   assert(nblocks > 0);

   SCIP_CALL( SCIPallocBufferArray(scip, &blockrepresentative, nblocks) );
   SCIP_CALL( SCIPallocBufferArray(scip, &consismaster, nconss) );
   SCIP_CALL( SCIPallocBufferArray(scip, &vartoblock, nvars) );
   SCIP_CALL( SCIPhashmapCreate(&constoblock, SCIPblkmem(scip), nconss) );
   SCIP_CALL( SCIPhashmapCreate(&newconstoblock, SCIPblkmem(scip), nconss) );

   for( i = 0; i < nmasterconss; ++i )
   {
      SCIP_CALL( SCIPhashmapInsert(constoblock,(void*) (size_t)masterconss[i], (void*) (size_t) (nblocks+1)) );
   }

   for( i = 0; i < nconss; ++i )
   {
      consismaster[i] = SCIPhashmapExists(constoblock, (void*) (size_t) seeed->getOpenconss()[i]);
   }

   for( i = 0; i < nvars; ++i )
   {
      vartoblock[i] = -1;
   }

   for( i = 0; i < nblocks; ++i )
   {
      blockrepresentative[i] = -1;
   }

   /* assign constraints to representatives */

   /* go through the all constraints */
   for( i = 0; i < nconss; ++i )
   {
      int consblock;
      int cons = seeed->getOpenconss()[i];

      if( consismaster[i] )
         continue;

      /* get variables of constraint; ignore empty constraints */
      ncurvars = seeedpool->getNVarsForCons(seeed->getOpenconss()[i]);
      assert(ncurvars >= 0);

      assert(SCIPhashmapGetImage(constoblock, (void*) (size_t) cons) == NULL);

      /* if there are no variables, put it in the first block, otherwise put it in the next block */
      if( ncurvars == 0 )
         consblock = -1;
      else
         consblock = nextblock;

      /* go through all variables */
      for( j = 0; j < ncurvars; ++j )
      {
         int var;
         int varblock;
         var = seeedpool->getVarsForCons(cons)[j];

         assert(var >= 0);

         /** @todo what about deleted variables? */
         /* get block of variable */
         varblock = vartoblock[var];

         /* if variable is already assigned to a block, assign constraint to that block */
         if( varblock > -1 && varblock != consblock )
         {
            consblock = MIN(consblock, blockrepresentative[varblock]);
            SCIPdebugPrintf("still in block %d.\n", varblock);
         }
         else if( varblock == -1 )
         {
            /* if variable is free, assign it to the new block for this constraint */
            varblock = consblock;
            assert(varblock > 0);
            assert(varblock <= nextblock);
            vartoblock[var] = varblock;
            SCIPdebugPrintf("new in block %d.\n", varblock);
         }
         else
         {
            assert((varblock > 0) && (consblock == varblock));
            SCIPdebugPrintf("no change.\n");
         }

         SCIPdebugPrintf("VARINDEX: %d (%d)\n", var, vartoblock[var]);
      }

      /* if the constraint belongs to a new block, mark it as such */
      if( consblock == nextblock )
      {
         assert(consblock > 0);
         blockrepresentative[consblock] = consblock;
         assert(blockrepresentative[consblock] > 0);
         assert(blockrepresentative[consblock] <= nextblock);
         ++(nextblock);
      }

      SCIPdebugMessage("Cons %s will be in block %d (next %d)\n", SCIPconsGetName(seeedpool->getConsForIndex(cons)), consblock, nextblock);

      for( j = 0; j < ncurvars; ++j )
      {
         int var;
         int oldblock;
         var = seeedpool->getVarsForCons(cons)[j];

         oldblock = vartoblock[var];
         assert((oldblock > 0) && (oldblock <= nextblock));

         SCIPdebugMessage("\tVar %s ", SCIPvarGetName(seeedpool->getVarForIndex(var)));
         if( oldblock != consblock )
         {
            SCIPdebugPrintf("reset from %d to block %d.\n", oldblock, consblock);
            vartoblock[var] = consblock;
            SCIPdebugPrintf("VARINDEX: %d (%d)\n", var, consblock);

            if( (blockrepresentative[oldblock] != -1) && (blockrepresentative[oldblock] > blockrepresentative[consblock]) )
            {
               int oldrepr;
               oldrepr = blockrepresentative[oldblock];
               SCIPdebugMessage("\t\tBlock representative from block %d changed from %d to %d.\n", oldblock, blockrepresentative[oldblock], consblock);
               assert(consblock > 0);
               blockrepresentative[oldblock] = consblock;
               if( (oldrepr != consblock) && (oldrepr != oldblock) )
               {
                  blockrepresentative[oldrepr] = consblock;
                  SCIPdebugMessage("\t\tBlock representative from block %d changed from %d to %d.\n", oldrepr, blockrepresentative[oldrepr], consblock);
               }
            }
         }
         else
         {
            SCIPdebugPrintf("will not be changed from %d to %d.\n", oldblock, consblock);
         }
      }
      assert(consblock >= 1 || consblock == -1);
      assert(consblock <= nextblock);

      /* store the constraint block */
      if( consblock != -1 )
      {
         SCIPdebugMessage("cons %s in block %d\n", SCIPconsGetName(seeedpool->getConsForIndex(cons)), consblock);
         SCIP_CALL( SCIPhashmapInsert(constoblock, (void*) (size_t) cons, (void*)(size_t)consblock) );
      }
      else
      {
         SCIPdebugMessage("ignoring %s\n", SCIPconsGetName(seeedpool->getConsForIndex(cons)));
      }
   }

   /* postprocess blockrepresentatives */

   int tempblock = 1;
   int maxblock = nextblock;

   assert(maxblock >= 1);
   assert(blockrepresentative != NULL );
   //SCIPdebugPrintf("Blocks: ");

   for( i = 1; i < maxblock; ++i )
   {
      /* forward replace the representatives */
      assert(blockrepresentative[i] >= 0);
      assert(blockrepresentative[i] < maxblock);
      if( blockrepresentative[i] != i )
         blockrepresentative[i] = blockrepresentative[blockrepresentative[i]];
      else
      {
         blockrepresentative[i] = tempblock;
         ++tempblock;
      }
      /* It is crucial that this condition holds */
      assert(blockrepresentative[i] <= i);
 //     SCIPdebugPrintf("%d ", blockrepresentative[i]);
   }
//   SCIPdebugPrintf("\n");

   /* convert temporary data to detectordata */

   /* fillout Constoblock */
   /* convert temporary data to detectordata */
   for( i = 0; i < nconss; ++i )
   {
      int consblock;

      int cons = seeed->getOpenconss()[i];

      if( consismaster[i] )
      {
         SCIP_CALL( SCIPhashmapInsert(newconstoblock, (void*) (size_t) cons, (void*) (size_t) (nblocks+1)) );
         continue;
      }

      if( !SCIPhashmapExists(constoblock, (void*) (size_t) cons) )
         continue;

      consblock = (int) (size_t) SCIPhashmapGetImage(constoblock, (void*) (size_t) cons); /*lint !e507*/
      assert(consblock > 0);
      consblock = blockrepresentative[consblock];
      assert(consblock <= nblocks);
      SCIP_CALL( SCIPhashmapInsert(newconstoblock, (void*) (size_t) cons, (void*)(size_t)consblock) );
      SCIPdebugMessage("%d %s\n", consblock, SCIPconsGetName(seeedpool->getConsForIndex(cons)));
   }
   (*newSeeed) = new gcg::Seeed(seeed, seeedpool);
   SCIP_CALL( (*newSeeed)->assignSeeedFromConstoblock(newconstoblock, nblocks, seeedpool) );

   SCIPfreeBufferArray(scip, &vartoblock);
   SCIPfreeBufferArray(scip, &consismaster);
   SCIPfreeBufferArray(scip, &blockrepresentative);
   SCIPhashmapFree(&constoblock);
   SCIPhashmapFree(&newconstoblock);

   return SCIP_OKAY;
}

/** destructor of detector to free user data (called when GCG is exiting) */
static
DEC_DECL_FREEDETECTOR(detectorFreeIsomorph)
{ /*lint --e{715}*/
   DEC_DETECTORDATA *detectordata;

   assert(scip != NULL);
   assert(detector != NULL);

   assert(strcmp(DECdetectorGetName(detector), DEC_DETECTORNAME) == 0);

   detectordata = DECdetectorGetData(detector);
   assert(detectordata != NULL);

   SCIPfreeMemory(scip, &detectordata);

   return SCIP_OKAY;
}

/** detector initialization method (called after problem was transformed) */
static
DEC_DECL_INITDETECTOR(detectorInitIsomorph)
{ /*lint --e{715}*/
   DEC_DETECTORDATA *detectordata;

   assert(scip != NULL);
   assert(detector != NULL);

   assert(strcmp(DECdetectorGetName(detector), DEC_DETECTORNAME) == 0);

   detectordata = DECdetectorGetData(detector);
   assert(detectordata != NULL);

   detectordata->result = SCIP_SUCCESS;
   detectordata->numofsol = DEFAULT_NUMOFSOL;

   return SCIP_OKAY;
}

/** renumbers the permutations from 0 to n-1 and returns the number of permutations
 * @return the number of permutations
 */
int renumberPermutations(
   int*                  permutation,        /**< the permutation */
   int                   permsize            /**< size of the permutation */
)
{
   // renumbering from 0 to number of permutations
   int nperms = -1;

   for( int i = 0; i < permsize; i++ )
   {
      SCIPdebugMessage("%d: %d -> ", i, permutation[i]);
      if( permutation[i] == -1 )
      {
         SCIPdebugPrintf("%d\n", permutation[i]);
         continue;
      }

      if( permutation[i] > nperms && permutation[permutation[i]] > nperms )
      {
         nperms++;
         permutation[i] = nperms;
      }
      else
      {
         permutation[i] = permutation[permutation[i]];
      }
      SCIPdebugPrintf("%d\n", permutation[i]);
   }

   return nperms+1;
}

/** collapses the permutation, if possible */
void collapsePermutation(
   int*                  permutation,        /**< the permutation */
   int                   permsize            /**< size of the permutation */
)
{
   int tmp = 0;
   // assign to a permutation circle only one number
   for( int i = 0; i < permsize; i++ )
   {
      if( permutation[i] != -1 && permutation[i] != i )
      {
         tmp = permutation[i];
         permutation[i] = permutation[tmp];
      }
      SCIPdebugMessage("%d %d\n",i, permutation[i]);

   }
}

/** filters the best permutation */
SCIP_RETCODE filterPermutation(
   SCIP*                 scip,               /**< SCIP data structure */
   int*                  permutation,        /**< the permutation */
   int                   permsize,           /**< size of the permutation */
   int                   nperms              /**< number of permutations */
)
{
   int best = 0;
   int i;
   int* count;

   assert(scip != NULL);
   assert(permutation != NULL);
   assert(permsize > 0);
   assert(nperms > 0);

   SCIP_CALL( SCIPallocBufferArray(scip, &count, nperms) );
   BMSclearMemoryArray(count, nperms);

   for( i = 0; i < permsize; ++i )
   {
      if( permutation[i] >= 0 )
         count[permutation[i]] += 1;
   }

   best = count-std::max_element(count, count+nperms);
   SCIPfreeBufferArray(scip, &count);

   for( i = 0; i < permsize; ++i )
   {
      if( permutation[i] != best )
         permutation[i] = -1;
   }


   return SCIP_OKAY;
}

/** detection function of isomorph detector */
static
SCIP_RETCODE detectIsomorph(
   SCIP*                 scip,               /**< SCIP data structure */
   int*                  ndecdecomps,        /**< pointer to store number of decompositions */
   DEC_DECOMP***         decdecomps,         /**< pointer to store decompositions */
   DEC_DETECTORDATA*     detectordata,       /**< detector data structure */
   SCIP_RESULT*          result,             /**< pointer to store result */
   SCIP_Bool             onlysign            /**< use only sign of coefficients instead of coefficients? */
)
{
   bliss::Graph graph;
   bliss::Stats bstats;
   AUT_HOOK *ptrhook;
   AUT_COLOR *colorinfo;

   int nconss = SCIPgetNConss(scip);
   int i;
   int unique;
   int oldndecdecomps;

   oldndecdecomps = *ndecdecomps;

   detectordata->result = SCIP_SUCCESS;

   colorinfo = new AUT_COLOR();

   colorinfo->setOnlySign(onlysign);

   if( !onlysign )
      SCIPverbMessage(scip, SCIP_VERBLEVEL_NORMAL, NULL, "Detecting aggregatable structure: ");
   else
      SCIPverbMessage(scip, SCIP_VERBLEVEL_NORMAL, NULL, "Detecting almost aggregatable structure: ");

   SCIP_CALL( setupArrays(scip, colorinfo, &detectordata->result) );
   SCIP_CALL( createGraph(scip, *colorinfo, &graph, &detectordata->result) );

   ptrhook = new AUT_HOOK(FALSE, graph.get_nof_vertices(), scip);
   for( i = 0; i < nconss; i++ )
   {
      ptrhook->conssperm[i] = -1;
   }

   graph.find_automorphisms(bstats, fhook, ptrhook);

   if( !ptrhook->getBool() )
      detectordata->result = SCIP_DIDNOTFIND;

   if( detectordata->result == SCIP_SUCCESS )
   {
      int nperms;
      DEC_DECOMP* newdecomp;
      int nmasterconss;
      SCIP_CONS** masterconss = NULL;
      int p;

      // assign to a permutation circle only one number
      collapsePermutation(ptrhook->conssperm, nconss);
      // renumbering from 0 to number of permutations
      nperms = renumberPermutations(ptrhook->conssperm, nconss);

      // filter decomposition with largest orbit
      if( detectordata->maxdecomps == 1)
         SCIP_CALL( filterPermutation(scip, ptrhook->conssperm, nconss, nperms) );

      if( *ndecdecomps == 0 )
         SCIP_CALL( SCIPallocMemoryArray(scip, decdecomps, *ndecdecomps + MIN(detectordata->maxdecomps, nperms)) ); /*lint !e506*/
      else
         SCIP_CALL( SCIPreallocMemoryArray(scip, decdecomps, *ndecdecomps + MIN(detectordata->maxdecomps, nperms)) ); /*lint !e506*/

      int pos = *ndecdecomps;
      for( p = *ndecdecomps; p < *ndecdecomps + nperms && pos < detectordata->maxdecomps; ++p )
      {
         SCIP_CALL( SCIPallocMemoryArray(scip, &masterconss, nconss) );

         SCIPdebugMessage("masterconss of decomp %d:\n", p);

         nmasterconss = 0;
         for( i = 0; i < nconss; i++ )
         {
            if( p - *ndecdecomps != ptrhook->conssperm[i] )
            {
               masterconss[nmasterconss] = SCIPgetConss(scip)[i];
               SCIPdebugMessage("%s\n", SCIPconsGetName(masterconss[nmasterconss]));
               nmasterconss++;
            }
         }
         SCIPdebugMessage("%d\n", nmasterconss);

         if( nmasterconss < SCIPgetNConss(scip) )
         {
            SCIP_CALL( DECcreateDecompFromMasterconss(scip, &((*decdecomps)[pos]), masterconss, nmasterconss) );

            SCIPfreeMemoryArray(scip, &masterconss);
         }
         else
         {
            SCIPfreeMemoryArray(scip, &masterconss);

            continue;
         }


         SCIP_CALL( DECcreatePolishedDecomp(scip, (*decdecomps)[pos], &newdecomp) );
         if( newdecomp != NULL )
         {
            SCIP_CALL( DECdecompFree(scip, &((*decdecomps)[pos])) );
            (*decdecomps)[pos] = newdecomp;
         }

         ++pos;
      }
      *ndecdecomps = pos;

      if( *ndecdecomps > 0 )
      {
         unique = DECfilterSimilarDecompositions(scip, *decdecomps, *ndecdecomps);
      }
      else
      {
         unique = *ndecdecomps;
      }

      for( p = unique; p < *ndecdecomps; ++p )
      {
         SCIP_CALL( DECdecompFree(scip, &((*decdecomps)[p])) );
         (*decdecomps)[p] = NULL;
      }

      *ndecdecomps = unique;

      if( *ndecdecomps > 0 )
      {
         SCIP_CALL( SCIPreallocMemoryArray(scip, decdecomps, *ndecdecomps) );
      }

      SCIPverbMessage(scip, SCIP_VERBLEVEL_NORMAL, NULL, "found %d (new) decompositions.\n", *ndecdecomps - oldndecdecomps);
   }
   else
   {
      SCIPverbMessage(scip, SCIP_VERBLEVEL_NORMAL, NULL, "not found.\n");
   }

   if( *ndecdecomps == 0 )
   {
      SCIPfreeMemoryArrayNull(scip, decdecomps);
   }

   delete colorinfo;

   delete ptrhook;

   *result = detectordata->result;

   return SCIP_OKAY;
}

/** detection function of isomorph detector for seeeds */
static
SCIP_RETCODE detectIsomorph(
   SCIP*                 scip,               /**< SCIP data structure */
   gcg::Seeed*           seeed,              /**< seeed to propagate */
   gcg::Seeedpool*       seeedpool,          /**< seeedpool */
   int*                  nNewSeeeds,         /**< pointer to store number of seeeds */
   gcg::Seeed***         newSeeeds,          /**< pointer to store seeeds */
   DEC_DETECTORDATA*     detectordata,       /**< detector data structure */
   SCIP_RESULT*          result,             /**< pointer to store result */
   SCIP_Bool             onlysign            /**< use only sign of coefficients instead of coefficients? */
)
{
   bliss::Graph graph;
   bliss::Stats bstats;
   AUT_HOOK *ptrhook;
   AUT_COLOR *colorinfo;

   int nconss = seeed->getNOpenconss();
   int i;
   int oldnseeeds;

   oldnseeeds = *nNewSeeeds;

   detectordata->result = SCIP_SUCCESS;

   colorinfo = new AUT_COLOR();

   colorinfo->setOnlySign(onlysign);


   if( !onlysign )
      SCIPverbMessage(scip, SCIP_VERBLEVEL_NORMAL, NULL, "Detecting aggregatable structure: ");
   else
      SCIPverbMessage(scip, SCIP_VERBLEVEL_NORMAL, NULL, "Detecting almost aggregatable structure: ");

   SCIP_CALL( setupArrays(scip, colorinfo, &detectordata->result, seeed, seeedpool) );
   SCIP_CALL( createGraph(scip, *colorinfo, &graph, &detectordata->result, seeed, seeedpool) );

   ptrhook = new AUT_HOOK(FALSE, graph.get_nof_vertices(), scip, seeed, seeedpool);
   for( i = 0; i < nconss; i++ )
   {
      ptrhook->conssperm[i] = -1;
   }

   graph.find_automorphisms(bstats, fhookForSeeeds, ptrhook);

   if( !ptrhook->getBool() )
      detectordata->result = SCIP_DIDNOTFIND;

   if( detectordata->result == SCIP_SUCCESS )
   {
      int nperms;
      int nmasterconss;
      int* masterconss = NULL;
      int p;

      // assign to a permutation circle only one number
      collapsePermutation(ptrhook->conssperm, nconss);
      // renumbering from 0 to number of permutations
      nperms = renumberPermutations(ptrhook->conssperm, nconss);

      // filter decomposition with largest orbit
      if( detectordata->numofsol == 1)
         SCIP_CALL( filterPermutation(scip, ptrhook->conssperm, nconss, nperms) );


      if( *nNewSeeeds == 0 )
         SCIP_CALL( SCIPallocMemoryArray(scip, newSeeeds, *nNewSeeeds + MIN(detectordata->numofsol, nperms)) ); /*lint !e506*/
      else
         SCIP_CALL( SCIPreallocMemoryArray(scip, newSeeeds, *nNewSeeeds + MIN(detectordata->numofsol, nperms)) ); /*lint !e506*/

      int pos = *nNewSeeeds;
      for( p = *nNewSeeeds; p < *nNewSeeeds + nperms && pos < detectordata->numofsol; ++p )
      {
         SCIP_CALL( SCIPallocMemoryArray(scip, &masterconss, nconss) );

         SCIPdebugMessage("masterconss of seeed %d:\n", p);

         nmasterconss = 0;
         for( i = 0; i < nconss; i++ )
         {
            if( p - *nNewSeeeds != ptrhook->conssperm[i] )
            {
               masterconss[nmasterconss] = seeed->getOpenconss()[i];
               SCIPdebugMessage("%s\n", SCIPconsGetName(seeedpool->getConsForIndex(masterconss[nmasterconss])));
               nmasterconss++;
            }
         }
         SCIPdebugMessage("%d\n", nmasterconss);

         if( nmasterconss < nconss )
         {
            SCIP_CALL( createSeeedFromMasterconss(scip, &((*newSeeeds)[pos]), masterconss, nmasterconss, seeed, seeedpool) );

            SCIPfreeMemoryArray(scip, &masterconss);
         }

         else
         {
            SCIPfreeMemoryArray(scip, &masterconss);

            continue;
         }

         ++pos;
      }
      *nNewSeeeds = pos;

      if( *nNewSeeeds > 0 )
      {
         SCIP_CALL( SCIPreallocMemoryArray(scip, newSeeeds, *nNewSeeeds) );
      }

      SCIPverbMessage(scip, SCIP_VERBLEVEL_NORMAL, NULL, "found %d (new) decompositions.\n", *nNewSeeeds - oldnseeeds);
      }
      else
      {
         SCIPverbMessage(scip, SCIP_VERBLEVEL_NORMAL, NULL, "not found.\n");
      }

      if( *nNewSeeeds == 0 )
      {
         SCIPfreeMemoryArrayNull(scip, newSeeeds);
      }


      delete colorinfo;

      delete ptrhook;

      *result = detectordata->result;

      return SCIP_OKAY;
}


//#define detectorPropagateSeeedIsomorph NULL
DEC_DECL_PROPAGATESEEED(detectorPropagateSeeedIsomorph)
{
   *result = SCIP_DIDNOTFIND;
   DEC_DETECTORDATA* detectordata = DECdetectorGetData(detector);
   gcg::Seeed* seeed = seeedPropagationData->seeedToPropagate;

   seeedPropagationData->nNewSeeeds = 0;
   seeedPropagationData->newSeeeds = NULL;

   if(seeed->getNBlocks() != 0 || seeed->getNOpenvars() != seeed->getNVars())
   {
      *result = SCIP_SUCCESS;
      return SCIP_OKAY;
   }

   if( detectordata->extend )
      SCIP_CALL( detectIsomorph(scip, seeed, seeedPropagationData->seeedpool, &(seeedPropagationData->nNewSeeeds), &(seeedPropagationData->newSeeeds), detectordata, result, TRUE) );

   if( detectordata->exact )
      SCIP_CALL( detectIsomorph(scip, seeed, seeedPropagationData->seeedpool, &(seeedPropagationData->nNewSeeeds), &(seeedPropagationData->newSeeeds), detectordata, result, FALSE) );

   for( int i = 0; i < seeedPropagationData->nNewSeeeds; ++i )
   {
      seeedPropagationData->newSeeeds[i]->setDetectorPropagated(seeedPropagationData->seeedpool->getIndexForDetector(detector));
      seeedPropagationData->newSeeeds[i]->refineToMaster(seeedPropagationData->seeedpool);
   }
   return SCIP_OKAY;
}
#define detectorExitIsomorph NULL


/** detection function of detector */
static DEC_DECL_DETECTSTRUCTURE(detectorDetectIsomorph)
{ /*lint -esym(429,ptrhook)*/

   *result = SCIP_DIDNOTFIND;

   *ndecdecomps = 0;
   *decdecomps = NULL;

   if( detectordata->extend )
      SCIP_CALL( detectIsomorph(scip, ndecdecomps, decdecomps, detectordata, result, TRUE) );

   if( detectordata->exact )
      SCIP_CALL( detectIsomorph(scip, ndecdecomps, decdecomps, detectordata, result, FALSE) );

   return SCIP_OKAY;
}

/*
 * detector specific interface methods
 */


/** creates the handler for isomorph subproblems and includes it in SCIP */
extern "C"
SCIP_RETCODE SCIPincludeDetectorIsomorphism(
   SCIP*                 scip                /**< SCIP data structure */
   )
{
   DEC_DETECTORDATA* detectordata;

   detectordata = NULL;

   SCIP_CALL( SCIPallocMemory(scip, &detectordata) );
   assert(detectordata != NULL);



   SCIP_CALL( DECincludeDetector(scip, DEC_DETECTORNAME, DEC_DECCHAR, DEC_DESC, DEC_FREQCALLROUND, DEC_MAXCALLROUND, DEC_MINCALLROUND, DEC_PRIORITY, DEC_ENABLED, DEC_SKIP, DEC_USEFULRECALL,
      detectordata, detectorDetectIsomorph, detectorFreeIsomorph, detectorInitIsomorph, detectorExitIsomorph, detectorPropagateSeeedIsomorph) );

   /* add isomorph constraint handler parameters */
   SCIP_CALL( SCIPaddIntParam(scip, "detectors/isomorph/maxdecomps",
      "Maximum number of solutions/decompositions", &detectordata->maxdecomps, FALSE,
      DEFAULT_MAXDECOMPS, 1, INT_MAX, NULL, NULL) );
   SCIP_CALL( SCIPaddBoolParam(scip, "detectors/isomorph/exact",
      "Use exact coefficients for detection?", &detectordata->exact, FALSE,
         DEFAULT_EXACT, NULL, NULL) );
   SCIP_CALL( SCIPaddBoolParam(scip, "detectors/isomorph/extend",
      "Extend detection by using the sign of the coefficients instead of the coefficients?", &detectordata->extend, FALSE,
      DEFAULT_EXTEND, NULL, NULL) );

   return SCIP_OKAY;
}
