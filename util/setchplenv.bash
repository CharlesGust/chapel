# bash shell script to set the Chapel environment variables


# shallow test to see if we are in the correct directory
# Just probe to see if we have a few essential subdirectories --
# indicating that we are probably in a Chapel root directory.
if [ -d "util" ] && [ -d "compiler" ] && [ -d "runtime" ] && [ -d "modules" ]
   then
      echo -n "Setting CHPL_HOME "
      export CHPL_HOME=$PWD
      echo "to $CHPL_HOME"

      echo -n "Setting CHPL_HOST_PLATFORM "
      export CHPL_HOST_PLATFORM=`"$CHPL_HOME"/util/chplenv/platform`
      echo "to $CHPL_HOST_PLATFORM"

      echo -n "Updating PATH to include "
      export PATH="$PATH":"$CHPL_HOME"/bin/$CHPL_HOST_PLATFORM:"$CHPL_HOME"/util
      echo "$CHPL_HOME"/bin/$CHPL_HOST_PLATFORM
      echo    "                     and ""$CHPL_HOME"/util

      echo -n "Updating MANPATH to include "
      export MANPATH="$MANPATH":"$CHPL_HOME"/man
      echo "$CHPL_HOME"/man
   else
      echo "Error: util/setchplenv must be sourced from within the chapel root directory"
fi
