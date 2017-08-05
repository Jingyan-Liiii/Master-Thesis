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

/**@file   dec_varclass.cpp
 * @ingroup DETECTORS
 * @brief  detector varclass
 * @author Julius Hense
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#include "dec_varclass.h"
#include "cons_decomp.h"
#include "class_seeed.h"
#include "class_seeedpool.h"
#include "class_varclassifier.h"
#include "gcg.h"
#include "scip/cons_setppc.h"
#include "scip/scip.h"
#include "scip_misc.h"
#include "scip/clock.h"

#include <sstream>

#include <iostream>
#include <algorithm>

/* constraint handler properties */
#define DEC_DETECTORNAME          "varclass"       /**< name of detector */
#define DEC_DESC                  "detector varclass" /**< description of detector*/
#define DEC_FREQCALLROUND         1           /** frequency the detector gets called in detection loop ,ie it is called in round r if and only if minCallRound <= r <= maxCallRound AND  (r - minCallRound) mod freqCallRound == 0 */
#define DEC_MAXCALLROUND          0           /** last round the detector gets called                              */
#define DEC_MINCALLROUND          0           /** first round the detector gets called                              */
#define DEC_FREQCALLROUNDORIGINAL 1           /** frequency the detector gets called in detection loop while detecting the original problem   */
#define DEC_MAXCALLROUNDORIGINAL  INT_MAX     /** last round the detector gets called while detecting the original problem                            */
#define DEC_MINCALLROUNDORIGINAL  0           /** first round the detector gets called while detecting the original problem    */
#define DEC_PRIORITY              0           /**< priority of the constraint handler for separation */
#define DEC_DECCHAR               'v'         /**< display character of detector */
#define DEC_ENABLED               TRUE        /**< should the detection be enabled */
#define DEC_ENABLEDORIGINAL       TRUE        /**< should the detection of the original problem be enabled */
#define DEC_ENABLEDFINISHING      FALSE        /**< should the finishing be enabled */
#define DEC_SKIP                  FALSE       /**< should detector be skipped if other detectors found decompositions */
#define DEC_USEFULRECALL          FALSE       /**< is it useful to call this detector on a descendant of the propagated seeed */
#define DEC_LEGACYMODE            FALSE       /**< should (old) DETECTSTRUCTURE method also be used for detection */

#define DEFAULT_MAXIMUMNCLASSES     8
#define AGGRESSIVE_MAXIMUMNCLASSES  10
#define FAST_MAXIMUMNCLASSES        6

#define SET_MULTIPLEFORSIZETRANSF   12500

/*
 * Data structures
 */

/** @todo fill in the necessary detector data */

/** detector handler data */
struct DEC_DetectorData
{
};

/*
 * Local methods
 */

/* put your local methods here, and declare them static */

/*
 * detector callback methods
 */

/** destructor of detector to free user data (called when GCG is exiting) */
#define freeVarclass NULL

/** destructor of detector to free detector data (called before the solving process begins) */
#if 0
static
DEC_DECL_EXITDETECTOR(exitVarclass)
{ /*lint --e{715}*/

   SCIPerrorMessage("Exit function of detector <%s> not implemented!\n", DEC_DETECTORNAME);
   SCIPABORT();

   return SCIP_OKAY;
}
#else
#define exitVarclass NULL
#endif

/** detection initialization function of detector (called before solving is about to begin) */
#if 0
static
DEC_DECL_INITDETECTOR(initConsclass)
{ /*lint --e{715}*/

   SCIPerrorMessage("Init function of detector <%s> not implemented!\n", DEC_DETECTORNAME);
   SCIPABORT();

   return SCIP_OKAY;
}
#else
#define initVarclass NULL
#endif

/** detection function of detector */
//static DEC_DECL_DETECTSTRUCTURE(detectVarclass)
//{ /*lint --e{715}*/
//   *result = SCIP_DIDNOTFIND;
//
//   SCIPerrorMessage("Detection function of detector <%s> not implemented!\n", DEC_DETECTORNAME)
//;   SCIPABORT(); /*lint --e{527}*/
//
//   return SCIP_OKAY;
//}

#define detectVarclass NULL

#define finishSeeedVarclass NULL

static DEC_DECL_PROPAGATESEEED(propagateSeeedVarclass)
{
  *result = SCIP_DIDNOTFIND;
  char decinfo[SCIP_MAXSTRLEN];

  SCIP_CLOCK* temporaryClock;

  if (seeedPropagationData->seeedToPropagate->getNOpenconss() != seeedPropagationData->seeedpool->getNConss() ||  seeedPropagationData->seeedToPropagate->getNOpenvars() != seeedPropagationData->seeedpool->getNVars() )
  {
    *result = SCIP_SUCCESS;
     return SCIP_OKAY;
  }

  SCIP_CALL_ABORT( SCIPcreateClock(scip, &temporaryClock) );
  SCIP_CALL_ABORT( SCIPstartClock(scip, temporaryClock) );

  std::vector<gcg::Seeed*> foundseeeds(0);

  gcg::Seeed* seeedOrig;
  gcg::Seeed* seeed;

  int maximumnclasses;

  SCIPgetIntParam(scip, "detectors/varclass/maxnclasses", &maximumnclasses); /* if  distribution of classes exceed this number its skipped */

  for( int classifierIndex = 0; classifierIndex < seeedPropagationData->seeedpool->getNVarClassifiers(); ++classifierIndex )
  {
    gcg::VarClassifier* classifier = seeedPropagationData->seeedpool->getVarClassifier( classifierIndex );
    std::vector<int> varclassindices_master = std::vector<int>(0);
    std::vector<int> varclassindices_linking = std::vector<int>(0);

    if ( classifier->getNClasses() > maximumnclasses )
    {
       std::cout << " the current varclass distribution includes " <<  classifier->getNClasses() << " classes but only " << maximumnclasses << " are allowed for propagateSeeed() of var class detector" << std::endl;
       continue;
    }

    seeedOrig = seeedPropagationData->seeedToPropagate;

    for( int i = 0; i < classifier->getNClasses(); ++ i )
    {
       switch( classifier->getClassDecompInfo( i ) )
       {
          case gcg::ALL:
             break;
          case gcg::LINKING:
             varclassindices_linking.push_back( i );
             break;
          case gcg::MASTER:
             varclassindices_master.push_back( i );
             break;
          case gcg::BLOCK:
             break;
       }
    }

    std::vector< std::vector<int> > subsetsOfVarclasses = classifier->getAllSubsets( true, false, false, false );

    for( size_t subset = 0; subset < subsetsOfVarclasses.size(); ++subset )
    {
       if( subsetsOfVarclasses[subset].size() == 0 && varclassindices_master.size() == 0 && varclassindices_linking.size() == 0 )
          continue;

       seeed = new gcg::Seeed(seeedOrig);

       /** book open vars that have a) type of the current subset or b) decomp info LINKING as linking vars */
       for( int i = 0; i < seeed->getNOpenvars(); ++i )
       {
          bool foundVar = false;
          for( size_t varclassId = 0; varclassId < subsetsOfVarclasses[subset].size(); ++varclassId )
          {
              if( classifier->getClassOfVar( seeed->getOpenvars()[i] ) == subsetsOfVarclasses[subset][varclassId] )
              {
                  seeed->bookAsLinkingVar(seeed->getOpenvars()[i]);
                  foundVar = true;
                  break;
              }
          }
          /** only check varclassindices_linking if current var has not already been found in a subset */
          if ( !foundVar )
          {
             for( size_t varclassId = 0; varclassId < varclassindices_linking.size(); ++varclassId )
             {
                if( classifier->getClassOfVar( seeed->getOpenvars()[i] ) == varclassindices_linking[varclassId] )
                {
                   seeed->bookAsLinkingVar(seeed->getOpenvars()[i]);
                   foundVar = true;
                   break;
                }
             }
          }
          /** only check varclassindices_master if current var has not already been found in a subset */
          if ( !foundVar )
          {
             for( size_t varclassId = 0; varclassId < varclassindices_master.size(); ++varclassId )
             {
                if( classifier->getClassOfVar( seeed->getOpenvars()[i] ) == varclassindices_master[varclassId] )
                {
                   seeed->bookAsMasterVar(seeed->getOpenvars()[i]);
                   break;
                }
             }
          }
       }

       /** set decinfo to: varclass_<classfier_name>:<linking_class_name#1>-...-<linking_class_name#n> */
       std::stringstream decdesc;
       decdesc << "varclass" << "\\_" << classifier->getName() << ": \\\\ ";
       std::vector<int> curlinkingclasses( varclassindices_linking );
       for ( size_t varclassId = 0; varclassId < subsetsOfVarclasses[subset].size(); ++varclassId )
       {
          if ( varclassId > 0 )
          {
             decdesc << "-";
          }
          decdesc << classifier->getClassName( subsetsOfVarclasses[subset][varclassId] );

          if( std::find( varclassindices_linking.begin(), varclassindices_linking.end(),
             subsetsOfVarclasses[subset][varclassId] ) == varclassindices_linking.end() )
          {
             curlinkingclasses.push_back( subsetsOfVarclasses[subset][varclassId] );
          }
       }
       for ( size_t varclassId = 0; varclassId < varclassindices_linking.size(); ++varclassId )
       {
          if ( varclassId > 0 || subsetsOfVarclasses[subset].size() > 0)
          {
             decdesc << "-";
          }
          decdesc << classifier->getClassName( varclassindices_linking[varclassId] );
       }

       seeed->flushBooked();
       (void) SCIPsnprintf(decinfo, SCIP_MAXSTRLEN, decdesc.str().c_str());
       seeed->addDetectorChainInfo(decinfo);
       seeed->setDetectorPropagated(detector);
       seeed->setVarClassifierStatistics( seeed->getNDetectors() - 1, classifierIndex, curlinkingclasses,
          varclassindices_master );

       foundseeeds.push_back(seeed);
    }
  }

  SCIP_CALL_ABORT( SCIPstopClock(scip, temporaryClock ) );

  SCIP_CALL( SCIPallocMemoryArray(scip, &(seeedPropagationData->newSeeeds), foundseeeds.size() ) );
  seeedPropagationData->nNewSeeeds = foundseeeds.size();

  for( int s = 0; s < seeedPropagationData->nNewSeeeds; ++s )
  {
     seeedPropagationData->newSeeeds[s] = foundseeeds[s];
     seeedPropagationData->newSeeeds[s]->addClockTime(SCIPclockGetTime(temporaryClock )  );
  }

  SCIP_CALL_ABORT(SCIPfreeClock(scip, &temporaryClock) );

  *result = SCIP_SUCCESS;

   return SCIP_OKAY;
}

static
DEC_DECL_SETPARAMAGGRESSIVE(setParamAggressiveVarclass)
{
   char setstr[SCIP_MAXSTRLEN];
   SCIP_Real modifier;

   int newval;
   const char* name = DECdetectorGetName(detector);

   (void) SCIPsnprintf(setstr, SCIP_MAXSTRLEN, "detectors/%s/enabled", name);
   SCIP_CALL( SCIPsetBoolParam(scip, setstr, TRUE) );

   (void) SCIPsnprintf(setstr, SCIP_MAXSTRLEN, "detectors/%s/origenabled", name);
   SCIP_CALL( SCIPsetBoolParam(scip, setstr, TRUE) );

   (void) SCIPsnprintf(setstr, SCIP_MAXSTRLEN, "detectors/%s/finishingenabled", name);
   SCIP_CALL( SCIPsetBoolParam(scip, setstr, FALSE ) );


   modifier = ((SCIP_Real)SCIPgetNConss(scip) + (SCIP_Real)SCIPgetNVars(scip) ) / SET_MULTIPLEFORSIZETRANSF;
   modifier = log(modifier) / log(2.);

   if (!SCIPisFeasPositive(scip, modifier) )
      modifier = -1.;

   modifier = SCIPfloor(scip, modifier);

   newval = MAX( 2, AGGRESSIVE_MAXIMUMNCLASSES - modifier );
   (void) SCIPsnprintf(setstr, SCIP_MAXSTRLEN, "detectors/%s/maxnclasses", name);

   SCIP_CALL( SCIPsetIntParam(scip, setstr, newval ) );
   SCIPinfoMessage(scip, NULL, "\n%s = %d\n", setstr, newval);


   return SCIP_OKAY;

}


static
DEC_DECL_SETPARAMDEFAULT(setParamDefaultVarclass)
{
   char setstr[SCIP_MAXSTRLEN];
   SCIP_Real modifier;

   int newval;
   const char* name = DECdetectorGetName(detector);

   (void) SCIPsnprintf(setstr, SCIP_MAXSTRLEN, "detectors/%s/enabled", name);
   SCIP_CALL( SCIPsetBoolParam(scip, setstr, TRUE) );

   (void) SCIPsnprintf(setstr, SCIP_MAXSTRLEN, "detectors/%s/origenabled", name);
   SCIP_CALL( SCIPsetBoolParam(scip, setstr, TRUE ) );

   (void) SCIPsnprintf(setstr, SCIP_MAXSTRLEN, "detectors/%s/finishingenabled", name);
   SCIP_CALL( SCIPsetBoolParam(scip, setstr, FALSE ) );

   modifier = ( (SCIP_Real)SCIPgetNConss(scip) + (SCIP_Real)SCIPgetNVars(scip) ) / SET_MULTIPLEFORSIZETRANSF;
   modifier = log(modifier) / log(2);

   if (!SCIPisFeasPositive(scip, modifier) )
      modifier = -1.;

   modifier = SCIPfloor(scip, modifier);

   newval = MAX( 2, DEFAULT_MAXIMUMNCLASSES - modifier );
   (void) SCIPsnprintf(setstr, SCIP_MAXSTRLEN, "detectors/%s/maxnclasses", name);

   SCIP_CALL( SCIPsetIntParam(scip, setstr, newval ) );
   SCIPinfoMessage(scip, NULL, "\n%s = %d\n", setstr, newval);

   return SCIP_OKAY;

}

static
DEC_DECL_SETPARAMFAST(setParamFastVarclass)
{
   char setstr[SCIP_MAXSTRLEN];
   SCIP_Real modifier;
   int newval;

   const char* name = DECdetectorGetName(detector);

   (void) SCIPsnprintf(setstr, SCIP_MAXSTRLEN, "detectors/%s/enabled", name);
   SCIP_CALL( SCIPsetBoolParam(scip, setstr, TRUE) );

   (void) SCIPsnprintf(setstr, SCIP_MAXSTRLEN, "detectors/%s/origenabled", name);
   SCIP_CALL( SCIPsetBoolParam(scip, setstr, FALSE) );

   (void) SCIPsnprintf(setstr, SCIP_MAXSTRLEN, "detectors/%s/finishingenabled", name);
   SCIP_CALL( SCIPsetBoolParam(scip, setstr, FALSE ) );

   modifier = ( (SCIP_Real)SCIPgetNConss(scip) + (SCIP_Real)SCIPgetNVars(scip) ) / SET_MULTIPLEFORSIZETRANSF;

   modifier = log(modifier) / log(2);

   if (!SCIPisFeasPositive(scip, modifier) )
      modifier = -1.;

   modifier = SCIPfloor(scip, modifier);

   (void) SCIPsnprintf(setstr, SCIP_MAXSTRLEN, "detectors/%s/maxnclasses", name);

   newval = MAX( 2, FAST_MAXIMUMNCLASSES - modifier );

   SCIP_CALL( SCIPsetIntParam(scip, setstr, newval ) );
   SCIPinfoMessage(scip, NULL, "\n%s = %d\n", setstr, newval);

   return SCIP_OKAY;

}



/*
 * detector specific interface methods
 */

/** creates the handler for varclass detector and includes it in SCIP */
SCIP_RETCODE SCIPincludeDetectorVarclass(SCIP* scip /**< SCIP data structure */
)
{
   DEC_DETECTORDATA* detectordata;
   char setstr[SCIP_MAXSTRLEN];

   /**@todo create varclass detector data here*/
   detectordata = NULL;

   SCIP_CALL(
      DECincludeDetector(scip, DEC_DETECTORNAME, DEC_DECCHAR, DEC_DESC, DEC_FREQCALLROUND, DEC_MAXCALLROUND,
         DEC_MINCALLROUND, DEC_FREQCALLROUNDORIGINAL, DEC_MAXCALLROUNDORIGINAL, DEC_MINCALLROUNDORIGINAL, DEC_PRIORITY, DEC_ENABLED, DEC_ENABLEDORIGINAL, DEC_ENABLEDFINISHING, DEC_SKIP, DEC_USEFULRECALL, DEC_LEGACYMODE, detectordata, detectVarclass,
         freeVarclass, initVarclass, exitVarclass, propagateSeeedVarclass, finishSeeedVarclass, setParamAggressiveVarclass, setParamDefaultVarclass, setParamFastVarclass));

   /**@todo add varclass detector parameters */

   const char* name = DEC_DETECTORNAME;
   (void) SCIPsnprintf(setstr, SCIP_MAXSTRLEN, "detectors/%s/maxnclasses", name);
   SCIP_CALL( SCIPaddIntParam(scip, setstr, "maximum number of classes ",  NULL, FALSE, DEFAULT_MAXIMUMNCLASSES, 1, INT_MAX, NULL, NULL ) );

   return SCIP_OKAY;
}
