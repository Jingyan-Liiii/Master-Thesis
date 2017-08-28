/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                           */
/*                  This file is part of the program                         */
/*          GCG --- Generic Column Generation                                */
/*                  a Dantzig-Wolfe decomposition based extension            */
/*                  of the branch-cut-and-price framework                    */
/*         SCIP --- Solving Constraint Integer Programs                      */
/*                                                                           */
/* Copyright (C) 2010-2017 Operations Research, RWTH Aachen University       */
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

/**@file   class_miscvisualization.cpp
 * @brief  miscellaneous methods for visualizations
 * @author Hanna Franzen
 *
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#include "class_miscvisualization.h"
#include "class_seeedpool.h"
#include "class_seeed.h"

#include "scip/scip.h"

#include <unistd.h>

namespace gcg {

/** constructor */
MiscVisualization::MiscVisualization(){

}

/** destructor */
MiscVisualization::~MiscVisualization(){

}

/** gives a consistent filename for a (single) seeed visualization that includes the probname and seeedID
 *
 * @return filename including the extension
 * */
char* MiscVisualization::GCGgetVisualizationFilename(
   SCIP* scip,             /**< scip data structure */
   SeeedPtr seeed,         /**< seeed that is to be visualized */
   const char* extension   /**< file extension */
   )
{
   char* name;
   char* detectorchainstring;
   char probname[SCIP_MAXSTRLEN];
   char* outname;

   (void) SCIPsnprintf(probname, SCIP_MAXSTRLEN, "%s", SCIPgetProbName(scip));
   SCIPsplitFilename(probname, NULL, &name, NULL, NULL);

   /* get detector chain string*/
   detectorchainstring = seeed->getDetectorChainString();

   /* print header */
   if( seeed == NULL )
      (void) SCIPsnprintf(outname, SCIP_MAXSTRLEN, "%s", name);
   else
   {
      (void) SCIPsnprintf(outname, SCIP_MAXSTRLEN, "%s-%s-%d-%d-%s", name, detectorchainstring, seeed->getID(),
         seeed->getNBlocks(), extension);
   }

   return outname;
}

/** gives the path of the file */
char* MiscVisualization::GCGgetFilePath(
   SCIP* scip,       /**< scip data structure */
   FILE* file        /**< file */
   )
{
   char* pfile;
   char sympath[SCIP_MAXSTRLEN];
   int filedesc;
   int success;

   filedesc = fileno(file); /* get link to file descriptor */
   if( filedesc < 0 )
   {
      /*@todo error or similar*/
   }
   snprintf(sympath, SCIP_MAXSTRLEN, "/proc/self/fd/%d", filedesc); /* set symbolic link to file */
   success = readlink(sympath, pfile, SCIP_MAXSTRLEN); /* get actual path including extension */
   if( success < 0 )
   {
      /*@todo error or similar*/
   }
   return pfile;
}

/** gets a pointer to the Seeed with given ID
 *
 * @returns SeeedPtr to Seeed or NULL if there is no Seeed with the given ID
 * @returns pool: Seeedpool* where the Seeed was found
 */
SeeedPtr MiscVisualization::GCGgetSeeed(
   SCIP* scip,       /**< SCIP data structure */
   int seeedid,      /**< ID of Seeed */
   Seeedpool* pool   /**< outputs where the Seeed was found (if not needed input NULL) */
   )
{
   SCIP_CONSHDLR* conshdlr;
   SCIP_CONSHDLRDATA* conshdlrdata;
   SeeedPtr seeed;
   Seeedpool* seeedpool;

   /* get Seeed from seeedid */
   seeed = NULL;
   conshdlr = SCIPfindConshdlr( scip, "decomp" );

   if( conshdlr == NULL )
   {
      SCIPerrorMessage("Decomp constraint handler is not included, cannot find Seeed!\n");
      return NULL;
   }

   conshdlrdata = SCIPconshdlrGetData(conshdlr);
   assert(conshdlrdata != NULL);

   seeedpool = conshdlrdata->seeedpool;
   pool = seeedpool;

   if( seeedpool != NULL )
   {
      /* find in presolved */
      for( int i = 0; i < seeedpool->getNAncestorSeeeds(); ++i)
      {
         if( seeedpool->getAncestorSeeed(i)!= NULL && seeedpool->getAncestorSeeed(i)->getID() == seeedid )
            return seeedpool->getAncestorSeeed(i);
      }

      for( int i = 0; i < seeedpool->getNIncompleteSeeeds(); ++i)
      {
         if( seeedpool->getIncompleteSeeed(i)->getID() == seeedid )
            return seeedpool->getIncompleteSeeed(i);
      }

      for( int i = 0; i < seeedpool->getNFinishedSeeeds(); ++i)
      {
         if( seeedpool->getFinishedSeeed(i)->getID() == seeedid )
            return seeedpool->getFinishedSeeed(i);
      }

      for( int i = 0; i < seeedpool->getNCurrentSeeeds(); ++i)
      {
         if( seeedpool->getCurrentSeeed(i)->getID() == seeedid )
            return seeedpool->getCurrentSeeed(i);
      }
   }

   /* find in unpresolved */
   seeedpool = conshdlrdata->seeedpoolunpresolved;
   pool = seeedpool;

   if( seeedpool != NULL )
   {
      for( int i = 0; i < seeedpool->getNAncestorSeeeds(); ++i)
      {
         if( seeedpool->getAncestorSeeed(i)!= NULL && seeedpool->getAncestorSeeed(i)->getID() == seeedid )
            return seeedpool->getAncestorSeeed(i);
      }

      for( int i = 0; i < seeedpool->getNIncompleteSeeeds(); ++i)
      {
         if( seeedpool->getIncompleteSeeed(i)->getID() == seeedid )
            return seeedpool->getIncompleteSeeed(i);
      }

      for( int i = 0; i < seeedpool->getNFinishedSeeeds(); ++i)
      {
         if( seeedpool->getFinishedSeeed(i)->getID() == seeedid )
            return seeedpool->getFinishedSeeed(i);
      }

      for( int i = 0; i < seeedpool->getNCurrentSeeeds(); ++i)
      {
         if( seeedpool->getCurrentSeeed(i)->getID() == seeedid )
            return seeedpool->getCurrentSeeed(i);
      }
   }

   pool = NULL;
   return seeed;
}

} /* namespace gcg */
