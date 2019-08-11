#!/bin/bash

# Generate doxygen documentation for GCG.
#
# Optionally, a custom .dxy file can be passed for the doxygen configuration.

# Stop on error.
set -e

./resources/devs/howtos/createindexes.sh
./resources/devs/detectors/createindexes.sh
cd $(dirname $0)

# Bypassed through usage of a direct link
if [ "$1" == "--mathjax" ]
then
   DOXYGEN_USE_MATHJAX="YES"
   if [ -d html/MathJax ]
   then
      echo "Updating git repository for MathJax."
      cd html/MathJax
      git pull
      cd ../..
   else
      echo "Cloning git repository for MathJax."
      cd html
      git clone https://github.com/mathjax/MathJax.git
      cd ..
   fi
else
   DOXYGEN_USE_MATHJAX="NO"
fi

if [ "$HTML_FILE_EXTENSION" = "" ]
then
    HTML_FILE_EXTENSION=shtml
fi

# Find relevant documentation versions.

CURRENT_VERSION=`grep '@version' main.md | awk '{ printf("%s", $2); }'`

echo "Building documentation in html/doc-${CURRENT_VERSION}."
echo "Please ensure that graphviz is installed on your system."
echo "Please ensure that you have php installed."
echo "Please ensure that GCG was installed correctly."
cd resources/misc/faq
python parser.py --linkext $HTML_FILE_EXTENSION  && php localfaq.php > faq.inc
cd ../../../
echo "<li><a href='../doc-${CURRENT_VERSION}/index.html'>GCG ${CURRENT_VERSION}</a></li>" > docversions.html
cd ..
bin/gcg -c "set default set save doc/resources/parameters.set quit"
cd doc
# Create index.html and gcgheader.html.

SCIPOPTSUITEHEADER=`sed 's/\//\\\\\//g' scipoptsuiteheader.html.in | tr -d '\n'`
DOCVERSIONS=`sed 's/\//\\\\\//g' docversions.html | tr -d '\n'`

sed -e "s/<SCIPOPTSUITEHEADER\/>/${SCIPOPTSUITEHEADER}/g" -e "s/<DOCVERSIONS\/>/${DOCVERSIONS}/g" -e "s/..\/doc/doc/g" < index.html.in > html/index.html
sed -e "s/<SCIPOPTSUITEHEADER\/>/${SCIPOPTSUITEHEADER}/g" -e "s/<DOCVERSIONS\/>/${DOCVERSIONS}/g" < gcgheader.html.in > gcgheader.html

# Build the gcg documentation.
DOXYGEN_USE_MATHJAX=${DOXYGEN_USE_MATHJAX} doxygen gcg.dxy

echo "Cleaning up."
rm -rf html/doc-${CURRENT_VERSION} docversions.html gcgheader.html
mv html/doc html/doc-${CURRENT_VERSION}

# Remove citelist.html (the Bibliography) manually from the menu (but still reachable via link)
cd html/doc-${CURRENT_VERSION}
sed -i "/citelist/d" pages.html
sed -i "/citelist/d" navtreedata.js
sed -i "s/\:\[4/\:\[3/g" navtreeindex*.js # citelist is the third item in the navigation (after Users Guide and Devs Guide,
sed -i "s/\:\[5/\:\[4/g" navtreeindex*.js # since Installation counts as homepage and thus 0)
echo "Done."
