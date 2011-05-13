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

/**@file   prop.h
 * @brief  internal methods for propagators
 * @author Tobias Achterberg
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#ifndef __SCIP_PROP_H__
#define __SCIP_PROP_H__


#include "scip/def.h"
#include "blockmemshell/memory.h"
#include "scip/type_retcode.h"
#include "scip/type_result.h"
#include "scip/type_set.h"
#include "scip/type_stat.h"
#include "scip/type_lp.h"
#include "scip/type_var.h"
#include "scip/type_prop.h"
#include "scip/pub_prop.h"

#ifdef __cplusplus
extern "C" {
#endif

/** copies the given propagator to a new scip */
extern
SCIP_RETCODE SCIPpropCopyInclude(
   SCIP_PROP*            prop,               /**< propagator */
   SCIP_SET*             set                 /**< SCIP_SET of SCIP to copy to */
   );

/** creates a propagator */
extern
SCIP_RETCODE SCIPpropCreate(
   SCIP_PROP**           prop,               /**< pointer to propagator data structure */
   SCIP_SET*             set,                /**< global SCIP settings */
   BMS_BLKMEM*           blkmem,             /**< block memory for parameter settings */
   const char*           name,               /**< name of propagator */
   const char*           desc,               /**< description of propagator */
   int                   priority,           /**< priority of propagator (>= 0: before, < 0: after constraint handlers) */
   int                   freq,               /**< frequency for calling propagator */
   SCIP_Bool             delay,              /**< should propagator be delayed, if other propagators found reductions? */
   SCIP_DECL_PROPCOPY    ((*propcopy)),      /**< copy method of propagator or NULL if you don't want to copy your plugin into subscips */
   SCIP_DECL_PROPFREE    ((*propfree)),      /**< destructor of propagator */
   SCIP_DECL_PROPINIT    ((*propinit)),      /**< initialize propagator */
   SCIP_DECL_PROPEXIT    ((*propexit)),      /**< deinitialize propagator */
   SCIP_DECL_PROPINITSOL ((*propinitsol)),   /**< solving process initialization method of propagator */
   SCIP_DECL_PROPEXITSOL ((*propexitsol)),   /**< solving process deinitialization method of propagator */
   SCIP_DECL_PROPEXEC    ((*propexec)),      /**< execution method of propagator */
   SCIP_DECL_PROPRESPROP ((*propresprop)),   /**< propagation conflict resolving method */
   SCIP_PROPDATA*        propdata            /**< propagator data */
   );

/** calls destructor and frees memory of propagator */
extern
SCIP_RETCODE SCIPpropFree(
   SCIP_PROP**           prop,               /**< pointer to propagator data structure */
   SCIP_SET*             set                 /**< global SCIP settings */
   );

/** initializes propagator */
extern
SCIP_RETCODE SCIPpropInit(
   SCIP_PROP*            prop,               /**< propagator */
   SCIP_SET*             set                 /**< global SCIP settings */
   );

/** calls exit method of propagator */
extern
SCIP_RETCODE SCIPpropExit(
   SCIP_PROP*            prop,               /**< propagator */
   SCIP_SET*             set                 /**< global SCIP settings */
   );

/** informs propagator that the branch and bound process is being started */
extern
SCIP_RETCODE SCIPpropInitsol(
   SCIP_PROP*            prop,               /**< propagator */
   SCIP_SET*             set                 /**< global SCIP settings */
   );

/** informs propagator that the branch and bound process data is being freed */
extern
SCIP_RETCODE SCIPpropExitsol(
   SCIP_PROP*            prop,               /**< propagator */
   SCIP_SET*             set                 /**< global SCIP settings */
   );

/** calls execution method of propagator */
extern
SCIP_RETCODE SCIPpropExec(
   SCIP_PROP*            prop,               /**< propagator */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_STAT*            stat,               /**< dynamic problem statistics */
   int                   depth,              /**< depth of current node */
   SCIP_Bool             execdelayed,        /**< execute propagator even if it is marked to be delayed */
   SCIP_RESULT*          result              /**< pointer to store the result of the callback method */
   );

/** resolves the given conflicting bound, that was deduced by the given propagator, by putting all "reason" bounds
 *  leading to the deduction into the conflict queue with calls to SCIPaddConflictLb() and SCIPaddConflictUb()
 */
extern
SCIP_RETCODE SCIPpropResolvePropagation(
   SCIP_PROP*            prop,               /**< propagator */
   SCIP_SET*             set,                /**< global SCIP settings */
   SCIP_VAR*             infervar,           /**< variable whose bound was deduced by the constraint */
   int                   inferinfo,          /**< user inference information attached to the bound change */
   SCIP_BOUNDTYPE        inferboundtype,     /**< bound that was deduced (lower or upper bound) */
   SCIP_BDCHGIDX*        bdchgidx,           /**< bound change index, representing the point of time where change took place */
   SCIP_RESULT*          result              /**< pointer to store the result of the callback method */
   );

/** sets priority of propagator */
extern
void SCIPpropSetPriority(
   SCIP_PROP*            prop,               /**< propagator */
   SCIP_SET*             set,                /**< global SCIP settings */
   int                   priority            /**< new priority of the propagator */
   );

#ifdef __cplusplus
}
#endif

#endif
