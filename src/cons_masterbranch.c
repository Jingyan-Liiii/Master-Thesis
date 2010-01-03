/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                           */
/*                  This file is part of the program and library             */
/*         SCIP --- Solving Constraint Integer Programs                      */
/*                                                                           */
/*    Copyright (C) 2002-2008 Konrad-Zuse-Zentrum                            */
/*                            fuer Informationstechnik Berlin                */
/*                                                                           */
/*  SCIP is distributed under the terms of the ZIB Academic License.         */
/*                                                                           */
/*  You should have received a copy of the ZIB Academic License              */
/*  along with SCIP; see the file COPYING. If not email to scip@zib.de.      */
/*                                                                           */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
#pragma ident "@(#) $Id$"
//#define SCIP_DEBUG
//#define CHECKPROPAGATEDVARS
/**@file   cons_masterbranch.c
 * @brief  constraint handler for storing the branching decisions at each node of the tree
 * @author Gerald Gamrath
 *
 */

#include <assert.h>
#include <string.h>

#include "scip/type_cons.h"
#include "scip/cons_linear.h"

#include "cons_masterbranch.h"
#include "cons_origbranch.h"
#include "relax_gcg.h"
#include "pricer_gcg.h"

#include "struct_vardata.h"


/* constraint handler properties */
#define CONSHDLR_NAME          "masterbranch"
#define CONSHDLR_DESC          "store branching decision at nodes of the tree constraint handler"
#define CONSHDLR_SEPAPRIORITY         0 /**< priority of the constraint handler for separation */
#define CONSHDLR_ENFOPRIORITY         0 /**< priority of the constraint handler for constraint enforcing */
#define CONSHDLR_CHECKPRIORITY  2000000 /**< priority of the constraint handler for checking feasibility */
#define CONSHDLR_SEPAFREQ            -1 /**< frequency for separating cuts; zero means to separate only in the root node */
#define CONSHDLR_PROPFREQ             1 /**< frequency for propagating domains; zero means only preprocessing propagation */
#define CONSHDLR_EAGERFREQ          100 /**< frequency for using all instead of only the useful constraints in separation,
                                              * propagation and enforcement, -1 for no eager evaluations, 0 for first only */
#define CONSHDLR_MAXPREROUNDS        -1 /**< maximal number of presolving rounds the constraint handler participates in (-1: no limit) */
#define CONSHDLR_DELAYSEPA        FALSE /**< should separation method be delayed, if other separators found cuts? */
#define CONSHDLR_DELAYPROP        FALSE /**< should propagation method be delayed, if other propagators found reductions? */
#define CONSHDLR_DELAYPRESOL      FALSE /**< should presolving method be delayed, if other presolvers found reductions? */
#define CONSHDLR_NEEDSCONS         TRUE /**< should the constraint handler be skipped, if no constraints are available? */



/** constraint data for branch orig constraints */
struct SCIP_ConsData
{
   int                propagatedvars;        /**< number of Vars that existed, the last time, the related node was propagated,
                                                used to determine whether the constraint should be repropagated */
   SCIP_Bool          needprop;              /**< should the constraint be propagated? */
   SCIP_Bool          created;
   SCIP_NODE*         node;                  /**< the node at which the cons is sticking */
   SCIP_CONS*         parentcons;            /**< the masterbranch constraint of the parent node */
   SCIP_CONS*         child1cons;            /**< the masterbranch constraint of the first child node */
   SCIP_CONS*         child2cons;            /**< the masterbranch constraint of the second child node */
   SCIP_CONS*         origcons;              /**< the corresponding origbranch cons in the original program */

   GCG_BRANCHDATA*    branchdata;
   SCIP_BRANCHRULE*   branchrule;
   
   SCIP_VAR**         boundchgvars;
   SCIP_Real*         newbounds;
   SCIP_Real*         oldbounds;
   SCIP_BOUNDTYPE*    boundtypes;
   int*               nboundchangestreated;
   int                nboundchanges;
   int                nactivated;
   char*              name;
};

/** constraint handler data */
struct SCIP_ConshdlrData
{
   SCIP_CONS**        stack;                 /**< stack for storing active constraints */
   int                nstack;                /**< number of elements on the stack */
   int                maxstacksize;          /**< maximum size of the stack */
   SCIP_VAR**         pendingvars;
   SCIP_BOUNDTYPE*    pendingbndtypes;
   SCIP_Real*         pendingnewbnds;
   SCIP_Real*         pendingoldbnds;
   int                npendingbnds;
   SCIP_Bool          pendingbndsactivated;
   int                maxpendingbnds;
};

#ifdef CHECKPROPAGATEDVARS
/*
 * Local methods
 */
static
SCIP_Bool checkVars(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONSHDLR*        conshdlr,           /**< data structure of the masterbranch constraint handler */
   SCIP_Bool             printall            /**< should all violations be printed or only the first one? */
   )
{
   SCIP_CONSHDLRDATA* conshdlrData;  
   SCIP_CONS*         cons;
   SCIP_CONSDATA*     consdata;
   SCIP_VAR**         vars;
   int                nvars;
   SCIP_VARDATA*      vardata;
   SCIP*              origscip;
   int                i;
   int                j;
   int                c;

   assert(conshdlr != NULL); 
   conshdlrData = SCIPconshdlrGetData(conshdlr);
   assert(conshdlrData != NULL);
   assert(conshdlrData->stack != NULL);

   origscip = GCGpricerGetOrigprob(scip);
   assert(origscip != NULL);

   vars = SCIPgetVars(scip);
   assert(vars != NULL);
   nvars = SCIPgetNVars(scip);

   printf("checkVars()\n");

   /* first of all, check whether variables not fixed to 0 are really valid for the current node */
   /* iterate over all constraints */
   for ( c = 0; c < conshdlrData->nstack; c++ )
   {
      cons = conshdlrData->stack[c];
      consdata = SCIPconsGetData(cons);

      if ( consdata->branchrule == NULL )
         continue;
           
      /* iterate over all vars and check whether they violate the current cons */
      for ( i = 0; i < nvars; i++)
      {
         if ( !SCIPisFeasZero(scip, SCIPvarGetUbLocal(vars[i])) )
         {
            vardata = SCIPvarGetData(vars[i]);
            assert(vardata != NULL);
            assert(vardata->vartype == GCG_VARTYPE_MASTER);
            assert(vardata->blocknr >= -1 && vardata->blocknr < GCGrelaxGetNPricingprobs(origscip));
            assert(vardata->data.mastervardata.norigvars >= 0);
            assert(vardata->data.mastervardata.origvars != NULL || vardata->data.mastervardata.norigvars == 0);
            assert(vardata->data.mastervardata.origvals != NULL || vardata->data.mastervardata.norigvars == 0);
            
            for ( j = 0; j < vardata->data.mastervardata.norigvars; j++ )
            {
               if ( vardata->data.mastervardata.origvars[j] == consdata->origvar )
               {
                  if ( consdata->conssense == GCG_CONSSENSE_GE && 
                     SCIPisFeasLT(scip, vardata->data.mastervardata.origvals[j], consdata->val) )
                  {
                     printf("var %s: upper bound should be fixed to 0 because of cons %s [c=%d], but it is not!\n", SCIPvarGetName(vars[i]), SCIPconsGetName(cons), c);
                     printf("--> Reason: origvars[j] = %s >= origvals[j] = %g violated!\n",
                        SCIPvarGetName(vardata->data.mastervardata.origvars[j]), vardata->data.mastervardata.origvals[j]);
                     if ( !printall )
                        return FALSE;
                  }
                  if ( consdata->conssense == GCG_CONSSENSE_LE && 
                     SCIPisFeasGT(scip, vardata->data.mastervardata.origvals[j], consdata->val) )
                  {
                     printf("var %s: upper bound should be fixed to 0 because of cons %s [c=%d], but it is not!\n", SCIPvarGetName(vars[i]), SCIPconsGetName(cons), c);
                     printf("--> Reason: origvars[j] = %s <= origvals[j] = %g violated!\n",
                        SCIPvarGetName(vardata->data.mastervardata.origvars[j]), vardata->data.mastervardata.origvals[j]);                       
                     if ( !printall )
                        return FALSE;
                  }
                  
               }
               
            }
         }
      }
   }
   /* now check for all variables fixed to 0, whether there is a reason for this fixing active at the current node */

   return TRUE;   
}
#endif


/*
 * Callback methods
 */


/** destructor of constraint handler to free constraint handler data (called when SCIP is exiting) */
static
SCIP_DECL_CONSFREE(consFreeMasterbranch)
{
   SCIP_CONSHDLRDATA* conshdlrData;

   assert(scip != NULL);
   assert(conshdlr != NULL);
   assert(strcmp(SCIPconshdlrGetName(conshdlr), CONSHDLR_NAME) == 0);

   conshdlrData = SCIPconshdlrGetData(conshdlr);
   assert(conshdlrData != NULL);

   SCIPdebugMessage("freeing masterbranch constraint handler\n");

   /* free constraint handler storage */
   assert(conshdlrData->stack == NULL);
   SCIPfreeMemory(scip, &conshdlrData);

   return SCIP_OKAY;
}


/** solving process initialization method of constraint handler (called when branch and bound process is about to begin) */
static
SCIP_DECL_CONSINITSOL(consInitsolMasterbranch)
{
   SCIP_CONSHDLRDATA* conshdlrData;
   SCIP_CONS* cons;

   assert(scip != NULL);
   assert(conshdlr != NULL);
   assert(strcmp(SCIPconshdlrGetName(conshdlr), CONSHDLR_NAME) == 0);

   conshdlrData = SCIPconshdlrGetData(conshdlr);
   assert(conshdlrData != NULL);

   /* prepare stack */
   SCIP_CALL( SCIPallocMemoryArray(scip, &conshdlrData->stack, conshdlrData->maxstacksize) );
   conshdlrData->nstack = 0;


   /* prepare pending bound changes */
   conshdlrData->npendingbnds = 0;
   conshdlrData->maxpendingbnds = 5;
   conshdlrData->pendingbndsactivated = FALSE;
   SCIP_CALL( SCIPallocMemoryArray(scip, &(conshdlrData->pendingvars), conshdlrData->maxpendingbnds) );
   SCIP_CALL( SCIPallocMemoryArray(scip, &(conshdlrData->pendingbndtypes), conshdlrData->maxpendingbnds) );
   SCIP_CALL( SCIPallocMemoryArray(scip, &(conshdlrData->pendingoldbnds), conshdlrData->maxpendingbnds) );
   SCIP_CALL( SCIPallocMemoryArray(scip, &(conshdlrData->pendingnewbnds), conshdlrData->maxpendingbnds) );


   SCIPdebugMessage("consInitsolMasterbranch()\n");

   assert(SCIPgetRootNode(scip) != NULL);

   SCIP_CALL( GCGcreateConsMasterbranch(scip, &cons, SCIPgetRootNode(scip), NULL) );

   SCIP_CALL( SCIPaddConsNode(scip, SCIPgetRootNode(scip), cons, SCIPgetRootNode(scip)) );

   /* release constraints */
   SCIP_CALL( SCIPreleaseCons(scip, &cons) );

   return SCIP_OKAY;
}


/** solving process deinitialization method of constraint handler (called before branch and bound process data is freed) */
static
SCIP_DECL_CONSEXITSOL(consExitsolMasterbranch)
{
   SCIP_CONSHDLRDATA* conshdlrData;

   assert(scip != NULL);
   assert(conshdlr != NULL);
   assert(strcmp(SCIPconshdlrGetName(conshdlr), CONSHDLR_NAME) == 0);

   conshdlrData = SCIPconshdlrGetData(conshdlr);
   assert(conshdlrData != NULL);
   assert(conshdlrData->nstack == 1);
   SCIPdebugMessage("exiting masterbranch constraint handler\n");

   /* free stack */
   SCIPfreeMemoryArray(scip, &conshdlrData->stack);
   SCIPfreeMemoryArray(scip, &conshdlrData->pendingvars);
   SCIPfreeMemoryArray(scip, &conshdlrData->pendingbndtypes);
   SCIPfreeMemoryArray(scip, &conshdlrData->pendingoldbnds);
   SCIPfreeMemoryArray(scip, &conshdlrData->pendingnewbnds);

   return SCIP_OKAY;
}


/** frees specific constraint data */
static
SCIP_DECL_CONSDELETE(consDeleteMasterbranch)
{
   SCIP_CONSHDLRDATA* conshdlrData;
   SCIP_CONSDATA* consdata2;

   assert(scip != NULL);
   assert(conshdlr != NULL);
   assert(cons != NULL);
   assert(consdata != NULL);
   assert(strcmp(SCIPconshdlrGetName(conshdlr), CONSHDLR_NAME) == 0);
   assert(*consdata != NULL);

   conshdlrData = SCIPconshdlrGetData(conshdlr);

   SCIPdebugMessage("Deleting masterbranch constraint: <%s>.\n", (*consdata)->name);

   /* set the mastercons pointer of the corresponding origcons to NULL */
   if ( (*consdata)->origcons != NULL )
      GCGconsOrigbranchSetMastercons((*consdata)->origcons, NULL);
   /* set the pointer in parents and children to NULL */
   if ( (*consdata)->parentcons != NULL )
   {
      consdata2 = SCIPconsGetData((*consdata)->parentcons);
      if ( consdata2->child1cons == cons )
      {
         consdata2->child1cons = NULL;
      }
      else
      {
         assert(consdata2->child2cons == cons);
         consdata2->child2cons = NULL;
      }
   }
   assert((*consdata)->child1cons == NULL);
   assert((*consdata)->child2cons == NULL);

   /* delete branchdata, if the corresponding origcons was already deleted */
   if ( (*consdata)->origcons == NULL && (*consdata)->branchdata != NULL )
   {
      SCIP_CALL( GCGrelaxBranchDataDelete(GCGpricerGetOrigprob(scip), (*consdata)->branchrule, &(*consdata)->branchdata) );
   }

   /* delete array with bound changes */
   if ( (*consdata)->nboundchanges > 0 )
   {
      SCIPfreeMemoryArray(scip, &(*consdata)->oldbounds);
      SCIPfreeMemoryArray(scip, &(*consdata)->newbounds);
      SCIPfreeMemoryArray(scip, &(*consdata)->boundtypes);
      SCIPfreeMemoryArray(scip, &(*consdata)->boundchgvars);
   }

   if ( (*consdata)->nboundchangestreated != NULL )
   {
      SCIPfreeMemoryArray(scip, &(*consdata)->nboundchangestreated);
   }



   /* free constraint data */
   if ( (*consdata)->name != NULL )
   {
      BMSfreeBlockMemoryArray(SCIPblkmem(scip), &(*consdata)->name, strlen((*consdata)->name)+1);
   }
   SCIPfreeBlockMemory(scip, consdata);

   return SCIP_OKAY;
}


/** constraint activation notification method of constraint handler */
static
SCIP_DECL_CONSACTIVE(consActiveMasterbranch)
{
   SCIP* origscip;
   SCIP_CONSHDLRDATA* conshdlrData;
   SCIP_CONSDATA* consdata;
   SCIP_CONS* origcons;

   assert(scip != NULL);
   assert(conshdlr != NULL);
   assert(strcmp(SCIPconshdlrGetName(conshdlr), CONSHDLR_NAME) == 0);
   assert(cons != NULL);

   conshdlrData = SCIPconshdlrGetData(conshdlr);
   assert(conshdlrData != NULL);
   assert(conshdlrData->stack != NULL);

   consdata = SCIPconsGetData(cons);
   assert(consdata != NULL);
   assert(consdata->node != NULL);

   origscip = GCGpricerGetOrigprob(scip);
   assert(origscip != NULL);

   consdata->nactivated++;

   assert(SCIPgetNVars(scip) >= consdata->propagatedvars);
   if ( SCIPgetNVars(scip) > consdata->propagatedvars )
   {
      //consdata->needprop = TRUE;
      //SCIP_CALL( SCIPrepropagateNode(scip, consdata->node) );
   }

   if ( !consdata->created )
   {
      origcons = GCGconsOrigbranchGetActiveCons(origscip);
      assert(origcons != NULL);

      consdata->origcons = origcons;
      consdata->branchrule = GCGconsOrigbranchGetBranchrule(origcons);
      consdata->branchdata = GCGconsOrigbranchGetBranchdata(origcons);
      GCGconsOrigbranchSetMastercons(origcons, cons);

      SCIP_ALLOC( BMSduplicateBlockMemoryArray(SCIPblkmem(scip), &consdata->name, SCIPconsGetName(consdata->origcons), 
            strlen(SCIPconsGetName(consdata->origcons))+1) );

      assert(SCIPgetCurrentNode(scip) == consdata->node || consdata->node == SCIPgetRootNode(scip));
      assert((SCIPgetNNodesLeft(scip)+SCIPgetNNodes(scip) == 1) == (consdata->node == SCIPgetRootNode(scip)));
      assert(SCIPnodeGetDepth(GCGconsOrigbranchGetNode(consdata->origcons)) == SCIPnodeGetDepth(consdata->node));
      assert(consdata->parentcons != NULL || SCIPnodeGetDepth(consdata->node) == 0);
      assert(consdata->parentcons == NULL || 
         SCIPconsGetData(consdata->parentcons)->origcons == GCGconsOrigbranchGetParentcons(consdata->origcons));

      consdata->created = TRUE;
   }

   /* put constraint on the stack */
   if ( conshdlrData->nstack >= conshdlrData->maxstacksize )
   {
      SCIPreallocMemoryArray(scip, &(conshdlrData->stack), 2*(conshdlrData->maxstacksize));
      conshdlrData->maxstacksize = 2*(conshdlrData->maxstacksize);
      SCIPdebugMessage("reallocating Memory for stack! %d --> %d\n", conshdlrData->maxstacksize/2, conshdlrData->maxstacksize);
   }
   conshdlrData->stack[conshdlrData->nstack] = cons;
   (conshdlrData->nstack)++;

   SCIPdebugMessage("Activating masterbranch constraint: <%s> [stack size: %d], needprop = %d.\n", 
      consdata->name, conshdlrData->nstack, consdata->needprop);

   /* call branching specific activation method */
   if ( consdata->branchrule != NULL )
   {
      SCIP_CALL( GCGrelaxBranchActiveMaster(origscip, consdata->branchrule, consdata->branchdata) );
   }

   return SCIP_OKAY;
}



/** constraint deactivation notification method of constraint handler */
static
SCIP_DECL_CONSDEACTIVE(consDeactiveMasterbranch)
{
   SCIP_CONSHDLRDATA* conshdlrData;
   SCIP_CONSDATA* consdata;
   SCIP* origscip;

   assert(scip != NULL);
   assert(conshdlr != NULL);
   assert(strcmp(SCIPconshdlrGetName(conshdlr), CONSHDLR_NAME) == 0);
   assert(cons != NULL);

   conshdlrData = SCIPconshdlrGetData(conshdlr);
   assert(conshdlrData != NULL);
   assert(conshdlrData->stack != NULL || conshdlrData->nstack == 1);
   assert(conshdlrData->nstack > 0);
   assert(conshdlrData->nstack == 1 || cons == conshdlrData->stack[conshdlrData->nstack-1]);

   consdata = SCIPconsGetData(cons);
   assert(consdata != NULL);
   assert(consdata->created);

   origscip = GCGpricerGetOrigprob(scip);
   assert(origscip != NULL);

   if ( SCIPgetStage(scip) == SCIP_STAGE_SOLVING )
      consdata->propagatedvars = SCIPgetNVars(scip);

   /* remove constraint from the stack */
   (conshdlrData->nstack)--;


   SCIPdebugMessage("Deactivating masterbranch constraint: <%s> [stack size: %d].\n", 
      consdata->name, conshdlrData->nstack);

   /* call branching specific deactivation method */
   if ( consdata->branchrule != NULL )
   {
      SCIP_CALL( GCGrelaxBranchDeactiveMaster(GCGpricerGetOrigprob(scip), consdata->branchrule, consdata->branchdata) );
   }

   return SCIP_OKAY;
}



/** domain propagation method of constraint handler */
static
SCIP_DECL_CONSPROP(consPropMasterbranch)
{
   SCIP* origscip;
   SCIP_CONSHDLRDATA* conshdlrData;  
   SCIP_CONS* cons;
   SCIP_CONSDATA* consdata;
   SCIP_VARDATA* vardata;
   SCIP_VARDATA* pricingvardata;
   SCIP_Real val;

   SCIP_VAR** mastervars;
   int nmastervars;
   SCIP_VAR** pricingvars;
   int npricingvars;

   int propcount;
   int i;
   int j;
   int k;
   SCIP_Bool fixed;

   assert(conshdlr != NULL); 
   conshdlrData = SCIPconshdlrGetData(conshdlr);
   assert(conshdlrData != NULL);
   assert(conshdlrData->stack != NULL);

   origscip = GCGpricerGetOrigprob(scip);
   assert(origscip != NULL);

   *result = SCIP_DIDNOTRUN;

   /* the constraint data of the cons related to the current node */
   cons = conshdlrData->stack[conshdlrData->nstack-1];
   assert(cons != NULL);

   consdata = SCIPconsGetData(cons);
   assert(consdata != NULL);

   if ( consdata->parentcons == NULL || !consdata->needprop )
   {
#ifdef CHECKPROPAGATEDVARS
      SCIP_Bool consistent;
      consistent = checkVars(scip, conshdlr, TRUE);
      assert(consistent);
#endif
      *result = SCIP_DIDNOTRUN;
      return SCIP_OKAY;
   }

   SCIPdebugMessage("Starting propagation of masterbranch constraint: <%s>, stack size = %d.\n", 
      consdata->name, conshdlrData->nstack);

   *result = SCIP_DIDNOTFIND;

   propcount = 0;

   mastervars = SCIPgetVars(scip);
   nmastervars = SCIPgetNVars(scip);

   /* iterate over all master variables */
   for ( i = 0; i < nmastervars; i++)
   {
      vardata = SCIPvarGetData(mastervars[i]);
      assert(vardata != NULL);
      assert(vardata->vartype == GCG_VARTYPE_MASTER);
      assert(vardata->blocknr >= -1 && vardata->blocknr < GCGrelaxGetNPricingprobs(origscip));
      assert(vardata->data.mastervardata.norigvars >= 0);
      assert(vardata->data.mastervardata.origvars != NULL || vardata->data.mastervardata.norigvars == 0);
      assert(vardata->data.mastervardata.origvals != NULL || vardata->data.mastervardata.norigvars == 0);
      assert(vardata->blocknr != -1 || vardata->data.mastervardata.norigvars == 2 );

      fixed = FALSE;

      /* only look at variables not already fixed to 0 */
      if ( (SCIPisFeasZero(scip, SCIPvarGetUbLocal(mastervars[i]))) && vardata->blocknr != -1 )
         continue;

      /* the variable was copied from original to master */
      if ( vardata->blocknr == -1 )
      {
         assert(vardata->data.mastervardata.norigvars == 2);
         assert(SCIPisFeasEQ(scip, vardata->data.mastervardata.origvals[0], 1.0));
         assert(SCIPisFeasEQ(scip, vardata->data.mastervardata.origvals[1], 0.0));
         assert(vardata->data.mastervardata.origvars[0] != NULL);
         assert(vardata->data.mastervardata.origvars[0] == vardata->data.mastervardata.origvars[1]);

         if ( SCIPisLT(scip, SCIPvarGetLbLocal(mastervars[i]), SCIPvarGetLbLocal(vardata->data.mastervardata.origvars[0])) )
         {
            SCIP_CALL( SCIPchgVarLb(scip, mastervars[i], SCIPvarGetLbLocal(vardata->data.mastervardata.origvars[0])) );
            propcount++;
         }
         if ( SCIPisGT(scip, SCIPvarGetUbLocal(mastervars[i]), SCIPvarGetUbLocal(vardata->data.mastervardata.origvars[0])) )
         {
            SCIP_CALL( SCIPchgVarUb(scip, mastervars[i], SCIPvarGetUbLocal(vardata->data.mastervardata.origvars[0])) );
            propcount++;
         }
      }
      else
      {
         assert(!SCIPisFeasZero(scip, SCIPvarGetUbLocal(mastervars[i])));
         /* iterate over all variables of the corresponding pricing problem */
         pricingvars = SCIPgetVars(GCGrelaxGetPricingprob(origscip, vardata->blocknr));
         npricingvars = SCIPgetNVars(GCGrelaxGetPricingprob(origscip, vardata->blocknr));

         for ( k = 0; k < vardata->data.mastervardata.norigvars; k++ )
         {
            //printf("k = %d: var %s\n", k, SCIPvarGetName(vardata->data.mastervardata.origvars[k]));
         }

         k = 0; /* index in origvars array of master variable */
         for ( j = 0; j < npricingvars && !fixed; j++ )
         {
            pricingvardata = SCIPvarGetData(pricingvars[j]);
            assert(pricingvardata != NULL);
            assert(pricingvardata->vartype == GCG_VARTYPE_PRICING);
            assert(pricingvardata->blocknr == vardata->blocknr);
            assert(pricingvardata->data.pricingvardata.origvars != NULL);
            assert(pricingvardata->data.pricingvardata.norigvars > 0);
            assert(pricingvardata->data.pricingvardata.origvars[0] != NULL);

            //printf("j = %d: var %s\n", j, SCIPvarGetName(pricingvardata->data.pricingvardata.origvars[0]));

            if ( k < vardata->data.mastervardata.norigvars &&
               pricingvardata->data.pricingvardata.origvars[0] == vardata->data.mastervardata.origvars[k] )
            {
               //printf("vars are equal for j = %d, k = %d\n", j, k);
               val = vardata->data.mastervardata.origvals[k];
               k++;
            }
            else
            {
               val = 0.0;
            }

            /* check lower bound */
            if ( SCIPisFeasLT(scip, val, SCIPvarGetLbLocal(pricingvardata->data.pricingvardata.origvars[0])) )
            {
               SCIP_CALL( SCIPchgVarUb(scip, mastervars[i], 0.0) );
               propcount++;
               fixed = TRUE;
               break;
            }
            /* check upper bound */
            if ( SCIPisFeasGT(scip, val, SCIPvarGetUbLocal(pricingvardata->data.pricingvardata.origvars[0])) )
            {
               SCIP_CALL( SCIPchgVarUb(scip, mastervars[i], 0.0) );
               propcount++;
               fixed = TRUE;
               break;
            }
         }
         assert(j < npricingvars || k == vardata->data.mastervardata.norigvars );

      }
   }

   /* update bounds in the pricing problems */
   for ( i = 0; i < GCGrelaxGetNPricingprobs(origscip); i++ )
   {
      pricingvars = SCIPgetVars(GCGrelaxGetPricingprob(origscip, i));
      npricingvars = SCIPgetNVars(GCGrelaxGetPricingprob(origscip, i));

      for ( j = 0; j < npricingvars ; j++ )
      {
         pricingvardata = SCIPvarGetData(pricingvars[j]);
         assert(pricingvardata != NULL);
         assert(pricingvardata->vartype == GCG_VARTYPE_PRICING);
         assert(pricingvardata->blocknr == i );
         assert(pricingvardata->data.pricingvardata.origvars != NULL);
         assert(pricingvardata->data.pricingvardata.norigvars > 0);
         assert(pricingvardata->data.pricingvardata.origvars[0] != NULL);

         /* set bounds of variables in the pricing problem to the bounds of the original variables */
         if ( SCIPisGT(scip, SCIPvarGetLbLocal(pricingvardata->data.pricingvardata.origvars[0]),
               SCIPvarGetUbLocal(pricingvars[j])) )
         {
            SCIP_CALL( SCIPchgVarUb(GCGrelaxGetPricingprob(origscip, i), pricingvars[j], 
                  SCIPvarGetUbLocal(pricingvardata->data.pricingvardata.origvars[0])) );
            SCIP_CALL( SCIPchgVarLb(GCGrelaxGetPricingprob(origscip, i), pricingvars[j], 
                  SCIPvarGetLbLocal(pricingvardata->data.pricingvardata.origvars[0])) );
         }
         else
         {
            SCIP_CALL( SCIPchgVarLb(GCGrelaxGetPricingprob(origscip, i), pricingvars[j], 
                  SCIPvarGetLbLocal(pricingvardata->data.pricingvardata.origvars[0])) );
            SCIP_CALL( SCIPchgVarUb(GCGrelaxGetPricingprob(origscip, i), pricingvars[j], 
                  SCIPvarGetUbLocal(pricingvardata->data.pricingvardata.origvars[0])) );
         }
      }
   }
      
   if ( consdata->branchrule != NULL )
   {
      SCIP_CALL( GCGrelaxBranchPropMaster(GCGpricerGetOrigprob(scip), consdata->branchrule, consdata->branchdata, result) );
   }

   SCIPdebugMessage("Finished propagation of masterbranch constraint: %d vars fixed.\n", propcount);

   if ( *result != SCIP_CUTOFF )
      if ( propcount > 0 )
         *result = SCIP_REDUCEDDOM;

   consdata->needprop = FALSE;
   consdata->propagatedvars = SCIPgetNVars(scip);

#ifdef CHECKPROPAGATEDVARS
   {
      SCIP_Bool consistent;
      consistent = checkVars(scip, conshdlr, TRUE);
      assert(consistent);
   }
#endif

   return SCIP_OKAY;
}

/* define not used callbacks as NULL */
#define consEnfolpMasterbranch NULL
#define consEnfopsMasterbranch NULL
#define consCheckMasterbranch NULL
#define consLockMasterbranch NULL
#define consPresolMasterbranch NULL
#define consRespropMasterbranch NULL
#define consInitMasterbranch NULL
#define consExitMasterbranch NULL
#define consInitpreMasterbranch NULL
#define consExitpreMasterbranch NULL
#define consTransMasterbranch NULL
#define consInitlpMasterbranch NULL
#define consSepalpMasterbranch NULL
#define consSepasolMasterbranch NULL
#define consEnableMasterbranch NULL
#define consDisableMasterbranch NULL
#define consPrintMasterbranch NULL
#define consCopyMasterbranch NULL
#define consParseMasterbranch NULL


/*
 * interface methods
 */


/** creates the handler for masterbranch constraints and includes it in SCIP */
SCIP_RETCODE SCIPincludeConshdlrMasterbranch(
   SCIP*                 scip                /**< SCIP data structure */
   )
{
   SCIP_CONSHDLRDATA* conshdlrData;

   SCIPdebugMessage("Including masterbranch constraint handler.\n");

   SCIP_CALL( SCIPallocMemory(scip, &conshdlrData) );
   conshdlrData->stack = NULL;
   conshdlrData->nstack = 0;
   conshdlrData->maxstacksize = 25;

   /* include constraint handler */
   SCIP_CALL( SCIPincludeConshdlr(scip, CONSHDLR_NAME, CONSHDLR_DESC,
         CONSHDLR_SEPAPRIORITY, CONSHDLR_ENFOPRIORITY, CONSHDLR_CHECKPRIORITY,
         CONSHDLR_SEPAFREQ, CONSHDLR_PROPFREQ, CONSHDLR_EAGERFREQ, CONSHDLR_MAXPREROUNDS,
         CONSHDLR_DELAYSEPA, CONSHDLR_DELAYPROP, CONSHDLR_DELAYPRESOL, CONSHDLR_NEEDSCONS,
         consFreeMasterbranch, consInitMasterbranch, consExitMasterbranch,
         consInitpreMasterbranch, consExitpreMasterbranch, consInitsolMasterbranch, consExitsolMasterbranch,
         consDeleteMasterbranch, consTransMasterbranch, consInitlpMasterbranch,
         consSepalpMasterbranch, consSepasolMasterbranch, consEnfolpMasterbranch, consEnfopsMasterbranch, consCheckMasterbranch,
         consPropMasterbranch, consPresolMasterbranch, consRespropMasterbranch, consLockMasterbranch,
         consActiveMasterbranch, consDeactiveMasterbranch,
         consEnableMasterbranch, consDisableMasterbranch,
         consPrintMasterbranch, consCopyMasterbranch, consParseMasterbranch, 
         conshdlrData) );

   return SCIP_OKAY;
}


/** creates and captures a masterbranch constraint*/
SCIP_RETCODE GCGcreateConsMasterbranch(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONS**           cons,               /**< pointer to hold the created constraint */
   SCIP_NODE*            node,
   SCIP_CONS*            parentcons
   )
{
   SCIP_CONSHDLR* conshdlr;
   SCIP_CONSDATA* consdata;

   assert(scip != NULL);
   assert(node != NULL);
   assert((parentcons == NULL) == (SCIPnodeGetDepth(node) == 0));


   /* find the masterbranch constraint handler */
   conshdlr = SCIPfindConshdlr(scip, CONSHDLR_NAME);
   if ( conshdlr == NULL )
   {
      SCIPerrorMessage("masterbranch constraint handler not found\n");
      return SCIP_PLUGINNOTFOUND;
   }

   /* create constraint data */
   SCIP_CALL( SCIPallocBlockMemory(scip, &consdata) );

   consdata->propagatedvars = 0;
   consdata->needprop = TRUE;

   consdata->node = node;
   consdata->parentcons = parentcons;
   consdata->child1cons = NULL;
   consdata->child2cons = NULL;
   consdata->created = FALSE;
   consdata->origcons = NULL;
   consdata->name = NULL;

   consdata->branchrule = NULL;
   consdata->branchdata = NULL;

   consdata->boundchgvars = NULL;
   consdata->boundtypes = NULL;
   consdata->newbounds = NULL;
   consdata->oldbounds = NULL;
   consdata->nboundchangestreated = NULL;
   consdata->nboundchanges = 0;
   consdata->nactivated = 0;
   

   SCIPdebugMessage("Creating masterbranch constraint.\n");

   /* create constraint */
   SCIP_CALL( SCIPcreateCons(scip, cons, "masterbranch", conshdlr, consdata, FALSE, FALSE, FALSE, FALSE, TRUE,
         TRUE, FALSE, FALSE, FALSE, TRUE) );

   if ( parentcons != NULL )
   {      
      SCIP_CONSDATA* parentdata;

      parentdata = SCIPconsGetData(parentcons);
      assert(parentdata != NULL);

      if ( parentdata->child1cons == NULL )
      {
         parentdata->child1cons = *cons;
      }
      else
      {
         assert(parentdata->child2cons == NULL);
         parentdata->child2cons = *cons;
      }
   }

   return SCIP_OKAY;
}




/* ----------------------------------- external methods -------------------------- */

/** returns the masterbranch constraint of the current node */
SCIP_CONS* GCGconsMasterbranchGetActiveCons(
   SCIP*                 scip                /**< SCIP data structure */
   )
{
   SCIP_CONSHDLR*     conshdlr;
   SCIP_CONSHDLRDATA* conshdlrData;

   assert(scip != NULL);
   conshdlr = SCIPfindConshdlr(scip, CONSHDLR_NAME);
   if ( conshdlr == NULL )
   {
      SCIPerrorMessage("masterbranch constraint handler not found\n");
      return NULL;
   }
   conshdlrData = SCIPconshdlrGetData(conshdlr);
   assert(conshdlrData != NULL);
   assert(conshdlrData->stack != NULL);
   assert(conshdlrData->nstack > 0);

   return conshdlrData->stack[conshdlrData->nstack-1];
}


/** returns the stack and the number of elements on it */
void GCGconsMasterbranchGetStack(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONS***          stack,              /**< return value: pointer to the stack */
   int*                  nstackelements      /**< return value: pointer to int, for number of elements on the stack */
   )
{
   SCIP_CONSHDLR*     conshdlr;
   SCIP_CONSHDLRDATA* conshdlrData;

   assert(scip != NULL);
   conshdlr = SCIPfindConshdlr(scip, CONSHDLR_NAME);
   if ( conshdlr == NULL )
   {
      SCIPerrorMessage("masterbranch constraint handler not found\n");
      return;
   }   
   conshdlrData = SCIPconshdlrGetData(conshdlr);
   assert(conshdlrData != NULL);
   assert(conshdlrData->stack != NULL);

   *stack = conshdlrData->stack;
   *nstackelements = conshdlrData->nstack;
}

/** returns the number of elements on the stack */
int GCGconsMasterbranchGetNStackelements(
   SCIP*                 scip                /**< SCIP data structure */
   )
{
   SCIP_CONSHDLR*     conshdlr;
   SCIP_CONSHDLRDATA* conshdlrData;

   assert(scip != NULL);
   conshdlr = SCIPfindConshdlr(scip, CONSHDLR_NAME);
   if ( conshdlr == NULL )
   {
      SCIPerrorMessage("masterbranch constraint handler not found\n");
      return -1;
   }   
   conshdlrData = SCIPconshdlrGetData(conshdlr);
   assert(conshdlrData != NULL);
   assert(conshdlrData->stack != NULL);

   return conshdlrData->nstack;
}

/** returns the branching data for a given masterbranch constraint */
GCG_BRANCHDATA* GCGconsMasterbranchGetBranchdata(
   SCIP_CONS*            cons
   )
{
   SCIP_CONSDATA* consdata;

   consdata = SCIPconsGetData(cons);
   assert(consdata != NULL);

   return consdata->branchdata;
}

/** returns the node in the B&B tree at which the given masterbranch constraint is sticking */
SCIP_NODE* GCGconsMasterbranchGetNode(
   SCIP_CONS*            cons
   )
{
   SCIP_CONSDATA* consdata;

   consdata = SCIPconsGetData(cons);
   assert(consdata != NULL);

   return consdata->node;
}

/** returns the masterbranch constraint of the B&B father of the node at which the 
    given masterbranch constraint is sticking */
SCIP_CONS* GCGconsMasterbranchGetParentcons(
   SCIP_CONS*            cons
   )
{
   SCIP_CONSDATA* consdata;

   consdata = SCIPconsGetData(cons);
   assert(consdata != NULL);

   return consdata->parentcons;
}

/** returns the masterbranch constraint of the first child of the node at which the 
    given masterbranch constraint is sticking */
SCIP_CONS* GCGconsMasterbranchGetChild1cons(
   SCIP_CONS*            cons
   )
{
   SCIP_CONSDATA* consdata;

   consdata = SCIPconsGetData(cons);
   assert(consdata != NULL);

   return consdata->child1cons;
}

/** returns the masterbranch constraint of the second child of the node at which the 
    given masterbranch constraint is sticking */
SCIP_CONS* GCGconsMasterbranchGetChild2cons(
   SCIP_CONS*            cons
   )
{
   SCIP_CONSDATA* consdata;

   consdata = SCIPconsGetData(cons);
   assert(consdata != NULL);

   return consdata->child2cons;
}

/** returns the origbranch constraint of the node in the original program corresponding to the node 
    at which the given masterbranch constraint is sticking */
SCIP_CONS* GCGconsMasterbranchGetOrigcons(
   SCIP_CONS*            cons
   )
{
   SCIP_CONSDATA* consdata;

   consdata = SCIPconsGetData(cons);
   assert(consdata != NULL);

   return consdata->origcons;
}

/** sets the origbranch constraint of the node in the master program corresponding to the node 
    at which the given masterbranchbranch constraint is sticking */
void GCGconsMasterbranchSetOrigcons(
   SCIP_CONS*            cons,
   SCIP_CONS*            origcons
   )
{
   SCIP_CONSDATA* consdata;

   consdata = SCIPconsGetData(cons);
   assert(consdata != NULL);
   assert(consdata->origcons == NULL || origcons == NULL);

   consdata->origcons = origcons;
}


/** checks the consistency of the masterbranch constraints in the problem */
void GCGconsMasterbranchCheckConsistency(
   SCIP*                 scip
   )
{
   SCIP_CONSHDLR*     conshdlr;
   SCIP_CONS** conss;
   SCIP_CONSDATA* consdata;
   int nconss;
   int i;

   if ( scip == NULL )
      return;

   assert(scip != NULL);
   conshdlr = SCIPfindConshdlr(scip, CONSHDLR_NAME);
   if ( conshdlr == NULL )
   {
      SCIPerrorMessage("masterbranch constraint handler not found\n");
      assert(0);
      return;
   }   

   conss = SCIPconshdlrGetConss(conshdlr);
   nconss = SCIPconshdlrGetNConss(conshdlr);

   for ( i = 0; i < nconss; i++ )
   {
      consdata = SCIPconsGetData(conss[i]);
      assert(consdata != NULL);
      assert(consdata->node != NULL);
      assert((consdata->parentcons == NULL) == (SCIPnodeGetDepth(consdata->node) == 0));
      assert(consdata->origcons == NULL || consdata->created);
      assert(consdata->parentcons == NULL || SCIPconsGetData(consdata->parentcons)->child1cons == conss[i]
         || SCIPconsGetData(consdata->parentcons)->child2cons == conss[i]);
      assert(consdata->child1cons == NULL || SCIPconsGetData(consdata->child1cons)->parentcons == conss[i]);
      assert(consdata->child2cons == NULL || SCIPconsGetData(consdata->child2cons)->parentcons == conss[i]);
      assert(consdata->origcons == NULL || 
         GCGconsOrigbranchGetMastercons(consdata->origcons) == conss[i]);
   }

   SCIPdebugMessage("checked consistency of %d masterbranch constraints, all ok!\n", nconss);
}
