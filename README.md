# nginx :❄️

说明:该源码为 1.21.5 版本

环境搭建:

1.进入nginx目录,分别执行:

```shell
./auto/configure --add-module=src/ext/http_mytest_module 
```

或:
```shell
./auto/configure --prefix=path/nginx  \
        --conf-path=path/nginx/conf/nginx.conf \
        --with-pcre --with-debug \
        --add-module=src/ext/http_mytest_module
```
2.
```shell
cmake .
```
3.
```shell
make
```

4.在linux上用clion打开该项目

5.编辑运行环境设置,将程序参数设置为
```text
-c  path/nginx/conf/nginx.conf
```

6.运行

注意:请将path改成自己的目录,例如:
```text
/home/yuan/code/cgit
```

Ubuntu24添加依赖:
```shell
sudo apt install libaio-dev libaio1t64
```

