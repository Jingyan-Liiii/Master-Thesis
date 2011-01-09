/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                           */
/*                  This file is part of the program and library             */
/*         SCIP --- Solving Constraint Integer Programs                      */
/*                                                                           */
/*    Copyright (C) 2002-2011 Konrad-Zuse-Zentrum                            */
/*                            fuer Informationstechnik Berlin                */
/*                                                                           */
/*  SCIP is distributed under the terms of the ZIB Academic License.         */
/*                                                                           */
/*  You should have received a copy of the ZIB Academic License.             */
/*  along with SCIP; see the file COPYING. If not email to scip@zib.de.      */
/*                                                                           */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
#pragma ident "@(#) $Id: objcloneable.h,v 1.7.2.1 2011/01/02 11:19:53 bzfheinz Exp $"

/**@file   objcloneable.h
 * @author Michael Winkler
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#ifndef __SCIP_OBJCLONEABLE_H__
#define __SCIP_OBJCLONEABLE_H__

#include "scip/def.h"
#include "scip/scip.h"

namespace scip
{
   /** all except constraint handlers and variable pricer C++ wrapper object plugins should extend this class */
   struct ObjCloneable 
   {
      virtual ~ObjCloneable() {}

      /** clone method which will be used to copy objective plugins which are not a constraint handlers or a variable
       *  pricer plugin */
      virtual ObjCloneable* clone(
         SCIP*                 scip                /**< SCIP data structure */
         ) const
      {
         return 0;
      }
      
      /** returns whether the objective plugin is copyable */
      virtual SCIP_Bool iscloneable(
         void
         ) const
      {
         return false;
      }
   };
}

#endif
