# elib-cli Design Spec

嵌入式命令行接口库，零动态内存分配，C99，遵循 elib 家族规范。

## 架构决策

- **单文件核心**：所有功能在 `elib_cli_core.c` 中实现，与 elib-key/elib-fsm 风格一致
- **Feed 模式输入**：用户调用 `elib_cli_feed_char()` 传入字符，不绑定 IO
- **所有资源用户分配**：接收缓冲区、历史缓冲区、命令表、暂存缓冲区均在初始化时由用户提供
- **无编译时宏限制**：不使用 `#define ELIB_CLI_MAX_*`，一切运行时配置

## 数据结构

```c
/* 回调类型 */
typedef void (*elib_cli_cmd_fn)(const char *args);
typedef int  (*elib_cli_print_fn)(const char *fmt, ...);

/* 换行符类型 */
typedef enum {
    ELIB_CLI_NL_LF = 0,     /* \n */
    ELIB_CLI_NL_CR,         /* \r */
    ELIB_CLI_NL_CRLF,       /* \r\n */
} elib_cli_nl_t;

/* 命令描述符 */
typedef struct {
    const char *name;
    const char *help;
    elib_cli_cmd_fn callback;
} elib_cli_cmd_t;

/* 配置 - 所有资源用户分配 */
typedef struct {
    const char *prompt;           /* 提示符，如 "> " */
    elib_cli_nl_t newline;        /* 换行符类型 */
    elib_cli_print_fn print;      /* printf 风格输出回调 */
    char *rx_buf;                 /* 用户分配的接收缓冲区 */
    uint16_t rx_buf_size;         /* 接收缓冲区大小 */
    char *history_buf;            /* 用户分配的历史环形缓冲区 */
    uint16_t history_buf_size;    /* 历史缓冲区大小 */
    elib_cli_cmd_t *cmd_table;    /* 用户分配的命令表 */
    uint8_t cmd_table_size;       /* 命令表容量 */
    char *saved_input;            /* 浏览历史时暂存当前输入（用户分配） */
    uint16_t saved_buf_size;      /* 暂存缓冲区大小 */
    int echo;                     /* 回显开关，1=开 0=关 */
} elib_cli_cfg_t;

/* 主上下文 */
typedef struct {
    const elib_cli_cfg_t *cfg;
    uint8_t cmd_count;            /* 已注册命令数 */
    uint16_t rx_pos;              /* 接收缓冲区写入位置 */
    uint16_t saved_len;           /* 暂存输入长度 */
    int hist_offset;              /* 历史浏览偏移，0=当前输入 */
    uint8_t escape_state;         /* ANSI 转义序列状态机：0=正常, 1=收到ESC, 2=收到'[' */
    int initialized;
} elib_cli_ctx_t;
```

## API

```c
/* 初始化/反初始化 */
elib_cli_err_t elib_cli_init(elib_cli_ctx_t *ctx, const elib_cli_cfg_t *cfg);
void elib_cli_deinit(elib_cli_ctx_t *ctx);

/* 输入 */
elib_cli_err_t elib_cli_feed_char(elib_cli_ctx_t *ctx, char ch);

/* 命令注册 */
elib_cli_err_t elib_cli_register(elib_cli_ctx_t *ctx, const char *name,
                                  const char *help, elib_cli_cmd_fn callback);

/* 输出 */
elib_cli_err_t elib_cli_print(elib_cli_ctx_t *ctx, const char *fmt, ...);

/* 参数解析辅助 */
int elib_cli_argc(const char *args);
const char *elib_cli_argv(const char *args, int index);
```

## 错误码

```c
typedef enum {
    ELIB_CLI_OK = 0,
    ELIB_CLI_ERR_INVALID_PARAM,
    ELIB_CLI_ERR_NOT_INITIALIZED,
    ELIB_CLI_ERR_CMD_TABLE_FULL,
    ELIB_CLI_ERR_CMD_EXISTS,
    ELIB_CLI_ERR_CMD_NOT_FOUND,
    ELIB_CLI_ERR_RX_OVERFLOW,
} elib_cli_err_t;
```

## 内部行为

### feed_char 字符处理

| 字符 | 处理 |
|------|------|
| 换行符（按配置） | 执行命令、存入历史、清空 rx_buf、打印 prompt。CRLF 模式下 `\r` 仅标记等待 `\n`，收到 `\n` 才执行 |
| 退格 (0x08/0x7F) | 删除 rx_buf 末字符，输出 `\b \b` |
| Tab (\t) | 命令匹配：1条→补全，多条→每行输出匹配项，0条→无操作 |
| ESC (0x1b) | 进入 ANSI 转义状态 (escape_state=1) |
| '[' (escape_state=1) | 转义状态推进 (escape_state=2) |
| 'A' (escape_state=2) | 上键：hist_offset++，显示对应历史命令，escape_state=0 |
| 'B' (escape_state=2) | 下键：hist_offset--，回到末尾时恢复暂存输入，escape_state=0 |
| 其他 (escape_state>0) | 丢弃当前转义序列，escape_state=0 |
| 普通可打印字符 | 追加到 rx_buf（不超过 rx_buf_size-1），回显 |

### 历史环形缓冲区

- 以 `\n` 分隔存储，如 `cmd1\ncmd2\ncmd3\n`
- 追加新命令：从尾部写入
- 空间不足：从头部清除整条命令（找到第一个 `\n`，丢弃其之前内容），可能清除多条
- 浏览历史：从后向前扫描 `\n` 分隔符

### 历史浏览

- 上键：hist_offset 递增，显示更早的历史命令
- 下键：hist_offset 递减，回到 0 时恢复用户当前输入
- 首次按上键时，将当前 rx_buf 内容暂存到 saved_input
- 换行执行命令时，hist_offset 重置为 0

### Tab 补全

- 取 rx_buf 中空格前的部分作为前缀
- 遍历 cmd_table，前缀匹配 name
- 1 条匹配：补全 rx_buf + 追加空格，回显
- 多条匹配：输出所有匹配命令名，每行一个
- 0 条匹配：无操作

### 命令执行

1. 在 rx_buf 中找到第一个空格，分割为 cmd 和 args
2. 在 cmd_table 中查找 cmd（区分大小写，strcmp）
3. 找到则调用 callback(args) — args 指向空格后的第一个非空格字符
4. 未找到则输出 "unknown command: xxx"
5. 内置 help 命令：遍历 cmd_table 输出每条命令的 name 和 help
6. 用户可通过 register("help", ...) 覆盖内置 help

### 回显

- echo 开启时：普通字符原样回显，退格输出 `\b \b`
- echo 关闭时：不回显任何内容
- 命令执行结果和 Tab 提示不受 echo 开关影响（走 print 回调）

## 文件结构

```
elib-cli/
├── include/
│   ├── elib_cli.h
│   ├── elib_cli_types.h
│   └── elib_cli_err.h
├── src/
│   ├── elib_cli_core.h
│   └── elib_cli_core.c
├── test/
│   └── test_elib_cli.c
├── scripts/
│   ├── setup-push-remote.sh
│   └── setup-push-remote.bat
├── docs/
├── .gitattributes
├── LICENSE
└── README.md
```

## 测试策略

- Mock print 回调，验证输出内容
- Feed 字符序列，验证命令执行和参数传递
- 测试 Tab 补全：单匹配、多匹配、无匹配
- 测试历史：添加、浏览、环形覆盖
- 测试退格、回显开关、错误码
