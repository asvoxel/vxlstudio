# VxlApp Examples

## pcb_demo

PCB solder joint inspection using SimCamera.

### Build the .vxap package

```bash
cd VxlStudio/
python tools/vxap_pack.py vxlapp/examples/pcb_demo/ vxlapp/examples/pcb_demo.vxap -v
```

### Run

```bash
# List pipeline steps
python -m vxlapp.main vxlapp/examples/pcb_demo.vxap --list-steps

# Run once (headless)
python -m vxlapp.main vxlapp/examples/pcb_demo.vxap --once

# Run with GUI (falls back to headless if dearpygui not installed)
python -m vxlapp.main vxlapp/examples/pcb_demo.vxap
```
