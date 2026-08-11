# Harmony Git Bash

[查看英文版](README.en.md)

Harmony Git Bash 是一个正在开发中的 Git Bash 原生 HarmonyOS PC 适配项目。
项目保留以终端为核心的 Git Bash 交互方式，不会将它替换成图形化 Git 客户端。

**Git Bash 原始源代码仓库：**
[git-for-windows/git](https://github.com/git-for-windows/git)

Git Bash 并不是一个独立的终端项目：Git for Windows 由 Git、MSYS2 和 mintty
共同组成。由于无法直接把 Windows 二进制程序有效移植到 HarmonyOS，本项目会先
适配终端交互约定，再单独实现原生 Git 服务。

应用图标和窗口 Logo 保持 Git for Windows 原版设计，仅将资源格式转换为
HarmonyOS 所需格式。详情参见[图标来源说明](docs/ICON-SOURCE.md)。

## 当前实现

- 面向 HarmonyOS PC 的深色 MINGW64 风格终端界面
- Shell 命令：`pwd`、`ls`、`cd`、`echo`、`printf`、`cat`、`env`、
  `printenv`、`export`、`unset`、`set`、`clear`、`help`
- 支持 `$VAR`、`${VAR}` 和 `$?` 环境变量展开、命令级变量赋值、
  `PWD`/`OLDPWD` 跟踪、嵌套 `$(...)`/反引号命令替换、未加引号的路径名
  通配符展开、基础 `<`、`>`、`>>` 重定向、按顺序处理的 `0/1/2`
  文件描述符重定向（包括描述符复制和 `/dev/null`），以及通过原终端提示符
  交互输入的 `<<`/`<<-` heredoc
- 通过 ArkTS N-API 边界加载 Harmony NDK C++17 服务
- 可从仓库目录、仓库子目录、文件选择器 `file://` URI 或关联 Git worktree
  中识别真实仓库
- 已支持真实本地命令：`status`、`add`、`rm`、`mv`、`restore`、`reset`、
  `commit`、`diff`、`log`、`show`、`cat-file`、`hash-object`、`ls-tree`、
  `ls-files`、`check-ignore`、`show-ref`、`symbolic-ref`、`update-ref`、
  `tag`、`branch`、`switch`、`checkout`、`remote`、`reflog`、`rev-parse`、
  `init` 和 `open`
- 支持解析 index v2/v3/v4，并显示真实的已修改、已删除和未跟踪工作区状态
- 支持 loose/packed 分支引用、`HEAD`、独立 fetch/push URL，以及 worktree
  `commondir` 解析
- 支持 blob、tree 和 commit 的 loose/packed Git 对象读写，包括用于暂存区和
  工作区差异的 OFS_DELTA、REF_DELTA 解析
- 支持真实 index v2 写入、分支创建/切换/删除/重置、hard reset、源文件恢复，
  以及暂存区与工作区联合恢复
- 支持 `git branch -m/-M/-c/-C` 分支重命名和复制
- 支持 `git rm` 和 `git mv` 本地路径删除与重命名，包括 cached、force、
  recursive 删除，并在移动时保留未暂存内容
- 支持通过 `git show` 显示提交，包括 `--stat`、`--oneline`、路径限制和
  annotated tag peeling
- 支持通过 `git cat-file` 检查对象，包括类型、大小、是否存在、格式化输出、
  显式对象类型、缩写对象 ID、revision path 和 tag peel 表达式
- 支持通过 `git hash-object` 计算文件哈希并选择性写入 loose object，包括
  多文件、相对子目录路径及显式 blob/tree/commit/tag 类型
- 支持通过 `git ls-tree` 列出树，包括 recursive、directory、tree、long、
  name-only、object-only、full-name、full-tree 和按路径过滤的输出
- 支持通过 `git ls-files` 列出 cached、modified、deleted、untracked 和
  ignored 路径，包括 stage 元数据、pathspec，以及命令相对路径或完整路径输出
- 支持 loose/packed tag 按通配符列出、创建 lightweight/annotated tag、
  通过编辑器输入消息、强制替换 tag 和删除 tag
- `status` 和 `git add` 支持 `.gitignore`、`.git/info/exclude`、
  `core.excludesFile` 和默认全局忽略规则
- 支持通过 `git check-ignore` 检查忽略规则，包括规则来源和行号详细输出、
  已跟踪文件过滤、`--no-index` 及相对子目录路径
- 支持通过 `git show-ref` 检查引用，包括 heads/tags 过滤、`HEAD`、精确验证、
  静默检查、annotated tag 解引用、仅哈希输出和缩写输出
- 支持通过 `git symbolic-ref` 读取、写入和删除符号引用，包括短名称、
  recursive/no-recurse 解析和 reflog 消息
- 支持通过 `git update-ref` 进行原子式引用创建、compare-and-swap 更新、
  符号解引用、`--no-deref`、删除和 reflog 消息写入；同时支持以换行符或 NUL
  分隔的 `--stdin` 事务、symbolic-ref 命令、prepare/commit/abort 状态、
  预检验证和文件系统回滚
- 支持通过 `git rev-list` 遍历提交图，包括 revision range、排除规则、
  namespace selector、父提交输出、计数、排序、merge 过滤和按路径限制历史
- 支持通过 `git merge-base` 查询共同祖先，包括 pair、all、octopus、
  independent、`--is-ancestor` 和支持 reflog 的 `--fork-point` 模式
- 支持通过 `git for-each-ref` 枚举引用，包括 pattern、exclusion、count、
  formatting atom、sorting、points-at 和 merged/contains 过滤
- 命令解析支持带引号的提交消息
- 支持 system、global、local 和显式文件 Git 配置，包括 `include.path`、
  条件式 `gitdir`、`gitdir/i`、`onbranch` include、scope/include 控制、
  `--get-all`、`--add`、`--unset-all`，以及 `remote.origin.url` 等 subsection key
- 支持重复的命令级 `git -c` 覆盖，以及 `--bool`、`--int`、`--bool-or-int`、
  `--bool-or-str`、`--path`、`--expiry-date`、`--type` 和 `--default`
  配置值处理
- 支持本地远程仓库管理：`remote add`、`remove`、`rename`、`get-url` 和
  `set-url`，包括独立 push URL
- 通过 HarmonyOS NetworkKit 支持 HTTPS 远程引用发现和 `git ls-remote`，
  包括 heads/tags 过滤、pattern、禁止 peeled-ref 输出、符号 `HEAD`、
  URL 检查和退出码行为
- 通过 HarmonyOS NetworkKit 支持 HTTPS `git fetch`，包括 upload-pack
  协商、二进制 pack 传输、side-band 进度/错误处理、pack/index 安装、
  远程跟踪引用事务更新、符号 remote `HEAD` 和 `FETCH_HEAD`
- 支持 HTTPS `git clone`，包括默认目标目录推断、自定义远程名称、
  `--no-checkout`、远程默认分支选择、工作区 checkout 和上游分支配置
- 支持对已配置或显式指定的上游分支执行 HTTPS `git pull`，包括最新状态检测、
  fast-forward checkout 和明确拒绝 divergent history
- 通过 receive-pack 支持 HTTPS `git push`，包括原生 pack 生成、
  report-status 解析、新建/删除分支、`--force`、`-u` 和本地
  non-fast-forward 拒绝
- 支持 Git 配置中的 HTTPS 传输控制，包括系统/禁用/自定义代理模式、
  proxy exclusion、连接/读取超时、上传/下载进度回调，以及通过 `Ctrl+C` 取消
- 所有 HTTPS advertisement、fetch 和 push 请求均使用 HarmonyOS NetworkKit
  系统 CA 验证，并支持通过 `http.sslCAInfo` 指定自定义 CA 文件；
  `http.sslVerify=false` 请求会被拒绝，不会绕过证书验证
- 支持 Git pack 校验和 index v2 生成，包括尾部 SHA-1 校验、对象/delta 解析、
  每对象 CRC、损坏数据拒绝和 pack/index 原子安装
- 支持已实现的引用变更操作读写本地 `HEAD` 和分支 reflog
- 支持 reflog 的 `show`、`list`、`exists`、`write`、`delete` 和 `drop`，
  包括数字 selector、rewrite/updateref、dry-run/verbose 输出和单 worktree 清理
- 支持 reflog `expire`，包括时间阈值、不可达提交清理、stale object 修复、
  rewrite/updateref、dry-run、verbose 输出和 worktree 范围选择
- 支持数字和日期 reflog selector、skip/since/until 过滤、常用 pretty/date
  格式，以及通过 `git log -g` 和 `git log --walk-reflogs` 遍历 reflog
- 支持 reflog count 快捷方式、可配置对象 ID 缩写、内置
  `short`/`medium`/`full`/`fuller`/`reference` 格式、常用身份/日期占位符，
  以及与上游一致的选项验证
- 提供确定性的 ArkTS 测试，以及通过系统 Git 创建的主机原生 fixture
- 记录 Git for Windows、Git 和 mintty 上游提交，并提供本地刷新脚本

原始终端布局、颜色和 MINGW64 风格提示符均保持不变。连接原生服务后，本地仓库
命令会直接操作磁盘中的真实仓库。旧版内存兼容行为仍由单元测试覆盖，用于验证
Shell 表面，并且只会在打开原生仓库之前使用。

## 进度快照

截至 2026 年 8 月 11 日，功能实现清单完成度为 **65/71（92%）**：

- 终端兼容基础：5/5
- 原生本地仓库后端：44/44
- 远程传输：10/11
- Shell 与终端一致性：6/8
- HarmonyOS PC 真机验证：0/3

该百分比衡量的是已经实现并完成验证的工程工作，而不只是 UI 或项目脚手架。
签名发布包明确不计入此功能范围。只有具备自动化验证或设备证据的已完成清单项
才会计数。此前合并在一起的配置/reflog 路线图项目已经拆分为三个可独立验证的
项目，这只修正了分母，没有改变已经实现的行为。终端 UI 保持不变。在完成
HarmonyOS PC 真机验证前，不能将本适配称为全部完成。

## 当前限制

- 配置条件 include 目前支持 `gitdir`、不区分大小写的 `gitdir/i` 和
  `onbranch`。上游的 `hasconfig:remote.*.url`、正则表达式查找、URL 匹配，
  以及 section 重命名/删除尚未实现。
- 原生 index 写入会将 v3/v4 index 规范化为 v2，不会保留 split-index 或
  untracked-cache 等可选 index extension 数据。
- 在原生 checkout 支持 gitlink 前，项目会拒绝 materialize submodule。
- 大型 pack 文件会在每次对象操作时读入内存；流式处理和对象存储缓存仍属于后续
  性能优化工作。
- 文件选择器 URI 访问仍需在 HarmonyOS PC 真机上验证。
- HTTPS `git fetch` 目前会为一个指定远程仓库获取已公布的分支 tip。
  显式 refspec、pruning、自动跟随 tag、shallow/filter 协商和大型 pack
  流式处理尚未实现。传输进度会通过原生 transport callback 收集，但为了保持
  现有 Git Bash UI，不会额外渲染到终端输出。
- `git clone` 目前支持 HTTPS 仓库、默认或显式目标路径、自定义 origin 名称和
  no-checkout 模式。克隆失败时尚不会自动删除刚初始化的目标目录。
- `git pull` 目前仅执行 fast-forward 更新。三路合并、rebase、autostash、
  显式 refspec 和冲突处理流程尚未实现。
- `git push` 目前支持对提供 report-status 的服务器执行 HTTPS receive-pack。
  请求失败时会打开一次性终端用户名/密码提示。已认证 HTTPS 凭据会从历史记录、
  显示输出和可复制终端行中移除；HarmonyOS AssetStoreKit 可用时会保存凭据，
  安全资产服务不可用时则保留在进程内存中。Credential helper 和面向真实可写
  远程仓库的服务端集成仍需验证。
- Reflog 遍历支持数字/日期 selector、count/skip/time 过滤、
  `short`/`medium`/`full`/`fuller`/`reference` pretty 格式和常用身份/日期
  占位符。需要完整 commit body、tree ID、parent list、note、signature 或
  decoration 元数据的格式，要等原生 reflog 服务提供这些字段后才能开放。
- 命令历史、命令/路径补全及本地设备复制粘贴控制已经实现，但键盘、输入法和
  剪贴板行为仍需在 HarmonyOS PC 真机上验证。
- HarmonyOS NetworkKit 系统 CA 验证和 `http.sslCAInfo` 自定义 CA 路径已经
  接入 HTTPS advertisement、fetch 和 push 请求。真实系统证书库和自定义 CA
  文件行为仍需 HarmonyOS PC 真机验证。项目会主动拒绝
  `http.sslVerify=false`。AssetStoreKit 持久化在完成真实设备重启、锁定状态和
  账户配置验证前属于尽力而为。自定义代理目前不支持认证，也未实现外部
  credential helper。
- SSH 传输、密钥处理、known hosts 和密码短语提示尚未实现。
- 不带 `-m`/`--message` 的 annotated tag 使用现有终端输入区域作为消息编辑器。
  按 Enter 输入行，使用 `:wq` 保存，或使用 `:q!`/`:cq` 取消。
- `git ls-files --ignored` 目前支持带标准仓库/全局 exclude 的未跟踪
  `--others` 模式，尚不支持查询已跟踪的 ignored file。
- 基础的引号感知单行管道可以将 `echo`/`printf` 输出传给原生命令。
  `git hash-object --stdin/--stdin-paths` 和 `git check-ignore --stdin`
  可以通过该路径接收以换行符分隔的输入。
- `git hash-object --path/--literally`、以 NUL 分隔的 stdin 记录和交互输入，
  仍需等待基于 PTY 的 Shell 输入流。
- `git show-ref --exclude-existing[=<pattern>]` 接受以换行符分隔的管道输入，
  并遵循上游 suffix parsing、prefix filtering 和 existing-ref suppression。
- `git update-ref --stdin` 接受以换行符或 NUL 分隔的 `update`、`create`、
  `delete`、`verify`、`symref-update`、`symref-create`、`symref-delete`、
  `symref-verify`、`option no-deref`、`start`、`prepare`、`commit` 和
  `abort` 命令。内置 `printf` 支持 NUL/八进制转义和重复使用格式，
  可执行 `printf '%s\0' ... | git update-ref --stdin -z`。
  `--batch-updates` 会提交有效项目，并以 Git 的
  `rejected <ref> <new> <old> <reason>` 格式报告可恢复失败，包括不区分
  大小写文件系统上的冲突。
- `git rev-list --stdin` 接受以换行符分隔的 revision，以及 `--` 后的路径；
  object/bisect 枚举仍需进一步扩展原生提交图。
- `git for-each-ref --stdin` 接受以换行符分隔的 ref pattern。宿主语言引用和
  pagination atom 仍需扩展 formatter。
- 管道和重定向目前在内存中以单行方式执行。嵌套 `$(...)` 和反引号命令替换会
  执行受支持的本地 Shell/Git 命令，并恢复 Shell 目录和环境状态。未加引号的
  `*`、`?` 和方括号路径模式通过原生目录服务展开，保留不匹配的模式，并遵循
  隐藏文件前导点规则。交互 heredoc 支持 `<<` 和移除制表符的 `<<-`，使用
  Bash 风格的 `>` 续行提示符；未加引号的 delimiter 会展开变量和命令替换，
  加引号的 delimiter 会保留字面内容。未加引号的变量和命令替换结果使用
  Bash 风格 `IFS` 字段分割，加引号和赋值展开则保持完整。常用整数算术展开
  支持变量、十进制/十六进制数、括号及 `+`、`-`、`*`、`/`、`%`。
  完整参数展开、剩余引用边界情况、job control 和 PTY 进程执行尚未实现。
  文件描述符重定向仅限受支持的内存命令集和文件描述符 `0`、`1`、`2`；
  它不会创建原生进程会话，也不支持任意文件描述符编号。

## 构建

使用 DevEco Studio 和 HarmonyOS 6.1.1（API 24），然后运行：

```bash
bash ./scripts/verify.sh
```

验证脚本会运行主机原生仓库 fixture、ArkTS 单元测试、`arm64-v8a` 和
`x86_64` 两种原生构建，以及 HAP 组装。

未签名开发版 HAP 输出位置：

```text
entry/build/default/outputs/default/entry-default-unsigned.hap
```

## 上游同步

本项目将 Git for Windows 记录为 `upstream`，将用户仓库记录为 `origin`。
检查上游变更后，可通过以下命令刷新已记录的上游提交：

```bash
bash ./scripts/update-upstream.sh
git add UPSTREAM.json
git commit -m "Update Git for Windows upstream snapshot"
```

来源归属和同步策略参见[上游同步说明](docs/UPSTREAM.md)。

## 许可证

Git 和 Git for Windows 使用 GPL-2.0-only 许可证。项目导入或链接上游 Git
代码后，也应按照同一许可证分发。初始 ArkTS 终端 Shell 未内置上游源代码；
后续所有导入都必须保留上游声明并提供对应源代码。
