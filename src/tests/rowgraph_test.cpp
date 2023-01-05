/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                           */
/*                  This file is part of the program                         */
/*          GCG --- Generic Column Generation                                */
/*                  a Dantzig-Wolfe decomposition based extension            */
/*                  of the branch-cut-and-price framework                    */
/*         SCIP --- Solving Constraint Integer Programs                      */
/*                                                                           */
/* Copyright (C) 2010-2023 Operations Research, RWTH Aachen University       */
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

/**@file   rowgraph_test.cpp
 * @brief  unit tests for row graph
 * @author Martin Bergner
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#include "graph/rowgraph.h"
#include "test.h"
#include "graphtest.h"

class RowTest : public GraphTest
{

};

TEST_F(RowTest, WriteFileTest) {
   FILE* file = fopen("rowgraph.g", "wx");
   ASSERT_TRUE(file != NULL);
   int fd= fileno(file);
   ASSERT_NE(fd, -1);
   SCIP_CALL_EXPECT( createVar("[integer] <x1>: obj=1.0, original bounds=[0,1]") );
   SCIP_CALL_EXPECT( createVar("[integer] <x2>: obj=1.0, original bounds=[0,3]") );
   SCIP_CALL_EXPECT( createVar("[integer] <x3>: obj=1.0, original bounds=[0,3]") );

   SCIP_CALL_EXPECT( createCons("[linear] <c1>: 1<x1>[I] +1<x2>[I] +1<x3>[I]<= 2") );
   SCIP_CALL_EXPECT( createCons("[linear] <c2>: 2<x1>[I] <= 5") );
   SCIP_CALL_EXPECT( createCons("[linear] <c3>: 1<x3>[I] == 1") );
   SCIP_CALL_EXPECT( createCons("[linear] <c4>: 1<x1>[I] +1<x2>[I] == 1") );
   gcg::Weights weights(1.0, 2, 3, 4, 5, 6);
   gcg::RowGraph<gcg::GraphTclique> graph(scip, weights );

   SCIP_CALL_EXPECT( graph.createFromMatrix(SCIPgetConss(scip), SCIPgetVars(scip), SCIPgetNConss(scip), SCIPgetNVars(scip)) );

   ASSERT_EQ( SCIP_OKAY, graph.writeToFile(fd, FALSE) );
   fclose(file);
   ASSERT_TRUE( SCIPfileExists("rowgraph.g") );

   int tmp[] = {4, 4, 2, 3, 4, 1, 4, 1, 1, 2};

   std::vector<int> array(&tmp[0], &tmp[0]+10);

   if( SCIPfileExists("rowgraph.g") )
   {
      parseFile("rowgraph.g", array);
      remove("rowgraph.g");
   }
}
