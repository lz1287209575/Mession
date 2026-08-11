# 仓库整理 · Step 1：Worktree 与分支清理

日期:2026-08-11
状态:设计中(待用户复核)
范围:本地 worktree / 本地分支的清理;**不动远端 / 不动 main 提交内容 / 不动其他子项**

---

## 1. 背景

仓库当前 worktree 数量:6 个。逐项核对后,4 个应清退、1 个保留(mheadercodegen-ast 独立重构)、1 个就是主工作区。

四个清退目标的共性:均为 2026-07-15 那批 client-protocol 任务的产物,且其中三个 tip 都在 `7cd064d`(`refactor(client-protocol): step-2 collapse RpcDispatch to single FunctionId path`),工作区里 `debug-baseline-timeout` 还携带未提交的 `validate.py` / `EndpointCache.*` / `EchoService.cpp` / `GatewayServer.cpp` 改动。

第四个 `clienttarget-resolver-2026-07-15` 与它们不是同一组:它的 `MClientTargetResolver` 框架已合入 main(`0cc729c` + `48b3f6e` 均在 main 祖先链上),只剩空 worktree 目录与对应本地分支,本质是残留物。

判定:

- 三个 client-protocol 重复组已偏离 main 走向(2026-07-15 后 main 转 Lua 抽象层,本批再无后续 PR),继续保留只占空间并误导后来人。
- `clienttarget-resolver` worktree 已无业务价值。

## 2. 目标

- 释放 4 个 worktree 目录与对应本地分支
- 在 main 上落 2 个 commit:`docs(spec)` + `chore(repo)`,记录决策与 TODO §8 留痕
- 不引入脚本 / 不动远端 / 不动 main 业务提交

## 3. 范围

### 3.1 本轮清退(4 组)

| 序号 | worktree 路径 | 关联本地分支 | 备注 |
|---|---|---|---|
| W1 | `.claude/worktrees/clientmanifest-emit-2026-07-15` | `worktree-clientmanifest-emit-2026-07-15` | 与 W2/W3 tip 同为 `7cd064d` |
| W2 | `.claude/worktrees/debug-baseline-timeout-2026-07-15` | `worktree-debug-baseline-timeout-2026-07-15` | 携带未提交工作区改动,按用户决定**全删不看** |
| W3 | `.claude/worktrees/improve-service-discovery` | `worktree-mession-clientprotocol-2026-07-15` | 目录名 `improve-service-discovery` 与分支名 `mession-clientprotocol` 命名漂移;tip 仍是 `7cd064d` |
| W4 | `.claude/worktrees/clienttarget-resolver-2026-07-15` | `worktree-clienttarget-resolver-2026-07-15` | 已合入 main,留空壳 |

### 3.2 保留

| 对象 | 原因 |
|---|---|
| `.worktrees/mheadercodegen-ast` + `mheadercodegen-ast` 分支 | 10 个提交独立完整(MHeaderTool libtooling 重写),下轮单独走 rebase / 合并 / 归档 |
| 主工作区 `/root/Mession` | — |

### 3.3 不动

- 远端:`origin/*` 全量保留(包括 `origin/worktree-improve-service-discovery`,本轮不 push 删除)
- 本地分支 `refactor/base-project-structure`(2026-03-25,文档集重写,待评估)、`worktree-mheadertool-refactor`(MHeaderTool perf 缓存)
- main 分支本身

## 4. 执行序列

四步独立,可按任意顺序执行。建议按 W4 → W1 → W2 → W3 顺序:先删已合入 main 的(最安全),再删重复组。

每步两命令:

```bash
git worktree remove --force <path>
git branch -D <branch>
```

每步后立即校验:

```bash
git worktree list                                  # 应少一行
git branch --list <branch>                         # 应空
```

## 5. 前置校验

执行任一删除前:

```bash
git status --porcelain                             # 必须为空(主工作区无未提交改动)
git worktree list                                  # 6 项,核对 path 拼写
```

主工作区有未提交改动 → 中断,先提交 / 暂存 / 丢弃。

## 6. 错误处理

| 失败 | 处理 |
|---|---|
| `git worktree remove --force` 失败 | 中断后续;在终端 stderr 留痕;**不删**对应 branch,以便后续手工排查 |
| `git branch -D` 失败(如分支是当前 checkout) | 跳过该 branch,继续其他 step;最后再处理 |
| `git worktree remove` 报 "fatal: not removing worktree …",但目录被另一进程占用 | `rm -rf <path>` 兜底,只在 spec 显式提示后人工执行 |
| `git worktree list` 仍残留路径 | 手工 `rm -rf` 兜底 |

回滚:删掉的本地分支在 7 天内可通过 `git reflog` / `git fsck --dangling` 找回 dangling commit。本 spec 与 TODO.md §8 的留痕使"为何删"可追溯。

## 7. 审计落盘

落 2 个 commit 到 main:

1. `docs(spec): record repo step-1 worktree cleanup`
   - 新增 `Docs/superpowers/specs/2026-08-11-repo-step1-worktree-cleanup-design.md`
2. `chore(repo): note step-1 worktree cleanup in TODO §8`
   - 在 `TODO.md` 第 87 行附近(`文档与仓库卫生`小节)的"过期 worktree / 已合分支清理"条目下追加一行:

   ```markdown
   - 2026-08-11:删除 W1/W2/W3/W4(clientmanifest-emit / debug-baseline-timeout / improve-service-discovery / clienttarget-resolver);详见 `Docs/superpowers/specs/2026-08-11-repo-step1-worktree-cleanup-design.md`
   ```

两个 commit 顺序:先 spec,后 TODO 引用,保持链接单向可达。

不写 `Co-Authored-By:` 行。

## 8. 验证

清退后必须全部通过:

| 项 | 期望 |
|---|---|
| `git worktree list` 行数 | 2(主工作区 + mheadercodegen-ast) |
| `git branch --list 'worktree-client*' 'worktree-debug*' 'worktree-mession*'` | 空 |
| `git status --porcelain` 在主工作区 | 空(已合并两个 commit) |
| `TODO.md` §8 含 2026-08-11 留痕行 | 是 |
| `.claude/worktrees/` 残留 W1–W4 目录 | 无 |
| `.worktrees/mheadercodegen-ast/` 仍在 | 是 |

## 9. 不在本轮

下轮(Step 2 及以后)再处理:

- `.gitignore` 补 `.opencode/`、`.claude/worktrees/`(后者其实已忽略,复核)
- 6 个 `core.*` 文件 git 索引清理(已从磁盘删但 index 仍标 D)
- `docs/` → `Docs/` 归一(3 个 plan + 2 个早期 spec 合并)
- `.worktrees/mheadercodegen-ast/TODO.md` 副本处理(随该 worktree 下轮一起处理)
- 远端 `origin/worktree-improve-service-discovery` 删除(需评估是否值得 `git push --delete`)
- `mheadercodegen-ast` 10 个 commit 的合并 / rebase / 归档决策
- `refactor/base-project-structure` 与 `worktree-mheadertool-refactor` 处置

## 10. 风险与已知偏差

| 项 | 备注 |
|---|---|
| W2 未提交工作区改动丢弃 | 用户明确决定"全删不看",需在 TODO §8 留痕中明示 |
| 远端 `origin/worktree-improve-service-discovery` 仍存在 | 本轮不动,后续 pull 时若本地无对应分支,git 会提示 stale remote-tracking,可手动 `git remote prune origin` |
| `main` 仍领先 `origin/main` 78 个 commit | 与本轮无关,但提醒后续 push 前需 rebase / squash |

---

## 附录 A:执行命令一览

```bash
# 前置校验
cd /root/Mession
test -z "$(git status --porcelain)" || { echo "main worktree dirty"; exit 1; }
git worktree list

# W4(已合 main,最干净)
git worktree remove --force .claude/worktrees/clienttarget-resolver-2026-07-15
git branch -D worktree-clienttarget-resolver-2026-07-15

# W1 / W2 / W3(client-protocol 重复组,顺序任意)
git worktree remove --force .claude/worktrees/clientmanifest-emit-2026-07-15
git branch -D worktree-clientmanifest-emit-2026-07-15

git worktree remove --force .claude/worktrees/debug-baseline-timeout-2026-07-15
git branch -D worktree-debug-baseline-timeout-2026-07-15

git worktree remove --force .claude/worktrees/improve-service-discovery
git branch -D worktree-mession-clientprotocol-2026-07-15

# 校验
git worktree list
git branch --list 'worktree-client*' 'worktree-debug*' 'worktree-mession*'
```

执行后,落两个 commit(spec + TODO),收工。
