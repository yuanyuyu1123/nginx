# nginx

说明:该源码为 1.21.5 版本

环境搭建:

1.进入nginx目录,分别执行

./auto/configure --add-module=src/ext/http_mytest_module 或

./auto/configure --prefix=/home/yuan/code/cgit/nginx  \
            --conf-path=/home/yuan/code/cgit/nginx/conf/nginx.conf \
            --with-file-aio --with-pcre --with-debug \
            --add-module=src/ext/http_mytest_module

cmake .

make

2.在linux上用clion打开该项目

3.编辑运行环境设置,将程序参数设置为

-c  /usr/local/cgit/nginx/conf/nginx.conf

4.运行
注意: 使用 --with-file-aio需要安装libaio,否则无法debug \
ubuntu24:  sudo apt install libaio-dev libaio1t64
