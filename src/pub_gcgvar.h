/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                           */
/*                  This file is part of the program                         */
/*          GCG --- Generic Colum Generation                                 */
/*                  a Dantzig-Wolfe decomposition based extension            */
/*                  of the branch-cut-and-price framework                    */
/*         SCIP --- Solving Constraint Integer Programs                      */
/*                                                                           */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
#pragma ident "@(#) $Id$"

/**@file   dec_borderheur.h
 * @brief  borderheur presolver
 * @author Martin Bergner
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#ifndef __GCG_PUB_GCGVAR_H__
#define __GCG_PUB_GCGVAR_H__


#include "scip/scip.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Returns TRUE or FALSE whether variable is a pricing variable or not */
extern 
SCIP_Bool GCGvarIsPricing(
   SCIP_VAR* var          /**< SCIP variable */
   );

/** Returns TRUE or FALSE whether variable is a original variable or not */
extern 
SCIP_Bool GCGvarIsOriginal(
   SCIP_VAR* var          /**< SCIP variable structure */
   );

/** Returns TRUE or FALSE whether variable is a master variable or not */
extern 
SCIP_Bool GCGvarIsMaster(
   SCIP_VAR* var          /**< SCIP variable structure */
   );

   /** Returns TRUE or FALSE whether variable is a linking variable or not */
extern
SCIP_Bool GCGvarIsLinking(
   SCIP_VAR* var /**< SCIP variable structure */
   );

/** Returns the original var of a pricing variable */
extern
SCIP_VAR* GCGpricingVarGetOriginalVar(
   SCIP_VAR* var /**< SCIP variable structure */
   );

/** Returns the pricing var of an original variable */
extern
SCIP_VAR* GCGoriginalVarGetPricingVar(
   SCIP_VAR* var /**< SCIP variable structure */
   );

/** Returns the pricing variables of an linking variable */
extern
SCIP_VAR** GCGlinkingVarGetPricingVars(
   SCIP_VAR* var /**< SCIP variable structure */
   );

/** Returns the number of master variables the original variable is contained in */
extern
int GCGoriginalVarGetNMastervars(
   SCIP_VAR* var
   );

/** Returns the master variables the original variable is contained in */
extern
SCIP_VAR** GCGoriginalVarGetMastervars(
   SCIP_VAR* var
   );

/** Returns the fraction of master variables the original variable is contained in */
extern
SCIP_Real* GCGoriginalVarGetMastervals(
   SCIP_VAR* var
   );

/** Returns the number of original variables the master variable is contained in */
extern
int GCGmasterVarGetNOrigvars(
   SCIP_VAR* var
   );

/** Returns the original variables the master variable is contained in */
extern
SCIP_VAR** GCGmasterVarGetOrigvars(
   SCIP_VAR* var
   );

/** Returns the fraction of original variables the master variable is contained in */
extern
SCIP_Real* GCGmasterVarGetOrigvals(
   SCIP_VAR* var
   );

/** Returns the number of original variables the pricing variable is contained in */
extern
int GCGpricingVarGetNOrigvars(
   SCIP_VAR* var
   );

/** Returns the original variables the pricing variable is contained in */
extern
SCIP_VAR** GCGpricingVarGetOrigvars(
   SCIP_VAR* var
   );

/** Returns the block of the variable */
extern
int GCGvarGetBlock(
   SCIP_VAR* var
   );

/** Returns TRUE if the linking variable is in the block, FALSE otherwise */
extern
SCIP_Bool GCGisLinkingVarInBlock(
   SCIP_VAR* var,
   int block
   );
   
#ifdef __cplusplus
}
#endif

#endif
