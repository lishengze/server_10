g_client_user=""
g_client_passwd=""
g_ip=""
g_nic_ip=""
g_server_user=""
g_server_passwd=""
g_path=""
while getopts "hc:C:s:S:I:i:P:" arg
do
    case $arg in
        h)
            echo "optional arguments:"
            echo "  -h  show this help message and exit"
            echo "  -c  [client_user]"
            echo "  -C  [client_passwd]"
            echo "  -s  [server_user]"
            echo "  -S  [server_passwd]"
            echo "  -I  [client_ip:server_ip]"
            echo "  -i  [client_nic_ip:server_nic_ip]"
            echo "  -P  [exe_path]"
            exit 0
            ;;
        c)
            g_client_user=${OPTARG}
            ;;
        C)
            g_client_passwd=${OPTARG}
            ;;
        s)
            g_server_user=${OPTARG}
            ;;
        S)
            g_server_passwd=${OPTARG}
            ;;
        I)
            g_ip=${OPTARG}
            ;;
        i)
            g_nic_ip=${OPTARG}
            ;;
        P)
            g_path=${OPTARG}
            ;;
    esac
done

if [ -z ${g_client_user} ] || [ -z ${g_client_passwd} ] || [ -z ${g_server_user} ] || [ -z ${g_server_passwd} ]; then
    echo "(Client/Server)'s (User/Passwd) not found"
    exit 1
fi

if [ -z ${g_path} ]; then
    echo "exe path not found"
    exit 1
fi

g_client_ip=`echo $g_ip | awk -F':' '{print $1}'`
g_server_ip=`echo $g_ip | awk -F':' '{print $2}'`

g_client_nic_ip=`echo $g_nic_ip | awk -F':' '{print $1}'`
g_server_nic_ip=`echo $g_nic_ip | awk -F':' '{print $2}'`

g_cln_ssh_param="-p ${g_client_passwd} ssh -o StrictHostKeyChecking=no ${g_client_user}@${g_client_ip}"
g_ser_ssh_param="-p ${g_server_passwd} ssh -o StrictHostKeyChecking=no ${g_server_user}@${g_server_ip}"


g_testcase_list="pingpong-tcp-direct
                 pingpong-tcp-onload-duplex
                 pingpong-tcp-onload-parallel
                 pingpong-tcp-normal-duplex
                 pingpong-tcp-normal-parallel
                 stream-tcp-direct
                 stream-tcp-onload-duplex
                 stream-tcp-onload-parallel
                 stream-tcp-normal-duplex
                 stream-tcp-normal-parallel"
#g_testcase_list="pingpong-tcp-direct"
g_transmit_rate=(0)
g_size_list=(128 256 512 1024)
#g_size_list=(128 512)

mkdir TEMPTEST
rm TEMPTEST/*
for size in ${g_size_list[@]}; do
    for var in $g_testcase_list; do
        if [ `echo $var | awk -F'-' '{print $1}'` == pingpong ]; then
            g_transmit_rate=(0)
        else
            if [ "$size" == "128" ]; then
                g_transmit_rate=(1000 5000 10000 30000 50000 100000)
                #g_transmit_rate=(150000 200000 250000 300000)
            elif [ "$size" == "256" ]; then
                g_transmit_rate=(1000 5000 10000 30000 50000 100000)
            elif [ "$size" == "512" ]; then
                g_transmit_rate=(1000 5000 10000 30000 50000)
                #g_transmit_rate=(100000 150000 200000 250000 300000)
            elif [ "$size" == "1024" ]; then
                g_transmit_rate=(1000 5000 10000 30000)
            else
                g_transmit_rate=(0)
            fi
        fi
        for rate in ${g_transmit_rate[@]}; do
            echo ${size}-${rate}-${var}
            cln_msg_ip="--remote-ip ${g_server_nic_ip}"
            ser_msg_ip=""
            onload=""
            ifc=`echo $var | awk -F'-' '{print $3}'`
            if [ $ifc == onload ]; then
                onload="onload"
            elif [ $ifc == direct ]; then
                cln_msg_ip="--message-ip ${g_client_nic_ip} --remote-ip ${g_server_nic_ip}"
                ser_msg_ip="--message-ip ${g_server_nic_ip}"
            fi
            duplex=""
            if [ "`echo $var | awk -F'-' '{print $4}'`" == "duplex" ]; then
                duplex="--duplex-io"
            fi

            sshpass ${g_ser_ssh_param} "killall tcp_engine_perf_test_server"
            sshpass ${g_cln_ssh_param} "killall tcp_engine_perf_test_client"

            cln_param="${duplex} ${cln_msg_ip} --tx-low-latency 1 --rx-low-latency 1\
            --transmit-rate ${rate} --message-size ${size} --runtime 45"

            ser_param="${duplex} ${ser_msg_ip} --tx-low-latency 1 --rx-low-latency 1\
            --mode 1"

            echo ${ser_param}
            echo ${cln_param}

            (sshpass ${g_ser_ssh_param} "cd ${g_path}; 
            ${onload} numactl --cpunodebind=0 --localalloc ./tcp_engine_perf_test_server ${ser_param} &" &) 1>/dev/null 2>&1

            rm temp.result
            (sshpass ${g_cln_ssh_param} "cd ${g_path}; 
            ${onload} numactl --cpunodebind=0 --localalloc ./tcp_engine_perf_test_client ${cln_param} &" &) >temp.result
            
            echo
            while true
            do
                line=`cat temp.result | wc -l`
                if [ $line -ge 3 ]; then
                    break
                fi
                sleep 1
            done
            sshpass ${g_ser_ssh_param} "killall tcp_engine_perf_test_server 1>/dev/null 2>&1"
            sshpass ${g_cln_ssh_param} "killall tcp_engine_perf_test_client 1>/dev/null 2>&1"
            mv temp.result TEMPTEST/${size}-${rate}-${var}.result
        done
    done
done

cd TEMPTEST
mkdir merge
rm merge/*

for testcase in ${g_testcase_list[@]}; do
    if [ "`echo ${testcase} | grep -o pingpong`" == "pingpong" ]; then
        for size in ${g_size_list[@]}; do
            file=`ls | grep ${testcase} | grep pingpong | grep ${size}`
            if [ -n "${file}" ]; then
                print=`cat ${file} | tail -n 1`
                print="${size} | ${print}"
                echo ${print} >> merge/${testcase}.result
            fi
        done
        continue
    fi

    for size in ${g_size_list[@]}; do
        for rate in ${g_transmit_rate[@]}; do
            temp="-${rate}-"
            file=`ls | grep ${testcase} | grep -e ${temp} | grep ${size}`
            if [ -n "${file}" ]; then
                print=`cat ${file} | tail -n 1`
                print="${size} ${rate} | ${print}"
                echo ${print} >> merge/${testcase}.result
            fi
        done
    done
done
