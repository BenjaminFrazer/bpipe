# Property System Documentation Review

## Executive Summary
Review of CLAUDE.md, filter_capability_system.md, and pipeline_property_validation.md reveals several critical issues that need addressing, most notably that property behaviors are declared but never actually applied during validation.

## A) Clarifications Needed

### filter_capability_system.md
1. **Line 274-279**: Multi-port filter example references `PROP_CHANNEL_COUNT` but this property isn't defined in the property list or used elsewhere in the system. Need to either implement channel count property or remove from examples.

2. **Line 336-337**: `CONSTRAINT_OP_MULTI_INPUT_ALIGNED` is marked as "(proposed)" but used throughout examples. Unclear if this is implemented or planned. Should clearly separate implemented vs planned features.

3. **Line 366**: States "Property propagation: Actually apply declared behaviors" as remaining work, contradicting earlier sections that imply behaviors work. This needs urgent clarification.

### pipeline_property_validation.md
1. **Line 18-19**: States "Top-level pipelines are self-contained with no external inputs" but line 262 mentions they could have external inputs. This contradiction needs resolution.

2. **Line 96-98**: "Adaptive Behavior" example mentions BatchMatcher detecting downstream requirements but doesn't explain the mechanism for backward property flow in a forward topological traversal.

3. **Line 293**: Claims "complete encapsulation" but line 302 says designer must ensure contract matches topology. These statements are contradictory - either pipelines are black boxes or they require internal validation.

## B) Problematic Corner Cases

### filter_capability_system.md
1. **UNKNOWN property cascade**: If upstream provides UNKNOWN and filter uses PRESERVE, output stays UNKNOWN. This could cascade through long filter chains, making debugging difficult. Need strategy for handling UNKNOWN propagation.

2. **Multi-input batch size mismatch**: Line 163-164 shows Mixer accepting different batch sizes (256 vs 128) but doesn't explain how it handles the operational mismatch. Need specification for batch size reconciliation.

3. **Circular validation dependencies**: No handling for filters that need downstream requirements to determine their outputs (e.g., adaptive filters). Need backward propagation or negotiation mechanism.

### pipeline_property_validation.md
1. **Validation order paradox**: Line 169 mentions BatchMatcher "detected from sink" but validation is supposedly topological (forward). This suggests backward propagation that isn't explained.

2. **Partial batch ambiguity**: No specification for how UNKNOWN properties interact with `accepts_partial_fill` flag. What happens when batch size is UNKNOWN but filter accepts partial fills?

3. **No error recovery**: When validation fails deep in nested pipeline, no mechanism suggests fixes or alternatives to users. Need better error handling strategy.

## C) Improvements Recommended

### CLAUDE.md
1. Add reference to property system documentation in mandatory reading section
2. Include `make lint` in pre-commit workflow emphasis  
3. Add example of reading specs before implementation

### filter_capability_system.md
1. **Add property state table**: Create comprehensive table showing all properties, possible states (including UNKNOWN), and which filters typically set/require them.

2. **Add validation flowchart**: Visual diagram of validation sequence would significantly improve understanding.

3. **Clarify behavior precedence**: Specify what happens if filter declares both SET and PRESERVE for same property.

4. **Add troubleshooting guide**: Document common validation failures with solutions.

5. **Fix feature status**: Remove "(proposed)" markers or clearly separate implemented vs planned features in dedicated sections.

### pipeline_property_validation.md
1. **Explain backward propagation**: Clarify how BatchMatcher can "detect from sink" during forward topological traversal. Either this is a two-pass algorithm or there's missing information.

2. **Add state machine diagram**: Visualize property states and transitions during validation process.

3. **Specify UNKNOWN handling**: Document comprehensive rules for UNKNOWN propagation and when it triggers failures.

4. **Add implementation example**: Show actual C code for validation algorithm, not just pseudo-code.

5. **Resolve encapsulation model**: Choose either true black-box pipelines or pipelines requiring internal validation - current docs are contradictory.

## Critical Issues

### 1. Property Behaviors Not Applied (HIGHEST PRIORITY)
The most critical issue is that property behaviors are declared but never actually applied (filter_capability_system.md:366). The system only checks constraints against declared properties, not computed properties after transformations. This breaks the core promise of validating actual runtime behavior.

**Required fix**: Implement `prop_propagate()` function that applies behaviors to compute output properties, then wire it into validation flow.

### 2. Missing Implementation Details
Several core features appear unimplemented:
- Multi-input alignment validation
- Pipeline-wide validation 
- Property propagation through behaviors
- Error message retrieval

### 3. Inconsistent Validation Model
Documents describe both:
- Forward-only topological traversal
- Backward detection of downstream requirements

These are incompatible without a two-pass algorithm or negotiation protocol.

## Recommendations Priority

1. **Immediate**: Fix property behavior application - system is broken without this
2. **High**: Clarify validation algorithm (one-pass vs two-pass)
3. **High**: Resolve pipeline encapsulation contradictions
4. **Medium**: Document UNKNOWN property handling comprehensively
5. **Medium**: Separate implemented vs proposed features
6. **Low**: Add visual diagrams and troubleshooting guides

## Testing Gaps

The documentation mentions testing needs but doesn't specify:
- How to test property propagation through complex DAGs
- How to verify multi-input alignment 
- How to test UNKNOWN property handling
- How to validate nested pipeline contracts

These test specifications should be added to ensure proper implementation.

## Conclusion

The property system has solid conceptual design but significant implementation gaps. The most critical issue is that property behaviors aren't actually applied during validation, making the system unable to fulfill its core purpose. This should be fixed immediately before building additional features on top of a broken foundation.