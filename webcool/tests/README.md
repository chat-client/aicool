# webcool/tests

这个目录存放 webcool 相关的测试与回归脚本。

## 前置条件

运行脚本前，请先启动 webcool 服务，例如：

```bash
/Users/zsx/work/github/chat-client/aicool/webcool/webcool -s 127.0.0.1:18093 -d /Users/zsx/work/github/chat-client/aicool/webcool/uploads
```

如果你已经在仓库根目录配置了 Python 虚拟环境，也可以优先使用：

```bash
/Users/zsx/work/github/chat-client/aicool/.venv/bin/python
```

## 脚本说明

### webcool_regression.py

用途：
- 回归验证“文件移动到文件夹”路径链路
- 回归验证“文件绑定到标签”路径链路
- 自动做往返校验，并自动清理临时标签

示例：

```bash
python webcool/tests/webcool_regression.py --base-url http://127.0.0.1:18093
```

或：

```bash
make webcool-regression WEBCOOL_BASE_URL=http://127.0.0.1:18093
```

### delete_tag_by_name.py

用途：
- 按标签名精确删除标签
- 如果同名标签有多个，会直接报错，避免误删

示例：

```bash
python webcool/tests/delete_tag_by_name.py --base-url http://127.0.0.1:18093 --name test2
```

### unbind_tag_file.py

用途：
- 把某个文件从指定标签中解绑
- 可通过 `--tag-id` 或 `--tag-name` 指定标签

示例：

```bash
python webcool/tests/unbind_tag_file.py --base-url http://127.0.0.1:18093 --tag-name test2 --file 分类/1-06 明月千里寄相思.mp3
```

或：

```bash
python webcool/tests/unbind_tag_file.py --base-url http://127.0.0.1:18093 --tag-id tag_1778833961_1 --file 分类/1-06 明月千里寄相思.mp3
```
