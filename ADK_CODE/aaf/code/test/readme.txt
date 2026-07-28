修改 删除 监控    AAF1      127.0.0.1   true    AAF1   ["ForTest"]     ["ForTest"]
修改 删除 监控    AAF1_S    127.0.0.1   true    AAF1_S ["ForTest_S"]   ["ForTest_S"]

bin/gcc-5.4.0/debug/threading-multi/test_ami_app -n AAF1 --domain-server {localhost:2379}


修改 删除 监控    MS-11   127.0.0.1   false   MS  ["MXXX"]    ["MXXX"]
修改 删除 监控    MS-12   127.0.0.1   false   MS  ["MXXX"]    ["MXXX"]
修改 删除 监控    MS-13   127.0.0.1   false   MS      ["YYYY"]

bin/gcc-5.4.0/debug/threading-multi/test_master_slave -n MS-11 --domain-server {localhost:9379}
bin/gcc-5.4.0/debug/threading-multi/test_master_slave -n MS-12 --domain-server {localhost:9379}
bin/gcc-5.4.0/debug/threading-multi/test_master_slave -n MS-13 --domain-server {localhost:9379}

[test_member_lost_event]
>>>>>>>> configure  
修改 删除 监控    MS-11   127.0.0.1   false   MS  ["XXXX"]    ["XXXX"]
修改 删除 监控    MS-12   127.0.0.1   false   MS  ["XXXX"]    ["XXXX"]
修改 删除 监控    MS-13   127.0.0.1   false   MS  ["XXXX"]    ["XXXX"]

>>>>>>>> build  
b2 debug -j4 test_member_lost_event

>>>>>>>> run       
bin/gcc-5.4.0/debug/threading-multi/test_member_lost_event -n test_app_1 --context-name MS-11 --domain-server {localhost:9379}
bin/gcc-5.4.0/debug/threading-multi/test_member_lost_event -n test_app_1 --context-name MS-12 --domain-server {localhost:9379}
bin/gcc-5.4.0/debug/threading-multi/test_member_lost_event -n test_app_1 --context-name MS-13 --domain-server {localhost:9379}

>>>>>>>> expect output
kill the leader MS-11, MS-12 report following informations:
role change to leader
lost members : [MS-11]

[test_generic_fw_*]
bin/gcc-5.4.0/debug/threading-multi/test_generic_fw_* -n test

>>>>>>>> check
test_generic_fw      check cpu utilization
test_generic_fw_v2   check cpu utilization
test_generic_fw_v3   check cpu utilization
test_generic_fw_v4   check app is exit




