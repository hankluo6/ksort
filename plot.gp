reset
set ylabel 'time(nsec)'
set title 'Performance'
set term png enhanced font 'Verdana,10'
set output 'runtime5.png'
set xlabel 'experiment'

plot [][0:1000000] 'out2.txt' using 1 with linespoints linewidth 1 title 'kernel heap sort', \
'' using 2 with linespoints linewidth 1 title 'merge sort', \
'' using 3 with linespoints linewidth 1 title 'shell sort', \
'' using 4 with linespoints linewidth 1 title 'binary insertion sort', \
'' using 5 with linespoints linewidth 1 title 'heap sort', \
'' using 6 with linespoints linewidth 1 title 'quick sort', \
'' using 7 with linespoints linewidth 1 title 'selection sort', \
'' using 8 with linespoints linewidth 1 title 'tim sort', \
'' using 9 with linespoints linewidth 1 title 'bubble sort', \
'' using 10 with linespoints linewidth 1 title 'bitonic sort', \
'' using 11 with linespoints linewidth 1 title 'merge sort in place', \
'' using 12 with linespoints linewidth 1 title 'grail sort', \
'' using 13 with linespoints linewidth 1 title 'sqrt sort', \
'' using 14 with linespoints linewidth 1 title 'rec stable sort', \
'' using 15 with linespoints linewidth 1 title 'grail sort dyn buffer'