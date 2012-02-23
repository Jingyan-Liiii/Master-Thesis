/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                           */
/*                  This file is part of the program                         */
/*          GCG --- Generic Column Generation                                */
/*                  a Dantzig-Wolfe decomposition based extension            */
/*                  of the branch-cut-and-price framework                    */
/*         SCIP --- Solving Constraint Integer Programs                      */
/*                                                                           */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/**@file   dialog_gcg.c
 * @brief  gcg user interface dialog
 * @author Tobias Achterberg
 * @author Timo Berthold
 * @author Gerald Gamrath
 * @author Martin Bergner
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#include <assert.h>
#include <string.h>

#include "scip/pub_dialog.h"
#include "scip/type_dialog.h"

#include "dialog_gcg.h"
#include "relax_gcg.h"
#include "pricer_gcg.h"
#include "cons_decomp.h"


/** executes a menu dialog */
static
SCIP_RETCODE dialogExecMenu(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_DIALOG*          dialog,             /**< dialog menu */
   SCIP_DIALOGHDLR*      dialoghdlr,         /**< dialog handler */
   SCIP_DIALOG**         nextdialog          /**< pointer to store next dialog to execute */
   )
{
   char* command;
   SCIP_Bool again;
   SCIP_Bool endoffile;
   int nfound;

   do
   {
      again = FALSE;

      /* get the next word of the command string */
      SCIP_CALL( SCIPdialoghdlrGetWord(dialoghdlr, dialog, NULL, &command, &endoffile) );
      if( endoffile )
      {
         *nextdialog = NULL;
         return SCIP_OKAY;
      }

      /* exit to the root dialog, if command is empty */
      if( command[0] == '\0' )
      {
         *nextdialog = SCIPdialoghdlrGetRoot(dialoghdlr);
         return SCIP_OKAY;
      }
      else if( strcmp(command, "..") == 0 )
      {
         *nextdialog = SCIPdialogGetParent(dialog);
         if( *nextdialog == NULL )
            *nextdialog = SCIPdialoghdlrGetRoot(dialoghdlr);
         return SCIP_OKAY;
      }

      /* find command in dialog */
      nfound = SCIPdialogFindEntry(dialog, command, nextdialog);

      /* check result */
      if( nfound == 0 )
      {
         SCIPdialogMessage(scip, NULL, "command <%s> not available\n", command);
         SCIPdialoghdlrClearBuffer(dialoghdlr);
         *nextdialog = dialog;
      }
      else if( nfound >= 2 )
      {
         SCIPdialogMessage(scip, NULL, "\npossible completions:\n");
         SCIP_CALL( SCIPdialogDisplayCompletions(dialog, scip, command) );
         SCIPdialogMessage(scip, NULL, "\n");
         SCIPdialoghdlrClearBuffer(dialoghdlr);
         again = TRUE;
      }
   }
   while( again );

   return SCIP_OKAY;
}


/* parse the given string to detect a bool value and returns it */
static
SCIP_Bool parseBoolValue(
   SCIP*                 scip,               /**< SCIP data structure */
   const char*           valuestr,           /**< string to parse */
   SCIP_Bool*            error               /**< pointer to store the error result */
   )
{
   assert( scip  != NULL );
   assert( valuestr != NULL );
   assert( error != NULL );

   *error = FALSE;

   switch( valuestr[0] )
   {
   case 'f':
   case 'F':
   case '0':
   case 'n':
   case 'N':
      return FALSE;
   case 't':
   case 'T':
   case '1':
   case 'y':
   case 'Y':
      return TRUE;
   default:
      SCIPdialogMessage(scip, NULL, "\ninvalid parameter value <%s>\n\n", valuestr);
      *error = TRUE;
      break;
   }

   return FALSE;
}


/* display the reader information */
static
void displayReaders(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_Bool             reader,             /**< display reader which can read */
   SCIP_Bool             writer              /**< display reader which can write */
   )
{
   SCIP_READER** readers;
   int nreaders;
   int r;

   assert( scip != NULL );

   readers = SCIPgetReaders(scip);
   nreaders = SCIPgetNReaders(scip);

   /* display list of readers */
   SCIPdialogMessage(scip, NULL, "\n");
   SCIPdialogMessage(scip, NULL, " file reader          extension  description\n");
   SCIPdialogMessage(scip, NULL, " -----------          ---------  -----------\n");
   for( r = 0; r < nreaders; ++r )
   {
      if( (reader && SCIPreaderCanRead(readers[r])) || (writer && SCIPreaderCanWrite(readers[r])) )
      {
         SCIPdialogMessage(scip, NULL, " %-20s ", SCIPreaderGetName(readers[r]));
         if( strlen(SCIPreaderGetName(readers[r])) > 20 )
            SCIPdialogMessage(scip, NULL, "\n %20s ", "-->");
         SCIPdialogMessage(scip, NULL, "%9s  ", SCIPreaderGetExtension(readers[r]));
         SCIPdialogMessage(scip, NULL, "%s", SCIPreaderGetDesc(readers[r]));
         SCIPdialogMessage(scip, NULL, "\n");
      }
   }
   SCIPdialogMessage(scip, NULL, "\n");
}


/* writes problem to file */
static
SCIP_RETCODE writeProblem(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_DIALOG*          dialog,             /**< dialog menu */
   SCIP_DIALOGHDLR*      dialoghdlr,         /**< dialog handler */
   SCIP_DIALOG**         nextdialog,         /**< pointer to store next dialog to execute */
   SCIP_Bool             transformed,        /**< output the transformed problem? */
   SCIP_Bool             genericnames        /**< using generic variable and constraint names? */
   )
{
   char* filename;
   SCIP_Bool endoffile;
   SCIP_RETCODE retcode;

   SCIP_CALL( SCIPdialoghdlrGetWord(dialoghdlr, dialog, "enter filename: ", &filename, &endoffile) );
   if( endoffile )
   {
      *nextdialog = NULL;
      return SCIP_OKAY;
   }

   if( filename[0] != '\0' )
   {
      char* tmpfilename;
      char* extension;

      SCIP_CALL( SCIPdialoghdlrAddHistory(dialoghdlr, dialog, filename, TRUE) );

      /* copy filename */
      SCIP_CALL( SCIPduplicateBufferArray(scip, &tmpfilename, filename, (int)strlen(filename)+1) );
      extension = NULL;

      do
      {
         if( transformed )
            retcode = SCIPwriteTransProblem(scip, tmpfilename, extension, genericnames);
         else
            retcode = SCIPwriteOrigProblem(scip, tmpfilename, extension, genericnames);

         if( retcode == SCIP_FILECREATEERROR )
         {
            SCIPdialogMessage(scip, NULL, "error creating the file <%s>\n", filename);
            SCIPdialoghdlrClearBuffer(dialoghdlr);
            break;
         }
         else if(retcode == SCIP_WRITEERROR )
         {
            SCIPdialogMessage(scip, NULL, "error writing file <%s>\n", filename);
            SCIPdialoghdlrClearBuffer(dialoghdlr);
            break;
         }
         else if( retcode == SCIP_PLUGINNOTFOUND )
         {
            /* ask user once for a suitable reader */
            if( extension == NULL )
            {
               SCIPdialogMessage(scip, NULL, "no reader for requested output format\n");

               SCIPdialogMessage(scip, NULL, "following readers are avaliable for writing:\n");
               displayReaders(scip, FALSE, TRUE);

               SCIP_CALL( SCIPdialoghdlrGetWord(dialoghdlr, dialog,
                     "select a suitable reader by extension (or return): ", &extension, &endoffile) );

               if( extension[0] == '\0' )
                  break;
            }
            else
            {
               SCIPdialogMessage(scip, NULL, "no reader for output in <%s> format\n", extension);
               extension = NULL;
            }
         }
         else
         {
            /* check for unexpected errors */
            SCIP_CALL( retcode );

            /* print result message if writing was successful */
            if( transformed )
               SCIPdialogMessage(scip, NULL, "written transformed problem to file <%s>\n", tmpfilename);
            else
               SCIPdialogMessage(scip, NULL, "written original problem to file <%s>\n", tmpfilename);
            break;
         }
      }
      while (extension != NULL );

      SCIPfreeBufferArray(scip, &tmpfilename);
   }

   return SCIP_OKAY;
}

/** writes out all decompositions currently known to cons_decomp */
static
SCIP_RETCODE writeAllDecompositions(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_DIALOG*          dialog,             /**< dialog menu */
   SCIP_DIALOGHDLR*      dialoghdlr,         /**< dialog handler */
   SCIP_DIALOG**         nextdialog          /**< pointer to store next dialog to execute */
   )
{

   char* filename;
   SCIP_Bool endoffile;
   SCIP_RETCODE retcode;

   SCIP_CALL( SCIPdialoghdlrGetWord(dialoghdlr, dialog, "enter extension: ", &filename, &endoffile) );
   if( endoffile )
   {
      *nextdialog = NULL;
      return SCIP_OKAY;
   }

   if( filename[0] != '\0' )
   {
      char* extension;
      extension = filename;
      SCIP_CALL( SCIPdialoghdlrAddHistory(dialoghdlr, dialog, extension, TRUE) );

      do
      {
         retcode = DECwriteAllDecomps(scip, extension);

         if( retcode == SCIP_FILECREATEERROR )
         {
            SCIPdialogMessage(scip, NULL, "error creating files\n");
            SCIPdialoghdlrClearBuffer(dialoghdlr);
            break;
         }
         else if(retcode == SCIP_WRITEERROR )
         {
            SCIPdialogMessage(scip, NULL, "error writing files\n");
            SCIPdialoghdlrClearBuffer(dialoghdlr);
            break;
         }
         else if( retcode == SCIP_PLUGINNOTFOUND )
         {
            /* ask user once for a suitable reader */
            if( extension == NULL )
            {
               SCIPdialogMessage(scip, NULL, "no reader for requested output format\n");

               SCIPdialogMessage(scip, NULL, "following readers are avaliable for writing:\n");
               displayReaders(scip, FALSE, TRUE);

               SCIP_CALL( SCIPdialoghdlrGetWord(dialoghdlr, dialog,
                     "select a suitable reader by extension (or return): ", &extension, &endoffile) );

               if( extension[0] == '\0' )
                  break;
            }
            else
            {
               SCIPdialogMessage(scip, NULL, "no reader for output in <%s> format\n", extension);
               extension = NULL;
            }
         }
         else
         {
            /* check for unexpected errors */
            SCIP_CALL( retcode );

            /* print result message if writing was successful */
            SCIPdialogMessage(scip, NULL, "written all decompositions %s\n", extension);
            break;
         }
      }
      while (extension != NULL );
   }

   return SCIP_OKAY;
}

/** standard menu dialog execution method, that displays it's help screen if the remaining command line is empty */
SCIP_DECL_DIALOGEXEC(GCGdialogExecMenu)
{  /*lint --e{715}*/
   /* if remaining command string is empty, display menu of available options */
   if( SCIPdialoghdlrIsBufferEmpty(dialoghdlr) )
   {
      SCIPdialogMessage(scip, NULL, "\n");
      SCIP_CALL( SCIPdialogDisplayMenu(dialog, scip) );
      SCIPdialogMessage(scip, NULL, "\n");
   }

   SCIP_CALL( dialogExecMenu(scip, dialog, dialoghdlr, nextdialog) );

   return SCIP_OKAY;
}

/** standard menu dialog execution method, that doesn't display it's help screen */
SCIP_DECL_DIALOGEXEC(GCGdialogExecMenuLazy)
{  /*lint --e{715}*/
   SCIP_CALL( dialogExecMenu(scip, dialog, dialoghdlr, nextdialog) );

   return SCIP_OKAY;
}

/** dialog execution method for the checksol command */
SCIP_DECL_DIALOGEXEC(GCGdialogExecChecksol)
{  /*lint --e{715}*/
   SCIP_SOL* sol;
   SCIP_Bool feasible;

   SCIP_CALL( SCIPdialoghdlrAddHistory(dialoghdlr, dialog, NULL, FALSE) );

   SCIPdialogMessage(scip, NULL, "\n");
   if( SCIPgetStage(scip) >= SCIP_STAGE_TRANSFORMED )
      sol = SCIPgetBestSol(scip);
   else
      sol = NULL;

   if( sol == NULL )
      SCIPdialogMessage(scip, NULL, "no feasible solution available\n");
   else
   {
      SCIPmessagePrintInfo("check best solution\n");
      SCIP_CALL( SCIPcheckSolOrig(scip, sol, &feasible, TRUE, FALSE) );

      if( feasible )
         SCIPdialogMessage(scip, NULL, "solution is feasible in original problem\n");
   }
   SCIPdialogMessage(scip, NULL, "\n");

   *nextdialog = SCIPdialogGetParent(dialog);

   return SCIP_OKAY;
}

/** dialog execution method for the conflictgraph command */
SCIP_DECL_DIALOGEXEC(GCGdialogExecConflictgraph)
{  /*lint --e{715}*/
   SCIP_RETCODE retcode;
   SCIP_Bool endoffile;
   char* filename;

   assert(nextdialog != NULL);

   *nextdialog = NULL;

   if( !SCIPisTransformed(scip) )
   {
      SCIPdialogMessage(scip, NULL, "cannot call method before problem was transformed\n");
      SCIPdialoghdlrClearBuffer(dialoghdlr);
   }
   else
   {
      SCIP_CALL( SCIPdialoghdlrGetWord(dialoghdlr, dialog, "enter filename: ", &filename, &endoffile) );
      if( endoffile )
      {
         *nextdialog = NULL;
         return SCIP_OKAY;
      }

      if( filename[0] != '\0' )
      {
         SCIP_CALL( SCIPdialoghdlrAddHistory(dialoghdlr, dialog, filename, TRUE) );

         retcode = SCIPwriteImplicationConflictGraph(scip, filename);
         if( retcode == SCIP_FILECREATEERROR )
            SCIPdialogMessage(scip, NULL, "error writing file <%s>\n", filename);
         else
         {
            SCIP_CALL( retcode );
         }
      }
   }

   *nextdialog = SCIPdialoghdlrGetRoot(dialoghdlr);

   return SCIP_OKAY;
}

/** dialog execution method for the display branching command */
SCIP_DECL_DIALOGEXEC(GCGdialogExecDisplayBranching)
{  /*lint --e{715}*/
   SCIP_BRANCHRULE** branchrules;
   SCIP_BRANCHRULE** sorted;
   int nbranchrules;
   int i;

   SCIP_CALL( SCIPdialoghdlrAddHistory(dialoghdlr, dialog, NULL, FALSE) );

   branchrules = SCIPgetBranchrules(scip);
   nbranchrules = SCIPgetNBranchrules(scip);

   /* copy branchrules array into temporary memory for sorting */
   SCIP_CALL( SCIPduplicateBufferArray(scip, &sorted, branchrules, nbranchrules) );

   /* sort the branching rules */
   SCIPsortPtr((void**)sorted, SCIPbranchruleComp, nbranchrules);

   /* display sorted list of branching rules */
   SCIPdialogMessage(scip, NULL, "\n");
   SCIPdialogMessage(scip, NULL, " branching rule       priority maxdepth maxbddist  description\n");
   SCIPdialogMessage(scip, NULL, " --------------       -------- -------- ---------  -----------\n");
   for( i = 0; i < nbranchrules; ++i )
   {
      SCIPdialogMessage(scip, NULL, " %-20s ", SCIPbranchruleGetName(sorted[i]));
      if( strlen(SCIPbranchruleGetName(sorted[i])) > 20 )
         SCIPdialogMessage(scip, NULL, "\n %20s ", "-->");
      SCIPdialogMessage(scip, NULL, "%8d %8d %8.1f%%  ", SCIPbranchruleGetPriority(sorted[i]),
         SCIPbranchruleGetMaxdepth(sorted[i]), 100.0 * SCIPbranchruleGetMaxbounddist(sorted[i]));
      SCIPdialogMessage(scip, NULL, "%s", SCIPbranchruleGetDesc(sorted[i]));
      SCIPdialogMessage(scip, NULL, "\n");
   }
   SCIPdialogMessage(scip, NULL, "\n");

   /* free temporary memory */
   SCIPfreeBufferArray(scip, &sorted);

   *nextdialog = SCIPdialoghdlrGetRoot(dialoghdlr);

   return SCIP_OKAY;
}

/** dialog execution method for the display conflict command */
SCIP_DECL_DIALOGEXEC(GCGdialogExecDisplayConflict)
{  /*lint --e{715}*/
   SCIP_CONFLICTHDLR** conflicthdlrs;
   SCIP_CONFLICTHDLR** sorted;
   int nconflicthdlrs;
   int i;

   SCIP_CALL( SCIPdialoghdlrAddHistory(dialoghdlr, dialog, NULL, FALSE) );

   conflicthdlrs = SCIPgetConflicthdlrs(scip);
   nconflicthdlrs = SCIPgetNConflicthdlrs(scip);

   /* copy conflicthdlrs array into temporary memory for sorting */
   SCIP_CALL( SCIPduplicateBufferArray(scip, &sorted, conflicthdlrs, nconflicthdlrs) );

   /* sort the conflict handlers */
   SCIPsortPtr((void**)sorted, SCIPconflicthdlrComp, nconflicthdlrs);

   /* display sorted list of conflict handlers */
   SCIPdialogMessage(scip, NULL, "\n");
   SCIPdialogMessage(scip, NULL, " conflict handler     priority  description\n");
   SCIPdialogMessage(scip, NULL, " ----------------     --------  -----------\n");
   for( i = 0; i < nconflicthdlrs; ++i )
   {
      SCIPdialogMessage(scip, NULL, " %-20s ", SCIPconflicthdlrGetName(sorted[i]));
      if( strlen(SCIPconflicthdlrGetName(sorted[i])) > 20 )
         SCIPdialogMessage(scip, NULL, "\n %20s ", "-->");
      SCIPdialogMessage(scip, NULL, "%8d  ", SCIPconflicthdlrGetPriority(sorted[i]));
      SCIPdialogMessage(scip, NULL, "%s", SCIPconflicthdlrGetDesc(sorted[i]));
      SCIPdialogMessage(scip, NULL, "\n");
   }
   SCIPdialogMessage(scip, NULL, "\n");

   /* free temporary memory */
   SCIPfreeBufferArray(scip, &sorted);

   *nextdialog = SCIPdialoghdlrGetRoot(dialoghdlr);

   return SCIP_OKAY;
}

/** dialog execution method for the display conshdlrs command */
SCIP_DECL_DIALOGEXEC(GCGdialogExecDisplayConshdlrs)
{  /*lint --e{715}*/
   SCIP_CONSHDLR** conshdlrs;
   int nconshdlrs;
   int i;

   SCIP_CALL( SCIPdialoghdlrAddHistory(dialoghdlr, dialog, NULL, FALSE) );

   conshdlrs = SCIPgetConshdlrs(scip);
   nconshdlrs = SCIPgetNConshdlrs(scip);

   /* display list of constraint handlers */
   SCIPdialogMessage(scip, NULL, "\n");
   SCIPdialogMessage(scip, NULL, " constraint handler   chckprio enfoprio sepaprio sepaf propf eager  description\n");
   SCIPdialogMessage(scip, NULL, " ------------------   -------- -------- -------- ----- ----- -----  -----------\n");
   for( i = 0; i < nconshdlrs; ++i )
   {
      SCIPdialogMessage(scip, NULL, " %-20s ", SCIPconshdlrGetName(conshdlrs[i]));
      if( strlen(SCIPconshdlrGetName(conshdlrs[i])) > 20 )
         SCIPdialogMessage(scip, NULL, "\n %20s ", "-->");
      SCIPdialogMessage(scip, NULL, "%8d %8d %8d %5d %5d %5d  ",
         SCIPconshdlrGetCheckPriority(conshdlrs[i]),
         SCIPconshdlrGetEnfoPriority(conshdlrs[i]),
         SCIPconshdlrGetSepaPriority(conshdlrs[i]),
         SCIPconshdlrGetSepaFreq(conshdlrs[i]),
         SCIPconshdlrGetPropFreq(conshdlrs[i]),
         SCIPconshdlrGetEagerFreq(conshdlrs[i]));
      SCIPdialogMessage(scip, NULL, "%s", SCIPconshdlrGetDesc(conshdlrs[i]));
      SCIPdialogMessage(scip, NULL, "\n");
   }
   SCIPdialogMessage(scip, NULL, "\n");

   *nextdialog = SCIPdialoghdlrGetRoot(dialoghdlr);

   return SCIP_OKAY;
}

/** dialog execution method for the display displaycols command */
SCIP_DECL_DIALOGEXEC(GCGdialogExecDisplayDisplaycols)
{  /*lint --e{715}*/
   SCIP_DISP** disps;
   int ndisps;
   int i;

   SCIP_CALL( SCIPdialoghdlrAddHistory(dialoghdlr, dialog, NULL, FALSE) );

   disps = SCIPgetDisps(scip);
   ndisps = SCIPgetNDisps(scip);

   /* display list of display columns */
   SCIPdialogMessage(scip, NULL, "\n");
   SCIPdialogMessage(scip, NULL, " display column       header           position width priority status  description\n");
   SCIPdialogMessage(scip, NULL, " --------------       ------           -------- ----- -------- ------  -----------\n");
   for( i = 0; i < ndisps; ++i )
   {
      SCIPdialogMessage(scip, NULL, " %-20s ", SCIPdispGetName(disps[i]));
      if( strlen(SCIPdispGetName(disps[i])) > 20 )
         SCIPdialogMessage(scip, NULL, "\n %20s ", "-->");
      SCIPdialogMessage(scip, NULL, "%-16s ", SCIPdispGetHeader(disps[i]));
      if( strlen(SCIPdispGetHeader(disps[i])) > 16 )
         SCIPdialogMessage(scip, NULL, "\n %20s %16s ", "", "-->");
      SCIPdialogMessage(scip, NULL, "%8d ", SCIPdispGetPosition(disps[i]));
      SCIPdialogMessage(scip, NULL, "%5d ", SCIPdispGetWidth(disps[i]));
      SCIPdialogMessage(scip, NULL, "%8d ", SCIPdispGetPriority(disps[i]));
      switch( SCIPdispGetStatus(disps[i]) )
      {
      case SCIP_DISPSTATUS_OFF:
         SCIPdialogMessage(scip, NULL, "%6s  ", "off");
         break;
      case SCIP_DISPSTATUS_AUTO:
         SCIPdialogMessage(scip, NULL, "%6s  ", "auto");
         break;
      case SCIP_DISPSTATUS_ON:
         SCIPdialogMessage(scip, NULL, "%6s  ", "on");
         break;
      default:
         SCIPdialogMessage(scip, NULL, "%6s  ", "???");
         break;
      }
      SCIPdialogMessage(scip, NULL, "%s", SCIPdispGetDesc(disps[i]));
      SCIPdialogMessage(scip, NULL, "\n");
   }
   SCIPdialogMessage(scip, NULL, "\n");

   *nextdialog = SCIPdialoghdlrGetRoot(dialoghdlr);

   return SCIP_OKAY;
}

/** dialog execution method for the display heuristics command */
SCIP_DECL_DIALOGEXEC(GCGdialogExecDisplayHeuristics)
{  /*lint --e{715}*/
   SCIP_HEUR** heurs;
   int nheurs;
   int i;

   SCIP_CALL( SCIPdialoghdlrAddHistory(dialoghdlr, dialog, NULL, FALSE) );

   heurs = SCIPgetHeurs(scip);
   nheurs = SCIPgetNHeurs(scip);

   /* display list of primal heuristics */
   SCIPdialogMessage(scip, NULL, "\n");
   SCIPdialogMessage(scip, NULL, " primal heuristic     c priority freq ofs  description\n");
   SCIPdialogMessage(scip, NULL, " ----------------     - -------- ---- ---  -----------\n");
   for( i = 0; i < nheurs; ++i )
   {
      SCIPdialogMessage(scip, NULL, " %-20s ", SCIPheurGetName(heurs[i]));
      if( strlen(SCIPheurGetName(heurs[i])) > 20 )
         SCIPdialogMessage(scip, NULL, "\n %20s ", "-->");
      SCIPdialogMessage(scip, NULL, "%c ", SCIPheurGetDispchar(heurs[i]));
      SCIPdialogMessage(scip, NULL, "%8d ", SCIPheurGetPriority(heurs[i]));
      SCIPdialogMessage(scip, NULL, "%4d ", SCIPheurGetFreq(heurs[i]));
      SCIPdialogMessage(scip, NULL, "%3d  ", SCIPheurGetFreqofs(heurs[i]));
      SCIPdialogMessage(scip, NULL, "%s", SCIPheurGetDesc(heurs[i]));
      SCIPdialogMessage(scip, NULL, "\n");
   }
   SCIPdialogMessage(scip, NULL, "\n");

   *nextdialog = SCIPdialoghdlrGetRoot(dialoghdlr);

   return SCIP_OKAY;
}

/** dialog execution method for the display memory command */
SCIP_DECL_DIALOGEXEC(GCGdialogExecDisplayMemory)
{  /*lint --e{715}*/
   SCIP_CALL( SCIPdialoghdlrAddHistory(dialoghdlr, dialog, NULL, FALSE) );

   SCIPdialogMessage(scip, NULL, "\n");
   SCIPprintMemoryDiagnostic(scip);
   SCIPdialogMessage(scip, NULL, "\n");

   *nextdialog = SCIPdialoghdlrGetRoot(dialoghdlr);

   return SCIP_OKAY;
}

/** dialog execution method for the display nodeselectors command */
SCIP_DECL_DIALOGEXEC(GCGdialogExecDisplayNodeselectors)
{  /*lint --e{715}*/
   SCIP_NODESEL** nodesels;
   int nnodesels;
   int i;

   SCIP_CALL( SCIPdialoghdlrAddHistory(dialoghdlr, dialog, NULL, FALSE) );

   nodesels = SCIPgetNodesels(scip);
   nnodesels = SCIPgetNNodesels(scip);

   /* display list of node selectors */
   SCIPdialogMessage(scip, NULL, "\n");
   SCIPdialogMessage(scip, NULL, " node selector        std priority memsave prio  description\n");
   SCIPdialogMessage(scip, NULL, " -------------        ------------ ------------  -----------\n");
   for( i = 0; i < nnodesels; ++i )
   {
      SCIPdialogMessage(scip, NULL, " %-20s ", SCIPnodeselGetName(nodesels[i]));
      if( strlen(SCIPnodeselGetName(nodesels[i])) > 20 )
         SCIPdialogMessage(scip, NULL, "\n %20s ", "-->");
      SCIPdialogMessage(scip, NULL, "%12d ", SCIPnodeselGetStdPriority(nodesels[i]));
      SCIPdialogMessage(scip, NULL, "%12d  ", SCIPnodeselGetMemsavePriority(nodesels[i]));
      SCIPdialogMessage(scip, NULL, "%s", SCIPnodeselGetDesc(nodesels[i]));
      SCIPdialogMessage(scip, NULL, "\n");
   }
   SCIPdialogMessage(scip, NULL, "\n");

   *nextdialog = SCIPdialoghdlrGetRoot(dialoghdlr);

   return SCIP_OKAY;
}

/** dialog execution method for the display parameters command */
SCIP_DECL_DIALOGEXEC(GCGdialogExecDisplayParameters)
{  /*lint --e{715}*/
   SCIP_CALL( SCIPdialoghdlrAddHistory(dialoghdlr, dialog, NULL, FALSE) );

   SCIPdialogMessage(scip, NULL, "\n");
   SCIPdialogMessage(scip, NULL, "number of parameters = %d\n", SCIPgetNParams(scip));
   SCIPdialogMessage(scip, NULL, "non-default parameter settings:\n");
   SCIP_CALL( SCIPwriteParams(scip, NULL, FALSE, TRUE) );
   SCIPdialogMessage(scip, NULL, "\n");

   *nextdialog = SCIPdialoghdlrGetRoot(dialoghdlr);

   return SCIP_OKAY;
}

/** dialog execution method for the display presolvers command */
SCIP_DECL_DIALOGEXEC(GCGdialogExecDisplayPresolvers)
{  /*lint --e{715}*/
   SCIP_PRESOL** presols;
   int npresols;
   int i;

   SCIP_CALL( SCIPdialoghdlrAddHistory(dialoghdlr, dialog, NULL, FALSE) );

   presols = SCIPgetPresols(scip);
   npresols = SCIPgetNPresols(scip);

   /* display list of presolvers */
   SCIPdialogMessage(scip, NULL, "\n");
   SCIPdialogMessage(scip, NULL, " presolver            priority  description\n");
   SCIPdialogMessage(scip, NULL, " ---------            --------  -----------\n");
   for( i = 0; i < npresols; ++i )
   {
      SCIPdialogMessage(scip, NULL, " %-20s ", SCIPpresolGetName(presols[i]));
      if( strlen(SCIPpresolGetName(presols[i])) > 20 )
         SCIPdialogMessage(scip, NULL, "\n %20s ", "-->");
      SCIPdialogMessage(scip, NULL, "%8d%c ", SCIPpresolGetPriority(presols[i]),
         SCIPpresolIsDelayed(presols[i]) ? 'd' : ' ');
      SCIPdialogMessage(scip, NULL, "%s", SCIPpresolGetDesc(presols[i]));
      SCIPdialogMessage(scip, NULL, "\n");
   }
   SCIPdialogMessage(scip, NULL, "\n");

   *nextdialog = SCIPdialoghdlrGetRoot(dialoghdlr);

   return SCIP_OKAY;
}

/** dialog execution method for the display problem command */
SCIP_DECL_DIALOGEXEC(GCGdialogExecDisplayProblem)
{  /*lint --e{715}*/
   SCIP_CALL( SCIPdialoghdlrAddHistory(dialoghdlr, dialog, NULL, FALSE) );

   SCIPdialogMessage(scip, NULL, "\n");

   if( SCIPgetStage(scip) >= SCIP_STAGE_PROBLEM )
   {
      SCIP_CALL( SCIPprintOrigProblem(scip, NULL, "cip", FALSE) );
   }
   else
      SCIPdialogMessage(scip, NULL, "no problem available\n");

   SCIPdialogMessage(scip, NULL, "\n");

   *nextdialog = SCIPdialoghdlrGetRoot(dialoghdlr);

   return SCIP_OKAY;
}

/** dialog execution method for the display propagators command */
SCIP_DECL_DIALOGEXEC(GCGdialogExecDisplayPropagators)
{  /*lint --e{715}*/
   SCIP_PROP** props;
   int nprops;
   int i;

   SCIP_CALL( SCIPdialoghdlrAddHistory(dialoghdlr, dialog, NULL, FALSE) );

   props = SCIPgetProps(scip);
   nprops = SCIPgetNProps(scip);

   /* display list of propagators */
   SCIPdialogMessage(scip, NULL, "\n");
   SCIPdialogMessage(scip, NULL, " propagator           priority  freq  description\n");
   SCIPdialogMessage(scip, NULL, " ----------           --------  ----  -----------\n");
   for( i = 0; i < nprops; ++i )
   {
      SCIPdialogMessage(scip, NULL, " %-20s ", SCIPpropGetName(props[i]));
      if( strlen(SCIPpropGetName(props[i])) > 20 )
         SCIPdialogMessage(scip, NULL, "\n %20s ", "-->");
      SCIPdialogMessage(scip, NULL, "%8d%c ", SCIPpropGetPriority(props[i]), SCIPpropIsDelayed(props[i]) ? 'd' : ' ');
      SCIPdialogMessage(scip, NULL, "%4d  ", SCIPpropGetFreq(props[i]));
      SCIPdialogMessage(scip, NULL, "%s", SCIPpropGetDesc(props[i]));
      SCIPdialogMessage(scip, NULL, "\n");
   }
   SCIPdialogMessage(scip, NULL, "\n");

   *nextdialog = SCIPdialoghdlrGetRoot(dialoghdlr);

   return SCIP_OKAY;
}

/** dialog execution method for the display readers command */
SCIP_DECL_DIALOGEXEC(GCGdialogExecDisplayReaders)
{  /*lint --e{715}*/
   SCIP_CALL( SCIPdialoghdlrAddHistory(dialoghdlr, dialog, NULL, FALSE) );

   /* print reader information */
   displayReaders(scip, TRUE, TRUE);

   *nextdialog = SCIPdialoghdlrGetRoot(dialoghdlr);

   return SCIP_OKAY;
}

/** dialog execution method for the display separators command */
SCIP_DECL_DIALOGEXEC(GCGdialogExecDisplaySeparators)
{  /*lint --e{715}*/
   SCIP_SEPA** sepas;
   int nsepas;
   int i;

   SCIP_CALL( SCIPdialoghdlrAddHistory(dialoghdlr, dialog, NULL, FALSE) );

   sepas = SCIPgetSepas(scip);
   nsepas = SCIPgetNSepas(scip);

   /* display list of separators */
   SCIPdialogMessage(scip, NULL, "\n");
   SCIPdialogMessage(scip, NULL, " separator            priority  freq bddist  description\n");
   SCIPdialogMessage(scip, NULL, " ---------            --------  ---- ------  -----------\n");
   for( i = 0; i < nsepas; ++i )
   {
      SCIPdialogMessage(scip, NULL, " %-20s ", SCIPsepaGetName(sepas[i]));
      if( strlen(SCIPsepaGetName(sepas[i])) > 20 )
         SCIPdialogMessage(scip, NULL, "\n %20s ", "-->");
      SCIPdialogMessage(scip, NULL, "%8d%c ", SCIPsepaGetPriority(sepas[i]), SCIPsepaIsDelayed(sepas[i]) ? 'd' : ' ');
      SCIPdialogMessage(scip, NULL, "%4d ", SCIPsepaGetFreq(sepas[i]));
      SCIPdialogMessage(scip, NULL, "%6.2f  ", SCIPsepaGetMaxbounddist(sepas[i]));
      SCIPdialogMessage(scip, NULL, "%s", SCIPsepaGetDesc(sepas[i]));
      SCIPdialogMessage(scip, NULL, "\n");
   }
   SCIPdialogMessage(scip, NULL, "\n");

   *nextdialog = SCIPdialoghdlrGetRoot(dialoghdlr);

   return SCIP_OKAY;
}

/** dialog execution method for the display solution command */
SCIP_DECL_DIALOGEXEC(GCGdialogExecDisplaySolution)
{  /*lint --e{715}*/
   SCIP_CALL( SCIPdialoghdlrAddHistory(dialoghdlr, dialog, NULL, FALSE) );

   SCIPdialogMessage(scip, NULL, "\n");
   SCIP_CALL( SCIPprintBestSol(scip, NULL, FALSE) );
   SCIPdialogMessage(scip, NULL, "\n");

   *nextdialog = SCIPdialoghdlrGetRoot(dialoghdlr);

   return SCIP_OKAY;
}

/** dialog execution method for the display statistics command */
SCIP_DECL_DIALOGEXEC(GCGdialogExecDisplayStatistics)
{  /*lint --e{715}*/
   SCIP_CALL( SCIPdialoghdlrAddHistory(dialoghdlr, dialog, NULL, FALSE) );

   SCIPdialogMessage(scip, NULL, "\nMaster Program statistics:\n");
   SCIP_CALL( SCIPprintStatistics(GCGrelaxGetMasterprob(scip), NULL) );
   SCIPdialogMessage(scip, NULL, "\nOriginal Program statistics:\n");
   SCIP_CALL( SCIPprintStatistics(scip, NULL) );
   SCIPdialogMessage(scip, NULL, "\n");
   GCGpricerPrintStatistics(GCGrelaxGetMasterprob(scip), NULL);
   SCIPdialogMessage(scip, NULL, "\n");


   *nextdialog = SCIPdialoghdlrGetRoot(dialoghdlr);

   return SCIP_OKAY;
}

/** dialog execution method for the display statistics command */
SCIP_DECL_DIALOGEXEC(GCGdialogExecSetMaster)
{  /*lint --e{715}*/
   SCIP_CALL( SCIPdialoghdlrAddHistory(dialoghdlr, dialog, NULL, FALSE) );

   SCIPverbMessage(scip, SCIP_VERBLEVEL_DIALOG, NULL, "switching to the master problem...\n");
   SCIP_CALL( SCIPstartInteraction(GCGrelaxGetMasterprob(scip)) );
   SCIPverbMessage(scip, SCIP_VERBLEVEL_DIALOG, NULL, "back in the original problem...\n");

   *nextdialog = SCIPdialoghdlrGetRoot(dialoghdlr);

   return SCIP_OKAY;
}

/** dialog execution method for the detect command */
SCIP_DECL_DIALOGEXEC(GCGdialogExecDetect)
{  /*lint --e{715}*/
   long long int nnodes;

   SCIP_CALL( SCIPdialoghdlrAddHistory(dialoghdlr, dialog, NULL, FALSE) );

   SCIPverbMessage(scip, SCIP_VERBLEVEL_DIALOG, NULL, "Starting detection\n");
   SCIP_CALL( SCIPgetLongintParam(scip, "limits/nodes", &nnodes) );
   if( SCIPgetStage(scip) > SCIP_STAGE_INIT)
   {
      if(SCIPgetStage(scip) < SCIP_STAGE_PRESOLVED)
         SCIP_CALL( SCIPpresolve(scip) );
      SCIP_CALL( SCIPsetLongintParam(scip, "limits/nodes", 0) );
      SCIP_CALL( SCIPsolve(scip) );
   }
   else
      SCIPverbMessage(scip, SCIP_VERBLEVEL_DIALOG, NULL, "No problem exists");

   /*    SCIP_CALL( DECdetectStructure(scip) ); */

   SCIP_CALL( SCIPsetLongintParam(scip, "limits/nodes", nnodes) );

   *nextdialog = SCIPdialoghdlrGetRoot(dialoghdlr);

   return SCIP_OKAY;
}

/** dialog execution method for the display transproblem command */
SCIP_DECL_DIALOGEXEC(GCGdialogExecDisplayTransproblem)
{  /*lint --e{715}*/
   SCIP_CALL( SCIPdialoghdlrAddHistory(dialoghdlr, dialog, NULL, FALSE) );

   SCIPdialogMessage(scip, NULL, "\n");
   if(SCIPgetStage(scip) >= SCIP_STAGE_TRANSFORMED)
   {
      SCIP_CALL( SCIPprintTransProblem(scip, NULL, "cip", FALSE) );
   }
   else
      SCIPdialogMessage(scip, NULL, "no transformed problem available\n");

   SCIPdialogMessage(scip, NULL, "\n");

   *nextdialog = SCIPdialoghdlrGetRoot(dialoghdlr);

   return SCIP_OKAY;
}

/** dialog execution method for the display value command */
SCIP_DECL_DIALOGEXEC(GCGdialogExecDisplayValue)
{  /*lint --e{715}*/
   SCIP_SOL* sol;
   SCIP_VAR* var;
   char* varname;
   SCIP_Real solval;
   SCIP_Bool endoffile;

   SCIPdialogMessage(scip, NULL, "\n");

   if( SCIPgetStage(scip) >= SCIP_STAGE_TRANSFORMED )
      sol = SCIPgetBestSol(scip);
   else
      sol = NULL;

   if( sol == NULL )
   {
      SCIPdialogMessage(scip, NULL, "no feasible solution available\n");
      SCIPdialoghdlrClearBuffer(dialoghdlr);
   }
   else
   {
      SCIP_CALL( SCIPdialoghdlrGetWord(dialoghdlr, dialog, "enter variable name: ", &varname, &endoffile) );
      if( endoffile )
      {
         *nextdialog = NULL;
         return SCIP_OKAY;
      }

      if( varname[0] != '\0' )
      {
         SCIP_CALL( SCIPdialoghdlrAddHistory(dialoghdlr, dialog, varname, TRUE) );

         var = SCIPfindVar(scip, varname);
         if( var == NULL )
            SCIPdialogMessage(scip, NULL, "variable <%s> not found\n", varname);
         else
         {
            solval = SCIPgetSolVal(scip, sol, var);
            SCIPdialogMessage(scip, NULL, "%-32s", SCIPvarGetName(var));
            if( SCIPisInfinity(scip, solval) )
               SCIPdialogMessage(scip, NULL, " +infinity");
            else if( SCIPisInfinity(scip, -solval) )
               SCIPdialogMessage(scip, NULL, " -infinity");
            else
               SCIPdialogMessage(scip, NULL, " %20.15g", solval);
            SCIPdialogMessage(scip, NULL, " \t(obj:%.15g)\n", SCIPvarGetObj(var));
         }
      }
   }
   SCIPdialogMessage(scip, NULL, "\n");

   *nextdialog = SCIPdialoghdlrGetRoot(dialoghdlr);

   return SCIP_OKAY;
}

/** dialog execution method for the display varbranchstatistics command */
SCIP_DECL_DIALOGEXEC(GCGdialogExecDisplayVarbranchstatistics)
{  /*lint --e{715}*/
   SCIP_CALL( SCIPdialoghdlrAddHistory(dialoghdlr, dialog, NULL, FALSE) );

   SCIPdialogMessage(scip, NULL, "\n");
   SCIP_CALL( SCIPprintBranchingStatistics(scip, NULL) );
   SCIPdialogMessage(scip, NULL, "\n");

   *nextdialog = SCIPdialoghdlrGetRoot(dialoghdlr);

   return SCIP_OKAY;
}

/** dialog execution method for the help command */
SCIP_DECL_DIALOGEXEC(GCGdialogExecHelp)
{  /*lint --e{715}*/
   SCIP_CALL( SCIPdialoghdlrAddHistory(dialoghdlr, dialog, NULL, FALSE) );

   SCIPdialogMessage(scip, NULL, "\n");
   SCIP_CALL( SCIPdialogDisplayMenu(SCIPdialogGetParent(dialog), scip) );
   SCIPdialogMessage(scip, NULL, "\n");

   *nextdialog = SCIPdialogGetParent(dialog);

   return SCIP_OKAY;
}

/** dialog execution method for the display transsolution command */
SCIP_DECL_DIALOGEXEC(GCGdialogExecDisplayTranssolution)
{  /*lint --e{715}*/
   SCIP_CALL( SCIPdialoghdlrAddHistory(dialoghdlr, dialog, NULL, FALSE) );

   SCIPdialogMessage(scip, NULL, "\n");
   if( SCIPgetStage(scip) >= SCIP_STAGE_TRANSFORMED )
   {
      if( SCIPsolGetOrigin(SCIPgetBestSol(scip)) == SCIP_SOLORIGIN_ORIGINAL )
      {
         SCIPdialogMessage(scip, NULL, "best solution exists only in original problem space\n");
      }
      else
      {
         SCIP_CALL( SCIPprintBestTransSol(scip, NULL, FALSE) );
      }
   }
   else
      SCIPdialogMessage(scip, NULL, "no solution available\n");
   SCIPdialogMessage(scip, NULL, "\n");

   *nextdialog = SCIPdialoghdlrGetRoot(dialoghdlr);

   return SCIP_OKAY;
}

/** dialog execution method for the free command */
SCIP_DECL_DIALOGEXEC(GCGdialogExecFree)
{  /*lint --e{715}*/
   SCIP_CALL( SCIPdialoghdlrAddHistory(dialoghdlr, dialog, NULL, FALSE) );

   SCIP_CALL( SCIPfreeProb(scip) );

   *nextdialog = SCIPdialogGetParent(dialog);

   return SCIP_OKAY;
}

/** dialog execution method for the newstart command */
SCIP_DECL_DIALOGEXEC(GCGdialogExecNewstart)
{  /*lint --e{715}*/
   SCIP_CALL( SCIPdialoghdlrAddHistory(dialoghdlr, dialog, NULL, FALSE) );

   SCIP_CALL( SCIPfreeSolve(scip, TRUE) );

   *nextdialog = SCIPdialogGetParent(dialog);

   return SCIP_OKAY;
}

/** dialog execution method for the optimize command */
SCIP_DECL_DIALOGEXEC(GCGdialogExecOptimize)
{  /*lint --e{715}*/
   SCIP_CALL( SCIPdialoghdlrAddHistory(dialoghdlr, dialog, NULL, FALSE) );

   SCIPdialogMessage(scip, NULL, "\n");
   switch( SCIPgetStage(scip) )
   {
   case SCIP_STAGE_INIT:
      SCIPdialogMessage(scip, NULL, "no problem exists\n");
      break;

   case SCIP_STAGE_PROBLEM:
   case SCIP_STAGE_TRANSFORMED:
   case SCIP_STAGE_PRESOLVING:
   case SCIP_STAGE_PRESOLVED:
   case SCIP_STAGE_SOLVING:
      SCIP_CALL( SCIPsolve(scip) );
      break;

   case SCIP_STAGE_SOLVED:
      SCIPdialogMessage(scip, NULL, "problem is already solved\n");
      break;

   case SCIP_STAGE_TRANSFORMING:
   case SCIP_STAGE_INITSOLVE:
   case SCIP_STAGE_FREESOLVE:
   case SCIP_STAGE_FREETRANS:
   default:
      SCIPerrorMessage("invalid SCIP stage\n");
      return SCIP_INVALIDCALL;
   }
   SCIPdialogMessage(scip, NULL, "\n");

   *nextdialog = SCIPdialoghdlrGetRoot(dialoghdlr);

   return SCIP_OKAY;
}

/** dialog execution method for the presolve command */
SCIP_DECL_DIALOGEXEC(GCGdialogExecPresolve)
{  /*lint --e{715}*/
   SCIP_CALL( SCIPdialoghdlrAddHistory(dialoghdlr, dialog, NULL, FALSE) );

   SCIPdialogMessage(scip, NULL, "\n");
   switch( SCIPgetStage(scip) )
   {
   case SCIP_STAGE_INIT:
      SCIPdialogMessage(scip, NULL, "no problem exists\n");
      break;

   case SCIP_STAGE_PROBLEM:
   case SCIP_STAGE_TRANSFORMED:
   case SCIP_STAGE_PRESOLVING:
      SCIP_CALL( SCIPpresolve(scip) );
      break;

   case SCIP_STAGE_PRESOLVED:
   case SCIP_STAGE_SOLVING:
      SCIPdialogMessage(scip, NULL, "problem is already presolved\n");
      break;

   case SCIP_STAGE_SOLVED:
      SCIPdialogMessage(scip, NULL, "problem is already solved\n");
      break;

   case SCIP_STAGE_TRANSFORMING:
   case SCIP_STAGE_INITSOLVE:
   case SCIP_STAGE_FREESOLVE:
   case SCIP_STAGE_FREETRANS:
   default:
      SCIPerrorMessage("invalid SCIP stage\n");
      return SCIP_INVALIDCALL;
   }
   SCIPdialogMessage(scip, NULL, "\n");

   *nextdialog = SCIPdialoghdlrGetRoot(dialoghdlr);

   return SCIP_OKAY;
}

/** dialog execution method for the quit command */
SCIP_DECL_DIALOGEXEC(GCGdialogExecQuit)
{  /*lint --e{715}*/
   SCIPdialogMessage(scip, NULL, "\n");

   *nextdialog = NULL;

   return SCIP_OKAY;
}

/** dialog execution method for the read command */
SCIP_DECL_DIALOGEXEC(GCGdialogExecRead)
{  /*lint --e{715}*/
   SCIP_RETCODE retcode;
   char* filename;
   SCIP_Bool endoffile;

   SCIP_CALL( SCIPdialoghdlrGetWord(dialoghdlr, dialog, "enter filename: ", &filename, &endoffile) );
   if( endoffile )
   {
      *nextdialog = NULL;
      return SCIP_OKAY;
   }

   if( filename[0] != '\0' )
   {
      SCIP_CALL( SCIPdialoghdlrAddHistory(dialoghdlr, dialog, filename, TRUE) );

      if( SCIPfileExists(filename) )
      {
         char* tmpfilename;
         char* extension;

         /* copy filename */
         SCIP_CALL( SCIPduplicateBufferArray(scip, &tmpfilename, filename, (int)strlen(filename)+1) );
         extension = NULL;

         do
         {
            retcode = SCIPreadProb(scip, tmpfilename, extension);
            if( retcode == SCIP_READERROR || retcode == SCIP_NOFILE )
            {
               if( extension == NULL )
                  SCIPdialogMessage(scip, NULL, "error reading file <%s>\n", tmpfilename);
               else
                  SCIPdialogMessage(scip, NULL, "error reading file <%s> using <%s> file format\n",
                     tmpfilename, extension);

               SCIP_CALL( SCIPfreeProb(scip) );
               break;
            }
            else if( retcode == SCIP_PLUGINNOTFOUND )
            {
               /* ask user once for a suitable reader */
               if( extension == NULL )
               {
                  SCIPdialogMessage(scip, NULL, "no reader for input file <%s> available\n", tmpfilename);

                  SCIPdialogMessage(scip, NULL, "following readers are avaliable for reading:\n");
                  displayReaders(scip, TRUE, FALSE);

                  SCIP_CALL( SCIPdialoghdlrGetWord(dialoghdlr, dialog,
                        "select a suitable reader by extension (or return): ", &extension, &endoffile) );

                  if( extension[0] == '\0' )
                     break;
               }
               else
               {
                  SCIPdialogMessage(scip, NULL, "no reader for file extension <%s> available\n", extension);
                  extension = NULL;
               }
            }
            else
            {
               /* check if an unexpected error occurred during the reading process */
               SCIP_CALL( retcode );
               break;
            }
         }
         while (extension != NULL );

         /* free buffer array */
         SCIPfreeBufferArray(scip, &tmpfilename);
      }
      else
      {
         SCIPdialogMessage(scip, NULL, "file <%s> not found\n", filename);
         SCIPdialoghdlrClearBuffer(dialoghdlr);
      }
   }

   *nextdialog = SCIPdialoghdlrGetRoot(dialoghdlr);

   return SCIP_OKAY;
}

/** dialog execution method for the set default command */
SCIP_DECL_DIALOGEXEC(GCGdialogExecSetDefault)
{  /*lint --e{715}*/
   SCIP_CALL( SCIPdialoghdlrAddHistory(dialoghdlr, dialog, NULL, FALSE) );

   SCIP_CALL( SCIPresetParams(scip) );
   SCIPdialogMessage(scip, NULL, "reset parameters to their default values\n");

   *nextdialog = SCIPdialoghdlrGetRoot(dialoghdlr);

   return SCIP_OKAY;
}

/** dialog execution method for the set load command */
SCIP_DECL_DIALOGEXEC(GCGdialogExecSetLoad)
{  /*lint --e{715}*/
   char* filename;
   SCIP_Bool endoffile;

   SCIP_CALL( SCIPdialoghdlrGetWord(dialoghdlr, dialog, "enter filename: ", &filename, &endoffile) );
   if( endoffile )
   {
      *nextdialog = NULL;
      return SCIP_OKAY;
   }

   if( filename[0] != '\0' )
   {
      SCIP_CALL( SCIPdialoghdlrAddHistory(dialoghdlr, dialog, filename, TRUE) );

      if( SCIPfileExists(filename) )
      {
         SCIP_CALL( SCIPreadParams(scip, filename) );
         SCIPdialogMessage(scip, NULL, "loaded parameter file <%s>\n", filename);
      }
      else
      {
         SCIPdialogMessage(scip, NULL, "file <%s> not found\n", filename);
         SCIPdialoghdlrClearBuffer(dialoghdlr);
      }
   }

   *nextdialog = SCIPdialoghdlrGetRoot(dialoghdlr);

   return SCIP_OKAY;
}

/** dialog execution method for the set save command */
SCIP_DECL_DIALOGEXEC(GCGdialogExecSetSave)
{  /*lint --e{715}*/
   char* filename;
   SCIP_Bool endoffile;

   SCIP_CALL( SCIPdialoghdlrGetWord(dialoghdlr, dialog, "enter filename: ", &filename, &endoffile) );
   if( endoffile )
   {
      *nextdialog = NULL;
      return SCIP_OKAY;
   }

   if( filename[0] != '\0' )
   {
      SCIP_CALL( SCIPdialoghdlrAddHistory(dialoghdlr, dialog, filename, TRUE) );

      SCIP_CALL( SCIPwriteParams(scip, filename, TRUE, FALSE) );
      SCIPdialogMessage(scip, NULL, "saved parameter file <%s>\n", filename);
   }

   *nextdialog = SCIPdialoghdlrGetRoot(dialoghdlr);

   return SCIP_OKAY;
}

/** dialog execution method for the set diffsave command */
SCIP_DECL_DIALOGEXEC(GCGdialogExecSetDiffsave)
{  /*lint --e{715}*/
   char* filename;
   SCIP_Bool endoffile;

   SCIP_CALL( SCIPdialoghdlrGetWord(dialoghdlr, dialog, "enter filename: ", &filename, &endoffile) );
   if( endoffile )
   {
      *nextdialog = NULL;
      return SCIP_OKAY;
   }

   if( filename[0] != '\0' )
   {
      SCIP_CALL( SCIPdialoghdlrAddHistory(dialoghdlr, dialog, filename, TRUE) );

      SCIP_CALL( SCIPwriteParams(scip, filename, TRUE, TRUE) );
      SCIPdialogMessage(scip, NULL, "saved non-default parameter settings to file <%s>\n", filename);
   }

   *nextdialog = SCIPdialoghdlrGetRoot(dialoghdlr);

   return SCIP_OKAY;
}

/** dialog execution method for the set parameter command */
SCIP_DECL_DIALOGEXEC(GCGdialogExecSetParam)
{  /*lint --e{715}*/
   SCIP_RETCODE retcode;
   SCIP_PARAM* param;
   char prompt[SCIP_MAXSTRLEN];
   char* valuestr;
   SCIP_Bool boolval;
   int intval;
   SCIP_Longint longintval;
   SCIP_Real realval;
   char charval;
   SCIP_Bool endoffile;
   SCIP_Bool error;

   *nextdialog = SCIPdialoghdlrGetRoot(dialoghdlr);

   /* get the parameter to set */
   param = (SCIP_PARAM*)SCIPdialogGetData(dialog);

   /* depending on the parameter type, request a user input */
   switch( SCIPparamGetType(param) )
   {
   case SCIP_PARAMTYPE_BOOL:
      (void) SCIPsnprintf(prompt, SCIP_MAXSTRLEN, "current value: %s, new value (TRUE/FALSE): ",
         SCIPparamGetBool(param) ? "TRUE" : "FALSE");
      SCIP_CALL( SCIPdialoghdlrGetWord(dialoghdlr, dialog, prompt, &valuestr, &endoffile) );
      if( endoffile )
      {
         *nextdialog = NULL;
         return SCIP_OKAY;
      }
      if( valuestr[0] == '\0' )
         return SCIP_OKAY;

      boolval = parseBoolValue(scip, valuestr, &error);

      if( !error )
      {
         SCIP_CALL( SCIPparamSetBool(param, scip, boolval, FALSE) );
         SCIPdialogMessage(scip, NULL, "parameter <%s> set to %s\n", SCIPparamGetName(param), boolval ? "TRUE" : "FALSE");
         SCIP_CALL( SCIPdialoghdlrAddHistory(dialoghdlr, dialog, boolval ? "TRUE" : "FALSE", TRUE) );
      }

      break;

   case SCIP_PARAMTYPE_INT:
      (void) SCIPsnprintf(prompt, SCIP_MAXSTRLEN, "current value: %d, new value [%d,%d]: ",
         SCIPparamGetInt(param), SCIPparamGetIntMin(param), SCIPparamGetIntMax(param));
      SCIP_CALL( SCIPdialoghdlrGetWord(dialoghdlr, dialog, prompt, &valuestr, &endoffile) );
      if( endoffile )
      {
         *nextdialog = NULL;
         return SCIP_OKAY;
      }
      if( valuestr[0] == '\0' )
         return SCIP_OKAY;

      SCIP_CALL( SCIPdialoghdlrAddHistory(dialoghdlr, dialog, valuestr, TRUE) );

      if( sscanf(valuestr, "%d", &intval) != 1 )
      {
         SCIPdialogMessage(scip, NULL, "\ninvalid input <%s>\n\n", valuestr);
         return SCIP_OKAY;
      }
      retcode = SCIPparamSetInt(param, scip, intval, FALSE);
      if( retcode != SCIP_PARAMETERWRONGVAL )
      {
         SCIP_CALL( retcode );
      }
      SCIPdialogMessage(scip, NULL, "parameter <%s> set to %d\n", SCIPparamGetName(param), SCIPparamGetInt(param));
      break;

   case SCIP_PARAMTYPE_LONGINT:
      (void) SCIPsnprintf(prompt, SCIP_MAXSTRLEN, "current value: %"SCIP_LONGINT_FORMAT", new value [%"SCIP_LONGINT_FORMAT",%"SCIP_LONGINT_FORMAT"]: ",
         SCIPparamGetLongint(param), SCIPparamGetLongintMin(param), SCIPparamGetLongintMax(param));
      SCIP_CALL( SCIPdialoghdlrGetWord(dialoghdlr, dialog, prompt, &valuestr, &endoffile) );
      if( endoffile )
      {
         *nextdialog = NULL;
         return SCIP_OKAY;
      }
      if( valuestr[0] == '\0' )
         return SCIP_OKAY;

      SCIP_CALL( SCIPdialoghdlrAddHistory(dialoghdlr, dialog, valuestr, TRUE) );

      if( sscanf(valuestr, "%"SCIP_LONGINT_FORMAT, &longintval) != 1 )
      {
         SCIPdialogMessage(scip, NULL, "\ninvalid input <%s>\n\n", valuestr);
         return SCIP_OKAY;
      }
      retcode = SCIPparamSetLongint(param, scip, longintval, FALSE);
      if( retcode != SCIP_PARAMETERWRONGVAL )
      {
         SCIP_CALL( retcode );
      }
      SCIPdialogMessage(scip, NULL, "parameter <%s> set to %"SCIP_LONGINT_FORMAT"\n", SCIPparamGetName(param), SCIPparamGetLongint(param));
      break;

   case SCIP_PARAMTYPE_REAL:
      (void) SCIPsnprintf(prompt, SCIP_MAXSTRLEN, "current value: %.15g, new value [%.15g,%.15g]: ",
         SCIPparamGetReal(param), SCIPparamGetRealMin(param), SCIPparamGetRealMax(param));
      SCIP_CALL( SCIPdialoghdlrGetWord(dialoghdlr, dialog, prompt, &valuestr, &endoffile) );
      if( endoffile )
      {
         *nextdialog = NULL;
         return SCIP_OKAY;
      }
      if( valuestr[0] == '\0' )
         return SCIP_OKAY;

      SCIP_CALL( SCIPdialoghdlrAddHistory(dialoghdlr, dialog, valuestr, TRUE) );

      if( sscanf(valuestr, "%"SCIP_REAL_FORMAT, &realval) != 1 )
      {
         SCIPdialogMessage(scip, NULL, "\ninvalid input <%s>\n\n", valuestr);
         return SCIP_OKAY;
      }
      retcode = SCIPparamSetReal(param, scip, realval, FALSE);
      if( retcode != SCIP_PARAMETERWRONGVAL )
      {
         SCIP_CALL( retcode );
      }
      SCIPdialogMessage(scip, NULL, "parameter <%s> set to %.15g\n", SCIPparamGetName(param), SCIPparamGetReal(param));
      break;

   case SCIP_PARAMTYPE_CHAR:
      (void) SCIPsnprintf(prompt, SCIP_MAXSTRLEN, "current value: <%c>, new value: ", SCIPparamGetChar(param));
      SCIP_CALL( SCIPdialoghdlrGetWord(dialoghdlr, dialog, prompt, &valuestr, &endoffile) );
      if( endoffile )
      {
         *nextdialog = NULL;
         return SCIP_OKAY;
      }
      if( valuestr[0] == '\0' )
         return SCIP_OKAY;

      SCIP_CALL( SCIPdialoghdlrAddHistory(dialoghdlr, dialog, valuestr, TRUE) );

      if( sscanf(valuestr, "%c", &charval) != 1 )
      {
         SCIPdialogMessage(scip, NULL, "\ninvalid input <%s>\n\n", valuestr);
         return SCIP_OKAY;
      }
      retcode = SCIPparamSetChar(param, scip, charval, FALSE);
      if( retcode != SCIP_PARAMETERWRONGVAL )
      {
         SCIP_CALL( retcode );
      }
      SCIPdialogMessage(scip, NULL, "parameter <%s> set to <%c>\n", SCIPparamGetName(param), SCIPparamGetChar(param));
      break;

   case SCIP_PARAMTYPE_STRING:
      (void) SCIPsnprintf(prompt, SCIP_MAXSTRLEN, "current value: <%s>, new value: ", SCIPparamGetString(param));
      SCIP_CALL( SCIPdialoghdlrGetWord(dialoghdlr, dialog, prompt, &valuestr, &endoffile) );
      if( endoffile )
      {
         *nextdialog = NULL;
         return SCIP_OKAY;
      }
      if( valuestr[0] == '\0' )
         return SCIP_OKAY;

      SCIP_CALL( SCIPdialoghdlrAddHistory(dialoghdlr, dialog, valuestr, TRUE) );

      retcode = SCIPparamSetString(param, scip, valuestr, FALSE);
      if( retcode != SCIP_PARAMETERWRONGVAL )
      {
         SCIP_CALL( retcode );
      }
      SCIPdialogMessage(scip, NULL, "parameter <%s> set to <%s>\n", SCIPparamGetName(param), SCIPparamGetString(param));
      break;

   default:
      SCIPerrorMessage("invalid parameter type\n");
      return SCIP_INVALIDDATA;
   }

   return SCIP_OKAY;
}

/** dialog description method for the set parameter command */
SCIP_DECL_DIALOGDESC(GCGdialogDescSetParam)
{  /*lint --e{715}*/
   SCIP_PARAM* param;
   char valuestr[SCIP_MAXSTRLEN];

   /* get the parameter to set */
   param = (SCIP_PARAM*)SCIPdialogGetData(dialog);

   /* retrieve parameter's current value */
   switch( SCIPparamGetType(param) )
   {
   case SCIP_PARAMTYPE_BOOL:
      if( SCIPparamGetBool(param) )
         (void) SCIPsnprintf(valuestr, SCIP_MAXSTRLEN, "TRUE");
      else
         (void) SCIPsnprintf(valuestr, SCIP_MAXSTRLEN, "FALSE");
      break;

   case SCIP_PARAMTYPE_INT:
      (void) SCIPsnprintf(valuestr, SCIP_MAXSTRLEN, "%d", SCIPparamGetInt(param));
      break;

   case SCIP_PARAMTYPE_LONGINT:
      (void) SCIPsnprintf(valuestr, SCIP_MAXSTRLEN, "%"SCIP_LONGINT_FORMAT, SCIPparamGetLongint(param));
      break;

   case SCIP_PARAMTYPE_REAL:
      (void) SCIPsnprintf(valuestr, SCIP_MAXSTRLEN, "%.15g", SCIPparamGetReal(param));
      if( strchr(valuestr, '.') == NULL && strchr(valuestr, 'e') == NULL )
         (void) SCIPsnprintf(valuestr, SCIP_MAXSTRLEN, "%.1f", SCIPparamGetReal(param));
      break;

   case SCIP_PARAMTYPE_CHAR:
      (void) SCIPsnprintf(valuestr, SCIP_MAXSTRLEN, "%c", SCIPparamGetChar(param));
      break;

   case SCIP_PARAMTYPE_STRING:
      (void) SCIPsnprintf(valuestr, SCIP_MAXSTRLEN, "%s", SCIPparamGetString(param));
      break;

   default:
      SCIPerrorMessage("invalid parameter type\n");
      return SCIP_INVALIDDATA;
   }
   valuestr[SCIP_MAXSTRLEN-1] = '\0';

   /* display parameter's description */
   SCIPdialogMessage(scip, NULL, "%s", SCIPparamGetDesc(param));

   /* display parameter's current value */
   SCIPdialogMessage(scip, NULL, " [%s]", valuestr);

   return SCIP_OKAY;
}

/** dialog execution method for the set branching direction command */
SCIP_DECL_DIALOGEXEC(GCGdialogExecSetBranchingDirection)
{  /*lint --e{715}*/
   SCIP_VAR* var;
   char prompt[SCIP_MAXSTRLEN];
   char* valuestr;
   int direction;
   SCIP_Bool endoffile;

   *nextdialog = SCIPdialoghdlrGetRoot(dialoghdlr);

   /* branching priorities cannot be set, if no problem was created */
   if( SCIPgetStage(scip) == SCIP_STAGE_INIT )
   {
      SCIPdialogMessage(scip, NULL, "cannot set branching directions before problem was created\n");
      return SCIP_OKAY;
   }

   /* get variable name from user */
   SCIP_CALL( SCIPdialoghdlrGetWord(dialoghdlr, dialog, "variable name: ", &valuestr, &endoffile) );
   if( endoffile )
   {
      *nextdialog = NULL;
      return SCIP_OKAY;
   }
   if( valuestr[0] == '\0' )
      return SCIP_OKAY;

   /* find variable */
   var = SCIPfindVar(scip, valuestr);
   if( var == NULL )
   {
      SCIPdialogMessage(scip, NULL, "variable <%s> does not exist in problem\n", valuestr);
      return SCIP_OKAY;
   }

   /* get new branching direction from user */
   switch( SCIPvarGetBranchDirection(var) )
   {
   case SCIP_BRANCHDIR_DOWNWARDS:
      direction = -1;
      break;
   case SCIP_BRANCHDIR_AUTO:
      direction = 0;
      break;
   case SCIP_BRANCHDIR_UPWARDS:
      direction = +1;
      break;
   default:
      SCIPerrorMessage("invalid preferred branching direction <%d> of variable <%s>\n",
         SCIPvarGetBranchDirection(var), SCIPvarGetName(var));
      return SCIP_INVALIDDATA;
   }
   (void) SCIPsnprintf(prompt, SCIP_MAXSTRLEN, "current value: %d, new value: ", direction);
   SCIP_CALL( SCIPdialoghdlrGetWord(dialoghdlr, dialog, prompt, &valuestr, &endoffile) );
   if( endoffile )
   {
      *nextdialog = NULL;
      return SCIP_OKAY;
   }
   SCIPescapeString(prompt, SCIP_MAXSTRLEN, SCIPvarGetName(var));
   (void) SCIPsnprintf(prompt, SCIP_MAXSTRLEN, "%s %s", prompt, valuestr);
   if( valuestr[0] == '\0' )
      return SCIP_OKAY;

   SCIP_CALL( SCIPdialoghdlrAddHistory(dialoghdlr, dialog, prompt, FALSE) );

   if( sscanf(valuestr, "%d", &direction) != 1 )
   {
      SCIPdialogMessage(scip, NULL, "\ninvalid input <%s>\n\n", valuestr);
      return SCIP_OKAY;
   }
   if( direction < -1 || direction > +1 )
   {
      SCIPdialogMessage(scip, NULL, "\ninvalid input <%d>: direction must be -1, 0, or +1\n\n", direction);
      return SCIP_OKAY;
   }

   /* set new branching direction */
   if( direction == -1 )
      SCIP_CALL( SCIPchgVarBranchDirection(scip, var, SCIP_BRANCHDIR_DOWNWARDS) );
   else if( direction == 0 )
      SCIP_CALL( SCIPchgVarBranchDirection(scip, var, SCIP_BRANCHDIR_AUTO) );
   else
      SCIP_CALL( SCIPchgVarBranchDirection(scip, var, SCIP_BRANCHDIR_UPWARDS) );

   SCIPdialogMessage(scip, NULL, "branching direction of variable <%s> set to %d\n", SCIPvarGetName(var), direction);

   return SCIP_OKAY;
}


/** dialog execution method for the set branching priority command */
SCIP_DECL_DIALOGEXEC(GCGdialogExecSetBranchingPriority)
{  /*lint --e{715}*/
   SCIP_VAR* var;
   char prompt[SCIP_MAXSTRLEN];
   char* valuestr;
   int priority;
   SCIP_Bool endoffile;

   *nextdialog = SCIPdialoghdlrGetRoot(dialoghdlr);

   /* branching priorities cannot be set, if no problem was created */
   if( SCIPgetStage(scip) == SCIP_STAGE_INIT )
   {
      SCIPdialogMessage(scip, NULL, "cannot set branching priorities before problem was created\n");
      return SCIP_OKAY;
   }

   /* get variable name from user */
   SCIP_CALL( SCIPdialoghdlrGetWord(dialoghdlr, dialog, "variable name: ", &valuestr, &endoffile) );
   if( endoffile )
   {
      *nextdialog = NULL;
      return SCIP_OKAY;
   }
   if( valuestr[0] == '\0' )
      return SCIP_OKAY;

   /* find variable */
   var = SCIPfindVar(scip, valuestr);
   if( var == NULL )
   {
      SCIPdialogMessage(scip, NULL, "variable <%s> does not exist in problem\n", valuestr);
      return SCIP_OKAY;
   }

   /* get new branching priority from user */
   (void) SCIPsnprintf(prompt, SCIP_MAXSTRLEN, "current value: %d, new value: ", SCIPvarGetBranchPriority(var));
   SCIP_CALL( SCIPdialoghdlrGetWord(dialoghdlr, dialog, prompt, &valuestr, &endoffile) );
   if( endoffile )
   {
      *nextdialog = NULL;
      return SCIP_OKAY;
   }
   SCIPescapeString(prompt, SCIP_MAXSTRLEN, SCIPvarGetName(var));
   (void) SCIPsnprintf(prompt, SCIP_MAXSTRLEN, "%s %s", prompt, valuestr);
   if( valuestr[0] == '\0' )
      return SCIP_OKAY;

   SCIP_CALL( SCIPdialoghdlrAddHistory(dialoghdlr, dialog, prompt, FALSE) );

   if( sscanf(valuestr, "%d", &priority) != 1 )
   {
      SCIPdialogMessage(scip, NULL, "\ninvalid input <%s>\n\n", valuestr);
      return SCIP_OKAY;
   }

   /* set new branching priority */
   SCIP_CALL( SCIPchgVarBranchPriority(scip, var, priority) );
   SCIPdialogMessage(scip, NULL, "branching priority of variable <%s> set to %d\n", SCIPvarGetName(var), SCIPvarGetBranchPriority(var));

   return SCIP_OKAY;
}

/** dialog execution method for the set heuristics aggressive command */
SCIP_DECL_DIALOGEXEC(GCGdialogExecSetHeuristicsEmphasisAggressive)
{  /*lint --e{715}*/
   SCIP_CALL( SCIPdialoghdlrAddHistory(dialoghdlr, dialog, NULL, FALSE) );

   *nextdialog = SCIPdialoghdlrGetRoot(dialoghdlr);

   SCIP_CALL( SCIPsetHeuristics(scip, SCIP_PARAMSETTING_AGGRESSIVE, FALSE) );

   return SCIP_OKAY;
}

/** dialog execution method for the set heuristics fast command */
SCIP_DECL_DIALOGEXEC(GCGdialogExecSetHeuristicsEmphasisFast)
{  /*lint --e{715}*/
   SCIP_CALL( SCIPdialoghdlrAddHistory(dialoghdlr, dialog, NULL, FALSE) );

   *nextdialog = SCIPdialoghdlrGetRoot(dialoghdlr);

   SCIP_CALL( SCIPsetHeuristics(scip, SCIP_PARAMSETTING_FAST, FALSE) );

   return SCIP_OKAY;
}

/** dialog execution method for the set heuristics off command */
SCIP_DECL_DIALOGEXEC(GCGdialogExecSetHeuristicsEmphasisOff)
{  /*lint --e{715}*/
   SCIP_CALL( SCIPdialoghdlrAddHistory(dialoghdlr, dialog, NULL, FALSE) );

   *nextdialog = SCIPdialoghdlrGetRoot(dialoghdlr);

   SCIP_CALL( SCIPsetHeuristics(scip, SCIP_PARAMSETTING_OFF, FALSE) );

   return SCIP_OKAY;
}

/** dialog execution method for the set limits objective command */
SCIP_DECL_DIALOGEXEC(GCGdialogExecSetLimitsObjective)
{  /*lint --e{715}*/
   char prompt[SCIP_MAXSTRLEN];
   char* valuestr;
   SCIP_Real objlim;
   SCIP_Bool endoffile;

   *nextdialog = SCIPdialoghdlrGetRoot(dialoghdlr);

   /* objective limit cannot be set, if no problem was created */
   if( SCIPgetStage(scip) == SCIP_STAGE_INIT )
   {
      SCIPdialogMessage(scip, NULL, "cannot set objective limit before problem was created\n");
      return SCIP_OKAY;
   }

   /* get new objective limit from user */
   (void) SCIPsnprintf(prompt, SCIP_MAXSTRLEN, "current value: %.15g, new value: ", SCIPgetObjlimit(scip));
   SCIP_CALL( SCIPdialoghdlrGetWord(dialoghdlr, dialog, prompt, &valuestr, &endoffile) );
   if( endoffile )
   {
      *nextdialog = NULL;
      return SCIP_OKAY;
   }
   if( valuestr[0] == '\0' )
      return SCIP_OKAY;

   SCIP_CALL( SCIPdialoghdlrAddHistory(dialoghdlr, dialog, valuestr, TRUE) );

   if( sscanf(valuestr, "%"SCIP_REAL_FORMAT, &objlim) != 1 )
   {
      SCIPdialogMessage(scip, NULL, "\ninvalid input <%s>\n\n", valuestr);
      return SCIP_OKAY;
   }

   /* check, if new objective limit is valid */
   if( SCIPgetStage(scip) > SCIP_STAGE_PROBLEM
      && SCIPtransformObj(scip, objlim) > SCIPtransformObj(scip, SCIPgetObjlimit(scip)) )
   {
      SCIPdialogMessage(scip, NULL, "\ncannot relax objective limit from %.15g to %.15g after problem was transformed\n\n",
         SCIPgetObjlimit(scip), objlim);
      return SCIP_OKAY;
   }

   /* set new objective limit */
   SCIP_CALL( SCIPsetObjlimit(scip, objlim) );
   SCIPdialogMessage(scip, NULL, "objective value limit set to %.15g\n", SCIPgetObjlimit(scip));

   return SCIP_OKAY;
}

/** dialog execution method for the write lp command */
static
SCIP_DECL_DIALOGEXEC(GCGdialogExecWriteLp)
{  /*lint --e{715}*/
   char* filename;
   SCIP_Bool endoffile;

   SCIPdialogMessage(scip, NULL, "\n");

   /* node relaxations only exist in solving & solved stage */
   if( SCIPgetStage(scip) < SCIP_STAGE_SOLVING )
   {
      SCIPdialogMessage(scip, NULL, "There is no node LP relaxation before solving starts\n");
      *nextdialog = SCIPdialoghdlrGetRoot(dialoghdlr);
      return SCIP_OKAY;
   }
   if( SCIPgetStage(scip) >= SCIP_STAGE_SOLVED )
   {
      SCIPdialogMessage(scip, NULL, "There is no node LP relaxation after problem was solved\n");
      *nextdialog = SCIPdialoghdlrGetRoot(dialoghdlr);
      return SCIP_OKAY;
   }

   SCIP_CALL( SCIPdialoghdlrGetWord(dialoghdlr, dialog, "enter filename: ", &filename, &endoffile) );
   if( endoffile )
   {
      *nextdialog = SCIPdialoghdlrGetRoot(dialoghdlr);
      return SCIP_OKAY;
   }
   if( filename[0] != '\0' )
   {
      SCIP_CALL( SCIPdialoghdlrAddHistory(dialoghdlr, dialog, filename, TRUE) );
      SCIP_CALL( SCIPwriteLP(scip, filename) );
      SCIPdialogMessage(scip, NULL, "written node LP relaxation to file <%s>\n", filename);
   }

   SCIPdialogMessage(scip, NULL, "\n");

   *nextdialog = SCIPdialoghdlrGetRoot(dialoghdlr);

   return SCIP_OKAY;
}

/** dialog execution method for the write mip command */
static
SCIP_DECL_DIALOGEXEC(GCGdialogExecWriteMip)
{  /*lint --e{715}*/
   char command[SCIP_MAXSTRLEN];
   char filename[SCIP_MAXSTRLEN];
   SCIP_Bool endoffile;
   char* valuestr;
   SCIP_Bool offset;
   SCIP_Bool generic;
   SCIP_Bool error;

   *nextdialog = SCIPdialoghdlrGetRoot(dialoghdlr);

   /* node relaxations only exist in solving & solved stage */
   if( SCIPgetStage(scip) < SCIP_STAGE_SOLVING )
   {
      SCIPdialogMessage(scip, NULL, "There is no node MIP relaxation before solving starts\n");
      return SCIP_OKAY;
   }
   if( SCIPgetStage(scip) >= SCIP_STAGE_SOLVED )
   {
      SCIPdialogMessage(scip, NULL, "There is no node MIP relaxation after problem was solved\n");
      return SCIP_OKAY;
   }

   /* first get file name */
   SCIP_CALL( SCIPdialoghdlrGetWord(dialoghdlr, dialog, "enter filename: ", &valuestr, &endoffile) );
   if( endoffile )
   {
      *nextdialog = NULL;
      return SCIP_OKAY;
   }
   if( valuestr[0] == '\0' )
      return SCIP_OKAY;

   (void)strncpy(filename, valuestr, SCIP_MAXSTRLEN-1);

   /* second ask for generic variable and row names */
   SCIP_CALL( SCIPdialoghdlrGetWord(dialoghdlr, dialog,
         "using generic variable and row names (TRUE/FALSE): ",
         &valuestr, &endoffile) );

   if( endoffile )
   {
      *nextdialog = NULL;
      return SCIP_OKAY;
   }
   if( valuestr[0] == '\0' )
      return SCIP_OKAY;

   generic = parseBoolValue(scip, valuestr, &error);

   if( error )
      return SCIP_OKAY;

   /* adjust command and add to the history */
   SCIPescapeString(command, SCIP_MAXSTRLEN, filename);
   (void) SCIPsnprintf(command, SCIP_MAXSTRLEN, "%s %s", command, generic ? "TRUE" : "FALSE");

   /* third ask if for adjusting the objective offset */
   SCIP_CALL( SCIPdialoghdlrGetWord(dialoghdlr, dialog,
         "using original objective function (TRUE/FALSE): ",
         &valuestr, &endoffile) );

   if( endoffile )
   {
      *nextdialog = NULL;
      return SCIP_OKAY;
   }
   if( valuestr[0] == '\0' )
      return SCIP_OKAY;

   offset = parseBoolValue(scip, valuestr, &error);

   if( error )
      return SCIP_OKAY;

   (void) SCIPsnprintf(command, SCIP_MAXSTRLEN, "%s %s", command, offset ? "TRUE" : "FALSE");
   SCIP_CALL( SCIPdialoghdlrAddHistory(dialoghdlr, dialog, command, FALSE) );

   /* execute command */
   SCIP_CALL( SCIPwriteMIP(scip, filename, generic, offset) );
   SCIPdialogMessage(scip, NULL, "written node MIP relaxation to file <%s>\n", filename);

   SCIPdialogMessage(scip, NULL, "\n");

   return SCIP_OKAY;
}

/** dialog execution method for the write problem command */
static
SCIP_DECL_DIALOGEXEC(GCGdialogExecWriteProblem)
{  /*lint --e{715}*/
   SCIP_CALL( SCIPdialoghdlrAddHistory(dialoghdlr, dialog, NULL, FALSE) );

   if( SCIPgetStage(scip) >= SCIP_STAGE_PROBLEM )
   {
      SCIP_CALL( writeProblem(scip, dialog, dialoghdlr, nextdialog, FALSE, FALSE) );
   }
   else
      SCIPdialogMessage(scip, NULL, "no problem available\n");

   *nextdialog = SCIPdialoghdlrGetRoot(dialoghdlr);

   return SCIP_OKAY;
}

/** dialog execution method for the write generic problem command */
static
SCIP_DECL_DIALOGEXEC(GCGdialogExecWriteGenProblem)
{  /*lint --e{715}*/
   SCIP_CALL( SCIPdialoghdlrAddHistory(dialoghdlr, dialog, NULL, FALSE) );

   if( SCIPgetStage(scip) >= SCIP_STAGE_PROBLEM )
   {
      SCIP_CALL( writeProblem(scip, dialog, dialoghdlr, nextdialog, FALSE, TRUE) );
   }
   else
      SCIPdialogMessage(scip, NULL, "no problem available\n");

   *nextdialog = SCIPdialoghdlrGetRoot(dialoghdlr);

   return SCIP_OKAY;
}

/** dialog execution method for the write solution command */
static
SCIP_DECL_DIALOGEXEC(GCGdialogExecWriteSolution)
{  /*lint --e{715}*/
   char* filename;
   SCIP_Bool endoffile;

   SCIPdialogMessage(scip, NULL, "\n");

   SCIP_CALL( SCIPdialoghdlrGetWord(dialoghdlr, dialog, "enter filename: ", &filename, &endoffile) );
   if( endoffile )
   {
      *nextdialog = NULL;
      return SCIP_OKAY;
   }
   if( filename[0] != '\0' )
   {
      FILE* file;

      SCIP_CALL( SCIPdialoghdlrAddHistory(dialoghdlr, dialog, filename, TRUE) );

      file = fopen(filename, "w");
      if( file == NULL )
      {
         SCIPdialogMessage(scip, NULL, "error creating file <%s>\n", filename);
         SCIPdialoghdlrClearBuffer(dialoghdlr);
      }
      else
      {
         SCIPinfoMessage(scip, file, "solution status: ");
         SCIP_CALL( SCIPprintStatus(scip, file) );
         SCIPinfoMessage(scip, file, "\n");
         SCIP_CALL( SCIPprintBestSol(scip, file, FALSE) );
         SCIPdialogMessage(scip, NULL, "written solution information to file <%s>\n", filename);
         fclose(file);
      }
   }

   SCIPdialogMessage(scip, NULL, "\n");

   *nextdialog = SCIPdialoghdlrGetRoot(dialoghdlr);

   return SCIP_OKAY;
}

/** dialog execution method for the write statistics command */
static
SCIP_DECL_DIALOGEXEC(GCGdialogExecWriteStatistics)
{  /*lint --e{715}*/
   char* filename;
   SCIP_Bool endoffile;

   SCIPdialogMessage(scip, NULL, "\n");

   SCIP_CALL( SCIPdialoghdlrGetWord(dialoghdlr, dialog, "enter filename: ", &filename, &endoffile) );
   if( endoffile )
   {
      *nextdialog = NULL;
      return SCIP_OKAY;
   }
   if( filename[0] != '\0' )
   {
      FILE* file;

      SCIP_CALL( SCIPdialoghdlrAddHistory(dialoghdlr, dialog, filename, TRUE) );

      file = fopen(filename, "w");
      if( file == NULL )
      {
         SCIPdialogMessage(scip, NULL, "error creating file <%s>\n", filename);
         SCIPprintSysError(filename);
         SCIPdialoghdlrClearBuffer(dialoghdlr);
      }
      else
      {
         SCIP_CALL( SCIPprintStatistics(scip, file) );
         SCIPdialogMessage(scip, NULL, "written statistics to file <%s>\n", filename);
         fclose(file);
      }
   }

   SCIPdialogMessage(scip, NULL, "\n");

   *nextdialog = SCIPdialoghdlrGetRoot(dialoghdlr);

   return SCIP_OKAY;
}

/** dialog execution method for the write transproblem command */
static
SCIP_DECL_DIALOGEXEC(GCGdialogExecWriteTransproblem)
{  /*lint --e{715}*/
   SCIP_CALL( SCIPdialoghdlrAddHistory(dialoghdlr, dialog, NULL, FALSE) );

   if( SCIPgetStage(scip) >= SCIP_STAGE_TRANSFORMED )
   {
      SCIP_CALL( writeProblem(scip, dialog, dialoghdlr, nextdialog, TRUE, FALSE) );
   }
   else
      SCIPdialogMessage(scip, NULL, "no transformed problem available\n");

   *nextdialog = SCIPdialoghdlrGetRoot(dialoghdlr);

   return SCIP_OKAY;
}

/** dialog execution method for the write generic transproblem command */
static
SCIP_DECL_DIALOGEXEC(GCGdialogExecWriteGenTransproblem)
{  /*lint --e{715}*/
   SCIP_CALL( SCIPdialoghdlrAddHistory(dialoghdlr, dialog, NULL, FALSE) );

   if( SCIPgetStage(scip) >= SCIP_STAGE_TRANSFORMED )
   {
      SCIP_CALL( writeProblem(scip, dialog, dialoghdlr, nextdialog, TRUE, TRUE) );
   }
   else
      SCIPdialogMessage(scip, NULL, "no transformed problem available\n");

   *nextdialog = SCIPdialoghdlrGetRoot(dialoghdlr);

   return SCIP_OKAY;
}

/** dialog execution method for writing all known decompositions */
static
SCIP_DECL_DIALOGEXEC(GCGdialogExecWriteAllDecompositions)
{
   SCIP_CALL( SCIPdialoghdlrAddHistory(dialoghdlr, dialog, NULL, FALSE) );

   if( SCIPgetStage(scip) >= SCIP_STAGE_PROBLEM )
   {
      SCIP_CALL( writeAllDecompositions(scip, dialog, dialoghdlr, nextdialog) );
   }
   else
      SCIPdialogMessage(scip, NULL, "no problem available\n");

   *nextdialog = SCIPdialoghdlrGetRoot(dialoghdlr);

   return SCIP_OKAY;
}


/** creates a root dialog */
SCIP_RETCODE GCGcreateRootDialog(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_DIALOG**         root                /**< pointer to store the root dialog */
   )
{
   SCIP_CALL( SCIPincludeDialog(scip, root, NULL, GCGdialogExecMenuLazy, NULL, NULL,
         "GCG", "GCG's main menu", TRUE, NULL) );

   SCIP_CALL( SCIPsetRootDialog(scip, *root) );
   SCIP_CALL( SCIPreleaseDialog(scip, root) );
   *root = SCIPgetRootDialog(scip);

   return SCIP_OKAY;
}


/** includes or updates the gcg dialog menus in SCIP */
SCIP_RETCODE SCIPincludeDialogGcg(
   SCIP*                 scip                /**< SCIP data structure */
   )
{
   SCIP_DIALOG* root;
   SCIP_DIALOG* submenu;
   SCIP_DIALOG* dialog;

   /* root menu */
   root = SCIPgetRootDialog(scip);
   if( root == NULL )
   {
      SCIP_CALL( GCGcreateRootDialog(scip, &root) );
   }

   /* checksol */
   if( !SCIPdialogHasEntry(root, "checksol") )
   {
      SCIP_CALL( SCIPincludeDialog(scip, &dialog, NULL, GCGdialogExecChecksol, NULL, NULL,
            "checksol", "double checks best solution w.r.t. original problem", FALSE, NULL) );
      SCIP_CALL( SCIPaddDialogEntry(scip, root, dialog) );
      SCIP_CALL( SCIPreleaseDialog(scip, &dialog) );
   }

   /* conflictgraph */
   if( !SCIPdialogHasEntry(root, "conflictgraph") )
   {
      SCIP_CALL( SCIPincludeDialog(scip, &dialog, NULL, GCGdialogExecConflictgraph, NULL, NULL,
            "conflictgraph", "writes binary variable implications of transformed problem as conflict graph to file",
            FALSE, NULL) );
      SCIP_CALL( SCIPaddDialogEntry(scip, root, dialog) );
      SCIP_CALL( SCIPreleaseDialog(scip, &dialog) );
   }

   /* display */
   if( !SCIPdialogHasEntry(root, "display") )
   {
      SCIP_CALL( SCIPincludeDialog(scip, &submenu, NULL, GCGdialogExecMenu, NULL, NULL,
            "display", "display information", TRUE, NULL) );
      SCIP_CALL( SCIPaddDialogEntry(scip, root, submenu) );
      SCIP_CALL( SCIPreleaseDialog(scip, &submenu) );
   }
   if( SCIPdialogFindEntry(root, "display", &submenu) != 1 )
   {
      SCIPerrorMessage("display sub menu not found\n");
      return SCIP_PLUGINNOTFOUND;
   }

   /* display branching */
   if( !SCIPdialogHasEntry(submenu, "branching") )
   {
      SCIP_CALL( SCIPincludeDialog(scip, &dialog, NULL, GCGdialogExecDisplayBranching, NULL, NULL,
            "branching", "display branching rules", FALSE, NULL) );
      SCIP_CALL( SCIPaddDialogEntry(scip, submenu, dialog) );
      SCIP_CALL( SCIPreleaseDialog(scip, &dialog) );
   }

   /* display conflict */
   if( !SCIPdialogHasEntry(submenu, "conflict") )
   {
      SCIP_CALL( SCIPincludeDialog(scip, &dialog, NULL, GCGdialogExecDisplayConflict, NULL, NULL,
            "conflict", "display conflict handlers", FALSE, NULL) );
      SCIP_CALL( SCIPaddDialogEntry(scip, submenu, dialog) );
      SCIP_CALL( SCIPreleaseDialog(scip, &dialog) );
   }

   /* display conshdlrs */
   if( !SCIPdialogHasEntry(submenu, "conshdlrs") )
   {
      SCIP_CALL( SCIPincludeDialog(scip, &dialog, NULL, GCGdialogExecDisplayConshdlrs, NULL, NULL,
            "conshdlrs", "display constraint handlers", FALSE, NULL) );
      SCIP_CALL( SCIPaddDialogEntry(scip, submenu, dialog) );
      SCIP_CALL( SCIPreleaseDialog(scip, &dialog) );
   }

   /* display displaycols */
   if( !SCIPdialogHasEntry(submenu, "displaycols") )
   {
      SCIP_CALL( SCIPincludeDialog(scip, &dialog, NULL, GCGdialogExecDisplayDisplaycols, NULL, NULL,
            "displaycols", "display display columns", FALSE, NULL) );
      SCIP_CALL( SCIPaddDialogEntry(scip, submenu, dialog) );
      SCIP_CALL( SCIPreleaseDialog(scip, &dialog) );
   }

   /* display heuristics */
   if( !SCIPdialogHasEntry(submenu, "heuristics") )
   {
      SCIP_CALL( SCIPincludeDialog(scip, &dialog, NULL, GCGdialogExecDisplayHeuristics, NULL, NULL,
            "heuristics", "display primal heuristics", FALSE, NULL) );
      SCIP_CALL( SCIPaddDialogEntry(scip, submenu, dialog) );
      SCIP_CALL( SCIPreleaseDialog(scip, &dialog) );
   }

   /* display memory */
   if( !SCIPdialogHasEntry(submenu, "memory") )
   {
      SCIP_CALL( SCIPincludeDialog(scip, &dialog, NULL, GCGdialogExecDisplayMemory, NULL, NULL,
            "memory", "display memory diagnostics", FALSE, NULL) );
      SCIP_CALL( SCIPaddDialogEntry(scip, submenu, dialog) );
      SCIP_CALL( SCIPreleaseDialog(scip, &dialog) );
   }

   /* display nodeselectors */
   if( !SCIPdialogHasEntry(submenu, "nodeselectors") )
   {
      SCIP_CALL( SCIPincludeDialog(scip, &dialog, NULL, GCGdialogExecDisplayNodeselectors, NULL, NULL,
            "nodeselectors", "display node selectors", FALSE, NULL) );
      SCIP_CALL( SCIPaddDialogEntry(scip, submenu, dialog) );
      SCIP_CALL( SCIPreleaseDialog(scip, &dialog) );
   }

   /* display parameters */
   if( !SCIPdialogHasEntry(submenu, "parameters") )
   {
      SCIP_CALL( SCIPincludeDialog(scip, &dialog, NULL, GCGdialogExecDisplayParameters, NULL, NULL,
            "parameters", "display non-default parameter settings", FALSE, NULL) );
      SCIP_CALL( SCIPaddDialogEntry(scip, submenu, dialog) );
      SCIP_CALL( SCIPreleaseDialog(scip, &dialog) );
   }

   /* display presolvers */
   if( !SCIPdialogHasEntry(submenu, "presolvers") )
   {
      SCIP_CALL( SCIPincludeDialog(scip, &dialog, NULL, GCGdialogExecDisplayPresolvers, NULL, NULL,
            "presolvers", "display presolvers", FALSE, NULL) );
      SCIP_CALL( SCIPaddDialogEntry(scip, submenu, dialog) );
      SCIP_CALL( SCIPreleaseDialog(scip, &dialog) );
   }

   /* display problem */
   if( !SCIPdialogHasEntry(submenu, "problem") )
   {
      SCIP_CALL( SCIPincludeDialog(scip, &dialog, NULL, GCGdialogExecDisplayProblem, NULL, NULL,
            "problem", "display original problem", FALSE, NULL) );
      SCIP_CALL( SCIPaddDialogEntry(scip, submenu, dialog) );
      SCIP_CALL( SCIPreleaseDialog(scip, &dialog) );
   }

   /* display propagators */
   if( !SCIPdialogHasEntry(submenu, "propagators") )
   {
      SCIP_CALL( SCIPincludeDialog(scip, &dialog, NULL, GCGdialogExecDisplayPropagators, NULL, NULL,
            "propagators", "display propagators", FALSE, NULL) );
      SCIP_CALL( SCIPaddDialogEntry(scip, submenu, dialog) );
      SCIP_CALL( SCIPreleaseDialog(scip, &dialog) );
   }

   /* display readers */
   if( !SCIPdialogHasEntry(submenu, "readers") )
   {
      SCIP_CALL( SCIPincludeDialog(scip, &dialog, NULL, GCGdialogExecDisplayReaders, NULL, NULL,
            "readers", "display file readers", FALSE, NULL) );
      SCIP_CALL( SCIPaddDialogEntry(scip, submenu, dialog) );
      SCIP_CALL( SCIPreleaseDialog(scip, &dialog) );
   }

   /* display separators */
   if( !SCIPdialogHasEntry(submenu, "separators") )
   {
      SCIP_CALL( SCIPincludeDialog(scip, &dialog, NULL, GCGdialogExecDisplaySeparators, NULL, NULL,
            "separators", "display cut separators", FALSE, NULL) );
      SCIP_CALL( SCIPaddDialogEntry(scip, submenu, dialog) );
      SCIP_CALL( SCIPreleaseDialog(scip, &dialog) );
   }

   /* display solution */
   if( !SCIPdialogHasEntry(submenu, "solution") )
   {
      SCIP_CALL( SCIPincludeDialog(scip, &dialog, NULL, GCGdialogExecDisplaySolution, NULL, NULL,
            "solution", "display best primal solution", FALSE, NULL) );
      SCIP_CALL( SCIPaddDialogEntry(scip, submenu, dialog) );
      SCIP_CALL( SCIPreleaseDialog(scip, &dialog) );
   }

   /* display statistics */
   if( !SCIPdialogHasEntry(submenu, "statistics") )
   {
      SCIP_CALL( SCIPincludeDialog(scip, &dialog, NULL, GCGdialogExecDisplayStatistics, NULL, NULL,
            "statistics", "display problem and optimization statistics", FALSE, NULL) );
      SCIP_CALL( SCIPaddDialogEntry(scip, submenu, dialog) );
      SCIP_CALL( SCIPreleaseDialog(scip, &dialog) );
   }

   /* display transproblem */
   if( !SCIPdialogHasEntry(submenu, "transproblem") )
   {
      SCIP_CALL( SCIPincludeDialog(scip, &dialog, NULL, GCGdialogExecDisplayTransproblem, NULL, NULL,
            "transproblem", "display current node transformed problem", FALSE, NULL) );
      SCIP_CALL( SCIPaddDialogEntry(scip, submenu, dialog) );
      SCIP_CALL( SCIPreleaseDialog(scip, &dialog) );
   }

   /* display value */
   if( !SCIPdialogHasEntry(submenu, "value") )
   {
      SCIP_CALL( SCIPincludeDialog(scip, &dialog, NULL, GCGdialogExecDisplayValue, NULL, NULL,
            "value", "display value of single variable in best primal solution", FALSE, NULL) );
      SCIP_CALL( SCIPaddDialogEntry(scip, submenu, dialog) );
      SCIP_CALL( SCIPreleaseDialog(scip, &dialog) );
   }

   /* display varbranchstatistics */
   if( !SCIPdialogHasEntry(submenu, "varbranchstatistics") )
   {
      SCIP_CALL( SCIPincludeDialog(scip, &dialog, NULL, GCGdialogExecDisplayVarbranchstatistics, NULL, NULL,
            "varbranchstatistics", "display statistics for branching on variables", FALSE, NULL) );
      SCIP_CALL( SCIPaddDialogEntry(scip, submenu, dialog) );
      SCIP_CALL( SCIPreleaseDialog(scip, &dialog) );
   }

   /* display transsolution */
   if( !SCIPdialogHasEntry(submenu, "transsolution") )
   {
      SCIP_CALL( SCIPincludeDialog(scip, &dialog, NULL, GCGdialogExecDisplayTranssolution, NULL, NULL,
            "transsolution", "display best primal solution in transformed variables", FALSE, NULL) );
      SCIP_CALL( SCIPaddDialogEntry(scip, submenu, dialog) );
      SCIP_CALL( SCIPreleaseDialog(scip, &dialog) );
   }

   /* free */
   if( !SCIPdialogHasEntry(root, "free") )
   {
      SCIP_CALL( SCIPincludeDialog(scip, &dialog, NULL, GCGdialogExecFree, NULL, NULL,
            "free", "free current problem from memory", FALSE, NULL) );
      SCIP_CALL( SCIPaddDialogEntry(scip, root, dialog) );
      SCIP_CALL( SCIPreleaseDialog(scip, &dialog) );
   }

   /* help */
   if( !SCIPdialogHasEntry(root, "help") )
   {
      SCIP_CALL( SCIPincludeDialog(scip, &dialog, NULL, GCGdialogExecHelp, NULL, NULL,
            "help", "display this help", FALSE, NULL) );
      SCIP_CALL( SCIPaddDialogEntry(scip, root, dialog) );
      SCIP_CALL( SCIPreleaseDialog(scip, &dialog) );
   }

   /* newstart */
   if( !SCIPdialogHasEntry(root, "newstart") )
   {
      SCIP_CALL( SCIPincludeDialog(scip, &dialog, NULL, GCGdialogExecNewstart, NULL, NULL,
            "newstart", "reset branch and bound tree to start again from root", FALSE, NULL) );
      SCIP_CALL( SCIPaddDialogEntry(scip, root, dialog) );
      SCIP_CALL( SCIPreleaseDialog(scip, &dialog) );
   }

   /* optimize */
   if( !SCIPdialogHasEntry(root, "optimize") )
   {
      SCIP_CALL( SCIPincludeDialog(scip, &dialog, NULL, GCGdialogExecOptimize, NULL, NULL,
            "optimize", "solve the problem", FALSE, NULL) );
      SCIP_CALL( SCIPaddDialogEntry(scip, root, dialog) );
      SCIP_CALL( SCIPreleaseDialog(scip, &dialog) );
   }

   /* presolve */
   if( !SCIPdialogHasEntry(root, "presolve") )
   {
      SCIP_CALL( SCIPincludeDialog(scip, &dialog, NULL, GCGdialogExecPresolve, NULL, NULL,
            "presolve", "solve the problem, but stop after presolving stage", FALSE, NULL) );
      SCIP_CALL( SCIPaddDialogEntry(scip, root, dialog) );
      SCIP_CALL( SCIPreleaseDialog(scip, &dialog) );
   }

   /* quit */
   if( !SCIPdialogHasEntry(root, "quit") )
   {
      SCIP_CALL( SCIPincludeDialog(scip, &dialog, NULL, GCGdialogExecQuit, NULL, NULL,
            "quit", "leave GCG", FALSE, NULL) );
      SCIP_CALL( SCIPaddDialogEntry(scip, root, dialog) );
      SCIP_CALL( SCIPreleaseDialog(scip, &dialog) );
   }

   /* read */
   if( !SCIPdialogHasEntry(root, "read") )
   {
      SCIP_CALL( SCIPincludeDialog(scip, &dialog, NULL, GCGdialogExecRead, NULL, NULL,
            "read", "read a problem", FALSE, NULL) );
      SCIP_CALL( SCIPaddDialogEntry(scip, root, dialog) );
      SCIP_CALL( SCIPreleaseDialog(scip, &dialog) );
   }

   /* set */
   SCIP_CALL( SCIPincludeDialogGcgSet(scip) );

   /* display statistics */
   if( !SCIPdialogHasEntry(root, "master") )
   {
      SCIP_CALL( SCIPincludeDialog(scip, &dialog, NULL, GCGdialogExecSetMaster, NULL, NULL,
            "master", "switch to the interactive shell of the master problem", FALSE, NULL) );
      SCIP_CALL( SCIPaddDialogEntry(scip, root, dialog) );
      SCIP_CALL( SCIPreleaseDialog(scip, &dialog) );
   }

   /* detect */
   if( !SCIPdialogHasEntry(root, "detect") )
   {
      SCIP_CALL( SCIPincludeDialog(scip, &dialog, NULL, GCGdialogExecDetect, NULL, NULL,
            "detect", "Detect structure", FALSE, NULL) );
      SCIP_CALL( SCIPaddDialogEntry(scip, root, dialog) );
      SCIP_CALL( SCIPreleaseDialog(scip, &dialog) );
   }

   /* write */
   if( !SCIPdialogHasEntry(root, "write") )
   {
      SCIP_CALL( SCIPincludeDialog(scip, &submenu, NULL, GCGdialogExecMenu, NULL, NULL,
            "write", "write information to file", TRUE, NULL) );
      SCIP_CALL( SCIPaddDialogEntry(scip, root, submenu) );
      SCIP_CALL( SCIPreleaseDialog(scip, &submenu) );
   }
   if( SCIPdialogFindEntry(root, "write", &submenu) != 1 )
   {
      SCIPerrorMessage("write sub menu not found\n");
      return SCIP_PLUGINNOTFOUND;
   }

   /* write lp */
   if( !SCIPdialogHasEntry(submenu, "lp") )
   {
      SCIP_CALL( SCIPincludeDialog(scip, &dialog, NULL, GCGdialogExecWriteLp, NULL, NULL,
            "lp", "write current node LP relaxation in LP format to file", FALSE, NULL) );
      SCIP_CALL( SCIPaddDialogEntry(scip, submenu, dialog) );
      SCIP_CALL( SCIPreleaseDialog(scip, &dialog) );
   }

   /* write mip */
   if( !SCIPdialogHasEntry(submenu, "mip") )
   {
      SCIP_CALL( SCIPincludeDialog(scip, &dialog, NULL, GCGdialogExecWriteMip, NULL, NULL,
            "mip", "write current node MIP relaxation in LP format to file", FALSE, NULL) );
      SCIP_CALL( SCIPaddDialogEntry(scip, submenu, dialog) );
      SCIP_CALL( SCIPreleaseDialog(scip, &dialog) );
   }

   /* write problem */
   if( !SCIPdialogHasEntry(submenu, "problem") )
   {
      SCIP_CALL( SCIPincludeDialog(scip, &dialog, NULL, GCGdialogExecWriteProblem, NULL, NULL,
            "problem",
            "write original problem to file (format is given by file extension, e.g., orig.{lp,rlp,cip,mps})",
            FALSE, NULL) );
      SCIP_CALL( SCIPaddDialogEntry(scip, submenu, dialog) );
      SCIP_CALL( SCIPreleaseDialog(scip, &dialog) );
   }

   /* write generic problem */
   if( !SCIPdialogHasEntry(submenu, "genproblem") )
   {
      SCIP_CALL( SCIPincludeDialog(scip, &dialog, NULL, GCGdialogExecWriteGenProblem, NULL, NULL,
            "genproblem",
            "write original problem with generic names to file (format is given by file extension, e.g., orig.{lp,rlp,cip,mps})",
            FALSE, NULL) );
      SCIP_CALL( SCIPaddDialogEntry(scip, submenu, dialog) );
      SCIP_CALL( SCIPreleaseDialog(scip, &dialog) );
   }

   /* write solution */
   if( !SCIPdialogHasEntry(submenu, "solution") )
   {
      SCIP_CALL( SCIPincludeDialog(scip, &dialog, NULL, GCGdialogExecWriteSolution, NULL, NULL,
            "solution", "write best primal solution to file", FALSE, NULL) );
      SCIP_CALL( SCIPaddDialogEntry(scip, submenu, dialog) );
      SCIP_CALL( SCIPreleaseDialog(scip, &dialog) );
   }

   /* write statistics */
   if( !SCIPdialogHasEntry(submenu, "statistics") )
   {
      SCIP_CALL( SCIPincludeDialog(scip, &dialog, NULL, GCGdialogExecWriteStatistics, NULL, NULL,
            "statistics", "write statistics to file", FALSE, NULL) );
      SCIP_CALL( SCIPaddDialogEntry(scip, submenu, dialog) );
      SCIP_CALL( SCIPreleaseDialog(scip, &dialog) );
   }

   /* write transproblem */
   if( !SCIPdialogHasEntry(submenu, "transproblem") )
   {
      SCIP_CALL( SCIPincludeDialog(scip, &dialog, NULL, GCGdialogExecWriteTransproblem, NULL, NULL,
            "transproblem",
            "write current node transformed problem to file (format is given by file extension, e.g., trans.{lp,rlp,cip,mps})",
            FALSE, NULL) );
      SCIP_CALL( SCIPaddDialogEntry(scip, submenu, dialog) );
      SCIP_CALL( SCIPreleaseDialog(scip, &dialog) );
   }

   /* write transproblem */
   if( !SCIPdialogHasEntry(submenu, "gentransproblem") )
   {
      SCIP_CALL( SCIPincludeDialog(scip, &dialog, NULL, GCGdialogExecWriteGenTransproblem, NULL, NULL,
            "gentransproblem",
            "write current node transformed problem with generic names to file (format is given by file extension, e.g., trans.{lp,rlp,cip,mps})",
            FALSE, NULL) );
      SCIP_CALL( SCIPaddDialogEntry(scip, submenu, dialog) );
      SCIP_CALL( SCIPreleaseDialog(scip, &dialog) );
   }

   /* write alldecompositions */
   if( !SCIPdialogHasEntry(submenu, "alldecompositions") )
   {
      SCIP_CALL( SCIPincludeDialog(scip, &dialog, NULL, GCGdialogExecWriteAllDecompositions, NULL, NULL,
            "alldecompositions",
            "write all known decompostions to file (format is given by file extension, e.g., {dec,blk,ref})",
            FALSE, NULL) );
      SCIP_CALL( SCIPaddDialogEntry(scip, submenu, dialog) );
      SCIP_CALL( SCIPreleaseDialog(scip, &dialog) );
   }

   return SCIP_OKAY;
}

/** if a '/' occurs in the parameter's name, adds a sub menu dialog to the given menu and inserts the parameter dialog
 *  recursively in the sub menu; if no '/' occurs in the name, adds a parameter change dialog into the given dialog menu
 */
static
SCIP_RETCODE addParamDialog(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_DIALOG*          menu,               /**< dialog menu to insert the parameter into */
   SCIP_PARAM*           param,              /**< parameter to add a dialog for */
   char*                 paramname           /**< parameter name to parse */
   )
{
   char* slash;
   char* dirname;

   assert(paramname != NULL);

   /* check for a '/' */
   slash = strchr(paramname, '/');

   if( slash == NULL )
   {
      /* check, if the corresponding dialog already exists */
      if( !SCIPdialogHasEntry(menu, paramname) )
      {
         SCIP_DIALOG* paramdialog;

         if( SCIPparamIsAdvanced(param) )
         {
            SCIP_DIALOG* advmenu;

            if( !SCIPdialogHasEntry(menu, "advanced") )
            {
               /* if not yet existing, create an advanced sub menu */
               char desc[SCIP_MAXSTRLEN];

               (void) SCIPsnprintf(desc, SCIP_MAXSTRLEN, "advanced parameters");
               SCIP_CALL( SCIPincludeDialog(scip, &advmenu, NULL, GCGdialogExecMenu, NULL, NULL, "advanced", desc, TRUE, NULL) );
               SCIP_CALL( SCIPaddDialogEntry(scip, menu, advmenu) );
               SCIP_CALL( SCIPreleaseDialog(scip, &advmenu) );
            }

            /* find the corresponding sub menu */
            (void)SCIPdialogFindEntry(menu, "advanced", &advmenu);
            if( advmenu == NULL )
            {
               SCIPerrorMessage("dialog sub menu not found\n");
               return SCIP_PLUGINNOTFOUND;
            }

            if( !SCIPdialogHasEntry(advmenu, paramname) )
            {
               /* create a parameter change dialog */
               SCIP_CALL( SCIPincludeDialog(scip, &paramdialog, NULL, GCGdialogExecSetParam, GCGdialogDescSetParam, NULL,
                     paramname, SCIPparamGetDesc(param), FALSE, (SCIP_DIALOGDATA*)param) );
               SCIP_CALL( SCIPaddDialogEntry(scip, advmenu, paramdialog) );
               SCIP_CALL( SCIPreleaseDialog(scip, &paramdialog) );
            }
         }
         else
         {
            /* create a parameter change dialog */
            SCIP_CALL( SCIPincludeDialog(scip, &paramdialog, NULL, GCGdialogExecSetParam, GCGdialogDescSetParam, NULL,
                  paramname, SCIPparamGetDesc(param), FALSE, (SCIP_DIALOGDATA*)param) );
            SCIP_CALL( SCIPaddDialogEntry(scip, menu, paramdialog) );
            SCIP_CALL( SCIPreleaseDialog(scip, &paramdialog) );
         }
      }
   }
   else
   {
      SCIP_DIALOG* submenu;

      /* split the parameter name into dirname and parameter name */
      dirname = paramname;
      paramname = slash+1;
      *slash = '\0';

      /* if not yet existing, create a corresponding sub menu */
      if( !SCIPdialogHasEntry(menu, dirname) )
      {
         char desc[SCIP_MAXSTRLEN];

         (void) SCIPsnprintf(desc, SCIP_MAXSTRLEN, "parameters for <%s>", dirname);
         SCIP_CALL( SCIPincludeDialog(scip, &submenu, NULL, GCGdialogExecMenu, NULL, NULL, dirname, desc, TRUE, NULL) );
         SCIP_CALL( SCIPaddDialogEntry(scip, menu, submenu) );
         SCIP_CALL( SCIPreleaseDialog(scip, &submenu) );
      }

      /* find the corresponding sub menu */
      (void)SCIPdialogFindEntry(menu, dirname, &submenu);
      if( submenu == NULL )
      {
         SCIPerrorMessage("dialog sub menu not found\n");
         return SCIP_PLUGINNOTFOUND;
      }

      /* recursively call add parameter method */
      SCIP_CALL( addParamDialog(scip, submenu, param, paramname) );
   }

   return SCIP_OKAY;
}

/** create a "emphasis" sub menu */
static
SCIP_RETCODE createEmphasisSubmenu(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_DIALOG*          root,               /**< the menu to add the empty sub menu */
   SCIP_DIALOG**         submenu             /**< pointer to store the created emphasis sub menu */
   )
{
   if( !SCIPdialogHasEntry(root, "emphasis") )
   {
      SCIP_CALL( SCIPincludeDialog(scip, submenu,
            NULL, GCGdialogExecMenu, NULL, NULL,
            "emphasis", "predefined parameter settings", TRUE, NULL) );
      SCIP_CALL( SCIPaddDialogEntry(scip, root, *submenu) );
      SCIP_CALL( SCIPreleaseDialog(scip, submenu) );
   }
   else if( SCIPdialogFindEntry(root, "emphasis", submenu) != 1 )
   {
      SCIPerrorMessage("emphasis sub menu not found\n");
      return SCIP_PLUGINNOTFOUND;
   }

   assert(*submenu != NULL);

   return SCIP_OKAY;
}

/** includes or updates the "set" menu for each available parameter setting */
SCIP_RETCODE SCIPincludeDialogGcgSet(
   SCIP*                 scip                /**< SCIP data structure */
   )
{
   SCIP_DIALOG* root;
   SCIP_DIALOG* setmenu;
   SCIP_DIALOG* submenu;
   SCIP_DIALOG* emphasismenu;
   SCIP_DIALOG* dialog;
   SCIP_PARAM** params;
   const char* pname;
   char* paramname;
   int nparams;
   int i;

   SCIP_BRANCHRULE** branchrules;
   SCIP_CONFLICTHDLR** conflicthdlrs;
   SCIP_CONSHDLR** conshdlrs;
   SCIP_DISP** disps;
   SCIP_HEUR** heurs;
   SCIP_NODESEL** nodesels;
   SCIP_PRESOL** presols;
   SCIP_PRICER** pricers;
   SCIP_READER** readers;
   SCIP_SEPA** sepas;
   int nbranchrules;
   int nconflicthdlrs;
   int nconshdlrs;
   int ndisps;
   int nheurs;
   int nnodesels;
   int npresols;
   int npricers;
   int nreaders;
   int nsepas;

   /* get root dialog */
   root = SCIPgetRootDialog(scip);
   if( root == NULL )
   {
      SCIPerrorMessage("root dialog not found\n");
      return SCIP_PLUGINNOTFOUND;
   }

   /* find (or create) the "set" menu of the root dialog */
   if( !SCIPdialogHasEntry(root, "set") )
   {
      SCIP_CALL( SCIPincludeDialog(scip, &setmenu, NULL, GCGdialogExecMenu, NULL, NULL,
            "set", "load/save/change parameters", TRUE, NULL) );
      SCIP_CALL( SCIPaddDialogEntry(scip, root, setmenu) );
      SCIP_CALL( SCIPreleaseDialog(scip, &setmenu) );
   }
   if( SCIPdialogFindEntry(root, "set", &setmenu) != 1 )
   {
      SCIPerrorMessage("set sub menu not found\n");
      return SCIP_PLUGINNOTFOUND;
   }

   /* set gcg */
   if( !SCIPdialogHasEntry(setmenu, "default") )
   {
      SCIP_CALL( SCIPincludeDialog(scip, &dialog, NULL, GCGdialogExecSetDefault, NULL, NULL,
            "default", "reset parameter settings to their default values", FALSE, NULL) );
      SCIP_CALL( SCIPaddDialogEntry(scip, setmenu, dialog) );
      SCIP_CALL( SCIPreleaseDialog(scip, &dialog) );
   }

   /* set load */
   if( !SCIPdialogHasEntry(setmenu, "load") )
   {
      SCIP_CALL( SCIPincludeDialog(scip, &dialog, NULL, GCGdialogExecSetLoad, NULL, NULL,
            "load", "load parameter settings from a file", FALSE, NULL) );
      SCIP_CALL( SCIPaddDialogEntry(scip, setmenu, dialog) );
      SCIP_CALL( SCIPreleaseDialog(scip, &dialog) );
   }

   /* set save */
   if( !SCIPdialogHasEntry(setmenu, "save") )
   {
      SCIP_CALL( SCIPincludeDialog(scip, &dialog, NULL, GCGdialogExecSetSave, NULL, NULL,
            "save", "save parameter settings to a file", FALSE, NULL) );
      SCIP_CALL( SCIPaddDialogEntry(scip, setmenu, dialog) );
      SCIP_CALL( SCIPreleaseDialog(scip, &dialog) );
   }

   /* set diffsave */
   if( !SCIPdialogHasEntry(setmenu, "diffsave") )
   {
      SCIP_CALL( SCIPincludeDialog(scip, &dialog, NULL, GCGdialogExecSetDiffsave, NULL, NULL,
            "diffsave", "save non-default parameter settings to a file", FALSE, NULL) );
      SCIP_CALL( SCIPaddDialogEntry(scip, setmenu, dialog) );
      SCIP_CALL( SCIPreleaseDialog(scip, &dialog) );
   }

   /* set branching */
   if( !SCIPdialogHasEntry(setmenu, "branching") )
   {
      SCIP_CALL( SCIPincludeDialog(scip, &submenu, NULL, GCGdialogExecMenu, NULL, NULL,
            "branching", "change parameters for branching rules", TRUE, NULL) );
      SCIP_CALL( SCIPaddDialogEntry(scip, setmenu, submenu) );
      SCIP_CALL( SCIPreleaseDialog(scip, &submenu) );
   }
   if( SCIPdialogFindEntry(setmenu, "branching", &submenu) != 1 )
   {
      SCIPerrorMessage("branching sub menu not found\n");
      return SCIP_PLUGINNOTFOUND;
   }

   nbranchrules = SCIPgetNBranchrules(scip);
   branchrules = SCIPgetBranchrules(scip);

   for( i = 0; i < nbranchrules; ++i )
   {
      if( !SCIPdialogHasEntry(submenu, SCIPbranchruleGetName(branchrules[i])) )
      {
         SCIP_CALL( SCIPincludeDialog(scip, &dialog, NULL, GCGdialogExecMenu, NULL, NULL,
               SCIPbranchruleGetName(branchrules[i]), SCIPbranchruleGetDesc(branchrules[i]), TRUE, NULL) );
         SCIP_CALL( SCIPaddDialogEntry(scip, submenu, dialog) );
         SCIP_CALL( SCIPreleaseDialog(scip, &dialog) );
      }
   }

   /* set branching priority */
   if( !SCIPdialogHasEntry(submenu, "priority") )
   {
      SCIP_CALL( SCIPincludeDialog(scip, &dialog, NULL, GCGdialogExecSetBranchingPriority, NULL, NULL,
            "priority", "change branching priority of a single variable", FALSE, NULL) );
      SCIP_CALL( SCIPaddDialogEntry(scip, submenu, dialog) );
      SCIP_CALL( SCIPreleaseDialog(scip, &dialog) );
   }

   /* set branching direction */
   if( !SCIPdialogHasEntry(submenu, "direction") )
   {
      SCIP_CALL( SCIPincludeDialog(scip, &dialog, NULL, GCGdialogExecSetBranchingDirection, NULL, NULL,
            "direction", "change preferred branching direction of a single variable (-1:down, 0:auto, +1:up)",
            FALSE, NULL) );
      SCIP_CALL( SCIPaddDialogEntry(scip, submenu, dialog) );
      SCIP_CALL( SCIPreleaseDialog(scip, &dialog) );
   }

   /* set conflict */
   if( !SCIPdialogHasEntry(setmenu, "conflict") )
   {
      SCIP_CALL( SCIPincludeDialog(scip, &submenu, NULL, GCGdialogExecMenu, NULL, NULL,
            "conflict", "change parameters for conflict handlers", TRUE, NULL) );
      SCIP_CALL( SCIPaddDialogEntry(scip, setmenu, submenu) );
      SCIP_CALL( SCIPreleaseDialog(scip, &submenu) );
   }
   if( SCIPdialogFindEntry(setmenu, "conflict", &submenu) != 1 )
   {
      SCIPerrorMessage("conflict sub menu not found\n");
      return SCIP_PLUGINNOTFOUND;
   }

   nconflicthdlrs = SCIPgetNConflicthdlrs(scip);
   conflicthdlrs = SCIPgetConflicthdlrs(scip);

   for( i = 0; i < nconflicthdlrs; ++i )
   {
      if( !SCIPdialogHasEntry(submenu, SCIPconflicthdlrGetName(conflicthdlrs[i])) )
      {
         SCIP_CALL( SCIPincludeDialog(scip, &dialog, NULL, GCGdialogExecMenu, NULL, NULL,
               SCIPconflicthdlrGetName(conflicthdlrs[i]), SCIPconflicthdlrGetDesc(conflicthdlrs[i]), TRUE, NULL) );
         SCIP_CALL( SCIPaddDialogEntry(scip, submenu, dialog) );
         SCIP_CALL( SCIPreleaseDialog(scip, &dialog) );
      }
   }

   /* set constraints */
   if( !SCIPdialogHasEntry(setmenu, "constraints") )
   {
      SCIP_CALL( SCIPincludeDialog(scip, &submenu, NULL, GCGdialogExecMenu, NULL, NULL,
            "constraints", "change parameters for constraint handlers", TRUE, NULL) );
      SCIP_CALL( SCIPaddDialogEntry(scip, setmenu, submenu) );
      SCIP_CALL( SCIPreleaseDialog(scip, &submenu) );
   }
   if( SCIPdialogFindEntry(setmenu, "constraints", &submenu) != 1 )
   {
      SCIPerrorMessage("constraints sub menu not found\n");
      return SCIP_PLUGINNOTFOUND;
   }

   nconshdlrs = SCIPgetNConshdlrs(scip);
   conshdlrs = SCIPgetConshdlrs(scip);

   for( i = 0; i < nconshdlrs; ++i )
   {
      if( !SCIPdialogHasEntry(submenu, SCIPconshdlrGetName(conshdlrs[i])) )
      {
         SCIP_CALL( SCIPincludeDialog(scip, &dialog, NULL, GCGdialogExecMenu, NULL, NULL,
               SCIPconshdlrGetName(conshdlrs[i]), SCIPconshdlrGetDesc(conshdlrs[i]), TRUE, NULL) );
         SCIP_CALL( SCIPaddDialogEntry(scip, submenu, dialog) );
         SCIP_CALL( SCIPreleaseDialog(scip, &dialog) );
      }
   }

   /* set display */
   if( !SCIPdialogHasEntry(setmenu, "display") )
   {
      SCIP_CALL( SCIPincludeDialog(scip, &submenu, NULL, GCGdialogExecMenu, NULL, NULL,
            "display", "change parameters for display columns", TRUE, NULL) );
      SCIP_CALL( SCIPaddDialogEntry(scip, setmenu, submenu) );
      SCIP_CALL( SCIPreleaseDialog(scip, &submenu) );
   }
   if( SCIPdialogFindEntry(setmenu, "display", &submenu) != 1 )
   {
      SCIPerrorMessage("display sub menu not found\n");
      return SCIP_PLUGINNOTFOUND;
   }

   ndisps = SCIPgetNDisps(scip);
   disps = SCIPgetDisps(scip);

   for( i = 0; i < ndisps; ++i )
   {
      if( !SCIPdialogHasEntry(submenu, SCIPdispGetName(disps[i])) )
      {
         SCIP_CALL( SCIPincludeDialog(scip, &dialog, NULL, GCGdialogExecMenu, NULL, NULL,
               SCIPdispGetName(disps[i]), SCIPdispGetDesc(disps[i]), TRUE, NULL) );
         SCIP_CALL( SCIPaddDialogEntry(scip, submenu, dialog) );
         SCIP_CALL( SCIPreleaseDialog(scip, &dialog) );
      }
   }

   /* set heuristics */
   if( !SCIPdialogHasEntry(setmenu, "heuristics") )
   {
      SCIP_CALL( SCIPincludeDialog(scip, &submenu, NULL, GCGdialogExecMenu, NULL, NULL,
            "heuristics", "change parameters for primal heuristics", TRUE, NULL) );
      SCIP_CALL( SCIPaddDialogEntry(scip, setmenu, submenu) );
      SCIP_CALL( SCIPreleaseDialog(scip, &submenu) );
   }
   if( SCIPdialogFindEntry(setmenu, "heuristics", &submenu) != 1 )
   {
      SCIPerrorMessage("heuristics sub menu not found\n");
      return SCIP_PLUGINNOTFOUND;
   }

   nheurs = SCIPgetNHeurs(scip);
   heurs = SCIPgetHeurs(scip);

   for( i = 0; i < nheurs; ++i )
   {
      if( !SCIPdialogHasEntry(submenu, SCIPheurGetName(heurs[i])) )
      {
         SCIP_CALL( SCIPincludeDialog(scip, &dialog, NULL, GCGdialogExecMenu, NULL, NULL,
               SCIPheurGetName(heurs[i]), SCIPheurGetDesc(heurs[i]), TRUE, NULL) );
         SCIP_CALL( SCIPaddDialogEntry(scip, submenu, dialog) );
         SCIP_CALL( SCIPreleaseDialog(scip, &dialog) );
      }
   }

   /* create set heuristics emphasis */
   SCIP_CALL( createEmphasisSubmenu(scip, submenu, &emphasismenu) );
   assert(emphasismenu != NULL);

   /* set heuristics emphasis aggressive */
   if( !SCIPdialogHasEntry(emphasismenu, "aggressive") )
   {
      SCIP_CALL( SCIPincludeDialog(scip, &dialog,
            NULL, GCGdialogExecSetHeuristicsEmphasisAggressive, NULL, NULL,
            "aggressive", "sets heuristics <aggressive>", FALSE, NULL) );
      SCIP_CALL( SCIPaddDialogEntry(scip, emphasismenu, dialog) );
      SCIP_CALL( SCIPreleaseDialog(scip, &dialog) );
   }

   /* set heuristics emphasis fast */
   if( !SCIPdialogHasEntry(emphasismenu, "fast") )
   {
      SCIP_CALL( SCIPincludeDialog(scip, &dialog,
            NULL, GCGdialogExecSetHeuristicsEmphasisFast, NULL, NULL,
            "fast", "sets heuristics <fast>", FALSE, NULL) );
      SCIP_CALL( SCIPaddDialogEntry(scip, emphasismenu, dialog) );
      SCIP_CALL( SCIPreleaseDialog(scip, &dialog) );
   }

   /* set heuristics emphasis off */
   if( !SCIPdialogHasEntry(emphasismenu, "off") )
   {
      SCIP_CALL( SCIPincludeDialog(scip, &dialog,
            NULL, GCGdialogExecSetHeuristicsEmphasisOff, NULL, NULL,
            "off", "turns <off> all heuritics", FALSE, NULL) );
      SCIP_CALL( SCIPaddDialogEntry(scip, emphasismenu, dialog) );
      SCIP_CALL( SCIPreleaseDialog(scip, &dialog) );
   }

   /* set limits */
   if( !SCIPdialogHasEntry(setmenu, "limits") )
   {
      SCIP_CALL( SCIPincludeDialog(scip, &submenu, NULL, GCGdialogExecMenu, NULL, NULL,
            "limits", "change parameters for time, memory, objective value, and other limits", TRUE, NULL) );
      SCIP_CALL( SCIPaddDialogEntry(scip, setmenu, submenu) );

      SCIP_CALL( SCIPincludeDialog(scip, &dialog, NULL, GCGdialogExecSetLimitsObjective, NULL, NULL,
            "objective", "set limit on objective value", FALSE, NULL) );
      SCIP_CALL( SCIPaddDialogEntry(scip, submenu, dialog) );
      SCIP_CALL( SCIPreleaseDialog(scip, &dialog) );

      SCIP_CALL( SCIPreleaseDialog(scip, &submenu) );
   }

   /* set lp */
   if( !SCIPdialogHasEntry(setmenu, "lp") )
   {
      SCIP_CALL( SCIPincludeDialog(scip, &submenu, NULL, GCGdialogExecMenu, NULL, NULL,
            "lp", "change parameters for linear programming relaxations", TRUE, NULL) );
      SCIP_CALL( SCIPaddDialogEntry(scip, setmenu, submenu) );
      SCIP_CALL( SCIPreleaseDialog(scip, &submenu) );
   }

   /* set memory */
   if( !SCIPdialogHasEntry(setmenu, "memory") )
   {
      SCIP_CALL( SCIPincludeDialog(scip, &submenu, NULL, GCGdialogExecMenu, NULL, NULL,
            "memory", "change parameters for memory management", TRUE, NULL) );
      SCIP_CALL( SCIPaddDialogEntry(scip, setmenu, submenu) );
      SCIP_CALL( SCIPreleaseDialog(scip, &submenu) );
   }

   /* set misc */
   if( !SCIPdialogHasEntry(setmenu, "misc") )
   {
      SCIP_CALL( SCIPincludeDialog(scip, &submenu, NULL, GCGdialogExecMenu, NULL, NULL,
            "misc", "change parameters for miscellaneous stuff", TRUE, NULL) );
      SCIP_CALL( SCIPaddDialogEntry(scip, setmenu, submenu) );
      SCIP_CALL( SCIPreleaseDialog(scip, &submenu) );
   }

   /* set nodeselection */
   if( !SCIPdialogHasEntry(setmenu, "nodeselection") )
   {
      SCIP_CALL( SCIPincludeDialog(scip, &submenu, NULL, GCGdialogExecMenu, NULL, NULL,
            "nodeselection", "change parameters for node selectors", TRUE, NULL) );
      SCIP_CALL( SCIPaddDialogEntry(scip, setmenu, submenu) );
      SCIP_CALL( SCIPreleaseDialog(scip, &submenu) );
   }
   if( SCIPdialogFindEntry(setmenu, "nodeselection", &submenu) != 1 )
   {
      SCIPerrorMessage("nodeselection sub menu not found\n");
      return SCIP_PLUGINNOTFOUND;
   }

   nnodesels = SCIPgetNNodesels(scip);
   nodesels = SCIPgetNodesels(scip);

   for( i = 0; i < nnodesels; ++i )
   {
      if( !SCIPdialogHasEntry(submenu, SCIPnodeselGetName(nodesels[i])) )
      {
         SCIP_CALL( SCIPincludeDialog(scip, &dialog, NULL, GCGdialogExecMenu, NULL, NULL,
               SCIPnodeselGetName(nodesels[i]), SCIPnodeselGetDesc(nodesels[i]), TRUE, NULL) );
         SCIP_CALL( SCIPaddDialogEntry(scip, submenu, dialog) );
         SCIP_CALL( SCIPreleaseDialog(scip, &dialog) );
      }
   }

   /* set numerics */
   if( !SCIPdialogHasEntry(setmenu, "numerics") )
   {
      SCIP_CALL( SCIPincludeDialog(scip, &submenu, NULL, GCGdialogExecMenu, NULL, NULL,
            "numerics", "change parameters for numerical values", TRUE, NULL) );
      SCIP_CALL( SCIPaddDialogEntry(scip, setmenu, submenu) );
      SCIP_CALL( SCIPreleaseDialog(scip, &submenu) );
   }

   /* set presolving */
   if( !SCIPdialogHasEntry(setmenu, "presolving") )
   {
      SCIP_CALL( SCIPincludeDialog(scip, &submenu, NULL, GCGdialogExecMenu, NULL, NULL,
            "presolving", "change parameters for presolving", TRUE, NULL) );
      SCIP_CALL( SCIPaddDialogEntry(scip, setmenu, submenu) );
      SCIP_CALL( SCIPreleaseDialog(scip, &submenu) );
   }
   if( SCIPdialogFindEntry(setmenu, "presolving", &submenu) != 1 )
   {
      SCIPerrorMessage("presolving sub menu not found\n");
      return SCIP_PLUGINNOTFOUND;
   }

   npresols = SCIPgetNPresols(scip);
   presols = SCIPgetPresols(scip);

   for( i = 0; i < npresols; ++i )
   {
      if( !SCIPdialogHasEntry(submenu, SCIPpresolGetName(presols[i])) )
      {
         SCIP_CALL( SCIPincludeDialog(scip, &dialog, NULL, GCGdialogExecMenu, NULL, NULL,
               SCIPpresolGetName(presols[i]), SCIPpresolGetDesc(presols[i]), TRUE, NULL) );
         SCIP_CALL( SCIPaddDialogEntry(scip, submenu, dialog) );
         SCIP_CALL( SCIPreleaseDialog(scip, &dialog) );
      }
   }

   /* set pricing */
   if( !SCIPdialogHasEntry(setmenu, "pricing") )
   {
      SCIP_CALL( SCIPincludeDialog(scip, &submenu, NULL, GCGdialogExecMenu, NULL, NULL,
            "pricing", "change parameters for pricing variables", TRUE, NULL) );
      SCIP_CALL( SCIPaddDialogEntry(scip, setmenu, submenu) );
      SCIP_CALL( SCIPreleaseDialog(scip, &submenu) );
   }
   if( SCIPdialogFindEntry(setmenu, "pricing", &submenu) != 1 )
   {
      SCIPerrorMessage("pricing sub menu not found\n");
      return SCIP_PLUGINNOTFOUND;
   }

   npricers = SCIPgetNPricers(scip);
   pricers = SCIPgetPricers(scip);

   for( i = 0; i < npricers; ++i )
   {
      if( !SCIPdialogHasEntry(submenu, SCIPpricerGetName(pricers[i])) )
      {
         SCIP_CALL( SCIPincludeDialog(scip, &dialog, NULL, GCGdialogExecMenu, NULL, NULL,
               SCIPpricerGetName(pricers[i]), SCIPpricerGetDesc(pricers[i]), TRUE, NULL) );
         SCIP_CALL( SCIPaddDialogEntry(scip, submenu, dialog) );
         SCIP_CALL( SCIPreleaseDialog(scip, &dialog) );
      }
   }

   /* set propagation */
   if( !SCIPdialogHasEntry(setmenu, "propagating") )
   {
      SCIP_CALL( SCIPincludeDialog(scip, &submenu, NULL, GCGdialogExecMenu, NULL, NULL,
            "propagating", "change parameters for constraint propagation", TRUE, NULL) );
      SCIP_CALL( SCIPaddDialogEntry(scip, setmenu, submenu) );
      SCIP_CALL( SCIPreleaseDialog(scip, &submenu) );
   }

   /* set reading */
   if( !SCIPdialogHasEntry(setmenu, "reading") )
   {
      SCIP_CALL( SCIPincludeDialog(scip, &submenu, NULL, GCGdialogExecMenu, NULL, NULL,
            "reading", "change parameters for problem file readers", TRUE, NULL) );
      SCIP_CALL( SCIPaddDialogEntry(scip, setmenu, submenu) );
      SCIP_CALL( SCIPreleaseDialog(scip, &submenu) );
   }
   if( SCIPdialogFindEntry(setmenu, "reading", &submenu) != 1 )
   {
      SCIPerrorMessage("reading sub menu not found\n");
      return SCIP_PLUGINNOTFOUND;
   }

   nreaders = SCIPgetNReaders(scip);
   readers = SCIPgetReaders(scip);

   for( i = 0; i < nreaders; ++i )
   {
      if( !SCIPdialogHasEntry(submenu, SCIPreaderGetName(readers[i])) )
      {
         SCIP_CALL( SCIPincludeDialog(scip, &dialog, NULL, GCGdialogExecMenu, NULL, NULL,
               SCIPreaderGetName(readers[i]), SCIPreaderGetDesc(readers[i]), TRUE, NULL) );
         SCIP_CALL( SCIPaddDialogEntry(scip, submenu, dialog) );
         SCIP_CALL( SCIPreleaseDialog(scip, &dialog) );
      }
   }

   /* set separating */
   if( !SCIPdialogHasEntry(setmenu, "separating") )
   {
      SCIP_CALL( SCIPincludeDialog(scip, &submenu, NULL, GCGdialogExecMenu, NULL, NULL,
            "separating", "change parameters for cut separators", TRUE, NULL) );
      SCIP_CALL( SCIPaddDialogEntry(scip, setmenu, submenu) );
      SCIP_CALL( SCIPreleaseDialog(scip, &submenu) );
   }
   if( SCIPdialogFindEntry(setmenu, "separating", &submenu) != 1 )
   {
      SCIPerrorMessage("separating sub menu not found\n");
      return SCIP_PLUGINNOTFOUND;
   }

   nsepas = SCIPgetNSepas(scip);
   sepas = SCIPgetSepas(scip);

   for( i = 0; i < nsepas; ++i )
   {
      if( !SCIPdialogHasEntry(submenu, SCIPsepaGetName(sepas[i])) )
      {
         SCIP_CALL( SCIPincludeDialog(scip, &dialog, NULL, GCGdialogExecMenu, NULL, NULL,
               SCIPsepaGetName(sepas[i]), SCIPsepaGetDesc(sepas[i]), TRUE, NULL) );
         SCIP_CALL( SCIPaddDialogEntry(scip, submenu, dialog) );
         SCIP_CALL( SCIPreleaseDialog(scip, &dialog) );
      }
   }

   /* set timing */
   if( !SCIPdialogHasEntry(setmenu, "timing") )
   {
      SCIP_CALL( SCIPincludeDialog(scip, &submenu, NULL, GCGdialogExecMenu, NULL, NULL,
            "timing", "change parameters for timing issues", TRUE, NULL) );
      SCIP_CALL( SCIPaddDialogEntry(scip, setmenu, submenu) );
      SCIP_CALL( SCIPreleaseDialog(scip, &submenu) );
   }

   /* set vbc */
   if( !SCIPdialogHasEntry(setmenu, "vbc") )
   {
      SCIP_CALL( SCIPincludeDialog(scip, &submenu, NULL, GCGdialogExecMenu, NULL, NULL,
            "vbc", "change parameters for VBC tool output", TRUE, NULL) );
      SCIP_CALL( SCIPaddDialogEntry(scip, setmenu, submenu) );
      SCIP_CALL( SCIPreleaseDialog(scip, &submenu) );
   }

   /* get SCIP's parameters */
   params = SCIPgetParams(scip);
   nparams = SCIPgetNParams(scip);

   /* insert each parameter into the set menu */
   for( i = 0; i < nparams; ++i )
   {
      pname = SCIPparamGetName(params[i]);
      SCIP_ALLOC( BMSduplicateMemoryArray(&paramname, pname, strlen(pname)+1) );
      SCIP_CALL( addParamDialog(scip, setmenu, params[i], paramname) );
      BMSfreeMemoryArray(&paramname);
   }

   return SCIP_OKAY;
}
