# TcpEngine性能测试使用手册
> TcpEngine性能测试脚本会通过ssh用户指定的客户端/服务端的IP，并在用户指定的路径拉起测试程序。测试脚本在客户端拉起的测试程序是`tcp_engine_perf_test_client`，在服务端拉起的测试程序是`tcp_engine_perf_test_server`

## 依赖工具
- sshpass。使用者需要保证运行脚本的机器安装了此工具，并且在系统运行路径当中，并且保证运行脚本的机器可以ping通客户端/服务端的IP。

## 测试脚本用法
```
# -c指定的是客户端的账号
# -C指定的是客户端的密码
# -s指定的是服务端的账号
# -S指定的是服务端的密码
# -I指定的是需要ssh的客户端/服务端IP
# -i指定的是TcpEngine性能测试需要用到的网卡IP
# -P指定的是测试程序tcp_engine_perf_test_client和tcp_engine_perf_test_server所在目录的绝对路径
./tcp_engine_perf_test.sh -c [client_user] -C [client_passwd] -s [server_user] -S [server_passwd] -I [client_ip:server_ip] -i [client_nic_ip:server_nic_ip] -P [exe_path]
```

## 基于AMI安装包使用
安装包中*script*文件夹存放了测试脚本`tcp_engine_perf_test.sh`，*bin*文件夹存放了测试程序`tcp_engine_perf_test_client`和`tcp_engine_perf_test_server`，只需在需要ssh的客户端/服务端主机部署AMI安装包，然后按照要求运行脚本。假如客户端/服务端主机是*10.128.0.40/10.128.0.42*，使用*40*的Solarfaire网卡*10.50.50.40*以及*42*的Solarfaire网卡*10.50.50.42*，两个主机的账号密码都是`archforce`，并且将AMI包放在用户*home*目录底下。测试脚本的使用情况如下
```
./tcp_engine_perf_test.sh -c archforce -C archforce -s archforce -S archforce -I 10.128.0.40:10.128.0.42 -i 10.50.50.40:10.50.50.42 -P ~/AMI/bin/
```

## 结果分析
脚本当前目录会生成一个*TEMPTEST*的文件夹，不同的文件代表不同的测试场景，文件的命名规则为`[消息大小]-[消息发送速率]-[pingpong/stream]-tcp-[协议栈]-[单工/双工]`。
*TEMPTEST*文件夹底下还有个*merge*的文件夹，里面是按照`[pingpong/stream]-tcp-[协议栈]-[单工/双工]`的划分数据汇总。

## 数据解读
每一个场次的数据含义依次是`平均值 最小值 最大值 P50 P90 P95 P99` 