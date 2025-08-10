# Property Validation Implementation Roadmap

## Overview
This roadmap defines the incremental steps to implement pipeline-wide property validation as specified in `pipeline_property_validation.md`.

## Current Implementation Status

### Already Implemented
- ✅ Basic `prop_propagate()` function (single input/output only)
- ✅ `prop_validate_connection()` for constraint checking
- ✅ `prop_validate_multi_input_alignment()` function exists
- ✅ Filter structure has single `output_properties` field
- ✅ Filter structure has `input_properties[MAX_INPUTS]` array
- ✅ Constraint and behavior declaration functions
- ✅ Property table operations (set/get)
- ✅ Source filters set output_properties directly (not via prop_propagate)

### Not Implemented
- ❌ `pipeline_validate_properties()` function
- ❌ Multi-input support in `prop_propagate()`
- ❌ Multi-output support (no `MAX_OUTPUTS` or array)
- ❌ Output port parameter in `prop_propagate()`
- ❌ Topological DAG traversal for validation
- ❌ Validation called before pipeline start
- ❌ Source filters using prop_propagate with UNKNOWN
- ❌ Error message context tracking

## Phase 0: Minimal Viable Validation (Week 0 - Can Start Immediately)

### 0.1 Add prop_set_all_unknown()
- **File**: `bpipe/properties.c`
- **Simple**: Just sets all properties to known=false
- **Enables**: Source filters to use prop_propagate

### 0.2 Update Source Filters to Use Behaviors
- **File**: `bpipe/signal_generator.c` (start with one)
- **Current**: Direct `prop_set_*()` calls
- **Change to**:
  ```c
  // Add behaviors during init
  prop_append_behavior(&sg->base, PROP_DATA_TYPE, BEHAVIOR_OP_SET, &dtype);
  // Then propagate from UNKNOWN
  PropertyTable_t unknown = prop_table_init();
  prop_set_all_unknown(&unknown);
  sg->base.output_properties = prop_propagate(&unknown, &sg->base.contract);
  ```

### 0.3 Simple Linear Pipeline Validation
- **File**: `bpipe/pipeline.c`
- **Implement**: Basic `pipeline_validate_properties()` for linear pipelines only
- **Skip**: Topological sort (assume filters array is already ordered)
- **Benefit**: Get validation working for 80% of use cases

## Phase 1: Enhance Core Infrastructure (Week 1)

### 1.1 Upgrade prop_propagate() for multi-input/output
- **File**: `bpipe/properties.c`
- **Current**: Takes single upstream PropertyTable, returns single downstream
- **Needed**: 
  - Accept array of input PropertyTables
  - Add n_inputs parameter
  - Add output_port parameter
  - Update PRESERVE behavior to use operand.u32 for input selection
  - Handle UNKNOWN inputs for source filters

### 1.2 Add Multi-Output Support (if needed)
- **File**: `bpipe/core.h`
- **Current**: Single `PropertyTable_t output_properties`
- **Decision Required**: Do we need multi-output now or defer?
- **If yes**:
  - Define `MAX_OUTPUTS` (default 8)
  - Change to `PropertyTable_t output_properties[MAX_OUTPUTS]`
  - Add `uint32_t n_outputs` field
  - Update all filter initializations

### 1.3 Update Source Filters
- **Files**: All source filters (signal_generator, csv_source, etc.)
- **Current**: Directly call `prop_set_*()` functions on output_properties
- **Needed**:
  - Convert to use SET behaviors via `prop_append_behavior()`
  - Create UNKNOWN property table
  - Call upgraded `prop_propagate()` with UNKNOWN inputs
  - Store result in output_properties

## Phase 2: Pipeline Validation Core (Week 2)

### 2.1 Implement Topological Sort
- **File**: `bpipe/pipeline.c`
- **Tasks**:
  - Add DAG traversal function for topological ordering
  - Identify source filters (no inputs)
  - Handle cycles detection and error reporting

### 2.2 Implement pipeline_validate_properties()
- **File**: `bpipe/pipeline.c`
- **Priority**: CRITICAL - main validation entry point
- **Tasks**:
  - Process filters in topological order
  - Propagate properties through each filter
  - Validate constraints at each step
  - Generate comprehensive error messages
  - Handle external inputs for nested pipelines

### 2.3 Basic Constraint Validation
- **File**: `bpipe/properties.c`
- **Tasks**:
  - Implement constraint checking (EXISTS, EQ, GTE, LTE)
  - Generate specific error messages for failures
  - Track validation path for error context

## Phase 3: Integration (Week 3)

### 3.1 Update Pipeline Start
- **File**: `bpipe/pipeline.c`
- **Current**: `pipeline_start()` directly starts all filters without validation
- **Needed**:
  - Call `pipeline_validate_properties()` before loop
  - Return error with message if validation fails
  - Only start filters if validation passes
  - Consider adding bypass flag for backward compatibility

### 3.2 Update Transform Filters
- **Files**: All transform filters (map, batch_matcher, etc.)
- **Tasks**:
  - Remove any hardcoded property assumptions
  - Ensure behaviors are properly declared
  - Update to use cached properties from validation

### 3.3 Testing Infrastructure
- **Files**: `tests/test_property_validation.c`
- **Tasks**:
  - Test successful validation paths
  - Test each constraint failure type
  - Test UNKNOWN property propagation
  - Test complex DAG topologies

## Phase 4: Advanced Features (Week 4)

### 4.1 Multi-Input Alignment
- **File**: `bpipe/properties.c`
- **Tasks**:
  - Implement CONSTRAINT_OP_MULTI_INPUT_ALIGNED
  - Validate properties match across specified inputs
  - Add tests for alignment validation

### 4.2 Multi-Output Support
- **Files**: Router/splitter filters
- **Tasks**:
  - Update filters to compute properties per output port
  - Test property propagation through multi-output filters
  - Validate downstream connections from each port

### 4.3 Error Reporting Enhancement
- **File**: `bpipe/properties.c`
- **Tasks**:
  - Add validation context tracking (filter path)
  - Improve error message formatting
  - Add suggestions for common issues

## Phase 5: Filter Migration (Week 5)

### 5.1 Complete Filter Migration
- **Tasks**:
  - Audit all filters for constraint/behavior declarations
  - Add missing property specifications
  - Update tests for each filter

### 5.2 Documentation Updates
- **Tasks**:
  - Update filter implementation guide
  - Add property validation examples
  - Create troubleshooting guide

## Success Criteria

Each phase must pass before moving to the next:

1. **Phase 1**: `prop_propagate()` correctly computes properties for all test cases
2. **Phase 2**: Simple linear pipelines validate correctly
3. **Phase 3**: Complex DAGs with multiple paths validate correctly
4. **Phase 4**: Multi-input/output filters validate correctly
5. **Phase 5**: All existing tests pass with validation enabled

## Testing Strategy

- **Unit tests**: Each new function gets comprehensive tests
- **Integration tests**: End-to-end pipeline validation scenarios
- **Regression tests**: Ensure existing functionality isn't broken
- **Performance tests**: Validation shouldn't significantly impact startup time

## Risk Mitigation

- **Incremental deployment**: Each phase is independently valuable
- **Feature flag**: Add compile flag to disable validation if issues arise
- **Backward compatibility**: Ensure existing code continues to work
- **Early testing**: Test with real pipelines as soon as Phase 2 complete

## Estimated Timeline

- **Total Duration**: 5 weeks
- **Critical Path**: Phases 1-2 (must complete for basic functionality)
- **Parallelizable**: Phase 5 can start alongside Phase 4
- **Buffer Time**: Add 1 week buffer for unexpected issues

## Next Steps

1. Start with Phase 1.1 - implement `prop_propagate()`
2. Create test harness for property propagation
3. Update one source filter as proof of concept
4. Review and adjust roadmap based on learnings