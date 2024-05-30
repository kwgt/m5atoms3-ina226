#! /usr/bin/env gnuplot

set terminal png size 3840, 1080
set output "power.png"
set datafile separator ","

set xdata time
set timefmt "%s"
set format x "%m/%d\n%H:%M:%S"

set format y "%.0fmA"
set ylabel "Current"
set yrange [0:1100]

set grid

lt(t) = (t / 1000) + 32400

plot "afo.csv" using (lt($1)):($3) axis x1y1 \
	with lines lc rgb "#CF8080" notitle, \
     "afo.csv" using (lt($1)):($3) axis x1y1 \
	with dots lc rgb "#0000Fc" title "CURRENT"


