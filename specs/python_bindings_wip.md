# Python bindings
- This spec is a WIP.

goals:
- Minimal boilerplate.
    - ideally just a single generic python filter object which can initialise and wrap around any filter.
    - all calls are made to the filter's opps interface
- python layer configuration and composition
    - configiguraiton should be intutive and high level.


## Example python usage

### Sine-triangle PWM
```python
from bpipe import filt

f = 1e3 # 1kHz
Ts = 1/1e6 # 1M
sg = filt.create_siggen(waveform="sin", omega=2*pi*f, sample_rate=Ts, phase_rad=0)
log = filt.create_csv_logger(dtype=float)
ge = filt.create_element_wise(opp="ge"


```
