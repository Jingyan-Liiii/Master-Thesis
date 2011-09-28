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
/**@file   reader_blk.c
 * @brief  BLK file reader
 * @ingroup FILEREADERS
 * @author Lukas Kirchhart
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
#include <struct_decomp.h>

#include "reader_blk_n.h"
#include "relax_gcg.h"
#include "scip/cons_knapsack.h"
#include "scip/cons_linear.h"
#include "scip/cons_logicor.h"
#include "scip/cons_setppc.h"
#include "scip/cons_varbound.h"
#include "scip/cons_sos1.h"
#include "scip/cons_sos2.h"
#include "scip_misc.h"


#define READER_NAME             "blkreader2"
#define READER_DESC             "file reader for blocks corresponding to a mip in lpb format"
#define READER_EXTENSION        "blk2"


/*
 * Data structures
 */
#define BLK_MAX_LINELEN       65536
#define BLK_MAX_PUSHEDTOKENS  2
#define BLK_INIT_COEFSSIZE    8192
#define BLK_MAX_PRINTLEN      561       /**< the maximum length of any line is 560 + '\\0' = 561*/
#define BLK_MAX_NAMELEN       256       /**< the maximum length for any name is 255 + '\\0' = 256 */
#define BLK_PRINTLEN          100

/** Section in BLK File */
enum BlkSection
{
   BLK_START, BLK_NBLOCKS, BLK_BLOCK, BLK_MASTERCONSS, BLK_END
};
typedef enum BlkSection BLKSECTION;

enum BlkExpType
{
   BLK_EXP_NONE, BLK_EXP_UNSIGNED, BLK_EXP_SIGNED
};
typedef enum BlkExpType BLKEXPTYPE;

/** BLK reading data */
struct BlkInput
{
   SCIP_FILE* file;
   char linebuf[BLK_MAX_LINELEN];
   char* token;
   char* tokenbuf;
   char* pushedtokens[BLK_MAX_PUSHEDTOKENS];
   int npushedtokens;
   int linenumber;
   int linepos;
   int nblocks;
   /**number of the currentblock between 0 and Nblocks-1*/
   int blocknr;
   BLKSECTION section;
   SCIP_Bool haserror;
};
typedef struct BlkInput BLKINPUT;

/** data for gp reader */
struct SCIP_ReaderData
{
   DECDECOMP* decdecomp;
   SCIP_HASHMAP *vartoindex;
   /*index=var id // value= -1 or blockID or -2 for multipleblocks*/
   int* varstoblock;
   /**n variable per block that are no linkingvars*/
   int* nblockvars;
   /**[blknr][consid]  */
   SCIP_CONS*** blockcons;
   int* nblockcons;
   /**index = consID; value = blockID*/
   int* usedcons;
   int nlinkingblocks;
   int nlinkingcons;
   int nlinkingvars;
};
static const int false = 1;
static const int true = 0;
static const int novalue = - 1;
static const int linkingvalue = - 2;
static const char delimchars[] = " \f\n\r\t\v";
static const char tokenchars[] = "-+:<>=";
static const char commentchars[] = "\\";




/*
 * Local methods (for reading)
 */

/** issues an error message and marks the BLK data to have errors */
static
void
syntaxError(
        SCIP* scip, /**< SCIP data structure */
        BLKINPUT* blkinput, /**< BLK reading data */
        const char* msg /**< error message */
        )
{
   char formatstr[256];

   assert(blkinput != NULL);

   SCIPverbMessage(scip, SCIP_VERBLEVEL_MINIMAL, NULL, "Syntax error in line %d: %s ('%s')\n",
           blkinput->linenumber, msg, blkinput->token);
   if( blkinput->linebuf[strlen(blkinput->linebuf) - 1] == '\n' )
   {
      SCIPverbMessage(scip, SCIP_VERBLEVEL_MINIMAL, NULL, "  input: %s", blkinput->linebuf);
   }
   else
   {
      SCIPverbMessage(scip, SCIP_VERBLEVEL_MINIMAL, NULL, "  input: %s\n", blkinput->linebuf);
   }
   (void) SCIPsnprintf(formatstr, 256, "         %%%ds\n", blkinput->linepos);
   SCIPverbMessage(scip, SCIP_VERBLEVEL_MINIMAL, NULL, formatstr, "^");
   blkinput->section = BLK_END;
   blkinput->haserror = TRUE;
}

/** returns whether a syntax error was detected */
static
SCIP_Bool
hasError(
        BLKINPUT* blkinput /**< BLK reading data */
        )
{
   assert(blkinput != NULL);

   return blkinput->haserror;
}

/** returns whether the given character is a token delimiter */
static
SCIP_Bool
isDelimChar(
        char c /**< input character */
        )
{
   return (c == '\0') || (strchr(delimchars, c) != NULL);
}

/** returns whether the given character is a single token */
static
SCIP_Bool
isTokenChar(
        char c /**< input character */
        )
{
   return (strchr(tokenchars, c) != NULL);
}

/** returns whether the current character is member of a value string */
static
SCIP_Bool
isValueChar(
        char c, /**< input character */
        char nextc, /**< next input character */
        SCIP_Bool firstchar, /**< is the given character the first char of the token? */
        SCIP_Bool* hasdot, /**< pointer to update the dot flag */
        BLKEXPTYPE* exptype /**< pointer to update the exponent type */
        )
{
   assert(hasdot != NULL);
   assert(exptype != NULL);

   if( isdigit(c) )
      return TRUE;
   else if( (*exptype == BLK_EXP_NONE) && ! (*hasdot) && (c == '.') )
   {
      *hasdot = TRUE;
      return TRUE;
   }
   else if( ! firstchar && (*exptype == BLK_EXP_NONE) && (c == 'e' || c == 'E') )
   {
      if( nextc == '+' || nextc == '-' )
      {
         *exptype = BLK_EXP_SIGNED;
         return TRUE;
      }
      else if( isdigit(nextc) )
      {
         *exptype = BLK_EXP_UNSIGNED;
         return TRUE;
      }
   }
   else if( (*exptype == BLK_EXP_SIGNED) && (c == '+' || c == '-') )
   {
      *exptype = BLK_EXP_UNSIGNED;
      return TRUE;
   }

   return FALSE;
}

/** reads the next line from the input file into the line buffer; skips comments;
 *  returns whether a line could be read
 */
static
SCIP_Bool
getNextLine(
        BLKINPUT* blkinput /**< BLK reading data */
        )
{
   int i;

   assert(blkinput != NULL);

   /* clear the line */
   BMSclearMemoryArray(blkinput->linebuf, BLK_MAX_LINELEN);

   /* read next line */
   blkinput->linepos = 0;
   blkinput->linebuf[BLK_MAX_LINELEN - 2] = '\0';
   if( SCIPfgets(blkinput->linebuf, sizeof (blkinput->linebuf), blkinput->file) == NULL )
      return FALSE;
   blkinput->linenumber ++;
   if( blkinput->linebuf[BLK_MAX_LINELEN - 2] != '\0' )
   {
      SCIPerrorMessage("Error: line %d exceeds %d characters\n", blkinput->linenumber, BLK_MAX_LINELEN - 2);
      blkinput->haserror = TRUE;
      return FALSE;
   }
   blkinput->linebuf[BLK_MAX_LINELEN - 1] = '\0';
   blkinput->linebuf[BLK_MAX_LINELEN - 2] = '\0'; /* we want to use lookahead of one char -> we need two \0 at the end */

   /* skip characters after comment symbol */
   for( i = 0; commentchars[i] != '\0'; ++ i )
   {
      char* commentstart;

      commentstart = strchr(blkinput->linebuf, commentchars[i]);
      if( commentstart != NULL )
      {
         *commentstart = '\0';
         *(commentstart + 1) = '\0'; /* we want to use lookahead of one char -> we need two \0 at the end */
      }
   }

   return TRUE;
}

/** swaps the addresses of two pointers */
static
void
swapPointers(
        char** pointer1, /**< first pointer */
        char** pointer2 /**< second pointer */
        )
{
   char* tmp;

   tmp = * pointer1;
   *pointer1 = * pointer2;
   *pointer2 = tmp;
}

/** reads the next token from the input file into the token buffer; returns whether a token was read */
static
SCIP_Bool
getNextToken(BLKINPUT* blkinput /**< BLK reading data */)
{
   SCIP_Bool hasdot;
   BLKEXPTYPE exptype;
   char* buf;
   int tokenlen;

   assert(blkinput != NULL);
   assert(blkinput->linepos < BLK_MAX_LINELEN);

   /* check the token stack */
   if( blkinput->npushedtokens > 0 )
   {
      swapPointers(&blkinput->token, &blkinput->pushedtokens[blkinput->npushedtokens - 1]);
      blkinput->npushedtokens --;
      SCIPdebugMessage("(line %d) read token again: '%s'\n", blkinput->linenumber, blkinput->token);
      return TRUE;
   }

   /* skip delimiters */
   buf = blkinput->linebuf;
   while( isDelimChar(buf[blkinput->linepos]) )
   {
      if( buf[blkinput->linepos] == '\0' )
      {
         if( ! getNextLine(blkinput) )
         {
            blkinput->section = BLK_END;
            SCIPdebugMessage("(line %d) end of file\n", blkinput->linenumber);
            return FALSE;
         }
         assert(blkinput->linepos == 0);
      }
      else
         blkinput->linepos ++;
   }
   assert(blkinput->linepos < BLK_MAX_LINELEN);
   assert(! isDelimChar(buf[blkinput->linepos]));

   /* check if the token is a value */
   hasdot = FALSE;
   exptype = BLK_EXP_NONE;
   if( isValueChar(buf[blkinput->linepos], buf[blkinput->linepos + 1], TRUE, &hasdot, &exptype) )
   {
      /* read value token */
      tokenlen = 0;
      do
      {
         assert(tokenlen < BLK_MAX_LINELEN);
         assert(! isDelimChar(buf[blkinput->linepos]));
         blkinput->token[tokenlen] = buf[blkinput->linepos];
         tokenlen ++;
         blkinput->linepos ++;
      }
      while( isValueChar(buf[blkinput->linepos], buf[blkinput->linepos + 1], FALSE, &hasdot, &exptype) );
   }
   else
   {
      /* read non-value token */
      tokenlen = 0;
      do
      {
         assert(tokenlen < BLK_MAX_LINELEN);
         blkinput->token[tokenlen] = buf[blkinput->linepos];
         tokenlen ++;
         blkinput->linepos ++;
         if( tokenlen == 1 && isTokenChar(blkinput->token[0]) )
            break;
      }
      while( ! isDelimChar(buf[blkinput->linepos]) && ! isTokenChar(buf[blkinput->linepos]) );

      /* if the token is an equation sense '<', '>', or '=', skip a following '='
       * if the token is an equality token '=' and the next character is a '<' or '>', replace the token by the inequality sense
       */
      if( tokenlen >= 1
              && (blkinput->token[tokenlen - 1] == '<' || blkinput->token[tokenlen - 1] == '>' || blkinput->token[tokenlen - 1] == '=')
              && buf[blkinput->linepos] == '=' )
      {
         blkinput->linepos ++;
      }
      else if( blkinput->token[tokenlen - 1] == '=' && (buf[blkinput->linepos] == '<' || buf[blkinput->linepos] == '>') )
      {
         blkinput->token[tokenlen - 1] = buf[blkinput->linepos];
         blkinput->linepos ++;
      }
   }
   assert(tokenlen < BLK_MAX_LINELEN);
   blkinput->token[tokenlen] = '\0';

   SCIPdebugMessage("(line %d) read token: '%s'\n", blkinput->linenumber, blkinput->token);
   //printf("(line %d) read token: '%s'\n", blkinput->linenumber, blkinput->token);

   return TRUE;
}

/** puts the current token on the token stack, such that it is read at the next call to getNextToken() */
static
void
pushToken(
        BLKINPUT* blkinput /**< BLK reading data */
        )
{
   assert(blkinput != NULL);
   assert(blkinput->npushedtokens < BLK_MAX_PUSHEDTOKENS);

   swapPointers(&blkinput->pushedtokens[blkinput->npushedtokens], &blkinput->token);
   blkinput->npushedtokens ++;
}

/** swaps the current token with the token buffer */
static
void
swapTokenBuffer(
        BLKINPUT* blkinput /**< BLK reading data */
        )
{
   assert(blkinput != NULL);

   swapPointers(&blkinput->token, &blkinput->tokenbuf);
}

/** returns whether the current token is a value */
static
SCIP_Bool
isInt(
        SCIP* scip, /**< SCIP data structure */
        BLKINPUT* blkinput, /**< BLK reading data */
        int* value /**< pointer to store the value (unchanged, if token is no value) */
        )
{
   assert(blkinput != NULL);
   assert(value != NULL);

   if( strcasecmp(blkinput->token, "INFINITY") == 0 || strcasecmp(blkinput->token, "INF") == 0 )
   {
      *value = SCIPinfinity(scip);
      return TRUE;
   }
   else
   {
      long val;
      char* endptr;

      val = strtol(blkinput->token, &endptr, 0);
      if( endptr != blkinput->token && * endptr == '\0' )
      {
         *value = val;
         return TRUE;
      }
   }

   return FALSE;
}

/** checks whether the current token is a section identifier, and if yes, switches to the corresponding section */
static
SCIP_Bool
isNewSection(
        SCIP* scip, /**< SCIP data structure */
        BLKINPUT* blkinput /**< BLK reading data */
        )
{
   SCIP_Bool iscolon;

   assert(blkinput != NULL);

   /* remember first token by swapping the token buffer */
   swapTokenBuffer(blkinput);

   /* look at next token: if this is a ':', the first token is a name and no section keyword */
   iscolon = FALSE;
   if( getNextToken(blkinput) )
   {
      iscolon = (strcmp(blkinput->token, ":") == 0);
      pushToken(blkinput);
   }

   /* reinstall the previous token by swapping back the token buffer */
   swapTokenBuffer(blkinput);

   /* check for ':' */
   if( iscolon )
      return FALSE;

   if( strcasecmp(blkinput->token, "NBLOCKS") == 0 )
   {
      SCIPdebugMessage("(line %d) new section: NBLOCKS\n", blkinput->linenumber);
      blkinput->section = BLK_NBLOCKS;
      return TRUE;
   }

   if( strcasecmp(blkinput->token, "BLOCK") == 0 )
   {
      int blocknr;

      blkinput->section = BLK_BLOCK;

      if( getNextToken(blkinput) )
      {
         /* read block number */
         if( isInt(scip, blkinput, &blocknr) )
         {
            assert(blocknr >= 0);
            assert(blocknr <= blkinput->nblocks);

            blkinput->blocknr = blocknr - 1;
         }
         else
            syntaxError(scip, blkinput, "no block number after block keyword!\n");
      }
      else
         syntaxError(scip, blkinput, "no block number after block keyword!\n");

      SCIPdebugMessage("new section: BLOCK %d\n", blkinput->blocknr);

      return TRUE;

   }

   if( strcasecmp(blkinput->token, "MASTERCONSS") == 0 )
   {
      blkinput->section = BLK_MASTERCONSS;

      SCIPdebugMessage("new section: MASTERCONSS\n");

      return TRUE;
   }

   if( strcasecmp(blkinput->token, "END") == 0 )
   {
      SCIPdebugMessage("(line %d) new section: END\n", blkinput->linenumber);
      blkinput->section = BLK_END;
      return TRUE;
   }

   return FALSE;
}

/** reads the header of the file */
static SCIP_RETCODE
readStart(
        SCIP* scip, /**< SCIP data structure */
        BLKINPUT* blkinput /**< BLK reading data */
        )
{
   assert(blkinput != NULL);

   /* everything before first section is treated as comment */
   do
   {
      /* get token */
      if( ! getNextToken(blkinput) )
         return SCIP_OKAY;
   }
   while( ! isNewSection(scip, blkinput) );

   return SCIP_OKAY;
}

/** reads the nblocks section */
static
SCIP_RETCODE
readNBlocks(
        SCIP* scip, /**< SCIP data structure */
        BLKINPUT* blkinput /**< BLK reading data */
        )
{
   int nblocks;

   assert(blkinput != NULL);

   while( getNextToken(blkinput) )
   {
      /* check if we reached a new section */
      if( isNewSection(scip, blkinput) )
         return SCIP_OKAY;

      /* read number of blocks */
      if( isInt(scip, blkinput, &nblocks) )
      {
         //assert(nblocks > 0);


         if( blkinput->nblocks == novalue )
         {
            blkinput->nblocks = nblocks;
            GCGrelaxSetNPricingprobs(scip, nblocks);
         }
         else
            syntaxError(scip, blkinput, "2 integer values in nblocks section");
         SCIPdebugMessage("Number of blocks = %d\n", blkinput->nblocks);
      }
   }

   return SCIP_OKAY;
}

/** reads the blocks section */
static
SCIP_RETCODE
readBlock(
        SCIP* scip, /**< SCIP data structure */
        BLKINPUT* blkinput /**< BLK reading data */
        )
{
   SCIP_READER* reader;
   SCIP_READERDATA* readerdata;
   int varvalue;
   int varvalueindex;
   int consindex;
   int i;
   int blockid;
   int nvars;
   SCIP_CONS* cons;

   SCIP_VAR** vars;
   assert(blkinput != NULL);

   reader = SCIPfindReader(scip, READER_NAME);
   assert(reader != NULL);

   readerdata = SCIPreaderGetData(reader);
   assert(readerdata != NULL);

   while( getNextToken(blkinput) )
   {
      /* check if we reached a new section */
      if( isNewSection(scip, blkinput) )
         break;

      /* the token must be the name of an existing cons */
      cons = SCIPfindCons(scip, blkinput->token);
      if( cons == NULL )
      {
         syntaxError(scip, blkinput, "unknown variable in block section");
         break;
      }
      //get all vars for the specific constraint
      vars = SCIPgetVarsXXX(scip, cons);
      nvars = SCIPgetNVarsXXX(scip, cons);
/*
      printf("nvars=%d at block=%d  cons=%s\n", nvars, blkinput->blocknr, SCIPconsGetName(cons));
*/
      for( i = 0; i < nvars; i ++ )
      {
         /*
          * set each var to the current block
          */
         SCIP_CALL(GCGrelaxSetOriginalVarBlockNr(scip, vars[i], blkinput->blocknr));
         /*
          * saving for dedecomp
          */
         readerdata->nblockvars[blkinput->blocknr] = (readerdata->nblockvars[blkinput->blocknr]) + 1;

         //mark each var if it is in none, one or more blocks
         varvalueindex = SCIPvarGetProbindex(vars[i]);
         varvalue = readerdata->varstoblock[varvalueindex ];
         if( varvalue == novalue )
         {
            readerdata->varstoblock[varvalueindex ] = blkinput->blocknr;
         }
         else if( varvalue != novalue || varvalue != linkingvalue )
         {
            readerdata->varstoblock[varvalueindex ] = linkingvalue;
            //decrease the value again if it is a linking var
            readerdata->nblockvars[blkinput->blocknr] = readerdata->nblockvars[blkinput->blocknr] - 1;
            readerdata->nlinkingvars = readerdata->nlinkingvars + 1;
         }
         /*
                  else
                  {
                     //do nothing  because value is already ==-2
                  }
          */
      }
/*
      printf("%i\n", readerdata->nblockvars[blkinput->blocknr]);
      printf("-------------------\n");
*/
      /*
       * saving block <-> constraint
       */
      blockid = blkinput->blocknr;
      consindex = readerdata->nblockcons[blockid];
      readerdata->blockcons[blkinput->blocknr][consindex] = cons;
      blockid = readerdata->nblockcons[blkinput->blocknr];
      readerdata->nblockcons[blkinput->blocknr] = readerdata->nblockcons[blkinput->blocknr] + 1;
      blockid = readerdata->nblockcons[blkinput->blocknr];

      /*funktioniert leider so nicht da SCIPconsGetPos nicht fuktioniert
            //used cons
            if( readerdata->usedcons[consindex] == novalue )
            {
               readerdata->usedcons[consindex] = blkinput->blocknr;
            }
            else
            {
               readerdata->usedcons[consindex] = linkingvalue;
               readerdata->nlinkingcons ++;
            }
                  consindex = SCIPconsGetPos(cons);
                  readerdata->blockcons[blkinput->blocknr][consindex] = consindex;
                  readerdata->nblockcons[blkinput->blocknr] ++;
                  //used cons
                  if( readerdata->usedcons[consindex] == novalue )
                  {
                     readerdata->usedcons[consindex] = blkinput->blocknr;
                  }
                  else
                  {
                     readerdata->usedcons[consindex] = linkingvalue;
                     readerdata->nlinkingcons ++;
                  }
       */


   }
   SCIPfreeMemoryArray(scip, &vars);
   return SCIP_OKAY;
}

/** reads the masterconss section */
static
SCIP_RETCODE
readMasterconss(
        SCIP* scip, /**< SCIP data structure */
        BLKINPUT* blkinput /**< BLK reading data */
        )
{
   assert(blkinput != NULL);

   while( getNextToken(blkinput) )
   {
      SCIP_CONS* cons;

      /* check if we reached a new section */
      if( isNewSection(scip, blkinput) )
         break;

      cons = NULL;

      /* the token must be the name of an existing constraint */
      cons = SCIPfindCons(scip, blkinput->token);
      if( cons == NULL )
      {
         syntaxError(scip, blkinput, "unknown constraint in masterconss section");
         break;
      }
      else
      {
         /* set the block number of the variable to the number of the current block */
         SCIP_CALL(GCGrelaxMarkConsMaster(scip, cons));
      }
   }

   return SCIP_OKAY;
}

/** fills the whole Decomp struct after the blk file has been read */
static
SCIP_RETCODE
fillDecompStruct(
        SCIP* scip, /**< SCIP data structure */
        BLKINPUT* blkinput, /**< BLK reading data */
        SCIP_READERDATA* readerdata/** reader data*/
        )
{
   /*
   SCIP_VAR***    subscipvars;
   int*           nsubscipvars;
   SCIP_CONS***   subscipconss;
   int*           nsubscipconss;
   SCIP_CONS**    linkingconss;
   int            nlinkingconss;
   SCIP_CONS**    linkingcuts;
   int            nlinkingcuts;
   SCIP_VAR**     linkingvars;
   int            nlinkingvars;
    */
   //delcare
   DECDECOMP* decomp;
   SCIP_VAR** allvars;
   SCIP_CONS** allcons;
   int i;
   int j;
   int n;
   int n2;
   int value;
   int ind;
   int tmp;

   decomp = readerdata->decdecomp;

   /*
    * alloc
    */
   SCIP_CALL(SCIPallocMemory(scip, &decomp));
   //nvars
   SCIP_CALL(SCIPallocMemoryArray(scip, &decomp->nsubscipvars, blkinput->nblocks));
   for( i = 0; i < blkinput->nblocks; i ++ )
   {
      decomp->nsubscipvars[i] = 0;
   }
   //vars
   SCIP_CALL(SCIPallocMemoryArray(scip, &decomp->subscipvars, blkinput->nblocks));
   for( i = 0; i < blkinput->nblocks; i ++ )
   {
      SCIP_CALL(SCIPallocMemoryArray(scip, &decomp->subscipvars[i], readerdata->nblockvars[i]));
   }
   //linking vars
   SCIP_CALL(SCIPallocMemoryArray(scip, &decomp->linkingvars, readerdata->nlinkingvars));
   decomp->nlinkingvars = 0;

   //cons
   SCIP_CALL(SCIPallocMemoryArray(scip, &decomp->subscipconss, blkinput->nblocks));
   for( i = 0; i < blkinput->nblocks; i ++ )
   {
      SCIP_CALL(SCIPallocMemoryArray(scip, &decomp->subscipconss[i], readerdata->nblockcons[i]));
   }
   //n cons
   SCIP_CALL(SCIPallocMemoryArray(scip, &decomp->nsubscipconss, blkinput->nblocks));
   for( i = 0; i < blkinput->nblocks; i ++ )
   {
      decomp->nsubscipconss[i] = 0;
   }

   /*
      //linking cons
      SCIP_CALL(SCIPallocMemoryArray(scip, &decomp->linkingconss, readerdata->nlinkingcons));
    */
   decomp->nlinkingconss = 0;



   //init
   allvars = SCIPgetVars(scip);
   allcons = SCIPgetConss(scip);
   //TODO init the whole decomp
   /*
    * insert values to decomp struct
    */
   //vars to blocks
   n = SCIPgetNVars(scip);
   for( i = 0; i < n; i ++ )
   {
      value = readerdata->varstoblock[i];
      if( value == novalue )
      {
         //TODO what shall I do in this case ??
      }
      else if( value == linkingvalue )
      {
         ind = decomp->nlinkingvars;
         decomp->linkingvars[ind] = allvars[i];
         decomp->nlinkingvars = decomp->nlinkingvars + 1;
      }
      else
      {//value = block id=index of block
         assert(value >= 0);
         assert(value <= blkinput->nblocks);
         //TODO check if "i" is the id of the variable
         ind = decomp->nsubscipvars[value];

         assert(ind >= 0);
         tmp = readerdata->nblockvars[value];
         assert(ind <= readerdata->nblockvars[value]);


         decomp->subscipvars[value][ind] = allvars[i];
         decomp->nsubscipvars[value] ++;
      }
   }
   //constraints to blocks
   /* n = blkinput->nblocks;
    n2 = SCIPgetNConss(scip);
    for( i = 0; i < n; i ++ )
    {
       n2 = readerdata->nblockcons[i];
       for( j = 0; j < n2; j ++ )
       {
                //j=consID
                   blockid = cons[j];
         

                   if( value != novalue )
                   {
                      if( readerdata->usedcons[j] != linkingvalue )
                      {
                         ind = (int) decomp->nsubscipconss[i];
                         decomp->subscipconss[i][ind] = allcons[j];
                         decomp->nsubscipconss[i] ++;
                      }
                      else
                      {
                         ind = decomp->nlinkingconss;
                         decomp->linkingconss[ind] = allcons[j];
                         decomp->nlinkingconss ++;
                      }
                   }
     }
     }
    */
   n = blkinput->nblocks;
   for( i = 0; i < n; i ++ )
   {
      n2 = readerdata->nblockcons[i];
      for( j = 0; j < n2; j ++ )
      {
         ind = decomp->nsubscipconss[i];
         decomp->subscipconss[i][ind] = readerdata->blockcons[i][j];
         decomp->nsubscipconss[i] ++;
         ind = decomp->nsubscipconss[i];
      }
   }

   for( i = 0; i < blkinput->nblocks; i ++ )
   {
      SCIPfreeMemoryArray(scip, &readerdata->blockcons[i]);
   }
   SCIPfreeMemoryArray(scip, &readerdata->blockcons);
   SCIPfreeMemoryArray(scip, &readerdata->nblockcons);
   SCIPfreeMemoryArray(scip, &readerdata->varstoblock);
   SCIPfreeMemoryArray(scip, &readerdata->nblockvars);
   SCIPfreeMemoryArray(scip, &readerdata->usedcons);

   return SCIP_OKAY;
}

/** reads an BLK file */
static
SCIP_RETCODE
readBLKFile(
        SCIP* scip, /**< SCIP data structure */
        BLKINPUT* blkinput, /**< BLK reading data */
        const char* filename /**< name of the input file */
        )
{
   int i;
   int n;
   int nblocksread;
   int nvars;
   SCIP_READER* reader;
   SCIP_READERDATA* readerdata;
   nblocksread = false;
   assert(blkinput != NULL);

   SCIP_CALL(GCGrelaxCreateOrigVarsData(scip));

   /* open file */
   blkinput->file = SCIPfopen(filename, "r");
   if( blkinput->file == NULL )
   {
      SCIPerrorMessage("cannot open file <%s> for reading\n", filename);
      SCIPprintSysError(filename);
      return SCIP_NOFILE;
   }

   //TODO check
   assert(scip != NULL);

   reader = SCIPfindReader(scip, READER_NAME);
   assert(reader != NULL);

   readerdata = SCIPreaderGetData(reader);
   assert(readerdata != NULL);

   readerdata->nlinkingcons = 0;
   readerdata->nlinkingvars = 0;
   nvars = SCIPgetNVars(scip);
   //alloc: var -> block mapping
   SCIP_CALL(SCIPallocMemoryArray(scip, &readerdata->varstoblock, nvars));
   for( i = 0; i < nvars; i ++ )
   {
      readerdata->varstoblock[i] = novalue;
   }
   //alloc usedcons
   SCIP_CALL(SCIPallocMemoryArray(scip, &readerdata->usedcons, SCIPgetNConss(scip)));
   for( i = 0; i < SCIPgetNConss(scip); i ++ )
   {
      readerdata->usedcons[i] = novalue;
   }


   /* parse the file */
   blkinput->section = BLK_START;
   while( blkinput->section != BLK_END && ! hasError(blkinput) )
   {
      switch( blkinput->section )
      {
         case BLK_START:
            SCIP_CALL(readStart(scip, blkinput));
            break;

         case BLK_NBLOCKS:
            SCIP_CALL(readNBlocks(scip, blkinput));
            break;

         case BLK_BLOCK:
            if( nblocksread == false )
            {
               //alloc n vars per block
               SCIP_CALL(SCIPallocMemoryArray(scip, &readerdata->nblockvars, blkinput->nblocks));
               for( i = 0; i < blkinput->nblocks; i ++ )
               {
                  readerdata->nblockvars[i] = 0;
               }
               //alloc: constraint -> block
               SCIP_CALL(SCIPallocMemoryArray(scip, &readerdata->blockcons, blkinput->nblocks));
               n = SCIPgetNConss(scip);
               for( i = 0; i < blkinput->nblocks; i ++ )
               {
                  SCIP_CALL(SCIPallocMemoryArray(scip, &readerdata->blockcons[i], n));
               }
               SCIP_CALL(SCIPallocMemoryArray(scip, &readerdata->nblockcons, blkinput->nblocks));
               for( i = 0; i < blkinput->nblocks; i ++ )
               {
                  readerdata->nblockcons[i] = 0;
               }
               nblocksread = true;
            }
            SCIP_CALL(readBlock(scip, blkinput));
            break;

         case BLK_MASTERCONSS:
            SCIP_CALL(readMasterconss(scip, blkinput));
            break;

         case BLK_END: /* this is already handled in the while() loop */
         default:
            SCIPerrorMessage("invalid BLK file section <%d>\n", blkinput->section);
            return SCIP_INVALIDDATA;
      }
   }
   //fill decomp
   SCIP_CALL(fillDecompStruct(scip, blkinput, readerdata));

   /* close file */
   SCIPfclose(blkinput->file);

   return SCIP_OKAY;
}


/*
 * Callback methods of reader
 */

/** destructor of reader to free user data (called when SCIP is exiting) */
#define readerFreeBlk NULL

/** problem reading method of reader */
static
SCIP_DECL_READERREAD(readerReadBlk2)
{
   SCIP_CALL(SCIPreadBlk2(scip, reader, filename, result));

   return SCIP_OKAY;
}

/** problem writing method of reader */
static
SCIP_DECL_READERWRITE(readerWriteBlk2)
{
   return SCIP_OKAY;
}

/*
 * reader specific interface methods
 */

/** includes the blk file reader in SCIP */
SCIP_RETCODE
SCIPincludeReaderBlk2(
        SCIP * scip /**< SCIP data structure */
        )
{
   SCIP_READERDATA* readerdata;

   /* create blk reader data */
   SCIP_CALL(SCIPallocMemory(scip, &readerdata));

   /* include lp reader */
   SCIP_CALL(SCIPincludeReader(scip, READER_NAME, READER_DESC, READER_EXTENSION, NULL,
           readerFreeBlk, readerReadBlk2, readerWriteBlk2, readerdata));

   return SCIP_OKAY;
}

/* reads problem from file */
SCIP_RETCODE
SCIPreadBlk2(
        SCIP* scip, /**< SCIP data structure */
        SCIP_READER* reader, /**< the file reader itself */
        const char* filename, /**< full path and name of file to read, or NULL if stdin should be used */
        SCIP_RESULT * result /**< pointer to store the result of the file reading call */
        )
{
   BLKINPUT blkinput;
   int i;

   /* initialize BLK input data */
   blkinput.file = NULL;
   blkinput.linebuf[0] = '\0';
   SCIP_CALL(SCIPallocMemoryArray(scip, &blkinput.token, BLK_MAX_LINELEN));
   blkinput.token[0] = '\0';
   SCIP_CALL(SCIPallocMemoryArray(scip, &blkinput.tokenbuf, BLK_MAX_LINELEN));
   blkinput.tokenbuf[0] = '\0';
   for( i = 0; i < BLK_MAX_PUSHEDTOKENS; ++ i )
   {
      SCIP_CALL(SCIPallocMemoryArray(scip, &blkinput.pushedtokens[i], BLK_MAX_LINELEN));
   }

   blkinput.npushedtokens = 0;
   blkinput.linenumber = 0;
   blkinput.linepos = 0;
   blkinput.section = BLK_START;
   blkinput.nblocks = novalue;
   blkinput.blocknr = - 2;
   blkinput.haserror = FALSE;

   /* read the file */
   SCIP_CALL(readBLKFile(scip, &blkinput, filename));

   /* free dynamically allocated memory */
   SCIPfreeMemoryArray(scip, &blkinput.token);
   SCIPfreeMemoryArray(scip, &blkinput.tokenbuf);
   for( i = 0; i < BLK_MAX_PUSHEDTOKENS; ++ i )
   {
      SCIPfreeMemoryArray(scip, &blkinput.pushedtokens[i]);
   }

   /* evaluate the result */
   if( blkinput.haserror )
      return SCIP_READERROR;
   else
   {
      *result = SCIP_SUCCESS;
   }

   return SCIP_OKAY;
}

/*
 * reader specific interface methods
 */

SCIP_RETCODE
SCIPReaderBlk2SetDecomp(
        SCIP* scip,
        DECDECOMP * decdecomp
        )
{
   SCIP_READER* reader;
   SCIP_READERDATA* readerdata;
   assert(scip != NULL);

   assert(decdecomp != NULL);

   reader = SCIPfindReader(scip, READER_NAME);
   assert(reader != NULL);

   readerdata = SCIPreaderGetData(reader);
   assert(readerdata != NULL);

   readerdata->decdecomp = decdecomp;

   return SCIP_OKAY;
}
