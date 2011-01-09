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
#pragma ident "@(#) $Id: nlpi_xxx.h,v 1.3 2010/09/15 14:57:34 bzfviger Exp $"

/**@file    nlpi_xxx.h
 * @brief   XXX NLP interface
 * @ingroup NLPIS
 * @author  you
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#ifndef __SCIP_NLPI_XXX_H__
#define __SCIP_NLPI_XXX_H__

#include "nlpi/type_nlpi.h"

#ifdef __cplusplus
extern "C" {
#endif

/** create solver interface for Xxx solver */
extern
SCIP_RETCODE SCIPcreateNlpSolverXxx(
   BMS_BLKMEM*           blkmem,             /**< block memory data structure */
   SCIP_NLPI**           nlpi                /**< pointer to buffer for nlpi address */
);

#ifdef __cplusplus
}
#endif

#endif /* __SCIP_NLPI_XXX_H__ */
