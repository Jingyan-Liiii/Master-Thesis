/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                           */
/*                  This file is part of the program                         */
/*          GCG --- Generic Column Generation                                */
/*                  a Dantzig-Wolfe decomposition based extension            */
/*                  of the branch-cut-and-price framework                    */
/*         SCIP --- Solving Constraint Integer Programs                      */
/*                                                                           */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/* #define SCIP_DEBUG */
//#define CHECKCONSISTENCY
/**@file stat.c
 * @brief  Some printing methods for statistics
 * @author Alexander Gross
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/
#include "scip/scip.h"
#include "stat.h"
#include "scip_misc.h"
#include "pub_decomp.h"
#include "cons_decomp.h"
#include "struct_detector.h"
#include "pub_gcgvar.h"
#include "struct_vardata.h"

/** prints information about the best decomposition*/
SCIP_RETCODE writeDecompositionData(SCIP* scip)
{
   DECDECOMP* decomposition = NULL;
   DEC_DETECTOR* detector;
   DEC_DECTYPE type;
   const char* typeName;
   const char* detectorName;
   int i, nBlocks, nLinkingCons, nLinkingVars;
   int* nVarsInBlocks;
   int* nConsInBlocks;
   char* ausgabe;

   SCIPallocBufferArray(scip, &ausgabe, SCIP_MAXSTRLEN);

   decomposition = DECgetBestDecomp(scip);
   type = DECdecdecompGetType(decomposition);
   typeName = DECgetStrType(type);

   detector = DECdecdecompGetDetector(decomposition);
   if( detector != NULL )
      detectorName = detector->name;
   else
      detectorName = NULL;

   nBlocks = DECdecdecompGetNBlocks(decomposition);

   nVarsInBlocks = DECdecdecompGetNSubscipvars(decomposition);
   nConsInBlocks = DECdecdecompGetNSubscipconss(decomposition);

   nLinkingVars = DECdecdecompGetNLinkingvars(decomposition);
   nLinkingCons = DECdecdecompGetNLinkingconss(decomposition);

   //Print
   SCIPinfoMessage(scip, NULL, "Decomposition:\n");
   SCIPsnprintf(ausgabe, SCIP_MAXSTRLEN, "Decomposition Type: %s \n", typeName);
   SCIPinfoMessage(scip, NULL, ausgabe);

   SCIPsnprintf(ausgabe, SCIP_MAXSTRLEN, "Decomposition Detector: %s\n", detectorName == NULL? "reader": detectorName );
   SCIPinfoMessage(scip, NULL, ausgabe);

   SCIPinfoMessage(scip, NULL, "Number of Blocks: %d \n", nBlocks);
   SCIPinfoMessage(scip, NULL, "Number of LinkingVars: %d\n", nLinkingVars);
   SCIPinfoMessage(scip, NULL, "Number of LinkingCons: %d\n", nLinkingCons);

   SCIPinfoMessage(scip, NULL, "Block Information\n");
   SCIPinfoMessage(scip, NULL, "no.:\t\t#Vars\t\t#Constraints\n");
   for( i = 0; i < nBlocks; i++ )
   {
      //SCIPinfoMessage(scip,NULL,"Vars in Block %d: %d\n", i ,nVarsInBlocks[i]);
      SCIPinfoMessage(scip, NULL, "%d:\t\t%d\t\t%d\n", i, nVarsInBlocks[i], nConsInBlocks[i]);
      //SCIPinfoMessage(scip,NULL,"Cons in Block %d: %d\n",i,nConsInBlocks[i]);
   }

   SCIPfreeBufferArray(scip, &ausgabe);

   return SCIP_OKAY;
}

/** prints information about the creation of the Vars*/
SCIP_RETCODE writeVarCreationDetails(SCIP* scip)
{
   SCIP_VAR** vars;
   SCIP_VARDATA* vardata;
   SCIP_SOL* sol;
   SCIP_Real time;
   SCIP_Real solvingtime;
   SCIP_Longint nnodes;

   int nvars, i, n;
   long long int node;
   long long int* createnodestat;
   int* nodes;         /** < Wurzel Knoten und nicht wurzelknoten  */
   int* createtimestat;
   int* createiterstat;
   int iteration;
   int m;

   SCIP_Real redcost;
   SCIP_Real gap;

   nvars = SCIPgetNVars(scip);
   nnodes = SCIPgetNNodes(scip);
   sol = SCIPgetBestSol(scip);

   solvingtime = SCIPgetSolvingTime(scip);

   SCIP_CALL( SCIPallocBufferArray(scip,&vars,nvars));
   SCIP_CALL( SCIPallocBufferArray(scip,&nodes,2));         /** < 0= WurzelKnoten,1= alle anderen */
   SCIP_CALL( SCIPallocBufferArray(scip,&createnodestat,nnodes));
   SCIP_CALL( SCIPallocBufferArray(scip,&createtimestat,10));
   SCIP_CALL( SCIPallocBufferArray(scip,&createiterstat,10));
   vars = SCIPgetVars(scip);

   SCIPinfoMessage(scip,NULL,"AddedVarDetails:\n");

   for( i = 0; i < 10; i++ )
   {
      createtimestat[i] = 0;
      createiterstat[i] = 0;
   }

   nodes[0]=0;
   nodes[1]=0;

   SCIPinfoMessage(scip, NULL, "VAR: name\tnode\ttime\titer\tredcost\tgap\tsolval\n");
   for( i = 0; i < nvars; i++ )
   {
      vardata = SCIPvarGetData(vars[i]);
      node = GCGgetCreationNode(scip, vardata);
      time = GCGgetCreationTime(scip, vardata);
      iteration = GCGgetIteration(scip, vardata);
      redcost = GCGgetRedcost(scip, vardata);
      gap = GCGgetGap(scip, vardata);

      SCIPinfoMessage(scip, NULL, "VAR: <%s>\t%lld\t%f\t%d\t%f\t%f\t%f\n", SCIPvarGetName(vars[i]), node, time, iteration, redcost, gap, SCIPgetSolVal(scip, sol, vars[i]));

      if( SCIPisEQ(scip, SCIPgetSolVal(scip, sol, vars[i]), 0.0) )
      {
         continue;
      }
      else
      {
         SCIPdebugMessage("var <%s> has sol value %f (%lld, %f)\n", SCIPvarGetName(vars[i]),
         SCIPgetSolVal(scip, sol, vars[i]), node, time);
      }

      n = (int)(100 * time / solvingtime) % 10;
      m = (int)(100 * iteration / SCIPgetNLPIterations(scip)) % 10;
      createiterstat[n]++;
      createtimestat[m]++;

      if(node==1)
      {
         nodes[0]++;
      }
      else
      {
         nodes[1]++;
      }
   }


   SCIPinfoMessage(scip,NULL,"Root node:\tAdded Vars %d\n",nodes[0]);
   SCIPinfoMessage(scip,NULL,"Leftover nodes:\tAdded Vars %d\n",nodes[1]);

   for( i = 0; i < 10; i++ )
   {
         SCIPinfoMessage(scip, NULL,"Time %d-%d%%: Vars: %d \n", 10 * i, 10 * (i + 1), createtimestat[i]);
   }


   for( i = 0; i < 10; i++ )
   {
         SCIPinfoMessage(scip, NULL,"Iter %d-%d%%: Vars: %d \n", 10 * i, 10 * (i + 1), createiterstat[i]);
   }


   return SCIP_OKAY;
}