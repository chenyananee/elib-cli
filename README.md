# elib-cli

资源受限嵌入式系统用的零动态内存命令行接口库：用户静态提供上下文、接收缓冲、历史缓冲与命令表，逐字节喂入，纯 C99，单头伞 API。

当前版本：**2.0.0**（`ELIB_CLI_VERSION`），单头 `include/elib_cli.h`。

## 特性

- **零动态内存 / 零全局**：`ctx`、接收缓冲、历史缓冲、暂存缓冲、命令表全部由用户静态提供，库自身不持有任何状态，可同时存在多个 CLI 实例
- **单头伞 API**：全部公共类型与函数集中在 `elib_cli.h`，无子头
- **一次一字节的输入模型**：`elib_cli_feed_char()` 只吃一个字节，不绑定任何 IO；输入由调用方从 UART / USB / RTT 里搬进来
- **转义序列整段吞掉**：`ESC [ 参数 中间 终止` 与 `ESC O 终止` 被当作完整单元消费，参数化变体（如 Ctrl+Up 的 `ESC[1;5A`）绝不会把 `1;5` 漏进命令行
- **转义里的 CR/LF 不会提交**：序列体中出现的 `0x0D` / `0x0A` 一律不触发行提交
- **三种换行可配**：LF / CR / CRLF；CRLF 模式容错——单个 `\r` 立即提交，随后紧跟的 `\n` 被吞掉，终端不会"按了回车没反应"
- **整行入历史**：提交的是完整命令行（含参数），缓冲满时整条淘汰旧记录，超长行直接跳过而不是清空历史
- **上下键浏览**：第一次上翻把正在编辑的行暂存起来，下翻回到原样；被覆盖的行即使比历史短也照样替换
- **Tab 补全**：只在命令名尚未输入空格时生效；单匹配补全并补一个空格，多匹配列出候选并重绘，无匹配什么都不做
- **内置 help**：不占用户命令表的槽位，可被同名注册覆盖，Tab 也能补全
- **回显可关**：`ELIB_CLI_FLAG_ECHO` 控制是否镜像输入（字符、退格、重绘），关了之后完全静音，由终端自己负责
- **六种错误码**：`OK / INVALID_PARAM / NOT_INITIALIZED / CMD_TABLE_FULL / RX_OVERFLOW / CMD_NOT_FOUND`
- **参数解析辅助**：`elib_cli_argc()` 与带长度出参的 `elib_cli_argv()`
- **纯 C99**，C++ 兼容（`extern "C"`）

## 快速开始

### 1. 静态定义资源

```c
#include "elib_cli.h"    /* 构建时加 -I<repo>/include */

static char             g_rx_buf[128];
static char             g_hist_buf[1024];
static char             g_saved_buf[128];
static elib_cli_cmd_t   g_cmd_table[16];
static elib_cli_ctx_t   g_cli;

/* 输出回调：printf 风格，user_data 就是 cfg.user_data
 * 一个输入字节可能触发多次调用（每回显一个字符一次） */
static int cli_print(void *user_data, const char *fmt, ...)
{
    va_list ap;
    int n;

    (void)user_data;
    va_start(ap, fmt);
    n = vprintf(fmt, ap);        /* 或 UART_Send / SEGGER_RTT_vprintf */
    va_end(ap);
    return n;
}

/* 命令回调：args 是命令名之后的文本，永不为 NULL（无参数时是空串）
 * 指针指向接收缓冲，只在本次调用期间有效 */
static void cmd_led(void *user_data, const char *args)
{
    uint32_t len = 0u;
    const char *arg0 = elib_cli_argv(args, 0u, &len);

    (void)user_data;
    if (arg0 != NULL && len == 2u && strncmp(arg0, "on", 2u) == 0) {
        LED_On();
    } else {
        LED_Off();
    }
    /* 不要在这里写行尾：库会在回调返回后补上终止符 */
}
```

### 2. 初始化

```c
static void app_cli_init(void)
{
    elib_cli_cfg_t cfg = {
        .prompt           = "> ",                 /* 可 NULL：不打印提示符 */
        .newline          = ELIB_CLI_NL_CRLF,     /* LF / CR / CRLF */
        .print            = cli_print,            /* 必填 */
        .user_data        = NULL,                 /* 透传给两个回调 */
        .rx_buf           = g_rx_buf,             /* 必填，至少 2 字节 */
        .rx_buf_size      = sizeof(g_rx_buf),
        .history_buf      = g_hist_buf,           /* 可选，至少 3 字节 */
        .history_buf_size = sizeof(g_hist_buf),
        .cmd_table        = g_cmd_table,          /* 必填，至少 1 个槽位 */
        .cmd_table_size   = 16u,
        .saved_input      = g_saved_buf,          /* 可选，至少 2 字节 */
        .saved_buf_size   = sizeof(g_saved_buf),
        .flags            = ELIB_CLI_FLAG_ECHO,   /* 镜像输入 */
    };

    /* cfg 按值拷贝，可以放栈上；它指向的缓冲与命令表必须长期存在 */
    elib_cli_init(&g_cli, &cfg);

    elib_cli_register(&g_cli, "led",    "Control LED", cmd_led);
    elib_cli_register(&g_cli, "reboot", "Reboot system", cmd_reboot);
}
```

`init` 只准备资源，**不打印初始提示符**——需要的话由调用方在传输层就绪后自己打印。

### 3. 喂字节

```c
for (;;) {
    if (uart_has_data()) {
        (void)elib_cli_feed_char(&g_cli, (char)uart_getc());
    }
}
```

### 4. 反初始化

```c
elib_cli_deinit(&g_cli);   /* 之后除 init 外所有 API 返回 NOT_INITIALIZED */
```

## API 一览

| 分组 | 函数 | 说明 |
|------|------|------|
| 生命周期 | `elib_cli_init(ctx, cfg)` | 初始化（memset 覆盖 ctx；cfg 按值拷贝；非法配置拒收） |
| | `elib_cli_deinit(ctx)` | 反初始化（NULL 安全；之后除 init 外全部拒收） |
| 命令 | `elib_cli_register(ctx, name, help, callback)` | 注册命令；同名则原地替换 |
| 输入 | `elib_cli_feed_char(ctx, ch)` | 消费一个字节（唯一输入入口） |
| 参数 | `elib_cli_argc(args)` | 按空格统计参数个数 |
| | `elib_cli_argv(args, index, &len)` | 定位第 index 个参数并给出长度 |

错误码（6 个，`elib_cli_err_t`）：

| 错误码 | 含义 |
|--------|------|
| `ELIB_CLI_OK` | 成功；**也包括被忽略的字节**（控制字符、无匹配的 Tab 等） |
| `ELIB_CLI_ERR_INVALID_PARAM` | 空指针、非法换行值、未知 flag、缓冲过小、参数不全 |
| `ELIB_CLI_ERR_NOT_INITIALIZED` | 未 `init`，或已 `deinit` |
| `ELIB_CLI_ERR_CMD_TABLE_FULL` | 命令表已满，无法新增（替换不受影响） |
| `ELIB_CLI_ERR_RX_OVERFLOW` | 本行已满，该字节被丢弃，行内容不变 |
| `ELIB_CLI_ERR_CMD_NOT_FOUND` | 提交的行没有匹配到任何命令（用户已通过 `print` 得知） |

`feed_char` 返回的是**这一个字节**的处理结果：提交一个未知命令时返回 `CMD_NOT_FOUND`，其余情况返回 `OK`。

## 输入模型

`elib_cli_feed_char()` 只认下面这些字节，其余一律忽略（不入行、不回显、不报错）：

| 字节 | 含义 |
|------|------|
| `0x20`..`0x7E` | 可打印字符：追加到行尾，回显开启时镜像 |
| `0x08`、`0x7F` | 退格：删掉最后一个字节，回显开启时输出 `\b \b` |
| `0x09` | Tab：触发命令名补全 |
| 换行符 | 提交当前行，具体哪个字节由 `cfg.newline` 决定 |
| `0x1B` | ANSI 转义序列的起始，见下 |

> 行内容永远是 NUL 结尾的可打印字符串：`strlen(rx_buf) == rx_pos`，且 `rx_pos <= rx_buf_size - 1`。

### 转义序列

```
ESC [ <参数 0x30..0x3F>* <中间 0x20..0x2F>* <终止 0x40..0x7E>
ESC O <终止>
```

- 只有终止字节 `A` / `B` 有动作（上翻 / 下翻历史），其余序列整段吞掉
- 参数化变体（`ESC[1;5A`、`ESC[3~`、`ESC[2J`）不会被注入命令名
- 单独的 `ESC` 只吞掉紧随其后的一个字节
- 序列体里的 `0x0D` / `0x0A` **不提交**；它们若是终止字节之一，就把整段结束掉

逐字节样例（`rx` 为输入，`line` 为最终行内容）：

| 输入 | line | 说明 |
|------|------|------|
| `ab ESC [ 2 J cd` | `abcd` | 清屏序列整段吞掉 |
| `ab ESC [ 3 ~ c` | `abc` | Delete 键整段吞掉 |
| `ab ESC x cd` | `abcd` | 孤立 ESC 吞一个字节 |
| `ESC [ 1 ; 5 A` | （空） | 参数化上键 = 上翻一次 |
| `ESC O A` | （空） | SS3 上键 = 上翻一次 |

## 换行语义

| `cfg.newline` | 提交条件 | 库自己输出的终止符 |
|---------------|----------|--------------------|
| `ELIB_CLI_NL_LF` | `\n` | `\n` |
| `ELIB_CLI_NL_CR` | `\r` | `\r` |
| `ELIB_CLI_NL_CRLF` | `\r`（立即），或 `\r\n` | `\r\n` |

非本模式的换行字节被**忽略**：不入行、不回显、不提交。例如 LF 模式下收到 `\r` 什么都不发生。

CRLF 模式是"容错"的，规则只有两条：

1. 见到 `\r` **立刻提交**，不需要等下一个字节（终端只发 CR 时不会看起来像死键）
2. 提交后吞掉紧跟的一个 `\n`；若下一个字节不是 `\n`，它照常被解释（例如 `"led\rX"` 提交 `led`，随后 `X` 进入新行）

因此：

| 输入（CRLF 模式） | 提交次数 | 说明 |
|--------------------|----------|------|
| `led \r \n` | 1 | 标准 CRLF |
| `led \r` | 1 | 只有 CR，立即提交 |
| `led \r \n \n \n` | 1 | 多余的 LF 被忽略 |
| `led \r \r` | 2 | 第二个 CR 提交一个空行 |
| `led \n` | 0 | 单独的 LF 没有意义 |

历史缓冲**始终**用 `\n` 作分隔符，与换行模式无关。

## 命令行语义

提交时把行按**单个空格**切分：

- 去掉行首空格后，第一个 token 是命令名，区分大小写
- 命令名之后的所有内容（去掉前导空格，保留中间与尾部空格）作为 `args` 原样交给回调
- 空行（或全空格行）只重新打印提示符，不调用任何回调、不写历史
- 未知命令打印 `unknown command: <名字>`，`feed_char` 返回 `CMD_NOT_FOUND`

```
输入:   "  led   on  now \n"
命令名: "led"
args:   "on  now "        ← 前导空格去掉，中间/尾部空格保留
```

### 内置 help

- 列出命令表（注册顺序），每行 `名字` 或在有 help 文本时按 `%-12s %s` 对齐，最后列出内置 `help` 自己
- **不占**用户命令表的槽位，`ctx.cmd_count` 不会因为它增加
- 可以用 `elib_cli_register(ctx, "help", ...)` 覆盖成自己的命令，此后内置列表不再出现
- 参数被忽略（`help whatever` 与 `help` 等价）

## 历史

- 每次提交把**整行**（命令名 + 参数）以 `<行>\n` 追加进 `history_buf`
- 空行不记录；连续相同的行会重复记录（去重留给调用方）
- 空间不足时**整条**淘汰最旧的记录，绝不会留下半条
- 一行连空缓冲都装不下时**跳过**这一行，历史保持原样（不会清空）
- 上/下键按"距最近一条的偏移"浏览；第一次上翻会把正在编辑的行存进 `saved_input`，下翻回到偏移 0 时恢复它
- `saved_input` 太小会截断暂存内容，但不会越界
- 没给 `history_buf` 或没给 `saved_input` 时，相关功能安静地退化（上/下翻无效果）

历史缓冲的形态永远满足：`len <= history_buf_size`、以 `\n` 结尾、不含空记录（`"\n\n"`）。

## 补全

Tab 只在**命令名还没输入空格**时生效；一旦有了空格，光标就在参数区，Tab 什么都不做。

| 情况 | 行为 |
|------|------|
| 唯一匹配（含内置 help） | 整行替换为 `名字` + 一个空格，回显开启时先擦掉旧内容再打印新内容 |
| 多个匹配 | 换行、逐行列出候选（用户表在前，内置 help 在后）、重打提示符与当前行 |
| 无匹配 | 什么都不做，返回 `OK` |
| 补全后放不下（`名字长度 + 2 > rx_buf_size`） | 放弃补全，行不变 |
| 用户注册了 `help` | 内置 help 不再参与补全，也不会重复列出 |

## 回显

| `flags` | 行为 |
|---------|------|
| 含 `ELIB_CLI_FLAG_ECHO` | 镜像库消费掉的东西：每个接受的可打印字符、每次退格的 `\b \b`、历史/补全后的重绘 |
| 不含 | 完全不打印输入回显；提交时仍然输出终止符与提示符，`unknown command` 与 help 列表照常输出 |

回显只影响"打印了什么"，不影响行内容、历史与补全结果。

## 回调约定与并发

```c
typedef int  (*elib_cli_print_fn)(void *user_data, const char *fmt, ...);
typedef void (*elib_cli_cmd_fn)(void *user_data, const char *args);
```

- `args` **永不为 NULL**，无参数时是空串；它指向接收缓冲，只在调用期间有效（要留存请拷贝）
- `print` 一个输入字节可能被调用多次，必须非阻塞；异步发送要在回调内先拷贝
- `user_data` 由 `cfg` 透传，两个回调拿到同一个指针——所以命令不需要自己的全局变量
- 命令回调**不得**在同一个 ctx 上再调 `feed_char()` / `register()` / `deinit()`：`feed_char` 会改行状态，不可重入
- 库会在回调返回后补上终止符并重打提示符，回调输出**不要自己写行尾**
- 回调需要输出时请走同一个 `print`，否则顺序会乱

`feed_char` 会修改共享的行状态，因此**不是**可重入的：只从一处喂（主循环或某个 ISR，二选一），不要两边同时喂，也不要在回调里喂。

## 生命周期与内存

- `init` **无条件** memset `ctx`：任意历史状态都能重初始化
- `cfg` 是**值结构**：逐字段拷贝，`ctx.cfg` 内嵌一份，不保存指向 `cfg` 的指针，因此 `cfg` 可以放栈上
- `init` 会清空 `rx_buf[0]`、`history_buf[0]`、`saved_input[0]`，避免残留字节被解析
- 所有缓冲与命令表必须**长期存在**（寿命 ≥ `ctx` 的使用期），`name` / `help` 字符串同样必须长期有效（库只存指针）
- `init` 先做完全部校验再动 `ctx`：配置被拒收时 `ctx` 一个字节都不会变
- `deinit` 只清运行时状态，配置仍可读，但除 `init` 外全部 API 返回 `NOT_INITIALIZED`
- 重新 `init` 会整体替换配置、命令表计数、历史内容与行状态

## 编译

把 `include/` 加进头文件搜索路径，源文件编译为普通 C99 目标文件：

```bash
arm-none-eabi-gcc -std=c99 -Wall -Wextra -Werror -Wpedantic -Iinclude \
    -c src/elib_cli_core.c -o elib_cli_core.o
```

源文件直连伞头（`#include "elib_cli.h"`），不依赖任何相对路径，不随库提供构建系统。

## 测试

`test/` 下 12 个独立测试文件（共 162 个用例），每件自带 `main()`，可分别编译运行：

```bash
for t in test/test_*.c; do
    gcc -std=c99 -Wall -Wextra -Werror -Wpedantic -Iinclude \
        "$t" src/elib_cli_core.c -o "${t%.c}.exe"
    "./${t%.c}.exe"
done
```

| 文件 | 覆盖 |
|------|------|
| `test_init.c` | 版本宏与最小缓冲宏、cfg 逐字段拷贝、栈上 cfg 可销毁、十类非法配置拒收（含半配缓冲、换行越界、未知 flag）、被拒收时 ctx 不变、脏 ctx 重初始化、`init` 清空用户缓冲且不打印 |
| `test_register.c` | 参数校验、未初始化/已反初始化拒收、注册顺序、同表替换、大小写敏感、表满 `CMD_TABLE_FULL`、表满仍可替换、`help` 占普通槽位、重初始化清表 |
| `test_line_edit.c` | 可打印区间两端、退格与 DEL、空行退格静默、15 种控制字节被忽略、`RX_OVERFLOW` 边界与恢复、缓冲始终 NUL 结尾 |
| `test_execute.c` | 无参/有参分派、前导空格剥离、中间与尾部空格保留、空行与全空格行、未知命令文案与 `CMD_NOT_FOUND`、大小写敏感、`user_data` 透传、提交后状态复位、历史写整行、回调输出顺序 |
| `test_args.c` | `argc` 的各种空格形态、`argv` 定位与长度出参、越界与 NULL、`len` 可省略、`p[len]` 不是 NUL 的契约、`argv` 与 `argc` 一致 |
| `test_tab.c` | 单匹配补全补空格、无匹配静默、多匹配列表与重绘、内置 help 参与补全、参数区 Tab 无效（含 `led ` 与 `led on`）、容量边界（恰好放下 / 放不下）、`help` 覆盖后不重复、回显关闭时的补全与列表 |
| `test_history.c` | 整行记录、空行不记录、上翻顺序与上限、重绘擦除、下翻恢复暂存行、暂存缓冲截断、无历史缓冲、整条淘汰、超长行跳过、重复行保留、提交复位游标、缓冲形态不变量 |
| `test_escape.c` | CSI / SS3 上下键、参数化 Ctrl+Up、清屏与 Delete 序列、孤立 ESC 吞一字节、未完成 CSI、中间字节、序列内 CR/LF/CRLF 不提交、NUL 结束序列、SS3 功能键、空历史 |
| `test_newline.c` | LF / CR / CRLF 的提交与忽略规则、CRLF 单 CR 立即提交、吞掉配对的 LF、保留 CR 之后的字节、连续终止符、输出终止符随模式、终止符不入行、重初始化切换模式 |
| `test_echo.c` | 开回显的三类镜像、关回显时输入/退格/历史/补全静默但状态正确、提交仍输出终止符与提示符、未知命令仍报告、关回显绝不输出退格、回显只影响输出 |
| `test_lifecycle.c` | `deinit` NULL 安全、清运行时状态、重复 `deinit`、死后全部拒收、NULL ctx、零值 ctx、配置仍可读、重新 init 复活、重初始化丢弃会话、**两个 ctx 互不干扰**、解码器重新武装 |
| `test_help.c` | 列表顺序与 `%-12s` 对齐（含 12 字符名不补位）、空表只列自己、不占槽位、忽略参数、返回 `OK`、被覆盖、关回显仍列表、无多余空行、大小写敏感 |

### 与参考模型交叉验证

除上述断言外，行为另用一份独立的 Python 参考模型交叉验证（不参与构建，只用于验证）：

- **模型自洽**：`64165` 项断言，包含 34 条逐字节场景（每条都独立重算输出、行内容、返回值、回调记录）、两套结构不同的实现（逐字节状态机 vs 整段预读扫描）在 `2 flags × 3 换行 × 4 历史 × 4 暂存 × 4 命令表 × 20 输入` 共 `7680` 组组合上的全量对拍，以及 3000 组随机输入的不变量（行不越界、暂存不越界、历史以 `\n` 结尾且无空记录、偏移有界、关回显时不出现孤立退格）
- **C ↔ 模型对拍**：把真实实现编译到宿主机运行，与模型逐字段比较 `35428` 组输入（`212568` 项断言），字段包含输出字节、行内容、历史内容、偏移、每次回调的名字/参数/`argc`/`argv` 以及 `feed_char` 的最后返回值

结果均为 `TOTAL FAILS: 0`。

## 许可证

MIT License（见 `LICENSE`）。
