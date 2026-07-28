#!/bin/bash
# author: zhaonan
# date  : 2017/11/24
# email : zhaonan@archforce.com.cn

ShowHelp()
{
	echo ""
	echo "$0 context-name/application-name"
	echo ""
    echo "   -m show log more than content"
    echo "   -e show log level above Info"
    echo "   -r show log in reverse"
	echo "   -a show all logs (include AMI log)"
	echo "   -d the day shift of the log file"
	echo "      value X means show the log X days before"
    echo "   -t show log title only"
    echo "   -c show log content only"
	echo ""
	echo "   press \"shift + g\" to goto the end of the log"
	echo "   press \"b\" to page up"
	echo "   press \"f\" to page down"
	echo "   press \"LineNumber + g\" to goto line"
	echo "   press \"q\" to quit"
    exit 1
}

context_name=`echo $1 | sed -n 's/^[^-].\+/&/p'`
if [ ! -z ${context_name} ]; then
	shift 1
fi

print_cmd="cat"
show_content=1
filter=" | sed -n 's/^@/&/p' | grep -v \"ami::\" | "
day_shift=0
log_file_prefix="log"
title_only=0
content_only=0
while getopts "mrhad:etc" arg
do
	case $arg in
		h)
		  ShowHelp
		  exit 1
		  ;;
		m)
		  show_content=0
		  ;;
	 	r)
		  print_cmd="tac"
		  ;;
		a)
		  filter=" | sed -n 's/^@/&/p' | "
		  ;;
		d)
		  day_shift=$OPTARG
		  ;;
		e)
		  log_file_prefix="evt"
		  ;;
        t)
          title_only=1
          ;;
        c)
          content_only=1
          ;;
	esac
done

shift $((OPTIND - 1))
if [ -z ${context_name} ]; then
	context_name=$1
fi

user=`whoami`
date_str=`date +%Y-%m-%d`
if (($day_shift != 0)); then
	date_str=`date -d "$date_str -${day_shift} day" '+%F'`
fi

if [ $user != "root" ]; then
	log_dir="/home/${user}"
else
	log_dir="/root"
fi

if [ -z ${context_name} ]; then
    log_files=`ls -t ${log_dir}/log/${log_file_prefix}*.log`
    for log_file in $log_files
    do
        log_file_path=$log_file
        break
    done
    log_files=""
fi

if [ ! -z ${log_file_path} ]; then
    echo "start to view log file <$log_file_path>"
    if (($show_content == 1 )); then
        if (($content_only == 1 )); then
            cmd_str="$print_cmd $log_file_path ${filter} awk '{ for(i=1; i<=NF; ++i) if (i ==3 || i==5) printf \$i\" \"; else if (i > 13) printf \$i\" \"; print \" \"}'"
            eval $cmd_str | awk -F'|' '{print $1" | "$3}' | less
            exit 0
        elif (($title_only == 1 )); then
            cmd_str="$print_cmd $log_file_path ${filter} awk '{ for(i=1; i<=NF; ++i) if (i ==3 || i==5) printf \$i\" \"; else if (i > 13) printf \$i\" \"; print \" \"}'"
            eval $cmd_str | awk -F'|' '{print $1" | "$2}' | less
            exit 0
        fi
        cmd_str="$print_cmd $log_file_path ${filter} awk '{ for(i=1; i<=NF; ++i) if (i >13 || i ==3 || i==5) printf \$i\" \"; print \" \"}' | less"
        eval $cmd_str
        exit 0
    fi

    cmd_str="$print_cmd $log_file_path ${filter} awk '{for(i=1; i<=NF; ++i) if (i>4 && i != 6 && i != 7) printf \$i\" \"; print \" \" }' | sed -n 's/[^ ]\+/&/p' | less"
    eval $cmd_str
    exit 0
fi

log_files=`ls ${log_dir}/log/${log_file_prefix}*.log`
for log_file in ${log_files}
do
#    echo $log_file
    result=`basename ${log_file} | sed -n "s/^${log_file_prefix}_${context_name}.*${date_str}/&/p"`
    if [ ! -z ${result}  ]; then
       log_file_path=$log_file
       break
    fi
done

if [ -z ${log_file_path} ]; then
    echo "context name ${context_name} miss match"
    exit 1
fi
echo "start to view log file <$log_file_path>"

#exit 0
#log_file_path="${log_dir}/log/log_${context_name}_${date_str}.log"
#echo $log_file_path
#exit 0 
#cat $log_file_path | less

if (($show_content == 1 )); then
    if (($content_only == 1 )); then
        cmd_str="$print_cmd $log_file_path ${filter} awk '{ for(i=1; i<=NF; ++i) if (i ==3 || i==5) printf \$i\" \"; else if (i > 13) printf \$i\" \"; print \" \"}'"
        eval $cmd_str | awk -F'|' '{print $1" | "$3}' | less
        exit 0
    elif (($title_only == 1 )); then
        cmd_str="$print_cmd $log_file_path ${filter} awk '{ for(i=1; i<=NF; ++i) if (i ==3 || i==5) printf \$i\" \"; else if (i > 13) printf \$i\" \"; print \" \"}'"
        eval $cmd_str | awk -F'|' '{print $1" | "$2}' | less
        exit 0
    fi
	cmd_str="$print_cmd $log_file_path ${filter} awk '{ for(i=1; i<=NF; ++i) if (i >13 || i ==3 || i==5) printf \$i\" \"; print \" \"}' | less"
	eval $cmd_str
	exit 0
fi

cmd_str="$print_cmd $log_file_path ${filter} awk '{for(i=1; i<=NF; ++i) if (i>4 && i != 6 && i != 7) printf \$i\" \"; print \" \" }' | sed -n 's/[^ ]\+/&/p' | less"
eval $cmd_str
