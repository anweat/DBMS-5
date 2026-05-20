# GitHub 仓库保护设置

目标：限制组员直接向主干提交，所有代码通过 PR、Review 后合并；CI workflow 合入远端后再启用必需检查。

## 需要保护的分支

- `main`：正式稳定分支
- `develop`：日常集成分支

如果当前仓库还没有 `develop` 分支，先从当前稳定分支创建：

```powershell
git checkout -b develop
git push -u DBMS-5 develop
```

## main 分支保护

在 GitHub 页面进入：

`Settings -> Branches -> Add branch protection rule`

Branch name pattern 填：

```text
main
```

勾选：

- Require a pull request before merging
- Require approvals，数量填 `1`
- Dismiss stale pull request approvals when new commits are pushed
- Require review from Code Owners
- Require branches to be up to date before merging
- Restrict who can push to matching branches
- Do not allow bypassing the above settings

暂不勾选：

- Require status checks to pass before merging

原因：远端 `main/develop` 还没有 `.github/workflows/build.yml` 时，如果提前把 `build-and-test` 设为必需检查，PR 会一直等待不存在的检查，导致合并卡住。

等 workflow 文件合并进 `develop/main` 后，再启用 Required status checks，并选择：

```text
build-and-test
```

不要勾选：

- Allow force pushes
- Allow deletions

## develop 分支保护

Branch name pattern 填：

```text
develop
```

勾选：

- Require a pull request before merging
- Require approvals，数量填 `1`
- Dismiss stale pull request approvals when new commits are pushed
- Require review from Code Owners
暂不勾选：

- Require status checks to pass before merging

等 workflow 文件合并进 `develop/main` 后，再启用 Required status checks，并选择：

```text
build-and-test
```

如果小组开发节奏很快，`Require branches to be up to date before merging` 可以先不开，等冲刺稳定后再打开。

## 当前已通过 gh 配置的规则

截至本次配置，`main` 和 `develop` 已启用：

- Require a pull request before merging
- Require approvals：`1`
- Dismiss stale pull request approvals when new commits are pushed
- Require review from Code Owners
- Require conversation resolution before merging
- Require linear history
- Include administrators / Do not allow bypassing
- 禁止 force push
- 禁止删除分支

仓库合并策略已配置：

- 只允许 squash merge
- 禁止 merge commit
- 禁止 rebase merge
- 允许 auto-merge
- 合并后自动删除源分支

CI 必需检查待 `.github/workflows/build.yml` 合并到远端主干后启用。

可用以下命令启用：

```powershell
$gh = 'C:\Program Files\GitHub CLI\gh.exe'
$protection = @'
{
  "required_status_checks": {
    "strict": true,
    "contexts": ["build-and-test"]
  },
  "enforce_admins": true,
  "required_pull_request_reviews": {
    "dismiss_stale_reviews": true,
    "require_code_owner_reviews": true,
    "required_approving_review_count": 1,
    "require_last_push_approval": false
  },
  "restrictions": null,
  "required_linear_history": true,
  "allow_force_pushes": false,
  "allow_deletions": false,
  "block_creations": false,
  "required_conversation_resolution": true,
  "lock_branch": false,
  "allow_fork_syncing": true
}
'@
foreach ($branch in @('main','develop')) {
  $protection | & $gh api "repos/anweat/DBMS-5/branches/$branch/protection" -X PUT --input -
}
```

## 配套文件

仓库已加入：

- `.github/pull_request_template.md`：PR 描述模板
- `.github/CODEOWNERS`：代码负责人审查规则
- `.github/workflows/build.yml`：PR 构建和测试检查

注意：`CODEOWNERS` 里当前只写了 `@anweat`，拿到组员 GitHub 用户名后，应替换或追加对应模块负责人。
