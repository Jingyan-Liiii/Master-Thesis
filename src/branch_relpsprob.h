/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                           */
/*                  This file is part of the program and library             */
/*         SCIP --- Solving Constraint Integer Programs                      */
/*                                                                           */
/*    Copyright (C) 2002-2010 Konrad-Zuse-Zentrum                            */
/*                            fuer Informationstechnik Berlin                */
/*                                                                           */
/*  SCIP is distributed under the terms of the ZIB Academic License.         */
/*                                                                           */
/*  You should have received a copy of the ZIB Academic License              */
/*  along with SCIP; see the file COPYING. If not email to scip@zib.de.      */
/*                                                                           */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
#pragma ident "@(#) $Id: branch_relpsprob.h,v 1.16 2010/02/04 10:23:56 bzfheinz Exp $"

/**@file   branch_relpsprob.h
 * @brief  reliable pseudo costs branching rule
 * @author Tobias Achterberg
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#ifndef __SCIP_BRANCH_RELPSPROB_H__
#define __SCIP_BRANCH_RELPSPROB_H__


#include "scip/scip.h"

#ifdef __cplusplus
extern "C" {
#endif

/** creates the reliable pseudo cost braching rule and includes it in SCIP */
extern
SCIP_RETCODE SCIPincludeBranchruleRelpsprob(
   SCIP*                 scip                /**< SCIP data structure */
   );

/** execution reliability pseudo cost probing branching with the given branching candidates */
extern
SCIP_RETCODE SCIPgetRelpsprobBranchVar(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_Bool             allowaddcons,       /**< is the branching rule allowed to add constraints to the current node
                                              *   in order to cut off the current solution instead of creating a branching? */
   SCIP_VAR**            branchcands,        /**< brancing candidates */
   SCIP_Real*            branchcandssol,     /**< solution value for the branching candidates */
   SCIP_Real*            branchcandsfrac,    /**< fractional part of the branching candidates, zero possible */
   int                   nbranchcands,       /**< number of branching candidates */
   int                   nvars,              /**< number of variables to be watched by bdchgdata */
   SCIP_RESULT*          result,             /**< pointer to the result of the execution */
   SCIP_VAR**            branchvar           /**< pointer to the variable to branch on */
   );

#ifdef __cplusplus
}
#endif

#endif
