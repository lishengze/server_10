# 利用OpenSSL生成SSL加密需要用到的文件
以下只是生成相关文件的代码举例，实际使用应该根据使用者的需求改变相应命令参数，甚至是相应的命令

#### 生成CA根证书
假如是测试所需，可以使用自签名的模式，以本服务器作为CA生成CA根证书
```
# 生成CA的私钥
openssl genrsa -out ca.key 2048
# 生成CA证书请求
openssl req -new -key ca.key -out ca.csr
# 利用CA私钥自签名得到CA根证书
openssl x509 -req -days 365 -in ca.csr -signkey ca.key -out ca.crt
```
**ca.crt**可以作为客户端信任的证书链文件，实际上的证书链文件可能包括除了CA根证书之外的被信任方的证书，例如本文件夹底下的**ca-chain.cert.pem**

#### 生成服务端证书
```
# 生成服务端的私钥
openssl genrsa -out server.key 1024
# 生成证书请求
openssl req -new -key server.key -out server.csr
# 利用CA根证书及CA私钥签名得到服务端的数字证书
openssl ca -in server.csr -out server.crt -cert ca.crt -keyfile ca.key
# 将服务端私钥和服务端数字证书合并到同一个pem格式的文件
cat server.key server.crt >server.pem
```
**server.crt**就是服务端的数字证书，本文件底下的**server.pem**就是服务端私钥和服务端数字证书合并后的结果

#### 生成Diffie-Hellman参数文件
```
openssl dhparam -out dh.pem 2048
```