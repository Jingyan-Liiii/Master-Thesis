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

/**@file   class_seeedpool.h
 * @brief  class with functions for seeed pool where a seeed is a (potentially incomplete) description of a decomposition (not to confuse with the band from German capital)
 * @author Michael Bastubbe
 * @author Julius Hense
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#ifndef GCG_CLASS_SEEEDPOOL_H__
#define GCG_CLASS_SEEEDPOOL_H__

#include "objscip/objscip.h"
#include <vector>
#include <tr1/unordered_map> //c++ hashmap
#include <unordered_map>
#include <functional>
#include <string>
#include <utility>
#include "gcg.h"

#include "class_seeed.h"
#include "class_consclassifier.h"
#include "class_varclassifier.h"

struct Seeed_Propagation_Data
{
   gcg::Seeedpool* seeedpool;
   gcg::Seeed* seeedToPropagate;
   gcg::Seeed** newSeeeds;
   int nNewSeeeds;
};


namespace gcg{


//typedef boost::shared_ptr<Seeed> SeeedPtr;
typedef Seeed* SeeedPtr;

// Only for pairs of std::hash-able types for simplicity.
// You can of course template this struct to allow other hash functions
struct pair_hash
{
   template<class T1, class T2>
   std::size_t operator()(
      const std::pair<T1, T2> &p) const
   {
      auto h1 = std::hash<T1>{}( p.first );
      auto h2 = std::hash<T2>{}( p.second );

      // overly simple hash combination
      return h1 ^ h2;
   }
};

class Seeedpool
{ /*lint -esym(1712,Seeedpool)*/

private:
   SCIP* scip;                                                 /**< SCIP data structure */
   std::vector<SeeedPtr> incompleteSeeeds;                     /**< vector of incomplete seeeds that can be used for initialization */
   std::vector<SeeedPtr> currSeeeds;                           /**< vector of current (open) seeeds */
   std::vector<SeeedPtr> finishedSeeeds;                       /**< vector of finished seeeds */
   std::vector<SeeedPtr> ancestorseeeds;                       /**< @todo revise comment! collection of all relevant seeeds, allrelevaseeeds[i] contains seeed with id i; non relevant seeeds are repepresented by a null pointer */
   int maxndetectionrounds;                                    /**< maximum number of detection rounds */
   std::vector<std::vector<int>> varsForConss;                 /**< stores for every constraint the indices of variables
                                                                 *< that are contained in the constraint */
   std::vector<std::vector<double>> valsForConss;              /**< stores for every constraint the coefficients of
                                                                 *< variables that are contained in the constraint (i.e.
                                                                 *< have a nonzero coefficient) */
   std::vector<std::vector<int>> conssForVars;                 /**< stores for every variable the indices of constraints
                                                                 *< containing this variable */
   std::vector<SCIP_CONS*> consToScipCons;                     /**< stores the corresponding scip constraints pointer */
   std::vector<SCIP_VAR*> varToScipVar;                        /**< stores the corresponding scip variable pointer */
   std::vector<DEC_DETECTOR*> detectorToScipDetector;          /**< stores the corresponding SCIP detector pinter */
   std::vector<DEC_DETECTOR*> detectorToFinishingScipDetector; /**< stores the corresponding finishing SCIP detector pointer*/
   std::vector<DEC_DETECTOR*> detectorToPostprocessingScipDetector; /**< stores the corresponding postprocessing SCIP detector pointer*/
   std::vector<std::vector<int>> conssadjacencies;
   std::tr1::unordered_map<SCIP_CONS*, int> scipConsToIndex;   /**< maps SCIP_CONS* to the corresponding index */
   std::tr1::unordered_map<SCIP_VAR*, int> scipVarToIndex;     /**< maps SCIP_VAR* to the corresponding index */
   std::tr1::unordered_map<DEC_DETECTOR*, int> scipDetectorToIndex;        /**< maps SCIP_VAR* to the corresponding index */
   std::tr1::unordered_map<DEC_DETECTOR*, int> scipFinishingDetectorToIndex;     /**< maps SCIP_VAR* to the corresponding
                                                                                   *< index */
   std::tr1::unordered_map<DEC_DETECTOR*, int> scipPostprocessingDetectorToIndex;     /**< maps SCIP_VAR* to the corresponding
                                                                                   *< index */

   std::tr1::unordered_map<std::pair<int, int>, SCIP_Real, pair_hash> valsMap;   /**< maps an entry of the matrix to its
                                                                                   *< value, zeros are omitted */

   std::vector<SCIP_VAR*> unpresolvedfixedtozerovars;

   int nVars;                    /**< number of variables */
   int nConss;                   /**< number of constraints */
   int nDetectors;               /**< number of detectors */
   int nFinishingDetectors;      /**< number of finishing detectors */
   int nPostprocessingDetectors; /**< number of postprocessing detectors */
   int nnonzeros;                /**< number of nonzero entries in the coefficient matrix */

//   DEC_DECOMP** decompositions;  /**< decompositions found by the detectors */
//   int ndecompositions;          /**< number of decompositions found by the detectors */

   /** oracle data */
   std::vector<int> usercandidatesnblocks;               /**< candidate for the number of blocks that were given by the user and thus will be handled priorized */
   std::vector<std::pair<int, int>> candidatesNBlocks;   /**< candidate for the number of blocks  */


   SCIP_Bool transformed;                                /**< corresponds the matrix datastructure to the transformed
                                                           *< problem */

   std::vector<SeeedPtr> seeedstopopulate;               /**< seeeds that are translated seeeds from found ones for the
                                                          *< original problem */


public:

   std::vector<ConsClassifier*> consclassescollection;   /**< collection of different constraint class distributions  */
   std::vector<VarClassifier*> varclassescollection;     /**< collection of different variable class distributions   */

   SCIP_Real classificationtime;
   SCIP_Real nblockscandidatescalctime;
   SCIP_Real postprocessingtime;
   SCIP_Real scorecalculatingtime;
   SCIP_Real translatingtime;

   /** constructor */
   Seeedpool(
      SCIP* scip,                /**< SCIP data structure */
      const char* conshdlrName,  /**< name of the conshandler maintaining the seeedpool */
      SCIP_Bool transformed      /**< true if the seeedpool is created for the presolved version of the problem */
      );

   /** destructor */
   ~Seeedpool();

   /** creates constraint and variable classifiers, and deduces block number candidates */
   SCIP_RETCODE calcClassifierAndNBlockCandidates(
      SCIP* givenScip /**< SCIP data structure */
      );

   /** constructs seeeds using the registered detectors
    *  @return user has to free seeeds */
   std::vector<SeeedPtr> findSeeeds();

   /* sorts seeeds in finished seeeds data structure according to their score */
   void sortFinishedForScore();

   /** method to complete a set of incomplete seeeds with the help of all included detectors that implement a finishing method
    *  @return set of completed decomposition */
   std::vector<SeeedPtr> finishIncompleteSeeeds(
      std::vector<SeeedPtr> incompleteseeeds /**< the set of incompleted seeeds */
      );

   /** calls findSeeeds method and translates the resulting seeeds into decompositions */
   void findDecompositions();

   /** returns seeed with the corresponding id */
   gcg::Seeed* findFinishedSeeedByID(
      int      seeedid
      );

   /** adds a seeed to ancestor seeeds */
   void addSeeedToAncestor(
      SeeedPtr seeed
      );

   /** adds a seeed to current seeeds */
   void addSeeedToCurr(
      SeeedPtr seeed
      );

   /** adds a seeed to finished seeeds */
   void addSeeedToFinished(
      SeeedPtr seeed,
      SCIP_Bool* success
      );

   /** adds a seeed to finished seeeds without checking for duplicates, dev has to check this on his own*/
   void addSeeedToFinishedUnchecked(
      SeeedPtr seeed
      );

   /** adds a seeed to incomplete seeeds */
   void addSeeedToIncomplete(
      SeeedPtr seeed,
      SCIP_Bool* success
      );

   SCIP_Bool areThereContinuousVars();

   /** clears ancestor seeed data structure */
   void clearAncestorSeeeds();

   /** clears current seeed data structure */
   void clearCurrentSeeeds();

   /** clears finished seeed data structure */
   void clearFinishedSeeeds();

   /** clears incomplete seeed data structure */
   void clearIncompleteSeeeds();

   /** returns a seeed from ancestor seeed data structure */
   SeeedPtr getAncestorSeeed(
      int seeedindex /**< index of seeed in ancestor seeed data structure */
      );

   /** returns a seeed from current (open) seeed data structure */
   SeeedPtr getCurrentSeeed(
      int seeedindex /**< index of seeed in current (open) seeed data structure */
      );

   /** returns a seeed from finished seeed data structure */
   SeeedPtr getFinishedSeeed(
      int seeedindex /**< index of seeed in finished seeed data structure */
      );

   /** returns a seeed from incomplete seeed data structure */
   SeeedPtr getIncompleteSeeed(
      int seeedindex /**< index of seeed in incomplete seeed data structure */
      );

   /** returns size of ancestor seeed data structure */
   int getNAncestorSeeeds();

   /** returns size of current (open) seeed data structure */
   int getNCurrentSeeeds();

   /** returns size of finished seeed data structure */
   int getNFinishedSeeeds();

   /** returns size of incomplete seeed data structure */
   int getNIncompleteSeeeds();

   /** returns true if the given seeed is a duplicate of a seeed that is already contained in
    *  finished seeeds or current seeeds data structure */
   bool hasDuplicate(
      SeeedPtr seeed
      );

   /** translates seeeds and classifiers if the index structure of the problem has changed, e.g. due to presolving */
   void translateSeeedData(
      Seeedpool* otherpool,                              /**< old seeedpool */
      std::vector<Seeed*> otherseeeds,                   /**< seeeds to be translated */
      std::vector<Seeed*>& newseeeds,                    /**< translated seeeds (pass empty vector) */
      std::vector<ConsClassifier*> otherconsclassifiers, /**< consclassifiers to be translated */
      std::vector<ConsClassifier*>& newconsclassifiers,  /**< translated consclassifiers (pass empty vector) */
      std::vector<VarClassifier*> othervarclassifiers,   /**< varclassifiers to be translated */
      std::vector<VarClassifier*>& newvarclassifiers     /**< translated varclassifiers (pass empty vector) */
      );

   /** translates seeeds if the index structure of the problem has changed, e.g. due to presolving */
   void translateSeeeds(
      Seeedpool* otherpool,            /**< old seeedpool */
      std::vector<Seeed*> otherseeeds, /**< seeeds to be translated */
      std::vector<Seeed*>& newseeeds   /**< translated seeeds (pass empty vector) */
      );

   /** registers translated seeeds from the original problem */
   void populate(
      std::vector<SeeedPtr> seeeds
      );

   /** sorts the seeed and calculates its implicit assignments, hashvalue and evaluation */
   SCIP_RETCODE prepareSeeed(
      SeeedPtr seeed
      );

   /** @todo revise comment and probably rename! sorts seeeds in allrelevantseeeds data structure by ascending id */
   void sortAllRelevantSeeeds();


   bool isConsCardinalityCons(
         int  consindexd
         );

 /** is cons with specified indec partitioning packing, or covering constraint?*/
   bool isConsSetppc(
      int  consindexd
      );

   /** is cons with specified indec partitioning, or packing covering constraint?*/
   bool isConsSetpp(
      int  consindexd
      );



   /** returns the variable indices of the coefficient matrix for a constraint */
   const int* getVarsForCons(
      int consIndex /**< index of the constraint to be considered */
      );

   /** returns the coefficients of the coefficient matrix for a constraint */
   const SCIP_Real* getValsForCons(
      int consIndex /**< index of the constraint to be considered */
      );

   /** returns the constraint indices of the coefficient matrix for a variable */
   const int* getConssForVar(
      int varIndex /**< index of the variable to be considered */
      );

   /** returns the number of variables for a given constraint */
   int getNVarsForCons(
      int consIndex /**< index of the constraint to be considered */
      );

   /** returns the number of constraints for a given variable */
   int getNConssForVar(
      int varIndex /**< index of the variable to be considered */
      );

   const int* getConssForCons(
      int consIndex /**< index of the constraint to be considered */
      );

   /** returns the number of constraints for a given constraint */
   int getNConssForCons(
      int consIndex /**< index of the constraint to be considered */
      );

   /** returns the SCIP variable related to a variable index */
   SCIP_VAR* getVarForIndex(
      int varIndex /**< index of the variable to be considered */
      );

   /** returns the SCIP constraint related to a constraint index */
   SCIP_CONS* getConsForIndex(
      int consIndex /**< index of the constraint to be considered */
      );

   /** returns the detector related to a detector index */
   DEC_DETECTOR* getDetectorForIndex(
      int detectorIndex /**< index of the detector to be considered */
      );

   /** returns the detector related to a finishing detector index */
   DEC_DETECTOR* getFinishingDetectorForIndex(
      int detectorIndex /**< index of the finishing detector to be considered */
      );

   /** returns the detector related to a finishing detector index */
   DEC_DETECTOR* getPostprocessingDetectorForIndex(
      int detectorIndex /**< index of the postprocessing detector to be considered */
      );


   /** returns a coefficient from the coefficient matrix */
   SCIP_Real getVal(
      int row, /**< index of the constraint to be considered */
      int col  /**< index of the variable to be considered */
      );

   /** returns the variable index related to a SCIP variable */
   int getIndexForVar(
      SCIP_VAR* var
      );

   /** returns the constraint index related to a SCIP constraint */
   int getIndexForCons(
      SCIP_CONS* cons
      );

   /** returns the detector index related to a detector */
   int getIndexForDetector(
      DEC_DETECTOR* detector
      );

   /** returns the finishing detector index related to a detector */
   int getIndexForFinishingDetector(
      DEC_DETECTOR* detector
      );

   /** returns the postprocessing detector index related to a detector */
   int getIndexForPostprocessingDetector(
      DEC_DETECTOR* detector
      );


   /** returns a new unique id for a seeed */
   int getNewIdForSeeed();

   /** returns the number of detectors used in the seeedpool */
   int getNDetectors();

   /** returns the number of nonzero entries in the coefficient matrix */
   int getNNonzeros();

   /** returns the number of finishing detectors used in the seeedpool */
   int getNFinishingDetectors();

   /** returns the number of postprocessing detectors used in the seeedpool */
   int getNPostprocessingDetectors();

   /** returns the number of variables considered in the seeedpool */
   int getNVars();

   /** returns the number of constraints considered in the seeedpool */
   int getNConss();

   /* returns associated scip */
   SCIP* getScip();


   /** returns the candidates for block size sorted in descending order by how often a candidate was added */
   std::vector<int> getSortedCandidatesNBlocks();

   /** returns the candidates for block size sorted in descending order by how often a candidate was added with nvotes information*/
    std::vector<std::pair<int, int>> getSortedCandidatesNBlocksFull();


   /** adds a candidate for block size and counts how often a candidate is added */
   void addCandidatesNBlocks(
      int candidate /**< candidate for block size */
      );

   /** adds a candidate for block size and counts how often a candidate is added */
   void addCandidatesNBlocksNVotes(
      int candidate, /**< candidate for block size */
      int nvotes     /**< number of votes this candidates will get */
      );


   /** adds a candidate for block size given by the user */
   void addUserCandidatesNBlocks(
      int candidate /**< candidate for block size */
      );

   /** returns number of user-given block size candidates */
   int getNUserCandidatesNBlocks();

   /** calculates and adds block size candidates using constraint classifications and variable classifications */
   void calcCandidatesNBlocks();

   /** adds a constraint classifier if it is no duplicate of an existing constraint classifier */
   void addConsClassifier(
      ConsClassifier* classifier /**< consclassifier to be added */
      );

   /** returns a new constraint classifier
    *  where all constraints with identical SCIP constype are assigned to the same class */
   ConsClassifier* createConsClassifierForSCIPConstypes();

   /** returns a new constraint classifier
    *  where all constraints with identical Miplib constype are assigned to the same class */
   ConsClassifier* createConsClassifierForMiplibConstypes();


   /** returns a new constraint classifier
    *  where all constraints with identical consname (ignoring digits) are assigned to the same class */
   ConsClassifier* createConsClassifierForConsnamesDigitFreeIdentical();

   /** returns a new constraint classifier
    *  where all constraints whose consnames do not a have levenshtein distance to each other
    *  higher than a given connectivity are assigned to the same class */
   ConsClassifier* createConsClassifierForConsnamesLevenshteinDistanceConnectivity(
      int connectivity /**< given connectivity */
      );

   /** returns a new constraint classifier
    *  where all constraints with identical number of nonzero coefficients are assigned to the same class */
   ConsClassifier* createConsClassifierForNNonzeros();

   /** returns pointer to a constraint classifier */
   ConsClassifier* getConsClassifier(
      int classifierIndex /**< index of constraint classifier */
      );

   /** returns the assignment of constraints to classes of a classifier as integer array */
   int* getConsClassifierArray(
      int classifierIndex /**< index of constraint classifier */
      );

   /** returns number of different constraint classifiers */
   int getNConsClassifiers();

   /** adds constraint classifiers with a reduced number of classes */
   void reduceConsclasses();

   /** adds a variable classifier if it is no duplicate of an existing variable classifier */
   void addVarClassifier(
      VarClassifier* classifier /**< varclassifier to be added */
      );

   /** returns a new variable classifier
    *  where all variables with identical objective function value are assigned to the same class */
   VarClassifier* createVarClassifierForObjValues();

   /** returns a new variable classifier
    *  where all variables are assigned to class zero, positive or negative according to their objective function value sign
    *  all class zero variables are assumed to be only master variables (set via DECOMPINFO)
    *  @todo correct? */
   VarClassifier* createVarClassifierForObjValueSigns();

   /** returns a new variable classifier
    *  where all variables with identical SCIP vartype are assigned to the same class */
   VarClassifier* createVarClassifierForSCIPVartypes();

   /** returns number of different variable classifiers */
   int getNVarClassifiers();

   /** returns pointer to a variable classifier */
   VarClassifier* getVarClassifier(
      int classifierIndex /**< index of variable classifier */
      );

   /** returns the assignment of variables to classes of a classifier as integer array */
   int* getVarClassifierArray(
      int classifierIndex /**< index of constraint classifier */
      );

   /** adds variable classifiers with a reduced number of classes */
   void reduceVarclasses();

   /** returns a vector of seeeds where all seeeds of given seeeds having only one block are removed
    *  except for the two seeeds with the lowest numbers of masterconss */
   std::vector<SeeedPtr> removeSomeOneblockDecomps(
      std::vector<SeeedPtr> givenseeeds
      );

   /** creates a decomposition for a given seeed */
   SCIP_RETCODE createDecompFromSeeed(
      SeeedPtr seeed,         /** seeed the decomposition is created for */
      DEC_DECOMP** newdecomp  /** the new decomp created from the seeed */
      );

   /** creates a seeed for a given decomposition
    *  the resulting seeed will not have a detectorchaininfo or any ancestor or finishing detector data
    *  only use this method if the seeedpool is for the transformed problem
    *  the resulting seeed may only be added to the seeedpool for the presolved problem */
   SCIP_RETCODE createSeeedFromDecomp(
      DEC_DECOMP* decomp, /**< decomposition the seeed is created for */
      SeeedPtr* newseeed /**< the new seeed created from the decomp */
      );

   /** returns true if the matrix structure corresponds to the transformed problem */
   SCIP_Bool getTransformedInfo();

   SCIP_RETCODE printBlockcandidateInformation(
    SCIP*                 scip,               /**< SCIP data structure */
    FILE*                 file                /**< output file or NULL for standard output */
   );

   SCIP_RETCODE printClassifierInformation(
    SCIP*                 scip,               /**< SCIP data structure */
    FILE*                 file                /**< output file or NULL for standard output */
   );


private:

   /** calculates necessary data for translating seeeds and classifiers */
   void calcTranslationMapping(
      Seeedpool* origpool, /** original seeedpool */
      std::vector<int>& rowothertothis,   /** constraint index mapping from old to new seeedpool */
      std::vector<int>& rowthistoother,   /** constraint index mapping new to old seeedpool */
      std::vector<int>& colothertothis,   /** variable index mapping from old to new seeedpool */
      std::vector<int>& colthistoother,   /** variable index mapping from new to old seeedpool */
      std::vector<int>& missingrowinthis  /** missing constraint indices in new seeedpool */
      );

   /** returns translated Seeeds derived from given mapping data */
   std::vector<Seeed*> getTranslatedSeeeds(
      std::vector<Seeed*>& otherseeeds,   /**< seeeds to be translated */
      std::vector<int>& rowothertothis,   /** constraint index mapping from old to new seeedpool */
      std::vector<int>& rowthistoother,   /** constraint index mapping new to old seeedpool */
      std::vector<int>& colothertothis,   /** variable index mapping from old to new seeedpool */
      std::vector<int>& colthistoother    /** variable index mapping from new to old seeedpool */
      );

   /** returns translated ConsClassifiers derived from given mapping data */
   std::vector<ConsClassifier*> getTranslatedConsClassifiers(
      std::vector<ConsClassifier*>& otherclassifiers, /**< consclassifiers to be translated */
      std::vector<int>& rowothertothis,   /** constraint index mapping from old to new seeedpool */
      std::vector<int>& rowthistoother    /** constraint index mapping new to old seeedpool */
      );

   /** returns translated VarClassifiers derived from given mapping data */
   std::vector<VarClassifier*> getTranslatedVarClassifiers(
      std::vector<VarClassifier*>& otherclassifiers, /**< varclassifiers to be translated */
      std::vector<int>& colothertothis,   /** variable index mapping from old to new seeedpool */
      std::vector<int>& colthistoother    /** variable index mapping from new to old seeedpool */
      );



};
/* class Seeedpool */



} /* namespace gcg */
#endif /* GCG_CLASS_SEEEDPOOL_H__ */
