#!/bin/bash
DATADIR=$XDG_DATA_HOME/heretic2r
GAMEMSG_SHASUM="083534d115f9d28ba0cb420764dd28ddbe74bda9976d7ac49515cbc184331c9d  $DATADIR/base/Gamemsg.txt"
declare -a libs=("gamex86.so" "Client Effects.so" "Player.so")

mkdir -p "$DATADIR" "$DATADIR/saves"
for gamedata_file in $DATADIR/base/Htic2-{0..1}.pak; do
    if [[ ! -f $gamedata_file ]]; then
        zenity --error --ok-label "Quit" --width=400 \
            --title "Failed to find Heretic II game data" --text "
Could not find Heretic II game data file <tt><b>$(basename $gamedata_file)</b></tt>\n\n
Please ensure you have copied this file to\n<tt><b>$DATADIR/</b></tt>."
        xdg-open $DATADIR
        exit 1
    fi
done

if (! echo $GAMEMSG_SHASUM | sha256sum --check --status); then
    echo "Copying Heretic II 1.06 patch files..."
    cp -r /app/share/games/h2e/* $DATADIR/base/
fi

for file in "${libs[@]}"; do
    if [[ ! -f $DATADIR/base/$file ]]; then
        echo "Copying missing library $file..."
        cp "/app/lib/$file" $DATADIR/base/
    fi
done

cd $DATADIR
heretic2r
