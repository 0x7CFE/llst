#!/bin/bash
# Author:Andrey Nikishaev

FORMAT="%ad  %an  <%ae>%n%n%w(0,4,4)%B"

git tag -l | sort -u -r | while read TAG ; do
    echo
    if [ $NEXT ];then
        echo [$NEXT]
    else
        echo "[Current]"
    fi
    GIT_PAGER=cat git log --no-merges --date=short --format="$FORMAT" $TAG..$NEXT
    NEXT=$TAG
done
FIRST=$(git tag -l | head -1)
echo [$FIRST]
GIT_PAGER=cat git log --no-merges --date=short --format="$FORMAT" $FIRST
