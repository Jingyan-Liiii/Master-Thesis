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

/**@file   class_seeed.cpp
 * @brief  class with functions for seeed (aka incomplete decomposition )
 * @author Michael Bastubbe
 *
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#include "class_seeed.h"
#include "gcg.h"
#include "class_seeedpool.h"
#include "scip/cons_setppc.h"
#include "scip/scip.h"
#include "scip_misc.h"

#include <iostream>
#include <exception>
#include <algorithm>
#include <queue>

#define SCIP_CALL_EXC(x)   do                                                                                  \
                       {                                                                                      \
                          SCIP_RETCODE _restat_;                                                              \
                          if( (_restat_ = (x)) !=  SCIP_OKAY )                                                \
                          {                                                                                   \
                             SCIPerrorMessage("Error <%d> in function call\n", _restat_);                     \
                             throw std::exception();                                                          \
                           }                                                                                  \
                       }                                                                                      \
                       while( FALSE )

namespace gcg {

const int Seeed::primes[] = { 2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37, 41, 43, 47, 53, 59, 61, 67, 71, 73, 79, 83, 89,
   97, 101, 103, 107, 109, 113, 127, 131, 137, 139, 149, 151, 157, 163, 167, 173, 179, 181, 191, 193, 197, 199, 211, 223,
   227, 229, 233, 239, 241, 251, 257, 263, 269, 271, 277, 281, 283, 293, 307, 311, 313, 317, 331, 337, 347, 349 };

const int Seeed::nPrimes = 70;

/** constructor(s) */
Seeed::Seeed(
   SCIP*       _scip,
   int         givenId,                    /**< id that is given to this seeed */
   int         givenNDetectors,            /**< number of detectors */
   int         givenNConss,                /**number of constraints */
   int         givenNVars                  /**number of variables */
) :
   scip(_scip), id(givenId), nBlocks(0), nVars(givenNVars), nConss(givenNConss), propagatedByDetector(
      std::vector<bool>(givenNDetectors, false)), openVarsAndConssCalculated(false)
{
}

Seeed::Seeed(const Seeed *seeedToCopy, Seeedpool* seeedpool)
{
   scip = (seeedToCopy->scip);
   id = seeedpool->getNewIdForSeeed();
   nBlocks = seeedToCopy->nBlocks;
   nVars = seeedToCopy->nVars;
   nConss = seeedToCopy->nConss;
   masterConss = seeedToCopy->masterConss;
   masterVars = seeedToCopy->masterVars;
   conssForBlocks = seeedToCopy->conssForBlocks;
   varsForBlocks = seeedToCopy->varsForBlocks;
   linkingVars = seeedToCopy->linkingVars;
   stairlinkingVars = seeedToCopy->stairlinkingVars;
   openVars = seeedToCopy->openVars;
   openConss = seeedToCopy->openConss;
   propagatedByDetector = seeedToCopy->propagatedByDetector;
   detectorChain = seeedToCopy->detectorChain;
   openVarsAndConssCalculated = seeedToCopy->openVarsAndConssCalculated;
}

Seeed::~Seeed()
{
}

bool compare_blocks(std::pair<int, int> const & a, std::pair<int, int> const & b)
{
   return (a.second < b.second);
}

/** add a block, returns the number of the new block */
int Seeed::addBlock()
{
   std::vector<int> vector = std::vector<int>(0);
   conssForBlocks.push_back(vector);
   varsForBlocks.push_back(vector);
   stairlinkingVars.push_back(vector);
   nBlocks++;
   return nBlocks - 1;
}

/** returns if constraints are assigned to blocks */
bool Seeed::alreadyAssignedConssToBlocks()
{
   for( int b = 0; b < this->nBlocks; ++b )
      if( conssForBlocks[b].size() != 0 )
         return true;
   return false;
}

/** returns if the open vars and conss are calculated */
bool Seeed::areOpenVarsAndConssCalculated()
{
   return openVarsAndConssCalculated;
}

/** assigns open conss and vars if they can be found in blocks */
SCIP_RETCODE Seeed::assignAllDependent(Seeedpool* seeedpool)
{
   bool success = true;

   while( success )
      success = assignHittingOpenconss(seeedpool) || assignHittingOpenvars(seeedpool);
   sort();
   return SCIP_OKAY;
}

/** fills out the vorder of the seeed with the hashmap constoblock if there are still assigned conss and vars */
SCIP_RETCODE Seeed::assignBorderFromConstoblock(SCIP_HASHMAP* constoblock, int givenNBlocks, Seeedpool* seeedpool)
{
   int cons;

   for( int i = 0; i < getNOpenconss(); ++i )
   {
      cons = openConss[i];
      if( !SCIPhashmapExists(constoblock, (void*)(size_t)cons) )
         continue;
      if( (int)(size_t)SCIPhashmapGetImage(constoblock, (void*)(size_t)cons) - 1 == givenNBlocks )
      {
         bookAsMasterCons(cons);
      }
   }

   flushBooked();

   sort();
   assert(checkConsistency());
   return SCIP_OKAY;
}

/** assigns openVars to Stairlinking if they can be found in two consecutive  blocks*/
bool Seeed::assignCurrentStairlinking(Seeedpool* seeedpool)
{
   std::vector<int> blocksOfOpenvar;
   bool foundInBlock;
   bool assigned = false;
   int var;
   int cons;

   if( !openVarsAndConssCalculated )
   {
      calcOpenvars();
      calcOpenconss();
      openVarsAndConssCalculated = true;
   }

   for( int i = 0; i < getNOpenvars(); ++i )
   {
      blocksOfOpenvar.clear();
      foundInBlock = false;
      var = openVars[i];
      for( int b = 0; b < nBlocks; ++b )
      {
         for( int c = 0; c < getNConssForBlock(b) && !foundInBlock; ++c )
         {
            cons = conssForBlocks[b][c];
            for( int v = 0; v < seeedpool->getNVarsForCons(cons) && !foundInBlock; ++v )
            {
               if( seeedpool->getVarsForCons(cons)[v] == var )
               {
                  blocksOfOpenvar.push_back(b);
               }
            }
         }
      }
      if( blocksOfOpenvar.size() == 2 && blocksOfOpenvar[0] + 1 == blocksOfOpenvar[1] )
      {
         bookAsStairlinkingVar(var, blocksOfOpenvar[0]);
         assigned = true;
      }
   }

   flushBooked();

   if( assigned )
      sort();
   return assigned;
}

/** assigns open conss if they includes blockvars, returns true if open conss are assigned */
bool Seeed::assignHittingOpenconss(Seeedpool* seeedpool)
{
   int cons;
   int var;
   int block;
   bool stairlinking; /** true if the cons includes stairlinkingvars */
   bool assigned = false; /** true if open conss are assigned in the function */
   std::vector<int>::iterator it;
   std::vector<int> blocksOfStairlinkingvars; /** first block of stairlinkingvars which can be found in the cons */
   std::vector<int> blocksOfBlockvars; /** blocks with blockvars which can be found in the cons */
   std::vector<int> blocks; /** cons can be assigned to the blocks stored in this vector */
   std::vector<int> eraseBlock;

   if( !openVarsAndConssCalculated )
   {
      calcOpenvars();
      calcOpenconss();
      openVarsAndConssCalculated = true;
   }

   for( size_t c = 0; c < openConss.size(); ++c )
   {
      cons = openConss[c];
      stairlinking = false;

      blocksOfBlockvars.clear();
      blocks.clear();
      blocksOfStairlinkingvars.clear();
      eraseBlock.clear();

      /** fill out blocksOfStairlinkingvars and blocksOfBlockvars */
      for( int v = 0; v < seeedpool->getNVarsForCons(cons); ++v )
      {
         var = seeedpool->getVarsForCons(cons)[v];
         for( int b = 0; b < nBlocks; ++b )
         {
            if( isVarBlockvarOfBlock(var, b) )
            {
               blocksOfBlockvars.push_back(b);
               break;
            }
         }
         for( int b = 0; b < nBlocks; ++b )
         {
            if( isVarStairlinkingvarOfBlock(var, b) )
            {
               stairlinking = true;
               blocksOfStairlinkingvars.push_back(b);
               break;
            }
         }
      }

      /** fill out blocks */
      if( stairlinking && blocksOfBlockvars.size() < 2 )
      {
         if( blocksOfBlockvars.size() == 0 )
         {
            blocks.push_back(blocksOfStairlinkingvars[0]);
            blocks.push_back(blocksOfStairlinkingvars[0] + 1);
            for( size_t i = 1; i < blocksOfStairlinkingvars.size(); ++i )
            {
               it = blocks.begin();
               for( ; it != blocks.end(); ++it )
               {
                  if( *it != blocksOfStairlinkingvars[i] && *it != blocksOfStairlinkingvars[i] + 1 )
                     eraseBlock.push_back(*it);
               }
               for( size_t j = 0; j < eraseBlock.size(); ++j )
               {
                  it = find(blocks.begin(), blocks.end(), eraseBlock[j]);
                  assert(it != blocks.end());
                  blocks.erase(it);
               }
            }
         }
         else
         {
            blocks.push_back(blocksOfBlockvars[0]);
            for( size_t i = 0; i < blocksOfStairlinkingvars.size(); ++i )
            {
               if( blocks[0] != blocksOfStairlinkingvars[i] && blocks[0] != blocksOfStairlinkingvars[i] + 1 )
               {
                  blocks.clear();
                  break;
               }
            }
         }

      }

      if( blocksOfBlockvars.size() > 1 )
      {
         bookAsMasterCons(cons);
         assigned = true;
      }
      else if( stairlinking == false && blocksOfBlockvars.size() == 1 )
      {
         bookAsBlockCons(cons, blocksOfBlockvars[0]);
         assigned = true;
      }
      else if( stairlinking == true && blocks.size() == 0 )
      {
         bookAsMasterCons(cons);
         assigned = true;
      }
      else if( stairlinking == true && blocks.size() == 1 )
      {
         bookAsBlockCons(cons, blocks[0]);
         assigned = true;
      }
      else if( stairlinking == true && blocks.size() > 1 )
      {
         block = blocks[0];
         for( size_t i = 1; i < blocks.size(); ++i )
         {
            if( getNConssForBlock(i) < getNConssForBlock(block) )
               block = i;
         }
         bookAsBlockCons(cons, block);
         assigned = true;
      }
   }

   flushBooked();

   if( assigned )
      sort();

   return assigned;
}

/** assigns open vars if they can be found in one block, returns true if open vars are assigned */
bool Seeed::assignHittingOpenvars(Seeedpool* seeedpool)
{
   int cons;
   int var;
   std::vector<int> blocksOfOpenvar;
   bool found;
   bool assigned = false;

   if( !openVarsAndConssCalculated )
   {
      calcOpenvars();
      calcOpenconss();
      openVarsAndConssCalculated = true;
   }

   /** set vars to linking, if they can be found in more than one block */
   for( size_t i = 0; i < openVars.size(); ++i )
   {
      blocksOfOpenvar.clear();
      var = openVars[i];
      for( int b = 0; b < nBlocks; ++b )
      {
         found = false;
         for( int c = 0; c < getNConssForBlock(b) && !found; ++c )
         {
            cons = conssForBlocks[b][c];
            for( int v = 0; v < seeedpool->getNVarsForCons(cons) && !found; ++v )
            {
               if( seeedpool->getVarsForCons(cons)[v] == var )
               {
                  blocksOfOpenvar.push_back(b);
                  found = true;
               }
            }
         }
      }
      if( blocksOfOpenvar.size() == 1 )
      {
         bookAsBlockVar(var, blocksOfOpenvar[0]);
         assigned = true;
      }
      else if( blocksOfOpenvar.size() > 1 )
      {
         bookAsLinkingVar(var);
         assigned = true;
      }
   }

   flushBooked();

   if( assigned )
      sort();

   return assigned;
}

/** assign open conss that hits a block and other open vars that to border */
SCIP_RETCODE Seeed::assignOpenPartialHittingConsToMaster(
   Seeedpool*       seeedpool
)
{
   int cons;
   int var;
   std::vector<int> blocksOfBlockvars; /** blocks with blockvars which can be found in the cons */
   std::vector<int> blocksOfOpenvar; /** blocks in which the open var can be found */
   bool found;
   bool master;
   bool hitsOpenVar;


   if( !openVarsAndConssCalculated )
   {
      calcOpenvars();
      calcOpenconss();
      openVarsAndConssCalculated = true;
   }

   /** set openConss with more than two blockvars to master */
   for( size_t c = 0; c < openConss.size(); ++c )
   {
      blocksOfBlockvars.clear();
      master = false;
      hitsOpenVar = false;
      cons = openConss[c];

      for( int v = 0; v < seeedpool->getNVarsForCons(cons) && !master; ++v )
      {
         var = seeedpool->getVarsForCons(cons)[v];

         if ( isVarOpenvar(var) )
         {
            hitsOpenVar = true;
            continue;
         }

         if( isVarMastervar(var) )
         {
            master = true;
            bookAsMasterCons(cons);
            continue;
         }

         for( int b = 0; b < nBlocks; ++b )
         {
            if( isVarBlockvarOfBlock(var, b) )
            {
               blocksOfBlockvars.push_back(b);
               break;
            }
         }
      }
      if( blocksOfBlockvars.size() == 1 && hitsOpenVar )
      {
         bookAsMasterCons(cons);
      }
   }

   flushBooked();

   return SCIP_OKAY;
}

/** assign open conss (and vars) that hits a block and other open vars (or cons) to border */
SCIP_RETCODE Seeed::assignOpenPartialHittingToMaster(
   Seeedpool*       seeedpool
)
{
   assignOpenPartialHittingConsToMaster(seeedpool);
   assignOpenPartialHittingVarsToMaster(seeedpool);

return SCIP_OKAY;
}

/** assign open vars that hits a block and other open cons that to border */
SCIP_RETCODE Seeed::assignOpenPartialHittingVarsToMaster(
   Seeedpool*       seeedpool
)
{

   int cons;
   int var;
   std::vector<int> blocksOfBlockvars; /** blocks with blockvars which can be found in the cons */
   std::vector<int> blocksOfOpenvar; /** blocks in which the open var can be found */
   bool found;
   bool master;
   bool hitsOpenCons;


   if( !openVarsAndConssCalculated )
   {
      calcOpenvars();
      calcOpenconss();
      openVarsAndConssCalculated = true;
   }

    /** set open var to linking if it can be found in one block an open constraint */
    for( size_t i = 0; i < openVars.size(); ++i )
    {
       blocksOfOpenvar.clear();
       var = openVars[i];
       hitsOpenCons = false;

       for( int c = 0; c < seeedpool->getNConssForVar(var); ++c )
       {
          cons = seeedpool->getConssForVar(var)[c];
          if( isConsOpencons(cons) )
          {
             hitsOpenCons = true;
             continue;
          }
          for( int b = 0; b < nBlocks; ++b )
          {
             if( isConsBlockconsOfBlock(cons,b) )
                blocksOfOpenvar.push_back(b);
          }

       }

       if( blocksOfOpenvar.size() == 1 && hitsOpenCons )
       {
          bookAsLinkingVar(var);
       }
    }

    flushBooked();

    return SCIP_OKAY;


return SCIP_OKAY;
}

/** fills out the seeed with the hashmap constoblock if there are still assigned conss and vars */
SCIP_RETCODE Seeed::assignSeeedFromConstoblock(SCIP_HASHMAP* constoblock, int givenNBlocks, Seeedpool* seeedpool)
{
   int oldNBlocks = nBlocks;
   int consblock;
   int cons;

   assert(givenNBlocks >= 0);

   for( int b = 0; b < givenNBlocks; ++b )
      addBlock();

   for( int i = 0; i < getNOpenconss(); ++i )
   {
      cons = openConss[i];
      if( !SCIPhashmapExists(constoblock, (void*)(size_t)cons) )
         continue;
      consblock = oldNBlocks + ((int)(size_t)SCIPhashmapGetImage(constoblock, (void*)(size_t)cons) - 1);
      assert(consblock >= oldNBlocks && consblock <= nBlocks);
      if( consblock == nBlocks )
         bookAsMasterCons(cons);
      else
         bookAsBlockCons(cons, consblock);
   }

   flushBooked();

   deleteEmptyBlocks();
   sort();
   assert(checkConsistency());
   return SCIP_OKAY;
}

/** book a constraint to be added to the block constraints of the given block (after calling flushBookes) */
SCIP_RETCODE Seeed::bookAsBlockCons(
        int consToBlock,
        int block
)
{
   std::pair<int, int> pair(consToBlock, block);
   bookedAsBlockConss.push_back(pair);
   return SCIP_OKAY;
}

/** book a variable to be added to the master variables (after calling flushBooked) */
SCIP_RETCODE Seeed::bookAsLinkingVar(int varToLinking)
{
   bookedAsLinkingVars.push_back(varToLinking);
   return SCIP_OKAY;
}

/** book a constraint to be added to the master constraints (after calling flushBooked)*/
SCIP_RETCODE Seeed::bookAsMasterCons(int consToMaster)
{
   bookedAsMasterConss.push_back(consToMaster);
   return SCIP_OKAY;
}

/** book a variable to be added to the block constraints of the given block (after calling flushBookes) */
SCIP_RETCODE Seeed::bookAsBlockVar(int varToBlock, int block)
{
   std::pair<int, int> pair(varToBlock, block);
   bookedAsBlockVars.push_back(pair);
   return SCIP_OKAY;
}

/** book a variable to be added to the master variables (after calling flushBooked) */
SCIP_RETCODE Seeed::bookAsMasterVar(int varToMaster)
{
   bookedAsMasterVars.push_back(varToMaster);
   return SCIP_OKAY;
}

/** book a variable to be added to the stairlinking variables of the given block and the following block (after calling flushBookes) */
SCIP_RETCODE Seeed::bookAsStairlinkingVar(int varToStairlinking, int firstBlock)
{
   std::pair<int, int> pair(varToStairlinking, firstBlock);
   bookedAsStairlinkingVars.push_back(pair);
   return SCIP_OKAY;
}

/** calculates the hashvalue of the seeed for comparing */
void Seeed::calcHashvalue()
{
   std::vector<std::pair<int, int>> blockorder = std::vector<std::pair<int, int> >(0);
   long hashval = 0;
   long borderval = 0;

   /** find sorting for blocks (non decreasing according smallest row index) */
   for( int i = 0; i < this->nBlocks; ++i )
   {
      blockorder.push_back(std::pair<int, int>(i, this->conssForBlocks[i][0]));
   }

   std::sort(blockorder.begin(), blockorder.end(), compare_blocks);

   for( int i = 0; i < nBlocks; ++i )
   {
      long blockval = 0;

      for( int tau = 0; tau < conssForBlocks[i].size(); ++tau )
      {
         blockval += (2 * conssForBlocks[i][tau] + 1) * pow(2, tau % 16);
      }

      hashval += primes[i % (nPrimes - 1)] * blockval;
   }

   for( int tau = 0; tau < masterConss.size(); ++tau )
   {
      borderval += (2 * masterConss[tau] + 1) * pow(2, tau % 16);
   }

   hashval += primes[nBlocks % nPrimes] * borderval;

   hashval += primes[(nBlocks + 1) % nPrimes] * openVars.size();

   this->hashvalue = hashval;

   return;
}

/** calculates vector containing constraints not assigned yet */
void Seeed::calcOpenconss()
{
   std::vector<bool> openConssBool(nConss, true);
   openConss.clear();
   std::vector<int>::const_iterator consIter = masterConss.begin();
   std::vector<int>::const_iterator consIterEnd = masterConss.end();
   for( ; consIter != consIterEnd; ++consIter )
      openConssBool[*consIter] = false;
   for( int b = 0; b < nBlocks; ++b )
   {
      consIter = conssForBlocks[b].begin();
      consIterEnd = conssForBlocks[b].end();
      for( ; consIter != consIterEnd; ++consIter )
         openConssBool[*consIter] = false;
   }

   for( int i = 0; i < nConss; ++i )
   {
      if( openConssBool[i] )
         openConss.push_back(i);
   }

   return;
}

/** constructs vector containing variables not assigned yet */
void Seeed::calcOpenvars()
{

   openVars = std::vector<int>(0);
   std::vector<bool> openVarsBool(nVars, true);

   std::vector<int>::const_iterator varIter = linkingVars.begin();
   std::vector<int>::const_iterator varIterEnd = linkingVars.end();
   for( ; varIter != varIterEnd; ++varIter )
      openVarsBool[*varIter] = false;

   varIter = masterVars.begin();
   varIterEnd = masterVars.end();
   for( ; varIter != varIterEnd; ++varIter )
      openVarsBool[*varIter] = false;

   for( int b = 0; b < nBlocks; ++b )
   {
      varIter = varsForBlocks[b].begin();
      varIterEnd = varsForBlocks[b].end();
      for( ; varIter != varIterEnd; ++varIter )
         openVarsBool[*varIter] = false;
   }

   for( int b = 0; b < nBlocks; ++b )
   {
      varIter = stairlinkingVars[b].begin();
      varIterEnd = stairlinkingVars[b].end();
      for( ; varIter != varIterEnd; ++varIter )
         openVarsBool[*varIter] = false;
   }

   for( int i = 0; i < nVars; ++i )
   {
      if( openVarsBool[i] )
         openVars.push_back(i);
   }

   return;

}

/** returns whether all cons are assigned and deletes the vector open cons if all are assigned */
bool Seeed::checkAllConsAssigned()
{
   for( size_t i = 0; i < openConss.size(); ++i )
   {
      bool consfound = false;
      for( size_t k = 0; k < masterConss.size(); ++k )
      {
         if( openConss[i] == masterConss[k] )
         {
            consfound = true;
            break;
         }
      }
      for( int b = 0; b < nBlocks && !consfound; ++b )
      {
         for( size_t k = 0; k < conssForBlocks[b].size(); ++k )
         {
            if( openConss[i] == conssForBlocks[b][k] )
            {
               consfound = true;
               break;
            }
         }
      }
      if( !consfound )
      {
         return false;
      }
   }
   openConss.clear();
   return true;
}

/** check the consistency of this seeed */
bool Seeed::checkConsistency()
{

   std::vector<bool> openVarsBool(nVars, true);
   std::vector<int> stairlinkingvarsvec(0);
   int firstFound;
   std::vector<int>::const_iterator varIter = linkingVars.begin();
   std::vector<int>::const_iterator varIterEnd = linkingVars.end();
   int value;

   /** check if nblocks is set appropriate */
   if( nBlocks != conssForBlocks.size() )
   {
      std::cout << "Warning! In (seeed " << id << ") nBlocks " << nBlocks << " and size of conssForBlocks"
         << conssForBlocks.size() << " are not identical" << std::endl;
      assert(false);
      return false;
   }

   if( nBlocks != varsForBlocks.size() )
   {
      std::cout << "Warning! In (seeed " << id << ") nBlocks " << nBlocks << " and size of varsForBlocks"
         << varsForBlocks.size() << " are not identical" << std::endl;
      assert(false);
      return false;
   }

   /** check for empty (row- and col-wise) blocks */

   for( int b = 0; b < nBlocks; ++b )
   {
      if( conssForBlocks[b].size() == 0 && varsForBlocks[b].size() == 0 )
      {
         std::cout << "Warning! In (seeed " << id << ") block " << b << " is empty!" << std::endl;
         this->displaySeeed();
         assert(false);
         return false;
      }
   }

   /**check variables (every variable is assigned at most once) */

   for( ; varIter != varIterEnd; ++varIter )
   {
      if( !openVarsBool[*varIter] )
      {
         std::cout << "Warning! (seeed " << id << ") Variable with index " << *varIter << " is already assigned."
            << std::endl;
         assert(false);
         return false;
      }
      openVarsBool[*varIter] = false;
   }

   for( int b = 0; b < nBlocks; ++b )
   {
      varIter = varsForBlocks[b].begin();
      varIterEnd = varsForBlocks[b].end();
      for( ; varIter != varIterEnd; ++varIter )
      {
         if( !openVarsBool[*varIter] )
         {
            std::cout << "Warning! (seeed " << id << ") Variable with index " << *varIter << " is already assigned."
               << std::endl;
            assert(false);
            return false;
         }
         openVarsBool[*varIter] = false;
      }
   }

   varIter = masterVars.begin();
   varIterEnd = masterVars.end();
   for( ; varIter != varIterEnd; ++varIter )
   {
      if( !openVarsBool[*varIter] )
      {
         std::cout << "Warning! (seeed " << id << ") Variable with index " << *varIter << " is already assigned."
            << std::endl;
         assert(false);
         return false;
      }
      openVarsBool[*varIter] = false;
   }

   /** vector of of all stairlinkingvars */
   for( int b = 0; b < nBlocks; ++b )
   {
      for( size_t i = 0; i < stairlinkingVars[b].size(); ++i )
      {
         if( find(stairlinkingvarsvec.begin(), stairlinkingvarsvec.end(), stairlinkingVars[b][i])
            == stairlinkingvarsvec.end() )
         {
            stairlinkingvarsvec.push_back(stairlinkingVars[b][i]);
         }
      }
   }

   varIter = stairlinkingvarsvec.begin();
   varIterEnd = stairlinkingvarsvec.end();
   for( ; varIter != varIterEnd; ++varIter )
   {
      firstFound = -1;
      for( int b = 0; b < nBlocks; ++b )
      {
         if( isVarStairlinkingvarOfBlock(*varIter, b) )
         {
            firstFound = b;
            openVarsBool[*varIter] = false;
            break;
         }
      }
      if( firstFound == -1 )
      {
         std::cout << "Warning! (seeed " << id << ") Variable with index " << *varIter
            << " is assigned to the stairlinkingvars but can not be found in a block" << std::endl;
         assert(false);
         return false;
      }
      if( firstFound == nBlocks - 1 || !isVarStairlinkingvarOfBlock(*varIter, firstFound + 1) )
      {
         std::cout << "Warning! (seeed " << id << ") Variable with index " << *varIter
            << " is assigned to the stairlinkingvars but doens't link blocks" << std::endl;
         assert(false);
         return false;
      }
      for( int b = firstFound + 2; b < nBlocks; ++b )
      {
         if( isVarBlockvarOfBlock(*varIter, b) )
         {
            std::cout << "Warning! (seeed " << id << ") Variable with index " << *varIter
               << " is assigned to the stairlinkingvars but can be found in more than two blocks" << std::endl;
            assert(false);
            return false;
         }
      }
   }

   if( !openVarsAndConssCalculated )
   {
      calcOpenconss();
      calcOpenvars();

      openVarsAndConssCalculated = true;
   }

   /** check if all not assigned variables are open vars */
   for( int v = 0; v < nVars; ++v )
   {
      if( openVarsBool[v] == true && isVarOpenvar(v) == false )
      {
         std::cout << "Warning! (seeed " << id << ") Variable with index " << v << " is not assigned and not an open var."
            << std::endl;
         assert(false);
         return false;
      }
   }

   /** check if all open vars are not assigned */
   for( size_t i = 0; i < openVars.size(); ++i )
   {
      if( openVarsBool[openVars[i]] == false )
      {
         std::cout << "Warning! (seeed " << id << ") Variable with index " << openVars[i] << " is an open var but assigned."
            << std::endl;
         assert(false);
         return false;
      }
   }

   /** check constraints (every constraint is assigned at most once) */
   std::vector<bool> openConssBool(nConss, true);
   std::vector<int> openConssVec(0);
   std::vector<int>::const_iterator consIter = masterConss.begin();
   std::vector<int>::const_iterator consIterEnd = masterConss.end();

   for( ; consIter != consIterEnd; ++consIter )
   {
      if( !openConssBool[*consIter] )
      {
         std::cout << "Warning! (seeed " << id << ") Constraint with index " << *consIter << "is already assigned "
            << std::endl;
         assert(false);
         return false;
      }
      openConssBool[*consIter] = false;
   }

   for( int b = 0; b < nBlocks; ++b )
   {
      consIter = conssForBlocks[b].begin();
      consIterEnd = conssForBlocks[b].end();
      for( ; consIter != consIterEnd; ++consIter )
      {
         if( !openConssBool[*consIter] )
         {
            std::cout << "Warning! (seeed " << id << ") Constraint with index " << *consIter << " is already assigned "
               << std::endl;
            assert(false);
            return false;
         }
         openConssBool[*consIter] = false;
      }
   }

   /** check if all not assigned constraints are open cons */
   for( int v = 0; v < nConss; ++v )
   {
      if( openConssBool[v] == true && isConsOpencons(v) == false )
      {
         std::cout << "Warning! (seeed " << id << ") Constraint with index " << v
            << " is not assigned and not an open cons." << std::endl;
         assert(false);
         return false;
      }
   }

   /** check if all open conss are not assigned */
   for( size_t i = 0; i < openConss.size(); ++i )
   {
      if( openConssBool[openConss[i]] == false )
      {
         std::cout << "Warning! (seeed " << id << ") Constraint with index " << openConss[i]
            << " is an open cons but assigned." << std::endl;
         assert(FALSE);
         return false;
      }
   }

   /** check if the seeed is sorted */
   for( int b = 0; b < nBlocks; ++b )
   {
      value = -1;
      for( int v = 0; v < getNVarsForBlock(b); ++v )
      {
         if( !(value < getVarsForBlock(b)[v]) )
         {
            std::cout << "Warning! (seeed " << id << ") Variables of block " << b << " are not sorted." << std::endl;
            assert(false);
            return false;
         }
         value = getVarsForBlock(b)[v];
      }
   }
   for( int b = 0; b < nBlocks; ++b )
   {
      value = -1;
      for( int v = 0; v < getNStairlinkingvars(b); ++v )
      {
         if( !(value < getStairlinkingvars(b)[v]) )
         {
            std::cout << "Warning! (seeed " << id << ") Stairlinkingvariables of block " << b << " are not sorted."
               << std::endl;
            assert(false);
            return false;
         }
         value = getStairlinkingvars(b)[v];
      }
   }
   value = -1;
   for( int v = 0; v < getNLinkingvars(); ++v )
   {
      if( !(value < getLinkingvars()[v]) )
      {
         std::cout << "Warning! (seeed " << id << ") Linkingvariables are not sorted." << std::endl;
         assert(false);
         return false;
      }
   }
   value = -1;
   for( int v = 0; v < getNMastervars(); ++v )
   {
      if( !(value < getMastervars()[v]) )
      {
         std::cout << "Warning! (seeed " << id << ") Mastervariables are not sorted." << std::endl;
         assert(false);
         return false;
      }
   }
   for( int b = 0; b < nBlocks; ++b )
   {
      value = -1;
      for( int v = 0; v < getNConssForBlock(b); ++v )
      {
         if( !(value < getConssForBlock(b)[v]) )
         {
            std::cout << "Warning! (seeed " << id << ") Constraints of block " << b << " are not sorted." << std::endl;
            assert(false);
            return false;
         }
         value = getConssForBlock(b)[v];
      }
   }
   value = -1;
   for( int v = 0; v < getNMasterconss(); ++v )
   {
      if( !(value < getMasterconss()[v]) )
      {
         std::cout << "Warning! (seeed " << id << ") Masterconstraints are not sorted." << std::endl;
         assert(false);
         return false;
      }
   }

   return true;
}

bool Seeed::checkVarsAndConssConsistency(Seeedpool* seeedpool)
{
   std::vector<int>::const_iterator consIter;
   std::vector<int>::const_iterator consIterEnd;
   int var;

   for( int b = 0; b < nBlocks; ++b )
   {
      consIter = conssForBlocks[b].begin();
      consIterEnd = conssForBlocks[b].end();
      for( ; consIter != consIterEnd; ++consIter )
      {
         for( int v = 0; v < seeedpool->getNVarsForCons(*consIter); ++v )
         {
            var = seeedpool->getVarsForCons(*consIter)[v];
            if( !isVarMastervar(var) && !isVarBlockvarOfBlock(var, b) && !isVarStairlinkingvarOfBlock(var, b)
               && !isVarLinkingvar(var) && !isVarOpenvar(var) )
            {
               return false;
            }

         }
      }
   }
   return true;
}

/** assigns the open cons and open vars */
SCIP_RETCODE Seeed::completeGreedily(Seeedpool* seeedpool)
{

   bool checkVar;
   bool varInBlock;
   bool notassigned;

   /** tools to check if the openVars can still be found in a constraint yet*/
   std::vector<int> varInBlocks; /** stores, in which block the variable can be found */

   /** tools to update openVars */

   if( !openVarsAndConssCalculated )
   {
      calcOpenconss();
      calcOpenvars();

      openVarsAndConssCalculated = true;
   }

   assert((int ) conssForBlocks.size() == nBlocks);
   assert((int ) varsForBlocks.size() == nBlocks);
   assert((int ) stairlinkingVars.size() == nBlocks);

   if( nBlocks == 0 && openConss.size() > 0 )
   {
      nBlocks = 1;
      std::vector<int> vec = std::vector<int>(0);
      conssForBlocks.push_back(vec);
      varsForBlocks.push_back(vec);
      stairlinkingVars.push_back(vec);
      if( openConss.size() != 0 )
      {
         setConsToBlock(openConss[0], 0);
         openConss.erase(openConss.begin());
      }
      else if( masterConss.size() != 0 )
      {
         setConsToBlock(masterConss[0], 0);
         masterConss.erase(masterConss.begin());
      }
      else
         assert(!(openConss.size() == 0 && masterConss.size() == 0));
   }

   /** check if the openVars can found in a constraint yet */
   for( size_t i = 0; i < openVars.size(); ++i )
   {
      varInBlocks.clear();

      /** test if the variable can be found in blocks */
      for( int b = 0; b < nBlocks; ++b )
      {
         varInBlock = false;
         for( size_t k = 0; k < conssForBlocks[b].size() && !varInBlock; ++k )
         {
            for( int l = 0; l < seeedpool->getNVarsForCons(conssForBlocks[b][k]); ++l )
            {
               if( openVars[i] == seeedpool->getVarsForCons(conssForBlocks[b][k])[l] )
               {
                  varInBlocks.push_back(b);
                  varInBlock = true;
                  break;
               }
            }
         }
      }
      if( varInBlocks.size() == 1 ) /** if the variable can be found in one block set the variable to a variable of the block*/
      {
         bookAsBlockVar(openVars[i], varInBlocks[0]);
         continue; /** the variable does'nt need to be checked any more */
      }
      else if( varInBlocks.size() == 2 ) /** if the variable can be found in two blocks check if it is a linking var or a stairlinking var*/
      {
         if( varInBlocks[0] + 1 == varInBlocks[1] )
         {
            bookAsStairlinkingVar(openVars[i], varInBlocks[0]);
            continue; /** the variable does'nt need to be checked any more */
         }
         else
         {
            bookAsLinkingVar(openVars[i]);
            continue; /** the variable does'nt need to be checked any more */
         }
      }
      else if( varInBlocks.size() > 2 ) /** if the variable can be found in more than two blocks it is a linking var */
      {
         bookAsLinkingVar(openVars[i]);
         continue; /** the variable does'nt need to be checked any more */
      }

      /** if the variable can be found in an open constraint it is still an open var */
      for( size_t j = 0; j < openConss.size(); ++j )
      {
         for( int k = 0; k < seeedpool->getNVarsForCons(j); ++k )
         {
            if( openVars[i] == seeedpool->getVarsForCons(j)[k] )
            {
               checkVar = false;
               break;
            }
         }
         if( !checkVar )
         {
            break;
         }
      }

      /** test if the variable can be found in a master constraint yet */
      for( size_t j = 0; j < masterConss.size() && checkVar; ++j )
      {
         for( int k = 0; k < seeedpool->getNVarsForCons(masterConss[j]); ++k )
         {
            if( openVars[i] == seeedpool->getVarsForCons(masterConss[j])[k] )
            {
               bookAsMasterVar(openVars[i]);
               checkVar = false; /** the variable does'nt need to be checked any more */
               break;
            }
         }
      }
   }

   flushBooked();

   /** assign open conss greedily */
   for( size_t i = 0; i < openConss.size(); ++i )
   {
      std::vector<int> vecOpenvarsOfBlock; /** stores the open vars of the blocks */
      bool consGotBlockcons = false; /** if the constraint can be assigned to a block */

      /** check if the constraint can be assigned to a block */
      for( int j = 0; j < nBlocks; ++j )
      {
         /** check if all vars of the constraint are a block var of the current block, an open var, a linkingvar or a mastervar*/
         consGotBlockcons = true;
         for( int k = 0; k < seeedpool->getNVarsForCons(openConss[i]); ++k )
         {
            if( isVarBlockvarOfBlock(seeedpool->getVarsForCons(openConss[i])[k], j)
               || isVarOpenvar(seeedpool->getVarsForCons(openConss[i])[k])
               || isVarLinkingvar(seeedpool->getVarsForCons(openConss[i])[k])
               || isVarStairlinkingvarOfBlock(seeedpool->getVarsForCons(openConss[i])[k], j) )
            {
               if( isVarOpenvar(seeedpool->getVarsForCons(openConss[i])[k]) )
               {
                  vecOpenvarsOfBlock.push_back(seeedpool->getVarsForCons(openConss[i])[k]); /**!!!*/
               }
            }
            else
            {
               vecOpenvarsOfBlock.clear(); /** the open vars do'nt get vars of the block */
               consGotBlockcons = false; /** the constraint can't be constraint of the block, check the next block */
               break;
            }
         }
         if( consGotBlockcons ) /** the constraint can be assigned to the current block */
         {
            bookAsBlockCons(openConss[i], j);
            for( size_t k = 0; k < vecOpenvarsOfBlock.size(); ++k ) /** the openvars in the constraint get block vars */
            {
               setVarToBlock(vecOpenvarsOfBlock[k], j);
               deleteOpenvar(vecOpenvarsOfBlock[k]);
            }
            vecOpenvarsOfBlock.clear();

            break;
         }
      }

      if( !consGotBlockcons ) /** the constraint can not be assigned to a block, set it to master */
         bookAsMasterCons(openConss[i]);
   }

   flushBooked();

   /** assign open vars greedily */
   for( size_t i = 0; i < openVars.size(); ++i )
   {
      notassigned = true;
      for( size_t j = 0; j < masterConss.size() && notassigned; ++j )
      {
         for( int k = 0; k < seeedpool->getNVarsForCons(masterConss[j]); ++k )
         {
            if( openVars[i] == seeedpool->getVarsForCons(masterConss[j])[k] )
            {
               bookAsMasterVar(openVars[i]);
               notassigned = false;
               break;
            }
         }
      }
   }

   flushBooked();

   /** check if the open cons are all assigned */
   if( !checkAllConsAssigned() )
   {
      std::cout << "ERROR: Something went wrong, there are still open cons, although all should have been assigned ";
      assert(false);
   }

   /** check if the open vars are all assigned */
   if( !openVars.empty() )
   {
      std::cout << "ERROR: Something went wrong, there are still open vars, although all should have been assigned ";
      assert(false);
   }

   sort();
   assert(checkConsistency());

   return SCIP_OKAY;

}

/** assigns the open cons and open vars */
SCIP_RETCODE Seeed::completeByConnected(Seeedpool* seeedpool)
{

   bool checkVar;
   bool varInBlock;
   bool notassigned;
   int cons;
   int var;


   /** tools to check if the openVars can still be found in a constraint yet*/
   std::vector<int> varInBlocks; /** stores, in which block the variable can be found */

   /** tools to update openVars */
   std::vector<int> openvarsToDelete(0);
   std::vector<int> oldOpenconss;

   std::vector<bool> isConsOpen(nConss, false);
   std::vector<bool> isConsVisited(nConss, false);

   std::vector<bool> isVarOpen(nVars, false);
   std::vector<bool> isVarVisited(nVars, false);

   std::queue<int> helpqueue = std::queue<int>();
   std::vector<int> neighborConss(0);
   std::vector<int> neighborVars(0);

   if( !openVarsAndConssCalculated )
   {
      calcOpenconss();
      calcOpenvars();
      openVarsAndConssCalculated = true;
   }

   assert((int ) conssForBlocks.size() == nBlocks);
   assert((int ) varsForBlocks.size() == nBlocks);
   assert((int ) stairlinkingVars.size() == nBlocks);

   SCIP_CALL( refineToMaster(seeedpool) );

   if(nBlocks < 0)
      nBlocks = 0;

   /** initialize data structures */
   for( size_t c = 0; c < openConss.size(); ++c )
   {
        cons = openConss[c];
        isConsOpen[cons] = true;
   }

   for( size_t v = 0; v < openVars.size(); ++v )
   {
        var = openVars[v];
        isVarOpen[var] = true;
   }

   /** do breadth first search */
   while( !openConss.empty() )
   {
      int newBlockNr;

      assert(helpqueue.empty());
      helpqueue.push(openConss[0]);
      neighborConss.clear();
      neighborConss.push_back(openConss[0]);
      isConsVisited[openConss[0] ] = true;
      neighborVars.clear();

      while( !helpqueue.empty() )
      {
         int nodeCons = helpqueue.front();
         assert( isConsOpencons(nodeCons) );
         helpqueue.pop();
         for( int v = 0; v < seeedpool->getNVarsForCons(nodeCons) ; ++v )
         {
            var = seeedpool->getVarsForCons(nodeCons)[v];
            assert( isVarOpenvar(var) || isVarLinkingvar(var ) );

            if( isVarVisited[var] || isVarLinkingvar(var) )
               continue;

            for( int c = 0; c < seeedpool->getNConssForVar(var) ; ++c )
            {
               int otherNodeCons  = seeedpool->getConssForVar(var)[c];
               if( !isConsOpen[otherNodeCons] || isConsVisited[otherNodeCons] )
               {
                  continue;
               }
               assert(isConsOpencons(otherNodeCons) );
               isConsVisited[otherNodeCons] = true;
               neighborConss.push_back(otherNodeCons);
               helpqueue.push(otherNodeCons);
            }
            isVarVisited[var] = true;
            neighborVars.push_back(var);
         }
      } //endwhile(!queue.empty() )

      newBlockNr = getNBlocks() + 1;
      setNBlocks(newBlockNr);
      for ( size_t i = 0; i < neighborConss.size(); ++i )
      {
         cons = neighborConss[i];

         assert(isConsOpencons(cons) );
         setConsToBlock(cons, newBlockNr-1);

         deleteOpencons(cons);
      }
      for ( size_t i = 0; i < neighborVars.size(); ++i )
      {
         var = neighborVars[i];
         setVarToBlock(var, newBlockNr-1);
         assert(isVarOpenvar(var) );
         deleteOpenvar(var);
      }

   } // endwhile( !openConss.empty() )


   for ( size_t i = 0; i < openVars.size(); ++i )
   {
      var = openVars[i];
      if(getNBlocks() != 0)
         setVarToBlock(var, 0);
      else
         setVarToMaster(var);
      openvarsToDelete.push_back(var);
   }

   for ( size_t i = 0; i < openvarsToDelete.size(); ++i )
   {
         var = openvarsToDelete[i];
         deleteOpenvar(var);
    }


   assert(openConss.empty());
   assert(openVars.empty());

   sort();
   assert(checkConsistency());

   return SCIP_OKAY;

}

/** assigns the open cons and open vars which are implicitly assigned, i.e. constraints having variables in more than one block or having variables only in one block and no open vars; vice versa for variables */
SCIP_RETCODE Seeed::considerImplicits(Seeedpool* seeedpool)
{
   int cons;
   int var;
   std::vector<int> blocksOfBlockvars; /** blocks with blockvars which can be found in the cons */
   std::vector<int> blocksOfOpenvar; /** blocks in which the open var can be found */
   bool found;
   bool master;
   bool hitsOpenVar;
   bool hitsOpenCons;


   if( !openVarsAndConssCalculated )
   {
      calcOpenvars();
      calcOpenconss();
      openVarsAndConssCalculated = true;
   }

   /** set openConss with more than two blockvars to master */
   for( size_t c = 0; c < openConss.size(); ++c )
   {
      blocksOfBlockvars.clear();
      master = false;
      hitsOpenVar = false;
      cons = openConss[c];


      for( int v = 0; v < seeedpool->getNVarsForCons(cons) && !master; ++v )
      {
         var = seeedpool->getVarsForCons(cons)[v];

         if ( isVarOpenvar(var) )
         {
            hitsOpenVar = true;
            continue;
         }

         if( isVarMastervar(var) )
         {
            master = true;
            bookAsMasterCons(cons);
            continue;
         }

         for( int b = 0; b < nBlocks && !master; ++b )
         {
            if( isVarBlockvarOfBlock(var, b) )
            {
               blocksOfBlockvars.push_back(b);
               break;
            }
         }
      }

      if( blocksOfBlockvars.size() > 1 && !master )
         bookAsMasterCons(cons);

      /* also assign open constraints that have only vars assigned to one single block and no open vars*/
      if( blocksOfBlockvars.size() == 1 && !hitsOpenVar && !master)
         bookAsBlockCons(cons, blocksOfBlockvars[0]);

   }

   flushBooked();

   /** set open var to linking, if it can be found in more than one block or set it to a block if it has only constraints in that block and no opnen constriants */
   for( size_t i = 0; i < openVars.size(); ++i )
   {
      blocksOfOpenvar.clear();
      var = openVars[i];
      hitsOpenCons = false;

      for( int c = 0; c < seeedpool->getNConssForVar(var); ++c )
      {
         cons = seeedpool->getConssForVar(var)[c];
         if( isConsOpencons(cons) )
         {
            hitsOpenCons = true;
            continue;
         }
         for( int b = 0; b < nBlocks; ++b )
         {
            if( isConsBlockconsOfBlock(cons,b) )
               blocksOfOpenvar.push_back(b);
         }

      }
      if( blocksOfOpenvar.size() > 1 )
      {
         bookAsLinkingVar(var);
         continue;
      }

      if( blocksOfOpenvar.size() == 1 && !hitsOpenCons )
      {
         bookAsBlockVar(var, blocksOfOpenvar[0]);
      }
   }

   flushBooked();

   return SCIP_OKAY;
}

/** deletes empty blocks */
SCIP_RETCODE Seeed::deleteEmptyBlocks()
{
   bool emptyBlocks = true;
   int block;
   int b;
   while(emptyBlocks)
   {
      emptyBlocks = false;
      for(b = 0; b < nBlocks; ++b)
      {
         if(conssForBlocks[b].size() == 0 && varsForBlocks[b].size() == 0)
         {
            emptyBlocks = true;
            block = b;
         }
      }
      if(emptyBlocks)
      {
         nBlocks--;

         std::vector<std::vector<int>>::iterator it;

         it = stairlinkingVars.begin();
         for( b = 0; b < block + 1; ++b)
            it++;
         stairlinkingVars.erase(it);

         it = conssForBlocks.begin();
         for( b = 0; b < block + 1; ++b)
            it++;
         conssForBlocks.erase(it);

         it = varsForBlocks.begin();
         for( b = 0; b < block + 1; ++b)
            it++;
         varsForBlocks.erase(it);
      }
   }
   return SCIP_OKAY;
}

/** deletes an open conss */
SCIP_RETCODE Seeed::deleteOpencons(int opencons)
{
   std::vector<int>::iterator it;
   it = find(openConss.begin(), openConss.end(), opencons);
   assert(it != openConss.end());
   openConss.erase(it);
   return SCIP_OKAY;
}

/** deletes an open var */
SCIP_RETCODE Seeed::deleteOpenvar(int openvar)
{
   std::vector<int>::iterator it;
   it = find(openVars.begin(), openVars.end(), openvar);
   assert(it != openVars.end());
   openVars.erase(it);
   return SCIP_OKAY;
}

/** displays the assignments of the conss */
SCIP_RETCODE Seeed::displayConss()
{
   for( int b = 0; b < nBlocks; ++b )
   {
      if( getNConssForBlock(b) != 0 )
      {
         std::cout << "constraint(s) in block " << b << ": ";
         std::cout << getConssForBlock(b)[0];
         for( int c = 1; c < getNConssForBlock(b); ++c )
            std::cout << ", " << getConssForBlock(b)[c];
         std::cout << "\n";
      }
      else
         std::cout << "0 constraints in block " << b << std::endl;
   }

   if( getNMasterconss() != 0 )
   {
      std::cout << "masterconstraint(s): ";
      std::cout << masterConss[0];
      for( int c = 1; c < getNMasterconss(); ++c )
         std::cout << ", " << masterConss[c];
      std::cout << "\n";
   }
   else
      std::cout << "0 masterconstraints" << std::endl;

   if( getNOpenconss() != 0 )
   {
      std::cout << "open constraint(s): ";
      std::cout << openConss[0];
      for( int c = 1; c < getNOpenconss(); ++c )
         std::cout << ", " << openConss[c];
      std::cout << "\n";
   }
   else
      std::cout << "0 open constraints" << std::endl;

   return SCIP_OKAY;
}

/** displays the relevant information of the seeed */
SCIP_RETCODE Seeed::displaySeeed()
{
   std::cout << "ID: " << id << std::endl;
   std::cout << "number of blocks: " << nBlocks << std::endl;
   std::cout << "hashvalue: " << hashvalue << std::endl;

   for( int b = 0; b < nBlocks; ++b )
   {
      std::cout << getNConssForBlock(b) << " constraint(s) in block " << b << std::endl;
      std::cout << getNVarsForBlock(b) << " variable(s) in block " << b << std::endl;
      std::cout << getNStairlinkingvars(b) << " stairlinkingvariable(s) in block " << b << std::endl;
   }

   std::cout << getNLinkingvars() << " linkingvariable(s)" << std::endl;
   std::cout << getNMasterconss() << " mastercontraint(s)" << std::endl;
   std::cout << getNMastervars() << " mastervariable(s)" << std::endl;
   std::cout << getNOpenconss() << " open constraint(s)" << std::endl;
   std::cout << getNOpenvars() << " open variable(s)" << std::endl;
   std::cout << getNDetectors() << " detector(s)";
   if( getNDetectors() != 0 )
   {
      std::cout << ": " << detectorChain[0];
      for( int d = 1; d < getNDetectors(); ++d )
         std::cout << ", " << detectorChain[d];
   }
   std::cout << "\n";

   return SCIP_OKAY;
}

/** displays the assignments of the vars */
SCIP_RETCODE Seeed::displayVars()
{
   for( int b = 0; b < nBlocks; ++b )
   {
      if( getNVarsForBlock(b) != 0 )
      {
         std::cout << "variable(s) in block " << b << ": ";
         std::cout << getVarsForBlock(b)[0];
         for( int c = 1; c < getNVarsForBlock(b); ++c )
            std::cout << ", " << getVarsForBlock(b)[c];
         std::cout << "\n";
      }
      else
         std::cout << "0 variables in block " << b << std::endl;
      if( getNStairlinkingvars(b) != 0 )
      {
         std::cout << "stairlinkingvariable(s) in block " << b << ": ";
         std::cout << getStairlinkingvars(b)[0];
         for( int c = 1; c < getNStairlinkingvars(b); ++c )
            std::cout << ", " << getStairlinkingvars(b)[c];
         std::cout << "\n";
      }
      else
         std::cout << "0 stairlinkingvariables in block " << b << std::endl;
   }

   if( getNLinkingvars() != 0 )
   {
      std::cout << "linkingvariable(s): ";
      std::cout << linkingVars[0];
      for( int c = 1; c < getNLinkingvars(); ++c )
         std::cout << ", " << linkingVars[c];
      std::cout << "\n";
   }
   else
      std::cout << "0 linkingvariables" << std::endl;

   if( getNMastervars() != 0 )
   {
      std::cout << "mastervariable(s): ";
      std::cout << masterVars[0];
      for( int c = 1; c < getNMastervars(); ++c )
         std::cout << ", " << masterVars[c];
      std::cout << "\n";
   }
   else
      std::cout << "0 mastervariables" << std::endl;

   if( getNOpenvars() != 0 )
   {
      std::cout << "open variable(s): ";
      std::cout << openVars[0];
      for( int c = 1; c < getNOpenvars(); ++c )
         std::cout << ", " << openVars[c];
      std::cout << "\n";
   }
   else
      std::cout << "0 open variables" << std::endl;

   return SCIP_OKAY;
}

/** fills out the border of a seeed with the hashmap constoblock */
SCIP_RETCODE Seeed::filloutBorderFromConstoblock(SCIP_HASHMAP* constoblock, int givenNBlocks, Seeedpool* seeedpool)
{
   nBlocks = givenNBlocks;
   nVars = seeedpool->getNVars();
   nConss = seeedpool->getNConss();
   int consnum;
   int consblock;
   int varnum;

   for( int i = 0; i < nConss; ++i )
   {
      consnum = i;
      consblock = ((int)(size_t)SCIPhashmapGetImage(constoblock, (void*)(size_t)i)) - 1;
      assert(consblock >= 0 && consblock <= nBlocks);
      if( consblock == nBlocks )
      {
         setConsToMaster(consnum);
      }
      else
      {
         openConss.push_back(consnum);
      }
   }

   for( int i = 0; i < nVars; ++i )
   {
      varnum = i;
      openVars.push_back(varnum);
   }

   nBlocks = 0;
   sort();
   assert(checkConsistency());
   return SCIP_OKAY;
}

/** fills out a seeed with the hashmap constoblock */
SCIP_RETCODE Seeed::filloutSeeedFromConstoblock(SCIP_HASHMAP* constoblock, int givenNBlocks, Seeedpool* seeedpool)
{
   assert(givenNBlocks >= 0);
   nBlocks = givenNBlocks;
   nVars = seeedpool->getNVars();
   nConss = seeedpool->getNConss();
   int consnum;
   int consblock;
   int varnum;
   bool varInBlock;
   std::vector<int> varInBlocks = std::vector<int>(0);
   std::vector<int> emptyVector = std::vector<int>(0);

   for( int c = 0; c < nConss; ++c )
   {
      assert(SCIPhashmapExists(constoblock, (void* ) (size_t ) c));
      assert((int )(size_t )SCIPhashmapGetImage(constoblock, (void* ) (size_t ) c) - 1 <= nBlocks);
      assert((int )(size_t )SCIPhashmapGetImage(constoblock, (void* ) (size_t ) c) - 1 >= 0);
   }

   for( int b = (int)conssForBlocks.size(); b < nBlocks; b++ )
   {
      conssForBlocks.push_back(emptyVector);
   }

   for( int b = (int)varsForBlocks.size(); b < nBlocks; b++ )
   {
      varsForBlocks.push_back(emptyVector);
   }

   for( int b = (int)stairlinkingVars.size(); b < nBlocks; b++ )
   {
      stairlinkingVars.push_back(emptyVector);
   }

   for( int i = 0; i < nConss; ++i )
   {
      consnum = i;
      consblock = ((int)(size_t)SCIPhashmapGetImage(constoblock, (void*)(size_t)i)) - 1;
      assert(consblock >= 0 && consblock <= nBlocks);
      if( consblock == nBlocks )
      {
         setConsToMaster(consnum);
      }
      else
      {
         setConsToBlock(consnum, consblock);
      }
   }

   for( int i = 0; i < nVars; ++i )
   {
      varInBlocks.clear();
      varnum = i;

      /** test if the variable can be found in blocks */
      for( int b = 0; b < nBlocks; ++b )
      {
         varInBlock = false;
         for( size_t k = 0; k < conssForBlocks[b].size() && !varInBlock; ++k )
         {
            for( int l = 0; l < seeedpool->getNVarsForCons(conssForBlocks[b][k]) && !varInBlock; ++l )
            {
               if( varnum == (seeedpool->getVarsForCons(conssForBlocks[b][k]))[l] )
               {
                  varInBlocks.push_back(b);
                  varInBlock = true;
               }
            }
         }
      }
      if( varInBlocks.size() == 1 ) /** if the var can be found in one block set the var to block var */
      {
         setVarToBlock(varnum, varInBlocks[0]);
      }
      else if( varInBlocks.size() == 2 ) /** if the variable can be found in two blocks check if it is a linking var or a stairlinking var*/
      {
         if( varInBlocks[0] + 1 == varInBlocks[1] )
         {
            setVarToStairlinking(varnum, varInBlocks[0], varInBlocks[1]);
         }
         else
         {
            setVarToLinking(varnum);
         }
      }
      else if( varInBlocks.size() > 2 ) /** if the variable can be found in more than two blocks it is a linking var */
      {
         setVarToLinking(varnum);
      }
      else
      {
         assert(varInBlocks.size() == 0);
         setVarToMaster(varnum);
      }
   }
   sort();
   openVars = std::vector<int>(0);
   openConss = std::vector<int>(0);
   openVarsAndConssCalculated = true;

   deleteEmptyBlocks();
   sort();
   assert(checkConsistency());
   assert(checkVarsAndConssConsistency(seeedpool));

   return SCIP_OKAY;
}

/** finds linking-variables that are actually master-variables. I.e. the variable is adjacent to only master-constraints. */
SCIP_RETCODE Seeed::findVarsLinkingToMaster(Seeedpool* seeedpool)
{
   int i;
   int j;
   const int* varcons;
   bool isMasterVar;
   const int* lvars = getLinkingvars();
   std::vector<int> foundMasterVarIndices;

   // sort Master constraints for binary search
   sort();

   for( i = 0; i < getNLinkingvars(); ++i )
   {
      isMasterVar = true;
      varcons = seeedpool->getConssForVar(lvars[i]);
      for( j = 0; j < seeedpool->getNConssForVar(lvars[i]); ++j )
      {
         if( !std::binary_search(masterConss.begin(), masterConss.end(), varcons[j]) )
         {
            isMasterVar = false;
            break;
         }
      }

      if( isMasterVar )
      {
         foundMasterVarIndices.push_back(i);
      }
   }

   for( std::vector<int>::reverse_iterator it = foundMasterVarIndices.rbegin(); it != foundMasterVarIndices.rend(); ++it )
   {
      masterVars.push_back(lvars[*it]);
      linkingVars.erase(linkingVars.begin() + *it);
   }

   return SCIP_OKAY;
}

/** finds linking-variables that are actually stairlinking-variables. I.e. the variable is adjacent to constraints in exactly two block (not adjacent to open cons and master-cons). */
SCIP_RETCODE Seeed::findVarsLinkingToStairlinking(Seeedpool* seeedpool)
{
   int i;
   int j;
   int k;

   int consblock;
   int block1 = -1;
   int block2 = -1;

   const int* varcons;
   const int* lvars = getLinkingvars();

   std::vector<int> foundMasterVarIndices;

   sort();

   for( i = 0; i < getNLinkingvars(); ++i )
   {
      block1 = -1;
      block2 = -1;
      varcons = seeedpool->getConssForVar(lvars[i]);
      for( j = 0; j < seeedpool->getNConssForVar(lvars[i]); ++j )
      {
         consblock = -1;
         for( k = 0; k < nBlocks; ++k )
         {
            if( std::binary_search(conssForBlocks[k].begin(), conssForBlocks[k].end(), varcons[j]) )
            {
               consblock = k;
               break;
            }
         }

         if( consblock == -1 )
         {
            block1 = -1;
            block2 = -1;
            break;
         }
         else if( block1 == consblock || block2 == consblock )
         {
            continue;
         }
         else if( block1 == -1 )
         {
            block1 = consblock;
            continue;
         }
         else if( block2 == -1 )
         {
            block2 = consblock;
            continue;
         }
         else
         {
            block1 = -1;
            block2 = -1;
            break;
         }
      }

      if( block1 != -1 && block2 != -1 )
      {
         setVarToStairlinking(lvars[i], block1, block2);
         foundMasterVarIndices.push_back(i);
      }
   }

   for( std::vector<int>::reverse_iterator it = foundMasterVarIndices.rbegin(); it != foundMasterVarIndices.rend(); ++it )
   {
      linkingVars.erase(linkingVars.begin() + *it);
   }

   return SCIP_OKAY;
}

/** assign all booked constraints and variables and delete them from opencons / openvars */
SCIP_RETCODE Seeed::flushBooked()
{
   std::vector<int>::const_iterator bookedIter;
   std::vector<int>::const_iterator bookedIterEnd;
   std::vector<std::pair<int, int>>::iterator bookedIter2;
   std::vector<std::pair<int, int>>::iterator bookedIterEnd2;


   bookedIter = bookedAsMasterConss.begin();
   bookedIterEnd = bookedAsMasterConss.end();
   for( ; bookedIter != bookedIterEnd; ++bookedIter )
   {
      setConsToMaster(*bookedIter);
      deleteOpencons(*bookedIter);
   }
   bookedAsMasterConss.clear();

   bookedIter2 = bookedAsBlockConss.begin();
   bookedIterEnd2 = bookedAsBlockConss.end();
   for( ; bookedIter2 != bookedIterEnd2; ++bookedIter2 )
   {
      setConsToBlock((*bookedIter2).first, (*bookedIter2).second);
      deleteOpencons((*bookedIter2).first);
   }
   bookedAsBlockConss.clear();

   bookedIter = bookedAsLinkingVars.begin();
   bookedIterEnd = bookedAsLinkingVars.end();
   for( ; bookedIter != bookedIterEnd; ++bookedIter )
   {
      setVarToLinking(*bookedIter);
      deleteOpenvar(*bookedIter);
   }
   bookedAsLinkingVars.clear();

   bookedIter = bookedAsMasterVars.begin();
   bookedIterEnd = bookedAsMasterVars.end();
   for( ; bookedIter != bookedIterEnd; ++bookedIter )
   {
      setVarToMaster(*bookedIter);
      deleteOpenvar(*bookedIter);
   }
   bookedAsMasterVars.clear();

   bookedIter2 = bookedAsBlockVars.begin();
   bookedIterEnd2 = bookedAsBlockVars.end();
   for( ; bookedIter2 != bookedIterEnd2; ++bookedIter2 )
   {
      setVarToBlock((*bookedIter2).first, (*bookedIter2).second);
      deleteOpenvar((*bookedIter2).first);
   }
   bookedAsBlockVars.clear();

   bookedIter2 = bookedAsStairlinkingVars.begin();
   bookedIterEnd2 = bookedAsStairlinkingVars.end();
   for( ; bookedIter2 != bookedIterEnd2; ++bookedIter2 )
   {
      setVarToStairlinking((*bookedIter2).first, (*bookedIter2).second, (*bookedIter2).second + 1);
      deleteOpenvar((*bookedIter2).first);
   }
   bookedAsStairlinkingVars.clear();

   return SCIP_OKAY;
}

/** returns vector containing master conss */
const int* Seeed::getConssForBlock(int block)
{
   return &conssForBlocks[block][0];
}

/** returns the detectorchain */
int* Seeed::getDetectorchain()
{
   return &detectorChain[0];
}

/** returns the calculated has value of this seeed */
long Seeed::getHashValue()
{
   return this->hashvalue;
}

/** returns the id of the seeed */
int Seeed::getID()
{
   return id;
}

/** returns vector containing linking vars */
const int* Seeed::getLinkingvars()
{
   return &linkingVars[0];
}

/** returns vector containing master conss */
const int* Seeed::getMasterconss()
{
   return &masterConss[0];
}

/** returns vector containing master vars (every constraint containing a master var is in master)*/
const int* Seeed::getMastervars()
{
   return &masterVars[0];
}

/** returns number of blocks */
int Seeed::getNBlocks()
{
   return nBlocks;
}

/** get number of conss */
int Seeed::getNConss()
{
   return nConss;
}

/** returns vector containing master conss */
int Seeed::getNConssForBlock(int block)
{
   return (int)conssForBlocks[block].size();
}

/** returns the number of detectors the seeed is propagated by */
int Seeed::getNDetectors()
{
   return (int)detectorChain.size();
}

/** returns size of vector containing linking vars */
int Seeed::getNLinkingvars()
{
   return (int)linkingVars.size();
}

/** returns vector containing master conss */
int Seeed::getNMasterconss()
{
   return (int)masterConss.size();
}

/** returns vector containing master vars (every constraint containing a master var is in master)*/
int Seeed::getNMastervars()
{
   return (int)masterVars.size();
}

/** returns size of vector containing variables not assigned yet */
int Seeed::getNOpenconss()
{
   if( !openVarsAndConssCalculated )
   {
      calcOpenconss();
      calcOpenvars();

      openVarsAndConssCalculated = true;
   }
   return (int)openConss.size();
}

/** returns size of vector containing constraints not assigned yet */
int Seeed::getNOpenvars()
{
   if( !openVarsAndConssCalculated )
   {
      calcOpenconss();
      calcOpenvars();

      openVarsAndConssCalculated = true;
   }
   return (int)openVars.size();
}

/** returns size of vector containing stairlinking vars */
int Seeed::getNStairlinkingvars(int block)
{
   return (int)stairlinkingVars[block].size();
}

/** get number of vars */
int Seeed::getNVars()
{
   return nVars;
}

/** returns size of vector containing vars of a certain block */
int Seeed::getNVarsForBlock(int block)
{
   return (int)varsForBlocks[block].size();
}

/** returns vector containing constraints not assigned yet */
const int* Seeed::getOpenconss()
{
   if( !openVarsAndConssCalculated )
   {
      calcOpenconss();
      calcOpenvars();

      openVarsAndConssCalculated = true;
   }

   return &openConss[0];
}

/** returns vector containing variables not assigned yet*/
const int* Seeed::getOpenvars()
{
   if( !openVarsAndConssCalculated )
   {
      calcOpenconss();
      calcOpenvars();

      openVarsAndConssCalculated = true;
   }

   return &openVars[0];
}

/** returns vector containing stairlinking vars */
const int* Seeed::getStairlinkingvars(int block)
{
   return &stairlinkingVars[block][0];
}

/** returns vector containing vars of a certain block */
const int* Seeed::getVarsForBlock(int block)
{
   return &varsForBlocks[block][0];
}

/** returns whether the cons is a cons of the block */
bool Seeed::isConsBlockconsOfBlock(int cons, int block)
{
   if( find(conssForBlocks[block].begin(), conssForBlocks[block].end(), cons) != conssForBlocks[block].end() )
      return true;
   else
      return false;
}

/** returns whether the cons is a master cons*/
bool Seeed::isConsMastercons(int cons)
{
   if( find(masterConss.begin(), masterConss.end(), cons) != masterConss.end() )
      return true;
   else
      return false;
}

/** return whether the cons is an open conss */
bool Seeed::isConsOpencons(int cons)
{
   if( find(openConss.begin(), openConss.end(), cons) != openConss.end() )
      return true;
   else
      return false;
}

/** returns whether this seeed was propagated by certain detector */
bool Seeed::isPropagatedBy(int detectorID)
{
   assert(propagatedByDetector.size() > detectorID);
   return propagatedByDetector[detectorID];
}

/** return whether the var is a var of the block */
bool Seeed::isVarBlockvarOfBlock(int var, int block)
{
   if( find(varsForBlocks[block].begin(), varsForBlocks[block].end(), var) != varsForBlocks[block].end() )
      return true;
   else
      return false;
}

/** returns whether the var is a master var */
bool Seeed::isVarMastervar(int var)
{
   if( find(masterVars.begin(), masterVars.end(), var) != masterVars.end() )
      return true;
   else
      return false;
}

/** returns whether the var is a linking var */
bool Seeed::isVarLinkingvar(int var)
{
   if( find(linkingVars.begin(), linkingVars.end(), var) != linkingVars.end() )
      return true;
   else
      return false;
}

/** returns whether the var is an open var */
bool Seeed::isVarOpenvar(int var)
{
   if( find(openVars.begin(), openVars.end(), var) != openVars.end() )
      return true;
   else
      return false;
}

/** returns whether the var is a stairlinkingvar of the block */
bool Seeed::isVarStairlinkingvarOfBlock(int var, int block)
{
   if( find(stairlinkingVars[block].begin(), stairlinkingVars[block].end(), var) != stairlinkingVars[block].end() )
      return true;
   else
      return false;
}

/** refine seeed: do obvious (considerImplicits()) and some non-obvious assignments assignOpenPartialHittingToMaster() */
 SCIP_RETCODE Seeed::refineToMaster(
    Seeedpool*       seeedpool
 ){
    SCIP_CALL( considerImplicits(seeedpool) );
    SCIP_CALL( assignOpenPartialHittingToMaster(seeedpool) );

    return SCIP_OKAY;
 }

/** add a constraint to a block */
SCIP_RETCODE Seeed::setConsToBlock(int consToBlock, int block)
{
   assert(conssForBlocks.size() > block);

   conssForBlocks[block].push_back(consToBlock);

   return SCIP_OKAY;
}

/** add a constraint to the master constraints */
SCIP_RETCODE Seeed::setConsToMaster(int consToMaster)
{
   masterConss.push_back(consToMaster);

   return SCIP_OKAY;
}

/** sets seeed to be propagated by detector with detectorID  */
SCIP_RETCODE Seeed::setDetectorPropagated(int detectorID)
{
   assert(propagatedByDetector.size() > detectorID);
   propagatedByDetector[detectorID] = true;
   detectorChain.push_back(detectorID);

   return SCIP_OKAY;
}

/** set number of blocks, atm only increasing number of blocks  */
SCIP_RETCODE Seeed::setNBlocks(int newNBlocks)
{
   assert(newNBlocks >= nBlocks);

   /** increase number of blocks in conssForBlocks and varsForBlocks */
   for( int b = nBlocks; b < newNBlocks; ++b )
   {
      conssForBlocks.push_back(std::vector<int>(0));
      varsForBlocks.push_back(std::vector<int>(0));
      stairlinkingVars.push_back(std::vector<int>(0));
   }

   nBlocks = newNBlocks;

   return SCIP_OKAY;
}

/** sets open vars and conss to be calculated  */
SCIP_RETCODE Seeed::setOpenVarsAndConssCalculated(bool value)
{
   openVarsAndConssCalculated = value;
   return SCIP_OKAY;
}

/** add a variable to a block */
SCIP_RETCODE Seeed::setVarToBlock(int varToBlock, int block)
{
   assert(varsForBlocks.size() > block);

   varsForBlocks[block].push_back(varToBlock);
   return SCIP_OKAY;
}

/** add a variable to the linking variables */
SCIP_RETCODE Seeed::setVarToLinking(int varToLinking)
{
   linkingVars.push_back(varToLinking);
   return SCIP_OKAY;
}

/** add a variable to the master variables (every constraint consisting it is in master ) */
SCIP_RETCODE Seeed::setVarToMaster(int varToMaster)
{
   masterVars.push_back(varToMaster);

   return SCIP_OKAY;
}

/** add a variable to the stair linking variables */
SCIP_RETCODE Seeed::setVarToStairlinking(int varToStairLinking, int block1, int block2)
{
   stairlinkingVars[block1].push_back(varToStairLinking);
   stairlinkingVars[block2].push_back(varToStairLinking);

   return SCIP_OKAY;
}

/** sorts the vars and conss according their numbers */
void Seeed::sort()
{
   for( int b = 0; b < nBlocks; ++b )
   {
      std::sort(varsForBlocks[b].begin(), varsForBlocks[b].end());
      std::sort(stairlinkingVars[b].begin(), stairlinkingVars[b].end());
      std::sort(conssForBlocks[b].begin(), conssForBlocks[b].end());
   }
   std::sort(linkingVars.begin(), linkingVars.end());
   std::sort(masterVars.begin(), masterVars.end());
   std::sort(masterConss.begin(), masterConss.end());
}

} /* namespace gcg */
