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

/**@file   hyperrowcolgraph_test.cpp
 * @brief  Unit tests for row-column hypergraph
 * @author Martin Bergner
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#include "graph/hyperrowcolgraph.h"
#include "test.h"
#include "graphtest.h"

class HyperrowcolTest : public GraphTest {

};


TEST_F(HyperrowcolTest, CreateTest) {
   SCIP_CALL_EXPECT( createVar("[integer] <x1>: obj=1.0, original bounds=[0,1]") );
   SCIP_CALL_EXPECT( createVar("[integer] <x2>: obj=1.0, original bounds=[0,3]") );
   SCIP_CALL_EXPECT( createVar("[implicit] <x3>: obj=1.0, original bounds=[0,1]") );
   SCIP_CALL_EXPECT( createVar("[continous] <x4>: obj=1.0, original bounds=[0,3]") );

   SCIP_CALL_EXPECT( createCons("[linear] <c1>: 1<x1>[I] +1<x2>[I] +1<x4>[I] <= 2") );
   SCIP_CALL_EXPECT( createCons("[linear] <c2>: 2<x1>[I] +2<x2>[I] +3<x3>[I] <= 5") );
   SCIP_CALL_EXPECT( createCons("[linear] <c3>: 1<x1>[I] +1<x3>[I] == 1") );
   gcg::Weights weights(1.0, 2, 3, 4, 5, 6);
   gcg::HyperrowcolGraph graph(scip, weights );

   ASSERT_EQ(SCIP_OKAY, graph.createFromMatrix(SCIPgetConss(scip), SCIPgetVars(scip), SCIPgetNConss(scip), SCIPgetNVars(scip)) );

}

TEST_F(HyperrowcolTest, WriteFileTest) {
   SCIP_CALL_EXPECT( createVar("[integer] <x1>: obj=1.0, original bounds=[0,1]") );
   SCIP_CALL_EXPECT( createVar("[integer] <x2>: obj=1.0, original bounds=[0,3]") );
   SCIP_CALL_EXPECT( createVar("[implicit] <x3>: obj=1.0, original bounds=[0,1]") );
   SCIP_CALL_EXPECT( createVar("[continous] <x4>: obj=1.0, original bounds=[0,3]") );

   SCIP_CALL_EXPECT( createCons("[linear] <c1>: 1<x1>[I] +1<x2>[I] +1<x4>[I] <= 2") );
   SCIP_CALL_EXPECT( createCons("[linear] <c2>: 2<x1>[I] +2<x2>[I] +3<x3>[I] <= 5") );
   SCIP_CALL_EXPECT( createCons("[linear] <c3>: 1<x1>[I] +1<x3>[I] == 1") );
   gcg::Weights weights(1.0, 2, 3, 4, 5, 6);
   gcg::HyperrowcolGraph graph(scip, weights );

   ASSERT_EQ(SCIP_OKAY, graph.createFromMatrix(SCIPgetConss(scip), SCIPgetVars(scip), SCIPgetNConss(scip), SCIPgetNVars(scip)) );
   ASSERT_EQ( SCIP_OKAY, graph.writeToFile("hypergraph.g") );

   ASSERT_EQ( TRUE, SCIPfileExists("hypergraph.g") );

   int tmp[] = {8, 7, 1, 4, 7, 2, 5, 6, 8, 3, 1, 2, 3, 4, 5, 6, 7, 8};

   std::vector<int> array(&tmp[0], &tmp[0]+18);

   if( SCIPfileExists("hypergraph.g") )
   {
      parseFile("hypergraph.g", array);
      unlink("hypergraph.g");
   }

}
