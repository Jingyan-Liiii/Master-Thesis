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

/**@file   dec_staircase.c
 * @ingroup DETECTORS
 * @brief  detector for staircase matrices
 * @author Martin Bergner
 *
 * This detector detects staircase structures in the constraint matrix by searching for the longest shortest path
 * in the row graph of the matrix.
 *
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/
/* #define SCIP_DEBUG */

#include <assert.h>
#include <string.h>

#include "dec_staircase.h"
#include "cons_decomp.h"
#include "scip_misc.h"
#include "pub_decomp.h"
#include "tclique/tclique.h"

/* constraint handler properties */
#define DEC_DETECTORNAME         "staircase"    /**< name of detector */
#define DEC_DESC                 "Staircase detection via shortest paths" /**< description of detector */
#define DEC_PRIORITY             200            /**< priority of the detector */
#define DEC_DECCHAR              'S'            /**< display character of detector */
#define DEC_ENABLED              TRUE           /**< should the detection be enabled */
#define DEC_SKIP                 FALSE          /**< should detector be skipped if others found detections */


#define TCLIQUE_CALL(x) do                                                                                    \
                       {                                                                                      \
                          if((x) != TRUE )                                                      \
                          {                                                                                   \
                             SCIPerrorMessage("Error in function call\n");                                    \
                             return SCIP_ERROR;                                                               \
                           }                                                                                  \
                       }                                                                                      \
                       while( FALSE )

/*
 * Data structures
 */

/** constraint handler data */
struct DEC_DetectorData
{
   SCIP_HASHMAP*  constoblock;
   SCIP_HASHMAP*  vartoblock;
   TCLIQUE_GRAPH* graph;
   int*           components;
   int            ncomponents;
   int            nblocks;
};


/*
 * Local methods
 */

/* put your local methods here, and declare them static */

static SCIP_DECL_SORTPTRCOMP(cmp)
{
   if( elem1 == elem2 )
      return 0;
   else if( elem1 < elem2 )
      return -1;
   else {
      assert(elem1 > elem2);
      return 1;
   }
}

/** creates the graph from the constraint matrix */
static
SCIP_RETCODE createGraph(
   SCIP*                 scip,               /**< SCIP data structure */
   TCLIQUE_GRAPH**       graph               /**< Graph data structure */
   )
{
   int i;
   int j;
   int v;
   int nconss;
   SCIP_CONS** conss;
   SCIP_Bool useprobvars = FALSE;

   assert(scip != NULL);
   assert(graph != NULL);

   nconss = SCIPgetNConss(scip);
   conss = SCIPgetConss(scip);

   TCLIQUE_CALL( tcliqueCreate(graph) );
   assert(*graph != NULL);

   for( i = 0; i < nconss; ++i )
   {
      TCLIQUE_CALL( tcliqueAddNode(*graph, i, 0) );
   }

   useprobvars = ( SCIPgetStage(scip) >= SCIP_STAGE_TRANSFORMED );

   /* Be aware: the following has n*n*m*log(m) complexity but doesn't need any additional memory
      With additional memory, we can get it down to probably n*m + m*m*n  */
   for( i = 0; i < nconss; ++i )
   {
      SCIP_VAR** curvars1;
      int ncurvars1;

      ncurvars1 = GCGconsGetNVars(scip, conss[i]);
      SCIP_CALL( SCIPallocBufferArray(scip, &curvars1, ncurvars1) );

      SCIP_CALL( GCGconsGetVars(scip, conss[i], curvars1, ncurvars1) );

      if( useprobvars )
      {
         /* replace all variables by probvars */
         for( v = 0; v < ncurvars1; ++v )
         {
            curvars1[v] = SCIPvarGetProbvar(curvars1[v]);
            assert( SCIPvarIsActive(curvars1[v]) );
         }
      }

      SCIPsortPtr((void**)curvars1, cmp, ncurvars1);

      for( j = i+1; j < nconss; ++j )
      {
         SCIP_VAR** curvars2;
         int ncurvars2;

         ncurvars2 = GCGconsGetNVars(scip, conss[j]);
         SCIP_CALL( SCIPallocBufferArray(scip, &curvars2, ncurvars2) );

         SCIP_CALL( GCGconsGetVars(scip, conss[j], curvars2, ncurvars2) );

         for( v = 0; v < ncurvars2; ++v )
         {
            int pos;

            if( useprobvars )
            {
               curvars2[v] = SCIPvarGetProbvar(curvars2[v]);
               assert( SCIPvarIsActive(curvars2[v]) );
            }

            if( SCIPsortedvecFindPtr((void*)curvars1, cmp, curvars2[v], ncurvars1, &pos) )
            {
               assert( curvars1[pos] == curvars2[v] );
               TCLIQUE_CALL( tcliqueAddEdge(*graph, i, j) );
               break;
            }
         }
         SCIPfreeBufferArray(scip, &curvars2);
      }

      SCIPfreeBufferArray(scip, &curvars1);
   }

   TCLIQUE_CALL( tcliqueFlush(*graph) );
   /*SCIPdebug(tcliquePrintGraph(*graph));*/

   return SCIP_OKAY;
}


/** returns the distance between vertex i and j based on the distance matrix */
static
int getDistance(
   int                   i,                  /**< vertex i */
   int                   j,                  /**< vertex j */
   int**                 distance            /**< triangular distance matrix */
   )
{
   assert(distance != NULL);

   if( i >= j )
      return distance[i][j];
   else if (i < j)
      return distance[j][i];
   else
      return 0;
}

/** finds the diameter of the graph and computes all distances from some vertex of maximum eccentricity to all other vertices */
static
SCIP_RETCODE findDiameter(
   SCIP *scip,
   DEC_DETECTORDATA*     detectordata,       /**< constraint handler data structure */
   int*                  maxdistance,        /**< diameter of the graph */
   int*                  ncomp,              /**< number of vertices the component contains */
   int*                  vertices,           /**< */
   int*                  distances,          /**< distances of vertices to some vertex of maximum eccentricity */
   int                   component
)
{
   TCLIQUE_GRAPH* graph;
   int diameter = -1;
   int* queue;          /* queue, first entry is queue[squeue], last entry is queue[equeue] */
   int squeue;          /* index of first entry of the queue */
   int equeue;          /* index of last entry of the queue */
   int nnodes;          /* number of vertices the graph contains */
   int ncompnodes = 0;  /* number of vertices the component contains */
   SCIP_Bool* marked;
   int* eccentricity;   /* upper bounds on the eccentricities */
   int* dist;           /* distances from some start vertex to all other vertices (gets updated in each BFS) */
   int* origdegree;     /* degrees of the vertices */
   int* degree;         /* degrees of the vertices sorted in decreasing order */
   int* degreepos;
   int i;
   int j;
   int k = 50;          /* number of low-degree vertices that are visited before high-degree vertices are visited */

   assert(scip != NULL);
   assert(detectordata != NULL);
   assert(detectordata->graph != NULL);
   assert(detectordata->components != NULL);
   assert(maxdistance != NULL);
   assert(vertices != NULL);
   assert(distances != NULL);
   assert(ncomp != NULL);

   graph = detectordata->graph;
   nnodes = tcliqueGetNNodes(graph);

   SCIP_CALL( SCIPallocMemoryArray(scip, &queue, nnodes) );
   SCIP_CALL( SCIPallocMemoryArray(scip, &marked, nnodes) );
   SCIP_CALL( SCIPallocMemoryArray(scip, &eccentricity, nnodes) );
   SCIP_CALL( SCIPallocMemoryArray(scip, &dist, nnodes) );
   SCIP_CALL( SCIPallocMemoryArray(scip, &degree, nnodes) );
   SCIP_CALL( SCIPallocMemoryArray(scip, &degreepos, nnodes) );

   /* get degrees of vertices and initialize all eccentricities of vertices to values representing upper bounds */
   origdegree = tcliqueGetDegrees(graph);
   for( i = 0; i < nnodes; ++i )
   {
      if( detectordata->components[i] != component )
         continue;

      eccentricity[i] = 2 * nnodes; /* do not use INT_MAX because it could lead to an overflow when adding values */
      degree[ncompnodes] = origdegree[i];    /* we copy the degree array because we are going to sort it */
      degreepos[ncompnodes] = i;

      ++ncompnodes;
   }

   /* sort vertices by their degrees in decreasing order */
   SCIPsortDownIntInt(degree, degreepos, ncompnodes);

   if( ncompnodes < k )
      k = ncompnodes;

   /* for each vertex a BFS will be performed */
   for( j = 0; j < ncompnodes; j++ )
   {
      int eccent = 0; /* eccentricity of this vertex, only valid if vertex has not been pruned */
      int startnode;
      SCIP_Bool pruned = FALSE;

      /* change order in which BFSes are performed: first start at 'k' low-degree vertices, then start BFS at high-degree vertices */
      if(j < k)
         startnode = degreepos[ncompnodes - k + j];
      else
         startnode = degreepos[j - k];

      /* eccentricity[startnode] always represents an UPPER BOUND on the actual eccentricity! */
      if( eccentricity[startnode] <= diameter )
         continue;

      /* unmark all vertices */
      BMSclearMemoryArray(marked, nnodes);

      /* add 'startnode' to the queue */
      queue[0] = startnode;
      equeue = 1;
      squeue = 0;
      marked[startnode] = TRUE;
      dist[startnode] = 0;

      /* continue BFS until vertex gets pruned or all vertices have been visited */
      while( !pruned && (equeue > squeue) )
      {
         int currentnode;
         int currentdistance;
         int* lastneighbour;
         int* node;

         /* dequeue new node */
         currentnode = queue[squeue];
         currentdistance = dist[currentnode];
         ++squeue;

         lastneighbour = tcliqueGetLastAdjedge(graph, currentnode);
         /* go through all neighbours */
         for( node = tcliqueGetFirstAdjedge(graph, currentnode); !pruned && (node <= lastneighbour); ++node )
         {
            const int v = *node;
            /* visit 'v' if it has not been visited yet */
            if( !marked[v] )
            {
               /* mark 'v' and add it to the queue */
               marked[v] = TRUE;
               queue[equeue] = v;
               dist[v] = currentdistance + 1;
               ++equeue;

               /* if 'v' is further away from the startnode than any other vertex, update the eccentricity */
               if( dist[v] > eccent )
                  eccent = dist[v];

               /* prune the startnode if its eccentricity will certainly not lead to a new upper bound */
               if( eccentricity[v] + dist[v] <= diameter )
               {
                  pruned = TRUE;
                  eccent = eccentricity[v] + dist[v];
               }

               /* update upper bound on eccentricity of 'v' */
               /*if( eccentricity[currentnode] + dist[v] < eccentricity[v] )
                  eccentricity[v] = eccentricity[currentnode] + dist[v];*/
            }
         }
      }

      eccentricity[startnode] = eccent;

      if( eccent > diameter )
      {
         SCIPdebugMessage("new incumbent in component %i: path of length %i starts at %i\n", component, eccent, startnode);
         diameter = eccent;

         *maxdistance = diameter;
         *ncomp = ncompnodes;
         /*detectordata->nblocks = diameter + 1;*/

         for( i = 0; i < ncompnodes; ++i )
         {
            vertices[i] = queue[i];
            distances[i] = dist[queue[i]];
         }
      }
   }

   SCIPfreeMemoryArray(scip, &queue);
   SCIPfreeMemoryArray(scip, &marked);
   SCIPfreeMemoryArray(scip, &eccentricity);
   SCIPfreeMemoryArray(scip, &dist);
   SCIPfreeMemoryArray(scip, &degree);
   SCIPfreeMemoryArray(scip, &degreepos);

   return SCIP_OKAY;
}

/** perform BFS on the graph, storing distance information in the user supplied array */
static
SCIP_RETCODE doBFS(
   SCIP*                 scip,               /**< SCIP data structure */
   DEC_DETECTORDATA*     detectordata,       /**< constraint handler data structure */
   int                   startnode,          /**< starting node */
   int**                 distances           /**< triangular matrix to store the distance when starting from node i */
   )
{
   int *queue;
   int nnodes;
   SCIP_Bool* marked;
   int squeue;
   int equeue;
   int i;
   int* node;

   TCLIQUE_GRAPH* graph;

   assert(scip != NULL);
   assert(detectordata != NULL);
   assert(distances != NULL);
   assert(detectordata->graph != NULL);
   graph = detectordata->graph;
   nnodes = tcliqueGetNNodes(graph);

   assert(startnode < tcliqueGetNNodes(graph));

   squeue = 0;
   equeue = 0;

   SCIP_CALL( SCIPallocMemoryArray(scip, &queue, nnodes) );
   SCIP_CALL( SCIPallocMemoryArray(scip, &marked, nnodes) );

   for( i = 0; i < nnodes; ++i )
   {
      marked[i] = FALSE;
   }

   queue[equeue] = startnode;
   ++equeue;

   distances[startnode][startnode] = 0;
   marked[startnode] = TRUE;

   while( equeue > squeue )
   {
      int currentnode;
      int* lastneighbour;

      /* dequeue new node */
      currentnode = queue[squeue];

      assert(currentnode < nnodes);
      ++squeue;

      lastneighbour = tcliqueGetLastAdjedge(graph, currentnode);
      /* go through all neighbours */
      for( node = tcliqueGetFirstAdjedge(graph, currentnode); node <= lastneighbour; ++node )
      {
         if( !marked[*node] )
         {
            int curdistance;

            curdistance = getDistance(startnode, currentnode, distances);

            marked[*node] = TRUE;
            queue[equeue] = *node;
            if( *node < startnode )
               distances[startnode][*node] = curdistance+1;
            else if( *node > startnode )
               distances[*node][startnode] = curdistance+1;

            ++equeue;
         }
      }
   }

   SCIPfreeMemoryArray(scip, &queue);
   SCIPfreeMemoryArray(scip, &marked);

   return SCIP_OKAY;
}

/** finds the maximal shortest path by inspecting the distance array and returns the path in start and end*/
static
SCIP_RETCODE findMaximalPath(
   SCIP*                 scip,               /**< SCIP data structure */
   DEC_DETECTORDATA*     detectordata,       /**< constraint handler data structure */
   int**                 distance,           /**< distance matrix of the maximal s-t path starting from s to any node t*/
   int*                  start,              /**< start vertex */
   int*                  end                 /**< end vertex */
   )
{
   int i;
   int j;
   int max;

   assert(scip != NULL);
   assert(detectordata != NULL);
   assert(distance != NULL);
   assert(start != NULL);
   assert(end != NULL);

   max = -1;
   *start = -1;
   *end = -1;

   for( i = 0; i < tcliqueGetNNodes(detectordata->graph); ++i )
   {
      for( j = 0; j < i; ++j )
      {
         if( distance[i][j] > max )
         {
            max = distance[i][j];
            *start = i;
            *end = j;
         }
      }
   }
   assert(*start >= 0);
   assert(*end >= 0);

   SCIPdebugMessage("Path from %d to %d is longest %d.\n", *start, *end, max);
   detectordata->nblocks = max+1;

   return SCIP_OKAY;
}

/** this method will construct the cuts based on the longest shortest path and the distance matrix */
static
SCIP_RETCODE constructCuts(
   SCIP*                 scip,               /**< SCIP data structure */
   DEC_DETECTORDATA*     detectordata,       /**< constraint handler data structure */
   int                   start,              /**< start vertex */
   int                   end,                /**< end vertex */
   int**                 distance,           /**< distance matrix giving the distance from any constraint to any constraint */
   SCIP_VAR****          cuts                /**< which variables should be in the cuts */
   )
{

   int nnodes;
   SCIP_CONS** conss;
   int i;

   assert(scip != NULL);
   assert(detectordata != NULL);
   assert(start >= 0);
   assert(end >= 0);
   assert(distance != NULL);
   assert(cuts != NULL);

   assert(detectordata->graph != NULL);
   nnodes = tcliqueGetNNodes(detectordata->graph);
   conss = SCIPgetConss(scip);
   assert(start < nnodes);
   assert(end < nnodes);

   /* The cuts will be generated on a trivial basis:
    * The vertices  of distance i will be in block i
    */

   for( i = 0; i < nnodes; ++i )
   {
      int dist;
      dist = getDistance(start, i, distance);
      SCIPdebugPrintf("from %d to %d = %d (%s = %d)\n", start, i, dist, SCIPconsGetName(conss[i]), dist+1 );
      SCIP_CALL( SCIPhashmapInsert(detectordata->constoblock, conss[i], (void*) (size_t) (dist+1)) );
   }

   return SCIP_OKAY;
}

/** finds connected components of the graph */
static
SCIP_RETCODE findConnectedComponents(
   SCIP*                 scip,               /** SCIP data structure */
   DEC_DETECTORDATA*     detectordata        /** constraint handler data structure */
   )
{
   int i;
   int nnodes;
   int ncomps = 0;
   int curcomp;
   int* queue;
   int squeue;
   int equeue;
   TCLIQUE_GRAPH* graph;
   int* component;

   assert(scip != NULL);
   assert(detectordata != NULL);
   assert(detectordata->graph != NULL);
   assert(tcliqueGetNNodes(detectordata->graph) >= 0);

   graph = detectordata->graph;
   nnodes = tcliqueGetNNodes(graph);

   assert(detectordata->components == NULL);
   SCIP_CALL( SCIPallocBufferArray(scip, &(detectordata->components), nnodes) );
   component = detectordata->components;

   for( i = 0; i < nnodes; ++i )
      component[i] = -1;

   SCIP_CALL( SCIPallocMemoryArray(scip, &queue, nnodes) );

   for( i = 0; i < nnodes; ++i )
   {
      /* find first node that has not been visited yet */
      if( component[i] >= 0 )
         continue;

      SCIPdebugMessage("found new component; starting at %i\n", i);
      squeue = 0;
      equeue = 1;
      queue[0] = i;
      curcomp = ncomps++;

      while( equeue > squeue )
      {
         int curnode;
         int* lastneighbour;
         int* node;

         curnode = queue[squeue++];

         assert(curnode < nnodes);

         lastneighbour = tcliqueGetLastAdjedge(graph, curnode);
         for( node = tcliqueGetFirstAdjedge(graph, curnode); node <= lastneighbour; ++node )
         {
            assert(*node < nnodes);

            if( component[*node] == -1 )
            {
               component[*node] = curcomp;
               queue[equeue++] = *node;
            }
         }
      }
   }

   detectordata->ncomponents = ncomps;
   SCIPdebugMessage("found %i components\n", ncomps);

   SCIPfreeMemoryArray(scip, &queue);
   return SCIP_OKAY;
}

/** looks for staircase components in the constraints in detectordata */
static
SCIP_RETCODE findStaircaseComponents(
   SCIP*                 scip,               /**< SCIP data structure */
   DEC_DETECTORDATA*     detectordata,       /**< constraint handler data structure */
   SCIP_RESULT*          result              /**< result pointer to indicate success or failure */
   )
{
   int nconss;
   int** distance;
   int i;
   int start;
   int end;
   SCIP_VAR*** cuts;

   assert(scip != NULL);
   assert(detectordata != NULL);
   assert(result != NULL);

   nconss = SCIPgetNConss(scip);

   /* allocate triangular distance matrix */
   SCIP_CALL( SCIPallocMemoryArray(scip, &distance, nconss) );
   for( i = 0; i < nconss; ++i )
   {
      SCIP_CALL( SCIPallocMemoryArray(scip, &(distance[i]), (size_t)i+1) ); /*lint !e866*/
      BMSclearMemoryArray(distance[i], (size_t)i+1); /*lint !e866*/
   }

   for( i = 0; i < nconss; ++i )
   {
      SCIP_CALL( doBFS(scip, detectordata, i, distance) );
   }

   SCIP_CALL( findMaximalPath(scip, detectordata, distance, &start, &end) );
   SCIP_CALL( constructCuts(scip, detectordata, start, end, distance, &cuts) );

   for( i = 0; i < nconss; ++i )
   {
      SCIPfreeMemoryArray(scip, &distance[i]);
   }
   SCIPfreeMemoryArray(scip, &distance);

   if( detectordata->nblocks > 1 )
      *result = SCIP_SUCCESS;
   else
      *result = SCIP_DIDNOTFIND;

   return SCIP_OKAY;
}


/* copy conshdldata data to decdecomp */
static
SCIP_RETCODE copyToDecdecomp(
   SCIP*              scip,                  /**< SCIP data structure */
   DEC_DETECTORDATA*  detectordata,          /**< constraint handler data structure */
   DEC_DECOMP*        decdecomp              /**< decdecomp data structure */
   )
{

   assert(scip != NULL);
   assert(detectordata != NULL);
   assert(decdecomp != NULL);

   SCIP_CALL( DECfilloutDecompFromConstoblock(scip, decdecomp, detectordata->constoblock, detectordata->nblocks, TRUE) );

   return SCIP_OKAY;
}

/** solving process initialization method of constraint handler (called when branch and bound process is about to begin) */
static
DEC_DECL_INITDETECTOR(initStaircase)
{  /*lint --e{715}*/

   DEC_DETECTORDATA *detectordata;

   assert(scip != NULL);
   assert(detector != NULL);

   assert(strcmp(DECdetectorGetName(detector), DEC_DETECTORNAME) == 0);

   detectordata = DECdetectorGetData(detector);
   assert(detectordata != NULL);

   detectordata->constoblock = NULL;
   detectordata->vartoblock = NULL;

   detectordata->nblocks = 0;
   SCIP_CALL( SCIPhashmapCreate(&detectordata->constoblock, SCIPblkmem(scip), SCIPgetNConss(scip)) );
   return SCIP_OKAY;
}

/** destructor of constraint handler to free constraint handler data (called when SCIP is exiting) */
static
DEC_DECL_EXITDETECTOR(exitStaircase)
{  /*lint --e{715}*/
   DEC_DETECTORDATA *detectordata;

   assert(scip != NULL);
   assert(detector != NULL);

   assert(strcmp(DECdetectorGetName(detector), DEC_DETECTORNAME) == 0);

   detectordata = DECdetectorGetData(detector);
   assert(detectordata != NULL);

   if( detectordata->graph != NULL )
   {
      tcliqueFree(&detectordata->graph);
   }

   if( detectordata->components != NULL )
   {
      SCIPfreeBufferArray(scip, &detectordata->components);
   }

   SCIPfreeMemory(scip, &detectordata);

   return SCIP_OKAY;
}

static
DEC_DECL_DETECTSTRUCTURE(detectStaircase)
{
   int i;
   int j;
   int* nodes;
   int nnodes;
   int* distances;
   int* blocks;
   int nblocks = 0;

   *result = SCIP_DIDNOTFIND;

   SCIPverbMessage(scip, SCIP_VERBLEVEL_NORMAL, NULL, "Detecting staircase structure:");

   SCIP_CALL( createGraph(scip, &(detectordata->graph)) );

   if( tcliqueGetNNodes(detectordata->graph) > 0 )
   {
      nnodes = tcliqueGetNNodes(detectordata->graph);

      /* find connected components of the graph */
      SCIP_CALL( findConnectedComponents(scip, detectordata) );

      SCIP_CALL( SCIPallocBufferArray(scip, &nodes, nnodes) );
      SCIP_CALL( SCIPallocBufferArray(scip, &distances, nnodes) );
      SCIP_CALL( SCIPallocBufferArray(scip, &blocks, nnodes) );

      for( i = 0; i < nnodes; ++i)
         blocks[i] = -1;

      /* find the diameter for each component */
      for( i = 0; i < detectordata->ncomponents; ++i )
      {
         int diameter = 0;
         int ncompsize = 0;

         SCIP_CALL( findDiameter(scip, detectordata, &diameter, &ncompsize, nodes, distances, i) );
         SCIPdebugMessage("component %i has %i vertices and diameter %i\n", i, ncompsize, diameter);

         for( j = 0; j < ncompsize; j++ )
         {
            assert(nodes[j] >= 0);
            assert(nodes[j] < nnodes);
            assert(distances[j] >= 0);
            assert(distances[j] <= diameter);
            assert(distances[j] + nblocks < nnodes);

            blocks[nodes[j]] = nblocks + distances[j];
            SCIPdebugMessage("\tnode %i to block %i\n", nodes[j], nblocks + distances[j]);
         }

         nblocks += (diameter + 1);
      }

      if( nblocks > 0 )
      {
         SCIP_CONS** conss = SCIPgetConss(scip);

         detectordata->nblocks = nblocks;

         for( i = 0; i < nnodes; ++i )
         {
            assert(blocks[i] >= 0);
            SCIP_CALL( SCIPhashmapInsert(detectordata->constoblock, conss[i], (void*) (size_t) (blocks[i] + 1)) );
         }

         SCIPverbMessage(scip, SCIP_VERBLEVEL_NORMAL, NULL, " found %d blocks.\n", detectordata->nblocks);
         SCIP_CALL( SCIPallocMemoryArray(scip, decdecomps, 1) ); /*lint !e506*/
         SCIP_CALL( DECdecompCreate(scip, &((*decdecomps)[0])) );
         SCIP_CALL( copyToDecdecomp(scip, detectordata, (*decdecomps)[0]) );
         *ndecdecomps = 1;
         *result = SCIP_SUCCESS;
      }

      SCIPfreeBufferArray(scip, &blocks);
      SCIPfreeBufferArray(scip, &nodes);
      SCIPfreeBufferArray(scip, &distances);
      SCIPfreeBufferArray(scip, &(detectordata->components));
   }

   if( *result != SCIP_SUCCESS )
   {
      SCIPverbMessage(scip, SCIP_VERBLEVEL_NORMAL, NULL, " not found.\n");
      if( detectordata->constoblock != NULL )
         SCIPhashmapFree(&detectordata->constoblock);
      if( detectordata->vartoblock != NULL )
         SCIPhashmapFree(&detectordata->vartoblock);
   }

   return SCIP_OKAY;
}


/*
 * constraint specific interface methods
 */

/** creates the handler for staircase constraints and includes it in SCIP */
SCIP_RETCODE SCIPincludeDetectionStaircase(
   SCIP*                 scip                /**< SCIP data structure */
   )
{
   DEC_DETECTORDATA* detectordata;

   /* create staircase constraint handler data */
   detectordata = NULL;

   SCIP_CALL( SCIPallocMemory(scip, &detectordata) );
   assert(detectordata != NULL);
   detectordata->graph = NULL;
   detectordata->components = NULL;
   detectordata->ncomponents = 0;
   detectordata->constoblock = NULL;
   detectordata->vartoblock = NULL;
   detectordata->nblocks = 0;
   SCIP_CALL( DECincludeDetector(scip, DEC_DETECTORNAME, DEC_DECCHAR, DEC_DESC, DEC_PRIORITY, DEC_ENABLED, DEC_SKIP, detectordata, detectStaircase, initStaircase, exitStaircase) );

   return SCIP_OKAY;
}
