# Working on Dafin with Claude

Read `DAFIN.md` first. Several Claude chats often work on this repository at the same time, so
every chat follows these rules, so that no chat cancels or overwrites another's work.

## Parallel-chat rules

1. **Own branch.** Work on a branch of your own, `claude/<short-task-name>`, made from the latest
   `origin/dafin-edition`. Never push to another chat's branch, never force-push, never delete a
   branch you did not make.
2. **Wait for GitHub to be idle.** Before any push (your own branch included) or starting a
   workflow, check:
   `gh api "repos/laur23212/dafin/actions/runs?per_page=10" --jq '.workflow_runs[] | [.name,.status,.head_branch] | @tsv'`
   If any run is `queued` or `in_progress`, wait and check again in about 15 minutes (a scheduled
   check-in, not a loop). A push cancels the running Tests on that branch, and starting Build
   cancels a running Build. Never stop a run that is in progress.
3. **Merging into `dafin-edition`.** Only when GitHub is idle and your branch's Tests run passed:
   `git fetch origin dafin-edition`, rebase on it, resolve conflicts keeping the other chat's
   changes (add to `DAFIN.md` rows rather than replacing them), then fast-forward push.
4. **One Windows build at a time**, after the last merge of the day:
   `gh api -X POST repos/laur23212/dafin/actions/workflows/build.yml/dispatches -f ref=dafin-edition -f 'inputs[platforms]=windows'`.
   Don't monitor it; the user sends the log if it fails. Don't start one while a Build is queued
   or running.
5. **Say which files you will change** before starting, so the user can warn you when another
   chat is editing the same ones.

## Model and effort

6. **Judge the model and effort before starting.** Decide which model (Sonnet or Opus) and which
   effort (Low, Medium, High, Extra or Max) the task needs, then:
   - If the current setting is enough, say so in one line and start.
   - If it needs more, stop and tell the user what to switch to and why. Don't start, so they can
     change it and resend.
   - If it needs less, mention that they could switch down next time, and start anyway.

   Errands (merge, build, adding templates, small text edits) need little. Features, crashes and
   changes across many files need more. If a task turns out harder midway, say so then.
