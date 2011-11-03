/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                           */
/*                  This file is part of the program                         */
/*          GCG --- Generic Column Generation                                */
/*                  a Dantzig-Wolfe decomposition based extension            */
/*                  of the branch-cut-and-price framework                    */
/*         SCIP --- Solving Constraint Integer Programs                      */
/*                                                                           */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*#define SCIP_DEBUG*/
/**@file   reader_ref.c
 * @brief  REF file reader for files *ref.txt
 * @ingroup FILEREADERS
 * @author Gerald Gamrath
 * @author Christian Puchert
 *
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#include <stdlib.h>
#include <assert.h>
#include <string.h>
#if defined(_WIN32) || defined(_WIN64)
#else
#include <strings.h>
#endif
#include <ctype.h>

#include "reader_ref.h"
#include "relax_gcg.h"
#include "struct_decomp.h"
#include "scip/cons_linear.h"
#include "pub_gcgvar.h"

#define READER_NAME             "refreader"
#define READER_DESC             "file reader for blocks corresponding to a mip in lpb format"
#define READER_EXTENSION        "txt"

#define GCG_NATIVE_LINKINGVARS
/*
 * Data structures
 */
#define REF_MAX_LINELEN       65536
#define REF_MAX_PUSHEDTOKENS  2
#define REF_INIT_COEFSSIZE    8192
#define REF_MAX_PRINTLEN      561       /**< the maximum length of any line is 560 + '\\0' = 561*/
#define REF_MAX_NAMELEN       256       /**< the maximum length for any name is 255 + '\\0' = 256 */
#define REF_PRINTLEN          100

/** Section in REF File */
enum RefSection
{
   REF_START, REF_NBLOCKS, REF_BLOCKSIZES, REF_BLOCKS, REF_END
};
typedef enum RefSection REFSECTION;

enum RefExpType
{
   REF_EXP_NONE, REF_EXP_UNSIGNED, REF_EXP_SIGNED
};
typedef enum RefExpType REFEXPTYPE;


/** REF reading data */
struct RefInput
{
   SCIP_FILE*           file;
   char                 linebuf[REF_MAX_LINELEN];
   char*                token;
   char*                tokenbuf;
   char*                pushedtokens[REF_MAX_PUSHEDTOKENS];
   int                  npushedtokens;
   int                  linenumber;
   int                  linepos;
   int                  nblocks;
   int                  blocknr;
   int                  nassignedvars;
   int*                 blocksizes;
   int                  totalconss;
   int                  totalreadconss;
   SCIP_CONS**          markedmasterconss;
   int                  nmarkedmasterconss;
   REFSECTION           section;
   SCIP_Bool            haserror;
};
typedef struct RefInput REFINPUT;

static const char delimchars[] = " \f\n\r\t\v";
static const char tokenchars[] = "-+:<>=";
static const char commentchars[] = "\\";

struct SCIP_ReaderData
{
   DECDECOMP*           decdecomp;
};



/*
 * Local methods (for reading)
 */

/** issues an error message and marks the REF data to have errors */
static
void syntaxError(
   SCIP*                 scip,               /**< SCIP data structure */
   REFINPUT*             refinput,            /**< REF reading data */
   const char*           msg                 /**< error message */
   )
{
   char formatstr[256];

   assert(refinput != NULL);

   SCIPverbMessage(scip, SCIP_VERBLEVEL_MINIMAL, NULL, "Syntax error in line %d: %s ('%s')\n",
      refinput->linenumber, msg, refinput->token);
   if( refinput->linebuf[strlen(refinput->linebuf)-1] == '\n' )
   {
      SCIPverbMessage(scip, SCIP_VERBLEVEL_MINIMAL, NULL, "  input: %s", refinput->linebuf);
   }
   else
   {
      SCIPverbMessage(scip, SCIP_VERBLEVEL_MINIMAL, NULL, "  input: %s\n", refinput->linebuf);
   }
   (void) SCIPsnprintf(formatstr, 256, "         %%%ds\n", refinput->linepos);
   SCIPverbMessage(scip, SCIP_VERBLEVEL_MINIMAL, NULL, formatstr, "^");
   refinput->section  = REF_END;
   refinput->haserror = TRUE;
}

/** returns whether a syntax error was detected */
static
SCIP_Bool hasError(
   REFINPUT*              refinput             /**< REF reading data */
   )
{
   assert(refinput != NULL);

   return refinput->haserror;
}

/** returns whether the given character is a token delimiter */
static
SCIP_Bool isDelimChar(
   char                  c                   /**< input character */
   )
{
   return (c == '\0') || (strchr(delimchars, c) != NULL);
}

/** returns whether the given character is a single token */
static
SCIP_Bool isTokenChar(
   char                  c                   /**< input character */
   )
{
   return (strchr(tokenchars, c) != NULL);
}

/** returns whether the current character is member of a value string */
static
SCIP_Bool isValueChar(
   char                  c,                  /**< input character */
   char                  nextc,              /**< next input character */
   SCIP_Bool             firstchar,          /**< is the given character the first char of the token? */
   SCIP_Bool*            hasdot,             /**< pointer to update the dot flag */
   REFEXPTYPE*           exptype             /**< pointer to update the exponent type */
   )
{
   assert(hasdot != NULL);
   assert(exptype != NULL);

   if( isdigit(c) )
      return TRUE;
   else if( (*exptype == REF_EXP_NONE) && !(*hasdot) && (c == '.') )
   {
      *hasdot = TRUE;
      return TRUE;
   }
   else if( !firstchar && (*exptype == REF_EXP_NONE) && (c == 'e' || c == 'E') )
   {
      if( nextc == '+' || nextc == '-' )
      {
         *exptype = REF_EXP_SIGNED;
         return TRUE;
      }
      else if( isdigit(nextc) )
      {
         *exptype = REF_EXP_UNSIGNED;
         return TRUE;
      }
   }
   else if( (*exptype == REF_EXP_SIGNED) && (c == '+' || c == '-') )
   {
      *exptype = REF_EXP_UNSIGNED;
      return TRUE;
   }

   return FALSE;
}

/** reads the next line from the input file into the line buffer; skips comments;
 *  returns whether a line could be read
 */
static
SCIP_Bool getNextLine(
   REFINPUT*              refinput             /**< REF reading data */
   )
{
   int i;

   assert(refinput != NULL);

   /* clear the line */
   BMSclearMemoryArray(refinput->linebuf, REF_MAX_LINELEN);

   /* read next line */
   refinput->linepos = 0;
   refinput->linebuf[REF_MAX_LINELEN-2] = '\0';
   if( SCIPfgets(refinput->linebuf, sizeof(refinput->linebuf), refinput->file) == NULL )
      return FALSE;
   refinput->linenumber++;
   if( refinput->linebuf[REF_MAX_LINELEN-2] != '\0' )
   {
      SCIPerrorMessage("Error: line %d exceeds %d characters\n", refinput->linenumber, REF_MAX_LINELEN-2);
      refinput->haserror = TRUE;
      return FALSE;
   }
   refinput->linebuf[REF_MAX_LINELEN-1] = '\0';
   refinput->linebuf[REF_MAX_LINELEN-2] = '\0'; /* we want to use lookahead of one char -> we need two \0 at the end */

   /* skip characters after comment symbol */
   for( i = 0; commentchars[i] != '\0'; ++i )
   {
      char* commentstart;

      commentstart = strchr(refinput->linebuf, commentchars[i]);
      if( commentstart != NULL )
      {
         *commentstart = '\0';
         *(commentstart+1) = '\0'; /* we want to use lookahead of one char -> we need two \0 at the end */
      }
   }

   return TRUE;
}

/** reads the next token from the input file into the token buffer; returns whether a token was read */
static
SCIP_Bool getNextToken(
   REFINPUT*              refinput             /**< REF reading data */
   )
{
   SCIP_Bool hasdot;
   REFEXPTYPE exptype;
   char* buf;
   int tokenlen;

   assert(refinput != NULL);
   assert(refinput->linepos < REF_MAX_LINELEN);

   /* check the token stack */
   if( refinput->npushedtokens > 0 )
   {
      SCIPswapPointers((void**)&refinput->token, (void**)&refinput->pushedtokens[refinput->npushedtokens-1]);
      refinput->npushedtokens--;
      SCIPdebugMessage("(line %d) read token again: '%s'\n", refinput->linenumber, refinput->token);
      return TRUE;
   }

   /* skip delimiters */
   buf = refinput->linebuf;
   while( isDelimChar(buf[refinput->linepos]) )
   {
      if( buf[refinput->linepos] == '\0' )
      {
         if( !getNextLine(refinput) )
         {
            refinput->section = REF_END;
            refinput->blocknr++;
            SCIPdebugMessage("(line %d) end of file\n", refinput->linenumber);
            return FALSE;
         }
         else
         {
            if ( refinput->section == REF_START )
               refinput->section = REF_NBLOCKS;
            else if( refinput->section == REF_BLOCKSIZES )
            {
               refinput->section = REF_BLOCKS;
               refinput->blocknr = 0;
            }
            return FALSE;
         }
      }
      else
         refinput->linepos++;
   }
   assert(refinput->linepos < REF_MAX_LINELEN);
   assert(!isDelimChar(buf[refinput->linepos]));

   /* check if the token is a value */
   hasdot = FALSE;
   exptype = REF_EXP_NONE;
   if( isValueChar(buf[refinput->linepos], buf[refinput->linepos+1], TRUE, &hasdot, &exptype) )
   {
      /* read value token */
      tokenlen = 0;
      do
      {
         assert(tokenlen < REF_MAX_LINELEN);
         assert(!isDelimChar(buf[refinput->linepos]));
         refinput->token[tokenlen] = buf[refinput->linepos];
         tokenlen++;
         refinput->linepos++;
      }
      while( isValueChar(buf[refinput->linepos], buf[refinput->linepos+1], FALSE, &hasdot, &exptype) );
   }
   else
   {
      /* read non-value token */
      tokenlen = 0;
      do
      {
         assert(tokenlen < REF_MAX_LINELEN);
         refinput->token[tokenlen] = buf[refinput->linepos];
         tokenlen++;
         refinput->linepos++;
         if( tokenlen == 1 && isTokenChar(refinput->token[0]) )
            break;
      }
      while( !isDelimChar(buf[refinput->linepos]) && !isTokenChar(buf[refinput->linepos]) );

      /* if the token is an equation sense '<', '>', or '=', skip a following '='
       * if the token is an equality token '=' and the next character is a '<' or '>', replace the token by the inequality sense
       */
      if( tokenlen >= 1
         && (refinput->token[tokenlen-1] == '<' || refinput->token[tokenlen-1] == '>' || refinput->token[tokenlen-1] == '=')
         && buf[refinput->linepos] == '=' )
      {
         refinput->linepos++;
      }
      else if( refinput->token[tokenlen-1] == '=' && (buf[refinput->linepos] == '<' || buf[refinput->linepos] == '>') )
      {
         refinput->token[tokenlen-1] = buf[refinput->linepos];
         refinput->linepos++;
      }
   }
   assert(tokenlen < REF_MAX_LINELEN);
   refinput->token[tokenlen] = '\0';

//   SCIPdebugMessage("(line %d) read token: '%s'\n", refinput->linenumber, refinput->token);
   //printf("(line %d) read token: '%s'\n", refinput->linenumber, refinput->token);

   return TRUE;
}

/** returns whether the current token is a value */
static
SCIP_Bool isInt(
   SCIP*                 scip,               /**< SCIP data structure */
   REFINPUT*             refinput,           /**< REF reading data */
   int*                  value               /**< pointer to store the value (unchanged, if token is no value) */
   )
{
   assert(refinput != NULL);
   assert(value != NULL);

   if( strcasecmp(refinput->token, "INFINITY") == 0 || strcasecmp(refinput->token, "INF") == 0 )
   {
      *value = SCIPinfinity(scip);
      return TRUE;
   }
   else
   {
      long val;
      char* endptr;

      val = strtol(refinput->token, &endptr, 0);
      if( endptr != refinput->token && *endptr == '\0' )
      {
         *value = val;
         return TRUE;
      }
   }

   return FALSE;
}

/** reads the header of the file */
static
SCIP_RETCODE readStart(
   SCIP*                 scip,               /**< SCIP data structure */
   REFINPUT*             refinput            /**< REF reading data */
   )
{
   assert(refinput != NULL);

   getNextToken(refinput);

//   /* everything before first section is treated as comment */
//   do
//   {
//      /* get token */
//      if( !getNextToken(refinput) )
//         return SCIP_OKAY;
//   }
//   while( !isNewSection(scip, refinput) );

   return SCIP_OKAY;
}

/** reads the nblocks section */
static
SCIP_RETCODE readNBlocks(
   SCIP*                 scip,               /**< SCIP data structure */
   REFINPUT*             refinput            /**< REF reading data */
   )
{
   int nblocks;

   assert(refinput != NULL);

   if( getNextToken(refinput) )
   {
      /* read number of blocks */
      if( isInt(scip, refinput, &nblocks) )
      {
         if( refinput->nblocks == -1 )
         {
            refinput->nblocks = nblocks;
            SCIP_CALL( SCIPallocBufferArray(scip, &refinput->blocksizes, nblocks) );
            GCGrelaxSetNPricingprobs(scip, nblocks);
         }
         SCIPdebugMessage("Number of blocks = %d\n", refinput->nblocks);
      }
      else
         syntaxError(scip, refinput, "NBlocks: Value not an integer.\n");
   }
   else
      syntaxError(scip, refinput, "Could not read number of blocks.\n");

   refinput->section = REF_BLOCKSIZES;

   return SCIP_OKAY;
}

/** reads the blocksizes section */
static
SCIP_RETCODE readBlockSizes(
   SCIP*                 scip,               /**< SCIP data structure */
   REFINPUT*             refinput            /**< REF reading data */
   )
{
   int blocknr;
   int blocksize;

   assert(refinput != NULL);

   for( blocknr = 0; getNextToken(refinput) && blocknr < refinput->nblocks; blocknr++ )
   {
      if( isInt(scip, refinput, &blocksize) )
      {
         refinput->blocksizes[blocknr] = blocksize;
         refinput->totalconss += blocksize;
      }
      else
         syntaxError(scip, refinput, "Blocksize: Value not integer.\n");
   }
   if( blocknr != refinput->nblocks )
      syntaxError(scip, refinput, "Could not get sizes for all blocks.\n");

   return SCIP_OKAY;
}

/** reads the blocks section */
static
SCIP_RETCODE readBlocks(
   SCIP*                 scip,               /**< SCIP data structure */
   REFINPUT*             refinput            /**< REF reading data */
   )
{
   SCIP_CONS** conss;
   SCIP_CONS* cons;
   SCIP_VAR* var;
#ifndef GCG_NATIVE_LINKINGVARS
   SCIP_VAR** copyvars;
   SCIP_VAR* varcopy;
   int ncopyvars;
#endif
//   int val;
   int consctr;
   int v;

   assert(refinput != NULL);

   consctr = 0;

   while( refinput->blocknr < refinput->nblocks )
   {
      SCIPdebugMessage("Reading constraints of block %d/%d\n", refinput->blocknr + 1, refinput->nblocks);
      while( getNextToken(refinput) )
      {
         SCIP_CONSHDLR* conshdlr;
         SCIP_VAR** vars;
         int consnr;
         int nvars;

         conss = SCIPgetConss(scip);

         if( isInt(scip, refinput, &consnr) )
         {
            SCIPdebugMessage("  -> constraint %d\n", consnr);

            cons = conss[consnr];
            conshdlr = SCIPconsGetHdlr(cons);

            if( strcmp(SCIPconshdlrGetName(conshdlr), "linear") == 0)
            {
               vars = SCIPgetVarsLinear(scip, cons);
               nvars = SCIPgetNVarsLinear(scip, cons);
            }
            else
            {
               SCIPdebugMessage("    constraint of unknown type.\n");
               continue;
            }
#ifndef GCG_NATIVE_LINKINGVARS
            SCIP_CALL( SCIPallocBufferArray(scip, &copyvars, nvars));
            ncopyvars = 0;
#endif
            for( v = 0; v < nvars; v++ )
            {
               var = vars[v];

               SCIPdebugMessage("    -> variable %s\n", SCIPvarGetName(var));

               /* set the block number of the variable to the number of the current block */
#ifdef GCG_NATIVE_LINKINGVARS
               SCIP_CALL( GCGrelaxSetOriginalVarBlockNr(scip, var, refinput->blocknr) );
               refinput->nassignedvars++;
#else
               if( GCGvarGetBlock(var) == -1 )
               {
                  /* set the block number of the variable to the number of the current block */
                  SCIP_CALL( GCGrelaxSetOriginalVarBlockNr(scip, var, refinput->blocknr) );
                  refinput->nassignedvars++;
               }
               else if( GCGvarGetBlock(var) != refinput->blocknr )
               {
                  copyvars[ncopyvars] = var;
                  ncopyvars++;
               }
#endif
            }
#ifndef GCG_NATIVE_LINKINGVARS
            /* create copies for variables that are already assigned to another block */
            for( v = 0; v < ncopyvars; v++)
            {
               char newvarname[SCIP_MAXSTRLEN];

               var = copyvars[v];

               /* the variable already appears in another block, so we may need to copy it */
               (void) SCIPsnprintf(newvarname, SCIP_MAXSTRLEN, "%s_%d", SCIPvarGetName(var), refinput->blocknr + 1);
               varcopy = SCIPfindVar(scip, newvarname);
               if( varcopy == NULL )
               {
                  SCIP_CONS* couplingcons;
                  char consname[SCIP_MAXSTRLEN];

                  /* create and add a copy of the variable */
                  /* IMPORTANT: Do not take the original variable objective value as we might add it a couple of times */
                  SCIP_CALL( SCIPcreateVar(scip, &varcopy, newvarname, SCIPvarGetLbGlobal(var), SCIPvarGetUbGlobal(var),
                        0, SCIPvarGetType(var), SCIPvarIsInitial(var), SCIPvarIsRemovable(var),
                        NULL, NULL, NULL, NULL, NULL) );
                  SCIP_CALL( SCIPaddVar(scip, varcopy) );
                  SCIP_CALL( GCGrelaxCreateOrigVardata(scip, varcopy) );

                  /* assign the copy to the current block */
                  SCIP_CALL( GCGrelaxSetOriginalVarBlockNr(scip, varcopy, refinput->blocknr) );

                  /* create a coupling constraint between the variable and its copy */
                  (void) SCIPsnprintf(consname, SCIP_MAXSTRLEN, "coupling_%s_%s", SCIPvarGetName(var), newvarname);
                  SCIP_CALL( SCIPcreateConsLinear(scip, &couplingcons, consname, 0, NULL, NULL,
                        0, 0, TRUE, TRUE, TRUE, TRUE, TRUE, FALSE, TRUE, FALSE, FALSE, FALSE) );
                  SCIP_CALL( SCIPaddCoefLinear(scip, couplingcons, var, 1.0) );
                  SCIP_CALL( SCIPaddCoefLinear(scip, couplingcons, varcopy, -1.0) );
                  SCIP_CALL( SCIPaddCons(scip, couplingcons) );

                  /* the coupling constraint must be put into the master problem */
                  SCIP_CALL( SCIPreallocBufferArray(scip, &refinput->markedmasterconss, refinput->nmarkedmasterconss + 1) );
                  refinput->markedmasterconss[refinput->nmarkedmasterconss] = couplingcons;
                  refinput->nmarkedmasterconss++;

                  SCIPdebugMessage("    -> copied variable %s to %s\n", SCIPvarGetName(var), newvarname);
               }

               /* replace variable by its copy in the current constraint */
               SCIP_CALL( SCIPaddCoefLinear(scip, cons, var, -val) );
               SCIP_CALL( SCIPaddCoefLinear(scip, cons, varcopy, val) );
            }
            SCIPfreeBufferArray(scip, &copyvars);
#endif
            consctr++;
            refinput->totalreadconss++;
         }
         else
            syntaxError(scip, refinput, "ConsNr: Value not an integer.\n");
      }

      if( consctr == refinput->blocksizes[refinput->blocknr] )
      {
         refinput->blocknr++;
         consctr = 0;
      }
   }

   return SCIP_OKAY;
}



/** reads an REF file */
static
SCIP_RETCODE readREFFile(
   SCIP*                 scip,               /**< SCIP data structure */
   REFINPUT*             refinput,           /**< REF reading data */
   const char*           filename            /**< name of the input file */
   )
{
   int i;
   assert(refinput != NULL);

   SCIP_CALL( GCGcreateOrigVarsData(scip) );

   /* open file */
   refinput->file = SCIPfopen(filename, "r");
   if( refinput->file == NULL )
   {
      SCIPerrorMessage("cannot open file <%s> for reading\n", filename);
      SCIPprintSysError(filename);
      return SCIP_NOFILE;
   }

   /* parse the file */
   refinput->section = REF_START;
   while( refinput->section != REF_END && !hasError(refinput) )
   {
      switch( refinput->section )
      {
      case REF_START:
         SCIP_CALL( readStart(scip, refinput) );
         break;

      case REF_NBLOCKS:
         SCIP_CALL( readNBlocks(scip, refinput) );
         break;

      case REF_BLOCKSIZES:
         SCIP_CALL( readBlockSizes(scip, refinput) );
         break;

      case REF_BLOCKS:
         SCIP_CALL( readBlocks(scip, refinput) );
         break;

      case REF_END: /* this is already handled in the while() loop */
      default:
         SCIPerrorMessage("invalid REF file section <%d>\n", refinput->section);
         return SCIP_INVALIDDATA;
      }
   }

   /* close file */
   SCIPfclose(refinput->file);


   for (i = 0; i < refinput->nmarkedmasterconss; ++i)
   {
      SCIP_CALL( GCGrelaxMarkConsMaster(scip, refinput->markedmasterconss[i]) );
   }
   return SCIP_OKAY;
}

/** writes a BLK file */
static
SCIP_RETCODE writeREFFile(
   SCIP*                 scip,              /**< SCIP data structure */
   SCIP_READER*          reader,
   FILE*                 file

   )
{
   SCIP_READERDATA* readerdata;
   SCIP_HASHMAP *cons2origindex;

   SCIP_CONS** conss;
   int nconss;

   int i;
   int j;
   int nblocks;
   assert(scip != NULL);
   assert(reader != NULL);
   assert(file != NULL);

   readerdata = SCIPreaderGetData(reader);
   assert(readerdata != NULL);

   if(readerdata->decdecomp == NULL)
   {
      SCIPerrorMessage("No reformulation exists, cannot write reformulation file!\n");
      return SCIP_INVALIDCALL;
   }
   nblocks = readerdata->decdecomp->nblocks;
   conss = SCIPgetOrigConss(scip);
   nconss = SCIPgetNOrigConss(scip);

   SCIP_CALL(SCIPhashmapCreate(&cons2origindex, SCIPblkmem(scip), 2*nconss));
   for( i = 0; i < nconss; ++i)
   {
      size_t ind;
      size_t unconss;
      SCIP_CONS* cons;

      ind = i+1;
      unconss = nconss;
      assert(ind > 0);
      assert(ind <= unconss);
      cons = SCIPfindCons(scip, SCIPconsGetName(conss[i]));

      SCIPdebugMessage("cons added: %zu\t%p\t%s\n",ind, cons, SCIPconsGetName(cons));
      SCIP_CALL(SCIPhashmapInsert(cons2origindex, cons, (void*)(ind))); /* shift by 1 to enable error checking */

   }

   SCIPinfoMessage(scip, file, "%d ", nblocks);
   assert(readerdata->decdecomp->nsubscipconss != NULL);
   assert(readerdata->decdecomp->subscipconss != NULL);
   for(i = 0; i < nblocks; ++i)
   {
      SCIPinfoMessage(scip, file, "%d ", readerdata->decdecomp->nsubscipconss[i]);
   }
   SCIPinfoMessage(scip, file, "\n", nblocks);

   for(i = 0; i < nblocks; ++i)
   {
      for(j = 0; j < readerdata->decdecomp->nsubscipconss[i]; ++j)
      {
         size_t ind;
         size_t unconss;
         SCIP_CONS* cons;
//         assert(SCIPconsIsTransformed(readerdata->decdecomp->subscipconss[i][j]));
//         SCIP_CALL(SCIPgetTransformedCons(scip, readerdata->decdecomp->subscipconss[i][j], &cons));
//         assert(cons != NULL);
         cons = SCIPfindCons(scip, SCIPconsGetName(readerdata->decdecomp->subscipconss[i][j]));
         ind = (size_t)SCIPhashmapGetImage(cons2origindex, cons);
         SCIPdebugMessage("cons retrieve (o): %zu\t%p\t%s\n", ind, cons, SCIPconsGetName(cons));

         unconss = nconss;
         assert(ind > 0); /* shift by 1 */
         assert(ind <= unconss); /* shift by 1 */
         SCIPinfoMessage(scip, file, "%d ", ind-1);
      }
      SCIPinfoMessage(scip, file, "\n", nblocks);
   }
   SCIPhashmapFree(&cons2origindex);
   return SCIP_OKAY;
}


/*
 * Callback methods of reader
 */

/** destructor of reader to free user data (called when SCIP is exiting) */
static
SCIP_DECL_READERFREE(readerFreeRef)
{
   SCIP_READERDATA* readerdata;
   assert(scip != NULL);
   assert(reader != NULL);

   assert(strcmp(SCIPreaderGetName(reader), READER_NAME) == 0);
   readerdata = SCIPreaderGetData(reader);
   assert(readerdata != NULL);

   SCIPfreeMemory(scip, &readerdata);

   return SCIP_OKAY;
}


/** problem reading method of reader */
static
SCIP_DECL_READERREAD(readerReadRef)
{
   SCIP_CALL( SCIPreadRef(scip, reader, filename, result) );

   return SCIP_OKAY;
}


/** problem writing method of reader */
static
SCIP_DECL_READERWRITE(readerWriteRef)
{

   SCIP_CALL(writeREFFile(scip, reader, file));
   *result = SCIP_SUCCESS;
   return SCIP_OKAY;
}

/*
 * reader specific interface methods
 */

/** includes the blk file reader in SCIP */
SCIP_RETCODE SCIPincludeReaderRef(
   SCIP*                 scip                /**< SCIP data structure */
   )
{
   SCIP_READERDATA* readerdata;

   /* create blk reader data */
   SCIP_CALL(SCIPallocMemory(scip, &readerdata));

   /* include lp reader */
   SCIP_CALL( SCIPincludeReader(scip, READER_NAME, READER_DESC, READER_EXTENSION,
         NULL, readerFreeRef, readerReadRef, readerWriteRef, readerdata) );

   return SCIP_OKAY;
}


/* reads problem from file */
SCIP_RETCODE SCIPreadRef(
   SCIP*              scip,               /**< SCIP data structure */
   SCIP_READER*       reader,             /**< the file reader itself */
   const char*        filename,           /**< full path and name of file to read, or NULL if stdin should be used */
   SCIP_RESULT*       result              /**< pointer to store the result of the file reading call */
   )
{
   REFINPUT refinput;
   int i;

#ifdef SCIP_DEBUG
   SCIP_VAR** vars;
   int nvars;
#endif

   /* initialize REF input data */
   refinput.file = NULL;
   refinput.linebuf[0] = '\0';
   SCIP_CALL( SCIPallocMemoryArray(scip, &refinput.token, REF_MAX_LINELEN) );
   refinput.token[0] = '\0';
   SCIP_CALL( SCIPallocMemoryArray(scip, &refinput.tokenbuf, REF_MAX_LINELEN) );
   refinput.tokenbuf[0] = '\0';
   for( i = 0; i < REF_MAX_PUSHEDTOKENS; ++i )
   {
      SCIP_CALL( SCIPallocMemoryArray(scip, &refinput.pushedtokens[i], REF_MAX_LINELEN) );
   }
   SCIP_CALL( SCIPallocBufferArray(scip, &refinput.markedmasterconss, 1) );

   refinput.npushedtokens = 0;
   refinput.linenumber = 0;
   refinput.linepos = 0;
//   refinput.section = REF_START;
   refinput.nblocks = -1;
   refinput.blocknr = -2;
   refinput.totalconss = 0;
   refinput.totalreadconss = 0;
   refinput.nassignedvars = 0;
   refinput.nmarkedmasterconss = 0;
   refinput.haserror = FALSE;

   /* read the file */
   SCIP_CALL( readREFFile(scip, &refinput, filename) );

   SCIPdebugMessage("Read %d/%d conss in ref-file\n", refinput.totalreadconss, refinput.totalconss);
   SCIPdebugMessage("Assigned %d variables to %d blocks.\n", refinput.nassignedvars, refinput.nblocks);

#ifdef SCIP_DEBUG
   SCIP_CALL( SCIPgetVarsData(scip, &vars, &nvars, NULL, NULL, NULL, NULL));

   for( i = 0; i < nvars; i++ )
   {
      if( GCGvarGetBlock(vars[i]) == -1)
      {
         SCIPdebugMessage("  -> not assigned: variable %s\n", SCIPvarGetName(vars[i]));
      }
   }
#endif

//   SCIP_CALL( writeBLKFile(scip, &refinput) );

   /* free dynamically allocated memory */
   SCIPfreeMemoryArray(scip, &refinput.token);
   SCIPfreeMemoryArray(scip, &refinput.tokenbuf);
   for( i = 0; i < REF_MAX_PUSHEDTOKENS; ++i )
   {
      SCIPfreeMemoryArray(scip, &refinput.pushedtokens[i]);
   }
   SCIPfreeBufferArray(scip, &refinput.markedmasterconss);
   SCIPfreeBufferArray(scip, &refinput.blocksizes);

   /* evaluate the result */
   if( refinput.haserror )
      return SCIP_READERROR;
   else
   {
      *result = SCIP_SUCCESS;
   }

   return SCIP_OKAY;
}

/** set the decomp structure */
extern
SCIP_RETCODE SCIPReaderREFSetDecomp(
   SCIP*       scip,       /**< SCIP data structure */
   SCIP_READER* reader,    /**< Reader data structure */
   DECDECOMP*  decdecomp   /**< DECOMP data structure */
   )
{

   SCIP_READERDATA* readerdata;
   assert(scip != NULL);
   assert(decdecomp != NULL);
   assert(reader != NULL);

   assert(strcmp(SCIPreaderGetName(reader), READER_NAME) == 0);
   readerdata = SCIPreaderGetData(reader);

   assert(readerdata != NULL);

   readerdata->decdecomp = decdecomp;

   return SCIP_OKAY;
}
