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

/**@file   hyperrowcolgraph.cpp
 * @brief  Description
 * @author bergner
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#include "hyperrowcolgraph.h"
#include "scip_misc.h"
#include <fstream>
#include <algorithm>
#include <set>

using std::ifstream;

namespace gcg {

HyperrowcolGraph::HyperrowcolGraph(
   SCIP*                 scip,              /**< SCIP data structure */
   Weights               w                  /**< weights for the given graph */
): Graph(scip, w)
{
   name = std::string("hyperrowcol");
}

HyperrowcolGraph::~HyperrowcolGraph()
{

}


/**
 * Builds a bipartite representation of the hyperrowcol graph out of the matrix.
 *
 * The function will create an node for every constraint, every variable and every nonzero entry of the matrix.
 * One side of the bipartite graph are the nonzero entries (nodes), the constraints and variables are on the other side (hyperedges).
 * A nonzero entry a_{ij} is incident to the constraint i and the variable j.
 *
 * @todo The nonzeroness is not checked, all variables in the variable array are considered
 */
SCIP_RETCODE HyperrowcolGraph::createFromMatrix(
   SCIP_CONS**           conss,              /**< constraints for which graph should be created */
   SCIP_VAR**            vars,               /**< variables for which graph should be created */
   int                   nconss_,            /**< number of constraints */
   int                   nvars_              /**< number of variables */
   )
{
   int i;
   int j;
   SCIP_Bool success;

   assert(conss != NULL);
   assert(vars != NULL);
   assert(nvars_ > 0);
   assert(nconss_ > 0);

   nvars = nvars_;
   nconss = nconss_;

   /* create nodes for constraints and variables (hyperedges) */
   for( i = 0; i < nvars + nconss; ++i )
   {
      TCLIQUE_WEIGHT weight;

      /* note that the first nvars nodes correspond to variables */
      if( i < nvars )
      {
         weight = weights.calculate(vars[i]);
         SCIPdebugMessage("Weight for var <%s> is %d\n", SCIPvarGetName(vars[i]), weight);
      }

      else
      {
         weight = weights.calculate(conss[i-nvars]);
         SCIPdebugMessage("Weight for cons <%s> is %d\n", SCIPconsGetName(conss[i-nvars]), weight);
      }

      TCLIQUE_CALL( tcliqueAddNode(tgraph, i, weight) );
   }

   /* go through all constraints */
   for( i = 0; i < nconss; ++i )
   {
      SCIP_VAR **curvars;

      int ncurvars;
      SCIP_CALL( SCIPgetConsNVars(scip_, conss[i], &ncurvars, &success) );
      assert(success);
      if( ncurvars == 0 )
         continue;

      /*
       * may work as is, as we are copying the constraint later regardless
       * if there are variables in it or not
       */
      SCIP_CALL( SCIPallocBufferArray(scip_, &curvars, ncurvars) );
      SCIP_CALL( SCIPgetConsVars(scip_, conss[i], curvars, ncurvars, &success) );
      assert(success);

      /** @todo skip all variables that have a zero coeffient or where all coefficients add to zero */
      /** @todo Do more then one entry per variable actually work? */

      for( j = 0; j < ncurvars; ++j )
      {
         SCIP_VAR* var;
         int varIndex;

         if( !SCIPisVarRelevant(curvars[j]) )
            continue;

         if( SCIPgetStage(scip_) >= SCIP_STAGE_TRANSFORMED)
            var = SCIPvarGetProbvar(curvars[j]);
         else
            var = curvars[j];

         assert(var != NULL);
         varIndex = SCIPvarGetProbindex(var);
         assert(varIndex >= 0);
         assert(varIndex < nvars);

         SCIPdebugMessage("Cons <%s> (%d), var <%s> (%d), nonzero %d\n", SCIPconsGetName(conss[i]), i, SCIPvarGetName(var), varIndex, nnonzeroes);
         /* add nonzero node and edge to variable and constraint) */;
         TCLIQUE_CALL( tcliqueAddNode(tgraph, nvars+nconss+nnonzeroes, 0) );
         TCLIQUE_CALL( tcliqueAddEdge(tgraph, varIndex, nvars+nconss+nnonzeroes) );
         TCLIQUE_CALL( tcliqueAddEdge(tgraph, nvars+i, nvars+nconss+nnonzeroes) );

         nnonzeroes++;
      }
      SCIPfreeBufferArray(scip_, &curvars);
   }

   TCLIQUE_CALL( tcliqueFlush(tgraph) );

   return SCIP_OKAY;
}

/** writes the graph to the given file.
 *  The format is graph dependent
 */
SCIP_RETCODE HyperrowcolGraph::writeToFile(
   const char*        filename,           /**< filename where the graph should be written to */
   SCIP_Bool          edgeweights = FALSE /**< whether to write edgeweights */
 )
{
   FILE* file;
   assert(filename != NULL);
   file = fopen(filename, "wx");
   if( file == NULL )
      return SCIP_FILECREATEERROR;

   SCIPinfoMessage(scip_, file, "%d %d %d\n", nvars+nconss, nnonzeroes+dummynodes, edgeweights ? 1 :0);

   for( int i = 0; i < nvars+nconss; ++i )
   {
      std::vector<int> neighbors = Graph::getNeighbors(i);
      int nneighbors = Graph::getNNeighbors(i);
      if( edgeweights )
      {
         SCIPinfoMessage(scip_, file, "%d ", Graph::getWeight(i));
      }
      for( int j = 0; j < nneighbors; ++j )
      {
         SCIPinfoMessage(scip_, file, "%d ",neighbors[j]+1-nvars-nconss);
      }
      SCIPinfoMessage(scip_, file, "\n");
   }

   if( !fclose(file) )
      return SCIP_OKAY;
   else
      return SCIP_WRITEERROR;
}


SCIP_RETCODE HyperrowcolGraph::readPartition(
   const char* filename
)
{
   ifstream input(filename);
   if( !input.good() )
   {
      SCIPerrorMessage("Could not open file <%s> for reading\n", filename);
      return SCIP_READERROR;
   }
   partition.resize(nnonzeroes);
   for( int i = 0; i < nnonzeroes; ++i )
   {
      int part = 0;
      if( !(input >> part) )
      {
         SCIPerrorMessage("Could not read from file <%s>. It may be in the wrong format\n", filename);
         return SCIP_READERROR;
      }
      partition[i] = part;
   }

   input.close();
   return SCIP_OKAY;
}

int HyperrowcolGraph::getNEdges()
{
   return nconss+nvars;
}


int HyperrowcolGraph::getNNodes()
{
   return nnonzeroes;
}

class function {
   int diff;
public:
   function(int i):diff(i) {}
   int operator()(int i) { return i-diff;}
};


std::vector<int> HyperrowcolGraph::getNeighbors(
   int i
)
{
   assert(i >= 0);
   assert(i < nnonzeroes);
   function f(nconss+nvars);
   std::vector<int>::iterator it;
   std::set<int> neighbors;
   std::vector<int> immediateneighbors = Graph::getNeighbors(i+nconss+nvars);
   for( size_t j = 0; j < immediateneighbors.size(); ++j)
   {
      std::vector<int> alternateneighbor = Graph::getNeighbors(immediateneighbors[j]);
      neighbors.insert(alternateneighbor.begin(), alternateneighbor.end() );
   }
   std::vector<int> r(neighbors.size(), 0);
   std::transform(neighbors.begin(), neighbors.end(), r.begin(), f);
   it = std::remove(r.begin(), r.end(), i);

   return std::vector<int>(r.begin(), it);
}

std::vector<int> HyperrowcolGraph::getHyperedgeNodes(
   int i
)
{
   function f(nconss+nvars);
   assert(i >= 0);
   assert(i < nconss+nvars);

   std::vector<int> neighbors = Graph::getNeighbors(i);
   std::transform(neighbors.begin(), neighbors.end(), neighbors.begin(), f);
   return neighbors;
}

std::vector<int> HyperrowcolGraph::getConsNonzeroNodes(
   int i
)
{
   function f(nconss+nvars);
   assert(i >= 0);
   assert(i < nconss);

   std::vector<int> neighbors = Graph::getNeighbors(i+nvars);
   std::transform(neighbors.begin(), neighbors.end(), neighbors.begin(), f);
   return neighbors;
}

std::vector<int> HyperrowcolGraph::getVarNonzeroNodes(
   int i
)
{
   function f(nconss+nvars);
   assert(i >= 0);
   assert(i < nvars);

   std::vector<int> neighbors = Graph::getNeighbors(i);
   std::transform(neighbors.begin(), neighbors.end(), neighbors.begin(), f);
   return neighbors;
}

SCIP_RETCODE HyperrowcolGraph::createDecompFromPartition(
   DEC_DECOMP**       decomp              /**< decomposition structure to generate */
   )
{
   int nblocks;
   SCIP_HASHMAP* constoblock;

   int *nsubscipconss;
   int i;
   SCIP_CONS **conss;
   SCIP_VAR **vars;
   SCIP_Bool emptyblocks = FALSE;

   conss = SCIPgetConss(scip_);
   vars = SCIPgetVars(scip_);

   nblocks = *(std::max_element(partition.begin(), partition.end()))+1;
   SCIP_CALL( SCIPallocBufferArray(scip_, &nsubscipconss, nblocks) );
   BMSclearMemoryArray(nsubscipconss, nblocks);

   SCIP_CALL( SCIPhashmapCreate(&constoblock, SCIPblkmem(scip_), nconss) );

   /* assign constraints to partition */
   for( i = 0; i < nconss; i++ )
   {
      std::set<int> blocks;
      std::vector<int> nonzeros = getConsNonzeroNodes(i);
      for( size_t k = 0; k < nonzeros.size(); ++k )
      {
         blocks.insert(partition[nonzeros[k]]);
      }
      if( blocks.size() > 1 )
      {
         SCIP_CALL( SCIPhashmapInsert(constoblock, conss[i], (void*) (size_t) (nblocks+1)) );
      }
      else
      {
         int block = *(blocks.begin());
         SCIP_CALL( SCIPhashmapInsert(constoblock, conss[i], (void*) (size_t) (block +1)) );
         ++(nsubscipconss[block]);
      }
   }

   /* first, make sure that there are constraints in every block, otherwise the hole thing is useless */
   for( i = 0; i < nblocks; ++i )
   {
      if( nsubscipconss[i] == 0 )
      {
         SCIPdebugMessage("Block %d does not have any constraints!\n", i);
         emptyblocks = TRUE;
      }
   }

   if( !emptyblocks )
   {
      SCIP_CALL( DECdecompCreate(scip_, decomp) );
      SCIP_CALL( DECfilloutDecdecompFromConstoblock(scip_, *decomp, constoblock, nblocks, vars, nvars, conss, nconss, FALSE) );
   }
   else {
      SCIPhashmapFree(&constoblock);
      *decomp = NULL;
   }

   SCIPfreeBufferArray(scip_, &nsubscipconss);
   return SCIP_OKAY;
}
} /* namespace gcg */