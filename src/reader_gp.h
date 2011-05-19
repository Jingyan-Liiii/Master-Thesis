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
#pragma ident "@(#) $Id: reader_gp.h,v 1.17 2010/01/04 20:35:47 bzfheinz Exp $"

/**@file   reader_gp.h
 * @brief  GP file reader
 * @author Martin Bergner
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#ifndef __SCIP_READER_GP_H__
#define __SCIP_READER_GP_H__


#include "scip/scip.h"
#include "struct_decomp.h"
#ifdef __cplusplus
extern "C" {
#endif

/** includes the gp file reader into SCIP */
extern
SCIP_RETCODE SCIPincludeReaderGp(
   SCIP*                 scip                /**< SCIP data structure */
   );

SCIP_RETCODE SCIPwriteGp(
   SCIP* scip,                                /**< SCIP data structure */
   FILE* file,                                /**< File pointer to write to */
   DECDECOMP* decdecomp,                      /**< Decomposition pointer */
   SCIP_Bool writeDecomposition                  /**< whether to write decomposed problem */
   );

SCIP_RETCODE SCIPReaderGpSetDecomp(
   SCIP* scip,
   DECDECOMP* decdecomp
   );

#ifdef __cplusplus
}
#endif

#endif
