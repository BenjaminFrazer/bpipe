# Python Binding Architecture

## Overview

This document describes the architectural principles for Python bindings in the bpipe framework, established after identifying critical initialization issues that led to undefined behavior.

## Core Principle: Wrapper Pattern

**All Python initialization code MUST wrap the corresponding C initialization functions.**

This principle ensures:
- **Consistency**: Python and C filters behave identically
- **Completeness**: All fields are properly initialized
- **Maintainability**: Changes to core initialization propagate automatically
- **Safety**: Thread synchronization primitives are correctly initialized

## Historical Context

Originally, Python bindings reimplemented initialization logic directly, leading to:
1. **Missing mutex initialization** - `filter_mutex` was never initialized
2. **Incomplete state setup** - Fields like `running`, `n_sources`, `n_sinks` were uninitialized
3. **Partial buffer initialization** - Only some buffer fields were set
4. **Maintenance burden** - Duplicated logic that drifted from C implementation

These issues caused subtle bugs including worker threads failing due to uninitialized mutexes.

## Architectural Design

### Initialization Layers

```
┌─────────────────────────┐
│   Python User Code      │  BpFilterPy(capacity_exp=10, dtype=DTYPE_FLOAT)
├─────────────────────────┤
│  Python Binding Layer   │  Translates Python args → C config
├─────────────────────────┤
│    C Core Functions     │  BpFilter_Init(&filter, &config)
├─────────────────────────┤
│   Low-Level Init        │  pthread_mutex_init, memory allocation
└─────────────────────────┘
```

### Responsibilities

**Python Binding Layer:**
- Parse Python arguments (kwargs, positional args)
- Map Python conventions to C structures
- Set Python-specific fields (e.g., `transform = BpPyTransform`)
- Handle Python object lifecycle (refcounting)

**C Core Layer:**
- Validate configuration
- Initialize all filter fields
- Create synchronization primitives
- Allocate memory
- Set up worker thread infrastructure

### Naming Conventions

To clarify the separation of concerns, Python initialization follows this pattern:

```c
// Public Python init - handles Python-specific setup
int BpFilterPy_init(PyObject* self, PyObject* args, PyObject* kwds)
{
    // 1. Call base class init (which wraps C core)
    if (BpFilterBase.tp_init(self, args, kwds) < 0) 
        return -1;
    
    // 2. Set Python-specific fields
    BpFilterPy_t* filter = (BpFilterPy_t*) self;
    filter->base.transform = BpPyTransform;
    
    return 0;
}

// Base class init - wraps C core initialization
int BpFilterBase_init(PyObject* self, PyObject* args, PyObject* kwds)
{
    // 1. Parse Python arguments into C config
    BpFilterConfig config = BP_FILTER_CONFIG_DEFAULT;
    // ... parse args ...
    
    // 2. Call C core initializer
    BpFilterPy_t* filter = (BpFilterPy_t*) self;
    Bp_EC result = BpFilter_Init(&filter->base, &config);
    
    return (result == Bp_EC_OK) ? 0 : -1;
}
```

## Implementation Guidelines

### DO:
- Always call the C initialization function
- Map Python parameters to C configuration structures
- Preserve Python API compatibility through parameter translation
- Add Python-specific initialization AFTER core init
- Handle initialization failures by cleaning up properly

### DON'T:
- Reimplement C initialization logic in Python bindings
- Directly manipulate internal fields that C init handles
- Skip mutex or synchronization primitive initialization
- Assume default values match between Python and C

## Benefits

1. **Single Source of Truth**: C implementation defines initialization
2. **Automatic Updates**: New fields added to C structures get initialized
3. **Reduced Bugs**: No missing initialization steps
4. **Easier Debugging**: Initialization follows predictable path
5. **Better Testing**: Can test C and Python initialization separately

## Migration Strategy

See `specs/python_init_migration_spec.md` for the detailed migration plan.