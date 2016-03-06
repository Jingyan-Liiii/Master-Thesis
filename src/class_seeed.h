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

/**@file   class_Seeed.h
 * @brief  class with functions for seeed
 * @author Michael Bastubbe
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#ifndef GCG_CLASS_SEEED_H__
#define GCG_CLASS_SEEED_H__


#include "objscip/objscip.h"
#include <vector>



namespace gcg {



class Seeed
{

private:
	SCIP*							scip;
   int								id;						/**< id of the seeed */
   int 								nBlocks;				/**< number of blocks the decomposition currently has */
   int 								nVars;
   int								nConss;
   std::vector<int>					masterConss;			/**< vector containing indices of master constraints */
   std::vector<int>					masterVars;				/**< vector containing indices of master variables */
   std::vector<std::vector<int>> 	conssForBlocks; 		/**< conssForBlocks[k] contains a vector of indices of all constraints assigned to block k */
   std::vector<std::vector<int>> 	varsForBlocks; 			/**< varsForBlocks[k] contains a vector of indices of all variables assigned to block k */
   std::vector<int> 				linkingVars;			/**< vector containing indices of linking variables */
   std::vector<int> 				stairlinkingVars;		/**< vector containing indices of staircase linking variables */
   std::vector<int> 				openVars;				/**< vector containing indices of  variables that are not assigned yet*/
   std::vector<int> 				openConss;				/**< vector containing indices of  constraints that are not assigned yet*/
   std::vector<bool> 				propagatedByDetector;	/**< propagatedByDetector[i] is this seeed propagated by detector i */
   std::vector<int> 				detectorChain;
   bool 							openVarsAndConssCalculated; /** are the


public:

   /** constructor(s) */
   Seeed(
	  int               id,      		   	/**< id that is given to this seeed */
	  int               nDetectors,         /**< number of detectors */
	  int				nConss,				/**number of constraints */
	  int 				nVars				/**number of variables */
      );

   ~Seeed();

   /**check-methods */

   /** check the consistency of this seeed */
   bool checkConsistency(
   );

   /** set-methods */

   /** set number of blocks, atm only increasing number of blocks  */
      SCIP_RETCODE setNBlocks(
	   int nBlocks
      );

   /** add a constraint to the master constraints */
   SCIP_RETCODE setConsToMaster(
		   int consToMaster
   );

   /** add a variable to the master variables (every constraint consisting it is in master ) */
   SCIP_RETCODE setVarToMaster(
		   int varToMaster
   );

   /** add a constraint to a block */
   SCIP_RETCODE setConsToBlock(
		   int consToBlock,
		   int block
   );

   /** add a variable to a block */
   SCIP_RETCODE setVarToBlock(
		   int varToBlock,
		   int block
   );

   /** add a variable to the linking variables */
   SCIP_RETCODE setVarToLinking(
		   int varToLinking
   );

   /** add a variable to the stair linking variables */
   SCIP_RETCODE setVarToStairlinking(
		   int varToStairLinking
   );

   SCIP_RETCODE setDetectorPropagated(
   		   int detectorID
     );


   /** get-methods */

   /** returns array containing master conss */
   const int* getMasterconss(
   );

   /** returns size of array containing master conss */
   int getNMasterconss(
   );


   /** returns vector containing master vars (every constraint containing a master var is in master )*/
   const int* getMastervars(
   );

   /** returns vector containing master conss */
   const int* getConssForBlock(
		   int block
   );

   /** returns vector containing vars of a certain block */
   const int* getVarsForBlock(
		   int block
   );

   /** returns vector containing linking vars */
   const int* getLinkingvars(
   );

   /** returns vector containing stairlinking vars */
   const int* getStairlinkingvars(
   );

   /** returns vector containing variables not assigned yet */
   const int* getOpenvars(
   );

   /**  returns vector containing constraints not assigned yet */
   const int* getOpenconss(
   );

   /** returns size of vector containing master vars (every constraint containing a master var is in master )*/
   int getNMastervars(
   );

   /** returns size of vector containing master conss */
   int getNConssForBlock(
		   int block
   );

   /** returns size of vector containing vars of a certain block */
   int getNVarsForBlock(
		   int block
   );

   /** returns size ofvector containing linking vars */
   int getNLinkingvars(
   );

   /** returns size ofvector containing stairlinking vars */
   int getNStairlinkingvars(
   );

   void  calcOpenconss();

   void  calcOpenvars();


   /** returns size of vector containing variables not assigned yet */
   int getNOpenvars(
   );

   /** returns size of vector containing constraints not assigned yet */
   int getNOpenconss(
   );

   /** returns whether this seeed was propagated by certain detector */
   bool isPropagatedBy(
		   int detectorID
   );

   SCIP_RETCODE Seeed::completeGreedily();


private:



};

} /* namespace gcg */
#endif /* GCG_CLASS_Seeed_H__ */