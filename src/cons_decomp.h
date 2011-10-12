/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                           */
/*                  This file is part of the program                         */
/*          GCG --- Generic Column Generation                                */
/*                  a Dantzig-Wolfe decomposition based extension            */
/*                  of the branch-cut-and-price framework                    */
/*         SCIP --- Solving Constraint Integer Programs                      */
/*                                                                           */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/**@file   cons_decomp.h
 * @brief  constraint handler for decomp constraints
 * @author Tobias Achterberg
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#ifndef __SCIP_CONS_DECOMP_H__
#define __SCIP_CONS_DECOMP_H__


#include "scip/scip.h"
#include "type_detector.h"

typedef struct DEC_Detector DEC_DETECTOR;
typedef struct DEC_DetectorData DEC_DETECTORDATA;


#ifdef __cplusplus
extern "C" {
#endif

/** creates the handler for decomp constraints and includes it in SCIP */
extern
SCIP_RETCODE SCIPincludeConshdlrDecomp(
   SCIP*                 scip                /**< SCIP data structure */
   );

/** creates and captures a decomp constraint */
extern
SCIP_RETCODE SCIPcreateConsDecomp(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONS**           cons,               /**< pointer to hold the created constraint */
   const char*           name                /**< name of constraint */
   );

/** returns the decomposition structure **/
extern
DECDECOMP* SCIPconshdlrDecompGetDecdecomp(
   SCIP *scip                            /**< SCIP data structure */
   );

extern
DEC_DETECTORDATA* DECdetectorGetData(
   DEC_DETECTOR* detector
   );

extern
const char* DECdetectorGetName(
   DEC_DETECTOR* detector
   );


extern
DEC_DETECTOR* DECfindDetector(
   SCIP *scip,
   const char *name
   );

extern
SCIP_RETCODE DECincludeDetector(
   SCIP* scip,
   const char *name,
   DEC_DETECTORDATA *detectordata,
   DEC_DECL_DETECTSTRUCTURE((*detectStructure)),
   DEC_DECL_SETSTRUCTDECOMP((*setStructDecomp)),
   DEC_DECL_INITDETECTOR((*initDetector)),
   DEC_DECL_EXITDETECTOR((*exitDetector)),
   DEC_DECL_GETPRIORITY((*getPriority))
   );

extern
SCIP_Real DECgetRemainingTime(
   SCIP* scip
   );

#ifdef __cplusplus
}
#endif

#endif
