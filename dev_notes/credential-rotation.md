# Credential rotation record (PCKNOB-02b)

The repository is public. The 2026-09-12 sweep found three live credentials in
the tree; two are in the remote's history. Rotation happens at the providers and
is recorded here. Deleting a value from HEAD does nothing for history.

| Credential | Where it was | On the remote? | Since | Rotated on | Rotated by | New value stored where |
|---|---|---|---|---|---|---|
| HiveMQ broker host / username / password | QNOB: `PC/src/mqtt_handler.py:22`, `config_handler.py:43-47`, `qnob_config.json`, two committed `.pyc`. homio-round-hmi: `esp-idf/PC/src/*` | yes — QNOB (`master`, `task/esp-idf-migration`) **and** homio-round-hmi (`main`, `codex/multi-board-support`) | QNOB `eb894ea` 2025-03-28 | _pending_ | | PC app config only (PCKNOB-02, Phase 1) |
| OpenWeatherMap API key | `WeatherHandler.h:28` default argument; sent over plaintext HTTP | yes — QNOB, both branches | `bf2dc2f` | _pending_ | | NVS via a `configureWeather` command (NET-28) |
| Bench WiFi SSID / PSK | `esp-idf/sdkconfig.defaults.esp32c6:18-19` | **yes** — homio-round-hmi (public), `main` and `codex/multi-board-support`; not on QNOB | homio `84e66e8` "Re-root repository at ESP" | _pending — change the WiFi password_ | | `esp-idf/sdkconfig.defaults.local` (untracked overlay, BLD-20) |

Both repositories are public: `github.com/alperbasarn/QNOB` and the nested
firmware repository `github.com/alperbasarn/homio-round-hmi` (checked out at
`screen/SW/ESP`). An earlier check covered only the QNOB remote and wrongly
concluded the WiFi password was private.

## Done in Phase 0 (BLD-20)

- `sdkconfig.defaults.esp32c6` now carries empty `CONFIG_QNOB_WIFI_SSID` /
  `CONFIG_QNOB_WIFI_PASSWORD`. `WiFiManager.cpp:298` only uses the defaults when
  `strlen(CONFIG_QNOB_WIFI_SSID) > 0`, so a provisioned device is unaffected.
- The bench values moved to `esp-idf/sdkconfig.defaults.local`, ignored by
  `screen/SW/ESP/.gitignore`; `build-variant.{sh,ps1}` append it to
  `SDKCONFIG_DEFAULTS` when present. `sdkconfig.defaults.local.example` documents it.
- Gate before tagging `p0-baseline` (BLD-03):
  `git grep -iE 'QNOB_WIFI_(SSID|PASSWORD)="[^"]'` must return nothing.

## Open question (section 9 of the WBS)

Rewrite git history for the two public credentials, or accept the old values as
burned once rotated? Rewriting invalidates every clone and requires a force-push;
accepting them as burned is sufficient once the providers reject the old values.

Decision: _pending_

2026-09-12 (NET-28): the OpenWeatherMap literal is gone from `WeatherHandler.h`; the key now comes from `CONFIG_QNOB_WEATHER_API_KEY`, kept in the untracked `sdkconfig.defaults.local` (see `sdkconfig.defaults.local.example`). The old value stays burned until it is rotated at the provider.

2026-09-12 (WP1.4 commit): the same key was also in `arduino/WeatherHandler.h` (blanked) and inside the tracked OTA image `esp-idf/ota/qnub-screen.bin` (untracked; `esp-idf/ota/` is ignored; the file stays on disk). History carries both until the key is rotated.

2026-10-23 (merge of `main`, `integration-main-log.md`): main's tree carried the weather key again, as a literal fallback in `InternetHandler.cpp` and inside its committed build output (`src/esp-idf/build/`, `build_s3_amoled175/`), the bench PSK in `sdkconfig.defaults.esp32c6`, and the broker host, user and password in three PC-app files. The merged index carries none of them; main's history still does. Rotation stays the owner action.
