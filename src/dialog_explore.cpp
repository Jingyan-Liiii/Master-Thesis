/*                                                                           */
/* This program is free software; you can redistribute it and/or             */
/* modify it under the terms of the GNU Lesser General Public License        */
/* as published by the Free Software Foundation; either version 3            */
/* of the License, or (at your option) any later version.                    */
/*                                                                           */
/* This program is distributed in the hope that it will be useful,           */
/* but WITHOUT ANY WARRANTY; without even the implied warranty of            */
/* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the             */
/* GNU Lesser General Public License for more details.                       */
/*                                                                           */
/* You should have received a copy of the GNU Lesser General Public License  */
/* along with this program; if not, write to the Free Software               */
/* Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.*/
/*                                                                           */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/**@file   dialog_explore.c
 * @brief  dialog menu for exploring decompositions
 * @author Michael Bastubbe
 * @author Hanna Franzen
 *
 * This file contains all dialog calls to build and use the explore menu.
 * The explore menu gives the user detailed information about all decompositions and a possibility to edit such.
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#include <assert.h>
#include <stddef.h>
#include <stdlib.h>

#include <string>
#include <iostream>
#include <regex>
#include <map>

#include "class_seeed.h"
#include "cons_decomp.h"
#include "wrapper_seeed.h"

/* column headers */
#define DEFAULT_COLUMN_MIN_WIDTH  4 /**< min width of a column in the menu table */
#define DEFAULT_COLUMN_MAX_WIDTH 10 /**< max width of a column (also determines max width of column header abbreviation) */
#define DEFAULT_COLUMNS "nr id nbloc nmacon nlivar nmavar nstlva score history pre nopcon nopvar sel" /**< default column headers */

#define DEFAULT_MENULENGTH 10 /**< initial number of entries in menu */

namespace gcg
{


/** gets the seeed structure from a given id (local help function)
 * 
 * @todo remove this help function once the seeed structure is depricated
 * @returns seeed for given id
*/
static 
Seeed* getSeeed(
   SCIP* scip,    /**< SCIP data structure */
   int id         /**< id of seeed */
   )
{
   Seeed_Wrapper sw;
   GCGgetSeeedFromID(scip, &id, &sw);
   assert( sw.seeed != NULL );
   return sw.seeed;
}


/** modifies menulength according to input and updates menu accordingly
 * @returns SCIP return code */
static
SCIP_RETCODE SCIPdialogSetNEntires(
   SCIP* scip,                   /**< SCIP data structure */
   SCIP_DIALOGHDLR* dialoghdlr,  /**< dialog handler for user input management */
   SCIP_DIALOG* dialog,          /**< dialog for user input management */
   int listlength,               /**< length of seeed id list */
   int* menulength               /**< current menu length to be modified */
   )
{
   char* ntovisualize;
   SCIP_Bool endoffile;
   int newlength;
   int commandlen;

   SCIPdialogMessage(scip, NULL, "Please specify the amount of entries to be shown in this menu:\n");
   SCIP_CALL( SCIPdialoghdlrGetWord(dialoghdlr, dialog, " ", &ntovisualize, &endoffile) );
   commandlen = strlen(ntovisualize);

   newlength = -1;
   if( commandlen != 0 )
      newlength = atoi(ntovisualize);

   /* check whether there are decompositions,
    * (preventing "Why doesn't it show anything? Maybe the entry number is 0") */
   if( SCIPconshdlrDecompGetNSeeeds(scip) == 0 )
   {
      SCIPinfoMessage(scip, NULL, "No decompositions available. Please detect first.\n");
      return SCIP_OKAY;
   }

   if( commandlen == 0 || newlength < 1 )
   {
      SCIPdialogMessage( scip, NULL, "The input was not a valid number." );
      return SCIP_OKAY;
   }

   if( newlength < listlength )
      *menulength = newlength;
   else
      *menulength = listlength;

   return SCIP_OKAY;
}


/** changes the used score internally and updates the seeedinfo structure accordingly
 * 
 * @returns SCIP return code
 */
static
SCIP_RETCODE GCGdialogChangeScore(
   SCIP* scip,                         /**< SCIP data structure */
   SCIP_DIALOGHDLR* dialoghdlr,        /**< dialog handler for user input management */
   SCIP_DIALOG* dialog                 /**< dialog for user input management */
   )
{
   char* getscore;
   SCIP_Bool endoffile;
   int commandlen;

   SCIPdialogMessage(scip, NULL, "\nPlease specify the new score:\n");
   SCIPdialogMessage(scip, NULL, "0: max white, \n1: border area, \n2: classic, \n3: max foreseeing white, \n4: ppc-max-white, \n");
   SCIPdialogMessage(scip, NULL, "5: max foreseeing white with aggregation info, \n6: ppc-max-white with aggregation info, \n7: experimental benders score\n");
   SCIPdialogMessage(scip, NULL, "8: strong decomposition score\n");
   SCIPdialogMessage(scip, NULL, "Note: Sets the detection/scoretype parameter to the given score.\n");

   /* get input */
   SCIP_CALL( SCIPdialoghdlrGetWord(dialoghdlr, dialog, " ", &getscore, &endoffile) );
   commandlen = strlen(getscore);
   if( commandlen != 0 )
   {
      /* convert to int (if value is invalid, this results in 0) */
      int scorenr = atoi(getscore);

      /* check if the value is in valid range */
      if(scorenr >= 0 && scorenr <= 8)
      {
         /* set score */
         SCIPsetIntParam(scip, "detection/scoretype", scorenr);
         SCIPconshdlrDecompSetScoretype(scip, static_cast<SCORETYPE>(scorenr));
         SCIPdialogMessage(scip, NULL, "Score set to %d.\n", scorenr);
      }
   }

   return SCIP_OKAY;
}


/**
 * Outputs the given char x times as SCIPdialogMessage
 * @returns SCIP status
 */
static
SCIP_RETCODE outputCharXTimes(
   SCIP* scip,          /**< SCIP data structure */
   const char letter,   /**< char to write */
   int x                /**< write char x times */
   )
{
   for(int i = 0; i < x; i++)
      SCIPdialogMessage(scip, NULL, "%c", letter);

   return SCIP_OKAY;
}

/** @brief show current menu containing seeed information
 *
 * Update length of seeed list in case it changed since the last command
 * and show the table of seeeds.
 * @returns SCIP status
 */
static
SCIP_RETCODE SCIPdialogShowMenu(
   SCIP* scip,                            /**< SCIP data structure */
   std::vector<std::string> columns,      /**< list of column headers (abbreviations) */
   int* nseeeds,                          /**< max number of seeeds */
   const int startindex,                  /**< index (in seeed list) of uppermost seeed in extract */
   int menulength,                        /**< number of menu entries */
   std::vector<int>* idlist               /**< current list of seeed ids */
   )
{
   assert(scip != NULL);

   /* update seeed list in case it changed (in which case the amount of seeeds should have changed)*/
   if(*nseeeds < SCIPconshdlrDecompGetNSeeeds(scip))
   {
      *nseeeds = SCIPconshdlrDecompGetNSeeeds(scip);
      int* idarray;
      int listlength;
      SCIP_CALL( SCIPallocBlockMemoryArray(scip, &idarray, *nseeeds) );
      SCIPconshdlrDecompGetSeeedLeafList(scip, &idarray, &listlength);

      /* reset idlist to the new idarray */
      idlist->clear();
      for(int i = 0; i < listlength; i++)
      {
         idlist->push_back(idarray[i]);
      }

      /* free idarray */
      SCIPfreeBlockMemoryArray(scip, &idarray, *nseeeds);
   }

   /* sort seeed ids by score, descending (in case score was changed or id list was updated)*/
   std::sort(idlist->begin(), idlist->end(), [&](const int a, const int b) {return (getSeeed(scip, a)->getScore(SCIPconshdlrDecompGetScoretype(scip)) > getSeeed(scip, b)->getScore(SCIPconshdlrDecompGetScoretype(scip))); });

   /* count corresponding seeeds for overview statistics */
   int ndetectedpresolved = 0;
   int ndetectedunpresolved = 0;

   for(int i = 0; i < (int) idlist->size(); ++i)
   {
      Seeed* seeed = getSeeed(scip, idlist->at(i));
      /* finished seeeds */
      if(seeed->isComplete())
      {
         /* from presolved problem */
         if(!seeed->isFromUnpresolved())
         {
            ++ndetectedpresolved;
         }
         /* from original problem */
         else
         {
            ++ndetectedunpresolved;
         }
      }
   }

   /* count width of menu table by summing width of column headers,
    * make header line, and border line for header (will be beneath the column headers) as follows:
    * a border line of the length of the column width as '-' for each column and a space between the columns,
    * e.g. header line "   nr   id nbloc nmacon  sel ",
    * e.g. underscores " ---- ---- ----- ------ ---- " */
   std::string headerline;
   std::string borderline;
   std::map<std::string, int> columnlength;
   int linelength = 0;

   /* line starts with a space */
   headerline = " ";
   borderline = " ";

   /* add each column header */
   for(auto header : columns)
   {
      /* "score" is a wildcard for the current score, relace it with actual scoretype */
      std::string newheader;
      if(header != "score")
         newheader = header;
      else
         newheader = SCIPconshdlrDecompGetScoretypeShortName(scip, SCIPconshdlrDecompGetScoretype(scip));
      
      /* make sure the header name is unique and add a length for header */
      assert(columnlength.find(header) == columnlength.end());
      columnlength.insert(std::pair<std::string,int>(header, 0));
      /* if header is smaller than min column width, add spaces to header first */
      if(newheader.size() < DEFAULT_COLUMN_MIN_WIDTH)
      {
         for(int i = 0; i < (DEFAULT_COLUMN_MIN_WIDTH - (int) newheader.size()); i++)
         {
            headerline += " ";
            borderline += "-";
            columnlength.at(header)++;
         }
      }
      /* add header to headerline and add #chars of header as '-' to borderline*/
      headerline += newheader;
      for(int i = 0; i < (int) newheader.size(); i++)
         borderline += "-";
      columnlength.at(header) += (int) newheader.size();
      /* add space to both lines as column border */
      headerline += " ";
      borderline += " ";
      /* add columnlength (+1 for border space) to overall linelength */
      linelength += columnlength.at(header) + 1;
   }

   /* display overview statistics */
   SCIPdialogMessage(scip, NULL, "\n");
   outputCharXTimes(scip, '=', linelength);
   SCIPdialogMessage(scip, NULL, " \n");
   SCIPdialogMessage(scip, NULL, "Summary              presolved       original \n");
   SCIPdialogMessage(scip, NULL, "                     ---------       -------- \n");
   SCIPdialogMessage(scip, NULL, "detected             ");
   SCIPdialogMessage(scip, NULL, "%9d       ", ndetectedpresolved );
   SCIPdialogMessage(scip, NULL, "%8d\n", ndetectedunpresolved );
   outputCharXTimes(scip, '=', linelength);
   SCIPdialogMessage(scip, NULL, " \n");
   /* display header of table */
   SCIPdialogMessage(scip, NULL, "%s\n", headerline.c_str());
   SCIPdialogMessage(scip, NULL, "%s\n", borderline.c_str());

   /* go through all seeeds that should currently be displayed,
    * so from startindex on menulength many entries if there are that much left in the list */
   for(int i = startindex; i < startindex + menulength && i < (int) idlist->size(); ++i)
   {
      /* get current seeed */
      Seeed seeed = getSeeed(scip, idlist->at(i));

      /* each line starts with a space */
      SCIPdialogMessage(scip, NULL, " ");

      /* go through the columns and write the entry for each one */
      for(auto header : columns)
      {
         std::string towrite;
         if(header == "nr")
            towrite = std::to_string(i);
         else if(header == "id")
            towrite = std::to_string(seeed.getID());
         else if(header == "nbloc")
            towrite = std::to_string(seeed.getNBlocks());
         else if(header == "nmacon")
            towrite = std::to_string(seeed.getNMasterconss());
         else if(header == "nmavar")
            towrite = std::to_string(seeed.getNMastervars());
         else if(header == "nlivar")
            towrite = std::to_string(seeed.getNLinkingvars());
         else if(header == "nstlva")
            towrite = std::to_string(seeed.getNTotalStairlinkingvars());
         else if(header == "score")
            towrite = std::to_string(seeed.getScore(SCIPconshdlrDecompGetScoretype(scip))).substr(0, columnlength.at(header));
         else if(header == "history")
            towrite = seeed.getDetectorChainString();
         else if(header == "pre")
            towrite = (!seeed.isFromUnpresolved()) ? "yes" : "no";
         else if(header == "nopcon")
            towrite = std::to_string(seeed.getNOpenconss());
         else if(header == "nopvar")
            towrite = std::to_string(seeed.getNOpenvars());
         else if(header == "sel")
            towrite = (seeed.isSelected()) ? "yes" : "no";
         else 
            towrite = " ";

         /* write spaces to fill out the columnwidth until towrite */
         outputCharXTimes(scip, ' ', (columnlength.at(header) - (int) towrite.size()));
         /* write actual value of the column +1 space for border */
         SCIPdialogMessage(scip, NULL, "%s ", towrite.c_str());
      }

      /* continue to next line */
      SCIPdialogMessage(scip, NULL, "\n");
   }

   /* at the end of the table add a line */
   outputCharXTimes(scip, '=', linelength);

   return SCIP_OKAY;
}


/** Shows information about the explore screen and its abbreviations
 *
 * @returns SCIP status */
static
SCIP_RETCODE SCIPdialogShowLegend(
   SCIP* scip,                         /**< SCIP data structure */
   std::vector<std::string> columns    /**< list of table header entries */
   )
{
   assert(scip != NULL);
   DEC_DETECTOR** detectors;

   /* print header for detector list */
   SCIPdialogMessage(scip, NULL, "List of included detectors for decompositions histories: \n");

   SCIPdialogMessage(scip, NULL, "\n%30s    %4s\n", "detector" , "char");
   SCIPdialogMessage(scip, NULL, "%30s    %4s\n", "--------" , "----");

   /* get and print char of each detector */
   detectors = SCIPconshdlrDecompGetDetectors(scip);

   for( int det = 0; det < SCIPconshdlrDecompGetNDetectors(scip); ++det )
   {
      DEC_DETECTOR* detector;
      detector = detectors[det];

      SCIPdialogMessage(scip, NULL, "%30s    %4c\n", DECdetectorGetName(detector), DECdetectorGetChar(detector));
   }
   /* print usergiven as part of detector chars */
   SCIPdialogMessage(scip, NULL, "%30s    %4s\n", "given by user" , "U");
   SCIPdialogMessage(scip, NULL, "\n" );

   SCIPdialogMessage(scip, NULL, "=================================================================================================== \n");

   SCIPdialogMessage(scip, NULL, "\n" );

   /* print header of abbreviation table */
   SCIPdialogMessage(scip, NULL, "List of abbreviations of decomposition table \n" );
   SCIPdialogMessage(scip, NULL, "\n" );
   SCIPdialogMessage(scip, NULL, "%30s     %s\n", "abbreviation", "description");
   SCIPdialogMessage(scip, NULL, "%30s     %s\n", "------------", "-----------");

   /* add legend entry for each header abbreviation */
   for(auto header : columns)
   {
      /* get description for current header */
      std::string desc;
      if(header == "nr")
         desc = "number of the decomposition (use this number for choosing the decomposition)";
      else if(header == "id")
         desc = "id of the decomposition (identifies the decomposition in reports/statistics/visualizations/etc.)";
      else if(header == "nbloc")
         desc = "number of blocks";
      else if(header == "nmacon")
         desc = "number of master constraints";
      else if(header == "nmavar")
         desc = "number of master variables (do not occur in blocks)";
      else if(header == "nlivar")
         desc = "number of linking variables";
      else if(header == "nstlva")
         desc = "number of stairlinking variables";
      else if(header == "score")
         desc = SCIPconshdlrDecompGetScoretypeDescription(scip, SCIPconshdlrDecompGetScoretype(scip));
      else if(header == "history")
         desc = "list of detector chars worked on this decomposition ";
      else if(header == "pre")
         desc = "is this decomposition for the presolved problem";
      else if(header == "nopcon")
         desc = "number of open constraints";
      else if(header == "nopvar")
         desc = "number of open variables";
      else if(header == "sel")
         desc = "is this decomposition selected at the moment";
      else 
         desc = " ";

      /* print the header with the description */
      if(header != "score")
      {
         SCIPdialogMessage(scip, NULL, "%30s     %s\n", header.c_str(), desc.c_str());
      }
      /* if the header is "score" replace with shortname of the current score */
      else
      {
         SCIPdialogMessage(scip, NULL, "%30s     %s\n", SCIPconshdlrDecompGetScoretypeShortName(scip, SCIPconshdlrDecompGetScoretype(scip)), desc.c_str());
      }
      
   }
   SCIPdialogMessage(scip, NULL, "\n=================================================================================================== \n");

   return SCIP_OKAY;
}

/** Shows help section of explore menu
 *
 * @returns SCIP status */
static
SCIP_RETCODE SCIPdialogShowHelp(
   SCIP* scip  /**< SCIP data structure */
   )
{
   assert(scip != NULL);

   SCIPdialogMessage(scip, NULL, "=================================================================================================== \n");
   SCIPdialogMessage(scip, NULL, "\n" );
   SCIPdialogMessage(scip, NULL, "List of selection commands \n" );
   SCIPdialogMessage(scip, NULL, "\n" );
   SCIPdialogMessage(scip, NULL, "%30s     %s\n", "command", "description");
   SCIPdialogMessage(scip, NULL, "%30s     %s\n", "-------", "-----------");
   SCIPdialogMessage(scip, NULL, "%30s     %s\n", "select", "selects/unselects decomposition with given id");
   SCIPdialogMessage(scip, NULL, "%30s     %s\n", "previous", "displays the preceding decompositions (if there are any)");
   SCIPdialogMessage(scip, NULL, "%30s     %s\n", "next", "displays the subsequent decompositions (if there are any)");
   SCIPdialogMessage(scip, NULL, "%30s     %s\n", "top", "displays the first decompositions");
   SCIPdialogMessage(scip, NULL, "%30s     %s\n", "end", "displays the last decompositions");
   SCIPdialogMessage(scip, NULL, "%30s     %s\n", "legend", "displays the legend for table header and history abbreviations");
   SCIPdialogMessage(scip, NULL, "%30s     %s\n", "help", "displays this help");
   SCIPdialogMessage(scip, NULL, "%30s     %s\n", "number_entries", "modifies the number of displayed decompositions");
   SCIPdialogMessage(scip, NULL, "%30s     %s\n", "visualize", "visualizes the specified decomposition (requires gnuplot)");
   SCIPdialogMessage(scip, NULL, "%30s     %s\n", "inspect", "displays detailed information for the specified decomposition");
   SCIPdialogMessage(scip, NULL, "%30s     %s\n", "set_score", "sets the score by which the \"goodness\" of decompositions is evaluated");
   SCIPdialogMessage(scip, NULL, "%30s     %s\n", "quit", "return to main menu");

   SCIPdialogMessage(scip, NULL, "\n=================================================================================================== \n");

   return SCIP_OKAY;
}


/** Shows a visualization of the seeed specified by the user via the dialog
 *
 * @returns SCIP status */
static
SCIP_RETCODE SCIPdialogSelectVisualize(
   SCIP*                   scip,       /**< SCIP data structure */
   SCIP_DIALOGHDLR*        dialoghdlr, /**< dialog handler for user input management */
   SCIP_DIALOG*            dialog,     /**< dialog for user input management */
   std::vector<int>        idlist      /**< current list of seeed ids */
   )
{
   char* ntovisualize;
   SCIP_Bool endoffile;
   int idtovisu;
   int commandlen;

   assert(scip != NULL);

   SCIPdialogMessage(scip, NULL, "Please specify the nr of the decomposition to be visualized:\n");
   SCIP_CALL( SCIPdialoghdlrGetWord(dialoghdlr, dialog, " ", &ntovisualize, &endoffile) );
   commandlen = strlen(ntovisualize);

   idtovisu = -1;
   if( commandlen != 0 )
      idtovisu = atoi(ntovisualize);

   /* check whether the seeed exists */
   if( commandlen == 0 || idtovisu < 0 || idtovisu >= (int) idlist.size() )
   {
      SCIPdialogMessage( scip, NULL, "This nr is out of range." );
      return SCIP_OKAY;
   }

   /* get and show seeed */
   Seeed* seeed = getSeeed(scip, idlist.at(idtovisu));
   seeed->showVisualisation();

   return SCIP_OKAY;
}


/**
 * Displays information about a seeed that is chosen by the user in a dialog.
 *
 * @returns SCIP status
 */
static
SCIP_RETCODE SCIPdialogInspectSeeed(
   SCIP*                   scip,       /**< SCIP data structure */
   SCIP_DIALOGHDLR*        dialoghdlr, /**< dialog handler for user input management */
   SCIP_DIALOG*            dialog,     /**< dialog for user input management */
   std::vector<int>        idlist      /**< current list of seeed ids */
   )
{
   char* ntoinspect;
   char* ndetaillevel;
   SCIP_Bool endoffile;
   int idtoinspect;
   int detaillevel;

   int commandlen;

   assert( scip != NULL );

   /* read the id of the decomposition to be inspected */
   SCIPdialogMessage( scip, NULL, "Please specify the nr of the decomposition to be inspected:\n");
   SCIP_CALL( SCIPdialoghdlrGetWord( dialoghdlr, dialog, " ", &ntoinspect, &endoffile ) );
   commandlen = strlen( ntoinspect );

   idtoinspect = -1;
   if( commandlen != 0 )
      idtoinspect = atoi( ntoinspect );

   if(idtoinspect < 0 || idtoinspect >= (int) idlist.size()){
      SCIPdialogMessage( scip, NULL, "This nr is out of range." );
      return SCIP_OKAY;
   }

   /* check whether ID is in valid range */
   Seeed* seeed = getSeeed(scip, idlist.at(idtoinspect));

   /* read the desired detail level; for wrong input, it is set to 1 by default */
   SCIPdialogMessage( scip, NULL,
      "Please specify the detail level:\n  0 - brief overview\n  1 - block and detector info (default)\n  2 - cons and var assignments\n" );
   SCIP_CALL( SCIPdialoghdlrGetWord( dialoghdlr, dialog, " ", &ndetaillevel, &endoffile ) );
   commandlen = strlen( ndetaillevel );

   detaillevel = 1;
   if( commandlen != 0 )
   {
      std::stringstream convert( ndetaillevel );
      convert >> detaillevel;

      if( detaillevel < 0 || ( detaillevel == 0 && ndetaillevel[0] != '0' ) )
      {
         detaillevel = 1;
      }
   }

   /* call displayInfo method according to chosen parameters */
   seeed->displayInfo( detaillevel );

   return SCIP_OKAY;
}


/** Lets the user select decompositions from the explore menu
 *
 * @returns SCIP status */
static
SCIP_RETCODE SCIPdialogSelect(
   SCIP*                   scip,       /**< SCIP data structure */
   SCIP_DIALOGHDLR*        dialoghdlr, /**< dialog handler for user input management */
   SCIP_DIALOG*            dialog,     /**< dialog for user input management */
   std::vector<int>        idlist      /**< current list of seeed ids */
   )
{
   char* ntovisualize;
   SCIP_Bool endoffile;
   int idtovisu;

   int commandlen;

   assert(scip != NULL);

   /* get input */
   SCIPdialogMessage(scip, NULL, "Please specify the nr of the decomposition to be selected:\n");
   SCIP_CALL( SCIPdialoghdlrGetWord(dialoghdlr, dialog, " ", &ntovisualize, &endoffile) );
   commandlen = strlen(ntovisualize);

   idtovisu = -1;
   if( commandlen != 0)
      idtovisu = atoi(ntovisualize);

   /* check if the input is a valid number */
   if( commandlen == 0 || idtovisu < 0 || idtovisu >= (int) idlist.size() )
   {
      SCIPdialogMessage( scip, NULL, "This nr is out of range, nothing was selected." );
      return SCIP_OKAY;
   }

   /* get seeed from id*/
   Seeed* seeed = getSeeed(scip, idlist.at(idtovisu));

   /* reverse selection (deselects if seeed was previously selected) */
   seeed->setSelected(!seeed->isSelected() );

   if( seeed->isSelected() )
      std::cout << "is selected!" << seeed->isSelected() << std::endl;

   return SCIP_OKAY;
}

static
SCIP_RETCODE SCIPdialogExecCommand(
   SCIP*                   scip,          /**< SCIP data structure */
   SCIP_DIALOGHDLR*        dialoghdlr,    /**< dialog handler for user input management */
   SCIP_DIALOG*            dialog,        /**< dialog for user input management */
   std::vector<std::string> columns,
   char*                   command,
   SCIP_Bool               endoffile,
   int*                    startindex,
   int*                    menulength,
   SCIP_Bool*              finished,
   int*                    nseeeds,
   std::vector<int>*       idlist         /**< current list of seeed ids */
   )
{
   int commandlen = strlen(command);

      if( strncmp( command, "previous", commandlen) == 0 )
      {
         *startindex = *startindex - *menulength;
         if(*startindex < 0 )
            *startindex = 0;
      }
      else if( strncmp( command, "next", commandlen) == 0 )
      {
         *startindex = *startindex + *menulength;
         if( *startindex > (int) idlist->size() - *menulength )
            *startindex = (int) idlist->size() - *menulength;
      }
      else if( strncmp( command, "top", commandlen) == 0 )
      {
         *startindex = 0;
      }
      else if( strncmp( command, "end", commandlen) == 0 )
      {
         *startindex = (int) idlist->size() - *menulength;
      }

      else if( strncmp( command, "quit", commandlen) == 0 || strncmp( command, "..", commandlen) == 0 )
      {
         *finished = TRUE;
         SCIP_CALL( SCIPconshdlrDecompChooseCandidatesFromSelected(scip) );
      }

      else if( strncmp( command, "legend", commandlen) == 0 )
      {
         SCIP_CALL( SCIPdialogShowLegend(scip, columns) );
      }

      else if( strncmp( command, "help", commandlen) == 0 )
      {
         SCIP_CALL( SCIPdialogShowHelp(scip) );
      }

      else if( strncmp( command, "number_entries", commandlen) == 0 )
      {
         SCIP_CALL( SCIPdialogSetNEntires(scip, dialoghdlr, dialog, (int) idlist->size(), menulength) );
      }

      else if( strncmp( command, "visualize", commandlen) == 0 )
      {
         SCIP_CALL( SCIPdialogSelectVisualize(scip, dialoghdlr, dialog, *idlist) );
      }

      else if( strncmp( command, "inspect", commandlen) == 0 )
      {
         SCIP_CALL( SCIPdialogInspectSeeed( scip, dialoghdlr, dialog, *idlist) );
      }

      else if( strncmp( command, "select", commandlen) == 0 )
      {
         SCIP_CALL( SCIPdialogSelect(scip, dialoghdlr, dialog, *idlist) );
      }

      else if( strncmp( command, "set_score", commandlen) == 0 )
      {
         SCIP_CALL( GCGdialogChangeScore(scip, dialoghdlr, dialog) );
      }

      //@todo
      /*
      else if( strncmp( command, "sort_asc", commandlen) == 0 )
      {
         SCIP_CALL( SCIPdialogChangeOrder(scip, dialoghdlr, dialog, seeedinfos) );
      }

      else if( strncmp( command, "sort_by", commandlen) == 0 )
      {
         SCIP_CALL( SCIPdialogChangeOrder(scip, dialoghdlr, dialog, seeedinfos) );
      }
      */

   return SCIP_OKAY;
}


extern "C" {

SCIP_RETCODE GCGdialogExecExplore(
   SCIP*                   scip,
   SCIP_DIALOGHDLR*        dialoghdlr,
   SCIP_DIALOG*            dialog
   )
{
   /* set navigation defaults */
   int startindex = 0;
   int menulength = DEFAULT_MENULENGTH;

   /* check for available seeeds */
   int nseeeds;   /**< stores the last known number of seeeds, is handed down to check for changes in seeed number */
   nseeeds = SCIPconshdlrDecompGetNSeeeds(scip);
   if(nseeeds == 0)
   {
      SCIPdialogMessage( scip, NULL, "There are no decompositions to explore yet, please detect first.\n" );
      return SCIP_OKAY;
   }

   /* get initial seeed id list */
   int* idarray;
   int listlength;
   SCIP_CALL( SCIPallocBlockMemoryArray(scip, &idarray, nseeeds) );
   SCIPconshdlrDecompGetSeeedLeafList(scip, &idarray, &listlength);

   /* put ids into vector for easier handling */
   std::vector<int> idlist = std::vector<int>();
   for(int i = 0; i < listlength; i++)
   {
      idlist.push_back(idarray[i]);
   }

   /* free idarray */
   SCIPfreeBlockMemoryArray(scip, &idarray, nseeeds);

   /* sort by score, descending */
   std::sort(idlist.begin(), idlist.end(), [&](const int a, const int b) {return (getSeeed(scip, a)->getScore(SCIPconshdlrDecompGetScoretype(scip)) > getSeeed(scip, b)->getScore(SCIPconshdlrDecompGetScoretype(scip))); });

   /* set initial columns */
   std::vector<std::string> columns;
   char columnstr[] = DEFAULT_COLUMNS;
   char* tempcolumns = strtok(columnstr, " ");
   while(tempcolumns != NULL)
   {
      /* get each column header of default */
      char newchar[DEFAULT_COLUMN_MAX_WIDTH]; // cutting string at max column width if longer
      strcpy(newchar, tempcolumns);
      columns.push_back(newchar);
      /**@note: 'score' is a wildcard! replace by score name later*/
      
      /* get the next item in the list */
      tempcolumns = strtok (NULL, " ");
   }

   /* while user has not aborted: show current list extract and catch commands */
   SCIP_Bool finished = false;
   char* command;
   SCIP_Bool endoffile;
   while( !finished )
   {
      SCIPdialogShowMenu(scip, columns, &nseeeds, startindex, menulength, &idlist);

      SCIP_CALL( SCIPdialoghdlrGetWord(dialoghdlr, dialog,
         "Please enter command or decomposition id to select (or \"h\" for help) : \nGCG/explore> ", &command, &endoffile) );

      SCIPdialogExecCommand(scip, dialoghdlr, dialog, columns, command, endoffile, &startindex, &menulength, &finished, &nseeeds, &idlist);
   }

   return SCIP_OKAY;
}

} // extern "C"

} // namespace gcg