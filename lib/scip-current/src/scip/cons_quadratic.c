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

/**@file   cons_quadratic.c
 * @ingroup CONSHDLRS
 * @brief  constraint handler for quadratic constraints
 * @author Stefan Vigerske
 * 
 * @todo SCIP might fix linear variables on +/- infinity; remove them in presolve and take care later
 * @todo constraints that are always feasible w.r.t. local/global bounds should be enabled/disabled (see logicor, setppc)
 * @todo round constraint bounds to integers if all coefficients and variables are (impl.) integer
 * @todo constraints in one variable should be replaced by linear variable or similar
 * @todo recognize and reformulate complementarity constraints (x*y = 0)
 * @todo check if some quadratic terms appear in several constraints and try to simplify (e.g., nous1)
 * @todo skip separation in enfolp if for current LP (check LP id) was already separated
 * @todo watch unbounded variables to enable/disable propagation
 * @todo sort order in bilinvar1/bilinvar2 such that the var which is involved in more terms is in bilinvar1, and use this info propagate and AddLinearReform
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/


#include <assert.h>
#include <string.h> /* for strcmp */ 
#include <ctype.h>  /* for isdigit */

#include "scip/cons_quadratic.h"
#include "scip/cons_linear.h"
#include "scip/cons_and.h"
#include "scip/cons_varbound.h"
#include "scip/intervalarith.h"
#include "scip/heur_subnlp.h"
#include "scip/heur_trysol.h"
#include "nlpi/nlpi.h"
#include "nlpi/nlpi_ipopt.h"

/* constraint handler properties */
#define CONSHDLR_NAME          "quadratic"
#define CONSHDLR_DESC          "quadratic constraints of the form lhs <= b' x + x' A x <= rhs"
#define CONSHDLR_SEPAPRIORITY        10 /**< priority of the constraint handler for separation */
#define CONSHDLR_ENFOPRIORITY       -50 /**< priority of the constraint handler for constraint enforcing */
#define CONSHDLR_CHECKPRIORITY -4000000 /**< priority of the constraint handler for checking feasibility */
#define CONSHDLR_SEPAFREQ             2 /**< frequency for separating cuts; zero means to separate only in the root node */
#define CONSHDLR_PROPFREQ             2 /**< frequency for propagating domains; zero means only preprocessing propagation */
#define CONSHDLR_EAGERFREQ          100 /**< frequency for using all instead of only the useful constraints in separation,
                                         *   propagation and enforcement, -1 for no eager evaluations, 0 for first only */
#define CONSHDLR_MAXPREROUNDS        -1 /**< maximal number of presolving rounds the constraint handler participates in (-1: no limit) */
#define CONSHDLR_DELAYSEPA        FALSE /**< should separation method be delayed, if other separators found cuts? */
#define CONSHDLR_DELAYPROP        FALSE /**< should propagation method be delayed, if other propagators found reductions? */
#define CONSHDLR_DELAYPRESOL      FALSE /**< should presolving method be delayed, if other presolvers found reductions? */
#define CONSHDLR_NEEDSCONS         TRUE /**< should the constraint handler be skipped, if no constraints are available? */

#define MAXDNOM                 10000LL /**< maximal denominator for simple rational fixed values */

/*
 * Data structures
 */

/** Eventdata for variable bound change events. */
struct SCIP_EventData
{
   SCIP_CONSDATA*        consdata;           /**< the constraint data */
   int                   varidx;             /**< the index of the variable which bound change is catched, positive for linear variables, negative for quadratic variables */
   int                   filterpos;          /**< position of eventdata in SCIP's event filter */
};

/** Data of a quadratic constraint. */
struct SCIP_ConsData
{
   SCIP_Real             lhs;                /**< left hand side of constraint */
   SCIP_Real             rhs;                /**< right hand side of constraint */

   int                   nlinvars;           /**< number of linear variables */
   int                   linvarssize;        /**< length of linear variable arrays */
   SCIP_VAR**            linvars;            /**< linear variables */
   SCIP_Real*            lincoefs;           /**< coefficients of linear variables */
   SCIP_EVENTDATA**      lineventdata;       /**< eventdata for bound change of linear variable */

   int                   nquadvars;          /**< number of variables in quadratic terms */
   int                   quadvarssize;       /**< length of quadratic variable terms arrays */
   SCIP_QUADVARTERM*     quadvarterms;       /**< array with quadratic variable terms */

   int                   nbilinterms;        /**< number of bilinear terms */
   int                   bilintermssize;     /**< length of bilinear term arrays */
   SCIP_BILINTERM*       bilinterms;         /**< bilinear terms array */

   SCIP_NLROW*           nlrow;              /**< a nonlinear row representation of this constraint */

   unsigned int          linvarssorted:1;    /**< are the linear variables already sorted? */
   unsigned int          linvarsmerged:1;    /**< are equal linear variables already merged? */
   unsigned int          quadvarssorted:1;   /**< are the quadratic variables already sorted? */
   unsigned int          quadvarsmerged:1;   /**< are equal quadratic variables already merged? */
   unsigned int          bilinsorted:1;      /**< are the bilinear terms already sorted? */
   unsigned int          bilinmerged:1;      /**< are equal bilinar terms already merged? */

   unsigned int          isconvex:1;         /**< is quadratic function is convex ? */
   unsigned int          isconcave:1;        /**< is quadratic function is concave ? */
   unsigned int          iscurvchecked:1;    /**< is quadratic function checked on convexity or concavity ? */
   unsigned int          isremovedfixings:1; /**< did we removed fixed/aggr/multiaggr variables ? */
   unsigned int          ispropagated:1;     /**< was the constraint propagated with respect to the current bounds ? */
   unsigned int          ispresolved:1;      /**< did we checked for possibilities of upgrading or implicit integer variables ? */

   SCIP_Real             minlinactivity;     /**< sum of minimal activities of all linear terms with finite minimal activity */
   SCIP_Real             maxlinactivity;     /**< sum of maximal activities of all linear terms with finite maximal activity */
   int                   minlinactivityinf;  /**< number of linear terms with infinite minimal activity */
   int                   maxlinactivityinf;  /**< number of linear terms with infinity maximal activity */
   SCIP_INTERVAL         quadactivitybounds; /**< bounds on the activity of the quadratic term, if up to date, otherwise empty interval */
   SCIP_Real             activity;           /**< activity of quadratic function w.r.t. current solution */
   SCIP_Real             lhsviol;            /**< violation of lower bound by current solution (used temporarily inside constraint handler) */
   SCIP_Real             rhsviol;            /**< violation of lower bound by current solution (used temporarily inside constraint handler) */

   int                   linvar_maydecrease; /**< index of a variable in linvars that may be decreased without making any other constraint infeasible, or -1 if none */
   int                   linvar_mayincrease; /**< index of a variable in linvars that may be increased without making any other constraint infeasible, or -1 if none */
};

/** quadratic constraint update method */
struct SCIP_QuadConsUpgrade
{
   SCIP_DECL_QUADCONSUPGD((*quadconsupgd));    /**< method to call for upgrading quadratic constraint */
   int                     priority;           /**< priority of upgrading method */
   SCIP_Bool               active;             /**< is upgrading enabled */
};
typedef struct SCIP_QuadConsUpgrade SCIP_QUADCONSUPGRADE; /**< quadratic constraint update method */

/** constraint handler data */
struct SCIP_ConshdlrData
{
   int                   replacebinaryprodlength;   /**< length of linear term which when multiplied with a binary variable is replaced by an auxiliary variable and an equivalent linear formulation */
   int                   empathy4and;               /**< how much empathy we have for using the AND constraint handler: 0 avoid always; 1 use sometimes; 2 use as often as possible */
   SCIP_Real             mincutefficacysepa;        /**< minimal efficacy of a cut in order to add it to relaxation during separation */
   SCIP_Real             mincutefficacyenfo;        /**< minimal target efficacy of a cut in order to add it to relaxation during enforcement (may be ignored) */
   SCIP_Bool             doscaling;                 /**< should constraints be scaled in the feasibility check ? */
   SCIP_Real             defaultbound;              /**< a bound to set for variables that are unbounded and in a nonconvex term after presolve */
   SCIP_Real             cutmaxrange;               /**< maximal range (maximal coef / minimal coef) of a cut in order to be added to LP */
   SCIP_Bool             linearizenlpsol;           /**< whether convex quadratic constraints should be linearized in a solution found by the NLP or RENS heuristic */
   SCIP_Bool             checkcurvature;            /**< whether functions should be checked for convexity/concavity */
   SCIP_Bool             linfeasshift;              /**< whether to make solutions in check feasible if possible */
   SCIP_Bool             disaggregate;              /**< whether to disaggregate quadratic constraints */
   int                   maxproprounds;             /**< limit on number of propagation rounds for a single constraint within one round of SCIP propagation */

   SCIP_HEUR*            subnlpheur;                /**< a pointer to the subNLP heuristic, if available */
   SCIP_HEUR*            rensheur;                  /**< a pointer to the RENS heuristic, if available */
   SCIP_HEUR*            trysolheur;                /**< a pointer to the TRYSOL heuristic, if available */
   SCIP_EVENTHDLR*       eventhdlr;                 /**< our handler for variable bound change events */
   int                   newsoleventfilterpos;      /**< filter position of new solution event handler, if catched */
  
   SCIP_QUADCONSUPGRADE** quadconsupgrades;         /**< quadratic constraint upgrade methods for specializing quadratic constraints */
   int                   quadconsupgradessize;      /**< size of quadconsupgrade array */
   int                   nquadconsupgrades;         /**< number of quadratic constraint upgrade methods */
#ifdef USECLOCK   
   SCIP_CLOCK*           clock1;
   SCIP_CLOCK*           clock2;
   SCIP_CLOCK*           clock3;
#endif
};

/** data structure for tokenizing strings */
struct SCIP_Tokenizer
{
   const char* strbuf;                       /**< string to be tokenized */
   char* token;                              /**< current token */
   char* tokenbuf;                           /**< token buffer */
   int strpos;                               /**< current position in string */
};
typedef struct SCIP_Tokenizer SCIP_TOKENIZER;     /**< data structure for tokenizing a string */

/** enum type for constraint sense */
enum CipSense
{
   CIP_SENSE_NOTHING, CIP_SENSE_LE, CIP_SENSE_GE, CIP_SENSE_EQ
};
typedef enum CipSense CIPSENSE;                   /**< enum type for constraint sense */


/*
 * local methods for managing quadratic constraint update methods
 */


/** checks whether a quadratic constraint upgrade method has already be registered */
static
SCIP_Bool conshdlrdataHasUpgrade(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONSHDLRDATA*    conshdlrdata,       /**< constraint handler data */
   SCIP_DECL_QUADCONSUPGD((*quadconsupgd)),  /**< method to call for upgrading quadratic constraint */
   const char*           conshdlrname        /**< name of the constraint handler */
   )
{
   int i;

   assert(scip != NULL);
   assert(conshdlrdata != NULL);
   assert(quadconsupgd != NULL);
   assert(conshdlrname != NULL);

   for( i = conshdlrdata->nquadconsupgrades - 1; i >= 0; --i )
   {
      if( conshdlrdata->quadconsupgrades[i]->quadconsupgd == quadconsupgd )
      {
#ifdef SCIP_DEBUG
         SCIPwarningMessage("Try to add already known upgrade message %p for constraint handler <%s>.\n", (void*)quadconsupgd, conshdlrname);
#endif
         return TRUE;
      }
   }

   return FALSE;
}

/*
 * Local methods
 */

/* local methods for parsing quadratic constraints */
static const char delimchars[] = " \f\n\r\t\v";
static const char tokenchars[] = "-+<>=^";

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

/** returns whether the current token is an equation sense */
static
SCIP_Bool isSense(
   SCIP_TOKENIZER*       tokenizer,          /**< LP reading data */
   CIPSENSE*             sense               /**< pointer to store the equation sense */
   )
{
   assert(tokenizer != NULL);
   assert(sense != NULL);

   if( strcmp(tokenizer->token, "<=") == 0 )
   {
      *sense = CIP_SENSE_LE;
      return TRUE;
   }
   else if( strcmp(tokenizer->token, ">=") == 0 )
   {
      *sense = CIP_SENSE_GE;
      return TRUE;
   }
   else if( strcmp(tokenizer->token, "==") == 0 )
   {
      *sense = CIP_SENSE_EQ;
      return TRUE;
   }

   return FALSE;
}

/** returns whether the current token is a sign */
static
SCIP_Bool isSign(
   SCIP_TOKENIZER*       tokenizer,          /**< LP reading data */
   int*                  sign                /**< pointer to update the sign */
   )
{
   assert(tokenizer != NULL);
   assert(sign != NULL);
   assert(*sign == +1 || *sign == -1);

   if( tokenizer->token[1] == '\0' )
   {
      if( *tokenizer->token == '+' )
         return TRUE;
      else if( *tokenizer->token == '-' )
      {
         *sign *= -1;
         return TRUE;
      }
   }

   return FALSE;
}

/** returns whether the current token is a value */
static
SCIP_Bool isValue(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_TOKENIZER*       lpinput,            /**< LP reading data */
   SCIP_Real*            value               /**< pointer to store the value (unchanged, if token is no value) */
   )
{
   assert(lpinput != NULL);
   assert(value != NULL);

   if( strcasecmp(lpinput->token, "INFINITY") == 0 || strcasecmp(lpinput->token, "INF") == 0 )
   {
      *value = SCIPinfinity(scip);
      return TRUE;
   }
   else
   {
      double val;
      char* endptr;

      val = strtod(lpinput->token, &endptr);
      if( endptr != lpinput->token && *endptr == '\0' )
      {
         *value = val;
         return TRUE;
      }
   }

   return FALSE;
}


/** returns whether the current character is member of a value string */
static
SCIP_Bool isValueChar(
   char                  c,                  /**< input character */
   char                  nextc,              /**< next input character */
   SCIP_Bool*            hasdot,             /**< pointer to update the dot flag */
   char*                 hasexp              /**< pointer to update the exponent flag */
   )
{
   assert(hasdot != NULL);
   assert(hasexp != NULL);
   assert(*hasexp == 0 || *hasexp == 1 || *hasexp == 2);

   /* hasexp = 0 means, that we did not had an 'e', 'E', 'd', or 'D' yet
    * hasexp = 1 means, that we just had an 'e', 'E', 'd', or 'D'; in this case, an '-' is allowed to follow
    * hasexp = 2 means, that we already had an 'e', 'E', 'd', or 'D', but it is more than 1 character away
    */

   if( *hasexp == 1 )
   { /* we just had an 'e', 'E', 'd', or 'D', so we set *hasexp to 2
        this is now the only case where an '-' or '+' is allowed */
      *hasexp = 2;
      if( (c == '-' || c == '+') && isdigit(nextc) )
         return TRUE;
   }

   if( isdigit(c) )
      return TRUE;
   else if( !(*hasdot) && !(*hasexp) && (c == '.') && isdigit(nextc) )
   {
      *hasdot = TRUE;
      return TRUE;
   }
   else if( (*hasexp == 0) && (c == 'e' || c == 'E' || c == 'd' || c == 'D') && (isdigit(nextc) || nextc == '-' || nextc == '+') )
   {
      *hasexp = 1;
      return TRUE;
   }

   return FALSE;
}

/** reads the next token from the string into the token buffer; returns whether a token was read */
static
SCIP_Bool getNextToken(
   SCIP_TOKENIZER*       tokenizer           /**< string tokenizer */
   )
{
   SCIP_Bool hasdot;
   char hasexp;
   int tokenlen;
   const char* buf;
   
   /* skip delimiters */
   buf = tokenizer->strbuf;
   while( isDelimChar(buf[tokenizer->strpos]) )
   {
      if( buf[tokenizer->strpos] == '\0' )
      {
         SCIPdebugMessage("end of string\n");
         return FALSE;
      }
      
      tokenizer->strpos++;
   }

   assert(!isDelimChar(buf[tokenizer->strpos]));

   /* check if the token is a value */
   hasdot = FALSE;
   hasexp = 0;
   if( isValueChar(buf[tokenizer->strpos], buf[tokenizer->strpos+1], &hasdot, &hasexp) )
   {
      /* read value token */
      tokenlen = 0;
      do
      {
         assert(tokenlen < SCIP_MAXSTRLEN);
         assert(!isDelimChar(buf[tokenizer->strpos]));
         tokenizer->token[tokenlen] = buf[tokenizer->strpos];
         tokenlen++;
         tokenizer->strpos++;
      }
      while( isValueChar(buf[tokenizer->strpos], buf[tokenizer->strpos+1], &hasdot, &hasexp) );
   }
   else
   {
      /* read non-value token */
      tokenlen = 0;
      do
      {
         assert(tokenlen < SCIP_MAXSTRLEN);
         tokenizer->token[tokenlen] = buf[tokenizer->strpos];
         tokenlen++;
         tokenizer->strpos++;
         if( tokenlen == 1 && isTokenChar(tokenizer->token[0]) )
            break;
      }
      while( !isDelimChar(buf[tokenizer->strpos]) && !isTokenChar(buf[tokenizer->strpos]) );

      /* if the token is an equation sense '<', '>', or '=', skip a following '='
       * if the token is an equality token '=' and the next character is a '<' or '>', replace the token by the inequality sense
       * if the token is an ^ and the next character is a '2', read the next character 
       */
      if( tokenlen == 1
         && (tokenizer->token[0] == '<' || tokenizer->token[0] == '>' || tokenizer->token[0] == '=')
         && buf[tokenizer->strpos] == '=' )
      {
         tokenizer->token[tokenlen] = buf[tokenizer->strpos];
         tokenlen++;
         tokenizer->strpos++;
      }
      else if( tokenlen == 1 && tokenizer->token[0] == '=' && (buf[tokenizer->strpos] == '<' || buf[tokenizer->strpos] == '>') )
      {
         tokenlen++;
         tokenizer->token[1] = tokenizer->token[0];
         tokenizer->token[0] = buf[tokenizer->strpos];
         tokenizer->token[1] = buf[tokenizer->strpos];
         tokenizer->strpos++;
      }
      else if( tokenlen == 1 && tokenizer->token[0] == '^' && buf[tokenizer->strpos] == '2' )
      {
         tokenizer->token[1] = buf[tokenizer->strpos];
         tokenlen++;
         tokenizer->strpos++;
      }
   }
   assert(tokenlen < SCIP_MAXSTRLEN);
   tokenizer->token[tokenlen] = '\0';

   SCIPdebugMessage("read token: '%s'\n", tokenizer->token);

   return TRUE;
}

/** translate from one value of infinity to another
 * 
 *  if val is >= infty1, then give infty2, else give val
 */
#define infty2infty(infty1, infty2, val) (val >= infty1 ? infty2 : val)

/* catches variable bound change events on a linear variable in a quadratic constraint */
static
SCIP_RETCODE catchLinearVarEvents(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_EVENTHDLR*       eventhdlr,          /**< event handler */
   SCIP_CONS*            cons,               /**< constraint for which to catch bound change events */
   int                   linvarpos           /**< position of variable in linear variables array */
   )
{
   SCIP_CONSDATA*  consdata;
   SCIP_EVENTDATA* eventdata;
   SCIP_EVENTTYPE  eventtype;
   
   assert(scip != NULL);
   assert(eventhdlr != NULL);
   assert(cons != NULL);
   
   consdata = SCIPconsGetData(cons);
   assert(consdata != NULL);
   
   assert(linvarpos >= 0);
   assert(linvarpos < consdata->nlinvars);
   assert(consdata->lineventdata != NULL);

   SCIP_CALL( SCIPallocBlockMemory(scip, &eventdata) );
   
   eventdata->consdata = consdata;
   eventdata->varidx = linvarpos;
   
   eventtype = SCIP_EVENTTYPE_VARFIXED;
   if( !SCIPisInfinity(scip, consdata->rhs) )
   {
      /* if right hand side is finite, then a tightening in the lower bound of coef*linvar is of interest
       * since we also want to keep activities in consdata uptodate, we also need to know when the corresponding bound is relaxed */
      if( consdata->lincoefs[linvarpos] > 0.0 )
         eventtype |= SCIP_EVENTTYPE_LBCHANGED;
      else
         eventtype |= SCIP_EVENTTYPE_UBCHANGED;
   }
   if( !SCIPisInfinity(scip, -consdata->lhs) )
   {
      /* if left hand side is finite, then a tightening in the upper bound of coef*linvar is of interest
       * since we also want to keep activities in consdata uptodate, we also need to know when the corresponding bound is relaxed */
      if( consdata->lincoefs[linvarpos] > 0.0 )
         eventtype |= SCIP_EVENTTYPE_UBCHANGED;
      else
         eventtype |= SCIP_EVENTTYPE_LBCHANGED;
   }

   SCIP_CALL( SCIPcatchVarEvent(scip, consdata->linvars[linvarpos], eventtype, eventhdlr, eventdata, &eventdata->filterpos) );

   consdata->lineventdata[linvarpos] = eventdata;

   return SCIP_OKAY;
}

/* drops variable bound change events on a linear variable in a quadratic constraint */
static
SCIP_RETCODE dropLinearVarEvents(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_EVENTHDLR*       eventhdlr,          /**< event handler */
   SCIP_CONS*            cons,               /**< constraint for which to catch bound change events */
   int                   linvarpos           /**< position of variable in linear variables array */
   )
{
   SCIP_CONSDATA* consdata;
   SCIP_EVENTTYPE  eventtype;

   assert(scip != NULL);
   assert(eventhdlr != NULL);
   assert(cons != NULL);
   
   consdata = SCIPconsGetData(cons);
   assert(consdata != NULL);
   
   assert(linvarpos >= 0);
   assert(linvarpos < consdata->nlinvars);
   assert(consdata->lineventdata != NULL);
   assert(consdata->lineventdata[linvarpos] != NULL);
   assert(consdata->lineventdata[linvarpos]->consdata == consdata);
   assert(consdata->lineventdata[linvarpos]->varidx == linvarpos);
   assert(consdata->lineventdata[linvarpos]->filterpos >= 0);
   
   eventtype = SCIP_EVENTTYPE_VARFIXED;
   if( !SCIPisInfinity(scip, consdata->rhs) )
   {
      /* if right hand side is finite, then a tightening in the lower bound of coef*linvar is of interest
       * since we also want to keep activities in consdata uptodate, we also need to know when the corresponding bound is relaxed */
      if( consdata->lincoefs[linvarpos] > 0.0 )
         eventtype |= SCIP_EVENTTYPE_LBCHANGED;
      else
         eventtype |= SCIP_EVENTTYPE_UBCHANGED;
   }
   if( !SCIPisInfinity(scip, -consdata->lhs) )
   {
      /* if left hand side is finite, then a tightening in the upper bound of coef*linvar is of interest
       * since we also want to keep activities in consdata uptodate, we also need to know when the corresponding bound is relaxed */
      if( consdata->lincoefs[linvarpos] > 0.0 )
         eventtype |= SCIP_EVENTTYPE_UBCHANGED;
      else
         eventtype |= SCIP_EVENTTYPE_LBCHANGED;
   }

   SCIP_CALL( SCIPdropVarEvent(scip, consdata->linvars[linvarpos], eventtype, eventhdlr, consdata->lineventdata[linvarpos], consdata->lineventdata[linvarpos]->filterpos) );
   
   SCIPfreeBlockMemory(scip, &consdata->lineventdata[linvarpos]);

   return SCIP_OKAY;
}

/* catches variable bound change events on a quadratic variable in a quadratic constraint */
static
SCIP_RETCODE catchQuadVarEvents(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_EVENTHDLR*       eventhdlr,          /**< event handler */
   SCIP_CONS*            cons,               /**< constraint for which to catch bound change events */
   int                   quadvarpos          /**< position of variable in quadratic variables array */
   )
{
   SCIP_CONSDATA* consdata;
   SCIP_EVENTDATA* eventdata;
   
   assert(scip != NULL);
   assert(eventhdlr != NULL);
   assert(cons != NULL);
   
   consdata = SCIPconsGetData(cons);
   assert(consdata != NULL);
   
   assert(quadvarpos >= 0);
   assert(quadvarpos < consdata->nquadvars);
   assert(consdata->quadvarterms[quadvarpos].eventdata == NULL);
   
   SCIP_CALL( SCIPallocBlockMemory(scip, &eventdata) );
   
   eventdata->consdata = consdata;
   eventdata->varidx   = -quadvarpos-1;
   SCIP_CALL( SCIPcatchVarEvent(scip, consdata->quadvarterms[quadvarpos].var, SCIP_EVENTTYPE_BOUNDCHANGED | SCIP_EVENTTYPE_VARFIXED, eventhdlr, eventdata, &eventdata->filterpos) );

   consdata->quadvarterms[quadvarpos].eventdata = eventdata;

   return SCIP_OKAY;
}

/* catches variable bound change events on a quadratic variable in a quadratic constraint */
static
SCIP_RETCODE dropQuadVarEvents(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_EVENTHDLR*       eventhdlr,          /**< event handler */
   SCIP_CONS*            cons,               /**< constraint for which to catch bound change events */
   int                   quadvarpos          /**< position of variable in quadratic variables array */
   )
{
   SCIP_CONSDATA* consdata;
   
   assert(scip != NULL);
   assert(eventhdlr != NULL);
   assert(cons != NULL);
   
   consdata = SCIPconsGetData(cons);
   assert(consdata != NULL);
   
   assert(quadvarpos >= 0);
   assert(quadvarpos < consdata->nquadvars);
   assert(consdata->quadvarterms[quadvarpos].eventdata != NULL);
   assert(consdata->quadvarterms[quadvarpos].eventdata->consdata == consdata);
   assert(consdata->quadvarterms[quadvarpos].eventdata->varidx == -quadvarpos-1);
   assert(consdata->quadvarterms[quadvarpos].eventdata->filterpos >= 0);
   
   SCIP_CALL( SCIPdropVarEvent(scip, consdata->quadvarterms[quadvarpos].var, SCIP_EVENTTYPE_BOUNDCHANGED | SCIP_EVENTTYPE_VARFIXED, eventhdlr, consdata->quadvarterms[quadvarpos].eventdata, consdata->quadvarterms[quadvarpos].eventdata->filterpos) );

   SCIPfreeBlockMemory(scip, &consdata->quadvarterms[quadvarpos].eventdata);

   return SCIP_OKAY;
}

/** catch variable events */
static
SCIP_RETCODE catchVarEvents(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_EVENTHDLR*       eventhdlr,          /**< event handler */
   SCIP_CONS*            cons                /**< constraint for which to catch bound change events */      
   )
{
   SCIP_CONSDATA* consdata;
   int i;
   
   assert(scip != NULL);
   assert(cons != NULL);
   assert(eventhdlr != NULL);

   consdata = SCIPconsGetData(cons);
   assert(consdata != NULL);
   assert(consdata->lineventdata == NULL);

   /* we will update isremovedfixings, so reset it to TRUE first */
   consdata->isremovedfixings = TRUE;
   
   SCIP_CALL( SCIPallocBlockMemoryArray(scip, &consdata->lineventdata, consdata->linvarssize) );
   for( i = 0; i < consdata->nlinvars; ++i )
   {
      SCIP_CALL( catchLinearVarEvents(scip, eventhdlr, cons, i) );
      
      consdata->isremovedfixings = consdata->isremovedfixings && SCIPvarIsActive(consdata->linvars[i]);
   }
   
   for( i = 0; i < consdata->nquadvars; ++i )
   {
      assert(consdata->quadvarterms[i].eventdata == NULL);

      SCIP_CALL( catchQuadVarEvents(scip, eventhdlr, cons, i) );
      
      consdata->isremovedfixings = consdata->isremovedfixings && SCIPvarIsActive(consdata->quadvarterms[i].var);
   }
   
   consdata->ispropagated = FALSE;

   return SCIP_OKAY;
}

/** drop variable events */
static
SCIP_RETCODE dropVarEvents(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_EVENTHDLR*       eventhdlr,          /**< event handler */
   SCIP_CONS*            cons                /**< constraint for which to drop bound change events */
   )
{
   SCIP_CONSDATA* consdata;
   int i;
   
   assert(scip != NULL);
   assert(eventhdlr != NULL);
   assert(cons != NULL);

   consdata = SCIPconsGetData(cons);
   assert(consdata != NULL);

   if( consdata->lineventdata != NULL )
   {
      for( i = 0; i < consdata->nlinvars; ++i )
      {
         if( consdata->lineventdata[i] != NULL )
         {
            SCIP_CALL( dropLinearVarEvents(scip, eventhdlr, cons, i) );
         }
      }
      SCIPfreeBlockMemoryArray(scip, &consdata->lineventdata, consdata->linvarssize);
   }
   
   for( i = 0; i < consdata->nquadvars; ++i )
   {
      if( consdata->quadvarterms[i].eventdata != NULL )
      {
         SCIP_CALL( dropQuadVarEvents(scip, eventhdlr, cons, i) );
      }
   }

   return SCIP_OKAY;
}

/** locks a linear variable in a constraint */
static
SCIP_RETCODE lockLinearVariable(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONS*            cons,               /**< constraint where to lock a variable */
   SCIP_VAR*             var,                /**< variable to lock */
   SCIP_Real             coef                /**< coefficient of variable in constraint */
   )
{
   SCIP_CONSDATA* consdata;
   
   assert(scip != NULL);
   assert(cons != NULL);
   assert(var != NULL);
   assert(coef != 0.0);

   consdata = SCIPconsGetData(cons);
   assert(consdata != NULL);

   if( coef > 0.0 )
   {
      SCIP_CALL( SCIPlockVarCons(scip, var, cons, !SCIPisInfinity(scip, -consdata->lhs), !SCIPisInfinity(scip,  consdata->rhs)) );
   }
   else
   {
      SCIP_CALL( SCIPlockVarCons(scip, var, cons, !SCIPisInfinity(scip,  consdata->rhs), !SCIPisInfinity(scip, -consdata->lhs)) );
   }

   return SCIP_OKAY;
}

/** unlocks a linear variable in a constraint */
static
SCIP_RETCODE unlockLinearVariable(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONS*            cons,               /**< constraint where to unlock a variable */
   SCIP_VAR*             var,                /**< variable to unlock */
   SCIP_Real             coef                /**< coefficient of variable in constraint */
   )
{
   SCIP_CONSDATA* consdata;
   
   assert(scip != NULL);
   assert(cons != NULL);
   assert(var != NULL);
   assert(coef != 0.0);

   consdata = SCIPconsGetData(cons);
   assert(consdata != NULL);

   if( coef > 0.0 )
   {
      SCIP_CALL( SCIPunlockVarCons(scip, var, cons, !SCIPisInfinity(scip, -consdata->lhs), !SCIPisInfinity(scip,  consdata->rhs)) );
   }
   else
   {
      SCIP_CALL( SCIPunlockVarCons(scip, var, cons, !SCIPisInfinity(scip,  consdata->rhs), !SCIPisInfinity(scip, -consdata->lhs)) );
   }

   return SCIP_OKAY;
}

/** locks a quadratic variable in a constraint */
static
SCIP_RETCODE lockQuadraticVariable(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONS*            cons,               /**< constraint where to lock a variable */
   SCIP_VAR*             var                 /**< variable to lock */
   )
{
   SCIP_CALL( SCIPlockVarCons(scip, var, cons, TRUE, TRUE) );

   return SCIP_OKAY;
}

/** unlocks a quadratic variable in a constraint */
static
SCIP_RETCODE unlockQuadraticVariable(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONS*            cons,               /**< constraint where to unlock a variable */
   SCIP_VAR*             var                 /**< variable to unlock */
   )
{
   SCIP_CALL( SCIPunlockVarCons(scip, var, cons, TRUE, TRUE) );

   return SCIP_OKAY;
}

/** computes the minimal and maximal activity for the linear part in a constraint data
 *  only sums up terms that contribute finite values
 *  gives the number of terms that contribute infinite values
 *  only computes those activities where the corresponding side of the constraint is finite
 */
static
void consdataUpdateLinearActivity(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONSDATA*        consdata,           /**< constraint data */
   SCIP_Real             intervalinfty       /**< infinity value used in interval operations */
   )
{  /*lint --e{666}*/
   SCIP_ROUNDMODE prevroundmode;
   int       i;
   SCIP_Real bnd;

   assert(scip != NULL);
   assert(consdata != NULL);

   /* if variable bounds are not strictly consistent, then the activity update methods may yield inconsistent activities
    * in this case, we also recompute the activities
    */
   if( consdata->minlinactivity != SCIP_INVALID && consdata->maxlinactivity != SCIP_INVALID &&
       (consdata->minlinactivityinf > 0 || consdata->maxlinactivityinf > 0 || consdata->minlinactivity <= consdata->maxlinactivity) )
   {
      /* activities should be uptodate */
      assert(consdata->minlinactivityinf >= 0);
      assert(consdata->maxlinactivityinf >= 0);
      return;
   }

   consdata->minlinactivityinf = 0;
   consdata->maxlinactivityinf = 0;

   /* if lhs is -infinite, then we do not compute a maximal activity, so we set it to  infinity
    * if rhs is  infinite, then we do not compute a minimal activity, so we set it to -infinity
    */
   consdata->minlinactivity = SCIPisInfinity(scip,  consdata->rhs) ? -intervalinfty : 0.0;
   consdata->maxlinactivity = SCIPisInfinity(scip, -consdata->lhs) ?  intervalinfty : 0.0;

   if( consdata->nlinvars == 0 )
      return;

   /* if the activities computed here should be still uptodate after boundchanges,
    * variable events need to be catched */
   assert(consdata->lineventdata != NULL);

   prevroundmode = SCIPintervalGetRoundingMode();

   if( !SCIPisInfinity(scip,  consdata->rhs) )
   {
      /* compute minimal activity only if there is a finite right hand side */
      SCIPintervalSetRoundingModeDownwards();

      for( i = 0; i < consdata->nlinvars; ++i )
      {
         assert(consdata->lineventdata[i] != NULL);
         if( consdata->lincoefs[i] >= 0.0 )
         {
            bnd = MIN(SCIPvarGetLbLocal(consdata->linvars[i]), SCIPvarGetUbLocal(consdata->linvars[i]));
            if( SCIPisInfinity(scip, -bnd) )
            {
               ++consdata->minlinactivityinf;
               continue;
            }
            assert(!SCIPisInfinity(scip, bnd)); /* do not like variables that are fixed at +infinity */
         }
         else
         {
            bnd = MAX(SCIPvarGetLbLocal(consdata->linvars[i]), SCIPvarGetUbLocal(consdata->linvars[i]));
            if( SCIPisInfinity(scip,  bnd) )
            {
               ++consdata->minlinactivityinf;
               continue;
            }
            assert(!SCIPisInfinity(scip, -bnd)); /* do not like variables that are fixed at -infinity */
         }
         consdata->minlinactivity += consdata->lincoefs[i] * bnd;
      }
   }

   if( !SCIPisInfinity(scip, -consdata->lhs) )
   {
      /* compute maximal activity only if there is a finite left hand side */
      SCIPintervalSetRoundingModeUpwards();

      for( i = 0; i < consdata->nlinvars; ++i )
      {
         assert(consdata->lineventdata[i] != NULL);
         if( consdata->lincoefs[i] >= 0.0 )
         {
            bnd = MAX(SCIPvarGetLbLocal(consdata->linvars[i]), SCIPvarGetUbLocal(consdata->linvars[i]));
            if( SCIPisInfinity(scip,  bnd) )
            {
               ++consdata->maxlinactivityinf;
               continue;
            }
            assert(!SCIPisInfinity(scip, -bnd)); /* do not like variables that are fixed at -infinity */
         }
         else
         {
            bnd = MIN(SCIPvarGetLbLocal(consdata->linvars[i]), SCIPvarGetUbLocal(consdata->linvars[i]));
            if( SCIPisInfinity(scip, -bnd) )
            {
               ++consdata->maxlinactivityinf;
               continue;
            }
            assert(!SCIPisInfinity(scip, bnd)); /* do not like variables that are fixed at +infinity */
         }
         consdata->maxlinactivity += consdata->lincoefs[i] * bnd;
      }
   }

   SCIPintervalSetRoundingMode(prevroundmode);

   assert(consdata->minlinactivityinf > 0 || consdata->maxlinactivityinf > 0 || consdata->minlinactivity <= consdata->maxlinactivity);
}

/** update the linear activities after a change in the lower bound of a variable */
static
void consdataUpdateLinearActivityLbChange(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONSDATA*        consdata,           /**< constraint data */
   SCIP_Real             coef,               /**< coefficient of variable in constraint */
   SCIP_Real             oldbnd,             /**< previous lower bound of variable */
   SCIP_Real             newbnd              /**< new lower bound of variable */
   )
{
   SCIP_ROUNDMODE prevroundmode;

   assert(scip != NULL);
   assert(consdata != NULL);
   /* we can't deal with lower bounds at infinity */
   assert(!SCIPisInfinity(scip, oldbnd));
   assert(!SCIPisInfinity(scip, newbnd));

   /* @todo since we check the linear activity for consistency later anyway, we may skip changing the rounding mode here */

   /* assume lhs <= a*x + y <= rhs, then the following boundchanges can be deduced:
    * a > 0:  y <= rhs - a*lb(x),  y >= lhs - a*ub(x)
    * a < 0:  y <= rhs - a*ub(x),  y >= lhs - a*lb(x)
    */

   if( coef > 0.0 )
   {
      /* we should only be called if rhs is finite */
      assert(!SCIPisInfinity(scip, consdata->rhs));

      /* we have no min activities computed so far, so cannot update */
      if( consdata->minlinactivity == SCIP_INVALID )
         return;

      assert(!SCIPisInfinity(scip, -consdata->minlinactivity));

      prevroundmode = SCIPintervalGetRoundingMode();
      SCIPintervalSetRoundingModeDownwards();

      /* update min activity */
      if( SCIPisInfinity(scip, -oldbnd) )
      {
         --consdata->minlinactivityinf;
         assert(consdata->minlinactivityinf >= 0);
      }
      else
      {
         SCIP_Real minuscoef;
         minuscoef = -coef;
         consdata->minlinactivity += minuscoef * oldbnd;
      }

      if( SCIPisInfinity(scip, -newbnd) )
      {
         ++consdata->minlinactivityinf;
      }
      else
      {
         consdata->minlinactivity += coef * newbnd;
      }

      SCIPintervalSetRoundingMode(prevroundmode);
   }
   else
   {
      /* we should only be called if lhs is finite */
      assert(!SCIPisInfinity(scip, -consdata->lhs));

      /* we have no max activities computed so far, so cannot update */
      if( consdata->maxlinactivity == SCIP_INVALID )
         return;

      assert(!SCIPisInfinity(scip, consdata->maxlinactivity));

      prevroundmode = SCIPintervalGetRoundingMode();
      SCIPintervalSetRoundingModeUpwards();

      /* update max activity */
      if( SCIPisInfinity(scip, -oldbnd) )
      {
         --consdata->maxlinactivityinf;
         assert(consdata->maxlinactivityinf >= 0);
      }
      else
      {
         SCIP_Real minuscoef;
         minuscoef = -coef;
         consdata->maxlinactivity += minuscoef * oldbnd;
      }

      if( SCIPisInfinity(scip, -newbnd) )
      {
         ++consdata->maxlinactivityinf;
      }
      else
      {
         consdata->maxlinactivity += coef * newbnd;
      }

      SCIPintervalSetRoundingMode(prevroundmode);
   }
}

/** update the linear activities after a change in the upper bound of a variable */
static
void consdataUpdateLinearActivityUbChange(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONSDATA*        consdata,           /**< constraint data */
   SCIP_Real             coef,               /**< coefficient of variable in constraint */
   SCIP_Real             oldbnd,             /**< previous lower bound of variable */
   SCIP_Real             newbnd              /**< new lower bound of variable */
   )
{
   SCIP_ROUNDMODE prevroundmode;

   assert(scip != NULL);
   assert(consdata != NULL);
   /* we can't deal with upper bounds at -infinity */
   assert(!SCIPisInfinity(scip, -oldbnd));
   assert(!SCIPisInfinity(scip, -newbnd));

   /* @todo since we check the linear activity for consistency later anyway, we may skip changing the rounding mode here */

   /* assume lhs <= a*x + y <= rhs, then the following boundchanges can be deduced:
    * a > 0:  y <= rhs - a*lb(x),  y >= lhs - a*ub(x)
    * a < 0:  y <= rhs - a*ub(x),  y >= lhs - a*lb(x)
    */

   if( coef > 0.0 )
   {
      /* we should only be called if lhs is finite */
      assert(!SCIPisInfinity(scip, -consdata->lhs));

      /* we have no max activities computed so far, so cannot update */
      if( consdata->maxlinactivity == SCIP_INVALID )
         return;

      assert(!SCIPisInfinity(scip, consdata->maxlinactivity));

      prevroundmode = SCIPintervalGetRoundingMode();
      SCIPintervalSetRoundingModeUpwards();

      /* update max activity */
      if( SCIPisInfinity(scip, oldbnd) )
      {
         --consdata->maxlinactivityinf;
         assert(consdata->maxlinactivityinf >= 0);
      }
      else
      {
         SCIP_Real minuscoef;
         minuscoef = -coef;
         consdata->maxlinactivity += minuscoef * oldbnd;
      }

      if( SCIPisInfinity(scip, newbnd) )
      {
         ++consdata->maxlinactivityinf;
      }
      else
      {
         consdata->maxlinactivity += coef * newbnd;
      }

      SCIPintervalSetRoundingMode(prevroundmode);
   }
   else
   {
      /* we should only be called if rhs is finite */
      assert(!SCIPisInfinity(scip, consdata->rhs));

      /* we have no min activities computed so far, so cannot update */
      if( consdata->minlinactivity == SCIP_INVALID )
         return;

      assert(!SCIPisInfinity(scip, -consdata->minlinactivity));

      prevroundmode = SCIPintervalGetRoundingMode();
      SCIPintervalSetRoundingModeDownwards();

      /* update min activity */
      if( SCIPisInfinity(scip, oldbnd) )
      {
         --consdata->minlinactivityinf;
         assert(consdata->minlinactivityinf >= 0);
      }
      else
      {
         SCIP_Real minuscoef;
         minuscoef = -coef;
         consdata->minlinactivity += minuscoef * oldbnd;
      }

      if( SCIPisInfinity(scip, newbnd) )
      {
         ++consdata->minlinactivityinf;
      }
      else
      {
         consdata->minlinactivity += coef * newbnd;
      }

      SCIPintervalSetRoundingMode(prevroundmode);
   }
}

/** processes variable fixing or bound change event */
static
SCIP_DECL_EVENTEXEC(processVarEvent)
{
   SCIP_CONSDATA* consdata;
   SCIP_EVENTTYPE eventtype;

   assert(scip != NULL);
   assert(event != NULL);
   assert(eventdata != NULL);
   assert(eventhdlr != NULL);

   consdata = eventdata->consdata;
   assert(consdata != NULL);
   assert(eventdata->varidx <  0 ||  eventdata->varidx   < consdata->nlinvars);
   assert(eventdata->varidx >= 0 || -eventdata->varidx-1 < consdata->nquadvars);

   eventtype = SCIPeventGetType(event);

   if( eventtype & SCIP_EVENTTYPE_VARFIXED )
   {
      consdata->isremovedfixings = FALSE;
   }

   if( eventtype & SCIP_EVENTTYPE_BOUNDCHANGED )
   {
      if( eventdata->varidx < 0 )
      {
         /* mark activity bounds for this quad var term variable as not up to date anymore */
         SCIPintervalSetEmpty(&consdata->quadactivitybounds);
      }
      else
      {
         /* update activity bounds for linear terms */
         if( eventtype & SCIP_EVENTTYPE_LBCHANGED )
            consdataUpdateLinearActivityLbChange(scip, consdata, consdata->lincoefs[eventdata->varidx], SCIPeventGetOldbound(event), SCIPeventGetNewbound(event));
         else
            consdataUpdateLinearActivityUbChange(scip, consdata, consdata->lincoefs[eventdata->varidx], SCIPeventGetOldbound(event), SCIPeventGetNewbound(event));
      }

      if( eventtype & SCIP_EVENTTYPE_BOUNDTIGHTENED )
         consdata->ispropagated = FALSE;
   }

   return SCIP_OKAY;
}

/** ensures, that linear vars and coefs arrays can store at least num entries */
static
SCIP_RETCODE consdataEnsureLinearVarsSize(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONSDATA*        consdata,           /**< quadratic constraint data */
   int                   num                 /**< minimum number of entries to store */
   )
{
   assert(scip != NULL);
   assert(consdata != NULL);
   assert(consdata->nlinvars <= consdata->linvarssize);

   if( num > consdata->linvarssize )
   {
      int newsize;

      newsize = SCIPcalcMemGrowSize(scip, num);
      SCIP_CALL( SCIPreallocBlockMemoryArray(scip, &consdata->linvars,  consdata->linvarssize, newsize) );
      SCIP_CALL( SCIPreallocBlockMemoryArray(scip, &consdata->lincoefs, consdata->linvarssize, newsize) );
      if( consdata->lineventdata != NULL )
      {
         SCIP_CALL( SCIPreallocBlockMemoryArray(scip, &consdata->lineventdata, consdata->linvarssize, newsize) );
      }
      consdata->linvarssize = newsize;
   }
   assert(num <= consdata->linvarssize);

   return SCIP_OKAY;
}

/** ensures, that quadratic variable terms array can store at least num entries */
static
SCIP_RETCODE consdataEnsureQuadVarTermsSize(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONSDATA*        consdata,           /**< quadratic constraint data */
   int                   num                 /**< minimum number of entries to store */
   )
{
   assert(scip != NULL);
   assert(consdata != NULL);
   assert(consdata->nquadvars <= consdata->quadvarssize);

   if( num > consdata->quadvarssize )
   {
      int newsize;

      newsize = SCIPcalcMemGrowSize(scip, num);
      SCIP_CALL( SCIPreallocBlockMemoryArray(scip, &consdata->quadvarterms, consdata->quadvarssize, newsize) );
      consdata->quadvarssize = newsize;
   }
   assert(num <= consdata->quadvarssize);

   return SCIP_OKAY;
}

/** ensures, that adjaceny array array can store at least num entries */
static
SCIP_RETCODE consdataEnsureAdjBilinSize(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_QUADVARTERM*     quadvarterm,        /**< quadratic variable term */
   int                   num                 /**< minimum number of entries to store */
   )
{
   assert(scip != NULL);
   assert(quadvarterm != NULL);
   assert(quadvarterm->nadjbilin <= quadvarterm->adjbilinsize);

   if( num > quadvarterm->adjbilinsize )
   {
      int newsize;

      newsize = SCIPcalcMemGrowSize(scip, num);
      SCIP_CALL( SCIPreallocBlockMemoryArray(scip, &quadvarterm->adjbilin, quadvarterm->adjbilinsize, newsize) );
      quadvarterm->adjbilinsize = newsize;
   }
   assert(num <= quadvarterm->adjbilinsize);

   return SCIP_OKAY;
}

/** ensures, that bilinear term arrays can store at least num entries */
static
SCIP_RETCODE consdataEnsureBilinSize(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONSDATA*        consdata,           /**< quadratic constraint data */
   int                   num                 /**< minimum number of entries to store */
   )
{
   assert(scip != NULL);
   assert(consdata != NULL);
   assert(consdata->nbilinterms <= consdata->bilintermssize);

   if( num > consdata->bilintermssize )
   {
      int newsize;

      newsize = SCIPcalcMemGrowSize(scip, num);
      SCIP_CALL( SCIPreallocBlockMemoryArray(scip, &consdata->bilinterms, consdata->bilintermssize, newsize) );
      consdata->bilintermssize = newsize;
   }
   assert(num <= consdata->bilintermssize);

   return SCIP_OKAY;
}


/** creates empty constraint data structure */
static
SCIP_RETCODE consdataCreateEmpty(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONSDATA**       consdata            /**< a buffer to store pointer to new constraint data */
   )
{
   assert(scip != NULL);
   assert(consdata != NULL);

   SCIP_CALL( SCIPallocBlockMemory(scip, consdata) );
   BMSclearMemory(*consdata);

   (*consdata)->lhs = -SCIPinfinity(scip);
   (*consdata)->rhs =  SCIPinfinity(scip);

   (*consdata)->linvarssorted  = TRUE;
   (*consdata)->linvarsmerged  = TRUE;
   (*consdata)->quadvarssorted = TRUE;
   (*consdata)->quadvarsmerged = TRUE;
   (*consdata)->bilinsorted    = TRUE;
   (*consdata)->bilinmerged    = TRUE;

   (*consdata)->isremovedfixings = TRUE;
   (*consdata)->ispropagated     = TRUE;

   (*consdata)->linvar_maydecrease = -1;
   (*consdata)->linvar_mayincrease = -1;

   (*consdata)->minlinactivity = SCIP_INVALID;
   (*consdata)->maxlinactivity = SCIP_INVALID;
   (*consdata)->minlinactivityinf = -1;
   (*consdata)->maxlinactivityinf = -1;

   return SCIP_OKAY;
}

/** creates constraint data structure */
static
SCIP_RETCODE consdataCreate(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONSDATA**       consdata,           /**< a buffer to store pointer to new constraint data */
   SCIP_Real             lhs,                /**< left hand side of constraint */
   SCIP_Real             rhs,                /**< right hand side of constraint */
   int                   nlinvars,           /**< number of linear variables */
   SCIP_VAR**            linvars,            /**< array of linear variables */
   SCIP_Real*            lincoefs,           /**< array of coefficients of linear variables */
   int                   nquadvars,          /**< number of quadratic variables */
   SCIP_QUADVARTERM*     quadvarterms,       /**< array of quadratic variable terms */
   int                   nbilinterms,        /**< number of bilinear terms */
   SCIP_BILINTERM*       bilinterms,         /**< array of bilinear terms */
   SCIP_Bool             capturevars         /**< whether we should capture variables */
   )
{
   int i;

   assert(scip != NULL);
   assert(consdata != NULL);
   
   assert(nlinvars == 0 || linvars  != NULL);
   assert(nlinvars == 0 || lincoefs != NULL);
   assert(nquadvars == 0 || quadvarterms != NULL);
   assert(nbilinterms == 0 || bilinterms != NULL);

   SCIP_CALL( SCIPallocBlockMemory(scip, consdata) );
   BMSclearMemory(*consdata);

   (*consdata)->minlinactivity = SCIP_INVALID;
   (*consdata)->maxlinactivity = SCIP_INVALID;
   (*consdata)->minlinactivityinf = -1;
   (*consdata)->maxlinactivityinf = -1;

   (*consdata)->lhs = lhs;
   (*consdata)->rhs = rhs;
   
   if( nlinvars > 0 )
   {
      SCIP_CALL( SCIPduplicateBlockMemoryArray(scip, &(*consdata)->linvars,  linvars,  nlinvars) );
      SCIP_CALL( SCIPduplicateBlockMemoryArray(scip, &(*consdata)->lincoefs, lincoefs, nlinvars) );
      (*consdata)->nlinvars = nlinvars;
      (*consdata)->linvarssize = nlinvars;
      
      if( capturevars )
         for( i = 0; i < nlinvars; ++i )
         {
            SCIP_CALL( SCIPcaptureVar(scip, linvars[i]) );
         }
   }
   else
   {
      (*consdata)->linvarssorted = TRUE;
      (*consdata)->linvarsmerged = TRUE;
      (*consdata)->minlinactivity = 0.0;
      (*consdata)->maxlinactivity = 0.0;
      (*consdata)->minlinactivityinf = 0;
      (*consdata)->maxlinactivityinf = 0;
   }
   
   if( nquadvars > 0 )
   {
      SCIP_CALL( SCIPduplicateBlockMemoryArray(scip, &(*consdata)->quadvarterms, quadvarterms, nquadvars) );
      
      for( i = 0; i < nquadvars; ++i )
      {
         (*consdata)->quadvarterms[i].eventdata = NULL;
         if( quadvarterms[i].nadjbilin )
         {
            SCIP_CALL( SCIPduplicateBlockMemoryArray(scip, &(*consdata)->quadvarterms[i].adjbilin, quadvarterms[i].adjbilin, quadvarterms[i].nadjbilin) );
            (*consdata)->quadvarterms[i].adjbilinsize = quadvarterms[i].nadjbilin;
         }
         else
         {
            assert((*consdata)->quadvarterms[i].nadjbilin == 0);
            (*consdata)->quadvarterms[i].adjbilin = NULL;
            (*consdata)->quadvarterms[i].adjbilinsize = 0;
         }
         if( capturevars )
         {
            SCIP_CALL( SCIPcaptureVar(scip, quadvarterms[i].var) );
         }
      }
      
      (*consdata)->nquadvars = nquadvars;
      (*consdata)->quadvarssize = nquadvars;
      SCIPintervalSetEmpty(&(*consdata)->quadactivitybounds);
   }
   else
   {
      (*consdata)->quadvarssorted = TRUE;
      (*consdata)->quadvarsmerged = TRUE;
      SCIPintervalSet(&(*consdata)->quadactivitybounds, 0.0);
   }
   
   if( nbilinterms > 0 )
   {
      SCIP_CALL( SCIPduplicateBlockMemoryArray(scip, &(*consdata)->bilinterms, bilinterms, nbilinterms) );
      (*consdata)->nbilinterms = nbilinterms;
      (*consdata)->bilintermssize = nbilinterms;
   }
   else
   {
      (*consdata)->bilinsorted = TRUE;
      (*consdata)->bilinmerged = TRUE;
   }

   (*consdata)->linvar_maydecrease = -1;
   (*consdata)->linvar_mayincrease = -1;
   
   (*consdata)->activity = SCIP_INVALID;
   (*consdata)->lhsviol  = SCIPisInfinity(scip, -lhs) ? 0.0 : SCIP_INVALID;
   (*consdata)->rhsviol  = SCIPisInfinity(scip,  rhs) ? 0.0 : SCIP_INVALID;

   return SCIP_OKAY;
}

/** frees constraint data structure */
static
SCIP_RETCODE consdataFree(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONSDATA**       consdata            /**< pointer to constraint data to free */
   )
{
   int i;

   assert(scip != NULL);
   assert(consdata != NULL);
   assert(*consdata != NULL);

   /* release linear variables and free linear part */
   if( (*consdata)->linvarssize > 0 )
   {
      for( i = 0; i < (*consdata)->nlinvars; ++i )
      {
         assert((*consdata)->lineventdata == NULL || (*consdata)->lineventdata[i] == NULL); /* variable events should have been dropped earlier */
         SCIP_CALL( SCIPreleaseVar(scip, &(*consdata)->linvars[i]) );
      }
      SCIPfreeBlockMemoryArray(scip, &(*consdata)->linvars,  (*consdata)->linvarssize);
      SCIPfreeBlockMemoryArray(scip, &(*consdata)->lincoefs, (*consdata)->linvarssize);
      SCIPfreeBlockMemoryArrayNull(scip, &(*consdata)->lineventdata, (*consdata)->linvarssize);
   }
   assert((*consdata)->linvars == NULL);
   assert((*consdata)->lincoefs == NULL);
   assert((*consdata)->lineventdata == NULL);

   /* release quadratic variables and free quadratic variable term part */
   for( i = 0; i < (*consdata)->nquadvars; ++i )
   {
      assert((*consdata)->quadvarterms[i].eventdata == NULL); /* variable events should have been dropped earlier */
      SCIPfreeBlockMemoryArrayNull(scip, &(*consdata)->quadvarterms[i].adjbilin, (*consdata)->quadvarterms[i].adjbilinsize);
      SCIP_CALL( SCIPreleaseVar(scip, &(*consdata)->quadvarterms[i].var) );
   }
   SCIPfreeBlockMemoryArrayNull(scip, &(*consdata)->quadvarterms, (*consdata)->quadvarssize);

   /* free bilinear terms */
   SCIPfreeBlockMemoryArrayNull(scip, &(*consdata)->bilinterms, (*consdata)->bilintermssize);

   /* free nonlinear row representation */
   if( (*consdata)->nlrow != NULL )
   {
      SCIP_CALL( SCIPreleaseNlRow(scip, &(*consdata)->nlrow) );
   }

   SCIPfreeBlockMemory(scip, consdata);
   *consdata = NULL;
   
   return SCIP_OKAY;
}

/** sorts linear part of constraint data */
static
void consdataSortLinearVars(
   SCIP_CONSDATA*        consdata            /**< quadratic constraint data */
   )
{
   assert(consdata != NULL);

   if( consdata->linvarssorted )
      return;

   if( consdata->nlinvars <= 1 )
   {
      consdata->linvarssorted = TRUE;
      return;
   }

   if( consdata->lineventdata == NULL )
   {
      SCIPsortPtrReal((void**)consdata->linvars, consdata->lincoefs, SCIPvarComp, consdata->nlinvars);
   }
   else
   {
      int i;
      
      SCIPsortPtrPtrReal((void**)consdata->linvars, (void**)consdata->lineventdata, consdata->lincoefs, SCIPvarComp, consdata->nlinvars);
      
      /* update variable indices in event data */
      for( i = 0; i < consdata->nlinvars; ++i )
         if( consdata->lineventdata[i] != NULL )
            consdata->lineventdata[i]->varidx = i;
   }
   
   consdata->linvarssorted = TRUE;
}

#if 0 /* noone needs this routine currently */
/** returns the position of variable in the linear coefficients array of a constraint, or -1 if not found */
static
int consdataFindLinearVar(
   SCIP_CONSDATA*        consdata,           /**< quadratic constraint data */
   SCIP_VAR*             var                 /**< variable to search for */
   )
{
   int pos;
   
   assert(consdata != NULL);
   assert(var != NULL);

   if( consdata->nlinvars == 0 )
      return -1;
   
   consdataSortLinearVars(consdata);
   
   if( !SCIPsortedvecFindPtr((void**)consdata->linvars, SCIPvarComp, (void*)var, consdata->nlinvars, &pos) )
      pos = -1;
   
   return pos;
}
#endif

/** index comparison method for quadratic variable terms: compares two indices of the quadratic variable set in the quadratic constraint */
static
SCIP_DECL_SORTINDCOMP(quadVarTermComp)
{  /*lint --e{715}*/
   SCIP_CONSDATA* consdata = (SCIP_CONSDATA*)dataptr;

   assert(consdata != NULL);
   assert(0 <= ind1 && ind1 < consdata->nquadvars);
   assert(0 <= ind2 && ind2 < consdata->nquadvars);

   return SCIPvarCompare(consdata->quadvarterms[ind1].var, consdata->quadvarterms[ind2].var);
}

/** sorting of quadratic variable terms */
static
SCIP_RETCODE consdataSortQuadVarTerms(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONSDATA*        consdata            /**< quadratic constraint data */
   )
{
   int* perm;
   int  i;
   int  nexti;
   int  v;
   SCIP_QUADVARTERM quadterm;

   assert(scip != NULL);
   assert(consdata != NULL);

   if( consdata->quadvarssorted )
      return SCIP_OKAY;

   if( consdata->nquadvars == 0 )
   {
      consdata->quadvarssorted = TRUE;
      return SCIP_OKAY;
   }

   /* get temporary memory to store the sorted permutation */
   SCIP_CALL( SCIPallocBufferArray(scip, &perm, consdata->nquadvars) );

   /* call bubble sort */
   SCIPsort(perm, quadVarTermComp, (void*)consdata, consdata->nquadvars);

   /* permute the quadratic variable terms according to the resulting permutation */
   for( v = 0; v < consdata->nquadvars; ++v )
   {
      if( perm[v] != v )
      {
         quadterm = consdata->quadvarterms[v];

         i = v;
         do
         {
            assert(0 <= perm[i] && perm[i] < consdata->nquadvars);
            assert(perm[i] != i);
            consdata->quadvarterms[i] = consdata->quadvarterms[perm[i]];
            if( consdata->quadvarterms[i].eventdata != NULL )
            {
               consdata->quadvarterms[i].eventdata->varidx = -i-1;
            }
            nexti = perm[i];
            perm[i] = i;
            i = nexti;
         }
         while( perm[i] != v );
         consdata->quadvarterms[i] = quadterm;
         if( consdata->quadvarterms[i].eventdata != NULL )
         {
            consdata->quadvarterms[i].eventdata->varidx = -i-1;
         }
         perm[i] = i;
      }
   }
   consdata->quadvarssorted = TRUE;

   /* free temporary memory */
   SCIPfreeBufferArray(scip, &perm);
   
   return SCIP_OKAY;
}

/** returns the position of variable in the quadratic variable terms array of a constraint, or -1 if not found */
static
SCIP_RETCODE consdataFindQuadVarTerm(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONSDATA*        consdata,           /**< quadratic constraint data */
   SCIP_VAR*             var,                /**< variable to search for */
   int*                  pos                 /**< buffer where to store position of var in quadvarterms array, or -1 if not found */
   )
{
   int left;
   int right;
   int cmpres;

   assert(consdata != NULL);
   assert(var != NULL);
   assert(pos != NULL);
   
   if( consdata->nquadvars == 0 )
   {
      *pos = -1;
      return SCIP_OKAY;
   }
   
   SCIP_CALL( consdataSortQuadVarTerms(scip, consdata) );
   
   left = 0;
   right = consdata->nquadvars - 1;
   while( left <= right )
   {
      int middle;

      middle = (left+right)/2;
      assert(0 <= middle && middle < consdata->nquadvars);
      
      cmpres = SCIPvarCompare(var, consdata->quadvarterms[middle].var);

      if( cmpres < 0 )
         right = middle - 1;
      else if( cmpres > 0 )
         left  = middle + 1;
      else
      {
         *pos = middle;
         return SCIP_OKAY;
      }
   }
   assert(left == right+1);

   *pos = -1;
   
   return SCIP_OKAY;
}

/** index comparison method for bilinear terms: compares two index pairs of the bilinear term set in the quadratic constraint */
static
SCIP_DECL_SORTINDCOMP(bilinTermComp)
{  /*lint --e{715}*/
   SCIP_CONSDATA* consdata = (SCIP_CONSDATA*)dataptr;
   int var1cmp;

   assert(consdata != NULL);
   assert(0 <= ind1 && ind1 < consdata->nbilinterms);
   assert(0 <= ind2 && ind2 < consdata->nbilinterms);

   var1cmp = SCIPvarCompare(consdata->bilinterms[ind1].var1, consdata->bilinterms[ind2].var1);
   if( var1cmp != 0 )
      return var1cmp;

   return SCIPvarCompare(consdata->bilinterms[ind1].var2, consdata->bilinterms[ind2].var2);
}

/** sorting of bilinear terms */
static
SCIP_RETCODE consdataSortBilinTerms(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONSDATA*        consdata            /**< quadratic constraint data */
   )
{
   int* perm;
   int* invperm;
   int  i;
   int  nexti;
   int  v;
   SCIP_BILINTERM bilinterm;

   assert(scip != NULL);
   assert(consdata != NULL);

   if( consdata->bilinsorted )
      return SCIP_OKAY;

   if( consdata->nbilinterms == 0 )
   {
      consdata->bilinsorted = TRUE;
      return SCIP_OKAY;
   }

   /* get temporary memory to store the sorted permutation and the inverse permutation */
   SCIP_CALL( SCIPallocBufferArray(scip, &perm,    consdata->nbilinterms) );
   SCIP_CALL( SCIPallocBufferArray(scip, &invperm, consdata->nbilinterms) );

   /* call bubble sort */
   SCIPsort(perm, bilinTermComp, (void*)consdata, consdata->nbilinterms);

   /* compute inverted permutation */
   for( v = 0; v < consdata->nbilinterms; ++v )
   {
      assert(0 <= perm[v] && perm[v] < consdata->nbilinterms);
      invperm[perm[v]] = v;
   }

   /* permute the bilinear terms according to the resulting permutation */
   for( v = 0; v < consdata->nbilinterms; ++v )
   {
      if( perm[v] != v )
      {
         bilinterm = consdata->bilinterms[v];

         i = v;
         do
         {
            assert(0 <= perm[i] && perm[i] < consdata->nbilinterms);
            assert(perm[i] != i);
            consdata->bilinterms[i] = consdata->bilinterms[perm[i]];
            nexti = perm[i];
            perm[i] = i;
            i = nexti;
         }
         while( perm[i] != v );
         consdata->bilinterms[i] = bilinterm;
         perm[i] = i;
      }
   }

   /* update the adjacency information in the quadratic variable terms */
   for( v = 0; v < consdata->nquadvars; ++v )
      for( i = 0; i < consdata->quadvarterms[v].nadjbilin; ++i )
         consdata->quadvarterms[v].adjbilin[i] = invperm[consdata->quadvarterms[v].adjbilin[i]];

   consdata->bilinsorted = TRUE;

   /* free temporary memory */
   SCIPfreeBufferArray(scip, &perm);
   SCIPfreeBufferArray(scip, &invperm);
   
   return SCIP_OKAY;
}

/** moves a linear variable from one position to another */
static
void consdataMoveLinearVar(
   SCIP_CONSDATA*        consdata,           /**< constraint data */
   int                   oldpos,             /**< position of variable that shall be moved */
   int                   newpos              /**< new position of variable */
   )
{
   assert(consdata != NULL);
   assert(oldpos >= 0);
   assert(oldpos < consdata->nlinvars);
   assert(newpos >= 0);
   assert(newpos < consdata->linvarssize);
   
   if( newpos == oldpos )
      return;
   
   consdata->linvars [newpos] = consdata->linvars [oldpos];
   consdata->lincoefs[newpos] = consdata->lincoefs[oldpos];
   
   if( consdata->lineventdata != NULL )
   {
      assert(newpos >= consdata->nlinvars || consdata->lineventdata[newpos] == NULL);
      
      consdata->lineventdata[newpos] = consdata->lineventdata[oldpos];
      consdata->lineventdata[newpos]->varidx = newpos;
      
      consdata->lineventdata[oldpos] = NULL;
   }
   
   consdata->linvarssorted = FALSE;
}   

/** moves a quadratic variable from one position to another */
static
void consdataMoveQuadVarTerm(
   SCIP_CONSDATA*        consdata,           /**< constraint data */
   int                   oldpos,             /**< position of variable that shall be moved */
   int                   newpos              /**< new position of variable */
   )
{
   assert(consdata != NULL);
   assert(oldpos >= 0);
   assert(oldpos < consdata->nquadvars);
   assert(newpos >= 0);
   assert(newpos < consdata->quadvarssize);
   
   if( newpos == oldpos )
      return;
   
   assert(newpos >= consdata->nquadvars || consdata->quadvarterms[newpos].eventdata == NULL);
   
   consdata->quadvarterms[newpos] = consdata->quadvarterms[oldpos];
   
   if( consdata->quadvarterms[newpos].eventdata != NULL )
   {
      consdata->quadvarterms[newpos].eventdata->varidx = -newpos-1;
      consdata->quadvarterms[oldpos].eventdata = NULL;
   }
   
   consdata->quadvarssorted = FALSE;
}   

/** adds linear coefficient in quadratic constraint */
static
SCIP_RETCODE addLinearCoef(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONS*            cons,               /**< quadratic constraint */
   SCIP_VAR*             var,                /**< variable of constraint entry */
   SCIP_Real             coef                /**< coefficient of constraint entry */
   )
{
   SCIP_CONSDATA* consdata;
   SCIP_Bool transformed;

   assert(scip != NULL);
   assert(cons != NULL);
   assert(var  != NULL);

   /* ignore coefficient if it is nearly zero */
   if( SCIPisZero(scip, coef) )
      return SCIP_OKAY;

   consdata = SCIPconsGetData(cons);
   assert(consdata != NULL);

   /* are we in the transformed problem? */
   transformed = SCIPconsIsTransformed(cons);

   /* always use transformed variables in transformed constraints */
   if( transformed )
   {
      SCIP_CALL( SCIPgetTransformedVar(scip, var, &var) );
   }
   assert(var != NULL);
   assert(transformed == SCIPvarIsTransformed(var));

   SCIP_CALL( consdataEnsureLinearVarsSize(scip, consdata, consdata->nlinvars+1) );
   consdata->linvars [consdata->nlinvars] = var;
   consdata->lincoefs[consdata->nlinvars] = coef;
  
   ++consdata->nlinvars;

   /* catch variable events */
   if( consdata->lineventdata != NULL )
   {
      SCIP_CONSHDLR* conshdlr;
      SCIP_CONSHDLRDATA* conshdlrdata;

      /* get event handler */
      conshdlr = SCIPconsGetHdlr(cons);
      conshdlrdata = SCIPconshdlrGetData(conshdlr);
      assert(conshdlrdata != NULL);
      assert(conshdlrdata->eventhdlr != NULL);
      
      consdata->lineventdata[consdata->nlinvars-1] = NULL;

      /* catch bound change events of variable */
      SCIP_CALL( catchLinearVarEvents(scip, conshdlrdata->eventhdlr, cons, consdata->nlinvars-1) );
   }

   /* invalidate activity information */
   consdata->activity = SCIP_INVALID;
   consdata->minlinactivity = SCIP_INVALID;
   consdata->maxlinactivity = SCIP_INVALID;
   consdata->minlinactivityinf = -1;
   consdata->maxlinactivityinf = -1;

   /* invalidate nonlinear row */
   if( consdata->nlrow != NULL )
   {
      SCIP_CALL( SCIPreleaseNlRow(scip, &consdata->nlrow) );
   }

   /* install rounding locks for new variable */
   SCIP_CALL( lockLinearVariable(scip, cons, var, coef) );
   
   /* capture new variable */
   SCIP_CALL( SCIPcaptureVar(scip, var) );

   consdata->ispropagated = FALSE;
   consdata->ispresolved = FALSE;
   consdata->isremovedfixings = consdata->isremovedfixings && SCIPvarIsActive(var);
   if( consdata->nlinvars == 1 )
      consdata->linvarssorted = TRUE;
   else
      consdata->linvarssorted = consdata->linvarssorted &&
         (SCIPvarCompare(consdata->linvars[consdata->nlinvars-2], consdata->linvars[consdata->nlinvars-1]) == -1);
   /* always set too FALSE since the new linear variable should be checked if already existing as quad var term */ 
   consdata->linvarsmerged = FALSE;

   return SCIP_OKAY;
}

/** deletes linear coefficient at given position from quadratic constraint data */
static
SCIP_RETCODE delLinearCoefPos(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONS*            cons,               /**< quadratic constraint */
   int                   pos                 /**< position of coefficient to delete */
   )
{
   SCIP_CONSDATA* consdata;
   SCIP_VAR* var;
   SCIP_Real coef;

   assert(scip != NULL);
   assert(cons != NULL);

   consdata = SCIPconsGetData(cons);
   assert(consdata != NULL);
   assert(0 <= pos && pos < consdata->nlinvars);

   var  = consdata->linvars[pos];
   coef = consdata->lincoefs[pos];
   assert(var != NULL);

   /* remove rounding locks for deleted variable */
   SCIP_CALL( unlockLinearVariable(scip, cons, var, coef) );

   /* if we catch variable events, drop the events on the variable */
   if( consdata->lineventdata != NULL )
   {
      SCIP_CONSHDLR* conshdlr;
      SCIP_CONSHDLRDATA* conshdlrdata;

      /* get event handler */
      conshdlr = SCIPconsGetHdlr(cons);
      conshdlrdata = SCIPconshdlrGetData(conshdlr);
      assert(conshdlrdata != NULL);
      assert(conshdlrdata->eventhdlr != NULL);

      /* drop bound change events of variable */
      SCIP_CALL( dropLinearVarEvents(scip, conshdlrdata->eventhdlr, cons, pos) );
   }

   /* release variable */
   SCIP_CALL( SCIPreleaseVar(scip, &consdata->linvars[pos]) );

   /* move the last variable to the free slot */
   consdataMoveLinearVar(consdata, consdata->nlinvars-1, pos);
   
   --consdata->nlinvars;
   
   /* invalidate activity */
   consdata->activity = SCIP_INVALID;
   consdata->minlinactivity = SCIP_INVALID;
   consdata->maxlinactivity = SCIP_INVALID;
   consdata->minlinactivityinf = -1;
   consdata->maxlinactivityinf = -1;

   /* invalidate nonlinear row */
   if( consdata->nlrow != NULL )
   {
      SCIP_CALL( SCIPreleaseNlRow(scip, &consdata->nlrow) );
   }

   consdata->ispropagated = FALSE;
   consdata->ispresolved  = FALSE;

   return SCIP_OKAY;
}

/** changes linear coefficient value at given position of quadratic constraint */
static
SCIP_RETCODE chgLinearCoefPos(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONS*            cons,               /**< quadratic constraint */
   int                   pos,                /**< position of linear coefficient to change */
   SCIP_Real             newcoef             /**< new value of linear coefficient */
   )
{
   SCIP_CONSHDLR* conshdlr;
   SCIP_CONSHDLRDATA* conshdlrdata;
   SCIP_CONSDATA* consdata;
   SCIP_VAR* var;
   SCIP_Real coef;

   assert(scip != NULL);
   assert(cons != NULL);
   assert(!SCIPisZero(scip, newcoef));

   conshdlrdata = NULL;

   consdata = SCIPconsGetData(cons);
   assert(consdata != NULL);
   assert(0 <= pos);
   assert(pos < consdata->nlinvars);
   assert(!SCIPisZero(scip, newcoef));

   var = consdata->linvars[pos];
   coef = consdata->lincoefs[pos];
   assert(var != NULL);
   assert(SCIPconsIsTransformed(cons) == SCIPvarIsTransformed(var));

   /* invalidate activity */
   consdata->activity = SCIP_INVALID;
   consdata->minlinactivity = SCIP_INVALID;
   consdata->maxlinactivity = SCIP_INVALID;
   consdata->minlinactivityinf = -1;
   consdata->maxlinactivityinf = -1;

   /* invalidate nonlinear row */
   if( consdata->nlrow != NULL )
   {
      SCIP_CALL( SCIPreleaseNlRow(scip, &consdata->nlrow) );
   }

   /* if necessary, remove the rounding locks and event catching of the variable */
   if( newcoef * coef < 0.0 )
   {
      if( SCIPconsIsLocked(cons) )
      {
         assert(SCIPconsIsTransformed(cons));

         /* remove rounding locks for variable with old coefficient */
         SCIP_CALL( unlockLinearVariable(scip, cons, var, coef) );
      }

      if( consdata->lineventdata[pos] != NULL )
      {
         /* get event handler */
         conshdlr = SCIPconsGetHdlr(cons);
         conshdlrdata = SCIPconshdlrGetData(conshdlr);
         assert(conshdlrdata != NULL);
         assert(conshdlrdata->eventhdlr != NULL);

         /* drop bound change events of variable */
         SCIP_CALL( dropLinearVarEvents(scip, conshdlrdata->eventhdlr, cons, pos) );
      }
   }

   /* change the coefficient */
   consdata->lincoefs[pos] = newcoef;

   /* if necessary, install the rounding locks and event catching of the variable again */
   if( newcoef * coef < 0.0 )
   {
      if( SCIPconsIsLocked(cons) )
      {
         /* install rounding locks for variable with new coefficient */
         SCIP_CALL( lockLinearVariable(scip, cons, var, newcoef) );
      }

      if( conshdlrdata != NULL )
      {
         /* catch bound change events of variable */
         SCIP_CALL( catchLinearVarEvents(scip, conshdlrdata->eventhdlr, cons, pos) );
      }
   }

   consdata->ispropagated = FALSE;
   consdata->ispresolved = FALSE;

   return SCIP_OKAY;
}

/** adds quadratic variable term to quadratic constraint */
static
SCIP_RETCODE addQuadVarTerm(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONS*            cons,               /**< quadratic constraint */
   SCIP_VAR*             var,                /**< variable to add */
   SCIP_Real             lincoef,            /**< linear coefficient of variable */
   SCIP_Real             sqrcoef,            /**< square coefficient of variable */
   SCIP_Bool             catchevents         /**< whether we should catch variable events */
   )
{
   SCIP_CONSDATA* consdata;
   SCIP_Bool transformed;
   SCIP_QUADVARTERM* quadvarterm;

   assert(scip != NULL);
   assert(cons != NULL);
   assert(var  != NULL);

   consdata = SCIPconsGetData(cons);
   assert(consdata != NULL);

   /* are we in the transformed problem? */
   transformed = SCIPconsIsTransformed(cons);

   /* always use transformed variables in transformed constraints */
   if( transformed )
   {
      SCIP_CALL( SCIPgetTransformedVar(scip, var, &var) );
   }
   assert(var != NULL);
   assert(transformed == SCIPvarIsTransformed(var));

   SCIP_CALL( consdataEnsureQuadVarTermsSize(scip, consdata, consdata->nquadvars+1) );
   
   quadvarterm = &consdata->quadvarterms[consdata->nquadvars];
   quadvarterm->var       = var;
   quadvarterm->lincoef   = lincoef;
   quadvarterm->sqrcoef   = sqrcoef;
   quadvarterm->adjbilinsize = 0;
   quadvarterm->nadjbilin = 0;
   quadvarterm->adjbilin  = NULL;
   quadvarterm->eventdata = NULL;
  
   ++consdata->nquadvars;

   /* capture variable */
   SCIP_CALL( SCIPcaptureVar(scip, var) );
   
   /* catch variable events, if we do so */
   if( catchevents )
   {
      SCIP_CONSHDLR* conshdlr;
      SCIP_CONSHDLRDATA* conshdlrdata;

      /* get event handler */
      conshdlr = SCIPconsGetHdlr(cons);
      conshdlrdata = SCIPconshdlrGetData(conshdlr);
      assert(conshdlrdata != NULL);
      assert(conshdlrdata->eventhdlr != NULL);
      
      /* catch bound change events of variable */
      SCIP_CALL( catchQuadVarEvents(scip, conshdlrdata->eventhdlr, cons, consdata->nquadvars-1) );
   }

   /* invalidate activity information */
   consdata->activity = SCIP_INVALID;
   SCIPintervalSetEmpty(&consdata->quadactivitybounds);

   /* invalidate nonlinear row */
   if( consdata->nlrow != NULL )
   {
      SCIP_CALL( SCIPreleaseNlRow(scip, &consdata->nlrow) );
   }

   /* install rounding locks for new variable */
   SCIP_CALL( lockQuadraticVariable(scip, cons, var) );

   consdata->ispropagated = FALSE;
   consdata->ispresolved  = FALSE;
   consdata->isremovedfixings = consdata->isremovedfixings && SCIPvarIsActive(var);
   if( consdata->nquadvars == 1 )
      consdata->quadvarssorted = TRUE;
   else
      consdata->quadvarssorted = consdata->quadvarssorted
         && (SCIPvarCompare(consdata->quadvarterms[consdata->nquadvars-2].var, consdata->quadvarterms[consdata->nquadvars-1].var) == -1);
   /* also set to FALSE if nquadvars == 1, since the new variable should be checked for linearity and other stuff in mergeAndClean ... */ 
   consdata->quadvarsmerged = FALSE;
   
   consdata->iscurvchecked = FALSE;

   return SCIP_OKAY;
}

/** deletes quadratic variable term at given position from quadratic constraint data */
static
SCIP_RETCODE delQuadVarTermPos(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONS*            cons,               /**< quadratic constraint */
   int                   pos                 /**< position of term to delete */
   )
{
   SCIP_CONSDATA* consdata;
   SCIP_VAR* var;

   assert(scip != NULL);
   assert(cons != NULL);

   consdata = SCIPconsGetData(cons);
   assert(consdata != NULL);
   assert(0 <= pos && pos < consdata->nquadvars);

   var = consdata->quadvarterms[pos].var;
   assert(var != NULL);
   assert(consdata->quadvarterms[pos].nadjbilin == 0);

   /* remove rounding locks for deleted variable */
   SCIP_CALL( unlockQuadraticVariable(scip, cons, var) );

   /* if we catch variable events, drop the events on the variable */
   if( consdata->quadvarterms[pos].eventdata != NULL )
   {
      SCIP_CONSHDLR* conshdlr;
      SCIP_CONSHDLRDATA* conshdlrdata;

      /* get event handler */
      conshdlr = SCIPconsGetHdlr(cons);
      conshdlrdata = SCIPconshdlrGetData(conshdlr);
      assert(conshdlrdata != NULL);
      assert(conshdlrdata->eventhdlr != NULL);

      /* drop bound change events of variable */
      SCIP_CALL( dropQuadVarEvents(scip, conshdlrdata->eventhdlr, cons, pos) );
   }
   
   /* release variable */
   SCIP_CALL( SCIPreleaseVar(scip, &consdata->quadvarterms[pos].var) );
   
   /* free adjacency array */
   SCIPfreeBlockMemoryArrayNull(scip, &consdata->quadvarterms[pos].adjbilin, consdata->quadvarterms[pos].adjbilinsize);

   /* move the last variable term to the free slot */
   consdataMoveQuadVarTerm(consdata, consdata->nquadvars-1, pos);
   
   --consdata->nquadvars;
   
   /* invalidate activity */
   consdata->activity = SCIP_INVALID;

   /* invalidate nonlinear row */
   if( consdata->nlrow != NULL )
   {
      SCIP_CALL( SCIPreleaseNlRow(scip, &consdata->nlrow) );
   }

   consdata->ispropagated  = FALSE;
   consdata->ispresolved   = FALSE;
   consdata->iscurvchecked = FALSE;

   return SCIP_OKAY;
}

/** replace variable in quadratic variable term at given position of quadratic constraint data 
 * allows to replace x by coef*y+offset, thereby maintaining linear and square coefficients and bilinear terms */
static
SCIP_RETCODE replaceQuadVarTermPos(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONS*            cons,               /**< quadratic constraint */
   int                   pos,                /**< position of term to replace */
   SCIP_VAR*             var,                /**< new variable */
   SCIP_Real             coef,               /**< linear coefficient of new variable */
   SCIP_Real             offset              /**< offset of new variable */
   )
{
   SCIP_CONSDATA* consdata;
   SCIP_QUADVARTERM* quadvarterm;
   SCIP_EVENTHDLR* eventhdlr;
   SCIP_BILINTERM* bilinterm;
   SCIP_Real constant;
   
   int i;
   SCIP_VAR* var2;
   
   consdata = SCIPconsGetData(cons);
   assert(consdata != NULL);
   assert(pos >= 0);
   assert(pos <  consdata->nquadvars);

   quadvarterm = &consdata->quadvarterms[pos];
   
   /* remove rounding locks for old variable */
   SCIP_CALL( unlockQuadraticVariable(scip, cons, quadvarterm->var) );

   /* if we catch variable events, drop the events on the old variable */
   if( quadvarterm->eventdata != NULL )
   {
      SCIP_CONSHDLR* conshdlr;
      SCIP_CONSHDLRDATA* conshdlrdata;

      /* get event handler */
      conshdlr = SCIPconsGetHdlr(cons);
      conshdlrdata = SCIPconshdlrGetData(conshdlr);
      assert(conshdlrdata != NULL);
      assert(conshdlrdata->eventhdlr != NULL);

      eventhdlr = conshdlrdata->eventhdlr;
      
      /* drop bound change events of variable */
      SCIP_CALL( dropQuadVarEvents(scip, eventhdlr, cons, pos) );
   }
   else
   {
      eventhdlr = NULL;
   }
   
   /* compute constant and put into lhs/rhs */
   constant = quadvarterm->lincoef * offset + quadvarterm->sqrcoef * offset * offset;
   if( constant != 0.0 )
   {
      /* maintain constant part */
      if( !SCIPisInfinity(scip, -consdata->lhs) )
         consdata->lhs -= constant;
      if( !SCIPisInfinity(scip,  consdata->rhs) )
         consdata->rhs -= constant;
   }

   /* update linear and square coefficient */
   quadvarterm->lincoef *= coef;
   quadvarterm->lincoef += 2.0 * quadvarterm->sqrcoef * coef * offset;
   quadvarterm->sqrcoef *= coef * coef;

   /* update bilinear terms */
   for( i = 0; i < quadvarterm->nadjbilin; ++i )
   {
      bilinterm = &consdata->bilinterms[quadvarterm->adjbilin[i]];
      
      if( bilinterm->var1 == quadvarterm->var )
      {
         bilinterm->var1 = var;
         var2 = bilinterm->var2;
      }
      else
      {
         assert(bilinterm->var2 == quadvarterm->var);
         bilinterm->var2 = var;
         var2 = bilinterm->var1;
      }
      
      if( var == var2 )
      {
         /* looks like we actually have a square term here */
         quadvarterm->lincoef += bilinterm->coef * offset;
         quadvarterm->sqrcoef += bilinterm->coef * coef;
         /* deleting bilinear terms is expensive, since it requires updating adjacency information
          * thus, for now we just set the coefficient to 0.0 and clear in later when the bilinear terms are merged */
         bilinterm->coef = 0.0;
         continue;
      }

      /* swap var1 and var2 if they are in wrong order */
      if( SCIPvarCompare(bilinterm->var1, bilinterm->var2) < 0 )
      {
         SCIP_VAR* tmp;
         tmp = bilinterm->var1;
         bilinterm->var1 = bilinterm->var2;
         bilinterm->var2 = tmp;
      }

      if( offset != 0.0 )
      {
         /* need to find var2 and add offset*bilinterm->coef to linear coefficient */
         int var2pos;
         
         var2pos = 0;
         while( consdata->quadvarterms[var2pos].var != var2 )
         {
            ++var2pos;
            assert(var2pos < consdata->nquadvars);
         }
         
         consdata->quadvarterms[var2pos].lincoef += bilinterm->coef * offset;
      }
      
      bilinterm->coef *= coef;
   }

   /* release old variable */
   SCIP_CALL( SCIPreleaseVar(scip, &quadvarterm->var) );

   /* set new variable */
   quadvarterm->var = var;
   
   /* capture new variable */
   SCIP_CALL( SCIPcaptureVar(scip, quadvarterm->var) );

   /* catch variable events, if we do so */
   if( eventhdlr != NULL )
   {
      /* catch bound change events of variable */
      SCIP_CALL( catchQuadVarEvents(scip, eventhdlr, cons, pos) );
   }

   /* invalidate activity information */
   consdata->activity = SCIP_INVALID;
   SCIPintervalSetEmpty(&consdata->quadactivitybounds);

   /* invalidate nonlinear row */
   if( consdata->nlrow != NULL )
   {
      SCIP_CALL( SCIPreleaseNlRow(scip, &consdata->nlrow) );
   }

   /* install rounding locks for new variable */
   SCIP_CALL( lockQuadraticVariable(scip, cons, var) );

   consdata->isremovedfixings = consdata->isremovedfixings && SCIPvarIsActive(var);
   if( consdata->nquadvars == 1 )
   {
      consdata->quadvarssorted = TRUE;
      consdata->quadvarsmerged = TRUE;
   }
   else
   {
      consdata->quadvarssorted = FALSE;
      consdata->quadvarsmerged = FALSE;
   }
   consdata->bilinmerged &= (quadvarterm->nadjbilin == 0);  /*lint !e514*/
   
   consdata->ispropagated  = FALSE;
   consdata->ispresolved   = FALSE;
   consdata->iscurvchecked = FALSE;

   return SCIP_OKAY;
}

/** adds a bilinear term to quadratic constraint */
static
SCIP_RETCODE addBilinearTerm(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONS*            cons,               /**< quadratic constraint */
   int                   var1pos,            /**< position of first variable in quadratic variables array */
   int                   var2pos,            /**< position of second variable in quadratic variables array */
   SCIP_Real             coef                /**< coefficient of bilinear term */
   )
{
   SCIP_CONSDATA* consdata;
   SCIP_BILINTERM* bilinterm;

   assert(scip != NULL);
   assert(cons != NULL);
   
   if( var1pos == var2pos )
   {
      SCIPerrorMessage("tried to add bilinear term where both variables are the same\n");
      return SCIP_INVALIDDATA;
   }

   consdata = SCIPconsGetData(cons);
   assert(consdata != NULL);
   
   assert(var1pos >= 0);
   assert(var1pos < consdata->nquadvars);
   assert(var2pos >= 0);
   assert(var2pos < consdata->nquadvars);

   SCIP_CALL( consdataEnsureBilinSize(scip, consdata, consdata->nbilinterms + 1) );
   
   bilinterm = &consdata->bilinterms[consdata->nbilinterms];
   if( SCIPvarCompare(consdata->quadvarterms[var1pos].var, consdata->quadvarterms[var2pos].var) > 0 )
   {
      bilinterm->var1 = consdata->quadvarterms[var1pos].var;
      bilinterm->var2 = consdata->quadvarterms[var2pos].var;
   }
   else
   {
      bilinterm->var1 = consdata->quadvarterms[var2pos].var;
      bilinterm->var2 = consdata->quadvarterms[var1pos].var;
   }
   bilinterm->coef = coef;
   
   if( bilinterm->var1 == bilinterm->var2 )
   {
      SCIPerrorMessage("tried to add bilinear term where both variables are the same, but appear at different positions in quadvarterms array\n");
      return SCIP_INVALIDDATA;
   }
   
   SCIP_CALL( consdataEnsureAdjBilinSize(scip, &consdata->quadvarterms[var1pos], consdata->quadvarterms[var1pos].nadjbilin + 1) );
   SCIP_CALL( consdataEnsureAdjBilinSize(scip, &consdata->quadvarterms[var2pos], consdata->quadvarterms[var2pos].nadjbilin + 1) );
   
   consdata->quadvarterms[var1pos].adjbilin[consdata->quadvarterms[var1pos].nadjbilin] = consdata->nbilinterms;
   consdata->quadvarterms[var2pos].adjbilin[consdata->quadvarterms[var2pos].nadjbilin] = consdata->nbilinterms;
   ++consdata->quadvarterms[var1pos].nadjbilin;
   ++consdata->quadvarterms[var2pos].nadjbilin;
   
   ++consdata->nbilinterms;
   
   /* invalidate activity information */
   consdata->activity = SCIP_INVALID;
   SCIPintervalSetEmpty(&consdata->quadactivitybounds);

   /* invalidate nonlinear row */
   if( consdata->nlrow != NULL )
   {
      SCIP_CALL( SCIPreleaseNlRow(scip, &consdata->nlrow) );
   }

   consdata->ispropagated = FALSE;
   consdata->ispresolved  = FALSE;
   if( consdata->nbilinterms == 1 )
   {
      consdata->bilinsorted = TRUE;
      consdata->bilinmerged = TRUE;
   }
   else
   {
      consdata->bilinsorted = consdata->bilinsorted
         && (bilinTermComp(consdata, consdata->nbilinterms-2, consdata->nbilinterms-1) >= 0);
      consdata->bilinmerged = FALSE;
   }
   
   consdata->iscurvchecked = FALSE;

   return SCIP_OKAY;
}

/** removes a set of bilinear terms and updates adjacency information in quad var terms
 * Note: this function sorts the given array termposs */
static
SCIP_RETCODE removeBilinearTermsPos(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONS*            cons,               /**< quadratic constraint */
   int                   nterms,             /**< number of terms to delete */
   int*                  termposs            /**< indices of terms to delete */
   )
{
   SCIP_CONSDATA* consdata;
   int* newpos;
   int i;
   int j;
   int offset;
   
   assert(scip != NULL);
   assert(cons != NULL);
   assert(nterms == 0 || termposs != NULL);
   
   if( nterms == 0 )
      return SCIP_OKAY;
   
   consdata = SCIPconsGetData(cons);
   assert(consdata != NULL);
   
   SCIPsortInt(termposs, nterms);
   
   SCIP_CALL( SCIPallocBufferArray(scip, &newpos, consdata->nbilinterms) );
   
   i = 0;
   offset = 0;
   for( j = 0; j < consdata->nbilinterms; ++j )
   {
      /* if j'th term is deleted, increase offset and continue */
      if( i < nterms && j == termposs[i] )
      {
         ++offset;
         ++i;
         newpos[j] = -1;
         continue;
      }
      
      /* otherwise, move it forward and remember new position */
      if( offset > 0 )
         consdata->bilinterms[j-offset] = consdata->bilinterms[j];
      newpos[j] = j - offset;
   }
   assert(offset == nterms);
   
   /* update adjacency and activity information in quad var terms */
   for( i = 0; i < consdata->nquadvars; ++i )
   {
      offset = 0;
      for( j = 0; j < consdata->quadvarterms[i].nadjbilin; ++j )
      {
         assert(consdata->quadvarterms[i].adjbilin[j] < consdata->nbilinterms);
         if( newpos[consdata->quadvarterms[i].adjbilin[j]] == -1 )
         {
            /* corresponding bilinear term was deleted, thus increase offset */
            ++offset;
         }
         else
         {
            /* update index of j'th bilin term and store at position j-offset */
            consdata->quadvarterms[i].adjbilin[j-offset] = newpos[consdata->quadvarterms[i].adjbilin[j]];
         }
      }
      consdata->quadvarterms[i].nadjbilin -= offset;
      /* some bilinear term was removed, so invalidate activity bounds */
   }
   
   consdata->nbilinterms -= nterms;
   
   SCIPfreeBufferArray(scip, &newpos);
   
   /* some quad vars may be linear now */
   consdata->quadvarsmerged = FALSE;
   
   consdata->ispropagated  = FALSE;
   consdata->ispresolved   = FALSE;
   consdata->iscurvchecked = FALSE;
   SCIPintervalSetEmpty(&consdata->quadactivitybounds);

   /* invalidate activity */
   consdata->activity = SCIP_INVALID;
   
   /* invalidate nonlinear row */
   if( consdata->nlrow != NULL )
   {
      SCIP_CALL( SCIPreleaseNlRow(scip, &consdata->nlrow) );
   }

   return SCIP_OKAY;
}

/* merges quad var terms that correspond to the same variable and does additional cleanup
 * if a quadratic variable terms is actually linear, makes a linear term out of it
 * also replaces squares of binary variables by the binary variables, i.e., adds sqrcoef to lincoef
 */
static
SCIP_RETCODE mergeAndCleanQuadVarTerms(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONS*            cons                /**< quadratic constraint */
   )
{
   SCIP_QUADVARTERM* quadvarterm;
   SCIP_CONSDATA* consdata;
   int i;
   int j;

   assert(scip != NULL);
   assert(cons != NULL);

   consdata = SCIPconsGetData(cons);
   
   if( consdata->quadvarsmerged )
      return SCIP_OKAY;
   
   if( consdata->nquadvars == 0 )
   {
      consdata->quadvarsmerged = TRUE;
      return SCIP_OKAY;
   }

   i = 0;
   while( i < consdata->nquadvars )
   {
      /* make sure quad var terms are sorted (do this in every round, since we may move variables around) */
      SCIP_CALL( consdataSortQuadVarTerms(scip, consdata) );
      
      quadvarterm = &consdata->quadvarterms[i];
      
      for( j = i+1; j < consdata->nquadvars && consdata->quadvarterms[j].var == quadvarterm->var; ++j )
      {
         /* add quad var term j to current term i */
         quadvarterm->lincoef += consdata->quadvarterms[j].lincoef;
         quadvarterm->sqrcoef += consdata->quadvarterms[j].sqrcoef;
         if( consdata->quadvarterms[j].nadjbilin > 0 )
         {
            /* move adjacency information from j to i */
            SCIP_CALL( consdataEnsureAdjBilinSize(scip, quadvarterm, quadvarterm->nadjbilin + consdata->quadvarterms[j].nadjbilin) );
            BMScopyMemoryArray(&quadvarterm->adjbilin[quadvarterm->nadjbilin], consdata->quadvarterms[j].adjbilin, consdata->quadvarterms[j].nadjbilin);  /*lint !e866*/
            quadvarterm->nadjbilin += consdata->quadvarterms[j].nadjbilin;
            consdata->quadvarterms[j].nadjbilin = 0;
         }
         consdata->quadvarterms[j].lincoef = 0.0;
         consdata->quadvarterms[j].sqrcoef = 0.0;
         /* mark that activity information in quadvarterm is not up to date anymore */
      }
      
      /* remove quad var terms i+1..j-1 backwards */
      for( j = j-1; j > i; --j )
      {
         SCIP_CALL( delQuadVarTermPos(scip, cons, j) );
      }

      /* for binary variables, x^2 = x
       * @todo doing this reformulation may destroy possible convexity, see, e.g., isqp1 */
      if( quadvarterm->sqrcoef != 0.0 && SCIPvarIsBinary(quadvarterm->var) )
      {
         quadvarterm->lincoef += quadvarterm->sqrcoef;
         quadvarterm->sqrcoef = 0.0;

         /* invalidate nonlinear row */
         if( consdata->nlrow != NULL )
         {
            SCIP_CALL( SCIPreleaseNlRow(scip, &consdata->nlrow) );
         }
      }

      /* if its 0.0 or linear, get rid of it */
      if( SCIPisZero(scip, consdata->quadvarterms[i].sqrcoef) && consdata->quadvarterms[i].nadjbilin == 0 )
      {
         if( !SCIPisZero(scip, consdata->quadvarterms[i].lincoef) )
         {
            /* seem to be a linear term now, thus add as linear term */
            SCIP_CALL( addLinearCoef(scip, cons, consdata->quadvarterms[i].var, consdata->quadvarterms[i].lincoef) );
         }
         /* remove term at pos i */
         SCIP_CALL( delQuadVarTermPos(scip, cons, i) );
      }
      else
      {
         ++i;
      }
   }
   
   consdata->quadvarsmerged = TRUE;
   SCIPintervalSetEmpty(&consdata->quadactivitybounds);

   return SCIP_OKAY;
}

/* merges entries with same linear variable into one entry and cleans up entries with coefficient 0.0 */
static
SCIP_RETCODE mergeAndCleanLinearVars(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONS*            cons                /**< quadratic constraint */
   )
{
   SCIP_CONSDATA* consdata;
   SCIP_Real newcoef;
   int i;
   int j;
   int qvarpos;
   
   assert(scip != NULL);
   assert(cons != NULL);
   
   consdata = SCIPconsGetData(cons);
   
   if( consdata->linvarsmerged )
      return SCIP_OKAY;
   
   if( consdata->nlinvars == 0 )
   {
      consdata->linvarsmerged = TRUE;
      return SCIP_OKAY;
   }
   
   i = 0;
   while( i < consdata->nlinvars )
   {
      /* make sure linear variables are sorted (do this in every round, since we may move variables around) */
      consdataSortLinearVars(consdata);

      /* sum up coefficients that correspond to variable i */
      newcoef = consdata->lincoefs[i];
      for( j = i+1; j < consdata->nlinvars && consdata->linvars[i] == consdata->linvars[j]; ++j )
         newcoef += consdata->lincoefs[j];
      /* delete the additional variables in backward order */ 
      for( j = j-1; j > i; --j )
      {
         SCIP_CALL( delLinearCoefPos(scip, cons, j) );
      }
      
      /* check if there is already a quadratic variable term with this variable */
      SCIP_CALL( consdataFindQuadVarTerm(scip, consdata, consdata->linvars[i], &qvarpos) );
      if( qvarpos >= 0)
      {
         /* add newcoef to linear coefficient of quadratic variable and mark linear variable as to delete */
         assert(qvarpos < consdata->nquadvars);
         assert(consdata->quadvarterms[qvarpos].var == consdata->linvars[i]);
         consdata->quadvarterms[qvarpos].lincoef += newcoef;
         newcoef = 0.0;
         SCIPintervalSetEmpty(&consdata->quadactivitybounds);
      }

      /* delete also entry at position i, if it became zero (or was zero before) */
      if( SCIPisZero(scip, newcoef) )
      {
         SCIP_CALL( delLinearCoefPos(scip, cons, i) );
      }
      else
      {
         SCIP_CALL( chgLinearCoefPos(scip, cons, i, newcoef) );
         ++i;
      }
   }
   
   consdata->linvarsmerged = TRUE;

   return SCIP_OKAY;
}

/* merges bilinear terms with same variables into a single term, removes bilinear terms with coefficient 0.0 */
static
SCIP_RETCODE mergeAndCleanBilinearTerms(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONS*            cons                /**< quadratic constraint */
   )
{
   SCIP_CONSDATA* consdata;
   SCIP_BILINTERM* bilinterm;
   int i;
   int j;
   int* todelete;
   int ntodelete;
   
   assert(scip != NULL);
   assert(cons != NULL);
   
   consdata = SCIPconsGetData(cons);
   
   if( consdata->bilinmerged )
      return SCIP_OKAY;
   
   if( consdata->nbilinterms == 0 )
   {
      consdata->bilinmerged = TRUE;
      return SCIP_OKAY;
   }
   
   /* alloc memory for array of terms that need to be deleted finally */
   ntodelete = 0;
   SCIP_CALL( SCIPallocBufferArray(scip, &todelete, consdata->nbilinterms) );

   /* make sure bilinear terms are sorted */
   SCIP_CALL( consdataSortBilinTerms(scip, consdata) );

   i = 0;
   while( i < consdata->nbilinterms )
   {
      bilinterm = &consdata->bilinterms[i];

      /* sum up coefficients that correspond to same variables as term i */
      for( j = i+1; j < consdata->nbilinterms && bilinterm->var1 == consdata->bilinterms[j].var1 && bilinterm->var2 == consdata->bilinterms[j].var2; ++j )
      {
         bilinterm->coef += consdata->bilinterms[j].coef;
         todelete[ntodelete++] = j;
      }

      /* delete also entry at position i, if it became zero (or was zero before) */
      if( SCIPisZero(scip, bilinterm->coef) )
      {
         todelete[ntodelete++] = i;
      }
      
      /* continue with term after the current series */
      i = j;
   }

   /* delete bilinear terms */
   SCIP_CALL( removeBilinearTermsPos(scip, cons, ntodelete, todelete) );
   
   SCIPfreeBufferArray(scip, &todelete);
   
   consdata->bilinmerged = TRUE;
   
   return SCIP_OKAY;
}

/** removes fixes (or aggregated) variables from a quadratic constraint */
static
SCIP_RETCODE removeFixedVariables(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONS*            cons                /**< quadratic constraint */
   )
{
   SCIP_CONSDATA* consdata;
   SCIP_BILINTERM* bilinterm;
   SCIP_Real coef;
   SCIP_Real offset;
   SCIP_VAR* var;
   SCIP_VAR* var2;
   int var2pos;
   int i;
   int j;
   int k;
   
   SCIP_Bool have_change;
   
   assert(scip != NULL);
   assert(cons != NULL);
   
   consdata = SCIPconsGetData(cons);
   
   have_change = FALSE;
   i = 0;
   while( i < consdata->nlinvars )
   {
      var = consdata->linvars[i];
         
      if( SCIPvarIsActive(var) )
      {
         ++i;
         continue;
      }
      
      have_change = TRUE;
      
      coef = consdata->lincoefs[i];
      offset = 0.0;
      
      SCIP_CALL( SCIPvarGetProbvarSum(&var, &coef, &offset) );
      
      SCIPdebugMessage("  linear term %g*<%s> is replaced by %g * <%s> + %g\n", consdata->lincoefs[i], SCIPvarGetName(consdata->linvars[i]), coef, SCIPvarGetName(var), offset);
      
      /* delete previous variable (this will move another variable to position i) */
      SCIP_CALL( delLinearCoefPos(scip, cons, i) );

      /* put constant part into bounds */
      if( offset != 0.0 )
      {
         if( !SCIPisInfinity(scip, -consdata->lhs) )
            consdata->lhs -= offset;
         if( !SCIPisInfinity(scip,  consdata->rhs) )
            consdata->rhs -= offset;
      }

      /* nothing left to do if variable had been fixed */
      if( coef == 0.0 )
         continue;
      
      /* if GetProbvar gave a linear variable, just add it
       * if it's a multilinear variable, add it's disaggregated variables */
      if( SCIPvarIsActive(var) )
      {
         SCIP_CALL( addLinearCoef(scip, cons, var, coef) );
      }
      else
      {
         int        naggrs;
         SCIP_VAR** aggrvars;
         SCIP_Real* aggrscalars;
         SCIP_Real  aggrconstant;
         
         assert(SCIPvarGetStatus(var) == SCIP_VARSTATUS_MULTAGGR);
         
         naggrs = SCIPvarGetMultaggrNVars(var);
         aggrvars = SCIPvarGetMultaggrVars(var);
         aggrscalars = SCIPvarGetMultaggrScalars(var);
         aggrconstant = SCIPvarGetMultaggrConstant(var);
         
         SCIP_CALL( consdataEnsureLinearVarsSize(scip, consdata, consdata->nlinvars + naggrs) );
         
         for( j = 0; j < naggrs; ++j )
         {
            SCIP_CALL( addLinearCoef(scip, cons, aggrvars[j], coef * aggrscalars[j]) );
         }
         
         if( aggrconstant != 0.0 )
         {
            if( !SCIPisInfinity(scip, -consdata->lhs) )
               consdata->lhs -= coef * aggrconstant;
            if( !SCIPisInfinity(scip,  consdata->rhs) )
               consdata->rhs -= coef * aggrconstant;
         }
      }
   }
   
   i = 0;
   while( i < consdata->nquadvars )
   {
      var = consdata->quadvarterms[i].var;
    
      if( SCIPvarIsActive(var) )
      {
         ++i;
         continue;
      }

      have_change = TRUE;

      coef   = 1.0;
      offset = 0.0;
      SCIP_CALL( SCIPvarGetProbvarSum(&var, &coef, &offset) );

      SCIPdebugMessage("  quadratic variable <%s> with status %d is replaced by %g * <%s> + %g\n", SCIPvarGetName(consdata->quadvarterms[i].var), SCIPvarGetStatus(consdata->quadvarterms[i].var), coef, SCIPvarGetName(var), offset);

      /* handle fixed variable */
      if( coef == 0.0 )
      {
         /* if not fixed to 0.0, add to linear coefs of vars in bilinear terms, and deal with linear and square term as constant */ 
         if( offset != 0.0 )
         {
            for( j = 0; j < consdata->quadvarterms[i].nadjbilin; ++j )
            {
               bilinterm = &consdata->bilinterms[consdata->quadvarterms[i].adjbilin[j]];
               
               var2 = bilinterm->var1 == var ? bilinterm->var2 : bilinterm->var1;
               assert(var2 != var);
               
               var2pos = 0;
               while( consdata->quadvarterms[var2pos].var != var2 )
               {
                  ++var2pos;
                  assert(var2pos < consdata->nquadvars);
               }
               consdata->quadvarterms[var2pos].lincoef += bilinterm->coef * offset;
               SCIPintervalSetEmpty(&consdata->quadactivitybounds);
            }
            
            offset = consdata->quadvarterms[i].lincoef * offset + consdata->quadvarterms[i].sqrcoef * offset * offset;
            if( !SCIPisInfinity(scip, -consdata->lhs) )
               consdata->lhs -= offset;
            if( !SCIPisInfinity(scip,  consdata->rhs) )
               consdata->rhs -= offset;
         }
         
         /* remove bilinear terms */
         SCIP_CALL( removeBilinearTermsPos(scip, cons, consdata->quadvarterms[i].nadjbilin, consdata->quadvarterms[i].adjbilin) );
         
         /* delete quad. var term i */
         SCIP_CALL( delQuadVarTermPos(scip, cons, i) );
         
         continue;
      }
      
      assert(var != NULL);
      
      /* if GetProbvar gave an active variable, replace the quad var term so that it uses the new variable */
      if( SCIPvarIsActive(var) )
      {
         /* replace x by coef*y+offset */
         SCIP_CALL( replaceQuadVarTermPos(scip, cons, i, var, coef, offset) );

         continue;
      }
      else
      {
         /* if GetProbVar gave a multiaggr. variable, add new quad var terms and new bilinear terms
          * x is replaced by coef * (sum_i a_ix_i + b) + offset
          * lcoef * x + scoef * x^2 + bcoef * x * y ->
          *   (b*coef + offset) * (lcoef + (b*coef + offset) * scoef)
          * + sum_i a_i*coef * (lcoef + 2 (b*coef + offset) * scoef) x_i
          * + sum_i (a_i*coef)^2 * scoef * x_i^2
          * + 2 sum_{i,j, i<j} (a_i a_j coef^2 scoef) x_i x_j
          * + bcoef * (b*coef + offset + coef * sum_i a_ix_i) y 
          */
         int        naggrs;
         SCIP_VAR** aggrvars;     /* x_i */
         SCIP_Real* aggrscalars;  /* a_i */
         SCIP_Real  aggrconstant; /* b */
         int nquadtermsold;
         
         SCIP_Real lcoef;
         SCIP_Real scoef;
         
         assert(SCIPvarGetStatus(var) == SCIP_VARSTATUS_MULTAGGR);
         
         naggrs = SCIPvarGetMultaggrNVars(var);
         aggrvars = SCIPvarGetMultaggrVars(var);
         aggrscalars = SCIPvarGetMultaggrScalars(var);
         aggrconstant = SCIPvarGetMultaggrConstant(var);
         
         lcoef = consdata->quadvarterms[i].lincoef;
         scoef = consdata->quadvarterms[i].sqrcoef;
         
         nquadtermsold = consdata->nquadvars;
         
         SCIP_CALL( consdataEnsureQuadVarTermsSize(scip, consdata, consdata->nquadvars + naggrs) );
         
         /* take care of constant part */
         if( aggrconstant != 0.0 || offset != 0.0 )
         {
            SCIP_Real constant;
            constant = (aggrconstant * coef + offset) * (lcoef + (aggrconstant * coef + offset) * scoef);
            if( !SCIPisInfinity(scip, -consdata->lhs) )
               consdata->lhs -= constant;
            if( !SCIPisInfinity(scip,  consdata->rhs) )
               consdata->rhs -= constant;
         }

         /* add x_i's with linear and square coefficients */
         for( j = 0; j < naggrs; ++j )
         {
            SCIP_CALL( addQuadVarTerm(scip, cons, aggrvars[j],
               coef * aggrscalars[j] * (lcoef + 2.0 * scoef * (coef * aggrconstant + offset)),
               coef * coef * aggrscalars[j] * aggrscalars[j] * scoef,
               TRUE) );
         }
         
         /* ensure space for bilinear terms */
         SCIP_CALL( consdataEnsureBilinSize(scip, consdata, consdata->nquadvars + (scoef != 0.0 ? (naggrs * (naggrs-1))/2 : 0) + consdata->quadvarterms[j].nadjbilin * naggrs) );
         
         /* add x_j*x_k's */
         if( scoef != 0.0 )
         {
            for( j = 0; j < naggrs; ++j )
               for( k = 0; k < j; ++k )
               {
                  assert(aggrvars[j] != aggrvars[k]);
                  SCIP_CALL( addBilinearTerm(scip, cons, nquadtermsold + j, nquadtermsold + k, 
                     2.0 * aggrscalars[j] * aggrscalars[k] * coef * coef * scoef) );
               }
         }

         /* add x_i*y's */
         for( k = 0; k < consdata->quadvarterms[i].nadjbilin; ++k )
         {
            bilinterm = &consdata->bilinterms[consdata->quadvarterms[i].adjbilin[k]];
            var2 = (bilinterm->var1 == consdata->quadvarterms[i].var) ? bilinterm->var2 : bilinterm->var1;
            assert(var2 != consdata->quadvarterms[i].var);
            
            /* this is not efficient, but we cannot sort the quadratic terms here, since we currently iterate over them */
            var2pos = 0;
            while( consdata->quadvarterms[var2pos].var != var2 )
            {
               ++var2pos;
               assert(var2pos < consdata->nquadvars);
            }
            
            for( j = 0; j < naggrs; ++j )
            {
               if( aggrvars[j] == var2 )
               { /* x_i == y, so we have a square term here */
                  consdata->quadvarterms[var2pos].sqrcoef += bilinterm->coef * coef * aggrscalars[j];
               }
               else
               { /* x_i != y, so we need to add a bilinear term here */
                  SCIP_CALL( addBilinearTerm(scip, cons, nquadtermsold + j, var2pos,
                     bilinterm->coef * coef * aggrscalars[j]) );
               }
            }
            
            consdata->quadvarterms[var2pos].lincoef += bilinterm->coef * (aggrconstant * coef + offset);
         }

         /* remove bilinear terms */
         SCIP_CALL( removeBilinearTermsPos(scip, cons, consdata->quadvarterms[i].nadjbilin, consdata->quadvarterms[i].adjbilin) );
         
         /* delete quad. var term i */
         SCIP_CALL( delQuadVarTermPos(scip, cons, i) );
      }
   }
   
   consdata->isremovedfixings = TRUE;
   
   SCIPdebugMessage("removed fixations from <%s>\n  -> ", SCIPconsGetName(cons));
   SCIPdebug( SCIPprintCons(scip, cons, NULL) );
   
#ifndef NDEBUG
   for( i = 0; i < consdata->nlinvars; ++i )
      assert(SCIPvarIsActive(consdata->linvars[i]));

   for( i = 0; i < consdata->nquadvars; ++i )
      assert(SCIPvarIsActive(consdata->quadvarterms[i].var));
#endif
   
   if( !have_change )
      return SCIP_OKAY;
   
   /* some quadratic variable may have been replaced by an already existing linear variable
    * in this case, we want the linear variable to be removed, which happens in mergeAndCleanLinearVars
    */ 
   consdata->linvarsmerged = FALSE;
   
   SCIP_CALL( mergeAndCleanBilinearTerms(scip, cons) );
   SCIP_CALL( mergeAndCleanQuadVarTerms(scip, cons) );
   SCIP_CALL( mergeAndCleanLinearVars(scip, cons) );
   
#ifndef NDEBUG
   for( i = 0; i < consdata->nbilinterms; ++i )
   {
      assert(consdata->bilinterms[i].var1 != consdata->bilinterms[i].var2);
      assert(consdata->bilinterms[i].coef != 0.0);
   }
#endif
   
   return SCIP_OKAY;
}

/** create a nonlinear row representation of the constraint and stores them in consdata */
static
SCIP_RETCODE createNlRow(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONS*            cons                /**< quadratic constraint */
   )
{
   SCIP_CONSDATA* consdata;
   int        nquadvars;     /* number of variables in quadratic terms */
   SCIP_VAR** quadvars;      /* variables in quadratic terms */
   int        nquadelems;    /* number of quadratic elements (square and bilinear terms) */
   SCIP_QUADELEM* quadelems; /* quadratic elements (square and bilinear terms) */
   int        nquadlinterms; /* number of linear terms using variables that are in quadratic terms */
   SCIP_VAR** quadlinvars;   /* variables of linear terms using variables that are in quadratic terms */
   SCIP_Real* quadlincoefs;  /* coefficients of linear terms using variables that are in quadratic terms */
   int i;
   int idx1;
   int idx2;
   int lincnt;
   int elcnt;
   SCIP_VAR* lastvar;
   int lastvaridx;

   assert(scip != NULL);
   assert(cons != NULL);

   consdata = SCIPconsGetData(cons);
   assert(consdata != NULL);

   if( consdata->nlrow != NULL )
   {
      SCIP_CALL( SCIPreleaseNlRow(scip, &consdata->nlrow) );
   }

   nquadvars = consdata->nquadvars;
   nquadelems = consdata->nbilinterms;
   nquadlinterms = 0;
   for( i = 0; i < nquadvars; ++i )
   {
      if( consdata->quadvarterms[i].sqrcoef != 0.0 )
         ++nquadelems;
      if( !SCIPisZero(scip, consdata->quadvarterms[i].lincoef) )
         ++nquadlinterms;
   }

   SCIP_CALL( SCIPallocBufferArray(scip, &quadvars,  nquadvars) );
   SCIP_CALL( SCIPallocBufferArray(scip, &quadelems, nquadelems) );
   SCIP_CALL( SCIPallocBufferArray(scip, &quadlinvars,  nquadlinterms) );
   SCIP_CALL( SCIPallocBufferArray(scip, &quadlincoefs, nquadlinterms) );

   lincnt = 0;
   elcnt = 0;
   for( i = 0; i < nquadvars; ++i )
   {
      quadvars[i] = consdata->quadvarterms[i].var;

      if( consdata->quadvarterms[i].sqrcoef != 0.0 )
      {
         assert(elcnt < nquadelems);
         quadelems[elcnt].idx1 = i;
         quadelems[elcnt].idx2 = i;
         quadelems[elcnt].coef = consdata->quadvarterms[i].sqrcoef;
         ++elcnt;
      }

      if( !SCIPisZero(scip, consdata->quadvarterms[i].lincoef) )
      {
         assert(lincnt < nquadlinterms);
         quadlinvars [lincnt] = consdata->quadvarterms[i].var;
         quadlincoefs[lincnt] = consdata->quadvarterms[i].lincoef;
         ++lincnt;
      }
   }
   assert(lincnt == nquadlinterms);

   /* bilinear terms are sorted first by first variable, then by second variable
    * thus, it makes sense to remember the index of the previous first variable for the case a series of bilinear terms with the same first var appears */
   lastvar = NULL;
   lastvaridx = -1;
   for( i = 0; i < consdata->nbilinterms; ++i )
   {
      if( lastvar == consdata->bilinterms[i].var1 )
      {
         assert(lastvaridx >= 0);
         assert(consdata->quadvarterms[lastvaridx].var == consdata->bilinterms[i].var1);
      }
      else
      {
         lastvar = consdata->bilinterms[i].var1;
         SCIP_CALL( consdataFindQuadVarTerm(scip, consdata, lastvar, &lastvaridx) );
      }
      idx1 = lastvaridx;

      SCIP_CALL( consdataFindQuadVarTerm(scip, consdata, consdata->bilinterms[i].var2, &idx2) );

      assert(elcnt < nquadelems);
      quadelems[elcnt].idx1 = MIN(idx1, idx2);
      quadelems[elcnt].idx2 = MAX(idx1, idx2);
      quadelems[elcnt].coef = consdata->bilinterms[i].coef;
      ++elcnt;
   }
   assert(elcnt == nquadelems);

   SCIP_CALL( SCIPcreateNlRow(scip, &consdata->nlrow, SCIPconsGetName(cons), 0.0,
      consdata->nlinvars, consdata->linvars, consdata->lincoefs,
      nquadvars, quadvars, nquadelems, quadelems,
      NULL, consdata->lhs, consdata->rhs) );

   SCIP_CALL( SCIPaddLinearCoefsToNlRow(scip, consdata->nlrow, nquadlinterms, quadlinvars, quadlincoefs) );
   
   SCIPfreeBufferArray(scip, &quadvars);
   SCIPfreeBufferArray(scip, &quadelems);
   SCIPfreeBufferArray(scip, &quadlinvars);
   SCIPfreeBufferArray(scip, &quadlincoefs);

   return SCIP_OKAY;
}

/** reformulates products of binary variables as AND constraint
 *  For a product x*y, with x and y binary variables, the product is replaced by a new auxiliary variable z and the constraint z = {x and y} is added.
 */
static
SCIP_RETCODE presolveTryAddAND(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONS*            cons,               /**< constraint */
   int*                  naddconss           /**< buffer where to add the number of AND constraints added */
   )
{
   SCIP_CONSDATA*     consdata;
   char               name[SCIP_MAXSTRLEN];
   SCIP_VAR*          vars[2];
   SCIP_VAR*          auxvar;
   SCIP_CONS*         andcons;
   int                i;
   int                ntodelete;
   int*               todelete;

   assert(scip != NULL);
   assert(cons != NULL);
   assert(naddconss != NULL);
   
   consdata = SCIPconsGetData(cons);
   assert(consdata != NULL);
   
   if( consdata->nbilinterms == 0 )
      return SCIP_OKAY;
   
   /* get array to store indices of bilinear terms that shall be deleted */
   SCIP_CALL( SCIPallocBufferArray(scip, &todelete, consdata->nbilinterms) );
   ntodelete = 0;
   
   for( i = 0; i < consdata->nbilinterms; ++i )
   {
      vars[0] = consdata->bilinterms[i].var1;
      if( !SCIPvarIsBinary(vars[0]) )
         continue;
      
      vars[1] = consdata->bilinterms[i].var2;
      if( !SCIPvarIsBinary(vars[1]) )
         continue;
      
      /* create auxiliary variable */ 
      (void)SCIPsnprintf(name, SCIP_MAXSTRLEN, "prod%s*%s", SCIPvarGetName(vars[0]), SCIPvarGetName(vars[1]));
      SCIP_CALL( SCIPcreateVar(scip, &auxvar, name, 0.0, 1.0, 0.0, SCIP_VARTYPE_BINARY, 
            TRUE, TRUE, NULL, NULL, NULL, NULL, NULL) );
      SCIP_CALL( SCIPaddVar(scip, auxvar) );

      /* create and constraint auxvar = x and y */
      (void)SCIPsnprintf(name, SCIP_MAXSTRLEN, "%sAND%s", SCIPvarGetName(vars[0]), SCIPvarGetName(vars[1]));
      SCIP_CALL( SCIPcreateConsAnd(scip, &andcons, name, auxvar, 2, vars,
         SCIPconsIsInitial(cons), SCIPconsIsSeparated(cons), SCIPconsIsEnforced(cons),
         SCIPconsIsChecked(cons), SCIPconsIsPropagated(cons), SCIPconsIsLocal(cons), SCIPconsIsModifiable(cons),
         SCIPconsIsDynamic(cons), SCIPconsIsRemovable(cons), SCIPconsIsStickingAtNode(cons)) );
      SCIP_CALL( SCIPaddCons(scip, andcons) );
      SCIPdebugMessage("added AND constraint: ");
      SCIPdebug( SCIPprintCons(scip, andcons, NULL) );
      SCIP_CALL( SCIPreleaseCons(scip, &andcons) );
      ++*naddconss;
      
      /* add bilincoef * auxvar to linear terms */
      SCIP_CALL( addLinearCoef(scip, cons, auxvar, consdata->bilinterms[i].coef) );
      SCIP_CALL( SCIPreleaseVar(scip, &auxvar) );

      /* remember that we have to delete this bilinear term */
      assert(ntodelete < consdata->nbilinterms);
      todelete[ntodelete++] = i;
   }

   /* remove bilinear terms that have been replaced */
   SCIP_CALL( removeBilinearTermsPos(scip, cons, ntodelete, todelete) );
   SCIPfreeBufferArray(scip, todelete);
   
   return SCIP_OKAY;
}

/** Reformulates products of binary times bounded continuous variables as system of linear inequalities (plus auxiliary variable).
 * 
 *  For a product x*y, with y a binary variable and x a continous variable with finite bounds,
 *  an auxiliary variable z and the inequalities \f$ x^L * y \leq z \leq x^U * y \f$ and \f$ x - (1-y)*x^U \leq z \leq x - (1-y)*x^L \f$ are added.
 * 
 *  If x is a linear term consisting of more than one variable, it is split up in groups of linear terms of length at most maxnrvar.
 *  For each product of linear term of length at most maxnrvar with y, an auxiliary z and linear inequalities are added.
 * 
 *  If y is a binary variable, the AND constraint \f$ z = x \wedge y \f$ may be added instead of linear constraints.
 */
static
SCIP_RETCODE presolveTryAddLinearReform(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONS*            cons,               /**< constraint */
   int*                  naddconss,          /**< buffer where to add the number of auxiliary constraints added */
   int                   maxnrvar,           /**< maximal number of variables in linear term to consider when replacing by one auxiliary variable */
   int                   empathy4and         /**< empathy for using AND constraint handler */
   )
{  /*lint --e{666} */
   SCIP_CONSDATA*     consdata;
   SCIP_VAR**         xvars;
   SCIP_Real*         xcoef;
   SCIP_INTERVAL      xbnds;
   SCIP_INTERVAL      tmp;
   int                nxvars;
   SCIP_VAR*          y;
   SCIP_VAR*          bvar;
   char               name[SCIP_MAXSTRLEN];
   int                nbilinterms;
   SCIP_VAR*          auxvar;
   SCIP_CONS*         auxcons;
   int                i;
   int                j;
   int                k;
   int                bilinidx;
   SCIP_Real          bilincoef;
   SCIP_Real          mincoef;
   SCIP_Real          maxcoef;
   int*               todelete;
   int                ntodelete;

   assert(scip != NULL);
   assert(cons != NULL);
   assert(naddconss != NULL);
   
   if( maxnrvar == 0 )
      return SCIP_OKAY;
   
   consdata = SCIPconsGetData(cons);
   assert(consdata != NULL);
   
   xvars = NULL;
   xcoef = NULL;
   todelete = NULL;
   
   for( i = 0; i < consdata->nquadvars; ++i )
   {
      y = consdata->quadvarterms[i].var;
      if( !SCIPvarIsBinary(y) )
         continue;
      
      nbilinterms = consdata->quadvarterms[i].nadjbilin;
      if( nbilinterms == 0 )
         continue;

      SCIP_CALL( SCIPreallocBufferArray(scip, &xvars, MIN(maxnrvar, nbilinterms)+2) ); /* add 2 for later use when creating linear constraints */
      SCIP_CALL( SCIPreallocBufferArray(scip, &xcoef, MIN(maxnrvar, nbilinterms)+2) );
      
      /* alloc array to store indices of bilinear terms that shall be deleted */
      SCIP_CALL( SCIPreallocBufferArray(scip, &todelete, nbilinterms) );
      ntodelete = 0;

      /* setup a list of bounded variables x_i with coefficients a_i that are multiplied with binary y: y*(sum_i a_i*x_i)
       * and compute range of sum_i a_i*x_i
       * we may need several rounds of maxnrvar < nbilinterms
       */
      j = 0;
      do
      {
         nxvars = 0;
         SCIPintervalSet(&xbnds, 0.0);

         mincoef = SCIPinfinity(scip);
         maxcoef = 0.0;

         /* collect at most maxnrvar variables for x term */
         for( ; j < nbilinterms && nxvars < maxnrvar; ++j )
         {
            bilinidx = consdata->quadvarterms[i].adjbilin[j];
            assert(bilinidx >= 0);
            assert(bilinidx < consdata->nbilinterms);
            
            bvar = consdata->bilinterms[bilinidx].var1;
            if( bvar == y )
               bvar = consdata->bilinterms[bilinidx].var2;
            assert(bvar != y);
            
            /* skip products with unbounded variables */
            if( SCIPisInfinity(scip, -SCIPvarGetLbGlobal(bvar)) || SCIPisInfinity(scip, SCIPvarGetUbGlobal(bvar)) )
            {
               continue;
            }

            bilincoef = consdata->bilinterms[bilinidx].coef;

            /* add bvar to x term */  
            xvars[nxvars] = bvar;
            xcoef[nxvars] = bilincoef;
            ++nxvars;

            /* update bounds on x term */
            SCIPintervalSetBounds(&tmp, MIN(SCIPvarGetLbGlobal(bvar), SCIPvarGetUbGlobal(bvar)), MAX(SCIPvarGetLbGlobal(bvar), SCIPvarGetUbGlobal(bvar)));
            SCIPintervalMulScalar(SCIPinfinity(scip), &tmp, tmp, bilincoef);
            SCIPintervalAdd(SCIPinfinity(scip), &xbnds, xbnds, tmp);

            if( ABS(bilincoef) < mincoef )
               mincoef = ABS(bilincoef);
            if( ABS(bilincoef) > maxcoef )
               maxcoef = ABS(bilincoef);
            
            /* remember that we have to remove this bilinear term later */
            assert(ntodelete < nbilinterms);
            todelete[ntodelete++] = bilinidx;
         }

         if( nxvars == 0 ) /* all (remaining) x_j seem to be unbounded */
            break;
         
         assert(!SCIPisInfinity(scip, -SCIPintervalGetInf(xbnds)));
         assert(!SCIPisInfinity(scip,  SCIPintervalGetSup(xbnds)));
         
         if( nxvars == 1 && empathy4and >= 1 && SCIPvarIsBinary(xvars[0]) )
         {
            /* product of two binary variables, replace by auxvar and AND constraint */
            /* add auxiliary variable z */
            (void)SCIPsnprintf(name, SCIP_MAXSTRLEN, "prod%s*%s", SCIPvarGetName(y), SCIPvarGetName(xvars[0]));
            SCIP_CALL( SCIPcreateVar(scip, &auxvar, name, 0.0, 1.0, 0.0, SCIP_VARTYPE_BINARY,
                  TRUE, TRUE, NULL, NULL, NULL, NULL, NULL) );
            SCIP_CALL( SCIPaddVar(scip, auxvar) );

            /* add constraint z = x and y */
            xvars[1] = y;
            (void)SCIPsnprintf(name, SCIP_MAXSTRLEN, "%sAND%s", SCIPvarGetName(y), SCIPvarGetName(xvars[0]));
            SCIP_CALL( SCIPcreateConsAnd(scip, &auxcons, name, auxvar, 2, xvars,
               SCIPconsIsInitial(cons), SCIPconsIsSeparated(cons), SCIPconsIsEnforced(cons),
               SCIPconsIsChecked(cons), SCIPconsIsPropagated(cons),  SCIPconsIsLocal(cons), SCIPconsIsModifiable(cons),
               SCIPconsIsDynamic(cons), SCIPconsIsRemovable(cons),   SCIPconsIsStickingAtNode(cons)) );
            SCIP_CALL( SCIPaddCons(scip, auxcons) );
            SCIPdebugMessage("added AND constraint: ");
            SCIPdebug( SCIPprintCons(scip, auxcons, NULL) );
            SCIP_CALL( SCIPreleaseCons(scip, &auxcons) );
            ++*naddconss;
            
            /* add linear term coef*auxvar */
            SCIP_CALL( addLinearCoef(scip, cons, auxvar, xcoef[0]) );

            /* forget about auxvar */
            SCIP_CALL( SCIPreleaseVar(scip, &auxvar) );
         }
         else
         {
            /* product of binary avariable with more than one binary or with continuous variables or with binary and user did not like AND -> replace by auxvar and linear constraints */
            SCIP_Real scale = 1.0;
            if( maxcoef < 0.5 )
               scale = maxcoef;
            if( mincoef > 2.0 )
               scale = mincoef;
            if( scale != 1.0 )
            {
               /* scale will be a cofficient in a linear constraint -> there is no harm in choosing a "nice value" for it */
               scale = SCIPselectSimpleValue(scale / 2.0, 1.5 * scale, MAXDNOM);
               
               SCIPdebugMessage("binary reformulation using scale %g\n", scale);
               SCIPintervalDivScalar(SCIPinfinity(scip), &xbnds, xbnds, scale);
               for( k = 0; k < nxvars; ++k )
                  xcoef[k] /= scale;
            }
            
            /* add auxiliary variable z */
            if( nxvars == 1 )
               (void)SCIPsnprintf(name, SCIP_MAXSTRLEN, "prod%s*%s", SCIPvarGetName(y), SCIPvarGetName(xvars[0]));
            else
               (void)SCIPsnprintf(name, SCIP_MAXSTRLEN, "prod%s*%s*more", SCIPvarGetName(y), SCIPvarGetName(xvars[0]));
            SCIP_CALL( SCIPcreateVar(scip, &auxvar, name, MIN(0., SCIPintervalGetInf(xbnds)), MAX(0., SCIPintervalGetSup(xbnds)), 
                  0.0, SCIP_VARTYPE_CONTINUOUS, TRUE, TRUE, NULL, NULL, NULL, NULL, NULL) );
            SCIP_CALL( SCIPaddVar(scip, auxvar) );

            /* TODO: what about making not all constraint initial here? */
            if( !SCIPisZero(scip, SCIPintervalGetInf(xbnds)) )
            { /* add 0 <= z - xbnds.inf * y constraint (as varbound constraint) */
               (void)SCIPsnprintf(name, SCIP_MAXSTRLEN, "linreform%s_1", SCIPvarGetName(y));
               SCIP_CALL( SCIPcreateConsVarbound(scip, &auxcons, name, auxvar, y, -SCIPintervalGetInf(xbnds), 0.0, SCIPinfinity(scip),
                  SCIPconsIsInitial(cons), SCIPconsIsSeparated(cons), SCIPconsIsEnforced(cons),
                  SCIPconsIsChecked(cons), SCIPconsIsPropagated(cons),  SCIPconsIsLocal(cons), SCIPconsIsModifiable(cons),
                  SCIPconsIsDynamic(cons), SCIPconsIsRemovable(cons),   SCIPconsIsStickingAtNode(cons)) );
               SCIP_CALL( SCIPaddCons(scip, auxcons) );
               SCIPdebugMessage("added varbound constraint: ");
               SCIPdebug( SCIPprintCons(scip, auxcons, NULL) );
               SCIP_CALL( SCIPreleaseCons(scip, &auxcons) );
               ++*naddconss;
            }
            if( !SCIPisZero(scip, SCIPintervalGetSup(xbnds)) )
            { /* add z - xbnds.sup * y <= 0 constraint (as varbound constraint) */
               (void)SCIPsnprintf(name, SCIP_MAXSTRLEN, "linreform%s_2", SCIPvarGetName(y));
               SCIP_CALL( SCIPcreateConsVarbound(scip, &auxcons, name, auxvar, y, -SCIPintervalGetSup(xbnds), -SCIPinfinity(scip), 0.0,
                  SCIPconsIsInitial(cons), SCIPconsIsSeparated(cons), SCIPconsIsEnforced(cons),
                  SCIPconsIsChecked(cons), SCIPconsIsPropagated(cons),  SCIPconsIsLocal(cons), SCIPconsIsModifiable(cons),
                  SCIPconsIsDynamic(cons), SCIPconsIsRemovable(cons),   SCIPconsIsStickingAtNode(cons)) );
               SCIP_CALL( SCIPaddCons(scip, auxcons) );
               SCIPdebug( SCIPdebugMessage("added varbound constraint: ") );
               SCIP_CALL( SCIPreleaseCons(scip, &auxcons) );
               ++*naddconss;
            }

            /* add xbnds.inf <= sum_i a_i*x_i + xbnds.inf * y - z constraint */
            xvars[nxvars]   = y;
            xvars[nxvars+1] = auxvar;
            xcoef[nxvars]   = SCIPintervalGetInf(xbnds);
            xcoef[nxvars+1] = -1;

            (void)SCIPsnprintf(name, SCIP_MAXSTRLEN, "linreform%s_3", SCIPvarGetName(y));
            SCIP_CALL( SCIPcreateConsLinear(scip, &auxcons, name, nxvars+2, xvars, xcoef, SCIPintervalGetInf(xbnds), SCIPinfinity(scip),
               SCIPconsIsInitial(cons), SCIPconsIsSeparated(cons), SCIPconsIsEnforced(cons),
               SCIPconsIsChecked(cons), SCIPconsIsPropagated(cons), SCIPconsIsLocal(cons), SCIPconsIsModifiable(cons),
               SCIPconsIsDynamic(cons), SCIPconsIsRemovable(cons), SCIPconsIsStickingAtNode(cons)) );
            SCIP_CALL( SCIPaddCons(scip, auxcons) );
            SCIPdebugMessage("added linear constraint: ");
            SCIPdebug( SCIPprintCons(scip, auxcons, NULL) );
            SCIP_CALL( SCIPreleaseCons(scip, &auxcons) );
            ++*naddconss;

            /* add sum_i a_i*x_i + xbnds.sup * y - z <= xbnds.sup constraint */
            xcoef[nxvars] = SCIPintervalGetSup(xbnds);

            (void)SCIPsnprintf(name, SCIP_MAXSTRLEN, "linreform%s_4", SCIPvarGetName(y));
            SCIP_CALL( SCIPcreateConsLinear(scip, &auxcons, name, nxvars+2, xvars, xcoef, -SCIPinfinity(scip), SCIPintervalGetSup(xbnds),
               SCIPconsIsInitial(cons), SCIPconsIsSeparated(cons), SCIPconsIsEnforced(cons),
               SCIPconsIsChecked(cons), SCIPconsIsPropagated(cons), SCIPconsIsLocal(cons), SCIPconsIsModifiable(cons),
               SCIPconsIsDynamic(cons), SCIPconsIsRemovable(cons), SCIPconsIsStickingAtNode(cons)) );
            SCIP_CALL( SCIPaddCons(scip, auxcons) );
            SCIPdebugMessage("added linear constraint: ");
            SCIPdebug( SCIPprintCons(scip, auxcons, NULL) );
            SCIP_CALL( SCIPreleaseCons(scip, &auxcons) );
            ++*naddconss;

            /* add linear term scale*auxvar to this constraint */
            SCIP_CALL( addLinearCoef(scip, cons, auxvar, scale) );

            /* forget about auxvar */
            SCIP_CALL( SCIPreleaseVar(scip, &auxvar) );
         }
      }
      while( j < nbilinterms );
    
      /* remove bilinear terms that have been replaced */
      SCIP_CALL( removeBilinearTermsPos(scip, cons, ntodelete, todelete) );
   }

   SCIPfreeBufferArrayNull(scip, &xvars);
   SCIPfreeBufferArrayNull(scip, &xcoef);
   SCIPfreeBufferArrayNull(scip, &todelete);

   return SCIP_OKAY;
}

/** tries to automatically convert a quadratic constraint (or a part of it) into a more specific and more specialized constraint */
static
SCIP_RETCODE presolveUpgrade(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONSHDLR*        conshdlr,           /**< constraint handler data structure */
   SCIP_CONS*            cons,               /**< source constraint to try to convert */
   int*                  nupgdconss,         /**< number of constraints that are an upgrade of this quadratic constraint */
   SCIP_CONS***          upgdconss           /**< array of constraints that are an upgrade of this quadratic constraint */
   )
{
   SCIP_CONSHDLRDATA* conshdlrdata;
   SCIP_CONSDATA* consdata;
   SCIP_VAR* var;
   SCIP_Real lincoef;
   SCIP_Real quadcoef;
   SCIP_Real lb;
   SCIP_Real ub;
   int nbinlin;
   int nbinquad;
   int nintlin;
   int nintquad;
   int nimpllin;
   int nimplquad;
   int ncontlin;
   int ncontquad;
   SCIP_Bool integral;
   int i;
   int upgdconsssize;

   assert(nupgdconss != NULL);
   assert( upgdconss != NULL);

   *nupgdconss = 0;
   *upgdconss  = NULL;

   /* we cannot upgrade a modifiable quadratic constraint, since we don't know what additional coefficients to expect */
   if( SCIPconsIsModifiable(cons) )
      return SCIP_OKAY;

   consdata = SCIPconsGetData(cons);
   assert(consdata != NULL);
   
   /* skip check for upgrades, if constraint has been checked already and there is no pending bound tightening */
   if( consdata->ispresolved && consdata->ispropagated )
      return SCIP_OKAY;

   conshdlrdata = SCIPconshdlrGetData(conshdlr);
   assert(conshdlrdata != NULL);
   
   /* if there are no upgrade methods, we can also stop */
   if( conshdlrdata->nquadconsupgrades == 0 )
      return SCIP_OKAY;

   upgdconsssize = 2;
   SCIP_CALL( SCIPallocBufferArray(scip, upgdconss, upgdconsssize) );

   /* calculate some statistics on quadratic constraint */
   nbinlin   = 0;
   nbinquad  = 0;
   nintlin   = 0;
   nintquad  = 0;
   nimpllin  = 0;
   nimplquad = 0;
   ncontlin  = 0;
   ncontquad = 0;
   integral  = TRUE;
   for( i = 0; i < consdata->nlinvars; ++i )
   {
      var = consdata->linvars[i];
      lincoef = consdata->lincoefs[i];
      lb  = SCIPvarGetLbLocal(var);
      ub  = SCIPvarGetUbLocal(var);
      assert(!SCIPisZero(scip, lincoef));

      switch( SCIPvarGetType(var) )
      {
      case SCIP_VARTYPE_BINARY:
         if( !SCIPisZero(scip, lb) || !SCIPisZero(scip, ub) )
            integral = integral && SCIPisIntegral(scip, lincoef);
         nbinlin++;
         break;
      case SCIP_VARTYPE_INTEGER:
         if( !SCIPisZero(scip, lb) || !SCIPisZero(scip, ub) )
            integral = integral && SCIPisIntegral(scip, lincoef);
         nintlin++;
         break;
      case SCIP_VARTYPE_IMPLINT:
         if( !SCIPisZero(scip, lb) || !SCIPisZero(scip, ub) )
            integral = integral && SCIPisIntegral(scip, lincoef);
         nimpllin++;
         break;
      case SCIP_VARTYPE_CONTINUOUS:
         integral = integral && SCIPisRelEQ(scip, lb, ub) && SCIPisIntegral(scip, lincoef * lb);
         ncontlin++;
         break;
      default:
         SCIPerrorMessage("unknown variable type\n");
         return SCIP_INVALIDDATA;
      }
   }

   for( i = 0; i < consdata->nquadvars; ++i )
   {
      var = consdata->quadvarterms[i].var;
      lincoef  = consdata->quadvarterms[i].lincoef;
      quadcoef = consdata->quadvarterms[i].sqrcoef;
      lb  = SCIPvarGetLbLocal(var);
      ub  = SCIPvarGetUbLocal(var);

      switch( SCIPvarGetType(var) )
      {
      case SCIP_VARTYPE_BINARY:
         if( !SCIPisZero(scip, lb) || !SCIPisZero(scip, ub) )
            integral = integral && SCIPisIntegral(scip, lincoef) && SCIPisIntegral(scip, quadcoef);
         nbinquad++;
         break;
      case SCIP_VARTYPE_INTEGER:
         if( !SCIPisZero(scip, lb) || !SCIPisZero(scip, ub) )
            integral = integral && SCIPisIntegral(scip, lincoef) && SCIPisIntegral(scip, quadcoef);
         nintquad++;
         break;
      case SCIP_VARTYPE_IMPLINT:
         if( !SCIPisZero(scip, lb) || !SCIPisZero(scip, ub) )
            integral = integral && SCIPisIntegral(scip, lincoef) && SCIPisIntegral(scip, quadcoef);
         nimplquad++;
         break;
      case SCIP_VARTYPE_CONTINUOUS:
         integral = integral && SCIPisRelEQ(scip, lb, ub) && SCIPisIntegral(scip, lincoef * lb + quadcoef * lb * lb);
         ncontquad++;
         break;
      default:
         SCIPerrorMessage("unknown variable type\n");
         return SCIP_INVALIDDATA;
      }
   }
   
   if( integral )
   {
      for( i = 0; i < consdata->nbilinterms && integral; ++i )
      {
         if( SCIPvarGetType(consdata->bilinterms[i].var1) < SCIP_VARTYPE_CONTINUOUS && SCIPvarGetType(consdata->bilinterms[i].var2) < SCIP_VARTYPE_CONTINUOUS )
            integral = integral && SCIPisIntegral(scip, consdata->bilinterms[i].coef);
         else
            integral = FALSE;
      }
   }

   /* call the upgrading methods */

   SCIPdebugMessage("upgrading quadratic constraint <%s> (%d upgrade methods):\n",
      SCIPconsGetName(cons), conshdlrdata->nquadconsupgrades);
   SCIPdebugMessage(" binlin=%d binquad=%d intlin=%d intquad=%d impllin=%d implquad=%d contlin=%d contquad=%d integral=%u\n",
      nbinlin, nbinquad, nintlin, nintquad, nimpllin, nimplquad, ncontlin, ncontquad, integral);
   SCIPdebug( SCIPprintCons(scip, cons, NULL) );

   /* try all upgrading methods in priority order in case the upgrading step is enable  */
   for( i = 0; i < conshdlrdata->nquadconsupgrades; ++i )
   {
      if( conshdlrdata->quadconsupgrades[i]->active )
      {
         SCIP_CALL( conshdlrdata->quadconsupgrades[i]->quadconsupgd(scip, cons,
            nbinlin, nbinquad, nintlin, nintquad, nimpllin, nimplquad, ncontlin, ncontquad, integral,
            nupgdconss, *upgdconss, upgdconsssize) );

         while( *nupgdconss < 0 )
         {
            /* upgrade function requires more memory: resize upgdconss and call again */
            assert(-*nupgdconss > upgdconsssize);
            upgdconsssize = -*nupgdconss;
            SCIP_CALL( SCIPreallocBufferArray(scip, upgdconss, -*nupgdconss) );

            SCIP_CALL( conshdlrdata->quadconsupgrades[i]->quadconsupgd(scip, cons,
               nbinlin, nbinquad, nintlin, nintquad, nimpllin, nimplquad, ncontlin, ncontquad, integral,
               nupgdconss, *upgdconss, upgdconsssize) );

            assert(*nupgdconss != 0);
         }

         if( *nupgdconss > 0 )
         { /* got upgrade */
#ifdef SCIP_DEBUG
            int j;

            SCIP_CALL( SCIPprintCons(scip, cons, NULL) );
            SCIPdebugMessage(" -> upgraded to %d constraints:\n", *nupgdconss);
            for( j = 0; j < *nupgdconss; ++j )
            {
               SCIPdebugPrintf("\t");
               SCIP_CALL( SCIPprintCons(scip, (*upgdconss)[j], NULL) );
            }
#endif
            break;
         }
      }
   }

   assert(*nupgdconss >= 0);
   if( *nupgdconss == 0 )
   {
      SCIPfreeBufferArray(scip, upgdconss);
   }

   return SCIP_OKAY;
}

/** helper function for presolveDisaggregate */
static
SCIP_RETCODE presolveDisaggregateMarkComponent(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONSDATA*        consdata,           /**< constraint data */
   int                   quadvaridx,         /**< index of quadratic variable to mark */
   SCIP_HASHMAP*         var2component,      /**< variables to components mapping */
   int                   componentnr         /**< the component number to mark to */ 
   )
{
   SCIP_QUADVARTERM* quadvarterm;
   SCIP_VAR* othervar;
   int othervaridx;
   int i;
   
   assert(consdata != NULL);
   assert(quadvaridx >= 0);
   assert(quadvaridx < consdata->nquadvars);
   assert(var2component != NULL);
   assert(componentnr >= 0);
   
   quadvarterm = &consdata->quadvarterms[quadvaridx];
   
   if( SCIPhashmapExists(var2component, quadvarterm->var) )
   {
      /* if we saw the variable before, then it should have the same component number */
      assert((int)(size_t)SCIPhashmapGetImage(var2component, quadvarterm->var) == componentnr);
      return SCIP_OKAY;
   }
   
   /* assign component number to variable */
   SCIP_CALL( SCIPhashmapInsert(var2component, quadvarterm->var, (void*)(size_t)componentnr) );
   
   /* assign same component number to all variables this variable is multiplied with */
   for( i = 0; i < quadvarterm->nadjbilin; ++i )
   {
      othervar = consdata->bilinterms[quadvarterm->adjbilin[i]].var1 == quadvarterm->var ?
         consdata->bilinterms[quadvarterm->adjbilin[i]].var2 :
         consdata->bilinterms[quadvarterm->adjbilin[i]].var1;
      SCIP_CALL( consdataFindQuadVarTerm(scip, consdata, othervar, &othervaridx) );
      assert(othervaridx >= 0);
      SCIP_CALL( presolveDisaggregateMarkComponent(scip, consdata, othervaridx, var2component, componentnr) );
   }

   return SCIP_OKAY;
}

/** for quadratic constraints that consists of a sum of quadratic terms, disaggregates the sum into a set of constraints by introducing auxiliary variables */
static
SCIP_RETCODE presolveDisaggregate(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONSHDLR*        conshdlr,           /**< constraint handler data structure */
   SCIP_CONS*            cons,               /**< source constraint to try to convert */
   SCIP_Bool*            success,            /**< buffer to store whether a disaggregation was done */
   int*                  naddconss           /**< pointer to counter of added constraints */
   )
{
   SCIP_CONSDATA* consdata;
   SCIP_HASHMAP* var2component;
   int ncomponents;
   int i;
   int comp;
   SCIP_CONS** auxconss;
   SCIP_VAR** auxvars;
   SCIP_Real* auxcoefs;
   char name[SCIP_MAXSTRLEN];
   
   assert(scip != NULL);
   assert(conshdlr != NULL);
   assert(cons != NULL);
   assert(success != NULL);
   assert(naddconss != NULL);
   
   consdata = SCIPconsGetData(cons);
   assert(consdata != NULL);
   
   *success = FALSE;
   
   if( consdata->nquadvars <= 1 )
      return SCIP_OKAY;

   /* sort quadratic variable terms here, so we can later search in it without reordering the array */
   SCIP_CALL( consdataSortQuadVarTerms(scip, consdata) );
   
   /* check how many quadratic terms with non-overlapping variables we have
    * in other words, the number of components in the sparsity graph of the quadratic term matrix */
   ncomponents = 0;
   SCIP_CALL( SCIPhashmapCreate(&var2component, SCIPblkmem(scip), SCIPcalcHashtableSize(consdata->nquadvars)) );
   for( i = 0; i < consdata->nquadvars; ++i )
   {
      /* if variable was marked already, skip it */
      if( SCIPhashmapExists(var2component, (void*)consdata->quadvarterms[i].var) )
         continue;
      
      SCIP_CALL( presolveDisaggregateMarkComponent(scip, consdata, i, var2component, ncomponents) );
      ++ncomponents;
   }
   assert(ncomponents >= 1);
   
   /* if there is only one component, we cannot disaggregate
    * @todo we could still split the constraint into several while keeping the number of variables sharing several constraints as small as possible */
   if( ncomponents == 1 )
   {
      SCIPhashmapFree(&var2component);
      return SCIP_OKAY;
   }

   SCIP_CALL( SCIPallocBufferArray(scip, &auxconss, ncomponents) );
   SCIP_CALL( SCIPallocBufferArray(scip, &auxvars,  ncomponents) );
   SCIP_CALL( SCIPallocBufferArray(scip, &auxcoefs, ncomponents) );
   
   /* create auxiliary variables and empty constraints for each component */
   for( comp = 0; comp < ncomponents; ++comp )
   {
      (void) SCIPsnprintf(name, SCIP_MAXSTRLEN, "%s_comp%d", SCIPconsGetName(cons), comp);
      
      SCIP_CALL( SCIPcreateVar(scip, &auxvars[comp], name, -SCIPinfinity(scip), SCIPinfinity(scip), 0.0,
         SCIP_VARTYPE_CONTINUOUS, SCIPconsIsInitial(cons), TRUE, NULL, NULL, NULL, NULL, NULL) );
      
      SCIP_CALL( SCIPcreateConsQuadratic2(scip, &auxconss[comp], name, 0, NULL, NULL, 0, NULL, 0, NULL,
         SCIPisInfinity(scip, -consdata->lhs) ? -SCIPinfinity(scip) : 0.0,
         SCIPisInfinity(scip,  consdata->rhs) ?  SCIPinfinity(scip) : 0.0,
         SCIPconsIsInitial(cons), SCIPconsIsSeparated(cons), SCIPconsIsEnforced(cons),
         SCIPconsIsChecked(cons), SCIPconsIsPropagated(cons), SCIPconsIsLocal(cons), SCIPconsIsModifiable(cons),
         SCIPconsIsDynamic(cons), SCIPconsIsRemovable(cons)) );
      
      auxcoefs[comp] = SCIPinfinity(scip);
   }
   
   /* add quadratic variables to each component constraint
    * delete adjacency information */
   for( i = 0; i < consdata->nquadvars; ++i )
   {
      comp = (int)(size_t) SCIPhashmapGetImage(var2component, consdata->quadvarterms[i].var);
      assert(comp >= 0);
      assert(comp < ncomponents);
      
      /* add variable term to corresponding constraint */
      SCIP_CALL( SCIPaddQuadVarQuadratic(scip, auxconss[comp], consdata->quadvarterms[i].var, consdata->quadvarterms[i].lincoef, consdata->quadvarterms[i].sqrcoef) );
      
      /* reduce coefficient of aux variable */
      if( !SCIPisZero(scip, consdata->quadvarterms[i].lincoef) && ABS(consdata->quadvarterms[i].lincoef) < auxcoefs[comp] )
         auxcoefs[comp] = ABS(consdata->quadvarterms[i].lincoef);
      if( !SCIPisZero(scip, consdata->quadvarterms[i].sqrcoef) && ABS(consdata->quadvarterms[i].sqrcoef) < auxcoefs[comp] )
         auxcoefs[comp] = ABS(consdata->quadvarterms[i].sqrcoef);
      
      SCIPfreeBlockMemoryArray(scip, &consdata->quadvarterms[i].adjbilin, consdata->quadvarterms[i].adjbilinsize);
      consdata->quadvarterms[i].nadjbilin = 0;
      consdata->quadvarterms[i].adjbilinsize = 0;
   }
   
   /* add bilinear terms to each component constraint */
   for( i = 0; i < consdata->nbilinterms; ++i )
   {
      comp = (int)(size_t) SCIPhashmapGetImage(var2component, consdata->bilinterms[i].var1);
      assert(comp == (int)(size_t) SCIPhashmapGetImage(var2component, consdata->bilinterms[i].var2));
      assert(!SCIPisZero(scip, consdata->bilinterms[i].coef));
      
      SCIP_CALL( SCIPaddBilinTermQuadratic(scip, auxconss[comp], 
         consdata->bilinterms[i].var1, consdata->bilinterms[i].var2, consdata->bilinterms[i].coef) );
      
      if( ABS(consdata->bilinterms[i].coef) < auxcoefs[comp] )
         auxcoefs[comp] = ABS(consdata->bilinterms[i].coef);
   }

   /* forget about bilinear terms in cons */
   SCIPfreeBlockMemoryArray(scip, &consdata->bilinterms, consdata->bilintermssize);
   consdata->nbilinterms = 0;
   consdata->bilintermssize = 0;
   
   /* remove quadratic variable terms from cons */
   for( i = consdata->nquadvars - 1; i >= 0; --i )
   {
      SCIP_CALL( delQuadVarTermPos(scip, cons, i) );
   }
   assert(consdata->nquadvars == 0);

   /* add auxiliary variables to auxiliary constraints
    * add aux vars and constraints to SCIP 
    * add aux vars to this constraint */
   SCIPdebugMessage("add %d constraints for disaggregation of quadratic constraint <%s>\n", ncomponents, SCIPconsGetName(cons));
   SCIP_CALL( consdataEnsureLinearVarsSize(scip, consdata, consdata->nlinvars + ncomponents) );
   for( comp = 0; comp < ncomponents; ++comp )
   {
      SCIP_CALL( SCIPaddLinearVarQuadratic(scip, auxconss[comp], auxvars[comp], -auxcoefs[comp]) );
      
      SCIP_CALL( SCIPaddVar(scip, auxvars[comp]) );
      
      SCIP_CALL( SCIPaddCons(scip, auxconss[comp]) );
      SCIPdebug( SCIPprintCons(scip, auxconss[comp], NULL) );
      
      SCIP_CALL( addLinearCoef(scip, cons, auxvars[comp], 1.0 / auxcoefs[comp]) );
      
      SCIP_CALL( SCIPreleaseCons(scip, &auxconss[comp]) );
      SCIP_CALL( SCIPreleaseVar(scip, &auxvars[comp]) );
   }
   *naddconss += ncomponents;
   
   SCIPdebug( SCIPprintCons(scip, cons, NULL) );

   SCIPfreeBufferArray(scip, &auxconss);
   SCIPfreeBufferArray(scip, &auxvars);
   SCIPfreeBufferArray(scip, &auxcoefs);
   SCIPhashmapFree(&var2component);
   
   *success = TRUE;
   
   return SCIP_OKAY;
}

/** checks a quadratic constraint for convexity and/or concavity */
static
SCIP_RETCODE checkCurvature(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONS*            cons,               /**< quadratic constraint */
   SCIP_Bool             checkmultivariate   /**< whether curvature should also be checked for multivariate functions */
   )
{
   SCIP_CONSDATA* consdata;
   double*        matrix;
   SCIP_HASHMAP*  var2index;
   int            i;
   int            n;
   int            nn;
   int            row;
   int            col;
   double*        alleigval;

   assert(scip != NULL);
   assert(cons != NULL);

   consdata = SCIPconsGetData(cons);
   assert(consdata != NULL);

   n = consdata->nquadvars;

   if( consdata->iscurvchecked )
      return SCIP_OKAY;
   
   SCIPdebugMessage("Checking curvature of constraint <%s>\n", SCIPconsGetName(cons));

   if( n == 1 )
   {
      assert(consdata->nbilinterms == 0);
      consdata->isconvex      = !SCIPisNegative(scip, consdata->quadvarterms[0].sqrcoef);
      consdata->isconcave     = !SCIPisPositive(scip, consdata->quadvarterms[0].sqrcoef);
      consdata->iscurvchecked = TRUE;
      return SCIP_OKAY;
   }

   if( n == 0 )
   {
      consdata->isconvex = TRUE;
      consdata->isconcave = TRUE;
      consdata->iscurvchecked = TRUE;
      return SCIP_OKAY;
   }

   if( consdata->nbilinterms == 0 )
   {
      consdata->isconvex = TRUE;
      consdata->isconcave = TRUE;
      for( i = 0; i < n; ++i )
      {
         consdata->isconvex  = consdata->isconvex  && !SCIPisNegative(scip, consdata->quadvarterms[i].sqrcoef);
         consdata->isconcave = consdata->isconcave && !SCIPisPositive(scip, consdata->quadvarterms[i].sqrcoef);
      }
      consdata->iscurvchecked = TRUE;
      return SCIP_OKAY;
   }

   if( !checkmultivariate )
   {
      consdata->isconvex  = FALSE;
      consdata->isconcave = FALSE;
      consdata->iscurvchecked = TRUE;
      return SCIP_OKAY;
   }

   if( n == 2 )
   { /* compute eigenvalues by hand */
      assert(consdata->nbilinterms == 1);
      consdata->isconvex =
         consdata->quadvarterms[0].sqrcoef >= 0 &&
         consdata->quadvarterms[1].sqrcoef >= 0 &&
         4 * consdata->quadvarterms[0].sqrcoef * consdata->quadvarterms[1].sqrcoef >= consdata->bilinterms[0].coef * consdata->bilinterms[0].coef;
      consdata->isconcave = 
         consdata->quadvarterms[0].sqrcoef <= 0 &&
         consdata->quadvarterms[1].sqrcoef <= 0 &&
         4 * consdata->quadvarterms[0].sqrcoef * consdata->quadvarterms[1].sqrcoef >= consdata->bilinterms[0].coef * consdata->bilinterms[0].coef;
      consdata->iscurvchecked = TRUE;
      return SCIP_OKAY;
   }

   /* lower triangular of quadratic term matrix, scaled by box diameter */
   nn = n * n;
   SCIP_CALL( SCIPallocBufferArray(scip, &matrix, nn) );
   BMSclearMemoryArray(matrix, nn);

   consdata->isconvex  = TRUE;
   consdata->isconcave = TRUE;

   SCIP_CALL( SCIPhashmapCreate(&var2index, SCIPblkmem(scip), SCIPcalcHashtableSize(5 * n)) );
   for( i = 0; i < n; ++i )
   {
      if( consdata->quadvarterms[i].nadjbilin > 0 )
      {
         SCIP_CALL( SCIPhashmapInsert(var2index, consdata->quadvarterms[i].var, (void*)(size_t)i) );
         matrix[i*n + i] = consdata->quadvarterms[i].sqrcoef;
      }
      /* nonzero elements on diagonal tell a lot about convexity/concavity */
      if( SCIPisNegative(scip, consdata->quadvarterms[i].sqrcoef) )
         consdata->isconvex  = FALSE;
      if( SCIPisPositive(scip, consdata->quadvarterms[i].sqrcoef) )
         consdata->isconcave = FALSE;
   }

   if( !consdata->isconvex && !consdata->isconcave )
   {
      SCIPfreeBufferArray(scip, &matrix);
      SCIPhashmapFree(&var2index);
      consdata->iscurvchecked = TRUE;
      return SCIP_OKAY;
   }

   if( SCIPisIpoptAvailableIpopt() )
   {
      for( i = 0; i < consdata->nbilinterms; ++i )
      {
         assert(SCIPhashmapExists(var2index, consdata->bilinterms[i].var1));
         assert(SCIPhashmapExists(var2index, consdata->bilinterms[i].var2));
         row = (int)(size_t)SCIPhashmapGetImage(var2index, consdata->bilinterms[i].var1);
         col = (int)(size_t)SCIPhashmapGetImage(var2index, consdata->bilinterms[i].var2);
         if( row < col )
            matrix[row * n + col] = consdata->bilinterms[i].coef/2;
         else
            matrix[col * n + row] = consdata->bilinterms[i].coef/2;
      }

      SCIP_CALL( SCIPallocBufferArray(scip, &alleigval, n) );
      /* TODO can we compute only min and max eigval?
      TODO can we estimate the numerical error? 
      TODO trying a cholesky factorization may be much faster */
      if( LapackDsyev(FALSE, n, matrix, alleigval) != SCIP_OKAY )
      {
         SCIPwarningMessage("Failed to compute eigenvalues of quadratic coefficient matrix of constraint %s. Assuming matrix is indefinite.\n", SCIPconsGetName(cons));
         consdata->isconvex = FALSE;
         consdata->isconcave = FALSE;
      }
      else
      {
#if 0
         SCIP_Bool allbinary;
         printf("cons <%s>[%g,%g] spectrum = [%g,%g]\n", SCIPconsGetName(cons), consdata->lhs, consdata->rhs, alleigval[0], alleigval[n-1]);
#endif
         consdata->isconvex  &= !SCIPisNegative(scip, alleigval[0]);   /*lint !e514*/
         consdata->isconcave &= !SCIPisPositive(scip, alleigval[n-1]); /*lint !e514*/
         consdata->iscurvchecked = TRUE;
#if 0
         for( i = 0; i < consdata->nquadvars; ++i )
            if( !SCIPvarIsBinary(consdata->quadvarterms[i].var) )
               break;
         allbinary = i == consdata->nquadvars;

         if( !SCIPisInfinity(scip, consdata->rhs) && alleigval[0] > 0.1 && allbinary )
         {
            printf("deconvexify cons <%s> by shifting hessian by %g\n", SCIPconsGetName(cons), alleigval[0]);
            for( i = 0; i < consdata->nquadvars; ++i )
            {
               consdata->quadvarterms[i].sqrcoef -= alleigval[0];
               consdata->quadvarterms[i].lincoef += alleigval[0];
            }
         }

         if( !SCIPisInfinity(scip, consdata->lhs) && alleigval[n-1] < -0.1 && allbinary )
         {
            printf("deconcavify cons <%s> by shifting hessian by %g\n", SCIPconsGetName(cons), alleigval[n-1]);
            for( i = 0; i < consdata->nquadvars; ++i )
            {
               consdata->quadvarterms[i].sqrcoef -= alleigval[n-1];
               consdata->quadvarterms[i].lincoef += alleigval[n-1];
            }
         }
#endif
      }

      SCIPfreeBufferArray(scip, &alleigval);
   }
   else
   {
      consdata->isconvex = FALSE;
      consdata->isconcave = FALSE;
      consdata->iscurvchecked = TRUE; /* set to TRUE since it does not help to repeat this procedure again and again (that will not bring Ipopt in) */
   }
   
   SCIPhashmapFree(&var2index);
   SCIPfreeBufferArray(scip, &matrix);

   return SCIP_OKAY;
}

/** sets bounds for variables in not evidently convex terms to some predefined value */
static
SCIP_RETCODE boundUnboundedVars(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONS*            cons,               /**< constraint */
   SCIP_Real             bound,              /**< value to use for bound */
   int*                  nchgbnds            /**< buffer where to add the number of bound changes, or NULL */
   )
{
   SCIP_Bool      infeasible;
   SCIP_CONSDATA* consdata;
   int            i;
   
   assert(scip != NULL);
   assert(cons != NULL);

   if( SCIPisInfinity(scip, bound) )
      return SCIP_OKAY;

   consdata =  SCIPconsGetData(cons);
   assert(consdata != NULL);

   for( i = 0; i < consdata->nquadvars; ++i )
   {
      if( consdata->quadvarterms[i].nadjbilin == 0 &&
          (SCIPisInfinity(scip,  consdata->rhs) || consdata->quadvarterms[i].sqrcoef > 0) &&
          (SCIPisInfinity(scip, -consdata->lhs) || consdata->quadvarterms[i].sqrcoef < 0) )
         continue; /* skip evidently convex terms */

      if( SCIPisInfinity(scip, -SCIPvarGetLbLocal(consdata->quadvarterms[i].var)) )
      {
         SCIPverbMessage(scip, SCIP_VERBLEVEL_NORMAL, NULL, "set lower bound of %s to %g\n", SCIPvarGetName(consdata->quadvarterms[i].var), -bound);
         SCIP_CALL( SCIPtightenVarLb(scip, consdata->quadvarterms[i].var, -bound, FALSE, &infeasible, NULL) );
         assert(!infeasible);
         if( nchgbnds != NULL )
            ++*nchgbnds;
      }

      if( SCIPisInfinity(scip,  SCIPvarGetUbLocal(consdata->quadvarterms[i].var)) )
      {
         SCIPverbMessage(scip, SCIP_VERBLEVEL_NORMAL, NULL, "set upper bound of %s to %g\n", SCIPvarGetName(consdata->quadvarterms[i].var),  bound);
         SCIP_CALL( SCIPtightenVarUb(scip, consdata->quadvarterms[i].var,  bound, FALSE, &infeasible, NULL) );
         assert(!infeasible);
         if( nchgbnds != NULL ) 
            ++*nchgbnds;
      }
   }

   return SCIP_OKAY;
}

/** gets euclidean norm of gradient of quadratic function */
static
SCIP_Real getGradientNorm(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONS*            cons,               /**< constraint */
   SCIP_SOL*             sol                 /**< solution or NULL if LP solution should be used */
   )
{
   SCIP_CONSDATA* consdata;
   SCIP_Real      norm = 0.0;
   SCIP_Real      g;
   int            i, j, k;
   SCIP_VAR*      var;
   
   assert(scip != NULL);
   assert(cons != NULL);
   
   consdata = SCIPconsGetData(cons);
   assert(consdata != NULL);
   
   /* TODO allow also other norms than euclidean, maybe read separating/efficacynorm */
   
   for( i = 0; i < consdata->nlinvars; ++i )
      norm += consdata->lincoefs[i] * consdata->lincoefs[i];
   
   for( i = 0; i < consdata->nquadvars; ++i )
   {
      var = consdata->quadvarterms[i].var;
      assert(!SCIPisInfinity(scip,  SCIPgetSolVal(scip, sol, var)));
      assert(!SCIPisInfinity(scip, -SCIPgetSolVal(scip, sol, var)));
      g  =     consdata->quadvarterms[i].lincoef;
      g += 2 * consdata->quadvarterms[i].sqrcoef * SCIPgetSolVal(scip, sol, var);
      for( j = 0; j < consdata->quadvarterms[i].nadjbilin; ++j )
      {
         k = consdata->quadvarterms[i].adjbilin[j];
         if( consdata->bilinterms[k].var1 == var )
            g += consdata->bilinterms[k].coef * SCIPgetSolVal(scip, sol, consdata->bilinterms[k].var2);
         else
            g += consdata->bilinterms[k].coef * SCIPgetSolVal(scip, sol, consdata->bilinterms[k].var1);
      }
      norm += g*g;
   }
   
   return sqrt(norm);
}

/** computes activity and violation of a constraint */
static
SCIP_RETCODE computeViolation(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONS*            cons,               /**< constraint */
   SCIP_SOL*             sol,                /**< solution or NULL if LP solution should be used */
   SCIP_Bool             doscaling           /**< should we scale the violation by the gradient of the quadratic function ? */ 
   )
{  /*lint --e{666}*/
   SCIP_CONSDATA* consdata;
   SCIP_Real varval;
   int i;
   int j;
   
   assert(scip != NULL);
   assert(cons != NULL);
   
   consdata = SCIPconsGetData(cons);
   assert(consdata != NULL);

   consdata->activity = 0.0;
   varval = 0.0;
   
   /* TODO take better care of variables at +/- infinity: e.g., run instance waste in debug mode with a short timelimit (30s) */
   for( i = 0; i < consdata->nlinvars; ++i )
   {
      if( SCIPisInfinity(scip, ABS(SCIPgetSolVal(scip, sol, consdata->linvars[i]))) )
      {
         consdata->activity = SCIPinfinity(scip);
         if( !SCIPisInfinity(scip, -consdata->lhs) )
            consdata->lhsviol = SCIPinfinity(scip);
         if( !SCIPisInfinity(scip,  consdata->rhs) )
            consdata->rhsviol = SCIPinfinity(scip);
         return SCIP_OKAY;
      }
      consdata->activity += consdata->lincoefs[i] * SCIPgetSolVal(scip, sol, consdata->linvars[i]);
   }

   for( j = 0; j < consdata->nquadvars; ++j )
   {
      varval = SCIPgetSolVal(scip, sol, consdata->quadvarterms[j].var);
      if( SCIPisInfinity(scip, ABS(varval)) )
      {
         consdata->activity = SCIPinfinity(scip);
         if( !SCIPisInfinity(scip, -consdata->lhs) )
            consdata->lhsviol = SCIPinfinity(scip);
         if( !SCIPisInfinity(scip,  consdata->rhs) )
            consdata->rhsviol = SCIPinfinity(scip);
         return SCIP_OKAY;
      }
      consdata->activity += (consdata->quadvarterms[j].lincoef + consdata->quadvarterms[j].sqrcoef * varval) * varval;
   }
   
   for( j = 0; j < consdata->nbilinterms; ++j )
      consdata->activity += consdata->bilinterms[j].coef * SCIPgetSolVal(scip, sol, consdata->bilinterms[j].var1) * SCIPgetSolVal(scip, sol, consdata->bilinterms[j].var2);

   if( consdata->activity < consdata->lhs && !SCIPisInfinity(scip, -consdata->lhs) )
      consdata->lhsviol = consdata->lhs - consdata->activity;
   else
      consdata->lhsviol = 0.0;
   
   if( consdata->activity > consdata->rhs && !SCIPisInfinity(scip,  consdata->rhs) )
      consdata->rhsviol = consdata->activity - consdata->rhs;
   else
      consdata->rhsviol = 0.0;
   
   if( doscaling && (consdata->lhsviol || consdata->rhsviol) )
   {
      SCIP_Real norm = getGradientNorm(scip, cons, sol);
      if( norm > 1.0 )
      { /* scale only if > 1.0, since LP solvers may scale also only if cut norm is > 1 */
         consdata->lhsviol /= norm;
         consdata->rhsviol /= norm;
      }
   }
   
   return SCIP_OKAY;
}

/** computes violation of a set of constraints */
static
SCIP_RETCODE computeViolations(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONS**           conss,              /**< constraints */
   int                   nconss,             /**< number of constraints */
   SCIP_SOL*             sol,                /**< solution or NULL if LP solution should be used */
   SCIP_Bool             doscaling,          /**< are we scaling when computing violation ? */
   SCIP_CONS**           maxviolcon          /**< buffer to store constraint with largest violation, or NULL if solution is feasible */
   )
{
   SCIP_CONSDATA* consdata;
   SCIP_Real      viol;
   SCIP_Real      maxviol;
   int            c;

   assert(scip != NULL);
   assert(conss != NULL || nconss == 0);
   assert(maxviolcon != NULL);
   
   *maxviolcon = NULL;

   maxviol = 0.0;
   
   for( c = 0; c < nconss; ++c )
   {
      assert(conss != NULL);
      assert(conss[c] != NULL);
      
      SCIP_CALL( computeViolation(scip, conss[c], sol, doscaling) );

      consdata = SCIPconsGetData(conss[c]);
      assert(consdata != NULL);
      
      viol = MAX(consdata->lhsviol, consdata->rhsviol);
      if( viol > maxviol && SCIPisFeasPositive(scip, viol) )
      {
         maxviol = viol;
         *maxviolcon = conss[c];
      }
   }
   
   return SCIP_OKAY;
}

#if 0
/** generates a cut based on linearization for a carefully choosen reference point
 */
static
SCIP_RETCODE generateCutCareful(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONS*            cons,               /**< constraint */
   SCIP_SOL*             sol,                /**< solution to separate, or NULL if LP solution should be used */
   SCIP_BOUNDTYPE        violbound,          /**< for which bound a cut should be generated */
   SCIP_ROW**            row,                /**< storage for cut */
   SCIP_Real             maxrange            /**< maximal range allowed */
   )
{
   SCIP_CONSDATA* consdata;
   SCIP_Bool      isconvex;
   SCIP_Bool      isglobal;
   SCIP_Real      coef;
   SCIP_Real      rowcoef;
   SCIP_Real      bnd;
   SCIP_VAR*      x;
   SCIP_VAR*      y;
   SCIP_Real      xval;
   SCIP_Real      yval;
   int            j;
   SCIP_HASHMAP*  quadvaridx = NULL;
   SCIP_Real*     refpoint;
   SCIP_Real      threshold;

   assert(scip != NULL);
   assert(cons != NULL);
   assert(row  != NULL);
   assert(*row == NULL);

   consdata = SCIPconsGetData(cons);
   assert(consdata != NULL);

   isconvex = (violbound == SCIP_BOUNDTYPE_LOWER) ? consdata->isconcave : consdata->isconvex;
   isglobal = SCIPconsIsGlobal(cons) && isconvex;
   
   assert(isconvex);
   
   /* create mapping quadvars to index in quadvars arrays, if have bilin terms */
   if( consdata->nbilinterms )
   {
      SCIP_CALL( SCIPhashmapCreate(&quadvaridx, SCIPblkmem(scip), SCIPcalcHashtableSize(5 * consdata->nquadvars)) );
      for( j = 0; j < consdata->nquadvars; ++j)
      {
         x = consdata->quadvars[j];
         SCIP_CALL( SCIPhashmapInsert(quadvaridx, x, (void*)(size_t)j) );
      }
   }
   SCIP_CALL( SCIPallocBufferArray(scip, &refpoint, consdata->nquadvars) );
   
   for( threshold = 1E+6; threshold > 0.1; threshold /= 10 )
   {
      SCIPdebugMessage("try with threshold %g\n", threshold);
      assert(*row == NULL);
      
      SCIP_CALL( SCIPcreateEmptyRow(scip, row, "cut", -SCIPinfinity(scip), SCIPinfinity(scip), !isglobal /* locally */, FALSE /* modifiable */, TRUE /* removable */ ) );
      bnd = (violbound == SCIP_BOUNDTYPE_LOWER) ? consdata->lhs : consdata->rhs;
      assert(!SCIPisInfinity(scip, ABS(bnd)));

      /* add linear part */
      SCIP_CALL( SCIPaddVarsToRow(scip, *row, consdata->nlinvars, consdata->linvars, consdata->lincoefs) );

      /* create refpoint */
      for( j = 0; j < consdata->nquadvars; ++j)
      {
         x = consdata->quadvars[j];
         xval = SCIPgetSolVal(scip, sol, x);
         if( xval > threshold )
            xval = MAX( threshold, SCIPvarGetLbLocal(x));
         else if( xval < -threshold )
            xval = MIN(-threshold, SCIPvarGetUbLocal(x));

         refpoint[j] = xval;
      }

      for( j = 0; j < consdata->nquadvars; ++j )
      { /* linear term + linearization of square term */
         x = consdata->quadvars[j];
         rowcoef = consdata->quadlincoefs[j];

         if( consdata->quadsqrcoefs[j] )
         {
            xval = refpoint[j];

            rowcoef += 2*consdata->quadsqrcoefs[j]*xval;
            bnd     +=   consdata->quadsqrcoefs[j]*xval*xval;

            if( SCIPisInfinity(scip, ABS(rowcoef)) )
            {
               SCIPdebugMessage("skip linearization of square term in constraint %s because coeff. of var %s is at infinity (%g)\n", SCIPconsGetName(cons), SCIPvarGetName(x), rowcoef);
               SCIP_CALL( SCIPreleaseRow(scip, row) );
               break;
            }
         }

         SCIP_CALL( SCIPaddVarToRow(scip, *row, x, rowcoef) );
      }
      
      if( *row == NULL )
         continue;

      for( j = 0; j < consdata->nbilinterms; ++j )
      { /* linearization of bilinear terms */
         coef = consdata->bilincoefs[j];
         x    = consdata->bilinvars1[j];

         assert(SCIPhashmapExists(quadvaridx, x));
         assert((int)(size_t)SCIPhashmapGetImage(quadvaridx, x) < consdata->nquadvars);

         xval = refpoint[(int)(size_t)SCIPhashmapGetImage(quadvaridx, x)];

         if( SCIPisInfinity(scip, ABS(coef*xval)) )
         {
            SCIPdebugMessage("skip linearization of bilinear term in constraint %s because var %s gives coeff. at infinity (%g)\n", SCIPconsGetName(cons), SCIPvarGetName(x), coef*xval);
            SCIP_CALL( SCIPreleaseRow(scip, row) );
            break;
         }

         y = consdata->bilinvars2[j];

         assert(SCIPhashmapExists(quadvaridx, y));
         assert((int)(size_t)SCIPhashmapGetImage(quadvaridx, y) < consdata->nquadvars);

         yval = refpoint[(int)(size_t)SCIPhashmapGetImage(quadvaridx, y)];

         if( SCIPisInfinity(scip, ABS(coef*yval)) )
         {
            SCIPdebugMessage("skip linearization of bilinear term in constraint %s because var %s gives coeff. at infinity (%g)\n", SCIPconsGetName(cons), SCIPvarGetName(y), coef*yval);
            SCIP_CALL( SCIPreleaseRow(scip, row) );
            break;
         }

         SCIP_CALL( SCIPaddVarToRow(scip, *row, x, coef * yval) );
         SCIP_CALL( SCIPaddVarToRow(scip, *row, y, coef * xval) );
         bnd += coef * xval * yval;
      }
      
      if( *row == NULL )
         continue;

      SCIPdebugMessage(" -> generated careful cut rhs=%g, min=%f, max=%f range=%g\n",
          ABS(bnd),
          SCIPgetRowMinCoef(scip, *row), SCIPgetRowMaxCoef(scip, *row),
          SCIPgetRowMaxCoef(scip, *row)/SCIPgetRowMinCoef(scip, *row));

      if( SCIPisInfinity(scip, ABS(bnd)) )
      { /* seems to be a numerically bad cut */
         SCIPdebugMessage("skip cut for constraint %s because of very large left or right hand side: %g\n", SCIPconsGetName(cons), bnd);
         SCIP_CALL( SCIPreleaseRow(scip, row) );
         continue;
      }

      if( SCIPgetRowMaxCoef(scip, *row)/SCIPgetRowMinCoef(scip, *row) > maxrange )
      { /* seems to be a numerically bad cut */
         SCIPdebugMessage("skip cut for constraint %s because of very large range: %g\n", SCIPconsGetName(cons), SCIPgetRowMaxCoef(scip, *row)/SCIPgetRowMinCoef(scip, *row));
         SCIP_CALL( SCIPreleaseRow(scip, row) );
         continue;
      }

      if( violbound == SCIP_BOUNDTYPE_LOWER )
      {
         SCIP_CALL( SCIPchgRowLhs(scip, *row, bnd) );
      }
      else
      {
         SCIP_CALL( SCIPchgRowRhs(scip, *row, bnd) );
      }
      break; /* have a cut that is good enough */
   }

   SCIPfreeBufferArray(scip, &refpoint);
   if( quadvaridx != NULL )
      SCIPhashmapFree(&quadvaridx);
   
   return SCIP_OKAY;
}
#endif

/** generates a cut based on linearization (if convex) or McCormick (if nonconvex)
 */
static
SCIP_RETCODE generateCut(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONS*            cons,               /**< constraint */
   SCIP_SOL*             sol,                /**< solution to separate, or NULL if LP solution should be used */
   SCIP_BOUNDTYPE        violbound,          /**< for which bound a cut should be generated */
   SCIP_ROW**            row,                /**< storage for cut */
   SCIP_Real             maxrange,           /**< maximal range allowed */
   SCIP_Bool             checkcurvmultivar   /**< are we allowed to check the curvature of a multivariate quadratic function, if not done yet */
   )
{
   SCIP_CONSDATA* consdata;
   char           cutname[SCIP_MAXSTRLEN];
   SCIP_Bool      isconvex;
   SCIP_Bool      isglobal;
   SCIP_Real      coef;
   SCIP_Real      rowcoef;
   SCIP_Real      xcoef;
   SCIP_Real      ycoef;
   SCIP_Real      bnd;
   SCIP_Real      bnd_;
   SCIP_VAR*      x;
   SCIP_VAR*      y;
   SCIP_Real      xlb;
   SCIP_Real      xub;
   SCIP_Real      xval;
   SCIP_Real      ylb;
   SCIP_Real      yub;
   SCIP_Real      yval;
   int            j;

   assert(scip != NULL);
   assert(cons != NULL);
   assert(row != NULL);
   
   consdata = SCIPconsGetData(cons);
   assert(consdata != NULL);

   SCIP_CALL( checkCurvature(scip, cons, checkcurvmultivar ) );

   isconvex = (violbound == SCIP_BOUNDTYPE_LOWER) ? consdata->isconcave : consdata->isconvex;
   isglobal = SCIPconsIsGlobal(cons) && isconvex;

   if( isconvex )
      (void) SCIPsnprintf(cutname, SCIP_MAXSTRLEN, "%s_side%d_linearization_%d", SCIPconsGetName(cons), violbound, SCIPgetNLPs(scip));
   else
      (void) SCIPsnprintf(cutname, SCIP_MAXSTRLEN, "%s_side%d_mccormick_%d", SCIPconsGetName(cons), violbound, SCIPgetNLPs(scip));

   SCIP_CALL( SCIPcreateEmptyRow(scip, row, cutname, -SCIPinfinity(scip), SCIPinfinity(scip), !isglobal /* locally */, FALSE /* modifiable */, TRUE /* removable */ ) );
   bnd = (violbound == SCIP_BOUNDTYPE_LOWER) ? consdata->lhs : consdata->rhs;
   assert(!SCIPisInfinity(scip, ABS(bnd)));

   /* TODO should we buffer the coefficients of the quadratic variables before adding them to the row? */

   if( isconvex )
   {  /* do first-order taylor for each term */
      for( j = 0; j < consdata->nquadvars; ++j )
      { /* linear term + linearization of square term */
         x = consdata->quadvarterms[j].var;
         rowcoef = consdata->quadvarterms[j].lincoef;
         
         if( consdata->quadvarterms[j].sqrcoef )
         {
            xval = SCIPgetSolVal(scip, sol, x);
            /* can happen when called from initlp */
            if( xval < SCIPvarGetLbLocal(x) )
               xval = SCIPvarGetLbLocal(x);
            else if( xval > SCIPvarGetUbLocal(x) )
               xval = SCIPvarGetUbLocal(x);
            if( SCIPisInfinity(scip, ABS(xval)) )
            {
               SCIPdebugMessage("skip linearization of square term in constraint %s because var %s is at infinity\n", SCIPconsGetName(cons), SCIPvarGetName(x));
               SCIP_CALL( SCIPreleaseRow(scip, row) );
               return SCIP_OKAY;
            }

            if( consdata->nbilinterms || SCIPvarGetType(x) == SCIP_VARTYPE_CONTINUOUS || SCIPisIntegral(scip, xval) )
            {
               rowcoef += 2*consdata->quadvarterms[j].sqrcoef * xval;
               bnd += consdata->quadvarterms[j].sqrcoef * xval * xval;
            }
            else
            { /* if variable is discrete but fractional and there are no bilinear terms, try to be more clever */
               /* TODO: could we do something similar even if there are bilinear terms? */
               SCIP_Real f;

               f = SCIPfloor(scip, xval);
               rowcoef += consdata->quadvarterms[j].sqrcoef * (2*f+1);
               bnd += consdata->quadvarterms[j].sqrcoef * f * (f+1);
            }

            if( SCIPisInfinity(scip, ABS(rowcoef)) )
            {
               SCIPdebugMessage("skip linearization of square term in constraint %s because var %s is almost at infinity\n", SCIPconsGetName(cons), SCIPvarGetName(x));
               SCIP_CALL( SCIPreleaseRow(scip, row) );
               return SCIP_OKAY;
            }
         }

         SCIP_CALL( SCIPaddVarToRow(scip, *row, x, rowcoef) );
      }

      for( j = 0; j < consdata->nbilinterms; ++j )
      { /* linearization of bilinear terms */
         coef = consdata->bilinterms[j].coef;
         x = consdata->bilinterms[j].var1;
         xval = SCIPgetSolVal(scip, sol, x);
         if( xval < SCIPvarGetLbLocal(x) )
            xval = SCIPvarGetLbLocal(x);
         else if( xval > SCIPvarGetUbLocal(x) )
            xval = SCIPvarGetUbLocal(x);
         if( SCIPisInfinity(scip, ABS(coef*xval)) )
         {
            SCIPdebugMessage("skip linearization of bilinear term in constraint %s because var %s is at infinity\n", SCIPconsGetName(cons), SCIPvarGetName(x));
            SCIP_CALL( SCIPreleaseRow(scip, row) );
            return SCIP_OKAY;
         }

         y = consdata->bilinterms[j].var2;
         yval = SCIPgetSolVal(scip, sol, y);
         if( yval < SCIPvarGetLbLocal(y) )
            yval = SCIPvarGetLbLocal(y);
         else if( yval > SCIPvarGetUbLocal(y) )
            yval = SCIPvarGetUbLocal(y);
         if( SCIPisInfinity(scip, ABS(coef*yval)) )
         {
            SCIPdebugMessage("skip linearization of bilinear term in constraint %s because var %s is at infinity\n", SCIPconsGetName(cons), SCIPvarGetName(y));
            SCIP_CALL( SCIPreleaseRow(scip, row) );
            return SCIP_OKAY;
         }

         SCIP_CALL( SCIPaddVarToRow(scip, *row, x, coef * yval) );
         SCIP_CALL( SCIPaddVarToRow(scip, *row, y, coef * xval) );
         bnd += coef * xval * yval;
      }
      
      if( SCIPisInfinity(scip, ABS(bnd)) )
      { /* seems to be a numerically bad cut */
         SCIPdebugMessage("skip cut for constraint %s because bound became very large: %g\n", SCIPconsGetName(cons), bnd);
         SCIP_CALL( SCIPreleaseRow(scip, row) );
         return SCIP_OKAY;
      }
     
      if( violbound == SCIP_BOUNDTYPE_LOWER )
      {
         SCIP_CALL( SCIPchgRowLhs(scip, *row, bnd) );
      }
      else
      {
         SCIP_CALL( SCIPchgRowRhs(scip, *row, bnd) );
      }
   }
   else
   { /* underestimate and linearize each term separately -> McCormick */
      for( j = 0; j < consdata->nquadvars; ++j )
      {
         x = consdata->quadvarterms[j].var;
         rowcoef = consdata->quadvarterms[j].lincoef;

         if( consdata->quadvarterms[j].sqrcoef != 0.0 )
         {
            xval = SCIPgetSolVal(scip, sol, x);
            xlb = SCIPvarGetLbLocal(x);
            xub = SCIPvarGetUbLocal(x);
            if( xval < xlb )
               xval = xlb;
            else if( xval > xub )
               xval = xub;
            if( SCIPisInfinity(scip, ABS(xval)) )
            {
               SCIPdebugMessage("skip underestimator of square term in constraint %s because var %s is at infinity\n", SCIPconsGetName(cons), SCIPvarGetName(x));
               SCIP_CALL( SCIPreleaseRow(scip, row) );
               return SCIP_OKAY;
            }

            /* linearization of square term */
            coef = consdata->quadvarterms[j].sqrcoef;

            if( (violbound == SCIP_BOUNDTYPE_LOWER && coef <= 0) ||
                (violbound == SCIP_BOUNDTYPE_UPPER && coef >  0) )
            { /* convex -> linearize */
               if( SCIPvarGetType(x) == SCIP_VARTYPE_CONTINUOUS || SCIPisIntegral(scip, xval) )
               {
                  rowcoef += 2 * coef * xval;
                  bnd += coef * xval * xval;
               }
               else
               { /* if variable is discrete but fractional, try to be more clever */
                  SCIP_Real f;

                  f = SCIPfloor(scip, xval);
                  rowcoef += coef*(2*f+1);
                  bnd += coef*f*(f+1);
               }
            }
            else
            { /* not convex -> secand approximation */
               if( SCIPisInfinity(scip, -xlb) || SCIPisInfinity(scip, xub) )
               {
                  SCIPdebugMessage("skip secand approx of square term in constraint %s because var %s is at infinity\n", SCIPconsGetName(cons), SCIPvarGetName(x));
                  SCIP_CALL( SCIPreleaseRow(scip, row) );
                  return SCIP_OKAY;
               }

               rowcoef += coef * (xlb+xub);
               bnd += coef * xlb * xub;
            }

            if( SCIPisInfinity(scip, ABS(rowcoef)) )
            {
               SCIPdebugMessage("skip underestimator of square term in constraint %s because var %s is almost at infinity\n", SCIPconsGetName(cons), SCIPvarGetName(x));
               SCIP_CALL( SCIPreleaseRow(scip, row) );
               return SCIP_OKAY;
            }
         }
         
         if( rowcoef != 0.0 )
         {
            SCIP_CALL( SCIPaddVarToRow(scip, *row, x, rowcoef) );
         }
      }

      for( j = 0; j < consdata->nbilinterms; ++j )
      {
         x = consdata->bilinterms[j].var1;
         xval = SCIPgetSolVal(scip, sol, x);
         xlb = SCIPvarGetLbLocal(x);
         xub = SCIPvarGetUbLocal(x);
         if( xval < xlb )
            xval = xlb;
         else if( xval > xub )
            xval = xub;

         y = consdata->bilinterms[j].var2;
         yval = SCIPgetSolVal(scip, sol, y);
         ylb = SCIPvarGetLbLocal(y);
         yub = SCIPvarGetUbLocal(y);
         if( yval < ylb )
            yval = ylb;
         else if( yval > yub )
            yval = yub;

         if( SCIPisEQ(scip, xlb, xub) )
         {
            /* x is fixed, so bilinear term is linear: bilincoef * xval * y */
            xcoef = 0.0;
            ycoef = xval * consdata->bilinterms[j].coef;
            bnd_  = 0.0;
         }
         else if( SCIPisEQ(scip, ylb, yub) )
         {
            /* y is fixed, so bilinear term is linear: bilincoef * yval * x */
            xcoef = yval * consdata->bilinterms[j].coef;
            ycoef = 0.0;
            bnd_  = 0.0;
         }
         else
         {
            /* x and y are not fixed, use McCormick */
            if( SCIPisInfinity(scip, ABS(xval)) )
            {
               SCIPdebugMessage("skip underestimator of bilinear term in constraint %s because var %s is at infinity\n", SCIPconsGetName(cons), SCIPvarGetName(x));
               SCIP_CALL( SCIPreleaseRow(scip, row) );
               return SCIP_OKAY;
            }

            if( SCIPisInfinity(scip, ABS(xval)) )
            {
               SCIPdebugMessage("skip underestimator of bilinear term in constraint %s because var %s is at infinity\n", SCIPconsGetName(cons), SCIPvarGetName(y));
               SCIP_CALL( SCIPreleaseRow(scip, row) );
               return SCIP_OKAY;
            }

            coef = consdata->bilinterms[j].coef;
            if( violbound == SCIP_BOUNDTYPE_LOWER )
               coef = -coef;

            if( coef > 0.0 )
            {
               if( !SCIPisInfinity(scip, -xlb) && !SCIPisInfinity(scip, -ylb) &&
                  (SCIPisInfinity(scip,  xub) ||  SCIPisInfinity(scip,  yub) ||
                     (xub-xlb)*yval + (yub-ylb)*xval <= xub*yub - xlb*ylb) )
               {
                  xcoef = coef * ylb;
                  ycoef = coef * xlb;
                  bnd_  = coef * xlb * ylb;
               }
               else if( !SCIPisInfinity(scip, xub) && !SCIPisInfinity(scip, yub) )
               {
                  xcoef = coef * yub;
                  ycoef = coef * xub;
                  bnd_  = coef * xub * yub;
               }
               else
               {
                  SCIPdebugMessage("skip underestimator of bilinear term in constraint %s because var %s or %s is unbounded\n", SCIPconsGetName(cons), SCIPvarGetName(x), SCIPvarGetName(y));
                  SCIP_CALL( SCIPreleaseRow(scip, row) );
                  return SCIP_OKAY;
               }
            }
            else
            { /* coef < 0 */
               if( !SCIPisInfinity(scip,  xub) && !SCIPisInfinity(scip, -ylb) &&
                  (SCIPisInfinity(scip, -xlb) ||  SCIPisInfinity(scip,  yub) ||
                     (xub-xlb)*yval - (yub-ylb)*xval <= xub*ylb - xlb*yub) )
               {
                  xcoef = coef * ylb;
                  ycoef = coef * xub;
                  bnd_  = coef * xub * ylb;
               }
               else if( !SCIPisInfinity(scip, -xlb) && !SCIPisInfinity(scip, yub) )
               {
                  xcoef = coef * yub;
                  ycoef = coef * xlb;
                  bnd_  = coef * xlb * yub;
               }
               else
               {
                  SCIPdebugMessage("skip underestimator of bilinear term in constraint %s because var %s or %s is unbounded\n", SCIPconsGetName(cons), SCIPvarGetName(x), SCIPvarGetName(y));
                  SCIP_CALL( SCIPreleaseRow(scip, row) );
                  return SCIP_OKAY;
               }
            }

            if( violbound == SCIP_BOUNDTYPE_LOWER )
            {
               xcoef = -xcoef;
               ycoef = -ycoef;
               bnd_  = -bnd_;
            }
         }
         
         if( SCIPisInfinity(scip, ABS(xcoef)) || SCIPisInfinity(scip, ABS(ycoef)) )
         {
            SCIPdebugMessage("skip underestimator of bilinear term in constraint %s because var %s or %s is almost unbounded\n", SCIPconsGetName(cons), SCIPvarGetName(x), SCIPvarGetName(y));
            SCIP_CALL( SCIPreleaseRow(scip, row) );
            return SCIP_OKAY;
         }
         
         SCIP_CALL( SCIPaddVarToRow(scip, *row, x, xcoef) );
         SCIP_CALL( SCIPaddVarToRow(scip, *row, y, ycoef) );
         bnd += bnd_;
      }
      
      if( violbound == SCIP_BOUNDTYPE_LOWER )
      {
         SCIP_CALL( SCIPchgRowLhs(scip, *row, bnd) );
      }
      else
      {
         SCIP_CALL( SCIPchgRowRhs(scip, *row, bnd) );
      }
   }

   /* add linear part */
   SCIP_CALL( SCIPaddVarsToRow(scip, *row, consdata->nlinvars, consdata->linvars, consdata->lincoefs) );

   SCIPdebugMessage("found cut <%s>, rhs=%g, min=%f, max=%f range=%g nnz=%d\n",
       SCIProwGetName(*row), ABS(bnd),
       SCIPgetRowMinCoef(scip, *row), SCIPgetRowMaxCoef(scip, *row),
       SCIPgetRowMaxCoef(scip, *row)/SCIPgetRowMinCoef(scip, *row),
       SCIProwGetNNonz(*row));

   if( SCIPisInfinity(scip, ABS(bnd)) )
   { /* seems to be a numerically bad cut */
      SCIPdebugMessage("skip cut for constraint %s because of very large left or right hand side: %g\n", SCIPconsGetName(cons), bnd);
      SCIP_CALL( SCIPreleaseRow(scip, row) );
      return SCIP_OKAY;
   }

   if( SCIPgetRowMaxCoef(scip, *row)/SCIPgetRowMinCoef(scip, *row) > maxrange )
   { /* seems to be a numerically bad cut */
      SCIPdebugMessage("skip cut for constraint %s because of very large range: %g\n", SCIPconsGetName(cons), SCIPgetRowMaxCoef(scip, *row)/SCIPgetRowMinCoef(scip, *row));
      SCIP_CALL( SCIPreleaseRow(scip, row) );
      return SCIP_OKAY;
   }

   return SCIP_OKAY;
}

/** tries to separate solution or LP solution by a linear cut
 * 
 *  assumes that constraint violations have been computed 
 */
static
SCIP_RETCODE separatePoint(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONSHDLR*        conshdlr,           /**< quadratic constraints handler */
   SCIP_CONS**           conss,              /**< constraints */
   int                   nconss,             /**< number of constraints */
   int                   nusefulconss,       /**< number of constraints that seem to be useful */
   SCIP_SOL*             sol,                /**< solution to separate, or NULL if LP solution should be used */
   SCIP_Real             minefficacy,        /**< minimal efficacy of a cut if it should be added to the LP */
   SCIP_Bool             convexalways,       /**< whether to ignore minefficacy criteria for a convex constraint (and use feastol instead) */
   SCIP_RESULT*          result,             /**< result of separation */
   SCIP_Real*            bestefficacy        /**< buffer to store best efficacy of a cut that was added to the LP, if found; or NULL if not of interest */
   )
{
   SCIP_CONSHDLRDATA* conshdlrdata;
   SCIP_CONSDATA*     consdata;
   SCIP_Real          feasibility;
   SCIP_Real          norm;
   SCIP_Real          efficacy;
   SCIP_BOUNDTYPE     violbound;
   int                c;
   SCIP_ROW*          row;

   assert(scip != NULL);
   assert(conshdlr != NULL);
   assert(conss != NULL || nconss == 0);
   assert(nusefulconss <= nconss);
   assert(result != NULL);
   
   *result = SCIP_FEASIBLE;
   
   conshdlrdata = SCIPconshdlrGetData(conshdlr);
   assert(conshdlrdata != NULL);

   if( bestefficacy != NULL )
      *bestefficacy = 0.0;

   for( c = 0; c < nconss; ++c )
   {
      assert(conss != NULL);
      consdata = SCIPconsGetData(conss[c]);
      assert(consdata != NULL);

      if( SCIPisFeasPositive(scip, consdata->lhsviol) || SCIPisFeasPositive(scip, consdata->rhsviol) )
      {
         /* we are not feasible anymore */
         if( *result == SCIP_FEASIBLE )
            *result = SCIP_DIDNOTFIND;

         violbound = SCIPisFeasPositive(scip, consdata->lhsviol) ? SCIP_BOUNDTYPE_LOWER : SCIP_BOUNDTYPE_UPPER;

         /* generate cut */
         SCIP_CALL( generateCut(scip, conss[c], sol, violbound, &row, conshdlrdata->cutmaxrange, conshdlrdata->checkcurvature) );
#if 0
         /* if generation failed, then probably because of numerical issues;
          * if the constraint is convex and we are desperate to get a cut, then we can try again with a better chosen reference point */
         if( row == NULL && !addweakcuts &&
            ( (violbound == SCIP_BOUNDTYPE_UPPER && consdata->isconvex ) || /* convex  constraint, or */
              (violbound == SCIP_BOUNDTYPE_LOWER && consdata->isconcave) )  /* concave constraint */
           )
         {
            SCIP_CALL( generateCutCareful(scip, conss[c], sol, violbound, &row, conshdlrdata->cutmaxrange) );
         }
#endif
         
         if( row == NULL ) /* failed to generate cut */
            continue;

         if( sol == NULL )
         {
            if( SCIPgetLPSolstat(scip) == SCIP_LPSOLSTAT_UNBOUNDEDRAY )
            {
               SCIP_Real rayprod;
               SCIP_VAR* var;
               int i;

               /* if the LP is unbounded, then we accept only cuts that cut into the direction of a hopefully existing primal ray
                * that is, assume a ray r is given such that p + t*r is feasible for the LP for all t >= t_0 and some p
                * given a cut lhs <= <c,x> <= rhs, we check whether it imposes an upper bound on t and thus bounds the ray
                * this is given if rhs < infinity and <c,r> > 0, since we then enforce <c,p+t*r> = <c,p> + t<c,r> <= rhs, i.e., t <= (rhs - <c,p>)/<c,r>
                * similar, lhs > -infinity and <c,r> < 0 is good
                */

               if( SCIPhasPrimalRay(scip) )
               {
                  rayprod = 0.0;
                  for( i = 0; i < SCIProwGetNNonz(row); ++i )
                  {
                     assert(SCIProwGetCols(row)[i] != NULL);
                     var = SCIPcolGetVar(SCIProwGetCols(row)[i]);
                     assert(var != NULL);

                     rayprod += SCIProwGetVals(row)[i] * SCIPgetPrimalRayVal(scip, var);
                  }
                  if( !SCIPisInfinity(scip, SCIProwGetRhs(row)) && SCIPisPositive(scip, rayprod) )
                  {
                     feasibility = -rayprod;
                  }
                  else if( !SCIPisInfinity(scip, -SCIProwGetLhs(row)) && SCIPisNegative(scip, rayprod) )
                  {
                     feasibility = rayprod;
                  }
                  else
                  {
                     feasibility = 0.0;
                  }
               }
               else
               {
                  SCIPdebugMessage("do not have ray from unbounded LP, cannot check if cut intersects with unbounded ray, skip cut\n");
                  SCIP_CALL( SCIPreleaseRow(scip, &row) );
                  continue;
               }
            }
            else
            {
               feasibility = SCIPgetRowLPFeasibility(scip, row);
            }
         }
         else
            feasibility = SCIPgetRowSolFeasibility(scip, row, sol);
         norm = SCIProwGetNorm(row);

         /* in difference to SCIPgetCutEfficacy, we scale by norm only if the norm is > 1.0
          * this avoid finding cuts efficiant which are only very slightly violated
          * CPLEX does not seem to scale row coefficients up too
          */
         if( norm > 1.0 )
            efficacy = -feasibility / norm;
         else
            efficacy = -feasibility;

         if( efficacy > minefficacy ||
            (convexalways &&
             ((violbound == SCIP_BOUNDTYPE_UPPER && consdata->isconvex ) || (violbound == SCIP_BOUNDTYPE_LOWER && consdata->isconcave)) &&
             efficacy > SCIPfeastol(scip)
            )
           )
         { /* cut cuts off solution */
            SCIP_CALL( SCIPaddCut(scip, sol, row, FALSE /* forcecut */) );
            *result = SCIP_SEPARATED;
            SCIP_CALL( SCIPresetConsAge(scip, conss[c]) );
            SCIPdebugMessage("add cut with efficacy %g and feasibility %g for constraint <%s> violated by %g\n", efficacy, feasibility,
               SCIPconsGetName(conss[c]), consdata->lhsviol+consdata->rhsviol);
            if( bestefficacy != NULL && efficacy > *bestefficacy )
               *bestefficacy = efficacy;
         }

         SCIP_CALL( SCIPreleaseRow (scip, &row) );
      }

      /* enforce only useful constraints
       * others are only checked and enforced if we are still feasible or have not found a separating cut yet
       */ 
      if( c >= nusefulconss && *result == SCIP_SEPARATED )
         break;
   }

   return SCIP_OKAY;
}

/** processes the event that a new primal solution has been found */
static
SCIP_DECL_EVENTEXEC(processNewSolutionEvent)
{
   SCIP_CONSHDLR* conshdlr;
   SCIP_CONSHDLRDATA* conshdlrdata;
   SCIP_CONS**    conss;
   int            nconss;
   SCIP_CONSDATA* consdata;
   int            c;
   SCIP_SOL*      sol;
   SCIP_ROW*      row = NULL;

   assert(scip != NULL);
   assert(event != NULL);
   assert(eventdata != NULL);
   assert(eventhdlr != NULL);

   assert((SCIPeventGetType(event) & SCIP_EVENTTYPE_SOLFOUND) != 0);

   conshdlr = (SCIP_CONSHDLR*)eventdata;

   nconss = SCIPconshdlrGetNConss(conshdlr);

   if( nconss == 0 )
      return SCIP_OKAY;

   conshdlrdata = SCIPconshdlrGetData(conshdlr);
   assert(conshdlrdata != NULL);

   sol = SCIPeventGetSol(event);
   assert(sol != NULL);

   /* we are only interested in solution coming from the NLP or RENS heuristic (is that good?) */
   if( SCIPsolGetHeur(sol) == NULL )
      return SCIP_OKAY;
   /* @todo maybe we should just linearize in every solution that is found, or in every improving solution? */
   if( SCIPsolGetHeur(sol) != conshdlrdata->subnlpheur && SCIPsolGetHeur(sol) != conshdlrdata->rensheur)
      return SCIP_OKAY;

   conss = SCIPconshdlrGetConss(conshdlr);
   assert(conss != NULL);

   SCIPdebugMessage("catched new sol event %x from heur %p; have %d conss\n", SCIPeventGetType(event), (void*)SCIPsolGetHeur(sol), nconss);

   for( c = 0; c < nconss; ++c )
   {
      if( SCIPconsIsLocal(conss[c]) )
         continue;

      SCIP_CALL( checkCurvature(scip, conss[c], conshdlrdata->checkcurvature) );
      
      consdata = SCIPconsGetData(conss[c]);
      assert(consdata != NULL);

      if( consdata->isconvex && !SCIPisInfinity(scip, consdata->rhs) )
      {
         SCIP_CALL( generateCut(scip, conss[c], sol, SCIP_BOUNDTYPE_UPPER, &row, conshdlrdata->cutmaxrange, conshdlrdata->checkcurvature) );
      }
      else if( consdata->isconcave && !SCIPisInfinity(scip, -consdata->lhs) )
      {
         SCIP_CALL( generateCut(scip, conss[c], sol, SCIP_BOUNDTYPE_LOWER, &row, conshdlrdata->cutmaxrange, conshdlrdata->checkcurvature) );
      }
      else
         continue;

      if( row == NULL )
         continue;

      assert(!SCIProwIsLocal(row));

      SCIP_CALL( SCIPaddPoolCut(scip, row) );
      SCIP_CALL( SCIPreleaseRow(scip, &row) );
   }

   return SCIP_OKAY;
}

/** computes the infeasibilities of variables from the convexification gaps in the constraints and notifies the branching rule about them
 */
static
SCIP_RETCODE registerVariableInfeasibilities(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONSHDLR*        conshdlr,           /**< constraint handler */
   SCIP_CONS**           conss,              /**< constraints to check */
   int                   nconss,             /**< number of constraints to check */
   int*                  nnotify             /**< counter for number of notifications performed */
   )
{
   SCIP_CONSHDLRDATA* conshdlrdata;
   SCIP_CONSDATA*     consdata;
   int                c;
   int                j;
   SCIP_Real          xlb;
   SCIP_Real          xub;
   SCIP_Real          xval;
   SCIP_Real          ylb;
   SCIP_Real          yub;
   SCIP_Real          yval;
   SCIP_Real          gap;
   SCIP_Real          coef_;
   
   assert(scip != NULL);
   assert(conshdlr != NULL);
   assert(conss != NULL || nconss == 0);
   
   conshdlrdata = SCIPconshdlrGetData(conshdlr);
   assert(conshdlrdata != NULL);
   
   *nnotify = 0;

   for( c = 0; c < nconss; ++c )
   {
      assert(conss != NULL);
      consdata = SCIPconsGetData(conss[c]);
      assert(consdata != NULL);
      
      if( !consdata->nquadvars )
         continue;
      
      if( (!SCIPisFeasPositive(scip, consdata->lhsviol) || consdata->isconcave) &&
          (!SCIPisFeasPositive(scip, consdata->rhsviol) || consdata->isconvex ) )
         continue;
      SCIPdebugMessage("con %s violation: %g %g  convex: %u %u\n", SCIPconsGetName(conss[c]), consdata->lhsviol, consdata->rhsviol, consdata->isconvex, consdata->isconcave);
      
      for( j = 0; j < consdata->nquadvars; ++j )
      { /* square terms */
         if( (SCIPisFeasPositive(scip, consdata->rhsviol) && consdata->quadvarterms[j].sqrcoef < 0) ||
             (SCIPisFeasPositive(scip, consdata->lhsviol) && consdata->quadvarterms[j].sqrcoef > 0) )
         {
            xlb = SCIPvarGetLbLocal(consdata->quadvarterms[j].var);
            xub = SCIPvarGetUbLocal(consdata->quadvarterms[j].var);
            if( SCIPisEQ(scip, xlb, xub) )
            {
               SCIPdebugMessage("ignore fixed variable <%s>[%g, %g], diff %g\n", SCIPvarGetName(consdata->quadvarterms[j].var), xlb, xub, xub-xlb);
               continue;
            }

            xval = SCIPgetSolVal(scip, NULL, consdata->quadvarterms[j].var);

            if( SCIPisInfinity(scip, -xlb) || SCIPisInfinity(scip, xub) )
               gap = SCIPinfinity(scip);
            else if( xval < xlb || xval > xub )
               continue;
            else
               gap = (xval-xlb)*(xub-xval)/(1+2*ABS(xval));
            assert(!SCIPisNegative(scip, gap));
            SCIP_CALL( SCIPaddExternBranchCand(scip, consdata->quadvarterms[j].var, MAX(gap, 0.0), SCIP_INVALID) );
            ++*nnotify;
         }
      }

      for( j = 0; j < consdata->nbilinterms; ++j )
      { /* bilinear terms
           if any of the variables if fixed, then it actually behaves like a linear term, so we don't need to branch on it */
         xlb = SCIPvarGetLbLocal(consdata->bilinterms[j].var1);
         xub = SCIPvarGetUbLocal(consdata->bilinterms[j].var1);
         if( SCIPisEQ(scip, xlb, xub) )
            continue;

         ylb = SCIPvarGetLbLocal(consdata->bilinterms[j].var2);
         yub = SCIPvarGetUbLocal(consdata->bilinterms[j].var2);
         if( SCIPisEQ(scip, ylb, yub) )
            continue;

         /* if x is unbounded but y is binary, then it's actually easier to branch on y, since the term will become linear this way */
         if( SCIPisInfinity(scip, -xlb) || SCIPisInfinity(scip, xub) )
         {
            if( SCIPvarIsBinary(consdata->bilinterms[j].var2) )
               SCIP_CALL( SCIPaddExternBranchCand(scip, consdata->bilinterms[j].var2, SCIPinfinity(scip), SCIP_INVALID) );
            else
               SCIP_CALL( SCIPaddExternBranchCand(scip, consdata->bilinterms[j].var1, SCIPinfinity(scip), SCIP_INVALID) );
            ++*nnotify;
            continue;
         }

         /* if y is unbounded but x is binary, then it's actually easier to branch on x, since the term will become linear this way */
         if( SCIPisInfinity(scip, -ylb) || SCIPisInfinity(scip, yub) )
         {
            if( SCIPvarIsBinary(consdata->bilinterms[j].var1) )
               SCIP_CALL( SCIPaddExternBranchCand(scip, consdata->bilinterms[j].var1, SCIPinfinity(scip), SCIP_INVALID) );
            else
               SCIP_CALL( SCIPaddExternBranchCand(scip, consdata->bilinterms[j].var2, SCIPinfinity(scip), SCIP_INVALID) );
            ++*nnotify;
            continue;
         }

         xval = SCIPgetSolVal(scip, NULL, consdata->bilinterms[j].var1);
         if( xval < xlb )
            xval = xlb;
         else if( xval > xub )
            xval = xub;

         yval = SCIPgetSolVal(scip, NULL, consdata->bilinterms[j].var2);
         if( yval < ylb )
            yval = ylb;
         else if( yval > yub )
            yval = yub;

         coef_ = SCIPisFeasPositive(scip, consdata->lhsviol) ? -consdata->bilinterms[j].coef : consdata->bilinterms[j].coef;
         if( coef_ > 0.0 )
         {
            if( (xub-xlb)*yval + (yub-ylb)*xval <= xub*yub - xlb*ylb )
               gap = (xval*yval - xlb*yval - ylb*xval + xlb*ylb) / (1+sqrt(xval*xval + yval*yval));
            else
               gap = (xval*yval - xval*yub - yval*xub + xub*yub) / (1+sqrt(xval*xval + yval*yval));
         }
         else
         { /* coef_ < 0 */
            if( (xub-xlb)*yval - (yub-ylb)*xval <= xub*ylb - xlb*yub )
               gap = -(xval*yval - xval*ylb - yval*xub + xub*ylb) / (1+sqrt(xval*xval + yval*yval));
            else
               gap = -(xval*yval - xval*yub - yval*xlb + xlb*yub) / (1+sqrt(xval*xval + yval*yval));
         }

         assert(!SCIPisNegative(scip, gap));
         if( gap < 0.0 )
            gap = 0.0;

         SCIP_CALL( SCIPaddExternBranchCand(scip, consdata->bilinterms[j].var1, gap, SCIP_INVALID) );
         SCIP_CALL( SCIPaddExternBranchCand(scip, consdata->bilinterms[j].var2, gap, SCIP_INVALID) );
         *nnotify += 2;
      }
   }

   SCIPdebugMessage("registered %d branching candidates\n", *nnotify);

   return SCIP_OKAY;
}

/** registers a quadratic variable from a violated constraint as branching candidate that has a large absolute value in the LP relaxation */
static
SCIP_RETCODE registerLargeLPValueVariableForBranching(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONS**           conss,              /**< constraints */
   int                   nconss,             /**< number of constraints */
   SCIP_VAR**            brvar               /**< buffer to store branching variable */
   )
{
   SCIP_CONSDATA*      consdata;
   SCIP_Real           val;
   SCIP_Real           brvarval;
   int                 i;
   int                 c;
   
   assert(scip  != NULL);
   assert(conss != NULL || nconss == 0);
   
   *brvar = NULL;
   brvarval = -1.0;

   for( c = 0; c < nconss; ++c )
   {
      assert(conss != NULL);
      consdata = SCIPconsGetData(conss[c]);
      assert(consdata != NULL);
      
      if( !SCIPisFeasPositive(scip, consdata->lhsviol) && !SCIPisFeasPositive(scip, consdata->rhsviol) )
         continue;
      
      for( i = 0; i < consdata->nquadvars; ++i )
      {
         /* do not propose fixed variables */
         if( SCIPisEQ(scip, SCIPvarGetLbLocal(consdata->quadvarterms[i].var), SCIPvarGetUbLocal(consdata->quadvarterms[i].var)) )
            continue;
         val = SCIPgetSolVal(scip, NULL, consdata->quadvarterms[i].var);
         if( ABS(val) > brvarval )
         {
            brvarval = ABS(val);
            *brvar = consdata->quadvarterms[i].var;
         }
      }
   }
   
   if( *brvar != NULL )
   {
      SCIP_CALL( SCIPaddExternBranchCand(scip, *brvar, brvarval, SCIP_INVALID) );
   }
   
   return SCIP_OKAY;
}


/** replaces violated quadratic constraints where all quadratic variables are fixed by linear constraints */
static
SCIP_RETCODE replaceByLinearConstraints(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONS**           conss,              /**< constraints */
   int                   nconss              /**< number of constraints */
   )
{
   SCIP_CONS*          cons;
   SCIP_CONSDATA*      consdata;
   SCIP_Real           constant;
   SCIP_Real           val1;
   SCIP_Real           val2;
   int                 i;
   int                 c;

   assert(scip  != NULL);
   assert(conss != NULL || nconss == 0);

   for( c = 0; c < nconss; ++c )
   {
      assert(conss != NULL);
      consdata = SCIPconsGetData(conss[c]);
      assert(consdata != NULL);

      if( !SCIPisFeasPositive(scip, consdata->lhsviol) && !SCIPisFeasPositive(scip, consdata->rhsviol) )
         continue;

      constant = 0.0;

      for( i = 0; i < consdata->nquadvars; ++i )
      {
         /* variables should be fixed if constraint is violated */
         assert(SCIPisRelEQ(scip, SCIPvarGetLbLocal(consdata->quadvarterms[i].var), SCIPvarGetUbLocal(consdata->quadvarterms[i].var)));

         val1 = (SCIPvarGetUbLocal(consdata->quadvarterms[i].var) + SCIPvarGetLbLocal(consdata->quadvarterms[i].var)) / 2.0;
         constant += (consdata->quadvarterms[i].lincoef + consdata->quadvarterms[i].sqrcoef * val1) * val1;
      }

      for( i = 0; i < consdata->nbilinterms; ++i )
      {
         val1 = (SCIPvarGetUbLocal(consdata->bilinterms[i].var1) + SCIPvarGetLbLocal(consdata->bilinterms[i].var1)) / 2.0;
         val2 = (SCIPvarGetUbLocal(consdata->bilinterms[i].var2) + SCIPvarGetLbLocal(consdata->bilinterms[i].var2)) / 2.0;
         constant += consdata->bilinterms[i].coef * val1 * val2;
      }

      SCIP_CALL( SCIPcreateConsLinear(scip, &cons, SCIPconsGetName(conss[c]),
         consdata->nlinvars, consdata->linvars, consdata->lincoefs,
         SCIPisInfinity(scip, -consdata->lhs) ? -SCIPinfinity(scip) : (consdata->lhs - constant),
         SCIPisInfinity(scip,  consdata->rhs) ?  SCIPinfinity(scip) : (consdata->rhs - constant),
         SCIPconsIsInitial(conss[c]), SCIPconsIsSeparated(conss[c]), SCIPconsIsEnforced(conss[c]),
         SCIPconsIsChecked(conss[c]), SCIPconsIsPropagated(conss[c]),  TRUE,
         SCIPconsIsModifiable(conss[c]), SCIPconsIsDynamic(conss[c]), SCIPconsIsRemovable(conss[c]),
         SCIPconsIsStickingAtNode(conss[c])) );

      SCIPdebugMessage("replace quadratic constraint <%s> by linear constraint after all quadratic vars have been fixed\n", SCIPconsGetName(conss[c]) );
      SCIPdebug( SCIPprintCons(scip, cons, NULL) );
      SCIP_CALL( SCIPaddConsLocal(scip, cons, NULL) );
      SCIP_CALL( SCIPreleaseCons(scip, &cons) );

      SCIP_CALL( SCIPdelConsLocal(scip, conss[c]) );
   }

   return SCIP_OKAY;
}

/* tightens a lower bound on a variable and checks the result */
static
SCIP_RETCODE propagateBoundsTightenVarLb(
   SCIP*                 scip,               /**< SCIP data structure */ 
   SCIP_CONS*            cons,               /**< constraint where we currently propagate */
   SCIP_Real             intervalinfty,      /**< infinity value used in interval operations */
   SCIP_VAR*             var,                /**< variable which domain we might reduce */
   SCIP_Real             bnd,                /**< new lower bound for variable */
   SCIP_RESULT*          result,             /**< result to update if there was a tightening or cutoff */
   int*                  nchgbds             /**< counter to increase if a bound was tightened */
   )
{
   SCIP_Bool infeas;
   SCIP_Bool tightened;
   
   assert(scip != NULL);
   assert(cons != NULL);
   assert(intervalinfty > 0.0);
   assert(bnd > -intervalinfty);
   assert(var != NULL);
   assert(result != NULL);
   assert(*result == SCIP_DIDNOTFIND || *result == SCIP_REDUCEDDOM);
   assert(nchgbds != NULL);
   
   /* new bound is no improvement */
   if( SCIPisLE(scip, bnd, SCIPvarGetLbLocal(var)) )
      return SCIP_OKAY;

   if( SCIPisInfinity(scip, bnd) )
   { /* domain will be outside [-infty, +infty] -> declare node infeasible */
      *result = SCIP_CUTOFF;
      SCIP_CALL( SCIPresetConsAge(scip, cons) );
      return SCIP_OKAY;
   }
   
   /* new lower bound is very low (between -intervalinfty and -SCIPinfinity()) */
   if( SCIPisInfinity(scip, -bnd) )
      return SCIP_OKAY;

   bnd = SCIPadjustedVarLb(scip, var, bnd);
   SCIP_CALL( SCIPtightenVarLb(scip, var, bnd, FALSE, &infeas, &tightened) );
   if( infeas )
   {
      SCIPdebugMessage("%s found constraint <%s> infeasible due to tightened lower bound %g for variable <%s>\n", SCIPinProbing(scip) ? "in probing" : "", SCIPconsGetName(cons), bnd, SCIPvarGetName(var));
      *result = SCIP_CUTOFF;
      SCIP_CALL( SCIPresetConsAge(scip, cons) );
      return SCIP_OKAY;
   }
   if( tightened )
   {
      SCIPdebugMessage("%s tightened lower bound of variable <%s> in constraint <%s> to %g\n", SCIPinProbing(scip) ? "in probing" : "", SCIPvarGetName(var), SCIPconsGetName(cons), bnd);
      ++*nchgbds;
      *result = SCIP_REDUCEDDOM;
      SCIP_CALL( SCIPresetConsAge(scip, cons) );
   }
   
   return SCIP_OKAY;
}

/* tightens an upper bound on a variable and checks the result */
static
SCIP_RETCODE propagateBoundsTightenVarUb(
   SCIP*                 scip,               /**< SCIP data structure */ 
   SCIP_CONS*            cons,               /**< constraint where we currently propagate */
   SCIP_Real             intervalinfty,      /**< infinity value used in interval operations */
   SCIP_VAR*             var,                /**< variable which domain we might reduce */
   SCIP_Real             bnd,                /**< new upper bound for variable */
   SCIP_RESULT*          result,             /**< result to update if there was a tightening or cutoff */
   int*                  nchgbds             /**< counter to increase if a bound was tightened */
   )
{
   SCIP_Bool infeas;
   SCIP_Bool tightened;
   
   assert(scip != NULL);
   assert(cons != NULL);
   assert(intervalinfty > 0.0);
   assert(bnd < intervalinfty);
   assert(var != NULL);
   assert(result != NULL);
   assert(*result == SCIP_DIDNOTFIND || *result == SCIP_REDUCEDDOM);
   assert(nchgbds != NULL);
   
   /* new bound is no improvement */
   if( SCIPisGE(scip, bnd, SCIPvarGetUbLocal(var)) )
      return SCIP_OKAY;

   if( SCIPisInfinity(scip, -bnd) )
   { /* domain will be outside [-infty, +infty] -> declare node infeasible */
      *result = SCIP_CUTOFF;
      SCIP_CALL( SCIPresetConsAge(scip, cons) );
      return SCIP_OKAY;
   }
   
   /* new upper bound is very high (between SCIPinfinity() and intervalinfty) */
   if( SCIPisInfinity(scip, bnd) )
      return SCIP_OKAY;

   bnd = SCIPadjustedVarUb(scip, var, bnd);
   SCIP_CALL( SCIPtightenVarUb(scip, var, bnd, FALSE, &infeas, &tightened) );
   if( infeas )
   {
      SCIPdebugMessage("%s found constraint <%s> infeasible due to tightened upper bound %g for variable <%s>\n", SCIPinProbing(scip) ? "in probing" : "", SCIPconsGetName(cons), bnd, SCIPvarGetName(var));
      *result = SCIP_CUTOFF;
      SCIP_CALL( SCIPresetConsAge(scip, cons) );
      return SCIP_OKAY;
   }
   if( tightened )
   {
      SCIPdebugMessage("%s tightened upper bound of variable <%s> in constraint <%s> to %g\n", SCIPinProbing(scip) ? "in probing" : "", SCIPvarGetName(var), SCIPconsGetName(cons), bnd);
      ++*nchgbds;
      *result = SCIP_REDUCEDDOM;
      SCIP_CALL( SCIPresetConsAge(scip, cons) );
   }
   
   return SCIP_OKAY;
}

/** solves a quadratic equation \f$ a x^2 + b x \in rhs \f$ (with b an interval) and reduces bounds on x or deduces infeasibility if possible
 */
static
SCIP_RETCODE propagateBoundsQuadVar(
   SCIP*                 scip,               /**< SCIP data structure */ 
   SCIP_CONS*            cons,               /**< constraint where we currently propagate */
   SCIP_Real             intervalinfty,      /**< infinity value used in interval operations */
   SCIP_VAR*             var,                /**< variable which bounds with might tighten */
   SCIP_Real             a,                  /**< coefficient in square term */
   SCIP_INTERVAL         b,                  /**< coefficient in linear term */
   SCIP_INTERVAL         rhs,                /**< right hand side of quadratic equation */
   SCIP_RESULT*          result,             /**< result of propagation */
   int*                  nchgbds             /**< buffer where to add number of tightened bounds */
   )
{
   SCIP_INTERVAL newrange;
   
   assert(scip != NULL);
   assert(cons != NULL);
   assert(var != NULL);
   assert(result != NULL);
   assert(nchgbds != NULL);

   /* compute solution of a*x^2 + b*x \in rhs */
   if( a == 0.0 && SCIPintervalGetInf(b) == 0.0 && SCIPintervalGetSup(b) == 0.0 )
   { /* relatively easy case: 0.0 \in rhs, thus check if infeasible or just redundant */
      if( SCIPintervalGetInf(rhs) > 0.0 || SCIPintervalGetSup(rhs) < 0.0 )
      {
         SCIPdebugMessage("found <%s> infeasible due to domain propagation for quadratic variable <%s>\n", SCIPconsGetName(cons), SCIPvarGetName(var));
         SCIP_CALL( SCIPresetConsAge(scip, cons) );
         *result = SCIP_CUTOFF;
      }
      return SCIP_OKAY;
   }
   else if( SCIPvarGetLbLocal(var) >= 0.0 )
   { 
      SCIP_INTERVAL a_;

      /* need only positive solutions */
      SCIPintervalSet(&a_, a);
      SCIPintervalSolveUnivariateQuadExpressionPositive(intervalinfty, &newrange, a_, b, rhs);
   }
   else if( SCIPvarGetUbLocal(var) <= 0.0 )
   {
      /* need only negative solutions */
      SCIP_INTERVAL a_;
      SCIP_INTERVAL tmp;
      SCIPintervalSet(&a_, a);
      SCIPintervalSetBounds(&tmp, -SCIPintervalGetSup(b), -SCIPintervalGetInf(b));
      SCIPintervalSolveUnivariateQuadExpressionPositive(intervalinfty, &tmp, a_, tmp, rhs);
      if( SCIPintervalIsEmpty(tmp) )
      {
         SCIPdebugMessage("found <%s> infeasible due to domain propagation for quadratic variable <%s>\n", SCIPconsGetName(cons), SCIPvarGetName(var));
         *result = SCIP_CUTOFF;
         SCIP_CALL( SCIPresetConsAge(scip, cons) );
         return SCIP_OKAY;
      }
      SCIPintervalSetBounds(&newrange, -SCIPintervalGetSup(tmp), -SCIPintervalGetInf(tmp));
   }
   else
   {
      /* need both positive and negative solution */
      SCIP_INTERVAL a_;
      SCIPintervalSet(&a_, a);
      SCIPintervalSolveUnivariateQuadExpression(intervalinfty, &newrange, a_, b, rhs);
   }
   
   /* SCIPdebugMessage("%g x^2 + [%g, %g] x in [%g, %g] -> [%g, %g]\n", a, b.inf, b.sup, rhs.inf, rhs.sup, newrange.inf, newrange.sup); */

   if( SCIPisInfinity(scip, SCIPintervalGetInf(newrange)) || SCIPisInfinity(scip, -SCIPintervalGetSup(newrange)) )
   { /* domain outside [-infty, +infty] -> declare node infeasible */
      SCIPdebugMessage("found <%s> infeasible because propagated domain of quadratic variable <%s> is outside of (-infty, +infty)\n", SCIPconsGetName(cons), SCIPvarGetName(var));
      *result = SCIP_CUTOFF;
      SCIP_CALL( SCIPresetConsAge(scip, cons) );
      return SCIP_OKAY;
   }

   if( SCIPintervalIsEmpty(newrange) )
   {
      SCIPdebugMessage("found <%s> infeasible due to domain propagation for quadratic variable <%s>\n", SCIPconsGetName(cons), SCIPvarGetName(var));
      *result = SCIP_CUTOFF;
      return SCIP_OKAY;
   }

   if( !SCIPisInfinity(scip, -SCIPintervalGetInf(newrange)) )
   {
      SCIP_CALL( propagateBoundsTightenVarLb(scip, cons, intervalinfty, var, SCIPintervalGetInf(newrange), result, nchgbds) );
      if( *result == SCIP_CUTOFF )
         return SCIP_OKAY;
   }

   if( !SCIPisInfinity(scip,  SCIPintervalGetSup(newrange)) )
   {
      SCIP_CALL( propagateBoundsTightenVarUb(scip, cons, intervalinfty, var, SCIPintervalGetSup(newrange), result, nchgbds) );
      if( *result == SCIP_CUTOFF )
         return SCIP_OKAY;
   }

   return SCIP_OKAY;
}

/** tries to deduce domain reductions for x in xsqrcoef x^2 + xlincoef x + ysqrcoef y^2 + ylincoef y + bilincoef x y \\in rhs
 * NOTE that domain reductions for y are not deduced 
 */
static
SCIP_RETCODE propagateBoundsBilinearTerm(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONS*            cons,               /**< the constraint, where the bilinear term belongs to */
   SCIP_Real             intervalinfty,      /**< infinity value used in interval operations */
   SCIP_VAR*             x,                  /**< first variable */
   SCIP_Real             xsqrcoef,           /**< square coefficient of x */
   SCIP_Real             xlincoef,           /**< linear coefficient of x */
   SCIP_VAR*             y,                  /**< second variable */
   SCIP_Real             ysqrcoef,           /**< square coefficient of y */
   SCIP_Real             ylincoef,           /**< linear coefficient of y */
   SCIP_Real             bilincoef,          /**< bilinear coefficient of x*y */
   SCIP_INTERVAL         rhs,                /**< right hand side of quadratic equation */
   SCIP_RESULT*          result,             /**< pointer to store result of domain propagation */
   int*                  nchgbds             /**< counter to increment if domain reductions are found */
   )
{
   SCIP_INTERVAL myrhs;
   SCIP_INTERVAL varbnds;
   SCIP_INTERVAL lincoef;
   
   assert(scip != NULL);
   assert(cons != NULL);
   assert(x != NULL);
   assert(y != NULL);
   assert(x != y);
   assert(result != NULL);
   assert(*result == SCIP_DIDNOTFIND || *result == SCIP_REDUCEDDOM);
   assert(nchgbds != NULL);
   assert(bilincoef != 0.0);
   
   if( SCIPintervalIsEntire(intervalinfty, rhs) )
      return SCIP_OKAY;
  
   /* try to find domain reductions for x */
   SCIPintervalSetBounds(&varbnds, MIN(SCIPvarGetLbLocal(y), SCIPvarGetUbLocal(y)), MAX(SCIPvarGetLbLocal(y), SCIPvarGetUbLocal(y)));

   /* put ysqrcoef*y^2 + ylincoef * y into rhs */
   if( SCIPintervalGetSup(rhs) >= intervalinfty )
   {
      /* if rhs is unbounded by above, it is sufficient to get an upper bound on ysqrcoef*y^2 + ylincoef * y */
      SCIP_ROUNDMODE roundmode;
      SCIP_Real      tmp;
      
      SCIPintervalSet(&lincoef, ylincoef);
      tmp = SCIPintervalQuadUpperBound(intervalinfty, ysqrcoef, lincoef, varbnds);
      roundmode = SCIPintervalGetRoundingMode();
      SCIPintervalSetRoundingModeDownwards();
      SCIPintervalSetBounds(&myrhs, SCIPintervalGetInf(rhs) - tmp, intervalinfty);
      SCIPintervalSetRoundingMode(roundmode);
   }
   else if( SCIPintervalGetInf(rhs) <= -intervalinfty )
   {
      /* if rhs is unbounded by below, it is sufficient to get a  lower bound on ysqrcoef*y^2 + ylincoef * y */
      SCIP_ROUNDMODE roundmode;
      SCIP_Real      tmp;
      
      SCIPintervalSet(&lincoef, -ylincoef);
      tmp = -SCIPintervalQuadUpperBound(intervalinfty, -ysqrcoef, lincoef, varbnds);
      roundmode = SCIPintervalGetRoundingMode();
      SCIPintervalSetRoundingModeUpwards();
      SCIPintervalSetBounds(&myrhs, -intervalinfty, SCIPintervalGetSup(rhs) - tmp);
      SCIPintervalSetRoundingMode(roundmode);
   }
   else
   {
      /* if rhs is bounded, we need both bounds on ysqrcoef*y^2 + ylincoef * y */
      SCIP_INTERVAL tmp;
      
      SCIPintervalSet(&lincoef, ylincoef);
      SCIPintervalQuad(intervalinfty, &tmp, ysqrcoef, lincoef, varbnds);
      SCIPintervalSub(intervalinfty, &myrhs, rhs, tmp);
   }
   
   /* create equation xsqrcoef * x^2 + (xlincoef + bilincoef * [ylb, yub]) * x \in myrhs */
   SCIPintervalMulScalar(intervalinfty, &lincoef, varbnds, bilincoef);
   SCIPintervalAddScalar(intervalinfty, &lincoef, lincoef, xlincoef);
   
   /* propagate bounds on x */
   SCIP_CALL( propagateBoundsQuadVar(scip, cons, intervalinfty, x, xsqrcoef, lincoef, myrhs, result, nchgbds) );

   return SCIP_OKAY;
}

/** computes the minimal and maximal activity for the quadratic part in a constraint data
 *  only sums up terms that contribute finite values
 *  gives the number of terms that contribute infinite values
 *  only computes those activities where the corresponding side of the constraint is finite
 */
static
void propagateBoundsGetQuadActivity(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONSDATA*        consdata,           /**< constraint data */
   SCIP_Real             intervalinfty,      /**< infinity value used in interval operations */
   SCIP_Real*            minquadactivity,    /**< minimal activity of quadratic variable terms where only terms with finite minimal activity contribute */
   SCIP_Real*            maxquadactivity,    /**< maximal activity of quadratic variable terms where only terms with finite maximal activity contribute */
   int*                  minactivityinf,     /**< number of quadratic variables that contribute -infinity to minimal activity */
   int*                  maxactivityinf,     /**< number of quadratic variables that contribute +infinity to maximal activity */
   SCIP_INTERVAL*        quadactcontr        /**< contribution of each quadractic variables to quadactivity */
   )
{  /*lint --e{666}*/
   SCIP_ROUNDMODE prevroundmode;
   int       i;
   int       j;
   int       k;
   SCIP_INTERVAL tmp;
   SCIP_Real bnd;
   SCIP_INTERVAL xrng;
   SCIP_INTERVAL lincoef;

   assert(scip != NULL);
   assert(consdata != NULL);
   assert(minquadactivity != NULL);
   assert(maxquadactivity != NULL);
   assert(minactivityinf != NULL);
   assert(maxactivityinf != NULL);
   assert(quadactcontr != NULL);

   /* if lhs is -infinite, then we do not compute a maximal activity, so we set it to  infinity
    * if rhs is  infinite, then we do not compute a minimal activity, so we set it to -infinity
    */
   *minquadactivity = SCIPisInfinity(scip,  consdata->rhs) ? -intervalinfty : 0.0;
   *maxquadactivity = SCIPisInfinity(scip, -consdata->lhs) ?  intervalinfty : 0.0;
   
   *minactivityinf = 0;
   *maxactivityinf = 0;

   if( consdata->nquadvars == 0 )
   {
      SCIPintervalSet(&consdata->quadactivitybounds, 0.0); 
      return;
   }
   
   for( i = 0; i < consdata->nquadvars; ++i )
   {
      /* there should be no quadratic variables fixed at -/+ infinity due to our locks */
      assert(!SCIPisInfinity(scip,  SCIPvarGetLbLocal(consdata->quadvarterms[i].var)));
      assert(!SCIPisInfinity(scip, -SCIPvarGetUbLocal(consdata->quadvarterms[i].var)));

      SCIPintervalSetBounds(&quadactcontr[i], -intervalinfty, intervalinfty);

      SCIPintervalSetBounds(&xrng,
         -infty2infty(SCIPinfinity(scip), intervalinfty, -MIN(SCIPvarGetLbLocal(consdata->quadvarterms[i].var), SCIPvarGetUbLocal(consdata->quadvarterms[i].var))),
          infty2infty(SCIPinfinity(scip), intervalinfty,  MAX(SCIPvarGetLbLocal(consdata->quadvarterms[i].var), SCIPvarGetUbLocal(consdata->quadvarterms[i].var))));

      SCIPintervalSet(&lincoef, consdata->quadvarterms[i].lincoef);
      for( j = 0; j < consdata->quadvarterms[i].nadjbilin; ++j )
      {
         k = consdata->quadvarterms[i].adjbilin[j];
         if( consdata->bilinterms[k].var1 != consdata->quadvarterms[i].var )
            continue; /* handle this term later */

         SCIPintervalSetBounds(&tmp, 
            -infty2infty(SCIPinfinity(scip), intervalinfty, -MIN(SCIPvarGetLbLocal(consdata->bilinterms[k].var2), SCIPvarGetUbLocal(consdata->bilinterms[k].var2))),
             infty2infty(SCIPinfinity(scip), intervalinfty,  MAX(SCIPvarGetLbLocal(consdata->bilinterms[k].var2), SCIPvarGetUbLocal(consdata->bilinterms[k].var2))));
         SCIPintervalMulScalar(intervalinfty, &tmp, tmp, consdata->bilinterms[k].coef);
         SCIPintervalAdd(intervalinfty, &lincoef, lincoef, tmp);
      }

      if( !SCIPisInfinity(scip, -consdata->lhs) )
      {
         /* compute maximal activity only if there is a finite left hand side */
         bnd = SCIPintervalQuadUpperBound(intervalinfty, consdata->quadvarterms[i].sqrcoef, lincoef, xrng);
         if( SCIPisInfinity(scip,  bnd) )
         {
            ++*maxactivityinf;
         }
         else if( SCIPisInfinity(scip, -bnd) )
         {
            /* if maximal activity is below value for -infinity, let's take -1e10 as upper bound on maximal activity
             * @todo something better?
             */
            bnd = -sqrt(SCIPinfinity(scip));
            *maxquadactivity += bnd;
            quadactcontr[i].sup = bnd;
         }
         else
         {
            prevroundmode = SCIPintervalGetRoundingMode();
            SCIPintervalSetRoundingModeUpwards();
            *maxquadactivity += bnd;
            SCIPintervalSetRoundingMode(prevroundmode);
            quadactcontr[i].sup = bnd;
         }
      }
      
      if( !SCIPisInfinity(scip,  consdata->rhs) )
      {
         /* compute minimal activity only if there is a finite right hand side */
         SCIPintervalSetBounds(&lincoef, -SCIPintervalGetSup(lincoef), -SCIPintervalGetInf(lincoef));
         bnd = -SCIPintervalQuadUpperBound(intervalinfty, -consdata->quadvarterms[i].sqrcoef, lincoef, xrng);

         if( SCIPisInfinity(scip, -bnd) )
         {
            ++*minactivityinf;
         }
         else if( SCIPisInfinity(scip, bnd) )
         {
            /* if minimal activity is above value for infinity, let's take 1e10 as lower bound on minimal activity
             * @todo something better?
             */
            bnd = sqrt(SCIPinfinity(scip));
            *minquadactivity += bnd;
            quadactcontr[i].inf = bnd;
         }
         else
         {
            prevroundmode = SCIPintervalGetRoundingMode();
            SCIPintervalSetRoundingModeDownwards();
            *minquadactivity += bnd;
            SCIPintervalSetRoundingMode(prevroundmode);
            quadactcontr[i].inf = bnd;
         }
      }

   }
   
   SCIPintervalSetBounds(&consdata->quadactivitybounds,
      *minactivityinf > 0 ? -intervalinfty : *minquadactivity,
      *maxactivityinf > 0 ?  intervalinfty : *maxquadactivity);
   assert(!SCIPintervalIsEmpty(consdata->quadactivitybounds));
}

/** propagates bounds on a quadratic constraint */
static
SCIP_RETCODE propagateBoundsCons(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONSHDLR*        conshdlr,           /**< constraint handler */
   SCIP_CONS*            cons,               /**< constraint to process */
   SCIP_RESULT*          result,             /**< pointer to store the result of the propagation call */
   int*                  nchgbds,            /**< buffer where to add the the number of changed bounds */
   SCIP_Bool*            redundant           /**< buffer where to store whether constraint has been found to be redundant */
   )
{  /*lint --e{666}*/
   SCIP_CONSDATA*     consdata;
   SCIP_CONSHDLRDATA* conshdlrdata;
   SCIP_INTERVAL      consbounds;    /* lower and upper bounds of constraint */
   SCIP_INTERVAL      consactivity;  /* activity of linear plus quadratic part */
   SCIP_Real          intervalinfty; /* infinity used for interval computation */  
   SCIP_Real          minquadactivity; /* lower bound on finite activities of quadratic part */
   SCIP_Real          maxquadactivity; /* upper bound on finite activities of quadratic part */
   int                quadminactinf; /* number of quadratic variables that contribute -infinity to minimal activity of quadratic term */
   int                quadmaxactinf; /* number of quadratic variables that contribute +infinity to maximal activity of quadratic term */
   SCIP_INTERVAL*     quadactcontr;  /* constribution of each quadratic variable term to quadactivity */
   
   SCIP_VAR*          var;
   SCIP_INTERVAL      rhs;           /* right hand side of quadratic equation */
   SCIP_INTERVAL      tmp;
   SCIP_ROUNDMODE     roundmode;
   SCIP_Real          bnd;
   int                i;

   assert(scip != NULL);
   assert(conshdlr != NULL);
   assert(cons != NULL);
   assert(result != NULL);
   assert(nchgbds != NULL);
   assert(redundant != NULL);

   conshdlrdata = SCIPconshdlrGetData(conshdlr);
   assert(conshdlrdata != NULL);

   consdata = SCIPconsGetData(cons);
   assert(consdata != NULL);

   *result = SCIP_DIDNOTRUN;
   *redundant = FALSE;

   if( consdata->ispropagated )
      return SCIP_OKAY;

   *result = SCIP_DIDNOTFIND;

   intervalinfty = 1000 * SCIPinfinity(scip) * SCIPinfinity(scip);

   quadactcontr = NULL;
   quadminactinf = -1;
   quadmaxactinf = -1;

   SCIPdebugMessage("start domain propagation for constraint <%s>\n", SCIPconsGetName(cons));

   consdata->ispropagated = TRUE;

   /* make sure we have activity of linear term and that they are consistent */
   consdataUpdateLinearActivity(scip, consdata, intervalinfty);
   assert(consdata->minlinactivity != SCIP_INVALID);
   assert(consdata->maxlinactivity != SCIP_INVALID);
   assert(consdata->minlinactivityinf >= 0);
   assert(consdata->maxlinactivityinf >= 0);

   SCIPdebugMessage("linear activity: [%g, %g]   quadratic activity: [%g, %g]\n",
      consdata->minlinactivityinf > 0 ? -SCIPinfinity(scip) : consdata->minlinactivity,
      consdata->maxlinactivityinf > 0 ?  SCIPinfinity(scip) : consdata->maxlinactivity,
      consdata->quadactivitybounds.inf, consdata->quadactivitybounds.sup);

   /* compute activity of quad term part, if not up to date
    * in that case, we also collect the contribution of each quad var term for later */
   if( SCIPintervalIsEmpty(consdata->quadactivitybounds) )
   {
      SCIP_CALL( SCIPallocBufferArray(scip, &quadactcontr, consdata->nquadvars) );
      propagateBoundsGetQuadActivity(scip, consdata, intervalinfty, &minquadactivity, &maxquadactivity, &quadminactinf, &quadmaxactinf, quadactcontr);
      assert(!SCIPintervalIsEmpty(consdata->quadactivitybounds));
   }

   /* extend constraint bounds by feasibility tolerance to avoid some numerical difficulties */
   SCIPintervalSetBounds(&consbounds,
      -infty2infty(SCIPinfinity(scip), intervalinfty, -consdata->lhs+SCIPfeastol(scip)),
       infty2infty(SCIPinfinity(scip), intervalinfty,  consdata->rhs+SCIPfeastol(scip)));

   /* check redundancy and infeasibility */
   SCIPintervalSetBounds(&consactivity, consdata->minlinactivityinf > 0 ? -intervalinfty : consdata->minlinactivity, consdata->maxlinactivityinf > 0 ? intervalinfty : consdata->maxlinactivity);
   SCIPintervalAdd(intervalinfty, &consactivity, consactivity, consdata->quadactivitybounds);
   if( SCIPintervalIsSubsetEQ(intervalinfty, consactivity, consbounds) )
   {
      SCIPdebugMessage("found constraint <%s> to be redundant: sides: [%g, %g], activity: [%g, %g]\n",
         SCIPconsGetName(cons), consdata->lhs, consdata->rhs, SCIPintervalGetInf(consactivity), SCIPintervalGetSup(consactivity));
      *redundant = TRUE;
      goto propagateBoundsConsCleanup;
   }

   if( SCIPintervalAreDisjoint(consbounds, consactivity) )
   {
      SCIPdebugMessage("found constraint <%s> to be infeasible; sides: [%g, %g], activity: [%g, %g], infeas: %g\n",
         SCIPconsGetName(cons), consdata->lhs, consdata->rhs, SCIPintervalGetInf(consactivity), SCIPintervalGetSup(consactivity),
         MAX(consdata->lhs - SCIPintervalGetSup(consactivity), SCIPintervalGetInf(consactivity) - consdata->rhs));
      *result = SCIP_CUTOFF;
      goto propagateBoundsConsCleanup;
   }

   /* propagate linear part \in rhs = consbounds - quadactivity (use the one from consdata, since that includes infinities) */
   SCIPintervalSub(intervalinfty, &rhs, consbounds, consdata->quadactivitybounds);
   if( !SCIPintervalIsEntire(intervalinfty, rhs) )
   {
      SCIP_Real coef;

      for( i = 0; i < consdata->nlinvars; ++i )
      {
         coef = consdata->lincoefs[i];
         var  = consdata->linvars[i];

         /* skip fixed variables ??????????? */
         if( SCIPisEQ(scip, SCIPvarGetLbLocal(var), SCIPvarGetUbLocal(var)) )
            continue;

         if( coef > 0.0 )
         {
            if( SCIPintervalGetSup(rhs) < intervalinfty )
            {
               assert(consdata->minlinactivity != SCIP_INVALID);
               /* try to tighten the upper bound on var x */
               if( consdata->minlinactivityinf == 0 )
               {
                  assert(!SCIPisInfinity(scip, -SCIPvarGetLbLocal(var)));
                  /* tighten upper bound on x to (rhs.sup - (minlinactivity - coef * xlb)) / coef */
                  roundmode = SCIPintervalGetRoundingMode();
                  SCIPintervalSetRoundingModeUpwards();
                  bnd  = SCIPintervalGetSup(rhs);
                  bnd -= consdata->minlinactivity;
                  bnd += coef * SCIPvarGetLbLocal(var);
                  bnd /= coef;
                  SCIPintervalSetRoundingMode(roundmode);
                  SCIP_CALL( propagateBoundsTightenVarUb(scip, cons, intervalinfty, var, bnd, result, nchgbds) );
                  if( *result == SCIP_CUTOFF )
                     break;
               }
               else if( consdata->minlinactivityinf == 1 && SCIPisInfinity(scip, -SCIPvarGetLbLocal(var)) )
               {
                  /* x was the variable that made the minimal linear activity equal -infinity, so
                   * we tighten upper bound on x to just (rhs.sup - minlinactivity) / coef */
                  roundmode = SCIPintervalGetRoundingMode();
                  SCIPintervalSetRoundingModeUpwards();
                  bnd  = SCIPintervalGetSup(rhs);
                  bnd -= consdata->minlinactivity;
                  bnd /= coef;
                  SCIPintervalSetRoundingMode(roundmode);
                  SCIP_CALL( propagateBoundsTightenVarUb(scip, cons, intervalinfty, var, bnd, result, nchgbds) );
                  if( *result == SCIP_CUTOFF )
                     break;
               }
               /* otherwise the minimal activity is -infinity and x is not solely responsible for this */
            }

            if( SCIPintervalGetInf(rhs) > -intervalinfty )
            {
               assert(consdata->maxlinactivity != SCIP_INVALID);
               /* try to tighten the lower bound on var x */
               if( consdata->maxlinactivityinf == 0 )
               {
                  assert(!SCIPisInfinity(scip, SCIPvarGetUbLocal(var)));
                  /* tighten lower bound on x to (rhs.inf - (maxlinactivity - coef * xub)) / coef */
                  roundmode = SCIPintervalGetRoundingMode();
                  SCIPintervalSetRoundingModeDownwards();
                  bnd  = SCIPintervalGetInf(rhs);
                  bnd -= consdata->maxlinactivity;
                  bnd += coef * SCIPvarGetUbLocal(var);
                  bnd /= coef;
                  SCIPintervalSetRoundingMode(roundmode);
                  SCIP_CALL( propagateBoundsTightenVarLb(scip, cons, intervalinfty, var, bnd, result, nchgbds) );
                  if( *result == SCIP_CUTOFF )
                     break;
               }
               else if( consdata->maxlinactivityinf == 1 && SCIPisInfinity(scip, SCIPvarGetUbLocal(var)) )
               {
                  /* x was the variable that made the maximal linear activity equal infinity, so
                   * we tighten upper bound on x to just (rhs.inf - maxlinactivity) / coef */
                  roundmode = SCIPintervalGetRoundingMode();
                  SCIPintervalSetRoundingModeDownwards();
                  bnd  = SCIPintervalGetInf(rhs);
                  bnd -= consdata->maxlinactivity;
                  bnd /= coef;
                  SCIPintervalSetRoundingMode(roundmode);
                  SCIP_CALL( propagateBoundsTightenVarLb(scip, cons, intervalinfty, var, bnd, result, nchgbds) );
                  if( *result == SCIP_CUTOFF )
                     break;
               }
               /* otherwise the maximal activity is +infinity and x is not solely responsible for this */
            }
         }
         else
         {
            assert(coef < 0.0 );
            if( SCIPintervalGetInf(rhs) > -intervalinfty )
            {
               assert(consdata->maxlinactivity != SCIP_INVALID);
               /* try to tighten the upper bound on var x */
               if( consdata->maxlinactivityinf == 0 )
               {
                  assert(!SCIPisInfinity(scip, SCIPvarGetLbLocal(var)));
                  /* compute upper bound on x to (maxlinactivity - coef * xlb) - rhs.inf / (-coef) */
                  roundmode = SCIPintervalGetRoundingMode();
                  SCIPintervalSetRoundingModeUpwards();
                  bnd  = consdata->maxlinactivity;
                  bnd += (-coef) * SCIPvarGetLbLocal(var);
                  bnd -= SCIPintervalGetInf(rhs);
                  bnd /= (-coef);
                  SCIPintervalSetRoundingMode(roundmode);
                  SCIP_CALL( propagateBoundsTightenVarUb(scip, cons, intervalinfty, var, bnd, result, nchgbds) );
                  if( *result == SCIP_CUTOFF )
                     break;
               }
               else if( consdata->maxlinactivityinf == 1 && SCIPisInfinity(scip, -SCIPvarGetLbLocal(var)) )
               {
                  /* x was the variable that made the maximal linear activity equal infinity, so
                   * we tighten upper bound on x to just (maxlinactivity - rhs.inf) / (-coef) */
                  roundmode = SCIPintervalGetRoundingMode();
                  SCIPintervalSetRoundingModeUpwards();
                  bnd  = consdata->maxlinactivity;
                  bnd -= SCIPintervalGetInf(rhs);
                  bnd /= (-coef);
                  SCIPintervalSetRoundingMode(roundmode);
                  SCIP_CALL( propagateBoundsTightenVarUb(scip, cons, intervalinfty, var, bnd, result, nchgbds) );
                  if( *result == SCIP_CUTOFF )
                     break;
               }
               /* otherwise the maximal activity is infinity and x is not solely responsible for this */
            }

            if( SCIPintervalGetSup(rhs) < intervalinfty )
            {
               assert(consdata->minlinactivity != SCIP_INVALID);
               /* try to tighten the lower bound on var x */
               if( consdata->minlinactivityinf == 0 )
               {
                  assert(!SCIPisInfinity(scip, SCIPvarGetUbLocal(var)));
                  /* compute lower bound on x to (minlinactivity - coef * xub) - rhs.sup / (-coef) */
                  roundmode = SCIPintervalGetRoundingMode();
                  SCIPintervalSetRoundingModeDownwards();
                  bnd  = consdata->minlinactivity;
                  bnd += (-coef) * SCIPvarGetUbLocal(var);
                  bnd -= SCIPintervalGetSup(rhs);
                  bnd /= (-coef);
                  SCIPintervalSetRoundingMode(roundmode);
                  SCIP_CALL( propagateBoundsTightenVarLb(scip, cons, intervalinfty, var, bnd, result, nchgbds) );
                  if( *result == SCIP_CUTOFF )
                     break;
               }
               else if( consdata->minlinactivityinf == 1 && SCIPisInfinity(scip, SCIPvarGetUbLocal(var)) )
               {
                  /* x was the variable that made the maximal linear activity equal -infinity, so
                   * we tighten lower bound on x to just (minlinactivity - rhs.sup) / (-coef) */
                  roundmode = SCIPintervalGetRoundingMode();
                  SCIPintervalSetRoundingModeDownwards();
                  bnd  = consdata->minlinactivity;
                  bnd -= SCIPintervalGetSup(rhs);
                  bnd /= (-coef);
                  SCIPintervalSetRoundingMode(roundmode);
                  SCIP_CALL( propagateBoundsTightenVarLb(scip, cons, intervalinfty, var, bnd, result, nchgbds) );
                  if( *result == SCIP_CUTOFF )
                     break;
               }
               /* otherwise the minimal activity is -infinity and x is not solely responsible for this */
            }
         }
      }
      if( *result == SCIP_CUTOFF )
         goto propagateBoundsConsCleanup;
   }

   /* propagate quadratic part \in rhs = consbounds - linactivity */
   assert(consdata->minlinactivity != SCIP_INVALID);
   assert(consdata->maxlinactivity != SCIP_INVALID);
   consdataUpdateLinearActivity(scip, consdata, intervalinfty); /* make sure, activities of linear part did not become invalid by above boundchanges, if any */
   assert(consdata->minlinactivityinf > 0 || consdata->maxlinactivityinf > 0 || consdata->minlinactivity <= consdata->maxlinactivity);
   SCIPintervalSetBounds(&tmp,
      consdata->minlinactivityinf > 0 ? -intervalinfty : consdata->minlinactivity,
      consdata->maxlinactivityinf > 0 ?  intervalinfty : consdata->maxlinactivity);
   SCIPintervalSub(intervalinfty, &rhs, consbounds, tmp);
   if( !SCIPintervalIsEntire(intervalinfty, rhs) )
   {
      if( consdata->nquadvars == 1 )
      {
         /* quadratic part is just a*x^2+b*x -> a common case that we treat directly */
         SCIP_INTERVAL lincoef;    /* linear coefficient of quadratic equation */

         assert(consdata->nbilinterms == 0);

         var = consdata->quadvarterms[0].var;
         SCIPintervalSet(&lincoef, consdata->quadvarterms[0].lincoef);

         /* propagate a*x^2 + b*x \in rhs */
         SCIP_CALL( propagateBoundsQuadVar(scip, cons, intervalinfty, var, consdata->quadvarterms[0].sqrcoef, lincoef, rhs, result, nchgbds) );
      }
      else if( consdata->nbilinterms == 1 && consdata->nquadvars == 2 )
      {
         /* quadratic part is just ax*x^2+bx*x + ay*y^2+by*y + c*xy -> a common case that we treat directly */
         assert(consdata->bilinterms[0].var1 == consdata->quadvarterms[0].var || consdata->bilinterms[0].var1 == consdata->quadvarterms[1].var);
         assert(consdata->bilinterms[0].var2 == consdata->quadvarterms[0].var || consdata->bilinterms[0].var2 == consdata->quadvarterms[1].var);

         /* find domain reductions for x from a_x x^2 + b_x x + a_y y^2 + b_y y + c x y \in rhs */
         SCIP_CALL( propagateBoundsBilinearTerm(scip, cons, intervalinfty,
            consdata->quadvarterms[0].var, consdata->quadvarterms[0].sqrcoef, consdata->quadvarterms[0].lincoef,
            consdata->quadvarterms[1].var, consdata->quadvarterms[1].sqrcoef, consdata->quadvarterms[1].lincoef,
            consdata->bilinterms[0].coef,
            rhs, result, nchgbds) );
         if( *result != SCIP_CUTOFF )
         {
            /* find domain reductions for y from a_x x^2 + b_x x + a_y y^2 + b_y y + c x y \in rhs */
            SCIP_CALL( propagateBoundsBilinearTerm(scip, cons, intervalinfty,
               consdata->quadvarterms[1].var, consdata->quadvarterms[1].sqrcoef, consdata->quadvarterms[1].lincoef,
               consdata->quadvarterms[0].var, consdata->quadvarterms[0].sqrcoef, consdata->quadvarterms[0].lincoef,
               consdata->bilinterms[0].coef,
               rhs, result, nchgbds) );
         }
      }
      else
      {
         /* general case */

         /* compute "advanced" information on quad var term activities, if not uptodate */
         if( quadminactinf == -1  )
         {
            assert(quadactcontr == NULL);
            SCIP_CALL( SCIPallocBufferArray(scip, &quadactcontr, consdata->nquadvars) );
            propagateBoundsGetQuadActivity(scip, consdata, intervalinfty, &minquadactivity, &maxquadactivity, &quadminactinf, &quadmaxactinf, quadactcontr);
         }
         assert(quadactcontr != NULL);
         assert(quadminactinf >= 0);
         assert(quadmaxactinf >= 0);

         /* if the quad activities are not hopelessly unbounded on useful sides, try to deduce domain reductions on quad vars */
         if( (SCIPintervalGetSup(rhs) <  intervalinfty && quadminactinf <= 1) ||
             (SCIPintervalGetInf(rhs) > -intervalinfty && quadmaxactinf <= 1) )
         {
            SCIP_INTERVAL lincoef;
            SCIP_INTERVAL rhs2;
            int j;
            int k;

            for( i = 0; i < consdata->nquadvars; ++i )
            {
               var = consdata->quadvarterms[i].var;

               /* skip fixed variables ??????????? */
               if( SCIPisEQ(scip, SCIPvarGetLbLocal(var), SCIPvarGetUbLocal(var)) )
                  continue;

               /* compute rhs2 such that we can propagate quadvarterm(x_i) \in rhs2 */

               /* setup rhs2.sup = rhs.sup - (quadactivity.inf - quadactcontr[i].inf), if everything were finite
                * if only quadactcontr[i].inf is infinite (i.e., the other i are all finite), we just get rhs2.sup = rhs.sup
                * otherwise we get rhs2.sup = infinity */
               if( SCIPintervalGetSup(rhs) < intervalinfty )
               {
                  if( quadminactinf == 0 || (quadminactinf == 1 && SCIPintervalGetInf(quadactcontr[i]) <= -intervalinfty) )
                  {
                     /* the residual quad min activity w.r.t. quad var term i is finite */
                     assert(!SCIPisInfinity(scip, -minquadactivity));  /*lint !e644*/
                     roundmode = SCIPintervalGetRoundingMode();
                     SCIPintervalSetRoundingModeUpwards();
                     rhs2.sup = rhs.sup - minquadactivity;
                     if( quadminactinf == 0 && SCIPintervalGetInf(quadactcontr[i]) != 0.0 )
                     {
                        /* the residual quad min activity w.r.t. quad var term i is finite and nonzero, so add it to right hand side */
                        assert(!SCIPisInfinity(scip, -SCIPintervalGetInf(quadactcontr[i])));
                        rhs2.sup += SCIPintervalGetInf(quadactcontr[i]);
                     }
                     SCIPintervalSetRoundingMode(roundmode);
                  }
                  else
                  {
                     /* there are either >= 2 quad var terms contributing -infinity, or there is one which is not i */
                     rhs2.sup = intervalinfty;
                  }
               }
               else
               {
                  rhs2.sup = intervalinfty;
               }

               /* setup rhs.inf = rhs.inf - (quadactivity.sup - quadactcontr[i].sup), see also above */
               if( SCIPintervalGetInf(rhs) > -intervalinfty )
               {
                  if( quadmaxactinf == 0 || (quadmaxactinf == 1 && SCIPintervalGetSup(quadactcontr[i]) >= intervalinfty) )
                  {
                     /* the residual quad max activity w.r.t. quad var term i is finite and nonzero, so add it to right hand side */
                     assert(!SCIPisInfinity(scip, maxquadactivity));  /*lint !e644*/
                     roundmode = SCIPintervalGetRoundingMode();
                     SCIPintervalSetRoundingModeDownwards();
                     rhs2.inf = rhs.inf - maxquadactivity;
                     /* the residual quad max activity w.r.t. quad var term i is finite */
                     if( quadmaxactinf == 0 && SCIPintervalGetSup(quadactcontr[i]) != 0.0 )
                     {
                        assert(!SCIPisInfinity(scip, SCIPintervalGetSup(quadactcontr[i])));
                        rhs2.inf += SCIPintervalGetSup(quadactcontr[i]);
                     }
                     SCIPintervalSetRoundingMode(roundmode);
                  }
                  else
                  {
                     /* there are either >= 2 quad var terms contributing infinity, or there is one which is not i */
                     rhs2.inf = -intervalinfty;
                  }
               }
               else
               {
                  rhs2.inf = -intervalinfty;
               }
               assert(!SCIPintervalIsEmpty(rhs2));

               /* if rhs2 is entire, then there is nothing we could propagate */
               if( SCIPintervalIsEntire(intervalinfty, rhs2) )
                  continue;

               /* assemble linear coefficient for quad equation a*x^2 + b*x \in rhs2 */
               SCIPintervalSet(&lincoef, consdata->quadvarterms[i].lincoef);
               for( j = 0; j < consdata->quadvarterms[i].nadjbilin; ++j )
               {
                  k = consdata->quadvarterms[i].adjbilin[j];
                  if( consdata->bilinterms[k].var1 != var )
                     continue; /* this term does not contribute to the activity of quad var term i */

                  SCIPintervalSetBounds(&tmp,
                     -infty2infty(SCIPinfinity(scip), intervalinfty, -MIN(SCIPvarGetLbLocal(consdata->bilinterms[k].var2), SCIPvarGetUbLocal(consdata->bilinterms[k].var2))),
                      infty2infty(SCIPinfinity(scip), intervalinfty,  MAX(SCIPvarGetLbLocal(consdata->bilinterms[k].var2), SCIPvarGetUbLocal(consdata->bilinterms[k].var2))));
                  SCIPintervalMulScalar(intervalinfty, &tmp, tmp, consdata->bilinterms[k].coef);
                  SCIPintervalAdd(intervalinfty, &lincoef, lincoef, tmp);
               }

               /* deduce domain reductions for x_i */
               SCIP_CALL( propagateBoundsQuadVar(scip, cons, intervalinfty, var, consdata->quadvarterms[i].sqrcoef, lincoef, rhs2, result, nchgbds) );
               if( *result == SCIP_CUTOFF )
                  goto propagateBoundsConsCleanup;
            }
         }
      }
   }

propagateBoundsConsCleanup:
   SCIPfreeBufferArrayNull(scip, &quadactcontr);
   
   return SCIP_OKAY;
}

/** calls domain propagation for a set of constraints */
static
SCIP_RETCODE propagateBounds(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONSHDLR*        conshdlr,           /**< constraint handler */
   SCIP_CONS**           conss,              /**< constraints to process */
   int                   nconss,             /**< number of constraints */
   SCIP_RESULT*          result,             /**< pointer to store the result of the propagation calls */
   int*                  nchgbds             /**< buffer where to add the the number of changed bounds */
)
{
   SCIP_CONSHDLRDATA* conshdlrdata;
   SCIP_RESULT propresult;
   SCIP_Bool   redundant;
   int         c;
   int         roundnr;
   SCIP_Bool   success;

   assert(scip != NULL);
   assert(conshdlr != NULL);
   assert(conss != NULL || nconss == 0);
   assert(result != NULL);
   assert(nchgbds != NULL);

   conshdlrdata = SCIPconshdlrGetData(conshdlr);
   assert(conshdlrdata != NULL);

   *result = SCIP_DIDNOTFIND;
   roundnr = 0;

   do
   {
      success = FALSE;

      SCIPdebugMessage("starting domain propagation round %d for %d constraints\n", roundnr, nconss);

      for( c = 0; c < nconss && *result != SCIP_CUTOFF; ++c )
      {
         assert(conss != NULL);
         if( !SCIPconsIsEnabled(conss[c]) )
            continue;

         SCIP_CALL( propagateBoundsCons(scip, conshdlr, conss[c], &propresult, nchgbds, &redundant) );
         if( propresult != SCIP_DIDNOTFIND && propresult != SCIP_DIDNOTRUN )
         {
            *result = propresult;
            success = TRUE;
         }
         if( redundant )
         {
            SCIP_CALL( SCIPdelConsLocal(scip, conss[c]) );
         }
      }

   } while( success && *result != SCIP_CUTOFF && ++roundnr < conshdlrdata->maxproprounds );

   return SCIP_OKAY;
}

/** Given a solution where every quadratic constraint is either feasible or can be made feasible by
 * moving a linear variable, construct the corresponding feasible solution and pass it to the trysol heuristic.
 * The method assumes that this is always possible and that not all constraints are feasible already.
 */
static
SCIP_RETCODE proposeFeasibleSolution(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONSHDLR*        conshdlr,           /**< constraint handler */
   SCIP_CONS**           conss,              /**< constraints to process */
   int                   nconss,             /**< number of constraints */
   SCIP_SOL*             sol,                /**< solution to process */
   SCIP_Bool*            success             /**< buffer to store whether we succeeded to construct a solution that satisfies all provided constraints */
)
{
   SCIP_CONSDATA* consdata;
   SCIP_SOL* newsol;
   SCIP_VAR* var;
   int c;
   SCIP_Real viol;
   SCIP_Real norm;
   SCIP_Real delta;
   SCIP_Real gap;

   assert(scip  != NULL);
   assert(conshdlr != NULL);
   assert(conss != NULL || nconss == 0);
   assert(success != NULL);

   *success = FALSE;

   if( sol != NULL )
   {
      SCIP_CALL( SCIPcreateSolCopy(scip, &newsol, sol) );
   }
   else
   {
      SCIP_CALL( SCIPcreateLPSol(scip, &newsol, NULL) );
   }
   SCIP_CALL( SCIPunlinkSol(scip, newsol) );

   for( c = 0; c < nconss; ++c )
   {
      consdata = SCIPconsGetData(conss[c]);  /*lint !e613*/
      assert(consdata != NULL);

      /* recompute violation of solution in case solution has changed
       * get absolution violation and sign */
      if( SCIPisFeasPositive(scip, consdata->lhsviol) )
      {
         SCIP_CALL( computeViolation(scip, conss[c], newsol, TRUE) );  /*lint !e613*/
         viol = consdata->lhs - consdata->activity;
      }
      else if( SCIPisFeasPositive(scip, consdata->rhsviol) )
      {
         SCIP_CALL( computeViolation(scip, conss[c], newsol, TRUE) );  /*lint !e613*/
         viol = consdata->rhs - consdata->activity;
      }
      else
         continue; /* constraint is satisfied */

      assert(viol != 0.0);
      if( consdata->linvar_mayincrease >= 0 &&
           ((viol > 0.0 && consdata->lincoefs[consdata->linvar_mayincrease] > 0.0) ||
            (viol < 0.0 && consdata->lincoefs[consdata->linvar_mayincrease] < 0.0)) )
      {
         /* have variable where increasing makes the constraint less violated */
         var = consdata->linvars[consdata->linvar_mayincrease];
         /* compute how much we would like to increase var */
         delta = viol / consdata->lincoefs[consdata->linvar_mayincrease];
         assert(delta > 0.0);
         /* if var has an upper bound, may need to reduce delta */
         if( !SCIPisInfinity(scip, SCIPvarGetUbGlobal(var)) )
         {
            gap = SCIPvarGetUbGlobal(var) - SCIPgetSolVal(scip, newsol, var);
            delta = MIN(MAX(0.0, gap), delta);
         }
         if( SCIPisPositive(scip, delta) )
         {
            /* if variable is integral, round delta up so that it will still have an integer value */
            if( SCIPvarIsIntegral(var) )
               delta = SCIPceil(scip, delta);

            SCIP_CALL( SCIPincSolVal(scip, newsol, var, delta) );
            SCIPdebugMessage("increase <%s> by %g to %g\n", SCIPvarGetName(var), delta, SCIPgetSolVal(scip, newsol, var));

            /* adjust constraint violation, if satisfied go on to next constraint */
            viol -= consdata->lincoefs[consdata->linvar_mayincrease] * delta;
            if( SCIPisZero(scip, viol) )
               continue;
         }
      }

      assert(viol != 0.0);
      if( consdata->linvar_maydecrease >= 0 &&
         ((viol > 0.0 && consdata->lincoefs[consdata->linvar_maydecrease] < 0.0) ||
          (viol < 0.0 && consdata->lincoefs[consdata->linvar_maydecrease] > 0.0)) )
      {
         /* have variable where decreasing makes constraint less violated */
         var = consdata->linvars[consdata->linvar_maydecrease];
         /* compute how much we would like to decrease var */
         delta = viol / consdata->lincoefs[consdata->linvar_maydecrease];
         assert(delta < 0.0);
         /* if var has a lower bound, may need to reduce delta */
         if( !SCIPisInfinity(scip, -SCIPvarGetLbGlobal(var)) )
         {
            gap = SCIPgetSolVal(scip, newsol, var) - SCIPvarGetLbGlobal(var);
            delta = MAX(MIN(0.0, gap), delta);
         }
         if( SCIPisNegative(scip, delta) )
         {
            /* if variable is integral, round delta down so that it will still have an integer value */
            if( SCIPvarIsIntegral(var) )
               delta = SCIPfloor(scip, delta);
            SCIP_CALL( SCIPincSolVal(scip, newsol, var, delta) );
            SCIPdebugMessage("increase <%s> by %g to %g\n", SCIPvarGetName(var), delta, SCIPgetSolVal(scip, newsol, var));

            /* adjust constraint violation, if satisfied go on to next constraint */
            viol -= consdata->lincoefs[consdata->linvar_maydecrease] * delta;
            if( SCIPisZero(scip, viol) )
               continue;
         }
      }

      /* still here... so maybe we could not make constraint feasible due to variable bounds
       * check if we are feasible w.r.t. (relative) feasibility tolerance */
      norm = getGradientNorm(scip, conss[c], newsol);  /*lint !e613*/
      if( norm > 1.0 )
         viol /= norm;
      /* if still violated, we give up */
      if( SCIPisFeasPositive(scip, REALABS(viol)) )
         break;

      /* if objective value is not better than current upper bound, we give up */
      if( !SCIPisInfinity(scip, SCIPgetUpperbound(scip)) && !SCIPisSumLT(scip, SCIPgetSolTransObj(scip, newsol), SCIPgetUpperbound(scip)) )
         break;
   }

   /* if we have a solution that should satisfy all quadratic constraints and has a better objective than the current upper bound,
    * then pass it to the trysol heuristic */
   if( c == nconss )
   {
      SCIP_CONSHDLRDATA* conshdlrdata;

      SCIPdebugMessage("pass solution with objective val %g to trysol heuristic\n", SCIPgetSolTransObj(scip, newsol));

      conshdlrdata = SCIPconshdlrGetData(conshdlr);
      assert(conshdlrdata != NULL);
      assert(conshdlrdata->trysolheur != NULL);

      SCIP_CALL( SCIPheurPassSolTrySol(scip, conshdlrdata->trysolheur, newsol) );
      *success = TRUE;
   }

   SCIP_CALL( SCIPfreeSol(scip, &newsol) );

   return SCIP_OKAY;
}

/*
 * Callback methods of constraint handler
 */

/** copy method for constraint handler plugins (called when SCIP copies plugins) */
static
SCIP_DECL_CONSHDLRCOPY(conshdlrCopyQuadratic)
{  /*lint --e{715}*/
   assert(scip != NULL);
   assert(conshdlr != NULL);
   assert(strcmp(SCIPconshdlrGetName(conshdlr), CONSHDLR_NAME) == 0);

   /* call inclusion method of constraint handler */
   SCIP_CALL( SCIPincludeConshdlrQuadratic(scip) );
 
   *valid = TRUE;

   return SCIP_OKAY;
}

/** destructor of constraint handler to free constraint handler data (called when SCIP is exiting) */
static
SCIP_DECL_CONSFREE(consFreeQuadratic)
{
   SCIP_CONSHDLRDATA* conshdlrdata;
   int                i;
   
   assert(scip     != NULL);
   assert(conshdlr != NULL);
   
   conshdlrdata = SCIPconshdlrGetData(conshdlr);
   assert(conshdlrdata != NULL);
   
   for( i = 0; i < conshdlrdata->nquadconsupgrades; ++i )
   {
      assert(conshdlrdata->quadconsupgrades[i] != NULL);
      SCIPfreeMemory(scip, &conshdlrdata->quadconsupgrades[i]);
   }
   SCIPfreeMemoryArrayNull(scip, &conshdlrdata->quadconsupgrades);
   
   SCIPfreeMemory(scip, &conshdlrdata);
   
   return SCIP_OKAY;
}

/** initialization method of constraint handler (called after problem was transformed) */
static
SCIP_DECL_CONSINIT(consInitQuadratic)
{  /*lint --e{715} */
   SCIP_CONSHDLRDATA* conshdlrdata;
   int c;
   
   assert(scip != NULL);
   assert(conshdlr != NULL);
   
   conshdlrdata = SCIPconshdlrGetData(conshdlr);
   assert(conshdlrdata != NULL);
   
#ifdef USECLOCK   
   SCIP_CALL( SCIPcreateClock(scip, &conshdlrdata->clock1) );
   SCIP_CALL( SCIPcreateClock(scip, &conshdlrdata->clock2) );
   SCIP_CALL( SCIPcreateClock(scip, &conshdlrdata->clock3) );
#endif
   
   conshdlrdata->subnlpheur = SCIPfindHeur(scip, "subnlp");
   conshdlrdata->rensheur   = SCIPfindHeur(scip, "rens");
   conshdlrdata->trysolheur = SCIPfindHeur(scip, "trysol");

   /* catch variable events */
   for( c = 0; c < nconss; ++c )
   {
      SCIP_CALL( catchVarEvents(scip, conshdlrdata->eventhdlr, conss[c]) );
   }
   
   return SCIP_OKAY;
}


/** deinitialization method of constraint handler (called before transformed problem is freed) */
static
SCIP_DECL_CONSEXIT(consExitQuadratic)
{  /*lint --e{715} */
   SCIP_CONSHDLRDATA* conshdlrdata;
   int c;
   
   assert(scip != NULL);
   assert(conshdlr != NULL);
   
   conshdlrdata = SCIPconshdlrGetData(conshdlr);
   assert(conshdlrdata != NULL);
   
   /* drop variable events */
   for( c = 0; c < nconss; ++c )
   {
      SCIP_CALL( dropVarEvents(scip, conshdlrdata->eventhdlr, conss[c]) );
   }

#ifdef USECLOCK
   printf("clock1: %g\t clock2: %g\t clock3: %g\n", SCIPgetClockTime(scip, conshdlrdata->clock1), SCIPgetClockTime(scip, conshdlrdata->clock2), SCIPgetClockTime(scip, conshdlrdata->clock3));
   SCIP_CALL( SCIPfreeClock(scip, &conshdlrdata->clock1) );
   SCIP_CALL( SCIPfreeClock(scip, &conshdlrdata->clock2) );
   SCIP_CALL( SCIPfreeClock(scip, &conshdlrdata->clock3) );
#endif
   
   conshdlrdata->subnlpheur = NULL;
   conshdlrdata->rensheur   = NULL;
   conshdlrdata->trysolheur = NULL;

   return SCIP_OKAY;
}

/** presolving initialization method of constraint handler (called when presolving is about to begin) */
static
SCIP_DECL_CONSINITPRE(consInitpreQuadratic)
{
   SCIP_CONSHDLRDATA* conshdlrdata;
   SCIP_CONSDATA* consdata;
   int c;
   
   assert(scip != NULL);
   assert(conshdlr != NULL);
   assert(conss != NULL || nconss == 0);
   
   conshdlrdata = SCIPconshdlrGetData(conshdlr);
   assert(conshdlrdata != NULL);
   
   *result = SCIP_FEASIBLE;

   for( c = 0; c < nconss; ++c )
   {
      consdata = SCIPconsGetData(conss[c]);  /*lint !e613*/
      assert(consdata != NULL);

      /* reset linvar_may{in,de}crease to -1 in case some values are still set from a previous solve round */
      consdata->linvar_mayincrease = -1;
      consdata->linvar_maydecrease = -1;
   }

   return SCIP_OKAY;
}

/** presolving deinitialization method of constraint handler (called after presolving has been finished) */
static
SCIP_DECL_CONSEXITPRE(consExitpreQuadratic)
{
   SCIP_CONSHDLRDATA* conshdlrdata;
   SCIP_CONSDATA*     consdata;
   int                i;
   int                c;
   
   assert(scip != NULL);
   assert(conshdlr != NULL);
   assert(conss != NULL || nconss == 0);
   
   conshdlrdata = SCIPconshdlrGetData(conshdlr);
   assert(conshdlrdata != NULL);

   *result = SCIP_FEASIBLE;

   for( c = 0; c < nconss; ++c )
   {
      assert(conss != NULL);
      consdata = SCIPconsGetData(conss[c]);
      assert(consdata != NULL);

      if( !consdata->isremovedfixings )
      {
         SCIP_CALL( removeFixedVariables(scip, conss[c]) );
      }
      /* make sure we do not have duplicate bilinear terms, quad var terms, or linear vars */
      SCIP_CALL( mergeAndCleanBilinearTerms(scip, conss[c]) );
      SCIP_CALL( mergeAndCleanQuadVarTerms(scip, conss[c]) );
      SCIP_CALL( mergeAndCleanLinearVars(scip, conss[c]) );

      assert(consdata->isremovedfixings);
      assert(consdata->linvarsmerged);
      assert(consdata->quadvarsmerged);
      assert(consdata->bilinmerged);

#ifndef NDEBUG
      for( i = 0; i < consdata->nlinvars; ++i )
         assert(SCIPvarIsActive(consdata->linvars[i]));

      for( i = 0; i < consdata->nquadvars; ++i )
         assert(SCIPvarIsActive(consdata->quadvarterms[i].var));
#endif

      SCIP_CALL( boundUnboundedVars(scip, conss[c], conshdlrdata->defaultbound, NULL) );
      
      /* tell SCIP that we have something nonlinear */
      if( consdata->nquadvars > 0 )
      {
         SCIPmarkNonlinearitiesPresent(scip);
         if( !SCIPhasContinuousNonlinearitiesPresent(scip) )
            for( i = 0; i < consdata->nquadvars; ++i )
               if( SCIPvarGetType(consdata->quadvarterms[i].var) >= SCIP_VARTYPE_CONTINUOUS )
               {
                  SCIPmarkContinuousNonlinearitiesPresent(scip);
                  break;
               }
      }
   }

   return SCIP_OKAY;
}

/** solving process initialization method of constraint handler (called when branch and bound process is about to begin) */
static
SCIP_DECL_CONSINITSOL(consInitsolQuadratic)
{
   SCIP_CONSHDLRDATA* conshdlrdata;
   SCIP_CONSDATA*     consdata;
   int                c, i;
   int                poslock;
   int                neglock;
   
   assert(scip     != NULL);
   assert(conshdlr != NULL);
   assert(conss    != NULL || nconss == 0);
   
   conshdlrdata = SCIPconshdlrGetData(conshdlr);
   assert(conshdlrdata != NULL);
   
   for( c = 0; c < nconss; ++c )
   {
      assert(conss != NULL);
      consdata = SCIPconsGetData(conss[c]);
      assert(consdata != NULL);

      /* check for a linear variable that can be increase or decreased without harming feasibility */
      for( i = 0; i < consdata->nlinvars; ++i )
      {
         /* compute locks of i'th linear variable */
         if( consdata->lincoefs[i] > 0.0 )
         {
            poslock = !SCIPisInfinity(scip, -consdata->lhs) ? 1 : 0;
            neglock = !SCIPisInfinity(scip,  consdata->rhs) ? 1 : 0;
         }
         else
         {
            poslock = !SCIPisInfinity(scip,  consdata->rhs) ? 1 : 0;
            neglock = !SCIPisInfinity(scip, -consdata->lhs) ? 1 : 0;
         }

         if( SCIPvarGetNLocksDown(consdata->linvars[i]) - neglock == 0 )
         {
            /* for a*x + q(y) \in [lhs, rhs], we can decrease x without harming other constraints */
            /* if we have already one candidate, then take the one where the loss in the objective function is less */
            if( (consdata->linvar_maydecrease < 0) ||
               (SCIPvarGetObj(consdata->linvars[consdata->linvar_maydecrease]) / consdata->lincoefs[consdata->linvar_maydecrease]
                 > SCIPvarGetObj(consdata->linvars[i]) / consdata->lincoefs[i]) )
               consdata->linvar_maydecrease = i;
         }

         if( SCIPvarGetNLocksDown(consdata->linvars[i]) - poslock == 0 )
         {
            /* for a*x + q(y) \in [lhs, rhs], we can increase x without harm */
            /* if we have already one candidate, then take the one where the loss in the objective function is less */
            if( (consdata->linvar_mayincrease < 0) ||
               (SCIPvarGetObj(consdata->linvars[consdata->linvar_mayincrease]) / consdata->lincoefs[consdata->linvar_mayincrease]
                 > SCIPvarGetObj(consdata->linvars[i]) / consdata->lincoefs[i]) )
               consdata->linvar_mayincrease = i;
         }
      }
#ifdef SCIP_DEBUG
      if( consdata->linvar_mayincrease >= 0 )
      {
         SCIPdebugMessage("may increase <%s> to become feasible\n", SCIPvarGetName(consdata->linvars[consdata->linvar_mayincrease]));
      }
      if( consdata->linvar_maydecrease >= 0 )
      {
         SCIPdebugMessage("may decrease <%s> to become feasible\n", SCIPvarGetName(consdata->linvars[consdata->linvar_maydecrease]));
      }
#endif

      /* add nlrow respresentation to NLP, if NLP had been constructed */
      if( SCIPisNLPConstructed(scip) )
      {
         if( consdata->nlrow == NULL )
         {
            SCIP_CALL( createNlRow(scip, conss[c]) );
            assert(consdata->nlrow != NULL);
         }
         SCIP_CALL( SCIPaddNlRow(scip, consdata->nlrow) );
      }
   }

   conshdlrdata->newsoleventfilterpos = -1;
   if( nconss != 0 && (conshdlrdata->subnlpheur != NULL || conshdlrdata->rensheur != NULL) && conshdlrdata->linearizenlpsol )
   {
      SCIP_EVENTHDLR* eventhdlr;

      eventhdlr = SCIPfindEventhdlr(scip, CONSHDLR_NAME"_newsolution");
      assert(eventhdlr != NULL);

      SCIP_CALL( SCIPcatchEvent(scip, SCIP_EVENTTYPE_SOLFOUND, eventhdlr, (SCIP_EVENTDATA*)conshdlr, &conshdlrdata->newsoleventfilterpos) );
   }

   if( nconss != 0 && !SCIPisIpoptAvailableIpopt() && !SCIPisInRestart(scip) )
   {
      SCIPverbMessage(scip, SCIP_VERBLEVEL_HIGH, NULL, "Quadratic constraint handler does not have LAPACK for eigenvalue computation. Will assume that matrices (with size > 2x2) are indefinite.\n");
   }

   return SCIP_OKAY;
}

/** solving process deinitialization method of constraint handler (called before branch and bound process data is freed) */
static
SCIP_DECL_CONSEXITSOL(consExitsolQuadratic)
{  /*lint --e{715}*/
   SCIP_CONSHDLRDATA* conshdlrdata;
   SCIP_CONSDATA* consdata;
   int c;
   
   assert(scip != NULL);
   assert(conshdlr != NULL);
   assert(conss != NULL || nconss == 0);
   
   conshdlrdata = SCIPconshdlrGetData(conshdlr);
   assert(conshdlrdata != NULL);

   if( conshdlrdata->newsoleventfilterpos >= 0 )
   {
      SCIP_EVENTHDLR* eventhdlr;

      /* failing of the following events mean that new solution events should not have been catched */
      assert(conshdlrdata->subnlpheur != NULL || conshdlrdata->rensheur != NULL);
      assert(conshdlrdata->linearizenlpsol);

      eventhdlr = SCIPfindEventhdlr(scip, CONSHDLR_NAME"_newsolution");
      assert(eventhdlr != NULL);

      SCIP_CALL( SCIPdropEvent(scip, SCIP_EVENTTYPE_SOLFOUND, eventhdlr, (SCIP_EVENTDATA*)conshdlr, conshdlrdata->newsoleventfilterpos) );
      conshdlrdata->newsoleventfilterpos = -1;
   }

   for( c = 0; c < nconss; ++c )
   {
      consdata = SCIPconsGetData(conss[c]);  /*lint !e613*/
      assert(consdata != NULL);

      /* free nonlinear row representation */
      if( consdata->nlrow != NULL )
      {
         SCIP_CALL( SCIPreleaseNlRow(scip, &consdata->nlrow) );
      }
   }

   return SCIP_OKAY;
}

/** frees specific constraint data */
static
SCIP_DECL_CONSDELETE(consDeleteQuadratic)
{
   SCIP_CONSHDLRDATA* conshdlrdata;
   
   assert(scip != NULL);
   assert(conshdlr != NULL);
   assert(cons != NULL);
   assert(consdata != NULL);
   assert(SCIPconsGetData(cons) == *consdata);
   
   conshdlrdata = SCIPconshdlrGetData(conshdlr);
   assert(conshdlrdata != NULL);

   if( SCIPconsIsTransformed(cons) )
   {
      SCIP_CALL( dropVarEvents(scip, conshdlrdata->eventhdlr, cons) );
   }

   SCIP_CALL( consdataFree(scip, consdata) );

   assert(*consdata == NULL);

   return SCIP_OKAY;
}

/** transforms constraint data into data belonging to the transformed problem */
static
SCIP_DECL_CONSTRANS(consTransQuadratic)
{  
   SCIP_CONSDATA* sourcedata;
   SCIP_CONSDATA* targetdata;
   int            i;

   sourcedata = SCIPconsGetData(sourcecons);
   assert(sourcedata != NULL);
   
   SCIP_CALL( consdataCreate(scip, &targetdata,
      sourcedata->lhs, sourcedata->rhs,
      sourcedata->nlinvars, sourcedata->linvars, sourcedata->lincoefs,
      sourcedata->nquadvars, sourcedata->quadvarterms,
      sourcedata->nbilinterms, sourcedata->bilinterms,
      FALSE) );
   
   for( i = 0; i < targetdata->nlinvars; ++i )
   {
      SCIP_CALL( SCIPgetTransformedVar(scip, targetdata->linvars[i], &targetdata->linvars[i]) );
      SCIP_CALL( SCIPcaptureVar(scip, targetdata->linvars[i]) );
   }
   
   for( i = 0; i < targetdata->nquadvars; ++i )
   {
      SCIP_CALL( SCIPgetTransformedVar(scip, targetdata->quadvarterms[i].var, &targetdata->quadvarterms[i].var) );
      SCIP_CALL( SCIPcaptureVar(scip, targetdata->quadvarterms[i].var) );
   }
   
   for( i = 0; i < targetdata->nbilinterms; ++i )
   {
      SCIP_CALL( SCIPgetTransformedVar(scip, targetdata->bilinterms[i].var1, &targetdata->bilinterms[i].var1) );
      SCIP_CALL( SCIPgetTransformedVar(scip, targetdata->bilinterms[i].var2, &targetdata->bilinterms[i].var2) );
   }
   
   /* create target constraint */
   SCIP_CALL( SCIPcreateCons(scip, targetcons, SCIPconsGetName(sourcecons), conshdlr, targetdata,
      SCIPconsIsInitial(sourcecons), SCIPconsIsSeparated(sourcecons), SCIPconsIsEnforced(sourcecons),
      SCIPconsIsChecked(sourcecons), SCIPconsIsPropagated(sourcecons),  SCIPconsIsLocal(sourcecons),
      SCIPconsIsModifiable(sourcecons), SCIPconsIsDynamic(sourcecons), SCIPconsIsRemovable(sourcecons),
      SCIPconsIsStickingAtNode(sourcecons)) );

   SCIPdebugMessage("created transformed quadratic constraint ");
   SCIPdebug( SCIPprintCons(scip, *targetcons, NULL) );

   return SCIP_OKAY;
}

/** LP initialization method of constraint handler */
static
SCIP_DECL_CONSINITLP(consInitlpQuadratic)
{  
   SCIP_CONSHDLRDATA* conshdlrdata;
   SCIP_CONSDATA*     consdata;
   SCIP_ROW*          row;
   int                i;

   assert(scip != NULL);
   assert(conshdlr != NULL);
   assert(conss != NULL || nconss == 0);
   
   conshdlrdata = SCIPconshdlrGetData(conshdlr);
   assert(conshdlrdata != NULL);

   for( i = 0; i < nconss; ++i )
   {
      assert(conss != NULL);
      consdata = SCIPconsGetData(conss[i]);
      assert(consdata != NULL);

      if( !SCIPisInfinity(scip, -consdata->lhs) )
      {
         SCIP_CALL( generateCut(scip, conss[i], NULL, SCIP_BOUNDTYPE_LOWER, &row, conshdlrdata->cutmaxrange, conshdlrdata->checkcurvature) );
         if( row != NULL )
         {
            SCIP_CALL( SCIPaddCut(scip, NULL, row, FALSE /* forcecut */) );
#ifdef SCIP_DEBUG
            SCIP_CALL( SCIPprintRow(scip, row, NULL) );
#endif
            SCIP_CALL( SCIPreleaseRow (scip, &row) );
         }
      }
      if( !SCIPisInfinity(scip, consdata->rhs) )
      {
         SCIP_CALL( generateCut(scip, conss[i], NULL, SCIP_BOUNDTYPE_UPPER, &row, conshdlrdata->cutmaxrange, conshdlrdata->checkcurvature) );
         if( row != NULL )
         {
            SCIP_CALL( SCIPaddCut(scip, NULL, row, FALSE /* forcecut */) );
#ifdef SCIP_DEBUG
            SCIP_CALL( SCIPprintRow(scip, row, NULL) );
#endif
            SCIP_CALL( SCIPreleaseRow (scip, &row) );
         }
      }
   }

   return SCIP_OKAY;
}

/** separation method of constraint handler for LP solutions */
static
SCIP_DECL_CONSSEPALP(consSepalpQuadratic)
{  
   SCIP_CONSHDLRDATA* conshdlrdata;
   SCIP_CONS*         maxviolcon;

   assert(scip != NULL);
   assert(conshdlr != NULL);
   assert(conss != NULL || nconss == 0);
   assert(result != NULL);

   *result = SCIP_DIDNOTFIND;
   
   conshdlrdata = SCIPconshdlrGetData(conshdlr);
   assert(conshdlrdata != NULL);

   SCIP_CALL( computeViolations(scip, conss, nconss, NULL, conshdlrdata->doscaling, &maxviolcon) );
   if( maxviolcon == NULL )
      return SCIP_OKAY;

   SCIP_CALL( separatePoint(scip, conshdlr, conss, nconss, nusefulconss, NULL, conshdlrdata->mincutefficacysepa, FALSE, result, NULL) );
   if( *result == SCIP_SEPARATED )
      return SCIP_OKAY;

   return SCIP_OKAY;
}

/** separation method of constraint handler for arbitrary primal solutions */
static
SCIP_DECL_CONSSEPASOL(consSepasolQuadratic)
{
   SCIP_CONSHDLRDATA* conshdlrdata;
   SCIP_CONS*         maxviolcon;
   
   assert(scip != NULL);
   assert(conshdlr != NULL);
   assert(conss != NULL || nconss == 0);
   assert(sol != NULL);
   assert(result != NULL);
   
   conshdlrdata = SCIPconshdlrGetData(conshdlr);
   assert(conshdlrdata != NULL);

   *result = SCIP_DIDNOTFIND;

   SCIP_CALL( computeViolations(scip, conss, nconss, sol, conshdlrdata->doscaling, &maxviolcon) );
   if( maxviolcon == NULL )
      return SCIP_OKAY;

   SCIP_CALL( separatePoint(scip, conshdlr, conss, nconss, nusefulconss, sol, conshdlrdata->mincutefficacysepa, FALSE, result, NULL) );

   return SCIP_OKAY;
}

/** constraint enforcing method of constraint handler for LP solutions */
static
SCIP_DECL_CONSENFOLP(consEnfolpQuadratic)
{  /*lint --e{715}*/
   SCIP_CONSHDLRDATA* conshdlrdata;
   SCIP_CONSDATA*     consdata;
   SCIP_CONS*         maxviolcon;
   SCIP_Real          maxviol;
   SCIP_RESULT        propresult;
   SCIP_RESULT        separateresult;
   int                nchgbds;
   int                nnotify;
   SCIP_Real          sepaefficacy;
   SCIP_Real          minefficacy;

   assert(scip != NULL);
   assert(conshdlr != NULL);
   assert(conss != NULL || nconss == 0);
   
   conshdlrdata = SCIPconshdlrGetData(conshdlr);
   assert(conshdlrdata != NULL);
   
   SCIP_CALL( computeViolations(scip, conss, nconss, NULL, conshdlrdata->doscaling, &maxviolcon) );
   if( maxviolcon == NULL )
   {
      *result = SCIP_FEASIBLE;
      return SCIP_OKAY;
   }
   
   *result = SCIP_INFEASIBLE;

   consdata = SCIPconsGetData(maxviolcon);
   assert(consdata != NULL);
   maxviol = consdata->lhsviol + consdata->rhsviol;
   assert(!SCIPisFeasZero(scip, maxviol));

   SCIPdebugMessage("enfolp with max violation %g in cons <%s>\n", maxviol, SCIPconsGetName(maxviolcon));

   /* run domain propagation */
   nchgbds = 0;
   SCIP_CALL( propagateBounds(scip, conshdlr, conss, nconss, &propresult, &nchgbds) );
   if( propresult == SCIP_CUTOFF || propresult == SCIP_REDUCEDDOM )
   {
      *result = propresult;
      return SCIP_OKAY;
   }

   /* we would like a cut that is efficient enough that it is not redundant in the LP (>feastol)
    * however, if the maximal violation is very small, also the best cut efficacy cannot be large
    * thus, in the latter case, we are also happy if the efficacy is at least, say, 75% of the maximal violation
    * but in any case we need an efficacy that is at least feastol
    */
   minefficacy = MIN(0.75*maxviol, conshdlrdata->mincutefficacyenfo);
   minefficacy = MAX(minefficacy, SCIPfeastol(scip));
   SCIP_CALL( separatePoint(scip, conshdlr, conss, nconss, nusefulconss, NULL, minefficacy, TRUE, &separateresult, &sepaefficacy) );
   if( separateresult == SCIP_SEPARATED )
   {
      SCIPdebugMessage("separation succeeded (bestefficacy = %g, minefficacy = %g)\n", sepaefficacy, minefficacy);
      *result = SCIP_SEPARATED;
      return SCIP_OKAY;
   }

   /* we are not feasible, the whole node is not infeasible, and we cannot find a good cut
    * -> collect variables for branching
    */

   SCIPdebugMessage("separation failed (bestefficacy = %g < %g = minefficacy ); max viol: %g\n", sepaefficacy, minefficacy, maxviol);

   /* find branching candidates */
   SCIP_CALL( registerVariableInfeasibilities(scip, conshdlr, conss, nconss, &nnotify) );

   if( nnotify == 0 && !solinfeasible && minefficacy > SCIPfeastol(scip) )
   {
      /* fallback 1: we also have no branching candidates, so try to find a weak cut */
      SCIP_CALL( separatePoint(scip, conshdlr, conss, nconss, nusefulconss, NULL, SCIPfeastol(scip), TRUE, &separateresult, &sepaefficacy) );
      if( separateresult == SCIP_SEPARATED )
      {
         *result = SCIP_SEPARATED;
         return SCIP_OKAY;
      }
   }

   if( nnotify == 0 && !solinfeasible )
   { /* fallback 2: separation probably failed because of numerical difficulties with a convex constraint;
        if noone declared solution infeasible yet and we had not even found a weak cut, try to resolve by branching */
      SCIP_VAR* brvar = NULL;
      SCIP_CALL( registerLargeLPValueVariableForBranching(scip, conss, nconss, &brvar) );
      if( brvar == NULL )
      {
         /* fallback 3: all quadratic variables seem to be fixed -> replace by linear constraint */
         SCIP_CALL( replaceByLinearConstraints(scip, conss, nconss) );
         *result = SCIP_CONSADDED;
         return SCIP_OKAY;
      }
      else
      {
         SCIPdebugMessage("Could not find any usual branching variable candidate. Proposed variable <%s> with LP value %g for branching.\n", SCIPvarGetName(brvar), SCIPgetSolVal(scip, NULL, brvar));
      }
   }
   
   return SCIP_OKAY;
}


/** constraint enforcing method of constraint handler for pseudo solutions */
static
SCIP_DECL_CONSENFOPS(consEnfopsQuadratic)
{  /*lint --e{715}*/
   SCIP_CONSHDLRDATA* conshdlrdata;
   SCIP_CONS*         maxviolcon;
   SCIP_CONSDATA*     consdata;
   SCIP_RESULT        propresult;
   SCIP_VAR*          var;
   int                c;
   int                i;
   int                nchgbds;

   assert(scip != NULL);
   assert(conshdlr != NULL);
   assert(conss != NULL || nconss == 0);
   
   conshdlrdata = SCIPconshdlrGetData(conshdlr);
   assert(conshdlrdata != NULL);
   
   SCIP_CALL( computeViolations(scip, conss, nconss, NULL, conshdlrdata->doscaling, &maxviolcon) );
   if( maxviolcon == NULL )
   {
      *result = SCIP_FEASIBLE;
      return SCIP_OKAY;
   }
   
   *result = SCIP_INFEASIBLE;

   SCIPdebugMessage("enfops with max violation in cons <%s>\n", SCIPconsGetName(maxviolcon));

   /* run domain propagation */
   nchgbds = 0;
   SCIP_CALL( propagateBounds(scip, conshdlr, conss, nconss, &propresult, &nchgbds) );
   if( propresult == SCIP_CUTOFF || propresult == SCIP_REDUCEDDOM )
   {
      *result = propresult;
      return SCIP_OKAY;
   }

   /* we are not feasible and we cannot proof that the whole node is infeasible
    * -> collect all variables in violated constraints for branching
    */

   for( c = 0; c < nconss; ++c )
   {
      assert(conss != NULL);
      consdata = SCIPconsGetData(conss[c]);
      assert(consdata != NULL);

      if( !SCIPisFeasPositive(scip, consdata->lhsviol) && !SCIPisFeasPositive(scip, consdata->rhsviol) )
         continue;

      for( i = 0; i < consdata->nlinvars; ++i )
      {
         var = consdata->linvars[i];
         if( !SCIPisEQ(scip, SCIPvarGetLbLocal(var), SCIPvarGetUbLocal(var)) )
         {
            SCIP_CALL( SCIPaddExternBranchCand(scip, var, MAX(consdata->lhsviol, consdata->rhsviol), SCIP_INVALID) );
         }
      }

      for( i = 0; i < consdata->nquadvars; ++i )
      {
         var = consdata->quadvarterms[i].var;
         if( !SCIPisEQ(scip, SCIPvarGetLbLocal(var), SCIPvarGetUbLocal(var)) )
         {
            SCIP_CALL( SCIPaddExternBranchCand(scip, var, MAX(consdata->lhsviol, consdata->rhsviol), SCIP_INVALID) );
         }
      }
   }

   return SCIP_OKAY;
}

/** domain propagation method of constraint handler */
static
SCIP_DECL_CONSPROP(consPropQuadratic)
{
   int         nchgbds;

   assert(scip != NULL);
   assert(conshdlr != NULL);
   assert(conss != NULL || nconss == 0);
   assert(result != NULL);

   nchgbds = 0;
   SCIP_CALL( propagateBounds(scip, conshdlr, conss, nconss, result, &nchgbds) );

   return SCIP_OKAY;
}

/** presolving method of constraint handler */
static
SCIP_DECL_CONSPRESOL(consPresolQuadratic)
{  /*lint --e{715,788}*/
   SCIP_CONSHDLRDATA* conshdlrdata;
   SCIP_CONSDATA*     consdata;
   int                c, i;
   SCIP_Bool          havechange;
   int                mynupgdconss;
   SCIP_CONS**        myupgdconss;

   assert(scip     != NULL);
   assert(conshdlr != NULL);
   assert(conss    != NULL || nconss == 0);
   assert(result   != NULL);
   
   *result = SCIP_DIDNOTFIND;
   
   if( nrounds > 0 && nnewfixedvars == 0 && nnewupgdconss == 0 && nnewaddconss == 0 && nnewchgbds == 0 && nnewaggrvars == 0 && nnewchgvartypes == 0 )
      return SCIP_OKAY;

   conshdlrdata = SCIPconshdlrGetData(conshdlr);
   assert(conshdlrdata != NULL);

   for( c = 0; c < nconss; ++c )
   {
      assert(conss != NULL);
      consdata = SCIPconsGetData(conss[c]);
      assert(consdata != NULL);

      SCIPdebugMessage("process constraint <%s>\n", SCIPconsGetName(conss[c]));
      SCIPdebug( SCIPprintCons(scip, conss[c], NULL) );
      
      /** call upgrade methods before aggregated variables are replaced */
      SCIP_CALL( presolveUpgrade(scip, conshdlr, conss[c], &mynupgdconss, &myupgdconss) );
      assert(mynupgdconss >= 0);
      assert((myupgdconss != NULL) == (mynupgdconss > 0));
      if( mynupgdconss > 0 )
      {
         /* someone found an upgrade */

         /* add the upgraded constraints to the problem and forget them */
         for( i = 0; i < mynupgdconss; ++i )
         {
            SCIP_CALL( SCIPaddCons(scip, myupgdconss[i]) );      /*lint !e613*/
            SCIP_CALL( SCIPreleaseCons(scip, &myupgdconss[i]) ); /*lint !e613*/
         }

         /* count the first upgrade constraint as constraint upgrade and the remaining ones as added constraints */
         (*nupgdconss)++;
         *naddconss += mynupgdconss - 1;
         *result = SCIP_SUCCESS;

         /* delete upgraded constraint */
         SCIPdebugMessage("delete constraint <%s> after upgrade\n", SCIPconsGetName(conss[c]));
         SCIP_CALL( dropVarEvents(scip, conshdlrdata->eventhdlr, conss[c]) );
         SCIP_CALL( SCIPdelCons(scip, conss[c]) );

         SCIPfreeBufferArray(scip, &myupgdconss);
         continue;
      }
      
      havechange = FALSE;

      if( !consdata->isremovedfixings )
      {
         SCIP_CALL( removeFixedVariables(scip, conss[c]) );
         assert(consdata->isremovedfixings);
         havechange = TRUE;
      }
      
      if( !consdata->ispresolved || (!consdata->ispropagated && conshdlrdata->replacebinaryprodlength) )
      {
         int naddconss_old;

         naddconss_old = *naddconss;
         if( conshdlrdata->empathy4and == 2 )
         {
            /* user really likes AND, so give him */
            SCIP_CALL( presolveTryAddAND(scip, conss[c], naddconss) );
            assert(*naddconss >= naddconss_old);
         }

         if( *naddconss == naddconss_old )
         {
            /* user not so empathic about AND, or we don't have products of two binaries, so try this more general reformulation */
            SCIP_CALL( presolveTryAddLinearReform(scip, conss[c], naddconss, conshdlrdata->replacebinaryprodlength, conshdlrdata->empathy4and) );
            assert(*naddconss >= naddconss_old);
         }

         if( *naddconss > naddconss_old )
         {
            /* if something happened, report success and cleanup constraint */
            *result = SCIP_SUCCESS;
            havechange = TRUE;
            SCIP_CALL( mergeAndCleanBilinearTerms(scip, conss[c]) );
            SCIP_CALL( mergeAndCleanQuadVarTerms(scip, conss[c]) );
            SCIP_CALL( mergeAndCleanLinearVars(scip, conss[c]) );
         }
      }
      
      if( !consdata->ispresolved && conshdlrdata->disaggregate )
      {
         SCIP_Bool disaggrsuccess;
         
         SCIP_CALL( presolveDisaggregate(scip, conshdlr, conss[c], &disaggrsuccess, naddconss) );
      }
      
      if( consdata->nlinvars == 0 && consdata->nquadvars == 0 )
      { /* all variables fixed or removed, constraint function is 0.0 now */
         SCIP_CALL( dropVarEvents(scip, conshdlrdata->eventhdlr, conss[c]) ); /* well, there shouldn't be any variables left anyway */
         if( (!SCIPisInfinity(scip, -consdata->lhs) && SCIPisFeasPositive(scip, consdata->lhs)) ||
             (!SCIPisInfinity(scip,  consdata->rhs) && SCIPisFeasNegative(scip, consdata->rhs)) )
         { /* left hand side positive or right hand side negative */
            SCIPdebugMessage("constraint <%s> is constant and infeasible\n", SCIPconsGetName(conss[c]));
            SCIP_CALL( SCIPdelCons(scip, conss[c]) );
            ++*ndelconss;
            *result = SCIP_CUTOFF;
            break;
         }
         else
         { /* left and right hand side are consistent */
            SCIPdebugMessage("constraint <%s> is constant and feasible, deleting\n", SCIPconsGetName(conss[c]));
            SCIP_CALL( SCIPdelCons(scip, conss[c]) );
            ++*ndelconss;
            *result = SCIP_SUCCESS;
            continue;
         }
      }
      
      if( consdata->nquadvars == 0 )
      { /* all quadratic variables are fixed or removed, constraint is now linear */
         SCIP_CONS* lincons;
         
         SCIPdebugMessage("upgrade to linear constraint\n");
         SCIP_CALL( SCIPcreateConsLinear(scip, &lincons, SCIPconsGetName(conss[c]),
            consdata->nlinvars, consdata->linvars, consdata->lincoefs,
            consdata->lhs, consdata->rhs,
            SCIPconsIsInitial(conss[c]), SCIPconsIsSeparated(conss[c]), SCIPconsIsEnforced(conss[c]),
            SCIPconsIsChecked(conss[c]), SCIPconsIsPropagated(conss[c]),  SCIPconsIsLocal(conss[c]),
            SCIPconsIsModifiable(conss[c]), SCIPconsIsDynamic(conss[c]), SCIPconsIsRemovable(conss[c]),
            SCIPconsIsStickingAtNode(conss[c])) );

#ifdef SCIP_DEBUG
         SCIP_CALL( SCIPprintCons(scip, lincons, NULL) );
#endif
         SCIP_CALL( SCIPaddCons(scip, lincons) );
         SCIP_CALL( SCIPreleaseCons(scip, &lincons) );

         SCIP_CALL( dropVarEvents(scip, conshdlrdata->eventhdlr, conss[c]) );
         SCIP_CALL( SCIPdelCons(scip, conss[c]) );
         ++*nupgdconss;
         *result = SCIP_SUCCESS;
         continue;
      }

      if( !consdata->ispropagated )
      { /* try domain propagation if there were bound changes or constraint has changed (in which case, processVarEvents may have set ispropagated to false) */
         SCIP_RESULT propresult;
         SCIP_Bool redundant;
         SCIP_CALL( propagateBoundsCons(scip, conshdlr, conss[c], &propresult, nchgbds, &redundant) );
         switch( propresult )
         {
            case SCIP_REDUCEDDOM:
               *result = SCIP_SUCCESS;
               break;
            case SCIP_CUTOFF:
               SCIPdebugMessage("propagation on constraint <%s> says problem is infeasible in presolve\n", SCIPconsGetName(conss[c]));
               *result = SCIP_CUTOFF;
               return SCIP_OKAY;
            default:
               assert(propresult == SCIP_DIDNOTFIND || propresult == SCIP_DIDNOTRUN);
         }

         /* delete constraint if found redundant by bound tightening */
         if( redundant )
         {
            SCIP_CALL( dropVarEvents(scip, conshdlrdata->eventhdlr, conss[c]) );
            SCIP_CALL( SCIPdelCons(scip, conss[c]) );
            ++*ndelconss;
            *result = SCIP_SUCCESS;
            continue;
         }

         if( propresult != SCIP_REDUCEDDOM && !SCIPisInfinity(scip, conshdlrdata->defaultbound) )
         {
            if( nrounds > 0 )
            {
               int nboundchanges;

               nboundchanges = 0;

               SCIP_CALL( boundUnboundedVars(scip, conss[c], conshdlrdata->defaultbound, &nboundchanges) );
               if( nboundchanges != 0 )
               {
                  *nchgbds += nboundchanges;
                  *result   = SCIP_SUCCESS;
               }
            }
            else
            { /* wait for next round (or do in exitpre if no next round) */
               consdata->ispropagated = FALSE;
            }
         }
      }

      if( (nnewchgvartypes != 0 || havechange || !consdata->ispresolved)
          && (SCIPisEQ(scip, consdata->lhs, consdata->rhs) && SCIPisIntegral(scip, consdata->lhs)) )
      { /* check if we have a single linear continuous variable that we can make implicit integer 
        *  TODO allow for coefficient != +/-1 in front of linear var
        */
         int       ncontvar;
         SCIP_VAR* candidate;
         SCIP_Bool fail;

         fail = FALSE;
         candidate = NULL;
         ncontvar = 0;
         
         for( i = 0; !fail && i < consdata->nlinvars; ++i )
         {
            if( !SCIPisIntegral(scip, consdata->lincoefs[i]) )
            {
               fail = TRUE;
            }
            else if( SCIPvarGetType(consdata->linvars[i]) == SCIP_VARTYPE_CONTINUOUS )
            {
               if( ncontvar > 0 ) /* now at 2nd continuous variable */
                  fail = TRUE;
               else if( SCIPisEQ(scip, ABS(consdata->lincoefs[i]), 1.0) )
                  candidate = consdata->linvars[i];
               ++ncontvar;
            }
         }
         for( i = 0; !fail && i < consdata->nquadvars; ++i )
            fail = SCIPvarGetType(consdata->quadvarterms[i].var) == SCIP_VARTYPE_CONTINUOUS ||
                  !SCIPisIntegral(scip, consdata->quadvarterms[i].lincoef) ||
                  !SCIPisIntegral(scip, consdata->quadvarterms[i].sqrcoef);
         for( i = 0; !fail && i < consdata->nbilinterms; ++i )
            fail = !SCIPisIntegral(scip, consdata->bilinterms[i].coef);

         if( !fail && candidate != NULL )
         {
            SCIP_Bool infeasible;

            SCIPdebugMessage("make variable <%s> implicit integer due to constraint <%s>\n", SCIPvarGetName(candidate), SCIPconsGetName(conss[c]));

            SCIP_CALL( SCIPchgVarType(scip, candidate, SCIP_VARTYPE_IMPLINT, &infeasible) );
            if( infeasible )
            {
               SCIPdebugMessage("infeasible upgrade of variable <%s> to integral type, domain is empty\n", SCIPvarGetName(candidate));
               *result = SCIP_CUTOFF;

               return SCIP_OKAY;
            }

            ++(*nchgvartypes);
            *result = SCIP_SUCCESS;
         }
      }

      consdata->ispresolved = TRUE;
   }

   return SCIP_OKAY;
}


/** propagation conflict resolving method of constraint handler */
#if 0
static
SCIP_DECL_CONSRESPROP(consRespropQuadratic)
{  /*lint --e{715}*/
   SCIPerrorMessage("method of quadratic constraint handler not implemented yet\n");
   SCIPABORT(); /*lint --e{527}*/

   return SCIP_OKAY;
}
#else
#define consRespropQuadratic NULL
#endif

/** variable rounding lock method of constraint handler */
static
SCIP_DECL_CONSLOCK(consLockQuadratic)
{  /*lint --e{715}*/
   SCIP_CONSDATA* consdata;
   SCIP_Bool      haslb;
   SCIP_Bool      hasub;
   int            i;
   
   assert(scip != NULL);
   assert(cons != NULL);
  
   consdata = SCIPconsGetData(cons);
   assert(consdata != NULL);

   haslb = !SCIPisInfinity(scip, -consdata->lhs);
   hasub = !SCIPisInfinity(scip, consdata->rhs);

   for( i = 0; i < consdata->nlinvars; ++i )
   {
      if( consdata->lincoefs[i] > 0 )
      {
         if( haslb )
         {
            SCIP_CALL( SCIPaddVarLocks(scip, consdata->linvars[i], nlockspos, nlocksneg) );
         }
         if( hasub )
         {
            SCIP_CALL( SCIPaddVarLocks(scip, consdata->linvars[i], nlocksneg, nlockspos) );
         }
      }
      else
      {
         if( haslb )
         {
            SCIP_CALL( SCIPaddVarLocks(scip, consdata->linvars[i], nlocksneg, nlockspos) );
         }
         if( hasub )
         {
            SCIP_CALL( SCIPaddVarLocks(scip, consdata->linvars[i], nlockspos, nlocksneg) );
         }
      }
   }

   for( i = 0; i < consdata->nquadvars; ++i )
   { /* TODO try to be more clever */
      SCIP_CALL( SCIPaddVarLocks(scip, consdata->quadvarterms[i].var, nlockspos+nlocksneg, nlockspos+nlocksneg) );
   }

   return SCIP_OKAY;
}


/** constraint activation notification method of constraint handler */
#if 0
static
SCIP_DECL_CONSACTIVE(consActiveQuadratic)
{  /*lint --e{715}*/
   SCIPerrorMessage("method of quadratic constraint handler not implemented yet\n");
   SCIPABORT(); /*lint --e{527}*/

   return SCIP_OKAY;
}
#else
#define consActiveQuadratic NULL
#endif


/** constraint deactivation notification method of constraint handler */
#if 0
static
SCIP_DECL_CONSDEACTIVE(consDeactiveQuadratic)
{  /*lint --e{715}*/
   SCIPerrorMessage("method of quadratic constraint handler not implemented yet\n");
   SCIPABORT(); /*lint --e{527}*/

   return SCIP_OKAY;
}
#else
#define consDeactiveQuadratic NULL
#endif


/** constraint enabling notification method of constraint handler */
#if 0
static
SCIP_DECL_CONSENABLE(consEnableQuadratic)
{  /*lint --e{715}*/
   SCIPerrorMessage("method of quadratic constraint handler not implemented yet\n");
   SCIPABORT(); /*lint --e{527}*/

   return SCIP_OKAY;
}
#else
#define consEnableQuadratic NULL
#endif


/** constraint disabling notification method of constraint handler */
#if 0
static
SCIP_DECL_CONSDISABLE(consDisableQuadratic)
{  /*lint --e{715}*/
   SCIPerrorMessage("method of quadratic constraint handler not implemented yet\n");
   SCIPABORT(); /*lint --e{527}*/

   return SCIP_OKAY;
}
#else
#define consDisableQuadratic NULL
#endif


/** constraint display method of constraint handler */
static
SCIP_DECL_CONSPRINT(consPrintQuadratic)
{  /*lint --e{715}*/
   SCIP_CONSDATA* consdata;
   int            j;
   
   assert(scip != NULL);
   assert(cons != NULL);
   
   consdata = SCIPconsGetData(cons);
   assert(consdata != NULL);

   /* print left hand side for ranged rows */
   if( !SCIPisInfinity(scip, -consdata->lhs)
      && !SCIPisInfinity(scip, consdata->rhs)
      && !SCIPisEQ(scip, consdata->lhs, consdata->rhs) )
      SCIPinfoMessage(scip, file, "%.15g <= ", consdata->lhs);

   /* print coefficients and variables */
   if( consdata->nlinvars == 0 && consdata->nquadvars == 0 )
      SCIPinfoMessage(scip, file, "0 ");
   else
   {
      for( j = 0; j < consdata->nlinvars; ++j )
      {
         SCIPinfoMessage(scip, file, "%+.15g<%s>[%c] ", consdata->lincoefs[j], SCIPvarGetName(consdata->linvars[j]),
            SCIPvarGetType(consdata->linvars[j]) == SCIP_VARTYPE_BINARY ? 'B' :
            SCIPvarGetType(consdata->linvars[j]) == SCIP_VARTYPE_INTEGER ? 'I' :
            SCIPvarGetType(consdata->linvars[j]) == SCIP_VARTYPE_IMPLINT ? 'I' : 'C');
      }

      for( j = 0; j < consdata->nquadvars; ++j )
      {
         if( consdata->quadvarterms[j].lincoef != 0.0 )
         {
            SCIPinfoMessage(scip, file, "%+.15g<%s>[%c]", consdata->quadvarterms[j].lincoef, SCIPvarGetName(consdata->quadvarterms[j].var),
               SCIPvarGetType(consdata->quadvarterms[j].var) == SCIP_VARTYPE_BINARY ? 'B' :
               SCIPvarGetType(consdata->quadvarterms[j].var) == SCIP_VARTYPE_INTEGER ? 'I' :
               SCIPvarGetType(consdata->quadvarterms[j].var) == SCIP_VARTYPE_IMPLINT ? 'I' : 'C');
         }
         if( consdata->quadvarterms[j].sqrcoef != 0.0 )
         {
            SCIPinfoMessage(scip, file, "%+.15g<%s>[%c]^2", consdata->quadvarterms[j].sqrcoef, SCIPvarGetName(consdata->quadvarterms[j].var),
               SCIPvarGetType(consdata->quadvarterms[j].var) == SCIP_VARTYPE_BINARY ? 'B' :
               SCIPvarGetType(consdata->quadvarterms[j].var) == SCIP_VARTYPE_INTEGER ? 'I' :
               SCIPvarGetType(consdata->quadvarterms[j].var) == SCIP_VARTYPE_IMPLINT ? 'I' : 'C',
               SCIPvarGetName(consdata->quadvarterms[j].var),
               SCIPvarGetType(consdata->quadvarterms[j].var) == SCIP_VARTYPE_BINARY ? 'B' :
               SCIPvarGetType(consdata->quadvarterms[j].var) == SCIP_VARTYPE_INTEGER ? 'I' :
               SCIPvarGetType(consdata->quadvarterms[j].var) == SCIP_VARTYPE_IMPLINT ? 'I' : 'C');
         }
      }

      for( j = 0; j < consdata->nbilinterms; ++j )
      {
         SCIPinfoMessage(scip, file, "%+.15g<%s>[%c]<%s>[%c]", consdata->bilinterms[j].coef,
            SCIPvarGetName(consdata->bilinterms[j].var1),
            SCIPvarGetType(consdata->bilinterms[j].var1) == SCIP_VARTYPE_BINARY ? 'B' :
            SCIPvarGetType(consdata->bilinterms[j].var1) == SCIP_VARTYPE_INTEGER ? 'I' :
            SCIPvarGetType(consdata->bilinterms[j].var1) == SCIP_VARTYPE_IMPLINT ? 'I' : 'C',
            SCIPvarGetName(consdata->bilinterms[j].var2),
            SCIPvarGetType(consdata->bilinterms[j].var2) == SCIP_VARTYPE_BINARY ? 'B' :
            SCIPvarGetType(consdata->bilinterms[j].var2) == SCIP_VARTYPE_INTEGER ? 'I' :
            SCIPvarGetType(consdata->bilinterms[j].var2) == SCIP_VARTYPE_IMPLINT ? 'I' : 'C');
      }
   }

   /* print right hand side */
   if( SCIPisEQ(scip, consdata->lhs, consdata->rhs) )
   {
      SCIPinfoMessage(scip, file, " == %.15g", consdata->rhs);
   }
   else if( !SCIPisInfinity(scip, consdata->rhs) )
   {
      SCIPinfoMessage(scip, file, " <= %.15g", consdata->rhs);
   }
   else if( !SCIPisInfinity(scip, -consdata->lhs) )
   {
      SCIPinfoMessage(scip, file, " >= %.15g", consdata->lhs);
   }
   else
   {
      SCIPinfoMessage(scip, file, " [free]");
   }

   return SCIP_OKAY;
}

/** feasibility check method of constraint handler for integral solutions */
static
SCIP_DECL_CONSCHECK(consCheckQuadratic)
{  /*lint --e{715}*/
   SCIP_CONSHDLRDATA* conshdlrdata;
   SCIP_CONSDATA*     consdata;
   SCIP_Real          maxviol;
   int                c;
   SCIP_Bool          maypropfeasible; /* whether we may be able to propose a feasible solution */

   assert(scip != NULL);
   assert(conss != NULL || nconss == 0);
   assert(sol != NULL);
   assert(result != NULL);
   
   conshdlrdata = SCIPconshdlrGetData(conshdlr);
   assert(conshdlrdata != NULL);

   *result = SCIP_FEASIBLE;

   maxviol = 0.0;
   maypropfeasible = conshdlrdata->linfeasshift && (conshdlrdata->trysolheur != NULL);
   for( c = 0; c < nconss; ++c )
   {
      assert(conss != NULL);
      SCIP_CALL( computeViolation(scip, conss[c], sol, conshdlrdata->doscaling) );
      
      consdata = SCIPconsGetData(conss[c]);
      assert(consdata != NULL);
      
      if( SCIPisFeasPositive(scip, consdata->lhsviol) || SCIPisFeasPositive(scip, consdata->rhsviol) )
      {
         *result = SCIP_INFEASIBLE;
         if( printreason )
         {
            SCIP_CALL( SCIPprintCons(scip, conss[c], NULL) );
            if( SCIPisFeasPositive(scip, consdata->lhsviol) )
            {
               SCIPinfoMessage(scip, NULL, "violation: left hand side is violated by %.15g (scaled: %.15g)\n", consdata->lhs - consdata->activity, consdata->lhsviol);
            }
            if( SCIPisFeasPositive(scip, consdata->rhsviol) )
            {
               SCIPinfoMessage(scip, NULL, "violation: right hand side is violated by %.15g (scaled: %.15g)\n", consdata->activity - consdata->rhs, consdata->rhsviol);
            }
         }
         if( (conshdlrdata->subnlpheur == NULL || sol == NULL) && !maypropfeasible )
            return SCIP_OKAY;
         if( consdata->lhsviol > maxviol || consdata->rhsviol > maxviol )
            maxviol = consdata->lhsviol + consdata->rhsviol;
         if( maypropfeasible )
         {
            if( SCIPisFeasPositive(scip, consdata->lhsviol) )
            {
               /* check if there is a variable which may help to get the left hand side satisfied
                * if there is no such var, then we cannot get feasible */
               if( !(consdata->linvar_mayincrease >= 0 && consdata->lincoefs[consdata->linvar_mayincrease] > 0.0) &&
                   !(consdata->linvar_maydecrease >= 0 && consdata->lincoefs[consdata->linvar_maydecrease] < 0.0) )
                  maypropfeasible = FALSE;
            }
            else
            {
               assert(SCIPisFeasPositive(scip, consdata->rhsviol));
               /* check if there is a variable which may help to get the right hand side satisfied
                * if there is no such var, then we cannot get feasible */
               if( !(consdata->linvar_mayincrease >= 0 && consdata->lincoefs[consdata->linvar_mayincrease] < 0.0) &&
                   !(consdata->linvar_maydecrease >= 0 && consdata->lincoefs[consdata->linvar_maydecrease] > 0.0) )
                  maypropfeasible = FALSE;
            }
         }
      }
   }
   
   if( *result == SCIP_INFEASIBLE && maypropfeasible )
   {
      SCIP_Bool success;

      SCIP_CALL( proposeFeasibleSolution(scip, conshdlr, conss, nconss, sol, &success) );

      /* do not pass solution to NLP heuristic if we made it feasible this way */
      if( success )
         return SCIP_OKAY;
   }

   if( *result == SCIP_INFEASIBLE && conshdlrdata->subnlpheur != NULL && sol != NULL )
   {
      SCIP_CALL( SCIPupdateStartpointHeurSubNlp(scip, conshdlrdata->subnlpheur, sol, maxviol) );
   }

   return SCIP_OKAY;
}

/** constraint copying method of constraint handler */
static
SCIP_DECL_CONSCOPY(consCopyQuadratic)
{
   SCIP_CONSDATA*    consdata;
   SCIP_CONSDATA*    targetconsdata;
   SCIP_VAR**        linvars;
   SCIP_QUADVARTERM* quadvarterms;
   SCIP_BILINTERM*   bilinterms;
   int               i;
   int               j;
   int               k;
   
   assert(scip != NULL);
   assert(cons != NULL);
   assert(sourcescip != NULL);
   assert(sourceconshdlr != NULL);
   assert(sourcecons != NULL);
   assert(varmap != NULL);
   assert(valid != NULL);
   
   consdata = SCIPconsGetData(sourcecons);
   assert(consdata != NULL);
   
   linvars = NULL;
   quadvarterms = NULL;
   bilinterms = NULL;

   *valid = TRUE;
   
   if( consdata->nlinvars != 0 )
   {
      SCIP_CALL( SCIPallocBufferArray(sourcescip, &linvars, consdata->nlinvars) );
      for( i = 0; i < consdata->nlinvars; ++i )
      {
         SCIP_CALL( SCIPgetVarCopy(sourcescip, scip, consdata->linvars[i], &linvars[i], varmap, consmap, global, valid) );
         assert(!(*valid) || linvars[i] != NULL);

         /* we do not copy, if a variable is missing */
         if( !(*valid) )
            goto TERMINATE;  
      }
   }
   
   if( consdata->nbilinterms != 0 )
   {
      SCIP_CALL( SCIPallocBufferArray(sourcescip, &bilinterms, consdata->nbilinterms) );
   }

   if( consdata->nquadvars != 0 )
   {
      SCIP_CALL( SCIPallocBufferArray(sourcescip, &quadvarterms, consdata->nquadvars) );
      for( i = 0; i < consdata->nquadvars; ++i )
      {
         SCIP_CALL( SCIPgetVarCopy(sourcescip, scip, consdata->quadvarterms[i].var, &quadvarterms[i].var, varmap, consmap, global, valid) );
         assert(!(*valid) || quadvarterms[i].var != NULL);
         
         /* we do not copy, if a variable is missing */
         if( !(*valid) )
            goto TERMINATE;
         
         quadvarterms[i].lincoef   = consdata->quadvarterms[i].lincoef;
         quadvarterms[i].sqrcoef   = consdata->quadvarterms[i].sqrcoef;
         quadvarterms[i].eventdata = NULL;
         quadvarterms[i].nadjbilin = consdata->quadvarterms[i].nadjbilin;
         quadvarterms[i].adjbilin  = consdata->quadvarterms[i].adjbilin;
         
         assert(consdata->nbilinterms != 0 || consdata->quadvarterms[i].nadjbilin == 0);
         
         for( j = 0; j < consdata->quadvarterms[i].nadjbilin; ++j )
         {
            assert(bilinterms != NULL);
            
            k = consdata->quadvarterms[i].adjbilin[j];
            assert(consdata->bilinterms[k].var1 != NULL);
            assert(consdata->bilinterms[k].var2 != NULL);
            if( consdata->bilinterms[k].var1 == consdata->quadvarterms[i].var )
            {
               if( consdata->bilinterms[k].var2 == consdata->quadvarterms[i].var )
                  printf("%g %s %s in <%s>\n", consdata->bilinterms[k].coef, SCIPvarGetName(consdata->bilinterms[k].var1), SCIPvarGetName(consdata->bilinterms[k].var2), SCIPconsGetName(sourcecons));
               assert(consdata->bilinterms[k].var2 != consdata->quadvarterms[i].var);
               bilinterms[k].var1 = quadvarterms[i].var;
            }
            else
            {
               assert(consdata->bilinterms[k].var2 == consdata->quadvarterms[i].var);
               bilinterms[k].var2 = quadvarterms[i].var;
            }
            bilinterms[k].coef = consdata->bilinterms[k].coef; 
         }
      }
   }
      
   assert(stickingatnode == FALSE);
   SCIP_CALL( SCIPcreateConsQuadratic2(scip, cons, name ? name : SCIPconsGetName(sourcecons),
      consdata->nlinvars, linvars, consdata->lincoefs,
      consdata->nquadvars, quadvarterms,
      consdata->nbilinterms, bilinterms,
      consdata->lhs, consdata->rhs,
      initial, separate, enforce, check, propagate, local, modifiable, dynamic, removable) );

   /* copy information on curvature */
   targetconsdata = SCIPconsGetData(*cons);
   targetconsdata->isconvex      = consdata->isconvex;
   targetconsdata->isconcave     = consdata->isconcave;
   targetconsdata->iscurvchecked = consdata->iscurvchecked;

 TERMINATE:
   SCIPfreeBufferArrayNull(sourcescip, &quadvarterms);
   SCIPfreeBufferArrayNull(sourcescip, &bilinterms);
   SCIPfreeBufferArrayNull(sourcescip, &linvars);
   
   return SCIP_OKAY;
}

/** constraint parsing method of constraint handler */
#if 1
static
SCIP_DECL_CONSPARSE(consParseQuadratic)
{  /*lint --e{715}*/
   SCIP_TOKENIZER tokenizer;
   SCIP_VAR** linvars;
   SCIP_Real* lincoefs;
   SCIP_VAR** quadvars1;
   SCIP_VAR** quadvars2;
   SCIP_Real* quadcoefs;
   SCIP_VAR*  var1;
   SCIP_VAR*  var2;

   SCIP_Real lhs;
   SCIP_Real rhs;

   SCIP_Bool havesign;
   SCIP_Bool havevalue;
   CIPSENSE sense;
   SCIP_Real coef;
   int coefsign;
   int lincoefssize;
   int nlincoefs;
   int bilincoefssize;
   int nbilincoefs;
   
   /* initialize tokenizer */
   tokenizer.strbuf = str;
   SCIP_CALL( SCIPallocBufferArray(scip, &tokenizer.token, SCIP_MAXSTRLEN) );
   tokenizer.token[0] = '\0';
   SCIP_CALL( SCIPallocBufferArray(scip, &tokenizer.tokenbuf, SCIP_MAXSTRLEN) );
   tokenizer.tokenbuf[0] = '\0';
   tokenizer.strpos = 0;
   
   /* initialize buffers for storing the coefficients */
   lincoefssize = 100;
   SCIP_CALL( SCIPallocBufferArray(scip, &linvars,  lincoefssize) );
   SCIP_CALL( SCIPallocBufferArray(scip, &lincoefs, lincoefssize) );
   bilincoefssize = 100;
   SCIP_CALL( SCIPallocBufferArray(scip, &quadvars1, bilincoefssize) );
   SCIP_CALL( SCIPallocBufferArray(scip, &quadvars2, bilincoefssize) );
   SCIP_CALL( SCIPallocBufferArray(scip, &quadcoefs, bilincoefssize) );
   
   /* set right hand and left side to their default values */
   lhs = -SCIPinfinity(scip);
   rhs =  SCIPinfinity(scip);

   (*success) = TRUE;

   /* read the coefficients */
   coefsign = +1;
   coef = 1.0;
   havesign  = FALSE;
   havevalue = FALSE;
   sense = CIP_SENSE_NOTHING;
   nlincoefs = 0;
   nbilincoefs = 0;
   var1 = NULL;
   var2 = NULL;

   SCIPdebugMessage("start parsing quadratic constraint expression\n");

   while( getNextToken(&tokenizer) && (*success) )
   {
      /* if the variable type is given ignore it */
      if( strncmp(tokenizer.token, "[B]", 3) == 0 && strlen(tokenizer.token) == 3 )
      {
         assert(var1 != NULL);
         assert(SCIPvarGetType(var2 == NULL ? var1 : var2) == SCIP_VARTYPE_BINARY);
         SCIPdebugMessage("ignoring token <%s>\n", tokenizer.token);
         continue;
      }
      if( strncmp(tokenizer.token, "[I]", 3) == 0 && strlen(tokenizer.token) == 3 )
      {
         assert(var1 != NULL);
         /* in can be the case that the variable type is already changed to binary (for example the variable is fixed to
          * zero in the file); hence, there could be a mismatch between the information of the file and current variable type; */
         assert(SCIPvarGetType(var2 == NULL ? var1 : var2) != SCIP_VARTYPE_CONTINUOUS);
         SCIPdebugMessage("ignoring token <%s>\n", tokenizer.token);
         continue;
      }
      if( strncmp(tokenizer.token, "[C]", 3) == 0 && strlen(tokenizer.token) == 3 )
      {
         assert(var1 != NULL);
         assert(SCIPvarGetType(var2 == NULL ? var1 : var2) == SCIP_VARTYPE_CONTINUOUS || 
            (SCIPisRelEQ(scip, SCIPvarGetLbGlobal(var1), SCIPvarGetUbGlobal(var1)) && SCIPisIntegral(scip, SCIPvarGetLbGlobal(var1))) );
         SCIPdebugMessage("ignoring token <%s>\n", tokenizer.token);
         continue;
      }
      
      if( *tokenizer.token == '>' && strlen(tokenizer.token) == 1 )
      {
         SCIPdebugMessage("ignoring token <%s>\n", tokenizer.token);
         continue;
      }
      if( *tokenizer.token == '<' && strlen(tokenizer.token) == 1 )
      {
         SCIPdebugMessage("ignoring token <%s>\n", tokenizer.token);
         continue;
      }
      
      /* check if we read a ^2 */
      if( strncmp(tokenizer.token, "^2", 2) == 0 && strlen(tokenizer.token) == 2 )
      {
         if( var1 == NULL )
         {
            SCIPverbMessage(scip, SCIP_VERBLEVEL_MINIMAL, NULL, "Syntax error: expected variable before ^2 token\n");
            (*success) = FALSE;
            continue;
         }
         if( var2 != NULL )
         {
            SCIPverbMessage(scip, SCIP_VERBLEVEL_MINIMAL, NULL, "Syntax error: unexpected ^2 token after <%s>*<%s>\n", SCIPvarGetName(var1), SCIPvarGetName(var2));
            (*success) = FALSE;
            continue;
         }
         SCIPdebugMessage("read ^2 symbol after variable <%s>\n", SCIPvarGetName(var1));
         var2 = var1;
         continue;
      }
      
      /* check if a term was finished */
      if( var1 != NULL )
      {
         CIPSENSE  nextsense = CIP_SENSE_NOTHING;
         int       nextcoefsign = +1;
         SCIP_Real nextcoef = 1.0;

         if( isSense(&tokenizer, &nextsense) || isSign(&tokenizer, &nextcoefsign) || isValue(scip, &tokenizer, &nextcoef) )
         {
            if( var2 == NULL )
            {
               /* insert the coefficient */
               SCIPdebugMessage("read coefficient: %+g<%s>\n", coefsign * coef, SCIPvarGetName(var1));
               if( !SCIPisZero(scip, coef) )
               {
                  /* resize the vars and coefs array if needed */
                  if( nlincoefs >= lincoefssize )
                  {
                     lincoefssize *= 2;
                     lincoefssize = MAX(lincoefssize, nlincoefs+1);
                     SCIP_CALL( SCIPreallocBufferArray(scip, &linvars,  lincoefssize) );
                     SCIP_CALL( SCIPreallocBufferArray(scip, &lincoefs, lincoefssize) );
                  }
                  assert(nlincoefs < lincoefssize);

                  /* add coefficient */
                  linvars [nlincoefs] = var1;
                  lincoefs[nlincoefs] = coefsign * coef;
                  nlincoefs++;
               }
            }
            else
            {
               /* insert the coefficient */
               SCIPdebugMessage("read coefficient: %+g<%s><%s>\n", coefsign * coef, SCIPvarGetName(var1), SCIPvarGetName(var2));
               if( !SCIPisZero(scip, coef) )
               {
                  /* resize the vars and coefs array if needed */
                  if( nbilincoefs >= bilincoefssize )
                  {
                     bilincoefssize *= 2;
                     bilincoefssize = MAX(bilincoefssize, nbilincoefs+1);
                     SCIP_CALL( SCIPreallocBufferArray(scip, &quadvars1, bilincoefssize) );
                     SCIP_CALL( SCIPreallocBufferArray(scip, &quadvars2, bilincoefssize) );
                     SCIP_CALL( SCIPreallocBufferArray(scip, &quadcoefs, bilincoefssize) );
                  }
                  assert(nbilincoefs < bilincoefssize);

                  /* add coefficient */
                  quadvars1[nbilincoefs] = var1;
                  quadvars2[nbilincoefs] = var2;
                  quadcoefs[nbilincoefs] = coefsign * coef;
                  nbilincoefs++;
               }
            }
            
            /* reset the flags and coefficient value for the next term */
            coefsign = +1;
            coef = 1.0;
            havesign = FALSE;
            havevalue = FALSE;
            var1 = NULL;
            var2 = NULL;
         }
      }
      
      /* check if we reached an equation sense */
      if( isSense(&tokenizer, &sense) )
      {
         if( nlincoefs == 0 && nbilincoefs == 0 && havevalue )
         {
            /* the constraint has no variables */
            switch(sense)
            {
            case CIP_SENSE_LE:
               lhs = coefsign * coef;
               break;
            case CIP_SENSE_GE:
               rhs = coefsign * coef;
               break;
            case CIP_SENSE_EQ:
               lhs = coefsign * coef;
               rhs = coefsign * coef;
               break;
            default:
               SCIPverbMessage(scip, SCIP_VERBLEVEL_MINIMAL, NULL, "Syntax error: expected unknown sense <%d>)\n", sense);
               (*success) = FALSE;
            } /*lint !e788*/
            sense = CIP_SENSE_NOTHING;
            havevalue = FALSE;
         }

         continue;
      }
      
      /* check if we read a sign */
      if( isSign(&tokenizer, &coefsign) )
      {
         SCIPdebugMessage("read coefficient sign: %+d\n", coefsign);
         havesign = TRUE;
         continue;
      }

      /* all but the first coefficient need a sign */
      if( (nlincoefs > 0 || nbilincoefs > 0) && !havesign && sense == CIP_SENSE_NOTHING )
      {
         SCIPverbMessage(scip, SCIP_VERBLEVEL_MINIMAL, NULL, "Syntax error: expected sign ('+' or '-') or sense ('<' or '>')\n");
         (*success) = FALSE;
         break;
      }

      /* check if we read a value */
      if( isValue(scip, &tokenizer, &coef) )
      {
         SCIPdebugMessage("read coefficient value: <%g> with sign %+d\n", coef, coefsign);
         if( havevalue )
         {
            SCIPverbMessage(scip, SCIP_VERBLEVEL_MINIMAL, NULL, "Syntax error: two consecutive values");
            (*success) = FALSE;
            break;
         }
         havevalue = TRUE;
         
         if( sense == CIP_SENSE_EQ ) 
         {
            lhs = coefsign * coef;
            rhs = coefsign * coef;
         }
         else if( sense == CIP_SENSE_LE ) 
            rhs = coefsign * coef;
         if( sense == CIP_SENSE_GE ) 
            lhs = coefsign * coef;
         
         continue;
      }
      
      /* the token is a variable name */
      
      /* if there is no first variable yet, get the corresponding variable */
      if( var1 == NULL )
      {
         var1 = SCIPfindVar(scip, tokenizer.token);

         if( var1 == NULL )
         {
            SCIPverbMessage(scip, SCIP_VERBLEVEL_MINIMAL, NULL, "unknown variable <%s>", tokenizer.token);
            (*success) = FALSE;
         }
         
         continue;
      }
      
      /* if there is already a first and a second variable, then this is an error */
      if( var2 != NULL )
      {
         SCIPverbMessage(scip, SCIP_VERBLEVEL_MINIMAL, NULL, "third variable variable <%s> in product", tokenizer.token);
         (*success) = FALSE;
         continue;
      }
      
      /* get the corresponding variable */
      var2 = SCIPfindVar(scip, tokenizer.token);

      if( var2 == NULL )
      {
         SCIPverbMessage(scip, SCIP_VERBLEVEL_MINIMAL, NULL, "unknown variable <%s>", tokenizer.token);
         (*success) = FALSE;
      }
   }
   
   if( var1 != NULL )
   { /* TODO may this happen for free constraints? */
      SCIPverbMessage(scip, SCIP_VERBLEVEL_MINIMAL, NULL, "unexpected end of string after term with variable <%s>", SCIPvarGetName(var1));
      (*success) = FALSE;
   }

   if( *success )
   {
      assert(stickingatnode == FALSE);
      SCIP_CALL( SCIPcreateConsQuadratic(scip, cons, name, nlincoefs, linvars, lincoefs, 
         nbilincoefs, quadvars1, quadvars2, quadcoefs, lhs, rhs, 
         initial, separate, enforce, check, propagate, local, modifiable, dynamic, removable) );
   }

   SCIPfreeBufferArray(scip, &lincoefs);
   SCIPfreeBufferArray(scip, &linvars);
   SCIPfreeBufferArray(scip, &quadvars1);
   SCIPfreeBufferArray(scip, &quadvars2);
   SCIPfreeBufferArray(scip, &quadcoefs);
   tokenizer.token[0] = '\0';
   SCIPfreeBufferArray(scip, &tokenizer.tokenbuf);
   SCIPfreeBufferArray(scip, &tokenizer.token);
   
   return SCIP_OKAY;
}
#else
#define consParseQuadratic NULL
#endif


/*
 * constraint specific interface methods
 */

/** creates the handler for quadratic constraints and includes it in SCIP */
SCIP_RETCODE SCIPincludeConshdlrQuadratic(
   SCIP*                 scip                /**< SCIP data structure */
   )
{
   SCIP_CONSHDLRDATA* conshdlrdata;

   /* create quadratic constraint handler data */
   SCIP_CALL( SCIPallocMemory(scip, &conshdlrdata) );
   BMSclearMemory(conshdlrdata);

   /* include constraint handler */
   SCIP_CALL( SCIPincludeConshdlr(scip, CONSHDLR_NAME, CONSHDLR_DESC,
         CONSHDLR_SEPAPRIORITY, CONSHDLR_ENFOPRIORITY, CONSHDLR_CHECKPRIORITY,
         CONSHDLR_SEPAFREQ, CONSHDLR_PROPFREQ, CONSHDLR_EAGERFREQ, CONSHDLR_MAXPREROUNDS,
         CONSHDLR_DELAYSEPA, CONSHDLR_DELAYPROP, CONSHDLR_DELAYPRESOL, CONSHDLR_NEEDSCONS,
         conshdlrCopyQuadratic,
         consFreeQuadratic, consInitQuadratic, consExitQuadratic,
         consInitpreQuadratic, consExitpreQuadratic, consInitsolQuadratic, consExitsolQuadratic,
         consDeleteQuadratic, consTransQuadratic, consInitlpQuadratic,
         consSepalpQuadratic, consSepasolQuadratic, consEnfolpQuadratic, consEnfopsQuadratic, consCheckQuadratic,
         consPropQuadratic, consPresolQuadratic, consRespropQuadratic, consLockQuadratic,
         consActiveQuadratic, consDeactiveQuadratic,
         consEnableQuadratic, consDisableQuadratic,
         consPrintQuadratic, consCopyQuadratic, consParseQuadratic,
         conshdlrdata) );

   /* add quadratic constraint handler parameters */
   SCIP_CALL( SCIPaddIntParam(scip, "constraints/"CONSHDLR_NAME"/replacebinaryprod",
         "max. length of linear term which when multiplied with a binary variables is replaced by an auxiliary variable and a linear reformulation (0 to turn off)",
         &conshdlrdata->replacebinaryprodlength, FALSE, INT_MAX, 0, INT_MAX, NULL, NULL) );

   SCIP_CALL( SCIPaddIntParam(scip, "constraints/"CONSHDLR_NAME"/empathy4and",
         "empathy level for using the AND constraint handler: 0 always avoid using AND; 1 use AND sometimes; 2 use AND as often as possible",
         &conshdlrdata->empathy4and, FALSE, 0, 0, 2, NULL, NULL) );
   
   SCIP_CALL( SCIPaddRealParam(scip, "constraints/"CONSHDLR_NAME"/minefficacysepa",
         "minimal efficacy for a cut to be added to the LP during separation; overwrites separating/efficacy",
         &conshdlrdata->mincutefficacysepa, FALSE, 0.0001, 0.0, SCIPinfinity(scip), NULL, NULL) );

   SCIP_CALL( SCIPaddRealParam(scip, "constraints/"CONSHDLR_NAME"/minefficacyenfo",
         "minimal target efficacy of a cut in order to add it to relaxation during enforcement (may be ignored)",
         &conshdlrdata->mincutefficacyenfo, FALSE, 2.0*SCIPfeastol(scip), 0.0, SCIPinfinity(scip), NULL, NULL) );

   SCIP_CALL( SCIPaddBoolParam(scip, "constraints/"CONSHDLR_NAME"/scaling", 
         "whether a quadratic constraint should be scaled w.r.t. the current gradient norm when checking for feasibility",
         &conshdlrdata->doscaling, FALSE, TRUE, NULL, NULL) );

   SCIP_CALL( SCIPaddRealParam(scip, "constraints/"CONSHDLR_NAME"/defaultbound",
         "a default bound to impose on unbounded variables in quadratic terms (-defaultbound is used for missing lower bounds)",
         &conshdlrdata->defaultbound, TRUE, SCIPinfinity(scip), 0.0, SCIPinfinity(scip), NULL, NULL) );

   SCIP_CALL( SCIPaddRealParam(scip, "constraints/"CONSHDLR_NAME"/cutmaxrange",
         "maximal range of a cut (maximal coefficient divided by minimal coefficient) in order to be added to LP relaxation",
         &conshdlrdata->cutmaxrange, FALSE, 1e+10, 0.0, SCIPinfinity(scip), NULL, NULL) );

   SCIP_CALL( SCIPaddBoolParam(scip, "constraints/"CONSHDLR_NAME"/linearizenlpsol",
         "whether convex quadratic constraints should be linearized in a solution found by the NLP or RENS heuristic",
         &conshdlrdata->linearizenlpsol, FALSE, TRUE, NULL, NULL) );

   SCIP_CALL( SCIPaddBoolParam(scip, "constraints/"CONSHDLR_NAME"/checkcurvature",
         "whether multivariate quadratic functions should be checked for convexity/concavity",
         &conshdlrdata->checkcurvature, FALSE, TRUE, NULL, NULL) );
   
   SCIP_CALL( SCIPaddBoolParam(scip, "constraints/"CONSHDLR_NAME"/linfeasshift",
         "whether to try to make solutions in check function feasible by shifting a linear variable (esp. useful if constraint was actually objective function)",
         &conshdlrdata->linfeasshift, FALSE, TRUE, NULL, NULL) );

   SCIP_CALL( SCIPaddBoolParam(scip, "constraints/"CONSHDLR_NAME"/disaggregate",
         "whether to disaggregate quadratic parts that decompose into a sum of non-overlapping quadratic terms",
         &conshdlrdata->disaggregate, TRUE, FALSE, NULL, NULL) );

   SCIP_CALL( SCIPaddIntParam(scip, "constraints/"CONSHDLR_NAME"/maxproprounds",
         "limit on number of propagation rounds for a single constraint within one round of SCIP propagation",
         &conshdlrdata->maxproprounds, FALSE, 1, 0, INT_MAX, NULL, NULL) );

   SCIP_CALL( SCIPincludeEventhdlr(scip, CONSHDLR_NAME"_boundchange", "signals a bound change to a quadratic constraint",
         NULL,
         NULL, NULL, NULL, NULL, NULL, NULL, processVarEvent, NULL) );
   conshdlrdata->eventhdlr = SCIPfindEventhdlr(scip, CONSHDLR_NAME"_boundchange");

   SCIP_CALL( SCIPincludeEventhdlr(scip, CONSHDLR_NAME"_newsolution", "handles the event that a new primal solution has been found",
         NULL,
         NULL, NULL, NULL, NULL, NULL, NULL, processNewSolutionEvent, NULL) );

   return SCIP_OKAY;
}

/** includes a quadratic constraint update method into the quadratic constraint handler */
SCIP_RETCODE SCIPincludeQuadconsUpgrade(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_DECL_QUADCONSUPGD((*quadconsupgd)),  /**< method to call for upgrading quadratic constraint */
   int                   priority,           /**< priority of upgrading method */
   const char*           conshdlrname        /**< name of the constraint handler */
   )
{
   SCIP_CONSHDLR*        conshdlr;
   SCIP_CONSHDLRDATA*    conshdlrdata;
   SCIP_QUADCONSUPGRADE* quadconsupgrade;
   char                  paramname[SCIP_MAXSTRLEN];
   char                  paramdesc[SCIP_MAXSTRLEN];
   int                   i;
   
   assert(quadconsupgd != NULL);
   assert(conshdlrname != NULL );

   /* find the quadratic constraint handler */
   conshdlr = SCIPfindConshdlr(scip, CONSHDLR_NAME);
   if( conshdlr == NULL )
   {
      SCIPerrorMessage("quadratic constraint handler not found\n");
      return SCIP_PLUGINNOTFOUND;
   }

   conshdlrdata = SCIPconshdlrGetData(conshdlr);
   assert(conshdlrdata != NULL);

   if( !conshdlrdataHasUpgrade(scip, conshdlrdata, quadconsupgd, conshdlrname) )
   {
      /* create a quadratic constraint upgrade data object */
      SCIP_CALL( SCIPallocMemory(scip, &quadconsupgrade) );
      quadconsupgrade->quadconsupgd = quadconsupgd;
      quadconsupgrade->priority     = priority;
      quadconsupgrade->active       = TRUE;

      /* insert quadratic constraint upgrade method into constraint handler data */
      assert(conshdlrdata->nquadconsupgrades <= conshdlrdata->quadconsupgradessize);
      if( conshdlrdata->nquadconsupgrades+1 > conshdlrdata->quadconsupgradessize )
      {
         int newsize;

         newsize = SCIPcalcMemGrowSize(scip, conshdlrdata->nquadconsupgrades+1);
         SCIP_CALL( SCIPreallocMemoryArray(scip, &conshdlrdata->quadconsupgrades, newsize) );
         conshdlrdata->quadconsupgradessize = newsize;
      }
      assert(conshdlrdata->nquadconsupgrades+1 <= conshdlrdata->quadconsupgradessize);
   
      for( i = conshdlrdata->nquadconsupgrades; i > 0 && conshdlrdata->quadconsupgrades[i-1]->priority < quadconsupgrade->priority; --i )
         conshdlrdata->quadconsupgrades[i] = conshdlrdata->quadconsupgrades[i-1];
      assert(0 <= i && i <= conshdlrdata->nquadconsupgrades);
      conshdlrdata->quadconsupgrades[i] = quadconsupgrade;
      conshdlrdata->nquadconsupgrades++;

      /* adds parameter to turn on and off the upgrading step */
      (void) SCIPsnprintf(paramname, SCIP_MAXSTRLEN, "constraints/"CONSHDLR_NAME"/upgrade/%s", conshdlrname);
      (void) SCIPsnprintf(paramdesc, SCIP_MAXSTRLEN, "enable quadratic upgrading for constraint handler <%s>", conshdlrname);
      SCIP_CALL( SCIPaddBoolParam(scip,
            paramname, paramdesc,
            &quadconsupgrade->active, FALSE, TRUE, NULL, NULL) );
   }

   return SCIP_OKAY;
}

/** Creates and captures a quadratic constraint.
 * 
 *  The constraint should be given in the form
 *  \f[
 *  \ell \leq \sum_{i=1}^n b_i x_i + \sum_{j=1}^m a_j y_jz_j \leq u,
 *  \f]
 *  where \f$x_i = y_j = z_k\f$ is possible.
 */
SCIP_RETCODE SCIPcreateConsQuadratic(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONS**           cons,               /**< pointer to hold the created constraint */
   const char*           name,               /**< name of constraint */
   int                   nlinvars,           /**< number of linear terms (n) */
   SCIP_VAR**            linvars,            /**< array with variables in linear part (x_i) */
   SCIP_Real*            lincoefs,           /**< array with coefficients of variables in linear part (b_i) */
   int                   nquadterms,         /**< number of quadratic terms (m) */
   SCIP_VAR**            quadvars1,          /**< array with first variables in quadratic terms (y_j) */
   SCIP_VAR**            quadvars2,          /**< array with second variables in quadratic terms (z_j) */
   SCIP_Real*            quadcoefs,          /**< array with coefficients of quadratic terms (a_j) */
   SCIP_Real             lhs,                /**< left hand side of quadratic equation (ell) */
   SCIP_Real             rhs,                /**< right hand side of quadratic equation (u) */
   SCIP_Bool             initial,            /**< should the LP relaxation of constraint be in the initial LP?
                                              *   Usually set to TRUE. Set to FALSE for 'lazy constraints'. */
   SCIP_Bool             separate,           /**< should the constraint be separated during LP processing?
                                              *   Usually set to TRUE. */
   SCIP_Bool             enforce,            /**< should the constraint be enforced during node processing?
                                              *   TRUE for model constraints, FALSE for additional, redundant constraints. */
   SCIP_Bool             check,              /**< should the constraint be checked for feasibility?
                                              *   TRUE for model constraints, FALSE for additional, redundant constraints. */
   SCIP_Bool             propagate,          /**< should the constraint be propagated during node processing?
                                              *   Usually set to TRUE. */
   SCIP_Bool             local,              /**< is constraint only valid locally?
                                              *   Usually set to FALSE. Has to be set to TRUE, e.g., for branching constraints. */
   SCIP_Bool             modifiable,         /**< is constraint modifiable (subject to column generation)?
                                              *   Usually set to FALSE. In column generation applications, set to TRUE if pricing
                                              *   adds coefficients to this constraint. */
   SCIP_Bool             dynamic,            /**< is constraint subject to aging?
                                              *   Usually set to FALSE. Set to TRUE for own cuts which
                                              *   are seperated as constraints. */
   SCIP_Bool             removable           /**< should the relaxation be removed from the LP due to aging or cleanup?
                                              *   Usually set to FALSE. Set to TRUE for 'lazy constraints' and 'user cuts'. */
   )
{
   SCIP_CONSHDLR* conshdlr;
   SCIP_CONSDATA* consdata;
   SCIP_HASHMAP*  quadvaridxs;
   SCIP_Real      sqrcoef;
   int i;
   int var1pos;
   int var2pos;
   
   int nbilinterms;
   
   assert(modifiable == FALSE); /* we do not support column generation */

   /* find the quadratic constraint handler */
   conshdlr = SCIPfindConshdlr(scip, CONSHDLR_NAME);
   if( conshdlr == NULL )
   {
      SCIPerrorMessage("quadratic constraint handler not found\n");
      return SCIP_PLUGINNOTFOUND;
   }

   /* create constraint data and constraint */
   SCIP_CALL( consdataCreateEmpty(scip, &consdata) );
   
   consdata->lhs = lhs;
   consdata->rhs = rhs;
   
   SCIP_CALL( SCIPcreateCons(scip, cons, name, conshdlr, consdata, initial, separate, enforce, check, propagate,
         local, modifiable, dynamic, removable, FALSE) );

   /* add quadratic variables and remember their indices */
   SCIP_CALL( SCIPhashmapCreate(&quadvaridxs, SCIPblkmem(scip), SCIPcalcHashtableSize(5 * nquadterms)) );
   nbilinterms = 0;
   for( i = 0; i < nquadterms; ++i )
   {
      if( SCIPisZero(scip, quadcoefs[i]) )
         continue;
      
      /* if it is actually a square term, remember it's coefficient */
      if( quadvars1[i] == quadvars2[i] )
         sqrcoef = quadcoefs[i];
      else
         sqrcoef = 0.0;

      /* add quadvars1[i], if not in there already */
      if( !SCIPhashmapExists(quadvaridxs, quadvars1[i]) )
      {
         SCIP_CALL( addQuadVarTerm(scip, *cons, quadvars1[i], 0.0, sqrcoef, FALSE) );
         assert(consdata->nquadvars >= 0);
         assert(consdata->quadvarterms[consdata->nquadvars-1].var == quadvars1[i]);
         
         SCIP_CALL( SCIPhashmapInsert(quadvaridxs, quadvars1[i], (void*)(size_t)(consdata->nquadvars-1)) );
      }
      else if( !SCIPisZero(scip, sqrcoef) )
      {
         /* if it's there already, but we got a square coefficient, add it to the previous one */ 
         var1pos = (int) (size_t) SCIPhashmapGetImage(quadvaridxs, quadvars1[i]);
         assert(consdata->quadvarterms[var1pos].var == quadvars1[i]);
         consdata->quadvarterms[var1pos].sqrcoef += sqrcoef;
      }
      
      if( quadvars1[i] == quadvars2[i] )
         continue;
      
      /* add quadvars2[i], if not in there already */
      if( !SCIPhashmapExists(quadvaridxs, quadvars2[i]) )
      {
         assert(sqrcoef == 0.0);
         SCIP_CALL( addQuadVarTerm(scip, *cons, quadvars2[i], 0.0, 0.0, FALSE) );
         assert(consdata->nquadvars >= 0);
         assert(consdata->quadvarterms[consdata->nquadvars-1].var == quadvars2[i]);
         
         SCIP_CALL( SCIPhashmapInsert(quadvaridxs, quadvars2[i], (void*)(size_t)(consdata->nquadvars-1)) );
      }
      
      ++nbilinterms;
   }
   
   /* add bilinear terms, if we saw any */
   if( nbilinterms > 0 )
   {
      SCIP_CALL( consdataEnsureBilinSize(scip, consdata, nbilinterms) );
      for( i = 0; i < nquadterms; ++i )
      {
         if( SCIPisZero(scip, quadcoefs[i]) )
            continue;

         /* square terms have been taken care of already */
         if( quadvars1[i] == quadvars2[i] )
            continue;
         
         assert(SCIPhashmapExists(quadvaridxs, quadvars1[i]));
         assert(SCIPhashmapExists(quadvaridxs, quadvars2[i]));
         
         var1pos = (int) (size_t) SCIPhashmapGetImage(quadvaridxs, quadvars1[i]);
         var2pos = (int) (size_t) SCIPhashmapGetImage(quadvaridxs, quadvars2[i]);
         
         SCIP_CALL( addBilinearTerm(scip, *cons, var1pos, var2pos, quadcoefs[i]) );
      }
   }

   /* add linear variables */
   SCIP_CALL( consdataEnsureLinearVarsSize(scip, consdata, nlinvars) );
   for( i = 0; i < nlinvars; ++i )
   {
      if( SCIPisZero(scip, lincoefs[i]) )
         continue;
      
      /* if it's a linear coefficient for a quadratic variable, add it there, otherwise add as linear variable */
      if( SCIPhashmapExists(quadvaridxs, linvars[i]) )
      {
         var1pos = (int) (size_t) SCIPhashmapGetImage(quadvaridxs, linvars[i]);
         assert(consdata->quadvarterms[var1pos].var == linvars[i]);
         consdata->quadvarterms[var1pos].lincoef += lincoefs[i];
      }
      else
      {
         SCIP_CALL( addLinearCoef(scip, *cons, linvars[i], lincoefs[i]) );
      }
   }

   if( SCIPisTransformed(scip) )
   {
      SCIP_CONSHDLRDATA* conshdlrdata = SCIPconshdlrGetData(conshdlr);
      assert(conshdlrdata != NULL);
      assert(conshdlrdata->eventhdlr != NULL);
      
      SCIP_CALL( catchVarEvents(scip, conshdlrdata->eventhdlr, *cons) );
   }
   
   SCIPhashmapFree(&quadvaridxs);
   
   SCIPdebugMessage("created quadratic constraint ");
   SCIPdebug( SCIPprintCons(scip, *cons, NULL) );

   return SCIP_OKAY;
}

/** Creates and captures a quadratic constraint.
 * 
 * The constraint should be given in the form
 * \f[
 * \ell \leq \sum_{i=1}^n b_i x_i + \sum_{j=1}^m (a_j y_j^2 + b_j y_j) + \sum_{k=1}^p c_kv_kw_k \leq u.
 * \f]
 */
SCIP_RETCODE SCIPcreateConsQuadratic2(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONS**           cons,               /**< pointer to hold the created constraint */
   const char*           name,               /**< name of constraint */
   int                   nlinvars,           /**< number of linear terms (n) */
   SCIP_VAR**            linvars,            /**< array with variables in linear part (x_i) */ 
   SCIP_Real*            lincoefs,           /**< array with coefficients of variables in linear part (b_i) */ 
   int                   nquadvarterms,      /**< number of quadratic terms (m) */
   SCIP_QUADVARTERM*     quadvarterms,       /**< quadratic variable terms */
   int                   nbilinterms,        /**< number of bilinear terms (p) */
   SCIP_BILINTERM*       bilinterms,         /**< bilinear terms */
   SCIP_Real             lhs,                /**< constraint left hand side (ell) */
   SCIP_Real             rhs,                /**< constraint right hand side (u) */
   SCIP_Bool             initial,            /**< should the LP relaxation of constraint be in the initial LP? */
   SCIP_Bool             separate,           /**< should the constraint be separated during LP processing? */
   SCIP_Bool             enforce,            /**< should the constraint be enforced during node processing? */
   SCIP_Bool             check,              /**< should the constraint be checked for feasibility? */
   SCIP_Bool             propagate,          /**< should the constraint be propagated during node processing? */
   SCIP_Bool             local,              /**< is constraint only valid locally? */
   SCIP_Bool             modifiable,         /**< is constraint modifiable (subject to column generation)? */
   SCIP_Bool             dynamic,            /**< is constraint dynamic? */
   SCIP_Bool             removable           /**< should the constraint be removed from the LP due to aging or cleanup? */
   )
{
   SCIP_CONSHDLR* conshdlr;
   SCIP_CONSDATA* consdata;
   
   assert(modifiable == FALSE); /* we do not support column generation */
   assert(nlinvars == 0 || (linvars != NULL && lincoefs != NULL));
   assert(nquadvarterms == 0 || quadvarterms != NULL);
   assert(nbilinterms == 0 || bilinterms != NULL);
   
   /* find the quadratic constraint handler */
   conshdlr = SCIPfindConshdlr(scip, CONSHDLR_NAME);
   if( conshdlr == NULL )
   {
      SCIPerrorMessage("quadratic constraint handler not found\n");
      return SCIP_PLUGINNOTFOUND;
   }

   /* create constraint data */
   SCIP_CALL( consdataCreate(scip, &consdata, lhs, rhs,
      nlinvars, linvars, lincoefs, nquadvarterms, quadvarterms, nbilinterms, bilinterms,
      TRUE) );
  
   /* create constraint */
   SCIP_CALL( SCIPcreateCons(scip, cons, name, conshdlr, consdata, initial, separate, enforce, check, propagate,
      local, modifiable, dynamic, removable, FALSE) );

   if( SCIPisTransformed(scip) )
   {
      SCIP_CONSHDLRDATA* conshdlrdata = SCIPconshdlrGetData(conshdlr);
      assert(conshdlrdata != NULL);
      assert(conshdlrdata->eventhdlr != NULL);
      
      SCIP_CALL( catchVarEvents(scip, conshdlrdata->eventhdlr, *cons) );
   }

   return SCIP_OKAY;
}

/** Adds a constant to the constraint function, that is, substracts a constant from both sides */
void SCIPaddConstantQuadratic(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONS*            cons,               /**< constraint */
   SCIP_Real             constant            /**< constant to substract from both sides */
   )
{
   SCIP_CONSDATA* consdata;

   assert(scip != NULL);
   assert(cons != NULL);
   assert(!SCIPisInfinity(scip, REALABS(constant)));

   consdata = SCIPconsGetData(cons);
   assert(consdata != NULL);
   assert(consdata->lhs <= consdata->rhs);

   if( !SCIPisInfinity(scip, -consdata->lhs) )
      consdata->lhs -= constant;
   if( !SCIPisInfinity(scip,  consdata->rhs) )
      consdata->rhs -= constant;

   if( consdata->lhs > consdata->rhs )
   {
      assert(SCIPisEQ(scip, consdata->lhs, consdata->rhs));
      consdata->lhs = consdata->rhs;
   }
}

/** Adds a linear variable with coefficient to a quadratic constraint.
 */
SCIP_RETCODE SCIPaddLinearVarQuadratic(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONS*            cons,               /**< constraint */
   SCIP_VAR*             var,                /**< variable */
   SCIP_Real             coef                /**< coefficient of variable */
   )
{
   assert(scip != NULL);
   assert(cons != NULL);
   assert(var  != NULL);
   assert(!SCIPisInfinity(scip, REALABS(coef)));
   
   SCIP_CALL( addLinearCoef(scip, cons, var, coef) );
   
   return SCIP_OKAY;
}

/** Adds a quadratic variable with linear and square coefficient to a quadratic constraint.
 */
SCIP_RETCODE SCIPaddQuadVarQuadratic(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONS*            cons,               /**< constraint */
   SCIP_VAR*             var,                /**< variable */
   SCIP_Real             lincoef,            /**< linear coefficient of variable */
   SCIP_Real             sqrcoef             /**< square coefficient of variable */
   )
{
   assert(scip != NULL);
   assert(cons != NULL);
   assert(var  != NULL);
   assert(!SCIPisInfinity(scip, REALABS(lincoef)));
   assert(!SCIPisInfinity(scip, REALABS(sqrcoef)));
   
   SCIP_CALL( addQuadVarTerm(scip, cons, var, lincoef, sqrcoef, SCIPconsIsTransformed(cons)) );
   
   return SCIP_OKAY;
}

/** Adds a linear coefficient for a quadratic variable.
 * variable need to have been added as quadratic variable before
 * @see SCIPaddQuadVarQuadratic
 */
SCIP_RETCODE SCIPaddQuadVarLinearCoefQuadratic(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONS*            cons,               /**< constraint */
   SCIP_VAR*             var,                /**< variable */
   SCIP_Real             coef                /**< value to add to linear coefficient of variable */
   )
{
   SCIP_CONSDATA* consdata;
   int pos;

   assert(scip != NULL);
   assert(cons != NULL);
   assert(var  != NULL);
   assert(!SCIPisInfinity(scip, REALABS(coef)));

   if( SCIPisZero(scip, coef) )
      return SCIP_OKAY;

   consdata = SCIPconsGetData(cons);
   assert(consdata != NULL);

   SCIP_CALL( consdataFindQuadVarTerm(scip, consdata, var, &pos) );
   if( pos < 0 )
   {
      SCIPerrorMessage("Quadratic variable <%s> not found in constraint. Cannot change linear coefficient.\n", SCIPvarGetName(var));
      return SCIP_INVALIDDATA;
   }
   assert(pos < consdata->nquadvars);
   assert(consdata->quadvarterms[pos].var == var);

   consdata->quadvarterms[pos].lincoef += coef;

   /* update flags and invalid activities */
   consdata->ispropagated  = FALSE;
   consdata->ispresolved   = consdata->ispresolved && !SCIPisZero(scip, consdata->quadvarterms[pos].lincoef);

   SCIPintervalSetEmpty(&consdata->quadactivitybounds);
   consdata->activity = SCIP_INVALID;

   return SCIP_OKAY;
}

/** Adds a square coefficient for a quadratic variable.
 * variable need to have been added as quadratic variable before
 * @see SCIPaddQuadVarQuadratic
 */
SCIP_RETCODE SCIPaddSquareCoefQuadratic(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONS*            cons,               /**< constraint */
   SCIP_VAR*             var,                /**< variable */
   SCIP_Real             coef                /**< value to add to square coefficient of variable */
   )
{
   SCIP_CONSDATA* consdata;
   int pos;

   assert(scip != NULL);
   assert(cons != NULL);
   assert(var  != NULL);
   assert(!SCIPisInfinity(scip, REALABS(coef)));

   if( SCIPisZero(scip, coef) )
      return SCIP_OKAY;

   consdata = SCIPconsGetData(cons);
   assert(consdata != NULL);

   SCIP_CALL( consdataFindQuadVarTerm(scip, consdata, var, &pos) );
   if( pos < 0 )
   {
      SCIPerrorMessage("Quadratic variable <%s> not found in constraint. Cannot change square coefficient.\n", SCIPvarGetName(var));
      return SCIP_INVALIDDATA;
   }
   assert(pos < consdata->nquadvars);
   assert(consdata->quadvarterms[pos].var == var);

   consdata->quadvarterms[pos].sqrcoef += coef;

   /* update flags and invalid activities */
   consdata->isconvex      = FALSE;
   consdata->isconcave     = FALSE;
   consdata->iscurvchecked = FALSE;
   consdata->ispropagated  = FALSE;
   consdata->ispresolved   = consdata->ispresolved && !SCIPisZero(scip, consdata->quadvarterms[pos].sqrcoef);

   SCIPintervalSetEmpty(&consdata->quadactivitybounds);
   consdata->activity = SCIP_INVALID;

   return SCIP_OKAY;
}

/** Adds a bilinear term to a quadratic constraint.
 * The variables of the bilinear term must have been added before.
 * The variables need to be different.
 */
SCIP_RETCODE SCIPaddBilinTermQuadratic(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONS*            cons,               /**< constraint */
   SCIP_VAR*             var1,               /**< first variable */
   SCIP_VAR*             var2,               /**< second variable */
   SCIP_Real             coef                /**< coefficient of bilinear term */
   )
{
   SCIP_CONSDATA* consdata;
   int            var1pos;
   int            var2pos;
   
   assert(scip != NULL);
   assert(cons != NULL);
   assert(var1 != NULL);
   assert(var2 != NULL);
   assert(var1 != var2);
   assert(!SCIPisInfinity(scip, REALABS(coef)));
   
   consdata = SCIPconsGetData(cons);
   assert(consdata != NULL);
   
   SCIP_CALL( consdataFindQuadVarTerm(scip, consdata, var1, &var1pos) );
   if( var1pos < 0 )
   {
      SCIPerrorMessage("Quadratic variable <%s> not found in constraint. Cannot add bilinear term.\n", SCIPvarGetName(var1));
      return SCIP_INVALIDDATA;
   }
   
   SCIP_CALL( consdataFindQuadVarTerm(scip, consdata, var2, &var2pos) );
   if( var2pos < 0 )
   {
      SCIPerrorMessage("Quadratic variable <%s> not found in constraint. Cannot add bilinear term.\n", SCIPvarGetName(var2));
      return SCIP_INVALIDDATA;
   }
   
   SCIP_CALL( addBilinearTerm(scip, cons, var1pos, var2pos, coef) );

   return SCIP_OKAY;
}

/** Gets the quadratic constraint as a nonlinear row representation.
 */
SCIP_RETCODE SCIPgetNlRowQuadratic(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONS*            cons,               /**< constraint */
   SCIP_NLROW**          nlrow               /**< a buffer where to store pointer to nonlinear row */
   )
{
   SCIP_CONSDATA* consdata;

   assert(cons  != NULL);
   assert(nlrow != NULL);

   consdata = SCIPconsGetData(cons);
   assert(consdata != NULL);

   if( consdata->nlrow == NULL )
   {
      SCIP_CALL( createNlRow(scip, cons) );
   }
   assert(consdata->nlrow != NULL);
   *nlrow = consdata->nlrow;

   return SCIP_OKAY;
}

/** Gets the number of variables in the linear term of a quadratic constraint.
 */
int SCIPgetNLinearVarsQuadratic(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONS*            cons                /**< constraint */
   )
{
   assert(cons != NULL);
   assert(SCIPconsGetData(cons) != NULL);
   
   return SCIPconsGetData(cons)->nlinvars;
}

/** Gets the variables in the linear part of a quadratic constraint.
 *  Length is given by SCIPgetNLinearVarsQuadratic.
 */
SCIP_VAR** SCIPgetLinearVarsQuadratic(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONS*            cons                /**< constraint */
   )
{
   assert(cons != NULL);
   assert(SCIPconsGetData(cons) != NULL);
   
   return SCIPconsGetData(cons)->linvars;
}

/** Gets the coefficients in the linear part of a quadratic constraint.
 *  Length is given by SCIPgetNLinearVarsQuadratic.
 */
SCIP_Real* SCIPgetCoefsLinearVarsQuadratic(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONS*            cons                /**< constraint */
   )
{
   assert(cons != NULL);
   assert(SCIPconsGetData(cons) != NULL);
   
   return SCIPconsGetData(cons)->lincoefs;
}

/** Gets the number of quadratic variable terms of a quadratic constraint.
 */
int SCIPgetNQuadVarTermsQuadratic(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONS*            cons                /**< constraint */
   )
{
   assert(cons != NULL);
   assert(SCIPconsGetData(cons) != NULL);
   
   return SCIPconsGetData(cons)->nquadvars;
}

/** Gets the quadratic variable terms of a quadratic constraint.
 *  Length is given by SCIPgetNQuadVarTermsQuadratic.
 */
SCIP_QUADVARTERM* SCIPgetQuadVarTermsQuadratic(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONS*            cons                /**< constraint */
   )
{
   assert(cons != NULL);
   assert(SCIPconsGetData(cons) != NULL);
   
   return SCIPconsGetData(cons)->quadvarterms;
}

/** Gets the number of bilinear terms of a quadratic constraint.
 */
int SCIPgetNBilinTermsQuadratic(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONS*            cons                /**< constraint */
   )
{
   assert(cons != NULL);
   assert(SCIPconsGetData(cons) != NULL);
   
   return SCIPconsGetData(cons)->nbilinterms;
}

/** Gets the bilinear terms of a quadratic constraint.
 *  Length is given by SCIPgetNBilinTermQuadratic.
 */
SCIP_BILINTERM* SCIPgetBilinTermsQuadratic(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONS*            cons                /**< constraint */
   )
{
   assert(cons != NULL);
   assert(SCIPconsGetData(cons) != NULL);
   
   return SCIPconsGetData(cons)->bilinterms;
}

/** Gets the left hand side of a quadratic constraint.
 */
SCIP_Real SCIPgetLhsQuadratic(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONS*            cons                /**< constraint */
   )
{
   assert(cons != NULL);
   assert(SCIPconsGetData(cons) != NULL);
   
   return SCIPconsGetData(cons)->lhs;
}

/** Gets the right hand side of a quadratic constraint.
 */
SCIP_Real SCIPgetRhsQuadratic(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONS*            cons                /**< constraint */
   )
{
   assert(cons != NULL);
   assert(SCIPconsGetData(cons) != NULL);
   
   return SCIPconsGetData(cons)->rhs;
}

/** Check the quadratic function of a quadratic constraint for its semi-definitness, if not done yet.
 */
SCIP_RETCODE SCIPcheckCurvatureQuadratic(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONS*            cons                /**< constraint */
   )
{
   assert(cons != NULL);

   SCIP_CALL( checkCurvature(scip, cons, TRUE) );
   
   return SCIP_OKAY;
}

/** Indicates whether the quadratic function of a quadratic constraint is (known to be) convex.
 */
SCIP_Bool SCIPisConvexQuadratic(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONS*            cons                /**< constraint */
   )
{
   assert(cons != NULL);
   assert(SCIPconsGetData(cons) != NULL);
   
   /* with FALSE, one should never get an error, since there is no memory allocated */
   SCIP_CALL_ABORT( checkCurvature(scip, cons, FALSE) );
   
   return SCIPconsGetData(cons)->isconvex;
}

/** Indicates whether the quadratic function of a quadratic constraint is (known to be) concave.
 */
SCIP_Bool SCIPisConcaveQuadratic(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONS*            cons                /**< constraint */
   )
{
   assert(cons != NULL);
   assert(SCIPconsGetData(cons) != NULL);

   /* with FALSE, one should never get an error, since there is no memory allocated */
   SCIP_CALL_ABORT( checkCurvature(scip, cons, FALSE) );

   return SCIPconsGetData(cons)->isconcave;
}

/** Computes the violation of a constraint by a solution */
SCIP_RETCODE SCIPgetViolationQuadratic(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONS*            cons,               /**< constraint */
   SCIP_SOL*             sol,                /**< solution which violation to calculate, or NULL for LP solution */
   SCIP_Real*            violation           /**< buffer to store violation of constraint */
   )
{
   SCIP_CONSDATA* consdata;
   
   assert(scip != NULL);
   assert(cons != NULL);
   assert(violation != NULL);
   
   SCIP_CALL( computeViolation(scip, cons, sol, TRUE) ); /* we assume that scaling was left on */

   consdata = SCIPconsGetData(cons);
   assert(consdata != NULL);
   
   *violation = MAX(consdata->lhsviol, consdata->rhsviol);
   
   return SCIP_OKAY;
}

/** Adds the constraint to an NLPI problem. */
SCIP_RETCODE SCIPaddToNlpiProblemQuadratic(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONS*            cons,               /**< constraint */
   SCIP_NLPI*            nlpi,               /**< interface to NLP solver */
   SCIP_NLPIPROBLEM*     nlpiprob,           /**< NLPI problem where to add constraint */
   SCIP_HASHMAP*         scipvar2nlpivar,    /**< mapping from SCIP variables to variable indices in NLPI */
   SCIP_Bool             names               /**< whether to pass constraint names to NLPI */
   )
{
   SCIP_CONSDATA* consdata;
   int            nlininds;
   int*           lininds;
   SCIP_Real*     linvals;
   int            nquadelems;
   SCIP_QUADELEM* quadelems;
   SCIP_VAR*      othervar;
   const char*    name;
   int            j;
   int            l;
   int            lincnt;
   int            quadcnt;
   int            idx1;
   int            idx2;

   assert(scip != NULL);
   assert(cons != NULL);
   assert(nlpi != NULL);
   assert(nlpiprob != NULL);
   assert(scipvar2nlpivar != NULL);
   
   consdata = SCIPconsGetData(cons);
   assert(consdata != NULL);

   /* count nonzeros in quadratic part */
   nlininds = consdata->nlinvars;
   nquadelems = consdata->nbilinterms;
   for( j = 0; j < consdata->nquadvars; ++j )
   {
      if( consdata->quadvarterms[j].sqrcoef )
         ++nquadelems;
      if( consdata->quadvarterms[j].lincoef != 0.0 )
         ++nlininds;
   }

   /* setup linear part */
   lininds = NULL;
   linvals = NULL;
   lincnt  = 0;
   if( nlininds > 0 )
   {
      SCIP_CALL( SCIPallocBufferArray(scip, &lininds, nlininds) );
      SCIP_CALL( SCIPallocBufferArray(scip, &linvals, nlininds) );
      
      for( j = 0; j < consdata->nlinvars; ++j )
      {
         linvals[j] = consdata->lincoefs[j];
         assert(SCIPhashmapExists(scipvar2nlpivar, consdata->linvars[j]));
         lininds[j] = (int) (size_t) SCIPhashmapGetImage(scipvar2nlpivar, consdata->linvars[j]);
      }
      
      lincnt = consdata->nlinvars;
   }

   /* setup quadratic part */
   quadelems = NULL;
   if( nquadelems > 0 )
   {
      SCIP_CALL( SCIPallocBufferArray(scip, &quadelems, nquadelems) );
   }
   quadcnt = 0;
     
   for( j = 0; j < consdata->nquadvars; ++j )
   {
      assert(SCIPhashmapExists(scipvar2nlpivar, consdata->quadvarterms[j].var));
      idx1 = (int)(size_t)SCIPhashmapGetImage(scipvar2nlpivar, consdata->quadvarterms[j].var);
      if( consdata->quadvarterms[j].lincoef != 0.0 )
      {
         assert(lininds != NULL);
         assert(linvals != NULL);
         lininds[lincnt] = idx1;
         linvals[lincnt] = consdata->quadvarterms[j].lincoef;
         ++lincnt;
      }

      if( consdata->quadvarterms[j].sqrcoef != 0.0 )
      {
         assert(quadcnt < nquadelems);
         assert(quadelems != NULL);
         quadelems[quadcnt].idx1 = idx1;
         quadelems[quadcnt].idx2 = idx1;
         quadelems[quadcnt].coef = consdata->quadvarterms[j].sqrcoef;
         ++quadcnt;
      }

      for( l = 0; l < consdata->quadvarterms[j].nadjbilin; ++l )
      {
         othervar = consdata->bilinterms[consdata->quadvarterms[j].adjbilin[l]].var2;
         /* if othervar is on position 2, then we process this bilinear term later (or it was processed already) */
         if( othervar == consdata->quadvarterms[j].var )
            continue;

         assert(quadcnt < nquadelems);
         assert(quadelems != NULL);
         assert(SCIPhashmapExists(scipvar2nlpivar, othervar));
         idx2 = (int)(size_t)SCIPhashmapGetImage(scipvar2nlpivar, othervar);
         quadelems[quadcnt].idx1 = MIN(idx1, idx2);
         quadelems[quadcnt].idx2 = MAX(idx1, idx2);
         quadelems[quadcnt].coef = consdata->bilinterms[consdata->quadvarterms[j].adjbilin[l]].coef;
         ++quadcnt;
      }
   }

   assert(quadcnt == nquadelems);
   assert(lincnt  == nlininds);

   name = names ? SCIPconsGetName(cons) : NULL;

   SCIP_CALL( SCIPnlpiAddConstraints(nlpi, nlpiprob, 1,
      &consdata->lhs, &consdata->rhs,
      &nlininds, &lininds, &linvals ,
      &nquadelems, &quadelems,
      NULL, NULL, &name) );

   SCIPfreeBufferArrayNull(scip, &quadelems);
   SCIPfreeBufferArrayNull(scip, &lininds);
   SCIPfreeBufferArrayNull(scip, &linvals);

   return SCIP_OKAY;
}
