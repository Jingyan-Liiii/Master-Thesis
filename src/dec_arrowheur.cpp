/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                           */
/*                  This file is part of the program                         */
/*          GCG --- Generic Column Generation                                */
/*                  a Dantzig-Wolfe decomposition based extension            */
/*                  of the branch-cut-and-price framework                    */
/*         SCIP --- Solving Constraint Integer Programs                      */
/*                                                                           */
/* Copyright (C) 2010-2013 Operations Research, RWTH Aachen University       */
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

/**@file   dec_arrowheur.c
 * @brief  arrowheur detector
 * @author Martin Bergner
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/
/* #define SCIP_DEBUG */

#include <cassert>
#include <cstring>
#include <cerrno>

#include "dec_arrowheur.h"

#include "cons_decomp.h"
#include "struct_decomp.h"
#include "pub_decomp.h"
#include "scip_misc.h"
#include "scip/pub_misc.h"
#include "scip/cons_linear.h"
#include "scip/cons_setppc.h"
#include "graph/hyperrowcolgraph.h"
#include "graph/weights.h"

#include <set>

using gcg::HyperrowcolGraph;
using gcg::Weights;

#define DEC_DETECTORNAME      "arrowheur"    /**< name of the detector */
#define DEC_DESC              "enforces arrowhead structures using graph partitioning" /**< description of detector */
#define DEC_PRIORITY          1000           /**< priority of the detector */
#define DEC_DECCHAR           'a'            /**< display character of detector */
#define DEC_ENABLED           TRUE           /**< should detector be called by default */

/* Default parameter settings */
#define DEFAULT_VARWEIGHT         1          /**< weight for variable nodes */
#define DEFAULT_VARWEIGHTBIN      2          /**< weight for binary variable nodes */
#define DEFAULT_VARWEIGHTINT      2          /**< weight for integer variable nodes */
#define DEFAULT_VARWEIGHTCONT     1          /**< weight for continous variable nodes */
#define DEFAULT_VARWEIGHTIMPL     2          /**< weight for implicit integer variable nodes */
#define DEFAULT_CONSWEIGHT        5          /**< weight for constraint hyperedges */
#define DEFAULT_RANDSEED          1          /**< random seed for the hmetis call */
#define DEFAULT_TIDY              TRUE       /**< whether to clean up afterwards */
#define DEFAULT_DUMMYNODES        0.2        /**< percentage of dummy vertices*/
#define DEFAULT_CONSWEIGHT_SETPPC 5          /**< weight for constraint hyperedges that are setpartitioning or covering constraints */
#define DEFAULT_MAXBLOCKS         10         /**< value for the maximum number of blocks to be considered */
#define DEFAULT_MINBLOCKS         2          /**< value for the minimum number of blocks to be considered */
#define DEFAULT_ALPHA             0.0        /**< factor for standard deviation of constraint weights */
#define DEFAULT_BETA              0.5        /**< factor of how the weight for equality and inequality constraints is distributed (keep 1/2 for the same on both) */
#define DEFAULT_METIS_UBFACTOR    5.0        /**< default unbalance factor given to metis on the commandline */
#define DEFAULT_METIS_VERBOSE     FALSE      /**< should metis be verbose */
#define DEFAULT_METISUSEPTYPE_RB  TRUE       /**< Should metis use the rb or kway partitioning algorithm */
#define DEFAULT_REALNAME          FALSE      /**< whether the metis name should be real or temporary */

/*
 * Data structures
 */

/** private detector data */
struct DEC_DetectorData
{
   /* Graph stuff for hmetis */
   HyperrowcolGraph* graph;                   /**< the graph of the matrix */
   char           tempfile[SCIP_MAXSTRLEN];  /**< filename for the metis input file */

   /* weight parameters */
   int       varWeight;             /**< weight of a variable hyperedge */
   int       varWeightBinary;       /**< weight of a binary variable hyperedge */
   int       varWeightContinous;    /**< weight of a continuous variable hyperedge */
   int       varWeightInteger;      /**< weight of an integer variable hyperedge */
   int       varWeightImplint;      /**< weight of an implicit integer variable hyperedge */
   int       consWeight;            /**< weight of a constraint hyperedge */
   int       consWeightSetppc;      /**< weight of a setppc constraint hyperedge */
   SCIP_Real alpha;                 /**< factor for constraint coefficient value standard deviation */
   SCIP_Real beta;                  /**< factor for equality od inequality constraints */

   /* general parameters */
   SCIP_Real dummynodes;      /**< percent of dummy nodes */
   SCIP_Bool tidy;            /**< whether tempory metis files should be cleaned up */
   int       maxblocks;       /**< maximal number of blocks to test */
   int       minblocks;       /**< minimal number of blocks to test */

   /* metis parameters */
   int       randomseed;      /**< metis random seed */
   SCIP_Real metisubfactor;   /**< metis unbalance factor */
   SCIP_Bool metisverbose;    /**< should metis ouput be displayed */
   SCIP_Bool metisuseptyperb; /**< flag to indicate whether metis uses kway or rb partitioning */
   SCIP_Bool realname;        /**< flag to indicate real problem name or temporary filename for metis files */

   /* various data */
   SCIP_CLOCK* metisclock;    /**< clock to measure metis time */
   int         blocks;        /**< indicates the current block */
   SCIP_Bool   found;         /**< indicates whethere a decomposition has been found */
};

/*
 * Local methods
 */

/* put your local methods here, and declare them static */

/** detector initialization method */
static
DEC_DECL_INITDETECTOR(initArrowheur)
{
   int nconss;
   DEC_DETECTORDATA* detectordata;
   assert(scip != NULL);

   detectordata = DECdetectorGetData(detector);
   assert(detectordata != NULL);
   assert(strcmp(DECdetectorGetName(detector), DEC_DETECTORNAME) == 0);

   nconss = SCIPgetNConss(scip);
   detectordata->maxblocks = MIN(nconss, detectordata->maxblocks);

   SCIP_CALL( SCIPcreateWallClock(scip, &detectordata->metisclock) );

   return SCIP_OKAY;
}

/** presolving deinitialization method of presolver (called after presolving has been finished) */
static
DEC_DECL_EXITDETECTOR(exitArrowheur)
{
   DEC_DETECTORDATA* detectordata;

   assert(scip != NULL);

   detectordata = DECdetectorGetData(detector);
   assert(detectordata != NULL);

   assert(strcmp(DECdetectorGetName(detector), DEC_DETECTORNAME) == 0);
   /* copy data to decomp structure */
   if( !detectordata->found )
   {
      SCIPfreeMemory(scip, &detectordata);
      return SCIP_OKAY;
   }

   SCIP_CALL( SCIPfreeClock(scip, &detectordata->metisclock) );
   SCIPfreeMemory(scip, &detectordata);

   return SCIP_OKAY;
}

/** will call hmetis via a system call */
static
SCIP_RETCODE callMetis(
   SCIP*                 scip,               /**< SCIP data struture */
   DEC_DETECTORDATA*     detectordata,       /**< presolver data data structure */
   SCIP_RESULT*          result              /**< result indicating whether the detection was successful */
   )
{
   char metiscall[SCIP_MAXSTRLEN];
   char metisout[SCIP_MAXSTRLEN];

   int status;

   SCIP_Real remainingtime;

   assert(scip != NULL);
   assert(detectordata != NULL);

   *result = SCIP_DIDNOTRUN;

   remainingtime = DECgetRemainingTime(scip);

   if( remainingtime <= 0 )
   {
      return SCIP_OKAY;
   }

   /* call metis via syscall as there is no library usable ... */
   if( !SCIPisInfinity(scip, DECgetRemainingTime(scip)) )
   {
      (void) SCIPsnprintf(metiscall, SCIP_MAXSTRLEN, "zsh -c \"ulimit -t %.0f;hmetis %s %d -seed %d -ptype %s -ufactor %f %s\"",
               remainingtime,
               detectordata->tempfile,
               detectordata->blocks,
               detectordata->randomseed,
               detectordata->metisuseptyperb ? "rb" : "kway",
               detectordata->metisubfactor,
               detectordata->metisverbose ? "" : "> /dev/null" );
   }
   else
   {
      (void) SCIPsnprintf(metiscall, SCIP_MAXSTRLEN, "zsh -c \"hmetis %s %d -seed %d -ptype %s -ufactor %f %s\"",
               detectordata->tempfile,
               detectordata->blocks,
               detectordata->randomseed,
               detectordata->metisuseptyperb ? "rb" : "kway",
               detectordata->metisubfactor,
               detectordata->metisverbose ? "" : "> /dev/null" );
   }

   SCIP_CALL( SCIPresetClock(scip, detectordata->metisclock) );
   SCIP_CALL( SCIPstartClock(scip, detectordata->metisclock) );
   SCIPdebugMessage("Calling metis with: %s\n", metiscall);
   SCIPverbMessage(scip, SCIP_VERBLEVEL_NORMAL, NULL, " %d", detectordata->blocks );
   status = system( metiscall );

   SCIP_CALL( SCIPstopClock(scip, detectordata->metisclock) );
   SCIPdebugMessage("time left before metis started: %f, time metis spend %f, remainingtime: %f\n", remainingtime, SCIPgetClockTime(scip, detectordata->metisclock),  remainingtime-SCIPgetClockTime(scip, detectordata->metisclock) );

   /* check error codes */
   if( status == -1 )
   {
      SCIPerrorMessage("System call did not succed: %s\n", strerror( errno ));
      SCIPerrorMessage("Call was %s\n", metiscall);
   }
   else if( status != 0 )
   {

      SCIPerrorMessage("Calling hmetis unsuccessful! See the above error message for more details.\n");
      SCIPerrorMessage("Call was %s\n", metiscall);
   }

   /* exit gracefully in case of errors */
   if( status != 0 )
   {
      return SCIP_ERROR;
   }

   (void) SCIPsnprintf(metisout, SCIP_MAXSTRLEN, "%s.part.%d", detectordata->tempfile, detectordata->blocks);
   SCIP_CALL( detectordata->graph->readPartition(metisout) );

   /* if desired delete the temoprary metis file */
   if( detectordata->tidy )
   {
      status = remove( metisout );
      if( status == -1 )
      {
         SCIPerrorMessage("Could not remove metis output file: %s\n", strerror( errno ));
         return SCIP_WRITEERROR;
      }
   }
   else
   {
      SCIPinfoMessage(scip, NULL, "Temporary file is in: %s\n", detectordata->tempfile);
   }
   *result = SCIP_SUCCESS;
   return SCIP_OKAY;
}

/** creates the temporary metis input file */
static
SCIP_RETCODE createMetisFile(
   SCIP*                 scip,               /**< SCIP data struture */
   DEC_DETECTORDATA*     detectordata        /**< detector data structure */
   )
{
   int nvertices;
   int ndummyvertices;
   char* filename;

   nvertices = detectordata->graph->getNNodes();
   /*lint --e{524}*/
   ndummyvertices = SCIPceil(scip, detectordata->dummynodes*nvertices);
   detectordata->graph->setDummynodes(ndummyvertices);

   if( !detectordata->realname )
   {
      (void) SCIPsnprintf(detectordata->tempfile, SCIP_MAXSTRLEN, "gcg-metis-XXXXXX");
   }
   else
   {
      (void) SCIPsnprintf(detectordata->tempfile, SCIP_MAXSTRLEN, "gcg-%s-XXXXXX", SCIPgetProbName(scip));
   }

   filename = mktemp(detectordata->tempfile);

   SCIP_CALL( detectordata->graph->writeToFile(filename, TRUE) );
   return SCIP_OKAY;
}

/** detection callback method */
static
DEC_DECL_DETECTSTRUCTURE(detectAndBuildArrowhead)
{
   int i;
   int j;
   int ndecs;
   int status;

   assert(scip != NULL);
   assert(detectordata != NULL);
   assert(decdecomps != NULL);
   assert(ndecdecomps != NULL);

   SCIPdebugMessage("Detecting structure from %s\n", DEC_DETECTORNAME);
   ndecs = detectordata->maxblocks-detectordata->minblocks+1;
   *ndecdecomps = 0;

   /* allocate space for output data */
   assert(detectordata->maxblocks >= detectordata->minblocks);
   SCIP_CALL( SCIPallocMemoryArray(scip, decdecomps, ndecs) );

   /* build the hypergraph structure from the original problem */

   Weights w(detectordata->varWeight, detectordata->varWeightBinary, detectordata->varWeightContinous,detectordata->varWeightInteger,detectordata->varWeightInteger,detectordata->consWeight);
   detectordata->graph = new HyperrowcolGraph(scip, w);
   SCIP_CALL( detectordata->graph->createFromMatrix(SCIPgetConss(scip), SCIPgetVars(scip), SCIPgetNConss(scip), SCIPgetNVars(scip)) );
   SCIP_CALL( createMetisFile(scip, detectordata) );

   SCIPverbMessage(scip, SCIP_VERBLEVEL_NORMAL, NULL, "Detecting Arrowhead structure:");
   for( j = 0, i = detectordata->minblocks; i <= detectordata->maxblocks; ++i )
   {
      detectordata->blocks = i;
      /* get the partitions for the new variables from metis */
      SCIP_CALL( callMetis(scip, detectordata, result) );

      if( *result != SCIP_SUCCESS )
      {
         *result = SCIP_DIDNOTFIND;
         return SCIP_OKAY;
      }
      else
      {
         detectordata->found = TRUE;
      }

      SCIP_CALL( detectordata->graph->createDecompFromPartition(&(*decdecomps)[j]) );
      if( (*decdecomps)[j] != NULL )
      {
         *ndecdecomps += 1;
         ++j;
      }
   }
   SCIPverbMessage(scip, SCIP_VERBLEVEL_NORMAL, NULL, " done, %d decompositions found.\n",  *ndecdecomps);

   delete detectordata->graph;
   detectordata->graph = NULL;

   SCIP_CALL( SCIPreallocMemoryArray(scip, decdecomps, *ndecdecomps) );

   if( detectordata->tidy )
   {
      status = remove( detectordata->tempfile );
      if( status == -1 )
      {
         SCIPerrorMessage("Could not remove metis input file: ", strerror( errno ));
         return SCIP_WRITEERROR;
      }
   }

   *result = SCIP_SUCCESS;
   return SCIP_OKAY;
}


/** creates the arrowheur presolver and includes it in SCIP */
SCIP_RETCODE SCIPincludeDetectionArrowheur(
   SCIP*                 scip                /**< SCIP data structure */
   )
{
   DEC_DETECTORDATA *detectordata;
   assert(scip != NULL);

   SCIP_CALL( SCIPallocMemory(scip, &detectordata) );

   assert(detectordata != NULL);
   detectordata->found = FALSE;
   detectordata->blocks = -1;

   SCIP_CALL( DECincludeDetector(scip, DEC_DETECTORNAME, DEC_DECCHAR, DEC_DESC, DEC_PRIORITY, DEC_ENABLED, detectordata, detectAndBuildArrowhead, initArrowheur, exitArrowheur) );


   /* add arrowheur presolver parameters */
   SCIP_CALL( SCIPaddIntParam(scip, "detectors/arrowheur/maxblocks", "The maximal number of blocks", &detectordata->maxblocks, FALSE, DEFAULT_MAXBLOCKS, 2, 1000000, NULL, NULL) );
   SCIP_CALL( SCIPaddIntParam(scip, "detectors/arrowheur/minblocks", "The minimal number of blocks", &detectordata->minblocks, FALSE, DEFAULT_MINBLOCKS, 2, 1000000, NULL, NULL) );
   SCIP_CALL( SCIPaddRealParam(scip, "detectors/arrowheur/beta", "factor on how heavy equality (beta) and inequality constraints are measured", &detectordata->beta, FALSE, DEFAULT_BETA, 0.0, 1.0, NULL, NULL ) );
   SCIP_CALL( SCIPaddRealParam(scip, "detectors/arrowheur/alpha", "factor on how heavy the standard deviation of the coefficients is measured", &detectordata->alpha, FALSE, DEFAULT_ALPHA, 0.0, 1E20, NULL, NULL ) );
   SCIP_CALL( SCIPaddIntParam(scip, "detectors/arrowheur/varWeight", "Weight of a variable hyperedge", &detectordata->varWeight, FALSE, DEFAULT_VARWEIGHT, 0, 1000000, NULL, NULL) );
   SCIP_CALL( SCIPaddIntParam(scip, "detectors/arrowheur/varWeightBinary", "Weight of a binary variable hyperedge", &detectordata->varWeightBinary, FALSE, DEFAULT_VARWEIGHTBIN, 0, 1000000, NULL, NULL) );
   SCIP_CALL( SCIPaddIntParam(scip, "detectors/arrowheur/varWeightContinous", "Weight of a continuos variable hyperedge", &detectordata->varWeightContinous, FALSE, DEFAULT_VARWEIGHTCONT, 0, 1000000, NULL, NULL) );
   SCIP_CALL( SCIPaddIntParam(scip, "detectors/arrowheur/varWeightImplint", "Weight of a implicit integer variable hyperedge", &detectordata->varWeightImplint, FALSE, DEFAULT_VARWEIGHTIMPL, 0, 1000000, NULL, NULL) );
   SCIP_CALL( SCIPaddIntParam(scip, "detectors/arrowheur/varWeightInteger", "Weight of a integer variable hyperedge", &detectordata->varWeightInteger, FALSE, DEFAULT_VARWEIGHTINT, 0, 1000000, NULL, NULL) );
   SCIP_CALL( SCIPaddIntParam(scip, "detectors/arrowheur/consWeight", "Weight of a constraint hyperedge", &detectordata->consWeight, FALSE, DEFAULT_CONSWEIGHT, 0, 1000000, NULL, NULL) );
   SCIP_CALL( SCIPaddBoolParam(scip, "detectors/arrowheur/tidy", "Whether to clean up temporary files", &detectordata->tidy, FALSE, DEFAULT_TIDY, NULL, NULL) );
   SCIP_CALL( SCIPaddIntParam(scip, "detectors/arrowheur/randomseed", "random seed for hmetis", &detectordata->randomseed, FALSE, DEFAULT_RANDSEED, -1, INT_MAX, NULL, NULL) );
   SCIP_CALL( SCIPaddRealParam(scip, "detectors/arrowheur/dummynodes", "percentage of dummy nodes for metis", &detectordata->dummynodes, FALSE, DEFAULT_DUMMYNODES, 0.0, 1.0, NULL, NULL) );
   SCIP_CALL( SCIPaddIntParam(scip, "detectors/arrowheur/consWeightSetppc", "Weight for constraint hyperedges that are setpartitioning or covering constraints", &detectordata->consWeightSetppc, FALSE, DEFAULT_CONSWEIGHT_SETPPC, 0, 1000000, NULL, NULL) );
   SCIP_CALL( SCIPaddRealParam(scip, "detectors/arrowheur/ubfactor", "Unbalance factor for metis", &detectordata->metisubfactor, FALSE, DEFAULT_METIS_UBFACTOR, 0.0, 1E20, NULL, NULL ) );
   SCIP_CALL( SCIPaddBoolParam(scip, "detectors/arrowheur/metisverbose", "Should the metis output be displayed", &detectordata->metisverbose, FALSE, DEFAULT_METIS_VERBOSE, NULL, NULL ) );
   SCIP_CALL( SCIPaddBoolParam(scip, "detectors/arrowheur/metisuseptyperb", "Should the rb or kway method be used for partitioning by metis", &detectordata->metisuseptyperb, FALSE, DEFAULT_METISUSEPTYPE_RB, NULL, NULL) );
   SCIP_CALL( SCIPaddBoolParam(scip, "detectors/arrowheur/realname", "Should the problem be used for metis files or a temporary name", &detectordata->realname, FALSE, DEFAULT_REALNAME, NULL, NULL) );

   return SCIP_OKAY;
}