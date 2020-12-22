#! /bin/bash

###                                                    ###
#    This script is used to create a bundle for OS X     #
###                                                    ###

mkdir -p Traverso.app/Contents/MacOS/
mkdir -p Traverso.app/Contents/Resources/

cp bin/traverso Traverso.app/Contents/MacOS/
cp resources/images/traverso_mac.icns Traverso.app/Contents/Resources/Traverso.icns
cp resources/Info.plist Traverso.app/Contents/
cp /usr/local/bin/sox Traverso.app/Contents/MacOS/
cp /usr/local/bin/cdrdao Traverso.app/Contents/MacOS/

/usr/local/opt/qt/bin/macdeployqt ./Traverso.app -verbose=2 -executable=./Traverso.app/Contents/MacOS/sox -executable=./Traverso.app/Contents/MacOS/cdrdao
