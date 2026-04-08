# QNOB Screen Test Project

Standalone ESP-IDF project for display/touch bring-up.

Build and flash:

```bash
idf.py -C screen-test set-target esp32c6
idf.py -C screen-test build
idf.py -C screen-test -p <PORT> flash monitor
```

Expected boot log line:

- `SCREEN_TEST: Starting screen/touch test app`
