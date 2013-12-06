/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                           */
/*                  This file is part of the program                         */
/*          GCG --- Generic Colum Generation                                 */
/*                  a Dantzig-Wolfe decomposition based extension            */
/*                  of the branch-cut-and-price framework                    */
/*         SCIP --- Solving Constraint Integer Programs                      */
/*                                                                           */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/**@file   branch_generic.c
 * @ingroup BRANCHINGRULES
 * @brief  branching rule based on vanderbeck's generic branching scheme
 * @author Marcel Schmickerath
 * @author Martin Bergner
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/
/* #define SCIP_DEBUG */
#include "branch_generic.h"
#include "relax_gcg.h"
#include "cons_masterbranch.h"
#include "cons_origbranch.h"
#include "pricer_gcg.h"
#include "scip/cons_linear.h"
#include "type_branchgcg.h"
#include "pub_gcgvar.h"

#include "scip/nodesel_bfs.h"
#include "scip/nodesel_dfs.h"
#include "scip/nodesel_estimate.h"
#include "scip/nodesel_hybridestim.h"
#include "scip/nodesel_restartdfs.h"
#include "scip/branch_allfullstrong.h"
#include "scip/branch_fullstrong.h"
#include "scip/branch_inference.h"
#include "scip/branch_mostinf.h"
#include "scip/branch_leastinf.h"
#include "scip/branch_pscost.h"
#include "scip/branch_random.h"
#include "scip/branch_relpscost.h"

#include <assert.h>
#include <string.h>


#define BRANCHRULE_NAME          "generic"
#define BRANCHRULE_DESC          "generic branching rule by Vanderbeck"
#define BRANCHRULE_PRIORITY      -100
#define BRANCHRULE_MAXDEPTH      -1
#define BRANCHRULE_MAXBOUNDDIST  1.0


#define EVENTHDLR_NAME         "genericbranchvaradd"
#define EVENTHDLR_DESC         "event handler for adding a new generated mastervar into the right branching constraints by using Vanderbecks generic branching scheme"


struct GCG_BranchData
{
   GCG_COMPSEQUENCE**   C;                  /**< S[k] bound sequence for block k */ /* !!! sort of each C[i] = S is important !!! */
   int*                 sequencesizes;      /**< number of bounds in S[k] */
   int                  Csize;
   SCIP_Real            lhs;
   SCIP_CONS*           mastercons;         /**< constraint enforcing the branching restriction in the master problem */
   GCG_COMPSEQUENCE*    consS;              /**< component bound sequence which induce the current branching constraint */
   int                  consSsize;
   int                  consblocknr;
};

/** set of component bounds in separate */
struct GCG_Record
{
   GCG_COMPSEQUENCE**   record;             /**< returnvalue of separate function */
   int                  recordsize;
   int*                 sequencesizes;
};
typedef struct GCG_Record GCG_RECORD;

/*
 * Callback methods
 */

/* define not used callback as NULL*/
#define branchFreeGeneric NULL
#define branchExitGeneric NULL
#define branchInitsolGeneric NULL
#define branchExitsolGeneric NULL

/** initialization method of event handler (called after problem was transformed) */
static
SCIP_DECL_EVENTINIT(eventInitGenericbranchvaradd)
{  /*lint --e{715}*/
    assert(scip != NULL);
    assert(eventhdlr != NULL);
    assert(strcmp(SCIPeventhdlrGetName(eventhdlr), EVENTHDLR_NAME) == 0);

    /* notify SCIP that your event handler wants to react on the event type */
    SCIP_CALL( SCIPcatchEvent( scip, SCIP_EVENTTYPE_VARADDED, eventhdlr, NULL, NULL) );

    return SCIP_OKAY;
}

/** deinitialization method of event handler (called before transformed problem is freed) */
static
SCIP_DECL_EVENTEXIT(eventExitGenericbranchvaradd)
{  /*lint --e{715}*/
    assert(scip != NULL);
    assert(eventhdlr != NULL);
    assert(strcmp(SCIPeventhdlrGetName(eventhdlr), EVENTHDLR_NAME) == 0);

    /* notify SCIP that your event handler wants to drop the event type */
    SCIP_CALL( SCIPdropEvent( scip, SCIP_EVENTTYPE_VARADDED, eventhdlr, NULL, -1) );

    return SCIP_OKAY;
}

/** execution method of event handler */
static
SCIP_DECL_EVENTEXEC(eventExecGenericbranchvaradd)
{  /*lint --e{715}*/
   SCIP* origscip;
   SCIP_CONS* masterbranchcons;
   SCIP_CONS* parentcons;
   SCIP_Bool varinS;
   SCIP_VAR* mastervar;
   SCIP_VAR** allorigvars;
   SCIP_VAR** mastervars;
   GCG_BRANCHDATA* branchdata;
   int p;
   int allnorigvars;
   int nmastervars;

   assert(eventhdlr != NULL);
   assert(strcmp(SCIPeventhdlrGetName(eventhdlr), EVENTHDLR_NAME) == 0);
   assert(event != NULL);
   assert(scip != NULL);
   assert(SCIPeventGetType(event) == SCIP_EVENTTYPE_VARADDED);

   varinS = TRUE;
   p = 0;
   mastervar = SCIPeventGetVar(event);
   if( !GCGvarIsMaster(mastervar) )
      return SCIP_OKAY;

   origscip = GCGpricerGetOrigprob(scip);
   assert(origscip != NULL);

   /*   SCIPdebugMessage("exec method of event_genericbranchvaradd\n"); */

   masterbranchcons = GCGconsMasterbranchGetActiveCons(scip);
   assert(masterbranchcons != NULL);

   /* if branch rule is not generic, abort */
   if( !GCGisBranchruleGeneric(GCGconsMasterbranchGetbranchrule(masterbranchcons)) )
      return SCIP_OKAY;

   SCIP_CALL( SCIPgetVarsData(origscip, &allorigvars, &allnorigvars, NULL, NULL, NULL, NULL) );
   SCIP_CALL( SCIPgetVarsData(scip, &mastervars, &nmastervars, NULL, NULL, NULL, NULL) );

   parentcons = masterbranchcons;
   branchdata = GCGconsMasterbranchGetBranchdata(parentcons);


   if( GCGvarIsMaster(mastervar) &&  (GCGconsMasterbranchGetbranchrule(parentcons) != NULL || GCGconsMasterbranchGetOrigbranchrule(parentcons) != NULL ) )
   {
      SCIPdebugMessage("Mastervar <%s>\n", SCIPvarGetName(mastervar));
      while( parentcons != NULL && branchdata != NULL
            && GCGbranchGenericBranchdataGetConsS(branchdata) != NULL && GCGbranchGenericBranchdataGetConsSsize(branchdata) > 0 )
      {
         SCIP_Bool blockfound;
         SCIP_VAR** pricingvars;
         int k;

         if( GCGconsMasterbranchGetbranchrule(parentcons) == NULL || strcmp(SCIPbranchruleGetName(GCGconsMasterbranchGetbranchrule(parentcons)), "generic") != 0 )
            break;

         if( GCGconsMasterbranchGetOrigbranchrule(parentcons) == NULL || strcmp(SCIPbranchruleGetName(GCGconsMasterbranchGetOrigbranchrule(parentcons)), "generic") != 0 )
            break;

         assert(branchdata != NULL);


         if( (GCGbranchGenericBranchdataGetConsblocknr(branchdata) != GCGvarGetBlock(mastervar) && GCGvarGetBlock(mastervar) != -1 )
               || (GCGvarGetBlock(mastervar) == -1 && !GCGvarIsLinking(mastervar)) )
         {
            parentcons = GCGconsMasterbranchGetParentcons(parentcons);

            if( parentcons != NULL )
               branchdata = GCGconsMasterbranchGetBranchdata(parentcons);

            continue;
         }

         blockfound = TRUE;

         if( GCGvarGetBlock(mastervar) == -1 )
         {
            assert( GCGvarIsLinking(mastervar) );
            blockfound = FALSE;

            pricingvars = GCGlinkingVarGetPricingVars(mastervar);
            assert(pricingvars != NULL );

            for( k=0; k<GCGlinkingVarGetNBlocks(mastervar); ++k )
            {
               if( pricingvars[k] != NULL )
               {
                  if( GCGvarGetBlock(pricingvars[k]) == GCGbranchGenericBranchdataGetConsblocknr(branchdata) )
                  {
                     blockfound = TRUE;
                     break;
                  }
               }
            }
         }
         if( !blockfound )
         {
            parentcons = GCGconsMasterbranchGetParentcons(parentcons);

            if( parentcons != NULL )
               branchdata = GCGconsMasterbranchGetBranchdata(parentcons);

            continue;
         }


         SCIPdebugMessage("consSsize = %d\n", GCGbranchGenericBranchdataGetConsSsize(branchdata));
         varinS = TRUE;
         for( p = 0; p < GCGbranchGenericBranchdataGetConsSsize(branchdata); ++p )
         {
            SCIP_Real generatorentry;

            generatorentry = getGeneratorEntry(mastervar, GCGbranchGenericBranchdataGetConsS(branchdata)[p].component);

            if( GCGbranchGenericBranchdataGetConsS(branchdata)[p].sense == GCG_COMPSENSE_GE )
            {
               if( SCIPisLT(scip, generatorentry, GCGbranchGenericBranchdataGetConsS(branchdata)[p].bound) )
               {
                  varinS = FALSE;
                  break;
               }
            }
            else
            {
               if( SCIPisGE(scip, generatorentry, GCGbranchGenericBranchdataGetConsS(branchdata)[p].bound) )
               {
                  varinS = FALSE;
                  break;
               }
            }
         }
         if( varinS )
         {
            SCIPdebugMessage("mastervar is added\n");
            SCIP_CALL( SCIPaddCoefLinear(scip, GCGbranchGenericBranchdataGetMastercons(branchdata), mastervar, 1.0) );
         }

         parentcons = GCGconsMasterbranchGetParentcons(parentcons);
         branchdata = GCGconsMasterbranchGetBranchdata(parentcons);
      }
   }

   return SCIP_OKAY;
}

/*
 * branching specific interface methods
 */

/** computes the generator of mastervar for the entry in origvar
 * @return entry of the generator corresponding to origvar */
SCIP_Real getGeneratorEntry(
   SCIP_VAR*            mastervar,          /**< current mastervariable */
   SCIP_VAR*            origvar             /**< corresponding origvar */
   )
{
   int i;
   SCIP_VAR** origvars;
   SCIP_Real* origvals;
   int norigvars;

   assert(mastervar != NULL);
   assert(origvar != NULL);

   origvars = GCGmasterVarGetOrigvars(mastervar);
   norigvars = GCGmasterVarGetNOrigvars(mastervar);
   origvals = GCGmasterVarGetOrigvals(mastervar);

   for( i = 0; i < norigvars; ++i )
   {
      if( origvars[i] == origvar )
      {
         return origvals[i];
      }
   }

   return 0;
}

/** method for initializing the set of respected indices */
static
SCIP_RETCODE InitIndexSet(
   SCIP*                scip,           /**< SCIP data structure */
   SCIP_VAR**           F,              /**< array of fractional mastervars */
   int                  Fsize,          /**< number of fractional mastervars */
   SCIP_VAR***           IndexSet,       /**< set to initialize */
   int*                 IndexSetSize   /**< size of the index set */
   )
{
   int i;
   int j;
   int k;
   SCIP_VAR** origvars;
   int norigvars;

   i = 0;
   j = 0;
   k = 0;
   norigvars = 0;
   *IndexSet = NULL;
   *IndexSetSize = 0;
   assert( F!= NULL);
   assert( Fsize > 0);

   for( i=0; i<Fsize; ++i )
   {
      origvars = GCGmasterVarGetOrigvars(F[i]);
      norigvars = GCGmasterVarGetNOrigvars(F[i]);

      if( *IndexSetSize == 0 && norigvars > 0 )
      {
         *IndexSetSize = norigvars;
         SCIP_CALL( SCIPallocMemoryArray(scip, IndexSet, *IndexSetSize) );
         for( j=0; j<*IndexSetSize; ++j )
         {
            (*IndexSet)[j] = origvars[j];
         }
      }
      else
      {
         for( j=0; j<norigvars; ++j )
         {
            int oldsize;

            oldsize = *IndexSetSize;

            for( k=0; k<oldsize; ++k )
            {
               /*  if variable already in union */
               if( (*IndexSet)[k] == origvars[j] )
               {
                  break;
               }
               if( k == oldsize-1 )
               {
                  /*  add variable to the end */
                  ++(*IndexSetSize);
                  SCIP_CALL( SCIPreallocMemoryArray(scip, IndexSet, *IndexSetSize) );
                  (*IndexSet)[*IndexSetSize-1] = origvars[j];
               }
            }
         }
      }
   }

   return SCIP_OKAY;
}

/** method for calculating the median over all fractional components values using
 * the quickselect algorithm (or a variant of it)
 *
 * This method will change the array
 *
 * @return median or if the median is the minimum return ceil(arithm middle)
 */
static
SCIP_Real GetMedian(
   SCIP*                scip,               /**< SCIP data structure */
   SCIP_Real*           array,              /**< array to find the median in (will be destroyed) */
   int                  arraysize,          /**< size of the array */
   SCIP_Real            min                 /**< minimum of array */
   )
{
   SCIP_Real Median;
   SCIP_Real swap;
   int l;
   int r;
   int i;
   int j;
   int MedianIndex;
   SCIP_Real arithmMiddle;

   assert(scip != NULL);
   assert(array != NULL);
   assert(arraysize > 0);

   r = arraysize -1;
   l = 0;
   arithmMiddle = 0;

   if( arraysize & 1 )
      MedianIndex = arraysize/2;
   else
      MedianIndex = arraysize/2 -1;

   while( l < r-1 )
   {
      Median = array[MedianIndex];
      i = l;
      j = r;
      do
      {
         while( SCIPisLT(scip, array[i], Median) )
            ++i;
         while( SCIPisGT(scip, array[j], Median) )
            --j;
         if( i <=j )
         {
            swap = array[i];
            array[i] = array[j];
            array[j] = swap;
            ++i;
            --j;
         }
      } while( i <=j );
      if( j < MedianIndex )
         l = i;
      if( i > MedianIndex )
         r = j;
   }
   Median = array[MedianIndex];

   if( SCIPisEQ(scip, Median, min) )
   {
      for( i=0; i<arraysize; ++i )
         arithmMiddle += 1.0*array[i]/arraysize;

      Median = SCIPceil(scip, arithmMiddle);
   }

   return Median;
}

/** comparefunction for lexicographical sort */
static
SCIP_DECL_SORTPTRCOMP(ptrcomp)
{
   GCG_STRIP* strip1;
   GCG_STRIP* strip2;
   SCIP_VAR* mastervar1;
   SCIP_VAR* mastervar2;
   SCIP_VAR** origvars;
   int norigvars;
   int i;

   strip1 = (GCG_STRIP*) elem1;
   strip2 = (GCG_STRIP*) elem2;

   mastervar1 = strip1->mastervar;
   mastervar2 = strip2->mastervar;

   i = 0;
   assert(mastervar1 != NULL);
   assert(mastervar2 != NULL);

   if( GCGvarGetBlock(mastervar1) == -1 )
   {
      SCIPdebugMessage("linkingvar\n");
      assert(GCGvarIsLinking(mastervar1));
   }
   if( GCGvarGetBlock(mastervar2) == -1 )
   {
      SCIPdebugMessage("linkingvar\n");
      assert(GCGvarIsLinking(mastervar2));
   }

   origvars = GCGmasterVarGetOrigvars(mastervar1);
   norigvars = GCGmasterVarGetNOrigvars(mastervar1);

   for( i=0; i<norigvars; ++i )
   {
      if( getGeneratorEntry(mastervar1, origvars[i]) > getGeneratorEntry(mastervar2, origvars[i]) )
         return -1;
      if( getGeneratorEntry(mastervar1, origvars[i]) < getGeneratorEntry(mastervar2, origvars[i]) )
         return 1;
   }

   return 0;
}

/** lexicographical sort using scipsort
 * This method will change the array
 */
static
SCIP_RETCODE LexicographicSort(
   GCG_STRIP**           array,              /**< array to sort (will be changed) */
   int                  arraysize           /**< size of the array */
   )
{

   assert(array != NULL);
   assert(arraysize > 0);

   SCIPdebugMessage("Lexicographic sorting\n");

   SCIPsortPtr((void**)array, ptrcomp, arraysize );

   return SCIP_OKAY;
}


/** compare function for ILO: returns 1 if bd1 < bd2 else -1 with respect to bound sequence */
static
int ILOcomp(
   SCIP*                scip,               /**< SCIP data structure */
   SCIP_VAR*            mastervar1,         /**< first strip */
   SCIP_VAR*            mastervar2,         /**< second strip */
   GCG_COMPSEQUENCE**   C,                  /**< component bound sequence to compare with */
   int                  NBoundsequences,    /**< size of the bound sequence */
   int*                 sequencesizes,      /**< sizes of the bound sequences */
   int                  p                   /**< current depth in C*/
   )
{
   int ivalue;
   int j;
   int k;
   int l;
   int Nupper;
   int Nlower;
   int returnvalue;
   GCG_COMPSEQUENCE** CopyC;
   SCIP_VAR* origvar;
   int* newsequencesizes;
   GCG_STRIP* strip1;
   GCG_STRIP* strip2;

   j = 0;
   k = 0;
   l = 0;
   Nupper = 0;
   Nlower = 0;
   origvar = NULL;
   newsequencesizes = NULL;
   strip1 = NULL;
   strip2 = NULL;

   /* lexicographic Order? */
   if( C == NULL || NBoundsequences <= 1 )
   {
      SCIP_CALL( SCIPallocBuffer(scip, &strip1) );
      SCIP_CALL( SCIPallocBuffer(scip, &strip2) );

      strip1->scip = scip;
      strip2->scip = scip;
      strip1->C = NULL;
      strip2->C = NULL;
      strip1->Csize = 0;
      strip2->Csize = 0;
      strip1->sequencesizes = NULL;
      strip2->sequencesizes = NULL;
      strip1->mastervar = mastervar1;
      strip2->mastervar = mastervar2;

      returnvalue = (*ptrcomp)( strip1, strip2);

      SCIPfreeBuffer(scip, &strip1);
      SCIPfreeBuffer(scip, &strip2);

      return returnvalue;
   }

   assert(C != NULL);
   assert(NBoundsequences > 0);

   /* find i which is in all S in C on position p */
   while( sequencesizes[k] < p )
   {
      ++k;
      assert(k < NBoundsequences);
   }
   origvar = C[k][p-1].component;
   assert(origvar != NULL);
   ivalue = C[k][p-1].bound;

   /* calculate subset of C */
   for( j=0; j< NBoundsequences; ++j )
   {
      if( sequencesizes[j] >= p )
      {
         assert(C[j][p-1].component == origvar);
         if( C[j][p-1].sense == GCG_COMPSENSE_GE )
            ++Nupper;
         else
            ++Nlower;
      }
   }

   if( SCIPisGE(scip, getGeneratorEntry(mastervar1, origvar), ivalue) && SCIPisGE(scip, getGeneratorEntry(mastervar2, origvar), ivalue) )
   {
      k=0;
      SCIP_CALL( SCIPallocMemoryArray(scip, &CopyC, Nupper) );
      SCIP_CALL( SCIPallocMemoryArray(scip, &newsequencesizes, Nupper) );
      for( j=0; j< NBoundsequences; ++j )
      {
         if( sequencesizes[j] >= p )
            assert(C[j][p-1].component == origvar);

         if( sequencesizes[j] >= p && C[j][p-1].sense == GCG_COMPSENSE_GE )
         {
            CopyC[k] = NULL;
            SCIP_CALL( SCIPallocMemoryArray(scip, &(CopyC[k]), sequencesizes[j]) );
            for( l=0; l< sequencesizes[j]; ++l )
            {
               CopyC[k][l] = C[j][l];
            }
            newsequencesizes[k] = sequencesizes[j];
            ++k;
         }
      }

      if( k != Nupper )
      {
         SCIPdebugMessage("k = %d, Nupper+1 =%d\n", k, Nupper+1);
      }

      if( Nupper != 0 )
         assert( k == Nupper );

      returnvalue = ILOcomp( scip, mastervar1, mastervar2, CopyC, Nupper, newsequencesizes, p+1);

      for( j=0; j< Nupper; ++j )
      {
         SCIPfreeMemoryArray(scip, &(CopyC[j]) );
      }
      SCIPfreeMemoryArray(scip, &newsequencesizes);
      SCIPfreeMemoryArray(scip, &CopyC);

      return returnvalue;
   }


   if( SCIPisLT(scip, getGeneratorEntry(mastervar1, origvar), ivalue) && SCIPisLT(scip, getGeneratorEntry(mastervar2, origvar), ivalue) )
   {
      k=0;
      SCIP_CALL( SCIPallocMemoryArray(scip, &CopyC, Nlower) );
      SCIP_CALL( SCIPallocMemoryArray(scip, &newsequencesizes, Nlower) );
      for( j=0; j< NBoundsequences; ++j )
      {
         if( sequencesizes[j] >= p )
            assert(C[j][p-1].component == origvar);

         if( sequencesizes[j] >= p && C[j][p-1].sense != GCG_COMPSENSE_GE )
         {
            CopyC[k] = NULL;
            SCIP_CALL( SCIPallocMemoryArray(scip, &(CopyC[k]), sequencesizes[j]) );
            for( l=0; l< sequencesizes[j]; ++l )
            {
               CopyC[k][l] = C[j][l];
            }
            newsequencesizes[k] = sequencesizes[j];
            ++k;
         }
      }

      if( k != Nlower )
      {
         SCIPdebugMessage("k = %d, Nlower+1 =%d\n", k, Nlower+1);
      }

      if( Nlower != 0 )
         assert( k == Nlower);

      returnvalue = ILOcomp( scip, mastervar1, mastervar2, CopyC, Nlower, newsequencesizes, p+1);

      for( j=0; j< Nlower; ++j )
      {

         SCIPfreeMemoryArray(scip, &(CopyC[j]) );
      }
      SCIPfreeMemoryArray(scip, &newsequencesizes);
      SCIPfreeMemoryArray(scip, &CopyC);

      return returnvalue;
   }
   if( SCIPisGT(scip, getGeneratorEntry(mastervar1, origvar), getGeneratorEntry(mastervar2, origvar)) )
      return 1;
   else
      return -1;
}

/** comparefunction for induced lexicographical sort */
static
SCIP_DECL_SORTPTRCOMP(ptrilocomp)
{
   GCG_STRIP* strip1;
   GCG_STRIP* strip2;
   int returnvalue;

   strip1 = (GCG_STRIP*) elem1;
   strip2 = (GCG_STRIP*) elem2;

   returnvalue = ILOcomp( strip1->scip, strip1->mastervar, strip2->mastervar, strip1->C, strip1->Csize, strip1->sequencesizes, 1);

   return returnvalue;
}

/** induced lexicographical sort */
static
SCIP_RETCODE InducedLexicographicSort(
   SCIP*                scip,               /**< SCIP ptr*/
   GCG_STRIP**          array,              /**< array of strips to sort in ILO*/
   int                  arraysize,          /**< size of the array */
   GCG_COMPSEQUENCE**   C,                  /**< current set o comp bound sequences*/
   int                  NBoundsequences,    /**< size of C */
   int*                 sequencesizes       /**< sizes of the sequences in C */
   )
{
   int i;

   SCIPdebugMessage("Induced Lexicographic sorting\n");

   if( NBoundsequences == 0 )
      return LexicographicSort( array, arraysize );
   assert( C!= NULL );

   assert(arraysize > 0);

   if( arraysize <= 1 )
      return SCIP_OKAY;

   for( i=0; i<arraysize; ++i )
   {
      array[i]->scip = scip;
      array[i]->Csize = NBoundsequences;
      array[i]->sequencesizes = sequencesizes;
      array[i]->C = C;
   }

   SCIPsortPtr((void**)array, ptrilocomp, arraysize);

   return SCIP_OKAY;
}

/** partitions the strip according to the priority */
static
SCIP_RETCODE partition(
   SCIP*                scip,               /**< SCIP data structure */
   SCIP_VAR**           J,                  /**< */
   int*                 Jsize,              /**< */
   int*                 priority,           /**< branching priorities */
   SCIP_VAR**           F,                  /**< set of fractional solutions satisfying bounds */
   int                  Fsize,              /**< size of list of fractional solutions satisfying bounds */
   SCIP_VAR**           origvar,            /**< */
   SCIP_Real*           median              /**< */
   )
{
   int j;
   int l;
   SCIP_Real min;
   SCIP_Real maxPriority;
   SCIP_Real* compvalues;

   do
   {
      min = INT_MAX;
      maxPriority = INT_MIN;

      /* max-min priority */
      for ( j = 0; j < *Jsize; ++j )
      {
         if ( priority[j] > maxPriority && SCIPvarGetType(J[j]) != SCIP_VARTYPE_CONTINUOUS )
         {
            maxPriority = priority[j];
            *origvar = J[j];
         }
      }
      SCIP_CALL( SCIPallocBufferArray(scip, &compvalues, Fsize) );
      for ( l = 0; l < Fsize; ++l )
      {
         compvalues[l] = getGeneratorEntry(F[l], *origvar);
         if ( SCIPisLT(scip, compvalues[l], min) )
            min = compvalues[l];
      }
      *median = GetMedian(scip, compvalues, Fsize, min);
      SCIPfreeBufferArray(scip, &compvalues);

      assert(min != INT_MAX);

      if ( !SCIPisEQ(scip, *median, 0) )
      {
         SCIPdebugMessage("median = %g\n", *median);
         SCIPdebugMessage("min = %g\n", min);
         SCIPdebugMessage("Jsize = %d\n", *Jsize);
      }

      if ( SCIPisEQ(scip, *median, min) )
      {
         /* here with max-min priority */
         for ( j = 0; j < *Jsize; ++j )
         {
            if ( *origvar == J[j] )
            {
               assert(priority[j] == 0);
               J[j] = J[*Jsize - 1];
               priority[j] = priority[*Jsize - 1];
               break;
            }
         }
         if( j < *Jsize )
            *Jsize = *Jsize-1;
      }
      assert(*Jsize >= 0);

   }while ( SCIPisEQ(scip, *median, min) && *Jsize > 0);

   return SCIP_OKAY;
}

/** add identified sequence to record */
static
SCIP_RETCODE addToRecord(
   SCIP*                scip,               /**< SCIP data structure */
   GCG_RECORD*          record,             /**< record of identified sequences */
   GCG_COMPSEQUENCE*    S,                  /**< bound restriction sequence */
   int                  Ssize               /**< size of bound restriction sequence */
)
{
   int i;

   SCIPdebugMessage("recordsize=%d, Ssize=%d\n", record->recordsize, Ssize);

   if( record->recordsize == 0 )
   {
      SCIP_CALL( SCIPallocMemoryArray(scip, &(record->record), 1) );
      SCIP_CALL( SCIPallocMemoryArray(scip, &(record->sequencesizes), 1) );
   }
   else
   {
      SCIP_CALL( SCIPreallocMemoryArray(scip, &(record->record), record->recordsize+1) );
      SCIP_CALL( SCIPreallocMemoryArray(scip, &(record->sequencesizes), record->recordsize+1) );
   }
   SCIP_CALL( SCIPallocMemoryArray(scip, &(record->record[record->recordsize]), Ssize) );
   for( i=0; i<Ssize;++i )
   {
      record->record[record->recordsize][i].component = S[i].component;
      record->record[record->recordsize][i].sense = S[i].sense;
      record->record[record->recordsize][i].bound = S[i].bound;
   }

   record->sequencesizes[record->recordsize] = Ssize; /* +1 ? */

   record->recordsize++;

   return SCIP_OKAY;
}


/** separation at the root node */
static
SCIP_RETCODE Separate(
   SCIP*                scip,               /**< SCIP data structure */
   SCIP_VAR**           F,                  /**< fractional strips respecting bound restrictions */
   int                  Fsize,              /**< size of the strips */
   SCIP_VAR**           IndexSet,           /**< index set */
   int                  IndexSetSize,       /**< size of index set */
   GCG_COMPSEQUENCE*    S,                  /**< ordered set of bound restrictions */
   int                  Ssize,              /**< size of the ordered set */
   GCG_RECORD*          record              /**< identified bound sequences */
   )
{
   int j;
   int k;
   int l;
   int Jsize;
   SCIP_VAR** J;
   SCIP_Real median;
   SCIP_Real min;
   int Fupper;
   int Flower;
   int* priority;
   SCIP_VAR* origvar;
   SCIP_VAR** copyF;
   GCG_COMPSEQUENCE* upperLowerS;
   GCG_COMPSEQUENCE* upperS;
   SCIP_Real* alpha;
   SCIP_Real* compvalues;
   SCIP_Real  muF;
   SCIP_Bool found;

   assert(scip != NULL);
   assert((Fsize == 0) == (F == NULL));
   assert((IndexSetSize == 0) == (IndexSet == NULL));

   j = 0;
   k = 0;
   l = 0;
   Jsize = 0;
   Fupper = 0;
   Flower = 0;
   muF = 0;
   min = INT_MAX;
   found = FALSE;
   priority = NULL;
   compvalues = NULL;
   J = NULL;
   origvar = NULL;
   copyF = NULL;
   upperLowerS = NULL;
   upperS = NULL;
   alpha = NULL;

   SCIPdebugMessage("Separate with ");

   /* if there are no fractional columns or potential columns, return */
   if( Fsize == 0 || IndexSetSize == 0 )
   {
      SCIPdebugPrintf("nothing, no fractional columns\n");
      return SCIP_OKAY;
   }

   assert( F != NULL );
   assert( IndexSet != NULL );

   for( j=0; j<Fsize; ++j )
      muF += SCIPgetSolVal(GCGrelaxGetMasterprob(scip), NULL, F[j]);
   SCIPdebugPrintf("Fsize = %d; Ssize = %d, IndexSetSize = %d, nuF=%.6g \n", Fsize, Ssize, IndexSetSize, muF);

   /* detect fractional alpha_i */
   SCIP_CALL( SCIPallocBufferArray(scip, &alpha, IndexSetSize) );

   for( k=0; k < IndexSetSize; ++k )
   {
      GCG_COMPSEQUENCE* copyS;
      SCIP_Real mu_F;
      SCIP_Bool even;

      even = TRUE;
      mu_F = 0;
      origvar = IndexSet[k];
      copyS = NULL;
      alpha[k] = 0;

      if( SCIPvarGetType(origvar) == SCIP_VARTYPE_CONTINUOUS )
         continue;

      SCIP_CALL( SCIPallocBufferArray(scip, &compvalues, Fsize) );
      for( l=0; l<Fsize; ++l )
      {
         compvalues[l] = getGeneratorEntry(F[l], origvar);
         if( SCIPisLT(scip, compvalues[l], min) )
            min = compvalues[l];
      }

      median = GetMedian(scip, compvalues, Fsize, min);
      SCIPfreeBufferArray(scip, &compvalues);
      compvalues = NULL;


      for( j = 0; j < Fsize; ++j )
      {
         SCIP_Real generatorentry;

         generatorentry = getGeneratorEntry(F[j], origvar);

         if( SCIPisGE(scip, generatorentry, median) )
         {
            alpha[k] += SCIPgetSolVal(GCGrelaxGetMasterprob(scip), NULL, F[j]);
         }
      }
      if( SCIPisGT(scip, alpha[k], 0) && SCIPisLT(scip, alpha[k], muF) )
      {
         ++Jsize;
      }
      if( !SCIPisFeasIntegral(scip, alpha[k]))
      {
         SCIPdebugMessage("alpha[%d] = %g\n", k, alpha[k]);
         found = TRUE;

         /* ********************************** *
          *   add the current pair to record   *
          * ********************************** */

         /* copy S */
         SCIP_CALL( SCIPallocMemoryArray(scip, &copyS, Ssize+1) );
         for( l=0; l < Ssize; ++l )
         {
            copyS[l] = S[l];
         }

         /* create temporary array to compute median */
         SCIP_CALL( SCIPallocBufferArray(scip, &compvalues, Fsize) );
         for( l=0; l<Fsize; ++l )
         {
            compvalues[l] = getGeneratorEntry(F[l], origvar);
            if( SCIPisLT(scip, compvalues[l], min) )
               min = compvalues[l];
         }
         assert(median == GetMedian(scip, compvalues, Fsize, min));
         median = GetMedian(scip, compvalues, Fsize, min);
         SCIPfreeBufferArray(scip, &compvalues);
         compvalues = NULL;

         /** @todo mb: this is a fix for an issue that Marcel claims that Vanderbeck did wrong */
         j = 0;

         do
         {
            mu_F = 0;
            if( even )
            {
               median = median+j;
               even = FALSE;
            }
            else
            {
               median = median-j;
               even = TRUE;
            }

            for( l=0; l<Fsize; ++l )
            {
               if( SCIPisGE(scip, getGeneratorEntry(F[l], origvar), median) )
                  mu_F += SCIPgetSolVal(GCGrelaxGetMasterprob(scip), NULL, F[l]);
            }
            ++j;

         } while( SCIPisFeasIntegral(scip, mu_F) );

         SCIPdebugMessage("new median is %g, comp=%s, Ssize=%d\n", median, SCIPvarGetName(origvar), Ssize);

         /* add last bound change to the copy of S */
         copyS[Ssize].component = origvar;
         copyS[Ssize].sense = GCG_COMPSENSE_GE;
         copyS[Ssize].bound = median;

         /* add identified sequence to record */
         SCIP_CALL( addToRecord(scip, record, copyS, Ssize+1) );


         /* ********************************** *
          *  end adding to record              *
          * ********************************** */
         SCIPfreeMemoryArrayNull(scip, &copyS);
         copyS = NULL;
      }
   }

   if( found )
   {
      SCIPfreeBufferArrayNull(scip, &alpha);

      SCIPdebugMessage("one S found with size %d\n", record->sequencesizes[record->recordsize-1]);

      return SCIP_OKAY;
   }


   /* ********************************** *
    *  discriminating components         *
    * ********************************** */

   /** @todo mb: this is a filter */
   SCIP_CALL( SCIPallocMemoryArray(scip, &J, Jsize) );
   j=0;
   for( k=0; k<IndexSetSize; ++k )
   {
      if( SCIPisGT(scip, alpha[k], 0) && SCIPisLT(scip, alpha[k], muF) )
      {
         J[j] = IndexSet[k];
         ++j;
      }
   }
   assert( j == Jsize );

   /* ********************************** *
    *  compute priority  (max-min)       *
    * ********************************** */

   SCIP_CALL( SCIPallocMemoryArray(scip, &priority, Jsize) );
   for( j=0; j<Jsize; ++j )
   {
      int maxcomp;
      int mincomp;

      maxcomp = INT_MIN;
      mincomp = INT_MAX;

      origvar = J[j];

      for( l=0; l<Fsize; ++l )
      {
         SCIP_Real generatorentry;

         generatorentry = getGeneratorEntry(F[l], origvar);

         if( generatorentry > maxcomp )
            maxcomp = generatorentry;

         if( generatorentry < mincomp )
            mincomp = generatorentry;
      }
      priority[j] = maxcomp-mincomp;
   }

   SCIP_CALL( partition(scip, J, &Jsize, priority, F, Fsize, &origvar, &median) );

   /** @todo mb: this is a copy of S for the recursive call below */
   SCIP_CALL( SCIPallocMemoryArray(scip, &upperLowerS, Ssize+1) );
   SCIP_CALL( SCIPallocMemoryArray(scip, &upperS, Ssize+1) );
   for( l=0; l < Ssize; ++l )
   {
      upperLowerS[l] = S[l];
      upperS[l] = S[l];
   }

   upperLowerS[Ssize].component = origvar;/* i; */
   upperS[Ssize].component = origvar;
   upperLowerS[Ssize].sense = GCG_COMPSENSE_LT;
   upperS[Ssize].sense = GCG_COMPSENSE_GE;
   upperLowerS[Ssize].bound = median;
   upperS[Ssize].bound = median;

   for( k=0; k<Fsize; ++k )
   {
      if( SCIPisGE(scip, getGeneratorEntry(F[k], origvar), median) )
         ++Fupper;
      else
         ++Flower;
   }

   /* ********************************** *
    *  choose smallest partition         *
    * ********************************** */

   SCIP_CALL( SCIPallocMemoryArray(scip, &copyF, Fsize) );
   j = 0;

   if( Flower > 0 )
   {
      j = 0;

      for( k=0; k<Fsize; ++k )
      {
         if( SCIPisLT(scip, getGeneratorEntry(F[k], origvar), median) )
         {
            copyF[j] = F[k];
            ++j;
         }
      }
      /*Fsize = Flower;*/
      assert(j < Fsize+1);
      if( Jsize == 0 && J != NULL )
      {
         SCIPfreeMemoryArrayNull(scip, &J);
      }
      SCIP_CALL( Separate( scip, copyF, Flower, J, Jsize, upperLowerS, Ssize+1, record) );
   }

   if( Fupper > 0 )
   {
      upperLowerS[Ssize].sense = GCG_COMPSENSE_GE;
      j = 0;

      for( k=0; k<Fsize; ++k )
      {
         if( SCIPisGE(scip, getGeneratorEntry(F[k], origvar), median) )
         {
            copyF[j] = F[k];
            ++j;
         }
      }
      /*Fsize = Fupper;*/
      assert(j < Fsize+1);
      if( Jsize == 0 && J != NULL )
      {
         SCIPfreeMemoryArrayNull(scip, &J);
      }
      SCIP_CALL( Separate( scip, copyF, Fupper, J, Jsize, upperS, Ssize+1, record) );
   }

   SCIPfreeMemoryArrayNull(scip, &copyF);
   SCIPfreeMemoryArrayNull(scip, &upperLowerS);
   SCIPfreeMemoryArrayNull(scip, &upperS);
   SCIPfreeMemoryArray(scip, &priority);
   SCIPfreeMemoryArrayNull(scip, &J);
   SCIPfreeBufferArray(scip, &alpha);

   return SCIP_OKAY;
}

/** choose a component bound sequence to create branching */
static
SCIP_RETCODE ChoseS(
   SCIP*                scip,               /**< SCIP data structure */
   GCG_RECORD**         record,             /**< candidate of bound sequences */
   GCG_COMPSEQUENCE**   S,                  /**< pointer to return chosen bound sequence */
   int*                 Ssize               /**< size of the chosen bound sequence */
   )
{
   int minSizeOfMaxPriority;  /* needed if the last comp priority is equal to the one in other bound sequences */
   int maxPriority;
   int i;
   int Index;

   minSizeOfMaxPriority = INT_MAX;
   maxPriority = INT_MIN;
   i = 0;
   Index = -1;

   SCIPdebugMessage("Chose S \n");

   assert((*record)->recordsize > 0);

   for( i=0; i< (*record)->recordsize; ++i )
   {
      assert((*record)->sequencesizes != NULL );
      assert((*record)->sequencesizes[i] > 0);
      if(maxPriority <= 1 ) /*  later by pseudocosts e.g. */
      {
         if( maxPriority < 1 )
         {
            maxPriority = 1; /*  only choose here first smallest S */
            minSizeOfMaxPriority = (*record)->sequencesizes[i];
            Index = i;
         }
         else
            if( (*record)->sequencesizes[i] < minSizeOfMaxPriority )
            {
               minSizeOfMaxPriority = (*record)->sequencesizes[i];
               Index = i;
            }
      }
   }
   assert(maxPriority != INT_MIN);
   assert(minSizeOfMaxPriority != INT_MAX);
   assert(Index >= 0);

   *Ssize = minSizeOfMaxPriority;
   SCIP_CALL( SCIPallocMemoryArray(scip, S, *Ssize) );
   for( i=0; i< *Ssize;++i )
   {
      (*S)[i] =  (*record)->record[Index][i];
   }

   assert(S!=NULL);
   assert(*S!=NULL);

   /* free record */
   for( i=0; i< (*record)->recordsize; ++i )
   {
      SCIPfreeMemoryArray(scip, &((*record)->record[i]) );
   }
   SCIPfreeMemoryArray(scip, &((*record)->record) );

   SCIPdebugMessage("with size %d \n", *Ssize);

   assert(*S!=NULL);

   return SCIP_OKAY;
}

/** updates the new set of sequences C in CopyC and the corresponding size array newsequencesizes
 *  returns the size of CopyC */
static
int computeNewSequence(
   int                   Csize,              /**< size of the sequence */
   int                   p,                  /**< index of node */
   SCIP_VAR*             origvar,            /**< another index generatorentry */
   int*                  sequencesizes,      /**< size of the sequences */
   GCG_COMPSEQUENCE**    C,                  /**< original sequence */
   GCG_COMPSEQUENCE**    CopyC,              /**< new sequence */
   int*                  newsequencesizes,   /**< output parameter for the new sequence */
   GCG_COMPSENSE         sense               /**< sense of the comparison */
   )
{
   int j;
   int k;
   for( k = 0, j = 0; j < Csize; ++j )
   {
      if ( sequencesizes[j] >= p )
         assert(C[j][p-1].component == origvar);

      if ( sequencesizes[j] >= p && C[j][p-1].sense == sense )
      {
         CopyC[k] = C[j];
         newsequencesizes[k] = sequencesizes[j];
         ++k;
      }
   }
   return k;
}

/** auxilary function to compute alpha for given index */
static
double computeAlpha(
   SCIP*                 scip,               /**< SCIP data structure */
   int                   Fsize,              /**< size of F */
   GCG_COMPSENSE         isense,             /**< sense of the bound for origvar */
   double                ivalue,             /**< value of the bound for origvar */
   SCIP_VAR*             origvar,            /**< index of the variable */
   SCIP_VAR**            F                   /**< current fractional mastervars*/
   )
{
   int j;
   SCIP_Real alpha_i = 0;
   for ( j = 0; j < Fsize; ++j )
   {
      SCIP_Real generatorentry;

      generatorentry = getGeneratorEntry(F[j], origvar);

      if ( (isense == GCG_COMPSENSE_GE && SCIPisGE(scip, generatorentry, ivalue)) ||
           (isense == GCG_COMPSENSE_LT && SCIPisLT(scip, generatorentry, ivalue)) )
      {
         alpha_i += SCIPgetSolVal(GCGrelaxGetMasterprob(scip), NULL, F[j]);
      }
   }

   return alpha_i;
}

/** separation at a node other than the root node */
static
SCIP_RETCODE Explore(
   SCIP*                scip,               /**< SCIP data structure */
   GCG_COMPSEQUENCE**   C,                  /**< */
   int                  Csize,              /**< */
   int*                 sequencesizes,      /**< */
   int                  p,                  /**< */
   SCIP_VAR**           F,                  /**< Strip of fractional columns */
   int                  Fsize,              /**< size of the strips */
   SCIP_VAR**           IndexSet,           /**< */
   int                  IndexSetSize,       /**< */
   GCG_COMPSEQUENCE**   S,                  /**< component sequences */
   int*                 Ssize,              /**< length of component sequences */
   GCG_RECORD*          record              /**< */
   )
{
   int j;
   int k;
   int l;
   SCIP_Real ivalue;
   GCG_COMPSENSE isense;
   SCIP_Real median;
   int Fupper;
   int Flower;
   int Cupper;
   int Clower;
   int lowerSsize;
   SCIP_VAR** copyF;
   GCG_COMPSEQUENCE* copyS;
   GCG_COMPSEQUENCE* lowerS;
   GCG_COMPSEQUENCE** CopyC;
   int* newsequencesizes;
   SCIP_VAR* origvar;
   SCIP_Real alpha_i;
   SCIP_Real  muF;
   SCIP_Bool found;
   SCIP_Real nu_F;

   j = 0;
   k = 0;
   l = 0;
   alpha_i = 0;
   muF = 0;
   Fupper = 0;
   Flower = 0;
   Cupper = 0;
   Clower = 0;
   lowerSsize = 0;
   newsequencesizes = NULL;
   copyF = NULL;
   CopyC = NULL;
   copyS = NULL;
   origvar = NULL;
   lowerS =  NULL;
   found = FALSE;

   SCIPdebugMessage("Explore\n");

   SCIPdebugMessage("with Fsize = %d, Csize = %d, Ssize = %d, p = %d\n", Fsize, Csize, *Ssize, p);

   /* *************************************** *
    *   if C=Ø, call separate and return that *
    * *************************************** */

   if( C == NULL || Fsize==0 || IndexSetSize==0 || Csize == 0 )
   {
      /* SCIPdebugMessage("go to Separate\n"); */
      assert(S != NULL);

      SCIP_CALL( Separate( scip, F, Fsize, IndexSet, IndexSetSize, *S, *Ssize, record) );

      if( *Ssize > 0 && *S != NULL)
      {
         SCIPfreeMemoryArrayNull(scip, S);
         *S = NULL;
         *Ssize = 0;
      }
      return SCIP_OKAY;
   }

   assert(C != NULL);
   assert(Csize > 0);
   assert(F != NULL);
   assert(IndexSet != NULL);
   assert(sequencesizes != NULL);

   /* ******************************************* *
    * find i which is in all S in C on position p *
    * ******************************************* */

   while( sequencesizes[k] < p )
   {
    /*   SCIPdebugMessage("sequencesizes[%d] = %d\n", k, sequencesizes[k]); */
      ++k;
      if( k >= Csize )
      {
         SCIPdebugMessage("no %dth element bounded\n", p);
         assert(S != NULL);
         SCIP_CALL( Separate( scip, F, Fsize, IndexSet, IndexSetSize, *S, *Ssize, record) );

         if( *Ssize > 0 && *S != NULL )
         {
            SCIPfreeMemoryArrayNull(scip, S);
            *S = NULL;
            *Ssize = 0;
         }

         return SCIP_OKAY;
      }
      assert( k < Csize );
   }
   origvar = C[k][p-1].component;
   isense = C[k][p-1].sense;
   ivalue = C[k][p-1].bound;

   assert(origvar != NULL);
   /* SCIPdebugMessage("orivar = %s; ivalue = %g\n", SCIPvarGetName(origvar), ivalue); */

   for( j=0; j<Fsize; ++j )
   {
      muF += SCIPgetSolVal(GCGrelaxGetMasterprob(scip), NULL, F[j]);
   }

   /* SCIPdebugMessage("muF = %g\n", muF); */

   /* ******************************************* *
    * compute alpha_i                             *
    * ******************************************* */

   alpha_i = computeAlpha(scip, Fsize, isense, ivalue, origvar, F);

   if( alpha_i == 0 && isense != GCG_COMPSENSE_GE )
   {
      isense = GCG_COMPSENSE_GE;
      alpha_i = computeAlpha(scip, Fsize, isense, ivalue, origvar, F);
   }

   median = ivalue;

   /* SCIPdebugMessage("alpha(%s) = %g\n", SCIPvarGetName(origvar), alpha_i); */

   /* ******************************************* *
    * if f > 0, add pair to record                *
    * ******************************************* */

   if( !SCIPisFeasIntegral(scip, alpha_i) )
   {
      found = TRUE;
      /* SCIPdebugMessage("fractional alpha(%s) = %g\n", SCIPvarGetName(origvar), alpha_i); */

      /* ******************************************* *
       * compute nu_F                                *
       * ******************************************* */

      nu_F = 0;
      for( l = 0; l < Fsize; ++l )
      {
         if( (isense == GCG_COMPSENSE_GE && SCIPisGE(scip, getGeneratorEntry(F[l], origvar), ivalue) )
            || (isense == GCG_COMPSENSE_LT && SCIPisLT(scip, getGeneratorEntry(F[l], origvar), ivalue)) )
         {
               nu_F += SCIPgetSolVal(GCGrelaxGetMasterprob(scip), NULL, F[l]);
         }
      }

      /* ******************************************* *
       * add to record                               *
       * ******************************************* */

      if( SCIPisGT(scip, nu_F - SCIPfloor(scip, nu_F), 0) )
      {
         SCIP_CALL( SCIPallocMemoryArray(scip, &copyS, *Ssize+1) );
         for( l = 0; l < *Ssize; ++l )
         {
            copyS[l] = (*S)[l];
         }
         copyS[*Ssize].component = origvar;
         copyS[*Ssize].sense = isense;
         copyS[*Ssize].bound = ivalue;
         SCIP_CALL( addToRecord(scip, record, copyS, *Ssize+1) );
      }
      else
      {
         found = FALSE;
      }
   }

   if( found )
   {
      SCIPdebugMessage("found fractional alpha\n");
      SCIPfreeMemoryArrayNull(scip, &copyS);
      return SCIP_OKAY;
   }

   /* add bound to the end of S */
   ++(*Ssize);
   if( S==NULL || *S==NULL )
   {
      assert(*Ssize == 1);
      SCIP_CALL( SCIPallocMemoryArray(scip, S, *Ssize) );
   }
   else
   {
      assert(*Ssize >1);
      SCIP_CALL( SCIPreallocMemoryArray(scip, S, *Ssize) );
   }
   median = ivalue;
   (*S)[*Ssize-1].component = origvar;
   (*S)[*Ssize-1].sense = GCG_COMPSENSE_GE;
   (*S)[*Ssize-1].bound = median;

   SCIP_CALL( SCIPallocMemoryArray(scip, &lowerS, *Ssize) );

   for( k=0; k<*Ssize-1; ++k )
   {
      lowerS[k].component = (*S)[k].component;
      lowerS[k].sense = (*S)[k].sense;
      lowerS[k].bound = (*S)[k].bound;
   }
   lowerSsize = *Ssize;
   lowerS[lowerSsize-1].component = origvar;
   lowerS[lowerSsize-1].sense = GCG_COMPSENSE_LT;
   lowerS[lowerSsize-1].bound = median;

   for( k=0; k<Fsize; ++k )
   {
      if( SCIPisGE(scip, getGeneratorEntry(F[k], origvar), median) )
         ++Fupper;
      else
         ++Flower;
   }

   /* calculate subset of C */
   for( j=0; j< Csize; ++j )
   {
      if( sequencesizes[j] >= p )
      {
         if( C[j][p-1].sense == GCG_COMPSENSE_GE )
         {
            ++Cupper;
         }
         else
         {
            ++Clower;
            assert( C[j][p-1].sense == GCG_COMPSENSE_LT );
         }
      }
   }

   SCIPdebugMessage("Cupper = %d, Clower = %d\n", Cupper, Clower);

   if( SCIPisLE(scip, alpha_i, 0) && Fupper != 0 )
      Flower = INT_MAX;
   if( SCIPisEQ(scip, alpha_i, muF) && Flower != 0 )
      Fupper = INT_MAX;

   if( Fupper > 0  && Fupper != INT_MAX )
   {
      SCIPdebugMessage("chose upper bound Fupper = %d, Cupper = %d\n", Fupper, Cupper);

      SCIP_CALL( SCIPallocMemoryArray(scip, &copyF, Fupper) );
      for( j = 0, k = 0; k < Fsize; ++k )
      {
         if( SCIPisGE(scip, getGeneratorEntry(F[k], origvar), median) )
         {
            copyF[j] = F[k];
            ++j;
         }
      }

      /* new C */
      if( Fupper > 0 )
      {
         SCIP_CALL( SCIPallocMemoryArray(scip, &CopyC, Cupper) );
         SCIP_CALL( SCIPallocMemoryArray(scip, &newsequencesizes, Cupper) );
         k = computeNewSequence(Csize, p, origvar, sequencesizes, C, CopyC, newsequencesizes, GCG_COMPSENSE_GE);
         if( k != Cupper )
         {
            SCIPdebugMessage("k = %d, p = %d\n", k, p);
         }
         assert(k == Cupper);
      }
      else
      {
         CopyC = NULL;
         k = 0;
      }

      SCIP_CALL( Explore( scip, CopyC, Cupper, newsequencesizes, p+1, copyF, Fupper, IndexSet, IndexSetSize, S, Ssize, record) );
      SCIPfreeMemoryArrayNull(scip, &copyF);
      copyF = NULL;
   }

   if( Flower > 0 )
   {
      SCIPdebugMessage("chose lower bound Flower = %d Clower = %d\n", Flower, Clower);

      if( copyF == NULL )
      {
         SCIP_CALL( SCIPallocMemoryArray(scip, &copyF, Flower) );
      }
      j = 0;
      for( k=0; k<Fsize; ++k )
      {
         if( SCIPisLT(scip, getGeneratorEntry(F[k], origvar), median) )
         {
            copyF[j] = F[k];
            ++j;
         }
      }

      /* new C */
      if( Flower > 0 )
      {
         SCIPfreeMemoryArrayNull(scip, &CopyC);
         SCIPfreeMemoryArrayNull(scip, &newsequencesizes);

         SCIP_CALL( SCIPallocMemoryArray(scip, &CopyC, Clower) );
         SCIP_CALL( SCIPallocMemoryArray(scip, &newsequencesizes, Clower) );
         k = computeNewSequence(Csize, p, origvar, sequencesizes, C, CopyC, newsequencesizes, GCG_COMPSENSE_LT);
         if( k != Clower )
         {
            SCIPdebugMessage("k = %d, p = %d\n", k, p);
         }
         assert(k == Clower);
      }
      else
      {
         CopyC = NULL;
         k = 0;
      }

      SCIP_CALL( Explore( scip, CopyC, Clower, newsequencesizes, p+1, copyF, Flower, IndexSet, IndexSetSize, &lowerS, &lowerSsize, record) );
   }

   SCIPfreeMemoryArrayNull(scip, &copyF);
   SCIPfreeMemoryArrayNull(scip, &copyS);
   SCIPfreeMemoryArrayNull(scip, &lowerS);
   SCIPfreeMemoryArrayNull(scip, &CopyC);
   SCIPfreeMemoryArrayNull(scip, &newsequencesizes);

   if( *Ssize > 0 && *S != NULL )
   {
      SCIPfreeMemoryArrayNull(scip, S);

      *Ssize = 0;
   }

   return SCIP_OKAY;
}

/** callup method for seperate
 * decides whether Separate or Explore should be done */
static
SCIP_RETCODE ChooseSeparateMethod(
   SCIP*                scip,               /**< */
   SCIP_VAR**           F,
   int                  Fsize,              /**< */
   GCG_COMPSEQUENCE**   S,                  /**< */
   int*                 Ssize,              /**< */
   GCG_COMPSEQUENCE**   C,                  /**< */
   int                  Csize,              /**< */
   int*                 CompSizes,           /**< */
   int                  blocknr,
   SCIP_BRANCHRULE*      branchrule,
   SCIP_RESULT*         result,
   int*                 checkedblocks,
   int                  ncheckedblocks,
   GCG_STRIP***         checkedblockssortstrips,
   int*                 checkedblocksnsortstrips
   )
{
   SCIP* masterscip;
   int i;
   SCIP_VAR** IndexSet;
   SCIP_VAR** mastervars;
   int IndexSetSize;
   GCG_RECORD* record;
   int exploreSsize;
   GCG_COMPSEQUENCE* exploreS;
   GCG_STRIP** strips;
   int nstrips;
   int nmastervars;

   assert(Fsize > 0);
   assert(F != NULL);
   IndexSetSize = 0;
   exploreSsize = 0;
   exploreS = NULL;
   record = NULL;
   IndexSet = NULL;
   strips = NULL;
   nstrips = 0;

   SCIPdebugMessage("Calling Separate\n");

   SCIP_CALL( SCIPallocBuffer(scip, &record) );
   record->recordsize = 0;
   record->record = NULL;
   record->sequencesizes = NULL;

   /* calculate IndexSet */
   SCIP_CALL( InitIndexSet(scip, F, Fsize, &IndexSet, &IndexSetSize) );
   assert(IndexSetSize > 0);
   assert(IndexSet != NULL);

   /* rootnode? */
   if( Csize<=0 )
      SCIP_CALL( Separate( scip, F, Fsize, IndexSet, IndexSetSize, NULL, 0, record) );
   else
   {
      assert( C!=NULL );
      SCIP_CALL( Explore( scip, C, Csize, CompSizes, 1, F, Fsize, IndexSet, IndexSetSize, &exploreS, &exploreSsize, record) );

      SCIPfreeMemoryArrayNull(scip, &exploreS);
   }

   assert(record != NULL);

   if( record->recordsize <= 0 )
   {
      masterscip = GCGrelaxGetMasterprob(scip);
      assert(masterscip != NULL);
      SCIP_CALL( SCIPgetVarsData(masterscip, &mastervars, &nmastervars, NULL, NULL, NULL, NULL) );

      ++ncheckedblocks;
      assert(ncheckedblocks <= GCGrelaxGetNPricingprobs(scip)+1);

      if( ncheckedblocks == 1 )
      {
         SCIP_CALL( SCIPallocBufferArray(scip, &checkedblocks, ncheckedblocks) );
         SCIP_CALL( SCIPallocBufferArray(scip, &checkedblockssortstrips, ncheckedblocks) );
         SCIP_CALL( SCIPallocBufferArray(scip, &checkedblocksnsortstrips, ncheckedblocks) );
      }
      else
      {
         SCIP_CALL( SCIPreallocBufferArray(scip, &checkedblocks, ncheckedblocks) );
         SCIP_CALL( SCIPreallocBufferArray(scip, &checkedblockssortstrips, ncheckedblocks) );
         SCIP_CALL( SCIPreallocBufferArray(scip, &checkedblocksnsortstrips, ncheckedblocks) );
      }

      checkedblocks[ncheckedblocks-1] = blocknr;

      for( i=0; i<nmastervars; ++i )
      {
         SCIP_Bool blockfound;
         SCIP_VAR** pricingvars;
         int u;

         if( GCGvarGetBlock(mastervars[i]) == -1 && GCGvarIsLinking(mastervars[i]) )
         {
            blockfound = FALSE;

            pricingvars = GCGlinkingVarGetPricingVars(mastervars[i]);
            assert(pricingvars != NULL );

            for( u=0; u<GCGlinkingVarGetNBlocks(mastervars[i]); ++u )
            {
               if( pricingvars[u] != NULL && GCGvarGetBlock(pricingvars[u]) == blocknr )
               {
                  blockfound = TRUE;
                  break;
               }
            }
         }
         else
         {
            blockfound = (GCGvarGetBlock(mastervars[i]) == blocknr);
         }

         if( blockfound )
         {
            ++nstrips;

            if( nstrips == 1 )
            {
               SCIP_CALL( SCIPallocBufferArray(scip, &strips, nstrips) );
            }
            else
            {
               SCIP_CALL( SCIPreallocBufferArray(scip, &strips, nstrips) );
            }

            SCIP_CALL( SCIPallocBuffer(scip, &(strips[nstrips-1])) );

            strips[nstrips-1]->C = NULL;
            strips[nstrips-1]->mastervar = mastervars[i];
            strips[nstrips-1]->Csize = 0;
            strips[nstrips-1]->sequencesizes = NULL;
            strips[nstrips-1]->scip = NULL;
         }
      }

      SCIP_CALL( InducedLexicographicSort(scip, strips, nstrips, C, Csize, CompSizes) );

      checkedblocksnsortstrips[ncheckedblocks-1] = nstrips;

      SCIP_CALL( SCIPallocBufferArray(scip, &(checkedblockssortstrips[ncheckedblocks-1]), nstrips) );

      /* sort the direct copied origvars at the end */

      for( i=0; i<nstrips; ++i )
      {
         SCIP_CALL( SCIPallocBuffer(scip, &(checkedblockssortstrips[ncheckedblocks-1][i])) );
         checkedblockssortstrips[ncheckedblocks-1][i] = strips[i];
      }

      for( i=0; i<nstrips; ++i )
      {
         SCIPfreeBuffer(scip, &(strips[i]));
         strips[i] = NULL;
      }
      SCIPfreeBufferArrayNull(scip, &strips);

      /*choose new block */
      SCIP_CALL( GCGbranchGenericInitbranch(scip, branchrule, result, checkedblocks, ncheckedblocks, checkedblockssortstrips, checkedblocksnsortstrips) );

   }
   else
   {
      if( ncheckedblocks > 0 )
      {
         SCIPfreeBufferArray(scip, &checkedblocks);

         for( i=0; i< ncheckedblocks; ++i )
         {
            int j;

            j = 0;

            for( j=0; j< checkedblocksnsortstrips[i]; ++j )
            {
               SCIPfreeBuffer(scip, &(checkedblockssortstrips[i][j]) );
            }

            SCIPfreeBufferArray(scip, &(checkedblockssortstrips[i]) );
         }

         SCIPfreeBufferArray(scip, &checkedblockssortstrips);
         SCIPfreeBufferArray(scip, &checkedblocksnsortstrips);
      }
   }

   assert(record->recordsize > 0);

   SCIP_CALL( ChoseS( scip, &record, S, Ssize) );
   assert(*S!=NULL);

   SCIPfreeMemoryArray(scip, &IndexSet);
   if( record != NULL )
   {
      SCIPfreeMemoryArrayNull(scip, &record->record);
      SCIPfreeMemoryArrayNull(scip, &record->sequencesizes);
      SCIPfreeBuffer(scip, &record);
   }
   return SCIP_OKAY;
}

/** callback deletion method for branching data*/
static
GCG_DECL_BRANCHDATADELETE(branchDataDeleteGeneric)
{
   assert(scip != NULL);
   assert(branchdata != NULL);

   if( *branchdata == NULL )
   {
      SCIPdebugMessage("branchDataDeleteGeneric: cannot delete empty branchdata\n");

      return SCIP_OKAY;
   }

   if( (*branchdata)->mastercons != NULL )
   {
      SCIPdebugMessage("branchDataDeleteGeneric: child blocknr %d, %s\n", (*branchdata)->consblocknr,
         SCIPconsGetName((*branchdata)->mastercons) );
   }
   else
   {
      SCIPdebugMessage("branchDataDeleteGeneric: child blocknr %d, empty mastercons\n", (*branchdata)->consblocknr);
   }

   /* release constraint that enforces the branching decision */
   if( (*branchdata)->mastercons != NULL )
   {
      SCIP_CALL( SCIPreleaseCons(GCGrelaxGetMasterprob(scip), &(*branchdata)->mastercons) );
      (*branchdata)->mastercons = NULL;
   }

   if( (*branchdata)->consS != NULL && (*branchdata)->consSsize > 0 )
   {
      SCIPfreeMemoryArray(scip, &((*branchdata)->consS));
      (*branchdata)->consS = NULL;
      (*branchdata)->consSsize = 0;
   }

   SCIPfreeMemoryNull(scip, branchdata);
   *branchdata = NULL;

   return SCIP_OKAY;
}

/** check method for pruning ChildS directly on childnodes
 *  retrun TRUE if node is pruned */
static
SCIP_Bool checkchildconsS(
   SCIP*                 scip,
   SCIP_Real             lhs,               /**< lhs for childnode which is checkes to be pruned */
   GCG_COMPSEQUENCE*     childS,            /**< Component Bound Sequence defining the childnode */
   int                   childSsize,        /**< */
   SCIP_CONS*            parentcons,      /**< */
   int                   childBlocknr       /**< number of the block for the childnode */
   )
{
   int i;
   int nchildren;

   nchildren = GCGconsMasterbranchGetNChildcons(parentcons);
   assert(nchildren>0);

   for( i=0; i<nchildren; ++i )
   {
      SCIP_CONS* childcons;
      GCG_BRANCHDATA* branchdata;
      SCIP_Bool same;
      int j;

      same = TRUE;
      childcons = GCGconsMasterbranchGetChildcons(parentcons, i);
      if( childcons == NULL )
         continue;

      if( GCGconsMasterbranchGetbranchrule(childcons) != NULL && strcmp(SCIPbranchruleGetName(GCGconsMasterbranchGetbranchrule(childcons)), "generic") != 0 )
         continue;

      branchdata = GCGconsMasterbranchGetBranchdata(childcons);
      assert(branchdata != NULL || GCGconsMasterbranchGetOrigbranchdata(childcons) != NULL);

      if( branchdata == NULL )
         branchdata = GCGconsMasterbranchGetOrigbranchdata(childcons);

      if( childBlocknr != branchdata->consblocknr || childSsize != branchdata->consSsize || !SCIPisEQ(scip, lhs, branchdata->lhs) )
         continue;

      assert(childSsize > 0 && branchdata->consSsize > 0);

      for( j=0; j< childSsize; ++j )
      {
         if( childS[j].component != branchdata->consS[j].component
            || childS[j].sense != branchdata->consS[j].sense
            || !SCIPisEQ(scip, childS[j].bound, branchdata->consS[j].bound) )
         {
            same = FALSE;
            break;
         }
      }

      if( same )
      {
         SCIPdebugMessage("child pruned \n");
         return TRUE;
      }
   }
   return FALSE;
}

/** check method for pruning ChildS indirectly by parentnodes
 *  retrun TRUE if node is pruned */
static
SCIP_Bool pruneChildNodeByDominanceGeneric(
   SCIP*                 scip,              /**< SCIP data structure */
   SCIP_Real             lhs,               /**< lhs for childnode which is checkes to be pruned */
   GCG_COMPSEQUENCE*     childS,            /**< Component Bound Sequence defining the childnode */
   int                   childSsize,        /**< */
   SCIP_CONS*            masterbranchcons,      /**< */
   int                   childBlocknr       /**< number of the block for the childnode */
   )
{
   SCIP_CONS* cons;
   SCIP_Bool ispruned;

   ispruned = FALSE;

   SCIPdebugMessage("Prune by dominance\n");
   cons = GCGconsMasterbranchGetParentcons(masterbranchcons);

   if( cons == NULL )
   {
      SCIPdebugMessage("cons == NULL, not pruned\n");
      return FALSE;
   }
   while( cons != NULL )
   {
      GCG_BRANCHDATA* parentdata;

      parentdata = GCGconsMasterbranchGetBranchdata(cons);
      if( parentdata == NULL )
      {
         /* root node: check children for pruning */
         return checkchildconsS(scip, lhs, childS, childSsize, cons, childBlocknr);
      }
      if( strcmp(SCIPbranchruleGetName(GCGconsMasterbranchGetbranchrule(cons)), "generic") != 0 )
         return checkchildconsS(scip, lhs, childS, childSsize, cons, childBlocknr);

      ispruned = checkchildconsS(scip, lhs, childS, childSsize, cons, childBlocknr);

      if( ispruned )
      {
         return TRUE;
      }

      cons = GCGconsMasterbranchGetParentcons(cons);
   }

   SCIPdebugMessage("child not pruned\n");
   return FALSE;
}

/** initialize branchdata at the node */
static
SCIP_RETCODE initNodeBranchdata(
   GCG_BRANCHDATA**      nodebranchdata,     /**< branching data to set */
   int                   blocknr             /**< block we are branching in */
   )
{
   SCIP_CALL( SCIPallocMemory(scip, nodebranchdata) );

   (*nodebranchdata)->consblocknr = blocknr;
   (*nodebranchdata)->mastercons = NULL;
   (*nodebranchdata)->consS = NULL;
   (*nodebranchdata)->C = NULL;
   (*nodebranchdata)->sequencesizes = NULL;
   (*nodebranchdata)->Csize = 0;
   (*nodebranchdata)->consSsize = 0;

   return SCIP_OKAY;
}

/** for given component bound sequence S, create |S|+1 Vanderbeck branching nodes */
static
SCIP_RETCODE createChildNodesGeneric(
   SCIP*                 scip,              /**< SCIP data structure */
   SCIP_BRANCHRULE*      branchrule,        /**< branching rule */
   GCG_COMPSEQUENCE*     S,                 /**< Component Bound Sequence defining the nodes */
   int                   Ssize,             /**< size of S*/
   int                   blocknr,           /**< number of the block */
   SCIP_CONS*            masterbranchcons,   /**< current masterbranchcons*/
   SCIP_RESULT*          result
   )
{
#ifdef SCIP_DEBUG
   SCIP_Real identicalcontrol = -1;
#endif
   SCIP*  masterscip;
   int i;
   int p;
   int k;
   SCIP_Real pL;
   SCIP_Real L;
   SCIP_Real lhs;
   SCIP_Real lhsSum;
   int nmastervars;
   int nmastervars2;
   int ncopymastervars;
   int nbranchcands;
   int nchildnodes;
   SCIP_Real mu;  /*  mu(S) */
   SCIP_VAR** mastervars;
   SCIP_VAR** mastervars2;
   SCIP_VAR** branchcands;
   SCIP_VAR** copymastervars;

   assert(scip != NULL);
   assert(Ssize > 0);
   assert(S != NULL);

   lhs = 0;
   lhsSum = 0;
   nchildnodes = 0;
   p = 0;
   k = 0;
   i = 0;
   L = 0;
   mu = 0;

   pL = GCGrelaxGetNIdenticalBlocks(scip, blocknr);
   SCIPdebugMessage("Vanderbeck branching rule Node creation for blocknr %d with %.1f identical blocks \n", blocknr, pL);


   /*  get variable data of the master problem */
   masterscip = GCGrelaxGetMasterprob(scip);
   assert(masterscip != NULL);
   SCIP_CALL( SCIPgetVarsData(masterscip, &mastervars, &nmastervars, NULL, NULL, NULL, NULL) );
   nmastervars2 = nmastervars;
   assert(nmastervars >= 0);

   SCIP_CALL( SCIPduplicateMemoryArray(scip, &copymastervars, mastervars, nmastervars) );
   SCIP_CALL( SCIPduplicateMemoryArray(scip, &mastervars2, mastervars, nmastervars) );

   SCIP_CALL( SCIPgetLPBranchCands(masterscip, &branchcands, NULL, NULL, &nbranchcands, NULL, NULL) );

   SCIPdebugMessage("Vanderbeck branching rule: creating %d nodes\n", Ssize+1);

   for( p=0; p<Ssize+1; ++p )
   {
      GCG_BRANCHDATA* branchchilddata;
      SCIP_NODE* child;
      SCIP_CONS* childcons;
      char childname[SCIP_MAXSTRLEN];

      mu = 0;
      branchchilddata = NULL;

      /*  allocate branchdata for child and store information */
      SCIP_CALL( initNodeBranchdata(&branchchilddata, blocknr) );

      if( p == Ssize )
      {
         SCIP_CALL( SCIPallocMemoryArray(scip, &(branchchilddata->consS), Ssize) );
         branchchilddata->consSsize = Ssize;
      }
      else
      {
         SCIP_CALL( SCIPallocMemoryArray(scip, &(branchchilddata->consS), p+1) );
         branchchilddata->consSsize = p+1;
      }
      for( k=0; k<=p; ++k )
      {
         GCG_COMPSEQUENCE compBound;

         if( k == Ssize )
         {
            assert( p == Ssize );
            compBound = S[k-1];
            branchchilddata->consS[k-1] = compBound;
         }
         else
         {
            compBound = S[k];
            if( k >= p )
            {
               if( S[p].sense == GCG_COMPSENSE_GE )
                  compBound.sense = GCG_COMPSENSE_LT;
               else
                  compBound.sense = GCG_COMPSENSE_GE;
            }
            branchchilddata->consS[k] = compBound;
         }
      }

      /* last node? */
      if( p == Ssize )
      {
         lhs = pL;
      }
      else
      {
         /* calculate mu */
         ncopymastervars = nmastervars2;
         for( i=0; i<ncopymastervars; ++i )
         {
            SCIP_VAR* swap;
            SCIP_VAR** pricingvars;
            SCIP_Real generator_i;
            SCIP_Bool blockfound;
            int u;

            blockfound = TRUE;
            pricingvars = NULL;
            u = 0;

            if( i >= nmastervars2 )
               break;

            if( GCGvarGetBlock(mastervars2[i]) == -1 && GCGvarIsLinking(mastervars2[i]) )
            {
               blockfound = FALSE;

               pricingvars = GCGlinkingVarGetPricingVars(mastervars2[i]);
               assert(pricingvars != NULL );

               for( u=0; u<GCGlinkingVarGetNBlocks(mastervars2[i]); ++u )
               {
                  if( pricingvars[u] != NULL && GCGvarGetBlock(pricingvars[u]) == blocknr )
                  {
                     blockfound = TRUE;
                     break;
                  }
               }
            }
            else
            {
               blockfound = (GCGvarGetBlock(mastervars2[i]) == blocknr);
            }

            if( blockfound )
            {
               generator_i = getGeneratorEntry(mastervars2[i], S[p].component);

               if( (S[p].sense == GCG_COMPSENSE_GE && SCIPisGE(scip, generator_i, S[p].bound)) ||
                  (S[p].sense == GCG_COMPSENSE_LT && SCIPisLT(scip, generator_i, S[p].bound) ) )
               {
                  mu += SCIPgetSolVal(masterscip, NULL, mastervars2[i]);
               }
               else if( ncopymastervars > 0 )
               {
                  swap = mastervars2[i];
                  mastervars2[i] = mastervars2[nmastervars2-1];
                  mastervars2[nmastervars2-1] = swap;
                  --nmastervars2;
                  --i;
               }
            }
            else if( nmastervars2 > 0 )
            {
               swap = mastervars2[i];
               mastervars2[i] = mastervars2[nmastervars2-1];
               mastervars2[nmastervars2-1] = swap;
               --nmastervars2;
               --i;
            }
         }

         if( p == Ssize-1 )
         {
            L = SCIPceil(scip, mu);
            SCIPdebugMessage("mu = %g, \n", mu);
            assert(!SCIPisFeasIntegral(scip,mu));
         }
         else
         {
            L = mu;
            SCIPdebugMessage("mu = %g should be integer, \n", mu);
            assert(SCIPisFeasIntegral(scip,mu));
         }
         lhs = pL-L+1;
      }
      SCIPdebugMessage("pL = %g \n", pL);
      pL = L;

      branchchilddata->lhs = lhs;
      SCIPdebugMessage("L = %g, \n", L);
      SCIPdebugMessage("lhs set to %g \n", lhs);
      assert(SCIPisFeasIntegral(scip, lhs));
      lhsSum += lhs;


      if( masterbranchcons == NULL || !pruneChildNodeByDominanceGeneric(scip, lhs, branchchilddata->consS, branchchilddata->consSsize, masterbranchcons, blocknr) )
      {
         if( masterbranchcons != NULL )
         {
            ++nchildnodes;

            SCIP_CALL( SCIPcreateChild(masterscip, &child, 0.0, SCIPgetLocalTransEstimate(masterscip)) );
            SCIP_CALL( GCGcreateConsMasterbranch(masterscip, &childcons, child, GCGconsMasterbranchGetActiveCons(masterscip)) );
            SCIP_CALL( SCIPaddConsNode(masterscip, child, childcons, NULL) );

            /* define names for origbranch constraints */
            (void) SCIPsnprintf(childname, SCIP_MAXSTRLEN, "node(%d,%d, %d) last comp=%s, sense %d, bound %g", SCIPnodeGetNumber(child), blocknr, p+1,
               SCIPvarGetName(branchchilddata->consS[branchchilddata->consSsize-1].component),
               branchchilddata->consS[branchchilddata->consSsize-1].sense,
               branchchilddata->consS[branchchilddata->consSsize-1].bound);

            assert(branchchilddata != NULL);

            SCIP_CALL( GCGconsMasterbranchSetOrigConsData(masterscip, childcons, childname, branchrule, branchchilddata,
               NULL, 0, FALSE, FALSE, FALSE, NULL, 0, 2, 0) );

            /*  release constraints */
            SCIP_CALL( SCIPreleaseCons(masterscip, &childcons) );
         }
      }
      else
      {
         SCIPfreeMemoryArrayNull(scip, &(branchchilddata->consS));
         SCIPfreeMemoryNull(scip, &branchchilddata);
      }
   }
   SCIPdebugMessage("lhsSum = %g\n", lhsSum);

#ifdef SCIP_DEBUG
   SCIP_CALL( SCIPgetVarsData(masterscip, &mastervars, &nmastervars, NULL, NULL, NULL, NULL) );

  for( i=0; i<nmastervars; ++i )
  {
     SCIP_VAR* mastervar;
     SCIP_VAR** pricingvars;
     SCIP_Bool blockfound;
     int u;

     mastervar = mastervars[i];
     pricingvars = NULL;
     blockfound = FALSE;
     u = 0;

     if( GCGvarGetBlock(mastervar) == -1 && GCGvarIsLinking(mastervar) )
     {
        assert( GCGvarIsLinking(mastervar) );
        blockfound = FALSE;

        pricingvars = GCGlinkingVarGetPricingVars(mastervar);
        assert(pricingvars != NULL );

        for( u=0; u<GCGlinkingVarGetNBlocks(mastervar); ++u )
        {
           if( pricingvars[u] != NULL && GCGvarGetBlock(pricingvars[u]) == blocknr )
           {
              blockfound = TRUE;
              break;
           }
        }
     }
     else
     {
        blockfound = (GCGvarGetBlock(mastervar) == blocknr);
     }

     if( blockfound )
     {
        identicalcontrol += SCIPgetSolVal(masterscip, NULL, mastervar);
     }

  }
  if( !SCIPisEQ(scip, identicalcontrol, GCGrelaxGetNIdenticalBlocks(scip, blocknr)) )
  {
     SCIPdebugMessage("width of the block is only %g\n", identicalcontrol);
  }

  assert( SCIPisEQ(scip, identicalcontrol, GCGrelaxGetNIdenticalBlocks(scip, blocknr)) );
#endif

   assert( SCIPisEQ(scip, lhsSum, GCGrelaxGetNIdenticalBlocks(scip, blocknr) + Ssize) );

   SCIPfreeMemoryArray(scip, &mastervars2);
   SCIPfreeMemoryArray(scip, &copymastervars);

   if( nchildnodes <= 0 )
   {
      SCIPdebugMessage("node cut off, since all childnodes have been pruned\n");

      *result = SCIP_CUTOFF;
   }

   return SCIP_OKAY;
}

/** branching on copied origvar directly in master
 * @return SCIP_RETCODE */
static
SCIP_RETCODE branchDirectlyOnMastervar(
   SCIP*                scip,
   SCIP_VAR*            mastervar,
   SCIP_BRANCHRULE*     branchrule
   )
{
   SCIP* masterscip;
   GCG_BRANCHDATA* branchupchilddata;
   GCG_BRANCHDATA* branchdownchilddata;
   SCIP_NODE* upchild;
   SCIP_NODE* downchild;
   SCIP_CONS* upchildcons;
   SCIP_CONS* downchildcons;
   char upchildname[SCIP_MAXSTRLEN];
   char downchildname[SCIP_MAXSTRLEN];
   int bound;

   masterscip = GCGrelaxGetMasterprob(scip);
   assert(masterscip != NULL);

   bound = SCIPceil( scip, SCIPgetSolVal(masterscip, NULL, mastervar));

   /*  allocate branchdata for child and store information */
   SCIP_CALL( initNodeBranchdata(&branchupchilddata, -3) );
   SCIP_CALL( initNodeBranchdata(&branchdownchilddata, -3) );

   SCIP_CALL( SCIPallocMemoryArray(scip, &(branchupchilddata->consS), 1) );
   branchupchilddata->consSsize = 1;

   SCIP_CALL( SCIPallocMemoryArray(scip, &(branchdownchilddata->consS), 1) );
      branchdownchilddata->consSsize = 1;

   branchupchilddata->consS[0].component = mastervar;
   branchupchilddata->consS[0].sense = GCG_COMPSENSE_GE;
   branchupchilddata->consS[0].bound = bound;

   branchdownchilddata->consS[0].component = mastervar;
   branchdownchilddata->consS[0].sense = GCG_COMPSENSE_LT;
   branchdownchilddata->consS[0].bound = bound;


   (void) SCIPsnprintf(upchildname, SCIP_MAXSTRLEN, "node(1,-3, %f) direct up on comp=%s", branchupchilddata->consS[0].bound,
               SCIPvarGetName(branchupchilddata->consS[branchupchilddata->consSsize-1].component));
   (void) SCIPsnprintf(downchildname, SCIP_MAXSTRLEN, "node(1,-3, %f) direct up on comp=%s", branchdownchilddata->consS[0].bound,
               SCIPvarGetName(branchdownchilddata->consS[branchdownchilddata->consSsize-1].component));

   SCIP_CALL( SCIPcreateChild(masterscip, &upchild, 0.0, SCIPgetLocalTransEstimate(masterscip)) );
   SCIP_CALL( GCGcreateConsMasterbranch(masterscip, &upchildcons, upchild, GCGconsMasterbranchGetActiveCons(masterscip)) );
   SCIP_CALL( SCIPaddConsNode(masterscip, upchild, upchildcons, NULL) );

   SCIP_CALL( SCIPcreateChild(masterscip, &downchild, 0.0, SCIPgetLocalTransEstimate(masterscip)) );
   SCIP_CALL( GCGcreateConsMasterbranch(masterscip, &downchildcons, downchild, GCGconsMasterbranchGetActiveCons(masterscip)) );
   SCIP_CALL( SCIPaddConsNode(masterscip, downchild, downchildcons, NULL) );

   assert(branchupchilddata != NULL);
   SCIP_CALL( GCGconsMasterbranchSetOrigConsData(masterscip, upchildcons, upchildname, branchrule, branchupchilddata,
      NULL, 0, FALSE, FALSE, FALSE, NULL, 0, 2, 0) );

   assert(branchdownchilddata != NULL);
   SCIP_CALL( GCGconsMasterbranchSetOrigConsData(masterscip, downchildcons, downchildname, branchrule, branchdownchilddata,
      NULL, 0, FALSE, FALSE, FALSE, NULL, 0, 2, 0) );

   /*  release constraints */
   SCIP_CALL( SCIPreleaseCons(masterscip, &upchildcons) );

   SCIP_CALL( SCIPreleaseCons(masterscip, &downchildcons) );

   return SCIP_OKAY;
}


/** creates (integer) origsol with respect to the oder of the checkedblocks
 * @return SCIP_RETCODE */
static
SCIP_RETCODE createSortedOrigsol(
   SCIP*               scip,
   SCIP_VAR**          nonsortmastervars,
   int                 nnonsortmastervars,
   GCG_STRIP***        checkedblockssortstrips,
   int*                checkedblocksnsortstrips,
   int                 ncheckedblocks,
   SCIP_SOL**          origsol
)
{
   SCIP* masterprob;
   int npricingprobs;
   int* blocknrs;
   SCIP_Real* blockvalue;
   SCIP_Real increaseval;
   SCIP_VAR** sortmastervars;
   SCIP_Real* mastervals;
   SCIP_VAR** vars;
   int nvars;
   SCIP_Real feastol;
   int i;
   int j;
   int nsortmastervars;

#ifndef NDEBUG
   SCIP_SOL* mastersol;
#endif

   assert(scip != NULL);
   assert(origsol != NULL);

   masterprob = GCGrelaxGetMasterprob(scip);

#ifndef NDEBUG
   mastersol = SCIPgetBestSol(masterprob);
   assert( !SCIPisInfinity(scip, SCIPgetSolOrigObj(masterprob, mastersol)) );
#endif

   npricingprobs = GCGrelaxGetNPricingprobs(scip);


   SCIP_CALL( SCIPcreateSol(scip, origsol, GCGrelaxGetProbingheur(scip)) );

   SCIP_CALL( SCIPallocBufferArray(scip, &blockvalue, npricingprobs) );
   SCIP_CALL( SCIPallocBufferArray(scip, &blocknrs, npricingprobs) );

   nsortmastervars = 0;

   for( i=0;i<ncheckedblocks; ++i )
   {
      for( j=0; j<checkedblocksnsortstrips[i]; ++j )
      {
         ++nsortmastervars;
         if( nsortmastervars ==1 )
         {
            SCIP_CALL( SCIPallocBufferArray(scip, &sortmastervars, nsortmastervars) );
         }
         else
         {
            SCIP_CALL( SCIPreallocBufferArray(scip, &sortmastervars, nsortmastervars) );
         }
         sortmastervars[nsortmastervars -1] = checkedblockssortstrips[i][j]->mastervar;
         SCIP_CALL( SCIPreallocMemoryArray(scip, &mastervals, nsortmastervars) );
         mastervals[nsortmastervars -1] = SCIPgetSolVal(masterprob, NULL, checkedblockssortstrips[i][j]->mastervar);
      }
   }

   for( i=0; i<nnonsortmastervars; ++i )
   {
      ++nsortmastervars;
      if( nsortmastervars ==1 )
      {
         SCIP_CALL( SCIPallocBufferArray(scip, &sortmastervars, nsortmastervars) );
      }
      else
      {
         SCIP_CALL( SCIPreallocBufferArray(scip, &sortmastervars, nsortmastervars) );
      }
      sortmastervars[nsortmastervars -1] = nonsortmastervars[i];
      SCIP_CALL( SCIPreallocMemoryArray(scip, &mastervals, nsortmastervars) );
      mastervals[nsortmastervars -1] = SCIPgetSolVal(masterprob, NULL, nonsortmastervars[i]);

   }

   /* initialize the block values for the pricing problems */
   for( i = 0; i < npricingprobs; i++ )
   {
      blockvalue[i] = 0.0;
      blocknrs[i] = 0;
   }

   /* loop over all given master variables */
   for( i = 0; i < nsortmastervars; i++ )
   {
      SCIP_VAR** origvars;
      int norigvars;
      SCIP_Real* origvals;
      SCIP_Bool isray;
      int blocknr;

      origvars = GCGmasterVarGetOrigvars(sortmastervars[i]);
      norigvars = GCGmasterVarGetNOrigvars(sortmastervars[i]);
      origvals = GCGmasterVarGetOrigvals(sortmastervars[i]);
      blocknr = GCGvarGetBlock(sortmastervars[i]);
      isray = GCGmasterVarIsRay(sortmastervars[i]);

      assert(GCGvarIsMaster(sortmastervars[i]));
      assert(!SCIPisFeasNegative(scip, mastervals[i]));

      /** @todo handle infinite master solution values */
      assert(!SCIPisInfinity(scip, mastervals[i]));

      /* first of all, handle variables representing rays */
      if( isray )
      {
         assert(blocknr >= 0);
         /* we also want to take into account variables representing rays, that have a small value (between normal and feas eps),
          * so we do no feas comparison here */
         if( SCIPisPositive(scip, mastervals[i]) )
         {
            /* loop over all original variables contained in the current master variable */
            for( j = 0; j < norigvars; j++ )
            {
               if( SCIPisZero(scip, origvals[j]) )
                  break;

               assert(!SCIPisZero(scip, origvals[j]));

               /* the original variable is a linking variable: just transfer the solution value of the direct copy (this is done later) */
               if( GCGvarIsLinking(origvars[j]) )
                  continue;

               SCIPdebugMessage("Increasing value of %s by %f because of %s\n", SCIPvarGetName(origvars[j]), origvals[j] * mastervals[i], SCIPvarGetName(sortmastervars[i]));
               /* increase the corresponding value */
               SCIP_CALL( SCIPincSolVal(scip, *origsol, origvars[j], origvals[j] * mastervals[i]) );
            }
         }
         mastervals[i] = 0.0;
         continue;
      }

      /* handle the variables with value >= 1 to get integral values in original solution */
      while( SCIPisFeasGE(scip, mastervals[i], 1.0) )
      {
         /* variable was directly transferred to the master problem (only in linking conss or linking variable) */
         /** @todo this may be the wrong place for this case, handle it before the while loop
          * and remove the similar case in the next while loop */
         if( blocknr == -1 )
         {
            assert(norigvars == 1);
            assert(origvals[0] == 1.0);

            /* increase the corresponding value */
            SCIPdebugMessage("Increasing value of %s by %f because of %s\n", SCIPvarGetName(origvars[0]), origvals[0] * mastervals[i],  SCIPvarGetName(sortmastervars[i]));
            SCIP_CALL( SCIPincSolVal(scip, *origsol, origvars[0], origvals[0] * mastervals[i]) );
            mastervals[i] = 0.0;
         }
         else
         {
            assert(blocknr >= 0);
            /* loop over all original variables contained in the current master variable */
            for( j = 0; j < norigvars; j++ )
            {
               SCIP_VAR* pricingvar;
               int norigpricingvars;
               SCIP_VAR** origpricingvars;
               if( SCIPisZero(scip, origvals[j]) )
                  break;
               assert(!SCIPisZero(scip, origvals[j]));

               /* the original variable is a linking variable: just transfer the solution value of the direct copy (this is done above) */
               if( GCGvarIsLinking(origvars[j]) )
                  continue;

               pricingvar = GCGoriginalVarGetPricingVar(origvars[j]);
               assert(GCGvarIsPricing(pricingvar));

               norigpricingvars = GCGpricingVarGetNOrigvars(pricingvar);
               origpricingvars = GCGpricingVarGetOrigvars(pricingvar);

               /* just in case a variable has a value higher than the number of blocks, it represents */
               if( norigpricingvars <= blocknrs[blocknr] )
               {
                  SCIPdebugMessage("Increasing value of %s by %f because of %s\n", SCIPvarGetName(origpricingvars[norigpricingvars-1]), mastervals[i] * origvals[j], SCIPvarGetName(sortmastervars[i]));
                  /* increase the corresponding value */
                  SCIP_CALL( SCIPincSolVal(scip, *origsol, origpricingvars[norigpricingvars-1], mastervals[i] * origvals[j]) );
                  mastervals[i] = 1.0;
               }
               /* this should be default */
               else
               {
                  SCIPdebugMessage("Increasing value of %s by %f because of %s\n", SCIPvarGetName(origpricingvars[blocknrs[blocknr]]), origvals[j], SCIPvarGetName(sortmastervars[i]) );
                  /* increase the corresponding value */
                  SCIP_CALL( SCIPincSolVal(scip, *origsol, origpricingvars[blocknrs[blocknr]], origvals[j]) );
               }
            }
            mastervals[i] = mastervals[i] - 1.0;
            blocknrs[blocknr]++;
         }
      }

      SCIPfreeBufferArray(scip, &sortmastervars);
      SCIPfreeMemoryArray(scip, &mastervals);

      return SCIP_OKAY;
   }

/* loop over all given master variables */
for( i = 0; i < nsortmastervars; i++ )
{
   SCIP_VAR** origvars;
   int norigvars;
   SCIP_Real* origvals;
   int blocknr;

   origvars = GCGmasterVarGetOrigvars(sortmastervars[i]);
   norigvars = GCGmasterVarGetNOrigvars(sortmastervars[i]);
   origvals = GCGmasterVarGetOrigvals(sortmastervars[i]);
   blocknr = GCGvarGetBlock(sortmastervars[i]);

   if( SCIPisFeasZero(scip, mastervals[i]) )
   {
      continue;
   }
   assert(SCIPisFeasGE(scip, mastervals[i], 0.0) && SCIPisFeasLT(scip, mastervals[i], 1.0));

   while( SCIPisFeasPositive(scip, mastervals[i]) )
   {
      assert(GCGvarIsMaster(sortmastervars[i]));
      assert(!GCGmasterVarIsRay(sortmastervars[i]));

      if( blocknr == -1 )
      {
         assert(norigvars == 1);
         assert(origvals[0] == 1.0);

         SCIPdebugMessage("Increasing value of %s by %f because of %s\n", SCIPvarGetName(origvars[0]), origvals[0] * mastervals[i], SCIPvarGetName(sortmastervars[i]) );
         /* increase the corresponding value */
         SCIP_CALL( SCIPincSolVal(scip, *origsol, origvars[0], origvals[0] * mastervals[i]) );
         mastervals[i] = 0.0;
      }
      else
      {
         increaseval = MIN(mastervals[i], 1.0 - blockvalue[blocknr]);
         /* loop over all original variables contained in the current master variable */
         for( j = 0; j < norigvars; j++ )
         {
            SCIP_VAR* pricingvar;
            int norigpricingvars;
            SCIP_VAR** origpricingvars;

            if( SCIPisZero(scip, origvals[j]) )
               continue;

            /* the original variable is a linking variable: just transfer the solution value of the direct copy (this is done above) */
            if( GCGvarIsLinking(origvars[j]) )
               continue;

            pricingvar = GCGoriginalVarGetPricingVar(origvars[j]);
            assert(GCGvarIsPricing(pricingvar));

            norigpricingvars = GCGpricingVarGetNOrigvars(pricingvar);
            origpricingvars = GCGpricingVarGetOrigvars(pricingvar);

            if( norigpricingvars <= blocknrs[blocknr] )
            {
               increaseval = mastervals[i];

               SCIPdebugMessage("Increasing value of %s by %f because of %s\n", SCIPvarGetName(origpricingvars[norigpricingvars-1]), origvals[j] * increaseval, SCIPvarGetName(sortmastervars[i]) );
               /* increase the corresponding value */
               SCIP_CALL( SCIPincSolVal(scip, *origsol, origpricingvars[norigpricingvars-1], origvals[j] * increaseval) );
            }
            else
            {
               /* increase the corresponding value */
               SCIPdebugMessage("Increasing value of %s by %f because of %s\n", SCIPvarGetName(origpricingvars[blocknrs[blocknr]]), origvals[j] * increaseval, SCIPvarGetName(sortmastervars[i]) );
               SCIP_CALL( SCIPincSolVal(scip, *origsol, origpricingvars[blocknrs[blocknr]], origvals[j] * increaseval) );
            }
         }

         mastervals[i] = mastervals[i] - increaseval;
         if( SCIPisFeasZero(scip, mastervals[i]) )
         {
            mastervals[i] = 0.0;
         }
         blockvalue[blocknr] += increaseval;

         /* if the value assigned to the block is equal to 1, this block is full and we take the next block */
         if( SCIPisFeasGE(scip, blockvalue[blocknr], 1.0) )
         {
            blockvalue[blocknr] = 0.0;
            blocknrs[blocknr]++;
         }
      }
   }
}

SCIPfreeBufferArray(scip, &mastervals);
SCIPfreeBufferArray(scip, &blocknrs);
SCIPfreeBufferArray(scip, &blockvalue);

/* if the solution violates one of its bounds by more than feastol
 * and less than 10*feastol, round it and print a warning
 */
SCIP_CALL( SCIPgetVarsData(scip, &vars, &nvars, NULL, NULL, NULL, NULL) );
SCIP_CALL( SCIPgetRealParam(scip, "numerics/feastol", &feastol) );
for( i = 0; i < nvars; ++i )
{
   SCIP_Real solval;
   SCIP_Real lb;
   SCIP_Real ub;

   solval = SCIPgetSolVal(scip, *origsol, vars[i]);
   lb = SCIPvarGetLbLocal(vars[i]);
   ub = SCIPvarGetUbLocal(vars[i]);

   if( SCIPisFeasGT(scip, solval, ub) && EPSEQ(solval, ub, 10 * feastol) )
   {
      SCIP_CALL( SCIPsetSolVal(scip, *origsol, vars[i], ub) );
      SCIPwarningMessage(scip, "Variable %s rounded from %g to %g in relaxation solution\n",
         SCIPvarGetName(vars[i]), solval, ub);
   }
   else if( SCIPisFeasLT(scip, solval, lb) && EPSEQ(solval, lb, 10 * feastol) )
   {
      SCIP_CALL( SCIPsetSolVal(scip, *origsol, vars[i], lb) );
      SCIPwarningMessage(scip, "Variable %s rounded from %g to %g in relaxation solution\n",
         SCIPvarGetName(vars[i]), solval, lb);
   }
}
return SCIP_OKAY;
}


/** prepares informations for using the generic branching scheme
 * @return SCIP_RETCODE */
SCIP_RETCODE GCGbranchGenericInitbranch(
   SCIP*                masterscip,         /**< */
   SCIP_BRANCHRULE*     branchrule,
   SCIP_RESULT*         result,
   int*                 checkedblocks,
   int                  ncheckedblocks,
   GCG_STRIP***         checkedblockssortstrips,
   int*                 checkedblocksnsortstrips
   )
{
   SCIP* origscip;
   SCIP_Bool feasible;
   SCIP_Bool SinC;
   SCIP_Bool foundblocknr;
   SCIP_VAR** branchcands;
   SCIP_VAR** allorigvars;
   SCIP_VAR** mastervars;
   SCIP_VAR** nonsortmastervars;
   int nmastervars;
   SCIP_CONS* masterbranchcons;
   SCIP_CONS* parentcons;
   int nbranchcands;
   GCG_BRANCHDATA* branchdata;
   SCIP_VAR* mastervar;
   SCIP_Real mastervarValue;
   GCG_COMPSEQUENCE* S;
   GCG_COMPSEQUENCE** C;
   SCIP_SOL* origsol;
   SCIP_VAR** F;
   SCIP_VAR** pricingvars;
   int Ssize;
   int Csize;
   int Fsize;
   int nnonsortmastervars;
   int* sequencesizes;
   int blocknr;
   int i;
   int j;
   int c;
   int allnorigvars;

   blocknr = -2;
   Ssize = 0;
   Csize = 0;
   Fsize = 0;
   nnonsortmastervars = 0;
   i = 0;
   j = 0;
   c = 0;
   feasible = TRUE;
   SinC = TRUE;
   foundblocknr = FALSE;
   branchdata = NULL;
   S = NULL;
   C = NULL;
   F = NULL;
   sequencesizes = NULL;
   pricingvars = NULL;
   nonsortmastervars = NULL;

   assert(masterscip != NULL);

   SCIPdebugMessage("get informations for Vanderbecks generic branching\n");

   origscip = GCGpricerGetOrigprob(masterscip);

   assert(origscip != NULL);
   SCIP_CALL( SCIPgetLPBranchCands(masterscip, &branchcands, NULL, NULL, &nbranchcands, NULL, NULL) );

   SCIP_CALL( SCIPgetVarsData(origscip, &allorigvars, &allnorigvars, NULL, NULL, NULL, NULL) );
   SCIP_CALL( SCIPgetVarsData(masterscip, &mastervars, &nmastervars, NULL, NULL, NULL, NULL) );

   assert(nbranchcands > 0);

   for( i=0; i<nbranchcands; ++i )
   {
      mastervar = branchcands[i];
      assert(GCGvarIsMaster(mastervar));
      blocknr = GCGvarGetBlock(mastervar);

      if( blocknr == -1 )
      {
         if( GCGvarIsLinking(mastervar) )
         {

            pricingvars = GCGlinkingVarGetPricingVars(mastervar);
            assert(pricingvars != NULL );

            for( i=0; i<GCGlinkingVarGetNBlocks(mastervar); ++i )
            {
               if( pricingvars[i] != NULL )
               {
                  blocknr = GCGvarGetBlock(pricingvars[i]);

                  foundblocknr = TRUE;

                  for( j=0; j<ncheckedblocks; ++j )
                  {
                     if( checkedblocks[j] == blocknr )
                        foundblocknr = FALSE;
                  }
                  if( foundblocknr )
                  {
                     break;
                  }
               }
            }
            if( blocknr > -1 )
               break;
         }
         else
            break;
      }

      if( blocknr > -1 )
      {
         foundblocknr = TRUE;
         for( j=0; j<ncheckedblocks; ++j )
         {
            if( checkedblocks[j] == blocknr )
            {
               foundblocknr = FALSE;
               break;
            }
         }
         if( foundblocknr )
            break;
      }
   }

   assert( i <= nbranchcands ); /* else all blocks has been checked and we can observe an integer solution */
   if( i > nbranchcands )
   {
      /* keep up the sort of non checked blocks */

      SCIP_CALL( SCIPgetVarsData(masterscip, &mastervars, &nmastervars, NULL, NULL, NULL, NULL) );
      /* if the block was note checked, you cannot change the order in it */
      nnonsortmastervars = 0;

      /* todo handle linking variables, may be multiple in ckeckedblocksstrips */
      for( i=0; i<nmastervars; ++i )
      {
         SCIP_Bool blockchecked;

         mastervar = mastervars[i];
         blocknr = GCGvarGetBlock(mastervar);
         blockchecked = FALSE;

         for( j=0; j<ncheckedblocks; ++j )
         {
            if( checkedblocks[j] == blocknr )
            {
               blockchecked = TRUE;
               break;
            }
         }

         if( !blockchecked )
         {
            ++nnonsortmastervars;
            if( nnonsortmastervars == 1 )
            {
               SCIP_CALL( SCIPallocBufferArray(origscip, &nonsortmastervars, nnonsortmastervars) );
            }
            else
            {
               SCIP_CALL( SCIPreallocBufferArray(origscip, &nonsortmastervars, nnonsortmastervars) );
            }
            nonsortmastervars[nnonsortmastervars-1] = mastervar;
         }
      }

      SCIP_CALL( createSortedOrigsol(origscip, nonsortmastervars, nnonsortmastervars, checkedblockssortstrips, checkedblocksnsortstrips, ncheckedblocks, &origsol) );

      /* try new solution to original problem and free it immediately */
#ifdef SCIP_DEBUG
      SCIP_CALL( SCIPtrySolFree(origscip, &origsol, TRUE, TRUE, TRUE, TRUE, &feasible) );
#else
      SCIP_CALL( SCIPtrySolFree(origscip, &origsol, FALSE, TRUE, TRUE, TRUE, &feasible) );
#endif

      /* free memory */
      if( ncheckedblocks > 0 )
      {
         SCIPfreeBufferArray(origscip, &checkedblocks);

         for( i=0; i< ncheckedblocks; ++i )
         {
            j = 0;

            for( j=0; j< checkedblocksnsortstrips[i]; ++j )
            {
               SCIPfreeBuffer(origscip, &(checkedblockssortstrips[i][j]) );
            }

            SCIPfreeBufferArray(origscip, &(checkedblockssortstrips[i]) );
         }

         if( nnonsortmastervars > 0 )
            SCIPfreeBufferArray(origscip, &nonsortmastervars);

         SCIPfreeBufferArray(origscip, &checkedblockssortstrips);
         SCIPfreeBufferArray(origscip, &checkedblocksnsortstrips);

         ncheckedblocks = 0;
      }

      assert(feasible); /*handle linking vars*/

      *result = SCIP_CUTOFF;
      return SCIP_OKAY;
   }

   if( blocknr < -1 )
   {
      feasible = TRUE;
      SCIPdebugMessage("Vanderbeck generic branching rule could not find variables to branch on!\n");

      return -1;
   }
   else
      feasible = FALSE;

   /* a special case; branch on copy of an origvar directly:- here say blocknr = -3 */
   if( blocknr == -1 && !GCGvarIsLinking(mastervar) )
      blocknr = -3;

   masterbranchcons = GCGconsMasterbranchGetActiveCons(masterscip);
   SCIPdebugMessage("branching in block %d \n", blocknr);

   if( blocknr == -3 )
   {
      /* direct branch on copied origvar */
      SCIP_CALL( branchDirectlyOnMastervar(origscip, mastervar, branchrule) );

      return SCIP_OKAY;
   }

   /* calculate F and the strips */
   for( i=0; i<nbranchcands; ++i )
   {
      SCIP_Bool blockfound;
      int k;

      k = 0;
      mastervar = branchcands[i];
      assert(GCGvarIsMaster(mastervar));

      blockfound = TRUE;

      if( GCGvarGetBlock(mastervar) == -1 && GCGvarIsLinking(mastervar) )
      {
         blockfound = FALSE;

         pricingvars = GCGlinkingVarGetPricingVars(mastervar);
         assert(pricingvars != NULL );

         for( k=0; k<GCGlinkingVarGetNBlocks(mastervar); ++k )
         {
            if( pricingvars[k] != NULL )
            {
               if( GCGvarGetBlock(pricingvars[k]) == GCGbranchGenericBranchdataGetConsblocknr(branchdata) )
               {
                  blockfound = TRUE;
                  break;
               }
            }
         }
      }
      else
      {
         blockfound = (blocknr == GCGvarGetBlock(mastervar));
      }

      if( blockfound )
      {
         mastervarValue = SCIPgetSolVal(masterscip, NULL, mastervar);
         if( SCIPisGT(origscip, mastervarValue - SCIPfloor(origscip, mastervarValue), 0) )
         {

            if( Fsize == 0 )
            {
               SCIP_CALL( SCIPallocMemoryArray(origscip, &F, Fsize+1) );
            }
            else
            {
               SCIP_CALL( SCIPreallocMemoryArray(origscip, &F, Fsize+1) );
            }
            F[Fsize] = mastervar;
            ++Fsize;
         }
      }
   }

   /* old data to regard? */
   if( masterbranchcons != NULL && GCGconsMasterbranchGetBranchdata(masterbranchcons) != NULL )
   {
      /* calculate C */
      parentcons = masterbranchcons;
      Csize = 0;
      while( parentcons != NULL && GCGconsMasterbranchGetbranchrule(parentcons) != NULL
         && strcmp(SCIPbranchruleGetName(GCGconsMasterbranchGetbranchrule(parentcons)), "generic") == 0)
      {
         branchdata = GCGconsMasterbranchGetBranchdata(parentcons);
         if( branchdata == NULL )
         {
            SCIPdebugMessage("branchdata is NULL\n");
            break;
         }
         if( branchdata->consS == NULL || branchdata->consSsize == 0 )
            break;
         if( branchdata->consblocknr != blocknr )
         {
            parentcons = GCGconsMasterbranchGetParentcons(parentcons);
            continue;
         }
         if( Csize == 0 )
         {
            assert(branchdata != NULL);
            assert(branchdata->consSsize > 0);
            Csize = 1;
            SCIP_CALL( SCIPallocMemoryArray(origscip, &C, Csize) );
            SCIP_CALL( SCIPallocMemoryArray(origscip, &sequencesizes, Csize) );
            C[0] = NULL;
            SCIP_CALL( SCIPallocMemoryArray(origscip, &(C[0]), branchdata->consSsize) );
            for( i=0; i<branchdata->consSsize; ++i )
            {
               C[0][i] = branchdata->consS[i];
            }
            sequencesizes[0] = branchdata->consSsize;
            parentcons = GCGconsMasterbranchGetParentcons(parentcons);
         }
         else
         {
            /* S not yet in C ? */
            SinC = FALSE;
            for( c=0; c<Csize && !SinC; ++c )
            {
               SinC = TRUE;
               if( branchdata->consSsize == sequencesizes[c] )
               {
                  for( i=0; i<branchdata->consSsize; ++i )
                  {
                     if( branchdata->consS[i].component != C[c][i].component || branchdata->consS[i].sense != C[c][i].sense || !SCIPisEQ(origscip, branchdata->consS[i].bound, C[c][i].bound) )
                     {
                        SinC = FALSE;
                        break;
                     }
                  }
               }
               else
                  SinC = FALSE;
            }
            if( !SinC )
            {
               ++Csize;
               SCIP_CALL( SCIPreallocMemoryArray(origscip, &C, Csize) );
               SCIP_CALL( SCIPreallocMemoryArray(origscip, &sequencesizes, Csize) );
               C[Csize-1] = NULL;
               SCIP_CALL( SCIPallocMemoryArray(origscip, &(C[Csize-1]), branchdata->consSsize) );

               /** @todo copy memory */
               for( i=0; i<branchdata->consSsize; ++i )
               {
                  C[Csize-1][i] = branchdata->consS[i];
               }
               sequencesizes[Csize-1] = branchdata->consSsize;
            }
            parentcons = GCGconsMasterbranchGetParentcons(parentcons);
         }
      }

      if( C != NULL )
      {
         SCIPdebugMessage("Csize = %d\n", Csize);

         for( i=0; i<Csize; ++i )
         {
            for( c=0; c<sequencesizes[i]; ++c )
            {
               SCIPdebugMessage("C[%d][%d].component = %s\n", i, c, SCIPvarGetName(C[i][c].component) );
               SCIPdebugMessage("C[%d][%d].sense = %d\n", i, c, C[i][c].sense);
               SCIPdebugMessage("C[%d][%d].bound = %.6g\n", i, c, C[i][c].bound);
            }
         }
         /* SCIP_CALL( InducedLexicographicSort(scip, F, Fsize, C, Csize, sequencesizes) ); */
         SCIP_CALL( ChooseSeparateMethod(origscip, F, Fsize, &S, &Ssize, C, Csize, sequencesizes, blocknr, branchrule, result, checkedblocks,
            ncheckedblocks,
            checkedblockssortstrips,
            checkedblocksnsortstrips) );
      }
      else
      {
         SCIPdebugMessage("C == NULL\n");
         /* SCIP_CALL( InducedLexicographicSort( scip, F, Fsize, NULL, 0, NULL ) ); */
         SCIP_CALL( ChooseSeparateMethod( origscip, F, Fsize, &S, &Ssize, NULL, 0, NULL, blocknr, branchrule, result, checkedblocks,
            ncheckedblocks,
            checkedblockssortstrips,
            checkedblocksnsortstrips) );
      }
      if( sequencesizes != NULL )
      {
         assert(Csize > 0);
         SCIPfreeMemoryArray(origscip, &sequencesizes);
      }
      for( i=0; i<Csize; ++i )
      {
         SCIPfreeMemoryArrayNull(origscip, &(C[i]));
      }
      if( C != NULL )
      {
         assert( Csize > 0);
         SCIPfreeMemoryArrayNull(origscip, &C);
      }
   }
   else
   {
      SCIPdebugMessage("root node\n");
      /* SCIP_CALL( InducedLexicographicSort( scip, F, Fsize, NULL, 0, NULL ) ); */
      SCIP_CALL( ChooseSeparateMethod( origscip, F, Fsize, &S, &Ssize, NULL, 0, NULL, blocknr, branchrule, result, checkedblocks,
         ncheckedblocks,
         checkedblockssortstrips,
         checkedblocksnsortstrips) );
   }

   if( feasible )
   {
      SCIPdebugMessage("Vanderbeck generic branching rule could not find variables to branch on!\n");
      return -1;
   }

   /* create the |S|+1 child nodes in the branch-and-bound tree */
   if( S != NULL && Ssize > 0 )
      SCIP_CALL( createChildNodesGeneric(origscip, branchrule, S, Ssize, blocknr, masterbranchcons, result) );

   SCIPdebugMessage("free F\n");
   SCIPfreeMemoryArray(origscip, &F);
   SCIPfreeMemoryArrayNull(origscip, &S);

   return SCIP_OKAY;
}

/* from branch_master */
static
SCIP_RETCODE GCGincludeMasterCopyPlugins(
   SCIP*                 scip                /**< SCIP data structure */
   )
{
   SCIP_CALL( SCIPincludeNodeselBfs(scip) );
   SCIP_CALL( SCIPincludeNodeselDfs(scip) );
   SCIP_CALL( SCIPincludeNodeselEstimate(scip) );
   SCIP_CALL( SCIPincludeNodeselHybridestim(scip) );
   SCIP_CALL( SCIPincludeNodeselRestartdfs(scip) );
   SCIP_CALL( SCIPincludeBranchruleAllfullstrong(scip) );
   SCIP_CALL( SCIPincludeBranchruleFullstrong(scip) );
   SCIP_CALL( SCIPincludeBranchruleInference(scip) );
   SCIP_CALL( SCIPincludeBranchruleMostinf(scip) );
   SCIP_CALL( SCIPincludeBranchruleLeastinf(scip) );
   SCIP_CALL( SCIPincludeBranchrulePscost(scip) );
   SCIP_CALL( SCIPincludeBranchruleRandom(scip) );
   SCIP_CALL( SCIPincludeBranchruleRelpscost(scip) );
   return SCIP_OKAY;
}
/** copy method for master branching rule */
static
SCIP_DECL_BRANCHCOPY(branchCopyGeneric)
{
   assert(branchrule != NULL);
   assert(scip != NULL);
   SCIP_CALL( GCGincludeMasterCopyPlugins(scip) );
   return SCIP_OKAY;
}

/** callback activation method */
static
GCG_DECL_BRANCHACTIVEMASTER(branchActiveMasterGeneric)
{
   SCIP* origscip;
   SCIP_VAR** mastervars;
   SCIP_VAR** copymastervars;
   SCIP_VAR** allorigvars;
   int allnorigvars;
   int nmastervars;
   int nvarsadded;
   int nnewmastervars;
   int i;
   int p;
   char name[SCIP_MAXSTRLEN];

   i = 0;
   p = 0;
   nmastervars = 0;
   nvarsadded = 0;
   copymastervars = NULL;

   assert(scip != NULL);
   assert(branchdata != NULL);

   origscip = GCGpricerGetOrigprob(scip);
   assert(origscip != NULL);


   if( branchdata->consblocknr == -3 )
   {
      assert(branchdata->consSsize == 1);
      (void) SCIPsnprintf(name, SCIP_MAXSTRLEN, "directchild(%d, %g) sense = %d",
            branchdata->consSsize, branchdata->consS[0].bound, branchdata->consS[0].sense);

      /*  create constraint for child */
      if( branchdata->consS[0].sense == GCG_COMPSENSE_GE )
      {
         SCIP_CALL( SCIPcreateConsLinear(scip, &(branchdata->mastercons), name, 0, NULL, NULL,
            branchdata->consS[0].bound, SCIPinfinity(origscip), TRUE, TRUE, TRUE, TRUE, TRUE, TRUE, TRUE, FALSE, FALSE, TRUE) );
      }
      else
      {
         SCIP_CALL( SCIPcreateConsLinear(scip, &(branchdata->mastercons), name, 0, NULL, NULL,
            -SCIPinfinity(origscip), branchdata->consS[0].bound-1, TRUE, TRUE, TRUE, TRUE, TRUE, TRUE, TRUE, FALSE, FALSE, TRUE) );
      }

      SCIP_CALL( SCIPaddCoefLinear(scip, branchdata->mastercons, branchdata->consS[0].component, 1.0) );

      /* add constraint to the master problem that enforces the branching decision */
      SCIP_CALL( SCIPaddCons(scip, branchdata->mastercons) );

      return SCIP_OKAY;
   }

   SCIP_CALL( SCIPgetVarsData(scip, &mastervars, &nmastervars, NULL, NULL, NULL, NULL) );
   SCIP_CALL( SCIPgetVarsData(origscip, &allorigvars, &allnorigvars, NULL, NULL, NULL, NULL) );

   SCIP_CALL( SCIPduplicateMemoryArray(origscip, &copymastervars, mastervars, nmastervars) );

   SCIPdebugMessage("branchActiveMasterGeneric: Block %d, Ssize %d)\n", branchdata->consblocknr,
      branchdata->consSsize);

   assert( (branchdata->consSsize == 0 ) == (branchdata->consS == NULL) );

   if( branchdata->consS == NULL )
   {
      assert(branchdata->consSsize == 0);
      SCIPdebugMessage("root node:\n");
      return SCIP_OKAY;
   }

   /* create corresponding constraint in the master problem, if not yet created */
   if( branchdata->mastercons == NULL && branchdata->consSsize > 0 )
   {

      (void) SCIPsnprintf(name, SCIP_MAXSTRLEN, "child(%d, %g)", branchdata->consSsize, branchdata->lhs);

      /*  create constraint for child */
      SCIP_CALL( SCIPcreateConsLinear(scip, &(branchdata->mastercons), name, 0, NULL, NULL,
            branchdata->lhs, SCIPinfinity(origscip), TRUE, TRUE, TRUE, TRUE, TRUE, TRUE, TRUE, FALSE, FALSE, TRUE) );

      /* add mastervars */
      for( p=0; p< branchdata->consSsize; ++p )
      {
         nnewmastervars = nmastervars;
         for( i=0; i<nnewmastervars; ++i )
         {
            SCIP_Real generator_i;
            SCIP_VAR** pricingvars;
            int k;
            SCIP_Bool blockfound;

            if( i >= nmastervars )
               break;


            if( GCGvarGetBlock(copymastervars[i]) == branchdata->consblocknr
               || ( GCGvarGetBlock(copymastervars[i]) == -1 && GCGvarIsLinking(copymastervars[i])) )
            {
               blockfound = TRUE;

               if( GCGvarGetBlock(copymastervars[i]) == -1 )
               {
                  assert( GCGvarIsLinking(copymastervars[i]) );
                  blockfound = FALSE;

                  pricingvars = GCGlinkingVarGetPricingVars(copymastervars[i]);
                  assert(pricingvars != NULL );

                  for( k=0; k<GCGlinkingVarGetNBlocks(copymastervars[i]); ++k )
                  {
                     if( pricingvars[k] != NULL )
                     {
                        if( GCGvarGetBlock(pricingvars[k]) == branchdata->consblocknr )
                        {
                           blockfound = TRUE;
                           break;
                        }
                     }
                  }
               }
               if( !blockfound )
               {
                  /* small down array */
                  copymastervars[i] = copymastervars[nmastervars-1];
                  --i;
                  --nmastervars;
                  continue;
               }

               generator_i = getGeneratorEntry(copymastervars[i], branchdata->consS[p].component);

               if( branchdata->consS[p].sense == GCG_COMPSENSE_GE )
               {
                  if( SCIPisGE(origscip, generator_i, branchdata->consS[p].bound) )
                  {
                     if( p == branchdata->consSsize-1 )
                     {
                        /* add var to constraint */
                        ++nvarsadded;
                        SCIP_CALL( SCIPaddCoefLinear(scip, branchdata->mastercons, copymastervars[i], 1.0) );
                     }
                  }
                  else
                  {
                     /* small down array */
                     copymastervars[i] = copymastervars[nmastervars-1];
                     --i;
                     --nmastervars;
                  }
               }
               else
               {
                  if( SCIPisLT(origscip, generator_i, branchdata->consS[p].bound) )
                  {
                     if( p == branchdata->consSsize-1 )
                     {
                        /* add var to constraint */
                        ++nvarsadded;
                        SCIP_CALL( SCIPaddCoefLinear(scip, branchdata->mastercons, copymastervars[i], 1.0) );
                     }
                  }
                  else
                  {
                     /* small down array */
                     copymastervars[i] = copymastervars[nmastervars-1];
                     --i;
                     --nmastervars;
                  }
               }
            }
            else
            {
               /* small down array */
               copymastervars[i] = copymastervars[nmastervars-1];
               --i;
               --nmastervars;
            }
         }
      }
   }
   /* add constraint to the master problem that enforces the branching decision */
   SCIP_CALL( SCIPaddCons(scip, branchdata->mastercons) );

   SCIPdebugMessage("%d vars added with lhs=%g\n", nvarsadded, branchdata->lhs);
   assert(nvarsadded > 0);

   SCIPfreeMemoryArrayNull(origscip, &copymastervars);

   return SCIP_OKAY;
}

/** callback deactivation method */
static
GCG_DECL_BRANCHDEACTIVEMASTER(branchDeactiveMasterGeneric)
{
   assert(scip != NULL);
   assert(branchdata != NULL);
   assert(branchdata->mastercons != NULL);

   SCIPdebugMessage("branchDeactiveMasterGeneric: Block %d, Ssize %d\n", branchdata->consblocknr,
      branchdata->consSsize);

   /* remove constraint from the master problem that enforces the branching decision */
   assert(branchdata->mastercons != NULL);
   SCIP_CALL( SCIPdelCons(scip, branchdata->mastercons) );
   SCIP_CALL( SCIPreleaseCons(scip, &(branchdata->mastercons)) );
   branchdata->mastercons = NULL;

   return SCIP_OKAY;
}



/** callback propagation method */
static
GCG_DECL_BRANCHPROPMASTER(branchPropMasterGeneric)
{
   assert(scip != NULL);
   assert(branchdata != NULL);
   assert(branchdata->mastercons != NULL);
   assert(branchdata->consS != NULL);

   /* SCIPdebugMessage("branchPropMasterGeneric: Block %d ,Ssize %d)\n", branchdata->consblocknr, branchdata->consSsize); */

   return SCIP_OKAY;
}

/** branching execution method for fractional LP solutions */
static
SCIP_DECL_BRANCHEXECLP(branchExeclpGeneric)
{  /*lint --e{715}*/
   SCIP* origscip;
   SCIP_Bool feasible;
   SCIP_Bool discretization;

   feasible = TRUE;

   assert(branchrule != NULL);
   assert(strcmp(SCIPbranchruleGetName(branchrule), BRANCHRULE_NAME) == 0);
   assert(scip != NULL);
   assert(result != NULL);

   origscip = GCGpricerGetOrigprob(scip);
   assert(origscip != NULL);

   SCIPdebugMessage("Execrel method of Vanderbecks generic branching\n");

   *result = SCIP_DIDNOTRUN;

   /* the branching scheme only works for the discretization approach */
   SCIP_CALL( SCIPgetBoolParam(origscip, "relaxing/gcg/discretization", &discretization) );
   if( !discretization )
   {
      SCIPdebugMessage("Generic branching only for discretization approach\n");
      return SCIP_OKAY;
   }

   if( GCGrelaxIsMasterSetCovering(origscip) || GCGrelaxIsMasterSetPartitioning(origscip) )
   {
      SCIPdebugMessage("Generic branching executed on a set covering or set partitioning problem\n");
   }

   /* check whether the current original solution is integral */
#ifdef SCIP_DEBUG
   SCIP_CALL( SCIPcheckSol(scip, GCGrelaxGetCurrentOrigSol(origscip), TRUE, TRUE, TRUE, TRUE, &feasible) );
#else
   SCIP_CALL( SCIPcheckSol(scip, GCGrelaxGetCurrentOrigSol(origscip), FALSE, TRUE, TRUE, TRUE, &feasible) );
#endif

   if( feasible )
   {
      SCIPdebugMessage("node cut off, since origsol was feasible, solval = %f\n",
         SCIPgetSolOrigObj(origscip, GCGrelaxGetCurrentOrigSol(origscip)));

      *result = SCIP_CUTOFF;
      return SCIP_OKAY;
   }

   *result = SCIP_BRANCHED;

   SCIP_CALL( GCGbranchGenericInitbranch(scip, branchrule, result, NULL, 0, NULL, NULL) );

   return SCIP_OKAY;
}

/** branching execution method for relaxation solutions */
static
SCIP_DECL_BRANCHEXECEXT(branchExecextGeneric)
{  /*lint --e{715}*/
   SCIPdebugMessage("Execext method of generic branching\n");

   *result = SCIP_DIDNOTRUN;

   return SCIP_OKAY;
}

/** branching execution method for not completely fixed pseudo solutions */ /*todo*/
static
SCIP_DECL_BRANCHEXECPS(branchExecpsGeneric)
{  /*lint --e{715}*/
   SCIP_CONS* masterbranchcons;
   GCG_BRANCHDATA* branchdata;

   assert(branchrule != NULL);
   assert(strcmp(SCIPbranchruleGetName(branchrule), BRANCHRULE_NAME) == 0);
   assert(scip != NULL);
   assert(result != NULL);

   SCIPdebugMessage("Execps method of Vanderbecks generic branching\n");

   return SCIP_OKAY;

   *result = SCIP_DIDNOTRUN;

   masterbranchcons = GCGconsMasterbranchGetActiveCons(scip);

   if( masterbranchcons != NULL )
      branchdata = GCGconsMasterbranchGetBranchdata(masterbranchcons);

   if( branchdata != NULL )
   {
      *result = SCIP_BRANCHED;
   }

   return SCIP_OKAY;
}

/** initialization method of branching rule (called after problem was transformed) */
static
SCIP_DECL_BRANCHINIT(branchInitGeneric)
{
   SCIP* origscip;

   origscip = GCGpricerGetOrigprob(scip);
   assert(branchrule != NULL);
   assert(origscip != NULL);

   SCIPdebugMessage("Init method of Vanderbecks generic branching\n");

   SCIP_CALL( GCGrelaxIncludeBranchrule(origscip, branchrule, branchActiveMasterGeneric,
         branchDeactiveMasterGeneric, branchPropMasterGeneric, NULL, branchDataDeleteGeneric) );

   return SCIP_OKAY;
}

/** creates the most infeasible LP branching rule and includes it in SCIP */
SCIP_RETCODE SCIPincludeBranchruleGeneric(
   SCIP*                 scip                /**< SCIP data structure */
   )
{
   SCIP_BRANCHRULEDATA* branchruledata;

   /* create branching rule data */
   branchruledata = NULL;

   SCIPdebugMessage("Include method of Vanderbecks generic branching\n");

   /* include branching rule */
   SCIP_CALL( SCIPincludeBranchrule(scip, BRANCHRULE_NAME, BRANCHRULE_DESC, BRANCHRULE_PRIORITY,
         BRANCHRULE_MAXDEPTH, BRANCHRULE_MAXBOUNDDIST, branchCopyGeneric,
         branchFreeGeneric, branchInitGeneric, branchExitGeneric, branchInitsolGeneric,
         branchExitsolGeneric, branchExeclpGeneric, branchExecextGeneric, branchExecpsGeneric,
         branchruledata) );

   /* include event handler for adding generated mastervars to the branching constraints */
   SCIP_CALL( SCIPincludeEventhdlr(scip, EVENTHDLR_NAME, EVENTHDLR_DESC,
         NULL, NULL, eventInitGenericbranchvaradd, eventExitGenericbranchvaradd,
         NULL, NULL, NULL, eventExecGenericbranchvaradd,
         NULL) );

   return SCIP_OKAY;
}

/** initializes branchdata */
SCIP_RETCODE GCGbranchGenericCreateBranchdata(
   SCIP*                 scip,               /**< SCIP data structure */
   GCG_BRANCHDATA**      branchdata          /**< branching data to initialize */
   )
{
   assert(scip != NULL);
   assert(branchdata != NULL);

   SCIP_CALL( SCIPallocMemory(scip, branchdata) );
   (*branchdata)->consS = NULL;
   (*branchdata)->consSsize = 0;
   (*branchdata)->sequencesizes = 0;
   (*branchdata)->C = NULL;
   (*branchdata)->mastercons = NULL;
   (*branchdata)->consblocknr = -2;

   return SCIP_OKAY;
}

GCG_COMPSEQUENCE* GCGbranchGenericBranchdataGetConsS(
   GCG_BRANCHDATA*      branchdata          /**< branching data to initialize */
   )
{
   assert(branchdata != NULL);
   return branchdata->consS;
}

int GCGbranchGenericBranchdataGetConsSsize(
   GCG_BRANCHDATA*      branchdata          /**< branching data to initialize */
   )
{
   assert(branchdata != NULL);
   return branchdata->consSsize;
}

int GCGbranchGenericBranchdataGetConsblocknr(
   GCG_BRANCHDATA*      branchdata          /**< branching data to initialize */
   )
{
   assert(branchdata != NULL);
   return branchdata->consblocknr;
}

SCIP_CONS* GCGbranchGenericBranchdataGetMastercons(
   GCG_BRANCHDATA*      branchdata          /**< branching data to initialize */
   )
{
   assert(branchdata != NULL);
   return branchdata->mastercons;
}

/** returns true when the branch rule is the generic branchrule */
SCIP_Bool GCGisBranchruleGeneric(
   SCIP_BRANCHRULE*      branchrule          /**< branchrule to check */
)
{
   return (branchrule != NULL) && (strcmp(BRANCHRULE_NAME, SCIPbranchruleGetName(branchrule)) == 0);
}
