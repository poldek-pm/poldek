#!/bin/sh
# visualize repository by graphviz

REPODIR="$1"
[ -n "$REPODIR" ] || exit 1
[ -d "$REPODIR" ] || exit 1

../cli/poldek --st dir -s $REPODIR --depgraph dot:$DIR/graph.dot
dot -Tps $DIR/graph.dot > $DIR/graph.ps
ps2epsi $DIR/graph.ps $DIR/graph.eps