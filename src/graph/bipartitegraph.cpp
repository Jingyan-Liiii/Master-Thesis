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

/**@file   bipartitegraph.cpp
 * @brief  A bipartite graph
 * @author Martin Bergner
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/


#include "bipartitegraph.h"
#include "scip_misc.h"

namespace gcg {


BipartiteGraph::BipartiteGraph(
      SCIP*                 scip,              /**< SCIP data structure */
      Weights               w                 /**< weights for the given graph */
   ): Graph(scip, w)
{
   // TODO Auto-generated constructor stub
   name = std::string("bipartite");
}

BipartiteGraph::~BipartiteGraph()
{
   // TODO Auto-generated destructor stub
}


/**
 * Builds a bipartite graph structure out of the matrix.
 *
 * The function will create an node for every constraint and every variable.
 * A constraint and a variable are adjacent if the variable appears in the constraint variable array.
 *
 * @todo The nonzeroness is not checked, all variables in the variable array are considered
 */
SCIP_RETCODE BipartiteGraph::createFromMatrix(
   SCIP_CONS**           conss,              /**< constraints for which graph should be created */
   SCIP_VAR**            vars,               /**< variables for which graph should be created */
   int                   nconss_,             /**< number of constraints */
   int                   nvars_               /**< number of variables */
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

   for( i = 0; i < nvars + nconss; ++i )
   {
      TCLIQUE_WEIGHT weight;

      /* note that the first nvars nodes correspond to variables */
      if( i < nvars )
         weight = weights.calculate(vars[i]);
      else
         weight = weights.calculate(conss[i-nvars]);

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

         TCLIQUE_CALL( tcliqueAddEdge(tgraph, varIndex, nvars+i) );
      }
      SCIPfreeBufferArray(scip_, &curvars);
   }

   TCLIQUE_CALL( tcliqueFlush(tgraph) );

   nnonzeroes = tcliqueGetNEdges(tgraph);

   return SCIP_OKAY;
}

} /* namespace gcg */
