/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                           */
/*                  This file is part of the program                         */
/*          GCG --- Generic Column Generation                                */
/*                  a Dantzig-Wolfe decomposition based extension            */
/*                  of the branch-cut-and-price framework                    */
/*         SCIP --- Solving Constraint Integer Programs                      */
/*                                                                           */
/* Copyright (C) 2010-2014 Operations Research, RWTH Aachen University       */
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

/**@file   pub_decomp.h
 * @ingroup DECOMP
 * @ingroup PUBLICMETHODS
 * @brief  public methods for working with decomposition structures
 * @author Martin Bergner
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/
#ifndef GCG_PUB_DECOMP_H__
#define GCG_PUB_DECOMP_H__

#include "type_decomp.h"
#include "scip/type_scip.h"
#include "scip/type_retcode.h"
#include "scip/type_var.h"
#include "scip/type_cons.h"
#include "scip/type_misc.h"
#include "type_detector.h"

#ifdef __cplusplus
extern "C" {
#endif

/** score data structure **/
struct Dec_Scores
{
   SCIP_Real             borderscore;        /**< score of the border */
   SCIP_Real             densityscore;       /**< score of block densities */
   SCIP_Real             linkingscore;       /**< score related to interlinking blocks */
   SCIP_Real             totalscore;         /**< accumulated score */
};
typedef struct Dec_Scores DEC_SCORES;

/** converts the DEC_DECTYPE enum to a string */
const char *DECgetStrType(
   DEC_DECTYPE           type                /**< decomposition type */
   );

/** initializes the decdecomp structure to absolutely nothing */
SCIP_RETCODE DECdecompCreate(
   SCIP*                 scip,               /**< SCIP data structure */
   DEC_DECOMP**          decdecomp           /**< decdecomp instance */
   );

/** frees the decdecomp structure */
SCIP_RETCODE DECdecompFree(
   SCIP*                 scip,               /**< SCIP data structure */
   DEC_DECOMP**          decdecomp           /**< decdecomp instance */
   );

/** sets the type of the decomposition */
SCIP_RETCODE DECdecompSetType(
   DEC_DECOMP*           decdecomp,          /**< decdecomp instance */
   DEC_DECTYPE           type               /**< type of the decomposition */
   );

/** gets the type of the decomposition */
DEC_DECTYPE DECdecompGetType(
   DEC_DECOMP*           decdecomp           /**< decdecomp instance */
   );

/** sets the presolved flag for decomposition */
void DECdecompSetPresolved(
   DEC_DECOMP*           decdecomp,          /**< decdecomp instance */
   SCIP_Bool             presolved           /**< presolved flag for decomposition */
   );

/** gets the presolved flag for decomposition */
SCIP_Bool DECdecompGetPresolved(
   DEC_DECOMP*           decdecomp           /**< decdecomp instance */
   );

/** sets the number of blocks for decomposition */
void DECdecompSetNBlocks(
   DEC_DECOMP*           decdecomp,          /**< decdecomp instance */
   int                   nblocks             /**< number of blocks for decomposition */
   );

/** gets the number of blocks for decomposition */
int DECdecompGetNBlocks(
   DEC_DECOMP*           decdecomp           /**< decdecomp instance */
   );

/** copies the input subscipvars array to the given decdecomp structure */
SCIP_RETCODE DECdecompSetSubscipvars(
   SCIP*                 scip,               /**< SCIP data structure */
   DEC_DECOMP*           decdecomp,          /**< decdecomp instance */
   SCIP_VAR***           subscipvars,        /**< subscipvars array  */
   int*                  nsubscipvars        /**< number of subscipvars per block */
   );

/** returns the subscipvars array of the given decdecomp structure */
SCIP_VAR*** DECdecompGetSubscipvars(
   DEC_DECOMP*           decdecomp           /**< decdecomp instance */
   );

/** returns the nsubscipvars array of the given decdecomp structure */
int* DECdecompGetNSubscipvars(
   DEC_DECOMP*           decdecomp           /**< decdecomp instance */
   );

/** copies the input subscipconss array to the given decdecomp structure */
SCIP_RETCODE DECdecompSetSubscipconss(
   SCIP*                 scip,               /**< SCIP data structure */
   DEC_DECOMP*           decdecomp,          /**< decdecomp instance */
   SCIP_CONS***          subscipconss,       /**< subscipconss array  */
   int*                  nsubscipconss       /**< number of subscipconss per block */
   );

/** returns the subscipconss array of the given decdecomp structure */
SCIP_CONS*** DECdecompGetSubscipconss(
   DEC_DECOMP*           decdecomp           /**< decdecomp instance */
   );

/** returns the nsubscipconss array of the given decdecomp structure */
int*  DECdecompGetNSubscipconss(
   DEC_DECOMP*           decdecomp           /**< decdecomp instance */
   );

/** copies the input linkingconss array to the given decdecomp structure */
SCIP_RETCODE DECdecompSetLinkingconss(
   SCIP*                 scip,               /**< SCIP data structure */
   DEC_DECOMP*           decdecomp,          /**< decdecomp instance */
   SCIP_CONS**           linkingconss,       /**< linkingconss array  */
   int                   nlinkingconss       /**< number of linkingconss per block */
   );

/** returns the linkingconss array of the given decdecomp structure */
SCIP_CONS**  DECdecompGetLinkingconss(
   DEC_DECOMP*           decdecomp           /**< decdecomp instance */
   );

/** returns the nlinkingconss array of the given decdecomp structure */
int  DECdecompGetNLinkingconss(
   DEC_DECOMP*           decdecomp           /**< decdecomp instance */
   );

/** copies the input linkingvars array to the given decdecomp structure */
SCIP_RETCODE DECdecompSetLinkingvars(
   SCIP*                 scip,               /**< SCIP data structure */
   DEC_DECOMP*           decdecomp,          /**< decdecomp instance */
   SCIP_VAR**            linkingvars,        /**< linkingvars array  */
   int                   nlinkingvars        /**< number of linkingvars per block */
   );

/** returns the linkingvars array of the given decdecomp structure */
SCIP_VAR** DECdecompGetLinkingvars(
   DEC_DECOMP*           decdecomp           /**< decdecomp instance */
   );

/** returns the nlinkingvars array of the given decdecomp structure */
int DECdecompGetNLinkingvars(
   DEC_DECOMP*           decdecomp           /**< decdecomp instance */
   );

/** copies the input stairlinkingvars array to the given decdecomp structure */
SCIP_RETCODE DECdecompSetStairlinkingvars(
   SCIP*                 scip,               /**< SCIP data structure */
   DEC_DECOMP*           decdecomp,          /**< decdecomp instance */
   SCIP_VAR***           stairlinkingvars,   /**< stairlinkingvars array  */
   int*                  nstairlinkingvars   /**< number of linkingvars per block */
   );

/** returns the stairlinkingvars array of the given decdecomp structure */
SCIP_VAR*** DECdecompGetStairlinkingvars(
   DEC_DECOMP*           decdecomp           /**< decdecomp instance */
   );

/** returns the nstairlinkingvars array of the given decdecomp structure */
int* DECdecompGetNStairlinkingvars(
   DEC_DECOMP*           decdecomp           /**< decdecomp instance */
   );

/** sets the vartoblock hashmap of the given decdecomp structure */
void DECdecompSetVartoblock(
   DEC_DECOMP*           decdecomp,          /**< decdecomp instance */
   SCIP_HASHMAP*         vartoblock          /**< Vartoblock hashmap */
   );

/** returns the vartoblock hashmap of the given decdecomp structure */
SCIP_HASHMAP* DECdecompGetVartoblock(
   DEC_DECOMP*           decdecomp           /**< decdecomp instance */
   );

/** sets the constoblock hashmap of the given decdecomp structure */
void DECdecompSetConstoblock(
   DEC_DECOMP*           decdecomp,          /**< decdecomp instance */
   SCIP_HASHMAP*         constoblock         /**< Constoblock hashmap */
   );

/** returns the constoblock hashmap of the given decdecomp structure */
SCIP_HASHMAP* DECdecompGetConstoblock(
   DEC_DECOMP*           decdecomp           /**< decdecomp instance */
   );

/** sets the varindex hashmap of the given decdecomp structure */
void DECdecompSetVarindex(
   DEC_DECOMP*           decdecomp,          /**< decdecomp instance */
   SCIP_HASHMAP*         varindex            /**< Varindex hashmap */
   );

/** returns the varindex hashmap of the given decdecomp structure */
SCIP_HASHMAP* DECdecompGetVarindex(
   DEC_DECOMP*           decdecomp           /**< decdecomp instance */
   );

/** sets the consindex hashmap of the given decdecomp structure */
void DECdecompSetConsindex(
   DEC_DECOMP*           decdecomp,          /**< decdecomp instance */
   SCIP_HASHMAP*         consindex           /**< Consindexk hashmap */
   );

/** returns the consindex hashmap of the given decdecomp structure */
SCIP_HASHMAP* DECdecompGetConsindex(
   DEC_DECOMP*           decdecomp           /**< decdecomp instance */
   );

/** completely initializes decdecomp from the values of the hashmaps */
SCIP_RETCODE DECfillOutDecdecompFromHashmaps(
   SCIP*                 scip,               /**< SCIP data structure */
   DEC_DECOMP*           decdecomp,          /**< decdecomp instance */
   SCIP_HASHMAP*         vartoblock,         /**< variable to block hashmap */
   SCIP_HASHMAP*         constoblock,        /**< constraint to block hashmap */
   int                   nblocks,            /**< number of blocks */
   SCIP_VAR**            vars,               /**< variable array */
   int                   nvars,              /**< number of variables */
   SCIP_CONS**           conss,              /**< constraint array */
   int                   nconss,             /**< number of constraints */
   SCIP_Bool             staircase           /**< should the decomposition be a staircase structure */
   );

/** completely fills out detector structure from only the constraint partition */
SCIP_RETCODE DECfilloutDecdecompFromConstoblock(
   SCIP*                 scip,               /**< SCIP data structure */
   DEC_DECOMP*           decdecomp,          /**< decomposition structure */
   SCIP_HASHMAP*         constoblock,        /**< constraint to block hashmap */
   int                   nblocks,            /**< number of blocks */
   SCIP_VAR**            vars,               /**< variable array */
   int                   nvars,              /**< number of variables */
   SCIP_CONS**           conss,              /**< constraint array */
   int                   nconss,             /**< number of constraints */
   SCIP_Bool             staircase           /**< should the decomposition be a staircase structure */
   );

/** sets the detector for the given decdecomp structure */
void DECdecompSetDetector(
   DEC_DECOMP*           decdecomp,          /**< decdecomp instance */
   DEC_DETECTOR*         detector            /**< detector data structure */
   );

/** gets the detector for the given decdecomp structure */
DEC_DETECTOR* DECdecompGetDetector(
   DEC_DECOMP*           decdecomp           /**< decdecomp instance */
   );

/** transforms all constraints and variables, updating the arrays */
SCIP_RETCODE DECdecompTransform(
   SCIP*                 scip,               /**< SCIP data structure */
   DEC_DECOMP*           decdecomp           /**< decdecomp instance */
   );

/**
 * Adds all those constraints that were added to the problem after the decomposition as created
 */
extern
SCIP_RETCODE DECdecompAddRemainingConss(
   SCIP*                 scip,               /**< SCIP data structure */
   DEC_DECOMP*           decdecomp           /**< decomposition data structure */
   );


extern
SCIP_RETCODE DECdecompCheckConsistency(
   SCIP*                 scip,               /**< SCIP data structure */
   DEC_DECOMP*           decdecomp           /**< decomposition data structure */
   );

/** creates a decomposition with all constraints in the master */
extern
SCIP_RETCODE DECcreateBasicDecomp(
   SCIP*                 scip,                /**< SCIP data structure */
   DEC_DECOMP**          decomp               /**< decomposition structure */
   );

/** creates a decomposition with provided constraints in the master
 * The function will put the remaining constraints in one or more pricing problems
 * depending on whether the subproblems decompose with no variables in common.
 */
extern
SCIP_RETCODE DECcreateDecompFromMasterconss(
   SCIP*                 scip,                /**< SCIP data structure */
   DEC_DECOMP**          decomp,              /**< decomposition structure */
   SCIP_CONS**           conss,               /**< constraints to be put in the master */
   int                   nconss               /**< number of constraints in the master */
   );

/** return the number of variables and binary, integer, implied integer, continuous variables of all subproblems */
extern
void DECgetSubproblemVarsData(
   SCIP*                 scip,                /**< SCIP data structure */
   DEC_DECOMP*           decomp,              /**< decomposition structure */
   int*                  nvars,               /**< pointer to array of size nproblems to store number of subproblem vars or NULL */
   int*                  nbinvars,            /**< pointer to array of size nproblems to store number of binary subproblem vars or NULL */
   int*                  nintvars,            /**< pointer to array of size nproblems to store number of integer subproblem vars or NULL */
   int*                  nimplvars,           /**< pointer to array of size nproblems to store number of implied subproblem vars or NULL */
   int*                  ncontvars,           /**< pointer to array of size nproblems to store number of continuous subproblem vars or NULL */
   int                   nproblems            /**< size of the arrays*/
   );

/** return the number of variables and binary, integer, implied integer, continuous variables of the master */
extern
void DECgetLinkingVarsData(
   SCIP*                 scip,               /**< SCIP data structure */
   DEC_DECOMP*           decomp,             /**< decomposition structure */
   int*                  nvars,              /**< pointer to store number of linking vars or NULL */
   int*                  nbinvars,           /**< pointer to store number of binary linking vars or NULL */
   int*                  nintvars,           /**< pointer to store number of integer linking vars or NULL */
   int*                  nimplvars,          /**< pointer to store number of implied linking vars or NULL */
   int*                  ncontvars           /**< pointer to store number of continuous linking vars or NULL */
   );

/**
 * returns the number of nonzeros of each column of the constraint matrix both in the subproblem and in the master
 * @note For linking variables, the number of nonzeros in the subproblems corresponds to the number on nonzeros
 * in the border
 *
 * @note The arrays have to be allocated by the caller
 *
 * @pre This function assumes that constraints are partitioned in the decomp structure, no constraint is present in more than one block
 *
 */
extern
SCIP_RETCODE DECgetDensityData(
   SCIP*                 scip,               /**< SCIP data structure */
   DEC_DECOMP*           decomp,             /**< decomposition structure */
   SCIP_VAR**            vars,               /**< pointer to array store variables belonging to density */
   int                   nvars,              /**< number of variables */
   SCIP_CONS**           conss,              /**< pointer to array to store constraints belonging to the density */
   int                   nconss,             /**< number of constraints */
   int*                  varsubproblemdensity, /**< pointer to array to store the nonzeros for the subproblems */
   int*                  varmasterdensity,   /**< pointer to array to store the nonzeros for the master */
   int*                  conssubproblemdensity, /**< pointer to array to store the nonzeros for the subproblems */
   int*                  consmasterdensity   /**< pointer to array to store the nonzeros for the master */
);

/**
 *  calculates the number of up and down locks of variables for a given decomposition in both the original problem and the pricingproblems
 *
 *  @note All arrays need to be allocated by the caller
 *
 *  @warning This function needs a lot of memory (nvars*nblocks+1) array entries
 */
SCIP_RETCODE DECgetVarLockData(
   SCIP*                 scip,               /**< SCIP data structure */
   DEC_DECOMP*           decomp,             /**< decomposition structure */
   SCIP_VAR**            vars,               /**< pointer to array store variables belonging to density */
   int                   nvars,              /**< number of variables */
   int                   nsubproblems,       /**< number of sub problems */
   int**                 subsciplocksdown,   /**< pointer to two dimensional array to store the down locks for the subproblems */
   int**                 subsciplocksup,     /**< pointer to two dimensional array to store the down locks for the subproblems */
   int*                  masterlocksdown,    /**< pointer to array to store the down locks for the master */
   int*                  masterlocksup       /**< pointer to array to store the down locks for the master */
   );


/** computes the score of the given decomposition based on the border, the average density score and the ratio of
 * linking variables
 */
extern
SCIP_RETCODE DECevaluateDecomposition(
   SCIP*                 scip,               /**< SCIP data structure */
   DEC_DECOMP*           decdecomp,          /**< decomposition data structure */
   DEC_SCORES*           score               /**< returns the score of the decomposition */
   );

/** display statistics about the decomposition */
extern
SCIP_RETCODE GCGprintDecompStatistics(
   SCIP*                 scip,               /**< SCIP data structure */
   FILE*                 file                /**< output file or NULL for standard output */
   );

/** returns whether both structures lead to the same decomposition */
extern
SCIP_Bool DECdecompositionsAreEqual(
   SCIP*                 scip,               /**< SCIP data structure */
   DEC_DECOMP*           decomp1,            /**< first decomp data structure */
   DEC_DECOMP*           decomp2             /**< second decomp data structure */
);


/** filters similar decompositions from a given list and moves them to the end
 * @return the number of unique decompositions
 */
extern
int DECfilterSimilarDecompositions(
   SCIP*                 scip,               /**< SCIP data structure */
   DEC_DECOMP**          decs,               /**< array of decompositions */
   int                   ndecs               /**< number of decompositions */
);

/** returns the number of the block that the constraint is with respect to the decomposition */
extern
SCIP_RETCODE DECdetermineConsBlock(
   SCIP*                 scip,               /**< SCIP data structure */
   DEC_DECOMP*           decomp,             /**< decomposition */
   SCIP_CONS*            cons,               /**< constraint to check */
   int                   *block              /**< block of the constraint (or nblocks for master) */
);

/** move a master constraint to pricing problem */
SCIP_RETCODE DECdecompMoveLinkingConsToPricing(
   SCIP*                 scip,               /**< SCIP data structure */
   DEC_DECOMP*           decomp,             /**< decomposition */
   int                   consindex,          /**< index of constraint to move */
   int                   block               /**< block of the pricing problem where to move */
   );

/** tries to assign masterconss to pricing problem */
SCIP_RETCODE DECtryAssignMasterconssToExistingPricing(
   SCIP*                 scip,               /**< SCIP data structure */
   DEC_DECOMP*           decomp,             /**< decomposition */
   int*                  transferred         /**< number of master constraints reassigned */
   );

/** removes a variable from the linking variable array */
SCIP_RETCODE DECdecompRemoveLinkingVar(
   SCIP*                 scip,               /**< SCIP data structure */
   DEC_DECOMP*           decomp,             /**< decomposition */
   SCIP_VAR*             var,                /**< variable to remove */
   SCIP_Bool*            success             /**< indicates whether the variable was successfully removed */
   );

/** tries to assign masterconss to new pricing problem */
SCIP_RETCODE DECtryAssignMasterconssToNewPricing(
   SCIP*                 scip,               /**< SCIP data structure */
   DEC_DECOMP*           decomp,             /**< decomposition */
   DEC_DECOMP**          newdecomp,          /**< new decomposition, if successful */
   int*                  transferred         /**< number of master constraints reassigned */
   );

/** polish the decomposition and try to greedily assign master constraints to pricing problem where usefule */
SCIP_RETCODE DECcreatePolishedDecomp(
   SCIP*                 scip,               /**< SCIP data structure */
   DEC_DECOMP*           decomp,             /**< decomposition */
   DEC_DECOMP**          newdecomp           /**< new decomposition, if successful */
   );

#ifdef __cplusplus
}
#endif
#endif
