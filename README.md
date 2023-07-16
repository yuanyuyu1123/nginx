# nginx

说明：该源码为 1.21.5 版本

环境搭建：

1.进入nginx目录，分别执行

./auto/configure

找到当前目录下的 objs文件夹 下的 Makefile文件，将"-Werror"去掉

make && make install

2.在ubuntu上用clion打开该项目

3.编辑运行环境设置，将程序参数设置为

-c  /usr/local/nginx/conf/nginx.conf

4.运行

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
