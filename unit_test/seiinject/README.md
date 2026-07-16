# Unit test for amba_seiinject

Tests the `amba_seiinject` GStreamer element (injects timestamp and GPS SEI into H.264/H.265).

## Build

- **Ambarella build** (amba.mk): `test-seiinject` is built as part of `ambagst-test` package.
- **ambabuild** (Makefile): from package root, run:
  ```bash
  make -C unit_test/seiinject
  ```
  Binary: `$(INTERNAL_BIN_DIR)/test-seiinject`

## Run

Ensure `libgstamba.so` is loadable by GStreamer (e.g. install to system or set `GST_PLUGIN_PATH`):

```bash
export GST_PLUGIN_PATH=/path/to/dir/containing/libgstamba.so
./test-seiinject
```

### Usage

1. **No arguments** – Built-in test: push minimal H.264 NAL via appsrc → `amba_seiinject` → fakesink. No file needed.
2. **One argument** – Input H.264 file (Annex B byte-stream): `./test-seiinject input.h264`  
   Output: `/tmp/seiinject_out.h264`
3. **Two arguments** – Input and output: `./test-seiinject input.h264 output.h264`

Input file must be H.264 Annex B byte-stream (e.g. from `gst-launch-1.0 ... ! filesink` or similar).

## Example with gst-launch (manual test)

```bash
# Inject SEI from file (byte-stream)
gst-launch-1.0 filesrc location=in.h264 ! video/x-h264, stream-format=byte-stream ! \
  amba_seiinject add-timestamp=true add-gps=false ! \
  filesink location=out.h264

# With GPS device
gst-launch-1.0 filesrc location=in.h264 ! video/x-h264 ! \
  amba_seiinject gps-device=/dev/ttyUSB0 ! filesink location=out.h264
```
