# Pipeline Property Validation Design

## Overview

The property system currently performs local pairwise validation at connection time via `filt_connect()`. This document proposes adding global pipeline-wide validation that propagates properties through the entire filter graph.

## Current State

### What Works Now
- **Local Validation**: `filt_connect()` validates immediate connections
- **Source Properties**: Source filters explicitly set output properties
- **Sink Requirements**: Sink filters declare input constraints
- **Basic Propagation**: `prop_propagate()` exists but isn't used

### Current Limitations
- Intermediate filter output properties aren't computed
- No end-to-end validation before pipeline start
- Properties don't flow through the graph
- Can't detect issues that require global context

## Proposed Solution

### Two-Phase Validation Strategy

#### Phase 1: Connection-Time (Local)
- **When**: During `filt_connect()`
- **What**: Basic pairwise compatibility check
- **Scope**: Two directly connected filters only
- **Purpose**: Catch obvious errors early (type mismatches, etc.)

#### Phase 2: Pipeline-Wide (Global)
- **When**: Before pipeline start (in `pipeline_start()` or explicit call)
- **What**: Full graph traversal with property propagation
- **Scope**: Entire filter DAG
- **Purpose**: Complete validation with property inference

### Implementation Design

```c
/* Add to pipeline.h */
Bp_EC pipeline_validate_properties(Pipeline_t* pipeline);

/* Add to core.h or properties.h */
Bp_EC filt_propagate_properties(Filter_t* filter);
Bp_EC filt_validate_graph(Filter_t* root);
```

### Algorithm for `pipeline_validate_properties()`

```c
Bp_EC pipeline_validate_properties(Pipeline_t* pipeline)
{
    // 1. Build dependency graph (topological sort)
    Filter_t** sorted_filters = topological_sort(pipeline->filters, 
                                                 pipeline->n_filters);
    
    // 2. Propagate properties in topological order
    for (size_t i = 0; i < pipeline->n_filters; i++) {
        Filter_t* filter = sorted_filters[i];
        
        // Skip source filters (they set their own properties)
        if (filter->n_input_buffers == 0) continue;
        
        // Collect properties from all inputs
        PropertyTable_t combined_inputs = prop_table_init();
        for (size_t j = 0; j < filter->n_inputs; j++) {
            Filter_t* upstream = get_upstream_filter(filter, j);
            if (upstream) {
                // Merge upstream properties (handle conflicts)
                prop_merge(&combined_inputs, &upstream->output_properties);
            }
        }
        
        // Apply filter's behaviors to compute output
        filter->output_properties = prop_propagate(&combined_inputs, 
                                                   &filter->contract);
    }
    
    // 3. Validate all connections with complete properties
    for (size_t i = 0; i < pipeline->n_connections; i++) {
        Connection_t* conn = &pipeline->connections[i];
        
        Bp_EC err = prop_validate_connection(
            &conn->from_filter->output_properties,
            &conn->to_filter->contract,
            error_msg, sizeof(error_msg));
            
        if (err != Bp_EC_OK) {
            // Log which connection failed and why
            return err;
        }
    }
    
    return Bp_EC_OK;
}
```

### Benefits

1. **Complete Validation**: Full end-to-end property checking
2. **Property Inference**: Intermediate filters get correct properties
3. **Better Errors**: Can report the full chain that led to incompatibility
4. **Optimization Opportunities**: Could optimize buffer sizes based on properties
5. **Debugging**: Can dump property flow for troubleshooting

### Usage Pattern

```c
// Build pipeline
Pipeline_t pipeline;
pipeline_init(&pipeline, config);

// Make connections (basic validation happens here)
for (each connection) {
    filt_connect(source, sport, sink, dport);  // Quick local checks
}

// Validate entire pipeline before starting
Bp_EC err = pipeline_validate_properties(&pipeline);
if (err != Bp_EC_OK) {
    // Report validation errors
    return err;
}

// Start pipeline (properties are now guaranteed valid)
pipeline_start(&pipeline);
```

### Integration Points

1. **Automatic in `pipeline_start()`**: Could be called automatically
2. **Explicit API**: User can call for validation without starting
3. **Debug Mode**: Could dump property flow graph
4. **Test Support**: Tests can validate property propagation

### Edge Cases to Handle

1. **Cycles**: Detect and reject cyclic dependencies
2. **Multiple Inputs**: Merge properties from multiple sources
3. **Conflicting Properties**: Define precedence rules
4. **Unknown Properties**: Decide if unknown means "any" or "error"
5. **Dynamic Properties**: Some filters might change properties at runtime

### Future Extensions

1. **Property Negotiation**: Filters could negotiate compatible settings
2. **Auto-Configuration**: Automatically configure intermediate filters
3. **Property Constraints Solver**: Use constraint solving for complex requirements
4. **Runtime Property Updates**: Support dynamic property changes

## Implementation Priority

1. **Phase 1**: Basic propagation for linear pipelines
2. **Phase 2**: Multi-input merge support
3. **Phase 3**: Full DAG support with cycle detection
4. **Phase 4**: Advanced features (negotiation, auto-config)

## Testing Strategy

1. **Unit Tests**: Test property propagation logic
2. **Integration Tests**: Test full pipeline validation
3. **Error Cases**: Test detection of incompatibilities
4. **Complex Topologies**: Test DAGs, fan-out, fan-in

## Migration Path

1. Pipeline validation is **opt-in** initially
2. Add warnings for pipelines that don't validate
3. Eventually make validation mandatory
4. Deprecate workarounds once stable

This design preserves the current fail-fast behavior while adding comprehensive validation when needed.