#!/usr/bin/env bash
#set -x
#* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
#*                                                                           *
#*                  This file is part of the program                         *
#*          GCG --- Generic Column Generation                                *
#*                  a Dantzig-Wolfe decomposition based extension            *
#*                  of the branch-cut-and-price framework                    *
#*         SCIP --- Solving Constraint Integer Programs                      *
#*                                                                           *
#* Copyright (C) 2010-2012 Operations Research, RWTH Aachen University       *
#*                         Zuse Institute Berlin (ZIB)                       *
#*                                                                           *
#* This program is free software; you can redistribute it and/or             *
#* modify it under the terms of the GNU Lesser General Public License        *
#* as published by the Free Software Foundation; either version 3            *
#* of the License, or (at your option) any later version.                    *
#*                                                                           *
#* This program is distributed in the hope that it will be useful,           *
#* but WITHOUT ANY WARRANTY; without even the implied warranty of            *
#* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the             *
#* GNU Lesser General Public License for more details.                       *
#*                                                                           *
#* You should have received a copy of the GNU Lesser General Public License  *
#* along with this program; if not, write to the Free Software               *
#* Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.*
#*                                                                           *
#* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
#
# @author Martin Bergner
# @author Gerald Gamrath
#
# Call with "make testcluster"
#
# The queue is passed via $QUEUE (possibly defined in a local makefile in scip/make/local).
#
# For each run, we can specify the number of nodes reserved for a run via $PPN. If tests runs
# with valid time measurements should be executed, this number should be chosen in such a way
# that a job is run on a single computer, i.e., in general, $PPN should equal the number of cores
# of each computer. Of course, the value depends on the specific computer/queue.
#
# To get the result files call "./evalcheck_cluster.sh
# results/check.$TSTNAME.$BINNAME.$SETNAME.eval in directory check/
# This leads to result files
#  - results/check.$TSTNAME.$BINNMAE.$SETNAME.out
#  - results/check.$TSTNAME.$BINNMAE.$SETNAME.res
#  - results/check.$TSTNAME.$BINNMAE.$SETNAME.err

TSTNAME=$1
BINNAME=$2
SETNAME=$3
BINID=$4
TIMELIMIT=$5
NODELIMIT=$6
MEMLIMIT=$7
THREADS=$8
FEASTOL=$9
DISPFREQ=${10}
CONTINUE=${11}
QUEUETYPE=${12}
QUEUE=${13}
PPN=${14}
CLIENTTMPDIR=${15}
NOWAITCLUSTER=${16}
EXCLUSIVE=${17}
MODE='solve'

# check all variables defined
if [ -z ${EXCLUSIVE} ]
then
    echo Skipping test since EXCLUSIVE is not defined.
    exit 1;
fi


# get current SCIP path
SCIPPATH=`pwd`

if test ! -e $SCIPPATH/results
then
    mkdir $SCIPPATH/results
fi

SETTINGS=$SCIPPATH/../settings/$SETNAME.set

# check if the settings file exists
if test $SETNAME != "default"
then
    if test ! -e $SETTINGS
    then
        echo Skipping test since the settings file $SETTINGS does not exist.
        exit
    fi
fi

# check if binary exists
if test ! -e $SCIPPATH/../$BINNAME
then
    echo Skipping test since the binary $BINNAME does not exist.
    exit
fi

# check if queue has been defined
if test "$QUEUE" = ""
then
    echo Skipping test since the queue name has not been defined.
    exit
fi

# check if number of nodes has been defined
if test "$PPN" = ""
then
    echo Skipping test since the number of nodes has not been defined.
    exit
fi

# check if the slurm blades should be used exclusively
if test "$EXCLUSIVE" = "true"
then
    EXCLUSIVE=" --exclusive"
else
    EXCLUSIVE=""
fi

# we add 100% to the hard time limit and additional 600 seconds in case of small time limits
HARDTIMELIMIT=`expr \`expr $TIMELIMIT + 600\` + $TIMELIMIT`

# we add 10% to the hard memory limit and additional 100MB to the hard memory limit
HARDMEMLIMIT=`expr \`expr $MEMLIMIT + 100\` + \`expr $MEMLIMIT / 10\``

# in case of qsub queue the memory is measured in kB
if test  "$QUEUETYPE" = "qsub"
then
    HARDMEMLIMIT=`expr $HARDMEMLIMIT \* 1024000`
fi

ULIMITMEMLIMIT=`expr $HARDMEMLIMIT \* 1024000`

EVALFILE=$SCIPPATH/results/check.$QUEUE.$TSTNAME.$BINID.$SETNAME.eval
echo > $EVALFILE

# counter to define file names for a test set uniquely
COUNT=1

for i in `cat testset/$TSTNAME.test` DONE
do
  if test "$i" = "DONE"
      then
      break
  fi

  # check if problem instance exists
  if test -f $SCIPPATH/$i
  then

      # the cluster queue has an upper bound of 2000 jobs; if this limit is
      # reached the submitted jobs are dumped; to avoid that we check the total
      # load of the cluster and wait until it is save (total load not more than
      # 1900 jobs) to submit the next job.
      if test "$NOWAITCLUSTER" != "1"
      then
	  ./waitcluster.sh 1600 $QUEUE 200
      fi

      SHORTFILENAME=`basename $i .gz`
      SHORTFILENAME=`basename $SHORTFILENAME .mps`
      SHORTFILENAME=`basename $SHORTFILENAME .lp`
      SHORTFILENAME=`basename $SHORTFILENAME .opb`

      FILENAME=$USER.$QUEUE.$TSTNAME.$COUNT"_"$SHORTFILENAME.$BINID.$SETNAME
      BASENAME=$SCIPPATH/results/$FILENAME

      DIRNAME=`dirname $i`
      DECFILE=$SCIPPATH/$DIRNAME/$SHORTFILENAME.dec.gz

      TMPFILE=$BASENAME.tmp
      SETFILE=$BASENAME.set

      echo $BASENAME >> $EVALFILE

      COUNT=`expr $COUNT + 1`

      # in case we want to continue we check if the job was already performed
      if test "$CONTINUE" != "false"
      then
	  if test -e results/$FILENAME.out
	  then
	      echo skipping file $i due to existing output file $FILENAME.out
	      continue
	  fi
      fi

      echo > $TMPFILE
      if test $SETNAME != "default"
      then
	  echo set load $SETTINGS            >>  $TMPFILE
      fi
      if test $FEASTOL != "default"
      then
	  echo set numerics feastol $FEASTOL >> $TMPFILE
      fi
      echo set limits time $TIMELIMIT        >> $TMPFILE
      echo set limits nodes $NODELIMIT       >> $TMPFILE
      echo set limits memory $MEMLIMIT       >> $TMPFILE
      echo set lp advanced threads $THREADS  >> $TMPFILE
      echo set timing clocktype 1            >> $TMPFILE
      echo set display verblevel 4           >> $TMPFILE
      echo set display freq $DISPFREQ        >> $TMPFILE
      echo set memory savefac 1.0            >> $TMPFILE # avoid switching to dfs - better abort with memory error
      echo set save $SETFILE                 >> $TMPFILE
      echo read $SCIPPATH/$i                 >> $TMPFILE
#  echo presolve                         >> $TMPFILE
      if test $MODE = "detect"
      then
	  echo presolve                      >> $TMPFILE
	  echo detect                        >> $TMPFILE
	  echo display statistics            >> $TMPFILE
	  echo presolve                      >> $TMPFILE
      else
          if test -f $DECFILE -a $MODE = "readdec"
          then
              echo read $DECFILE         >> $TMPFILE
          fi
	  echo optimize                      >> $TMPFILE
	  echo display statistics            >> $TMPFILE
	  echo display additionalstatistics  >> $TMPFILE
#            echo display solution                  >> $TMPFILE
	  echo checksol                      >> $TMPFILE
      fi
      echo quit                              >> $TMPFILE

      # additional environment variables needed by runcluster.sh
      export SOLVERPATH=$SCIPPATH
      export BINNAME=$BINNAME
      export BASENAME=$FILENAME
      export FILENAME=$i
      export CLIENTTMPDIR=$CLIENTTMPDIR

      # check queue type
      if test  "$QUEUETYPE" = "srun"
      then
	  srun --job-name=GCG$SHORTFILENAME --mem=$HARDMEMLIMIT -p $QUEUE --time=${HARDTIMELIMIT}${EXCLUSIVE} runcluster.sh &
      elif test  "$QUEUETYPE" = "bsub"
      then
	  cp runcluster_aachen.sh runcluster_tmp.sh
	  TLIMIT=`expr $HARDTIMELIMIT / 60`
	  sed -i 's,\$CLIENTTMPDIR,$TMP,' runcluster_tmp.sh
	  sed -i "s,\$BASENAME,$BASENAME," runcluster_tmp.sh
	  sed -i "s,\$BINNAME,$BINNAME," runcluster_tmp.sh
	  sed -i "s,\$FILENAME,$FILENAME," runcluster_tmp.sh
	  sed -i "s,\$TLIMIT,$TLIMIT," runcluster_tmp.sh
	  sed -i "s,\$SHORTFILENAME,$SHORTFILENAME," runcluster_tmp.sh
	  sed -i "s,\$HARDMEMLIMIT,$HARDMEMLIMIT," runcluster_tmp.sh
	  sed -i "s,\$ULIMITMEMLIMIT,$ULIMITMEMLIMIT," runcluster_tmp.sh
	  sed -i "s,\$SOLVERPATH,$SOLVERPATH," runcluster_tmp.sh
#	  sed -i "s,,," runcluster_tmp.sh


#	  less runcluster_aachen.sh
#	  bsub -J SCIP$SHORTFILENAME -M $HARDMEMLIMIT -q $QUEUE -W $TLIMIT -o /dev/null < runcluster_tmp.sh &
	  bsub -q $QUEUE -o error/out_$SHORTFILENAME_%I_%J.txt < runcluster_tmp.sh &
#	  bsub -q $QUEUE -o /dev/null < runcluster_tmp.sh &
      elif test  "$QUEUETYPE" = "qsub"
      then
	  cp runcluster_aachen.sh runcluster_tmp.sh
	  sed -i 's/$CLIENTTMPDIR/$TMP/' runcluster_tmp.sh
	  sed -i "s,\$BASENAME,$BASENAME," runcluster_tmp.sh
	  sed -i "s,\$BINNAME,$BINNAME," runcluster_tmp.sh
	  sed -i "s,\$FILENAME,$FILENAME," runcluster_tmp.sh
	  sed -i "s,\$SOLVERPATH,$SOLVERPATH," runcluster_tmp.sh
#	  sed -i "s,,," runcluster_tmp.sh
#	  less runcluster_aachen.sh
	  qsub -l h_rt=$HARDTIMELIMIT -l h_vmem=$HARDMEMLIMIT -l threads=1 -l ostype=linux -N SCIP$SHORTFILENAME -o /dev/null -e /dev/null  runcluster_tmp.sh
#	  qsub -l h_rt=$HARDTIMELIMIT -l h_vmem=$HARDMEMLIMIT -l threads=1 -l ostype=linux -q $QUEUE -N SCIP$SHORTFILENAME  runcluster_tmp.sh
      else
          # -V to copy all environment variables
	  qsub -l walltime=$HARDTIMELIMIT -l mem=$HARDMEMLIMIT -l nodes=1:ppn=$PPN -N SCIP$SHORTFILENAME -V -q $QUEUE -o /dev/null -e /dev/null runcluster.sh
      fi
  else
      echo "input file "$SCIPPATH/$i" not found!"
  fi
done
