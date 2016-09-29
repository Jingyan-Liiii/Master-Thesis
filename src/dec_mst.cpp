/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                           */
/*                  This file is part of the program                         */
/*          GCG --- Generic Column Generation                                */
/*                  a Dantzig-Wolfe decomposition based extension            */
/*                  of the branch-cut-and-price framework                    */
/*         SCIP --- Solving Constraint Integer Programs                      */
/*                                                                           */
/* Copyright (C) 2010-2015 Operations Research, RWTH Aachen University       */
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

/**@file   dec_mst.cpp
 * @ingroup DETECTORS
 * @brief  detector MST
 * @author Igor Pesic
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/
#include <time.h>     // for measuring time performance

#include "dec_mst.h"
#include "cons_decomp.h"
#include "graph/matrixgraph.h"
#include "graph/rowgraph_weighted.h"
#include "graph/graph_gcg.h"

using gcg::RowGraphWeighted;
using gcg::Weights;
using gcg::GraphGCG;


/* constraint handler properties */
#define DEC_DETECTORNAME         "mst"                               /**< name of detector */
#define DEC_DESC                 "detector based on MST clustering"  /**< description of detector*/
#define DEC_PRIORITY             910         /**< priority of the constraint handler for separation */
#define DEC_DECCHAR              'M'         /**< display character of detector */
#define DEC_ENABLED              TRUE        /**< should the detection be enabled */
#define DEC_SKIP                 FALSE       /**< should detector be skipped if other detectors found decompositions */

/* Default parameter settings*/
#define DEFAULT_N_ITERATIONS              51
#define DEFAULT_JOHNSON_ENABLE            true
#define DEFAULT_INTERSECTION_ENABLE       false
#define DEFAULT_JACCARD_ENABLE            false
#define DEFAULT_COSINE_ENABLE             false
#define DEFAULT_SIMPSON_ENABLE            false
#define DEFAULT_POSTPROC_ENABLE           true
#define MAX_N_BLOCKS                      100

/*
 * Data structures
 */

/** detector handler data */
struct DEC_DetectorData
{
   std::vector< RowGraphWeighted<GraphGCG>*> *graphs;    /**< the graph of the matrix */
   SCIP_RESULT result;                                 /**< result pointer to indicate success or failure */
   SCIP_Bool found;
   int n_iterations;
   int n_similarities;                                  /**< number of active similarities */
   SCIP_Bool johnsonenable;
   SCIP_Bool intersectionenable;
   SCIP_Bool jaccardenable;
   SCIP_Bool cosineenable;
   SCIP_Bool simpsonenable;
   SCIP_Bool postprocenable;
};


/*
 * Local methods
 */

static std::vector<double> getEpsList(int length, double mid, bool isintersection)
{
   int n1, n2;
   if(isintersection)
   {
      n1 = (int) round((length+1) / 2.0);
      n2 = n1;
   }
   else
   {
      n2 = (int) round((length+1) / 4.0);
      n1 = abs(length - n2) + 1;
   }

   double s = mid;
   double end1 = mid + 0.9;      // lower boundary
   double end2 = mid + 0.4;      // upper boundary

   double q1 = pow(end1/s, 1.0/(double)(n1-1));
   double q2 = pow(end2/s, 1.0/(double)(n2-1));

   std::vector<double> geom_seq1(n1-1);
   std::vector<double> geom_seq2(n2);

   int j = 0;
   for(int i = n1 - 1; i > 0; i--)
   {
      geom_seq1[j] = 2*s-s*pow(q1, (double)i);
      j++;
   }
   for(int i = 0; i < n2; i++)
   {
      geom_seq2[i] = s*pow(q2, (double)i);
   }

   geom_seq1.insert( geom_seq1.end(), geom_seq2.begin(), geom_seq2.end() );

   assert((int)geom_seq1.size() == length);

   return geom_seq1;
}

/*
 * detector callback methods
 */

/** destructor of detector to free user data (called when GCG is exiting) */
static
DEC_DECL_FREEDETECTOR(freeMST)
{
   DEC_DETECTORDATA* detectordata;

   assert(scip != NULL);

   detectordata = DECdetectorGetData(detector);
   assert(detectordata != NULL);
   assert(strcmp(DECdetectorGetName(detector), DEC_DETECTORNAME) == 0);

   SCIPfreeMemory(scip, &detectordata);

   return SCIP_OKAY;
}

/** destructor of detector to free detector data (called before the solving process begins) */
static
DEC_DECL_EXITDETECTOR(exitMST)
{
   DEC_DETECTORDATA* detectordata;
   assert(scip != NULL);

   detectordata = DECdetectorGetData(detector);
   assert(detectordata != NULL);
   assert(strcmp(DECdetectorGetName(detector), DEC_DETECTORNAME) == 0);

   delete detectordata->graphs;

   return SCIP_OKAY;
}


/** detection initialization function of detector (called before solving is about to begin) */
static
DEC_DECL_INITDETECTOR(initMST)
{  /*lint --e{715}*/

   DEC_DETECTORDATA* detectordata;
   assert(scip != NULL);


   detectordata = DECdetectorGetData(detector);
   assert(detectordata != NULL);
   assert(strcmp(DECdetectorGetName(detector), DEC_DETECTORNAME) == 0);

   detectordata->n_similarities = -1;
   detectordata->found = FALSE;
   detectordata->graphs = new std::vector< RowGraphWeighted<GraphGCG>*>();
   return SCIP_OKAY;
}

/** detection function of detector */
static
DEC_DECL_DETECTSTRUCTURE(detectMST)
{ /*lint --e{715}*/

   assert(scip != NULL);
   assert(detectordata != NULL);
   assert(decdecomps != NULL);
   *result = SCIP_DIDNOTFIND;

   Weights w(1, 1, 1, 1, 1, 1);

   SCIPverbMessage(scip, SCIP_VERBLEVEL_NORMAL, NULL, "Detecting MST structure:");

   time_t start, cp0, cp1, d_s, d_e;
   time(&start);

   std::vector<std::string> sim;

   if(detectordata->johnsonenable)
   {
      RowGraphWeighted<GraphGCG>* g = new RowGraphWeighted<GraphGCG>(scip, w);
      SCIP_CALL( g->createFromMatrix(SCIPgetConss(scip), SCIPgetVars(scip), SCIPgetNConss(scip), SCIPgetNVars(scip),
            gcg::DISTANCE_MEASURE::JOHNSON, gcg::WEIGHT_TYPE::DIST));
      detectordata->graphs->push_back(g);
      sim.push_back("Johnson");
   }
   if(detectordata->intersectionenable)
   {
      RowGraphWeighted<GraphGCG>* g = new RowGraphWeighted<GraphGCG>(scip, w);
      SCIP_CALL( g->createFromMatrix(SCIPgetConss(scip), SCIPgetVars(scip), SCIPgetNConss(scip), SCIPgetNVars(scip),
            gcg::DISTANCE_MEASURE::INTERSECTION, gcg::WEIGHT_TYPE::DIST));
      detectordata->graphs->push_back(g);
      sim.push_back("Intersection");
   }
   if(detectordata->jaccardenable)
   {
      RowGraphWeighted<GraphGCG>* g = new RowGraphWeighted<GraphGCG>(scip, w);
      SCIP_CALL( g->createFromMatrix(SCIPgetConss(scip), SCIPgetVars(scip), SCIPgetNConss(scip), SCIPgetNVars(scip),
            gcg::DISTANCE_MEASURE::JACCARD, gcg::WEIGHT_TYPE::DIST));
      detectordata->graphs->push_back(g);
      sim.push_back("Jaccard");
   }
   if(detectordata->cosineenable)
   {
      RowGraphWeighted<GraphGCG>* g = new RowGraphWeighted<GraphGCG>(scip, w);
      SCIP_CALL( g->createFromMatrix(SCIPgetConss(scip), SCIPgetVars(scip), SCIPgetNConss(scip), SCIPgetNVars(scip),
            gcg::DISTANCE_MEASURE::COSINE, gcg::WEIGHT_TYPE::DIST));
      detectordata->graphs->push_back(g);
      sim.push_back("Cosine");
   }
   if(detectordata->simpsonenable)
   {
      RowGraphWeighted<GraphGCG>* g = new RowGraphWeighted<GraphGCG>(scip, w);
      SCIP_CALL( g->createFromMatrix(SCIPgetConss(scip), SCIPgetVars(scip), SCIPgetNConss(scip), SCIPgetNVars(scip),
            gcg::DISTANCE_MEASURE::SIMPSON, gcg::WEIGHT_TYPE::DIST));
      detectordata->graphs->push_back(g);
      sim.push_back("Simspon");
   }
   time(&cp0);
   detectordata->n_similarities = (int) detectordata->graphs->size();

   double q = 10; // quantile to search for the percentile needed for the mid of the eps list
   std::vector<double> mids(detectordata->graphs->size());      // middle values for each eps list
   std::vector<std::vector<double> > epsLists(detectordata->graphs->size());
   for(int i = 0; i < (int)detectordata->graphs->size(); i++)
   {
      mids[i] = detectordata->graphs->at(i)->getEdgeWeightPercentile(q);
      if(i == 1 && detectordata->intersectionenable)
      {
         epsLists[i] = getEpsList(detectordata->n_iterations, mids[i], true); // case for intersection
      }
      else
      {
         epsLists[i] = getEpsList(detectordata->n_iterations, mids[i], false); // case for all except intersection
      }

   }

   time(&cp1);

   int max_ndecs = detectordata->n_iterations * detectordata->graphs->size();
   SCIP_CALL( SCIPallocMemoryArray(scip, decdecomps, max_ndecs) );

   const int max_blocks = std::min((int)round(0.3 * SCIPgetNConss(scip)), MAX_N_BLOCKS);

   *ndecdecomps = 0;
   time(&d_s);
   for(int i = 0; i < (int)detectordata->graphs->size(); i++)
   {
      RowGraphWeighted<GraphGCG>* graph = detectordata->graphs->at(i);
      SCIPverbMessage(scip, SCIP_VERBLEVEL_NORMAL, NULL, "\n  %s similarity: ", sim[i].c_str());
      int old_n_blocks = -1;
      int old_non_cl = -1;
      for(int j = 0; j < (int)epsLists[i].size() ; j++ )
      {
         double eps = epsLists[i][j];
         if(eps <= 0.0)
         {
            continue;
         }
         if(eps >= 1.0)
         {
            break;
         }

         // run MST with different eps
         SCIP_CALL( graph->computePartitionMST(eps, detectordata->postprocenable) );


         int n_blocks;
         SCIP_CALL( graph->getNBlocks(n_blocks) );
         int non_cl;

         SCIP_CALL( graph->nonClustered(non_cl) );

         // skip the case if we have too many blocks (it means we must increase eps) or if the clustering is the same as the last one
         if( n_blocks > max_blocks || n_blocks == 0 || (n_blocks == old_n_blocks && non_cl == old_non_cl) )
         {
            continue;
         }
         // stop. eps is already too big
         if( n_blocks == 1 && non_cl == 0)
         {
            break;
         }
         SCIPverbMessage(scip, SCIP_VERBLEVEL_NORMAL, NULL, "\n    Blocks: %d, Master Conss: %d/%d, ", n_blocks, non_cl, SCIPgetNConss(scip));
         old_n_blocks = n_blocks;
         old_non_cl = non_cl;


         SCIP_CALL( graph->createDecompFromPartition(&(*decdecomps)[*ndecdecomps]) );

         auto check = DECdecompGetNLinkingvars((*decdecomps)[*ndecdecomps]);
         SCIPverbMessage(scip, SCIP_VERBLEVEL_NORMAL, NULL, "Link Vars: %d. ", check);

         if( (*decdecomps)[*ndecdecomps] != NULL )
         {
            *ndecdecomps += 1;
            detectordata->found = TRUE;
         }
      }
      delete detectordata->graphs->at(i);
      detectordata->graphs->at(i) = NULL;
   }

   detectordata->graphs->clear();
   time(&d_e);
   double elapsed_graphs = difftime(cp0, start);
   double elapsed_mst = difftime(d_e, d_s);

   SCIPverbMessage(scip, SCIP_VERBLEVEL_NORMAL, NULL, " done, %d similarities used, %d decompositions found.\n", detectordata->n_similarities, *ndecdecomps);
   SCIPverbMessage(scip, SCIP_VERBLEVEL_NORMAL, NULL, "MST runtime: graphs: %.2lf, mst: %.2lf. \n", elapsed_graphs, elapsed_mst);

   SCIP_CALL( SCIPreallocMemoryArray(scip, decdecomps, *ndecdecomps) );

   *result = *ndecdecomps > 0 ? SCIP_SUCCESS: SCIP_DIDNOTFIND;
   if( *ndecdecomps == 0 )
   {
      SCIPfreeMemoryArrayNull(scip, decdecomps);
   }
   return SCIP_OKAY;
}

#define propagateSeeedMST NULL


/*
 * detector specific interface methods
 */

/** creates the handler for xyz detector and includes it in SCIP */
SCIP_RETCODE SCIPincludeDetectorMST(
   SCIP*                 scip                /**< SCIP data structure */
   )
{
#if !defined(_WIN32) && !defined(_WIN64)
   DEC_DETECTORDATA *detectordata = NULL;
   assert(scip != NULL);

   SCIP_CALL( SCIPallocMemory(scip, &detectordata) );

   assert(detectordata != NULL);
   detectordata->found = FALSE;

   SCIP_CALL( DECincludeDetector(scip, DEC_DETECTORNAME, DEC_DECCHAR, DEC_DESC, DEC_PRIORITY, DEC_ENABLED, DEC_SKIP,
      detectordata, detectMST, freeMST, initMST, exitMST, propagateSeeedMST) );

   /* add arrowheur presolver parameters */
   SCIP_CALL( SCIPaddIntParam(scip, "detectors/mst/niterations", "Number of iterations to run mst with different eps.", &detectordata->n_iterations, FALSE, DEFAULT_N_ITERATIONS, 11, 1001, NULL, NULL) );
   SCIP_CALL( SCIPaddBoolParam(scip, "detectors/mst/johson", "Enable johson distance measure.", &detectordata->johnsonenable, FALSE, DEFAULT_JOHNSON_ENABLE, NULL, NULL ) );
   SCIP_CALL( SCIPaddBoolParam(scip, "detectors/mst/intersection", "Enable intersection distance measure.", &detectordata->intersectionenable, FALSE, DEFAULT_INTERSECTION_ENABLE, NULL, NULL ) );
   SCIP_CALL( SCIPaddBoolParam(scip, "detectors/mst/jaccard", "Enable jaccard distance measure.", &detectordata->jaccardenable, FALSE, DEFAULT_JACCARD_ENABLE, NULL, NULL) );
   SCIP_CALL( SCIPaddBoolParam(scip, "detectors/mst/cosine", "Enable cosine distance measure.", &detectordata->cosineenable, FALSE, DEFAULT_COSINE_ENABLE, NULL, NULL) );
   SCIP_CALL( SCIPaddBoolParam(scip, "detectors/mst/simpson", "Enable simpson distance measure.", &detectordata->simpsonenable, FALSE, DEFAULT_SIMPSON_ENABLE, NULL, NULL ) );
   SCIP_CALL( SCIPaddBoolParam(scip, "detectors/mst/postprocenable", "Enable post-processing step..", &detectordata->postprocenable, FALSE, DEFAULT_POSTPROC_ENABLE, NULL, NULL ) );

#endif
   return SCIP_OKAY;
}