/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                           */
/*                  This file is part of the program and library             */
/*         SCIP --- Solving Constraint Integer Programs                      */
/*                                                                           */
/*    Copyright (C) 2002-2009 Konrad-Zuse-Zentrum                            */
/*                            fuer Informationstechnik Berlin                */
/*                                                                           */
/*  SCIP is distributed under the terms of the ZIB Academic License.         */
/*                                                                           */
/*  You should have received a copy of the ZIB Academic License              */
/*  along with SCIP; see the file COPYING. If not email to scip@zib.de.      */
/*                                                                           */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
#pragma ident "@(#) $Id$"
#define SCIP_DEBUG
/**@file   branch_master.c
 * @ingroup BRANCHINGRULES
 * @brief  most infeasible LP branching rule
 * @author Gerald Gamrath
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#include <assert.h>
#include <string.h>

#include "branch_master.h"
#include "cons_origbranch.h"


#define BRANCHRULE_NAME          "master"
#define BRANCHRULE_DESC          "branching for generic column generation, doing most infeasible branching"
#define BRANCHRULE_PRIORITY      1000000
#define BRANCHRULE_MAXDEPTH      -1
#define BRANCHRULE_MAXBOUNDDIST  1.0




/*
 * Callback methods
 */

/** destructor of branching rule to free user data (called when SCIP is exiting) */
#define branchFreeMaster NULL


/** initialization method of branching rule (called after problem was transformed) */
#define branchInitMaster NULL


/** deinitialization method of branching rule (called before transformed problem is freed) */
#define branchExitMaster NULL


/** solving process initialization method of branching rule (called when branch and bound process is about to begin) */
#define branchInitsolMaster NULL


/** solving process deinitialization method of branching rule (called before branch and bound process data is freed) */
#define branchExitsolMaster NULL


/** branching execution method for fractional LP solutions */
static
SCIP_DECL_BRANCHEXECLP(branchExeclpMaster)
{  
   SCIP_NODE* childup;
   SCIP_NODE* childdown;

   SCIPdebugMessage("Execlp method of master branching\n");

   /* create the b&b-tree child-nodes of the current node */
   SCIP_CALL( SCIPcreateChild(scip, &childup, 0.0, SCIPgetLocalTransEstimate(scip)) );
   SCIP_CALL( SCIPcreateChild(scip, &childdown, 0.0, SCIPgetLocalTransEstimate(scip)) );

   *result = SCIP_BRANCHED;

   return SCIP_OKAY;
}


/** branching execution method for not completely fixed pseudo solutions */
static
SCIP_DECL_BRANCHEXECPS(branchExecpsMaster)
{
   SCIPdebugMessage("Execps method of master branching\n");

   return SCIP_OKAY;
}




/*
 * branching specific interface methods
 */

/** creates the most infeasible LP braching rule and includes it in SCIP */
SCIP_RETCODE SCIPincludeBranchruleMaster(
   SCIP*                 scip                /**< SCIP data structure */
   )
{
   SCIP_BRANCHRULEDATA* branchruledata;

   /* create inference branching rule data */
   branchruledata = NULL;

   /* include branching rule */
   SCIP_CALL( SCIPincludeBranchrule(scip, BRANCHRULE_NAME, BRANCHRULE_DESC, BRANCHRULE_PRIORITY, 
         BRANCHRULE_MAXDEPTH, BRANCHRULE_MAXBOUNDDIST,
         branchFreeMaster, branchInitMaster, branchExitMaster, branchInitsolMaster, branchExitsolMaster, 
         branchExeclpMaster, branchExecpsMaster,
         branchruledata) );

   return SCIP_OKAY;
}
