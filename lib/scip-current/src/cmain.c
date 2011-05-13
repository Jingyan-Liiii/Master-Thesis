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
/*  You should have received a copy of the ZIB Academic License              */
/*  along with SCIP; see the file COPYING. If not email to scip@zib.de.      */
/*                                                                           */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/**@file   cmain.c
 * @brief  main file for C compilation
 * @author Tobias Achterberg
 */

/*--+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#include <stdio.h>

#include "scip/scip.h"
#include "scip/scipshell.h"


int
main(
   int                        argc,
   char**                     argv
   )
{
   SCIP_RETCODE retcode;

   /**@todo implement remaining events */
   /**@todo avoid addition of identical rows */
   /**@todo avoid addition of identical constraints */
   /**@todo pricing for pseudo solutions */
   /**@todo it's a bit ugly, that user call backs may be called before the nodequeue was processed */
   /**@todo unboundness detection in presolving -> convert problem into feasibility problem to decide
    *       unboundness/infeasibility */
   /**@todo variable event PSSOLCHANGED, update pseudo activities in constraints to speed up checking of pseudo solutions */
   /**@todo branching rule acting as a filter by temporary changing the branching priority of variables and returning 
    *       SCIP_DIDNOTFIND to let the next branching rule select the branching variable */
   /**@todo use aging in all constraint handlers */
   /**@todo try to not use the first but the shortest constraint as reason for a deduction */

   retcode = SCIPrunShell(argc, argv, "scip.set");
   if( retcode != SCIP_OKAY )
   {
      SCIPprintError(retcode, stderr);
      return -1;
   }

   return 0;
}
