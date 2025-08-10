# Property Validation Implementation Roadmap

## Overview
This roadmap defines the incremental steps to implement pipeline-wide property validation as specified in `pipeline_property_validation.md`.

## Current Implementation Status

### Completed (Phases 0-3)
- ✅ `prop_set_all_unknown()` for creating UNKNOWN property tables
- ✅ `prop_propagate()` function with multi-input support
- ✅ `prop_validate_connection()` for constraint checking
- ✅ `prop_validate_multi_input_alignment()` for aligned input validation
- ✅ `pipeline_validate_properties()` function (linear validation)
- ✅ Filter structure has single `output_properties` field
- ✅ Filter structure has `input_properties[MAX_INPUTS]` array
- ✅ Constraint and behavior declaration functions
- ✅ Property table operations (set/get)
- ✅ Source filters using behaviors and prop_propagate
- ✅ Validation automatically called during `pipeline_start()`
- ✅ Basic error message generation

### Not Yet Implemented (Phases 4-5)
- ❌ Multi-output support (no `MAX_OUTPUTS` or array)
- ❌ Output port parameter actually used in `prop_propagate()` (always 0)
- ❌ Topological DAG traversal (linear order only)
- ❌ Nested pipeline external inputs support
- ❌ Advanced error message context tracking
- ❌ Property negotiation
- ❌ Channel count property

## Phase 0: Minimal Viable Validation ✅ COMPLETED

### 0.1 Add prop_set_all_unknown() ✅
- **Status**: COMPLETED
- **Location**: `bpipe/properties.c`
- **Implementation**: Sets all properties to known=false
- **Result**: Source filters can now use prop_propagate

### 0.2 Update Source Filters to Use Behaviors ✅
- **Status**: COMPLETED
- **Example**: `bpipe/signal_generator.c`
- **Implementation**: Source filters now:
  - Add behaviors during init via `prop_append_behavior()`
  - Use prop_propagate with UNKNOWN inputs
  - Compute output_properties from behaviors

### 0.3 Simple Linear Pipeline Validation ✅
- **Status**: COMPLETED
- **Location**: `bpipe/pipeline.c`
- **Implementation**: `pipeline_validate_properties()` validates linear pipelines
- **Note**: Topological sort not yet implemented (uses filter array order)
- **Result**: Validation working for most common use cases

## Phase 1: Enhance Core Infrastructure ✅ COMPLETED

### 1.1 Upgrade prop_propagate() for multi-input/output ✅
- **Status**: COMPLETED
- **Location**: `bpipe/properties.c`
- **Implementation**: 
  - ✅ Accepts array of input PropertyTables
  - ✅ Has n_inputs parameter
  - ✅ Has output_port parameter (but always 0 currently)
  - ✅ PRESERVE behavior uses operand.u32 for input selection
  - ✅ Handles UNKNOWN inputs for source filters

### 1.2 Add Multi-Output Support ❌ DEFERRED
- **Status**: NOT IMPLEMENTED (deferred to Phase 4)
- **Current**: Single `PropertyTable_t output_properties`
- **Rationale**: Single output sufficient for current use cases
- **Future work**:
  - Define `MAX_OUTPUTS` (default 8)
  - Change to `PropertyTable_t output_properties[MAX_OUTPUTS]`
  - Add `uint32_t n_outputs` field
  - Update all filter initializations

### 1.3 Update Source Filters ✅
- **Status**: COMPLETED
- **Files**: signal_generator.c, csv_source.c, and others
- **Implementation**:
  - ✅ Use SET behaviors via `prop_append_behavior()`
  - ✅ Create UNKNOWN property table with `prop_set_all_unknown()`
  - ✅ Call `prop_propagate()` with UNKNOWN inputs
  - ✅ Store result in output_properties

## Phase 2: Pipeline Validation Core ✅ COMPLETED

### 2.1 Implement Topological Sort ❌ DEFERRED
- **Status**: NOT IMPLEMENTED (deferred to Phase 4)
- **Current**: Linear validation using filter array order
- **Rationale**: Linear order sufficient for most pipelines
- **Future tasks**:
  - Add DAG traversal function for topological ordering
  - Identify source filters (no inputs)
  - Handle cycles detection and error reporting

### 2.2 Implement pipeline_validate_properties() ✅
- **Status**: COMPLETED
- **Location**: `bpipe/pipeline.c`
- **Implementation**:
  - ✅ Processes filters in linear order (topological sort deferred)
  - ✅ Propagates properties through each filter
  - ✅ Validates constraints at each step
  - ✅ Generates basic error messages
  - ⚠️ External inputs parameter exists but not used (nested pipelines not supported)

### 2.3 Basic Constraint Validation ✅
- **Status**: COMPLETED
- **Location**: `bpipe/properties.c`
- **Implementation**:
  - ✅ Constraint checking (EXISTS, EQ, GTE, LTE)
  - ✅ Basic error messages for failures
  - ⚠️ Limited validation path tracking (basic context only)

## Phase 3: Integration ✅ COMPLETED

### 3.1 Update Pipeline Start ✅
- **Status**: COMPLETED
- **Location**: `bpipe/pipeline.c`
- **Implementation**:
  - ✅ Calls `pipeline_validate_properties()` before starting filters
  - ✅ Returns error with message if validation fails
  - ✅ Only starts filters if validation passes
  - ✅ Error messages printed to stderr

### 3.2 Update Transform Filters ✅
- **Status**: COMPLETED
- **Files**: map.c, batch_matcher.c, sample_aligner.c, etc.
- **Implementation**:
  - ✅ Behaviors properly declared
  - ✅ Use constraint and behavior declarations
  - ✅ Properties computed during validation

### 3.3 Testing Infrastructure ✅
- **Status**: COMPLETED
- **Location**: `tests/test_property_validation.c`
- **Coverage**:
  - ✅ Successful validation paths
  - ✅ Constraint failure types
  - ✅ UNKNOWN property propagation
  - ⚠️ Complex DAG topologies (limited by linear validation)

## Phase 4: Advanced Features ⏳ IN PROGRESS

### 4.1 Multi-Input Alignment ✅
- **Status**: COMPLETED
- **Location**: `bpipe/properties.c`
- **Implementation**:
  - ✅ CONSTRAINT_OP_MULTI_INPUT_ALIGNED defined
  - ✅ `prop_validate_multi_input_alignment()` validates matching properties
  - ✅ Called during pipeline validation

### 4.2 Multi-Output Support ❌ NOT STARTED
- **Status**: NOT IMPLEMENTED
- **Blockers**: Requires structural changes to Filter_t
- **Tasks**:
  - Add MAX_OUTPUTS and output_properties array
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

### Completed Phases ✅
1. **Phase 0**: ✅ prop_set_all_unknown() and source filter migration complete
2. **Phase 1**: ✅ `prop_propagate()` correctly computes properties with multi-input support
3. **Phase 2**: ✅ Linear pipelines validate correctly
4. **Phase 3**: ✅ Integration complete, validation called during pipeline_start()

### Remaining Work
5. **Phase 4**: ⏳ Multi-output support and topological sort pending
6. **Phase 5**: ⏳ Complete filter migration and documentation updates

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

With Phases 0-3 complete, focus on:

1. **Multi-output support** (Phase 4.2) - Add structural support for multiple output ports
2. **Topological sort** (Phase 4) - Enable validation of complex DAG topologies
3. **Complete filter migration** (Phase 5) - Ensure all filters use property system
4. **Documentation polish** - Update all guides to reflect current implementation
5. **Advanced features** - Property negotiation, channel count support