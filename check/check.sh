#!/bin/bash
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
TSTNAME=$1
BINNAME=$2
SETNAME=$3
BINID=$4
TIMELIMIT=$5
NODELIMIT=$6
MEMLIMIT=$7
FEASTOL=$8
DISPFREQ=$9
CONTINUE=${10}
LOCK=${11}
VERSION=${12}

SETDIR=../settings

if test ! -e results
then
    mkdir results
fi
if test ! -e locks
then
    mkdir locks
fi

LOCKFILE=locks/$TSTNAME.$SETNAME.$VERSION.lock
RUNFILE=locks/$TSTNAME.$SETNAME.$VERSION.run.$BINID
DONEFILE=locks/$TSTNAME.$SETNAME.$VERSION.done

OUTFILE=results/check.$TSTNAME.$BINID.$SETNAME.out
ERRFILE=results/check.$TSTNAME.$BINID.$SETNAME.err
RESFILE=results/check.$TSTNAME.$BINID.$SETNAME.res
TEXFILE=results/check.$TSTNAME.$BINID.$SETNAME.tex
TMPFILE=results/check.$TSTNAME.$BINID.$SETNAME.tmp
SETFILE=results/check.$TSTNAME.$BINID.$SETNAME.set

SETTINGS=$SETDIR/$SETNAME.set

if test "$LOCK" == "true"
then
    if test -e $DONEFILE
    then
	echo skipping test due to existing done file $DONEFILE
	exit
    fi
    if test -e $LOCKFILE
    then
	if test -e $RUNFILE
        then
	    echo continuing aborted run with run file $RUNFILE
	else
	    echo skipping test due to existing lock file $LOCKFILE
	    exit
	fi
    fi
    date > $LOCKFILE
    date > $RUNFILE
fi

if test ! -e $OUTFILE
then
    CONTINUE=false
fi

if test "$CONTINUE" == "true"
then
    MVORCP=cp
else
    MVORCP=mv
fi

DATEINT=`date +"%s"`
if test -e $OUTFILE
then
    $MVORCP $OUTFILE $OUTFILE.old-$DATEINT
fi
if test -e $ERRFILE
then
    $MVORCP $ERRFILE $ERRFILE.old-$DATEINT
fi

if test "$CONTINUE" == "true"
then
    LASTPROB=`getlastprob.awk $OUTFILE`
    echo Continuing benchmark. Last solved instance: $LASTPROB
    echo "" >> $OUTFILE
    echo "----- Continuing from here. Last solved: $LASTPROB -----" >> $OUTFILE
    echo "" >> $OUTFILE
else
    LASTPROB=""
fi

uname -a >>$OUTFILE
uname -a >>$ERRFILE
date >>$OUTFILE
date >>$ERRFILE

HARDTIMELIMIT=`echo "($TIMELIMIT*1.2)+10" | bc`
HARDMEMLIMIT=`echo "($MEMLIMIT*1.1+10)*1024" | bc`

echo "hard time limit: $HARDTIMELIMIT s" >>$OUTFILE
echo "hard mem limit: $HARDMEMLIMIT k" >>$OUTFILE

for i in `cat $TSTNAME.test` DONE
do
    if test "$i" == "DONE"
    then
	date > $DONEFILE
	break
    fi

    if test "$LASTPROB" == ""
    then
	blkfile=`echo $i | sed 's/lp/blk/g'`
	LASTPROB=""
	if test -f $i
	then
	    echo @01 $i ===========
	    echo @01 $blkfile ===========
	    echo @01 $i ===========      >> $ERRFILE
	    echo set load $SETTINGS                >  $TMPFILE
	    if test $FEASTOL != "default"
	    then
		echo set numerics feastol $FEASTOL    >> $TMPFILE
	    fi
	    echo set limits time $TIMELIMIT        >> $TMPFILE
	    echo set limits nodes $NODELIMIT       >> $TMPFILE
	    echo set limits memory $MEMLIMIT       >> $TMPFILE
	    echo set timing clocktype 1            >> $TMPFILE
	    echo set display verblevel 4           >> $TMPFILE
	    echo set display freq $DISPFREQ        >> $TMPFILE
	    echo set memory savefac 1.0            >> $TMPFILE # avoid switching to dfs - better abort with memory error
	    echo set save $SETFILE                 >> $TMPFILE
	    echo read $i                           >> $TMPFILE
	    echo read $blkfile                     >> $TMPFILE
	    echo optimize                          >> $TMPFILE
	    echo display statistics                >> $TMPFILE
#	    echo display solution                  >> $TMPFILE
	    echo checksol                          >> $TMPFILE
	    echo quit                              >> $TMPFILE

#	    waitcplex.sh # ??????????????????

	    echo -----------------------------
	    date
	    date >>$ERRFILE
	    echo -----------------------------
	    date +"@03 %s"
	    tcsh -c "limit cputime $HARDTIMELIMIT s; limit memoryuse $HARDMEMLIMIT k; limit filesize 200 M; ../$2 < $TMPFILE" 2>>$ERRFILE
	    date +"@04 %s"
	    echo -----------------------------
	    date
	    date >>$ERRFILE
	    echo -----------------------------
	    echo
	    echo =ready=
	else
	    echo @02 FILE NOT FOUND: $i ===========
	    echo @02 FILE NOT FOUND: $i =========== >>$ERRFILE
	fi
    else
	echo skipping $i
	if test "$LASTPROB" == "$i"
	then
	    LASTPROB=""
        fi
    fi
done | tee -a $OUTFILE

rm -f $TMPFILE

date >>$OUTFILE
date >>$ERRFILE

if test -e $DONEFILE
then
    ./evalcheck.sh $OUTFILE

    if test "$LOCK" == "true"
    then
	rm -f $RUNFILE
    fi
fi
