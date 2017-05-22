#!/bin/bash
function getCpuUsage {
	# a = user + nice + system + idle + iowait + irq + softirq + steal
	# b = idle + iowait
	# c = a - b
	# d = c / a * 100
	# diff = (c2-c1)/(a2-a1)*100

	local a1=$(grep 'cpu ' /proc/stat | awk '{usage=($2+$3+$4+$5+$6+$7+$8+$9)} END {print usage}')
	local b1=$(grep 'cpu ' /proc/stat | awk '{usage=($5+$6)} END {print usage}')
	local c1=$(echo "scale=2; ($a1-$b1)" | bc)
	sleep 1
	local a2=$(grep 'cpu ' /proc/stat | awk '{usage=($2+$3+$4+$5+$6+$7+$8+$9)} END {print usage}')
	local b2=$(grep 'cpu ' /proc/stat | awk '{usage=($5+$6)} END {print usage}')
	local c2=$(echo "scale=2; ($a2-$b2)" | bc)

	local diff=$(echo "scale = 4; ($c2-$c1)/($a2-$a1)*100" | bc)
	echo "$diff"
}

gcc -o mandel mandel.c -pthread
if [ "$1" == "-h" ]
    then
        echo -e "Usage: \n    ./runmandel.sh <width> <height> <filename.bmp> <number of threads> [bg] \n    ./runmandel.sh -h  => to open this help. \n    <filename> => to redirect output to a file. \n    bg => only together with <filename> to run mandel.c in background."
		exit
fi
tmp="$3"
re='^[0-9]+$'   # regex to check if input is a number
if [ -z "$3" ] || [ ${tmp: -4} != ".bmp" ]	# third argument is empty or not a bitmap filetype
    then
		echo -e "No outputfile .bmp specified!\nStarting mandel in 5 seconds without an output file and with only one thread..."
		sleep 5
        ./mandel $1 $2
    elif [ "$5" == "bg" ]   # fourth argument is "bg" for background
        then
			./mandel $1 $2 $4 > $3 &    # start mandel in background
			pid=$!		# PID of mandel 
			
			trap "kill $pid 2>/dev/null" EXIT    # kill mandel if skript is killed
			
			while kill -0 $pid 2>/dev/null; do    # while mandel is running
			    # show CPU usage
			    cpuusage=$(getCpuUsage)
			    echo -en "\rCurrent Total CPU Usage: $cpuusage%"    #-en to intepret special characters like \r and to omit trailing newline
			    sleep 1
			done
			trap - EXIT    # disable trap on normal exit            
        elif [[ $5 =~ $re ]]
            then
                start=$4
                end=$5
                for((i=$start; i<=$end; i++))
                    do
                        echo -e "\n=> Starting mandel with $i thread(s):"
                        if [ "$6" == "bg" ]
                        then
                            ./mandel $1 $2 $i > $3 &
                            pid=$!		# PID of mandel

                            trap "kill $pid 2>/dev/null" EXIT    # kill mandel if skript is killed

                            while kill -0 $pid 2>/dev/null; do    # while mandel is running
                                # show CPU usage
                                cpuusage=$(getCpuUsage)
                                echo -en "\rCurrent Total CPU Usage: $cpuusage%"    #-en to intepret special characters like \r and to omit trailing newline
                                sleep 1
                            done
                            trap - EXIT    # disable trap on normal exit
                            
                            else
                                ./mandel $1 $2 $i > $3
                        fi
                    done
        else
            echo "Program call didn't match any other scheme, so executing ./mandel $1 $2 $4 > $3 in 5 seconds..."
            sleep 5
            ./mandel $1 $2 $4 > $3 	# forward stdout to file specified by third argument
fi


