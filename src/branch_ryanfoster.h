/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                           */
/*                  This file is part of the program                         */
/*          GCG --- Generic Column Generation                                */
/*                  a Dantzig-Wolfe decomposition based extension            */
/*                  of the branch-cut-and-price framework                    */
/*         SCIP --- Solving Constraint Integer Programs                      */
/*                                                                           */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/**@file   branch_ryanfoster.h
 * @brief  branching rule for original problem in gcg implementing the Ryan and Foster branching scheme
 * @author Gerald Gamrath
 * @ingroup BRANCHINGRULES
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#ifndef __SCIP_BRANCH_RYANFOSTER_H__
#define __SCIP_BRANCH_RYANFOSTER_H__


#include "scip/scip.h"


/** creates the most infeasible LP braching rule and includes it in SCIP */
extern
SCIP_RETCODE SCIPincludeBranchruleRyanfoster(
   SCIP*                 scip                /**< SCIP data structure */
   );

#endif
