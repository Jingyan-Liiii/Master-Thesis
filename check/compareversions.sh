#!/bin/bash

# Open questions: 
#	- Does still script run when a different (not script-containing) version is called by checkout?
#	- Output: do normal out files suffice? Maybe add a nice comparison of the summaries?
#	          if comparison, tolerances, e.g. for slightly different run times? maybe same O(log)?
#	- Checkout different versions simultaniously in different folders? 


#####
# README: 
# The script can be called with parameters of the form 
# "testset" "testparams" "version1" "comparams1" "params1" "version2" "comparams2" "params2" "version3" ...
# If no such parameters are present the script interactively asks to enter these parameters.
# Parameters:
#	- testset:	Testset to run the different versions on
#	- testparams:	Parameters for the test script
#	- versionN:	git branch or hash #N
#	- comparamsN:	compile parameters for GCG version #N
#	- paramsN:	parameters with which to run GCG version #N
#####


# For readability reasons: 
# Even though not necessary please "declare" global variables here if they are guaranteed to exist.
TESTSET=""		# testset on which to run all versions
USERINPUT=""		# temp var for user inputs
VERSIONCOUNTER=0	# stores amount of versions to compare


# 1) If no params were given ask for testset, versions and parameters, and store them.
#    Else store testset and fill other parameters into arrays without interaction.
echo ""
echo "This script will run different versions of GCG using the test script for comparison."

if [ -z $1 ]
then
	# Case that no parameters where given when script was started
	echo "You have not specified what you want to compare yet."
	echo "If you would like to avoid this dialog in the future please call this script with"
	echo "\"testset\" \"testparams\" \"version1\" \"comparams1\" \"params1\" \"version2\" \"comparams2\" \"params2\" ... "
	echo "(where\"testparams\" are parameters for the test script; \"version\" is a git branch or git hash;"
	echo "GCG should be compiled with \"comparams\" and called with \"params\")."
	echo ""

	# Let user specify the testset
	echo "What testset would you like to run?"
	read USERINPUT
	while [ -z $USERINPUT ]
	do
		echo "You forgot to enter the testset. Try again:"
		read USERINPUT
	done
	TESTSET="$USERINPUT"

	# Let user specify test script parameters (which might be empty)
	echo "What parameters would you like the test to run with? (Press Enter for no parameters)"
	read USERINPUT
	TESTPARAMS="$USERINPUT"

	# Set VERSIONCOUNTER manually
	echo "How many different versions of GCG would you like to compare?"
	read USERINPUT
	# Is input a number? evaluate input arithmetically and compare to itself for quick check
	if [ -z "$USERINPUT" ] || [ "$USERINPUT" -eq 0 ] || [[ $((USERINPUT)) != "$USERINPUT" ]]
	then
		echo "None? Ok, goodbye then."
		exit 0
	fi
	VERSIONCOUNTER="$USERINPUT"

	# Let user enter git version and params (all params might be empty)
	# Note: for better user interaction the indices of VERSION, COMPARAMS and PARAMS arrays start at 1
	i=1
	while [ "$i" -le "$VERSIONCOUNTER" ]
	do
		echo "Please enter the git branch name or git hash of GCG version" $i ": "
		read USERINPUT
		while [ -z $USERINPUT ]
		do
			echo "You forgot to enter the git version. Try again:"
			read USERINPUT
		done
		VERSION[${i}]="$USERINPUT"
		echo "Please enter the parameters to use for compilation of this GCG version (press Enter for no parameters)."
		read USERINPUT
		COMPARAMS[${i}]="$USERINPUT"
		echo "Please enter the parameters to run this GCG version with (press Enter for no parameters)."
		read USERINPUT
		PARAMS[${i}]="$USERINPUT"
		# Increment loop iterator
		i=$(($i + 1))
	done
else
	# Case that parameters where given when script was started
	TESTSET=$1
	TESTPARAMS=$2
	VERSIONCOUNTER=0
	# Cycle through arguments starting from the third one (first was testset, second was testparams)
	for i in "${@:3}"
	do
		# check whether the current index belongs to version, comparams or params
		if [ $(($i % 3)) = 0 ]
		then
			VERSIONCOUNTER=$((VERSIONCOUNTER + 1))
			VERSION[VERSIONCOUNTER]=${i}
		elif [ $(($i % 3)) = 1 ]; then
			COMPARAMS[VERSIONCOUNTER]=${i}
		else
			PARAMS[VERSIONCOUNTER]=${i}
		fi
	done
	# Quick sanity check for input
	if [ "$VERSIONCOUNTER" -eq 0 ]
	then
		echo "There are no versions to compare. The correct argument format of this script is:"
		echo "\"testset\" \"version1\" \"comparams1\" \"params1\" \"version2\" \"comparams2\" \"params2\" ... "
		echo "Alternatively you can restart this script without arguments to receive interactive help with getting the input right."
		echo "Goodbye until the next try!"
		exit 0
	fi
fi


# 2) check out the version(s), compile, run with corresponding parameter(s)

# Check whether all versions/comparams/params are the same to determine different cases in the following
i=1
SAMEVERSION=1
SAMECOMPARAMS=1
SAMEPARAMS=1
while (( i <= "$VERSIONCOUNTER" ))
do
	while (( j <= "$VERSIONCOUNTER" ))
	do
		if [ VERSION[i] != VERSION[j] ] && (( i != j ))
		then
			SAMEVERSION=0
		fi
		if [ COMPARAMS[i] != COMPARAMS[j] ] && (( i != j ))
		then
			SAMECOMPARAMS=0
		fi
		if [ PARAMS[i] != PARAMS[j] ] && (( i != j ))
		then
			SAMEPARAMS=0
		fi
		j=$(($j + 1))
	done
	i=$(($i + 1))
done
echo "$SAMEVERSION" "$SAMECOMPARAMS" "$SAMEPARAMS"


# 	in all cases check whether checkout was successful & compilation was successful
#	Case 2.1) same version with same parameters twice: call normal test script
#	Case 2.2) Add case: same version with different parameters: check out and run twice with diff params
#	Case 2.3) Add case: different versions with same parameters: check out & run successively
#		if out files would get overwritten then add version/params coding to their names
#	Case 2.4) Add case: different versions with different parameters: as 2.3)
#		if out files would get overwritten then add version/params coding to their names

# 3) if wished do sth with the output, e.g. make summary of differences in summary etc.

# termination
exit 0
