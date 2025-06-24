# Agents

## Architecture

- The core of the system is a real-time, push-based data processing framework.
- Components are connected in a directed acyclic graph (DAG), and each component has its own dedicated input queue. 
- The system allows backpressure handling by configuring input queues to either block producers or drop incoming samples if full.
- The user interacts with a C Python object wrapping a a C struct and a pthread. 
- Users wire components directly using `.set_sink()`.

## Context 
- Top-level requirements are held in `requirements.adoc`.

## Agent Tasks

- Agent tasks are described in `tasks.md`
- TASK_ID: Each task has a unique identifier e.g. "FEATURE_XX"
- Task file structure: The task file is in a markdown format where each task is a second level heading named according to TASK_ID.

### Getting Task information:
When instructed to complete a task, look up the section matching the TASK_ID in `tasks.md`.
Run the following command to list task specific information:
`./get_task.sh OBJ tasks.m`

### Branch workflow: 
When working on a task first create a branch off main named after the TASK_ID before doing any work.

## Workflow
### Logging work
For ever set of changes that is made as a result of user prompt record both the user input and the resulting changes in `<TASK_ID>_log.md` with the following heading format:
- ##<MM-DD-YY>_<MM:SS> - <5 word Summary>
- Unless specified otherwise unit tests should be created for every new piece of functionality.

### Where to request user confirmation
- When a task cannot be completed due to missing context about the goals, key inputs or development environment.
- When a task is believed to be complete.

### Where not to request user confirmation
- Moving to the next phase of an implementaiton plan. Implement everything in one go. Reviews will happen later when complete.

### Task completion
- WORK_SUMMARY - bulletpoint summary of all work completed on this task.
- When a task is completed provide the user with a WORK_SUMMARY and list of options:
    - Merge into main.
    - Proposed improvements shortlist - Shortlist of proposed improvements.
- Once merged into main move the task into `task_archive.md`

## Tests

### Running `c` Tests
- Change directory into `tests` directory
- Run tests by executing: `make run`

## Project Structure
Outuput of the `tree` command.
```shell
.
├── AGENTS.md
├── bpipe
│   ├── core.c
│   ├── core.h
│   ├── core_python.c
│   ├── core_python.h
│   ├── signal_gen.c
│   └── signal_gen.h
├── compile_commands.json
├── lib
│   └── Unity
├── requirements.adoc
├── setup.py
├── task_archive.md
├── tasks.md
└── tests
    ├── Makefile
    ├── test_core_filter.c
    ├── test_sentinel.c
    ├── test_signal_gen.c
    └── unity.h
```
