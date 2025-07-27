# Best Practices for Handling Static Analyzer False Positives

## 1. Inline Suppression Comments (Recommended for Spot Issues)

For clang-tidy, use `NOLINT` comments:

```c
// For single line
size_t idx = state->batches[0]->head;  // NOLINT(clang-analyzer-core.NullDereference)

// For block
// NOLINTNEXTLINE(clang-analyzer-core.NullDereference)
size_t idx = state->batches[0]->head;

// For multiple lines
// NOLINTBEGIN(clang-analyzer-core.NullDereference)
size_t idx = state->batches[0]->head;
((float*) batch->data)[idx] = (float) values[col];
// NOLINTEND(clang-analyzer-core.NullDereference)
```

## 2. Configuration File Suppressions (Recommended for Patterns)

Create `.clang-tidy` in the project root:

```yaml
---
Checks: '*,-clang-analyzer-core.NullDereference'
WarningsAsErrors: ''
HeaderFilterRegex: ''
AnalyzeTemporaryDtors: false
FormatStyle: none
CheckOptions:
  - key: cppcoreguidelines-macro-usage.AllowedRegexp
    value: 'BP_.*|CHECK_ERR'
```

Or more targeted suppressions in `.clang-tidy`:

```yaml
---
# Enable all checks but suppress specific false positives
Checks: '*'
WarningsAsErrors: ''
CheckOptions:
  - key: clang-analyzer-core.NullDereference.Suppressed
    value: 'csv_source.c:257;csv_source.c:343'
```

## 3. Analyzer-Specific Assertions (Good for Documentation)

Add assertions that only run during static analysis:

```c
#ifdef __clang_analyzer__
  // Help the analyzer understand our invariants
  assert(state->batches[0] != NULL);
  assert(values != NULL || self->n_data_columns == 0);
#endif
```

## 4. Defensive Null Checks with Optimization Hints

Add checks that document invariants and get optimized away:

```c
// Document the invariant and help static analyzer
if (!state->batches[0]) {
    __builtin_unreachable();  // Tell compiler this is impossible
}
size_t idx = state->batches[0]->head;
```

Or with a macro:

```c
#define ANALYZER_ASSUME(cond) \
    do { \
        if (!(cond)) __builtin_unreachable(); \
    } while(0)

ANALYZER_ASSUME(state->batches[0] != NULL);
size_t idx = state->batches[0]->head;
```

## 5. Suppression File (Best for CI/CD)

Create `clang-tidy-suppressions.txt`:

```
# False positives in csv_source.c
bpipe/csv_source.c:257: clang-analyzer-core.NullDereference
bpipe/csv_source.c:343: clang-analyzer-core.NullDereference
bpipe/csv_source.c:361: clang-analyzer-core.NullDereference
bpipe/csv_source.c:364: clang-analyzer-core.NullDereference
bpipe/csv_source.c:367: clang-analyzer-core.NullDereference
bpipe/csv_source.c:446: clang-analyzer-core.CallAndMessage
```

Then run: `clang-tidy --exclude-header-filter=suppressions.txt`

## 6. Refactoring for Analyzer-Friendly Code

Sometimes minor refactoring can help:

```c
// Instead of:
for (size_t i = 0; i < self->n_data_columns; i++) {
    values[i] = self->parse_buffer[self->data_column_indices[i]];
}

// More analyzer-friendly:
if (self->n_data_columns > 0 && values != NULL) {
    for (size_t i = 0; i < self->n_data_columns; i++) {
        values[i] = self->parse_buffer[self->data_column_indices[i]];
    }
}
```

## Recommended Approach for bpipe2

For this project, I recommend a combination:

1. **Project-wide .clang-tidy file** to suppress the specific false positive patterns
2. **Inline NOLINT comments** with explanatory text for future maintainers
3. **Documentation** (like the analysis file we created) explaining why these are false positives

This provides:
- Clean CI/CD builds
- Documentation for future developers
- Minimal code changes
- Easy to update when analyzers improve