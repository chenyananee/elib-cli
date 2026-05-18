# elib-cli

零动态内存分配的嵌入式命令行接口库。

## 特性

- **命令注册与回调** - 注册命令名、帮助字符串和回调函数，区分大小写匹配
- **Tab 补全** - 单匹配自动补全，多匹配列出候选
- **历史记录** - 上下键浏览历史，环形缓冲区自动淘汰旧命令
- **退格删除** - 支持 0x08/0x7F 退格键
- **Feed 模式输入** - 用户调用 `elib_cli_feed_char()` 传入字符，不绑定 IO
- **可配置换行符** - 支持 LF/CR/CRLF 三种换行模式
- **回显控制** - 可开关回显
- **内置 help 命令** - 列出所有已注册命令及帮助，可被覆盖
- **参数解析辅助** - 提供 `elib_cli_argc/argv` 工具函数
- **零动态分配** - 纯静态内存，所有资源由用户分配
- **C99 标准** - 无外部依赖

## 目录结构

```
elib-cli/
├── include/
│   ├── elib_cli.h          # 公共 API 头文件
│   ├── elib_cli_types.h    # 类型定义
│   └── elib_cli_err.h      # 错误码定义
├── src/
│   ├── elib_cli_core.h     # 内部头文件
│   └── elib_cli_core.c     # 核心实现
├── test/
│   └── test_elib_cli.c     # 单元测试
├── scripts/                # 构建和部署脚本
├── LICENSE
└── README.md
```

## API 参考

| 函数 | 说明 |
|------|------|
| `elib_cli_init(ctx, cfg)` | 初始化 CLI，配置资源和参数 |
| `elib_cli_deinit(ctx)` | 反初始化 CLI |
| `elib_cli_feed_char(ctx, ch)` | 输入一个字符，内部自动处理回显、补全、历史等 |
| `elib_cli_register(ctx, name, help, callback)` | 注册命令（重复注册则覆盖） |
| `elib_cli_argc(args)` | 计算参数个数（按空格分割） |
| `elib_cli_argv(args, index)` | 获取第 index 个参数的指针 |

## 使用示例

```c
#include "elib_cli.h"

/* 用户分配资源 */
static char rx_buf[128];
static char history_buf[256];
static char saved_buf[128];
static elib_cli_cmd_t cmd_table[8];
static elib_cli_ctx_t cli;

/* 硬件输出回调 */
static int cli_print(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);  /* 或 UART_Send */
    va_end(ap);
    return 0;
}

/* 命令回调 */
static void cmd_led(const char *args) {
    printf("LED args: %s\n", args);
}

static void cmd_reboot(const char *args) {
    NVIC_SystemReset();
}

int main(void) {
    const elib_cli_cfg_t cfg = {
        .prompt = "> ",
        .newline = ELIB_CLI_NL_LF,
        .print = cli_print,
        .rx_buf = rx_buf,
        .rx_buf_size = sizeof(rx_buf),
        .history_buf = history_buf,
        .history_buf_size = sizeof(history_buf),
        .cmd_table = cmd_table,
        .cmd_table_size = 8,
        .saved_input = saved_buf,
        .saved_buf_size = sizeof(saved_buf),
        .echo = 1,
    };

    elib_cli_init(&cli, &cfg);
    elib_cli_register(&cli, "led", "Control LED", cmd_led);
    elib_cli_register(&cli, "reboot", "Reboot system", cmd_reboot);

    while (1) {
        if (uart_has_data()) {
            elib_cli_feed_char(&cli, uart_getc());
        }
    }
}
```

## 编译测试

```bash
gcc -std=c99 -Wall -Wextra -Iinclude -o test_elib_cli test/test_elib_cli.c src/elib_cli_core.c && ./test_elib_cli
```

## 许可证

MIT License
