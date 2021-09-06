#!/bin/bash

if ! pgrep Xephyr > /dev/null ; then
    XEPHYR=$(whereis -b Xephyr | cut -f2 -d' ')
    echo "start Xephyr"

    if [ $# -eq 2 ] ; then
        XINITRC=./xinitrc_cmp
    else
        XINITRC=./xinitrc
    fi

    xinit $XINITRC -- "$XEPHYR" $1 -ac -br -screen 800x600 -host-cursor 2> /dev/null
fi
