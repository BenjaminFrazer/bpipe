# Property System Documentation Review

## Executive Summary
Review of CLAUDE.md, filter_capability_system.md, and pipeline_property_validation.md reveals several critical issues that need addressing, most notably that property behaviors are declared but never actually applied during validation.

## A) Clarifications Needed

### filter_capability_system.md
1. **Line 274-279**: Multi-port filter example references `PROP_CHANNEL_COUNT` but this property isn't defined in the property list or used elsewhere in the system. Need to either implement channel count property or remove from examples.
- this was just an example property applicable to this use-case. Mitigation: Example to clafify that this property doesn't exist and is ilustritive of a concept.

2. **Line 336-337**: `CONSTRAINT_OP_MULTI_INPUT_ALIGNED` is marked as "(proposed)" but used throughout examples. Unclear if this is implemented or planned. Should clearly separate implemented vs planned features.
- now implemented. 

3. **Line 366**: States "Property propagation: Actually apply declared behaviors" as remaining work, contradicting earlier sections that imply behaviors work. This needs urgent clarification.
- **CONFIRMED TODO**: `prop_propagate()` is implemented but NOT wired into validation. Validation uses static `output_properties` instead of computing them. Critical gap.

### pipeline_property_validation.md
1. **Line 18-19**: States "Top-level pipelines are self-contained with no external inputs" but line 262 mentions they could have external inputs. This contradiction needs resolution.
- No line 262 states that nested pipelines i.e. a non-toplevel pipeline contained within another pipeline can have external inputs. Mitigation: Clarify

2. **Line 96-98**: "Adaptive Behavior" example mentions BatchMatcher detecting downstream requirements but doesn't explain the mechanism for backward property flow in a forward topological traversal.
- the batch matcher cares about matching and guaranteeing batch capacity. It can do this by interrogating it's destination buffer. Mitigation: Clarify.

3. **Line 293**: Claims "complete encapsulation" but line 302 says designer must ensure contract matches topology. These statements are contradictory - either pipelines are black boxes or they require internal validation.
- Yes the internal validation of a Pipeline happens against it's publicly declared contract offline by the compliance test framework. Validation when in use just relies on the publically declared contract. Mitigation: Clarify.

## B) Problematic Corner Cases

### filter_capability_system.md
1. **UNKNOWN property cascade**: If upstream provides UNKNOWN and filter uses PRESERVE, output stays UNKNOWN. This could cascade through long filter chains, making debugging difficult. Need strategy for handling UNKNOWN propagation.
- I don't see the issue, if an unkown property propogates through the chain and causes a failure due to a downstream filter requireing a constraint then this is correct behaviour?
- **RESOLVED**: You're correct. This is working as designed - UNKNOWN propagation until constraint failure is the intended behavior for early error detection.

2. **Multi-input batch size mismatch**: Line 163-164 shows Mixer accepting different batch sizes (256 vs 128) but doesn't explain how it handles the operational mismatch. Need specification for batch size reconciliation.
- **NEEDS CLARIFICATION**: This is about runtime behavior - how does the mixer handle receiving 256 samples on one input and 128 on another? Buffer? Resample? Error?
- example is to illustrate the behaviour of properties not a spec for an actuall filter. Perhaps clarify this on the example?
- **RESOLVED**: The example is illustrative of property handling, not actual filter behavior. Docs should add note: "Example shows property validation only - actual runtime behavior is filter-specific"

3. **Circular validation dependencies**: No handling for filters that need downstream requirements to determine their outputs (e.g., adaptive filters). Need backward propagation or negotiation mechanism.
- I think this is too complex to consider at this stage.
- **DEFERRED**: Agreed. Can be addressed in future versions if needed.

### pipeline_property_validation.md
1. **Validation order paradox**: Line 169 mentions BatchMatcher "detected from sink" but validation is supposedly topological (forward). This suggests backward propagation that isn't explained.
- The backwards propogation happens at connection time. Validation happens afterwards. Mitigation: clarify.
- **RESOLVED**: Two-phase process: 1) Connection time (can inspect downstream), 2) Validation time (forward propagation only). Docs should clarify this separation.

2. **Partial batch ambiguity**: No specification for how UNKNOWN properties interact with `accepts_partial_fill` flag. What happens when batch size is UNKNOWN but filter accepts partial fills?
- we have a max_batch_size and a min_batch_size property but no single batch_size. accepts_partial_fill simply means that (max_batch_size==input buffer capacity) > (min_batch_size=1) does this answer the question.
- **RESOLVED**: Yes, this clarifies it. `accepts_partial_fill` sets min=1, max=buffer_capacity. If upstream batch sizes are UNKNOWN, validation fails only if filter has explicit batch size constraints.

3. **No error recovery**: When validation fails deep in nested pipeline, no mechanism suggests fixes or alternatives to users. Need better error handling strategy.
- Pipelines which are to be used as a component of other pipelines should be individually tested before being combined.
- **RESOLVED**: Testing strategy addresses this. Component pipelines must pass compliance tests before integration. Runtime errors should still provide clear diagnostics.

## C) Improvements Recommended

### CLAUDE.md
1. Add reference to property system documentation in mandatory reading section
- yes.
2. Include `make lint` in pre-commit workflow emphasis  
- yes.
3. Add example of reading specs before implementation

### filter_capability_system.md
1. **Add property state table**: Create comprehensive table showing all properties, possible states (including UNKNOWN), and which filters typically set/require them.
- yes.

2. **Add validation flowchart**: Visual diagram of validation sequence would significantly improve understanding.
- yes, but don't duplicate what is in other docs consider reffering out.

3. **Clarify behavior precedence**: Specify what happens if filter declares both SET and PRESERVE for same property.
- Is there any scenario in which this is not a user error?
- **RESOLVED**: No valid scenario exists. This should be a compile-time or init-time error. Add assertion to prevent this.

4. **Add troubleshooting guide**: Document common validation failures with solutions.
yes.

5. **Fix feature status**: Remove "(proposed)" markers or clearly separate implemented vs planned features in dedicated sections.
yes.

### pipeline_property_validation.md
1. **Explain backward propagation**: Clarify how BatchMatcher can "detect from sink" during forward topological traversal. Either this is a two-pass algorithm or there's missing information.
- yes, this is done at connection time. so a two part algorithm. I

2. **Add state machine diagram**: Visualize property states and transitions during validation process.
- yes.

3. **Specify UNKNOWN handling**: Document comprehensive rules for UNKNOWN propagation and when it triggers failures.
- if a property is targeted by any input constraint and is not known, including multi-input alignment constraints this is a failure.
- **RESOLVED**: Clear rule: UNKNOWN fails validation if ANY constraint (including multi-input alignment) requires that property. This should be documented explicitly.


4. **Add implementation example**: Show actual C code for validation algorithm, not just pseudo-code.
- This is a requirements document. Behaviour should be unambigously defined but seperate from implementation.
- **RESOLVED**: Correct - keep requirements separate from implementation. Pseudo-code is appropriate for specification docs.

5. **Resolve encapsulation model**: Choose either true black-box pipelines or pipelines requiring internal validation - current docs are contradictory.
- pipelines are black boxes, AND publicly declared contract is interally validated offline as a part of the unit test framework.
- **RESOLVED**: Both are true - black-box at runtime, internal validation during testing. This dual nature should be clearly explained in docs.

## Critical Issues

### 1. Property Behaviors Not Applied (HIGHEST PRIORITY)
The most critical issue is that property behaviors are declared but never actually applied (filter_capability_system.md:366). The system only checks constraints against declared properties, not computed properties after transformations. This breaks the core promise of validating actual runtime behavior.

**Required fix**: Implement `prop_propagate()` function that applies behaviors to compute output properties, then wire it into validation flow.

### 2. Missing Implementation Details
Several core features appear unimplemented:
- ~~Multi-input alignment validation~~ (NOW IMPLEMENTED)
- Pipeline-wide validation 
- Property propagation through behaviors (function exists but not wired up)
- Error message retrieval

### 3. Inconsistent Validation Model
Documents describe both:
- Forward-only topological traversal
- Backward detection of downstream requirements
- Forward only validation of filters.

These are incompatible without a two-pass algorithm or negotiation protocol.
- **RESOLVED**: It's a two-phase process: 1) Connection time (filters can inspect downstream), 2) Validation time (forward-only propagation). Docs need to clearly separate these phases.

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

## Resolution Summary

After review and discussion, the following clarifications emerged:

### Design Clarifications
- **UNKNOWN propagation**: Working as designed - fails at constraint validation, not earlier
- **Two-phase process**: Connection time (can inspect downstream) vs Validation time (forward-only)
- **Pipeline encapsulation**: Black-box at runtime, internally validated during testing
- **Batch size properties**: min_batch_size and max_batch_size, not single batch_size
- **SET+PRESERVE conflict**: Should be an init-time error, no valid use case

### Remaining Critical Issues
1. **Property behaviors not wired up** - `prop_propagate()` exists but unused (HIGHEST PRIORITY)
2. **Documentation gaps** - Several sections need clarification per discussions above

### Deferred Items
- Circular validation dependencies (too complex for current stage)
- Negotiation protocols (future enhancement)
