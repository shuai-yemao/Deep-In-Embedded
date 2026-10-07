mkdir命令（make direactory）是用于创建新的目录（文件夹）

```
mkdir [-p] [Linux路径]
```

-p的作用是一次性创建多个层次的目录，当前没有的路径也会被创建

```
mkdir -p home/test/666
```

在home中创建新目录无需权限，但是在home目录外是需要管理员权限，否则会创建目录失败
