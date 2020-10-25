/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                           */
/*                  This file is part of the program                         */
/*          GCG --- Generic Column Generation                                */
/*                  a Dantzig-Wolfe decomposition based extension            */
/*                  of the branch-cut-and-price framework                    */
/*         SCIP --- Solving Constraint Integer Programs                      */
/*                                                                           */
/* Copyright (C) 2010-2020 Operations Research, RWTH Aachen University       */
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

/**@file   struct_consclassifier.h
 * @brief  data structures for constraint classifiers
 * @author William Ma
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#ifndef GCG_STRUCT_CONSCLASSIFIER_H__
#define GCG_STRUCT_CONSCLASSIFIER_H__

#include "type_consclassifier.h"


/** detector data structure */
struct DEC_ConsClassifier {
   const char*           name;               /**< name of the detector */
   const char*           description;        /**< description of the detector */
   int                   priority;           /**< classifier priority */

   SCIP_Bool             enabled;        /* is enabled by default */

   DEC_CLASSIFIERDATA*   clsdata;            /**< custom data structure of the classifiers */

   DEC_DECL_FREECONSCLASSIFIER((*freeClassifier));                  /**< destructor of detector */
   DEC_DECL_CONSCLASSIFY((*classify));            /**< structure detection method of detector */
};


#endif //GCG_STRUCT_CONSCLASSIFIER_H__
