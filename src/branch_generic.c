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
//#define SCIP_DEBUG
/**@file   branch_generic.c
 * @ingroup BRANCHINGRULES
 * @brief  branching rule based on vanderbeck's generic branching scheme
 * @author Marcel Schmickerath
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#include <assert.h>
#include <string.h>

#include "branch_generic.h"
#include "relax_gcg.h"
#include "cons_origbranch.h"
#include "pricer_gcg.h"
#include "scip/cons_varbound.h"
#include "type_branchgcg.h"
#include "pub_gcgvar.h"

#include <stdio.h>
#include <stdlib.h>


#define BRANCHRULE_NAME          "generic"
#define BRANCHRULE_DESC          "generic branching rule by Vanderbeck"
#define BRANCHRULE_PRIORITY      100  //?
#define BRANCHRULE_MAXDEPTH      -1
#define BRANCHRULE_MAXBOUNDDIST  1.0

typedef int ComponentBoundSequence[3];  // [[comp], [sense], [bound]]    sense=1 means >=, sense=0 means <

struct GCG_BranchData
{
   ComponentBoundSequence**   C;             /**< S[k] bound sequence for block k */ //!!! sort of each C[i]=S[i] is important !!!
   int*               sequencsizes;                 /**< number of bounds in S[k] */
   int                Csize;
   ComponentBoundSequence*   S;             /**< component bound sequence which induce the current branching constraint */
   int                Ssize;
   ComponentBoundSequence*   childS;       /**< component bound sequence which induce the child nodes, need for prune by dominance */
   int                childSsize;
   int                blocknr;             /**< number of block branching was performed */
   int                childnr;
   int                lhs;
   SCIP_CONS*         mastercons;          /**< constraint enforcing the branching restriction in the master problem */
};

struct GCG_Strip
{
   SCIP_VAR*          mastervar;             /**< master variable */
   SCIP_Real          mastervarValue;
   int                blocknr;               /**< number of the block in which the strip belong */
   SCIP_Real*         generator;             /**< corresponding generator to the mastervar */
   SCIP_Bool*         compisinteger;         /**< ? is comp with integer origvars? */
   int                generatorsize;
   ComponentBoundSequence**   C;             /**< often NULL, only needed for ptrilocomp */
   int                Csize;
   int*               sequencesizes;
};

/*
//help structure only for ptrilocomp
struct GCG_GeneratorAndC
{
   SCIP_VAR*          mastervar;          
   int                blocknr;            
   SCIP_Real*         generator;          
   int                generatorsize;
   ComponentBoundSequence**   C;
};
*/



/** set of component bounds in separate */
struct GCG_Record
{
	ComponentBoundSequence**   record;             /**< returnvalue of separte function */
	   int                recordsize;
	   int*               sequencesizes;
};

/*
 * Callback methods for enforcing branching constraints
 */




/** callback propagation method */
static
GCG_DECL_BRANCHPROPMASTER(branchPropMasterRyanfoster)
{
   SCIP_VAR** vars;
   SCIP_Real val1;
   SCIP_Real val2;
   int nvars;
   int propcount;
   int i;
   int j;

   assert(scip != NULL);
   assert(branchdata != NULL);
   assert(branchdata->var1 != NULL);
   assert(branchdata->var2 != NULL);
   assert(branchdata->pricecons != NULL);

   assert(GCGpricerGetOrigprob(scip) != NULL);

   SCIPdebugMessage("branchPropMasterRyanfoster: %s(%s, %s)\n", ( branchdata->same ? "same" : "differ" ),
      SCIPvarGetName(branchdata->var1), SCIPvarGetName(branchdata->var2));

   *result = SCIP_DIDNOTFIND;

   propcount = 0;

   vars = SCIPgetVars(scip);
   nvars = SCIPgetNVars(scip);

   /* iterate over all master variables */
   for( i = 0; i < nvars; i++ )
   {
      int norigvars;
      SCIP_Real* origvals;
      SCIP_VAR** origvars;

      origvars = GCGmasterVarGetOrigvars(vars[i]);
      origvals = GCGmasterVarGetOrigvals(vars[i]);
      norigvars = GCGmasterVarGetNOrigvars(vars[i]);

      /* only look at variables not fixed to 0 */
      if( !SCIPisFeasZero(scip, SCIPvarGetUbLocal(vars[i])) )
      {
         assert(GCGvarIsMaster(vars[i]));

         /* if variable belongs to a different block than the branching restriction, we do not have to look at it */
         if( branchdata->blocknr != GCGvarGetBlock(vars[i]) )
            continue;

         /* save the values of the original variables for the current master variable */
         val1 = 0.0;
         val2 = 0.0;
         for( j = 0; j < norigvars; j++ )
         {
            if( origvars[j] == branchdata->var1 )
            {
               assert(SCIPisEQ(scip, origvals[j], 1.0));
               val1 = origvals[j];
               continue;
            }
            if( origvars[j] == branchdata->var2 )
            {
               assert(SCIPisEQ(scip, origvals[j], 1.0));
               val2 = origvals[j];
            }
         }

         /* if branching enforces that both original vars are either both contained or none of them is contained
          * and the current master variable has different values for both of them, fix the variable to 0 */
         if( branchdata->same && !SCIPisEQ(scip, val1, val2) )
         {
            SCIP_CALL( SCIPchgVarUb(scip, vars[i], 0.0) );
            propcount++;
         }
         /* if branching enforces that both original vars must be in different mastervars, fix all
          * master variables to 0 that contain both */
         if( !branchdata->same && SCIPisEQ(scip, val1, 1.0) && SCIPisEQ(scip, val2, 1.0) )
         {
            SCIP_CALL( SCIPchgVarUb(scip, vars[i], 0.0) );
            propcount++;
         }
      }
   }

   SCIPdebugMessage("Finished propagation of branching decision constraint: %s(%s, %s), %d vars fixed.\n",
      ( branchdata->same ? "same" : "differ" ), SCIPvarGetName(branchdata->var1), SCIPvarGetName(branchdata->var2), propcount);

   if( propcount > 0 )
   {
      *result = SCIP_REDUCEDDOM;
   }

   return SCIP_OKAY;
}

/*
 * Callback methods
 */


/** branching execution method for fractional LP solutions */
static
SCIP_DECL_BRANCHEXECLP(branchExeclpRyanfoster)
{  /*lint --e{715}*/
   SCIPdebugMessage("Execlp method of ryanfoster branching\n");

   *result = SCIP_DIDNOTRUN;

   return SCIP_OKAY;
}

/** branching execution method for relaxation solutions */
static
SCIP_DECL_BRANCHEXECEXT(branchExecextRyanfoster)
{  /*lint --e{715}*/
   SCIP* masterscip;

   SCIP_Bool feasible;
   SCIP_Bool contained;

   SCIP_VAR** branchcands;
   int nbranchcands;

   int v1;
   int v2;
   int o1;
   int o2;
   int j;

   SCIP_VAR* mvar1;
   SCIP_VAR* mvar2;
   SCIP_VAR* ovar1;
   SCIP_VAR* ovar2;

   SCIP_VAR** origvars1;
   SCIP_VAR** origvars2;
   int norigvars1;
   int norigvars2;

   assert(branchrule != NULL);
   assert(strcmp(SCIPbranchruleGetName(branchrule), BRANCHRULE_NAME) == 0);
   assert(scip != NULL);
   assert(result != NULL);

   SCIPdebugMessage("Execrel method of ryanfoster branching\n");

   *result = SCIP_DIDNOTRUN;

   /* do not perform Ryan & Foster branching if we have neither a set partitioning nor a set covering structure */
   if( !GCGrelaxIsMasterSetCovering(scip) || !GCGrelaxIsMasterSetPartitioning(scip) )
   {
      SCIPdebugMessage("Not executing Ryan&Foster branching, master is neither set covering nor set partitioning\n");
      return SCIP_OKAY;
   }

   /* check whether the current original solution is integral */
#ifdef SCIP_DEBUG
   SCIP_CALL( SCIPcheckSol(scip, GCGrelaxGetCurrentOrigSol(scip), TRUE, TRUE, TRUE, TRUE, &feasible) );
#else
   SCIP_CALL( SCIPcheckSol(scip, GCGrelaxGetCurrentOrigSol(scip), FALSE, TRUE, TRUE, TRUE, &feasible) );
#endif
   if( feasible )
   {
      SCIPdebugMessage("node cut off, since origsol was feasible, solval = %f\n",
         SCIPgetSolOrigObj(scip, GCGrelaxGetCurrentOrigSol(scip)));

      *result = SCIP_CUTOFF;

      return SCIP_OKAY;
   }

   /* the current original solution is not integral, now we have to branch;
    * first, get the master problem and all variables of the master problem
    */
   masterscip = GCGrelaxGetMasterprob(scip);
   SCIP_CALL( SCIPgetLPBranchCands(masterscip, &branchcands, NULL, NULL, &nbranchcands, NULL) );

   /* now search for two (fractional) columns mvar1, mvar2 in the master and 2 original variables ovar1, ovar2
    * s.t. mvar1 contains both ovar1 and ovar2 and mvar2 contains ovar1, but not ovar2
    */
   ovar1 = NULL;
   ovar2 = NULL;
   mvar1 = NULL;
   mvar2 = NULL;
   feasible = FALSE;

   /* select first fractional column (mvar1) */
   for( v1 = 0; v1 < nbranchcands && !feasible; v1++ )
   {
      mvar1 = branchcands[v1];
      assert(GCGvarIsMaster(mvar1));

      origvars1 = GCGmasterVarGetOrigvars(mvar1);
      norigvars1 = GCGmasterVarGetNOrigvars(mvar1);

      /* select first original variable ovar1, that should be contained in both master variables */
      for( o1 = 0; o1 < norigvars1 && !feasible; o1++ )
      {
         ovar1 = origvars1[o1];
         assert(!SCIPisZero(scip, GCGmasterVarGetOrigvals(mvar1)[o1]));

         /* mvar1 contains ovar1, look for mvar2 which constains ovar1, too */
         for( v2 = v1+1; v2 < nbranchcands && !feasible; v2++ )
         {
            mvar2 = branchcands[v2];
            assert(GCGvarIsMaster(mvar2));

            origvars2 = GCGmasterVarGetOrigvars(mvar2);
            norigvars2 = GCGmasterVarGetNOrigvars(mvar2);

            /* check whether ovar1 is contained in mvar2, too */
            contained = FALSE;
            for( j = 0; j < norigvars2; j++ )
            {
               assert(!SCIPisZero(scip, GCGmasterVarGetOrigvals(mvar2)[j]));
               if( origvars2[j] == ovar1 )
               {
                  contained = TRUE;
                  break;
               }
            }

            /* mvar2 does not contain ovar1, so look for another mvar2 */
            if( !contained )
               continue;

            /* mvar2 also contains ovar1, now look for ovar2 contained in mvar1, but not in mvar2 */
            for( o2 = 0; o2 < norigvars1; o2++ )
            {
               assert(!SCIPisZero(scip, GCGmasterVarGetOrigvals(mvar1)[o2]));

               ovar2 = origvars1[o2];
               if( ovar2 == ovar1 )
                  continue;

               /* check whether ovar2 is contained in mvar2, too */
               contained = FALSE;
               for( j = 0; j < norigvars2; j++ )
               {
                  assert(!SCIPisZero(scip, GCGmasterVarGetOrigvals(mvar2)[j]));

                  if( origvars2[j] == ovar2 )
                  {
                     contained = TRUE;
                     break;
                  }
               }

               /* ovar2 should be contained in mvar1 but not in mvar2, so look for another ovar2,
                * if the current one is contained in mvar2
                */
               if( contained )
                  continue;

               /* if we arrive here, ovar2 is contained in mvar1 but not in mvar2, so everything is fine */
               feasible = TRUE;
               break;
            }

            /* we did not find an ovar2 contained in mvar1, but not in mvar2,
             * now look for one contained in mvar2, but not in mvar1
             */
            if( !feasible )
            {
               for( o2 = 0; o2 < norigvars2; o2++ )
               {
                  assert(!SCIPisZero(scip, GCGmasterVarGetOrigvals(mvar2)[o2]));

                  ovar2 = origvars2[o2];

                  if( ovar2 == ovar1 )
                     continue;

                  contained = FALSE;
                  for( j = 0; j < norigvars1; j++ )
                  {
                     assert(!SCIPisZero(scip, GCGmasterVarGetOrigvals(mvar1)[j]));
                     if( origvars1[j] == ovar2 )
                     {
                        contained = TRUE;
                        break;
                     }
                  }

                  /* ovar2 should be contained in mvar2 but not in mvar1, so look for another ovar2,
                   * if the current one is contained in mvar1
                   */
                  if( contained )
                     continue;

                  /* if we arrive here, ovar2 is contained in mvar2 but not in mvar1, so everything is fine */
                  feasible = TRUE;
                  break;
               }
            }
         }
      }
   }

   if( !feasible )
   {
      SCIPdebugMessage("Ryanfoster branching rule could not find variables to branch on!\n");
      return SCIP_OKAY;
   }

   /* create the two child nodes in the branch-and-bound tree */
   SCIP_CALL( createChildNodesRyanfoster(scip, branchrule, ovar1, ovar2, GCGvarGetBlock(mvar1)) );

   *result = SCIP_BRANCHED;

   return SCIP_OKAY;
}

/** branching execution method for not completely fixed pseudo solutions */
static
SCIP_DECL_BRANCHEXECPS(branchExecpsRyanfoster)
{  /*lint --e{715}*/
   SCIP_CONS** origbranchconss;
   GCG_BRANCHDATA* branchdata;
   SCIP_VAR** branchcands;
   SCIP_VAR* ovar1;
   SCIP_VAR* ovar2;
   SCIP_Bool feasible;
   int norigbranchconss;
   int nbranchcands;
   int o1;
   int o2;
   int c;

   assert(branchrule != NULL);
   assert(strcmp(SCIPbranchruleGetName(branchrule), BRANCHRULE_NAME) == 0);
   assert(scip != NULL);
   assert(result != NULL);

   SCIPdebugMessage("Execps method of ryanfoster branching\n");

   *result = SCIP_DIDNOTRUN;

   /* do not perform Ryan & Foster branching if we have neither a set partitioning nor a set covering structure */
   if( !GCGrelaxIsMasterSetCovering(scip) || !GCGrelaxIsMasterSetPartitioning(scip) )
   {
      SCIPdebugMessage("Not executing Ryanfoster branching, master is neither set covering nor set partitioning\n");
      return SCIP_OKAY;
   }

   /* get unfixed variables and stack of active origbranchconss */
   SCIP_CALL( SCIPgetPseudoBranchCands(scip, &branchcands, NULL, &nbranchcands) );
   GCGconsOrigbranchGetStack(scip, &origbranchconss, &norigbranchconss);

   ovar1 = NULL;
   ovar2 = NULL;
   feasible = FALSE;

   /* select first original variable ovar1 */
   for( o1 = 0; o1 < nbranchcands && !feasible; ++o1 )
   {
      ovar1 = branchcands[o1];

      /* select second original variable o2 */
      for( o2 = o1 + 1; o2 < nbranchcands; ++o2 )
      {
         ovar2 = branchcands[o2];

         assert(ovar2 != ovar1);

         /* check whether we already branched on this combination of variables */
         for( c = 0; c < norigbranchconss; ++c )
         {
            if( GCGconsOrigbranchGetBranchrule(origbranchconss[c]) == branchrule )
               continue;

            branchdata = GCGconsOrigbranchGetBranchdata(origbranchconss[c]);

            if( (branchdata->var1 == ovar1 && branchdata->var2 == ovar2)
               || (branchdata->var1 == ovar2 && branchdata->var2 == ovar1) )
            {
               break;
            }
         }

         /* we did not break, so there is no active origbranch constraint with both variables */
         if( c == norigbranchconss )
         {
            feasible = TRUE;
            break;
         }
      }
   }

   if( !feasible )
   {
      SCIPdebugMessage("Ryanfoster branching rule could not find variables to branch on!\n");
      return SCIP_OKAY;
   }

   /* create the two child nodes in the branch-and-bound tree */
   SCIP_CALL( createChildNodesRyanfoster(scip, branchrule, ovar1, ovar2, GCGvarGetBlock(ovar1)) );

   *result = SCIP_BRANCHED;

   return SCIP_OKAY;
}

/** initialization method of branching rule (called after problem was transformed) */
static
SCIP_DECL_BRANCHINIT(branchInitGeneric)
{  
   assert(branchrule != NULL);

   SCIP_CALL( GCGrelaxIncludeBranchrule(scip, branchrule, branchActiveMasterGeneric, 
         branchDeactiveMasterGeneric, branchPropMasterGeneric, NULL, branchDataDeleteGeneric) );
   
   return SCIP_OKAY;
}



/* define not used callback as NULL*/
#define branchCopyGeneric NULL
#define branchFreeGeneric NULL
#define branchExitGeneric NULL
#define branchInitsolGeneric NULL
#define branchExitsolGeneric NULL


/*
 * branching specific interface methods
 */

/** method for calculating the median over all fractional components values if its the minimum return ceil(arithm middle)*/
static
SCIP_Real GetMedian(SCIP* scip, SCIP_Real* array, int arraysize, int min)
{  
	SCIP_Real Median;
	SCIP_Real swap;
	int l;
	int r;
	int i;
	int j;
	int MedianIndex;
	double arithmMiddle;
	
	r = arraysize -1;
	l = 0;
	arithmMiddle = 0;
	
	if( arraysize & 1)
		MedianIndex = arraysize/2;
	else
		MedianIndex = arraysize/2 -1;
	
	while(l < r-1)
	{
		Median = array[ MedianIndex ];
		i = l;
		j = r;
		do
		{
			while( array[i] < Median )
				++i;
			while( array[j] > Median )
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
	Median = array[ MedianIndex ];
	
	if( Median == min )
	{
		for(i=0; i<arraysize; ++i)
			arithmMiddle+=array[i];
		arithmMiddle /= arraysize;
		Median = SCIPceil(scip, aithmMiddle);
	}
	
   return Median;
}

// comparefunction for lexicographical sort
static
SCIP_DECL_SORTPTRCOMP(ptrcomp)
{
   struct GCG_Strip* strip1;
   struct GCG_Strip* strip2;
   int i;
   
   strip1 = (struct GCG_Strip*) elem1;
   strip2 = (struct GCG_Strip*) elem2;

   i = 0;
   
   for( i=0; i< strip1->generatorsize; ++i)
   {
	   if( strip1->generator[i] > strip2->generator[i] )
		   return -1;
	   if( strip1->generator[i] < strip2->generator[i] )
	   		   return 1;
   }
     
   return 0;
}

// lexicographical sort using scipsort
// !!! changes the array
static
SCIP_RETCODE LexicographicSort( GCG_Strip** array, int arraysize)
{
     
   SCIPsortPtr( (void**) array, ptrcomp, arraysize );
   
   //change array
   
   
   return SCIP_OKAY;
}


// compare function for ILO: returns 1 if bd1 < bd2 else -1 
static
int ILOcomp( GCG_Strip* strip1, GCG_Strip* strip2, ComponentBoundSequence** C, int NBoundsequences, int* sequencesizes, int p) // ComponentBoundSequence* S, int Ssize, int* IndexSet, int indexsetsize)
{
	int i;
	int isense;
	int ivalue;
	int j;
	int k;
	int l;
	int medianvalue;
	int newCsize;
	SCIP_Bool inall;
	SCIP_Bool inCj;
	//SCIP_Bool inI;
	SCIP_Bool returnvalue;
	//SCIP_Bool iinI;
	ComponentBoundSequence newcompbound;
	ComponentBoundSequence** copyC;
	//int* copyI;
	
	i = -1;
	j = 0;
	k = 0;
	l = -1;
	inall = FALSE;
	inCj = FALSE;
	//inI = FALSE;
	//iinI=FALSE;
	newCsize = 0;
	
	
	
	//lexicographic Order ?
	if( C == NULL || NBoundsequences==1 )
		return *ptrcomp( strip1, strip2);// == -1);
	
	assert(C!=NULL);
	assert(NBoundsequences>0);
	//find i which is in all S in C on position p (not exactly like pseudocode ?
	while( sequencesizes[k] < p-1 )
	{
		++k;
		assert(k<NBoundsequences);
	}
	i = C[k][p-1][0];
	isense = C[k][p-1][1];
	ivalue = C[k][p-1][2];
	
  /*
	
	while(!inall)
	{
		++l;
		assert( l< indexsetsize);
		i = IndexSet[l];
	//	inI = FALSE;
		for(j=0; j<indexsetsize; ++j)
	//	{
		//	if(IndexSet[j] == i)
	//		{
		//		inI =TRUE;
				break;
		//	}
	//	}
	//	if(!inI)
		//	continue;
		
		inall = TRUE;
		for(j=0; j< NBoundsequences; ++j)
		{
			inCj = FALSE;
			for(k=0; k<sequencesizes[j]; ++k)
			{
				if(C[j][k][0] == i)
				{
					inCj = TRUE;
					medianvalue = C[j][k][2];
					break;
				}
			}
			if(!inCj)
			{
				inall = FALSE;
				break;
			}
		}
	}*/
   	
	assert(i>=0);
	assert(i<indexsetsize);
	
/*
	//duplicate?
	if(Ssize == 0)
		SCIP_CALL( SCIPallocBufferArray(scip, &S, 1) );
	else{
		//realloc S
		SCIP_CALL( SCIPallocBufferArray(scip, &copyS, Ssize) );
		
		for(j=0; j<Ssize; ++j)
			copyS[j]=S[j];
		
		SCIP_CALL( SCIPreallocBufferArray(scip, &S, Ssize+1) );
		
		for(j=0; j<Ssize; ++j)
			S[j]=copyS[j];
		
		SCIPfreeBufferArray(scip, &copyS);
	}
	++Ssize;
	*/
/*
	//realloc I if i is in the Indexset (identical i in recursion possible if not a BP)
	for(j=0;j<indexsetsize;++j)
		if(IndexSet[j]==i)
		{
			iinI=TRUE;
			break;
		}
	if(iinI){
		SCIP_CALL( SCIPallocBufferArray(scip, &copyI, indexsetsize) );

		for(j=0; j<indexsetsize; ++j)
			copyI[j]=IndexSet[j];

		SCIP_CALL( SCIPreallocBufferArray(scip, &IndexSet, indexsetsize-1) );

		k = 0;
		for(j=0; j<indexsetsize; ++j)
		{
			if(copyI[j] != i )
			{
				IndexSet[k]=copyI[j];
				++k;
			}
		}
		--indexsetsize;
		SCIPfreeBufferArray(scip, &copyI);
	}
	*/
	
	//calculate subset of C
	for(j=0; j< NBoundsequences; ++j)
	{
		if(C[j][p-1][0] == 1)
			++Nupper;
		else 
			++Nlower;
	}
	
//	if( strip1->generator[i]>=ivalue && strip2->generator[i]>=ivalue )
//		sameupper = TRUE;
	
	

   if( strip1->generator[i]>=ivalue && strip2->generator[i]>=ivalue )
   {
	   newcompbound[0] = i;
	   newcompbound[1] = 1;
	   newcompbound[2] = ivalue;
	   S[Ssize]=newbound;
	   k=0;
	   SCIP_CALL( SCIPallocBufferArray(scip, &copyC, Nupper) );
	   SCIP_CALL( SCIPreallocBufferArray(scip, &newsequencesizes, Nupper) );
	   for(j=0; j< NBoundsequences; ++j)
	   {
		   assert(C[j][p-1][0] == i);

		   if(C[j][p-1][1] == 1)
		   {
			   copyC[k]=C[j];
			   newsequencesizes[k]=sequencesizes[j];
			   ++k;
		   }
	   }

	   //SCIP_CALL( SCIPreallocBufferArray(scip, &C, Nupper) );
	   
	 //  for(j=0;j<Nupper;++j)
	//	   C[j]=copyC[j];
	   
	   

	   returnvalue = ILOcomp( strip1, strip2, copyC, Nupper, newsequencesizes, p+1); // S, Ssize, IndexSet, indexsetsize, p+1);
   
	   SCIPfreeBufferArray(scip, &newsequencesizes);
	   SCIPfreeBufferArray(scip, &copyC);
	   
	   return returnvalue;
   }
   
   
   if( strip1->generator[i]<ivalue && strip2->generator[i]<ivalue )
      {
	   newcompbound[0] = i;
	   newcompbound[1] = 0;
	   newcompbound[2] = ivalue;
	   S[Ssize]=newbound;
	   k=0;
	   SCIP_CALL( SCIPallocBufferArray(scip, &copyC, Nlower) );
	   SCIP_CALL( SCIPreallocBufferArray(scip, &newsequencesizes, Nlower) );
	   for(j=0; j< NBoundsequences; ++j)
	   {
		   assert(C[j][p-1][0] == i);

		   if(C[j][p-1][1] == 0)
		   {
			   copyC[k]=C[j];
			   newsequencesizes[k]=sequencesizes[j];
			   ++k;
		   }
	   }

	   	   //SCIP_CALL( SCIPreallocBufferArray(scip, &C, Nlower) );
	   	   
	   	  // for(j=0;j<Nlower;++j)
	   	//	   C[j]=copyC[j];
	   	   
	   	 

	   	   returnvalue = ILOcomp( strip1, strip2, copyC, Nlower, newsequencesizes, p+1);// S, Ssize, IndexSet, indexsetsize, p+1);
	      
	   	   SCIPfreeBufferArray(scip, &newsequencesizes);
	   	  SCIPfreeBufferArray(scip, &copyC);
	   	   
	   	   return returnvalue; 
      }
   if( strip1->generator[i] > strip2->generator[i])
	   return 1;
   else 
	   return -1;
}

// comparefunction for induced lexicographical sort
static
SCIP_DECL_SORTPTRCOMP(ptrilocomp)
{
   struct GCG_Strip* strip1;
   struct GCG_Strip* strip2;
   int returnvalue;
   
   strip1 = (struct GCG_Strip*) elem1;
   strip2 = (struct GCG_Strip*) elem2;

   //ComponentBoundSequence** C;
   
   //&C=&(strip1->C);
   
   returnvalue=ILOcomp(strip1, strip2, strip1->C, strip1->Csize, strip1->sequencesizes, 1); //NULL, 0, strip1->IndexSet, strip1->generatorsize, 1);

   return returnvalue;
}

/*
// induced lexicographical sort based on QuickSort
static
SCIP_RETCODE ILOQSort( SCIP* scip, GCG_BranchData** array, int arraysize, ComponentBoundSequence** C, int sequencesize, int l, int r )
{
	int i;
	int j;
	int k;
	GCG_Branchdata* pivot;
	GCG_Branchdata* swap;
	int* IndexSet;
	indexsetsize;
	
	i = l;
	j = r;
	pivot = array[(l+r)/2];
	indexsetsize = pivot->generatorsize;
	k = 0;
	
	SCIP_CALL( SCIPallocBufferArray(scip, &IndexSet, indexsetsize) );
	for( k = 0; k < indexsetsize; ++k )
		IndexSet[k] = k; // ! n-1 here, instead of n
	
	do
	{
		while( ILOcomp( array[i], pivot, C, sequencesize, NULL, IndexSet, indexsetsize, 1))
			++i;
		while( ILOcomp( pivot, array[j], C, sequencesize, NULL, IndexSet, indexsetsize, 1))
					--j;
		if( i <= j )
		{
			swap = array[i];
			array[i] = array[j];
			array[j] = swap;
			++i;
			--j;
		}
	}while( i <= j );
	if( l < j )
		ILOQSort( scip, array, arraysize, C, sequencesize, l, j );
	if( i < r )
		ILOQSort( scip, array, arraysize, C, sequencesize, i, r );
	
	
	SCIPfreeBufferArray(scip, &IndexSet);
   
   return SCIP_OKAY;
}
*/

// induced lexicographical sort
static
SCIP_RETCODE InducedLexicographicSort( SCIP* scip, GCG_Strip** array, int arraysize, ComponentBoundSequence** C, int NBoundsequences, int* sequencesizes )
{
	int i;
	int n;
	int* IS;
	
	if( sequencesize == 0 )
		return LexicographicSort( array, arraysize );
	assert( C!= NULL );
	
   //ILOQSort( scip, array, arraysize, C, sequencesize, 0, arraysize-1 );
   
	//set data in the strips for the ptrcomp
	n=array[0]->generatorsize;
	/*
	SCIP_CALL( SCIPallocBufferArray(scip, &IS, n) );
	for( i=0; i<n; ++i )
		IS[i]=i;
	*/
	
	for( i=0; i<arraysize; ++i ){
		&(array[i]->strip->C) = &C;
		&(array[i]->strip->Csize) = &NBoundsequences; 
		&(array[i]->strip->sequencesizes) = &sequencesizes;
		//array[i]->strip->IndexSet = IS;
		//array[i]->strip->Indexsetsize = n;
	}
	
	SCIPsortPtr((void**) array, ptrilocomp, arraysize);
	
//	SCIPfreeBufferArray(scip, &IS);
	
   return SCIP_OKAY;
}


// separation at the root node
static
struct GCG_Record* Separate( SCIP* scip, GCG_Strip** F, int Fsize, int* IndexSet, int IndexSetSize, ComponentBoundSequence* S, int Ssize, struct GCG_Record* record )
{
	int i;
	int j;
	int n;
	int k;
	int l;
	int Jsize;
	int* J;
	int median;
	int min;
	int Fupper;
	int Flower;
	//int copySsize;
	GCG_Strip** copyF;
	ComponentBoundSequence* upperLowerS;
	//ComponentBoundSequence* lowerS;
	SCIP_Real* alpha;
	SCIP_Real* compvalues;
	SCIP_Real  muF;
	SCIP_Real maxPriority;
	SCIP_Bool found;
	
	i = 0;
	j = 0;
	k = 0;
	l = 0;
	Jsize = 0;
	Fupper = 0;
	Flower = 0;
	muF = 0;
	//copySsize = 0;
	min = INT_MAX;
	maxPriority = INT_MIN;
	found = FALSE;
	
	if(Fsize == 0 || IndexSetSize == 0)
		return record;
	
	assert( F != NULL ); 
	assert( IndexSetSize != NULL );

	for(j=0; j<Fsize; ++j)
		muF += F[j]->mastervarValue;

	SCIP_CALL( SCIPallocBufferArray(scip, &alpha, IndexSetSize) );
	
	for(k=0; k<IndexSetSize; ++k)
	{
		ComponentBoundSequence* copyS;
		i = IndexSet[k]; 
		alpha[k] = 0;
		for(j=0; j<Fsize; ++j)
			alpha[k] += F[j]->generator[i] * F[j]->mastervarValue;
		if( SCIPisGT(scip, alpha[k], 0) && SCIPisLT(scip, alpha[k], muF) )
			++Jsize;
		if( SCIPisGT(scip, alpha[k] - SCIPfloor(scip, alpha[k]), 0) )
		{
			found = TRUE;
			
			//add to record
			++Ssize;
			SCIP_CALL( SCIPallocBufferArray(scip, &copyS, Ssize) );
			for(l=0; l < Ssize-1; ++l)
				copyS[l]=S[l];
			
			
			copyS[Ssize-1][0] = i;
			copyS[Ssize-1][1] = 1;
			
			SCIP_CALL( SCIPallocBufferArray(scip, &compvalues, Fsize) );
			for(l=0; l<Fsize; ++l)
			{
				compvalues[l] = F[l]->generator[i];
				if( SCIPisLT(scip, compvalues[l], min) )
					min = compvalues[l];
			}
			median = GetMedian(scip, compvalues, Fsize, min);
			copyS[Ssize-1][2] = median;
			
			SCIPfreeBufferArray(scip, &compvalues);
			
			record->recordsize++;
			SCIP_CALL( SCIPreallocBufferArray(scip, &(record->record), record->recordsize) );
			SCIP_CALL( SCIPreallocBufferArray(scip, &(record->sequencesizes), record->recordsize) );
			record->record[record->recordsize-1] = copyS;
			record->sequencesizes[record->recordsize-1] = Ssize;
			--Ssize;
		}
	}
	
	if(found)
	{
		SCIPfreeBufferArray(scip, &alpha);
		//SCIPfreeBufferArray(scip, &copyS);
		return record;
	}
	
	//discriminating components
	SCIP_CALL( SCIPallocBufferArray(scip, &J, Jsize) );
	j=0;
	for(k=0; k<IndexSetSize; ++k)
		{
			if( SCIPisGT(scip, alpha[k], 0) && SCIPisLT(scip, alpha[k], muF) )
			{
				J[j] = IndexSet[k];
				++j;
			}
		}
	
	//Partition
	min=INT_MAX;
	do{
	for(j=0; j<Jsize; ++j)
	{
		if( getPriority(J[j]) > maxPriority )
		    i = J[j]; 
	}
	
	
	SCIP_CALL( SCIPallocBufferArray(scip, &compvalues, Fsize) );
	for(l=0; l<Fsize; ++l)
	{
		compvalues[l] = F[l]->generator[i];
		if( SCIPisLT(scip, compvalues[l], min) )
			min = compvalues[l];
	}
	median = GetMedian(scip, compvalues, Fsize, min);
	SCIPfreeBufferArray(scip, &compvalues);
	
	if( SCIPisEQ(scip, median, min) )
	{
		for(j=0; j<Jsize; ++j)
			{
				if( i == J[j])
				{
					J[j]=J[Jsize-1];
					break;
				}
			}
		--Jsize;
		
	}
	
	assert(Jsize>=0);
	}while( SCIPisEQ(scip, median, min) );
	
	
	++Ssize;
	SCIP_CALL( SCIPallocBufferArray(scip, &upperLowerS, Ssize) );
						for(l=0; l < Ssize-1; ++l)
							upperLowerS[l]=S[l];
						
						upperLowerS[Ssize-1][0] = i;
						upperLowerS[Ssize-1][1] = 1;
						upperLowerS[Ssize-1][2] = median;

	for(k=0; k<Fsize; ++k)
	{
		if( SCIPisGE(scip, F[k]->generator[i], median) )
			++Fupper;
		else 
			--Flower;
	}
	
    //choose smallest partition
	
	if( Flower < Fupper )
	{
		SCIP_CALL( SCIPallocBufferArray(scip, &copyF, Flower) );
		j = 0;
		for(k=0; k<Fsize; ++k)
			{
				if( SCIPisLT(scip, F[k]->generator[i], median) )
					copyF[j]=F[k];
			}
		Fsize = Flower;
	}
	else
	{
		upperLowerS[Ssize-1][1] = 0;
		SCIP_CALL( SCIPallocBufferArray(scip, &copyF, Fupper) );
		j = 0;
		for(k=0; k<Fsize; ++k)
		{
			if( SCIPisGE(scip, F[k]->generator[i], median) )
				copyF[j]=F[k];
		}
		Fsize = Fupper;
	}
	
	record = Separate( scip, copyF, Fsize, J, Jsize, upperLowerS, Ssize, record );
	
	SCIPfreeBufferArray(scip, &J);
	SCIPfreeBufferArray(scip, &copyF);
	SCIPfreeBufferArray(scip, &upperLowerS);
	SCIPfreeBufferArray(scip, &alpha);
	
	return record;	
}

// choose a component bound sequence 
static
SCIP_RETCODE ChoseS( SCIP* scip, struct GCG_Record* record, ComponentBoundSequence* S, int* Ssize )
{
	int minSizeOfMaxPriority;  //neede if the last comp priority is euqal to the one in other bound sequences
	int maxPriority;
	int i;
	int Index;
	
	minSizeOfMaxPriority = INT_MAX;
	maxPriority = INT_MIN;
	i = 0;
	Index = -1;
	
	for( i=0; i< record->recordsize; ++i )
	{
		assert(record->sequencesizes > 0);
		if(maxPriority <= getPriority( record->record[i][record->sequencesizes[i] -1 ][0] ) )
		{
			if( maxPriority < getPriority( record->record[i][record->sequencesizes[i] -1 ][0] ) )
			{
				maxPriority = getPriority( record->record[i][record->sequencesizes[i] -1 ][0] );
				minSizeOfMaxPriority = record->sequencesizes[i];
				Index = i;
			}
			else
				if( record->sequencesizes[i] < minSize )
				{
					minSizeOfMaxPriority = record->sequencesizes[i];
					Index = i;
				}
		}
	}
	assert(maxPriority!=INT_MIN);
	assert(minSizeOfMaxPriority!=INT_MAX);
	assert(Index>=0);
	
	sSize = minSizeOfMaxPriority;
	SCIP_CALL( SCIPallocBufferArray(scip, &S, sSize) );
	for(i=0; i< sSize;++i)
		S[i]=record->record[Index][i];
	//&S = &(record->record[i]);
	
	//free record
	for( i=0; i< record->recordsize; ++i )
	{
		SCIPfreeBufferArray(scip, &(record->record[i]) );
	}
	SCIPfreeBufferArray(scip, &(record->record) );
	
	return SCIP_OKAY;	
}



// separation at a node other than the root node
static
struct GCG_Record* Explore( SCIP* scip, ComponentBoundSequence** C, int Csize, int* sequencesizes, int p, GCG_Strip** F, int Fsize, int* IndexSet, int IndexSetSize, ComponentBoundSequence* S, int Ssize, struct GCG_Record* record )
{
	int i;
	int j;
	int n;
	int k;
	int l;
	int* newsequencesizes;
	int isense;
	int ivalue;
	int median;
	int min;
	int Fupper;
	int Flower;
	int Cupper;
	int Clower;
	int copyCsize;
	//int copySsize;
	GCG_Strip** copyF;
	ComponentBoundSequence** copyC;
	//ComponentBoundSequence* lowerS;
	SCIP_Real alpha_i;
	SCIP_Real* compvalues;
	SCIP_Real  muF;
	SCIP_Bool found;
	
	i = 0;
	j = 0;
	k = 0;
	l = 0;
	Jsize = 0;
	Fupper = 0;
	Flower = 0;
	Cupper = 0;
	Clower = 0;
	copyCsize = 0;
	muF = 0;
	//copySsize = 0;
	min = INT_MAX;
	maxPriority = INT_MIN;
	found = FALSE;

	//call separate?
	if( C == NULL || Fsize==0 || IndexSetSize==0 ) //   || NBoundsequences==1
		return Separate( scip, F, Fsize, IndexSet, IndexSetSize, S, Ssize, record );

	assert( C!=NULL );
	assert( Csize>0 );
	assert( F != NULL ); 
	assert( IndexSetSize != NULL );

	//find i which is in all S in C on position p (not exactly like pseudocode ?
	while( sequencesizes[k] < p-1 )
	{
		++k;
		assert( k<Csize );
	}
	i = C[k][p-1][0];
	isense = C[k][p-1][1];
	ivalue = C[k][p-1][2];


	for(j=0; j<Fsize; ++j)
		muF += F[j]->mastervarValue;

	SCIP_CALL( SCIPallocBufferArray(scip, &alpha, IndexSetSize) );
	
	
	alpha_i = 0;
	for(j=0; j<Fsize; ++j)
		if(F[j]->generator[i] >= ivalue)
			alpha_i += F[j]->generator[i] * F[j]->mastervarValue;

		if( SCIPisGT(scip, alpha_i - SCIPfloor(scip, alpha_i), 0) )
		{
			found = TRUE;
			
			//add to record
			++Ssize;
			SCIP_CALL( SCIPallocBufferArray(scip, &copyS, Ssize) );
			for(l=0; l < Ssize-1; ++l)
				copyS[l]=S[l];
			
			
			copyS[Ssize-1][0] = i;
			copyS[Ssize-1][1] = 1;
			copyS[Ssize-1][2] = ivalue;
			
			
			record->recordsize++;
			SCIP_CALL( SCIPreallocBufferArray(scip, &(record->record), record->recordsize) );
			SCIP_CALL( SCIPreallocBufferArray(scip, &(record->sequencesizes), record->recordsize) );
			record->record[record->recordsize-1] = copyS;
			record->sequencesizes[record->recordsize-1] = Ssize;
			--Ssize;
		}
	
	
	if(found)
	{
		SCIPfreeBufferArray(scip, &alpha);
		//SCIPfreeBufferArray(scip, &copyS);
		return record;
	}
	

	
	//Partition
	min = INT_MAX;
	do{	
	
	SCIP_CALL( SCIPallocBufferArray(scip, &compvalues, Fsize) );
	for(l=0; l<Fsize; ++l)
	{
		compvalues[l] = F[l]->generator[i];
		if( SCIPisLT(scip, compvalues[l], min) )
			min = compvalues[l];
	}
	median = GetMedian(scip, compvalues, Fsize, min);
	SCIPfreeBufferArray(scip, &compvalues);
	
	if( SCIPisEQ(scip, median, min) )
	{
		for(j=0; j<IndexSetSize; ++j)
			{
				if( i == IndexSet[j])
				{
					IndexSet[j]=IndexSet[IndexSetSize-1];
					break;
				}
			}
		--IndexSetSize;
		
	}
	
	assert(IndexSetSize>=0);
	}while( SCIPisEQ(scip, median, min) );
	
	
	++Ssize;
	SCIP_CALL( SCIPreallocBufferArray(scip, &S, Ssize) );
					//	for(l=0; l < Ssize-1; ++l)
				//			upperLowerS[l]=S[l];
						
						S[Ssize-1][0] = i;
						S[Ssize-1][1] = 1;
						S[Ssize-1][2] = median;

	for(k=0; k<Fsize; ++k)
	{
		if( SCIPisGE(scip, F[k]->generator[i], median) )
			++Fupper;
		else 
			--Flower;
	}
	
	//calculate subset of C
		for(j=0; j< Csize; ++j)
		{
			if(C[j][p-1][0] == 1)
				++Cupper;
			else 
				++Clower;
		}
	
	if( SCIPisLE(scip, alpha_i, 0) )
		Cupper = INT_MAX;
	if( SCIPisEQ(scip, alpha_i, muF) )
		Clower = INT_MAX;
		
    //choose smallest partition
	
	if( Fupper <= Flower )
	{
		SCIP_CALL( SCIPallocBufferArray(scip, &copyF, Flower) );
		j = 0;
		for(k=0; k<Fsize; ++k)
		{
			if( SCIPisGE(scip, F[k]->generator[i], median) )
				copyF[j]=F[k];
		}
		Fsize = Fupper;

		//new C
		k=0;
		SCIP_CALL( SCIPallocBufferArray(scip, &copyC, Cupper) );
		SCIP_CALL( SCIPallocBufferArray(scip, &newsequencesizes, Cupper) );
		for(j=0; j< Csize; ++j)
		{
			assert(C[j][p-1][0] == i);

			if(C[j][p-1][1] == 1)
			{
				copyC[k]=C[j];
				newsequencesizes[k]=sequencesizes[j];
				++k;
			}
		}
		Csize = Cupper;

	}
	else
	{
		upperLowerS[Ssize-1][1] = 0;
		SCIP_CALL( SCIPallocBufferArray(scip, &copyF, Fupper) );
		j = 0;
		for(k=0; k<Fsize; ++k)
		{
			if( SCIPisLT(scip, F[k]->generator[i], median) )
				copyF[j]=F[k];
		}
		Fsize = Flower;

		//new C
		k=0;
		SCIP_CALL( SCIPallocBufferArray(scip, &copyC, Clower) );
		SCIP_CALL( SCIPallocBufferArray(scip, &newsequencesizes, Clower) );
		for(j=0; j< Csize; ++j)
		{
			assert(C[j][p-1][0] == i);

			if(C[j][p-1][1] == 0)
			{
				copyC[k]=C[j];
				newsequencesizes[k]=sequencesizes[j];
				++k;
			}
		}
		Csize = Clower;
		S[Ssize-1][1] = 0;
	}

	record = Separate( scip, CopyC, Csize, newsequencesizes, p+1, copyF, Fsize, IndexSet, IndexSetSize, S, Ssize, record );
	
	
	SCIPfreeBufferArray(scip, &copyF);
	SCIPfreeBufferArray(scip, &copyC);
	SCIPfreeBufferArray(scip, &newsequencesizes);
	
	return record;	
}


// callup method for seperate 
static
SCIP_RETCODE CallSeparate( SCIP* scip, GCG_Strip** F, int Fsize, ComponentBoundSequence* S, int* Ssize, ComponentBoundSequence** C, int Csize, int* CompSizes )
{
	int i;
	int n;
	int* IndexSet;
	int IndexSetSize;
    struct GCG_Record* record;
	
	assert(Fsize > 0);
	assert(F!=NULL);
	
	record = (struct GCG_Record*) malloc(sizeof(struct GCG_Record));
	
	//calculate IndexSet
	IndexSetSize = F[0]->generatorsize;
	SCIP_CALL( SCIPallocBufferArray(scip, &IndexSet, IndexSetSize) );
	for( i=0; i<IndexSetSize; ++i )
		//if( !continousVar(i))
			IndexSet[i]=i;

	//rootnode?
	if( Csize<=0 )
		Separate( scip, F, Fsize, IndexSet, IndexSetSize, NULL, 0, record );
	else
	{
		assert( C!=NULL );
		Explore( scip, C, Csize, CompSizes, 1, F, Fsize, IndexSet, IndexSetSize, NULL, 0, record);
	}	
	
	
	ChoseS( scip, record, S, &Ssize );
	
	SCIPfreeBufferArray(scip, &IndexSet);
	
	return SCIP_OKAY;	
}


/** callback deletion method for branching data*/
static
GCG_DECL_BRANCHDATADELETE(branchDataDeleteGeneric)
{
   assert(scip != NULL);
   assert(branchdata != NULL);

   SCIPdebugMessage("branchDataDeleteGeneric: child blocknr %d, %s\n", *branchdata)->blocknr,
      (*branchdata)->cons);

   /* release constraint that enforces the branching decision */
   if( (*branchdata)->cons != NULL )
   {
      SCIP_CALL( SCIPreleaseCons(GCGrelaxGetMasterprob(scip), &(*branchdata)->cons) );
   }

   SCIPfreeBufferArray(scip, &((*branchdata)->S));
   if( (*branchddata)->childS !=NULL )
	   SCIPfreeBufferArray(scip, &((*branchdata)->childS));
   
   SCIPfreeMemory(scip, branchdata);
   *branchdata = NULL;

   return SCIP_OKAY;
}


/** for given component bound sequence S, create |S|+1 Vanderbeck branching nodes */
static
SCIP_RETCODE createChildNodesGeneric(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_BRANCHRULE*      branchrule,         /**< branching rule */
   ComponentBoundSequence* S,              /**< Component Bound Sequence defining the nodes */
   int                   Ssize,
   ComponentBoundSequence** C,              /**< previous Component Bound Sequences */
   int                   Csize,
   int*                  sequencesizes,
   int                   blocknr,             /**< number of the block */
   GCG_Strip**           F,                   /**< strips with mu>0 */  //for rhs, will be small than
   int                   Fsize
   )
{
	//SCIP_NODE* childsame;
	//SCIP_NODE* childdiffer;
	//SCIP_CONS* origbranchsame;
	//SCIP_CONS* origbranchdiffer;
	//GCG_BRANCHDATA* branchsamedata;
	//GCG_BRANCHDATA* branchdifferdata;
	//char samename[SCIP_MAXSTRLEN];
	//char differname[SCIP_MAXSTRLEN];

	SCIP*  masterscip;
	int norigvars;
	int v;
	int i;
	int p;
	int k;
	int pL;
	int L;
	int lhs;
	int nmastervars;
	int newnmastervars;
	int newFsize;
	SCIP_Real mu;  // mu(S)
	SCIP_VAR** mastervars;
	SCIP_VAR** copymastervars;
	SCIP_VAR* mvar;
	SCIP_VARDATA* vardata;
	SCIP_Bool nodeRedundant;


	lhs = 0;
	p = 0;
	k = 0;
	i = 0;
	L = 0;
	pL = nblocks;  // Npricingprobs??? ändert sich bei vanderbeckpricing??
	mu = 0;
	nodeRedundant = FALSE;


	// get variable data of the master problem 
	SCIP_CALL( SCIPgetVarsData(scip, &mastervars, &nmastervars, NULL, NULL, NULL, NULL) );
	assert(nmastervars >= 0); 
	SCIP_CALL( SCIPallocBufferArray(scip, &copymastervars, nmastervars) );

	for(i=0, i<nmastervars, ++i)
		copymastervars[i] = mastervars[i];

	assert(scip != NULL);
	assert(branchrule != NULL);
	assert(Ssize != 0);
	assert(S != NULL);

	masterscip = GCGrelaxGetMasterprob(scip);
	SCIP_CALL( SCIPgetVarsData(masterscip, &mastervars, &nmastervars, NULL, NULL, NULL, NULL) );
	SCIP_CALL( SCIPgetLPBranchCands(masterscip, &branchcands, &branchcandssol, &branchcandsfrac, &nbranchcands, NULL) );


	SCIPdebugMessage("Vanderbeck branching rule: creating %d nodes\n", Ssize+1);


	for( p=0; p<Ssize+1; ++p )
	{
		SCIP_NODE* child;
		SCIP_CONS* branchcons;
		SCIP_CONS* mastercons;
		GCG_BRANCHDATA* branchchilddata;
		char childname[SCIP_MAXSTRLEN];

		mu = 0;
		
		/* allocate branchdata for same child and store information */
		   SCIP_CALL( SCIPallocMemory(scip, &branchchilddata) );
		   branchchilddata->same = TRUE;
		   branchchilddata->blocknr = blocknr;
		   branchchilddata->cons = NULL;
		   branchchilddata->chidnr = p;
		   
		   if( p == Ssize )
		   {
			   SCIP_CALL( SCIPallocBufferArray(scip, &(branchchilddata->S), Ssize) );
			   branchchilddata->Ssize = Ssize;
		   }
		   else
		   {
			   SCIP_CALL( SCIPallocBufferArray(scip, &(branchchilddata->S), p+1) );
			   branchchilddata->Ssize = p+1;
		   }
		   for( k=0; k<=p; ++k)
		   {
			   ComponentBoundSequence compBound;
			   
			   if( k < p-1 )
			   {
				   compBound[0] = S[p][0];
				   compBound[1] = S[p][1];
				   compBound[2] = S[p][2];
			   }
			   else
			   {
				   compBound[0] = S[p][0];
				   compBound[2] = S[p][2];
				   if( S[p][2] == 1)
					   compBound[1] = 0;
				   else
					   compBound[1] = 1;
			   }
			    
			   if( k == Ssize )
			   {
				   assert( p == Ssize );
				   compBound[1] = S[p][1];
				   branchchilddata->S[k-1] = compBound;
			   }
			   else
				   branchchilddata->S[k] = compBound;
		   }

		//last node?
		if( p == Ssize )
		{
			//vardata = SCIPvarGetData(mvar);
			// assert(vardata->vartype == GCG_VARTYPE_MASTER);

			lhs = pL;
		}
		else
		{
			newFsize = Fsize;
			for( i=0; i<newFsize; ++i)
			{
				if(S[p][1] == 1 )
				{
					if(F[i]->generator[S[p][0]] >= S[p][2] )
						mu += F[i]->mastervarValue;
				}
				else  //nested erasing
				{
					if(newFsize > 0)
					{
						F[i] = F[newFsize-1];
						--newFsize;
						--i;
					}
				}
			}
			Fsize = newFsize;

			if( p == Ssize-1)
				L = SCIPceil(scip, mu);
			else
			{
				L = mu;
			}
			lhs = pL-L+1;
		}
		pL = L;
		
		branchchilddata->lhs = lhs;

		// define name for constraint 
		(void) SCIPsnprintf(childname, SCIP_MAXSTRLEN, "child(%d, %d)", p, lhs);

		//create cons
		SCIP_CALL( GCGcreateConsMasterbranch(scip, &branchcons, childname, child, GCGconsMasterbranchGetActiveCons(scip)), branchrule, branchchilddata );
		

		//addNode
		SCIP_CALL( SCIPaddConsNode(scip, child, branchcons, NULL) );

		//release constraint 
		SCIP_CALL( SCIPreleaseCons(scip, &branchcons) );

		// create constraint for child
		SCIP_CALL( SCIPcreateConsLinear(scip, &mastercons, childname, 0, NULL, NULL,
				lhs, SCIPinfinity(scip), TRUE, TRUE, TRUE, TRUE, TRUE, TRUE, TRUE, FALSE, FALSE, TRUE) );

		//Node redundant?


		//add variables to constraint
		if(!nodeRedundant)
		{

			newnmastervars = nmastervars;
			for( i=0; i<newnmastervars; ++i)
			{
				if( GCGvarGetBlock(copymastervars[i]) == blocknr )
				{
					if(p==Ssize)
					{
						assert( (copymastervars[i].generator[S[p][0]] >= S[p][2] && S[p][1]==1) || (copymastervars[i].generator[S[p][0]] < S[p][2] && S[p][1]!=1) );
						SCIP_CALL( SCIPaddCoefLinear(scip, mastercons, copymastervars[i], 1.0) );
						//small down array
						&copymastervars[i] = &copymastervars[newmastervars-1];
						--newnmastervars;
						--i;
					}
					else
					{
						if( S[p][1] == 1 )
						{
							// if( mastervar[i] >= S[p][2] ) current mastervars stays in array
							if( copymastervars[i].generator[S[p][0]] < S[p][2] )
							{
								//add var to constraint
								SCIP_CALL( SCIPaddCoefLinear(scip, mastercons, copymastervars[i], 1.0) );

								//small down array
								&copymastervars[i] = &copymastervars[newmastervars-1];
								--newnmastervars;
								--i;
							}
						}
						else
						{
							// if( mastervar[i] < S[p][2] ) current mastervars stays in array
							if( copymastervars[i].generator[S[p][0]] >= S[p][2] )
							{
								//add var to constraint
								SCIP_CALL( SCIPaddCoefLinear(scip, mastercons, copymastervars[i], 1.0) );

								//small down array
								&copymastervars[i] = &copymastervars[newmastervars-1];
								--newnmastervars;
								--i;
							}    	    				
						}
					}
				}
				else
				{
					//small down array
					&copymastervars[i] = &copymastervars[newmastervars-1];
					--newnmastervars;
					--i;
				}
			}
			nmastervars = newnmastervars;
		}

		//add cons locally to the problem and release it
		SCIP_CALL( SCIPaddConsNode(scip, child, mastercons, NULL) );
		SCIP_CALL( SCIPreleaseCons(scip, &mastercons) );
	}


	SCIPfreeBufferArray(scip, &copymastervars);
	
	return SCIP_OKAY; 
}


/** callback activation method */
static
GCG_DECL_BRANCHACTIVEMASTER(branchActiveMasterGeneric)
{
   //SCIP* origscip;
   SCIP* masterscip;
   SCIP_VAR** mastervars;
   SCIP_VAR** copymastervars;
   int nmastervars;
   int nnewmastervars;
   int i;
   int p;
   char name[SCIP_MAXSTRLEN];
   
   i = 0;
   p = 0;
   nmastervars = 0;

   assert(scip != NULL);
   assert(branchdata != NULL);
   assert(branchdata->S != NULL);

   masterscip = GCGrelaxGetMasterprob(scip);
   assert(masterscip != NULL);
   SCIP_CALL( SCIPgetVarsData(masterscip, &mastervars, &nmastervars, NULL, NULL, NULL, NULL) );
   
   SCIP_CALL( SCIPallocBufferArray(scip, &copymastervars, nmastervars) );
   
   for(i=0, i<nmastervars, ++i)
	   copymastervars[i] = mastervars[i];
   

   SCIPdebugMessage("branchActiveMasterGeneric: Block %d, Ssize %d)\n", branchdata->blocknr,
      branchdata->Ssize);

  /* create corresponding constraint in the master problem, if not yet created */
   if( branchdata->cons == NULL )
   {
      
         (void) SCIPsnprintf(name, SCIP_MAXSTRLEN, "child(%d, %d)", branchdata->childnr, lhs);

         // create constraint for child
   		SCIP_CALL( SCIPcreateConsLinear(masterscip, &(branchdata->cons), name, 0, NULL, NULL,
         				branchdata->lhs, SCIPinfinity(scip), TRUE, TRUE, TRUE, TRUE, TRUE, TRUE, TRUE, FALSE, FALSE, TRUE) );
         
   		
   		//add mastervars
   		for(p=0; p< branchdata->Ssize; ++p)
   		{
   			nnewmastervars = nmastervars;
   			for(i=0; i<nnewmastervars; ++i)
   			{
   				if( GCGvarGetBlock(copymastervars[i]) == branchdata->blocknr )
   				{
   					if( branchdata->S[p][1] == 1)
   					{
   						if(copymastervars[i].generator[branchdata->S[p][0]] >= branchdata->S[p][2])
   						{
   							if( p == branchdata->Ssize-1 )
   								//add var to constraint
   								SCIP_CALL( SCIPaddCoefLinear(masterscip, branchdata->cons, copymastervars[i], 1.0) );
   						}
   						else
   						{
   							//small down array
   							&copymastervars[i] = &mcopyastervars[newmastervars-1];
   							--i;
   							--nnewmastervars;
   						}
   					}
   					else
   					{
   						if(copymastervars[i].generator[branchdata->S[p][0]] < branchdata->S[p][2])
   						{
   							if( p == branchdata->Ssize-1 )
   								//add var to constraint
   								SCIP_CALL( SCIPaddCoefLinear(masterscip, branchdata->cons, copymastervars[i], 1.0) );
   						}
   						else
   						{
   							//small down array
   							&copymastervars[i] = &copymastervars[newmastervars-1];
   							--i;
   							--nnewmastervars;
   						}
   					}
   					
   				}
   				else
   				{
   					//small down array
   					&copymastervars[i] = &copymastervars[newmastervars-1];
   					--i;
   					--nnewmastervars;
   				}
   			}
   			nmastervars = nnewmastervars;
   		}
   }
   /* add constraint to the master problem that enforces the branching decision */
   SCIP_CALL( SCIPaddCons(masterscip, branchdata->cons) );
   
   SCIPfreeBufferArray(scip, &copymastervars);

   return SCIP_OKAY;
}

/** callback deactivation method */
static
GCG_DECL_BRANCHDEACTIVEMASTER(branchDeactiveMasterGeneric)
{
   SCIP* masterscip;
   
   assert(scip != NULL);
   assert(branchdata != NULL);
   assert(branchdata->cons != NULL);

   masterscip = GCGrelaxGetMasterprob(scip);
   assert(masterscip != NULL);

   SCIPdebugMessage("branchDeactiveMasterGeneric: Block %d, Ssize %d)\n", branchdata->blocknr,
      branchdata->Ssize);

   /* remove constraint from the master problem that enforces the branching decision */
   assert(branchdata->cons != NULL);
   SCIP_CALL( SCIPdelCons(masterscip, branchdata->cons) );

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

   /* include branching rule */
   SCIP_CALL( SCIPincludeBranchrule(scip, BRANCHRULE_NAME, BRANCHRULE_DESC, BRANCHRULE_PRIORITY, 
         BRANCHRULE_MAXDEPTH, BRANCHRULE_MAXBOUNDDIST, branchCopyGeneric,
         branchFreeGeneric, branchInitGeneric, branchExitGeneric, branchInitsolGeneric, 
         branchExitsolGeneric, branchExeclpGeneric, branchExecextGeneric, branchExecpsGeneric,
         branchruledata) );

   return SCIP_OKAY;
}
