# OS2026 JYY Labs

本仓库保存 OS2026 课程 M1-M9 Mini Labs 的源码和测试文件。

各实验按目录独立组织，每个实验都可以在自己的目录中单独编译、测试和运行演示。

## 目录结构

```text
m1/    labyrinth：命令行迷宫后端
m2/    pstree：简化版进程树打印工具
m3/    sperf：基于 strace 的系统调用耗时统计工具
m4/    crepl：支持动态编译和加载的小型 C REPL
m5/    mymalloc：线程安全的内存分配器
m6/    gpt.c：GPT-2 124M 推理与 worker 并行化
m7/    httpd：支持 CGI 的多线程 HTTP 服务器
m8/    fsrecov：FAT32 BMP 文件恢复工具
m9/    libkvdb：append-only key-value 数据库
```

## 编译与测试

先进入对应实验目录：

```sh
cd m1
```

编译：

```sh
make
```

运行测试：

```sh
make test
```

运行演示：

```sh
make demo
```

`m1` 到 `m9` 均采用类似的测试方式。
