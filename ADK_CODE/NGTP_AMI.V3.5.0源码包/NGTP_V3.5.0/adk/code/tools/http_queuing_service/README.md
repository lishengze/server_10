#http queuing service

该队列服务用于内部CI流水线，任务排队和分发

```shell
#可以移除对boost库的依赖
b2 debug -j20 http_queuing_service link=static

```

## 使用命令示例

```shell
# url 格式为:  http:://ip:port/QueueName/function

curl http://10.128.0.42:12556/myqueue/push/ -d 'abc=1'  -X PUT
curl http://10.128.0.42:12556/myqueue/push/ -d 'abc=2'  -X PUT
curl http://10.128.0.42:12556/myqueue/push/ -d 'abc=3'  -X PUT

curl http://10.128.0.42:12556/myqueue/queue_length/
queue_length:3

curl http://10.128.0.42:12556/myqueue/pop/ -X PUT
abc=1

curl http://10.128.0.42:12556/myqueue/pop/ -X PUT
abc=2

curl http://10.128.0.42:12556/myqueue/pop/ -X PUT
abc=3

# 以下命令关闭队列，之后当队列为空时，调用 is_close/ 返回true
curl http://10.128.0.42:12556/my_queue/is_close/  -X GET

curl http://10.128.0.42:12556/my_queue/is_close/  -X GET
{"closed":"false"}

curl http://10.128.0.42:12556/my_queue/is_close/  -X GET
{"closed":"true"}
```

