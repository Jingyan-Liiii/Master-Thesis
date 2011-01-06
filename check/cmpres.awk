#!/bin/gawk -f
#* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
#*                                                                           *
#*                  This file is part of the program                         *
#*          GCG --- Generic Colum Generation                                 *
#*                  a Dantzig-Wolfe decomposition based extension            *
#*                  of the branch-cut-and-price framework                    *
#*         SCIP --- Solving Constraint Integer Programs                      *
#*                                                                           *
#* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
# $Id$
#
#@file    cmpres.awk
#@brief   SCIP Check Comparison Report Generator
#@author  Tobias Achterberg
#
function abs(x)
{
   return x < 0 ? -x : x;
}

function min(x,y)
{
   return (x) < (y) ? (x) : (y);
}

function max(x,y)
{
   return (x) > (y) ? (x) : (y);
}

function ceil(x)
{
   return (x == int(x) ? x : (x < 0 ? int(x) : int(x+1)));
}

function floor(x)
{
   return (x == int(x) ? x : (x < 0 ? int(x-1) : int(x)));
}

function fracceil(x,f)
{
   return ceil(x/f)*f;
}

function fracfloor(x,f)
{
   return floor(x/f)*f;
}

function mod(x,m)
{
   return (x - m*floor(x/m));
}

function printhline(nsolver)
{
   for( s = 0; s < nsolver; ++s )
   {
      if( s == 0 )
         printf("--------------------+-+---------+--------+");
      else
         printf("-+---------+--------+------+------+");
   }
   printf("-------------\n");
}

function isfaster(t,reft,tol)
{
   return (t < 1.0/tol*reft && t <= reft - 0.2);
}

function isslower(t,reft,tol)
{
   return isfaster(reft, t, tol);
}

function texcompstr(val,refval, x,s,t)
{
   x = floor(100*(val/refval-1.0)+0.5);
   s = "";
   t = "";
   if( x < 0 )
   {
      if( x <= -texcolorlimit )
      {
         s = "\\textcolor{red}{\\raisebox{0.25ex}{\\tiny $-$}";
         t = "}";
      }
      else
         s = "\\raisebox{0.25ex}{\\tiny $-$}";
   }
   else if( x > 0 )
   {
      if( x >= +texcolorlimit )
      {
         s = "\\textcolor{blue}{\\raisebox{0.25ex}{\\tiny $+$}";
         t = "}";
      }
      else
         s = "\\raisebox{0.25ex}{\\tiny $+$}";
   }

   return sprintf("%s%d%s", s, abs(x), t);
}

function texstring(s, ts)
{
   ts = s;
   gsub(/_/, "\\_", ts);

   return ts;
}

function texint(x, ts,r)
{
   ts = "";
   x = floor(x);
   while( x != 0 )
   {
      r = mod(x, 1000);
      x = floor(x/1000);
      if( ts != "" )
         ts = "\\," ts;
      ts = r "" ts;
   }

   return ts;
}

function texsolvername(s, sname)
{
   sname = solvername[s];
   if( setname[sname] != "" )
      sname = setname[sname];
   else
   {
      sub(/.*:/, "", sname);
      sub(/.*_/, "", sname);
      if( length(sname) > 12 )
         sname = substr(sname, length(sname)-11, 12);
   }

   return sname;
}

BEGIN {
   infinity = 1e+20;
   timegeomshift = 10.0;
   nodegeomshift = 100.0;
   mintime = 0.5;
   wintolerance = 1.1;
   markbettertime = 1.1;
   markworsetime  = 1.1;
   markbetternodes = 5.0;
   markworsenodes  = 5.0;
   onlymarked = 0;
   onlyprocessed = 0;
   maxscore = 10.0;
   consistency = 1;
   onlyfeasible = 0;
   onlyinfeasible = 0;
   onlyfail = 0;
   exclude = "";
   texfile = "";
   texincfile = "";
   texsummaryfile = "";
   texsummaryheader = 0;
   texsummaryweight = 0;
   texsummaryshifted = 0;
   texcolorlimit = 5;
   textestset = "";
   texcmpfile = "";
   texcmpfiledir = "";
   texcmpfilename = "";
   thesisnames = 0;
   nsetnames = 0;
   onlygroup = 0;
   group = "default";

   problistlen = 0;
   nsolver = 0;
   nprobs[nsolver] = 0;
   fulltotaltime = 0.0;
}
/^=group=/ {
   group = $2;
}
/^=setname= / {
   if( setorder[$2] == 0 )
   {
      nsetnames++;
      setorder[$2] = nsetnames;
      setname[$2] = $3;
      for( i = 4; i <= NF; i++ )
         setname[$2] = setname[$2]" "$i;
   }
   setingroup[$2,group] = 1;
}
/^@02 timelimit: / {
   timelimit[nsolver] = $3;
}
/^@01 / {
   if( onlygroup == 0 || setingroup[$2,onlygroup] )
   {
      solvername[nsolver] = $2;
      nsolver++;
   }
   nprobs[nsolver] = 0;
}
// {
   # check if this is a useable line
   if( $10 == "ok" || $10 == "timeout" || $10 == "unknown" || $10 == "abort" || $10 == "fail" || $10 == "readerror" ) # CPLEX, CBC
   {
      # collect data
      name[nsolver,nprobs[nsolver]] = $1;
      type[nsolver,nprobs[nsolver]] = "?";
      conss[nsolver,nprobs[nsolver]] = $2;
      vars[nsolver,nprobs[nsolver]] = $3;
      dualbound[nsolver,nprobs[nsolver]] = max(min($4, +infinity), -infinity);
      primalbound[nsolver,nprobs[nsolver]] = max(min($5, +infinity), -infinity);
      gap[nsolver,nprobs[nsolver]] = $6;
      iters[nsolver,nprobs[nsolver]] = $7;
      nodes[nsolver,nprobs[nsolver]] = max($8,1);
      time[nsolver,nprobs[nsolver]] = fracceil(max($9,mintime),0.1);
      status[nsolver,nprobs[nsolver]] = $10;
      probidx[$1,nsolver] = nprobs[nsolver];
      probcnt[$1]++;
      nprobs[nsolver]++;
      if( probcnt[$1] == 1 )
      {
         problist[problistlen] = $1;
         problistlen++;
      }
   }
   else if( $13 == "ok" || $13 == "timeout" || $13 == "unknown" || $13 == "abort" || $13 == "fail" || $13 == "readerror" ) # SCIP
   {
      # collect data (line with original and presolved problem size and simplex iterations)
      name[nsolver,nprobs[nsolver]] = $1;
      type[nsolver,nprobs[nsolver]] = $2;
      conss[nsolver,nprobs[nsolver]] = $5;
      vars[nsolver,nprobs[nsolver]] = $6;
      dualbound[nsolver,nprobs[nsolver]] = max(min($7, +infinity), -infinity);
      primalbound[nsolver,nprobs[nsolver]] = max(min($8, +infinity), -infinity);
      gap[nsolver,nprobs[nsolver]] = $9;
      iters[nsolver,nprobs[nsolver]] = $10;
      nodes[nsolver,nprobs[nsolver]] = max($11,1);
      time[nsolver,nprobs[nsolver]] = fracceil(max($12,mintime),0.1);
      status[nsolver,nprobs[nsolver]] = $13;
      probidx[$1,nsolver] = nprobs[nsolver];
      probcnt[$1]++;
      nprobs[nsolver]++;
      if( probcnt[$1] == 1 )
      {
         problist[problistlen] = $1;
         problistlen++;
      }
   }
}
END {
   if( onlygroup > 0 && nsolver == 1 && solvername[1] == "SCIP:default" )
   {
      printf("only SCIP:default setting found\n");
      exit 1;
   }
   if( nsolver == 0 )
   {
      printf("no instances found in log file\n");
      exit 1;
   }

   # tex comparison file: either directly as 'texcmpfile' or as pair 'texcmpfiledir/texcmpfilename'
   if( texcmpfile == "" && texcmpfiledir != "" && texcmpfilename != "" )
      texcmpfile = texcmpfiledir "/" texcmpfilename;

   # process exclude string
   n = split(exclude, a, ",");
   for( i = 1; i <= n; i++ )
      excluded[a[i]] = 1;

   # initialize means
   for( s = 0; s < nsolver; ++s )
   {
      # cat: 0 - all, 1 - different path, 2 - equal path, 3 - all timeout
      for( cat = 0; cat <= 3; cat++ )
      {
         nevalprobs[s,cat] = 0;
         nprocessedprobs[s,cat] = 0;
         timetotal[s,cat] = 0.0;
         nodetotal[s,cat] = 0.0;
         timegeom[s,cat] = 1.0;
         nodegeom[s,cat] = 1.0;
         timeshiftedgeom[s,cat] = timegeomshift;
         nodeshiftedgeom[s,cat] = nodegeomshift;
         reftimetotal[s,cat] = 0.0;
         refnodetotal[s,cat] = 0.0;
         reftimegeom[s,cat] = 1.0;
         refnodegeom[s,cat] = 1.0;
         reftimeshiftedgeom[s,cat] = timegeomshift;
         refnodeshiftedgeom[s,cat] = nodegeomshift;
         wins[s,cat] = 0;
         nsolved[s,cat] = 0;
         ntimeouts[s,cat] = 0;
         nfails[s,cat] = 0;
         better[s,cat] = 0;
         worse[s,cat] = 0;
         betterobj[s,cat] = 0;
         worseobj[s,cat] = 0;
         feasibles[s,cat] = 0;
         score[s,cat] = 1.0;
      }
   }
   besttimegeom = 1.0;
   bestnodegeom = 1.0;
   besttimeshiftedgeom = timegeomshift;
   bestnodeshiftedgeom = nodegeomshift;
   bestnsolved = 0;
   bestntimeouts = 0;
   bestnfails = 0;
   bestbetter = 0;
   bestbetterobj = 0;
   bestfeasibles = 0;

   # calculate the order in which the columns should be printed: CPLEX < SCIP, default < non-default
   for( s = 0; s < nsolver; ++s )
   {
      sname = solvername[s];
      for( o = 0; o < s; ++o )
      {
         i = printorder[o];
         iname = solvername[i];
         if( nsetnames > 0 )
         {
            # use order given by =setname= entries
            if( setorder[sname] < setorder[iname] )
               break;
         }
         else
         {
            # use alphabetical order, but put CPLEX before SCIP and "default" before all others
            if( substr(sname, 1, 5) == "CPLEX" && substr(iname, 1, 5) != "CPLEX" )
               break;
            if( substr(sname, 1, 5) == substr(iname, 1, 5) &&
               match(sname, "default") != 0 && match(iname, "default") == 0 )
               break;
            if( substr(sname, 1, 5) == substr(iname, 1, 5) &&
               (match(sname, "default") == 0) == (match(iname, "default") == 0) &&
               sname < iname )
               break;
         }
      }
      for( j = s-1; j >= o; --j )
         printorder[j+1] = printorder[j];
      printorder[o] = s;
   }

   # print headers
   for( o = 0; o < nsolver; ++o )
   {
      s = printorder[o];
      if( o == 0 )
         printf(" %39s |", solvername[s]);
      else
      {
         if( length(solvername[s]) <= 33 )
            printf("%33s |", solvername[s]);
	 else
            printf("%34s|", solvername[s]);
      }
   }
   printf("\n");
   printhline(nsolver);
   printf("  Name              |");
   for( s = 0; s < nsolver; ++s )
   {
      if( s == 0 )
         printf("F|   Nodes |   Time |");
      else
         printf("F|   Nodes |   Time | NodQ | TimQ |");
   }
   printf(" bounds check\n");
   printhline(nsolver);

   # tex comparison headers
   if( texcmpfile != "" )
   {
      printf("{\\sffamily\n") > texcmpfile;
      printf("\\scriptsize\n") > texcmpfile;
      printf("\\setlength{\\extrarowheight}{1pt}\n") > texcmpfile;
      printf("\\setlength{\\tabcolsep}{2pt}\n") > texcmpfile;
      printf("\\newcommand{\\g}{\\raisebox{0.25ex}{\\tiny $>$}}\n") > texcmpfile;
      printf("\\newcommand{\\spc}{\\hspace{2em}}\n") > texcmpfile;
      printf("\\begin{tabular*}{\\columnwidth}{@{\\extracolsep{\\fill}}l") > texcmpfile;
      for( s = 0; s < nsolver; ++s )
         printf("@{\\spc}rr") > texcmpfile;
      printf("@{}}\n") > texcmpfile;
      printf("\\toprule\n") > texcmpfile;
      solverextension = "";
      for( o = 0; o < nsolver; ++o )
      {
         s = printorder[o];
         solverextension = solverextension "i";
         printf("& \\multicolumn{2}{@{\\spc}c%s}{\\solvername%s} ", o < nsolver-1 ? "@{\\spc}" : "", solverextension) > texcmpfile;
      }
      printf("\\\\\n") > texcmpfile;
      for( o = 0; o < nsolver; ++o )
         printf("& Nodes & Time ") > texcmpfile;
      printf("\\\\\n") > texcmpfile;
      printf("\\midrule\n") > texcmpfile;
   }

   # display the problem results and calculate mean values
   for( i = 0; i < problistlen; ++i )
   {
      p = problist[i];
      if( length(p) > 18 )
         shortp = substr(p, length(p)-17, 18);
      else
         shortp = p;

      line = sprintf("%-18s", shortp);
      fail = 0;
      readerror = 0;
      unprocessed = 0;
      mindb = +infinity;
      maxdb = -infinity;
      minpb = +infinity;
      maxpb = -infinity;
      itercomp = -1;
      nodecomp = -1;
      timecomp = -1;
      besttime = +infinity;
      bestnodes = +infinity;
      worsttime = -infinity;
      worstnodes = -infinity;
      worstiters = -infinity;
      nthisunprocessed = 0;
      nthissolved = 0;
      nthistimeouts = 0;
      nthisfails = 0;
      ismini = 0;
      ismaxi = 0;
      mark = " ";
      countprob = 1;

      # check for exclusion
      if( excluded[p] )
      {
         unprocessed = 1;
         countprob = 0;
      }

      # find best and worst run and check whether this instance should be counted in overall statistics
      for( s = 0; s < nsolver; ++s )
      {
         pidx = probidx[p,s];
         processed = (pidx != "");

	 # make sure, nodes and time are non-zero for geometric means
	 nodes[s,pidx] = max(nodes[s,pidx], 1);
	 time[s,pidx] = max(time[s,pidx], mintime);
         fulltotaltime += time[s,pidx];

         # If we got a timeout although the time limit has not been reached (e.g., due to a memory limit),
         # we assume that the run would have been continued with the same nodes/sec.
         # Set the time to the time limit and increase the nodes accordingly.
         if( status[s,pidx] == "timeout" && time[s,pidx] < timelimit[s] )
         {
            nodes[s,pidx] *= timelimit[s]/time[s,pidx];
            time[s,pidx] = timelimit[s];
         }

         # if the solver exceeded the timelimit, set status accordingly
         if( (status[s,pidx] == "ok" || status[s,pidx] == "unknown") && timelimit[s] > 0.0 && time[s,pidx] > timelimit[s] )
         {
            status[s,pidx] = "timeout";
            time[s,pidx] = timelimit[s];
         }

         # check if all solvers processed the problem
         if( !processed )
	 {
            marker = "?";
	    unprocessed = 1;
	 }

         # check if solver ran successfully (i.e., no abort nor fail)
         if( processed && (status[s,pidx] == "ok" || status[s,pidx] == "unknown" || status[s,pidx] == "timeout") )
         {
            besttime = min(besttime, time[s,pidx]);
            bestnodes = min(bestnodes, nodes[s,pidx]);
            worsttime = max(worsttime, time[s,pidx]);
	    worstnodes = max(worstnodes, nodes[s,pidx]);
	    worstiters = max(worstiters, iters[s,pidx]);
         }
         else
            countprob = 0;
      }
      worsttime = max(worsttime, mintime);
      worstnodes = max(worstnodes, 1);
      worstiters = max(worstiters, 0);

      # check for each solver if it has same path as reference solver -> category
      for( o = 0; o < nsolver; ++o )
      {
         s = printorder[o];
         pidx = probidx[p,s];
         processed = (pidx != "");

         if( !processed )
            continue;
         
         if( nodecomp == -1 )
         {
            itercomp = iters[s,pidx];
            nodecomp = nodes[s,pidx];
            timecomp = time[s,pidx];
            timeoutcomp = (status[s,pidx] == "timeout");
         }
         iseqpath = (iters[s,pidx] == itercomp && nodes[s,pidx] == nodecomp);
         hastimeout = timeoutcomp || (status[s,pidx] == "timeout");

         # which category?
         if( hastimeout )
            category[s] = 3;
         else if( iseqpath )
            category[s] = 2;
         else
            category[s] = 1;
      }

      # evaluate instance for all solvers
      for( o = 0; o < nsolver; ++o )
      {
         s = printorder[o];
         pidx = probidx[p,s];
         processed = (pidx != "");
         if( processed && name[s,pidx] != p )
            printf("Error: solver %d, probidx %d, <%s> != <%s>\n", solvername[s], pidx, name[s,pidx], p);

         # check if solver ran successfully (i.e., no abort nor fail)
         if( processed )
	 {
            if( status[s,pidx] == "ok" || status[s,pidx] == "unknown" )
            {
               marker = " ";
               if( !unprocessed )
               {
                  nsolved[s,0]++;
                  nsolved[s,category[s]]++;
                  nthissolved++;
               }
            }
            else if( status[s,pidx] == "timeout" )
            {
               marker = ">";
               if( !unprocessed )
               {
                  # if memory limit was exceeded or we hit a hard time/memory limit,
                  # replace time and nodes by worst time and worst nodes of all runs
                  if( time[s,pidx] < 0.99*worsttime || nodes[s,pidx] <= 1 )
                  {
                     iters[s,pidx] = worstiters+s; # make sure this is not treated as equal path
                     nodes[s,pidx] = worstnodes;
                     time[s,pidx] = worsttime;
                  }
                  if( countprob )
                  {
                     ntimeouts[s,0]++;
                     ntimeouts[s,category[s]]++;
                     nthistimeouts++;
                  }
               }
            }
            else
            {
               marker = "!";
               fail = 1;
               if( status[s,pidx] == "readerror" )
                  readerror = 1;
               if( !unprocessed )
               {
                  nfails[s,0]++;
                  nfails[s,category[s]]++;
                  nthisfails++;
               }
            }
         }

	 if( primalbound[s,pidx] < infinity )
	    feasmark = " ";
	 else
	    feasmark = "#";

         if( processed && !fail )
	 {
	    mindb = min(mindb, dualbound[s,pidx]);
            maxdb = max(maxdb, dualbound[s,pidx]);
            minpb = min(minpb, primalbound[s,pidx]);
	    maxpb = max(maxpb, primalbound[s,pidx]);
	    ismini = ismini || (primalbound[s,pidx] > dualbound[s,pidx] + 1e-06);
	    ismaxi = ismaxi || (primalbound[s,pidx] < dualbound[s,pidx] - 1e-06);
	 }

         # print statistics
         if( !processed )
            line = sprintf("%s           -        -", line);
         else
            line = sprintf("%s %s%10d %s%7.1f", line, feasmark, nodes[s,pidx], marker, time[s,pidx]);
         if( o > 0 )
         {
            if( !processed )
               line = sprintf("%s      -", line);
            else if( nodes[s,pidx]/nodecomp > 999.99 )
               line = sprintf("%s  Large", line);
            else
               line = sprintf("%s %6.2f", line, nodes[s,pidx]/nodecomp);
            if( !processed )
               line = sprintf("%s      -", line);
            else if( time[s,pidx]/timecomp > 999.99 )
               line = sprintf("%s  Large", line);
            else
	       line = sprintf("%s %6.2f", line, time[s,pidx]/timecomp);
	    if( processed &&
		(nodes[s,pidx] > markworsenodes * nodecomp ||
		 nodes[s,pidx] < 1.0/markbetternodes * nodecomp ||
		 isfaster(time[s,pidx], timecomp, markbettertime) ||
		 isslower(time[s,pidx], timecomp, markworsetime)) )
	       mark = "*";
         }
      }

      # update the best status information
      if( nthissolved > 0 )
	bestnsolved++;
      else if( nthistimeouts > 0 )
	bestntimeouts++;
      else if( nthisfails == nsolver - nthisunprocessed )
	bestnfails++;

      # check for inconsistency in the primal and dual bounds
      if( readerror )
      {
         line = sprintf("%s  readerror", line);
	 mark = " ";
      }
      else if( fail )
      {
         line = sprintf("%s  fail", line);
	 mark = " ";
      }
      else if( consistency &&
         ((ismini && ismaxi) ||
            (ismini && maxdb - minpb > 1e-5 * max(max(abs(maxdb), abs(minpb)), 1.0)) ||
            (ismaxi && maxpb - mindb > 1e-5 * max(max(abs(maxpb), abs(mindb)), 1.0)) ||
            (!ismini && !ismaxi && abs(maxpb - minpb) > 1e-5 * max(abs(maxpb), 1.0))) )
      {
         line = sprintf("%s  inconsistent", line);
         fail = 1;
	 mark = " ";
      }
      else if( excluded[p] )
      {
         line = sprintf("%s  excluded", line);
	 mark = " ";
      }
      else if( unprocessed )
      {
         line = sprintf("%s  unprocessed", line);
	 mark = " ";
      }
      else
         line = sprintf("%s  ok", line);

      # calculate number of instances for which feasible solution has been found
      hasfeasible = 0;
      if( !unprocessed )
      {
         for( s = 0; s < nsolver; ++s )
         {
            nprocessedprobs[s,0]++;
            nprocessedprobs[s,category[s]]++;
            pidx = probidx[p,s];
	    if( primalbound[s,pidx] < infinity ) {
	       feasibles[s,0]++;
	       feasibles[s,category[s]]++;
	       hasfeasible = 1;
	    }
	 }
	 if( hasfeasible )
	   bestfeasibles++;
      }

      if( (!onlymarked || mark == "*") && (!onlyprocessed || !unprocessed) &&
          (!onlyfeasible || hasfeasible) && (!onlyinfeasible || !hasfeasible) &&
          (!onlyfail || fail) )
      {
         printf("%s %s\n", mark, line);

         # tex comparison file
         if( texcmpfile != "" )
         {
            printf("%s ", texstring(p)) > texcmpfile;
            ref = printorder[0];
            refnodes = nodes[ref,probidx[p,ref]];
            reftime = time[ref,probidx[p,ref]];
            refstatus = status[ref,probidx[p,ref]];
            for( o = 0; o < nsolver; ++o )
            {
               s = printorder[o];
               pidx = probidx[p,s];

               if( status[s,pidx] == "timeout" )
                  timeoutmarker = "\\g";
               else
                  timeoutmarker = "  ";

               if( nodes[s,pidx] <= 0.5*refnodes && status[s,pidx] != "timeout" )
                  nodecolor = "red";
               else if( nodes[s,pidx] >= 2.0*refnodes && refstatus != "timeout" )
                  nodecolor = "blue";
               else
                  nodecolor = "black";

               if( (time[s,pidx] <= 0.5*reftime && status[s,pidx] != "timeout") ||
                  (status[s,pidx] != "timeout" && refstatus == "timeout") )
                  timecolor = "red";
               else if( (time[s,pidx] >= 2.0*reftime && refstatus != "timeout") ||
                  (status[s,pidx] == "timeout" && refstatus != "timeout") )
                  timecolor = "blue";
               else
                  timecolor = "black";

               if( status[s,pidx] == "ok" || status[s,pidx] == "unknown" || status[s,pidx] == "timeout" )
                  printf("&\\textcolor{%s}{%s %8s} &\\textcolor{%s}{%s %8.1f} ",
                     nodecolor, timeoutmarker, texint(nodes[s,pidx]), timecolor, timeoutmarker, time[s,pidx]) > texcmpfile;
               else
                  printf("&        --- &        --- ") > texcmpfile;
            }
            printf("\\\\\n") > texcmpfile;


         }
      }

      # calculate totals and means for instances where no solver failed
      if( !fail && !unprocessed &&
          (!onlyfeasible || hasfeasible) && (!onlyinfeasible || !hasfeasible) )
      {
	 reftime = time[printorder[0],probidx[p,printorder[0]]];
         refnodes = nodes[printorder[0],probidx[p,printorder[0]]];
	 refobj = primalbound[printorder[0],probidx[p,printorder[0]]];
         hasbetter = 0;
	 hasbetterobj = 0;
         for( s = 0; s < nsolver; ++s )
         {
            pidx = probidx[p,s];
            for( cat = 0; cat <= 3; cat = 3*cat + category[s] )
            {
               nevalprobs[s,cat]++;
               nep = nevalprobs[s,cat];
               timetotal[s,cat] += time[s,pidx];
               nodetotal[s,cat] += nodes[s,pidx];
               timegeom[s,cat] = timegeom[s,cat]^((nep-1)/nep) * time[s,pidx]^(1.0/nep);
               nodegeom[s,cat] = nodegeom[s,cat]^((nep-1)/nep) * nodes[s,pidx]^(1.0/nep);
               timeshiftedgeom[s,cat] = timeshiftedgeom[s,cat]^((nep-1)/nep) * (time[s,pidx]+timegeomshift)^(1.0/nep);
               nodeshiftedgeom[s,cat] = nodeshiftedgeom[s,cat]^((nep-1)/nep) * (nodes[s,pidx]+nodegeomshift)^(1.0/nep);
               reftimetotal[s,cat] += reftime;
               refnodetotal[s,cat] += refnodes;
               reftimegeom[s,cat] = reftimegeom[s,cat]^((nep-1)/nep) * reftime^(1.0/nep);
               refnodegeom[s,cat] = refnodegeom[s,cat]^((nep-1)/nep) * refnodes^(1.0/nep);
               reftimeshiftedgeom[s,cat] = reftimeshiftedgeom[s,cat]^((nep-1)/nep) * (reftime+timegeomshift)^(1.0/nep);
               refnodeshiftedgeom[s,cat] = refnodeshiftedgeom[s,cat]^((nep-1)/nep) * (refnodes+nodegeomshift)^(1.0/nep);
               if( time[s,pidx] <= wintolerance*besttime )
                  wins[s,cat]++;
               if( isfaster(time[s,pidx], reftime, wintolerance) )
               {
                  better[s,cat]++;
                  hasbetter = 1;
               }
               else if( isslower(time[s,pidx], reftime, wintolerance) )
                  worse[s,cat]++;
               pb = primalbound[s,pidx];
               if( (ismini && pb - refobj < -0.01 * max(max(abs(refobj), abs(pb)), 1.0)) ||
                   (ismaxi && pb - refobj > +0.01 * max(max(abs(refobj), abs(pb)), 1.0)) ) {
                  betterobj[s,cat]++;
                  hasbetterobj = 1;
               }
               else if( (ismini && pb - refobj > +0.01 * max(max(abs(refobj), abs(pb)), 1.0)) ||
                        (ismaxi && pb - refobj < -0.01 * max(max(abs(refobj), abs(pb)), 1.0)) )
                  worseobj[s,cat]++;
               thisscore = reftime/time[s,pidx];
               thisscore = max(thisscore, 1/maxscore);
               thisscore = min(thisscore, maxscore);
               score[s,cat] = score[s,cat]^((nep-1)/nep) * thisscore^(1.0/nep);
            }
         }
         s = printorder[0];
	 besttimegeom = besttimegeom^((nevalprobs[s,0]-1)/nevalprobs[s,0]) * besttime^(1.0/nevalprobs[s,0]);
	 bestnodegeom = bestnodegeom^((nevalprobs[s,0]-1)/nevalprobs[s,0]) * bestnodes^(1.0/nevalprobs[s,0]);
	 besttimeshiftedgeom = besttimeshiftedgeom^((nevalprobs[s,0]-1)/nevalprobs[s,0]) * (besttime+timegeomshift)^(1.0/nevalprobs[s,0]);
	 bestnodeshiftedgeom = bestnodeshiftedgeom^((nevalprobs[s,0]-1)/nevalprobs[s,0]) * (bestnodes+nodegeomshift)^(1.0/nevalprobs[s,0]);
         if( hasbetter )
            bestbetter++;
	 if( hasbetterobj )
	   bestbetterobj++;
      }
   }
   printhline(nsolver);

   # make sure total time and nodes it not zero
   for( s = 0; s < nsolver; ++s )
   {
      for( cat = 0; cat <= 3; cat++ )
      {
         nodetotal[s,cat] = max(nodetotal[s,cat], 1);
         refnodetotal[s,cat] = max(refnodetotal[s,cat], 1);
         timetotal[s,cat] = max(timetotal[s,cat], mintime);
         reftimetotal[s,cat] = max(reftimetotal[s,cat], mintime);
      }
   }

   # print solvers' overall statistics
   probnumstr = "("nevalprobs[printorder[0],0]")";
   printf("%-14s %5s", "total", probnumstr);
   for( o = 0; o < nsolver; ++o )
   {
      s = printorder[o];
      if( o == 0 )
         printf(" %11d %8d", nodetotal[s,0], timetotal[s,0]);
      else
         printf(" %11d %8d              ", nodetotal[s,0], timetotal[s,0]);
   }
   printf("\n");
   printf("%-20s", "geom. mean");
   for( o = 0; o < nsolver; ++o )
   {
      s = printorder[o];
      if( o == 0 )
      {
         printf(" %11d %8.1f", nodegeom[s,0], timegeom[s,0]);
         nodegeomcomp = nodegeom[s,0];
         timegeomcomp = timegeom[s,0];
         nodetotalcomp = nodetotal[s,0];
         timetotalcomp = timetotal[s,0];
      }
      else
         printf(" %11d %8.1f %6.2f %6.2f", nodegeom[s,0], timegeom[s,0], nodegeom[s,0]/nodegeomcomp, timegeom[s,0]/timegeomcomp);
   }
   printf("\n");
   printf("%-20s", "shifted geom.");
   for( o = 0; o < nsolver; ++o )
   {
      s = printorder[o];
      for( cat = 0; cat <= 3; cat++ )
      {
         nodeshiftedgeom[s,cat] -= nodegeomshift;
         timeshiftedgeom[s,cat] -= timegeomshift;
         nodeshiftedgeom[s,cat] = max(nodeshiftedgeom[s,cat], mintime);
         timeshiftedgeom[s,cat] = max(timeshiftedgeom[s,cat], mintime);
         refnodeshiftedgeom[s,cat] -= nodegeomshift;
         reftimeshiftedgeom[s,cat] -= timegeomshift;
         refnodeshiftedgeom[s,cat] = max(refnodeshiftedgeom[s,cat], mintime);
         reftimeshiftedgeom[s,cat] = max(reftimeshiftedgeom[s,cat], mintime);
      }
      if( o == 0 )
      {
         printf(" %11d %8.1f", nodeshiftedgeom[s,0], timeshiftedgeom[s,0]);
         nodeshiftedgeomcomp = nodeshiftedgeom[s,0];
         timeshiftedgeomcomp = timeshiftedgeom[s,0];
      }
      else
         printf(" %11d %8.1f %6.2f %6.2f", nodeshiftedgeom[s,0], timeshiftedgeom[s,0],
            nodeshiftedgeom[s,0]/nodeshiftedgeomcomp, timeshiftedgeom[s,0]/timeshiftedgeomcomp);
   }
   bestnodeshiftedgeom -= nodegeomshift;
   besttimeshiftedgeom -= timegeomshift;
   bestnodeshiftedgeom = max(bestnodeshiftedgeom, 1.0);
   besttimeshiftedgeom = max(besttimeshiftedgeom, 1.0);
   
   printf("\n");
   printhline(nsolver);

   # tex comparison footer
   if( texcmpfile != "" )
   {
      printf("\\midrule\n") > texcmpfile;
      printf("geom. mean     ") > texcmpfile;
      for( o = 0; o < nsolver; ++o )
      {
         s = printorder[o];
         printf("& %8s & %8.1f", texint(nodegeom[s,0]), timegeom[s,0]) > texcmpfile;
      }
      printf("\\\\\n") > texcmpfile;
      printf("sh. geom. mean ") > texcmpfile;
      for( o = 0; o < nsolver; ++o )
      {
         s = printorder[o];
         printf("& %8s & %8.1f", texint(nodeshiftedgeom[s,0]), timeshiftedgeom[s,0]) > texcmpfile;
      }
      printf("\\\\\n") > texcmpfile;
      printf("arithm. mean   ") > texcmpfile;
      for( o = 0; o < nsolver; ++o )
      {
         s = printorder[o];
         printf("& %8s & %8.1f", texint(nodetotal[s,0]/nevalprobs[s,0]), timetotal[s,0]/nevalprobs[s,0]) > texcmpfile;
      }
      printf("\\\\\n") > texcmpfile;
      printf("\\bottomrule\n") > texcmpfile;
      printf("\\end{tabular*}\n") > texcmpfile;
      printf("}\n") > texcmpfile;
   }


   for( cat = 0; cat <= 3; cat++ )
   {
#      if( nprocessedprobs[cat] == 0 )
#         continue;

      header = (cat == 0 ? "all" : (cat == 1 ? "diff" : (cat == 2 ? "equal" : "timeout")));
      printf("\n");
      printf("%-7s                             proc eval fail time solv wins bett wors bobj wobj feas     nodes   shnodes    nodesQ  shnodesQ    time  shtime   timeQ shtimeQ   score\n",
         header);

      for( o = 0; o < nsolver; ++o )
      {
         s = printorder[o];
         if( o == 0 )
         {
            nodegeomcomp = nodegeom[s,cat];
            timegeomcomp = timegeom[s,cat];
            nodeshiftedgeomcomp = nodeshiftedgeom[s,cat];
            timeshiftedgeomcomp = timeshiftedgeom[s,cat];
         }
         if( (o > 0 || cat == 0) && nevalprobs[s,cat] > 0 )
         {
            printf("%-35s %4d %4d %4d %4d %4d %4d", solvername[s], nprocessedprobs[s,cat], nevalprobs[s,cat], nfails[s,cat],
               ntimeouts[s,cat], nsolved[s,cat], wins[s,cat]);
            printf(" %4d %4d", better[s,cat], worse[s,cat]);
            printf(" %4d %4d %4d %9d %9d %9.2f %9.2f %7.1f %7.1f %7.2f %7.2f %7.2f\n", 
               betterobj[s,cat], worseobj[s,cat], feasibles[s,cat],
               nodegeom[s,cat], nodeshiftedgeom[s,cat], nodegeom[s,cat]/refnodegeom[s,cat],
               nodeshiftedgeom[s,cat]/refnodeshiftedgeom[s,cat],
               timegeom[s,cat], timeshiftedgeom[s,cat], timegeom[s,cat]/reftimegeom[s,cat],
               timeshiftedgeom[s,cat]/reftimeshiftedgeom[s,cat], score[s,cat]);
         }
      }
      if( cat == 0 )
      {
         printf("%-35s           %4d %4d %4d %4s", "optimal auto settings", bestnfails, bestntimeouts, bestnsolved, "");
         printf(" %4d %4s", bestbetter, "");
         printf(" %4d %4s %4d %9d %9d %9.2f %9.2f %7.1f %7.1f %7.2f %7.2f %7s\n",
                bestbetterobj, "", bestfeasibles,
                bestnodegeom, bestnodeshiftedgeom, bestnodegeom/nodegeomcomp, bestnodeshiftedgeom/nodeshiftedgeomcomp,
                besttimegeom, besttimeshiftedgeom, besttimegeom/timegeomcomp, besttimeshiftedgeom/timeshiftedgeomcomp,
                "");
      }
   }

   printf("\n");
   printf("total time over all settings: %.1f sec = %.1f hours = %.1f days = %.1f weeks = %.1f months\n",
      fulltotaltime, fulltotaltime/3600.0, fulltotaltime/(3600.0*24), fulltotaltime/(3600.0*24*7),
      fulltotaltime/(3600.0*24*30));

   # generate tex file
   if( texfile != "" )
   {
      hasequalpath = 0;
      for( o = 0; o < nsolver; ++o )
      {
         if( nevalprobs[s,2] > 0 )
         {
            hasequalpath = 1;
            break;
         }
      }

      printf("generating tex file <%s>\n", texfile);
      printf("{\\sffamily\n") > texfile;
      printf("\\scriptsize\n") > texfile;
      printf("\\setlength{\\extrarowheight}{1pt}\n") > texfile;
      printf("\\setlength{\\tabcolsep}{2pt}\n") > texfile;
      printf("\\newcommand{\\spc}{\\hspace{%dem}}\n", 2-hasequalpath) > texfile;

      printf("\\begin{tabular*}{\\columnwidth}{@{\\extracolsep{\\fill}}lrrr@{\\spc}rrrrrr@{\\spc}rrrr") > texfile;
      if( hasequalpath )
         printf("@{\\spc}rrrr") > texfile;
      printf("@{}}\n") > texfile;

      printf("\\toprule\n") > texfile;

      printf("& & & & \\multicolumn{6}{c@{\\spc}}{all instances (%d)} & \\multicolumn{4}{c@{\\spc}}{different path}",
         nevalprobs[printorder[0],0]) > texfile;
      if( hasequalpath )
         printf("& \\multicolumn{4}{c}{equal path}") > texfile;
      printf("\\\\\n") > texfile;

      printf("setting & T & fst & slw & $\\textit{n}_\\textit{gm}$ & $\\textit{n}_\\textit{sgm}$ & $\\textit{n}_\\textit{tot}$ & $\\textit{t}_\\textit{gm}$ & $\\textit{t}_\\textit{sgm}$ & $\\textit{t}_\\textit{tot}$ & \\# & $\\textit{t}_\\textit{gm}$ & $\\textit{t}_\\textit{sgm}$ & $\\textit{t}_\\textit{tot}$") > texfile;
      if( hasequalpath )
         printf("& \\# & $\\textit{t}_\\textit{gm}$ & $\\textit{t}_\\textit{sgm}$ & $\\textit{t}_\\textit{tot}$") > texfile;
      printf("\\\\\n") > texfile;

      printf("\\midrule\n") > texfile;

      for( o = 0; o < nsolver; ++o )
      {
         s = printorder[o];
         printf("%-35s & %4d & %3d & %3d", texsolvername(s), ntimeouts[s,0],  better[s,0], worse[s,0]) > texfile;
         printf(" & %5s & %5s & %5s & %5s & %5s & %5s",
            texcompstr(nodegeom[s,0], refnodegeom[s,0]),
            texcompstr(nodeshiftedgeom[s,0], refnodeshiftedgeom[s,0]),
            texcompstr(nodetotal[s,0], refnodetotal[s,0]),
            texcompstr(timegeom[s,0], reftimegeom[s,0]),
            texcompstr(timeshiftedgeom[s,0], reftimeshiftedgeom[s,0]),
            texcompstr(timetotal[s,0], reftimetotal[s,0])) > texfile;
         if( nevalprobs[s,1] > 0 )
         {
            printf(" & %2d & %5s & %5s & %5s",
               nevalprobs[s,1],
               texcompstr(timegeom[s,1], reftimegeom[s,1]),
               texcompstr(timeshiftedgeom[s,1], reftimeshiftedgeom[s,1]),
               texcompstr(timetotal[s,1], reftimetotal[s,1])) > texfile;
         }
         else
            printf(" &  0 &     --- &     --- &     ---") > texfile;
         if( hasequalpath )
         {
            if( nevalprobs[s,2] > 0 )
            {
               printf(" & %2d & %5s & %5s & %5s",
                  nevalprobs[s,2],
                  texcompstr(timegeom[s,2], reftimegeom[s,2]),
                  texcompstr(timeshiftedgeom[s,2], reftimeshiftedgeom[s,2]),
                  texcompstr(timetotal[s,2], reftimetotal[s,2])) > texfile;
            }
            else
               printf(" &  0 &     --- &     --- &     ---") > texfile;
         }
         printf(" \\\\\n") > texfile;
      }

      printf("\\bottomrule\n") > texfile;
      printf("\\end{tabular*}\n") > texfile;
      printf("}\n") > texfile;

      # extend tex include file
      if( texincfile != "" )
      {
         n = split(texfile, a, "/");
         texpath = "";
         for( i = 1; i < n; i++ )
            texpath = texpath a[i] "/";
         texbase = a[n];
         sub(/\.tex/, "", texbase);
         n = split(texbase, a, "_");
         texsetname = a[2];
         texgroupname = a[3];
         textestname = a[4];

         printf("\n") >> texincfile;
         printf("\\begin{table}[hp]\n") > texincfile;
         printf("\\input{Tables/mip/auto/%s}\n", texbase) > texincfile;
         printf("\\smalltabcaption{\\label{table_%s_%s_%s}\n", texsetname, texgroupname, textestname) > texincfile;
         printf("Evaluation of \\setting%s%s on test set \\testset{%s}.}\n", texsetname, texgroupname, textestname) > texincfile;
         printf("\\end{table}\n") > texincfile;
         printf("\n") > texincfile;
      }
   }

   # generate (or extend) tex summary file
   if( texsummaryfile != "" )
   {
      n = split(texsummaryfile, a, "/");
      texsummarypath = "";
      for( i = 1; i < n; i++ )
         texsummarypath = texsummarypath a[i] "/";
      texsummarybase = a[n];
      sub(/\.tex/, "", texsummarybase);
      texsummaryfiletime = texsummarypath texsummarybase "_time.tex";
      texsummaryfilenodes = texsummarypath texsummarybase "_nodes.tex";
      if( texsummaryheader > 0 )
      {
         printf("{\\sffamily\n") > texsummaryfile;
         printf("\\scriptsize\n") > texsummaryfile;
         printf("\\setlength{\\extrarowheight}{1pt}\n") > texsummaryfile;
         printf("\\setlength{\\tabcolsep}{2pt}\n") > texsummaryfile;
         printf("\\newcommand{\\spc}{\\hspace{1em}}\n") > texsummaryfile;
         for( si = 0; si <= 2; si++ )
         {
            printf("\\ifthenelse{\\summaryinfo = %d}{\n", si) > texsummaryfile;
            printf("\\begin{tabular*}{\\columnwidth}{@{}ll@{\\extracolsep{\\fill}}") > texsummaryfile;
            for( o = 1; o < nsolver; o++ )
               printf("r") > texsummaryfile;
            printf("@{}}\n") > texsummaryfile;
            printf("\\toprule\n") > texsummaryfile;
            printf("& test set") > texsummaryfile;
            if( nsolver >= 9 )
            {
               for( o = 1; o < nsolver; o++ )
                  printf(" & %s", texsolvername(printorder[o])) > texsummaryfile;
            }
            else
            {
               for( o = 1; o < nsolver; o++ )
                  printf(" & \\makebox[0em][r]{%s}", texsolvername(printorder[o])) > texsummaryfile;
            }
            printf(" \\\\\n") > texsummaryfile;
            if( si == 0 || si == 1 )
            {
               printf("\\midrule\n") > texsummaryfile;
               printf("\\input{Tables/mip/auto/%s_time}\n", texsummarybase) > texsummaryfile;
            }
            if( si == 0 || si == 2 )
            {
               printf("\\midrule\n") > texsummaryfile;
               printf("\\input{Tables/mip/auto/%s_nodes}\n", texsummarybase) > texsummaryfile;
            }
            printf("\\bottomrule\n") >> texsummaryfile;
            printf("\\end{tabular*}\n") >> texsummaryfile;
            printf("}{}\n") >> texsummaryfile;
         }
         printf("}\n") > texsummaryfile;
         printf("\\raisebox{-%.1fex}[0em][0em]{\\rotatebox{90}{\\makebox[3em]{time}}}",
            1.5*(texsummaryheader+1)) > texsummaryfiletime;
         printf("\\raisebox{-%.1fex}[0em][0em]{\\rotatebox{90}{\\makebox[3em]{nodes}}}",
            1.5*(texsummaryheader+1)) > texsummaryfilenodes;
      }
      printf("& \\testset{%s}", textestset) >> texsummaryfiletime;
      for( o = 1; o < nsolver; o++ )
      {
         s = printorder[o];
         if( texsummaryshifted )
            printf(" & %s", texcompstr(timeshiftedgeom[s,0], reftimeshiftedgeom[s,0])) > texsummaryfiletime;
         else
            printf(" & %s", texcompstr(timegeom[s,0], reftimegeom[s,0])) > texsummaryfiletime;
      }
      printf("\\\\\n") > texsummaryfiletime;
      printf("& \\testset{%s}", textestset) >> texsummaryfilenodes;
      for( o = 1; o < nsolver; o++ )
      {
         s = printorder[o];
         if( texsummaryshifted )
            printf(" & %s", texcompstr(nodeshiftedgeom[s,0], refnodeshiftedgeom[s,0])) > texsummaryfilenodes;
         else
            printf(" & %s", texcompstr(nodegeom[s,0], refnodegeom[s,0])) > texsummaryfilenodes;
      }
      printf("\\\\\n") > texsummaryfilenodes;

      # add tex comment to summary file which is later be used to generate overall statistics
      for( o = 1; o < nsolver; o++ )
      {
         s = printorder[o];
         weight = (texsummaryweight == 0 ? nevalprobs[s,0] : texsummaryweight);
         if( texsummaryshifted )
            printf("%% =mean=  %s %.4f %.4f %g\n", solvername[s], 
               timeshiftedgeom[s,0]/reftimeshiftedgeom[s,0], nodeshiftedgeom[s,0]/refnodeshiftedgeom[s,0],
               weight) >> texsummaryfile;
         else
            printf("%% =mean=  %s %.4f %.4f %g\n", solvername[s], 
               timegeom[s,0]/reftimegeom[s,0], nodegeom[s,0]/refnodegeom[s,0], weight) >> texsummaryfile;
      }
   }
}
