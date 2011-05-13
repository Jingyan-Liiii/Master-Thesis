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

/**@file   heur_subnlp.h
 * @brief  NLP local search primal heuristic using subSCIPs
 * @author Stefan Vigerske
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#ifndef HEUR_SUBNLP_H_
#define HEUR_SUBNLP_H_

#include "scip/scip.h"

#ifdef __cplusplus
extern "C" {
#endif

/** creates the NLP local search primal heuristic and includes it in SCIP */
extern
SCIP_RETCODE SCIPincludeHeurSubNlp(
   SCIP*                 scip                /**< SCIP data structure */
   );

/** updates the starting point for the NLP heuristic
 * 
 * Is called, for example, by a constraint handler that handles nonlinear constraints when a check on feasibility of a solution fails.
 */
extern
SCIP_RETCODE SCIPupdateStartpointHeurSubNlp(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_HEUR*            heur,               /**< subNLP heuristic */
   SCIP_SOL*             solcand,            /**< solution candidate */
   SCIP_Real             violation           /**< constraint violation of solution candidate */
   );

/** main procedure of the subNLP heuristic */
extern
SCIP_RETCODE SCIPapplyHeurSubNlp(
   SCIP*                 scip,               /**< original SCIP data structure                                   */
   SCIP_HEUR*            heur,               /**< heuristic data structure                                       */
   SCIP_RESULT*          result,             /**< pointer to store result of: solution found, no solution found, or fixing is infeasible (cutoff) */
   SCIP_SOL*             refpoint,           /**< point to take fixation of discrete variables from, and startpoint for NLP solver; if NULL, then LP solution is used */
   SCIP_Longint          itercontingent,     /**< iteration limit for NLP solver                                 */
   SCIP_Real             timelimit,          /**< time limit for NLP solver                                      */
   SCIP_Real             minimprove,         /**< desired minimal relative improvement in objective function value */
   SCIP_Longint*         iterused            /**< buffer to store number of iterations used by NLP solver, or NULL if not of interest */
   );

/** adds all known linear constraint to the NLP, if initialized and not done already
 * This function is temporary and will hopefully become obsolete in the near future.
 */ 
extern
SCIP_RETCODE SCIPaddLinearConsToNlpHeurSubNlp(
   SCIP*                 scip,               /**< original SCIP data structure                                   */
   SCIP_HEUR*            heur,               /**< heuristic data structure                                       */
   SCIP_Bool             addcombconss,       /**< whether to add combinatorial linear constraints, i.e., linear constraints that involve only discrete variables */
   SCIP_Bool             addcontconss        /**< whether to add continuous    linear constraints, i.e., linear constraints that involve not only discrete variables */
);
   
/** gets subSCIP used by NLP heuristic, or NULL if none */
extern
SCIP* SCIPgetSubScipHeurSubNlp(
   SCIP*                 scip,               /**< original SCIP data structure                                   */
   SCIP_HEUR*            heur                /**< heuristic data structure                                       */
   );

/** gets mapping of SCIP variables to subSCIP variables */
extern
SCIP_VAR** SCIPgetVarMappingScip2SubScipHeurSubNlp(
   SCIP*                 scip,               /**< original SCIP data structure                                   */
   SCIP_HEUR*            heur                /**< heuristic data structure                                       */
   );

/** gets mapping of subSCIP variables to SCIP variables */
extern
SCIP_VAR** SCIPgetVarMappingSubScip2ScipHeurSubNlp(
   SCIP*                 scip,               /**< original SCIP data structure                                   */
   SCIP_HEUR*            heur                /**< heuristic data structure                                       */
   );

#ifdef __cplusplus
}
#endif

#endif /*HEUR_SUBNLP_H_*/
